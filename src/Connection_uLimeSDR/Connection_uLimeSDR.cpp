/**
@file Connection_uLimeSDR.cpp
@author Lime Microsystems
@brief Implementation of uLimeSDR board connection.
*/

#include "Connection_uLimeSDR.h"
#include "ErrorReporting.h"
#include <cstring>
#include <iostream>

#include <thread>
#include <chrono>
#include <FPGA_common.h>
#include <LMS7002M.h>
#include <ciso646>

using namespace std;
using namespace lime;

Connection_uLimeSDR::Connection_uLimeSDR(void *arg)
{
    mTimestampOffset = 0;
    rxLastTimestamp.store(0);
    mExpectedSampleRate = 0;
    generateData.store(false);
    rxRunning.store(false);
    txRunning.store(false);
    isConnected = false;
    terminateRx.store(false);
    terminateTx.store(false);
    rxDataRate_Bps.store(0);
    txDataRate_Bps.store(0);

    mStreamWrEndPtAddr = 0x03;
    mStreamRdEndPtAddr = 0x83;
    isConnected = false;
    txSize = 0;
    rxSize = 0;
#ifndef __unix__
	mFTHandle = NULL;
#else
    dev_handle = 0;
    devs = 0;
	mUsbCounter = 0;
    ctx = (libusb_context *)arg;
#endif
}

/**	@brief Initializes port type and object necessary to communicate to usb device.
*/
Connection_uLimeSDR::Connection_uLimeSDR(void *arg, const unsigned index, const int vid, const int pid)
{
    mTimestampOffset = 0;
    rxLastTimestamp.store(0);
    mExpectedSampleRate = 0;
    generateData.store(false);
    rxRunning.store(false);
    txRunning.store(false);
    isConnected = false;
    terminateRx.store(false);
    terminateTx.store(false);
    rxDataRate_Bps.store(0);
    txDataRate_Bps.store(0);

    mStreamWrEndPtAddr = 0x03;
    mStreamRdEndPtAddr = 0x83;
    isConnected = false;
    txSize = 0;
    rxSize = 0;
#ifndef __unix__
    mFTHandle = NULL;
#else
    dev_handle = 0;
    devs = 0;
	mUsbCounter = 0;
    ctx = (libusb_context *)arg;
#endif
    if(this->Open(index, vid, pid) != 0)
        std::cerr << GetLastErrorMessage() << std::endl;
}

/**	@brief Closes connection to chip and deallocates used memory.
*/
Connection_uLimeSDR::~Connection_uLimeSDR()
{
    for(auto i : mTxStreams)
        ControlStream((size_t)i, false);
    for(auto i : mRxStreams)
        ControlStream((size_t)i, false);
    for(auto i : mTxStreams)
        CloseStream((size_t)i);
    for(auto i : mRxStreams)
        CloseStream((size_t)i);
    UpdateThreads();
    Close();
}
#ifdef __unix__
int Connection_uLimeSDR::FT_FlushPipe(unsigned char ep)
{
    int actual = 0;
    unsigned char wbuffer[20]={0};

    mUsbCounter++;
    wbuffer[0] = (mUsbCounter)&0xFF;
    wbuffer[1] = (mUsbCounter>>8)&0xFF;
    wbuffer[2] = (mUsbCounter>>16)&0xFF;
    wbuffer[3] = (mUsbCounter>>24)&0xFF;
    wbuffer[4] = ep;
    libusb_bulk_transfer(dev_handle, 0x01, wbuffer, 20, &actual, 1000);
    if (actual != 20)
        return -1;

    mUsbCounter++;
    wbuffer[0] = (mUsbCounter)&0xFF;
    wbuffer[1] = (mUsbCounter>>8)&0xFF;
    wbuffer[2] = (mUsbCounter>>16)&0xFF;
    wbuffer[3] = (mUsbCounter>>24)&0xFF;
    wbuffer[4] = ep;
    wbuffer[5] = 0x03;
    libusb_bulk_transfer(dev_handle, 0x01, wbuffer, 20, &actual, 1000);
    if (actual != 20)
        return -1;
    return 0;
}

