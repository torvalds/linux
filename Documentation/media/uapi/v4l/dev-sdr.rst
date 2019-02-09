.. -*- coding: utf-8; mode: rst -*-

.. _sdr:

**************************************
Software Defined Radio Interface (SDR)
**************************************

SDR is an abbreviation of Software Defined Radio, the radio device which
uses application software for modulation or demodulation. This interface
is intended for controlling and data streaming of such devices.

SDR devices are accessed through character device special files named
``/dev/swradio0`` to ``/dev/swradio255`` with major number 81 and
dynamically allocated minor numbers 0 to 255.


Querying Capabilities
=====================

Devices supporting the SDR receiver interface set the
``V4L2_CAP_SDR_CAPTURE`` and ``V4L2_CAP_TUNER`` flag in the
``capabilities`` field of struct
:c:type:`v4l2_capability` returned by the
:ref:`VIDIOC_QUERYCAP` ioctl. That flag means the
device has an Analog to Digital Converter (ADC), which is a mandatory
element for the SDR receiver.

Devices supporting the SDR transmitter interface set the
``V4L2_CAP_SDR_OUTPUT`` and ``V4L2_CAP_MODULATOR`` flag in the
``capabilities`` field of struct
:c:type:`v4l2_capability` returned by the
:ref:`VIDIOC_QUERYCAP` ioctl. That flag means the
device has an Digital to Analog Converter (DAC), which is a mandatory
element for the SDR transmitter.

At least one of the read/write, streaming or asynchronous I/O methods
must be supported.


Supplemental Functions
======================

SDR devices can support :ref:`controls <control>`, and must support
the :ref:`tuner` ioctls. Tuner ioctls are used for setting the
ADC/DAC sampling rate (sampling frequency) and the possible radio
frequency (RF).

The ``V4L2_TUNER_SDR`` tuner type is used for setting SDR device ADC/DAC
frequency, and the ``V4L2_TUNER_RF`` tuner type is used for setting
radio frequency. The tuner index of the RF tuner (if any) must always
follow the SDR tuner index. Normally the SDR tuner is #0 and the RF
tuner is #1.

The :ref:`VIDIOC_S_HW_FREQ_SEEK` ioctl is
not supported.


Data Format Negotiation
=======================

The SDR device uses the :ref:`format` ioctls to select the
capture and output format. Both the sampling resolution and the data
streaming format are bound to that selectable format. In addition to the
basic :ref:`format` ioctls, the
:ref:`VIDIOC_ENUM_FMT` ioctl must be supported as
well.

To use the :ref:`format` ioctls applications set the ``type``
field of a struct :c:type:`v4l2_format` to
``V4L2_BUF_TYPE_SDR_CAPTURE`` or ``V4L2_BUF_TYPE_SDR_OUTPUT`` and use
the struct :c:type:`v4l2_sdr_format` ``sdr`` member
of the ``fmt`` union as needed per the desired operation. Currently
there is two fields, ``pixelformat`` and ``buffersize``, of struct
struct :c:type:`v4l2_sdr_format` which are used.
Content of the ``pixelformat`` is V4L2 FourCC code of the data format.
The ``buffersize`` field is maximum buffer size in bytes required for
data transfer, set by the driver in order to inform application.


.. c:type:: v4l2_sdr_format

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_sdr_format
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``pixelformat``
      - The data format or type of compression, set by the application.
	This is a little endian
	:ref:`four character code <v4l2-fourcc>`. V4L2 defines SDR
	formats in :ref:`sdr-formats`.
    * - __u32
      - ``buffersize``
      - Maximum size in bytes required for data. Value is set by the
	driver.
    * - __u8
      - ``reserved[24]``
      - This array is reserved for future extensions. Drivers and
	applications must set it to zero.


An SDR device may support :ref:`read/write <rw>` and/or streaming
(:ref:`memory mapping <mmap>` or :ref:`user pointer <userp>`) I/O.
