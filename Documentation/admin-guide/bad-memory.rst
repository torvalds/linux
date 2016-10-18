How to deal with bad memory e.g. reported by memtest86+ ?
=========================================================

March 2008
Jan-Simon Moeller, dl9pf@gmx.de



There are three possibilities I know of:

1) Reinsert/swap the memory modules

2) Buy new modules (best!) or try to exchange the memory
   if you have spare-parts

3) Use BadRAM or memmap

This Howto is about number 3) .


BadRAM
######

BadRAM is the actively developed and available as kernel-patch
here:  http://rick.vanrein.org/linux/badram/

For more details see the BadRAM documentation.

memmap
######

memmap is already in the kernel and usable as kernel-parameter at
boot-time.  Its syntax is slightly strange and you may need to
calculate the values by yourself!

Syntax to exclude a memory area (see admin-guide/kernel-parameters.rst for details)::

	memmap=<size>$<address>

Example: memtest86+ reported here errors at address 0x18691458, 0x18698424 and
some others. All had 0x1869xxxx in common, so I chose a pattern of
0x18690000,0xffff0000.

With the numbers of the example above::

	memmap=64K$0x18690000

or::

	memmap=0x10000$0x18690000
