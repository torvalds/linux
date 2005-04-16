/******************************************************************************
 *
 * Name:	skcsum.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.12 $
 * Date:	$Date: 2003/08/20 13:55:53 $
 * Purpose:	Store/verify Internet checksum in send/receive packets.
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2003 SysKonnect GmbH.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

#ifdef SK_USE_CSUM	/* Check if CSUM is to be used. */

#ifndef lint
static const char SysKonnectFileId[] =
	"@(#) $Id: skcsum.c,v 1.12 2003/08/20 13:55:53 mschmid Exp $ (C) SysKonnect.";
#endif	/* !lint */

/******************************************************************************
 *
 * Description:
 *
 * This is the "GEnesis" common module "CSUM".
 *
 * This module contains the code necessary to calculate, store, and verify the
 * Internet Checksum of IP, TCP, and UDP frames.
 *
 * "GEnesis" is an abbreviation of "Gigabit Ethernet Network System in Silicon"
 * and is the code name of this SysKonnect project.
 *
 * Compilation Options:
 *
 *	SK_USE_CSUM - Define if CSUM is to be used. Otherwise, CSUM will be an
 *	empty module.
 *
 *	SKCS_OVERWRITE_PROTO - Define to overwrite the default protocol id
 *	definitions. In this case, all SKCS_PROTO_xxx definitions must be made
 *	external.
 *
 *	SKCS_OVERWRITE_STATUS - Define to overwrite the default return status
 *	definitions. In this case, all SKCS_STATUS_xxx definitions must be made
 *	external.
 *
 * Include File Hierarchy:
 *
 *	"h/skdrv1st.h"
 *	"h/skcsum.h"
 *	"h/sktypes.h"
 *	"h/skqueue.h"
 *	"h/skdrv2nd.h"
 *
 ******************************************************************************/

#include "h/skdrv1st.h"
#include "h/skcsum.h"
#include "h/skdrv2nd.h"

/* defines ********************************************************************/

/* The size of an Ethernet MAC header. */
#define SKCS_ETHERNET_MAC_HEADER_SIZE			(6+6+2)

/* The size of the used topology's MAC header. */
#define	SKCS_MAC_HEADER_SIZE	SKCS_ETHERNET_MAC_HEADER_SIZE

/* The size of the IP header without any option fields. */
#define SKCS_IP_HEADER_SIZE						20

/*
 * Field offsets within the IP header.
 */

/* "Internet Header Version" and "Length". */
#define SKCS_OFS_IP_HEADER_VERSION_AND_LENGTH	0

/* "Total Length". */
#define SKCS_OFS_IP_TOTAL_LENGTH				2

/* "Flags" "Fragment Offset". */
#define SKCS_OFS_IP_FLAGS_AND_FRAGMENT_OFFSET	6

/* "Next Level Protocol" identifier. */
#define SKCS_OFS_IP_NEXT_LEVEL_PROTOCOL			9

/* Source IP address. */
#define SKCS_OFS_IP_SOURCE_ADDRESS				12

/* Destination IP address. */
#define SKCS_OFS_IP_DESTINATION_ADDRESS			16


/*
 * Field offsets within the UDP header.
 */

/* UDP checksum. */
#define SKCS_OFS_UDP_CHECKSUM					6

/* IP "Next Level Protocol" identifiers (see RFC 790). */
#define SKCS_PROTO_ID_TCP		6	/* Transport Control Protocol */
#define SKCS_PROTO_ID_UDP		17	/* User Datagram Protocol */

/* IP "Don't Fragment" bit. */
#define SKCS_IP_DONT_FRAGMENT	SKCS_HTON16(0x4000)

/* Add a byte offset to a pointer. */
#define SKCS_IDX(pPtr, Ofs)	((void *) ((char *) (pPtr) + (Ofs)))

/*
 * Macros that convert host to network representation and vice versa, i.e.
 * little/big endian conversion on little endian machines only.
 */
#ifdef SK_LITTLE_ENDIAN
#define SKCS_HTON16(Val16)	(((unsigned) (Val16) >> 8) | (((Val16) & 0xff) << 8))
#endif	/* SK_LITTLE_ENDIAN */
#ifdef SK_BIG_ENDIAN
#define SKCS_HTON16(Val16)	(Val16)
#endif	/* SK_BIG_ENDIAN */
#define SKCS_NTOH16(Val16)	SKCS_HTON16(Val16)

