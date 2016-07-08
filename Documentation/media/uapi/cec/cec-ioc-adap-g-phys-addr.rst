.. -*- coding: utf-8; mode: rst -*-

.. _CEC_ADAP_PHYS_ADDR:
.. _CEC_ADAP_G_PHYS_ADDR:
.. _CEC_ADAP_S_PHYS_ADDR:

************************************************
ioctl CEC_ADAP_G_PHYS_ADDR, CEC_ADAP_S_PHYS_ADDR
************************************************

Name
====

CEC_ADAP_G_PHYS_ADDR, CEC_ADAP_S_PHYS_ADDR - Get or set the physical address


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, __u16 *argp )

Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <cec-func-open>`.

``request``
    CEC_ADAP_G_PHYS_ADDR, CEC_ADAP_S_PHYS_ADDR

``argp``


Description
===========

Note: this documents the proposed CEC API. This API is not yet finalized
and is currently only available as a staging kernel module.

To query the current physical address applications call the
:ref:`CEC_ADAP_G_PHYS_ADDR` ioctl with a pointer to an __u16 where the
driver stores the physical address.

To set a new physical address applications store the physical address in
an __u16 and call the :ref:`CEC_ADAP_S_PHYS_ADDR` ioctl with a pointer to
this integer. :ref:`CEC_ADAP_S_PHYS_ADDR` is only available if
``CEC_CAP_PHYS_ADDR`` is set (ENOTTY error code will be returned
otherwise). :ref:`CEC_ADAP_S_PHYS_ADDR` can only be called by a file handle
in initiator mode (see :ref:`CEC_S_MODE`), if not
EBUSY error code will be returned.

The physical address is a 16-bit number where each group of 4 bits
represent a digit of the physical address a.b.c.d where the most
significant 4 bits represent 'a'. The CEC root device (usually the TV)
has address 0.0.0.0. Every device that is hooked up to an input of the
TV has address a.0.0.0 (where 'a' is ≥ 1), devices hooked up to those in
turn have addresses a.b.0.0, etc. So a topology of up to 5 devices deep
is supported. The physical address a device shall use is stored in the
EDID of the sink.

For example, the EDID for each HDMI input of the TV will have a
different physical address of the form a.0.0.0 that the sources will
read out and use as their physical address.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
