// SPDX-License-Identifier: GPL-2.0-only
/*
 * Debugfs support for hosts and cards
 *
 * Copyright (C) 2008 Atmel Corporation
 */
#include <linux/moduleparam.h>
#include <linux/export.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/fault-inject.h>
#include <linux/time.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>

#include "core.h"
#include "card.h"
#include "host.h"
#include "mmc_ops.h"

#ifdef CONFIG_FAIL_MMC_REQUEST

static DECLARE_FAULT_ATTR(fail_default_attr);
static char *fail_request;
module_param(fail_request, charp, 0);
MODULE_PARM_DESC(fail_request, "default fault injection attributes");

#endif /* CONFIG_FAIL_MMC_REQUEST */

/* The debugfs functions are optimized away when CONFIG_DEBUG_FS isn't set. */
static int mmc_ios_show(struct seq_file *s, void *data)
{
	static const char *vdd_str[] = {
		[8]	= "2.0",
		[9]	= "2.1",
		[10]	= "2.2",
		[11]	= "2.3",
		[12]	= "2.4",
		[13]	= "2.5",
		[14]	= "2.6",
		[15]	= "2.7",
		[16]	= "2.8",
		[17]	= "2.9",
		[18]	= "3.0",
		[19]	= "3.1",
		[20]	= "3.2",
		[21]	= "3.3",
		[22]	= "3.4",
		[23]	= "3.5",
		[24]	= "3.6",
	};
	struct mmc_host	*host = s->private;
	struct mmc_ios	*ios = &host->ios;
	const char *str;

	seq_printf(s, "clock:\t\t%u Hz\n", ios->clock);
	if (host->actual_clock)
		seq_printf(s, "actual clock:\t%u Hz\n", host->actual_clock);
	seq_printf(s, "vdd:\t\t%u ", ios->vdd);
	if ((1 << ios->vdd) & MMC_VDD_165_195)
		seq_printf(s, "(1.65 - 1.95 V)\n");
	else if (ios->vdd < (ARRAY_SIZE(vdd_str) - 1)
			&& vdd_str[ios->vdd] && vdd_str[ios->vdd + 1])
		seq_printf(s, "(%s ~ %s V)\n", vdd_str[ios->vdd],
				vdd_str[ios->vdd + 1]);
	else
		seq_printf(s, "(invalid)\n");

	switch (ios->bus_mode) {
	case MMC_BUSMODE_OPENDRAIN:
		str = "open drain";
		break;
	case MMC_BUSMODE_PUSHPULL:
		str = "push-pull";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "bus mode:\t%u (%s)\n", ios->bus_mode, str);

	switch (ios->chip_select) {
	case MMC_CS_DONTCARE:
		str = "don't care";
		break;
	case MMC_CS_HIGH:
		str = "active high";
		break;
	case MMC_CS_LOW:
		str = "active low";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "chip select:\t%u (%s)\n", ios->chip_select, str);

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		str = "off";
		break;
	case MMC_POWER_UP:
		str = "up";
		break;
	case MMC_POWER_ON:
		str = "on";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "power mode:\t%u (%s)\n", ios->power_mode, str);
	seq_printf(s, "bus width:\t%u (%u bits)\n",
			ios->bus_width, 1 << ios->bus_width);

	switch (ios->timing) {
	case MMC_TIMING_LEGACY:
		str = "legacy";
		break;
	case MMC_TIMING_MMC_HS:
		str = "mmc high-speed";
		break;
	case MMC_TIMING_SD_HS:
		str = "sd high-speed";
		break;
	case MMC_TIMING_UHS_SDR12:
		str = "sd uhs SDR12";
		break;
	case MMC_TIMING_UHS_SDR25:
		str = "sd uhs SDR25";
		break;
	case MMC_TIMING_UHS_SDR50:
		str = "sd uhs SDR50";
		break;
	case MMC_TIMING_UHS_SDR104:
		str = "sd uhs SDR104";
		break;
	case MMC_TIMING_UHS_DDR50:
		str = "sd uhs DDR50";
		break;
	case MMC_TIMING_MMC_DDR52:
		str = "mmc DDR52";
		break;
	case MMC_TIMING_MMC_HS200:
		str = "mmc HS200";
		break;
	case MMC_TIMING_MMC_HS400:
		str = mmc_card_hs400es(host->card) ?
			"mmc HS400 enhanced strobe" : "mmc HS400";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "timing spec:\t%u (%s)\n", ios->timing, str);

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		str = "3.30 V";
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		str = "1.80 V";
		break;
	case MMC_SIGNAL_VOLTAGE_120:
		str = "1.20 V";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "signal voltage:\t%u (%s)\n", ios->signal_voltage, str);

	switch (ios->drv_type) {
	case MMC_SET_DRIVER_TYPE_A:
		str = "driver type A";
		break;
	case MMC_SET_DRIVER_TYPE_B:
		str = "driver type B";
		break;
	case MMC_SET_DRIVER_TYPE_C:
		str = "driver type C";
		break;
	case MMC_SET_DRIVER_TYPE_D:
		str = "driver type D";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "driver type:\t%u (%s)\n", ios->drv_type, str);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mmc_ios);

static int mmc_clock_opt_get(void *data, u64 *val)
{
	struct mmc_host *host = data;

	*val = host->ios.clock;

	return 0;
}

