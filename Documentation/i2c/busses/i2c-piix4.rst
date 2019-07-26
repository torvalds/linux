=======================
Kernel driver i2c-piix4
=======================

Supported adapters:
  * Intel 82371AB PIIX4 and PIIX4E
  * Intel 82443MX (440MX)
    Datasheet: Publicly available at the Intel website
  * ServerWorks OSB4, CSB5, CSB6, HT-1000 and HT-1100 southbridges
    Datasheet: Only available via NDA from ServerWorks
  * ATI IXP200, IXP300, IXP400, SB600, SB700 and SB800 southbridges
    Datasheet: Not publicly available
    SB700 register reference available at:
    http://support.amd.com/us/Embedded_TechDocs/43009_sb7xx_rrg_pub_1.00.pdf
  * AMD SP5100 (SB700 derivative found on some server mainboards)
    Datasheet: Publicly available at the AMD website
    http://support.amd.com/us/Embedded_TechDocs/44413.pdf
  * AMD Hudson-2, ML, CZ
    Datasheet: Not publicly available
  * Hygon CZ
    Datasheet: Not publicly available
  * Standard Microsystems (SMSC) SLC90E66 (Victory66) southbridge
    Datasheet: Publicly available at the SMSC website http://www.smsc.com

Authors:
	- Frodo Looijaard <frodol@dds.nl>
	- Philip Edelbrock <phil@netroedge.com>


Module Parameters
-----------------

* force: int
  Forcibly enable the PIIX4. DANGEROUS!
* force_addr: int
  Forcibly enable the PIIX4 at the given address. EXTREMELY DANGEROUS!


Description
-----------

The PIIX4 (properly known as the 82371AB) is an Intel chip with a lot of
functionality. Among other things, it implements the PCI bus. One of its
minor functions is implementing a System Management Bus. This is a true
SMBus - you can not access it on I2C levels. The good news is that it
natively understands SMBus commands and you do not have to worry about
timing problems. The bad news is that non-SMBus devices connected to it can
confuse it mightily. Yes, this is known to happen...

Do ``lspci -v`` and see whether it contains an entry like this::

  0000:00:02.3 Bridge: Intel Corp. 82371AB/EB/MB PIIX4 ACPI (rev 02)
	       Flags: medium devsel, IRQ 9

Bus and device numbers may differ, but the function number must be
identical (like many PCI devices, the PIIX4 incorporates a number of
different 'functions', which can be considered as separate devices). If you
find such an entry, you have a PIIX4 SMBus controller.

On some computers (most notably, some Dells), the SMBus is disabled by
default. If you use the insmod parameter 'force=1', the kernel module will
try to enable it. THIS IS VERY DANGEROUS! If the BIOS did not set up a
correct address for this module, you could get in big trouble (read:
crashes, data corruption, etc.). Try this only as a last resort (try BIOS
updates first, for example), and backup first! An even more dangerous
option is 'force_addr=<IOPORT>'. This will not only enable the PIIX4 like
'force' foes, but it will also set a new base I/O port address. The SMBus
parts of the PIIX4 needs a range of 8 of these addresses to function
correctly. If these addresses are already reserved by some other device,
you will get into big trouble! DON'T USE THIS IF YOU ARE NOT VERY SURE
ABOUT WHAT YOU ARE DOING!

The PIIX4E is just an new version of the PIIX4; it is supported as well.
The PIIX/PIIX3 does not implement an SMBus or I2C bus, so you can't use
this driver on those mainboards.

The ServerWorks Southbridges, the Intel 440MX, and the Victory66 are
identical to the PIIX4 in I2C/SMBus support.

The AMD SB700, SB800, SP5100 and Hudson-2 chipsets implement two
PIIX4-compatible SMBus controllers. If your BIOS initializes the
secondary controller, it will be detected by this driver as
an "Auxiliary SMBus Host Controller".

If you own Force CPCI735 motherboard or other OSB4 based systems you may need
to change the SMBus Interrupt Select register so the SMBus controller uses
the SMI mode.

1) Use lspci command and locate the PCI device with the SMBus controller:
   00:0f.0 ISA bridge: ServerWorks OSB4 South Bridge (rev 4f)
   The line may vary for different chipsets. Please consult the driver source
   for all possible PCI ids (and lspci -n to match them). Lets assume the
   device is located at 00:0f.0.
2) Now you just need to change the value in 0xD2 register. Get it first with
   command: lspci -xxx -s 00:0f.0
   If the value is 0x3 then you need to change it to 0x1:
   setpci  -s 00:0f.0 d2.b=1

Please note that you don't need to do that in all cases, just when the SMBus is
not working properly.


Hardware-specific issues
------------------------

This driver will refuse to load on IBM systems with an Intel PIIX4 SMBus.
Some of these machines have an RFID EEPROM (24RF08) connected to the SMBus,
which can easily get corrupted due to a state machine bug. These are mostly
Thinkpad laptops, but desktop systems may also be affected. We have no list
of all affected systems, so the only safe solution was to prevent access to
the SMBus on all IBM systems (detected using DMI data.)

For additional information, read:
http://www.lm-sensors.org/browser/lm-sensors/trunk/README