/* typedefs *******************************************************************/

/* function prototypes ********************************************************/

/******************************************************************************
 *
 *	SkCsGetSendInfo - get checksum information for a send packet
 *
 * Description:
 *	Get all checksum information necessary to send a TCP or UDP packet. The
 *	function checks the IP header passed to it. If the high-level protocol
 *	is either TCP or UDP the pseudo header checksum is calculated and
 *	returned.
 *
 *	The function returns the total length of the IP header (including any
 *	IP option fields), which is the same as the start offset of the IP data
 *	which in turn is the start offset of the TCP or UDP header.
 *
 *	The function also returns the TCP or UDP pseudo header checksum, which
 *	should be used as the start value for the hardware checksum calculation.
 *	(Note that any actual pseudo header checksum can never calculate to
 *	zero.)
 *
 * Note:
 *	There is a bug in the GENESIS ASIC which may lead to wrong checksums.
 *
 * Arguments:
 *	pAc - A pointer to the adapter context struct.
 *
 *	pIpHeader - Pointer to IP header. Must be at least the IP header *not*
 *	including any option fields, i.e. at least 20 bytes.
 *
 *	Note: This pointer will be used to address 8-, 16-, and 32-bit
 *	variables with the respective alignment offsets relative to the pointer.
 *	Thus, the pointer should point to a 32-bit aligned address. If the
 *	target system cannot address 32-bit variables on non 32-bit aligned
 *	addresses, then the pointer *must* point to a 32-bit aligned address.
 *
 *	pPacketInfo - A pointer to the packet information structure for this
 *	packet. Before calling this SkCsGetSendInfo(), the following field must
 *	be initialized:
 *
 *		ProtocolFlags - Initialize with any combination of
 *		SKCS_PROTO_XXX bit flags. SkCsGetSendInfo() will only work on
 *		the protocols specified here. Any protocol(s) not specified
 *		here will be ignored.
 *
 *		Note: Only one checksum can be calculated in hardware. Thus, if
 *		SKCS_PROTO_IP is specified in the 'ProtocolFlags',
 *		SkCsGetSendInfo() must calculate the IP header checksum in
 *		software. It might be a better idea to have the calling
 *		protocol stack calculate the IP header checksum.
 *
 * Returns: N/A
 *	On return, the following fields in 'pPacketInfo' may or may not have
 *	been filled with information, depending on the protocol(s) found in the
 *	packet:
 *
 *	ProtocolFlags - Returns the SKCS_PROTO_XXX bit flags of the protocol(s)
 *	that were both requested by the caller and actually found in the packet.
 *	Protocol(s) not specified by the caller and/or not found in the packet
 *	will have their respective SKCS_PROTO_XXX bit flags reset.
 *
 *	Note: For IP fragments, TCP and UDP packet information is ignored.
 *
 *	IpHeaderLength - The total length in bytes of the complete IP header
 *	including any option fields is returned here. This is the start offset
 *	of the IP data, i.e. the TCP or UDP header if present.
 *
 *	IpHeaderChecksum - If IP has been specified in the 'ProtocolFlags', the
 *	16-bit Internet Checksum of the IP header is returned here. This value
 *	is to be stored into the packet's 'IP Header Checksum' field.
 *
 *	PseudoHeaderChecksum - If this is a TCP or UDP packet and if TCP or UDP
 *	has been specified in the 'ProtocolFlags', the 16-bit Internet Checksum
 *	of the TCP or UDP pseudo header is returned here.
 */
