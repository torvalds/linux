.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _dv-controls:

*******************************
Digital Video Control Reference
*******************************

The Digital Video control class is intended to control receivers and
transmitters for `VGA <http://en.wikipedia.org/wiki/Vga>`__,
`DVI <http://en.wikipedia.org/wiki/Digital_Visual_Interface>`__
(Digital Visual Interface), HDMI (:ref:`hdmi`) and DisplayPort
(:ref:`dp`). These controls are generally expected to be private to
the receiver or transmitter subdevice that implements them, so they are
only exposed on the ``/dev/v4l-subdev*`` device node.

.. note::

   Note that these devices can have multiple input or output pads which are
   hooked up to e.g. HDMI connectors. Even though the subdevice will
   receive or transmit video from/to only one of those pads, the other pads
   can still be active when it comes to EDID (Extended Display
   Identification Data, :ref:`vesaedid`) and HDCP (High-bandwidth Digital
   Content Protection System, :ref:`hdcp`) processing, allowing the
   device to do the fairly slow EDID/HDCP handling in advance. This allows
   for quick switching between connectors.

These pads appear in several of the controls in this section as
bitmasks, one bit for each pad. Bit 0 corresponds to pad 0, bit 1 to pad
1, etc. The maximum value of the control is the set of valid pads.


.. _dv-control-id:

Digital Video Control IDs
=========================

``V4L2_CID_DV_CLASS (class)``
    The Digital Video class descriptor.

``V4L2_CID_DV_TX_HOTPLUG (bitmask)``
    Many connectors have a hotplug pin which is high if EDID information
    is available from the source. This control shows the state of the
    hotplug pin as seen by the transmitter. Each bit corresponds to an
    output pad on the transmitter. If an output pad does not have an
    associated hotplug pin, then the bit for that pad will be 0. This
    read-only control is applicable to DVI-D, HDMI and DisplayPort
    connectors.

``V4L2_CID_DV_TX_RXSENSE (bitmask)``
    Rx Sense is the detection of pull-ups on the TMDS clock lines. This
    normally means that the sink has left/entered standby (i.e. the
    transmitter can sense that the receiver is ready to receive video).
    Each bit corresponds to an output pad on the transmitter. If an
    output pad does not have an associated Rx Sense, then the bit for
    that pad will be 0. This read-only control is applicable to DVI-D
    and HDMI devices.

``V4L2_CID_DV_TX_EDID_PRESENT (bitmask)``
    When the transmitter sees the hotplug signal from the receiver it
    will attempt to read the EDID. If set, then the transmitter has read
    at least the first block (= 128 bytes). Each bit corresponds to an
    output pad on the transmitter. If an output pad does not support
    EDIDs, then the bit for that pad will be 0. This read-only control
    is applicable to VGA, DVI-A/D, HDMI and DisplayPort connectors.

``V4L2_CID_DV_TX_MODE``
    (enum)

enum v4l2_dv_tx_mode -
    HDMI transmitters can transmit in DVI-D mode (just video) or in HDMI
    mode (video + audio + auxiliary data). This control selects which
    mode to use: V4L2_DV_TX_MODE_DVI_D or V4L2_DV_TX_MODE_HDMI.
    This control is applicable to HDMI connectors.

``V4L2_CID_DV_TX_RGB_RANGE``
    (enum)

enum v4l2_dv_rgb_range -
    Select the quantization range for RGB output. V4L2_DV_RANGE_AUTO
    follows the RGB quantization range specified in the standard for the
    video interface (ie. :ref:`cea861` for HDMI).
    V4L2_DV_RANGE_LIMITED and V4L2_DV_RANGE_FULL override the
    standard to be compatible with sinks that have not implemented the
    standard correctly (unfortunately quite common for HDMI and DVI-D).
    Full range allows all possible values to be used whereas limited
    range sets the range to (16 << (N-8)) - (235 << (N-8)) where N is
    the number of bits per component. This control is applicable to VGA,
    DVI-A/D, HDMI and DisplayPort connectors.

``V4L2_CID_DV_TX_IT_CONTENT_TYPE``
    (enum)

enum v4l2_dv_it_content_type -
    Configures the IT Content Type of the transmitted video. This
    information is sent over HDMI and DisplayPort connectors as part of
    the AVI InfoFrame. The term 'IT Content' is used for content that
    originates from a computer as opposed to content from a TV broadcast
    or an analog source. The enum v4l2_dv_it_content_type defines
    the possible content types:

.. tabularcolumns:: |p{7.0cm}|p{10.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_DV_IT_CONTENT_TYPE_GRAPHICS``
      - Graphics content. Pixel data should be passed unfiltered and
	without analog reconstruction.
    * - ``V4L2_DV_IT_CONTENT_TYPE_PHOTO``
      - Photo content. The content is derived from digital still pictures.
	The content should be passed through with minimal scaling and
	picture enhancements.
    * - ``V4L2_DV_IT_CONTENT_TYPE_CINEMA``
      - Cinema content.
    * - ``V4L2_DV_IT_CONTENT_TYPE_GAME``
      - Game content. Audio and video latency should be minimized.
    * - ``V4L2_DV_IT_CONTENT_TYPE_NO_ITC``
      - No IT Content information is available and the ITC bit in the AVI
	InfoFrame is set to 0.



``V4L2_CID_DV_RX_POWER_PRESENT (bitmask)``
    Detects whether the receiver receives power from the source (e.g.
    HDMI carries 5V on one of the pins). This is often used to power an
    eeprom which contains EDID information, such that the source can
    read the EDID even if the sink is in standby/power off. Each bit
    corresponds to an input pad on the receiver. If an input pad
    cannot detect whether power is present, then the bit for that pad
    will be 0. This read-only control is applicable to DVI-D, HDMI and
    DisplayPort connectors.

``V4L2_CID_DV_RX_RGB_RANGE``
    (enum)

enum v4l2_dv_rgb_range -
    Select the quantization range for RGB input. V4L2_DV_RANGE_AUTO
    follows the RGB quantization range specified in the standard for the
    video interface (ie. :ref:`cea861` for HDMI).
    V4L2_DV_RANGE_LIMITED and V4L2_DV_RANGE_FULL override the
    standard to be compatible with sources that have not implemented the
    standard correctly (unfortunately quite common for HDMI and DVI-D).
    Full range allows all possible values to be used whereas limited
    range sets the range to (16 << (N-8)) - (235 << (N-8)) where N is
    the number of bits per component. This control is applicable to VGA,
    DVI-A/D, HDMI and DisplayPort connectors.

``V4L2_CID_DV_RX_IT_CONTENT_TYPE``
    (enum)

enum v4l2_dv_it_content_type -
    Reads the IT Content Type of the received video. This information is
    sent over HDMI and DisplayPort connectors as part of the AVI
    InfoFrame. The term 'IT Content' is used for content that originates
    from a computer as opposed to content from a TV broadcast or an
    analog source. See ``V4L2_CID_DV_TX_IT_CONTENT_TYPE`` for the
    available content types.
