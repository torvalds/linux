/*
 * Copyright (c) 2006-2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * nverrval.h is a header used for macro expansion of the errors defined for
 * the Nv methods & interfaces.
 *
 * This header is NOT protected from being included multiple times, as it is
 * used for C pre-processor macro expansion of error codes, and the
 * descriptions of those error codes.
 *
 * Each error code has a unique name, description and value to make it easier
 * for developers to identify the source of a failure.  Thus there are no
 *generic or unknown error codes.
 */

/**
* @defgroup nv_errors NVIDIA Error Codes
*
* Provides return error codes for functions.
*
* @ingroup nvodm_errors
* @{
*/

/** common error codes */
NVERROR(Success,                            0x00000000, "success")
NVERROR(NotImplemented,                     0x00000001, "method or interface is not implemented")
NVERROR(NotSupported,                       0x00000002, "requested operation is not supported")
NVERROR(NotInitialized,                     0x00000003, "method or interface is not initialized")
NVERROR(BadParameter,                       0x00000004, "bad parameter to method or interface")
NVERROR(Timeout,                            0x00000005, "not completed in the expected time")
NVERROR(InsufficientMemory,                 0x00000006, "insufficient system memory")
NVERROR(ReadOnlyAttribute,                  0x00000007, "cannot write a read-only attribute")
NVERROR(InvalidState,                       0x00000008, "module is in invalid state to perform the requested operation")
NVERROR(InvalidAddress,                     0x00000009, "invalid address")
NVERROR(InvalidSize,                        0x0000000A, "invalid size")
NVERROR(BadValue,                           0x0000000B, "illegal value specified for parameter")
NVERROR(AlreadyAllocated,                   0x0000000D, "resource has already been allocated")
NVERROR(Busy,                               0x0000000E, "busy, try again")
NVERROR(ModuleNotPresent,                   0x000a000E, "hw module is not peresent")
NVERROR(ResourceError,                      0x0000000F, "clock, power, or pinmux resource error")
NVERROR(CountMismatch,                      0x00000010, "Encounter Error on count mismatch")

/* surface specific error codes */
NVERROR(InsufficientVideoMemory,            0x00010000, "insufficient video memory")
NVERROR(BadSurfaceColorScheme,              0x00010001, "this surface scheme is not supported in the current controller")
NVERROR(InvalidSurface,                     0x00010002, "invalid surface")
NVERROR(SurfaceNotSupported,                0x00010003, "surface is not supported")

/* display specific error codes */
NVERROR(DispInitFailed,                     0x00020000, "display initialization failed")
NVERROR(DispAlreadyAttached,                0x00020001, "the display is already attached to a controller")
NVERROR(DispTooManyDisplays,                0x00020002, "the controller has too many displays attached")
NVERROR(DispNoDisplaysAttached,             0x00020003, "the controller does not have an attached display")
NVERROR(DispModeNotSupported,               0x00020004, "the mode is not supported by the display or controller")
NVERROR(DispNotFound,                       0x00020005, "the requested display was not found")
NVERROR(DispAttachDissallowed,              0x00020006, "the display cannot attach to the given controller")
NVERROR(DispTypeNotSupported,               0x00020007, "display type not supported")
NVERROR(DispAuthenticationFailed,           0x00020008, "display authenication failed")
NVERROR(DispNotAttached,                    0x00020009, "display not attached")
NVERROR(DispSamePwrState,                   0x0002000A, "display already in requested power state")
NVERROR(DispEdidFailure,                    0x0002000B, "edid read/parsing failure")