void SkCsGetSendInfo(
SK_AC				*pAc,			/* Adapter context struct. */
void				*pIpHeader,		/* IP header. */
SKCS_PACKET_INFO	*pPacketInfo,	/* Packet information struct. */
int					NetNumber)		/* Net number */
{
	/* Internet Header Version found in IP header. */
	unsigned InternetHeaderVersion;

	/* Length of the IP header as found in IP header. */
	unsigned IpHeaderLength;

	/* Bit field specifiying the desired/found protocols. */
	unsigned ProtocolFlags;

	/* Next level protocol identifier found in IP header. */
	unsigned NextLevelProtocol;

	/* Length of IP data portion. */
	unsigned IpDataLength;

	/* TCP/UDP pseudo header checksum. */
	unsigned long PseudoHeaderChecksum;

	/* Pointer to next level protocol statistics structure. */
	SKCS_PROTO_STATS *NextLevelProtoStats;

	/* Temporary variable. */
	unsigned Tmp;

	Tmp = *(SK_U8 *)
		SKCS_IDX(pIpHeader, SKCS_OFS_IP_HEADER_VERSION_AND_LENGTH);

	/* Get the Internet Header Version (IHV). */
	/* Note: The IHV is stored in the upper four bits. */

	InternetHeaderVersion = Tmp >> 4;

	/* Check the Internet Header Version. */
	/* Note: We currently only support IP version 4. */

	if (InternetHeaderVersion != 4) {	/* IPv4? */
		SK_DBG_MSG(pAc, SK_DBGMOD_CSUM, SK_DBGCAT_ERR | SK_DBGCAT_TX,
			("Tx: Unknown Internet Header Version %u.\n",
			InternetHeaderVersion));
		pPacketInfo->ProtocolFlags = 0;
		pAc->Csum.ProtoStats[NetNumber][SKCS_PROTO_STATS_IP].TxUnableCts++;
		return;
	}

	/* Get the IP header length (IHL). */
	/*
	 * Note: The IHL is stored in the lower four bits as the number of
	 * 4-byte words.
	 */

	IpHeaderLength = (Tmp & 0xf) * 4;
	pPacketInfo->IpHeaderLength = IpHeaderLength;

	/* Check the IP header length. */

	/* 04-Aug-1998 sw - Really check the IHL? Necessary? */

	if (IpHeaderLength < 5*4) {
		SK_DBG_MSG(pAc, SK_DBGMOD_CSUM, SK_DBGCAT_ERR | SK_DBGCAT_TX,
			("Tx: Invalid IP Header Length %u.\n", IpHeaderLength));
		pPacketInfo->ProtocolFlags = 0;
		pAc->Csum.ProtoStats[NetNumber][SKCS_PROTO_STATS_IP].TxUnableCts++;
		return;
	}

	/* This is an IPv4 frame with a header of valid length. */

	pAc->Csum.ProtoStats[NetNumber][SKCS_PROTO_STATS_IP].TxOkCts++;

	/* Check if we should calculate the IP header checksum. */

	ProtocolFlags = pPacketInfo->ProtocolFlags;

	if (ProtocolFlags & SKCS_PROTO_IP) {
		pPacketInfo->IpHeaderChecksum =
			SkCsCalculateChecksum(pIpHeader, IpHeaderLength);
	}

	/* Get the next level protocol identifier. */

	NextLevelProtocol =
		*(SK_U8 *) SKCS_IDX(pIpHeader, SKCS_OFS_IP_NEXT_LEVEL_PROTOCOL);

	/*
	 * Check if this is a TCP or UDP frame and if we should calculate the
	 * TCP/UDP pseudo header checksum.
	 *
	 * Also clear all protocol bit flags of protocols not present in the
	 * frame.
	 */

	if ((ProtocolFlags & SKCS_PROTO_TCP) != 0 &&
		NextLevelProtocol == SKCS_PROTO_ID_TCP) {
		/* TCP/IP frame. */
		ProtocolFlags &= SKCS_PROTO_TCP | SKCS_PROTO_IP;
		NextLevelProtoStats =
			&pAc->Csum.ProtoStats[NetNumber][SKCS_PROTO_STATS_TCP];
	}
	else if ((ProtocolFlags & SKCS_PROTO_UDP) != 0 &&
		NextLevelProtocol == SKCS_PROTO_ID_UDP) {
		/* UDP/IP frame. */
		ProtocolFlags &= SKCS_PROTO_UDP | SKCS_PROTO_IP;
		NextLevelProtoStats =
			&pAc->Csum.ProtoStats[NetNumber][SKCS_PROTO_STATS_UDP];
	}
	else {
		/*
		 * Either not a TCP or UDP frame and/or TCP/UDP processing not
		 * specified.
		 */
		pPacketInfo->ProtocolFlags = ProtocolFlags & SKCS_PROTO_IP;
		return;
	}

	/* Check if this is an IP fragment. */

	/*
	 * Note: An IP fragment has a non-zero "Fragment Offset" field and/or
	 * the "More Fragments" bit set. Thus, if both the "Fragment Offset"
	 * and the "More Fragments" are zero, it is *not* a fragment. We can
	 * easily check both at the same time since they are in the same 16-bit
	 * word.
	 */

	if ((*(SK_U16 *)
		SKCS_IDX(pIpHeader, SKCS_OFS_IP_FLAGS_AND_FRAGMENT_OFFSET) &
		~SKCS_IP_DONT_FRAGMENT) != 0) {
		/* IP fragment; ignore all other protocols. */
		pPacketInfo->ProtocolFlags = ProtocolFlags & SKCS_PROTO_IP;
		NextLevelProtoStats->TxUnableCts++;
		return;
	}

	/*
	 * Calculate the TCP/UDP pseudo header checksum.
	 */

	/* Get total length of IP header and data. */

	IpDataLength =
		*(SK_U16 *) SKCS_IDX(pIpHeader, SKCS_OFS_IP_TOTAL_LENGTH);

	/* Get length of IP data portion. */

	IpDataLength = SKCS_NTOH16(IpDataLength) - IpHeaderLength;

	/* Calculate the sum of all pseudo header fields (16-bit). */

	PseudoHeaderChecksum =
		(unsigned long) *(SK_U16 *) SKCS_IDX(pIpHeader,
			SKCS_OFS_IP_SOURCE_ADDRESS + 0) +
		(unsigned long) *(SK_U16 *) SKCS_IDX(pIpHeader,
			SKCS_OFS_IP_SOURCE_ADDRESS + 2) +
		(unsigned long) *(SK_U16 *) SKCS_IDX(pIpHeader,
			SKCS_OFS_IP_DESTINATION_ADDRESS + 0) +
		(unsigned long) *(SK_U16 *) SKCS_IDX(pIpHeader,
			SKCS_OFS_IP_DESTINATION_ADDRESS + 2) +
		(unsigned long) SKCS_HTON16(NextLevelProtocol) +
		(unsigned long) SKCS_HTON16(IpDataLength);
	
	/* Add-in any carries. */

	SKCS_OC_ADD(PseudoHeaderChecksum, PseudoHeaderChecksum, 0);

	/* Add-in any new carry. */

	SKCS_OC_ADD(pPacketInfo->PseudoHeaderChecksum, PseudoHeaderChecksum, 0);

	pPacketInfo->ProtocolFlags = ProtocolFlags;
	NextLevelProtoStats->TxOkCts++;	/* Success. */
}	/* SkCsGetSendInfo */


