.. SPDX-License-Identifier: GPL-2.0

==========
Cards List
==========

The media subsystem provide support for lots of PCI and USB drivers, plus
platform-specific drivers. It also contains several ancillary I²C drivers.

The platform-specific drivers are usually present on embedded systems,
or are supported by the main board. Usually, setting them is done via
OpenFirmware or ACPI.

The PCI and USB drivers, however, are independent of the system's board,
and may be added/removed by the user.

This section contains a list of supported PCI and USB boards.

Please notice that this list is not exaustive.

USB drivers
===========

The USB boards are identified by an identification called USB ID.

The ``lsusb`` command allows identifying the USB IDs::

    $ lsusb
    ...
    Bus 001 Device 015: ID 046d:082d Logitech, Inc. HD Pro Webcam C920
    Bus 001 Device 074: ID 2040:b131 Hauppauge
    Bus 001 Device 075: ID 2013:024f PCTV Systems nanoStick T2 290e
    ...

Newer camera devices use a standard way to expose themselves as such,
via USB Video Class. Those cameras are automatically supported by the
``uvc-driver``.

Older cameras and TV USB devices uses USB Vendor Classes: each vendor
defines its own way to access the device. This section contains
card lists for such vendor-class devices.

While this is not as common as on PCI, sometimes the same USB ID is used
by different products. So, several media drivers allow passing a ``card=``
parameter, in order to setup a card number that would match the correct
settings for an specific product type.

.. toctree::
	:maxdepth: 1

	au0828-cardlist
	cx231xx-cardlist
	em28xx-cardlist
	tm6000-cardlist
	siano-cardlist
	usbvision-cardlist

	gspca-cardlist

	dvb-usb-dib0700-cardlist
	dvb-usb-dibusb-mb-cardlist
	dvb-usb-dibusb-mc-cardlist

	dvb-usb-a800-cardlist
	dvb-usb-af9005-cardlist
	dvb-usb-az6027-cardlist
	dvb-usb-cinergyT2-cardlist
	dvb-usb-cxusb-cardlist
	dvb-usb-digitv-cardlist
	dvb-usb-dtt200u-cardlist
	dvb-usb-dtv5100-cardlist
	dvb-usb-dw2102-cardlist
	dvb-usb-gp8psk-cardlist
	dvb-usb-m920x-cardlist
	dvb-usb-nova-t-usb2-cardlist
	dvb-usb-opera1-cardlist
	dvb-usb-pctv452e-cardlist
	dvb-usb-technisat-usb2-cardlist
	dvb-usb-ttusb2-cardlist
	dvb-usb-umt-010-cardlist
	dvb-usb-vp702x-cardlist
	dvb-usb-vp7045-cardlist

	dvb-usb-af9015-cardlist
	dvb-usb-af9035-cardlist
	dvb-usb-anysee-cardlist
	dvb-usb-au6610-cardlist
	dvb-usb-az6007-cardlist
	dvb-usb-ce6230-cardlist
	dvb-usb-dvbsky-cardlist
	dvb-usb-ec168-cardlist
	dvb-usb-gl861-cardlist
	dvb-usb-lmedm04-cardlist
	dvb-usb-mxl111sf-cardlist
	dvb-usb-rtl28xxu-cardlist
	dvb-usb-zd1301-cardlist

PCI drivers
===========

The PCI boards are identified by an identification called PCI ID. The PCI ID
is actually composed by two parts:

	- Vendor ID and device ID;
	- Subsystem ID and Subsystem device ID;

The ``lspci -nn`` command allows identifying the vendor/device PCI IDs::

    $ lspci -nn
    ...
    00:0b.0 Multimedia controller [0480]: Brooktree Corporation Bt878 Audio Capture [109e:0878] (rev 11)
    01:00.0 Multimedia video controller [0400]: Conexant Systems, Inc. CX23887/8 PCIe Broadcast Audio and Video Decoder with 3D Comb [14f1:8880] (rev 0f)
    01:01.0 Multimedia controller [0480]: Philips Semiconductors SAA7131/SAA7133/SAA7135 Video Broadcast Decoder [1131:7133] (rev d1)
    02:01.0 Multimedia video controller [0400]: Internext Compression Inc iTVC15 (CX23415) Video Decoder [4444:0803] (rev 01)
    02:02.0 Multimedia video controller [0400]: Conexant Systems, Inc. CX23418 Single-Chip MPEG-2 Encoder with Integrated Analog Video/Broadcast Audio Decoder [14f1:5b7a]
    02:03.0 Multimedia video controller [0400]: Brooktree Corporation Bt878 Video Capture [109e:036e] (rev 11)
    ...

The subsystem IDs can be obtained using ``lspci -vn``

.. code-block:: none
   :emphasize-lines: 4

    $ lspci -vn
    ...
	01:01.0 0480: 1131:7133 (rev d1)
	        Subsystem: 1461:f01d
	        Flags: bus master, medium devsel, latency 32, IRQ 209
	        Memory at e2002000 (32-bit, non-prefetchable) [size=2K]
	        Capabilities: [40] Power Management version 2
    ...

Unfortunately, sometimes the same PCI ID is used by different products.
So, several media drivers allow passing a ``card=`` parameter, in order
to setup a card number that would match the correct settings for an
specific board.

.. toctree::
	:maxdepth: 1

	bttv-cardlist
	cx18-cardlist
	cx23885-cardlist
	cx88-cardlist
	ivtv-cardlist
	saa7134-cardlist
	saa7164-cardlist

I²C drivers
===========

The I²C (Inter-Integrated Circuit) bus is a three-wires bus used internally
at the media cards for communication between different chips. While the bus
is not visible to the Linux Kernel, drivers need to send and receive
commands via the bus. The Linux Kernel driver abstraction has support to
implement different drivers for each component inside an I²C bus, as if
the bus were visible to the main system board.

One of the problems with I²C devices is that sometimes the same device may
work with different I²C hardware. This is common, for example, on devices
that comes with a tuner for North America market, and another one for
Europe. Some drivers have a ``tuner=`` modprobe parameter to allow using a
different tuner number in order to address such issue.

.. toctree::
	:maxdepth: 1

	tuner-cardlist
