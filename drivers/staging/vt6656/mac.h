/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
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
#include "tmacro.h"

#define REV_ID_VT3253_A0    0x00
#define REV_ID_VT3253_A1    0x01
#define REV_ID_VT3253_B0    0x08
#define REV_ID_VT3253_B1    0x09

//
// Registers in the MAC
//
#define MAC_REG_BISTCMD     0x04
#define MAC_REG_BISTSR0     0x05
#define MAC_REG_BISTSR1     0x06
#define MAC_REG_BISTSR2     0x07
#define MAC_REG_I2MCSR      0x08
#define MAC_REG_I2MTGID     0x09
#define MAC_REG_I2MTGAD     0x0A
#define MAC_REG_I2MCFG      0x0B
#define MAC_REG_I2MDIPT     0x0C
#define MAC_REG_I2MDOPT     0x0E
#define MAC_REG_USBSUS      0x0F

#define MAC_REG_LOCALID     0x14
#define MAC_REG_TESTCFG     0x15
#define MAC_REG_JUMPER0     0x16
#define MAC_REG_JUMPER1     0x17
#define MAC_REG_TMCTL       0x18
#define MAC_REG_TMDATA0     0x1C
#define MAC_REG_TMDATA1     0x1D
#define MAC_REG_TMDATA2     0x1E
#define MAC_REG_TMDATA3     0x1F

// MAC Parameter related
#define MAC_REG_LRT         0x20        //
#define MAC_REG_SRT         0x21        //
#define MAC_REG_SIFS        0x22        //
#define MAC_REG_DIFS        0x23        //
#define MAC_REG_EIFS        0x24        //
#define MAC_REG_SLOT        0x25        //
#define MAC_REG_BI          0x26        //
#define MAC_REG_CWMAXMIN0   0x28        //
#define MAC_REG_LINKOFFTOTM 0x2A
#define MAC_REG_SWTMOT      0x2B
#define MAC_REG_RTSOKCNT    0x2C
#define MAC_REG_RTSFAILCNT  0x2D
#define MAC_REG_ACKFAILCNT  0x2E
#define MAC_REG_FCSERRCNT   0x2F
// TSF Related
#define MAC_REG_TSFCNTR     0x30        //
#define MAC_REG_NEXTTBTT    0x38        //
#define MAC_REG_TSFOFST     0x40        //
#define MAC_REG_TFTCTL      0x48        //
// WMAC Control/Status Related
#define MAC_REG_ENCFG0      0x4C        //
#define MAC_REG_ENCFG1      0x4D        //
#define MAC_REG_ENCFG2      0x4E        //

#define MAC_REG_CFG         0x50        //
#define MAC_REG_TEST        0x52        //
#define MAC_REG_HOSTCR      0x54        //
#define MAC_REG_MACCR       0x55        //
#define MAC_REG_RCR         0x56        //
#define MAC_REG_TCR         0x57        //
#define MAC_REG_IMR         0x58        //
#define MAC_REG_ISR         0x5C
#define MAC_REG_ISR1        0x5D
// Power Saving Related
#define MAC_REG_PSCFG       0x60        //
#define MAC_REG_PSCTL       0x61        //
#define MAC_REG_PSPWRSIG    0x62        //
#define MAC_REG_BBCR13      0x63
#define MAC_REG_AIDATIM     0x64
#define MAC_REG_PWBT        0x66
#define MAC_REG_WAKEOKTMR   0x68
#define MAC_REG_CALTMR      0x69
#define MAC_REG_SYNSPACCNT  0x6A
#define MAC_REG_WAKSYNOPT   0x6B
// Baseband/IF Control Group
#define MAC_REG_BBREGCTL    0x6C        //
#define MAC_REG_CHANNEL     0x6D
#define MAC_REG_BBREGADR    0x6E
#define MAC_REG_BBREGDATA   0x6F
#define MAC_REG_IFREGCTL    0x70        //
#define MAC_REG_IFDATA      0x71        //
#define MAC_REG_ITRTMSET    0x74        //
#define MAC_REG_PAPEDELAY   0x77
#define MAC_REG_SOFTPWRCTL  0x78        //
#define MAC_REG_SOFTPWRCTL2 0x79        //
#define MAC_REG_GPIOCTL0    0x7A        //
#define MAC_REG_GPIOCTL1    0x7B        //

