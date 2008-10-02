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
#define		HFA384x_CMD_ALLOC_LEN_MIN	((UINT16)4)
#define		HFA384x_CMD_ALLOC_LEN_MAX	((UINT16)2400)
#define		HFA384x_BAP_DATALEN_MAX		((UINT16)4096)
#define		HFA384x_BAP_OFFSET_MAX		((UINT16)4096)
#define		HFA384x_PORTID_MAX		((UINT16)7)
#define		HFA384x_NUMPORTS_MAX		((UINT16)(HFA384x_PORTID_MAX+1))
#define		HFA384x_PDR_LEN_MAX		((UINT16)512)	/* in bytes, from EK */
#define		HFA384x_PDA_RECS_MAX		((UINT16)200)	/* a guess */
#define		HFA384x_PDA_LEN_MAX		((UINT16)1024)	/* in bytes, from EK */
#define		HFA384x_SCANRESULT_MAX		((UINT16)31)
#define		HFA384x_HSCANRESULT_MAX		((UINT16)31)
#define		HFA384x_CHINFORESULT_MAX	((UINT16)16)
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
#define		HFA384x_BAP_PROC			((UINT16)0)
#define		HFA384x_BAP_INT				((UINT16)1)
#define		HFA384x_PORTTYPE_IBSS			((UINT16)0)
#define		HFA384x_PORTTYPE_BSS			((UINT16)1)
#define		HFA384x_PORTTYPE_WDS			((UINT16)2)
#define		HFA384x_PORTTYPE_PSUEDOIBSS		((UINT16)3)
#define		HFA384x_PORTTYPE_HOSTAP    		((UINT16)6)
#define		HFA384x_WEPFLAGS_PRIVINVOKED		((UINT16)BIT0)
#define		HFA384x_WEPFLAGS_EXCLUDE		((UINT16)BIT1)
#define		HFA384x_WEPFLAGS_DISABLE_TXCRYPT	((UINT16)BIT4)
#define		HFA384x_WEPFLAGS_DISABLE_RXCRYPT	((UINT16)BIT7)
#define		HFA384x_WEPFLAGS_DISALLOW_MIXED 	((UINT16)BIT11)
#define		HFA384x_WEPFLAGS_IV_INTERVAL1		((UINT16)0)
#define		HFA384x_WEPFLAGS_IV_INTERVAL10		((UINT16)BIT5)
#define		HFA384x_WEPFLAGS_IV_INTERVAL50		((UINT16)BIT6)
#define		HFA384x_WEPFLAGS_IV_INTERVAL100		((UINT16)(BIT5 | BIT6))
#define		HFA384x_WEPFLAGS_FIRMWARE_WPA  		((UINT16)BIT8)
#define		HFA384x_WEPFLAGS_HOST_MIC      		((UINT16)BIT9)
#define 	HFA384x_ROAMMODE_FWSCAN_FWROAM		((UINT16)1)
#define 	HFA384x_ROAMMODE_FWSCAN_HOSTROAM	((UINT16)2)
#define 	HFA384x_ROAMMODE_HOSTSCAN_HOSTROAM	((UINT16)3)
#define 	HFA384x_PORTSTATUS_DISABLED		((UINT16)1)
#define 	HFA384x_PORTSTATUS_INITSRCH		((UINT16)2)
#define 	HFA384x_PORTSTATUS_CONN_IBSS		((UINT16)3)
#define 	HFA384x_PORTSTATUS_CONN_ESS		((UINT16)4)
#define 	HFA384x_PORTSTATUS_OOR_ESS		((UINT16)5)
#define 	HFA384x_PORTSTATUS_CONN_WDS		((UINT16)6)
#define 	HFA384x_PORTSTATUS_HOSTAP		((UINT16)8)
#define		HFA384x_RATEBIT_1			((UINT16)1)
#define		HFA384x_RATEBIT_2			((UINT16)2)
#define		HFA384x_RATEBIT_5dot5			((UINT16)4)
#define		HFA384x_RATEBIT_11			((UINT16)8)

/*--- Just some symbolic names for legibility -------*/
#define		HFA384x_TXCMD_NORECL		((UINT16)0)
#define		HFA384x_TXCMD_RECL		((UINT16)1)

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
#define		HFA384x_ADDR_AUX_OFF_MAX	((UINT16)0x007f)

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
		(((UINT32)(((UINT16)(p))&HFA384x_ADDR_AUX_PAGE_MASK)) <<7) | \
		((UINT32)(((UINT16)(o))&HFA384x_ADDR_AUX_OFF_MASK))

/* Make a 32-bit flat address from CMD format 16-bit page and offset */
#define		HFA384x_ADDR_CMD_MKFLAT(p,o)	\
		(((UINT32)(((UINT16)(p))&HFA384x_ADDR_CMD_PAGE_MASK)) <<16) | \
		((UINT32)(((UINT16)(o))&HFA384x_ADDR_CMD_OFF_MASK))

/* Make AUX format offset and page from a 32-bit flat address */
#define		HFA384x_ADDR_AUX_MKPAGE(f) \
		((UINT16)((((UINT32)(f))&HFA384x_ADDR_FLAT_AUX_PAGE_MASK)>>7))
#define		HFA384x_ADDR_AUX_MKOFF(f) \
		((UINT16)(((UINT32)(f))&HFA384x_ADDR_FLAT_AUX_OFF_MASK))

/* Make CMD format offset and page from a 32-bit flat address */
#define		HFA384x_ADDR_CMD_MKPAGE(f) \
		((UINT16)((((UINT32)(f))&HFA384x_ADDR_FLAT_CMD_PAGE_MASK)>>16))
#define		HFA384x_ADDR_CMD_MKOFF(f) \
		((UINT16)(((UINT32)(f))&HFA384x_ADDR_FLAT_CMD_OFF_MASK))

/*--- Aux register masks/tests ----------------------*/
/* Some of the upper bits of the AUX offset register are used to */
/*  select address space. */
#define		HFA384x_AUX_CTL_EXTDS	(0x00)
#define		HFA384x_AUX_CTL_NV	(0x01)
#define		HFA384x_AUX_CTL_PHY	(0x02)
#define		HFA384x_AUX_CTL_ICSRAM	(0x03)

/* Make AUX register offset and page values from a flat address */
#define		HFA384x_AUX_MKOFF(f, c) \
	(HFA384x_ADDR_AUX_MKOFF(f) | (((UINT16)(c))<<12))
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

/*--- Register I/O offsets --------------------------*/
#if ((WLAN_HOSTIF == WLAN_PCMCIA) || (WLAN_HOSTIF == WLAN_PLX))

#define		HFA384x_CMD_OFF			(0x00)
#define		HFA384x_PARAM0_OFF		(0x02)
#define		HFA384x_PARAM1_OFF		(0x04)
#define		HFA384x_PARAM2_OFF		(0x06)
#define		HFA384x_STATUS_OFF		(0x08)
#define		HFA384x_RESP0_OFF		(0x0A)
#define		HFA384x_RESP1_OFF		(0x0C)
#define		HFA384x_RESP2_OFF		(0x0E)
#define		HFA384x_INFOFID_OFF		(0x10)
#define		HFA384x_RXFID_OFF		(0x20)
#define		HFA384x_ALLOCFID_OFF		(0x22)
#define		HFA384x_TXCOMPLFID_OFF		(0x24)
#define		HFA384x_SELECT0_OFF		(0x18)
#define		HFA384x_OFFSET0_OFF		(0x1C)
#define		HFA384x_DATA0_OFF		(0x36)
#define		HFA384x_SELECT1_OFF		(0x1A)
#define		HFA384x_OFFSET1_OFF		(0x1E)
#define		HFA384x_DATA1_OFF		(0x38)
#define		HFA384x_EVSTAT_OFF		(0x30)
#define		HFA384x_INTEN_OFF		(0x32)
#define		HFA384x_EVACK_OFF		(0x34)
#define		HFA384x_CONTROL_OFF		(0x14)
#define		HFA384x_SWSUPPORT0_OFF		(0x28)
#define		HFA384x_SWSUPPORT1_OFF		(0x2A)
#define		HFA384x_SWSUPPORT2_OFF		(0x2C)
#define		HFA384x_AUXPAGE_OFF		(0x3A)
#define		HFA384x_AUXOFFSET_OFF		(0x3C)
#define		HFA384x_AUXDATA_OFF		(0x3E)

#elif (WLAN_HOSTIF == WLAN_PCI || WLAN_HOSTIF == WLAN_USB)

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
#define		HFA384x_INTEN_OFF		(0x64)
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

#endif

/*--- Register Field Masks --------------------------*/
#define		HFA384x_CMD_BUSY		((UINT16)BIT15)
#define		HFA384x_CMD_AINFO		((UINT16)(BIT14 | BIT13 | BIT12 | BIT11 | BIT10 | BIT9 | BIT8))
#define		HFA384x_CMD_MACPORT		((UINT16)(BIT10 | BIT9 | BIT8))
#define		HFA384x_CMD_RECL		((UINT16)BIT8)
#define		HFA384x_CMD_WRITE		((UINT16)BIT8)
#define		HFA384x_CMD_PROGMODE		((UINT16)(BIT9 | BIT8))
#define		HFA384x_CMD_CMDCODE		((UINT16)(BIT5 | BIT4 | BIT3 | BIT2 | BIT1 | BIT0))

#define		HFA384x_STATUS_RESULT		((UINT16)(BIT14 | BIT13 | BIT12 | BIT11 | BIT10 | BIT9 | BIT8))
#define		HFA384x_STATUS_CMDCODE		((UINT16)(BIT5 | BIT4 | BIT3 | BIT2 | BIT1 | BIT0))

#define		HFA384x_OFFSET_BUSY		((UINT16)BIT15)
#define		HFA384x_OFFSET_ERR		((UINT16)BIT14)
#define		HFA384x_OFFSET_DATAOFF		((UINT16)(BIT11 | BIT10 | BIT9 | BIT8 | BIT7 | BIT6 | BIT5 | BIT4 | BIT3 | BIT2 | BIT1))

#define		HFA384x_EVSTAT_TICK		((UINT16)BIT15)
#define		HFA384x_EVSTAT_WTERR		((UINT16)BIT14)
#define		HFA384x_EVSTAT_INFDROP		((UINT16)BIT13)
#define		HFA384x_EVSTAT_INFO		((UINT16)BIT7)
#define		HFA384x_EVSTAT_DTIM		((UINT16)BIT5)
#define		HFA384x_EVSTAT_CMD		((UINT16)BIT4)
#define		HFA384x_EVSTAT_ALLOC		((UINT16)BIT3)
#define		HFA384x_EVSTAT_TXEXC		((UINT16)BIT2)
#define		HFA384x_EVSTAT_TX		((UINT16)BIT1)
#define		HFA384x_EVSTAT_RX		((UINT16)BIT0)

#define         HFA384x_INT_BAP_OP           (HFA384x_EVSTAT_INFO|HFA384x_EVSTAT_RX|HFA384x_EVSTAT_TX|HFA384x_EVSTAT_TXEXC)

#define         HFA384x_INT_NORMAL           (HFA384x_EVSTAT_INFO|HFA384x_EVSTAT_RX|HFA384x_EVSTAT_TX|HFA384x_EVSTAT_TXEXC|HFA384x_EVSTAT_INFDROP|HFA384x_EVSTAT_ALLOC|HFA384x_EVSTAT_DTIM)

#define		HFA384x_INTEN_TICK		((UINT16)BIT15)
#define		HFA384x_INTEN_WTERR		((UINT16)BIT14)
#define		HFA384x_INTEN_INFDROP		((UINT16)BIT13)
#define		HFA384x_INTEN_INFO		((UINT16)BIT7)
#define		HFA384x_INTEN_DTIM		((UINT16)BIT5)
#define		HFA384x_INTEN_CMD		((UINT16)BIT4)
#define		HFA384x_INTEN_ALLOC		((UINT16)BIT3)
#define		HFA384x_INTEN_TXEXC		((UINT16)BIT2)
#define		HFA384x_INTEN_TX		((UINT16)BIT1)
#define		HFA384x_INTEN_RX		((UINT16)BIT0)

#define		HFA384x_EVACK_TICK		((UINT16)BIT15)
#define		HFA384x_EVACK_WTERR		((UINT16)BIT14)
#define		HFA384x_EVACK_INFDROP		((UINT16)BIT13)
#define		HFA384x_EVACK_INFO		((UINT16)BIT7)
#define		HFA384x_EVACK_DTIM		((UINT16)BIT5)
#define		HFA384x_EVACK_CMD		((UINT16)BIT4)
#define		HFA384x_EVACK_ALLOC		((UINT16)BIT3)
#define		HFA384x_EVACK_TXEXC		((UINT16)BIT2)
#define		HFA384x_EVACK_TX		((UINT16)BIT1)
#define		HFA384x_EVACK_RX		((UINT16)BIT0)

#define		HFA384x_CONTROL_AUXEN		((UINT16)(BIT15 | BIT14))


/*--- Command Code Constants --------------------------*/
/*--- Controller Commands --------------------------*/
#define		HFA384x_CMDCODE_INIT		((UINT16)0x00)
#define		HFA384x_CMDCODE_ENABLE		((UINT16)0x01)
#define		HFA384x_CMDCODE_DISABLE		((UINT16)0x02)
#define		HFA384x_CMDCODE_DIAG		((UINT16)0x03)

/*--- Buffer Mgmt Commands --------------------------*/
#define		HFA384x_CMDCODE_ALLOC		((UINT16)0x0A)
#define		HFA384x_CMDCODE_TX		((UINT16)0x0B)
#define		HFA384x_CMDCODE_CLRPRST		((UINT16)0x12)

/*--- Regulate Commands --------------------------*/
#define		HFA384x_CMDCODE_NOTIFY		((UINT16)0x10)
#define		HFA384x_CMDCODE_INQ		((UINT16)0x11)

/*--- Configure Commands --------------------------*/
#define		HFA384x_CMDCODE_ACCESS		((UINT16)0x21)
#define		HFA384x_CMDCODE_DOWNLD		((UINT16)0x22)

/*--- Debugging Commands -----------------------------*/
#define 	HFA384x_CMDCODE_MONITOR		((UINT16)(0x38))
#define		HFA384x_MONITOR_ENABLE		((UINT16)(0x0b))
#define		HFA384x_MONITOR_DISABLE		((UINT16)(0x0f))

/*--- Result Codes --------------------------*/
#define		HFA384x_SUCCESS			((UINT16)(0x00))
#define		HFA384x_CARD_FAIL		((UINT16)(0x01))
#define		HFA384x_NO_BUFF			((UINT16)(0x05))
#define		HFA384x_CMD_ERR			((UINT16)(0x7F))

/*--- Programming Modes --------------------------
	MODE 0: Disable programming
	MODE 1: Enable volatile memory programming
	MODE 2: Enable non-volatile memory programming
	MODE 3: Program non-volatile memory section
--------------------------------------------------*/
#define		HFA384x_PROGMODE_DISABLE	((UINT16)0x00)
#define		HFA384x_PROGMODE_RAM		((UINT16)0x01)
#define		HFA384x_PROGMODE_NV		((UINT16)0x02)
#define		HFA384x_PROGMODE_NVWRITE	((UINT16)0x03)

/*--- AUX register enable --------------------------*/
#define		HFA384x_AUXPW0			((UINT16)0xfe01)
#define		HFA384x_AUXPW1			((UINT16)0xdc23)
#define		HFA384x_AUXPW2			((UINT16)0xba45)

