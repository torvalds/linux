/* ZD1211 USB-WLAN driver for Linux
 *
 * Copyright (C) 2005-2007 Ulrich Kunitz <kune@deine-taler.de>
 * Copyright (C) 2006-2007 Daniel Drake <dsd@gentoo.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _ZD_CHIP_H
#define _ZD_CHIP_H

#include "zd_rf.h"
#include "zd_usb.h"

/* Header for the Media Access Controller (MAC) and the Baseband Processor
 * (BBP). It appears that the ZD1211 wraps the old ZD1205 with USB glue and
 * adds a processor for handling the USB protocol.
 */

/* Address space */
enum {
	/* CONTROL REGISTERS */
	CR_START			= 0x9000,


	/* FIRMWARE */
	FW_START			= 0xee00,


	/* EEPROM */
	E2P_START			= 0xf800,
	E2P_LEN				= 0x800,

	/* EEPROM layout */
	E2P_LOAD_CODE_LEN		= 0xe,		/* base 0xf800 */
	E2P_LOAD_VECT_LEN		= 0x9,		/* base 0xf80e */
	/* E2P_DATA indexes into this */
	E2P_DATA_LEN			= 0x7e,		/* base 0xf817 */
	E2P_BOOT_CODE_LEN		= 0x760,	/* base 0xf895 */
	E2P_INTR_VECT_LEN		= 0xb,		/* base 0xfff5 */

	/* Some precomputed offsets into the EEPROM */
	E2P_DATA_OFFSET			= E2P_LOAD_CODE_LEN + E2P_LOAD_VECT_LEN,
	E2P_BOOT_CODE_OFFSET		= E2P_DATA_OFFSET + E2P_DATA_LEN,
};

#define CTL_REG(offset) ((zd_addr_t)(CR_START + (offset)))
#define E2P_DATA(offset) ((zd_addr_t)(E2P_START + E2P_DATA_OFFSET + (offset)))
#define FWRAW_DATA(offset) ((zd_addr_t)(FW_START + (offset)))

/* 8-bit hardware registers */
#define CR0   CTL_REG(0x0000)
#define CR1   CTL_REG(0x0004)
#define CR2   CTL_REG(0x0008)
#define CR3   CTL_REG(0x000C)

#define CR5   CTL_REG(0x0010)
/*	bit 5: if set short preamble used
 *	bit 6: filter band - Japan channel 14 on, else off
 */
#define CR6   CTL_REG(0x0014)
#define CR7   CTL_REG(0x0018)
#define CR8   CTL_REG(0x001C)

#define CR4   CTL_REG(0x0020)

#define CR9   CTL_REG(0x0024)
/*	bit 2: antenna switch (together with CR10) */
#define CR10  CTL_REG(0x0028)
/*	bit 1: antenna switch (together with CR9)
 *	RF2959 controls with CR11 radion on and off
 */
#define CR11  CTL_REG(0x002C)
/*	bit 6:  TX power control for OFDM
 *	RF2959 controls with CR10 radio on and off
 */
#define CR12  CTL_REG(0x0030)
#define CR13  CTL_REG(0x0034)
#define CR14  CTL_REG(0x0038)
#define CR15  CTL_REG(0x003C)
#define CR16  CTL_REG(0x0040)
#define CR17  CTL_REG(0x0044)
#define CR18  CTL_REG(0x0048)
#define CR19  CTL_REG(0x004C)
#define CR20  CTL_REG(0x0050)
#define CR21  CTL_REG(0x0054)
#define CR22  CTL_REG(0x0058)
#define CR23  CTL_REG(0x005C)
#define CR24  CTL_REG(0x0060)	/* CCA threshold */
#define CR25  CTL_REG(0x0064)
#define CR26  CTL_REG(0x0068)
#define CR27  CTL_REG(0x006C)
#define CR28  CTL_REG(0x0070)
#define CR29  CTL_REG(0x0074)
#define CR30  CTL_REG(0x0078)
#define CR31  CTL_REG(0x007C)	/* TX power control for RF in CCK mode */
#define CR32  CTL_REG(0x0080)
#define CR33  CTL_REG(0x0084)
#define CR34  CTL_REG(0x0088)
#define CR35  CTL_REG(0x008C)
#define CR36  CTL_REG(0x0090)
#define CR37  CTL_REG(0x0094)
#define CR38  CTL_REG(0x0098)
#define CR39  CTL_REG(0x009C)
#define CR40  CTL_REG(0x00A0)
#define CR41  CTL_REG(0x00A4)
#define CR42  CTL_REG(0x00A8)
#define CR43  CTL_REG(0x00AC)
#define CR44  CTL_REG(0x00B0)
#define CR45  CTL_REG(0x00B4)
#define CR46  CTL_REG(0x00B8)
#define CR47  CTL_REG(0x00BC)	/* CCK baseband gain
	                         * (patch value might be in EEPROM)
				 */
