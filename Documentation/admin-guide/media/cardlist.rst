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

You may also take a look at
https://linuxtv.org/wiki/index.php/Hardware_Device_Information
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

Platform drivers
================

There are several drivers that are focused on providing support for
functionality that are already included at the main board, and don't
use neither USB nor PCI bus. Those drivers are called platform
drivers, and are very popular on embedded devices.

The current supported of platform drivers (not including staging drivers) are
listed below

=================  ============================================================
Driver             Name
=================  ============================================================
am437x-vpfe        TI AM437x VPFE
aspeed-video       Aspeed AST2400 and AST2500
atmel-isc          ATMEL Image Sensor Controller (ISC)
atmel-isi          ATMEL Image Sensor Interface (ISI)
c8sectpfe          SDR platform devices
c8sectpfe          SDR platform devices
cafe_ccic          Marvell 88ALP01 (Cafe) CMOS Camera Controller
cdns-csi2rx        Cadence MIPI-CSI2 RX Controller
cdns-csi2tx        Cadence MIPI-CSI2 TX Controller
coda-vpu           Chips&Media Coda multi-standard codec IP
dm355_ccdc         TI DM355 CCDC video capture
dm644x_ccdc        TI DM6446 CCDC video capture
exynos-fimc-is     EXYNOS4x12 FIMC-IS (Imaging Subsystem)
exynos-fimc-lite   EXYNOS FIMC-LITE camera interface
exynos-gsc         Samsung Exynos G-Scaler
exy                Samsung S5P/EXYNOS4 SoC series Camera Subsystem
fsl-viu            Freescale VIU
imx-pxp            i.MX Pixel Pipeline (PXP)
isdf               TI DM365 ISIF video capture
mmp_camera         Marvell Armada 610 integrated camera controller
mtk_jpeg           Mediatek JPEG Codec
mtk-mdp            Mediatek MDP
mtk-vcodec-dec     Mediatek Video Codec
mtk-vpu            Mediatek Video Processor Unit
mx2_emmaprp        MX2 eMMa-PrP
omap3-isp          OMAP 3 Camera
omap-vout          OMAP2/OMAP3 V4L2-Display
pxa_camera         PXA27x Quick Capture Interface
qcom-camss         Qualcomm V4L2 Camera Subsystem
rcar-csi2          R-Car MIPI CSI-2 Receiver
rcar_drif          Renesas Digital Radio Interface (DRIF)
rcar-fcp           Renesas Frame Compression Processor
rcar_fdp1          Renesas Fine Display Processor
rcar_jpu           Renesas JPEG Processing Unit
rcar-vin           R-Car Video Input (VIN)
renesas-ceu        Renesas Capture Engine Unit (CEU)
rockchip-rga       Rockchip Raster 2d Graphic Acceleration Unit
s3c-camif          Samsung S3C24XX/S3C64XX SoC Camera Interface
s5p-csis           S5P/EXYNOS MIPI-CSI2 receiver (MIPI-CSIS)
s5p-fimc           S5P/EXYNOS4 FIMC/CAMIF camera interface
s5p-g2d            Samsung S5P and EXYNOS4 G2D 2d graphics accelerator
s5p-jpeg           Samsung S5P/Exynos3250/Exynos4 JPEG codec
s5p-mfc            Samsung S5P MFC Video Codec
sh_veu             SuperH VEU mem2mem video processing
sh_vou             SuperH VOU video output
stm32-dcmi         STM32 Digital Camera Memory Interface (DCMI)
sun4i-csi          Allwinner A10 CMOS Sensor Interface Support
sun6i-csi          Allwinner V3s Camera Sensor Interface
sun8i-di           Allwinner Deinterlace
sun8i-rotate       Allwinner DE2 rotation
ti-cal             TI Memory-to-memory multimedia devices
ti-csc             TI DVB platform devices
ti-vpe             TI VPE (Video Processing Engine)
venus-enc          Qualcomm Venus V4L2 encoder/decoder
via-camera         VIAFB camera controller
video-mux          Video Multiplexer
vpif_display       TI DaVinci VPIF V4L2-Display
vpif_capture       TI DaVinci VPIF video capture
vpss               TI DaVinci VPBE V4L2-Display
vsp1               Renesas VSP1 Video Processing Engine
xilinx-tpg         Xilinx Video Test Pattern Generator
xilinx-video       Xilinx Video IP (EXPERIMENTAL)
xilinx-vtc         Xilinx Video Timing Controller
=================  ============================================================