#define		HFA384x_CONTROL_AUX_ISDISABLED	((UINT16)0x0000)
#define		HFA384x_CONTROL_AUX_ISENABLED	((UINT16)0xc000)
#define		HFA384x_CONTROL_AUX_DOENABLE	((UINT16)0x8000)
#define		HFA384x_CONTROL_AUX_DODISABLE	((UINT16)0x4000)

/*--- Record ID Constants --------------------------*/
/*--------------------------------------------------------------------
Configuration RIDs: Network Parameters, Static Configuration Entities
--------------------------------------------------------------------*/
#define		HFA384x_RID_CNFPORTTYPE		((UINT16)0xFC00)
#define		HFA384x_RID_CNFOWNMACADDR	((UINT16)0xFC01)
#define		HFA384x_RID_CNFDESIREDSSID	((UINT16)0xFC02)
#define		HFA384x_RID_CNFOWNCHANNEL	((UINT16)0xFC03)
#define		HFA384x_RID_CNFOWNSSID		((UINT16)0xFC04)
#define		HFA384x_RID_CNFOWNATIMWIN	((UINT16)0xFC05)
#define		HFA384x_RID_CNFSYSSCALE		((UINT16)0xFC06)
#define		HFA384x_RID_CNFMAXDATALEN	((UINT16)0xFC07)
#define		HFA384x_RID_CNFWDSADDR		((UINT16)0xFC08)
#define		HFA384x_RID_CNFPMENABLED	((UINT16)0xFC09)
#define		HFA384x_RID_CNFPMEPS		((UINT16)0xFC0A)
#define		HFA384x_RID_CNFMULTICASTRX	((UINT16)0xFC0B)
#define		HFA384x_RID_CNFMAXSLEEPDUR	((UINT16)0xFC0C)
#define		HFA384x_RID_CNFPMHOLDDUR	((UINT16)0xFC0D)
#define		HFA384x_RID_CNFOWNNAME		((UINT16)0xFC0E)
#define		HFA384x_RID_CNFOWNDTIMPER	((UINT16)0xFC10)
#define		HFA384x_RID_CNFWDSADDR1		((UINT16)0xFC11)
#define		HFA384x_RID_CNFWDSADDR2		((UINT16)0xFC12)
#define		HFA384x_RID_CNFWDSADDR3		((UINT16)0xFC13)
#define		HFA384x_RID_CNFWDSADDR4		((UINT16)0xFC14)
#define		HFA384x_RID_CNFWDSADDR5		((UINT16)0xFC15)
#define		HFA384x_RID_CNFWDSADDR6		((UINT16)0xFC16)
#define		HFA384x_RID_CNFMCASTPMBUFF	((UINT16)0xFC17)

/*--------------------------------------------------------------------
Configuration RID lengths: Network Params, Static Config Entities
  This is the length of JUST the DATA part of the RID (does not
  include the len or code fields)
--------------------------------------------------------------------*/
/* TODO: fill in the rest of these */
#define		HFA384x_RID_CNFPORTTYPE_LEN	((UINT16)2)
#define		HFA384x_RID_CNFOWNMACADDR_LEN	((UINT16)6)
#define		HFA384x_RID_CNFDESIREDSSID_LEN	((UINT16)34)
#define		HFA384x_RID_CNFOWNCHANNEL_LEN	((UINT16)2)
#define		HFA384x_RID_CNFOWNSSID_LEN	((UINT16)34)
#define		HFA384x_RID_CNFOWNATIMWIN_LEN	((UINT16)2)
#define		HFA384x_RID_CNFSYSSCALE_LEN	((UINT16)0)
#define		HFA384x_RID_CNFMAXDATALEN_LEN	((UINT16)0)
#define		HFA384x_RID_CNFWDSADDR_LEN	((UINT16)6)
#define		HFA384x_RID_CNFPMENABLED_LEN	((UINT16)0)
#define		HFA384x_RID_CNFPMEPS_LEN	((UINT16)0)
#define		HFA384x_RID_CNFMULTICASTRX_LEN	((UINT16)0)
#define		HFA384x_RID_CNFMAXSLEEPDUR_LEN	((UINT16)0)
#define		HFA384x_RID_CNFPMHOLDDUR_LEN	((UINT16)0)
#define		HFA384x_RID_CNFOWNNAME_LEN	((UINT16)34)
#define		HFA384x_RID_CNFOWNDTIMPER_LEN	((UINT16)0)
#define		HFA384x_RID_CNFWDSADDR1_LEN	((UINT16)6)
#define		HFA384x_RID_CNFWDSADDR2_LEN	((UINT16)6)
#define		HFA384x_RID_CNFWDSADDR3_LEN	((UINT16)6)
#define		HFA384x_RID_CNFWDSADDR4_LEN	((UINT16)6)
#define		HFA384x_RID_CNFWDSADDR5_LEN	((UINT16)6)
#define		HFA384x_RID_CNFWDSADDR6_LEN	((UINT16)6)
#define		HFA384x_RID_CNFMCASTPMBUFF_LEN	((UINT16)0)
#define		HFA384x_RID_CNFAUTHENTICATION_LEN ((UINT16)sizeof(UINT16))
#define		HFA384x_RID_CNFMAXSLEEPDUR_LEN	((UINT16)0)

/*--------------------------------------------------------------------
Configuration RIDs: Network Parameters, Dynamic Configuration Entities
--------------------------------------------------------------------*/
#define		HFA384x_RID_GROUPADDR		((UINT16)0xFC80)
#define		HFA384x_RID_CREATEIBSS		((UINT16)0xFC81)
#define		HFA384x_RID_FRAGTHRESH		((UINT16)0xFC82)
#define		HFA384x_RID_RTSTHRESH		((UINT16)0xFC83)
#define		HFA384x_RID_TXRATECNTL		((UINT16)0xFC84)
#define		HFA384x_RID_PROMISCMODE		((UINT16)0xFC85)
#define		HFA384x_RID_FRAGTHRESH0		((UINT16)0xFC90)
#define		HFA384x_RID_FRAGTHRESH1		((UINT16)0xFC91)
#define		HFA384x_RID_FRAGTHRESH2		((UINT16)0xFC92)
#define		HFA384x_RID_FRAGTHRESH3		((UINT16)0xFC93)
#define		HFA384x_RID_FRAGTHRESH4		((UINT16)0xFC94)
#define		HFA384x_RID_FRAGTHRESH5		((UINT16)0xFC95)
#define		HFA384x_RID_FRAGTHRESH6		((UINT16)0xFC96)
#define		HFA384x_RID_RTSTHRESH0		((UINT16)0xFC97)
#define		HFA384x_RID_RTSTHRESH1		((UINT16)0xFC98)
#define		HFA384x_RID_RTSTHRESH2		((UINT16)0xFC99)
#define		HFA384x_RID_RTSTHRESH3		((UINT16)0xFC9A)
#define		HFA384x_RID_RTSTHRESH4		((UINT16)0xFC9B)
#define		HFA384x_RID_RTSTHRESH5		((UINT16)0xFC9C)
#define		HFA384x_RID_RTSTHRESH6		((UINT16)0xFC9D)
#define		HFA384x_RID_TXRATECNTL0		((UINT16)0xFC9E)
#define		HFA384x_RID_TXRATECNTL1		((UINT16)0xFC9F)
#define		HFA384x_RID_TXRATECNTL2		((UINT16)0xFCA0)
#define		HFA384x_RID_TXRATECNTL3		((UINT16)0xFCA1)
#define		HFA384x_RID_TXRATECNTL4		((UINT16)0xFCA2)
#define		HFA384x_RID_TXRATECNTL5		((UINT16)0xFCA3)
#define		HFA384x_RID_TXRATECNTL6		((UINT16)0xFCA4)

/*--------------------------------------------------------------------
Configuration RID Lengths: Network Param, Dynamic Config Entities
  This is the length of JUST the DATA part of the RID (does not
  include the len or code fields)
--------------------------------------------------------------------*/
/* TODO: fill in the rest of these */
#define		HFA384x_RID_GROUPADDR_LEN	((UINT16)16 * WLAN_ADDR_LEN)
#define		HFA384x_RID_CREATEIBSS_LEN	((UINT16)0)
#define		HFA384x_RID_FRAGTHRESH_LEN	((UINT16)0)
#define		HFA384x_RID_RTSTHRESH_LEN	((UINT16)0)
#define		HFA384x_RID_TXRATECNTL_LEN	((UINT16)4)
#define		HFA384x_RID_PROMISCMODE_LEN	((UINT16)2)
#define		HFA384x_RID_FRAGTHRESH0_LEN	((UINT16)0)
#define		HFA384x_RID_FRAGTHRESH1_LEN	((UINT16)0)
#define		HFA384x_RID_FRAGTHRESH2_LEN	((UINT16)0)
#define		HFA384x_RID_FRAGTHRESH3_LEN	((UINT16)0)
#define		HFA384x_RID_FRAGTHRESH4_LEN	((UINT16)0)
#define		HFA384x_RID_FRAGTHRESH5_LEN	((UINT16)0)
#define		HFA384x_RID_FRAGTHRESH6_LEN	((UINT16)0)
#define		HFA384x_RID_RTSTHRESH0_LEN	((UINT16)0)
#define		HFA384x_RID_RTSTHRESH1_LEN	((UINT16)0)
#define		HFA384x_RID_RTSTHRESH2_LEN	((UINT16)0)
#define		HFA384x_RID_RTSTHRESH3_LEN	((UINT16)0)
#define		HFA384x_RID_RTSTHRESH4_LEN	((UINT16)0)
#define		HFA384x_RID_RTSTHRESH5_LEN	((UINT16)0)
#define		HFA384x_RID_RTSTHRESH6_LEN	((UINT16)0)
#define		HFA384x_RID_TXRATECNTL0_LEN	((UINT16)0)
#define		HFA384x_RID_TXRATECNTL1_LEN	((UINT16)0)
#define		HFA384x_RID_TXRATECNTL2_LEN	((UINT16)0)
#define		HFA384x_RID_TXRATECNTL3_LEN	((UINT16)0)
#define		HFA384x_RID_TXRATECNTL4_LEN	((UINT16)0)
#define		HFA384x_RID_TXRATECNTL5_LEN	((UINT16)0)
#define		HFA384x_RID_TXRATECNTL6_LEN	((UINT16)0)

/*--------------------------------------------------------------------
Configuration RIDs: Behavior Parameters
--------------------------------------------------------------------*/
#define		HFA384x_RID_ITICKTIME		((UINT16)0xFCE0)

/*--------------------------------------------------------------------
Configuration RID Lengths: Behavior Parameters
  This is the length of JUST the DATA part of the RID (does not
  include the len or code fields)
--------------------------------------------------------------------*/
#define		HFA384x_RID_ITICKTIME_LEN	((UINT16)2)

/*----------------------------------------------------------------------
Information RIDs: NIC Information
--------------------------------------------------------------------*/
#define		HFA384x_RID_MAXLOADTIME		((UINT16)0xFD00)
#define		HFA384x_RID_DOWNLOADBUFFER	((UINT16)0xFD01)
#define		HFA384x_RID_PRIIDENTITY		((UINT16)0xFD02)
#define		HFA384x_RID_PRISUPRANGE		((UINT16)0xFD03)
#define		HFA384x_RID_PRI_CFIACTRANGES	((UINT16)0xFD04)
#define		HFA384x_RID_NICSERIALNUMBER	((UINT16)0xFD0A)
#define		HFA384x_RID_NICIDENTITY		((UINT16)0xFD0B)
#define		HFA384x_RID_MFISUPRANGE		((UINT16)0xFD0C)
#define		HFA384x_RID_CFISUPRANGE		((UINT16)0xFD0D)
#define		HFA384x_RID_CHANNELLIST		((UINT16)0xFD10)
#define		HFA384x_RID_REGULATORYDOMAINS	((UINT16)0xFD11)
#define		HFA384x_RID_TEMPTYPE		((UINT16)0xFD12)
#define		HFA384x_RID_CIS			((UINT16)0xFD13)
#define		HFA384x_RID_STAIDENTITY		((UINT16)0xFD20)
#define		HFA384x_RID_STASUPRANGE		((UINT16)0xFD21)
#define		HFA384x_RID_STA_MFIACTRANGES	((UINT16)0xFD22)
#define		HFA384x_RID_STA_CFIACTRANGES	((UINT16)0xFD23)
#define		HFA384x_RID_BUILDSEQ		((UINT16)0xFFFE)
#define		HFA384x_RID_FWID		((UINT16)0xFFFF)

/*----------------------------------------------------------------------
Information RID Lengths: NIC Information
  This is the length of JUST the DATA part of the RID (does not
  include the len or code fields)
--------------------------------------------------------------------*/
#define		HFA384x_RID_MAXLOADTIME_LEN		((UINT16)0)
#define		HFA384x_RID_DOWNLOADBUFFER_LEN		((UINT16)sizeof(hfa384x_downloadbuffer_t))
#define		HFA384x_RID_PRIIDENTITY_LEN		((UINT16)8)
#define		HFA384x_RID_PRISUPRANGE_LEN		((UINT16)10)
#define		HFA384x_RID_CFIACTRANGES_LEN		((UINT16)10)
#define		HFA384x_RID_NICSERIALNUMBER_LEN		((UINT16)12)
#define		HFA384x_RID_NICIDENTITY_LEN		((UINT16)8)
#define		HFA384x_RID_MFISUPRANGE_LEN		((UINT16)10)
#define		HFA384x_RID_CFISUPRANGE_LEN		((UINT16)10)
#define		HFA384x_RID_CHANNELLIST_LEN		((UINT16)0)
#define		HFA384x_RID_REGULATORYDOMAINS_LEN	((UINT16)12)
#define		HFA384x_RID_TEMPTYPE_LEN		((UINT16)0)
#define		HFA384x_RID_CIS_LEN			((UINT16)480)
#define		HFA384x_RID_STAIDENTITY_LEN		((UINT16)8)
#define		HFA384x_RID_STASUPRANGE_LEN		((UINT16)10)
#define		HFA384x_RID_MFIACTRANGES_LEN		((UINT16)10)
#define		HFA384x_RID_CFIACTRANGES2_LEN		((UINT16)10)
#define		HFA384x_RID_BUILDSEQ_LEN		((UINT16)sizeof(hfa384x_BuildSeq_t))
#define		HFA384x_RID_FWID_LEN			((UINT16)sizeof(hfa384x_FWID_t))