#define CR48  CTL_REG(0x00C0)
#define CR49  CTL_REG(0x00C4)
#define CR50  CTL_REG(0x00C8)
#define CR51  CTL_REG(0x00CC)	/* TX power control for RF in 6-36M modes */
#define CR52  CTL_REG(0x00D0)	/* TX power control for RF in 48M mode */
#define CR53  CTL_REG(0x00D4)	/* TX power control for RF in 54M mode */
#define CR54  CTL_REG(0x00D8)
#define CR55  CTL_REG(0x00DC)
#define CR56  CTL_REG(0x00E0)
#define CR57  CTL_REG(0x00E4)
#define CR58  CTL_REG(0x00E8)
#define CR59  CTL_REG(0x00EC)
#define CR60  CTL_REG(0x00F0)
#define CR61  CTL_REG(0x00F4)
#define CR62  CTL_REG(0x00F8)
#define CR63  CTL_REG(0x00FC)
#define CR64  CTL_REG(0x0100)
#define CR65  CTL_REG(0x0104) /* OFDM 54M calibration */
#define CR66  CTL_REG(0x0108) /* OFDM 48M calibration */
#define CR67  CTL_REG(0x010C) /* OFDM 36M calibration */
#define CR68  CTL_REG(0x0110) /* CCK calibration */
#define CR69  CTL_REG(0x0114)
#define CR70  CTL_REG(0x0118)
#define CR71  CTL_REG(0x011C)
#define CR72  CTL_REG(0x0120)
#define CR73  CTL_REG(0x0124)
#define CR74  CTL_REG(0x0128)
#define CR75  CTL_REG(0x012C)
#define CR76  CTL_REG(0x0130)
#define CR77  CTL_REG(0x0134)
#define CR78  CTL_REG(0x0138)
#define CR79  CTL_REG(0x013C)
#define CR80  CTL_REG(0x0140)
#define CR81  CTL_REG(0x0144)
#define CR82  CTL_REG(0x0148)
#define CR83  CTL_REG(0x014C)
#define CR84  CTL_REG(0x0150)
#define CR85  CTL_REG(0x0154)
#define CR86  CTL_REG(0x0158)
#define CR87  CTL_REG(0x015C)
#define CR88  CTL_REG(0x0160)
#define CR89  CTL_REG(0x0164)
#define CR90  CTL_REG(0x0168)
#define CR91  CTL_REG(0x016C)
#define CR92  CTL_REG(0x0170)
#define CR93  CTL_REG(0x0174)
#define CR94  CTL_REG(0x0178)
#define CR95  CTL_REG(0x017C)
#define CR96  CTL_REG(0x0180)
#define CR97  CTL_REG(0x0184)
#define CR98  CTL_REG(0x0188)
#define CR99  CTL_REG(0x018C)
#define CR100 CTL_REG(0x0190)
#define CR101 CTL_REG(0x0194)
#define CR102 CTL_REG(0x0198)
#define CR103 CTL_REG(0x019C)
#define CR104 CTL_REG(0x01A0)
#define CR105 CTL_REG(0x01A4)
#define CR106 CTL_REG(0x01A8)
#define CR107 CTL_REG(0x01AC)
#define CR108 CTL_REG(0x01B0)
#define CR109 CTL_REG(0x01B4)
#define CR110 CTL_REG(0x01B8)
#define CR111 CTL_REG(0x01BC)
#define CR112 CTL_REG(0x01C0)
#define CR113 CTL_REG(0x01C4)
#define CR114 CTL_REG(0x01C8)
#define CR115 CTL_REG(0x01CC)
#define CR116 CTL_REG(0x01D0)
#define CR117 CTL_REG(0x01D4)
#define CR118 CTL_REG(0x01D8)
#define CR119 CTL_REG(0x01DC)
#define CR120 CTL_REG(0x01E0)
#define CR121 CTL_REG(0x01E4)
#define CR122 CTL_REG(0x01E8)
#define CR123 CTL_REG(0x01EC)
#define CR124 CTL_REG(0x01F0)
#define CR125 CTL_REG(0x01F4)
#define CR126 CTL_REG(0x01F8)
#define CR127 CTL_REG(0x01FC)
#define CR128 CTL_REG(0x0200)
#define CR129 CTL_REG(0x0204)
#define CR130 CTL_REG(0x0208)
#define CR131 CTL_REG(0x020C)
#define CR132 CTL_REG(0x0210)
#define CR133 CTL_REG(0x0214)
#define CR134 CTL_REG(0x0218)
#define CR135 CTL_REG(0x021C)
#define CR136 CTL_REG(0x0220)
#define CR137 CTL_REG(0x0224)
#define CR138 CTL_REG(0x0228)
#define CR139 CTL_REG(0x022C)
#define CR140 CTL_REG(0x0230)
#define CR141 CTL_REG(0x0234)
#define CR142 CTL_REG(0x0238)
#define CR143 CTL_REG(0x023C)
#define CR144 CTL_REG(0x0240)
#define CR145 CTL_REG(0x0244)
#define CR146 CTL_REG(0x0248)
#define CR147 CTL_REG(0x024C)
#define CR148 CTL_REG(0x0250)
#define CR149 CTL_REG(0x0254)
#define CR150 CTL_REG(0x0258)
#define CR151 CTL_REG(0x025C)
#define CR152 CTL_REG(0x0260)
#define CR153 CTL_REG(0x0264)
#define CR154 CTL_REG(0x0268)
#define CR155 CTL_REG(0x026C)
#define CR156 CTL_REG(0x0270)
#define CR157 CTL_REG(0x0274)
#define CR158 CTL_REG(0x0278)
#define CR159 CTL_REG(0x027C)
#define CR160 CTL_REG(0x0280)
#define CR161 CTL_REG(0x0284)
#define CR162 CTL_REG(0x0288)
#define CR163 CTL_REG(0x028C)
#define CR164 CTL_REG(0x0290)
#define CR165 CTL_REG(0x0294)
#define CR166 CTL_REG(0x0298)
#define CR167 CTL_REG(0x029C)
#define CR168 CTL_REG(0x02A0)
#define CR169 CTL_REG(0x02A4)
#define CR170 CTL_REG(0x02A8)
#define CR171 CTL_REG(0x02AC)
#define CR172 CTL_REG(0x02B0)
#define CR173 CTL_REG(0x02B4)
#define CR174 CTL_REG(0x02B8)
#define CR175 CTL_REG(0x02BC)
#define CR176 CTL_REG(0x02C0)
#define CR177 CTL_REG(0x02C4)
#define CR178 CTL_REG(0x02C8)
#define CR179 CTL_REG(0x02CC)
#define CR180 CTL_REG(0x02D0)
#define CR181 CTL_REG(0x02D4)
#define CR182 CTL_REG(0x02D8)
#define CR183 CTL_REG(0x02DC)
#define CR184 CTL_REG(0x02E0)
#define CR185 CTL_REG(0x02E4)
#define CR186 CTL_REG(0x02E8)
#define CR187 CTL_REG(0x02EC)
#define CR188 CTL_REG(0x02F0)
#define CR189 CTL_REG(0x02F4)
#define CR190 CTL_REG(0x02F8)
#define CR191 CTL_REG(0x02FC)
#define CR192 CTL_REG(0x0300)
#define CR193 CTL_REG(0x0304)
#define CR194 CTL_REG(0x0308)
#define CR195 CTL_REG(0x030C)
#define CR196 CTL_REG(0x0310)
#define CR197 CTL_REG(0x0314)
#define CR198 CTL_REG(0x0318)
#define CR199 CTL_REG(0x031C)
#define CR200 CTL_REG(0x0320)
#define CR201 CTL_REG(0x0324)
#define CR202 CTL_REG(0x0328)
#define CR203 CTL_REG(0x032C)	/* I2C bus template value & flash control */
#define CR204 CTL_REG(0x0330)
#define CR205 CTL_REG(0x0334)
#define CR206 CTL_REG(0x0338)
#define CR207 CTL_REG(0x033C)
#define CR208 CTL_REG(0x0340)
#define CR209 CTL_REG(0x0344)
#define CR210 CTL_REG(0x0348)
#define CR211 CTL_REG(0x034C)
#define CR212 CTL_REG(0x0350)
#define CR213 CTL_REG(0x0354)
#define CR214 CTL_REG(0x0358)
#define CR215 CTL_REG(0x035C)
#define CR216 CTL_REG(0x0360)
#define CR217 CTL_REG(0x0364)
#define CR218 CTL_REG(0x0368)
#define CR219 CTL_REG(0x036C)
#define CR220 CTL_REG(0x0370)
#define CR221 CTL_REG(0x0374)
#define CR222 CTL_REG(0x0378)
#define CR223 CTL_REG(0x037C)
#define CR224 CTL_REG(0x0380)
#define CR225 CTL_REG(0x0384)
#define CR226 CTL_REG(0x0388)
#define CR227 CTL_REG(0x038C)
#define CR228 CTL_REG(0x0390)
#define CR229 CTL_REG(0x0394)
#define CR230 CTL_REG(0x0398)
#define CR231 CTL_REG(0x039C)
#define CR232 CTL_REG(0x03A0)
#define CR233 CTL_REG(0x03A4)
#define CR234 CTL_REG(0x03A8)
#define CR235 CTL_REG(0x03AC)
#define CR236 CTL_REG(0x03B0)

