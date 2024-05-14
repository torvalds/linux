.. SPDX-License-Identifier: GPL-2.0

================
SMC 9xxxx Driver
================

Revision 0.12

3/5/96

Copyright 1996  Erik Stahlman

Released under terms of the GNU General Public License.

This file contains the instructions and caveats for my SMC9xxx driver.  You
should not be using the driver without reading this file.

Things to note about installation:

  1. The driver should work on all kernels from 1.2.13 until 1.3.71.
     (A kernel patch is supplied for 1.3.71 )

  2. If you include this into the kernel, you might need to change some
     options, such as for forcing IRQ.


  3.  To compile as a module, run 'make'.
      Make will give you the appropriate options for various kernel support.

  4.  Loading the driver as a module::

	use:   insmod smc9194.o
	optional parameters:
		io=xxxx    : your base address
		irq=xx	   : your irq
		ifport=x   :	0 for whatever is default
				1 for twisted pair
				2 for AUI  ( or BNC on some cards )

How to obtain the latest version?

FTP:
	ftp://fenris.campus.vt.edu/smc9/smc9-12.tar.gz
	ftp://sfbox.vt.edu/filebox/F/fenris/smc9/smc9-12.tar.gz


Contacting me:
    erik@mail.vt.edu
