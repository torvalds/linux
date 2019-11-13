// SPDX--License-Identifier: GPL-2.0

#include <asm/platform_early.h>
#include <linux/mod_devicetable.h>
#include <linux/pm.h>

static __initdata LIST_HEAD(sh_early_platform_driver_list);
static __initdata LIST_HEAD(sh_early_platform_device_list);

static const struct platform_device_id *
platform_match_id(const struct platform_device_id *id,
		  struct platform_device *pdev)
{
	while (id->name[0]) {
		if (strcmp(pdev->name, id->name) == 0) {
			pdev->id_entry = id;
			return id;
		}
		id++;
	}
	return NULL;
}

static int platform_match(struct device *dev, struct device_driver *drv)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct platform_driver *pdrv = to_platform_driver(drv);

	/* When driver_override is set, only bind to the matching driver */
	if (pdev->driver_override)
		return !strcmp(pdev->driver_override, drv->name);

	/* Then try to match against the id table */
	if (pdrv->id_table)
		return platform_match_id(pdrv->id_table, pdev) != NULL;

	/* fall-back to driver name match */
	return (strcmp(pdev->name, drv->name) == 0);
}

#ifdef CONFIG_PM
static void device_pm_init_common(struct device *dev)
{
	if (!dev->power.early_init) {
		spin_lock_init(&dev->power.lock);
		dev->power.qos = NULL;
		dev->power.early_init = true;
	}
}

static void pm_runtime_early_init(struct device *dev)
{
	dev->power.disable_depth = 1;
	device_pm_init_common(dev);
}
#else
static void pm_runtime_early_init(struct device *dev) {}
#endif

/**
 * sh_early_platform_driver_register - register early platform driver
 * @epdrv: sh_early_platform driver structure
 * @buf: string passed from early_param()
 *
 * Helper function for sh_early_platform_init() / sh_early_platform_init_buffer()
 */
int __init sh_early_platform_driver_register(struct sh_early_platform_driver *epdrv,
					  char *buf)
{
	char *tmp;
	int n;

	/* Simply add the driver to the end of the global list.
	 * Drivers will by default be put on the list in compiled-in order.
	 */
	if (!epdrv->list.next) {
		INIT_LIST_HEAD(&epdrv->list);
		list_add_tail(&epdrv->list, &sh_early_platform_driver_list);
	}

	/* If the user has specified device then make sure the driver
	 * gets prioritized. The driver of the last device specified on
	 * command line will be put first on the list.
	 */
	n = strlen(epdrv->pdrv->driver.name);
	if (buf && !strncmp(buf, epdrv->pdrv->driver.name, n)) {
		list_move(&epdrv->list, &sh_early_platform_driver_list);

		/* Allow passing parameters after device name */
		if (buf[n] == '\0' || buf[n] == ',')
			epdrv->requested_id = -1;
		else {
			epdrv->requested_id = simple_strtoul(&buf[n + 1],
							     &tmp, 10);

			if (buf[n] != '.' || (tmp == &buf[n + 1])) {
				epdrv->requested_id = EARLY_PLATFORM_ID_ERROR;
				n = 0;
			} else
				n += strcspn(&buf[n + 1], ",") + 1;
		}

		if (buf[n] == ',')
			n++;

		if (epdrv->bufsize) {
			memcpy(epdrv->buffer, &buf[n],
			       min_t(int, epdrv->bufsize, strlen(&buf[n]) + 1));
			epdrv->buffer[epdrv->bufsize - 1] = '\0';
		}
	}

	return 0;
}

/**
 * sh_early_platform_add_devices - adds a number of early platform devices
 * @devs: array of early platform devices to add
 * @num: number of early platform devices in array
 *
 * Used by early architecture code to register early platform devices and
 * their platform data.
 */
void __init sh_early_platform_add_devices(struct platform_device **devs, int num)
{
	struct device *dev;
	int i;

	/* simply add the devices to list */
	for (i = 0; i < num; i++) {
		dev = &devs[i]->dev;

		if (!dev->devres_head.next) {
			pm_runtime_early_init(dev);
			INIT_LIST_HEAD(&dev->devres_head);
			list_add_tail(&dev->devres_head,
				      &sh_early_platform_device_list);
		}
	}
}

/**
 * sh_early_platform_driver_register_all - register early platform drivers
 * @class_str: string to identify early platform driver class
 *
 * Used by architecture code to register all early platform drivers
 * for a certain class. If omitted then only early platform drivers
 * with matching kernel command line class parameters will be registered.
 */
void __init sh_early_platform_driver_register_all(char *class_str)
{
	/* The "class_str" parameter may or may not be present on the kernel
	 * command line. If it is present then there may be more than one
	 * matching parameter.
	 *
	 * Since we register our early platform drivers using early_param()
	 * we need to make sure that they also get registered in the case
	 * when the parameter is missing from the kernel command line.
	 *
	 * We use parse_early_options() to make sure the early_param() gets
	 * called at least once. The early_param() may be called more than
	 * once since the name of the preferred device may be specified on
	 * the kernel command line. sh_early_platform_driver_register() handles
	 * this case for us.
	 */
	parse_early_options(class_str);
}

