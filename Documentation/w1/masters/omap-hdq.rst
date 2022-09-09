========================================
Kernel driver for omap HDQ/1-wire module
========================================

Supported chips:
================
HDQ/1-wire controller on the TI OMAP 2430/3430 platforms.

A useful link about HDQ basics:
===============================
http://focus.ti.com/lit/an/slua408a/slua408a.pdf

Description:
============
The HDQ/1-Wire module of TI OMAP2430/3430 platforms implement the hardware
protocol of the master functions of the Benchmark HDQ and the Dallas
Semiconductor 1-Wire protocols. These protocols use a single wire for
communication between the master (HDQ/1-Wire controller) and the slave
(HDQ/1-Wire external compliant device).

A typical application of the HDQ/1-Wire module is the communication with battery
monitor (gas gauge) integrated circuits.

The controller supports operation in both HDQ and 1-wire mode. The essential
difference between the HDQ and 1-wire mode is how the slave device responds to
initialization pulse.In HDQ mode, the firmware does not require the host to
create an initialization pulse to the slave.However, the slave can be reset by
using an initialization pulse (also referred to as a break pulse).The slave
does not respond with a presence pulse as it does in the 1-Wire protocol.

Remarks:
========
The driver (drivers/w1/masters/omap_hdq.c) supports the HDQ mode of the
controller. In this mode, as we can not read the ID which obeys the W1
spec(family:id:crc), a module parameter can be passed to the driver which will
be used to calculate the CRC and pass back an appropriate slave ID to the W1
core.

By default the master driver and the BQ slave i/f
driver(drivers/w1/slaves/w1_bq27000.c) sets the ID to 1.
Please note to load both the modules with a different ID if required, but note
that the ID used should be same for both master and slave driver loading.

e.g::

  insmod omap_hdq.ko W1_ID=2
  insmod w1_bq27000.ko F_ID=2

The driver also supports 1-wire mode. In this mode, there is no need to
pass slave ID as parameter. The driver will auto-detect slaves connected
to the bus using SEARCH_ROM procedure. 1-wire mode can be selected by
setting "ti,mode" property to "1w" in DT (see
Documentation/devicetree/bindings/w1/omap-hdq.txt for more details).
By default driver is in HDQ mode.
