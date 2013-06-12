/* Temporary placeholder so the empty code can build */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>

static struct bus_type fmc_bus_type = {
	.name = "fmc",
};

static int fmc_init(void)
{
	return bus_register(&fmc_bus_type);
}

static void fmc_exit(void)
{
	bus_unregister(&fmc_bus_type);
}

module_init(fmc_init);
module_exit(fmc_exit);

MODULE_LICENSE("GPL");
