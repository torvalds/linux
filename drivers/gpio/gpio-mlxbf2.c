// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2020-2021 NVIDIA CORPORATION & AFFILIATES
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/resource.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/*
 * There are 3 YU GPIO blocks:
 * gpio[0]: HOST_GPIO0->HOST_GPIO31
 * gpio[1]: HOST_GPIO32->HOST_GPIO63
 * gpio[2]: HOST_GPIO64->HOST_GPIO69
 */
#define MLXBF2_GPIO_MAX_PINS_PER_BLOCK 32

/*
 * arm_gpio_lock register:
 * bit[31]	lock status: active if set
 * bit[15:0]	set lock
 * The lock is enabled only if 0xd42f is written to this field
 */
#define YU_ARM_GPIO_LOCK_ADDR		0x2801088
#define YU_ARM_GPIO_LOCK_SIZE		0x8
#define YU_LOCK_ACTIVE_BIT(val)		(val >> 31)
#define YU_ARM_GPIO_LOCK_ACQUIRE	0xd42f
#define YU_ARM_GPIO_LOCK_RELEASE	0x0

/*
 * gpio[x] block registers and their offset
 */
#define YU_GPIO_DATAIN			0x04
#define YU_GPIO_MODE1			0x08
#define YU_GPIO_MODE0			0x0c
#define YU_GPIO_DATASET			0x14
#define YU_GPIO_DATACLEAR		0x18
#define YU_GPIO_CAUSE_RISE_EN		0x44
#define YU_GPIO_CAUSE_FALL_EN		0x48
#define YU_GPIO_MODE1_CLEAR		0x50
#define YU_GPIO_MODE0_SET		0x54
#define YU_GPIO_MODE0_CLEAR		0x58
#define YU_GPIO_CAUSE_OR_CAUSE_EVTEN0	0x80
#define YU_GPIO_CAUSE_OR_EVTEN0		0x94
#define YU_GPIO_CAUSE_OR_CLRCAUSE	0x98

struct mlxbf2_gpio_context_save_regs {
	u32 gpio_mode0;
	u32 gpio_mode1;
};

/* BlueField-2 gpio block context structure. */
struct mlxbf2_gpio_context {
	struct gpio_chip gc;
	struct irq_chip irq_chip;

	/* YU GPIO blocks address */
	void __iomem *gpio_io;

	struct mlxbf2_gpio_context_save_regs *csave_regs;
};

/* BlueField-2 gpio shared structure. */
struct mlxbf2_gpio_param {
	void __iomem *io;
	struct resource *res;
	struct mutex *lock;
};

static struct resource yu_arm_gpio_lock_res =
	DEFINE_RES_MEM_NAMED(YU_ARM_GPIO_LOCK_ADDR, YU_ARM_GPIO_LOCK_SIZE, "YU_ARM_GPIO_LOCK");

static DEFINE_MUTEX(yu_arm_gpio_lock_mutex);

static struct mlxbf2_gpio_param yu_arm_gpio_lock_param = {
	.res = &yu_arm_gpio_lock_res,
	.lock = &yu_arm_gpio_lock_mutex,
};

/* Request memory region and map yu_arm_gpio_lock resource */
static int mlxbf2_gpio_get_lock_res(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	resource_size_t size;
	int ret = 0;

	mutex_lock(yu_arm_gpio_lock_param.lock);

	/* Check if the memory map already exists */
	if (yu_arm_gpio_lock_param.io)
		goto exit;

	res = yu_arm_gpio_lock_param.res;
	size = resource_size(res);

	if (!devm_request_mem_region(dev, res->start, size, res->name)) {
		ret = -EFAULT;
		goto exit;
	}

	yu_arm_gpio_lock_param.io = devm_ioremap(dev, res->start, size);
	if (!yu_arm_gpio_lock_param.io)
		ret = -ENOMEM;

exit:
	mutex_unlock(yu_arm_gpio_lock_param.lock);

	return ret;
}

