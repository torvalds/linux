.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver sbrmi
===================

Supported hardware:

  * Sideband Remote Management Interface (SB-RMI) compliant AMD SoC
    device connected to the BMC via the APML.

    Prefix: 'sbrmi'

    Addresses scanned: This driver doesn't support address scanning.

    To instantiate this driver on an AMD CPU with SB-RMI
    support, the i2c bus number would be the bus connected from the board
    management controller (BMC) to the CPU.
    The SMBus address is really 7 bits. Some vendors and the SMBus
    specification show the address as 8 bits, left justified with the R/W
    bit as a write (0) making bit 0. Some vendors use only the 7 bits
    to describe the address.
    As mentioned in AMD's APML specification, The SB-RMI address is
    normally 78h(0111 100W) or 3Ch(011 1100) for socket 0 and 70h(0111 000W)
    or 38h(011 1000) for socket 1, but it could vary based on hardware
    address select pins.

    Datasheet: The SB-RMI interface and protocol along with the Advanced
               Platform Management Link (APML) Specification is available
               as part of the open source SoC register reference at:

               https://www.amd.com/en/support/tech-docs?keyword=55898

Author: Akshay Gupta <akshay.gupta@amd.com>

Description
-----------

The APML provides a way to communicate with the SB Remote Management interface
(SB-RMI) module from the external SMBus master that can be used to report socket
power on AMD platforms using mailbox command and resembles a typical 8-pin remote
power sensor's I2C interface to BMC.

This driver implements current power with power cap and power cap max.

sysfs-Interface
---------------
Power sensors can be queried and set via the standard ``hwmon`` interface
on ``sysfs``, under the directory ``/sys/class/hwmon/hwmonX`` for some value
of ``X`` (search for the ``X`` such that ``/sys/class/hwmon/hwmonX/name`` has
content ``sbrmi``)

================ ===== ========================================================
Name             Perm   Description
================ ===== ========================================================
power1_input     RO    Current Power consumed
power1_cap       RW    Power limit can be set between 0 and power1_cap_max
power1_cap_max   RO    Maximum powerlimit calculated and reported by the SMU FW
================ ===== ========================================================

The following example show how the 'Power' attribute from the i2c-addresses
can be monitored using the userspace utilities like ``sensors`` binary::

  # sensors
  sbrmi-i2c-1-38
  Adapter: bcm2835 I2C adapter
  power1:       61.00 W (cap = 225.00 W)

  sbrmi-i2c-1-3c
  Adapter: bcm2835 I2C adapter
  power1:       28.39 W (cap = 224.77 W)
  #

Also, Below shows how get and set the values from sysfs entries individually::
  # cat /sys/class/hwmon/hwmon1/power1_cap_max
  225000000

  # echo 180000000 > /sys/class/hwmon/hwmon1/power1_cap
  # cat /sys/class/hwmon/hwmon1/power1_cap
  180000000
