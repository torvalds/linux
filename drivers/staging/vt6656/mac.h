/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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

#include <linux/bits.h>
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
#define I2MCFG_BOUNDCTL		BIT(7)
#define I2MCFG_WAITCTL		BIT(5)
#define I2MCFG_SCLOECTL		BIT(4)
#define I2MCFG_WBUSYCTL		BIT(3)
#define I2MCFG_NORETRY		BIT(2)
#define I2MCFG_I2MLDSEQ		BIT(1)
#define I2MCFG_I2CMFAST		BIT(0)

/* Bits in the I2MCSR EEPROM register */
#define I2MCSR_EEMW		BIT(7)
#define I2MCSR_EEMR		BIT(6)
#define I2MCSR_AUTOLD		BIT(3)
#define I2MCSR_NACK		BIT(1)
#define I2MCSR_DONE		BIT(0)

/* Bits in the TMCTL register */
#define TMCTL_TSUSP		BIT(2)
#define TMCTL_TMD		BIT(1)
#define TMCTL_TE		BIT(0)

/* Bits in the TFTCTL register */
#define TFTCTL_HWUTSF		BIT(7)
#define TFTCTL_TBTTSYNC		BIT(6)
#define TFTCTL_HWUTSFEN		BIT(5)
#define TFTCTL_TSFCNTRRD	BIT(4)
#define TFTCTL_TBTTSYNCEN	BIT(3)
#define TFTCTL_TSFSYNCEN	BIT(2)
#define TFTCTL_TSFCNTRST	BIT(1)
#define TFTCTL_TSFCNTREN	BIT(0)

/* Bits in the EnhanceCFG_0 register */
#define EnCFG_BBType_a		0x00
#define EnCFG_BBType_b		BIT(0)
#define EnCFG_BBType_g		BIT(1)
#define EnCFG_BBType_MASK	(EnCFG_BBType_b | EnCFG_BBType_g)
#define EnCFG_ProtectMd		BIT(5)

/* Bits in the EnhanceCFG_1 register */
#define EnCFG_BcnSusInd		BIT(0)
#define EnCFG_BcnSusClr		BIT(1)

/* Bits in the EnhanceCFG_2 register */
#define EnCFG_NXTBTTCFPSTR	BIT(0)
#define EnCFG_BarkerPream	BIT(1)
#define EnCFG_PktBurstMode	BIT(2)

/* Bits in the CFG register */
#define CFG_TKIPOPT		BIT(7)
#define CFG_RXDMAOPT		BIT(6)
#define CFG_TMOT_SW		BIT(5)
#define CFG_TMOT_HWLONG		BIT(4)
#define CFG_TMOT_HW		0x00
#define CFG_CFPENDOPT		BIT(3)
#define CFG_BCNSUSEN		BIT(2)
#define CFG_NOTXTIMEOUT		BIT(1)
#define CFG_NOBUFOPT		BIT(0)

/* Bits in the TEST register */
#define TEST_LBEXT		BIT(7)
#define TEST_LBINT		BIT(6)
#define TEST_LBNONE		0x00
#define TEST_SOFTINT		BIT(5)
#define TEST_CONTTX		BIT(4)
#define TEST_TXPE		BIT(3)
#define TEST_NAVDIS		BIT(2)
#define TEST_NOCTS		BIT(1)
#define TEST_NOACK		BIT(0)

/* Bits in the HOSTCR register */
#define HOSTCR_TXONST		BIT(7)
#define HOSTCR_RXONST		BIT(6)
#define HOSTCR_ADHOC		BIT(5)
#define HOSTCR_AP		BIT(4)
#define HOSTCR_TXON		BIT(3)
#define HOSTCR_RXON		BIT(2)
#define HOSTCR_MACEN		BIT(1)
#define HOSTCR_SOFTRST		BIT(0)

/* Bits in the MACCR register */
#define MACCR_SYNCFLUSHOK	BIT(2)
#define MACCR_SYNCFLUSH		BIT(1)
#define MACCR_CLRNAV		BIT(0)

/* Bits in the RCR register */
#define RCR_SSID		BIT(7)
#define RCR_RXALLTYPE		BIT(6)
#define RCR_UNICAST		BIT(5)
#define RCR_BROADCAST		BIT(4)
#define RCR_MULTICAST		BIT(3)
#define RCR_WPAERR		BIT(2)
#define RCR_ERRCRC		BIT(1)
#define RCR_BSSID		BIT(0)

/* Bits in the TCR register */
#define TCR_SYNCDCFOPT		BIT(1)
#define TCR_AUTOBCNTX		BIT(0)

/* ISR1 */
#define ISR_GPIO3		BIT(6)
#define ISR_RXNOBUF		BIT(3)
#define ISR_MIBNEARFULL		BIT(2)
#define ISR_SOFTINT		BIT(1)
#define ISR_FETALERR		BIT(0)

#define LEDSTS_STS		0x06
#define LEDSTS_TMLEN		0x78
#define LEDSTS_OFF		0x00
#define LEDSTS_ON		0x02
#define LEDSTS_SLOW		0x04
#define LEDSTS_INTER		0x06

