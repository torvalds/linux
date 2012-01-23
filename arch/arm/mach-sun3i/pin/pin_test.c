#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <asm/io.h>

#include <mach/gpio_v2.h>
#include <mach/script_v2.h>


static user_gpio_set_t gpio_set[38];

static int __init aw_pin_test_init(void)
{
	int ret;
	u32 gpio_handle;
	printk("aw_pin_test_init: enter\n");

	ret = script_parser_mainkey_get_gpio_cfg("uart_para", (void *)gpio_set, 38);
	if(!ret) {
		gpio_handle = gpio_request(gpio_set, 38);
		printk("gpio_handle=0x%08x, ret=%d\n", gpio_handle,ret);

		ret = gpio_release(gpio_handle, 2);
		printk("gpio_Release: ret=%d\n", ret);
	}
	else {
		printk("ERR: script_parser_mainkey_get_gpio_cfg\n");
	}

	return 0;
}
module_init(aw_pin_test_init);

static void __exit aw_pin_test_exit(void)
{
	printk("aw_pin_test_exit\n");
}
module_exit(aw_pin_test_exit);




