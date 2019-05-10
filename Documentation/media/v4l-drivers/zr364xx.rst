.. SPDX-License-Identifier: GPL-2.0

Zoran 364xx based USB webcam module
===================================

site: http://royale.zerezo.com/zr364xx/

mail: royale@zerezo.com

.. note::

   This documentation is outdated

Introduction
------------


This brings support under Linux for the Aiptek PocketDV 3300 in webcam
mode. If you just want to get on your PC the pictures and movies on the
camera, you should use the usb-storage module instead.

The driver works with several other cameras in webcam mode (see the list
below).

Maybe this code can work for other JPEG/USB cams based on the Coach
chips from Zoran?

Possible chipsets are : ZR36430 (ZR36430BGC) and
maybe ZR36431, ZR36440, ZR36442...

You can try the experience changing the vendor/product ID values (look
at the source code).

You can get these values by looking at /var/log/messages when you plug
your camera, or by typing : cat /sys/kernel/debug/usb/devices.

If you manage to use your cam with this code, you can send me a mail
(royale@zerezo.com) with the name of your cam and a patch if needed.

This is a beta release of the driver. Since version 0.70, this driver is
only compatible with V4L2 API and 2.6.x kernels. If you need V4L1 or
2.4x kernels support, please use an older version, but the code is not
maintained anymore. Good luck!

Install
-------

In order to use this driver, you must compile it with your kernel.

Location: Device Drivers -> Multimedia devices -> Video For Linux -> Video Capture Adapters -> V4L USB devices

Usage
-----

modprobe zr364xx debug=X mode=Y

- debug      : set to 1 to enable verbose debug messages
- mode       : 0 = 320x240, 1 = 160x120, 2 = 640x480

You can then use the camera with V4L2 compatible applications, for
example Ekiga.

To capture a single image, try this: dd if=/dev/video0 of=test.jpg bs=1M
count=1

links
-----

http://mxhaard.free.fr/ (support for many others cams including some Aiptek PocketDV)
http://www.harmwal.nl/pccam880/ (this project also supports cameras based on this chipset)

Supported devices
-----------------

======  =======  ==============  ====================
Vendor  Product  Distributor     Model
======  =======  ==============  ====================
0x08ca  0x0109   Aiptek          PocketDV 3300
0x08ca  0x0109   Maxell          Maxcam PRO DV3
0x041e  0x4024   Creative        PC-CAM 880
0x0d64  0x0108   Aiptek          Fidelity 3200
0x0d64  0x0108   Praktica        DCZ 1.3 S
0x0d64  0x0108   Genius          Digital Camera (?)
0x0d64  0x0108   DXG Technology  Fashion Cam
0x0546  0x3187   Polaroid        iON 230
0x0d64  0x3108   Praktica        Exakta DC 2200
0x0d64  0x3108   Genius          G-Shot D211
0x0595  0x4343   Concord         Eye-Q Duo 1300
0x0595  0x4343   Concord         Eye-Q Duo 2000
0x0595  0x4343   Fujifilm        EX-10
0x0595  0x4343   Ricoh           RDC-6000
0x0595  0x4343   Digitrex        DSC 1300
0x0595  0x4343   Firstline       FDC 2000
0x0bb0  0x500d   Concord         EyeQ Go Wireless
0x0feb  0x2004   CRS Electronic  3.3 Digital Camera
0x0feb  0x2004   Packard Bell    DSC-300
0x055f  0xb500   Mustek          MDC 3000
0x08ca  0x2062   Aiptek          PocketDV 5700
0x052b  0x1a18   Chiphead        Megapix V12
0x04c8  0x0729   Konica          Revio 2
0x04f2  0xa208   Creative        PC-CAM 850
0x0784  0x0040   Traveler        Slimline X5
0x06d6  0x0034   Trust           Powerc@m 750
0x0a17  0x0062   Pentax          Optio 50L
0x06d6  0x003b   Trust           Powerc@m 970Z
0x0a17  0x004e   Pentax          Optio 50
0x041e  0x405d   Creative        DiVi CAM 516
0x08ca  0x2102   Aiptek          DV T300
0x06d6  0x003d   Trust           Powerc@m 910Z
======  =======  ==============  ====================
