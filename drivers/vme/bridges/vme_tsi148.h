/*
 * tsi148.h
 *
 * Support for the Tundra TSI148 VME Bridge chip
 *
 * Author: Tom Armistead
 * Updated and maintained by Ajit Prem
 * Copyright 2004 Motorola Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef TSI148_H
#define TSI148_H

#ifndef	PCI_VENDOR_ID_TUNDRA
#define	PCI_VENDOR_ID_TUNDRA 0x10e3
#endif

#ifndef	PCI_DEVICE_ID_TUNDRA_TSI148
#define	PCI_DEVICE_ID_TUNDRA_TSI148 0x148
#endif

/*
 *  Define the number of each that the Tsi148 supports.
 */
#define TSI148_MAX_MASTER		8	/* Max Master Windows */
#define TSI148_MAX_SLAVE		8	/* Max Slave Windows */
#define TSI148_MAX_DMA			2	/* Max DMA Controllers */
#define TSI148_MAX_MAILBOX		4	/* Max Mail Box registers */
#define TSI148_MAX_SEMAPHORE		8	/* Max Semaphores */

/* Structure used to hold driver specific information */
struct tsi148_driver {
	void __iomem *base;	/* Base Address of device registers */
	wait_queue_head_t dma_queue[2];
	wait_queue_head_t iack_queue;
	void (*lm_callback[4])(void *);	/* Called in interrupt handler */
	void *lm_data[4];
	void *crcsr_kernel;
	dma_addr_t crcsr_bus;
	struct vme_master_resource *flush_image;
	struct mutex vme_rmw;		/* Only one RMW cycle at a time */
	struct mutex vme_int;		/*
					 * Only one VME interrupt can be
					 * generated at a time, provide locking
					 */
};

/*
 * Layout of a DMAC Linked-List Descriptor
 *
 * Note: This structure is accessed via the chip and therefore must be
 *       correctly laid out - It must also be aligned on 64-bit boundaries.
 */
struct tsi148_dma_descriptor {
	__be32 dsau;      /* Source Address */
	__be32 dsal;
	__be32 ddau;      /* Destination Address */
	__be32 ddal;
	__be32 dsat;      /* Source attributes */
	__be32 ddat;      /* Destination attributes */
	__be32 dnlau;     /* Next link address */
	__be32 dnlal;
	__be32 dcnt;      /* Byte count */
	__be32 ddbs;      /* 2eSST Broadcast select */
};

struct tsi148_dma_entry {
	/*
	 * The descriptor needs to be aligned on a 64-bit boundary, we increase
	 * the chance of this by putting it first in the structure.
	 */
	struct tsi148_dma_descriptor descriptor;
	struct list_head list;
	dma_addr_t dma_handle;
};

/*
 *  TSI148 ASIC register structure overlays and bit field definitions.
 *
 *      Note:   Tsi148 Register Group (CRG) consists of the following
 *              combination of registers:
 *                      PCFS    - PCI Configuration Space Registers
 *                      LCSR    - Local Control and Status Registers
 *                      GCSR    - Global Control and Status Registers
 *                      CR/CSR  - Subset of Configuration ROM /
 *                                Control and Status Registers
 */


/*
 *  Command/Status Registers (CRG + $004)
 */
#define TSI148_PCFS_ID			0x0
#define TSI148_PCFS_CSR			0x4
#define TSI148_PCFS_CLASS		0x8
#define TSI148_PCFS_MISC0		0xC
#define TSI148_PCFS_MBARL		0x10
#define TSI148_PCFS_MBARU		0x14

#define TSI148_PCFS_SUBID		0x28

#define TSI148_PCFS_CAPP		0x34

#define TSI148_PCFS_MISC1		0x3C

#define TSI148_PCFS_XCAPP		0x40
#define TSI148_PCFS_XSTAT		0x44

/*
 * LCSR definitions
 */

/*
 *    Outbound Translations
 */
#define TSI148_LCSR_OT0_OTSAU		0x100
#define TSI148_LCSR_OT0_OTSAL		0x104
#define TSI148_LCSR_OT0_OTEAU		0x108
#define TSI148_LCSR_OT0_OTEAL		0x10C
#define TSI148_LCSR_OT0_OTOFU		0x110
#define TSI148_LCSR_OT0_OTOFL		0x114
#define TSI148_LCSR_OT0_OTBS		0x118
#define TSI148_LCSR_OT0_OTAT		0x11C

#define TSI148_LCSR_OT1_OTSAU		0x120
#define TSI148_LCSR_OT1_OTSAL		0x124
#define TSI148_LCSR_OT1_OTEAU		0x128
#define TSI148_LCSR_OT1_OTEAL		0x12C
#define TSI148_LCSR_OT1_OTOFU		0x130
#define TSI148_LCSR_OT1_OTOFL		0x134
#define TSI148_LCSR_OT1_OTBS		0x138
#define TSI148_LCSR_OT1_OTAT		0x13C

#define TSI148_LCSR_OT2_OTSAU		0x140
#define TSI148_LCSR_OT2_OTSAL		0x144
#define TSI148_LCSR_OT2_OTEAU		0x148
#define TSI148_LCSR_OT2_OTEAL		0x14C
#define TSI148_LCSR_OT2_OTOFU		0x150
#define TSI148_LCSR_OT2_OTOFL		0x154
#define TSI148_LCSR_OT2_OTBS		0x158
#define TSI148_LCSR_OT2_OTAT		0x15C

#define TSI148_LCSR_OT3_OTSAU		0x160
#define TSI148_LCSR_OT3_OTSAL		0x164
#define TSI148_LCSR_OT3_OTEAU		0x168
#define TSI148_LCSR_OT3_OTEAL		0x16C
#define TSI148_LCSR_OT3_OTOFU		0x170
#define TSI148_LCSR_OT3_OTOFL		0x174
#define TSI148_LCSR_OT3_OTBS		0x178
#define TSI148_LCSR_OT3_OTAT		0x17C

#define TSI148_LCSR_OT4_OTSAU		0x180
#define TSI148_LCSR_OT4_OTSAL		0x184
#define TSI148_LCSR_OT4_OTEAU		0x188
#define TSI148_LCSR_OT4_OTEAL		0x18C
#define TSI148_LCSR_OT4_OTOFU		0x190
#define TSI148_LCSR_OT4_OTOFL		0x194
#define TSI148_LCSR_OT4_OTBS		0x198
#define TSI148_LCSR_OT4_OTAT		0x19C

#define TSI148_LCSR_OT5_OTSAU		0x1A0
#define TSI148_LCSR_OT5_OTSAL		0x1A4
#define TSI148_LCSR_OT5_OTEAU		0x1A8
#define TSI148_LCSR_OT5_OTEAL		0x1AC
#define TSI148_LCSR_OT5_OTOFU		0x1B0
#define TSI148_LCSR_OT5_OTOFL		0x1B4
#define TSI148_LCSR_OT5_OTBS		0x1B8
#define TSI148_LCSR_OT5_OTAT		0x1BC

#define TSI148_LCSR_OT6_OTSAU		0x1C0
#define TSI148_LCSR_OT6_OTSAL		0x1C4
#define TSI148_LCSR_OT6_OTEAU		0x1C8
#define TSI148_LCSR_OT6_OTEAL		0x1CC
#define TSI148_LCSR_OT6_OTOFU		0x1D0
#define TSI148_LCSR_OT6_OTOFL		0x1D4
#define TSI148_LCSR_OT6_OTBS		0x1D8
#define TSI148_LCSR_OT6_OTAT		0x1DC

#define TSI148_LCSR_OT7_OTSAU		0x1E0
#define TSI148_LCSR_OT7_OTSAL		0x1E4
#define TSI148_LCSR_OT7_OTEAU		0x1E8
#define TSI148_LCSR_OT7_OTEAL		0x1EC
#define TSI148_LCSR_OT7_OTOFU		0x1F0
#define TSI148_LCSR_OT7_OTOFL		0x1F4
#define TSI148_LCSR_OT7_OTBS		0x1F8
#define TSI148_LCSR_OT7_OTAT		0x1FC

#define TSI148_LCSR_OT0		0x100
#define TSI148_LCSR_OT1		0x120
#define TSI148_LCSR_OT2		0x140
#define TSI148_LCSR_OT3		0x160
#define TSI148_LCSR_OT4		0x180
#define TSI148_LCSR_OT5		0x1A0
#define TSI148_LCSR_OT6		0x1C0
#define TSI148_LCSR_OT7		0x1E0

static const int TSI148_LCSR_OT[8] = { TSI148_LCSR_OT0, TSI148_LCSR_OT1,
					 TSI148_LCSR_OT2, TSI148_LCSR_OT3,
					 TSI148_LCSR_OT4, TSI148_LCSR_OT5,
					 TSI148_LCSR_OT6, TSI148_LCSR_OT7 };

#define TSI148_LCSR_OFFSET_OTSAU	0x0
#define TSI148_LCSR_OFFSET_OTSAL	0x4
#define TSI148_LCSR_OFFSET_OTEAU	0x8
#define TSI148_LCSR_OFFSET_OTEAL	0xC
#define TSI148_LCSR_OFFSET_OTOFU	0x10
#define TSI148_LCSR_OFFSET_OTOFL	0x14
#define TSI148_LCSR_OFFSET_OTBS		0x18
#define TSI148_LCSR_OFFSET_OTAT		0x1C

/*
 * VMEbus interrupt ack
 * offset  200
 */
#define TSI148_LCSR_VIACK1	0x204
#define TSI148_LCSR_VIACK2	0x208
#define TSI148_LCSR_VIACK3	0x20C
#define TSI148_LCSR_VIACK4	0x210
#define TSI148_LCSR_VIACK5	0x214
#define TSI148_LCSR_VIACK6	0x218
#define TSI148_LCSR_VIACK7	0x21C

static const int TSI148_LCSR_VIACK[8] = { 0, TSI148_LCSR_VIACK1,
				TSI148_LCSR_VIACK2, TSI148_LCSR_VIACK3,
				TSI148_LCSR_VIACK4, TSI148_LCSR_VIACK5,
				TSI148_LCSR_VIACK6, TSI148_LCSR_VIACK7 };

/*
 * RMW
 * offset    220
 */
#define TSI148_LCSR_RMWAU	0x220
#define TSI148_LCSR_RMWAL	0x224
#define TSI148_LCSR_RMWEN	0x228
#define TSI148_LCSR_RMWC	0x22C
#define TSI148_LCSR_RMWS	0x230

/*
 * VMEbus control
 * offset    234
 */
#define TSI148_LCSR_VMCTRL	0x234
#define TSI148_LCSR_VCTRL	0x238
#define TSI148_LCSR_VSTAT	0x23C

/*
 * PCI status
 * offset  240
 */
#define TSI148_LCSR_PSTAT	0x240