/* NvOs error codes */
NVERROR(FileWriteFailed,                    0x00030000, "the file write operation failed")
NVERROR(FileReadFailed,                     0x00030001, "the file read operation failed")
NVERROR(EndOfFile,                          0x00030002, "the end of file has been reached")
NVERROR(FileOperationFailed,                0x00030003, "the file operation has failed")
NVERROR(DirOperationFailed,                 0x00030004, "the directory operation has failed")
NVERROR(EndOfDirList,                       0x00030005, "there are no more entries in the directory")
NVERROR(ConfigVarNotFound,                  0x00030006, "the configuration variable is not present")
NVERROR(InvalidConfigVar,                   0x00030007, "the configuration variable is corrupted")
NVERROR(LibraryNotFound,                    0x00030008, "the dynamic library was not found for open")
NVERROR(SymbolNotFound,                     0x00030009, "the symbol in a dyanmic library was not found")
NVERROR(MemoryMapFailed,                    0x0003000a, "the memory mapping operation failed")
NVERROR(IoctlFailed,                        0x0003000f, "the ioctl failed")
NVERROR(AccessDenied,                       0x00030010, "the pointer is invalid or require additional privileges for access")
NVERROR(DeviceNotFound,                     0x00030011, "requested device is not found")
NVERROR(KernelDriverNotFound,               0x00030012, "kernel driver not found")
NVERROR(FileNotFound,                       0x00030013, "File or directory not found")

/* I/O devices */
NVERROR(SpiReceiveError,                    0x00040000, "spi receive error" )
NVERROR(SpiTransmitError,                   0x00040001, "spi transmit error" )
NVERROR(HsmmcCardNotPresent,                0x00041000, "hsmmc card not present")
NVERROR(HsmmcControllerBusy,                0x00041001, "hsmmc controller is busy")
NVERROR(HsmmcAutoDetectCard,                0x00041002, "auto detect the card in hsmmc slot")
NVERROR(SdioCardNotPresent,                 0x00042000, "sdio card not present")
NVERROR(SdioInstanceTaken,                  0x00042001, "Instance unavailable or in use")
NVERROR(SdioControllerBusy,                 0x00042002, "controller is busy")
NVERROR(SdioReadFailed,                     0x00042003, "read transaction has failed")
NVERROR(SdioWriteFailed,                    0x00042004, "write transaction has failed")
NVERROR(SdioBadBlockSize,                   0x00042005, "bad block size")
NVERROR(SdioClockNotConfigured,             0x00042006, "Clock is not configured")
NVERROR(SdioSdhcPatternIntegrityFailed,     0x00042007, "SDHC Check pattern integrity failed")
NVERROR(SdioCommandFailed,                  0x00042008, "command failed to execute")
NVERROR(SdioCardAlwaysPresent,              0x00042009, "sdio card is soldered")
NVERROR(SdioAutoDetectCard,                 0x0004200a, "auto detect the sd card")
NVERROR(UsbInvalidEndpoint,                 0x00043000, "usb invalid endpoint")
NVERROR(UsbfTxfrActive,                     0x00043001, "The endpoint has an active transfer in progress.")
NVERROR(UsbfTxfrComplete,                   0x00043002, "The endpoint has a completed transfer that has not been cleared.")
NVERROR(UsbfTxfrFail,                       0x00043003, "The endpoint transfer is failed.")
NVERROR(UsbfEpStalled,                      0x00043004, "The endpoint has been placed in a halted or stalled state.")
NVERROR(UsbfCableDisConnected,              0x00043005, "usb cable disconnected")
NVERROR(UartOverrun,                        0x00044000, "overrun occurred when receiving the data")
NVERROR(UartFraming,                        0x00044001, "data received had framing error")
NVERROR(UartParity,                         0x00044002, "data received had parity error")
NVERROR(UartBreakReceived,                  0x00044004, "received break signal")
NVERROR(I2cReadFailed,                      0x00045000, "Failed to read data through I2C")
NVERROR(I2cWriteFailed,                     0x00045001, "Failed to write data through I2C")
NVERROR(I2cDeviceNotFound,                  0x00045003, "Slave Device Not Found")
NVERROR(I2cInternalError,                   0x00045004, "The controller reports the error during transaction like fifo overrun, underrun")
NVERROR(I2cArbitrationFailed,               0x00045005, "Master does not able to get the control of bus")
NVERROR(IdeHwError,                         0x00046000, "Ide HW error")
NVERROR(IdeReadError,                       0x00046001, "Ide read error")
NVERROR(IdeWriteError,                      0x00046002, "Ide write error")

