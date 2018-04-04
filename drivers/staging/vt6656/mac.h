// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * File: mac.h
 *
 * Purpose: MAC routines
 *
 * Author: Tevin Chen
 *
 * Date: May 21, 1996
 *
 * Revision History:
 *      07-01-2003 Bryan YC Fan:  Re-write codes to support VT3253 spec.
 *      08-25-2003 Kyle Hsu:      Porting MAC functions from sim53.
 *      09-03-2003 Bryan YC Fan:  Add MACvDisableProtectMD & MACvEnableProtectMD
 */

#ifndef __MAC_H__
#define __MAC_H__

#include "device.h"

#define REV_ID_VT3253_A0	0x00
#define REV_ID_VT3253_A1	0x01
#define REV_ID_VT3253_B0	0x08
#define REV_ID_VT3253_B1	0x09

/* Registers in the MAC */
#define MAC_REG_BISTCMD		0x04
#define MAC_REG_BISTSR0		0x05
#define MAC_REG_BISTSR1		0x06
#define MAC_REG_BISTSR2		0x07
#define MAC_REG_I2MCSR		0x08
#define MAC_REG_I2MTGID		0x09
#define MAC_REG_I2MTGAD		0x0a
#define MAC_REG_I2MCFG		0x0b
#define MAC_REG_I2MDIPT		0x0c
#define MAC_REG_I2MDOPT		0x0e
#define MAC_REG_USBSUS		0x0f

#define MAC_REG_LOCALID		0x14
#define MAC_REG_TESTCFG		0x15
#define MAC_REG_JUMPER0		0x16
#define MAC_REG_JUMPER1		0x17
#define MAC_REG_TMCTL		0x18
#define MAC_REG_TMDATA0		0x1c
#define MAC_REG_TMDATA1		0x1d
#define MAC_REG_TMDATA2		0x1e
#define MAC_REG_TMDATA3		0x1f

/* MAC Parameter related */
#define MAC_REG_LRT		0x20
#define MAC_REG_SRT		0x21
#define MAC_REG_SIFS		0x22
#define MAC_REG_DIFS		0x23
#define MAC_REG_EIFS		0x24
#define MAC_REG_SLOT		0x25
#define MAC_REG_BI		0x26
#define MAC_REG_CWMAXMIN0	0x28
#define MAC_REG_LINKOFFTOTM	0x2a
#define MAC_REG_SWTMOT		0x2b
#define MAC_REG_RTSOKCNT	0x2c
#define MAC_REG_RTSFAILCNT	0x2d
#define MAC_REG_ACKFAILCNT	0x2e
#define MAC_REG_FCSERRCNT	0x2f

/* TSF Related */
#define MAC_REG_TSFCNTR		0x30
#define MAC_REG_NEXTTBTT	0x38
#define MAC_REG_TSFOFST		0x40
#define MAC_REG_TFTCTL		0x48

/* WMAC Control/Status Related */
#define MAC_REG_ENCFG0		0x4c
#define MAC_REG_ENCFG1		0x4d
#define MAC_REG_ENCFG2		0x4e

#define MAC_REG_CFG		0x50
#define MAC_REG_TEST		0x52
#define MAC_REG_HOSTCR		0x54
#define MAC_REG_MACCR		0x55
#define MAC_REG_RCR		0x56
#define MAC_REG_TCR		0x57
#define MAC_REG_IMR		0x58
#define MAC_REG_ISR		0x5c
#define MAC_REG_ISR1		0x5d

/* Power Saving Related */
#define MAC_REG_PSCFG		0x60
#define MAC_REG_PSCTL		0x61
#define MAC_REG_PSPWRSIG	0x62
#define MAC_REG_BBCR13		0x63
#define MAC_REG_AIDATIM		0x64
#define MAC_REG_PWBT		0x66
#define MAC_REG_WAKEOKTMR	0x68
#define MAC_REG_CALTMR		0x69
#define MAC_REG_SYNSPACCNT	0x6a
#define MAC_REG_WAKSYNOPT	0x6b

