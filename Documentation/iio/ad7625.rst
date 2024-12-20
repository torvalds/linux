.. SPDX-License-Identifier: GPL-2.0-only

====================
AD7625 driver
====================

ADC driver for Analog Devices Inc. AD7625, AD7626, AD7960, and AD7961
devices. The module name is ``ad7625``.

Supported devices
=================

The following chips are supported by this driver:

* `AD7625 <https://www.analog.com/AD7625>`_
* `AD7626 <https://www.analog.com/AD7626>`_
* `AD7960 <https://www.analog.com/AD7960>`_
* `AD7961 <https://www.analog.com/AD7961>`_

The driver requires use of the Pulsar LVDS HDL project:

* `Pulsar LVDS HDL <http://analogdevicesinc.github.io/hdl/projects/pulsar_lvds/index.html>`_

To trigger conversions and enable subsequent data transfer, the devices
require coupled PWM signals with a phase offset.

Supported features
==================

Conversion control modes
------------------------

The driver currently supports one of two possible LVDS conversion control methods.

Echoed-Clock interface mode
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block::

                                                +----------------+
                     +xxxxxxxxxxxxxxxxxxxxxxxxxx| CNV            |
                     X                          |                |
                     v                          |    HOST        |
          +----------------------------+        |                |
          |      CNV+/CNV-   DCO+/DCO- |xxxxxxx>| CLK_IN         |
          |                            |        |                |
          |                            |        |                |
          |       AD7625         D+/D- |xxxxxxx>| DATA_IN        |
          |                            |        |                |
          |                            |        |                |
          |                  CLK+/CLK- |<xxxxxxx| CLK & CLK_GATE |
          +----------------------------+        |                |
                                                +----------------+

Reference voltage
-----------------

Three possible reference voltage sources are supported:

- Internal reference (only available on AD7625 and AD7626)
- External reference and internal buffer
- External reference

The source is determined by the device tree. If ``ref-supply`` is present, then
the external reference is used. If ``refin-supply`` is present, then the internal
buffer is used. If neither is present, then the internal reference is used.

Unimplemented features
----------------------

- Self-clocked mode


Device attributes
=================

The AD762x is a fully-differential ADC and has the following attributes:

+---------------------------------------+--------------------------------------------------------------+
| Attribute                             | Description                                                  |
+=======================================+==============================================================+
| ``scale``                             | Scale factor to convert raw value from buffered reads to mV. |
+---------------------------------------+--------------------------------------------------------------+


Device buffers
==============

This driver supports IIO triggered buffers.

See :doc:`iio_devbuf` for more information.
