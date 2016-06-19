/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * hpc3.h: Definitions for SGI HPC3 controller
 *
 * Copyright (C) 1996 David S. Miller
 * Copyright (C) 1998 Ralf Baechle
 */

#ifndef _SGI_HPC3_H
#define _SGI_HPC3_H

#include <linux/types.h>
#include <asm/page.h>

/* An HPC DMA descriptor. */
struct hpc_dma_desc {
	u32 pbuf;	/* physical address of data buffer */
	u32 cntinfo;	/* counter and info bits */
#define HPCDMA_EOX	0x80000000 /* last desc in chain for tx */
#define HPCDMA_EOR	0x80000000 /* last desc in chain for rx */
#define HPCDMA_EOXP	0x40000000 /* end of packet for tx */
#define HPCDMA_EORP	0x40000000 /* end of packet for rx */
#define HPCDMA_XIE	0x20000000 /* irq generated when at end of this desc */
#define HPCDMA_XIU	0x01000000 /* Tx buffer in use by CPU. */
#define HPCDMA_EIPC	0x00ff0000 /* SEEQ ethernet special xternal bytecount */
#define HPCDMA_ETXD	0x00008000 /* set to one by HPC when packet tx'd */
#define HPCDMA_OWN	0x00004000 /* Denotes ring buffer ownership on rx */
#define HPCDMA_BCNT	0x00003fff /* size in bytes of this dma buffer */

	u32 pnext;	/* paddr of next hpc_dma_desc if any */
};

/* The set of regs for each HPC3 PBUS DMA channel. */
struct hpc3_pbus_dmacregs {
	volatile u32 pbdma_bptr;	/* pbus dma channel buffer ptr */
	volatile u32 pbdma_dptr;	/* pbus dma channel desc ptr */
	u32 _unused0[0x1000/4 - 2];	/* padding */
	volatile u32 pbdma_ctrl;	/* pbus dma channel control register has
					 * copletely different meaning for read
					 * compared with write */
	/* read */
#define HPC3_PDMACTRL_INT	0x00000001 /* interrupt (cleared after read) */
#define HPC3_PDMACTRL_ISACT	0x00000002 /* channel active */
	/* write */
#define HPC3_PDMACTRL_SEL	0x00000002 /* little endian transfer */
#define HPC3_PDMACTRL_RCV	0x00000004 /* direction is receive */
#define HPC3_PDMACTRL_FLSH	0x00000008 /* enable flush for receive DMA */
#define HPC3_PDMACTRL_ACT	0x00000010 /* start dma transfer */
#define HPC3_PDMACTRL_LD	0x00000020 /* load enable for ACT */
#define HPC3_PDMACTRL_RT	0x00000040 /* Use realtime GIO bus servicing */
#define HPC3_PDMACTRL_HW	0x0000ff00 /* DMA High-water mark */
#define HPC3_PDMACTRL_FB	0x003f0000 /* Ptr to beginning of fifo */
#define HPC3_PDMACTRL_FE	0x3f000000 /* Ptr to end of fifo */

	u32 _unused1[0x1000/4 - 1];	/* padding */
};

/* The HPC3 SCSI registers, this does not include external ones. */
struct hpc3_scsiregs {
	volatile u32 cbptr;	/* current dma buffer ptr, diagnostic use only */
	volatile u32 ndptr;	/* next dma descriptor ptr */
	u32 _unused0[0x1000/4 - 2];	/* padding */
	volatile u32 bcd;	/* byte count info */
#define HPC3_SBCD_BCNTMSK 0x00003fff /* bytes to transfer from/to memory */
#define HPC3_SBCD_XIE	  0x00004000 /* Send IRQ when done with cur buf */
#define HPC3_SBCD_EOX	  0x00008000 /* Indicates this is last buf in chain */

