/* hfa384x.h
*
* Defines the constants and data structures for the hfa384x
*
* Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
* --------------------------------------------------------------------
*
* linux-wlan
*
*   The contents of this file are subject to the Mozilla Public
*   License Version 1.1 (the "License"); you may not use this file
*   except in compliance with the License. You may obtain a copy of
*   the License at http://www.mozilla.org/MPL/
*
*   Software distributed under the License is distributed on an "AS
*   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
*   implied. See the License for the specific language governing
*   rights and limitations under the License.
*
*   Alternatively, the contents of this file may be used under the
*   terms of the GNU Public License version 2 (the "GPL"), in which
*   case the provisions of the GPL are applicable instead of the
*   above.  If you wish to allow the use of your version of this file
*   only under the terms of the GPL and not to allow others to use
*   your version of this file under the MPL, indicate your decision
*   by deleting the provisions above and replace them with the notice
*   and other provisions required by the GPL.  If you do not delete
*   the provisions above, a recipient may use your version of this
*   file under either the MPL or the GPL.
*
* --------------------------------------------------------------------
*
* Inquiries regarding the linux-wlan Open Source project can be
* made directly to:
*
* AbsoluteValue Systems Inc.
* info@linux-wlan.com
* http://www.linux-wlan.com
*
* --------------------------------------------------------------------
*
* Portions of the development of this software were funded by
* Intersil Corporation as part of PRISM(R) chipset product development.
*
* --------------------------------------------------------------------
*
*   [Implementation and usage notes]
*
*   [References]
*	CW10 Programmer's Manual v1.5
*	IEEE 802.11 D10.0
*
* --------------------------------------------------------------------
*/

#ifndef _HFA384x_H
#define _HFA384x_H

/*=============================================================*/
#define HFA384x_FIRMWARE_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))

#define HFA384x_LEVEL_TO_dBm(v)   (0x100 + (v) * 100 / 255 - 100)

/*------ Constants --------------------------------------------*/
/*--- Mins & Maxs -----------------------------------*/
#define		HFA384x_CMD_ALLOC_LEN_MIN	((u16)4)
#define		HFA384x_CMD_ALLOC_LEN_MAX	((u16)2400)
#define		HFA384x_BAP_DATALEN_MAX		((u16)4096)
#define		HFA384x_BAP_OFFSET_MAX		((u16)4096)
#define		HFA384x_PORTID_MAX		((u16)7)
#define		HFA384x_NUMPORTS_MAX		((u16)(HFA384x_PORTID_MAX+1))
#define		HFA384x_PDR_LEN_MAX		((u16)512)	/* in bytes, from EK */
#define		HFA384x_PDA_RECS_MAX		((u16)200)	/* a guess */
#define		HFA384x_PDA_LEN_MAX		((u16)1024)	/* in bytes, from EK */
#define		HFA384x_SCANRESULT_MAX		((u16)31)
#define		HFA384x_HSCANRESULT_MAX		((u16)31)
#define		HFA384x_CHINFORESULT_MAX	((u16)16)
#define		HFA384x_DRVR_FIDSTACKLEN_MAX	(10)
#define		HFA384x_DRVR_TXBUF_MAX		(sizeof(hfa384x_tx_frame_t) + \
						WLAN_DATA_MAXLEN - \
						WLAN_WEP_IV_LEN - \
						WLAN_WEP_ICV_LEN + 2)
#define		HFA384x_DRVR_MAGIC		(0x4a2d)
#define		HFA384x_INFODATA_MAXLEN		(sizeof(hfa384x_infodata_t))
#define		HFA384x_INFOFRM_MAXLEN		(sizeof(hfa384x_InfFrame_t))
#define		HFA384x_RID_GUESSING_MAXLEN	2048  /* I'm not really sure */
#define		HFA384x_RIDDATA_MAXLEN		HFA384x_RID_GUESSING_MAXLEN
#define		HFA384x_USB_RWMEM_MAXLEN	2048

/*--- Support Constants -----------------------------*/
#define		HFA384x_BAP_PROC			((u16)0)
#define		HFA384x_BAP_int				((u16)1)
#define		HFA384x_PORTTYPE_IBSS			((u16)0)
#define		HFA384x_PORTTYPE_BSS			((u16)1)
#define		HFA384x_PORTTYPE_WDS			((u16)2)
#define		HFA384x_PORTTYPE_PSUEDOIBSS		((u16)3)
#define		HFA384x_PORTTYPE_HOSTAP    		((u16)6)
#define		HFA384x_WEPFLAGS_PRIVINVOKED		((u16)BIT0)
#define		HFA384x_WEPFLAGS_EXCLUDE		((u16)BIT1)
#define		HFA384x_WEPFLAGS_DISABLE_TXCRYPT	((u16)BIT4)
#define		HFA384x_WEPFLAGS_DISABLE_RXCRYPT	((u16)BIT7)
#define		HFA384x_WEPFLAGS_DISALLOW_MIXED 	((u16)BIT11)
#define		HFA384x_WEPFLAGS_IV_intERVAL1		((u16)0)
#define		HFA384x_WEPFLAGS_IV_intERVAL10		((u16)BIT5)
#define		HFA384x_WEPFLAGS_IV_intERVAL50		((u16)BIT6)
#define		HFA384x_WEPFLAGS_IV_intERVAL100		((u16)(BIT5 | BIT6))
#define		HFA384x_WEPFLAGS_FIRMWARE_WPA  		((u16)BIT8)
#define		HFA384x_WEPFLAGS_HOST_MIC      		((u16)BIT9)
#define 	HFA384x_ROAMMODE_FWSCAN_FWROAM		((u16)1)
#define 	HFA384x_ROAMMODE_FWSCAN_HOSTROAM	((u16)2)
#define 	HFA384x_ROAMMODE_HOSTSCAN_HOSTROAM	((u16)3)
#define 	HFA384x_PORTSTATUS_DISABLED		((u16)1)
#define 	HFA384x_PORTSTATUS_INITSRCH		((u16)2)
#define 	HFA384x_PORTSTATUS_CONN_IBSS		((u16)3)
#define 	HFA384x_PORTSTATUS_CONN_ESS		((u16)4)
#define 	HFA384x_PORTSTATUS_OOR_ESS		((u16)5)
#define 	HFA384x_PORTSTATUS_CONN_WDS		((u16)6)
#define 	HFA384x_PORTSTATUS_HOSTAP		((u16)8)
#define		HFA384x_RATEBIT_1			((u16)1)
#define		HFA384x_RATEBIT_2			((u16)2)
#define		HFA384x_RATEBIT_5dot5			((u16)4)
#define		HFA384x_RATEBIT_11			((u16)8)

/*--- Just some symbolic names for legibility -------*/
#define		HFA384x_TXCMD_NORECL		((u16)0)
#define		HFA384x_TXCMD_RECL		((u16)1)

/*--- MAC Internal memory constants and macros ------*/
/* masks and macros used to manipulate MAC internal memory addresses. */
/* MAC internal memory addresses are 23 bit quantities.  The MAC uses
 * a paged address space where the upper 16 bits are the page number
 * and the lower 7 bits are the offset.  There are various Host API
 * elements that require two 16-bit quantities to specify a MAC
 * internal memory address.  Unfortunately, some of the API's use a
 * page/offset format where the offset value is JUST the lower seven
 * bits and the page is  the remaining 16 bits.  Some of the API's
 * assume that the 23 bit address has been split at the 16th bit.  We
 * refer to these two formats as AUX format and CMD format.  The
 * macros below help handle some of this.
 */

/* Handy constant */
#define		HFA384x_ADDR_AUX_OFF_MAX	((u16)0x007f)

/* Mask bits for discarding unwanted pieces in a flat address */
#define		HFA384x_ADDR_FLAT_AUX_PAGE_MASK	(0x007fff80)
#define		HFA384x_ADDR_FLAT_AUX_OFF_MASK	(0x0000007f)
#define		HFA384x_ADDR_FLAT_CMD_PAGE_MASK	(0xffff0000)
#define		HFA384x_ADDR_FLAT_CMD_OFF_MASK	(0x0000ffff)

/* Mask bits for discarding unwanted pieces in AUX format 16-bit address parts */
#define		HFA384x_ADDR_AUX_PAGE_MASK	(0xffff)
#define		HFA384x_ADDR_AUX_OFF_MASK	(0x007f)

/* Mask bits for discarding unwanted pieces in CMD format 16-bit address parts */
#define		HFA384x_ADDR_CMD_PAGE_MASK	(0x007f)
#define		HFA384x_ADDR_CMD_OFF_MASK	(0xffff)

/* Make a 32-bit flat address from AUX format 16-bit page and offset */
#define		HFA384x_ADDR_AUX_MKFLAT(p,o)	\
		(((u32)(((u16)(p))&HFA384x_ADDR_AUX_PAGE_MASK)) <<7) | \
		((u32)(((u16)(o))&HFA384x_ADDR_AUX_OFF_MASK))

/* Make a 32-bit flat address from CMD format 16-bit page and offset */
#define		HFA384x_ADDR_CMD_MKFLAT(p,o)	\
		(((u32)(((u16)(p))&HFA384x_ADDR_CMD_PAGE_MASK)) <<16) | \
		((u32)(((u16)(o))&HFA384x_ADDR_CMD_OFF_MASK))

/* Make AUX format offset and page from a 32-bit flat address */
#define		HFA384x_ADDR_AUX_MKPAGE(f) \
		((u16)((((u32)(f))&HFA384x_ADDR_FLAT_AUX_PAGE_MASK)>>7))
#define		HFA384x_ADDR_AUX_MKOFF(f) \
		((u16)(((u32)(f))&HFA384x_ADDR_FLAT_AUX_OFF_MASK))

/* Make CMD format offset and page from a 32-bit flat address */
#define		HFA384x_ADDR_CMD_MKPAGE(f) \
		((u16)((((u32)(f))&HFA384x_ADDR_FLAT_CMD_PAGE_MASK)>>16))
#define		HFA384x_ADDR_CMD_MKOFF(f) \
		((u16)(((u32)(f))&HFA384x_ADDR_FLAT_CMD_OFF_MASK))

/*--- Aux register masks/tests ----------------------*/
/* Some of the upper bits of the AUX offset register are used to */
/*  select address space. */
#define		HFA384x_AUX_CTL_EXTDS	(0x00)
#define		HFA384x_AUX_CTL_NV	(0x01)
#define		HFA384x_AUX_CTL_PHY	(0x02)
#define		HFA384x_AUX_CTL_ICSRAM	(0x03)

/* Make AUX register offset and page values from a flat address */
#define		HFA384x_AUX_MKOFF(f, c) \
	(HFA384x_ADDR_AUX_MKOFF(f) | (((u16)(c))<<12))
#define		HFA384x_AUX_MKPAGE(f)	HFA384x_ADDR_AUX_MKPAGE(f)


/*--- Controller Memory addresses -------------------*/
#define		HFA3842_PDA_BASE	(0x007f0000UL)
#define		HFA3841_PDA_BASE	(0x003f0000UL)
#define		HFA3841_PDA_BOGUS_BASE	(0x00390000UL)

/*--- Driver Download states  -----------------------*/
#define		HFA384x_DLSTATE_DISABLED		0
#define		HFA384x_DLSTATE_RAMENABLED		1
#define		HFA384x_DLSTATE_FLASHENABLED		2
#define		HFA384x_DLSTATE_FLASHWRITTEN		3
#define		HFA384x_DLSTATE_FLASHWRITEPENDING	4
#define		HFA384x_DLSTATE_GENESIS 		5

#define		HFA384x_CMD_OFF			(0x00)
#define		HFA384x_PARAM0_OFF		(0x04)
#define		HFA384x_PARAM1_OFF		(0x08)
#define		HFA384x_PARAM2_OFF		(0x0c)
#define		HFA384x_STATUS_OFF		(0x10)
#define		HFA384x_RESP0_OFF		(0x14)
#define		HFA384x_RESP1_OFF		(0x18)
#define		HFA384x_RESP2_OFF		(0x1c)
#define		HFA384x_INFOFID_OFF		(0x20)
#define		HFA384x_RXFID_OFF		(0x40)
#define		HFA384x_ALLOCFID_OFF		(0x44)
#define		HFA384x_TXCOMPLFID_OFF		(0x48)
#define		HFA384x_SELECT0_OFF		(0x30)
#define		HFA384x_OFFSET0_OFF		(0x38)
#define		HFA384x_DATA0_OFF		(0x6c)
#define		HFA384x_SELECT1_OFF		(0x34)
#define		HFA384x_OFFSET1_OFF		(0x3c)
#define		HFA384x_DATA1_OFF		(0x70)
#define		HFA384x_EVSTAT_OFF		(0x60)
#define		HFA384x_intEN_OFF		(0x64)
#define		HFA384x_EVACK_OFF		(0x68)
#define		HFA384x_CONTROL_OFF		(0x28)
#define		HFA384x_SWSUPPORT0_OFF		(0x50)
#define		HFA384x_SWSUPPORT1_OFF		(0x54)
#define		HFA384x_SWSUPPORT2_OFF		(0x58)
#define		HFA384x_AUXPAGE_OFF		(0x74)
#define		HFA384x_AUXOFFSET_OFF		(0x78)
#define		HFA384x_AUXDATA_OFF		(0x7c)
#define		HFA384x_PCICOR_OFF		(0x4c)
#define		HFA384x_PCIHCR_OFF		(0x5c)
#define		HFA384x_PCI_M0_ADDRH_OFF	(0x80)
#define		HFA384x_PCI_M0_ADDRL_OFF	(0x84)
#define		HFA384x_PCI_M0_LEN_OFF		(0x88)
#define		HFA384x_PCI_M0_CTL_OFF		(0x8c)
#define		HFA384x_PCI_STATUS_OFF		(0x98)
#define		HFA384x_PCI_M1_ADDRH_OFF	(0xa0)
#define		HFA384x_PCI_M1_ADDRL_OFF	(0xa4)
#define		HFA384x_PCI_M1_LEN_OFF		(0xa8)
#define		HFA384x_PCI_M1_CTL_OFF		(0xac)

/*--- Register Field Masks --------------------------*/
#define		HFA384x_CMD_BUSY		((u16)BIT15)
#define		HFA384x_CMD_AINFO		((u16)(BIT14 | BIT13 | BIT12 | BIT11 | BIT10 | BIT9 | BIT8))
#define		HFA384x_CMD_MACPORT		((u16)(BIT10 | BIT9 | BIT8))
#define		HFA384x_CMD_RECL		((u16)BIT8)
#define		HFA384x_CMD_WRITE		((u16)BIT8)
#define		HFA384x_CMD_PROGMODE		((u16)(BIT9 | BIT8))
#define		HFA384x_CMD_CMDCODE		((u16)(BIT5 | BIT4 | BIT3 | BIT2 | BIT1 | BIT0))

#define		HFA384x_STATUS_RESULT		((u16)(BIT14 | BIT13 | BIT12 | BIT11 | BIT10 | BIT9 | BIT8))
#define		HFA384x_STATUS_CMDCODE		((u16)(BIT5 | BIT4 | BIT3 | BIT2 | BIT1 | BIT0))

#define		HFA384x_OFFSET_BUSY		((u16)BIT15)
#define		HFA384x_OFFSET_ERR		((u16)BIT14)
#define		HFA384x_OFFSET_DATAOFF		((u16)(BIT11 | BIT10 | BIT9 | BIT8 | BIT7 | BIT6 | BIT5 | BIT4 | BIT3 | BIT2 | BIT1))

#define		HFA384x_EVSTAT_TICK		((u16)BIT15)
#define		HFA384x_EVSTAT_WTERR		((u16)BIT14)
#define		HFA384x_EVSTAT_INFDROP		((u16)BIT13)
#define		HFA384x_EVSTAT_INFO		((u16)BIT7)
#define		HFA384x_EVSTAT_DTIM		((u16)BIT5)
#define		HFA384x_EVSTAT_CMD		((u16)BIT4)
#define		HFA384x_EVSTAT_ALLOC		((u16)BIT3)
#define		HFA384x_EVSTAT_TXEXC		((u16)BIT2)
#define		HFA384x_EVSTAT_TX		((u16)BIT1)
#define		HFA384x_EVSTAT_RX		((u16)BIT0)

#define         HFA384x_int_BAP_OP           (HFA384x_EVSTAT_INFO|HFA384x_EVSTAT_RX|HFA384x_EVSTAT_TX|HFA384x_EVSTAT_TXEXC)

#define         HFA384x_int_NORMAL           (HFA384x_EVSTAT_INFO|HFA384x_EVSTAT_RX|HFA384x_EVSTAT_TX|HFA384x_EVSTAT_TXEXC|HFA384x_EVSTAT_INFDROP|HFA384x_EVSTAT_ALLOC|HFA384x_EVSTAT_DTIM)

