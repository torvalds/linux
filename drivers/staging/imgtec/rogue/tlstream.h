/*************************************************************************/ /*!
@File
@Title          Transport Layer kernel side API.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    TL provides driver components with a way to copy data from kernel
                space to user space (e.g. screen/file).

                Data can be passed to the Transport Layer through the 
                TL Stream (kernel space) API interface.

                The buffer provided to every stream is a modified version of a 
                circular buffer. Which CB version is created is specified by
                relevant flags when creating a stream. Currently two types
                of buffer are available:
                - TL_FLAG_RESERVE_DROP_NEWER:
                  When the buffer is full, incoming data are dropped 
                  (instead of overwriting older data) and a marker is set 
                  to let the user know that data have been lost.
                - TL_FLAG_RESERVE_BLOCK:
                  When the circular buffer is full, reserve/write calls block
                  until enough space is freed.

                All size/space requests are in bytes. However, the actual
                implementation uses native word sizes (i.e. 4 byte aligned).

                The user does not need to provide space for the stream buffer 
                as the TL handles memory allocations and usage.

                Inserting data to a stream's buffer can be done either:
                - by using TLReserve/TLCommit: User is provided with a buffer
                                                 to write data to.
                - or by using TLWrite:         User provides a buffer with 
                                                 data to be committed. The TL 
                                                 copies the data from the 
                                                 buffer into the stream buffer 
                                                 and returns.
                Users should be aware that there are implementation overheads 
                associated with every stream buffer. If you find that less 
                data are captured than expected then try increasing the
                stream buffer size or use TLInfo to obtain buffer parameters
                and calculate optimum required values at run time.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/
#ifndef __TLSTREAM_H__
#define __TLSTREAM_H__


#include "img_types.h"
#include "pvrsrv_error.h"

/*! Flags specifying stream and circular buffer behaviour */
/*! Reject new data if the buffer is full, producer may then decide to 
 *    drop the data or retry after some time. */
#define TL_FLAG_RESERVE_DROP_NEWER     (1U<<0)
/*! Block Reserve (subsequently Write) calls if there is not enough space 
 *    until some space is freed via a client read operation. */
#define TL_FLAG_RESERVE_BLOCK          (1U<<1)
/*! When buffer is full, advance the tail/read position to accept the new
 * reserve call (size permitting), effectively overwriting the oldest 
 * data in the circular buffer. Not supported yet. */
#define TL_FLAG_RESERVE_DROP_OLDEST    (1U<<2)

/*! Do not destroy stream if there still are data that have not been 
 *     copied in user space. BLock until the stream is emptied. */
#define TL_FLAG_FORCE_FLUSH            (1U<<8)
/*! Do not signal consumers on commit automatically when the stream buffer
 * transitions from empty to non-empty. Producer responsible for signal when
 * it chooses. */
#define TL_FLAG_NO_SIGNAL_ON_COMMIT    (1U<<9)

/*! Defer allocation of stream's shared memory until first open. */
#define TL_FLAG_ALLOCATE_ON_FIRST_OPEN (1U<<10)

/*! Structure used to pass internal TL stream sizes information to users.*/
typedef struct _TL_STREAM_INFO_
{
    IMG_UINT32 headerSize;          /*!< Packet header size in bytes */
    IMG_UINT32 minReservationSize;  /*!< Minimum data size reserved in bytes */
    IMG_UINT32 pageSize;            /*!< Page size in bytes */
    IMG_UINT32 pageAlign;           /*!< Page alignment in bytes */
} TL_STREAM_INFO, *PTL_STREAM_INFO;

/*! Callback operations or notifications that a stream producer may handle
 * when requested by the Transport Layer.
 */
#define TL_SOURCECB_OP_CLIENT_EOS 0x01  /*!< Client has reached end of stream,
                                         * can anymore data be supplied?
                                         * ui32Resp ignored in this operation */

/*! Function pointer type for the callback handler into the "producer" code
 * that writes data to the TL stream.  Producer should handle the notification
 * or operation supplied in ui32ReqOp on stream hStream. The
 * Operations and notifications are defined above in TL_SOURCECB_OP */