/*
 * VME filter.
 * offset  250
 */
#define TSI148_LCSR_VMEFL	0x250

	/*
	 * VME exception.
	 * offset  260
 */
#define TSI148_LCSR_VEAU	0x260
#define TSI148_LCSR_VEAL	0x264
#define TSI148_LCSR_VEAT	0x268

	/*
	 * PCI error
	 * offset  270
	 */
#define TSI148_LCSR_EDPAU	0x270
#define TSI148_LCSR_EDPAL	0x274
#define TSI148_LCSR_EDPXA	0x278
#define TSI148_LCSR_EDPXS	0x27C
#define TSI148_LCSR_EDPAT	0x280

	/*
	 * Inbound Translations
	 * offset  300
	 */
#define TSI148_LCSR_IT0_ITSAU		0x300
#define TSI148_LCSR_IT0_ITSAL		0x304
#define TSI148_LCSR_IT0_ITEAU		0x308
#define TSI148_LCSR_IT0_ITEAL		0x30C
#define TSI148_LCSR_IT0_ITOFU		0x310
#define TSI148_LCSR_IT0_ITOFL		0x314
#define TSI148_LCSR_IT0_ITAT		0x318

#define TSI148_LCSR_IT1_ITSAU		0x320
#define TSI148_LCSR_IT1_ITSAL		0x324
#define TSI148_LCSR_IT1_ITEAU		0x328
#define TSI148_LCSR_IT1_ITEAL		0x32C
#define TSI148_LCSR_IT1_ITOFU		0x330
#define TSI148_LCSR_IT1_ITOFL		0x334
#define TSI148_LCSR_IT1_ITAT		0x338

#define TSI148_LCSR_IT2_ITSAU		0x340
#define TSI148_LCSR_IT2_ITSAL		0x344
#define TSI148_LCSR_IT2_ITEAU		0x348
#define TSI148_LCSR_IT2_ITEAL		0x34C
#define TSI148_LCSR_IT2_ITOFU		0x350
#define TSI148_LCSR_IT2_ITOFL		0x354
#define TSI148_LCSR_IT2_ITAT		0x358

#define TSI148_LCSR_IT3_ITSAU		0x360
#define TSI148_LCSR_IT3_ITSAL		0x364
#define TSI148_LCSR_IT3_ITEAU		0x368
#define TSI148_LCSR_IT3_ITEAL		0x36C
#define TSI148_LCSR_IT3_ITOFU		0x370
#define TSI148_LCSR_IT3_ITOFL		0x374
#define TSI148_LCSR_IT3_ITAT		0x378

#define TSI148_LCSR_IT4_ITSAU		0x380
#define TSI148_LCSR_IT4_ITSAL		0x384
#define TSI148_LCSR_IT4_ITEAU		0x388
#define TSI148_LCSR_IT4_ITEAL		0x38C
#define TSI148_LCSR_IT4_ITOFU		0x390
#define TSI148_LCSR_IT4_ITOFL		0x394
#define TSI148_LCSR_IT4_ITAT		0x398

#define TSI148_LCSR_IT5_ITSAU		0x3A0
#define TSI148_LCSR_IT5_ITSAL		0x3A4
#define TSI148_LCSR_IT5_ITEAU		0x3A8
#define TSI148_LCSR_IT5_ITEAL		0x3AC
#define TSI148_LCSR_IT5_ITOFU		0x3B0
#define TSI148_LCSR_IT5_ITOFL		0x3B4
#define TSI148_LCSR_IT5_ITAT		0x3B8

#define TSI148_LCSR_IT6_ITSAU		0x3C0
#define TSI148_LCSR_IT6_ITSAL		0x3C4
#define TSI148_LCSR_IT6_ITEAU		0x3C8
#define TSI148_LCSR_IT6_ITEAL		0x3CC
#define TSI148_LCSR_IT6_ITOFU		0x3D0
#define TSI148_LCSR_IT6_ITOFL		0x3D4
#define TSI148_LCSR_IT6_ITAT		0x3D8

#define TSI148_LCSR_IT7_ITSAU		0x3E0
#define TSI148_LCSR_IT7_ITSAL		0x3E4
#define TSI148_LCSR_IT7_ITEAU		0x3E8
#define TSI148_LCSR_IT7_ITEAL		0x3EC
#define TSI148_LCSR_IT7_ITOFU		0x3F0
#define TSI148_LCSR_IT7_ITOFL		0x3F4
#define TSI148_LCSR_IT7_ITAT		0x3F8


#define TSI148_LCSR_IT0		0x300
#define TSI148_LCSR_IT1		0x320
#define TSI148_LCSR_IT2		0x340
#define TSI148_LCSR_IT3		0x360
#define TSI148_LCSR_IT4		0x380
#define TSI148_LCSR_IT5		0x3A0
#define TSI148_LCSR_IT6		0x3C0
#define TSI148_LCSR_IT7		0x3E0

static const int TSI148_LCSR_IT[8] = { TSI148_LCSR_IT0, TSI148_LCSR_IT1,
					 TSI148_LCSR_IT2, TSI148_LCSR_IT3,
					 TSI148_LCSR_IT4, TSI148_LCSR_IT5,
					 TSI148_LCSR_IT6, TSI148_LCSR_IT7 };

#define TSI148_LCSR_OFFSET_ITSAU	0x0
#define TSI148_LCSR_OFFSET_ITSAL	0x4
#define TSI148_LCSR_OFFSET_ITEAU	0x8
#define TSI148_LCSR_OFFSET_ITEAL	0xC
#define TSI148_LCSR_OFFSET_ITOFU	0x10
#define TSI148_LCSR_OFFSET_ITOFL	0x14
#define TSI148_LCSR_OFFSET_ITAT		0x18

	/*
	 * Inbound Translation GCSR
	 * offset  400
	 */
#define TSI148_LCSR_GBAU	0x400
#define TSI148_LCSR_GBAL	0x404
#define TSI148_LCSR_GCSRAT	0x408

	/*
	 * Inbound Translation CRG
	 * offset  40C
	 */
#define TSI148_LCSR_CBAU	0x40C
#define TSI148_LCSR_CBAL	0x410
#define TSI148_LCSR_CSRAT	0x414

	/*
	 * Inbound Translation CR/CSR
	 *         CRG
	 * offset  418
	 */
#define TSI148_LCSR_CROU	0x418
#define TSI148_LCSR_CROL	0x41C
#define TSI148_LCSR_CRAT	0x420

	/*
	 * Inbound Translation Location Monitor
	 * offset  424
	 */
#define TSI148_LCSR_LMBAU	0x424
#define TSI148_LCSR_LMBAL	0x428
#define TSI148_LCSR_LMAT	0x42C

	/*
	 * VMEbus Interrupt Control.
	 * offset  430
	 */
#define TSI148_LCSR_BCU		0x430
#define TSI148_LCSR_BCL		0x434
#define TSI148_LCSR_BPGTR	0x438
#define TSI148_LCSR_BPCTR	0x43C
#define TSI148_LCSR_VICR	0x440

	/*
	 * Local Bus Interrupt Control.
	 * offset  448
	 */
#define TSI148_LCSR_INTEN	0x448
#define TSI148_LCSR_INTEO	0x44C
#define TSI148_LCSR_INTS	0x450
#define TSI148_LCSR_INTC	0x454
#define TSI148_LCSR_INTM1	0x458
#define TSI148_LCSR_INTM2	0x45C

	/*
	 * DMA Controllers
	 * offset 500
	 */
#define TSI148_LCSR_DCTL0	0x500
#define TSI148_LCSR_DSTA0	0x504
#define TSI148_LCSR_DCSAU0	0x508
#define TSI148_LCSR_DCSAL0	0x50C
#define TSI148_LCSR_DCDAU0	0x510
#define TSI148_LCSR_DCDAL0	0x514
#define TSI148_LCSR_DCLAU0	0x518
#define TSI148_LCSR_DCLAL0	0x51C
#define TSI148_LCSR_DSAU0	0x520
#define TSI148_LCSR_DSAL0	0x524
#define TSI148_LCSR_DDAU0	0x528
#define TSI148_LCSR_DDAL0	0x52C
#define TSI148_LCSR_DSAT0	0x530
#define TSI148_LCSR_DDAT0	0x534
#define TSI148_LCSR_DNLAU0	0x538
#define TSI148_LCSR_DNLAL0	0x53C
#define TSI148_LCSR_DCNT0	0x540
#define TSI148_LCSR_DDBS0	0x544

#define TSI148_LCSR_DCTL1	0x580
#define TSI148_LCSR_DSTA1	0x584
#define TSI148_LCSR_DCSAU1	0x588
#define TSI148_LCSR_DCSAL1	0x58C
#define TSI148_LCSR_DCDAU1	0x590
#define TSI148_LCSR_DCDAL1	0x594
#define TSI148_LCSR_DCLAU1	0x598
#define TSI148_LCSR_DCLAL1	0x59C
#define TSI148_LCSR_DSAU1	0x5A0
#define TSI148_LCSR_DSAL1	0x5A4
#define TSI148_LCSR_DDAU1	0x5A8
#define TSI148_LCSR_DDAL1	0x5AC
#define TSI148_LCSR_DSAT1	0x5B0
#define TSI148_LCSR_DDAT1	0x5B4
#define TSI148_LCSR_DNLAU1	0x5B8
#define TSI148_LCSR_DNLAL1	0x5BC
#define TSI148_LCSR_DCNT1	0x5C0
#define TSI148_LCSR_DDBS1	0x5C4

#define TSI148_LCSR_DMA0	0x500
#define TSI148_LCSR_DMA1	0x580


static const int TSI148_LCSR_DMA[TSI148_MAX_DMA] = { TSI148_LCSR_DMA0,
						TSI148_LCSR_DMA1 };

#define TSI148_LCSR_OFFSET_DCTL		0x0
#define TSI148_LCSR_OFFSET_DSTA		0x4
#define TSI148_LCSR_OFFSET_DCSAU	0x8
#define TSI148_LCSR_OFFSET_DCSAL	0xC
#define TSI148_LCSR_OFFSET_DCDAU	0x10
#define TSI148_LCSR_OFFSET_DCDAL	0x14
#define TSI148_LCSR_OFFSET_DCLAU	0x18
#define TSI148_LCSR_OFFSET_DCLAL	0x1C
#define TSI148_LCSR_OFFSET_DSAU		0x20
#define TSI148_LCSR_OFFSET_DSAL		0x24
#define TSI148_LCSR_OFFSET_DDAU		0x28
#define TSI148_LCSR_OFFSET_DDAL		0x2C
#define TSI148_LCSR_OFFSET_DSAT		0x30
#define TSI148_LCSR_OFFSET_DDAT		0x34
#define TSI148_LCSR_OFFSET_DNLAU	0x38
#define TSI148_LCSR_OFFSET_DNLAL	0x3C
#define TSI148_LCSR_OFFSET_DCNT		0x40
#define TSI148_LCSR_OFFSET_DDBS		0x44

	/*
	 * GCSR Register Group
	 */

	/*
	 *         GCSR    CRG
	 * offset   00     600 - DEVI/VENI
	 * offset   04     604 - CTRL/GA/REVID
	 * offset   08     608 - Semaphore3/2/1/0
	 * offset   0C     60C - Seamphore7/6/5/4
	 */
