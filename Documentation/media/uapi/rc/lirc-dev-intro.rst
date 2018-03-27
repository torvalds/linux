.. -*- coding: utf-8; mode: rst -*-

.. _lirc_dev_intro:

************
Introduction
************

The LIRC device interface is a bi-directional interface for transporting
raw IR data between userspace and kernelspace. Fundamentally, it is just
a chardev (/dev/lircX, for X = 0, 1, 2, ...), with a number of standard
struct file_operations defined on it. With respect to transporting raw
IR data to and fro, the essential fops are read, write and ioctl.

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
