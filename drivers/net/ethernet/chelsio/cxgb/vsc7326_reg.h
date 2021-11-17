/* SPDX-License-Identifier: GPL-2.0 */
/* $Date: 2006/04/28 19:20:17 $ $RCSfile: vsc7326_reg.h,v $ $Revision: 1.5 $ */
#ifndef _VSC7321_REG_H_
#define _VSC7321_REG_H_

/* Register definitions for Vitesse VSC7321 (Meigs II) MAC
 *
 * Straight off the data sheet, VMDS-10038 Rev 2.0 and
 * PD0011-01-14-Meigs-II 2002-12-12
 */

/* Just 'cause it's in here doesn't mean it's used. */

#define CRA(blk,sub,adr) ((((blk) & 0x7) << 13) | (((sub) & 0xf) << 9) | (((adr) & 0xff) << 1))

/* System and CPU comm's registers */
#define REG_CHIP_ID		CRA(0x7,0xf,0x00)	/* Chip ID */
#define REG_BLADE_ID		CRA(0x7,0xf,0x01)	/* Blade ID */
#define REG_SW_RESET		CRA(0x7,0xf,0x02)	/* Global Soft Reset */
#define REG_MEM_BIST		CRA(0x7,0xf,0x04)	/* mem */
#define REG_IFACE_MODE		CRA(0x7,0xf,0x07)	/* Interface mode */
#define REG_MSCH		CRA(0x7,0x2,0x06)	/* CRC error count */
#define REG_CRC_CNT		CRA(0x7,0x2,0x0a)	/* CRC error count */
#define REG_CRC_CFG		CRA(0x7,0x2,0x0b)	/* CRC config */
#define REG_SI_TRANSFER_SEL	CRA(0x7,0xf,0x18)	/* SI Transfer Select */
#define REG_PLL_CLK_SPEED	CRA(0x7,0xf,0x19)	/* Clock Speed Selection */
#define REG_SYS_CLK_SELECT	CRA(0x7,0xf,0x1c)	/* System Clock Select */
#define REG_GPIO_CTRL		CRA(0x7,0xf,0x1d)	/* GPIO Control */
#define REG_GPIO_OUT		CRA(0x7,0xf,0x1e)	/* GPIO Out */
#define REG_GPIO_IN		CRA(0x7,0xf,0x1f)	/* GPIO In */
#define REG_CPU_TRANSFER_SEL	CRA(0x7,0xf,0x20)	/* CPU Transfer Select */
#define REG_LOCAL_DATA		CRA(0x7,0xf,0xfe)	/* Local CPU Data Register */
#define REG_LOCAL_STATUS	CRA(0x7,0xf,0xff)	/* Local CPU Status Register */

/* Aggregator registers */
#define REG_AGGR_SETUP		CRA(0x7,0x1,0x00)	/* Aggregator Setup */
#define REG_PMAP_TABLE		CRA(0x7,0x1,0x01)	/* Port map table */
#define REG_MPLS_BIT0		CRA(0x7,0x1,0x08)	/* MPLS bit0 position */
#define REG_MPLS_BIT1		CRA(0x7,0x1,0x09)	/* MPLS bit1 position */
#define REG_MPLS_BIT2		CRA(0x7,0x1,0x0a)	/* MPLS bit2 position */
#define REG_MPLS_BIT3		CRA(0x7,0x1,0x0b)	/* MPLS bit3 position */
#define REG_MPLS_BITMASK	CRA(0x7,0x1,0x0c)	/* MPLS bit mask */
#define REG_PRE_BIT0POS		CRA(0x7,0x1,0x10)	/* Preamble bit0 position */
#define REG_PRE_BIT1POS		CRA(0x7,0x1,0x11)	/* Preamble bit1 position */
#define REG_PRE_BIT2POS		CRA(0x7,0x1,0x12)	/* Preamble bit2 position */
#define REG_PRE_BIT3POS		CRA(0x7,0x1,0x13)	/* Preamble bit3 position */
#define REG_PRE_ERR_CNT		CRA(0x7,0x1,0x14)	/* Preamble parity error count */