/* OWR error codes */
NVERROR(OwrReadFailed,                      0x00047000, "OWR data reading failed")
NVERROR(OwrWriteFailed,                     0x00047001, "OWR data write failed")
NVERROR(OwrBitTransferFailed,               0x00047002, "OWR bit transfer failed")
NVERROR(OwrInvalidOffset,                   0x00047003, "OWR invalid offset")

/* Nv2D error codes */
NVERROR(InvalidOperation,                   0x00050000, "invalid operation")

/* NvRm error codes */
NVERROR(RmInitFailed,                       0x00060000, "rm failed to initialize")
NVERROR(RmChannelInitFailure,               0x00060001, "channel init failed")
NVERROR(RmStreamInitFailure,                0x00060002, "stream init failed")
NVERROR(RmSyncPointAllocFailure,            0x00060003, "sync point alloc failed")
NVERROR(ResourceAlreadyInUse,               0x00060004, "resource already in use")
NVERROR(DmaBusy,                            0x00061000, "the dma channel is busy and not able to take any more request")
NVERROR(InvalidSourceId,                    0x00061001, "invalid source id")
NVERROR(DmaChannelNotAvailable,             0x00061002, "dma channel not available")

/* NvIsp error codes */
NVERROR(NoConnectedImager,                  0x00070001, "no imager connected")
NVERROR(UnsupportedResolution,              0x00070002, "unsupported resolution")
NVERROR(I2CCommunicationError,              0x00070003, "i2c communication failed")
NVERROR(IspConfigFileParseError,            0x00070004, "isp config file parse error")
NVERROR(TooDark,                            0x00070006, "image too dark for 3A operation")
NVERROR(InvalidIspConfigAttribute,          0x00070007, "invalid isp config attribute")
NVERROR(InvalidIspConfigAttributeElement,   0x00070008, "invalid isp config attribute element")
NVERROR(IspConfigSyntaxError,               0x00070009, "isp config syntax error")
NVERROR(ImagerVersionNotSupported,          0x0007000A, "imager version not supported")
NVERROR(CorruptedBuffer,                    0x0007000B, "buffer is corrupted")


/* NvTest error codes */
NVERROR(TestApplicationFailed,              0x00080000, "the test application failed")
NVERROR(TestNoUserInput,                    0x00080001, "no user input available")
NVERROR(TestCommandLineError,               0x00080002, "command line parsing error")
NVERROR(TestDataVerificationFailed,         0x00080003, "Data verification failed error")
NVERROR(TestServerFileReadFailed,           0x00081000, "reading the test file failed")
NVERROR(TestServerInvalidAddress,           0x00081001, "invalid connection address")
NVERROR(TestServerMemoryLimitExceeded,      0x00081002, "target memory limit exceeded")

/* NvCam error codes */
NVERROR(ColorFormatNotSupported,            0x00090006, "color format not supported")

/* Transport error codes */
NVERROR(TransportPortAlreadyExist,          0x000A0001, "The port name already exist.")
NVERROR(TransportMessageBoxEmpty,           0x000A0003, "Received Message box empty.")
NVERROR(TransportMessageBoxFull,            0x000A0004, "Message box is full and not able to send the message.")
NVERROR(TransportConnectionFailed,          0x000A0006, "Making connection to port is failed.")
NVERROR(TransportNotConnected,              0x000A0007, "Port is not connected.")

