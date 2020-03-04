.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _NET_REMOVE_IF:

*******************
ioctl NET_REMOVE_IF
*******************

Name
====

NET_REMOVE_IF - Removes a network interface.


Synopsis
========

.. c:function:: int ioctl( int fd, NET_REMOVE_IF, int ifnum )
    :name: NET_REMOVE_IF


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

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
