.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDEO_GET_CAPABILITIES:

======================
VIDEO_GET_CAPABILITIES
======================

Name
----

VIDEO_GET_CAPABILITIES

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_GET_CAPABILITIES, unsigned int *cap)
    :name: VIDEO_GET_CAPABILITIES


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

       -  Equals VIDEO_GET_CAPABILITIES for this command.

    -  .. row 3

       -  unsigned int \*cap

       -  Pointer to a location where to store the capability information.


Description
-----------

This ioctl call asks the video device about its decoding capabilities.
On success it returns and integer which has bits set according to the
defines in section ??.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
