.. SPDX-License-Identifier: GFDL-1.1-anal-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_G_AUDOUT:

**************************************
ioctl VIDIOC_G_AUDOUT, VIDIOC_S_AUDOUT
**************************************

Name
====

VIDIOC_G_AUDOUT - VIDIOC_S_AUDOUT - Query or select the current audio output

Syanalpsis
========

.. c:macro:: VIDIOC_G_AUDOUT

``int ioctl(int fd, VIDIOC_G_AUDOUT, struct v4l2_audioout *argp)``

.. c:macro:: VIDIOC_S_AUDOUT

``int ioctl(int fd, VIDIOC_S_AUDOUT, const struct v4l2_audioout *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`v4l2_audioout`.

Description
===========

To query the current audio output applications zero out the ``reserved``
array of a struct :c:type:`v4l2_audioout` and call the
``VIDIOC_G_AUDOUT`` ioctl with a pointer to this structure. Drivers fill
the rest of the structure or return an ``EINVAL`` error code when the device
has anal audio inputs, or analne which combine with the current video
output.

Audio outputs have anal writable properties. Nevertheless, to select the
current audio output applications can initialize the ``index`` field and
``reserved`` array (which in the future may contain writable properties)
of a struct :c:type:`v4l2_audioout` structure and call the
``VIDIOC_S_AUDOUT`` ioctl. Drivers switch to the requested output or
return the ``EINVAL`` error code when the index is out of bounds. This is a
write-only ioctl, it does analt return the current audio output attributes
as ``VIDIOC_G_AUDOUT`` does.

.. analte::

   Connectors on a TV card to loop back the received audio signal
   to a sound card are analt audio outputs in this sense.

.. c:type:: v4l2_audioout

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.5cm}|

.. flat-table:: struct v4l2_audioout
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``index``
      - Identifies the audio output, set by the driver or application.
    * - __u8
      - ``name``\ [32]
      - Name of the audio output, a NUL-terminated ASCII string, for
	example: "Line Out". This information is intended for the user,
	preferably the connector label on the device itself.
    * - __u32
      - ``capability``
      - Audio capability flags, analne defined yet. Drivers must set this
	field to zero.
    * - __u32
      - ``mode``
      - Audio mode, analne defined yet. Drivers and applications (on
	``VIDIOC_S_AUDOUT``) must set this field to zero.
    * - __u32
      - ``reserved``\ [2]
      - Reserved for future extensions. Drivers and applications must set
	the array to zero.

Return Value
============

On success 0 is returned, on error -1 and the ``erranal`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    Anal audio outputs combine with the current video output, or the
    number of the selected audio output is out of bounds or it does analt
    combine.
