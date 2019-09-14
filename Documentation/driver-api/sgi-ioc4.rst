====================================
SGI IOC4 PCI (multi function) device
====================================

The SGI IOC4 PCI device is a bit of a strange beast, so some notes on
it are in order.

First, even though the IOC4 performs multiple functions, such as an
IDE controller, a serial controller, a PS/2 keyboard/mouse controller,
and an external interrupt mechanism, it's not implemented as a
multifunction device.  The consequence of this from a software
standpoint is that all these functions share a single IRQ, and
they can't all register to own the same PCI device ID.  To make
matters a bit worse, some of the register blocks (and even registers
themselves) present in IOC4 are mixed-purpose between these several
functions, meaning that there's no clear "owning" device driver.

The solution is to organize the IOC4 driver into several independent
drivers, "ioc4", "sgiioc4", and "ioc4_serial".  Note that there is no
PS/2 controller driver as this functionality has never been wired up
on a shipping IO card.

ioc4
====
This is the core (or shim) driver for IOC4.  It is responsible for
initializing the basic functionality of the chip, and allocating
the PCI resources that are shared between the IOC4 functions.

This driver also provides registration functions that the other
IOC4 drivers can call to make their presence known.  Each driver
needs to provide a probe and remove function, which are invoked
by the core driver at appropriate times.  The interface of these
IOC4 function probe and remove operations isn't precisely the same
as PCI device probe and remove operations, but is logically the
same operation.

sgiioc4
=======
This is the IDE driver for IOC4.  Its name isn't very descriptive
simply for historical reasons (it used to be the only IOC4 driver
component).  There's not much to say about it other than it hooks
up to the ioc4 driver via the appropriate registration, probe, and
remove functions.

ioc4_serial
===========
This is the serial driver for IOC4.  There's not much to say about it
other than it hooks up to the ioc4 driver via the appropriate registration,
probe, and remove functions.