	volatile u32 ctrl;    /* control register */
#define HPC3_SCTRL_IRQ	  0x01 /* IRQ asserted, either dma done or parity */
#define HPC3_SCTRL_ENDIAN 0x02 /* DMA endian mode, 0=big 1=little */
#define HPC3_SCTRL_DIR	  0x04 /* DMA direction, 1=dev2mem 0=mem2dev */
#define HPC3_SCTRL_FLUSH  0x08 /* Tells HPC3 to flush scsi fifos */
#define HPC3_SCTRL_ACTIVE 0x10 /* SCSI DMA channel is active */
#define HPC3_SCTRL_AMASK  0x20 /* DMA active inhibits PIO */
#define HPC3_SCTRL_CRESET 0x40 /* Resets dma channel and external controller */
#define HPC3_SCTRL_PERR	  0x80 /* Bad parity on HPC3 iface to scsi controller */

	volatile u32 gfptr;	/* current GIO fifo ptr */
	volatile u32 dfptr;	/* current device fifo ptr */
	volatile u32 dconfig;	/* DMA configuration register */
#define HPC3_SDCFG_HCLK 0x00001 /* Enable DMA half clock mode */
#define HPC3_SDCFG_D1	0x00006 /* Cycles to spend in D1 state */
#define HPC3_SDCFG_D2	0x00038 /* Cycles to spend in D2 state */
#define HPC3_SDCFG_D3	0x001c0 /* Cycles to spend in D3 state */
#define HPC3_SDCFG_HWAT 0x00e00 /* DMA high water mark */
#define HPC3_SDCFG_HW	0x01000 /* Enable 16-bit halfword DMA accesses to scsi */
#define HPC3_SDCFG_SWAP 0x02000 /* Byte swap all DMA accesses */
#define HPC3_SDCFG_EPAR 0x04000 /* Enable parity checking for DMA */
#define HPC3_SDCFG_POLL 0x08000 /* hd_dreq polarity control */
#define HPC3_SDCFG_ERLY 0x30000 /* hd_dreq behavior control bits */

	volatile u32 pconfig;	/* PIO configuration register */
#define HPC3_SPCFG_P3	0x0003 /* Cycles to spend in P3 state */
#define HPC3_SPCFG_P2W	0x001c /* Cycles to spend in P2 state for writes */
#define HPC3_SPCFG_P2R	0x01e0 /* Cycles to spend in P2 state for reads */
#define HPC3_SPCFG_P1	0x0e00 /* Cycles to spend in P1 state */
#define HPC3_SPCFG_HW	0x1000 /* Enable 16-bit halfword PIO accesses to scsi */
#define HPC3_SPCFG_SWAP 0x2000 /* Byte swap all PIO accesses */
#define HPC3_SPCFG_EPAR 0x4000 /* Enable parity checking for PIO */
#define HPC3_SPCFG_FUJI 0x8000 /* Fujitsu scsi controller mode for faster dma/pio */

	u32 _unused1[0x1000/4 - 6];	/* padding */
};

/* SEEQ ethernet HPC3 registers, only one seeq per HPC3. */
struct hpc3_ethregs {
	/* Receiver registers. */
	volatile u32 rx_cbptr;	 /* current dma buffer ptr, diagnostic use only */
	volatile u32 rx_ndptr;	 /* next dma descriptor ptr */
	u32 _unused0[0x1000/4 - 2];	/* padding */
	volatile u32 rx_bcd;	/* byte count info */
#define HPC3_ERXBCD_BCNTMSK 0x00003fff /* bytes to be sent to memory */
#define HPC3_ERXBCD_XIE	    0x20000000 /* HPC3 interrupts cpu at end of this buf */
#define HPC3_ERXBCD_EOX	    0x80000000 /* flags this as end of descriptor chain */

