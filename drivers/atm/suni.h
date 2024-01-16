/* SPDX-License-Identifier: GPL-2.0 */
/*
 * drivers/atm/suni.h - S/UNI PHY driver
 */
 
/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */

#ifndef DRIVER_ATM_SUNI_H
#define DRIVER_ATM_SUNI_H

#include <linux/atmdev.h>
#include <linux/atmioc.h>
#include <linux/sonet.h>

/* SUNI registers */

#define SUNI_MRI		0x00	/* Master Reset and Identity / Load
					   Meter */
#define SUNI_MC			0x01	/* Master Configuration */
#define SUNI_MIS		0x02	/* Master Interrupt Status */
			  /* no 0x03 */
#define SUNI_MCM		0x04	/* Master Clock Monitor */
#define SUNI_MCT		0x05	/* Master Control */
#define SUNI_CSCS		0x06	/* Clock Synthesis Control and Status */
#define SUNI_CRCS		0x07	/* Clock Recovery Control and Status */
			     /* 0x08-0x0F reserved */
#define SUNI_RSOP_CIE		0x10	/* RSOP Control/Interrupt Enable */
#define SUNI_RSOP_SIS		0x11	/* RSOP Status/Interrupt Status */
#define SUNI_RSOP_SBL		0x12	/* RSOP Section BIP-8 LSB */
#define SUNI_RSOP_SBM		0x13	/* RSOP Section BIP-8 MSB */
#define SUNI_TSOP_CTRL		0x14	/* TSOP Control */
#define SUNI_TSOP_DIAG		0x15	/* TSOP Diagnostic */
			     /* 0x16-0x17 reserved */
#define SUNI_RLOP_CS		0x18	/* RLOP Control/Status */
#define SUNI_RLOP_IES		0x19	/* RLOP Interrupt Enable/Status */
#define SUNI_RLOP_LBL		0x1A	/* RLOP Line BIP-8/24 LSB */
#define SUNI_RLOP_LB		0x1B	/* RLOP Line BIP-8/24 */
#define SUNI_RLOP_LBM		0x1C	/* RLOP Line BIP-8/24 MSB */
#define SUNI_RLOP_LFL		0x1D	/* RLOP Line FEBE LSB */
#define SUNI_RLOP_LF 		0x1E	/* RLOP Line FEBE */
#define SUNI_RLOP_LFM		0x1F	/* RLOP Line FEBE MSB */
#define SUNI_TLOP_CTRL		0x20	/* TLOP Control */
#define SUNI_TLOP_DIAG		0x21	/* TLOP Diagnostic */
			     /* 0x22-0x27 reserved */
#define SUNI_SSTB_CTRL		0x28
#define SUNI_RPOP_SC		0x30	/* RPOP Status/Control */
#define SUNI_RPOP_IS		0x31	/* RPOP Interrupt Status */
			     /* 0x32 reserved */
#define SUNI_RPOP_IE		0x33	/* RPOP Interrupt Enable */
			     /* 0x34-0x36 reserved */
#define SUNI_RPOP_PSL		0x37	/* RPOP Path Signal Label */
#define SUNI_RPOP_PBL		0x38	/* RPOP Path BIP-8 LSB */
#define SUNI_RPOP_PBM		0x39	/* RPOP Path BIP-8 MSB */
#define SUNI_RPOP_PFL		0x3A	/* RPOP Path FEBE LSB */
#define SUNI_RPOP_PFM		0x3B	/* RPOP Path FEBE MSB */
			     /* 0x3C reserved */
#define SUNI_RPOP_PBC		0x3D	/* RPOP Path BIP-8 Configuration */
#define SUNI_RPOP_RC		0x3D	/* RPOP Ring Control (PM5355) */
			     /* 0x3E-0x3F reserved */
#define SUNI_TPOP_CD		0x40	/* TPOP Control/Diagnostic */
#define SUNI_TPOP_PC		0x41	/* TPOP Pointer Control */
			     /* 0x42-0x44 reserved */
#define SUNI_TPOP_APL		0x45	/* TPOP Arbitrary Pointer LSB */
#define SUNI_TPOP_APM		0x46	/* TPOP Arbitrary Pointer MSB */
			     /* 0x47 reserved */
#define SUNI_TPOP_PSL		0x48	/* TPOP Path Signal Label */
#define SUNI_TPOP_PS		0x49	/* TPOP Path Status */
			     /* 0x4A-0x4F reserved */
#define SUNI_RACP_CS		0x50	/* RACP Control/Status */
#define SUNI_RACP_IES		0x51	/* RACP Interrupt Enable/Status */
#define SUNI_RACP_MHP		0x52	/* RACP Match Header Pattern */
#define SUNI_RACP_MHM		0x53	/* RACP Match Header Mask */
#define SUNI_RACP_CHEC		0x54	/* RACP Correctable HCS Error Count */
#define SUNI_RACP_UHEC		0x55	/* RACP Uncorrectable HCS Err Count */
#define SUNI_RACP_RCCL		0x56	/* RACP Receive Cell Counter LSB */
#define SUNI_RACP_RCC		0x57	/* RACP Receive Cell Counter */
#define SUNI_RACP_RCCM		0x58	/* RACP Receive Cell Counter MSB */
#define SUNI_RACP_CFG		0x59	/* RACP Configuration */
			     /* 0x5A-0x5F reserved */
