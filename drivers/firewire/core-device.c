// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Device probing and sysfs code.
 *
 * Copyright (C) 2005-2006  Kristian Hoegsberg <krh@bitplanet.net>
 */

#include <linux/bug.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/idr.h>
#include <linux/jiffies.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include <linux/atomic.h>
#include <asm/byteorder.h>

#include "core.h"

void fw_csr_iterator_init(struct fw_csr_iterator *ci, const u32 *p)
{
	ci->p = p + 1;
	ci->end = ci->p + (p[0] >> 16);
}
EXPORT_SYMBOL(fw_csr_iterator_init);

int fw_csr_iterator_next(struct fw_csr_iterator *ci, int *key, int *value)
{
	*key = *ci->p >> 24;
	*value = *ci->p & 0xffffff;

	return ci->p++ < ci->end;
}
EXPORT_SYMBOL(fw_csr_iterator_next);

static const u32 *search_leaf(const u32 *directory, int search_key)
{
	struct fw_csr_iterator ci;
	int last_key = 0, key, value;

	fw_csr_iterator_init(&ci, directory);
	while (fw_csr_iterator_next(&ci, &key, &value)) {
		if (last_key == search_key &&
		    key == (CSR_DESCRIPTOR | CSR_LEAF))
			return ci.p - 1 + value;

		last_key = key;
	}

	return NULL;
}

static int textual_leaf_to_string(const u32 *block, char *buf, size_t size)
{
	unsigned int quadlets, i;
	char c;

	if (!size || !buf)
		return -EINVAL;

	quadlets = min(block[0] >> 16, 256U);
	if (quadlets < 2)
		return -ENODATA;

	if (block[1] != 0 || block[2] != 0)
		/* unknown language/character set */
		return -ENODATA;

	block += 3;
	quadlets -= 2;
	for (i = 0; i < quadlets * 4 && i < size - 1; i++) {
		c = block[i / 4] >> (24 - 8 * (i % 4));
		if (c == '\0')
			break;
		buf[i] = c;
	}
	buf[i] = '\0';

	return i;
}

/**
 * fw_csr_string() - reads a string from the configuration ROM
 * @directory:	e.g. root directory or unit directory
 * @key:	the key of the preceding directory entry
 * @buf:	where to put the string
 * @size:	size of @buf, in bytes
 *
 * The string is taken from a minimal ASCII text descriptor leaf after
 * the immediate entry with @key.  The string is zero-terminated.
 * An overlong string is silently truncated such that it and the
 * zero byte fit into @size.
 *
 * Returns strlen(buf) or a negative error code.
 */
int fw_csr_string(const u32 *directory, int key, char *buf, size_t size)
{
	const u32 *leaf = search_leaf(directory, key);
	if (!leaf)
		return -ENOENT;

	return textual_leaf_to_string(leaf, buf, size);
}
EXPORT_SYMBOL(fw_csr_string);

static void get_ids(const u32 *directory, int *id)
{
	struct fw_csr_iterator ci;
	int key, value;

	fw_csr_iterator_init(&ci, directory);
	while (fw_csr_iterator_next(&ci, &key, &value)) {
		switch (key) {
		case CSR_VENDOR:	id[0] = value; break;
		case CSR_MODEL:		id[1] = value; break;
		case CSR_SPECIFIER_ID:	id[2] = value; break;
		case CSR_VERSION:	id[3] = value; break;
		}
	}
}

static void get_modalias_ids(const struct fw_unit *unit, int *id)
{
	get_ids(&fw_parent_device(unit)->config_rom[5], id);
	get_ids(unit->directory, id);
}

static bool match_ids(const struct ieee1394_device_id *id_table, int *id)
{
	int match = 0;

	if (id[0] == id_table->vendor_id)
		match |= IEEE1394_MATCH_VENDOR_ID;
	if (id[1] == id_table->model_id)
		match |= IEEE1394_MATCH_MODEL_ID;
	if (id[2] == id_table->specifier_id)
		match |= IEEE1394_MATCH_SPECIFIER_ID;
	if (id[3] == id_table->version)
		match |= IEEE1394_MATCH_VERSION;

	return (match & id_table->match_flags) == id_table->match_flags;
}

static const struct ieee1394_device_id *unit_match(struct device *dev,
						   struct device_driver *drv)
{
	const struct ieee1394_device_id *id_table =
			container_of(drv, struct fw_driver, driver)->id_table;
	int id[] = {0, 0, 0, 0};

	get_modalias_ids(fw_unit(dev), id);

	for (; id_table->match_flags != 0; id_table++)
		if (match_ids(id_table, id))
			return id_table;

	return NULL;
}

static bool is_fw_unit(struct device *dev);

static int fw_unit_match(struct device *dev, struct device_driver *drv)
{
	/* We only allow binding to fw_units. */
	return is_fw_unit(dev) && unit_match(dev, drv) != NULL;
}

