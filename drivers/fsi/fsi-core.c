/*
 * FSI core driver
 *
 * Copyright (C) IBM Corporation 2016
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/crc4.h>
#include <linux/device.h>
#include <linux/fsi.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/bitops.h>

#include "fsi-master.h"

#define CREATE_TRACE_POINTS
#include <trace/events/fsi.h>

#define FSI_SLAVE_CONF_NEXT_MASK	GENMASK(31, 31)
#define FSI_SLAVE_CONF_SLOTS_MASK	GENMASK(23, 16)
#define FSI_SLAVE_CONF_SLOTS_SHIFT	16
#define FSI_SLAVE_CONF_VERSION_MASK	GENMASK(15, 12)
#define FSI_SLAVE_CONF_VERSION_SHIFT	12
#define FSI_SLAVE_CONF_TYPE_MASK	GENMASK(11, 4)
#define FSI_SLAVE_CONF_TYPE_SHIFT	4
#define FSI_SLAVE_CONF_CRC_SHIFT	4
#define FSI_SLAVE_CONF_CRC_MASK		GENMASK(3, 0)
#define FSI_SLAVE_CONF_DATA_BITS	28

#define FSI_PEEK_BASE			0x410

static const int engine_page_size = 0x400;

#define FSI_SLAVE_BASE			0x800

/*
 * FSI slave engine control register offsets
 */
#define FSI_SMODE		0x0	/* R/W: Mode register */
#define FSI_SISC		0x8	/* R/W: Interrupt condition */
#define FSI_SSTAT		0x14	/* R  : Slave status */
#define FSI_LLMODE		0x100	/* R/W: Link layer mode register */

/*
 * SMODE fields
 */
#define FSI_SMODE_WSC		0x80000000	/* Warm start done */
#define FSI_SMODE_ECRC		0x20000000	/* Hw CRC check */
#define FSI_SMODE_SID_SHIFT	24		/* ID shift */
#define FSI_SMODE_SID_MASK	3		/* ID Mask */
#define FSI_SMODE_ED_SHIFT	20		/* Echo delay shift */
#define FSI_SMODE_ED_MASK	0xf		/* Echo delay mask */
#define FSI_SMODE_SD_SHIFT	16		/* Send delay shift */
#define FSI_SMODE_SD_MASK	0xf		/* Send delay mask */
#define FSI_SMODE_LBCRR_SHIFT	8		/* Clk ratio shift */
#define FSI_SMODE_LBCRR_MASK	0xf		/* Clk ratio mask */

/*
 * LLMODE fields
 */
#define FSI_LLMODE_ASYNC	0x1

#define FSI_SLAVE_SIZE_23b		0x800000

static DEFINE_IDA(master_ida);

struct fsi_slave {
	struct device		dev;
	struct fsi_master	*master;
	int			id;
	int			link;
	uint32_t		size;	/* size of slave address space */
};

#define to_fsi_master(d) container_of(d, struct fsi_master, dev)
#define to_fsi_slave(d) container_of(d, struct fsi_slave, dev)

static const int slave_retries = 2;
static int discard_errors;

static int fsi_master_read(struct fsi_master *master, int link,
		uint8_t slave_id, uint32_t addr, void *val, size_t size);
static int fsi_master_write(struct fsi_master *master, int link,
		uint8_t slave_id, uint32_t addr, const void *val, size_t size);
static int fsi_master_break(struct fsi_master *master, int link);

/*
 * fsi_device_read() / fsi_device_write() / fsi_device_peek()
 *
 * FSI endpoint-device support
 *
 * Read / write / peek accessors for a client
 *
 * Parameters:
 * dev:  Structure passed to FSI client device drivers on probe().
 * addr: FSI address of given device.  Client should pass in its base address
 *       plus desired offset to access its register space.
 * val:  For read/peek this is the value read at the specified address. For
 *       write this is value to write to the specified address.
 *       The data in val must be FSI bus endian (big endian).
 * size: Size in bytes of the operation.  Sizes supported are 1, 2 and 4 bytes.
 *       Addresses must be aligned on size boundaries or an error will result.
 */
int fsi_device_read(struct fsi_device *dev, uint32_t addr, void *val,
		size_t size)
{
	if (addr > dev->size || size > dev->size || addr > dev->size - size)
		return -EINVAL;

	return fsi_slave_read(dev->slave, dev->addr + addr, val, size);
}
EXPORT_SYMBOL_GPL(fsi_device_read);

