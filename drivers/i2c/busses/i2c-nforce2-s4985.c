/*
 * i2c-nforce2-s4985.c - i2c-nforce2 extras for the Tyan S4985 motherboard
 *
 * Copyright (C) 2008 Jean Delvare <khali@linux-fr.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * We select the channels by sending commands to the Philips
 * PCA9556 chip at I2C address 0x18. The main adapter is used for
 * the non-multiplexed part of the bus, and 4 virtual adapters
 * are defined for the multiplexed addresses: 0x50-0x53 (memory
 * module EEPROM) located on channels 1-4. We define one virtual
 * adapter per CPU, which corresponds to one multiplexed channel:
 *   CPU0: virtual adapter 1, channel 1
 *   CPU1: virtual adapter 2, channel 2
 *   CPU2: virtual adapter 3, channel 3
 *   CPU3: virtual adapter 4, channel 4
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>

extern struct i2c_adapter *nforce2_smbus;

static struct i2c_adapter *s4985_adapter;
static struct i2c_algorithm *s4985_algo;

/* Wrapper access functions for multiplexed SMBus */
static DEFINE_MUTEX(nforce2_lock);

static s32 nforce2_access_virt0(struct i2c_adapter *adap, u16 addr,
				unsigned short flags, char read_write,
				u8 command, int size,
				union i2c_smbus_data *data)
{
	int error;

	/* We exclude the multiplexed addresses */
	if ((addr & 0xfc) == 0x50 || (addr & 0xfc) == 0x30
	 || addr == 0x18)
		return -ENXIO;

	mutex_lock(&nforce2_lock);
	error = nforce2_smbus->algo->smbus_xfer(adap, addr, flags, read_write,
						command, size, data);
	mutex_unlock(&nforce2_lock);

	return error;
}

/* We remember the last used channels combination so as to only switch
   channels when it is really needed. This greatly reduces the SMBus
   overhead, but also assumes that nobody will be writing to the PCA9556
   in our back. */
static u8 last_channels;

static inline s32 nforce2_access_channel(struct i2c_adapter *adap, u16 addr,
					 unsigned short flags, char read_write,
					 u8 command, int size,
					 union i2c_smbus_data *data,
					 u8 channels)
{
	int error;

	/* We exclude the non-multiplexed addresses */
	if ((addr & 0xfc) != 0x50 && (addr & 0xfc) != 0x30)
		return -ENXIO;

	mutex_lock(&nforce2_lock);
	if (last_channels != channels) {
		union i2c_smbus_data mplxdata;
		mplxdata.byte = channels;

		error = nforce2_smbus->algo->smbus_xfer(adap, 0x18, 0,
							I2C_SMBUS_WRITE, 0x01,
							I2C_SMBUS_BYTE_DATA,
							&mplxdata);
		if (error)
			goto UNLOCK;
		last_channels = channels;
	}
	error = nforce2_smbus->algo->smbus_xfer(adap, addr, flags, read_write,
						command, size, data);

UNLOCK:
	mutex_unlock(&nforce2_lock);
	return error;
}

static s32 nforce2_access_virt1(struct i2c_adapter *adap, u16 addr,
				unsigned short flags, char read_write,
				u8 command, int size,
				union i2c_smbus_data *data)
{
	/* CPU0: channel 1 enabled */
	return nforce2_access_channel(adap, addr, flags, read_write, command,
				      size, data, 0x02);
}

static s32 nforce2_access_virt2(struct i2c_adapter *adap, u16 addr,
				unsigned short flags, char read_write,
				u8 command, int size,
				union i2c_smbus_data *data)
{
	/* CPU1: channel 2 enabled */
	return nforce2_access_channel(adap, addr, flags, read_write, command,
				      size, data, 0x04);
}

static s32 nforce2_access_virt3(struct i2c_adapter *adap, u16 addr,
				unsigned short flags, char read_write,
				u8 command, int size,
				union i2c_smbus_data *data)
{
	/* CPU2: channel 3 enabled */
	return nforce2_access_channel(adap, addr, flags, read_write, command,
				      size, data, 0x08);
}

static s32 nforce2_access_virt4(struct i2c_adapter *adap, u16 addr,
				unsigned short flags, char read_write,
				u8 command, int size,
				union i2c_smbus_data *data)
{
	/* CPU3: channel 4 enabled */
	return nforce2_access_channel(adap, addr, flags, read_write, command,
				      size, data, 0x10);
}

