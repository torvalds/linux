#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/leds.h>

static struct gpio_keys_button csb701_buttons[] = {
	{
		.code	= 0x7,
		.gpio	= 1,
		.active_low = 1,
		.desc	= "SW2",
		.type	= EV_SW,
		.wakeup = 1,
	},
};

static struct gpio_keys_platform_data csb701_gpio_keys_data = {
	.buttons = csb701_buttons,
	.nbuttons = ARRAY_SIZE(csb701_buttons),
};

static struct gpio_led csb701_leds[] = {
	{
		.name	= "csb701:yellow:heartbeat",
		.default_trigger = "heartbeat",
		.gpio	= 11,
		.active_low = 1,
	},
};

static struct platform_device csb701_gpio_keys = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev.platform_data = &csb701_gpio_keys_data,
};

static struct gpio_led_platform_data csb701_leds_gpio_data = {
	.leds		= csb701_leds,
	.num_leds	= ARRAY_SIZE(csb701_leds),
};

static struct platform_device csb701_leds_gpio = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev.platform_data = &csb701_leds_gpio_data,
};

static struct platform_device *devices[] __initdata = {
	&csb701_gpio_keys,
	&csb701_leds_gpio,
};

static int __init csb701_init(void)
{
	return platform_add_devices(devices, ARRAY_SIZE(devices));
}

module_init(csb701_init);