int fsi_device_write(struct fsi_device *dev, uint32_t addr, const void *val,
		size_t size)
{
	if (addr > dev->size || size > dev->size || addr > dev->size - size)
		return -EINVAL;

	return fsi_slave_write(dev->slave, dev->addr + addr, val, size);
}
EXPORT_SYMBOL_GPL(fsi_device_write);

int fsi_device_peek(struct fsi_device *dev, void *val)
{
	uint32_t addr = FSI_PEEK_BASE + ((dev->unit - 2) * sizeof(uint32_t));

	return fsi_slave_read(dev->slave, addr, val, sizeof(uint32_t));
}

static void fsi_device_release(struct device *_device)
{
	struct fsi_device *device = to_fsi_dev(_device);

	kfree(device);
}

static struct fsi_device *fsi_create_device(struct fsi_slave *slave)
{
	struct fsi_device *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->dev.parent = &slave->dev;
	dev->dev.bus = &fsi_bus_type;
	dev->dev.release = fsi_device_release;

	return dev;
}

/* FSI slave support */
static int fsi_slave_calc_addr(struct fsi_slave *slave, uint32_t *addrp,
		uint8_t *idp)
{
	uint32_t addr = *addrp;
	uint8_t id = *idp;

	if (addr > slave->size)
		return -EINVAL;

	/* For 23 bit addressing, we encode the extra two bits in the slave
	 * id (and the slave's actual ID needs to be 0).
	 */
	if (addr > 0x1fffff) {
		if (slave->id != 0)
			return -EINVAL;
		id = (addr >> 21) & 0x3;
		addr &= 0x1fffff;
	}

	*addrp = addr;
	*idp = id;
	return 0;
}

int fsi_slave_report_and_clear_errors(struct fsi_slave *slave)
{
	struct fsi_master *master = slave->master;
	uint32_t irq, stat;
	int rc, link;
	uint8_t id;

	link = slave->link;
	id = slave->id;

	rc = fsi_master_read(master, link, id, FSI_SLAVE_BASE + FSI_SISC,
			&irq, sizeof(irq));
	if (rc)
		return rc;

	rc =  fsi_master_read(master, link, id, FSI_SLAVE_BASE + FSI_SSTAT,
			&stat, sizeof(stat));
	if (rc)
		return rc;

	dev_info(&slave->dev, "status: 0x%08x, sisc: 0x%08x\n",
			be32_to_cpu(stat), be32_to_cpu(irq));

	/* clear interrupts */
	return fsi_master_write(master, link, id, FSI_SLAVE_BASE + FSI_SISC,
			&irq, sizeof(irq));
}

static int fsi_slave_set_smode(struct fsi_master *master, int link, int id);

int fsi_slave_handle_error(struct fsi_slave *slave, bool write, uint32_t addr,
		size_t size)
{
	struct fsi_master *master = slave->master;
	int rc, link;
	uint32_t reg;
	uint8_t id;

	if (discard_errors)
		return -1;

	link = slave->link;
	id = slave->id;

	dev_dbg(&slave->dev, "handling error on %s to 0x%08x[%zd]",
			write ? "write" : "read", addr, size);

	/* try a simple clear of error conditions, which may fail if we've lost
	 * communication with the slave
	 */
	rc = fsi_slave_report_and_clear_errors(slave);
	if (!rc)
		return 0;

	/* send a TERM and retry */
	if (master->term) {
		rc = master->term(master, link, id);
		if (!rc) {
			rc = fsi_master_read(master, link, id, 0,
					&reg, sizeof(reg));
			if (!rc)
				rc = fsi_slave_report_and_clear_errors(slave);
			if (!rc)
				return 0;
		}
	}

	/* getting serious, reset the slave via BREAK */
	rc = fsi_master_break(master, link);
	if (rc)
		return rc;

	rc = fsi_slave_set_smode(master, link, id);
	if (rc)
		return rc;

	return fsi_slave_report_and_clear_errors(slave);
}

