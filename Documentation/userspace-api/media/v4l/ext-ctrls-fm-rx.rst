.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _fm-rx-controls:

*****************************
FM Receiver Control Reference
*****************************

The FM Receiver (FM_RX) class includes controls for common features of
FM Reception capable devices.


.. _fm-rx-control-id:

FM_RX Control IDs
=================

``V4L2_CID_FM_RX_CLASS (class)``
    The FM_RX class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.

``V4L2_CID_RDS_RECEPTION (boolean)``
    Enables/disables RDS reception by the radio tuner

``V4L2_CID_RDS_RX_PTY (integer)``
    Gets RDS Programme Type field. This encodes up to 31 pre-defined
    programme types.

``V4L2_CID_RDS_RX_PS_NAME (string)``
    Gets the Programme Service name (PS_NAME). It is intended for
    static display on a receiver. It is the primary aid to listeners in
    programme service identification and selection. In Annex E of
    :ref:`iec62106`, the RDS specification, there is a full
    description of the correct character encoding for Programme Service
    name strings. Also from RDS specification, PS is usually a single
    eight character text. However, it is also possible to find receivers
    which can scroll strings sized as 8 x N characters. So, this control
    must be configured with steps of 8 characters. The result is it must
    always contain a string with size multiple of 8.

``V4L2_CID_RDS_RX_RADIO_TEXT (string)``
    Gets the Radio Text info. It is a textual description of what is
    being broadcasted. RDS Radio Text can be applied when broadcaster
    wishes to transmit longer PS names, programme-related information or
    any other text. In these cases, RadioText can be used in addition to
    ``V4L2_CID_RDS_RX_PS_NAME``. The encoding for Radio Text strings is
    also fully described in Annex E of :ref:`iec62106`. The length of
    Radio Text strings depends on which RDS Block is being used to
    transmit it, either 32 (2A block) or 64 (2B block). However, it is
    also possible to find receivers which can scroll strings sized as 32
    x N or 64 x N characters. So, this control must be configured with
    steps of 32 or 64 characters. The result is it must always contain a
    string with size multiple of 32 or 64.

``V4L2_CID_RDS_RX_TRAFFIC_ANNOUNCEMENT (boolean)``
    If set, then a traffic announcement is in progress.

``V4L2_CID_RDS_RX_TRAFFIC_PROGRAM (boolean)``
    If set, then the tuned programme carries traffic announcements.

``V4L2_CID_RDS_RX_MUSIC_SPEECH (boolean)``
    If set, then this channel broadcasts music. If cleared, then it
    broadcasts speech. If the transmitter doesn't make this distinction,
    then it will be set.

``V4L2_CID_TUNE_DEEMPHASIS``
    (enum)

enum v4l2_deemphasis -
    Configures the de-emphasis value for reception. A de-emphasis filter
    is applied to the broadcast to accentuate the high audio
    frequencies. Depending on the region, a time constant of either 50
    or 75 useconds is used. The enum v4l2_deemphasis defines possible
    values for de-emphasis. Here they are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_DEEMPHASIS_DISABLED``
      - No de-emphasis is applied.
    * - ``V4L2_DEEMPHASIS_50_uS``
      - A de-emphasis of 50 uS is used.
    * - ``V4L2_DEEMPHASIS_75_uS``
      - A de-emphasis of 75 uS is used.
