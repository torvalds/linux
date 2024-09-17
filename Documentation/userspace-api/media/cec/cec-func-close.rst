.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: CEC

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

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

Description
===========

Closes the cec device. Resources associated with the file descriptor are
freed. The device configuration remain unchanged.

Return Value
============

:c:func:`close()` returns 0 on success. On error, -1 is returned, and
``errno`` is set appropriately. Possible error codes are:

``EBADF``
    ``fd`` is not a valid open file descriptor.