static int mmc_clock_opt_set(void *data, u64 val)
{
	struct mmc_host *host = data;

	/* We need this check due to input value is u64 */
	if (val != 0 && (val > host->f_max || val < host->f_min))
		return -EINVAL;

	mmc_claim_host(host);
	mmc_set_clock(host, (unsigned int) val);
	mmc_release_host(host);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(mmc_clock_fops, mmc_clock_opt_get, mmc_clock_opt_set,
	"%llu\n");

static int mmc_err_state_get(void *data, u64 *val)
{
	struct mmc_host *host = data;
	int i;

	if (!host)
		return -EINVAL;

	*val = 0;
	for (i = 0; i < MMC_ERR_MAX; i++) {
		if (host->err_stats[i]) {
			*val = 1;
			break;
		}
	}

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(mmc_err_state, mmc_err_state_get, NULL, "%llu\n");

static int mmc_err_stats_show(struct seq_file *file, void *data)
{
	struct mmc_host *host = file->private;
	const char *desc[MMC_ERR_MAX] = {
		[MMC_ERR_CMD_TIMEOUT] = "Command Timeout Occurred",
		[MMC_ERR_CMD_CRC] = "Command CRC Errors Occurred",
		[MMC_ERR_DAT_TIMEOUT] = "Data Timeout Occurred",
		[MMC_ERR_DAT_CRC] = "Data CRC Errors Occurred",
		[MMC_ERR_AUTO_CMD] = "Auto-Cmd Error Occurred",
		[MMC_ERR_ADMA] = "ADMA Error Occurred",
		[MMC_ERR_TUNING] = "Tuning Error Occurred",
		[MMC_ERR_CMDQ_RED] = "CMDQ RED Errors",
		[MMC_ERR_CMDQ_GCE] = "CMDQ GCE Errors",
		[MMC_ERR_CMDQ_ICCE] = "CMDQ ICCE Errors",
		[MMC_ERR_REQ_TIMEOUT] = "Request Timedout",
		[MMC_ERR_CMDQ_REQ_TIMEOUT] = "CMDQ Request Timedout",
		[MMC_ERR_ICE_CFG] = "ICE Config Errors",
		[MMC_ERR_CTRL_TIMEOUT] = "Controller Timedout errors",
		[MMC_ERR_UNEXPECTED_IRQ] = "Unexpected IRQ errors",
	};
	int i;

	for (i = 0; i < MMC_ERR_MAX; i++) {
		if (desc[i])
			seq_printf(file, "# %s:\t %d\n",
					desc[i], host->err_stats[i]);
	}

	return 0;
}

static int mmc_err_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmc_err_stats_show, inode->i_private);
}

static ssize_t mmc_err_stats_write(struct file *filp, const char __user *ubuf,
				   size_t cnt, loff_t *ppos)
{
	struct mmc_host *host = filp->f_mapping->host->i_private;

	pr_debug("%s: Resetting MMC error statistics\n", __func__);
	memset(host->err_stats, 0, sizeof(host->err_stats));

	return cnt;
}

static const struct file_operations mmc_err_stats_fops = {
	.open	= mmc_err_stats_open,
	.read	= seq_read,
	.write	= mmc_err_stats_write,
	.release = single_release,
};

static int mmc_caps_get(void *data, u64 *val)
{
	*val = *(u32 *)data;
	return 0;
}

static int mmc_caps_set(void *data, u64 val)
{
	u32 *caps = data;
	u32 diff = *caps ^ val;
	u32 allowed = MMC_CAP_AGGRESSIVE_PM |
		      MMC_CAP_SD_HIGHSPEED |
		      MMC_CAP_MMC_HIGHSPEED |
		      MMC_CAP_UHS |
		      MMC_CAP_DDR;

	if (diff & ~allowed)
		return -EINVAL;

	*caps = val;

	return 0;
}

static int mmc_caps2_set(void *data, u64 val)
{
	u32 allowed = MMC_CAP2_HSX00_1_8V | MMC_CAP2_HSX00_1_2V;
	u32 *caps = data;
	u32 diff = *caps ^ val;

	if (diff & ~allowed)
		return -EINVAL;

	*caps = val;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(mmc_caps_fops, mmc_caps_get, mmc_caps_set,
			 "0x%08llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(mmc_caps2_fops, mmc_caps_get, mmc_caps2_set,
			 "0x%08llx\n");

void mmc_add_host_debugfs(struct mmc_host *host)
{
	struct dentry *root;

	root = debugfs_create_dir(mmc_hostname(host), NULL);
	host->debugfs_root = root;

	debugfs_create_file("ios", S_IRUSR, root, host, &mmc_ios_fops);
	debugfs_create_file("caps", 0600, root, &host->caps, &mmc_caps_fops);
	debugfs_create_file("caps2", 0600, root, &host->caps2,
			    &mmc_caps2_fops);
	debugfs_create_file_unsafe("clock", S_IRUSR | S_IWUSR, root, host,
				   &mmc_clock_fops);

	debugfs_create_file_unsafe("err_state", 0600, root, host,
			    &mmc_err_state);
	debugfs_create_file("err_stats", 0600, root, host,
			    &mmc_err_stats_fops);

#ifdef CONFIG_FAIL_MMC_REQUEST
	if (fail_request)
		setup_fault_attr(&fail_default_attr, fail_request);
	host->fail_mmc_request = fail_default_attr;
	fault_create_debugfs_attr("fail_mmc_request", root,
				  &host->fail_mmc_request);
#endif
}

void mmc_remove_host_debugfs(struct mmc_host *host)
{
	debugfs_remove_recursive(host->debugfs_root);
}

void mmc_add_card_debugfs(struct mmc_card *card)
{
	struct mmc_host	*host = card->host;
	struct dentry	*root;

	if (!host->debugfs_root)
		return;

	root = debugfs_create_dir(mmc_card_id(card), host->debugfs_root);
	card->debugfs_root = root;

	debugfs_create_x32("state", S_IRUSR, root, &card->state);
}

void mmc_remove_card_debugfs(struct mmc_card *card)
{
	debugfs_remove_recursive(card->debugfs_root);
	card->debugfs_root = NULL;
}