static int __init nforce2_s4985_init(void)
{
	int i, error;
	union i2c_smbus_data ioconfig;

	if (!nforce2_smbus)
		return -ENODEV;

	/* Configure the PCA9556 multiplexer */
	ioconfig.byte = 0x00; /* All I/O to output mode */
	error = i2c_smbus_xfer(nforce2_smbus, 0x18, 0, I2C_SMBUS_WRITE, 0x03,
			       I2C_SMBUS_BYTE_DATA, &ioconfig);
	if (error) {
		dev_err(&nforce2_smbus->dev, "PCA9556 configuration failed\n");
		error = -EIO;
		goto ERROR0;
	}

	/* Unregister physical bus */
	error = i2c_del_adapter(nforce2_smbus);
	if (error) {
		dev_err(&nforce2_smbus->dev, "Physical bus removal failed\n");
		goto ERROR0;
	}

	printk(KERN_INFO "Enabling SMBus multiplexing for Tyan S4985\n");
	/* Define the 5 virtual adapters and algorithms structures */
	s4985_adapter = kzalloc(5 * sizeof(struct i2c_adapter), GFP_KERNEL);
	if (!s4985_adapter) {
		error = -ENOMEM;
		goto ERROR1;
	}
	s4985_algo = kzalloc(5 * sizeof(struct i2c_algorithm), GFP_KERNEL);
	if (!s4985_algo) {
		error = -ENOMEM;
		goto ERROR2;
	}

	/* Fill in the new structures */
	s4985_algo[0] = *(nforce2_smbus->algo);
	s4985_algo[0].smbus_xfer = nforce2_access_virt0;
	s4985_adapter[0] = *nforce2_smbus;
	s4985_adapter[0].algo = s4985_algo;
	s4985_adapter[0].dev.parent = nforce2_smbus->dev.parent;
	for (i = 1; i < 5; i++) {
		s4985_algo[i] = *(nforce2_smbus->algo);
		s4985_adapter[i] = *nforce2_smbus;
		snprintf(s4985_adapter[i].name, sizeof(s4985_adapter[i].name),
			 "SMBus nForce2 adapter (CPU%d)", i - 1);
		s4985_adapter[i].algo = s4985_algo + i;
		s4985_adapter[i].dev.parent = nforce2_smbus->dev.parent;
	}
	s4985_algo[1].smbus_xfer = nforce2_access_virt1;
	s4985_algo[2].smbus_xfer = nforce2_access_virt2;
	s4985_algo[3].smbus_xfer = nforce2_access_virt3;
	s4985_algo[4].smbus_xfer = nforce2_access_virt4;

	/* Register virtual adapters */
	for (i = 0; i < 5; i++) {
		error = i2c_add_adapter(s4985_adapter + i);
		if (error) {
			printk(KERN_ERR "i2c-nforce2-s4985: "
			       "Virtual adapter %d registration "
			       "failed, module not inserted\n", i);
			for (i--; i >= 0; i--)
				i2c_del_adapter(s4985_adapter + i);
			goto ERROR3;
		}
	}

	return 0;

ERROR3:
	kfree(s4985_algo);
	s4985_algo = NULL;
ERROR2:
	kfree(s4985_adapter);
	s4985_adapter = NULL;
ERROR1:
	/* Restore physical bus */
	i2c_add_adapter(nforce2_smbus);
ERROR0:
	return error;
}

static void __exit nforce2_s4985_exit(void)
{
	if (s4985_adapter) {
		int i;

		for (i = 0; i < 5; i++)
			i2c_del_adapter(s4985_adapter+i);
		kfree(s4985_adapter);
		s4985_adapter = NULL;
	}
	kfree(s4985_algo);
	s4985_algo = NULL;

	/* Restore physical bus */
	if (i2c_add_adapter(nforce2_smbus))
		printk(KERN_ERR "i2c-nforce2-s4985: "
		       "Physical bus restoration failed\n");
}

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("S4985 SMBus multiplexing");
MODULE_LICENSE("GPL");

module_init(nforce2_s4985_init);
module_exit(nforce2_s4985_exit);
