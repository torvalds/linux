
#include <linux/kernel.h>
#include <linux/module.h>

extern void if_sdio_init_module2(void);

static int wlan_init_module(void)
{
	printk("Loading wlan driver..........\n");
	if_sdio_init_module2();

	return 0;
}

module_init(wlan_init_module);

//module_exit(wlan_exit_module);
