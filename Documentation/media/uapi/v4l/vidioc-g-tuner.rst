.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDIOC_G_TUNER:

************************************
ioctl VIDIOC_G_TUNER, VIDIOC_S_TUNER
************************************

Name
====

VIDIOC_G_TUNER - VIDIOC_S_TUNER - Get or set tuner attributes


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_G_TUNER, struct v4l2_tuner *argp )
    :name: VIDIOC_G_TUNER

.. c:function:: int ioctl( int fd, VIDIOC_S_TUNER, const struct v4l2_tuner *argp )
    :name: VIDIOC_S_TUNER


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_tuner`.


Description
===========

To query the attributes of a tuner applications initialize the ``index``
field and zero out the ``reserved`` array of a struct
:c:type:`v4l2_tuner` and call the ``VIDIOC_G_TUNER`` ioctl
with a pointer to this structure. Drivers fill the rest of the structure
or return an ``EINVAL`` error code when the index is out of bounds. To
enumerate all tuners applications shall begin at index zero,
incrementing by one until the driver returns ``EINVAL``.

Tuners have two writable properties, the audio mode and the radio
frequency. To change the audio mode, applications initialize the
``index``, ``audmode`` and ``reserved`` fields and call the
``VIDIOC_S_TUNER`` ioctl. This will *not* change the current tuner,
which is determined by the current video input. Drivers may choose a
different audio mode if the requested mode is invalid or unsupported.
Since this is a write-only ioctl, it does not return the actually
selected audio mode.

:ref:`SDR <sdr>` specific tuner types are ``V4L2_TUNER_SDR`` and
``V4L2_TUNER_RF``. For SDR devices ``audmode`` field must be initialized
to zero. The term 'tuner' means SDR receiver in this context.

To change the radio frequency the
:ref:`VIDIOC_S_FREQUENCY <VIDIOC_G_FREQUENCY>` ioctl is available.


 .. tabularcolumns:: |p{1.3cm}|p{3.0cm}|p{6.6cm}|p{6.6cm}|

.. c:type:: v4l2_tuner

.. cssclass:: longtable

.. flat-table:: struct v4l2_tuner
    :header-rows:  0
    :stub-columns: 0

    * - __u32
      - ``index``
      - :cspan:`1` Identifies the tuner, set by the application.
    * - __u8
      - ``name``\ [32]
      - :cspan:`1`

	Name of the tuner, a NUL-terminated ASCII string.

	This information is intended for the user.
    * - __u32
      - ``type``
      - :cspan:`1` Type of the tuner, see :c:type:`v4l2_tuner_type`.
    * - __u32
      - ``capability``
      - :cspan:`1`

	Tuner capability flags, see :ref:`tuner-capability`. Audio flags
	indicate the ability to decode audio subprograms. They will *not*
	change, for example with the current video standard.

	When the structure refers to a radio tuner the
	``V4L2_TUNER_CAP_LANG1``, ``V4L2_TUNER_CAP_LANG2`` and
	``V4L2_TUNER_CAP_NORM`` flags can't be used.

	If multiple frequency bands are supported, then ``capability`` is
	the union of all ``capability`` fields of each struct
	:c:type:`v4l2_frequency_band`.
    * - __u32
      - ``rangelow``
      - :cspan:`1` The lowest tunable frequency in units of 62.5 kHz, or
	if the ``capability`` flag ``V4L2_TUNER_CAP_LOW`` is set, in units
	of 62.5 Hz, or if the ``capability`` flag ``V4L2_TUNER_CAP_1HZ``
	is set, in units of 1 Hz. If multiple frequency bands are
	supported, then ``rangelow`` is the lowest frequency of all the
	frequency bands.
    * - __u32
      - ``rangehigh``
      - :cspan:`1` The highest tunable frequency in units of 62.5 kHz,
	or if the ``capability`` flag ``V4L2_TUNER_CAP_LOW`` is set, in
	units of 62.5 Hz, or if the ``capability`` flag
	``V4L2_TUNER_CAP_1HZ`` is set, in units of 1 Hz. If multiple
	frequency bands are supported, then ``rangehigh`` is the highest
	frequency of all the frequency bands.
    * - __u32
      - ``rxsubchans``
      - :cspan:`1`

	Some tuners or audio decoders can determine the received audio
	subprograms by analyzing audio carriers, pilot tones or other
	indicators. To pass this information drivers set flags defined in
	:ref:`tuner-rxsubchans` in this field. For example:
    * -
      -
      - ``V4L2_TUNER_SUB_MONO``
      - receiving mono audio
    * -
      -
      - ``STEREO | SAP``
      - receiving stereo audio and a secondary audio program
    * -
      -
      - ``MONO | STEREO``
      - receiving mono or stereo audio, the hardware cannot distinguish
    * -
      -
      - ``LANG1 | LANG2``
      - receiving bilingual audio
    * -
      -
      - ``MONO | STEREO | LANG1 | LANG2``
      - receiving mono, stereo or bilingual audio
    * -
      -
      - :cspan:`1`

	When the ``V4L2_TUNER_CAP_STEREO``, ``_LANG1``, ``_LANG2`` or
	``_SAP`` flag is cleared in the ``capability`` field, the
	corresponding ``V4L2_TUNER_SUB_`` flag must not be set here.

	This field is valid only if this is the tuner of the current video
	input, or when the structure refers to a radio tuner.
    * - __u32
      - ``audmode``
      - :cspan:`1`

	The selected audio mode, see :ref:`tuner-audmode` for valid
	values. The audio mode does not affect audio subprogram detection,
	and like a :ref:`control` it does not automatically
	change unless the requested mode is invalid or unsupported. See
	:ref:`tuner-matrix` for possible results when the selected and
	received audio programs do not match.

	Currently this is the only field of struct
	struct :c:type:`v4l2_tuner` applications can change.
    * - __u32
      - ``signal``
      - :cspan:`1` The signal strength if known.

	Ranging from 0 to 65535. Higher values indicate a better signal.
    * - __s32
      - ``afc``
      - :cspan:`1` Automatic frequency control.

	When the ``afc`` value is negative, the frequency is too
	low, when positive too high.
    * - __u32
      - ``reserved``\ [4]
      - :cspan:`1` Reserved for future extensions.

	Drivers and applications must set the array to zero.



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. c:type:: v4l2_tuner_type

.. flat-table:: enum v4l2_tuner_type
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 6

    * - ``V4L2_TUNER_RADIO``
      - 1
      - Tuner supports radio
    * - ``V4L2_TUNER_ANALOG_TV``
      - 2
      - Tuner supports analog TV
    * - ``V4L2_TUNER_SDR``
      - 4
      - Tuner controls the A/D and/or D/A block of a
	Software Digital Radio (SDR)
    * - ``V4L2_TUNER_RF``
      - 5
      - Tuner controls the RF part of a Software Digital Radio (SDR)


.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _tuner-capability:

.. cssclass:: longtable

.. flat-table:: Tuner and Modulator Capability Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_TUNER_CAP_LOW``
      - 0x0001
      - When set, tuning frequencies are expressed in units of 62.5 Hz
	instead of 62.5 kHz.
    * - ``V4L2_TUNER_CAP_NORM``
      - 0x0002
      - This is a multi-standard tuner; the video standard can or must be
	switched. (B/G PAL tuners for example are typically not considered
	multi-standard because the video standard is automatically
	determined from the frequency band.) The set of supported video
	standards is available from the struct
	:c:type:`v4l2_input` pointing to this tuner, see the
	description of ioctl :ref:`VIDIOC_ENUMINPUT`
	for details. Only ``V4L2_TUNER_ANALOG_TV`` tuners can have this
	capability.
    * - ``V4L2_TUNER_CAP_HWSEEK_BOUNDED``
      - 0x0004
      - If set, then this tuner supports the hardware seek functionality
	where the seek stops when it reaches the end of the frequency
	range.
    * - ``V4L2_TUNER_CAP_HWSEEK_WRAP``
      - 0x0008
      - If set, then this tuner supports the hardware seek functionality
	where the seek wraps around when it reaches the end of the
	frequency range.
    * - ``V4L2_TUNER_CAP_STEREO``
      - 0x0010
      - Stereo audio reception is supported.
    * - ``V4L2_TUNER_CAP_LANG1``
      - 0x0040
      - Reception of the primary language of a bilingual audio program is
	supported. Bilingual audio is a feature of two-channel systems,
	transmitting the primary language monaural on the main audio
	carrier and a secondary language monaural on a second carrier.
	Only ``V4L2_TUNER_ANALOG_TV`` tuners can have this capability.
    * - ``V4L2_TUNER_CAP_LANG2``
      - 0x0020
      - Reception of the secondary language of a bilingual audio program
	is supported. Only ``V4L2_TUNER_ANALOG_TV`` tuners can have this
	capability.
    * - ``V4L2_TUNER_CAP_SAP``
      - 0x0020
      - Reception of a secondary audio program is supported. This is a
	feature of the BTSC system which accompanies the NTSC video
	standard. Two audio carriers are available for mono or stereo
	transmissions of a primary language, and an independent third
	carrier for a monaural secondary language. Only
	``V4L2_TUNER_ANALOG_TV`` tuners can have this capability.

	.. note::

	   The ``V4L2_TUNER_CAP_LANG2`` and ``V4L2_TUNER_CAP_SAP``
	   flags are synonyms. ``V4L2_TUNER_CAP_SAP`` applies when the tuner
	   supports the ``V4L2_STD_NTSC_M`` video standard.
    * - ``V4L2_TUNER_CAP_RDS``
      - 0x0080
      - RDS capture is supported. This capability is only valid for radio
	tuners.
    * - ``V4L2_TUNER_CAP_RDS_BLOCK_IO``
      - 0x0100
      - The RDS data is passed as unparsed RDS blocks.
    * - ``V4L2_TUNER_CAP_RDS_CONTROLS``
      - 0x0200
      - The RDS data is parsed by the hardware and set via controls.
    * - ``V4L2_TUNER_CAP_FREQ_BANDS``
      - 0x0400
      - The :ref:`VIDIOC_ENUM_FREQ_BANDS`
	ioctl can be used to enumerate the available frequency bands.
    * - ``V4L2_TUNER_CAP_HWSEEK_PROG_LIM``
      - 0x0800
      - The range to search when using the hardware seek functionality is
	programmable, see
	:ref:`VIDIOC_S_HW_FREQ_SEEK` for
	details.
    * - ``V4L2_TUNER_CAP_1HZ``
      - 0x1000
      - When set, tuning frequencies are expressed in units of 1 Hz
	instead of 62.5 kHz.



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _tuner-rxsubchans:

.. flat-table:: Tuner Audio Reception Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_TUNER_SUB_MONO``
      - 0x0001
      - The tuner receives a mono audio signal.
    * - ``V4L2_TUNER_SUB_STEREO``
      - 0x0002
      - The tuner receives a stereo audio signal.
    * - ``V4L2_TUNER_SUB_LANG1``
      - 0x0008
      - The tuner receives the primary language of a bilingual audio
	signal. Drivers must clear this flag when the current video
	standard is ``V4L2_STD_NTSC_M``.
    * - ``V4L2_TUNER_SUB_LANG2``
      - 0x0004
      - The tuner receives the secondary language of a bilingual audio
	signal (or a second audio program).
    * - ``V4L2_TUNER_SUB_SAP``
      - 0x0004
      - The tuner receives a Second Audio Program.

	.. note::

	   The ``V4L2_TUNER_SUB_LANG2`` and ``V4L2_TUNER_SUB_SAP``
	   flags are synonyms. The ``V4L2_TUNER_SUB_SAP`` flag applies
	   when the current video standard is ``V4L2_STD_NTSC_M``.
    * - ``V4L2_TUNER_SUB_RDS``
      - 0x0010
      - The tuner receives an RDS channel.



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _tuner-audmode:

.. flat-table:: Tuner Audio Modes
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_TUNER_MODE_MONO``
      - 0
      - Play mono audio. When the tuner receives a stereo signal this a
	down-mix of the left and right channel. When the tuner receives a
	bilingual or SAP signal this mode selects the primary language.
    * - ``V4L2_TUNER_MODE_STEREO``
      - 1
      - Play stereo audio. When the tuner receives bilingual audio it may
	play different languages on the left and right channel or the
	primary language is played on both channels.

	Playing different languages in this mode is deprecated. New
	drivers should do this only in ``MODE_LANG1_LANG2``.

	When the tuner receives no stereo signal or does not support
	stereo reception the driver shall fall back to ``MODE_MONO``.
    * - ``V4L2_TUNER_MODE_LANG1``
      - 3
      - Play the primary language, mono or stereo. Only
	``V4L2_TUNER_ANALOG_TV`` tuners support this mode.
    * - ``V4L2_TUNER_MODE_LANG2``
      - 2
      - Play the secondary language, mono. When the tuner receives no
	bilingual audio or SAP, or their reception is not supported the
	driver shall fall back to mono or stereo mode. Only
	``V4L2_TUNER_ANALOG_TV`` tuners support this mode.
    * - ``V4L2_TUNER_MODE_SAP``
      - 2
      - Play the Second Audio Program. When the tuner receives no
	bilingual audio or SAP, or their reception is not supported the
	driver shall fall back to mono or stereo mode. Only
	``V4L2_TUNER_ANALOG_TV`` tuners support this mode.

	.. note:: The ``V4L2_TUNER_MODE_LANG2`` and ``V4L2_TUNER_MODE_SAP``
	   are synonyms.
    * - ``V4L2_TUNER_MODE_LANG1_LANG2``
      - 4
      - Play the primary language on the left channel, the secondary
	language on the right channel. When the tuner receives no
	bilingual audio or SAP, it shall fall back to ``MODE_LANG1`` or
	``MODE_MONO``. Only ``V4L2_TUNER_ANALOG_TV`` tuners support this
	mode.

.. raw:: latex

    \scriptsize

.. tabularcolumns:: |p{1.5cm}|p{1.5cm}|p{2.9cm}|p{2.9cm}|p{2.9cm}|p{2.9cm}|

.. _tuner-matrix:

.. flat-table:: Tuner Audio Matrix
    :header-rows:  2
    :stub-columns: 0
    :widths: 7 7 14 14 14 14

    * -
      - :cspan:`4` Selected ``V4L2_TUNER_MODE_``
    * - Received ``V4L2_TUNER_SUB_``
      - ``MONO``
      - ``STEREO``
      - ``LANG1``
      - ``LANG2 = SAP``
      - ``LANG1_LANG2``\ [#f1]_
    * - ``MONO``
      - Mono
      - Mono/Mono
      - Mono
      - Mono
      - Mono/Mono
    * - ``MONO | SAP``
      - Mono
      - Mono/Mono
      - Mono
      - SAP
      - Mono/SAP (preferred) or Mono/Mono
    * - ``STEREO``
      - L+R
      - L/R
      - Stereo L/R (preferred) or Mono L+R
      - Stereo L/R (preferred) or Mono L+R
      - L/R (preferred) or L+R/L+R
    * - ``STEREO | SAP``
      - L+R
      - L/R
      - Stereo L/R (preferred) or Mono L+R
      - SAP
      - L+R/SAP (preferred) or L/R or L+R/L+R
    * - ``LANG1 | LANG2``
      - Language 1
      - Lang1/Lang2 (deprecated\ [#f2]_) or Lang1/Lang1
      - Language 1
      - Language 2
      - Lang1/Lang2 (preferred) or Lang1/Lang1

.. raw:: latex

    \normalsize

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :c:type:`v4l2_tuner` ``index`` is out of
    bounds.

.. [#f1]
   This mode has been added in Linux 2.6.17 and may not be supported by
   older drivers.

.. [#f2]
   Playback of both languages in ``MODE_STEREO`` is deprecated. In the
   future drivers should produce only the primary language in this mode.
   Applications should request ``MODE_LANG1_LANG2`` to record both
   languages or a stereo signal.
