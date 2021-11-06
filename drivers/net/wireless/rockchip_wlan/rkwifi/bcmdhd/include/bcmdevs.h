/*
 * Broadcom device-specific manifest constants.
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef	_BCMDEVS_H
#define	_BCMDEVS_H

/* PCI vendor IDs */
#define	VENDOR_EPIGRAM		0xfeda
#define	VENDOR_BROADCOM		0x14e4
#define	VENDOR_3COM		0x10b7
#define	VENDOR_NETGEAR		0x1385
#define	VENDOR_DIAMOND		0x1092
#define	VENDOR_INTEL		0x8086
#define	VENDOR_DELL		0x1028
#define	VENDOR_HP		0x103c
#define	VENDOR_HP_COMPAQ	0x0e11
#define	VENDOR_APPLE		0x106b
#define VENDOR_SI_IMAGE		0x1095		/* Silicon Image, used by Arasan SDIO Host */
#define VENDOR_BUFFALO		0x1154		/* Buffalo vendor id */
#define VENDOR_TI		0x104c		/* Texas Instruments */
#define VENDOR_RICOH		0x1180		/* Ricoh */
#define VENDOR_JMICRON		0x197b

/* precommit failed when this is removed */
/* BLAZAR_BRANCH_101_10_DHD_001/build/dhd/linux-fc19/brix-brcm */
/* TBD: Revisit later */
#ifdef BCMINTERNAL
#define VENDOR_JINVANI		0x1947		/* Jinvani Systech, Inc. */
#endif

/* PCMCIA vendor IDs */
#define	VENDOR_BROADCOM_PCMCIA	0x02d0

/* SDIO vendor IDs */
#define	VENDOR_BROADCOM_SDIO	0x00BF

/* DONGLE VID/PIDs */
#define BCM_DNGL_VID		0x0a5c
#define BCM_DNGL_BL_PID_4328	0xbd12
#define BCM_DNGL_BL_PID_4332	0xbd18
#define BCM_DNGL_BL_PID_4360	0xbd1d

#define BCM_DNGL_BDC_PID	0x0bdc
#define BCM_DNGL_JTAG_PID	0x4a44

/* Pseudo IDs */
#define FPGA_JTAGM_ID		0x43f0		/* FPGA jtagm device id */
#define BCM_JTAGM_ID		0x43f1		/* BCM jtagm device id */
#define SDIOH_FPGA_ID		0x43f2		/* sdio host fpga */
#define BCM_SDIOH_ID		0x43f3		/* BCM sdio host id */
#define SDIOD_FPGA_ID		0x43f4		/* sdio device fpga */
#define SPIH_FPGA_ID		0x43f5		/* PCI SPI Host Controller FPGA */
#define BCM_SPIH_ID		0x43f6		/* Synopsis SPI Host Controller */
#define MIMO_FPGA_ID		0x43f8		/* FPGA mimo minimacphy device id */
#define BCM_JTAGM2_ID		0x43f9		/* BCM alternate jtagm device id */
#define SDHCI_FPGA_ID		0x43fa		/* Standard SDIO Host Controller FPGA */
#define	BCM4710_DEVICE_ID	0x4710		/* 4710 primary function 0 */
#define	BCM47XX_AUDIO_ID	0x4711		/* 47xx audio codec */
#define	BCM47XX_V90_ID		0x4712		/* 47xx v90 codec */
#define	BCM47XX_ENET_ID		0x4713		/* 47xx enet */
#define	BCM47XX_EXT_ID		0x4714		/* 47xx external i/f */
#define	BCM47XX_GMAC_ID		0x4715		/* 47xx Unimac based GbE */
#define	BCM47XX_USBH_ID		0x4716		/* 47xx usb host */
#define	BCM47XX_USBD_ID		0x4717		/* 47xx usb device */
#define	BCM47XX_IPSEC_ID	0x4718		/* 47xx ipsec */
#define	BCM47XX_ROBO_ID		0x4719		/* 47xx/53xx roboswitch core */
#define	BCM47XX_USB20H_ID	0x471a		/* 47xx usb 2.0 host */
#define	BCM47XX_USB20D_ID	0x471b		/* 47xx usb 2.0 device */
#define	BCM47XX_ATA100_ID	0x471d		/* 47xx parallel ATA */
#define	BCM47XX_SATAXOR_ID	0x471e		/* 47xx serial ATA & XOR DMA */
#define	BCM47XX_GIGETH_ID	0x471f		/* 47xx GbE (5700) */
#define	BCM47XX_USB30H_ID	0x472a		/* 47xx usb 3.0 host */
#define	BCM47XX_USB30D_ID	0x472b		/* 47xx usb 3.0 device */
#define	BCM47XX_USBHUB_ID	0x472c		/* 47xx usb hub */
#define BCM47XX_SMBUS_EMU_ID	0x47fe		/* 47xx emulated SMBus device */
#define	BCM47XX_XOR_EMU_ID	0x47ff		/* 47xx emulated XOR engine */
#define JINVANI_SDIOH_ID	0x4743		/* Jinvani SDIO Gold Host */
#define BCM27XX_SDIOH_ID	0x2702		/* BCM27xx Standard SDIO Host */
#define PCIXX21_FLASHMEDIA_ID	0x803b		/* TI PCI xx21 Standard Host Controller */
#define PCIXX21_SDIOH_ID	0x803c		/* TI PCI xx21 Standard Host Controller */
#define R5C822_SDIOH_ID		0x0822		/* Ricoh Co Ltd R5C822 SD/SDIO/MMC/MS/MSPro Host */
#define JMICRON_SDIOH_ID	0x2381		/* JMicron Standard SDIO Host Controller */

