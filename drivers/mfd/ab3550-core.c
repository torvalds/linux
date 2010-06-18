/*
 * Copyright (C) 2007-2010 ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 * Low-level core for exclusive access to the AB3550 IC on the I2C bus
 * and some basic chip-configuration.
 * Author: Bengt Jonsson <bengt.g.jonsson@stericsson.com>
 * Author: Mattias Nilsson <mattias.i.nilsson@stericsson.com>
 * Author: Mattias Wallin <mattias.wallin@stericsson.com>
 * Author: Rickard Andersson <rickard.andersson@stericsson.com>
 */

#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/random.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/mfd/abx500.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/mfd/core.h>

#define AB3550_NAME_STRING "ab3550"
#define AB3550_ID_FORMAT_STRING "AB3550 %s"
#define AB3550_NUM_BANKS 2
#define AB3550_NUM_EVENT_REG 5

/* These are the only registers inside AB3550 used in this main file */

/* Chip ID register */
#define AB3550_CID_REG           0x20

/* Interrupt event registers */
#define AB3550_EVENT_BANK        0
#define AB3550_EVENT_REG         0x22

/* Read/write operation values. */
#define AB3550_PERM_RD (0x01)
#define AB3550_PERM_WR (0x02)

/* Read/write permissions. */
#define AB3550_PERM_RO (AB3550_PERM_RD)
#define AB3550_PERM_RW (AB3550_PERM_RD | AB3550_PERM_WR)

/**
 * struct ab3550
 * @access_mutex: lock out concurrent accesses to the AB registers
 * @i2c_client: I2C client for this chip
 * @chip_name: name of this chip variant
 * @chip_id: 8 bit chip ID for this chip variant
 * @mask_work: a worker for writing to mask registers
 * @event_lock: a lock to protect the event_mask
 * @event_mask: a local copy of the mask event registers
 * @startup_events: a copy of the first reading of the event registers
 * @startup_events_read: whether the first events have been read
 */
struct ab3550 {
	struct mutex access_mutex;
	struct i2c_client *i2c_client[AB3550_NUM_BANKS];
	char chip_name[32];
	u8 chip_id;
	struct work_struct mask_work;
	spinlock_t event_lock;
	u8 event_mask[AB3550_NUM_EVENT_REG];
	u8 startup_events[AB3550_NUM_EVENT_REG];
	bool startup_events_read;
#ifdef CONFIG_DEBUG_FS
	unsigned int debug_bank;
	unsigned int debug_address;
#endif
};

/**
 * struct ab3550_reg_range
 * @first: the first address of the range
 * @last: the last address of the range
 * @perm: access permissions for the range
 */
struct ab3550_reg_range {
	u8 first;
	u8 last;
	u8 perm;
};

/**
 * struct ab3550_reg_ranges
 * @count: the number of ranges in the list
 * @range: the list of register ranges
 */
struct ab3550_reg_ranges {
	u8 count;
	const struct ab3550_reg_range *range;
};

/*
 * Permissible register ranges for reading and writing per device and bank.
 *
 * The ranges must be listed in increasing address order, and no overlaps are
 * allowed. It is assumed that write permission implies read permission
 * (i.e. only RO and RW permissions should be used).  Ranges with write
 * permission must not be split up.
 */

#define NO_RANGE {.count = 0, .range = NULL,}

