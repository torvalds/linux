/*************************************************************************/ /*!
@File
@Title          Services Transport Layer common types and definitions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Transport layer common types and definitions included into
                both user mode and kernel mode source.
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
#ifndef PVR_TLCOMMON_H
#define PVR_TLCOMMON_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "img_defs.h"


/*! Handle type for stream descriptor objects as created by this API */
typedef IMG_HANDLE PVRSRVTL_SD;

/*! Maximum stream name length including the null byte */
#define PRVSRVTL_MAX_STREAM_NAME_SIZE	40U

/*! Maximum number of streams expected to exist */
#define PVRSRVTL_MAX_DISCOVERABLE_STREAMS_BUFFER (32*PRVSRVTL_MAX_STREAM_NAME_SIZE)

/*! Packet lengths are always rounded up to a multiple of 8 bytes */
#define PVRSRVTL_PACKET_ALIGNMENT		8U
#define PVRSRVTL_ALIGN(x)				PVR_ALIGN(x, PVRSRVTL_PACKET_ALIGNMENT)


/*! A packet is made up of a header structure followed by the data bytes.
 * There are 3 types of packet: normal (has data), data lost and padding,
 * see packet flags. Header kept small to reduce data overhead.
 *
 * if the ORDER of the structure members is changed, please UPDATE the
 * PVRSRVTL_PACKET_FLAG_OFFSET macro.
 *
 * Layout of uiTypeSize member is :
 *
 * |<---------------------------32-bits------------------------------>|
 * |<----8---->|<-----1----->|<----7--->|<------------16------------->|
 * |    Type   | Drop-Oldest |  UNUSED  |             Size            |
 *
 */
typedef struct
{
	IMG_UINT32 uiTypeSize;	/*!< Type, Drop-Oldest flag & number of bytes following header */
	IMG_UINT32 uiReserved;	/*!< Reserve, packets and data must be 8 byte aligned */

	/* First bytes of TL packet data follow header ... */
} PVRSRVTL_PACKETHDR, *PVRSRVTL_PPACKETHDR;

/* Structure must always be a size multiple of 8 as stream buffer
 * still an array of IMG_UINT32s.
 */
static_assert((sizeof(PVRSRVTL_PACKETHDR) & (PVRSRVTL_PACKET_ALIGNMENT-1U)) == 0U,
			  "sizeof(PVRSRVTL_PACKETHDR) must be a multiple of 8");

/*! Packet header reserved word fingerprint "TLP1" */
#define PVRSRVTL_PACKETHDR_RESERVED 0x31504C54U

/*! Packet header mask used to extract the size from the uiTypeSize member.
 * Do not use directly, see GET macros.
 */
#define PVRSRVTL_PACKETHDR_SIZE_MASK    0x0000FFFFU
#define PVRSRVTL_MAX_PACKET_SIZE        (PVRSRVTL_PACKETHDR_SIZE_MASK & ~0xFU)


/*! Packet header mask used to extract the type from the uiTypeSize member.
 * Do not use directly, see GET macros.
 */
#define PVRSRVTL_PACKETHDR_TYPE_MASK    0xFF000000U
#define PVRSRVTL_PACKETHDR_TYPE_OFFSET  24U

/*! Packet header mask used to check if packets before this one were dropped
 * or not. Do not use directly, see GET macros.
 */
#define PVRSRVTL_PACKETHDR_OLDEST_DROPPED_MASK    0x00800000U
#define PVRSRVTL_PACKETHDR_OLDEST_DROPPED_OFFSET    23U

/*! Packet type enumeration.
 */
typedef IMG_UINT32 PVRSRVTL_PACKETTYPE;

/*! Undefined packet */
#define PVRSRVTL_PACKETTYPE_UNDEF 0U

/*! Normal packet type. Indicates data follows the header.
 */
#define PVRSRVTL_PACKETTYPE_DATA 1U

/*! When seen this packet type indicates that at this moment in the stream
 * packet(s) were not able to be accepted due to space constraints and
 * that recent data may be lost - depends on how the producer handles the
 * error. Such packets have no data, data length is 0.
 */
#define PVRSRVTL_PACKETTYPE_MOST_RECENT_WRITE_FAILED 2U

/*! Packets with this type set are padding packets that contain undefined
 * data and must be ignored/skipped by the client. They are used when the
 * circular stream buffer wraps around and there is not enough space for
 * the data at the end of the buffer. Such packets have a length of 0 or
 * more.
 */
#define PVRSRVTL_PACKETTYPE_PADDING 3U

/*! This packet type conveys to the stream consumer that the stream
 * producer has reached the end of data for that data sequence. The
 * TLDaemon has several options for processing these packets that can
 * be selected on a per stream basis.
 */
#define PVRSRVTL_PACKETTYPE_MARKER_EOS 4U

/*! This is same as PVRSRVTL_PACKETTYPE_MARKER_EOS but additionally removes
 * old data record output file before opening new/next one
 */
#define PVRSRVTL_PACKETTYPE_MARKER_EOS_REMOVEOLD 5U

/*! Packet emitted on first stream opened by writer. Packet carries a name
 * of the opened stream in a form of null-terminated string.
 */
#define PVRSRVTL_PACKETTYPE_STREAM_OPEN_FOR_WRITE 6U

/*! Packet emitted on last stream closed by writer. Packet carries a name
 * of the closed stream in a form of null-terminated string.
 */
#define PVRSRVTL_PACKETTYPE_STREAM_CLOSE_FOR_WRITE 7U

#define PVRSRVTL_PACKETTYPE_LAST 8U

