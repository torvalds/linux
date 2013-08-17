/*
 * pc300.h	Cyclades-PC300(tm) Kernel API Definitions.
 *
 * Author:	Ivan Passos <ivan@cyclades.com>
 *
 * Copyright:	(c) 1999-2002 Cyclades Corp.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * $Log: pc300.h,v $
 * Revision 3.12  2002/03/07 14:17:09  henrique
 * License data fixed
 *
 * Revision 3.11  2002/01/28 21:09:39  daniela
 * Included ';' after pc300hw.bus.
 *
 * Revision 3.10  2002/01/17 17:58:52  ivan
 * Support for PC300-TE/M (PMC).
 *
 * Revision 3.9  2001/09/28 13:30:53  daniela
 * Renamed dma_start routine to rx_dma_start.
 *
 * Revision 3.8  2001/09/24 13:03:45  daniela
 * Fixed BOF interrupt treatment. Created dma_start routine.
 *
 * Revision 3.7  2001/08/10 17:19:58  daniela
 * Fixed IOCTLs defines.
 *
 * Revision 3.6  2001/07/18 19:24:42  daniela
 * Included kernel version.
 *
 * Revision 3.5  2001/07/05 18:38:08  daniela
 * DMA transmission bug fix.
 *
 * Revision 3.4  2001/06/26 17:10:40  daniela
 * New configuration parameters (line code, CRC calculation and clock).
 *
 * Revision 3.3  2001/06/22 13:13:02  regina
 * MLPPP implementation
 *
 * Revision 3.2  2001/06/18 17:56:09  daniela
 * Increased DEF_MTU and TX_QUEUE_LEN.
 *
 * Revision 3.1  2001/06/15 12:41:10  regina
 * upping major version number
 *
 * Revision 1.1.1.1  2001/06/13 20:25:06  daniela
 * PC300 initial CVS version (3.4.0-pre1)
 *
 * Revision 2.3 2001/03/05 daniela
 * Created struct pc300conf, to provide the hardware information to pc300util.
 * Inclusion of 'alloc_ramsize' field on structure 'pc300hw'.
 * 
 * Revision 2.2 2000/12/22 daniela
 * Structures and defines to support pc300util: statistics, status, 
 * loopback tests, trace.
 * 
 * Revision 2.1 2000/09/28 ivan
 * Inclusion of 'iophys' and 'iosize' fields on structure 'pc300hw', to 
 * allow release of I/O region at module unload.
 * Changed location of include files.
 *
 * Revision 2.0 2000/03/27 ivan
 * Added support for the PC300/TE cards.
 *
 * Revision 1.1 2000/01/31 ivan
 * Replaced 'pc300[drv|sca].h' former PC300 driver include files.
 *
 * Revision 1.0 1999/12/16 ivan
 * First official release.
 * Inclusion of 'nchan' field on structure 'pc300hw', to allow variable 
 * number of ports per card.
 * Inclusion of 'if_ptr' field on structure 'pc300dev'.
 *
 * Revision 0.6 1999/11/17 ivan
 * Changed X.25-specific function names to comply with adopted convention.
 *
 * Revision 0.5 1999/11/16 Daniela Squassoni
 * X.25 support.
 *
 * Revision 0.4 1999/11/15 ivan
 * Inclusion of 'clock' field on structure 'pc300hw'.
 *
 * Revision 0.3 1999/11/10 ivan
 * IOCTL name changing.
 * Inclusion of driver function prototypes.
 *
 * Revision 0.2 1999/11/03 ivan
 * Inclusion of 'tx_skb' and union 'ifu' on structure 'pc300dev'.
 *
 * Revision 0.1 1999/01/15 ivan
 * Initial version.
 *
 */

#ifndef	_PC300_H
#define	_PC300_H

#include <linux/hdlc.h>
#include "hd64572.h"
#include "pc300-falc-lh.h"

#define PC300_PROTO_MLPPP 1

#define	PC300_MAXCHAN	2	/* Number of channels per card */

#define	PC300_RAMSIZE	0x40000 /* RAM window size (256Kb) */
#define	PC300_FALCSIZE	0x400	/* FALC window size (1Kb) */

#define PC300_OSC_CLOCK	24576000
#define PC300_PCI_CLOCK	33000000

#define BD_DEF_LEN	0x0800	/* DMA buffer length (2KB) */
#define DMA_TX_MEMSZ	0x8000	/* Total DMA Tx memory size (32KB/ch) */
#define DMA_RX_MEMSZ	0x10000	/* Total DMA Rx memory size (64KB/ch) */

