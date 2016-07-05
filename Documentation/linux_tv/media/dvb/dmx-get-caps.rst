.. -*- coding: utf-8; mode: rst -*-

.. _DMX_GET_CAPS:

============
DMX_GET_CAPS
============

NAME
----

DMX_GET_CAPS

SYNOPSIS
--------

.. c:function:: int ioctl(fd, int request = DMX_GET_CAPS, dmx_caps_t *)


ARGUMENTS
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_GET_CAPS for this command.

    -  .. row 3

       -  dmx_caps_t *

       -  Undocumented.


DESCRIPTION
-----------

This ioctl is undocumented. Documentation is welcome.


RETURN VALUE
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