/* PCI Device IDs */
/* DEPRECATED but used */
#define	BCM4318_D11G_ID		0x4318		/* 4318 802.11b/g id */
/* DEPRECATED */

#define BCM4360_D11AC_ID	0x43a0
#define BCM4360_D11AC2G_ID	0x43a1
#define BCM4360_D11AC5G_ID	0x43a2
#define BCM4352_D11AC_ID	0x43b1		/* 4352 802.11ac dualband device */
#define BCM4352_D11AC2G_ID	0x43b2		/* 4352 802.11ac 2.4G device */
#define BCM4352_D11AC5G_ID	0x43b3		/* 4352 802.11ac 5G device */
#define BCM43602_D11AC_ID	0x43ba		/* ac dualband PCI devid SPROM programmed */
#define BCM43602_D11AC2G_ID	0x43bb		/* 43602 802.11ac 2.4G device */
#define BCM43602_D11AC5G_ID	0x43bc		/* 43602 802.11ac 5G device */

#define BCM43012_D11N_ID	0xA804		/* 43012 802.11n dualband device */
#define BCM43012_D11N2G_ID	0xA805		/* 43012 802.11n 2.4G device */
#define BCM43012_D11N5G_ID	0xA806		/* 43012 802.11n 5G device */
#define BCM43014_D11N_ID	0x4495		/* 43014 802.11n dualband device */
#define BCM43014_D11N2G_ID	0x4496		/* 43014 802.11n 2.4G device */
#define BCM43014_D11N5G_ID	0x4497		/* 43014 802.11n 5G device */
#define BCM43013_D11N_ID	0x4498		/* 43013 802.11n dualband device */
#define BCM43013_D11N2G_ID	0x4499		/* 43013 802.11n 2.4G device */
#define BCM43013_D11N5G_ID	0x449a		/* 43013 802.11n 5G device */

/* PCI Subsystem ID */
#define BCM4376_D11AX_ID	0x4445		/* 4376 802.11ax dualband device */
#define BCM4376_D11AX2G_ID	0x4436		/* 4376 802.11ax 2.4G device */
#define BCM4376_D11AX5G_ID	0x4437		/* 4376 802.11ax 5G device */

#define BCM4378_D11AX_ID	0x4425		/* 4378 802.11ax dualband device */
#define BCM4378_D11AX2G_ID	0x4426		/* 4378 802.11ax 2.4G device */
#define BCM4378_D11AX5G_ID	0x4427		/* 4378 802.11ax 5G device */

#define BCM4387_D11AX_ID	0x4433		/* 4387 802.11ax dualband device */
#define BCM4388_D11AX_ID	0x4434		/* 4388 802.11ax dualband device */
#define BCM4385_D11AX_ID	0x4442		/* 4385 802.11ax dualband device */
#define BCM4389_D11AX_ID	0x4441		/* 4389 802.11ax dualband device */
#define BCM4397_D11AX_ID	0x4443		/* 4397 802.11ax dualband device */

#define BCM4362_D11AX_ID	0x4490		/* 4362 802.11ax dualband device */
#define BCM4362_D11AX2G_ID	0x4491		/* 4362 802.11ax 2.4G device */
#define BCM4362_D11AX5G_ID	0x4492		/* 4362 802.11ax 5G device */
#define BCM43751_D11AX_ID	0x449a		/* 43751 802.11ac dualband device */
#define BCM43751_D11AX2G_ID	0x449b		/* 43751 802.11ac 2.4G device */
#define BCM43751_D11AX5G_ID	0x449c		/* 43751 802.11ac 5G device */
#define BCM43752_D11AX_ID	0x449d		/* 43752 802.11ax dualband device */
#define BCM43752_D11AX2G_ID	0x449e		/* 43752 802.11ax 2.4G device */
#define BCM43752_D11AX5G_ID	0x449f		/* 43752 802.11ax 5G device */

/* TBD change below values */
#define BCM4369_D11AX_ID	0x4470		/* 4369 802.11ax dualband device */
#define BCM4369_D11AX2G_ID	0x4471		/* 4369 802.11ax 2.4G device */
#define BCM4369_D11AX5G_ID	0x4472		/* 4369 802.11ax 5G device */

#define BCM4375_D11AX_ID	0x4475		/* 4375 802.11ax dualband device */
#define BCM4375_D11AX2G_ID	0x4476		/* 4375 802.11ax 2.4G device */
#define BCM4375_D11AX5G_ID	0x4477		/* 4375 802.11ax 5G device */

#define BCM4377_D11AX_ID	0x4480		/* 4377 802.11ax dualband device */
#define BCM4377_D11AX2G_ID	0x4481		/* 4377 802.11ax 2.4G device */
#define BCM4377_D11AX5G_ID	0x4482		/* 4377 802.11ax 5G device */

/* 4377 802.11ax dualband device with multifunction */
#define BCM4377_M_D11AX_ID	0x4488

/* Chip IDs */

