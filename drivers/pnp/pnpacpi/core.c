/*
 * pnpacpi -- PnP ACPI driver
 *
 * Copyright (c) 2004 Matthieu Castet <castet.matthieu@free.fr>
 * Copyright (c) 2004 Li Shaohua <shaohua.li@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/acpi.h>
#include <linux/pnp.h>
#include <linux/mod_devicetable.h>
#include <acpi/acpi_bus.h>
#include <acpi/actypes.h>

#include "pnpacpi.h"

static int num = 0;

/* We need only to blacklist devices that have already an acpi driver that
 * can't use pnp layer. We don't need to blacklist device that are directly
 * used by the kernel (PCI root, ...), as it is harmless and there were
 * already present in pnpbios. But there is an exception for devices that
 * have irqs (PIC, Timer) because we call acpi_register_gsi.
 * Finally, only devices that have a CRS method need to be in this list.
 */
static struct __initdata acpi_device_id excluded_id_list[] = {
	{"PNP0C09", 0},		/* EC */
	{"PNP0C0F", 0},		/* Link device */
	{"PNP0000", 0},		/* PIC */
	{"PNP0100", 0},		/* Timer */
	{"", 0},
};

static inline int is_exclusive_device(struct acpi_device *dev)
{
	return (!acpi_match_device_ids(dev, excluded_id_list));
}

/*
 * Compatible Device IDs
 */
#define TEST_HEX(c) \
	if (!(('0' <= (c) && (c) <= '9') || ('A' <= (c) && (c) <= 'F'))) \
		return 0
#define TEST_ALPHA(c) \
	if (!('@' <= (c) || (c) <= 'Z')) \
		return 0
static int __init ispnpidacpi(char *id)
{
	TEST_ALPHA(id[0]);
	TEST_ALPHA(id[1]);
	TEST_ALPHA(id[2]);
	TEST_HEX(id[3]);
	TEST_HEX(id[4]);
	TEST_HEX(id[5]);
	TEST_HEX(id[6]);
	if (id[7] != '\0')
		return 0;
	return 1;
}

static void __init pnpidacpi_to_pnpid(char *id, char *str)
{
	str[0] = id[0];
	str[1] = id[1];
	str[2] = id[2];
	str[3] = tolower(id[3]);
	str[4] = tolower(id[4]);
	str[5] = tolower(id[5]);
	str[6] = tolower(id[6]);
	str[7] = '\0';
}

static int pnpacpi_get_resources(struct pnp_dev *dev,
				 struct pnp_resource_table *res)
{
	acpi_status status;

	status = pnpacpi_parse_allocated_resource((acpi_handle) dev->data,
						  &dev->res);
	return ACPI_FAILURE(status) ? -ENODEV : 0;
}

static int pnpacpi_set_resources(struct pnp_dev *dev,
				 struct pnp_resource_table *res)
{
	acpi_handle handle = dev->data;
	struct acpi_buffer buffer;
	int ret = 0;
	acpi_status status;

	ret = pnpacpi_build_resource_template(handle, &buffer);
	if (ret)
		return ret;
	ret = pnpacpi_encode_resources(res, &buffer);
	if (ret) {
		kfree(buffer.pointer);
		return ret;
	}
	status = acpi_set_current_resources(handle, &buffer);
	if (ACPI_FAILURE(status))
		ret = -EINVAL;
	kfree(buffer.pointer);
	return ret;
}

static int pnpacpi_disable_resources(struct pnp_dev *dev)
{
	acpi_status status;

	/* acpi_unregister_gsi(pnp_irq(dev, 0)); */
	status = acpi_evaluate_object((acpi_handle) dev->data,
				      "_DIS", NULL, NULL);
	return ACPI_FAILURE(status) ? -ENODEV : 0;
}

#ifdef CONFIG_ACPI_SLEEP
static int pnpacpi_suspend(struct pnp_dev *dev, pm_message_t state)
{
	int power_state;

	power_state = acpi_pm_device_sleep_state(&dev->dev,
						device_may_wakeup(&dev->dev),
						NULL);
	if (power_state < 0)
		power_state = (state.event == PM_EVENT_ON) ?
				ACPI_STATE_D0 : ACPI_STATE_D3;

	return acpi_bus_set_power((acpi_handle) dev->data, power_state);
}

static int pnpacpi_resume(struct pnp_dev *dev)
{
	return acpi_bus_set_power((acpi_handle) dev->data, ACPI_STATE_D0);
}
#endif

static struct pnp_protocol pnpacpi_protocol = {
	.name	 = "Plug and Play ACPI",
	.get	 = pnpacpi_get_resources,
	.set	 = pnpacpi_set_resources,
	.disable = pnpacpi_disable_resources,
#ifdef CONFIG_ACPI_SLEEP
	.suspend = pnpacpi_suspend,
	.resume = pnpacpi_resume,
#endif
};

