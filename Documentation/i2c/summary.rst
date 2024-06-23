=============================
Introduction to I2C and SMBus
=============================

I²C (pronounce: I squared C and written I2C in the kernel documentation) is
a protocol developed by Philips. It is a two-wire protocol with variable
speed (typically up to 400 kHz, high speed modes up to 5 MHz). It provides
an inexpensive bus for connecting many types of devices with infrequent or
low bandwidth communications needs. I2C is widely used with embedded
systems. Some systems use variants that don't meet branding requirements,
and so are not advertised as being I2C but come under different names,
e.g. TWI (Two Wire Interface), IIC.

The latest official I2C specification is the `"I²C-bus specification and user
manual" (UM10204) <https://www.nxp.com/docs/en/user-guide/UM10204.pdf>`_
published by NXP Semiconductors, version 7 as of this writing.

SMBus (System Management Bus) is based on the I2C protocol, and is mostly
a subset of I2C protocols and signaling. Many I2C devices will work on an
SMBus, but some SMBus protocols add semantics beyond what is required to
achieve I2C branding. Modern PC mainboards rely on SMBus. The most common
devices connected through SMBus are RAM modules configured using I2C EEPROMs,
and hardware monitoring chips.

Because the SMBus is mostly a subset of the generalized I2C bus, we can
use its protocols on many I2C systems. However, there are systems that don't
meet both SMBus and I2C electrical constraints; and others which can't
implement all the common SMBus protocol semantics or messages.


Terminology
===========

The I2C bus connects one or more controller chips and one or more target chips.

.. kernel-figure::  i2c_bus.svg
   :alt:    Simple I2C bus with one controller and 3 targets

   Simple I2C bus

A **controller** chip is a node that starts communications with targets. In the
Linux kernel implementation it is also called an "adapter" or "bus". Controller
drivers are usually in the ``drivers/i2c/busses/`` subdirectory.

An **algorithm** contains general code that can be used to implement a whole
class of I2C controllers. Each specific controller driver either depends on an
algorithm driver in the ``drivers/i2c/algos/`` subdirectory, or includes its
own implementation.

A **target** chip is a node that responds to communications when addressed by a
controller. In the Linux kernel implementation it is also called a "client".
While targets are usually separate external chips, Linux can also act as a
target (needs hardware support) and respond to another controller on the bus.
This is then called a **local target**. In contrast, an external chip is called
a **remote target**.

Target drivers are kept in a directory specific to the feature they provide,
for example ``drivers/gpio/`` for GPIO expanders and ``drivers/media/i2c/`` for
video-related chips.

For the example configuration in the figure above, you will need one driver for
the I2C controller, and drivers for your I2C targets. Usually one driver for
each target.

Synonyms
--------

As mentioned above, the Linux I2C implementation historically uses the terms
"adapter" for controller and "client" for target. A number of data structures
have these synonyms in their name. So, when discussing implementation details,
you should be aware of these terms as well. The official wording is preferred,
though.

Outdated terminology
--------------------

In earlier I2C specifications, controller was named "master" and target was
named "slave". These terms have been obsoleted with v7 of the specification and
their use is also discouraged by the Linux Kernel Code of Conduct. You may
still find them in references to documentation which has not been updated. The
general attitude, however, is to use the inclusive terms: controller and
target. Work to replace the old terminology in the Linux Kernel is on-going.