static int fw_unit_probe(struct device *dev)
{
	struct fw_driver *driver =
			container_of(dev->driver, struct fw_driver, driver);

	return driver->probe(fw_unit(dev), unit_match(dev, dev->driver));
}

static void fw_unit_remove(struct device *dev)
{
	struct fw_driver *driver =
			container_of(dev->driver, struct fw_driver, driver);

	driver->remove(fw_unit(dev));
}

static int get_modalias(const struct fw_unit *unit, char *buffer, size_t buffer_size)
{
	int id[] = {0, 0, 0, 0};

	get_modalias_ids(unit, id);

	return snprintf(buffer, buffer_size,
			"ieee1394:ven%08Xmo%08Xsp%08Xver%08X",
			id[0], id[1], id[2], id[3]);
}

static int fw_unit_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct fw_unit *unit = fw_unit(dev);
	char modalias[64];

	get_modalias(unit, modalias, sizeof(modalias));

	if (add_uevent_var(env, "MODALIAS=%s", modalias))
		return -ENOMEM;

	return 0;
}

struct bus_type fw_bus_type = {
	.name = "firewire",
	.match = fw_unit_match,
	.probe = fw_unit_probe,
	.remove = fw_unit_remove,
};
EXPORT_SYMBOL(fw_bus_type);

int fw_device_enable_phys_dma(struct fw_device *device)
{
	int generation = device->generation;

	/* device->node_id, accessed below, must not be older than generation */
	smp_rmb();

	return device->card->driver->enable_phys_dma(device->card,
						     device->node_id,
						     generation);
}
EXPORT_SYMBOL(fw_device_enable_phys_dma);

struct config_rom_attribute {
	struct device_attribute attr;
	u32 key;
};

static ssize_t show_immediate(struct device *dev,
			      struct device_attribute *dattr, char *buf)
{
	struct config_rom_attribute *attr =
		container_of(dattr, struct config_rom_attribute, attr);
	struct fw_csr_iterator ci;
	const u32 *dir;
	int key, value, ret = -ENOENT;

	down_read(&fw_device_rwsem);

	if (is_fw_unit(dev))
		dir = fw_unit(dev)->directory;
	else
		dir = fw_device(dev)->config_rom + 5;

	fw_csr_iterator_init(&ci, dir);
	while (fw_csr_iterator_next(&ci, &key, &value))
		if (attr->key == key) {
			ret = snprintf(buf, buf ? PAGE_SIZE : 0,
				       "0x%06x\n", value);
			break;
		}

	up_read(&fw_device_rwsem);

	return ret;
}

#define IMMEDIATE_ATTR(name, key)				\
	{ __ATTR(name, S_IRUGO, show_immediate, NULL), key }

static ssize_t show_text_leaf(struct device *dev,
			      struct device_attribute *dattr, char *buf)
{
	struct config_rom_attribute *attr =
		container_of(dattr, struct config_rom_attribute, attr);
	const u32 *dir;
	size_t bufsize;
	char dummy_buf[2];
	int ret;

	down_read(&fw_device_rwsem);

	if (is_fw_unit(dev))
		dir = fw_unit(dev)->directory;
	else
		dir = fw_device(dev)->config_rom + 5;

	if (buf) {
		bufsize = PAGE_SIZE - 1;
	} else {
		buf = dummy_buf;
		bufsize = 1;
	}

	ret = fw_csr_string(dir, attr->key, buf, bufsize);

	if (ret >= 0) {
		/* Strip trailing whitespace and add newline. */
		while (ret > 0 && isspace(buf[ret - 1]))
			ret--;
		strcpy(buf + ret, "\n");
		ret++;
	}

	up_read(&fw_device_rwsem);

	return ret;
}

#define TEXT_LEAF_ATTR(name, key)				\
	{ __ATTR(name, S_IRUGO, show_text_leaf, NULL), key }

static struct config_rom_attribute config_rom_attributes[] = {
	IMMEDIATE_ATTR(vendor, CSR_VENDOR),
	IMMEDIATE_ATTR(hardware_version, CSR_HARDWARE_VERSION),
	IMMEDIATE_ATTR(specifier_id, CSR_SPECIFIER_ID),
	IMMEDIATE_ATTR(version, CSR_VERSION),
	IMMEDIATE_ATTR(model, CSR_MODEL),
	TEXT_LEAF_ATTR(vendor_name, CSR_VENDOR),
	TEXT_LEAF_ATTR(model_name, CSR_MODEL),
	TEXT_LEAF_ATTR(hardware_version_name, CSR_HARDWARE_VERSION),
};

static void init_fw_attribute_group(struct device *dev,
				    struct device_attribute *attrs,
				    struct fw_attribute_group *group)
{
	struct device_attribute *attr;
	int i, j;

	for (j = 0; attrs[j].attr.name != NULL; j++)
		group->attrs[j] = &attrs[j].attr;

	for (i = 0; i < ARRAY_SIZE(config_rom_attributes); i++) {
		attr = &config_rom_attributes[i].attr;
		if (attr->show(dev, attr, NULL) < 0)
			continue;
		group->attrs[j++] = &attr->attr;
	}

