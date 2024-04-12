.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver ChipCap2
======================

Supported chips:

  * Amphenol CC2D23, CC2D23S, CC2D25, CC2D25S, CC2D33, CC2D33S, CC2D35, CC2D35S

    Prefix: 'chipcap2'

    Addresses scanned: -

    Datasheet: https://www.amphenol-sensors.com/en/telaire/humidity/527-humidity-sensors/3095-chipcap-2

Author:

  - Javier Carrasco <javier.carrasco.cruz@gmail.com>

Description
-----------

This driver implements support for the Amphenol ChipCap 2, a humidity and
temperature chip family. Temperature is measured in milli degrees celsius,
relative humidity is expressed as a per cent mille. The measurement ranges
are the following:

  - Relative humidity: 0 to 100000 pcm (14-bit resolution)
  - Temperature: -40000 to +125000 m°C (14-bit resolution)

The device communicates with the I2C protocol and uses the I2C address 0x28
by default.

Depending on the hardware configuration, up to two humidity alarms to control
minimum and maximum values are provided. Their thresholds and hystersis can be
configured via sysfs.

Thresholds and hysteris must be provided as a per cent mille. These values
might be truncated to match the 14-bit device resolution (6.1 pcm/LSB)

Known Issues
------------

The driver does not support I2C address and command window length modification.

sysfs-Interface
---------------

The following list includes the sysfs attributes that the driver always provides,
their permissions and a short description:

=============================== ======= ========================================
Name                            Perm    Description
=============================== ======= ========================================
temp1_input:                    RO      temperature input
humidity1_input:                RO      humidity input
=============================== ======= ========================================

The following list includes the sysfs attributes that the driver may provide
depending on the hardware configuration:

=============================== ======= ========================================
Name                            Perm    Description
=============================== ======= ========================================
humidity1_min:                  RW      humidity low limit. Measurements under
                                        this limit trigger a humidity low alarm
humidity1_max:                  RW      humidity high limit. Measurements above
                                        this limit trigger a humidity high alarm
humidity1_min_hyst:             RW      humidity low hystersis
humidity1_max_hyst:             RW      humidity high hystersis
humidity1_min_alarm:            RO      humidity low alarm indicator
humidity1_max_alarm:            RO      humidity high alarm indicator
=============================== ======= ========================================
