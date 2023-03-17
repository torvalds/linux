===================================
Linux and parallel port IDE devices
===================================

PARIDE v1.03   (c) 1997-8  Grant Guenther <grant@torque.net>
PATA_PARPORT   (c) 2023 Ondrej Zary

1. Introduction
===============

Owing to the simplicity and near universality of the parallel port interface
to personal computers, many external devices such as portable hard-disk,
CD-ROM, LS-120 and tape drives use the parallel port to connect to their
host computer.  While some devices (notably scanners) use ad-hoc methods
to pass commands and data through the parallel port interface, most
external devices are actually identical to an internal model, but with
a parallel-port adapter chip added in.  Some of the original parallel port
adapters were little more than mechanisms for multiplexing a SCSI bus.
(The Iomega PPA-3 adapter used in the ZIP drives is an example of this
approach).  Most current designs, however, take a different approach.
The adapter chip reproduces a small ISA or IDE bus in the external device
and the communication protocol provides operations for reading and writing
device registers, as well as data block transfer functions.  Sometimes,
the device being addressed via the parallel cable is a standard SCSI
controller like an NCR 5380.  The "ditto" family of external tape
drives use the ISA replicator to interface a floppy disk controller,
which is then connected to a floppy-tape mechanism.  The vast majority
of external parallel port devices, however, are now based on standard
IDE type devices, which require no intermediate controller.  If one
were to open up a parallel port CD-ROM drive, for instance, one would
find a standard ATAPI CD-ROM drive, a power supply, and a single adapter
that interconnected a standard PC parallel port cable and a standard
IDE cable.  It is usually possible to exchange the CD-ROM device with
any other device using the IDE interface.

The document describes the support in Linux for parallel port IDE
devices.  It does not cover parallel port SCSI devices, "ditto" tape
drives or scanners.  Many different devices are supported by the
parallel port IDE subsystem, including:

	- MicroSolutions backpack CD-ROM
	- MicroSolutions backpack PD/CD
	- MicroSolutions backpack hard-drives
	- MicroSolutions backpack 8000t tape drive
	- SyQuest EZ-135, EZ-230 & SparQ drives
	- Avatar Shark
	- Imation Superdisk LS-120
	- Maxell Superdisk LS-120
	- FreeCom Power CD
	- Hewlett-Packard 5GB and 8GB tape drives
	- Hewlett-Packard 7100 and 7200 CD-RW drives

as well as most of the clone and no-name products on the market.

To support such a wide range of devices, pata_parport is actually structured
in two parts. There is a base pata_parport module which provides an interface
to kernel libata subsystem, registry and some common methods for accessing
the parallel ports.

The second component is a set of low-level protocol drivers for each of the
parallel port IDE adapter chips.  Thanks to the interest and encouragement of
Linux users from many parts of the world, support is available for almost all
known adapter protocols:

	====    ====================================== ====
        aten    ATEN EH-100                            (HK)
        bpck    Microsolutions backpack                (US)
        comm    DataStor (old-type) "commuter" adapter (TW)
        dstr    DataStor EP-2000                       (TW)
        epat    Shuttle EPAT                           (UK)
        epia    Shuttle EPIA                           (UK)
	fit2    FIT TD-2000			       (US)
	fit3    FIT TD-3000			       (US)
	friq    Freecom IQ cable                       (DE)
        frpw    Freecom Power                          (DE)
        kbic    KingByte KBIC-951A and KBIC-971A       (TW)
	ktti    KT Technology PHd adapter              (SG)
        on20    OnSpec 90c20                           (US)
        on26    OnSpec 90c26                           (US)
	====    ====================================== ====


2. Using pata_parport subsystem
===============================

While configuring the Linux kernel, you may choose either to build
the pata_parport drivers into your kernel, or to build them as modules.

