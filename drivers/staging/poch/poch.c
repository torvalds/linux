/*
 * User-space DMA and UIO based Redrapids Pocket Change CardBus driver
 *
 * Copyright 2008 Vijay Kumar <vijaykumar@bravegnu.org>
 *
 * Licensed under GPL version 2 only.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/uio_driver.h>
#include <linux/spinlock.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/poll.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/io.h>

#include "poch.h"

#include <asm/cacheflush.h>

#ifndef PCI_VENDOR_ID_RRAPIDS
#define PCI_VENDOR_ID_RRAPIDS 0x17D2
#endif

#ifndef PCI_DEVICE_ID_RRAPIDS_POCKET_CHANGE
#define PCI_DEVICE_ID_RRAPIDS_POCKET_CHANGE 0x0351
#endif

#define POCH_NCHANNELS 2

#define MAX_POCH_CARDS 8
#define MAX_POCH_DEVICES (MAX_POCH_CARDS * POCH_NCHANNELS)

#define DRV_NAME "poch"
#define PFX      DRV_NAME ": "

/*
 * BAR0 Bridge Register Definitions
 */

#define BRIDGE_REV_REG			0x0
#define BRIDGE_INT_MASK_REG		0x4
#define BRIDGE_INT_STAT_REG		0x8

#define BRIDGE_INT_ACTIVE		(0x1 << 31)
#define BRIDGE_INT_FPGA		        (0x1 << 2)
#define BRIDGE_INT_TEMP_FAIL		(0x1 << 1)
#define	BRIDGE_INT_TEMP_WARN		(0x1 << 0)

#define BRIDGE_FPGA_RESET_REG		0xC

#define BRIDGE_CARD_POWER_REG		0x10
#define BRIDGE_CARD_POWER_EN            (0x1 << 0)
#define BRIDGE_CARD_POWER_PROG_DONE     (0x1 << 31)

#define BRIDGE_JTAG_REG			0x14
#define BRIDGE_DMA_GO_REG		0x18
#define BRIDGE_STAT_0_REG		0x1C
#define BRIDGE_STAT_1_REG		0x20
#define BRIDGE_STAT_2_REG		0x24
#define BRIDGE_STAT_3_REG		0x28
#define BRIDGE_TEMP_STAT_REG		0x2C
#define BRIDGE_TEMP_THRESH_REG		0x30
#define BRIDGE_EEPROM_REVSEL_REG	0x34
#define BRIDGE_CIS_STRUCT_REG		0x100
#define BRIDGE_BOARDREV_REG		0x124

/*
 * BAR1 FPGA Register Definitions
 */

#define FPGA_IFACE_REV_REG		0x0
#define FPGA_RX_BLOCK_SIZE_REG		0x8
#define FPGA_TX_BLOCK_SIZE_REG		0xC
#define FPGA_RX_BLOCK_COUNT_REG		0x10
#define FPGA_TX_BLOCK_COUNT_REG		0x14
#define FPGA_RX_CURR_DMA_BLOCK_REG	0x18
#define FPGA_TX_CURR_DMA_BLOCK_REG	0x1C
#define FPGA_RX_GROUP_COUNT_REG		0x20
#define FPGA_TX_GROUP_COUNT_REG		0x24
#define FPGA_RX_CURR_GROUP_REG		0x28
#define FPGA_TX_CURR_GROUP_REG		0x2C
#define FPGA_RX_CURR_PCI_REG		0x38
#define FPGA_TX_CURR_PCI_REG		0x3C
#define FPGA_RX_GROUP0_START_REG	0x40
#define FPGA_TX_GROUP0_START_REG	0xC0
#define FPGA_DMA_DESC_1_REG		0x140
#define FPGA_DMA_DESC_2_REG		0x144
#define FPGA_DMA_DESC_3_REG		0x148
#define FPGA_DMA_DESC_4_REG		0x14C

#define FPGA_DMA_INT_STAT_REG		0x150
#define FPGA_DMA_INT_MASK_REG		0x154
#define FPGA_DMA_INT_RX		(1 << 0)
#define FPGA_DMA_INT_TX		(1 << 1)

#define FPGA_RX_GROUPS_PER_INT_REG	0x158
#define FPGA_TX_GROUPS_PER_INT_REG	0x15C
#define FPGA_DMA_ADR_PAGE_REG		0x160
#define FPGA_FPGA_REV_REG		0x200

#define FPGA_ADC_CLOCK_CTL_REG		0x204
#define FPGA_ADC_CLOCK_CTL_OSC_EN	(0x1 << 3)
#define FPGA_ADC_CLOCK_LOCAL_CLK	(0x1 | FPGA_ADC_CLOCK_CTL_OSC_EN)
#define FPGA_ADC_CLOCK_EXT_SAMP_CLK	0X0

#define FPGA_ADC_DAC_EN_REG		0x208
#define FPGA_ADC_DAC_EN_DAC_OFF         (0x1 << 1)
#define FPGA_ADC_DAC_EN_ADC_OFF         (0x1 << 0)

#define FPGA_INT_STAT_REG		0x20C
#define FPGA_INT_MASK_REG		0x210
#define FPGA_INT_PLL_UNLOCKED		(0x1 << 9)
#define FPGA_INT_DMA_CORE		(0x1 << 8)
#define FPGA_INT_TX_FF_EMPTY		(0x1 << 7)
#define FPGA_INT_RX_FF_EMPTY		(0x1 << 6)
#define FPGA_INT_TX_FF_OVRFLW		(0x1 << 3)
#define FPGA_INT_RX_FF_OVRFLW		(0x1 << 2)
#define FPGA_INT_TX_ACQ_DONE		(0x1 << 1)
#define FPGA_INT_RX_ACQ_DONE		(0x1)

#define FPGA_RX_CTL_REG			0x214
#define FPGA_RX_CTL_FIFO_FLUSH      	(0x1 << 9)
#define FPGA_RX_CTL_SYNTH_DATA		(0x1 << 8)
#define FPGA_RX_CTL_CONT_CAP		(0x0 << 1)
#define FPGA_RX_CTL_SNAP_CAP		(0x1 << 1)

#define FPGA_RX_ARM_REG			0x21C

