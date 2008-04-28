#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/spinlock.h>

#include <asm/gpio.h>


/* Optional implementation infrastructure for GPIO interfaces.
 *
 * Platforms may want to use this if they tend to use very many GPIOs
 * that aren't part of a System-On-Chip core; or across I2C/SPI/etc.
 *
 * When kernel footprint or instruction count is an issue, simpler
 * implementations may be preferred.  The GPIO programming interface
 * allows for inlining speed-critical get/set operations for common
 * cases, so that access to SOC-integrated GPIOs can sometimes cost
 * only an instruction or two per bit.
 */


/* When debugging, extend minimal trust to callers and platform code.
 * Also emit diagnostic messages that may help initial bringup, when
 * board setup or driver bugs are most common.
 *
 * Otherwise, minimize overhead in what may be bitbanging codepaths.
 */
#ifdef	DEBUG
#define	extra_checks	1
#else
#define	extra_checks	0
#endif

/* gpio_lock prevents conflicts during gpio_desc[] table updates.
 * While any GPIO is requested, its gpio_chip is not removable;
 * each GPIO's "requested" flag serves as a lock and refcount.
 */
static DEFINE_SPINLOCK(gpio_lock);

struct gpio_desc {
	struct gpio_chip	*chip;
	unsigned long		flags;
/* flag symbols are bit numbers */
#define FLAG_REQUESTED	0
#define FLAG_IS_OUT	1

#ifdef CONFIG_DEBUG_FS
	const char		*label;
#endif
};
static struct gpio_desc gpio_desc[ARCH_NR_GPIOS];

static inline void desc_set_label(struct gpio_desc *d, const char *label)
{
#ifdef CONFIG_DEBUG_FS
	d->label = label;
#endif
}

/* Warn when drivers omit gpio_request() calls -- legal but ill-advised
 * when setting direction, and otherwise illegal.  Until board setup code
 * and drivers use explicit requests everywhere (which won't happen when
 * those calls have no teeth) we can't avoid autorequesting.  This nag
 * message should motivate switching to explicit requests...
 */
static void gpio_ensure_requested(struct gpio_desc *desc)
{
	if (test_and_set_bit(FLAG_REQUESTED, &desc->flags) == 0) {
		pr_warning("GPIO-%d autorequested\n", (int)(desc - gpio_desc));
		desc_set_label(desc, "[auto]");
		if (!try_module_get(desc->chip->owner))
			pr_err("GPIO-%d: module can't be gotten \n",
					(int)(desc - gpio_desc));
	}
}

/* caller holds gpio_lock *OR* gpio is marked as requested */
static inline struct gpio_chip *gpio_to_chip(unsigned gpio)
{
	return gpio_desc[gpio].chip;
}

/**
 * gpiochip_add() - register a gpio_chip
 * @chip: the chip to register, with chip->base initialized
 * Context: potentially before irqs or kmalloc will work
 *
 * Returns a negative errno if the chip can't be registered, such as
 * because the chip->base is invalid or already associated with a
 * different chip.  Otherwise it returns zero as a success code.
 */