In either case, you will need to select "Parallel port IDE device support"
and at least one of the parallel port communication protocols.
If you do not know what kind of parallel port adapter is used in your drive,
you could begin by checking the file names and any text files on your DOS
installation floppy.  Alternatively, you can look at the markings on
the adapter chip itself.  That's usually sufficient to identify the
correct device.

You can actually select all the protocol modules, and allow the pata_parport
subsystem to try them all for you.

For the "brand-name" products listed above, here are the protocol
and high-level drivers that you would use:

	================	============	========
	Manufacturer		Model		Protocol
	================	============	========
	MicroSolutions		CD-ROM		bpck
	MicroSolutions		PD drive	bpck
	MicroSolutions		hard-drive	bpck
	MicroSolutions          8000t tape      bpck
	SyQuest			EZ, SparQ	epat
	Imation			Superdisk	epat
	Maxell                  Superdisk       friq
	Avatar			Shark		epat
	FreeCom			CD-ROM		frpw
	Hewlett-Packard		5GB Tape	epat
	Hewlett-Packard		7200e (CD)	epat
	Hewlett-Packard		7200e (CD-R)	epat
	================	============	========

All parports and all protocol drivers are probed automatically unless probe=0
parameter is used. So just "modprobe epat" is enough for a Imation SuperDisk
drive to work.

Manual device creation::

	# echo "port protocol mode unit delay" >/sys/bus/pata_parport/new_device

where:

	======== ================================================
	port	 parport name (or "auto" for all parports)
	protocol protocol name (or "auto" for all protocols)
	mode	 mode number (protocol-specific) or -1 for probe
	unit	 unit number (for backpack only, see below)
	delay	 I/O delay (see troubleshooting section below)
	======== ================================================

If you happen to be using a MicroSolutions backpack device, you will
also need to know the unit ID number for each drive.  This is usually
the last two digits of the drive's serial number (but read MicroSolutions'
documentation about this).

If you omit the parameters from the end, defaults will be used, e.g.:

Probe all parports with all protocols::

	# echo auto >/sys/bus/pata_parport/new_device

Probe parport0 using protocol epat and mode 4 (EPP-16)::

	# echo "parport0 epat 4" >/sys/bus/pata_parport/new_device

Probe parport0 using all protocols::

	# echo "parport0 auto" >/sys/bus/pata_parport/new_device

Probe all parports using protoocol epat::

	# echo "auto epat" >/sys/bus/pata_parport/new_device

Deleting devices::

	# echo pata_parport.0 >/sys/bus/pata_parport/delete_device


3. Troubleshooting
==================

3.1  Use EPP mode if you can
----------------------------

The most common problems that people report with the pata_parport drivers
concern the parallel port CMOS settings.  At this time, none of the
protocol modules support ECP mode, or any ECP combination modes.
If you are able to do so, please set your parallel port into EPP mode
using your CMOS setup procedure.

3.2  Check the port delay
-------------------------

Some parallel ports cannot reliably transfer data at full speed.  To
offset the errors, the protocol modules introduce a "port
delay" between each access to the i/o ports.  Each protocol sets
a default value for this delay.  In most cases, the user can override
the default and set it to 0 - resulting in somewhat higher transfer
rates.  In some rare cases (especially with older 486 systems) the
default delays are not long enough.  if you experience corrupt data
transfers, or unexpected failures, you may wish to increase the
port delay.

3.3  Some drives need a printer reset
-------------------------------------

There appear to be a number of "noname" external drives on the market
that do not always power up correctly.  We have noticed this with some
drives based on OnSpec and older Freecom adapters.  In these rare cases,
the adapter can often be reinitialised by issuing a "printer reset" on
the parallel port.  As the reset operation is potentially disruptive in
multiple device environments, the pata_parport drivers will not do it
automatically.  You can however, force a printer reset by doing::

	insmod lp reset=1
	rmmod lp

If you have one of these marginal cases, you should probably build
your pata_parport drivers as modules, and arrange to do the printer reset
before loading the pata_parport drivers.
