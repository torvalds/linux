.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.net

.. _NET_REMOVE_IF:

*******************
ioctl NET_REMOVE_IF
*******************

Name
====

NET_REMOVE_IF - Removes a network interface.

Synopsis
========

.. c:macro:: NET_REMOVE_IF

``int ioctl(int fd, NET_REMOVE_IF, int ifnum)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``net_if``
    number of the interface to be removed

Description
===========

The NET_REMOVE_IF ioctl deletes an interface previously created via
:ref:`NET_ADD_IF <net>`.

Return Value
============

On success 0 is returned, and :c:type:`ca_slot_info` is filled.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
