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

.. note:: This ioctl is undocumented. Documentation is welcome.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