#define BCM43143_CHIP_ID	43143		/* 43143 chipcommon chipid */
#define	BCM43242_CHIP_ID	43242		/* 43242 chipcommon chipid */
#define	BCM43460_CHIP_ID	43460		/* 4360  chipcommon chipid (OTP, RBBU) */
#define BCM4360_CHIP_ID		0x4360          /* 4360 chipcommon chipid */
#define BCM43362_CHIP_ID	43362		/* 43362 chipcommon chipid */
#define BCM4330_CHIP_ID		0x4330		/* 4330 chipcommon chipid */
#define	BCM4324_CHIP_ID		0x4324		/* 4324 chipcommon chipid */
#define BCM4334_CHIP_ID		0x4334		/* 4334 chipcommon chipid */
#define BCM4335_CHIP_ID		0x4335		/* 4335 chipcommon chipid */
#define BCM4339_CHIP_ID		0x4339		/* 4339 chipcommon chipid */
#define BCM4352_CHIP_ID		0x4352          /* 4352 chipcommon chipid */
#define BCM43526_CHIP_ID	0xAA06
#define BCM43340_CHIP_ID	43340		/* 43340 chipcommon chipid */
#define BCM43341_CHIP_ID	43341		/* 43341 chipcommon chipid */
#define BCM43562_CHIP_ID	0xAA2A          /* 43562 chipcommon chipid */
#define	BCM43012_CHIP_ID	0xA804          /* 43012 chipcommon chipid */
#define	BCM43013_CHIP_ID	0xA805          /* 43013 chipcommon chipid */
#define	BCM43014_CHIP_ID	0xA806          /* 43014 chipcommon chipid */
#define	BCM4369_CHIP_ID		0x4369          /* 4369 chipcommon chipid */
#define BCM4375_CHIP_ID		0x4375          /* 4375 chipcommon chipid */
#define BCM4376_CHIP_ID		0x4376          /* 4376 chipcommon chipid */
#define BCM4354_CHIP_ID		0x4354          /* 4354 chipcommon chipid */
#define BCM4356_CHIP_ID		0x4356          /* 4356 chipcommon chipid */
#define BCM4371_CHIP_ID		0x4371          /* 4371 chipcommon chipid */
#define BCM43569_CHIP_ID	0xAA31          /* 43569 chipcommon chipid */

#define BCM4345_CHIP_ID		0x4345		/* 4345 chipcommon chipid */
#define BCM43454_CHIP_ID	43454		/* 43454 chipcommon chipid */
#define BCM43430_CHIP_ID	43430		/* 43430 chipcommon chipid */
#define BCM4359_CHIP_ID		0x4359		/* 4359 chipcommon chipid */
#define BCM4362_CHIP_ID		0x4362          /* 4362 chipcommon chipid */
#define BCM43751_CHIP_ID	0xAAE7          /* 43751 chipcommon chipid */
#define BCM43752_CHIP_ID	0xAAE8          /* 43752 chipcommon chipid */
#define BCM4369_CHIP_ID		0x4369          /* 4369 chipcommon chipid */
#define BCM4377_CHIP_ID		0x4377          /* 4377 chipcommon chipid */
#define BCM4378_CHIP_ID		0x4378          /* 4378 chipcommon chipid */
#define BCM4385_CHIP_ID		0x4385          /* 4385 chipcommon chipid */
#define BCM4387_CHIP_ID		0x4387          /* 4387 chipcommon chipid */
#define BCM4388_CHIP_ID		0x4388          /* 4388 chipcommon chipid */
#define BCM4389_CHIP_ID		0x4389          /* 4389 chipcommon chipid */
#define BCM4397_CHIP_ID		0x4397          /* 4397 chipcommon chipid */

#define BCM4362_CHIP(chipid)	(CHIPID(chipid) == BCM4362_CHIP_ID)
#define BCM4362_CHIP_GRPID	BCM4362_CHIP_ID

#define BCM4369_CHIP(chipid)	((CHIPID(chipid) == BCM4369_CHIP_ID) || \
				(CHIPID(chipid) == BCM4377_CHIP_ID) || \
				(CHIPID(chipid) == BCM4375_CHIP_ID))
#define BCM4369_CHIP_GRPID		BCM4369_CHIP_ID: \
					case BCM4377_CHIP_ID: \
					case BCM4375_CHIP_ID

#define BCM4385_CHIP(chipid)	(CHIPID(chipid) == BCM4385_CHIP_ID)
#define BCM4385_CHIP_GRPID	BCM4385_CHIP_ID

#define BCM4378_CHIP(chipid)    (CHIPID(chipid) == BCM4378_CHIP_ID)
#define BCM4378_CHIP_GRPID	BCM4378_CHIP_ID

#define BCM4376_CHIP_GRPID	BCM4376_CHIP_ID
#define BCM4376_CHIP(chipid)    (CHIPID(chipid) == BCM4376_CHIP_ID)

#define BCM4387_CHIP(chipid)    (CHIPID(chipid) == BCM4387_CHIP_ID)
#define BCM4387_CHIP_GRPID	BCM4387_CHIP_ID

#define BCM4388_CHIP(chipid)	(CHIPID(chipid) == BCM4388_CHIP_ID)
#define BCM4388_CHIP_GRPID	BCM4388_CHIP_ID

#define BCM4389_CHIP(chipid)	(CHIPID(chipid) == BCM4389_CHIP_ID)
#define BCM4389_CHIP_GRPID	BCM4389_CHIP_ID

#define BCM4397_CHIP(chipid)	(CHIPID(chipid) == BCM4397_CHIP_ID)
#define BCM4397_CHIP_GRPID	BCM4397_CHIP_ID