	group->attrs[j] = NULL;
	group->groups[0] = &group->group;
	group->groups[1] = NULL;
	group->group.attrs = group->attrs;
	dev->groups = (const struct attribute_group **) group->groups;
}

static ssize_t modalias_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct fw_unit *unit = fw_unit(dev);
	int length;

	length = get_modalias(unit, buf, PAGE_SIZE);
	strcpy(buf + length, "\n");

	return length + 1;
}

static ssize_t rom_index_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct fw_device *device = fw_device(dev->parent);
	struct fw_unit *unit = fw_unit(dev);

	return sysfs_emit(buf, "%td\n", unit->directory - device->config_rom);
}

static struct device_attribute fw_unit_attributes[] = {
	__ATTR_RO(modalias),
	__ATTR_RO(rom_index),
	__ATTR_NULL,
};

static ssize_t config_rom_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct fw_device *device = fw_device(dev);
	size_t length;

	down_read(&fw_device_rwsem);
	length = device->config_rom_length * 4;
	memcpy(buf, device->config_rom, length);
	up_read(&fw_device_rwsem);

	return length;
}

static ssize_t guid_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct fw_device *device = fw_device(dev);
	int ret;

	down_read(&fw_device_rwsem);
	ret = sysfs_emit(buf, "0x%08x%08x\n", device->config_rom[3], device->config_rom[4]);
	up_read(&fw_device_rwsem);

	return ret;
}

static ssize_t is_local_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct fw_device *device = fw_device(dev);

	return sprintf(buf, "%u\n", device->is_local);
}

static int units_sprintf(char *buf, const u32 *directory)
{
	struct fw_csr_iterator ci;
	int key, value;
	int specifier_id = 0;
	int version = 0;

	fw_csr_iterator_init(&ci, directory);
	while (fw_csr_iterator_next(&ci, &key, &value)) {
		switch (key) {
		case CSR_SPECIFIER_ID:
			specifier_id = value;
			break;
		case CSR_VERSION:
			version = value;
			break;
		}
	}

	return sprintf(buf, "0x%06x:0x%06x ", specifier_id, version);
}

static ssize_t units_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct fw_device *device = fw_device(dev);
	struct fw_csr_iterator ci;
	int key, value, i = 0;

	down_read(&fw_device_rwsem);
	fw_csr_iterator_init(&ci, &device->config_rom[5]);
	while (fw_csr_iterator_next(&ci, &key, &value)) {
		if (key != (CSR_UNIT | CSR_DIRECTORY))
			continue;
		i += units_sprintf(&buf[i], ci.p + value - 1);
		if (i >= PAGE_SIZE - (8 + 1 + 8 + 1))
			break;
	}
	up_read(&fw_device_rwsem);

	if (i)
		buf[i - 1] = '\n';

	return i;
}

static struct device_attribute fw_device_attributes[] = {
	__ATTR_RO(config_rom),
	__ATTR_RO(guid),
	__ATTR_RO(is_local),
	__ATTR_RO(units),
	__ATTR_NULL,
};

static int read_rom(struct fw_device *device,
		    int generation, int index, u32 *data)
{
	u64 offset = (CSR_REGISTER_BASE | CSR_CONFIG_ROM) + index * 4;
	int i, rcode;

	/* device->node_id, accessed below, must not be older than generation */
	smp_rmb();

	for (i = 10; i < 100; i += 10) {
		rcode = fw_run_transaction(device->card,
				TCODE_READ_QUADLET_REQUEST, device->node_id,
				generation, device->max_speed, offset, data, 4);
		if (rcode != RCODE_BUSY)
			break;
		msleep(i);
	}
	be32_to_cpus(data);

	return rcode;
}

#define MAX_CONFIG_ROM_SIZE 256

/*
 * Read the bus info block, perform a speed probe, and read all of the rest of
 * the config ROM.  We do all this with a cached bus generation.  If the bus
 * generation changes under us, read_config_rom will fail and get retried.
 * It's better to start all over in this case because the node from which we
 * are reading the ROM may have changed the ROM during the reset.
 * Returns either a result code or a negative error code.
 */
