.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _AUDIO_SET_ID:

============
AUDIO_SET_ID
============

Name
----

AUDIO_SET_ID

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:function:: int  ioctl(int fd, AUDIO_SET_ID, int id)
    :name: AUDIO_SET_ID

Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -

       -  int fd

       -  File descriptor returned by a previous call to open().

    -

       -  int id

       -  audio sub-stream id


Description
-----------

This ioctl selects which sub-stream is to be decoded if a program or
system stream is sent to the video device. If no audio stream type is
set the id has to be in [0xC0,0xDF] for MPEG sound, in [0x80,0x87] for
AC3 and in [0xA0,0xA7] for LPCM. More specifications may follow for
other stream types. If the stream type is set the id just specifies the
substream id of the audio stream and only the first 5 bits are
recognized.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