#define CR240 CTL_REG(0x03C0)
/*	bit 7:  host-controlled RF register writes
 * CR241-CR245: for hardware controlled writing of RF bits, not needed for
 *              USB
 */
#define CR241 CTL_REG(0x03C4)
#define CR242 CTL_REG(0x03C8)
#define CR243 CTL_REG(0x03CC)
#define CR244 CTL_REG(0x03D0)
#define CR245 CTL_REG(0x03D4)

#define CR251 CTL_REG(0x03EC)	/* only used for activation and deactivation of
				 * Airoha RFs AL2230 and AL7230B
				 */
#define CR252 CTL_REG(0x03F0)
#define CR253 CTL_REG(0x03F4)
#define CR254 CTL_REG(0x03F8)
#define CR255 CTL_REG(0x03FC)

#define CR_MAX_PHY_REG 255

/* Taken from the ZYDAS driver, not all of them are relevant for the ZD1211
 * driver.
 */

#define CR_RF_IF_CLK			CTL_REG(0x0400)
#define CR_RF_IF_DATA			CTL_REG(0x0404)
#define CR_PE1_PE2			CTL_REG(0x0408)
#define CR_PE2_DLY			CTL_REG(0x040C)
#define CR_LE1				CTL_REG(0x0410)
#define CR_LE2				CTL_REG(0x0414)
/* Seems to enable/disable GPI (General Purpose IO?) */
#define CR_GPI_EN			CTL_REG(0x0418)
#define CR_RADIO_PD			CTL_REG(0x042C)
#define CR_RF2948_PD			CTL_REG(0x042C)
#define CR_ENABLE_PS_MANUAL_AGC		CTL_REG(0x043C)
#define CR_CONFIG_PHILIPS		CTL_REG(0x0440)
#define CR_SA2400_SER_AP		CTL_REG(0x0444)
#define CR_I2C_WRITE			CTL_REG(0x0444)
#define CR_SA2400_SER_RP		CTL_REG(0x0448)
#define CR_RADIO_PE			CTL_REG(0x0458)
#define CR_RST_BUS_MASTER		CTL_REG(0x045C)
#define CR_RFCFG			CTL_REG(0x0464)
#define CR_HSTSCHG			CTL_REG(0x046C)
#define CR_PHY_ON			CTL_REG(0x0474)
#define CR_RX_DELAY			CTL_REG(0x0478)
#define CR_RX_PE_DELAY			CTL_REG(0x047C)
#define CR_GPIO_1			CTL_REG(0x0490)
#define CR_GPIO_2			CTL_REG(0x0494)
#define CR_EncryBufMux			CTL_REG(0x04A8)
#define CR_PS_CTRL			CTL_REG(0x0500)
#define CR_ADDA_PWR_DWN			CTL_REG(0x0504)
#define CR_ADDA_MBIAS_WARMTIME		CTL_REG(0x0508)
#define CR_MAC_PS_STATE			CTL_REG(0x050C)

