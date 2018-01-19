/*
 * Driver for the Atmel Extensible DMA Controller (aka XDMAC on AT91 systems)
 *
 * Copyright (C) 2014 Atmel Corporation
 *
 * Author: Ludovic Desroches <ludovic.desroches@atmel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <asm/barrier.h>
#include <dt-bindings/dma/at91.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

#include "dmaengine.h"

/* Global registers */
#define AT_XDMAC_GTYPE		0x00	/* Global Type Register */
#define		AT_XDMAC_NB_CH(i)	(((i) & 0x1F) + 1)		/* Number of Channels Minus One */
#define		AT_XDMAC_FIFO_SZ(i)	(((i) >> 5) & 0x7FF)		/* Number of Bytes */
#define		AT_XDMAC_NB_REQ(i)	((((i) >> 16) & 0x3F) + 1)	/* Number of Peripheral Requests Minus One */
#define AT_XDMAC_GCFG		0x04	/* Global Configuration Register */
#define AT_XDMAC_GWAC		0x08	/* Global Weighted Arbiter Configuration Register */
#define AT_XDMAC_GIE		0x0C	/* Global Interrupt Enable Register */
#define AT_XDMAC_GID		0x10	/* Global Interrupt Disable Register */
#define AT_XDMAC_GIM		0x14	/* Global Interrupt Mask Register */
#define AT_XDMAC_GIS		0x18	/* Global Interrupt Status Register */
#define AT_XDMAC_GE		0x1C	/* Global Channel Enable Register */
#define AT_XDMAC_GD		0x20	/* Global Channel Disable Register */
#define AT_XDMAC_GS		0x24	/* Global Channel Status Register */
#define AT_XDMAC_GRS		0x28	/* Global Channel Read Suspend Register */
#define AT_XDMAC_GWS		0x2C	/* Global Write Suspend Register */
#define AT_XDMAC_GRWS		0x30	/* Global Channel Read Write Suspend Register */
#define AT_XDMAC_GRWR		0x34	/* Global Channel Read Write Resume Register */
#define AT_XDMAC_GSWR		0x38	/* Global Channel Software Request Register */
#define AT_XDMAC_GSWS		0x3C	/* Global channel Software Request Status Register */
#define AT_XDMAC_GSWF		0x40	/* Global Channel Software Flush Request Register */
#define AT_XDMAC_VERSION	0xFFC	/* XDMAC Version Register */

/* Channel relative registers offsets */
#define AT_XDMAC_CIE		0x00	/* Channel Interrupt Enable Register */
#define		AT_XDMAC_CIE_BIE	BIT(0)	/* End of Block Interrupt Enable Bit */
#define		AT_XDMAC_CIE_LIE	BIT(1)	/* End of Linked List Interrupt Enable Bit */
#define		AT_XDMAC_CIE_DIE	BIT(2)	/* End of Disable Interrupt Enable Bit */
#define		AT_XDMAC_CIE_FIE	BIT(3)	/* End of Flush Interrupt Enable Bit */
#define		AT_XDMAC_CIE_RBEIE	BIT(4)	/* Read Bus Error Interrupt Enable Bit */
#define		AT_XDMAC_CIE_WBEIE	BIT(5)	/* Write Bus Error Interrupt Enable Bit */
#define		AT_XDMAC_CIE_ROIE	BIT(6)	/* Request Overflow Interrupt Enable Bit */
#define AT_XDMAC_CID		0x04	/* Channel Interrupt Disable Register */
#define		AT_XDMAC_CID_BID	BIT(0)	/* End of Block Interrupt Disable Bit */
#define		AT_XDMAC_CID_LID	BIT(1)	/* End of Linked List Interrupt Disable Bit */
#define		AT_XDMAC_CID_DID	BIT(2)	/* End of Disable Interrupt Disable Bit */
#define		AT_XDMAC_CID_FID	BIT(3)	/* End of Flush Interrupt Disable Bit */
#define		AT_XDMAC_CID_RBEID	BIT(4)	/* Read Bus Error Interrupt Disable Bit */
#define		AT_XDMAC_CID_WBEID	BIT(5)	/* Write Bus Error Interrupt Disable Bit */
#define		AT_XDMAC_CID_ROID	BIT(6)	/* Request Overflow Interrupt Disable Bit */
#define AT_XDMAC_CIM		0x08	/* Channel Interrupt Mask Register */
#define		AT_XDMAC_CIM_BIM	BIT(0)	/* End of Block Interrupt Mask Bit */
#define		AT_XDMAC_CIM_LIM	BIT(1)	/* End of Linked List Interrupt Mask Bit */
#define		AT_XDMAC_CIM_DIM	BIT(2)	/* End of Disable Interrupt Mask Bit */
#define		AT_XDMAC_CIM_FIM	BIT(3)	/* End of Flush Interrupt Mask Bit */
#define		AT_XDMAC_CIM_RBEIM	BIT(4)	/* Read Bus Error Interrupt Mask Bit */
#define		AT_XDMAC_CIM_WBEIM	BIT(5)	/* Write Bus Error Interrupt Mask Bit */
#define		AT_XDMAC_CIM_ROIM	BIT(6)	/* Request Overflow Interrupt Mask Bit */
#define AT_XDMAC_CIS		0x0C	/* Channel Interrupt Status Register */
#define		AT_XDMAC_CIS_BIS	BIT(0)	/* End of Block Interrupt Status Bit */
#define		AT_XDMAC_CIS_LIS	BIT(1)	/* End of Linked List Interrupt Status Bit */
#define		AT_XDMAC_CIS_DIS	BIT(2)	/* End of Disable Interrupt Status Bit */
#define		AT_XDMAC_CIS_FIS	BIT(3)	/* End of Flush Interrupt Status Bit */
#define		AT_XDMAC_CIS_RBEIS	BIT(4)	/* Read Bus Error Interrupt Status Bit */
#define		AT_XDMAC_CIS_WBEIS	BIT(5)	/* Write Bus Error Interrupt Status Bit */
#define		AT_XDMAC_CIS_ROIS	BIT(6)	/* Request Overflow Interrupt Status Bit */
#define AT_XDMAC_CSA		0x10	/* Channel Source Address Register */
#define AT_XDMAC_CDA		0x14	/* Channel Destination Address Register */
#define AT_XDMAC_CNDA		0x18	/* Channel Next Descriptor Address Register */
#define		AT_XDMAC_CNDA_NDAIF(i)	((i) & 0x1)			/* Channel x Next Descriptor Interface */
#define		AT_XDMAC_CNDA_NDA(i)	((i) & 0xfffffffc)		/* Channel x Next Descriptor Address */
#define AT_XDMAC_CNDC		0x1C	/* Channel Next Descriptor Control Register */
#define		AT_XDMAC_CNDC_NDE		(0x1 << 0)		/* Channel x Next Descriptor Enable */
#define		AT_XDMAC_CNDC_NDSUP		(0x1 << 1)		/* Channel x Next Descriptor Source Update */
#define		AT_XDMAC_CNDC_NDDUP		(0x1 << 2)		/* Channel x Next Descriptor Destination Update */
#define		AT_XDMAC_CNDC_NDVIEW_NDV0	(0x0 << 3)		/* Channel x Next Descriptor View 0 */
#define		AT_XDMAC_CNDC_NDVIEW_NDV1	(0x1 << 3)		/* Channel x Next Descriptor View 1 */
#define		AT_XDMAC_CNDC_NDVIEW_NDV2	(0x2 << 3)		/* Channel x Next Descriptor View 2 */
#define		AT_XDMAC_CNDC_NDVIEW_NDV3	(0x3 << 3)		/* Channel x Next Descriptor View 3 */
#define AT_XDMAC_CUBC		0x20	/* Channel Microblock Control Register */
#define AT_XDMAC_CBC		0x24	/* Channel Block Control Register */
#define AT_XDMAC_CC		0x28	/* Channel Configuration Register */
#define		AT_XDMAC_CC_TYPE	(0x1 << 0)	/* Channel Transfer Type */
#define			AT_XDMAC_CC_TYPE_MEM_TRAN	(0x0 << 0)	/* Memory to Memory Transfer */
#define			AT_XDMAC_CC_TYPE_PER_TRAN	(0x1 << 0)	/* Peripheral to Memory or Memory to Peripheral Transfer */
#define		AT_XDMAC_CC_MBSIZE_MASK	(0x3 << 1)
#define			AT_XDMAC_CC_MBSIZE_SINGLE	(0x0 << 1)
#define			AT_XDMAC_CC_MBSIZE_FOUR		(0x1 << 1)
#define			AT_XDMAC_CC_MBSIZE_EIGHT	(0x2 << 1)
#define			AT_XDMAC_CC_MBSIZE_SIXTEEN	(0x3 << 1)
#define		AT_XDMAC_CC_DSYNC	(0x1 << 4)	/* Channel Synchronization */
#define			AT_XDMAC_CC_DSYNC_PER2MEM	(0x0 << 4)
#define			AT_XDMAC_CC_DSYNC_MEM2PER	(0x1 << 4)
#define		AT_XDMAC_CC_PROT	(0x1 << 5)	/* Channel Protection */
#define			AT_XDMAC_CC_PROT_SEC		(0x0 << 5)
#define			AT_XDMAC_CC_PROT_UNSEC		(0x1 << 5)
#define		AT_XDMAC_CC_SWREQ	(0x1 << 6)	/* Channel Software Request Trigger */
#define			AT_XDMAC_CC_SWREQ_HWR_CONNECTED	(0x0 << 6)
#define			AT_XDMAC_CC_SWREQ_SWR_CONNECTED	(0x1 << 6)
#define		AT_XDMAC_CC_MEMSET	(0x1 << 7)	/* Channel Fill Block of memory */
#define			AT_XDMAC_CC_MEMSET_NORMAL_MODE	(0x0 << 7)
#define			AT_XDMAC_CC_MEMSET_HW_MODE	(0x1 << 7)
#define		AT_XDMAC_CC_CSIZE(i)	((0x7 & (i)) << 8)	/* Channel Chunk Size */
#define		AT_XDMAC_CC_DWIDTH_OFFSET	11
#define		AT_XDMAC_CC_DWIDTH_MASK	(0x3 << AT_XDMAC_CC_DWIDTH_OFFSET)
#define		AT_XDMAC_CC_DWIDTH(i)	((0x3 & (i)) << AT_XDMAC_CC_DWIDTH_OFFSET)	/* Channel Data Width */
#define			AT_XDMAC_CC_DWIDTH_BYTE		0x0
#define			AT_XDMAC_CC_DWIDTH_HALFWORD	0x1
#define			AT_XDMAC_CC_DWIDTH_WORD		0x2
#define			AT_XDMAC_CC_DWIDTH_DWORD	0x3
#define		AT_XDMAC_CC_SIF(i)	((0x1 & (i)) << 13)	/* Channel Source Interface Identifier */
#define		AT_XDMAC_CC_DIF(i)	((0x1 & (i)) << 14)	/* Channel Destination Interface Identifier */
#define		AT_XDMAC_CC_SAM_MASK	(0x3 << 16)	/* Channel Source Addressing Mode */
#define			AT_XDMAC_CC_SAM_FIXED_AM	(0x0 << 16)
#define			AT_XDMAC_CC_SAM_INCREMENTED_AM	(0x1 << 16)
#define			AT_XDMAC_CC_SAM_UBS_AM		(0x2 << 16)
#define			AT_XDMAC_CC_SAM_UBS_DS_AM	(0x3 << 16)
#define		AT_XDMAC_CC_DAM_MASK	(0x3 << 18)	/* Channel Source Addressing Mode */
#define			AT_XDMAC_CC_DAM_FIXED_AM	(0x0 << 18)
#define			AT_XDMAC_CC_DAM_INCREMENTED_AM	(0x1 << 18)
#define			AT_XDMAC_CC_DAM_UBS_AM		(0x2 << 18)
#define			AT_XDMAC_CC_DAM_UBS_DS_AM	(0x3 << 18)
#define		AT_XDMAC_CC_INITD	(0x1 << 21)	/* Channel Initialization Terminated (read only) */
#define			AT_XDMAC_CC_INITD_TERMINATED	(0x0 << 21)
#define			AT_XDMAC_CC_INITD_IN_PROGRESS	(0x1 << 21)
#define		AT_XDMAC_CC_RDIP	(0x1 << 22)	/* Read in Progress (read only) */
#define			AT_XDMAC_CC_RDIP_DONE		(0x0 << 22)
#define			AT_XDMAC_CC_RDIP_IN_PROGRESS	(0x1 << 22)
#define		AT_XDMAC_CC_WRIP	(0x1 << 23)	/* Write in Progress (read only) */
#define			AT_XDMAC_CC_WRIP_DONE		(0x0 << 23)
#define			AT_XDMAC_CC_WRIP_IN_PROGRESS	(0x1 << 23)
#define		AT_XDMAC_CC_PERID(i)	(0x7f & (i) << 24)	/* Channel Peripheral Identifier */
#define AT_XDMAC_CDS_MSP	0x2C	/* Channel Data Stride Memory Set Pattern */
#define AT_XDMAC_CSUS		0x30	/* Channel Source Microblock Stride */
#define AT_XDMAC_CDUS		0x34	/* Channel Destination Microblock Stride */