static int read_config_rom(struct fw_device *device, int generation)
{
	struct fw_card *card = device->card;
	const u32 *old_rom, *new_rom;
	u32 *rom, *stack;
	u32 sp, key;
	int i, end, length, ret;

	rom = kmalloc(sizeof(*rom) * MAX_CONFIG_ROM_SIZE +
		      sizeof(*stack) * MAX_CONFIG_ROM_SIZE, GFP_KERNEL);
	if (rom == NULL)
		return -ENOMEM;

	stack = &rom[MAX_CONFIG_ROM_SIZE];
	memset(rom, 0, sizeof(*rom) * MAX_CONFIG_ROM_SIZE);

	device->max_speed = SCODE_100;

	/* First read the bus info block. */
	for (i = 0; i < 5; i++) {
		ret = read_rom(device, generation, i, &rom[i]);
		if (ret != RCODE_COMPLETE)
			goto out;
		/*
		 * As per IEEE1212 7.2, during initialization, devices can
		 * reply with a 0 for the first quadlet of the config
		 * rom to indicate that they are booting (for example,
		 * if the firmware is on the disk of a external
		 * harddisk).  In that case we just fail, and the
		 * retry mechanism will try again later.
		 */
		if (i == 0 && rom[i] == 0) {
			ret = RCODE_BUSY;
			goto out;
		}
	}

	device->max_speed = device->node->max_speed;

	/*
	 * Determine the speed of
	 *   - devices with link speed less than PHY speed,
	 *   - devices with 1394b PHY (unless only connected to 1394a PHYs),
	 *   - all devices if there are 1394b repeaters.
	 * Note, we cannot use the bus info block's link_spd as starting point
	 * because some buggy firmwares set it lower than necessary and because
	 * 1394-1995 nodes do not have the field.
	 */
	if ((rom[2] & 0x7) < device->max_speed ||
	    device->max_speed == SCODE_BETA ||
	    card->beta_repeaters_present) {
		u32 dummy;

		/* for S1600 and S3200 */
		if (device->max_speed == SCODE_BETA)
			device->max_speed = card->link_speed;

		while (device->max_speed > SCODE_100) {
			if (read_rom(device, generation, 0, &dummy) ==
			    RCODE_COMPLETE)
				break;
			device->max_speed--;
		}
	}

	/*
	 * Now parse the config rom.  The config rom is a recursive
	 * directory structure so we parse it using a stack of
	 * references to the blocks that make up the structure.  We
	 * push a reference to the root directory on the stack to
	 * start things off.
	 */
	length = i;
	sp = 0;
	stack[sp++] = 0xc0000005;
	while (sp > 0) {
		/*
		 * Pop the next block reference of the stack.  The
		 * lower 24 bits is the offset into the config rom,
		 * the upper 8 bits are the type of the reference the
		 * block.
		 */
		key = stack[--sp];
		i = key & 0xffffff;
		if (WARN_ON(i >= MAX_CONFIG_ROM_SIZE)) {
			ret = -ENXIO;
			goto out;
		}

		/* Read header quadlet for the block to get the length. */
		ret = read_rom(device, generation, i, &rom[i]);
		if (ret != RCODE_COMPLETE)
			goto out;
		end = i + (rom[i] >> 16) + 1;
		if (end > MAX_CONFIG_ROM_SIZE) {
			/*
			 * This block extends outside the config ROM which is
			 * a firmware bug.  Ignore this whole block, i.e.
			 * simply set a fake block length of 0.
			 */
			fw_err(card, "skipped invalid ROM block %x at %llx\n",
			       rom[i],
			       i * 4 | CSR_REGISTER_BASE | CSR_CONFIG_ROM);
			rom[i] = 0;
			end = i;
		}
		i++;

		/*
		 * Now read in the block.  If this is a directory
		 * block, check the entries as we read them to see if
		 * it references another block, and push it in that case.
		 */
		for (; i < end; i++) {
			ret = read_rom(device, generation, i, &rom[i]);
			if (ret != RCODE_COMPLETE)
				goto out;

			if ((key >> 30) != 3 || (rom[i] >> 30) < 2)
				continue;
			/*
			 * Offset points outside the ROM.  May be a firmware
			 * bug or an Extended ROM entry (IEEE 1212-2001 clause
			 * 7.7.18).  Simply overwrite this pointer here by a
			 * fake immediate entry so that later iterators over
			 * the ROM don't have to check offsets all the time.
			 */
			if (i + (rom[i] & 0xffffff) >= MAX_CONFIG_ROM_SIZE) {
				fw_err(card,
				       "skipped unsupported ROM entry %x at %llx\n",
				       rom[i],
				       i * 4 | CSR_REGISTER_BASE | CSR_CONFIG_ROM);
				rom[i] = 0;
				continue;
			}
			stack[sp++] = i + rom[i];
		}
		if (length < i)
			length = i;
	}

	old_rom = device->config_rom;
	new_rom = kmemdup(rom, length * 4, GFP_KERNEL);
	if (new_rom == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	down_write(&fw_device_rwsem);
	device->config_rom = new_rom;
	device->config_rom_length = length;
	up_write(&fw_device_rwsem);

	kfree(old_rom);
	ret = RCODE_COMPLETE;
	device->max_rec	= rom[2] >> 12 & 0xf;
	device->cmc	= rom[2] >> 30 & 1;
	device->irmc	= rom[2] >> 31 & 1;
 out:
	kfree(rom);

	return ret;
}

static void fw_unit_release(struct device *dev)
{
	struct fw_unit *unit = fw_unit(dev);

	fw_device_put(fw_parent_device(unit));
	kfree(unit);
}

static struct device_type fw_unit_type = {
	.uevent		= fw_unit_uevent,
	.release	= fw_unit_release,
};

static bool is_fw_unit(struct device *dev)
{
	return dev->type == &fw_unit_type;
}

static void create_units(struct fw_device *device)
{
	struct fw_csr_iterator ci;
	struct fw_unit *unit;
	int key, value, i;

	i = 0;
	fw_csr_iterator_init(&ci, &device->config_rom[5]);
	while (fw_csr_iterator_next(&ci, &key, &value)) {
		if (key != (CSR_UNIT | CSR_DIRECTORY))
			continue;

		/*
		 * Get the address of the unit directory and try to
		 * match the drivers id_tables against it.
		 */
		unit = kzalloc(sizeof(*unit), GFP_KERNEL);
		if (unit == NULL)
			continue;

		unit->directory = ci.p + value - 1;
		unit->device.bus = &fw_bus_type;
		unit->device.type = &fw_unit_type;
		unit->device.parent = &device->device;
		dev_set_name(&unit->device, "%s.%d", dev_name(&device->device), i++);

		BUILD_BUG_ON(ARRAY_SIZE(unit->attribute_group.attrs) <
				ARRAY_SIZE(fw_unit_attributes) +
				ARRAY_SIZE(config_rom_attributes));
		init_fw_attribute_group(&unit->device,
					fw_unit_attributes,
					&unit->attribute_group);

		if (device_register(&unit->device) < 0)
			goto skip_unit;

		fw_device_get(device);
		continue;

	skip_unit:
		kfree(unit);
	}
}

static int shutdown_unit(struct device *device, void *data)
{
	device_unregister(device);

	return 0;
}

/*
 * fw_device_rwsem acts as dual purpose mutex:
 *   - serializes accesses to fw_device_idr,
 *   - serializes accesses to fw_device.config_rom/.config_rom_length and
 *     fw_unit.directory, unless those accesses happen at safe occasions
 */
DECLARE_RWSEM(fw_device_rwsem);

DEFINE_IDR(fw_device_idr);
int fw_cdev_major;

struct fw_device *fw_device_get_by_devt(dev_t devt)
{
	struct fw_device *device;

	down_read(&fw_device_rwsem);
	device = idr_find(&fw_device_idr, MINOR(devt));
	if (device)
		fw_device_get(device);
	up_read(&fw_device_rwsem);

	return device;
}

struct workqueue_struct *fw_workqueue;
EXPORT_SYMBOL(fw_workqueue);

static void fw_schedule_device_work(struct fw_device *device,
				    unsigned long delay)
{
	queue_delayed_work(fw_workqueue, &device->work, delay);
}

/*
 * These defines control the retry behavior for reading the config
 * rom.  It shouldn't be necessary to tweak these; if the device
 * doesn't respond to a config rom read within 10 seconds, it's not
 * going to respond at all.  As for the initial delay, a lot of
 * devices will be able to respond within half a second after bus
 * reset.  On the other hand, it's not really worth being more
 * aggressive than that, since it scales pretty well; if 10 devices
 * are plugged in, they're all getting read within one second.
 */

#define MAX_RETRIES	10
#define RETRY_DELAY	(3 * HZ)
#define INITIAL_DELAY	(HZ / 2)
#define SHUTDOWN_DELAY	(2 * HZ)

static void fw_device_shutdown(struct work_struct *work)
{
	struct fw_device *device =
		container_of(work, struct fw_device, work.work);
	int minor = MINOR(device->device.devt);

	if (time_before64(get_jiffies_64(),
			  device->card->reset_jiffies + SHUTDOWN_DELAY)
	    && !list_empty(&device->card->link)) {
		fw_schedule_device_work(device, SHUTDOWN_DELAY);
		return;
	}

	if (atomic_cmpxchg(&device->state,
			   FW_DEVICE_GONE,
			   FW_DEVICE_SHUTDOWN) != FW_DEVICE_GONE)
		return;

	fw_device_cdev_remove(device);
	device_for_each_child(&device->device, NULL, shutdown_unit);
	device_unregister(&device->device);

	down_write(&fw_device_rwsem);
	idr_remove(&fw_device_idr, minor);
	up_write(&fw_device_rwsem);

	fw_device_put(device);
}

static void fw_device_release(struct device *dev)
{
	struct fw_device *device = fw_device(dev);
	struct fw_card *card = device->card;
	unsigned long flags;

	/*
	 * Take the card lock so we don't set this to NULL while a
	 * FW_NODE_UPDATED callback is being handled or while the
	 * bus manager work looks at this node.
	 */
	spin_lock_irqsave(&card->lock, flags);
	device->node->data = NULL;
	spin_unlock_irqrestore(&card->lock, flags);

	fw_node_put(device->node);
	kfree(device->config_rom);
	kfree(device);
	fw_card_put(card);
}

static struct device_type fw_device_type = {
	.release = fw_device_release,
};

static bool is_fw_device(struct device *dev)
{
	return dev->type == &fw_device_type;
}

static int update_unit(struct device *dev, void *data)
{
	struct fw_unit *unit = fw_unit(dev);
	struct fw_driver *driver = (struct fw_driver *)dev->driver;

	if (is_fw_unit(dev) && driver != NULL && driver->update != NULL) {
		device_lock(dev);
		driver->update(unit);
		device_unlock(dev);
	}

	return 0;
}

static void fw_device_update(struct work_struct *work)
{
	struct fw_device *device =
		container_of(work, struct fw_device, work.work);

	fw_device_cdev_update(device);
	device_for_each_child(&device->device, NULL, update_unit);
}

/*
 * If a device was pending for deletion because its node went away but its
 * bus info block and root directory header matches that of a newly discovered
 * device, revive the existing fw_device.
 * The newly allocated fw_device becomes obsolete instead.
 */
static int lookup_existing_device(struct device *dev, void *data)
{
	struct fw_device *old = fw_device(dev);
	struct fw_device *new = data;
	struct fw_card *card = new->card;
	int match = 0;

	if (!is_fw_device(dev))
		return 0;

	down_read(&fw_device_rwsem); /* serialize config_rom access */
	spin_lock_irq(&card->lock);  /* serialize node access */

	if (memcmp(old->config_rom, new->config_rom, 6 * 4) == 0 &&
	    atomic_cmpxchg(&old->state,
			   FW_DEVICE_GONE,
			   FW_DEVICE_RUNNING) == FW_DEVICE_GONE) {
		struct fw_node *current_node = new->node;
		struct fw_node *obsolete_node = old->node;

		new->node = obsolete_node;
		new->node->data = new;
		old->node = current_node;
		old->node->data = old;

		old->max_speed = new->max_speed;
		old->node_id = current_node->node_id;
		smp_wmb();  /* update node_id before generation */
		old->generation = card->generation;
		old->config_rom_retries = 0;
		fw_notice(card, "rediscovered device %s\n", dev_name(dev));

		old->workfn = fw_device_update;
		fw_schedule_device_work(old, 0);

		if (current_node == card->root_node)
			fw_schedule_bm_work(card, 0);

		match = 1;
	}

	spin_unlock_irq(&card->lock);
	up_read(&fw_device_rwsem);

	return match;
}

enum { BC_UNKNOWN = 0, BC_UNIMPLEMENTED, BC_IMPLEMENTED, };

static void set_broadcast_channel(struct fw_device *device, int generation)
{
	struct fw_card *card = device->card;
	__be32 data;
	int rcode;

	if (!card->broadcast_channel_allocated)
		return;

	/*
	 * The Broadcast_Channel Valid bit is required by nodes which want to
	 * transmit on this channel.  Such transmissions are practically
	 * exclusive to IP over 1394 (RFC 2734).  IP capable nodes are required
	 * to be IRM capable and have a max_rec of 8 or more.  We use this fact
	 * to narrow down to which nodes we send Broadcast_Channel updates.
	 */
	if (!device->irmc || device->max_rec < 8)
		return;

	/*
	 * Some 1394-1995 nodes crash if this 1394a-2000 register is written.
	 * Perform a read test first.
	 */
	if (device->bc_implemented == BC_UNKNOWN) {
		rcode = fw_run_transaction(card, TCODE_READ_QUADLET_REQUEST,
				device->node_id, generation, device->max_speed,
				CSR_REGISTER_BASE + CSR_BROADCAST_CHANNEL,
				&data, 4);
		switch (rcode) {
		case RCODE_COMPLETE:
			if (data & cpu_to_be32(1 << 31)) {
				device->bc_implemented = BC_IMPLEMENTED;
				break;
			}
			fallthrough;	/* to case address error */
		case RCODE_ADDRESS_ERROR:
			device->bc_implemented = BC_UNIMPLEMENTED;
		}
	}

	if (device->bc_implemented == BC_IMPLEMENTED) {
		data = cpu_to_be32(BROADCAST_CHANNEL_INITIAL |
				   BROADCAST_CHANNEL_VALID);
		fw_run_transaction(card, TCODE_WRITE_QUADLET_REQUEST,
				device->node_id, generation, device->max_speed,
				CSR_REGISTER_BASE + CSR_BROADCAST_CHANNEL,
				&data, 4);
	}
}

int fw_device_set_broadcast_channel(struct device *dev, void *gen)
{
	if (is_fw_device(dev))
		set_broadcast_channel(fw_device(dev), (long)gen);

	return 0;
}

static void fw_device_init(struct work_struct *work)
{
	struct fw_device *device =
		container_of(work, struct fw_device, work.work);
	struct fw_card *card = device->card;
	struct device *revived_dev;
	int minor, ret;

	/*
	 * All failure paths here set node->data to NULL, so that we
	 * don't try to do device_for_each_child() on a kfree()'d
	 * device.
	 */

	ret = read_config_rom(device, device->generation);
	if (ret != RCODE_COMPLETE) {
		if (device->config_rom_retries < MAX_RETRIES &&
		    atomic_read(&device->state) == FW_DEVICE_INITIALIZING) {
			device->config_rom_retries++;
			fw_schedule_device_work(device, RETRY_DELAY);
		} else {
			if (device->node->link_on)
				fw_notice(card, "giving up on node %x: reading config rom failed: %s\n",
					  device->node_id,
					  fw_rcode_string(ret));
			if (device->node == card->root_node)
				fw_schedule_bm_work(card, 0);
			fw_device_release(&device->device);
		}
		return;
	}

	revived_dev = device_find_child(card->device,
					device, lookup_existing_device);
	if (revived_dev) {
		put_device(revived_dev);
		fw_device_release(&device->device);

		return;
	}

	device_initialize(&device->device);

	fw_device_get(device);
	down_write(&fw_device_rwsem);
	minor = idr_alloc(&fw_device_idr, device, 0, 1 << MINORBITS,
			GFP_KERNEL);
	up_write(&fw_device_rwsem);

	if (minor < 0)
		goto error;

	device->device.bus = &fw_bus_type;
	device->device.type = &fw_device_type;
	device->device.parent = card->device;
	device->device.devt = MKDEV(fw_cdev_major, minor);
	dev_set_name(&device->device, "fw%d", minor);

	BUILD_BUG_ON(ARRAY_SIZE(device->attribute_group.attrs) <
			ARRAY_SIZE(fw_device_attributes) +
			ARRAY_SIZE(config_rom_attributes));
	init_fw_attribute_group(&device->device,
				fw_device_attributes,
				&device->attribute_group);

	if (device_add(&device->device)) {
		fw_err(card, "failed to add device\n");
		goto error_with_cdev;
	}

	create_units(device);

	/*
	 * Transition the device to running state.  If it got pulled
	 * out from under us while we did the initialization work, we
	 * have to shut down the device again here.  Normally, though,
	 * fw_node_event will be responsible for shutting it down when
	 * necessary.  We have to use the atomic cmpxchg here to avoid
	 * racing with the FW_NODE_DESTROYED case in
	 * fw_node_event().
	 */
	if (atomic_cmpxchg(&device->state,
			   FW_DEVICE_INITIALIZING,
			   FW_DEVICE_RUNNING) == FW_DEVICE_GONE) {
		device->workfn = fw_device_shutdown;
		fw_schedule_device_work(device, SHUTDOWN_DELAY);
	} else {
		fw_notice(card, "created device %s: GUID %08x%08x, S%d00\n",
			  dev_name(&device->device),
			  device->config_rom[3], device->config_rom[4],
			  1 << device->max_speed);
		device->config_rom_retries = 0;

		set_broadcast_channel(device, device->generation);

		add_device_randomness(&device->config_rom[3], 8);
	}

	/*
	 * Reschedule the IRM work if we just finished reading the
	 * root node config rom.  If this races with a bus reset we
	 * just end up running the IRM work a couple of extra times -
	 * pretty harmless.
	 */
	if (device->node == card->root_node)
		fw_schedule_bm_work(card, 0);

	return;

 error_with_cdev:
	down_write(&fw_device_rwsem);
	idr_remove(&fw_device_idr, minor);
	up_write(&fw_device_rwsem);
 error:
	fw_device_put(device);		/* fw_device_idr's reference */

	put_device(&device->device);	/* our reference */
}

/* Reread and compare bus info block and header of root directory */
static int reread_config_rom(struct fw_device *device, int generation,
			     bool *changed)
{
	u32 q;
	int i, rcode;

	for (i = 0; i < 6; i++) {
		rcode = read_rom(device, generation, i, &q);
		if (rcode != RCODE_COMPLETE)
			return rcode;

		if (i == 0 && q == 0)
			/* inaccessible (see read_config_rom); retry later */
			return RCODE_BUSY;

		if (q != device->config_rom[i]) {
			*changed = true;
			return RCODE_COMPLETE;
		}
	}

	*changed = false;
	return RCODE_COMPLETE;
}

static void fw_device_refresh(struct work_struct *work)
{
	struct fw_device *device =
		container_of(work, struct fw_device, work.work);
	struct fw_card *card = device->card;
	int ret, node_id = device->node_id;
	bool changed;

	ret = reread_config_rom(device, device->generation, &changed);
	if (ret != RCODE_COMPLETE)
		goto failed_config_rom;

	if (!changed) {
		if (atomic_cmpxchg(&device->state,
				   FW_DEVICE_INITIALIZING,
				   FW_DEVICE_RUNNING) == FW_DEVICE_GONE)
			goto gone;

		fw_device_update(work);
		device->config_rom_retries = 0;
		goto out;
	}

	/*
	 * Something changed.  We keep things simple and don't investigate
	 * further.  We just destroy all previous units and create new ones.
	 */
	device_for_each_child(&device->device, NULL, shutdown_unit);

	ret = read_config_rom(device, device->generation);
	if (ret != RCODE_COMPLETE)
		goto failed_config_rom;

	fw_device_cdev_update(device);
	create_units(device);

	/* Userspace may want to re-read attributes. */
	kobject_uevent(&device->device.kobj, KOBJ_CHANGE);

	if (atomic_cmpxchg(&device->state,
			   FW_DEVICE_INITIALIZING,
			   FW_DEVICE_RUNNING) == FW_DEVICE_GONE)
		goto gone;

	fw_notice(card, "refreshed device %s\n", dev_name(&device->device));
	device->config_rom_retries = 0;
	goto out;

 failed_config_rom:
	if (device->config_rom_retries < MAX_RETRIES &&
	    atomic_read(&device->state) == FW_DEVICE_INITIALIZING) {
		device->config_rom_retries++;
		fw_schedule_device_work(device, RETRY_DELAY);
		return;
	}

	fw_notice(card, "giving up on refresh of device %s: %s\n",
		  dev_name(&device->device), fw_rcode_string(ret));
 gone:
	atomic_set(&device->state, FW_DEVICE_GONE);
	device->workfn = fw_device_shutdown;
	fw_schedule_device_work(device, SHUTDOWN_DELAY);
 out:
	if (node_id == card->root_node->node_id)
		fw_schedule_bm_work(card, 0);
}

static void fw_device_workfn(struct work_struct *work)
{
	struct fw_device *device = container_of(to_delayed_work(work),
						struct fw_device, work);
	device->workfn(work);
}

void fw_node_event(struct fw_card *card, struct fw_node *node, int event)
{
	struct fw_device *device;

	switch (event) {
	case FW_NODE_CREATED:
		/*
		 * Attempt to scan the node, regardless whether its self ID has
		 * the L (link active) flag set or not.  Some broken devices
		 * send L=0 but have an up-and-running link; others send L=1
		 * without actually having a link.
		 */
 create:
		device = kzalloc(sizeof(*device), GFP_ATOMIC);
		if (device == NULL)
			break;

		/*
		 * Do minimal initialization of the device here, the
		 * rest will happen in fw_device_init().
		 *
		 * Attention:  A lot of things, even fw_device_get(),
		 * cannot be done before fw_device_init() finished!
		 * You can basically just check device->state and
		 * schedule work until then, but only while holding
		 * card->lock.
		 */
		atomic_set(&device->state, FW_DEVICE_INITIALIZING);
		device->card = fw_card_get(card);
		device->node = fw_node_get(node);
		device->node_id = node->node_id;
		device->generation = card->generation;
		device->is_local = node == card->local_node;
		mutex_init(&device->client_list_mutex);
		INIT_LIST_HEAD(&device->client_list);

		/*
		 * Set the node data to point back to this device so
		 * FW_NODE_UPDATED callbacks can update the node_id
		 * and generation for the device.
		 */
		node->data = device;

		/*
		 * Many devices are slow to respond after bus resets,
		 * especially if they are bus powered and go through
		 * power-up after getting plugged in.  We schedule the
		 * first config rom scan half a second after bus reset.
		 */
		device->workfn = fw_device_init;
		INIT_DELAYED_WORK(&device->work, fw_device_workfn);
		fw_schedule_device_work(device, INITIAL_DELAY);
		break;

	case FW_NODE_INITIATED_RESET:
	case FW_NODE_LINK_ON:
		device = node->data;
		if (device == NULL)
			goto create;

		device->node_id = node->node_id;
		smp_wmb();  /* update node_id before generation */
		device->generation = card->generation;
		if (atomic_cmpxchg(&device->state,
			    FW_DEVICE_RUNNING,
			    FW_DEVICE_INITIALIZING) == FW_DEVICE_RUNNING) {
			device->workfn = fw_device_refresh;
			fw_schedule_device_work(device,
				device->is_local ? 0 : INITIAL_DELAY);
		}
		break;

	case FW_NODE_UPDATED:
		device = node->data;
		if (device == NULL)
			break;

		device->node_id = node->node_id;
		smp_wmb();  /* update node_id before generation */
		device->generation = card->generation;
		if (atomic_read(&device->state) == FW_DEVICE_RUNNING) {
			device->workfn = fw_device_update;
			fw_schedule_device_work(device, 0);
		}
		break;

	case FW_NODE_DESTROYED:
	case FW_NODE_LINK_OFF:
		if (!node->data)
			break;

		/*
		 * Destroy the device associated with the node.  There
		 * are two cases here: either the device is fully
		 * initialized (FW_DEVICE_RUNNING) or we're in the
		 * process of reading its config rom
		 * (FW_DEVICE_INITIALIZING).  If it is fully
		 * initialized we can reuse device->work to schedule a
		 * full fw_device_shutdown().  If not, there's work
		 * scheduled to read it's config rom, and we just put
		 * the device in shutdown state to have that code fail
		 * to create the device.
		 */
		device = node->data;
		if (atomic_xchg(&device->state,
				FW_DEVICE_GONE) == FW_DEVICE_RUNNING) {
			device->workfn = fw_device_shutdown;
			fw_schedule_device_work(device,
				list_empty(&card->link) ? 0 : SHUTDOWN_DELAY);
		}
		break;
	}
}