#define FPGA_DOM_REG			0x224
#define	FPGA_DOM_DCM_RESET		(0x1 << 5)
#define FPGA_DOM_SOFT_RESET		(0x1 << 4)
#define FPGA_DOM_DUAL_M_SG_DMA		(0x0)
#define FPGA_DOM_TARGET_ACCESS		(0x1)

#define FPGA_TX_CTL_REG			0x228
#define FPGA_TX_CTL_FIFO_FLUSH          (0x1 << 9)
#define FPGA_TX_CTL_OUTPUT_ZERO         (0x0 << 2)
#define FPGA_TX_CTL_OUTPUT_CARDBUS      (0x1 << 2)
#define FPGA_TX_CTL_OUTPUT_ADC          (0x2 << 2)
#define FPGA_TX_CTL_OUTPUT_SNAPSHOT     (0x3 << 2)
#define FPGA_TX_CTL_LOOPBACK            (0x1 << 0)

#define FPGA_ENDIAN_MODE_REG		0x22C
#define FPGA_RX_FIFO_COUNT_REG		0x28C
#define FPGA_TX_ENABLE_REG		0x298
#define FPGA_TX_TRIGGER_REG		0x29C
#define FPGA_TX_DATAMEM_COUNT_REG	0x2A8
#define FPGA_CAP_FIFO_REG		0x300
#define FPGA_TX_SNAPSHOT_REG		0x8000

/*
 * Channel Index Definitions
 */

enum {
	CHNO_RX_CHANNEL,
	CHNO_TX_CHANNEL,
};

struct poch_dev;

enum channel_dir {
	CHANNEL_DIR_RX,
	CHANNEL_DIR_TX,
};

struct poch_group_info {
	struct page *pg;
	dma_addr_t dma_addr;
	unsigned long user_offset;
};

struct channel_info {
	unsigned int chno;

	atomic_t sys_block_size;
	atomic_t sys_group_size;
	atomic_t sys_group_count;

	enum channel_dir dir;

	unsigned long block_size;
	unsigned long group_size;
	unsigned long group_count;

	/* Contains the DMA address and VM offset of each group. */
	struct poch_group_info *groups;

	/* Contains the header and circular buffer exported to userspace. */
	spinlock_t group_offsets_lock;
	struct poch_cbuf_header *header;
	struct page *header_pg;
	unsigned long header_size;

	/* Last group indicated as 'complete' to user space. */
	unsigned int transfer;

	wait_queue_head_t wq;

	union {
		unsigned int data_available;
		unsigned int space_available;
	};

	void __iomem *bridge_iomem;
	void __iomem *fpga_iomem;
	spinlock_t *iomem_lock;

	atomic_t free;
	atomic_t inited;

	/* Error counters */
	struct poch_counters counters;
	spinlock_t counters_lock;

	struct device *dev;
};

struct poch_dev {
	struct uio_info uio;
	struct pci_dev *pci_dev;
	unsigned int nchannels;
	struct channel_info channels[POCH_NCHANNELS];
	struct cdev cdev;

	/* Counts the no. of channels that have been opened. On first
	 * open, the card is powered on. On last channel close, the
	 * card is powered off.
	 */
	atomic_t usage;

	void __iomem *bridge_iomem;
	void __iomem *fpga_iomem;
	spinlock_t iomem_lock;

	struct device *dev;
};

static dev_t poch_first_dev;
static struct class *poch_cls;
static DEFINE_IDR(poch_ids);

static ssize_t store_block_size(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct channel_info *channel = dev_get_drvdata(dev);
	unsigned long block_size;

	sscanf(buf, "%lu", &block_size);
	atomic_set(&channel->sys_block_size, block_size);

	return count;
}
static DEVICE_ATTR(block_size, S_IWUSR|S_IWGRP, NULL, store_block_size);

static ssize_t store_group_size(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct channel_info *channel = dev_get_drvdata(dev);
	unsigned long group_size;

	sscanf(buf, "%lu", &group_size);
	atomic_set(&channel->sys_group_size, group_size);

	return count;
}
static DEVICE_ATTR(group_size, S_IWUSR|S_IWGRP, NULL, store_group_size);

static ssize_t store_group_count(struct device *dev,
				struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct channel_info *channel = dev_get_drvdata(dev);
	unsigned long group_count;

	sscanf(buf, "%lu", &group_count);
	atomic_set(&channel->sys_group_count, group_count);

	return count;
}
static DEVICE_ATTR(group_count, S_IWUSR|S_IWGRP, NULL, store_group_count);

static ssize_t show_direction(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct channel_info *channel = dev_get_drvdata(dev);
	int len;

	len = sprintf(buf, "%s\n", (channel->dir ? "tx" : "rx"));
	return len;
}
static DEVICE_ATTR(dir, S_IRUSR|S_IRGRP, show_direction, NULL);

static unsigned long npages(unsigned long bytes)
{
	if (bytes % PAGE_SIZE == 0)
		return bytes / PAGE_SIZE;
	else
		return (bytes / PAGE_SIZE) + 1;
}

static ssize_t show_mmap_size(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct channel_info *channel = dev_get_drvdata(dev);
	int len;
	unsigned long mmap_size;
	unsigned long group_pages;
	unsigned long header_pages;
	unsigned long total_group_pages;

	group_pages = npages(channel->group_size);
	header_pages = npages(channel->header_size);
	total_group_pages = group_pages * channel->group_count;

	mmap_size = (header_pages + total_group_pages) * PAGE_SIZE;
	len = sprintf(buf, "%lu\n", mmap_size);
	return len;
}
static DEVICE_ATTR(mmap_size, S_IRUSR|S_IRGRP, show_mmap_size, NULL);

static struct device_attribute *poch_class_attrs[] = {
	&dev_attr_block_size,
	&dev_attr_group_size,
	&dev_attr_group_count,
	&dev_attr_dir,
	&dev_attr_mmap_size,
};

static void poch_channel_free_groups(struct channel_info *channel)
{
	unsigned long i;

	for (i = 0; i < channel->group_count; i++) {
		struct poch_group_info *group;
		unsigned int order;

		group = &channel->groups[i];
		order = get_order(channel->group_size);
		if (group->pg)
			__free_pages(group->pg, order);
	}
}

