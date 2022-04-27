/*************************************************************************/ /*!
@File
@Title          Transport Layer internals
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Transport Layer header used by TL internally
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
#ifndef TLINTERN_H
#define TLINTERN_H


#include "devicemem_typedefs.h"
#include "pvrsrv_tlcommon.h"
#include "lock.h"
#include "tlstream.h"

/* Forward declarations */
typedef struct _TL_SNODE_* PTL_SNODE;

/* To debug buffer utilisation enable this macro here and define
 * PVRSRV_NEED_PVR_TRACE in the server pvr_debug.c and in tutils.c
 * before the inclusion of pvr_debug.h.
 * Issue pvrtutils 6 on target to see stream buffer utilisation. */
//#define TL_BUFFER_STATS 1

/*! TL stream structure container.
 *    pbyBuffer   holds the circular buffer.
 *    ui32Read    points to the beginning of the buffer, ie to where data to
 *                  Read begin.
 *    ui32Write   points to the end of data that have been committed, ie this is
 *                  where new data will be written.
 *    ui32Pending number of bytes reserved in last reserve call which have not
 *                  yet been submitted. Therefore these data are not ready to
 *                  be transported.
 *    hStreamWLock - provides atomic protection for the ui32Pending & ui32Write
 *                   members of the structure for when they are checked and/or
 *                   updated in the context of a stream writer (producer)
 *                   calling DoTLStreamReserve() & TLStreamCommit().
 *                 - Reader context is not multi-threaded, only one client per
 *                   stream is allowed. Also note the read context may be in an
 *                   ISR which prevents a design where locks can be held in the
 *                   AcquireData/ReleaseData() calls. Thus this lock only
 *                   protects the stream members from simultaneous writers.
 *
 *      ui32Read < ui32Write <= ui32Pending
 *        where < and <= operators are overloaded to make sense in a circular way.
 */
