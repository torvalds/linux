/*
 * i2c-amd756-s4882.c - i2c-amd756 extras for the Tyan S4882 motherboard
 *
 * Copyright (C) 2004, 2008 Jean Delvare <khali@linux-fr.org>
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
 * module EEPROM) located on channels 1-4, and 0x4c (LM63)
 * located on multiplexed channels 0 and 5-7. We define one
 * virtual adapter per CPU, which corresponds to two multiplexed
 * channels:
 *   CPU0: virtual adapter 1, channels 1 and 0
 *   CPU1: virtual adapter 2, channels 2 and 5
 *   CPU2: virtual adapter 3, channels 3 and 6
 *   CPU3: virtual adapter 4, channels 4 and 7
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>

extern struct i2c_adapter amd756_smbus;

static struct i2c_adapter *s4882_adapter;
static struct i2c_algorithm *s4882_algo;

/* Wrapper access functions for multiplexed SMBus */
static DEFINE_MUTEX(amd756_lock);

static s32 amd756_access_virt0(struct i2c_adapter * adap, u16 addr,
			       unsigned short flags, char read_write,
			       u8 command, int size,
			       union i2c_smbus_data * data)
{
	int error;

	/* We exclude the multiplexed addresses */
	if (addr == 0x4c || (addr & 0xfc) == 0x50 || (addr & 0xfc) == 0x30
	 || addr == 0x18)
		return -1;

	mutex_lock(&amd756_lock);

	error = amd756_smbus.algo->smbus_xfer(adap, addr, flags, read_write,
					      command, size, data);

	mutex_unlock(&amd756_lock);

	return error;
}

/* We remember the last used channels combination so as to only switch
   channels when it is really needed. This greatly reduces the SMBus
   overhead, but also assumes that nobody will be writing to the PCA9556
   in our back. */
static u8 last_channels;

static inline s32 amd756_access_channel(struct i2c_adapter * adap, u16 addr,
					unsigned short flags, char read_write,
					u8 command, int size,
					union i2c_smbus_data * data,
					u8 channels)
{
	int error;

	/* We exclude the non-multiplexed addresses */
	if (addr != 0x4c && (addr & 0xfc) != 0x50 && (addr & 0xfc) != 0x30)
		return -1;

	mutex_lock(&amd756_lock);

	if (last_channels != channels) {
		union i2c_smbus_data mplxdata;
		mplxdata.byte = channels;

		error = amd756_smbus.algo->smbus_xfer(adap, 0x18, 0,
						      I2C_SMBUS_WRITE, 0x01,
						      I2C_SMBUS_BYTE_DATA,
						      &mplxdata);
		if (error)
			goto UNLOCK;
		last_channels = channels;
	}
	error = amd756_smbus.algo->smbus_xfer(adap, addr, flags, read_write,
					      command, size, data);

UNLOCK:
	mutex_unlock(&amd756_lock);
	return error;
}

static s32 amd756_access_virt1(struct i2c_adapter * adap, u16 addr,
			       unsigned short flags, char read_write,
			       u8 command, int size,
			       union i2c_smbus_data * data)
{
	/* CPU0: channels 1 and 0 enabled */
	return amd756_access_channel(adap, addr, flags, read_write, command,
				     size, data, 0x03);
}

static s32 amd756_access_virt2(struct i2c_adapter * adap, u16 addr,
			       unsigned short flags, char read_write,
			       u8 command, int size,
			       union i2c_smbus_data * data)
{
	/* CPU1: channels 2 and 5 enabled */
	return amd756_access_channel(adap, addr, flags, read_write, command,
				     size, data, 0x24);
}

static s32 amd756_access_virt3(struct i2c_adapter * adap, u16 addr,
			       unsigned short flags, char read_write,
			       u8 command, int size,
			       union i2c_smbus_data * data)
{
	/* CPU2: channels 3 and 6 enabled */
	return amd756_access_channel(adap, addr, flags, read_write, command,
				     size, data, 0x48);
}

static s32 amd756_access_virt4(struct i2c_adapter * adap, u16 addr,
			       unsigned short flags, char read_write,
			       u8 command, int size,
			       union i2c_smbus_data * data)
{
	/* CPU3: channels 4 and 7 enabled */
	return amd756_access_channel(adap, addr, flags, read_write, command,
				     size, data, 0x90);
}

