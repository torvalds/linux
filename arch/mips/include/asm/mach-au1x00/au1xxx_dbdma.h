/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Include file for Alchemy Semiconductor's Au1550 Descriptor
 *	Based DMA Controller.
 *
 * Copyright 2004 Embedded Edge, LLC
 *	dan@embeddededge.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Specifics for the Au1xxx Descriptor-Based DMA Controller,
 * first seen in the AU1550 part.
 */
#ifndef _AU1000_DBDMA_H_
#define _AU1000_DBDMA_H_

#ifndef _LANGUAGE_ASSEMBLY

typedef volatile struct dbdma_global {
	u32	ddma_config;
	u32	ddma_intstat;
	u32	ddma_throttle;
	u32	ddma_inten;
} dbdma_global_t;

/* General Configuration. */
#define DDMA_CONFIG_AF		(1 << 2)
#define DDMA_CONFIG_AH		(1 << 1)
#define DDMA_CONFIG_AL		(1 << 0)

#define DDMA_THROTTLE_EN	(1 << 31)

/* The structure of a DMA Channel. */
typedef volatile struct au1xxx_dma_channel {
	u32	ddma_cfg;	/* See below */
	u32	ddma_desptr;	/* 32-byte aligned pointer to descriptor */
	u32	ddma_statptr;	/* word aligned pointer to status word */
	u32	ddma_dbell;	/* A write activates channel operation */
	u32	ddma_irq;	/* If bit 0 set, interrupt pending */
	u32	ddma_stat;	/* See below */
	u32	ddma_bytecnt;	/* Byte count, valid only when chan idle */
	/* Remainder, up to the 256 byte boundary, is reserved. */
} au1x_dma_chan_t;

#define DDMA_CFG_SED	(1 << 9)	/* source DMA level/edge detect */
#define DDMA_CFG_SP	(1 << 8)	/* source DMA polarity */
#define DDMA_CFG_DED	(1 << 7)	/* destination DMA level/edge detect */
#define DDMA_CFG_DP	(1 << 6)	/* destination DMA polarity */
#define DDMA_CFG_SYNC	(1 << 5)	/* Sync static bus controller */
#define DDMA_CFG_PPR	(1 << 4)	/* PCI posted read/write control */
#define DDMA_CFG_DFN	(1 << 3)	/* Descriptor fetch non-coherent */
#define DDMA_CFG_SBE	(1 << 2)	/* Source big endian */
#define DDMA_CFG_DBE	(1 << 1)	/* Destination big endian */
#define DDMA_CFG_EN	(1 << 0)	/* Channel enable */

/*
 * Always set when descriptor processing done, regardless of
 * interrupt enable state.  Reflected in global intstat, don't
 * clear this until global intstat is read/used.
 */
#define DDMA_IRQ_IN	(1 << 0)

#define DDMA_STAT_DB	(1 << 2)	/* Doorbell pushed */
#define DDMA_STAT_V	(1 << 1)	/* Descriptor valid */
#define DDMA_STAT_H	(1 << 0)	/* Channel Halted */

/*
 * "Standard" DDMA Descriptor.
 * Must be 32-byte aligned.
 */
typedef volatile struct au1xxx_ddma_desc {
	u32	dscr_cmd0;		/* See below */
	u32	dscr_cmd1;		/* See below */
	u32	dscr_source0;		/* source phys address */
	u32	dscr_source1;		/* See below */
	u32	dscr_dest0;		/* Destination address */
	u32	dscr_dest1;		/* See below */
	u32	dscr_stat;		/* completion status */
	u32	dscr_nxtptr;		/* Next descriptor pointer (mostly) */
	/*
	 * First 32 bytes are HW specific!!!
	 * Lets have some SW data following -- make sure it's 32 bytes.
	 */
	u32	sw_status;
	u32 	sw_context;
	u32	sw_reserved[6];
} au1x_ddma_desc_t;

