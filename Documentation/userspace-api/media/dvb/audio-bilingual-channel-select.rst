.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _AUDIO_BILINGUAL_CHANNEL_SELECT:

==============================
AUDIO_BILINGUAL_CHANNEL_SELECT
==============================

Name
----

AUDIO_BILINGUAL_CHANNEL_SELECT

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:function:: int ioctl(int fd, AUDIO_BILINGUAL_CHANNEL_SELECT, struct *audio_channel_select)
    :name: AUDIO_BILINGUAL_CHANNEL_SELECT


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -

       -  int fd

       -  File descriptor returned by a previous call to open().

    -

       -  audio_channel_select_t ch

       -  Select the output format of the audio (mono left/right, stereo).


Description
-----------

This ioctl is obsolete. Do not use in new drivers. It has been replaced
by the V4L2 ``V4L2_CID_MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK`` control
for MPEG decoders controlled through V4L2.

This ioctl call asks the Audio Device to select the requested channel
for bilingual streams if possible.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
