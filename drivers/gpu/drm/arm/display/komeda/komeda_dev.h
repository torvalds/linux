/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#ifndef _KOMEDA_DEV_H_
#define _KOMEDA_DEV_H_

#include <linux/device.h>
#include <linux/clk.h>
#include "komeda_pipeline.h"
#include "malidp_product.h"
#include "komeda_format_caps.h"

#define KOMEDA_EVENT_VSYNC		BIT_ULL(0)
#define KOMEDA_EVENT_FLIP		BIT_ULL(1)
#define KOMEDA_EVENT_URUN		BIT_ULL(2)
#define KOMEDA_EVENT_IBSY		BIT_ULL(3)
#define KOMEDA_EVENT_OVR		BIT_ULL(4)
#define KOMEDA_EVENT_EOW		BIT_ULL(5)
#define KOMEDA_EVENT_MODE		BIT_ULL(6)

#define KOMEDA_ERR_TETO			BIT_ULL(14)
#define KOMEDA_ERR_TEMR			BIT_ULL(15)
#define KOMEDA_ERR_TITR			BIT_ULL(16)
#define KOMEDA_ERR_CPE			BIT_ULL(17)
#define KOMEDA_ERR_CFGE			BIT_ULL(18)
#define KOMEDA_ERR_AXIE			BIT_ULL(19)
#define KOMEDA_ERR_ACE0			BIT_ULL(20)
#define KOMEDA_ERR_ACE1			BIT_ULL(21)
#define KOMEDA_ERR_ACE2			BIT_ULL(22)
#define KOMEDA_ERR_ACE3			BIT_ULL(23)
#define KOMEDA_ERR_DRIFTTO		BIT_ULL(24)
#define KOMEDA_ERR_FRAMETO		BIT_ULL(25)
#define KOMEDA_ERR_CSCE			BIT_ULL(26)
#define KOMEDA_ERR_ZME			BIT_ULL(27)
#define KOMEDA_ERR_MERR			BIT_ULL(28)
#define KOMEDA_ERR_TCF			BIT_ULL(29)
#define KOMEDA_ERR_TTNG			BIT_ULL(30)
#define KOMEDA_ERR_TTF			BIT_ULL(31)

/* malidp device id */
enum {
	MALI_D71 = 0,
};

/* pipeline DT ports */
enum {
	KOMEDA_OF_PORT_OUTPUT		= 0,
	KOMEDA_OF_PORT_COPROC		= 1,
};

struct komeda_chip_info {
	u32 arch_id;
	u32 core_id;
	u32 core_info;
	u32 bus_width;
};

struct komeda_product_data {
	u32 product_id;
	const struct komeda_dev_funcs *(*identify)(u32 __iomem *reg,
					     struct komeda_chip_info *info);
};

struct komeda_dev;

struct komeda_events {
	u64 global;
	u64 pipes[KOMEDA_MAX_PIPELINES];
};

/**
 * struct komeda_dev_funcs
 *
 * Supplied by chip level and returned by the chip entry function xxx_identify,
 */
struct komeda_dev_funcs {
	/**
	 * @init_format_table:
	 *
	 * initialize &komeda_dev->format_table, this function should be called
	 * before the &enum_resource
	 */
	void (*init_format_table)(struct komeda_dev *mdev);
	/**
	 * @enum_resources:
	 *
	 * for CHIP to report or add pipeline and component resources to CORE
	 */
	int (*enum_resources)(struct komeda_dev *mdev);
	/** @cleanup: call to chip to cleanup komeda_dev->chip data */
	void (*cleanup)(struct komeda_dev *mdev);
	/**
	 * @irq_handler:
	 *
	 * for CORE to get the HW event from the CHIP when interrupt happened.
	 */
	irqreturn_t (*irq_handler)(struct komeda_dev *mdev,
				   struct komeda_events *events);
	/** @enable_irq: enable irq */
	int (*enable_irq)(struct komeda_dev *mdev);
	/** @disable_irq: disable irq */
	int (*disable_irq)(struct komeda_dev *mdev);
	/** @on_off_vblank: notify HW to on/off vblank */
	void (*on_off_vblank)(struct komeda_dev *mdev,
			      int master_pipe, bool on);

