.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.fe

.. _FE_DISEQC_SEND_BURST:

**************************
ioctl FE_DISEQC_SEND_BURST
**************************

Name
====

FE_DISEQC_SEND_BURST - Sends a 22KHz tone burst for 2x1 mini DiSEqC satellite selection.

Synopsis
========

.. c:macro:: FE_DISEQC_SEND_BURST

``int ioctl(int fd, FE_DISEQC_SEND_BURST, enum fe_sec_mini_cmd tone)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``tone``
    An integer enumerated value described at :c:type:`fe_sec_mini_cmd`.

Description
===========

This ioctl is used to set the generation of a 22kHz tone burst for mini
DiSEqC satellite selection for 2x1 switches. This call requires
read/write permissions.

It provides support for what's specified at
`Digital Satellite Equipment Control (DiSEqC) - Simple "ToneBurst" Detection Circuit specification. <http://www.eutelsat.com/files/contributed/satellites/pdf/Diseqc/associated%20docs/simple_tone_burst_detec.pdf>`__

Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