#define N_DMA_TX_BUF	(DMA_TX_MEMSZ / BD_DEF_LEN)	/* DMA Tx buffers */
#define N_DMA_RX_BUF	(DMA_RX_MEMSZ / BD_DEF_LEN)	/* DMA Rx buffers */

/* DMA Buffer Offsets */
#define DMA_TX_BASE	((N_DMA_TX_BUF + N_DMA_RX_BUF) *	\
			 PC300_MAXCHAN * sizeof(pcsca_bd_t))
#define DMA_RX_BASE	(DMA_TX_BASE + PC300_MAXCHAN*DMA_TX_MEMSZ)

/* DMA Descriptor Offsets */
#define DMA_TX_BD_BASE	0x0000
#define DMA_RX_BD_BASE	(DMA_TX_BD_BASE + ((PC300_MAXCHAN*DMA_TX_MEMSZ / \
				BD_DEF_LEN) * sizeof(pcsca_bd_t)))

/* DMA Descriptor Macros */
#define TX_BD_ADDR(chan, n)	(DMA_TX_BD_BASE + \
				 ((N_DMA_TX_BUF*chan) + n) * sizeof(pcsca_bd_t))
#define RX_BD_ADDR(chan, n)	(DMA_RX_BD_BASE + \
				 ((N_DMA_RX_BUF*chan) + n) * sizeof(pcsca_bd_t))

/* Macro to access the FALC registers (TE only) */
#define F_REG(reg, chan)	(0x200*(chan) + ((reg)<<2))

/***************************************
 * Memory access functions/macros      *
 * (required to support Alpha systems) *
 ***************************************/
#define cpc_writeb(port,val)	{writeb((u8)(val),(port)); mb();}
#define cpc_writew(port,val)	{writew((ushort)(val),(port)); mb();}
#define cpc_writel(port,val)	{writel((u32)(val),(port)); mb();}

#define cpc_readb(port)		readb(port)
#define cpc_readw(port)		readw(port)
#define cpc_readl(port)		readl(port)

/****** Data Structures *****************************************************/

/*
 *      RUNTIME_9050 - PLX PCI9050-1 local configuration and shared runtime
 *      registers. This structure can be used to access the 9050 registers
 *      (memory mapped).
 */
struct RUNTIME_9050 {
	u32 loc_addr_range[4];	/* 00-0Ch : Local Address Ranges */
	u32 loc_rom_range;	/* 10h : Local ROM Range */
	u32 loc_addr_base[4];	/* 14-20h : Local Address Base Addrs */
	u32 loc_rom_base;	/* 24h : Local ROM Base */
	u32 loc_bus_descr[4];	/* 28-34h : Local Bus Descriptors */
	u32 rom_bus_descr;	/* 38h : ROM Bus Descriptor */
	u32 cs_base[4];		/* 3C-48h : Chip Select Base Addrs */
	u32 intr_ctrl_stat;	/* 4Ch : Interrupt Control/Status */
	u32 init_ctrl;		/* 50h : EEPROM ctrl, Init Ctrl, etc */
};

#define PLX_9050_LINT1_ENABLE	0x01
#define PLX_9050_LINT1_POL	0x02
#define PLX_9050_LINT1_STATUS	0x04
#define PLX_9050_LINT2_ENABLE	0x08
#define PLX_9050_LINT2_POL	0x10
#define PLX_9050_LINT2_STATUS	0x20
#define PLX_9050_INTR_ENABLE	0x40
#define PLX_9050_SW_INTR	0x80

/* Masks to access the init_ctrl PLX register */
#define	PC300_CLKSEL_MASK		(0x00000004UL)
#define	PC300_CHMEDIA_MASK(chan)	(0x00000020UL<<(chan*3))
#define	PC300_CTYPE_MASK		(0x00000800UL)

/* CPLD Registers (base addr = falcbase, TE only) */
/* CPLD v. 0 */
#define CPLD_REG1	0x140	/* Chip resets, DCD/CTS status */
#define CPLD_REG2	0x144	/* Clock enable , LED control */
/* CPLD v. 2 or higher */
#define CPLD_V2_REG1	0x100	/* Chip resets, DCD/CTS status */
#define CPLD_V2_REG2	0x104	/* Clock enable , LED control */
#define CPLD_ID_REG	0x108	/* CPLD version */

/* CPLD Register bit description: for the FALC bits, they should always be 
   set based on the channel (use (bit<<(2*ch)) to access the correct bit for 
   that channel) */
