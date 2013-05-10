Welcome to Beta Release 2 of the combination ISDN driver for SpellCaster's
ISA ISDN adapters. Please note this release 2 includes support for the
DataCommute/BRI and TeleCommute/BRI adapters only and any other use is 
guaranteed to fail. If you have a DataCommute/PRI installed in the test
computer, we recommend removing it as it will be detected but will not
be usable.  To see what we have done to Beta Release 2, see section 3.

Speaking of guarantees, THIS IS BETA SOFTWARE and as such contains
bugs and defects either known or unknown. Use this software at your own
risk. There is NO SUPPORT for this software. Some help may be available
through the web site or the mailing list but such support is totally at
our own option and without warranty. If you choose to assume all and
total risk by using this driver, we encourage you to join the beta
mailing list.

To join the Linux beta mailing list, send a message to:
majordomo@spellcast.com with the words "subscribe linux-beta" as the only
contents of the message. Do not include a signature. If you choose to
remove yourself from this list at a later date, send another message to
the same address with the words "unsubscribe linux-beta" as its only
contents.

TABLE OF CONTENTS
-----------------
	1. Introduction
	 1.1 What is ISDN4Linux?
	 1.2 What is different between this driver and previous drivers?
	 1.3 How do I setup my system with the correct software to use
	     this driver release?
	
	2. Basic Operations
	 2.1 Unpacking and installing the driver
	 2.2 Read the man pages!!!
	 2.3 Installing the driver
	 2.4 Removing the driver
	 2.5 What to do if it doesn't load
	 2.6 How to setup ISDN4Linux with the driver

	3. Beta Change Summaries and Miscellaneous Notes

1. Introduction
---------------

The revision 2 Linux driver for SpellCaster ISA ISDN adapters is built
upon ISDN4Linux available separately or as included in Linux 2.0 and later.
The driver will support a maximum of 4 adapters in any one system of any
type including DataCommute/BRI, DataCommute/PRI and TeleCommute/BRI for a
maximum of 92 channels for host. The driver is supplied as a module in
source form and needs to be complied before it can be used. It has been
tested on Linux 2.0.20.

1.1 What Is ISDN4Linux

ISDN4Linux is a driver and set of tools used to access and use ISDN devices
on a Linux platform in a common and standard way. It supports HDLC and PPP
protocols and offers channel bundling and MLPPP support. To use ISDN4Linux
you need to configure your kernel for ISDN support and get the ISDN4Linux
tool kit from our web site.

ISDN4Linux creates a channel pool from all of the available ISDN channels
and therefore can function across adapters. When an ISDN4Linux compliant
driver (such as ours) is loaded, all of the channels go into a pool and
are used on a first-come first-served basis. In addition, individual
channels can be specifically bound to particular interfaces.

1.2 What is different between this driver and previous drivers?

The revision 2 driver besides adopting the ISDN4Linux architecture has many
subtle and not so subtle functional differences from previous releases. These
include:
	- More efficient shared memory management combined with a simpler
	  configuration. All adapters now use only 16Kbytes of shared RAM
	  versus between 16K and 64K. New methods for using the shared RAM
	  allow us to utilize all of the available RAM on the adapter through
	  only one 16K page.
	- Better detection of available upper memory. The probing routines
	  have been improved to better detect available shared RAM pages and
	  used pages are now locked.
	- Decreased loading time and a wider range of I/O ports probed.
	  We have significantly reduced the amount of time it takes to load
	  the driver and at the same time doubled the number of I/O ports
	  probed increasing the likelihood of finding an adapter.
	- We now support all ISA adapter models with a single driver instead
	  of separate drivers for each model. The revision 2 driver supports
	  the DataCommute/BRI, DataCommute/PRI and TeleCommute/BRI in any
	  combination up to a maximum of four adapters per system.
	- On board PPP protocol support has been removed in favour of the
	  sync-PPP support used in ISDN4Linux. This means more control of
	  the protocol parameters, faster negotiation time and a more
	  familiar interface.

1.3 How do I setup my system with the correct software to use
    this driver release?

Before you can compile, install and use the SpellCaster ISA ISDN driver, you
must ensure that the following software is installed, configured and running:

	- Linux kernel 2.0.20 or later with the required init and ps
	  versions. Please see your distribution vendor for the correct
	  utility packages. The latest kernel is available from
	  ftp://sunsite.unc.edu/pub/Linux/kernel/v2.0/

	- The latest modules package (modules-2.0.0.tar.gz) from
	  ftp://sunsite.unc.edu/pub/Linux/kernel/modules-2.0.0.tar.gz

	- The ISDN4Linux tools available from 
	  ftp://ftp.franken.de/pub/isdn4linux/v2.0/isdn4k-utils-2.0.tar.gz
	  This package may fail to compile for you so you can alternatively
	  get a pre-compiled version from
	  ftp://ftp.spellcast.com/pub/drivers/isdn4linux/isdn4k-bin-2.0.tar.gz


2. Basic Operations
-------------------

2.1 Unpacking and installing the driver

	1. As root, create a directory in a convenient place. We suggest
	   /usr/src/spellcaster.
	
	2. Unpack the archive with :
		tar xzf sc-n.nn.tar.gz -C /usr/src/spellcaster
	
	3. Change directory to /usr/src/spellcaster

	4. Read the README and RELNOTES files.

	5. Run 'make' and if all goes well, run 'make install'.

2.2 Read the man pages!!!

Make sure you read the scctrl(8) and sc(4) manual pages before continuing
any further. Type 'man 8 scctrl' and 'man 4 sc'.

2.3 Installing the driver

To install the driver, type '/sbin/insmod sc' as root. sc(4) details options
you can specify but you shouldn't need to use any unless this doesn't work.