MMC/SDIO DVB adapters
---------------------

=======  ===========================================
Driver   Name
=======  ===========================================
smssdio  Siano SMS1xxx based MDTV via SDIO interface
=======  ===========================================

Firewire driver
===============

The media subsystem also provides a firewire driver for digital TV:

=======  =====================
Driver   Name
=======  =====================
firedtv  FireDTV and FloppyDTV
=======  =====================

Radio drivers
=============

There is also support for pure AM/FM radio, and even for some FM radio
transmitters:

=====================  =========================================================
Driver                 Name
=====================  =========================================================
si4713                 Silicon Labs Si4713 FM Radio Transmitter
radio-aztech           Aztech/Packard Bell Radio
radio-cadet            ADS Cadet AM/FM Tuner
radio-gemtek           GemTek Radio card (or compatible)
radio-maxiradio        Guillemot MAXI Radio FM 2000 radio
radio-miropcm20        miroSOUND PCM20 radio
radio-aimslab          AIMSlab RadioTrack (aka RadioReveal)
radio-rtrack2          AIMSlab RadioTrack II
saa7706h               SAA7706H Car Radio DSP
radio-sf16fmi          SF16-FMI/SF16-FMP/SF16-FMD Radio
radio-sf16fmr2         SF16-FMR2/SF16-FMD2 Radio
radio-shark            Griffin radioSHARK USB radio receiver
shark2                 Griffin radioSHARK2 USB radio receiver
radio-si470x-common    Silicon Labs Si470x FM Radio Receiver
radio-si476x           Silicon Laboratories Si476x I2C FM Radio
radio-tea5764          TEA5764 I2C FM radio
tef6862                TEF6862 Car Radio Enhanced Selectivity Tuner
radio-terratec         TerraTec ActiveRadio ISA Standalone
radio-timb             Enable the Timberdale radio driver
radio-trust            Trust FM radio card
radio-typhoon          Typhoon Radio (a.k.a. EcoRadio)
radio-wl1273           Texas Instruments WL1273 I2C FM Radio
fm_drv                 ISA radio devices
fm_drv                 ISA radio devices
radio-zoltrix          Zoltrix Radio
dsbr100                D-Link/GemTek USB FM radio
radio-keene            Keene FM Transmitter USB
radio-ma901            Masterkit MA901 USB FM radio
radio-mr800            AverMedia MR 800 USB FM radio
radio-raremono         Thanko's Raremono AM/FM/SW radio
radio-si470x-usb       Silicon Labs Si470x FM Radio Receiver support with USB
radio-usb-si4713       Silicon Labs Si4713 FM Radio Transmitter support with USB
=====================  =========================================================

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

The current supported of I²C drivers (not including staging drivers) are
listed below.

Audio decoders, processors and mixers
-------------------------------------

============  ==========================================================
Driver        Name
============  ==========================================================
cs3308        Cirrus Logic CS3308 audio ADC
cs5345        Cirrus Logic CS5345 audio ADC
cs53l32a      Cirrus Logic CS53L32A audio ADC
msp3400       Micronas MSP34xx audio decoders
sony-btf-mpx  Sony BTF's internal MPX
tda1997x      NXP TDA1997x HDMI receiver
tda7432       Philips TDA7432 audio processor
tda9840       Philips TDA9840 audio processor
tea6415c      Philips TEA6415C audio processor
tea6420       Philips TEA6420 audio processor
tlv320aic23b  Texas Instruments TLV320AIC23B audio codec
tvaudio       Simple audio decoder chips
uda1342       Philips UDA1342 audio codec
vp27smpx      Panasonic VP27's internal MPX
wm8739        Wolfson Microelectronics WM8739 stereo audio ADC
wm8775        Wolfson Microelectronics WM8775 audio ADC with input mixer
============  ==========================================================

Audio/Video compression chips
-----------------------------

============  ==========================================================
Driver        Name
============  ==========================================================
saa6752hs     Philips SAA6752HS MPEG-2 Audio/Video Encoder
============  ==========================================================

Camera sensor devices
---------------------