#define CPLD_REG1_FALC_RESET	0x01
#define CPLD_REG1_SCA_RESET	0x02
#define CPLD_REG1_GLOBAL_CLK	0x08
#define CPLD_REG1_FALC_DCD	0x10
#define CPLD_REG1_FALC_CTS	0x20

#define CPLD_REG2_FALC_TX_CLK	0x01
#define CPLD_REG2_FALC_RX_CLK	0x02
#define CPLD_REG2_FALC_LED1	0x10
#define CPLD_REG2_FALC_LED2	0x20

/* Structure with FALC-related fields (TE only) */
#define PC300_FALC_MAXLOOP	0x0000ffff	/* for falc_issue_cmd() */

typedef struct falc {
	u8 sync;	/* If true FALC is synchronized */
	u8 active;	/* if TRUE then already active */
	u8 loop_active;	/* if TRUE a line loopback UP was received */
	u8 loop_gen;	/* if TRUE a line loopback UP was issued */

	u8 num_channels;
	u8 offset;	/* 1 for T1, 0 for E1 */
	u8 full_bandwidth;

	u8 xmb_cause;
	u8 multiframe_mode;

	/* Statistics */
	u16 pden;	/* Pulse Density violation count */
	u16 los;	/* Loss of Signal count */
	u16 losr;	/* Loss of Signal recovery count */
	u16 lfa;	/* Loss of frame alignment count */
	u16 farec;	/* Frame Alignment Recovery count */
	u16 lmfa;	/* Loss of multiframe alignment count */
	u16 ais;	/* Remote Alarm indication Signal count */
	u16 sec;	/* One-second timer */
	u16 es;		/* Errored second */
	u16 rai;	/* remote alarm received */
	u16 bec;
	u16 fec;
	u16 cvc;
	u16 cec;
	u16 ebc;

	/* Status */
	u8 red_alarm;
	u8 blue_alarm;
	u8 loss_fa;
	u8 yellow_alarm;
	u8 loss_mfa;
	u8 prbs;
} falc_t;

typedef struct falc_status {
	u8 sync;	/* If true FALC is synchronized */
	u8 red_alarm;
	u8 blue_alarm;
	u8 loss_fa;
	u8 yellow_alarm;
	u8 loss_mfa;
	u8 prbs;
} falc_status_t;

typedef struct rsv_x21_status {
	u8 dcd;
	u8 dsr;
	u8 cts;
	u8 rts;
	u8 dtr;
} rsv_x21_status_t;

typedef struct pc300stats {
	int hw_type;
	u32 line_on;
	u32 line_off;
	struct net_device_stats gen_stats;
	falc_t te_stats;
} pc300stats_t;

typedef struct pc300status {
	int hw_type;
	rsv_x21_status_t gen_status;
	falc_status_t te_status;
} pc300status_t;

typedef struct pc300loopback {
	char loop_type;
	char loop_on;
} pc300loopback_t;

typedef struct pc300patterntst {
	char patrntst_on;       /* 0 - off; 1 - on; 2 - read num_errors */
	u16 num_errors;
} pc300patterntst_t;

typedef struct pc300dev {
	struct pc300ch *chan;
	u8 trace_on;
	u32 line_on;		/* DCD(X.21, RSV) / sync(TE) change counters */
	u32 line_off;
	char name[16];
	struct net_device *dev;
#ifdef CONFIG_PC300_MLPPP
	void *cpc_tty;	/* information to PC300 TTY driver */
#endif
}pc300dev_t;

typedef struct pc300hw {
	int type;		/* RSV, X21, etc. */
	int bus;		/* Bus (PCI, PMC, etc.) */
	int nchan;		/* number of channels */
	int irq;		/* interrupt request level */
	u32 clock;		/* Board clock */
	u8 cpld_id;		/* CPLD ID (TE only) */
	u16 cpld_reg1;		/* CPLD reg 1 (TE only) */
	u16 cpld_reg2;		/* CPLD reg 2 (TE only) */
	u16 gpioc_reg;		/* PLX GPIOC reg */
	u16 intctl_reg;		/* PLX Int Ctrl/Status reg */
	u32 iophys;		/* PLX registers I/O base */
	u32 iosize;		/* PLX registers I/O size */
	u32 plxphys;		/* PLX registers MMIO base (physical) */
	void __iomem * plxbase;	/* PLX registers MMIO base (virtual) */
	u32 plxsize;		/* PLX registers MMIO size */
	u32 scaphys;		/* SCA registers MMIO base (physical) */
	void __iomem * scabase;	/* SCA registers MMIO base (virtual) */
	u32 scasize;		/* SCA registers MMIO size */
	u32 ramphys;		/* On-board RAM MMIO base (physical) */
	void __iomem * rambase;	/* On-board RAM MMIO base (virtual) */
	u32 alloc_ramsize;	/* RAM MMIO size allocated by the PCI bridge */
	u32 ramsize;		/* On-board RAM MMIO size */
	u32 falcphys;		/* FALC registers MMIO base (physical) */
	void __iomem * falcbase;/* FALC registers MMIO base (virtual) */
	u32 falcsize;		/* FALC registers MMIO size */
} pc300hw_t;

