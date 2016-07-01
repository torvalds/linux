.. -*- coding: utf-8; mode: rst -*-

.. _FE_READ_BER:

***********
FE_READ_BER
***********

DESCRIPTION

This ioctl call returns the bit error rate for the signal currently
received/demodulated by the front-end. For this command, read-only
access to the device is sufficient.

SYNOPSIS

int ioctl(int fd, int request = :ref:`FE_READ_BER`,
uint32_t *ber);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals :ref:`FE_READ_BER` for this command.

    -  .. row 3

       -  uint32_t *ber

       -  The bit error rate is stored into *ber.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------
