.. SPDX-License-Identifier: GPL-2.0

========================================
The COPS LocalTalk Linux driver (cops.c)
========================================

By Jay Schulist <jschlst@samba.org>

This driver has two modes and they are: Dayna mode and Tangent mode.
Each mode corresponds with the type of card. It has been found
that there are 2 main types of cards and all other cards are
the same and just have different names or only have minor differences
such as more IO ports. As this driver is tested it will
become more clear exactly what cards are supported.

Right now these cards are known to work with the COPS driver. The
LT-200 cards work in a somewhat more limited capacity than the
DL200 cards, which work very well and are in use by many people.

TANGENT driver mode:
	- Tangent ATB-II, Novell NL-1000, Daystar Digital LT-200

DAYNA driver mode:
	- Dayna DL2000/DaynaTalk PC (Half Length), COPS LT-95,
	- Farallon PhoneNET PC III, Farallon PhoneNET PC II

Other cards possibly supported mode unknown though:
	- Dayna DL2000 (Full length)

The COPS driver defaults to using Dayna mode. To change the driver's
mode if you built a driver with dual support use board_type=1 or
board_type=2 for Dayna or Tangent with insmod.

Operation/loading of the driver
===============================

Use modprobe like this:	/sbin/modprobe cops.o (IO #) (IRQ #)
If you do not specify any options the driver will try and use the IO = 0x240,
IRQ = 5. As of right now I would only use IRQ 5 for the card, if autoprobing.

To load multiple COPS driver Localtalk cards you can do one of the following::

	insmod cops io=0x240 irq=5
	insmod -o cops2 cops io=0x260 irq=3

Or in lilo.conf put something like this::

	append="ether=5,0x240,lt0 ether=3,0x260,lt1"

Then bring up the interface with ifconfig. It will look something like this::

  lt0       Link encap:UNSPEC  HWaddr 00-00-00-00-00-00-00-F7-00-00-00-00-00-00-00-00
	    inet addr:192.168.1.2  Bcast:192.168.1.255  Mask:255.255.255.0
	    UP BROADCAST RUNNING NOARP MULTICAST  MTU:600  Metric:1
	    RX packets:0 errors:0 dropped:0 overruns:0 frame:0
	    TX packets:0 errors:0 dropped:0 overruns:0 carrier:0 coll:0

Netatalk Configuration
======================

You will need to configure atalkd with something like the following to make
it work with the cops.c driver.

* For single LTalk card use::

    dummy -seed -phase 2 -net 2000 -addr 2000.10 -zone "1033"
    lt0 -seed -phase 1 -net 1000 -addr 1000.50 -zone "1033"

* For multiple cards, Ethernet and LocalTalk::

    eth0 -seed -phase 2 -net 3000 -addr 3000.20 -zone "1033"
    lt0 -seed -phase 1 -net 1000 -addr 1000.50 -zone "1033"

* For multiple LocalTalk cards, and an Ethernet card.

* Order seems to matter here, Ethernet last::

    lt0 -seed -phase 1 -net 1000 -addr 1000.10 -zone "LocalTalk1"
    lt1 -seed -phase 1 -net 2000 -addr 2000.20 -zone "LocalTalk2"
    eth0 -seed -phase 2 -net 3000 -addr 3000.30 -zone "EtherTalk"
