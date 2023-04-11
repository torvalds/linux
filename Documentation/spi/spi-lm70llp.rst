==============================================
spi_lm70llp :  LM70-LLP parport-to-SPI adapter
==============================================

Supported board/chip:

  * National Semiconductor LM70 LLP evaluation board

    Datasheet: http://www.national.com/pf/LM/LM70.html

Author:
        Kaiwan N Billimoria <kaiwan@designergraphix.com>

Description
-----------
This driver provides glue code connecting a National Semiconductor LM70 LLP
temperature sensor evaluation board to the kernel's SPI core subsystem.

This is a SPI master controller driver. It can be used in conjunction with
(layered under) the LM70 logical driver (a "SPI protocol driver").
In effect, this driver turns the parallel port interface on the eval board
into a SPI bus with a single device, which will be driven by the generic
LM70 driver (drivers/hwmon/lm70.c).


Hardware Interfacing
--------------------
The schematic for this particular board (the LM70EVAL-LLP) is
available (on page 4) here:

  http://www.national.com/appinfo/tempsensors/files/LM70LLPEVALmanual.pdf

The hardware interfacing on the LM70 LLP eval board is as follows:

   ======== == =========   ==========
   Parallel                 LM70 LLP
     Port   .  Direction   JP2 Header
   ======== == =========   ==========
      D0     2      -         -
      D1     3     -->      V+   5
      D2     4     -->      V+   5
      D3     5     -->      V+   5
      D4     6     -->      V+   5
      D5     7     -->      nCS  8
      D6     8     -->      SCLK 3
      D7     9     -->      SI/O 5
     GND    25      -       GND  7
    Select  13     <--      SI/O 1
   ======== == =========   ==========

Note that since the LM70 uses a "3-wire" variant of SPI, the SI/SO pin
is connected to both pin D7 (as Master Out) and Select (as Master In)
using an arrangement that lets either the parport or the LM70 pull the
pin low.  This can't be shared with true SPI devices, but other 3-wire
devices might share the same SI/SO pin.

The bitbanger routine in this driver (lm70_txrx) is called back from
the bound "hwmon/lm70" protocol driver through its sysfs hook, using a
spi_write_then_read() call.  It performs Mode 0 (SPI/Microwire) bitbanging.
The lm70 driver then interprets the resulting digital temperature value
and exports it through sysfs.

A "gotcha": National Semiconductor's LM70 LLP eval board circuit schematic
shows that the SI/O line from the LM70 chip is connected to the base of a
transistor Q1 (and also a pullup, and a zener diode to D7); while the
collector is tied to VCC.

Interpreting this circuit, when the LM70 SI/O line is High (or tristate
and not grounded by the host via D7), the transistor conducts and switches
the collector to zero, which is reflected on pin 13 of the DB25 parport
connector.  When SI/O is Low (driven by the LM70 or the host) on the other
hand, the transistor is cut off and the voltage tied to it's collector is
reflected on pin 13 as a High level.

So: the getmiso inline routine in this driver takes this fact into account,
inverting the value read at pin 13.


Thanks to
---------

- David Brownell for mentoring the SPI-side driver development.
- Dr.Craig Hollabaugh for the (early) "manual" bitbanging driver version.
- Nadir Billimoria for help interpreting the circuit schematic.
