.. SPDX-License-Identifier: GPL-2.0

=================================
Qlogic FASXXX Family Driver Notes
=================================

This driver supports the Qlogic FASXXX family of chips.  This driver
only works with the ISA, VLB, and PCMCIA versions of the Qlogic
FastSCSI!  cards as well as any other card based on the FASXX chip
(including the Control Concepts SCSI/IDE/SIO/PIO/FDC cards).

This driver does NOT support the PCI version.  Support for these PCI
Qlogic boards:

	* IQ-PCI
	* IQ-PCI-10
	* IQ-PCI-D

is provided by the qla1280 driver.

Nor does it support the PCI-Basic, which is supported by the
'am53c974' driver.

PCMCIA Support
==============

This currently only works if the card is enabled first from DOS.  This
means you will have to load your socket and card services, and
QL41DOS.SYS and QL40ENBL.SYS.  These are a minimum, but loading the
rest of the modules won't interfere with the operation.  The next
thing to do is load the kernel without resetting the hardware, which
can be a simple ctrl-alt-delete with a boot floppy, or by using
loadlin with the kernel image accessible from DOS.  If you are using
the Linux PCMCIA driver, you will have to adjust it or otherwise stop
it from configuring the card.

I am working with the PCMCIA group to make it more flexible, but that
may take a while.

All Cards
=========

The top of the qlogic.c file has a number of defines that controls
configuration.  As shipped, it provides a balance between speed and
function.  If there are any problems, try setting SLOW_CABLE to 1, and
then try changing USE_IRQ and TURBO_PDMA to zero.  If you are familiar
with SCSI, there are other settings which can tune the bus.

It may be a good idea to enable RESET_AT_START, especially if the
devices may not have been just powered up, or if you are restarting
after a crash, since they may be busy trying to complete the last
command or something.  It comes up faster if this is set to zero, and
if you have reliable hardware and connections it may be more useful to
not reset things.

Some Troubleshooting Tips
=========================

Make sure it works properly under DOS.  You should also do an initial FDISK
on a new drive if you want partitions.

Don't enable all the speedups first.  If anything is wrong, they will make
any problem worse.

Important
=========

The best way to test if your cables, termination, etc. are good is to
copy a very big file (e.g. a doublespace container file, or a very
large executable or archive).  It should be at least 5 megabytes, but
you can do multiple tests on smaller files.  Then do a COMP to verify
that the file copied properly.  (Turn off all caching when doing these
tests, otherwise you will test your RAM and not the files).  Then do
10 COMPs, comparing the same file on the SCSI hard drive, i.e. "COMP
realbig.doc realbig.doc".  Then do it after the computer gets warm.

I noticed my system which seems to work 100% would fail this test if
the computer was left on for a few hours.  It was worse with longer
cables, and more devices on the SCSI bus.  What seems to happen is
that it gets a false ACK causing an extra byte to be inserted into the
stream (and this is not detected).  This can be caused by bad
termination (the ACK can be reflected), or by noise when the chips
work less well because of the heat, or when cables get too long for
the speed.

Remember, if it doesn't work under DOS, it probably won't work under
Linux.