#define		HFA384x_intEN_TICK		((u16)BIT15)
#define		HFA384x_intEN_WTERR		((u16)BIT14)
#define		HFA384x_intEN_INFDROP		((u16)BIT13)
#define		HFA384x_intEN_INFO		((u16)BIT7)
#define		HFA384x_intEN_DTIM		((u16)BIT5)
#define		HFA384x_intEN_CMD		((u16)BIT4)
#define		HFA384x_intEN_ALLOC		((u16)BIT3)
#define		HFA384x_intEN_TXEXC		((u16)BIT2)
#define		HFA384x_intEN_TX		((u16)BIT1)
#define		HFA384x_intEN_RX		((u16)BIT0)

#define		HFA384x_EVACK_TICK		((u16)BIT15)
#define		HFA384x_EVACK_WTERR		((u16)BIT14)
#define		HFA384x_EVACK_INFDROP		((u16)BIT13)
#define		HFA384x_EVACK_INFO		((u16)BIT7)
#define		HFA384x_EVACK_DTIM		((u16)BIT5)
#define		HFA384x_EVACK_CMD		((u16)BIT4)
#define		HFA384x_EVACK_ALLOC		((u16)BIT3)
#define		HFA384x_EVACK_TXEXC		((u16)BIT2)
#define		HFA384x_EVACK_TX		((u16)BIT1)
#define		HFA384x_EVACK_RX		((u16)BIT0)

#define		HFA384x_CONTROL_AUXEN		((u16)(BIT15 | BIT14))


/*--- Command Code Constants --------------------------*/
/*--- Controller Commands --------------------------*/
#define		HFA384x_CMDCODE_INIT		((u16)0x00)
#define		HFA384x_CMDCODE_ENABLE		((u16)0x01)
#define		HFA384x_CMDCODE_DISABLE		((u16)0x02)
#define		HFA384x_CMDCODE_DIAG		((u16)0x03)

/*--- Buffer Mgmt Commands --------------------------*/
#define		HFA384x_CMDCODE_ALLOC		((u16)0x0A)
#define		HFA384x_CMDCODE_TX		((u16)0x0B)
#define		HFA384x_CMDCODE_CLRPRST		((u16)0x12)

/*--- Regulate Commands --------------------------*/
#define		HFA384x_CMDCODE_NOTIFY		((u16)0x10)
#define		HFA384x_CMDCODE_INQ		((u16)0x11)

/*--- Configure Commands --------------------------*/
#define		HFA384x_CMDCODE_ACCESS		((u16)0x21)
#define		HFA384x_CMDCODE_DOWNLD		((u16)0x22)

/*--- Debugging Commands -----------------------------*/
#define 	HFA384x_CMDCODE_MONITOR		((u16)(0x38))
#define		HFA384x_MONITOR_ENABLE		((u16)(0x0b))
#define		HFA384x_MONITOR_DISABLE		((u16)(0x0f))

/*--- Result Codes --------------------------*/
#define		HFA384x_SUCCESS			((u16)(0x00))
#define		HFA384x_CARD_FAIL		((u16)(0x01))
#define		HFA384x_NO_BUFF			((u16)(0x05))
#define		HFA384x_CMD_ERR			((u16)(0x7F))

/*--- Programming Modes --------------------------
	MODE 0: Disable programming
	MODE 1: Enable volatile memory programming
	MODE 2: Enable non-volatile memory programming
	MODE 3: Program non-volatile memory section
--------------------------------------------------*/
#define		HFA384x_PROGMODE_DISABLE	((u16)0x00)
#define		HFA384x_PROGMODE_RAM		((u16)0x01)
#define		HFA384x_PROGMODE_NV		((u16)0x02)
#define		HFA384x_PROGMODE_NVWRITE	((u16)0x03)

/*--- AUX register enable --------------------------*/
#define		HFA384x_AUXPW0			((u16)0xfe01)
#define		HFA384x_AUXPW1			((u16)0xdc23)
#define		HFA384x_AUXPW2			((u16)0xba45)

#define		HFA384x_CONTROL_AUX_ISDISABLED	((u16)0x0000)
#define		HFA384x_CONTROL_AUX_ISENABLED	((u16)0xc000)
#define		HFA384x_CONTROL_AUX_DOENABLE	((u16)0x8000)
#define		HFA384x_CONTROL_AUX_DODISABLE	((u16)0x4000)

/*--- Record ID Constants --------------------------*/
/*--------------------------------------------------------------------
Configuration RIDs: Network Parameters, Static Configuration Entities
--------------------------------------------------------------------*/
#define		HFA384x_RID_CNFPORTTYPE		((u16)0xFC00)
#define		HFA384x_RID_CNFOWNMACADDR	((u16)0xFC01)
#define		HFA384x_RID_CNFDESIREDSSID	((u16)0xFC02)
#define		HFA384x_RID_CNFOWNCHANNEL	((u16)0xFC03)
#define		HFA384x_RID_CNFOWNSSID		((u16)0xFC04)
#define		HFA384x_RID_CNFOWNATIMWIN	((u16)0xFC05)
#define		HFA384x_RID_CNFSYSSCALE		((u16)0xFC06)
#define		HFA384x_RID_CNFMAXDATALEN	((u16)0xFC07)
#define		HFA384x_RID_CNFWDSADDR		((u16)0xFC08)
#define		HFA384x_RID_CNFPMENABLED	((u16)0xFC09)
#define		HFA384x_RID_CNFPMEPS		((u16)0xFC0A)
#define		HFA384x_RID_CNFMULTICASTRX	((u16)0xFC0B)
#define		HFA384x_RID_CNFMAXSLEEPDUR	((u16)0xFC0C)
#define		HFA384x_RID_CNFPMHOLDDUR	((u16)0xFC0D)
#define		HFA384x_RID_CNFOWNNAME		((u16)0xFC0E)
#define		HFA384x_RID_CNFOWNDTIMPER	((u16)0xFC10)
#define		HFA384x_RID_CNFWDSADDR1		((u16)0xFC11)
#define		HFA384x_RID_CNFWDSADDR2		((u16)0xFC12)
#define		HFA384x_RID_CNFWDSADDR3		((u16)0xFC13)
#define		HFA384x_RID_CNFWDSADDR4		((u16)0xFC14)
#define		HFA384x_RID_CNFWDSADDR5		((u16)0xFC15)
#define		HFA384x_RID_CNFWDSADDR6		((u16)0xFC16)
#define		HFA384x_RID_CNFMCASTPMBUFF	((u16)0xFC17)

/*--------------------------------------------------------------------
Configuration RID lengths: Network Params, Static Config Entities
  This is the length of JUST the DATA part of the RID (does not
  include the len or code fields)
--------------------------------------------------------------------*/
/* TODO: fill in the rest of these */
#define		HFA384x_RID_CNFPORTTYPE_LEN	((u16)2)
#define		HFA384x_RID_CNFOWNMACADDR_LEN	((u16)6)
#define		HFA384x_RID_CNFDESIREDSSID_LEN	((u16)34)
#define		HFA384x_RID_CNFOWNCHANNEL_LEN	((u16)2)
#define		HFA384x_RID_CNFOWNSSID_LEN	((u16)34)
#define		HFA384x_RID_CNFOWNATIMWIN_LEN	((u16)2)
#define		HFA384x_RID_CNFSYSSCALE_LEN	((u16)0)
#define		HFA384x_RID_CNFMAXDATALEN_LEN	((u16)0)
#define		HFA384x_RID_CNFWDSADDR_LEN	((u16)6)
#define		HFA384x_RID_CNFPMENABLED_LEN	((u16)0)
#define		HFA384x_RID_CNFPMEPS_LEN	((u16)0)
#define		HFA384x_RID_CNFMULTICASTRX_LEN	((u16)0)
#define		HFA384x_RID_CNFMAXSLEEPDUR_LEN	((u16)0)
#define		HFA384x_RID_CNFPMHOLDDUR_LEN	((u16)0)
#define		HFA384x_RID_CNFOWNNAME_LEN	((u16)34)
#define		HFA384x_RID_CNFOWNDTIMPER_LEN	((u16)0)
#define		HFA384x_RID_CNFWDSADDR1_LEN	((u16)6)
#define		HFA384x_RID_CNFWDSADDR2_LEN	((u16)6)
#define		HFA384x_RID_CNFWDSADDR3_LEN	((u16)6)
#define		HFA384x_RID_CNFWDSADDR4_LEN	((u16)6)
#define		HFA384x_RID_CNFWDSADDR5_LEN	((u16)6)
#define		HFA384x_RID_CNFWDSADDR6_LEN	((u16)6)
#define		HFA384x_RID_CNFMCASTPMBUFF_LEN	((u16)0)
#define		HFA384x_RID_CNFAUTHENTICATION_LEN ((u16)sizeof(u16))
#define		HFA384x_RID_CNFMAXSLEEPDUR_LEN	((u16)0)

/*--------------------------------------------------------------------
Configuration RIDs: Network Parameters, Dynamic Configuration Entities
--------------------------------------------------------------------*/
#define		HFA384x_RID_GROUPADDR		((u16)0xFC80)
#define		HFA384x_RID_CREATEIBSS		((u16)0xFC81)
#define		HFA384x_RID_FRAGTHRESH		((u16)0xFC82)
#define		HFA384x_RID_RTSTHRESH		((u16)0xFC83)
#define		HFA384x_RID_TXRATECNTL		((u16)0xFC84)
#define		HFA384x_RID_PROMISCMODE		((u16)0xFC85)
#define		HFA384x_RID_FRAGTHRESH0		((u16)0xFC90)
#define		HFA384x_RID_FRAGTHRESH1		((u16)0xFC91)
#define		HFA384x_RID_FRAGTHRESH2		((u16)0xFC92)
#define		HFA384x_RID_FRAGTHRESH3		((u16)0xFC93)
#define		HFA384x_RID_FRAGTHRESH4		((u16)0xFC94)
#define		HFA384x_RID_FRAGTHRESH5		((u16)0xFC95)
#define		HFA384x_RID_FRAGTHRESH6		((u16)0xFC96)
#define		HFA384x_RID_RTSTHRESH0		((u16)0xFC97)
#define		HFA384x_RID_RTSTHRESH1		((u16)0xFC98)
#define		HFA384x_RID_RTSTHRESH2		((u16)0xFC99)
#define		HFA384x_RID_RTSTHRESH3		((u16)0xFC9A)
#define		HFA384x_RID_RTSTHRESH4		((u16)0xFC9B)
#define		HFA384x_RID_RTSTHRESH5		((u16)0xFC9C)
#define		HFA384x_RID_RTSTHRESH6		((u16)0xFC9D)
#define		HFA384x_RID_TXRATECNTL0		((u16)0xFC9E)
#define		HFA384x_RID_TXRATECNTL1		((u16)0xFC9F)
#define		HFA384x_RID_TXRATECNTL2		((u16)0xFCA0)
#define		HFA384x_RID_TXRATECNTL3		((u16)0xFCA1)
#define		HFA384x_RID_TXRATECNTL4		((u16)0xFCA2)
#define		HFA384x_RID_TXRATECNTL5		((u16)0xFCA3)
#define		HFA384x_RID_TXRATECNTL6		((u16)0xFCA4)

/*--------------------------------------------------------------------
Configuration RID Lengths: Network Param, Dynamic Config Entities
  This is the length of JUST the DATA part of the RID (does not
  include the len or code fields)
--------------------------------------------------------------------*/
/* TODO: fill in the rest of these */
#define		HFA384x_RID_GROUPADDR_LEN	((u16)16 * WLAN_ADDR_LEN)
#define		HFA384x_RID_CREATEIBSS_LEN	((u16)0)
#define		HFA384x_RID_FRAGTHRESH_LEN	((u16)0)
#define		HFA384x_RID_RTSTHRESH_LEN	((u16)0)
#define		HFA384x_RID_TXRATECNTL_LEN	((u16)4)
#define		HFA384x_RID_PROMISCMODE_LEN	((u16)2)
#define		HFA384x_RID_FRAGTHRESH0_LEN	((u16)0)
#define		HFA384x_RID_FRAGTHRESH1_LEN	((u16)0)
#define		HFA384x_RID_FRAGTHRESH2_LEN	((u16)0)
#define		HFA384x_RID_FRAGTHRESH3_LEN	((u16)0)
#define		HFA384x_RID_FRAGTHRESH4_LEN	((u16)0)
#define		HFA384x_RID_FRAGTHRESH5_LEN	((u16)0)
#define		HFA384x_RID_FRAGTHRESH6_LEN	((u16)0)
#define		HFA384x_RID_RTSTHRESH0_LEN	((u16)0)
#define		HFA384x_RID_RTSTHRESH1_LEN	((u16)0)
#define		HFA384x_RID_RTSTHRESH2_LEN	((u16)0)
#define		HFA384x_RID_RTSTHRESH3_LEN	((u16)0)
#define		HFA384x_RID_RTSTHRESH4_LEN	((u16)0)
#define		HFA384x_RID_RTSTHRESH5_LEN	((u16)0)
#define		HFA384x_RID_RTSTHRESH6_LEN	((u16)0)
#define		HFA384x_RID_TXRATECNTL0_LEN	((u16)0)
#define		HFA384x_RID_TXRATECNTL1_LEN	((u16)0)
#define		HFA384x_RID_TXRATECNTL2_LEN	((u16)0)
#define		HFA384x_RID_TXRATECNTL3_LEN	((u16)0)
#define		HFA384x_RID_TXRATECNTL4_LEN	((u16)0)
#define		HFA384x_RID_TXRATECNTL5_LEN	((u16)0)
#define		HFA384x_RID_TXRATECNTL6_LEN	((u16)0)

/*--------------------------------------------------------------------
Configuration RIDs: Behavior Parameters
--------------------------------------------------------------------*/
#define		HFA384x_RID_ITICKTIME		((u16)0xFCE0)

/*--------------------------------------------------------------------
Configuration RID Lengths: Behavior Parameters
  This is the length of JUST the DATA part of the RID (does not
  include the len or code fields)
--------------------------------------------------------------------*/
#define		HFA384x_RID_ITICKTIME_LEN	((u16)2)

/*----------------------------------------------------------------------
Information RIDs: NIC Information
--------------------------------------------------------------------*/
#define		HFA384x_RID_MAXLOADTIME		((u16)0xFD00)
#define		HFA384x_RID_DOWNLOADBUFFER	((u16)0xFD01)
#define		HFA384x_RID_PRIIDENTITY		((u16)0xFD02)
#define		HFA384x_RID_PRISUPRANGE		((u16)0xFD03)
#define		HFA384x_RID_PRI_CFIACTRANGES	((u16)0xFD04)
#define		HFA384x_RID_NICSERIALNUMBER	((u16)0xFD0A)
#define		HFA384x_RID_NICIDENTITY		((u16)0xFD0B)
#define		HFA384x_RID_MFISUPRANGE		((u16)0xFD0C)
#define		HFA384x_RID_CFISUPRANGE		((u16)0xFD0D)
#define		HFA384x_RID_CHANNELLIST		((u16)0xFD10)
#define		HFA384x_RID_REGULATORYDOMAINS	((u16)0xFD11)
#define		HFA384x_RID_TEMPTYPE		((u16)0xFD12)
#define		HFA384x_RID_CIS			((u16)0xFD13)
#define		HFA384x_RID_STAIDENTITY		((u16)0xFD20)
#define		HFA384x_RID_STASUPRANGE		((u16)0xFD21)
#define		HFA384x_RID_STA_MFIACTRANGES	((u16)0xFD22)
#define		HFA384x_RID_STA_CFIACTRANGES	((u16)0xFD23)
#define		HFA384x_RID_BUILDSEQ		((u16)0xFFFE)
#define		HFA384x_RID_FWID		((u16)0xFFFF)

/*----------------------------------------------------------------------
Information RID Lengths: NIC Information
  This is the length of JUST the DATA part of the RID (does not
  include the len or code fields)
--------------------------------------------------------------------*/
#define		HFA384x_RID_MAXLOADTIME_LEN		((u16)0)
#define		HFA384x_RID_DOWNLOADBUFFER_LEN		((u16)sizeof(hfa384x_downloadbuffer_t))
#define		HFA384x_RID_PRIIDENTITY_LEN		((u16)8)
#define		HFA384x_RID_PRISUPRANGE_LEN		((u16)10)
#define		HFA384x_RID_CFIACTRANGES_LEN		((u16)10)
#define		HFA384x_RID_NICSERIALNUMBER_LEN		((u16)12)
#define		HFA384x_RID_NICIDENTITY_LEN		((u16)8)
#define		HFA384x_RID_MFISUPRANGE_LEN		((u16)10)
#define		HFA384x_RID_CFISUPRANGE_LEN		((u16)10)
#define		HFA384x_RID_CHANNELLIST_LEN		((u16)0)
#define		HFA384x_RID_REGULATORYDOMAINS_LEN	((u16)12)
#define		HFA384x_RID_TEMPTYPE_LEN		((u16)0)
#define		HFA384x_RID_CIS_LEN			((u16)480)
#define		HFA384x_RID_STAIDENTITY_LEN		((u16)8)
#define		HFA384x_RID_STASUPRANGE_LEN		((u16)10)
#define		HFA384x_RID_MFIACTRANGES_LEN		((u16)10)
#define		HFA384x_RID_CFIACTRANGES2_LEN		((u16)10)
#define		HFA384x_RID_BUILDSEQ_LEN		((u16)sizeof(hfa384x_BuildSeq_t))
#define		HFA384x_RID_FWID_LEN			((u16)sizeof(hfa384x_FWID_t))