Make sure the driver loaded and detected all of the adapters by typing
'dmesg'.

The driver can be configured so that it is loaded upon startup.  To do this, 
edit the file "/etc/modules/'uname -f'/'uname -v'" and insert the driver name
"sc" into this file.

2.4 Removing the driver

To remove the driver, delete any interfaces that may exist (see isdnctrl(8)
for more on this) and then type '/sbin/rmmod sc'.

2.5 What to do if it doesn't load

If, when you try to install the driver, you get a message mentioning
'register_isdn' then you do not have the ISDN4Linux system installed. Please
make sure that ISDN support is configured in the kernel.

If you get a message that says 'initialization of sc failed', then the
driver failed to detect an adapter or failed to find resources needed such
as a free IRQ line or shared memory segment. If you are sure there are free
resources available, use the insmod options detailed in sc(4) to override
the probing function.  

Upon testing, the following problem was noted, the driver would load without
problems, but the board would not respond beyond that point.  When a check was 
done with 'cat /proc/interrupts' the interrupt count for sc was 0.  In the event 
of this problem, change the BIOS settings so that the interrupts in question are
reserved for ISA use only.   


2.6 How to setup ISDN4Linux with the driver

There are three main configurations which you can use with the driver:

A)	Basic HDLC connection
B)	PPP connection
C)	MLPPP connection

It should be mentioned here that you may also use a tty connection if you
desire. The Documentation directory of the isdn4linux subsystem offers good
documentation on this feature.

A) 10 steps to the establishment of a basic HDLC connection
-----------------------------------------------------------

- please open the isdn-hdlc file in the examples directory and follow along...
	
	This file is a script used to configure a BRI ISDN TA to establish a 
	basic HDLC connection between its two channels.  Two network 
	interfaces are created and two routes added between the channels.

	i)   using the isdnctrl utility, add an interface with "addif" and 
	     name it "isdn0"
	ii)  add the outgoing and inbound telephone numbers
	iii) set the Layer 2 protocol to hdlc
	iv)  set the eaz of the interface to be the phone number of that 
	     specific channel
	v)   to turn the callback features off, set the callback to "off" and
	     the callback delay (cbdelay) to 0.
	vi)  the hangup timeout can be set to a specified number of seconds
	vii) the hangup upon incoming call can be set on or off 
	viii) use the ifconfig command to bring up the network interface with 
	      a specific IP address and point to point address
	ix)  add a route to the IP address through the isdn0 interface
	x)   a ping should result in the establishment of the connection

	
B) Establishment of a PPP connection
------------------------------------

- please open the isdn-ppp file in the examples directory and follow along...
	
	This file is a script used to configure a BRI ISDN TA to establish a 
	PPP connection 	between the two channels.  The file is almost 
	identical to the HDLC connection example except that the packet 
	encapsulation type has to be set.
	
	use the same procedure as in the HDLC connection from steps i) to 
	iii) then, after the Layer 2 protocol is set, set the encapsulation 
	"encap" to syncppp. With this done, the rest of the steps, iv) to x) 
	can be followed from above.

	Then, the ipppd (ippp daemon) must be setup:
	
	xi)   use the ipppd function found in /sbin/ipppd to set the following:
	xii)  take out (minus) VJ compression and bsd compression
	xiii) set the mru size to 2000
	xiv)  link the two /dev interfaces to the daemon

NOTE:  A "*" in the inbound telephone number specifies that a call can be 
accepted on any number.

C) Establishment of a MLPPP connection
--------------------------------------

- please open the isdn-mppp file in the examples directory and follow along...
	
	This file is a script used to configure a BRI ISDN TA to accept a 
	Multi Link PPP connection. 
	
	i)   using the isdnctrl utility, add an interface with "addif" and 
	     name it "ippp0"
	ii)  add the inbound telephone number
	iii) set the Layer 2 protocol to hdlc and the Layer 3 protocol to 
	     trans (transparent)
	iv)  set the packet encapsulation to syncppp
	v)   set the eaz of the interface to be the phone number of that 
	     specific channel
	vi)  to turn the callback features off, set the callback to "off" and
	     the callback delay (cbdelay) to 0.
	vi)  the hangup timeout can be set to a specified number of seconds
	vii) the hangup upon incoming call can be set on or off 
	viii) add a slave interface and name it "ippp32" for example
	ix)  set the similar parameters for the ippp32 interface
	x)   use the ifconfig command to bring-up the ippp0 interface with a 
	     specific IP address and point to point address
	xi)  add a route to the IP address through the ippp0 interface
	xii) use the ipppd function found in /sbin/ipppd to set the following:
	xiii) take out (minus) bsd compression
	xiv) set the mru size to 2000
	xv)  add (+) the multi-link function "+mp"
	xvi)  link the two /dev interfaces to the daemon

NOTE:  To use the MLPPP connection to dial OUT to a MLPPP connection, change 
the inbound telephone numbers to the outgoing telephone numbers of the MLPPP 
host.

	
3. Beta Change Summaries and Miscellaneous Notes
------------------------------------------------
When using the "scctrl" utility to upload firmware revisions on the board,
please note that the byte count displayed at the end of the operation may be
different from the total number of bytes in the "dcbfwn.nn.sr" file. Please
disregard the displayed byte count.

It was noted that in Beta Release 1, the module would fail to load and result
in a segmentation fault when 'insmod'ed. This problem was created when one of
the isdn4linux parameters, (isdn_ctrl, data field) was filled in. In some
cases, this data field was NULL, and was left unchecked, so when it was
referenced... segv. The bug has been fixed around line 63-68 of event.c.

