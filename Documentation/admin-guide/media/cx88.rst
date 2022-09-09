.. SPDX-License-Identifier: GPL-2.0

The cx88 driver
===============

Author:  Gerd Hoffmann

This is a v4l2 device driver for the cx2388x chip.


Current status
--------------

video
	- Works.
	- Overlay isn't supported.

audio
	- Works. The TV standard detection is made by the driver, as the
	  hardware has bugs to auto-detect.
	- audio data dma (i.e. recording without loopback cable to the
	  sound card) is supported via cx88-alsa.

vbi
	- Works.


How to add support for new cards
--------------------------------

The driver needs some config info for the TV cards.  This stuff is in
cx88-cards.c.  If the driver doesn't work well you likely need a new
entry for your card in that file.  Check the kernel log (using dmesg)
to see whenever the driver knows your card or not.  There is a line
like this one:

.. code-block:: none

	cx8800[0]: subsystem: 0070:3400, board: Hauppauge WinTV \
		34xxx models [card=1,autodetected]

If your card is listed as "board: UNKNOWN/GENERIC" it is unknown to
the driver.  What to do then?

1) Try upgrading to the latest snapshot, maybe it has been added
   meanwhile.
2) You can try to create a new entry yourself, have a look at
   cx88-cards.c.  If that worked, mail me your changes as unified
   diff ("diff -u").
3) Or you can mail me the config information.  We need at least the
   following information to add the card:

     - the PCI Subsystem ID ("0070:3400" from the line above,
       "lspci -v" output is fine too).
     - the tuner type used by the card.  You can try to find one by
       trial-and-error using the tuner=<n> insmod option.  If you
       know which one the card has you can also have a look at the
       list in CARDLIST.tuner
