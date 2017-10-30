.. include:: <isonum.txt>

------------------------
BCM5974 Driver (bcm5974)
------------------------

:Copyright: |copy| 2008-2009	Henrik Rydberg <rydberg@euromail.se>

The USB initialization and package decoding was made by Scott Shawcroft as
part of the touchd user-space driver project:

:Copyright: |copy| 2008	Scott Shawcroft (scott.shawcroft@gmail.com)

The BCM5974 driver is based on the appletouch driver:

:Copyright: |copy| 2001-2004	Greg Kroah-Hartman (greg@kroah.com)
:Copyright: |copy| 2005		Johannes Berg (johannes@sipsolutions.net)
:Copyright: |copy| 2005		Stelian Pop (stelian@popies.net)
:Copyright: |copy| 2005		Frank Arnold (frank@scirocco-5v-turbo.de)
:Copyright: |copy| 2005		Peter Osterlund (petero2@telia.com)
:Copyright: |copy| 2005		Michael Hanselmann (linux-kernel@hansmi.ch)
:Copyright: |copy| 2006		Nicolas Boichat (nicolas@boichat.ch)

This driver adds support for the multi-touch trackpad on the new Apple
Macbook Air and Macbook Pro laptops. It replaces the appletouch driver on
those computers, and integrates well with the synaptics driver of the Xorg
system.

Known to work on Macbook Air, Macbook Pro Penryn and the new unibody
Macbook 5 and Macbook Pro 5.

Usage
-----

The driver loads automatically for the supported usb device ids, and
becomes available both as an event device (/dev/input/event*) and as a
mouse via the mousedev driver (/dev/input/mice).

USB Race
--------

The Apple multi-touch trackpads report both mouse and keyboard events via
different interfaces of the same usb device. This creates a race condition
with the HID driver, which, if not told otherwise, will find the standard
HID mouse and keyboard, and claim the whole device. To remedy, the usb
product id must be listed in the mouse_ignore list of the hid driver.

Debug output
------------

To ease the development for new hardware version, verbose packet output can
be switched on with the debug kernel module parameter. The range [1-9]
yields different levels of verbosity. Example (as root)::

    echo -n 9 > /sys/module/bcm5974/parameters/debug

    tail -f /var/log/debug

    echo -n 0 > /sys/module/bcm5974/parameters/debug

Trivia
------

The driver was developed at the ubuntu forums in June 2008 [#f1]_, and now has
a more permanent home at bitmath.org [#f2]_.

.. Links

.. [#f1] http://ubuntuforums.org/showthread.php?t=840040
.. [#f2] http://bitmath.org/code/