static struct
ab3550_reg_ranges ab3550_reg_ranges[AB3550_NUM_DEVICES][AB3550_NUM_BANKS] = {
	[AB3550_DEVID_DAC] = {
		NO_RANGE,
		{
			.count = 2,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0xb0,
					.last = 0xba,
					.perm = AB3550_PERM_RW,
				},
				{
					.first = 0xbc,
					.last = 0xc3,
					.perm = AB3550_PERM_RW,
				},
			},
		},
	},
	[AB3550_DEVID_LEDS] = {
		NO_RANGE,
		{
			.count = 2,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0x5a,
					.last = 0x88,
					.perm = AB3550_PERM_RW,
				},
				{
					.first = 0x8a,
					.last = 0xad,
					.perm = AB3550_PERM_RW,
				},
			}
		},
	},
	[AB3550_DEVID_POWER] = {
		{
			.count = 1,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0x21,
					.last = 0x21,
					.perm = AB3550_PERM_RO,
				},
			}
		},
		NO_RANGE,
	},
	[AB3550_DEVID_REGULATORS] = {
		{
			.count = 1,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0x69,
					.last = 0xa3,
					.perm = AB3550_PERM_RW,
				},
			}
		},
		{
			.count = 1,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0x14,
					.last = 0x16,
					.perm = AB3550_PERM_RW,
				},
			}
		},
	},
	[AB3550_DEVID_SIM] = {
		{
			.count = 1,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0x21,
					.last = 0x21,
					.perm = AB3550_PERM_RO,
				},
			}
		},
		{
			.count = 1,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0x14,
					.last = 0x17,
					.perm = AB3550_PERM_RW,
				},
			}

		},
	},
	[AB3550_DEVID_UART] = {
		NO_RANGE,
		NO_RANGE,
	},
	[AB3550_DEVID_RTC] = {
		{
			.count = 1,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0x00,
					.last = 0x0c,
					.perm = AB3550_PERM_RW,
				},
			}
		},
		NO_RANGE,
	},
	[AB3550_DEVID_CHARGER] = {
		{
			.count = 2,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0x10,
					.last = 0x1a,
					.perm = AB3550_PERM_RW,
				},
				{
					.first = 0x21,
					.last = 0x21,
					.perm = AB3550_PERM_RO,
				},
			}
		},
		NO_RANGE,
	},
	[AB3550_DEVID_ADC] = {
		NO_RANGE,
		{
			.count = 1,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0x20,
					.last = 0x56,
					.perm = AB3550_PERM_RW,
				},

			}
		},
	},
	[AB3550_DEVID_FUELGAUGE] = {
		{
			.count = 1,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0x21,
					.last = 0x21,
					.perm = AB3550_PERM_RO,
				},
			}
		},
		{
			.count = 1,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0x00,
					.last = 0x0e,
					.perm = AB3550_PERM_RW,
				},
			}
		},
	},
	[AB3550_DEVID_VIBRATOR] = {
		NO_RANGE,
		{
			.count = 1,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0x10,
					.last = 0x13,
					.perm = AB3550_PERM_RW,
				},

			}
		},
	},
	[AB3550_DEVID_CODEC] = {
		{
			.count = 2,
			.range = (struct ab3550_reg_range[]) {
				{
					.first = 0x31,
					.last = 0x63,
					.perm = AB3550_PERM_RW,
				},
				{
					.first = 0x65,
					.last = 0x68,
					.perm = AB3550_PERM_RW,
				},
			}
		},
		NO_RANGE,
	},
};

static struct mfd_cell ab3550_devs[AB3550_NUM_DEVICES] = {
	[AB3550_DEVID_DAC] = {
		.name = "ab3550-dac",
		.id = AB3550_DEVID_DAC,
		.num_resources = 0,
	},
	[AB3550_DEVID_LEDS] = {
		.name = "ab3550-leds",
		.id = AB3550_DEVID_LEDS,
	},
	[AB3550_DEVID_POWER] = {
		.name = "ab3550-power",
		.id = AB3550_DEVID_POWER,
	},
	[AB3550_DEVID_REGULATORS] = {
		.name = "ab3550-regulators",
		.id = AB3550_DEVID_REGULATORS,
	},
	[AB3550_DEVID_SIM] = {
		.name = "ab3550-sim",
		.id = AB3550_DEVID_SIM,
	},
	[AB3550_DEVID_UART] = {
		.name = "ab3550-uart",
		.id = AB3550_DEVID_UART,
	},
	[AB3550_DEVID_RTC] = {
		.name = "ab3550-rtc",
		.id = AB3550_DEVID_RTC,
	},
	[AB3550_DEVID_CHARGER] = {
		.name = "ab3550-charger",
		.id = AB3550_DEVID_CHARGER,
	},
	[AB3550_DEVID_ADC] = {
		.name = "ab3550-adc",
		.id = AB3550_DEVID_ADC,
		.num_resources = 10,
		.resources = (struct resource[]) {
			{
				.name = "TRIGGER-0",
				.flags = IORESOURCE_IRQ,
				.start = 16,
				.end = 16,
			},
			{
				.name = "TRIGGER-1",
				.flags = IORESOURCE_IRQ,
				.start = 17,
				.end = 17,
			},
			{
				.name = "TRIGGER-2",
				.flags = IORESOURCE_IRQ,
				.start = 18,
				.end = 18,
			},
			{
				.name = "TRIGGER-3",
				.flags = IORESOURCE_IRQ,
				.start = 19,
				.end = 19,
			},
			{
				.name = "TRIGGER-4",
				.flags = IORESOURCE_IRQ,
				.start = 20,
				.end = 20,
			},
			{
				.name = "TRIGGER-5",
				.flags = IORESOURCE_IRQ,
				.start = 21,
				.end = 21,
			},
			{
				.name = "TRIGGER-6",
				.flags = IORESOURCE_IRQ,
				.start = 22,
				.end = 22,
			},
			{
				.name = "TRIGGER-7",
				.flags = IORESOURCE_IRQ,
				.start = 23,
				.end = 23,
			},
			{
				.name = "TRIGGER-VBAT-TXON",
				.flags = IORESOURCE_IRQ,
				.start = 13,
				.end = 13,
			},
			{
				.name = "TRIGGER-VBAT",
				.flags = IORESOURCE_IRQ,
				.start = 12,
				.end = 12,
			},
		},
	},
	[AB3550_DEVID_FUELGAUGE] = {
		.name = "ab3550-fuelgauge",
		.id = AB3550_DEVID_FUELGAUGE,
	},
	[AB3550_DEVID_VIBRATOR] = {
		.name = "ab3550-vibrator",
		.id = AB3550_DEVID_VIBRATOR,
	},
	[AB3550_DEVID_CODEC] = {
		.name = "ab3550-codec",
		.id = AB3550_DEVID_CODEC,
	},
};

