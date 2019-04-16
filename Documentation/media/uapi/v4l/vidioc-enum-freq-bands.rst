.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDIOC_ENUM_FREQ_BANDS:

****************************
ioctl VIDIOC_ENUM_FREQ_BANDS
****************************

Name
====

VIDIOC_ENUM_FREQ_BANDS - Enumerate supported frequency bands


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_ENUM_FREQ_BANDS, struct v4l2_frequency_band *argp )
    :name: VIDIOC_ENUM_FREQ_BANDS


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_frequency_band`.


Description
===========

Enumerates the frequency bands that a tuner or modulator supports. To do
this applications initialize the ``tuner``, ``type`` and ``index``
fields, and zero out the ``reserved`` array of a struct
:c:type:`v4l2_frequency_band` and call the
:ref:`VIDIOC_ENUM_FREQ_BANDS` ioctl with a pointer to this structure.

This ioctl is supported if the ``V4L2_TUNER_CAP_FREQ_BANDS`` capability
of the corresponding tuner/modulator is set.


.. tabularcolumns:: |p{2.9cm}|p{2.9cm}|p{5.8cm}|p{2.9cm}|p{3.0cm}|

.. c:type:: v4l2_frequency_band

.. flat-table:: struct v4l2_frequency_band
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2 1 1

    * - __u32
      - ``tuner``
      - The tuner or modulator index number. This is the same value as in
	the struct :c:type:`v4l2_input` ``tuner`` field and
	the struct :c:type:`v4l2_tuner` ``index`` field, or
	the struct :c:type:`v4l2_output` ``modulator`` field
	and the struct :c:type:`v4l2_modulator` ``index``
	field.
    * - __u32
      - ``type``
      - The tuner type. This is the same value as in the struct
	:c:type:`v4l2_tuner` ``type`` field. The type must be
	set to ``V4L2_TUNER_RADIO`` for ``/dev/radioX`` device nodes, and
	to ``V4L2_TUNER_ANALOG_TV`` for all others. Set this field to
	``V4L2_TUNER_RADIO`` for modulators (currently only radio
	modulators are supported). See :c:type:`v4l2_tuner_type`
    * - __u32
      - ``index``
      - Identifies the frequency band, set by the application.
    * - __u32
      - ``capability``
      - :cspan:`2` The tuner/modulator capability flags for this
	frequency band, see :ref:`tuner-capability`. The
	``V4L2_TUNER_CAP_LOW`` or ``V4L2_TUNER_CAP_1HZ`` capability must
	be the same for all frequency bands of the selected
	tuner/modulator. So either all bands have that capability set, or
	none of them have that capability.
    * - __u32
      - ``rangelow``
      - :cspan:`2` The lowest tunable frequency in units of 62.5 kHz, or
	if the ``capability`` flag ``V4L2_TUNER_CAP_LOW`` is set, in units
	of 62.5 Hz, for this frequency band. A 1 Hz unit is used when the
	``capability`` flag ``V4L2_TUNER_CAP_1HZ`` is set.
    * - __u32
      - ``rangehigh``
      - :cspan:`2` The highest tunable frequency in units of 62.5 kHz,
	or if the ``capability`` flag ``V4L2_TUNER_CAP_LOW`` is set, in
	units of 62.5 Hz, for this frequency band. A 1 Hz unit is used
	when the ``capability`` flag ``V4L2_TUNER_CAP_1HZ`` is set.
    * - __u32
      - ``modulation``
      - :cspan:`2` The supported modulation systems of this frequency
	band. See :ref:`band-modulation`.

	.. note::

	   Currently only one modulation system per frequency band
	   is supported. More work will need to be done if multiple
	   modulation systems are possible. Contact the linux-media
	   mailing list
	   (`https://linuxtv.org/lists.php <https://linuxtv.org/lists.php>`__)
	   if you need such functionality.
    * - __u32
      - ``reserved``\ [9]
      - Reserved for future extensions.

	Applications and drivers must set the array to zero.



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _band-modulation:

.. flat-table:: Band Modulation Systems
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_BAND_MODULATION_VSB``
      - 0x02
      - Vestigial Sideband modulation, used for analog TV.
    * - ``V4L2_BAND_MODULATION_FM``
      - 0x04
      - Frequency Modulation, commonly used for analog radio.
    * - ``V4L2_BAND_MODULATION_AM``
      - 0x08
      - Amplitude Modulation, commonly used for analog radio.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The ``tuner`` or ``index`` is out of bounds or the ``type`` field is
    wrong.
