.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.1-no-invariants-or-later

========
Glossary
========

.. note::

   The goal of this section is to standardize the terms used within the media
   userspace API documentation. This is Work In Progress.

.. Please keep the glossary entries in alphabetical order

.. glossary::

    Bridge Driver
	A :term:`Device Driver` that implements the main logic to talk with
	media hardware.

    CEC API
	**Consumer Electronics Control API**

	An API designed to receive and transmit data via an HDMI
	CEC interface.

	See :ref:`cec`.

    Device Driver
	Part of the Linux Kernel that implements support for a hardware
	component.

    Device Node
	A character device node in the file system used to control and
	transfer data in and out of a Kernel driver.

    Digital TV API
	**Previously known as DVB API**

	An API designed to control a subset of the :term:`Media Hardware`
	that implements	digital TV (e. g. DVB, ATSC, ISDB, etc).

	See :ref:`dvbapi`.

    DSP
        **Digital Signal Processor**

	A specialized :term:`Microprocessor`, with its architecture
	optimized for the operational needs of digital signal processing.

    FPGA
	**Field-programmable Gate Array**

	An :term:`IC` circuit designed to be configured by a customer or
	a designer after manufacturing.

	See https://en.wikipedia.org/wiki/Field-programmable_gate_array.

    Hardware Component
	A subset of the :term:`Media Hardware`. For example an :term:`I²C` or
	:term:`SPI` device, or an :term:`IP Block` inside an
	:term:`SoC` or :term:`FPGA`.

    Hardware Peripheral
	A group of :term:`hardware components <Hardware Component>` that
	together make a larger user-facing functional peripheral. For
	instance, the :term:`SoC` :term:`ISP` :term:`IP Block`
	and the external camera sensors together make a camera hardware
	peripheral.

	Also known as :term:`Peripheral`.

    I²C
	**Inter-Integrated Circuit**

	A  multi-master, multi-slave, packet switched, single-ended,
	serial computer bus used to control some hardware components
	like sub-device hardware components.

	See http://www.nxp.com/docs/en/user-guide/UM10204.pdf.

    IC
	**Integrated circuit**

	A set of electronic circuits on one small flat piece of
	semiconductor material, normally silicon.

	Also known as chip.

    IP Block
	**Intellectual property core**

	In electronic design a semiconductor intellectual property core,
	is a reusable unit of logic, cell, or integrated circuit layout
	design that is the intellectual property of one party.
	IP Blocks may be licensed to another party or can be owned
	and used by a single party alone.

	See https://en.wikipedia.org/wiki/Semiconductor_intellectual_property_core).

    ISP
	**Image Signal Processor**

	A specialized processor that implements a set of algorithms for
	processing image data. ISPs may implement algorithms for lens
	shading correction, demosaicing, scaling and pixel format conversion
	as well as produce statistics for the use of the control
	algorithms (e.g. automatic exposure, white balance and focus).

    Media API
	A set of userspace APIs used to control the media hardware. It is
	composed by:

	  - :term:`CEC API`;
	  - :term:`Digital TV API`;
	  - :term:`MC API`;
	  - :term:`RC API`; and
	  - :term:`V4L2 API`.

	See :doc:`index`.

    MC API
	**Media Controller API**

	An API designed to expose and control the relationships between
	multimedia devices and sub-devices.

	See :ref:`media_controller`.

    MC-centric
	:term:`V4L2 Hardware` device driver that requires :term:`MC API`.

	Such drivers have ``V4L2_CAP_IO_MC`` device_caps field set
	(see :ref:`VIDIOC_QUERYCAP`).

	See :ref:`v4l2_hardware_control` for more details.

    Media Hardware
	Subset of the hardware that is supported by the Linux Media API.

	This includes audio and video capture and playback hardware,
	digital and analog TV, camera sensors, ISPs, remote controllers,
	codecs, HDMI Consumer Electronics Control, HDMI capture, etc.

    Microprocessor
	Electronic circuitry that carries out the instructions of a
	computer program by performing the basic arithmetic, logical,
	control and input/output (I/O) operations specified by the
	instructions on a single integrated circuit.

    Peripheral
	The same as :term:`Hardware Peripheral`.

    RC API
	**Remote Controller API**

	An API designed to receive and transmit data from remote
	controllers.

	See :ref:`remote_controllers`.

    SMBus
	A subset of I²C, which defines a stricter usage of the bus.

    SPI
	**Serial Peripheral Interface Bus**

	Synchronous serial communication interface specification used for
	short distance communication, primarily in embedded systems.

    SoC
	**System on a Chip**

	An integrated circuit that integrates all components of a computer
	or other electronic systems.

    V4L2 API
	**V4L2 userspace API**

	The userspace API defined in :ref:`v4l2spec`, which is used to
	control a V4L2 hardware.

    V4L2 Device Node
	A :term:`Device Node` that is associated to a V4L driver.

	The V4L2 device node naming is specified at :ref:`v4l2_device_naming`.

    V4L2 Hardware
	Part of the media hardware which is supported by the :term:`V4L2 API`.

    V4L2 Sub-device
	V4L2 hardware components that aren't controlled by a
	:term:`Bridge Driver`. See :ref:`subdev`.

    Video-node-centric
	V4L2 device driver that doesn't require a media controller to be used.

	Such drivers have the ``V4L2_CAP_IO_MC`` device_caps field unset
	(see :ref:`VIDIOC_QUERYCAP`).

    V4L2 Sub-device API
	Part of the :term:`V4L2 API` which control
	:term:`V4L2 sub-devices <V4L2 Sub-device>`, like sensors,
	HDMI receivers, scalers, deinterlacers.

	See :ref:`v4l2_hardware_control` for more details.
