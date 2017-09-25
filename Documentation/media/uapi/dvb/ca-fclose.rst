.. -*- coding: utf-8; mode: rst -*-

.. _ca_fclose:

=====================
Digital TV CA close()
=====================

Name
----

Digital TV CA close()


Synopsis
--------

.. c:function:: int close(int fd)
    :name: dvb-ca-close


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <dvb-ca-open>`.

Description
-----------

This system call closes a previously opened CA device.


Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