/*--------------------------------------------------------------------
Information RIDs:  MAC Information
--------------------------------------------------------------------*/
#define		HFA384x_RID_PORTSTATUS		((u16)0xFD40)
#define		HFA384x_RID_CURRENTSSID		((u16)0xFD41)
#define		HFA384x_RID_CURRENTBSSID	((u16)0xFD42)
#define		HFA384x_RID_COMMSQUALITY	((u16)0xFD43)
#define		HFA384x_RID_CURRENTTXRATE	((u16)0xFD44)
#define		HFA384x_RID_CURRENTBCNint	((u16)0xFD45)
#define		HFA384x_RID_CURRENTSCALETHRESH	((u16)0xFD46)
#define		HFA384x_RID_PROTOCOLRSPTIME	((u16)0xFD47)
#define		HFA384x_RID_SHORTRETRYLIMIT	((u16)0xFD48)
#define		HFA384x_RID_LONGRETRYLIMIT	((u16)0xFD49)
#define		HFA384x_RID_MAXTXLIFETIME	((u16)0xFD4A)
#define		HFA384x_RID_MAXRXLIFETIME	((u16)0xFD4B)
#define		HFA384x_RID_CFPOLLABLE		((u16)0xFD4C)
#define		HFA384x_RID_AUTHALGORITHMS	((u16)0xFD4D)
#define		HFA384x_RID_PRIVACYOPTIMP	((u16)0xFD4F)
#define		HFA384x_RID_DBMCOMMSQUALITY	((u16)0xFD51)
#define		HFA384x_RID_CURRENTTXRATE1	((u16)0xFD80)
#define		HFA384x_RID_CURRENTTXRATE2	((u16)0xFD81)
#define		HFA384x_RID_CURRENTTXRATE3	((u16)0xFD82)
#define		HFA384x_RID_CURRENTTXRATE4	((u16)0xFD83)
#define		HFA384x_RID_CURRENTTXRATE5	((u16)0xFD84)
#define		HFA384x_RID_CURRENTTXRATE6	((u16)0xFD85)
#define		HFA384x_RID_OWNMACADDRESS	((u16)0xFD86)
// #define	HFA384x_RID_PCFINFO		((u16)0xFD87)
#define		HFA384x_RID_SCANRESULTS       	((u16)0xFD88) // NEW
#define		HFA384x_RID_HOSTSCANRESULTS   	((u16)0xFD89) // NEW
#define		HFA384x_RID_AUTHENTICATIONUSED	((u16)0xFD8A) // NEW
#define		HFA384x_RID_ASSOCIATEFAILURE  	((u16)0xFD8D) // 1.8.0

/*--------------------------------------------------------------------
Information RID Lengths:  MAC Information
  This is the length of JUST the DATA part of the RID (does not
  include the len or code fields)
--------------------------------------------------------------------*/
#define		HFA384x_RID_PORTSTATUS_LEN		((u16)0)
#define		HFA384x_RID_CURRENTSSID_LEN		((u16)34)
#define		HFA384x_RID_CURRENTBSSID_LEN		((u16)WLAN_BSSID_LEN)
#define		HFA384x_RID_COMMSQUALITY_LEN		((u16)sizeof(hfa384x_commsquality_t))
#define		HFA384x_RID_DBMCOMMSQUALITY_LEN		((u16)sizeof(hfa384x_dbmcommsquality_t))
#define		HFA384x_RID_CURRENTTXRATE_LEN		((u16)0)
#define		HFA384x_RID_CURRENTBCNint_LEN		((u16)0)
#define		HFA384x_RID_STACURSCALETHRESH_LEN	((u16)12)
#define		HFA384x_RID_APCURSCALETHRESH_LEN	((u16)6)
#define		HFA384x_RID_PROTOCOLRSPTIME_LEN		((u16)0)
#define		HFA384x_RID_SHORTRETRYLIMIT_LEN		((u16)0)
#define		HFA384x_RID_LONGRETRYLIMIT_LEN		((u16)0)
#define		HFA384x_RID_MAXTXLIFETIME_LEN		((u16)0)
#define		HFA384x_RID_MAXRXLIFETIME_LEN		((u16)0)
#define		HFA384x_RID_CFPOLLABLE_LEN		((u16)0)
#define		HFA384x_RID_AUTHALGORITHMS_LEN		((u16)4)
#define		HFA384x_RID_PRIVACYOPTIMP_LEN		((u16)0)
#define		HFA384x_RID_CURRENTTXRATE1_LEN		((u16)0)
#define		HFA384x_RID_CURRENTTXRATE2_LEN		((u16)0)
#define		HFA384x_RID_CURRENTTXRATE3_LEN		((u16)0)
#define		HFA384x_RID_CURRENTTXRATE4_LEN		((u16)0)
#define		HFA384x_RID_CURRENTTXRATE5_LEN		((u16)0)
#define		HFA384x_RID_CURRENTTXRATE6_LEN		((u16)0)
#define		HFA384x_RID_OWNMACADDRESS_LEN		((u16)6)
#define		HFA384x_RID_PCFINFO_LEN			((u16)6)
#define		HFA384x_RID_CNFAPPCFINFO_LEN		((u16)sizeof(hfa384x_PCFInfo_data_t))
#define		HFA384x_RID_SCANREQUEST_LEN		((u16)sizeof(hfa384x_ScanRequest_data_t))
#define		HFA384x_RID_JOINREQUEST_LEN		((u16)sizeof(hfa384x_JoinRequest_data_t))
#define		HFA384x_RID_AUTHENTICATESTA_LEN		((u16)sizeof(hfa384x_authenticateStation_data_t))
#define		HFA384x_RID_CHANNELINFOREQUEST_LEN	((u16)sizeof(hfa384x_ChannelInfoRequest_data_t))
/*--------------------------------------------------------------------
Information RIDs:  Modem Information
--------------------------------------------------------------------*/
#define		HFA384x_RID_PHYTYPE		((u16)0xFDC0)
#define		HFA384x_RID_CURRENTCHANNEL	((u16)0xFDC1)
#define		HFA384x_RID_CURRENTPOWERSTATE	((u16)0xFDC2)
#define		HFA384x_RID_CCAMODE		((u16)0xFDC3)
#define		HFA384x_RID_SUPPORTEDDATARATES	((u16)0xFDC6)
#define		HFA384x_RID_LFOSTATUS           ((u16)0xFDC7) // 1.7.1

/*--------------------------------------------------------------------
Information RID Lengths:  Modem Information
  This is the length of JUST the DATA part of the RID (does not
  include the len or code fields)
--------------------------------------------------------------------*/
#define		HFA384x_RID_PHYTYPE_LEN			((u16)0)
#define		HFA384x_RID_CURRENTCHANNEL_LEN		((u16)0)
#define		HFA384x_RID_CURRENTPOWERSTATE_LEN	((u16)0)
#define		HFA384x_RID_CCAMODE_LEN			((u16)0)
#define		HFA384x_RID_SUPPORTEDDATARATES_LEN	((u16)10)

/*--------------------------------------------------------------------
API ENHANCEMENTS (NOT ALREADY IMPLEMENTED)
--------------------------------------------------------------------*/
#define		HFA384x_RID_CNFWEPDEFAULTKEYID	((u16)0xFC23)
#define		HFA384x_RID_CNFWEPDEFAULTKEY0	((u16)0xFC24)
#define		HFA384x_RID_CNFWEPDEFAULTKEY1	((u16)0xFC25)
#define		HFA384x_RID_CNFWEPDEFAULTKEY2	((u16)0xFC26)
#define		HFA384x_RID_CNFWEPDEFAULTKEY3	((u16)0xFC27)
#define		HFA384x_RID_CNFWEPFLAGS		((u16)0xFC28)
#define		HFA384x_RID_CNFWEPKEYMAPTABLE	((u16)0xFC29)
#define		HFA384x_RID_CNFAUTHENTICATION	((u16)0xFC2A)
#define		HFA384x_RID_CNFMAXASSOCSTATIONS	((u16)0xFC2B)
#define		HFA384x_RID_CNFTXCONTROL	((u16)0xFC2C)
#define		HFA384x_RID_CNFROAMINGMODE	((u16)0xFC2D)
#define		HFA384x_RID_CNFHOSTAUTHASSOC	((u16)0xFC2E)
#define		HFA384x_RID_CNFRCVCRCERROR	((u16)0xFC30)
// #define		HFA384x_RID_CNFMMLIFE		((u16)0xFC31)
#define		HFA384x_RID_CNFALTRETRYCNT	((u16)0xFC32)
#define		HFA384x_RID_CNFAPBCNint		((u16)0xFC33)
#define		HFA384x_RID_CNFAPPCFINFO	((u16)0xFC34)
#define		HFA384x_RID_CNFSTAPCFINFO	((u16)0xFC35)
#define		HFA384x_RID_CNFPRIORITYQUSAGE	((u16)0xFC37)
#define		HFA384x_RID_CNFTIMCTRL		((u16)0xFC40)
#define		HFA384x_RID_CNFTHIRTY2TALLY	((u16)0xFC42)
#define		HFA384x_RID_CNFENHSECURITY	((u16)0xFC43)
#define		HFA384x_RID_CNFDBMADJUST  	((u16)0xFC46) // NEW
#define		HFA384x_RID_CNFWPADATA       	((u16)0xFC48) // 1.7.0
#define		HFA384x_RID_CNFPROPOGATIONDELAY	((u16)0xFC49) // 1.7.6
#define		HFA384x_RID_CNFSHORTPREAMBLE	((u16)0xFCB0)
#define		HFA384x_RID_CNFEXCLONGPREAMBLE	((u16)0xFCB1)
#define		HFA384x_RID_CNFAUTHRSPTIMEOUT	((u16)0xFCB2)
#define		HFA384x_RID_CNFBASICRATES	((u16)0xFCB3)
#define		HFA384x_RID_CNFSUPPRATES	((u16)0xFCB4)
#define		HFA384x_RID_CNFFALLBACKCTRL	((u16)0xFCB5) // NEW
#define		HFA384x_RID_WEPKEYSTATUS   	((u16)0xFCB6) // NEW
#define		HFA384x_RID_WEPKEYMAPINDEX 	((u16)0xFCB7) // NEW
#define		HFA384x_RID_BROADCASTKEYID 	((u16)0xFCB8) // NEW
#define		HFA384x_RID_ENTSECFLAGEYID 	((u16)0xFCB9) // NEW
#define		HFA384x_RID_CNFPASSIVESCANCTRL	((u16)0xFCBA) // NEW STA
#define		HFA384x_RID_CNFWPAHANDLING	((u16)0xFCBB) // 1.7.0
#define		HFA384x_RID_MDCCONTROL        	((u16)0xFCBC) // 1.7.0/1.4.0
#define		HFA384x_RID_MDCCOUNTRY        	((u16)0xFCBD) // 1.7.0/1.4.0
#define		HFA384x_RID_TXPOWERMAX        	((u16)0xFCBE) // 1.7.0/1.4.0
#define		HFA384x_RID_CNFLFOENBLED      	((u16)0xFCBF) // 1.6.3
#define         HFA384x_RID_CAPINFO             ((u16)0xFCC0) // 1.7.0/1.3.7
#define         HFA384x_RID_LISTENintERVAL      ((u16)0xFCC1) // 1.7.0/1.3.7
#define         HFA384x_RID_DIVERSITYENABLED    ((u16)0xFCC2) // 1.7.0/1.3.7
#define         HFA384x_RID_LED_CONTROL         ((u16)0xFCC4) // 1.7.6
#define         HFA384x_RID_HFO_DELAY           ((u16)0xFCC5) // 1.7.6
#define         HFA384x_RID_DISSALOWEDBSSID     ((u16)0xFCC6) // 1.8.0
#define		HFA384x_RID_SCANREQUEST		((u16)0xFCE1)
#define		HFA384x_RID_JOINREQUEST		((u16)0xFCE2)
#define		HFA384x_RID_AUTHENTICATESTA	((u16)0xFCE3)
#define		HFA384x_RID_CHANNELINFOREQUEST	((u16)0xFCE4)
#define		HFA384x_RID_HOSTSCAN          	((u16)0xFCE5) // NEW STA
#define		HFA384x_RID_ASSOCIATESTA	((u16)0xFCE6)

#define		HFA384x_RID_CNFWEPDEFAULTKEY_LEN	((u16)6)
#define		HFA384x_RID_CNFWEP128DEFAULTKEY_LEN	((u16)14)
#define		HFA384x_RID_CNFPRIOQUSAGE_LEN		((u16)4)
/*--------------------------------------------------------------------
PD Record codes
--------------------------------------------------------------------*/
#define HFA384x_PDR_PCB_PARTNUM		((u16)0x0001)
#define HFA384x_PDR_PDAVER		((u16)0x0002)
#define HFA384x_PDR_NIC_SERIAL		((u16)0x0003)
#define HFA384x_PDR_MKK_MEASUREMENTS	((u16)0x0004)
#define HFA384x_PDR_NIC_RAMSIZE		((u16)0x0005)
#define HFA384x_PDR_MFISUPRANGE		((u16)0x0006)
#define HFA384x_PDR_CFISUPRANGE		((u16)0x0007)
#define HFA384x_PDR_NICID		((u16)0x0008)
//#define HFA384x_PDR_REFDAC_MEASUREMENTS	((u16)0x0010)
//#define HFA384x_PDR_VGDAC_MEASUREMENTS	((u16)0x0020)
//#define HFA384x_PDR_LEVEL_COMP_MEASUREMENTS	((u16)0x0030)
//#define HFA384x_PDR_MODEM_TRIMDAC_MEASUREMENTS	((u16)0x0040)
//#define HFA384x_PDR_COREGA_HACK		((u16)0x00ff)
#define HFA384x_PDR_MAC_ADDRESS		((u16)0x0101)
//#define HFA384x_PDR_MKK_CALLNAME	((u16)0x0102)
#define HFA384x_PDR_REGDOMAIN		((u16)0x0103)
#define HFA384x_PDR_ALLOWED_CHANNEL	((u16)0x0104)
#define HFA384x_PDR_DEFAULT_CHANNEL	((u16)0x0105)
//#define HFA384x_PDR_PRIVACY_OPTION	((u16)0x0106)
#define HFA384x_PDR_TEMPTYPE		((u16)0x0107)
//#define HFA384x_PDR_REFDAC_SETUP	((u16)0x0110)
//#define HFA384x_PDR_VGDAC_SETUP		((u16)0x0120)
//#define HFA384x_PDR_LEVEL_COMP_SETUP	((u16)0x0130)
//#define HFA384x_PDR_TRIMDAC_SETUP	((u16)0x0140)
#define HFA384x_PDR_IFR_SETTING		((u16)0x0200)
#define HFA384x_PDR_RFR_SETTING		((u16)0x0201)
#define HFA384x_PDR_HFA3861_BASELINE	((u16)0x0202)
#define HFA384x_PDR_HFA3861_SHADOW	((u16)0x0203)
#define HFA384x_PDR_HFA3861_IFRF	((u16)0x0204)
#define HFA384x_PDR_HFA3861_CHCALSP	((u16)0x0300)
#define HFA384x_PDR_HFA3861_CHCALI	((u16)0x0301)
#define HFA384x_PDR_MAX_TX_POWER  	((u16)0x0302)
#define HFA384x_PDR_MASTER_CHAN_LIST	((u16)0x0303)
#define HFA384x_PDR_3842_NIC_CONFIG	((u16)0x0400)
#define HFA384x_PDR_USB_ID		((u16)0x0401)
#define HFA384x_PDR_PCI_ID		((u16)0x0402)
#define HFA384x_PDR_PCI_IFCONF		((u16)0x0403)
#define HFA384x_PDR_PCI_PMCONF		((u16)0x0404)
#define HFA384x_PDR_RFENRGY		((u16)0x0406)
#define HFA384x_PDR_USB_POWER_TYPE      ((u16)0x0407)
//#define HFA384x_PDR_UNKNOWN408		((u16)0x0408)
#define HFA384x_PDR_USB_MAX_POWER	((u16)0x0409)
#define HFA384x_PDR_USB_MANUFACTURER	((u16)0x0410)
#define HFA384x_PDR_USB_PRODUCT  	((u16)0x0411)
#define HFA384x_PDR_ANT_DIVERSITY   	((u16)0x0412)
#define HFA384x_PDR_HFO_DELAY       	((u16)0x0413)
#define HFA384x_PDR_SCALE_THRESH 	((u16)0x0414)

