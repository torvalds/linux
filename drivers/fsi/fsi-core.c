// SPDX-License-Identifier: GPL-2.0-only
/*
 * FSI core driver
 *
 * Copyright (C) IBM Corporation 2016
 *
 * TODO:
 *  - Rework topology
 *  - s/chip_id/chip_loc
 *  - s/cfam/chip (cfam_id -> chip_id etc...)
 */

#include <linux/crc4.h>
#include <linux/device.h>
#include <linux/fsi.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include "fsi-master.h"
#include "fsi-slave.h"

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
#define FSI_SLBUS		0x30	/* W  : LBUS Ownership */
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
 * SLBUS fields
 */
#define FSI_SLBUS_FORCE		0x80000000	/* Force LBUS ownership */

/*
 * LLMODE fields
 */
#define FSI_LLMODE_ASYNC	0x1

#define FSI_SLAVE_SIZE_23b		0x800000

static DEFINE_IDA(master_ida);

static const int slave_retries = 2;
static int discard_errors;

static dev_t fsi_base_dev;
static DEFINE_IDA(fsi_minor_ida);
#define FSI_CHAR_MAX_DEVICES	0x1000

/* Legacy /dev numbering: 4 devices per chip, 16 chips */
#define FSI_CHAR_LEGACY_TOP	64

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

	of_node_put(device->dev.of_node);
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

static int fsi_slave_report_and_clear_errors(struct fsi_slave *slave)
{
	struct fsi_master *master = slave->master;
	__be32 irq, stat;
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

	dev_dbg(&slave->dev, "status: 0x%08x, sisc: 0x%08x\n",
			be32_to_cpu(stat), be32_to_cpu(irq));

	/* clear interrupts */
	return fsi_master_write(master, link, id, FSI_SLAVE_BASE + FSI_SISC,
			&irq, sizeof(irq));
}

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

static uint32_t fsi_slave_smode(int id, u8 t_senddly, u8 t_echodly)
{
	return FSI_SMODE_WSC | FSI_SMODE_ECRC
		| fsi_smode_sid(id)
		| fsi_smode_echodly(t_echodly - 1) | fsi_smode_senddly(t_senddly - 1)
		| fsi_smode_lbcrr(0x8);
}

static int fsi_slave_set_smode(struct fsi_slave *slave)
{
	uint32_t smode;
	__be32 data;

	/* set our smode register with the slave ID field to 0; this enables
	 * extended slave addressing
	 */
	smode = fsi_slave_smode(slave->id, slave->t_send_delay, slave->t_echo_delay);
	data = cpu_to_be32(smode);

	return fsi_master_write(slave->master, slave->link, slave->id,
				FSI_SLAVE_BASE + FSI_SMODE,
				&data, sizeof(data));
}

