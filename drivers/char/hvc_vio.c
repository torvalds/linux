/*
 * vio driver interface to hvc_console.c
 *
 * This code was moved here to allow the remaing code to be reused as a
 * generic polling mode with semi-reliable transport driver core to the
 * console and tty subsystems.
 *
 *
 * Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * Copyright (C) 2001 Paul Mackerras <paulus@au.ibm.com>, IBM
 * Copyright (C) 2004 Benjamin Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
 * Copyright (C) 2004 IBM Corporation
 *
 * Additional Author(s):
 *  Ryan S. Arnold <rsa@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/types.h>
#include <linux/init.h>

#include <asm/hvconsole.h>
#include <asm/vio.h>
#include <asm/prom.h>
#include <asm/firmware.h>

#include "hvc_console.h"

char hvc_driver_name[] = "hvc_console";

static struct vio_device_id hvc_driver_table[] __devinitdata = {
	{"serial", "hvterm1"},
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, hvc_driver_table);

static int filtered_get_chars(uint32_t vtermno, char *buf, int count)
{
	unsigned long got;
	int i;

	/*
	 * Vio firmware will read up to SIZE_VIO_GET_CHARS at its own discretion
	 * so we play safe and avoid the situation where got > count which could
	 * overload the flip buffer.
	 */
	if (count < SIZE_VIO_GET_CHARS)
		return -EAGAIN;

	got = hvc_get_chars(vtermno, buf, count);

	/*
	 * Work around a HV bug where it gives us a null
	 * after every \r.  -- paulus
	 */
	for (i = 1; i < got; ++i) {
		if (buf[i] == 0 && buf[i-1] == '\r') {
			--got;
			if (i < got)
				memmove(&buf[i], &buf[i+1],
					got - i);
		}
	}
	return got;
}

static struct hv_ops hvc_get_put_ops = {
	.get_chars = filtered_get_chars,
	.put_chars = hvc_put_chars,
	.notifier_add = notifier_add_irq,
	.notifier_del = notifier_del_irq,
	.notifier_hangup = notifier_hangup_irq,
};

static int __devinit hvc_vio_probe(struct vio_dev *vdev,
				const struct vio_device_id *id)
{
	struct hvc_struct *hp;

	/* probed with invalid parameters. */
	if (!vdev || !id)
		return -EPERM;

	hp = hvc_alloc(vdev->unit_address, vdev->irq, &hvc_get_put_ops,
			MAX_VIO_PUT_CHARS);
	if (IS_ERR(hp))
		return PTR_ERR(hp);
	dev_set_drvdata(&vdev->dev, hp);

	return 0;
}

static int __devexit hvc_vio_remove(struct vio_dev *vdev)
{
	struct hvc_struct *hp = dev_get_drvdata(&vdev->dev);

	return hvc_remove(hp);
}

static struct vio_driver hvc_vio_driver = {
	.id_table	= hvc_driver_table,
	.probe		= hvc_vio_probe,
	.remove		= hvc_vio_remove,
	.driver		= {
		.name	= hvc_driver_name,
		.owner	= THIS_MODULE,
	}
};

static int hvc_vio_init(void)
{
	int rc;

	if (firmware_has_feature(FW_FEATURE_ISERIES))
		return -EIO;

	/* Register as a vio device to receive callbacks */
	rc = vio_register_driver(&hvc_vio_driver);

	return rc;
}
module_init(hvc_vio_init); /* after drivers/char/hvc_console.c */

static void hvc_vio_exit(void)
{
	vio_unregister_driver(&hvc_vio_driver);
}
module_exit(hvc_vio_exit);

/* the device tree order defines our numbering */
static int hvc_find_vtys(void)
{
	struct device_node *vty;
	int num_found = 0;

	for (vty = of_find_node_by_name(NULL, "vty"); vty != NULL;
			vty = of_find_node_by_name(vty, "vty")) {
		const uint32_t *vtermno;

		/* We have statically defined space for only a certain number
		 * of console adapters.
		 */
		if (num_found >= MAX_NR_HVC_CONSOLES) {
			of_node_put(vty);
			break;
		}

		vtermno = of_get_property(vty, "reg", NULL);
		if (!vtermno)
			continue;

		if (of_device_is_compatible(vty, "hvterm1")) {
			hvc_instantiate(*vtermno, num_found, &hvc_get_put_ops);
			++num_found;
		}
	}

	return num_found;
}
console_initcall(hvc_find_vtys);