/******************************************************************************
 *
 *	SkCsGetReceiveInfo - verify checksum information for a received packet
 *
 * Description:
 *	Verify a received frame's checksum. The function returns a status code
 *	reflecting the result of the verification.
 *
 * Note:
 *	Before calling this function you have to verify that the frame is
 *	not padded and Checksum1 and Checksum2 are bigger than 1.
 *
 * Arguments:
 *	pAc - Pointer to adapter context struct.
 *
 *	pIpHeader - Pointer to IP header. Must be at least the length in bytes
 *	of the received IP header including any option fields. For UDP packets,
 *	8 additional bytes are needed to access the UDP checksum.
 *
 *	Note: The actual length of the IP header is stored in the lower four
 *	bits of the first octet of the IP header as the number of 4-byte words,
 *	so it must be multiplied by four to get the length in bytes. Thus, the
 *	maximum IP header length is 15 * 4 = 60 bytes.
 *
 *	Checksum1 - The first 16-bit Internet Checksum calculated by the
 *	hardware starting at the offset returned by SkCsSetReceiveFlags().
 *
 *	Checksum2 - The second 16-bit Internet Checksum calculated by the
 *	hardware starting at the offset returned by SkCsSetReceiveFlags().
 *
 * Returns:
 *	SKCS_STATUS_UNKNOWN_IP_VERSION - Not an IP v4 frame.
 *	SKCS_STATUS_IP_CSUM_ERROR - IP checksum error.
 *	SKCS_STATUS_IP_CSUM_ERROR_TCP - IP checksum error in TCP frame.
 *	SKCS_STATUS_IP_CSUM_ERROR_UDP - IP checksum error in UDP frame
 *	SKCS_STATUS_IP_FRAGMENT - IP fragment (IP checksum ok).
 *	SKCS_STATUS_IP_CSUM_OK - IP checksum ok (not a TCP or UDP frame).
 *	SKCS_STATUS_TCP_CSUM_ERROR - TCP checksum error (IP checksum ok).
 *	SKCS_STATUS_UDP_CSUM_ERROR - UDP checksum error (IP checksum ok).
 *	SKCS_STATUS_TCP_CSUM_OK - IP and TCP checksum ok.
 *	SKCS_STATUS_UDP_CSUM_OK - IP and UDP checksum ok.
 *	SKCS_STATUS_IP_CSUM_OK_NO_UDP - IP checksum OK and no UDP checksum.
 *
 *	Note: If SKCS_OVERWRITE_STATUS is defined, the SKCS_STATUS_XXX values
 *	returned here can be defined in some header file by the module using CSUM.
 *	In this way, the calling module can assign return values for its own needs,
 *	e.g. by assigning bit flags to the individual protocols.
 */
