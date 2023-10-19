Kernel driver lm95245
=====================

Supported chips:

  * TI LM95235

    Addresses scanned: I2C 0x18, 0x29, 0x4c

    Datasheet: Publicly available at the TI website

	       https://www.ti.com/lit/ds/symlink/lm95235.pdf

  * TI / National Semiconductor LM95245

    Addresses scanned: I2C 0x18, 0x19, 0x29, 0x4c, 0x4d

    Datasheet: Publicly available at the TI website

	       https://www.ti.com/lit/ds/symlink/lm95245.pdf

Author: Alexander Stein <alexander.stein@systec-electronic.com>

Description
-----------

LM95235 and LM95245 are 11-bit digital temperature sensors with a 2-wire System
Management Bus (SMBus) interface and TruTherm technology that can monitor
the temperature of a remote diode as well as its own temperature.
The chips can be used to very accurately monitor the temperature of
external devices such as microprocessors.

All temperature values are given in millidegrees Celsius. Local temperature
is given within a range of -127 to +127.875 degrees. Remote temperatures are
given within a range of -127 to +255 degrees. Resolution depends on
temperature input and range.

Each sensor has its own critical limit. Additionally, there is a relative
hysteresis value common to both critical limits. To make life easier to
user-space applications, two absolute values are exported, one for each
channel, but these values are of course linked. Only the local hysteresis
can be set from user-space, and the same delta applies to the remote
hysteresis.

The lm95245 driver can change its update interval to a fixed set of values.
It will round up to the next selectable interval. See the datasheet for exact
values. Reading sensor values more often will do no harm, but will return
'old' values.
