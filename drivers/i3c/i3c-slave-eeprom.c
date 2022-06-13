// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Aspeed Technology Inc.
 */

#include <linux/i3c/master.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>

struct eeprom_data {
	struct bin_attribute bin;
	struct work_struct prep_rdata;
	spinlock_t buffer_lock;
	u16 buffer_idx;
	u16 address_mask;
	u8 num_address_bytes;

	struct i3c_master_controller *i3c_controller;
	u8 buffer[];
};

static void i3c_slave_eeprom_prep_rdata(struct work_struct *work)
{
	struct eeprom_data *eeprom = container_of(work, struct eeprom_data, prep_rdata);
	struct i3c_slave_payload read_data, notify;
	u32 mdb = IBI_MDB_ID(0b101, 0x1f);

	notify.len = 1;
	notify.data = &mdb;

	read_data.len = eeprom->address_mask - eeprom->buffer_idx + 1;
	read_data.data = &eeprom->buffer[eeprom->buffer_idx];
	i3c_master_put_read_data(eeprom->i3c_controller, &read_data, &notify);
}

static void i3c_slave_eeprom_callback(struct i3c_master_controller *master,
				      const struct i3c_slave_payload *payload)
{
	struct eeprom_data *eeprom = dev_get_drvdata(&master->dev);
	int wr_len;
	u8 *buf = (u8 *)payload->data;

	if (!payload->len)
		return;

	if (eeprom->num_address_bytes == 2)
		eeprom->buffer_idx = ((u16)buf[0] << 8) | buf[1];
	else
		eeprom->buffer_idx = (u16)buf[0];

	wr_len = payload->len - eeprom->num_address_bytes;

	pr_debug("len = %d, index=%d, wr_len=%d\n", payload->len,
		 eeprom->buffer_idx, wr_len);

	if (wr_len > 0) {
		if (eeprom->buffer_idx + wr_len > eeprom->address_mask) {
			u16 len = eeprom->address_mask - eeprom->buffer_idx + 1;

			memcpy(&eeprom->buffer[eeprom->buffer_idx],
			       &buf[eeprom->num_address_bytes], len);
			memcpy(&eeprom->buffer[0],
			       &buf[eeprom->num_address_bytes + len],
			       wr_len - len);
		} else {
			memcpy(&eeprom->buffer[eeprom->buffer_idx],
			       &buf[eeprom->num_address_bytes], wr_len);
		}

		eeprom->buffer_idx += wr_len;
		eeprom->buffer_idx &= eeprom->address_mask;
	}

	/* prepare the read data outside of interrupt context */
	schedule_work(&eeprom->prep_rdata);
}

static ssize_t i3c_slave_eeprom_bin_read(struct file *filp,
					 struct kobject *kobj,
					 struct bin_attribute *attr, char *buf,
					 loff_t off, size_t count)
{
	struct eeprom_data *eeprom;
	unsigned long flags;

	eeprom = dev_get_drvdata(container_of(kobj, struct device, kobj));

	spin_lock_irqsave(&eeprom->buffer_lock, flags);
	memcpy(buf, &eeprom->buffer[off], count);
	spin_unlock_irqrestore(&eeprom->buffer_lock, flags);

	return count;
}

static ssize_t i3c_slave_eeprom_bin_write(struct file *filp,
					  struct kobject *kobj,
					  struct bin_attribute *attr, char *buf,
					  loff_t off, size_t count)
{
	struct eeprom_data *eeprom;
	unsigned long flags;

	eeprom = dev_get_drvdata(container_of(kobj, struct device, kobj));

	spin_lock_irqsave(&eeprom->buffer_lock, flags);
	memcpy(&eeprom->buffer[off], buf, count);
	spin_unlock_irqrestore(&eeprom->buffer_lock, flags);

	return count;
}

int i3c_slave_eeprom_probe(struct i3c_master_controller *master)
{
	struct eeprom_data *eeprom;
	int ret;
	struct i3c_slave_setup req = {};
	struct device *dev = &master->dev;

	/* fixed parameters for testing: size 64 bytes, address size is 1 byte */
	unsigned int size = 64;
	unsigned int flag_addr16 = 0;

	eeprom = devm_kzalloc(dev, sizeof(struct eeprom_data) + size, GFP_KERNEL);
	if (!eeprom)
		return -ENOMEM;

	eeprom->num_address_bytes = flag_addr16 ? 2 : 1;
	eeprom->address_mask = size - 1;
	spin_lock_init(&eeprom->buffer_lock);
	dev_set_drvdata(dev, eeprom);

	memset(eeprom->buffer, 0xff, size);

	sysfs_bin_attr_init(&eeprom->bin);
	eeprom->bin.attr.name = "slave-eeprom";
	eeprom->bin.attr.mode = 0600;
	eeprom->bin.read = i3c_slave_eeprom_bin_read;
	eeprom->bin.write = i3c_slave_eeprom_bin_write;
	eeprom->bin.size = size;

	eeprom->i3c_controller = master;

	ret = sysfs_create_bin_file(&dev->kobj, &eeprom->bin);
	if (ret)
		return ret;

	INIT_WORK(&eeprom->prep_rdata, i3c_slave_eeprom_prep_rdata);

	req.handler = i3c_slave_eeprom_callback;
	req.max_payload_len = size;
	req.num_slots = 1;

	ret = i3c_master_register_slave(master, &req);
	if (ret) {
		sysfs_remove_bin_file(&dev->kobj, &eeprom->bin);
		return ret;
	}

	return 0;
}

int i3c_slave_eeprom_remove(struct i3c_master_controller *master)
{
	struct device *dev = &master->dev;
	struct eeprom_data *eeprom = dev_get_drvdata(dev);

	i3c_master_unregister_slave(master);
	sysfs_remove_bin_file(&dev->kobj, &eeprom->bin);

	return 0;
}
