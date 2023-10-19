.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.1-no-invariants-or-later
.. c:namespace:: RC

.. _lirc_set_wideband_receiver:

********************************
ioctl LIRC_SET_WIDEBAND_RECEIVER
********************************

Name
====

LIRC_SET_WIDEBAND_RECEIVER - enable wide band receiver.

Synopsis
========

.. c:macro:: LIRC_SET_WIDEBAND_RECEIVER

``int ioctl(int fd, LIRC_SET_WIDEBAND_RECEIVER, __u32 *enable)``

Arguments
=========

``fd``
    File descriptor returned by open().

``enable``
    enable = 1 means enable wideband receiver, enable = 0 means disable
    wideband receiver.

Description
===========

Some receivers are equipped with special wide band receiver which is
intended to be used to learn output of existing remote. This ioctl
allows enabling or disabling it.

This might be useful of receivers that have otherwise narrow band receiver
that prevents them to be used with some remotes. Wide band receiver might
also be more precise. On the other hand its disadvantage it usually
reduced range of reception.

.. note::

    Wide band receiver might be implictly enabled if you enable
    carrier reports. In that case it will be disabled as soon as you disable
    carrier reports. Trying to disable wide band receiver while carrier
    reports are active will do nothing.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
