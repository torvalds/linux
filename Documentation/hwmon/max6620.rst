.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver max6620
=====================

Supported chips:

    Maxim MAX6620

    Prefix: 'max6620'

    Addresses scanned: none

    Datasheet: http://pdfserv.maxim-ic.com/en/ds/MAX6620.pdf

Authors:
    - L\. Grunenberg <contact@lgrunenberg.de>
    - Cumulus Networks <support@cumulusnetworks.com>
    - Shuotian Cheng <shuche@microsoft.com>
    - Arun Saravanan Balachandran <Arun_Saravanan_Balac@dell.com>

Description
-----------

This driver implements support for Maxim MAX6620 fan controller.

The driver configures the fan controller in RPM mode. To give the readings more
range or accuracy, the desired value can be set by a programmable register
(1, 2, 4, 8, 16 or 32). Set higher values for larger speeds.

The driver provides the following sensor access in sysfs:

================ ======= =====================================================
fan[1-4]_alarm   ro      Fan alarm.
fan[1-4]_div     rw      Sets the nominal RPM range of the fan. Valid values
                         are 1, 2, 4, 8, 16 and 32.
fan[1-4]_input   ro      Fan speed in RPM.
fan[1-4]_target  rw      Desired fan speed in RPM.
================ ======= =====================================================

Usage notes
-----------

This driver does not auto-detect devices. You will have to instantiate the
devices explicitly. Please see Documentation/i2c/instantiating-devices.rst for
details.