typedef PVRSRV_ERROR (*TL_STREAM_SOURCECB)(IMG_HANDLE hStream,
		IMG_UINT32 ui32ReqOp, IMG_UINT32* ui32Resp, void* pvUser);

typedef void (*TL_STREAM_ONREADEROPENCB)(void *pvArg);

/*************************************************************************/ /*!
 @Function      TLAllocSharedMem
 @Description   Allocates shared memory for the stream.
 @Input         phStream    Stream handle.
 @Return        eError      Internal services call returned eError error
                            number.
 @Return        PVRSRV_OK
*/ /**************************************************************************/
PVRSRV_ERROR
TLAllocSharedMemIfNull(IMG_HANDLE hStream);

/*************************************************************************/ /*!
 @Function      TLFreeSharedMem
 @Description   Frees stream's shared memory.
 @Input         phStream    Stream handle.
*/ /**************************************************************************/
void
TLFreeSharedMem(IMG_HANDLE hStream);

/*************************************************************************/ /*!
 @Function      TLStreamCreate
 @Description   Request the creation of a new stream and open a handle.
 				If creating a stream which should continue to exist after the
				current context is finished, then TLStreamCreate must be 
				followed by a TLStreamOpen call. On any case, the number of 
				create/open calls must balance with the number of close calls
				used. This ensures the resources of a stream are released when
				it is no longer required.
 @Output        phStream        Pointer to handle to store the new stream.
 @Input         szStreamName    Name of stream, maximum length:
                                  PRVSRVTL_MAX_STREAM_NAME_SIZE.
                                  If a longer string is provided,creation fails.
 @Input         ui32Size        Desired buffer size in bytes.
 @Input         ui32StreamFlags Flags that configure buffer behaviour.See above.
 @Input         pfProducerDB    Optional callback, may be null.
 @Input         pvProducerData  Optional user data for callback, may be null.
 @Return        PVRSRV_ERROR_INVALID_PARAMS  NULL stream handle or string name 
                                               exceeded MAX_STREAM_NAME_SIZE
 @Return        PVRSRV_ERROR_OUT_OF_MEMORY   Failed to allocate space for stream
                                               handle.
 @Return        PVRSRV_ERROR_DUPLICATE_VALUE There already exists a stream with
 											   the same stream name string.
 @Return        eError                       Internal services call returned
                                               eError error number.
 @Return        PVRSRV_OK
*/ /**************************************************************************/
PVRSRV_ERROR 
TLStreamCreate(IMG_HANDLE *phStream,
               IMG_CHAR	  *szStreamName,
               IMG_UINT32 ui32Size,
               IMG_UINT32 ui32StreamFlags,
               TL_STREAM_ONREADEROPENCB pfOnReaderOpenCB,
               void *pvOnReaderOpenUD,
               TL_STREAM_SOURCECB pfProducerCB,
               void *pvProducerUD);

/*************************************************************************/ /*!
 @Function      TLStreamOpen
 @Description   Attach to existing stream that has already been created by a
                  TLStreamCreate call. A handle is returned to the stream.
 @Output        phStream        Pointer to handle to store the stream.
 @Input         szStreamName    Name of stream, should match an already
                                  existing stream name
 @Return        PVRSRV_ERROR_NOT_FOUND        None of the streams matched the
                                                 requested stream name.
				PVRSRV_ERROR_INVALID_PARAMS	   non NULL pointer to stream 
											     handler is required.
 @Return        PVRSRV_OK                      Success.
*/ /**************************************************************************/
PVRSRV_ERROR
TLStreamOpen(IMG_HANDLE *phStream,
             IMG_CHAR   *szStreamName);