SKCS_STATUS SkCsGetReceiveInfo(
SK_AC		*pAc,		/* Adapter context struct. */
void		*pIpHeader,	/* IP header. */
unsigned	Checksum1,	/* Hardware checksum 1. */
unsigned	Checksum2,	/* Hardware checksum 2. */
int			NetNumber)	/* Net number */
{
	/* Internet Header Version found in IP header. */
	unsigned InternetHeaderVersion;

	/* Length of the IP header as found in IP header. */
	unsigned IpHeaderLength;

	/* Length of IP data portion. */
	unsigned IpDataLength;

	/* IP header checksum. */
	unsigned IpHeaderChecksum;

	/* IP header options checksum, if any. */
	unsigned IpOptionsChecksum;

	/* IP data checksum, i.e. TCP/UDP checksum. */
	unsigned IpDataChecksum;

	/* Next level protocol identifier found in IP header. */
	unsigned NextLevelProtocol;

	/* The checksum of the "next level protocol", i.e. TCP or UDP. */
	unsigned long NextLevelProtocolChecksum;

	/* Pointer to next level protocol statistics structure. */
	SKCS_PROTO_STATS *NextLevelProtoStats;

	/* Temporary variable. */
	unsigned Tmp;

	Tmp = *(SK_U8 *)
		SKCS_IDX(pIpHeader, SKCS_OFS_IP_HEADER_VERSION_AND_LENGTH);

	/* Get the Internet Header Version (IHV). */
	/* Note: The IHV is stored in the upper four bits. */

	InternetHeaderVersion = Tmp >> 4;

	/* Check the Internet Header Version. */
	/* Note: We currently only support IP version 4. */

	if (InternetHeaderVersion != 4) {	/* IPv4? */
		SK_DBG_MSG(pAc, SK_DBGMOD_CSUM, SK_DBGCAT_ERR | SK_DBGCAT_RX,
			("Rx: Unknown Internet Header Version %u.\n",
			InternetHeaderVersion));
		pAc->Csum.ProtoStats[NetNumber][SKCS_PROTO_STATS_IP].RxUnableCts++;
		return (SKCS_STATUS_UNKNOWN_IP_VERSION);
	}

	/* Get the IP header length (IHL). */
	/*
	 * Note: The IHL is stored in the lower four bits as the number of
	 * 4-byte words.
	 */

	IpHeaderLength = (Tmp & 0xf) * 4;

	/* Check the IP header length. */

	/* 04-Aug-1998 sw - Really check the IHL? Necessary? */

	if (IpHeaderLength < 5*4) {
		SK_DBG_MSG(pAc, SK_DBGMOD_CSUM, SK_DBGCAT_ERR | SK_DBGCAT_RX,
			("Rx: Invalid IP Header Length %u.\n", IpHeaderLength));
		pAc->Csum.ProtoStats[NetNumber][SKCS_PROTO_STATS_IP].RxErrCts++;
		return (SKCS_STATUS_IP_CSUM_ERROR);
	}

	/* This is an IPv4 frame with a header of valid length. */

	/* Get the IP header and data checksum. */

	IpDataChecksum = Checksum2;

	/*
	 * The IP header checksum is calculated as follows:
	 *
	 *	IpHeaderChecksum = Checksum1 - Checksum2
	 */

	SKCS_OC_SUB(IpHeaderChecksum, Checksum1, Checksum2);

	/* Check if any IP header options. */

	if (IpHeaderLength > SKCS_IP_HEADER_SIZE) {

		/* Get the IP options checksum. */

		IpOptionsChecksum = SkCsCalculateChecksum(
			SKCS_IDX(pIpHeader, SKCS_IP_HEADER_SIZE),
			IpHeaderLength - SKCS_IP_HEADER_SIZE);

		/* Adjust the IP header and IP data checksums. */

		SKCS_OC_ADD(IpHeaderChecksum, IpHeaderChecksum, IpOptionsChecksum);

		SKCS_OC_SUB(IpDataChecksum, IpDataChecksum, IpOptionsChecksum);
	}

	/*
	 * Check if the IP header checksum is ok.
	 *
	 * NOTE: We must check the IP header checksum even if the caller just wants
	 * us to check upper-layer checksums, because we cannot do any further
	 * processing of the packet without a valid IP checksum.
	 */
	
	/* Get the next level protocol identifier. */
	
	NextLevelProtocol = *(SK_U8 *)
		SKCS_IDX(pIpHeader, SKCS_OFS_IP_NEXT_LEVEL_PROTOCOL);

	if (IpHeaderChecksum != 0xffff) {
		pAc->Csum.ProtoStats[NetNumber][SKCS_PROTO_STATS_IP].RxErrCts++;
		/* the NDIS tester wants to know the upper level protocol too */
		if (NextLevelProtocol == SKCS_PROTO_ID_TCP) {
			return(SKCS_STATUS_IP_CSUM_ERROR_TCP);
		}
		else if (NextLevelProtocol == SKCS_PROTO_ID_UDP) {
			return(SKCS_STATUS_IP_CSUM_ERROR_UDP);
		}
		return (SKCS_STATUS_IP_CSUM_ERROR);
	}

	/*
	 * Check if this is a TCP or UDP frame and if we should calculate the
	 * TCP/UDP pseudo header checksum.
	 *
	 * Also clear all protocol bit flags of protocols not present in the
	 * frame.
	 */

	if ((pAc->Csum.ReceiveFlags[NetNumber] & SKCS_PROTO_TCP) != 0 &&
		NextLevelProtocol == SKCS_PROTO_ID_TCP) {
		/* TCP/IP frame. */
		NextLevelProtoStats =
			&pAc->Csum.ProtoStats[NetNumber][SKCS_PROTO_STATS_TCP];
	}
	else if ((pAc->Csum.ReceiveFlags[NetNumber] & SKCS_PROTO_UDP) != 0 &&
		NextLevelProtocol == SKCS_PROTO_ID_UDP) {
		/* UDP/IP frame. */
		NextLevelProtoStats =
			&pAc->Csum.ProtoStats[NetNumber][SKCS_PROTO_STATS_UDP];
	}
	else {
		/*
		 * Either not a TCP or UDP frame and/or TCP/UDP processing not
		 * specified.
		 */
		return (SKCS_STATUS_IP_CSUM_OK);
	}

	/* Check if this is an IP fragment. */

	/*
	 * Note: An IP fragment has a non-zero "Fragment Offset" field and/or
	 * the "More Fragments" bit set. Thus, if both the "Fragment Offset"
	 * and the "More Fragments" are zero, it is *not* a fragment. We can
	 * easily check both at the same time since they are in the same 16-bit
	 * word.
	 */

	if ((*(SK_U16 *)
		SKCS_IDX(pIpHeader, SKCS_OFS_IP_FLAGS_AND_FRAGMENT_OFFSET) &
		~SKCS_IP_DONT_FRAGMENT) != 0) {
		/* IP fragment; ignore all other protocols. */
		NextLevelProtoStats->RxUnableCts++;
		return (SKCS_STATUS_IP_FRAGMENT);
	}

	/*
	 * 08-May-2000 ra
	 *
	 * From RFC 768 (UDP)
	 * If the computed checksum is zero, it is transmitted as all ones (the
	 * equivalent in one's complement arithmetic).  An all zero transmitted
	 * checksum value means that the transmitter generated no checksum (for
	 * debugging or for higher level protocols that don't care).
	 */

	if (NextLevelProtocol == SKCS_PROTO_ID_UDP &&
		*(SK_U16*)SKCS_IDX(pIpHeader, IpHeaderLength + 6) == 0x0000) {

		NextLevelProtoStats->RxOkCts++;
		
		return (SKCS_STATUS_IP_CSUM_OK_NO_UDP);
	}

	/*
	 * Calculate the TCP/UDP checksum.
	 */

	/* Get total length of IP header and data. */

	IpDataLength =
		*(SK_U16 *) SKCS_IDX(pIpHeader, SKCS_OFS_IP_TOTAL_LENGTH);

	/* Get length of IP data portion. */

	IpDataLength = SKCS_NTOH16(IpDataLength) - IpHeaderLength;

	NextLevelProtocolChecksum =

		/* Calculate the pseudo header checksum. */

		(unsigned long) *(SK_U16 *) SKCS_IDX(pIpHeader,
			SKCS_OFS_IP_SOURCE_ADDRESS + 0) +
		(unsigned long) *(SK_U16 *) SKCS_IDX(pIpHeader,
			SKCS_OFS_IP_SOURCE_ADDRESS + 2) +
		(unsigned long) *(SK_U16 *) SKCS_IDX(pIpHeader,
			SKCS_OFS_IP_DESTINATION_ADDRESS + 0) +
		(unsigned long) *(SK_U16 *) SKCS_IDX(pIpHeader,
			SKCS_OFS_IP_DESTINATION_ADDRESS + 2) +
		(unsigned long) SKCS_HTON16(NextLevelProtocol) +
		(unsigned long) SKCS_HTON16(IpDataLength) +

		/* Add the TCP/UDP header checksum. */

		(unsigned long) IpDataChecksum;

	/* Add-in any carries. */

	SKCS_OC_ADD(NextLevelProtocolChecksum, NextLevelProtocolChecksum, 0);

	/* Add-in any new carry. */

	SKCS_OC_ADD(NextLevelProtocolChecksum, NextLevelProtocolChecksum, 0);

	/* Check if the TCP/UDP checksum is ok. */

	if ((unsigned) NextLevelProtocolChecksum == 0xffff) {

		/* TCP/UDP checksum ok. */

		NextLevelProtoStats->RxOkCts++;

		return (NextLevelProtocol == SKCS_PROTO_ID_TCP ?
			SKCS_STATUS_TCP_CSUM_OK : SKCS_STATUS_UDP_CSUM_OK);
	}
	
	/* TCP/UDP checksum error. */

	NextLevelProtoStats->RxErrCts++;

	return (NextLevelProtocol == SKCS_PROTO_ID_TCP ?
		SKCS_STATUS_TCP_CSUM_ERROR : SKCS_STATUS_UDP_CSUM_ERROR);
}	/* SkCsGetReceiveInfo */