/*
 * Acquire the YU arm_gpio_lock to be able to change the direction
 * mode. If the lock_active bit is already set, return an error.
 */
static int mlxbf2_gpio_lock_acquire(struct mlxbf2_gpio_context *gs)
{
	u32 arm_gpio_lock_val;

	mutex_lock(yu_arm_gpio_lock_param.lock);
	spin_lock(&gs->gc.bgpio_lock);

	arm_gpio_lock_val = readl(yu_arm_gpio_lock_param.io);

	/*
	 * When lock active bit[31] is set, ModeX is write enabled
	 */
	if (YU_LOCK_ACTIVE_BIT(arm_gpio_lock_val)) {
		spin_unlock(&gs->gc.bgpio_lock);
		mutex_unlock(yu_arm_gpio_lock_param.lock);
		return -EINVAL;
	}

	writel(YU_ARM_GPIO_LOCK_ACQUIRE, yu_arm_gpio_lock_param.io);

	return 0;
}

/*
 * Release the YU arm_gpio_lock after changing the direction mode.
 */
static void mlxbf2_gpio_lock_release(struct mlxbf2_gpio_context *gs)
	__releases(&gs->gc.bgpio_lock)
	__releases(yu_arm_gpio_lock_param.lock)
{
	writel(YU_ARM_GPIO_LOCK_RELEASE, yu_arm_gpio_lock_param.io);
	spin_unlock(&gs->gc.bgpio_lock);
	mutex_unlock(yu_arm_gpio_lock_param.lock);
}

/*
 * mode0 and mode1 are both locked by the gpio_lock field.
 *
 * Together, mode0 and mode1 define the gpio Mode dependeing also
 * on Reg_DataOut.
 *
 * {mode1,mode0}:{Reg_DataOut=0,Reg_DataOut=1}->{DataOut=0,DataOut=1}
 *
 * {0,0}:Reg_DataOut{0,1}->{Z,Z} Input PAD
 * {0,1}:Reg_DataOut{0,1}->{0,1} Full drive Output PAD
 * {1,0}:Reg_DataOut{0,1}->{0,Z} 0-set PAD to low, 1-float
 * {1,1}:Reg_DataOut{0,1}->{Z,1} 0-float, 1-set PAD to high
 */

/*
 * Set input direction:
 * {mode1,mode0} = {0,0}
 */
static int mlxbf2_gpio_direction_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	struct mlxbf2_gpio_context *gs = gpiochip_get_data(chip);
	int ret;

	/*
	 * Although the arm_gpio_lock was set in the probe function, check again
	 * if it is still enabled to be able to write to the ModeX registers.
	 */
	ret = mlxbf2_gpio_lock_acquire(gs);
	if (ret < 0)
		return ret;

	writel(BIT(offset), gs->gpio_io + YU_GPIO_MODE0_CLEAR);
	writel(BIT(offset), gs->gpio_io + YU_GPIO_MODE1_CLEAR);

	mlxbf2_gpio_lock_release(gs);

	return ret;
}

/*
 * Set output direction:
 * {mode1,mode0} = {0,1}
 */
static int mlxbf2_gpio_direction_output(struct gpio_chip *chip,
					unsigned int offset,
					int value)
{
	struct mlxbf2_gpio_context *gs = gpiochip_get_data(chip);
	int ret = 0;

	/*
	 * Although the arm_gpio_lock was set in the probe function,
	 * check again it is still enabled to be able to write to the
	 * ModeX registers.
	 */
	ret = mlxbf2_gpio_lock_acquire(gs);
	if (ret < 0)
		return ret;

	writel(BIT(offset), gs->gpio_io + YU_GPIO_MODE1_CLEAR);
	writel(BIT(offset), gs->gpio_io + YU_GPIO_MODE0_SET);

	mlxbf2_gpio_lock_release(gs);

	return ret;
}