/*
 * I2C transactions with error messages.
 */
static int ab3550_i2c_master_send(struct ab3550 *ab, u8 bank, u8 *data,
	u8 count)
{
	int err;

	err = i2c_master_send(ab->i2c_client[bank], data, count);
	if (err < 0) {
		dev_err(&ab->i2c_client[0]->dev, "send error: %d\n", err);
		return err;
	}
	return 0;
}

static int ab3550_i2c_master_recv(struct ab3550 *ab, u8 bank, u8 *data,
	u8 count)
{
	int err;

	err = i2c_master_recv(ab->i2c_client[bank], data, count);
	if (err < 0) {
		dev_err(&ab->i2c_client[0]->dev, "receive error: %d\n", err);
		return err;
	}
	return 0;
}

/*
 * Functionality for getting/setting register values.
 */
static int get_register_interruptible(struct ab3550 *ab, u8 bank, u8 reg,
	u8 *value)
{
	int err;

	err = mutex_lock_interruptible(&ab->access_mutex);
	if (err)
		return err;

	err = ab3550_i2c_master_send(ab, bank, &reg, 1);
	if (!err)
		err = ab3550_i2c_master_recv(ab, bank, value, 1);

	mutex_unlock(&ab->access_mutex);
	return err;
}

static int get_register_page_interruptible(struct ab3550 *ab, u8 bank,
	u8 first_reg, u8 *regvals, u8 numregs)
{
	int err;

	err = mutex_lock_interruptible(&ab->access_mutex);
	if (err)
		return err;

	err = ab3550_i2c_master_send(ab, bank, &first_reg, 1);
	if (!err)
		err = ab3550_i2c_master_recv(ab, bank, regvals, numregs);

	mutex_unlock(&ab->access_mutex);
	return err;
}

static int mask_and_set_register_interruptible(struct ab3550 *ab, u8 bank,
	u8 reg, u8 bitmask, u8 bitvalues)
{
	int err = 0;

	if (likely(bitmask)) {
		u8 reg_bits[2] = {reg, 0};

		err = mutex_lock_interruptible(&ab->access_mutex);
		if (err)
			return err;

		if (bitmask == 0xFF) /* No need to read in this case. */
			reg_bits[1] = bitvalues;
		else { /* Read and modify the register value. */
			u8 bits;

			err = ab3550_i2c_master_send(ab, bank, &reg, 1);
			if (err)
				goto unlock_and_return;
			err = ab3550_i2c_master_recv(ab, bank, &bits, 1);
			if (err)
				goto unlock_and_return;
			reg_bits[1] = ((~bitmask & bits) |
				(bitmask & bitvalues));
		}
		/* Write the new value. */
		err = ab3550_i2c_master_send(ab, bank, reg_bits, 2);
unlock_and_return:
		mutex_unlock(&ab->access_mutex);
	}
	return err;
}

/*
 * Read/write permission checking functions.
 */
static bool page_write_allowed(const struct ab3550_reg_ranges *ranges,
	u8 first_reg, u8 last_reg)
{
	u8 i;

	if (last_reg < first_reg)
		return false;

	for (i = 0; i < ranges->count; i++) {
		if (first_reg < ranges->range[i].first)
			break;
		if ((last_reg <= ranges->range[i].last) &&
			(ranges->range[i].perm & AB3550_PERM_WR))
			return true;
	}
	return false;
}

static bool reg_write_allowed(const struct ab3550_reg_ranges *ranges, u8 reg)
{
	return page_write_allowed(ranges, reg, reg);
}

static bool page_read_allowed(const struct ab3550_reg_ranges *ranges,
	u8 first_reg, u8 last_reg)
{
	u8 i;

	if (last_reg < first_reg)
		return false;
	/* Find the range (if it exists in the list) that includes first_reg. */
	for (i = 0; i < ranges->count; i++) {
		if (first_reg < ranges->range[i].first)
			return false;
		if (first_reg <= ranges->range[i].last)
			break;
	}
	/* Make sure that the entire range up to and including last_reg is
	 * readable. This may span several of the ranges in the list.
	 */
	while ((i < ranges->count) &&
		(ranges->range[i].perm & AB3550_PERM_RD)) {
		if (last_reg <= ranges->range[i].last)
			return true;
		if ((++i >= ranges->count) ||
			(ranges->range[i].first !=
			 (ranges->range[i - 1].last + 1))) {
			break;
		}
	}
	return false;
}