/* BIST registers */
/*#define REG_RAM_BIST_CMD	CRA(0x7,0x2,0x00)*/	/* RAM BIST Command Register */
/*#define REG_RAM_BIST_RESULT	CRA(0x7,0x2,0x01)*/	/* RAM BIST Read Status/Result */
#define REG_RAM_BIST_CMD	CRA(0x7,0x1,0x00)	/* RAM BIST Command Register */
#define REG_RAM_BIST_RESULT	CRA(0x7,0x1,0x01)	/* RAM BIST Read Status/Result */
#define   BIST_PORT_SELECT	0x00			/* BIST port select */
#define   BIST_COMMAND		0x01			/* BIST enable/disable */
#define   BIST_STATUS		0x02			/* BIST operation status */
#define   BIST_ERR_CNT_LSB	0x03			/* BIST error count lo 8b */
#define   BIST_ERR_CNT_MSB	0x04			/* BIST error count hi 8b */
#define   BIST_ERR_SEL_LSB	0x05			/* BIST error select lo 8b */
#define   BIST_ERR_SEL_MSB	0x06			/* BIST error select hi 8b */
#define   BIST_ERROR_STATE	0x07			/* BIST engine internal state */
#define   BIST_ERR_ADR0		0x08			/* BIST error address lo 8b */
#define   BIST_ERR_ADR1		0x09			/* BIST error address lomid 8b */
#define   BIST_ERR_ADR2		0x0a			/* BIST error address himid 8b */
#define   BIST_ERR_ADR3		0x0b			/* BIST error address hi 8b */

/* FIFO registers
 *   ie = 0 for ingress, 1 for egress
 *   fn = FIFO number, 0-9
 */
#define REG_TEST(ie,fn)		CRA(0x2,ie&1,0x00+fn)	/* Mode & Test Register */
#define REG_TOP_BOTTOM(ie,fn)	CRA(0x2,ie&1,0x10+fn)	/* FIFO Buffer Top & Bottom */
#define REG_TAIL(ie,fn)		CRA(0x2,ie&1,0x20+fn)	/* FIFO Write Pointer */
#define REG_HEAD(ie,fn)		CRA(0x2,ie&1,0x30+fn)	/* FIFO Read Pointer */
#define REG_HIGH_LOW_WM(ie,fn)	CRA(0x2,ie&1,0x40+fn)	/* Flow Control Water Marks */
#define REG_CT_THRHLD(ie,fn)	CRA(0x2,ie&1,0x50+fn)	/* Cut Through Threshold */
#define REG_FIFO_DROP_CNT(ie,fn) CRA(0x2,ie&1,0x60+fn)	/* Drop & CRC Error Counter */
#define REG_DEBUG_BUF_CNT(ie,fn) CRA(0x2,ie&1,0x70+fn)	/* Input Side Debug Counter */
#define REG_BUCKI(fn) CRA(0x2,2,0x20+fn)	/* Input Side Debug Counter */
#define REG_BUCKE(fn) CRA(0x2,3,0x20+fn)	/* Input Side Debug Counter */

/* Traffic shaper buckets
 *   ie = 0 for ingress, 1 for egress
 *   bn = bucket number 0-10 (yes, 11 buckets)
 */
/* OK, this one's kinda ugly.  Some hardware designers are perverse. */
#define REG_TRAFFIC_SHAPER_BUCKET(ie,bn) CRA(0x2,ie&1,0x0a + (bn>7) | ((bn&7)<<4))
#define REG_TRAFFIC_SHAPER_CONTROL(ie)	CRA(0x2,ie&1,0x3b)

