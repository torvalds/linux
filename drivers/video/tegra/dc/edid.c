/*
 * drivers/video/tegra/dc/edid.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define DEBUG

#include <linux/debugfs.h>
#include <linux/fb.h>
#include <linux/i2c.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>

#include "edid.h"

struct tegra_edid {
	struct i2c_client	*client;
	struct i2c_board_info	info;
	int			bus;

	u8			*data;
	unsigned		len;
};

#if defined(DEBUG) || defined(CONFIG_DEBUG_FS)
static int tegra_edid_show(struct seq_file *s, void *unused)
{
	struct tegra_edid *edid = s->private;
	int i;

	for (i = 0; i < edid->len; i++) {
		if (i % 16 == 0)
			seq_printf(s, "edid[%03x] =", i);

		seq_printf(s, " %02x", edid->data[i]);

		if (i % 16 == 15)
			seq_printf(s, "\n");
	}

	return 0;
}
#endif

#ifdef CONFIG_DEBUG_FS
static int tegra_edid_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, tegra_edid_show, inode->i_private);
}

static const struct file_operations tegra_edid_debug_fops = {
	.open		= tegra_edid_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void tegra_edid_debug_add(struct tegra_edid *edid)
{
	char name[] = "edidX";

	snprintf(name, sizeof(name), "edid%1d", edid->bus);
	debugfs_create_file(name, S_IRUGO, NULL, edid, &tegra_edid_debug_fops);
}
#else
void tegra_edid_debug_add(struct tegra_edid *edid)
{
}
#endif

#ifdef DEBUG
static char tegra_edid_dump_buff[16 * 1024];

static void tegra_edid_dump(struct tegra_edid *edid)
{
	struct seq_file s;
	int i;
	char c;

	memset(&s, 0x0, sizeof(s));

	s.buf = tegra_edid_dump_buff;
	s.size = sizeof(tegra_edid_dump_buff);
	s.private = edid;

	tegra_edid_show(&s, NULL);

	i = 0;
	while (i < s.count ) {
		if ((s.count - i) > 256) {
			c = s.buf[i + 256];
			s.buf[i + 256] = 0;
			printk("%s", s.buf + i);
			s.buf[i + 256] = c;
		} else {
			printk("%s", s.buf + i);
		}
		i += 256;
	}
}
#else
static void tegra_edid_dump(struct tegra_edid *edid)
{
}
#endif

int tegra_edid_read_block(struct tegra_edid *edid, int block, u8 *data)
{
	u8 block_buf[] = {block >> 1};
	u8 cmd_buf[] = {(block & 0x1) * 128};
	int status;
	struct i2c_msg msg[] = {
		{
			.addr = 0x30,
			.flags = 0,
			.len = 1,
			.buf = block_buf,
		},
		{
			.addr = 0x50,
			.flags = 0,
			.len = 1,
			.buf = cmd_buf,
		},
		{
			.addr = 0x50,
			.flags = I2C_M_RD,
			.len = 128,
			.buf = data,
		}};
	struct i2c_msg *m;
	int msg_len;

	if (block > 1) {
		msg_len = 3;
		m = msg;
	} else {
		msg_len = 2;
		m = &msg[1];
	}

	status = i2c_transfer(edid->client->adapter, m, msg_len);

	if (status < 0)
		return status;

	if (status != msg_len)
		return -EIO;

	return 0;
}


int tegra_edid_get_monspecs(struct tegra_edid *edid, struct fb_monspecs *specs)
{
	int i;
	int ret;
	int extension_blocks;

	ret = tegra_edid_read_block(edid, 0, edid->data);

	memset(specs, 0x0, sizeof(struct fb_monspecs));
	fb_edid_to_monspecs(edid->data, specs);
	if (specs->modedb == NULL)
		return -EINVAL;

	extension_blocks = edid->data[0x7e];

	for (i = 1; i <= extension_blocks; i++) {
		ret = tegra_edid_read_block(edid, i, edid->data + i * 128);
		if (ret < 0)
			break;

		if (edid->data[i * 128] == 0x2)
			fb_edid_add_monspecs(edid->data + i * 128, specs);
	}

	edid->len = i * 128;

	tegra_edid_dump(edid);

	return 0;
}

struct tegra_edid *tegra_edid_create(int bus)
{
	struct tegra_edid *edid;
	struct i2c_adapter *adapter;
	int err;

	edid = kzalloc(sizeof(struct tegra_edid), GFP_KERNEL);
	if (!edid)
		return ERR_PTR(-ENOMEM);

	edid->data = vmalloc(SZ_32K);
	if (!edid->data) {
		err = -ENOMEM;
		goto free_edid;
	}
	strlcpy(edid->info.type, "tegra_edid", sizeof(edid->info.type));
	edid->bus = bus;
	edid->info.addr = 0x50;
	edid->info.platform_data = edid;

	adapter = i2c_get_adapter(bus);
	if (!adapter) {
		pr_err("can't get adpater for bus %d\n", bus);
		err = -EBUSY;
		goto free_edid;
	}

	edid->client = i2c_new_device(adapter, &edid->info);
	i2c_put_adapter(adapter);

	if (!edid->client) {
		pr_err("can't create new device\n");
		err = -EBUSY;
		goto free_edid;
	}

	tegra_edid_debug_add(edid);

	return edid;

free_edid:
	vfree(edid->data);
	kfree(edid);

	return ERR_PTR(err);
}

void tegra_edid_destroy(struct tegra_edid *edid)
{
	i2c_release_client(edid->client);
	vfree(edid->data);
	kfree(edid);
}

static const struct i2c_device_id tegra_edid_id[] = {
        { "tegra_edid", 0 },
        { }
};

MODULE_DEVICE_TABLE(i2c, tegra_edid_id);

static struct i2c_driver tegra_edid_driver = {
        .id_table = tegra_edid_id,
        .driver = {
                .name = "tegra_edid",
        },
};

static int __init tegra_edid_init(void)
{
        return i2c_add_driver(&tegra_edid_driver);
}

static void __exit tegra_edid_exit(void)
{
        i2c_del_driver(&tegra_edid_driver);
}

module_init(tegra_edid_init);
module_exit(tegra_edid_exit);
