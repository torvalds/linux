.. -*- coding: utf-8; mode: rst -*-

.. _DMX_GET_EVENT:

=============
DMX_GET_EVENT
=============

Name
----

DMX_GET_EVENT


Synopsis
--------

.. c:function:: int ioctl( int fd, DMX_GET_EVENT, struct dmx_event *ev)
    :name: DMX_GET_EVENT


Arguments
---------

``fd``
    File descriptor returned by :c:func:`open() <dvb-dmx-open>`.

``ev``
    Pointer to the location where the event is to be stored.


Description
-----------

This ioctl call returns an event if available. If an event is not
available, the behavior depends on whether the device is in blocking or
non-blocking mode. In the latter case, the call fails immediately with
errno set to ``EWOULDBLOCK``. In the former case, the call blocks until an
event becomes available.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EWOULDBLOCK``

       -  There is no event pending, and the device is in non-blocking mode.
