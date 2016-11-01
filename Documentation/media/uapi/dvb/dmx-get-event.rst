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

.. cpp:function:: int ioctl( int fd, int request = DMX_GET_EVENT, struct dmx_event *ev)


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_GET_EVENT for this command.

    -  .. row 3

       -  struct dmx_event \*ev

       -  Pointer to the location where the event is to be stored.


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