#define TSI148_GCSR_ID		0x600
#define TSI148_GCSR_CSR		0x604
#define TSI148_GCSR_SEMA0	0x608
#define TSI148_GCSR_SEMA1	0x60C

	/*
	 * Mail Box
	 *         GCSR    CRG
	 * offset   10     610 - Mailbox0
	 */
#define TSI148_GCSR_MBOX0	0x610
#define TSI148_GCSR_MBOX1	0x614
#define TSI148_GCSR_MBOX2	0x618
#define TSI148_GCSR_MBOX3	0x61C

static const int TSI148_GCSR_MBOX[4] = { TSI148_GCSR_MBOX0,
					TSI148_GCSR_MBOX1,
					TSI148_GCSR_MBOX2,
					TSI148_GCSR_MBOX3 };

	/*
	 * CR/CSR
	 */

	/*
	 *        CR/CSR   CRG
	 * offset  7FFF4   FF4 - CSRBCR
	 * offset  7FFF8   FF8 - CSRBSR
	 * offset  7FFFC   FFC - CBAR
	 */
#define TSI148_CSRBCR	0xFF4
#define TSI148_CSRBSR	0xFF8
#define TSI148_CBAR	0xFFC




	/*
	 *  TSI148 Register Bit Definitions
	 */

	/*
	 *  PFCS Register Set
	 */
#define TSI148_PCFS_CMMD_SERR          (1<<8)	/* SERR_L out pin ssys err */
#define TSI148_PCFS_CMMD_PERR          (1<<6)	/* PERR_L out pin  parity */
#define TSI148_PCFS_CMMD_MSTR          (1<<2)	/* PCI bus master */
#define TSI148_PCFS_CMMD_MEMSP         (1<<1)	/* PCI mem space access  */
#define TSI148_PCFS_CMMD_IOSP          (1<<0)	/* PCI I/O space enable */

#define TSI148_PCFS_STAT_RCPVE         (1<<15)	/* Detected Parity Error */
#define TSI148_PCFS_STAT_SIGSE         (1<<14)	/* Signalled System Error */
#define TSI148_PCFS_STAT_RCVMA         (1<<13)	/* Received Master Abort */
#define TSI148_PCFS_STAT_RCVTA         (1<<12)	/* Received Target Abort */
#define TSI148_PCFS_STAT_SIGTA         (1<<11)	/* Signalled Target Abort */
#define TSI148_PCFS_STAT_SELTIM        (3<<9)	/* DELSEL Timing */
#define TSI148_PCFS_STAT_DPAR          (1<<8)	/* Data Parity Err Reported */
#define TSI148_PCFS_STAT_FAST          (1<<7)	/* Fast back-to-back Cap */
#define TSI148_PCFS_STAT_P66M          (1<<5)	/* 66 MHz Capable */
#define TSI148_PCFS_STAT_CAPL          (1<<4)	/* Capab List - address $34 */

/*
 *  Revision ID/Class Code Registers   (CRG +$008)
 */
#define TSI148_PCFS_CLAS_M             (0xFF<<24)	/* Class ID */
#define TSI148_PCFS_SUBCLAS_M          (0xFF<<16)	/* Sub-Class ID */
#define TSI148_PCFS_PROGIF_M           (0xFF<<8)	/* Sub-Class ID */
#define TSI148_PCFS_REVID_M            (0xFF<<0)	/* Rev ID */

/*
 * Cache Line Size/ Master Latency Timer/ Header Type Registers (CRG + $00C)
 */
#define TSI148_PCFS_HEAD_M             (0xFF<<16)	/* Master Lat Timer */
#define TSI148_PCFS_MLAT_M             (0xFF<<8)	/* Master Lat Timer */
#define TSI148_PCFS_CLSZ_M             (0xFF<<0)	/* Cache Line Size */

/*
 *  Memory Base Address Lower Reg (CRG + $010)
 */
#define TSI148_PCFS_MBARL_BASEL_M      (0xFFFFF<<12) /* Base Addr Lower Mask */
#define TSI148_PCFS_MBARL_PRE          (1<<3)	/* Prefetch */
#define TSI148_PCFS_MBARL_MTYPE_M      (3<<1)	/* Memory Type Mask */
#define TSI148_PCFS_MBARL_IOMEM        (1<<0)	/* I/O Space Indicator */

/*
 *  Message Signaled Interrupt Capabilities Register (CRG + $040)
 */
#define TSI148_PCFS_MSICAP_64BAC       (1<<7)	/* 64-bit Address Capable */
#define TSI148_PCFS_MSICAP_MME_M       (7<<4)	/* Multiple Msg Enable Mask */
#define TSI148_PCFS_MSICAP_MMC_M       (7<<1)	/* Multiple Msg Capable Mask */
#define TSI148_PCFS_MSICAP_MSIEN       (1<<0)	/* Msg signaled INT Enable */

/*
 *  Message Address Lower Register (CRG +$044)
 */
#define TSI148_PCFS_MSIAL_M            (0x3FFFFFFF<<2)	/* Mask */

/*
 *  Message Data Register (CRG + 4C)
 */
#define TSI148_PCFS_MSIMD_M            (0xFFFF<<0)	/* Mask */

/*
 *  PCI-X Capabilities Register (CRG + $050)
 */
#define TSI148_PCFS_PCIXCAP_MOST_M     (7<<4)	/* Max outstanding Split Tran */
#define TSI148_PCFS_PCIXCAP_MMRBC_M    (3<<2)	/* Max Mem Read byte cnt */
#define TSI148_PCFS_PCIXCAP_ERO        (1<<1)	/* Enable Relaxed Ordering */
#define TSI148_PCFS_PCIXCAP_DPERE      (1<<0)	/* Data Parity Recover Enable */

/*
 *  PCI-X Status Register (CRG +$054)
 */
#define TSI148_PCFS_PCIXSTAT_RSCEM     (1<<29)	/* Received Split Comp Error */
#define TSI148_PCFS_PCIXSTAT_DMCRS_M   (7<<26)	/* max Cumulative Read Size */
#define TSI148_PCFS_PCIXSTAT_DMOST_M   (7<<23)	/* max outstanding Split Trans
						 */
#define TSI148_PCFS_PCIXSTAT_DMMRC_M   (3<<21)	/* max mem read byte count */
#define TSI148_PCFS_PCIXSTAT_DC        (1<<20)	/* Device Complexity */
#define TSI148_PCFS_PCIXSTAT_USC       (1<<19)	/* Unexpected Split comp */
#define TSI148_PCFS_PCIXSTAT_SCD       (1<<18)	/* Split completion discard */
#define TSI148_PCFS_PCIXSTAT_133C      (1<<17)	/* 133MHz capable */
#define TSI148_PCFS_PCIXSTAT_64D       (1<<16)	/* 64 bit device */
#define TSI148_PCFS_PCIXSTAT_BN_M      (0xFF<<8)	/* Bus number */
#define TSI148_PCFS_PCIXSTAT_DN_M      (0x1F<<3)	/* Device number */
#define TSI148_PCFS_PCIXSTAT_FN_M      (7<<0)	/* Function Number */

/*
 *  LCSR Registers
 */

/*
 *  Outbound Translation Starting Address Lower
 */
#define TSI148_LCSR_OTSAL_M            (0xFFFF<<16)	/* Mask */

/*
 *  Outbound Translation Ending Address Lower
 */
#define TSI148_LCSR_OTEAL_M            (0xFFFF<<16)	/* Mask */

/*
 *  Outbound Translation Offset Lower
 */
#define TSI148_LCSR_OTOFFL_M           (0xFFFF<<16)	/* Mask */

/*
 *  Outbound Translation 2eSST Broadcast Select
 */
#define TSI148_LCSR_OTBS_M             (0xFFFFF<<0)	/* Mask */

/*
 *  Outbound Translation Attribute
 */
#define TSI148_LCSR_OTAT_EN            (1<<31)	/* Window Enable */
#define TSI148_LCSR_OTAT_MRPFD         (1<<18)	/* Prefetch Disable */

#define TSI148_LCSR_OTAT_PFS_M         (3<<16)	/* Prefetch Size Mask */
#define TSI148_LCSR_OTAT_PFS_2         (0<<16)	/* 2 Cache Lines P Size */
#define TSI148_LCSR_OTAT_PFS_4         (1<<16)	/* 4 Cache Lines P Size */
#define TSI148_LCSR_OTAT_PFS_8         (2<<16)	/* 8 Cache Lines P Size */
#define TSI148_LCSR_OTAT_PFS_16        (3<<16)	/* 16 Cache Lines P Size */

#define TSI148_LCSR_OTAT_2eSSTM_M      (7<<11)	/* 2eSST Xfer Rate Mask */
#define TSI148_LCSR_OTAT_2eSSTM_160    (0<<11)	/* 160MB/s 2eSST Xfer Rate */
#define TSI148_LCSR_OTAT_2eSSTM_267    (1<<11)	/* 267MB/s 2eSST Xfer Rate */
#define TSI148_LCSR_OTAT_2eSSTM_320    (2<<11)	/* 320MB/s 2eSST Xfer Rate */

#define TSI148_LCSR_OTAT_TM_M          (7<<8)	/* Xfer Protocol Mask */
#define TSI148_LCSR_OTAT_TM_SCT        (0<<8)	/* SCT Xfer Protocol */
#define TSI148_LCSR_OTAT_TM_BLT        (1<<8)	/* BLT Xfer Protocol */
#define TSI148_LCSR_OTAT_TM_MBLT       (2<<8)	/* MBLT Xfer Protocol */
#define TSI148_LCSR_OTAT_TM_2eVME      (3<<8)	/* 2eVME Xfer Protocol */
#define TSI148_LCSR_OTAT_TM_2eSST      (4<<8)	/* 2eSST Xfer Protocol */
#define TSI148_LCSR_OTAT_TM_2eSSTB     (5<<8)	/* 2eSST Bcast Xfer Protocol */

#define TSI148_LCSR_OTAT_DBW_M         (3<<6)	/* Max Data Width */
#define TSI148_LCSR_OTAT_DBW_16        (0<<6)	/* 16-bit Data Width */
#define TSI148_LCSR_OTAT_DBW_32        (1<<6)	/* 32-bit Data Width */

#define TSI148_LCSR_OTAT_SUP           (1<<5)	/* Supervisory Access */
#define TSI148_LCSR_OTAT_PGM           (1<<4)	/* Program Access */