/* ISR0 */
#define ISR_WATCHDOG		BIT(7)
#define ISR_SOFTTIMER		BIT(6)
#define ISR_GPIO0		BIT(5)
#define ISR_TBTT		BIT(4)
#define ISR_RXDMA0		BIT(3)
#define ISR_BNTX		BIT(2)
#define ISR_ACTX		BIT(0)

/* Bits in the PSCFG register */
#define PSCFG_PHILIPMD		BIT(6)
#define PSCFG_WAKECALEN		BIT(5)
#define PSCFG_WAKETMREN		BIT(4)
#define PSCFG_BBPSPROG		BIT(3)
#define PSCFG_WAKESYN		BIT(2)
#define PSCFG_SLEEPSYN		BIT(1)
#define PSCFG_AUTOSLEEP		BIT(0)

/* Bits in the PSCTL register */
#define PSCTL_WAKEDONE		BIT(5)
#define PSCTL_PS		BIT(4)
#define PSCTL_GO2DOZE		BIT(3)
#define PSCTL_LNBCN		BIT(2)
#define PSCTL_ALBCN		BIT(1)
#define PSCTL_PSEN		BIT(0)

/* Bits in the PSPWSIG register */
#define PSSIG_WPE3		BIT(7)
#define PSSIG_WPE2		BIT(6)
#define PSSIG_WPE1		BIT(5)
#define PSSIG_WRADIOPE		BIT(4)
#define PSSIG_SPE3		BIT(3)
#define PSSIG_SPE2		BIT(2)
#define PSSIG_SPE1		BIT(1)
#define PSSIG_SRADIOPE		BIT(0)

/* Bits in the BBREGCTL register */
#define BBREGCTL_DONE		BIT(2)
#define BBREGCTL_REGR		BIT(1)
#define BBREGCTL_REGW		BIT(0)

/* Bits in the IFREGCTL register */
#define IFREGCTL_DONE		BIT(2)
#define IFREGCTL_IFRF		BIT(1)
#define IFREGCTL_REGW		BIT(0)

/* Bits in the SOFTPWRCTL register */
#define SOFTPWRCTL_RFLEOPT	BIT(3)
#define SOFTPWRCTL_TXPEINV	BIT(1)
#define SOFTPWRCTL_SWPECTI	BIT(0)
#define SOFTPWRCTL_SWPAPE	BIT(5)
#define SOFTPWRCTL_SWCALEN	BIT(4)
#define SOFTPWRCTL_SWRADIO_PE	BIT(3)
#define SOFTPWRCTL_SWPE2	BIT(2)
#define SOFTPWRCTL_SWPE1	BIT(1)
#define SOFTPWRCTL_SWPE3	BIT(0)

/* Bits in the GPIOCTL1 register */
#define GPIO3_MD		BIT(5)
#define GPIO3_DATA		BIT(6)
#define GPIO3_INTMD		BIT(7)

/* Bits in the MISCFFCTL register */
#define MISCFFCTL_WRITE		BIT(0)

/* Loopback mode */
#define MAC_LB_EXT		BIT(1)
#define MAC_LB_INTERNAL		BIT(0)
#define MAC_LB_NONE		0x00

/* Ethernet address filter type */
#define PKT_TYPE_NONE		0x00 /* turn off receiver */
#define PKT_TYPE_ALL_MULTICAST	BIT(7)
#define PKT_TYPE_PROMISCUOUS	BIT(6)
#define PKT_TYPE_DIRECTED	BIT(5)	/* obselete */
#define PKT_TYPE_BROADCAST	BIT(4)
#define PKT_TYPE_MULTICAST	BIT(3)
#define PKT_TYPE_ERROR_WPA	BIT(2)
#define PKT_TYPE_ERROR_CRC	BIT(1)
#define PKT_TYPE_BSSID		BIT(0)

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

int vnt_mac_set_filter(struct vnt_private *priv, u64 mc_filter);
int vnt_mac_shutdown(struct vnt_private *priv);
int vnt_mac_set_bb_type(struct vnt_private *priv, u8 type);
int vnt_mac_disable_keyentry(struct vnt_private *priv, u8 entry_idx);
int vnt_mac_set_keyentry(struct vnt_private *priv, u16 key_ctl, u32 entry_idx,
			 u32 key_idx, u8 *addr, u8 *key);
int vnt_mac_reg_bits_off(struct vnt_private *priv, u8 reg_ofs, u8 bits);
int vnt_mac_reg_bits_on(struct vnt_private *priv, u8 reg_ofs, u8 bits);
int vnt_mac_write_word(struct vnt_private *priv, u8 reg_ofs, u16 word);
int vnt_mac_set_bssid_addr(struct vnt_private *priv, u8 *addr);
int vnt_mac_enable_protect_mode(struct vnt_private *priv);
int vnt_mac_disable_protect_mode(struct vnt_private *priv);
int vnt_mac_enable_barker_preamble_mode(struct vnt_private *priv);
int vnt_mac_disable_barker_preamble_mode(struct vnt_private *priv);
int vnt_mac_set_beacon_interval(struct vnt_private *priv, u16 interval);
int vnt_mac_set_led(struct vnt_private *privpriv, u8 state, u8 led);

#endif /* __MAC_H__ */