#define BCM43602_CHIP_ID	0xaa52		/* 43602 chipcommon chipid */
#define BCM43462_CHIP_ID	0xa9c6		/* 43462 chipcommon chipid */
#define BCM43522_CHIP_ID	0xaa02		/* 43522 chipcommon chipid */
#define BCM43602_CHIP(chipid)	((CHIPID(chipid) == BCM43602_CHIP_ID) || \
				(CHIPID(chipid) == BCM43462_CHIP_ID) || \
				(CHIPID(chipid) == BCM43522_CHIP_ID)) /* 43602 variations */
#define BCM43012_CHIP(chipid)	((CHIPID(chipid) == BCM43012_CHIP_ID) || \
				(CHIPID(chipid) == BCM43013_CHIP_ID) || \
				(CHIPID(chipid) == BCM43014_CHIP_ID))
#define CASE_BCM43602_CHIP		case BCM43602_CHIP_ID: /* fallthrough */ \
				case BCM43462_CHIP_ID: /* fallthrough */ \
				case BCM43522_CHIP_ID

/* Package IDs */

#define HDLSIM_PKG_ID		14		/* HDL simulator package id */
#define HWSIM_PKG_ID		15		/* Hardware simulator package id */

#define PCIXX21_FLASHMEDIA0_ID	0x8033		/* TI PCI xx21 Standard Host Controller */
#define PCIXX21_SDIOH0_ID	0x8034		/* TI PCI xx21 Standard Host Controller */

#define BCM43602_12x12_PKG_ID	(0x1)	/* 12x12 pins package, used for e.g. router designs */

/* 43012 package ID's
    http://confluence.broadcom.com/display/WLAN/BCM43012+Variants%2Cpackage%2Cballmap%2Cfloorplan#
    BCM43012Variants,package,ballmap,floorplan-PackageOptions
*/
#define BCM943012_WLCSPOLY_PKG_ID	0x0	/* WLCSP Oly package */
#define BCM943012_FCBGA_PKG_ID		0x3	/* FCBGA debug package */
#define BCM943012_WLCSPWE_PKG_ID	0x1	/* WLCSP WE package */
#define BCM943012_FCBGAWE_PKG_ID	0x5	/* FCBGA WE package */
#define BCM943012_WLBGA_PKG_ID		0x2	/* WLBGA package */

/* boardflags */
#define	BFL_BTC2WIRE		0x00000001  /* old 2wire Bluetooth coexistence, OBSOLETE */
#define BFL_BTCOEX      0x00000001      /* Board supports BTCOEX */
#define	BFL_PACTRL		0x00000002  /* Board has gpio 9 controlling the PA */
#define BFL_AIRLINEMODE	0x00000004  /* Board implements gpio radio disable indication */
#define	BFL_ADCDIV		0x00000008  /* Board has the rssi ADC divider */
#define BFL_DIS_256QAM		0x00000008
					/* for 4360, this bit is to disable 256QAM support */
#define	BFL_ENETROBO		0x00000010  /* Board has robo switch or core */
#define	BFL_TSSIAVG		0x00000010  /* TSSI averaging for ACPHY chips */
#define	BFL_NOPLLDOWN		0x00000020  /* Not ok to power down the chip pll and oscillator */
#define	BFL_CCKHIPWR		0x00000040  /* Can do high-power CCK transmission */
#define	BFL_ENETADM		0x00000080  /* Board has ADMtek switch */
#define	BFL_ENETVLAN		0x00000100  /* Board has VLAN capability */
#define	BFL_LTECOEX		0x00000200  /* LTE Coex enabled */
#define BFL_NOPCI		0x00000400  /* Board leaves PCI floating */
#define BFL_FEM			0x00000800  /* Board supports the Front End Module */
#define BFL_EXTLNA		0x00001000  /* Board has an external LNA in 2.4GHz band */
#define BFL_HGPA		0x00002000  /* Board has a high gain PA */
#define	BFL_BTC2WIRE_ALTGPIO	0x00004000  /* Board's BTC 2wire is in the alternate gpios */
#define	BFL_ALTIQ		0x00008000  /* Alternate I/Q settings */
#define BFL_NOPA		0x00010000  /* Board has no PA */
#define BFL_RSSIINV		0x00020000  /* Board's RSSI uses positive slope(not TSSI) */
#define BFL_PAREF		0x00040000  /* Board uses the PARef LDO */
#define BFL_3TSWITCH		0x00080000  /* Board uses a triple throw switch shared with BT */
#define BFL_PHASESHIFT		0x00100000  /* Board can support phase shifter */
#define BFL_BUCKBOOST		0x00200000  /* Power topology uses BUCKBOOST */
#define BFL_FEM_BT		0x00400000  /* Board has FEM and switch to share antenna w/ BT */
#define BFL_NOCBUCK		0x00800000  /* Power topology doesn't use CBUCK */
#define BFL_CCKFAVOREVM		0x01000000  /* Favor CCK EVM over spectral mask */
#define BFL_PALDO		0x02000000  /* Power topology uses PALDO */
#define BFL_LNLDO2_2P5		0x04000000  /* Select 2.5V as LNLDO2 output voltage */
/* BFL_FASTPWR and BFL_UCPWRCTL_MININDX are non-overlaping features and use the same bit */
#define BFL_FASTPWR		0x08000000  /* Fast switch/antenna powerup (no POR WAR) */
#define BFL_UCPWRCTL_MININDX	0x08000000  /* Enforce min power index to avoid FEM damage */
#define BFL_EXTLNA_5GHz		0x10000000  /* Board has an external LNA in 5GHz band */
#define BFL_TRSW_1by2		0x20000000  /* Board has 2 TRSW's in 1by2 designs */
#define BFL_GAINBOOSTA01        0x20000000  /* 5g Gainboost for core0 and core1 */
#define BFL_LO_TRSW_R_5GHz	0x40000000  /* In 5G do not throw TRSW to T for clipLO gain */
#define BFL_ELNA_GAINDEF	0x80000000  /* Backoff InitGain based on elna_2g/5g field
					     * when this flag is set
					     */
