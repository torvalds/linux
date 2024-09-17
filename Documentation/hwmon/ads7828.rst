Kernel driver ads7828
=====================

Supported chips:

  * Texas Instruments/Burr-Brown ADS7828

    Prefix: 'ads7828'

    Datasheet: Publicly available at the Texas Instruments website:

	       http://focus.ti.com/lit/ds/symlink/ads7828.pdf

  * Texas Instruments ADS7830

    Prefix: 'ads7830'

    Datasheet: Publicly available at the Texas Instruments website:

	       http://focus.ti.com/lit/ds/symlink/ads7830.pdf

Authors:
	- Steve Hardy <shardy@redhat.com>
	- Vivien Didelot <vivien.didelot@savoirfairelinux.com>
	- Guillaume Roguez <guillaume.roguez@savoirfairelinux.com>

Platform data
-------------

The ads7828 driver accepts an optional ads7828_platform_data structure (defined
in include/linux/platform_data/ads7828.h). The structure fields are:

* diff_input: (bool) Differential operation
    set to true for differential mode, false for default single ended mode.

* ext_vref: (bool) External reference
    set to true if it operates with an external reference, false for default
    internal reference.

* vref_mv: (unsigned int) Voltage reference
    if using an external reference, set this to the reference voltage in mV,
    otherwise it will default to the internal value (2500mV). This value will be
    bounded with limits accepted by the chip, described in the datasheet.

 If no structure is provided, the configuration defaults to single ended
 operation and internal voltage reference (2.5V).

Description
-----------

This driver implements support for the Texas Instruments ADS7828 and ADS7830.

The ADS7828 device is a 12-bit 8-channel A/D converter, while the ADS7830 does
8-bit sampling.

It can operate in single ended mode (8 +ve inputs) or in differential mode,
where 4 differential pairs can be measured.

The chip also has the facility to use an external voltage reference.  This
may be required if your hardware supplies the ADS7828 from a 5V supply, see
the datasheet for more details.

There is no reliable way to identify this chip, so the driver will not scan
some addresses to try to auto-detect it. That means that you will have to
statically declare the device in the platform support code.
