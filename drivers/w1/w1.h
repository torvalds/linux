/*
 *	w1.h
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

#define W1_SLAVE_ACTIVE		0

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
	struct w1_family	*family;
	void			*family_data;
	struct device		dev;
	struct completion	released;
};

typedef void (* w1_slave_found_callback)(void *, u64);


/**
 * Note: read_bit and write_bit are very low level functions and should only
 * be used with hardware that doesn't really support 1-wire operations,
 * like a parallel/serial port.
 * Either define read_bit and write_bit OR define, at minimum, touch_bit and
 * reset_bus.
 */
struct w1_bus_master
{
	/** the first parameter in all the functions below */
	void		*data;

	/**
	 * Sample the line level
	 * @return the level read (0 or 1)
	 */
	u8		(*read_bit)(void *);

	/** Sets the line level */
	void		(*write_bit)(void *, u8);

	/**
	 * touch_bit is the lowest-level function for devices that really
	 * support the 1-wire protocol.
	 * touch_bit(0) = write-0 cycle
	 * touch_bit(1) = write-1 / read cycle
	 * @return the bit read (0 or 1)
	 */
	u8		(*touch_bit)(void *, u8);

	/**
	 * Reads a bytes. Same as 8 touch_bit(1) calls.
	 * @return the byte read
	 */
	u8		(*read_byte)(void *);

	/**
	 * Writes a byte. Same as 8 touch_bit(x) calls.
	 */
	void		(*write_byte)(void *, u8);

	/**
	 * Same as a series of read_byte() calls
	 * @return the number of bytes read
	 */
	u8		(*read_block)(void *, u8 *, int);

	/** Same as a series of write_byte() calls */
	void		(*write_block)(void *, const u8 *, int);

	/**
	 * Combines two reads and a smart write for ROM searches
	 * @return bit0=Id bit1=comp_id bit2=dir_taken
	 */
	u8		(*triplet)(void *, u8);

	/**
	 * long write-0 with a read for the presence pulse detection
	 * @return -1=Error, 0=Device present, 1=No device present
	 */
	u8		(*reset_bus)(void *);

	/** Really nice hardware can handles the ROM searches */
	void		(*search)(void *, w1_slave_found_callback);
};

#define W1_MASTER_NEED_EXIT		0
#define W1_MASTER_NEED_RECONNECT	1

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
	int			search_count;

	atomic_t		refcnt;

	void			*priv;
	int			priv_size;

	long			flags;

	struct task_struct	*thread;
	struct semaphore	mutex;

	struct device_driver	*driver;
	struct device		dev;

	struct w1_bus_master	*bus_master;

	u32			seq, groups;
	struct sock		*nls;
};

int w1_create_master_attributes(struct w1_master *);
void w1_search(struct w1_master *dev, w1_slave_found_callback cb);

static inline struct w1_slave* dev_to_w1_slave(struct device *dev)
{
	return container_of(dev, struct w1_slave, dev);
}

static inline struct w1_slave* kobj_to_w1_slave(struct kobject *kobj)
{
	return dev_to_w1_slave(container_of(kobj, struct device, kobj));
}

static inline struct w1_master* dev_to_w1_master(struct device *dev)
{
	return container_of(dev, struct w1_master, dev);
}

extern int w1_max_slave_count;
extern int w1_max_slave_ttl;
extern spinlock_t w1_mlock;
extern struct list_head w1_masters;
extern struct device_driver w1_master_driver;
extern struct device w1_master_device;

int w1_process(void *data);
void w1_reconnect_slaves(struct w1_family *f);

#endif /* __KERNEL__ */

#endif /* __W1_H */