/* The SET_PACKET_* macros rely on the order the PVRSRVTL_PACKETHDR members are declared:
 * uiFlags is the upper half of a structure consisting of 2 uint16 quantities.
 */
#define PVRSRVTL_SET_PACKET_DATA(len)       (len) | (PVRSRVTL_PACKETTYPE_DATA                     << PVRSRVTL_PACKETHDR_TYPE_OFFSET)
#define PVRSRVTL_SET_PACKET_PADDING(len)    (len) | (PVRSRVTL_PACKETTYPE_PADDING                  << PVRSRVTL_PACKETHDR_TYPE_OFFSET)
#define PVRSRVTL_SET_PACKET_WRITE_FAILED    (0U)   | (PVRSRVTL_PACKETTYPE_MOST_RECENT_WRITE_FAILED << PVRSRVTL_PACKETHDR_TYPE_OFFSET)
#define PVRSRVTL_SET_PACKET_HDR(len, type)  (len) | ((type)                                       << PVRSRVTL_PACKETHDR_TYPE_OFFSET)

/*! Returns the number of bytes of data in the packet.
 * p may be any address type.
 */
#define GET_PACKET_DATA_LEN(p)	\
	((IMG_UINT32) ((PVRSRVTL_PPACKETHDR) (void *) (p))->uiTypeSize & PVRSRVTL_PACKETHDR_SIZE_MASK)


/*! Returns a IMG_BYTE* pointer to the first byte of data in the packet */
#define GET_PACKET_DATA_PTR(p)	\
	(((IMG_UINT8 *) (void *) (p)) + sizeof(PVRSRVTL_PACKETHDR))

/*! Turns the packet address p into a PVRSRVTL_PPACKETHDR pointer type.
 */
#define GET_PACKET_HDR(p)		((PVRSRVTL_PPACKETHDR) ((void *) (p)))

/*! Given a PVRSRVTL_PPACKETHDR address, return the address of the next pack
 *  It is up to the caller to determine if the new address is within the
 *  packet buffer.
 */
#define GET_NEXT_PACKET_ADDR(p) \
	GET_PACKET_HDR( \
		GET_PACKET_DATA_PTR(p) + \
		( \
			(GET_PACKET_DATA_LEN(p) + (PVRSRVTL_PACKET_ALIGNMENT-1U)) & \
			(~(PVRSRVTL_PACKET_ALIGNMENT-1U)) \
		) \
	)

/*! Get the type of the packet. p is of type PVRSRVTL_PPACKETHDR.
 */
#define GET_PACKET_TYPE(p)		(((p)->uiTypeSize & PVRSRVTL_PACKETHDR_TYPE_MASK)>>PVRSRVTL_PACKETHDR_TYPE_OFFSET)

/*! Set PACKETS_DROPPED flag in packet header as a part of uiTypeSize.
 * p is of type PVRSRVTL_PPACKETHDR.
 */
#define SET_PACKETS_DROPPED(p)		(((p)->uiTypeSize) | (1UL << PVRSRVTL_PACKETHDR_OLDEST_DROPPED_OFFSET))

/*! Check if packets were dropped before this packet.
 * p is of type PVRSRVTL_PPACKETHDR.
 */
#define CHECK_PACKETS_DROPPED(p)	((((p)->uiTypeSize & PVRSRVTL_PACKETHDR_OLDEST_DROPPED_MASK)>>PVRSRVTL_PACKETHDR_OLDEST_DROPPED_OFFSET) != 0U)

/*! Flags for use with PVRSRVTLOpenStream
 * 0x01 - Do not block in PVRSRVTLAcquireData() when no bytes are available
 * 0x02 - When the stream does not exist wait for a bit (2s) in
 *        PVRSRVTLOpenStream() and then exit with a timeout error if it still
 *        does not exist.
 * 0x04 - Open stream for write only operations.
 *        If flag is not used stream is opened as read-only. This flag is
 *        required if one wants to call reserve/commit/write function on the
 *        stream descriptor. Read from on the stream descriptor opened
 *        with this flag will fail.
 * 0x08 - Disable Producer Callback.
 *        If this flag is set and the stream becomes empty, do not call any
 *        associated producer callback to generate more data from the reader
 *        context.
 * 0x10 - Reset stream on open.
 *        When this flag is used the stream will drop all of the stored data.
 * 0x20 - Limit read position to the write position at time the stream
 *        was opened. Hence this flag will freeze the content read to that
 *        produced before the stream was opened for reading.
 * 0x40 - Ignore Open Callback.
 *        When this flag is set ignore any OnReaderOpenCallback setting for
 *        the stream. This allows access to the stream to be made without
 *        generating any extra packets into the stream.
 */

#define PVRSRV_STREAM_FLAG_NONE                        (0U)
#define PVRSRV_STREAM_FLAG_ACQUIRE_NONBLOCKING         (1U<<0)
#define PVRSRV_STREAM_FLAG_OPEN_WAIT                   (1U<<1)
#define PVRSRV_STREAM_FLAG_OPEN_WO                     (1U<<2)
#define PVRSRV_STREAM_FLAG_DISABLE_PRODUCER_CALLBACK   (1U<<3)
#define PVRSRV_STREAM_FLAG_RESET_ON_OPEN               (1U<<4)
#define PVRSRV_STREAM_FLAG_READ_LIMIT                  (1U<<5)
#define PVRSRV_STREAM_FLAG_IGNORE_OPEN_CALLBACK        (1U<<6)


#if defined(__cplusplus)
}
#endif

#endif /* PVR_TLCOMMON_H */
/******************************************************************************
 End of file (pvrsrv_tlcommon.h)
******************************************************************************/
