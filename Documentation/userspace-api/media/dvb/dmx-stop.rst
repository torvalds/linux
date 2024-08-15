.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.dmx

.. _DMX_STOP:

========
DMX_STOP
========

Name
----

DMX_STOP

Synopsis
--------

.. c:macro:: DMX_STOP

``int ioctl(int fd, DMX_STOP)``

Arguments
---------

``fd``
    File descriptor returned by :c:func:`open()`.

Description
-----------

This ioctl call is used to stop the actual filtering operation defined
via the ioctl calls :ref:`DMX_SET_FILTER` or :ref:`DMX_SET_PES_FILTER` and
started via the :ref:`DMX_START` command.

Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
