.. SPDX-License-Identifier: GPL-2.0+

HTE Kernel provider driver
==========================

Description
-----------
The Nvidia tegra194 HTE provider driver implements two GTE
(Generic Timestamping Engine) instances: 1) GPIO GTE and 2) LIC
(Legacy Interrupt Controller) IRQ GTE. Both GTE instances get the
timestamp from the system counter TSC which has 31.25MHz clock rate, and the
driver converts clock tick rate to nanoseconds before storing it as timestamp
value.

GPIO GTE
--------

This GTE instance timestamps GPIO in real time. For that to happen GPIO
needs to be configured as input. The always on (AON) GPIO controller instance
supports timestamping GPIOs in real time and it has 39 GPIO lines. The GPIO GTE
and AON GPIO controller are tightly coupled as it requires very specific bits
to be set in GPIO config register before GPIO GTE can be used, for that GPIOLIB
adds two optional APIs as below. The GPIO GTE code supports both kernel
and userspace consumers. The kernel space consumers can directly talk to HTE
subsystem while userspace consumers timestamp requests go through GPIOLIB CDEV
framework to HTE subsystem.

.. kernel-doc:: drivers/gpio/gpiolib.c
   :functions: gpiod_enable_hw_timestamp_ns gpiod_disable_hw_timestamp_ns

For userspace consumers, GPIO_V2_LINE_FLAG_EVENT_CLOCK_HTE flag must be
specified during IOCTL calls. Refer to ``tools/gpio/gpio-event-mon.c``, which
returns the timestamp in nanoseconds.

LIC (Legacy Interrupt Controller) IRQ GTE
-----------------------------------------

This GTE instance timestamps LIC IRQ lines in real time. There are 352 IRQ
lines which this instance can add timestamps to in real time. The hte
devicetree binding described at ``Documentation/devicetree/bindings/hte/``
provides an example of how a consumer can request an IRQ line. Since it is a
one-to-one mapping with IRQ GTE provider, consumers can simply specify the IRQ
number that they are interested in. There is no userspace consumer support for
this GTE instance in the HTE framework.

The provider source code of both IRQ and GPIO GTE instances is located at
``drivers/hte/hte-tegra194.c``. The test driver
``drivers/hte/hte-tegra194-test.c`` demonstrates HTE API usage for both IRQ
and GPIO GTE.