#define DSCR_CMD0_V		(1 << 31)	/* Descriptor valid */
#define DSCR_CMD0_MEM		(1 << 30)	/* mem-mem transfer */
#define DSCR_CMD0_SID_MASK	(0x1f << 25)	/* Source ID */
#define DSCR_CMD0_DID_MASK	(0x1f << 20)	/* Destination ID */
#define DSCR_CMD0_SW_MASK	(0x3 << 18)	/* Source Width */
#define DSCR_CMD0_DW_MASK	(0x3 << 16)	/* Destination Width */
#define DSCR_CMD0_ARB		(0x1 << 15)	/* Set for Hi Pri */
#define DSCR_CMD0_DT_MASK	(0x3 << 13)	/* Descriptor Type */
#define DSCR_CMD0_SN		(0x1 << 12)	/* Source non-coherent */
#define DSCR_CMD0_DN		(0x1 << 11)	/* Destination non-coherent */
#define DSCR_CMD0_SM		(0x1 << 10)	/* Stride mode */
#define DSCR_CMD0_IE		(0x1 << 8)	/* Interrupt Enable */
#define DSCR_CMD0_SP		(0x1 << 4)	/* Status pointer select */
#define DSCR_CMD0_CV		(0x1 << 2)	/* Clear Valid when done */
#define DSCR_CMD0_ST_MASK	(0x3 << 0)	/* Status instruction */

#define SW_STATUS_INUSE 	(1 << 0)

/* Command 0 device IDs. */
#define AU1550_DSCR_CMD0_UART0_TX	0
#define AU1550_DSCR_CMD0_UART0_RX	1
#define AU1550_DSCR_CMD0_UART3_TX	2
#define AU1550_DSCR_CMD0_UART3_RX	3
#define AU1550_DSCR_CMD0_DMA_REQ0	4
#define AU1550_DSCR_CMD0_DMA_REQ1	5
#define AU1550_DSCR_CMD0_DMA_REQ2	6
#define AU1550_DSCR_CMD0_DMA_REQ3	7
#define AU1550_DSCR_CMD0_USBDEV_RX0	8
#define AU1550_DSCR_CMD0_USBDEV_TX0	9
#define AU1550_DSCR_CMD0_USBDEV_TX1	10
#define AU1550_DSCR_CMD0_USBDEV_TX2	11
#define AU1550_DSCR_CMD0_USBDEV_RX3	12
#define AU1550_DSCR_CMD0_USBDEV_RX4	13
#define AU1550_DSCR_CMD0_PSC0_TX	14
#define AU1550_DSCR_CMD0_PSC0_RX	15
#define AU1550_DSCR_CMD0_PSC1_TX	16
#define AU1550_DSCR_CMD0_PSC1_RX	17
#define AU1550_DSCR_CMD0_PSC2_TX	18
#define AU1550_DSCR_CMD0_PSC2_RX	19
#define AU1550_DSCR_CMD0_PSC3_TX	20
#define AU1550_DSCR_CMD0_PSC3_RX	21
#define AU1550_DSCR_CMD0_PCI_WRITE	22
#define AU1550_DSCR_CMD0_NAND_FLASH	23
#define AU1550_DSCR_CMD0_MAC0_RX	24
#define AU1550_DSCR_CMD0_MAC0_TX	25
#define AU1550_DSCR_CMD0_MAC1_RX	26
#define AU1550_DSCR_CMD0_MAC1_TX	27