#define CR_INTERRUPT			CTL_REG(0x0510)
#define INT_TX_COMPLETE			(1 <<  0)
#define INT_RX_COMPLETE			(1 <<  1)
#define INT_RETRY_FAIL			(1 <<  2)
#define INT_WAKEUP			(1 <<  3)
#define INT_DTIM_NOTIFY			(1 <<  5)
#define INT_CFG_NEXT_BCN		(1 <<  6)
#define INT_BUS_ABORT			(1 <<  7)
#define INT_TX_FIFO_READY		(1 <<  8)
#define INT_UART			(1 <<  9)
#define INT_TX_COMPLETE_EN		(1 << 16)
#define INT_RX_COMPLETE_EN		(1 << 17)
#define INT_RETRY_FAIL_EN		(1 << 18)
#define INT_WAKEUP_EN			(1 << 19)
#define INT_DTIM_NOTIFY_EN		(1 << 21)
#define INT_CFG_NEXT_BCN_EN		(1 << 22)
#define INT_BUS_ABORT_EN		(1 << 23)
#define INT_TX_FIFO_READY_EN		(1 << 24)
#define INT_UART_EN			(1 << 25)

#define CR_TSF_LOW_PART			CTL_REG(0x0514)
#define CR_TSF_HIGH_PART		CTL_REG(0x0518)

/* Following three values are in time units (1024us)
 * Following condition must be met:
 * atim < tbtt < bcn
 */
#define CR_ATIM_WND_PERIOD		CTL_REG(0x051C)
#define CR_BCN_INTERVAL			CTL_REG(0x0520)
#define CR_PRE_TBTT			CTL_REG(0x0524)
/* in units of TU(1024us) */

/* for UART support */
#define CR_UART_RBR_THR_DLL		CTL_REG(0x0540)
#define CR_UART_DLM_IER			CTL_REG(0x0544)
#define CR_UART_IIR_FCR			CTL_REG(0x0548)
#define CR_UART_LCR			CTL_REG(0x054c)
#define CR_UART_MCR			CTL_REG(0x0550)
#define CR_UART_LSR			CTL_REG(0x0554)
#define CR_UART_MSR			CTL_REG(0x0558)
#define CR_UART_ECR			CTL_REG(0x055c)
#define CR_UART_STATUS			CTL_REG(0x0560)

#define CR_PCI_TX_ADDR_P1		CTL_REG(0x0600)
#define CR_PCI_TX_AddR_P2		CTL_REG(0x0604)
#define CR_PCI_RX_AddR_P1		CTL_REG(0x0608)
#define CR_PCI_RX_AddR_P2		CTL_REG(0x060C)

/* must be overwritten if custom MAC address will be used */
#define CR_MAC_ADDR_P1			CTL_REG(0x0610)
#define CR_MAC_ADDR_P2			CTL_REG(0x0614)
#define CR_BSSID_P1			CTL_REG(0x0618)
#define CR_BSSID_P2			CTL_REG(0x061C)
#define CR_BCN_PLCP_CFG			CTL_REG(0x0620)

/* Group hash table for filtering incoming packets.
 *
 * The group hash table is 64 bit large and split over two parts. The first
 * part is the lower part. The upper 6 bits of the last byte of the target
 * address are used as index. Packets are received if the hash table bit is
 * set. This is used for multicast handling, but for broadcasts (address
 * ff:ff:ff:ff:ff:ff) the highest bit in the second table must also be set.
 */
#define CR_GROUP_HASH_P1		CTL_REG(0x0624)
#define CR_GROUP_HASH_P2		CTL_REG(0x0628)

#define CR_RX_TIMEOUT			CTL_REG(0x062C)

/* Basic rates supported by the BSS. When producing ACK or CTS messages, the
 * device will use a rate in this table that is less than or equal to the rate
 * of the incoming frame which prompted the response. */
#define CR_BASIC_RATE_TBL		CTL_REG(0x0630)
#define CR_RATE_1M	(1 <<  0)	/* 802.11b */
#define CR_RATE_2M	(1 <<  1)	/* 802.11b */
#define CR_RATE_5_5M	(1 <<  2)	/* 802.11b */
#define CR_RATE_11M	(1 <<  3)	/* 802.11b */
#define CR_RATE_6M      (1 <<  8)	/* 802.11g */
#define CR_RATE_9M      (1 <<  9)	/* 802.11g */
#define CR_RATE_12M	(1 << 10)	/* 802.11g */
#define CR_RATE_18M	(1 << 11)	/* 802.11g */
#define CR_RATE_24M     (1 << 12)	/* 802.11g */
#define CR_RATE_36M     (1 << 13)	/* 802.11g */
#define CR_RATE_48M     (1 << 14)	/* 802.11g */
#define CR_RATE_54M     (1 << 15)	/* 802.11g */
#define CR_RATES_80211G	0xff00
#define CR_RATES_80211B	0x000f

/* Mandatory rates required in the BSS. When producing ACK or CTS messages, if
 * the device could not find an appropriate rate in CR_BASIC_RATE_TBL, it will
 * look for a rate in this table that is less than or equal to the rate of
 * the incoming frame. */
#define CR_MANDATORY_RATE_TBL		CTL_REG(0x0634)
#define CR_RTS_CTS_RATE			CTL_REG(0x0638)

/* These are all bit indexes in CR_RTS_CTS_RATE, so remember to shift. */
#define RTSCTS_SH_RTS_RATE		0
#define RTSCTS_SH_EXP_CTS_RATE		4
#define RTSCTS_SH_RTS_MOD_TYPE		8
#define RTSCTS_SH_RTS_PMB_TYPE		9
#define RTSCTS_SH_CTS_RATE		16
#define RTSCTS_SH_CTS_MOD_TYPE		24
#define RTSCTS_SH_CTS_PMB_TYPE		25

#define CR_WEP_PROTECT			CTL_REG(0x063C)
#define CR_RX_THRESHOLD			CTL_REG(0x0640)