static int fsi_slave_handle_error(struct fsi_slave *slave, bool write,
				  uint32_t addr, size_t size)
{
	struct fsi_master *master = slave->master;
	int rc, link;
	uint32_t reg;
	uint8_t id, send_delay, echo_delay;

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

	send_delay = slave->t_send_delay;
	echo_delay = slave->t_echo_delay;

	/* getting serious, reset the slave via BREAK */
	rc = fsi_master_break(master, link);
	if (rc)
		return rc;

	slave->t_send_delay = send_delay;
	slave->t_echo_delay = echo_delay;

	rc = fsi_slave_set_smode(slave);
	if (rc)
		return rc;

	if (master->link_config)
		master->link_config(master, link,
				    slave->t_send_delay,
				    slave->t_echo_delay);

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

int fsi_slave_claim_range(struct fsi_slave *slave,
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

void fsi_slave_release_range(struct fsi_slave *slave,
			     uint32_t addr, uint32_t size)
{
}
EXPORT_SYMBOL_GPL(fsi_slave_release_range);

static bool fsi_device_node_matches(struct device *dev, struct device_node *np,
		uint32_t addr, uint32_t size)
{
	u64 paddr, psize;

	if (of_property_read_reg(np, 0, &paddr, &psize))
		return false;

	if (paddr != addr)
		return false;

	if (psize != size) {
		dev_warn(dev,
			"node %pOF matches probed address, but not size (got 0x%llx, expected 0x%x)",
			np, psize, size);
	}

	return true;
}

/* Find a matching node for the slave engine at @address, using @size bytes
 * of space. Returns NULL if not found, or a matching node with refcount
 * already incremented.
 */
static struct device_node *fsi_device_find_of_node(struct fsi_device *dev)
{
	struct device_node *parent, *np;

	parent = dev_of_node(&dev->slave->dev);
	if (!parent)
		return NULL;

	for_each_child_of_node(parent, np) {
		if (fsi_device_node_matches(&dev->dev, np,
					dev->addr, dev->size))
			return np;
	}

	return NULL;
}

static int fsi_slave_scan(struct fsi_slave *slave)
{
	uint32_t engine_addr;
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
		uint32_t conf;
		__be32 data;

		rc = fsi_slave_read(slave, (i + 1) * sizeof(data),
				&data, sizeof(data));
		if (rc) {
			dev_warn(&slave->dev,
				"error reading slave registers\n");
			return -1;
		}
		conf = be32_to_cpu(data);

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

			trace_fsi_dev_init(dev);

			dev_dbg(&slave->dev,
			"engine[%i]: type %x, version %x, addr %x size %x\n",
					dev->unit, dev->engine_type, version,
					dev->addr, dev->size);

			dev_set_name(&dev->dev, "%02x:%02x:%02x:%02x",
					slave->master->idx, slave->link,
					slave->id, i - 2);
			dev->dev.of_node = fsi_device_find_of_node(dev);

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

static unsigned long aligned_access_size(size_t offset, size_t count)
{
	unsigned long offset_unit, count_unit;

	/* Criteria:
	 *
	 * 1. Access size must be less than or equal to the maximum access
	 *    width or the highest power-of-two factor of offset
	 * 2. Access size must be less than or equal to the amount specified by
	 *    count
	 *
	 * The access width is optimal if we can calculate 1 to be strictly
	 * equal while still satisfying 2.
	 */

	/* Find 1 by the bottom bit of offset (with a 4 byte access cap) */
	offset_unit = BIT(__builtin_ctzl(offset | 4));

	/* Find 2 by the top bit of count */
	count_unit = BIT(8 * sizeof(unsigned long) - 1 - __builtin_clzl(count));

	/* Constrain the maximum access width to the minimum of both criteria */
	return BIT(__builtin_ctzl(offset_unit | count_unit));
}

static ssize_t fsi_slave_sysfs_raw_read(struct file *file,
		struct kobject *kobj, const struct bin_attribute *attr, char *buf,
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
		read_len = aligned_access_size(off, count - total_len);

		rc = fsi_slave_read(slave, off, buf + total_len, read_len);
		if (rc)
			return rc;

		off += read_len;
	}

	return count;
}

static ssize_t fsi_slave_sysfs_raw_write(struct file *file,
		struct kobject *kobj, const struct bin_attribute *attr,
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
		write_len = aligned_access_size(off, count - total_len);

		rc = fsi_slave_write(slave, off, buf + total_len, write_len);
		if (rc)
			return rc;

		off += write_len;
	}

	return count;
}

static const struct bin_attribute fsi_slave_raw_attr = {
	.attr = {
		.name = "raw",
		.mode = 0600,
	},
	.size = 0,
	.read = fsi_slave_sysfs_raw_read,
	.write = fsi_slave_sysfs_raw_write,
};

static void fsi_slave_release(struct device *dev)
{
	struct fsi_slave *slave = to_fsi_slave(dev);

	fsi_free_minor(slave->dev.devt);
	of_node_put(dev->of_node);
	kfree(slave);
}

static bool fsi_slave_node_matches(struct device_node *np,
		int link, uint8_t id)
{
	u64 addr;

	if (of_property_read_reg(np, 0, &addr, NULL))
		return false;

	return addr == (((u64)link << 32) | id);
}

/* Find a matching node for the slave at (link, id). Returns NULL if none
 * found, or a matching node with refcount already incremented.
 */
static struct device_node *fsi_slave_find_of_node(struct fsi_master *master,
		int link, uint8_t id)
{
	struct device_node *parent, *np;

	parent = dev_of_node(&master->dev);
	if (!parent)
		return NULL;

	for_each_child_of_node(parent, np) {
		if (fsi_slave_node_matches(np, link, id))
			return np;
	}

	return NULL;
}

static ssize_t cfam_read(struct file *filep, char __user *buf, size_t count,
			 loff_t *offset)
{
	struct fsi_slave *slave = filep->private_data;
	size_t total_len, read_len;
	loff_t off = *offset;
	ssize_t rc;

	if (off < 0)
		return -EINVAL;

	if (off > 0xffffffff || count > 0xffffffff || off + count > 0xffffffff)
		return -EINVAL;

	for (total_len = 0; total_len < count; total_len += read_len) {
		__be32 data;

		read_len = min_t(size_t, count, 4);
		read_len -= off & 0x3;

		rc = fsi_slave_read(slave, off, &data, read_len);
		if (rc)
			goto fail;
		rc = copy_to_user(buf + total_len, &data, read_len);
		if (rc) {
			rc = -EFAULT;
			goto fail;
		}
		off += read_len;
	}
	rc = count;
 fail:
	*offset = off;
	return rc;
}

static ssize_t cfam_write(struct file *filep, const char __user *buf,
			  size_t count, loff_t *offset)
{
	struct fsi_slave *slave = filep->private_data;
	size_t total_len, write_len;
	loff_t off = *offset;
	ssize_t rc;


	if (off < 0)
		return -EINVAL;

	if (off > 0xffffffff || count > 0xffffffff || off + count > 0xffffffff)
		return -EINVAL;

	for (total_len = 0; total_len < count; total_len += write_len) {
		__be32 data;

		write_len = min_t(size_t, count, 4);
		write_len -= off & 0x3;

		rc = copy_from_user(&data, buf + total_len, write_len);
		if (rc) {
			rc = -EFAULT;
			goto fail;
		}
		rc = fsi_slave_write(slave, off, &data, write_len);
		if (rc)
			goto fail;
		off += write_len;
	}
	rc = count;
 fail:
	*offset = off;
	return rc;
}

static loff_t cfam_llseek(struct file *file, loff_t offset, int whence)
{
	switch (whence) {
	case SEEK_CUR:
		break;
	case SEEK_SET:
		file->f_pos = offset;
		break;
	default:
		return -EINVAL;
	}

	return offset;
}

static int cfam_open(struct inode *inode, struct file *file)
{
	struct fsi_slave *slave = container_of(inode->i_cdev, struct fsi_slave, cdev);

	file->private_data = slave;

	return 0;
}

static const struct file_operations cfam_fops = {
	.owner		= THIS_MODULE,
	.open		= cfam_open,
	.llseek		= cfam_llseek,
	.read		= cfam_read,
	.write		= cfam_write,
};

static ssize_t send_term_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct fsi_slave *slave = to_fsi_slave(dev);
	struct fsi_master *master = slave->master;

	if (!master->term)
		return -ENODEV;

	master->term(master, slave->link, slave->id);
	return count;
}