#define SUNI_TACP_CS		0x60	/* TACP Control/Status */
#define SUNI_TACP_IUCHP		0x61	/* TACP Idle/Unassigned Cell Hdr Pat */
#define SUNI_TACP_IUCPOP	0x62	/* TACP Idle/Unassigned Cell Payload
					   Octet Pattern */
#define SUNI_TACP_FIFO		0x63	/* TACP FIFO Configuration */
#define SUNI_TACP_TCCL		0x64	/* TACP Transmit Cell Counter LSB */
#define SUNI_TACP_TCC		0x65	/* TACP Transmit Cell Counter */
#define SUNI_TACP_TCCM		0x66	/* TACP Transmit Cell Counter MSB */
#define SUNI_TACP_CFG		0x67	/* TACP Configuration */
#define SUNI_SPTB_CTRL		0x68	/* SPTB Control */
			     /* 0x69-0x7F reserved */
#define	SUNI_MT			0x80	/* Master Test */
			     /* 0x81-0xFF reserved */

/* SUNI register values */


/* MRI is reg 0 */
#define SUNI_MRI_ID		0x0f	/* R, SUNI revision number */
#define SUNI_MRI_ID_SHIFT 	0
#define SUNI_MRI_TYPE		0x70	/* R, SUNI type (lite is 011) */
#define SUNI_MRI_TYPE_SHIFT 	4
#define SUNI_MRI_TYPE_PM5346	0x3	/* S/UNI 155 LITE */
#define SUNI_MRI_TYPE_PM5347	0x4	/* S/UNI 155 PLUS */
#define SUNI_MRI_TYPE_PM5350	0x7	/* S/UNI 155 ULTRA */
#define SUNI_MRI_TYPE_PM5355	0x1	/* S/UNI 622 */
#define SUNI_MRI_RESET		0x80	/* RW, reset & power down chip
					   0: normal operation
					   1: reset & low power */

/* MCM is reg 0x4 */
#define SUNI_MCM_LLE		0x20	/* line loopback (PM5355) */
#define SUNI_MCM_DLE		0x10	/* diagnostic loopback (PM5355) */

/* MCT is reg 5 */
#define SUNI_MCT_LOOPT		0x01	/* RW, timing source, 0: from
					   TRCLK+/- */
#define SUNI_MCT_DLE		0x02	/* RW, diagnostic loopback */
#define SUNI_MCT_LLE		0x04	/* RW, line loopback */
#define SUNI_MCT_FIXPTR		0x20	/* RW, disable transmit payload pointer
					   adjustments
					   0: payload ptr controlled by TPOP
					      ptr control reg
					   1: payload pointer fixed at 522 */
#define SUNI_MCT_LCDV		0x40	/* R, loss of cell delineation */
#define SUNI_MCT_LCDE		0x80	/* RW, loss of cell delineation
					   interrupt (1: on) */
/* RSOP_CIE is reg 0x10 */
#define SUNI_RSOP_CIE_OOFE	0x01	/* RW, enable interrupt on frame alarm
					   state change */
#define SUNI_RSOP_CIE_LOFE	0x02	/* RW, enable interrupt on loss of
					   frame state change */
#define SUNI_RSOP_CIE_LOSE	0x04	/* RW, enable interrupt on loss of
					   signal state change */
#define SUNI_RSOP_CIE_BIPEE	0x08	/* RW, enable interrupt on section
					   BIP-8 error (B1) */
#define SUNI_RSOP_CIE_FOOF	0x20	/* W, force RSOP out of frame at next
					   boundary */
#define SUNI_RSOP_CIE_DDS	0x40	/* RW, disable scrambling */

/* RSOP_SIS is reg 0x11 */
#define SUNI_RSOP_SIS_OOFV	0x01	/* R, out of frame */
#define SUNI_RSOP_SIS_LOFV	0x02	/* R, loss of frame */
#define SUNI_RSOP_SIS_LOSV	0x04	/* R, loss of signal */
#define SUNI_RSOP_SIS_OOFI	0x08	/* R, out of frame interrupt */
#define SUNI_RSOP_SIS_LOFI	0x10	/* R, loss of frame interrupt */
#define SUNI_RSOP_SIS_LOSI	0x20	/* R, loss of signal interrupt */
#define SUNI_RSOP_SIS_BIPEI	0x40	/* R, section BIP-8 interrupt */

/* TSOP_CTRL is reg 0x14 */
#define SUNI_TSOP_CTRL_LAIS	0x01	/* insert alarm indication signal */
#define SUNI_TSOP_CTRL_DS	0x40	/* disable scrambling */