#define AU1200_DSCR_CMD0_UART0_TX	0
#define AU1200_DSCR_CMD0_UART0_RX	1
#define AU1200_DSCR_CMD0_UART1_TX	2
#define AU1200_DSCR_CMD0_UART1_RX	3
#define AU1200_DSCR_CMD0_DMA_REQ0	4
#define AU1200_DSCR_CMD0_DMA_REQ1	5
#define AU1200_DSCR_CMD0_MAE_BE		6
#define AU1200_DSCR_CMD0_MAE_FE		7
#define AU1200_DSCR_CMD0_SDMS_TX0	8
#define AU1200_DSCR_CMD0_SDMS_RX0	9
#define AU1200_DSCR_CMD0_SDMS_TX1	10
#define AU1200_DSCR_CMD0_SDMS_RX1	11
#define AU1200_DSCR_CMD0_AES_TX		13
#define AU1200_DSCR_CMD0_AES_RX		12
#define AU1200_DSCR_CMD0_PSC0_TX	14
#define AU1200_DSCR_CMD0_PSC0_RX	15
#define AU1200_DSCR_CMD0_PSC1_TX	16
#define AU1200_DSCR_CMD0_PSC1_RX	17
#define AU1200_DSCR_CMD0_CIM_RXA	18
#define AU1200_DSCR_CMD0_CIM_RXB	19
#define AU1200_DSCR_CMD0_CIM_RXC	20
#define AU1200_DSCR_CMD0_MAE_BOTH	21
#define AU1200_DSCR_CMD0_LCD		22
#define AU1200_DSCR_CMD0_NAND_FLASH	23
#define AU1200_DSCR_CMD0_PSC0_SYNC	24
#define AU1200_DSCR_CMD0_PSC1_SYNC	25
#define AU1200_DSCR_CMD0_CIM_SYNC	26

#define AU1300_DSCR_CMD0_UART0_TX      0
#define AU1300_DSCR_CMD0_UART0_RX      1
#define AU1300_DSCR_CMD0_UART1_TX      2
#define AU1300_DSCR_CMD0_UART1_RX      3
#define AU1300_DSCR_CMD0_UART2_TX      4
#define AU1300_DSCR_CMD0_UART2_RX      5
#define AU1300_DSCR_CMD0_UART3_TX      6
#define AU1300_DSCR_CMD0_UART3_RX      7
#define AU1300_DSCR_CMD0_SDMS_TX0      8
#define AU1300_DSCR_CMD0_SDMS_RX0      9
#define AU1300_DSCR_CMD0_SDMS_TX1      10
#define AU1300_DSCR_CMD0_SDMS_RX1      11
#define AU1300_DSCR_CMD0_AES_TX        12
#define AU1300_DSCR_CMD0_AES_RX        13
#define AU1300_DSCR_CMD0_PSC0_TX       14
#define AU1300_DSCR_CMD0_PSC0_RX       15
#define AU1300_DSCR_CMD0_PSC1_TX       16
#define AU1300_DSCR_CMD0_PSC1_RX       17
#define AU1300_DSCR_CMD0_PSC2_TX       18
#define AU1300_DSCR_CMD0_PSC2_RX       19
#define AU1300_DSCR_CMD0_PSC3_TX       20
#define AU1300_DSCR_CMD0_PSC3_RX       21
#define AU1300_DSCR_CMD0_LCD           22
#define AU1300_DSCR_CMD0_NAND_FLASH    23
#define AU1300_DSCR_CMD0_SDMS_TX2      24
#define AU1300_DSCR_CMD0_SDMS_RX2      25
#define AU1300_DSCR_CMD0_CIM_SYNC      26
#define AU1300_DSCR_CMD0_UDMA          27
#define AU1300_DSCR_CMD0_DMA_REQ0      28
#define AU1300_DSCR_CMD0_DMA_REQ1      29

#define DSCR_CMD0_THROTTLE	30
#define DSCR_CMD0_ALWAYS	31
#define DSCR_NDEV_IDS		32
/* This macro is used to find/create custom device types */
#define DSCR_DEV2CUSTOM_ID(x, d) (((((x) & 0xFFFF) << 8) | 0x32000000) | \
				  ((d) & 0xFF))
#define DSCR_CUSTOM2DEV_ID(x)	((x) & 0xFF)

#define DSCR_CMD0_SID(x)	(((x) & 0x1f) << 25)
#define DSCR_CMD0_DID(x)	(((x) & 0x1f) << 20)