int Connection_uLimeSDR::FT_SetStreamPipe(unsigned char ep, size_t size)
{

    int actual = 0;
    unsigned char wbuffer[20]={0};

    mUsbCounter++;
    wbuffer[0] = (mUsbCounter)&0xFF;
    wbuffer[1] = (mUsbCounter>>8)&0xFF;
    wbuffer[2] = (mUsbCounter>>16)&0xFF;
    wbuffer[3] = (mUsbCounter>>24)&0xFF;
    wbuffer[4] = ep;
    libusb_bulk_transfer(dev_handle, 0x01, wbuffer, 20, &actual, 1000);
    if (actual != 20)
        return -1;

    mUsbCounter++;
    wbuffer[0] = (mUsbCounter)&0xFF;
    wbuffer[1] = (mUsbCounter>>8)&0xFF;
    wbuffer[2] = (mUsbCounter>>16)&0xFF;
    wbuffer[3] = (mUsbCounter>>24)&0xFF;
    wbuffer[5] = 0x02;
    wbuffer[8] = (size)&0xFF;
    wbuffer[9] = (size>>8)&0xFF;
    wbuffer[10] = (size>>16)&0xFF;
    wbuffer[11] = (size>>24)&0xFF;
    libusb_bulk_transfer(dev_handle, 0x01, wbuffer, 20, &actual, 1000);
    if (actual != 20)
        return -1;
    return 0;
}
#endif

/**	@brief Tries to open connected USB device and find communication endpoints.
@return Returns 0-Success, other-EndPoints not found or device didn't connect.
*/
int Connection_uLimeSDR::Open(const unsigned index, const int vid, const int pid)
{
#ifndef __unix__
	DWORD devCount;
	FT_STATUS ftStatus = FT_OK;
	DWORD dwNumDevices = 0;
	//
	// Open a device
	//
	ftStatus = FT_Create(0, FT_OPEN_BY_INDEX, &mFTHandle);
	if (FT_FAILED(ftStatus))
	{
		ReportError(ENODEV, "Failed to list USB Devices");
		return -1;
	}
	FT_AbortPipe(mFTHandle, mStreamRdEndPtAddr);
	FT_AbortPipe(mFTHandle, 0x82);
	FT_AbortPipe(mFTHandle, 0x02);
	FT_AbortPipe(mFTHandle, mStreamWrEndPtAddr);
	FT_SetStreamPipe(mFTHandle, FALSE, FALSE, 0x82, 64);
	FT_SetStreamPipe(mFTHandle, FALSE, FALSE, 0x02, 64);
	isConnected = true;
	return 0;
#else
    dev_handle = libusb_open_device_with_vid_pid(ctx, vid, pid);

    if(dev_handle == nullptr)
        return ReportError(ENODEV, "libusb_open failed");
    libusb_reset_device(dev_handle);
    if(libusb_kernel_driver_active(dev_handle, 1) == 1)   //find out if kernel driver is attached
    {
        printf("Kernel Driver Active\n");
        if(libusb_detach_kernel_driver(dev_handle, 1) == 0) //detach it
            printf("Kernel Driver Detached!\n");
    }
    int r = libusb_claim_interface(dev_handle, 1); //claim interface 0 (the first) of device
    if(r < 0)
    {
        printf("Cannot Claim Interface\n");
        return ReportError(-1, "Cannot claim interface - %s", libusb_strerror(libusb_error(r)));
    }
    r = libusb_claim_interface(dev_handle, 1); //claim interface 0 (the first) of device
    if(r < 0)
    {
        printf("Cannot Claim Interface\n");
        return ReportError(-1, "Cannot claim interface - %s", libusb_strerror(libusb_error(r)));
    }
    printf("Claimed Interface\n");

    FT_SetStreamPipe(0x82,64);
    FT_SetStreamPipe(0x02,64);
    isConnected = true;
    return 0;
#endif
}

/**	@brief Closes communication to device.
*/
void Connection_uLimeSDR::Close()
{
#ifndef __unix__
	FT_Close(mFTHandle);
#else
    if(dev_handle != 0)
    {
        FT_FlushPipe(mStreamRdEndPtAddr);
        FT_FlushPipe(0x82);
        libusb_release_interface(dev_handle, 1);
        libusb_close(dev_handle);
        dev_handle = 0;
    }
#endif
    isConnected = false;
}

/**	@brief Returns connection status
@return 1-connection open, 0-connection closed.
*/
bool Connection_uLimeSDR::IsOpen()
{
    return isConnected;
}