/******************************************************************************
 *
 *	SkCsSetReceiveFlags - set checksum receive flags
 *
 * Description:
 *	Use this function to set the various receive flags. According to the
 *	protocol flags set by the caller, the start offsets within received
 *	packets of the two hardware checksums are returned. These offsets must
 *	be stored in all receive descriptors.
 *
 * Arguments:
 *	pAc - Pointer to adapter context struct.
 *
 *	ReceiveFlags - Any combination of SK_PROTO_XXX flags of the protocols
 *	for which the caller wants checksum information on received frames.
 *
 *	pChecksum1Offset - The start offset of the first receive descriptor
 *	hardware checksum to be calculated for received frames is returned
 *	here.
 *
 *	pChecksum2Offset - The start offset of the second receive descriptor
 *	hardware checksum to be calculated for received frames is returned
 *	here.
 *
 * Returns: N/A
 *	Returns the two hardware checksum start offsets.
 */
void SkCsSetReceiveFlags(
SK_AC		*pAc,				/* Adapter context struct. */
unsigned	ReceiveFlags,		/* New receive flags. */
unsigned	*pChecksum1Offset,	/* Offset for hardware checksum 1. */
unsigned	*pChecksum2Offset,	/* Offset for hardware checksum 2. */
int			NetNumber)
{
	/* Save the receive flags. */

	pAc->Csum.ReceiveFlags[NetNumber] = ReceiveFlags;

	/* First checksum start offset is the IP header. */
	*pChecksum1Offset = SKCS_MAC_HEADER_SIZE;

	/*
	 * Second checksum start offset is the IP data. Note that this may vary
	 * if there are any IP header options in the actual packet.
	 */
	*pChecksum2Offset = SKCS_MAC_HEADER_SIZE + SKCS_IP_HEADER_SIZE;
}	/* SkCsSetReceiveFlags */

