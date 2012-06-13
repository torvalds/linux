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

#include <plat/board.h>
#include <plat/omap_hwmod.h>

#define OMAP15XX_NR_MMC		1
#define OMAP16XX_NR_MMC		2
#define OMAP1_MMC_SIZE		0x080
#define OMAP1_MMC1_BASE		0xfffb7800
#define OMAP1_MMC2_BASE		0xfffb7c00	/* omap16xx only */

#define OMAP24XX_NR_MMC		2
#define OMAP2420_MMC_SIZE	OMAP1_MMC_SIZE
#define OMAP2_MMC1_BASE		0x4809c000

#define OMAP4_MMC_REG_OFFSET	0x100

#define OMAP_MMC_MAX_SLOTS	2

/*
 * struct omap_mmc_dev_attr.flags possibilities
 *
 * OMAP_HSMMC_SUPPORTS_DUAL_VOLT: Some HSMMC controller instances can
 *    operate with either 1.8Vdc or 3.0Vdc card voltages; this flag
 *    should be set if this is the case.  See for example Section 22.5.3
 *    "MMC/SD/SDIO1 Bus Voltage Selection" of the OMAP34xx Multimedia
 *    Device Silicon Revision 3.1.x Revision ZR (July 2011) (SWPU223R).
 *
 * OMAP_HSMMC_BROKEN_MULTIBLOCK_READ: Multiple-block read transfers
 *    don't work correctly on some MMC controller instances on some
 *    OMAP3 SoCs; this flag should be set if this is the case.  See
 *    for example Advisory 2.1.1.128 "MMC: Multiple Block Read
 *    Operation Issue" in _OMAP3530/3525/3515/3503 Silicon Errata_
 *    Revision F (October 2010) (SPRZ278F).
 */
#define OMAP_HSMMC_SUPPORTS_DUAL_VOLT		BIT(0)
#define OMAP_HSMMC_BROKEN_MULTIBLOCK_READ	BIT(1)

struct omap_mmc_dev_attr {
	u8 flags;
};

struct omap_mmc_platform_data {
	/* back-link to device */
	struct device *dev;

	/* number of slots per controller */
	unsigned nr_slots:2;

	/* set if your board has components or wiring that limits the
	 * maximum frequency on the MMC bus */
	unsigned int max_freq;

	/* switch the bus to a new slot */
	int (*switch_slot)(struct device *dev, int slot);
	/* initialize board-specific MMC functionality, can be NULL if
	 * not supported */
	int (*init)(struct device *dev);
	void (*cleanup)(struct device *dev);
	void (*shutdown)(struct device *dev);

	/* To handle board related suspend/resume functionality for MMC */
	int (*suspend)(struct device *dev, int slot);
	int (*resume)(struct device *dev, int slot);

	/* Return context loss count due to PM states changing */
	int (*get_context_loss_count)(struct device *dev);

	u64 dma_mask;

	/* Integrating attributes from the omap_hwmod layer */
	u8 controller_flags;

	/* Register offset deviation */
	u16 reg_offset;

	struct omap_mmc_slot_data {

		/*
		 * 4/8 wires and any additional host capabilities
		 * need to OR'd all capabilities (ref. linux/mmc/host.h)
		 */
		u8  wires;	/* Used for the MMC driver on omap1 and 2420 */
		u32 caps;	/* Used for the MMC driver on 2430 and later */
		u32 pm_caps;	/* PM capabilities of the mmc */

		/*
		 * nomux means "standard" muxing is wrong on this board, and
		 * that board-specific code handled it before common init logic.
		 */
		unsigned nomux:1;

		/* switch pin can be for card detect (default) or card cover */
		unsigned cover:1;

		/* use the internal clock */
		unsigned internal_clock:1;

		/* nonremovable e.g. eMMC */
		unsigned nonremovable:1;

		/* Try to sleep or power off when possible */
		unsigned power_saving:1;

		/* If using power_saving and the MMC power is not to go off */
		unsigned no_off:1;

		/* eMMC does not handle power off when not in sleep state */
		unsigned no_regulator_off_init:1;

		/* Regulator off remapped to sleep */
		unsigned vcc_aux_disable_is_sleep:1;

		/* we can put the features above into this variable */
#define HSMMC_HAS_PBIAS		(1 << 0)
#define HSMMC_HAS_UPDATED_RESET	(1 << 1)
		unsigned features;

		int switch_pin;			/* gpio (card detect) */
		int gpio_wp;			/* gpio (write protect) */

		int (*set_bus_mode)(struct device *dev, int slot, int bus_mode);
		int (*set_power)(struct device *dev, int slot,
				 int power_on, int vdd);
		int (*get_ro)(struct device *dev, int slot);
		void (*remux)(struct device *dev, int slot, int power_on);
		/* Call back before enabling / disabling regulators */
		void (*before_set_reg)(struct device *dev, int slot,
				       int power_on, int vdd);
		/* Call back after enabling / disabling regulators */
		void (*after_set_reg)(struct device *dev, int slot,
				      int power_on, int vdd);
		/* if we have special card, init it using this callback */
		void (*init_card)(struct mmc_card *card);

		/* return MMC cover switch state, can be NULL if not supported.
		 *
		 * possible return values:
		 *   0 - closed
		 *   1 - open
		 */
		int (*get_cover_state)(struct device *dev, int slot);

		const char *name;
		u32 ocr_mask;

		/* Card detection IRQs */
		int card_detect_irq;
		int (*card_detect)(struct device *dev, int slot);

		unsigned int ban_openended:1;

	} slots[OMAP_MMC_MAX_SLOTS];
};

/* called from board-specific card detection service routine */
extern void omap_mmc_notify_cover_event(struct device *dev, int slot,
					int is_closed);

#if	defined(CONFIG_MMC_OMAP) || defined(CONFIG_MMC_OMAP_MODULE) || \
	defined(CONFIG_MMC_OMAP_HS) || defined(CONFIG_MMC_OMAP_HS_MODULE)
void omap1_init_mmc(struct omap_mmc_platform_data **mmc_data,
				int nr_controllers);
void omap242x_init_mmc(struct omap_mmc_platform_data **mmc_data);
#else
static inline void omap1_init_mmc(struct omap_mmc_platform_data **mmc_data,
				int nr_controllers)
{
}
static inline void omap242x_init_mmc(struct omap_mmc_platform_data **mmc_data)
{
}

#endif

extern int omap_msdi_reset(struct omap_hwmod *oh);

#endif
