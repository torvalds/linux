// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ZynqMP DPDMA Engine driver
 *
 * Copyright (C) 2015 - 2020 Xilinx, Inc.
 *
 * Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dma/xilinx_dpdma.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <dt-bindings/dma/xlnx-zynqmp-dpdma.h>

#include "../dmaengine.h"
#include "../virt-dma.h"

/* DPDMA registers */
#define XILINX_DPDMA_ERR_CTRL				0x000
#define XILINX_DPDMA_ISR				0x004
#define XILINX_DPDMA_IMR				0x008
#define XILINX_DPDMA_IEN				0x00c
#define XILINX_DPDMA_IDS				0x010
#define XILINX_DPDMA_INTR_DESC_DONE(n)			BIT((n) + 0)
#define XILINX_DPDMA_INTR_DESC_DONE_MASK		GENMASK(5, 0)
#define XILINX_DPDMA_INTR_NO_OSTAND(n)			BIT((n) + 6)
#define XILINX_DPDMA_INTR_NO_OSTAND_MASK		GENMASK(11, 6)
#define XILINX_DPDMA_INTR_AXI_ERR(n)			BIT((n) + 12)
#define XILINX_DPDMA_INTR_AXI_ERR_MASK			GENMASK(17, 12)
#define XILINX_DPDMA_INTR_DESC_ERR(n)			BIT((n) + 16)
#define XILINX_DPDMA_INTR_DESC_ERR_MASK			GENMASK(23, 18)
#define XILINX_DPDMA_INTR_WR_CMD_FIFO_FULL		BIT(24)
#define XILINX_DPDMA_INTR_WR_DATA_FIFO_FULL		BIT(25)
#define XILINX_DPDMA_INTR_AXI_4K_CROSS			BIT(26)
#define XILINX_DPDMA_INTR_VSYNC				BIT(27)
#define XILINX_DPDMA_INTR_CHAN_ERR_MASK			0x00041000
#define XILINX_DPDMA_INTR_CHAN_ERR			0x00fff000
#define XILINX_DPDMA_INTR_GLOBAL_ERR			0x07000000
#define XILINX_DPDMA_INTR_ERR_ALL			0x07fff000
#define XILINX_DPDMA_INTR_CHAN_MASK			0x00041041
#define XILINX_DPDMA_INTR_GLOBAL_MASK			0x0f000000
#define XILINX_DPDMA_INTR_ALL				0x0fffffff
#define XILINX_DPDMA_EISR				0x014
#define XILINX_DPDMA_EIMR				0x018
#define XILINX_DPDMA_EIEN				0x01c
#define XILINX_DPDMA_EIDS				0x020
#define XILINX_DPDMA_EINTR_INV_APB			BIT(0)
#define XILINX_DPDMA_EINTR_RD_AXI_ERR(n)		BIT((n) + 1)
#define XILINX_DPDMA_EINTR_RD_AXI_ERR_MASK		GENMASK(6, 1)
#define XILINX_DPDMA_EINTR_PRE_ERR(n)			BIT((n) + 7)
#define XILINX_DPDMA_EINTR_PRE_ERR_MASK			GENMASK(12, 7)
#define XILINX_DPDMA_EINTR_CRC_ERR(n)			BIT((n) + 13)
#define XILINX_DPDMA_EINTR_CRC_ERR_MASK			GENMASK(18, 13)
#define XILINX_DPDMA_EINTR_WR_AXI_ERR(n)		BIT((n) + 19)
#define XILINX_DPDMA_EINTR_WR_AXI_ERR_MASK		GENMASK(24, 19)
#define XILINX_DPDMA_EINTR_DESC_DONE_ERR(n)		BIT((n) + 25)
#define XILINX_DPDMA_EINTR_DESC_DONE_ERR_MASK		GENMASK(30, 25)
#define XILINX_DPDMA_EINTR_RD_CMD_FIFO_FULL		BIT(32)
#define XILINX_DPDMA_EINTR_CHAN_ERR_MASK		0x02082082
#define XILINX_DPDMA_EINTR_CHAN_ERR			0x7ffffffe
#define XILINX_DPDMA_EINTR_GLOBAL_ERR			0x80000001
#define XILINX_DPDMA_EINTR_ALL				0xffffffff
#define XILINX_DPDMA_CNTL				0x100
#define XILINX_DPDMA_GBL				0x104
#define XILINX_DPDMA_GBL_TRIG_MASK(n)			((n) << 0)
#define XILINX_DPDMA_GBL_RETRIG_MASK(n)			((n) << 6)
#define XILINX_DPDMA_ALC0_CNTL				0x108
#define XILINX_DPDMA_ALC0_STATUS			0x10c
#define XILINX_DPDMA_ALC0_MAX				0x110
#define XILINX_DPDMA_ALC0_MIN				0x114
#define XILINX_DPDMA_ALC0_ACC				0x118
#define XILINX_DPDMA_ALC0_ACC_TRAN			0x11c
#define XILINX_DPDMA_ALC1_CNTL				0x120
#define XILINX_DPDMA_ALC1_STATUS			0x124
#define XILINX_DPDMA_ALC1_MAX				0x128
#define XILINX_DPDMA_ALC1_MIN				0x12c
#define XILINX_DPDMA_ALC1_ACC				0x130
#define XILINX_DPDMA_ALC1_ACC_TRAN			0x134

/* Channel register */
#define XILINX_DPDMA_CH_BASE				0x200
#define XILINX_DPDMA_CH_OFFSET				0x100
#define XILINX_DPDMA_CH_DESC_START_ADDRE		0x000
#define XILINX_DPDMA_CH_DESC_START_ADDRE_MASK		GENMASK(15, 0)
#define XILINX_DPDMA_CH_DESC_START_ADDR			0x004
#define XILINX_DPDMA_CH_DESC_NEXT_ADDRE			0x008
#define XILINX_DPDMA_CH_DESC_NEXT_ADDR			0x00c
#define XILINX_DPDMA_CH_PYLD_CUR_ADDRE			0x010
#define XILINX_DPDMA_CH_PYLD_CUR_ADDR			0x014
#define XILINX_DPDMA_CH_CNTL				0x018
#define XILINX_DPDMA_CH_CNTL_ENABLE			BIT(0)
#define XILINX_DPDMA_CH_CNTL_PAUSE			BIT(1)
#define XILINX_DPDMA_CH_CNTL_QOS_DSCR_WR_MASK		GENMASK(5, 2)
#define XILINX_DPDMA_CH_CNTL_QOS_DSCR_RD_MASK		GENMASK(9, 6)
#define XILINX_DPDMA_CH_CNTL_QOS_DATA_RD_MASK		GENMASK(13, 10)
#define XILINX_DPDMA_CH_CNTL_QOS_VID_CLASS		11
#define XILINX_DPDMA_CH_STATUS				0x01c
#define XILINX_DPDMA_CH_STATUS_OTRAN_CNT_MASK		GENMASK(24, 21)
#define XILINX_DPDMA_CH_VDO				0x020
#define XILINX_DPDMA_CH_PYLD_SZ				0x024
#define XILINX_DPDMA_CH_DESC_ID				0x028
#define XILINX_DPDMA_CH_DESC_ID_MASK			GENMASK(15, 0)

/* DPDMA descriptor fields */
#define XILINX_DPDMA_DESC_CONTROL_PREEMBLE		0xa5
#define XILINX_DPDMA_DESC_CONTROL_COMPLETE_INTR		BIT(8)
#define XILINX_DPDMA_DESC_CONTROL_DESC_UPDATE		BIT(9)
#define XILINX_DPDMA_DESC_CONTROL_IGNORE_DONE		BIT(10)
#define XILINX_DPDMA_DESC_CONTROL_FRAG_MODE		BIT(18)
#define XILINX_DPDMA_DESC_CONTROL_LAST			BIT(19)
#define XILINX_DPDMA_DESC_CONTROL_ENABLE_CRC		BIT(20)
#define XILINX_DPDMA_DESC_CONTROL_LAST_OF_FRAME		BIT(21)
#define XILINX_DPDMA_DESC_ID_MASK			GENMASK(15, 0)
#define XILINX_DPDMA_DESC_HSIZE_STRIDE_HSIZE_MASK	GENMASK(17, 0)
#define XILINX_DPDMA_DESC_HSIZE_STRIDE_STRIDE_MASK	GENMASK(31, 18)
#define XILINX_DPDMA_DESC_ADDR_EXT_NEXT_ADDR_MASK	GENMASK(15, 0)
#define XILINX_DPDMA_DESC_ADDR_EXT_SRC_ADDR_MASK	GENMASK(31, 16)