/**	@brief Sends given data buffer to chip through USB port.
@param buffer data buffer, must not be longer than 64 bytes.
@param length given buffer size.
@param timeout_ms timeout limit for operation in milliseconds
@return number of bytes sent.
*/
int Connection_uLimeSDR::Write(const unsigned char *buffer, const int length, int timeout_ms)
{
    std::lock_guard<std::mutex> lock(mExtraUsbMutex);
    long len = 0;
    if(IsOpen() == false)
        return 0;

    unsigned char* wbuffer = new unsigned char[length];
    memcpy(wbuffer, buffer, length);
#ifndef __unix__

	//
	// Write to channel 1 ep 0x02
	//
	ULONG ulBytesWrite = 0;
	FT_STATUS ftStatus = FT_OK;
	OVERLAPPED	vOverlapped = { 0 };
	memset(&vOverlapped, 0, sizeof(OVERLAPPED));
	vOverlapped.hEvent = CreateEvent(NULL, false, false, NULL);
	ftStatus = FT_WritePipe(mFTHandle, 0x02, (unsigned char*)buffer, length, &ulBytesWrite, &vOverlapped);
	if (ftStatus != FT_IO_PENDING)
	{
		CloseHandle(vOverlapped.hEvent);
		return -1;
	}

	DWORD dwRet = WaitForSingleObject(vOverlapped.hEvent, timeout_ms);
	if (dwRet == WAIT_OBJECT_0 || dwRet == WAIT_TIMEOUT)
	{
		if (GetOverlappedResult(mFTHandle, &vOverlapped, &ulBytesWrite, FALSE)==FALSE)
		{
			ulBytesWrite = -1;
		}
	}
	else
	{
		ulBytesWrite = -1;
	}
	CloseHandle(vOverlapped.hEvent);
	return ulBytesWrite;
#else
    int actual = 0;
    libusb_bulk_transfer(dev_handle, 0x02, wbuffer, length, &actual, timeout_ms);
    len = actual;
#endif
    delete[] wbuffer;
    return len;
}

/**	@brief Reads data coming from the chip through USB port.
@param buffer pointer to array where received data will be copied, array must be
big enough to fit received data.
@param length number of bytes to read from chip.
@param timeout_ms timeout limit for operation in milliseconds
@return number of bytes received.
*/
int Connection_uLimeSDR::Read(unsigned char *buffer, const int length, int timeout_ms)
{
    std::lock_guard<std::mutex> lock(mExtraUsbMutex);
    long len = length;
    if(IsOpen() == false)
        return 0;
#ifndef __unix__
	//
	// Read from channel 1 ep 0x82
	//
	ULONG ulBytesRead = 0;
	FT_STATUS ftStatus = FT_OK;
	OVERLAPPED	vOverlapped = { 0 };
	memset(&vOverlapped, 0, sizeof(OVERLAPPED));
	vOverlapped.hEvent = CreateEvent(NULL, false, false, NULL);
	ftStatus = FT_ReadPipe(mFTHandle, 0x82, buffer, length, &ulBytesRead, &vOverlapped);
	if (ftStatus != FT_IO_PENDING)
	{
		CloseHandle(vOverlapped.hEvent);
		return -1;;
	}

	DWORD dwRet = WaitForSingleObject(vOverlapped.hEvent, timeout_ms);
	if (dwRet == WAIT_OBJECT_0 || dwRet == WAIT_TIMEOUT)
	{
		if (GetOverlappedResult(mFTHandle, &vOverlapped, &ulBytesRead, FALSE)==FALSE)
		{
			ulBytesRead = -1;
		}
	}
	else
	{
		ulBytesRead = -1;
	}
	CloseHandle(vOverlapped.hEvent);
	return ulBytesRead;
#else
    int actual = 0;
    libusb_bulk_transfer(dev_handle, 0x82, buffer, len, &actual, timeout_ms);
    len = actual;
#endif
    return len;
}