// MiscFF PIO related
#define MAC_REG_MISCFFNDEX  0xBC
#define MAC_REG_MISCFFCTL   0xBE
#define MAC_REG_MISCFFDATA  0xC0

// MAC Configuration Group
#define MAC_REG_PAR0        0xC4
#define MAC_REG_PAR4        0xC8
#define MAC_REG_BSSID0      0xCC
#define MAC_REG_BSSID4      0xD0
#define MAC_REG_MAR0        0xD4
#define MAC_REG_MAR4        0xD8
// MAC RSPPKT INFO Group
#define MAC_REG_RSPINF_B_1  0xDC
#define MAC_REG_RSPINF_B_2  0xE0
#define MAC_REG_RSPINF_B_5  0xE4
#define MAC_REG_RSPINF_B_11 0xE8
#define MAC_REG_RSPINF_A_6  0xEC
#define MAC_REG_RSPINF_A_9  0xEE
#define MAC_REG_RSPINF_A_12 0xF0
#define MAC_REG_RSPINF_A_18 0xF2
#define MAC_REG_RSPINF_A_24 0xF4
#define MAC_REG_RSPINF_A_36 0xF6
#define MAC_REG_RSPINF_A_48 0xF8
#define MAC_REG_RSPINF_A_54 0xFA
#define MAC_REG_RSPINF_A_72 0xFC

//
// Bits in the I2MCFG EEPROM register
//
#define I2MCFG_BOUNDCTL     0x80
#define I2MCFG_WAITCTL      0x20
#define I2MCFG_SCLOECTL     0x10
#define I2MCFG_WBUSYCTL     0x08
#define I2MCFG_NORETRY      0x04
#define I2MCFG_I2MLDSEQ     0x02
#define I2MCFG_I2CMFAST     0x01

//
// Bits in the I2MCSR EEPROM register
//
#define I2MCSR_EEMW         0x80
#define I2MCSR_EEMR         0x40
#define I2MCSR_AUTOLD       0x08
#define I2MCSR_NACK         0x02
#define I2MCSR_DONE         0x01

//
// Bits in the TMCTL register
//
#define TMCTL_TSUSP         0x04
#define TMCTL_TMD           0x02
#define TMCTL_TE            0x01

//
// Bits in the TFTCTL register
//
#define TFTCTL_HWUTSF       0x80        //
#define TFTCTL_TBTTSYNC     0x40
#define TFTCTL_HWUTSFEN     0x20
#define TFTCTL_TSFCNTRRD    0x10        //
#define TFTCTL_TBTTSYNCEN   0x08        //
#define TFTCTL_TSFSYNCEN    0x04        //
#define TFTCTL_TSFCNTRST    0x02        //
#define TFTCTL_TSFCNTREN    0x01        //

//
// Bits in the EnhanceCFG_0 register
//
#define EnCFG_BBType_a      0x00
#define EnCFG_BBType_b      0x01
#define EnCFG_BBType_g      0x02
#define EnCFG_BBType_MASK   0x03
#define EnCFG_ProtectMd     0x20

//
// Bits in the EnhanceCFG_1 register
//
#define EnCFG_BcnSusInd     0x01
#define EnCFG_BcnSusClr     0x02

//
// Bits in the EnhanceCFG_2 register
//
#define EnCFG_NXTBTTCFPSTR  0x01
#define EnCFG_BarkerPream   0x02
#define EnCFG_PktBurstMode  0x04

