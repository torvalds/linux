/*
 *
 * arch/arm/mach-u300/padmux.c
 *
 *
 * Copyright (C) 2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * U300 PADMUX functions
 * Author: Martin Persson <martin.persson@stericsson.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/bug.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <mach/u300-regs.h>
#include <mach/syscon.h>
#include "padmux.h"

static DEFINE_MUTEX(pmx_mutex);

const u32 pmx_registers[] = {
	(U300_SYSCON_VBASE + U300_SYSCON_PMC1LR),
	(U300_SYSCON_VBASE + U300_SYSCON_PMC1HR),
	(U300_SYSCON_VBASE + U300_SYSCON_PMC2R),
	(U300_SYSCON_VBASE + U300_SYSCON_PMC3R),
	(U300_SYSCON_VBASE + U300_SYSCON_PMC4R)
};

/* High level functionality */

/* Lazy dog:
 * onmask = {
 *   {"PMC1LR" mask, "PMC1LR" value},
 *   {"PMC1HR" mask, "PMC1HR" value},
 *   {"PMC2R"  mask, "PMC2R"  value},
 *   {"PMC3R"  mask, "PMC3R"  value},
 *   {"PMC4R"  mask, "PMC4R"  value}
 * }
 */
static struct pmx mmc_setting = {
	.setting = U300_APP_PMX_MMC_SETTING,
	.default_on = false,
	.activated = false,
	.name = "MMC",
	.onmask = {
		   {U300_SYSCON_PMC1LR_MMCSD_MASK,
		    U300_SYSCON_PMC1LR_MMCSD_MMCSD},
		   {0, 0},
		   {0, 0},
		   {0, 0},
		   {U300_SYSCON_PMC4R_APP_MISC_12_MASK,
		    U300_SYSCON_PMC4R_APP_MISC_12_APP_GPIO}
		   },
};

static struct pmx spi_setting = {
	.setting = U300_APP_PMX_SPI_SETTING,
	.default_on = false,
	.activated = false,
	.name = "SPI",
	.onmask = {{0, 0},
		   {U300_SYSCON_PMC1HR_APP_SPI_2_MASK |
		    U300_SYSCON_PMC1HR_APP_SPI_CS_1_MASK |
		    U300_SYSCON_PMC1HR_APP_SPI_CS_2_MASK,
		    U300_SYSCON_PMC1HR_APP_SPI_2_SPI |
		    U300_SYSCON_PMC1HR_APP_SPI_CS_1_SPI |
		    U300_SYSCON_PMC1HR_APP_SPI_CS_2_SPI},
		   {0, 0},
		   {0, 0},
		   {0, 0}
		   },
};

/* Available padmux settings */
static struct pmx *pmx_settings[] = {
	&mmc_setting,
	&spi_setting,
};

static void update_registers(struct pmx *pmx, bool activate)
{
	u16 regval, val, mask;
	int i;

	for (i = 0; i < ARRAY_SIZE(pmx_registers); i++) {
		if (activate)
			val = pmx->onmask[i].val;
		else
			val = 0;

		mask = pmx->onmask[i].mask;
		if (mask != 0) {
			regval = readw(pmx_registers[i]);
			regval &= ~mask;
			regval |= val;
			writew(regval, pmx_registers[i]);
		}
	}
}

struct pmx *pmx_get(struct device *dev, enum pmx_settings setting)
{
	int i;
	struct pmx *pmx = ERR_PTR(-ENOENT);

	if (dev == NULL)
		return ERR_PTR(-EINVAL);

	mutex_lock(&pmx_mutex);
	for (i = 0; i < ARRAY_SIZE(pmx_settings); i++) {

		if (setting == pmx_settings[i]->setting) {

			if (pmx_settings[i]->dev != NULL) {
				WARN(1, "padmux: required setting "
				     "in use by another consumer\n");
			} else {
				pmx = pmx_settings[i];
				pmx->dev = dev;
				dev_dbg(dev, "padmux: setting nr %d is now "
					"bound to %s and ready to use\n",
					setting, dev_name(dev));
				break;
			}
		}
	}
	mutex_unlock(&pmx_mutex);

	return pmx;
}
EXPORT_SYMBOL(pmx_get);

int pmx_put(struct device *dev, struct pmx *pmx)
{
	int i;
	int ret = -ENOENT;

	if (pmx == NULL || dev == NULL)
		return -EINVAL;

	mutex_lock(&pmx_mutex);
	for (i = 0; i < ARRAY_SIZE(pmx_settings); i++) {

		if (pmx->setting == pmx_settings[i]->setting) {

			if (dev != pmx->dev) {
				WARN(1, "padmux: cannot release handle as "
					"it is bound to another consumer\n");
				ret = -EINVAL;
				break;
			} else {
				pmx_settings[i]->dev = NULL;
				ret = 0;
				break;
			}
		}
	}
	mutex_unlock(&pmx_mutex);

	return ret;
}
EXPORT_SYMBOL(pmx_put);