#define BFL_EXTLNA_TX	0x20000000	/* Temp boardflag to indicate to */

/* boardflags2 */
#define BFL2_RXBB_INT_REG_DIS	0x00000001  /* Board has an external rxbb regulator */
#define BFL2_APLL_WAR		0x00000002  /* Flag to implement alternative A-band PLL settings */
#define BFL2_TXPWRCTRL_EN	0x00000004  /* Board permits enabling TX Power Control */
#define BFL2_2X4_DIV		0x00000008  /* Board supports the 2X4 diversity switch */
#define BFL2_5G_PWRGAIN		0x00000010  /* Board supports 5G band power gain */
#define BFL2_PCIEWAR_OVR	0x00000020  /* Board overrides ASPM and Clkreq settings */
#define BFL2_CAESERS_BRD	0x00000040  /* Board is Caesers brd (unused by sw) */
#define BFL2_WLCX_ATLAS		0x00000040  /* Board flag to initialize ECI for WLCX on FL-ATLAS */
#define BFL2_BTC3WIRE		0x00000080  /* Board support legacy 3 wire or 4 wire */
#define BFL2_BTCLEGACY          0x00000080  /* Board support legacy 3/4 wire, to replace
					     * BFL2_BTC3WIRE
					     */
#define BFL2_SKWRKFEM_BRD	0x00000100  /* 4321mcm93 board uses Skyworks FEM */
#define BFL2_SPUR_WAR		0x00000200  /* Board has a WAR for clock-harmonic spurs */
#define BFL2_GPLL_WAR		0x00000400  /* Flag to narrow G-band PLL loop b/w */
#define BFL2_TRISTATE_LED	0x00000800  /* Tri-state the LED */
#define BFL2_SINGLEANT_CCK	0x00001000  /* Tx CCK pkts on Ant 0 only */
#define BFL2_2G_SPUR_WAR	0x00002000  /* WAR to reduce and avoid clock-harmonic spurs in 2G */
#define BFL2_BPHY_ALL_TXCORES	0x00004000  /* Transmit bphy frames using all tx cores */
#define BFL2_FCC_BANDEDGE_WAR	0x00008000  /* Activates WAR to improve FCC bandedge performance */
#define BFL2_DAC_SPUR_IMPROVEMENT 0x00008000       /* Reducing DAC Spurs */
#define BFL2_GPLL_WAR2	        0x00010000  /* Flag to widen G-band PLL loop b/w */
#define BFL2_REDUCED_PA_TURNONTIME 0x00010000  /* Flag to reduce PA turn on Time */
#define BFL2_IPALVLSHIFT_3P3    0x00020000  /* Flag to Activate the PR 74115 PA Level Shift
					     * Workaround where the gpaio pin is connected to 3.3V
					     */
#define BFL2_INTERNDET_TXIQCAL  0x00040000  /* Use internal envelope detector for TX IQCAL */
#define BFL2_XTALBUFOUTEN       0x00080000  /* Keep the buffered Xtal output from radio on */
				/* Most drivers will turn it off without this flag */
				/* to save power. */

#define BFL2_ANAPACTRL_2G	0x00100000  /* 2G ext PAs are controlled by analog PA ctrl lines */
#define BFL2_ANAPACTRL_5G	0x00200000  /* 5G ext PAs are controlled by analog PA ctrl lines */
#define BFL2_ELNACTRL_TRSW_2G	0x00400000  /* AZW4329: 2G gmode_elna_gain controls TR Switch */
#define BFL2_BT_SHARE_ANT0	0x00800000  /* share core0 antenna with BT */
#define BFL2_TEMPSENSE_HIGHER	0x01000000  /* The tempsense threshold can sustain higher value
					     * than programmed. The exact delta is decided by
					     * driver per chip/boardtype. This can be used
					     * when tempsense qualification happens after shipment
					     */
#define BFL2_BTC3WIREONLY       0x02000000  /* standard 3 wire btc only.  4 wire not supported */
#define BFL2_PWR_NOMINAL	0x04000000  /* 0: power reduction on, 1: no power reduction */
#define BFL2_EXTLNA_PWRSAVE	0x08000000  /* boardflag to enable ucode to apply power save */
						/* ucode control of eLNA during Tx */
#define BFL2_SDR_EN		0x20000000  /* SDR enabled or disabled */
#define BFL2_DYNAMIC_VMID	0x10000000  /* boardflag to enable dynamic Vmid idle TSSI CAL */
#define BFL2_LNA1BYPFORTR2G	0x40000000  /* acphy, enable lna1 bypass for clip gain, 2g */
#define BFL2_LNA1BYPFORTR5G	0x80000000  /* acphy, enable lna1 bypass for clip gain, 5g */

