/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#ifndef _hive_isp_css_custom_host_hrt_h_
#define _hive_isp_css_custom_host_hrt_h_

#include <linux/delay.h>
#include "atomisp_helper.h"

/*
 * _hrt_master_port_store/load/uload -macros using __force attributed
 * cast to intentional dereferencing __iomem attributed (noderef)
 * pointer from atomisp_get_io_virt_addr
 */
#define _hrt_master_port_store_8(a, d) \
	(*((s8 __force *)atomisp_get_io_virt_addr(a)) = (d))

#define _hrt_master_port_store_16(a, d) \
	(*((s16 __force *)atomisp_get_io_virt_addr(a)) = (d))

#define _hrt_master_port_store_32(a, d) \
	(*((s32 __force *)atomisp_get_io_virt_addr(a)) = (d))

#define _hrt_master_port_load_8(a) \
	(*(s8 __force *)atomisp_get_io_virt_addr(a))

#define _hrt_master_port_load_16(a) \
	(*(s16 __force *)atomisp_get_io_virt_addr(a))

#define _hrt_master_port_load_32(a) \
	(*(s32 __force *)atomisp_get_io_virt_addr(a))

#define _hrt_master_port_uload_8(a) \
	(*(u8 __force *)atomisp_get_io_virt_addr(a))

#define _hrt_master_port_uload_16(a) \
	(*(u16 __force *)atomisp_get_io_virt_addr(a))

#define _hrt_master_port_uload_32(a) \
	(*(u32 __force *)atomisp_get_io_virt_addr(a))

#define _hrt_master_port_store_8_volatile(a, d)  _hrt_master_port_store_8(a, d)
#define _hrt_master_port_store_16_volatile(a, d) _hrt_master_port_store_16(a, d)
#define _hrt_master_port_store_32_volatile(a, d) _hrt_master_port_store_32(a, d)

#define _hrt_master_port_load_8_volatile(a)      _hrt_master_port_load_8(a)
#define _hrt_master_port_load_16_volatile(a)     _hrt_master_port_load_16(a)
#define _hrt_master_port_load_32_volatile(a)     _hrt_master_port_load_32(a)

#define _hrt_master_port_uload_8_volatile(a)     _hrt_master_port_uload_8(a)
#define _hrt_master_port_uload_16_volatile(a)    _hrt_master_port_uload_16(a)
#define _hrt_master_port_uload_32_volatile(a)    _hrt_master_port_uload_32(a)

static inline void hrt_sleep(void)
{
	udelay(1);
}

static inline uint32_t _hrt_mem_store(uint32_t to, const void *from, size_t n)
{
	unsigned i;
	uint32_t _to = to;
	const char *_from = (const char *)from;
	for (i = 0; i < n; i++, _to++, _from++)
		_hrt_master_port_store_8(_to, *_from);
	return _to;
}

static inline void *_hrt_mem_load(uint32_t from, void *to, size_t n)
{
	unsigned i;
	char *_to = (char *)to;
	uint32_t _from = from;
	for (i = 0; i < n; i++, _to++, _from++)
		*_to = _hrt_master_port_load_8(_from);
	return _to;
}

static inline uint32_t _hrt_mem_set(uint32_t to, int c, size_t n)
{
	unsigned i;
	uint32_t _to = to;
	for (i = 0; i < n; i++, _to++)
		_hrt_master_port_store_8(_to, c);
	return _to;
}

#endif /* _hive_isp_css_custom_host_hrt_h_ */