#define XILINX_DPDMA_ALIGN_BYTES			256
#define XILINX_DPDMA_LINESIZE_ALIGN_BITS		128

#define XILINX_DPDMA_NUM_CHAN				6

struct xilinx_dpdma_chan;

/**
 * struct xilinx_dpdma_hw_desc - DPDMA hardware descriptor
 * @control: control configuration field
 * @desc_id: descriptor ID
 * @xfer_size: transfer size
 * @hsize_stride: horizontal size and stride
 * @timestamp_lsb: LSB of time stamp
 * @timestamp_msb: MSB of time stamp
 * @addr_ext: upper 16 bit of 48 bit address (next_desc and src_addr)
 * @next_desc: next descriptor 32 bit address
 * @src_addr: payload source address (1st page, 32 LSB)
 * @addr_ext_23: payload source address (3nd and 3rd pages, 16 LSBs)
 * @addr_ext_45: payload source address (4th and 5th pages, 16 LSBs)
 * @src_addr2: payload source address (2nd page, 32 LSB)
 * @src_addr3: payload source address (3rd page, 32 LSB)
 * @src_addr4: payload source address (4th page, 32 LSB)
 * @src_addr5: payload source address (5th page, 32 LSB)
 * @crc: descriptor CRC
 */
struct xilinx_dpdma_hw_desc {
	u32 control;
	u32 desc_id;
	u32 xfer_size;
	u32 hsize_stride;
	u32 timestamp_lsb;
	u32 timestamp_msb;
	u32 addr_ext;
	u32 next_desc;
	u32 src_addr;
	u32 addr_ext_23;
	u32 addr_ext_45;
	u32 src_addr2;
	u32 src_addr3;
	u32 src_addr4;
	u32 src_addr5;
	u32 crc;
} __aligned(XILINX_DPDMA_ALIGN_BYTES);

/**
 * struct xilinx_dpdma_sw_desc - DPDMA software descriptor
 * @hw: DPDMA hardware descriptor
 * @node: list node for software descriptors
 * @dma_addr: DMA address of the software descriptor
 */
struct xilinx_dpdma_sw_desc {
	struct xilinx_dpdma_hw_desc hw;
	struct list_head node;
	dma_addr_t dma_addr;
};

/**
 * struct xilinx_dpdma_tx_desc - DPDMA transaction descriptor
 * @vdesc: virtual DMA descriptor
 * @chan: DMA channel
 * @descriptors: list of software descriptors
 * @error: an error has been detected with this descriptor
 */
struct xilinx_dpdma_tx_desc {
	struct virt_dma_desc vdesc;
	struct xilinx_dpdma_chan *chan;
	struct list_head descriptors;
	bool error;
};

#define to_dpdma_tx_desc(_desc) \
	container_of(_desc, struct xilinx_dpdma_tx_desc, vdesc)

/**
 * struct xilinx_dpdma_chan - DPDMA channel
 * @vchan: virtual DMA channel
 * @reg: register base address
 * @id: channel ID
 * @wait_to_stop: queue to wait for outstanding transacitons before stopping
 * @running: true if the channel is running
 * @first_frame: flag for the first frame of stream
 * @video_group: flag if multi-channel operation is needed for video channels
 * @lock: lock to access struct xilinx_dpdma_chan
 * @desc_pool: descriptor allocation pool
 * @err_task: error IRQ bottom half handler
 * @desc: References to descriptors being processed
 * @desc.pending: Descriptor schedule to the hardware, pending execution
 * @desc.active: Descriptor being executed by the hardware
 * @xdev: DPDMA device
 */
struct xilinx_dpdma_chan {
	struct virt_dma_chan vchan;
	void __iomem *reg;
	unsigned int id;

	wait_queue_head_t wait_to_stop;
	bool running;
	bool first_frame;
	bool video_group;

	spinlock_t lock; /* lock to access struct xilinx_dpdma_chan */
	struct dma_pool *desc_pool;
	struct tasklet_struct err_task;

	struct {
		struct xilinx_dpdma_tx_desc *pending;
		struct xilinx_dpdma_tx_desc *active;
	} desc;

	struct xilinx_dpdma_device *xdev;
};

#define to_xilinx_chan(_chan) \
	container_of(_chan, struct xilinx_dpdma_chan, vchan.chan)

/**
 * struct xilinx_dpdma_device - DPDMA device
 * @common: generic dma device structure
 * @reg: register base address
 * @dev: generic device structure
 * @irq: the interrupt number
 * @axi_clk: axi clock
 * @chan: DPDMA channels
 * @ext_addr: flag for 64 bit system (48 bit addressing)
 */
struct xilinx_dpdma_device {
	struct dma_device common;
	void __iomem *reg;
	struct device *dev;
	int irq;

	struct clk *axi_clk;
	struct xilinx_dpdma_chan *chan[XILINX_DPDMA_NUM_CHAN];

	bool ext_addr;
};

/* -----------------------------------------------------------------------------
 * DebugFS
 */
#define XILINX_DPDMA_DEBUGFS_READ_MAX_SIZE	32
#define XILINX_DPDMA_DEBUGFS_UINT16_MAX_STR	"65535"

/* Match xilinx_dpdma_testcases vs dpdma_debugfs_reqs[] entry */
enum xilinx_dpdma_testcases {
	DPDMA_TC_INTR_DONE,
	DPDMA_TC_NONE
};

struct xilinx_dpdma_debugfs {
	enum xilinx_dpdma_testcases testcase;
	u16 xilinx_dpdma_irq_done_count;
	unsigned int chan_id;
};

static struct xilinx_dpdma_debugfs dpdma_debugfs;
struct xilinx_dpdma_debugfs_request {
	const char *name;
	enum xilinx_dpdma_testcases tc;
	ssize_t (*read)(char *buf);
	int (*write)(char *args);
};

static void xilinx_dpdma_debugfs_desc_done_irq(struct xilinx_dpdma_chan *chan)
{
	if (IS_ENABLED(CONFIG_DEBUG_FS) && chan->id == dpdma_debugfs.chan_id)
		dpdma_debugfs.xilinx_dpdma_irq_done_count++;
}

static ssize_t xilinx_dpdma_debugfs_desc_done_irq_read(char *buf)
{
	size_t out_str_len;

	dpdma_debugfs.testcase = DPDMA_TC_NONE;

	out_str_len = strlen(XILINX_DPDMA_DEBUGFS_UINT16_MAX_STR);
	out_str_len = min_t(size_t, XILINX_DPDMA_DEBUGFS_READ_MAX_SIZE,
			    out_str_len);
	snprintf(buf, out_str_len, "%d",
		 dpdma_debugfs.xilinx_dpdma_irq_done_count);

	return 0;
}

static int xilinx_dpdma_debugfs_desc_done_irq_write(char *args)
{
	char *arg;
	int ret;
	u32 id;

	arg = strsep(&args, " ");
	if (!arg || strncasecmp(arg, "start", 5))
		return -EINVAL;

	arg = strsep(&args, " ");
	if (!arg)
		return -EINVAL;

	ret = kstrtou32(arg, 0, &id);
	if (ret < 0)
		return ret;

	if (id < ZYNQMP_DPDMA_VIDEO0 || id > ZYNQMP_DPDMA_AUDIO1)
		return -EINVAL;

	dpdma_debugfs.testcase = DPDMA_TC_INTR_DONE;
	dpdma_debugfs.xilinx_dpdma_irq_done_count = 0;
	dpdma_debugfs.chan_id = id;

	return 0;
}

/* Match xilinx_dpdma_testcases vs dpdma_debugfs_reqs[] entry */
static struct xilinx_dpdma_debugfs_request dpdma_debugfs_reqs[] = {
	{
		.name = "DESCRIPTOR_DONE_INTR",
		.tc = DPDMA_TC_INTR_DONE,
		.read = xilinx_dpdma_debugfs_desc_done_irq_read,
		.write = xilinx_dpdma_debugfs_desc_done_irq_write,
	},
};

