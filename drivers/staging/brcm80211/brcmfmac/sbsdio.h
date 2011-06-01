/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_SBSDIO_H
#define	_SBSDIO_H

#define SBSDIO_NUM_FUNCTION		3	/* as of sdiod rev 0, supports 3 functions */

/* function 1 miscellaneous registers */
#define SBSDIO_SPROM_CS			0x10000	/* sprom command and status */
#define SBSDIO_SPROM_INFO		0x10001	/* sprom info register */
#define SBSDIO_SPROM_DATA_LOW		0x10002	/* sprom indirect access data byte 0 */
#define SBSDIO_SPROM_DATA_HIGH		0x10003	/* sprom indirect access data byte 1 */
#define SBSDIO_SPROM_ADDR_LOW		0x10004	/* sprom indirect access addr byte 0 */
#define SBSDIO_SPROM_ADDR_HIGH		0x10005	/* sprom indirect access addr byte 0 */
#define SBSDIO_CHIP_CTRL_DATA		0x10006	/* xtal_pu (gpio) output */
#define SBSDIO_CHIP_CTRL_EN		0x10007	/* xtal_pu (gpio) enable */
#define SBSDIO_WATERMARK		0x10008	/* rev < 7, watermark for sdio device */
#define SBSDIO_DEVICE_CTL		0x10009	/* control busy signal generation */

/* registers introduced in rev 8, some content (mask/bits) defs in sbsdpcmdev.h */
#define SBSDIO_FUNC1_SBADDRLOW		0x1000A	/* SB Address Window Low (b15) */
#define SBSDIO_FUNC1_SBADDRMID		0x1000B	/* SB Address Window Mid (b23:b16) */
#define SBSDIO_FUNC1_SBADDRHIGH		0x1000C	/* SB Address Window High (b31:b24)    */
#define SBSDIO_FUNC1_FRAMECTRL		0x1000D	/* Frame Control (frame term/abort) */
#define SBSDIO_FUNC1_CHIPCLKCSR		0x1000E	/* ChipClockCSR (ALP/HT ctl/status) */
#define SBSDIO_FUNC1_SDIOPULLUP 	0x1000F	/* SdioPullUp (on cmd, d0-d2) */
#define SBSDIO_FUNC1_WFRAMEBCLO		0x10019	/* Write Frame Byte Count Low */
#define SBSDIO_FUNC1_WFRAMEBCHI		0x1001A	/* Write Frame Byte Count High */
#define SBSDIO_FUNC1_RFRAMEBCLO		0x1001B	/* Read Frame Byte Count Low */
#define SBSDIO_FUNC1_RFRAMEBCHI		0x1001C	/* Read Frame Byte Count High */

#define SBSDIO_FUNC1_MISC_REG_START	0x10000	/* f1 misc register start */
#define SBSDIO_FUNC1_MISC_REG_LIMIT	0x1001C	/* f1 misc register end */

/* SBSDIO_SPROM_CS */
#define SBSDIO_SPROM_IDLE		0
#define SBSDIO_SPROM_WRITE		1
#define SBSDIO_SPROM_READ		2
#define SBSDIO_SPROM_WEN		4
#define SBSDIO_SPROM_WDS		7
#define SBSDIO_SPROM_DONE		8

/* SBSDIO_SPROM_INFO */
#define SROM_SZ_MASK			0x03	/* SROM size, 1: 4k, 2: 16k */
#define SROM_BLANK			0x04	/* depreciated in corerev 6 */
#define	SROM_OTP			0x80	/* OTP present */

/* SBSDIO_CHIP_CTRL */
#define SBSDIO_CHIP_CTRL_XTAL		0x01	/* or'd with onchip xtal_pu,
						 * 1: power on oscillator
						 * (for 4318 only)
						 */
/* SBSDIO_WATERMARK */
#define SBSDIO_WATERMARK_MASK		0x7f	/* number of words - 1 for sd device
						 * to wait before sending data to host
						 */

/* SBSDIO_DEVICE_CTL */
#define SBSDIO_DEVCTL_SETBUSY		0x01	/* 1: device will assert busy signal when
						 * receiving CMD53
						 */
#define SBSDIO_DEVCTL_SPI_INTR_SYNC	0x02	/* 1: assertion of sdio interrupt is
						 * synchronous to the sdio clock
						 */
#define SBSDIO_DEVCTL_CA_INT_ONLY	0x04	/* 1: mask all interrupts to host
						 * except the chipActive (rev 8)
						 */
#define SBSDIO_DEVCTL_PADS_ISO		0x08	/* 1: isolate internal sdio signals, put
						 * external pads in tri-state; requires
						 * sdio bus power cycle to clear (rev 9)
						 */
