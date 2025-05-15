// SPDX-License-Identifier: GPL-2.0
/*
 * Support for the four N64 controllers.
 *
 * Copyright (c) 2021 Lauri Kasanen
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/limits.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/timer.h>

MODULE_AUTHOR("Lauri Kasanen <cand@gmx.com>");
MODULE_DESCRIPTION("Driver for N64 controllers");
MODULE_LICENSE("GPL");

#define PIF_RAM 0x1fc007c0

#define SI_DRAM_REG 0
#define SI_READ_REG 1
#define SI_WRITE_REG 4
#define SI_STATUS_REG 6

#define SI_STATUS_DMA_BUSY  BIT(0)
#define SI_STATUS_IO_BUSY   BIT(1)

#define N64_CONTROLLER_ID 0x0500

#define MAX_CONTROLLERS 4

static const char *n64joy_phys[MAX_CONTROLLERS] = {
	"n64joy/port0",
	"n64joy/port1",
	"n64joy/port2",
	"n64joy/port3",
};

struct n64joy_priv {
	u64 si_buf[8] ____cacheline_aligned;
	struct timer_list timer;
	struct mutex n64joy_mutex;
	struct input_dev *n64joy_dev[MAX_CONTROLLERS];
	u32 __iomem *reg_base;
	u8 n64joy_opened;
};

struct joydata {
	unsigned int: 16; /* unused */
	unsigned int err: 2;
	unsigned int: 14; /* unused */

	union {
		u32 data;

		struct {
			unsigned int a: 1;
			unsigned int b: 1;
			unsigned int z: 1;
			unsigned int start: 1;
			unsigned int up: 1;
			unsigned int down: 1;
			unsigned int left: 1;
			unsigned int right: 1;
			unsigned int: 2; /* unused */
			unsigned int l: 1;
			unsigned int r: 1;
			unsigned int c_up: 1;
			unsigned int c_down: 1;
			unsigned int c_left: 1;
			unsigned int c_right: 1;
			signed int x: 8;
			signed int y: 8;
		};
	};
};

static void n64joy_write_reg(u32 __iomem *reg_base, const u8 reg, const u32 value)
{
	writel(value, reg_base + reg);
}

static u32 n64joy_read_reg(u32 __iomem *reg_base, const u8 reg)
{
	return readl(reg_base + reg);
}

static void n64joy_wait_si_dma(u32 __iomem *reg_base)
{
	while (n64joy_read_reg(reg_base, SI_STATUS_REG) &
	       (SI_STATUS_DMA_BUSY | SI_STATUS_IO_BUSY))
		cpu_relax();
}

static void n64joy_exec_pif(struct n64joy_priv *priv, const u64 in[8])
{
	unsigned long flags;

	dma_cache_wback_inv((unsigned long) in, 8 * 8);
	dma_cache_inv((unsigned long) priv->si_buf, 8 * 8);

	local_irq_save(flags);

	n64joy_wait_si_dma(priv->reg_base);

	barrier();
	n64joy_write_reg(priv->reg_base, SI_DRAM_REG, virt_to_phys(in));
	barrier();
	n64joy_write_reg(priv->reg_base, SI_WRITE_REG, PIF_RAM);
	barrier();

	n64joy_wait_si_dma(priv->reg_base);

	barrier();
	n64joy_write_reg(priv->reg_base, SI_DRAM_REG, virt_to_phys(priv->si_buf));
	barrier();
	n64joy_write_reg(priv->reg_base, SI_READ_REG, PIF_RAM);
	barrier();

	n64joy_wait_si_dma(priv->reg_base);

	local_irq_restore(flags);
}

static const u64 polldata[] ____cacheline_aligned = {
	0xff010401ffffffff,
	0xff010401ffffffff,
	0xff010401ffffffff,
	0xff010401ffffffff,
	0xfe00000000000000,
	0,
	0,
	1
};

static void n64joy_poll(struct timer_list *t)
{
	const struct joydata *data;
	struct n64joy_priv *priv = container_of(t, struct n64joy_priv, timer);
	struct input_dev *dev;
	u32 i;

	n64joy_exec_pif(priv, polldata);

	data = (struct joydata *) priv->si_buf;

	for (i = 0; i < MAX_CONTROLLERS; i++) {
		if (!priv->n64joy_dev[i])
			continue;

		dev = priv->n64joy_dev[i];

		/* d-pad */
		input_report_key(dev, BTN_DPAD_UP, data[i].up);
		input_report_key(dev, BTN_DPAD_DOWN, data[i].down);
		input_report_key(dev, BTN_DPAD_LEFT, data[i].left);
		input_report_key(dev, BTN_DPAD_RIGHT, data[i].right);

		/* c buttons */
		input_report_key(dev, BTN_FORWARD, data[i].c_up);
		input_report_key(dev, BTN_BACK, data[i].c_down);
		input_report_key(dev, BTN_LEFT, data[i].c_left);
		input_report_key(dev, BTN_RIGHT, data[i].c_right);

		/* matching buttons */
		input_report_key(dev, BTN_START, data[i].start);
		input_report_key(dev, BTN_Z, data[i].z);

		/* remaining ones: a, b, l, r */
		input_report_key(dev, BTN_0, data[i].a);
		input_report_key(dev, BTN_1, data[i].b);
		input_report_key(dev, BTN_2, data[i].l);
		input_report_key(dev, BTN_3, data[i].r);

		input_report_abs(dev, ABS_X, data[i].x);
		input_report_abs(dev, ABS_Y, data[i].y);

		input_sync(dev);
	}

	mod_timer(&priv->timer, jiffies + msecs_to_jiffies(16));
}

