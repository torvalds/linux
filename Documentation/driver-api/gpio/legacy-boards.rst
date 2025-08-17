Supporting Legacy Boards
========================

Many drivers in the kernel, such as ``leds-gpio`` and ``gpio-keys``, are
migrating away from using board-specific ``platform_data`` to a unified device
properties interface. This interface allows drivers to be simpler and more
generic, as they can query properties in a standardized way.

On modern systems, these properties are provided via device tree. However, some
older platforms have not been converted to device tree and instead rely on
board files to describe their hardware configuration. To bridge this gap and
allow these legacy boards to work with modern, generic drivers, the kernel
provides a mechanism called **software nodes**.

This document provides a guide on how to convert a legacy board file from using
``platform_data`` and ``gpiod_lookup_table`` to the modern software node
approach for describing GPIO-connected devices.

The Core Idea: Software Nodes
-----------------------------

Software nodes allow board-specific code to construct an in-memory,
device-tree-like structure using struct software_node and struct
property_entry. This structure can then be associated with a platform device,
allowing drivers to use the standard device properties API (e.g.,
device_property_read_u32(), device_property_read_string()) to query
configuration, just as they would on an ACPI or device tree system.

The gpiolib code has support for handling software nodes, so that if GPIO is
described properly, as detailed in the section below, then regular gpiolib APIs,
such as gpiod_get(), gpiod_get_optional(), and others will work.

Requirements for GPIO Properties
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When using software nodes to describe GPIO connections, the following
requirements must be met for the GPIO core to correctly resolve the reference:

1.  **The GPIO controller's software node "name" must match the controller's
    "label".** The gpiolib core uses this name to find the corresponding
    struct gpio_chip at runtime.
    This software node has to be registered, but need not be attached to the
    device representing the GPIO controller that is providing the GPIO in
    question. It may be left as a "free floating" node.

2.  **The GPIO property must be a reference.** The ``PROPERTY_ENTRY_GPIO()``
    macro handles this as it is an alias for ``PROPERTY_ENTRY_REF()``.

3.  **The reference must have exactly two arguments:**

    - The first argument is the GPIO offset within the controller.
    - The second argument is the flags for the GPIO line (e.g.,
      GPIO_ACTIVE_HIGH, GPIO_ACTIVE_LOW).

The ``PROPERTY_ENTRY_GPIO()`` macro is the preferred way of defining GPIO
properties in software nodes.

Conversion Example
------------------

Let's walk through an example of converting a board file that defines a GPIO-
connected LED and a button.

Before: Using Platform Data
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A typical legacy board file might look like this:

.. code-block:: c

  #include <linux/platform_device.h>
  #include <linux/leds.h>
  #include <linux/gpio_keys.h>
  #include <linux/gpio/machine.h>

  #define MYBOARD_GPIO_CONTROLLER "gpio-foo"

  /* LED setup */
  static const struct gpio_led myboard_leds[] = {
  	{
  		.name = "myboard:green:status",
  		.default_trigger = "heartbeat",
  	},
  };

  static const struct gpio_led_platform_data myboard_leds_pdata = {
  	.num_leds = ARRAY_SIZE(myboard_leds),
  	.leds = myboard_leds,
  };

  static struct gpiod_lookup_table myboard_leds_gpios = {
  	.dev_id = "leds-gpio",
  	.table = {
  		GPIO_LOOKUP_IDX(MYBOARD_GPIO_CONTROLLER, 42, NULL, 0, GPIO_ACTIVE_HIGH),
  		{ },
  	},
  };

  /* Button setup */
  static struct gpio_keys_button myboard_buttons[] = {
  	{
  		.code = KEY_WPS_BUTTON,
  		.desc = "WPS Button",
  		.active_low = 1,
  	},
  };

  static const struct gpio_keys_platform_data myboard_buttons_pdata = {
  	.buttons = myboard_buttons,
  	.nbuttons = ARRAY_SIZE(myboard_buttons),
  };

  static struct gpiod_lookup_table myboard_buttons_gpios = {
  	.dev_id = "gpio-keys",
  	.table = {
  		GPIO_LOOKUP_IDX(MYBOARD_GPIO_CONTROLLER, 15, NULL, 0, GPIO_ACTIVE_LOW),
  		{ },
  	},
  };

  /* Device registration */
  static int __init myboard_init(void)
  {
  	gpiod_add_lookup_table(&myboard_leds_gpios);
  	gpiod_add_lookup_table(&myboard_buttons_gpios);

  	platform_device_register_data(NULL, "leds-gpio", -1,
  				      &myboard_leds_pdata, sizeof(myboard_leds_pdata));
  	platform_device_register_data(NULL, "gpio-keys", -1,
  				      &myboard_buttons_pdata, sizeof(myboard_buttons_pdata));

  	return 0;
  }

After: Using Software Nodes
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here is how the same configuration can be expressed using software nodes.

Step 1: Define the GPIO Controller Node
***************************************