static DEVICE_ATTR_WO(send_term);

static ssize_t slave_send_echo_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct fsi_slave *slave = to_fsi_slave(dev);

	return sprintf(buf, "%u\n", slave->t_send_delay);
}

static ssize_t slave_send_echo_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct fsi_slave *slave = to_fsi_slave(dev);
	struct fsi_master *master = slave->master;
	unsigned long val;
	int rc;

	if (kstrtoul(buf, 0, &val) < 0)
		return -EINVAL;

	if (val < 1 || val > 16)
		return -EINVAL;

	if (!master->link_config)
		return -ENXIO;

	/* Current HW mandates that send and echo delay are identical */
	slave->t_send_delay = val;
	slave->t_echo_delay = val;

	rc = fsi_slave_set_smode(slave);
	if (rc < 0)
		return rc;
	if (master->link_config)
		master->link_config(master, slave->link,
				    slave->t_send_delay,
				    slave->t_echo_delay);

	return count;
}

static DEVICE_ATTR(send_echo_delays, 0600,
		   slave_send_echo_show, slave_send_echo_store);

static ssize_t chip_id_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct fsi_slave *slave = to_fsi_slave(dev);

	return sprintf(buf, "%d\n", slave->chip_id);
}

static DEVICE_ATTR_RO(chip_id);