static int n64joy_open(struct input_dev *dev)
{
	struct n64joy_priv *priv = input_get_drvdata(dev);

	scoped_guard(mutex_intr, &priv->n64joy_mutex) {
		if (!priv->n64joy_opened) {
			/*
			 * We could use the vblank irq, but it's not important
			 * if the poll point slightly changes.
			 */
			timer_setup(&priv->timer, n64joy_poll, 0);
			mod_timer(&priv->timer, jiffies + msecs_to_jiffies(16));
		}

		priv->n64joy_opened++;
		return 0;
	}

	return -EINTR;
}

static void n64joy_close(struct input_dev *dev)
{
	struct n64joy_priv *priv = input_get_drvdata(dev);

	guard(mutex)(&priv->n64joy_mutex);

	if (!--priv->n64joy_opened)
		timer_delete_sync(&priv->timer);
}

static const u64 __initconst scandata[] ____cacheline_aligned = {
	0xff010300ffffffff,
	0xff010300ffffffff,
	0xff010300ffffffff,
	0xff010300ffffffff,
	0xfe00000000000000,
	0,
	0,
	1
};

/*
 * The target device is embedded and RAM-constrained. We save RAM
 * by initializing in __init code that gets dropped late in boot.
 * For the same reason there is no module or unloading support.
 */
static int __init n64joy_probe(struct platform_device *pdev)
{
	const struct joydata *data;
	struct n64joy_priv *priv;
	struct input_dev *dev;
	int err = 0;
	u32 i, j, found = 0;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	mutex_init(&priv->n64joy_mutex);

	priv->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->reg_base)) {
		err = PTR_ERR(priv->reg_base);
		goto fail;
	}

	/* The controllers are not hotpluggable, so we can scan in init */
	n64joy_exec_pif(priv, scandata);

	data = (struct joydata *) priv->si_buf;

	for (i = 0; i < MAX_CONTROLLERS; i++) {
		if (!data[i].err && data[i].data >> 16 == N64_CONTROLLER_ID) {
			found++;

			dev = priv->n64joy_dev[i] = input_allocate_device();
			if (!priv->n64joy_dev[i]) {
				err = -ENOMEM;
				goto fail;
			}

			input_set_drvdata(dev, priv);

			dev->name = "N64 controller";
			dev->phys = n64joy_phys[i];
			dev->id.bustype = BUS_HOST;
			dev->id.vendor = 0;
			dev->id.product = data[i].data >> 16;
			dev->id.version = 0;
			dev->dev.parent = &pdev->dev;

			dev->open = n64joy_open;
			dev->close = n64joy_close;

			/* d-pad */
			input_set_capability(dev, EV_KEY, BTN_DPAD_UP);
			input_set_capability(dev, EV_KEY, BTN_DPAD_DOWN);
			input_set_capability(dev, EV_KEY, BTN_DPAD_LEFT);
			input_set_capability(dev, EV_KEY, BTN_DPAD_RIGHT);
			/* c buttons */
			input_set_capability(dev, EV_KEY, BTN_LEFT);
			input_set_capability(dev, EV_KEY, BTN_RIGHT);
			input_set_capability(dev, EV_KEY, BTN_FORWARD);
			input_set_capability(dev, EV_KEY, BTN_BACK);
			/* matching buttons */
			input_set_capability(dev, EV_KEY, BTN_START);
			input_set_capability(dev, EV_KEY, BTN_Z);
			/* remaining ones: a, b, l, r */
			input_set_capability(dev, EV_KEY, BTN_0);
			input_set_capability(dev, EV_KEY, BTN_1);
			input_set_capability(dev, EV_KEY, BTN_2);
			input_set_capability(dev, EV_KEY, BTN_3);

			for (j = 0; j < 2; j++)
				input_set_abs_params(dev, ABS_X + j,
						     S8_MIN, S8_MAX, 0, 0);

			err = input_register_device(dev);
			if (err) {
				input_free_device(dev);
				goto fail;
			}
		}
	}

	pr_info("%u controller(s) connected\n", found);

	if (!found)
		return -ENODEV;

	return 0;
fail:
	for (i = 0; i < MAX_CONTROLLERS; i++) {
		if (!priv->n64joy_dev[i])
			continue;
		input_unregister_device(priv->n64joy_dev[i]);
	}
	return err;
}

static struct platform_driver n64joy_driver = {
	.driver = {
		.name = "n64joy",
	},
};

static int __init n64joy_init(void)
{
	return platform_driver_probe(&n64joy_driver, n64joy_probe);
}

module_init(n64joy_init);
