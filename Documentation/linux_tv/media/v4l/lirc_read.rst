.. -*- coding: utf-8; mode: rst -*-

.. _lirc_read:

*************
LIRC read fop
*************

The lircd userspace daemon reads raw IR data from the LIRC chardev. The
exact format of the data depends on what modes a driver supports, and
what mode has been selected. lircd obtains supported modes and sets the
active mode via the ioctl interface, detailed at :ref:`lirc_ioctl`.
The generally preferred mode is LIRC_MODE_MODE2, in which packets
containing an int value describing an IR signal are read from the
chardev.

See also
`http://www.lirc.org/html/technical.html <http://www.lirc.org/html/technical.html>`__
for more info.
