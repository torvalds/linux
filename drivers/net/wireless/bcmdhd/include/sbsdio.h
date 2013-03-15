/*
 * SDIO device core hardware definitions.
 * sdio is a portion of the pcmcia core in core rev 3 - rev 8
 *
 * SDIO core support 1bit, 4 bit SDIO mode as well as SPI mode.
 *
 * Copyright (C) 1999-2013, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: sbsdio.h 383835 2013-02-07 23:32:39Z $
 */

#ifndef	_SBSDIO_H
#define	_SBSDIO_H

#define SBSDIO_NUM_FUNCTION		3	/* as of sdiod rev 0, supports 3 functions */

/* function 1 miscellaneous registers */
#define SBSDIO_SPROM_CS			0x10000		/* sprom command and status */
#define SBSDIO_SPROM_INFO		0x10001		/* sprom info register */
#define SBSDIO_SPROM_DATA_LOW		0x10002		/* sprom indirect access data byte 0 */
#define SBSDIO_SPROM_DATA_HIGH		0x10003 	/* sprom indirect access data byte 1 */
#define SBSDIO_SPROM_ADDR_LOW		0x10004		/* sprom indirect access addr byte 0 */
#define SBSDIO_SPROM_ADDR_HIGH		0x10005		/* sprom indirect access addr byte 0 */
#define SBSDIO_CHIP_CTRL_DATA		0x10006		/* xtal_pu (gpio) output */
#define SBSDIO_CHIP_CTRL_EN		0x10007		/* xtal_pu (gpio) enable */
#define SBSDIO_WATERMARK		0x10008		/* rev < 7, watermark for sdio device */
#define SBSDIO_DEVICE_CTL		0x10009		/* control busy signal generation */

/* registers introduced in rev 8, some content (mask/bits) defs in sbsdpcmdev.h */
#define SBSDIO_FUNC1_SBADDRLOW		0x1000A		/* SB Address Window Low (b15) */
#define SBSDIO_FUNC1_SBADDRMID		0x1000B		/* SB Address Window Mid (b23:b16) */
#define SBSDIO_FUNC1_SBADDRHIGH		0x1000C		/* SB Address Window High (b31:b24)    */
#define SBSDIO_FUNC1_FRAMECTRL		0x1000D		/* Frame Control (frame term/abort) */
#define SBSDIO_FUNC1_CHIPCLKCSR		0x1000E		/* ChipClockCSR (ALP/HT ctl/status) */
#define SBSDIO_FUNC1_SDIOPULLUP 	0x1000F		/* SdioPullUp (on cmd, d0-d2) */
#define SBSDIO_FUNC1_WFRAMEBCLO		0x10019		/* Write Frame Byte Count Low */
#define SBSDIO_FUNC1_WFRAMEBCHI		0x1001A		/* Write Frame Byte Count High */
#define SBSDIO_FUNC1_RFRAMEBCLO		0x1001B		/* Read Frame Byte Count Low */
#define SBSDIO_FUNC1_RFRAMEBCHI		0x1001C		/* Read Frame Byte Count High */
#define SBSDIO_FUNC1_MESBUSYCTRL	0x1001D		/* MesBusyCtl at 0x1001D (rev 11) */

#define SBSDIO_FUNC1_MISC_REG_START	0x10000 	/* f1 misc register start */
#define SBSDIO_FUNC1_MISC_REG_LIMIT	0x1001C 	/* f1 misc register end */

/* Sdio Core Rev 12 */
#define SBSDIO_FUNC1_WAKEUPCTRL			0x1001E
#define SBSDIO_FUNC1_WCTRL_ALPWAIT_MASK		0x1
#define SBSDIO_FUNC1_WCTRL_ALPWAIT_SHIFT	0
#define SBSDIO_FUNC1_WCTRL_HTWAIT_MASK		0x2
#define SBSDIO_FUNC1_WCTRL_HTWAIT_SHIFT		1
#define SBSDIO_FUNC1_SLEEPCSR			0x1001F
#define SBSDIO_FUNC1_SLEEPCSR_KSO_MASK		0x1
#define SBSDIO_FUNC1_SLEEPCSR_KSO_SHIFT		0
#define SBSDIO_FUNC1_SLEEPCSR_KSO_EN		1
#define SBSDIO_FUNC1_SLEEPCSR_DEVON_MASK	0x2
#define SBSDIO_FUNC1_SLEEPCSR_DEVON_SHIFT	1

/* SBSDIO_SPROM_CS */
#define SBSDIO_SPROM_IDLE		0
#define SBSDIO_SPROM_WRITE		1
#define SBSDIO_SPROM_READ		2
#define SBSDIO_SPROM_WEN		4
#define SBSDIO_SPROM_WDS		7
#define SBSDIO_SPROM_DONE		8

/* SBSDIO_SPROM_INFO */
#define SROM_SZ_MASK			0x03		/* SROM size, 1: 4k, 2: 16k */
#define SROM_BLANK			0x04		/* depreciated in corerev 6 */
#define	SROM_OTP			0x80		/* OTP present */

/* SBSDIO_CHIP_CTRL */
#define SBSDIO_CHIP_CTRL_XTAL		0x01		/* or'd with onchip xtal_pu,
							 * 1: power on oscillator
							 * (for 4318 only)
							 */
/* SBSDIO_WATERMARK */
#define SBSDIO_WATERMARK_MASK		0x7f		/* number of words - 1 for sd device
							 * to wait before sending data to host
							 */

/* SBSDIO_MESBUSYCTRL */
/* When RX FIFO has less entries than this & MBE is set
 * => busy signal is asserted between data blocks.
*/
#define SBSDIO_MESBUSYCTRL_MASK		0x7f
#define SBSDIO_MESBUSYCTRL_ENAB		0x80		/* Enable busy capability for MES access */

