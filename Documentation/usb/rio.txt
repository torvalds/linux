============
Diamonds Rio
============

Copyright (C) 1999, 2000 Bruce Tenison

Portions Copyright (C) 1999, 2000 David Nelson

Thanks to David Nelson for guidance and the usage of the scanner.txt
and scanner.c files to model our driver and this informative file.

Mar. 2, 2000

Changes
=======

- Initial Revision


Overview
========

This README will address issues regarding how to configure the kernel
to access a RIO 500 mp3 player.
Before I explain how to use this to access the Rio500 please be warned:

.. warning::

   Please note that this software is still under development.  The authors
   are in no way responsible for any damage that may occur, no matter how
   inconsequential.

It seems that the Rio has a problem when sending .mp3 with low batteries.
I suggest when the batteries are low and you want to transfer stuff that you
replace it with a fresh one. In my case, what happened is I lost two 16kb
blocks (they are no longer usable to store information to it). But I don't
know if that's normal or not; it could simply be a problem with the flash
memory.

In an extreme case, I left my Rio playing overnight and the batteries wore
down to nothing and appear to have corrupted the flash memory. My RIO
needed to be replaced as a result.  Diamond tech support is aware of the
problem.  Do NOT allow your batteries to wear down to nothing before
changing them.  It appears RIO 500 firmware does not handle low battery
power well at all.

On systems with OHCI controllers, the kernel OHCI code appears to have
power on problems with some chipsets.  If you are having problems
connecting to your RIO 500, try turning it on first and then plugging it
into the USB cable.

Contact Information
-------------------

   The main page for the project is hosted at sourceforge.net in the following
   URL: <http://rio500.sourceforge.net>. You can also go to the project's
   sourceforge home page at: <http://sourceforge.net/projects/rio500/>.
   There is also a mailing list: rio500-users@lists.sourceforge.net

Authors
-------

Most of the code was written by Cesar Miquel <miquel@df.uba.ar>. Keith
Clayton <kclayton@jps.net> is incharge of the PPC port and making sure
things work there. Bruce Tenison <btenison@dibbs.net> is adding support
for .fon files and also does testing. The program will mostly sure be
re-written and Pete Ikusz along with the rest will re-design it. I would
also like to thank Tri Nguyen <tmn_3022000@hotmail.com> who provided use
with some important information regarding the communication with the Rio.

Additional Information and userspace tools

	http://rio500.sourceforge.net/


Requirements
============

A host with a USB port running a Linux kernel with RIO 500 support enabled.

The driver is a module called rio500, which should be automatically loaded
as you plug in your device. If that fails you can manually load it with

  modprobe rio500

Udev should automatically create a device node as soon as plug in your device.
If that fails, you can manually add a device for the USB rio500::

  mknod /dev/usb/rio500 c 180 64

In that case, set appropriate permissions for /dev/usb/rio500 (don't forget
about group and world permissions).  Both read and write permissions are
required for proper operation.

That's it.  The Rio500 Utils at: http://rio500.sourceforge.net should
be able to access the rio500.

Limits
======

You can use only a single rio500 device at a time with your computer.

Bugs
====

If you encounter any problems feel free to drop me an email.

Bruce Tenison
btenison@dibbs.net