static int poch_channel_alloc_groups(struct channel_info *channel)
{
	unsigned long i;
	unsigned long group_pages;
	unsigned long header_pages;

	group_pages = npages(channel->group_size);
	header_pages = npages(channel->header_size);

	for (i = 0; i < channel->group_count; i++) {
		struct poch_group_info *group;
		unsigned int order;
		gfp_t gfp_mask;

		group = &channel->groups[i];
		order = get_order(channel->group_size);

		/*
		 * __GFP_COMP is required here since we are going to
		 * perform non-linear mapping to userspace. For more
		 * information read the vm_insert_page() function
		 * comments.
		 */

		gfp_mask = GFP_KERNEL | GFP_DMA32 | __GFP_ZERO;
		group->pg = alloc_pages(gfp_mask, order);
		if (!group->pg) {
			poch_channel_free_groups(channel);
			return -ENOMEM;
		}

		/* FIXME: This is the physical address not the bus
		 * address!  This won't work in architectures that
		 * have an IOMMU. Can we use pci_map_single() for
		 * this?
		 */
		group->dma_addr = page_to_pfn(group->pg) * PAGE_SIZE;
		group->user_offset =
			(header_pages + (i * group_pages)) * PAGE_SIZE;

		printk(KERN_INFO PFX "%ld: user_offset: 0x%lx\n", i,
		       group->user_offset);
	}

	return 0;
}

static int channel_latch_attr(struct channel_info *channel)
{
	channel->group_count = atomic_read(&channel->sys_group_count);
	channel->group_size = atomic_read(&channel->sys_group_size);
	channel->block_size = atomic_read(&channel->sys_block_size);

	if (channel->group_count == 0) {
		printk(KERN_ERR PFX "invalid group count %lu",
		       channel->group_count);
		return -EINVAL;
	}

	if (channel->group_size == 0 ||
	    channel->group_size < channel->block_size) {
		printk(KERN_ERR PFX "invalid group size %lu",
		       channel->group_size);
		return -EINVAL;
	}

	if (channel->block_size == 0 || (channel->block_size % 8) != 0) {
		printk(KERN_ERR PFX "invalid block size %lu",
		       channel->block_size);
		return -EINVAL;
	}

	if (channel->group_size % channel->block_size != 0) {
		printk(KERN_ERR PFX
		       "group size should be multiple of block size");
		return -EINVAL;
	}

	return 0;
}

/*
 * Configure DMA group registers
 */
static void channel_dma_init(struct channel_info *channel)
{
	void __iomem *fpga = channel->fpga_iomem;
	u32 group_regs_base;
	u32 group_reg;
	unsigned int page;
	unsigned int group_in_page;
	unsigned long i;
	u32 block_size_reg;
	u32 block_count_reg;
	u32 group_count_reg;
	u32 groups_per_int_reg;
	u32 curr_pci_reg;

	if (channel->chno == CHNO_RX_CHANNEL) {
		group_regs_base = FPGA_RX_GROUP0_START_REG;
		block_size_reg = FPGA_RX_BLOCK_SIZE_REG;
		block_count_reg = FPGA_RX_BLOCK_COUNT_REG;
		group_count_reg = FPGA_RX_GROUP_COUNT_REG;
		groups_per_int_reg = FPGA_RX_GROUPS_PER_INT_REG;
		curr_pci_reg = FPGA_RX_CURR_PCI_REG;
	} else {
		group_regs_base = FPGA_TX_GROUP0_START_REG;
		block_size_reg = FPGA_TX_BLOCK_SIZE_REG;
		block_count_reg = FPGA_TX_BLOCK_COUNT_REG;
		group_count_reg = FPGA_TX_GROUP_COUNT_REG;
		groups_per_int_reg = FPGA_TX_GROUPS_PER_INT_REG;
		curr_pci_reg = FPGA_TX_CURR_PCI_REG;
	}

	printk(KERN_WARNING "block_size, group_size, group_count\n");
	/*
	 * Block size is represented in no. of 64 bit transfers.
	 */
	iowrite32(channel->block_size / 8, fpga + block_size_reg);
	iowrite32(channel->group_size / channel->block_size,
		  fpga + block_count_reg);
	iowrite32(channel->group_count, fpga + group_count_reg);
	/* FIXME: Hardcoded groups per int. Get it from sysfs? */
	iowrite32(1, fpga + groups_per_int_reg);

	/* Unlock PCI address? Not defined in the data sheet, but used
	 * in the reference code by Redrapids.
	 */
	iowrite32(0x1, fpga + curr_pci_reg);

	/* The DMA address page register is shared between the RX and
	 * TX channels, so acquire lock.
	 */
	for (i = 0; i < channel->group_count; i++) {
		page = i / 32;
		group_in_page = i % 32;

		group_reg = group_regs_base + (group_in_page * 4);

		spin_lock(channel->iomem_lock);
		iowrite32(page, fpga + FPGA_DMA_ADR_PAGE_REG);
		iowrite32(channel->groups[i].dma_addr, fpga + group_reg);
		spin_unlock(channel->iomem_lock);
	}

	for (i = 0; i < channel->group_count; i++) {
		page = i / 32;
		group_in_page = i % 32;

		group_reg = group_regs_base + (group_in_page * 4);

		spin_lock(channel->iomem_lock);
		iowrite32(page, fpga + FPGA_DMA_ADR_PAGE_REG);
		printk(KERN_INFO PFX "%ld: read dma_addr: 0x%x\n", i,
		       ioread32(fpga + group_reg));
		spin_unlock(channel->iomem_lock);
	}

}

static int poch_channel_alloc_header(struct channel_info *channel)
{
	struct poch_cbuf_header *header = channel->header;
	unsigned long group_offset_size;
	unsigned long tot_group_offsets_size;

	/* Allocate memory to hold header exported userspace */
	group_offset_size = sizeof(header->group_offsets[0]);
	tot_group_offsets_size = group_offset_size * channel->group_count;
	channel->header_size = sizeof(*header) + tot_group_offsets_size;
	channel->header_pg = alloc_pages(GFP_KERNEL | __GFP_ZERO,
					 get_order(channel->header_size));
	if (!channel->header_pg)
		return -ENOMEM;

	channel->header = page_address(channel->header_pg);

	return 0;
}