static ssize_t xilinx_dpdma_debugfs_read(struct file *f, char __user *buf,
					 size_t size, loff_t *pos)
{
	enum xilinx_dpdma_testcases testcase;
	char *kern_buff;
	int ret = 0;

	if (*pos != 0 || size <= 0)
		return -EINVAL;

	kern_buff = kzalloc(XILINX_DPDMA_DEBUGFS_READ_MAX_SIZE, GFP_KERNEL);
	if (!kern_buff) {
		dpdma_debugfs.testcase = DPDMA_TC_NONE;
		return -ENOMEM;
	}

	testcase = READ_ONCE(dpdma_debugfs.testcase);
	if (testcase != DPDMA_TC_NONE) {
		ret = dpdma_debugfs_reqs[testcase].read(kern_buff);
		if (ret < 0)
			goto done;
	} else {
		strlcpy(kern_buff, "No testcase executed",
			XILINX_DPDMA_DEBUGFS_READ_MAX_SIZE);
	}

	size = min(size, strlen(kern_buff));
	if (copy_to_user(buf, kern_buff, size))
		ret = -EFAULT;

done:
	kfree(kern_buff);
	if (ret)
		return ret;

	*pos = size + 1;
	return size;
}

static ssize_t xilinx_dpdma_debugfs_write(struct file *f,
					  const char __user *buf, size_t size,
					  loff_t *pos)
{
	char *kern_buff, *kern_buff_start;
	char *testcase;
	unsigned int i;
	int ret;

	if (*pos != 0 || size <= 0)
		return -EINVAL;

	/* Supporting single instance of test as of now. */
	if (dpdma_debugfs.testcase != DPDMA_TC_NONE)
		return -EBUSY;

	kern_buff = kzalloc(size, GFP_KERNEL);
	if (!kern_buff)
		return -ENOMEM;
	kern_buff_start = kern_buff;

	ret = strncpy_from_user(kern_buff, buf, size);
	if (ret < 0)
		goto done;

	/* Read the testcase name from a user request. */
	testcase = strsep(&kern_buff, " ");

	for (i = 0; i < ARRAY_SIZE(dpdma_debugfs_reqs); i++) {
		if (!strcasecmp(testcase, dpdma_debugfs_reqs[i].name))
			break;
	}

	if (i == ARRAY_SIZE(dpdma_debugfs_reqs)) {
		ret = -EINVAL;
		goto done;
	}

	ret = dpdma_debugfs_reqs[i].write(kern_buff);
	if (ret < 0)
		goto done;

	ret = size;

done:
	kfree(kern_buff_start);
	return ret;
}

static const struct file_operations fops_xilinx_dpdma_dbgfs = {
	.owner = THIS_MODULE,
	.read = xilinx_dpdma_debugfs_read,
	.write = xilinx_dpdma_debugfs_write,
};

static void xilinx_dpdma_debugfs_init(struct xilinx_dpdma_device *xdev)
{
	struct dentry *dent;

	dpdma_debugfs.testcase = DPDMA_TC_NONE;

	dent = debugfs_create_file("testcase", 0444, xdev->common.dbg_dev_root,
				   NULL, &fops_xilinx_dpdma_dbgfs);
	if (IS_ERR(dent))
		dev_err(xdev->dev, "Failed to create debugfs testcase file\n");
}

/* -----------------------------------------------------------------------------
 * I/O Accessors
 */

static inline u32 dpdma_read(void __iomem *base, u32 offset)
{
	return ioread32(base + offset);
}

static inline void dpdma_write(void __iomem *base, u32 offset, u32 val)
{
	iowrite32(val, base + offset);
}

static inline void dpdma_clr(void __iomem *base, u32 offset, u32 clr)
{
	dpdma_write(base, offset, dpdma_read(base, offset) & ~clr);
}

static inline void dpdma_set(void __iomem *base, u32 offset, u32 set)
{
	dpdma_write(base, offset, dpdma_read(base, offset) | set);
}

/* -----------------------------------------------------------------------------
 * Descriptor Operations
 */

/**
 * xilinx_dpdma_sw_desc_set_dma_addrs - Set DMA addresses in the descriptor
 * @xdev: DPDMA device
 * @sw_desc: The software descriptor in which to set DMA addresses
 * @prev: The previous descriptor
 * @dma_addr: array of dma addresses
 * @num_src_addr: number of addresses in @dma_addr
 *
 * Set all the DMA addresses in the hardware descriptor corresponding to @dev
 * from @dma_addr. If a previous descriptor is specified in @prev, its next
 * descriptor DMA address is set to the DMA address of @sw_desc. @prev may be
 * identical to @sw_desc for cyclic transfers.
 */
static void xilinx_dpdma_sw_desc_set_dma_addrs(struct xilinx_dpdma_device *xdev,
					       struct xilinx_dpdma_sw_desc *sw_desc,
					       struct xilinx_dpdma_sw_desc *prev,
					       dma_addr_t dma_addr[],
					       unsigned int num_src_addr)
{
	struct xilinx_dpdma_hw_desc *hw_desc = &sw_desc->hw;
	unsigned int i;

	hw_desc->src_addr = lower_32_bits(dma_addr[0]);
	if (xdev->ext_addr)
		hw_desc->addr_ext |=
			FIELD_PREP(XILINX_DPDMA_DESC_ADDR_EXT_SRC_ADDR_MASK,
				   upper_32_bits(dma_addr[0]));

	for (i = 1; i < num_src_addr; i++) {
		u32 *addr = &hw_desc->src_addr2;

		addr[i - 1] = lower_32_bits(dma_addr[i]);

		if (xdev->ext_addr) {
			u32 *addr_ext = &hw_desc->addr_ext_23;
			u32 addr_msb;

			addr_msb = upper_32_bits(dma_addr[i]) & GENMASK(15, 0);
			addr_msb <<= 16 * ((i - 1) % 2);
			addr_ext[(i - 1) / 2] |= addr_msb;
		}
	}

	if (!prev)
		return;

	prev->hw.next_desc = lower_32_bits(sw_desc->dma_addr);
	if (xdev->ext_addr)
		prev->hw.addr_ext |=
			FIELD_PREP(XILINX_DPDMA_DESC_ADDR_EXT_NEXT_ADDR_MASK,
				   upper_32_bits(sw_desc->dma_addr));
}

/**
 * xilinx_dpdma_chan_alloc_sw_desc - Allocate a software descriptor
 * @chan: DPDMA channel
 *
 * Allocate a software descriptor from the channel's descriptor pool.
 *
 * Return: a software descriptor or NULL.
 */
static struct xilinx_dpdma_sw_desc *
xilinx_dpdma_chan_alloc_sw_desc(struct xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_sw_desc *sw_desc;
	dma_addr_t dma_addr;

	sw_desc = dma_pool_zalloc(chan->desc_pool, GFP_ATOMIC, &dma_addr);
	if (!sw_desc)
		return NULL;

	sw_desc->dma_addr = dma_addr;

	return sw_desc;
}

/**
 * xilinx_dpdma_chan_free_sw_desc - Free a software descriptor
 * @chan: DPDMA channel
 * @sw_desc: software descriptor to free
 *
 * Free a software descriptor from the channel's descriptor pool.
 */
static void
xilinx_dpdma_chan_free_sw_desc(struct xilinx_dpdma_chan *chan,
			       struct xilinx_dpdma_sw_desc *sw_desc)
{
	dma_pool_free(chan->desc_pool, sw_desc, sw_desc->dma_addr);
}

/**
 * xilinx_dpdma_chan_dump_tx_desc - Dump a tx descriptor
 * @chan: DPDMA channel
 * @tx_desc: tx descriptor to dump
 *
 * Dump contents of a tx descriptor
 */
static void xilinx_dpdma_chan_dump_tx_desc(struct xilinx_dpdma_chan *chan,
					   struct xilinx_dpdma_tx_desc *tx_desc)
{
	struct xilinx_dpdma_sw_desc *sw_desc;
	struct device *dev = chan->xdev->dev;
	unsigned int i = 0;

	dev_dbg(dev, "------- TX descriptor dump start -------\n");
	dev_dbg(dev, "------- channel ID = %d -------\n", chan->id);