	volatile u32 rx_ctrl;	/* control register */
#define HPC3_ERXCTRL_STAT50 0x0000003f /* Receive status reg bits of Seeq8003 */
#define HPC3_ERXCTRL_STAT6  0x00000040 /* Rdonly irq status */
#define HPC3_ERXCTRL_STAT7  0x00000080 /* Rdonlt old/new status bit from Seeq */
#define HPC3_ERXCTRL_ENDIAN 0x00000100 /* Endian for dma channel, little=1 big=0 */
#define HPC3_ERXCTRL_ACTIVE 0x00000200 /* Tells if DMA transfer is in progress */
#define HPC3_ERXCTRL_AMASK  0x00000400 /* Tells if ACTIVE inhibits PIO's to hpc3 */
#define HPC3_ERXCTRL_RBO    0x00000800 /* Receive buffer overflow if set to 1 */

	volatile u32 rx_gfptr;	/* current GIO fifo ptr */
	volatile u32 rx_dfptr;	/* current device fifo ptr */
	u32 _unused1;		/* padding */
	volatile u32 reset;	/* reset register */
#define HPC3_ERST_CRESET 0x1	/* Reset dma channel and external controller */
#define HPC3_ERST_CLRIRQ 0x2	/* Clear channel interrupt */
#define HPC3_ERST_LBACK	 0x4	/* Enable diagnostic loopback mode of Seeq8003 */

	volatile u32 dconfig;	 /* DMA configuration register */
#define HPC3_EDCFG_D1	 0x0000f /* Cycles to spend in D1 state for PIO */
#define HPC3_EDCFG_D2	 0x000f0 /* Cycles to spend in D2 state for PIO */
#define HPC3_EDCFG_D3	 0x00f00 /* Cycles to spend in D3 state for PIO */
#define HPC3_EDCFG_WCTRL 0x01000 /* Enable writes of desc into ex ctrl port */
#define HPC3_EDCFG_FRXDC 0x02000 /* Clear eop stat bits upon rxdc, hw seeq fix */
#define HPC3_EDCFG_FEOP	 0x04000 /* Bad packet marker timeout enable */
#define HPC3_EDCFG_FIRQ	 0x08000 /* Another bad packet timeout enable */
#define HPC3_EDCFG_PTO	 0x30000 /* Programmed timeout value for above two */

	volatile u32 pconfig;	/* PIO configuration register */
#define HPC3_EPCFG_P1	 0x000f /* Cycles to spend in P1 state for PIO */
#define HPC3_EPCFG_P2	 0x00f0 /* Cycles to spend in P2 state for PIO */
#define HPC3_EPCFG_P3	 0x0f00 /* Cycles to spend in P3 state for PIO */
#define HPC3_EPCFG_TST	 0x1000 /* Diagnostic ram test feature bit */

	u32 _unused2[0x1000/4 - 8];	/* padding */

	/* Transmitter registers. */
	volatile u32 tx_cbptr;	/* current dma buffer ptr, diagnostic use only */
	volatile u32 tx_ndptr;	/* next dma descriptor ptr */
	u32 _unused3[0x1000/4 - 2];	/* padding */
	volatile u32 tx_bcd;		/* byte count info */
#define HPC3_ETXBCD_BCNTMSK 0x00003fff	/* bytes to be read from memory */
#define HPC3_ETXBCD_ESAMP   0x10000000	/* if set, too late to add descriptor */
#define HPC3_ETXBCD_XIE	    0x20000000	/* Interrupt cpu at end of cur desc */
#define HPC3_ETXBCD_EOP	    0x40000000	/* Last byte of cur buf is end of packet */
#define HPC3_ETXBCD_EOX	    0x80000000	/* This buf is the end of desc chain */

	volatile u32 tx_ctrl;		/* control register */
#define HPC3_ETXCTRL_STAT30 0x0000000f	/* Rdonly copy of seeq tx stat reg */
#define HPC3_ETXCTRL_STAT4  0x00000010	/* Indicate late collision occurred */
#define HPC3_ETXCTRL_STAT75 0x000000e0	/* Rdonly irq status from seeq */
#define HPC3_ETXCTRL_ENDIAN 0x00000100	/* DMA channel endian mode, 1=little 0=big */
#define HPC3_ETXCTRL_ACTIVE 0x00000200	/* DMA tx channel is active */
#define HPC3_ETXCTRL_AMASK  0x00000400	/* Indicates ACTIVE inhibits PIO's */

