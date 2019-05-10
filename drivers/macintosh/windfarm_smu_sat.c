/*
 * Windfarm PowerMac thermal control.  SMU "satellite" controller sensors.
 *
 * Copyright (C) 2005 Paul Mackerras, IBM Corp. <paulus@samba.org>
 *
 * Released under the terms of the GNU GPL v2.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <asm/prom.h>
#include <asm/smu.h>
#include <asm/pmac_low_i2c.h>

#include "windfarm.h"

#define VERSION "1.0"

/* If the cache is older than 800ms we'll refetch it */
#define MAX_AGE		msecs_to_jiffies(800)

struct wf_sat {
	struct kref		ref;
	int			nr;
	struct mutex		mutex;
	unsigned long		last_read; /* jiffies when cache last updated */
	u8			cache[16];
	struct list_head	sensors;
	struct i2c_client	*i2c;
	struct device_node	*node;
};

static struct wf_sat *sats[2];

struct wf_sat_sensor {
	struct list_head	link;
	int			index;
	int			index2;		/* used for power sensors */
	int			shift;
	struct wf_sat		*sat;
	struct wf_sensor 	sens;
};

#define wf_to_sat(c)	container_of(c, struct wf_sat_sensor, sens)

struct smu_sdbp_header *smu_sat_get_sdb_partition(unsigned int sat_id, int id,
						  unsigned int *size)
{
	struct wf_sat *sat;
	int err;
	unsigned int i, len;
	u8 *buf;
	u8 data[4];

	/* TODO: Add the resulting partition to the device-tree */

	if (sat_id > 1 || (sat = sats[sat_id]) == NULL)
		return NULL;

	err = i2c_smbus_write_word_data(sat->i2c, 8, id << 8);
	if (err) {
		printk(KERN_ERR "smu_sat_get_sdb_part wr error %d\n", err);
		return NULL;
	}

	err = i2c_smbus_read_word_data(sat->i2c, 9);
	if (err < 0) {
		printk(KERN_ERR "smu_sat_get_sdb_part rd len error\n");
		return NULL;
	}
	len = err;
	if (len == 0) {
		printk(KERN_ERR "smu_sat_get_sdb_part no partition %x\n", id);
		return NULL;
	}

	len = le16_to_cpu(len);
	len = (len + 3) & ~3;
	buf = kmalloc(len, GFP_KERNEL);
	if (buf == NULL)
		return NULL;

	for (i = 0; i < len; i += 4) {
		err = i2c_smbus_read_i2c_block_data(sat->i2c, 0xa, 4, data);
		if (err < 0) {
			printk(KERN_ERR "smu_sat_get_sdb_part rd err %d\n",
			       err);
			goto fail;
		}
		buf[i] = data[1];
		buf[i+1] = data[0];
		buf[i+2] = data[3];
		buf[i+3] = data[2];
	}

	printk(KERN_DEBUG "sat %d partition %x:", sat_id, id);
	print_hex_dump(KERN_DEBUG, "  ", DUMP_PREFIX_OFFSET,
		       16, 1, buf, len, false);
	if (size)
		*size = len;
	return (struct smu_sdbp_header *) buf;

 fail:
	kfree(buf);
	return NULL;
}
EXPORT_SYMBOL_GPL(smu_sat_get_sdb_partition);

/* refresh the cache */
static int wf_sat_read_cache(struct wf_sat *sat)
{
	int err;

	err = i2c_smbus_read_i2c_block_data(sat->i2c, 0x3f, 16, sat->cache);
	if (err < 0)
		return err;
	sat->last_read = jiffies;

#ifdef LOTSA_DEBUG
	{
		int i;
		printk(KERN_DEBUG "wf_sat_get: data is");
		print_hex_dump(KERN_DEBUG, "  ", DUMP_PREFIX_OFFSET,
			       16, 1, sat->cache, 16, false);
	}
#endif
	return 0;
}

static int wf_sat_sensor_get(struct wf_sensor *sr, s32 *value)
{
	struct wf_sat_sensor *sens = wf_to_sat(sr);
	struct wf_sat *sat = sens->sat;
	int i, err;
	s32 val;

	if (sat->i2c == NULL)
		return -ENODEV;

	mutex_lock(&sat->mutex);
	if (time_after(jiffies, (sat->last_read + MAX_AGE))) {
		err = wf_sat_read_cache(sat);
		if (err)
			goto fail;
	}

	i = sens->index * 2;
	val = ((sat->cache[i] << 8) + sat->cache[i+1]) << sens->shift;
	if (sens->index2 >= 0) {
		i = sens->index2 * 2;
		/* 4.12 * 8.8 -> 12.20; shift right 4 to get 16.16 */
		val = (val * ((sat->cache[i] << 8) + sat->cache[i+1])) >> 4;
	}

	*value = val;
	err = 0;

 fail:
	mutex_unlock(&sat->mutex);
	return err;
}

static void wf_sat_release(struct kref *ref)
{
	struct wf_sat *sat = container_of(ref, struct wf_sat, ref);

	if (sat->nr >= 0)
		sats[sat->nr] = NULL;
	kfree(sat);
}

static void wf_sat_sensor_release(struct wf_sensor *sr)
{
	struct wf_sat_sensor *sens = wf_to_sat(sr);
	struct wf_sat *sat = sens->sat;

	kfree(sens);
	kref_put(&sat->ref, wf_sat_release);
}

