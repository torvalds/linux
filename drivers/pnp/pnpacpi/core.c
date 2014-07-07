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

#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/pnp.h>
#include <linux/slab.h>
#include <linux/mod_devicetable.h>

#include "../base.h"
#include "pnpacpi.h"

static int num;

/*
 * Compatible Device IDs
 */
#define TEST_HEX(c) \
	if (!(('0' <= (c) && (c) <= '9') || ('A' <= (c) && (c) <= 'F'))) \
		return 0
#define TEST_ALPHA(c) \
	if (!('A' <= (c) && (c) <= 'Z')) \
		return 0
static int __init ispnpidacpi(const char *id)
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

static int pnpacpi_get_resources(struct pnp_dev *dev)
{
	pnp_dbg(&dev->dev, "get resources\n");
	return pnpacpi_parse_allocated_resource(dev);
}

static int pnpacpi_set_resources(struct pnp_dev *dev)
{
	struct acpi_device *acpi_dev;
	acpi_handle handle;
	int ret = 0;

	pnp_dbg(&dev->dev, "set resources\n");

	handle = ACPI_HANDLE(&dev->dev);
	if (!handle || acpi_bus_get_device(handle, &acpi_dev)) {
		dev_dbg(&dev->dev, "ACPI device not found in %s!\n", __func__);
		return -ENODEV;
	}

	if (WARN_ON_ONCE(acpi_dev != dev->data))
		dev->data = acpi_dev;

	if (acpi_has_method(handle, METHOD_NAME__SRS)) {
		struct acpi_buffer buffer;

		ret = pnpacpi_build_resource_template(dev, &buffer);
		if (ret)
			return ret;

		ret = pnpacpi_encode_resources(dev, &buffer);
		if (!ret) {
			acpi_status status;

			status = acpi_set_current_resources(handle, &buffer);
			if (ACPI_FAILURE(status))
				ret = -EIO;
		}
		kfree(buffer.pointer);
	}
	if (!ret && acpi_bus_power_manageable(handle))
		ret = acpi_bus_set_power(handle, ACPI_STATE_D0);

	return ret;
}

static int pnpacpi_disable_resources(struct pnp_dev *dev)
{
	struct acpi_device *acpi_dev;
	acpi_handle handle;
	acpi_status status;

	dev_dbg(&dev->dev, "disable resources\n");

	handle = ACPI_HANDLE(&dev->dev);
	if (!handle || acpi_bus_get_device(handle, &acpi_dev)) {
		dev_dbg(&dev->dev, "ACPI device not found in %s!\n", __func__);
		return 0;
	}

	/* acpi_unregister_gsi(pnp_irq(dev, 0)); */
	if (acpi_bus_power_manageable(handle))
		acpi_bus_set_power(handle, ACPI_STATE_D3_COLD);

	/* continue even if acpi_bus_set_power() fails */
	status = acpi_evaluate_object(handle, "_DIS", NULL, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND)
		return -ENODEV;

	return 0;
}

#ifdef CONFIG_ACPI_SLEEP
static bool pnpacpi_can_wakeup(struct pnp_dev *dev)
{
	struct acpi_device *acpi_dev;
	acpi_handle handle;

	handle = ACPI_HANDLE(&dev->dev);
	if (!handle || acpi_bus_get_device(handle, &acpi_dev)) {
		dev_dbg(&dev->dev, "ACPI device not found in %s!\n", __func__);
		return false;
	}

	return acpi_bus_can_wakeup(handle);
}

static int pnpacpi_suspend(struct pnp_dev *dev, pm_message_t state)
{
	struct acpi_device *acpi_dev;
	acpi_handle handle;
	int error = 0;

	handle = ACPI_HANDLE(&dev->dev);
	if (!handle || acpi_bus_get_device(handle, &acpi_dev)) {
		dev_dbg(&dev->dev, "ACPI device not found in %s!\n", __func__);
		return 0;
	}

	if (device_can_wakeup(&dev->dev)) {
		error = acpi_pm_device_sleep_wake(&dev->dev,
				device_may_wakeup(&dev->dev));
		if (error)
			return error;
	}

	if (acpi_bus_power_manageable(handle)) {
		int power_state = acpi_pm_device_sleep_state(&dev->dev, NULL,
							ACPI_STATE_D3_COLD);
		if (power_state < 0)
			power_state = (state.event == PM_EVENT_ON) ?
					ACPI_STATE_D0 : ACPI_STATE_D3_COLD;

		/*
		 * acpi_bus_set_power() often fails (keyboard port can't be
		 * powered-down?), and in any case, our return value is ignored
		 * by pnp_bus_suspend().  Hence we don't revert the wakeup
		 * setting if the set_power fails.
		 */
		error = acpi_bus_set_power(handle, power_state);
	}

	return error;
}