int pmx_activate(struct device *dev, struct pmx *pmx)
{
	int i, j, ret;
	ret = 0;

	if (pmx == NULL || dev == NULL)
		return -EINVAL;

	mutex_lock(&pmx_mutex);

	/* Make sure the required bits are not used */
	for (i = 0; i < ARRAY_SIZE(pmx_settings); i++) {

		if (pmx_settings[i]->dev == NULL || pmx_settings[i] == pmx)
			continue;

		for (j = 0; j < ARRAY_SIZE(pmx_registers); j++) {

			if (pmx_settings[i]->onmask[j].mask & pmx->
				onmask[j].mask) {
				/* More than one entry on the same bits */
				WARN(1, "padmux: cannot activate "
					"setting. Bit conflict with "
					"an active setting\n");

				ret = -EUSERS;
				goto exit;
			}
		}
	}
	update_registers(pmx, true);
	pmx->activated = true;
	dev_dbg(dev, "padmux: setting nr %d is activated\n",
		pmx->setting);

exit:
	mutex_unlock(&pmx_mutex);
	return ret;
}
EXPORT_SYMBOL(pmx_activate);

int pmx_deactivate(struct device *dev, struct pmx *pmx)
{
	int i;
	int ret = -ENOENT;

	if (pmx == NULL || dev == NULL)
		return -EINVAL;

	mutex_lock(&pmx_mutex);
	for (i = 0; i < ARRAY_SIZE(pmx_settings); i++) {

		if (pmx_settings[i]->dev == NULL)
			continue;

		if (pmx->setting == pmx_settings[i]->setting) {

			if (dev != pmx->dev) {
				WARN(1, "padmux: cannot deactivate "
				     "pmx setting as it was activated "
				     "by another consumer\n");

				ret = -EBUSY;
				continue;
			} else {
				update_registers(pmx, false);
				pmx_settings[i]->dev = NULL;
				pmx->activated = false;
				ret = 0;
				dev_dbg(dev, "padmux: setting nr %d is deactivated",
					pmx->setting);
				break;
			}
		}
	}
	mutex_unlock(&pmx_mutex);

	return ret;
}
EXPORT_SYMBOL(pmx_deactivate);

/*
 * For internal use only. If it is to be exported,
 * it should be reentrant. Notice that pmx_activate
 * (i.e. runtime settings) always override default settings.
 */
static int pmx_set_default(void)
{
	/* Used to identify several entries on the same bits */
	u16 modbits[ARRAY_SIZE(pmx_registers)];

	int i, j;

	memset(modbits, 0, ARRAY_SIZE(pmx_registers) * sizeof(u16));

	for (i = 0; i < ARRAY_SIZE(pmx_settings); i++) {

		if (!pmx_settings[i]->default_on)
			continue;

		for (j = 0; j < ARRAY_SIZE(pmx_registers); j++) {

			/* Make sure there is only one entry on the same bits */
			if (modbits[j] & pmx_settings[i]->onmask[j].mask) {
				BUG();
				return -EUSERS;
			}
			modbits[j] |= pmx_settings[i]->onmask[j].mask;
		}
		update_registers(pmx_settings[i], true);
	}
	return 0;
}

#if (defined(CONFIG_DEBUG_FS) && defined(CONFIG_U300_DEBUG))
static int pmx_show(struct seq_file *s, void *data)
{
	int i;
	seq_printf(s, "-------------------------------------------------\n");
	seq_printf(s, "SETTING     BOUND TO DEVICE               STATE\n");
	seq_printf(s, "-------------------------------------------------\n");
	mutex_lock(&pmx_mutex);
	for (i = 0; i < ARRAY_SIZE(pmx_settings); i++) {
		/* Format pmx and device name nicely */
		char cdp[33];
		int chars;

		chars = snprintf(&cdp[0], 17, "%s", pmx_settings[i]->name);
		while (chars < 16) {
			cdp[chars] = ' ';
			chars++;
		}
		chars = snprintf(&cdp[16], 17, "%s", pmx_settings[i]->dev ?
				dev_name(pmx_settings[i]->dev) : "N/A");
		while (chars < 16) {
			cdp[chars+16] = ' ';
			chars++;
		}
		cdp[32] = '\0';

		seq_printf(s,
			"%s\t%s\n",
			&cdp[0],
			pmx_settings[i]->activated ?
			"ACTIVATED" : "DEACTIVATED"
			);

	}
	mutex_unlock(&pmx_mutex);
	return 0;
}

static int pmx_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmx_show, NULL);
}

static const struct file_operations pmx_operations = {
	.owner		= THIS_MODULE,
	.open		= pmx_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init init_pmx_read_debugfs(void)
{
	/* Expose a simple debugfs interface to view pmx settings */
	(void) debugfs_create_file("padmux", S_IFREG | S_IRUGO,
				   NULL, NULL,
				   &pmx_operations);
	return 0;
}

/*
 * This needs to come in after the core_initcall(),
 * because debugfs is not available until
 * the subsystems come up.
 */
module_init(init_pmx_read_debugfs);
#endif

static int __init pmx_init(void)
{
	int ret;

	ret = pmx_set_default();

	if (IS_ERR_VALUE(ret))
		pr_crit("padmux: default settings could not be set\n");

	return 0;
}

/* Should be initialized before consumers */
core_initcall(pmx_init);