typedef struct _TL_STREAM_
{
	IMG_CHAR                szName[PRVSRVTL_MAX_STREAM_NAME_SIZE];  /*!< String name identifier */
	TL_OPMODE               eOpMode;                                /*!< Mode of Operation of TL Buffer */

	IMG_BOOL                bWaitForEmptyOnDestroy;                 /*!< Flag: On destroying a non-empty stream block until
                                                                         *         stream is drained. */
	IMG_BOOL                bNoSignalOnCommit;                      /*!< Flag: Used to avoid the TL signalling waiting consumers
                                                                         *         that new data is available on every commit. Producers
                                                                         *         using this flag will need to manually signal when
                                                                         *         appropriate using the TLStreamSync() API */

	void                    (*pfOnReaderOpenCallback)(void *pvArg); /*!< Optional on reader connect callback */
	void                    *pvOnReaderOpenUserData;                /*!< On reader connect user data */
	void                    (*pfProducerCallback)(void);            /*!< Optional producer callback of type TL_STREAM_SOURCECB */
	void                    *pvProducerUserData;                    /*!< Producer callback user data */

	struct _TL_STREAM_      *psNotifStream;                         /*!< Pointer to the stream to which notification will be sent */

	volatile IMG_UINT32     ui32Read;                               /*!< Pointer to the beginning of available data */
	volatile IMG_UINT32     ui32Write;                              /*!< Pointer to already committed data which are ready to be
                                                                         *   copied to user space */
	IMG_UINT32              ui32Pending;                            /*!< Count pending bytes reserved in buffer */
	IMG_UINT32              ui32Size;                               /*!< Buffer size */
	IMG_UINT32              ui32ThresholdUsageForSignal;            /*!< Buffer usage threshold at which a TL writer signals a blocked/
	                                                                     *    waiting reader when transitioning from empty->non-empty */
	IMG_UINT32              ui32MaxPacketSize;                      /*! Max TL packet size */
	IMG_BYTE                *pbyBuffer;                             /*!< Actual data buffer */

	PTL_SNODE               psNode;                                 /*!< Ptr to parent stream node */
	DEVMEM_MEMDESC          *psStreamMemDesc;                       /*!< MemDescriptor used to allocate buffer space through PMR */

	IMG_HANDLE              hProducerEvent;	                        /*!< Handle to wait on if there is not enough space */
	IMG_HANDLE              hProducerEventObj;                      /*!< Handle to signal blocked reserve calls */
	IMG_BOOL                bSignalPending;                         /*!< Tracks if a "signal" is pending to be sent to a blocked/
	                                                                     *    waiting reader */

	POS_LOCK                hStreamWLock;                           /*!< Writers Lock for ui32Pending & ui32Write*/
	POS_LOCK                hReadLock;                              /*!< Readers Lock for bReadPending & ui32Read*/
	IMG_BOOL                bReadPending;                           /*!< Tracks if a read operation is pending or not*/
	IMG_BOOL                bNoWrapPermanent;                       /*!< Flag: Prevents buffer wrap and subsequent data loss
	                                                                     *    as well as resetting the read position on close. */

#if defined(TL_BUFFER_STATS)
	IMG_UINT32              ui32CntReadFails;                       /*!< Tracks how many times reader failed to acquire read lock */
	IMG_UINT32              ui32CntReadSuccesses;                   /*!< Tracks how many times reader acquires read lock successfully */
	IMG_UINT32              ui32CntWriteSuccesses;                  /*!< Tracks how many times writer acquires read lock successfully */
	IMG_UINT32              ui32CntWriteWaits;                      /*!< Tracks how many times writer had to wait to acquire read lock */
	IMG_UINT32              ui32CntNumWriteSuccess;	                /*!< Tracks how many write operations were successful*/
	IMG_UINT32              ui32BufferUt;                           /*!< Buffer utilisation high watermark, see TL_BUFFER_STATS above */
	IMG_UINT32              ui32MaxReserveWatermark;                /*!< Max stream reserve size that was ever requested by a writer */
	IMG_UINT32              ui32SignalsSent;                        /*!< Number of signals that were actually sent by the write API */
	ATOMIC_T                bNoReaderSinceFirstReserve;             /*!< Tracks if a read has been done since the buffer was last found empty */
	IMG_UINT32              ui32TimeStart;                          /*!< Time at which a write (Reserve call) was done into an empty buffer.
	                                                                     *    Guarded by hStreamWLock. */
	IMG_UINT32              ui32MinTimeToFullInUs;                  /*!< Minimum time taken to (nearly) fully fill an empty buffer. Guarded
	                                                                     *    by hStreamWLock. */
	/* Behaviour counters, protected by hStreamLock in case of
	 * multi-threaded access */
	IMG_UINT32              ui32NumCommits;     /*!< Counters used to analysing stream performance, see ++ loc */
	IMG_UINT32              ui32SignalNotSent;  /*!< Counters used to analysing stream performance, see ++ loc */
	IMG_UINT32              ui32ManSyncs;       /*!< Counters used to analysing stream performance, see ++ loc */
	IMG_UINT32              ui32ProducerByteCount; /*!< Counters used to analysing stream performance, see ++ loc */

	/* Not protected by the lock, inc in the reader thread which is currently singular */
	IMG_UINT32              ui32AcquireRead1;   /*!< Counters used to analysing stream performance, see ++ loc */
	IMG_UINT32              ui32AcquireRead2;   /*!< Counters used to analysing stream performance, see ++ loc */
#endif

} TL_STREAM, *PTL_STREAM;

/* there need to be enough space reserved in the buffer for 2 minimal packets
 * and it needs to be aligned the same way the buffer is or there will be a
 * compile error.*/
#define BUFFER_RESERVED_SPACE (2 * PVRSRVTL_PACKET_ALIGNMENT)

/* ensure the space reserved follows the buffer's alignment */
static_assert(!(BUFFER_RESERVED_SPACE&(PVRSRVTL_PACKET_ALIGNMENT-1)),
			  "BUFFER_RESERVED_SPACE must be a multiple of PVRSRVTL_PACKET_ALIGNMENT");

/* Define the largest value that a uint that matches the
 * PVRSRVTL_PACKET_ALIGNMENT size can hold */
#define MAX_UINT 0xffffFFFF

/*! Defines the value used for TL_STREAM.ui32Pending when no reserve is
 * outstanding on the stream. */
#define NOTHING_PENDING IMG_UINT32_MAX


/*
 * Transport Layer Stream Descriptor types/defs
 */