#define AT_XDMAC_CHAN_REG_BASE	0x50	/* Channel registers base address */

/* Microblock control members */
#define AT_XDMAC_MBR_UBC_UBLEN_MAX	0xFFFFFFUL	/* Maximum Microblock Length */
#define AT_XDMAC_MBR_UBC_NDE		(0x1 << 24)	/* Next Descriptor Enable */
#define AT_XDMAC_MBR_UBC_NSEN		(0x1 << 25)	/* Next Descriptor Source Update */
#define AT_XDMAC_MBR_UBC_NDEN		(0x1 << 26)	/* Next Descriptor Destination Update */
#define AT_XDMAC_MBR_UBC_NDV0		(0x0 << 27)	/* Next Descriptor View 0 */
#define AT_XDMAC_MBR_UBC_NDV1		(0x1 << 27)	/* Next Descriptor View 1 */
#define AT_XDMAC_MBR_UBC_NDV2		(0x2 << 27)	/* Next Descriptor View 2 */
#define AT_XDMAC_MBR_UBC_NDV3		(0x3 << 27)	/* Next Descriptor View 3 */

#define AT_XDMAC_MAX_CHAN	0x20
#define AT_XDMAC_MAX_CSIZE	16	/* 16 data */
#define AT_XDMAC_MAX_DWIDTH	8	/* 64 bits */
#define AT_XDMAC_RESIDUE_MAX_RETRIES	5

#define AT_XDMAC_DMA_BUSWIDTHS\
	(BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED) |\
	BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |\
	BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |\
	BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |\
	BIT(DMA_SLAVE_BUSWIDTH_8_BYTES))

enum atc_status {
	AT_XDMAC_CHAN_IS_CYCLIC = 0,
	AT_XDMAC_CHAN_IS_PAUSED,
};

/* ----- Channels ----- */
struct at_xdmac_chan {
	struct dma_chan			chan;
	void __iomem			*ch_regs;
	u32				mask;		/* Channel Mask */
	u32				cfg;		/* Channel Configuration Register */
	u8				perid;		/* Peripheral ID */
	u8				perif;		/* Peripheral Interface */
	u8				memif;		/* Memory Interface */
	u32				save_cc;
	u32				save_cim;
	u32				save_cnda;
	u32				save_cndc;
	unsigned long			status;
	struct tasklet_struct		tasklet;
	struct dma_slave_config		sconfig;

	spinlock_t			lock;

	struct list_head		xfers_list;
	struct list_head		free_descs_list;
};


/* ----- Controller ----- */
struct at_xdmac {
	struct dma_device	dma;
	void __iomem		*regs;
	int			irq;
	struct clk		*clk;
	u32			save_gim;
	struct dma_pool		*at_xdmac_desc_pool;
	struct at_xdmac_chan	chan[0];
};


/* ----- Descriptors ----- */

/* Linked List Descriptor */
struct at_xdmac_lld {
	dma_addr_t	mbr_nda;	/* Next Descriptor Member */
	u32		mbr_ubc;	/* Microblock Control Member */
	dma_addr_t	mbr_sa;		/* Source Address Member */
	dma_addr_t	mbr_da;		/* Destination Address Member */
	u32		mbr_cfg;	/* Configuration Register */
	u32		mbr_bc;		/* Block Control Register */
	u32		mbr_ds;		/* Data Stride Register */
	u32		mbr_sus;	/* Source Microblock Stride Register */
	u32		mbr_dus;	/* Destination Microblock Stride Register */
};

/* 64-bit alignment needed to update CNDA and CUBC registers in an atomic way. */
struct at_xdmac_desc {
	struct at_xdmac_lld		lld;
	enum dma_transfer_direction	direction;
	struct dma_async_tx_descriptor	tx_dma_desc;
	struct list_head		desc_node;
	/* Following members are only used by the first descriptor */
	bool				active_xfer;
	unsigned int			xfer_size;
	struct list_head		descs_list;
	struct list_head		xfer_node;
} __aligned(sizeof(u64));

static inline void __iomem *at_xdmac_chan_reg_base(struct at_xdmac *atxdmac, unsigned int chan_nb)
{
	return atxdmac->regs + (AT_XDMAC_CHAN_REG_BASE + chan_nb * 0x40);
}

#define at_xdmac_read(atxdmac, reg) readl_relaxed((atxdmac)->regs + (reg))
#define at_xdmac_write(atxdmac, reg, value) \
	writel_relaxed((value), (atxdmac)->regs + (reg))

#define at_xdmac_chan_read(atchan, reg) readl_relaxed((atchan)->ch_regs + (reg))
#define at_xdmac_chan_write(atchan, reg, value) writel_relaxed((value), (atchan)->ch_regs + (reg))

static inline struct at_xdmac_chan *to_at_xdmac_chan(struct dma_chan *dchan)
{
	return container_of(dchan, struct at_xdmac_chan, chan);
}

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}

static inline struct at_xdmac *to_at_xdmac(struct dma_device *ddev)
{
	return container_of(ddev, struct at_xdmac, dma);
}

static inline struct at_xdmac_desc *txd_to_at_desc(struct dma_async_tx_descriptor *txd)
{
	return container_of(txd, struct at_xdmac_desc, tx_dma_desc);
}

static inline int at_xdmac_chan_is_cyclic(struct at_xdmac_chan *atchan)
{
	return test_bit(AT_XDMAC_CHAN_IS_CYCLIC, &atchan->status);
}

static inline int at_xdmac_chan_is_paused(struct at_xdmac_chan *atchan)
{
	return test_bit(AT_XDMAC_CHAN_IS_PAUSED, &atchan->status);
}

static inline int at_xdmac_csize(u32 maxburst)
{
	int csize;

	csize = ffs(maxburst) - 1;
	if (csize > 4)
		csize = -EINVAL;

	return csize;
};

static inline u8 at_xdmac_get_dwidth(u32 cfg)
{
	return (cfg & AT_XDMAC_CC_DWIDTH_MASK) >> AT_XDMAC_CC_DWIDTH_OFFSET;
};

static unsigned int init_nr_desc_per_channel = 64;
module_param(init_nr_desc_per_channel, uint, 0644);
MODULE_PARM_DESC(init_nr_desc_per_channel,
		 "initial descriptors per channel (default: 64)");


static bool at_xdmac_chan_is_enabled(struct at_xdmac_chan *atchan)
{
	return at_xdmac_chan_read(atchan, AT_XDMAC_GS) & atchan->mask;
}

static void at_xdmac_off(struct at_xdmac *atxdmac)
{
	at_xdmac_write(atxdmac, AT_XDMAC_GD, -1L);

	/* Wait that all chans are disabled. */
	while (at_xdmac_read(atxdmac, AT_XDMAC_GS))
		cpu_relax();

	at_xdmac_write(atxdmac, AT_XDMAC_GID, -1L);
}

/* Call with lock hold. */
static void at_xdmac_start_xfer(struct at_xdmac_chan *atchan,
				struct at_xdmac_desc *first)
{
	struct at_xdmac	*atxdmac = to_at_xdmac(atchan->chan.device);
	u32		reg;

	dev_vdbg(chan2dev(&atchan->chan), "%s: desc 0x%p\n", __func__, first);

	if (at_xdmac_chan_is_enabled(atchan))
		return;

	/* Set transfer as active to not try to start it again. */
	first->active_xfer = true;

	/* Tell xdmac where to get the first descriptor. */
	reg = AT_XDMAC_CNDA_NDA(first->tx_dma_desc.phys)
	      | AT_XDMAC_CNDA_NDAIF(atchan->memif);
	at_xdmac_chan_write(atchan, AT_XDMAC_CNDA, reg);

	/*
	 * When doing non cyclic transfer we need to use the next
	 * descriptor view 2 since some fields of the configuration register
	 * depend on transfer size and src/dest addresses.
	 */
	if (at_xdmac_chan_is_cyclic(atchan))
		reg = AT_XDMAC_CNDC_NDVIEW_NDV1;
	else if (first->lld.mbr_ubc & AT_XDMAC_MBR_UBC_NDV3)
		reg = AT_XDMAC_CNDC_NDVIEW_NDV3;
	else
		reg = AT_XDMAC_CNDC_NDVIEW_NDV2;
	/*
	 * Even if the register will be updated from the configuration in the
	 * descriptor when using view 2 or higher, the PROT bit won't be set
	 * properly. This bit can be modified only by using the channel
	 * configuration register.
	 */
	at_xdmac_chan_write(atchan, AT_XDMAC_CC, first->lld.mbr_cfg);

	reg |= AT_XDMAC_CNDC_NDDUP
	       | AT_XDMAC_CNDC_NDSUP
	       | AT_XDMAC_CNDC_NDE;
	at_xdmac_chan_write(atchan, AT_XDMAC_CNDC, reg);

