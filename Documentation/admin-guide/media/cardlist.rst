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

Please notice that this list is not exhaustive. You may also take a
look at https://linuxtv.org/wiki/index.php/Hardware_Device_Information
for more details about supported cards.

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

The current supported USB cards (not including staging drivers) are
listed below\ [#]_.

.. [#]

   some of the drivers have sub-drivers, not shown at this table.
   In particular, gspca driver has lots of sub-drivers,
   for cameras not supported by the USB Video Class (UVC) driver,
   as shown at :doc:`gspca card list <gspca-cardlist>`.


======================  =========================================================
Driver                  Name
======================  =========================================================
airspy                  AirSpy
au0828                  Auvitek AU0828
b2c2-flexcop-usb        Technisat/B2C2 Air/Sky/Cable2PC USB
cpia2                   CPiA2 Video For Linux
cx231xx                 Conexant cx231xx USB video capture
dvb-as102               Abilis AS102 DVB receiver
dvb-ttusb-budget        Technotrend/Hauppauge Nova - USB devices
dvb-usb-a800            AVerMedia AverTV DVB-T USB 2.0 (A800)
dvb-usb-af9005          Afatech AF9005 DVB-T USB1.1
dvb-usb-af9015          Afatech AF9015 DVB-T USB2.0
dvb-usb-af9035          Afatech AF9035 DVB-T USB2.0
dvb-usb-anysee          Anysee DVB-T/C USB2.0
dvb-usb-au6610          Alcor Micro AU6610 USB2.0
dvb-usb-az6007          AzureWave 6007 and clones DVB-T/C USB2.0
dvb-usb-az6027          Azurewave DVB-S/S2 USB2.0 AZ6027
dvb-usb-ce6230          Intel CE6230 DVB-T USB2.0
dvb-usb-cinergyT2       Terratec CinergyT2/qanu USB 2.0 DVB-T
dvb-usb-cxusb           Conexant USB2.0 hybrid
dvb-usb-dib0700         DiBcom DiB0700
dvb-usb-dibusb-common   DiBcom DiB3000M-B
dvb-usb-dibusb-mc       DiBcom DiB3000M-C/P
dvb-usb-digitv          Nebula Electronics uDigiTV DVB-T USB2.0
dvb-usb-dtt200u         WideView WT-200U and WT-220U (pen) DVB-T
dvb-usb-dtv5100         AME DTV-5100 USB2.0 DVB-T
dvb-usb-dvbsky          DVBSky USB
dvb-usb-dw2102          DvbWorld & TeVii DVB-S/S2 USB2.0
dvb-usb-ec168           E3C EC168 DVB-T USB2.0
dvb-usb-gl861           Genesys Logic GL861 USB2.0
dvb-usb-gp8psk          GENPIX 8PSK->USB module
dvb-usb-lmedm04         LME DM04/QQBOX DVB-S USB2.0
dvb-usb-m920x           Uli m920x DVB-T USB2.0
dvb-usb-nova-t-usb2     Hauppauge WinTV-NOVA-T usb2 DVB-T USB2.0
dvb-usb-opera           Opera1 DVB-S USB2.0 receiver
dvb-usb-pctv452e        Pinnacle PCTV HDTV Pro USB device/TT Connect S2-3600
dvb-usb-rtl28xxu        Realtek RTL28xxU DVB USB
dvb-usb-technisat-usb2  Technisat DVB-S/S2 USB2.0
dvb-usb-ttusb2          Pinnacle 400e DVB-S USB2.0
dvb-usb-umt-010         HanfTek UMT-010 DVB-T USB2.0
dvb_usb_v2              Support for various USB DVB devices v2
dvb-usb-vp702x          TwinhanDTV StarBox and clones DVB-S USB2.0
dvb-usb-vp7045          TwinhanDTV Alpha/MagicBoxII, DNTV tinyUSB2, Beetle USB2.0
em28xx                  Empia EM28xx USB devices
go7007                  WIS GO7007 MPEG encoder
gspca                   Drivers for several USB Cameras
hackrf                  HackRF
hdpvr                   Hauppauge HD PVR
msi2500                 Mirics MSi2500
mxl111sf-tuner          MxL111SF DTV USB2.0
pvrusb2                 Hauppauge WinTV-PVR USB2
pwc                     USB Philips Cameras
s2250                   Sensoray 2250/2251
s2255drv                USB Sensoray 2255 video capture device
smsusb                  Siano SMS1xxx based MDTV receiver
stkwebcam               USB Syntek DC1125 Camera
tm6000-alsa             TV Master TM5600/6000/6010 audio
tm6000-dvb              DVB Support for tm6000 based TV cards
tm6000                  TV Master TM5600/6000/6010 driver
ttusb_dec               Technotrend/Hauppauge USB DEC devices
usbtv                   USBTV007 video capture
uvcvideo                USB Video Class (UVC)
zd1301                  ZyDAS ZD1301
zr364xx                 USB ZR364XX Camera
======================  =========================================================

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

	other-usb-cardlist

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

The current supported PCI/PCIe cards (not including staging drivers) are
listed below\ [#]_.

.. [#] some of the drivers have sub-drivers, not shown at this table

================  ========================================================
Driver            Name
================  ========================================================
altera-ci         Altera FPGA based CI module
b2c2-flexcop-pci  Technisat/B2C2 Air/Sky/Cable2PC PCI
bt878             DVB/ATSC Support for bt878 based TV cards
bttv              BT8x8 Video For Linux
cobalt            Cisco Cobalt
cx18              Conexant cx23418 MPEG encoder
cx23885           Conexant cx23885 (2388x successor)
cx25821           Conexant cx25821
cx88xx            Conexant 2388x (bt878 successor)
ddbridge          Digital Devices bridge
dm1105            SDMC DM1105 based PCI cards
dt3155            DT3155 frame grabber
dvb-ttpci         AV7110 cards
earth-pt1         PT1 cards
earth-pt3         Earthsoft PT3 cards
hexium_gemini     Hexium Gemini frame grabber
hexium_orion      Hexium HV-PCI6 and Orion frame grabber
hopper            HOPPER based cards
ipu3-cio2         Intel ipu3-cio2 driver
ivtv              Conexant cx23416/cx23415 MPEG encoder/decoder
ivtvfb            Conexant cx23415 framebuffer
mantis            MANTIS based cards
meye              Sony Vaio Picturebook Motion Eye
mxb               Siemens-Nixdorf 'Multimedia eXtension Board'
netup-unidvb      NetUP Universal DVB card
ngene             Micronas nGene
pluto2            Pluto2 cards
saa7134           Philips SAA7134
saa7164           NXP SAA7164
smipcie           SMI PCIe DVBSky cards
solo6x10          Bluecherry / Softlogic 6x10 capture cards (MPEG-4/H.264)
sta2x11_vip       STA2X11 VIP Video For Linux
tw5864            Techwell TW5864 video/audio grabber and encoder
tw686x            Intersil/Techwell TW686x
tw68              Techwell tw68x Video For Linux
================  ========================================================

Some of those drivers support multiple devices, as shown at the card
lists below:

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
	frontend-cardlist