static void poch_channel_free_header(struct channel_info *channel)
{
	unsigned int order;

	order = get_order(channel->header_size);
	__free_pages(channel->header_pg, order);
}

static void poch_channel_init_header(struct channel_info *channel)
{
	int i;
	struct poch_group_info *groups;
	s32 *group_offsets;

	channel->header->group_size_bytes = channel->group_size;
	channel->header->group_count = channel->group_count;

	spin_lock_init(&channel->group_offsets_lock);

	group_offsets = channel->header->group_offsets;
	groups = channel->groups;

	for (i = 0; i < channel->group_count; i++) {
		if (channel->dir == CHANNEL_DIR_RX)
			group_offsets[i] = -1;
		else
			group_offsets[i] = groups[i].user_offset;
	}
}

static void __poch_channel_clear_counters(struct channel_info *channel)
{
	channel->counters.pll_unlock = 0;
	channel->counters.fifo_empty = 0;
	channel->counters.fifo_overflow = 0;
}

static int poch_channel_init(struct channel_info *channel,
			     struct poch_dev *poch_dev)
{
	struct pci_dev *pdev = poch_dev->pci_dev;
	struct device *dev = &pdev->dev;
	unsigned long alloc_size;
	int ret;

	printk(KERN_WARNING "channel_latch_attr\n");

	ret = channel_latch_attr(channel);
	if (ret != 0)
		goto out;

	channel->transfer = 0;

	/* Allocate memory to hold group information. */
	alloc_size = channel->group_count * sizeof(struct poch_group_info);
	channel->groups = kzalloc(alloc_size, GFP_KERNEL);
	if (!channel->groups) {
		dev_err(dev, "error allocating memory for group info\n");
		ret = -ENOMEM;
		goto out;
	}

	printk(KERN_WARNING "poch_channel_alloc_groups\n");

	ret = poch_channel_alloc_groups(channel);
	if (ret) {
		dev_err(dev, "error allocating groups of order %d\n",
			get_order(channel->group_size));
		goto out_free_group_info;
	}

	ret = poch_channel_alloc_header(channel);
	if (ret) {
		dev_err(dev, "error allocating user space header\n");
		goto out_free_groups;
	}

	channel->fpga_iomem = poch_dev->fpga_iomem;
	channel->bridge_iomem = poch_dev->bridge_iomem;
	channel->iomem_lock = &poch_dev->iomem_lock;
	spin_lock_init(&channel->counters_lock);

	__poch_channel_clear_counters(channel);

	printk(KERN_WARNING "poch_channel_init_header\n");

	poch_channel_init_header(channel);

	return 0;

 out_free_groups:
	poch_channel_free_groups(channel);
 out_free_group_info:
	kfree(channel->groups);
 out:
	return ret;
}

static int poch_wait_fpga_prog(void __iomem *bridge)
{
	unsigned long total_wait;
	const unsigned long wait_period = 100;
	/* FIXME: Get the actual timeout */
	const unsigned long prog_timeo = 10000; /* 10 Seconds */
	u32 card_power;

	printk(KERN_WARNING "poch_wait_fpg_prog\n");

	printk(KERN_INFO PFX "programming fpga ...\n");
	total_wait = 0;
	while (1) {
		msleep(wait_period);
		total_wait += wait_period;

		card_power = ioread32(bridge + BRIDGE_CARD_POWER_REG);
		if (card_power & BRIDGE_CARD_POWER_PROG_DONE) {
			printk(KERN_INFO PFX "programming done\n");
			return 0;
		}
		if (total_wait > prog_timeo) {
			printk(KERN_ERR PFX
			       "timed out while programming FPGA\n");
			return -EIO;
		}
	}
}

static void poch_card_power_off(struct poch_dev *poch_dev)
{
	void __iomem *bridge = poch_dev->bridge_iomem;
	u32 card_power;

	iowrite32(0, bridge + BRIDGE_INT_MASK_REG);
	iowrite32(0, bridge + BRIDGE_DMA_GO_REG);

	card_power = ioread32(bridge + BRIDGE_CARD_POWER_REG);
	iowrite32(card_power & ~BRIDGE_CARD_POWER_EN,
		  bridge + BRIDGE_CARD_POWER_REG);
}

enum clk_src {
	CLK_SRC_ON_BOARD,
	CLK_SRC_EXTERNAL
};

static void poch_card_clock_on(void __iomem *fpga)
{
	/* FIXME: Get this data through sysfs? */
	enum clk_src clk_src = CLK_SRC_ON_BOARD;

	if (clk_src == CLK_SRC_ON_BOARD) {
		iowrite32(FPGA_ADC_CLOCK_LOCAL_CLK | FPGA_ADC_CLOCK_CTL_OSC_EN,
			  fpga + FPGA_ADC_CLOCK_CTL_REG);
	} else if (clk_src == CLK_SRC_EXTERNAL) {
		iowrite32(FPGA_ADC_CLOCK_EXT_SAMP_CLK,
			  fpga + FPGA_ADC_CLOCK_CTL_REG);
	}
}

static int poch_card_power_on(struct poch_dev *poch_dev)
{
	void __iomem *bridge = poch_dev->bridge_iomem;
	void __iomem *fpga = poch_dev->fpga_iomem;

	iowrite32(BRIDGE_CARD_POWER_EN, bridge + BRIDGE_CARD_POWER_REG);

	if (poch_wait_fpga_prog(bridge) != 0) {
		poch_card_power_off(poch_dev);
		return -EIO;
	}

	poch_card_clock_on(fpga);

	/* Sync to new clock, reset state machines, set DMA mode. */
	iowrite32(FPGA_DOM_DCM_RESET | FPGA_DOM_SOFT_RESET
		  | FPGA_DOM_DUAL_M_SG_DMA, fpga + FPGA_DOM_REG);

	/* FIXME: The time required for sync. needs to be tuned. */
	msleep(1000);

	return 0;
}

