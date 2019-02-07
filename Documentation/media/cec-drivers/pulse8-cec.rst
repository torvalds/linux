.. SPDX-License-Identifier: GPL-2.0

Pulse-Eight CEC Adapter driver
==============================

The pulse8-cec driver implements the following module option:

``persistent_config``
---------------------

By default this is off, but when set to 1 the driver will store the current
settings to the device's internal eeprom and restore it the next time the
device is connected to the USB port.
