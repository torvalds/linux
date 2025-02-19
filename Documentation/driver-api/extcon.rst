=======================
Extcon Device Subsystem
=======================

Overview
========

The Extcon (External Connector) subsystem provides a unified framework for
managing external connectors in Linux systems. It allows drivers to report
the state of external connectors and provides a standardized interface for
userspace to query and monitor these states.

Extcon is particularly useful in modern devices with multiple connectivity
options, such as smartphones, tablets, and laptops. It helps manage various
types of connectors, including:

1. USB connectors (e.g., USB-C, micro-USB)
2. Charging ports (e.g., fast charging, wireless charging)
3. Audio jacks (e.g., 3.5mm headphone jack)
4. Video outputs (e.g., HDMI, DisplayPort)
5. Docking stations

Real-world examples:

1. Smartphone USB-C port:
   A single USB-C port on a smartphone can serve multiple functions. Extcon
   can manage the different states of this port, such as:
   - USB data connection
   - Charging (various types like fast charging, USB Power Delivery)
   - Audio output (USB-C headphones)
   - Video output (USB-C to HDMI adapter)

2. Laptop docking station:
   When a laptop is connected to a docking station, multiple connections are
   made simultaneously. Extcon can handle the state changes for:
   - Power delivery
   - External displays
   - USB hub connections
   - Ethernet connectivity

3. Wireless charging pad:
   Extcon can manage the state of a wireless charging connection, allowing
   the system to respond appropriately when a device is placed on or removed
   from the charging pad.

4. Smart TV HDMI ports:
   In a smart TV, Extcon can manage multiple HDMI ports, detecting when
   devices are connected or disconnected, and potentially identifying the
   type of device (e.g., gaming console, set-top box, Blu-ray player).

The Extcon framework simplifies the development of drivers for these complex
scenarios by providing a standardized way to report and query connector
states, handle mutually exclusive connections, and manage connector
properties. This allows for more robust and flexible handling of external
connections in modern devices.

Key Components
==============

extcon_dev
----------

The core structure representing an Extcon device::

    struct extcon_dev {
        const char *name;
        const unsigned int *supported_cable;
        const u32 *mutually_exclusive;

        /* Internal data */
        struct device dev;
        unsigned int id;
        struct raw_notifier_head nh_all;
        struct raw_notifier_head *nh;
        struct list_head entry;
        int max_supported;
        spinlock_t lock;
        u32 state;

        /* Sysfs related */
        struct device_type extcon_dev_type;
        struct extcon_cable *cables;
        struct attribute_group attr_g_muex;
        struct attribute **attrs_muex;
        struct device_attribute *d_attrs_muex;
    };

Key fields:

- ``name``: Name of the Extcon device
- ``supported_cable``: Array of supported cable types
- ``mutually_exclusive``: Array defining mutually exclusive cable types
  This field is crucial for enforcing hardware constraints. It's an array of
  32-bit unsigned integers, where each element represents a set of mutually
  exclusive cable types. The array should be terminated with a 0.

  For example:

  ::

      static const u32 mutually_exclusive[] = {
          BIT(0) | BIT(1),  /* Cable 0 and 1 are mutually exclusive */
          BIT(2) | BIT(3) | BIT(4),  /* Cables 2, 3, and 4 are mutually exclusive */
          0  /* Terminator */
      };

  In this example, cables 0 and 1 cannot be connected simultaneously, and
  cables 2, 3, and 4 are also mutually exclusive. This is useful for
  scenarios like a single port that can either be USB or HDMI, but not both
  at the same time.

  The Extcon core uses this information to prevent invalid combinations of
  cable states, ensuring that the reported states are always consistent
  with the hardware capabilities.

- ``state``: Current state of the device (bitmap of connected cables)


extcon_cable
------------

