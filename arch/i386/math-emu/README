 +---------------------------------------------------------------------------+
 |  wm-FPU-emu   an FPU emulator for 80386 and 80486SX microprocessors.      |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1995,1996,1997,1999                          |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail billm@melbpc.org.au              |
 |                                                                           |
 |    This program is free software; you can redistribute it and/or modify   |
 |    it under the terms of the GNU General Public License version 2 as      |
 |    published by the Free Software Foundation.                             |
 |                                                                           |
 |    This program is distributed in the hope that it will be useful,        |
 |    but WITHOUT ANY WARRANTY; without even the implied warranty of         |
 |    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
 |    GNU General Public License for more details.                           |
 |                                                                           |
 |    You should have received a copy of the GNU General Public License      |
 |    along with this program; if not, write to the Free Software            |
 |    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.              |
 |                                                                           |
 +---------------------------------------------------------------------------+



wm-FPU-emu is an FPU emulator for Linux. It is derived from wm-emu387
which was my 80387 emulator for early versions of djgpp (gcc under
msdos); wm-emu387 was in turn based upon emu387 which was written by
DJ Delorie for djgpp.  The interface to the Linux kernel is based upon
the original Linux math emulator by Linus Torvalds.

My target FPU for wm-FPU-emu is that described in the Intel486
Programmer's Reference Manual (1992 edition). Unfortunately, numerous
facets of the functioning of the FPU are not well covered in the
Reference Manual. The information in the manual has been supplemented
with measurements on real 80486's. Unfortunately, it is simply not
possible to be sure that all of the peculiarities of the 80486 have
been discovered, so there is always likely to be obscure differences
in the detailed behaviour of the emulator and a real 80486.

wm-FPU-emu does not implement all of the behaviour of the 80486 FPU,
but is very close.  See "Limitations" later in this file for a list of
some differences.

Please report bugs, etc to me at:
       billm@melbpc.org.au
or     b.metzenthen@medoto.unimelb.edu.au

For more information on the emulator and on floating point topics, see
my web pages, currently at  http://www.suburbia.net/~billm/


--Bill Metzenthen
  December 1999


----------------------- Internals of wm-FPU-emu -----------------------

Numeric algorithms:
(1) Add, subtract, and multiply. Nothing remarkable in these.
(2) Divide has been tuned to get reasonable performance. The algorithm
    is not the obvious one which most people seem to use, but is designed
    to take advantage of the characteristics of the 80386. I expect that
    it has been invented many times before I discovered it, but I have not
    seen it. It is based upon one of those ideas which one carries around
    for years without ever bothering to check it out.
(3) The sqrt function has been tuned to get good performance. It is based
    upon Newton's classic method. Performance was improved by capitalizing
    upon the properties of Newton's method, and the code is once again
    structured taking account of the 80386 characteristics.
(4) The trig, log, and exp functions are based in each case upon quasi-
    "optimal" polynomial approximations. My definition of "optimal" was
    based upon getting good accuracy with reasonable speed.
(5) The argument reducing code for the trig function effectively uses
    a value of pi which is accurate to more than 128 bits. As a consequence,
    the reduced argument is accurate to more than 64 bits for arguments up
    to a few pi, and accurate to more than 64 bits for most arguments,
    even for arguments approaching 2^63. This is far superior to an
    80486, which uses a value of pi which is accurate to 66 bits.

The code of the emulator is complicated slightly by the need to
account for a limited form of re-entrancy. Normally, the emulator will
emulate each FPU instruction to completion without interruption.
However, it may happen that when the emulator is accessing the user
memory space, swapping may be needed. In this case the emulator may be
temporarily suspended while disk i/o takes place. During this time
another process may use the emulator, thereby perhaps changing static
variables. The code which accesses user memory is confined to five
files:
    fpu_entry.c
    reg_ld_str.c
    load_store.c
    get_address.c
    errors.c
