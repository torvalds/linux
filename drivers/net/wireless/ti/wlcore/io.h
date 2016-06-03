/*
 * This file is part of wl1271
 *
 * Copyright (C) 1998-2009 Texas Instruments. All rights reserved.
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __IO_H__
#define __IO_H__

#include <linux/irqreturn.h>

#define HW_ACCESS_MEMORY_MAX_RANGE	0x1FFC0

#define HW_PARTITION_REGISTERS_ADDR     0x1FFC0
#define HW_PART0_SIZE_ADDR              (HW_PARTITION_REGISTERS_ADDR)
#define HW_PART0_START_ADDR             (HW_PARTITION_REGISTERS_ADDR + 4)
#define HW_PART1_SIZE_ADDR              (HW_PARTITION_REGISTERS_ADDR + 8)
#define HW_PART1_START_ADDR             (HW_PARTITION_REGISTERS_ADDR + 12)
#define HW_PART2_SIZE_ADDR              (HW_PARTITION_REGISTERS_ADDR + 16)
#define HW_PART2_START_ADDR             (HW_PARTITION_REGISTERS_ADDR + 20)
#define HW_PART3_SIZE_ADDR              (HW_PARTITION_REGISTERS_ADDR + 24)
#define HW_PART3_START_ADDR             (HW_PARTITION_REGISTERS_ADDR + 28)

#define HW_ACCESS_REGISTER_SIZE         4

#define HW_ACCESS_PRAM_MAX_RANGE	0x3c000

struct wl1271;

void wlcore_disable_interrupts(struct wl1271 *wl);
void wlcore_disable_interrupts_nosync(struct wl1271 *wl);
void wlcore_enable_interrupts(struct wl1271 *wl);
void wlcore_synchronize_interrupts(struct wl1271 *wl);

void wl1271_io_reset(struct wl1271 *wl);
void wl1271_io_init(struct wl1271 *wl);
int wlcore_translate_addr(struct wl1271 *wl, int addr);

/* Raw target IO, address is not translated */
static inline int __must_check wlcore_raw_write(struct wl1271 *wl, int addr,
						void *buf, size_t len,
						bool fixed)
{
	int ret;

	if (test_bit(WL1271_FLAG_IO_FAILED, &wl->flags) ||
	    WARN_ON((test_bit(WL1271_FLAG_IN_ELP, &wl->flags) &&
		     addr != HW_ACCESS_ELP_CTRL_REG)))
		return -EIO;

	ret = wl->if_ops->write(wl->dev, addr, buf, len, fixed);
	if (ret && wl->state != WLCORE_STATE_OFF)
		set_bit(WL1271_FLAG_IO_FAILED, &wl->flags);

	return ret;
}

static inline int __must_check wlcore_raw_read(struct wl1271 *wl, int addr,
					       void *buf, size_t len,
					       bool fixed)
{
	int ret;

	if (test_bit(WL1271_FLAG_IO_FAILED, &wl->flags) ||
	    WARN_ON((test_bit(WL1271_FLAG_IN_ELP, &wl->flags) &&
		     addr != HW_ACCESS_ELP_CTRL_REG)))
		return -EIO;

	ret = wl->if_ops->read(wl->dev, addr, buf, len, fixed);
	if (ret && wl->state != WLCORE_STATE_OFF)
		set_bit(WL1271_FLAG_IO_FAILED, &wl->flags);

	return ret;
}

static inline int __must_check wlcore_raw_read_data(struct wl1271 *wl, int reg,
						    void *buf, size_t len,
						    bool fixed)
{
	return wlcore_raw_read(wl, wl->rtable[reg], buf, len, fixed);
}

static inline int __must_check wlcore_raw_write_data(struct wl1271 *wl, int reg,
						     void *buf, size_t len,
						     bool fixed)
{
	return wlcore_raw_write(wl, wl->rtable[reg], buf, len, fixed);
}

