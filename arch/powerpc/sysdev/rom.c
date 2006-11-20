/*
 * ROM device registration
 *
 * (C) 2006 MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/kernel.h>
#include <asm/of_device.h>

static int __init powerpc_flash_init(void)
{
	struct device_node *node = NULL;

	/*
	 * Register all the devices which type is "rom"
	 */
	while ((node = of_find_node_by_type(node, "rom")) != NULL) {
		if (node->name == NULL) {
			printk(KERN_WARNING "powerpc_flash_init: found 'rom' "
				"device, but with no name, skipping...\n");
			continue;
		}
		of_platform_device_create(node, node->name, NULL);
	}
	return 0;
}

arch_initcall(powerpc_flash_init);