/* SBSDIO_DEVICE_CTL */
#define SBSDIO_DEVCTL_SETBUSY		0x01		/* 1: device will assert busy signal when
							 * receiving CMD53
							 */
#define SBSDIO_DEVCTL_SPI_INTR_SYNC	0x02		/* 1: assertion of sdio interrupt is
							 * synchronous to the sdio clock
							 */
#define SBSDIO_DEVCTL_CA_INT_ONLY	0x04		/* 1: mask all interrupts to host
							 * except the chipActive (rev 8)
							 */
#define SBSDIO_DEVCTL_PADS_ISO		0x08		/* 1: isolate internal sdio signals, put
							 * external pads in tri-state; requires
							 * sdio bus power cycle to clear (rev 9)
							 */
#define SBSDIO_DEVCTL_EN_F2_BLK_WATERMARK 0x10  /* Enable function 2 tx for each block */
#define SBSDIO_DEVCTL_F2WM_ENAB		0x10		/* Enable F2 Watermark */
#define SBSDIO_DEVCTL_NONDAT_PADS_ISO 	0x20		/* Isolate sdio clk and cmd (non-data) */

/* SBSDIO_FUNC1_CHIPCLKCSR */
#define SBSDIO_FORCE_ALP		0x01		/* Force ALP request to backplane */
#define SBSDIO_FORCE_HT			0x02		/* Force HT request to backplane */
#define SBSDIO_FORCE_ILP		0x04		/* Force ILP request to backplane */
#define SBSDIO_ALP_AVAIL_REQ		0x08		/* Make ALP ready (power up xtal) */
#define SBSDIO_HT_AVAIL_REQ		0x10		/* Make HT ready (power up PLL) */
#define SBSDIO_FORCE_HW_CLKREQ_OFF	0x20		/* Squelch clock requests from HW */
#define SBSDIO_ALP_AVAIL		0x40		/* Status: ALP is ready */
#define SBSDIO_HT_AVAIL			0x80		/* Status: HT is ready */
/* In rev8, actual avail bits followed original docs */
#define SBSDIO_Rev8_HT_AVAIL		0x40
#define SBSDIO_Rev8_ALP_AVAIL		0x80
#define SBSDIO_CSR_MASK			0x1F

#define SBSDIO_AVBITS			(SBSDIO_HT_AVAIL | SBSDIO_ALP_AVAIL)
#define SBSDIO_ALPAV(regval)		((regval) & SBSDIO_AVBITS)
#define SBSDIO_HTAV(regval)		(((regval) & SBSDIO_AVBITS) == SBSDIO_AVBITS)
#define SBSDIO_ALPONLY(regval)		(SBSDIO_ALPAV(regval) && !SBSDIO_HTAV(regval))
#define SBSDIO_CLKAV(regval, alponly)	(SBSDIO_ALPAV(regval) && \
					(alponly ? 1 : SBSDIO_HTAV(regval)))

/* SBSDIO_FUNC1_SDIOPULLUP */
#define SBSDIO_PULLUP_D0		0x01		/* Enable D0/MISO pullup */
#define SBSDIO_PULLUP_D1		0x02		/* Enable D1/INT# pullup */
#define SBSDIO_PULLUP_D2		0x04		/* Enable D2 pullup */
#define SBSDIO_PULLUP_CMD		0x08		/* Enable CMD/MOSI pullup */
#define SBSDIO_PULLUP_ALL		0x0f		/* All valid bits */

/* function 1 OCP space */
#define SBSDIO_SB_OFT_ADDR_MASK		0x07FFF		/* sb offset addr is <= 15 bits, 32k */
#define SBSDIO_SB_OFT_ADDR_LIMIT	0x08000
#define SBSDIO_SB_ACCESS_2_4B_FLAG	0x08000		/* with b15, maps to 32-bit SB access */

/* some duplication with sbsdpcmdev.h here */
/* valid bits in SBSDIO_FUNC1_SBADDRxxx regs */
#define SBSDIO_SBADDRLOW_MASK		0x80		/* Valid bits in SBADDRLOW */
#define SBSDIO_SBADDRMID_MASK		0xff		/* Valid bits in SBADDRMID */
#define SBSDIO_SBADDRHIGH_MASK		0xffU		/* Valid bits in SBADDRHIGH */
#define SBSDIO_SBWINDOW_MASK		0xffff8000	/* Address bits from SBADDR regs */

/* direct(mapped) cis space */
#define SBSDIO_CIS_BASE_COMMON		0x1000		/* MAPPED common CIS address */
#define SBSDIO_CIS_SIZE_LIMIT		0x200		/* maximum bytes in one CIS */
#define SBSDIO_OTP_CIS_SIZE_LIMIT       0x078           /* maximum bytes OTP CIS */

#define SBSDIO_CIS_OFT_ADDR_MASK	0x1FFFF		/* cis offset addr is < 17 bits */

#define SBSDIO_CIS_MANFID_TUPLE_LEN	6		/* manfid tuple length, include tuple,
							 * link bytes
							 */

/* indirect cis access (in sprom) */
#define SBSDIO_SPROM_CIS_OFFSET		0x8		/* 8 control bytes first, CIS starts from
							 * 8th byte
							 */

#define SBSDIO_BYTEMODE_DATALEN_MAX	64		/* sdio byte mode: maximum length of one
							 * data comamnd
							 */

#define SBSDIO_CORE_ADDR_MASK		0x1FFFF		/* sdio core function one address mask */

#endif	/* _SBSDIO_H */
