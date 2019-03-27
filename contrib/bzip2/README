
This is the README for bzip2/libzip2.
This version is fully compatible with the previous public releases.

------------------------------------------------------------------
This file is part of bzip2/libbzip2, a program and library for
lossless, block-sorting data compression.

bzip2/libbzip2 version 1.0.6 of 6 September 2010
Copyright (C) 1996-2010 Julian Seward <jseward@bzip.org>

Please read the WARNING, DISCLAIMER and PATENTS sections in this file.

This program is released under the terms of the license contained
in the file LICENSE.
------------------------------------------------------------------

Complete documentation is available in Postscript form (manual.ps),
PDF (manual.pdf) or html (manual.html).  A plain-text version of the
manual page is available as bzip2.txt.


HOW TO BUILD -- UNIX

Type 'make'.  This builds the library libbz2.a and then the programs
bzip2 and bzip2recover.  Six self-tests are run.  If the self-tests
complete ok, carry on to installation:

To install in /usr/local/bin, /usr/local/lib, /usr/local/man and
/usr/local/include, type

   make install

To install somewhere else, eg, /xxx/yyy/{bin,lib,man,include}, type

   make install PREFIX=/xxx/yyy

If you are (justifiably) paranoid and want to see what 'make install'
is going to do, you can first do

   make -n install                      or
   make -n install PREFIX=/xxx/yyy      respectively.

The -n instructs make to show the commands it would execute, but not
actually execute them.


HOW TO BUILD -- UNIX, shared library libbz2.so.

Do 'make -f Makefile-libbz2_so'.  This Makefile seems to work for
Linux-ELF (RedHat 7.2 on an x86 box), with gcc.  I make no claims
that it works for any other platform, though I suspect it probably
will work for most platforms employing both ELF and gcc.

bzip2-shared, a client of the shared library, is also built, but not
self-tested.  So I suggest you also build using the normal Makefile,
since that conducts a self-test.  A second reason to prefer the
version statically linked to the library is that, on x86 platforms,
building shared objects makes a valuable register (%ebx) unavailable
to gcc, resulting in a slowdown of 10%-20%, at least for bzip2.

Important note for people upgrading .so's from 0.9.0/0.9.5 to version
1.0.X.  All the functions in the library have been renamed, from (eg)
bzCompress to BZ2_bzCompress, to avoid namespace pollution.
Unfortunately this means that the libbz2.so created by
Makefile-libbz2_so will not work with any program which used an older
version of the library.  I do encourage library clients to make the
effort to upgrade to use version 1.0, since it is both faster and more
robust than previous versions.


HOW TO BUILD -- Windows 95, NT, DOS, Mac, etc.