/* register for controlling the LEDS */
#define CR_LED				CTL_REG(0x0644)
/* masks for controlling LEDs */
#define LED1				(1 <<  8)
#define LED2				(1 <<  9)
#define LED_SW				(1 << 10)

/* Seems to indicate that the configuration is over.
 */
#define CR_AFTER_PNP			CTL_REG(0x0648)
#define CR_ACK_TIME_80211		CTL_REG(0x0658)

#define CR_RX_OFFSET			CTL_REG(0x065c)

#define CR_BCN_LENGTH			CTL_REG(0x0664)
#define CR_PHY_DELAY			CTL_REG(0x066C)
#define CR_BCN_FIFO			CTL_REG(0x0670)
#define CR_SNIFFER_ON			CTL_REG(0x0674)

#define CR_ENCRYPTION_TYPE		CTL_REG(0x0678)
#define NO_WEP				0
#define WEP64				1
#define WEP128				5
#define WEP256				6
#define ENC_SNIFFER			8

#define CR_ZD1211_RETRY_MAX		CTL_REG(0x067C)

#define CR_REG1				CTL_REG(0x0680)
/* Setting the bit UNLOCK_PHY_REGS disallows the write access to physical
 * registers, so one could argue it is a LOCK bit. But calling it
 * LOCK_PHY_REGS makes it confusing.
 */
#define UNLOCK_PHY_REGS			(1 << 7)

#define CR_DEVICE_STATE			CTL_REG(0x0684)
#define CR_UNDERRUN_CNT			CTL_REG(0x0688)

#define CR_RX_FILTER			CTL_REG(0x068c)
#define RX_FILTER_ASSOC_REQUEST		(1 <<  0)
#define RX_FILTER_ASSOC_RESPONSE	(1 <<  1)
#define RX_FILTER_REASSOC_REQUEST	(1 <<  2)
#define RX_FILTER_REASSOC_RESPONSE	(1 <<  3)
#define RX_FILTER_PROBE_REQUEST		(1 <<  4)
#define RX_FILTER_PROBE_RESPONSE	(1 <<  5)
/* bits 6 and 7 reserved */
#define RX_FILTER_BEACON		(1 <<  8)
#define RX_FILTER_ATIM			(1 <<  9)
#define RX_FILTER_DISASSOC		(1 << 10)
#define RX_FILTER_AUTH			(1 << 11)
#define RX_FILTER_DEAUTH		(1 << 12)
#define RX_FILTER_PSPOLL		(1 << 26)
#define RX_FILTER_RTS			(1 << 27)
#define RX_FILTER_CTS			(1 << 28)
#define RX_FILTER_ACK			(1 << 29)
#define RX_FILTER_CFEND			(1 << 30)
#define RX_FILTER_CFACK			(1 << 31)

/* Enable bits for all frames you are interested in. */
#define STA_RX_FILTER	(RX_FILTER_ASSOC_REQUEST | RX_FILTER_ASSOC_RESPONSE | \
	RX_FILTER_REASSOC_REQUEST | RX_FILTER_REASSOC_RESPONSE | \
	RX_FILTER_PROBE_REQUEST | RX_FILTER_PROBE_RESPONSE | \
	(0x3 << 6) /* vendor driver sets these reserved bits */ | \
	RX_FILTER_BEACON | RX_FILTER_ATIM | RX_FILTER_DISASSOC | \
	RX_FILTER_AUTH | RX_FILTER_DEAUTH | \
	(0x7 << 13) /* vendor driver sets these reserved bits */ | \
	RX_FILTER_PSPOLL | RX_FILTER_ACK) /* 0x2400ffff */

#define RX_FILTER_CTRL (RX_FILTER_RTS | RX_FILTER_CTS | \
	RX_FILTER_CFEND | RX_FILTER_CFACK)

#define BCN_MODE_IBSS			0x2000000

/* Monitor mode sets filter to 0xfffff */

#define CR_ACK_TIMEOUT_EXT		CTL_REG(0x0690)
#define CR_BCN_FIFO_SEMAPHORE		CTL_REG(0x0694)

#define CR_IFS_VALUE			CTL_REG(0x0698)
#define IFS_VALUE_DIFS_SH		0
#define IFS_VALUE_EIFS_SH		12
#define IFS_VALUE_SIFS_SH		24
#define IFS_VALUE_DEFAULT		((  50 << IFS_VALUE_DIFS_SH) | \
					 (1148 << IFS_VALUE_EIFS_SH) | \
					 (  10 << IFS_VALUE_SIFS_SH))

#define CR_RX_TIME_OUT			CTL_REG(0x069C)
#define CR_TOTAL_RX_FRM			CTL_REG(0x06A0)
#define CR_CRC32_CNT			CTL_REG(0x06A4)
#define CR_CRC16_CNT			CTL_REG(0x06A8)
#define CR_DECRYPTION_ERR_UNI		CTL_REG(0x06AC)
#define CR_RX_FIFO_OVERRUN		CTL_REG(0x06B0)

#define CR_DECRYPTION_ERR_MUL		CTL_REG(0x06BC)

#define CR_NAV_CNT			CTL_REG(0x06C4)
#define CR_NAV_CCA			CTL_REG(0x06C8)
#define CR_RETRY_CNT			CTL_REG(0x06CC)

#define CR_READ_TCB_ADDR		CTL_REG(0x06E8)
#define CR_READ_RFD_ADDR		CTL_REG(0x06EC)
#define CR_CWMIN_CWMAX			CTL_REG(0x06F0)
#define CR_TOTAL_TX_FRM			CTL_REG(0x06F4)

/* CAM: Continuous Access Mode (power management) */
#define CR_CAM_MODE			CTL_REG(0x0700)
#define MODE_IBSS			0x0
#define MODE_AP				0x1
#define MODE_STA			0x2
#define MODE_AP_WDS			0x3

