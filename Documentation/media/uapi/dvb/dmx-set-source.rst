.. -*- coding: utf-8; mode: rst -*-

.. _DMX_SET_SOURCE:

==============
DMX_SET_SOURCE
==============

Name
----

DMX_SET_SOURCE


Synopsis
--------

.. c:function:: int ioctl(fd, DMX_SET_SOURCE, dmx_source_t *src)
    :name: DMX_SET_SOURCE


Arguments
---------


``fd``
    File descriptor returned by :c:func:`open() <dvb-dmx-open>`.

``src``
   Undocumented.


Description
-----------

.. note:: This ioctl is undocumented. Documentation is welcome.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
