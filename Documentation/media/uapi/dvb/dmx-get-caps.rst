.. -*- coding: utf-8; mode: rst -*-

.. _DMX_GET_CAPS:

============
DMX_GET_CAPS
============

Name
----

DMX_GET_CAPS


Synopsis
--------

.. c:function:: int ioctl(fd, DMX_GET_CAPS, struct dmx_caps *caps)
    :name: DMX_GET_CAPS

Arguments
---------

``fd``
    File descriptor returned by :c:func:`open() <dvb-dmx-open>`.

``caps``
    Pointer to struct :c:type:`dmx_caps`


Description
-----------

.. note:: This ioctl is undocumented. Documentation is welcome.

Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
