Kernel driver max31722
======================

Supported chips:

  * Maxim Integrated MAX31722

    Prefix: 'max31722'

    ACPI ID: MAX31722

    Addresses scanned: -

    Datasheet: https://datasheets.maximintegrated.com/en/ds/MAX31722-MAX31723.pdf

  * Maxim Integrated MAX31723

    Prefix: 'max31723'

    ACPI ID: MAX31723

    Addresses scanned: -

    Datasheet: https://datasheets.maximintegrated.com/en/ds/MAX31722-MAX31723.pdf

Author: Tiberiu Breana <tiberiu.a.breana@intel.com>

Description
-----------

This driver adds support for the Maxim Integrated MAX31722/MAX31723 thermometers
and thermostats running over an SPI interface.

Usage Notes
-----------

This driver uses ACPI to auto-detect devices. See ACPI IDs in the above section.

Sysfs entries
-------------

The following attribute is supported:

======================= =======================================================
temp1_input		Measured temperature. Read-only.
======================= =======================================================
