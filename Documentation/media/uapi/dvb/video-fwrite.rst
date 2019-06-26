.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _video_fwrite:

=================
dvb video write()
=================

Name
----

dvb video write()

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: size_t write(int fd, const void *buf, size_t count)


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
