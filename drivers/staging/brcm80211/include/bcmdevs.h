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

/* PCI vendor IDs */
#define	VENDOR_BROADCOM		0x14e4

/* DONGLE VID/PIDs */
#define BCM_DNGL_VID		0x0a5c
#define BCM_DNGL_BDC_PID	0x0bdc

#define	BCM4325_D11DUAL_ID	0x431b
#define	BCM4325_D11G_ID		0x431c
#define	BCM4325_D11A_ID		0x431d
#define BCM4329_D11N_ID		0x432e	/* 4329 802.11n dualband device */
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
#define BCM43236_D11N5G_ID	0x4348	/* 43236 802.11n 5GHz device */

#define BCM43421_D11N_ID	0xA99D	/* 43421 802.11n dualband device */
#define BCM4313_D11N2G_ID	0x4727	/* 4313 802.11n 2.4G device */
#define BCM4330_D11N_ID         0x4360	/* 4330 802.11n dualband device */
#define BCM4330_D11N2G_ID       0x4361	/* 4330 802.11n 2.4G device */
#define BCM4330_D11N5G_ID       0x4362	/* 4330 802.11n 5G device */
#define BCM4336_D11N_ID		0x4343	/* 4336 802.11n 2.4GHz device */
#define BCM6362_D11N_ID		0x435f	/* 6362 802.11n dualband device */
#define BCM4331_D11N_ID		0x4331	/* 4331 802.11n dualband id */
#define BCM4331_D11N2G_ID	0x4332	/* 4331 802.11n 2.4Ghz band id */
#define BCM4331_D11N5G_ID	0x4333	/* 4331 802.11n 5Ghz band id */

/* Chip IDs */
#define BCM4313_CHIP_ID		0x4313	/* 4313 chip id */
#define	BCM4319_CHIP_ID		0x4319	/* 4319 chip id */

#define	BCM43224_CHIP_ID	43224	/* 43224 chipcommon chipid */
#define	BCM43225_CHIP_ID	43225	/* 43225 chipcommon chipid */
#define	BCM43228_CHIP_ID	43228	/* 43228 chipcommon chipid */
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
#define	BCM4716_PKG_ID		8	/* 4716 package id */
#define	BCM4717_PKG_ID		9	/* 4717 package id */
#define	BCM4718_PKG_ID		10	/* 4718 package id */
#define BCM5356_PKG_NONMODE	1	/* 5356 package without nmode suppport */
#define BCM5358U_PKG_ID		8	/* 5358U package id */
#define BCM5358_PKG_ID		9	/* 5358 package id */
#define BCM47186_PKG_ID		10	/* 47186 package id */
#define BCM5357_PKG_ID		11	/* 5357 package id */
#define BCM5356U_PKG_ID		12	/* 5356U package id */
#define HDLSIM5350_PKG_ID	1	/* HDL simulator package id for a 5350 */
#define HDLSIM_PKG_ID		14	/* HDL simulator package id */
#define HWSIM_PKG_ID		15	/* Hardware simulator package id */
#define BCM43224_FAB_CSM	0x8	/* the chip is manufactured by CSM */
#define BCM43224_FAB_SMIC	0xa	/* the chip is manufactured by SMIC */
#define BCM4336_WLBGA_PKG_ID 0x8

/* boardflags */
#define	BFL_RESERVED1		0x00000001
#define	BFL_PACTRL		0x00000002	/* Board has gpio 9 controlling the PA */
#define	BFL_AIRLINEMODE		0x00000004	/* Board implements gpio 13 radio disable indication */
#define	BFL_ADCDIV		0x00000008	/* Board has the rssi ADC divider */
#define	BFL_ENETROBO		0x00000010	/* Board has robo switch or core */
#define	BFL_NOPLLDOWN		0x00000020	/* Not ok to power down the chip pll and oscillator */
#define	BFL_CCKHIPWR		0x00000040	/* Can do high-power CCK transmission */
#define	BFL_ENETADM		0x00000080	/* Board has ADMtek switch */
#define	BFL_ENETVLAN		0x00000100	/* Board has VLAN capability */
#define BFL_NOPCI		0x00000400	/* Board leaves PCI floating */
#define BFL_FEM			0x00000800	/* Board supports the Front End Module */
#define BFL_EXTLNA		0x00001000	/* Board has an external LNA in 2.4GHz band */
#define BFL_HGPA		0x00002000	/* Board has a high gain PA */
#define	BFL_RESERVED2		0x00004000
#define	BFL_ALTIQ		0x00008000	/* Alternate I/Q settings */
#define BFL_NOPA		0x00010000	/* Board has no PA */
#define BFL_RSSIINV		0x00020000	/* Board's RSSI uses positive slope(not TSSI) */
#define BFL_PAREF		0x00040000	/* Board uses the PARef LDO */
#define BFL_3TSWITCH		0x00080000	/* Board uses a triple throw switch shared with BT */
#define BFL_PHASESHIFT		0x00100000	/* Board can support phase shifter */
#define BFL_BUCKBOOST		0x00200000	/* Power topology uses BUCKBOOST */
#define BFL_FEM_BT		0x00400000	/* Board has FEM and switch to share antenna w/ BT */
#define BFL_NOCBUCK		0x00800000	/* Power topology doesn't use CBUCK */
#define BFL_CCKFAVOREVM		0x01000000	/* Favor CCK EVM over spectral mask */
#define BFL_PALDO		0x02000000	/* Power topology uses PALDO */
#define BFL_LNLDO2_2P5		0x04000000	/* Select 2.5V as LNLDO2 output voltage */
#define BFL_FASTPWR		0x08000000
#define BFL_UCPWRCTL_MININDX	0x08000000	/* Enforce min power index to avoid FEM damage */
#define BFL_EXTLNA_5GHz		0x10000000	/* Board has an external LNA in 5GHz band */
#define BFL_TRSW_1by2		0x20000000	/* Board has 2 TRSW's in 1by2 designs */
#define BFL_LO_TRSW_R_5GHz	0x40000000	/* In 5G do not throw TRSW to T for clipLO gain */
#define BFL_ELNA_GAINDEF	0x80000000	/* Backoff InitGain based on elna_2g/5g field
						 * when this flag is set
						 */

