.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _AUDIO_GET_CAPABILITIES:

======================
AUDIO_GET_CAPABILITIES
======================

Name
----

AUDIO_GET_CAPABILITIES

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:function:: int ioctl(int fd, AUDIO_GET_CAPABILITIES, unsigned int *cap)
    :name: AUDIO_GET_CAPABILITIES


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -

       -  int fd

       -  File descriptor returned by a previous call to open().

    -

       -  unsigned int \*cap

       -  Returns a bit array of supported sound formats.


Description
-----------

This ioctl call asks the Audio Device to tell us about the decoding
capabilities of the audio hardware.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
