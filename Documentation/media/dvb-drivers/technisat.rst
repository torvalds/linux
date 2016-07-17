How to set up the Technisat/B2C2 Flexcop devices
================================================

.. note::

   This documentation is outdated.

Author: Uwe Bugla <uwe.bugla@gmx.de> August 2009

Find out what device you have
-----------------------------

Important Notice: The driver does NOT support Technisat USB 2 devices!

First start your linux box with a shipped kernel:

.. code-block:: none

	lspci -vvv for a PCI device (lsusb -vvv for an USB device) will show you for example:
	02:0b.0 Network controller: Techsan Electronics Co Ltd B2C2 FlexCopII DVB chip /
	Technisat SkyStar2 DVB card (rev 02)

	dmesg | grep frontend may show you for example:
	DVB: registering frontend 0 (Conexant CX24123/CX24109)...

Kernel compilation:
-------------------

If the Flexcop / Technisat is the only DVB / TV / Radio device in your box
get rid of unnecessary modules and check this one:

``Multimedia support`` => ``Customise analog and hybrid tuner modules to build``

In this directory uncheck every driver which is activated there
(except ``Simple tuner support`` for ATSC 3rd generation only -> see case 9 please).

Then please activate:

- Main module part:

  ``Multimedia support`` => ``DVB/ATSC adapters`` => ``Technisat/B2C2 FlexcopII(b) and FlexCopIII adapters``

  #) => ``Technisat/B2C2 Air/Sky/Cable2PC PCI`` (PCI card) or
  #) => ``Technisat/B2C2 Air/Sky/Cable2PC USB`` (USB 1.1 adapter)
     and for troubleshooting purposes:
  #) => ``Enable debug for the B2C2 FlexCop drivers``

- Frontend / Tuner / Demodulator module part:

  ``Multimedia support`` => ``DVB/ATSC adapters``
   => ``Customise the frontend modules to build`` ``Customise DVB frontends`` =>

  - SkyStar DVB-S Revision 2.3:

    #) => ``Zarlink VP310/MT312/ZL10313 based``
    #) => ``Generic I2C PLL based tuners``

  - SkyStar DVB-S Revision 2.6:

    #) => ``ST STV0299 based``
    #) => ``Generic I2C PLL based tuners``

  - SkyStar DVB-S Revision 2.7:

    #) => ``Samsung S5H1420 based``
    #) => ``Integrant ITD1000 Zero IF tuner for DVB-S/DSS``
    #) => ``ISL6421 SEC controller``

  - SkyStar DVB-S Revision 2.8:

    #) => ``Conexant CX24123 based``
    #) => ``Conexant CX24113/CX24128 tuner for DVB-S/DSS``
    #) => ``ISL6421 SEC controller``

  - AirStar DVB-T card:

    #) => ``Zarlink MT352 based``
    #) => ``Generic I2C PLL based tuners``

  - CableStar DVB-C card:

    #) => ``ST STV0297 based``
    #) => ``Generic I2C PLL based tuners``

  - AirStar ATSC card 1st generation:

    #) => ``Broadcom BCM3510``

  - AirStar ATSC card 2nd generation:

    #) => ``NxtWave Communications NXT2002/NXT2004 based``
    #) => ``Generic I2C PLL based tuners``

  - AirStar ATSC card 3rd generation:

    #) => ``LG Electronics LGDT3302/LGDT3303 based``
    #) ``Multimedia support`` => ``Customise analog and hybrid tuner modules to build`` => ``Simple tuner support``

