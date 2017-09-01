.. -*- coding: utf-8; mode: rst -*-

.. _CA_RESET:

========
CA_RESET
========

Name
----

CA_RESET


Synopsis
--------

.. c:function:: int ioctl(fd, CA_RESET)
    :name: CA_RESET


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <cec-open>`.

Description
-----------

Puts the Conditional Access hardware on its initial state. It should
be called before start using the CA hardware.


Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
