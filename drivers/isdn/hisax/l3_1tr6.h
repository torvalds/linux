/* $Id: l3_1tr6.h,v 2.2.6.2 2001/09/23 22:24:49 kai Exp $
 *
 * German 1TR6 D-channel protocol defines
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef l3_1tr6
#define l3_1tr6

#define PROTO_DIS_N0 0x40
#define PROTO_DIS_N1 0x41

/*
 * MsgType N0
 */
#define MT_N0_REG_IND 0x61
#define MT_N0_CANC_IND 0x62
#define MT_N0_FAC_STA 0x63
#define MT_N0_STA_ACK 0x64
#define MT_N0_STA_REJ 0x65
#define MT_N0_FAC_INF 0x66
#define MT_N0_INF_ACK 0x67
#define MT_N0_INF_REJ 0x68
#define MT_N0_CLOSE   0x75
#define MT_N0_CLO_ACK 0x77

/*
 * MsgType N1
 */

#define MT_N1_ESC 0x00
#define MT_N1_ALERT 0x01
#define MT_N1_CALL_SENT 0x02
#define MT_N1_CONN 0x07
#define MT_N1_CONN_ACK 0x0F
#define MT_N1_SETUP 0x05
#define MT_N1_SETUP_ACK 0x0D
#define MT_N1_RES 0x26
#define MT_N1_RES_ACK 0x2E
#define MT_N1_RES_REJ 0x22
#define MT_N1_SUSP 0x25
#define MT_N1_SUSP_ACK 0x2D
#define MT_N1_SUSP_REJ 0x21
#define MT_N1_USER_INFO 0x20
#define MT_N1_DET 0x40
#define MT_N1_DISC 0x45
#define MT_N1_REL 0x4D
#define MT_N1_REL_ACK 0x5A
#define MT_N1_CANC_ACK 0x6E
#define MT_N1_CANC_REJ 0x67
#define MT_N1_CON_CON 0x69
#define MT_N1_FAC 0x60
#define MT_N1_FAC_ACK 0x68
#define MT_N1_FAC_CAN 0x66
#define MT_N1_FAC_REG 0x64
#define MT_N1_FAC_REJ 0x65
#define MT_N1_INFO 0x6D
#define MT_N1_REG_ACK 0x6C
#define MT_N1_REG_REJ 0x6F
#define MT_N1_STAT 0x63
#define MT_N1_INVALID 0

/*
 * W Elemente
 */

#define WE_Shift_F0 0x90
#define WE_Shift_F6 0x96
#define WE_Shift_OF0 0x98
#define WE_Shift_OF6 0x9E

#define WE0_cause 0x08
#define WE0_connAddr 0x0C
#define WE0_callID 0x10
#define WE0_chanID 0x18
#define WE0_netSpecFac 0x20
#define WE0_display 0x28
#define WE0_keypad 0x2C
#define WE0_origAddr 0x6C
#define WE0_destAddr 0x70
#define WE0_userInfo 0x7E

#define WE0_moreData 0xA0
#define WE0_congestLevel 0xB0

#define WE6_serviceInd 0x01
#define WE6_chargingInfo 0x02
#define WE6_date 0x03
#define WE6_facSelect 0x05
#define WE6_facStatus 0x06
#define WE6_statusCalled 0x07
#define WE6_addTransAttr 0x08

/*
 * FacCodes
 */
#define FAC_Sperre 0x01
#define FAC_Sperre_All 0x02
#define FAC_Sperre_Fern 0x03
#define FAC_Sperre_Intl 0x04
#define FAC_Sperre_Interk 0x05

#define FAC_Forward1 0x02
#define FAC_Forward2 0x03
#define FAC_Konferenz 0x06
#define FAC_GrabBchan 0x0F
#define FAC_Reactivate 0x10
#define FAC_Konferenz3 0x11
#define FAC_Dienstwechsel1 0x12
#define FAC_Dienstwechsel2 0x13
#define FAC_NummernIdent 0x14
#define FAC_GBG 0x15
#define FAC_DisplayUebergeben 0x17
#define FAC_DisplayUmgeleitet 0x1A
#define FAC_Unterdruecke 0x1B
#define FAC_Deactivate 0x1E
#define FAC_Activate 0x1D
#define FAC_SPV 0x1F
#define FAC_Rueckwechsel 0x23
#define FAC_Umleitung 0x24

/*
 * Cause codes
 */
#define CAUSE_InvCRef 0x01
#define CAUSE_BearerNotImpl 0x03
#define CAUSE_CIDunknown 0x07
#define CAUSE_CIDinUse 0x08
#define CAUSE_NoChans 0x0A
#define CAUSE_FacNotImpl 0x10
#define CAUSE_FacNotSubscr 0x11
#define CAUSE_OutgoingBarred 0x20
#define CAUSE_UserAccessBusy 0x21
#define CAUSE_NegativeGBG 0x22
#define CAUSE_UnknownGBG 0x23
#define CAUSE_NoSPVknown 0x25
#define CAUSE_DestNotObtain 0x35
#define CAUSE_NumberChanged 0x38
#define CAUSE_OutOfOrder 0x39
#define CAUSE_NoUserResponse 0x3A
#define CAUSE_UserBusy 0x3B
#define CAUSE_IncomingBarred 0x3D
#define CAUSE_CallRejected 0x3E
#define CAUSE_NetworkCongestion 0x59
#define CAUSE_RemoteUser 0x5A
#define CAUSE_LocalProcErr 0x70
#define CAUSE_RemoteProcErr 0x71
#define CAUSE_RemoteUserSuspend 0x72
#define CAUSE_RemoteUserResumed 0x73
#define CAUSE_UserInfoDiscarded 0x7F

#define T303	4000
#define T304	20000
#define T305	4000
#define T308	4000
#define T310	120000
#define T313	4000
#define T318	4000
#define T319	4000

#endif