As from version 1.12 of the emulator, no static variables are used
(apart from those in the kernel's per-process tables). The emulator is
therefore now fully re-entrant, rather than having just the restricted
form of re-entrancy which is required by the Linux kernel.

----------------------- Limitations of wm-FPU-emu -----------------------

There are a number of differences between the current wm-FPU-emu
(version 2.01) and the 80486 FPU (apart from bugs).  The differences
are fewer than those which applied to the 1.xx series of the emulator.
Some of the more important differences are listed below:

The Roundup flag does not have much meaning for the transcendental
functions and its 80486 value with these functions is likely to differ
from its emulator value.

In a few rare cases the Underflow flag obtained with the emulator will
be different from that obtained with an 80486. This occurs when the
following conditions apply simultaneously:
(a) the operands have a higher precision than the current setting of the
    precision control (PC) flags.
(b) the underflow exception is masked.
(c) the magnitude of the exact result (before rounding) is less than 2^-16382.
(d) the magnitude of the final result (after rounding) is exactly 2^-16382.
(e) the magnitude of the exact result would be exactly 2^-16382 if the
    operands were rounded to the current precision before the arithmetic
    operation was performed.
If all of these apply, the emulator will set the Underflow flag but a real
80486 will not.

NOTE: Certain formats of Extended Real are UNSUPPORTED. They are
unsupported by the 80486. They are the Pseudo-NaNs, Pseudoinfinities,
and Unnormals. None of these will be generated by an 80486 or by the
emulator. Do not use them. The emulator treats them differently in
detail from the way an 80486 does.

Self modifying code can cause the emulator to fail. An example of such
code is:
          movl %esp,[%ebx]
	  fld1
The FPU instruction may be (usually will be) loaded into the pre-fetch
queue of the CPU before the mov instruction is executed. If the
destination of the 'movl' overlaps the FPU instruction then the bytes
in the prefetch queue and memory will be inconsistent when the FPU
instruction is executed. The emulator will be invoked but will not be
able to find the instruction which caused the device-not-present
exception. For this case, the emulator cannot emulate the behaviour of
an 80486DX.

Handling of the address size override prefix byte (0x67) has not been
extensively tested yet. A major problem exists because using it in
vm86 mode can cause a general protection fault. Address offsets
greater than 0xffff appear to be illegal in vm86 mode but are quite
acceptable (and work) in real mode. A small test program developed to
check the addressing, and which runs successfully in real mode,
crashes dosemu under Linux and also brings Windows down with a general
protection fault message when run under the MS-DOS prompt of Windows
3.1. (The program simply reads data from a valid address).

The emulator supports 16-bit protected mode, with one difference from
an 80486DX.  A 80486DX will allow some floating point instructions to
write a few bytes below the lowest address of the stack.  The emulator
will not allow this in 16-bit protected mode: no instructions are
allowed to write outside the bounds set by the protection.

----------------------- Performance of wm-FPU-emu -----------------------

Speed.
-----

The speed of floating point computation with the emulator will depend
upon instruction mix. Relative performance is best for the instructions
which require most computation. The simple instructions are adversely
affected by the FPU instruction trap overhead.


Timing: Some simple timing tests have been made on the emulator functions.
The times include load/store instructions. All times are in microseconds
measured on a 33MHz 386 with 64k cache. The Turbo C tests were under
ms-dos, the next two columns are for emulators running with the djgpp
ms-dos extender. The final column is for wm-FPU-emu in Linux 0.97,
using libm4.0 (hard).

function      Turbo C        djgpp 1.06        WM-emu387     wm-FPU-emu

   +          60.5           154.8              76.5          139.4
   -          61.1-65.5      157.3-160.8        76.2-79.5     142.9-144.7
   *          71.0           190.8              79.6          146.6
   /          61.2-75.0      261.4-266.9        75.3-91.6     142.2-158.1

 sin()        310.8          4692.0            319.0          398.5
 cos()        284.4          4855.2            308.0          388.7
 tan()        495.0          8807.1            394.9          504.7
 atan()       328.9          4866.4            601.1          419.5-491.9

 sqrt()       128.7          crashed           145.2          227.0
 log()        413.1-419.1    5103.4-5354.21    254.7-282.2    409.4-437.1
 exp()        479.1          6619.2            469.1          850.8


The performance under Linux is improved by the use of look-ahead code.
The following results show the improvement which is obtained under
Linux due to the look-ahead code. Also given are the times for the
original Linux emulator with the 4.1 'soft' lib.

 [ Linus' note: I changed look-ahead to be the default under linux, as
   there was no reason not to use it after I had edited it to be
   disabled during tracing ]

            wm-FPU-emu w     original w
            look-ahead       'soft' lib
   +         106.4             190.2
   -         108.6-111.6      192.4-216.2
   *         113.4             193.1
   /         108.8-124.4      700.1-706.2

 sin()       390.5            2642.0
 cos()       381.5            2767.4
 tan()       496.5            3153.3
 atan()      367.2-435.5     2439.4-3396.8

 sqrt()      195.1            4732.5
 log()       358.0-387.5     3359.2-3390.3
 exp()       619.3            4046.4


These figures are now somewhat out-of-date. The emulator has become
progressively slower for most functions as more of the 80486 features
have been implemented.


----------------------- Accuracy of wm-FPU-emu -----------------------


The accuracy of the emulator is in almost all cases equal to or better
than that of an Intel 80486 FPU.

The results of the basic arithmetic functions (+,-,*,/), and fsqrt
match those of an 80486 FPU. They are the best possible; the error for
these never exceeds 1/2 an lsb. The fprem and fprem1 instructions
return exact results; they have no error.


The following table compares the emulator accuracy for the sqrt(),
trig and log functions against the Turbo C "emulator". For this table,
each function was tested at about 400 points. Ideal worst-case results
would be 64 bits. The reduced Turbo C accuracy of cos() and tan() for
arguments greater than pi/4 can be thought of as being related to the
precision of the argument x; e.g. an argument of pi/2-(1e-10) which is
accurate to 64 bits can result in a relative accuracy in cos() of
about 64 + log2(cos(x)) = 31 bits.


Function      Tested x range            Worst result                Turbo C
                                        (relative bits)

sqrt(x)       1 .. 2                    64.1                         63.2
atan(x)       1e-10 .. 200              64.2                         62.8
cos(x)        0 .. pi/2-(1e-10)         64.4 (x <= pi/4)             62.4
                                        64.1 (x = pi/2-(1e-10))      31.9
sin(x)        1e-10 .. pi/2             64.0                         62.8
tan(x)        1e-10 .. pi/2-(1e-10)     64.0 (x <= pi/4)             62.1
                                        64.1 (x = pi/2-(1e-10))      31.9
exp(x)        0 .. 1                    63.1 **                      62.9
log(x)        1+1e-6 .. 2               63.8 **                      62.1

** The accuracy for exp() and log() is low because the FPU (emulator)
does not compute them directly; two operations are required.


The emulator passes the "paranoia" tests (compiled with gcc 2.3.3 or
later) for 'float' variables (24 bit precision numbers) when precision
control is set to 24, 53 or 64 bits, and for 'double' variables (53
bit precision numbers) when precision control is set to 53 bits (a
properly performing FPU cannot pass the 'paranoia' tests for 'double'
variables when precision control is set to 64 bits).

The code for reducing the argument for the trig functions (fsin, fcos,
fptan and fsincos) has been improved and now effectively uses a value
for pi which is accurate to more than 128 bits precision. As a
consequence, the accuracy of these functions for large arguments has
been dramatically improved (and is now very much better than an 80486
FPU). There is also now no degradation of accuracy for fcos and fptan
for operands close to pi/2. Measured results are (note that the
definition of accuracy has changed slightly from that used for the
above table):

Function      Tested x range          Worst result
                                     (absolute bits)

cos(x)        0 .. 9.22e+18              62.0
sin(x)        1e-16 .. 9.22e+18          62.1
tan(x)        1e-16 .. 9.22e+18          61.8

It is possible with some effort to find very large arguments which
give much degraded precision. For example, the integer number
           8227740058411162616.0
is within about 10e-7 of a multiple of pi. To find the tan (for
example) of this number to 64 bits precision it would be necessary to
have a value of pi which had about 150 bits precision. The FPU
emulator computes the result to about 42.6 bits precision (the correct
result is about -9.739715e-8). On the other hand, an 80486 FPU returns
0.01059, which in relative terms is hopelessly inaccurate.

For arguments close to critical angles (which occur at multiples of
pi/2) the emulator is more accurate than an 80486 FPU. For very large
arguments, the emulator is far more accurate.


Prior to version 1.20 of the emulator, the accuracy of the results for
the transcendental functions (in their principal range) was not as
good as the results from an 80486 FPU. From version 1.20, the accuracy
has been considerably improved and these functions now give measured
worst-case results which are better than the worst-case results given
by an 80486 FPU.

The following table gives the measured results for the emulator. The
number of randomly selected arguments in each case is about half a
million.  The group of three columns gives the frequency of the given
accuracy in number of times per million, thus the second of these
columns shows that an accuracy of between 63.80 and 63.89 bits was
found at a rate of 133 times per one million measurements for fsin.
The results show that the fsin, fcos and fptan instructions return
results which are in error (i.e. less accurate than the best possible
result (which is 64 bits)) for about one per cent of all arguments
between -pi/2 and +pi/2.  The other instructions have a lower
frequency of results which are in error.  The last two columns give
the worst accuracy which was found (in bits) and the approximate value
of the argument which produced it.

                                frequency (per M)
                               -------------------   ---------------
instr   arg range    # tests   63.7   63.8    63.9   worst   at arg
                               bits   bits    bits    bits
-----  ------------  -------   ----   ----   -----   -----  --------
fsin     (0,pi/2)     547756      0    133   10673   63.89  0.451317
fcos     (0,pi/2)     547563      0    126   10532   63.85  0.700801
fptan    (0,pi/2)     536274     11    267   10059   63.74  0.784876
fpatan  4 quadrants   517087      0      8    1855   63.88  0.435121 (4q)
fyl2x     (0,20)      541861      0      0    1323   63.94  1.40923  (x)
fyl2xp1 (-.293,.414)  520256      0      0    5678   63.93  0.408542 (x)
f2xm1     (-1,1)      538847      4    481    6488   63.79  0.167709


Tests performed on an 80486 FPU showed results of lower accuracy. The
following table gives the results which were obtained with an AMD
486DX2/66 (other tests indicate that an Intel 486DX produces
identical results).  The tests were basically the same as those used
to measure the emulator (the values, being random, were in general not
the same).  The total number of tests for each instruction are given
at the end of the table, in case each about 100k tests were performed.
Another line of figures at the end of the table shows that most of the
instructions return results which are in error for more than 10
percent of the arguments tested.

The numbers in the body of the table give the approx number of times a
result of the given accuracy in bits (given in the left-most column)
was obtained per one million arguments. For three of the instructions,
two columns of results are given: * The second column for f2xm1 gives
the number cases where the results of the first column were for a
positive argument, this shows that this instruction gives better
results for positive arguments than it does for negative.  * In the
cases of fcos and fptan, the first column gives the results when all
cases where arguments greater than 1.5 were removed from the results
given in the second column. Unlike the emulator, an 80486 FPU returns
results of relatively poor accuracy for these instructions when the
argument approaches pi/2. The table does not show those cases when the
accuracy of the results were less than 62 bits, which occurs quite
often for fsin and fptan when the argument approaches pi/2. This poor
accuracy is discussed above in relation to the Turbo C "emulator", and
the accuracy of the value of pi.


bits   f2xm1  f2xm1 fpatan   fcos   fcos  fyl2x fyl2xp1  fsin  fptan  fptan
62.0       0      0      0      0    437      0      0      0      0    925
62.1       0      0     10      0    894      0      0      0      0   1023
62.2      14      0      0      0   1033      0      0      0      0    945
62.3      57      0      0      0   1202      0      0      0      0   1023
62.4     385      0      0     10   1292      0     23      0      0   1178
62.5    1140      0      0    119   1649      0     39      0      0   1149
62.6    2037      0      0    189   1620      0     16      0      0   1169
62.7    5086     14      0    646   2315     10    101     35     39   1402
62.8    8818     86      0    984   3050     59    287    131    224   2036
62.9   11340   1355      0   2126   4153     79    605    357    321   1948
63.0   15557   4750      0   3319   5376    246   1281    862    808   2688
63.1   20016   8288      0   4620   6628    511   2569   1723   1510   3302
63.2   24945  11127     10   6588   8098   1120   4470   2968   2990   4724
63.3   25686  12382     69   8774  10682   1906   6775   4482   5474   7236
63.4   29219  14722     79  11109  12311   3094   9414   7259   8912  10587
63.5   30458  14936    393  13802  15014   5874  12666   9609  13762  15262
63.6   32439  16448   1277  17945  19028  10226  15537  14657  19158  20346
63.7   35031  16805   4067  23003  23947  18910  20116  21333  25001  26209
63.8   33251  15820   7673  24781  25675  24617  25354  24440  29433  30329
63.9   33293  16833  18529  28318  29233  31267  31470  27748  29676  30601

Per cent with error:
        30.9           3.2          18.5    9.8   13.1   11.6          17.4
Total arguments tested:
       70194  70099 101784 100641 100641 101799 128853 114893 102675 102675


------------------------- Contributors -------------------------------

A number of people have contributed to the development of the
emulator, often by just reporting bugs, sometimes with suggested
fixes, and a few kind people have provided me with access in one way
or another to an 80486 machine. Contributors include (to those people
who I may have forgotten, please forgive me):

Linus Torvalds
Tommy.Thorn@daimi.aau.dk
Andrew.Tridgell@anu.edu.au
Nick Holloway, alfie@dcs.warwick.ac.uk
Hermano Moura, moura@dcs.gla.ac.uk
Jon Jagger, J.Jagger@scp.ac.uk
Lennart Benschop
Brian Gallew, geek+@CMU.EDU
Thomas Staniszewski, ts3v+@andrew.cmu.edu
Martin Howell, mph@plasma.apana.org.au
M Saggaf, alsaggaf@athena.mit.edu
Peter Barker, PETER@socpsy.sci.fau.edu
tom@vlsivie.tuwien.ac.at
Dan Russel, russed@rpi.edu
Daniel Carosone, danielce@ee.mu.oz.au
cae@jpmorgan.com
Hamish Coleman, t933093@minyos.xx.rmit.oz.au
Bruce Evans, bde@kralizec.zeta.org.au
Timo Korvola, Timo.Korvola@hut.fi
Rick Lyons, rick@razorback.brisnet.org.au
Rick, jrs@world.std.com
 
...and numerous others who responded to my request for help with
a real 80486.

