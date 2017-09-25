.. -*- coding: utf-8; mode: rst -*-

.. _frontend_f_close:

***************************
Digital TV frontend close()
***************************

Name
====

fe-close - Close a frontend device


Synopsis
========

.. code-block:: c

    #include <unistd.h>


.. c:function:: int close( int fd )
    :name: dvb-fe-close

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <dvb-fe-open>`.


Description
===========

This system call closes a previously opened front-end device. After
closing a front-end device, its corresponding hardware might be powered
down automatically.


Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
