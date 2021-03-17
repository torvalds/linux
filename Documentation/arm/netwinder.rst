================================
NetWinder specific documentation
================================

The NetWinder is a small low-power computer, primarily designed
to run Linux.  It is based around the StrongARM RISC processor,
DC21285 PCI bridge, with PC-type hardware glued around it.

Port usage
==========

=======  ====== ===============================
Min      Max	Description
=======  ====== ===============================
0x0000   0x000f	DMA1
0x0020   0x0021	PIC1
0x0060   0x006f	Keyboard
0x0070   0x007f	RTC
0x0080   0x0087	DMA1
0x0088   0x008f	DMA2
0x00a0   0x00a3	PIC2
0x00c0   0x00df	DMA2
0x0180   0x0187	IRDA
0x01f0   0x01f6	ide0
0x0201		Game port
0x0203		RWA010 configuration read
0x0220   ?	SoundBlaster
0x0250   ?	WaveArtist
0x0279		RWA010 configuration index
0x02f8   0x02ff	Serial ttyS1
0x0300   0x031f	Ether10
0x0338		GPIO1
0x033a		GPIO2
0x0370   0x0371	W83977F configuration registers
0x0388   ?	AdLib
0x03c0   0x03df	VGA
0x03f6		ide0
0x03f8   0x03ff	Serial ttyS0
0x0400   0x0408	DC21143
0x0480   0x0487	DMA1
0x0488   0x048f	DMA2
0x0a79		RWA010 configuration write
0xe800   0xe80f	ide0/ide1 BM DMA
=======  ====== ===============================


Interrupt usage
===============

======= ======= ========================
IRQ	type	Description
======= ======= ========================
 0	ISA	100Hz timer
 1	ISA	Keyboard
 2	ISA	cascade
 3	ISA	Serial ttyS1
 4	ISA	Serial ttyS0
 5	ISA	PS/2 mouse
 6	ISA	IRDA
 7	ISA	Printer
 8	ISA	RTC alarm
 9	ISA
10	ISA	GP10 (Orange reset button)
11	ISA
12	ISA	WaveArtist
13	ISA
14	ISA	hda1
15	ISA
======= ======= ========================

DMA usage
=========

======= ======= ===========
DMA	type	Description
======= ======= ===========
 0	ISA	IRDA
 1	ISA
 2	ISA	cascade
 3	ISA	WaveArtist
 4	ISA
 5	ISA
 6	ISA
 7	ISA	WaveArtist
======= ======= ===========
