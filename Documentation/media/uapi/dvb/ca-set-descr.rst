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

.. c:function:: int ioctl(fd, CA_SET_DESCR, struct ca_descr *desc)
    :name: CA_SET_DESCR


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <cec-open>`.

``msg``
  Pointer to struct :c:type:`ca_descr`.

Description
-----------

CA_SET_DESCR is used for feeding descrambler CA slots with descrambling
keys (refered as control words).

Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