#define TSI148_LCSR_OTAT_AMODE_M       (0xf<<0)	/* Address Mode Mask */
#define TSI148_LCSR_OTAT_AMODE_A16     (0<<0)	/* A16 Address Space */
#define TSI148_LCSR_OTAT_AMODE_A24     (1<<0)	/* A24 Address Space */
#define TSI148_LCSR_OTAT_AMODE_A32     (2<<0)	/* A32 Address Space */
#define TSI148_LCSR_OTAT_AMODE_A64     (4<<0)	/* A32 Address Space */
#define TSI148_LCSR_OTAT_AMODE_CRCSR   (5<<0)	/* CR/CSR Address Space */
#define TSI148_LCSR_OTAT_AMODE_USER1   (8<<0)	/* User1 Address Space */
#define TSI148_LCSR_OTAT_AMODE_USER2   (9<<0)	/* User2 Address Space */
#define TSI148_LCSR_OTAT_AMODE_USER3   (10<<0)	/* User3 Address Space */
#define TSI148_LCSR_OTAT_AMODE_USER4   (11<<0)	/* User4 Address Space */

/*
 *  VME Master Control Register  CRG+$234
 */
#define TSI148_LCSR_VMCTRL_VSA         (1<<27)	/* VMEbus Stop Ack */
#define TSI148_LCSR_VMCTRL_VS          (1<<26)	/* VMEbus Stop */
#define TSI148_LCSR_VMCTRL_DHB         (1<<25)	/* Device Has Bus */
#define TSI148_LCSR_VMCTRL_DWB         (1<<24)	/* Device Wants Bus */

#define TSI148_LCSR_VMCTRL_RMWEN       (1<<20)	/* RMW Enable */

#define TSI148_LCSR_VMCTRL_ATO_M       (7<<16)	/* Master Access Time-out Mask
						 */
#define TSI148_LCSR_VMCTRL_ATO_32      (0<<16)	/* 32 us */
#define TSI148_LCSR_VMCTRL_ATO_128     (1<<16)	/* 128 us */
#define TSI148_LCSR_VMCTRL_ATO_512     (2<<16)	/* 512 us */
#define TSI148_LCSR_VMCTRL_ATO_2M      (3<<16)	/* 2 ms */
#define TSI148_LCSR_VMCTRL_ATO_8M      (4<<16)	/* 8 ms */
#define TSI148_LCSR_VMCTRL_ATO_32M     (5<<16)	/* 32 ms */
#define TSI148_LCSR_VMCTRL_ATO_128M    (6<<16)	/* 128 ms */
#define TSI148_LCSR_VMCTRL_ATO_DIS     (7<<16)	/* Disabled */

#define TSI148_LCSR_VMCTRL_VTOFF_M     (7<<12)	/* VMEbus Master Time off */
#define TSI148_LCSR_VMCTRL_VTOFF_0     (0<<12)	/* 0us */
#define TSI148_LCSR_VMCTRL_VTOFF_1     (1<<12)	/* 1us */
#define TSI148_LCSR_VMCTRL_VTOFF_2     (2<<12)	/* 2us */
#define TSI148_LCSR_VMCTRL_VTOFF_4     (3<<12)	/* 4us */
#define TSI148_LCSR_VMCTRL_VTOFF_8     (4<<12)	/* 8us */
#define TSI148_LCSR_VMCTRL_VTOFF_16    (5<<12)	/* 16us */
#define TSI148_LCSR_VMCTRL_VTOFF_32    (6<<12)	/* 32us */
#define TSI148_LCSR_VMCTRL_VTOFF_64    (7<<12)	/* 64us */

#define TSI148_LCSR_VMCTRL_VTON_M      (7<<8)	/* VMEbus Master Time On */
#define TSI148_LCSR_VMCTRL_VTON_4      (0<<8)	/* 8us */
#define TSI148_LCSR_VMCTRL_VTON_8      (1<<8)	/* 8us */
#define TSI148_LCSR_VMCTRL_VTON_16     (2<<8)	/* 16us */
#define TSI148_LCSR_VMCTRL_VTON_32     (3<<8)	/* 32us */
#define TSI148_LCSR_VMCTRL_VTON_64     (4<<8)	/* 64us */
#define TSI148_LCSR_VMCTRL_VTON_128    (5<<8)	/* 128us */
#define TSI148_LCSR_VMCTRL_VTON_256    (6<<8)	/* 256us */
#define TSI148_LCSR_VMCTRL_VTON_512    (7<<8)	/* 512us */

#define TSI148_LCSR_VMCTRL_VREL_M      (3<<3)	/* VMEbus Master Rel Mode Mask
						 */
#define TSI148_LCSR_VMCTRL_VREL_T_D    (0<<3)	/* Time on or Done */
#define TSI148_LCSR_VMCTRL_VREL_T_R_D  (1<<3)	/* Time on and REQ or Done */
#define TSI148_LCSR_VMCTRL_VREL_T_B_D  (2<<3)	/* Time on and BCLR or Done */
#define TSI148_LCSR_VMCTRL_VREL_T_D_R  (3<<3)	/* Time on or Done and REQ */

#define TSI148_LCSR_VMCTRL_VFAIR       (1<<2)	/* VMEbus Master Fair Mode */
#define TSI148_LCSR_VMCTRL_VREQL_M     (3<<0)	/* VMEbus Master Req Level Mask
						 */

/*
 *  VMEbus Control Register CRG+$238
 */
#define TSI148_LCSR_VCTRL_LRE          (1<<31)	/* Late Retry Enable */

#define TSI148_LCSR_VCTRL_DLT_M        (0xF<<24)	/* Deadlock Timer */
#define TSI148_LCSR_VCTRL_DLT_OFF      (0<<24)	/* Deadlock Timer Off */
#define TSI148_LCSR_VCTRL_DLT_16       (1<<24)	/* 16 VCLKS */
#define TSI148_LCSR_VCTRL_DLT_32       (2<<24)	/* 32 VCLKS */
#define TSI148_LCSR_VCTRL_DLT_64       (3<<24)	/* 64 VCLKS */
#define TSI148_LCSR_VCTRL_DLT_128      (4<<24)	/* 128 VCLKS */
#define TSI148_LCSR_VCTRL_DLT_256      (5<<24)	/* 256 VCLKS */
#define TSI148_LCSR_VCTRL_DLT_512      (6<<24)	/* 512 VCLKS */
#define TSI148_LCSR_VCTRL_DLT_1024     (7<<24)	/* 1024 VCLKS */
#define TSI148_LCSR_VCTRL_DLT_2048     (8<<24)	/* 2048 VCLKS */
#define TSI148_LCSR_VCTRL_DLT_4096     (9<<24)	/* 4096 VCLKS */
#define TSI148_LCSR_VCTRL_DLT_8192     (0xA<<24)	/* 8192 VCLKS */
#define TSI148_LCSR_VCTRL_DLT_16384    (0xB<<24)	/* 16384 VCLKS */
#define TSI148_LCSR_VCTRL_DLT_32768    (0xC<<24)	/* 32768 VCLKS */

#define TSI148_LCSR_VCTRL_NERBB        (1<<20)	/* No Early Release of Bus Busy
						 */

#define TSI148_LCSR_VCTRL_SRESET       (1<<17)	/* System Reset */
#define TSI148_LCSR_VCTRL_LRESET       (1<<16)	/* Local Reset */

#define TSI148_LCSR_VCTRL_SFAILAI      (1<<15)	/* SYSFAIL Auto Slot ID */
#define TSI148_LCSR_VCTRL_BID_M        (0x1F<<8)	/* Broadcast ID Mask */

#define TSI148_LCSR_VCTRL_ATOEN        (1<<7)	/* Arbiter Time-out Enable */
#define TSI148_LCSR_VCTRL_ROBIN        (1<<6)	/* VMEbus Round Robin */

#define TSI148_LCSR_VCTRL_GTO_M        (7<<0)	/* VMEbus Global Time-out Mask
						 */
#define TSI148_LCSR_VCTRL_GTO_8	      (0<<0)	/* 8 us */
#define TSI148_LCSR_VCTRL_GTO_16	      (1<<0)	/* 16 us */
#define TSI148_LCSR_VCTRL_GTO_32	      (2<<0)	/* 32 us */
#define TSI148_LCSR_VCTRL_GTO_64	      (3<<0)	/* 64 us */
#define TSI148_LCSR_VCTRL_GTO_128      (4<<0)	/* 128 us */
#define TSI148_LCSR_VCTRL_GTO_256      (5<<0)	/* 256 us */
#define TSI148_LCSR_VCTRL_GTO_512      (6<<0)	/* 512 us */
#define TSI148_LCSR_VCTRL_GTO_DIS      (7<<0)	/* Disabled */

/*
 *  VMEbus Status Register  CRG + $23C
 */
#define TSI148_LCSR_VSTAT_CPURST       (1<<15)	/* Clear power up reset */
#define TSI148_LCSR_VSTAT_BRDFL        (1<<14)	/* Board fail */
#define TSI148_LCSR_VSTAT_PURSTS       (1<<12)	/* Power up reset status */
#define TSI148_LCSR_VSTAT_BDFAILS      (1<<11)	/* Board Fail Status */
#define TSI148_LCSR_VSTAT_SYSFAILS     (1<<10)	/* System Fail Status */
#define TSI148_LCSR_VSTAT_ACFAILS      (1<<9)	/* AC fail status */
#define TSI148_LCSR_VSTAT_SCONS        (1<<8)	/* System Cont Status */
#define TSI148_LCSR_VSTAT_GAP          (1<<5)	/* Geographic Addr Parity */
#define TSI148_LCSR_VSTAT_GA_M         (0x1F<<0)  /* Geographic Addr Mask */

/*
 *  PCI Configuration Status Register CRG+$240
 */
#define TSI148_LCSR_PSTAT_REQ64S       (1<<6)	/* Request 64 status set */
#define TSI148_LCSR_PSTAT_M66ENS       (1<<5)	/* M66ENS 66Mhz enable */
#define TSI148_LCSR_PSTAT_FRAMES       (1<<4)	/* Frame Status */
#define TSI148_LCSR_PSTAT_IRDYS        (1<<3)	/* IRDY status */
#define TSI148_LCSR_PSTAT_DEVSELS      (1<<2)	/* DEVL status */
#define TSI148_LCSR_PSTAT_STOPS        (1<<1)	/* STOP status */
#define TSI148_LCSR_PSTAT_TRDYS        (1<<0)	/* TRDY status */

/*
 *  VMEbus Exception Attributes Register  CRG + $268
 */