#ifdef __unix__
/**	@brief Function for handling libusb callbacks
*/
static void callback_libusbtransfer(libusb_transfer *trans)
{
    Connection_uLimeSDR::USBTransferContext *context = reinterpret_cast<Connection_uLimeSDR::USBTransferContext*>(trans->user_data);
    switch(trans->status)
    {
    case LIBUSB_TRANSFER_CANCELLED:
        //printf("Transfer %i canceled\n", context->id);
        context->bytesXfered = trans->actual_length;
        context->done.store(true);
        //context->used = false;
        //context->reset();
        break;
    case LIBUSB_TRANSFER_COMPLETED:
        //if(trans->actual_length == context->bytesExpected)
    {
        context->bytesXfered = trans->actual_length;
        context->done.store(true);
    }
    break;
    case LIBUSB_TRANSFER_ERROR:
        printf("TRANSFER ERRRO\n");
        context->bytesXfered = trans->actual_length;
        context->done.store(true);
        //context->used = false;
        break;
    case LIBUSB_TRANSFER_TIMED_OUT:
        //printf("transfer timed out %i\n", context->id);
        context->bytesXfered = trans->actual_length;
        context->done.store(true);
        //context->used = false;

        break;
    case LIBUSB_TRANSFER_OVERFLOW:
        printf("transfer overflow\n");

        break;
    case LIBUSB_TRANSFER_STALL:
        printf("transfer stalled\n");
        break;
    case LIBUSB_TRANSFER_NO_DEVICE:
        printf("transfer no device\n");

        break;
    }
    context->transferLock.unlock();
    context->cv.notify_one();
}
#endif

/**
@brief Starts asynchronous data reading from board
@param *buffer buffer where to store received data
@param length number of bytes to read
@return handle of transfer context
*/
int Connection_uLimeSDR::BeginDataReading(char *buffer, uint32_t length)
{
    int i = 0;
    bool contextFound = false;
    //find not used context
    for(i = 0; i<USB_MAX_CONTEXTS; i++)
    {
        if(!contexts[i].used)
        {
            contextFound = true;
            break;
        }
    }
    if(!contextFound)
    {
        printf("No contexts left for reading data\n");
        return -1;
    }
    contexts[i].used = true;
#ifndef __unix__
	if (length != rxSize)
	{
		rxSize = length;
		FT_SetStreamPipe(mFTHandle, FALSE, FALSE, mStreamRdEndPtAddr, rxSize);
	}
	memset(&contexts[i].inOvLap, 0, sizeof(OVERLAPPED));
	contexts[i].inOvLap.hEvent = CreateEvent(NULL, false, false, NULL);
	ULONG ulActual;
	if (FT_ReadPipe(mFTHandle, mStreamRdEndPtAddr, (unsigned char*)buffer, length, &ulActual, &contexts[i].inOvLap)!= FT_IO_PENDING)
	{
		return -1;
	}
#else
    if (length != rxSize)
    {
        rxSize = length;
        FT_SetStreamPipe(mStreamRdEndPtAddr,rxSize);
    }
    unsigned int Timeout = 500;
    libusb_transfer *tr = contexts[i].transfer;
    libusb_fill_bulk_transfer(tr, dev_handle, mStreamRdEndPtAddr, (unsigned char*)buffer, length, callback_libusbtransfer, &contexts[i], Timeout);
    contexts[i].done = false;
    contexts[i].bytesXfered = 0;
    contexts[i].bytesExpected = length;
    int status = libusb_submit_transfer(tr);
    if(status != 0)
    {
        printf("ERROR BEGIN DATA READING %s\n", libusb_error_name(status));
        contexts[i].used = false;
        return -1;
    }
    else
        contexts[i].transferLock.lock();
#endif
    return i;
}

/**
@brief Waits for asynchronous data reception
@param contextHandle handle of which context data to wait
@param timeout_ms number of miliseconds to wait
@return 1-data received, 0-data not received
*/
int Connection_uLimeSDR::WaitForReading(int contextHandle, unsigned int timeout_ms)
{
    if(contextHandle >= 0 && contexts[contextHandle].used == true)
    {
        int status = 0;
#ifndef __unix__
		contexts[contextHandle].inOvLap.InternalHigh = 0;
		DWORD dwRet = WaitForSingleObject(contexts[contextHandle].inOvLap.hEvent, timeout_ms);
		if (dwRet == WAIT_OBJECT_0 || dwRet == WAIT_TIMEOUT)
			return 1;
#else
        auto t1 = chrono::high_resolution_clock::now();
        auto t2 = chrono::high_resolution_clock::now();

        std::unique_lock<std::mutex> lck(contexts[contextHandle].transferLock);
        while(contexts[contextHandle].done.load() == false && std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() < timeout_ms)
        {
            //blocking not to waste CPU
            contexts[contextHandle].cv.wait(lck);
            t2 = chrono::high_resolution_clock::now();
        }
        return contexts[contextHandle].done.load() == true;
#endif
    }
    return 0;
}

