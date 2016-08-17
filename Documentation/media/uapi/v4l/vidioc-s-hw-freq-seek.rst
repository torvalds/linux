.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_S_HW_FREQ_SEEK:

***************************
ioctl VIDIOC_S_HW_FREQ_SEEK
***************************

Name
====

VIDIOC_S_HW_FREQ_SEEK - Perform a hardware frequency seek


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_hw_freq_seek *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_S_HW_FREQ_SEEK

``argp``


Description
===========

Start a hardware frequency seek from the current frequency. To do this
applications initialize the ``tuner``, ``type``, ``seek_upward``,
``wrap_around``, ``spacing``, ``rangelow`` and ``rangehigh`` fields, and
zero out the ``reserved`` array of a struct
:ref:`v4l2_hw_freq_seek <v4l2-hw-freq-seek>` and call the
``VIDIOC_S_HW_FREQ_SEEK`` ioctl with a pointer to this structure.

The ``rangelow`` and ``rangehigh`` fields can be set to a non-zero value
to tell the driver to search a specific band. If the struct
:ref:`v4l2_tuner <v4l2-tuner>` ``capability`` field has the
``V4L2_TUNER_CAP_HWSEEK_PROG_LIM`` flag set, these values must fall
within one of the bands returned by
:ref:`VIDIOC_ENUM_FREQ_BANDS`. If the
``V4L2_TUNER_CAP_HWSEEK_PROG_LIM`` flag is not set, then these values
must exactly match those of one of the bands returned by
:ref:`VIDIOC_ENUM_FREQ_BANDS`. If the
current frequency of the tuner does not fall within the selected band it
will be clamped to fit in the band before the seek is started.

If an error is returned, then the original frequency will be restored.

This ioctl is supported if the ``V4L2_CAP_HW_FREQ_SEEK`` capability is
set.

If this ioctl is called from a non-blocking filehandle, then ``EAGAIN``
error code is returned and no seek takes place.


.. _v4l2-hw-freq-seek:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_hw_freq_seek
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``tuner``

       -  The tuner index number. This is the same value as in the struct
	  :ref:`v4l2_input <v4l2-input>` ``tuner`` field and the struct
	  :ref:`v4l2_tuner <v4l2-tuner>` ``index`` field.

    -  .. row 2

       -  __u32

       -  ``type``

       -  The tuner type. This is the same value as in the struct
	  :ref:`v4l2_tuner <v4l2-tuner>` ``type`` field. See
	  :ref:`v4l2-tuner-type`

    -  .. row 3

       -  __u32

       -  ``seek_upward``

       -  If non-zero, seek upward from the current frequency, else seek
	  downward.

    -  .. row 4

       -  __u32

       -  ``wrap_around``

       -  If non-zero, wrap around when at the end of the frequency range,
	  else stop seeking. The struct :ref:`v4l2_tuner <v4l2-tuner>`
	  ``capability`` field will tell you what the hardware supports.

    -  .. row 5

       -  __u32

       -  ``spacing``

       -  If non-zero, defines the hardware seek resolution in Hz. The
	  driver selects the nearest value that is supported by the device.
	  If spacing is zero a reasonable default value is used.

    -  .. row 6

       -  __u32

       -  ``rangelow``

       -  If non-zero, the lowest tunable frequency of the band to search in
	  units of 62.5 kHz, or if the struct
	  :ref:`v4l2_tuner <v4l2-tuner>` ``capability`` field has the
	  ``V4L2_TUNER_CAP_LOW`` flag set, in units of 62.5 Hz or if the
	  struct :ref:`v4l2_tuner <v4l2-tuner>` ``capability`` field has
	  the ``V4L2_TUNER_CAP_1HZ`` flag set, in units of 1 Hz. If
	  ``rangelow`` is zero a reasonable default value is used.

    -  .. row 7

       -  __u32

       -  ``rangehigh``

       -  If non-zero, the highest tunable frequency of the band to search
	  in units of 62.5 kHz, or if the struct
	  :ref:`v4l2_tuner <v4l2-tuner>` ``capability`` field has the
	  ``V4L2_TUNER_CAP_LOW`` flag set, in units of 62.5 Hz or if the
	  struct :ref:`v4l2_tuner <v4l2-tuner>` ``capability`` field has
	  the ``V4L2_TUNER_CAP_1HZ`` flag set, in units of 1 Hz. If
	  ``rangehigh`` is zero a reasonable default value is used.

    -  .. row 8

       -  __u32

       -  ``reserved``\ [5]

       -  Reserved for future extensions. Applications must set the array to
	  zero.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The ``tuner`` index is out of bounds, the ``wrap_around`` value is
    not supported or one of the values in the ``type``, ``rangelow`` or
    ``rangehigh`` fields is wrong.

EAGAIN
    Attempted to call ``VIDIOC_S_HW_FREQ_SEEK`` with the filehandle in
    non-blocking mode.

ENODATA
    The hardware seek found no channels.

EBUSY
    Another hardware seek is already in progress.
