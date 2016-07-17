How to set up the Technisat/B2C2 Flexcop devices
================================================

1) Find out what device you have
================================

Important Notice: The driver does NOT support Technisat USB 2 devices!

First start your linux box with a shipped kernel:
lspci -vvv for a PCI device (lsusb -vvv for an USB device) will show you for example:
02:0b.0 Network controller: Techsan Electronics Co Ltd B2C2 FlexCopII DVB chip /
 Technisat SkyStar2 DVB card (rev 02)

dmesg | grep frontend may show you for example:
DVB: registering frontend 0 (Conexant CX24123/CX24109)...

2) Kernel compilation:
======================

If the Flexcop / Technisat is the only DVB / TV / Radio device in your box
 get rid of unnecessary modules and check this one:
"Multimedia support" => "Customise analog and hybrid tuner modules to build"
In this directory uncheck every driver which is activated there
 (except "Simple tuner support" for ATSC 3rd generation only -> see case 9 please).

Then please activate:
2a) Main module part:
"Multimedia support" => "DVB/ATSC adapters"
 => "Technisat/B2C2 FlexcopII(b) and FlexCopIII adapters"

a.) => "Technisat/B2C2 Air/Sky/Cable2PC PCI" (PCI card) or
b.) => "Technisat/B2C2 Air/Sky/Cable2PC USB" (USB 1.1 adapter)
 and for troubleshooting purposes:
c.) => "Enable debug for the B2C2 FlexCop drivers"

2b) Frontend / Tuner / Demodulator module part:
"Multimedia support" => "DVB/ATSC adapters"
 => "Customise the frontend modules to build" "Customise DVB frontends" =>

1.) SkyStar DVB-S Revision 2.3:
a.) => "Zarlink VP310/MT312/ZL10313 based"
b.) => "Generic I2C PLL based tuners"

2.) SkyStar DVB-S Revision 2.6:
a.) => "ST STV0299 based"
b.) => "Generic I2C PLL based tuners"

3.) SkyStar DVB-S Revision 2.7:
a.) => "Samsung S5H1420 based"
b.) => "Integrant ITD1000 Zero IF tuner for DVB-S/DSS"
c.) => "ISL6421 SEC controller"

4.) SkyStar DVB-S Revision 2.8:
a.) => "Conexant CX24123 based"
b.) => "Conexant CX24113/CX24128 tuner for DVB-S/DSS"
c.) => "ISL6421 SEC controller"

5.) AirStar DVB-T card:
a.) => "Zarlink MT352 based"
b.) => "Generic I2C PLL based tuners"

6.) CableStar DVB-C card:
a.) => "ST STV0297 based"
b.) => "Generic I2C PLL based tuners"

7.) AirStar ATSC card 1st generation:
a.) => "Broadcom BCM3510"

8.) AirStar ATSC card 2nd generation:
a.) => "NxtWave Communications NXT2002/NXT2004 based"
b.) => "Generic I2C PLL based tuners"

9.) AirStar ATSC card 3rd generation:
a.) => "LG Electronics LGDT3302/LGDT3303 based"
b.) "Multimedia support" => "Customise analog and hybrid tuner modules to build"
 => "Simple tuner support"

Author: Uwe Bugla <uwe.bugla@gmx.de> August 2009
