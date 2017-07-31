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

.. c:function:: int ioctl( int fd, VIDIOC_ENUMOUTPUT, struct v4l2_output *argp )
    :name: VIDIOC_ENUMOUTPUT


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``


Description
===========

To query the attributes of a video outputs applications initialize the
``index`` field of struct :c:type:`v4l2_output` and call
the :ref:`VIDIOC_ENUMOUTPUT` ioctl with a pointer to this structure.
Drivers fill the rest of the structure or return an ``EINVAL`` error code
when the index is out of bounds. To enumerate all outputs applications
shall begin at index zero, incrementing by one until the driver returns
EINVAL.


.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. c:type:: v4l2_output

.. flat-table:: struct v4l2_output
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``index``
      - Identifies the output, set by the application.
    * - __u8
      - ``name``\ [32]
      - Name of the video output, a NUL-terminated ASCII string, for
	example: "Vout". This information is intended for the user,
	preferably the connector label on the device itself.
    * - __u32
      - ``type``
      - Type of the output, see :ref:`output-type`.
    * - __u32
      - ``audioset``
      - Drivers can enumerate up to 32 video and audio outputs. This field
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
    * - __u32
      - ``modulator``
      - Output devices can have zero or more RF modulators. When the
	``type`` is ``V4L2_OUTPUT_TYPE_MODULATOR`` this is an RF connector
	and this field identifies the modulator. It corresponds to struct
	:c:type:`v4l2_modulator` field ``index``. For
	details on modulators see :ref:`tuner`.
    * - :ref:`v4l2_std_id <v4l2-std-id>`
      - ``std``
      - Every video output supports one or more different video standards.
	This field is a set of all supported standards. For details on
	video standards and how to switch see :ref:`standard`.
    * - __u32
      - ``capabilities``
      - This field provides capabilities for the output. See
	:ref:`output-capabilities` for flags.
    * - __u32
      - ``reserved``\ [3]
      - Reserved for future extensions. Drivers must set the array to
	zero.



.. tabularcolumns:: |p{7.0cm}|p{1.8cm}|p{8.7cm}|

.. _output-type:

.. flat-table:: Output Type
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_OUTPUT_TYPE_MODULATOR``
      - 1
      - This output is an analog TV modulator.
    * - ``V4L2_OUTPUT_TYPE_ANALOG``
      - 2
      - Analog baseband output, for example Composite / CVBS, S-Video,
	RGB.
    * - ``V4L2_OUTPUT_TYPE_ANALOGVGAOVERLAY``
      - 3
      - [?]



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _output-capabilities:

.. flat-table:: Output capabilities
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_OUT_CAP_DV_TIMINGS``
      - 0x00000002
      - This output supports setting video timings by using
	VIDIOC_S_DV_TIMINGS.
    * - ``V4L2_OUT_CAP_STD``
      - 0x00000004
      - This output supports setting the TV standard by using
	VIDIOC_S_STD.
    * - ``V4L2_OUT_CAP_NATIVE_SIZE``
      - 0x00000008
      - This output supports setting the native size using the
	``V4L2_SEL_TGT_NATIVE_SIZE`` selection target, see
	:ref:`v4l2-selections-common`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :c:type:`v4l2_output` ``index`` is out of
    bounds.
