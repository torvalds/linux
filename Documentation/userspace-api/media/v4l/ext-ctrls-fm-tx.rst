.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _fm-tx-controls:

********************************
FM Transmitter Control Reference
********************************

The FM Transmitter (FM_TX) class includes controls for common features
of FM transmissions capable devices. Currently this class includes
parameters for audio compression, pilot tone generation, audio deviation
limiter, RDS transmission and tuning power features.


.. _fm-tx-control-id:

FM_TX Control IDs
=================

``V4L2_CID_FM_TX_CLASS (class)``
    The FM_TX class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.

``V4L2_CID_RDS_TX_DEVIATION (integer)``
    Configures RDS signal frequency deviation level in Hz. The range and
    step are driver-specific.

``V4L2_CID_RDS_TX_PI (integer)``
    Sets the RDS Programme Identification field for transmission.

``V4L2_CID_RDS_TX_PTY (integer)``
    Sets the RDS Programme Type field for transmission. This encodes up
    to 31 pre-defined programme types.

``V4L2_CID_RDS_TX_PS_NAME (string)``
    Sets the Programme Service name (PS_NAME) for transmission. It is
    intended for static display on a receiver. It is the primary aid to
    listeners in programme service identification and selection. In
    Annex E of :ref:`iec62106`, the RDS specification, there is a full
    description of the correct character encoding for Programme Service
    name strings. Also from RDS specification, PS is usually a single
    eight character text. However, it is also possible to find receivers
    which can scroll strings sized as 8 x N characters. So, this control
    must be configured with steps of 8 characters. The result is it must
    always contain a string with size multiple of 8.

``V4L2_CID_RDS_TX_RADIO_TEXT (string)``
    Sets the Radio Text info for transmission. It is a textual
    description of what is being broadcasted. RDS Radio Text can be
    applied when broadcaster wishes to transmit longer PS names,
    programme-related information or any other text. In these cases,
    RadioText should be used in addition to ``V4L2_CID_RDS_TX_PS_NAME``.
    The encoding for Radio Text strings is also fully described in Annex
    E of :ref:`iec62106`. The length of Radio Text strings depends on
    which RDS Block is being used to transmit it, either 32 (2A block)
    or 64 (2B block). However, it is also possible to find receivers
    which can scroll strings sized as 32 x N or 64 x N characters. So,
    this control must be configured with steps of 32 or 64 characters.
    The result is it must always contain a string with size multiple of
    32 or 64.

``V4L2_CID_RDS_TX_MONO_STEREO (boolean)``
    Sets the Mono/Stereo bit of the Decoder Identification code. If set,
    then the audio was recorded as stereo.

``V4L2_CID_RDS_TX_ARTIFICIAL_HEAD (boolean)``
    Sets the
    `Artificial Head <http://en.wikipedia.org/wiki/Artificial_head>`__
    bit of the Decoder Identification code. If set, then the audio was
    recorded using an artificial head.

``V4L2_CID_RDS_TX_COMPRESSED (boolean)``
    Sets the Compressed bit of the Decoder Identification code. If set,
    then the audio is compressed.

``V4L2_CID_RDS_TX_DYNAMIC_PTY (boolean)``
    Sets the Dynamic PTY bit of the Decoder Identification code. If set,
    then the PTY code is dynamically switched.

``V4L2_CID_RDS_TX_TRAFFIC_ANNOUNCEMENT (boolean)``
    If set, then a traffic announcement is in progress.

``V4L2_CID_RDS_TX_TRAFFIC_PROGRAM (boolean)``
    If set, then the tuned programme carries traffic announcements.

``V4L2_CID_RDS_TX_MUSIC_SPEECH (boolean)``
    If set, then this channel broadcasts music. If cleared, then it
    broadcasts speech. If the transmitter doesn't make this distinction,
    then it should be set.

``V4L2_CID_RDS_TX_ALT_FREQS_ENABLE (boolean)``
    If set, then transmit alternate frequencies.

``V4L2_CID_RDS_TX_ALT_FREQS (__u32 array)``
    The alternate frequencies in kHz units. The RDS standard allows for
    up to 25 frequencies to be defined. Drivers may support fewer
    frequencies so check the array size.

``V4L2_CID_AUDIO_LIMITER_ENABLED (boolean)``
    Enables or disables the audio deviation limiter feature. The limiter
    is useful when trying to maximize the audio volume, minimize
    receiver-generated distortion and prevent overmodulation.

``V4L2_CID_AUDIO_LIMITER_RELEASE_TIME (integer)``
    Sets the audio deviation limiter feature release time. Unit is in
    useconds. Step and range are driver-specific.

``V4L2_CID_AUDIO_LIMITER_DEVIATION (integer)``
    Configures audio frequency deviation level in Hz. The range and step
    are driver-specific.

``V4L2_CID_AUDIO_COMPRESSION_ENABLED (boolean)``
    Enables or disables the audio compression feature. This feature
    amplifies signals below the threshold by a fixed gain and compresses
    audio signals above the threshold by the ratio of Threshold/(Gain +
    Threshold).

``V4L2_CID_AUDIO_COMPRESSION_GAIN (integer)``
    Sets the gain for audio compression feature. It is a dB value. The
    range and step are driver-specific.

``V4L2_CID_AUDIO_COMPRESSION_THRESHOLD (integer)``
    Sets the threshold level for audio compression freature. It is a dB
    value. The range and step are driver-specific.

``V4L2_CID_AUDIO_COMPRESSION_ATTACK_TIME (integer)``
    Sets the attack time for audio compression feature. It is a useconds
    value. The range and step are driver-specific.

``V4L2_CID_AUDIO_COMPRESSION_RELEASE_TIME (integer)``
    Sets the release time for audio compression feature. It is a
    useconds value. The range and step are driver-specific.

``V4L2_CID_PILOT_TONE_ENABLED (boolean)``
    Enables or disables the pilot tone generation feature.

``V4L2_CID_PILOT_TONE_DEVIATION (integer)``
    Configures pilot tone frequency deviation level. Unit is in Hz. The
    range and step are driver-specific.

``V4L2_CID_PILOT_TONE_FREQUENCY (integer)``
    Configures pilot tone frequency value. Unit is in Hz. The range and
    step are driver-specific.

``V4L2_CID_TUNE_PREEMPHASIS``
    (enum)

enum v4l2_preemphasis -
    Configures the pre-emphasis value for broadcasting. A pre-emphasis
    filter is applied to the broadcast to accentuate the high audio
    frequencies. Depending on the region, a time constant of either 50
    or 75 useconds is used. The enum v4l2_preemphasis defines possible
    values for pre-emphasis. Here they are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_PREEMPHASIS_DISABLED``
      - No pre-emphasis is applied.
    * - ``V4L2_PREEMPHASIS_50_uS``
      - A pre-emphasis of 50 uS is used.
    * - ``V4L2_PREEMPHASIS_75_uS``
      - A pre-emphasis of 75 uS is used.



``V4L2_CID_TUNE_POWER_LEVEL (integer)``
    Sets the output power level for signal transmission. Unit is in
    dBuV. Range and step are driver-specific.

``V4L2_CID_TUNE_ANTENNA_CAPACITOR (integer)``
    This selects the value of antenna tuning capacitor manually or
    automatically if set to zero. Unit, range and step are
    driver-specific.

For more details about RDS specification, refer to :ref:`iec62106`
document, from CENELEC.