#define HFA384x_PDR_HFA3861_MANF_TESTSP	((u16)0x0900)
#define HFA384x_PDR_HFA3861_MANF_TESTI	((u16)0x0901)
#define HFA384x_PDR_END_OF_PDA		((u16)0x0000)


/*=============================================================*/
/*------ Macros -----------------------------------------------*/

/*--- Register ID macros ------------------------*/

#define		HFA384x_CMD		HFA384x_CMD_OFF
#define		HFA384x_PARAM0		HFA384x_PARAM0_OFF
#define		HFA384x_PARAM1		HFA384x_PARAM1_OFF
#define		HFA384x_PARAM2		HFA384x_PARAM2_OFF
#define		HFA384x_STATUS		HFA384x_STATUS_OFF
#define		HFA384x_RESP0		HFA384x_RESP0_OFF
#define		HFA384x_RESP1		HFA384x_RESP1_OFF
#define		HFA384x_RESP2		HFA384x_RESP2_OFF
#define		HFA384x_INFOFID		HFA384x_INFOFID_OFF
#define		HFA384x_RXFID		HFA384x_RXFID_OFF
#define		HFA384x_ALLOCFID	HFA384x_ALLOCFID_OFF
#define		HFA384x_TXCOMPLFID	HFA384x_TXCOMPLFID_OFF
#define		HFA384x_SELECT0		HFA384x_SELECT0_OFF
#define		HFA384x_OFFSET0		HFA384x_OFFSET0_OFF
#define		HFA384x_DATA0		HFA384x_DATA0_OFF
#define		HFA384x_SELECT1		HFA384x_SELECT1_OFF
#define		HFA384x_OFFSET1		HFA384x_OFFSET1_OFF
#define		HFA384x_DATA1		HFA384x_DATA1_OFF
#define		HFA384x_EVSTAT		HFA384x_EVSTAT_OFF
#define		HFA384x_intEN		HFA384x_INTEN_OFF
#define		HFA384x_EVACK		HFA384x_EVACK_OFF
#define		HFA384x_CONTROL		HFA384x_CONTROL_OFF
#define		HFA384x_SWSUPPORT0	HFA384x_SWSUPPORT0_OFF
#define		HFA384x_SWSUPPORT1	HFA384x_SWSUPPORT1_OFF
#define		HFA384x_SWSUPPORT2	HFA384x_SWSUPPORT2_OFF
#define		HFA384x_AUXPAGE		HFA384x_AUXPAGE_OFF
#define		HFA384x_AUXOFFSET	HFA384x_AUXOFFSET_OFF
#define		HFA384x_AUXDATA		HFA384x_AUXDATA_OFF
#define		HFA384x_PCICOR		HFA384x_PCICOR_OFF
#define		HFA384x_PCIHCR		HFA384x_PCIHCR_OFF


/*--- Register Test/Get/Set Field macros ------------------------*/

#define		HFA384x_CMD_ISBUSY(value)		((u16)(((u16)value) & HFA384x_CMD_BUSY))
#define		HFA384x_CMD_AINFO_GET(value)		((u16)(((u16)(value) & HFA384x_CMD_AINFO) >> 8))
#define		HFA384x_CMD_AINFO_SET(value)		((u16)((u16)(value) << 8))
#define		HFA384x_CMD_MACPORT_GET(value)		((u16)(HFA384x_CMD_AINFO_GET((u16)(value) & HFA384x_CMD_MACPORT)))
#define		HFA384x_CMD_MACPORT_SET(value)		((u16)HFA384x_CMD_AINFO_SET(value))
#define		HFA384x_CMD_ISRECL(value)		((u16)(HFA384x_CMD_AINFO_GET((u16)(value) & HFA384x_CMD_RECL)))
#define		HFA384x_CMD_RECL_SET(value)		((u16)HFA384x_CMD_AINFO_SET(value))
#define		HFA384x_CMD_QOS_GET(value)		((u16)((((u16)(value))&((u16)0x3000)) >> 12))
#define		HFA384x_CMD_QOS_SET(value)		((u16)((((u16)(value)) << 12) & 0x3000))
#define		HFA384x_CMD_ISWRITE(value)		((u16)(HFA384x_CMD_AINFO_GET((u16)(value) & HFA384x_CMD_WRITE)))
#define		HFA384x_CMD_WRITE_SET(value)		((u16)HFA384x_CMD_AINFO_SET((u16)value))
#define		HFA384x_CMD_PROGMODE_GET(value)		((u16)(HFA384x_CMD_AINFO_GET((u16)(value) & HFA384x_CMD_PROGMODE)))
#define		HFA384x_CMD_PROGMODE_SET(value)		((u16)HFA384x_CMD_AINFO_SET((u16)value))
#define		HFA384x_CMD_CMDCODE_GET(value)		((u16)(((u16)(value)) & HFA384x_CMD_CMDCODE))
#define		HFA384x_CMD_CMDCODE_SET(value)		((u16)(value))

#define		HFA384x_STATUS_RESULT_GET(value)	((u16)((((u16)(value)) & HFA384x_STATUS_RESULT) >> 8))
#define		HFA384x_STATUS_RESULT_SET(value)	(((u16)(value)) << 8)
#define		HFA384x_STATUS_CMDCODE_GET(value)	(((u16)(value)) & HFA384x_STATUS_CMDCODE)
#define		HFA384x_STATUS_CMDCODE_SET(value)	((u16)(value))

#define		HFA384x_OFFSET_ISBUSY(value)		((u16)(((u16)(value)) & HFA384x_OFFSET_BUSY))
#define		HFA384x_OFFSET_ISERR(value)		((u16)(((u16)(value)) & HFA384x_OFFSET_ERR))
#define		HFA384x_OFFSET_DATAOFF_GET(value)	((u16)(((u16)(value)) & HFA384x_OFFSET_DATAOFF))
#define		HFA384x_OFFSET_DATAOFF_SET(value)	((u16)(value))

#define		HFA384x_EVSTAT_ISTICK(value)		((u16)(((u16)(value)) & HFA384x_EVSTAT_TICK))
#define		HFA384x_EVSTAT_ISWTERR(value)		((u16)(((u16)(value)) & HFA384x_EVSTAT_WTERR))
#define		HFA384x_EVSTAT_ISINFDROP(value)		((u16)(((u16)(value)) & HFA384x_EVSTAT_INFDROP))
#define		HFA384x_EVSTAT_ISINFO(value)		((u16)(((u16)(value)) & HFA384x_EVSTAT_INFO))
#define		HFA384x_EVSTAT_ISDTIM(value)		((u16)(((u16)(value)) & HFA384x_EVSTAT_DTIM))
#define		HFA384x_EVSTAT_ISCMD(value)		((u16)(((u16)(value)) & HFA384x_EVSTAT_CMD))
#define		HFA384x_EVSTAT_ISALLOC(value)		((u16)(((u16)(value)) & HFA384x_EVSTAT_ALLOC))
#define		HFA384x_EVSTAT_ISTXEXC(value)		((u16)(((u16)(value)) & HFA384x_EVSTAT_TXEXC))
#define		HFA384x_EVSTAT_ISTX(value)		((u16)(((u16)(value)) & HFA384x_EVSTAT_TX))
#define		HFA384x_EVSTAT_ISRX(value)		((u16)(((u16)(value)) & HFA384x_EVSTAT_RX))

#define		HFA384x_EVSTAT_ISBAP_OP(value)		((u16)(((u16)(value)) & HFA384x_int_BAP_OP))

#define		HFA384x_intEN_ISTICK(value)		((u16)(((u16)(value)) & HFA384x_INTEN_TICK))
#define		HFA384x_intEN_TICK_SET(value)		((u16)(((u16)(value)) << 15))
#define		HFA384x_intEN_ISWTERR(value)		((u16)(((u16)(value)) & HFA384x_INTEN_WTERR))
#define		HFA384x_intEN_WTERR_SET(value)		((u16)(((u16)(value)) << 14))
#define		HFA384x_intEN_ISINFDROP(value)		((u16)(((u16)(value)) & HFA384x_INTEN_INFDROP))
#define		HFA384x_intEN_INFDROP_SET(value)	((u16)(((u16)(value)) << 13))
#define		HFA384x_intEN_ISINFO(value)		((u16)(((u16)(value)) & HFA384x_INTEN_INFO))
#define		HFA384x_intEN_INFO_SET(value)		((u16)(((u16)(value)) << 7))
#define		HFA384x_intEN_ISDTIM(value)		((u16)(((u16)(value)) & HFA384x_INTEN_DTIM))
#define		HFA384x_intEN_DTIM_SET(value)		((u16)(((u16)(value)) << 5))
#define		HFA384x_intEN_ISCMD(value)		((u16)(((u16)(value)) & HFA384x_INTEN_CMD))
#define		HFA384x_intEN_CMD_SET(value)		((u16)(((u16)(value)) << 4))
#define		HFA384x_intEN_ISALLOC(value)		((u16)(((u16)(value)) & HFA384x_INTEN_ALLOC))
#define		HFA384x_intEN_ALLOC_SET(value)		((u16)(((u16)(value)) << 3))
#define		HFA384x_intEN_ISTXEXC(value)		((u16)(((u16)(value)) & HFA384x_INTEN_TXEXC))
#define		HFA384x_intEN_TXEXC_SET(value)		((u16)(((u16)(value)) << 2))
#define		HFA384x_intEN_ISTX(value)		((u16)(((u16)(value)) & HFA384x_INTEN_TX))
#define		HFA384x_intEN_TX_SET(value)		((u16)(((u16)(value)) << 1))
#define		HFA384x_intEN_ISRX(value)		((u16)(((u16)(value)) & HFA384x_INTEN_RX))
#define		HFA384x_intEN_RX_SET(value)		((u16)(((u16)(value)) << 0))

#define		HFA384x_EVACK_ISTICK(value)		((u16)(((u16)(value)) & HFA384x_EVACK_TICK))
#define		HFA384x_EVACK_TICK_SET(value)		((u16)(((u16)(value)) << 15))
#define		HFA384x_EVACK_ISWTERR(value)		((u16)(((u16)(value)) & HFA384x_EVACK_WTERR))
#define		HFA384x_EVACK_WTERR_SET(value)		((u16)(((u16)(value)) << 14))
#define		HFA384x_EVACK_ISINFDROP(value)		((u16)(((u16)(value)) & HFA384x_EVACK_INFDROP))
#define		HFA384x_EVACK_INFDROP_SET(value)	((u16)(((u16)(value)) << 13))
#define		HFA384x_EVACK_ISINFO(value)		((u16)(((u16)(value)) & HFA384x_EVACK_INFO))
#define		HFA384x_EVACK_INFO_SET(value)		((u16)(((u16)(value)) << 7))
#define		HFA384x_EVACK_ISDTIM(value)		((u16)(((u16)(value)) & HFA384x_EVACK_DTIM))
#define		HFA384x_EVACK_DTIM_SET(value)		((u16)(((u16)(value)) << 5))
#define		HFA384x_EVACK_ISCMD(value)		((u16)(((u16)(value)) & HFA384x_EVACK_CMD))
#define		HFA384x_EVACK_CMD_SET(value)		((u16)(((u16)(value)) << 4))
#define		HFA384x_EVACK_ISALLOC(value)		((u16)(((u16)(value)) & HFA384x_EVACK_ALLOC))
#define		HFA384x_EVACK_ALLOC_SET(value)		((u16)(((u16)(value)) << 3))
#define		HFA384x_EVACK_ISTXEXC(value)		((u16)(((u16)(value)) & HFA384x_EVACK_TXEXC))
#define		HFA384x_EVACK_TXEXC_SET(value)		((u16)(((u16)(value)) << 2))
#define		HFA384x_EVACK_ISTX(value)		((u16)(((u16)(value)) & HFA384x_EVACK_TX))
#define		HFA384x_EVACK_TX_SET(value)		((u16)(((u16)(value)) << 1))
#define		HFA384x_EVACK_ISRX(value)		((u16)(((u16)(value)) & HFA384x_EVACK_RX))
#define		HFA384x_EVACK_RX_SET(value)		((u16)(((u16)(value)) << 0))

#define		HFA384x_CONTROL_AUXEN_SET(value)	((u16)(((u16)(value)) << 14))
#define		HFA384x_CONTROL_AUXEN_GET(value)	((u16)(((u16)(value)) >> 14))

/* Byte Order */
#ifdef __KERNEL__
#define hfa384x2host_16(n)	(__le16_to_cpu((u16)(n)))
#define hfa384x2host_32(n)	(__le32_to_cpu((u32)(n)))
#define host2hfa384x_16(n)	(__cpu_to_le16((u16)(n)))
#define host2hfa384x_32(n)	(__cpu_to_le32((u32)(n)))
#endif

/* Host Maintained State Info */
#define HFA384x_STATE_PREINIT	0
#define HFA384x_STATE_INIT	1
#define HFA384x_STATE_RUNNING	2

/*=============================================================*/
/*------ Types and their related constants --------------------*/

#define HFA384x_HOSTAUTHASSOC_HOSTAUTH   BIT0
#define HFA384x_HOSTAUTHASSOC_HOSTASSOC  BIT1

#define HFA384x_WHAHANDLING_DISABLED     0
#define HFA384x_WHAHANDLING_PASSTHROUGH  BIT1

/*-------------------------------------------------------------*/
/* Commonly used basic types */
typedef struct hfa384x_bytestr
{
	u16	len;
	u8	data[0];
} __WLAN_ATTRIB_PACK__ hfa384x_bytestr_t;

typedef struct hfa384x_bytestr32
{
	u16	len;
	u8	data[32];
} __WLAN_ATTRIB_PACK__ hfa384x_bytestr32_t;

/*--------------------------------------------------------------------
Configuration Record Structures:
	Network Parameters, Static Configuration Entities
--------------------------------------------------------------------*/
/* Prototype structure: all configuration record structures start with
these members */

typedef struct hfa384x_record
{
	u16	reclen;
	u16	rid;
} __WLAN_ATTRIB_PACK__ hfa384x_rec_t;

typedef struct hfa384x_record16
{
	u16	reclen;
	u16	rid;
	u16	val;
} __WLAN_ATTRIB_PACK__ hfa384x_rec16_t;

typedef struct hfa384x_record32
{
	u16	reclen;
	u16	rid;
	u32	val;
} __WLAN_ATTRIB_PACK__ hfa384x_rec32;

/*-- Hardware/Firmware Component Information ----------*/
typedef struct hfa384x_compident
{
	u16	id;
	u16	variant;
	u16	major;
	u16	minor;
} __WLAN_ATTRIB_PACK__ hfa384x_compident_t;

typedef struct hfa384x_caplevel
{
	u16	role;
	u16	id;
	u16	variant;
	u16	bottom;
	u16	top;
} __WLAN_ATTRIB_PACK__ hfa384x_caplevel_t;