typedef struct _TL_STREAM_DESC_
{
	PTL_SNODE   psNode;         /*!< Ptr to parent stream node */
	IMG_UINT32  ui32Flags;      /*!< Flags supplied by client on stream open */
	IMG_HANDLE  hReadEvent;     /*!< For wait call (only used/set in reader descriptors) */
	IMG_INT     uiRefCount;     /*!< Reference count to the SD */

#if defined(TL_BUFFER_STATS)
	/* Behaviour counters, no multi-threading protection need as they are
	 * incremented in a single thread due to only supporting one reader
	 * at present */
	IMG_UINT32  ui32AcquireCount;  /*!< Counters used to analysing stream performance, see ++ loc */
	IMG_UINT32  ui32NoData;        /*!< Counters used to analysing stream performance, see ++ loc */
	IMG_UINT32  ui32NoDataSleep;   /*!< Counters used to analysing stream performance, see ++ loc */
	IMG_UINT32  ui32Signalled;     /*!< Counters used to analysing stream performance, see ++ loc */
	IMG_UINT32  ui32TimeoutEmpty;  /*!< Counters used to analysing stream performance, see ++ loc */
	IMG_UINT32  ui32TimeoutData;   /*!< Counters used to analysing stream performance, see ++ loc */
#endif
	IMG_UINT32  ui32ReadLimit;     /*!< Limit buffer reads to data present in the
                                        buffer at the time of stream open. */
	IMG_UINT32  ui32ReadLen;       /*!< Size of data returned by initial Acquire */
} TL_STREAM_DESC, *PTL_STREAM_DESC;

PTL_STREAM_DESC TLMakeStreamDesc(PTL_SNODE f1, IMG_UINT32 f2, IMG_HANDLE f3);

#define TL_STREAM_KM_FLAG_MASK	0xFFFF0000
#define TL_STREAM_FLAG_TEST		0x10000000
#define TL_STREAM_FLAG_WRAPREAD	0x00010000

#define TL_STREAM_UM_FLAG_MASK	0x0000FFFF

#if defined(TL_BUFFER_STATS)
#	define TL_COUNTER_INC(a)    ((a)++)
#	define TL_COUNTER_ADD(a,b)  ((a) += (b))
#else
#	define TL_COUNTER_INC(a)    (void)(0)
#	define TL_COUNTER_ADD(a,b)  (void)(0)
#endif
/*
 * Transport Layer stream list node
 */
typedef struct _TL_SNODE_
{
	struct _TL_SNODE_*	psNext;				/*!< Linked list next element */
	IMG_HANDLE			hReadEventObj;		/*!< Readers 'wait for data' event */
	PTL_STREAM			psStream;			/*!< TL Stream object */
	IMG_INT				uiWRefCount;		/*!< Stream writer reference count */
	PTL_STREAM_DESC		psRDesc;			/*!< Stream reader 0 or ptr only */
	PTL_STREAM_DESC		psWDesc;			/*!< Stream writer 0 or ptr only */
} TL_SNODE;

PTL_SNODE TLMakeSNode(IMG_HANDLE f2, TL_STREAM *f3, TL_STREAM_DESC *f4);

/*
 * Transport Layer global top types and variables
 * Use access function to obtain pointer.
 *
 * hTLGDLock - provides atomicity over read/check/write operations and
 *             sequence of operations on uiClientCnt, psHead list of SNODEs and
 *             the immediate members in a list element SNODE structure.
 *           - This larger scope of responsibility for this lock helps avoid
 *             the need for a lock in the SNODE structure.
 *           - Lock held in the client (reader) context when streams are
 *             opened/closed and in the server (writer) context when streams
 *             are created/open/closed.
 */
typedef struct _TL_GDATA_
{
	IMG_HANDLE hTLEventObj;        /* Global TL signal object, new streams, etc */

	IMG_UINT   uiClientCnt;        /* Counter to track the number of client stream connections. */
	PTL_SNODE  psHead;             /* List of TL streams and associated client handle */

	POS_LOCK   hTLGDLock;          /* Lock for structure AND psHead SNODE list */
} TL_GLOBAL_DATA, *PTL_GLOBAL_DATA;