int fsi_slave_read(struct fsi_slave *slave, uint32_t addr,
			void *val, size_t size)
{
	uint8_t id = slave->id;
	int rc, err_rc, i;

	rc = fsi_slave_calc_addr(slave, &addr, &id);
	if (rc)
		return rc;

	for (i = 0; i < slave_retries; i++) {
		rc = fsi_master_read(slave->master, slave->link,
				id, addr, val, size);
		if (!rc)
			break;

		err_rc = fsi_slave_handle_error(slave, false, addr, size);
		if (err_rc)
			break;
	}

	return rc;
}
EXPORT_SYMBOL_GPL(fsi_slave_read);

int fsi_slave_write(struct fsi_slave *slave, uint32_t addr,
			const void *val, size_t size)
{
	uint8_t id = slave->id;
	int rc, err_rc, i;

	rc = fsi_slave_calc_addr(slave, &addr, &id);
	if (rc)
		return rc;

	for (i = 0; i < slave_retries; i++) {
		rc = fsi_master_write(slave->master, slave->link,
				id, addr, val, size);
		if (!rc)
			break;

		err_rc = fsi_slave_handle_error(slave, true, addr, size);
		if (err_rc)
			break;
	}

	return rc;
}
EXPORT_SYMBOL_GPL(fsi_slave_write);

extern int fsi_slave_claim_range(struct fsi_slave *slave,
		uint32_t addr, uint32_t size)
{
	if (addr + size < addr)
		return -EINVAL;

	if (addr + size > slave->size)
		return -EINVAL;

	/* todo: check for overlapping claims */
	return 0;
}
EXPORT_SYMBOL_GPL(fsi_slave_claim_range);

extern void fsi_slave_release_range(struct fsi_slave *slave,
		uint32_t addr, uint32_t size)
{
}
EXPORT_SYMBOL_GPL(fsi_slave_release_range);

static int fsi_slave_scan(struct fsi_slave *slave)
{
	uint32_t engine_addr;
	uint32_t conf;
	int rc, i;

	/*
	 * scan engines
	 *
	 * We keep the peek mode and slave engines for the core; so start
	 * at the third slot in the configuration table. We also need to
	 * skip the chip ID entry at the start of the address space.
	 */
	engine_addr = engine_page_size * 3;
	for (i = 2; i < engine_page_size / sizeof(uint32_t); i++) {
		uint8_t slots, version, type, crc;
		struct fsi_device *dev;

		rc = fsi_slave_read(slave, (i + 1) * sizeof(conf),
				&conf, sizeof(conf));
		if (rc) {
			dev_warn(&slave->dev,
				"error reading slave registers\n");
			return -1;
		}
		conf = be32_to_cpu(conf);

		crc = crc4(0, conf, 32);
		if (crc) {
			dev_warn(&slave->dev,
				"crc error in slave register at 0x%04x\n",
				i);
			return -1;
		}

		slots = (conf & FSI_SLAVE_CONF_SLOTS_MASK)
			>> FSI_SLAVE_CONF_SLOTS_SHIFT;
		version = (conf & FSI_SLAVE_CONF_VERSION_MASK)
			>> FSI_SLAVE_CONF_VERSION_SHIFT;
		type = (conf & FSI_SLAVE_CONF_TYPE_MASK)
			>> FSI_SLAVE_CONF_TYPE_SHIFT;

		/*
		 * Unused address areas are marked by a zero type value; this
		 * skips the defined address areas
		 */
		if (type != 0 && slots != 0) {

			/* create device */
			dev = fsi_create_device(slave);
			if (!dev)
				return -ENOMEM;

			dev->slave = slave;
			dev->engine_type = type;
			dev->version = version;
			dev->unit = i;
			dev->addr = engine_addr;
			dev->size = slots * engine_page_size;

			dev_dbg(&slave->dev,
			"engine[%i]: type %x, version %x, addr %x size %x\n",
					dev->unit, dev->engine_type, version,
					dev->addr, dev->size);

			dev_set_name(&dev->dev, "%02x:%02x:%02x:%02x",
					slave->master->idx, slave->link,
					slave->id, i - 2);

			rc = device_register(&dev->dev);
			if (rc) {
				dev_warn(&slave->dev, "add failed: %d\n", rc);
				put_device(&dev->dev);
			}
		}

		engine_addr += slots * engine_page_size;

		if (!(conf & FSI_SLAVE_CONF_NEXT_MASK))
			break;
	}

	return 0;
}