static const struct wf_sensor_ops wf_sat_ops = {
	.get_value	= wf_sat_sensor_get,
	.release	= wf_sat_sensor_release,
	.owner		= THIS_MODULE,
};

static int wf_sat_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device_node *dev = client->dev.of_node;
	struct wf_sat *sat;
	struct wf_sat_sensor *sens;
	const u32 *reg;
	const char *loc;
	u8 chip, core;
	struct device_node *child;
	int shift, cpu, index;
	char *name;
	int vsens[2], isens[2];

	sat = kzalloc(sizeof(struct wf_sat), GFP_KERNEL);
	if (sat == NULL)
		return -ENOMEM;
	sat->nr = -1;
	sat->node = of_node_get(dev);
	kref_init(&sat->ref);
	mutex_init(&sat->mutex);
	sat->i2c = client;
	INIT_LIST_HEAD(&sat->sensors);
	i2c_set_clientdata(client, sat);

	vsens[0] = vsens[1] = -1;
	isens[0] = isens[1] = -1;
	child = NULL;
	while ((child = of_get_next_child(dev, child)) != NULL) {
		reg = of_get_property(child, "reg", NULL);
		loc = of_get_property(child, "location", NULL);
		if (reg == NULL || loc == NULL)
			continue;

		/* the cooked sensors are between 0x30 and 0x37 */
		if (*reg < 0x30 || *reg > 0x37)
			continue;
		index = *reg - 0x30;

		/* expect location to be CPU [AB][01] ... */
		if (strncmp(loc, "CPU ", 4) != 0)
			continue;
		chip = loc[4] - 'A';
		core = loc[5] - '0';
		if (chip > 1 || core > 1) {
			printk(KERN_ERR "wf_sat_create: don't understand "
			       "location %s for %pOF\n", loc, child);
			continue;
		}
		cpu = 2 * chip + core;
		if (sat->nr < 0)
			sat->nr = chip;
		else if (sat->nr != chip) {
			printk(KERN_ERR "wf_sat_create: can't cope with "
			       "multiple CPU chips on one SAT (%s)\n", loc);
			continue;
		}

		if (of_node_is_type(child, "voltage-sensor")) {
			name = "cpu-voltage";
			shift = 4;
			vsens[core] = index;
		} else if (of_node_is_type(child, "current-sensor")) {
			name = "cpu-current";
			shift = 8;
			isens[core] = index;
		} else if (of_node_is_type(child, "temp-sensor")) {
			name = "cpu-temp";
			shift = 10;
		} else
			continue;	/* hmmm shouldn't happen */

		/* the +16 is enough for "cpu-voltage-n" */
		sens = kzalloc(sizeof(struct wf_sat_sensor) + 16, GFP_KERNEL);
		if (sens == NULL) {
			printk(KERN_ERR "wf_sat_create: couldn't create "
			       "%s sensor %d (no memory)\n", name, cpu);
			continue;
		}
		sens->index = index;
		sens->index2 = -1;
		sens->shift = shift;
		sens->sat = sat;
		sens->sens.ops = &wf_sat_ops;
		sens->sens.name = (char *) (sens + 1);
		snprintf((char *)sens->sens.name, 16, "%s-%d", name, cpu);

		if (wf_register_sensor(&sens->sens))
			kfree(sens);
		else {
			list_add(&sens->link, &sat->sensors);
			kref_get(&sat->ref);
		}
	}

	/* make the power sensors */
	for (core = 0; core < 2; ++core) {
		if (vsens[core] < 0 || isens[core] < 0)
			continue;
		cpu = 2 * sat->nr + core;
		sens = kzalloc(sizeof(struct wf_sat_sensor) + 16, GFP_KERNEL);
		if (sens == NULL) {
			printk(KERN_ERR "wf_sat_create: couldn't create power "
			       "sensor %d (no memory)\n", cpu);
			continue;
		}
		sens->index = vsens[core];
		sens->index2 = isens[core];
		sens->shift = 0;
		sens->sat = sat;
		sens->sens.ops = &wf_sat_ops;
		sens->sens.name = (char *) (sens + 1);
		snprintf((char *)sens->sens.name, 16, "cpu-power-%d", cpu);

		if (wf_register_sensor(&sens->sens))
			kfree(sens);
		else {
			list_add(&sens->link, &sat->sensors);
			kref_get(&sat->ref);
		}
	}

	if (sat->nr >= 0)
		sats[sat->nr] = sat;

	return 0;
}

static int wf_sat_remove(struct i2c_client *client)
{
	struct wf_sat *sat = i2c_get_clientdata(client);
	struct wf_sat_sensor *sens;

	/* release sensors */
	while(!list_empty(&sat->sensors)) {
		sens = list_first_entry(&sat->sensors,
					struct wf_sat_sensor, link);
		list_del(&sens->link);
		wf_unregister_sensor(&sens->sens);
	}
	sat->i2c = NULL;
	kref_put(&sat->ref, wf_sat_release);

	return 0;
}

static const struct i2c_device_id wf_sat_id[] = {
	{ "MAC,smu-sat", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wf_sat_id);

static struct i2c_driver wf_sat_driver = {
	.driver = {
		.name		= "wf_smu_sat",
	},
	.probe		= wf_sat_probe,
	.remove		= wf_sat_remove,
	.id_table	= wf_sat_id,
};

module_i2c_driver(wf_sat_driver);

MODULE_AUTHOR("Paul Mackerras <paulus@samba.org>");
MODULE_DESCRIPTION("SMU satellite sensors for PowerMac thermal control");
MODULE_LICENSE("GPL");