/* Baseband/IF Control Group */
#define MAC_REG_BBREGCTL	0x6c
#define MAC_REG_CHANNEL		0x6d
#define MAC_REG_BBREGADR	0x6e
#define MAC_REG_BBREGDATA	0x6f
#define MAC_REG_IFREGCTL	0x70
#define MAC_REG_IFDATA		0x71
#define MAC_REG_ITRTMSET	0x74
#define MAC_REG_PAPEDELAY	0x77
#define MAC_REG_SOFTPWRCTL	0x78
#define MAC_REG_SOFTPWRCTL2	0x79
#define MAC_REG_GPIOCTL0	0x7a
#define MAC_REG_GPIOCTL1	0x7b

/* MiscFF PIO related */
#define MAC_REG_MISCFFNDEX	0xbc
#define MAC_REG_MISCFFCTL	0xbe
#define MAC_REG_MISCFFDATA	0xc0

/* MAC Configuration Group */
#define MAC_REG_PAR0		0xc4
#define MAC_REG_PAR4		0xc8
#define MAC_REG_BSSID0		0xcc
#define MAC_REG_BSSID4		0xd0
#define MAC_REG_MAR0		0xd4
#define MAC_REG_MAR4		0xd8

/* MAC RSPPKT INFO Group */
#define MAC_REG_RSPINF_B_1	0xdC
#define MAC_REG_RSPINF_B_2	0xe0
#define MAC_REG_RSPINF_B_5	0xe4
#define MAC_REG_RSPINF_B_11	0xe8
#define MAC_REG_RSPINF_A_6	0xec
#define MAC_REG_RSPINF_A_9	0xee
#define MAC_REG_RSPINF_A_12	0xf0
#define MAC_REG_RSPINF_A_18	0xf2
#define MAC_REG_RSPINF_A_24	0xf4
#define MAC_REG_RSPINF_A_36	0xf6
#define MAC_REG_RSPINF_A_48	0xf8
#define MAC_REG_RSPINF_A_54	0xfa
#define MAC_REG_RSPINF_A_72	0xfc

/* Bits in the I2MCFG EEPROM register */
#define I2MCFG_BOUNDCTL		0x80
#define I2MCFG_WAITCTL		0x20
#define I2MCFG_SCLOECTL		0x10
#define I2MCFG_WBUSYCTL		0x08
#define I2MCFG_NORETRY		0x04
#define I2MCFG_I2MLDSEQ		0x02
#define I2MCFG_I2CMFAST		0x01

/* Bits in the I2MCSR EEPROM register */
#define I2MCSR_EEMW		0x80
#define I2MCSR_EEMR		0x40
#define I2MCSR_AUTOLD		0x08
#define I2MCSR_NACK		0x02
#define I2MCSR_DONE		0x01

/* Bits in the TMCTL register */
#define TMCTL_TSUSP		0x04
#define TMCTL_TMD		0x02
#define TMCTL_TE		0x01

/* Bits in the TFTCTL register */
#define TFTCTL_HWUTSF		0x80
#define TFTCTL_TBTTSYNC		0x40
#define TFTCTL_HWUTSFEN		0x20
#define TFTCTL_TSFCNTRRD	0x10
#define TFTCTL_TBTTSYNCEN	0x08
#define TFTCTL_TSFSYNCEN	0x04
#define TFTCTL_TSFCNTRST	0x02
#define TFTCTL_TSFCNTREN	0x01

/* Bits in the EnhanceCFG_0 register */
#define EnCFG_BBType_a		0x00
#define EnCFG_BBType_b		0x01
#define EnCFG_BBType_g		0x02
#define EnCFG_BBType_MASK	0x03
#define EnCFG_ProtectMd		0x20

/* Bits in the EnhanceCFG_1 register */
#define EnCFG_BcnSusInd		0x01
#define EnCFG_BcnSusClr		0x02