/* Nand error codes */
NVERROR(NandReadFailed,                     0x000B0000, "Nand Read failed")
NVERROR(NandProgramFailed,                  0x000B0001, "Nand Program failed")
NVERROR(NandEraseFailed,                    0x000B0002, "Nand Erase failed")
NVERROR(NandCopyBackFailed,                 0x000B0003, "Nand Copy back failed")
NVERROR(NandOperationFailed,                0x000B0004, "requested Nand operation failed")
NVERROR(NandBusy,                           0x000B0005, "Nand operation incomplete and is busy")
NVERROR(NandNotOpened,                      0x000B0006, "Nand driver not opened")
NVERROR(NandAlreadyOpened,                  0x000B0007, "Nand driver is already opened")
NVERROR(NandBadOperationRequest,            0x000B0008, "status for wrong nand operation is requested ")
NVERROR(NandCommandQueueError,              0x000B0009, "Command queue error occured ")
NVERROR(NandReadEccFailed,                  0x000B0010, "Read with ECC resulted in uncorrectable errors")
NVERROR(NandFlashNotSupported,              0x000B0011, "Nand flash on board is not supported by the ddk")
NVERROR(NandLockFailed,                     0x000B0012, "Nand flash lock feature failed")
NVERROR(NandErrorThresholdReached,          0x000B0013, "Ecc errors reached the threshold set")
NVERROR(NandWriteFailed,                    0x000B0014, "Nand Write failed")
NVERROR(NandBadBlock,                       0x000B0015, "Indicates a bad block on media")
NVERROR(NandBadState,                       0x000B0016, "Indicates an invalid state")
NVERROR(NandBlockIsLocked,                  0x000B0017, "Indicates the block is locked")
NVERROR(NandNoFreeBlock,                    0x000B0018, "Indicates there is no free block in the flash")
NVERROR(NandTTFailed,                       0x000B0019, "Nand TT Failure")
NVERROR(NandTLFailed,                       0x000B001A, "Nand TL Failure")
NVERROR(NandTLNoBlockAssigned,              0x000B001B, "Nand TL No Block Assigned")

/* nvwinsys error codes */
NVERROR(WinSysBadDisplay,                   0x000C0000, "bad display specified")
NVERROR(WinSysNoDevice,                     0x000C0001, "no device found")
NVERROR(WinSysBadDrawable,                  0x000C0002, "bad drawable")

/* nvblserver error codes */
NVERROR(BLServerFileReadFailed,             0x000D0000, "reading the bootloader file failed")
NVERROR(BLServerInvalidAddress,             0x000D0001, "invalid connection address")
NVERROR(BLServerInvalidElfFile,             0x000D0002, "invalid elf file")
NVERROR(BLServerConnectionFailed,           0x000D0003, "connection with target failed")
NVERROR(BLServerMemoryLimitExceeded,        0x000D0005, "target memory limit exceeded")

/* NvMM Audio Mixer error codes */
NVERROR(AudioMixerPinTypeNotSupported,      0x000E0000, "Pin type is not supported")
NVERROR(AudioMixerDirectionNotSupported,    0x000E0001, "Pin direction is not supported")
NVERROR(AudioMixerNoMorePinsAvailable,      0x000E0002, "No more pins are available")
NVERROR(AudioMixerBadPinNumber,             0x000E0003, "Bad pin number")

/* NvMM Video Encoder error codes */
NVERROR(VideoEncResolutionNotSupported,     0x000E1000, "Resolution parameters must be multiple of 16")

/* NvMM JPEG Encoder error codes */
NVERROR(JPEGEncHWError,                     0x000E2000, "HW encountered some error in Encoding: either ICQ is full or MEMDMA is busy")

