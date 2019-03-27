 ------------------------------------------------------------------
 This file is part of bzip2/libbzip2, a program and library for
 lossless, block-sorting data compression.

 bzip2/libbzip2 version 1.0.6 of 6 September 2010
 Copyright (C) 1996-2010 Julian Seward <jseward@bzip.org>

 Please read the WARNING, DISCLAIMER and PATENTS sections in the 
 README file.

 This program is released under the terms of the license contained
 in the file LICENSE.
 ------------------------------------------------------------------


0.9.0
~~~~~
First version.


0.9.0a
~~~~~~
Removed 'ranlib' from Makefile, since most modern Unix-es 
don't need it, or even know about it.


0.9.0b
~~~~~~
Fixed a problem with error reporting in bzip2.c.  This does not effect
the library in any way.  Problem is: versions 0.9.0 and 0.9.0a (of the
program proper) compress and decompress correctly, but give misleading
error messages (internal panics) when an I/O error occurs, instead of
reporting the problem correctly.  This shouldn't give any data loss
(as far as I can see), but is confusing.

Made the inline declarations disappear for non-GCC compilers.


0.9.0c
~~~~~~
Fixed some problems in the library pertaining to some boundary cases.
This makes the library behave more correctly in those situations.  The
fixes apply only to features (calls and parameters) not used by
bzip2.c, so the non-fixedness of them in previous versions has no
effect on reliability of bzip2.c.

In bzlib.c:
   * made zero-length BZ_FLUSH work correctly in bzCompress().
   * fixed bzWrite/bzRead to ignore zero-length requests.
   * fixed bzread to correctly handle read requests after EOF.
   * wrong parameter order in call to bzDecompressInit in
     bzBuffToBuffDecompress.  Fixed.

In compress.c:
   * changed setting of nGroups in sendMTFValues() so as to 
     do a bit better on small files.  This _does_ effect
     bzip2.c.


0.9.5a
~~~~~~
Major change: add a fallback sorting algorithm (blocksort.c)
to give reasonable behaviour even for very repetitive inputs.
Nuked --repetitive-best and --repetitive-fast since they are
no longer useful.

Minor changes: mostly a whole bunch of small changes/
bugfixes in the driver (bzip2.c).  Changes pertaining to the
user interface are:

   allow decompression of symlink'd files to stdout
   decompress/test files even without .bz2 extension
   give more accurate error messages for I/O errors
   when compressing/decompressing to stdout, don't catch control-C
   read flags from BZIP2 and BZIP environment variables
   decline to break hard links to a file unless forced with -f
   allow -c flag even with no filenames
   preserve file ownerships as far as possible
   make -s -1 give the expected block size (100k)
   add a flag -q --quiet to suppress nonessential warnings
   stop decoding flags after --, so files beginning in - can be handled
   resolved inconsistent naming: bzcat or bz2cat ?
   bzip2 --help now returns 0

Programming-level changes are:

   fixed syntax error in GET_LL4 for Borland C++ 5.02
   let bzBuffToBuffDecompress return BZ_DATA_ERROR{_MAGIC}
   fix overshoot of mode-string end in bzopen_or_bzdopen
   wrapped bzlib.h in #ifdef __cplusplus ... extern "C" { ... }
   close file handles under all error conditions
   added minor mods so it compiles with DJGPP out of the box
   fixed Makefile so it doesn't give problems with BSD make
   fix uninitialised memory reads in dlltest.c

0.9.5b
~~~~~~
Open stdin/stdout in binary mode for DJGPP.

0.9.5c
~~~~~~
Changed BZ_N_OVERSHOOT to be ... + 2 instead of ... + 1.  The + 1
version could cause the sorted order to be wrong in some extremely
obscure cases.  Also changed setting of quadrant in blocksort.c.

0.9.5d
~~~~~~
The only functional change is to make bzlibVersion() in the library
return the correct string.  This has no effect whatsoever on the
functioning of the bzip2 program or library.  Added a couple of casts
so the library compiles without warnings at level 3 in MS Visual
Studio 6.0.  Included a Y2K statement in the file Y2K_INFO.  All other
changes are minor documentation changes.

1.0
~~~
Several minor bugfixes and enhancements:

* Large file support.  The library uses 64-bit counters to
  count the volume of data passing through it.  bzip2.c 
  is now compiled with -D_FILE_OFFSET_BITS=64 to get large
  file support from the C library.  -v correctly prints out
  file sizes greater than 4 gigabytes.  All these changes have
  been made without assuming a 64-bit platform or a C compiler
  which supports 64-bit ints, so, except for the C library
  aspect, they are fully portable.

* Decompression robustness.  The library/program should be
  robust to any corruption of compressed data, detecting and
  handling _all_ corruption, instead of merely relying on
  the CRCs.  What this means is that the program should 
  never crash, given corrupted data, and the library should
  always return BZ_DATA_ERROR.

* Fixed an obscure race-condition bug only ever observed on
  Solaris, in which, if you were very unlucky and issued
  control-C at exactly the wrong time, both input and output
  files would be deleted.

* Don't run out of file handles on test/decompression when
  large numbers of files have invalid magic numbers.

* Avoid library namespace pollution.  Prefix all exported 
  symbols with BZ2_.

* Minor sorting enhancements from my DCC2000 paper.

* Advance the version number to 1.0, so as to counteract the
  (false-in-this-case) impression some people have that programs 
  with version numbers less than 1.0 are in some way, experimental,
  pre-release versions.

* Create an initial Makefile-libbz2_so to build a shared library.
  Yes, I know I should really use libtool et al ...