//
// Bits in the CFG register
//
#define CFG_TKIPOPT         0x80
#define CFG_RXDMAOPT        0x40
#define CFG_TMOT_SW         0x20
#define CFG_TMOT_HWLONG     0x10
#define CFG_TMOT_HW         0x00
#define CFG_CFPENDOPT       0x08
#define CFG_BCNSUSEN        0x04
#define CFG_NOTXTIMEOUT     0x02
#define CFG_NOBUFOPT        0x01

//
// Bits in the TEST register
//
#define TEST_LBEXT          0x80        //
#define TEST_LBINT          0x40        //
#define TEST_LBNONE         0x00        //
#define TEST_SOFTINT        0x20        //
#define TEST_CONTTX         0x10        //
#define TEST_TXPE           0x08        //
#define TEST_NAVDIS         0x04        //
#define TEST_NOCTS          0x02        //
#define TEST_NOACK          0x01        //

//
// Bits in the HOSTCR register
//
#define HOSTCR_TXONST       0x80        //
#define HOSTCR_RXONST       0x40        //
#define HOSTCR_ADHOC        0x20        // Network Type 1 = Ad-hoc
#define HOSTCR_AP           0x10        // Port Type 1 = AP
#define HOSTCR_TXON         0x08        //0000 1000
#define HOSTCR_RXON         0x04        //0000 0100
#define HOSTCR_MACEN        0x02        //0000 0010
#define HOSTCR_SOFTRST      0x01        //0000 0001

//
// Bits in the MACCR register
//
#define MACCR_SYNCFLUSHOK   0x04        //
#define MACCR_SYNCFLUSH     0x02        //
#define MACCR_CLRNAV        0x01        //

//
// Bits in the RCR register
//
#define RCR_SSID            0x80
#define RCR_RXALLTYPE       0x40        //
#define RCR_UNICAST         0x20        //
#define RCR_BROADCAST       0x10        //
#define RCR_MULTICAST       0x08        //
#define RCR_WPAERR          0x04        //
#define RCR_ERRCRC          0x02        //
#define RCR_BSSID           0x01        //

//
// Bits in the TCR register
//
#define TCR_SYNCDCFOPT      0x02        //
#define TCR_AUTOBCNTX       0x01        // Beacon automatically transmit enable

//ISR1
#define ISR_GPIO3           0x40
#define ISR_RXNOBUF         0x08
#define ISR_MIBNEARFULL     0x04
#define ISR_SOFTINT         0x02
#define ISR_FETALERR        0x01

#define LEDSTS_STS          0x06
#define LEDSTS_TMLEN        0x78
#define LEDSTS_OFF          0x00
#define LEDSTS_ON           0x02
#define LEDSTS_SLOW         0x04
#define LEDSTS_INTER        0x06

//ISR0
#define ISR_WATCHDOG        0x80
#define ISR_SOFTTIMER       0x40
#define ISR_GPIO0           0x20
#define ISR_TBTT            0x10
#define ISR_RXDMA0          0x08
#define ISR_BNTX            0x04
#define ISR_ACTX            0x01

//
// Bits in the PSCFG register
//
#define PSCFG_PHILIPMD      0x40        //
#define PSCFG_WAKECALEN     0x20        //
#define PSCFG_WAKETMREN     0x10        //
#define PSCFG_BBPSPROG      0x08        //
#define PSCFG_WAKESYN       0x04        //
#define PSCFG_SLEEPSYN      0x02        //
#define PSCFG_AUTOSLEEP     0x01        //

//
// Bits in the PSCTL register
//
#define PSCTL_WAKEDONE      0x20        //
#define PSCTL_PS            0x10        //
#define PSCTL_GO2DOZE       0x08        //
#define PSCTL_LNBCN         0x04        //
#define PSCTL_ALBCN         0x02        //
#define PSCTL_PSEN          0x01        //

