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

bzip2-1.0.6 should compile without problems on the vast majority of
platforms.  Using the supplied Makefile, I've built and tested it
myself for x86-linux and amd64-linux.  With makefile.msc, Visual C++
6.0 and nmake, you can build a native Win32 version too.  Large file
support seems to work correctly on at least on amd64-linux.

When I say "large file" I mean a file of size 2,147,483,648 (2^31)
bytes or above.  Many older OSs can't handle files above this size,
but many newer ones can.  Large files are pretty huge -- most files
you'll encounter are not Large Files.

Early versions of bzip2 (0.1, 0.9.0, 0.9.5) compiled on a wide variety
of platforms without difficulty, and I hope this version will continue
in that tradition.  However, in order to support large files, I've had
to include the define -D_FILE_OFFSET_BITS=64 in the Makefile.  This
can cause problems.

The technique of adding -D_FILE_OFFSET_BITS=64 to get large file
support is, as far as I know, the Recommended Way to get correct large
file support.  For more details, see the Large File Support
Specification, published by the Large File Summit, at

   http://ftp.sas.com/standards/large.file

As a general comment, if you get compilation errors which you think
are related to large file support, try removing the above define from
the Makefile, ie, delete the line

   BIGFILES=-D_FILE_OFFSET_BITS=64 

from the Makefile, and do 'make clean ; make'.  This will give you a
version of bzip2 without large file support, which, for most
applications, is probably not a problem.  

Alternatively, try some of the platform-specific hints listed below.

You can use the spewG.c program to generate huge files to test bzip2's
large file support, if you are feeling paranoid.  Be aware though that
any compilation problems which affect bzip2 will also affect spewG.c,
alas.

AIX: I have reports that for large file support, you need to specify
-D_LARGE_FILES rather than -D_FILE_OFFSET_BITS=64.  I have not tested
this myself.