/*
 * Transport Layer Internal Kernel-Mode Server API
 */
TL_GLOBAL_DATA* TLGGD(void);		/* TLGetGlobalData() */

PVRSRV_ERROR TLInit(void);
void TLDeInit(void);

void TLAddStreamNode(PTL_SNODE psAdd);
PTL_SNODE TLFindStreamNodeByName(const IMG_CHAR *pszName);
PTL_SNODE TLFindStreamNodeByDesc(PTL_STREAM_DESC psDesc);
IMG_UINT32 TLDiscoverStreamNodes(const IMG_CHAR *pszNamePattern,
                          IMG_CHAR aaszStreams[][PRVSRVTL_MAX_STREAM_NAME_SIZE],
                          IMG_UINT32 ui32Max);
PTL_SNODE TLFindAndGetStreamNodeByDesc(PTL_STREAM_DESC psDesc);
void TLReturnStreamNode(PTL_SNODE psNode);

/******************************************************************************
 Function Name	: TLTryRemoveStreamAndFreeStreamNode

 Inputs		: PTL_SNODE	Pointer to the TL_SNODE whose stream is requested
			to be removed from TL_GLOBAL_DATA's list

 Return Value	: IMG_TRUE	-	If the stream was made NULL and this
					TL_SNODE was removed from the
					TL_GLOBAL_DATA's list

		  IMG_FALSE	-	If the stream wasn't made NULL as there
					is a client connected to this stream

 Description	: If there is no client currently connected to this stream then,
			This function removes this TL_SNODE from the
			TL_GLOBAL_DATA's list. The caller is responsible for the
			cleanup of the TL_STREAM whose TL_SNODE may be removed

		  Otherwise, this function does nothing
******************************************************************************/
IMG_BOOL  TLTryRemoveStreamAndFreeStreamNode(PTL_SNODE psRemove);

/******************************************************************************
 Function Name	: TLUnrefDescAndTryFreeStreamNode

 Inputs		: PTL_SNODE	Pointer to the TL_SNODE whose descriptor is
			requested to be removed
			: PTL_STREAM_DESC	Pointer to the STREAM_DESC

 Return Value	: IMG_TRUE	-	If this	TL_SNODE was removed from the
					TL_GLOBAL_DATA's list

		  IMG_FALSE	-	Otherwise

 Description	: This function removes the stream descriptor from this TL_SNODE
			and, if there is no writer (producer context) currently bound to this
			stream, this function removes this TL_SNODE from the TL_GLOBAL_DATA's
			list. The caller is responsible for the cleanup of the TL_STREAM
			whose TL_SNODE may be removed
******************************************************************************/
IMG_BOOL TLUnrefDescAndTryFreeStreamNode(PTL_SNODE psRemove, PTL_STREAM_DESC psSD);

/*
 * Transport Layer stream interface to server part declared here to avoid
 * circular dependency.
 */
IMG_UINT32 TLStreamAcquireReadPos(PTL_STREAM psStream,
                                  IMG_BOOL bDisableCallback,
                                  IMG_UINT32* puiReadOffset);
PVRSRV_ERROR TLStreamAdvanceReadPos(PTL_STREAM psStream,
                                    IMG_UINT32 uiReadLen,
                                    IMG_UINT32 uiOrigReadLen);
void TLStreamResetReadPos(PTL_STREAM psStream);

DEVMEM_MEMDESC* TLStreamGetBufferPointer(PTL_STREAM psStream);
IMG_BOOL TLStreamOutOfData(IMG_HANDLE psStream);

/******************************************************************************
 Function Name	: TLStreamDestroy

 Inputs		: PTL_STREAM	Pointer to the TL_STREAM to be destroyed

 Description	: This function performs all the clean-up operations required for
			destruction of this stream
******************************************************************************/
void TLStreamDestroy(PTL_STREAM psStream);

/*
 * Test related functions
 */
PVRSRV_ERROR TUtilsInit(PVRSRV_DEVICE_NODE *psDeviceNode);
PVRSRV_ERROR TUtilsDeinit(PVRSRV_DEVICE_NODE *psDeviceNode);


#endif /* TLINTERN_H */
/******************************************************************************
 End of file (tlintern.h)
******************************************************************************/