/* SROM 11 - 11ac boardflag definitions */
#define BFL_SROM11_BTCOEX  0x00000001  /* Board supports BTCOEX */
#define BFL_SROM11_WLAN_BT_SH_XTL  0x00000002  /* bluetooth and wlan share same crystal */
#define BFL_SROM11_EXTLNA	0x00001000  /* Board has an external LNA in 2.4GHz band */
#define BFL_SROM11_EPA_TURNON_TIME     0x00018000  /* 2 bits for different PA turn on times */
#define BFL_SROM11_EPA_TURNON_TIME_SHIFT  15
#define BFL_SROM11_PRECAL_TX_IDX	0x00040000  /* Dedicated TX IQLOCAL IDX values */
				/* per subband, as derived from 43602A1 MCH5 */
#define BFL_SROM11_EXTLNA_5GHz	0x10000000  /* Board has an external LNA in 5GHz band */
#define BFL_SROM11_GAINBOOSTA01	0x20000000  /* 5g Gainboost for core0 and core1 */
#define BFL2_SROM11_APLL_WAR	0x00000002  /* Flag to implement alternative A-band PLL settings */
#define BFL2_SROM11_ANAPACTRL_2G  0x00100000  /* 2G ext PAs are ctrl-ed by analog PA ctrl lines */
#define BFL2_SROM11_ANAPACTRL_5G  0x00200000  /* 5G ext PAs are ctrl-ed by analog PA ctrl lines */
#define BFL2_SROM11_SINGLEANT_CCK	0x00001000  /* Tx CCK pkts on Ant 0 only */
#define BFL2_SROM11_EPA_ON_DURING_TXIQLOCAL    0x00020000  /* Keep ext. PA's on in TX IQLO CAL */

/* boardflags3 */
#define BFL3_FEMCTRL_SUB	  0x00000007  /* acphy, subrevs of femctrl on top of srom_femctrl */
#define BFL3_RCAL_WAR		  0x00000008  /* acphy, rcal war active on this board (4335a0) */
#define BFL3_TXGAINTBLID	  0x00000070  /* acphy, txgain table id */
#define BFL3_TXGAINTBLID_SHIFT	  0x4         /* acphy, txgain table id shift bit */
#define BFL3_TSSI_DIV_WAR	  0x00000080  /* acphy, Seperate paparam for 20/40/80 */
#define BFL3_TSSI_DIV_WAR_SHIFT	  0x7         /* acphy, Seperate paparam for 20/40/80 shift bit */
#define BFL3_FEMTBL_FROM_NVRAM    0x00000100  /* acphy, femctrl table is read from nvram */
#define BFL3_FEMTBL_FROM_NVRAM_SHIFT 0x8         /* acphy, femctrl table is read from nvram */
#define BFL3_AGC_CFG_2G           0x00000200  /* acphy, gain control configuration for 2G */
#define BFL3_AGC_CFG_5G           0x00000400  /* acphy, gain control configuration for 5G */
#define BFL3_PPR_BIT_EXT          0x00000800  /* acphy, bit position for 1bit extension for ppr */
#define BFL3_PPR_BIT_EXT_SHIFT    11          /* acphy, bit shift for 1bit extension for ppr */
#define BFL3_BBPLL_SPR_MODE_DIS	  0x00001000  /* acphy, disables bbpll spur modes */
#define BFL3_RCAL_OTP_VAL_EN      0x00002000  /* acphy, to read rcal_trim value from otp */
#define BFL3_2GTXGAINTBL_BLANK	  0x00004000  /* acphy, blank the first X ticks of 2g gaintbl */
#define BFL3_2GTXGAINTBL_BLANK_SHIFT 14       /* acphy, blank the first X ticks of 2g gaintbl */
#define BFL3_5GTXGAINTBL_BLANK	  0x00008000  /* acphy, blank the first X ticks of 5g gaintbl */
#define BFL3_5GTXGAINTBL_BLANK_SHIFT 15       /* acphy, blank the first X ticks of 5g gaintbl */
#define BFL3_PHASETRACK_MAX_ALPHABETA	  0x00010000  /* acphy, to max out alpha,beta to 511 */
#define BFL3_PHASETRACK_MAX_ALPHABETA_SHIFT 16       /* acphy, to max out alpha,beta to 511 */
/* acphy, to use backed off gaintbl for lte-coex */
#define BFL3_LTECOEX_GAINTBL_EN           0x00060000
/* acphy, to use backed off gaintbl for lte-coex */
#define BFL3_LTECOEX_GAINTBL_EN_SHIFT 17
#define BFL3_5G_SPUR_WAR          0x00080000  /* acphy, enable spur WAR in 5G band */

/* acphy: lpmode2g and lpmode_5g related boardflags */
#define BFL3_ACPHY_LPMODE_2G	  0x00300000  /* bits 20:21 for lpmode_2g choice */
#define BFL3_ACPHY_LPMODE_2G_SHIFT	  20

#define BFL3_ACPHY_LPMODE_5G	  0x00C00000  /* bits 22:23 for lpmode_5g choice */
#define BFL3_ACPHY_LPMODE_5G_SHIFT	  22

#define BFL3_1X1_RSDB_ANT	  0x01000000  /* to find if 2-ant RSDB board or 1-ant RSDB board */
#define BFL3_1X1_RSDB_ANT_SHIFT           24

