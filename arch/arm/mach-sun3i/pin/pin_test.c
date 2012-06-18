/*
 * arch/arm/mach-sun3i/pin/pin_test.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

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




