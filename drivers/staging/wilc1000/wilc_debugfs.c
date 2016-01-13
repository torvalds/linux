/*
 * NewportMedia WiFi chipset driver test tools - wilc-debug
 * Copyright (c) 2012 NewportMedia Inc.
 * Author: SSW <sswd@wilcsemic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#if defined(WILC_DEBUGFS)
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/poll.h>
#include <linux/sched.h>

#include "wilc_wlan_if.h"


static struct dentry *wilc_dir;

/*
 * --------------------------------------------------------------------------------
 */

#define DBG_REGION_ALL	(GENERIC_DBG | HOSTAPD_DBG | HOSTINF_DBG | CORECONFIG_DBG | CFG80211_DBG | INT_DBG | TX_DBG | RX_DBG | LOCK_DBG | INIT_DBG | BUS_DBG | MEM_DBG)
#define DBG_LEVEL_ALL	(DEBUG | INFO | WRN | ERR)
atomic_t WILC_REGION = ATOMIC_INIT(INIT_DBG | GENERIC_DBG | CFG80211_DBG | FIRM_DBG | HOSTAPD_DBG);
EXPORT_SYMBOL_GPL(WILC_REGION);
atomic_t WILC_DEBUG_LEVEL = ATOMIC_INIT(ERR);
EXPORT_SYMBOL_GPL(WILC_DEBUG_LEVEL);

/*
 * --------------------------------------------------------------------------------
 */


static ssize_t wilc_debug_level_read(struct file *file, char __user *userbuf, size_t count, loff_t *ppos)
{
	char buf[128];
	int res = 0;

	/* only allow read from start */
	if (*ppos > 0)
		return 0;

	res = scnprintf(buf, sizeof(buf), "Debug Level: %x\n", atomic_read(&WILC_DEBUG_LEVEL));

	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}

static ssize_t wilc_debug_level_write(struct file *filp, const char __user *buf,
					size_t count, loff_t *ppos)
{
	int flag = 0;
	int ret;

	ret = kstrtouint_from_user(buf, count, 16, &flag);
	if (ret)
		return ret;

	if (flag > DBG_LEVEL_ALL) {
		printk("%s, value (0x%08x) is out of range, stay previous flag (0x%08x)\n", __func__, flag, atomic_read(&WILC_DEBUG_LEVEL));
		return -EINVAL;
	}

	atomic_set(&WILC_DEBUG_LEVEL, (int)flag);

	if (flag == 0)
		printk("Debug-level disabled\n");
	else
		printk("Debug-level enabled\n");

	return count;
}

static ssize_t wilc_debug_region_read(struct file *file, char __user *userbuf, size_t count, loff_t *ppos)
{
	char buf[128];
	int res = 0;

	/* only allow read from start */
	if (*ppos > 0)
		return 0;

	res = scnprintf(buf, sizeof(buf), "Debug region: %x\n", atomic_read(&WILC_REGION));

	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}

static ssize_t wilc_debug_region_write(struct file *filp, const char *buf, size_t count, loff_t *ppos)
{
	char buffer[128] = {};
	int flag;

	if (count > sizeof(buffer))
		return -EINVAL;

	if (copy_from_user(buffer, buf, count)) {
		return -EFAULT;
	}

	flag = buffer[0] - '0';

	if (flag > DBG_REGION_ALL) {
		printk("%s, value (0x%08x) is out of range, stay previous flag (0x%08x)\n", __func__, flag, atomic_read(&WILC_REGION));
		return -EFAULT;
	}

	atomic_set(&WILC_REGION, (int)flag);
	printk("new debug-region is %x\n", atomic_read(&WILC_REGION));

	return count;
}

/*
 * --------------------------------------------------------------------------------
 */

#define FOPS(_open, _read, _write, _poll) { \
		.owner	= THIS_MODULE, \
		.open	= (_open), \
		.read	= (_read), \
		.write	= (_write), \
		.poll		= (_poll), \
}

struct wilc_debugfs_info_t {
	const char *name;
	int perm;
	unsigned int data;
	struct file_operations fops;
};

static struct wilc_debugfs_info_t debugfs_info[] = {
	{ "wilc_debug_level",	0666,	(DEBUG | ERR), FOPS(NULL, wilc_debug_level_read, wilc_debug_level_write, NULL), },
	{ "wilc_debug_region",	0666,	(INIT_DBG | GENERIC_DBG | CFG80211_DBG), FOPS(NULL, wilc_debug_region_read, wilc_debug_region_write, NULL), },
};

static int __init wilc_debugfs_init(void)
{
	int i;

	struct dentry *debugfs_files;
	struct wilc_debugfs_info_t *info;

	wilc_dir = debugfs_create_dir("wilc_wifi", NULL);
	if (wilc_dir ==  ERR_PTR(-ENODEV)) {
		/* it's not error. the debugfs is just not being enabled. */
		printk("ERR, kernel has built without debugfs support\n");
		return 0;
	}

	if (!wilc_dir) {
		printk("ERR, debugfs create dir\n");
		return -1;
	}

	for (i = 0; i < ARRAY_SIZE(debugfs_info); i++) {
		info = &debugfs_info[i];
		debugfs_files = debugfs_create_file(info->name,
						    info->perm,
						    wilc_dir,
						    &info->data,
						    &info->fops);

		if (!debugfs_files) {
			printk("ERR fail to create the debugfs file, %s\n", info->name);
			debugfs_remove_recursive(wilc_dir);
			return -1;
		}
	}
	return 0;
}
module_init(wilc_debugfs_init);

static void __exit wilc_debugfs_remove(void)
{
	debugfs_remove_recursive(wilc_dir);
}
module_exit(wilc_debugfs_remove);

#endif

