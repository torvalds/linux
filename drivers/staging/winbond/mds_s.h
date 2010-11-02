#ifndef __WINBOND_MDS_H
#define __WINBOND_MDS_H

#include <linux/timer.h>
#include <linux/types.h>
#include <asm/atomic.h>

#include "localpara.h"
#include "mac_structures.h"

/* Preamble_Type, see <SFS-802.11G-MIB-203> */
enum {
	WLAN_PREAMBLE_TYPE_SHORT,
	WLAN_PREAMBLE_TYPE_LONG,
};

/*****************************************************************************/
#define MAX_USB_TX_DESCRIPTOR		15	/* IS89C35 ability */
#define MAX_USB_TX_BUFFER_NUMBER	4	/* Virtual pre-buffer number of MAX_USB_TX_BUFFER */
#define MAX_USB_TX_BUFFER		4096	/* IS89C35 ability 4n alignment is required for hardware */

#define AUTH_REQUEST_PAIRWISE_ERROR	0	/* _F flag setting */
#define AUTH_REQUEST_GROUP_ERROR	1	/* _F flag setting */

#define CURRENT_FRAGMENT_THRESHOLD	(adapter->Mds.TxFragmentThreshold & ~0x1)
#define CURRENT_PREAMBLE_MODE		(psLOCAL->boShortPreamble ? WLAN_PREAMBLE_TYPE_SHORT : WLAN_PREAMBLE_TYPE_LONG)
#define CURRENT_TX_RATE_FOR_MNG		(adapter->sLocalPara.CurrentTxRateForMng)
#define CURRENT_PROTECT_MECHANISM	(psLOCAL->boProtectMechanism)
#define CURRENT_RTS_THRESHOLD		(adapter->Mds.TxRTSThreshold)

#define MIB_GS_XMIT_OK_INC		(adapter->sLocalPara.GS_XMIT_OK++)
#define MIB_GS_RCV_OK_INC		(adapter->sLocalPara.GS_RCV_OK++)
#define MIB_GS_XMIT_ERROR_INC		(adapter->sLocalPara.GS_XMIT_ERROR)

/* ---------- TX ----------------------------------- */
#define ETHERNET_TX_DESCRIPTORS         MAX_USB_TX_BUFFER_NUMBER

/* ---------- RX ----------------------------------- */
#define ETHERNET_RX_DESCRIPTORS		8	/* It's not necessary to allocate more than 2 in sync indicate */

/*
 * ================================================================
 * Configration default value
 * ================================================================
 */
#define DEFAULT_MULTICASTLISTMAX	32	/* standard */
#define DEFAULT_TX_BURSTLENGTH		3	/* 32 Longwords */
#define DEFAULT_RX_BURSTLENGTH		3	/* 32 Longwords */
#define DEFAULT_TX_THRESHOLD		0	/* Full Packet */
#define DEFAULT_RX_THRESHOLD		0	/* Full Packet */
#define DEFAULT_MAXTXRATE		6	/* 11 Mbps (Long) */
#define DEFAULT_CHANNEL			3	/* Chennel 3 */
#define DEFAULT_RTSThreshold		2347	/* Disable RTS */
#define DEFAULT_PME			0	/* Disable */
#define DEFAULT_SIFSTIME		10
#define DEFAULT_ACKTIME_1ML             304	/* 148 + 44 + 112 */
#define DEFAULT_ACKTIME_2ML             248	/* 148 + 44 + 56 */
#define DEFAULT_FRAGMENT_THRESHOLD      2346	/* No fragment */
#define DEFAULT_PREAMBLE_LENGTH		72
#define DEFAULT_PLCPHEADERTIME_LENGTH	24

/*
 * ------------------------------------------------------------------------
 * 0.96 sec since time unit of the R03 for the current, W89C32 is about 60ns
 * instead of 960 ns. This shall be fixed in the future W89C32
 * -------------------------------------------------------------------------
 */
#define DEFAULT_MAX_RECEIVE_TIME	16440000

#define RX_BUF_SIZE			2352	/* 600 - For 301 must be multiple of 8 */
#define MAX_RX_DESCRIPTORS		18	/* Rx Layer 2 */

/* For brand-new rx system */
#define MDS_ID_IGNORE			ETHERNET_RX_DESCRIPTORS

/* For Tx Packet status classify */
#define PACKET_FREE_TO_USE		0
#define PACKET_COME_FROM_NDIS		0x08
#define PACKET_COME_FROM_MLME		0x80
#define PACKET_SEND_COMPLETE		0xff

struct wb35_mds {
	/* For Tx usage */
	u8	TxOwner[((MAX_USB_TX_BUFFER_NUMBER + 3) & ~0x03)];
	u8	*pTxBuffer;
	u16	TxBufferSize[((MAX_USB_TX_BUFFER_NUMBER + 1) & ~0x01)];
	u8	TxDesFrom[((MAX_USB_TX_DESCRIPTOR + 3) & ~0x03)];/* 1: MLME 2: NDIS control 3: NDIS data */
	u8	TxCountInBuffer[((MAX_USB_TX_DESCRIPTOR + 3) & ~0x03)];

	u8	TxFillIndex;	/* the next index of TxBuffer can be used */
	u8	TxDesIndex;	/* The next index of TxDes can be used */
	u8	ScanTxPause;	/* data Tx pause because the scanning is progressing, but probe request Tx won't. */
	u8	TxPause;	/*For pause the Mds_Tx modult */

	atomic_t	TxThreadCount;	/* For thread counting */

	u16	TxResult[((MAX_USB_TX_DESCRIPTOR + 1) & ~0x01)];/* Collect the sending result of Mpdu */

	u8	MicRedundant[8]; /* For tmp use */
	u8	*MicWriteAddress[2]; /* The start address to fill the Mic, use 2 point due to Mic maybe fragment */

	u16	MicWriteSize[2];

	u16	MicAdd; /* If want to add the Mic, this variable equal to 8 */
	u16	MicWriteIndex; /* The number of MicWriteAddress */

	u8	TxRate[((MAX_USB_TX_DESCRIPTOR + 1) & ~0x01)][2]; /* [0] current tx rate, [1] fall back rate */
	u8	TxInfo[((MAX_USB_TX_DESCRIPTOR + 1) & ~0x01)]; /*Store information for callback function */

	/* for scanning mechanism */
	u8	TxToggle;	/* It is TRUE if there are tx activities in some time interval */
	u8	Reserved_[3];

	/* ---- for Tx Parameter */
	u16	TxFragmentThreshold;	/* For frame body only */
	u16	TxRTSThreshold;

	u32	MaxReceiveTime;

	/* depend on OS, */
	u32	MulticastListNo;
	u32	PacketFilter; /* Setting by NDIS, the current packet filter in use. */
	u8	MulticastAddressesArray[DEFAULT_MULTICASTLISTMAX][MAC_ADDR_LENGTH];

	/* COUNTERMEASURE */
	u8	bMICfailCount;
	u8	boCounterMeasureBlock;
	u8	reserved_4[2];

	u32	TxTsc;
	u32	TxTsc_2;
};

#endif