	list_for_each_entry(sw_desc, &tx_desc->descriptors, node) {
		struct xilinx_dpdma_hw_desc *hw_desc = &sw_desc->hw;

		dev_dbg(dev, "------- HW descriptor %d -------\n", i++);
		dev_dbg(dev, "descriptor DMA addr: %pad\n", &sw_desc->dma_addr);
		dev_dbg(dev, "control: 0x%08x\n", hw_desc->control);
		dev_dbg(dev, "desc_id: 0x%08x\n", hw_desc->desc_id);
		dev_dbg(dev, "xfer_size: 0x%08x\n", hw_desc->xfer_size);
		dev_dbg(dev, "hsize_stride: 0x%08x\n", hw_desc->hsize_stride);
		dev_dbg(dev, "timestamp_lsb: 0x%08x\n", hw_desc->timestamp_lsb);
		dev_dbg(dev, "timestamp_msb: 0x%08x\n", hw_desc->timestamp_msb);
		dev_dbg(dev, "addr_ext: 0x%08x\n", hw_desc->addr_ext);
		dev_dbg(dev, "next_desc: 0x%08x\n", hw_desc->next_desc);
		dev_dbg(dev, "src_addr: 0x%08x\n", hw_desc->src_addr);
		dev_dbg(dev, "addr_ext_23: 0x%08x\n", hw_desc->addr_ext_23);
		dev_dbg(dev, "addr_ext_45: 0x%08x\n", hw_desc->addr_ext_45);
		dev_dbg(dev, "src_addr2: 0x%08x\n", hw_desc->src_addr2);
		dev_dbg(dev, "src_addr3: 0x%08x\n", hw_desc->src_addr3);
		dev_dbg(dev, "src_addr4: 0x%08x\n", hw_desc->src_addr4);
		dev_dbg(dev, "src_addr5: 0x%08x\n", hw_desc->src_addr5);
		dev_dbg(dev, "crc: 0x%08x\n", hw_desc->crc);
	}

	dev_dbg(dev, "------- TX descriptor dump end -------\n");
}

/**
 * xilinx_dpdma_chan_alloc_tx_desc - Allocate a transaction descriptor
 * @chan: DPDMA channel
 *
 * Allocate a tx descriptor.
 *
 * Return: a tx descriptor or NULL.
 */
static struct xilinx_dpdma_tx_desc *
xilinx_dpdma_chan_alloc_tx_desc(struct xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_tx_desc *tx_desc;

	tx_desc = kzalloc(sizeof(*tx_desc), GFP_NOWAIT);
	if (!tx_desc)
		return NULL;

	INIT_LIST_HEAD(&tx_desc->descriptors);
	tx_desc->chan = chan;
	tx_desc->error = false;

	return tx_desc;
}

/**
 * xilinx_dpdma_chan_free_tx_desc - Free a virtual DMA descriptor
 * @vdesc: virtual DMA descriptor
 *
 * Free the virtual DMA descriptor @vdesc including its software descriptors.
 */
static void xilinx_dpdma_chan_free_tx_desc(struct virt_dma_desc *vdesc)
{
	struct xilinx_dpdma_sw_desc *sw_desc, *next;
	struct xilinx_dpdma_tx_desc *desc;

	if (!vdesc)
		return;

	desc = to_dpdma_tx_desc(vdesc);

	list_for_each_entry_safe(sw_desc, next, &desc->descriptors, node) {
		list_del(&sw_desc->node);
		xilinx_dpdma_chan_free_sw_desc(desc->chan, sw_desc);
	}

	kfree(desc);
}

/**
 * xilinx_dpdma_chan_prep_interleaved_dma - Prepare an interleaved dma
 *					    descriptor
 * @chan: DPDMA channel
 * @xt: dma interleaved template
 *
 * Prepare a tx descriptor including internal software/hardware descriptors
 * based on @xt.
 *
 * Return: A DPDMA TX descriptor on success, or NULL.
 */
static struct xilinx_dpdma_tx_desc *
xilinx_dpdma_chan_prep_interleaved_dma(struct xilinx_dpdma_chan *chan,
				       struct dma_interleaved_template *xt)
{
	struct xilinx_dpdma_tx_desc *tx_desc;
	struct xilinx_dpdma_sw_desc *sw_desc;
	struct xilinx_dpdma_hw_desc *hw_desc;
	size_t hsize = xt->sgl[0].size;
	size_t stride = hsize + xt->sgl[0].icg;

	if (!IS_ALIGNED(xt->src_start, XILINX_DPDMA_ALIGN_BYTES)) {
		dev_err(chan->xdev->dev,
			"chan%u: buffer should be aligned at %d B\n",
			chan->id, XILINX_DPDMA_ALIGN_BYTES);
		return NULL;
	}

	tx_desc = xilinx_dpdma_chan_alloc_tx_desc(chan);
	if (!tx_desc)
		return NULL;

	sw_desc = xilinx_dpdma_chan_alloc_sw_desc(chan);
	if (!sw_desc) {
		xilinx_dpdma_chan_free_tx_desc(&tx_desc->vdesc);
		return NULL;
	}

	xilinx_dpdma_sw_desc_set_dma_addrs(chan->xdev, sw_desc, sw_desc,
					   &xt->src_start, 1);

	hw_desc = &sw_desc->hw;
	hsize = ALIGN(hsize, XILINX_DPDMA_LINESIZE_ALIGN_BITS / 8);
	hw_desc->xfer_size = hsize * xt->numf;
	hw_desc->hsize_stride =
		FIELD_PREP(XILINX_DPDMA_DESC_HSIZE_STRIDE_HSIZE_MASK, hsize) |
		FIELD_PREP(XILINX_DPDMA_DESC_HSIZE_STRIDE_STRIDE_MASK,
			   stride / 16);
	hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_PREEMBLE;
	hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_COMPLETE_INTR;
	hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_IGNORE_DONE;
	hw_desc->control |= XILINX_DPDMA_DESC_CONTROL_LAST_OF_FRAME;

	list_add_tail(&sw_desc->node, &tx_desc->descriptors);

	return tx_desc;
}

/* -----------------------------------------------------------------------------
 * DPDMA Channel Operations
 */

/**
 * xilinx_dpdma_chan_enable - Enable the channel
 * @chan: DPDMA channel
 *
 * Enable the channel and its interrupts. Set the QoS values for video class.
 */
static void xilinx_dpdma_chan_enable(struct xilinx_dpdma_chan *chan)
{
	u32 reg;

	reg = (XILINX_DPDMA_INTR_CHAN_MASK << chan->id)
	    | XILINX_DPDMA_INTR_GLOBAL_MASK;
	dpdma_write(chan->xdev->reg, XILINX_DPDMA_IEN, reg);
	reg = (XILINX_DPDMA_EINTR_CHAN_ERR_MASK << chan->id)
	    | XILINX_DPDMA_INTR_GLOBAL_ERR;
	dpdma_write(chan->xdev->reg, XILINX_DPDMA_EIEN, reg);

	reg = XILINX_DPDMA_CH_CNTL_ENABLE
	    | FIELD_PREP(XILINX_DPDMA_CH_CNTL_QOS_DSCR_WR_MASK,
			 XILINX_DPDMA_CH_CNTL_QOS_VID_CLASS)
	    | FIELD_PREP(XILINX_DPDMA_CH_CNTL_QOS_DSCR_RD_MASK,
			 XILINX_DPDMA_CH_CNTL_QOS_VID_CLASS)
	    | FIELD_PREP(XILINX_DPDMA_CH_CNTL_QOS_DATA_RD_MASK,
			 XILINX_DPDMA_CH_CNTL_QOS_VID_CLASS);
	dpdma_set(chan->reg, XILINX_DPDMA_CH_CNTL, reg);
}

/**
 * xilinx_dpdma_chan_disable - Disable the channel
 * @chan: DPDMA channel
 *
 * Disable the channel and its interrupts.
 */
static void xilinx_dpdma_chan_disable(struct xilinx_dpdma_chan *chan)
{
	u32 reg;

	reg = XILINX_DPDMA_INTR_CHAN_MASK << chan->id;
	dpdma_write(chan->xdev->reg, XILINX_DPDMA_IEN, reg);
	reg = XILINX_DPDMA_EINTR_CHAN_ERR_MASK << chan->id;
	dpdma_write(chan->xdev->reg, XILINX_DPDMA_EIEN, reg);

	dpdma_clr(chan->reg, XILINX_DPDMA_CH_CNTL, XILINX_DPDMA_CH_CNTL_ENABLE);
}

/**
 * xilinx_dpdma_chan_pause - Pause the channel
 * @chan: DPDMA channel
 *
 * Pause the channel.
 */
static void xilinx_dpdma_chan_pause(struct xilinx_dpdma_chan *chan)
{
	dpdma_set(chan->reg, XILINX_DPDMA_CH_CNTL, XILINX_DPDMA_CH_CNTL_PAUSE);
}

/**
 * xilinx_dpdma_chan_unpause - Unpause the channel
 * @chan: DPDMA channel
 *
 * Unpause the channel.
 */