/* Bits in the EnhanceCFG_2 register */
#define EnCFG_NXTBTTCFPSTR	0x01
#define EnCFG_BarkerPream	0x02
#define EnCFG_PktBurstMode	0x04

/* Bits in the CFG register */
#define CFG_TKIPOPT		0x80
#define CFG_RXDMAOPT		0x40
#define CFG_TMOT_SW		0x20
#define CFG_TMOT_HWLONG		0x10
#define CFG_TMOT_HW		0x00
#define CFG_CFPENDOPT		0x08
#define CFG_BCNSUSEN		0x04
#define CFG_NOTXTIMEOUT		0x02
#define CFG_NOBUFOPT		0x01

/* Bits in the TEST register */
#define TEST_LBEXT		0x80
#define TEST_LBINT		0x40
#define TEST_LBNONE		0x00
#define TEST_SOFTINT		0x20
#define TEST_CONTTX		0x10
#define TEST_TXPE		0x08
#define TEST_NAVDIS		0x04
#define TEST_NOCTS		0x02
#define TEST_NOACK		0x01

/* Bits in the HOSTCR register */
#define HOSTCR_TXONST		0x80
#define HOSTCR_RXONST		0x40
#define HOSTCR_ADHOC		0x20
#define HOSTCR_AP		0x10
#define HOSTCR_TXON		0x08
#define HOSTCR_RXON		0x04
#define HOSTCR_MACEN		0x02
#define HOSTCR_SOFTRST		0x01

/* Bits in the MACCR register */
#define MACCR_SYNCFLUSHOK	0x04
#define MACCR_SYNCFLUSH		0x02
#define MACCR_CLRNAV		0x01

/* Bits in the RCR register */
#define RCR_SSID		0x80
#define RCR_RXALLTYPE		0x40
#define RCR_UNICAST		0x20
#define RCR_BROADCAST		0x10
#define RCR_MULTICAST		0x08
#define RCR_WPAERR		0x04
#define RCR_ERRCRC		0x02
#define RCR_BSSID		0x01

/* Bits in the TCR register */
#define TCR_SYNCDCFOPT		0x02
#define TCR_AUTOBCNTX		0x01

/* ISR1 */
#define ISR_GPIO3		0x40
#define ISR_RXNOBUF		0x08
#define ISR_MIBNEARFULL		0x04
#define ISR_SOFTINT		0x02
#define ISR_FETALERR		0x01

#define LEDSTS_STS		0x06
#define LEDSTS_TMLEN		0x78
#define LEDSTS_OFF		0x00
#define LEDSTS_ON		0x02
#define LEDSTS_SLOW		0x04
#define LEDSTS_INTER		0x06

/* ISR0 */
#define ISR_WATCHDOG		0x80
#define ISR_SOFTTIMER		0x40
#define ISR_GPIO0		0x20
#define ISR_TBTT		0x10
#define ISR_RXDMA0		0x08
#define ISR_BNTX		0x04
#define ISR_ACTX		0x01

/* Bits in the PSCFG register */
#define PSCFG_PHILIPMD		0x40
#define PSCFG_WAKECALEN		0x20
#define PSCFG_WAKETMREN		0x10
#define PSCFG_BBPSPROG		0x08
#define PSCFG_WAKESYN		0x04
#define PSCFG_SLEEPSYN		0x02
#define PSCFG_AUTOSLEEP		0x01

/* Bits in the PSCTL register */
#define PSCTL_WAKEDONE		0x20
#define PSCTL_PS		0x10
#define PSCTL_GO2DOZE		0x08
#define PSCTL_LNBCN		0x04
#define PSCTL_ALBCN		0x02
#define PSCTL_PSEN		0x01

/* Bits in the PSPWSIG register */
#define PSSIG_WPE3		0x80
#define PSSIG_WPE2		0x40
#define PSSIG_WPE1		0x20
#define PSSIG_WRADIOPE		0x10
#define PSSIG_SPE3		0x08
#define PSSIG_SPE2		0x04
#define PSSIG_SPE1		0x02
#define PSSIG_SRADIOPE		0x01