/* NvMM Video Decoder error codes */
NVERROR(VideoDecRetainLock,                    0x000E3001, "Keep the HW lock with the decoder")
NVERROR(VideoDecMataDataFound,                 0x000E3002, "Decoder has decoded Mata Data Information")
NVERROR(VideoDecFrameDecoded,                  0x000E3004, "Decoder has decoded one complete Frame")
NVERROR(VideoDecDecodedPartialFrame,           0x000E3008, "Decoder has decoded Frame Partially")
NVERROR(VideoDecInsufficientBitstream,         0x000E3010, "unable to decode because of unavailablity of bitstream for decoding")
NVERROR(VideoDecOutputSurfaceUnavailable,      0x000E3020, "Output surface is unavailable for storing current decoded frame")
NVERROR(VideoDecUnsupportedStreamFormat,       0x000E3040, "Given i/p Stream format is not supported by Video Decoder")
NVERROR(VideoDecFrameDecodedPlusVideoDecEvent, 0x000E3080, "Decoder has decoded one complete frame and need to send event to client")
NVERROR(VideoDecFailed,                        0x000E3100, "Failed to decode")
NVERROR(VideoDecDecodingComplete,              0x000E3200, "Decoder has finished decoding")
NVERROR(VideoDecProvideNextIPBuffer,           0x000E3400, "Decoder is still using current buffer,mean while provide next ip buffer")
NVERROR(VideoDecProvideCurrentIPBuffer,        0x000E3800, "provide Current ip buffer again")

/* Vibrate shim error codes */
NVERROR(PipeNotConnected,                      0x000F0000, "Indicates that there are no readers attached to the message queue")
NVERROR(ReadQNotCreated,                       0x000F0001, "Some error creating the read message Q")

/* Content Parser, Writer, Pipe error codes */
NVERROR(ParserEndOfStream,                  0x00100000, "the end of stream has been reached")
NVERROR(ParserFailedToGetData,              0x00100001, "Could not get data because of some Error")
NVERROR(InSufficientBufferSize,             0x00100002, "InSufficientBufferSize for parser to read data")
NVERROR(ParserReadFailure,                  0x00100003, "Encounter Error on parser reads")
NVERROR(ParserOpenFailure,                  0x00100004, "Encounter Error on parser open")
NVERROR(UnSupportedStream,                  0x00100005, "Error for Unsupported streams")
NVERROR(ParserFailure,                      0x00100006, "Fail to Parse the file. Or General/logical Error encounter on other parser failures")
NVERROR(ParserHeaderDecodeNotComplete,      0x00100007, "Could not get data because Header Decode is not complete")
NVERROR(ParserCloseFailure,                 0x00100008, "Encounter Error on parser close")
NVERROR(ParserMarkerHit,                    0x00100009, "Parser Marker HIT")
NVERROR(ParserCorruptedStream,              0x0010000A, "Encounter error on corrupted Parser stream")
NVERROR(ParserDRMLicenseNotFound,           0x0010000B, "DRM License Not Found")
NVERROR(ParserDRMFailure,                   0x0010000C, "DRM Functionality Failed")
NVERROR(ParserSeekUnSupported,              0x0010000D, "Seek UnSupported dueto non-index rntries etc., ")
NVERROR(ParserTrickModeUnSupported,         0x0010000E, "Seek UnSupported dueto non-index rntries etc., ")
NVERROR(ParserCoreNotCreated,               0x0010000F, "Core not created ")
NVERROR(UnSupported_VideoStream,            0x00100010, "Error for Unsupported streams")
NVERROR(UnSupported_AudioStream,            0x00100011, "Error for Unsupported streams")
NVERROR(WriterOpenFailure,                  0x00101001, "Encounter Error on writer open")
NVERROR(WriterUnsupportedStream,            0x00101002, "Error for Unsupported streams in writer")
NVERROR(WriterUnsupportedUserData,          0x00101003, "Error Unsupported user data  set in writer")
NVERROR(WriterFileSizeLimitExceeded,        0x00101004, "File size limit exceeded in writer")
NVERROR(WriterInsufficientMemory,           0x00101005, "Insufficient memory in writer")
NVERROR(WriterFailure,                      0x00101006, "General/logical Error encounter on other writer failures")
NVERROR(WriterCloseFailure,                 0x00101007, "Encounter Error on writer close")
NVERROR(WriterInitFailure,                  0x00101008, "Writer Init Failed")
NVERROR(WriterFileWriteLimitExceeded,       0x00101009, "File Write limit exceeded in writer")
NVERROR(ContentPipeNoData,                  0x00102001, "Data not available")
NVERROR(ContentPipeNoFreeBuffers,           0x00102002, "No free buffers")
NVERROR(ContentPipeSpareAreaInUse,          0x00102003, "Spare buffer is in use")
NVERROR(ContentPipeEndOfStream,             0x00102004, "End of stream reached")
NVERROR(ContentPipeNotReady,                0x00102005, "Not Ready")
NVERROR(ContentPipeInNonCachingMode,        0x00102006, "In non-caching mode")
NVERROR(ContentPipeInsufficientMemory,      0x00102007, "Insufficient memory in ContentPipe")
NVERROR(ContentPipeNotInvalidated,          0x00102008, "ContentPipe memory is not invalidated")