static void xilinx_dpdma_chan_unpause(struct xilinx_dpdma_chan *chan)
{
	dpdma_clr(chan->reg, XILINX_DPDMA_CH_CNTL, XILINX_DPDMA_CH_CNTL_PAUSE);
}

static u32 xilinx_dpdma_chan_video_group_ready(struct xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_device *xdev = chan->xdev;
	u32 channels = 0;
	unsigned int i;

	for (i = ZYNQMP_DPDMA_VIDEO0; i <= ZYNQMP_DPDMA_VIDEO2; i++) {
		if (xdev->chan[i]->video_group && !xdev->chan[i]->running)
			return 0;

		if (xdev->chan[i]->video_group)
			channels |= BIT(i);
	}

	return channels;
}

/**
 * xilinx_dpdma_chan_queue_transfer - Queue the next transfer
 * @chan: DPDMA channel
 *
 * Queue the next descriptor, if any, to the hardware. If the channel is
 * stopped, start it first. Otherwise retrigger it with the next descriptor.
 */
static void xilinx_dpdma_chan_queue_transfer(struct xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_device *xdev = chan->xdev;
	struct xilinx_dpdma_sw_desc *sw_desc;
	struct xilinx_dpdma_tx_desc *desc;
	struct virt_dma_desc *vdesc;
	u32 reg, channels;
	bool first_frame;

	lockdep_assert_held(&chan->lock);

	if (chan->desc.pending)
		return;

	if (!chan->running) {
		xilinx_dpdma_chan_unpause(chan);
		xilinx_dpdma_chan_enable(chan);
		chan->first_frame = true;
		chan->running = true;
	}

	vdesc = vchan_next_desc(&chan->vchan);
	if (!vdesc)
		return;

	desc = to_dpdma_tx_desc(vdesc);
	chan->desc.pending = desc;
	list_del(&desc->vdesc.node);

	/*
	 * Assign the cookie to descriptors in this transaction. Only 16 bit
	 * will be used, but it should be enough.
	 */
	list_for_each_entry(sw_desc, &desc->descriptors, node)
		sw_desc->hw.desc_id = desc->vdesc.tx.cookie
				    & XILINX_DPDMA_CH_DESC_ID_MASK;

	sw_desc = list_first_entry(&desc->descriptors,
				   struct xilinx_dpdma_sw_desc, node);
	dpdma_write(chan->reg, XILINX_DPDMA_CH_DESC_START_ADDR,
		    lower_32_bits(sw_desc->dma_addr));
	if (xdev->ext_addr)
		dpdma_write(chan->reg, XILINX_DPDMA_CH_DESC_START_ADDRE,
			    FIELD_PREP(XILINX_DPDMA_CH_DESC_START_ADDRE_MASK,
				       upper_32_bits(sw_desc->dma_addr)));

	first_frame = chan->first_frame;
	chan->first_frame = false;

	if (chan->video_group) {
		channels = xilinx_dpdma_chan_video_group_ready(chan);
		/*
		 * Trigger the transfer only when all channels in the group are
		 * ready.
		 */
		if (!channels)
			return;
	} else {
		channels = BIT(chan->id);
	}

	if (first_frame)
		reg = XILINX_DPDMA_GBL_TRIG_MASK(channels);
	else
		reg = XILINX_DPDMA_GBL_RETRIG_MASK(channels);

	dpdma_write(xdev->reg, XILINX_DPDMA_GBL, reg);
}

/**
 * xilinx_dpdma_chan_ostand - Number of outstanding transactions
 * @chan: DPDMA channel
 *
 * Read and return the number of outstanding transactions from register.
 *
 * Return: Number of outstanding transactions from the status register.
 */
static u32 xilinx_dpdma_chan_ostand(struct xilinx_dpdma_chan *chan)
{
	return FIELD_GET(XILINX_DPDMA_CH_STATUS_OTRAN_CNT_MASK,
			 dpdma_read(chan->reg, XILINX_DPDMA_CH_STATUS));
}

/**
 * xilinx_dpdma_chan_notify_no_ostand - Notify no outstanding transaction event
 * @chan: DPDMA channel
 *
 * Notify waiters for no outstanding event, so waiters can stop the channel
 * safely. This function is supposed to be called when 'no outstanding'
 * interrupt is generated. The 'no outstanding' interrupt is disabled and
 * should be re-enabled when this event is handled. If the channel status
 * register still shows some number of outstanding transactions, the interrupt
 * remains enabled.
 *
 * Return: 0 on success. On failure, -EWOULDBLOCK if there's still outstanding
 * transaction(s).
 */
static int xilinx_dpdma_chan_notify_no_ostand(struct xilinx_dpdma_chan *chan)
{
	u32 cnt;

	cnt = xilinx_dpdma_chan_ostand(chan);
	if (cnt) {
		dev_dbg(chan->xdev->dev,
			"chan%u: %d outstanding transactions\n",
			chan->id, cnt);
		return -EWOULDBLOCK;
	}

	/* Disable 'no outstanding' interrupt */
	dpdma_write(chan->xdev->reg, XILINX_DPDMA_IDS,
		    XILINX_DPDMA_INTR_NO_OSTAND(chan->id));
	wake_up(&chan->wait_to_stop);

	return 0;
}

/**
 * xilinx_dpdma_chan_wait_no_ostand - Wait for the no outstanding irq
 * @chan: DPDMA channel
 *
 * Wait for the no outstanding transaction interrupt. This functions can sleep
 * for 50ms.
 *
 * Return: 0 on success. On failure, -ETIMEOUT for time out, or the error code
 * from wait_event_interruptible_timeout().
 */
static int xilinx_dpdma_chan_wait_no_ostand(struct xilinx_dpdma_chan *chan)
{
	int ret;

	/* Wait for a no outstanding transaction interrupt upto 50msec */
	ret = wait_event_interruptible_timeout(chan->wait_to_stop,
					       !xilinx_dpdma_chan_ostand(chan),
					       msecs_to_jiffies(50));
	if (ret > 0) {
		dpdma_write(chan->xdev->reg, XILINX_DPDMA_IEN,
			    XILINX_DPDMA_INTR_NO_OSTAND(chan->id));
		return 0;
	}

	dev_err(chan->xdev->dev, "chan%u: not ready to stop: %d trans\n",
		chan->id, xilinx_dpdma_chan_ostand(chan));

	if (ret == 0)
		return -ETIMEDOUT;

	return ret;
}

/**
 * xilinx_dpdma_chan_poll_no_ostand - Poll the outstanding transaction status
 * @chan: DPDMA channel
 *
 * Poll the outstanding transaction status, and return when there's no
 * outstanding transaction. This functions can be used in the interrupt context
 * or where the atomicity is required. Calling thread may wait more than 50ms.
 *
 * Return: 0 on success, or -ETIMEDOUT.
 */
static int xilinx_dpdma_chan_poll_no_ostand(struct xilinx_dpdma_chan *chan)
{
	u32 cnt, loop = 50000;

	/* Poll at least for 50ms (20 fps). */
	do {
		cnt = xilinx_dpdma_chan_ostand(chan);
		udelay(1);
	} while (loop-- > 0 && cnt);

	if (loop) {
		dpdma_write(chan->xdev->reg, XILINX_DPDMA_IEN,
			    XILINX_DPDMA_INTR_NO_OSTAND(chan->id));
		return 0;
	}

	dev_err(chan->xdev->dev, "chan%u: not ready to stop: %d trans\n",
		chan->id, xilinx_dpdma_chan_ostand(chan));

	return -ETIMEDOUT;
}

/**
 * xilinx_dpdma_chan_stop - Stop the channel
 * @chan: DPDMA channel
 *
 * Stop a previously paused channel by first waiting for completion of all
 * outstanding transaction and then disabling the channel.
 *
 * Return: 0 on success, or -ETIMEDOUT if the channel failed to stop.
 */
static int xilinx_dpdma_chan_stop(struct xilinx_dpdma_chan *chan)
{
	unsigned long flags;
	int ret;

	ret = xilinx_dpdma_chan_wait_no_ostand(chan);
	if (ret)
		return ret;

	spin_lock_irqsave(&chan->lock, flags);
	xilinx_dpdma_chan_disable(chan);
	chan->running = false;
	spin_unlock_irqrestore(&chan->lock, flags);

	return 0;
}

