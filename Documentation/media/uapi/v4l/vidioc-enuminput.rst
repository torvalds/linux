.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_ENUMINPUT:

**********************
ioctl VIDIOC_ENUMINPUT
**********************

Name
====

VIDIOC_ENUMINPUT - Enumerate video inputs


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_ENUMINPUT, struct v4l2_input *argp )
    :name: VIDIOC_ENUMINPUT


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_input`.


Description
===========

To query the attributes of a video input applications initialize the
``index`` field of struct :c:type:`v4l2_input` and call the
:ref:`VIDIOC_ENUMINPUT` with a pointer to this structure. Drivers
fill the rest of the structure or return an ``EINVAL`` error code when the
index is out of bounds. To enumerate all inputs applications shall begin
at index zero, incrementing by one until the driver returns ``EINVAL``.


.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. c:type:: v4l2_input

.. flat-table:: struct v4l2_input
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``index``
      - Identifies the input, set by the application.
    * - __u8
      - ``name``\ [32]
      - Name of the video input, a NUL-terminated ASCII string, for
	example: "Vin (Composite 2)". This information is intended for the
	user, preferably the connector label on the device itself.
    * - __u32
      - ``type``
      - Type of the input, see :ref:`input-type`.
    * - __u32
      - ``audioset``
      - Drivers can enumerate up to 32 video and audio inputs. This field
	shows which audio inputs were selectable as audio source if this
	was the currently selected video input. It is a bit mask. The LSB
	corresponds to audio input 0, the MSB to input 31. Any number of
	bits can be set, or none.

	When the driver does not enumerate audio inputs no bits must be
	set. Applications shall not interpret this as lack of audio
	support. Some drivers automatically select audio sources and do
	not enumerate them since there is no choice anyway.

	For details on audio inputs and how to select the current input
	see :ref:`audio`.
    * - __u32
      - ``tuner``
      - Capture devices can have zero or more tuners (RF demodulators).
	When the ``type`` is set to ``V4L2_INPUT_TYPE_TUNER`` this is an
	RF connector and this field identifies the tuner. It corresponds
	to struct :c:type:`v4l2_tuner` field ``index``. For
	details on tuners see :ref:`tuner`.
    * - :ref:`v4l2_std_id <v4l2-std-id>`
      - ``std``
      - Every video input supports one or more different video standards.
	This field is a set of all supported standards. For details on
	video standards and how to switch see :ref:`standard`.
    * - __u32
      - ``status``
      - This field provides status information about the input. See
	:ref:`input-status` for flags. With the exception of the sensor
	orientation bits ``status`` is only valid when this is the current
	input.
    * - __u32
      - ``capabilities``
      - This field provides capabilities for the input. See
	:ref:`input-capabilities` for flags.
    * - __u32
      - ``reserved``\ [3]
      - Reserved for future extensions. Drivers must set the array to
	zero.



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _input-type:

.. flat-table:: Input Types
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_INPUT_TYPE_TUNER``
      - 1
      - This input uses a tuner (RF demodulator).
    * - ``V4L2_INPUT_TYPE_CAMERA``
      - 2
      - Any non-tuner video input, for example Composite Video,
	S-Video, HDMI, camera sensor. The naming as ``_TYPE_CAMERA`` is historical,
	today we would have called it ``_TYPE_VIDEO``.
    * - ``V4L2_INPUT_TYPE_TOUCH``
      - 3
      - This input is a touch device for capturing raw touch data.



.. tabularcolumns:: |p{4.8cm}|p{2.6cm}|p{10.1cm}|

.. _input-status:

.. flat-table:: Input Status Flags
    :header-rows:  0
    :stub-columns: 0

    * - :cspan:`2` General
    * - ``V4L2_IN_ST_NO_POWER``
      - 0x00000001
      - Attached device is off.
    * - ``V4L2_IN_ST_NO_SIGNAL``
      - 0x00000002
      -
    * - ``V4L2_IN_ST_NO_COLOR``
      - 0x00000004
      - The hardware supports color decoding, but does not detect color
	modulation in the signal.
    * - :cspan:`2` Sensor Orientation
    * - ``V4L2_IN_ST_HFLIP``
      - 0x00000010
      - The input is connected to a device that produces a signal that is
	flipped horizontally and does not correct this before passing the
	signal to userspace.
    * - ``V4L2_IN_ST_VFLIP``
      - 0x00000020
      - The input is connected to a device that produces a signal that is
	flipped vertically and does not correct this before passing the
	signal to userspace.
	.. note:: A 180 degree rotation is the same as HFLIP | VFLIP
    * - :cspan:`2` Analog Video
    * - ``V4L2_IN_ST_NO_H_LOCK``
      - 0x00000100
      - No horizontal sync lock.
    * - ``V4L2_IN_ST_COLOR_KILL``
      - 0x00000200
      - A color killer circuit automatically disables color decoding when
	it detects no color modulation. When this flag is set the color
	killer is enabled *and* has shut off color decoding.
    * - ``V4L2_IN_ST_NO_V_LOCK``
      - 0x00000400
      - No vertical sync lock.
    * - ``V4L2_IN_ST_NO_STD_LOCK``
      - 0x00000800
      - No standard format lock in case of auto-detection format
	by the component.
    * - :cspan:`2` Digital Video
    * - ``V4L2_IN_ST_NO_SYNC``
      - 0x00010000
      - No synchronization lock.
    * - ``V4L2_IN_ST_NO_EQU``
      - 0x00020000
      - No equalizer lock.
    * - ``V4L2_IN_ST_NO_CARRIER``
      - 0x00040000
      - Carrier recovery failed.
    * - :cspan:`2` VCR and Set-Top Box
    * - ``V4L2_IN_ST_MACROVISION``
      - 0x01000000
      - Macrovision is an analog copy prevention system mangling the video
	signal to confuse video recorders. When this flag is set
	Macrovision has been detected.
    * - ``V4L2_IN_ST_NO_ACCESS``
      - 0x02000000
      - Conditional access denied.
    * - ``V4L2_IN_ST_VTR``
      - 0x04000000
      - VTR time constant. [?]



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _input-capabilities:

.. flat-table:: Input capabilities
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_IN_CAP_DV_TIMINGS``
      - 0x00000002
      - This input supports setting video timings by using
	``VIDIOC_S_DV_TIMINGS``.
    * - ``V4L2_IN_CAP_STD``
      - 0x00000004
      - This input supports setting the TV standard by using
	``VIDIOC_S_STD``.
    * - ``V4L2_IN_CAP_NATIVE_SIZE``
      - 0x00000008
      - This input supports setting the native size using the
	``V4L2_SEL_TGT_NATIVE_SIZE`` selection target, see
	:ref:`v4l2-selections-common`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :c:type:`v4l2_input` ``index`` is out of
    bounds.