First, define a software node that represents the GPIO controller that the
LEDs and buttons are connected to. The ``name`` of this node must match the
name of the driver for the GPIO controller (e.g., "gpio-foo").

.. code-block:: c

  #include <linux/property.h>
  #include <linux/gpio/property.h>

  #define MYBOARD_GPIO_CONTROLLER "gpio-foo"

  static const struct software_node myboard_gpio_controller_node = {
  	.name = MYBOARD_GPIO_CONTROLLER,
  };

Step 2: Define Consumer Device Nodes and Properties
***************************************************

Next, define the software nodes for the consumer devices (the LEDs and buttons).
This involves creating a parent node for each device type and child nodes for
each individual LED or button.

.. code-block:: c

  /* LED setup */
  static const struct software_node myboard_leds_node = {
  	.name = "myboard-leds",
  };

  static const struct property_entry myboard_status_led_props[] = {
  	PROPERTY_ENTRY_STRING("label", "myboard:green:status"),
  	PROPERTY_ENTRY_STRING("linux,default-trigger", "heartbeat"),
  	PROPERTY_ENTRY_GPIO("gpios", &myboard_gpio_controller_node, 42, GPIO_ACTIVE_HIGH),
  	{ }
  };

  static const struct software_node myboard_status_led_swnode = {
  	.name = "status-led",
  	.parent = &myboard_leds_node,
  	.properties = myboard_status_led_props,
  };

  /* Button setup */
  static const struct software_node myboard_keys_node = {
  	.name = "myboard-keys",
  };

  static const struct property_entry myboard_wps_button_props[] = {
  	PROPERTY_ENTRY_STRING("label", "WPS Button"),
  	PROPERTY_ENTRY_U32("linux,code", KEY_WPS_BUTTON),
  	PROPERTY_ENTRY_GPIO("gpios", &myboard_gpio_controller_node, 15, GPIO_ACTIVE_LOW),
  	{ }
  };

  static const struct software_node myboard_wps_button_swnode = {
  	.name = "wps-button",
  	.parent = &myboard_keys_node,
  	.properties = myboard_wps_button_props,
  };



Step 3: Group and Register the Nodes
************************************

For maintainability, it is often beneficial to group all software nodes into a
single array and register them with one call.

.. code-block:: c

  static const struct software_node * const myboard_swnodes[] = {
  	&myboard_gpio_controller_node,
  	&myboard_leds_node,
  	&myboard_status_led_swnode,
  	&myboard_keys_node,
  	&myboard_wps_button_swnode,
  	NULL
  };

  static int __init myboard_init(void)
  {
  	int error;

  	error = software_node_register_node_group(myboard_swnodes);
  	if (error) {
  		pr_err("Failed to register software nodes: %d\n", error);
  		return error;
  	}

  	// ... platform device registration follows
  }

.. note::
  When splitting registration of nodes by devices that they represent, it is
  essential that the software node representing the GPIO controller itself
  is registered first, before any of the nodes that reference it.

Step 4: Register Platform Devices with Software Nodes
*****************************************************

Finally, register the platform devices and associate them with their respective
software nodes using the ``fwnode`` field in struct platform_device_info.

.. code-block:: c

  static struct platform_device *leds_pdev;
  static struct platform_device *keys_pdev;

  static int __init myboard_init(void)
  {
  	struct platform_device_info pdev_info;
  	int error;

  	error = software_node_register_node_group(myboard_swnodes);
  	if (error)
  		return error;

  	memset(&pdev_info, 0, sizeof(pdev_info));
  	pdev_info.name = "leds-gpio";
  	pdev_info.id = PLATFORM_DEVID_NONE;
  	pdev_info.fwnode = software_node_fwnode(&myboard_leds_node);
  	leds_pdev = platform_device_register_full(&pdev_info);
  	if (IS_ERR(leds_pdev)) {
  		error = PTR_ERR(leds_pdev);
  		goto err_unregister_nodes;
  	}

  	memset(&pdev_info, 0, sizeof(pdev_info));
  	pdev_info.name = "gpio-keys";
  	pdev_info.id = PLATFORM_DEVID_NONE;
  	pdev_info.fwnode = software_node_fwnode(&myboard_keys_node);
  	keys_pdev = platform_device_register_full(&pdev_info);
  	if (IS_ERR(keys_pdev)) {
  		error = PTR_ERR(keys_pdev);
  		platform_device_unregister(leds_pdev);
  		goto err_unregister_nodes;
  	}

  	return 0;

  err_unregister_nodes:
  	software_node_unregister_node_group(myboard_swnodes);
  	return error;
  }

  static void __exit myboard_exit(void)
  {
  	platform_device_unregister(keys_pdev);
  	platform_device_unregister(leds_pdev);
  	software_node_unregister_node_group(myboard_swnodes);
  }

With these changes, the generic ``leds-gpio`` and ``gpio-keys`` drivers will
be able to probe successfully and get their configuration from the properties
defined in the software nodes, removing the need for board-specific platform
data.