#define TSI148_LCSR_VEAT_VES           (1<<31)	/* Status */
#define TSI148_LCSR_VEAT_VEOF          (1<<30)	/* Overflow */
#define TSI148_LCSR_VEAT_VESCL         (1<<29)	/* Status Clear */
#define TSI148_LCSR_VEAT_2EOT          (1<<21)	/* 2e Odd Termination */
#define TSI148_LCSR_VEAT_2EST          (1<<20)	/* 2e Slave terminated */
#define TSI148_LCSR_VEAT_BERR          (1<<19)	/* Bus Error */
#define TSI148_LCSR_VEAT_LWORD         (1<<18)	/* LWORD_ signal state */
#define TSI148_LCSR_VEAT_WRITE         (1<<17)	/* WRITE_ signal state */
#define TSI148_LCSR_VEAT_IACK          (1<<16)	/* IACK_ signal state */
#define TSI148_LCSR_VEAT_DS1           (1<<15)	/* DS1_ signal state */
#define TSI148_LCSR_VEAT_DS0           (1<<14)	/* DS0_ signal state */
#define TSI148_LCSR_VEAT_AM_M          (0x3F<<8)	/* Address Mode Mask */
#define TSI148_LCSR_VEAT_XAM_M         (0xFF<<0)	/* Master AMode Mask */


/*
 * VMEbus PCI Error Diagnostics PCI/X Attributes Register  CRG + $280
 */
#define TSI148_LCSR_EDPAT_EDPCL        (1<<29)

/*
 *  Inbound Translation Starting Address Lower
 */
#define TSI148_LCSR_ITSAL6432_M        (0xFFFF<<16)	/* Mask */
#define TSI148_LCSR_ITSAL24_M          (0x00FFF<<12)	/* Mask */
#define TSI148_LCSR_ITSAL16_M          (0x0000FFF<<4)	/* Mask */

/*
 *  Inbound Translation Ending Address Lower
 */
#define TSI148_LCSR_ITEAL6432_M        (0xFFFF<<16)	/* Mask */
#define TSI148_LCSR_ITEAL24_M          (0x00FFF<<12)	/* Mask */
#define TSI148_LCSR_ITEAL16_M          (0x0000FFF<<4)	/* Mask */

/*
 *  Inbound Translation Offset Lower
 */
#define TSI148_LCSR_ITOFFL6432_M       (0xFFFF<<16)	/* Mask */
#define TSI148_LCSR_ITOFFL24_M         (0xFFFFF<<12)	/* Mask */
#define TSI148_LCSR_ITOFFL16_M         (0xFFFFFFF<<4)	/* Mask */

/*
 *  Inbound Translation Attribute
 */
#define TSI148_LCSR_ITAT_EN            (1<<31)	/* Window Enable */
#define TSI148_LCSR_ITAT_TH            (1<<18)	/* Prefetch Threshold */

#define TSI148_LCSR_ITAT_VFS_M         (3<<16)	/* Virtual FIFO Size Mask */
#define TSI148_LCSR_ITAT_VFS_64        (0<<16)	/* 64 bytes Virtual FIFO Size */
#define TSI148_LCSR_ITAT_VFS_128       (1<<16)	/* 128 bytes Virtual FIFO Sz */
#define TSI148_LCSR_ITAT_VFS_256       (2<<16)	/* 256 bytes Virtual FIFO Sz */
#define TSI148_LCSR_ITAT_VFS_512       (3<<16)	/* 512 bytes Virtual FIFO Sz */

#define TSI148_LCSR_ITAT_2eSSTM_M      (7<<12)	/* 2eSST Xfer Rate Mask */
#define TSI148_LCSR_ITAT_2eSSTM_160    (0<<12)	/* 160MB/s 2eSST Xfer Rate */
#define TSI148_LCSR_ITAT_2eSSTM_267    (1<<12)	/* 267MB/s 2eSST Xfer Rate */
#define TSI148_LCSR_ITAT_2eSSTM_320    (2<<12)	/* 320MB/s 2eSST Xfer Rate */

#define TSI148_LCSR_ITAT_2eSSTB        (1<<11)	/* 2eSST Bcast Xfer Protocol */
#define TSI148_LCSR_ITAT_2eSST         (1<<10)	/* 2eSST Xfer Protocol */
#define TSI148_LCSR_ITAT_2eVME         (1<<9)	/* 2eVME Xfer Protocol */
#define TSI148_LCSR_ITAT_MBLT          (1<<8)	/* MBLT Xfer Protocol */
#define TSI148_LCSR_ITAT_BLT           (1<<7)	/* BLT Xfer Protocol */

#define TSI148_LCSR_ITAT_AS_M          (7<<4)	/* Address Space Mask */
#define TSI148_LCSR_ITAT_AS_A16        (0<<4)	/* A16 Address Space */
#define TSI148_LCSR_ITAT_AS_A24        (1<<4)	/* A24 Address Space */
#define TSI148_LCSR_ITAT_AS_A32        (2<<4)	/* A32 Address Space */
#define TSI148_LCSR_ITAT_AS_A64        (4<<4)	/* A64 Address Space */

#define TSI148_LCSR_ITAT_SUPR          (1<<3)	/* Supervisor Access */
#define TSI148_LCSR_ITAT_NPRIV         (1<<2)	/* Non-Priv (User) Access */
#define TSI148_LCSR_ITAT_PGM           (1<<1)	/* Program Access */
#define TSI148_LCSR_ITAT_DATA          (1<<0)	/* Data Access */

/*
 *  GCSR Base Address Lower Address  CRG +$404
 */
#define TSI148_LCSR_GBAL_M             (0x7FFFFFF<<5)	/* Mask */

/*
 *  GCSR Attribute Register CRG + $408
 */
#define TSI148_LCSR_GCSRAT_EN          (1<<7)	/* Enable access to GCSR */

#define TSI148_LCSR_GCSRAT_AS_M        (7<<4)	/* Address Space Mask */
#define TSI148_LCSR_GCSRAT_AS_A16       (0<<4)	/* Address Space 16 */
#define TSI148_LCSR_GCSRAT_AS_A24       (1<<4)	/* Address Space 24 */
#define TSI148_LCSR_GCSRAT_AS_A32       (2<<4)	/* Address Space 32 */
#define TSI148_LCSR_GCSRAT_AS_A64       (4<<4)	/* Address Space 64 */

#define TSI148_LCSR_GCSRAT_SUPR        (1<<3)	/* Sup set -GCSR decoder */
#define TSI148_LCSR_GCSRAT_NPRIV       (1<<2)	/* Non-Privliged set - CGSR */
#define TSI148_LCSR_GCSRAT_PGM         (1<<1)	/* Program set - GCSR decoder */
#define TSI148_LCSR_GCSRAT_DATA        (1<<0)	/* DATA set GCSR decoder */

/*
 *  CRG Base Address Lower Address  CRG + $410
 */
#define TSI148_LCSR_CBAL_M             (0xFFFFF<<12)

/*
 *  CRG Attribute Register  CRG + $414
 */
#define TSI148_LCSR_CRGAT_EN           (1<<7)	/* Enable PRG Access */

#define TSI148_LCSR_CRGAT_AS_M         (7<<4)	/* Address Space */
#define TSI148_LCSR_CRGAT_AS_A16       (0<<4)	/* Address Space 16 */
#define TSI148_LCSR_CRGAT_AS_A24       (1<<4)	/* Address Space 24 */
#define TSI148_LCSR_CRGAT_AS_A32       (2<<4)	/* Address Space 32 */
#define TSI148_LCSR_CRGAT_AS_A64       (4<<4)	/* Address Space 64 */

#define TSI148_LCSR_CRGAT_SUPR         (1<<3)	/* Supervisor Access */
#define TSI148_LCSR_CRGAT_NPRIV        (1<<2)	/* Non-Privliged(User) Access */
#define TSI148_LCSR_CRGAT_PGM          (1<<1)	/* Program Access */
#define TSI148_LCSR_CRGAT_DATA         (1<<0)	/* Data Access */

/*
 *  CR/CSR Offset Lower Register  CRG + $41C
 */
#define TSI148_LCSR_CROL_M             (0x1FFF<<19)	/* Mask */

/*
 *  CR/CSR Attribute register  CRG + $420
 */
#define TSI148_LCSR_CRAT_EN            (1<<7)	/* Enable access to CR/CSR */

/*
 *  Location Monitor base address lower register  CRG + $428
 */
#define TSI148_LCSR_LMBAL_M            (0x7FFFFFF<<5)	/* Mask */

/*
 *  Location Monitor Attribute Register  CRG + $42C
 */
#define TSI148_LCSR_LMAT_EN            (1<<7)	/* Enable Location Monitor */

#define TSI148_LCSR_LMAT_AS_M          (7<<4)	/* Address Space MASK  */
#define TSI148_LCSR_LMAT_AS_A16        (0<<4)	/* A16 */
#define TSI148_LCSR_LMAT_AS_A24        (1<<4)	/* A24 */
#define TSI148_LCSR_LMAT_AS_A32        (2<<4)	/* A32 */
#define TSI148_LCSR_LMAT_AS_A64        (4<<4)	/* A64 */

#define TSI148_LCSR_LMAT_SUPR          (1<<3)	/* Supervisor Access */
#define TSI148_LCSR_LMAT_NPRIV         (1<<2)	/* Non-Priv (User) Access */
#define TSI148_LCSR_LMAT_PGM           (1<<1)	/* Program Access */
#define TSI148_LCSR_LMAT_DATA          (1<<0)	/* Data Access  */

/*
 *  Broadcast Pulse Generator Timer Register  CRG + $438
 */
#define TSI148_LCSR_BPGTR_BPGT_M       (0xFFFF<<0)	/* Mask */

/*
 *  Broadcast Programmable Clock Timer Register  CRG + $43C
 */
#define TSI148_LCSR_BPCTR_BPCT_M       (0xFFFFFF<<0)	/* Mask */

/*
 *  VMEbus Interrupt Control Register           CRG + $43C
 */
#define TSI148_LCSR_VICR_CNTS_M        (3<<22)	/* Cntr Source MASK */
#define TSI148_LCSR_VICR_CNTS_DIS      (1<<22)	/* Cntr Disable */
#define TSI148_LCSR_VICR_CNTS_IRQ1     (2<<22)	/* IRQ1 to Cntr */
#define TSI148_LCSR_VICR_CNTS_IRQ2     (3<<22)	/* IRQ2 to Cntr */

#define TSI148_LCSR_VICR_EDGIS_M       (3<<20)	/* Edge interrupt MASK */
#define TSI148_LCSR_VICR_EDGIS_DIS     (1<<20)	/* Edge interrupt Disable */
#define TSI148_LCSR_VICR_EDGIS_IRQ1    (2<<20)	/* IRQ1 to Edge */
#define TSI148_LCSR_VICR_EDGIS_IRQ2    (3<<20)	/* IRQ2 to Edge */

