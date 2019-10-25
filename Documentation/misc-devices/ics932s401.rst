========================
Kernel driver ics932s401
========================

Supported chips:

  * IDT ICS932S401

    Prefix: 'ics932s401'

    Addresses scanned: I2C 0x69

    Datasheet: Publicly available at the IDT website

Author: Darrick J. Wong

Description
-----------

This driver implements support for the IDT ICS932S401 chip family.

This chip has 4 clock outputs--a base clock for the CPU (which is likely
multiplied to get the real CPU clock), a system clock, a PCI clock, a USB
clock, and a reference clock.  The driver reports selected and actual
frequency.  If spread spectrum mode is enabled, the driver also reports by what
percent the clock signal is being spread, which should be between 0 and -0.5%.
All frequencies are reported in KHz.

The ICS932S401 monitors all inputs continuously. The driver will not read
the registers more often than once every other second.

Special Features
----------------

The clocks could be reprogrammed to increase system speed.  I will not help you
do this, as you risk damaging your system!
