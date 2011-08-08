/* leds-sunfire.c: SUNW,Ultra-Enterprise LED driver.
 *
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <asm/fhc.h>
#include <asm/upa.h>

#define DRIVER_NAME	"leds-sunfire"
#define PFX		DRIVER_NAME ": "

MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_DESCRIPTION("Sun Fire LED driver");
MODULE_LICENSE("GPL");

struct sunfire_led {
	struct led_classdev	led_cdev;
	void __iomem		*reg;
};
#define	to_sunfire_led(d) container_of(d, struct sunfire_led, led_cdev)

static void __clockboard_set(struct led_classdev *led_cdev,
			     enum led_brightness led_val, u8 bit)
{
	struct sunfire_led *p = to_sunfire_led(led_cdev);
	u8 reg = upa_readb(p->reg);

	switch (bit) {
	case CLOCK_CTRL_LLED:
		if (led_val)
			reg &= ~bit;
		else
			reg |= bit;
		break;

	default:
		if (led_val)
			reg |= bit;
		else
			reg &= ~bit;
		break;
	}
	upa_writeb(reg, p->reg);
}

static void clockboard_left_set(struct led_classdev *led_cdev,
				enum led_brightness led_val)
{
	__clockboard_set(led_cdev, led_val, CLOCK_CTRL_LLED);
}

static void clockboard_middle_set(struct led_classdev *led_cdev,
				  enum led_brightness led_val)
{
	__clockboard_set(led_cdev, led_val, CLOCK_CTRL_MLED);
}

static void clockboard_right_set(struct led_classdev *led_cdev,
				 enum led_brightness led_val)
{
	__clockboard_set(led_cdev, led_val, CLOCK_CTRL_RLED);
}

static void __fhc_set(struct led_classdev *led_cdev,
			     enum led_brightness led_val, u32 bit)
{
	struct sunfire_led *p = to_sunfire_led(led_cdev);
	u32 reg = upa_readl(p->reg);

	switch (bit) {
	case FHC_CONTROL_LLED:
		if (led_val)
			reg &= ~bit;
		else
			reg |= bit;
		break;

	default:
		if (led_val)
			reg |= bit;
		else
			reg &= ~bit;
		break;
	}
	upa_writel(reg, p->reg);
}

static void fhc_left_set(struct led_classdev *led_cdev,
			 enum led_brightness led_val)
{
	__fhc_set(led_cdev, led_val, FHC_CONTROL_LLED);
}

static void fhc_middle_set(struct led_classdev *led_cdev,
			   enum led_brightness led_val)
{
	__fhc_set(led_cdev, led_val, FHC_CONTROL_MLED);
}

static void fhc_right_set(struct led_classdev *led_cdev,
			  enum led_brightness led_val)
{
	__fhc_set(led_cdev, led_val, FHC_CONTROL_RLED);
}

typedef void (*set_handler)(struct led_classdev *, enum led_brightness);
struct led_type {
	const char	*name;
	set_handler	handler;
	const char	*default_trigger;
};

#define NUM_LEDS_PER_BOARD	3
struct sunfire_drvdata {
	struct sunfire_led	leds[NUM_LEDS_PER_BOARD];
};

static int __devinit sunfire_led_generic_probe(struct platform_device *pdev,
					       struct led_type *types)
{
	struct sunfire_drvdata *p;
	int i, err;

	if (pdev->num_resources != 1) {
		printk(KERN_ERR PFX "Wrong number of resources %d, should be 1\n",
		       pdev->num_resources);
		err = -EINVAL;
		goto out;
	}

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		printk(KERN_ERR PFX "Could not allocate struct sunfire_drvdata\n");
		err = -ENOMEM;
		goto out;
	}

	for (i = 0; i < NUM_LEDS_PER_BOARD; i++) {
		struct led_classdev *lp = &p->leds[i].led_cdev;

		p->leds[i].reg = (void __iomem *) pdev->resource[0].start;
		lp->name = types[i].name;
		lp->brightness = LED_FULL;
		lp->brightness_set = types[i].handler;
		lp->default_trigger = types[i].default_trigger;

		err = led_classdev_register(&pdev->dev, lp);
		if (err) {
			printk(KERN_ERR PFX "Could not register %s LED\n",
			       lp->name);
			goto out_unregister_led_cdevs;
		}
	}

	dev_set_drvdata(&pdev->dev, p);

	return 0;

out_unregister_led_cdevs:
	for (i--; i >= 0; i--)
		led_classdev_unregister(&p->leds[i].led_cdev);
	kfree(p);
out:
	return err;
}

static int __devexit sunfire_led_generic_remove(struct platform_device *pdev)
{
	struct sunfire_drvdata *p = dev_get_drvdata(&pdev->dev);
	int i;

	for (i = 0; i < NUM_LEDS_PER_BOARD; i++)
		led_classdev_unregister(&p->leds[i].led_cdev);

	kfree(p);

	return 0;
}

static struct led_type clockboard_led_types[NUM_LEDS_PER_BOARD] = {
	{
		.name		= "clockboard-left",
		.handler	= clockboard_left_set,
	},
	{
		.name		= "clockboard-middle",
		.handler	= clockboard_middle_set,
	},
	{
		.name		= "clockboard-right",
		.handler	= clockboard_right_set,
		.default_trigger= "heartbeat",
	},
};

static int __devinit sunfire_clockboard_led_probe(struct platform_device *pdev)
{
	return sunfire_led_generic_probe(pdev, clockboard_led_types);
}

static struct led_type fhc_led_types[NUM_LEDS_PER_BOARD] = {
	{
		.name		= "fhc-left",
		.handler	= fhc_left_set,
	},
	{
		.name		= "fhc-middle",
		.handler	= fhc_middle_set,
	},
	{
		.name		= "fhc-right",
		.handler	= fhc_right_set,
		.default_trigger= "heartbeat",
	},
};

static int __devinit sunfire_fhc_led_probe(struct platform_device *pdev)
{
	return sunfire_led_generic_probe(pdev, fhc_led_types);
}

MODULE_ALIAS("platform:sunfire-clockboard-leds");
MODULE_ALIAS("platform:sunfire-fhc-leds");

static struct platform_driver sunfire_clockboard_led_driver = {
	.probe		= sunfire_clockboard_led_probe,
	.remove		= __devexit_p(sunfire_led_generic_remove),
	.driver		= {
		.name	= "sunfire-clockboard-leds",
		.owner	= THIS_MODULE,
	},
};

static struct platform_driver sunfire_fhc_led_driver = {
	.probe		= sunfire_fhc_led_probe,
	.remove		= __devexit_p(sunfire_led_generic_remove),
	.driver		= {
		.name	= "sunfire-fhc-leds",
		.owner	= THIS_MODULE,
	},
};

static int __init sunfire_leds_init(void)
{
	int err = platform_driver_register(&sunfire_clockboard_led_driver);

	if (err) {
		printk(KERN_ERR PFX "Could not register clock board LED driver\n");
		return err;
	}

	err = platform_driver_register(&sunfire_fhc_led_driver);
	if (err) {
		printk(KERN_ERR PFX "Could not register FHC LED driver\n");
		platform_driver_unregister(&sunfire_clockboard_led_driver);
	}

	return err;
}

static void __exit sunfire_leds_exit(void)
{
	platform_driver_unregister(&sunfire_clockboard_led_driver);
	platform_driver_unregister(&sunfire_fhc_led_driver);
}

module_init(sunfire_leds_init);
module_exit(sunfire_leds_exit);
