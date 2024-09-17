Kernel driver adm1025
=====================

Supported chips:

  * Analog Devices ADM1025, ADM1025A

    Prefix: 'adm1025'

    Addresses scanned: I2C 0x2c - 0x2e

    Datasheet: Publicly available at the Analog Devices website

  * Philips NE1619

    Prefix: 'ne1619'

    Addresses scanned: I2C 0x2c - 0x2d

    Datasheet: Publicly available at the Philips website

The NE1619 presents some differences with the original ADM1025:

  * Only two possible addresses (0x2c - 0x2d).
  * No temperature offset register, but we don't use it anyway.
  * No INT mode for pin 16. We don't play with it anyway.

Authors:
	- Chen-Yuan Wu <gwu@esoft.com>,
	- Jean Delvare <jdelvare@suse.de>

Description
-----------

(This is from Analog Devices.) The ADM1025 is a complete system hardware
monitor for microprocessor-based systems, providing measurement and limit
comparison of various system parameters. Five voltage measurement inputs
are provided, for monitoring +2.5V, +3.3V, +5V and +12V power supplies and
the processor core voltage. The ADM1025 can monitor a sixth power-supply
voltage by measuring its own VCC. One input (two pins) is dedicated to a
remote temperature-sensing diode and an on-chip temperature sensor allows
ambient temperature to be monitored.

One specificity of this chip is that the pin 11 can be hardwired in two
different manners. It can act as the +12V power-supply voltage analog
input, or as the a fifth digital entry for the VID reading (bit 4). It's
kind of strange since both are useful, and the reason for designing the
chip that way is obscure at least to me. The bit 5 of the configuration
register can be used to define how the chip is hardwired. Please note that
it is not a choice you have to make as the user. The choice was already
made by your motherboard's maker. If the configuration bit isn't set
properly, you'll have a wrong +12V reading or a wrong VID reading. The way
the driver handles that is to preserve this bit through the initialization
process, assuming that the BIOS set it up properly beforehand. If it turns
out not to be true in some cases, we'll provide a module parameter to force
modes.

This driver also supports the ADM1025A, which differs from the ADM1025
only in that it has "open-drain VID inputs while the ADM1025 has on-chip
100k pull-ups on the VID inputs". It doesn't make any difference for us.