/*************************************************************************/ /*!
 @Function      TLStreamReconfigure
 @Description   Request the stream flags controlling buffer behaviour to
                be updated.
                In the case where TL_FLAG_RESERVE_BLOCK is to be used,
                TLStreamCreate should be called without that flag and this
                function used to change the stream mode once a consumer process
                has been started. This avoids a deadlock scenario where the
                TLStreaWrite/TLStreamReserve call will hold the Bridge Lock
                while blocking if the TL buffer is full.
                The TL_FLAG_RESERVE_BLOCK should never drop the Bridge Lock
                as this leads to another deadlock scenario where the caller to
                TLStreamWrite/TLStreamReserve has already acquired another lock
                (eg. gHandleLock) which is not dropped. This then leads to that
                thead acquiring locks out of order.
 @Input         hStream         Handle to stream to update.
 @Input         ui32StreamFlags Flags that configure buffer behaviour. See above.
 @Return        PVRSRV_ERROR_INVALID_PARAMS  NULL stream handle or inconsistent
                                             stream flags.
 @Return        PVRSRV_ERROR_NOT_READY       Stream is currently being written to
                                             try again later.
 @Return        eError                       Internal services call returned
                                               eError error number.
 @Return        PVRSRV_OK
*/ /**************************************************************************/
PVRSRV_ERROR
TLStreamReconfigure(
		IMG_HANDLE hStream,
		IMG_UINT32 ui32StreamFlags);

/*************************************************************************/ /*!
 @Function      TLStreamClose
 @Description   Detach from the stream associated with the given handle. If
                  the current handle is the last one accessing the stream 
				  (i.e. the number of TLStreamCreate+TLStreamOpen calls matches
				  the number of TLStreamClose calls) then the stream is also
				  deleted.
				On return the handle is no longer valid.
 @Input         hStream     Handle to stream that will be closed.
 @Return        None.
*/ /**************************************************************************/
void
TLStreamClose(IMG_HANDLE hStream);

/*************************************************************************/ /*!
 @Function      TLStreamReserve
 @Description   Reserve space in stream buffer. When successful every
                  TLStreamReserve call must be followed by a matching
                  TLStreamCommit call. While a TLStreamCommit call is pending
                  for a stream, subsequent TLStreamReserve calls for this
                  stream will fail.
 @Input         hStream         Stream handle.
 @Output        ppui8Data       Pointer to a pointer to a location in the 
                                  buffer. The caller can then use this address
                                  in writing data into the stream. 
 @Input         ui32Size        Number of bytes to reserve in buffer.
 @Return        PVRSRV_INVALID_PARAMS       NULL stream handler.
 @Return        PVRSRV_ERROR_NOT_READY      There are data previously reserved
                                              that are pending to be committed.
 @Return        PVRSRV_ERROR_STREAM_MISUSE  Misusing the stream by trying to 
                                              reserve more space than the 
                                              buffer size.
 @Return        PVRSRV_ERROR_STREAM_RESERVE_TOO_BIG  The reserve size requested
                                                     is larger than the free
                                                     space or maximum supported
                                                     packet size.
 @Return        PVRSRV_OK                   Success, output arguments valid.
*/ /**************************************************************************/
PVRSRV_ERROR 
TLStreamReserve(IMG_HANDLE hStream, 
                IMG_UINT8  **ppui8Data,
                IMG_UINT32 ui32Size);

/*************************************************************************/ /*!
 @Function      TLStreamReserve2
 @Description   Reserve space in stream buffer. When successful every
                  TLStreamReserve call must be followed by a matching
                  TLStreamCommit call. While a TLStreamCommit call is pending
                  for a stream, subsequent TLStreamReserve calls for this
                  stream will fail.
 @Input         hStream         Stream handle.
 @Output        ppui8Data       Pointer to a pointer to a location in the
                                  buffer. The caller can then use this address
                                  in writing data into the stream.
 @Input         ui32Size        Ideal number of bytes to reserve in buffer.
 @Input         ui32SizeMin     Minimum number of bytes to reserve in buffer.
 @Input         pui32Available  Optional, but when present and the
                                  RESERVE_TOO_BIG error is returned, a size
                                  suggestion is returned in this argument which
                                  the caller can attempt to reserve again for a
                                  successful allocation.
 @Return        PVRSRV_INVALID_PARAMS       NULL stream handler.
 @Return        PVRSRV_ERROR_NOT_READY      There are data previously reserved
                                              that are pending to be committed.
 @Return        PVRSRV_ERROR_STREAM_MISUSE  Misusing the stream by trying to
                                              reserve more space than the
                                              buffer size.
 @Return        PVRSRV_ERROR_STREAM_RESERVE_TOO_BIG  The reserve size requested
                                                     is larger than the free
                                                     space or maximum supported
                                                     packet size.
                                                     Check the pui32Available
                                                     value for the correct
                                                     reserve size to use.
 @Return        PVRSRV_OK                   Success, output arguments valid.
*/ /**************************************************************************/
PVRSRV_ERROR
TLStreamReserve2(IMG_HANDLE hStream,
                IMG_UINT8  **ppui8Data,
                IMG_UINT32 ui32Size,
                IMG_UINT32 ui32SizeMin,
                IMG_UINT32* pui32Available);