============  ==========================================================
Driver        Name
============  ==========================================================
et8ek8        ET8EK8 camera sensor
hi556         Hynix Hi-556 sensor
imx214        Sony IMX214 sensor
imx219        Sony IMX219 sensor
imx258        Sony IMX258 sensor
imx274        Sony IMX274 sensor
imx290        Sony IMX290 sensor
imx319        Sony IMX319 sensor
imx355        Sony IMX355 sensor
m5mols        Fujitsu M-5MOLS 8MP sensor
mt9m001       mt9m001
mt9m032       MT9M032 camera sensor
mt9m111       mt9m111, mt9m112 and mt9m131
mt9p031       Aptina MT9P031
mt9t001       Aptina MT9T001
mt9t112       Aptina MT9T111/MT9T112
mt9v011       Micron mt9v011 sensor
mt9v032       Micron MT9V032 sensor
mt9v111       Aptina MT9V111 sensor
noon010pc30   Siliconfile NOON010PC30 sensor
ov13858       OmniVision OV13858 sensor
ov2640        OmniVision OV2640 sensor
ov2659        OmniVision OV2659 sensor
ov2680        OmniVision OV2680 sensor
ov2685        OmniVision OV2685 sensor
ov5640        OmniVision OV5640 sensor
ov5645        OmniVision OV5645 sensor
ov5647        OmniVision OV5647 sensor
ov5670        OmniVision OV5670 sensor
ov5675        OmniVision OV5675 sensor
ov5695        OmniVision OV5695 sensor
ov6650        OmniVision OV6650 sensor
ov7251        OmniVision OV7251 sensor
ov7640        OmniVision OV7640 sensor
ov7670        OmniVision OV7670 sensor
ov772x        OmniVision OV772x sensor
ov7740        OmniVision OV7740 sensor
ov8856        OmniVision OV8856 sensor
ov9640        OmniVision OV9640 sensor
ov9650        OmniVision OV9650/OV9652 sensor
rj54n1cb0c    Sharp RJ54N1CB0C sensor
s5c73m3       Samsung S5C73M3 sensor
s5k4ecgx      Samsung S5K4ECGX sensor
s5k5baf       Samsung S5K5BAF sensor
s5k6a3        Samsung S5K6A3 sensor
s5k6aa        Samsung S5K6AAFX sensor
smiapp        SMIA++/SMIA sensor
sr030pc30     Siliconfile SR030PC30 sensor
vs6624        ST VS6624 sensor
============  ==========================================================

Flash devices
-------------

============  ==========================================================
Driver        Name
============  ==========================================================
adp1653       ADP1653 flash
lm3560        LM3560 dual flash driver
lm3646        LM3646 dual flash driver
============  ==========================================================

IR I2C driver
-------------

============  ==========================================================
Driver        Name
============  ==========================================================
ir-kbd-i2c    I2C module for IR
============  ==========================================================

Lens drivers
------------

============  ==========================================================
Driver        Name
============  ==========================================================
ad5820        AD5820 lens voice coil
ak7375        AK7375 lens voice coil
dw9714        DW9714 lens voice coil
dw9807-vcm    DW9807 lens voice coil
============  ==========================================================

Miscellaneous helper chips
--------------------------

============  ==========================================================
Driver        Name
============  ==========================================================
video-i2c     I2C transport video
m52790        Mitsubishi M52790 A/V switch
st-mipid02    STMicroelectronics MIPID02 CSI-2 to PARALLEL bridge
ths7303       THS7303/53 Video Amplifier
============  ==========================================================

RDS decoders
------------

============  ==========================================================
Driver        Name
============  ==========================================================
saa6588       SAA6588 Radio Chip RDS decoder
============  ==========================================================

SDR tuner chips
---------------

============  ==========================================================
Driver        Name
============  ==========================================================
max2175       Maxim 2175 RF to Bits tuner
============  ==========================================================

Video and audio decoders
------------------------

============  ==========================================================
Driver        Name
============  ==========================================================
cx25840       Conexant CX2584x audio/video decoders
saa717x       Philips SAA7171/3/4 audio/video decoders
============  ==========================================================

Video decoders
--------------