#define CR_CAM_ROLL_TB_LOW		CTL_REG(0x0704)
#define CR_CAM_ROLL_TB_HIGH		CTL_REG(0x0708)
#define CR_CAM_ADDRESS			CTL_REG(0x070C)
#define CR_CAM_DATA			CTL_REG(0x0710)

#define CR_ROMDIR			CTL_REG(0x0714)

#define CR_DECRY_ERR_FLG_LOW		CTL_REG(0x0714)
#define CR_DECRY_ERR_FLG_HIGH		CTL_REG(0x0718)

#define CR_WEPKEY0			CTL_REG(0x0720)
#define CR_WEPKEY1			CTL_REG(0x0724)
#define CR_WEPKEY2			CTL_REG(0x0728)
#define CR_WEPKEY3			CTL_REG(0x072C)
#define CR_WEPKEY4			CTL_REG(0x0730)
#define CR_WEPKEY5			CTL_REG(0x0734)
#define CR_WEPKEY6			CTL_REG(0x0738)
#define CR_WEPKEY7			CTL_REG(0x073C)
#define CR_WEPKEY8			CTL_REG(0x0740)
#define CR_WEPKEY9			CTL_REG(0x0744)
#define CR_WEPKEY10			CTL_REG(0x0748)
#define CR_WEPKEY11			CTL_REG(0x074C)
#define CR_WEPKEY12			CTL_REG(0x0750)
#define CR_WEPKEY13			CTL_REG(0x0754)
#define CR_WEPKEY14			CTL_REG(0x0758)
#define CR_WEPKEY15			CTL_REG(0x075c)
#define CR_TKIP_MODE			CTL_REG(0x0760)

#define CR_EEPROM_PROTECT0		CTL_REG(0x0758)
#define CR_EEPROM_PROTECT1		CTL_REG(0x075C)

#define CR_DBG_FIFO_RD			CTL_REG(0x0800)
#define CR_DBG_SELECT			CTL_REG(0x0804)
#define CR_FIFO_Length			CTL_REG(0x0808)


#define CR_RSSI_MGC			CTL_REG(0x0810)

#define CR_PON				CTL_REG(0x0818)
#define CR_RX_ON			CTL_REG(0x081C)
#define CR_TX_ON			CTL_REG(0x0820)
#define CR_CHIP_EN			CTL_REG(0x0824)
#define CR_LO_SW			CTL_REG(0x0828)
#define CR_TXRX_SW			CTL_REG(0x082C)
#define CR_S_MD				CTL_REG(0x0830)

#define CR_USB_DEBUG_PORT		CTL_REG(0x0888)
#define CR_ZD1211B_CWIN_MAX_MIN_AC0	CTL_REG(0x0b00)
#define CR_ZD1211B_CWIN_MAX_MIN_AC1	CTL_REG(0x0b04)
#define CR_ZD1211B_CWIN_MAX_MIN_AC2	CTL_REG(0x0b08)
#define CR_ZD1211B_CWIN_MAX_MIN_AC3	CTL_REG(0x0b0c)
#define CR_ZD1211B_AIFS_CTL1		CTL_REG(0x0b10)
#define CR_ZD1211B_AIFS_CTL2		CTL_REG(0x0b14)
#define CR_ZD1211B_TXOP			CTL_REG(0x0b20)
#define CR_ZD1211B_RETRY_MAX		CTL_REG(0x0b28)

/* Value for CR_ZD1211_RETRY_MAX & CR_ZD1211B_RETRY_MAX. Vendor driver uses 2,
 * we use 0. The first rate is tried (count+2), then all next rates are tried
 * twice, until 1 Mbits is tried. */
#define	ZD1211_RETRY_COUNT		0
#define	ZD1211B_RETRY_COUNT	\
	(ZD1211_RETRY_COUNT <<  0)|	\
	(ZD1211_RETRY_COUNT <<  8)|	\
	(ZD1211_RETRY_COUNT << 16)|	\
	(ZD1211_RETRY_COUNT << 24)

/* Used to detect PLL lock */
#define UW2453_INTR_REG			((zd_addr_t)0x85c1)

#define CWIN_SIZE			0x007f043f


#define HWINT_ENABLED			\
	(INT_TX_COMPLETE_EN|		\
	 INT_RX_COMPLETE_EN|		\
	 INT_RETRY_FAIL_EN|		\
	 INT_WAKEUP_EN|			\
	 INT_CFG_NEXT_BCN_EN)

#define HWINT_DISABLED			0

#define E2P_PWR_INT_GUARD		8
#define E2P_CHANNEL_COUNT		14

/* If you compare this addresses with the ZYDAS orignal driver, please notify
 * that we use word mapping for the EEPROM.
 */

/*
 * Upper 16 bit contains the regulatory domain.
 */
#define E2P_SUBID		E2P_DATA(0x00)
#define E2P_POD			E2P_DATA(0x02)
#define E2P_MAC_ADDR_P1		E2P_DATA(0x04)
#define E2P_MAC_ADDR_P2		E2P_DATA(0x06)
#define E2P_PWR_CAL_VALUE1	E2P_DATA(0x08)
#define E2P_PWR_CAL_VALUE2	E2P_DATA(0x0a)
#define E2P_PWR_CAL_VALUE3	E2P_DATA(0x0c)
#define E2P_PWR_CAL_VALUE4      E2P_DATA(0x0e)
#define E2P_PWR_INT_VALUE1	E2P_DATA(0x10)
#define E2P_PWR_INT_VALUE2	E2P_DATA(0x12)
#define E2P_PWR_INT_VALUE3	E2P_DATA(0x14)
#define E2P_PWR_INT_VALUE4	E2P_DATA(0x16)

