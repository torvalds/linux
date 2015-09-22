/* OMAP SSI internal interface.
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2013 Sebastian Reichel
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __LINUX_HSI_OMAP_SSI_H__
#define __LINUX_HSI_OMAP_SSI_H__

#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/hsi/hsi.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#define SSI_MAX_CHANNELS	8
#define SSI_MAX_GDD_LCH		8
#define SSI_BYTES_TO_FRAMES(x) ((((x) - 1) >> 2) + 1)

/**
 * struct omap_ssm_ctx - OMAP synchronous serial module (TX/RX) context
 * @mode: Bit transmission mode
 * @channels: Number of channels
 * @framesize: Frame size in bits
 * @timeout: RX frame timeout
 * @divisor: TX divider
 * @arb_mode: Arbitration mode for TX frame (Round robin, priority)
 */
struct omap_ssm_ctx {
	u32	mode;
	u32	channels;
	u32	frame_size;
	union	{
			u32	timeout; /* Rx Only */
			struct	{
					u32	arb_mode;
					u32	divisor;
			}; /* Tx only */
	};
};

/**
 * struct omap_ssi_port - OMAP SSI port data
 * @dev: device associated to the port (HSI port)
 * @pdev: platform device associated to the port
 * @sst_dma: SSI transmitter physical base address
 * @ssr_dma: SSI receiver physical base address
 * @sst_base: SSI transmitter base address
 * @ssr_base: SSI receiver base address
 * @wk_lock: spin lock to serialize access to the wake lines
 * @lock: Spin lock to serialize access to the SSI port
 * @channels: Current number of channels configured (1,2,4 or 8)
 * @txqueue: TX message queues
 * @rxqueue: RX message queues
 * @brkqueue: Queue of incoming HWBREAK requests (FRAME mode)
 * @irq: IRQ number
 * @wake_irq: IRQ number for incoming wake line (-1 if none)
 * @wake_gpio: GPIO number for incoming wake line (-1 if none)
 * @pio_tasklet: Bottom half for PIO transfers and events
 * @wake_tasklet: Bottom half for incoming wake events
 * @wkin_cken: Keep track of clock references due to the incoming wake line
 * @wk_refcount: Reference count for output wake line
 * @sys_mpu_enable: Context for the interrupt enable register for irq 0
 * @sst: Context for the synchronous serial transmitter
 * @ssr: Context for the synchronous serial receiver
 */
struct omap_ssi_port {
	struct device		*dev;
	struct device           *pdev;
	dma_addr_t		sst_dma;
	dma_addr_t		ssr_dma;
	void __iomem		*sst_base;
	void __iomem		*ssr_base;
	spinlock_t		wk_lock;
	spinlock_t		lock;
	unsigned int		channels;
	struct list_head	txqueue[SSI_MAX_CHANNELS];
	struct list_head	rxqueue[SSI_MAX_CHANNELS];
	struct list_head	brkqueue;
	unsigned int		irq;
	int			wake_irq;
	int			wake_gpio;
	struct tasklet_struct	pio_tasklet;
	struct tasklet_struct	wake_tasklet;
	bool			wktest:1; /* FIXME: HACK to be removed */
	bool			wkin_cken:1; /* Workaround */
	unsigned int		wk_refcount;
	/* OMAP SSI port context */
	u32			sys_mpu_enable; /* We use only one irq */
	struct omap_ssm_ctx	sst;
	struct omap_ssm_ctx	ssr;
	u32			loss_count;
	u32			port_id;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dir;
#endif
};

/**
 * struct gdd_trn - GDD transaction data
 * @msg: Pointer to the HSI message being served
 * @sg: Pointer to the current sg entry being served
 */
struct gdd_trn {
	struct hsi_msg		*msg;
	struct scatterlist	*sg;
};

/**
 * struct omap_ssi_controller - OMAP SSI controller data
 * @dev: device associated to the controller (HSI controller)
 * @sys: SSI I/O base address
 * @gdd: GDD I/O base address
 * @fck: SSI functional clock
 * @gdd_irq: IRQ line for GDD
 * @gdd_tasklet: bottom half for DMA transfers
 * @gdd_trn: Array of GDD transaction data for ongoing GDD transfers
 * @lock: lock to serialize access to GDD
 * @loss_count: To follow if we need to restore context or not
 * @max_speed: Maximum TX speed (Kb/s) set by the clients.
 * @sysconfig: SSI controller saved context
 * @gdd_gcr: SSI GDD saved context
 * @get_loss: Pointer to omap_pm_get_dev_context_loss_count, if any
 * @port: Array of pointers of the ports of the controller
 * @dir: Debugfs SSI root directory
 */
struct omap_ssi_controller {
	struct device		*dev;
	void __iomem		*sys;
	void __iomem		*gdd;
	struct clk		*fck;
	unsigned int		gdd_irq;
	struct tasklet_struct	gdd_tasklet;
	struct gdd_trn		gdd_trn[SSI_MAX_GDD_LCH];
	spinlock_t		lock;
	unsigned long		fck_rate;
	u32			loss_count;
	u32			max_speed;
	/* OMAP SSI Controller context */
	u32			sysconfig;
	u32			gdd_gcr;
	int			(*get_loss)(struct device *dev);
	struct omap_ssi_port	**port;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dir;
#endif
};

#endif /* __LINUX_HSI_OMAP_SSI_H__ */