Represents an individual cable managed by an Extcon device::

    struct extcon_cable {
        struct extcon_dev *edev;
        int cable_index;
        struct attribute_group attr_g;
        struct device_attribute attr_name;
        struct device_attribute attr_state;
        struct attribute *attrs[3];
        union extcon_property_value usb_propval[EXTCON_PROP_USB_CNT];
        union extcon_property_value chg_propval[EXTCON_PROP_CHG_CNT];
        union extcon_property_value jack_propval[EXTCON_PROP_JACK_CNT];
        union extcon_property_value disp_propval[EXTCON_PROP_DISP_CNT];
        DECLARE_BITMAP(usb_bits, EXTCON_PROP_USB_CNT);
        DECLARE_BITMAP(chg_bits, EXTCON_PROP_CHG_CNT);
        DECLARE_BITMAP(jack_bits, EXTCON_PROP_JACK_CNT);
        DECLARE_BITMAP(disp_bits, EXTCON_PROP_DISP_CNT);
    };

Core Functions
==============

.. kernel-doc:: drivers/extcon/extcon.c
   :identifiers: extcon_get_state

.. kernel-doc:: drivers/extcon/extcon.c
   :identifiers: extcon_set_state

.. kernel-doc:: drivers/extcon/extcon.c
   :identifiers: extcon_set_state_sync

.. kernel-doc:: drivers/extcon/extcon.c
   :identifiers: extcon_get_property


Sysfs Interface
===============

Extcon devices expose the following sysfs attributes:

- ``name``: Name of the Extcon device
- ``state``: Current state of all supported cables
- ``cable.N/name``: Name of the Nth supported cable
- ``cable.N/state``: State of the Nth supported cable

Usage Example
-------------

.. code-block:: c

    #include <linux/module.h>
    #include <linux/platform_device.h>
    #include <linux/extcon.h>

    struct my_extcon_data {
        struct extcon_dev *edev;
        struct device *dev;
    };

    static const unsigned int my_extcon_cable[] = {
        EXTCON_USB,
        EXTCON_USB_HOST,
        EXTCON_NONE,
    };

    static int my_extcon_probe(struct platform_device *pdev)
    {
        struct my_extcon_data *data;
        int ret;

        data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
        if (!data)
            return -ENOMEM;

        data->dev = &pdev->dev;

        /* Initialize extcon device */
        data->edev = devm_extcon_dev_allocate(data->dev, my_extcon_cable);
        if (IS_ERR(data->edev)) {
            dev_err(data->dev, "Failed to allocate extcon device\n");
            return PTR_ERR(data->edev);
        }

        /* Register extcon device */
        ret = devm_extcon_dev_register(data->dev, data->edev);
        if (ret < 0) {
            dev_err(data->dev, "Failed to register extcon device\n");
            return ret;
        }

        platform_set_drvdata(pdev, data);

        /* Example: Set initial state */
        extcon_set_state_sync(data->edev, EXTCON_USB, true);

        dev_info(data->dev, "My extcon driver probed successfully\n");
        return 0;
    }

    static int my_extcon_remove(struct platform_device *pdev)
    {
        struct my_extcon_data *data = platform_get_drvdata(pdev);

        /* Example: Clear state before removal */
        extcon_set_state_sync(data->edev, EXTCON_USB, false);

        dev_info(data->dev, "My extcon driver removed\n");
        return 0;
    }

    static const struct of_device_id my_extcon_of_match[] = {
        { .compatible = "my,extcon-device", },
        { },
    };
    MODULE_DEVICE_TABLE(of, my_extcon_of_match);

    static struct platform_driver my_extcon_driver = {
        .driver = {
            .name = "my-extcon-driver",
            .of_match_table = my_extcon_of_match,
        },
        .probe = my_extcon_probe,
        .remove = my_extcon_remove,
    };

    module_platform_driver(my_extcon_driver);

This example demonstrates:
---------------------------

- Defining supported cable types (USB and USB Host in this case).
- Allocating and registering an extcon device.
- Setting an initial state for a cable (USB connected in this example).
- Clearing the state when the driver is removed.