/* TSOP_DIAG is reg 0x15 */
#define SUNI_TSOP_DIAG_DFP	0x01	/* insert single bit error cont. */
#define SUNI_TSOP_DIAG_DBIP8	0x02	/* insert section BIP err (cont) */
#define SUNI_TSOP_DIAG_DLOS	0x04	/* set line to zero (loss of signal) */

/* TLOP_DIAG is reg 0x21 */
#define SUNI_TLOP_DIAG_DBIP	0x01	/* insert line BIP err (continuously) */

/* SSTB_CTRL is reg 0x28 */
#define SUNI_SSTB_CTRL_LEN16	0x01	/* path trace message length bit */

/* RPOP_RC is reg 0x3D (PM5355) */
#define SUNI_RPOP_RC_ENSS	0x40	/* enable size bit */

/* TPOP_DIAG is reg 0x40 */
#define SUNI_TPOP_DIAG_PAIS	0x01	/* insert STS path alarm ind (cont) */
#define SUNI_TPOP_DIAG_DB3	0x02	/* insert path BIP err (continuously) */

/* TPOP_APM is reg 0x46 */
#define SUNI_TPOP_APM_APTR	0x03	/* RW, arbitrary pointer, upper 2
					   bits */
#define SUNI_TPOP_APM_APTR_SHIFT 0
#define SUNI_TPOP_APM_S		0x0c	/* RW, "unused" bits of payload
					   pointer */
#define SUNI_TPOP_APM_S_SHIFT	2
#define SUNI_TPOP_APM_NDF	0xf0	 /* RW, NDF bits */
#define SUNI_TPOP_APM_NDF_SHIFT	4

#define SUNI_TPOP_S_SONET	0	/* set S bits to 00 */
#define SUNI_TPOP_S_SDH		2	/* set S bits to 10 */

/* RACP_IES is reg 0x51 */
#define SUNI_RACP_IES_FOVRI	0x02	/* R, FIFO overrun */
#define SUNI_RACP_IES_UHCSI	0x04	/* R, uncorrectable HCS error */
#define SUNI_RACP_IES_CHCSI	0x08	/* R, correctable HCS error */
#define SUNI_RACP_IES_OOCDI	0x10	/* R, change of cell delineation
					   state */
#define SUNI_RACP_IES_FIFOE	0x20	/* RW, enable FIFO overrun interrupt */
#define SUNI_RACP_IES_HCSE	0x40	/* RW, enable HCS error interrupt */
#define SUNI_RACP_IES_OOCDE	0x80	/* RW, enable cell delineation state
					   change interrupt */

/* TACP_CS is reg 0x60 */
#define SUNI_TACP_CS_FIFORST	0x01	/* RW, reset transmit FIFO (sticky) */
#define SUNI_TACP_CS_DSCR	0x02	/* RW, disable payload scrambling */
#define SUNI_TACP_CS_HCAADD	0x04	/* RW, add coset polynomial to HCS */
#define SUNI_TACP_CS_DHCS	0x10	/* RW, insert HCS errors */
#define SUNI_TACP_CS_FOVRI	0x20	/* R, FIFO overrun */
#define SUNI_TACP_CS_TSOCI	0x40	/* R, TSOC input high */
#define SUNI_TACP_CS_FIFOE	0x80	/* RW, enable FIFO overrun interrupt */

/* TACP_IUCHP is reg 0x61 */
#define SUNI_TACP_IUCHP_CLP	0x01	/* RW, 8th bit of 4th octet of i/u
					   pattern */
#define SUNI_TACP_IUCHP_PTI	0x0e	/* RW, 5th-7th bits of 4th octet of i/u
					   pattern */
#define SUNI_TACP_IUCHP_PTI_SHIFT 1
#define SUNI_TACP_IUCHP_GFC	0xf0	/* RW, 1st-4th bits of 1st octet of i/u
					   pattern */
#define SUNI_TACP_IUCHP_GFC_SHIFT 4

/* SPTB_CTRL is reg 0x68 */
#define SUNI_SPTB_CTRL_LEN16	0x01	/* path trace message length */

/* MT is reg 0x80 */
#define SUNI_MT_HIZIO		0x01	/* RW, all but data bus & MP interface
					   tri-state */
#define SUNI_MT_HIZDATA		0x02	/* W, also tri-state data bus */
#define SUNI_MT_IOTST		0x04	/* RW, enable test mode */
#define SUNI_MT_DBCTRL		0x08	/* W, control data bus by CSB pin */
#define SUNI_MT_PMCTST		0x10	/* W, PMC test mode */
#define SUNI_MT_DS27_53		0x80	/* RW, select between 8- or 16- bit */


#define SUNI_IDLE_PATTERN       0x6a    /* idle pattern */


#ifdef __KERNEL__
struct suni_priv {
	struct k_sonet_stats sonet_stats;	/* link diagnostics */
	int loop_mode;				/* loopback mode */
	int type;				/* phy type */
	struct atm_dev *dev;			/* device back-pointer */
	struct suni_priv *next;			/* next SUNI */
};

int suni_init(struct atm_dev *dev);
#endif

#endif