typedef struct pc300chconf {
	sync_serial_settings	phys_settings;	/* Clock type/rate (in bps),
						   loopback mode */
	raw_hdlc_proto		proto_settings;	/* Encoding, parity (CRC) */
	u32 media;		/* HW media (RS232, V.35, etc.) */
	u32 proto;		/* Protocol (PPP, X.25, etc.) */

	/* TE-specific parameters */
	u8 lcode;		/* Line Code (AMI, B8ZS, etc.) */
	u8 fr_mode;		/* Frame Mode (ESF, D4, etc.) */
	u8 lbo;			/* Line Build Out */
	u8 rx_sens;		/* Rx Sensitivity (long- or short-haul) */
	u32 tslot_bitmap;	/* bit[i]=1  =>  timeslot _i_ is active */
} pc300chconf_t;

typedef struct pc300ch {
	struct pc300 *card;
	int channel;
	pc300dev_t d;
	pc300chconf_t conf;
	u8 tx_first_bd;	/* First TX DMA block descr. w/ data */
	u8 tx_next_bd;	/* Next free TX DMA block descriptor */
	u8 rx_first_bd;	/* First free RX DMA block descriptor */
	u8 rx_last_bd;	/* Last free RX DMA block descriptor */
	u8 nfree_tx_bd;	/* Number of free TX DMA block descriptors */
	falc_t falc;	/* FALC structure (TE only) */
} pc300ch_t;

typedef struct pc300 {
	pc300hw_t hw;			/* hardware config. */
	pc300ch_t chan[PC300_MAXCHAN];
	spinlock_t card_lock;
} pc300_t;

typedef struct pc300conf {
	pc300hw_t hw;
	pc300chconf_t conf;
} pc300conf_t;

/* DEV ioctl() commands */
#define	N_SPPP_IOCTLS	2

enum pc300_ioctl_cmds {
	SIOCCPCRESERVED = (SIOCDEVPRIVATE + N_SPPP_IOCTLS),
	SIOCGPC300CONF,
	SIOCSPC300CONF,
	SIOCGPC300STATUS,
	SIOCGPC300FALCSTATUS,
	SIOCGPC300UTILSTATS,
	SIOCGPC300UTILSTATUS,
	SIOCSPC300TRACE,
	SIOCSPC300LOOPBACK,
	SIOCSPC300PATTERNTEST,
};

/* Loopback types - PC300/TE boards */
enum pc300_loopback_cmds {
	PC300LOCLOOP = 1,
	PC300REMLOOP,
	PC300PAYLOADLOOP,
	PC300GENLOOPUP,
	PC300GENLOOPDOWN,
};

/* Control Constant Definitions */
#define	PC300_RSV	0x01
#define	PC300_X21	0x02
#define	PC300_TE	0x03

#define	PC300_PCI	0x00
#define	PC300_PMC	0x01

#define PC300_LC_AMI	0x01
#define PC300_LC_B8ZS	0x02
#define PC300_LC_NRZ	0x03
#define PC300_LC_HDB3	0x04

/* Framing (T1) */
#define PC300_FR_ESF		0x01
#define PC300_FR_D4		0x02
#define PC300_FR_ESF_JAPAN	0x03

/* Framing (E1) */
#define PC300_FR_MF_CRC4	0x04
#define PC300_FR_MF_NON_CRC4	0x05
#define PC300_FR_UNFRAMED	0x06

#define PC300_LBO_0_DB		0x00
#define PC300_LBO_7_5_DB	0x01
#define PC300_LBO_15_DB		0x02
#define PC300_LBO_22_5_DB	0x03

#define PC300_RX_SENS_SH	0x01
#define PC300_RX_SENS_LH	0x02

#define PC300_TX_TIMEOUT	(2*HZ)
#define PC300_TX_QUEUE_LEN	100
#define	PC300_DEF_MTU		1600

/* Function Prototypes */
int cpc_open(struct net_device *dev);

#endif	/* _PC300_H */
