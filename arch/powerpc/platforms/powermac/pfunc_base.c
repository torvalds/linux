// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/of_irq.h>

#include <asm/pmac_feature.h>
#include <asm/pmac_pfunc.h>

#undef DEBUG
#ifdef DEBUG
#define DBG(fmt...)	printk(fmt)
#else
#define DBG(fmt...)
#endif

static irqreturn_t macio_gpio_irq(int irq, void *data)
{
	pmf_do_irq(data);

	return IRQ_HANDLED;
}

static int macio_do_gpio_irq_enable(struct pmf_function *func)
{
	unsigned int irq = irq_of_parse_and_map(func->node, 0);
	if (!irq)
		return -EINVAL;
	return request_irq(irq, macio_gpio_irq, 0, func->node->name, func);
}

static int macio_do_gpio_irq_disable(struct pmf_function *func)
{
	unsigned int irq = irq_of_parse_and_map(func->node, 0);
	if (!irq)
		return -EINVAL;
	free_irq(irq, func);
	return 0;
}

static int macio_do_gpio_write(PMF_STD_ARGS, u8 value, u8 mask)
{
	u8 __iomem *addr = (u8 __iomem *)func->driver_data;
	unsigned long flags;
	u8 tmp;

	/* Check polarity */
	if (args && args->count && !args->u[0].v)
		value = ~value;

	/* Toggle the GPIO */
	raw_spin_lock_irqsave(&feature_lock, flags);
	tmp = readb(addr);
	tmp = (tmp & ~mask) | (value & mask);
	DBG("Do write 0x%02x to GPIO %pOF (%p)\n",
	    tmp, func->node, addr);
	writeb(tmp, addr);
	raw_spin_unlock_irqrestore(&feature_lock, flags);

	return 0;
}

static int macio_do_gpio_read(PMF_STD_ARGS, u8 mask, int rshift, u8 xor)
{
	u8 __iomem *addr = (u8 __iomem *)func->driver_data;
	u32 value;

	/* Check if we have room for reply */
	if (args == NULL || args->count == 0 || args->u[0].p == NULL)
		return -EINVAL;

	value = readb(addr);
	*args->u[0].p = ((value & mask) >> rshift) ^ xor;

	return 0;
}

static int macio_do_delay(PMF_STD_ARGS, u32 duration)
{
	/* assume we can sleep ! */
	msleep((duration + 999) / 1000);
	return 0;
}

static struct pmf_handlers macio_gpio_handlers = {
	.irq_enable	= macio_do_gpio_irq_enable,
	.irq_disable	= macio_do_gpio_irq_disable,
	.write_gpio	= macio_do_gpio_write,
	.read_gpio	= macio_do_gpio_read,
	.delay		= macio_do_delay,
};

static void __init macio_gpio_init_one(struct macio_chip *macio)
{
	struct device_node *gparent, *gp;

	/*
	 * Find the "gpio" parent node
	 */

	for_each_child_of_node(macio->of_node, gparent)
		if (of_node_name_eq(gparent, "gpio"))
			break;
	if (gparent == NULL)
		return;

	DBG("Installing GPIO functions for macio %pOF\n",
	    macio->of_node);

	/*
	 * Ok, got one, we dont need anything special to track them down, so
	 * we just create them all
	 */
	for_each_child_of_node(gparent, gp) {
		const u32 *reg = of_get_property(gp, "reg", NULL);
		unsigned long offset;
		if (reg == NULL)
			continue;
		offset = *reg;
		/* Deal with old style device-tree. We can safely hard code the
		 * offset for now too even if it's a bit gross ...
		 */
		if (offset < 0x50)
			offset += 0x50;
		offset += (unsigned long)macio->base;
		pmf_register_driver(gp, &macio_gpio_handlers, (void *)offset);
	}

	DBG("Calling initial GPIO functions for macio %pOF\n",
	    macio->of_node);

	/* And now we run all the init ones */
	for_each_child_of_node(gparent, gp)
		pmf_do_functions(gp, NULL, 0, PMF_FLAGS_ON_INIT, NULL);

	/* Note: We do not at this point implement the "at sleep" or "at wake"
	 * functions. I yet to find any for GPIOs anyway
	 */
}

static int macio_do_write_reg32(PMF_STD_ARGS, u32 offset, u32 value, u32 mask)
{
	struct macio_chip *macio = func->driver_data;
	unsigned long flags;

	raw_spin_lock_irqsave(&feature_lock, flags);
	MACIO_OUT32(offset, (MACIO_IN32(offset) & ~mask) | (value & mask));
	raw_spin_unlock_irqrestore(&feature_lock, flags);
	return 0;
}

