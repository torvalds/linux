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
#ifndef __PVR_TLCOMMON_H__
#define __PVR_TLCOMMON_H__

#if defined (__cplusplus)
extern "C" {
#endif

#include "img_defs.h"


/*! Handle type for stream descriptor objects as created by this API */
typedef IMG_HANDLE PVRSRVTL_SD;


/*! Maximum stream name length including the null byte */
#define PRVSRVTL_MAX_STREAM_NAME_SIZE	20U

/*! Packet lengths are always rounded up to a multiple of 4 bytes */
#define PVRSRVTL_PACKET_ALIGNMENT		4U
#define PVRSRVTL_ALIGN(x) 				((x+PVRSRVTL_PACKET_ALIGNMENT-1) & ~(PVRSRVTL_PACKET_ALIGNMENT-1))


/*! A packet is made up of a header structure followed by the data bytes.
 * There are 3 types of packet: normal (has data), data lost and padding,
 * see packet flags. Header kept small to reduce data overhead.
 *
 * if the ORDER of the structure members is changed, please UPDATE the 
 *   PVRSRVTL_PACKET_FLAG_OFFSET macro.
 */
typedef struct _PVRSRVTL_PACKETHDR_
{
	IMG_UINT16 uiDataLen;	/*!< Number of bytes following header */
	IMG_UINT16 uiFlags;		/*!< Packet flag word */

	/* First bytes of data ... */
	//IMG_UINT32 ui32Data;	// ... variable length data array

} PVRSRVTL_PACKETHDR, *PVRSRVTL_PPACKETHDR;

/* Structure must always be a size multiple of 4 as stream buffer
 * still an array of IMG_UINT32s.
 */
BLD_ASSERT((sizeof(PVRSRVTL_PACKETHDR)&3)==0, pvr_tlcommon_h)

/*! Packet header mask used to extract the type from the uiFlags member.
 * Do not use directly, \see TEST_PACKET_FLAG
 */
#define PVRSRVTL_PACKETHDR_TYPE_MASK			0x000f

/*! Packet header mask used to extract the flags from the uiFlags member.
 * Do not use directly, \see GET_PACKET_TYPE
 */
#define PVRSRVTL_PACKETHDR_FLAG_MASK			0xfff0

/*! Packet type enumeration.
 */
typedef enum _PVRSRVTL_PACKETTYPE_
{
	/*! Undefined packet */
	PVRSRVTL_PACKETTYPE_UNDEF = 0,

	/*! Normal packet type. Indicates data follows the header.
	 */
	PVRSRVTL_PACKETTYPE_DATA = 1,

	/*! When seen this packet type indicates that at this moment in the stream
	 * packet(s) were not able to be accepted due to space constraints and that
	 * recent data may be lost - depends on how the producer handles the
	 * error. Such packets have no data, data length is 0.
	 */
	PVRSRVTL_PACKETTYPE_MOST_RECENT_WRITE_FAILED = 2,

	/*! Packets with this type set are padding packets that contain undefined
	 * data and must be ignored/skipped by the client. They are used when the
	 * circular stream buffer wraps around and there is not enough space for
	 * the data at the end of the buffer. Such packets have a length of 0 or
	 * more.
	 */
	PVRSRVTL_PACKETTYPE_PADDING = 3,

	/*! This packet type conveys to the stream consumer that the stream producer
	 * has reached the end of data for that data sequence. The TLDaemon
	 * has several options for processing these packets that can be selected
	 * on a per stream basis.
	 */
	PVRSRVTL_PACKETTYPE_MARKER_EOS = 4,

	PVRSRVTL_PACKETTYPE_LAST = PVRSRVTL_PACKETTYPE_MARKER_EOS
} PVRSRVTL_PACKETTYPE;

/* The SET_PACKET_* macros rely on the order the PVRSRVTL_PACKETHDR members are declared:
 * uiFlags is the upper half of a structure consisting of 2 uint16 quantities.
 */
#define PVRSRVTL_PACKET_FLAG_OFFSET		(8 * sizeof( ((PVRSRVTL_PACKETHDR *)NULL)->uiDataLen ))
#define PVRSRVTL_SET_PACKET_DATA(len)   	(len) | (PVRSRVTL_PACKETTYPE_DATA                  <<PVRSRVTL_PACKET_FLAG_OFFSET)
#define PVRSRVTL_SET_PACKET_PADDING(len)	(len) | (PVRSRVTL_PACKETTYPE_PADDING               <<PVRSRVTL_PACKET_FLAG_OFFSET)
#define PVRSRVTL_SET_PACKET_HDR(len,type)	(len) | ((type)						               <<PVRSRVTL_PACKET_FLAG_OFFSET)
#define PVRSRVTL_SET_PACKET_WRITE_FAILED           (PVRSRVTL_PACKETTYPE_MOST_RECENT_WRITE_FAILED <<PVRSRVTL_PACKET_FLAG_OFFSET)

/*! Returns the number of bytes of data in the packet. p may be any address type
 * */
#define GET_PACKET_DATA_LEN(p)	\
	((IMG_UINT32) ((PVRSRVTL_PPACKETHDR)(p))->uiDataLen )

/*! Returns a IMG_BYTE* pointer to the first byte of data in the packet */
#define GET_PACKET_DATA_PTR(p)	\
	((IMG_PBYTE) ( ((IMG_SIZE_T)p) + sizeof(PVRSRVTL_PACKETHDR)) )

/*! Given a PVRSRVTL_PPACKETHDR address, return the address of the next pack
 *  It is up to the caller to determine if the new address is within the packet
 *  buffer.
 */
#define GET_NEXT_PACKET_ADDR(p) \
	((PVRSRVTL_PPACKETHDR) ( ((IMG_UINT8 *)p) + sizeof(PVRSRVTL_PACKETHDR) + \
	(((((PVRSRVTL_PPACKETHDR)p)->uiDataLen) + \
	(PVRSRVTL_PACKET_ALIGNMENT-1)) & (~(PVRSRVTL_PACKET_ALIGNMENT-1)) ) ))

/*! Turns the packet address p into a PVRSRVTL_PPACKETHDR pointer type
 */
#define GET_PACKET_HDR(p)		((PVRSRVTL_PPACKETHDR)(p))

/*! Get the type of the packet. p is of type PVRSRVTL_PPACKETHDR
 */
#define GET_PACKET_TYPE(p)		(((p)->uiFlags & PVRSRVTL_PACKETHDR_TYPE_MASK))

/*! Tests if a packet flag is set or not. p is of type PVRSRVTL_PPACKETHDR and
 *  f is one of the flags
 */
#define TEST_PACKET_FLAG(p, f)	((p->uiFlags & (f)) ? IMG_TRUE : IMG_FALSE)


/*! Flags for use with PVRSRVTLOpenStream
 * 0x01 - Do not block in PVRSRVTLAcquireData() when no bytes are available
 * 0x02 - When the stream does not exist wait for a bit (2s) in
 *        PVRSRVTLOpenStream() and then exit with a timeout error if it still
 *        does not exist.
 */
#define PVRSRV_STREAM_FLAG_NONE                 (1U<<0)
#define PVRSRV_STREAM_FLAG_ACQUIRE_NONBLOCKING  (1U<<1)
#define PVRSRV_STREAM_FLAG_OPEN_WAIT            (1U<<2)

#if defined (__cplusplus)
}
#endif

#endif /* __PVR_TLCOMMON_H__ */
/******************************************************************************
 End of file (pvr_tlcommon.h)
******************************************************************************/