	/** @dump_register: Optional, dump registers to seq_file */
	void (*dump_register)(struct komeda_dev *mdev, struct seq_file *seq);
	/**
	 * @change_opmode:
	 *
	 * Notify HW to switch to a new display operation mode.
	 */
	int (*change_opmode)(struct komeda_dev *mdev, int new_mode);
	/** @flush: Notify the HW to flush or kickoff the update */
	void (*flush)(struct komeda_dev *mdev,
		      int master_pipe, u32 active_pipes);
};

/*
 * DISPLAY_MODE describes how many display been enabled, and which will be
 * passed to CHIP by &komeda_dev_funcs->change_opmode(), then CHIP can do the
 * pipeline resources assignment according to this usage hint.
 * -   KOMEDA_MODE_DISP0: Only one display enabled, pipeline-0 work as master.
 * -   KOMEDA_MODE_DISP1: Only one display enabled, pipeline-0 work as master.
 * -   KOMEDA_MODE_DUAL_DISP: Dual display mode, both display has been enabled.
 * And D71 supports assign two pipelines to one single display on mode
 * KOMEDA_MODE_DISP0/DISP1
 */
enum {
	KOMEDA_MODE_INACTIVE	= 0,
	KOMEDA_MODE_DISP0	= BIT(0),
	KOMEDA_MODE_DISP1	= BIT(1),
	KOMEDA_MODE_DUAL_DISP	= KOMEDA_MODE_DISP0 | KOMEDA_MODE_DISP1,
};

/**
 * struct komeda_dev
 *
 * Pipeline and component are used to describe how to handle the pixel data.
 * komeda_device is for describing the whole view of the device, and the
 * control-abilites of device.
 */
struct komeda_dev {
	/** @dev: the base device structure */
	struct device *dev;
	/** @reg_base: the base address of komeda io space */
	u32 __iomem   *reg_base;
	/** @dma_parms: the dma parameters of komeda */
	struct device_dma_parameters dma_parms;

	/** @chip: the basic chip information */
	struct komeda_chip_info chip;
	/** @fmt_tbl: initialized by &komeda_dev_funcs->init_format_table */
	struct komeda_format_caps_table fmt_tbl;
	/** @pclk: APB clock for register access */
	struct clk *pclk;
	/** @mclk: HW main engine clk */
	struct clk *mclk;

	/** @irq: irq number */
	int irq;

	/** @lock: used to protect dpmode */
	struct mutex lock;
	/** @dpmode: current display mode */
	u32 dpmode;

	/** @n_pipelines: the number of pipe in @pipelines */
	int n_pipelines;
	/** @pipelines: the komeda pipelines */
	struct komeda_pipeline *pipelines[KOMEDA_MAX_PIPELINES];

	/** @funcs: chip funcs to access to HW */
	const struct komeda_dev_funcs *funcs;
	/**
	 * @chip_data:
	 *
	 * chip data will be added by &komeda_dev_funcs.enum_resources() and
	 * destroyed by &komeda_dev_funcs.cleanup()
	 */
	void *chip_data;

	/** @debugfs_root: root directory of komeda debugfs */
	struct dentry *debugfs_root;
};

static inline bool
komeda_product_match(struct komeda_dev *mdev, u32 target)
{
	return MALIDP_CORE_ID_PRODUCT_ID(mdev->chip.core_id) == target;
}

const struct komeda_dev_funcs *
d71_identify(u32 __iomem *reg, struct komeda_chip_info *chip);

struct komeda_dev *komeda_dev_create(struct device *dev);
void komeda_dev_destroy(struct komeda_dev *mdev);

struct komeda_dev *dev_to_mdev(struct device *dev);

#endif /*_KOMEDA_DEV_H_*/
