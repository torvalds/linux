.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_G_FREQUENCY:

********************************************
ioctl VIDIOC_G_FREQUENCY, VIDIOC_S_FREQUENCY
********************************************

Name
====

VIDIOC_G_FREQUENCY - VIDIOC_S_FREQUENCY - Get or set tuner or modulator radio frequency


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_frequency *argp )

.. cpp:function:: int ioctl( int fd, int request, const struct v4l2_frequency *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_G_FREQUENCY, VIDIOC_S_FREQUENCY

``argp``


Description
===========

To get the current tuner or modulator radio frequency applications set
the ``tuner`` field of a struct
:ref:`v4l2_frequency <v4l2-frequency>` to the respective tuner or
modulator number (only input devices have tuners, only output devices
have modulators), zero out the ``reserved`` array and call the
:ref:`VIDIOC_G_FREQUENCY <VIDIOC_G_FREQUENCY>` ioctl with a pointer to this structure. The
driver stores the current frequency in the ``frequency`` field.

To change the current tuner or modulator radio frequency applications
initialize the ``tuner``, ``type`` and ``frequency`` fields, and the
``reserved`` array of a struct :ref:`v4l2_frequency <v4l2-frequency>`
and call the :ref:`VIDIOC_S_FREQUENCY <VIDIOC_G_FREQUENCY>` ioctl with a pointer to this
structure. When the requested frequency is not possible the driver
assumes the closest possible value. However :ref:`VIDIOC_S_FREQUENCY <VIDIOC_G_FREQUENCY>` is a
write-only ioctl, it does not return the actual new frequency.


.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. _v4l2-frequency:

.. flat-table:: struct v4l2_frequency
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``tuner``

       -  The tuner or modulator index number. This is the same value as in
	  the struct :ref:`v4l2_input <v4l2-input>` ``tuner`` field and
	  the struct :ref:`v4l2_tuner <v4l2-tuner>` ``index`` field, or
	  the struct :ref:`v4l2_output <v4l2-output>` ``modulator`` field
	  and the struct :ref:`v4l2_modulator <v4l2-modulator>` ``index``
	  field.

    -  .. row 2

       -  __u32

       -  ``type``

       -  The tuner type. This is the same value as in the struct
	  :ref:`v4l2_tuner <v4l2-tuner>` ``type`` field. The type must be
	  set to ``V4L2_TUNER_RADIO`` for ``/dev/radioX`` device nodes, and
	  to ``V4L2_TUNER_ANALOG_TV`` for all others. Set this field to
	  ``V4L2_TUNER_RADIO`` for modulators (currently only radio
	  modulators are supported). See :ref:`v4l2-tuner-type`

    -  .. row 3

       -  __u32

       -  ``frequency``

       -  Tuning frequency in units of 62.5 kHz, or if the struct
	  :ref:`v4l2_tuner <v4l2-tuner>` or struct
	  :ref:`v4l2_modulator <v4l2-modulator>` ``capability`` flag
	  ``V4L2_TUNER_CAP_LOW`` is set, in units of 62.5 Hz. A 1 Hz unit is
	  used when the ``capability`` flag ``V4L2_TUNER_CAP_1HZ`` is set.

    -  .. row 4

       -  __u32

       -  ``reserved``\ [8]

       -  Reserved for future extensions. Drivers and applications must set
	  the array to zero.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The ``tuner`` index is out of bounds or the value in the ``type``
    field is wrong.

EBUSY
    A hardware seek is in progress.
