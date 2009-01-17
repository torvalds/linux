#ifndef __WINBOND_MLME_H
#define __WINBOND_MLME_H

#include <linux/types.h>
#include <linux/spinlock.h>

#include "mac_structures.h"
#include "mds_s.h"

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//	Mlme.h
//		Define the related definitions of MLME module
//	history -- 01/14/03' created
//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#define AUTH_REJECT_REASON_CHALLENGE_FAIL		1

//====== the state of MLME module
#define INACTIVE			0x0
#define IDLE_SCAN			0x1

//====== the state of MLME/ESS module
#define STATE_1				0x2
#define AUTH_REQ			0x3
#define AUTH_WEP			0x4
#define STATE_2				0x5
#define ASSOC_REQ			0x6
#define STATE_3				0x7

//====== the state of MLME/IBSS module
#define IBSS_JOIN_SYNC		0x8
#define IBSS_AUTH_REQ		0x9
#define IBSS_AUTH_CHANLGE	0xa
#define IBSS_AUTH_WEP		0xb
#define IBSS_AUTH_IND		0xc
#define IBSS_STATE_2		0xd



//=========================================
//depend on D5C(MAC timing control 03 register): MaxTxMSDULifeTime default 0x80000us
#define AUTH_FAIL_TIMEOUT		550
#define ASSOC_FAIL_TIMEOUT		550
#define REASSOC_FAIL_TIMEOUT	550



//
// MLME task global CONSTANTS, STRUCTURE, variables
//


/////////////////////////////////////////////////////////////
//  enum_ResultCode --
//  Result code returned from MLME to SME.
//
/////////////////////////////////////////////////////////////
// PD43 20030829 Modifiled
//#define	SUCCESS								0
#define MLME_SUCCESS                        0 //follow spec.
#define	INVALID_PARAMETERS					1 //Not following spec.
#define	NOT_SUPPPORTED						2
#define	TIMEOUT								3
#define	TOO_MANY_SIMULTANEOUS_REQUESTS		4
#define REFUSED								5
#define	BSS_ALREADY_STARTED_OR_JOINED		6
#define	TRANSMIT_FRAME_FAIL					7
#define	NO_BSS_FOUND						8
#define RETRY								9
#define GIVE_UP								10


#define OPEN_AUTH							0
#define SHARE_AUTH							1
#define ANY_AUTH							2
#define WPA_AUTH							3	//for WPA
#define WPAPSK_AUTH							4
#define WPANONE_AUTH						5
///////////////////////////////////////////// added by ws 04/19/04
#ifdef _WPA2_
#define WPA2_AUTH                           6//for WPA2
#define WPA2PSK_AUTH                        7
#endif //end def _WPA2_

//////////////////////////////////////////////////////////////////
//define the msg type of MLME module
//////////////////////////////////////////////////////////////////
//--------------------------------------------------------
//from SME

#define MLMEMSG_AUTH_REQ				0x0b
#define MLMEMSG_DEAUTH_REQ				0x0c
#define MLMEMSG_ASSOC_REQ				0x0d
#define MLMEMSG_REASSOC_REQ				0x0e
#define MLMEMSG_DISASSOC_REQ			0x0f
#define MLMEMSG_START_IBSS_REQ			0x10
#define MLMEMSG_IBSS_NET_CFM			0x11

//from RX :
#define MLMEMSG_RCV_MLMEFRAME			0x20
#define MLMEMSG_RCV_ASSOCRSP			0x22
#define MLMEMSG_RCV_REASSOCRSP			0x24
#define MLMEMSG_RCV_DISASSOC			0x2b
#define MLMEMSG_RCV_AUTH				0x2c
#define MLMEMSG_RCV_DEAUTH				0x2d


//from TX callback
#define MLMEMSG_TX_CALLBACK				0x40
#define MLMEMSG_ASSOCREQ_CALLBACK		0x41
#define MLMEMSG_REASSOCREQ_CALLBACK		0x43
#define MLMEMSG_DISASSOC_CALLBACK		0x4a
#define MLMEMSG_AUTH_CALLBACK			0x4c
#define MLMEMSG_DEAUTH_CALLBACK			0x4d

//#define MLMEMSG_JOIN_FAIL				4
//#define MLMEMSG_AUTHEN_FAIL			18
#define MLMEMSG_TIMEOUT					0x50

///////////////////////////////////////////////////////////////////////////
//Global data structures
#define MAX_NUM_TX_MMPDU	2
#define MAX_MMPDU_SIZE		1512
#define MAX_NUM_RX_MMPDU	6


///////////////////////////////////////////////////////////////////////////
//MACRO
#define boMLME_InactiveState(_AA_)	(_AA_->wState==INACTIVE)
#define boMLME_IdleScanState(_BB_)	(_BB_->wState==IDLE_SCAN)
#define boMLME_FoundSTAinfo(_CC_)	(_CC_->wState>=IDLE_SCAN)

typedef struct _MLME_FRAME
{
	//NDIS_PACKET		MLME_Packet;
	s8 *			pMMPDU;
	u16			len;
	u8			DataType;
	u8			IsInUsed;

	spinlock_t	MLMESpinLock;

    u8		TxMMPDU[MAX_NUM_TX_MMPDU][MAX_MMPDU_SIZE];
	u8		TxMMPDUInUse[ (MAX_NUM_TX_MMPDU+3) & ~0x03 ];

	u16		wNumTxMMPDU;
	u16		wNumTxMMPDUDiscarded;

    u8		RxMMPDU[MAX_NUM_RX_MMPDU][MAX_MMPDU_SIZE];
    u8	 	SaveRxBufSlotInUse[ (MAX_NUM_RX_MMPDU+3) & ~0x03 ];

	u16		wNumRxMMPDU;
	u16		wNumRxMMPDUDiscarded;

	u16		wNumRxMMPDUInMLME; 	// Number of the Rx MMPDU
	u16		reserved_1;			//  in MLME.
                    	            //  excluding the discarded
} MLME_FRAME, *psMLME_FRAME;

typedef struct _AUTHREQ {

	u8 	peerMACaddr[MAC_ADDR_LENGTH];
	u16	wAuthAlgorithm;

} MLME_AUTHREQ_PARA, *psMLME_AUTHREQ_PARA;

struct _Reason_Code {

	u8	peerMACaddr[MAC_ADDR_LENGTH];
	u16	wReasonCode;
};
typedef struct _Reason_Code MLME_DEAUTHREQ_PARA, *psMLME_DEAUTHREQ_PARA;
typedef struct _Reason_Code MLME_DISASSOCREQ_PARA, *psMLME_DISASSOCREQ_PARA;

typedef struct _ASSOCREQ {
  u8       PeerSTAAddr[MAC_ADDR_LENGTH];
  u16       CapabilityInfo;
  u16       ListenInterval;

}__attribute__ ((packed)) MLME_ASSOCREQ_PARA, *psMLME_ASSOCREQ_PARA;

typedef struct _REASSOCREQ {
  u8       NewAPAddr[MAC_ADDR_LENGTH];
  u16       CapabilityInfo;
  u16       ListenInterval;

}__attribute__ ((packed)) MLME_REASSOCREQ_PARA, *psMLME_REASSOCREQ_PARA;

typedef struct _MLMECALLBACK {

  u8 	*psFramePtr;
  u8		bResult;

} MLME_TXCALLBACK, *psMLME_TXCALLBACK;

typedef struct _RXDATA
{
	s32		FrameLength;
	u8	__attribute__ ((packed)) *pbFramePtr;

}__attribute__ ((packed)) RXDATA, *psRXDATA;

#endif
