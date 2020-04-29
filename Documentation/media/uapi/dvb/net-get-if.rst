.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _NET_GET_IF:

****************
ioctl NET_GET_IF
****************

Name
====

NET_GET_IF - Read the configuration data of an interface created via - :ref:`NET_ADD_IF <net>`.


Synopsis
========

.. c:function:: int ioctl( int fd, NET_GET_IF, struct dvb_net_if *net_if )
    :name: NET_GET_IF


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

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