/**
@brief Finishes asynchronous data reading from board
@param buffer array where to store received data
@param length number of bytes to read
@param contextHandle handle of which context to finish
@return false failure, true number of bytes received
*/
int Connection_uLimeSDR::FinishDataReading(char *buffer, uint32_t length, int contextHandle)
{
    if(contextHandle >= 0 && contexts[contextHandle].used == true)
    {
#ifndef __unix__
		ULONG ulActualBytesTransferred;

		if (GetOverlappedResult(mFTHandle, &contexts[contextHandle].inOvLap, &ulActualBytesTransferred, FALSE) == FALSE)
		{
			return -1;
		}
		length = ulActualBytesTransferred;
		contexts[contextHandle].used = false;
		return length;
#else
        length = contexts[contextHandle].bytesXfered;
        contexts[contextHandle].used = false;
        contexts[contextHandle].reset();
        return length;
#endif
    }
    else
        return 0;
}

/**
@brief Aborts reading operations
*/
void Connection_uLimeSDR::AbortReading()
{
#ifndef __unix__
	FT_AbortPipe(mFTHandle, mStreamRdEndPtAddr);
	for (int i = 0; i < USB_MAX_CONTEXTS; ++i)
	{
		if (contexts[i].used == true)
		{
			CloseHandle(contexts[i].inOvLap.hEvent);
			contexts[i].used = false;
		}
	}
	rxSize = 0;
#else

    for(int i = 0; i<USB_MAX_CONTEXTS; ++i)
    {
        if(contexts[i].used)
            libusb_cancel_transfer(contexts[i].transfer);
    }
    FT_FlushPipe(mStreamRdEndPtAddr);
    rxSize = 0;
#endif
}

/**
@brief Starts asynchronous data Sending to board
@param *buffer buffer to send
@param length number of bytes to send
@return handle of transfer context
*/
int Connection_uLimeSDR::BeginDataSending(const char *buffer, uint32_t length)
{
    int i = 0;
    //find not used context
    bool contextFound = false;
    for(i = 0; i<USB_MAX_CONTEXTS; i++)
    {
        if(!contextsToSend[i].used)
        {
            contextFound = true;
            break;
        }
    }
    if(!contextFound)
        return -1;
        contextsToSend[i].used = true;
#ifndef __unix__
	FT_STATUS ftStatus = FT_OK;
	ULONG ulActualBytesSend;
	if (length != txSize)
	{
		txSize = length;
		FT_SetStreamPipe(mFTHandle, FALSE, FALSE, mStreamWrEndPtAddr, txSize);
	}
	memset(&contextsToSend[i].inOvLap, 0, sizeof(OVERLAPPED));
	contextsToSend[i].inOvLap.hEvent = CreateEvent(NULL, false, false, NULL);
	ftStatus = FT_WritePipe(mFTHandle, mStreamWrEndPtAddr, (unsigned char*)buffer, length, &ulActualBytesSend, &contextsToSend[i].inOvLap);
	if (ftStatus != FT_IO_PENDING)
	{
		return -1;
	}
#else
    if (length != txSize)
    {
        txSize = length;
        FT_SetStreamPipe(mStreamWrEndPtAddr,txSize);
    }
    unsigned int Timeout = 500;
    libusb_transfer *tr = contextsToSend[i].transfer;
    libusb_fill_bulk_transfer(tr, dev_handle, mStreamWrEndPtAddr, (unsigned char*)buffer, length, callback_libusbtransfer, &contextsToSend[i], Timeout);
    contextsToSend[i].done = false;
    contextsToSend[i].bytesXfered = 0;
    contextsToSend[i].bytesExpected = length;
    int status = libusb_submit_transfer(tr);
    if(status != 0)
    {
        printf("ERROR BEGIN DATA SENDING %s\n", libusb_error_name(status));
        contextsToSend[i].used = false;
        return -1;
    }
    else
        contextsToSend[i].transferLock.lock();
#endif
    return i;
}

