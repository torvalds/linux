.. -*- coding: utf-8; mode: rst -*-

.. _cec-func-close:

***********
cec close()
***********

Name
====

cec-close - Close a cec device


Synopsis
========

.. code-block:: c

    #include <unistd.h>


.. c:function:: int close( int fd )
    :name: cec-close

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <cec-open>`.


Description
===========

.. note::

   This documents the proposed CEC API. This API is not yet finalized
   and is currently only available as a staging kernel module.

Closes the cec device. Resources associated with the file descriptor are
freed. The device configuration remain unchanged.


Return Value
============

:c:func:`close()` returns 0 on success. On error, -1 is returned, and
``errno`` is set appropriately. Possible error codes are:

``EBADF``
    ``fd`` is not a valid open file descriptor.