#define REG_SRAM_ADR(ie)	CRA(0x2,ie&1,0x0e)	/* FIFO SRAM address */
#define REG_SRAM_WR_STRB(ie)	CRA(0x2,ie&1,0x1e)	/* FIFO SRAM write strobe */
#define REG_SRAM_RD_STRB(ie)	CRA(0x2,ie&1,0x2e)	/* FIFO SRAM read strobe */
#define REG_SRAM_DATA_0(ie)	CRA(0x2,ie&1,0x3e)	/* FIFO SRAM data lo 8b */
#define REG_SRAM_DATA_1(ie)	CRA(0x2,ie&1,0x4e)	/* FIFO SRAM data lomid 8b */
#define REG_SRAM_DATA_2(ie)	CRA(0x2,ie&1,0x5e)	/* FIFO SRAM data himid 8b */
#define REG_SRAM_DATA_3(ie)	CRA(0x2,ie&1,0x6e)	/* FIFO SRAM data hi 8b */
#define REG_SRAM_DATA_BLK_TYPE(ie) CRA(0x2,ie&1,0x7e)	/* FIFO SRAM tag */
/* REG_ING_CONTROL equals REG_CONTROL with ie = 0, likewise REG_EGR_CONTROL is ie = 1 */
#define REG_CONTROL(ie)		CRA(0x2,ie&1,0x0f)	/* FIFO control */
#define REG_ING_CONTROL		CRA(0x2,0x0,0x0f)	/* Ingress control (alias) */
#define REG_EGR_CONTROL		CRA(0x2,0x1,0x0f)	/* Egress control (alias) */
#define REG_AGE_TIMER(ie)	CRA(0x2,ie&1,0x1f)	/* Aging timer */
#define REG_AGE_INC(ie)		CRA(0x2,ie&1,0x2f)	/* Aging increment */
#define DEBUG_OUT(ie)		CRA(0x2,ie&1,0x3f)	/* Output debug counter control */
#define DEBUG_CNT(ie)		CRA(0x2,ie&1,0x4f)	/* Output debug counter */

/* SPI4 interface */
#define REG_SPI4_MISC		CRA(0x5,0x0,0x00)	/* Misc Register */
#define REG_SPI4_STATUS		CRA(0x5,0x0,0x01)	/* CML Status */
#define REG_SPI4_ING_SETUP0	CRA(0x5,0x0,0x02)	/* Ingress Status Channel Setup */
#define REG_SPI4_ING_SETUP1	CRA(0x5,0x0,0x03)	/* Ingress Data Training Setup */
#define REG_SPI4_ING_SETUP2	CRA(0x5,0x0,0x04)	/* Ingress Data Burst Size Setup */
#define REG_SPI4_EGR_SETUP0	CRA(0x5,0x0,0x05)	/* Egress Status Channel Setup */
#define REG_SPI4_DBG_CNT(n)	CRA(0x5,0x0,0x10+n)	/* Debug counters 0-9 */
#define REG_SPI4_DBG_SETUP	CRA(0x5,0x0,0x1A)	/* Debug counters setup */
#define REG_SPI4_TEST		CRA(0x5,0x0,0x20)	/* Test Setup Register */
#define REG_TPGEN_UP0		CRA(0x5,0x0,0x21)	/* Test Pattern generator user pattern 0 */
#define REG_TPGEN_UP1		CRA(0x5,0x0,0x22)	/* Test Pattern generator user pattern 1 */
#define REG_TPCHK_UP0		CRA(0x5,0x0,0x23)	/* Test Pattern checker user pattern 0 */
#define REG_TPCHK_UP1		CRA(0x5,0x0,0x24)	/* Test Pattern checker user pattern 1 */
#define REG_TPSAM_P0		CRA(0x5,0x0,0x25)	/* Sampled pattern 0 */
#define REG_TPSAM_P1		CRA(0x5,0x0,0x26)	/* Sampled pattern 1 */
#define REG_TPERR_CNT		CRA(0x5,0x0,0x27)	/* Pattern checker error counter */
#define REG_SPI4_STICKY		CRA(0x5,0x0,0x30)	/* Sticky bits register */
#define REG_SPI4_DBG_INH	CRA(0x5,0x0,0x31)	/* Core egress & ingress inhibit */
#define REG_SPI4_DBG_STATUS	CRA(0x5,0x0,0x32)	/* Sampled ingress status */
#define REG_SPI4_DBG_GRANT	CRA(0x5,0x0,0x33)	/* Ingress cranted credit value */

#define REG_SPI4_DESKEW 	CRA(0x5,0x0,0x43)	/* Ingress cranted credit value */