NVERROR(UnSupportedMetadata,                0x00102009, "UnSupportedMetadata")
NVERROR(MetadataSuccess,                    0x0010200A, "Successfully Extracted the Metadata key")
NVERROR(MetadataFailure,                    0x0010200B, "Error Encountered during Meta data Extraction")
NVERROR(NewMetaDataAvailable,               0x0010200C, "NewMetaDataAvailable")

/* TrackList error codes */
NVERROR(TrackListInvalidTrackIndex,         0x00110001, "Invalid track number")
NVERROR(TrackListError,                     0x00110002, "Error encounterd in TrackList Operation")
NVERROR(TrackListItemStillPlayingError,     0x00110003, " Track list item is currently playing")
NVERROR(TrackListNotPlaying,                0x00110004, " Track list is not playing")

/* nv3p error codes */
NVERROR(Nv3pUnrecoverableProtocol,          0x00120000, "unrecoverable protocol error")
NVERROR(Nv3pBadPacketType,                  0x00120001, "bad packet type")
NVERROR(Nv3pPacketNacked,                   0x00120002, "packet was nacked")
NVERROR(Nv3pBadReceiveLength,               0x00120003, "bad receive length")
NVERROR(Nv3pBadReturnData,                  0x00120004, "bad return data")

/* AES error codes */
NVERROR(AesClearSbkFailed,                  0x00130000, "AES clear Secure Boot Key Failed")
NVERROR(AesLockSskFailed,                   0x00130001, "AES Lock Secure Storage Key Failed")
NVERROR(AesDisableCryptoFailed,             0x00130002, "AES disable crypto failed")