	dev_vdbg(chan2dev(&atchan->chan),
		 "%s: CC=0x%08x CNDA=0x%08x, CNDC=0x%08x, CSA=0x%08x, CDA=0x%08x, CUBC=0x%08x\n",
		 __func__, at_xdmac_chan_read(atchan, AT_XDMAC_CC),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CNDA),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CNDC),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CSA),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CDA),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CUBC));

	at_xdmac_chan_write(atchan, AT_XDMAC_CID, 0xffffffff);
	reg = AT_XDMAC_CIE_RBEIE | AT_XDMAC_CIE_WBEIE | AT_XDMAC_CIE_ROIE;
	/*
	 * There is no end of list when doing cyclic dma, we need to get
	 * an interrupt after each periods.
	 */
	if (at_xdmac_chan_is_cyclic(atchan))
		at_xdmac_chan_write(atchan, AT_XDMAC_CIE,
				    reg | AT_XDMAC_CIE_BIE);
	else
		at_xdmac_chan_write(atchan, AT_XDMAC_CIE,
				    reg | AT_XDMAC_CIE_LIE);
	at_xdmac_write(atxdmac, AT_XDMAC_GIE, atchan->mask);
	dev_vdbg(chan2dev(&atchan->chan),
		 "%s: enable channel (0x%08x)\n", __func__, atchan->mask);
	wmb();
	at_xdmac_write(atxdmac, AT_XDMAC_GE, atchan->mask);

	dev_vdbg(chan2dev(&atchan->chan),
		 "%s: CC=0x%08x CNDA=0x%08x, CNDC=0x%08x, CSA=0x%08x, CDA=0x%08x, CUBC=0x%08x\n",
		 __func__, at_xdmac_chan_read(atchan, AT_XDMAC_CC),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CNDA),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CNDC),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CSA),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CDA),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CUBC));

}

static dma_cookie_t at_xdmac_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct at_xdmac_desc	*desc = txd_to_at_desc(tx);
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(tx->chan);
	dma_cookie_t		cookie;
	unsigned long		irqflags;

	spin_lock_irqsave(&atchan->lock, irqflags);
	cookie = dma_cookie_assign(tx);

	dev_vdbg(chan2dev(tx->chan), "%s: atchan 0x%p, add desc 0x%p to xfers_list\n",
		 __func__, atchan, desc);
	list_add_tail(&desc->xfer_node, &atchan->xfers_list);
	if (list_is_singular(&atchan->xfers_list))
		at_xdmac_start_xfer(atchan, desc);

	spin_unlock_irqrestore(&atchan->lock, irqflags);
	return cookie;
}

static struct at_xdmac_desc *at_xdmac_alloc_desc(struct dma_chan *chan,
						 gfp_t gfp_flags)
{
	struct at_xdmac_desc	*desc;
	struct at_xdmac		*atxdmac = to_at_xdmac(chan->device);
	dma_addr_t		phys;

	desc = dma_pool_zalloc(atxdmac->at_xdmac_desc_pool, gfp_flags, &phys);
	if (desc) {
		INIT_LIST_HEAD(&desc->descs_list);
		dma_async_tx_descriptor_init(&desc->tx_dma_desc, chan);
		desc->tx_dma_desc.tx_submit = at_xdmac_tx_submit;
		desc->tx_dma_desc.phys = phys;
	}

	return desc;
}

static void at_xdmac_init_used_desc(struct at_xdmac_desc *desc)
{
	memset(&desc->lld, 0, sizeof(desc->lld));
	INIT_LIST_HEAD(&desc->descs_list);
	desc->direction = DMA_TRANS_NONE;
	desc->xfer_size = 0;
	desc->active_xfer = false;
}

/* Call must be protected by lock. */
static struct at_xdmac_desc *at_xdmac_get_desc(struct at_xdmac_chan *atchan)
{
	struct at_xdmac_desc *desc;

	if (list_empty(&atchan->free_descs_list)) {
		desc = at_xdmac_alloc_desc(&atchan->chan, GFP_NOWAIT);
	} else {
		desc = list_first_entry(&atchan->free_descs_list,
					struct at_xdmac_desc, desc_node);
		list_del(&desc->desc_node);
		at_xdmac_init_used_desc(desc);
	}

	return desc;
}

static void at_xdmac_queue_desc(struct dma_chan *chan,
				struct at_xdmac_desc *prev,
				struct at_xdmac_desc *desc)
{
	if (!prev || !desc)
		return;

	prev->lld.mbr_nda = desc->tx_dma_desc.phys;
	prev->lld.mbr_ubc |= AT_XDMAC_MBR_UBC_NDE;

	dev_dbg(chan2dev(chan),	"%s: chain lld: prev=0x%p, mbr_nda=%pad\n",
		__func__, prev, &prev->lld.mbr_nda);
}

static inline void at_xdmac_increment_block_count(struct dma_chan *chan,
						  struct at_xdmac_desc *desc)
{
	if (!desc)
		return;

	desc->lld.mbr_bc++;

	dev_dbg(chan2dev(chan),
		"%s: incrementing the block count of the desc 0x%p\n",
		__func__, desc);
}

static struct dma_chan *at_xdmac_xlate(struct of_phandle_args *dma_spec,
				       struct of_dma *of_dma)
{
	struct at_xdmac		*atxdmac = of_dma->of_dma_data;
	struct at_xdmac_chan	*atchan;
	struct dma_chan		*chan;
	struct device		*dev = atxdmac->dma.dev;

	if (dma_spec->args_count != 1) {
		dev_err(dev, "dma phandler args: bad number of args\n");
		return NULL;
	}

	chan = dma_get_any_slave_channel(&atxdmac->dma);
	if (!chan) {
		dev_err(dev, "can't get a dma channel\n");
		return NULL;
	}

	atchan = to_at_xdmac_chan(chan);
	atchan->memif = AT91_XDMAC_DT_GET_MEM_IF(dma_spec->args[0]);
	atchan->perif = AT91_XDMAC_DT_GET_PER_IF(dma_spec->args[0]);
	atchan->perid = AT91_XDMAC_DT_GET_PERID(dma_spec->args[0]);
	dev_dbg(dev, "chan dt cfg: memif=%u perif=%u perid=%u\n",
		 atchan->memif, atchan->perif, atchan->perid);

	return chan;
}

static int at_xdmac_compute_chan_conf(struct dma_chan *chan,
				      enum dma_transfer_direction direction)
{
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	int			csize, dwidth;

	if (direction == DMA_DEV_TO_MEM) {
		atchan->cfg =
			AT91_XDMAC_DT_PERID(atchan->perid)
			| AT_XDMAC_CC_DAM_INCREMENTED_AM
			| AT_XDMAC_CC_SAM_FIXED_AM
			| AT_XDMAC_CC_DIF(atchan->memif)
			| AT_XDMAC_CC_SIF(atchan->perif)
			| AT_XDMAC_CC_SWREQ_HWR_CONNECTED
			| AT_XDMAC_CC_DSYNC_PER2MEM
			| AT_XDMAC_CC_MBSIZE_SIXTEEN
			| AT_XDMAC_CC_TYPE_PER_TRAN;
		csize = ffs(atchan->sconfig.src_maxburst) - 1;
		if (csize < 0) {
			dev_err(chan2dev(chan), "invalid src maxburst value\n");
			return -EINVAL;
		}
		atchan->cfg |= AT_XDMAC_CC_CSIZE(csize);
		dwidth = ffs(atchan->sconfig.src_addr_width) - 1;
		if (dwidth < 0) {
			dev_err(chan2dev(chan), "invalid src addr width value\n");
			return -EINVAL;
		}
		atchan->cfg |= AT_XDMAC_CC_DWIDTH(dwidth);
	} else if (direction == DMA_MEM_TO_DEV) {
		atchan->cfg =
			AT91_XDMAC_DT_PERID(atchan->perid)
			| AT_XDMAC_CC_DAM_FIXED_AM
			| AT_XDMAC_CC_SAM_INCREMENTED_AM
			| AT_XDMAC_CC_DIF(atchan->perif)
			| AT_XDMAC_CC_SIF(atchan->memif)
			| AT_XDMAC_CC_SWREQ_HWR_CONNECTED
			| AT_XDMAC_CC_DSYNC_MEM2PER
			| AT_XDMAC_CC_MBSIZE_SIXTEEN
			| AT_XDMAC_CC_TYPE_PER_TRAN;
		csize = ffs(atchan->sconfig.dst_maxburst) - 1;
		if (csize < 0) {
			dev_err(chan2dev(chan), "invalid src maxburst value\n");
			return -EINVAL;
		}
		atchan->cfg |= AT_XDMAC_CC_CSIZE(csize);
		dwidth = ffs(atchan->sconfig.dst_addr_width) - 1;
		if (dwidth < 0) {
			dev_err(chan2dev(chan), "invalid dst addr width value\n");
			return -EINVAL;
		}
		atchan->cfg |= AT_XDMAC_CC_DWIDTH(dwidth);
	}

	dev_dbg(chan2dev(chan),	"%s: cfg=0x%08x\n", __func__, atchan->cfg);

	return 0;
}

/*
 * Only check that maxburst and addr width values are supported by the
 * the controller but not that the configuration is good to perform the
 * transfer since we don't know the direction at this stage.
 */
static int at_xdmac_check_slave_config(struct dma_slave_config *sconfig)
{
	if ((sconfig->src_maxburst > AT_XDMAC_MAX_CSIZE)
	    || (sconfig->dst_maxburst > AT_XDMAC_MAX_CSIZE))
		return -EINVAL;

	if ((sconfig->src_addr_width > AT_XDMAC_MAX_DWIDTH)
	    || (sconfig->dst_addr_width > AT_XDMAC_MAX_DWIDTH))
		return -EINVAL;

	return 0;
}

static int at_xdmac_set_slave_config(struct dma_chan *chan,
				      struct dma_slave_config *sconfig)
{
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);

	if (at_xdmac_check_slave_config(sconfig)) {
		dev_err(chan2dev(chan), "invalid slave configuration\n");
		return -EINVAL;
	}

	memcpy(&atchan->sconfig, sconfig, sizeof(atchan->sconfig));

	return 0;
}