static ssize_t cfam_id_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct fsi_slave *slave = to_fsi_slave(dev);

	return sprintf(buf, "0x%x\n", slave->cfam_id);
}

static DEVICE_ATTR_RO(cfam_id);

static struct attribute *cfam_attr[] = {
	&dev_attr_send_echo_delays.attr,
	&dev_attr_chip_id.attr,
	&dev_attr_cfam_id.attr,
	&dev_attr_send_term.attr,
	NULL,
};

static const struct attribute_group cfam_attr_group = {
	.attrs = cfam_attr,
};

static const struct attribute_group *cfam_attr_groups[] = {
	&cfam_attr_group,
	NULL,
};

static char *cfam_devnode(const struct device *dev, umode_t *mode,
			  kuid_t *uid, kgid_t *gid)
{
	const struct fsi_slave *slave = to_fsi_slave(dev);

#ifdef CONFIG_FSI_NEW_DEV_NODE
	return kasprintf(GFP_KERNEL, "fsi/cfam%d", slave->cdev_idx);
#else
	return kasprintf(GFP_KERNEL, "cfam%d", slave->cdev_idx);
#endif
}

static const struct device_type cfam_type = {
	.name = "cfam",
	.devnode = cfam_devnode,
	.groups = cfam_attr_groups
};

static char *fsi_cdev_devnode(const struct device *dev, umode_t *mode,
			      kuid_t *uid, kgid_t *gid)
{
#ifdef CONFIG_FSI_NEW_DEV_NODE
	return kasprintf(GFP_KERNEL, "fsi/%s", dev_name(dev));
#else
	return kasprintf(GFP_KERNEL, "%s", dev_name(dev));
#endif
}

const struct device_type fsi_cdev_type = {
	.name = "fsi-cdev",
	.devnode = fsi_cdev_devnode,
};
EXPORT_SYMBOL_GPL(fsi_cdev_type);

/* Backward compatible /dev/ numbering in "old style" mode */
static int fsi_adjust_index(int index)
{
#ifdef CONFIG_FSI_NEW_DEV_NODE
	return index;
#else
	return index + 1;
#endif
}

static int __fsi_get_new_minor(struct fsi_slave *slave, enum fsi_dev_type type,
			       dev_t *out_dev, int *out_index)
{
	int cid = slave->chip_id;
	int id;

	/* Check if we qualify for legacy numbering */
	if (cid >= 0 && cid < 16 && type < 4) {
		/*
		 * Try reserving the legacy number, which has 0 - 0x3f reserved
		 * in the ida range. cid goes up to 0xf and type contains two
		 * bits, so construct the id with the below two bit shift.
		 */
		id = (cid << 2) | type;
		id = ida_alloc_range(&fsi_minor_ida, id, id, GFP_KERNEL);
		if (id >= 0) {
			*out_index = fsi_adjust_index(cid);
			*out_dev = fsi_base_dev + id;
			return 0;
		}
		/* Other failure */
		if (id != -ENOSPC)
			return id;
		/* Fallback to non-legacy allocation */
	}
	id = ida_alloc_range(&fsi_minor_ida, FSI_CHAR_LEGACY_TOP,
			     FSI_CHAR_MAX_DEVICES - 1, GFP_KERNEL);
	if (id < 0)
		return id;
	*out_index = fsi_adjust_index(id);
	*out_dev = fsi_base_dev + id;
	return 0;
}

