.. SPDX-License-Identifier: GPL-2.0

PCI drivers
===========

The PCI boards are identified by an identification called PCI ID. The PCI ID
is actually composed by two parts:

	- Vendor ID and device ID;
	- Subsystem ID and Subsystem device ID;

The ``lspci -nn`` command allows identifying the vendor/device PCI IDs:

.. code-block:: none
   :emphasize-lines: 3

    $ lspci -nn
    ...
    00:0a.0 Multimedia controller [0480]: Philips Semiconductors SAA7131/SAA7133/SAA7135 Video Broadcast Decoder [1131:7133] (rev d1)
    00:0b.0 Multimedia controller [0480]: Brooktree Corporation Bt878 Audio Capture [109e:0878] (rev 11)
    01:00.0 Multimedia video controller [0400]: Conexant Systems, Inc. CX23887/8 PCIe Broadcast Audio and Video Decoder with 3D Comb [14f1:8880] (rev 0f)
    02:01.0 Multimedia video controller [0400]: Internext Compression Inc iTVC15 (CX23415) Video Decoder [4444:0803] (rev 01)
    02:02.0 Multimedia video controller [0400]: Conexant Systems, Inc. CX23418 Single-Chip MPEG-2 Encoder with Integrated Analog Video/Broadcast Audio Decoder [14f1:5b7a]
    02:03.0 Multimedia video controller [0400]: Brooktree Corporation Bt878 Video Capture [109e:036e] (rev 11)
    ...

The subsystem IDs can be obtained using ``lspci -vn``

.. code-block:: none
   :emphasize-lines: 4

    $ lspci -vn
    ...
	00:0a.0 0480: 1131:7133 (rev d1)
		Subsystem: 1461:f01d
		Flags: bus master, medium devsel, latency 32, IRQ 209
		Memory at e2002000 (32-bit, non-prefetchable) [size=2K]
		Capabilities: [40] Power Management version 2
    ...

At the above example, the first card uses the ``saa7134`` driver, and
has a vendor/device PCI ID equal to ``1131:7133`` and a PCI subsystem
ID equal to ``1461:f01d`` (see :doc:`Saa7134 card list<saa7134-cardlist>`).

Unfortunately, sometimes the same PCI subsystem ID is used by different
products. So, several media drivers allow passing a ``card=`` parameter,
in order to setup a card number that would match the correct settings for
an specific board.

The current supported PCI/PCIe cards (not including staging drivers) are
listed below\ [#]_.

.. [#] some of the drivers have sub-drivers, not shown at this table

================  ========================================================
Driver            Name
================  ========================================================
altera-ci         Altera FPGA based CI module
b2c2-flexcop-pci  Technisat/B2C2 Air/Sky/Cable2PC PCI
bt878             DVB/ATSC Support for bt878 based TV cards
bttv              BT8x8 Video For Linux
cobalt            Cisco Cobalt
cx18              Conexant cx23418 MPEG encoder
cx23885           Conexant cx23885 (2388x successor)
cx25821           Conexant cx25821
cx88xx            Conexant 2388x (bt878 successor)
ddbridge          Digital Devices bridge
dm1105            SDMC DM1105 based PCI cards
dt3155            DT3155 frame grabber
dvb-ttpci         AV7110 cards
earth-pt1         PT1 cards
earth-pt3         Earthsoft PT3 cards
hexium_gemini     Hexium Gemini frame grabber
hexium_orion      Hexium HV-PCI6 and Orion frame grabber
hopper            HOPPER based cards
ipu3-cio2         Intel ipu3-cio2 driver
ivtv              Conexant cx23416/cx23415 MPEG encoder/decoder
ivtvfb            Conexant cx23415 framebuffer
mantis            MANTIS based cards
meye              Sony Vaio Picturebook Motion Eye
mxb               Siemens-Nixdorf 'Multimedia eXtension Board'
netup-unidvb      NetUP Universal DVB card
ngene             Micronas nGene
pluto2            Pluto2 cards
saa7134           Philips SAA7134
saa7164           NXP SAA7164
smipcie           SMI PCIe DVBSky cards
solo6x10          Bluecherry / Softlogic 6x10 capture cards (MPEG-4/H.264)
sta2x11_vip       STA2X11 VIP Video For Linux
tw5864            Techwell TW5864 video/audio grabber and encoder
tw686x            Intersil/Techwell TW686x
tw68              Techwell tw68x Video For Linux
zoran             Zoran-36057/36067 JPEG codec
================  ========================================================

Some of those drivers support multiple devices, as shown at the card
lists below:

.. toctree::
	:maxdepth: 1

	bttv-cardlist
	cx18-cardlist
	cx23885-cardlist
	cx88-cardlist
	ivtv-cardlist
	saa7134-cardlist
	saa7164-cardlist
	zoran-cardlist
