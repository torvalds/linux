/*
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2011 Thomas Chou
 * Copyright (C) 2011 Walter Goossens
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include <linux/io.h>

static int __init nios2_soc_device_init(void)
{
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;
	const char *machine;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (soc_dev_attr) {
		machine = of_flat_dt_get_machine_name();
		if (machine)
			soc_dev_attr->machine = kasprintf(GFP_KERNEL, "%s",
						machine);

		soc_dev_attr->family = "Nios II";

		soc_dev = soc_device_register(soc_dev_attr);
		if (IS_ERR(soc_dev)) {
			kfree(soc_dev_attr->machine);
			kfree(soc_dev_attr);
		}
	}

	return 0;
}

device_initcall(nios2_soc_device_init);