/**
 * xilinx_dpdma_chan_done_irq - Handle hardware descriptor completion
 * @chan: DPDMA channel
 *
 * Handle completion of the currently active descriptor (@chan->desc.active). As
 * we currently support cyclic transfers only, this just invokes the cyclic
 * callback. The descriptor will be completed at the VSYNC interrupt when a new
 * descriptor replaces it.
 */
static void xilinx_dpdma_chan_done_irq(struct xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_tx_desc *active;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	xilinx_dpdma_debugfs_desc_done_irq(chan);

	active = chan->desc.active;
	if (active)
		vchan_cyclic_callback(&active->vdesc);
	else
		dev_warn(chan->xdev->dev,
			 "chan%u: DONE IRQ with no active descriptor!\n",
			 chan->id);

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dpdma_chan_vsync_irq - Handle hardware descriptor scheduling
 * @chan: DPDMA channel
 *
 * At VSYNC the active descriptor may have been replaced by the pending
 * descriptor. Detect this through the DESC_ID and perform appropriate
 * bookkeeping.
 */
static void xilinx_dpdma_chan_vsync_irq(struct  xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_tx_desc *pending;
	struct xilinx_dpdma_sw_desc *sw_desc;
	unsigned long flags;
	u32 desc_id;

	spin_lock_irqsave(&chan->lock, flags);

	pending = chan->desc.pending;
	if (!chan->running || !pending)
		goto out;

	desc_id = dpdma_read(chan->reg, XILINX_DPDMA_CH_DESC_ID)
		& XILINX_DPDMA_CH_DESC_ID_MASK;

	/* If the retrigger raced with vsync, retry at the next frame. */
	sw_desc = list_first_entry(&pending->descriptors,
				   struct xilinx_dpdma_sw_desc, node);
	if (sw_desc->hw.desc_id != desc_id) {
		dev_dbg(chan->xdev->dev,
			"chan%u: vsync race lost (%u != %u), retrying\n",
			chan->id, sw_desc->hw.desc_id, desc_id);
		goto out;
	}

	/*
	 * Complete the active descriptor, if any, promote the pending
	 * descriptor to active, and queue the next transfer, if any.
	 */
	if (chan->desc.active)
		vchan_cookie_complete(&chan->desc.active->vdesc);
	chan->desc.active = pending;
	chan->desc.pending = NULL;

	xilinx_dpdma_chan_queue_transfer(chan);

out:
	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dpdma_chan_err - Detect any channel error
 * @chan: DPDMA channel
 * @isr: masked Interrupt Status Register
 * @eisr: Error Interrupt Status Register
 *
 * Return: true if any channel error occurs, or false otherwise.
 */
static bool
xilinx_dpdma_chan_err(struct xilinx_dpdma_chan *chan, u32 isr, u32 eisr)
{
	if (!chan)
		return false;

	if (chan->running &&
	    ((isr & (XILINX_DPDMA_INTR_CHAN_ERR_MASK << chan->id)) ||
	    (eisr & (XILINX_DPDMA_EINTR_CHAN_ERR_MASK << chan->id))))
		return true;

	return false;
}

/**
 * xilinx_dpdma_chan_handle_err - DPDMA channel error handling
 * @chan: DPDMA channel
 *
 * This function is called when any channel error or any global error occurs.
 * The function disables the paused channel by errors and determines
 * if the current active descriptor can be rescheduled depending on
 * the descriptor status.
 */
static void xilinx_dpdma_chan_handle_err(struct xilinx_dpdma_chan *chan)
{
	struct xilinx_dpdma_device *xdev = chan->xdev;
	struct xilinx_dpdma_tx_desc *active;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	dev_dbg(xdev->dev, "chan%u: cur desc addr = 0x%04x%08x\n",
		chan->id,
		dpdma_read(chan->reg, XILINX_DPDMA_CH_DESC_START_ADDRE),
		dpdma_read(chan->reg, XILINX_DPDMA_CH_DESC_START_ADDR));
	dev_dbg(xdev->dev, "chan%u: cur payload addr = 0x%04x%08x\n",
		chan->id,
		dpdma_read(chan->reg, XILINX_DPDMA_CH_PYLD_CUR_ADDRE),
		dpdma_read(chan->reg, XILINX_DPDMA_CH_PYLD_CUR_ADDR));

	xilinx_dpdma_chan_disable(chan);
	chan->running = false;

	if (!chan->desc.active)
		goto out_unlock;

	active = chan->desc.active;
	chan->desc.active = NULL;

	xilinx_dpdma_chan_dump_tx_desc(chan, active);

	if (active->error)
		dev_dbg(xdev->dev, "chan%u: repeated error on desc\n",
			chan->id);

	/* Reschedule if there's no new descriptor */
	if (!chan->desc.pending &&
	    list_empty(&chan->vchan.desc_issued)) {
		active->error = true;
		list_add_tail(&active->vdesc.node,
			      &chan->vchan.desc_issued);
	} else {
		xilinx_dpdma_chan_free_tx_desc(&active->vdesc);
	}

out_unlock:
	spin_unlock_irqrestore(&chan->lock, flags);
}

/* -----------------------------------------------------------------------------
 * DMA Engine Operations
 */

static struct dma_async_tx_descriptor *
xilinx_dpdma_prep_interleaved_dma(struct dma_chan *dchan,
				  struct dma_interleaved_template *xt,
				  unsigned long flags)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_dpdma_tx_desc *desc;

	if (xt->dir != DMA_MEM_TO_DEV)
		return NULL;

	if (!xt->numf || !xt->sgl[0].size)
		return NULL;

	if (!(flags & DMA_PREP_REPEAT) || !(flags & DMA_PREP_LOAD_EOT))
		return NULL;

	desc = xilinx_dpdma_chan_prep_interleaved_dma(chan, xt);
	if (!desc)
		return NULL;

	vchan_tx_prep(&chan->vchan, &desc->vdesc, flags | DMA_CTRL_ACK);

	return &desc->vdesc.tx;
}

/**
 * xilinx_dpdma_alloc_chan_resources - Allocate resources for the channel
 * @dchan: DMA channel
 *
 * Allocate a descriptor pool for the channel.
 *
 * Return: 0 on success, or -ENOMEM if failed to allocate a pool.
 */
static int xilinx_dpdma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(dchan);
	size_t align = __alignof__(struct xilinx_dpdma_sw_desc);

	chan->desc_pool = dma_pool_create(dev_name(chan->xdev->dev),
					  chan->xdev->dev,
					  sizeof(struct xilinx_dpdma_sw_desc),
					  align, 0);
	if (!chan->desc_pool) {
		dev_err(chan->xdev->dev,
			"chan%u: failed to allocate a descriptor pool\n",
			chan->id);
		return -ENOMEM;
	}

	return 0;
}

/**
 * xilinx_dpdma_free_chan_resources - Free all resources for the channel
 * @dchan: DMA channel
 *
 * Free resources associated with the virtual DMA channel, and destroy the
 * descriptor pool.
 */
static void xilinx_dpdma_free_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(dchan);

	vchan_free_chan_resources(&chan->vchan);

	dma_pool_destroy(chan->desc_pool);
	chan->desc_pool = NULL;
}

static void xilinx_dpdma_issue_pending(struct dma_chan *dchan)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->vchan.lock, flags);
	if (vchan_issue_pending(&chan->vchan))
		xilinx_dpdma_chan_queue_transfer(chan);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
}

static int xilinx_dpdma_config(struct dma_chan *dchan,
			       struct dma_slave_config *config)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_dpdma_peripheral_config *pconfig;
	unsigned long flags;

	/*
	 * The destination address doesn't need to be specified as the DPDMA is
	 * hardwired to the destination (the DP controller). The transfer
	 * width, burst size and port window size are thus meaningless, they're
	 * fixed both on the DPDMA side and on the DP controller side.
	 */

	/*
	 * Use the peripheral_config to indicate that the channel is part
	 * of a video group. This requires matching use of the custom
	 * structure in each driver.
	 */
	pconfig = config->peripheral_config;
	if (WARN_ON(pconfig && config->peripheral_size != sizeof(*pconfig)))
		return -EINVAL;

	spin_lock_irqsave(&chan->lock, flags);
	if (chan->id <= ZYNQMP_DPDMA_VIDEO2 && pconfig)
		chan->video_group = pconfig->video_group;
	spin_unlock_irqrestore(&chan->lock, flags);

	return 0;
}

static int xilinx_dpdma_pause(struct dma_chan *dchan)
{
	xilinx_dpdma_chan_pause(to_xilinx_chan(dchan));

	return 0;
}

