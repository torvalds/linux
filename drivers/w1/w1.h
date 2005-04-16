/*
 * 	w1.h
 *
 * Copyright (c) 2004 Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 * 
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __W1_H
#define __W1_H

struct w1_reg_num
{
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u64	family:8,
		id:48,
		crc:8;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u64	crc:8,
		id:48,
		family:8;
#else
#error "Please fix <asm/byteorder.h>"
#endif
};

#ifdef __KERNEL__

#include <linux/completion.h>
#include <linux/device.h>

#include <net/sock.h>

#include <asm/semaphore.h>

#include "w1_family.h"

#define W1_MAXNAMELEN		32
#define W1_SLAVE_DATA_SIZE	128

#define W1_SEARCH		0xF0
#define W1_CONDITIONAL_SEARCH	0xEC
#define W1_CONVERT_TEMP		0x44
#define W1_SKIP_ROM		0xCC
#define W1_READ_SCRATCHPAD	0xBE
#define W1_READ_ROM		0x33
#define W1_READ_PSUPPLY		0xB4
#define W1_MATCH_ROM		0x55

#define W1_SLAVE_ACTIVE		(1<<0)

struct w1_slave
{
	struct module		*owner;
	unsigned char		name[W1_MAXNAMELEN];
	struct list_head	w1_slave_entry;
	struct w1_reg_num	reg_num;
	atomic_t		refcnt;
	u8			rom[9];
	u32			flags;
	int			ttl;

	struct w1_master	*master;
	struct w1_family 	*family;
	struct device 		dev;
	struct completion 	dev_released;

	struct bin_attribute 	attr_bin;
	struct device_attribute	attr_name, attr_val;
};

typedef void (* w1_slave_found_callback)(unsigned long, u64);

struct w1_bus_master
{
	unsigned long		data;

	u8			(*read_bit)(unsigned long);
	void			(*write_bit)(unsigned long, u8);
  	
	u8			(*read_byte)(unsigned long);
  	void			(*write_byte)(unsigned long, u8);
  	
	u8			(*read_block)(unsigned long, u8 *, int);
	void			(*write_block)(unsigned long, u8 *, int);
	
  	u8			(*touch_bit)(unsigned long, u8);
  
  	u8			(*reset_bus)(unsigned long);

	void			(*search)(unsigned long, w1_slave_found_callback);
};

struct w1_master
{
	struct list_head	w1_master_entry;
	struct module		*owner;
	unsigned char		name[W1_MAXNAMELEN];
	struct list_head	slist;
	int			max_slave_count, slave_count;
	unsigned long		attempts;
	int			slave_ttl;
	int			initialized;
	u32			id;

	atomic_t		refcnt;

	void			*priv;
	int			priv_size;

	int			need_exit;
	pid_t			kpid;
	struct semaphore 	mutex;

	struct device_driver	*driver;
	struct device 		dev;
	struct completion 	dev_released;
	struct completion 	dev_exited;

	struct w1_bus_master	*bus_master;

	u32			seq, groups;
	struct sock 		*nls;
};

int w1_create_master_attributes(struct w1_master *);
void w1_destroy_master_attributes(struct w1_master *);
void w1_search(struct w1_master *dev);

#endif /* __KERNEL__ */

#endif /* __W1_H */