It's difficult for me to support compilation on all these platforms.
My approach is to collect binaries for these platforms, and put them
on the master web site (http://www.bzip.org).  Look there.  However
(FWIW), bzip2-1.0.X is very standard ANSI C and should compile
unmodified with MS Visual C.  If you have difficulties building, you
might want to read README.COMPILATION.PROBLEMS.

At least using MS Visual C++ 6, you can build from the unmodified
sources by issuing, in a command shell: 

   nmake -f makefile.msc

(you may need to first run the MSVC-provided script VCVARS32.BAT
 so as to set up paths to the MSVC tools correctly).


VALIDATION

Correct operation, in the sense that a compressed file can always be
decompressed to reproduce the original, is obviously of paramount
importance.  To validate bzip2, I used a modified version of Mark
Nelson's churn program.  Churn is an automated test driver which
recursively traverses a directory structure, using bzip2 to compress
and then decompress each file it encounters, and checking that the
decompressed data is the same as the original.



Please read and be aware of the following:

WARNING:

   This program and library (attempts to) compress data by 
   performing several non-trivial transformations on it.  
   Unless you are 100% familiar with *all* the algorithms 
   contained herein, and with the consequences of modifying them, 
   you should NOT meddle with the compression or decompression 
   machinery.  Incorrect changes can and very likely *will* 
   lead to disastrous loss of data.


DISCLAIMER:

   I TAKE NO RESPONSIBILITY FOR ANY LOSS OF DATA ARISING FROM THE
   USE OF THIS PROGRAM/LIBRARY, HOWSOEVER CAUSED.

   Every compression of a file implies an assumption that the
   compressed file can be decompressed to reproduce the original.
   Great efforts in design, coding and testing have been made to
   ensure that this program works correctly.  However, the complexity
   of the algorithms, and, in particular, the presence of various
   special cases in the code which occur with very low but non-zero
   probability make it impossible to rule out the possibility of bugs
   remaining in the program.  DO NOT COMPRESS ANY DATA WITH THIS
   PROGRAM UNLESS YOU ARE PREPARED TO ACCEPT THE POSSIBILITY, HOWEVER
   SMALL, THAT THE DATA WILL NOT BE RECOVERABLE.

   That is not to say this program is inherently unreliable.  
   Indeed, I very much hope the opposite is true.  bzip2/libbzip2 
   has been carefully constructed and extensively tested.


PATENTS:

   To the best of my knowledge, bzip2/libbzip2 does not use any 
   patented algorithms.  However, I do not have the resources 
   to carry out a patent search.  Therefore I cannot give any 
   guarantee of the above statement.



WHAT'S NEW IN 0.9.0 (as compared to 0.1pl2) ?

   * Approx 10% faster compression, 30% faster decompression
   * -t (test mode) is a lot quicker
   * Can decompress concatenated compressed files
   * Programming interface, so programs can directly read/write .bz2 files
   * Less restrictive (BSD-style) licensing
   * Flag handling more compatible with GNU gzip
   * Much more documentation, i.e., a proper user manual
   * Hopefully, improved portability (at least of the library)

WHAT'S NEW IN 0.9.5 ?

   * Compression speed is much less sensitive to the input
     data than in previous versions.  Specifically, the very
     slow performance caused by repetitive data is fixed.
   * Many small improvements in file and flag handling.
   * A Y2K statement.

WHAT'S NEW IN 1.0.0 ?

   See the CHANGES file.

WHAT'S NEW IN 1.0.2 ?

   See the CHANGES file.

WHAT'S NEW IN 1.0.3 ?

   See the CHANGES file.

WHAT'S NEW IN 1.0.4 ?

   See the CHANGES file.

WHAT'S NEW IN 1.0.5 ?

   See the CHANGES file.

WHAT'S NEW IN 1.0.6 ?

   See the CHANGES file.


I hope you find bzip2 useful.  Feel free to contact me at
   jseward@bzip.org
if you have any suggestions or queries.  Many people mailed me with
comments, suggestions and patches after the releases of bzip-0.15,
bzip-0.21, and bzip2 versions 0.1pl2, 0.9.0, 0.9.5, 1.0.0, 1.0.1,
1.0.2 and 1.0.3, and the changes in bzip2 are largely a result of this
feedback.  I thank you for your comments.

bzip2's "home" is http://www.bzip.org/

Julian Seward
jseward@bzip.org
Cambridge, UK.

18     July 1996 (version 0.15)
25   August 1996 (version 0.21)
 7   August 1997 (bzip2, version 0.1)
29   August 1997 (bzip2, version 0.1pl2)
23   August 1998 (bzip2, version 0.9.0)
 8     June 1999 (bzip2, version 0.9.5)
 4     Sept 1999 (bzip2, version 0.9.5d)
 5      May 2000 (bzip2, version 1.0pre8)
30 December 2001 (bzip2, version 1.0.2pre1)
15 February 2005 (bzip2, version 1.0.3)
20 December 2006 (bzip2, version 1.0.4)
10 December 2007 (bzip2, version 1.0.5)
 6     Sept 2010 (bzip2, version 1.0.6)
