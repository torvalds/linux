// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2010 Broadcom Corporation
 */

#ifndef	_BRCM_DEFS_H_
#define	_BRCM_DEFS_H_

#include <linux/types.h>

#define	SI_BUS			0
#define	PCI_BUS			1
#define	PCMCIA_BUS		2
#define SDIO_BUS		3
#define JTAG_BUS		4
#define USB_BUS			5
#define SPI_BUS			6

#define	OFF	0
#define	ON	1		/* ON = 1 */
#define	AUTO	(-1)		/* Auto = -1 */

/*
 * Priority definitions according 802.1D
 */
#define	PRIO_8021D_NONE		2
#define	PRIO_8021D_BK		1
#define	PRIO_8021D_BE		0
#define	PRIO_8021D_EE		3
#define	PRIO_8021D_CL		4
#define	PRIO_8021D_VI		5
#define	PRIO_8021D_VO		6
#define	PRIO_8021D_NC		7

#define	MAXPRIO			7
#define NUMPRIO			(MAXPRIO + 1)

#define WL_NUMRATES		16	/* max # of rates in a rateset */

#define BRCM_CNTRY_BUF_SZ	4	/* Country string is 3 bytes + NUL */

#define BRCM_SET_CHANNEL	30
#define BRCM_SET_SRL		32
#define BRCM_SET_LRL		34
#define BRCM_SET_BCNPRD		76

#define BRCM_GET_CURR_RATESET	114	/* current rateset */
#define BRCM_GET_PHYLIST	180

/* Bit masks for radio disabled status - returned by WL_GET_RADIO */

#define WL_RADIO_SW_DISABLE		(1<<0)
#define WL_RADIO_HW_DISABLE		(1<<1)
/* some countries don't support any channel */
#define WL_RADIO_COUNTRY_DISABLE	(1<<3)

/* Override bit for SET_TXPWR.  if set, ignore other level limits */
#define WL_TXPWR_OVERRIDE	(1U<<31)

/* band types */
#define	BRCM_BAND_AUTO		0	/* auto-select */
#define	BRCM_BAND_5G		1	/* 5 Ghz */
#define	BRCM_BAND_2G		2	/* 2.4 Ghz */
#define	BRCM_BAND_ALL		3	/* all bands */

/* Debug levels */
#define BRCM_DL_INFO		0x00000001
#define BRCM_DL_MAC80211	0x00000002
#define BRCM_DL_RX		0x00000004
#define BRCM_DL_TX		0x00000008
#define BRCM_DL_INT		0x00000010
#define BRCM_DL_DMA		0x00000020
#define BRCM_DL_HT		0x00000040

/* Values for PM */
#define PM_OFF	0
#define PM_MAX	1
#define PM_FAST 2

/*
 * Sonics Configuration Space Registers.
 */

/* core sbconfig regs are top 256bytes of regs */
#define	SBCONFIGOFF		0xf00

/* cpp contortions to concatenate w/arg prescan */
#ifndef	PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif

#endif				/* _BRCM_DEFS_H_ */
