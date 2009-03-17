/*
 * Copyright (C) 2005-2006  Kristian Hoegsberg <krh@bitplanet.net>
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __fw_device_h
#define __fw_device_h

#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/idr.h>
#include <linux/rwsem.h>
#include <asm/atomic.h>

enum fw_device_state {
	FW_DEVICE_INITIALIZING,
	FW_DEVICE_RUNNING,
	FW_DEVICE_GONE,
	FW_DEVICE_SHUTDOWN,
};

struct fw_attribute_group {
	struct attribute_group *groups[2];
	struct attribute_group group;
	struct attribute *attrs[11];
};

/*
 * Note, fw_device.generation always has to be read before fw_device.node_id.
 * Use SMP memory barriers to ensure this.  Otherwise requests will be sent
 * to an outdated node_id if the generation was updated in the meantime due
 * to a bus reset.
 *
 * Likewise, fw-core will take care to update .node_id before .generation so
 * that whenever fw_device.generation is current WRT the actual bus generation,
 * fw_device.node_id is guaranteed to be current too.
 *
 * The same applies to fw_device.card->node_id vs. fw_device.generation.
 *
 * fw_device.config_rom and fw_device.config_rom_length may be accessed during
 * the lifetime of any fw_unit belonging to the fw_device, before device_del()
 * was called on the last fw_unit.  Alternatively, they may be accessed while
 * holding fw_device_rwsem.
 */
struct fw_device {
	atomic_t state;
	struct fw_node *node;
	int node_id;
	int generation;
	unsigned max_speed;
	bool cmc;
	struct fw_card *card;
	struct device device;
	struct list_head client_list;
	u32 *config_rom;
	size_t config_rom_length;
	int config_rom_retries;
	struct delayed_work work;
	struct fw_attribute_group attribute_group;
};

static inline struct fw_device *fw_device(struct device *dev)
{
	return container_of(dev, struct fw_device, device);
}

static inline int fw_device_is_shutdown(struct fw_device *device)
{
	return atomic_read(&device->state) == FW_DEVICE_SHUTDOWN;
}

static inline struct fw_device *fw_device_get(struct fw_device *device)
{
	get_device(&device->device);

	return device;
}

static inline void fw_device_put(struct fw_device *device)
{
	put_device(&device->device);
}

struct fw_device *fw_device_get_by_devt(dev_t devt);
int fw_device_enable_phys_dma(struct fw_device *device);

void fw_device_cdev_update(struct fw_device *device);
void fw_device_cdev_remove(struct fw_device *device);

extern struct rw_semaphore fw_device_rwsem;
extern struct idr fw_device_idr;
extern int fw_cdev_major;

/*
 * fw_unit.directory must not be accessed after device_del(&fw_unit.device).
 */
struct fw_unit {
	struct device device;
	u32 *directory;
	struct fw_attribute_group attribute_group;
};

static inline struct fw_unit *fw_unit(struct device *dev)
{
	return container_of(dev, struct fw_unit, device);
}

static inline struct fw_unit *fw_unit_get(struct fw_unit *unit)
{
	get_device(&unit->device);

	return unit;
}

static inline void fw_unit_put(struct fw_unit *unit)
{
	put_device(&unit->device);
}

#define CSR_OFFSET	0x40
#define CSR_LEAF	0x80
#define CSR_DIRECTORY	0xc0

#define CSR_DESCRIPTOR		0x01
#define CSR_VENDOR		0x03
#define CSR_HARDWARE_VERSION	0x04
#define CSR_NODE_CAPABILITIES	0x0c
#define CSR_UNIT		0x11
#define CSR_SPECIFIER_ID	0x12
#define CSR_VERSION		0x13
#define CSR_DEPENDENT_INFO	0x14
#define CSR_MODEL		0x17
#define CSR_INSTANCE		0x18
#define CSR_DIRECTORY_ID	0x20

struct fw_csr_iterator {
	u32 *p;
	u32 *end;
};

void fw_csr_iterator_init(struct fw_csr_iterator *ci, u32 *p);
int fw_csr_iterator_next(struct fw_csr_iterator *ci,
			 int *key, int *value);

#define FW_MATCH_VENDOR		0x0001
#define FW_MATCH_MODEL		0x0002
#define FW_MATCH_SPECIFIER_ID	0x0004
#define FW_MATCH_VERSION	0x0008

struct fw_device_id {
	u32 match_flags;
	u32 vendor;
	u32 model;
	u32 specifier_id;
	u32 version;
	void *driver_data;
};

struct fw_driver {
	struct device_driver driver;
	/* Called when the parent device sits through a bus reset. */
	void (*update) (struct fw_unit *unit);
	const struct fw_device_id *id_table;
};

static inline struct fw_driver *
fw_driver(struct device_driver *drv)
{
	return container_of(drv, struct fw_driver, driver);
}

extern const struct file_operations fw_device_ops;

#endif /* __fw_device_h */
