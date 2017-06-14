MIPI CSI-2
==========

CSI-2 is a data bus intended for transferring images from cameras to
the host SoC. It is defined by the `MIPI alliance`_.

.. _`MIPI alliance`: http://www.mipi.org/

Transmitter drivers
-------------------

CSI-2 transmitter, such as a sensor or a TV tuner, drivers need to
provide the CSI-2 receiver with information on the CSI-2 bus
configuration. These include the V4L2_CID_LINK_FREQ and
V4L2_CID_PIXEL_RATE controls and
(:c:type:`v4l2_subdev_video_ops`->s_stream() callback). These
interface elements must be present on the sub-device represents the
CSI-2 transmitter.

The V4L2_CID_LINK_FREQ control is used to tell the receiver driver the
frequency (and not the symbol rate) of the link. The
V4L2_CID_PIXEL_RATE is may be used by the receiver to obtain the pixel
rate the transmitter uses. The
:c:type:`v4l2_subdev_video_ops`->s_stream() callback provides an
ability to start and stop the stream.

The value of the V4L2_CID_PIXEL_RATE is calculated as follows::

	pixel_rate = link_freq * 2 * nr_of_lanes / bits_per_sample

where

.. list-table:: variables in pixel rate calculation
   :header-rows: 1

   * - variable or constant
     - description
   * - link_freq
     - The value of the V4L2_CID_LINK_FREQ integer64 menu item.
   * - nr_of_lanes
     - Number of data lanes used on the CSI-2 link. This can
       be obtained from the OF endpoint configuration.
   * - 2
     - Two bits are transferred per clock cycle per lane.
   * - bits_per_sample
     - Number of bits per sample.

The transmitter drivers must, if possible, configure the CSI-2
transmitter to *LP-11 mode* whenever the transmitter is powered on but
not active. Some transmitters do this automatically but some have to
be explicitly programmed to do so, and some are unable to do so
altogether due to hardware constraints.

Receiver drivers
----------------

Before the receiver driver may enable the CSI-2 transmitter by using
the :c:type:`v4l2_subdev_video_ops`->s_stream(), it must have powered
the transmitter up by using the
:c:type:`v4l2_subdev_core_ops`->s_power() callback. This may take
place either indirectly by using :c:func:`v4l2_pipeline_pm_use` or
directly.