/* Bits in the BBREGCTL register */
#define BBREGCTL_DONE		0x04
#define BBREGCTL_REGR		0x02
#define BBREGCTL_REGW		0x01

/* Bits in the IFREGCTL register */
#define IFREGCTL_DONE		0x04
#define IFREGCTL_IFRF		0x02
#define IFREGCTL_REGW		0x01

/* Bits in the SOFTPWRCTL register */
#define SOFTPWRCTL_RFLEOPT	0x08
#define SOFTPWRCTL_TXPEINV	0x02
#define SOFTPWRCTL_SWPECTI	0x01
#define SOFTPWRCTL_SWPAPE	0x20
#define SOFTPWRCTL_SWCALEN	0x10
#define SOFTPWRCTL_SWRADIO_PE	0x08
#define SOFTPWRCTL_SWPE2	0x04
#define SOFTPWRCTL_SWPE1	0x02
#define SOFTPWRCTL_SWPE3	0x01

/* Bits in the GPIOCTL1 register */
#define GPIO3_MD		0x20
#define GPIO3_DATA		0x40
#define GPIO3_INTMD		0x80

/* Bits in the MISCFFCTL register */
#define MISCFFCTL_WRITE		0x0001

/* Loopback mode */
#define MAC_LB_EXT		0x02
#define MAC_LB_INTERNAL		0x01
#define MAC_LB_NONE		0x00

/* Ethernet address filter type */
#define PKT_TYPE_NONE		0x00 /* turn off receiver */
#define PKT_TYPE_ALL_MULTICAST	0x80
#define PKT_TYPE_PROMISCUOUS	0x40
#define PKT_TYPE_DIRECTED	0x20 /* obselete */
#define PKT_TYPE_BROADCAST	0x10
#define PKT_TYPE_MULTICAST	0x08
#define PKT_TYPE_ERROR_WPA	0x04
#define PKT_TYPE_ERROR_CRC	0x02
#define PKT_TYPE_BSSID		0x01

#define Default_BI              0x200

/* MiscFIFO Offset */
#define MISCFIFO_KEYETRY0	32
#define MISCFIFO_KEYENTRYSIZE	22

#define MAC_REVISION_A0		0x00
#define MAC_REVISION_A1		0x01

struct vnt_mac_set_key {
	union {
		struct {
			u8 addr[ETH_ALEN];
			__le16 key_ctl;
		} write __packed;
		u32 swap[2];
	} u;
	u8 key[WLAN_KEY_LEN_CCMP];
} __packed;

void vnt_mac_set_filter(struct vnt_private *priv, u64 mc_filter);
void vnt_mac_shutdown(struct vnt_private *priv);
void vnt_mac_set_bb_type(struct vnt_private *priv, u8 type);
void vnt_mac_disable_keyentry(struct vnt_private *priv, u8 entry_idx);
void vnt_mac_set_keyentry(struct vnt_private *priv, u16 key_ctl, u32 entry_idx,
			  u32 key_idx, u8 *addr, u8 *key);
void vnt_mac_reg_bits_off(struct vnt_private *priv, u8 reg_ofs, u8 bits);
void vnt_mac_reg_bits_on(struct vnt_private *priv, u8 reg_ofs, u8 bits);
void vnt_mac_write_word(struct vnt_private *priv, u8 reg_ofs, u16 word);
void vnt_mac_set_bssid_addr(struct vnt_private *priv, u8 *addr);
void vnt_mac_enable_protect_mode(struct vnt_private *priv);
void vnt_mac_disable_protect_mode(struct vnt_private *priv);
void vnt_mac_enable_barker_preamble_mode(struct vnt_private *priv);
void vnt_mac_disable_barker_preamble_mode(struct vnt_private *priv);
void vnt_mac_set_beacon_interval(struct vnt_private *priv, u16 interval);
void vnt_mac_set_led(struct vnt_private *privpriv, u8 state, u8 led);

#endif /* __MAC_H__ */