static void poch_channel_analog_on(struct channel_info *channel)
{
	void __iomem *fpga = channel->fpga_iomem;
	u32 adc_dac_en;

	spin_lock(channel->iomem_lock);
	adc_dac_en = ioread32(fpga + FPGA_ADC_DAC_EN_REG);
	switch (channel->chno) {
	case CHNO_RX_CHANNEL:
		iowrite32(adc_dac_en & ~FPGA_ADC_DAC_EN_ADC_OFF,
			  fpga + FPGA_ADC_DAC_EN_REG);
		break;
	case CHNO_TX_CHANNEL:
		iowrite32(adc_dac_en & ~FPGA_ADC_DAC_EN_DAC_OFF,
			  fpga + FPGA_ADC_DAC_EN_REG);
		break;
	}
	spin_unlock(channel->iomem_lock);
}

static int poch_open(struct inode *inode, struct file *filp)
{
	struct poch_dev *poch_dev;
	struct channel_info *channel;
	void __iomem *bridge;
	void __iomem *fpga;
	int chno;
	int usage;
	int ret;

	poch_dev = container_of(inode->i_cdev, struct poch_dev, cdev);
	bridge = poch_dev->bridge_iomem;
	fpga = poch_dev->fpga_iomem;

	chno = iminor(inode) % poch_dev->nchannels;
	channel = &poch_dev->channels[chno];

	if (!atomic_dec_and_test(&channel->free)) {
		atomic_inc(&channel->free);
		ret = -EBUSY;
		goto out;
	}

	usage = atomic_inc_return(&poch_dev->usage);

	printk(KERN_WARNING "poch_card_power_on\n");

	if (usage == 1) {
		ret = poch_card_power_on(poch_dev);
		if (ret)
			goto out_dec_usage;
	}

	printk(KERN_INFO "CardBus Bridge Revision: %x\n",
	       ioread32(bridge + BRIDGE_REV_REG));
	printk(KERN_INFO "CardBus Interface Revision: %x\n",
	       ioread32(fpga + FPGA_IFACE_REV_REG));

	channel->chno = chno;
	filp->private_data = channel;

	printk(KERN_WARNING "poch_channel_init\n");

	ret = poch_channel_init(channel, poch_dev);
	if (ret)
		goto out_power_off;

	poch_channel_analog_on(channel);

	printk(KERN_WARNING "channel_dma_init\n");

	channel_dma_init(channel);

	printk(KERN_WARNING "poch_channel_analog_on\n");

	if (usage == 1) {
		printk(KERN_WARNING "setting up DMA\n");

		/* Initialize DMA Controller. */
		iowrite32(FPGA_CAP_FIFO_REG, bridge + BRIDGE_STAT_2_REG);
		iowrite32(FPGA_DMA_DESC_1_REG, bridge + BRIDGE_STAT_3_REG);

		ioread32(fpga + FPGA_DMA_INT_STAT_REG);
		ioread32(fpga + FPGA_INT_STAT_REG);
		ioread32(bridge + BRIDGE_INT_STAT_REG);

		/* Initialize Interrupts. FIXME: Enable temperature
		 * handling We are enabling both Tx and Rx channel
		 * interrupts here. Do we need to enable interrupts
		 * only for the current channel? Anyways we won't get
		 * the interrupt unless the DMA is activated.
		 */
		iowrite32(BRIDGE_INT_FPGA, bridge + BRIDGE_INT_MASK_REG);
		iowrite32(FPGA_INT_DMA_CORE
			  | FPGA_INT_PLL_UNLOCKED
			  | FPGA_INT_TX_FF_EMPTY
			  | FPGA_INT_RX_FF_EMPTY
			  | FPGA_INT_TX_FF_OVRFLW
			  | FPGA_INT_RX_FF_OVRFLW,
			  fpga + FPGA_INT_MASK_REG);
		iowrite32(FPGA_DMA_INT_RX | FPGA_DMA_INT_TX,
			  fpga + FPGA_DMA_INT_MASK_REG);
	}

	if (channel->dir == CHANNEL_DIR_TX) {
		/* Flush TX FIFO and output data from cardbus. */
		iowrite32(FPGA_TX_CTL_FIFO_FLUSH
			  | FPGA_TX_CTL_OUTPUT_CARDBUS,
			  fpga + FPGA_TX_CTL_REG);
	} else {
		/* Flush RX FIFO and output data to cardbus. */
		iowrite32(FPGA_RX_CTL_CONT_CAP
			  | FPGA_RX_CTL_FIFO_FLUSH,
			  fpga + FPGA_RX_CTL_REG);
	}

	atomic_inc(&channel->inited);

	return 0;

 out_power_off:
	if (usage == 1)
		poch_card_power_off(poch_dev);
 out_dec_usage:
	atomic_dec(&poch_dev->usage);
	atomic_inc(&channel->free);
 out:
	return ret;
}

static int poch_release(struct inode *inode, struct file *filp)
{
	struct channel_info *channel = filp->private_data;
	struct poch_dev *poch_dev;
	int usage;

	poch_dev = container_of(inode->i_cdev, struct poch_dev, cdev);

	usage = atomic_dec_return(&poch_dev->usage);
	if (usage == 0) {
		printk(KERN_WARNING "poch_card_power_off\n");
		poch_card_power_off(poch_dev);
	}

	atomic_dec(&channel->inited);
	poch_channel_free_header(channel);
	poch_channel_free_groups(channel);
	kfree(channel->groups);
	atomic_inc(&channel->free);

	return 0;
}

/*
 * Map the header and the group buffers, to user space.
 */