/*-- Configuration Record: cnfPortType --*/
typedef struct hfa384x_cnfPortType
{
	u16	cnfPortType;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfPortType_t;

/*-- Configuration Record: cnfOwnMACAddress --*/
typedef struct hfa384x_cnfOwnMACAddress
{
	u8	cnfOwnMACAddress[6];
} __WLAN_ATTRIB_PACK__ hfa384x_cnfOwnMACAddress_t;

/*-- Configuration Record: cnfDesiredSSID --*/
typedef struct hfa384x_cnfDesiredSSID
{
	u8	cnfDesiredSSID[34];
} __WLAN_ATTRIB_PACK__ hfa384x_cnfDesiredSSID_t;

/*-- Configuration Record: cnfOwnChannel --*/
typedef struct hfa384x_cnfOwnChannel
{
	u16	cnfOwnChannel;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfOwnChannel_t;

/*-- Configuration Record: cnfOwnSSID --*/
typedef struct hfa384x_cnfOwnSSID
{
	u8	cnfOwnSSID[34];
} __WLAN_ATTRIB_PACK__ hfa384x_cnfOwnSSID_t;

/*-- Configuration Record: cnfOwnATIMWindow --*/
typedef struct hfa384x_cnfOwnATIMWindow
{
	u16	cnfOwnATIMWindow;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfOwnATIMWindow_t;

/*-- Configuration Record: cnfSystemScale --*/
typedef struct hfa384x_cnfSystemScale
{
	u16	cnfSystemScale;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfSystemScale_t;

/*-- Configuration Record: cnfMaxDataLength --*/
typedef struct hfa384x_cnfMaxDataLength
{
	u16	cnfMaxDataLength;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfMaxDataLength_t;

/*-- Configuration Record: cnfWDSAddress --*/
typedef struct hfa384x_cnfWDSAddress
{
	u8	cnfWDSAddress[6];
} __WLAN_ATTRIB_PACK__ hfa384x_cnfWDSAddress_t;

/*-- Configuration Record: cnfPMEnabled --*/
typedef struct hfa384x_cnfPMEnabled
{
	u16	cnfPMEnabled;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfPMEnabled_t;

/*-- Configuration Record: cnfPMEPS --*/
typedef struct hfa384x_cnfPMEPS
{
	u16	cnfPMEPS;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfPMEPS_t;

/*-- Configuration Record: cnfMulticastReceive --*/
typedef struct hfa384x_cnfMulticastReceive
{
	u16	cnfMulticastReceive;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfMulticastReceive_t;

/*-- Configuration Record: cnfAuthentication --*/
#define HFA384x_CNFAUTHENTICATION_OPENSYSTEM	0x0001
#define HFA384x_CNFAUTHENTICATION_SHAREDKEY	0x0002
#define HFA384x_CNFAUTHENTICATION_LEAP     	0x0004

/*-- Configuration Record: cnfMaxSleepDuration --*/
typedef struct hfa384x_cnfMaxSleepDuration
{
	u16	cnfMaxSleepDuration;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfMaxSleepDuration_t;

/*-- Configuration Record: cnfPMHoldoverDuration --*/
typedef struct hfa384x_cnfPMHoldoverDuration
{
	u16	cnfPMHoldoverDuration;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfPMHoldoverDuration_t;

/*-- Configuration Record: cnfOwnName --*/
typedef struct hfa384x_cnfOwnName
{
	u8	cnfOwnName[34];
} __WLAN_ATTRIB_PACK__ hfa384x_cnfOwnName_t;

/*-- Configuration Record: cnfOwnDTIMPeriod --*/
typedef struct hfa384x_cnfOwnDTIMPeriod
{
	u16	cnfOwnDTIMPeriod;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfOwnDTIMPeriod_t;

/*-- Configuration Record: cnfWDSAddress --*/
typedef struct hfa384x_cnfWDSAddressN
{
	u8	cnfWDSAddress[6];
} __WLAN_ATTRIB_PACK__ hfa384x_cnfWDSAddressN_t;

/*-- Configuration Record: cnfMulticastPMBuffering --*/
typedef struct hfa384x_cnfMulticastPMBuffering
{
	u16	cnfMulticastPMBuffering;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfMulticastPMBuffering_t;

/*--------------------------------------------------------------------
Configuration Record Structures:
	Network Parameters, Dynamic Configuration Entities
--------------------------------------------------------------------*/

/*-- Configuration Record: GroupAddresses --*/
typedef struct hfa384x_GroupAddresses
{
	u8	MACAddress[16][6];
} __WLAN_ATTRIB_PACK__ hfa384x_GroupAddresses_t;

/*-- Configuration Record: CreateIBSS --*/
typedef struct hfa384x_CreateIBSS
{
	u16	CreateIBSS;
} __WLAN_ATTRIB_PACK__ hfa384x_CreateIBSS_t;

#define HFA384x_CREATEIBSS_JOINCREATEIBSS          0
#define HFA384x_CREATEIBSS_JOINESS_JOINCREATEIBSS  1
#define HFA384x_CREATEIBSS_JOINIBSS                2
#define HFA384x_CREATEIBSS_JOINESS_JOINIBSS        3

/*-- Configuration Record: FragmentationThreshold --*/
typedef struct hfa384x_FragmentationThreshold
{
	u16	FragmentationThreshold;
} __WLAN_ATTRIB_PACK__ hfa384x_FragmentationThreshold_t;

/*-- Configuration Record: RTSThreshold --*/
typedef struct hfa384x_RTSThreshold
{
	u16	RTSThreshold;
} __WLAN_ATTRIB_PACK__ hfa384x_RTSThreshold_t;

/*-- Configuration Record: TxRateControl --*/
typedef struct hfa384x_TxRateControl
{
	u16	TxRateControl;
} __WLAN_ATTRIB_PACK__ hfa384x_TxRateControl_t;

/*-- Configuration Record: PromiscuousMode --*/
typedef struct hfa384x_PromiscuousMode
{
	u16	PromiscuousMode;
} __WLAN_ATTRIB_PACK__ hfa384x_PromiscuousMode_t;

/*-- Configuration Record: ScanRequest (data portion only) --*/
typedef struct hfa384x_ScanRequest_data
{
	u16	channelList;
	u16	txRate;
} __WLAN_ATTRIB_PACK__ hfa384x_ScanRequest_data_t;

/*-- Configuration Record: HostScanRequest (data portion only) --*/
typedef struct hfa384x_HostScanRequest_data
{
	u16	channelList;
	u16	txRate;
	hfa384x_bytestr32_t ssid;
} __WLAN_ATTRIB_PACK__ hfa384x_HostScanRequest_data_t;

/*-- Configuration Record: JoinRequest (data portion only) --*/
typedef struct hfa384x_JoinRequest_data
{
	u8	bssid[WLAN_BSSID_LEN];
	u16	channel;
} __WLAN_ATTRIB_PACK__ hfa384x_JoinRequest_data_t;

/*-- Configuration Record: authenticateStation (data portion only) --*/
typedef struct hfa384x_authenticateStation_data
{
	u8	address[WLAN_ADDR_LEN];
	u16	status;
	u16	algorithm;
} __WLAN_ATTRIB_PACK__ hfa384x_authenticateStation_data_t;

/*-- Configuration Record: associateStation (data portion only) --*/
typedef struct hfa384x_associateStation_data
{
	u8	address[WLAN_ADDR_LEN];
	u16	status;
	u16	type;
} __WLAN_ATTRIB_PACK__ hfa384x_associateStation_data_t;

/*-- Configuration Record: ChannelInfoRequest (data portion only) --*/
typedef struct hfa384x_ChannelInfoRequest_data
{
	u16	channelList;
	u16	channelDwellTime;
} __WLAN_ATTRIB_PACK__ hfa384x_ChannelInfoRequest_data_t;

/*-- Configuration Record: WEPKeyMapping (data portion only) --*/
typedef struct hfa384x_WEPKeyMapping
{
	u8	address[WLAN_ADDR_LEN];
	u16	key_index;
	u8 	key[16];
	u8 	mic_transmit_key[4];
	u8 	mic_receive_key[4];
} __WLAN_ATTRIB_PACK__ hfa384x_WEPKeyMapping_t;

/*-- Configuration Record: WPAData       (data portion only) --*/
typedef struct hfa384x_WPAData
{
	u16	datalen;
        u8 	data[0]; // max 80
} __WLAN_ATTRIB_PACK__ hfa384x_WPAData_t;

/*--------------------------------------------------------------------
Configuration Record Structures: Behavior Parameters
--------------------------------------------------------------------*/

/*-- Configuration Record: TickTime --*/
typedef struct hfa384x_TickTime
{
	u16	TickTime;
} __WLAN_ATTRIB_PACK__ hfa384x_TickTime_t;

/*--------------------------------------------------------------------
Information Record Structures: NIC Information
--------------------------------------------------------------------*/

/*-- Information Record: MaxLoadTime --*/
typedef struct hfa384x_MaxLoadTime
{
	u16	MaxLoadTime;
} __WLAN_ATTRIB_PACK__ hfa384x_MaxLoadTime_t;

/*-- Information Record: DownLoadBuffer --*/
/* NOTE: The page and offset are in AUX format */
typedef struct hfa384x_downloadbuffer
{
	u16	page;
	u16	offset;
	u16	len;
} __WLAN_ATTRIB_PACK__ hfa384x_downloadbuffer_t;

/*-- Information Record: PRIIdentity --*/
typedef struct hfa384x_PRIIdentity
{
	u16	PRICompID;
	u16	PRIVariant;
	u16	PRIMajorVersion;
	u16	PRIMinorVersion;
} __WLAN_ATTRIB_PACK__ hfa384x_PRIIdentity_t;

/*-- Information Record: PRISupRange --*/
typedef struct hfa384x_PRISupRange
{
	u16	PRIRole;
	u16	PRIID;
	u16	PRIVariant;
	u16	PRIBottom;
	u16	PRITop;
} __WLAN_ATTRIB_PACK__ hfa384x_PRISupRange_t;

/*-- Information Record: CFIActRanges --*/
typedef struct hfa384x_CFIActRanges
{
	u16	CFIRole;
	u16	CFIID;
	u16	CFIVariant;
	u16	CFIBottom;
	u16	CFITop;
} __WLAN_ATTRIB_PACK__ hfa384x_CFIActRanges_t;

/*-- Information Record: NICSerialNumber --*/
typedef struct hfa384x_NICSerialNumber
{
	u8	NICSerialNumber[12];
} __WLAN_ATTRIB_PACK__ hfa384x_NICSerialNumber_t;

/*-- Information Record: NICIdentity --*/
typedef struct hfa384x_NICIdentity
{
	u16	NICCompID;
	u16	NICVariant;
	u16	NICMajorVersion;
	u16	NICMinorVersion;
} __WLAN_ATTRIB_PACK__ hfa384x_NICIdentity_t;

/*-- Information Record: MFISupRange --*/
typedef struct hfa384x_MFISupRange
{
	u16	MFIRole;
	u16	MFIID;
	u16	MFIVariant;
	u16	MFIBottom;
	u16	MFITop;
} __WLAN_ATTRIB_PACK__ hfa384x_MFISupRange_t;

/*-- Information Record: CFISupRange --*/
typedef struct hfa384x_CFISupRange
{
	u16	CFIRole;
	u16	CFIID;
	u16	CFIVariant;
	u16	CFIBottom;
	u16	CFITop;
} __WLAN_ATTRIB_PACK__ hfa384x_CFISupRange_t;

/*-- Information Record: BUILDSEQ:BuildSeq --*/
typedef struct hfa384x_BuildSeq {
	u16	primary;
	u16	secondary;
} __WLAN_ATTRIB_PACK__ hfa384x_BuildSeq_t;

/*-- Information Record: FWID --*/
#define HFA384x_FWID_LEN	14
typedef struct hfa384x_FWID {
	u8	primary[HFA384x_FWID_LEN];
	u8	secondary[HFA384x_FWID_LEN];
} __WLAN_ATTRIB_PACK__ hfa384x_FWID_t;

/*-- Information Record: ChannelList --*/
typedef struct hfa384x_ChannelList
{
	u16	ChannelList;
} __WLAN_ATTRIB_PACK__ hfa384x_ChannelList_t;

/*-- Information Record: RegulatoryDomains --*/
typedef struct hfa384x_RegulatoryDomains
{
	u8	RegulatoryDomains[12];
} __WLAN_ATTRIB_PACK__ hfa384x_RegulatoryDomains_t;

/*-- Information Record: TempType --*/
typedef struct hfa384x_TempType
{
	u16	TempType;
} __WLAN_ATTRIB_PACK__ hfa384x_TempType_t;

/*-- Information Record: CIS --*/
typedef struct hfa384x_CIS
{
	u8	CIS[480];
} __WLAN_ATTRIB_PACK__ hfa384x_CIS_t;

/*-- Information Record: STAIdentity --*/
typedef struct hfa384x_STAIdentity
{
	u16	STACompID;
	u16	STAVariant;
	u16	STAMajorVersion;
	u16	STAMinorVersion;
} __WLAN_ATTRIB_PACK__ hfa384x_STAIdentity_t;

/*-- Information Record: STASupRange --*/
typedef struct hfa384x_STASupRange
{
	u16	STARole;
	u16	STAID;
	u16	STAVariant;
	u16	STABottom;
	u16	STATop;
} __WLAN_ATTRIB_PACK__ hfa384x_STASupRange_t;

/*-- Information Record: MFIActRanges --*/
typedef struct hfa384x_MFIActRanges
{
	u16	MFIRole;
	u16	MFIID;
	u16	MFIVariant;
	u16	MFIBottom;
	u16	MFITop;
} __WLAN_ATTRIB_PACK__ hfa384x_MFIActRanges_t;

/*--------------------------------------------------------------------
Information Record Structures: NIC Information
--------------------------------------------------------------------*/

/*-- Information Record: PortStatus --*/
typedef struct hfa384x_PortStatus
{
	u16	PortStatus;
} __WLAN_ATTRIB_PACK__ hfa384x_PortStatus_t;

#define HFA384x_PSTATUS_DISABLED	((u16)1)
#define HFA384x_PSTATUS_SEARCHING	((u16)2)
#define HFA384x_PSTATUS_CONN_IBSS	((u16)3)
#define HFA384x_PSTATUS_CONN_ESS	((u16)4)
#define HFA384x_PSTATUS_OUTOFRANGE	((u16)5)
#define HFA384x_PSTATUS_CONN_WDS	((u16)6)

/*-- Information Record: CurrentSSID --*/
typedef struct hfa384x_CurrentSSID
{
	u8	CurrentSSID[34];
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentSSID_t;

/*-- Information Record: CurrentBSSID --*/
typedef struct hfa384x_CurrentBSSID
{
	u8	CurrentBSSID[6];
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentBSSID_t;

/*-- Information Record: commsquality --*/
typedef struct hfa384x_commsquality
{
	u16	CQ_currBSS;
	u16	ASL_currBSS;
	u16	ANL_currFC;
} __WLAN_ATTRIB_PACK__ hfa384x_commsquality_t;

/*-- Information Record: dmbcommsquality --*/
typedef struct hfa384x_dbmcommsquality
{
	u16	CQdbm_currBSS;
	u16	ASLdbm_currBSS;
	u16	ANLdbm_currFC;
} __WLAN_ATTRIB_PACK__ hfa384x_dbmcommsquality_t;

/*-- Information Record: CurrentTxRate --*/
typedef struct hfa384x_CurrentTxRate
{
	u16	CurrentTxRate;
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentTxRate_t;

/*-- Information Record: CurrentBeaconInterval --*/
typedef struct hfa384x_CurrentBeaconInterval
{
	u16	CurrentBeaconInterval;
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentBeaconInterval_t;

/*-- Information Record: CurrentScaleThresholds --*/
typedef struct hfa384x_CurrentScaleThresholds
{
	u16	EnergyDetectThreshold;
	u16	CarrierDetectThreshold;
	u16	DeferDetectThreshold;
	u16	CellSearchThreshold; /* Stations only */
	u16	DeadSpotThreshold; /* Stations only */
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentScaleThresholds_t;

/*-- Information Record: ProtocolRspTime --*/
typedef struct hfa384x_ProtocolRspTime
{
	u16	ProtocolRspTime;
} __WLAN_ATTRIB_PACK__ hfa384x_ProtocolRspTime_t;

/*-- Information Record: ShortRetryLimit --*/
typedef struct hfa384x_ShortRetryLimit
{
	u16	ShortRetryLimit;
} __WLAN_ATTRIB_PACK__ hfa384x_ShortRetryLimit_t;

/*-- Information Record: LongRetryLimit --*/
typedef struct hfa384x_LongRetryLimit
{
	u16	LongRetryLimit;
} __WLAN_ATTRIB_PACK__ hfa384x_LongRetryLimit_t;

/*-- Information Record: MaxTransmitLifetime --*/
typedef struct hfa384x_MaxTransmitLifetime
{
	u16	MaxTransmitLifetime;
} __WLAN_ATTRIB_PACK__ hfa384x_MaxTransmitLifetime_t;

/*-- Information Record: MaxReceiveLifetime --*/
typedef struct hfa384x_MaxReceiveLifetime
{
	u16	MaxReceiveLifetime;
} __WLAN_ATTRIB_PACK__ hfa384x_MaxReceiveLifetime_t;

/*-- Information Record: CFPollable --*/
typedef struct hfa384x_CFPollable
{
	u16	CFPollable;
} __WLAN_ATTRIB_PACK__ hfa384x_CFPollable_t;

/*-- Information Record: AuthenticationAlgorithms --*/
typedef struct hfa384x_AuthenticationAlgorithms
{
	u16	AuthenticationType;
	u16	TypeEnabled;
} __WLAN_ATTRIB_PACK__ hfa384x_AuthenticationAlgorithms_t;

/*-- Information Record: AuthenticationAlgorithms
(data only --*/
typedef struct hfa384x_AuthenticationAlgorithms_data
{
	u16	AuthenticationType;
	u16	TypeEnabled;
} __WLAN_ATTRIB_PACK__ hfa384x_AuthenticationAlgorithms_data_t;

/*-- Information Record: PrivacyOptionImplemented --*/
typedef struct hfa384x_PrivacyOptionImplemented
{
	u16	PrivacyOptionImplemented;
} __WLAN_ATTRIB_PACK__ hfa384x_PrivacyOptionImplemented_t;

/*-- Information Record: OwnMACAddress --*/
typedef struct hfa384x_OwnMACAddress
{
	u8	OwnMACAddress[6];
} __WLAN_ATTRIB_PACK__ hfa384x_OwnMACAddress_t;

/*-- Information Record: PCFInfo --*/
typedef struct hfa384x_PCFInfo
{
	u16	MediumOccupancyLimit;
	u16	CFPPeriod;
	u16	CFPMaxDuration;
	u16	CFPFlags;
} __WLAN_ATTRIB_PACK__ hfa384x_PCFInfo_t;

/*-- Information Record: PCFInfo (data portion only) --*/
typedef struct hfa384x_PCFInfo_data
{
	u16	MediumOccupancyLimit;
	u16	CFPPeriod;
	u16	CFPMaxDuration;
	u16	CFPFlags;
} __WLAN_ATTRIB_PACK__ hfa384x_PCFInfo_data_t;

/*--------------------------------------------------------------------
Information Record Structures: Modem Information Records
--------------------------------------------------------------------*/

/*-- Information Record: PHYType --*/
typedef struct hfa384x_PHYType
{
	u16	PHYType;
} __WLAN_ATTRIB_PACK__ hfa384x_PHYType_t;

/*-- Information Record: CurrentChannel --*/
typedef struct hfa384x_CurrentChannel
{
	u16	CurrentChannel;
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentChannel_t;

/*-- Information Record: CurrentPowerState --*/
typedef struct hfa384x_CurrentPowerState
{
	u16	CurrentPowerState;
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentPowerState_t;

/*-- Information Record: CCAMode --*/
typedef struct hfa384x_CCAMode
{
	u16	CCAMode;
} __WLAN_ATTRIB_PACK__ hfa384x_CCAMode_t;

/*-- Information Record: SupportedDataRates --*/
typedef struct hfa384x_SupportedDataRates
{
	u8	SupportedDataRates[10];
} __WLAN_ATTRIB_PACK__ hfa384x_SupportedDataRates_t;

/*-- Information Record: LFOStatus --*/
typedef struct hfa384x_LFOStatus
{
	u16  TestResults;
	u16  LFOResult;
	u16  VRHFOResult;
} __WLAN_ATTRIB_PACK__ hfa384x_LFOStatus_t;

#define HFA384x_TESTRESULT_ALLPASSED    BIT0
#define HFA384x_TESTRESULT_LFO_FAIL     BIT1
#define HFA384x_TESTRESULT_VR_HF0_FAIL  BIT2
#define HFA384x_HOST_FIRM_COORDINATE    BIT7
#define HFA384x_TESTRESULT_COORDINATE   BIT15

/*-- Information Record: LEDControl --*/
typedef struct hfa384x_LEDControl
{
	u16  searching_on;
	u16  searching_off;
	u16  assoc_on;
	u16  assoc_off;
	u16  activity;
} __WLAN_ATTRIB_PACK__ hfa384x_LEDControl_t;

/*--------------------------------------------------------------------
                 FRAME DESCRIPTORS AND FRAME STRUCTURES

FRAME DESCRIPTORS: Offsets

----------------------------------------------------------------------
Control Info (offset 44-51)
--------------------------------------------------------------------*/
#define		HFA384x_FD_STATUS_OFF			((u16)0x44)
#define		HFA384x_FD_TIME_OFF			((u16)0x46)
#define		HFA384x_FD_SWSUPPORT_OFF		((u16)0x4A)
#define		HFA384x_FD_SILENCE_OFF			((u16)0x4A)
#define		HFA384x_FD_SIGNAL_OFF			((u16)0x4B)
#define		HFA384x_FD_RATE_OFF			((u16)0x4C)
#define		HFA384x_FD_RXFLOW_OFF			((u16)0x4D)
#define		HFA384x_FD_RESERVED_OFF			((u16)0x4E)
#define		HFA384x_FD_TXCONTROL_OFF		((u16)0x50)
/*--------------------------------------------------------------------
802.11 Header (offset 52-6B)
--------------------------------------------------------------------*/
#define		HFA384x_FD_FRAMECONTROL_OFF		((u16)0x52)
#define		HFA384x_FD_DURATIONID_OFF		((u16)0x54)
#define		HFA384x_FD_ADDRESS1_OFF			((u16)0x56)
#define		HFA384x_FD_ADDRESS2_OFF			((u16)0x5C)
#define		HFA384x_FD_ADDRESS3_OFF			((u16)0x62)
#define		HFA384x_FD_SEQCONTROL_OFF		((u16)0x68)
#define		HFA384x_FD_ADDRESS4_OFF			((u16)0x6A)
#define		HFA384x_FD_DATALEN_OFF			((u16)0x70)
/*--------------------------------------------------------------------
802.3 Header (offset 72-7F)
--------------------------------------------------------------------*/
#define		HFA384x_FD_DESTADDRESS_OFF		((u16)0x72)
#define		HFA384x_FD_SRCADDRESS_OFF		((u16)0x78)
#define		HFA384x_FD_DATALENGTH_OFF		((u16)0x7E)

/*--------------------------------------------------------------------
FRAME STRUCTURES: Communication Frames
----------------------------------------------------------------------
Communication Frames: Transmit Frames
--------------------------------------------------------------------*/
/*-- Communication Frame: Transmit Frame Structure --*/
typedef struct hfa384x_tx_frame
{
	u16	status;
	u16	reserved1;
	u16	reserved2;
	u32	sw_support;
	u8	tx_retrycount;
	u8   tx_rate;
	u16	tx_control;

	/*-- 802.11 Header Information --*/

	u16	frame_control;
	u16	duration_id;
	u8	address1[6];
	u8	address2[6];
	u8	address3[6];
	u16	sequence_control;
	u8	address4[6];
	u16	data_len; /* little endian format */

	/*-- 802.3 Header Information --*/

	u8	dest_addr[6];
	u8	src_addr[6];
	u16	data_length; /* big endian format */
} __WLAN_ATTRIB_PACK__ hfa384x_tx_frame_t;
/*--------------------------------------------------------------------
Communication Frames: Field Masks for Transmit Frames
--------------------------------------------------------------------*/
/*-- Status Field --*/
#define		HFA384x_TXSTATUS_ACKERR			((u16)BIT5)
#define		HFA384x_TXSTATUS_FORMERR		((u16)BIT3)
#define		HFA384x_TXSTATUS_DISCON			((u16)BIT2)
#define		HFA384x_TXSTATUS_AGEDERR		((u16)BIT1)
#define		HFA384x_TXSTATUS_RETRYERR		((u16)BIT0)
/*-- Transmit Control Field --*/
#define		HFA384x_TX_CFPOLL			((u16)BIT12)
#define		HFA384x_TX_PRST				((u16)BIT11)
#define		HFA384x_TX_MACPORT			((u16)(BIT10 | BIT9 | BIT8))
#define		HFA384x_TX_NOENCRYPT			((u16)BIT7)
#define		HFA384x_TX_RETRYSTRAT			((u16)(BIT6 | BIT5))
#define		HFA384x_TX_STRUCTYPE			((u16)(BIT4 | BIT3))
#define		HFA384x_TX_TXEX				((u16)BIT2)
#define		HFA384x_TX_TXOK				((u16)BIT1)
/*--------------------------------------------------------------------
Communication Frames: Test/Get/Set Field Values for Transmit Frames
--------------------------------------------------------------------*/
/*-- Status Field --*/
#define HFA384x_TXSTATUS_ISERROR(v)	\
	(((u16)(v))&\
	(HFA384x_TXSTATUS_ACKERR|HFA384x_TXSTATUS_FORMERR|\
	HFA384x_TXSTATUS_DISCON|HFA384x_TXSTATUS_AGEDERR|\
	HFA384x_TXSTATUS_RETRYERR))

#define	HFA384x_TXSTATUS_ISACKERR(v)	((u16)(((u16)(v)) & HFA384x_TXSTATUS_ACKERR))
#define	HFA384x_TXSTATUS_ISFORMERR(v)	((u16)(((u16)(v)) & HFA384x_TXSTATUS_FORMERR))
#define	HFA384x_TXSTATUS_ISDISCON(v)	((u16)(((u16)(v)) & HFA384x_TXSTATUS_DISCON))
#define	HFA384x_TXSTATUS_ISAGEDERR(v)	((u16)(((u16)(v)) & HFA384x_TXSTATUS_AGEDERR))
#define	HFA384x_TXSTATUS_ISRETRYERR(v)	((u16)(((u16)(v)) & HFA384x_TXSTATUS_RETRYERR))

#define	HFA384x_TX_GET(v,m,s)		((((u16)(v))&((u16)(m)))>>((u16)(s)))
#define	HFA384x_TX_SET(v,m,s)		((((u16)(v))<<((u16)(s)))&((u16)(m)))

#define	HFA384x_TX_CFPOLL_GET(v)	HFA384x_TX_GET(v, HFA384x_TX_CFPOLL,12)
#define	HFA384x_TX_CFPOLL_SET(v)	HFA384x_TX_SET(v, HFA384x_TX_CFPOLL,12)
#define	HFA384x_TX_PRST_GET(v)		HFA384x_TX_GET(v, HFA384x_TX_PRST,11)
#define	HFA384x_TX_PRST_SET(v)		HFA384x_TX_SET(v, HFA384x_TX_PRST,11)
#define	HFA384x_TX_MACPORT_GET(v)	HFA384x_TX_GET(v, HFA384x_TX_MACPORT, 8)
#define	HFA384x_TX_MACPORT_SET(v)	HFA384x_TX_SET(v, HFA384x_TX_MACPORT, 8)
#define	HFA384x_TX_NOENCRYPT_GET(v)	HFA384x_TX_GET(v, HFA384x_TX_NOENCRYPT, 7)
#define	HFA384x_TX_NOENCRYPT_SET(v)	HFA384x_TX_SET(v, HFA384x_TX_NOENCRYPT, 7)
#define	HFA384x_TX_RETRYSTRAT_GET(v)	HFA384x_TX_GET(v, HFA384x_TX_RETRYSTRAT, 5)
#define	HFA384x_TX_RETRYSTRAT_SET(v)	HFA384x_TX_SET(v, HFA384x_TX_RETRYSTRAT, 5)
#define	HFA384x_TX_STRUCTYPE_GET(v)	HFA384x_TX_GET(v, HFA384x_TX_STRUCTYPE, 3)
#define	HFA384x_TX_STRUCTYPE_SET(v)	HFA384x_TX_SET(v, HFA384x_TX_STRUCTYPE, 3)
#define	HFA384x_TX_TXEX_GET(v)		HFA384x_TX_GET(v, HFA384x_TX_TXEX, 2)
#define	HFA384x_TX_TXEX_SET(v)		HFA384x_TX_SET(v, HFA384x_TX_TXEX, 2)
#define	HFA384x_TX_TXOK_GET(v)		HFA384x_TX_GET(v, HFA384x_TX_TXOK, 1)
#define	HFA384x_TX_TXOK_SET(v)		HFA384x_TX_SET(v, HFA384x_TX_TXOK, 1)
/*--------------------------------------------------------------------
Communication Frames: Receive Frames
--------------------------------------------------------------------*/
/*-- Communication Frame: Receive Frame Structure --*/
typedef struct hfa384x_rx_frame
{
	/*-- MAC rx descriptor (hfa384x byte order) --*/
	u16	status;
	u32	time;
	u8	silence;
	u8	signal;
	u8	rate;
	u8	rx_flow;
	u16	reserved1;
	u16	reserved2;

	/*-- 802.11 Header Information (802.11 byte order) --*/
	u16	frame_control;
	u16	duration_id;
	u8	address1[6];
	u8	address2[6];
	u8	address3[6];
	u16	sequence_control;
	u8	address4[6];
	u16	data_len; /* hfa384x (little endian) format */

	/*-- 802.3 Header Information --*/
	u8	dest_addr[6];
	u8	src_addr[6];
	u16	data_length; /* IEEE? (big endian) format */
} __WLAN_ATTRIB_PACK__ hfa384x_rx_frame_t;
/*--------------------------------------------------------------------
Communication Frames: Field Masks for Receive Frames
--------------------------------------------------------------------*/
/*-- Offsets --------*/
#define		HFA384x_RX_DATA_LEN_OFF			((u16)44)
#define		HFA384x_RX_80211HDR_OFF			((u16)14)
#define		HFA384x_RX_DATA_OFF			((u16)60)

/*-- Status Fields --*/
#define		HFA384x_RXSTATUS_MSGTYPE		((u16)(BIT15 | BIT14 | BIT13))
#define		HFA384x_RXSTATUS_MACPORT		((u16)(BIT10 | BIT9 | BIT8))
#define		HFA384x_RXSTATUS_UNDECR			((u16)BIT1)
#define		HFA384x_RXSTATUS_FCSERR			((u16)BIT0)
/*--------------------------------------------------------------------
Communication Frames: Test/Get/Set Field Values for Receive Frames
--------------------------------------------------------------------*/
#define		HFA384x_RXSTATUS_MSGTYPE_GET(value)	((u16)((((u16)(value)) & HFA384x_RXSTATUS_MSGTYPE) >> 13))
#define		HFA384x_RXSTATUS_MSGTYPE_SET(value)	((u16)(((u16)(value)) << 13))
#define		HFA384x_RXSTATUS_MACPORT_GET(value)	((u16)((((u16)(value)) & HFA384x_RXSTATUS_MACPORT) >> 8))
#define		HFA384x_RXSTATUS_MACPORT_SET(value)	((u16)(((u16)(value)) << 8))
#define		HFA384x_RXSTATUS_ISUNDECR(value)	((u16)(((u16)(value)) & HFA384x_RXSTATUS_UNDECR))
#define		HFA384x_RXSTATUS_ISFCSERR(value)	((u16)(((u16)(value)) & HFA384x_RXSTATUS_FCSERR))
/*--------------------------------------------------------------------
 FRAME STRUCTURES: Information Types and Information Frame Structures
----------------------------------------------------------------------
Information Types
--------------------------------------------------------------------*/
#define		HFA384x_IT_HANDOVERADDR			((u16)0xF000UL)
#define		HFA384x_IT_HANDOVERDEAUTHADDRESS	((u16)0xF001UL)//AP 1.3.7
#define		HFA384x_IT_COMMTALLIES			((u16)0xF100UL)
#define		HFA384x_IT_SCANRESULTS			((u16)0xF101UL)
#define		HFA384x_IT_CHINFORESULTS		((u16)0xF102UL)
#define		HFA384x_IT_HOSTSCANRESULTS		((u16)0xF103UL)
#define		HFA384x_IT_LINKSTATUS			((u16)0xF200UL)
#define		HFA384x_IT_ASSOCSTATUS			((u16)0xF201UL)
#define		HFA384x_IT_AUTHREQ			((u16)0xF202UL)
#define		HFA384x_IT_PSUSERCNT			((u16)0xF203UL)
#define		HFA384x_IT_KEYIDCHANGED			((u16)0xF204UL)
#define		HFA384x_IT_ASSOCREQ    			((u16)0xF205UL)
#define		HFA384x_IT_MICFAILURE  			((u16)0xF206UL)

/*--------------------------------------------------------------------
Information Frames Structures
----------------------------------------------------------------------
Information Frames: Notification Frame Structures
--------------------------------------------------------------------*/
/*--  Notification Frame,MAC Mgmt: Handover Address --*/
typedef struct hfa384x_HandoverAddr
{
	u16	framelen;
	u16	infotype;
	u8	handover_addr[WLAN_BSSID_LEN];
} __WLAN_ATTRIB_PACK__ hfa384x_HandoverAddr_t;

/*--  Inquiry Frame, Diagnose: Communication Tallies --*/
typedef struct hfa384x_CommTallies16
{
	u16	txunicastframes;
	u16	txmulticastframes;
	u16	txfragments;
	u16	txunicastoctets;
	u16	txmulticastoctets;
	u16	txdeferredtrans;
	u16	txsingleretryframes;
	u16	txmultipleretryframes;
	u16	txretrylimitexceeded;
	u16	txdiscards;
	u16	rxunicastframes;
	u16	rxmulticastframes;
	u16	rxfragments;
	u16	rxunicastoctets;
	u16	rxmulticastoctets;
	u16	rxfcserrors;
	u16	rxdiscardsnobuffer;
	u16	txdiscardswrongsa;
	u16	rxdiscardswepundecr;
	u16	rxmsginmsgfrag;
	u16	rxmsginbadmsgfrag;
} __WLAN_ATTRIB_PACK__ hfa384x_CommTallies16_t;

typedef struct hfa384x_CommTallies32
{
	u32	txunicastframes;
	u32	txmulticastframes;
	u32	txfragments;
	u32	txunicastoctets;
	u32	txmulticastoctets;
	u32	txdeferredtrans;
	u32	txsingleretryframes;
	u32	txmultipleretryframes;
	u32	txretrylimitexceeded;
	u32	txdiscards;
	u32	rxunicastframes;
	u32	rxmulticastframes;
	u32	rxfragments;
	u32	rxunicastoctets;
	u32	rxmulticastoctets;
	u32	rxfcserrors;
	u32	rxdiscardsnobuffer;
	u32	txdiscardswrongsa;
	u32	rxdiscardswepundecr;
	u32	rxmsginmsgfrag;
	u32	rxmsginbadmsgfrag;
} __WLAN_ATTRIB_PACK__ hfa384x_CommTallies32_t;

/*--  Inquiry Frame, Diagnose: Scan Results & Subfields--*/
typedef struct hfa384x_ScanResultSub
{
	u16	chid;
	u16	anl;
	u16	sl;
	u8	bssid[WLAN_BSSID_LEN];
	u16	bcnint;
	u16	capinfo;
	hfa384x_bytestr32_t	ssid;
	u8	supprates[10]; /* 802.11 info element */
	u16	proberesp_rate;
} __WLAN_ATTRIB_PACK__ hfa384x_ScanResultSub_t;

typedef struct hfa384x_ScanResult
{
	u16	rsvd;
	u16	scanreason;
	hfa384x_ScanResultSub_t
		result[HFA384x_SCANRESULT_MAX];
} __WLAN_ATTRIB_PACK__ hfa384x_ScanResult_t;

/*--  Inquiry Frame, Diagnose: ChInfo Results & Subfields--*/
typedef struct hfa384x_ChInfoResultSub
{
	u16	chid;
	u16	anl;
	u16	pnl;
	u16	active;
} __WLAN_ATTRIB_PACK__ hfa384x_ChInfoResultSub_t;

#define HFA384x_CHINFORESULT_BSSACTIVE	BIT0
#define HFA384x_CHINFORESULT_PCFACTIVE	BIT1

typedef struct hfa384x_ChInfoResult
{
	u16	scanchannels;
	hfa384x_ChInfoResultSub_t
		result[HFA384x_CHINFORESULT_MAX];
} __WLAN_ATTRIB_PACK__ hfa384x_ChInfoResult_t;

/*--  Inquiry Frame, Diagnose: Host Scan Results & Subfields--*/
typedef struct hfa384x_HScanResultSub
{
	u16	chid;
	u16	anl;
	u16	sl;
	u8	bssid[WLAN_BSSID_LEN];
	u16	bcnint;
	u16	capinfo;
	hfa384x_bytestr32_t	ssid;
	u8	supprates[10]; /* 802.11 info element */
	u16	proberesp_rate;
	u16	atim;
} __WLAN_ATTRIB_PACK__ hfa384x_HScanResultSub_t;

typedef struct hfa384x_HScanResult
{
	u16	nresult;
	u16	rsvd;
	hfa384x_HScanResultSub_t
		result[HFA384x_HSCANRESULT_MAX];
} __WLAN_ATTRIB_PACK__ hfa384x_HScanResult_t;

/*--  Unsolicited Frame, MAC Mgmt: LinkStatus --*/

#define HFA384x_LINK_NOTCONNECTED	((u16)0)
#define HFA384x_LINK_CONNECTED		((u16)1)
#define HFA384x_LINK_DISCONNECTED	((u16)2)
#define HFA384x_LINK_AP_CHANGE		((u16)3)
#define HFA384x_LINK_AP_OUTOFRANGE	((u16)4)
#define HFA384x_LINK_AP_INRANGE		((u16)5)
#define HFA384x_LINK_ASSOCFAIL		((u16)6)

typedef struct hfa384x_LinkStatus
{
	u16	linkstatus;
} __WLAN_ATTRIB_PACK__ hfa384x_LinkStatus_t;


/*--  Unsolicited Frame, MAC Mgmt: AssociationStatus (--*/

#define HFA384x_ASSOCSTATUS_STAASSOC	((u16)1)
#define HFA384x_ASSOCSTATUS_REASSOC	((u16)2)
#define HFA384x_ASSOCSTATUS_DISASSOC	((u16)3)
#define HFA384x_ASSOCSTATUS_ASSOCFAIL	((u16)4)
#define HFA384x_ASSOCSTATUS_AUTHFAIL	((u16)5)

typedef struct hfa384x_AssocStatus
{
	u16	assocstatus;
	u8	sta_addr[WLAN_ADDR_LEN];
	/* old_ap_addr is only valid if assocstatus == 2 */
	u8	old_ap_addr[WLAN_ADDR_LEN];
	u16	reason;
	u16	reserved;
} __WLAN_ATTRIB_PACK__ hfa384x_AssocStatus_t;

/*--  Unsolicited Frame, MAC Mgmt: AuthRequest (AP Only) --*/

typedef struct hfa384x_AuthRequest
{
	u8	sta_addr[WLAN_ADDR_LEN];
	u16	algorithm;
} __WLAN_ATTRIB_PACK__ hfa384x_AuthReq_t;

/*--  Unsolicited Frame, MAC Mgmt: AssocRequest (AP Only) --*/

typedef struct hfa384x_AssocRequest
{
	u8	sta_addr[WLAN_ADDR_LEN];
	u16	type;
	u8   wpa_data[80];
} __WLAN_ATTRIB_PACK__ hfa384x_AssocReq_t;


#define HFA384x_ASSOCREQ_TYPE_ASSOC     0
#define HFA384x_ASSOCREQ_TYPE_REASSOC   1

/*--  Unsolicited Frame, MAC Mgmt: MIC Failure  (AP Only) --*/

typedef struct hfa384x_MicFailure
{
	u8	sender[WLAN_ADDR_LEN];
	u8	dest[WLAN_ADDR_LEN];
} __WLAN_ATTRIB_PACK__ hfa384x_MicFailure_t;

/*--  Unsolicited Frame, MAC Mgmt: PSUserCount (AP Only) --*/

typedef struct hfa384x_PSUserCount
{
	u16	usercnt;
} __WLAN_ATTRIB_PACK__ hfa384x_PSUserCount_t;

typedef struct hfa384x_KeyIDChanged
{
	u8	sta_addr[WLAN_ADDR_LEN];
	u16	keyid;
} __WLAN_ATTRIB_PACK__ hfa384x_KeyIDChanged_t;

/*--  Collection of all Inf frames ---------------*/
typedef union hfa384x_infodata {
	hfa384x_CommTallies16_t	commtallies16;
	hfa384x_CommTallies32_t	commtallies32;
	hfa384x_ScanResult_t	scanresult;
	hfa384x_ChInfoResult_t	chinforesult;
	hfa384x_HScanResult_t	hscanresult;
	hfa384x_LinkStatus_t	linkstatus;
	hfa384x_AssocStatus_t	assocstatus;
	hfa384x_AuthReq_t	authreq;
	hfa384x_PSUserCount_t	psusercnt;
	hfa384x_KeyIDChanged_t  keyidchanged;
} __WLAN_ATTRIB_PACK__ hfa384x_infodata_t;

typedef struct hfa384x_InfFrame
{
	u16			framelen;
	u16			infotype;
	hfa384x_infodata_t	info;
} __WLAN_ATTRIB_PACK__ hfa384x_InfFrame_t;

/*--------------------------------------------------------------------
USB Packet structures and constants.
--------------------------------------------------------------------*/

/* Should be sent to the ctrlout endpoint */
#define HFA384x_USB_ENBULKIN	6

/* Should be sent to the bulkout endpoint */
#define HFA384x_USB_TXFRM	0
#define HFA384x_USB_CMDREQ	1
#define HFA384x_USB_WRIDREQ	2
#define HFA384x_USB_RRIDREQ	3
#define HFA384x_USB_WMEMREQ	4
#define HFA384x_USB_RMEMREQ	5

/* Received from the bulkin endpoint */
#define HFA384x_USB_ISFRM(a)	(!((a) & 0x8000))
#define HFA384x_USB_ISTXFRM(a)	(((a) & 0x9000) == 0x1000)
#define HFA384x_USB_ISRXFRM(a)	(!((a) & 0x9000))
#define HFA384x_USB_INFOFRM	0x8000
#define HFA384x_USB_CMDRESP	0x8001
#define HFA384x_USB_WRIDRESP	0x8002
#define HFA384x_USB_RRIDRESP	0x8003
#define HFA384x_USB_WMEMRESP	0x8004
#define HFA384x_USB_RMEMRESP	0x8005
#define HFA384x_USB_BUFAVAIL	0x8006
#define HFA384x_USB_ERROR	0x8007

/*------------------------------------*/
/* Request (bulk OUT) packet contents */

typedef struct hfa384x_usb_txfrm {
	hfa384x_tx_frame_t	desc;
	u8			data[WLAN_DATA_MAXLEN];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_txfrm_t;

typedef struct hfa384x_usb_cmdreq {
	u16		type;
	u16		cmd;
	u16		parm0;
	u16		parm1;
	u16		parm2;
	u8		pad[54];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_cmdreq_t;

typedef struct hfa384x_usb_wridreq {
	u16		type;
	u16		frmlen;
	u16		rid;
	u8		data[HFA384x_RIDDATA_MAXLEN];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_wridreq_t;

typedef struct hfa384x_usb_rridreq {
	u16		type;
	u16		frmlen;
	u16		rid;
	u8		pad[58];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_rridreq_t;

typedef struct hfa384x_usb_wmemreq {
	u16		type;
	u16		frmlen;
	u16		offset;
	u16		page;
	u8		data[HFA384x_USB_RWMEM_MAXLEN];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_wmemreq_t;

typedef struct hfa384x_usb_rmemreq {
	u16		type;
	u16		frmlen;
	u16		offset;
	u16		page;
	u8		pad[56];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_rmemreq_t;

/*------------------------------------*/
/* Response (bulk IN) packet contents */

typedef struct hfa384x_usb_rxfrm {
	hfa384x_rx_frame_t	desc;
	u8			data[WLAN_DATA_MAXLEN];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_rxfrm_t;

typedef struct hfa384x_usb_infofrm {
	u16			type;
	hfa384x_InfFrame_t	info;
} __WLAN_ATTRIB_PACK__ hfa384x_usb_infofrm_t;

typedef struct hfa384x_usb_statusresp {
	u16		type;
	u16		status;
	u16		resp0;
	u16		resp1;
	u16		resp2;
} __WLAN_ATTRIB_PACK__ hfa384x_usb_cmdresp_t;

typedef hfa384x_usb_cmdresp_t hfa384x_usb_wridresp_t;

typedef struct hfa384x_usb_rridresp {
	u16		type;
	u16		frmlen;
	u16		rid;
	u8		data[HFA384x_RIDDATA_MAXLEN];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_rridresp_t;

typedef hfa384x_usb_cmdresp_t hfa384x_usb_wmemresp_t;

typedef struct hfa384x_usb_rmemresp {
	u16		type;
	u16		frmlen;
	u8		data[HFA384x_USB_RWMEM_MAXLEN];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_rmemresp_t;

typedef struct hfa384x_usb_bufavail {
	u16		type;
	u16		frmlen;
} __WLAN_ATTRIB_PACK__ hfa384x_usb_bufavail_t;

typedef struct hfa384x_usb_error {
	u16		type;
	u16		errortype;
} __WLAN_ATTRIB_PACK__ hfa384x_usb_error_t;

/*----------------------------------------------------------*/
/* Unions for packaging all the known packet types together */

typedef union hfa384x_usbout {
	u16			type;
	hfa384x_usb_txfrm_t	txfrm;
	hfa384x_usb_cmdreq_t	cmdreq;
	hfa384x_usb_wridreq_t	wridreq;
	hfa384x_usb_rridreq_t	rridreq;
	hfa384x_usb_wmemreq_t	wmemreq;
	hfa384x_usb_rmemreq_t	rmemreq;
} __WLAN_ATTRIB_PACK__ hfa384x_usbout_t;

typedef union hfa384x_usbin {
	u16			type;
	hfa384x_usb_rxfrm_t	rxfrm;
	hfa384x_usb_txfrm_t	txfrm;
	hfa384x_usb_infofrm_t	infofrm;
	hfa384x_usb_cmdresp_t	cmdresp;
	hfa384x_usb_wridresp_t	wridresp;
	hfa384x_usb_rridresp_t	rridresp;
	hfa384x_usb_wmemresp_t	wmemresp;
	hfa384x_usb_rmemresp_t	rmemresp;
	hfa384x_usb_bufavail_t	bufavail;
	hfa384x_usb_error_t	usberror;
	u8			boguspad[3000];
} __WLAN_ATTRIB_PACK__ hfa384x_usbin_t;

/*--------------------------------------------------------------------
PD record structures.
--------------------------------------------------------------------*/

typedef struct hfa384x_pdr_pcb_partnum
{
	u8	num[8];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_pcb_partnum_t;

typedef struct hfa384x_pdr_pcb_tracenum
{
	u8	num[8];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_pcb_tracenum_t;

typedef struct hfa384x_pdr_nic_serial
{
	u8	num[12];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_nic_serial_t;

typedef struct hfa384x_pdr_mkk_measurements
{
	double	carrier_freq;
	double	occupied_band;
	double	power_density;
	double	tx_spur_f1;
	double	tx_spur_f2;
	double	tx_spur_f3;
	double	tx_spur_f4;
	double	tx_spur_l1;
	double	tx_spur_l2;
	double	tx_spur_l3;
	double	tx_spur_l4;
	double	rx_spur_f1;
	double	rx_spur_f2;
	double	rx_spur_l1;
	double	rx_spur_l2;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_mkk_measurements_t;

typedef struct hfa384x_pdr_nic_ramsize
{
	u8	size[12]; /* units of KB */
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_nic_ramsize_t;

typedef struct hfa384x_pdr_mfisuprange
{
	u16	id;
	u16	variant;
	u16	bottom;
	u16	top;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_mfisuprange_t;

typedef struct hfa384x_pdr_cfisuprange
{
	u16	id;
	u16	variant;
	u16	bottom;
	u16	top;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_cfisuprange_t;

typedef struct hfa384x_pdr_nicid
{
	u16	id;
	u16	variant;
	u16	major;
	u16	minor;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_nicid_t;


typedef struct hfa384x_pdr_refdac_measurements
{
	u16	value[0];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_refdac_measurements_t;

typedef struct hfa384x_pdr_vgdac_measurements
{
	u16	value[0];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_vgdac_measurements_t;

typedef struct hfa384x_pdr_level_comp_measurements
{
	u16	value[0];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_level_compc_measurements_t;

typedef struct hfa384x_pdr_mac_address
{
	u8	addr[6];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_mac_address_t;

typedef struct hfa384x_pdr_mkk_callname
{
	u8	callname[8];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_mkk_callname_t;

typedef struct hfa384x_pdr_regdomain
{
	u16	numdomains;
	u16	domain[5];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_regdomain_t;

typedef struct hfa384x_pdr_allowed_channel
{
	u16	ch_bitmap;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_allowed_channel_t;

typedef struct hfa384x_pdr_default_channel
{
	u16	channel;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_default_channel_t;

typedef struct hfa384x_pdr_privacy_option
{
	u16	available;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_privacy_option_t;

typedef struct hfa384x_pdr_temptype
{
	u16	type;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_temptype_t;

typedef struct hfa384x_pdr_refdac_setup
{
	u16	ch_value[14];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_refdac_setup_t;

typedef struct hfa384x_pdr_vgdac_setup
{
	u16	ch_value[14];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_vgdac_setup_t;

typedef struct hfa384x_pdr_level_comp_setup
{
	u16	ch_value[14];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_level_comp_setup_t;

typedef struct hfa384x_pdr_trimdac_setup
{
	u16	trimidac;
	u16	trimqdac;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_trimdac_setup_t;

typedef struct hfa384x_pdr_ifr_setting
{
	u16	value[3];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_ifr_setting_t;

typedef struct hfa384x_pdr_rfr_setting
{
	u16	value[3];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_rfr_setting_t;

typedef struct hfa384x_pdr_hfa3861_baseline
{
	u16	value[50];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_baseline_t;

typedef struct hfa384x_pdr_hfa3861_shadow
{
	u32	value[32];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_shadow_t;

typedef struct hfa384x_pdr_hfa3861_ifrf
{
	u32	value[20];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_ifrf_t;

typedef struct hfa384x_pdr_hfa3861_chcalsp
{
	u16	value[14];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_chcalsp_t;

typedef struct hfa384x_pdr_hfa3861_chcali
{
	u16	value[17];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_chcali_t;

typedef struct hfa384x_pdr_hfa3861_nic_config
{
	u16	config_bitmap;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_nic_config_t;

typedef struct hfa384x_pdr_hfo_delay
{
	u8   hfo_delay;
} __WLAN_ATTRIB_PACK__ hfa384x_hfo_delay_t;

typedef struct hfa384x_pdr_hfa3861_manf_testsp
{
	u16	value[30];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_manf_testsp_t;

typedef struct hfa384x_pdr_hfa3861_manf_testi
{
	u16	value[30];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_manf_testi_t;

typedef struct hfa384x_end_of_pda
{
	u16	crc;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_end_of_pda_t;

typedef struct hfa384x_pdrec
{
	u16	len; /* in words */
	u16	code;
	union pdr {
	hfa384x_pdr_pcb_partnum_t	pcb_partnum;
	hfa384x_pdr_pcb_tracenum_t	pcb_tracenum;
	hfa384x_pdr_nic_serial_t	nic_serial;
	hfa384x_pdr_mkk_measurements_t	mkk_measurements;
	hfa384x_pdr_nic_ramsize_t	nic_ramsize;
	hfa384x_pdr_mfisuprange_t	mfisuprange;
	hfa384x_pdr_cfisuprange_t	cfisuprange;
	hfa384x_pdr_nicid_t		nicid;
	hfa384x_pdr_refdac_measurements_t	refdac_measurements;
	hfa384x_pdr_vgdac_measurements_t	vgdac_measurements;
	hfa384x_pdr_level_compc_measurements_t	level_compc_measurements;
	hfa384x_pdr_mac_address_t	mac_address;
	hfa384x_pdr_mkk_callname_t	mkk_callname;
	hfa384x_pdr_regdomain_t		regdomain;
	hfa384x_pdr_allowed_channel_t	allowed_channel;
	hfa384x_pdr_default_channel_t	default_channel;
	hfa384x_pdr_privacy_option_t	privacy_option;
	hfa384x_pdr_temptype_t		temptype;
	hfa384x_pdr_refdac_setup_t	refdac_setup;
	hfa384x_pdr_vgdac_setup_t	vgdac_setup;
	hfa384x_pdr_level_comp_setup_t	level_comp_setup;
	hfa384x_pdr_trimdac_setup_t	trimdac_setup;
	hfa384x_pdr_ifr_setting_t	ifr_setting;
	hfa384x_pdr_rfr_setting_t	rfr_setting;
	hfa384x_pdr_hfa3861_baseline_t	hfa3861_baseline;
	hfa384x_pdr_hfa3861_shadow_t	hfa3861_shadow;
	hfa384x_pdr_hfa3861_ifrf_t	hfa3861_ifrf;
	hfa384x_pdr_hfa3861_chcalsp_t	hfa3861_chcalsp;
	hfa384x_pdr_hfa3861_chcali_t	hfa3861_chcali;
	hfa384x_pdr_nic_config_t	nic_config;
	hfa384x_hfo_delay_t             hfo_delay;
	hfa384x_pdr_hfa3861_manf_testsp_t	hfa3861_manf_testsp;
	hfa384x_pdr_hfa3861_manf_testi_t	hfa3861_manf_testi;
	hfa384x_pdr_end_of_pda_t	end_of_pda;

	} data;
} __WLAN_ATTRIB_PACK__ hfa384x_pdrec_t;


#ifdef __KERNEL__
/*--------------------------------------------------------------------
---  MAC state structure, argument to all functions --
---  Also, a collection of support types --
--------------------------------------------------------------------*/
typedef struct hfa384x_statusresult
{
	u16	status;
	u16	resp0;
	u16	resp1;
	u16	resp2;
} hfa384x_cmdresult_t;

/* USB Control Exchange (CTLX):
 *  A queue of the structure below is maintained for all of the
 *  Request/Response type USB packets supported by Prism2.
 */
/* The following hfa384x_* structures are arguments to
 * the usercb() for the different CTLX types.
 */
typedef hfa384x_cmdresult_t hfa384x_wridresult_t;
typedef hfa384x_cmdresult_t hfa384x_wmemresult_t;

typedef struct hfa384x_rridresult
{
	u16		rid;
	const void	*riddata;
	unsigned int		riddata_len;
} hfa384x_rridresult_t;

enum ctlx_state {
	CTLX_START = 0,	/* Start state, not queued */

	CTLX_COMPLETE,	/* CTLX successfully completed */
	CTLX_REQ_FAILED,	/* OUT URB completed w/ error */

	CTLX_PENDING,		/* Queued, data valid */
	CTLX_REQ_SUBMITTED,	/* OUT URB submitted */
	CTLX_REQ_COMPLETE,	/* OUT URB complete */
	CTLX_RESP_COMPLETE	/* IN URB received */
};
typedef enum ctlx_state  CTLX_STATE;

struct hfa384x_usbctlx;
struct hfa384x;

typedef void (*ctlx_cmdcb_t)( struct hfa384x*, const struct hfa384x_usbctlx* );

typedef void (*ctlx_usercb_t)(
	struct hfa384x	*hw,
	void		*ctlxresult,
	void		*usercb_data);

typedef struct hfa384x_usbctlx
{
	struct list_head	list;

	size_t			outbufsize;
	hfa384x_usbout_t	outbuf;		/* pkt buf for OUT */
	hfa384x_usbin_t		inbuf;		/* pkt buf for IN(a copy) */

	CTLX_STATE		state;		/* Tracks running state */

	struct completion	done;
	volatile int		reapable;	/* Food for the reaper task */

	ctlx_cmdcb_t		cmdcb;		/* Async command callback */
	ctlx_usercb_t		usercb;		/* Async user callback, */
	void			*usercb_data;	/*  at CTLX completion  */

	int			variant;	/* Identifies cmd variant */
} hfa384x_usbctlx_t;

typedef struct hfa384x_usbctlxq
{
	spinlock_t		lock;
	struct list_head	pending;
	struct list_head	active;
	struct list_head	completing;
	struct list_head	reapable;
} hfa384x_usbctlxq_t;

typedef struct hfa484x_metacmd
{
	u16		cmd;

	u16          parm0;
	u16          parm1;
	u16          parm2;

	hfa384x_cmdresult_t result;
} hfa384x_metacmd_t;

#define	MAX_PRISM2_GRP_ADDR	16
#define	MAX_GRP_ADDR		32
#define WLAN_COMMENT_MAX	80  /* Max. length of user comment string. */

#define MM_SAT_PCF		(BIT14)
#define MM_GCSD_PCF		(BIT15)
#define MM_GCSD_PCF_EB		(BIT14 | BIT15)

#define WLAN_STATE_STOPPED	0   /* Network is not active. */
#define WLAN_STATE_STARTED	1   /* Network has been started. */

#define WLAN_AUTH_MAX           60  /* Max. # of authenticated stations. */
#define WLAN_ACCESS_MAX		60  /* Max. # of stations in an access list. */
#define WLAN_ACCESS_NONE	0   /* No stations may be authenticated. */
#define WLAN_ACCESS_ALL		1   /* All stations may be authenticated. */
#define WLAN_ACCESS_ALLOW	2   /* Authenticate only "allowed" stations. */
#define WLAN_ACCESS_DENY	3   /* Do not authenticate "denied" stations. */

/* XXX These are going away ASAP */
typedef struct prism2sta_authlist
{
	unsigned int	cnt;
	u8	addr[WLAN_AUTH_MAX][WLAN_ADDR_LEN];
	u8	assoc[WLAN_AUTH_MAX];
} prism2sta_authlist_t;

typedef struct prism2sta_accesslist
{
	unsigned int	modify;
	unsigned int	cnt;
	u8	addr[WLAN_ACCESS_MAX][WLAN_ADDR_LEN];
	unsigned int	cnt1;
	u8	addr1[WLAN_ACCESS_MAX][WLAN_ADDR_LEN];
} prism2sta_accesslist_t;

typedef struct hfa384x
{
	/* USB support data */
	struct usb_device	*usb;
	struct urb		rx_urb;
	struct sk_buff		*rx_urb_skb;
	struct urb		tx_urb;
	struct urb		ctlx_urb;
	hfa384x_usbout_t	txbuff;
	hfa384x_usbctlxq_t	ctlxq;
	struct timer_list	reqtimer;
	struct timer_list	resptimer;

	struct timer_list	throttle;

	struct tasklet_struct	reaper_bh;
	struct tasklet_struct	completion_bh;

	struct work_struct	usb_work;

	unsigned long		usb_flags;
#define THROTTLE_RX	0
#define THROTTLE_TX	1
#define WORK_RX_HALT	2
#define WORK_TX_HALT	3
#define WORK_RX_RESUME	4
#define WORK_TX_RESUME	5

	unsigned short		req_timer_done:1;
	unsigned short		resp_timer_done:1;

	int                     endp_in;
	int                     endp_out;

	int                     sniff_fcs;
	int                     sniff_channel;
	int                     sniff_truncate;
	int                     sniffhdr;

	wait_queue_head_t cmdq;	        /* wait queue itself */

	/* Controller state */
	u32		state;
	u32		isap;
	u8		port_enabled[HFA384x_NUMPORTS_MAX];

	/* Download support */
	unsigned int				dlstate;
	hfa384x_downloadbuffer_t	bufinfo;
	u16				dltimeout;

	int                          scanflag;    /* to signal scan comlete */
	int                          join_ap;        /* are we joined to a specific ap */
	int                          join_retries;   /* number of join retries till we fail */
	hfa384x_JoinRequest_data_t   joinreq;        /* join request saved data */

	wlandevice_t            *wlandev;
	/* Timer to allow for the deferred processing of linkstatus messages */
	struct work_struct 	link_bh;

        struct work_struct      commsqual_bh;
	hfa384x_commsquality_t  qual;
	struct timer_list	commsqual_timer;

	u16 link_status;
	u16 link_status_new;
	struct sk_buff_head        authq;

	/* And here we have stuff that used to be in priv */

	/* State variables */
	unsigned int		presniff_port_type;
	u16		presniff_wepflags;
	u32		dot11_desired_bss_type;

	int             dbmadjust;

	/* Group Addresses - right now, there are up to a total
	of MAX_GRP_ADDR group addresses */
	u8		dot11_grp_addr[MAX_GRP_ADDR][WLAN_ADDR_LEN];
	unsigned int		dot11_grpcnt;

	/* Component Identities */
	hfa384x_compident_t	ident_nic;
	hfa384x_compident_t	ident_pri_fw;
	hfa384x_compident_t	ident_sta_fw;
	hfa384x_compident_t	ident_ap_fw;
	u16			mm_mods;

	/* Supplier compatibility ranges */
	hfa384x_caplevel_t	cap_sup_mfi;
	hfa384x_caplevel_t	cap_sup_cfi;
	hfa384x_caplevel_t	cap_sup_pri;
	hfa384x_caplevel_t	cap_sup_sta;
	hfa384x_caplevel_t	cap_sup_ap;

	/* Actor compatibility ranges */
	hfa384x_caplevel_t	cap_act_pri_cfi; /* pri f/w to controller interface */
	hfa384x_caplevel_t	cap_act_sta_cfi; /* sta f/w to controller interface */
	hfa384x_caplevel_t	cap_act_sta_mfi; /* sta f/w to modem interface */
	hfa384x_caplevel_t	cap_act_ap_cfi;  /* ap f/w to controller interface */
	hfa384x_caplevel_t	cap_act_ap_mfi;  /* ap f/w to modem interface */

	u32			psusercount;  /* Power save user count. */
	hfa384x_CommTallies32_t	tallies;      /* Communication tallies. */
	u8			comment[WLAN_COMMENT_MAX+1]; /* User comment */

	/* Channel Info request results (AP only) */
	struct {
		atomic_t		done;
		u8			count;
		hfa384x_ChInfoResult_t	results;
	} channel_info;

	hfa384x_InfFrame_t      *scanresults;


        prism2sta_authlist_t	authlist;     /* Authenticated station list. */
	unsigned int			accessmode;   /* Access mode. */
        prism2sta_accesslist_t	allow;        /* Allowed station list. */
        prism2sta_accesslist_t	deny;         /* Denied station list. */

} hfa384x_t;

/*=============================================================*/
/*--- Function Declarations -----------------------------------*/
/*=============================================================*/
void
hfa384x_create(
	hfa384x_t *hw,
	struct usb_device *usb);

void hfa384x_destroy(hfa384x_t *hw);

int
hfa384x_corereset( hfa384x_t *hw, int holdtime, int settletime, int genesis);
int
hfa384x_drvr_chinforesults( hfa384x_t *hw);
int
hfa384x_drvr_commtallies( hfa384x_t *hw);
int
hfa384x_drvr_disable(hfa384x_t *hw, u16 macport);
int
hfa384x_drvr_enable(hfa384x_t *hw, u16 macport);
int
hfa384x_drvr_flashdl_enable(hfa384x_t *hw);
int
hfa384x_drvr_flashdl_disable(hfa384x_t *hw);
int
hfa384x_drvr_flashdl_write(hfa384x_t *hw, u32 daddr, void* buf, u32 len);
int
hfa384x_drvr_getconfig(hfa384x_t *hw, u16 rid, void *buf, u16 len);
int
hfa384x_drvr_handover( hfa384x_t *hw, u8 *addr);
int
hfa384x_drvr_hostscanresults( hfa384x_t *hw);
int
hfa384x_drvr_low_level(hfa384x_t *hw, hfa384x_metacmd_t *cmd);
int
hfa384x_drvr_mmi_read(hfa384x_t *hw, u32 address, u32 *result);
int
hfa384x_drvr_mmi_write(hfa384x_t *hw, u32 address, u32 data);
int
hfa384x_drvr_ramdl_enable(hfa384x_t *hw, u32 exeaddr);
int
hfa384x_drvr_ramdl_disable(hfa384x_t *hw);
int
hfa384x_drvr_ramdl_write(hfa384x_t *hw, u32 daddr, void* buf, u32 len);
int
hfa384x_drvr_readpda(hfa384x_t *hw, void *buf, unsigned int len);
int
hfa384x_drvr_scanresults( hfa384x_t *hw);

int
hfa384x_drvr_setconfig(hfa384x_t *hw, u16 rid, void *buf, u16 len);

static inline int
hfa384x_drvr_getconfig16(hfa384x_t *hw, u16 rid, void *val)
{
	int		result = 0;
	result = hfa384x_drvr_getconfig(hw, rid, val, sizeof(u16));
	if ( result == 0 ) {
		*((u16*)val) = hfa384x2host_16(*((u16*)val));
	}
	return result;
}

static inline int
hfa384x_drvr_getconfig32(hfa384x_t *hw, u16 rid, void *val)
{
	int		result = 0;

	result = hfa384x_drvr_getconfig(hw, rid, val, sizeof(u32));
	if ( result == 0 ) {
		*((u32*)val) = hfa384x2host_32(*((u32*)val));
	}

	return result;
}

static inline int
hfa384x_drvr_setconfig16(hfa384x_t *hw, u16 rid, u16 val)
{
	u16 value = host2hfa384x_16(val);
	return hfa384x_drvr_setconfig(hw, rid, &value, sizeof(value));
}

static inline int
hfa384x_drvr_setconfig32(hfa384x_t *hw, u16 rid, u32 val)
{
	u32 value = host2hfa384x_32(val);
	return hfa384x_drvr_setconfig(hw, rid, &value, sizeof(value));
}

int
hfa384x_drvr_getconfig_async(hfa384x_t     *hw,
                              u16        rid,
                              ctlx_usercb_t usercb,
                              void          *usercb_data);

int
hfa384x_drvr_setconfig_async(hfa384x_t *hw,
                              u16 rid,
                              void *buf,
                              u16 len,
                              ctlx_usercb_t usercb,
                              void *usercb_data);

static inline int
hfa384x_drvr_setconfig16_async(hfa384x_t *hw, u16 rid, u16 val)
{
	u16 value = host2hfa384x_16(val);
	return hfa384x_drvr_setconfig_async(hw, rid, &value, sizeof(value),
					    NULL , NULL);
}

static inline int
hfa384x_drvr_setconfig32_async(hfa384x_t *hw, u16 rid, u32 val)
{
	u32 value = host2hfa384x_32(val);
	return hfa384x_drvr_setconfig_async(hw, rid, &value, sizeof(value),
					    NULL , NULL);
}


int
hfa384x_drvr_start(hfa384x_t *hw);
int
hfa384x_drvr_stop(hfa384x_t *hw);
int
hfa384x_drvr_txframe(hfa384x_t *hw, struct sk_buff *skb, p80211_hdr_t *p80211_hdr, p80211_metawep_t *p80211_wep);
void
hfa384x_tx_timeout(wlandevice_t *wlandev);

int
hfa384x_cmd_initialize(hfa384x_t *hw);
int
hfa384x_cmd_enable(hfa384x_t *hw, u16 macport);
int
hfa384x_cmd_disable(hfa384x_t *hw, u16 macport);
int
hfa384x_cmd_diagnose(hfa384x_t *hw);
int
hfa384x_cmd_allocate(hfa384x_t *hw, u16 len);
int
hfa384x_cmd_transmit(hfa384x_t *hw, u16 reclaim, u16 qos, u16 fid);
int
hfa384x_cmd_clearpersist(hfa384x_t *hw, u16 fid);
int
hfa384x_cmd_access(hfa384x_t *hw, u16 write, u16 rid, void *buf, u16 len);
int
hfa384x_cmd_monitor(hfa384x_t *hw, u16 enable);
int
hfa384x_cmd_download(
	hfa384x_t *hw,
	u16 mode,
	u16 lowaddr,
	u16 highaddr,
	u16 codelen);
int
hfa384x_cmd_aux_enable(hfa384x_t *hw, int force);
int
hfa384x_cmd_aux_disable(hfa384x_t *hw);
int
hfa384x_copy_from_bap(
	hfa384x_t *hw,
	u16	bap,
	u16	id,
	u16	offset,
	void	*buf,
	unsigned int	len);
int
hfa384x_copy_to_bap(
	hfa384x_t *hw,
	u16	bap,
	u16	id,
	u16	offset,
	void	*buf,
	unsigned int	len);
void
hfa384x_copy_from_aux(
	hfa384x_t *hw,
	u32	cardaddr,
	u32	auxctl,
	void	*buf,
	unsigned int	len);
void
hfa384x_copy_to_aux(
	hfa384x_t *hw,
	u32	cardaddr,
	u32	auxctl,
	void	*buf,
	unsigned int	len);

#endif /* __KERNEL__ */

#endif  /* _HFA384x_H */