/**
@brief Waits for asynchronous data sending
@param contextHandle handle of which context data to wait
@param timeout_ms number of miliseconds to wait
@return 1-data received, 0-data not received
*/
int Connection_uLimeSDR::WaitForSending(int contextHandle, unsigned int timeout_ms)
{
    if(contextsToSend[contextHandle].used == true)
    {
#ifndef __unix__
		contextsToSend[contextHandle].inOvLap.InternalHigh = 0;
		DWORD dwRet = WaitForSingleObject(contextsToSend[contextHandle].inOvLap.hEvent, timeout_ms);
		if (dwRet == WAIT_OBJECT_0 || dwRet == WAIT_TIMEOUT)
			return 1;
#else
        auto t1 = chrono::high_resolution_clock::now();
        auto t2 = chrono::high_resolution_clock::now();
        std::unique_lock<std::mutex> lck(contextsToSend[contextHandle].transferLock);
        while(contextsToSend[contextHandle].done.load() == false && std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() < timeout_ms)
        {
            //blocking not to waste CPU
            contextsToSend[contextHandle].cv.wait(lck);
            t2 = chrono::high_resolution_clock::now();
        }
        return contextsToSend[contextHandle].done == true;
#endif
    }
    return 0;
}

/**
@brief Finishes asynchronous data sending to board
@param buffer array where to store received data
@param length number of bytes to read
@param contextHandle handle of which context to finish
@return false failure, true number of bytes sent
*/
int Connection_uLimeSDR::FinishDataSending(const char *buffer, uint32_t length, int contextHandle)
{
    if(contextsToSend[contextHandle].used == true)
    {
#ifndef __unix__
		ULONG ulActualBytesTransferred ;

		if (GetOverlappedResult(mFTHandle, &contextsToSend[contextHandle].inOvLap, &ulActualBytesTransferred, FALSE)==FALSE)
		{
			return -1;
		}
		length = ulActualBytesTransferred;
		CloseHandle(contextsToSend[contextHandle].inOvLap.hEvent);
		contextsToSend[contextHandle].used = false;
		return length;
#else
        length = contextsToSend[contextHandle].bytesXfered;
        contextsToSend[contextHandle].used = false;
        contextsToSend[contextHandle].reset();
        return length;
#endif
    }
    else
        return 0;
}

/**
@brief Aborts sending operations
*/
void Connection_uLimeSDR::AbortSending()
{
#ifndef __unix__
	FT_AbortPipe(mFTHandle, mStreamWrEndPtAddr);
	for (int i = 0; i < USB_MAX_CONTEXTS; ++i)
	{
		if (contextsToSend[i].used == true)
		{
			CloseHandle(contextsToSend[i].inOvLap.hEvent);
			contextsToSend[i].used = false;
		}
	}
	txSize = 0;
#else
    for(int i = 0; i<USB_MAX_CONTEXTS; ++i)
    {
        if(contextsToSend[i].used)
            libusb_cancel_transfer(contextsToSend[i].transfer);
    }
    FT_FlushPipe(mStreamWrEndPtAddr);
    txSize = 0;
#endif
}