static bool reg_read_allowed(const struct ab3550_reg_ranges *ranges, u8 reg)
{
	return page_read_allowed(ranges, reg, reg);
}

/*
 * The exported register access functionality.
 */
int ab3550_get_chip_id(struct device *dev)
{
	struct ab3550 *ab = dev_get_drvdata(dev->parent);
	return (int)ab->chip_id;
}

int ab3550_mask_and_set_register_interruptible(struct device *dev, u8 bank,
	u8 reg, u8 bitmask, u8 bitvalues)
{
	struct ab3550 *ab;
	struct platform_device *pdev = to_platform_device(dev);

	if ((AB3550_NUM_BANKS <= bank) ||
		!reg_write_allowed(&ab3550_reg_ranges[pdev->id][bank], reg))
		return -EINVAL;

	ab = dev_get_drvdata(dev->parent);
	return mask_and_set_register_interruptible(ab, bank, reg,
		bitmask, bitvalues);
}

int ab3550_set_register_interruptible(struct device *dev, u8 bank, u8 reg,
	u8 value)
{
	return ab3550_mask_and_set_register_interruptible(dev, bank, reg, 0xFF,
		value);
}

int ab3550_get_register_interruptible(struct device *dev, u8 bank, u8 reg,
	u8 *value)
{
	struct ab3550 *ab;
	struct platform_device *pdev = to_platform_device(dev);

	if ((AB3550_NUM_BANKS <= bank) ||
		!reg_read_allowed(&ab3550_reg_ranges[pdev->id][bank], reg))
		return -EINVAL;

	ab = dev_get_drvdata(dev->parent);
	return get_register_interruptible(ab, bank, reg, value);
}

int ab3550_get_register_page_interruptible(struct device *dev, u8 bank,
	u8 first_reg, u8 *regvals, u8 numregs)
{
	struct ab3550 *ab;
	struct platform_device *pdev = to_platform_device(dev);

	if ((AB3550_NUM_BANKS <= bank) ||
		!page_read_allowed(&ab3550_reg_ranges[pdev->id][bank],
			first_reg, (first_reg + numregs - 1)))
		return -EINVAL;

	ab = dev_get_drvdata(dev->parent);
	return get_register_page_interruptible(ab, bank, first_reg, regvals,
		numregs);
}

int ab3550_event_registers_startup_state_get(struct device *dev, u8 *event)
{
	struct ab3550 *ab;

	ab = dev_get_drvdata(dev->parent);
	if (!ab->startup_events_read)
		return -EAGAIN; /* Try again later */

	memcpy(event, ab->startup_events, AB3550_NUM_EVENT_REG);
	return 0;
}

int ab3550_startup_irq_enabled(struct device *dev, unsigned int irq)
{
	struct ab3550 *ab;
	struct ab3550_platform_data *plf_data;
	bool val;

	ab = get_irq_chip_data(irq);
	plf_data = ab->i2c_client[0]->dev.platform_data;
	irq -= plf_data->irq.base;
	val = ((ab->startup_events[irq / 8] & BIT(irq % 8)) != 0);

	return val;
}

static struct abx500_ops ab3550_ops = {
	.get_chip_id = ab3550_get_chip_id,
	.get_register = ab3550_get_register_interruptible,
	.set_register = ab3550_set_register_interruptible,
	.get_register_page = ab3550_get_register_page_interruptible,
	.set_register_page = NULL,
	.mask_and_set_register = ab3550_mask_and_set_register_interruptible,
	.event_registers_startup_state_get =
		ab3550_event_registers_startup_state_get,
	.startup_irq_enabled = ab3550_startup_irq_enabled,
};