#define BFL3_EXT_LPO_ISCLOCK      0x02000000  /* External LPO is clock, not x-tal */
#define BFL3_FORCE_INT_LPO_SEL    0x04000000  /* Force internal lpo */
#define BFL3_FORCE_EXT_LPO_SEL    0x08000000  /* Force external lpo */

#define BFL3_EN_BRCM_IMPBF        0x10000000  /* acphy, Allow BRCM Implicit TxBF */

#define BFL3_PADCAL_OTP_VAL_EN    0x20000000  /* acphy, to read pad cal values from otp */

#define BFL3_AVVMID_FROM_NVRAM    0x40000000  /* Read Av Vmid from NVRAM  */
#define BFL3_VLIN_EN_FROM_NVRAM    0x80000000  /* Read Vlin En from NVRAM  */

#define BFL3_AVVMID_FROM_NVRAM_SHIFT   30   /* Read Av Vmid from NVRAM  */
#define BFL3_VLIN_EN_FROM_NVRAM_SHIFT   31   /* Enable Vlin  from NVRAM  */

/* boardflags4 for SROM12/SROM13 */

/* To distinguigh between normal and 4dB pad board */
#define BFL4_SROM12_4dBPAD			(1u << 0)

/* Determine power detector type for 2G */
#define BFL4_SROM12_2G_DETTYPE			(1u << 1u)

/* Determine power detector type for 5G */
#define BFL4_SROM12_5G_DETTYPE			(1u << 2u)

/* using pa_dettype from SROM13 flags */
#define BFL4_SROM13_DETTYPE_EN			(1u << 3u)

/* using cck spur reduction setting */
#define BFL4_SROM13_CCK_SPUR_EN			(1u << 4u)

/* using 1.5V cbuck board */
#define BFL4_SROM13_1P5V_CBUCK			(1u << 7u)

/* Enable/disable bit for sw chain mask */
#define BFL4_SROM13_EN_SW_TXRXCHAIN_MASK	(1u << 8u)

#define BFL4_BTCOEX_OVER_SECI	0x00000400u	/* Enable btcoex over gci seci */

/* RFFE rFEM 5G and 2G present bit */
#define BFL4_FEM_RFFE		(1u << 21u)

/* papd params */
#define PAPD_TX_ATTN_2G 0xFF
#define PAPD_TX_ATTN_5G 0xFF00
#define PAPD_TX_ATTN_5G_SHIFT 8
#define PAPD_RX_ATTN_2G 0xFF
#define PAPD_RX_ATTN_5G 0xFF00
#define PAPD_RX_ATTN_5G_SHIFT 8
#define PAPD_CAL_IDX_2G 0xFF
#define PAPD_CAL_IDX_5G 0xFF00
#define PAPD_CAL_IDX_5G_SHIFT 8
#define PAPD_BBMULT_2G 0xFF
#define PAPD_BBMULT_5G 0xFF00
#define PAPD_BBMULT_5G_SHIFT 8
#define TIA_GAIN_MODE_2G 0xFF
#define TIA_GAIN_MODE_5G 0xFF00
#define TIA_GAIN_MODE_5G_SHIFT 8
#define PAPD_EPS_OFFSET_2G 0xFFFF
#define PAPD_EPS_OFFSET_5G 0xFFFF0000
#define PAPD_EPS_OFFSET_5G_SHIFT 16
#define PAPD_CALREF_DB_2G 0xFF
#define PAPD_CALREF_DB_5G 0xFF00
#define PAPD_CALREF_DB_5G_SHIFT 8

/* board specific GPIO assignment, gpio 0-3 are also customer-configurable led */
#define	BOARD_GPIO_BTC3W_IN	0x850	/* bit 4 is RF_ACTIVE, bit 6 is STATUS, bit 11 is PRI */
#define	BOARD_GPIO_BTC3W_OUT	0x020	/* bit 5 is TX_CONF */
#define	BOARD_GPIO_BTCMOD_IN	0x010	/* bit 4 is the alternate BT Coexistence Input */
#define	BOARD_GPIO_BTCMOD_OUT	0x020	/* bit 5 is the alternate BT Coexistence Out */
#define	BOARD_GPIO_BTC_IN	0x080	/* bit 7 is BT Coexistence Input */
#define	BOARD_GPIO_BTC_OUT	0x100	/* bit 8 is BT Coexistence Out */
#define	BOARD_GPIO_PACTRL	0x200	/* bit 9 controls the PA on new 4306 boards */
#define BOARD_GPIO_12		0x1000	/* gpio 12 */
#define BOARD_GPIO_13		0x2000	/* gpio 13 */
#define BOARD_GPIO_BTC4_IN	0x0800	/* gpio 11, coex4, in */
#define BOARD_GPIO_BTC4_BT	0x2000	/* gpio 12, coex4, bt active */
#define BOARD_GPIO_BTC4_STAT	0x4000	/* gpio 14, coex4, status */
#define BOARD_GPIO_BTC4_WLAN	0x8000	/* gpio 15, coex4, wlan active */
#define	BOARD_GPIO_1_WLAN_PWR	0x02	/* throttle WLAN power on X21 board */
#define	BOARD_GPIO_2_WLAN_PWR	0x04	/* throttle WLAN power on X29C board */
#define	BOARD_GPIO_3_WLAN_PWR	0x08	/* throttle WLAN power on X28 board */
#define	BOARD_GPIO_4_WLAN_PWR	0x10	/* throttle WLAN power on X19 board */
#define	BOARD_GPIO_13_WLAN_PWR	0x2000	/* throttle WLAN power on X14 board */

