/*
 * arch/arm/plat-spear/include/plat/padmux.c
 *
 * SPEAr platform specific gpio pads muxing source file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <plat/padmux.h>

/*
 * struct pmx: pmx definition structure
 *
 * base: base address of configuration registers
 * mode_reg: mode configurations
 * mux_reg: muxing configurations
 * active_mode: pointer to current active mode
 */
struct pmx {
	u32 base;
	struct pmx_reg mode_reg;
	struct pmx_reg mux_reg;
	struct pmx_mode *active_mode;
};

static struct pmx *pmx;

/**
 * pmx_mode_set - Enables an multiplexing mode
 * @mode - pointer to pmx mode
 *
 * It will set mode of operation in hardware.
 * Returns -ve on Err otherwise 0
 */
static int pmx_mode_set(struct pmx_mode *mode)
{
	u32 val;

	if (!mode->name)
		return -EFAULT;

	pmx->active_mode = mode;

	val = readl(pmx->base + pmx->mode_reg.offset);
	val &= ~pmx->mode_reg.mask;
	val |= mode->mask & pmx->mode_reg.mask;
	writel(val, pmx->base + pmx->mode_reg.offset);

	return 0;
}

/**
 * pmx_devs_enable - Enables list of devices
 * @devs - pointer to pmx device array
 * @count - number of devices to enable
 *
 * It will enable pads for all required peripherals once and only once.
 * If peripheral is not supported by current mode then request is rejected.
 * Conflicts between peripherals are not handled and peripherals will be
 * enabled in the order they are present in pmx_dev array.
 * In case of conflicts last peripheral enalbed will be present.
 * Returns -ve on Err otherwise 0
 */
static int pmx_devs_enable(struct pmx_dev **devs, u8 count)
{
	u32 val, i, mask;

	if (!count)
		return -EINVAL;

	val = readl(pmx->base + pmx->mux_reg.offset);
	for (i = 0; i < count; i++) {
		u8 j = 0;

		if (!devs[i]->name || !devs[i]->modes) {
			printk(KERN_ERR "padmux: dev name or modes is null\n");
			continue;
		}
		/* check if peripheral exists in active mode */
		if (pmx->active_mode) {
			bool found = false;
			for (j = 0; j < devs[i]->mode_count; j++) {
				if (devs[i]->modes[j].ids &
						pmx->active_mode->id) {
					found = true;
					break;
				}
			}
			if (found == false) {
				printk(KERN_ERR "%s device not available in %s"\
						"mode\n", devs[i]->name,
						pmx->active_mode->name);
				continue;
			}
		}

		/* enable peripheral */
		mask = devs[i]->modes[j].mask & pmx->mux_reg.mask;
		if (devs[i]->enb_on_reset)
			val &= ~mask;
		else
			val |= mask;

		devs[i]->is_active = true;
	}
	writel(val, pmx->base + pmx->mux_reg.offset);
	kfree(pmx);

	/* this will ensure that multiplexing can't be changed now */
	pmx = (struct pmx *)-1;

	return 0;
}

/**
 * pmx_register - registers a platform requesting pad mux feature
 * @driver - pointer to driver structure containing driver specific parameters
 *
 * Also this must be called only once. This will allocate memory for pmx
 * structure, will call pmx_mode_set, will call pmx_devs_enable.
 * Returns -ve on Err otherwise 0
 */
int pmx_register(struct pmx_driver *driver)
{
	int ret = 0;

	if (pmx)
		return -EPERM;
	if (!driver->base || !driver->devs)
		return -EFAULT;

	pmx = kzalloc(sizeof(*pmx), GFP_KERNEL);
	if (!pmx)
		return -ENOMEM;

	pmx->base = (u32)driver->base;
	pmx->mode_reg.offset = driver->mode_reg.offset;
	pmx->mode_reg.mask = driver->mode_reg.mask;
	pmx->mux_reg.offset = driver->mux_reg.offset;
	pmx->mux_reg.mask = driver->mux_reg.mask;

	/* choose mode to enable */
	if (driver->mode) {
		ret = pmx_mode_set(driver->mode);
		if (ret)
			goto pmx_fail;
	}
	ret = pmx_devs_enable(driver->devs, driver->devs_count);
	if (ret)
		goto pmx_fail;

	return 0;

pmx_fail:
	return ret;
}
