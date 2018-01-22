.. -*- coding: utf-8; mode: rst -*-

.. _CA_GET_DESCR_INFO:

=================
CA_GET_DESCR_INFO
=================

Name
----

CA_GET_DESCR_INFO


Synopsis
--------

.. c:function:: int  ioctl(fd, CA_GET_DESCR_INFO, struct ca_descr_info *desc)
    :name: CA_GET_DESCR_INFO

Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <dvb-ca-open>`.

``desc``
  Pointer to struct :c:type:`ca_descr_info`.

Description
-----------

Returns information about all descrambler slots.

Return Value
------------

On success 0 is returned, and :c:type:`ca_descr_info` is filled.

On error -1 is returned, and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
