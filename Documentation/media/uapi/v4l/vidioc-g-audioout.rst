.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_G_AUDOUT:

**************************************
ioctl VIDIOC_G_AUDOUT, VIDIOC_S_AUDOUT
**************************************

Name
====

VIDIOC_G_AUDOUT - VIDIOC_S_AUDOUT - Query or select the current audio output


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_audioout *argp )

.. cpp:function:: int ioctl( int fd, int request, const struct v4l2_audioout *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_G_AUDOUT, VIDIOC_S_AUDOUT

``argp``


Description
===========

To query the current audio output applications zero out the ``reserved``
array of a struct :ref:`v4l2_audioout <v4l2-audioout>` and call the
``VIDIOC_G_AUDOUT`` ioctl with a pointer to this structure. Drivers fill
the rest of the structure or return an ``EINVAL`` error code when the device
has no audio inputs, or none which combine with the current video
output.

Audio outputs have no writable properties. Nevertheless, to select the
current audio output applications can initialize the ``index`` field and
``reserved`` array (which in the future may contain writable properties)
of a :ref:`struct v4l2_audioout <v4l2-audioout>` structure and call the
``VIDIOC_S_AUDOUT`` ioctl. Drivers switch to the requested output or
return the ``EINVAL`` error code when the index is out of bounds. This is a
write-only ioctl, it does not return the current audio output attributes
as ``VIDIOC_G_AUDOUT`` does.

.. note:: Connectors on a TV card to loop back the received audio signal
   to a sound card are not audio outputs in this sense.


.. _v4l2-audioout:

.. flat-table:: struct v4l2_audioout
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``index``

       -  Identifies the audio output, set by the driver or application.

    -  .. row 2

       -  __u8

       -  ``name``\ [32]

       -  Name of the audio output, a NUL-terminated ASCII string, for
	  example: "Line Out". This information is intended for the user,
	  preferably the connector label on the device itself.

    -  .. row 3

       -  __u32

       -  ``capability``

       -  Audio capability flags, none defined yet. Drivers must set this
	  field to zero.

    -  .. row 4

       -  __u32

       -  ``mode``

       -  Audio mode, none defined yet. Drivers and applications (on
	  ``VIDIOC_S_AUDOUT``) must set this field to zero.

    -  .. row 5

       -  __u32

       -  ``reserved``\ [2]

       -  Reserved for future extensions. Drivers and applications must set
	  the array to zero.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    No audio outputs combine with the current video output, or the
    number of the selected audio output is out of bounds or it does not
    combine.
