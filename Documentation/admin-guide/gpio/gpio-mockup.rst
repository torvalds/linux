.. SPDX-License-Identifier: GPL-2.0-only

GPIO Testing Driver
===================

.. note::

   This module has been obsoleted by the more flexible gpio-sim.rst.
   New developments should use that API and existing developments are
   encouraged to migrate as soon as possible.
   This module will continue to be maintained but no new features will be
   added.

The GPIO Testing Driver (gpio-mockup) provides a way to create simulated GPIO
chips for testing purposes. The lines exposed by these chips can be accessed
using the standard GPIO character device interface as well as manipulated
using the dedicated debugfs directory structure.

Creating simulated chips using module params
--------------------------------------------

When loading the gpio-mockup driver a number of parameters can be passed to the
module.

    gpio_mockup_ranges

        This parameter takes an argument in the form of an array of integer
        pairs. Each pair defines the base GPIO number (non-negative integer)
        and the first number after the last of this chip. If the base GPIO
        is -1, the gpiolib will assign it automatically. while the following
        parameter is the number of lines exposed by the chip.

        Example: gpio_mockup_ranges=-1,8,-1,16,405,409

        The line above creates three chips. The first one will expose 8 lines,
        the second 16 and the third 4. The base GPIO for the third chip is set
        to 405 while for two first chips it will be assigned automatically.

    gpio_mockup_named_lines

        This parameter doesn't take any arguments. It lets the driver know that
        GPIO lines exposed by it should be named.

        The name format is: gpio-mockup-X-Y where X is mockup chip's ID
        and Y is the line offset.

Manipulating simulated lines
----------------------------

Each mockup chip creates its own subdirectory in /sys/kernel/debug/gpio-mockup/.
The directory is named after the chip's label. A symlink is also created, named
after the chip's name, which points to the label directory.

Inside each subdirectory, there's a separate attribute for each GPIO line. The
name of the attribute represents the line's offset in the chip.

Reading from a line attribute returns the current value. Writing to it (0 or 1)
changes the configuration of the simulated pull-up/pull-down resistor
(1 - pull-up, 0 - pull-down).