/* Source/Destination transfer width. */
#define DSCR_CMD0_BYTE		0
#define DSCR_CMD0_HALFWORD	1
#define DSCR_CMD0_WORD		2

#define DSCR_CMD0_SW(x)		(((x) & 0x3) << 18)
#define DSCR_CMD0_DW(x)		(((x) & 0x3) << 16)

/* DDMA Descriptor Type. */
#define DSCR_CMD0_STANDARD	0
#define DSCR_CMD0_LITERAL	1
#define DSCR_CMD0_CMP_BRANCH	2

#define DSCR_CMD0_DT(x)		(((x) & 0x3) << 13)

/* Status Instruction. */
#define DSCR_CMD0_ST_NOCHANGE	0	/* Don't change */
#define DSCR_CMD0_ST_CURRENT	1	/* Write current status */
#define DSCR_CMD0_ST_CMD0	2	/* Write cmd0 with V cleared */
#define DSCR_CMD0_ST_BYTECNT	3	/* Write remaining byte count */

#define DSCR_CMD0_ST(x)		(((x) & 0x3) << 0)

/* Descriptor Command 1. */
#define DSCR_CMD1_SUPTR_MASK	(0xf << 28)	/* upper 4 bits of src addr */
#define DSCR_CMD1_DUPTR_MASK	(0xf << 24)	/* upper 4 bits of dest addr */
#define DSCR_CMD1_FL_MASK	(0x3 << 22)	/* Flag bits */
#define DSCR_CMD1_BC_MASK	(0x3fffff)	/* Byte count */

/* Flag description. */
#define DSCR_CMD1_FL_MEM_STRIDE0	0
#define DSCR_CMD1_FL_MEM_STRIDE1	1
#define DSCR_CMD1_FL_MEM_STRIDE2	2

#define DSCR_CMD1_FL(x)		(((x) & 0x3) << 22)

/* Source1, 1-dimensional stride. */
#define DSCR_SRC1_STS_MASK	(3 << 30)	/* Src xfer size */
#define DSCR_SRC1_SAM_MASK	(3 << 28)	/* Src xfer movement */
#define DSCR_SRC1_SB_MASK	(0x3fff << 14)	/* Block size */
#define DSCR_SRC1_SB(x)		(((x) & 0x3fff) << 14)
#define DSCR_SRC1_SS_MASK	(0x3fff << 0)	/* Stride */
#define DSCR_SRC1_SS(x)		(((x) & 0x3fff) << 0)

/* Dest1, 1-dimensional stride. */
#define DSCR_DEST1_DTS_MASK	(3 << 30)	/* Dest xfer size */
#define DSCR_DEST1_DAM_MASK	(3 << 28)	/* Dest xfer movement */
#define DSCR_DEST1_DB_MASK	(0x3fff << 14)	/* Block size */
#define DSCR_DEST1_DB(x)	(((x) & 0x3fff) << 14)
#define DSCR_DEST1_DS_MASK	(0x3fff << 0)	/* Stride */
#define DSCR_DEST1_DS(x)	(((x) & 0x3fff) << 0)

#define DSCR_xTS_SIZE1		0
#define DSCR_xTS_SIZE2		1
#define DSCR_xTS_SIZE4		2
#define DSCR_xTS_SIZE8		3
#define DSCR_SRC1_STS(x)	(((x) & 3) << 30)
#define DSCR_DEST1_DTS(x)	(((x) & 3) << 30)

#define DSCR_xAM_INCREMENT	0
#define DSCR_xAM_DECREMENT	1
#define DSCR_xAM_STATIC		2
#define DSCR_xAM_BURST		3
#define DSCR_SRC1_SAM(x)	(((x) & 3) << 28)
#define DSCR_DEST1_DAM(x)	(((x) & 3) << 28)

