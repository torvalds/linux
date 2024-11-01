/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */
#ifndef __ASM_CDMM_H
#define __ASM_CDMM_H

#include <linux/device.h>
#include <linux/mod_devicetable.h>

/**
 * struct mips_cdmm_device - Represents a single device on a CDMM bus.
 * @dev:	Driver model device object.
 * @cpu:	CPU which can access this device.
 * @res:	MMIO resource.
 * @type:	Device type identifier.
 * @rev:	Device revision number.
 */
struct mips_cdmm_device {
	struct device		dev;
	unsigned int		cpu;
	struct resource		res;
	unsigned int		type;
	unsigned int		rev;
};

/**
 * struct mips_cdmm_driver - Represents a driver for a CDMM device.
 * @drv:	Driver model driver object.
 * @probe	Callback for probing newly discovered devices.
 * @remove:	Callback to remove the device.
 * @shutdown:	Callback on system shutdown.
 * @cpu_down:	Callback when the parent CPU is going down.
 *		Any CPU pinned threads/timers should be disabled.
 * @cpu_up:	Callback when the parent CPU is coming back up again.
 *		CPU pinned threads/timers can be restarted.
 * @id_table:	Table for CDMM IDs to match against.
 */
struct mips_cdmm_driver {
	struct device_driver	drv;
	int			(*probe)(struct mips_cdmm_device *);
	int			(*remove)(struct mips_cdmm_device *);
	void			(*shutdown)(struct mips_cdmm_device *);
	int			(*cpu_down)(struct mips_cdmm_device *);
	int			(*cpu_up)(struct mips_cdmm_device *);
	const struct mips_cdmm_device_id *id_table;
};

/**
 * mips_cdmm_phys_base() - Choose a physical base address for CDMM region.
 *
 * Picking a suitable physical address at which to map the CDMM region is
 * platform specific, so this function can be defined by platform code to
 * pick a suitable value if none is configured by the bootloader.
 *
 * This address must be 32kB aligned, and the region occupies a maximum of 32kB
 * of physical address space which must not be used for anything else.
 *
 * Returns:	Physical base address for CDMM region, or 0 on failure.
 */
phys_addr_t mips_cdmm_phys_base(void);

extern struct bus_type mips_cdmm_bustype;
void __iomem *mips_cdmm_early_probe(unsigned int dev_type);

#define to_mips_cdmm_device(d)	container_of(d, struct mips_cdmm_device, dev)

#define mips_cdmm_get_drvdata(d)	dev_get_drvdata(&d->dev)
#define mips_cdmm_set_drvdata(d, p)	dev_set_drvdata(&d->dev, p)

int mips_cdmm_driver_register(struct mips_cdmm_driver *);
void mips_cdmm_driver_unregister(struct mips_cdmm_driver *);

/*
 * module_mips_cdmm_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#define module_mips_cdmm_driver(__mips_cdmm_driver) \
	module_driver(__mips_cdmm_driver, mips_cdmm_driver_register, \
			mips_cdmm_driver_unregister)

/*
 * builtin_mips_cdmm_driver() - Helper macro for drivers that don't do anything
 * special in init and have no exit. This eliminates some boilerplate. Each
 * driver may only use this macro once, and calling it replaces device_initcall
 * (or in some cases, the legacy __initcall). This is meant to be a direct
 * parallel of module_mips_cdmm_driver() above but without the __exit stuff that
 * is not used for builtin cases.
 */
#define builtin_mips_cdmm_driver(__mips_cdmm_driver) \
	builtin_driver(__mips_cdmm_driver, mips_cdmm_driver_register)

/* drivers/tty/mips_ejtag_fdc.c */

#ifdef CONFIG_MIPS_EJTAG_FDC_EARLYCON
int setup_early_fdc_console(void);
#else
static inline int setup_early_fdc_console(void)
{
	return -ENODEV;
}
#endif

#endif /* __ASM_CDMM_H */