#define SBSDIO_DEVCTL_SB_RST_CTL	0x30	/* Force SD->SB reset mapping (rev 11) */
#define SBSDIO_DEVCTL_RST_CORECTL	0x00	/*   Determined by CoreControl bit */
#define SBSDIO_DEVCTL_RST_BPRESET	0x10	/*   Force backplane reset */
#define SBSDIO_DEVCTL_RST_NOBPRESET	0x20	/*   Force no backplane reset */

/* SBSDIO_FUNC1_CHIPCLKCSR */
#define SBSDIO_FORCE_ALP		0x01	/* Force ALP request to backplane */
#define SBSDIO_FORCE_HT			0x02	/* Force HT request to backplane */
#define SBSDIO_FORCE_ILP		0x04	/* Force ILP request to backplane */
#define SBSDIO_ALP_AVAIL_REQ		0x08	/* Make ALP ready (power up xtal) */
#define SBSDIO_HT_AVAIL_REQ		0x10	/* Make HT ready (power up PLL) */
#define SBSDIO_FORCE_HW_CLKREQ_OFF	0x20	/* Squelch clock requests from HW */
#define SBSDIO_ALP_AVAIL		0x40	/* Status: ALP is ready */
#define SBSDIO_HT_AVAIL			0x80	/* Status: HT is ready */
/* In rev8, actual avail bits followed original docs */
#define SBSDIO_Rev8_HT_AVAIL		0x40
#define SBSDIO_Rev8_ALP_AVAIL		0x80

#define SBSDIO_AVBITS			(SBSDIO_HT_AVAIL | SBSDIO_ALP_AVAIL)
#define SBSDIO_ALPAV(regval)		((regval) & SBSDIO_AVBITS)
#define SBSDIO_HTAV(regval)		(((regval) & SBSDIO_AVBITS) == SBSDIO_AVBITS)
#define SBSDIO_ALPONLY(regval)		(SBSDIO_ALPAV(regval) && !SBSDIO_HTAV(regval))
#define SBSDIO_CLKAV(regval, alponly)	(SBSDIO_ALPAV(regval) && \
					(alponly ? 1 : SBSDIO_HTAV(regval)))

/* SBSDIO_FUNC1_SDIOPULLUP */
#define SBSDIO_PULLUP_D0		0x01	/* Enable D0/MISO pullup */
#define SBSDIO_PULLUP_D1		0x02	/* Enable D1/INT# pullup */
#define SBSDIO_PULLUP_D2		0x04	/* Enable D2 pullup */
#define SBSDIO_PULLUP_CMD		0x08	/* Enable CMD/MOSI pullup */
#define SBSDIO_PULLUP_ALL		0x0f	/* All valid bits */

/* function 1 OCP space */
#define SBSDIO_SB_OFT_ADDR_MASK		0x07FFF	/* sb offset addr is <= 15 bits, 32k */
#define SBSDIO_SB_OFT_ADDR_LIMIT	0x08000
#define SBSDIO_SB_ACCESS_2_4B_FLAG	0x08000	/* with b15, maps to 32-bit SB access */

/* some duplication with sbsdpcmdev.h here */
/* valid bits in SBSDIO_FUNC1_SBADDRxxx regs */
#define SBSDIO_SBADDRLOW_MASK		0x80	/* Valid bits in SBADDRLOW */
#define SBSDIO_SBADDRMID_MASK		0xff	/* Valid bits in SBADDRMID */
#define SBSDIO_SBADDRHIGH_MASK		0xffU	/* Valid bits in SBADDRHIGH */
#define SBSDIO_SBWINDOW_MASK		0xffff8000	/* Address bits from SBADDR regs */

/* direct(mapped) cis space */
#define SBSDIO_CIS_BASE_COMMON		0x1000	/* MAPPED common CIS address */
#define SBSDIO_CIS_SIZE_LIMIT		0x200	/* maximum bytes in one CIS */
#define SBSDIO_OTP_CIS_SIZE_LIMIT       0x078	/* maximum bytes OTP CIS */

#define SBSDIO_CIS_OFT_ADDR_MASK	0x1FFFF	/* cis offset addr is < 17 bits */

#define SBSDIO_CIS_MANFID_TUPLE_LEN	6	/* manfid tuple length, include tuple,
						 * link bytes
						 */

/* indirect cis access (in sprom) */
#define SBSDIO_SPROM_CIS_OFFSET		0x8	/* 8 control bytes first, CIS starts from
						 * 8th byte
						 */

#define SBSDIO_BYTEMODE_DATALEN_MAX	64	/* sdio byte mode: maximum length of one
						 * data command
						 */

#define SBSDIO_CORE_ADDR_MASK		0x1FFFF	/* sdio core function one address mask */

#endif				/* _SBSDIO_H */
