/*
 * Support for viafb GPIO ports.
 *
 * Copyright 2009 Jonathan Corbet <corbet@lwn.net>
 * Distributable under version 2 of the GNU General Public License.
 */

#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/via-core.h>
#include <linux/via-gpio.h>
#include <linux/export.h>

/*
 * The ports we know about.  Note that the port-25 gpios are not
 * mentioned in the datasheet.
 */

struct viafb_gpio {
	char *vg_name;	/* Data sheet name */
	u16 vg_io_port;
	u8  vg_port_index;
	int  vg_mask_shift;
};

static struct viafb_gpio viafb_all_gpios[] = {
	{
		.vg_name = "VGPIO0",  /* Guess - not in datasheet */
		.vg_io_port = VIASR,
		.vg_port_index = 0x25,
		.vg_mask_shift = 1
	},
	{
		.vg_name = "VGPIO1",
		.vg_io_port = VIASR,
		.vg_port_index = 0x25,
		.vg_mask_shift = 0
	},
	{
		.vg_name = "VGPIO2",  /* aka DISPCLKI0 */
		.vg_io_port = VIASR,
		.vg_port_index = 0x2c,
		.vg_mask_shift = 1
	},
	{
		.vg_name = "VGPIO3",  /* aka DISPCLKO0 */
		.vg_io_port = VIASR,
		.vg_port_index = 0x2c,
		.vg_mask_shift = 0
	},
	{
		.vg_name = "VGPIO4",  /* DISPCLKI1 */
		.vg_io_port = VIASR,
		.vg_port_index = 0x3d,
		.vg_mask_shift = 1
	},
	{
		.vg_name = "VGPIO5",  /* DISPCLKO1 */
		.vg_io_port = VIASR,
		.vg_port_index = 0x3d,
		.vg_mask_shift = 0
	},
};

#define VIAFB_NUM_GPIOS ARRAY_SIZE(viafb_all_gpios)

/*
 * This structure controls the active GPIOs, which may be a subset
 * of those which are known.
 */

struct viafb_gpio_cfg {
	struct gpio_chip gpio_chip;
	struct viafb_dev *vdev;
	struct viafb_gpio *active_gpios[VIAFB_NUM_GPIOS];
	const char *gpio_names[VIAFB_NUM_GPIOS];
};

/*
 * GPIO access functions
 */
static void via_gpio_set(struct gpio_chip *chip, unsigned int nr,
			 int value)
{
	struct viafb_gpio_cfg *cfg = container_of(chip,
						  struct viafb_gpio_cfg,
						  gpio_chip);
	u8 reg;
	struct viafb_gpio *gpio;
	unsigned long flags;

	spin_lock_irqsave(&cfg->vdev->reg_lock, flags);
	gpio = cfg->active_gpios[nr];
	reg = via_read_reg(VIASR, gpio->vg_port_index);
	reg |= 0x40 << gpio->vg_mask_shift;  /* output enable */
	if (value)
		reg |= 0x10 << gpio->vg_mask_shift;
	else
		reg &= ~(0x10 << gpio->vg_mask_shift);
	via_write_reg(VIASR, gpio->vg_port_index, reg);
	spin_unlock_irqrestore(&cfg->vdev->reg_lock, flags);
}

static int via_gpio_dir_out(struct gpio_chip *chip, unsigned int nr,
			    int value)
{
	via_gpio_set(chip, nr, value);
	return 0;
}

/*
 * Set the input direction.  I'm not sure this is right; we should
 * be able to do input without disabling output.
 */
static int via_gpio_dir_input(struct gpio_chip *chip, unsigned int nr)
{
	struct viafb_gpio_cfg *cfg = container_of(chip,
						  struct viafb_gpio_cfg,
						  gpio_chip);
	struct viafb_gpio *gpio;
	unsigned long flags;

	spin_lock_irqsave(&cfg->vdev->reg_lock, flags);
	gpio = cfg->active_gpios[nr];
	via_write_reg_mask(VIASR, gpio->vg_port_index, 0,
			0x40 << gpio->vg_mask_shift);
	spin_unlock_irqrestore(&cfg->vdev->reg_lock, flags);
	return 0;
}

static int via_gpio_get(struct gpio_chip *chip, unsigned int nr)
{
	struct viafb_gpio_cfg *cfg = container_of(chip,
						  struct viafb_gpio_cfg,
						  gpio_chip);
	u8 reg;
	struct viafb_gpio *gpio;
	unsigned long flags;

	spin_lock_irqsave(&cfg->vdev->reg_lock, flags);
	gpio = cfg->active_gpios[nr];
	reg = via_read_reg(VIASR, gpio->vg_port_index);
	spin_unlock_irqrestore(&cfg->vdev->reg_lock, flags);
	return reg & (0x04 << gpio->vg_mask_shift);
}


static struct viafb_gpio_cfg viafb_gpio_config = {
	.gpio_chip = {
		.label = "VIAFB onboard GPIO",
		.owner = THIS_MODULE,
		.direction_output = via_gpio_dir_out,
		.set = via_gpio_set,
		.direction_input = via_gpio_dir_input,
		.get = via_gpio_get,
		.base = -1,
		.ngpio = 0,
		.can_sleep = 0
	}
};

