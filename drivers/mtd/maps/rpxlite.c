/*
 * Handle mapping of the flash on the RPX Lite and CLLF boards
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>


#define WINDOW_ADDR 0xfe000000
#define WINDOW_SIZE 0x800000

static struct mtd_info *mymtd;

static struct map_info rpxlite_map = {
	.name = "RPX",
	.size = WINDOW_SIZE,
	.bankwidth = 4,
	.phys = WINDOW_ADDR,
};

int __init init_rpxlite(void)
{
	printk(KERN_NOTICE "RPX Lite or CLLF flash device: %x at %x\n", WINDOW_SIZE*4, WINDOW_ADDR);
	rpxlite_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE * 4);

	if (!rpxlite_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}
	simple_map_init(&rpxlite_map);
	mymtd = do_map_probe("cfi_probe", &rpxlite_map);
	if (mymtd) {
		mymtd->owner = THIS_MODULE;
		add_mtd_device(mymtd);
		return 0;
	}

	iounmap((void *)rpxlite_map.virt);
	return -ENXIO;
}

static void __exit cleanup_rpxlite(void)
{
	if (mymtd) {
		del_mtd_device(mymtd);
		map_destroy(mymtd);
	}
	if (rpxlite_map.virt) {
		iounmap((void *)rpxlite_map.virt);
		rpxlite_map.virt = 0;
	}
}

module_init(init_rpxlite);
module_exit(cleanup_rpxlite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arnold Christensen <AKC@pel.dk>");
MODULE_DESCRIPTION("MTD map driver for RPX Lite and CLLF boards");