static inline int __must_check wlcore_raw_read32(struct wl1271 *wl, int addr,
						 u32 *val)
{
	int ret;

	ret = wlcore_raw_read(wl, addr, wl->buffer_32,
			      sizeof(*wl->buffer_32), false);
	if (ret < 0)
		return ret;

	if (val)
		*val = le32_to_cpu(*wl->buffer_32);

	return 0;
}

static inline int __must_check wlcore_raw_write32(struct wl1271 *wl, int addr,
						  u32 val)
{
	*wl->buffer_32 = cpu_to_le32(val);
	return wlcore_raw_write(wl, addr, wl->buffer_32,
				sizeof(*wl->buffer_32), false);
}

static inline int __must_check wlcore_read(struct wl1271 *wl, int addr,
					   void *buf, size_t len, bool fixed)
{
	int physical;

	physical = wlcore_translate_addr(wl, addr);

	return wlcore_raw_read(wl, physical, buf, len, fixed);
}

static inline int __must_check wlcore_write(struct wl1271 *wl, int addr,
					    void *buf, size_t len, bool fixed)
{
	int physical;

	physical = wlcore_translate_addr(wl, addr);

	return wlcore_raw_write(wl, physical, buf, len, fixed);
}

static inline int __must_check wlcore_write_data(struct wl1271 *wl, int reg,
						 void *buf, size_t len,
						 bool fixed)
{
	return wlcore_write(wl, wl->rtable[reg], buf, len, fixed);
}

static inline int __must_check wlcore_read_data(struct wl1271 *wl, int reg,
						void *buf, size_t len,
						bool fixed)
{
	return wlcore_read(wl, wl->rtable[reg], buf, len, fixed);
}

static inline int __must_check wlcore_read_hwaddr(struct wl1271 *wl, int hwaddr,
						  void *buf, size_t len,
						  bool fixed)
{
	int physical;
	int addr;

	/* Convert from FW internal address which is chip arch dependent */
	addr = wl->ops->convert_hwaddr(wl, hwaddr);

	physical = wlcore_translate_addr(wl, addr);

	return wlcore_raw_read(wl, physical, buf, len, fixed);
}

static inline int __must_check wlcore_read32(struct wl1271 *wl, int addr,
					     u32 *val)
{
	return wlcore_raw_read32(wl, wlcore_translate_addr(wl, addr), val);
}

static inline int __must_check wlcore_write32(struct wl1271 *wl, int addr,
					      u32 val)
{
	return wlcore_raw_write32(wl, wlcore_translate_addr(wl, addr), val);
}

static inline int __must_check wlcore_read_reg(struct wl1271 *wl, int reg,
					       u32 *val)
{
	return wlcore_raw_read32(wl,
				 wlcore_translate_addr(wl, wl->rtable[reg]),
				 val);
}

static inline int __must_check wlcore_write_reg(struct wl1271 *wl, int reg,
						u32 val)
{
	return wlcore_raw_write32(wl,
				  wlcore_translate_addr(wl, wl->rtable[reg]),
				  val);
}

static inline void wl1271_power_off(struct wl1271 *wl)
{
	int ret = 0;

	if (!test_bit(WL1271_FLAG_GPIO_POWER, &wl->flags))
		return;

	if (wl->if_ops->power)
		ret = wl->if_ops->power(wl->dev, false);
	if (!ret)
		clear_bit(WL1271_FLAG_GPIO_POWER, &wl->flags);
}

static inline int wl1271_power_on(struct wl1271 *wl)
{
	int ret = 0;

	if (wl->if_ops->power)
		ret = wl->if_ops->power(wl->dev, true);
	if (ret == 0)
		set_bit(WL1271_FLAG_GPIO_POWER, &wl->flags);

	return ret;
}

int wlcore_set_partition(struct wl1271 *wl,
			 const struct wlcore_partition_set *p);

bool wl1271_set_block_size(struct wl1271 *wl);

/* Functions from wl1271_main.c */

int wl1271_tx_dummy_packet(struct wl1271 *wl);

#endif