static int macio_do_read_reg32(PMF_STD_ARGS, u32 offset)
{
	struct macio_chip *macio = func->driver_data;

	/* Check if we have room for reply */
	if (args == NULL || args->count == 0 || args->u[0].p == NULL)
		return -EINVAL;

	*args->u[0].p = MACIO_IN32(offset);
	return 0;
}

static int macio_do_write_reg8(PMF_STD_ARGS, u32 offset, u8 value, u8 mask)
{
	struct macio_chip *macio = func->driver_data;
	unsigned long flags;

	raw_spin_lock_irqsave(&feature_lock, flags);
	MACIO_OUT8(offset, (MACIO_IN8(offset) & ~mask) | (value & mask));
	raw_spin_unlock_irqrestore(&feature_lock, flags);
	return 0;
}

static int macio_do_read_reg8(PMF_STD_ARGS, u32 offset)
{
	struct macio_chip *macio = func->driver_data;

	/* Check if we have room for reply */
	if (args == NULL || args->count == 0 || args->u[0].p == NULL)
		return -EINVAL;

	*((u8 *)(args->u[0].p)) = MACIO_IN8(offset);
	return 0;
}

static int macio_do_read_reg32_msrx(PMF_STD_ARGS, u32 offset, u32 mask,
				    u32 shift, u32 xor)
{
	struct macio_chip *macio = func->driver_data;

	/* Check if we have room for reply */
	if (args == NULL || args->count == 0 || args->u[0].p == NULL)
		return -EINVAL;

	*args->u[0].p = ((MACIO_IN32(offset) & mask) >> shift) ^ xor;
	return 0;
}

static int macio_do_read_reg8_msrx(PMF_STD_ARGS, u32 offset, u32 mask,
				   u32 shift, u32 xor)
{
	struct macio_chip *macio = func->driver_data;

	/* Check if we have room for reply */
	if (args == NULL || args->count == 0 || args->u[0].p == NULL)
		return -EINVAL;

	*((u8 *)(args->u[0].p)) = ((MACIO_IN8(offset) & mask) >> shift) ^ xor;
	return 0;
}

static int macio_do_write_reg32_slm(PMF_STD_ARGS, u32 offset, u32 shift,
				    u32 mask)
{
	struct macio_chip *macio = func->driver_data;
	unsigned long flags;
	u32 tmp, val;

	/* Check args */
	if (args == NULL || args->count == 0)
		return -EINVAL;

	raw_spin_lock_irqsave(&feature_lock, flags);
	tmp = MACIO_IN32(offset);
	val = args->u[0].v << shift;
	tmp = (tmp & ~mask) | (val & mask);
	MACIO_OUT32(offset, tmp);
	raw_spin_unlock_irqrestore(&feature_lock, flags);
	return 0;
}

static int macio_do_write_reg8_slm(PMF_STD_ARGS, u32 offset, u32 shift,
				   u32 mask)
{
	struct macio_chip *macio = func->driver_data;
	unsigned long flags;
	u32 tmp, val;

	/* Check args */
	if (args == NULL || args->count == 0)
		return -EINVAL;

	raw_spin_lock_irqsave(&feature_lock, flags);
	tmp = MACIO_IN8(offset);
	val = args->u[0].v << shift;
	tmp = (tmp & ~mask) | (val & mask);
	MACIO_OUT8(offset, tmp);
	raw_spin_unlock_irqrestore(&feature_lock, flags);
	return 0;
}

static struct pmf_handlers macio_mmio_handlers = {
	.write_reg32		= macio_do_write_reg32,
	.read_reg32		= macio_do_read_reg32,
	.write_reg8		= macio_do_write_reg8,
	.read_reg8		= macio_do_read_reg8,
	.read_reg32_msrx	= macio_do_read_reg32_msrx,
	.read_reg8_msrx		= macio_do_read_reg8_msrx,
	.write_reg32_slm	= macio_do_write_reg32_slm,
	.write_reg8_slm		= macio_do_write_reg8_slm,
	.delay			= macio_do_delay,
};

static void __init macio_mmio_init_one(struct macio_chip *macio)
{
	DBG("Installing MMIO functions for macio %pOF\n",
	    macio->of_node);

	pmf_register_driver(macio->of_node, &macio_mmio_handlers, macio);
}