* Make the program exit with 2 instead of 0 when decompression
  fails due to a bad magic number (ie, an invalid bzip2 header).
  Also exit with 1 (as the manual claims :-) whenever a diagnostic
  message would have been printed AND the corresponding operation 
  is aborted, for example
     bzip2: Output file xx already exists.
  When a diagnostic message is printed but the operation is not 
  aborted, for example
     bzip2: Can't guess original name for wurble -- using wurble.out
  then the exit value 0 is returned, unless some other problem is
  also detected.

  I think it corresponds more closely to what the manual claims now.


1.0.1
~~~~~
* Modified dlltest.c so it uses the new BZ2_ naming scheme.
* Modified makefile-msc to fix minor build probs on Win2k.
* Updated README.COMPILATION.PROBLEMS.

There are no functionality changes or bug fixes relative to version
1.0.0.  This is just a documentation update + a fix for minor Win32
build problems.  For almost everyone, upgrading from 1.0.0 to 1.0.1 is
utterly pointless.  Don't bother.


1.0.2
~~~~~
A bug fix release, addressing various minor issues which have appeared
in the 18 or so months since 1.0.1 was released.  Most of the fixes
are to do with file-handling or documentation bugs.  To the best of my
knowledge, there have been no data-loss-causing bugs reported in the
compression/decompression engine of 1.0.0 or 1.0.1.

Note that this release does not improve the rather crude build system
for Unix platforms.  The general plan here is to autoconfiscate/
libtoolise 1.0.2 soon after release, and release the result as 1.1.0
or perhaps 1.2.0.  That, however, is still just a plan at this point.

Here are the changes in 1.0.2.  Bug-reporters and/or patch-senders in
parentheses.

* Fix an infinite segfault loop in 1.0.1 when a directory is
  encountered in -f (force) mode.
     (Trond Eivind Glomsrod, Nicholas Nethercote, Volker Schmidt)

* Avoid double fclose() of output file on certain I/O error paths.
     (Solar Designer)

* Don't fail with internal error 1007 when fed a long stream (> 48MB)
  of byte 251.  Also print useful message suggesting that 1007s may be
  caused by bad memory.
     (noticed by Juan Pedro Vallejo, fixed by me)

* Fix uninitialised variable silly bug in demo prog dlltest.c.
     (Jorj Bauer)

* Remove 512-MB limitation on recovered file size for bzip2recover
  on selected platforms which support 64-bit ints.  At the moment
  all GCC supported platforms, and Win32.
     (me, Alson van der Meulen)

* Hard-code header byte values, to give correct operation on platforms
  using EBCDIC as their native character set (IBM's OS/390).
     (Leland Lucius)

* Copy file access times correctly.
     (Marty Leisner)

* Add distclean and check targets to Makefile.
     (Michael Carmack)

* Parameterise use of ar and ranlib in Makefile.  Also add $(LDFLAGS).
     (Rich Ireland, Bo Thorsen)

* Pass -p (create parent dirs as needed) to mkdir during make install.
     (Jeremy Fusco)

* Dereference symlinks when copying file permissions in -f mode.
     (Volker Schmidt)

* Majorly simplify implementation of uInt64_qrm10.
     (Bo Lindbergh)

* Check the input file still exists before deleting the output one,
  when aborting in cleanUpAndFail().
     (Joerg Prante, Robert Linden, Matthias Krings)

Also a bunch of patches courtesy of Philippe Troin, the Debian maintainer
of bzip2:

* Wrapper scripts (with manpages): bzdiff, bzgrep, bzmore.

* Spelling changes and minor enhancements in bzip2.1.

* Avoid race condition between creating the output file and setting its
  interim permissions safely, by using fopen_output_safely().
  No changes to bzip2recover since there is no issue with file
  permissions there.

* do not print senseless report with -v when compressing an empty
  file.

* bzcat -f works on non-bzip2 files.

* do not try to escape shell meta-characters on unix (the shell takes
  care of these).

* added --fast and --best aliases for -1 -9 for gzip compatibility.


1.0.3 (15 Feb 05)
~~~~~~~~~~~~~~~~~
Fixes some minor bugs since the last version, 1.0.2.

* Further robustification against corrupted compressed data.
  There are currently no known bitstreams which can cause the
  decompressor to crash, loop or access memory which does not
  belong to it.  If you are using bzip2 or the library to 
  decompress bitstreams from untrusted sources, an upgrade
  to 1.0.3 is recommended.  This fixes CAN-2005-1260.

* The documentation has been converted to XML, from which html
  and pdf can be derived.

* Various minor bugs in the documentation have been fixed.

* Fixes for various compilation warnings with newer versions of
  gcc, and on 64-bit platforms.

* The BZ_NO_STDIO cpp symbol was not properly observed in 1.0.2.
  This has been fixed.


1.0.4 (20 Dec 06)
~~~~~~~~~~~~~~~~~
Fixes some minor bugs since the last version, 1.0.3.

* Fix file permissions race problem (CAN-2005-0953).

* Avoid possible segfault in BZ2_bzclose.  From Coverity's NetBSD
  scan.

* 'const'/prototype cleanups in the C code.

* Change default install location to /usr/local, and handle multiple
  'make install's without error.

* Sanitise file names more carefully in bzgrep.  Fixes CAN-2005-0758
  to the extent that applies to bzgrep.

* Use 'mktemp' rather than 'tempfile' in bzdiff.

* Tighten up a couple of assertions in blocksort.c following automated
  analysis.

* Fix minor doc/comment bugs.


1.0.5 (10 Dec 07)
~~~~~~~~~~~~~~~~~
Security fix only.  Fixes CERT-FI 20469 as it applies to bzip2.


1.0.6 (6 Sept 10)
~~~~~~~~~~~~~~~~~

* Security fix for CVE-2010-0405.  This was reported by Mikolaj
  Izdebski.

* Make the documentation build on Ubuntu 10.04