/*--------------------------------------------------------------------
Information RIDs:  MAC Information
--------------------------------------------------------------------*/
#define		HFA384x_RID_PORTSTATUS		((UINT16)0xFD40)
#define		HFA384x_RID_CURRENTSSID		((UINT16)0xFD41)
#define		HFA384x_RID_CURRENTBSSID	((UINT16)0xFD42)
#define		HFA384x_RID_COMMSQUALITY	((UINT16)0xFD43)
#define		HFA384x_RID_CURRENTTXRATE	((UINT16)0xFD44)
#define		HFA384x_RID_CURRENTBCNINT	((UINT16)0xFD45)
#define		HFA384x_RID_CURRENTSCALETHRESH	((UINT16)0xFD46)
#define		HFA384x_RID_PROTOCOLRSPTIME	((UINT16)0xFD47)
#define		HFA384x_RID_SHORTRETRYLIMIT	((UINT16)0xFD48)
#define		HFA384x_RID_LONGRETRYLIMIT	((UINT16)0xFD49)
#define		HFA384x_RID_MAXTXLIFETIME	((UINT16)0xFD4A)
#define		HFA384x_RID_MAXRXLIFETIME	((UINT16)0xFD4B)
#define		HFA384x_RID_CFPOLLABLE		((UINT16)0xFD4C)
#define		HFA384x_RID_AUTHALGORITHMS	((UINT16)0xFD4D)
#define		HFA384x_RID_PRIVACYOPTIMP	((UINT16)0xFD4F)
#define		HFA384x_RID_DBMCOMMSQUALITY	((UINT16)0xFD51)
#define		HFA384x_RID_CURRENTTXRATE1	((UINT16)0xFD80)
#define		HFA384x_RID_CURRENTTXRATE2	((UINT16)0xFD81)
#define		HFA384x_RID_CURRENTTXRATE3	((UINT16)0xFD82)
#define		HFA384x_RID_CURRENTTXRATE4	((UINT16)0xFD83)
#define		HFA384x_RID_CURRENTTXRATE5	((UINT16)0xFD84)
#define		HFA384x_RID_CURRENTTXRATE6	((UINT16)0xFD85)
#define		HFA384x_RID_OWNMACADDRESS	((UINT16)0xFD86)
// #define	HFA384x_RID_PCFINFO		((UINT16)0xFD87)
#define		HFA384x_RID_SCANRESULTS       	((UINT16)0xFD88) // NEW
#define		HFA384x_RID_HOSTSCANRESULTS   	((UINT16)0xFD89) // NEW
#define		HFA384x_RID_AUTHENTICATIONUSED	((UINT16)0xFD8A) // NEW
#define		HFA384x_RID_ASSOCIATEFAILURE  	((UINT16)0xFD8D) // 1.8.0

/*--------------------------------------------------------------------
Information RID Lengths:  MAC Information
  This is the length of JUST the DATA part of the RID (does not
  include the len or code fields)
--------------------------------------------------------------------*/
#define		HFA384x_RID_PORTSTATUS_LEN		((UINT16)0)
#define		HFA384x_RID_CURRENTSSID_LEN		((UINT16)34)
#define		HFA384x_RID_CURRENTBSSID_LEN		((UINT16)WLAN_BSSID_LEN)
#define		HFA384x_RID_COMMSQUALITY_LEN		((UINT16)sizeof(hfa384x_commsquality_t))
#define		HFA384x_RID_DBMCOMMSQUALITY_LEN		((UINT16)sizeof(hfa384x_dbmcommsquality_t))
#define		HFA384x_RID_CURRENTTXRATE_LEN		((UINT16)0)
#define		HFA384x_RID_CURRENTBCNINT_LEN		((UINT16)0)
#define		HFA384x_RID_STACURSCALETHRESH_LEN	((UINT16)12)
#define		HFA384x_RID_APCURSCALETHRESH_LEN	((UINT16)6)
#define		HFA384x_RID_PROTOCOLRSPTIME_LEN		((UINT16)0)
#define		HFA384x_RID_SHORTRETRYLIMIT_LEN		((UINT16)0)
#define		HFA384x_RID_LONGRETRYLIMIT_LEN		((UINT16)0)
#define		HFA384x_RID_MAXTXLIFETIME_LEN		((UINT16)0)
#define		HFA384x_RID_MAXRXLIFETIME_LEN		((UINT16)0)
#define		HFA384x_RID_CFPOLLABLE_LEN		((UINT16)0)
#define		HFA384x_RID_AUTHALGORITHMS_LEN		((UINT16)4)
#define		HFA384x_RID_PRIVACYOPTIMP_LEN		((UINT16)0)
#define		HFA384x_RID_CURRENTTXRATE1_LEN		((UINT16)0)
#define		HFA384x_RID_CURRENTTXRATE2_LEN		((UINT16)0)
#define		HFA384x_RID_CURRENTTXRATE3_LEN		((UINT16)0)
#define		HFA384x_RID_CURRENTTXRATE4_LEN		((UINT16)0)
#define		HFA384x_RID_CURRENTTXRATE5_LEN		((UINT16)0)
#define		HFA384x_RID_CURRENTTXRATE6_LEN		((UINT16)0)
#define		HFA384x_RID_OWNMACADDRESS_LEN		((UINT16)6)
#define		HFA384x_RID_PCFINFO_LEN			((UINT16)6)
#define		HFA384x_RID_CNFAPPCFINFO_LEN		((UINT16)sizeof(hfa384x_PCFInfo_data_t))
#define		HFA384x_RID_SCANREQUEST_LEN		((UINT16)sizeof(hfa384x_ScanRequest_data_t))
#define		HFA384x_RID_JOINREQUEST_LEN		((UINT16)sizeof(hfa384x_JoinRequest_data_t))
#define		HFA384x_RID_AUTHENTICATESTA_LEN		((UINT16)sizeof(hfa384x_authenticateStation_data_t))
#define		HFA384x_RID_CHANNELINFOREQUEST_LEN	((UINT16)sizeof(hfa384x_ChannelInfoRequest_data_t))
/*--------------------------------------------------------------------
Information RIDs:  Modem Information
--------------------------------------------------------------------*/
#define		HFA384x_RID_PHYTYPE		((UINT16)0xFDC0)
#define		HFA384x_RID_CURRENTCHANNEL	((UINT16)0xFDC1)
#define		HFA384x_RID_CURRENTPOWERSTATE	((UINT16)0xFDC2)
#define		HFA384x_RID_CCAMODE		((UINT16)0xFDC3)
#define		HFA384x_RID_SUPPORTEDDATARATES	((UINT16)0xFDC6)
#define		HFA384x_RID_LFOSTATUS           ((UINT16)0xFDC7) // 1.7.1

/*--------------------------------------------------------------------
Information RID Lengths:  Modem Information
  This is the length of JUST the DATA part of the RID (does not
  include the len or code fields)
--------------------------------------------------------------------*/
#define		HFA384x_RID_PHYTYPE_LEN			((UINT16)0)
#define		HFA384x_RID_CURRENTCHANNEL_LEN		((UINT16)0)
#define		HFA384x_RID_CURRENTPOWERSTATE_LEN	((UINT16)0)
#define		HFA384x_RID_CCAMODE_LEN			((UINT16)0)
#define		HFA384x_RID_SUPPORTEDDATARATES_LEN	((UINT16)10)

/*--------------------------------------------------------------------
API ENHANCEMENTS (NOT ALREADY IMPLEMENTED)
--------------------------------------------------------------------*/
#define		HFA384x_RID_CNFWEPDEFAULTKEYID	((UINT16)0xFC23)
#define		HFA384x_RID_CNFWEPDEFAULTKEY0	((UINT16)0xFC24)
#define		HFA384x_RID_CNFWEPDEFAULTKEY1	((UINT16)0xFC25)
#define		HFA384x_RID_CNFWEPDEFAULTKEY2	((UINT16)0xFC26)
#define		HFA384x_RID_CNFWEPDEFAULTKEY3	((UINT16)0xFC27)
#define		HFA384x_RID_CNFWEPFLAGS		((UINT16)0xFC28)
#define		HFA384x_RID_CNFWEPKEYMAPTABLE	((UINT16)0xFC29)
#define		HFA384x_RID_CNFAUTHENTICATION	((UINT16)0xFC2A)
#define		HFA384x_RID_CNFMAXASSOCSTATIONS	((UINT16)0xFC2B)
#define		HFA384x_RID_CNFTXCONTROL	((UINT16)0xFC2C)
#define		HFA384x_RID_CNFROAMINGMODE	((UINT16)0xFC2D)
#define		HFA384x_RID_CNFHOSTAUTHASSOC	((UINT16)0xFC2E)
#define		HFA384x_RID_CNFRCVCRCERROR	((UINT16)0xFC30)
// #define		HFA384x_RID_CNFMMLIFE		((UINT16)0xFC31)
#define		HFA384x_RID_CNFALTRETRYCNT	((UINT16)0xFC32)
#define		HFA384x_RID_CNFAPBCNINT		((UINT16)0xFC33)
#define		HFA384x_RID_CNFAPPCFINFO	((UINT16)0xFC34)
#define		HFA384x_RID_CNFSTAPCFINFO	((UINT16)0xFC35)
#define		HFA384x_RID_CNFPRIORITYQUSAGE	((UINT16)0xFC37)
#define		HFA384x_RID_CNFTIMCTRL		((UINT16)0xFC40)
#define		HFA384x_RID_CNFTHIRTY2TALLY	((UINT16)0xFC42)
#define		HFA384x_RID_CNFENHSECURITY	((UINT16)0xFC43)
#define		HFA384x_RID_CNFDBMADJUST  	((UINT16)0xFC46) // NEW
#define		HFA384x_RID_CNFWPADATA       	((UINT16)0xFC48) // 1.7.0
#define		HFA384x_RID_CNFPROPOGATIONDELAY	((UINT16)0xFC49) // 1.7.6
#define		HFA384x_RID_CNFSHORTPREAMBLE	((UINT16)0xFCB0)
#define		HFA384x_RID_CNFEXCLONGPREAMBLE	((UINT16)0xFCB1)
#define		HFA384x_RID_CNFAUTHRSPTIMEOUT	((UINT16)0xFCB2)
#define		HFA384x_RID_CNFBASICRATES	((UINT16)0xFCB3)
#define		HFA384x_RID_CNFSUPPRATES	((UINT16)0xFCB4)
#define		HFA384x_RID_CNFFALLBACKCTRL	((UINT16)0xFCB5) // NEW
#define		HFA384x_RID_WEPKEYSTATUS   	((UINT16)0xFCB6) // NEW
#define		HFA384x_RID_WEPKEYMAPINDEX 	((UINT16)0xFCB7) // NEW
#define		HFA384x_RID_BROADCASTKEYID 	((UINT16)0xFCB8) // NEW
#define		HFA384x_RID_ENTSECFLAGEYID 	((UINT16)0xFCB9) // NEW
#define		HFA384x_RID_CNFPASSIVESCANCTRL	((UINT16)0xFCBA) // NEW STA
#define		HFA384x_RID_CNFWPAHANDLING	((UINT16)0xFCBB) // 1.7.0
#define		HFA384x_RID_MDCCONTROL        	((UINT16)0xFCBC) // 1.7.0/1.4.0
#define		HFA384x_RID_MDCCOUNTRY        	((UINT16)0xFCBD) // 1.7.0/1.4.0
#define		HFA384x_RID_TXPOWERMAX        	((UINT16)0xFCBE) // 1.7.0/1.4.0
#define		HFA384x_RID_CNFLFOENBLED      	((UINT16)0xFCBF) // 1.6.3
#define         HFA384x_RID_CAPINFO             ((UINT16)0xFCC0) // 1.7.0/1.3.7
#define         HFA384x_RID_LISTENINTERVAL      ((UINT16)0xFCC1) // 1.7.0/1.3.7
#define         HFA384x_RID_DIVERSITYENABLED    ((UINT16)0xFCC2) // 1.7.0/1.3.7
#define         HFA384x_RID_LED_CONTROL         ((UINT16)0xFCC4) // 1.7.6
#define         HFA384x_RID_HFO_DELAY           ((UINT16)0xFCC5) // 1.7.6
#define         HFA384x_RID_DISSALOWEDBSSID     ((UINT16)0xFCC6) // 1.8.0
#define		HFA384x_RID_SCANREQUEST		((UINT16)0xFCE1)
#define		HFA384x_RID_JOINREQUEST		((UINT16)0xFCE2)
#define		HFA384x_RID_AUTHENTICATESTA	((UINT16)0xFCE3)
#define		HFA384x_RID_CHANNELINFOREQUEST	((UINT16)0xFCE4)
#define		HFA384x_RID_HOSTSCAN          	((UINT16)0xFCE5) // NEW STA
#define		HFA384x_RID_ASSOCIATESTA	((UINT16)0xFCE6)

#define		HFA384x_RID_CNFWEPDEFAULTKEY_LEN	((UINT16)6)
#define		HFA384x_RID_CNFWEP128DEFAULTKEY_LEN	((UINT16)14)
#define		HFA384x_RID_CNFPRIOQUSAGE_LEN		((UINT16)4)
/*--------------------------------------------------------------------
PD Record codes
--------------------------------------------------------------------*/
#define HFA384x_PDR_PCB_PARTNUM		((UINT16)0x0001)
#define HFA384x_PDR_PDAVER		((UINT16)0x0002)
#define HFA384x_PDR_NIC_SERIAL		((UINT16)0x0003)
#define HFA384x_PDR_MKK_MEASUREMENTS	((UINT16)0x0004)
#define HFA384x_PDR_NIC_RAMSIZE		((UINT16)0x0005)
#define HFA384x_PDR_MFISUPRANGE		((UINT16)0x0006)
#define HFA384x_PDR_CFISUPRANGE		((UINT16)0x0007)
#define HFA384x_PDR_NICID		((UINT16)0x0008)
//#define HFA384x_PDR_REFDAC_MEASUREMENTS	((UINT16)0x0010)
//#define HFA384x_PDR_VGDAC_MEASUREMENTS	((UINT16)0x0020)
//#define HFA384x_PDR_LEVEL_COMP_MEASUREMENTS	((UINT16)0x0030)
//#define HFA384x_PDR_MODEM_TRIMDAC_MEASUREMENTS	((UINT16)0x0040)
//#define HFA384x_PDR_COREGA_HACK		((UINT16)0x00ff)
#define HFA384x_PDR_MAC_ADDRESS		((UINT16)0x0101)
//#define HFA384x_PDR_MKK_CALLNAME	((UINT16)0x0102)
#define HFA384x_PDR_REGDOMAIN		((UINT16)0x0103)
#define HFA384x_PDR_ALLOWED_CHANNEL	((UINT16)0x0104)
#define HFA384x_PDR_DEFAULT_CHANNEL	((UINT16)0x0105)
//#define HFA384x_PDR_PRIVACY_OPTION	((UINT16)0x0106)
#define HFA384x_PDR_TEMPTYPE		((UINT16)0x0107)
//#define HFA384x_PDR_REFDAC_SETUP	((UINT16)0x0110)
//#define HFA384x_PDR_VGDAC_SETUP		((UINT16)0x0120)
//#define HFA384x_PDR_LEVEL_COMP_SETUP	((UINT16)0x0130)
//#define HFA384x_PDR_TRIMDAC_SETUP	((UINT16)0x0140)
#define HFA384x_PDR_IFR_SETTING		((UINT16)0x0200)
#define HFA384x_PDR_RFR_SETTING		((UINT16)0x0201)
#define HFA384x_PDR_HFA3861_BASELINE	((UINT16)0x0202)
#define HFA384x_PDR_HFA3861_SHADOW	((UINT16)0x0203)
#define HFA384x_PDR_HFA3861_IFRF	((UINT16)0x0204)
#define HFA384x_PDR_HFA3861_CHCALSP	((UINT16)0x0300)
#define HFA384x_PDR_HFA3861_CHCALI	((UINT16)0x0301)
#define HFA384x_PDR_MAX_TX_POWER  	((UINT16)0x0302)
#define HFA384x_PDR_MASTER_CHAN_LIST	((UINT16)0x0303)
#define HFA384x_PDR_3842_NIC_CONFIG	((UINT16)0x0400)
#define HFA384x_PDR_USB_ID		((UINT16)0x0401)
#define HFA384x_PDR_PCI_ID		((UINT16)0x0402)
#define HFA384x_PDR_PCI_IFCONF		((UINT16)0x0403)
#define HFA384x_PDR_PCI_PMCONF		((UINT16)0x0404)
#define HFA384x_PDR_RFENRGY		((UINT16)0x0406)
#define HFA384x_PDR_USB_POWER_TYPE      ((UINT16)0x0407)
//#define HFA384x_PDR_UNKNOWN408		((UINT16)0x0408)
#define HFA384x_PDR_USB_MAX_POWER	((UINT16)0x0409)
#define HFA384x_PDR_USB_MANUFACTURER	((UINT16)0x0410)
#define HFA384x_PDR_USB_PRODUCT  	((UINT16)0x0411)
#define HFA384x_PDR_ANT_DIVERSITY   	((UINT16)0x0412)
#define HFA384x_PDR_HFO_DELAY       	((UINT16)0x0413)
#define HFA384x_PDR_SCALE_THRESH 	((UINT16)0x0414)