static const char *const fsi_dev_type_names[] = {
	"cfam",
	"sbefifo",
	"scom",
	"occ",
};

int fsi_get_new_minor(struct fsi_device *fdev, enum fsi_dev_type type,
		      dev_t *out_dev, int *out_index)
{
	if (fdev->dev.of_node) {
		int aid = of_alias_get_id(fdev->dev.of_node, fsi_dev_type_names[type]);

		if (aid >= 0) {
			/* Use the same scheme as the legacy numbers. */
			int id = (aid << 2) | type;

			id = ida_alloc_range(&fsi_minor_ida, id, id, GFP_KERNEL);
			if (id >= 0) {
				*out_index = aid;
				*out_dev = fsi_base_dev + id;
				return 0;
			}

			if (id != -ENOSPC)
				return id;
		}
	}

	return __fsi_get_new_minor(fdev->slave, type, out_dev, out_index);
}
EXPORT_SYMBOL_GPL(fsi_get_new_minor);

void fsi_free_minor(dev_t dev)
{
	ida_free(&fsi_minor_ida, MINOR(dev));
}
EXPORT_SYMBOL_GPL(fsi_free_minor);

static int fsi_slave_init(struct fsi_master *master, int link, uint8_t id)
{
	uint32_t cfam_id;
	struct fsi_slave *slave;
	uint8_t crc;
	__be32 data, llmode, slbus;
	int rc;

	/* Currently, we only support single slaves on a link, and use the
	 * full 23-bit address range
	 */
	if (id != 0)
		return -EINVAL;

	rc = fsi_master_read(master, link, id, 0, &data, sizeof(data));
	if (rc) {
		dev_dbg(&master->dev, "can't read slave %02x:%02x %d\n",
				link, id, rc);
		return -ENODEV;
	}
	cfam_id = be32_to_cpu(data);

	crc = crc4(0, cfam_id, 32);
	if (crc) {
		trace_fsi_slave_invalid_cfam(master, link, cfam_id);
		dev_warn(&master->dev, "slave %02x:%02x invalid cfam id CRC!\n",
				link, id);
		return -EIO;
	}

	dev_dbg(&master->dev, "fsi: found chip %08x at %02x:%02x:%02x\n",
			cfam_id, master->idx, link, id);

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

	dev_set_name(&slave->dev, "slave@%02x:%02x", link, id);
	slave->dev.type = &cfam_type;
	slave->dev.parent = &master->dev;
	slave->dev.of_node = fsi_slave_find_of_node(master, link, id);
	slave->dev.release = fsi_slave_release;
	device_initialize(&slave->dev);
	slave->cfam_id = cfam_id;
	slave->master = master;
	slave->link = link;
	slave->id = id;
	slave->size = FSI_SLAVE_SIZE_23b;
	slave->t_send_delay = 16;
	slave->t_echo_delay = 16;

	/* Get chip ID if any */
	slave->chip_id = -1;
	if (slave->dev.of_node) {
		uint32_t prop;
		if (!of_property_read_u32(slave->dev.of_node, "chip-id", &prop))
			slave->chip_id = prop;

	}

	slbus = cpu_to_be32(FSI_SLBUS_FORCE);
	rc = fsi_master_write(master, link, id, FSI_SLAVE_BASE + FSI_SLBUS,
			      &slbus, sizeof(slbus));
	if (rc)
		dev_warn(&master->dev,
			 "can't set slbus on slave:%02x:%02x %d\n", link, id,
			 rc);

	rc = fsi_slave_set_smode(slave);
	if (rc) {
		dev_warn(&master->dev,
				"can't set smode on slave:%02x:%02x %d\n",
				link, id, rc);
		goto err_free;
	}

	/* Allocate a minor in the FSI space */
	rc = __fsi_get_new_minor(slave, fsi_dev_cfam, &slave->dev.devt,
				 &slave->cdev_idx);
	if (rc)
		goto err_free;

	trace_fsi_slave_init(slave);

	/* Create chardev for userspace access */
	cdev_init(&slave->cdev, &cfam_fops);
	rc = cdev_device_add(&slave->cdev, &slave->dev);
	if (rc) {
		dev_err(&slave->dev, "Error %d creating slave device\n", rc);
		goto err_free_ida;
	}

	/* Now that we have the cdev registered with the core, any fatal
	 * failures beyond this point will need to clean up through
	 * cdev_device_del(). Fortunately though, nothing past here is fatal.
	 */

	if (master->link_config)
		master->link_config(master, link,
				    slave->t_send_delay,
				    slave->t_echo_delay);

	/* Legacy raw file -> to be removed */
	rc = device_create_bin_file(&slave->dev, &fsi_slave_raw_attr);
	if (rc)
		dev_warn(&slave->dev, "failed to create raw attr: %d\n", rc);


	rc = fsi_slave_scan(slave);
	if (rc)
		dev_dbg(&master->dev, "failed during slave scan with: %d\n",
				rc);

	return 0;

err_free_ida:
	fsi_free_minor(slave->dev.devt);
err_free:
	of_node_put(slave->dev.of_node);
	kfree(slave);
	return rc;
}

