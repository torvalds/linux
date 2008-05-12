/*
 * Written by Pekka Paalanen, 2008 <pq@iki.fi>
 */
#include <linux/module.h>
#include <asm/io.h>

extern void __iomem *ioremap_nocache_trace(unsigned long offset,
						unsigned long size);
extern void iounmap_trace(volatile void __iomem *addr);

#define MODULE_NAME "testmmiotrace"

static unsigned long mmio_address;
module_param(mmio_address, ulong, 0);
MODULE_PARM_DESC(mmio_address, "Start address of the mapping of 16 kB.");

static void do_write_test(void __iomem *p)
{
	unsigned int i;
	for (i = 0; i < 256; i++)
		iowrite8(i, p + i);
	for (i = 1024; i < (5 * 1024); i += 2)
		iowrite16(i * 12 + 7, p + i);
	for (i = (5 * 1024); i < (16 * 1024); i += 4)
		iowrite32(i * 212371 + 13, p + i);
}

static void do_read_test(void __iomem *p)
{
	unsigned int i;
	volatile unsigned int v;
	for (i = 0; i < 256; i++)
		v = ioread8(p + i);
	for (i = 1024; i < (5 * 1024); i += 2)
		v = ioread16(p + i);
	for (i = (5 * 1024); i < (16 * 1024); i += 4)
		v = ioread32(p + i);
}

static void do_test(void)
{
	void __iomem *p = ioremap_nocache_trace(mmio_address, 0x4000);
	if (!p) {
		printk(KERN_ERR MODULE_NAME ": could not ioremap IO memory, "
							"aborting.\n");
		return;
	}
	do_write_test(p);
	do_read_test(p);
	iounmap_trace(p);
}

static int __init init(void)
{
	if (mmio_address == 0) {
		printk(KERN_ERR MODULE_NAME ": you have to use the module "
						"argument mmio_address.\n");
		printk(KERN_ERR MODULE_NAME ": DO NOT LOAD THIS MODULE UNLESS"
				" YOU REALLY KNOW WHAT YOU ARE DOING!\n");
		return -ENXIO;
	}

	printk(KERN_WARNING MODULE_NAME ": WARNING: mapping 16 kB @ 0x%08lx "
					"in PCI address space, and writing "
					"rubbish in there.\n", mmio_address);
	do_test();
	return 0;
}

static void __exit cleanup(void)
{
	printk(KERN_DEBUG MODULE_NAME ": unloaded.\n");
}

module_init(init);
module_exit(cleanup);
MODULE_LICENSE("GPL");
