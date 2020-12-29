.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver sbtsi_temp
========================

Supported hardware:

  * Sideband interface (SBI) Temperature Sensor Interface (SB-TSI)
    compliant AMD SoC temperature device.

    Prefix: 'sbtsi_temp'

    Addresses scanned: This driver doesn't support address scanning.

    To instantiate this driver on an AMD CPU with SB-TSI
    support, the i2c bus number would be the bus connected from the board
    management controller (BMC) to the CPU. The i2c address is specified in
    Section 6.3.1 of the SoC register reference: The SB-TSI address is normally
    98h for socket 0 and 90h for socket 1, but it could vary based on hardware
    address select pins.

    Datasheet: The SB-TSI interface and protocol is available as part of
               the open source SoC register reference at:

	       https://www.amd.com/system/files/TechDocs/56255_OSRR.pdf

               The Advanced Platform Management Link (APML) Specification is
               available at:

	       http://developer.amd.com/wordpress/media/2012/10/41918.pdf

Author: Kun Yi <kunyi@google.com>

Description
-----------

The SBI temperature sensor interface (SB-TSI) is an emulation of the software
and physical interface of a typical 8-pin remote temperature sensor (RTS) on
AMD SoCs. It implements one temperature sensor with readings and limit
registers encode the temperature in increments of 0.125 from 0 to 255.875.
Limits can be set through the writable thresholds, and if reached will trigger
corresponding alert signals.