static irqreturn_t ab3550_irq_handler(int irq, void *data)
{
	struct ab3550 *ab = data;
	int err;
	unsigned int i;
	u8 e[AB3550_NUM_EVENT_REG];
	u8 *events;
	unsigned long flags;

	events = (ab->startup_events_read ? e : ab->startup_events);

	err = get_register_page_interruptible(ab, AB3550_EVENT_BANK,
		AB3550_EVENT_REG, events, AB3550_NUM_EVENT_REG);
	if (err)
		goto err_event_rd;

	if (!ab->startup_events_read) {
		dev_info(&ab->i2c_client[0]->dev,
			"startup events 0x%x,0x%x,0x%x,0x%x,0x%x\n",
			ab->startup_events[0], ab->startup_events[1],
			ab->startup_events[2], ab->startup_events[3],
			ab->startup_events[4]);
		ab->startup_events_read = true;
		goto out;
	}

	/* The two highest bits in event[4] are not used. */
	events[4] &= 0x3f;

	spin_lock_irqsave(&ab->event_lock, flags);
	for (i = 0; i < AB3550_NUM_EVENT_REG; i++)
		events[i] &= ~ab->event_mask[i];
	spin_unlock_irqrestore(&ab->event_lock, flags);

	for (i = 0; i < AB3550_NUM_EVENT_REG; i++) {
		u8 bit;
		u8 event_reg;

		dev_dbg(&ab->i2c_client[0]->dev, "IRQ Event[%d]: 0x%2x\n",
			i, events[i]);

		event_reg = events[i];
		for (bit = 0; event_reg; bit++, event_reg /= 2) {
			if (event_reg % 2) {
				unsigned int irq;
				struct ab3550_platform_data *plf_data;

				plf_data = ab->i2c_client[0]->dev.platform_data;
				irq = plf_data->irq.base + (i * 8) + bit;
				handle_nested_irq(irq);
			}
		}
	}
out:
	return IRQ_HANDLED;

err_event_rd:
	dev_dbg(&ab->i2c_client[0]->dev, "error reading event registers\n");
	return IRQ_HANDLED;
}

#ifdef CONFIG_DEBUG_FS
static struct ab3550_reg_ranges debug_ranges[AB3550_NUM_BANKS] = {
	{
		.count = 6,
		.range = (struct ab3550_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x0e,
			},
			{
				.first = 0x10,
				.last = 0x1a,
			},
			{
				.first = 0x1e,
				.last = 0x4f,
			},
			{
				.first = 0x51,
				.last = 0x63,
			},
			{
				.first = 0x65,
				.last = 0xa3,
			},
			{
				.first = 0xa5,
				.last = 0xa8,
			},
		}
	},
	{
		.count = 8,
		.range = (struct ab3550_reg_range[]) {
			{
				.first = 0x00,
				.last = 0x0e,
			},
			{
				.first = 0x10,
				.last = 0x17,
			},
			{
				.first = 0x1a,
				.last = 0x1c,
			},
			{
				.first = 0x20,
				.last = 0x56,
			},
			{
				.first = 0x5a,
				.last = 0x88,
			},
			{
				.first = 0x8a,
				.last = 0xad,
			},
			{
				.first = 0xb0,
				.last = 0xba,
			},
			{
				.first = 0xbc,
				.last = 0xc3,
			},
		}
	},
};

static int ab3550_registers_print(struct seq_file *s, void *p)
{
	struct ab3550 *ab = s->private;
	int bank;

	seq_printf(s, AB3550_NAME_STRING " register values:\n");

	for (bank = 0; bank < AB3550_NUM_BANKS; bank++) {
		unsigned int i;

		seq_printf(s, " bank %d:\n", bank);
		for (i = 0; i < debug_ranges[bank].count; i++) {
			u8 reg;

			for (reg = debug_ranges[bank].range[i].first;
				reg <= debug_ranges[bank].range[i].last;
				reg++) {
				u8 value;

				get_register_interruptible(ab, bank, reg,
					&value);
				seq_printf(s, "  [%d/0x%02X]: 0x%02X\n", bank,
					reg, value);
			}
		}
	}
	return 0;
}

static int ab3550_registers_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab3550_registers_print, inode->i_private);
}

static const struct file_operations ab3550_registers_fops = {
	.open = ab3550_registers_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ab3550_bank_print(struct seq_file *s, void *p)
{
	struct ab3550 *ab = s->private;

	seq_printf(s, "%d\n", ab->debug_bank);
	return 0;
}

static int ab3550_bank_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab3550_bank_print, inode->i_private);
}

static ssize_t ab3550_bank_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct ab3550 *ab = ((struct seq_file *)(file->private_data))->private;
	char buf[32];
	int buf_size;
	unsigned long user_bank;
	int err;

	/* Get userspace string and assure termination */
	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	err = strict_strtoul(buf, 0, &user_bank);
	if (err)
		return -EINVAL;

	if (user_bank >= AB3550_NUM_BANKS) {
		dev_err(&ab->i2c_client[0]->dev,
			"debugfs error input > number of banks\n");
		return -EINVAL;
	}

	ab->debug_bank = user_bank;

	return buf_size;
}

static int ab3550_address_print(struct seq_file *s, void *p)
{
	struct ab3550 *ab = s->private;

	seq_printf(s, "0x%02X\n", ab->debug_address);
	return 0;
}