/* boardflags2 */
#define BFL2_RXBB_INT_REG_DIS	0x00000001	/* Board has an external rxbb regulator */
#define BFL2_APLL_WAR		0x00000002	/* Flag to implement alternative A-band PLL settings */
#define BFL2_TXPWRCTRL_EN	0x00000004	/* Board permits enabling TX Power Control */
#define BFL2_2X4_DIV		0x00000008	/* Board supports the 2X4 diversity switch */
#define BFL2_5G_PWRGAIN		0x00000010	/* Board supports 5G band power gain */
#define BFL2_PCIEWAR_OVR	0x00000020	/* Board overrides ASPM and Clkreq settings */
#define BFL2_CAESERS_BRD	0x00000040	/* Board is Caesers brd (unused by sw) */
#define BFL2_LEGACY		0x00000080
#define BFL2_SKWRKFEM_BRD	0x00000100	/* 4321mcm93 board uses Skyworks FEM */
#define BFL2_SPUR_WAR		0x00000200	/* Board has a WAR for clock-harmonic spurs */
#define BFL2_GPLL_WAR		0x00000400	/* Flag to narrow G-band PLL loop b/w */
#define BFL2_TRISTATE_LED	0x00000800	/* Tri-state the LED */
#define BFL2_SINGLEANT_CCK	0x00001000	/* Tx CCK pkts on Ant 0 only */
#define BFL2_2G_SPUR_WAR	0x00002000	/* WAR to reduce and avoid clock-harmonic spurs in 2G */
#define BFL2_BPHY_ALL_TXCORES	0x00004000	/* Transmit bphy frames using all tx cores */
#define BFL2_FCC_BANDEDGE_WAR	0x00008000	/* using 40Mhz LPF for 20Mhz bandedge channels */
#define BFL2_GPLL_WAR2	        0x00010000	/* Flag to widen G-band PLL loop b/w */
#define BFL2_IPALVLSHIFT_3P3    0x00020000
#define BFL2_INTERNDET_TXIQCAL  0x00040000	/* Use internal envelope detector for TX IQCAL */
#define BFL2_XTALBUFOUTEN       0x00080000	/* Keep the buffered Xtal output from radio "ON"
						 * Most drivers will turn it off without this flag
						 * to save power.
						 */

/* board specific GPIO assignment, gpio 0-3 are also customer-configurable led */
#define	BOARD_GPIO_RESERVED1	0x010
#define	BOARD_GPIO_RESERVED2	0x020
#define	BOARD_GPIO_RESERVED3	0x080
#define	BOARD_GPIO_RESERVED4	0x100
#define	BOARD_GPIO_PACTRL	0x200	/* bit 9 controls the PA on new 4306 boards */
#define BOARD_GPIO_12		0x1000	/* gpio 12 */
#define BOARD_GPIO_13		0x2000	/* gpio 13 */
#define BOARD_GPIO_RESERVED5	0x0800
#define BOARD_GPIO_RESERVED6	0x2000
#define BOARD_GPIO_RESERVED7	0x4000
#define BOARD_GPIO_RESERVED8	0x8000

#define	PCI_CFG_GPIO_SCS	0x10	/* PCI config space bit 4 for 4306c0 slow clock source */
#define PCI_CFG_GPIO_HWRAD	0x20	/* PCI config space GPIO 13 for hw radio disable */
#define PCI_CFG_GPIO_XTAL	0x40	/* PCI config space GPIO 14 for Xtal power-up */
#define PCI_CFG_GPIO_PLL	0x80	/* PCI config space GPIO 15 for PLL power-down */

/* power control defines */
#define PLL_DELAY		150	/* us pll on delay */
#define FREF_DELAY		200	/* us fref change delay */
#define MIN_SLOW_CLK		32	/* us Slow clock period */
#define	XTAL_ON_DELAY		1000	/* us crystal power-on delay */

/* # of GPIO pins */
#define GPIO_NUMPINS		16

/* Reference board types */
#define	SPI_BOARD		0x0402

#endif				/* _BCMDEVS_H */
