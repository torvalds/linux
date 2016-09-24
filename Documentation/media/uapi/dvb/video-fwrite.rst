.. -*- coding: utf-8; mode: rst -*-

.. _video_fwrite:

=================
dvb video write()
=================

Name
----

dvb video write()


Synopsis
--------

.. cpp:function:: size_t write(int fd, const void *buf, size_t count)


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  void \*buf

       -  Pointer to the buffer containing the PES data.

    -  .. row 3

       -  size_t count

       -  Size of buf.


Description
-----------

This system call can only be used if VIDEO_SOURCE_MEMORY is selected
in the ioctl call VIDEO_SELECT_SOURCE. The data provided shall be in
PES format, unless the capability allows other formats. If O_NONBLOCK
is not specified the function will block until buffer space is
available. The amount of data to be transferred is implied by count.


Return Value
------------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EPERM``

       -  Mode VIDEO_SOURCE_MEMORY not selected.

    -  .. row 2

       -  ``ENOMEM``

       -  Attempted to write more data than the internal buffer can hold.

    -  .. row 3

       -  ``EBADF``

       -  fd is not a valid open file descriptor.