//
// Bits in the PSPWSIG register
//
#define PSSIG_WPE3          0x80        //
#define PSSIG_WPE2          0x40        //
#define PSSIG_WPE1          0x20        //
#define PSSIG_WRADIOPE      0x10        //
#define PSSIG_SPE3          0x08        //
#define PSSIG_SPE2          0x04        //
#define PSSIG_SPE1          0x02        //
#define PSSIG_SRADIOPE      0x01        //

//
// Bits in the BBREGCTL register
//
#define BBREGCTL_DONE       0x04        //
#define BBREGCTL_REGR       0x02        //
#define BBREGCTL_REGW       0x01        //

//
// Bits in the IFREGCTL register
//
#define IFREGCTL_DONE       0x04        //
#define IFREGCTL_IFRF       0x02        //
#define IFREGCTL_REGW       0x01        //

//
// Bits in the SOFTPWRCTL register
//
#define SOFTPWRCTL_RFLEOPT      0x08  //
#define SOFTPWRCTL_TXPEINV      0x02  //
#define SOFTPWRCTL_SWPECTI      0x01  //
#define SOFTPWRCTL_SWPAPE       0x20  //
#define SOFTPWRCTL_SWCALEN      0x10  //
#define SOFTPWRCTL_SWRADIO_PE   0x08  //
#define SOFTPWRCTL_SWPE2        0x04  //
#define SOFTPWRCTL_SWPE1        0x02  //
#define SOFTPWRCTL_SWPE3        0x01  //

//
// Bits in the GPIOCTL1 register
//
#define GPIO3_MD                0x20    //
#define GPIO3_DATA              0x40    //
#define GPIO3_INTMD             0x80    //

//
// Bits in the MISCFFCTL register
//
#define MISCFFCTL_WRITE     0x0001      //

// Loopback mode
#define MAC_LB_EXT          0x02        //
#define MAC_LB_INTERNAL     0x01        //
#define MAC_LB_NONE         0x00        //

// Ethernet address filter type
#define PKT_TYPE_NONE           0x00    // turn off receiver
#define PKT_TYPE_ALL_MULTICAST  0x80
#define PKT_TYPE_PROMISCUOUS    0x40
#define PKT_TYPE_DIRECTED       0x20    // obselete, directed address is always accepted
#define PKT_TYPE_BROADCAST      0x10
#define PKT_TYPE_MULTICAST      0x08
#define PKT_TYPE_ERROR_WPA      0x04
#define PKT_TYPE_ERROR_CRC      0x02
#define PKT_TYPE_BSSID          0x01

#define Default_BI              0x200

// MiscFIFO Offset
#define MISCFIFO_KEYETRY0       32
#define MISCFIFO_KEYENTRYSIZE   22

// max time out delay time
#define W_MAX_TIMEOUT       0xFFF0U     //

// wait time within loop
#define CB_DELAY_LOOP_WAIT  10          // 10ms

#define MAC_REVISION_A0     0x00
#define MAC_REVISION_A1     0x01

void MACvWriteMultiAddr(struct vnt_private *, u64);
void MACbShutdown(struct vnt_private *);
void MACvSetBBType(struct vnt_private *, u8);
void MACvDisableKeyEntry(struct vnt_private *, u32);
void MACvSetKeyEntry(struct vnt_private *, u16, u32, u32, u8 *, u32 *);
void MACvRegBitsOff(struct vnt_private *, u8, u8);
void MACvRegBitsOn(struct vnt_private *, u8, u8);
void MACvWriteWord(struct vnt_private *, u8, u16);
void MACvWriteBSSIDAddress(struct vnt_private *, u8 *);
void MACvEnableProtectMD(struct vnt_private *);
void MACvDisableProtectMD(struct vnt_private *);
void MACvEnableBarkerPreambleMd(struct vnt_private *);
void MACvDisableBarkerPreambleMd(struct vnt_private *);
void MACvWriteBeaconInterval(struct vnt_private *, u16);

#endif /* __MAC_H__ */
