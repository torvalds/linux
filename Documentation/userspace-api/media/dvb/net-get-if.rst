.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.net

.. _NET_GET_IF:

****************
ioctl NET_GET_IF
****************

Name
====

NET_GET_IF - Read the configuration data of an interface created via - :ref:`NET_ADD_IF <net>`.

Synopsis
========

.. c:macro:: NET_GET_IF

``int ioctl(int fd, NET_GET_IF, struct dvb_net_if *net_if)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``net_if``
    pointer to struct :c:type:`dvb_net_if`

Description
===========

The NET_GET_IF ioctl uses the interface number given by the struct
:c:type:`dvb_net_if`::ifnum field and fills the content of
struct :c:type:`dvb_net_if` with the packet ID and
encapsulation type used on such interface. If the interface was not
created yet with :ref:`NET_ADD_IF <net>`, it will return -1 and fill
the ``errno`` with ``EINVAL`` error code.

Return Value
============

On success 0 is returned, and :c:type:`ca_slot_info` is filled.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