static struct dma_async_tx_descriptor *
at_xdmac_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		       unsigned int sg_len, enum dma_transfer_direction direction,
		       unsigned long flags, void *context)
{
	struct at_xdmac_chan		*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac_desc		*first = NULL, *prev = NULL;
	struct scatterlist		*sg;
	int				i;
	unsigned int			xfer_size = 0;
	unsigned long			irqflags;
	struct dma_async_tx_descriptor	*ret = NULL;

	if (!sgl)
		return NULL;

	if (!is_slave_direction(direction)) {
		dev_err(chan2dev(chan), "invalid DMA direction\n");
		return NULL;
	}

	dev_dbg(chan2dev(chan), "%s: sg_len=%d, dir=%s, flags=0x%lx\n",
		 __func__, sg_len,
		 direction == DMA_MEM_TO_DEV ? "to device" : "from device",
		 flags);

	/* Protect dma_sconfig field that can be modified by set_slave_conf. */
	spin_lock_irqsave(&atchan->lock, irqflags);

	if (at_xdmac_compute_chan_conf(chan, direction))
		goto spin_unlock;

	/* Prepare descriptors. */
	for_each_sg(sgl, sg, sg_len, i) {
		struct at_xdmac_desc	*desc = NULL;
		u32			len, mem, dwidth, fixed_dwidth;

		len = sg_dma_len(sg);
		mem = sg_dma_address(sg);
		if (unlikely(!len)) {
			dev_err(chan2dev(chan), "sg data length is zero\n");
			goto spin_unlock;
		}
		dev_dbg(chan2dev(chan), "%s: * sg%d len=%u, mem=0x%08x\n",
			 __func__, i, len, mem);

		desc = at_xdmac_get_desc(atchan);
		if (!desc) {
			dev_err(chan2dev(chan), "can't get descriptor\n");
			if (first)
				list_splice_init(&first->descs_list, &atchan->free_descs_list);
			goto spin_unlock;
		}

		/* Linked list descriptor setup. */
		if (direction == DMA_DEV_TO_MEM) {
			desc->lld.mbr_sa = atchan->sconfig.src_addr;
			desc->lld.mbr_da = mem;
		} else {
			desc->lld.mbr_sa = mem;
			desc->lld.mbr_da = atchan->sconfig.dst_addr;
		}
		dwidth = at_xdmac_get_dwidth(atchan->cfg);
		fixed_dwidth = IS_ALIGNED(len, 1 << dwidth)
			       ? dwidth
			       : AT_XDMAC_CC_DWIDTH_BYTE;
		desc->lld.mbr_ubc = AT_XDMAC_MBR_UBC_NDV2			/* next descriptor view */
			| AT_XDMAC_MBR_UBC_NDEN					/* next descriptor dst parameter update */
			| AT_XDMAC_MBR_UBC_NSEN					/* next descriptor src parameter update */
			| (len >> fixed_dwidth);				/* microblock length */
		desc->lld.mbr_cfg = (atchan->cfg & ~AT_XDMAC_CC_DWIDTH_MASK) |
				    AT_XDMAC_CC_DWIDTH(fixed_dwidth);
		dev_dbg(chan2dev(chan),
			 "%s: lld: mbr_sa=%pad, mbr_da=%pad, mbr_ubc=0x%08x\n",
			 __func__, &desc->lld.mbr_sa, &desc->lld.mbr_da, desc->lld.mbr_ubc);

		/* Chain lld. */
		if (prev)
			at_xdmac_queue_desc(chan, prev, desc);

		prev = desc;
		if (!first)
			first = desc;

		dev_dbg(chan2dev(chan), "%s: add desc 0x%p to descs_list 0x%p\n",
			 __func__, desc, first);
		list_add_tail(&desc->desc_node, &first->descs_list);
		xfer_size += len;
	}


	first->tx_dma_desc.flags = flags;
	first->xfer_size = xfer_size;
	first->direction = direction;
	ret = &first->tx_dma_desc;

spin_unlock:
	spin_unlock_irqrestore(&atchan->lock, irqflags);
	return ret;
}

static struct dma_async_tx_descriptor *
at_xdmac_prep_dma_cyclic(struct dma_chan *chan, dma_addr_t buf_addr,
			 size_t buf_len, size_t period_len,
			 enum dma_transfer_direction direction,
			 unsigned long flags)
{
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac_desc	*first = NULL, *prev = NULL;
	unsigned int		periods = buf_len / period_len;
	int			i;
	unsigned long		irqflags;

	dev_dbg(chan2dev(chan), "%s: buf_addr=%pad, buf_len=%zd, period_len=%zd, dir=%s, flags=0x%lx\n",
		__func__, &buf_addr, buf_len, period_len,
		direction == DMA_MEM_TO_DEV ? "mem2per" : "per2mem", flags);

	if (!is_slave_direction(direction)) {
		dev_err(chan2dev(chan), "invalid DMA direction\n");
		return NULL;
	}

	if (test_and_set_bit(AT_XDMAC_CHAN_IS_CYCLIC, &atchan->status)) {
		dev_err(chan2dev(chan), "channel currently used\n");
		return NULL;
	}

	if (at_xdmac_compute_chan_conf(chan, direction))
		return NULL;

	for (i = 0; i < periods; i++) {
		struct at_xdmac_desc	*desc = NULL;

		spin_lock_irqsave(&atchan->lock, irqflags);
		desc = at_xdmac_get_desc(atchan);
		if (!desc) {
			dev_err(chan2dev(chan), "can't get descriptor\n");
			if (first)
				list_splice_init(&first->descs_list, &atchan->free_descs_list);
			spin_unlock_irqrestore(&atchan->lock, irqflags);
			return NULL;
		}
		spin_unlock_irqrestore(&atchan->lock, irqflags);
		dev_dbg(chan2dev(chan),
			"%s: desc=0x%p, tx_dma_desc.phys=%pad\n",
			__func__, desc, &desc->tx_dma_desc.phys);

		if (direction == DMA_DEV_TO_MEM) {
			desc->lld.mbr_sa = atchan->sconfig.src_addr;
			desc->lld.mbr_da = buf_addr + i * period_len;
		} else {
			desc->lld.mbr_sa = buf_addr + i * period_len;
			desc->lld.mbr_da = atchan->sconfig.dst_addr;
		}
		desc->lld.mbr_cfg = atchan->cfg;
		desc->lld.mbr_ubc = AT_XDMAC_MBR_UBC_NDV1
			| AT_XDMAC_MBR_UBC_NDEN
			| AT_XDMAC_MBR_UBC_NSEN
			| period_len >> at_xdmac_get_dwidth(desc->lld.mbr_cfg);

		dev_dbg(chan2dev(chan),
			 "%s: lld: mbr_sa=%pad, mbr_da=%pad, mbr_ubc=0x%08x\n",
			 __func__, &desc->lld.mbr_sa, &desc->lld.mbr_da, desc->lld.mbr_ubc);

		/* Chain lld. */
		if (prev)
			at_xdmac_queue_desc(chan, prev, desc);

		prev = desc;
		if (!first)
			first = desc;

		dev_dbg(chan2dev(chan), "%s: add desc 0x%p to descs_list 0x%p\n",
			 __func__, desc, first);
		list_add_tail(&desc->desc_node, &first->descs_list);
	}

	at_xdmac_queue_desc(chan, prev, first);
	first->tx_dma_desc.flags = flags;
	first->xfer_size = buf_len;
	first->direction = direction;

	return &first->tx_dma_desc;
}

static inline u32 at_xdmac_align_width(struct dma_chan *chan, dma_addr_t addr)
{
	u32 width;

	/*
	 * Check address alignment to select the greater data width we
	 * can use.
	 *
	 * Some XDMAC implementations don't provide dword transfer, in
	 * this case selecting dword has the same behavior as
	 * selecting word transfers.
	 */
	if (!(addr & 7)) {
		width = AT_XDMAC_CC_DWIDTH_DWORD;
		dev_dbg(chan2dev(chan), "%s: dwidth: double word\n", __func__);
	} else if (!(addr & 3)) {
		width = AT_XDMAC_CC_DWIDTH_WORD;
		dev_dbg(chan2dev(chan), "%s: dwidth: word\n", __func__);
	} else if (!(addr & 1)) {
		width = AT_XDMAC_CC_DWIDTH_HALFWORD;
		dev_dbg(chan2dev(chan), "%s: dwidth: half word\n", __func__);
	} else {
		width = AT_XDMAC_CC_DWIDTH_BYTE;
		dev_dbg(chan2dev(chan), "%s: dwidth: byte\n", __func__);
	}

	return width;
}

static struct at_xdmac_desc *
at_xdmac_interleaved_queue_desc(struct dma_chan *chan,
				struct at_xdmac_chan *atchan,
				struct at_xdmac_desc *prev,
				dma_addr_t src, dma_addr_t dst,
				struct dma_interleaved_template *xt,
				struct data_chunk *chunk)
{
	struct at_xdmac_desc	*desc;
	u32			dwidth;
	unsigned long		flags;
	size_t			ublen;
	/*
	 * WARNING: The channel configuration is set here since there is no
	 * dmaengine_slave_config call in this case. Moreover we don't know the
	 * direction, it involves we can't dynamically set the source and dest
	 * interface so we have to use the same one. Only interface 0 allows EBI
	 * access. Hopefully we can access DDR through both ports (at least on
	 * SAMA5D4x), so we can use the same interface for source and dest,
	 * that solves the fact we don't know the direction.
	 * ERRATA: Even if useless for memory transfers, the PERID has to not
	 * match the one of another channel. If not, it could lead to spurious
	 * flag status.
	 */
	u32			chan_cc = AT_XDMAC_CC_PERID(0x3f)
					| AT_XDMAC_CC_DIF(0)
					| AT_XDMAC_CC_SIF(0)
					| AT_XDMAC_CC_MBSIZE_SIXTEEN
					| AT_XDMAC_CC_TYPE_MEM_TRAN;

	dwidth = at_xdmac_align_width(chan, src | dst | chunk->size);
	if (chunk->size >= (AT_XDMAC_MBR_UBC_UBLEN_MAX << dwidth)) {
		dev_dbg(chan2dev(chan),
			"%s: chunk too big (%zu, max size %lu)...\n",
			__func__, chunk->size,
			AT_XDMAC_MBR_UBC_UBLEN_MAX << dwidth);
		return NULL;
	}

	if (prev)
		dev_dbg(chan2dev(chan),
			"Adding items at the end of desc 0x%p\n", prev);

	if (xt->src_inc) {
		if (xt->src_sgl)
			chan_cc |=  AT_XDMAC_CC_SAM_UBS_AM;
		else
			chan_cc |=  AT_XDMAC_CC_SAM_INCREMENTED_AM;
	}

	if (xt->dst_inc) {
		if (xt->dst_sgl)
			chan_cc |=  AT_XDMAC_CC_DAM_UBS_AM;
		else
			chan_cc |=  AT_XDMAC_CC_DAM_INCREMENTED_AM;
	}

	spin_lock_irqsave(&atchan->lock, flags);
	desc = at_xdmac_get_desc(atchan);
	spin_unlock_irqrestore(&atchan->lock, flags);
	if (!desc) {
		dev_err(chan2dev(chan), "can't get descriptor\n");
		return NULL;
	}

	chan_cc |= AT_XDMAC_CC_DWIDTH(dwidth);

	ublen = chunk->size >> dwidth;

	desc->lld.mbr_sa = src;
	desc->lld.mbr_da = dst;
	desc->lld.mbr_sus = dmaengine_get_src_icg(xt, chunk);
	desc->lld.mbr_dus = dmaengine_get_dst_icg(xt, chunk);

	desc->lld.mbr_ubc = AT_XDMAC_MBR_UBC_NDV3
		| AT_XDMAC_MBR_UBC_NDEN
		| AT_XDMAC_MBR_UBC_NSEN
		| ublen;
	desc->lld.mbr_cfg = chan_cc;

	dev_dbg(chan2dev(chan),
		"%s: lld: mbr_sa=%pad, mbr_da=%pad, mbr_ubc=0x%08x, mbr_cfg=0x%08x\n",
		__func__, &desc->lld.mbr_sa, &desc->lld.mbr_da,
		desc->lld.mbr_ubc, desc->lld.mbr_cfg);