static int xilinx_dpdma_resume(struct dma_chan *dchan)
{
	xilinx_dpdma_chan_unpause(to_xilinx_chan(dchan));

	return 0;
}

/**
 * xilinx_dpdma_terminate_all - Terminate the channel and descriptors
 * @dchan: DMA channel
 *
 * Pause the channel without waiting for ongoing transfers to complete. Waiting
 * for completion is performed by xilinx_dpdma_synchronize() that will disable
 * the channel to complete the stop.
 *
 * All the descriptors associated with the channel that are guaranteed not to
 * be touched by the hardware. The pending and active descriptor are not
 * touched, and will be freed either upon completion, or by
 * xilinx_dpdma_synchronize().
 *
 * Return: 0 on success, or -ETIMEDOUT if the channel failed to stop.
 */
static int xilinx_dpdma_terminate_all(struct dma_chan *dchan)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_dpdma_device *xdev = chan->xdev;
	LIST_HEAD(descriptors);
	unsigned long flags;
	unsigned int i;

	/* Pause the channel (including the whole video group if applicable). */
	if (chan->video_group) {
		for (i = ZYNQMP_DPDMA_VIDEO0; i <= ZYNQMP_DPDMA_VIDEO2; i++) {
			if (xdev->chan[i]->video_group &&
			    xdev->chan[i]->running) {
				xilinx_dpdma_chan_pause(xdev->chan[i]);
				xdev->chan[i]->video_group = false;
			}
		}
	} else {
		xilinx_dpdma_chan_pause(chan);
	}

	/* Gather all the descriptors we can free and free them. */
	spin_lock_irqsave(&chan->vchan.lock, flags);
	vchan_get_all_descriptors(&chan->vchan, &descriptors);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	vchan_dma_desc_free_list(&chan->vchan, &descriptors);

	return 0;
}

/**
 * xilinx_dpdma_synchronize - Synchronize callback execution
 * @dchan: DMA channel
 *
 * Synchronizing callback execution ensures that all previously issued
 * transfers have completed and all associated callbacks have been called and
 * have returned.
 *
 * This function waits for the DMA channel to stop. It assumes it has been
 * paused by a previous call to dmaengine_terminate_async(), and that no new
 * pending descriptors have been issued with dma_async_issue_pending(). The
 * behaviour is undefined otherwise.
 */
static void xilinx_dpdma_synchronize(struct dma_chan *dchan)
{
	struct xilinx_dpdma_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	xilinx_dpdma_chan_stop(chan);

	spin_lock_irqsave(&chan->vchan.lock, flags);
	if (chan->desc.pending) {
		vchan_terminate_vdesc(&chan->desc.pending->vdesc);
		chan->desc.pending = NULL;
	}
	if (chan->desc.active) {
		vchan_terminate_vdesc(&chan->desc.active->vdesc);
		chan->desc.active = NULL;
	}
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	vchan_synchronize(&chan->vchan);
}

/* -----------------------------------------------------------------------------
 * Interrupt and Tasklet Handling
 */

/**
 * xilinx_dpdma_err - Detect any global error
 * @isr: Interrupt Status Register
 * @eisr: Error Interrupt Status Register
 *
 * Return: True if any global error occurs, or false otherwise.
 */
static bool xilinx_dpdma_err(u32 isr, u32 eisr)
{
	if (isr & XILINX_DPDMA_INTR_GLOBAL_ERR ||
	    eisr & XILINX_DPDMA_EINTR_GLOBAL_ERR)
		return true;

	return false;
}

/**
 * xilinx_dpdma_handle_err_irq - Handle DPDMA error interrupt
 * @xdev: DPDMA device
 * @isr: masked Interrupt Status Register
 * @eisr: Error Interrupt Status Register
 *
 * Handle if any error occurs based on @isr and @eisr. This function disables
 * corresponding error interrupts, and those should be re-enabled once handling
 * is done.
 */
static void xilinx_dpdma_handle_err_irq(struct xilinx_dpdma_device *xdev,
					u32 isr, u32 eisr)
{
	bool err = xilinx_dpdma_err(isr, eisr);
	unsigned int i;

	dev_dbg_ratelimited(xdev->dev,
			    "error irq: isr = 0x%08x, eisr = 0x%08x\n",
			    isr, eisr);

	/* Disable channel error interrupts until errors are handled. */
	dpdma_write(xdev->reg, XILINX_DPDMA_IDS,
		    isr & ~XILINX_DPDMA_INTR_GLOBAL_ERR);
	dpdma_write(xdev->reg, XILINX_DPDMA_EIDS,
		    eisr & ~XILINX_DPDMA_EINTR_GLOBAL_ERR);

	for (i = 0; i < ARRAY_SIZE(xdev->chan); i++)
		if (err || xilinx_dpdma_chan_err(xdev->chan[i], isr, eisr))
			tasklet_schedule(&xdev->chan[i]->err_task);
}

/**
 * xilinx_dpdma_enable_irq - Enable interrupts
 * @xdev: DPDMA device
 *
 * Enable interrupts.
 */
static void xilinx_dpdma_enable_irq(struct xilinx_dpdma_device *xdev)
{
	dpdma_write(xdev->reg, XILINX_DPDMA_IEN, XILINX_DPDMA_INTR_ALL);
	dpdma_write(xdev->reg, XILINX_DPDMA_EIEN, XILINX_DPDMA_EINTR_ALL);
}

/**
 * xilinx_dpdma_disable_irq - Disable interrupts
 * @xdev: DPDMA device
 *
 * Disable interrupts.
 */
static void xilinx_dpdma_disable_irq(struct xilinx_dpdma_device *xdev)
{
	dpdma_write(xdev->reg, XILINX_DPDMA_IDS, XILINX_DPDMA_INTR_ALL);
	dpdma_write(xdev->reg, XILINX_DPDMA_EIDS, XILINX_DPDMA_EINTR_ALL);
}

/**
 * xilinx_dpdma_chan_err_task - Per channel tasklet for error handling
 * @t: pointer to the tasklet associated with this handler
 *
 * Per channel error handling tasklet. This function waits for the outstanding
 * transaction to complete and triggers error handling. After error handling,
 * re-enable channel error interrupts, and restart the channel if needed.
 */
static void xilinx_dpdma_chan_err_task(struct tasklet_struct *t)
{
	struct xilinx_dpdma_chan *chan = from_tasklet(chan, t, err_task);
	struct xilinx_dpdma_device *xdev = chan->xdev;
	unsigned long flags;

	/* Proceed error handling even when polling fails. */
	xilinx_dpdma_chan_poll_no_ostand(chan);

	xilinx_dpdma_chan_handle_err(chan);

	dpdma_write(xdev->reg, XILINX_DPDMA_IEN,
		    XILINX_DPDMA_INTR_CHAN_ERR_MASK << chan->id);
	dpdma_write(xdev->reg, XILINX_DPDMA_EIEN,
		    XILINX_DPDMA_EINTR_CHAN_ERR_MASK << chan->id);

	spin_lock_irqsave(&chan->lock, flags);
	xilinx_dpdma_chan_queue_transfer(chan);
	spin_unlock_irqrestore(&chan->lock, flags);
}

static irqreturn_t xilinx_dpdma_irq_handler(int irq, void *data)
{
	struct xilinx_dpdma_device *xdev = data;
	unsigned long mask;
	unsigned int i;
	u32 status;
	u32 error;

	status = dpdma_read(xdev->reg, XILINX_DPDMA_ISR);
	error = dpdma_read(xdev->reg, XILINX_DPDMA_EISR);
	if (!status && !error)
		return IRQ_NONE;

	dpdma_write(xdev->reg, XILINX_DPDMA_ISR, status);
	dpdma_write(xdev->reg, XILINX_DPDMA_EISR, error);

	if (status & XILINX_DPDMA_INTR_VSYNC) {
		/*
		 * There's a single VSYNC interrupt that needs to be processed
		 * by each running channel to update the active descriptor.
		 */
		for (i = 0; i < ARRAY_SIZE(xdev->chan); i++) {
			struct xilinx_dpdma_chan *chan = xdev->chan[i];

			if (chan)
				xilinx_dpdma_chan_vsync_irq(chan);
		}
	}

	mask = FIELD_GET(XILINX_DPDMA_INTR_DESC_DONE_MASK, status);
	if (mask) {
		for_each_set_bit(i, &mask, ARRAY_SIZE(xdev->chan))
			xilinx_dpdma_chan_done_irq(xdev->chan[i]);
	}

	mask = FIELD_GET(XILINX_DPDMA_INTR_NO_OSTAND_MASK, status);
	if (mask) {
		for_each_set_bit(i, &mask, ARRAY_SIZE(xdev->chan))
			xilinx_dpdma_chan_notify_no_ostand(xdev->chan[i]);
	}

	mask = status & XILINX_DPDMA_INTR_ERR_ALL;
	if (mask || error)
		xilinx_dpdma_handle_err_irq(xdev, mask, error);

	return IRQ_HANDLED;
}

