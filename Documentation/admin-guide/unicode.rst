Unicode support
===============

		 Last update: 2005-01-17, version 1.4

This file is maintained by H. Peter Anvin <unicode@lanana.org> as part
of the Linux Assigned Names And Numbers Authority (LANANA) project.
The current version can be found at:

	    http://www.lanana.org/docs/unicode/admin-guide/unicode.rst

Introduction
------------

The Linux kernel code has been rewritten to use Unicode to map
characters to fonts.  By downloading a single Unicode-to-font table,
both the eight-bit character sets and UTF-8 mode are changed to use
the font as indicated.

This changes the semantics of the eight-bit character tables subtly.
The four character tables are now:

=============== =============================== ================
Map symbol	Map name			Escape code (G0)
=============== =============================== ================
LAT1_MAP	Latin-1 (ISO 8859-1)		ESC ( B
GRAF_MAP	DEC VT100 pseudographics	ESC ( 0
IBMPC_MAP	IBM code page 437		ESC ( U
USER_MAP	User defined			ESC ( K
=============== =============================== ================

In particular, ESC ( U is no longer "straight to font", since the font
might be completely different than the IBM character set.  This
permits for example the use of block graphics even with a Latin-1 font
loaded.

Note that although these codes are similar to ISO 2022, neither the
codes nor their uses match ISO 2022; Linux has two 8-bit codes (G0 and
G1), whereas ISO 2022 has four 7-bit codes (G0-G3).

In accordance with the Unicode standard/ISO 10646 the range U+F000 to
U+F8FF has been reserved for OS-wide allocation (the Unicode Standard
refers to this as a "Corporate Zone", since this is inaccurate for
Linux we call it the "Linux Zone").  U+F000 was picked as the starting
point since it lets the direct-mapping area start on a large power of
two (in case 1024- or 2048-character fonts ever become necessary).
This leaves U+E000 to U+EFFF as End User Zone.

[v1.2]: The Unicodes range from U+F000 and up to U+F7FF have been
hard-coded to map directly to the loaded font, bypassing the
translation table.  The user-defined map now defaults to U+F000 to
U+F0FF, emulating the previous behaviour.  In practice, this range
might be shorter; for example, vgacon can only handle 256-character
(U+F000..U+F0FF) or 512-character (U+F000..U+F1FF) fonts.


Actual characters assigned in the Linux Zone
--------------------------------------------

In addition, the following characters not present in Unicode 1.1.4
have been defined; these are used by the DEC VT graphics map.  [v1.2]
THIS USE IS OBSOLETE AND SHOULD NO LONGER BE USED; PLEASE SEE BELOW.

====== ======================================
U+F800 DEC VT GRAPHICS HORIZONTAL LINE SCAN 1
U+F801 DEC VT GRAPHICS HORIZONTAL LINE SCAN 3
U+F803 DEC VT GRAPHICS HORIZONTAL LINE SCAN 7
U+F804 DEC VT GRAPHICS HORIZONTAL LINE SCAN 9
====== ======================================

The DEC VT220 uses a 6x10 character matrix, and these characters form
a smooth progression in the DEC VT graphics character set.  I have
omitted the scan 5 line, since it is also used as a block-graphics
character, and hence has been coded as U+2500 FORMS LIGHT HORIZONTAL.

[v1.3]: These characters have been officially added to Unicode 3.2.0;
they are added at U+23BA, U+23BB, U+23BC, U+23BD.  Linux now uses the
new values.

[v1.2]: The following characters have been added to represent common
keyboard symbols that are unlikely to ever be added to Unicode proper
since they are horribly vendor-specific.  This, of course, is an
excellent example of horrible design.

====== ======================================
U+F810 KEYBOARD SYMBOL FLYING FLAG
U+F811 KEYBOARD SYMBOL PULLDOWN MENU
U+F812 KEYBOARD SYMBOL OPEN APPLE
U+F813 KEYBOARD SYMBOL SOLID APPLE
====== ======================================

Klingon language support
------------------------

In 1996, Linux was the first operating system in the world to add
support for the artificial language Klingon, created by Marc Okrand
for the "Star Trek" television series.	This encoding was later
adopted by the ConScript Unicode Registry and proposed (but ultimately
rejected) for inclusion in Unicode Plane 1.  Thus, it remains as a
Linux/CSUR private assignment in the Linux Zone.

This encoding has been endorsed by the Klingon Language Institute.
For more information, contact them at:

	http://www.kli.org/

Since the characters in the beginning of the Linux CZ have been more
of the dingbats/symbols/forms type and this is a language, I have
located it at the end, on a 16-cell boundary in keeping with standard
Unicode practice.

.. note::

  This range is now officially managed by the ConScript Unicode
  Registry.  The normative reference is at:

	http://www.evertype.com/standards/csur/klingon.html

Klingon has an alphabet of 26 characters, a positional numeric writing
system with 10 digits, and is written left-to-right, top-to-bottom.

Several glyph forms for the Klingon alphabet have been proposed.
However, since the set of symbols appear to be consistent throughout,
with only the actual shapes being different, in keeping with standard
Unicode practice these differences are considered font variants.

======	=======================================================
U+F8D0	KLINGON LETTER A
U+F8D1	KLINGON LETTER B
U+F8D2	KLINGON LETTER CH
U+F8D3	KLINGON LETTER D
U+F8D4	KLINGON LETTER E
U+F8D5	KLINGON LETTER GH
U+F8D6	KLINGON LETTER H
U+F8D7	KLINGON LETTER I
U+F8D8	KLINGON LETTER J
U+F8D9	KLINGON LETTER L
U+F8DA	KLINGON LETTER M
U+F8DB	KLINGON LETTER N
U+F8DC	KLINGON LETTER NG
U+F8DD	KLINGON LETTER O
U+F8DE	KLINGON LETTER P
U+F8DF	KLINGON LETTER Q
	- Written <q> in standard Okrand Latin transliteration
U+F8E0	KLINGON LETTER QH
	- Written <Q> in standard Okrand Latin transliteration
U+F8E1	KLINGON LETTER R
U+F8E2	KLINGON LETTER S
U+F8E3	KLINGON LETTER T
U+F8E4	KLINGON LETTER TLH
U+F8E5	KLINGON LETTER U
U+F8E6	KLINGON LETTER V
U+F8E7	KLINGON LETTER W
U+F8E8	KLINGON LETTER Y
U+F8E9	KLINGON LETTER GLOTTAL STOP

U+F8F0	KLINGON DIGIT ZERO
U+F8F1	KLINGON DIGIT ONE
U+F8F2	KLINGON DIGIT TWO
U+F8F3	KLINGON DIGIT THREE
U+F8F4	KLINGON DIGIT FOUR
U+F8F5	KLINGON DIGIT FIVE
U+F8F6	KLINGON DIGIT SIX
U+F8F7	KLINGON DIGIT SEVEN
U+F8F8	KLINGON DIGIT EIGHT
U+F8F9	KLINGON DIGIT NINE

U+F8FD	KLINGON COMMA
U+F8FE	KLINGON FULL STOP
U+F8FF	KLINGON SYMBOL FOR EMPIRE
======	=======================================================

Other Fictional and Artificial Scripts
--------------------------------------

Since the assignment of the Klingon Linux Unicode block, a registry of
fictional and artificial scripts has been established by John Cowan
<jcowan@reutershealth.com> and Michael Everson <everson@evertype.com>.
The ConScript Unicode Registry is accessible at:

	  http://www.evertype.com/standards/csur/

The ranges used fall at the low end of the End User Zone and can hence
not be normatively assigned, but it is recommended that people who
wish to encode fictional scripts use these codes, in the interest of
interoperability.  For Klingon, CSUR has adopted the Linux encoding.
The CSUR people are driving adding Tengwar and Cirth into Unicode
Plane 1; the addition of Klingon to Unicode Plane 1 has been rejected
and so the above encoding remains official.