	/* Chain lld. */
	if (prev)
		at_xdmac_queue_desc(chan, prev, desc);

	return desc;
}

static struct dma_async_tx_descriptor *
at_xdmac_prep_interleaved(struct dma_chan *chan,
			  struct dma_interleaved_template *xt,
			  unsigned long flags)
{
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac_desc	*prev = NULL, *first = NULL;
	dma_addr_t		dst_addr, src_addr;
	size_t			src_skip = 0, dst_skip = 0, len = 0;
	struct data_chunk	*chunk;
	int			i;

	if (!xt || !xt->numf || (xt->dir != DMA_MEM_TO_MEM))
		return NULL;

	/*
	 * TODO: Handle the case where we have to repeat a chain of
	 * descriptors...
	 */
	if ((xt->numf > 1) && (xt->frame_size > 1))
		return NULL;

	dev_dbg(chan2dev(chan), "%s: src=%pad, dest=%pad, numf=%zu, frame_size=%zu, flags=0x%lx\n",
		__func__, &xt->src_start, &xt->dst_start,	xt->numf,
		xt->frame_size, flags);

	src_addr = xt->src_start;
	dst_addr = xt->dst_start;

	if (xt->numf > 1) {
		first = at_xdmac_interleaved_queue_desc(chan, atchan,
							NULL,
							src_addr, dst_addr,
							xt, xt->sgl);

		/* Length of the block is (BLEN+1) microblocks. */
		for (i = 0; i < xt->numf - 1; i++)
			at_xdmac_increment_block_count(chan, first);

		dev_dbg(chan2dev(chan), "%s: add desc 0x%p to descs_list 0x%p\n",
			__func__, first, first);
		list_add_tail(&first->desc_node, &first->descs_list);
	} else {
		for (i = 0; i < xt->frame_size; i++) {
			size_t src_icg = 0, dst_icg = 0;
			struct at_xdmac_desc *desc;

			chunk = xt->sgl + i;

			dst_icg = dmaengine_get_dst_icg(xt, chunk);
			src_icg = dmaengine_get_src_icg(xt, chunk);

			src_skip = chunk->size + src_icg;
			dst_skip = chunk->size + dst_icg;

			dev_dbg(chan2dev(chan),
				"%s: chunk size=%zu, src icg=%zu, dst icg=%zu\n",
				__func__, chunk->size, src_icg, dst_icg);

			desc = at_xdmac_interleaved_queue_desc(chan, atchan,
							       prev,
							       src_addr, dst_addr,
							       xt, chunk);
			if (!desc) {
				list_splice_init(&first->descs_list,
						 &atchan->free_descs_list);
				return NULL;
			}

			if (!first)
				first = desc;

			dev_dbg(chan2dev(chan), "%s: add desc 0x%p to descs_list 0x%p\n",
				__func__, desc, first);
			list_add_tail(&desc->desc_node, &first->descs_list);

			if (xt->src_sgl)
				src_addr += src_skip;

			if (xt->dst_sgl)
				dst_addr += dst_skip;

			len += chunk->size;
			prev = desc;
		}
	}

	first->tx_dma_desc.cookie = -EBUSY;
	first->tx_dma_desc.flags = flags;
	first->xfer_size = len;

	return &first->tx_dma_desc;
}

static struct dma_async_tx_descriptor *
at_xdmac_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
			 size_t len, unsigned long flags)
{
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac_desc	*first = NULL, *prev = NULL;
	size_t			remaining_size = len, xfer_size = 0, ublen;
	dma_addr_t		src_addr = src, dst_addr = dest;
	u32			dwidth;
	/*
	 * WARNING: We don't know the direction, it involves we can't
	 * dynamically set the source and dest interface so we have to use the
	 * same one. Only interface 0 allows EBI access. Hopefully we can
	 * access DDR through both ports (at least on SAMA5D4x), so we can use
	 * the same interface for source and dest, that solves the fact we
	 * don't know the direction.
	 * ERRATA: Even if useless for memory transfers, the PERID has to not
	 * match the one of another channel. If not, it could lead to spurious
	 * flag status.
	 */
	u32			chan_cc = AT_XDMAC_CC_PERID(0x3f)
					| AT_XDMAC_CC_DAM_INCREMENTED_AM
					| AT_XDMAC_CC_SAM_INCREMENTED_AM
					| AT_XDMAC_CC_DIF(0)
					| AT_XDMAC_CC_SIF(0)
					| AT_XDMAC_CC_MBSIZE_SIXTEEN
					| AT_XDMAC_CC_TYPE_MEM_TRAN;
	unsigned long		irqflags;

	dev_dbg(chan2dev(chan), "%s: src=%pad, dest=%pad, len=%zd, flags=0x%lx\n",
		__func__, &src, &dest, len, flags);

	if (unlikely(!len))
		return NULL;

	dwidth = at_xdmac_align_width(chan, src_addr | dst_addr);

	/* Prepare descriptors. */
	while (remaining_size) {
		struct at_xdmac_desc	*desc = NULL;

		dev_dbg(chan2dev(chan), "%s: remaining_size=%zu\n", __func__, remaining_size);

		spin_lock_irqsave(&atchan->lock, irqflags);
		desc = at_xdmac_get_desc(atchan);
		spin_unlock_irqrestore(&atchan->lock, irqflags);
		if (!desc) {
			dev_err(chan2dev(chan), "can't get descriptor\n");
			if (first)
				list_splice_init(&first->descs_list, &atchan->free_descs_list);
			return NULL;
		}

		/* Update src and dest addresses. */
		src_addr += xfer_size;
		dst_addr += xfer_size;

		if (remaining_size >= AT_XDMAC_MBR_UBC_UBLEN_MAX << dwidth)
			xfer_size = AT_XDMAC_MBR_UBC_UBLEN_MAX << dwidth;
		else
			xfer_size = remaining_size;

		dev_dbg(chan2dev(chan), "%s: xfer_size=%zu\n", __func__, xfer_size);

		/* Check remaining length and change data width if needed. */
		dwidth = at_xdmac_align_width(chan,
					      src_addr | dst_addr | xfer_size);
		chan_cc &= ~AT_XDMAC_CC_DWIDTH_MASK;
		chan_cc |= AT_XDMAC_CC_DWIDTH(dwidth);

		ublen = xfer_size >> dwidth;
		remaining_size -= xfer_size;

		desc->lld.mbr_sa = src_addr;
		desc->lld.mbr_da = dst_addr;
		desc->lld.mbr_ubc = AT_XDMAC_MBR_UBC_NDV2
			| AT_XDMAC_MBR_UBC_NDEN
			| AT_XDMAC_MBR_UBC_NSEN
			| ublen;
		desc->lld.mbr_cfg = chan_cc;

		dev_dbg(chan2dev(chan),
			 "%s: lld: mbr_sa=%pad, mbr_da=%pad, mbr_ubc=0x%08x, mbr_cfg=0x%08x\n",
			 __func__, &desc->lld.mbr_sa, &desc->lld.mbr_da, desc->lld.mbr_ubc, desc->lld.mbr_cfg);

		/* Chain lld. */
		if (prev)
			at_xdmac_queue_desc(chan, prev, desc);

		prev = desc;
		if (!first)
			first = desc;

		dev_dbg(chan2dev(chan), "%s: add desc 0x%p to descs_list 0x%p\n",
			 __func__, desc, first);
		list_add_tail(&desc->desc_node, &first->descs_list);
	}

	first->tx_dma_desc.flags = flags;
	first->xfer_size = len;

	return &first->tx_dma_desc;
}

static struct at_xdmac_desc *at_xdmac_memset_create_desc(struct dma_chan *chan,
							 struct at_xdmac_chan *atchan,
							 dma_addr_t dst_addr,
							 size_t len,
							 int value)
{
	struct at_xdmac_desc	*desc;
	unsigned long		flags;
	size_t			ublen;
	u32			dwidth;
	/*
	 * WARNING: The channel configuration is set here since there is no
	 * dmaengine_slave_config call in this case. Moreover we don't know the
	 * direction, it involves we can't dynamically set the source and dest
	 * interface so we have to use the same one. Only interface 0 allows EBI
	 * access. Hopefully we can access DDR through both ports (at least on
	 * SAMA5D4x), so we can use the same interface for source and dest,
	 * that solves the fact we don't know the direction.
	 * ERRATA: Even if useless for memory transfers, the PERID has to not
	 * match the one of another channel. If not, it could lead to spurious
	 * flag status.
	 */
	u32			chan_cc = AT_XDMAC_CC_PERID(0x3f)
					| AT_XDMAC_CC_DAM_UBS_AM
					| AT_XDMAC_CC_SAM_INCREMENTED_AM
					| AT_XDMAC_CC_DIF(0)
					| AT_XDMAC_CC_SIF(0)
					| AT_XDMAC_CC_MBSIZE_SIXTEEN
					| AT_XDMAC_CC_MEMSET_HW_MODE
					| AT_XDMAC_CC_TYPE_MEM_TRAN;

	dwidth = at_xdmac_align_width(chan, dst_addr);

	if (len >= (AT_XDMAC_MBR_UBC_UBLEN_MAX << dwidth)) {
		dev_err(chan2dev(chan),
			"%s: Transfer too large, aborting...\n",
			__func__);
		return NULL;
	}

	spin_lock_irqsave(&atchan->lock, flags);
	desc = at_xdmac_get_desc(atchan);
	spin_unlock_irqrestore(&atchan->lock, flags);
	if (!desc) {
		dev_err(chan2dev(chan), "can't get descriptor\n");
		return NULL;
	}

	chan_cc |= AT_XDMAC_CC_DWIDTH(dwidth);

	ublen = len >> dwidth;

	desc->lld.mbr_da = dst_addr;
	desc->lld.mbr_ds = value;
	desc->lld.mbr_ubc = AT_XDMAC_MBR_UBC_NDV3
		| AT_XDMAC_MBR_UBC_NDEN
		| AT_XDMAC_MBR_UBC_NSEN
		| ublen;
	desc->lld.mbr_cfg = chan_cc;

	dev_dbg(chan2dev(chan),
		"%s: lld: mbr_da=%pad, mbr_ds=0x%08x, mbr_ubc=0x%08x, mbr_cfg=0x%08x\n",
		__func__, &desc->lld.mbr_da, desc->lld.mbr_ds, desc->lld.mbr_ubc,
		desc->lld.mbr_cfg);

	return desc;
}

static struct dma_async_tx_descriptor *
at_xdmac_prep_dma_memset(struct dma_chan *chan, dma_addr_t dest, int value,
			 size_t len, unsigned long flags)
{
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac_desc	*desc;

	dev_dbg(chan2dev(chan), "%s: dest=%pad, len=%zu, pattern=0x%x, flags=0x%lx\n",
		__func__, &dest, len, value, flags);

	if (unlikely(!len))
		return NULL;

	desc = at_xdmac_memset_create_desc(chan, atchan, dest, len, value);
	list_add_tail(&desc->desc_node, &desc->descs_list);

	desc->tx_dma_desc.cookie = -EBUSY;
	desc->tx_dma_desc.flags = flags;
	desc->xfer_size = len;

	return &desc->tx_dma_desc;
}

