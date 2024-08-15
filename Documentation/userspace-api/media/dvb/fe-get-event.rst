.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.fe

.. _FE_GET_EVENT:

************
FE_GET_EVENT
************

Name
====

FE_GET_EVENT

.. attention:: This ioctl is deprecated.

Synopsis
========

.. c:macro:: FE_GET_EVENT

``int ioctl(int fd, FE_GET_EVENT, struct dvb_frontend_event *ev)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``ev``
    Points to the location where the event, if any, is to be stored.

Description
===========

This ioctl call returns a frontend event if available. If an event is
not available, the behavior depends on whether the device is in blocking
or non-blocking mode. In the latter case, the call fails immediately
with errno set to ``EWOULDBLOCK``. In the former case, the call blocks until
an event becomes available.

Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  .. row 1

       -  ``EWOULDBLOCK``

       -  There is no event pending, and the device is in non-blocking mode.

    -  .. row 2

       -  ``EOVERFLOW``

       -  Overflow in event queue - one or more events were lost.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
