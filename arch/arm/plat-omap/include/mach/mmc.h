/*
 * MMC definitions for OMAP2
 *
 * Copyright (C) 2006 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OMAP2_MMC_H
#define __OMAP2_MMC_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/mmc/host.h>

#include <mach/board.h>

#define OMAP_MMC_MAX_SLOTS	2

struct omap_mmc_platform_data {
	struct omap_mmc_conf	conf;

	/* number of slots on board */
	unsigned nr_slots:2;

	/* set if your board has components or wiring that limits the
	 * maximum frequency on the MMC bus */
	unsigned int max_freq;

	/* switch the bus to a new slot */
	int (* switch_slot)(struct device *dev, int slot);
	/* initialize board-specific MMC functionality, can be NULL if
	 * not supported */
	int (* init)(struct device *dev);
	void (* cleanup)(struct device *dev);
	void (* shutdown)(struct device *dev);

	/* To handle board related suspend/resume functionality for MMC */
	int (*suspend)(struct device *dev, int slot);
	int (*resume)(struct device *dev, int slot);

	struct omap_mmc_slot_data {
		int (* set_bus_mode)(struct device *dev, int slot, int bus_mode);
		int (* set_power)(struct device *dev, int slot, int power_on, int vdd);
		int (* get_ro)(struct device *dev, int slot);

		/* return MMC cover switch state, can be NULL if not supported.
		 *
		 * possible return values:
		 *   0 - open
		 *   1 - closed
		 */
		int (* get_cover_state)(struct device *dev, int slot);

		const char *name;
		u32 ocr_mask;

		/* Card detection IRQs */
		int card_detect_irq;
		int (* card_detect)(int irq);

		unsigned int ban_openended:1;

	} slots[OMAP_MMC_MAX_SLOTS];
};

extern void omap_set_mmc_info(int host, const struct omap_mmc_platform_data *info);

/* called from board-specific card detection service routine */
extern void omap_mmc_notify_cover_event(struct device *dev, int slot, int is_closed);

#endif
