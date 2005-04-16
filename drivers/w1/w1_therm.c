/*
 * 	w1_therm.c
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
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>

#include "w1.h"
#include "w1_io.h"
#include "w1_int.h"
#include "w1_family.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <johnpol@2ka.mipt.ru>");
MODULE_DESCRIPTION("Driver for 1-wire Dallas network protocol, temperature family.");

static u8 bad_roms[][9] = {
				{0xaa, 0x00, 0x4b, 0x46, 0xff, 0xff, 0x0c, 0x10, 0x87}, 
				{}
			};

static ssize_t w1_therm_read_name(struct device *, char *);
static ssize_t w1_therm_read_temp(struct device *, char *);
static ssize_t w1_therm_read_bin(struct kobject *, char *, loff_t, size_t);

static struct w1_family_ops w1_therm_fops = {
	.rname = &w1_therm_read_name,
	.rbin = &w1_therm_read_bin,
	.rval = &w1_therm_read_temp,
	.rvalname = "temp1_input",
};

static ssize_t w1_therm_read_name(struct device *dev, char *buf)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	return sprintf(buf, "%s\n", sl->name);
}

static inline int w1_convert_temp(u8 rom[9])
{
	int t, h;
	
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

static ssize_t w1_therm_read_temp(struct device *dev, char *buf)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	return sprintf(buf, "%d\n", w1_convert_temp(sl->rom));
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
	struct w1_slave *sl = container_of(container_of(kobj, struct device, kobj),
			      			struct w1_slave, dev);
	struct w1_master *dev = sl->master;
	u8 rom[9], crc, verdict;
	int i, max_trying = 10;

	atomic_inc(&sl->refcnt);
	smp_mb__after_atomic_inc();
	if (down_interruptible(&sl->master->mutex)) {
		count = 0;
		goto out_dec;
	}

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
		if (!w1_reset_bus (dev)) {
			int count = 0;
			u8 match[9] = {W1_MATCH_ROM, };
			unsigned int tm = 750;

			memcpy(&match[1], (u64 *) & sl->reg_num, 8);
			
			w1_write_block(dev, match, 9);

			w1_write_8(dev, W1_CONVERT_TEMP);

			while (tm) {
				tm = msleep_interruptible(tm);
				if (signal_pending(current))
					flush_signals(current);
			}

			if (!w1_reset_bus (dev)) {
				w1_write_block(dev, match, 9);
				
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
	
	count += sprintf(buf + count, "t=%d\n", w1_convert_temp(rom));
out:
	up(&dev->mutex);
out_dec:
	smp_mb__before_atomic_inc();
	atomic_dec(&sl->refcnt);

	return count;
}

static struct w1_family w1_therm_family = {
	.fid = W1_FAMILY_THERM,
	.fops = &w1_therm_fops,
};

static int __init w1_therm_init(void)
{
	return w1_register_family(&w1_therm_family);
}

static void __exit w1_therm_fini(void)
{
	w1_unregister_family(&w1_therm_family);
}

module_init(w1_therm_init);
module_exit(w1_therm_fini);
