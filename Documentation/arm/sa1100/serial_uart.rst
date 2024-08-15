==================
SA1100 serial port
==================

The SA1100 serial port had its major/minor numbers officially assigned::

  > Date: Sun, 24 Sep 2000 21:40:27 -0700
  > From: H. Peter Anvin <hpa@transmeta.com>
  > To: Nicolas Pitre <nico@CAM.ORG>
  > Cc: Device List Maintainer <device@lanana.org>
  > Subject: Re: device
  >
  > Okay.  Note that device numbers 204 and 205 are used for "low density
  > serial devices", so you will have a range of minors on those majors (the
  > tty device layer handles this just fine, so you don't have to worry about
  > doing anything special.)
  >
  > So your assignments are:
  >
  > 204 char        Low-density serial ports
  >                   5 = /dev/ttySA0               SA1100 builtin serial port 0
  >                   6 = /dev/ttySA1               SA1100 builtin serial port 1
  >                   7 = /dev/ttySA2               SA1100 builtin serial port 2
  >
  > 205 char        Low-density serial ports (alternate device)
  >                   5 = /dev/cusa0                Callout device for ttySA0
  >                   6 = /dev/cusa1                Callout device for ttySA1
  >                   7 = /dev/cusa2                Callout device for ttySA2
  >

You must create those inodes in /dev on the root filesystem used
by your SA1100-based device::

	mknod ttySA0 c 204 5
	mknod ttySA1 c 204 6
	mknod ttySA2 c 204 7
	mknod cusa0 c 205 5
	mknod cusa1 c 205 6
	mknod cusa2 c 205 7

In addition to the creation of the appropriate device nodes above, you
must ensure your user space applications make use of the correct device
name. The classic example is the content of the /etc/inittab file where
you might have a getty process started on ttyS0.

In this case:

- replace occurrences of ttyS0 with ttySA0, ttyS1 with ttySA1, etc.

- don't forget to add 'ttySA0', 'console', or the appropriate tty name
  in /etc/securetty for root to be allowed to login as well.