#define TSI148_LCSR_VICR_IRQIF_M       (3<<18)	/* IRQ1* Function MASK */
#define TSI148_LCSR_VICR_IRQIF_NORM    (1<<18)	/* Normal */
#define TSI148_LCSR_VICR_IRQIF_PULSE   (2<<18)	/* Pulse Generator */
#define TSI148_LCSR_VICR_IRQIF_PROG    (3<<18)	/* Programmable Clock */
#define TSI148_LCSR_VICR_IRQIF_1U      (4<<18)	/* 1us Clock */

#define TSI148_LCSR_VICR_IRQ2F_M       (3<<16)	/* IRQ2* Function MASK */
#define TSI148_LCSR_VICR_IRQ2F_NORM    (1<<16)	/* Normal */
#define TSI148_LCSR_VICR_IRQ2F_PULSE   (2<<16)	/* Pulse Generator */
#define TSI148_LCSR_VICR_IRQ2F_PROG    (3<<16)	/* Programmable Clock */
#define TSI148_LCSR_VICR_IRQ2F_1U      (4<<16)	/* 1us Clock */

#define TSI148_LCSR_VICR_BIP           (1<<15)	/* Broadcast Interrupt Pulse */

#define TSI148_LCSR_VICR_IRQC          (1<<12)	/* VMEbus IRQ Clear */
#define TSI148_LCSR_VICR_IRQS          (1<<11)	/* VMEbus IRQ Status */

#define TSI148_LCSR_VICR_IRQL_M        (7<<8)	/* VMEbus SW IRQ Level Mask */
#define TSI148_LCSR_VICR_IRQL_1        (1<<8)	/* VMEbus SW IRQ Level 1 */
#define TSI148_LCSR_VICR_IRQL_2        (2<<8)	/* VMEbus SW IRQ Level 2 */
#define TSI148_LCSR_VICR_IRQL_3        (3<<8)	/* VMEbus SW IRQ Level 3 */
#define TSI148_LCSR_VICR_IRQL_4        (4<<8)	/* VMEbus SW IRQ Level 4 */
#define TSI148_LCSR_VICR_IRQL_5        (5<<8)	/* VMEbus SW IRQ Level 5 */
#define TSI148_LCSR_VICR_IRQL_6        (6<<8)	/* VMEbus SW IRQ Level 6 */
#define TSI148_LCSR_VICR_IRQL_7        (7<<8)	/* VMEbus SW IRQ Level 7 */

static const int TSI148_LCSR_VICR_IRQL[8] = { 0, TSI148_LCSR_VICR_IRQL_1,
			TSI148_LCSR_VICR_IRQL_2, TSI148_LCSR_VICR_IRQL_3,
			TSI148_LCSR_VICR_IRQL_4, TSI148_LCSR_VICR_IRQL_5,
			TSI148_LCSR_VICR_IRQL_6, TSI148_LCSR_VICR_IRQL_7 };

#define TSI148_LCSR_VICR_STID_M        (0xFF<<0)	/* Status/ID Mask */

/*
 *  Interrupt Enable Register   CRG + $440
 */
#define TSI148_LCSR_INTEN_DMA1EN       (1<<25)	/* DMAC 1 */
#define TSI148_LCSR_INTEN_DMA0EN       (1<<24)	/* DMAC 0 */
#define TSI148_LCSR_INTEN_LM3EN        (1<<23)	/* Location Monitor 3 */
#define TSI148_LCSR_INTEN_LM2EN        (1<<22)	/* Location Monitor 2 */
#define TSI148_LCSR_INTEN_LM1EN        (1<<21)	/* Location Monitor 1 */
#define TSI148_LCSR_INTEN_LM0EN        (1<<20)	/* Location Monitor 0 */
#define TSI148_LCSR_INTEN_MB3EN        (1<<19)	/* Mail Box 3 */
#define TSI148_LCSR_INTEN_MB2EN        (1<<18)	/* Mail Box 2 */
#define TSI148_LCSR_INTEN_MB1EN        (1<<17)	/* Mail Box 1 */
#define TSI148_LCSR_INTEN_MB0EN        (1<<16)	/* Mail Box 0 */
#define TSI148_LCSR_INTEN_PERREN       (1<<13)	/* PCI/X Error */
#define TSI148_LCSR_INTEN_VERREN       (1<<12)	/* VMEbus Error */
#define TSI148_LCSR_INTEN_VIEEN        (1<<11)	/* VMEbus IRQ Edge */
#define TSI148_LCSR_INTEN_IACKEN       (1<<10)	/* IACK */
#define TSI148_LCSR_INTEN_SYSFLEN      (1<<9)	/* System Fail */
#define TSI148_LCSR_INTEN_ACFLEN       (1<<8)	/* AC Fail */
#define TSI148_LCSR_INTEN_IRQ7EN       (1<<7)	/* IRQ7 */
#define TSI148_LCSR_INTEN_IRQ6EN       (1<<6)	/* IRQ6 */
#define TSI148_LCSR_INTEN_IRQ5EN       (1<<5)	/* IRQ5 */
#define TSI148_LCSR_INTEN_IRQ4EN       (1<<4)	/* IRQ4 */
#define TSI148_LCSR_INTEN_IRQ3EN       (1<<3)	/* IRQ3 */
#define TSI148_LCSR_INTEN_IRQ2EN       (1<<2)	/* IRQ2 */
#define TSI148_LCSR_INTEN_IRQ1EN       (1<<1)	/* IRQ1 */

static const int TSI148_LCSR_INTEN_LMEN[4] = { TSI148_LCSR_INTEN_LM0EN,
					TSI148_LCSR_INTEN_LM1EN,
					TSI148_LCSR_INTEN_LM2EN,
					TSI148_LCSR_INTEN_LM3EN };

static const int TSI148_LCSR_INTEN_IRQEN[7] = { TSI148_LCSR_INTEN_IRQ1EN,
					TSI148_LCSR_INTEN_IRQ2EN,
					TSI148_LCSR_INTEN_IRQ3EN,
					TSI148_LCSR_INTEN_IRQ4EN,
					TSI148_LCSR_INTEN_IRQ5EN,
					TSI148_LCSR_INTEN_IRQ6EN,
					TSI148_LCSR_INTEN_IRQ7EN };

/*
 *  Interrupt Enable Out Register CRG + $444
 */
#define TSI148_LCSR_INTEO_DMA1EO       (1<<25)	/* DMAC 1 */
#define TSI148_LCSR_INTEO_DMA0EO       (1<<24)	/* DMAC 0 */
#define TSI148_LCSR_INTEO_LM3EO        (1<<23)	/* Loc Monitor 3 */
#define TSI148_LCSR_INTEO_LM2EO        (1<<22)	/* Loc Monitor 2 */
#define TSI148_LCSR_INTEO_LM1EO        (1<<21)	/* Loc Monitor 1 */
#define TSI148_LCSR_INTEO_LM0EO        (1<<20)	/* Location Monitor 0 */
#define TSI148_LCSR_INTEO_MB3EO        (1<<19)	/* Mail Box 3 */
#define TSI148_LCSR_INTEO_MB2EO        (1<<18)	/* Mail Box 2 */
#define TSI148_LCSR_INTEO_MB1EO        (1<<17)	/* Mail Box 1 */
#define TSI148_LCSR_INTEO_MB0EO        (1<<16)	/* Mail Box 0 */
#define TSI148_LCSR_INTEO_PERREO       (1<<13)	/* PCI/X Error */
#define TSI148_LCSR_INTEO_VERREO       (1<<12)	/* VMEbus Error */
#define TSI148_LCSR_INTEO_VIEEO        (1<<11)	/* VMEbus IRQ Edge */
#define TSI148_LCSR_INTEO_IACKEO       (1<<10)	/* IACK */
#define TSI148_LCSR_INTEO_SYSFLEO      (1<<9)	/* System Fail */
#define TSI148_LCSR_INTEO_ACFLEO       (1<<8)	/* AC Fail */
#define TSI148_LCSR_INTEO_IRQ7EO       (1<<7)	/* IRQ7 */
#define TSI148_LCSR_INTEO_IRQ6EO       (1<<6)	/* IRQ6 */
#define TSI148_LCSR_INTEO_IRQ5EO       (1<<5)	/* IRQ5 */
#define TSI148_LCSR_INTEO_IRQ4EO       (1<<4)	/* IRQ4 */
#define TSI148_LCSR_INTEO_IRQ3EO       (1<<3)	/* IRQ3 */
#define TSI148_LCSR_INTEO_IRQ2EO       (1<<2)	/* IRQ2 */
#define TSI148_LCSR_INTEO_IRQ1EO       (1<<1)	/* IRQ1 */

static const int TSI148_LCSR_INTEO_LMEO[4] = { TSI148_LCSR_INTEO_LM0EO,
					TSI148_LCSR_INTEO_LM1EO,
					TSI148_LCSR_INTEO_LM2EO,
					TSI148_LCSR_INTEO_LM3EO };

static const int TSI148_LCSR_INTEO_IRQEO[7] = { TSI148_LCSR_INTEO_IRQ1EO,
					TSI148_LCSR_INTEO_IRQ2EO,
					TSI148_LCSR_INTEO_IRQ3EO,
					TSI148_LCSR_INTEO_IRQ4EO,
					TSI148_LCSR_INTEO_IRQ5EO,
					TSI148_LCSR_INTEO_IRQ6EO,
					TSI148_LCSR_INTEO_IRQ7EO };

/*
 *  Interrupt Status Register CRG + $448
 */
#define TSI148_LCSR_INTS_DMA1S         (1<<25)	/* DMA 1 */
#define TSI148_LCSR_INTS_DMA0S         (1<<24)	/* DMA 0 */
#define TSI148_LCSR_INTS_LM3S          (1<<23)	/* Location Monitor 3 */
#define TSI148_LCSR_INTS_LM2S          (1<<22)	/* Location Monitor 2 */
#define TSI148_LCSR_INTS_LM1S          (1<<21)	/* Location Monitor 1 */
#define TSI148_LCSR_INTS_LM0S          (1<<20)	/* Location Monitor 0 */
#define TSI148_LCSR_INTS_MB3S          (1<<19)	/* Mail Box 3 */
#define TSI148_LCSR_INTS_MB2S          (1<<18)	/* Mail Box 2 */
#define TSI148_LCSR_INTS_MB1S          (1<<17)	/* Mail Box 1 */
#define TSI148_LCSR_INTS_MB0S          (1<<16)	/* Mail Box 0 */
#define TSI148_LCSR_INTS_PERRS         (1<<13)	/* PCI/X Error */
#define TSI148_LCSR_INTS_VERRS         (1<<12)	/* VMEbus Error */
#define TSI148_LCSR_INTS_VIES          (1<<11)	/* VMEbus IRQ Edge */
#define TSI148_LCSR_INTS_IACKS         (1<<10)	/* IACK */
#define TSI148_LCSR_INTS_SYSFLS        (1<<9)	/* System Fail */
#define TSI148_LCSR_INTS_ACFLS         (1<<8)	/* AC Fail */
#define TSI148_LCSR_INTS_IRQ7S         (1<<7)	/* IRQ7 */
#define TSI148_LCSR_INTS_IRQ6S         (1<<6)	/* IRQ6 */
#define TSI148_LCSR_INTS_IRQ5S         (1<<5)	/* IRQ5 */
#define TSI148_LCSR_INTS_IRQ4S         (1<<4)	/* IRQ4 */
#define TSI148_LCSR_INTS_IRQ3S         (1<<3)	/* IRQ3 */
#define TSI148_LCSR_INTS_IRQ2S         (1<<2)	/* IRQ2 */
#define TSI148_LCSR_INTS_IRQ1S         (1<<1)	/* IRQ1 */