#define HFA384x_PDR_HFA3861_MANF_TESTSP	((UINT16)0x0900)
#define HFA384x_PDR_HFA3861_MANF_TESTI	((UINT16)0x0901)
#define HFA384x_PDR_END_OF_PDA		((UINT16)0x0000)


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
#define		HFA384x_INTEN		HFA384x_INTEN_OFF
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

#define		HFA384x_CMD_ISBUSY(value)		((UINT16)(((UINT16)value) & HFA384x_CMD_BUSY))
#define		HFA384x_CMD_AINFO_GET(value)		((UINT16)(((UINT16)(value) & HFA384x_CMD_AINFO) >> 8))
#define		HFA384x_CMD_AINFO_SET(value)		((UINT16)((UINT16)(value) << 8))
#define		HFA384x_CMD_MACPORT_GET(value)		((UINT16)(HFA384x_CMD_AINFO_GET((UINT16)(value) & HFA384x_CMD_MACPORT)))
#define		HFA384x_CMD_MACPORT_SET(value)		((UINT16)HFA384x_CMD_AINFO_SET(value))
#define		HFA384x_CMD_ISRECL(value)		((UINT16)(HFA384x_CMD_AINFO_GET((UINT16)(value) & HFA384x_CMD_RECL)))
#define		HFA384x_CMD_RECL_SET(value)		((UINT16)HFA384x_CMD_AINFO_SET(value))
#define		HFA384x_CMD_QOS_GET(value)		((UINT16((((UINT16)(value))&((UINT16)0x3000)) >> 12))
#define		HFA384x_CMD_QOS_SET(value)		((UINT16)((((UINT16)(value)) << 12) & 0x3000))
#define		HFA384x_CMD_ISWRITE(value)		((UINT16)(HFA384x_CMD_AINFO_GET((UINT16)(value) & HFA384x_CMD_WRITE)))
#define		HFA384x_CMD_WRITE_SET(value)		((UINT16)HFA384x_CMD_AINFO_SET((UINT16)value))
#define		HFA384x_CMD_PROGMODE_GET(value)		((UINT16)(HFA384x_CMD_AINFO_GET((UINT16)(value) & HFA384x_CMD_PROGMODE)))
#define		HFA384x_CMD_PROGMODE_SET(value)		((UINT16)HFA384x_CMD_AINFO_SET((UINT16)value))
#define		HFA384x_CMD_CMDCODE_GET(value)		((UINT16)(((UINT16)(value)) & HFA384x_CMD_CMDCODE))
#define		HFA384x_CMD_CMDCODE_SET(value)		((UINT16)(value))

#define		HFA384x_STATUS_RESULT_GET(value)	((UINT16)((((UINT16)(value)) & HFA384x_STATUS_RESULT) >> 8))
#define		HFA384x_STATUS_RESULT_SET(value)	(((UINT16)(value)) << 8)
#define		HFA384x_STATUS_CMDCODE_GET(value)	(((UINT16)(value)) & HFA384x_STATUS_CMDCODE)
#define		HFA384x_STATUS_CMDCODE_SET(value)	((UINT16)(value))

#define		HFA384x_OFFSET_ISBUSY(value)		((UINT16)(((UINT16)(value)) & HFA384x_OFFSET_BUSY))
#define		HFA384x_OFFSET_ISERR(value)		((UINT16)(((UINT16)(value)) & HFA384x_OFFSET_ERR))
#define		HFA384x_OFFSET_DATAOFF_GET(value)	((UINT16)(((UINT16)(value)) & HFA384x_OFFSET_DATAOFF))
#define		HFA384x_OFFSET_DATAOFF_SET(value)	((UINT16)(value))

#define		HFA384x_EVSTAT_ISTICK(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVSTAT_TICK))
#define		HFA384x_EVSTAT_ISWTERR(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVSTAT_WTERR))
#define		HFA384x_EVSTAT_ISINFDROP(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVSTAT_INFDROP))
#define		HFA384x_EVSTAT_ISINFO(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVSTAT_INFO))
#define		HFA384x_EVSTAT_ISDTIM(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVSTAT_DTIM))
#define		HFA384x_EVSTAT_ISCMD(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVSTAT_CMD))
#define		HFA384x_EVSTAT_ISALLOC(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVSTAT_ALLOC))
#define		HFA384x_EVSTAT_ISTXEXC(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVSTAT_TXEXC))
#define		HFA384x_EVSTAT_ISTX(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVSTAT_TX))
#define		HFA384x_EVSTAT_ISRX(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVSTAT_RX))

#define		HFA384x_EVSTAT_ISBAP_OP(value)		((UINT16)(((UINT16)(value)) & HFA384x_INT_BAP_OP))

#define		HFA384x_INTEN_ISTICK(value)		((UINT16)(((UINT16)(value)) & HFA384x_INTEN_TICK))
#define		HFA384x_INTEN_TICK_SET(value)		((UINT16)(((UINT16)(value)) << 15))
#define		HFA384x_INTEN_ISWTERR(value)		((UINT16)(((UINT16)(value)) & HFA384x_INTEN_WTERR))
#define		HFA384x_INTEN_WTERR_SET(value)		((UINT16)(((UINT16)(value)) << 14))
#define		HFA384x_INTEN_ISINFDROP(value)		((UINT16)(((UINT16)(value)) & HFA384x_INTEN_INFDROP))
#define		HFA384x_INTEN_INFDROP_SET(value)	((UINT16)(((UINT16)(value)) << 13))
#define		HFA384x_INTEN_ISINFO(value)		((UINT16)(((UINT16)(value)) & HFA384x_INTEN_INFO))
#define		HFA384x_INTEN_INFO_SET(value)		((UINT16)(((UINT16)(value)) << 7))
#define		HFA384x_INTEN_ISDTIM(value)		((UINT16)(((UINT16)(value)) & HFA384x_INTEN_DTIM))
#define		HFA384x_INTEN_DTIM_SET(value)		((UINT16)(((UINT16)(value)) << 5))
#define		HFA384x_INTEN_ISCMD(value)		((UINT16)(((UINT16)(value)) & HFA384x_INTEN_CMD))
#define		HFA384x_INTEN_CMD_SET(value)		((UINT16)(((UINT16)(value)) << 4))
#define		HFA384x_INTEN_ISALLOC(value)		((UINT16)(((UINT16)(value)) & HFA384x_INTEN_ALLOC))
#define		HFA384x_INTEN_ALLOC_SET(value)		((UINT16)(((UINT16)(value)) << 3))
#define		HFA384x_INTEN_ISTXEXC(value)		((UINT16)(((UINT16)(value)) & HFA384x_INTEN_TXEXC))
#define		HFA384x_INTEN_TXEXC_SET(value)		((UINT16)(((UINT16)(value)) << 2))
#define		HFA384x_INTEN_ISTX(value)		((UINT16)(((UINT16)(value)) & HFA384x_INTEN_TX))
#define		HFA384x_INTEN_TX_SET(value)		((UINT16)(((UINT16)(value)) << 1))
#define		HFA384x_INTEN_ISRX(value)		((UINT16)(((UINT16)(value)) & HFA384x_INTEN_RX))
#define		HFA384x_INTEN_RX_SET(value)		((UINT16)(((UINT16)(value)) << 0))

#define		HFA384x_EVACK_ISTICK(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVACK_TICK))
#define		HFA384x_EVACK_TICK_SET(value)		((UINT16)(((UINT16)(value)) << 15))
#define		HFA384x_EVACK_ISWTERR(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVACK_WTERR))
#define		HFA384x_EVACK_WTERR_SET(value)		((UINT16)(((UINT16)(value)) << 14))
#define		HFA384x_EVACK_ISINFDROP(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVACK_INFDROP))
#define		HFA384x_EVACK_INFDROP_SET(value)	((UINT16)(((UINT16)(value)) << 13))
#define		HFA384x_EVACK_ISINFO(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVACK_INFO))
#define		HFA384x_EVACK_INFO_SET(value)		((UINT16)(((UINT16)(value)) << 7))
#define		HFA384x_EVACK_ISDTIM(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVACK_DTIM))
#define		HFA384x_EVACK_DTIM_SET(value)		((UINT16)(((UINT16)(value)) << 5))
#define		HFA384x_EVACK_ISCMD(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVACK_CMD))
#define		HFA384x_EVACK_CMD_SET(value)		((UINT16)(((UINT16)(value)) << 4))
#define		HFA384x_EVACK_ISALLOC(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVACK_ALLOC))
#define		HFA384x_EVACK_ALLOC_SET(value)		((UINT16)(((UINT16)(value)) << 3))
#define		HFA384x_EVACK_ISTXEXC(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVACK_TXEXC))
#define		HFA384x_EVACK_TXEXC_SET(value)		((UINT16)(((UINT16)(value)) << 2))
#define		HFA384x_EVACK_ISTX(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVACK_TX))
#define		HFA384x_EVACK_TX_SET(value)		((UINT16)(((UINT16)(value)) << 1))
#define		HFA384x_EVACK_ISRX(value)		((UINT16)(((UINT16)(value)) & HFA384x_EVACK_RX))
#define		HFA384x_EVACK_RX_SET(value)		((UINT16)(((UINT16)(value)) << 0))

#define		HFA384x_CONTROL_AUXEN_SET(value)	((UINT16)(((UINT16)(value)) << 14))
#define		HFA384x_CONTROL_AUXEN_GET(value)	((UINT16)(((UINT16)(value)) >> 14))

/* Byte Order */
#ifdef __KERNEL__
#define hfa384x2host_16(n)	(__le16_to_cpu((UINT16)(n)))
#define hfa384x2host_32(n)	(__le32_to_cpu((UINT32)(n)))
#define host2hfa384x_16(n)	(__cpu_to_le16((UINT16)(n)))
#define host2hfa384x_32(n)	(__cpu_to_le32((UINT32)(n)))
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
	UINT16	len;
	UINT8	data[0];
} __WLAN_ATTRIB_PACK__ hfa384x_bytestr_t;

typedef struct hfa384x_bytestr32
{
	UINT16	len;
	UINT8	data[32];
} __WLAN_ATTRIB_PACK__ hfa384x_bytestr32_t;

/*--------------------------------------------------------------------
Configuration Record Structures:
	Network Parameters, Static Configuration Entities
--------------------------------------------------------------------*/
/* Prototype structure: all configuration record structures start with
these members */

typedef struct hfa384x_record
{
	UINT16	reclen;
	UINT16	rid;
} __WLAN_ATTRIB_PACK__ hfa384x_rec_t;

typedef struct hfa384x_record16
{
	UINT16	reclen;
	UINT16	rid;
	UINT16	val;
} __WLAN_ATTRIB_PACK__ hfa384x_rec16_t;

typedef struct hfa384x_record32
{
	UINT16	reclen;
	UINT16	rid;
	UINT32	val;
} __WLAN_ATTRIB_PACK__ hfa384x_rec32;

/*-- Hardware/Firmware Component Information ----------*/
typedef struct hfa384x_compident
{
	UINT16	id;
	UINT16	variant;
	UINT16	major;
	UINT16	minor;
} __WLAN_ATTRIB_PACK__ hfa384x_compident_t;

typedef struct hfa384x_caplevel
{
	UINT16	role;
	UINT16	id;
	UINT16	variant;
	UINT16	bottom;
	UINT16	top;
} __WLAN_ATTRIB_PACK__ hfa384x_caplevel_t;

