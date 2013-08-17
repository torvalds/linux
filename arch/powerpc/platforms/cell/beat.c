/*
 * Simple routines for Celleb/Beat
 *
 * (C) Copyright 2006-2007 TOSHIBA CORPORATION
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/export.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/rtc.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/reboot.h>

#include <asm/hvconsole.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/firmware.h>

#include "beat_wrapper.h"
#include "beat.h"
#include "beat_interrupt.h"

static int beat_pm_poweroff_flag;

void beat_restart(char *cmd)
{
	beat_shutdown_logical_partition(!beat_pm_poweroff_flag);
}

void beat_power_off(void)
{
	beat_shutdown_logical_partition(0);
}

u64 beat_halt_code = 0x1000000000000000UL;
EXPORT_SYMBOL(beat_halt_code);

void beat_halt(void)
{
	beat_shutdown_logical_partition(beat_halt_code);
}

int beat_set_rtc_time(struct rtc_time *rtc_time)
{
	u64 tim;
	tim = mktime(rtc_time->tm_year+1900,
		     rtc_time->tm_mon+1, rtc_time->tm_mday,
		     rtc_time->tm_hour, rtc_time->tm_min, rtc_time->tm_sec);
	if (beat_rtc_write(tim))
		return -1;
	return 0;
}

void beat_get_rtc_time(struct rtc_time *rtc_time)
{
	u64 tim;

	if (beat_rtc_read(&tim))
		tim = 0;
	to_tm(tim, rtc_time);
	rtc_time->tm_year -= 1900;
	rtc_time->tm_mon -= 1;
}

#define	BEAT_NVRAM_SIZE	4096

ssize_t beat_nvram_read(char *buf, size_t count, loff_t *index)
{
	unsigned int i;
	unsigned long len;
	char *p = buf;

	if (*index >= BEAT_NVRAM_SIZE)
		return -ENODEV;
	i = *index;
	if (i + count > BEAT_NVRAM_SIZE)
		count = BEAT_NVRAM_SIZE - i;

	for (; count != 0; count -= len) {
		len = count;
		if (len > BEAT_NVRW_CNT)
			len = BEAT_NVRW_CNT;
		if (beat_eeprom_read(i, len, p))
			return -EIO;

		p += len;
		i += len;
	}
	*index = i;
	return p - buf;
}

ssize_t beat_nvram_write(char *buf, size_t count, loff_t *index)
{
	unsigned int i;
	unsigned long len;
	char *p = buf;

	if (*index >= BEAT_NVRAM_SIZE)
		return -ENODEV;
	i = *index;
	if (i + count > BEAT_NVRAM_SIZE)
		count = BEAT_NVRAM_SIZE - i;

	for (; count != 0; count -= len) {
		len = count;
		if (len > BEAT_NVRW_CNT)
			len = BEAT_NVRW_CNT;
		if (beat_eeprom_write(i, len, p))
			return -EIO;

		p += len;
		i += len;
	}
	*index = i;
	return p - buf;
}

ssize_t beat_nvram_get_size(void)
{
	return BEAT_NVRAM_SIZE;
}

int beat_set_xdabr(unsigned long dabr)
{
	if (beat_set_dabr(dabr, DABRX_KERNEL | DABRX_USER))
		return -1;
	return 0;
}

int64_t beat_get_term_char(u64 vterm, u64 *len, u64 *t1, u64 *t2)
{
	u64 db[2];
	s64 ret;

	ret = beat_get_characters_from_console(vterm, len, (u8 *)db);
	if (ret == 0) {
		*t1 = db[0];
		*t2 = db[1];
	}
	return ret;
}
EXPORT_SYMBOL(beat_get_term_char);

int64_t beat_put_term_char(u64 vterm, u64 len, u64 t1, u64 t2)
{
	u64 db[2];

	db[0] = t1;
	db[1] = t2;
	return beat_put_characters_to_console(vterm, len, (u8 *)db);
}
EXPORT_SYMBOL(beat_put_term_char);

void beat_power_save(void)
{
	beat_pause(0);
}

#ifdef CONFIG_KEXEC
void beat_kexec_cpu_down(int crash, int secondary)
{
	beatic_deinit_IRQ();
}
#endif

static irqreturn_t beat_power_event(int virq, void *arg)
{
	printk(KERN_DEBUG "Beat: power button pressed\n");
	beat_pm_poweroff_flag = 1;
	ctrl_alt_del();
	return IRQ_HANDLED;
}

static irqreturn_t beat_reset_event(int virq, void *arg)
{
	printk(KERN_DEBUG "Beat: reset button pressed\n");
	beat_pm_poweroff_flag = 0;
	ctrl_alt_del();
	return IRQ_HANDLED;
}

static struct beat_event_list {
	const char *typecode;
	irq_handler_t handler;
	unsigned int virq;
} beat_event_list[] = {
	{ "power", beat_power_event, 0 },
	{ "reset", beat_reset_event, 0 },
};

static int __init beat_register_event(void)
{
	u64 path[4], data[2];
	int rc, i;
	unsigned int virq;

	for (i = 0; i < ARRAY_SIZE(beat_event_list); i++) {
		struct beat_event_list *ev = &beat_event_list[i];

		if (beat_construct_event_receive_port(data) != 0) {
			printk(KERN_ERR "Beat: "
			       "cannot construct event receive port for %s\n",
			       ev->typecode);
			return -EINVAL;
		}

		virq = irq_create_mapping(NULL, data[0]);
		if (virq == NO_IRQ) {
			printk(KERN_ERR "Beat: failed to get virtual IRQ"
			       " for event receive port for %s\n",
			       ev->typecode);
			beat_destruct_event_receive_port(data[0]);
			return -EIO;
		}
		ev->virq = virq;

		rc = request_irq(virq, ev->handler, 0,
				      ev->typecode, NULL);
		if (rc != 0) {
			printk(KERN_ERR "Beat: failed to request virtual IRQ"
			       " for event receive port for %s\n",
			       ev->typecode);
			beat_destruct_event_receive_port(data[0]);
			return rc;
		}

		path[0] = 0x1000000065780000ul;	/* 1,ex */
		path[1] = 0x627574746f6e0000ul;	/* button */
		path[2] = 0;
		strncpy((char *)&path[2], ev->typecode, 8);
		path[3] = 0;
		data[1] = 0;

		beat_create_repository_node(path, data);
	}
	return 0;
}

static int __init beat_event_init(void)
{
	if (!firmware_has_feature(FW_FEATURE_BEAT))
		return -EINVAL;

	beat_pm_poweroff_flag = 0;
	return beat_register_event();
}

device_initcall(beat_event_init);
