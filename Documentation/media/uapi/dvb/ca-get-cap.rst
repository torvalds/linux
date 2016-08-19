.. -*- coding: utf-8; mode: rst -*-

.. _CA_GET_CAP:

==========
CA_GET_CAP
==========

Name
----

CA_GET_CAP


Synopsis
--------

.. c:function:: int ioctl(fd, CA_GET_CAP, ca_caps_t *caps)
    :name: CA_GET_CAP


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <dvb-ca-open>`.

``caps``
  Undocumented.


Description
-----------

.. note:: This ioctl is undocumented. Documentation is welcome.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