/*
 * Manage the software enable bit.
 */
static void viafb_gpio_enable(struct viafb_gpio *gpio)
{
	via_write_reg_mask(VIASR, gpio->vg_port_index, 0x02, 0x02);
}

static void viafb_gpio_disable(struct viafb_gpio *gpio)
{
	via_write_reg_mask(VIASR, gpio->vg_port_index, 0, 0x02);
}

#ifdef CONFIG_PM

static int viafb_gpio_suspend(void *private)
{
	return 0;
}

static int viafb_gpio_resume(void *private)
{
	int i;

	for (i = 0; i < viafb_gpio_config.gpio_chip.ngpio; i += 2)
		viafb_gpio_enable(viafb_gpio_config.active_gpios[i]);
	return 0;
}

static struct viafb_pm_hooks viafb_gpio_pm_hooks = {
	.suspend = viafb_gpio_suspend,
	.resume = viafb_gpio_resume
};
#endif /* CONFIG_PM */

/*
 * Look up a specific gpio and return the number it was assigned.
 */
int viafb_gpio_lookup(const char *name)
{
	int i;

	for (i = 0; i < viafb_gpio_config.gpio_chip.ngpio; i++)
		if (!strcmp(name, viafb_gpio_config.active_gpios[i]->vg_name))
			return viafb_gpio_config.gpio_chip.base + i;
	return -1;
}
EXPORT_SYMBOL_GPL(viafb_gpio_lookup);

/*
 * Platform device stuff.
 */
static __devinit int viafb_gpio_probe(struct platform_device *platdev)
{
	struct viafb_dev *vdev = platdev->dev.platform_data;
	struct via_port_cfg *port_cfg = vdev->port_cfg;
	int i, ngpio = 0, ret;
	struct viafb_gpio *gpio;
	unsigned long flags;

	/*
	 * Set up entries for all GPIOs which have been configured to
	 * operate as such (as opposed to as i2c ports).
	 */
	for (i = 0; i < VIAFB_NUM_PORTS; i++) {
		if (port_cfg[i].mode != VIA_MODE_GPIO)
			continue;
		for (gpio = viafb_all_gpios;
		     gpio < viafb_all_gpios + VIAFB_NUM_GPIOS; gpio++)
			if (gpio->vg_port_index == port_cfg[i].ioport_index) {
				viafb_gpio_config.active_gpios[ngpio] = gpio;
				viafb_gpio_config.gpio_names[ngpio] =
					gpio->vg_name;
				ngpio++;
			}
	}
	viafb_gpio_config.gpio_chip.ngpio = ngpio;
	viafb_gpio_config.gpio_chip.names = viafb_gpio_config.gpio_names;
	viafb_gpio_config.vdev = vdev;
	if (ngpio == 0) {
		printk(KERN_INFO "viafb: no GPIOs configured\n");
		return 0;
	}
	/*
	 * Enable the ports.  They come in pairs, with a single
	 * enable bit for both.
	 */
	spin_lock_irqsave(&viafb_gpio_config.vdev->reg_lock, flags);
	for (i = 0; i < ngpio; i += 2)
		viafb_gpio_enable(viafb_gpio_config.active_gpios[i]);
	spin_unlock_irqrestore(&viafb_gpio_config.vdev->reg_lock, flags);
	/*
	 * Get registered.
	 */
	viafb_gpio_config.gpio_chip.base = -1;  /* Dynamic */
	ret = gpiochip_add(&viafb_gpio_config.gpio_chip);
	if (ret) {
		printk(KERN_ERR "viafb: failed to add gpios (%d)\n", ret);
		viafb_gpio_config.gpio_chip.ngpio = 0;
	}
#ifdef CONFIG_PM
	viafb_pm_register(&viafb_gpio_pm_hooks);
#endif
	return ret;
}


static int viafb_gpio_remove(struct platform_device *platdev)
{
	unsigned long flags;
	int ret = 0, i;

#ifdef CONFIG_PM
	viafb_pm_unregister(&viafb_gpio_pm_hooks);
#endif

	/*
	 * Get unregistered.
	 */
	if (viafb_gpio_config.gpio_chip.ngpio > 0) {
		ret = gpiochip_remove(&viafb_gpio_config.gpio_chip);
		if (ret) { /* Somebody still using it? */
			printk(KERN_ERR "Viafb: GPIO remove failed\n");
			return ret;
		}
	}
	/*
	 * Disable the ports.
	 */
	spin_lock_irqsave(&viafb_gpio_config.vdev->reg_lock, flags);
	for (i = 0; i < viafb_gpio_config.gpio_chip.ngpio; i += 2)
		viafb_gpio_disable(viafb_gpio_config.active_gpios[i]);
	viafb_gpio_config.gpio_chip.ngpio = 0;
	spin_unlock_irqrestore(&viafb_gpio_config.vdev->reg_lock, flags);
	return ret;
}

static struct platform_driver via_gpio_driver = {
	.driver = {
		.name = "viafb-gpio",
	},
	.probe = viafb_gpio_probe,
	.remove = viafb_gpio_remove,
};

int viafb_gpio_init(void)
{
	return platform_driver_register(&via_gpio_driver);
}

void viafb_gpio_exit(void)
{
	platform_driver_unregister(&via_gpio_driver);
}
