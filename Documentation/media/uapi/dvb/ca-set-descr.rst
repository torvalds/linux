.. -*- coding: utf-8; mode: rst -*-

.. _CA_SET_DESCR:

============
CA_SET_DESCR
============

Name
----

CA_SET_DESCR


Synopsis
--------

.. c:function:: int ioctl(fd, CA_SET_DESCR, ca_descr_t *desc)
    :name:


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <cec-open>`.

``msg``
  Undocumented.


Description
-----------

.. note:: This ioctl is undocumented. Documentation is welcome.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