static int ab3550_address_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab3550_address_print, inode->i_private);
}

static ssize_t ab3550_address_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct ab3550 *ab = ((struct seq_file *)(file->private_data))->private;
	char buf[32];
	int buf_size;
	unsigned long user_address;
	int err;

	/* Get userspace string and assure termination */
	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	err = strict_strtoul(buf, 0, &user_address);
	if (err)
		return -EINVAL;
	if (user_address > 0xff) {
		dev_err(&ab->i2c_client[0]->dev,
			"debugfs error input > 0xff\n");
		return -EINVAL;
	}
	ab->debug_address = user_address;
	return buf_size;
}

static int ab3550_val_print(struct seq_file *s, void *p)
{
	struct ab3550 *ab = s->private;
	int err;
	u8 regvalue;

	err = get_register_interruptible(ab, (u8)ab->debug_bank,
		(u8)ab->debug_address, &regvalue);
	if (err)
		return -EINVAL;
	seq_printf(s, "0x%02X\n", regvalue);

	return 0;
}

static int ab3550_val_open(struct inode *inode, struct file *file)
{
	return single_open(file, ab3550_val_print, inode->i_private);
}

static ssize_t ab3550_val_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct ab3550 *ab = ((struct seq_file *)(file->private_data))->private;
	char buf[32];
	int buf_size;
	unsigned long user_val;
	int err;
	u8 regvalue;

	/* Get userspace string and assure termination */
	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	err = strict_strtoul(buf, 0, &user_val);
	if (err)
		return -EINVAL;
	if (user_val > 0xff) {
		dev_err(&ab->i2c_client[0]->dev,
			"debugfs error input > 0xff\n");
		return -EINVAL;
	}
	err = mask_and_set_register_interruptible(
		ab, (u8)ab->debug_bank,
		(u8)ab->debug_address, 0xFF, (u8)user_val);
	if (err)
		return -EINVAL;

	get_register_interruptible(ab, (u8)ab->debug_bank,
		(u8)ab->debug_address, &regvalue);
	if (err)
		return -EINVAL;

	return buf_size;
}