============  ==========================================================
Driver        Name
============  ==========================================================
adv7180       Analog Devices ADV7180 decoder
adv7183       Analog Devices ADV7183 decoder
adv748x       Analog Devices ADV748x decoder
adv7604       Analog Devices ADV7604 decoder
adv7842       Analog Devices ADV7842 decoder
bt819         BT819A VideoStream decoder
bt856         BT856 VideoStream decoder
bt866         BT866 VideoStream decoder
ks0127        KS0127 video decoder
ml86v7667     OKI ML86V7667 video decoder
saa7110       Philips SAA7110 video decoder
saa7115       Philips SAA7111/3/4/5 video decoders
tc358743      Toshiba TC358743 decoder
tvp514x       Texas Instruments TVP514x video decoder
tvp5150       Texas Instruments TVP5150 video decoder
tvp7002       Texas Instruments TVP7002 video decoder
tw2804        Techwell TW2804 multiple video decoder
tw9903        Techwell TW9903 video decoder
tw9906        Techwell TW9906 video decoder
tw9910        Techwell TW9910 video decoder
vpx3220       vpx3220a, vpx3216b & vpx3214c video decoders
============  ==========================================================

Video encoders
--------------

============  ==========================================================
Driver        Name
============  ==========================================================
ad9389b       Analog Devices AD9389B encoder
adv7170       Analog Devices ADV7170 video encoder
adv7175       Analog Devices ADV7175 video encoder
adv7343       ADV7343 video encoder
adv7393       ADV7393 video encoder
adv7511-v4l2  Analog Devices ADV7511 encoder
ak881x        AK8813/AK8814 video encoders
saa7127       Philips SAA7127/9 digital video encoders
saa7185       Philips SAA7185 video encoder
ths8200       Texas Instruments THS8200 video encoder
============  ==========================================================

Video improvement chips
-----------------------

============  ==========================================================
Driver        Name
============  ==========================================================
upd64031a     NEC Electronics uPD64031A Ghost Reduction
upd64083      NEC Electronics uPD64083 3-Dimensional Y/C separation
============  ==========================================================

Tuner drivers
-------------

============  ==================================================
Driver        Name
============  ==================================================
e4000         Elonics E4000 silicon tuner
fc0011        Fitipower FC0011 silicon tuner
fc0012        Fitipower FC0012 silicon tuner
fc0013        Fitipower FC0013 silicon tuner
fc2580        FCI FC2580 silicon tuner
it913x        ITE Tech IT913x silicon tuner
m88rs6000t    Montage M88RS6000 internal tuner
max2165       Maxim MAX2165 silicon tuner
mc44s803      Freescale MC44S803 Low Power CMOS Broadband tuners
msi001        Mirics MSi001
mt2060        Microtune MT2060 silicon IF tuner
mt2063        Microtune MT2063 silicon IF tuner
mt20xx        Microtune 2032 / 2050 tuners
mt2131        Microtune MT2131 silicon tuner
mt2266        Microtune MT2266 silicon tuner
mxl301rf      MaxLinear MxL301RF tuner
mxl5005s      MaxLinear MSL5005S silicon tuner
mxl5007t      MaxLinear MxL5007T silicon tuner
qm1d1b0004    Sharp QM1D1B0004 tuner
qm1d1c0042    Sharp QM1D1C0042 tuner
qt1010        Quantek QT1010 silicon tuner
r820t         Rafael Micro R820T silicon tuner
si2157        Silicon Labs Si2157 silicon tuner
tuner-types   Simple tuner support
tda18212      NXP TDA18212 silicon tuner
tda18218      NXP TDA18218 silicon tuner
tda18250      NXP TDA18250 silicon tuner
tda18271      NXP TDA18271 silicon tuner
tda827x       Philips TDA827X silicon tuner
tda8290       TDA 8290/8295 + 8275(a)/18271 tuner combo
tda9887       TDA 9885/6/7 analog IF demodulator
tea5761       TEA 5761 radio tuner
tea5767       TEA 5767 radio tuner
tua9001       Infineon TUA9001 silicon tuner
tuner-xc2028  XCeive xc2028/xc3028 tuners
xc4000        Xceive XC4000 silicon tuner
xc5000        Xceive XC5000 silicon tuner
============  ==================================================

.. toctree::
	:maxdepth: 1

	tuner-cardlist
	frontend-cardlist

Test drivers
============

In order to test userspace applications, there's a number of virtual
drivers, with provide test functionality, simulating real hardware
devices:

=======  ======================================
Driver   Name
=======  ======================================
vicodec  Virtual Codec Driver
vim2m    Virtual Memory-to-Memory Driver
vimc     Virtual Media Controller Driver (VIMC)
vivid    Virtual Video Test Driver
=======  ======================================