int gpiochip_add(struct gpio_chip *chip)
{
	unsigned long	flags;
	int		status = 0;
	unsigned	id;

	/* NOTE chip->base negative is reserved to mean a request for
	 * dynamic allocation.  We don't currently support that.
	 */

	if (chip->base < 0 || (chip->base  + chip->ngpio) >= ARCH_NR_GPIOS) {
		status = -EINVAL;
		goto fail;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	/* these GPIO numbers must not be managed by another gpio_chip */
	for (id = chip->base; id < chip->base + chip->ngpio; id++) {
		if (gpio_desc[id].chip != NULL) {
			status = -EBUSY;
			break;
		}
	}
	if (status == 0) {
		for (id = chip->base; id < chip->base + chip->ngpio; id++) {
			gpio_desc[id].chip = chip;
			gpio_desc[id].flags = 0;
		}
	}

	spin_unlock_irqrestore(&gpio_lock, flags);
fail:
	/* failures here can mean systems won't boot... */
	if (status)
		pr_err("gpiochip_add: gpios %d..%d (%s) not registered\n",
			chip->base, chip->base + chip->ngpio,
			chip->label ? : "generic");
	return status;
}
EXPORT_SYMBOL_GPL(gpiochip_add);

/**
 * gpiochip_remove() - unregister a gpio_chip
 * @chip: the chip to unregister
 *
 * A gpio_chip with any GPIOs still requested may not be removed.
 */
int gpiochip_remove(struct gpio_chip *chip)
{
	unsigned long	flags;
	int		status = 0;
	unsigned	id;

	spin_lock_irqsave(&gpio_lock, flags);

	for (id = chip->base; id < chip->base + chip->ngpio; id++) {
		if (test_bit(FLAG_REQUESTED, &gpio_desc[id].flags)) {
			status = -EBUSY;
			break;
		}
	}
	if (status == 0) {
		for (id = chip->base; id < chip->base + chip->ngpio; id++)
			gpio_desc[id].chip = NULL;
	}

	spin_unlock_irqrestore(&gpio_lock, flags);
	return status;
}
EXPORT_SYMBOL_GPL(gpiochip_remove);


/* These "optional" allocation calls help prevent drivers from stomping
 * on each other, and help provide better diagnostics in debugfs.
 * They're called even less than the "set direction" calls.
 */
int gpio_request(unsigned gpio, const char *label)
{
	struct gpio_desc	*desc;
	int			status = -EINVAL;
	unsigned long		flags;

	spin_lock_irqsave(&gpio_lock, flags);

	if (gpio >= ARCH_NR_GPIOS)
		goto done;
	desc = &gpio_desc[gpio];
	if (desc->chip == NULL)
		goto done;

	if (!try_module_get(desc->chip->owner))
		goto done;

	/* NOTE:  gpio_request() can be called in early boot,
	 * before IRQs are enabled.
	 */

	if (test_and_set_bit(FLAG_REQUESTED, &desc->flags) == 0) {
		desc_set_label(desc, label ? : "?");
		status = 0;
	} else {
		status = -EBUSY;
		module_put(desc->chip->owner);
	}

done:
	if (status)
		pr_debug("gpio_request: gpio-%d (%s) status %d\n",
			gpio, label ? : "?", status);
	spin_unlock_irqrestore(&gpio_lock, flags);
	return status;
}
EXPORT_SYMBOL_GPL(gpio_request);

void gpio_free(unsigned gpio)
{
	unsigned long		flags;
	struct gpio_desc	*desc;

	if (gpio >= ARCH_NR_GPIOS) {
		WARN_ON(extra_checks);
		return;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	desc = &gpio_desc[gpio];
	if (desc->chip && test_and_clear_bit(FLAG_REQUESTED, &desc->flags)) {
		desc_set_label(desc, NULL);
		module_put(desc->chip->owner);
	} else
		WARN_ON(extra_checks);

	spin_unlock_irqrestore(&gpio_lock, flags);
}
EXPORT_SYMBOL_GPL(gpio_free);


/**
 * gpiochip_is_requested - return string iff signal was requested
 * @chip: controller managing the signal
 * @offset: of signal within controller's 0..(ngpio - 1) range
 *
 * Returns NULL if the GPIO is not currently requested, else a string.
 * If debugfs support is enabled, the string returned is the label passed
 * to gpio_request(); otherwise it is a meaningless constant.
 *
 * This function is for use by GPIO controller drivers.  The label can
 * help with diagnostics, and knowing that the signal is used as a GPIO
 * can help avoid accidentally multiplexing it to another controller.
 */
const char *gpiochip_is_requested(struct gpio_chip *chip, unsigned offset)
{
	unsigned gpio = chip->base + offset;

	if (gpio >= ARCH_NR_GPIOS || gpio_desc[gpio].chip != chip)
		return NULL;
	if (test_bit(FLAG_REQUESTED, &gpio_desc[gpio].flags) == 0)
		return NULL;
#ifdef CONFIG_DEBUG_FS
	return gpio_desc[gpio].label;
#else
	return "?";
#endif
}
EXPORT_SYMBOL_GPL(gpiochip_is_requested);


/* Drivers MUST set GPIO direction before making get/set calls.  In
 * some cases this is done in early boot, before IRQs are enabled.
 *
 * As a rule these aren't called more than once (except for drivers
 * using the open-drain emulation idiom) so these are natural places
 * to accumulate extra debugging checks.  Note that we can't (yet)
 * rely on gpio_request() having been called beforehand.
 */

int gpio_direction_input(unsigned gpio)
{
	unsigned long		flags;
	struct gpio_chip	*chip;
	struct gpio_desc	*desc = &gpio_desc[gpio];
	int			status = -EINVAL;

	spin_lock_irqsave(&gpio_lock, flags);

	if (gpio >= ARCH_NR_GPIOS)
		goto fail;
	chip = desc->chip;
	if (!chip || !chip->get || !chip->direction_input)
		goto fail;
	gpio -= chip->base;
	if (gpio >= chip->ngpio)
		goto fail;
	gpio_ensure_requested(desc);

	/* now we know the gpio is valid and chip won't vanish */

	spin_unlock_irqrestore(&gpio_lock, flags);

	might_sleep_if(extra_checks && chip->can_sleep);

	status = chip->direction_input(chip, gpio);
	if (status == 0)
		clear_bit(FLAG_IS_OUT, &desc->flags);
	return status;
fail:
	spin_unlock_irqrestore(&gpio_lock, flags);
	if (status)
		pr_debug("%s: gpio-%d status %d\n",
			__FUNCTION__, gpio, status);
	return status;
}
EXPORT_SYMBOL_GPL(gpio_direction_input);

int gpio_direction_output(unsigned gpio, int value)
{
	unsigned long		flags;
	struct gpio_chip	*chip;
	struct gpio_desc	*desc = &gpio_desc[gpio];
	int			status = -EINVAL;

	spin_lock_irqsave(&gpio_lock, flags);

	if (gpio >= ARCH_NR_GPIOS)
		goto fail;
	chip = desc->chip;
	if (!chip || !chip->set || !chip->direction_output)
		goto fail;
	gpio -= chip->base;
	if (gpio >= chip->ngpio)
		goto fail;
	gpio_ensure_requested(desc);

	/* now we know the gpio is valid and chip won't vanish */

	spin_unlock_irqrestore(&gpio_lock, flags);

	might_sleep_if(extra_checks && chip->can_sleep);

	status = chip->direction_output(chip, gpio, value);
	if (status == 0)
		set_bit(FLAG_IS_OUT, &desc->flags);
	return status;
fail:
	spin_unlock_irqrestore(&gpio_lock, flags);
	if (status)
		pr_debug("%s: gpio-%d status %d\n",
			__FUNCTION__, gpio, status);
	return status;
}
EXPORT_SYMBOL_GPL(gpio_direction_output);


/* I/O calls are only valid after configuration completed; the relevant
 * "is this a valid GPIO" error checks should already have been done.
 *
 * "Get" operations are often inlinable as reading a pin value register,
 * and masking the relevant bit in that register.
 *
 * When "set" operations are inlinable, they involve writing that mask to
 * one register to set a low value, or a different register to set it high.
 * Otherwise locking is needed, so there may be little value to inlining.
 *
 *------------------------------------------------------------------------
 *
 * IMPORTANT!!!  The hot paths -- get/set value -- assume that callers
 * have requested the GPIO.  That can include implicit requesting by
 * a direction setting call.  Marking a gpio as requested locks its chip
 * in memory, guaranteeing that these table lookups need no more locking
 * and that gpiochip_remove() will fail.
 *
 * REVISIT when debugging, consider adding some instrumentation to ensure
 * that the GPIO was actually requested.
 */

/**
 * __gpio_get_value() - return a gpio's value
 * @gpio: gpio whose value will be returned
 * Context: any
 *
 * This is used directly or indirectly to implement gpio_get_value().
 * It returns the zero or nonzero value provided by the associated
 * gpio_chip.get() method; or zero if no such method is provided.
 */
int __gpio_get_value(unsigned gpio)
{
	struct gpio_chip	*chip;

	chip = gpio_to_chip(gpio);
	WARN_ON(extra_checks && chip->can_sleep);
	return chip->get ? chip->get(chip, gpio - chip->base) : 0;
}
EXPORT_SYMBOL_GPL(__gpio_get_value);

/**
 * __gpio_set_value() - assign a gpio's value
 * @gpio: gpio whose value will be assigned
 * @value: value to assign
 * Context: any
 *
 * This is used directly or indirectly to implement gpio_set_value().
 * It invokes the associated gpio_chip.set() method.
 */
void __gpio_set_value(unsigned gpio, int value)
{
	struct gpio_chip	*chip;

	chip = gpio_to_chip(gpio);
	WARN_ON(extra_checks && chip->can_sleep);
	chip->set(chip, gpio - chip->base, value);
}
EXPORT_SYMBOL_GPL(__gpio_set_value);

/**
 * __gpio_cansleep() - report whether gpio value access will sleep
 * @gpio: gpio in question
 * Context: any
 *
 * This is used directly or indirectly to implement gpio_cansleep().  It
 * returns nonzero if access reading or writing the GPIO value can sleep.
 */
int __gpio_cansleep(unsigned gpio)
{
	struct gpio_chip	*chip;

	/* only call this on GPIOs that are valid! */
	chip = gpio_to_chip(gpio);

	return chip->can_sleep;
}
EXPORT_SYMBOL_GPL(__gpio_cansleep);



/* There's no value in making it easy to inline GPIO calls that may sleep.
 * Common examples include ones connected to I2C or SPI chips.
 */

int gpio_get_value_cansleep(unsigned gpio)
{
	struct gpio_chip	*chip;

	might_sleep_if(extra_checks);
	chip = gpio_to_chip(gpio);
	return chip->get(chip, gpio - chip->base);
}
EXPORT_SYMBOL_GPL(gpio_get_value_cansleep);

void gpio_set_value_cansleep(unsigned gpio, int value)
{
	struct gpio_chip	*chip;

	might_sleep_if(extra_checks);
	chip = gpio_to_chip(gpio);
	chip->set(chip, gpio - chip->base, value);
}
EXPORT_SYMBOL_GPL(gpio_set_value_cansleep);


#ifdef CONFIG_DEBUG_FS

#include <linux/debugfs.h>
#include <linux/seq_file.h>


static void gpiolib_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	unsigned		i;
	unsigned		gpio = chip->base;
	struct gpio_desc	*gdesc = &gpio_desc[gpio];
	int			is_out;

	for (i = 0; i < chip->ngpio; i++, gpio++, gdesc++) {
		if (!test_bit(FLAG_REQUESTED, &gdesc->flags))
			continue;

		is_out = test_bit(FLAG_IS_OUT, &gdesc->flags);
		seq_printf(s, " gpio-%-3d (%-12s) %s %s",
			gpio, gdesc->label,
			is_out ? "out" : "in ",
			chip->get
				? (chip->get(chip, i) ? "hi" : "lo")
				: "?  ");

		if (!is_out) {
			int		irq = gpio_to_irq(gpio);
			struct irq_desc	*desc = irq_desc + irq;

			/* This races with request_irq(), set_irq_type(),
			 * and set_irq_wake() ... but those are "rare".
			 *
			 * More significantly, trigger type flags aren't
			 * currently maintained by genirq.
			 */
			if (irq >= 0 && desc->action) {
				char *trigger;

				switch (desc->status & IRQ_TYPE_SENSE_MASK) {
				case IRQ_TYPE_NONE:
					trigger = "(default)";
					break;
				case IRQ_TYPE_EDGE_FALLING:
					trigger = "edge-falling";
					break;
				case IRQ_TYPE_EDGE_RISING:
					trigger = "edge-rising";
					break;
				case IRQ_TYPE_EDGE_BOTH:
					trigger = "edge-both";
					break;
				case IRQ_TYPE_LEVEL_HIGH:
					trigger = "level-high";
					break;
				case IRQ_TYPE_LEVEL_LOW:
					trigger = "level-low";
					break;
				default:
					trigger = "?trigger?";
					break;
				}

				seq_printf(s, " irq-%d %s%s",
					irq, trigger,
					(desc->status & IRQ_WAKEUP)
						? " wakeup" : "");
			}
		}

		seq_printf(s, "\n");
	}
}

static int gpiolib_show(struct seq_file *s, void *unused)
{
	struct gpio_chip	*chip = NULL;
	unsigned		gpio;
	int			started = 0;

	/* REVISIT this isn't locked against gpio_chip removal ... */

	for (gpio = 0; gpio < ARCH_NR_GPIOS; gpio++) {
		if (chip == gpio_desc[gpio].chip)
			continue;
		chip = gpio_desc[gpio].chip;
		if (!chip)
			continue;

		seq_printf(s, "%sGPIOs %d-%d, %s%s:\n",
				started ? "\n" : "",
				chip->base, chip->base + chip->ngpio - 1,
				chip->label ? : "generic",
				chip->can_sleep ? ", can sleep" : "");
		started = 1;
		if (chip->dbg_show)
			chip->dbg_show(s, chip);
		else
			gpiolib_dbg_show(s, chip);
	}
	return 0;
}

static int gpiolib_open(struct inode *inode, struct file *file)
{
	return single_open(file, gpiolib_show, NULL);
}

static struct file_operations gpiolib_operations = {
	.open		= gpiolib_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init gpiolib_debugfs_init(void)
{
	/* /sys/kernel/debug/gpio */
	(void) debugfs_create_file("gpio", S_IFREG | S_IRUGO,
				NULL, NULL, &gpiolib_operations);
	return 0;
}
subsys_initcall(gpiolib_debugfs_init);

#endif	/* DEBUG_FS */
