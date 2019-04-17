Kernel driver lm77
==================

Supported chips:

  * National Semiconductor LM77

    Prefix: 'lm77'

    Addresses scanned: I2C 0x48 - 0x4b

    Datasheet: Publicly available at the National Semiconductor website

	       http://www.national.com/


Author: Andras BALI <drewie@freemail.hu>

Description
-----------

The LM77 implements one temperature sensor. The temperature
sensor incorporates a band-gap type temperature sensor,
10-bit ADC, and a digital comparator with user-programmable upper
and lower limit values.

The LM77 implements 3 limits: low (temp1_min), high (temp1_max) and
critical (temp1_crit.) It also implements an hysteresis mechanism which
applies to all 3 limits. The relative difference is stored in a single
register on the chip, which means that the relative difference between
the limit and its hysteresis is always the same for all 3 limits.

This implementation detail implies the following:

* When setting a limit, its hysteresis will automatically follow, the
  difference staying unchanged. For example, if the old critical limit
  was 80 degrees C, and the hysteresis was 75 degrees C, and you change
  the critical limit to 90 degrees C, then the hysteresis will
  automatically change to 85 degrees C.
* All 3 hysteresis can't be set independently. We decided to make
  temp1_crit_hyst writable, while temp1_min_hyst and temp1_max_hyst are
  read-only. Setting temp1_crit_hyst writes the difference between
  temp1_crit_hyst and temp1_crit into the chip, and the same relative
  hysteresis applies automatically to the low and high limits.
* The limits should be set before the hysteresis.