static int pnpacpi_resume(struct pnp_dev *dev)
{
	struct acpi_device *acpi_dev;
	acpi_handle handle = ACPI_HANDLE(&dev->dev);
	int error = 0;

	if (!handle || acpi_bus_get_device(handle, &acpi_dev)) {
		dev_dbg(&dev->dev, "ACPI device not found in %s!\n", __func__);
		return -ENODEV;
	}

	if (device_may_wakeup(&dev->dev))
		acpi_pm_device_sleep_wake(&dev->dev, false);

	if (acpi_bus_power_manageable(handle))
		error = acpi_bus_set_power(handle, ACPI_STATE_D0);

	return error;
}
#endif

struct pnp_protocol pnpacpi_protocol = {
	.name	 = "Plug and Play ACPI",
	.get	 = pnpacpi_get_resources,
	.set	 = pnpacpi_set_resources,
	.disable = pnpacpi_disable_resources,
#ifdef CONFIG_ACPI_SLEEP
	.can_wakeup = pnpacpi_can_wakeup,
	.suspend = pnpacpi_suspend,
	.resume = pnpacpi_resume,
#endif
};
EXPORT_SYMBOL(pnpacpi_protocol);

static char *__init pnpacpi_get_id(struct acpi_device *device)
{
	struct acpi_hardware_id *id;

	list_for_each_entry(id, &device->pnp.ids, list) {
		if (ispnpidacpi(id->id))
			return id->id;
	}

	return NULL;
}

static int __init pnpacpi_add_device(struct acpi_device *device)
{
	struct pnp_dev *dev;
	char *pnpid;
	struct acpi_hardware_id *id;
	int error;

	/* Skip devices that are already bound */
	if (device->physical_node_count)
		return 0;

	/*
	 * If a PnPacpi device is not present , the device
	 * driver should not be loaded.
	 */
	if (!acpi_has_method(device->handle, "_CRS"))
		return 0;

	pnpid = pnpacpi_get_id(device);
	if (!pnpid)
		return 0;

	if (!device->status.present)
		return 0;

	dev = pnp_alloc_dev(&pnpacpi_protocol, num, pnpid);
	if (!dev)
		return -ENOMEM;

	dev->data = device;
	/* .enabled means the device can decode the resources */
	dev->active = device->status.enabled;
	if (acpi_has_method(device->handle, "_SRS"))
		dev->capabilities |= PNP_CONFIGURABLE;
	dev->capabilities |= PNP_READ;
	if (device->flags.dynamic_status && (dev->capabilities & PNP_CONFIGURABLE))
		dev->capabilities |= PNP_WRITE;
	if (device->flags.removable)
		dev->capabilities |= PNP_REMOVABLE;
	if (acpi_has_method(device->handle, "_DIS"))
		dev->capabilities |= PNP_DISABLE;

	if (strlen(acpi_device_name(device)))
		strncpy(dev->name, acpi_device_name(device), sizeof(dev->name));
	else
		strncpy(dev->name, acpi_device_bid(device), sizeof(dev->name));

	if (dev->active)
		pnpacpi_parse_allocated_resource(dev);

	if (dev->capabilities & PNP_CONFIGURABLE)
		pnpacpi_parse_resource_option_data(dev);

	list_for_each_entry(id, &device->pnp.ids, list) {
		if (!strcmp(id->id, pnpid))
			continue;
		if (!ispnpidacpi(id->id))
			continue;
		pnp_add_id(dev, id->id);
	}

	/* clear out the damaged flags */
	if (!dev->active)
		pnp_init_resources(dev);

	error = pnp_add_device(dev);
	if (error) {
		put_device(&dev->dev);
		return error;
	}

	error = acpi_bind_one(&dev->dev, device);

	num++;

	return error;
}

static acpi_status __init pnpacpi_add_device_handler(acpi_handle handle,
						     u32 lvl, void *context,
						     void **rv)
{
	struct acpi_device *device;

	if (acpi_bus_get_device(handle, &device))
		return AE_CTRL_DEPTH;
	if (acpi_is_pnp_device(device))
		pnpacpi_add_device(device);
	return AE_OK;
}

int pnpacpi_disabled __initdata;
static int __init pnpacpi_init(void)
{
	if (acpi_disabled || pnpacpi_disabled) {
		printk(KERN_INFO "pnp: PnP ACPI: disabled\n");
		return 0;
	}
	printk(KERN_INFO "pnp: PnP ACPI init\n");
	pnp_register_protocol(&pnpacpi_protocol);
	acpi_get_devices(NULL, pnpacpi_add_device_handler, NULL, NULL);
	printk(KERN_INFO "pnp: PnP ACPI: found %d devices\n", num);
	pnp_platform_devices = 1;
	return 0;
}

fs_initcall(pnpacpi_init);

static int __init pnpacpi_setup(char *str)
{
	if (str == NULL)
		return 1;
	if (!strncmp(str, "off", 3))
		pnpacpi_disabled = 1;
	return 1;
}

__setup("pnpacpi=", pnpacpi_setup);
