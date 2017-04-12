.. -*- coding: utf-8; mode: rst -*-

.. _CEC_ADAP_PHYS_ADDR:
.. _CEC_ADAP_G_PHYS_ADDR:
.. _CEC_ADAP_S_PHYS_ADDR:

****************************************************
ioctls CEC_ADAP_G_PHYS_ADDR and CEC_ADAP_S_PHYS_ADDR
****************************************************

Name
====

CEC_ADAP_G_PHYS_ADDR, CEC_ADAP_S_PHYS_ADDR - Get or set the physical address


Synopsis
========

.. c:function:: int ioctl( int fd, CEC_ADAP_G_PHYS_ADDR, __u16 *argp )
    :name: CEC_ADAP_G_PHYS_ADDR

.. c:function:: int ioctl( int fd, CEC_ADAP_S_PHYS_ADDR, __u16 *argp )
    :name: CEC_ADAP_S_PHYS_ADDR

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <cec-open>`.

``argp``
    Pointer to the CEC address.

Description
===========

To query the current physical address applications call
:ref:`ioctl CEC_ADAP_G_PHYS_ADDR <CEC_ADAP_G_PHYS_ADDR>` with a pointer to a __u16 where the
driver stores the physical address.

To set a new physical address applications store the physical address in
a __u16 and call :ref:`ioctl CEC_ADAP_S_PHYS_ADDR <CEC_ADAP_S_PHYS_ADDR>` with a pointer to
this integer. The :ref:`ioctl CEC_ADAP_S_PHYS_ADDR <CEC_ADAP_S_PHYS_ADDR>` is only available if
``CEC_CAP_PHYS_ADDR`` is set (the ``ENOTTY`` error code will be returned
otherwise). The :ref:`ioctl CEC_ADAP_S_PHYS_ADDR <CEC_ADAP_S_PHYS_ADDR>` can only be called
by a file descriptor in initiator mode (see :ref:`CEC_S_MODE`), if not
the ``EBUSY`` error code will be returned.

To clear an existing physical address use ``CEC_PHYS_ADDR_INVALID``.
The adapter will go to the unconfigured state.

If logical address types have been defined (see :ref:`ioctl CEC_ADAP_S_LOG_ADDRS <CEC_ADAP_S_LOG_ADDRS>`),
then this ioctl will block until all
requested logical addresses have been claimed. If the file descriptor is in non-blocking mode
then it will not wait for the logical addresses to be claimed, instead it just returns 0.

A :ref:`CEC_EVENT_STATE_CHANGE <CEC-EVENT-STATE-CHANGE>` event is sent when the physical address
changes.

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
