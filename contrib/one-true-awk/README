/****************************************************************
Copyright (C) Lucent Technologies 1997
All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name Lucent Technologies or any of
its entities not be used in advertising or publicity pertaining
to distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.
****************************************************************/

This is the version of awk described in "The AWK Programming Language",
by Al Aho, Brian Kernighan, and Peter Weinberger
(Addison-Wesley, 1988, ISBN 0-201-07981-X).

Changes, mostly bug fixes and occasional enhancements, are listed
in FIXES.  If you distribute this code further, please please please
distribute FIXES with it.  If you find errors, please report them
to bwk@cs.princeton.edu.  Thanks.

The program itself is created by
	make
which should produce a sequence of messages roughly like this:

	yacc -d awkgram.y

conflicts: 43 shift/reduce, 85 reduce/reduce
	mv y.tab.c ytab.c
	mv y.tab.h ytab.h
	cc -c ytab.c
	cc -c b.c
	cc -c main.c
	cc -c parse.c
	cc maketab.c -o maketab
	./maketab >proctab.c
	cc -c proctab.c
	cc -c tran.c
	cc -c lib.c
	cc -c run.c
	cc -c lex.c
	cc ytab.o b.o main.o parse.o proctab.o tran.o lib.o run.o lex.o -lm

This produces an executable a.out; you will eventually want to
move this to some place like /usr/bin/awk.

If your system does not have yacc or bison (the GNU
equivalent), you must compile the pieces manually.  We have
included yacc output in ytab.c and ytab.h, and backup copies in
case you overwrite them.  We have also included a copy of
proctab.c so you do not need to run maketab.

NOTE: This version uses ANSI C, as you should also.  We have
compiled this without any changes using gcc -Wall and/or local C
compilers on a variety of systems, but new systems or compilers
may raise some new complaint; reports of difficulties are
welcome.

This also compiles with Visual C++ on all flavors of Windows,
*if* you provide versions of popen and pclose.  The file
missing95.c contains versions that can be used to get started
with, though the underlying support has mysterious properties,
the symptom of which can be truncated pipe output.  Beware.  The
file makefile.win gives hints on how to proceed; if you run
vcvars32.bat, it will set up necessary paths and parameters so
you can subsequently run nmake -f makefile.win.  Beware also that
when running on Windows under command.com, various quoting
conventions are different from Unix systems: single quotes won't
work around arguments, and various characters like % are
interpreted within double quotes.

This compiles without change on Macintosh OS X using gcc and
the standard developer tools.

This is also said to compile on Macintosh OS 9 systems, using the
file "buildmac" provided by Dan Allen (danallen@microsoft.com),
to whom many thanks.

The version of malloc that comes with some systems is sometimes
astonishly slow.  If awk seems slow, you might try fixing that.
More generally, turning on optimization can significantly improve
awk's speed, perhaps by 1/3 for highest levels.