/* 10GbE MAC Block Registers */
/* Note that those registers that are exactly the same for 10GbE as for
 * tri-speed are only defined with the version that needs a port number.
 * Pass 0xa in those cases.
 *
 * Also note that despite the presence of a MAC address register, this part
 * does no ingress MAC address filtering.  That register is used only for
 * pause frame detection and generation.
 */
/* 10GbE specific, and different from tri-speed */
#define REG_MISC_10G		CRA(0x1,0xa,0x00)	/* Misc 10GbE setup */
#define REG_PAUSE_10G		CRA(0x1,0xa,0x01)	/* Pause register */
#define REG_NORMALIZER_10G	CRA(0x1,0xa,0x05)	/* 10G normalizer */
#define REG_STICKY_RX		CRA(0x1,0xa,0x06)	/* RX debug register */
#define REG_DENORM_10G		CRA(0x1,0xa,0x07)	/* Denormalizer  */
#define REG_STICKY_TX		CRA(0x1,0xa,0x08)	/* TX sticky bits */
#define REG_MAX_RXHIGH		CRA(0x1,0xa,0x0a)	/* XGMII lane 0-3 debug */
#define REG_MAX_RXLOW		CRA(0x1,0xa,0x0b)	/* XGMII lane 4-7 debug */
#define REG_MAC_TX_STICKY	CRA(0x1,0xa,0x0c)	/* MAC Tx state sticky debug */
#define REG_MAC_TX_RUNNING	CRA(0x1,0xa,0x0d)	/* MAC Tx state running debug */
#define REG_TX_ABORT_AGE	CRA(0x1,0xa,0x14)	/* Aged Tx frames discarded */
#define REG_TX_ABORT_SHORT	CRA(0x1,0xa,0x15)	/* Short Tx frames discarded */
#define REG_TX_ABORT_TAXI	CRA(0x1,0xa,0x16)	/* Taxi error frames discarded */
#define REG_TX_ABORT_UNDERRUN	CRA(0x1,0xa,0x17)	/* Tx Underrun abort counter */
#define REG_TX_DENORM_DISCARD	CRA(0x1,0xa,0x18)	/* Tx denormalizer discards */
#define REG_XAUI_STAT_A		CRA(0x1,0xa,0x20)	/* XAUI status A */
#define REG_XAUI_STAT_B		CRA(0x1,0xa,0x21)	/* XAUI status B */
#define REG_XAUI_STAT_C		CRA(0x1,0xa,0x22)	/* XAUI status C */
#define REG_XAUI_CONF_A		CRA(0x1,0xa,0x23)	/* XAUI configuration A */
#define REG_XAUI_CONF_B		CRA(0x1,0xa,0x24)	/* XAUI configuration B */
#define REG_XAUI_CODE_GRP_CNT	CRA(0x1,0xa,0x25)	/* XAUI code group error count */
#define REG_XAUI_CONF_TEST_A	CRA(0x1,0xa,0x26)	/* XAUI test register A */
#define REG_PDERRCNT		CRA(0x1,0xa,0x27)	/* XAUI test register B */

/* pn = port number 0-9 for tri-speed, 10 for 10GbE */
/* Both tri-speed and 10GbE */
#define REG_MAX_LEN(pn)		CRA(0x1,pn,0x02)	/* Max length */
#define REG_MAC_HIGH_ADDR(pn)	CRA(0x1,pn,0x03)	/* Upper 24 bits of MAC addr */
#define REG_MAC_LOW_ADDR(pn)	CRA(0x1,pn,0x04)	/* Lower 24 bits of MAC addr */

/* tri-speed only
 * pn = port number, 0-9
 */
