/*
 * drivers/amlogic/ion_dev/dev_ion.c
 *
 * Copyright (C) 2013 Amlogic, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/ion.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <ion_priv.h>
#include <linux/of.h>
#include <linux/of_fdt.h>

MODULE_DESCRIPTION("AMLOGIC ION driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Amlogic SH");

static unsigned debug = 0;
module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

#define dprintk(level, fmt, arg...)                 \
    do {                                \
        if (debug >= level)                 \
            printk(KERN_DEBUG "ion-dev: " fmt, ## arg);     \
    } while (0)
#define MAX_HEAP 4

static struct ion_device *idev;
static int num_heaps;
static struct ion_heap **heaps;
static struct ion_platform_heap my_ion_heap[MAX_HEAP];

static struct resource memobj;
int dev_ion_probe(struct platform_device *pdev) {
    int err;
    int i;

    struct resource *res;
    struct device_node	*of_node = pdev->dev.of_node;
    const void *name;
    int offset,size;

    num_heaps = 1;
    my_ion_heap[0].type = ION_HEAP_TYPE_SYSTEM;
    my_ion_heap[0].id = ION_HEAP_TYPE_SYSTEM;
    my_ion_heap[0].name = "vmalloc_ion";
#if 0
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
#else
    res = &memobj;
    i = find_reserve_block(of_node->name,0);
    if(i < 0){
	 name = of_get_property(of_node, "share-memory-name", NULL);
	 if(!name)
	 {
        	printk("\ndev_ion memory resource undefined1.\n");
        	return -EFAULT;
	 }
	 else
	 {
		i= find_reserve_block_by_name((char *)name);
		if(i<0)
		{
			printk("\ndev_ion memory resource undefined2.\n");
        		return -EFAULT;
		}
		name = of_get_property(of_node, "share-memory-offset", NULL);
		if(name)
			offset = of_read_ulong(name,1);
		else
		{
			printk("\ndev_ion memory resource undefined3.\n");
        		return -EFAULT;
		}
		name = of_get_property(of_node, "share-memory-size", NULL);
		if(name)
			size = of_read_ulong(name,1);
		else
		{
			printk("\ndev_ion memory resource undefined4.\n");
        		return -EFAULT;
		}


		res->start = (phys_addr_t)get_reserve_block_addr(i)+offset;
    		res->end = res->start+ size-1;

	 }
    }
    else
    {
    	res->start = (phys_addr_t)get_reserve_block_addr(i);
    	res->end = res->start+ (phys_addr_t)get_reserve_block_size(i)-1;
    }
#endif
    if (res) {
        num_heaps = 2;
        my_ion_heap[1].type = ION_HEAP_TYPE_CARVEOUT;//ION_HEAP_TYPE_CHUNK;//ION_HEAP_TYPE_CARVEOUT;
        my_ion_heap[1].id = ION_HEAP_TYPE_CARVEOUT;
        my_ion_heap[1].name = "carveout_ion";
        my_ion_heap[1].base = (ion_phys_addr_t) res->start;
        my_ion_heap[1].size = res->end - res->start + 1;
    }
    heaps = kzalloc(sizeof(struct ion_heap *) * num_heaps, GFP_KERNEL);
    idev = ion_device_create(NULL);
    if (IS_ERR_OR_NULL(idev)) {
        kfree(heaps);
        panic(0);
        return PTR_ERR(idev);
    }

    /* create the heaps as specified in the board file */
    for (i = 0; i < num_heaps; i++) {
        heaps[i] = ion_heap_create(&my_ion_heap[i]);
        if (IS_ERR_OR_NULL(heaps[i])) {
            err = PTR_ERR(heaps[i]);
            goto err;
        }
        ion_device_add_heap(idev, heaps[i]);
        dprintk(2, "add heap type:%d id:%d\n", my_ion_heap[i].type, my_ion_heap[i].id);
    }
    platform_set_drvdata(pdev, idev);
    return 0;
err:
	for (i = 0; i < num_heaps; i++) {
        if (heaps[i])
            ion_heap_destroy(heaps[i]);
    }
    kfree(heaps);
    panic(0);
    return err;
}

int dev_ion_remove(struct platform_device *pdev) {
    struct ion_device *idev = platform_get_drvdata(pdev);
    int i;

    ion_device_destroy(idev);
    for (i = 0; i < num_heaps; i++)
        ion_heap_destroy(heaps[i]);
    kfree(heaps);
    return 0;
}

static const struct of_device_id amlogic_ion_dev_dt_match[] = { { .compatible = "amlogic,ion_dev", }, { }, };

static struct platform_driver ion_driver = {
        .probe = dev_ion_probe,
        .remove = dev_ion_remove,
        .driver = {
                .name = "ion_dev",
                .owner = THIS_MODULE,
                .of_match_table = amlogic_ion_dev_dt_match
        }
};

static int __init ion_init(void)
{
    return platform_driver_register(&ion_driver);
}

static void __exit ion_exit(void)
{
    platform_driver_unregister(&ion_driver);
}

module_init(ion_init);
module_exit(ion_exit);

