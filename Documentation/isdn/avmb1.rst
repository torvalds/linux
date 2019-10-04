================================
Driver for active AVM Controller
================================

The driver provides a kernel capi2.0 Interface (kernelcapi) and
on top of this a User-Level-CAPI2.0-interface (capi)
and a driver to connect isdn4linux with CAPI2.0 (capidrv).
The lowlevel interface can be used to implement a CAPI2.0
also for passive cards since July 1999.

The author can be reached at calle@calle.in-berlin.de.
The command avmcapictrl is part of the isdn4k-utils.
t4-files can be found at ftp://ftp.avm.de/cardware/b1/linux/firmware

Currently supported cards:

	- B1 ISA (all versions)
	- B1 PCI
	- T1/T1B (HEMA card)
	- M1
	- M2
	- B1 PCMCIA

Installing
----------

You need at least /dev/capi20 to load the firmware.

::

    mknod /dev/capi20 c 68 0
    mknod /dev/capi20.00 c 68 1
    mknod /dev/capi20.01 c 68 2
    .
    .
    .
    mknod /dev/capi20.19 c 68 20

Running
-------

To use the card you need the t4-files to download the firmware.
AVM GmbH provides several t4-files for the different D-channel
protocols (b1.t4 for Euro-ISDN). Install these file in /lib/isdn.

if you configure as modules load the modules this way::

    insmod /lib/modules/current/misc/capiutil.o
    insmod /lib/modules/current/misc/b1.o
    insmod /lib/modules/current/misc/kernelcapi.o
    insmod /lib/modules/current/misc/capidrv.o
    insmod /lib/modules/current/misc/capi.o

if you have an B1-PCI card load the module b1pci.o::

    insmod /lib/modules/current/misc/b1pci.o

and load the firmware with::

    avmcapictrl load /lib/isdn/b1.t4 1

if you have an B1-ISA card load the module b1isa.o
and add the card by calling::

    avmcapictrl add 0x150 15

and load the firmware by calling::

    avmcapictrl load /lib/isdn/b1.t4 1

if you have an T1-ISA card load the module t1isa.o
and add the card by calling::

    avmcapictrl add 0x450 15 T1 0

and load the firmware by calling::

    avmcapictrl load /lib/isdn/t1.t4 1

if you have an PCMCIA card (B1/M1/M2) load the module b1pcmcia.o
before you insert the card.

Leased Lines with B1
--------------------

Init card and load firmware.

For an D64S use "FV: 1" as phone number

For an D64S2 use "FV: 1" and "FV: 2" for multilink
or "FV: 1,2" to use CAPI channel bundling.

/proc-Interface
-----------------

/proc/capi::

  dr-xr-xr-x   2 root     root            0 Jul  1 14:03 .
  dr-xr-xr-x  82 root     root            0 Jun 30 19:08 ..
  -r--r--r--   1 root     root            0 Jul  1 14:03 applications
  -r--r--r--   1 root     root            0 Jul  1 14:03 applstats
  -r--r--r--   1 root     root            0 Jul  1 14:03 capi20
  -r--r--r--   1 root     root            0 Jul  1 14:03 capidrv
  -r--r--r--   1 root     root            0 Jul  1 14:03 controller
  -r--r--r--   1 root     root            0 Jul  1 14:03 contrstats
  -r--r--r--   1 root     root            0 Jul  1 14:03 driver
  -r--r--r--   1 root     root            0 Jul  1 14:03 ncci
  -r--r--r--   1 root     root            0 Jul  1 14:03 users

/proc/capi/applications:
   applid level3cnt datablkcnt datablklen ncci-cnt recvqueuelen
	level3cnt:
	    capi_register parameter
	datablkcnt:
	    capi_register parameter
	ncci-cnt:
	    current number of nccis (connections)
	recvqueuelen:
	    number of messages on receive queue

   for example::

	1 -2 16 2048 1 0
	2 2 7 2048 1 0

/proc/capi/applstats:
   applid recvctlmsg nrecvdatamsg nsentctlmsg nsentdatamsg
	recvctlmsg:
	    capi messages received without DATA_B3_IND
	recvdatamsg:
	    capi DATA_B3_IND received
	sentctlmsg:
	    capi messages sent without DATA_B3_REQ
	sentdatamsg:
	    capi DATA_B3_REQ sent

   for example::

	1 2057 1699 1721 1699

/proc/capi/capi20: statistics of capi.o (/dev/capi20)
    minor nopen nrecvdropmsg nrecvctlmsg nrecvdatamsg sentctlmsg sentdatamsg
	minor:
	    minor device number of capi device
	nopen:
	    number of calls to devices open
	nrecvdropmsg:
	    capi messages dropped (messages in recvqueue in close)
	nrecvctlmsg:
	    capi messages received without DATA_B3_IND
	nrecvdatamsg:
	    capi DATA_B3_IND received
	nsentctlmsg:
	    capi messages sent without DATA_B3_REQ
	nsentdatamsg:
	    capi DATA_B3_REQ sent

   for example::

	1 2 18 0 16 2

/proc/capi/capidrv: statistics of capidrv.o (capi messages)
    nrecvctlmsg nrecvdatamsg sentctlmsg sentdatamsg
	nrecvctlmsg:
	    capi messages received without DATA_B3_IND
	nrecvdatamsg:
	    capi DATA_B3_IND received
	nsentctlmsg:
	    capi messages sent without DATA_B3_REQ
	nsentdatamsg:
	    capi DATA_B3_REQ sent

   for example:
	2780 2226 2256 2226

/proc/capi/controller:
   controller drivername state cardname   controllerinfo

   for example::

	1 b1pci      running  b1pci-e000       B1 3.07-01 0xe000 19
	2 t1isa      running  t1isa-450        B1 3.07-01 0x450 11 0
	3 b1pcmcia   running  m2-150           B1 3.07-01 0x150 5

/proc/capi/contrstats:
    controller nrecvctlmsg nrecvdatamsg sentctlmsg sentdatamsg
	nrecvctlmsg:
	    capi messages received without DATA_B3_IND
	nrecvdatamsg:
	    capi DATA_B3_IND received
	nsentctlmsg:
	    capi messages sent without DATA_B3_REQ
	nsentdatamsg:
	    capi DATA_B3_REQ sent

   for example::

	1 2845 2272 2310 2274
	2 2 0 2 0
	3 2 0 2 0

/proc/capi/driver:
   drivername ncontroller

   for example::

	b1pci                            1
	t1isa                            1
	b1pcmcia                         1
	b1isa                            0

/proc/capi/ncci:
   apllid ncci winsize sendwindow

   for example::

	1 0x10101 8 0

/proc/capi/users: kernelmodules that use the kernelcapi.
   name

   for example::

	capidrv
	capi20

Questions
---------

Check out the FAQ (ftp.isdn4linux.de) or subscribe to the
linux-avmb1@calle.in-berlin.de mailing list by sending
a mail to majordomo@calle.in-berlin.de with
subscribe linux-avmb1
in the body.

German documentation and several scripts can be found at
ftp://ftp.avm.de/cardware/b1/linux/

Bugs
----

If you find any please let me know.

Enjoy,

Carsten Paeth (calle@calle.in-berlin.de)
