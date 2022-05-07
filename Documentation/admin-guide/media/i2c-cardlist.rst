.. SPDX-License-Identifier: GPL-2.0

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
