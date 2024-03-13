Kernel driver ad7314
====================

Supported chips:

   * Analog Devices AD7314

     Prefix: 'ad7314'

     Datasheet: Publicly available at Analog Devices website.

   * Analog Devices ADT7301

     Prefix: 'adt7301'

     Datasheet: Publicly available at Analog Devices website.

   * Analog Devices ADT7302

     Prefix: 'adt7302'

     Datasheet: Publicly available at Analog Devices website.

Description
-----------

Driver supports the above parts.  The ad7314 has a 10 bit
sensor with 1lsb = 0.25 degrees centigrade. The adt7301 and
adt7302 have 14 bit sensors with 1lsb = 0.03125 degrees centigrade.

Notes
-----

Currently power down mode is not supported.