static struct device_node *unin_hwclock;

static int unin_do_write_reg32(PMF_STD_ARGS, u32 offset, u32 value, u32 mask)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&feature_lock, flags);
	/* This is fairly bogus in darwin, but it should work for our needs
	 * implemeted that way:
	 */
	UN_OUT(offset, (UN_IN(offset) & ~mask) | (value & mask));
	raw_spin_unlock_irqrestore(&feature_lock, flags);
	return 0;
}


static struct pmf_handlers unin_mmio_handlers = {
	.write_reg32		= unin_do_write_reg32,
	.delay			= macio_do_delay,
};

static void __init uninorth_install_pfunc(void)
{
	struct device_node *np;

	DBG("Installing functions for UniN %pOF\n",
	    uninorth_node);

	/*
	 * Install handlers for the bridge itself
	 */
	pmf_register_driver(uninorth_node, &unin_mmio_handlers, NULL);
	pmf_do_functions(uninorth_node, NULL, 0, PMF_FLAGS_ON_INIT, NULL);


	/*
	 * Install handlers for the hwclock child if any
	 */
	for (np = NULL; (np = of_get_next_child(uninorth_node, np)) != NULL;)
		if (of_node_name_eq(np, "hw-clock")) {
			unin_hwclock = np;
			break;
		}
	if (unin_hwclock) {
		DBG("Installing functions for UniN clock %pOF\n",
		    unin_hwclock);
		pmf_register_driver(unin_hwclock, &unin_mmio_handlers, NULL);
		pmf_do_functions(unin_hwclock, NULL, 0, PMF_FLAGS_ON_INIT,
				 NULL);
	}
}

/* We export this as the SMP code might init us early */
int __init pmac_pfunc_base_install(void)
{
	static int pfbase_inited;
	int i;

	if (pfbase_inited)
		return 0;
	pfbase_inited = 1;

	if (!machine_is(powermac))
		return 0;

	DBG("Installing base platform functions...\n");

	/*
	 * Locate mac-io chips and install handlers
	 */
	for (i = 0 ; i < MAX_MACIO_CHIPS; i++) {
		if (macio_chips[i].of_node) {
			macio_mmio_init_one(&macio_chips[i]);
			macio_gpio_init_one(&macio_chips[i]);
		}
	}

	/*
	 * Install handlers for northbridge and direct mapped hwclock
	 * if any. We do not implement the config space access callback
	 * which is only ever used for functions that we do not call in
	 * the current driver (enabling/disabling cells in U2, mostly used
	 * to restore the PCI settings, we do that differently)
	 */
	if (uninorth_node && uninorth_base)
		uninorth_install_pfunc();

	DBG("All base functions installed\n");

	return 0;
}
machine_arch_initcall(powermac, pmac_pfunc_base_install);

#ifdef CONFIG_PM

/* Those can be called by pmac_feature. Ultimately, I should use a sysdev
 * or a device, but for now, that's good enough until I sort out some
 * ordering issues. Also, we do not bother with GPIOs, as so far I yet have
 * to see a case where a GPIO function has the on-suspend or on-resume bit
 */
void pmac_pfunc_base_suspend(void)
{
	int i;

	for (i = 0 ; i < MAX_MACIO_CHIPS; i++) {
		if (macio_chips[i].of_node)
			pmf_do_functions(macio_chips[i].of_node, NULL, 0,
					 PMF_FLAGS_ON_SLEEP, NULL);
	}
	if (uninorth_node)
		pmf_do_functions(uninorth_node, NULL, 0,
				 PMF_FLAGS_ON_SLEEP, NULL);
	if (unin_hwclock)
		pmf_do_functions(unin_hwclock, NULL, 0,
				 PMF_FLAGS_ON_SLEEP, NULL);
}

void pmac_pfunc_base_resume(void)
{
	int i;

	if (unin_hwclock)
		pmf_do_functions(unin_hwclock, NULL, 0,
				 PMF_FLAGS_ON_WAKE, NULL);
	if (uninorth_node)
		pmf_do_functions(uninorth_node, NULL, 0,
				 PMF_FLAGS_ON_WAKE, NULL);
	for (i = 0 ; i < MAX_MACIO_CHIPS; i++) {
		if (macio_chips[i].of_node)
			pmf_do_functions(macio_chips[i].of_node, NULL, 0,
					 PMF_FLAGS_ON_WAKE, NULL);
	}
}

#endif /* CONFIG_PM */