/* Contains a bit for each allowed channel. It gives for Europe (ETSI 0x30)
 * also only 11 channels. */
#define E2P_ALLOWED_CHANNEL	E2P_DATA(0x18)

#define E2P_DEVICE_VER		E2P_DATA(0x20)
#define E2P_PHY_REG		E2P_DATA(0x25)
#define E2P_36M_CAL_VALUE1	E2P_DATA(0x28)
#define E2P_36M_CAL_VALUE2      E2P_DATA(0x2a)
#define E2P_36M_CAL_VALUE3      E2P_DATA(0x2c)
#define E2P_36M_CAL_VALUE4	E2P_DATA(0x2e)
#define E2P_11A_INT_VALUE1	E2P_DATA(0x30)
#define E2P_11A_INT_VALUE2	E2P_DATA(0x32)
#define E2P_11A_INT_VALUE3	E2P_DATA(0x34)
#define E2P_11A_INT_VALUE4	E2P_DATA(0x36)
#define E2P_48M_CAL_VALUE1	E2P_DATA(0x38)
#define E2P_48M_CAL_VALUE2	E2P_DATA(0x3a)
#define E2P_48M_CAL_VALUE3	E2P_DATA(0x3c)
#define E2P_48M_CAL_VALUE4	E2P_DATA(0x3e)
#define E2P_48M_INT_VALUE1	E2P_DATA(0x40)
#define E2P_48M_INT_VALUE2	E2P_DATA(0x42)
#define E2P_48M_INT_VALUE3	E2P_DATA(0x44)
#define E2P_48M_INT_VALUE4	E2P_DATA(0x46)
#define E2P_54M_CAL_VALUE1	E2P_DATA(0x48)	/* ??? */
#define E2P_54M_CAL_VALUE2	E2P_DATA(0x4a)
#define E2P_54M_CAL_VALUE3	E2P_DATA(0x4c)
#define E2P_54M_CAL_VALUE4	E2P_DATA(0x4e)
#define E2P_54M_INT_VALUE1	E2P_DATA(0x50)
#define E2P_54M_INT_VALUE2	E2P_DATA(0x52)
#define E2P_54M_INT_VALUE3	E2P_DATA(0x54)
#define E2P_54M_INT_VALUE4	E2P_DATA(0x56)

/* This word contains the base address of the FW_REG_ registers below */
#define FWRAW_REGS_ADDR		FWRAW_DATA(0x1d)

/* All 16 bit values, offset from the address in FWRAW_REGS_ADDR */
enum {
	FW_REG_FIRMWARE_VER	= 0,
	/* non-zero if USB high speed connection */
	FW_REG_USB_SPEED	= 1,
	FW_REG_FIX_TX_RATE	= 2,
	/* Seems to be able to control LEDs over the firmware */
	FW_REG_LED_LINK_STATUS	= 3,
	FW_REG_SOFT_RESET	= 4,
	FW_REG_FLASH_CHK	= 5,
};

/* Values for FW_LINK_STATUS */
#define FW_LINK_OFF		0x0
#define FW_LINK_TX		0x1
/* 0x2 - link led on? */

enum {
	/* indices for ofdm_cal_values */
	OFDM_36M_INDEX = 0,
	OFDM_48M_INDEX = 1,
	OFDM_54M_INDEX = 2,
};

struct zd_chip {
	struct zd_usb usb;
	struct zd_rf rf;
	struct mutex mutex;
	/* Base address of FW_REG_ registers */
	zd_addr_t fw_regs_base;
	/* EepSetPoint in the vendor driver */
	u8 pwr_cal_values[E2P_CHANNEL_COUNT];
	/* integration values in the vendor driver */
	u8 pwr_int_values[E2P_CHANNEL_COUNT];
	/* SetPointOFDM in the vendor driver */
	u8 ofdm_cal_values[3][E2P_CHANNEL_COUNT];
	u16 link_led;
	unsigned int pa_type:4,
		patch_cck_gain:1, patch_cr157:1, patch_6m_band_edge:1,
		new_phy_layout:1, al2230s_bit:1,
		supports_tx_led:1;
};

static inline struct zd_chip *zd_usb_to_chip(struct zd_usb *usb)
{
	return container_of(usb, struct zd_chip, usb);
}

static inline struct zd_chip *zd_rf_to_chip(struct zd_rf *rf)
{
	return container_of(rf, struct zd_chip, rf);
}

#define zd_chip_dev(chip) (&(chip)->usb.intf->dev)

void zd_chip_init(struct zd_chip *chip,
	         struct ieee80211_hw *hw,
	         struct usb_interface *intf);
void zd_chip_clear(struct zd_chip *chip);
int zd_chip_read_mac_addr_fw(struct zd_chip *chip, u8 *addr);
int zd_chip_init_hw(struct zd_chip *chip);
int zd_chip_reset(struct zd_chip *chip);

static inline int zd_chip_is_zd1211b(struct zd_chip *chip)
{
	return chip->usb.is_zd1211b;
}

static inline int zd_ioread16v_locked(struct zd_chip *chip, u16 *values,
	                              const zd_addr_t *addresses,
				      unsigned int count)
{
	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	return zd_usb_ioread16v(&chip->usb, values, addresses, count);
}

static inline int zd_ioread16_locked(struct zd_chip *chip, u16 *value,
	                             const zd_addr_t addr)
{
	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	return zd_usb_ioread16(&chip->usb, value, addr);
}

int zd_ioread32v_locked(struct zd_chip *chip, u32 *values,
	                const zd_addr_t *addresses, unsigned int count);

static inline int zd_ioread32_locked(struct zd_chip *chip, u32 *value,
	                             const zd_addr_t addr)
{
	return zd_ioread32v_locked(chip, value, (const zd_addr_t *)&addr, 1);
}

