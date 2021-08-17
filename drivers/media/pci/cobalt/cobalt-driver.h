/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  cobalt driver internal defines and structures
 *
 *  Derived from cx18-driver.h
 *
 *  Copyright 2012-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

#ifndef COBALT_DRIVER_H
#define COBALT_DRIVER_H

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/i2c.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-sg.h>

#include "m00233_video_measure_memmap_package.h"
#include "m00235_fdma_packer_memmap_package.h"
#include "m00389_cvi_memmap_package.h"
#include "m00460_evcnt_memmap_package.h"
#include "m00473_freewheel_memmap_package.h"
#include "m00479_clk_loss_detector_memmap_package.h"
#include "m00514_syncgen_flow_evcnt_memmap_package.h"

/* System device ID */
#define PCI_DEVICE_ID_COBALT	0x2732

/* Number of cobalt device nodes. */
#define COBALT_NUM_INPUTS	4
#define COBALT_NUM_NODES	6

/* Number of cobalt device streams. */
#define COBALT_NUM_STREAMS	12

#define COBALT_HSMA_IN_NODE	4
#define COBALT_HSMA_OUT_NODE	5

/* Cobalt audio streams */
#define COBALT_AUDIO_IN_STREAM	6
#define COBALT_AUDIO_OUT_STREAM 11

/* DMA stuff */
#define DMA_CHANNELS_MAX	16

/* i2c stuff */
#define I2C_CLIENTS_MAX		16
#define COBALT_NUM_ADAPTERS	5

#define COBALT_CLK		50000000

/* System status register */
#define COBALT_SYSSTAT_DIP0_MSK			BIT(0)
#define COBALT_SYSSTAT_DIP1_MSK			BIT(1)
#define COBALT_SYSSTAT_HSMA_PRSNTN_MSK		BIT(2)
#define COBALT_SYSSTAT_FLASH_RDYBSYN_MSK	BIT(3)
#define COBALT_SYSSTAT_VI0_5V_MSK		BIT(4)
#define COBALT_SYSSTAT_VI0_INT1_MSK		BIT(5)
#define COBALT_SYSSTAT_VI0_INT2_MSK		BIT(6)
#define COBALT_SYSSTAT_VI0_LOST_DATA_MSK	BIT(7)
#define COBALT_SYSSTAT_VI1_5V_MSK		BIT(8)
#define COBALT_SYSSTAT_VI1_INT1_MSK		BIT(9)
#define COBALT_SYSSTAT_VI1_INT2_MSK		BIT(10)
#define COBALT_SYSSTAT_VI1_LOST_DATA_MSK	BIT(11)
#define COBALT_SYSSTAT_VI2_5V_MSK		BIT(12)
#define COBALT_SYSSTAT_VI2_INT1_MSK		BIT(13)
#define COBALT_SYSSTAT_VI2_INT2_MSK		BIT(14)
#define COBALT_SYSSTAT_VI2_LOST_DATA_MSK	BIT(15)
#define COBALT_SYSSTAT_VI3_5V_MSK		BIT(16)
#define COBALT_SYSSTAT_VI3_INT1_MSK		BIT(17)
#define COBALT_SYSSTAT_VI3_INT2_MSK		BIT(18)
#define COBALT_SYSSTAT_VI3_LOST_DATA_MSK	BIT(19)
#define COBALT_SYSSTAT_VIHSMA_5V_MSK		BIT(20)
#define COBALT_SYSSTAT_VIHSMA_INT1_MSK		BIT(21)
#define COBALT_SYSSTAT_VIHSMA_INT2_MSK		BIT(22)
#define COBALT_SYSSTAT_VIHSMA_LOST_DATA_MSK	BIT(23)
#define COBALT_SYSSTAT_VOHSMA_INT1_MSK		BIT(24)
#define COBALT_SYSSTAT_VOHSMA_PLL_LOCKED_MSK	BIT(25)
#define COBALT_SYSSTAT_VOHSMA_LOST_DATA_MSK	BIT(26)
#define COBALT_SYSSTAT_AUD_PLL_LOCKED_MSK	BIT(28)
#define COBALT_SYSSTAT_AUD_IN_LOST_DATA_MSK	BIT(29)
#define COBALT_SYSSTAT_AUD_OUT_LOST_DATA_MSK	BIT(30)
#define COBALT_SYSSTAT_PCIE_SMBCLK_MSK		BIT(31)

/* Cobalt memory map */
#define COBALT_I2C_0_BASE			0x0
#define COBALT_I2C_1_BASE			0x080
#define COBALT_I2C_2_BASE			0x100
#define COBALT_I2C_3_BASE			0x180
#define COBALT_I2C_HSMA_BASE			0x200