static const struct file_operations ab3550_bank_fops = {
	.open = ab3550_bank_open,
	.write = ab3550_bank_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab3550_address_fops = {
	.open = ab3550_address_open,
	.write = ab3550_address_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations ab3550_val_fops = {
	.open = ab3550_val_open,
	.write = ab3550_val_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct dentry *ab3550_dir;
static struct dentry *ab3550_reg_file;
static struct dentry *ab3550_bank_file;
static struct dentry *ab3550_address_file;
static struct dentry *ab3550_val_file;

static inline void ab3550_setup_debugfs(struct ab3550 *ab)
{
	ab->debug_bank = 0;
	ab->debug_address = 0x00;

	ab3550_dir = debugfs_create_dir(AB3550_NAME_STRING, NULL);
	if (!ab3550_dir)
		goto exit_no_debugfs;

	ab3550_reg_file = debugfs_create_file("all-registers",
		S_IRUGO, ab3550_dir, ab, &ab3550_registers_fops);
	if (!ab3550_reg_file)
		goto exit_destroy_dir;

	ab3550_bank_file = debugfs_create_file("register-bank",
		(S_IRUGO | S_IWUGO), ab3550_dir, ab, &ab3550_bank_fops);
	if (!ab3550_bank_file)
		goto exit_destroy_reg;

	ab3550_address_file = debugfs_create_file("register-address",
		(S_IRUGO | S_IWUGO), ab3550_dir, ab, &ab3550_address_fops);
	if (!ab3550_address_file)
		goto exit_destroy_bank;

	ab3550_val_file = debugfs_create_file("register-value",
		(S_IRUGO | S_IWUGO), ab3550_dir, ab, &ab3550_val_fops);
	if (!ab3550_val_file)
		goto exit_destroy_address;

	return;

exit_destroy_address:
	debugfs_remove(ab3550_address_file);
exit_destroy_bank:
	debugfs_remove(ab3550_bank_file);
exit_destroy_reg:
	debugfs_remove(ab3550_reg_file);
exit_destroy_dir:
	debugfs_remove(ab3550_dir);
exit_no_debugfs:
	dev_err(&ab->i2c_client[0]->dev, "failed to create debugfs entries.\n");
	return;
}

static inline void ab3550_remove_debugfs(void)
{
	debugfs_remove(ab3550_val_file);
	debugfs_remove(ab3550_address_file);
	debugfs_remove(ab3550_bank_file);
	debugfs_remove(ab3550_reg_file);
	debugfs_remove(ab3550_dir);
}

#else /* !CONFIG_DEBUG_FS */
static inline void ab3550_setup_debugfs(struct ab3550 *ab)
{
}
static inline void ab3550_remove_debugfs(void)
{
}
#endif

/*
 * Basic set-up, datastructure creation/destruction and I2C interface.
 * This sets up a default config in the AB3550 chip so that it
 * will work as expected.
 */
static int __init ab3550_setup(struct ab3550 *ab)
{
	int err = 0;
	int i;
	struct ab3550_platform_data *plf_data;
	struct abx500_init_settings *settings;

	plf_data = ab->i2c_client[0]->dev.platform_data;
	settings = plf_data->init_settings;

	for (i = 0; i < plf_data->init_settings_sz; i++) {
		err = mask_and_set_register_interruptible(ab,
			settings[i].bank,
			settings[i].reg,
			0xFF, settings[i].setting);
		if (err)
			goto exit_no_setup;

		/* If event mask register update the event mask in ab3550 */
		if ((settings[i].bank == 0) &&
			(AB3550_IMR1 <= settings[i].reg) &&
			(settings[i].reg <= AB3550_IMR5)) {
			ab->event_mask[settings[i].reg - AB3550_IMR1] =
				settings[i].setting;
		}
	}
exit_no_setup:
	return err;
}

static void ab3550_mask_work(struct work_struct *work)
{
	struct ab3550 *ab = container_of(work, struct ab3550, mask_work);
	int i;
	unsigned long flags;
	u8 mask[AB3550_NUM_EVENT_REG];

	spin_lock_irqsave(&ab->event_lock, flags);
	for (i = 0; i < AB3550_NUM_EVENT_REG; i++)
		mask[i] = ab->event_mask[i];
	spin_unlock_irqrestore(&ab->event_lock, flags);

	for (i = 0; i < AB3550_NUM_EVENT_REG; i++) {
		int err;

		err = mask_and_set_register_interruptible(ab, 0,
			(AB3550_IMR1 + i), ~0, mask[i]);
		if (err)
			dev_err(&ab->i2c_client[0]->dev,
				"ab3550_mask_work failed 0x%x,0x%x\n",
				(AB3550_IMR1 + i), mask[i]);
	}
}

static void ab3550_mask(unsigned int irq)
{
	unsigned long flags;
	struct ab3550 *ab;
	struct ab3550_platform_data *plf_data;

	ab = get_irq_chip_data(irq);
	plf_data = ab->i2c_client[0]->dev.platform_data;
	irq -= plf_data->irq.base;

	spin_lock_irqsave(&ab->event_lock, flags);
	ab->event_mask[irq / 8] |= BIT(irq % 8);
	spin_unlock_irqrestore(&ab->event_lock, flags);

	schedule_work(&ab->mask_work);
}

static void ab3550_unmask(unsigned int irq)
{
	unsigned long flags;
	struct ab3550 *ab;
	struct ab3550_platform_data *plf_data;

	ab = get_irq_chip_data(irq);
	plf_data = ab->i2c_client[0]->dev.platform_data;
	irq -= plf_data->irq.base;

	spin_lock_irqsave(&ab->event_lock, flags);
	ab->event_mask[irq / 8] &= ~BIT(irq % 8);
	spin_unlock_irqrestore(&ab->event_lock, flags);

	schedule_work(&ab->mask_work);
}

static void noop(unsigned int irq)
{
}

static struct irq_chip ab3550_irq_chip = {
	.name		= "ab3550-core", /* Keep the same name as the request */
	.startup	= NULL, /* defaults to enable */
	.shutdown	= NULL, /* defaults to disable */
	.enable		= NULL, /* defaults to unmask */
	.disable	= ab3550_mask, /* No default to mask in chip.c */
	.ack		= noop,
	.mask		= ab3550_mask,
	.unmask		= ab3550_unmask,
	.end		= NULL,
};

struct ab_family_id {
	u8	id;
	char	*name;
};

static const struct ab_family_id ids[] __initdata = {
	/* AB3550 */
	{
		.id = AB3550_P1A,
		.name = "P1A"
	},
	/* Terminator */
	{
		.id = 0x00,
	}
};

static int __init ab3550_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct ab3550 *ab;
	struct ab3550_platform_data *ab3550_plf_data =
		client->dev.platform_data;
	int err;
	int i;
	int num_i2c_clients = 0;

	ab = kzalloc(sizeof(struct ab3550), GFP_KERNEL);
	if (!ab) {
		dev_err(&client->dev,
			"could not allocate " AB3550_NAME_STRING " device\n");
		return -ENOMEM;
	}

	/* Initialize data structure */
	mutex_init(&ab->access_mutex);
	spin_lock_init(&ab->event_lock);
	ab->i2c_client[0] = client;

	i2c_set_clientdata(client, ab);

	/* Read chip ID register */
	err = get_register_interruptible(ab, 0, AB3550_CID_REG, &ab->chip_id);
	if (err) {
		dev_err(&client->dev, "could not communicate with the analog "
			"baseband chip\n");
		goto exit_no_detect;
	}

	for (i = 0; ids[i].id != 0x0; i++) {
		if (ids[i].id == ab->chip_id) {
			snprintf(&ab->chip_name[0], sizeof(ab->chip_name) - 1,
				AB3550_ID_FORMAT_STRING, ids[i].name);
			break;
		}
	}

	if (ids[i].id == 0x0) {
		dev_err(&client->dev, "unknown analog baseband chip id: 0x%x\n",
			ab->chip_id);
		dev_err(&client->dev, "driver not started!\n");
		goto exit_no_detect;
	}

	dev_info(&client->dev, "detected AB chip: %s\n", &ab->chip_name[0]);

	/* Attach other dummy I2C clients. */
	while (++num_i2c_clients < AB3550_NUM_BANKS) {
		ab->i2c_client[num_i2c_clients] =
			i2c_new_dummy(client->adapter,
				(client->addr + num_i2c_clients));
		if (!ab->i2c_client[num_i2c_clients]) {
			err = -ENOMEM;
			goto exit_no_dummy_client;
		}
		strlcpy(ab->i2c_client[num_i2c_clients]->name, id->name,
			sizeof(ab->i2c_client[num_i2c_clients]->name));
	}

	err = ab3550_setup(ab);
	if (err)
		goto exit_no_setup;

	INIT_WORK(&ab->mask_work, ab3550_mask_work);

	for (i = 0; i < ab3550_plf_data->irq.count; i++) {
		unsigned int irq;

		irq = ab3550_plf_data->irq.base + i;
		set_irq_chip_data(irq, ab);
		set_irq_chip_and_handler(irq, &ab3550_irq_chip,
			handle_simple_irq);
		set_irq_nested_thread(irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		set_irq_noprobe(irq);
#endif
	}

	err = request_threaded_irq(client->irq, NULL, ab3550_irq_handler,
		IRQF_ONESHOT, "ab3550-core", ab);
	/* This real unpredictable IRQ is of course sampled for entropy */
	rand_initialize_irq(client->irq);

	if (err)
		goto exit_no_irq;

	err = abx500_register_ops(&client->dev, &ab3550_ops);
	if (err)
		goto exit_no_ops;

	/* Set up and register the platform devices. */
	for (i = 0; i < AB3550_NUM_DEVICES; i++) {
		ab3550_devs[i].platform_data = ab3550_plf_data->dev_data[i];
		ab3550_devs[i].data_size = ab3550_plf_data->dev_data_sz[i];
	}

	err = mfd_add_devices(&client->dev, 0, ab3550_devs,
		ARRAY_SIZE(ab3550_devs), NULL,
		ab3550_plf_data->irq.base);

	ab3550_setup_debugfs(ab);

	return 0;

exit_no_ops:
exit_no_irq:
exit_no_setup:
exit_no_dummy_client:
	/* Unregister the dummy i2c clients. */
	while (--num_i2c_clients)
		i2c_unregister_device(ab->i2c_client[num_i2c_clients]);
exit_no_detect:
	kfree(ab);
	return err;
}

static int __exit ab3550_remove(struct i2c_client *client)
{
	struct ab3550 *ab = i2c_get_clientdata(client);
	int num_i2c_clients = AB3550_NUM_BANKS;

	mfd_remove_devices(&client->dev);
	ab3550_remove_debugfs();

	while (--num_i2c_clients)
		i2c_unregister_device(ab->i2c_client[num_i2c_clients]);

	/*
	 * At this point, all subscribers should have unregistered
	 * their notifiers so deactivate IRQ
	 */
	free_irq(client->irq, ab);
	kfree(ab);
	return 0;
}

static const struct i2c_device_id ab3550_id[] = {
	{AB3550_NAME_STRING, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, ab3550_id);

static struct i2c_driver ab3550_driver = {
	.driver = {
		.name	= AB3550_NAME_STRING,
		.owner	= THIS_MODULE,
	},
	.id_table	= ab3550_id,
	.probe		= ab3550_probe,
	.remove		= __exit_p(ab3550_remove),
};

static int __init ab3550_i2c_init(void)
{
	return i2c_add_driver(&ab3550_driver);
}

static void __exit ab3550_i2c_exit(void)
{
	i2c_del_driver(&ab3550_driver);
}

subsys_initcall(ab3550_i2c_init);
module_exit(ab3550_i2c_exit);

MODULE_AUTHOR("Mattias Wallin <mattias.wallin@stericsson.com>");
MODULE_DESCRIPTION("AB3550 core driver");
MODULE_LICENSE("GPL");