/* Block Driver error codes */
/* generic error codes */
NVERROR(BlockDriverIllegalIoctl,            0x00140001, "Block Driver illegal IOCTL invoked")
NVERROR(BlockDriverOverlappedPartition,     0x00140002, "Block Driver partition overlap")
NVERROR(BlockDriverNoPartition,             0x00140003, "Block Driver IOCTL call needs partition create")
NVERROR(BlockDriverIllegalPartId,           0x00140004, "Block Driver operation using illegal partition ID")
NVERROR(BlockDriverWriteVerifyFailed,       0x00140005, "Block Driver write data comparison failed")
/* Nand specific block driver errors */
NVERROR(NandBlockDriverEraseFailure,        0x00140011, "Nand Block Driver erase failed")
NVERROR(NandBlockDriverWriteFailure,        0x00140012, "Nand Block Driver write failed")
NVERROR(NandBlockDriverReadFailure,         0x00140013, "Nand Block Driver read failed")
NVERROR(NandBlockDriverLockFailure,         0x00140014, "Nand Block Driver lock failed")
NVERROR(NandRegionIllegalAddress,           0x00140015, "Nand Block Driver sector access illegal")
NVERROR(NandRegionTableOpFailure,           0x00140016, "Nand Block Driver region operation failed")
NVERROR(NandBlockDriverMultiInterleave,     0x00140017, "Nand Block Driver multiple interleave modes")
NVERROR(NandTagAreaSearchFailure,           0x0014001c, "Nand Block Driver tag area search failed")
/* EMMC specific block driver errors */
NVERROR(EmmcBlockDriverLockNotSupported,    0x00140101, "EMMC Block Driver Lock operation not supported")
NVERROR(EmmcBlockDriverLockUnaligned,       0x00140102, "EMMC Block Driver Lock area size or location unaligned")
NVERROR(EmmcBlockDriverIllegalStateRead,    0x00140103, "EMMC Block Driver Read when state is not TRANS")
NVERROR(EmmcBlockDriverIllegalStateWrite,   0x00140104, "EMMC Block Driver Write when state is not TRANS")
NVERROR(EmmcCommandFailed,                  0x00140105, "EMMC Block Driver command failed to execute")
NVERROR(EmmcReadFailed,                     0x00140106, "EMMC Block Driver Read failed")
NVERROR(EmmcWriteFailed,                    0x00140107, "EMMC Block Driver Write failed")
NVERROR(EmmcBlockDriverEraseFailure,        0x00140108, "Emmc Block Driver erase failed")
NVERROR(EmmcBlockDriverIllegalAddress,      0x00140109, "Emmc Block Driver address is illegal or misaligned")
NVERROR(EmmcBlockDriverLockFailure,         0x0014010A, "Emmc Block Driver lock failed")
NVERROR(EmmcBlockDriverBlockIsLocked,       0x0014010B, "Emmc Block Driver Accessed block is locked")
/* Mipi Hsi error codes */
NVERROR(MipiHsiTxFifoEmpty,                 0x00140200, "TX_FIFO_CNT in Status and Interrupt Identification Register is zero")
NVERROR(MipiHsiRxFifoEmpty,                 0x00140201, "RX_FIFO_CNT in Status and Interrupt Identification Register is zero")
NVERROR(MipiHsiBusy,                        0x00140202, "Mipi Hsi controller is busy")
NVERROR(MipiHsiHandleNotConfigured,         0x00140203, "Mipi Hsi handle is not configured")
NVERROR(MipiHsiTransmitError,               0x00140204, "Mipi Hsi transmit error - check the write function ")
NVERROR(MipiHsiReceiveError,                0x00140205, "Mipi Hsi receive error - check the read function")
NVERROR(MipiHsiTransferIncomplete,          0x00140206, "Mipi Hsi requested number of packets are not transferred")

/* Shader compiler error codes */
NVERROR(SCCompileFail,                      0x00150000, "Source shader compilation failed")

/* Drm error codes */
NVERROR(DrmFailure,                         0x00160000, "Drm - Failed")
NVERROR(DrmInvalidArg,                      0x00160001, "Drm Invalid arguments passed")
NVERROR(DrmOutOfMemory,                     0x00160002, "Drm-Memory insufficent")
NVERROR(DrmFileNotFound,                    0x00160003, "Drm - File not found")
NVERROR(DrmBufferTooSmall,                  0x00160004, "Drm- Buffer size passed is too small")
NVERROR(DrmInvalidLicense,                  0x00160005, "Drm - Invalid license")
NVERROR(DrmLicenseExpired,                  0x00160006, "Drm- License expired")
NVERROR(DrmRightsNotAvailable,              0x00160007, "Drm-Right are not available")
NVERROR(DrmLicenseNotFound,                 0x00160008, "Drm - License it not found")
NVERROR(DrmInvalidBindId,                   0x00160009, "Drm - Invalid Bind Id ")
NVERROR(DrmVersionNotSupported,             0x0016000a, "Drm-Unsupported Version")
NVERROR(DrmMeteringNotSupported,            0x0016000b, "Drm- Metering is not supported")
NVERROR(DrmDecryptionFailed,                0x0016000c, "Drm- Decryption failed")
/* System Update error codes */
NVERROR(SysUpdateInvalidBLVersion,          0x00170000, "NvSysUpdate - InvalidBL Version")

/** @} */
/* ^^^ ADD ALL NEW ERRORS RIGHT ABOVE HERE ^^^ */
