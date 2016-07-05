.. -*- coding: utf-8; mode: rst -*-

.. _frontend_f_close:

********************
DVB frontend close()
********************

NAME
====

fe-close - Close a frontend device

SYNOPSIS
========

.. code-block:: c

    #include <unistd.h>


.. cpp:function:: int close( int fd )


ARGUMENTS
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.


DESCRIPTION
===========

This system call closes a previously opened front-end device. After
closing a front-end device, its corresponding hardware might be powered
down automatically.


RETURN VALUE
============

The function returns 0 on success, -1 on failure and the ``errno`` is
set appropriately. Possible error codes:

EBADF
    ``fd`` is not a valid open file descriptor.
