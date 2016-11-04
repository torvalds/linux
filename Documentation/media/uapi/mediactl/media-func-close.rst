.. -*- coding: utf-8; mode: rst -*-

.. _media-func-close:

*************
media close()
*************

Name
====

media-close - Close a media device


Synopsis
========

.. code-block:: c

    #include <unistd.h>


.. c:function:: int close( int fd )
    :name: mc-close

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <mc-open>`.


Description
===========

Closes the media device. Resources associated with the file descriptor
are freed. The device configuration remain unchanged.


Return Value
============

:ref:`close() <media-func-close>` returns 0 on success. On error, -1 is returned, and
``errno`` is set appropriately. Possible error codes are:

EBADF
    ``fd`` is not a valid open file descriptor.