static int poch_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct channel_info *channel = filp->private_data;

	unsigned long start;
	unsigned long size;

	unsigned long group_pages;
	unsigned long header_pages;
	unsigned long total_group_pages;

	int pg_num;
	struct page *pg;

	int i;
	int ret;

	printk(KERN_WARNING "poch_mmap\n");

	if (vma->vm_pgoff) {
		printk(KERN_WARNING PFX "page offset: %lu\n", vma->vm_pgoff);
		return -EINVAL;
	}

	group_pages = npages(channel->group_size);
	header_pages = npages(channel->header_size);
	total_group_pages = group_pages * channel->group_count;

	size = vma->vm_end - vma->vm_start;
	if (size != (header_pages + total_group_pages) * PAGE_SIZE) {
		printk(KERN_WARNING PFX "required %lu bytes\n", size);
		return -EINVAL;
	}

	start = vma->vm_start;

	/* FIXME: Cleanup required on failure? */
	pg = channel->header_pg;
	for (pg_num = 0; pg_num < header_pages; pg_num++, pg++) {
		printk(KERN_DEBUG PFX "page_count: %d\n", page_count(pg));
		printk(KERN_DEBUG PFX "%d: header: 0x%lx\n", pg_num, start);
		ret = vm_insert_page(vma, start, pg);
		if (ret) {
			printk(KERN_DEBUG "vm_insert 1 failed at %lx\n", start);
			return ret;
		}
		start += PAGE_SIZE;
	}

	for (i = 0; i < channel->group_count; i++) {
		pg = channel->groups[i].pg;
		for (pg_num = 0; pg_num < group_pages; pg_num++, pg++) {
			printk(KERN_DEBUG PFX "%d: group %d: 0x%lx\n",
			       pg_num, i, start);
			ret = vm_insert_page(vma, start, pg);
			if (ret) {
				printk(KERN_DEBUG PFX
				       "vm_insert 2 failed at %d\n", pg_num);
				return ret;
			}
			start += PAGE_SIZE;
		}
	}

	return 0;
}

/*
 * Check whether there is some group that the user space has not
 * consumed yet. When the user space consumes a group, it sets it to
 * -1. Cosuming could be reading data in case of RX and filling a
 * buffer in case of TX.
 */
static int poch_channel_available(struct channel_info *channel)
{
	int i;

	spin_lock_irq(&channel->group_offsets_lock);

	for (i = 0; i < channel->group_count; i++) {
		if (channel->header->group_offsets[i] != -1) {
			spin_unlock_irq(&channel->group_offsets_lock);
			return 1;
		}
	}

	spin_unlock_irq(&channel->group_offsets_lock);

	return 0;
}

static unsigned int poch_poll(struct file *filp, poll_table *pt)
{
	struct channel_info *channel = filp->private_data;
	unsigned int ret = 0;

	poll_wait(filp, &channel->wq, pt);

	if (poch_channel_available(channel)) {
		if (channel->dir == CHANNEL_DIR_RX)
			ret = POLLIN | POLLRDNORM;
		else
			ret = POLLOUT | POLLWRNORM;
	}

	return ret;
}

static int poch_ioctl(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
	struct channel_info *channel = filp->private_data;
	void __iomem *fpga = channel->fpga_iomem;
	void __iomem *bridge = channel->bridge_iomem;
	void __user *argp = (void __user *)arg;
	struct vm_area_struct *vms;
	struct poch_counters counters;
	int ret;

	switch (cmd) {
	case POCH_IOC_TRANSFER_START:
		switch (channel->chno) {
		case CHNO_TX_CHANNEL:
			printk(KERN_INFO PFX "ioctl: Tx start\n");
			iowrite32(0x1, fpga + FPGA_TX_TRIGGER_REG);
			iowrite32(0x1, fpga + FPGA_TX_ENABLE_REG);

			/* FIXME: Does it make sense to do a DMA GO
			 * twice, once in Tx and once in Rx.
			 */
			iowrite32(0x1, bridge + BRIDGE_DMA_GO_REG);
			break;
		case CHNO_RX_CHANNEL:
			printk(KERN_INFO PFX "ioctl: Rx start\n");
			iowrite32(0x1, fpga + FPGA_RX_ARM_REG);
			iowrite32(0x1, bridge + BRIDGE_DMA_GO_REG);
			break;
		}
		break;
	case POCH_IOC_TRANSFER_STOP:
		switch (channel->chno) {
		case CHNO_TX_CHANNEL:
			printk(KERN_INFO PFX "ioctl: Tx stop\n");
			iowrite32(0x0, fpga + FPGA_TX_ENABLE_REG);
			iowrite32(0x0, fpga + FPGA_TX_TRIGGER_REG);
			iowrite32(0x0, bridge + BRIDGE_DMA_GO_REG);
			break;
		case CHNO_RX_CHANNEL:
			printk(KERN_INFO PFX "ioctl: Rx stop\n");
			iowrite32(0x0, fpga + FPGA_RX_ARM_REG);
			iowrite32(0x0, bridge + BRIDGE_DMA_GO_REG);
			break;
		}
		break;
	case POCH_IOC_GET_COUNTERS:
		if (!access_ok(VERIFY_WRITE, argp, sizeof(struct poch_counters)))
			return -EFAULT;

		spin_lock_irq(&channel->counters_lock);
		counters = channel->counters;
		__poch_channel_clear_counters(channel);
		spin_unlock_irq(&channel->counters_lock);

		ret = copy_to_user(argp, &counters,
				   sizeof(struct poch_counters));
		if (ret)
			return ret;

		break;
	case POCH_IOC_SYNC_GROUP_FOR_USER:
	case POCH_IOC_SYNC_GROUP_FOR_DEVICE:
		vms = find_vma(current->mm, arg);
		if (!vms)
			/* Address not mapped. */
			return -EINVAL;
		if (vms->vm_file != filp)
			/* Address mapped from different device/file. */
			return -EINVAL;

		flush_cache_range(vms, arg, arg + channel->group_size);
		break;
	}
	return 0;
}

static struct file_operations poch_fops = {
	.owner = THIS_MODULE,
	.open = poch_open,
	.release = poch_release,
	.ioctl = poch_ioctl,
	.poll = poch_poll,
	.mmap = poch_mmap
};

static void poch_irq_dma(struct channel_info *channel)
{
	u32 prev_transfer;
	u32 curr_transfer;
	long groups_done;
	unsigned long i, j;
	struct poch_group_info *groups;
	s32 *group_offsets;
	u32 curr_group_reg;

	if (!atomic_read(&channel->inited))
		return;

	prev_transfer = channel->transfer;

	if (channel->chno == CHNO_RX_CHANNEL)
		curr_group_reg = FPGA_RX_CURR_GROUP_REG;
	else
		curr_group_reg = FPGA_TX_CURR_GROUP_REG;

	curr_transfer = ioread32(channel->fpga_iomem + curr_group_reg);

	groups_done = curr_transfer - prev_transfer;
	/* Check wrap over, and handle it. */
	if (groups_done <= 0)
		groups_done += channel->group_count;

	group_offsets = channel->header->group_offsets;
	groups = channel->groups;

	spin_lock(&channel->group_offsets_lock);

	for (i = 0; i < groups_done; i++) {
		j = (prev_transfer + i) % channel->group_count;
		group_offsets[j] = groups[j].user_offset;
	}

	spin_unlock(&channel->group_offsets_lock);

	channel->transfer = curr_transfer;

	wake_up_interruptible(&channel->wq);
}