#define REG_MODE_CFG(pn)	CRA(0x1,pn,0x00)	/* Mode configuration */
#define REG_PAUSE_CFG(pn)	CRA(0x1,pn,0x01)	/* Pause configuration */
#define REG_NORMALIZER(pn)	CRA(0x1,pn,0x05)	/* Normalizer */
#define REG_TBI_STATUS(pn)	CRA(0x1,pn,0x06)	/* TBI status */
#define REG_PCS_STATUS_DBG(pn)	CRA(0x1,pn,0x07)	/* PCS status debug */
#define REG_PCS_CTRL(pn)	CRA(0x1,pn,0x08)	/* PCS control */
#define REG_TBI_CONFIG(pn)	CRA(0x1,pn,0x09)	/* TBI configuration */
#define REG_STICK_BIT(pn)	CRA(0x1,pn,0x0a)	/* Sticky bits */
#define REG_DEV_SETUP(pn)	CRA(0x1,pn,0x0b)	/* MAC clock/reset setup */
#define REG_DROP_CNT(pn)	CRA(0x1,pn,0x0c)	/* Drop counter */
#define REG_PORT_POS(pn)	CRA(0x1,pn,0x0d)	/* Preamble port position */
#define REG_PORT_FAIL(pn)	CRA(0x1,pn,0x0e)	/* Preamble port position */
#define REG_SERDES_CONF(pn)	CRA(0x1,pn,0x0f)	/* SerDes configuration */
#define REG_SERDES_TEST(pn)	CRA(0x1,pn,0x10)	/* SerDes test */
#define REG_SERDES_STAT(pn)	CRA(0x1,pn,0x11)	/* SerDes status */
#define REG_SERDES_COM_CNT(pn)	CRA(0x1,pn,0x12)	/* SerDes comma counter */
#define REG_DENORM(pn)		CRA(0x1,pn,0x15)	/* Frame denormalization */
#define REG_DBG(pn)		CRA(0x1,pn,0x16)	/* Device 1G debug */
#define REG_TX_IFG(pn)		CRA(0x1,pn,0x18)	/* Tx IFG config */
#define REG_HDX(pn)		CRA(0x1,pn,0x19)	/* Half-duplex config */

/* Statistics */
/* CRA(0x4,pn,reg) */
/* reg below */
/* pn = port number, 0-a, a = 10GbE */

enum {
	RxInBytes		= 0x00,	// # Rx in octets
	RxSymbolCarrier		= 0x01,	// Frames w/ symbol errors
	RxPause			= 0x02,	// # pause frames received
	RxUnsupOpcode		= 0x03,	// # control frames with unsupported opcode
	RxOkBytes		= 0x04,	// # octets in good frames
	RxBadBytes		= 0x05,	// # octets in bad frames
	RxUnicast		= 0x06,	// # good unicast frames
	RxMulticast		= 0x07,	// # good multicast frames
	RxBroadcast		= 0x08,	// # good broadcast frames
	Crc			= 0x09,	// # frames w/ bad CRC only
	RxAlignment		= 0x0a,	// # frames w/ alignment err
	RxUndersize		= 0x0b,	// # frames undersize
	RxFragments		= 0x0c,	// # frames undersize w/ crc err
	RxInRangeLengthError	= 0x0d,	// # frames with length error
	RxOutOfRangeError	= 0x0e,	// # frames with illegal length field
	RxOversize		= 0x0f,	// # frames oversize
	RxJabbers		= 0x10,	// # frames oversize w/ crc err
	RxSize64		= 0x11,	// # frames 64 octets long
	RxSize65To127		= 0x12,	// # frames 65-127 octets
	RxSize128To255		= 0x13,	// # frames 128-255
	RxSize256To511		= 0x14,	// # frames 256-511
	RxSize512To1023		= 0x15,	// # frames 512-1023
	RxSize1024To1518	= 0x16,	// # frames 1024-1518
	RxSize1519ToMax		= 0x17,	// # frames 1519-max

