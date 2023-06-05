// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2019 Intel Corporation.

#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/mod_devicetable.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include "bus.h"

static struct dentry *sdw_debugfs_root;

void sdw_bus_debugfs_init(struct sdw_bus *bus)
{
	char name[16];

	if (!sdw_debugfs_root)
		return;

	/* create the debugfs master-N */
	snprintf(name, sizeof(name), "master-%d-%d", bus->id, bus->link_id);
	bus->debugfs = debugfs_create_dir(name, sdw_debugfs_root);
}

void sdw_bus_debugfs_exit(struct sdw_bus *bus)
{
	debugfs_remove_recursive(bus->debugfs);
}

#define RD_BUF (3 * PAGE_SIZE)

static ssize_t sdw_sprintf(struct sdw_slave *slave,
			   char *buf, size_t pos, unsigned int reg)
{
	int value;

	value = sdw_read_no_pm(slave, reg);

	if (value < 0)
		return scnprintf(buf + pos, RD_BUF - pos, "%3x\tXX\n", reg);
	else
		return scnprintf(buf + pos, RD_BUF - pos,
				"%3x\t%2x\n", reg, value);
}

static int sdw_slave_reg_show(struct seq_file *s_file, void *data)
{
	struct sdw_slave *slave = s_file->private;
	char *buf;
	ssize_t ret;
	int i, j;

	buf = kzalloc(RD_BUF, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = pm_runtime_resume_and_get(&slave->dev);
	if (ret < 0 && ret != -EACCES) {
		kfree(buf);
		return ret;
	}

	ret = scnprintf(buf, RD_BUF, "Register  Value\n");

	/* DP0 non-banked registers */
	ret += scnprintf(buf + ret, RD_BUF - ret, "\nDP0\n");
	for (i = SDW_DP0_INT; i <= SDW_DP0_PREPARECTRL; i++)
		ret += sdw_sprintf(slave, buf, ret, i);

	/* DP0 Bank 0 registers */
	ret += scnprintf(buf + ret, RD_BUF - ret, "Bank0\n");
	ret += sdw_sprintf(slave, buf, ret, SDW_DP0_CHANNELEN);
	for (i = SDW_DP0_SAMPLECTRL1; i <= SDW_DP0_LANECTRL; i++)
		ret += sdw_sprintf(slave, buf, ret, i);

	/* DP0 Bank 1 registers */
	ret += scnprintf(buf + ret, RD_BUF - ret, "Bank1\n");
	ret += sdw_sprintf(slave, buf, ret,
			SDW_DP0_CHANNELEN + SDW_BANK1_OFFSET);
	for (i = SDW_DP0_SAMPLECTRL1 + SDW_BANK1_OFFSET;
			i <= SDW_DP0_LANECTRL + SDW_BANK1_OFFSET; i++)
		ret += sdw_sprintf(slave, buf, ret, i);

	/* SCP registers */
	ret += scnprintf(buf + ret, RD_BUF - ret, "\nSCP\n");
	for (i = SDW_SCP_INT1; i <= SDW_SCP_BANKDELAY; i++)
		ret += sdw_sprintf(slave, buf, ret, i);
	for (i = SDW_SCP_DEVID_0; i <= SDW_SCP_DEVID_5; i++)
		ret += sdw_sprintf(slave, buf, ret, i);

	/*
	 * SCP Bank 0/1 registers are read-only and cannot be
	 * retrieved from the Slave. The Master typically keeps track
	 * of the current frame size so the information can be found
	 * in other places
	 */

	/* DP1..14 registers */
	for (i = 1; SDW_VALID_PORT_RANGE(i); i++) {

		/* DPi registers */
		ret += scnprintf(buf + ret, RD_BUF - ret, "\nDP%d\n", i);
		for (j = SDW_DPN_INT(i); j <= SDW_DPN_PREPARECTRL(i); j++)
			ret += sdw_sprintf(slave, buf, ret, j);

		/* DPi Bank0 registers */
		ret += scnprintf(buf + ret, RD_BUF - ret, "Bank0\n");
		for (j = SDW_DPN_CHANNELEN_B0(i);
		     j <= SDW_DPN_LANECTRL_B0(i); j++)
			ret += sdw_sprintf(slave, buf, ret, j);

		/* DPi Bank1 registers */
		ret += scnprintf(buf + ret, RD_BUF - ret, "Bank1\n");
		for (j = SDW_DPN_CHANNELEN_B1(i);
		     j <= SDW_DPN_LANECTRL_B1(i); j++)
			ret += sdw_sprintf(slave, buf, ret, j);
	}

	seq_printf(s_file, "%s", buf);

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put(&slave->dev);

	kfree(buf);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(sdw_slave_reg);

void sdw_slave_debugfs_init(struct sdw_slave *slave)
{
	struct dentry *master;
	struct dentry *d;
	char name[32];

	master = slave->bus->debugfs;

	/* create the debugfs slave-name */
	snprintf(name, sizeof(name), "%s", dev_name(&slave->dev));
	d = debugfs_create_dir(name, master);

	debugfs_create_file("registers", 0400, d, slave, &sdw_slave_reg_fops);

	slave->debugfs = d;
}

void sdw_slave_debugfs_exit(struct sdw_slave *slave)
{
	debugfs_remove_recursive(slave->debugfs);
}

void sdw_debugfs_init(void)
{
	sdw_debugfs_root = debugfs_create_dir("soundwire", NULL);
}

void sdw_debugfs_exit(void)
{
	debugfs_remove_recursive(sdw_debugfs_root);
}