int Connection_uLimeSDR::UpdateThreads()
{
    bool needTx = false;
    bool needRx = false;

    //check which threads are needed
    for(auto i : mRxStreams)
        if(i->IsActive())
        {
            needRx = true;
            break;
        }
    for(auto i : mTxStreams)
        if(i->IsActive())
        {
            needTx = true;
            break;
        }

    //stop threads if not needed
    if(not needTx and txRunning.load())
    {
        terminateTx.store(true);
        txThread.join();
        txRunning.store(false);
    }
    if(not needRx and rxRunning.load())
    {
        terminateRx.store(true);
        rxThread.join();
        rxRunning.store(false);
    }

    //configure FPGA on first start, or disable FPGA when not streaming
    if((needTx or needRx) && (not rxRunning.load() and not txRunning.load()))
    {
        //enable FPGA streaming
        fpga::StopStreaming(this);
        fpga::ResetTimestamp(this);
        //USB FIFO reset
        LMS64CProtocol::GenericPacket ctrPkt;
        ctrPkt.cmd = CMD_USB_FIFO_RST;
        ctrPkt.outBuffer.push_back(0x01);
        TransferPacket(ctrPkt);
        ctrPkt.outBuffer[0] = 0x00;
        TransferPacket(ctrPkt);

        //enable MIMO mode, 12 bit compressed values
        StreamConfig config;
        config.linkFormat = StreamConfig::STREAM_12_BIT_COMPRESSED;
        uint16_t smpl_width; // 0-16 bit, 1-14 bit, 2-12 bit
        if(config.linkFormat == StreamConfig::STREAM_12_BIT_IN_16)
            smpl_width = 0x0;
        else if(config.linkFormat == StreamConfig::STREAM_12_BIT_COMPRESSED)
            smpl_width = 0x2;
        else
            smpl_width = 0x2;
        WriteRegister(0x0008, 0x0100 | smpl_width);

        uint16_t channelEnables = 0;
        for(uint8_t i=0; i<mRxStreams.size(); ++i)
            channelEnables |= (1 << mRxStreams[i]->config.channelID);
        for(uint8_t i=0; i<mTxStreams.size(); ++i)
            channelEnables |= (1 << mTxStreams[i]->config.channelID);
        WriteRegister(0x0007, channelEnables);

        LMS7002M lmsControl;
        lmsControl.SetConnection(this);
        bool fromChip = true;
        lmsControl.Modify_SPI_Reg_bits(LMS7param(LML1_MODE), 0, fromChip);
        lmsControl.Modify_SPI_Reg_bits(LMS7param(LML2_MODE), 0, fromChip);
        lmsControl.Modify_SPI_Reg_bits(LMS7param(LML1_FIDM), 0, fromChip);
        lmsControl.Modify_SPI_Reg_bits(LMS7param(LML2_FIDM), 0, fromChip);
        lmsControl.Modify_SPI_Reg_bits(LMS7param(PD_RX_AFE1), 0, fromChip);
        lmsControl.Modify_SPI_Reg_bits(LMS7param(PD_TX_AFE1), 0, fromChip);
        lmsControl.Modify_SPI_Reg_bits(LMS7param(PD_RX_AFE2), 0, fromChip);
        lmsControl.Modify_SPI_Reg_bits(LMS7param(PD_TX_AFE2), 0, fromChip);

        lmsControl.Modify_SPI_Reg_bits(LMS7param(LML2_S0S), 1, fromChip);
        lmsControl.Modify_SPI_Reg_bits(LMS7param(LML2_S1S), 0, fromChip);
        lmsControl.Modify_SPI_Reg_bits(LMS7param(LML2_S2S), 3, fromChip);
        lmsControl.Modify_SPI_Reg_bits(LMS7param(LML2_S3S), 2, fromChip);

        if(channelEnables & 0x2) //enable MIMO
        {
            uint16_t macBck = lmsControl.Get_SPI_Reg_bits(LMS7param(MAC), fromChip);
            lmsControl.Modify_SPI_Reg_bits(LMS7param(MAC), 1, fromChip);
            lmsControl.Modify_SPI_Reg_bits(LMS7param(EN_NEXTRX_RFE), 1, fromChip);
            lmsControl.Modify_SPI_Reg_bits(LMS7param(EN_NEXTTX_TRF), 1, fromChip);
            lmsControl.Modify_SPI_Reg_bits(LMS7param(MAC), macBck, fromChip);
        }

        fpga::StartStreaming(this);
    }
    else if(not needTx and not needRx)
    {
        //disable FPGA streaming
        fpga::StopStreaming(this);
    }

    //FPGA should be configured and activated, start needed threads
    if(needRx and not rxRunning.load())
    {
        ThreadData args;
        args.terminate = &terminateRx;
        args.dataPort = this;
        args.dataRate_Bps = &rxDataRate_Bps;
        args.channels = mRxStreams;
        args.generateData = &generateData;
        args.safeToConfigInterface = &safeToConfigInterface;
        args.lastTimestamp = &rxLastTimestamp;;

        rxRunning.store(true);
        terminateRx.store(false);
        rxThread = std::thread(ReceivePacketsLoop, args);
    }
    if(needTx and not txRunning.load())
    {
        ThreadData args;
        args.terminate = &terminateTx;
        args.dataPort = this;
        args.dataRate_Bps = &txDataRate_Bps;
        args.channels = mTxStreams;
        args.generateData = &generateData;
        args.safeToConfigInterface = &safeToConfigInterface;
        args.lastTimestamp = nullptr;

        txRunning.store(true);
        terminateTx.store(false);
        txThread = std::thread(TransmitPacketsLoop, args);
    }
    return 0;
}
