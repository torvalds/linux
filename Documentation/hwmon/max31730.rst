Kernel driver max31790
======================

Supported chips:

  * Maxim MAX31730

    Prefix: 'max31730'

    Addresses scanned: 0x1c, 0x1d, 0x1e, 0x1f, 0x4c, 0x4d, 0x4e, 0x4f

    Datasheet: https://datasheets.maximintegrated.com/en/ds/MAX31730.pdf

Author: Guenter Roeck <linux@roeck-us.net>


Description
-----------

This driver implements support for Maxim MAX31730.

The MAX31730 temperature sensor monitors its own temperature and the
temperatures of three external diode-connected transistors. The operating
supply voltage is from 3.0V to 3.6V. Resistance cancellation compensates
for high series resistance in circuit-board traces and the external thermal
diode, while beta compensation corrects for temperature-measurement
errors due to low-beta sensing transistors.


Sysfs entries
-------------

=================== == =======================================================
temp[1-4]_enable    RW Temperature enable/disable
                       Set to 0 to enable channel, 0 to disable
temp[1-4]_input     RO Temperature input
temp[2-4]_fault     RO Fault indicator for remote channels
temp[1-4]_max       RW Maximum temperature
temp[1-4]_max_alarm RW Maximum temperature alarm
temp[1-4]_min       RW Minimum temperature. Common for all channels.
                       Only temp1_min is writeable.
temp[1-4]_min_alarm RO Minimum temperature alarm
temp[2-4]_offset    RW Temperature offset for remote channels
=================== == =======================================================