static struct dma_async_tx_descriptor *
at_xdmac_prep_dma_memset_sg(struct dma_chan *chan, struct scatterlist *sgl,
			    unsigned int sg_len, int value,
			    unsigned long flags)
{
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac_desc	*desc, *pdesc = NULL,
				*ppdesc = NULL, *first = NULL;
	struct scatterlist	*sg, *psg = NULL, *ppsg = NULL;
	size_t			stride = 0, pstride = 0, len = 0;
	int			i;

	if (!sgl)
		return NULL;

	dev_dbg(chan2dev(chan), "%s: sg_len=%d, value=0x%x, flags=0x%lx\n",
		__func__, sg_len, value, flags);

	/* Prepare descriptors. */
	for_each_sg(sgl, sg, sg_len, i) {
		dev_dbg(chan2dev(chan), "%s: dest=%pad, len=%d, pattern=0x%x, flags=0x%lx\n",
			__func__, &sg_dma_address(sg), sg_dma_len(sg),
			value, flags);
		desc = at_xdmac_memset_create_desc(chan, atchan,
						   sg_dma_address(sg),
						   sg_dma_len(sg),
						   value);
		if (!desc && first)
			list_splice_init(&first->descs_list,
					 &atchan->free_descs_list);

		if (!first)
			first = desc;

		/* Update our strides */
		pstride = stride;
		if (psg)
			stride = sg_dma_address(sg) -
				(sg_dma_address(psg) + sg_dma_len(psg));

		/*
		 * The scatterlist API gives us only the address and
		 * length of each elements.
		 *
		 * Unfortunately, we don't have the stride, which we
		 * will need to compute.
		 *
		 * That make us end up in a situation like this one:
		 *    len    stride    len    stride    len
		 * +-------+        +-------+        +-------+
		 * |  N-2  |        |  N-1  |        |   N   |
		 * +-------+        +-------+        +-------+
		 *
		 * We need all these three elements (N-2, N-1 and N)
		 * to actually take the decision on whether we need to
		 * queue N-1 or reuse N-2.
		 *
		 * We will only consider N if it is the last element.
		 */
		if (ppdesc && pdesc) {
			if ((stride == pstride) &&
			    (sg_dma_len(ppsg) == sg_dma_len(psg))) {
				dev_dbg(chan2dev(chan),
					"%s: desc 0x%p can be merged with desc 0x%p\n",
					__func__, pdesc, ppdesc);

				/*
				 * Increment the block count of the
				 * N-2 descriptor
				 */
				at_xdmac_increment_block_count(chan, ppdesc);
				ppdesc->lld.mbr_dus = stride;

				/*
				 * Put back the N-1 descriptor in the
				 * free descriptor list
				 */
				list_add_tail(&pdesc->desc_node,
					      &atchan->free_descs_list);

				/*
				 * Make our N-1 descriptor pointer
				 * point to the N-2 since they were
				 * actually merged.
				 */
				pdesc = ppdesc;

			/*
			 * Rule out the case where we don't have
			 * pstride computed yet (our second sg
			 * element)
			 *
			 * We also want to catch the case where there
			 * would be a negative stride,
			 */
			} else if (pstride ||
				   sg_dma_address(sg) < sg_dma_address(psg)) {
				/*
				 * Queue the N-1 descriptor after the
				 * N-2
				 */
				at_xdmac_queue_desc(chan, ppdesc, pdesc);

				/*
				 * Add the N-1 descriptor to the list
				 * of the descriptors used for this
				 * transfer
				 */
				list_add_tail(&desc->desc_node,
					      &first->descs_list);
				dev_dbg(chan2dev(chan),
					"%s: add desc 0x%p to descs_list 0x%p\n",
					__func__, desc, first);
			}
		}

		/*
		 * If we are the last element, just see if we have the
		 * same size than the previous element.
		 *
		 * If so, we can merge it with the previous descriptor
		 * since we don't care about the stride anymore.
		 */
		if ((i == (sg_len - 1)) &&
		    sg_dma_len(psg) == sg_dma_len(sg)) {
			dev_dbg(chan2dev(chan),
				"%s: desc 0x%p can be merged with desc 0x%p\n",
				__func__, desc, pdesc);

			/*
			 * Increment the block count of the N-1
			 * descriptor
			 */
			at_xdmac_increment_block_count(chan, pdesc);
			pdesc->lld.mbr_dus = stride;

			/*
			 * Put back the N descriptor in the free
			 * descriptor list
			 */
			list_add_tail(&desc->desc_node,
				      &atchan->free_descs_list);
		}

		/* Update our descriptors */
		ppdesc = pdesc;
		pdesc = desc;

		/* Update our scatter pointers */
		ppsg = psg;
		psg = sg;

		len += sg_dma_len(sg);
	}

	first->tx_dma_desc.cookie = -EBUSY;
	first->tx_dma_desc.flags = flags;
	first->xfer_size = len;

	return &first->tx_dma_desc;
}

static enum dma_status
at_xdmac_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
		struct dma_tx_state *txstate)
{
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac		*atxdmac = to_at_xdmac(atchan->chan.device);
	struct at_xdmac_desc	*desc, *_desc;
	struct list_head	*descs_list;
	enum dma_status		ret;
	int			residue, retry;
	u32			cur_nda, check_nda, cur_ubc, mask, value;
	u8			dwidth = 0;
	unsigned long		flags;
	bool			initd;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	if (!txstate)
		return ret;

	spin_lock_irqsave(&atchan->lock, flags);

	desc = list_first_entry(&atchan->xfers_list, struct at_xdmac_desc, xfer_node);

	/*
	 * If the transfer has not been started yet, don't need to compute the
	 * residue, it's the transfer length.
	 */
	if (!desc->active_xfer) {
		dma_set_residue(txstate, desc->xfer_size);
		goto spin_unlock;
	}

	residue = desc->xfer_size;
	/*
	 * Flush FIFO: only relevant when the transfer is source peripheral
	 * synchronized. Flush is needed before reading CUBC because data in
	 * the FIFO are not reported by CUBC. Reporting a residue of the
	 * transfer length while we have data in FIFO can cause issue.
	 * Usecase: atmel USART has a timeout which means I have received
	 * characters but there is no more character received for a while. On
	 * timeout, it requests the residue. If the data are in the DMA FIFO,
	 * we will return a residue of the transfer length. It means no data
	 * received. If an application is waiting for these data, it will hang
	 * since we won't have another USART timeout without receiving new
	 * data.
	 */
	mask = AT_XDMAC_CC_TYPE | AT_XDMAC_CC_DSYNC;
	value = AT_XDMAC_CC_TYPE_PER_TRAN | AT_XDMAC_CC_DSYNC_PER2MEM;
	if ((desc->lld.mbr_cfg & mask) == value) {
		at_xdmac_write(atxdmac, AT_XDMAC_GSWF, atchan->mask);
		while (!(at_xdmac_chan_read(atchan, AT_XDMAC_CIS) & AT_XDMAC_CIS_FIS))
			cpu_relax();
	}

	/*
	 * The easiest way to compute the residue should be to pause the DMA
	 * but doing this can lead to miss some data as some devices don't
	 * have FIFO.
	 * We need to read several registers because:
	 * - DMA is running therefore a descriptor change is possible while
	 * reading these registers
	 * - When the block transfer is done, the value of the CUBC register
	 * is set to its initial value until the fetch of the next descriptor.
	 * This value will corrupt the residue calculation so we have to skip
	 * it.
	 *
	 * INITD --------                    ------------
	 *              |____________________|
	 *       _______________________  _______________
	 * NDA       @desc2             \/   @desc3
	 *       _______________________/\_______________
	 *       __________  ___________  _______________
	 * CUBC       0    \/ MAX desc1 \/  MAX desc2
	 *       __________/\___________/\_______________
	 *
	 * Since descriptors are aligned on 64 bits, we can assume that
	 * the update of NDA and CUBC is atomic.
	 * Memory barriers are used to ensure the read order of the registers.
	 * A max number of retries is set because unlikely it could never ends.
	 */
	for (retry = 0; retry < AT_XDMAC_RESIDUE_MAX_RETRIES; retry++) {
		check_nda = at_xdmac_chan_read(atchan, AT_XDMAC_CNDA) & 0xfffffffc;
		rmb();
		initd = !!(at_xdmac_chan_read(atchan, AT_XDMAC_CC) & AT_XDMAC_CC_INITD);
		rmb();
		cur_ubc = at_xdmac_chan_read(atchan, AT_XDMAC_CUBC);
		rmb();
		cur_nda = at_xdmac_chan_read(atchan, AT_XDMAC_CNDA) & 0xfffffffc;
		rmb();

		if ((check_nda == cur_nda) && initd)
			break;
	}

	if (unlikely(retry >= AT_XDMAC_RESIDUE_MAX_RETRIES)) {
		ret = DMA_ERROR;
		goto spin_unlock;
	}

	/*
	 * Flush FIFO: only relevant when the transfer is source peripheral
	 * synchronized. Another flush is needed here because CUBC is updated
	 * when the controller sends the data write command. It can lead to
	 * report data that are not written in the memory or the device. The
	 * FIFO flush ensures that data are really written.
	 */
	if ((desc->lld.mbr_cfg & mask) == value) {
		at_xdmac_write(atxdmac, AT_XDMAC_GSWF, atchan->mask);
		while (!(at_xdmac_chan_read(atchan, AT_XDMAC_CIS) & AT_XDMAC_CIS_FIS))
			cpu_relax();
	}

	/*
	 * Remove size of all microblocks already transferred and the current
	 * one. Then add the remaining size to transfer of the current
	 * microblock.
	 */
	descs_list = &desc->descs_list;
	list_for_each_entry_safe(desc, _desc, descs_list, desc_node) {
		dwidth = at_xdmac_get_dwidth(desc->lld.mbr_cfg);
		residue -= (desc->lld.mbr_ubc & 0xffffff) << dwidth;
		if ((desc->lld.mbr_nda & 0xfffffffc) == cur_nda)
			break;
	}
	residue += cur_ubc << dwidth;

	dma_set_residue(txstate, residue);

	dev_dbg(chan2dev(chan),
		 "%s: desc=0x%p, tx_dma_desc.phys=%pad, tx_status=%d, cookie=%d, residue=%d\n",
		 __func__, desc, &desc->tx_dma_desc.phys, ret, cookie, residue);

spin_unlock:
	spin_unlock_irqrestore(&atchan->lock, flags);
	return ret;
}

/* Call must be protected by lock. */
static void at_xdmac_remove_xfer(struct at_xdmac_chan *atchan,
				    struct at_xdmac_desc *desc)
{
	dev_dbg(chan2dev(&atchan->chan), "%s: desc 0x%p\n", __func__, desc);

	/*
	 * Remove the transfer from the transfer list then move the transfer
	 * descriptors into the free descriptors list.
	 */
	list_del(&desc->xfer_node);
	list_splice_init(&desc->descs_list, &atchan->free_descs_list);
}

