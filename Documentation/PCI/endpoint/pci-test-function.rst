.. SPDX-License-Identifier: GPL-2.0

=================
PCI Test Function
=================

:Author: Kishon Vijay Abraham I <kishon@ti.com>

Traditionally PCI RC has always been validated by using standard
PCI cards like ethernet PCI cards or USB PCI cards or SATA PCI cards.
However with the addition of EP-core in linux kernel, it is possible
to configure a PCI controller that can operate in EP mode to work as
a test device.

The PCI endpoint test device is a virtual device (defined in software)
used to test the endpoint functionality and serve as a sample driver
for other PCI endpoint devices (to use the EP framework).

The PCI endpoint test device has the following registers:

	1) PCI_ENDPOINT_TEST_MAGIC
	2) PCI_ENDPOINT_TEST_COMMAND
	3) PCI_ENDPOINT_TEST_STATUS
	4) PCI_ENDPOINT_TEST_SRC_ADDR
	5) PCI_ENDPOINT_TEST_DST_ADDR
	6) PCI_ENDPOINT_TEST_SIZE
	7) PCI_ENDPOINT_TEST_CHECKSUM
	8) PCI_ENDPOINT_TEST_IRQ_TYPE
	9) PCI_ENDPOINT_TEST_IRQ_NUMBER

* PCI_ENDPOINT_TEST_MAGIC

This register will be used to test BAR0. A known pattern will be written
and read back from MAGIC register to verify BAR0.

* PCI_ENDPOINT_TEST_COMMAND

This register will be used by the host driver to indicate the function
that the endpoint device must perform.

========	================================================================
Bitfield	Description
========	================================================================
Bit 0		raise legacy IRQ
Bit 1		raise MSI IRQ
Bit 2		raise MSI-X IRQ
Bit 3		read command (read data from RC buffer)
Bit 4		write command (write data to RC buffer)
Bit 5		copy command (copy data from one RC buffer to another RC buffer)
========	================================================================

* PCI_ENDPOINT_TEST_STATUS

This register reflects the status of the PCI endpoint device.

========	==============================
Bitfield	Description
========	==============================
Bit 0		read success
Bit 1		read fail
Bit 2		write success
Bit 3		write fail
Bit 4		copy success
Bit 5		copy fail
Bit 6		IRQ raised
Bit 7		source address is invalid
Bit 8		destination address is invalid
========	==============================

* PCI_ENDPOINT_TEST_SRC_ADDR

This register contains the source address (RC buffer address) for the
COPY/READ command.

* PCI_ENDPOINT_TEST_DST_ADDR

This register contains the destination address (RC buffer address) for
the COPY/WRITE command.

* PCI_ENDPOINT_TEST_IRQ_TYPE

This register contains the interrupt type (Legacy/MSI) triggered
for the READ/WRITE/COPY and raise IRQ (Legacy/MSI) commands.

Possible types:

======	==
Legacy	0
MSI	1
MSI-X	2
======	==

* PCI_ENDPOINT_TEST_IRQ_NUMBER

This register contains the triggered ID interrupt.

Admissible values:

======	===========
Legacy	0
MSI	[1 .. 32]
MSI-X	[1 .. 2048]
======	===========