	volatile u32 tx_gfptr;		/* current GIO fifo ptr */
	volatile u32 tx_dfptr;		/* current device fifo ptr */
	u32 _unused4[0x1000/4 - 4];	/* padding */
};

struct hpc3_regs {
	/* First regs for the PBUS 8 dma channels. */
	struct hpc3_pbus_dmacregs pbdma[8];

	/* Now the HPC scsi registers, we get two scsi reg sets. */
	struct hpc3_scsiregs scsi_chan0, scsi_chan1;

	/* The SEEQ hpc3 ethernet dma/control registers. */
	struct hpc3_ethregs ethregs;

	/* Here are where the hpc3 fifo's can be directly accessed
	 * via PIO accesses.  Under normal operation we never stick
	 * our grubby paws in here so it's just padding. */
	u32 _unused0[0x18000/4];

	/* HPC3 irq status regs.  Due to a peculiar bug you need to
	 * look at two different register addresses to get at all of
	 * the status bits.  The first reg can only reliably report
	 * bits 4:0 of the status, and the second reg can only
	 * reliably report bits 9:5 of the hpc3 irq status.  I told
	 * you it was a peculiar bug. ;-)
	 */
	volatile u32 istat0;		/* Irq status, only bits <4:0> reliable. */
#define HPC3_ISTAT_PBIMASK	0x0ff	/* irq bits for pbus devs 0 --> 7 */
#define HPC3_ISTAT_SC0MASK	0x100	/* irq bit for scsi channel 0 */
#define HPC3_ISTAT_SC1MASK	0x200	/* irq bit for scsi channel 1 */

	volatile u32 gio_misc;		/* GIO misc control bits. */
#define HPC3_GIOMISC_ERTIME	0x1	/* Enable external timer real time. */
#define HPC3_GIOMISC_DENDIAN	0x2	/* dma descriptor endian, 1=lit 0=big */

	u32 eeprom;			/* EEPROM data reg. */
#define HPC3_EEPROM_EPROT	0x01	/* Protect register enable */
#define HPC3_EEPROM_CSEL	0x02	/* Chip select */
#define HPC3_EEPROM_ECLK	0x04	/* EEPROM clock */
#define HPC3_EEPROM_DATO	0x08	/* Data out */
#define HPC3_EEPROM_DATI	0x10	/* Data in */

	volatile u32 istat1;		/* Irq status, only bits <9:5> reliable. */
	volatile u32 bestat;		/* Bus error interrupt status reg. */
#define HPC3_BESTAT_BLMASK	0x000ff /* Bus lane where bad parity occurred */
#define HPC3_BESTAT_CTYPE	0x00100 /* Bus cycle type, 0=PIO 1=DMA */
#define HPC3_BESTAT_PIDSHIFT	9
#define HPC3_BESTAT_PIDMASK	0x3f700 /* DMA channel parity identifier */

	u32 _unused1[0x14000/4 - 5];	/* padding */

	/* Now direct PIO per-HPC3 peripheral access to external regs. */
	volatile u32 scsi0_ext[256];	/* SCSI channel 0 external regs */
	u32 _unused2[0x7c00/4];
	volatile u32 scsi1_ext[256];	/* SCSI channel 1 external regs */
	u32 _unused3[0x7c00/4];
	volatile u32 eth_ext[320];	/* Ethernet external registers */
	u32 _unused4[0x3b00/4];