static void mlxbf2_gpio_irq_enable(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct mlxbf2_gpio_context *gs = gpiochip_get_data(gc);
	int offset = irqd_to_hwirq(irqd);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&gs->gc.bgpio_lock, flags);
	val = readl(gs->gpio_io + YU_GPIO_CAUSE_OR_CLRCAUSE);
	val |= BIT(offset);
	writel(val, gs->gpio_io + YU_GPIO_CAUSE_OR_CLRCAUSE);

	val = readl(gs->gpio_io + YU_GPIO_CAUSE_OR_EVTEN0);
	val |= BIT(offset);
	writel(val, gs->gpio_io + YU_GPIO_CAUSE_OR_EVTEN0);
	spin_unlock_irqrestore(&gs->gc.bgpio_lock, flags);
}

static void mlxbf2_gpio_irq_disable(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct mlxbf2_gpio_context *gs = gpiochip_get_data(gc);
	int offset = irqd_to_hwirq(irqd);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&gs->gc.bgpio_lock, flags);
	val = readl(gs->gpio_io + YU_GPIO_CAUSE_OR_EVTEN0);
	val &= ~BIT(offset);
	writel(val, gs->gpio_io + YU_GPIO_CAUSE_OR_EVTEN0);
	spin_unlock_irqrestore(&gs->gc.bgpio_lock, flags);
}

static irqreturn_t mlxbf2_gpio_irq_handler(int irq, void *ptr)
{
	struct mlxbf2_gpio_context *gs = ptr;
	struct gpio_chip *gc = &gs->gc;
	unsigned long pending;
	u32 level;

	pending = readl(gs->gpio_io + YU_GPIO_CAUSE_OR_CAUSE_EVTEN0);
	writel(pending, gs->gpio_io + YU_GPIO_CAUSE_OR_CLRCAUSE);

	for_each_set_bit(level, &pending, gc->ngpio) {
		int gpio_irq = irq_find_mapping(gc->irq.domain, level);
		generic_handle_irq(gpio_irq);
	}

	return IRQ_RETVAL(pending);
}

static int
mlxbf2_gpio_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct mlxbf2_gpio_context *gs = gpiochip_get_data(gc);
	int offset = irqd_to_hwirq(irqd);
	unsigned long flags;
	bool fall = false;
	bool rise = false;
	u32 val;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_BOTH:
		fall = true;
		rise = true;
		break;
	case IRQ_TYPE_EDGE_RISING:
		rise = true;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		fall = true;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&gs->gc.bgpio_lock, flags);
	if (fall) {
		val = readl(gs->gpio_io + YU_GPIO_CAUSE_FALL_EN);
		val |= BIT(offset);
		writel(val, gs->gpio_io + YU_GPIO_CAUSE_FALL_EN);
	}

	if (rise) {
		val = readl(gs->gpio_io + YU_GPIO_CAUSE_RISE_EN);
		val |= BIT(offset);
		writel(val, gs->gpio_io + YU_GPIO_CAUSE_RISE_EN);
	}
	spin_unlock_irqrestore(&gs->gc.bgpio_lock, flags);

	return 0;
}

