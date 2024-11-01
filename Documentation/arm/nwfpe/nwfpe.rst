Introduction
============

This directory contains the version 0.92 test release of the NetWinder
Floating Point Emulator.

The majority of the code was written by me, Scott Bambrough It is
written in C, with a small number of routines in inline assembler
where required.  It was written quickly, with a goal of implementing a
working version of all the floating point instructions the compiler
emits as the first target.  I have attempted to be as optimal as
possible, but there remains much room for improvement.

I have attempted to make the emulator as portable as possible.  One of
the problems is with leading underscores on kernel symbols.  Elf
kernels have no leading underscores, a.out compiled kernels do.  I
have attempted to use the C_SYMBOL_NAME macro wherever this may be
important.

Another choice I made was in the file structure.  I have attempted to
contain all operating system specific code in one module (fpmodule.*).
All the other files contain emulator specific code.  This should allow
others to port the emulator to NetBSD for instance relatively easily.

The floating point operations are based on SoftFloat Release 2, by
John Hauser.  SoftFloat is a software implementation of floating-point
that conforms to the IEC/IEEE Standard for Binary Floating-point
Arithmetic.  As many as four formats are supported: single precision,
double precision, extended double precision, and quadruple precision.
All operations required by the standard are implemented, except for
conversions to and from decimal.  We use only the single precision,
double precision and extended double precision formats.  The port of
SoftFloat to the ARM was done by Phil Blundell, based on an earlier
port of SoftFloat version 1 by Neil Carson for NetBSD/arm32.

The file README.FPE contains a description of what has been implemented
so far in the emulator.  The file TODO contains a information on what
remains to be done, and other ideas for the emulator.

Bug reports, comments, suggestions should be directed to me at
<scottb@netwinder.org>.  General reports of "this program doesn't
work correctly when your emulator is installed" are useful for
determining that bugs still exist; but are virtually useless when
attempting to isolate the problem.  Please report them, but don't
expect quick action.  Bugs still exist.  The problem remains in isolating
which instruction contains the bug.  Small programs illustrating a specific
problem are a godsend.

Legal Notices
-------------

The NetWinder Floating Point Emulator is free software.  Everything Rebel.com
has written is provided under the GNU GPL.  See the file COPYING for copying
conditions.  Excluded from the above is the SoftFloat code.  John Hauser's
legal notice for SoftFloat is included below.

-------------------------------------------------------------------------------

SoftFloat Legal Notice

SoftFloat was written by John R. Hauser.  This work was made possible in
part by the International Computer Science Institute, located at Suite 600,
1947 Center Street, Berkeley, California 94704.  Funding was partially
provided by the National Science Foundation under grant MIP-9311980.  The
original version of this code was written as part of a project to build
a fixed-point vector processor in collaboration with the University of
California at Berkeley, overseen by Profs. Nelson Morgan and John Wawrzynek.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort
has been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT
TIMES RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO
PERSONS AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ANY
AND ALL LOSSES, COSTS, OR OTHER PROBLEMS ARISING FROM ITS USE.
-------------------------------------------------------------------------------
