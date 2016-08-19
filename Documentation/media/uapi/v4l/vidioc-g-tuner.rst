.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_G_TUNER:

************************************
ioctl VIDIOC_G_TUNER, VIDIOC_S_TUNER
************************************

Name
====

VIDIOC_G_TUNER - VIDIOC_S_TUNER - Get or set tuner attributes


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_tuner *argp )

.. cpp:function:: int ioctl( int fd, int request, const struct v4l2_tuner *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_G_TUNER, VIDIOC_S_TUNER

``argp``


Description
===========

To query the attributes of a tuner applications initialize the ``index``
field and zero out the ``reserved`` array of a struct
:ref:`v4l2_tuner <v4l2-tuner>` and call the ``VIDIOC_G_TUNER`` ioctl
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


.. _v4l2-tuner:

.. flat-table:: struct v4l2_tuner
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  __u32

       -  ``index``

       -  :cspan:`1` Identifies the tuner, set by the application.

    -  .. row 2

       -  __u8

       -  ``name``\ [32]

       -  :cspan:`1`

	  Name of the tuner, a NUL-terminated ASCII string. This information
	  is intended for the user.

    -  .. row 3

       -  __u32

       -  ``type``

       -  :cspan:`1` Type of the tuner, see :ref:`v4l2-tuner-type`.

    -  .. row 4

       -  __u32

       -  ``capability``

       -  :cspan:`1`

	  Tuner capability flags, see :ref:`tuner-capability`. Audio flags
	  indicate the ability to decode audio subprograms. They will *not*
	  change, for example with the current video standard.

	  When the structure refers to a radio tuner the
	  ``V4L2_TUNER_CAP_LANG1``, ``V4L2_TUNER_CAP_LANG2`` and
	  ``V4L2_TUNER_CAP_NORM`` flags can't be used.

	  If multiple frequency bands are supported, then ``capability`` is
	  the union of all ``capability`` fields of each struct
	  :ref:`v4l2_frequency_band <v4l2-frequency-band>`.

    -  .. row 5

       -  __u32

       -  ``rangelow``

       -  :cspan:`1` The lowest tunable frequency in units of 62.5 kHz, or
	  if the ``capability`` flag ``V4L2_TUNER_CAP_LOW`` is set, in units
	  of 62.5 Hz, or if the ``capability`` flag ``V4L2_TUNER_CAP_1HZ``
	  is set, in units of 1 Hz. If multiple frequency bands are
	  supported, then ``rangelow`` is the lowest frequency of all the
	  frequency bands.

    -  .. row 6

       -  __u32

       -  ``rangehigh``

       -  :cspan:`1` The highest tunable frequency in units of 62.5 kHz,
	  or if the ``capability`` flag ``V4L2_TUNER_CAP_LOW`` is set, in
	  units of 62.5 Hz, or if the ``capability`` flag
	  ``V4L2_TUNER_CAP_1HZ`` is set, in units of 1 Hz. If multiple
	  frequency bands are supported, then ``rangehigh`` is the highest
	  frequency of all the frequency bands.

    -  .. row 7

       -  __u32

       -  ``rxsubchans``

       -  :cspan:`1`

	  Some tuners or audio decoders can determine the received audio
	  subprograms by analyzing audio carriers, pilot tones or other
	  indicators. To pass this information drivers set flags defined in
	  :ref:`tuner-rxsubchans` in this field. For example:

    -  .. row 8

       -
       -
       -  ``V4L2_TUNER_SUB_MONO``

       -  receiving mono audio

    -  .. row 9

       -
       -
       -  ``STEREO | SAP``

       -  receiving stereo audio and a secondary audio program

    -  .. row 10

       -
       -
       -  ``MONO | STEREO``

       -  receiving mono or stereo audio, the hardware cannot distinguish

    -  .. row 11

       -
       -
       -  ``LANG1 | LANG2``

       -  receiving bilingual audio

    -  .. row 12

       -
       -
       -  ``MONO | STEREO | LANG1 | LANG2``

       -  receiving mono, stereo or bilingual audio

    -  .. row 13

       -
       -
       -  :cspan:`1`

	  When the ``V4L2_TUNER_CAP_STEREO``, ``_LANG1``, ``_LANG2`` or
	  ``_SAP`` flag is cleared in the ``capability`` field, the
	  corresponding ``V4L2_TUNER_SUB_`` flag must not be set here.

	  This field is valid only if this is the tuner of the current video
	  input, or when the structure refers to a radio tuner.

    -  .. row 14

       -  __u32

       -  ``audmode``

       -  :cspan:`1`

	  The selected audio mode, see :ref:`tuner-audmode` for valid
	  values. The audio mode does not affect audio subprogram detection,
	  and like a :ref:`control` it does not automatically
	  change unless the requested mode is invalid or unsupported. See
	  :ref:`tuner-matrix` for possible results when the selected and
	  received audio programs do not match.

	  Currently this is the only field of struct
	  :ref:`struct v4l2_tuner <v4l2-tuner>` applications can change.

    -  .. row 15

       -  __u32

       -  ``signal``

       -  :cspan:`1` The signal strength if known, ranging from 0 to
	  65535. Higher values indicate a better signal.

    -  .. row 16

       -  __s32

       -  ``afc``

       -  :cspan:`1` Automatic frequency control: When the ``afc`` value
	  is negative, the frequency is too low, when positive too high.

    -  .. row 17

       -  __u32

       -  ``reserved``\ [4]

       -  :cspan:`1` Reserved for future extensions. Drivers and
	  applications must set the array to zero.



.. _v4l2-tuner-type:

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: enum v4l2_tuner_type
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 6


    -  .. row 1

       -  ``V4L2_TUNER_RADIO``

       -  1

       - Tuner supports radio

    -  .. row 2

       -  ``V4L2_TUNER_ANALOG_TV``

       -  2

       - Tuner supports analog TV

    -  .. row 3

       -  ``V4L2_TUNER_SDR``

       -  4

       - Tuner controls the A/D and/or D/A block of a
	 Sofware Digital Radio (SDR)

    -  .. row 4

       -  ``V4L2_TUNER_RF``

       -  5

       - Tuner controls the RF part of a Sofware Digital Radio (SDR)


.. _tuner-capability:

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: Tuner and Modulator Capability Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_TUNER_CAP_LOW``

       -  0x0001

       -  When set, tuning frequencies are expressed in units of 62.5 Hz
	  instead of 62.5 kHz.

    -  .. row 2

       -  ``V4L2_TUNER_CAP_NORM``

       -  0x0002

       -  This is a multi-standard tuner; the video standard can or must be
	  switched. (B/G PAL tuners for example are typically not considered
	  multi-standard because the video standard is automatically
	  determined from the frequency band.) The set of supported video
	  standards is available from the struct
	  :ref:`v4l2_input <v4l2-input>` pointing to this tuner, see the
	  description of ioctl :ref:`VIDIOC_ENUMINPUT`
	  for details. Only ``V4L2_TUNER_ANALOG_TV`` tuners can have this
	  capability.

    -  .. row 3

       -  ``V4L2_TUNER_CAP_HWSEEK_BOUNDED``

       -  0x0004

       -  If set, then this tuner supports the hardware seek functionality
	  where the seek stops when it reaches the end of the frequency
	  range.

    -  .. row 4

       -  ``V4L2_TUNER_CAP_HWSEEK_WRAP``

       -  0x0008

       -  If set, then this tuner supports the hardware seek functionality
	  where the seek wraps around when it reaches the end of the
	  frequency range.

    -  .. row 5

       -  ``V4L2_TUNER_CAP_STEREO``

       -  0x0010

       -  Stereo audio reception is supported.

    -  .. row 6

       -  ``V4L2_TUNER_CAP_LANG1``

       -  0x0040

       -  Reception of the primary language of a bilingual audio program is
	  supported. Bilingual audio is a feature of two-channel systems,
	  transmitting the primary language monaural on the main audio
	  carrier and a secondary language monaural on a second carrier.
	  Only ``V4L2_TUNER_ANALOG_TV`` tuners can have this capability.

    -  .. row 7

       -  ``V4L2_TUNER_CAP_LANG2``

       -  0x0020

       -  Reception of the secondary language of a bilingual audio program
	  is supported. Only ``V4L2_TUNER_ANALOG_TV`` tuners can have this
	  capability.

    -  .. row 8

       -  ``V4L2_TUNER_CAP_SAP``

       -  0x0020

       -  Reception of a secondary audio program is supported. This is a
	  feature of the BTSC system which accompanies the NTSC video
	  standard. Two audio carriers are available for mono or stereo
	  transmissions of a primary language, and an independent third
	  carrier for a monaural secondary language. Only
	  ``V4L2_TUNER_ANALOG_TV`` tuners can have this capability.

	  .. note::

	     The ``V4L2_TUNER_CAP_LANG2`` and ``V4L2_TUNER_CAP_SAP``
	     flags are synonyms. ``V4L2_TUNER_CAP_SAP`` applies when the tuner
	     supports the ``V4L2_STD_NTSC_M`` video standard.

    -  .. row 9

       -  ``V4L2_TUNER_CAP_RDS``

       -  0x0080

       -  RDS capture is supported. This capability is only valid for radio
	  tuners.

    -  .. row 10

       -  ``V4L2_TUNER_CAP_RDS_BLOCK_IO``

       -  0x0100

       -  The RDS data is passed as unparsed RDS blocks.

    -  .. row 11

       -  ``V4L2_TUNER_CAP_RDS_CONTROLS``

       -  0x0200

       -  The RDS data is parsed by the hardware and set via controls.

    -  .. row 12

       -  ``V4L2_TUNER_CAP_FREQ_BANDS``

       -  0x0400

       -  The :ref:`VIDIOC_ENUM_FREQ_BANDS`
	  ioctl can be used to enumerate the available frequency bands.

    -  .. row 13

       -  ``V4L2_TUNER_CAP_HWSEEK_PROG_LIM``

       -  0x0800

       -  The range to search when using the hardware seek functionality is
	  programmable, see
	  :ref:`VIDIOC_S_HW_FREQ_SEEK` for
	  details.

    -  .. row 14

       -  ``V4L2_TUNER_CAP_1HZ``

       -  0x1000

       -  When set, tuning frequencies are expressed in units of 1 Hz
	  instead of 62.5 kHz.



.. _tuner-rxsubchans:

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: Tuner Audio Reception Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_TUNER_SUB_MONO``

       -  0x0001

       -  The tuner receives a mono audio signal.

    -  .. row 2

       -  ``V4L2_TUNER_SUB_STEREO``

       -  0x0002

       -  The tuner receives a stereo audio signal.

    -  .. row 3

       -  ``V4L2_TUNER_SUB_LANG1``

       -  0x0008

       -  The tuner receives the primary language of a bilingual audio
	  signal. Drivers must clear this flag when the current video
	  standard is ``V4L2_STD_NTSC_M``.

    -  .. row 4

       -  ``V4L2_TUNER_SUB_LANG2``

       -  0x0004

       -  The tuner receives the secondary language of a bilingual audio
	  signal (or a second audio program).

    -  .. row 5

       -  ``V4L2_TUNER_SUB_SAP``

       -  0x0004

       -  The tuner receives a Second Audio Program.

	  .. note::

	     The ``V4L2_TUNER_SUB_LANG2`` and ``V4L2_TUNER_SUB_SAP``
	     flags are synonyms. The ``V4L2_TUNER_SUB_SAP`` flag applies
	     when the current video standard is ``V4L2_STD_NTSC_M``.

    -  .. row 6

       -  ``V4L2_TUNER_SUB_RDS``

       -  0x0010

       -  The tuner receives an RDS channel.



.. _tuner-audmode:

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: Tuner Audio Modes
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_TUNER_MODE_MONO``

       -  0

       -  Play mono audio. When the tuner receives a stereo signal this a
	  down-mix of the left and right channel. When the tuner receives a
	  bilingual or SAP signal this mode selects the primary language.

    -  .. row 2

       -  ``V4L2_TUNER_MODE_STEREO``

       -  1

       -  Play stereo audio. When the tuner receives bilingual audio it may
	  play different languages on the left and right channel or the
	  primary language is played on both channels.

	  Playing different languages in this mode is deprecated. New
	  drivers should do this only in ``MODE_LANG1_LANG2``.

	  When the tuner receives no stereo signal or does not support
	  stereo reception the driver shall fall back to ``MODE_MONO``.

    -  .. row 3

       -  ``V4L2_TUNER_MODE_LANG1``

       -  3

       -  Play the primary language, mono or stereo. Only
	  ``V4L2_TUNER_ANALOG_TV`` tuners support this mode.

    -  .. row 4

       -  ``V4L2_TUNER_MODE_LANG2``

       -  2

       -  Play the secondary language, mono. When the tuner receives no
	  bilingual audio or SAP, or their reception is not supported the
	  driver shall fall back to mono or stereo mode. Only
	  ``V4L2_TUNER_ANALOG_TV`` tuners support this mode.

    -  .. row 5

       -  ``V4L2_TUNER_MODE_SAP``

       -  2

       -  Play the Second Audio Program. When the tuner receives no
	  bilingual audio or SAP, or their reception is not supported the
	  driver shall fall back to mono or stereo mode. Only
	  ``V4L2_TUNER_ANALOG_TV`` tuners support this mode.

	  .. note:: The ``V4L2_TUNER_MODE_LANG2`` and ``V4L2_TUNER_MODE_SAP``
	     are synonyms.

    -  .. row 6

       -  ``V4L2_TUNER_MODE_LANG1_LANG2``

       -  4

       -  Play the primary language on the left channel, the secondary
	  language on the right channel. When the tuner receives no
	  bilingual audio or SAP, it shall fall back to ``MODE_LANG1`` or
	  ``MODE_MONO``. Only ``V4L2_TUNER_ANALOG_TV`` tuners support this
	  mode.



.. _tuner-matrix:

.. flat-table:: Tuner Audio Matrix
    :header-rows:  2
    :stub-columns: 0


    -  .. row 1

       -
       -  :cspan:`5` Selected ``V4L2_TUNER_MODE_``

    -  .. row 2

       -  Received ``V4L2_TUNER_SUB_``

       -  ``MONO``

       -  ``STEREO``

       -  ``LANG1``

       -  ``LANG2 = SAP``

       -  ``LANG1_LANG2``\  [#f1]_

    -  .. row 3

       -  ``MONO``

       -  Mono

       -  Mono/Mono

       -  Mono

       -  Mono

       -  Mono/Mono

    -  .. row 4

       -  ``MONO | SAP``

       -  Mono

       -  Mono/Mono

       -  Mono

       -  SAP

       -  Mono/SAP (preferred) or Mono/Mono

    -  .. row 5

       -  ``STEREO``

       -  L+R

       -  L/R

       -  Stereo L/R (preferred) or Mono L+R

       -  Stereo L/R (preferred) or Mono L+R

       -  L/R (preferred) or L+R/L+R

    -  .. row 6

       -  ``STEREO | SAP``

       -  L+R

       -  L/R

       -  Stereo L/R (preferred) or Mono L+R

       -  SAP

       -  L+R/SAP (preferred) or L/R or L+R/L+R

    -  .. row 7

       -  ``LANG1 | LANG2``

       -  Language 1

       -  Lang1/Lang2 (deprecated [#f2]_) or Lang1/Lang1

       -  Language 1

       -  Language 2

       -  Lang1/Lang2 (preferred) or Lang1/Lang1


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :ref:`v4l2_tuner <v4l2-tuner>` ``index`` is out of
    bounds.

.. [#f1]
   This mode has been added in Linux 2.6.17 and may not be supported by
   older drivers.

.. [#f2]
   Playback of both languages in ``MODE_STEREO`` is deprecated. In the
   future drivers should produce only the primary language in this mode.
   Applications should request ``MODE_LANG1_LANG2`` to record both
   languages or a stereo signal.
