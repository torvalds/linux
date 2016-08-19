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

.. c:function:: int  ioctl(fd, CA_GET_DESCR_INFO, ca_descr_info_t *desc)
    :name: CA_GET_DESCR_INFO

Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <dvb-ca-open>`.

``desc``
  Undocumented.


Description
-----------

.. note:: This ioctl is undocumented. Documentation is welcome.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