/* FSI master support */
static int fsi_check_access(uint32_t addr, size_t size)
{
	if (size == 4) {
		if (addr & 0x3)
			return -EINVAL;
	} else if (size == 2) {
		if (addr & 0x1)
			return -EINVAL;
	} else if (size != 1)
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

static int fsi_master_link_disable(struct fsi_master *master, int link)
{
	if (master->link_enable)
		return master->link_enable(master, link, false);

	return 0;
}

static int fsi_master_link_enable(struct fsi_master *master, int link)
{
	if (master->link_enable)
		return master->link_enable(master, link, true);

	return 0;
}

/*
 * Issue a break command on this link
 */
static int fsi_master_break(struct fsi_master *master, int link)
{
	int rc = 0;

	trace_fsi_master_break(master, link);

	if (master->send_break)
		rc = master->send_break(master, link);
	if (master->link_config)
		master->link_config(master, link, 16, 16);

	return rc;
}

static int fsi_master_scan(struct fsi_master *master)
{
	int link, rc;

	trace_fsi_master_scan(master, true);
	for (link = 0; link < master->n_links; link++) {
		rc = fsi_master_link_enable(master, link);
		if (rc) {
			dev_dbg(&master->dev,
				"enable link %d failed: %d\n", link, rc);
			continue;
		}
		rc = fsi_master_break(master, link);
		if (rc) {
			fsi_master_link_disable(master, link);
			dev_dbg(&master->dev,
				"break to link %d failed: %d\n", link, rc);
			continue;
		}

		rc = fsi_slave_init(master, link, 0);
		if (rc)
			fsi_master_link_disable(master, link);
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
	struct fsi_slave *slave = to_fsi_slave(dev);

	device_for_each_child(dev, NULL, fsi_slave_remove_device);
	cdev_device_del(&slave->cdev, &slave->dev);
	put_device(dev);
	return 0;
}

static void fsi_master_unscan(struct fsi_master *master)
{
	trace_fsi_master_scan(master, false);
	device_for_each_child(&master->dev, NULL, fsi_master_remove_slave);
}

int fsi_master_rescan(struct fsi_master *master)
{
	int rc;

	mutex_lock(&master->scan_lock);
	fsi_master_unscan(master);
	rc = fsi_master_scan(master);
	mutex_unlock(&master->scan_lock);

	return rc;
}
EXPORT_SYMBOL_GPL(fsi_master_rescan);

static ssize_t master_rescan_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct fsi_master *master = to_fsi_master(dev);
	int rc;

	rc = fsi_master_rescan(master);
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

static struct attribute *master_attrs[] = {
	&dev_attr_break.attr,
	&dev_attr_rescan.attr,
	NULL
};

ATTRIBUTE_GROUPS(master);

static struct class fsi_master_class = {
	.name = "fsi-master",
	.dev_groups = master_groups,
};

int fsi_master_register(struct fsi_master *master)
{
	int rc;
	struct device_node *np;

	mutex_init(&master->scan_lock);

	/* Alloc the requested index if it's non-zero */
	if (master->idx) {
		master->idx = ida_alloc_range(&master_ida, master->idx,
					      master->idx, GFP_KERNEL);
	} else {
		master->idx = ida_alloc(&master_ida, GFP_KERNEL);
	}

	if (master->idx < 0)
		return master->idx;

	if (!dev_name(&master->dev))
		dev_set_name(&master->dev, "fsi%d", master->idx);

	master->dev.class = &fsi_master_class;

	mutex_lock(&master->scan_lock);
	rc = device_register(&master->dev);
	if (rc) {
		ida_free(&master_ida, master->idx);
		goto out;
	}

	np = dev_of_node(&master->dev);
	if (!of_property_read_bool(np, "no-scan-on-init")) {
		fsi_master_scan(master);
	}
out:
	mutex_unlock(&master->scan_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(fsi_master_register);

void fsi_master_unregister(struct fsi_master *master)
{
	int idx = master->idx;

	trace_fsi_master_unregister(master);

	mutex_lock(&master->scan_lock);
	fsi_master_unscan(master);
	master->n_links = 0;
	mutex_unlock(&master->scan_lock);

	device_unregister(&master->dev);
	ida_free(&master_ida, idx);
}
EXPORT_SYMBOL_GPL(fsi_master_unregister);

/* FSI core & Linux bus type definitions */

static int fsi_bus_match(struct device *dev, const struct device_driver *drv)
{
	struct fsi_device *fsi_dev = to_fsi_dev(dev);
	const struct fsi_driver *fsi_drv = to_fsi_drv(drv);
	const struct fsi_device_id *id;

	if (!fsi_drv->id_table)
		return 0;

	for (id = fsi_drv->id_table; id->engine_type; id++) {
		if (id->engine_type != fsi_dev->engine_type)
			continue;
		if (id->version == FSI_VERSION_ANY ||
		    id->version == fsi_dev->version) {
			if (drv->of_match_table) {
				if (of_driver_match_device(dev, drv))
					return 1;
			} else {
				return 1;
			}
		}
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

const struct bus_type fsi_bus_type = {
	.name		= "fsi",
	.match		= fsi_bus_match,
};
EXPORT_SYMBOL_GPL(fsi_bus_type);

static int __init fsi_init(void)
{
	int rc;

	rc = alloc_chrdev_region(&fsi_base_dev, 0, FSI_CHAR_MAX_DEVICES, "fsi");
	if (rc)
		return rc;
	rc = bus_register(&fsi_bus_type);
	if (rc)
		goto fail_bus;

	rc = class_register(&fsi_master_class);
	if (rc)
		goto fail_class;

	return 0;

 fail_class:
	bus_unregister(&fsi_bus_type);
 fail_bus:
	unregister_chrdev_region(fsi_base_dev, FSI_CHAR_MAX_DEVICES);
	return rc;
}
postcore_initcall(fsi_init);

static void fsi_exit(void)
{
	class_unregister(&fsi_master_class);
	bus_unregister(&fsi_bus_type);
	unregister_chrdev_region(fsi_base_dev, FSI_CHAR_MAX_DEVICES);
	ida_destroy(&fsi_minor_ida);
}
module_exit(fsi_exit);
module_param(discard_errors, int, 0664);
MODULE_DESCRIPTION("FSI core driver");
MODULE_LICENSE("GPL");
MODULE_PARM_DESC(discard_errors, "Don't invoke error handling on bus accesses");
