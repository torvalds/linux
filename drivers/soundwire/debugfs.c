// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2019 Intel Corporation.

#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/firmware.h>
#include <linux/mod_devicetable.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/string_choices.h>
#include "bus.h"

static struct dentry *sdw_debugfs_root;

void sdw_bus_debugfs_init(struct sdw_bus *bus)
{
	char name[16];

	if (!sdw_debugfs_root)
		return;

	/* create the debugfs master-N */
	snprintf(name, sizeof(name), "master-%d-%d", bus->controller_id, bus->link_id);
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
	ssize_t ret;
	int i, j;

	char *buf __free(kfree) = kzalloc(RD_BUF, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = pm_runtime_get_sync(&slave->dev);
	if (ret < 0 && ret != -EACCES) {
		pm_runtime_put_noidle(&slave->dev);
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
	for (i = SDW_SCP_INT1; i <= SDW_SCP_BUS_CLOCK_BASE; i++)
		ret += sdw_sprintf(slave, buf, ret, i);
	for (i = SDW_SCP_DEVID_0; i <= SDW_SCP_DEVID_5; i++)
		ret += sdw_sprintf(slave, buf, ret, i);
	for (i = SDW_SCP_FRAMECTRL_B0; i <= SDW_SCP_BUSCLOCK_SCALE_B0; i++)
		ret += sdw_sprintf(slave, buf, ret, i);
	for (i = SDW_SCP_FRAMECTRL_B1; i <= SDW_SCP_BUSCLOCK_SCALE_B1; i++)
		ret += sdw_sprintf(slave, buf, ret, i);
	for (i = SDW_SCP_PHY_OUT_CTRL_0; i <= SDW_SCP_PHY_OUT_CTRL_7; i++)
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

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(sdw_slave_reg);

#define MAX_CMD_BYTES (1024 * 1024)

static int cmd;
static int cmd_type;
static u32 start_addr;
static size_t num_bytes;
static u8 read_buffer[MAX_CMD_BYTES];
static char *firmware_file;

static int set_command(void *data, u64 value)
{
	struct sdw_slave *slave = data;

	if (value > 1)
		return -EINVAL;

	/* Userspace changed the hardware state behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	dev_dbg(&slave->dev, "command: %s\n", str_read_write(value));
	cmd = value;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(set_command_fops, NULL,
			 set_command, "%llu\n");

static int set_command_type(void *data, u64 value)
{
	struct sdw_slave *slave = data;

	if (value > 1)
		return -EINVAL;

	/* Userspace changed the hardware state behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	dev_dbg(&slave->dev, "command type: %s\n", value ? "BRA" : "Column0");

	cmd_type = (int)value;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(set_command_type_fops, NULL,
			 set_command_type, "%llu\n");

static int set_start_address(void *data, u64 value)
{
	struct sdw_slave *slave = data;

	/* Userspace changed the hardware state behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	dev_dbg(&slave->dev, "start address %#llx\n", value);

	start_addr = value;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(set_start_address_fops, NULL,
			 set_start_address, "%llu\n");

static int set_num_bytes(void *data, u64 value)
{
	struct sdw_slave *slave = data;

	if (value == 0 || value > MAX_CMD_BYTES)
		return -EINVAL;

	/* Userspace changed the hardware state behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	dev_dbg(&slave->dev, "number of bytes %lld\n", value);

	num_bytes = value;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(set_num_bytes_fops, NULL,
			 set_num_bytes, "%llu\n");

static int do_bpt_sequence(struct sdw_slave *slave, bool write, u8 *buffer)
{
	struct sdw_bpt_msg msg = {0};

	msg.addr = start_addr;
	msg.len = num_bytes;
	msg.dev_num = slave->dev_num;
	if (write)
		msg.flags = SDW_MSG_FLAG_WRITE;
	else
		msg.flags = SDW_MSG_FLAG_READ;
	msg.buf = buffer;

	return sdw_bpt_send_sync(slave->bus, slave, &msg);
}

static int cmd_go(void *data, u64 value)
{
	const struct firmware *fw = NULL;
	struct sdw_slave *slave = data;
	ktime_t start_t;
	ktime_t finish_t;
	int ret;

	if (value != 1)
		return -EINVAL;

	/* one last check */
	if (start_addr > SDW_REG_MAX ||
	    num_bytes == 0 || num_bytes > MAX_CMD_BYTES)
		return -EINVAL;

	ret = pm_runtime_get_sync(&slave->dev);
	if (ret < 0 && ret != -EACCES) {
		pm_runtime_put_noidle(&slave->dev);
		return ret;
	}

	if (cmd == 0) {
		ret = request_firmware(&fw, firmware_file, &slave->dev);
		if (ret < 0) {
			dev_err(&slave->dev, "firmware %s not found\n", firmware_file);
			goto out;
		}
		if (fw->size < num_bytes) {
			dev_err(&slave->dev,
				"firmware %s: firmware size %zd, desired %zd\n",
				firmware_file, fw->size, num_bytes);
			goto out;
		}
	}

	/* Userspace changed the hardware state behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	dev_dbg(&slave->dev, "starting command\n");
	start_t = ktime_get();

	if (cmd == 0) {
		if (cmd_type)
			ret = do_bpt_sequence(slave, true, (u8 *)fw->data);
		else
			ret = sdw_nwrite_no_pm(slave, start_addr, num_bytes, fw->data);
	} else {
		memset(read_buffer, 0, sizeof(read_buffer));

		if (cmd_type)
			ret = do_bpt_sequence(slave, false, read_buffer);
		else
			ret = sdw_nread_no_pm(slave, start_addr, num_bytes, read_buffer);
	}

	finish_t = ktime_get();

	dev_dbg(&slave->dev, "command completed, num_byte %zu status %d, time %lld ms\n",
		num_bytes, ret, div_u64(finish_t - start_t, NSEC_PER_MSEC));

out:
	if (fw)
		release_firmware(fw);

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put(&slave->dev);

	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(cmd_go_fops, NULL,
			 cmd_go, "%llu\n");

#define MAX_LINE_LEN 128

static int read_buffer_show(struct seq_file *s_file, void *data)
{
	char buf[MAX_LINE_LEN];
	int i;

	if (num_bytes == 0 || num_bytes > MAX_CMD_BYTES)
		return -EINVAL;

	for (i = 0; i < num_bytes; i++) {
		scnprintf(buf, MAX_LINE_LEN, "address %#x val 0x%02x\n",
			  start_addr + i, read_buffer[i]);
		seq_printf(s_file, "%s", buf);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(read_buffer);

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

	/* interface to send arbitrary commands */
	debugfs_create_file("command", 0200, d, slave, &set_command_fops);
	debugfs_create_file("command_type", 0200, d, slave, &set_command_type_fops);
	debugfs_create_file("start_address", 0200, d, slave, &set_start_address_fops);
	debugfs_create_file("num_bytes", 0200, d, slave, &set_num_bytes_fops);
	debugfs_create_file("go", 0200, d, slave, &cmd_go_fops);

	debugfs_create_file("read_buffer", 0400, d, slave, &read_buffer_fops);
	firmware_file = NULL;
	debugfs_create_str("firmware_file", 0200, d, &firmware_file);

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