static void at_xdmac_advance_work(struct at_xdmac_chan *atchan)
{
	struct at_xdmac_desc	*desc;
	unsigned long		flags;

	spin_lock_irqsave(&atchan->lock, flags);

	/*
	 * If channel is enabled, do nothing, advance_work will be triggered
	 * after the interruption.
	 */
	if (!at_xdmac_chan_is_enabled(atchan) && !list_empty(&atchan->xfers_list)) {
		desc = list_first_entry(&atchan->xfers_list,
					struct at_xdmac_desc,
					xfer_node);
		dev_vdbg(chan2dev(&atchan->chan), "%s: desc 0x%p\n", __func__, desc);
		if (!desc->active_xfer)
			at_xdmac_start_xfer(atchan, desc);
	}

	spin_unlock_irqrestore(&atchan->lock, flags);
}

static void at_xdmac_handle_cyclic(struct at_xdmac_chan *atchan)
{
	struct at_xdmac_desc		*desc;
	struct dma_async_tx_descriptor	*txd;

	desc = list_first_entry(&atchan->xfers_list, struct at_xdmac_desc, xfer_node);
	txd = &desc->tx_dma_desc;

	if (txd->flags & DMA_PREP_INTERRUPT)
		dmaengine_desc_get_callback_invoke(txd, NULL);
}

static void at_xdmac_tasklet(unsigned long data)
{
	struct at_xdmac_chan	*atchan = (struct at_xdmac_chan *)data;
	struct at_xdmac_desc	*desc;
	u32			error_mask;

	dev_dbg(chan2dev(&atchan->chan), "%s: status=0x%08lx\n",
		 __func__, atchan->status);

	error_mask = AT_XDMAC_CIS_RBEIS
		     | AT_XDMAC_CIS_WBEIS
		     | AT_XDMAC_CIS_ROIS;

	if (at_xdmac_chan_is_cyclic(atchan)) {
		at_xdmac_handle_cyclic(atchan);
	} else if ((atchan->status & AT_XDMAC_CIS_LIS)
		   || (atchan->status & error_mask)) {
		struct dma_async_tx_descriptor  *txd;

		if (atchan->status & AT_XDMAC_CIS_RBEIS)
			dev_err(chan2dev(&atchan->chan), "read bus error!!!");
		if (atchan->status & AT_XDMAC_CIS_WBEIS)
			dev_err(chan2dev(&atchan->chan), "write bus error!!!");
		if (atchan->status & AT_XDMAC_CIS_ROIS)
			dev_err(chan2dev(&atchan->chan), "request overflow error!!!");

		spin_lock_bh(&atchan->lock);
		desc = list_first_entry(&atchan->xfers_list,
					struct at_xdmac_desc,
					xfer_node);
		dev_vdbg(chan2dev(&atchan->chan), "%s: desc 0x%p\n", __func__, desc);
		BUG_ON(!desc->active_xfer);

		txd = &desc->tx_dma_desc;

		at_xdmac_remove_xfer(atchan, desc);
		spin_unlock_bh(&atchan->lock);

		if (!at_xdmac_chan_is_cyclic(atchan)) {
			dma_cookie_complete(txd);
			if (txd->flags & DMA_PREP_INTERRUPT)
				dmaengine_desc_get_callback_invoke(txd, NULL);
		}

		dma_run_dependencies(txd);

		at_xdmac_advance_work(atchan);
	}
}

static irqreturn_t at_xdmac_interrupt(int irq, void *dev_id)
{
	struct at_xdmac		*atxdmac = (struct at_xdmac *)dev_id;
	struct at_xdmac_chan	*atchan;
	u32			imr, status, pending;
	u32			chan_imr, chan_status;
	int			i, ret = IRQ_NONE;

	do {
		imr = at_xdmac_read(atxdmac, AT_XDMAC_GIM);
		status = at_xdmac_read(atxdmac, AT_XDMAC_GIS);
		pending = status & imr;

		dev_vdbg(atxdmac->dma.dev,
			 "%s: status=0x%08x, imr=0x%08x, pending=0x%08x\n",
			 __func__, status, imr, pending);

		if (!pending)
			break;

		/* We have to find which channel has generated the interrupt. */
		for (i = 0; i < atxdmac->dma.chancnt; i++) {
			if (!((1 << i) & pending))
				continue;

			atchan = &atxdmac->chan[i];
			chan_imr = at_xdmac_chan_read(atchan, AT_XDMAC_CIM);
			chan_status = at_xdmac_chan_read(atchan, AT_XDMAC_CIS);
			atchan->status = chan_status & chan_imr;
			dev_vdbg(atxdmac->dma.dev,
				 "%s: chan%d: imr=0x%x, status=0x%x\n",
				 __func__, i, chan_imr, chan_status);
			dev_vdbg(chan2dev(&atchan->chan),
				 "%s: CC=0x%08x CNDA=0x%08x, CNDC=0x%08x, CSA=0x%08x, CDA=0x%08x, CUBC=0x%08x\n",
				 __func__,
				 at_xdmac_chan_read(atchan, AT_XDMAC_CC),
				 at_xdmac_chan_read(atchan, AT_XDMAC_CNDA),
				 at_xdmac_chan_read(atchan, AT_XDMAC_CNDC),
				 at_xdmac_chan_read(atchan, AT_XDMAC_CSA),
				 at_xdmac_chan_read(atchan, AT_XDMAC_CDA),
				 at_xdmac_chan_read(atchan, AT_XDMAC_CUBC));

			if (atchan->status & (AT_XDMAC_CIS_RBEIS | AT_XDMAC_CIS_WBEIS))
				at_xdmac_write(atxdmac, AT_XDMAC_GD, atchan->mask);

			tasklet_schedule(&atchan->tasklet);
			ret = IRQ_HANDLED;
		}

	} while (pending);

	return ret;
}

static void at_xdmac_issue_pending(struct dma_chan *chan)
{
	struct at_xdmac_chan *atchan = to_at_xdmac_chan(chan);

	dev_dbg(chan2dev(&atchan->chan), "%s\n", __func__);

	if (!at_xdmac_chan_is_cyclic(atchan))
		at_xdmac_advance_work(atchan);

	return;
}

static int at_xdmac_device_config(struct dma_chan *chan,
				  struct dma_slave_config *config)
{
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	int ret;
	unsigned long		flags;

	dev_dbg(chan2dev(chan), "%s\n", __func__);

	spin_lock_irqsave(&atchan->lock, flags);
	ret = at_xdmac_set_slave_config(chan, config);
	spin_unlock_irqrestore(&atchan->lock, flags);

	return ret;
}

static int at_xdmac_device_pause(struct dma_chan *chan)
{
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac		*atxdmac = to_at_xdmac(atchan->chan.device);
	unsigned long		flags;

	dev_dbg(chan2dev(chan), "%s\n", __func__);

	if (test_and_set_bit(AT_XDMAC_CHAN_IS_PAUSED, &atchan->status))
		return 0;

	spin_lock_irqsave(&atchan->lock, flags);
	at_xdmac_write(atxdmac, AT_XDMAC_GRWS, atchan->mask);
	while (at_xdmac_chan_read(atchan, AT_XDMAC_CC)
	       & (AT_XDMAC_CC_WRIP | AT_XDMAC_CC_RDIP))
		cpu_relax();
	spin_unlock_irqrestore(&atchan->lock, flags);

	return 0;
}

static int at_xdmac_device_resume(struct dma_chan *chan)
{
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac		*atxdmac = to_at_xdmac(atchan->chan.device);
	unsigned long		flags;

	dev_dbg(chan2dev(chan), "%s\n", __func__);

	spin_lock_irqsave(&atchan->lock, flags);
	if (!at_xdmac_chan_is_paused(atchan)) {
		spin_unlock_irqrestore(&atchan->lock, flags);
		return 0;
	}

	at_xdmac_write(atxdmac, AT_XDMAC_GRWR, atchan->mask);
	clear_bit(AT_XDMAC_CHAN_IS_PAUSED, &atchan->status);
	spin_unlock_irqrestore(&atchan->lock, flags);

	return 0;
}

static int at_xdmac_device_terminate_all(struct dma_chan *chan)
{
	struct at_xdmac_desc	*desc, *_desc;
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac		*atxdmac = to_at_xdmac(atchan->chan.device);
	unsigned long		flags;

	dev_dbg(chan2dev(chan), "%s\n", __func__);

	spin_lock_irqsave(&atchan->lock, flags);
	at_xdmac_write(atxdmac, AT_XDMAC_GD, atchan->mask);
	while (at_xdmac_read(atxdmac, AT_XDMAC_GS) & atchan->mask)
		cpu_relax();

	/* Cancel all pending transfers. */
	list_for_each_entry_safe(desc, _desc, &atchan->xfers_list, xfer_node)
		at_xdmac_remove_xfer(atchan, desc);

	clear_bit(AT_XDMAC_CHAN_IS_PAUSED, &atchan->status);
	clear_bit(AT_XDMAC_CHAN_IS_CYCLIC, &atchan->status);
	spin_unlock_irqrestore(&atchan->lock, flags);

	return 0;
}

static int at_xdmac_alloc_chan_resources(struct dma_chan *chan)
{
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac_desc	*desc;
	int			i;
	unsigned long		flags;

	spin_lock_irqsave(&atchan->lock, flags);

	if (at_xdmac_chan_is_enabled(atchan)) {
		dev_err(chan2dev(chan),
			"can't allocate channel resources (channel enabled)\n");
		i = -EIO;
		goto spin_unlock;
	}

	if (!list_empty(&atchan->free_descs_list)) {
		dev_err(chan2dev(chan),
			"can't allocate channel resources (channel not free from a previous use)\n");
		i = -EIO;
		goto spin_unlock;
	}

	for (i = 0; i < init_nr_desc_per_channel; i++) {
		desc = at_xdmac_alloc_desc(chan, GFP_ATOMIC);
		if (!desc) {
			dev_warn(chan2dev(chan),
				"only %d descriptors have been allocated\n", i);
			break;
		}
		list_add_tail(&desc->desc_node, &atchan->free_descs_list);
	}

	dma_cookie_init(chan);

	dev_dbg(chan2dev(chan), "%s: allocated %d descriptors\n", __func__, i);

spin_unlock:
	spin_unlock_irqrestore(&atchan->lock, flags);
	return i;
}

static void at_xdmac_free_chan_resources(struct dma_chan *chan)
{
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac		*atxdmac = to_at_xdmac(chan->device);
	struct at_xdmac_desc	*desc, *_desc;

	list_for_each_entry_safe(desc, _desc, &atchan->free_descs_list, desc_node) {
		dev_dbg(chan2dev(chan), "%s: freeing descriptor %p\n", __func__, desc);
		list_del(&desc->desc_node);
		dma_pool_free(atxdmac->at_xdmac_desc_pool, desc, desc->tx_dma_desc.phys);
	}

	return;
}

#ifdef CONFIG_PM
static int atmel_xdmac_prepare(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct at_xdmac		*atxdmac = platform_get_drvdata(pdev);
	struct dma_chan		*chan, *_chan;

	list_for_each_entry_safe(chan, _chan, &atxdmac->dma.channels, device_node) {
		struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);

		/* Wait for transfer completion, except in cyclic case. */
		if (at_xdmac_chan_is_enabled(atchan) && !at_xdmac_chan_is_cyclic(atchan))
			return -EAGAIN;
	}
	return 0;
}
#else
#	define atmel_xdmac_prepare NULL
#endif

