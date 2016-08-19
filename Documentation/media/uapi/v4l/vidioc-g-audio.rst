.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_G_AUDIO:

************************************
ioctl VIDIOC_G_AUDIO, VIDIOC_S_AUDIO
************************************

Name
====

VIDIOC_G_AUDIO - VIDIOC_S_AUDIO - Query or select the current audio input and its attributes


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_G_AUDIO, struct v4l2_audio *argp )
    :name: VIDIOC_G_AUDIO

.. c:function:: int ioctl( int fd, VIDIOC_S_AUDIO, const struct v4l2_audio *argp )
    :name: VIDIOC_S_AUDIO


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``


Description
===========

To query the current audio input applications zero out the ``reserved``
array of a struct :ref:`v4l2_audio <v4l2-audio>` and call the
:ref:`VIDIOC_G_AUDIO <VIDIOC_G_AUDIO>` ioctl with a pointer to this structure. Drivers fill
the rest of the structure or return an ``EINVAL`` error code when the device
has no audio inputs, or none which combine with the current video input.

Audio inputs have one writable property, the audio mode. To select the
current audio input *and* change the audio mode, applications initialize
the ``index`` and ``mode`` fields, and the ``reserved`` array of a
:ref:`struct v4l2_audio <v4l2-audio>` structure and call the :ref:`VIDIOC_S_AUDIO <VIDIOC_G_AUDIO>`
ioctl. Drivers may switch to a different audio mode if the request
cannot be satisfied. However, this is a write-only ioctl, it does not
return the actual new audio mode.


.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. _v4l2-audio:

.. flat-table:: struct v4l2_audio
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``index``

       -  Identifies the audio input, set by the driver or application.

    -  .. row 2

       -  __u8

       -  ``name``\ [32]

       -  Name of the audio input, a NUL-terminated ASCII string, for
	  example: "Line In". This information is intended for the user,
	  preferably the connector label on the device itself.

    -  .. row 3

       -  __u32

       -  ``capability``

       -  Audio capability flags, see :ref:`audio-capability`.

    -  .. row 4

       -  __u32

       -  ``mode``

       -  Audio mode flags set by drivers and applications (on
	  :ref:`VIDIOC_S_AUDIO <VIDIOC_G_AUDIO>` ioctl), see :ref:`audio-mode`.

    -  .. row 5

       -  __u32

       -  ``reserved``\ [2]

       -  Reserved for future extensions. Drivers and applications must set
	  the array to zero.



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _audio-capability:

.. flat-table:: Audio Capability Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_AUDCAP_STEREO``

       -  0x00001

       -  This is a stereo input. The flag is intended to automatically
	  disable stereo recording etc. when the signal is always monaural.
	  The API provides no means to detect if stereo is *received*,
	  unless the audio input belongs to a tuner.

    -  .. row 2

       -  ``V4L2_AUDCAP_AVL``

       -  0x00002

       -  Automatic Volume Level mode is supported.



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _audio-mode:

.. flat-table:: Audio Mode Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_AUDMODE_AVL``

       -  0x00001

       -  AVL mode is on.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    No audio inputs combine with the current video input, or the number
    of the selected audio input is out of bounds or it does not combine.