/*************************************************************************/ /*!
 @Function      TLStreamCommit
 @Description   Notify TL that data have been written in the stream buffer.
                  Should always follow and match TLStreamReserve call.
 @Input         hStream         Stream handle.
 @Input         ui32Size        Number of bytes that have been added to the
                                  stream.
 @Return        PVRSRV_ERROR_INVALID_PARAMS  NULL stream handle.
 @Return        PVRSRV_ERROR_STREAM_MISUSE   Commit results in more data 
                                               committed than the buffer size,
                                               the stream is misused.
 @Return        eError                       Commit was successful but 
                                               internal services call returned
                                               eError error number.
 @Return        PVRSRV_OK
*/ /**************************************************************************/
PVRSRV_ERROR 
TLStreamCommit(IMG_HANDLE hStream,
               IMG_UINT32 ui32Size);

/*************************************************************************/ /*!
 @Function      TLStreamWrite
 @Description   Combined Reserve/Commit call. This function Reserves space in 
                  the specified stream buffer, copies ui32Size bytes of data
                  from the array pui8Src points to and Commits in an "atomic"
                  style operation.
 @Input         hStream         Stream handle.
 @Input         pui8Src         Source to read data from.
 @Input         ui32Size        Number of bytes to copy and commit.
 @Return        PVRSRV_ERROR_INVALID_PARAMS  NULL stream handler.
 @Return        eError                       Error codes returned by either 
                                               Reserve or Commit.
 @Return        PVRSRV_OK
 */ /**************************************************************************/
PVRSRV_ERROR 
TLStreamWrite(IMG_HANDLE hStream, 
              IMG_UINT8  *pui8Src,
              IMG_UINT32 ui32Size);

/*************************************************************************/ /*!
 @Function      TLStreamSync
 @Description   Signal the consumer to start acquiring data from the stream
                buffer. Called by producers that use the TL_FLAG_NO_SIGNAL_ON_COMMIT
                flag to manually control when consumers starting reading the
                stream. Used when multiple small writes need to be batched.
 @Input         hStream         Stream handle.
 @Return        PVRSRV_ERROR_INVALID_PARAMS  NULL stream handle.
 @Return        eError                       Error codes returned by either
                                               Reserve or Commit.
 @Return        PVRSRV_OK
 */ /**************************************************************************/
PVRSRV_ERROR
TLStreamSync(IMG_HANDLE hStream);


/*************************************************************************/ /*!
 @Function      TLStreamMarkEOS
 @Description   Insert a EOS marker packet in the given stream.
 @Input         hStream         Stream handle.
 @Return        PVRSRV_ERROR_INVALID_PARAMS	NULL stream handler.
 @Return        eError                     	Error codes returned by either
                                              Reserve or Commit.
 @Return        PVRSRV_OK       			Success.
*/ /**************************************************************************/
PVRSRV_ERROR 
TLStreamMarkEOS(IMG_HANDLE hStream);

/*************************************************************************/ /*!
 @Function      TLStreamInfo
 @Description   Run time information about buffer elemental sizes.
                It sets psInfo members accordingly. Users can use those values
                to calculate the parameters they use in TLStreamCreate and 
                TLStreamReserve.
 @Output        psInfo          pointer to stream info structure.
 @Return        None.
*/ /**************************************************************************/
void
TLStreamInfo(PTL_STREAM_INFO psInfo);


#endif /* __TLSTREAM_H__ */
/*****************************************************************************
 End of file (tlstream.h)
*****************************************************************************/