static inline int zd_iowrite16_locked(struct zd_chip *chip, u16 value,
	                              zd_addr_t addr)
{
	struct zd_ioreq16 ioreq;

	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	ioreq.addr = addr;
	ioreq.value = value;

	return zd_usb_iowrite16v(&chip->usb, &ioreq, 1);
}

int zd_iowrite16a_locked(struct zd_chip *chip,
                         const struct zd_ioreq16 *ioreqs, unsigned int count);

int _zd_iowrite32v_locked(struct zd_chip *chip, const struct zd_ioreq32 *ioreqs,
			  unsigned int count);

static inline int zd_iowrite32_locked(struct zd_chip *chip, u32 value,
	                              zd_addr_t addr)
{
	struct zd_ioreq32 ioreq;

	ioreq.addr = addr;
	ioreq.value = value;

	return _zd_iowrite32v_locked(chip, &ioreq, 1);
}

int zd_iowrite32a_locked(struct zd_chip *chip,
	                 const struct zd_ioreq32 *ioreqs, unsigned int count);

static inline int zd_rfwrite_locked(struct zd_chip *chip, u32 value, u8 bits)
{
	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	return zd_usb_rfwrite(&chip->usb, value, bits);
}

int zd_rfwrite_cr_locked(struct zd_chip *chip, u32 value);

int zd_rfwritev_locked(struct zd_chip *chip,
	               const u32* values, unsigned int count, u8 bits);
int zd_rfwritev_cr_locked(struct zd_chip *chip,
	                  const u32* values, unsigned int count);

/* Locking functions for reading and writing registers.
 * The different parameters are intentional.
 */
int zd_ioread16(struct zd_chip *chip, zd_addr_t addr, u16 *value);
int zd_iowrite16(struct zd_chip *chip, zd_addr_t addr, u16 value);
int zd_ioread32(struct zd_chip *chip, zd_addr_t addr, u32 *value);
int zd_iowrite32(struct zd_chip *chip, zd_addr_t addr, u32 value);
int zd_ioread32v(struct zd_chip *chip, const zd_addr_t *addresses,
	          u32 *values, unsigned int count);
int zd_iowrite32a(struct zd_chip *chip, const struct zd_ioreq32 *ioreqs,
	           unsigned int count);

int zd_chip_set_channel(struct zd_chip *chip, u8 channel);
static inline u8 _zd_chip_get_channel(struct zd_chip *chip)
{
	return chip->rf.channel;
}
u8  zd_chip_get_channel(struct zd_chip *chip);
int zd_read_regdomain(struct zd_chip *chip, u8 *regdomain);
int zd_write_mac_addr(struct zd_chip *chip, const u8 *mac_addr);
int zd_chip_switch_radio_on(struct zd_chip *chip);
int zd_chip_switch_radio_off(struct zd_chip *chip);
int zd_chip_enable_int(struct zd_chip *chip);
void zd_chip_disable_int(struct zd_chip *chip);
int zd_chip_enable_rxtx(struct zd_chip *chip);
void zd_chip_disable_rxtx(struct zd_chip *chip);
int zd_chip_enable_hwint(struct zd_chip *chip);
int zd_chip_disable_hwint(struct zd_chip *chip);
int zd_chip_generic_patch_6m_band(struct zd_chip *chip, int channel);
int zd_chip_set_rts_cts_rate_locked(struct zd_chip *chip, int preamble);

static inline int zd_get_encryption_type(struct zd_chip *chip, u32 *type)
{
	return zd_ioread32(chip, CR_ENCRYPTION_TYPE, type);
}

static inline int zd_set_encryption_type(struct zd_chip *chip, u32 type)
{
	return zd_iowrite32(chip, CR_ENCRYPTION_TYPE, type);
}

static inline int zd_chip_get_basic_rates(struct zd_chip *chip, u16 *cr_rates)
{
	return zd_ioread16(chip, CR_BASIC_RATE_TBL, cr_rates);
}

int zd_chip_set_basic_rates(struct zd_chip *chip, u16 cr_rates);

int zd_chip_lock_phy_regs(struct zd_chip *chip);
int zd_chip_unlock_phy_regs(struct zd_chip *chip);

enum led_status {
	ZD_LED_OFF = 0,
	ZD_LED_SCANNING = 1,
	ZD_LED_ASSOCIATED = 2,
};

int zd_chip_control_leds(struct zd_chip *chip, enum led_status status);

int zd_set_beacon_interval(struct zd_chip *chip, u32 interval);

static inline int zd_get_beacon_interval(struct zd_chip *chip, u32 *interval)
{
	return zd_ioread32(chip, CR_BCN_INTERVAL, interval);
}

struct rx_status;

u8 zd_rx_qual_percent(const void *rx_frame, unsigned int size,
	               const struct rx_status *status);

u8 zd_rx_rate(const void *rx_frame, const struct rx_status *status);

struct zd_mc_hash {
	u32 low;
	u32 high;
};

static inline void zd_mc_clear(struct zd_mc_hash *hash)
{
	hash->low = 0;
	/* The interfaces must always received broadcasts.
	 * The hash of the broadcast address ff:ff:ff:ff:ff:ff is 63.
	 */
	hash->high = 0x80000000;
}

static inline void zd_mc_add_all(struct zd_mc_hash *hash)
{
	hash->low = hash->high = 0xffffffff;
}

static inline void zd_mc_add_addr(struct zd_mc_hash *hash, u8 *addr)
{
	unsigned int i = addr[5] >> 2;
	if (i < 32) {
		hash->low |= 1 << i;
	} else {
		hash->high |= 1 << (i-32);
	}
}

int zd_chip_set_multicast_hash(struct zd_chip *chip,
	                       struct zd_mc_hash *hash);

u64 zd_chip_get_tsf(struct zd_chip *chip);

#endif /* _ZD_CHIP_H */