/* BlueField-2 GPIO driver initialization routine. */
static int
mlxbf2_gpio_probe(struct platform_device *pdev)
{
	struct mlxbf2_gpio_context *gs;
	struct device *dev = &pdev->dev;
	struct gpio_irq_chip *girq;
	struct gpio_chip *gc;
	unsigned int npins;
	const char *name;
	int ret, irq;

	name = dev_name(dev);

	gs = devm_kzalloc(dev, sizeof(*gs), GFP_KERNEL);
	if (!gs)
		return -ENOMEM;

	/* YU GPIO block address */
	gs->gpio_io = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gs->gpio_io))
		return PTR_ERR(gs->gpio_io);

	ret = mlxbf2_gpio_get_lock_res(pdev);
	if (ret) {
		dev_err(dev, "Failed to get yu_arm_gpio_lock resource\n");
		return ret;
	}

	if (device_property_read_u32(dev, "npins", &npins))
		npins = MLXBF2_GPIO_MAX_PINS_PER_BLOCK;

	gc = &gs->gc;

	ret = bgpio_init(gc, dev, 4,
			gs->gpio_io + YU_GPIO_DATAIN,
			gs->gpio_io + YU_GPIO_DATASET,
			gs->gpio_io + YU_GPIO_DATACLEAR,
			NULL,
			NULL,
			0);

	if (ret) {
		dev_err(dev, "bgpio_init failed\n");
		return ret;
	}

	gc->direction_input = mlxbf2_gpio_direction_input;
	gc->direction_output = mlxbf2_gpio_direction_output;
	gc->ngpio = npins;
	gc->owner = THIS_MODULE;

	irq = platform_get_irq(pdev, 0);
	if (irq >= 0) {
		gs->irq_chip.name = name;
		gs->irq_chip.irq_set_type = mlxbf2_gpio_irq_set_type;
		gs->irq_chip.irq_enable = mlxbf2_gpio_irq_enable;
		gs->irq_chip.irq_disable = mlxbf2_gpio_irq_disable;

		girq = &gs->gc.irq;
		girq->chip = &gs->irq_chip;
		girq->handler = handle_simple_irq;
		girq->default_type = IRQ_TYPE_NONE;
		/* This will let us handle the parent IRQ in the driver */
		girq->num_parents = 0;
		girq->parents = NULL;
		girq->parent_handler = NULL;

		/*
		 * Directly request the irq here instead of passing
		 * a flow-handler because the irq is shared.
		 */
		ret = devm_request_irq(dev, irq, mlxbf2_gpio_irq_handler,
				       IRQF_SHARED, name, gs);
		if (ret) {
			dev_err(dev, "failed to request IRQ");
			return ret;
		}
	}

	platform_set_drvdata(pdev, gs);

	ret = devm_gpiochip_add_data(dev, &gs->gc, gs);
	if (ret) {
		dev_err(dev, "Failed adding memory mapped gpiochip\n");
		return ret;
	}

	return 0;
}

static int __maybe_unused mlxbf2_gpio_suspend(struct device *dev)
{
	struct mlxbf2_gpio_context *gs = dev_get_drvdata(dev);

	gs->csave_regs->gpio_mode0 = readl(gs->gpio_io +
		YU_GPIO_MODE0);
	gs->csave_regs->gpio_mode1 = readl(gs->gpio_io +
		YU_GPIO_MODE1);

	return 0;
}

static int __maybe_unused mlxbf2_gpio_resume(struct device *dev)
{
	struct mlxbf2_gpio_context *gs = dev_get_drvdata(dev);

	writel(gs->csave_regs->gpio_mode0, gs->gpio_io +
		YU_GPIO_MODE0);
	writel(gs->csave_regs->gpio_mode1, gs->gpio_io +
		YU_GPIO_MODE1);

	return 0;
}
static SIMPLE_DEV_PM_OPS(mlxbf2_pm_ops, mlxbf2_gpio_suspend, mlxbf2_gpio_resume);

static const struct acpi_device_id __maybe_unused mlxbf2_gpio_acpi_match[] = {
	{ "MLNXBF22", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, mlxbf2_gpio_acpi_match);

static struct platform_driver mlxbf2_gpio_driver = {
	.driver = {
		.name = "mlxbf2_gpio",
		.acpi_match_table = mlxbf2_gpio_acpi_match,
		.pm = &mlxbf2_pm_ops,
	},
	.probe    = mlxbf2_gpio_probe,
};

module_platform_driver(mlxbf2_gpio_driver);

MODULE_DESCRIPTION("Mellanox BlueField-2 GPIO Driver");
MODULE_AUTHOR("Asmaa Mnebhi <asmaa@nvidia.com>");
MODULE_LICENSE("GPL v2");