#ifdef CONFIG_PM_SLEEP
static int atmel_xdmac_suspend(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct at_xdmac		*atxdmac = platform_get_drvdata(pdev);
	struct dma_chan		*chan, *_chan;

	list_for_each_entry_safe(chan, _chan, &atxdmac->dma.channels, device_node) {
		struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);

		atchan->save_cc = at_xdmac_chan_read(atchan, AT_XDMAC_CC);
		if (at_xdmac_chan_is_cyclic(atchan)) {
			if (!at_xdmac_chan_is_paused(atchan))
				at_xdmac_device_pause(chan);
			atchan->save_cim = at_xdmac_chan_read(atchan, AT_XDMAC_CIM);
			atchan->save_cnda = at_xdmac_chan_read(atchan, AT_XDMAC_CNDA);
			atchan->save_cndc = at_xdmac_chan_read(atchan, AT_XDMAC_CNDC);
		}
	}
	atxdmac->save_gim = at_xdmac_read(atxdmac, AT_XDMAC_GIM);

	at_xdmac_off(atxdmac);
	clk_disable_unprepare(atxdmac->clk);
	return 0;
}

static int atmel_xdmac_resume(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct at_xdmac		*atxdmac = platform_get_drvdata(pdev);
	struct at_xdmac_chan	*atchan;
	struct dma_chan		*chan, *_chan;
	int			i;
	int ret;

	ret = clk_prepare_enable(atxdmac->clk);
	if (ret)
		return ret;

	/* Clear pending interrupts. */
	for (i = 0; i < atxdmac->dma.chancnt; i++) {
		atchan = &atxdmac->chan[i];
		while (at_xdmac_chan_read(atchan, AT_XDMAC_CIS))
			cpu_relax();
	}

	at_xdmac_write(atxdmac, AT_XDMAC_GIE, atxdmac->save_gim);
	list_for_each_entry_safe(chan, _chan, &atxdmac->dma.channels, device_node) {
		atchan = to_at_xdmac_chan(chan);
		at_xdmac_chan_write(atchan, AT_XDMAC_CC, atchan->save_cc);
		if (at_xdmac_chan_is_cyclic(atchan)) {
			if (at_xdmac_chan_is_paused(atchan))
				at_xdmac_device_resume(chan);
			at_xdmac_chan_write(atchan, AT_XDMAC_CNDA, atchan->save_cnda);
			at_xdmac_chan_write(atchan, AT_XDMAC_CNDC, atchan->save_cndc);
			at_xdmac_chan_write(atchan, AT_XDMAC_CIE, atchan->save_cim);
			wmb();
			at_xdmac_write(atxdmac, AT_XDMAC_GE, atchan->mask);
		}
	}
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static int at_xdmac_probe(struct platform_device *pdev)
{
	struct resource	*res;
	struct at_xdmac	*atxdmac;
	int		irq, size, nr_channels, i, ret;
	void __iomem	*base;
	u32		reg;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	/*
	 * Read number of xdmac channels, read helper function can't be used
	 * since atxdmac is not yet allocated and we need to know the number
	 * of channels to do the allocation.
	 */
	reg = readl_relaxed(base + AT_XDMAC_GTYPE);
	nr_channels = AT_XDMAC_NB_CH(reg);
	if (nr_channels > AT_XDMAC_MAX_CHAN) {
		dev_err(&pdev->dev, "invalid number of channels (%u)\n",
			nr_channels);
		return -EINVAL;
	}

	size = sizeof(*atxdmac);
	size += nr_channels * sizeof(struct at_xdmac_chan);
	atxdmac = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!atxdmac) {
		dev_err(&pdev->dev, "can't allocate at_xdmac structure\n");
		return -ENOMEM;
	}

	atxdmac->regs = base;
	atxdmac->irq = irq;

	atxdmac->clk = devm_clk_get(&pdev->dev, "dma_clk");
	if (IS_ERR(atxdmac->clk)) {
		dev_err(&pdev->dev, "can't get dma_clk\n");
		return PTR_ERR(atxdmac->clk);
	}

	/* Do not use dev res to prevent races with tasklet */
	ret = request_irq(atxdmac->irq, at_xdmac_interrupt, 0, "at_xdmac", atxdmac);
	if (ret) {
		dev_err(&pdev->dev, "can't request irq\n");
		return ret;
	}

	ret = clk_prepare_enable(atxdmac->clk);
	if (ret) {
		dev_err(&pdev->dev, "can't prepare or enable clock\n");
		goto err_free_irq;
	}

	atxdmac->at_xdmac_desc_pool =
		dmam_pool_create(dev_name(&pdev->dev), &pdev->dev,
				sizeof(struct at_xdmac_desc), 4, 0);
	if (!atxdmac->at_xdmac_desc_pool) {
		dev_err(&pdev->dev, "no memory for descriptors dma pool\n");
		ret = -ENOMEM;
		goto err_clk_disable;
	}

	dma_cap_set(DMA_CYCLIC, atxdmac->dma.cap_mask);
	dma_cap_set(DMA_INTERLEAVE, atxdmac->dma.cap_mask);
	dma_cap_set(DMA_MEMCPY, atxdmac->dma.cap_mask);
	dma_cap_set(DMA_MEMSET, atxdmac->dma.cap_mask);
	dma_cap_set(DMA_MEMSET_SG, atxdmac->dma.cap_mask);
	dma_cap_set(DMA_SLAVE, atxdmac->dma.cap_mask);
	/*
	 * Without DMA_PRIVATE the driver is not able to allocate more than
	 * one channel, second allocation fails in private_candidate.
	 */
	dma_cap_set(DMA_PRIVATE, atxdmac->dma.cap_mask);
	atxdmac->dma.dev				= &pdev->dev;
	atxdmac->dma.device_alloc_chan_resources	= at_xdmac_alloc_chan_resources;
	atxdmac->dma.device_free_chan_resources		= at_xdmac_free_chan_resources;
	atxdmac->dma.device_tx_status			= at_xdmac_tx_status;
	atxdmac->dma.device_issue_pending		= at_xdmac_issue_pending;
	atxdmac->dma.device_prep_dma_cyclic		= at_xdmac_prep_dma_cyclic;
	atxdmac->dma.device_prep_interleaved_dma	= at_xdmac_prep_interleaved;
	atxdmac->dma.device_prep_dma_memcpy		= at_xdmac_prep_dma_memcpy;
	atxdmac->dma.device_prep_dma_memset		= at_xdmac_prep_dma_memset;
	atxdmac->dma.device_prep_dma_memset_sg		= at_xdmac_prep_dma_memset_sg;
	atxdmac->dma.device_prep_slave_sg		= at_xdmac_prep_slave_sg;
	atxdmac->dma.device_config			= at_xdmac_device_config;
	atxdmac->dma.device_pause			= at_xdmac_device_pause;
	atxdmac->dma.device_resume			= at_xdmac_device_resume;
	atxdmac->dma.device_terminate_all		= at_xdmac_device_terminate_all;
	atxdmac->dma.src_addr_widths = AT_XDMAC_DMA_BUSWIDTHS;
	atxdmac->dma.dst_addr_widths = AT_XDMAC_DMA_BUSWIDTHS;
	atxdmac->dma.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	atxdmac->dma.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;

	/* Disable all chans and interrupts. */
	at_xdmac_off(atxdmac);

	/* Init channels. */
	INIT_LIST_HEAD(&atxdmac->dma.channels);
	for (i = 0; i < nr_channels; i++) {
		struct at_xdmac_chan *atchan = &atxdmac->chan[i];

		atchan->chan.device = &atxdmac->dma;
		list_add_tail(&atchan->chan.device_node,
			      &atxdmac->dma.channels);

		atchan->ch_regs = at_xdmac_chan_reg_base(atxdmac, i);
		atchan->mask = 1 << i;

		spin_lock_init(&atchan->lock);
		INIT_LIST_HEAD(&atchan->xfers_list);
		INIT_LIST_HEAD(&atchan->free_descs_list);
		tasklet_init(&atchan->tasklet, at_xdmac_tasklet,
			     (unsigned long)atchan);

		/* Clear pending interrupts. */
		while (at_xdmac_chan_read(atchan, AT_XDMAC_CIS))
			cpu_relax();
	}
	platform_set_drvdata(pdev, atxdmac);

	ret = dma_async_device_register(&atxdmac->dma);
	if (ret) {
		dev_err(&pdev->dev, "fail to register DMA engine device\n");
		goto err_clk_disable;
	}

	ret = of_dma_controller_register(pdev->dev.of_node,
					 at_xdmac_xlate, atxdmac);
	if (ret) {
		dev_err(&pdev->dev, "could not register of dma controller\n");
		goto err_dma_unregister;
	}

	dev_info(&pdev->dev, "%d channels, mapped at 0x%p\n",
		 nr_channels, atxdmac->regs);

	return 0;

err_dma_unregister:
	dma_async_device_unregister(&atxdmac->dma);
err_clk_disable:
	clk_disable_unprepare(atxdmac->clk);
err_free_irq:
	free_irq(atxdmac->irq, atxdmac);
	return ret;
}

static int at_xdmac_remove(struct platform_device *pdev)
{
	struct at_xdmac	*atxdmac = (struct at_xdmac *)platform_get_drvdata(pdev);
	int		i;

	at_xdmac_off(atxdmac);
	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&atxdmac->dma);
	clk_disable_unprepare(atxdmac->clk);

	free_irq(atxdmac->irq, atxdmac);

	for (i = 0; i < atxdmac->dma.chancnt; i++) {
		struct at_xdmac_chan *atchan = &atxdmac->chan[i];

		tasklet_kill(&atchan->tasklet);
		at_xdmac_free_chan_resources(&atchan->chan);
	}

	return 0;
}

static const struct dev_pm_ops atmel_xdmac_dev_pm_ops = {
	.prepare	= atmel_xdmac_prepare,
	SET_LATE_SYSTEM_SLEEP_PM_OPS(atmel_xdmac_suspend, atmel_xdmac_resume)
};

static const struct of_device_id atmel_xdmac_dt_ids[] = {
	{
		.compatible = "atmel,sama5d4-dma",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, atmel_xdmac_dt_ids);

static struct platform_driver at_xdmac_driver = {
	.probe		= at_xdmac_probe,
	.remove		= at_xdmac_remove,
	.driver = {
		.name		= "at_xdmac",
		.of_match_table	= of_match_ptr(atmel_xdmac_dt_ids),
		.pm		= &atmel_xdmac_dev_pm_ops,
	}
};

static int __init at_xdmac_init(void)
{
	return platform_driver_probe(&at_xdmac_driver, at_xdmac_probe);
}
subsys_initcall(at_xdmac_init);

MODULE_DESCRIPTION("Atmel Extended DMA Controller driver");
MODULE_AUTHOR("Ludovic Desroches <ludovic.desroches@atmel.com>");
MODULE_LICENSE("GPL");
