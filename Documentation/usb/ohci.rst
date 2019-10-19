====
OHCI
====

23-Aug-2002

The "ohci-hcd" driver is a USB Host Controller Driver (HCD) that is derived
from the "usb-ohci" driver from the 2.4 kernel series.  The "usb-ohci" code
was written primarily by Roman Weissgaerber <weissg@vienna.at> but with
contributions from many others (read its copyright/licencing header).

It supports the "Open Host Controller Interface" (OHCI), which standardizes
hardware register protocols used to talk to USB 1.1 host controllers.  As
compared to the earlier "Universal Host Controller Interface" (UHCI) from
Intel, it pushes more intelligence into the hardware.  USB 1.1 controllers
from vendors other than Intel and VIA generally use OHCI.

Changes since the 2.4 kernel include

	- improved robustness; bugfixes; and less overhead
	- supports the updated and simplified usbcore APIs
	- interrupt transfers can be larger, and can be queued
	- less code, by using the upper level "hcd" framework
	- supports some non-PCI implementations of OHCI
	- ... more

The "ohci-hcd" driver handles all USB 1.1 transfer types.  Transfers of all
types can be queued.  That was also true in "usb-ohci", except for interrupt
transfers.  Previously, using periods of one frame would risk data loss due
to overhead in IRQ processing.  When interrupt transfers are queued, those
risks can be minimized by making sure the hardware always has transfers to
work on while the OS is getting around to the relevant IRQ processing.

- David Brownell
  <dbrownell@users.sourceforge.net>
