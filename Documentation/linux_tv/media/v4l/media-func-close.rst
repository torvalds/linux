.. -*- coding: utf-8; mode: rst -*-

.. _media-func-close:

*************
media close()
*************

*man media-close(2)*

Close a media device


Synopsis
========

.. code-block:: c

    #include <unistd.h>


.. cpp:function:: int close( int fd )

Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.


Description
===========

Closes the media device. Resources associated with the file descriptor
are freed. The device configuration remain unchanged.


Return Value
============

:c:func:`close()` returns 0 on success. On error, -1 is returned, and
``errno`` is set appropriately. Possible error codes are:

EBADF
    ``fd`` is not a valid open file descriptor.


.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------