#define COBALT_SYS_CTRL_BASE			0x400
#define COBALT_SYS_CTRL_HSMA_TX_ENABLE_BIT	1
#define COBALT_SYS_CTRL_VIDEO_RX_RESETN_BIT(n)	(4 + 4 * (n))
#define COBALT_SYS_CTRL_NRESET_TO_HDMI_BIT(n)	(5 + 4 * (n))
#define COBALT_SYS_CTRL_HPD_TO_CONNECTOR_BIT(n)	(6 + 4 * (n))
#define COBALT_SYS_CTRL_AUDIO_IPP_RESETN_BIT(n)	(7 + 4 * (n))
#define COBALT_SYS_CTRL_PWRDN0_TO_HSMA_TX_BIT	24
#define COBALT_SYS_CTRL_VIDEO_TX_RESETN_BIT	25
#define COBALT_SYS_CTRL_AUDIO_OPP_RESETN_BIT	27

#define COBALT_SYS_STAT_BASE			0x500
#define COBALT_SYS_STAT_MASK			(COBALT_SYS_STAT_BASE + 0x08)
#define COBALT_SYS_STAT_EDGE			(COBALT_SYS_STAT_BASE + 0x0c)

#define COBALT_HDL_INFO_BASE			0x4800
#define COBALT_HDL_INFO_SIZE			0x200

#define COBALT_VID_BASE				0x10000
#define COBALT_VID_SIZE				0x1000

#define COBALT_CVI(cobalt, c) \
	(cobalt->bar1 + COBALT_VID_BASE + (c) * COBALT_VID_SIZE)
#define COBALT_CVI_VMR(cobalt, c) \
	(cobalt->bar1 + COBALT_VID_BASE + (c) * COBALT_VID_SIZE + 0x100)
#define COBALT_CVI_EVCNT(cobalt, c) \
	(cobalt->bar1 + COBALT_VID_BASE + (c) * COBALT_VID_SIZE + 0x200)
#define COBALT_CVI_FREEWHEEL(cobalt, c) \
	(cobalt->bar1 + COBALT_VID_BASE + (c) * COBALT_VID_SIZE + 0x300)
#define COBALT_CVI_CLK_LOSS(cobalt, c) \
	(cobalt->bar1 + COBALT_VID_BASE + (c) * COBALT_VID_SIZE + 0x400)
#define COBALT_CVI_PACKER(cobalt, c) \
	(cobalt->bar1 + COBALT_VID_BASE + (c) * COBALT_VID_SIZE + 0x500)

#define COBALT_TX_BASE(cobalt) (cobalt->bar1 + COBALT_VID_BASE + 0x5000)

#define DMA_INTERRUPT_STATUS_REG		0x08

#define COBALT_HDL_SEARCH_STR			"** HDL version info **"

/* Cobalt CPU bus interface */
#define COBALT_BUS_BAR1_BASE			0x600
#define COBALT_BUS_SRAM_BASE			0x0
#define COBALT_BUS_CPLD_BASE			0x00600000
#define COBALT_BUS_FLASH_BASE			0x08000000

/* FDMA to PCIe packing */
#define COBALT_BYTES_PER_PIXEL_YUYV		2
#define COBALT_BYTES_PER_PIXEL_RGB24		3
#define COBALT_BYTES_PER_PIXEL_RGB32		4

/* debugging */
extern int cobalt_debug;
extern int cobalt_ignore_err;