static const int TSI148_LCSR_INTS_LMS[4] = { TSI148_LCSR_INTS_LM0S,
					TSI148_LCSR_INTS_LM1S,
					TSI148_LCSR_INTS_LM2S,
					TSI148_LCSR_INTS_LM3S };

static const int TSI148_LCSR_INTS_MBS[4] = { TSI148_LCSR_INTS_MB0S,
					TSI148_LCSR_INTS_MB1S,
					TSI148_LCSR_INTS_MB2S,
					TSI148_LCSR_INTS_MB3S };

/*
 *  Interrupt Clear Register CRG + $44C
 */
#define TSI148_LCSR_INTC_DMA1C         (1<<25)	/* DMA 1 */
#define TSI148_LCSR_INTC_DMA0C         (1<<24)	/* DMA 0 */
#define TSI148_LCSR_INTC_LM3C          (1<<23)	/* Location Monitor 3 */
#define TSI148_LCSR_INTC_LM2C          (1<<22)	/* Location Monitor 2 */
#define TSI148_LCSR_INTC_LM1C          (1<<21)	/* Location Monitor 1 */
#define TSI148_LCSR_INTC_LM0C          (1<<20)	/* Location Monitor 0 */
#define TSI148_LCSR_INTC_MB3C          (1<<19)	/* Mail Box 3 */
#define TSI148_LCSR_INTC_MB2C          (1<<18)	/* Mail Box 2 */
#define TSI148_LCSR_INTC_MB1C          (1<<17)	/* Mail Box 1 */
#define TSI148_LCSR_INTC_MB0C          (1<<16)	/* Mail Box 0 */
#define TSI148_LCSR_INTC_PERRC         (1<<13)	/* VMEbus Error */
#define TSI148_LCSR_INTC_VERRC         (1<<12)	/* VMEbus Access Time-out */
#define TSI148_LCSR_INTC_VIEC          (1<<11)	/* VMEbus IRQ Edge */
#define TSI148_LCSR_INTC_IACKC         (1<<10)	/* IACK */
#define TSI148_LCSR_INTC_SYSFLC        (1<<9)	/* System Fail */
#define TSI148_LCSR_INTC_ACFLC         (1<<8)	/* AC Fail */

static const int TSI148_LCSR_INTC_LMC[4] = { TSI148_LCSR_INTC_LM0C,
					TSI148_LCSR_INTC_LM1C,
					TSI148_LCSR_INTC_LM2C,
					TSI148_LCSR_INTC_LM3C };

static const int TSI148_LCSR_INTC_MBC[4] = { TSI148_LCSR_INTC_MB0C,
					TSI148_LCSR_INTC_MB1C,
					TSI148_LCSR_INTC_MB2C,
					TSI148_LCSR_INTC_MB3C };

/*
 *  Interrupt Map Register 1 CRG + $458
 */
#define TSI148_LCSR_INTM1_DMA1M_M      (3<<18)	/* DMA 1 */
#define TSI148_LCSR_INTM1_DMA0M_M      (3<<16)	/* DMA 0 */
#define TSI148_LCSR_INTM1_LM3M_M       (3<<14)	/* Location Monitor 3 */
#define TSI148_LCSR_INTM1_LM2M_M       (3<<12)	/* Location Monitor 2 */
#define TSI148_LCSR_INTM1_LM1M_M       (3<<10)	/* Location Monitor 1 */
#define TSI148_LCSR_INTM1_LM0M_M       (3<<8)	/* Location Monitor 0 */
#define TSI148_LCSR_INTM1_MB3M_M       (3<<6)	/* Mail Box 3 */
#define TSI148_LCSR_INTM1_MB2M_M       (3<<4)	/* Mail Box 2 */
#define TSI148_LCSR_INTM1_MB1M_M       (3<<2)	/* Mail Box 1 */
#define TSI148_LCSR_INTM1_MB0M_M       (3<<0)	/* Mail Box 0 */

/*
 *  Interrupt Map Register 2 CRG + $45C
 */
#define TSI148_LCSR_INTM2_PERRM_M      (3<<26)	/* PCI Bus Error */
#define TSI148_LCSR_INTM2_VERRM_M      (3<<24)	/* VMEbus Error */
#define TSI148_LCSR_INTM2_VIEM_M       (3<<22)	/* VMEbus IRQ Edge */
#define TSI148_LCSR_INTM2_IACKM_M      (3<<20)	/* IACK */
#define TSI148_LCSR_INTM2_SYSFLM_M     (3<<18)	/* System Fail */
#define TSI148_LCSR_INTM2_ACFLM_M      (3<<16)	/* AC Fail */
#define TSI148_LCSR_INTM2_IRQ7M_M      (3<<14)	/* IRQ7 */
#define TSI148_LCSR_INTM2_IRQ6M_M      (3<<12)	/* IRQ6 */
#define TSI148_LCSR_INTM2_IRQ5M_M      (3<<10)	/* IRQ5 */
#define TSI148_LCSR_INTM2_IRQ4M_M      (3<<8)	/* IRQ4 */
#define TSI148_LCSR_INTM2_IRQ3M_M      (3<<6)	/* IRQ3 */
#define TSI148_LCSR_INTM2_IRQ2M_M      (3<<4)	/* IRQ2 */
#define TSI148_LCSR_INTM2_IRQ1M_M      (3<<2)	/* IRQ1 */

/*
 *  DMA Control (0-1) Registers CRG + $500
 */
#define TSI148_LCSR_DCTL_ABT           (1<<27)	/* Abort */
#define TSI148_LCSR_DCTL_PAU           (1<<26)	/* Pause */
#define TSI148_LCSR_DCTL_DGO           (1<<25)	/* DMA Go */

#define TSI148_LCSR_DCTL_MOD           (1<<23)	/* Mode */

#define TSI148_LCSR_DCTL_VBKS_M        (7<<12)	/* VMEbus block Size MASK */
#define TSI148_LCSR_DCTL_VBKS_32       (0<<12)	/* VMEbus block Size 32 */
#define TSI148_LCSR_DCTL_VBKS_64       (1<<12)	/* VMEbus block Size 64 */
#define TSI148_LCSR_DCTL_VBKS_128      (2<<12)	/* VMEbus block Size 128 */
#define TSI148_LCSR_DCTL_VBKS_256      (3<<12)	/* VMEbus block Size 256 */
#define TSI148_LCSR_DCTL_VBKS_512      (4<<12)	/* VMEbus block Size 512 */
#define TSI148_LCSR_DCTL_VBKS_1024     (5<<12)	/* VMEbus block Size 1024 */
#define TSI148_LCSR_DCTL_VBKS_2048     (6<<12)	/* VMEbus block Size 2048 */
#define TSI148_LCSR_DCTL_VBKS_4096     (7<<12)	/* VMEbus block Size 4096 */

#define TSI148_LCSR_DCTL_VBOT_M        (7<<8)	/* VMEbus back-off MASK */
#define TSI148_LCSR_DCTL_VBOT_0        (0<<8)	/* VMEbus back-off  0us */
#define TSI148_LCSR_DCTL_VBOT_1        (1<<8)	/* VMEbus back-off 1us */
#define TSI148_LCSR_DCTL_VBOT_2        (2<<8)	/* VMEbus back-off 2us */
#define TSI148_LCSR_DCTL_VBOT_4        (3<<8)	/* VMEbus back-off 4us */
#define TSI148_LCSR_DCTL_VBOT_8        (4<<8)	/* VMEbus back-off 8us */
#define TSI148_LCSR_DCTL_VBOT_16       (5<<8)	/* VMEbus back-off 16us */
#define TSI148_LCSR_DCTL_VBOT_32       (6<<8)	/* VMEbus back-off 32us */
#define TSI148_LCSR_DCTL_VBOT_64       (7<<8)	/* VMEbus back-off 64us */

#define TSI148_LCSR_DCTL_PBKS_M        (7<<4)	/* PCI block size MASK */
#define TSI148_LCSR_DCTL_PBKS_32       (0<<4)	/* PCI block size 32 bytes */
#define TSI148_LCSR_DCTL_PBKS_64       (1<<4)	/* PCI block size 64 bytes */
#define TSI148_LCSR_DCTL_PBKS_128      (2<<4)	/* PCI block size 128 bytes */
#define TSI148_LCSR_DCTL_PBKS_256      (3<<4)	/* PCI block size 256 bytes */
#define TSI148_LCSR_DCTL_PBKS_512      (4<<4)	/* PCI block size 512 bytes */
#define TSI148_LCSR_DCTL_PBKS_1024     (5<<4)	/* PCI block size 1024 bytes */
#define TSI148_LCSR_DCTL_PBKS_2048     (6<<4)	/* PCI block size 2048 bytes */
#define TSI148_LCSR_DCTL_PBKS_4096     (7<<4)	/* PCI block size 4096 bytes */

#define TSI148_LCSR_DCTL_PBOT_M        (7<<0)	/* PCI back off MASK */
#define TSI148_LCSR_DCTL_PBOT_0        (0<<0)	/* PCI back off 0us */
#define TSI148_LCSR_DCTL_PBOT_1        (1<<0)	/* PCI back off 1us */
#define TSI148_LCSR_DCTL_PBOT_2        (2<<0)	/* PCI back off 2us */
#define TSI148_LCSR_DCTL_PBOT_4        (3<<0)	/* PCI back off 3us */
#define TSI148_LCSR_DCTL_PBOT_8        (4<<0)	/* PCI back off 4us */
#define TSI148_LCSR_DCTL_PBOT_16       (5<<0)	/* PCI back off 8us */
#define TSI148_LCSR_DCTL_PBOT_32       (6<<0)	/* PCI back off 16us */
#define TSI148_LCSR_DCTL_PBOT_64       (7<<0)	/* PCI back off 32us */

/*
 *  DMA Status Registers (0-1)  CRG + $504
 */