static ssize_t fsi_slave_sysfs_raw_read(struct file *file,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	struct fsi_slave *slave = to_fsi_slave(kobj_to_dev(kobj));
	size_t total_len, read_len;
	int rc;

	if (off < 0)
		return -EINVAL;

	if (off > 0xffffffff || count > 0xffffffff || off + count > 0xffffffff)
		return -EINVAL;

	for (total_len = 0; total_len < count; total_len += read_len) {
		read_len = min_t(size_t, count, 4);
		read_len -= off & 0x3;

		rc = fsi_slave_read(slave, off, buf + total_len, read_len);
		if (rc)
			return rc;

		off += read_len;
	}

	return count;
}

static ssize_t fsi_slave_sysfs_raw_write(struct file *file,
		struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct fsi_slave *slave = to_fsi_slave(kobj_to_dev(kobj));
	size_t total_len, write_len;
	int rc;

	if (off < 0)
		return -EINVAL;

	if (off > 0xffffffff || count > 0xffffffff || off + count > 0xffffffff)
		return -EINVAL;

	for (total_len = 0; total_len < count; total_len += write_len) {
		write_len = min_t(size_t, count, 4);
		write_len -= off & 0x3;

		rc = fsi_slave_write(slave, off, buf + total_len, write_len);
		if (rc)
			return rc;

		off += write_len;
	}

	return count;
}

static struct bin_attribute fsi_slave_raw_attr = {
	.attr = {
		.name = "raw",
		.mode = 0600,
	},
	.size = 0,
	.read = fsi_slave_sysfs_raw_read,
	.write = fsi_slave_sysfs_raw_write,
};

static ssize_t fsi_slave_sysfs_term_write(struct file *file,
		struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct fsi_slave *slave = to_fsi_slave(kobj_to_dev(kobj));
	struct fsi_master *master = slave->master;

	if (!master->term)
		return -ENODEV;

	master->term(master, slave->link, slave->id);
	return count;
}

static struct bin_attribute fsi_slave_term_attr = {
	.attr = {
		.name = "term",
		.mode = 0200,
	},
	.size = 0,
	.write = fsi_slave_sysfs_term_write,
};

/* Encode slave local bus echo delay */
static inline uint32_t fsi_smode_echodly(int x)
{
	return (x & FSI_SMODE_ED_MASK) << FSI_SMODE_ED_SHIFT;
}

/* Encode slave local bus send delay */
static inline uint32_t fsi_smode_senddly(int x)
{
	return (x & FSI_SMODE_SD_MASK) << FSI_SMODE_SD_SHIFT;
}

/* Encode slave local bus clock rate ratio */
static inline uint32_t fsi_smode_lbcrr(int x)
{
	return (x & FSI_SMODE_LBCRR_MASK) << FSI_SMODE_LBCRR_SHIFT;
}

/* Encode slave ID */
static inline uint32_t fsi_smode_sid(int x)
{
	return (x & FSI_SMODE_SID_MASK) << FSI_SMODE_SID_SHIFT;
}

static const uint32_t fsi_slave_smode(int id)
{
	return FSI_SMODE_WSC | FSI_SMODE_ECRC
		| fsi_smode_sid(id)
		| fsi_smode_echodly(0xf) | fsi_smode_senddly(0xf)
		| fsi_smode_lbcrr(0x8);
}

static int fsi_slave_set_smode(struct fsi_master *master, int link, int id)
{
	uint32_t smode;

	/* set our smode register with the slave ID field to 0; this enables
	 * extended slave addressing
	 */
	smode = fsi_slave_smode(id);
	smode = cpu_to_be32(smode);

	return fsi_master_write(master, link, id, FSI_SLAVE_BASE + FSI_SMODE,
			&smode, sizeof(smode));
}

static void fsi_slave_release(struct device *dev)
{
	struct fsi_slave *slave = to_fsi_slave(dev);

	kfree(slave);
}

