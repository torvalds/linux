.. SPDX-License-Identifier: GPL-2.0

=====================================================
Notes on the DEC FDDIcontroller 700 (DEFZA-xx) driver
=====================================================

:Version: v.1.1.4


DEC FDDIcontroller 700 is DEC's first-generation TURBOchannel FDDI
network card, designed in 1990 specifically for the DECstation 5000
model 200 workstation.  The board is a single attachment station and
it was manufactured in two variations, both of which are supported.

First is the SAS MMF DEFZA-AA option, the original design implementing
the standard MMF-PMD, however with a pair of ST connectors rather than
the usual MIC connector.  The other one is the SAS ThinWire/STP DEFZA-CA
option, denoted 700-C, with the network medium selectable by a switch
between the DEC proprietary ThinWire-PMD using a BNC connector and the
standard STP-PMD using a DE-9F connector.  This option can interface to
a DECconcentrator 500 device and, in the case of the STP-PMD, also other
FDDI equipment and was designed to make it easier to transition from
existing IEEE 802.3 10BASE2 Ethernet and IEEE 802.5 Token Ring networks
by providing means to reuse existing cabling.

This driver handles any number of cards installed in a single system.
They get fddi0, fddi1, etc. interface names assigned in the order of
increasing TURBOchannel slot numbers.

The board only supports DMA on the receive side.  Transmission involves
the use of PIO.  As a result under a heavy transmission load there will
be a significant impact on system performance.

The board supports a 64-entry CAM for matching destination addresses.
Two entries are preoccupied by the Directed Beacon and Ring Purger
multicast addresses and the rest is used as a multicast filter.  An
all-multi mode is also supported for LLC frames and it is used if
requested explicitly or if the CAM overflows.  The promiscuous mode
supports separate enables for LLC and SMT frames, but this driver
doesn't support changing them individually.


Known problems:

None.


To do:

5. MAC address change.  The card does not support changing the Media
   Access Controller's address registers but a similar effect can be
   achieved by adding an alias to the CAM.  There is no way to disable
   matching against the original address though.

7. Queueing incoming/outgoing SMT frames in the driver if the SMT
   receive/RMC transmit ring is full. (?)

8. Retrieving/reporting FDDI/SNMP stats.


Both success and failure reports are welcome.

Maciej W. Rozycki  <macro@linux-mips.org>
