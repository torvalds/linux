/*
 *	w1_therm.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the therms of the GNU General Public License as published by
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <asm/types.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>

#include "../w1.h"
#include "../w1_int.h"
#include "../w1_family.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <johnpol@2ka.mipt.ru>");
MODULE_DESCRIPTION("Driver for 1-wire Dallas network protocol, temperature family.");

static u8 bad_roms[][9] = {
				{0xaa, 0x00, 0x4b, 0x46, 0xff, 0xff, 0x0c, 0x10, 0x87},
				{}
			};

static ssize_t w1_therm_read_bin(struct kobject *, char *, loff_t, size_t);

static struct bin_attribute w1_therm_bin_attr = {
	.attr = {
		.name = "w1_slave",
		.mode = S_IRUGO,
		.owner = THIS_MODULE,
	},
	.size = W1_SLAVE_DATA_SIZE,
	.read = w1_therm_read_bin,
};

static int w1_therm_add_slave(struct w1_slave *sl)
{
	return sysfs_create_bin_file(&sl->dev.kobj, &w1_therm_bin_attr);
}

static void w1_therm_remove_slave(struct w1_slave *sl)
{
	sysfs_remove_bin_file(&sl->dev.kobj, &w1_therm_bin_attr);
}

static struct w1_family_ops w1_therm_fops = {
	.add_slave	= w1_therm_add_slave,
	.remove_slave	= w1_therm_remove_slave,
};

static struct w1_family w1_therm_family_DS18S20 = {
	.fid = W1_THERM_DS18S20,
	.fops = &w1_therm_fops,
};

static struct w1_family w1_therm_family_DS18B20 = {
	.fid = W1_THERM_DS18B20,
	.fops = &w1_therm_fops,
};

static struct w1_family w1_therm_family_DS1822 = {
	.fid = W1_THERM_DS1822,
	.fops = &w1_therm_fops,
};

struct w1_therm_family_converter
{
	u8			broken;
	u16			reserved;
	struct w1_family	*f;
	int			(*convert)(u8 rom[9]);
};

static inline int w1_DS18B20_convert_temp(u8 rom[9]);
static inline int w1_DS18S20_convert_temp(u8 rom[9]);

static struct w1_therm_family_converter w1_therm_families[] = {
	{
		.f		= &w1_therm_family_DS18S20,
		.convert 	= w1_DS18S20_convert_temp
	},
	{
		.f		= &w1_therm_family_DS1822,
		.convert 	= w1_DS18B20_convert_temp
	},
	{
		.f		= &w1_therm_family_DS18B20,
		.convert 	= w1_DS18B20_convert_temp
	},
};

static inline int w1_DS18B20_convert_temp(u8 rom[9])
{
	int t = (rom[1] << 8) | rom[0];
	t /= 16;
	return t;
}

static inline int w1_DS18S20_convert_temp(u8 rom[9])
{
	int t, h;

	if (!rom[7])
		return 0;

	if (rom[1] == 0)
		t = ((s32)rom[0] >> 1)*1000;
	else
		t = 1000*(-1*(s32)(0x100-rom[0]) >> 1);

	t -= 250;
	h = 1000*((s32)rom[7] - (s32)rom[6]);
	h /= (s32)rom[7];
	t += h;

	return t;
}

static inline int w1_convert_temp(u8 rom[9], u8 fid)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(w1_therm_families); ++i)
		if (w1_therm_families[i].f->fid == fid)
			return w1_therm_families[i].convert(rom);

	return 0;
}

static int w1_therm_check_rom(u8 rom[9])
{
	int i;

	for (i=0; i<sizeof(bad_roms)/9; ++i)
		if (!memcmp(bad_roms[i], rom, 9))
			return 1;

	return 0;
}

static ssize_t w1_therm_read_bin(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	struct w1_master *dev = sl->master;
	u8 rom[9], crc, verdict;
	int i, max_trying = 10;

	mutex_lock(&sl->master->mutex);

	if (off > W1_SLAVE_DATA_SIZE) {
		count = 0;
		goto out;
	}
	if (off + count > W1_SLAVE_DATA_SIZE) {
		count = 0;
		goto out;
	}

	memset(buf, 0, count);
	memset(rom, 0, sizeof(rom));

	count = 0;
	verdict = 0;
	crc = 0;

	while (max_trying--) {
		if (!w1_reset_select_slave(sl)) {
			int count = 0;
			unsigned int tm = 750;

			w1_write_8(dev, W1_CONVERT_TEMP);

			msleep(tm);

			if (!w1_reset_select_slave(sl)) {

				w1_write_8(dev, W1_READ_SCRATCHPAD);
				if ((count = w1_read_block(dev, rom, 9)) != 9) {
					dev_warn(&dev->dev, "w1_read_block() returned %d instead of 9.\n", count);
				}

				crc = w1_calc_crc8(rom, 8);

				if (rom[8] == crc && rom[0])
					verdict = 1;
			}
		}

		if (!w1_therm_check_rom(rom))
			break;
	}

	for (i = 0; i < 9; ++i)
		count += sprintf(buf + count, "%02x ", rom[i]);
	count += sprintf(buf + count, ": crc=%02x %s\n",
			   crc, (verdict) ? "YES" : "NO");
	if (verdict)
		memcpy(sl->rom, rom, sizeof(sl->rom));
	else
		dev_warn(&dev->dev, "18S20 doesn't respond to CONVERT_TEMP.\n");

	for (i = 0; i < 9; ++i)
		count += sprintf(buf + count, "%02x ", sl->rom[i]);

	count += sprintf(buf + count, "t=%d\n", w1_convert_temp(rom, sl->family->fid));
out:
	mutex_unlock(&dev->mutex);

	return count;
}

static int __init w1_therm_init(void)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(w1_therm_families); ++i) {
		err = w1_register_family(w1_therm_families[i].f);
		if (err)
			w1_therm_families[i].broken = 1;
	}

	return 0;
}

static void __exit w1_therm_fini(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(w1_therm_families); ++i)
		if (!w1_therm_families[i].broken)
			w1_unregister_family(w1_therm_families[i].f);
}

module_init(w1_therm_init);
module_exit(w1_therm_fini);
