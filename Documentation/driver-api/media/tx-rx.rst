.. SPDX-License-Identifier: GPL-2.0

.. _transmitter-receiver:

Pixel data transmitter and receiver drivers
===========================================

V4L2 supports various devices that transmit and receiver pixel data. Examples of
these devices include a camera sensor, a TV tuner and a parallel or a CSI-2
receiver in an SoC.

Bus types
---------

The following busses are the most common. This section discusses these two only.

MIPI CSI-2
^^^^^^^^^^

CSI-2 is a data bus intended for transferring images from cameras to
the host SoC. It is defined by the `MIPI alliance`_.

.. _`MIPI alliance`: https://www.mipi.org/

Parallel
^^^^^^^^

`BT.601`_ and `BT.656`_ are the most common parallel busses.

.. _`BT.601`: https://en.wikipedia.org/wiki/Rec._601
.. _`BT.656`: https://en.wikipedia.org/wiki/ITU-R_BT.656

Transmitter drivers
-------------------

Transmitter drivers generally need to provide the receiver drivers with the
configuration of the transmitter. What is required depends on the type of the
bus. These are common for both busses.

Media bus pixel code
^^^^^^^^^^^^^^^^^^^^

See :ref:`v4l2-mbus-pixelcode`.

Link frequency
^^^^^^^^^^^^^^

The :ref:`V4L2_CID_LINK_FREQ <v4l2-cid-link-freq>` control is used to tell the
receiver the frequency of the bus (i.e. it is not the same as the symbol rate).

``.s_stream()`` callback
^^^^^^^^^^^^^^^^^^^^^^^^

The struct struct v4l2_subdev_video_ops->s_stream() callback is used by the
receiver driver to control the transmitter driver's streaming state.


CSI-2 transmitter drivers
-------------------------

Pixel rate
^^^^^^^^^^

The pixel rate on the bus is calculated as follows::

	pixel_rate = link_freq * 2 * nr_of_lanes * 16 / k / bits_per_sample

where

.. list-table:: variables in pixel rate calculation
   :header-rows: 1

   * - variable or constant
     - description
   * - link_freq
     - The value of the ``V4L2_CID_LINK_FREQ`` integer64 menu item.
   * - nr_of_lanes
     - Number of data lanes used on the CSI-2 link. This can
       be obtained from the OF endpoint configuration.
   * - 2
     - Data is transferred on both rising and falling edge of the signal.
   * - bits_per_sample
     - Number of bits per sample.
   * - k
     - 16 for D-PHY and 7 for C-PHY

.. note::

	The pixel rate calculated this way is **not** the same thing as the
	pixel rate on the camera sensor's pixel array which is indicated by the
	:ref:`V4L2_CID_PIXEL_RATE <v4l2-cid-pixel-rate>` control.

LP-11 and LP-111 modes
^^^^^^^^^^^^^^^^^^^^^^

The transmitter drivers must, if possible, configure the CSI-2 transmitter to
*LP-11 or LP-111 mode* whenever the transmitter is powered on but not active,
and maintain *LP-11 or LP-111 mode* until stream on. Only at stream on should
the transmitter activate the clock on the clock lane and transition to *HS
mode*.

Some transmitters do this automatically but some have to be explicitly
programmed to do so, and some are unable to do so altogether due to
hardware constraints.

The receiver thus need to be configured to expect LP-11 or LP-111 mode from the
transmitter before the transmitter driver's ``.s_stream()`` op is called.

Stopping the transmitter
^^^^^^^^^^^^^^^^^^^^^^^^

A transmitter stops sending the stream of images as a result of
calling the ``.s_stream()`` callback. Some transmitters may stop the
stream at a frame boundary whereas others stop immediately,
effectively leaving the current frame unfinished. The receiver driver
should not make assumptions either way, but function properly in both
cases.