	/* Per-peripheral device external registers and DMA/PIO control. */
	volatile u32 pbus_extregs[16][256];
	volatile u32 pbus_dmacfg[8][128];
	/* Cycles to spend in D3 for reads */
#define HPC3_DMACFG_D3R_MASK		0x00000001
#define HPC3_DMACFG_D3R_SHIFT		0
	/* Cycles to spend in D4 for reads */
#define HPC3_DMACFG_D4R_MASK		0x0000001e
#define HPC3_DMACFG_D4R_SHIFT		1
	/* Cycles to spend in D5 for reads */
#define HPC3_DMACFG_D5R_MASK		0x000001e0
#define HPC3_DMACFG_D5R_SHIFT		5
	/* Cycles to spend in D3 for writes */
#define HPC3_DMACFG_D3W_MASK		0x00000200
#define HPC3_DMACFG_D3W_SHIFT		9
	/* Cycles to spend in D4 for writes */
#define HPC3_DMACFG_D4W_MASK		0x00003c00
#define HPC3_DMACFG_D4W_SHIFT		10
	/* Cycles to spend in D5 for writes */
#define HPC3_DMACFG_D5W_MASK		0x0003c000
#define HPC3_DMACFG_D5W_SHIFT		14
	/* Enable 16-bit DMA access mode */
#define HPC3_DMACFG_DS16		0x00040000
	/* Places halfwords on high 16 bits of bus */
#define HPC3_DMACFG_EVENHI		0x00080000
	/* Make this device real time */
#define HPC3_DMACFG_RTIME		0x00200000
	/* 5 bit burst count for DMA device */
#define HPC3_DMACFG_BURST_MASK		0x07c00000
#define HPC3_DMACFG_BURST_SHIFT 22
	/* Use live pbus_dreq unsynchronized signal */
#define HPC3_DMACFG_DRQLIVE		0x08000000
	volatile u32 pbus_piocfg[16][64];
	/* Cycles to spend in P2 state for reads */
#define HPC3_PIOCFG_P2R_MASK		0x00001
#define HPC3_PIOCFG_P2R_SHIFT		0
	/* Cycles to spend in P3 state for reads */
#define HPC3_PIOCFG_P3R_MASK		0x0001e
#define HPC3_PIOCFG_P3R_SHIFT		1
	/* Cycles to spend in P4 state for reads */
#define HPC3_PIOCFG_P4R_MASK		0x001e0
#define HPC3_PIOCFG_P4R_SHIFT		5
	/* Cycles to spend in P2 state for writes */
#define HPC3_PIOCFG_P2W_MASK		0x00200
#define HPC3_PIOCFG_P2W_SHIFT		9
	/* Cycles to spend in P3 state for writes */
#define HPC3_PIOCFG_P3W_MASK		0x03c00
#define HPC3_PIOCFG_P3W_SHIFT		10
	/* Cycles to spend in P4 state for writes */
#define HPC3_PIOCFG_P4W_MASK		0x3c000
#define HPC3_PIOCFG_P4W_SHIFT		14
	/* Enable 16-bit PIO accesses */
#define HPC3_PIOCFG_DS16		0x40000
	/* Place even address bits in bits <15:8> */
#define HPC3_PIOCFG_EVENHI		0x80000

	/* PBUS PROM control regs. */
	volatile u32 pbus_promwe;	/* PROM write enable register */
#define HPC3_PROM_WENAB 0x1	/* Enable writes to the PROM */

	u32 _unused5[0x0800/4 - 1];
	volatile u32 pbus_promswap;	/* Chip select swap reg */
#define HPC3_PROM_SWAP	0x1	/* invert GIO addr bit to select prom0 or prom1 */

	u32 _unused6[0x0800/4 - 1];
	volatile u32 pbus_gout; /* PROM general purpose output reg */
#define HPC3_PROM_STAT	0x1	/* General purpose status bit in gout */

	u32 _unused7[0x1000/4 - 1];
	volatile u32 rtcregs[14];	/* Dallas clock registers */
	u32 _unused8[50];
	volatile u32 bbram[8192-50-14]; /* Battery backed ram */
};

/*
 * It is possible to have two HPC3's within the address space on
 * one machine, though only having one is more likely on an Indy.
 */
extern struct hpc3_regs *hpc3c0, *hpc3c1;
#define HPC3_CHIP0_BASE		0x1fb80000	/* physical */
#define HPC3_CHIP1_BASE		0x1fb00000	/* physical */

extern void sgihpc_init(void);

#endif /* _SGI_HPC3_H */
