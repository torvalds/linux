.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_ENUMOUTPUT:

***********************
ioctl VIDIOC_ENUMOUTPUT
***********************

Name
====

VIDIOC_ENUMOUTPUT - Enumerate video outputs


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_output *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_ENUMOUTPUT

``argp``


Description
===========

To query the attributes of a video outputs applications initialize the
``index`` field of struct :ref:`v4l2_output <v4l2-output>` and call
the :ref:`VIDIOC_ENUMOUTPUT` ioctl with a pointer to this structure.
Drivers fill the rest of the structure or return an ``EINVAL`` error code
when the index is out of bounds. To enumerate all outputs applications
shall begin at index zero, incrementing by one until the driver returns
EINVAL.


.. _v4l2-output:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_output
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``index``

       -  Identifies the output, set by the application.

    -  .. row 2

       -  __u8

       -  ``name``\ [32]

       -  Name of the video output, a NUL-terminated ASCII string, for
	  example: "Vout". This information is intended for the user,
	  preferably the connector label on the device itself.

    -  .. row 3

       -  __u32

       -  ``type``

       -  Type of the output, see :ref:`output-type`.

    -  .. row 4

       -  __u32

       -  ``audioset``

       -  Drivers can enumerate up to 32 video and audio outputs. This field
	  shows which audio outputs were selectable as the current output if
	  this was the currently selected video output. It is a bit mask.
	  The LSB corresponds to audio output 0, the MSB to output 31. Any
	  number of bits can be set, or none.

	  When the driver does not enumerate audio outputs no bits must be
	  set. Applications shall not interpret this as lack of audio
	  support. Drivers may automatically select audio outputs without
	  enumerating them.

	  For details on audio outputs and how to select the current output
	  see :ref:`audio`.

    -  .. row 5

       -  __u32

       -  ``modulator``

       -  Output devices can have zero or more RF modulators. When the
	  ``type`` is ``V4L2_OUTPUT_TYPE_MODULATOR`` this is an RF connector
	  and this field identifies the modulator. It corresponds to struct
	  :ref:`v4l2_modulator <v4l2-modulator>` field ``index``. For
	  details on modulators see :ref:`tuner`.

    -  .. row 6

       -  :ref:`v4l2_std_id <v4l2-std-id>`

       -  ``std``

       -  Every video output supports one or more different video standards.
	  This field is a set of all supported standards. For details on
	  video standards and how to switch see :ref:`standard`.

    -  .. row 7

       -  __u32

       -  ``capabilities``

       -  This field provides capabilities for the output. See
	  :ref:`output-capabilities` for flags.

    -  .. row 8

       -  __u32

       -  ``reserved``\ [3]

       -  Reserved for future extensions. Drivers must set the array to
	  zero.



.. _output-type:

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: Output Type
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_OUTPUT_TYPE_MODULATOR``

       -  1

       -  This output is an analog TV modulator.

    -  .. row 2

       -  ``V4L2_OUTPUT_TYPE_ANALOG``

       -  2

       -  Analog baseband output, for example Composite / CVBS, S-Video,
	  RGB.

    -  .. row 3

       -  ``V4L2_OUTPUT_TYPE_ANALOGVGAOVERLAY``

       -  3

       -  [?]



.. _output-capabilities:

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: Output capabilities
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_OUT_CAP_DV_TIMINGS``

       -  0x00000002

       -  This output supports setting video timings by using
	  VIDIOC_S_DV_TIMINGS.

    -  .. row 2

       -  ``V4L2_OUT_CAP_STD``

       -  0x00000004

       -  This output supports setting the TV standard by using
	  VIDIOC_S_STD.

    -  .. row 3

       -  ``V4L2_OUT_CAP_NATIVE_SIZE``

       -  0x00000008

       -  This output supports setting the native size using the
	  ``V4L2_SEL_TGT_NATIVE_SIZE`` selection target, see
	  :ref:`v4l2-selections-common`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :ref:`v4l2_output <v4l2-output>` ``index`` is out of
    bounds.
