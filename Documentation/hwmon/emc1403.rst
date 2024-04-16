Kernel driver emc1403
=====================

Supported chips:

  * SMSC / Microchip EMC1402, EMC1412

    Addresses scanned: I2C 0x18, 0x1c, 0x29, 0x4c, 0x4d, 0x5c

    Prefix: 'emc1402'

    Datasheets:

	- http://ww1.microchip.com/downloads/en/DeviceDoc/1412.pdf
	- https://ww1.microchip.com/downloads/en/DeviceDoc/1402.pdf

  * SMSC / Microchip EMC1403, EMC1404, EMC1413, EMC1414

    Addresses scanned: I2C 0x18, 0x29, 0x4c, 0x4d

    Prefix: 'emc1403', 'emc1404'

    Datasheets:

	- http://ww1.microchip.com/downloads/en/DeviceDoc/1403_1404.pdf
	- http://ww1.microchip.com/downloads/en/DeviceDoc/1413_1414.pdf

  * SMSC / Microchip EMC1422

    Addresses scanned: I2C 0x4c

    Prefix: 'emc1422'

    Datasheet:

	- https://ww1.microchip.com/downloads/en/DeviceDoc/1422.pdf

  * SMSC / Microchip EMC1423, EMC1424

    Addresses scanned: I2C 0x4c

    Prefix: 'emc1423', 'emc1424'

    Datasheet:

	- https://ww1.microchip.com/downloads/en/DeviceDoc/1423_1424.pdf

Author:
    Kalhan Trisal <kalhan.trisal@intel.com


Description
-----------

The Standard Microsystems Corporation (SMSC) / Microchip EMC14xx chips
contain up to four temperature sensors. EMC14x2 support two sensors
(one internal, one external). EMC14x3 support three sensors (one internal,
two external), and EMC14x4 support four sensors (one internal, three
external).

The chips implement three limits for each sensor: low (tempX_min), high
(tempX_max) and critical (tempX_crit.) The chips also implement an
hysteresis mechanism which applies to all limits. The relative difference
is stored in a single register on the chip, which means that the relative
difference between the limit and its hysteresis is always the same for
all three limits.

This implementation detail implies the following:

* When setting a limit, its hysteresis will automatically follow, the
  difference staying unchanged. For example, if the old critical limit
  was 80 degrees C, and the hysteresis was 75 degrees C, and you change
  the critical limit to 90 degrees C, then the hysteresis will
  automatically change to 85 degrees C.
* The hysteresis values can't be set independently. We decided to make
  only temp1_crit_hyst writable, while all other hysteresis attributes
  are read-only. Setting temp1_crit_hyst writes the difference between
  temp1_crit_hyst and temp1_crit into the chip, and the same relative
  hysteresis applies automatically to all other limits.
* The limits should be set before the hysteresis.