/*-- Configuration Record: cnfPortType --*/
typedef struct hfa384x_cnfPortType
{
	UINT16	cnfPortType;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfPortType_t;

/*-- Configuration Record: cnfOwnMACAddress --*/
typedef struct hfa384x_cnfOwnMACAddress
{
	UINT8	cnfOwnMACAddress[6];
} __WLAN_ATTRIB_PACK__ hfa384x_cnfOwnMACAddress_t;

/*-- Configuration Record: cnfDesiredSSID --*/
typedef struct hfa384x_cnfDesiredSSID
{
	UINT8	cnfDesiredSSID[34];
} __WLAN_ATTRIB_PACK__ hfa384x_cnfDesiredSSID_t;

/*-- Configuration Record: cnfOwnChannel --*/
typedef struct hfa384x_cnfOwnChannel
{
	UINT16	cnfOwnChannel;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfOwnChannel_t;

/*-- Configuration Record: cnfOwnSSID --*/
typedef struct hfa384x_cnfOwnSSID
{
	UINT8	cnfOwnSSID[34];
} __WLAN_ATTRIB_PACK__ hfa384x_cnfOwnSSID_t;

/*-- Configuration Record: cnfOwnATIMWindow --*/
typedef struct hfa384x_cnfOwnATIMWindow
{
	UINT16	cnfOwnATIMWindow;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfOwnATIMWindow_t;

/*-- Configuration Record: cnfSystemScale --*/
typedef struct hfa384x_cnfSystemScale
{
	UINT16	cnfSystemScale;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfSystemScale_t;

/*-- Configuration Record: cnfMaxDataLength --*/
typedef struct hfa384x_cnfMaxDataLength
{
	UINT16	cnfMaxDataLength;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfMaxDataLength_t;

/*-- Configuration Record: cnfWDSAddress --*/
typedef struct hfa384x_cnfWDSAddress
{
	UINT8	cnfWDSAddress[6];
} __WLAN_ATTRIB_PACK__ hfa384x_cnfWDSAddress_t;

/*-- Configuration Record: cnfPMEnabled --*/
typedef struct hfa384x_cnfPMEnabled
{
	UINT16	cnfPMEnabled;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfPMEnabled_t;

/*-- Configuration Record: cnfPMEPS --*/
typedef struct hfa384x_cnfPMEPS
{
	UINT16	cnfPMEPS;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfPMEPS_t;

/*-- Configuration Record: cnfMulticastReceive --*/
typedef struct hfa384x_cnfMulticastReceive
{
	UINT16	cnfMulticastReceive;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfMulticastReceive_t;

/*-- Configuration Record: cnfAuthentication --*/
#define HFA384x_CNFAUTHENTICATION_OPENSYSTEM	0x0001
#define HFA384x_CNFAUTHENTICATION_SHAREDKEY	0x0002
#define HFA384x_CNFAUTHENTICATION_LEAP     	0x0004

/*-- Configuration Record: cnfMaxSleepDuration --*/
typedef struct hfa384x_cnfMaxSleepDuration
{
	UINT16	cnfMaxSleepDuration;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfMaxSleepDuration_t;

/*-- Configuration Record: cnfPMHoldoverDuration --*/
typedef struct hfa384x_cnfPMHoldoverDuration
{
	UINT16	cnfPMHoldoverDuration;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfPMHoldoverDuration_t;

/*-- Configuration Record: cnfOwnName --*/
typedef struct hfa384x_cnfOwnName
{
	UINT8	cnfOwnName[34];
} __WLAN_ATTRIB_PACK__ hfa384x_cnfOwnName_t;

/*-- Configuration Record: cnfOwnDTIMPeriod --*/
typedef struct hfa384x_cnfOwnDTIMPeriod
{
	UINT16	cnfOwnDTIMPeriod;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfOwnDTIMPeriod_t;

/*-- Configuration Record: cnfWDSAddress --*/
typedef struct hfa384x_cnfWDSAddressN
{
	UINT8	cnfWDSAddress[6];
} __WLAN_ATTRIB_PACK__ hfa384x_cnfWDSAddressN_t;

/*-- Configuration Record: cnfMulticastPMBuffering --*/
typedef struct hfa384x_cnfMulticastPMBuffering
{
	UINT16	cnfMulticastPMBuffering;
} __WLAN_ATTRIB_PACK__ hfa384x_cnfMulticastPMBuffering_t;

/*--------------------------------------------------------------------
Configuration Record Structures:
	Network Parameters, Dynamic Configuration Entities
--------------------------------------------------------------------*/

/*-- Configuration Record: GroupAddresses --*/
typedef struct hfa384x_GroupAddresses
{
	UINT8	MACAddress[16][6];
} __WLAN_ATTRIB_PACK__ hfa384x_GroupAddresses_t;

/*-- Configuration Record: CreateIBSS --*/
typedef struct hfa384x_CreateIBSS
{
	UINT16	CreateIBSS;
} __WLAN_ATTRIB_PACK__ hfa384x_CreateIBSS_t;

#define HFA384x_CREATEIBSS_JOINCREATEIBSS          0
#define HFA384x_CREATEIBSS_JOINESS_JOINCREATEIBSS  1
#define HFA384x_CREATEIBSS_JOINIBSS                2
#define HFA384x_CREATEIBSS_JOINESS_JOINIBSS        3

/*-- Configuration Record: FragmentationThreshold --*/
typedef struct hfa384x_FragmentationThreshold
{
	UINT16	FragmentationThreshold;
} __WLAN_ATTRIB_PACK__ hfa384x_FragmentationThreshold_t;

/*-- Configuration Record: RTSThreshold --*/
typedef struct hfa384x_RTSThreshold
{
	UINT16	RTSThreshold;
} __WLAN_ATTRIB_PACK__ hfa384x_RTSThreshold_t;

/*-- Configuration Record: TxRateControl --*/
typedef struct hfa384x_TxRateControl
{
	UINT16	TxRateControl;
} __WLAN_ATTRIB_PACK__ hfa384x_TxRateControl_t;

/*-- Configuration Record: PromiscuousMode --*/
typedef struct hfa384x_PromiscuousMode
{
	UINT16	PromiscuousMode;
} __WLAN_ATTRIB_PACK__ hfa384x_PromiscuousMode_t;

/*-- Configuration Record: ScanRequest (data portion only) --*/
typedef struct hfa384x_ScanRequest_data
{
	UINT16	channelList;
	UINT16	txRate;
} __WLAN_ATTRIB_PACK__ hfa384x_ScanRequest_data_t;

/*-- Configuration Record: HostScanRequest (data portion only) --*/
typedef struct hfa384x_HostScanRequest_data
{
	UINT16	channelList;
	UINT16	txRate;
	hfa384x_bytestr32_t ssid;
} __WLAN_ATTRIB_PACK__ hfa384x_HostScanRequest_data_t;

/*-- Configuration Record: JoinRequest (data portion only) --*/
typedef struct hfa384x_JoinRequest_data
{
	UINT8	bssid[WLAN_BSSID_LEN];
	UINT16	channel;
} __WLAN_ATTRIB_PACK__ hfa384x_JoinRequest_data_t;

/*-- Configuration Record: authenticateStation (data portion only) --*/
typedef struct hfa384x_authenticateStation_data
{
	UINT8	address[WLAN_ADDR_LEN];
	UINT16	status;
	UINT16	algorithm;
} __WLAN_ATTRIB_PACK__ hfa384x_authenticateStation_data_t;

/*-- Configuration Record: associateStation (data portion only) --*/
typedef struct hfa384x_associateStation_data
{
	UINT8	address[WLAN_ADDR_LEN];
	UINT16	status;
	UINT16	type;
} __WLAN_ATTRIB_PACK__ hfa384x_associateStation_data_t;

/*-- Configuration Record: ChannelInfoRequest (data portion only) --*/
typedef struct hfa384x_ChannelInfoRequest_data
{
	UINT16	channelList;
	UINT16	channelDwellTime;
} __WLAN_ATTRIB_PACK__ hfa384x_ChannelInfoRequest_data_t;

/*-- Configuration Record: WEPKeyMapping (data portion only) --*/
typedef struct hfa384x_WEPKeyMapping
{
	UINT8	address[WLAN_ADDR_LEN];
	UINT16	key_index;
	UINT8 	key[16];
	UINT8 	mic_transmit_key[4];
	UINT8 	mic_receive_key[4];
} __WLAN_ATTRIB_PACK__ hfa384x_WEPKeyMapping_t;

/*-- Configuration Record: WPAData       (data portion only) --*/
typedef struct hfa384x_WPAData
{
	UINT16	datalen;
        UINT8 	data[0]; // max 80
} __WLAN_ATTRIB_PACK__ hfa384x_WPAData_t;

/*--------------------------------------------------------------------
Configuration Record Structures: Behavior Parameters
--------------------------------------------------------------------*/

/*-- Configuration Record: TickTime --*/
typedef struct hfa384x_TickTime
{
	UINT16	TickTime;
} __WLAN_ATTRIB_PACK__ hfa384x_TickTime_t;

/*--------------------------------------------------------------------
Information Record Structures: NIC Information
--------------------------------------------------------------------*/

/*-- Information Record: MaxLoadTime --*/
typedef struct hfa384x_MaxLoadTime
{
	UINT16	MaxLoadTime;
} __WLAN_ATTRIB_PACK__ hfa384x_MaxLoadTime_t;

/*-- Information Record: DownLoadBuffer --*/
/* NOTE: The page and offset are in AUX format */
typedef struct hfa384x_downloadbuffer
{
	UINT16	page;
	UINT16	offset;
	UINT16	len;
} __WLAN_ATTRIB_PACK__ hfa384x_downloadbuffer_t;

/*-- Information Record: PRIIdentity --*/
typedef struct hfa384x_PRIIdentity
{
	UINT16	PRICompID;
	UINT16	PRIVariant;
	UINT16	PRIMajorVersion;
	UINT16	PRIMinorVersion;
} __WLAN_ATTRIB_PACK__ hfa384x_PRIIdentity_t;

/*-- Information Record: PRISupRange --*/
typedef struct hfa384x_PRISupRange
{
	UINT16	PRIRole;
	UINT16	PRIID;
	UINT16	PRIVariant;
	UINT16	PRIBottom;
	UINT16	PRITop;
} __WLAN_ATTRIB_PACK__ hfa384x_PRISupRange_t;

/*-- Information Record: CFIActRanges --*/
typedef struct hfa384x_CFIActRanges
{
	UINT16	CFIRole;
	UINT16	CFIID;
	UINT16	CFIVariant;
	UINT16	CFIBottom;
	UINT16	CFITop;
} __WLAN_ATTRIB_PACK__ hfa384x_CFIActRanges_t;

/*-- Information Record: NICSerialNumber --*/
typedef struct hfa384x_NICSerialNumber
{
	UINT8	NICSerialNumber[12];
} __WLAN_ATTRIB_PACK__ hfa384x_NICSerialNumber_t;

/*-- Information Record: NICIdentity --*/
typedef struct hfa384x_NICIdentity
{
	UINT16	NICCompID;
	UINT16	NICVariant;
	UINT16	NICMajorVersion;
	UINT16	NICMinorVersion;
} __WLAN_ATTRIB_PACK__ hfa384x_NICIdentity_t;

/*-- Information Record: MFISupRange --*/
typedef struct hfa384x_MFISupRange
{
	UINT16	MFIRole;
	UINT16	MFIID;
	UINT16	MFIVariant;
	UINT16	MFIBottom;
	UINT16	MFITop;
} __WLAN_ATTRIB_PACK__ hfa384x_MFISupRange_t;

/*-- Information Record: CFISupRange --*/
typedef struct hfa384x_CFISupRange
{
	UINT16	CFIRole;
	UINT16	CFIID;
	UINT16	CFIVariant;
	UINT16	CFIBottom;
	UINT16	CFITop;
} __WLAN_ATTRIB_PACK__ hfa384x_CFISupRange_t;

/*-- Information Record: BUILDSEQ:BuildSeq --*/
typedef struct hfa384x_BuildSeq {
	UINT16	primary;
	UINT16	secondary;
} __WLAN_ATTRIB_PACK__ hfa384x_BuildSeq_t;

/*-- Information Record: FWID --*/
#define HFA384x_FWID_LEN	14
typedef struct hfa384x_FWID {
	UINT8	primary[HFA384x_FWID_LEN];
	UINT8	secondary[HFA384x_FWID_LEN];
} __WLAN_ATTRIB_PACK__ hfa384x_FWID_t;

/*-- Information Record: ChannelList --*/
typedef struct hfa384x_ChannelList
{
	UINT16	ChannelList;
} __WLAN_ATTRIB_PACK__ hfa384x_ChannelList_t;

/*-- Information Record: RegulatoryDomains --*/
typedef struct hfa384x_RegulatoryDomains
{
	UINT8	RegulatoryDomains[12];
} __WLAN_ATTRIB_PACK__ hfa384x_RegulatoryDomains_t;

/*-- Information Record: TempType --*/
typedef struct hfa384x_TempType
{
	UINT16	TempType;
} __WLAN_ATTRIB_PACK__ hfa384x_TempType_t;

/*-- Information Record: CIS --*/
typedef struct hfa384x_CIS
{
	UINT8	CIS[480];
} __WLAN_ATTRIB_PACK__ hfa384x_CIS_t;

/*-- Information Record: STAIdentity --*/
typedef struct hfa384x_STAIdentity
{
	UINT16	STACompID;
	UINT16	STAVariant;
	UINT16	STAMajorVersion;
	UINT16	STAMinorVersion;
} __WLAN_ATTRIB_PACK__ hfa384x_STAIdentity_t;

/*-- Information Record: STASupRange --*/
typedef struct hfa384x_STASupRange
{
	UINT16	STARole;
	UINT16	STAID;
	UINT16	STAVariant;
	UINT16	STABottom;
	UINT16	STATop;
} __WLAN_ATTRIB_PACK__ hfa384x_STASupRange_t;

/*-- Information Record: MFIActRanges --*/
typedef struct hfa384x_MFIActRanges
{
	UINT16	MFIRole;
	UINT16	MFIID;
	UINT16	MFIVariant;
	UINT16	MFIBottom;
	UINT16	MFITop;
} __WLAN_ATTRIB_PACK__ hfa384x_MFIActRanges_t;

/*--------------------------------------------------------------------
Information Record Structures: NIC Information
--------------------------------------------------------------------*/

/*-- Information Record: PortStatus --*/
typedef struct hfa384x_PortStatus
{
	UINT16	PortStatus;
} __WLAN_ATTRIB_PACK__ hfa384x_PortStatus_t;

#define HFA384x_PSTATUS_DISABLED	((UINT16)1)
#define HFA384x_PSTATUS_SEARCHING	((UINT16)2)
#define HFA384x_PSTATUS_CONN_IBSS	((UINT16)3)
#define HFA384x_PSTATUS_CONN_ESS	((UINT16)4)
#define HFA384x_PSTATUS_OUTOFRANGE	((UINT16)5)
#define HFA384x_PSTATUS_CONN_WDS	((UINT16)6)

/*-- Information Record: CurrentSSID --*/
typedef struct hfa384x_CurrentSSID
{
	UINT8	CurrentSSID[34];
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentSSID_t;

/*-- Information Record: CurrentBSSID --*/
typedef struct hfa384x_CurrentBSSID
{
	UINT8	CurrentBSSID[6];
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentBSSID_t;

/*-- Information Record: commsquality --*/
typedef struct hfa384x_commsquality
{
	UINT16	CQ_currBSS;
	UINT16	ASL_currBSS;
	UINT16	ANL_currFC;
} __WLAN_ATTRIB_PACK__ hfa384x_commsquality_t;

/*-- Information Record: dmbcommsquality --*/
typedef struct hfa384x_dbmcommsquality
{
	UINT16	CQdbm_currBSS;
	UINT16	ASLdbm_currBSS;
	UINT16	ANLdbm_currFC;
} __WLAN_ATTRIB_PACK__ hfa384x_dbmcommsquality_t;

/*-- Information Record: CurrentTxRate --*/
typedef struct hfa384x_CurrentTxRate
{
	UINT16	CurrentTxRate;
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentTxRate_t;

/*-- Information Record: CurrentBeaconInterval --*/
typedef struct hfa384x_CurrentBeaconInterval
{
	UINT16	CurrentBeaconInterval;
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentBeaconInterval_t;

/*-- Information Record: CurrentScaleThresholds --*/
typedef struct hfa384x_CurrentScaleThresholds
{
	UINT16	EnergyDetectThreshold;
	UINT16	CarrierDetectThreshold;
	UINT16	DeferDetectThreshold;
	UINT16	CellSearchThreshold; /* Stations only */
	UINT16	DeadSpotThreshold; /* Stations only */
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentScaleThresholds_t;

/*-- Information Record: ProtocolRspTime --*/
typedef struct hfa384x_ProtocolRspTime
{
	UINT16	ProtocolRspTime;
} __WLAN_ATTRIB_PACK__ hfa384x_ProtocolRspTime_t;

/*-- Information Record: ShortRetryLimit --*/
typedef struct hfa384x_ShortRetryLimit
{
	UINT16	ShortRetryLimit;
} __WLAN_ATTRIB_PACK__ hfa384x_ShortRetryLimit_t;

/*-- Information Record: LongRetryLimit --*/
typedef struct hfa384x_LongRetryLimit
{
	UINT16	LongRetryLimit;
} __WLAN_ATTRIB_PACK__ hfa384x_LongRetryLimit_t;

/*-- Information Record: MaxTransmitLifetime --*/
typedef struct hfa384x_MaxTransmitLifetime
{
	UINT16	MaxTransmitLifetime;
} __WLAN_ATTRIB_PACK__ hfa384x_MaxTransmitLifetime_t;

/*-- Information Record: MaxReceiveLifetime --*/
typedef struct hfa384x_MaxReceiveLifetime
{
	UINT16	MaxReceiveLifetime;
} __WLAN_ATTRIB_PACK__ hfa384x_MaxReceiveLifetime_t;

/*-- Information Record: CFPollable --*/
typedef struct hfa384x_CFPollable
{
	UINT16	CFPollable;
} __WLAN_ATTRIB_PACK__ hfa384x_CFPollable_t;

/*-- Information Record: AuthenticationAlgorithms --*/
typedef struct hfa384x_AuthenticationAlgorithms
{
	UINT16	AuthenticationType;
	UINT16	TypeEnabled;
} __WLAN_ATTRIB_PACK__ hfa384x_AuthenticationAlgorithms_t;

/*-- Information Record: AuthenticationAlgorithms
(data only --*/
typedef struct hfa384x_AuthenticationAlgorithms_data
{
	UINT16	AuthenticationType;
	UINT16	TypeEnabled;
} __WLAN_ATTRIB_PACK__ hfa384x_AuthenticationAlgorithms_data_t;

/*-- Information Record: PrivacyOptionImplemented --*/
typedef struct hfa384x_PrivacyOptionImplemented
{
	UINT16	PrivacyOptionImplemented;
} __WLAN_ATTRIB_PACK__ hfa384x_PrivacyOptionImplemented_t;

/*-- Information Record: OwnMACAddress --*/
typedef struct hfa384x_OwnMACAddress
{
	UINT8	OwnMACAddress[6];
} __WLAN_ATTRIB_PACK__ hfa384x_OwnMACAddress_t;

/*-- Information Record: PCFInfo --*/
typedef struct hfa384x_PCFInfo
{
	UINT16	MediumOccupancyLimit;
	UINT16	CFPPeriod;
	UINT16	CFPMaxDuration;
	UINT16	CFPFlags;
} __WLAN_ATTRIB_PACK__ hfa384x_PCFInfo_t;

/*-- Information Record: PCFInfo (data portion only) --*/
typedef struct hfa384x_PCFInfo_data
{
	UINT16	MediumOccupancyLimit;
	UINT16	CFPPeriod;
	UINT16	CFPMaxDuration;
	UINT16	CFPFlags;
} __WLAN_ATTRIB_PACK__ hfa384x_PCFInfo_data_t;

/*--------------------------------------------------------------------
Information Record Structures: Modem Information Records
--------------------------------------------------------------------*/

/*-- Information Record: PHYType --*/
typedef struct hfa384x_PHYType
{
	UINT16	PHYType;
} __WLAN_ATTRIB_PACK__ hfa384x_PHYType_t;

/*-- Information Record: CurrentChannel --*/
typedef struct hfa384x_CurrentChannel
{
	UINT16	CurrentChannel;
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentChannel_t;

/*-- Information Record: CurrentPowerState --*/
typedef struct hfa384x_CurrentPowerState
{
	UINT16	CurrentPowerState;
} __WLAN_ATTRIB_PACK__ hfa384x_CurrentPowerState_t;

/*-- Information Record: CCAMode --*/
typedef struct hfa384x_CCAMode
{
	UINT16	CCAMode;
} __WLAN_ATTRIB_PACK__ hfa384x_CCAMode_t;

/*-- Information Record: SupportedDataRates --*/
typedef struct hfa384x_SupportedDataRates
{
	UINT8	SupportedDataRates[10];
} __WLAN_ATTRIB_PACK__ hfa384x_SupportedDataRates_t;

/*-- Information Record: LFOStatus --*/
typedef struct hfa384x_LFOStatus
{
	UINT16  TestResults;
	UINT16  LFOResult;
	UINT16  VRHFOResult;
} __WLAN_ATTRIB_PACK__ hfa384x_LFOStatus_t;

#define HFA384x_TESTRESULT_ALLPASSED    BIT0
#define HFA384x_TESTRESULT_LFO_FAIL     BIT1
#define HFA384x_TESTRESULT_VR_HF0_FAIL  BIT2
#define HFA384x_HOST_FIRM_COORDINATE    BIT7
#define HFA384x_TESTRESULT_COORDINATE   BIT15

/*-- Information Record: LEDControl --*/
typedef struct hfa384x_LEDControl
{
	UINT16  searching_on;
	UINT16  searching_off;
	UINT16  assoc_on;
	UINT16  assoc_off;
	UINT16  activity;
} __WLAN_ATTRIB_PACK__ hfa384x_LEDControl_t;

/*--------------------------------------------------------------------
                 FRAME DESCRIPTORS AND FRAME STRUCTURES

FRAME DESCRIPTORS: Offsets

----------------------------------------------------------------------
Control Info (offset 44-51)
--------------------------------------------------------------------*/
#define		HFA384x_FD_STATUS_OFF			((UINT16)0x44)
#define		HFA384x_FD_TIME_OFF			((UINT16)0x46)
#define		HFA384x_FD_SWSUPPORT_OFF		((UINT16)0x4A)
#define		HFA384x_FD_SILENCE_OFF			((UINT16)0x4A)
#define		HFA384x_FD_SIGNAL_OFF			((UINT16)0x4B)
#define		HFA384x_FD_RATE_OFF			((UINT16)0x4C)
#define		HFA384x_FD_RXFLOW_OFF			((UINT16)0x4D)
#define		HFA384x_FD_RESERVED_OFF			((UINT16)0x4E)
#define		HFA384x_FD_TXCONTROL_OFF		((UINT16)0x50)
/*--------------------------------------------------------------------
802.11 Header (offset 52-6B)
--------------------------------------------------------------------*/
#define		HFA384x_FD_FRAMECONTROL_OFF		((UINT16)0x52)
#define		HFA384x_FD_DURATIONID_OFF		((UINT16)0x54)
#define		HFA384x_FD_ADDRESS1_OFF			((UINT16)0x56)
#define		HFA384x_FD_ADDRESS2_OFF			((UINT16)0x5C)
#define		HFA384x_FD_ADDRESS3_OFF			((UINT16)0x62)
#define		HFA384x_FD_SEQCONTROL_OFF		((UINT16)0x68)
#define		HFA384x_FD_ADDRESS4_OFF			((UINT16)0x6A)
#define		HFA384x_FD_DATALEN_OFF			((UINT16)0x70)
/*--------------------------------------------------------------------
802.3 Header (offset 72-7F)
--------------------------------------------------------------------*/
#define		HFA384x_FD_DESTADDRESS_OFF		((UINT16)0x72)
#define		HFA384x_FD_SRCADDRESS_OFF		((UINT16)0x78)
#define		HFA384x_FD_DATALENGTH_OFF		((UINT16)0x7E)

/*--------------------------------------------------------------------
FRAME STRUCTURES: Communication Frames
----------------------------------------------------------------------
Communication Frames: Transmit Frames
--------------------------------------------------------------------*/
/*-- Communication Frame: Transmit Frame Structure --*/
typedef struct hfa384x_tx_frame
{
	UINT16	status;
	UINT16	reserved1;
	UINT16	reserved2;
	UINT32	sw_support;
	UINT8	tx_retrycount;
	UINT8   tx_rate;
	UINT16	tx_control;

	/*-- 802.11 Header Information --*/

	UINT16	frame_control;
	UINT16	duration_id;
	UINT8	address1[6];
	UINT8	address2[6];
	UINT8	address3[6];
	UINT16	sequence_control;
	UINT8	address4[6];
	UINT16	data_len; /* little endian format */

	/*-- 802.3 Header Information --*/

	UINT8	dest_addr[6];
	UINT8	src_addr[6];
	UINT16	data_length; /* big endian format */
} __WLAN_ATTRIB_PACK__ hfa384x_tx_frame_t;
/*--------------------------------------------------------------------
Communication Frames: Field Masks for Transmit Frames
--------------------------------------------------------------------*/
/*-- Status Field --*/
#define		HFA384x_TXSTATUS_ACKERR			((UINT16)BIT5)
#define		HFA384x_TXSTATUS_FORMERR		((UINT16)BIT3)
#define		HFA384x_TXSTATUS_DISCON			((UINT16)BIT2)
#define		HFA384x_TXSTATUS_AGEDERR		((UINT16)BIT1)
#define		HFA384x_TXSTATUS_RETRYERR		((UINT16)BIT0)
/*-- Transmit Control Field --*/
#define		HFA384x_TX_CFPOLL			((UINT16)BIT12)
#define		HFA384x_TX_PRST				((UINT16)BIT11)
#define		HFA384x_TX_MACPORT			((UINT16)(BIT10 | BIT9 | BIT8))
#define		HFA384x_TX_NOENCRYPT			((UINT16)BIT7)
#define		HFA384x_TX_RETRYSTRAT			((UINT16)(BIT6 | BIT5))
#define		HFA384x_TX_STRUCTYPE			((UINT16)(BIT4 | BIT3))
#define		HFA384x_TX_TXEX				((UINT16)BIT2)
#define		HFA384x_TX_TXOK				((UINT16)BIT1)
/*--------------------------------------------------------------------
Communication Frames: Test/Get/Set Field Values for Transmit Frames
--------------------------------------------------------------------*/
/*-- Status Field --*/
#define HFA384x_TXSTATUS_ISERROR(v)	\
	(((UINT16)(v))&\
	(HFA384x_TXSTATUS_ACKERR|HFA384x_TXSTATUS_FORMERR|\
	HFA384x_TXSTATUS_DISCON|HFA384x_TXSTATUS_AGEDERR|\
	HFA384x_TXSTATUS_RETRYERR))

#define	HFA384x_TXSTATUS_ISACKERR(v)	((UINT16)(((UINT16)(v)) & HFA384x_TXSTATUS_ACKERR))
#define	HFA384x_TXSTATUS_ISFORMERR(v)	((UINT16)(((UINT16)(v)) & HFA384x_TXSTATUS_FORMERR))
#define	HFA384x_TXSTATUS_ISDISCON(v)	((UINT16)(((UINT16)(v)) & HFA384x_TXSTATUS_DISCON))
#define	HFA384x_TXSTATUS_ISAGEDERR(v)	((UINT16)(((UINT16)(v)) & HFA384x_TXSTATUS_AGEDERR))
#define	HFA384x_TXSTATUS_ISRETRYERR(v)	((UINT16)(((UINT16)(v)) & HFA384x_TXSTATUS_RETRYERR))

#define	HFA384x_TX_GET(v,m,s)		((((UINT16)(v))&((UINT16)(m)))>>((UINT16)(s)))
#define	HFA384x_TX_SET(v,m,s)		((((UINT16)(v))<<((UINT16)(s)))&((UINT16)(m)))

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
	UINT16	status;
	UINT32	time;
	UINT8	silence;
	UINT8	signal;
	UINT8	rate;
	UINT8	rx_flow;
	UINT16	reserved1;
	UINT16	reserved2;

	/*-- 802.11 Header Information (802.11 byte order) --*/
	UINT16	frame_control;
	UINT16	duration_id;
	UINT8	address1[6];
	UINT8	address2[6];
	UINT8	address3[6];
	UINT16	sequence_control;
	UINT8	address4[6];
	UINT16	data_len; /* hfa384x (little endian) format */

	/*-- 802.3 Header Information --*/
	UINT8	dest_addr[6];
	UINT8	src_addr[6];
	UINT16	data_length; /* IEEE? (big endian) format */
} __WLAN_ATTRIB_PACK__ hfa384x_rx_frame_t;
/*--------------------------------------------------------------------
Communication Frames: Field Masks for Receive Frames
--------------------------------------------------------------------*/
/*-- Offsets --------*/
#define		HFA384x_RX_DATA_LEN_OFF			((UINT16)44)
#define		HFA384x_RX_80211HDR_OFF			((UINT16)14)
#define		HFA384x_RX_DATA_OFF			((UINT16)60)

/*-- Status Fields --*/
#define		HFA384x_RXSTATUS_MSGTYPE		((UINT16)(BIT15 | BIT14 | BIT13))
#define		HFA384x_RXSTATUS_MACPORT		((UINT16)(BIT10 | BIT9 | BIT8))
#define		HFA384x_RXSTATUS_UNDECR			((UINT16)BIT1)
#define		HFA384x_RXSTATUS_FCSERR			((UINT16)BIT0)
/*--------------------------------------------------------------------
Communication Frames: Test/Get/Set Field Values for Receive Frames
--------------------------------------------------------------------*/
#define		HFA384x_RXSTATUS_MSGTYPE_GET(value)	((UINT16)((((UINT16)(value)) & HFA384x_RXSTATUS_MSGTYPE) >> 13))
#define		HFA384x_RXSTATUS_MSGTYPE_SET(value)	((UINT16)(((UINT16)(value)) << 13))
#define		HFA384x_RXSTATUS_MACPORT_GET(value)	((UINT16)((((UINT16)(value)) & HFA384x_RXSTATUS_MACPORT) >> 8))
#define		HFA384x_RXSTATUS_MACPORT_SET(value)	((UINT16)(((UINT16)(value)) << 8))
#define		HFA384x_RXSTATUS_ISUNDECR(value)	((UINT16)(((UINT16)(value)) & HFA384x_RXSTATUS_UNDECR))
#define		HFA384x_RXSTATUS_ISFCSERR(value)	((UINT16)(((UINT16)(value)) & HFA384x_RXSTATUS_FCSERR))
/*--------------------------------------------------------------------
 FRAME STRUCTURES: Information Types and Information Frame Structures
----------------------------------------------------------------------
Information Types
--------------------------------------------------------------------*/
#define		HFA384x_IT_HANDOVERADDR			((UINT16)0xF000UL)
#define		HFA384x_IT_HANDOVERDEAUTHADDRESS	((UINT16)0xF001UL)//AP 1.3.7
#define		HFA384x_IT_COMMTALLIES			((UINT16)0xF100UL)
#define		HFA384x_IT_SCANRESULTS			((UINT16)0xF101UL)
#define		HFA384x_IT_CHINFORESULTS		((UINT16)0xF102UL)
#define		HFA384x_IT_HOSTSCANRESULTS		((UINT16)0xF103UL)
#define		HFA384x_IT_LINKSTATUS			((UINT16)0xF200UL)
#define		HFA384x_IT_ASSOCSTATUS			((UINT16)0xF201UL)
#define		HFA384x_IT_AUTHREQ			((UINT16)0xF202UL)
#define		HFA384x_IT_PSUSERCNT			((UINT16)0xF203UL)
#define		HFA384x_IT_KEYIDCHANGED			((UINT16)0xF204UL)
#define		HFA384x_IT_ASSOCREQ    			((UINT16)0xF205UL)
#define		HFA384x_IT_MICFAILURE  			((UINT16)0xF206UL)

/*--------------------------------------------------------------------
Information Frames Structures
----------------------------------------------------------------------
Information Frames: Notification Frame Structures
--------------------------------------------------------------------*/
/*--  Notification Frame,MAC Mgmt: Handover Address --*/
typedef struct hfa384x_HandoverAddr
{
	UINT16	framelen;
	UINT16	infotype;
	UINT8	handover_addr[WLAN_BSSID_LEN];
} __WLAN_ATTRIB_PACK__ hfa384x_HandoverAddr_t;

/*--  Inquiry Frame, Diagnose: Communication Tallies --*/
typedef struct hfa384x_CommTallies16
{
	UINT16	txunicastframes;
	UINT16	txmulticastframes;
	UINT16	txfragments;
	UINT16	txunicastoctets;
	UINT16	txmulticastoctets;
	UINT16	txdeferredtrans;
	UINT16	txsingleretryframes;
	UINT16	txmultipleretryframes;
	UINT16	txretrylimitexceeded;
	UINT16	txdiscards;
	UINT16	rxunicastframes;
	UINT16	rxmulticastframes;
	UINT16	rxfragments;
	UINT16	rxunicastoctets;
	UINT16	rxmulticastoctets;
	UINT16	rxfcserrors;
	UINT16	rxdiscardsnobuffer;
	UINT16	txdiscardswrongsa;
	UINT16	rxdiscardswepundecr;
	UINT16	rxmsginmsgfrag;
	UINT16	rxmsginbadmsgfrag;
} __WLAN_ATTRIB_PACK__ hfa384x_CommTallies16_t;

typedef struct hfa384x_CommTallies32
{
	UINT32	txunicastframes;
	UINT32	txmulticastframes;
	UINT32	txfragments;
	UINT32	txunicastoctets;
	UINT32	txmulticastoctets;
	UINT32	txdeferredtrans;
	UINT32	txsingleretryframes;
	UINT32	txmultipleretryframes;
	UINT32	txretrylimitexceeded;
	UINT32	txdiscards;
	UINT32	rxunicastframes;
	UINT32	rxmulticastframes;
	UINT32	rxfragments;
	UINT32	rxunicastoctets;
	UINT32	rxmulticastoctets;
	UINT32	rxfcserrors;
	UINT32	rxdiscardsnobuffer;
	UINT32	txdiscardswrongsa;
	UINT32	rxdiscardswepundecr;
	UINT32	rxmsginmsgfrag;
	UINT32	rxmsginbadmsgfrag;
} __WLAN_ATTRIB_PACK__ hfa384x_CommTallies32_t;

/*--  Inquiry Frame, Diagnose: Scan Results & Subfields--*/
typedef struct hfa384x_ScanResultSub
{
	UINT16	chid;
	UINT16	anl;
	UINT16	sl;
	UINT8	bssid[WLAN_BSSID_LEN];
	UINT16	bcnint;
	UINT16	capinfo;
	hfa384x_bytestr32_t	ssid;
	UINT8	supprates[10]; /* 802.11 info element */
	UINT16	proberesp_rate;
} __WLAN_ATTRIB_PACK__ hfa384x_ScanResultSub_t;

typedef struct hfa384x_ScanResult
{
	UINT16	rsvd;
	UINT16	scanreason;
	hfa384x_ScanResultSub_t
		result[HFA384x_SCANRESULT_MAX];
} __WLAN_ATTRIB_PACK__ hfa384x_ScanResult_t;

/*--  Inquiry Frame, Diagnose: ChInfo Results & Subfields--*/
typedef struct hfa384x_ChInfoResultSub
{
	UINT16	chid;
	UINT16	anl;
	UINT16	pnl;
	UINT16	active;
} __WLAN_ATTRIB_PACK__ hfa384x_ChInfoResultSub_t;

#define HFA384x_CHINFORESULT_BSSACTIVE	BIT0
#define HFA384x_CHINFORESULT_PCFACTIVE	BIT1

typedef struct hfa384x_ChInfoResult
{
	UINT16	scanchannels;
	hfa384x_ChInfoResultSub_t
		result[HFA384x_CHINFORESULT_MAX];
} __WLAN_ATTRIB_PACK__ hfa384x_ChInfoResult_t;

/*--  Inquiry Frame, Diagnose: Host Scan Results & Subfields--*/
typedef struct hfa384x_HScanResultSub
{
	UINT16	chid;
	UINT16	anl;
	UINT16	sl;
	UINT8	bssid[WLAN_BSSID_LEN];
	UINT16	bcnint;
	UINT16	capinfo;
	hfa384x_bytestr32_t	ssid;
	UINT8	supprates[10]; /* 802.11 info element */
	UINT16	proberesp_rate;
	UINT16	atim;
} __WLAN_ATTRIB_PACK__ hfa384x_HScanResultSub_t;

typedef struct hfa384x_HScanResult
{
	UINT16	nresult;
	UINT16	rsvd;
	hfa384x_HScanResultSub_t
		result[HFA384x_HSCANRESULT_MAX];
} __WLAN_ATTRIB_PACK__ hfa384x_HScanResult_t;

/*--  Unsolicited Frame, MAC Mgmt: LinkStatus --*/

#define HFA384x_LINK_NOTCONNECTED	((UINT16)0)
#define HFA384x_LINK_CONNECTED		((UINT16)1)
#define HFA384x_LINK_DISCONNECTED	((UINT16)2)
#define HFA384x_LINK_AP_CHANGE		((UINT16)3)
#define HFA384x_LINK_AP_OUTOFRANGE	((UINT16)4)
#define HFA384x_LINK_AP_INRANGE		((UINT16)5)
#define HFA384x_LINK_ASSOCFAIL		((UINT16)6)

typedef struct hfa384x_LinkStatus
{
	UINT16	linkstatus;
} __WLAN_ATTRIB_PACK__ hfa384x_LinkStatus_t;


/*--  Unsolicited Frame, MAC Mgmt: AssociationStatus (--*/

#define HFA384x_ASSOCSTATUS_STAASSOC	((UINT16)1)
#define HFA384x_ASSOCSTATUS_REASSOC	((UINT16)2)
#define HFA384x_ASSOCSTATUS_DISASSOC	((UINT16)3)
#define HFA384x_ASSOCSTATUS_ASSOCFAIL	((UINT16)4)
#define HFA384x_ASSOCSTATUS_AUTHFAIL	((UINT16)5)

typedef struct hfa384x_AssocStatus
{
	UINT16	assocstatus;
	UINT8	sta_addr[WLAN_ADDR_LEN];
	/* old_ap_addr is only valid if assocstatus == 2 */
	UINT8	old_ap_addr[WLAN_ADDR_LEN];
	UINT16	reason;
	UINT16	reserved;
} __WLAN_ATTRIB_PACK__ hfa384x_AssocStatus_t;

/*--  Unsolicited Frame, MAC Mgmt: AuthRequest (AP Only) --*/

typedef struct hfa384x_AuthRequest
{
	UINT8	sta_addr[WLAN_ADDR_LEN];
	UINT16	algorithm;
} __WLAN_ATTRIB_PACK__ hfa384x_AuthReq_t;

/*--  Unsolicited Frame, MAC Mgmt: AssocRequest (AP Only) --*/

typedef struct hfa384x_AssocRequest
{
	UINT8	sta_addr[WLAN_ADDR_LEN];
	UINT16	type;
	UINT8   wpa_data[80];
} __WLAN_ATTRIB_PACK__ hfa384x_AssocReq_t;


#define HFA384x_ASSOCREQ_TYPE_ASSOC     0
#define HFA384x_ASSOCREQ_TYPE_REASSOC   1

/*--  Unsolicited Frame, MAC Mgmt: MIC Failure  (AP Only) --*/

typedef struct hfa384x_MicFailure
{
	UINT8	sender[WLAN_ADDR_LEN];
	UINT8	dest[WLAN_ADDR_LEN];
} __WLAN_ATTRIB_PACK__ hfa384x_MicFailure_t;

/*--  Unsolicited Frame, MAC Mgmt: PSUserCount (AP Only) --*/

typedef struct hfa384x_PSUserCount
{
	UINT16	usercnt;
} __WLAN_ATTRIB_PACK__ hfa384x_PSUserCount_t;

typedef struct hfa384x_KeyIDChanged
{
	UINT8	sta_addr[WLAN_ADDR_LEN];
	UINT16	keyid;
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
	UINT16			framelen;
	UINT16			infotype;
	hfa384x_infodata_t	info;
} __WLAN_ATTRIB_PACK__ hfa384x_InfFrame_t;

#if (WLAN_HOSTIF == WLAN_USB)
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
	UINT8			data[WLAN_DATA_MAXLEN];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_txfrm_t;

typedef struct hfa384x_usb_cmdreq {
	UINT16		type;
	UINT16		cmd;
	UINT16		parm0;
	UINT16		parm1;
	UINT16		parm2;
	UINT8		pad[54];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_cmdreq_t;

typedef struct hfa384x_usb_wridreq {
	UINT16		type;
	UINT16		frmlen;
	UINT16		rid;
	UINT8		data[HFA384x_RIDDATA_MAXLEN];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_wridreq_t;

typedef struct hfa384x_usb_rridreq {
	UINT16		type;
	UINT16		frmlen;
	UINT16		rid;
	UINT8		pad[58];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_rridreq_t;

typedef struct hfa384x_usb_wmemreq {
	UINT16		type;
	UINT16		frmlen;
	UINT16		offset;
	UINT16		page;
	UINT8		data[HFA384x_USB_RWMEM_MAXLEN];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_wmemreq_t;

typedef struct hfa384x_usb_rmemreq {
	UINT16		type;
	UINT16		frmlen;
	UINT16		offset;
	UINT16		page;
	UINT8		pad[56];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_rmemreq_t;

/*------------------------------------*/
/* Response (bulk IN) packet contents */

typedef struct hfa384x_usb_rxfrm {
	hfa384x_rx_frame_t	desc;
	UINT8			data[WLAN_DATA_MAXLEN];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_rxfrm_t;

typedef struct hfa384x_usb_infofrm {
	UINT16			type;
	hfa384x_InfFrame_t	info;
} __WLAN_ATTRIB_PACK__ hfa384x_usb_infofrm_t;

typedef struct hfa384x_usb_statusresp {
	UINT16		type;
	UINT16		status;
	UINT16		resp0;
	UINT16		resp1;
	UINT16		resp2;
} __WLAN_ATTRIB_PACK__ hfa384x_usb_cmdresp_t;

typedef hfa384x_usb_cmdresp_t hfa384x_usb_wridresp_t;

typedef struct hfa384x_usb_rridresp {
	UINT16		type;
	UINT16		frmlen;
	UINT16		rid;
	UINT8		data[HFA384x_RIDDATA_MAXLEN];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_rridresp_t;

typedef hfa384x_usb_cmdresp_t hfa384x_usb_wmemresp_t;

typedef struct hfa384x_usb_rmemresp {
	UINT16		type;
	UINT16		frmlen;
	UINT8		data[HFA384x_USB_RWMEM_MAXLEN];
} __WLAN_ATTRIB_PACK__ hfa384x_usb_rmemresp_t;

typedef struct hfa384x_usb_bufavail {
	UINT16		type;
	UINT16		frmlen;
} __WLAN_ATTRIB_PACK__ hfa384x_usb_bufavail_t;

typedef struct hfa384x_usb_error {
	UINT16		type;
	UINT16		errortype;
} __WLAN_ATTRIB_PACK__ hfa384x_usb_error_t;

/*----------------------------------------------------------*/
/* Unions for packaging all the known packet types together */

typedef union hfa384x_usbout {
	UINT16			type;
	hfa384x_usb_txfrm_t	txfrm;
	hfa384x_usb_cmdreq_t	cmdreq;
	hfa384x_usb_wridreq_t	wridreq;
	hfa384x_usb_rridreq_t	rridreq;
	hfa384x_usb_wmemreq_t	wmemreq;
	hfa384x_usb_rmemreq_t	rmemreq;
} __WLAN_ATTRIB_PACK__ hfa384x_usbout_t;

typedef union hfa384x_usbin {
	UINT16			type;
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
	UINT8			boguspad[3000];
} __WLAN_ATTRIB_PACK__ hfa384x_usbin_t;

#endif /* WLAN_USB */

/*--------------------------------------------------------------------
PD record structures.
--------------------------------------------------------------------*/

typedef struct hfa384x_pdr_pcb_partnum
{
	UINT8	num[8];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_pcb_partnum_t;

typedef struct hfa384x_pdr_pcb_tracenum
{
	UINT8	num[8];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_pcb_tracenum_t;

typedef struct hfa384x_pdr_nic_serial
{
	UINT8	num[12];
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
	UINT8	size[12]; /* units of KB */
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_nic_ramsize_t;

typedef struct hfa384x_pdr_mfisuprange
{
	UINT16	id;
	UINT16	variant;
	UINT16	bottom;
	UINT16	top;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_mfisuprange_t;

typedef struct hfa384x_pdr_cfisuprange
{
	UINT16	id;
	UINT16	variant;
	UINT16	bottom;
	UINT16	top;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_cfisuprange_t;

typedef struct hfa384x_pdr_nicid
{
	UINT16	id;
	UINT16	variant;
	UINT16	major;
	UINT16	minor;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_nicid_t;


typedef struct hfa384x_pdr_refdac_measurements
{
	UINT16	value[0];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_refdac_measurements_t;

typedef struct hfa384x_pdr_vgdac_measurements
{
	UINT16	value[0];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_vgdac_measurements_t;

typedef struct hfa384x_pdr_level_comp_measurements
{
	UINT16	value[0];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_level_compc_measurements_t;

typedef struct hfa384x_pdr_mac_address
{
	UINT8	addr[6];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_mac_address_t;

typedef struct hfa384x_pdr_mkk_callname
{
	UINT8	callname[8];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_mkk_callname_t;

typedef struct hfa384x_pdr_regdomain
{
	UINT16	numdomains;
	UINT16	domain[5];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_regdomain_t;

typedef struct hfa384x_pdr_allowed_channel
{
	UINT16	ch_bitmap;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_allowed_channel_t;

typedef struct hfa384x_pdr_default_channel
{
	UINT16	channel;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_default_channel_t;

typedef struct hfa384x_pdr_privacy_option
{
	UINT16	available;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_privacy_option_t;

typedef struct hfa384x_pdr_temptype
{
	UINT16	type;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_temptype_t;

typedef struct hfa384x_pdr_refdac_setup
{
	UINT16	ch_value[14];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_refdac_setup_t;

typedef struct hfa384x_pdr_vgdac_setup
{
	UINT16	ch_value[14];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_vgdac_setup_t;

typedef struct hfa384x_pdr_level_comp_setup
{
	UINT16	ch_value[14];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_level_comp_setup_t;

typedef struct hfa384x_pdr_trimdac_setup
{
	UINT16	trimidac;
	UINT16	trimqdac;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_trimdac_setup_t;

typedef struct hfa384x_pdr_ifr_setting
{
	UINT16	value[3];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_ifr_setting_t;

typedef struct hfa384x_pdr_rfr_setting
{
	UINT16	value[3];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_rfr_setting_t;

typedef struct hfa384x_pdr_hfa3861_baseline
{
	UINT16	value[50];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_baseline_t;

typedef struct hfa384x_pdr_hfa3861_shadow
{
	UINT32	value[32];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_shadow_t;

typedef struct hfa384x_pdr_hfa3861_ifrf
{
	UINT32	value[20];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_ifrf_t;

typedef struct hfa384x_pdr_hfa3861_chcalsp
{
	UINT16	value[14];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_chcalsp_t;

typedef struct hfa384x_pdr_hfa3861_chcali
{
	UINT16	value[17];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_chcali_t;

typedef struct hfa384x_pdr_hfa3861_nic_config
{
	UINT16	config_bitmap;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_nic_config_t;

typedef struct hfa384x_pdr_hfo_delay
{
	UINT8   hfo_delay;
} __WLAN_ATTRIB_PACK__ hfa384x_hfo_delay_t;

typedef struct hfa384x_pdr_hfa3861_manf_testsp
{
	UINT16	value[30];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_manf_testsp_t;

typedef struct hfa384x_pdr_hfa3861_manf_testi
{
	UINT16	value[30];
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_hfa3861_manf_testi_t;

typedef struct hfa384x_end_of_pda
{
	UINT16	crc;
} __WLAN_ATTRIB_PACK__ hfa384x_pdr_end_of_pda_t;

typedef struct hfa384x_pdrec
{
	UINT16	len; /* in words */
	UINT16	code;
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
	UINT16	status;
	UINT16	resp0;
	UINT16	resp1;
	UINT16	resp2;
} hfa384x_cmdresult_t;

#if (WLAN_HOSTIF == WLAN_USB)

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
	UINT16		rid;
	const void	*riddata;
	UINT		riddata_len;
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
#endif

typedef struct hfa484x_metacmd
{
	UINT16		cmd;

	UINT16          parm0;
	UINT16          parm1;
	UINT16          parm2;

#if 0 //XXX cmd irq stuff
	UINT16          bulkid;         /* what RID/FID to copy down. */
	int             bulklen;        /* how much to copy from BAP */
        char            *bulkdata;      /* And to where? */
#endif

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
	UINT	cnt;
	UINT8	addr[WLAN_AUTH_MAX][WLAN_ADDR_LEN];
	UINT8	assoc[WLAN_AUTH_MAX];
} prism2sta_authlist_t;

typedef struct prism2sta_accesslist
{
	UINT	modify;
	UINT	cnt;
	UINT8	addr[WLAN_ACCESS_MAX][WLAN_ADDR_LEN];
	UINT	cnt1;
	UINT8	addr1[WLAN_ACCESS_MAX][WLAN_ADDR_LEN];
} prism2sta_accesslist_t;

typedef struct hfa384x
{
#if (WLAN_HOSTIF != WLAN_USB)
	/* Resource config */
	UINT32			iobase;
	char			__iomem *membase;
	UINT32			irq;
#else
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
#endif /* !USB */

#if (WLAN_HOSTIF == WLAN_PCMCIA)
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	struct pcmcia_device *pdev;
#else
	dev_link_t	*link;
#endif
	dev_node_t	node;
#endif

	int                     sniff_fcs;
	int                     sniff_channel;
	int                     sniff_truncate;
	int                     sniffhdr;

	wait_queue_head_t cmdq;	        /* wait queue itself */

	/* Controller state */
	UINT32		state;
	UINT32		isap;
	UINT8		port_enabled[HFA384x_NUMPORTS_MAX];
#if (WLAN_HOSTIF != WLAN_USB)
	UINT		auxen;
	UINT            isram16;
#endif /* !USB */

	/* Download support */
	UINT				dlstate;
	hfa384x_downloadbuffer_t	bufinfo;
	UINT16				dltimeout;

#if (WLAN_HOSTIF != WLAN_USB)
	spinlock_t	cmdlock;
	volatile int    cmdflag;        /* wait queue flag */
	hfa384x_metacmd_t *cmddata;      /* for our async callback */

	/* BAP support */
	spinlock_t	baplock;
	struct tasklet_struct   bap_tasklet;

	/* MAC buffer ids */
        UINT16          txfid_head;
        UINT16          txfid_tail;
        UINT            txfid_N;
        UINT16          txfid_queue[HFA384x_DRVR_FIDSTACKLEN_MAX];
	UINT16			infofid;
	struct semaphore	infofid_sem;
#endif /* !USB */

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

	UINT16 link_status;
	UINT16 link_status_new;
	struct sk_buff_head        authq;

	/* And here we have stuff that used to be in priv */

	/* State variables */
	UINT		presniff_port_type;
	UINT16		presniff_wepflags;
	UINT32		dot11_desired_bss_type;
	int		ap;	/* AP flag: 0 - Station, 1 - Access Point. */

	int             dbmadjust;

	/* Group Addresses - right now, there are up to a total
	of MAX_GRP_ADDR group addresses */
	UINT8		dot11_grp_addr[MAX_GRP_ADDR][WLAN_ADDR_LEN];
	UINT		dot11_grpcnt;

	/* Component Identities */
	hfa384x_compident_t	ident_nic;
	hfa384x_compident_t	ident_pri_fw;
	hfa384x_compident_t	ident_sta_fw;
	hfa384x_compident_t	ident_ap_fw;
	UINT16			mm_mods;

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

	UINT32			psusercount;  /* Power save user count. */
	hfa384x_CommTallies32_t	tallies;      /* Communication tallies. */
	UINT8			comment[WLAN_COMMENT_MAX+1]; /* User comment */

	/* Channel Info request results (AP only) */
	struct {
		atomic_t		done;
		UINT8			count;
		hfa384x_ChInfoResult_t	results;
	} channel_info;

	hfa384x_InfFrame_t      *scanresults;


        prism2sta_authlist_t	authlist;     /* Authenticated station list. */
	UINT			accessmode;   /* Access mode. */
        prism2sta_accesslist_t	allow;        /* Allowed station list. */
        prism2sta_accesslist_t	deny;         /* Denied station list. */

} hfa384x_t;

/*=============================================================*/
/*--- Function Declarations -----------------------------------*/
/*=============================================================*/
#if (WLAN_HOSTIF == WLAN_USB)
void
hfa384x_create(
	hfa384x_t *hw,
	struct usb_device *usb);
#else
void
hfa384x_create(
	hfa384x_t *hw,
	UINT irq,
	UINT32 iobase,
	UINT8 __iomem *membase);
#endif

void hfa384x_destroy(hfa384x_t *hw);

irqreturn_t
hfa384x_interrupt(int irq, void *dev_id PT_REGS);
int
hfa384x_corereset( hfa384x_t *hw, int holdtime, int settletime, int genesis);
int
hfa384x_drvr_chinforesults( hfa384x_t *hw);
int
hfa384x_drvr_commtallies( hfa384x_t *hw);
int
hfa384x_drvr_disable(hfa384x_t *hw, UINT16 macport);
int
hfa384x_drvr_enable(hfa384x_t *hw, UINT16 macport);
int
hfa384x_drvr_flashdl_enable(hfa384x_t *hw);
int
hfa384x_drvr_flashdl_disable(hfa384x_t *hw);
int
hfa384x_drvr_flashdl_write(hfa384x_t *hw, UINT32 daddr, void* buf, UINT32 len);
int
hfa384x_drvr_getconfig(hfa384x_t *hw, UINT16 rid, void *buf, UINT16 len);
int
hfa384x_drvr_handover( hfa384x_t *hw, UINT8 *addr);
int
hfa384x_drvr_hostscanresults( hfa384x_t *hw);
int
hfa384x_drvr_low_level(hfa384x_t *hw, hfa384x_metacmd_t *cmd);
int
hfa384x_drvr_mmi_read(hfa384x_t *hw, UINT32 address, UINT32 *result);
int
hfa384x_drvr_mmi_write(hfa384x_t *hw, UINT32 address, UINT32 data);
int
hfa384x_drvr_ramdl_enable(hfa384x_t *hw, UINT32 exeaddr);
int
hfa384x_drvr_ramdl_disable(hfa384x_t *hw);
int
hfa384x_drvr_ramdl_write(hfa384x_t *hw, UINT32 daddr, void* buf, UINT32 len);
int
hfa384x_drvr_readpda(hfa384x_t *hw, void *buf, UINT len);
int
hfa384x_drvr_scanresults( hfa384x_t *hw);

int
hfa384x_drvr_setconfig(hfa384x_t *hw, UINT16 rid, void *buf, UINT16 len);

static inline int
hfa384x_drvr_getconfig16(hfa384x_t *hw, UINT16 rid, void *val)
{
	int		result = 0;
	result = hfa384x_drvr_getconfig(hw, rid, val, sizeof(UINT16));
	if ( result == 0 ) {
		*((UINT16*)val) = hfa384x2host_16(*((UINT16*)val));
	}
	return result;
}

static inline int
hfa384x_drvr_getconfig32(hfa384x_t *hw, UINT16 rid, void *val)
{
	int		result = 0;

	result = hfa384x_drvr_getconfig(hw, rid, val, sizeof(UINT32));
	if ( result == 0 ) {
		*((UINT32*)val) = hfa384x2host_32(*((UINT32*)val));
	}

	return result;
}

static inline int
hfa384x_drvr_setconfig16(hfa384x_t *hw, UINT16 rid, UINT16 val)
{
	UINT16 value = host2hfa384x_16(val);
	return hfa384x_drvr_setconfig(hw, rid, &value, sizeof(value));
}

static inline int
hfa384x_drvr_setconfig32(hfa384x_t *hw, UINT16 rid, UINT32 val)
{
	UINT32 value = host2hfa384x_32(val);
	return hfa384x_drvr_setconfig(hw, rid, &value, sizeof(value));
}

#if (WLAN_HOSTIF == WLAN_USB)
int
hfa384x_drvr_getconfig_async(hfa384x_t     *hw,
                              UINT16        rid,
                              ctlx_usercb_t usercb,
                              void          *usercb_data);

int
hfa384x_drvr_setconfig_async(hfa384x_t *hw,
                              UINT16 rid,
                              void *buf,
                              UINT16 len,
                              ctlx_usercb_t usercb,
                              void *usercb_data);
#else
static inline int
hfa384x_drvr_setconfig_async(hfa384x_t *hw, UINT16 rid, void *buf, UINT16 len,
			     void *ptr1, void *ptr2)
{
         (void)ptr1;
         (void)ptr2;
         return hfa384x_drvr_setconfig(hw, rid, buf, len);
}
#endif

static inline int
hfa384x_drvr_setconfig16_async(hfa384x_t *hw, UINT16 rid, UINT16 val)
{
	UINT16 value = host2hfa384x_16(val);
	return hfa384x_drvr_setconfig_async(hw, rid, &value, sizeof(value),
					    NULL , NULL);
}

static inline int
hfa384x_drvr_setconfig32_async(hfa384x_t *hw, UINT16 rid, UINT32 val)
{
	UINT32 value = host2hfa384x_32(val);
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
hfa384x_cmd_enable(hfa384x_t *hw, UINT16 macport);
int
hfa384x_cmd_disable(hfa384x_t *hw, UINT16 macport);
int
hfa384x_cmd_diagnose(hfa384x_t *hw);
int
hfa384x_cmd_allocate(hfa384x_t *hw, UINT16 len);
int
hfa384x_cmd_transmit(hfa384x_t *hw, UINT16 reclaim, UINT16 qos, UINT16 fid);
int
hfa384x_cmd_clearpersist(hfa384x_t *hw, UINT16 fid);
int
hfa384x_cmd_notify(hfa384x_t *hw, UINT16 reclaim, UINT16 fid, void *buf, UINT16 len);
int
hfa384x_cmd_inquire(hfa384x_t *hw, UINT16 fid);
int
hfa384x_cmd_access(hfa384x_t *hw, UINT16 write, UINT16 rid, void *buf, UINT16 len);
int
hfa384x_cmd_monitor(hfa384x_t *hw, UINT16 enable);
int
hfa384x_cmd_download(
	hfa384x_t *hw,
	UINT16 mode,
	UINT16 lowaddr,
	UINT16 highaddr,
	UINT16 codelen);
int
hfa384x_cmd_aux_enable(hfa384x_t *hw, int force);
int
hfa384x_cmd_aux_disable(hfa384x_t *hw);
int
hfa384x_copy_from_bap(
	hfa384x_t *hw,
	UINT16	bap,
	UINT16	id,
	UINT16	offset,
	void	*buf,
	UINT	len);
int
hfa384x_copy_to_bap(
	hfa384x_t *hw,
	UINT16	bap,
	UINT16	id,
	UINT16	offset,
	void	*buf,
	UINT	len);
void
hfa384x_copy_from_aux(
	hfa384x_t *hw,
	UINT32	cardaddr,
	UINT32	auxctl,
	void	*buf,
	UINT	len);
void
hfa384x_copy_to_aux(
	hfa384x_t *hw,
	UINT32	cardaddr,
	UINT32	auxctl,
	void	*buf,
	UINT	len);

#if (WLAN_HOSTIF != WLAN_USB)

/*
   HFA384x is a LITTLE ENDIAN part.

   the get/setreg functions implicitly byte-swap the data to LE.
   the _noswap variants do not perform a byte-swap on the data.
*/

static inline UINT16
__hfa384x_getreg(hfa384x_t *hw, UINT reg);

static inline void
__hfa384x_setreg(hfa384x_t *hw, UINT16 val, UINT reg);

static inline UINT16
__hfa384x_getreg_noswap(hfa384x_t *hw, UINT reg);

static inline void
__hfa384x_setreg_noswap(hfa384x_t *hw, UINT16 val, UINT reg);

#ifdef REVERSE_ENDIAN
#define hfa384x_getreg __hfa384x_getreg_noswap
#define hfa384x_setreg __hfa384x_setreg_noswap
#define hfa384x_getreg_noswap __hfa384x_getreg
#define hfa384x_setreg_noswap __hfa384x_setreg
#else
#define hfa384x_getreg __hfa384x_getreg
#define hfa384x_setreg __hfa384x_setreg
#define hfa384x_getreg_noswap __hfa384x_getreg_noswap
#define hfa384x_setreg_noswap __hfa384x_setreg_noswap
#endif

/*----------------------------------------------------------------
* hfa384x_getreg
*
* Retrieve the value of one of the MAC registers.  Done here
* because different PRISM2 MAC parts use different buses and such.
* NOTE: This function returns the value in HOST ORDER!!!!!!
*
* Arguments:
*       hw         MAC part structure
*       reg        Register identifier (offset for I/O based i/f)
*
* Returns:
*       Value from the register in HOST ORDER!!!!
----------------------------------------------------------------*/
static inline UINT16
__hfa384x_getreg(hfa384x_t *hw, UINT reg)
{
/*	printk(KERN_DEBUG "Reading from 0x%0x\n", hw->membase + reg); */
#if ((WLAN_HOSTIF == WLAN_PCMCIA) || (WLAN_HOSTIF == WLAN_PLX))
	return wlan_inw_le16_to_cpu(hw->iobase+reg);
#elif (WLAN_HOSTIF == WLAN_PCI)
	return __le16_to_cpu(readw(hw->membase + reg));
#endif
}

/*----------------------------------------------------------------
* hfa384x_setreg
*
* Set the value of one of the MAC registers.  Done here
* because different PRISM2 MAC parts use different buses and such.
* NOTE: This function assumes the value is in HOST ORDER!!!!!!
*
* Arguments:
*       hw	MAC part structure
*	val	Value, in HOST ORDER!!, to put in the register
*       reg	Register identifier (offset for I/O based i/f)
*
* Returns:
*       Nothing
----------------------------------------------------------------*/
static inline void
__hfa384x_setreg(hfa384x_t *hw, UINT16 val, UINT reg)
{
#if ((WLAN_HOSTIF == WLAN_PCMCIA) || (WLAN_HOSTIF == WLAN_PLX))
	wlan_outw_cpu_to_le16( val, hw->iobase + reg);
	return;
#elif (WLAN_HOSTIF == WLAN_PCI)
	writew(__cpu_to_le16(val), hw->membase + reg);
	return;
#endif
}


/*----------------------------------------------------------------
* hfa384x_getreg_noswap
*
* Retrieve the value of one of the MAC registers.  Done here
* because different PRISM2 MAC parts use different buses and such.
*
* Arguments:
*       hw         MAC part structure
*       reg        Register identifier (offset for I/O based i/f)
*
* Returns:
*       Value from the register.
----------------------------------------------------------------*/
static inline UINT16
__hfa384x_getreg_noswap(hfa384x_t *hw, UINT reg)
{
#if ((WLAN_HOSTIF == WLAN_PCMCIA) || (WLAN_HOSTIF == WLAN_PLX))
	return wlan_inw(hw->iobase+reg);
#elif (WLAN_HOSTIF == WLAN_PCI)
	return readw(hw->membase + reg);
#endif
}


/*----------------------------------------------------------------
* hfa384x_setreg_noswap
*
* Set the value of one of the MAC registers.  Done here
* because different PRISM2 MAC parts use different buses and such.
*
* Arguments:
*       hw	MAC part structure
*	val	Value to put in the register
*       reg	Register identifier (offset for I/O based i/f)
*
* Returns:
*       Nothing
----------------------------------------------------------------*/
static inline void
__hfa384x_setreg_noswap(hfa384x_t *hw, UINT16 val, UINT reg)
{
#if ((WLAN_HOSTIF == WLAN_PCMCIA) || (WLAN_HOSTIF == WLAN_PLX))
	wlan_outw( val, hw->iobase + reg);
	return;
#elif (WLAN_HOSTIF == WLAN_PCI)
	writew(val, hw->membase + reg);
	return;
#endif
}


static inline void hfa384x_events_all(hfa384x_t *hw)
{
	hfa384x_setreg(hw,
		       HFA384x_INT_NORMAL
#ifdef CMD_IRQ
		       | HFA384x_INTEN_CMD_SET(1)
#endif
		       ,
		       HFA384x_INTEN);

}

static inline void hfa384x_events_nobap(hfa384x_t *hw)
{
	hfa384x_setreg(hw,
		        (HFA384x_INT_NORMAL & ~HFA384x_INT_BAP_OP)
#ifdef CMD_IRQ
		       | HFA384x_INTEN_CMD_SET(1)
#endif
		       ,
		       HFA384x_INTEN);

}

#endif /* WLAN_HOSTIF != WLAN_USB */
#endif /* __KERNEL__ */

#endif  /* _HFA384x_H */
