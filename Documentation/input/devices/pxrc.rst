=======================================================
pxrc - PhoenixRC Flight Controller Adapter
=======================================================

:Author: Marcus Folkesson <marcus.folkesson@gmail.com>

This driver let you use your own RC controller plugged into the
adapter that comes with PhoenixRC or other compatible adapters.

The adapter supports 7 analog channels and 1 digital input switch.

Notes
=====

Many RC controllers is able to configure which stick goes to which channel.
This is also configurable in most simulators, so a matching is not necessary.

The driver is generating the following input event for analog channels:

+---------+----------------+
| Channel |      Event     |
+=========+================+
|     1   |  ABS_X         |
+---------+----------------+
|     2   |  ABS_Y         |
+---------+----------------+
|     3   |  ABS_RX        |
+---------+----------------+
|     4   |  ABS_RY        |
+---------+----------------+
|     5   |  ABS_RUDDER    |
+---------+----------------+
|     6   |  ABS_THROTTLE  |
+---------+----------------+
|     7   |  ABS_MISC      |
+---------+----------------+

The digital input switch is generated as an `BTN_A` event.

Manual Testing
==============

To test this driver's functionality you may use `input-event` which is part of
the `input layer utilities` suite [1]_.

For example::

    > modprobe pxrc
    > input-events <devnr>

To print all input events from input `devnr`.

References
==========

.. [1] https://www.kraxel.org/cgit/input/
