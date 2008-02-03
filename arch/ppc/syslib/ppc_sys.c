/*
 * PPC System library functions
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2005 Freescale Semiconductor Inc.
 * Copyright 2005 MontaVista, Inc. by Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/string.h>
#include <linux/bootmem.h>
#include <asm/ppc_sys.h>

int (*ppc_sys_device_fixup) (struct platform_device * pdev);

static int ppc_sys_inited;
static int ppc_sys_func_inited;

static const char *ppc_sys_func_names[] = {
	[PPC_SYS_FUNC_DUMMY] = "dummy",
	[PPC_SYS_FUNC_ETH] = "eth",
	[PPC_SYS_FUNC_UART] = "uart",
	[PPC_SYS_FUNC_HLDC] = "hldc",
	[PPC_SYS_FUNC_USB] = "usb",
	[PPC_SYS_FUNC_IRDA] = "irda",
};

void __init identify_ppc_sys_by_id(u32 id)
{
	unsigned int i = 0;
	while (1) {
		if ((ppc_sys_specs[i].mask & id) == ppc_sys_specs[i].value)
			break;
		i++;
	}

	cur_ppc_sys_spec = &ppc_sys_specs[i];

	return;
}

void __init identify_ppc_sys_by_name(char *name)
{
	unsigned int i = 0;
	while (ppc_sys_specs[i].ppc_sys_name[0]) {
		if (!strcmp(ppc_sys_specs[i].ppc_sys_name, name))
			break;
		i++;
	}
	cur_ppc_sys_spec = &ppc_sys_specs[i];

	return;
}

static int __init count_sys_specs(void)
{
	int i = 0;
	while (ppc_sys_specs[i].ppc_sys_name[0])
		i++;
	return i;
}

static int __init find_chip_by_name_and_id(char *name, u32 id)
{
	int ret = -1;
	unsigned int i = 0;
	unsigned int j = 0;
	unsigned int dups = 0;

	unsigned char matched[count_sys_specs()];

	while (ppc_sys_specs[i].ppc_sys_name[0]) {
		if (!strcmp(ppc_sys_specs[i].ppc_sys_name, name))
			matched[j++] = i;
		i++;
	}

	ret = i;

	if (j != 0) {
		for (i = 0; i < j; i++) {
			if ((ppc_sys_specs[matched[i]].mask & id) ==
			    ppc_sys_specs[matched[i]].value) {
				ret = matched[i];
				dups++;
			}
		}
		ret = (dups == 1) ? ret : (-1 * dups);
	}
	return ret;
}

void __init identify_ppc_sys_by_name_and_id(char *name, u32 id)
{
	int i = find_chip_by_name_and_id(name, id);
	BUG_ON(i < 0);
	cur_ppc_sys_spec = &ppc_sys_specs[i];
}

/* Update all memory resources by paddr, call before platform_device_register */
void __init
ppc_sys_fixup_mem_resource(struct platform_device *pdev, phys_addr_t paddr)
{
	int i;
	for (i = 0; i < pdev->num_resources; i++) {
		struct resource *r = &pdev->resource[i];
		if (((r->flags & IORESOURCE_MEM) == IORESOURCE_MEM) && 
			((r->flags & PPC_SYS_IORESOURCE_FIXUPPED) != PPC_SYS_IORESOURCE_FIXUPPED)) {
			r->start += paddr;
			r->end += paddr;
			r->flags |= PPC_SYS_IORESOURCE_FIXUPPED;
		}
	}
}

/* Get platform_data pointer out of platform device, call before platform_device_register */
void *__init ppc_sys_get_pdata(enum ppc_sys_devices dev)
{
	return ppc_sys_platform_devices[dev].dev.platform_data;
}

void ppc_sys_device_remove(enum ppc_sys_devices dev)
{
	unsigned int i;

	if (ppc_sys_inited) {
		platform_device_unregister(&ppc_sys_platform_devices[dev]);
	} else {
		if (cur_ppc_sys_spec == NULL)
			return;
		for (i = 0; i < cur_ppc_sys_spec->num_devices; i++)
			if (cur_ppc_sys_spec->device_list[i] == dev)
				cur_ppc_sys_spec->device_list[i] = -1;
	}
}

/* Platform-notify mapping
 * Helper function for BSP code to assign board-specific platfom-divice bits
 */

void platform_notify_map(const struct platform_notify_dev_map *map,
			 struct device *dev)
{
	struct platform_device *pdev;
	int len, idx;
	const char *s;

	/* do nothing if no device or no bus_id */
	if (!dev || !dev->bus_id)
		return;

	/* call per device map */
	while (map->bus_id != NULL) {
		idx = -1;
		s = strrchr(dev->bus_id, '.');
		if (s != NULL) {
			idx = (int)simple_strtol(s + 1, NULL, 10);
			len = s - dev->bus_id;
		} else {
			s = dev->bus_id;
			len = strlen(dev->bus_id);
		}

		if (!strncmp(dev->bus_id, map->bus_id, len)) {
			pdev = container_of(dev, struct platform_device, dev);
			map->rtn(pdev, idx);
		}
		map++;
	}
}