#define GPIO_BTC4W_OUT_4312  0x010  /* bit 4 is BT_IODISABLE */

#define	PCI_CFG_GPIO_SCS	0x10	/* PCI config space bit 4 for 4306c0 slow clock source */
#define PCI_CFG_GPIO_HWRAD	0x20	/* PCI config space GPIO 13 for hw radio disable */
#define PCI_CFG_GPIO_XTAL	0x40	/* PCI config space GPIO 14 for Xtal power-up */
#define PCI_CFG_GPIO_PLL	0x80	/* PCI config space GPIO 15 for PLL power-down */

/* need to be moved to a chip specific header file */
/* power control defines */
#define PLL_DELAY		150		/* us pll on delay */
#define FREF_DELAY		200		/* us fref change delay */
#define MIN_SLOW_CLK		32		/* us Slow clock period */
#define	XTAL_ON_DELAY		1000		/* us crystal power-on delay */

/* Board IDs */

/* Reference Board Types */
#define	BU4710_BOARD		0x0400
#define	VSIM4710_BOARD		0x0401
#define	QT4710_BOARD		0x0402

#define	BCM94710D_BOARD		0x041a
#define	BCM94710R1_BOARD	0x041b
#define	BCM94710R4_BOARD	0x041c
#define	BCM94710AP_BOARD	0x041d

#define	BU2050_BOARD		0x041f

/* BCM4318 boards */
#define BU4318_BOARD		0x0447
#define CB4318_BOARD		0x0448
#define MPG4318_BOARD		0x0449
#define MP4318_BOARD		0x044a
#define SD4318_BOARD		0x044b
#define	BCM94318MPGH_BOARD	0x0463

/* 4321 boards */
#define BU4321_BOARD		0x046b
#define BU4321E_BOARD		0x047c
#define MP4321_BOARD		0x046c
#define CB2_4321_BOARD		0x046d
#define CB2_4321_AG_BOARD	0x0066
#define MC4321_BOARD		0x046e

/* 4360 Boards */
#define BCM94360X52C            0X0117
#define BCM94360X52D            0X0137
#define BCM94360X29C            0X0112
#define BCM94360X29CP2          0X0134
#define BCM94360X29CP3          0X013B
#define BCM94360X51             0x0111
#define BCM94360X51P2           0x0129
#define BCM94360X51P3           0x0142
#define BCM94360X51A            0x0135
#define BCM94360X51B            0x0136
#define BCM94360CS              0x061B
#define BCM94360J28_D11AC2G     0x0c00
#define BCM94360J28_D11AC5G     0x0c01
#define BCM94360USBH5_D11AC5G   0x06aa
#define BCM94360MCM5            0x06d8

/* need to update si_fixup_vid_overrides() for additional platforms */

/* 43012 wlbga Board */
#define BCM943012WLREF_SSID	0x07d7

/* 43012 fcbga Board */
#define BCM943012FCREF_SSID	0x07d4

/* 43602 Boards, unclear yet what boards will be created. */
#define BCM943602RSVD1_SSID	0x06a5
#define BCM943602RSVD2_SSID	0x06a6
#define BCM943602X87            0X0133
#define BCM943602X87P2          0X0152
#define BCM943602X87P3          0X0153 /* need to update si_fixup_vid_overrides() */
#define BCM943602X238           0X0132
#define BCM943602X238D          0X014A
#define BCM943602X238DP2        0X0155 /* J117 */
#define BCM943602X238DP3        0X0156 /* J94 */
#define BCM943602X100           0x0761 /* Dev only */
#define BCM943602X100GS         0x0157 /* Woody */
#define BCM943602X100P2         0x015A /* Buzz, Zurg */

/* 4375B0 WLCSP SEMCO Board */
#define BCM94375B0_WLCSP_SSID	0x086b

/* # of GPIO pins */
#define GPIO_NUMPINS		32

/* chip RAM specifications */
#define RDL_RAM_SIZE_4360  0xA0000
#define RDL_RAM_BASE_4360  0x60000000

/* generic defs for nvram "muxenab" bits
* Note: these differ for 4335a0. refer bcmchipc.h for specific mux options.
*/
#define MUXENAB_UART		0x00000001
#define MUXENAB_GPIO		0x00000002
#define MUXENAB_ERCX		0x00000004	/* External Radio BT coex */
#define MUXENAB_JTAG		0x00000008
#define MUXENAB_HOST_WAKE	0x00000010	/* configure GPIO for SDIO host_wake */
#define MUXENAB_I2S_EN		0x00000020
#define MUXENAB_I2S_MASTER	0x00000040
#define MUXENAB_I2S_FULL	0x00000080
#define MUXENAB_SFLASH		0x00000100
#define MUXENAB_RFSWCTRL0	0x00000200
#define MUXENAB_RFSWCTRL1	0x00000400
#define MUXENAB_RFSWCTRL2	0x00000800
#define MUXENAB_SECI		0x00001000
#define MUXENAB_BT_LEGACY	0x00002000
#define MUXENAB_HOST_WAKE1	0x00004000	/* configure alternative GPIO for SDIO host_wake */

/* Boot flags */
#define FLASH_KERNEL_NFLASH	0x00000001
#define FLASH_BOOT_NFLASH	0x00000002

#endif /* _BCMDEVS_H */