/**
 * sh_early_platform_match - find early platform device matching driver
 * @epdrv: early platform driver structure
 * @id: id to match against
 */
static struct platform_device * __init
sh_early_platform_match(struct sh_early_platform_driver *epdrv, int id)
{
	struct platform_device *pd;

	list_for_each_entry(pd, &sh_early_platform_device_list, dev.devres_head)
		if (platform_match(&pd->dev, &epdrv->pdrv->driver))
			if (pd->id == id)
				return pd;

	return NULL;
}

/**
 * sh_early_platform_left - check if early platform driver has matching devices
 * @epdrv: early platform driver structure
 * @id: return true if id or above exists
 */
static int __init sh_early_platform_left(struct sh_early_platform_driver *epdrv,
				       int id)
{
	struct platform_device *pd;

	list_for_each_entry(pd, &sh_early_platform_device_list, dev.devres_head)
		if (platform_match(&pd->dev, &epdrv->pdrv->driver))
			if (pd->id >= id)
				return 1;

	return 0;
}

/**
 * sh_early_platform_driver_probe_id - probe drivers matching class_str and id
 * @class_str: string to identify early platform driver class
 * @id: id to match against
 * @nr_probe: number of platform devices to successfully probe before exiting
 */
static int __init sh_early_platform_driver_probe_id(char *class_str,
						 int id,
						 int nr_probe)
{
	struct sh_early_platform_driver *epdrv;
	struct platform_device *match;
	int match_id;
	int n = 0;
	int left = 0;

	list_for_each_entry(epdrv, &sh_early_platform_driver_list, list) {
		/* only use drivers matching our class_str */
		if (strcmp(class_str, epdrv->class_str))
			continue;

		if (id == -2) {
			match_id = epdrv->requested_id;
			left = 1;

		} else {
			match_id = id;
			left += sh_early_platform_left(epdrv, id);

			/* skip requested id */
			switch (epdrv->requested_id) {
			case EARLY_PLATFORM_ID_ERROR:
			case EARLY_PLATFORM_ID_UNSET:
				break;
			default:
				if (epdrv->requested_id == id)
					match_id = EARLY_PLATFORM_ID_UNSET;
			}
		}

		switch (match_id) {
		case EARLY_PLATFORM_ID_ERROR:
			pr_warn("%s: unable to parse %s parameter\n",
				class_str, epdrv->pdrv->driver.name);
			/* fall-through */
		case EARLY_PLATFORM_ID_UNSET:
			match = NULL;
			break;
		default:
			match = sh_early_platform_match(epdrv, match_id);
		}

		if (match) {
			/*
			 * Set up a sensible init_name to enable
			 * dev_name() and others to be used before the
			 * rest of the driver core is initialized.
			 */
			if (!match->dev.init_name && slab_is_available()) {
				if (match->id != -1)
					match->dev.init_name =
						kasprintf(GFP_KERNEL, "%s.%d",
							  match->name,
							  match->id);
				else
					match->dev.init_name =
						kasprintf(GFP_KERNEL, "%s",
							  match->name);

				if (!match->dev.init_name)
					return -ENOMEM;
			}

			if (epdrv->pdrv->probe(match))
				pr_warn("%s: unable to probe %s early.\n",
					class_str, match->name);
			else
				n++;
		}

		if (n >= nr_probe)
			break;
	}

	if (left)
		return n;
	else
		return -ENODEV;
}

/**
 * sh_early_platform_driver_probe - probe a class of registered drivers
 * @class_str: string to identify early platform driver class
 * @nr_probe: number of platform devices to successfully probe before exiting
 * @user_only: only probe user specified early platform devices
 *
 * Used by architecture code to probe registered early platform drivers
 * within a certain class. For probe to happen a registered early platform
 * device matching a registered early platform driver is needed.
 */
int __init sh_early_platform_driver_probe(char *class_str,
				       int nr_probe,
				       int user_only)
{
	int k, n, i;

	n = 0;
	for (i = -2; n < nr_probe; i++) {
		k = sh_early_platform_driver_probe_id(class_str, i, nr_probe - n);

		if (k < 0)
			break;

		n += k;

		if (user_only)
			break;
	}

	return n;
}

/**
 * sh_early_platform_cleanup - clean up early platform code
 */
static int __init sh_early_platform_cleanup(void)
{
	struct platform_device *pd, *pd2;

	/* clean up the devres list used to chain devices */
	list_for_each_entry_safe(pd, pd2, &sh_early_platform_device_list,
				 dev.devres_head) {
		list_del(&pd->dev.devres_head);
		memset(&pd->dev.devres_head, 0, sizeof(pd->dev.devres_head));
	}

	return 0;
}
/*
 * This must happen once after all early devices are probed but before probing
 * real platform devices.
 */
subsys_initcall(sh_early_platform_cleanup);
