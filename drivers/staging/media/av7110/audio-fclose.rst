.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.audio

.. _audio_fclose:

========================
Digital TV audio close()
========================

Name
----

Digital TV audio close()

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:function:: int close(int fd)

Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

Description
-----------

This system call closes a previously opened audio device.

Return Value
------------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  .. row 1

       -  ``EBADF``

       -  fd is not a valid open file descriptor.