#define TSI148_LCSR_DSTA_SMA           (1<<31)	/* PCI Signalled Master Abt */
#define TSI148_LCSR_DSTA_RTA           (1<<30)	/* PCI Received Target Abt */
#define TSI148_LCSR_DSTA_MRC           (1<<29)	/* PCI Max Retry Count */
#define TSI148_LCSR_DSTA_VBE           (1<<28)	/* VMEbus error */
#define TSI148_LCSR_DSTA_ABT           (1<<27)	/* Abort */
#define TSI148_LCSR_DSTA_PAU           (1<<26)	/* Pause */
#define TSI148_LCSR_DSTA_DON           (1<<25)	/* Done */
#define TSI148_LCSR_DSTA_BSY           (1<<24)	/* Busy */

/*
 *  DMA Current Link Address Lower (0-1)
 */
#define TSI148_LCSR_DCLAL_M            (0x3FFFFFF<<6)	/* Mask */

/*
 *  DMA Source Attribute (0-1) Reg
 */
#define TSI148_LCSR_DSAT_TYP_M         (3<<28)	/* Source Bus Type */
#define TSI148_LCSR_DSAT_TYP_PCI       (0<<28)	/* PCI Bus */
#define TSI148_LCSR_DSAT_TYP_VME       (1<<28)	/* VMEbus */
#define TSI148_LCSR_DSAT_TYP_PAT       (2<<28)	/* Data Pattern */

#define TSI148_LCSR_DSAT_PSZ           (1<<25)	/* Pattern Size */
#define TSI148_LCSR_DSAT_NIN           (1<<24)	/* No Increment */

#define TSI148_LCSR_DSAT_2eSSTM_M      (3<<11)	/* 2eSST Trans Rate Mask */
#define TSI148_LCSR_DSAT_2eSSTM_160    (0<<11)	/* 160 MB/s */
#define TSI148_LCSR_DSAT_2eSSTM_267    (1<<11)	/* 267 MB/s */
#define TSI148_LCSR_DSAT_2eSSTM_320    (2<<11)	/* 320 MB/s */

#define TSI148_LCSR_DSAT_TM_M          (7<<8)	/* Bus Transfer Protocol Mask */
#define TSI148_LCSR_DSAT_TM_SCT        (0<<8)	/* SCT */
#define TSI148_LCSR_DSAT_TM_BLT        (1<<8)	/* BLT */
#define TSI148_LCSR_DSAT_TM_MBLT       (2<<8)	/* MBLT */
#define TSI148_LCSR_DSAT_TM_2eVME      (3<<8)	/* 2eVME */
#define TSI148_LCSR_DSAT_TM_2eSST      (4<<8)	/* 2eSST */
#define TSI148_LCSR_DSAT_TM_2eSSTB     (5<<8)	/* 2eSST Broadcast */

#define TSI148_LCSR_DSAT_DBW_M         (3<<6)	/* Max Data Width MASK */
#define TSI148_LCSR_DSAT_DBW_16        (0<<6)	/* 16 Bits */
#define TSI148_LCSR_DSAT_DBW_32        (1<<6)	/* 32 Bits */

#define TSI148_LCSR_DSAT_SUP           (1<<5)	/* Supervisory Mode */
#define TSI148_LCSR_DSAT_PGM           (1<<4)	/* Program Mode */

#define TSI148_LCSR_DSAT_AMODE_M       (0xf<<0)	/* Address Space Mask */
#define TSI148_LCSR_DSAT_AMODE_A16     (0<<0)	/* A16 */
#define TSI148_LCSR_DSAT_AMODE_A24     (1<<0)	/* A24 */
#define TSI148_LCSR_DSAT_AMODE_A32     (2<<0)	/* A32 */
#define TSI148_LCSR_DSAT_AMODE_A64     (4<<0)	/* A64 */
#define TSI148_LCSR_DSAT_AMODE_CRCSR   (5<<0)	/* CR/CSR */
#define TSI148_LCSR_DSAT_AMODE_USER1   (8<<0)	/* User1 */
#define TSI148_LCSR_DSAT_AMODE_USER2   (9<<0)	/* User2 */
#define TSI148_LCSR_DSAT_AMODE_USER3   (0xa<<0)	/* User3 */
#define TSI148_LCSR_DSAT_AMODE_USER4   (0xb<<0)	/* User4 */

/*
 *  DMA Destination Attribute Registers (0-1)
 */
#define TSI148_LCSR_DDAT_TYP_PCI       (0<<28)	/* Destination PCI Bus  */
#define TSI148_LCSR_DDAT_TYP_VME       (1<<28)	/* Destination VMEbus */

#define TSI148_LCSR_DDAT_2eSSTM_M      (3<<11)	/* 2eSST Transfer Rate Mask */
#define TSI148_LCSR_DDAT_2eSSTM_160    (0<<11)	/* 160 MB/s */
#define TSI148_LCSR_DDAT_2eSSTM_267    (1<<11)	/* 267 MB/s */
#define TSI148_LCSR_DDAT_2eSSTM_320    (2<<11)	/* 320 MB/s */

#define TSI148_LCSR_DDAT_TM_M          (7<<8)	/* Bus Transfer Protocol Mask */
#define TSI148_LCSR_DDAT_TM_SCT        (0<<8)	/* SCT */
#define TSI148_LCSR_DDAT_TM_BLT        (1<<8)	/* BLT */
#define TSI148_LCSR_DDAT_TM_MBLT       (2<<8)	/* MBLT */
#define TSI148_LCSR_DDAT_TM_2eVME      (3<<8)	/* 2eVME */
#define TSI148_LCSR_DDAT_TM_2eSST      (4<<8)	/* 2eSST */
#define TSI148_LCSR_DDAT_TM_2eSSTB     (5<<8)	/* 2eSST Broadcast */

#define TSI148_LCSR_DDAT_DBW_M         (3<<6)	/* Max Data Width MASK */
#define TSI148_LCSR_DDAT_DBW_16        (0<<6)	/* 16 Bits */
#define TSI148_LCSR_DDAT_DBW_32        (1<<6)	/* 32 Bits */

#define TSI148_LCSR_DDAT_SUP           (1<<5)	/* Supervisory/User Access */
#define TSI148_LCSR_DDAT_PGM           (1<<4)	/* Program/Data Access */

#define TSI148_LCSR_DDAT_AMODE_M       (0xf<<0)	/* Address Space Mask */
#define TSI148_LCSR_DDAT_AMODE_A16      (0<<0)	/* A16 */
#define TSI148_LCSR_DDAT_AMODE_A24      (1<<0)	/* A24 */
#define TSI148_LCSR_DDAT_AMODE_A32      (2<<0)	/* A32 */
#define TSI148_LCSR_DDAT_AMODE_A64      (4<<0)	/* A64 */
#define TSI148_LCSR_DDAT_AMODE_CRCSR   (5<<0)	/* CRC/SR */
#define TSI148_LCSR_DDAT_AMODE_USER1   (8<<0)	/* User1 */
#define TSI148_LCSR_DDAT_AMODE_USER2   (9<<0)	/* User2 */
#define TSI148_LCSR_DDAT_AMODE_USER3   (0xa<<0)	/* User3 */
#define TSI148_LCSR_DDAT_AMODE_USER4   (0xb<<0)	/* User4 */

/*
 *  DMA Next Link Address Lower
 */
#define TSI148_LCSR_DNLAL_DNLAL_M      (0x3FFFFFF<<6)	/* Address Mask */
#define TSI148_LCSR_DNLAL_LLA          (1<<0)  /* Last Link Address Indicator */

/*
 *  DMA 2eSST Broadcast Select
 */
#define TSI148_LCSR_DBS_M              (0x1FFFFF<<0)	/* Mask */

/*
 *  GCSR Register Group
 */

/*
 *  GCSR Control and Status Register  CRG + $604
 */
#define TSI148_GCSR_GCTRL_LRST         (1<<15)	/* Local Reset */
#define TSI148_GCSR_GCTRL_SFAILEN      (1<<14)	/* System Fail enable */
#define TSI148_GCSR_GCTRL_BDFAILS      (1<<13)	/* Board Fail Status */
#define TSI148_GCSR_GCTRL_SCON         (1<<12)	/* System Copntroller */
#define TSI148_GCSR_GCTRL_MEN          (1<<11)	/* Module Enable (READY) */

#define TSI148_GCSR_GCTRL_LMI3S        (1<<7)	/* Loc Monitor 3 Int Status */
#define TSI148_GCSR_GCTRL_LMI2S        (1<<6)	/* Loc Monitor 2 Int Status */
#define TSI148_GCSR_GCTRL_LMI1S        (1<<5)	/* Loc Monitor 1 Int Status */
#define TSI148_GCSR_GCTRL_LMI0S        (1<<4)	/* Loc Monitor 0 Int Status */
#define TSI148_GCSR_GCTRL_MBI3S        (1<<3)	/* Mail box 3 Int Status */
#define TSI148_GCSR_GCTRL_MBI2S        (1<<2)	/* Mail box 2 Int Status */
#define TSI148_GCSR_GCTRL_MBI1S        (1<<1)	/* Mail box 1 Int Status */
#define TSI148_GCSR_GCTRL_MBI0S        (1<<0)	/* Mail box 0 Int Status */

#define TSI148_GCSR_GAP                (1<<5)	/* Geographic Addr Parity */
#define TSI148_GCSR_GA_M               (0x1F<<0)  /* Geographic Address Mask */

/*
 *  CR/CSR Register Group
 */

/*
 *  CR/CSR Bit Clear Register CRG + $FF4
 */
#define TSI148_CRCSR_CSRBCR_LRSTC      (1<<7)	/* Local Reset Clear */
#define TSI148_CRCSR_CSRBCR_SFAILC     (1<<6)	/* System Fail Enable Clear */
#define TSI148_CRCSR_CSRBCR_BDFAILS    (1<<5)	/* Board Fail Status */
#define TSI148_CRCSR_CSRBCR_MENC       (1<<4)	/* Module Enable Clear */
#define TSI148_CRCSR_CSRBCR_BERRSC     (1<<3)	/* Bus Error Status Clear */

/*
 *  CR/CSR Bit Set Register CRG+$FF8
 */
#define TSI148_CRCSR_CSRBSR_LISTS      (1<<7)	/* Local Reset Clear */
#define TSI148_CRCSR_CSRBSR_SFAILS     (1<<6)	/* System Fail Enable Clear */
#define TSI148_CRCSR_CSRBSR_BDFAILS    (1<<5)	/* Board Fail Status */
#define TSI148_CRCSR_CSRBSR_MENS       (1<<4)	/* Module Enable Clear */
#define TSI148_CRCSR_CSRBSR_BERRS      (1<<3)	/* Bus Error Status Clear */

/*
 *  CR/CSR Base Address Register CRG + FFC
 */
#define TSI148_CRCSR_CBAR_M            (0x1F<<3)	/* Mask */

#endif				/* TSI148_H */