/*
   Function assignment stuff.
 Intended to work as follows:
 the device name defined in foo_devices.c will be concatenated with :"func",
 where func is string map of respective function from platfom_device_func enum

 The PPC_SYS_FUNC_DUMMY function is intended to remove all assignments, making the device to appear
 in platform bus with unmodified name.
 */

/*
   Here we'll replace .name pointers with fixed-length strings
   Hereby, this should be called *before* any func stuff triggeded.
 */
void ppc_sys_device_initfunc(void)
{
	int i;
	const char *name;
	static char new_names[NUM_PPC_SYS_DEVS][BUS_ID_SIZE];
	enum ppc_sys_devices cur_dev;

	/* If inited yet, do nothing */
	if (ppc_sys_func_inited)
		return;

	for (i = 0; i < cur_ppc_sys_spec->num_devices; i++) {
		if ((cur_dev = cur_ppc_sys_spec->device_list[i]) < 0)
			continue;

		if (ppc_sys_platform_devices[cur_dev].name) {
			/*backup name */
			name = ppc_sys_platform_devices[cur_dev].name;
			strlcpy(new_names[i], name, BUS_ID_SIZE);
			ppc_sys_platform_devices[cur_dev].name = new_names[i];
		}
	}

	ppc_sys_func_inited = 1;
}

/*The "engine" of the func stuff. Here we either concat specified function string description
 to the name, or remove it if PPC_SYS_FUNC_DUMMY parameter is passed here*/
void ppc_sys_device_setfunc(enum ppc_sys_devices dev,
			    enum platform_device_func func)
{
	char *s;
	char *name = (char *)ppc_sys_platform_devices[dev].name;
	char tmp[BUS_ID_SIZE];

	if (!ppc_sys_func_inited) {
		printk(KERN_ERR "Unable to alter function - not inited!\n");
		return;
	}

	if (ppc_sys_inited) {
		platform_device_unregister(&ppc_sys_platform_devices[dev]);
	}

	if ((s = (char *)strchr(name, ':')) != NULL) {	/* reassign */
		/* Either change the name after ':' or remove func modifications */
		if (func != PPC_SYS_FUNC_DUMMY)
			strlcpy(s + 1, ppc_sys_func_names[func], BUS_ID_SIZE);
		else
			*s = 0;
	} else if (func != PPC_SYS_FUNC_DUMMY) {
		/* do assignment if it is not just "clear"  request */
		sprintf(tmp, "%s:%s", name, ppc_sys_func_names[func]);
		strlcpy(name, tmp, BUS_ID_SIZE);
	}

	if (ppc_sys_inited) {
		platform_device_register(&ppc_sys_platform_devices[dev]);
	}
}

void ppc_sys_device_disable(enum ppc_sys_devices dev)
{
	BUG_ON(cur_ppc_sys_spec == NULL);

	/*Check if it is enabled*/
	if(!(cur_ppc_sys_spec->config[dev] & PPC_SYS_CONFIG_DISABLED)) {
		if (ppc_sys_inited) {
			platform_device_unregister(&ppc_sys_platform_devices[dev]);
		}
		cur_ppc_sys_spec->config[dev] |= PPC_SYS_CONFIG_DISABLED;
	}
}

void ppc_sys_device_enable(enum ppc_sys_devices dev)
{
	BUG_ON(cur_ppc_sys_spec == NULL);

	/*Check if it is disabled*/
	if(cur_ppc_sys_spec->config[dev] & PPC_SYS_CONFIG_DISABLED) {
		if (ppc_sys_inited) {
			platform_device_register(&ppc_sys_platform_devices[dev]);
		}
		cur_ppc_sys_spec->config[dev] &= ~PPC_SYS_CONFIG_DISABLED;
	}

}

void ppc_sys_device_enable_all(void)
{
	enum ppc_sys_devices cur_dev;
	int i;

	for (i = 0; i < cur_ppc_sys_spec->num_devices; i++) {
		cur_dev = cur_ppc_sys_spec->device_list[i];
		ppc_sys_device_enable(cur_dev);
	}
}

void ppc_sys_device_disable_all(void)
{
	enum ppc_sys_devices cur_dev;
	int i;

	for (i = 0; i < cur_ppc_sys_spec->num_devices; i++) {
		cur_dev = cur_ppc_sys_spec->device_list[i];
		ppc_sys_device_disable(cur_dev);
	}
}


static int __init ppc_sys_init(void)
{
	unsigned int i, dev_id, ret = 0;

	BUG_ON(cur_ppc_sys_spec == NULL);

	for (i = 0; i < cur_ppc_sys_spec->num_devices; i++) {
		dev_id = cur_ppc_sys_spec->device_list[i];
		if ((dev_id != -1) &&
		!(cur_ppc_sys_spec->config[dev_id] & PPC_SYS_CONFIG_DISABLED)) {
			if (ppc_sys_device_fixup != NULL)
				ppc_sys_device_fixup(&ppc_sys_platform_devices
						     [dev_id]);
			if (platform_device_register
			    (&ppc_sys_platform_devices[dev_id])) {
				ret = 1;
				printk(KERN_ERR
				       "unable to register device %d\n",
				       dev_id);
			}
		}
	}

	ppc_sys_inited = 1;
	return ret;
}

subsys_initcall(ppc_sys_init);