static int __init amd756_s4882_init(void)
{
	int i, error;
	union i2c_smbus_data ioconfig;

	/* Unregister physical bus */
	error = i2c_del_adapter(&amd756_smbus);
	if (error) {
		if (error == -EINVAL)
			error = -ENODEV;
		else
			dev_err(&amd756_smbus.dev, "Physical bus removal "
				"failed\n");
		goto ERROR0;
	}

	printk(KERN_INFO "Enabling SMBus multiplexing for Tyan S4882\n");
	/* Define the 5 virtual adapters and algorithms structures */
	if (!(s4882_adapter = kzalloc(5 * sizeof(struct i2c_adapter),
				      GFP_KERNEL))) {
		error = -ENOMEM;
		goto ERROR1;
	}
	if (!(s4882_algo = kzalloc(5 * sizeof(struct i2c_algorithm),
				   GFP_KERNEL))) {
		error = -ENOMEM;
		goto ERROR2;
	}

	/* Fill in the new structures */
	s4882_algo[0] = *(amd756_smbus.algo);
	s4882_algo[0].smbus_xfer = amd756_access_virt0;
	s4882_adapter[0] = amd756_smbus;
	s4882_adapter[0].algo = s4882_algo;
	s4882_adapter[0].dev.parent = amd756_smbus.dev.parent;
	for (i = 1; i < 5; i++) {
		s4882_algo[i] = *(amd756_smbus.algo);
		s4882_adapter[i] = amd756_smbus;
		sprintf(s4882_adapter[i].name,
			"SMBus 8111 adapter (CPU%d)", i-1);
		s4882_adapter[i].algo = s4882_algo+i;
		s4882_adapter[i].dev.parent = amd756_smbus.dev.parent;
	}
	s4882_algo[1].smbus_xfer = amd756_access_virt1;
	s4882_algo[2].smbus_xfer = amd756_access_virt2;
	s4882_algo[3].smbus_xfer = amd756_access_virt3;
	s4882_algo[4].smbus_xfer = amd756_access_virt4;

	/* Configure the PCA9556 multiplexer */
	ioconfig.byte = 0x00; /* All I/O to output mode */
	error = amd756_smbus.algo->smbus_xfer(&amd756_smbus, 0x18, 0,
					      I2C_SMBUS_WRITE, 0x03,
					      I2C_SMBUS_BYTE_DATA, &ioconfig);
	if (error) {
		dev_err(&amd756_smbus.dev, "PCA9556 configuration failed\n");
		error = -EIO;
		goto ERROR3;
	}

	/* Register virtual adapters */
	for (i = 0; i < 5; i++) {
		error = i2c_add_adapter(s4882_adapter+i);
		if (error) {
			dev_err(&amd756_smbus.dev,
			       "Virtual adapter %d registration "
			       "failed, module not inserted\n", i);
			for (i--; i >= 0; i--)
				i2c_del_adapter(s4882_adapter+i);
			goto ERROR3;
		}
	}

	return 0;

ERROR3:
	kfree(s4882_algo);
	s4882_algo = NULL;
ERROR2:
	kfree(s4882_adapter);
	s4882_adapter = NULL;
ERROR1:
	/* Restore physical bus */
	i2c_add_adapter(&amd756_smbus);
ERROR0:
	return error;
}

static void __exit amd756_s4882_exit(void)
{
	if (s4882_adapter) {
		int i;

		for (i = 0; i < 5; i++)
			i2c_del_adapter(s4882_adapter+i);
		kfree(s4882_adapter);
		s4882_adapter = NULL;
	}
	kfree(s4882_algo);
	s4882_algo = NULL;

	/* Restore physical bus */
	if (i2c_add_adapter(&amd756_smbus))
		dev_err(&amd756_smbus.dev, "Physical bus restoration "
			"failed\n");
}

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("S4882 SMBus multiplexing");
MODULE_LICENSE("GPL");

module_init(amd756_s4882_init);
module_exit(amd756_s4882_exit);