static int fsi_slave_init(struct fsi_master *master, int link, uint8_t id)
{
	uint32_t chip_id, llmode;
	struct fsi_slave *slave;
	uint8_t crc;
	int rc;

	/* Currently, we only support single slaves on a link, and use the
	 * full 23-bit address range
	 */
	if (id != 0)
		return -EINVAL;

	rc = fsi_master_read(master, link, id, 0, &chip_id, sizeof(chip_id));
	if (rc) {
		dev_dbg(&master->dev, "can't read slave %02x:%02x %d\n",
				link, id, rc);
		return -ENODEV;
	}
	chip_id = be32_to_cpu(chip_id);

	crc = crc4(0, chip_id, 32);
	if (crc) {
		dev_warn(&master->dev, "slave %02x:%02x invalid chip id CRC!\n",
				link, id);
		return -EIO;
	}

	dev_info(&master->dev, "fsi: found chip %08x at %02x:%02x:%02x\n",
			chip_id, master->idx, link, id);

	rc = fsi_slave_set_smode(master, link, id);
	if (rc) {
		dev_warn(&master->dev,
				"can't set smode on slave:%02x:%02x %d\n",
				link, id, rc);
		return -ENODEV;
	}

	/* If we're behind a master that doesn't provide a self-running bus
	 * clock, put the slave into async mode
	 */
	if (master->flags & FSI_MASTER_FLAG_SWCLOCK) {
		llmode = cpu_to_be32(FSI_LLMODE_ASYNC);
		rc = fsi_master_write(master, link, id,
				FSI_SLAVE_BASE + FSI_LLMODE,
				&llmode, sizeof(llmode));
		if (rc)
			dev_warn(&master->dev,
				"can't set llmode on slave:%02x:%02x %d\n",
				link, id, rc);
	}

	/* We can communicate with a slave; create the slave device and
	 * register.
	 */
	slave = kzalloc(sizeof(*slave), GFP_KERNEL);
	if (!slave)
		return -ENOMEM;

	slave->master = master;
	slave->dev.parent = &master->dev;
	slave->dev.release = fsi_slave_release;
	slave->link = link;
	slave->id = id;
	slave->size = FSI_SLAVE_SIZE_23b;

	dev_set_name(&slave->dev, "slave@%02x:%02x", link, id);
	rc = device_register(&slave->dev);
	if (rc < 0) {
		dev_warn(&master->dev, "failed to create slave device: %d\n",
				rc);
		put_device(&slave->dev);
		return rc;
	}

	rc = device_create_bin_file(&slave->dev, &fsi_slave_raw_attr);
	if (rc)
		dev_warn(&slave->dev, "failed to create raw attr: %d\n", rc);

	rc = device_create_bin_file(&slave->dev, &fsi_slave_term_attr);
	if (rc)
		dev_warn(&slave->dev, "failed to create term attr: %d\n", rc);

	rc = fsi_slave_scan(slave);
	if (rc)
		dev_dbg(&master->dev, "failed during slave scan with: %d\n",
				rc);

	return rc;
}

/* FSI master support */
static int fsi_check_access(uint32_t addr, size_t size)
{
	if (size != 1 && size != 2 && size != 4)
		return -EINVAL;

	if ((addr & 0x3) != (size & 0x3))
		return -EINVAL;

	return 0;
}

static int fsi_master_read(struct fsi_master *master, int link,
		uint8_t slave_id, uint32_t addr, void *val, size_t size)
{
	int rc;

	trace_fsi_master_read(master, link, slave_id, addr, size);

	rc = fsi_check_access(addr, size);
	if (!rc)
		rc = master->read(master, link, slave_id, addr, val, size);

	trace_fsi_master_rw_result(master, link, slave_id, addr, size,
			false, val, rc);

	return rc;
}

static int fsi_master_write(struct fsi_master *master, int link,
		uint8_t slave_id, uint32_t addr, const void *val, size_t size)
{
	int rc;

	trace_fsi_master_write(master, link, slave_id, addr, size, val);

	rc = fsi_check_access(addr, size);
	if (!rc)
		rc = master->write(master, link, slave_id, addr, val, size);

	trace_fsi_master_rw_result(master, link, slave_id, addr, size,
			true, val, rc);

	return rc;
}

static int fsi_master_link_enable(struct fsi_master *master, int link)
{
	if (master->link_enable)
		return master->link_enable(master, link);

	return 0;
}

/*
 * Issue a break command on this link
 */
static int fsi_master_break(struct fsi_master *master, int link)
{
	trace_fsi_master_break(master, link);

	if (master->send_break)
		return master->send_break(master, link);

	return 0;
}