static int __init pnpacpi_add_device(struct acpi_device *device)
{
	acpi_handle temp = NULL;
	acpi_status status;
	struct pnp_id *dev_id;
	struct pnp_dev *dev;

	status = acpi_get_handle(device->handle, "_CRS", &temp);
	if (ACPI_FAILURE(status) || !ispnpidacpi(acpi_device_hid(device)) ||
	    is_exclusive_device(device))
		return 0;

	dev = kzalloc(sizeof(struct pnp_dev), GFP_KERNEL);
	if (!dev) {
		pnp_err("Out of memory");
		return -ENOMEM;
	}
	dev->data = device->handle;
	/* .enabled means the device can decode the resources */
	dev->active = device->status.enabled;
	status = acpi_get_handle(device->handle, "_SRS", &temp);
	if (ACPI_SUCCESS(status))
		dev->capabilities |= PNP_CONFIGURABLE;
	dev->capabilities |= PNP_READ;
	if (device->flags.dynamic_status)
		dev->capabilities |= PNP_WRITE;
	if (device->flags.removable)
		dev->capabilities |= PNP_REMOVABLE;
	status = acpi_get_handle(device->handle, "_DIS", &temp);
	if (ACPI_SUCCESS(status))
		dev->capabilities |= PNP_DISABLE;

	dev->protocol = &pnpacpi_protocol;

	if (strlen(acpi_device_name(device)))
		strncpy(dev->name, acpi_device_name(device), sizeof(dev->name));
	else
		strncpy(dev->name, acpi_device_bid(device), sizeof(dev->name));

	dev->number = num;

	/* set the initial values for the PnP device */
	dev_id = kzalloc(sizeof(struct pnp_id), GFP_KERNEL);
	if (!dev_id)
		goto err;
	pnpidacpi_to_pnpid(acpi_device_hid(device), dev_id->id);
	pnp_add_id(dev_id, dev);

	if (dev->active) {
		/* parse allocated resource */
		status = pnpacpi_parse_allocated_resource(device->handle,
							  &dev->res);
		if (ACPI_FAILURE(status) && (status != AE_NOT_FOUND)) {
			pnp_err("PnPACPI: METHOD_NAME__CRS failure for %s",
				dev_id->id);
			goto err1;
		}
	}

	if (dev->capabilities & PNP_CONFIGURABLE) {
		status = pnpacpi_parse_resource_option_data(device->handle,
							    dev);
		if (ACPI_FAILURE(status) && (status != AE_NOT_FOUND)) {
			pnp_err("PnPACPI: METHOD_NAME__PRS failure for %s",
				dev_id->id);
			goto err1;
		}
	}

	/* parse compatible ids */
	if (device->flags.compatible_ids) {
		struct acpi_compatible_id_list *cid_list = device->pnp.cid_list;
		int i;

		for (i = 0; i < cid_list->count; i++) {
			if (!ispnpidacpi(cid_list->id[i].value))
				continue;
			dev_id = kzalloc(sizeof(struct pnp_id), GFP_KERNEL);
			if (!dev_id)
				continue;

			pnpidacpi_to_pnpid(cid_list->id[i].value, dev_id->id);
			pnp_add_id(dev_id, dev);
		}
	}

	/* clear out the damaged flags */
	if (!dev->active)
		pnp_init_resource_table(&dev->res);
	pnp_add_device(dev);
	num++;

	return AE_OK;
err1:
	kfree(dev_id);
err:
	kfree(dev);
	return -EINVAL;
}

static acpi_status __init pnpacpi_add_device_handler(acpi_handle handle,
						     u32 lvl, void *context,
						     void **rv)
{
	struct acpi_device *device;

	if (!acpi_bus_get_device(handle, &device))
		pnpacpi_add_device(device);
	else
		return AE_CTRL_DEPTH;
	return AE_OK;
}

static int __init acpi_pnp_match(struct device *dev, void *_pnp)
{
	struct acpi_device *acpi = to_acpi_device(dev);
	struct pnp_dev *pnp = _pnp;

	/* true means it matched */
	return acpi->flags.hardware_id
	    && !acpi_get_physical_device(acpi->handle)
	    && compare_pnp_id(pnp->id, acpi->pnp.hardware_id);
}

static int __init acpi_pnp_find_device(struct device *dev, acpi_handle * handle)
{
	struct device *adev;
	struct acpi_device *acpi;

	adev = bus_find_device(&acpi_bus_type, NULL,
			       to_pnp_dev(dev), acpi_pnp_match);
	if (!adev)
		return -ENODEV;

	acpi = to_acpi_device(adev);
	*handle = acpi->handle;
	put_device(adev);
	return 0;
}

/* complete initialization of a PNPACPI device includes having
 * pnpdev->dev.archdata.acpi_handle point to its ACPI sibling.
 */
static struct acpi_bus_type __initdata acpi_pnp_bus = {
	.bus	     = &pnp_bus_type,
	.find_device = acpi_pnp_find_device,
};

int pnpacpi_disabled __initdata;
static int __init pnpacpi_init(void)
{
	if (acpi_disabled || pnpacpi_disabled) {
		pnp_info("PnP ACPI: disabled");
		return 0;
	}
	pnp_info("PnP ACPI init");
	pnp_register_protocol(&pnpacpi_protocol);
	register_acpi_bus_type(&acpi_pnp_bus);
	acpi_get_devices(NULL, pnpacpi_add_device_handler, NULL, NULL);
	pnp_info("PnP ACPI: found %d devices", num);
	unregister_acpi_bus_type(&acpi_pnp_bus);
	pnp_platform_devices = 1;
	return 0;
}

subsys_initcall(pnpacpi_init);

static int __init pnpacpi_setup(char *str)
{
	if (str == NULL)
		return 1;
	if (!strncmp(str, "off", 3))
		pnpacpi_disabled = 1;
	return 1;
}

__setup("pnpacpi=", pnpacpi_setup);
