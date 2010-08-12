
#include <linux/kernel.h>
#include <linux/module.h>

extern int if_sdio_init_module2(void);
extern void if_sdio_exit_module(void);

static int wlan_init_module(void)
{
	printk("Loading wlan driver..........\n");
	return if_sdio_init_module2();
}

static void wlan_exit_module(void)
{
	printk("move wlan driver..........\n");
	if_sdio_exit_module();
}
module_init(wlan_init_module);
module_exit(wlan_exit_module);

MODULE_LICENSE("GPL V2");
