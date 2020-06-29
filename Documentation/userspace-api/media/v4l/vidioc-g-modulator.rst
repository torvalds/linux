.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDIOC_G_MODULATOR:

********************************************
ioctl VIDIOC_G_MODULATOR, VIDIOC_S_MODULATOR
********************************************

Name
====

VIDIOC_G_MODULATOR - VIDIOC_S_MODULATOR - Get or set modulator attributes


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_G_MODULATOR, struct v4l2_modulator *argp )
    :name: VIDIOC_G_MODULATOR

.. c:function:: int ioctl( int fd, VIDIOC_S_MODULATOR, const struct v4l2_modulator *argp )
    :name: VIDIOC_S_MODULATOR


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_modulator`.


Description
===========

To query the attributes of a modulator applications initialize the
``index`` field and zero out the ``reserved`` array of a struct
:c:type:`v4l2_modulator` and call the
:ref:`VIDIOC_G_MODULATOR <VIDIOC_G_MODULATOR>` ioctl with a pointer to this structure. Drivers
fill the rest of the structure or return an ``EINVAL`` error code when the
index is out of bounds. To enumerate all modulators applications shall
begin at index zero, incrementing by one until the driver returns
EINVAL.

Modulators have two writable properties, an audio modulation set and the
radio frequency. To change the modulated audio subprograms, applications
initialize the ``index`` and ``txsubchans`` fields and the ``reserved``
array and call the :ref:`VIDIOC_S_MODULATOR <VIDIOC_G_MODULATOR>` ioctl. Drivers may choose a
different audio modulation if the request cannot be satisfied. However
this is a write-only ioctl, it does not return the actual audio
modulation selected.

:ref:`SDR <sdr>` specific modulator types are ``V4L2_TUNER_SDR`` and
``V4L2_TUNER_RF``. For SDR devices ``txsubchans`` field must be
initialized to zero. The term 'modulator' means SDR transmitter in this
context.

To change the radio frequency the
:ref:`VIDIOC_S_FREQUENCY <VIDIOC_G_FREQUENCY>` ioctl is available.


.. tabularcolumns:: |p{2.9cm}|p{2.9cm}|p{5.8cm}|p{2.9cm}|p{3.0cm}|

.. c:type:: v4l2_modulator

.. flat-table:: struct v4l2_modulator
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2 1 1

    * - __u32
      - ``index``
      - Identifies the modulator, set by the application.
    * - __u8
      - ``name``\ [32]
      - Name of the modulator, a NUL-terminated ASCII string.

	This information is intended for the user.
    * - __u32
      - ``capability``
      - Modulator capability flags. No flags are defined for this field,
	the tuner flags in struct :c:type:`v4l2_tuner` are
	used accordingly. The audio flags indicate the ability to encode
	audio subprograms. They will *not* change for example with the
	current video standard.
    * - __u32
      - ``rangelow``
      - The lowest tunable frequency in units of 62.5 KHz, or if the
	``capability`` flag ``V4L2_TUNER_CAP_LOW`` is set, in units of
	62.5 Hz, or if the ``capability`` flag ``V4L2_TUNER_CAP_1HZ`` is
	set, in units of 1 Hz.
    * - __u32
      - ``rangehigh``
      - The highest tunable frequency in units of 62.5 KHz, or if the
	``capability`` flag ``V4L2_TUNER_CAP_LOW`` is set, in units of
	62.5 Hz, or if the ``capability`` flag ``V4L2_TUNER_CAP_1HZ`` is
	set, in units of 1 Hz.
    * - __u32
      - ``txsubchans``
      - With this field applications can determine how audio sub-carriers
	shall be modulated. It contains a set of flags as defined in
	:ref:`modulator-txsubchans`.

	.. note::

	   The tuner ``rxsubchans`` flags  are reused, but the
	   semantics are different. Video output devices
	   are assumed to have an analog or PCM audio input with 1-3
	   channels. The ``txsubchans`` flags select one or more channels
	   for modulation, together with some audio subprogram indicator,
	   for example, a stereo pilot tone.
    * - __u32
      - ``type``
      - :cspan:`2` Type of the modulator, see :c:type:`v4l2_tuner_type`.
    * - __u32
      - ``reserved``\ [3]
      - Reserved for future extensions.

	Drivers and applications must set the array to zero.



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _modulator-txsubchans:

.. flat-table:: Modulator Audio Transmission Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_TUNER_SUB_MONO``
      - 0x0001
      - Modulate channel 1 as mono audio, when the input has more
	channels, a down-mix of channel 1 and 2. This flag does not
	combine with ``V4L2_TUNER_SUB_STEREO`` or
	``V4L2_TUNER_SUB_LANG1``.
    * - ``V4L2_TUNER_SUB_STEREO``
      - 0x0002
      - Modulate channel 1 and 2 as left and right channel of a stereo
	audio signal. When the input has only one channel or two channels
	and ``V4L2_TUNER_SUB_SAP`` is also set, channel 1 is encoded as
	left and right channel. This flag does not combine with
	``V4L2_TUNER_SUB_MONO`` or ``V4L2_TUNER_SUB_LANG1``. When the
	driver does not support stereo audio it shall fall back to mono.
    * - ``V4L2_TUNER_SUB_LANG1``
      - 0x0008
      - Modulate channel 1 and 2 as primary and secondary language of a
	bilingual audio signal. When the input has only one channel it is
	used for both languages. It is not possible to encode the primary
	or secondary language only. This flag does not combine with
	``V4L2_TUNER_SUB_MONO``, ``V4L2_TUNER_SUB_STEREO`` or
	``V4L2_TUNER_SUB_SAP``. If the hardware does not support the
	respective audio matrix, or the current video standard does not
	permit bilingual audio the :ref:`VIDIOC_S_MODULATOR <VIDIOC_G_MODULATOR>` ioctl shall
	return an ``EINVAL`` error code and the driver shall fall back to mono
	or stereo mode.
    * - ``V4L2_TUNER_SUB_LANG2``
      - 0x0004
      - Same effect as ``V4L2_TUNER_SUB_SAP``.
    * - ``V4L2_TUNER_SUB_SAP``
      - 0x0004
      - When combined with ``V4L2_TUNER_SUB_MONO`` the first channel is
	encoded as mono audio, the last channel as Second Audio Program.
	When the input has only one channel it is used for both audio
	tracks. When the input has three channels the mono track is a
	down-mix of channel 1 and 2. When combined with
	``V4L2_TUNER_SUB_STEREO`` channel 1 and 2 are encoded as left and
	right stereo audio, channel 3 as Second Audio Program. When the
	input has only two channels, the first is encoded as left and
	right channel and the second as SAP. When the input has only one
	channel it is used for all audio tracks. It is not possible to
	encode a Second Audio Program only. This flag must combine with
	``V4L2_TUNER_SUB_MONO`` or ``V4L2_TUNER_SUB_STEREO``. If the
	hardware does not support the respective audio matrix, or the
	current video standard does not permit SAP the
	:ref:`VIDIOC_S_MODULATOR <VIDIOC_G_MODULATOR>` ioctl shall return an ``EINVAL`` error code and
	driver shall fall back to mono or stereo mode.
    * - ``V4L2_TUNER_SUB_RDS``
      - 0x0010
      - Enable the RDS encoder for a radio FM transmitter.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :c:type:`v4l2_modulator` ``index`` is
    out of bounds.