/* The next descriptor pointer. */
#define DSCR_NXTPTR_MASK	(0x07ffffff)
#define DSCR_NXTPTR(x)		((x) >> 5)
#define DSCR_GET_NXTPTR(x)	((x) << 5)
#define DSCR_NXTPTR_MS		(1 << 27)

/* The number of DBDMA channels. */
#define NUM_DBDMA_CHANS	16

/*
 * DDMA API definitions
 * FIXME: may not fit to this header file
 */
typedef struct dbdma_device_table {
	u32	dev_id;
	u32	dev_flags;
	u32	dev_tsize;
	u32	dev_devwidth;
	u32	dev_physaddr;		/* If FIFO */
	u32	dev_intlevel;
	u32	dev_intpolarity;
} dbdev_tab_t;


typedef struct dbdma_chan_config {
	spinlock_t      lock;

	u32			chan_flags;
	u32			chan_index;
	dbdev_tab_t		*chan_src;
	dbdev_tab_t		*chan_dest;
	au1x_dma_chan_t		*chan_ptr;
	au1x_ddma_desc_t	*chan_desc_base;
	u32			cdb_membase; /* kmalloc base of above */
	au1x_ddma_desc_t	*get_ptr, *put_ptr, *cur_ptr;
	void			*chan_callparam;
	void			(*chan_callback)(int, void *);
} chan_tab_t;

#define DEV_FLAGS_INUSE		(1 << 0)
#define DEV_FLAGS_ANYUSE	(1 << 1)
#define DEV_FLAGS_OUT		(1 << 2)
#define DEV_FLAGS_IN		(1 << 3)
#define DEV_FLAGS_BURSTABLE	(1 << 4)
#define DEV_FLAGS_SYNC		(1 << 5)
/* end DDMA API definitions */

/*
 * External functions for drivers to use.
 * Use this to allocate a DBDMA channel.  The device IDs are one of
 * the DSCR_CMD0 devices IDs, which is usually redefined to a more
 * meaningful name.  The 'callback' is called during DMA completion
 * interrupt.
 */
extern u32 au1xxx_dbdma_chan_alloc(u32 srcid, u32 destid,
				   void (*callback)(int, void *),
				   void *callparam);

#define DBDMA_MEM_CHAN	DSCR_CMD0_ALWAYS

/* Set the device width of an in/out FIFO. */
u32 au1xxx_dbdma_set_devwidth(u32 chanid, int bits);

/* Allocate a ring of descriptors for DBDMA. */
u32 au1xxx_dbdma_ring_alloc(u32 chanid, int entries);

/* Put buffers on source/destination descriptors. */
u32 au1xxx_dbdma_put_source(u32 chanid, dma_addr_t buf, int nbytes, u32 flags);
u32 au1xxx_dbdma_put_dest(u32 chanid, dma_addr_t buf, int nbytes, u32 flags);

/* Get a buffer from the destination descriptor. */
u32 au1xxx_dbdma_get_dest(u32 chanid, void **buf, int *nbytes);

void au1xxx_dbdma_stop(u32 chanid);
void au1xxx_dbdma_start(u32 chanid);
void au1xxx_dbdma_reset(u32 chanid);
u32 au1xxx_get_dma_residue(u32 chanid);

void au1xxx_dbdma_chan_free(u32 chanid);
void au1xxx_dbdma_dump(u32 chanid);

u32 au1xxx_dbdma_put_dscr(u32 chanid, au1x_ddma_desc_t *dscr);

u32 au1xxx_ddma_add_device(dbdev_tab_t *dev);
extern void au1xxx_ddma_del_device(u32 devid);
void *au1xxx_ddma_get_nextptr_virt(au1x_ddma_desc_t *dp);

/*
 *	Flags for the put_source/put_dest functions.
 */
#define DDMA_FLAGS_IE	(1 << 0)
#define DDMA_FLAGS_NOIE (1 << 1)

#endif /* _LANGUAGE_ASSEMBLY */
#endif /* _AU1000_DBDMA_H_ */
