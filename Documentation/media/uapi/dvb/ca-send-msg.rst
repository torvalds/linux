.. -*- coding: utf-8; mode: rst -*-

.. _CA_SEND_MSG:

===========
CA_SEND_MSG
===========

Name
----

CA_SEND_MSG


Synopsis
--------

.. c:function:: int ioctl(fd, CA_SEND_MSG, struct ca_msg *msg)
    :name: CA_SEND_MSG


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <cec-open>`.

``msg``
  Pointer to struct :c:type:`ca_msg`.


Description
-----------

.. note:: This ioctl is undocumented. Documentation is welcome.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
