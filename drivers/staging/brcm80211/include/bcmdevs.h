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

#ifndef	_BCMDEVS_H
#define	_BCMDEVS_H

#define	BCM4325_D11DUAL_ID	0x431b
#define	BCM4325_D11G_ID		0x431c
#define	BCM4325_D11A_ID		0x431d

#define BCM4329_D11N2G_ID	0x432f	/* 4329 802.11n 2.4G device */
#define BCM4329_D11N5G_ID	0x4330	/* 4329 802.11n 5G device */
#define BCM4329_D11NDUAL_ID	0x432e

#define BCM4319_D11N_ID		0x4337	/* 4319 802.11n dualband device */
#define BCM4319_D11N2G_ID	0x4338	/* 4319 802.11n 2.4G device */
#define BCM4319_D11N5G_ID	0x4339	/* 4319 802.11n 5G device */

#define BCM43224_D11N_ID	0x4353	/* 43224 802.11n dualband device */

#define BCM43225_D11N2G_ID	0x4357	/* 43225 802.11n 2.4GHz device */

#define BCM43236_D11N_ID	0x4346	/* 43236 802.11n dualband device */
#define BCM43236_D11N2G_ID	0x4347	/* 43236 802.11n 2.4GHz device */

#define BCM4313_D11N2G_ID	0x4727	/* 4313 802.11n 2.4G device */

/* Chip IDs */
#define BCM4313_CHIP_ID		0x4313	/* 4313 chip id */
#define	BCM4319_CHIP_ID		0x4319	/* 4319 chip id */

#define	BCM43224_CHIP_ID	43224	/* 43224 chipcommon chipid */
#define	BCM43225_CHIP_ID	43225	/* 43225 chipcommon chipid */
#define	BCM43421_CHIP_ID	43421	/* 43421 chipcommon chipid */
#define	BCM43235_CHIP_ID	43235	/* 43235 chipcommon chipid */
#define	BCM43236_CHIP_ID	43236	/* 43236 chipcommon chipid */
#define	BCM43238_CHIP_ID	43238	/* 43238 chipcommon chipid */
#define	BCM4329_CHIP_ID		0x4329	/* 4329 chipcommon chipid */
#define	BCM4325_CHIP_ID		0x4325	/* 4325 chipcommon chipid */
#define	BCM4331_CHIP_ID		0x4331	/* 4331 chipcommon chipid */
#define BCM4336_CHIP_ID		0x4336	/* 4336 chipcommon chipid */
#define BCM4330_CHIP_ID		0x4330	/* 4330 chipcommon chipid */
#define BCM6362_CHIP_ID		0x6362	/* 6362 chipcommon chipid */

/* these are router chips */
#define	BCM4716_CHIP_ID		0x4716	/* 4716 chipcommon chipid */
#define	BCM47162_CHIP_ID	47162	/* 47162 chipcommon chipid */
#define	BCM4748_CHIP_ID		0x4748	/* 4716 chipcommon chipid (OTP, RBBU) */
#define	BCM5356_CHIP_ID		0x5356	/* 5356 chipcommon chipid */
#define	BCM5357_CHIP_ID		0x5357	/* 5357 chipcommon chipid */

/* Package IDs */
#define BCM4329_289PIN_PKG_ID	0	/* 4329 289-pin package id */
#define BCM4329_182PIN_PKG_ID	1	/* 4329N 182-pin package id */
#define	BCM4717_PKG_ID		9	/* 4717 package id */
#define	BCM4718_PKG_ID		10	/* 4718 package id */
#define HDLSIM_PKG_ID		14	/* HDL simulator package id */
#define HWSIM_PKG_ID		15	/* Hardware simulator package id */
#define BCM43224_FAB_SMIC	0xa	/* the chip is manufactured by SMIC */

/* boardflags */
#define	BFL_PACTRL		0x00000002	/* Board has gpio 9 controlling the PA */
#define	BFL_NOPLLDOWN		0x00000020	/* Not ok to power down the chip pll and oscillator */
#define BFL_FEM			0x00000800	/* Board supports the Front End Module */
#define BFL_EXTLNA		0x00001000	/* Board has an external LNA in 2.4GHz band */
#define BFL_NOPA		0x00010000	/* Board has no PA */
#define BFL_BUCKBOOST		0x00200000	/* Power topology uses BUCKBOOST */
#define BFL_FEM_BT		0x00400000	/* Board has FEM and switch to share antenna w/ BT */
#define BFL_NOCBUCK		0x00800000	/* Power topology doesn't use CBUCK */
#define BFL_PALDO		0x02000000	/* Power topology uses PALDO */
#define BFL_EXTLNA_5GHz		0x10000000	/* Board has an external LNA in 5GHz band */

/* boardflags2 */
#define BFL2_RXBB_INT_REG_DIS	0x00000001	/* Board has an external rxbb regulator */
#define BFL2_APLL_WAR		0x00000002	/* Flag to implement alternative A-band PLL settings */
#define BFL2_TXPWRCTRL_EN	0x00000004	/* Board permits enabling TX Power Control */
#define BFL2_2X4_DIV		0x00000008	/* Board supports the 2X4 diversity switch */
#define BFL2_5G_PWRGAIN		0x00000010	/* Board supports 5G band power gain */
#define BFL2_PCIEWAR_OVR	0x00000020	/* Board overrides ASPM and Clkreq settings */
#define BFL2_LEGACY		0x00000080
#define BFL2_SKWRKFEM_BRD	0x00000100	/* 4321mcm93 board uses Skyworks FEM */
#define BFL2_SPUR_WAR		0x00000200	/* Board has a WAR for clock-harmonic spurs */
#define BFL2_GPLL_WAR		0x00000400	/* Flag to narrow G-band PLL loop b/w */
#define BFL2_SINGLEANT_CCK	0x00001000	/* Tx CCK pkts on Ant 0 only */
#define BFL2_2G_SPUR_WAR	0x00002000	/* WAR to reduce and avoid clock-harmonic spurs in 2G */
#define BFL2_GPLL_WAR2	        0x00010000	/* Flag to widen G-band PLL loop b/w */
#define BFL2_IPALVLSHIFT_3P3    0x00020000
#define BFL2_INTERNDET_TXIQCAL  0x00040000	/* Use internal envelope detector for TX IQCAL */
#define BFL2_XTALBUFOUTEN       0x00080000	/* Keep the buffered Xtal output from radio "ON"
						 * Most drivers will turn it off without this flag
						 * to save power.
						 */

/* board specific GPIO assignment, gpio 0-3 are also customer-configurable led */
#define	BOARD_GPIO_PACTRL	0x200	/* bit 9 controls the PA on new 4306 boards */
#define BOARD_GPIO_12		0x1000	/* gpio 12 */
#define BOARD_GPIO_13		0x2000	/* gpio 13 */

#define	PCI_CFG_GPIO_SCS	0x10	/* PCI config space bit 4 for 4306c0 slow clock source */
#define PCI_CFG_GPIO_XTAL	0x40	/* PCI config space GPIO 14 for Xtal power-up */
#define PCI_CFG_GPIO_PLL	0x80	/* PCI config space GPIO 15 for PLL power-down */

/* power control defines */
#define PLL_DELAY		150	/* us pll on delay */
#define FREF_DELAY		200	/* us fref change delay */
#define	XTAL_ON_DELAY		1000	/* us crystal power-on delay */

/* Reference board types */
#define	SPI_BOARD		0x0402

#endif				/* _BCMDEVS_H */