#ifndef SK_CS_CALCULATE_CHECKSUM

/******************************************************************************
 *
 *	SkCsCalculateChecksum - calculate checksum for specified data
 *
 * Description:
 *	Calculate and return the 16-bit Internet Checksum for the specified
 *	data.
 *
 * Arguments:
 *	pData - Pointer to data for which the checksum shall be calculated.
 *	Note: The pointer should be aligned on a 16-bit boundary.
 *
 *	Length - Length in bytes of data to checksum.
 *
 * Returns:
 *	The 16-bit Internet Checksum for the specified data.
 *
 *	Note: The checksum is calculated in the machine's natural byte order,
 *	i.e. little vs. big endian. Thus, the resulting checksum is different
 *	for the same input data on little and big endian machines.
 *
 *	However, when written back to the network packet, the byte order is
 *	always in correct network order.
 */
unsigned SkCsCalculateChecksum(
void		*pData,		/* Data to checksum. */
unsigned	Length)		/* Length of data. */
{
	SK_U16 *pU16;		/* Pointer to the data as 16-bit words. */
	unsigned long Checksum;	/* Checksum; must be at least 32 bits. */

	/* Sum up all 16-bit words. */

	pU16 = (SK_U16 *) pData;
	for (Checksum = 0; Length > 1; Length -= 2) {
		Checksum += *pU16++;
	}

	/* If this is an odd number of bytes, add-in the last byte. */

	if (Length > 0) {
#ifdef SK_BIG_ENDIAN
		/* Add the last byte as the high byte. */
		Checksum += ((unsigned) *(SK_U8 *) pU16) << 8;
#else	/* !SK_BIG_ENDIAN */
		/* Add the last byte as the low byte. */
		Checksum += *(SK_U8 *) pU16;
#endif	/* !SK_BIG_ENDIAN */
	}

	/* Add-in any carries. */

	SKCS_OC_ADD(Checksum, Checksum, 0);

	/* Add-in any new carry. */

	SKCS_OC_ADD(Checksum, Checksum, 0);

	/* Note: All bits beyond the 16-bit limit are now zero. */

	return ((unsigned) Checksum);
}	/* SkCsCalculateChecksum */