static int fsi_master_scan(struct fsi_master *master)
{
	int link, rc;

	for (link = 0; link < master->n_links; link++) {
		rc = fsi_master_link_enable(master, link);
		if (rc) {
			dev_dbg(&master->dev,
				"enable link %d failed: %d\n", link, rc);
			continue;
		}
		rc = fsi_master_break(master, link);
		if (rc) {
			dev_dbg(&master->dev,
				"break to link %d failed: %d\n", link, rc);
			continue;
		}

		fsi_slave_init(master, link, 0);
	}

	return 0;
}

static int fsi_slave_remove_device(struct device *dev, void *arg)
{
	device_unregister(dev);
	return 0;
}

static int fsi_master_remove_slave(struct device *dev, void *arg)
{
	device_for_each_child(dev, NULL, fsi_slave_remove_device);
	device_unregister(dev);
	return 0;
}

static void fsi_master_unscan(struct fsi_master *master)
{
	device_for_each_child(&master->dev, NULL, fsi_master_remove_slave);
}

static ssize_t master_rescan_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct fsi_master *master = to_fsi_master(dev);
	int rc;

	fsi_master_unscan(master);
	rc = fsi_master_scan(master);
	if (rc < 0)
		return rc;

	return count;
}

static DEVICE_ATTR(rescan, 0200, NULL, master_rescan_store);

static ssize_t master_break_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct fsi_master *master = to_fsi_master(dev);

	fsi_master_break(master, 0);

	return count;
}

static DEVICE_ATTR(break, 0200, NULL, master_break_store);

int fsi_master_register(struct fsi_master *master)
{
	int rc;

	if (!master)
		return -EINVAL;

	master->idx = ida_simple_get(&master_ida, 0, INT_MAX, GFP_KERNEL);
	dev_set_name(&master->dev, "fsi%d", master->idx);

	rc = device_register(&master->dev);
	if (rc) {
		ida_simple_remove(&master_ida, master->idx);
		return rc;
	}

	rc = device_create_file(&master->dev, &dev_attr_rescan);
	if (rc) {
		device_unregister(&master->dev);
		ida_simple_remove(&master_ida, master->idx);
		return rc;
	}

	rc = device_create_file(&master->dev, &dev_attr_break);
	if (rc) {
		device_unregister(&master->dev);
		ida_simple_remove(&master_ida, master->idx);
		return rc;
	}

	fsi_master_scan(master);

	return 0;
}
EXPORT_SYMBOL_GPL(fsi_master_register);

void fsi_master_unregister(struct fsi_master *master)
{
	if (master->idx >= 0) {
		ida_simple_remove(&master_ida, master->idx);
		master->idx = -1;
	}

	fsi_master_unscan(master);
	device_unregister(&master->dev);
}
EXPORT_SYMBOL_GPL(fsi_master_unregister);

/* FSI core & Linux bus type definitions */

static int fsi_bus_match(struct device *dev, struct device_driver *drv)
{
	struct fsi_device *fsi_dev = to_fsi_dev(dev);
	struct fsi_driver *fsi_drv = to_fsi_drv(drv);
	const struct fsi_device_id *id;

	if (!fsi_drv->id_table)
		return 0;

	for (id = fsi_drv->id_table; id->engine_type; id++) {
		if (id->engine_type != fsi_dev->engine_type)
			continue;
		if (id->version == FSI_VERSION_ANY ||
				id->version == fsi_dev->version)
			return 1;
	}

	return 0;
}

int fsi_driver_register(struct fsi_driver *fsi_drv)
{
	if (!fsi_drv)
		return -EINVAL;
	if (!fsi_drv->id_table)
		return -EINVAL;

	return driver_register(&fsi_drv->drv);
}
EXPORT_SYMBOL_GPL(fsi_driver_register);

void fsi_driver_unregister(struct fsi_driver *fsi_drv)
{
	driver_unregister(&fsi_drv->drv);
}
EXPORT_SYMBOL_GPL(fsi_driver_unregister);

struct bus_type fsi_bus_type = {
	.name		= "fsi",
	.match		= fsi_bus_match,
};
EXPORT_SYMBOL_GPL(fsi_bus_type);

static int fsi_init(void)
{
	return bus_register(&fsi_bus_type);
}

static void fsi_exit(void)
{
	bus_unregister(&fsi_bus_type);
}

module_init(fsi_init);
module_exit(fsi_exit);
module_param(discard_errors, int, 0664);
MODULE_LICENSE("GPL");
MODULE_PARM_DESC(discard_errors, "Don't invoke error handling on bus accesses");