#define cobalt_err(fmt, arg...)  v4l2_err(&cobalt->v4l2_dev, fmt, ## arg)
#define cobalt_warn(fmt, arg...) v4l2_warn(&cobalt->v4l2_dev, fmt, ## arg)
#define cobalt_info(fmt, arg...) v4l2_info(&cobalt->v4l2_dev, fmt, ## arg)
#define cobalt_dbg(level, fmt, arg...) \
	v4l2_dbg(level, cobalt_debug, &cobalt->v4l2_dev, fmt, ## arg)

struct cobalt;
struct cobalt_i2c_regs;

/* Per I2C bus private algo callback data */
struct cobalt_i2c_data {
	struct cobalt *cobalt;
	struct cobalt_i2c_regs __iomem *regs;
};

struct pci_consistent_buffer {
	void *virt;
	dma_addr_t bus;
	size_t bytes;
};

struct sg_dma_desc_info {
	void *virt;
	dma_addr_t bus;
	unsigned size;
	void *last_desc_virt;
	struct device *dev;
};

#define COBALT_MAX_WIDTH			1920
#define COBALT_MAX_HEIGHT			1200
#define COBALT_MAX_BPP				3
#define COBALT_MAX_FRAMESZ \
	(COBALT_MAX_WIDTH * COBALT_MAX_HEIGHT * COBALT_MAX_BPP)

#define NR_BUFS					VIDEO_MAX_FRAME

#define COBALT_STREAM_FL_DMA_IRQ		0
#define COBALT_STREAM_FL_ADV_IRQ		1

struct cobalt_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

static inline
struct cobalt_buffer *to_cobalt_buffer(struct vb2_v4l2_buffer *vb2)
{
	return container_of(vb2, struct cobalt_buffer, vb);
}

struct cobalt_stream {
	struct video_device vdev;
	struct vb2_queue q;
	struct list_head bufs;
	struct i2c_adapter *i2c_adap;
	struct v4l2_subdev *sd;
	struct mutex lock;
	spinlock_t irqlock;
	struct v4l2_dv_timings timings;
	u32 input;
	u32 pad_source;
	u32 width, height, bpp;
	u32 stride;
	u32 pixfmt;
	u32 sequence;
	u32 colorspace;
	u32 xfer_func;
	u32 ycbcr_enc;
	u32 quantization;

	u8 dma_channel;
	int video_channel;
	unsigned dma_fifo_mask;
	unsigned adv_irq_mask;
	struct sg_dma_desc_info dma_desc_info[NR_BUFS];
	unsigned long flags;
	bool unstable_frame;
	bool enable_cvi;
	bool enable_freewheel;
	unsigned skip_first_frames;
	bool is_output;
	bool is_audio;
	bool is_dummy;

	struct cobalt *cobalt;
	struct snd_cobalt_card *alsa;
};

struct snd_cobalt_card;

/* Struct to hold info about cobalt cards */
struct cobalt {
	int instance;
	struct pci_dev *pci_dev;
	struct v4l2_device v4l2_dev;
	/* serialize PCI access in cobalt_s_bit_sysctrl() */
	struct mutex pci_lock;

	void __iomem *bar0, *bar1;

	u8 card_rev;
	u16 device_id;

	/* device nodes */
	struct cobalt_stream streams[DMA_CHANNELS_MAX];
	struct i2c_adapter i2c_adap[COBALT_NUM_ADAPTERS];
	struct cobalt_i2c_data i2c_data[COBALT_NUM_ADAPTERS];
	bool have_hsma_rx;
	bool have_hsma_tx;

	/* irq */
	struct workqueue_struct *irq_work_queues;
	struct work_struct irq_work_queue;              /* work entry */
	/* irq counters */
	u32 irq_adv1;
	u32 irq_adv2;
	u32 irq_advout;
	u32 irq_dma_tot;
	u32 irq_dma[COBALT_NUM_STREAMS];
	u32 irq_none;
	u32 irq_full_fifo;

	/* omnitek dma */
	int dma_channels;
	int first_fifo_channel;
	bool pci_32_bit;

	char hdl_info[COBALT_HDL_INFO_SIZE];

	/* NOR flash */
	struct mtd_info *mtd;
};

static inline struct cobalt *to_cobalt(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct cobalt, v4l2_dev);
}

static inline void cobalt_write_bar0(struct cobalt *cobalt, u32 reg, u32 val)
{
	iowrite32(val, cobalt->bar0 + reg);
}

static inline u32 cobalt_read_bar0(struct cobalt *cobalt, u32 reg)
{
	return ioread32(cobalt->bar0 + reg);
}

static inline void cobalt_write_bar1(struct cobalt *cobalt, u32 reg, u32 val)
{
	iowrite32(val, cobalt->bar1 + reg);
}

static inline u32 cobalt_read_bar1(struct cobalt *cobalt, u32 reg)
{
	return ioread32(cobalt->bar1 + reg);
}

static inline u32 cobalt_g_sysctrl(struct cobalt *cobalt)
{
	return cobalt_read_bar1(cobalt, COBALT_SYS_CTRL_BASE);
}

static inline void cobalt_s_bit_sysctrl(struct cobalt *cobalt,
					int bit, int val)
{
	u32 ctrl;

	mutex_lock(&cobalt->pci_lock);
	ctrl = cobalt_read_bar1(cobalt, COBALT_SYS_CTRL_BASE);
	cobalt_write_bar1(cobalt, COBALT_SYS_CTRL_BASE,
			(ctrl & ~(1UL << bit)) | (val << bit));
	mutex_unlock(&cobalt->pci_lock);
}

static inline u32 cobalt_g_sysstat(struct cobalt *cobalt)
{
	return cobalt_read_bar1(cobalt, COBALT_SYS_STAT_BASE);
}

#define ADRS_REG (bar1 + COBALT_BUS_BAR1_BASE + 0)
#define LOWER_DATA (bar1 + COBALT_BUS_BAR1_BASE + 4)
#define UPPER_DATA (bar1 + COBALT_BUS_BAR1_BASE + 6)

static inline u32 cobalt_bus_read32(void __iomem *bar1, u32 bus_adrs)
{
	iowrite32(bus_adrs, ADRS_REG);
	return ioread32(LOWER_DATA);
}

static inline void cobalt_bus_write16(void __iomem *bar1,
				      u32 bus_adrs, u16 data)
{
	iowrite32(bus_adrs, ADRS_REG);
	if (bus_adrs & 2)
		iowrite16(data, UPPER_DATA);
	else
		iowrite16(data, LOWER_DATA);
}

static inline void cobalt_bus_write32(void __iomem *bar1,
				      u32 bus_adrs, u16 data)
{
	iowrite32(bus_adrs, ADRS_REG);
	if (bus_adrs & 2)
		iowrite32(data, UPPER_DATA);
	else
		iowrite32(data, LOWER_DATA);
}

/*==============Prototypes==================*/

void cobalt_pcie_status_show(struct cobalt *cobalt);

#endif