/* -----------------------------------------------------------------------------
 * Initialization & Cleanup
 */

static int xilinx_dpdma_chan_init(struct xilinx_dpdma_device *xdev,
				  unsigned int chan_id)
{
	struct xilinx_dpdma_chan *chan;

	chan = devm_kzalloc(xdev->dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	chan->id = chan_id;
	chan->reg = xdev->reg + XILINX_DPDMA_CH_BASE
		  + XILINX_DPDMA_CH_OFFSET * chan->id;
	chan->running = false;
	chan->xdev = xdev;

	spin_lock_init(&chan->lock);
	init_waitqueue_head(&chan->wait_to_stop);

	tasklet_setup(&chan->err_task, xilinx_dpdma_chan_err_task);

	chan->vchan.desc_free = xilinx_dpdma_chan_free_tx_desc;
	vchan_init(&chan->vchan, &xdev->common);

	xdev->chan[chan->id] = chan;

	return 0;
}

static void xilinx_dpdma_chan_remove(struct xilinx_dpdma_chan *chan)
{
	if (!chan)
		return;

	tasklet_kill(&chan->err_task);
	list_del(&chan->vchan.chan.device_node);
}

static struct dma_chan *of_dma_xilinx_xlate(struct of_phandle_args *dma_spec,
					    struct of_dma *ofdma)
{
	struct xilinx_dpdma_device *xdev = ofdma->of_dma_data;
	u32 chan_id = dma_spec->args[0];

	if (chan_id >= ARRAY_SIZE(xdev->chan))
		return NULL;

	if (!xdev->chan[chan_id])
		return NULL;

	return dma_get_slave_channel(&xdev->chan[chan_id]->vchan.chan);
}

static void dpdma_hw_init(struct xilinx_dpdma_device *xdev)
{
	unsigned int i;
	void __iomem *reg;

	/* Disable all interrupts */
	xilinx_dpdma_disable_irq(xdev);

	/* Stop all channels */
	for (i = 0; i < ARRAY_SIZE(xdev->chan); i++) {
		reg = xdev->reg + XILINX_DPDMA_CH_BASE
				+ XILINX_DPDMA_CH_OFFSET * i;
		dpdma_clr(reg, XILINX_DPDMA_CH_CNTL, XILINX_DPDMA_CH_CNTL_ENABLE);
	}

	/* Clear the interrupt status registers */
	dpdma_write(xdev->reg, XILINX_DPDMA_ISR, XILINX_DPDMA_INTR_ALL);
	dpdma_write(xdev->reg, XILINX_DPDMA_EISR, XILINX_DPDMA_EINTR_ALL);
}

static int xilinx_dpdma_probe(struct platform_device *pdev)
{
	struct xilinx_dpdma_device *xdev;
	struct dma_device *ddev;
	unsigned int i;
	int ret;

	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->dev = &pdev->dev;
	xdev->ext_addr = sizeof(dma_addr_t) > 4;

	INIT_LIST_HEAD(&xdev->common.channels);

	platform_set_drvdata(pdev, xdev);

	xdev->axi_clk = devm_clk_get(xdev->dev, "axi_clk");
	if (IS_ERR(xdev->axi_clk))
		return PTR_ERR(xdev->axi_clk);

	xdev->reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xdev->reg))
		return PTR_ERR(xdev->reg);

	dpdma_hw_init(xdev);

	xdev->irq = platform_get_irq(pdev, 0);
	if (xdev->irq < 0) {
		dev_err(xdev->dev, "failed to get platform irq\n");
		return xdev->irq;
	}

	ret = request_irq(xdev->irq, xilinx_dpdma_irq_handler, IRQF_SHARED,
			  dev_name(xdev->dev), xdev);
	if (ret) {
		dev_err(xdev->dev, "failed to request IRQ\n");
		return ret;
	}

	ddev = &xdev->common;
	ddev->dev = &pdev->dev;

	dma_cap_set(DMA_SLAVE, ddev->cap_mask);
	dma_cap_set(DMA_PRIVATE, ddev->cap_mask);
	dma_cap_set(DMA_INTERLEAVE, ddev->cap_mask);
	dma_cap_set(DMA_REPEAT, ddev->cap_mask);
	dma_cap_set(DMA_LOAD_EOT, ddev->cap_mask);
	ddev->copy_align = fls(XILINX_DPDMA_ALIGN_BYTES - 1);

	ddev->device_alloc_chan_resources = xilinx_dpdma_alloc_chan_resources;
	ddev->device_free_chan_resources = xilinx_dpdma_free_chan_resources;
	ddev->device_prep_interleaved_dma = xilinx_dpdma_prep_interleaved_dma;
	/* TODO: Can we achieve better granularity ? */
	ddev->device_tx_status = dma_cookie_status;
	ddev->device_issue_pending = xilinx_dpdma_issue_pending;
	ddev->device_config = xilinx_dpdma_config;
	ddev->device_pause = xilinx_dpdma_pause;
	ddev->device_resume = xilinx_dpdma_resume;
	ddev->device_terminate_all = xilinx_dpdma_terminate_all;
	ddev->device_synchronize = xilinx_dpdma_synchronize;
	ddev->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED);
	ddev->directions = BIT(DMA_MEM_TO_DEV);
	ddev->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;

	for (i = 0; i < ARRAY_SIZE(xdev->chan); ++i) {
		ret = xilinx_dpdma_chan_init(xdev, i);
		if (ret < 0) {
			dev_err(xdev->dev, "failed to initialize channel %u\n",
				i);
			goto error;
		}
	}

	ret = clk_prepare_enable(xdev->axi_clk);
	if (ret) {
		dev_err(xdev->dev, "failed to enable the axi clock\n");
		goto error;
	}

	ret = dma_async_device_register(ddev);
	if (ret) {
		dev_err(xdev->dev, "failed to register the dma device\n");
		goto error_dma_async;
	}

	ret = of_dma_controller_register(xdev->dev->of_node,
					 of_dma_xilinx_xlate, ddev);
	if (ret) {
		dev_err(xdev->dev, "failed to register DMA to DT DMA helper\n");
		goto error_of_dma;
	}

	xilinx_dpdma_enable_irq(xdev);

	xilinx_dpdma_debugfs_init(xdev);

	dev_info(&pdev->dev, "Xilinx DPDMA engine is probed\n");

	return 0;

error_of_dma:
	dma_async_device_unregister(ddev);
error_dma_async:
	clk_disable_unprepare(xdev->axi_clk);
error:
	for (i = 0; i < ARRAY_SIZE(xdev->chan); i++)
		xilinx_dpdma_chan_remove(xdev->chan[i]);

	free_irq(xdev->irq, xdev);

	return ret;
}

static int xilinx_dpdma_remove(struct platform_device *pdev)
{
	struct xilinx_dpdma_device *xdev = platform_get_drvdata(pdev);
	unsigned int i;

	/* Start by disabling the IRQ to avoid races during cleanup. */
	free_irq(xdev->irq, xdev);

	xilinx_dpdma_disable_irq(xdev);
	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&xdev->common);
	clk_disable_unprepare(xdev->axi_clk);

	for (i = 0; i < ARRAY_SIZE(xdev->chan); i++)
		xilinx_dpdma_chan_remove(xdev->chan[i]);

	return 0;
}

static const struct of_device_id xilinx_dpdma_of_match[] = {
	{ .compatible = "xlnx,zynqmp-dpdma",},
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xilinx_dpdma_of_match);

static struct platform_driver xilinx_dpdma_driver = {
	.probe			= xilinx_dpdma_probe,
	.remove			= xilinx_dpdma_remove,
	.driver			= {
		.name		= "xilinx-zynqmp-dpdma",
		.of_match_table	= xilinx_dpdma_of_match,
	},
};

module_platform_driver(xilinx_dpdma_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx ZynqMP DPDMA driver");
MODULE_LICENSE("GPL v2");