static irqreturn_t poch_irq_handler(int irq, void *p)
{
	struct poch_dev *poch_dev = p;
	void __iomem *bridge = poch_dev->bridge_iomem;
	void __iomem *fpga = poch_dev->fpga_iomem;
	struct channel_info *channel_rx = &poch_dev->channels[CHNO_RX_CHANNEL];
	struct channel_info *channel_tx = &poch_dev->channels[CHNO_TX_CHANNEL];
	u32 bridge_stat;
	u32 fpga_stat;
	u32 dma_stat;

	bridge_stat = ioread32(bridge + BRIDGE_INT_STAT_REG);
	fpga_stat = ioread32(fpga + FPGA_INT_STAT_REG);
	dma_stat = ioread32(fpga + FPGA_DMA_INT_STAT_REG);

	ioread32(fpga + FPGA_DMA_INT_STAT_REG);
	ioread32(fpga + FPGA_INT_STAT_REG);
	ioread32(bridge + BRIDGE_INT_STAT_REG);

	if (bridge_stat & BRIDGE_INT_FPGA) {
		if (fpga_stat & FPGA_INT_DMA_CORE) {
			if (dma_stat & FPGA_DMA_INT_RX)
				poch_irq_dma(channel_rx);
			if (dma_stat & FPGA_DMA_INT_TX)
				poch_irq_dma(channel_tx);
		}
		if (fpga_stat & FPGA_INT_PLL_UNLOCKED) {
			channel_tx->counters.pll_unlock++;
			channel_rx->counters.pll_unlock++;
			if (printk_ratelimit())
				printk(KERN_WARNING PFX "PLL unlocked\n");
		}
		if (fpga_stat & FPGA_INT_TX_FF_EMPTY)
			channel_tx->counters.fifo_empty++;
		if (fpga_stat & FPGA_INT_TX_FF_OVRFLW)
			channel_tx->counters.fifo_overflow++;
		if (fpga_stat & FPGA_INT_RX_FF_EMPTY)
			channel_rx->counters.fifo_empty++;
		if (fpga_stat & FPGA_INT_RX_FF_OVRFLW)
			channel_rx->counters.fifo_overflow++;

		/*
		 * FIXME: These errors should be notified through the
		 * poll interface as POLLERR.
		 */

		/* Re-enable interrupts. */
		iowrite32(BRIDGE_INT_FPGA, bridge + BRIDGE_INT_MASK_REG);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void poch_class_dev_unregister(struct poch_dev *poch_dev, int id)
{
	int i, j;
	int nattrs;
	struct channel_info *channel;
	dev_t devno;

	if (poch_dev->dev == NULL)
		return;

	for (i = 0; i < poch_dev->nchannels; i++) {
		channel = &poch_dev->channels[i];
		devno = poch_first_dev + (id * poch_dev->nchannels) + i;

		if (!channel->dev)
			continue;

		nattrs = sizeof(poch_class_attrs)/sizeof(poch_class_attrs[0]);
		for (j = 0; j < nattrs; j++)
			device_remove_file(channel->dev, poch_class_attrs[j]);

		device_unregister(channel->dev);
	}

	device_unregister(poch_dev->dev);
}

static int __devinit poch_class_dev_register(struct poch_dev *poch_dev,
					     int id)
{
	struct device *dev = &poch_dev->pci_dev->dev;
	int i, j;
	int nattrs;
	int ret;
	struct channel_info *channel;
	dev_t devno;

	poch_dev->dev = device_create(poch_cls, &poch_dev->pci_dev->dev,
				      MKDEV(0, 0), NULL, "poch%d", id);
	if (IS_ERR(poch_dev->dev)) {
		dev_err(dev, "error creating parent class device");
		ret = PTR_ERR(poch_dev->dev);
		poch_dev->dev = NULL;
		return ret;
	}

	for (i = 0; i < poch_dev->nchannels; i++) {
		channel = &poch_dev->channels[i];

		devno = poch_first_dev + (id * poch_dev->nchannels) + i;
		channel->dev = device_create(poch_cls, poch_dev->dev, devno,
					     NULL, "ch%d", i);
		if (IS_ERR(channel->dev)) {
			dev_err(dev, "error creating channel class device");
			ret = PTR_ERR(channel->dev);
			channel->dev = NULL;
			poch_class_dev_unregister(poch_dev, id);
			return ret;
		}

		dev_set_drvdata(channel->dev, channel);
		nattrs = sizeof(poch_class_attrs)/sizeof(poch_class_attrs[0]);
		for (j = 0; j < nattrs; j++) {
			ret = device_create_file(channel->dev,
						 poch_class_attrs[j]);
			if (ret) {
				dev_err(dev, "error creating attribute file");
				poch_class_dev_unregister(poch_dev, id);
				return ret;
			}
		}
	}

	return 0;
}

static int __devinit poch_pci_probe(struct pci_dev *pdev,
				    const struct pci_device_id *pci_id)
{
	struct device *dev = &pdev->dev;
	struct poch_dev *poch_dev;
	struct uio_info *uio;
	int ret;
	int id;
	int i;

	poch_dev = kzalloc(sizeof(struct poch_dev), GFP_KERNEL);
	if (!poch_dev) {
		dev_err(dev, "error allocating priv. data memory\n");
		return -ENOMEM;
	}

	poch_dev->pci_dev = pdev;
	uio = &poch_dev->uio;

	pci_set_drvdata(pdev, poch_dev);

	spin_lock_init(&poch_dev->iomem_lock);

	poch_dev->nchannels = POCH_NCHANNELS;
	poch_dev->channels[CHNO_RX_CHANNEL].dir = CHANNEL_DIR_RX;
	poch_dev->channels[CHNO_TX_CHANNEL].dir = CHANNEL_DIR_TX;

	for (i = 0; i < poch_dev->nchannels; i++) {
		init_waitqueue_head(&poch_dev->channels[i].wq);
		atomic_set(&poch_dev->channels[i].free, 1);
		atomic_set(&poch_dev->channels[i].inited, 0);
	}

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(dev, "error enabling device\n");
		goto out_free;
	}

	ret = pci_request_regions(pdev, "poch");
	if (ret) {
		dev_err(dev, "error requesting resources\n");
		goto out_disable;
	}

	uio->mem[0].addr = pci_resource_start(pdev, 1);
	if (!uio->mem[0].addr) {
		dev_err(dev, "invalid BAR1\n");
		ret = -ENODEV;
		goto out_release;
	}

	uio->mem[0].size = pci_resource_len(pdev, 1);
	uio->mem[0].memtype = UIO_MEM_PHYS;

	uio->name = "poch";
	uio->version = "0.0.1";
	uio->irq = -1;
	ret = uio_register_device(dev, uio);
	if (ret) {
		dev_err(dev, "error register UIO device: %d\n", ret);
		goto out_release;
	}

	poch_dev->bridge_iomem = ioremap(pci_resource_start(pdev, 0),
					 pci_resource_len(pdev, 0));
	if (poch_dev->bridge_iomem == NULL) {
		dev_err(dev, "error mapping bridge (bar0) registers\n");
		ret = -ENOMEM;
		goto out_uio_unreg;
	}

	poch_dev->fpga_iomem = ioremap(pci_resource_start(pdev, 1),
				       pci_resource_len(pdev, 1));
	if (poch_dev->fpga_iomem == NULL) {
		dev_err(dev, "error mapping fpga (bar1) registers\n");
		ret = -ENOMEM;
		goto out_bar0_unmap;
	}

	ret = request_irq(pdev->irq, poch_irq_handler, IRQF_SHARED,
			  dev_name(dev), poch_dev);
	if (ret) {
		dev_err(dev, "error requesting IRQ %u\n", pdev->irq);
		ret = -ENOMEM;
		goto out_bar1_unmap;
	}

	if (!idr_pre_get(&poch_ids, GFP_KERNEL)) {
		dev_err(dev, "error allocating memory ids\n");
		ret = -ENOMEM;
		goto out_free_irq;
	}

	idr_get_new(&poch_ids, poch_dev, &id);
	if (id >= MAX_POCH_CARDS) {
		dev_err(dev, "minors exhausted\n");
		ret = -EBUSY;
		goto out_free_irq;
	}

	cdev_init(&poch_dev->cdev, &poch_fops);
	poch_dev->cdev.owner = THIS_MODULE;
	ret = cdev_add(&poch_dev->cdev,
		       poch_first_dev + (id * poch_dev->nchannels),
		       poch_dev->nchannels);
	if (ret) {
		dev_err(dev, "error register character device\n");
		goto out_idr_remove;
	}

	ret = poch_class_dev_register(poch_dev, id);
	if (ret)
		goto out_cdev_del;

	return 0;

 out_cdev_del:
	cdev_del(&poch_dev->cdev);
 out_idr_remove:
	idr_remove(&poch_ids, id);
 out_free_irq:
	free_irq(pdev->irq, poch_dev);
 out_bar1_unmap:
	iounmap(poch_dev->fpga_iomem);
 out_bar0_unmap:
	iounmap(poch_dev->bridge_iomem);
 out_uio_unreg:
	uio_unregister_device(uio);
 out_release:
	pci_release_regions(pdev);
 out_disable:
	pci_disable_device(pdev);
 out_free:
	kfree(poch_dev);
	return ret;
}

/*
 * FIXME: We are yet to handle the hot unplug case.
 */
static void poch_pci_remove(struct pci_dev *pdev)
{
	struct poch_dev *poch_dev = pci_get_drvdata(pdev);
	struct uio_info *uio = &poch_dev->uio;
	unsigned int minor = MINOR(poch_dev->cdev.dev);
	unsigned int id = minor / poch_dev->nchannels;

	poch_class_dev_unregister(poch_dev, id);
	cdev_del(&poch_dev->cdev);
	idr_remove(&poch_ids, id);
	free_irq(pdev->irq, poch_dev);
	iounmap(poch_dev->fpga_iomem);
	iounmap(poch_dev->bridge_iomem);
	uio_unregister_device(uio);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	iounmap(uio->mem[0].internal_addr);

	kfree(poch_dev);
}

static const struct pci_device_id poch_pci_ids[] /* __devinitconst */ = {
	{ PCI_DEVICE(PCI_VENDOR_ID_RRAPIDS,
		     PCI_DEVICE_ID_RRAPIDS_POCKET_CHANGE) },
	{ 0, }
};

static struct pci_driver poch_pci_driver = {
	.name = DRV_NAME,
	.id_table = poch_pci_ids,
	.probe = poch_pci_probe,
	.remove = poch_pci_remove,
};

static int __init poch_init_module(void)
{
	int ret = 0;

	ret = alloc_chrdev_region(&poch_first_dev, 0,
				  MAX_POCH_DEVICES, DRV_NAME);
	if (ret) {
		printk(KERN_ERR PFX "error allocating device no.");
		return ret;
	}

	poch_cls = class_create(THIS_MODULE, "pocketchange");
	if (IS_ERR(poch_cls)) {
		ret = PTR_ERR(poch_cls);
		goto out_unreg_chrdev;
	}

	ret = pci_register_driver(&poch_pci_driver);
	if (ret) {
		printk(KERN_ERR PFX "error register PCI device");
		goto out_class_destroy;
	}

	return 0;

 out_class_destroy:
	class_destroy(poch_cls);

 out_unreg_chrdev:
	unregister_chrdev_region(poch_first_dev, MAX_POCH_DEVICES);

	return ret;
}

static void __exit poch_exit_module(void)
{
	pci_unregister_driver(&poch_pci_driver);
	class_destroy(poch_cls);
	unregister_chrdev_region(poch_first_dev, MAX_POCH_DEVICES);
}

module_init(poch_init_module);
module_exit(poch_exit_module);

MODULE_LICENSE("GPL v2");
