.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _lirc_dev_intro:

************
Introduction
************

LIRC stands for Linux Infrared Remote Control. The LIRC device interface is
a bi-directional interface for transporting raw IR and decoded scancodes
data between userspace and kernelspace. Fundamentally, it is just a chardev
(/dev/lircX, for X = 0, 1, 2, ...), with a number of standard struct
file_operations defined on it. With respect to transporting raw IR and
decoded scancodes to and fro, the essential fops are read, write and ioctl.

Example dmesg output upon a driver registering w/LIRC:

.. code-block:: none

    $ dmesg |grep lirc_dev
    rc rc0: lirc_dev: driver mceusb registered at minor = 0, raw IR receiver, raw IR transmitter

What you should see for a chardev:

.. code-block:: none

    $ ls -l /dev/lirc*
    crw-rw---- 1 root root 248, 0 Jul 2 22:20 /dev/lirc0

.. _lirc_modes:

**********
LIRC modes
**********

LIRC supports some modes of receiving and sending IR codes, as shown
on the following table.

.. _lirc-mode-scancode:
.. _lirc-scancode-flag-toggle:
.. _lirc-scancode-flag-repeat:

``LIRC_MODE_SCANCODE``

    This mode is for both sending and receiving IR.

    For transmitting (aka sending), create a ``struct lirc_scancode`` with
    the desired scancode set in the ``scancode`` member, :c:type:`rc_proto`
    set the IR protocol, and all other members set to 0. Write this struct to
    the lirc device.

    For receiving, you read ``struct lirc_scancode`` from the lirc device,
    with ``scancode`` set to the received scancode and the IR protocol
    :c:type:`rc_proto`. If the scancode maps to a valid key code, this is set
    in the ``keycode`` field, else it is set to ``KEY_RESERVED``.

    The ``flags`` can have ``LIRC_SCANCODE_FLAG_TOGGLE`` set if the toggle
    bit is set in protocols that support it (e.g. rc-5 and rc-6), or
    ``LIRC_SCANCODE_FLAG_REPEAT`` for when a repeat is received for protocols
    that support it (e.g. nec).

    In the Sanyo and NEC protocol, if you hold a button on remote, rather than
    repeating the entire scancode, the remote sends a shorter message with
    no scancode, which just means button is held, a "repeat". When this is
    received, the ``LIRC_SCANCODE_FLAG_REPEAT`` is set and the scancode and
    keycode is repeated.

    With nec, there is no way to distinguish "button hold" from "repeatedly
    pressing the same button". The rc-5 and rc-6 protocols have a toggle bit.
    When a button is released and pressed again, the toggle bit is inverted.
    If the toggle bit is set, the ``LIRC_SCANCODE_FLAG_TOGGLE`` is set.

    The ``timestamp`` field is filled with the time nanoseconds
    (in ``CLOCK_MONOTONIC``) when the scancode was decoded.

.. _lirc-mode-mode2:

``LIRC_MODE_MODE2``

    The driver returns a sequence of pulse and space codes to userspace,
    as a series of u32 values.

    This mode is used only for IR receive.

    The upper 8 bits determine the packet type, and the lower 24 bits
    the payload. Use ``LIRC_VALUE()`` macro to get the payload, and
    the macro ``LIRC_MODE2()`` will give you the type, which
    is one of:

    ``LIRC_MODE2_PULSE``

        Signifies the presence of IR in microseconds.

    ``LIRC_MODE2_SPACE``

        Signifies absence of IR in microseconds.

    ``LIRC_MODE2_FREQUENCY``

        If measurement of the carrier frequency was enabled with
        :ref:`lirc_set_measure_carrier_mode` then this packet gives you
        the carrier frequency in Hertz.

    ``LIRC_MODE2_TIMEOUT``

        If timeout reports are enabled with
        :ref:`lirc_set_rec_timeout_reports`, when the timeout set with
        :ref:`lirc_set_rec_timeout` expires due to no IR being detected,
        this packet will be sent, with the number of microseconds with
        no IR.

.. _lirc-mode-pulse:

``LIRC_MODE_PULSE``

    In pulse mode, a sequence of pulse/space integer values are written to the
    lirc device using :ref:`lirc-write`.

    The values are alternating pulse and space lengths, in microseconds. The
    first and last entry must be a pulse, so there must be an odd number
    of entries.

    This mode is used only for IR send.


**************************
Remote Controller protocol
**************************

An enum :c:type:`rc_proto` in the :ref:`lirc_header` lists all the
supported IR protocols:

.. kernel-doc:: include/uapi/linux/lirc.h