	TxOutBytes		= 0x18,	// # octets tx
	TxPause			= 0x19,	// # pause frames sent
	TxOkBytes		= 0x1a, // # octets tx OK
	TxUnicast		= 0x1b,	// # frames unicast
	TxMulticast		= 0x1c,	// # frames multicast
	TxBroadcast		= 0x1d,	// # frames broadcast
	TxMultipleColl		= 0x1e,	// # frames tx after multiple collisions
	TxLateColl		= 0x1f,	// # late collisions detected
	TxXcoll			= 0x20,	// # frames lost, excessive collisions
	TxDefer			= 0x21,	// # frames deferred on first tx attempt
	TxXdefer		= 0x22,	// # frames excessively deferred
	TxCsense		= 0x23,	// carrier sense errors at frame end
	TxSize64		= 0x24,	// # frames 64 octets long
	TxSize65To127		= 0x25,	// # frames 65-127 octets
	TxSize128To255		= 0x26,	// # frames 128-255
	TxSize256To511		= 0x27,	// # frames 256-511
	TxSize512To1023		= 0x28,	// # frames 512-1023
	TxSize1024To1518	= 0x29,	// # frames 1024-1518
	TxSize1519ToMax		= 0x2a,	// # frames 1519-max
	TxSingleColl		= 0x2b,	// # frames tx after single collision
	TxBackoff2		= 0x2c,	// # frames tx ok after 2 backoffs/collisions
	TxBackoff3		= 0x2d,	//   after 3 backoffs/collisions
	TxBackoff4		= 0x2e,	//   after 4
	TxBackoff5		= 0x2f,	//   after 5
	TxBackoff6		= 0x30,	//   after 6
	TxBackoff7		= 0x31,	//   after 7
	TxBackoff8		= 0x32,	//   after 8
	TxBackoff9		= 0x33,	//   after 9
	TxBackoff10		= 0x34,	//   after 10
	TxBackoff11		= 0x35,	//   after 11
	TxBackoff12		= 0x36,	//   after 12
	TxBackoff13		= 0x37,	//   after 13
	TxBackoff14		= 0x38,	//   after 14
	TxBackoff15		= 0x39,	//   after 15
	TxUnderrun		= 0x3a,	// # frames dropped from underrun
	// Hole. See REG_RX_XGMII_PROT_ERR below.
	RxIpgShrink		= 0x3c,	// # of IPG shrinks detected
	// Duplicate. See REG_STAT_STICKY10G below.
	StatSticky1G		= 0x3e,	// tri-speed sticky bits
	StatInit		= 0x3f	// Clear all statistics
};

#define REG_RX_XGMII_PROT_ERR	CRA(0x4,0xa,0x3b)		/* # protocol errors detected on XGMII interface */
#define REG_STAT_STICKY10G	CRA(0x4,0xa,StatSticky1G)	/* 10GbE sticky bits */

#define REG_RX_OK_BYTES(pn)	CRA(0x4,pn,RxOkBytes)
#define REG_RX_BAD_BYTES(pn)	CRA(0x4,pn,RxBadBytes)
#define REG_TX_OK_BYTES(pn)	CRA(0x4,pn,TxOkBytes)

/* MII-Management Block registers */
/* These are for MII-M interface 0, which is the bidirectional LVTTL one.  If
 * we hooked up to the one with separate directions, the middle 0x0 needs to
 * change to 0x1.  And the current errata states that MII-M 1 doesn't work.
 */

#define REG_MIIM_STATUS		CRA(0x3,0x0,0x00)	/* MII-M Status */
#define REG_MIIM_CMD		CRA(0x3,0x0,0x01)	/* MII-M Command */
#define REG_MIIM_DATA		CRA(0x3,0x0,0x02)	/* MII-M Data */
#define REG_MIIM_PRESCALE	CRA(0x3,0x0,0x03)	/* MII-M MDC Prescale */

#define REG_ING_FFILT_UM_EN	CRA(0x2, 0, 0xd)
#define REG_ING_FFILT_BE_EN	CRA(0x2, 0, 0x1d)
#define REG_ING_FFILT_VAL0	CRA(0x2, 0, 0x2d)
#define REG_ING_FFILT_VAL1	CRA(0x2, 0, 0x3d)
#define REG_ING_FFILT_MASK0	CRA(0x2, 0, 0x4d)
#define REG_ING_FFILT_MASK1	CRA(0x2, 0, 0x5d)
#define REG_ING_FFILT_MASK2	CRA(0x2, 0, 0x6d)
#define REG_ING_FFILT_ETYPE	CRA(0x2, 0, 0x7d)


/* Whew. */

#endif