#endif /* SK_CS_CALCULATE_CHECKSUM */

/******************************************************************************
 *
 *	SkCsEvent - the CSUM event dispatcher
 *
 * Description:
 *	This is the event handler for the CSUM module.
 *
 * Arguments:
 *	pAc - Pointer to adapter context.
 *
 *	Ioc - I/O context.
 *
 *	Event -	 Event id.
 *
 *	Param - Event dependent parameter.
 *
 * Returns:
 *	The 16-bit Internet Checksum for the specified data.
 *
 *	Note: The checksum is calculated in the machine's natural byte order,
 *	i.e. little vs. big endian. Thus, the resulting checksum is different
 *	for the same input data on little and big endian machines.
 *
 *	However, when written back to the network packet, the byte order is
 *	always in correct network order.
 */
int SkCsEvent(
SK_AC		*pAc,	/* Pointer to adapter context. */
SK_IOC		Ioc,	/* I/O context. */
SK_U32		Event,	/* Event id. */
SK_EVPARA	Param)	/* Event dependent parameter. */
{
	int ProtoIndex;
	int	NetNumber;

	switch (Event) {
	/*
	 * Clear protocol statistics.
	 *
	 * Param - Protocol index, or -1 for all protocols.
	 *		 - Net number.
	 */
	case SK_CSUM_EVENT_CLEAR_PROTO_STATS:

		ProtoIndex = (int)Param.Para32[1];
		NetNumber = (int)Param.Para32[0];
		if (ProtoIndex < 0) {	/* Clear for all protocols. */
			if (NetNumber >= 0) {
				SK_MEMSET(&pAc->Csum.ProtoStats[NetNumber][0], 0,
					sizeof(pAc->Csum.ProtoStats[NetNumber]));
			}
		}
		else {					/* Clear for individual protocol. */
			SK_MEMSET(&pAc->Csum.ProtoStats[NetNumber][ProtoIndex], 0,
				sizeof(pAc->Csum.ProtoStats[NetNumber][ProtoIndex]));
		}
		break;
	default:
		break;
	}
	return (0);	/* Success. */
}	/* SkCsEvent */

#endif	/* SK_USE_CSUM */
