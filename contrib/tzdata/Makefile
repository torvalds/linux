# Make and install tzdb code and data.

# This file is in the public domain, so clarified as of
# 2009-05-17 by Arthur David Olson.

# Package name for the code distribution.
PACKAGE=	tzcode

# Version number for the distribution, overridden in the 'tarballs' rule below.
VERSION=	unknown

# Email address for bug reports.
BUGEMAIL=	tz@iana.org

# DATAFORM selects the data format.
# Available formats represent essentially the same data, albeit
# possibly with minor discrepancies that users are not likely to notice.
# To get new features and the best data right away, use:
#	DATAFORM=	vanguard
# To wait a while before using new features, to give downstream users
# time to upgrade zic (the default), use:
#	DATAFORM=	main
# To wait even longer for new features, use:
#	DATAFORM=	rearguard
DATAFORM=		main

# Change the line below for your timezone (after finding the one you want in
# one of the $(TDATA) source files, or adding it to a source file).
# Alternatively, if you discover you've got the wrong timezone, you can just
#	zic -l rightzone
# to correct things.
# Use the command
#	make zonenames
# to get a list of the values you can use for LOCALTIME.

LOCALTIME=	GMT

# If you want something other than Eastern United States time as a template
# for handling ruleless POSIX-style timezone environment variables,
# change the line below (after finding the timezone you want in the
# one of the $(TDATA) source files, or adding it to a source file).
# A ruleless environment setting like TZ='CST6CDT' uses the rules in the
# template file to determine "spring forward" and "fall back" days and
# times; the environment variable itself specifies UT offsets of standard and
# daylight saving time.
# Alternatively, if you discover you've got the wrong timezone, you can just
#	zic -p rightzone
# to correct things.
# Use the command
#	make zonenames
# to get a list of the values you can use for POSIXRULES.

POSIXRULES=	America/New_York

# Also see TZDEFRULESTRING below, which takes effect only
# if the time zone files cannot be accessed.


# Installation locations.
#
# The defaults are suitable for Debian, except that if REDO is
# posix_right or right_posix then files that Debian puts under
# /usr/share/zoneinfo/posix and /usr/share/zoneinfo/right are instead
# put under /usr/share/zoneinfo-posix and /usr/share/zoneinfo-leaps,
# respectively.  Problems with the Debian approach are discussed in
# the commentary for the right_posix rule (below).

# Destination directory, which can be used for staging.
# 'make DESTDIR=/stage install' installs under /stage (e.g., to
# /stage/etc/localtime instead of to /etc/localtime).  Files under
# /stage are not intended to work as-is, but can be copied by hand to
# the root directory later.  If DESTDIR is empty, 'make install' does
# not stage, but installs directly into production locations.
DESTDIR =

# Everything is installed into subdirectories of TOPDIR, and used there.
# TOPDIR should be empty (meaning the root directory),
# or a directory name that does not end in "/".
# TOPDIR should be empty or an absolute name unless you're just testing.
TOPDIR =

# The default local timezone is taken from the file TZDEFAULT.
TZDEFAULT = $(TOPDIR)/etc/localtime

# The subdirectory containing installed program and data files, and
# likewise for installed files that can be shared among architectures.
# These should be relative file names.
USRDIR = usr
USRSHAREDIR = $(USRDIR)/share

# "Compiled" timezone information is placed in the "TZDIR" directory
# (and subdirectories).
# TZDIR_BASENAME should not contain "/" and should not be ".", ".." or empty.
TZDIR_BASENAME=	zoneinfo
TZDIR = $(TOPDIR)/$(USRSHAREDIR)/$(TZDIR_BASENAME)

# The "tzselect" and (if you do "make INSTALL") "date" commands go in:
BINDIR = $(TOPDIR)/$(USRDIR)/bin

# The "zdump" command goes in:
ZDUMPDIR = $(BINDIR)

# The "zic" command goes in:
ZICDIR = $(TOPDIR)/$(USRDIR)/sbin

# Manual pages go in subdirectories of. . .
MANDIR = $(TOPDIR)/$(USRSHAREDIR)/man

# Library functions are put in an archive in LIBDIR.
LIBDIR = $(TOPDIR)/$(USRDIR)/lib


# Types to try, as an alternative to time_t.
TIME_T_ALTERNATIVES = $(TIME_T_ALTERNATIVES_HEAD) $(TIME_T_ALTERNATIVES_TAIL)
TIME_T_ALTERNATIVES_HEAD = int64_t
TIME_T_ALTERNATIVES_TAIL = int32_t uint32_t uint64_t

# What kind of TZif data files to generate.  (TZif is the binary time
# zone data format that zic generates; see Internet RFC 8536.)
# If you want only POSIX time, with time values interpreted as
# seconds since the epoch (not counting leap seconds), use
#	REDO=		posix_only
# below.  If you want only "right" time, with values interpreted
# as seconds since the epoch (counting leap seconds), use
#	REDO=		right_only
# below.  If you want both sets of data available, with leap seconds not
# counted normally, use
#	REDO=		posix_right
# below.  If you want both sets of data available, with leap seconds counted
# normally, use
#	REDO=		right_posix
# below.  POSIX mandates that leap seconds not be counted; for compatibility
# with it, use "posix_only" or "posix_right".  Use POSIX time on systems with
# leap smearing; this can work better than unsmeared "right" time with
# applications that are not leap second aware, and is closer to unsmeared
# "right" time than unsmeared POSIX time is (e.g., 0.5 vs 1.0 s max error).

REDO=		posix_right

# To install data in text form that has all the information of the TZif data,
# (optionally incorporating leap second information), use
#	TZDATA_TEXT=	tzdata.zi leapseconds
# To install text data without leap second information (e.g., because
# REDO='posix_only'), use
#	TZDATA_TEXT=	tzdata.zi
# To avoid installing text data, use
#	TZDATA_TEXT=

TZDATA_TEXT=	leapseconds tzdata.zi

# For backward-compatibility links for old zone names, use
#	BACKWARD=	backward
# If you also want the link US/Pacific-New, even though it is confusing
# and is planned to be removed from the database eventually, use
#	BACKWARD=	backward pacificnew
# To omit these links, use
#	BACKWARD=

BACKWARD=	backward

# If you want out-of-scope and often-wrong data from the file 'backzone', use
#	PACKRATDATA=	backzone
# To omit this data, use
#	PACKRATDATA=

PACKRATDATA=

# The name of a locale using the UTF-8 encoding, used during self-tests.
# The tests are skipped if the name does not appear to work on this system.

UTF8_LOCALE=	en_US.utf8

# Since "." may not be in PATH...

YEARISTYPE=	./yearistype

# Non-default libraries needed to link.
LDLIBS=

# Add the following to the end of the "CFLAGS=" line as needed to override
# defaults specified in the source code.  "-DFOO" is equivalent to "-DFOO=1".
#  -DDEPRECATE_TWO_DIGIT_YEARS for optional runtime warnings about strftime
#	formats that generate only the last two digits of year numbers
#  -DEPOCH_LOCAL if the 'time' function returns local time not UT
#  -DEPOCH_OFFSET=N if the 'time' function returns a value N greater
#	than what POSIX specifies, assuming local time is UT.
#	For example, N is 252460800 on AmigaOS.
#  -DHAVE_DECL_ASCTIME_R=0 if <time.h> does not declare asctime_r
#  -DHAVE_DECL_ENVIRON if <unistd.h> declares 'environ'
#  -DHAVE_DIRECT_H if mkdir needs <direct.h> (MS-Windows)
#  -DHAVE_GENERIC=0 if _Generic does not work
#  -DHAVE_GETTEXT if 'gettext' works (e.g., GNU/Linux, FreeBSD, Solaris)
#  -DHAVE_INCOMPATIBLE_CTIME_R if your system's time.h declares
#	ctime_r and asctime_r incompatibly with the POSIX standard
#	(Solaris when _POSIX_PTHREAD_SEMANTICS is not defined).
#  -DHAVE_INTTYPES_H if you have a non-C99 compiler with <inttypes.h>
#  -DHAVE_LINK=0 if your system lacks a link function
#  -DHAVE_LOCALTIME_R=0 if your system lacks a localtime_r function
#  -DHAVE_LOCALTIME_RZ=0 if you do not want zdump to use localtime_rz
#	localtime_rz can make zdump significantly faster, but is nonstandard.
#  -DHAVE_POSIX_DECLS=0 if your system's include files do not declare
#	functions like 'link' or variables like 'tzname' required by POSIX
#  -DHAVE_SNPRINTF=0 if your system lacks the snprintf function
#  -DHAVE_STDBOOL_H if you have a non-C99 compiler with <stdbool.h>
#  -DHAVE_STDINT_H if you have a non-C99 compiler with <stdint.h>
#  -DHAVE_STRFTIME_L if <time.h> declares locale_t and strftime_l
#  -DHAVE_STRDUP=0 if your system lacks the strdup function
#  -DHAVE_STRTOLL=0 if your system lacks the strtoll function
#  -DHAVE_SYMLINK=0 if your system lacks the symlink function
#  -DHAVE_SYS_STAT_H=0 if your compiler lacks a <sys/stat.h>
#  -DHAVE_SYS_WAIT_H=0 if your compiler lacks a <sys/wait.h>
#  -DHAVE_TZSET=0 if your system lacks a tzset function
#  -DHAVE_UNISTD_H=0 if your compiler lacks a <unistd.h>
#  -Dlocale_t=XXX if your system uses XXX instead of locale_t
#  -DRESERVE_STD_EXT_IDS if your platform reserves standard identifiers
#	with external linkage, e.g., applications cannot define 'localtime'.
#  -Dssize_t=long on hosts like MS-Windows that lack ssize_t
#  -DSUPPRESS_TZDIR to not prepend TZDIR to file names; this has
#	security implications and is not recommended for general use
#  -DTHREAD_SAFE to make localtime.c thread-safe, as POSIX requires;
#	not needed by the main-program tz code, which is single-threaded.
#	Append other compiler flags as needed, e.g., -pthread on GNU/Linux.
#  -Dtime_tz=\"T\" to use T as the time_t type, rather than the system time_t
#	This is intended for internal use only; it mangles external names.
#  -DTZ_DOMAIN=\"foo\" to use "foo" for gettext domain name; default is "tz"
#  -DTZ_DOMAINDIR=\"/path\" to use "/path" for gettext directory;
#	the default is system-supplied, typically "/usr/lib/locale"
#  -DTZDEFRULESTRING=\",date/time,date/time\" to default to the specified
#	DST transitions if the time zone files cannot be accessed
#  -DUNINIT_TRAP if reading uninitialized storage can cause problems
#	other than simply getting garbage data
#  -DUSE_LTZ=0 to build zdump with the system time zone library
#	Also set TZDOBJS=zdump.o and CHECK_TIME_T_ALTERNATIVES= below.
#  -DZIC_MAX_ABBR_LEN_WO_WARN=3
#	(or some other number) to set the maximum time zone abbreviation length
#	that zic will accept without a warning (the default is 6)
#  $(GCC_DEBUG_FLAGS) if you are using recent GCC and want lots of checking
# Select instrumentation via "make GCC_INSTRUMENT='whatever'".
GCC_INSTRUMENT = \
  -fsanitize=undefined -fsanitize-address-use-after-scope \
  -fsanitize-undefined-trap-on-error -fstack-protector
GCC_DEBUG_FLAGS = -DGCC_LINT -g3 -O3 -fno-common \
  $(GCC_INSTRUMENT) \
  -Wall -Wextra \
  -Walloc-size-larger-than=100000 -Warray-bounds=2 \
  -Wbad-function-cast -Wcast-align=strict -Wdate-time \
  -Wdeclaration-after-statement -Wdouble-promotion \
  -Wformat=2 -Wformat-overflow=2 -Wformat-signedness -Wformat-truncation \
  -Winit-self -Wjump-misses-init -Wlogical-op \
  -Wmissing-declarations -Wmissing-prototypes -Wnested-externs \
  -Wold-style-definition -Woverlength-strings -Wpointer-arith \
  -Wshadow -Wshift-overflow=2 -Wstrict-prototypes -Wstringop-overflow=4 \
  -Wstringop-truncation -Wsuggest-attribute=cold \
  -Wsuggest-attribute=const -Wsuggest-attribute=format \
  -Wsuggest-attribute=malloc \
  -Wsuggest-attribute=noreturn -Wsuggest-attribute=pure \
  -Wtrampolines -Wundef -Wuninitialized -Wunused \
  -Wvariadic-macros -Wvla -Wwrite-strings \
  -Wno-address -Wno-format-nonliteral -Wno-sign-compare \
  -Wno-type-limits -Wno-unused-parameter
#
# If your system has a "GMT offset" field in its "struct tm"s
# (or if you decide to add such a field in your system's "time.h" file),
# add the name to a define such as
#	-DTM_GMTOFF=tm_gmtoff
# to the end of the "CFLAGS=" line.  If not defined, the code attempts to
# guess TM_GMTOFF from other macros; define NO_TM_GMTOFF to suppress this.
# Similarly, if your system has a "zone abbreviation" field, define
#	-DTM_ZONE=tm_zone
# and define NO_TM_ZONE to suppress any guessing.  These two fields are not
# required by POSIX, but are widely available on GNU/Linux and BSD systems.
#
# The next batch of options control support for external variables
# exported by tzcode.  In practice these variables are less useful
# than TM_GMTOFF and TM_ZONE.  However, most of them are standardized.
# #
# # To omit or support the external variable "tzname", add one of:
# #	-DHAVE_TZNAME=0
# #	-DHAVE_TZNAME=1
# # to the "CFLAGS=" line.  "tzname" is required by POSIX 1988 and later.
# # If not defined, the code attempts to guess HAVE_TZNAME from other macros.
# # Warning: unless time_tz is also defined, HAVE_TZNAME=1 can cause
# # crashes when combined with some platforms' standard libraries,
# # presumably due to memory allocation issues.
# #
# # To omit or support the external variables "timezone" and "daylight", add
# #	-DUSG_COMPAT=0
# #	-DUSG_COMPAT=1
# # to the "CFLAGS=" line; "timezone" and "daylight" are inspired by
# # Unix Systems Group code and are required by POSIX 2008 (with XSI) and later.
# # If not defined, the code attempts to guess USG_COMPAT from other macros.
# #
# # To support the external variable "altzone", add
# #	-DALTZONE
# # to the end of the "CFLAGS=" line; although "altzone" appeared in
# # System V Release 3.1 it has not been standardized.
#
# If you want functions that were inspired by early versions of X3J11's work,
# add
#	-DSTD_INSPIRED
# to the end of the "CFLAGS=" line.  This arranges for the functions
# "tzsetwall", "offtime", "timelocal", "timegm", "timeoff",
# "posix2time", and "time2posix" to be added to the time conversion library.
# "tzsetwall" is like "tzset" except that it arranges for local wall clock
# time (rather than the timezone specified in the TZ environment variable)
# to be used.
# "offtime" is like "gmtime" except that it accepts a second (long) argument
# that gives an offset to add to the time_t when converting it.
# "timelocal" is equivalent to "mktime".
# "timegm" is like "timelocal" except that it turns a struct tm into
# a time_t using UT (rather than local time as "timelocal" does).
# "timeoff" is like "timegm" except that it accepts a second (long) argument
# that gives an offset to use when converting to a time_t.
# "posix2time" and "time2posix" are described in an included manual page.
# X3J11's work does not describe any of these functions.
# Sun has provided "tzsetwall", "timelocal", and "timegm" in SunOS 4.0.
# These functions may well disappear in future releases of the time
# conversion package.
#
# If you don't want functions that were inspired by NetBSD, add
#	-DNETBSD_INSPIRED=0
# to the end of the "CFLAGS=" line.  Otherwise, the functions
# "localtime_rz", "mktime_z", "tzalloc", and "tzfree" are added to the
# time library, and if STD_INSPIRED is also defined the functions
# "posix2time_z" and "time2posix_z" are added as well.
# The functions ending in "_z" (or "_rz") are like their unsuffixed
# (or suffixed-by-"_r") counterparts, except with an extra first
# argument of opaque type timezone_t that specifies the timezone.
# "tzalloc" allocates a timezone_t value, and "tzfree" frees it.
#
# If you want to allocate state structures in localtime, add
#	-DALL_STATE
# to the end of the "CFLAGS=" line.  Storage is obtained by calling malloc.
#
# NIST-PCTS:151-2, Version 1.4, (1993-12-03) is a test suite put
# out by the National Institute of Standards and Technology
# which claims to test C and Posix conformance.  If you want to pass PCTS, add
#	-DPCTS
# to the end of the "CFLAGS=" line.
#
# If you want strict compliance with XPG4 as of 1994-04-09, add
#	-DXPG4_1994_04_09
# to the end of the "CFLAGS=" line.  This causes "strftime" to always return
# 53 as a week number (rather than 52 or 53) for January days before
# January's first Monday when a "%V" format is used and January 1
# falls on a Friday, Saturday, or Sunday.

CFLAGS=

# Linker flags.  Default to $(LFLAGS) for backwards compatibility
# to release 2012h and earlier.

LDFLAGS=	$(LFLAGS)

# For leap seconds, this Makefile uses LEAPSECONDS='-L leapseconds' in
# submake command lines.  The default is no leap seconds.

LEAPSECONDS=

# The zic command and its arguments.

zic=		./zic
ZIC=		$(zic) $(ZFLAGS)

# To shrink the size of installed TZif files,
# append "-r @N" to omit data before N-seconds-after-the-Epoch.
# See the zic man page for more about -r.
ZFLAGS=

# How to use zic to install TZif files.

ZIC_INSTALL=	$(ZIC) -d '$(DESTDIR)$(TZDIR)' $(LEAPSECONDS)

# The name of a Posix-compliant 'awk' on your system.
# Older 'mawk' versions, such as the 'mawk' in Ubuntu 16.04, might dump core;
# on Ubuntu you can work around this with
#	AWK=		gawk
AWK=		awk

# The full path name of a Posix-compliant shell, preferably one that supports
# the Korn shell's 'select' statement as an extension.
# These days, Bash is the most popular.
# It should be OK to set this to /bin/sh, on platforms where /bin/sh
# lacks 'select' or doesn't completely conform to Posix, but /bin/bash
# is typically nicer if it works.
KSHELL=		/bin/bash

# Name of curl <https://curl.haxx.se/>, used for HTML validation.
CURL=		curl

# The path where SGML DTDs are kept and the catalog file(s) to use when
# validating HTML 4.01.  The default should work on both Debian and Red Hat.
SGML_TOPDIR= /usr
SGML_DTDDIR= $(SGML_TOPDIR)/share/xml/w3c-sgml-lib/schema/dtd
SGML_SEARCH_PATH= $(SGML_DTDDIR)/REC-html401-19991224
SGML_CATALOG_FILES= \
  $(SGML_TOPDIR)/share/doc/w3-recs/html/www.w3.org/TR/1999/REC-html401-19991224/HTML4.cat:$(SGML_TOPDIR)/share/sgml/html/4.01/HTML4.cat

# The name, arguments and environment of a program to validate HTML 4.01.
# See <http://openjade.sourceforge.net/doc/> for a validator, and
# <https://validator.w3.org/source/> for a validation library.
# Set VALIDATE=':' if you do not have such a program.
VALIDATE = nsgmls
VALIDATE_FLAGS = -s -B -wall -wno-unused-param
VALIDATE_ENV = \
  SGML_CATALOG_FILES='$(SGML_CATALOG_FILES)' \
  SGML_SEARCH_PATH='$(SGML_SEARCH_PATH)' \
  SP_CHARSET_FIXED=YES \
  SP_ENCODING=UTF-8

# This expensive test requires USE_LTZ.
# To suppress it, define this macro to be empty.
CHECK_TIME_T_ALTERNATIVES = check_time_t_alternatives

# SAFE_CHAR is a regular expression that matches a safe character.
# Some parts of this distribution are limited to safe characters;
# others can use any UTF-8 character.
# For now, the safe characters are a safe subset of ASCII.
# The caller must set the shell variable 'sharp' to the character '#',
# since Makefile macros cannot contain '#'.
# TAB_CHAR is a single tab character, in single quotes.
TAB_CHAR=	'	'
SAFE_CHARSET1=	$(TAB_CHAR)' !\"'$$sharp'$$%&'\''()*+,./0123456789:;<=>?@'
SAFE_CHARSET2=	'ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\^_`'
SAFE_CHARSET3=	'abcdefghijklmnopqrstuvwxyz{|}~'
SAFE_CHARSET=	$(SAFE_CHARSET1)$(SAFE_CHARSET2)$(SAFE_CHARSET3)
SAFE_CHAR=	'[]'$(SAFE_CHARSET)'-]'

# These characters are Latin-1, and so are likely to be displayable
# even in editors with limited character sets.
UNUSUAL_OK_LATIN_1 = «°±»½¾×
# This IPA symbol is represented in Unicode as the composition of
# U+0075 and U+032F, and U+032F is not considered alphabetic by some
# grep implementations that do not grok composition.
UNUSUAL_OK_IPA = u̯
# Non-ASCII non-letters that OK_CHAR allows, as these characters are
# useful in commentary.
UNUSUAL_OK_CHARSET= $(UNUSUAL_OK_LATIN_1)$(UNUSUAL_OK_IPA)

# OK_CHAR matches any character allowed in the distributed files.
# This is the same as SAFE_CHAR, except that UNUSUAL_OK_CHARSET and
# multibyte letters are also allowed so that commentary can contain a
# few safe symbols and people's names and can quote non-English sources.
# Other non-letters are limited to ASCII renderings for the
# convenience of maintainers using XEmacs 21.5.34, which by default
# mishandles Unicode characters U+0100 and greater.
OK_CHAR=	'[][:alpha:]$(UNUSUAL_OK_CHARSET)'$(SAFE_CHARSET)'-]'

# SAFE_LINE matches a line of safe characters.
# SAFE_SHARP_LINE is similar, except any OK character can follow '#';
# this is so that comments can contain non-ASCII characters.
# OK_LINE matches a line of OK characters.
SAFE_LINE=	'^'$(SAFE_CHAR)'*$$'
SAFE_SHARP_LINE='^'$(SAFE_CHAR)'*('$$sharp$(OK_CHAR)'*)?$$'
OK_LINE=	'^'$(OK_CHAR)'*$$'

# Flags to give 'tar' when making a distribution.
# Try to use flags appropriate for GNU tar.
GNUTARFLAGS= --numeric-owner --owner=0 --group=0 --mode=go+u,go-w --sort=name
TARFLAGS=	`if tar $(GNUTARFLAGS) --version >/dev/null 2>&1; \
		 then echo $(GNUTARFLAGS); \
		 else :; \
		 fi`

# Flags to give 'gzip' when making a distribution.
GZIPFLAGS=	-9n

###############################################################################

#MAKE=		make

cc=		cc
CC=		$(cc) -DTZDIR='"$(TZDIR)"'

AR=		ar

# ':' on typical hosts; 'ranlib' on the ancient hosts that still need ranlib.
RANLIB=		:

TZCOBJS=	zic.o
TZDOBJS=	zdump.o localtime.o asctime.o strftime.o
DATEOBJS=	date.o localtime.o strftime.o asctime.o
LIBSRCS=	localtime.c asctime.c difftime.c
LIBOBJS=	localtime.o asctime.o difftime.o
HEADERS=	tzfile.h private.h
NONLIBSRCS=	zic.c zdump.c
NEWUCBSRCS=	date.c strftime.c
SOURCES=	$(HEADERS) $(LIBSRCS) $(NONLIBSRCS) $(NEWUCBSRCS) \
			tzselect.ksh workman.sh
MANS=		newctime.3 newstrftime.3 newtzset.3 time2posix.3 \
			tzfile.5 tzselect.8 zic.8 zdump.8
MANTXTS=	newctime.3.txt newstrftime.3.txt newtzset.3.txt \
			time2posix.3.txt \
			tzfile.5.txt tzselect.8.txt zic.8.txt zdump.8.txt \
			date.1.txt
COMMON=		calendars CONTRIBUTING LICENSE Makefile \
			NEWS README theory.html version
WEB_PAGES=	tz-art.html tz-how-to.html tz-link.html
CHECK_WEB_PAGES=check_theory.html check_tz-art.html \
			check_tz-how-to.html check_tz-link.html
DOCS=		$(MANS) date.1 $(MANTXTS) $(WEB_PAGES)
PRIMARY_YDATA=	africa antarctica asia australasia \
		europe northamerica southamerica
YDATA=		$(PRIMARY_YDATA) etcetera
NDATA=		systemv factory
TDATA_TO_CHECK=	$(YDATA) $(NDATA) backward pacificnew
TDATA=		$(YDATA) $(NDATA) $(BACKWARD)
ZONETABLES=	zone1970.tab zone.tab
TABDATA=	iso3166.tab $(TZDATA_TEXT) $(ZONETABLES)
LEAP_DEPS=	leapseconds.awk leap-seconds.list
TZDATA_ZI_DEPS=	ziguard.awk zishrink.awk version $(TDATA) $(PACKRATDATA)
DSTDATA_ZI_DEPS= ziguard.awk $(TDATA) $(PACKRATDATA)
DATA=		$(TDATA_TO_CHECK) backzone iso3166.tab leap-seconds.list \
			leapseconds yearistype.sh $(ZONETABLES)
AWK_SCRIPTS=	checklinks.awk checktab.awk leapseconds.awk \
			ziguard.awk zishrink.awk
MISC=		$(AWK_SCRIPTS) zoneinfo2tdf.pl
TZS_YEAR=	2050
TZS_CUTOFF_FLAG=	-c $(TZS_YEAR)
TZS=		to$(TZS_YEAR).tzs
TZS_NEW=	to$(TZS_YEAR)new.tzs
TZS_DEPS=	$(PRIMARY_YDATA) asctime.c localtime.c \
			private.h tzfile.h zdump.c zic.c
# EIGHT_YARDS is just a yard short of the whole ENCHILADA.
EIGHT_YARDS = $(COMMON) $(DOCS) $(SOURCES) $(DATA) $(MISC) tzdata.zi
ENCHILADA = $(EIGHT_YARDS) $(TZS)

# Consult these files when deciding whether to rebuild the 'version' file.
# This list is not the same as the output of 'git ls-files', since
# .gitignore is not distributed.
VERSION_DEPS= \
		calendars CONTRIBUTING LICENSE Makefile NEWS README \
		africa antarctica asctime.c asia australasia \
		backward backzone \
		checklinks.awk checktab.awk \
		date.1 date.c difftime.c \
		etcetera europe factory iso3166.tab \
		leap-seconds.list leapseconds.awk localtime.c \
		newctime.3 newstrftime.3 newtzset.3 northamerica \
		pacificnew private.h \
		southamerica strftime.c systemv theory.html \
		time2posix.3 tz-art.html tz-how-to.html tz-link.html \
		tzfile.5 tzfile.h tzselect.8 tzselect.ksh \
		workman.sh yearistype.sh \
		zdump.8 zdump.c zic.8 zic.c \
		ziguard.awk zishrink.awk \
		zone.tab zone1970.tab zoneinfo2tdf.pl

# And for the benefit of csh users on systems that assume the user
# shell should be used to handle commands in Makefiles. . .

SHELL=		/bin/sh

all:		tzselect yearistype zic zdump libtz.a $(TABDATA) \
		  vanguard.zi main.zi rearguard.zi

ALL:		all date $(ENCHILADA)

install:	all $(DATA) $(REDO) $(MANS)
		mkdir -p '$(DESTDIR)$(BINDIR)' \
			'$(DESTDIR)$(ZDUMPDIR)' '$(DESTDIR)$(ZICDIR)' \
			'$(DESTDIR)$(LIBDIR)' \
			'$(DESTDIR)$(MANDIR)/man3' '$(DESTDIR)$(MANDIR)/man5' \
			'$(DESTDIR)$(MANDIR)/man8'
		$(ZIC_INSTALL) -l $(LOCALTIME) -p $(POSIXRULES) \
			-t '$(DESTDIR)$(TZDEFAULT)'
		cp -f $(TABDATA) '$(DESTDIR)$(TZDIR)/.'
		cp tzselect '$(DESTDIR)$(BINDIR)/.'
		cp zdump '$(DESTDIR)$(ZDUMPDIR)/.'
		cp zic '$(DESTDIR)$(ZICDIR)/.'
		cp libtz.a '$(DESTDIR)$(LIBDIR)/.'
		$(RANLIB) '$(DESTDIR)$(LIBDIR)/libtz.a'
		cp -f newctime.3 newtzset.3 '$(DESTDIR)$(MANDIR)/man3/.'
		cp -f tzfile.5 '$(DESTDIR)$(MANDIR)/man5/.'
		cp -f tzselect.8 zdump.8 zic.8 '$(DESTDIR)$(MANDIR)/man8/.'

INSTALL:	ALL install date.1
		mkdir -p '$(DESTDIR)$(BINDIR)' '$(DESTDIR)$(MANDIR)/man1'
		cp date '$(DESTDIR)$(BINDIR)/.'
		cp -f date.1 '$(DESTDIR)$(MANDIR)/man1/.'

version:	$(VERSION_DEPS)
		{ (type git) >/dev/null 2>&1 && \
		  V=`git describe --match '[0-9][0-9][0-9][0-9][a-z]*' \
				--abbrev=7 --dirty` || \
		  V='$(VERSION)'; } && \
		printf '%s\n' "$$V" >$@.out
		mv $@.out $@

# These files can be tailored by setting BACKWARD and PACKRATDATA.
vanguard.zi main.zi rearguard.zi: $(DSTDATA_ZI_DEPS)
		$(AWK) -v DATAFORM=`expr $@ : '\(.*\).zi'` -f ziguard.awk \
		  $(TDATA) $(PACKRATDATA) >$@.out
		mv $@.out $@
# This file has a version comment that attempts to capture any tailoring
# via BACKWARD, DATAFORM, PACKRATDATA, and REDO.
tzdata.zi:	$(DATAFORM).zi version zishrink.awk
		version=`sed 1q version` && \
		  LC_ALL=C $(AWK) \
		    -v dataform='$(DATAFORM)' \
		    -v deps='$(DSTDATA_ZI_DEPS) zishrink.awk' \
		    -v redo='$(REDO)' \
		    -v version="$$version" \
		    -f zishrink.awk \
		    $(DATAFORM).zi >$@.out
		mv $@.out $@

version.h:	version
		VERSION=`cat version` && printf '%s\n' \
		  'static char const PKGVERSION[]="($(PACKAGE)) ";' \
		  "static char const TZVERSION[]=\"$$VERSION\";" \
		  'static char const REPORT_BUGS_TO[]="$(BUGEMAIL)";' \
		  >$@.out
		mv $@.out $@

zdump:		$(TZDOBJS)
		$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $(TZDOBJS) $(LDLIBS)

zic:		$(TZCOBJS)
		$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $(TZCOBJS) $(LDLIBS)

yearistype:	yearistype.sh
		cp yearistype.sh yearistype
		chmod +x yearistype

leapseconds:	$(LEAP_DEPS)
		$(AWK) -f leapseconds.awk leap-seconds.list >$@.out
		mv $@.out $@

# Arguments to pass to submakes of install_data.
# They can be overridden by later submake arguments.
INSTALLARGS = \
 BACKWARD='$(BACKWARD)' \
 DESTDIR='$(DESTDIR)' \
 LEAPSECONDS='$(LEAPSECONDS)' \
 PACKRATDATA='$(PACKRATDATA)' \
 TZDEFAULT='$(TZDEFAULT)' \
 TZDIR='$(TZDIR)' \
 YEARISTYPE='$(YEARISTYPE)' \
 ZIC='$(ZIC)'

INSTALL_DATA_DEPS = zic leapseconds yearistype tzdata.zi

# 'make install_data' installs one set of TZif files.
install_data: $(INSTALL_DATA_DEPS)
		$(ZIC_INSTALL) tzdata.zi

posix_only: $(INSTALL_DATA_DEPS)
		$(MAKE) $(INSTALLARGS) LEAPSECONDS= install_data

right_only: $(INSTALL_DATA_DEPS)
		$(MAKE) $(INSTALLARGS) LEAPSECONDS='-L leapseconds' \
			install_data

# In earlier versions of this makefile, the other two directories were
# subdirectories of $(TZDIR).  However, this led to configuration errors.
# For example, with posix_right under the earlier scheme,
# TZ='right/Australia/Adelaide' got you localtime with leap seconds,
# but gmtime without leap seconds, which led to problems with applications
# like sendmail that subtract gmtime from localtime.
# Therefore, the other two directories are now siblings of $(TZDIR).
# You must replace all of $(TZDIR) to switch from not using leap seconds
# to using them, or vice versa.
right_posix:	right_only
		rm -fr '$(DESTDIR)$(TZDIR)-leaps'
		ln -s '$(TZDIR_BASENAME)' '$(DESTDIR)$(TZDIR)-leaps' || \
		  $(MAKE) $(INSTALLARGS) TZDIR='$(TZDIR)-leaps' right_only
		$(MAKE) $(INSTALLARGS) TZDIR='$(TZDIR)-posix' posix_only

posix_right:	posix_only
		rm -fr '$(DESTDIR)$(TZDIR)-posix'
		ln -s '$(TZDIR_BASENAME)' '$(DESTDIR)$(TZDIR)-posix' || \
		  $(MAKE) $(INSTALLARGS) TZDIR='$(TZDIR)-posix' posix_only
		$(MAKE) $(INSTALLARGS) TZDIR='$(TZDIR)-leaps' right_only

# This obsolescent rule is present for backwards compatibility with
# tz releases 2014g through 2015g.  It should go away eventually.
posix_packrat: $(INSTALL_DATA_DEPS)
		$(MAKE) $(INSTALLARGS) PACKRATDATA=backzone posix_only

zones:		$(REDO)

# dummy.zd is not a real file; it is mentioned here only so that the
# top-level 'make' does not have a syntax error.
ZDS = dummy.zd
# Rule used only by submakes invoked by the $(TZS_NEW) rule.
# It is separate so that GNU 'make -j' can run instances in parallel.
$(ZDS): zdump
		./zdump -i $(TZS_CUTOFF_FLAG) '$(wd)/'$$(expr $@ : '\(.*\).zd') \
		  >$@

TZS_NEW_DEPS = tzdata.zi zdump zic
$(TZS_NEW): $(TZS_NEW_DEPS)
		rm -fr tzs$(TZS_YEAR).dir
		mkdir tzs$(TZS_YEAR).dir
		$(zic) -d tzs$(TZS_YEAR).dir tzdata.zi
		$(AWK) '/^L/{print "Link\t" $$2 "\t" $$3}' \
		   tzdata.zi | LC_ALL=C sort >$@.out
		wd=`pwd` && \
		x=`$(AWK) '/^Z/{print "tzs$(TZS_YEAR).dir/" $$2 ".zd"}' \
				tzdata.zi \
			| LC_ALL=C sort -t . -k 2,2` && \
		set x $$x && \
		shift && \
		ZDS=$$* && \
		$(MAKE) wd="$$wd" TZS_CUTOFF_FLAG="$(TZS_CUTOFF_FLAG)" \
		  ZDS="$$ZDS" $$ZDS && \
		sed 's,^TZ=".*\.dir/,TZ=",' $$ZDS >>$@.out
		rm -fr tzs$(TZS_YEAR).dir
		mv $@.out $@

# If $(TZS) exists but 'make check_tzs' fails, a maintainer should inspect the
# failed output and fix the inconsistency, perhaps by running 'make force_tzs'.
$(TZS):
		touch $@

force_tzs:	$(TZS_NEW)
		cp $(TZS_NEW) $(TZS)

libtz.a:	$(LIBOBJS)
		rm -f $@
		$(AR) -rc $@ $(LIBOBJS)
		$(RANLIB) $@

date:		$(DATEOBJS)
		$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $(DATEOBJS) $(LDLIBS)

tzselect:	tzselect.ksh version
		VERSION=`cat version` && sed \
			-e 's|#!/bin/bash|#!$(KSHELL)|g' \
			-e 's|AWK=[^}]*|AWK=$(AWK)|g' \
			-e 's|\(PKGVERSION\)=.*|\1='\''($(PACKAGE)) '\''|' \
			-e 's|\(REPORT_BUGS_TO\)=.*|\1=$(BUGEMAIL)|' \
			-e 's|TZDIR=[^}]*|TZDIR=$(TZDIR)|' \
			-e 's|\(TZVERSION\)=.*|\1='"$$VERSION"'|' \
			<$@.ksh >$@.out
		chmod +x $@.out
		mv $@.out $@

check:		check_character_set check_white_space check_links \
		  check_name_lengths check_sorted \
		  check_tables check_web check_zishrink check_tzs

check_character_set: $(ENCHILADA)
	test ! '$(UTF8_LOCALE)' || \
	! printf 'A\304\200B\n' | \
	  LC_ALL='$(UTF8_LOCALE)' grep -q '^A.B$$' >/dev/null 2>&1 || { \
		LC_ALL='$(UTF8_LOCALE)' && export LC_ALL && \
		sharp='#' && \
		! grep -Env $(SAFE_LINE) $(MANS) date.1 $(MANTXTS) \
			$(MISC) $(SOURCES) $(WEB_PAGES) \
			CONTRIBUTING LICENSE README \
			version tzdata.zi && \
		! grep -Env $(SAFE_LINE)'|^UNUSUAL_OK_'$(OK_CHAR)'*$$' \
			Makefile && \
		! grep -Env $(SAFE_SHARP_LINE) $(TDATA_TO_CHECK) backzone \
			leapseconds yearistype.sh zone.tab && \
		! grep -Env $(OK_LINE) $(ENCHILADA); \
	}
	touch $@

check_white_space: $(ENCHILADA)
		patfmt=' \t|[\f\r\v]' && pat=`printf "$$patfmt\\n"` && \
		! grep -En "$$pat" \
			$$(ls $(ENCHILADA) | grep -Fvx leap-seconds.list)
		! grep -n '[[:space:]]$$' \
			$$(ls $(ENCHILADA) | grep -Fvx leap-seconds.list)
		touch $@

PRECEDES_FILE_NAME = ^(Zone|Link[[:space:]]+[^[:space:]]+)[[:space:]]+
FILE_NAME_COMPONENT_TOO_LONG = \
  $(PRECEDES_FILE_NAME)[^[:space:]]*[^/[:space:]]{15}

check_name_lengths: $(TDATA_TO_CHECK) backzone
		! grep -En '$(FILE_NAME_COMPONENT_TOO_LONG)' \
			$(TDATA_TO_CHECK) backzone
		touch $@

CHECK_CC_LIST = { n = split($$1,a,/,/); for (i=2; i<=n; i++) print a[1], a[i]; }

check_sorted: backward backzone iso3166.tab zone.tab zone1970.tab
		$(AWK) '/^Link/ {print $$3}' backward | LC_ALL=C sort -cu
		$(AWK) '/^Zone/ {print $$2}' backzone | LC_ALL=C sort -cu
		$(AWK) '/^[^#]/ {print $$1}' iso3166.tab | LC_ALL=C sort -cu
		$(AWK) '/^[^#]/ {print $$1}' zone.tab | LC_ALL=C sort -c
		$(AWK) '/^[^#]/ {print substr($$0, 1, 2)}' zone1970.tab | \
		  LC_ALL=C sort -c
		$(AWK) '/^[^#]/ $(CHECK_CC_LIST)' zone1970.tab | \
		  LC_ALL=C sort -cu
		touch $@

check_links:	checklinks.awk $(TDATA_TO_CHECK) tzdata.zi
		$(AWK) -f checklinks.awk $(TDATA_TO_CHECK)
		$(AWK) -f checklinks.awk tzdata.zi
		touch $@

check_tables:	checktab.awk $(PRIMARY_YDATA) $(ZONETABLES)
		for tab in $(ZONETABLES); do \
		  $(AWK) -f checktab.awk -v zone_table=$$tab $(PRIMARY_YDATA) \
		    || exit; \
		done
		touch $@

check_tzs:	$(TZS) $(TZS_NEW)
		if test -s $(TZS); then \
		  diff -u $(TZS) $(TZS_NEW); \
		else \
		  cp $(TZS_NEW) $(TZS); \
		fi
		touch $@

check_web:	$(CHECK_WEB_PAGES)
check_theory.html: theory.html
check_tz-art.html: tz-art.html
check_tz-link.html: tz-link.html
check_theory.html check_tz-art.html check_tz-link.html:
		$(CURL) -sS --url https://validator.w3.org/nu/ -F out=gnu \
		    -F file=@$$(expr $@ : 'check_\(.*\)') -o $@.out && \
		  test ! -s $@.out || { cat $@.out; exit 1; }
		mv $@.out $@
check_tz-how-to.html: tz-how-to.html
		$(VALIDATE_ENV) $(VALIDATE) $(VALIDATE_FLAGS) tz-how-to.html
		touch $@

# Check that zishrink.awk does not alter the data, and that ziguard.awk
# preserves main-format data.
check_zishrink: check_zishrink_posix check_zishrink_right
check_zishrink_posix check_zishrink_right: \
  zic leapseconds $(PACKRATDATA) $(TDATA) $(DATAFORM).zi tzdata.zi
		rm -fr $@.dir $@-t.dir $@-shrunk.dir
		mkdir $@.dir $@-t.dir $@-shrunk.dir
		case $@ in \
		  *_right) leap='-L leapseconds';; \
		  *) leap=;; \
		esac && \
		  $(ZIC) $$leap -d $@.dir $(DATAFORM).zi && \
		  $(ZIC) $$leap -d $@-shrunk.dir tzdata.zi && \
		  case $(DATAFORM) in \
		    main) \
		      $(ZIC) $$leap -d $@-t.dir $(TDATA) && \
		      $(AWK) '/^Rule/' $(TDATA) | \
			$(ZIC) $$leap -d $@-t.dir - $(PACKRATDATA) && \
		      diff -r $@.dir $@-t.dir;; \
		  esac
		diff -r $@.dir $@-shrunk.dir
		rm -fr $@.dir $@-t.dir $@-shrunk.dir
		touch $@

clean_misc:
		rm -fr check_*.dir
		rm -f *.o *.out $(TIME_T_ALTERNATIVES) \
		  check_* core typecheck_* \
		  date tzselect version.h zdump zic yearistype libtz.a
clean:		clean_misc
		rm -fr *.dir tzdb-*/
		rm -f *.zi $(TZS_NEW)

maintainer-clean: clean
		@echo 'This command is intended for maintainers to use; it'
		@echo 'deletes files that may need special tools to rebuild.'
		rm -f leapseconds version $(MANTXTS) $(TZS) *.asc *.tar.*

names:
		@echo $(ENCHILADA)

public:		check check_public $(CHECK_TIME_T_ALTERNATIVES) \
		tarballs signatures

date.1.txt:	date.1
newctime.3.txt:	newctime.3
newstrftime.3.txt: newstrftime.3
newtzset.3.txt:	newtzset.3
time2posix.3.txt: time2posix.3
tzfile.5.txt:	tzfile.5
tzselect.8.txt:	tzselect.8
zdump.8.txt:	zdump.8
zic.8.txt:	zic.8

$(MANTXTS):	workman.sh
		LC_ALL=C sh workman.sh `expr $@ : '\(.*\)\.txt$$'` >$@.out
		mv $@.out $@

# Set the timestamps to those of the git repository, if available,
# and if the files have not changed since then.
# This uses GNU 'touch' syntax 'touch -d@N FILE',
# where N is the number of seconds since 1970.
# If git or GNU 'touch' is absent, don't bother to sync with git timestamps.
# Also, set the timestamp of each prebuilt file like 'leapseconds'
# to be the maximum of the files it depends on.
set-timestamps.out: $(EIGHT_YARDS)
		rm -f $@
		if (type git) >/dev/null 2>&1 && \
		   files=`git ls-files $(EIGHT_YARDS)` && \
		   touch -md @1 test.out; then \
		  rm -f test.out && \
		  for file in $$files; do \
		    if git diff --quiet $$file; then \
		      time=`git log -1 --format='tformat:%ct' $$file` && \
		      touch -cmd @$$time $$file; \
		    else \
		      echo >&2 "$$file: warning: does not match repository"; \
		    fi || exit; \
		  done; \
		fi
		touch -cmr `ls -t $(LEAP_DEPS) | sed 1q` leapseconds
		for file in `ls $(MANTXTS) | sed 's/\.txt$$//'`; do \
		  touch -cmr `ls -t $$file workman.sh | sed 1q` $$file.txt || \
		    exit; \
		done
		touch -cmr `ls -t $(TZDATA_ZI_DEPS) | sed 1q` tzdata.zi
		touch -cmr `ls -t $(VERSION_DEPS) | sed 1q` version
		touch $@
set-tzs-timestamp.out: $(TZS)
		touch -cmr `ls -t $(TZS_DEPS) | sed 1q` $(TZS)
		touch $@

# The zics below ensure that each data file can stand on its own.
# We also do an all-files run to catch links to links.

check_public: $(VERSION_DEPS)
		rm -fr public.dir
		mkdir public.dir
		ln $(VERSION_DEPS) public.dir
		cd public.dir && $(MAKE) CFLAGS='$(GCC_DEBUG_FLAGS)' ALL
		for i in $(TDATA_TO_CHECK) public.dir/tzdata.zi; do \
		  public.dir/zic -v -d public.dir/zoneinfo $$i 2>&1 || exit; \
		done
		public.dir/zic -v -d public.dir/zoneinfo-all $(TDATA_TO_CHECK)
		rm -fr public.dir
		touch $@

# Check that the code works under various alternative
# implementations of time_t.
check_time_t_alternatives: $(TIME_T_ALTERNATIVES)
$(TIME_T_ALTERNATIVES_TAIL): $(TIME_T_ALTERNATIVES_HEAD)
$(TIME_T_ALTERNATIVES): $(VERSION_DEPS)
		rm -fr $@.dir
		mkdir $@.dir
		ln $(VERSION_DEPS) $@.dir
		case $@ in \
		  int32_t) range=-2147483648,2147483648;; \
		  u*) range=0,4294967296;; \
		  *) range=-4294967296,4294967296;; \
		esac && \
		wd=`pwd` && \
		zones=`$(AWK) '/^[^#]/ { print $$3 }' <zone1970.tab` && \
		if test $@ = $(TIME_T_ALTERNATIVES_HEAD); then \
		  range_target=; \
		else \
		  range_target=to$$range.tzs; \
		fi && \
		(cd $@.dir && \
		  $(MAKE) TOPDIR="$$wd/$@.dir" \
		    CFLAGS='$(CFLAGS) -Dtime_tz='"'$@'" \
		    REDO='$(REDO)' \
			D=$$wd/$@.dir \
		    TZS_YEAR="$$range" TZS_CUTOFF_FLAG="-t $$range" \
		    install $$range_target) && \
		test $@ = $(TIME_T_ALTERNATIVES_HEAD) || { \
		  (cd $(TIME_T_ALTERNATIVES_HEAD).dir && \
		    $(MAKE) TOPDIR="$$wd/$@.dir" \
		      TZS_YEAR="$$range" TZS_CUTOFF_FLAG="-t $$range" \
			D=$$wd/$@.dir \
		      to$$range.tzs) && \
		  diff -u $(TIME_T_ALTERNATIVES_HEAD).dir/to$$range.tzs \
			  $@.dir/to$$range.tzs && \
		  if diff -q Makefile Makefile 2>/dev/null; then \
		    quiet_option='-q'; \
		  else \
		    quiet_option=''; \
		  fi && \
		    diff $$quiet_option -r $(TIME_T_ALTERNATIVES_HEAD).dir/etc \
					   $@.dir/etc && \
		    diff $$quiet_option -r \
		      $(TIME_T_ALTERNATIVES_HEAD).dir/usr/share \
		      $@.dir/usr/share; \
		}
		touch $@

TRADITIONAL_ASC = \
  tzcode$(VERSION).tar.gz.asc \
  tzdata$(VERSION).tar.gz.asc
REARGUARD_ASC = \
  tzdata$(VERSION)-rearguard.tar.gz.asc
ALL_ASC = $(TRADITIONAL_ASC) $(REARGUARD_ASC) \
  tzdb-$(VERSION).tar.lz.asc

tarballs rearguard_tarballs traditional_tarballs \
signatures rearguard_signatures traditional_signatures: \
  version set-timestamps.out rearguard.zi
		VERSION=`cat version` && \
		$(MAKE) VERSION="$$VERSION" $@_version

# These *_version rules are intended for use if VERSION is set by some
# other means.  Ordinarily these rules are used only by the above
# non-_version rules, which set VERSION on the 'make' command line.
tarballs_version: traditional_tarballs_version rearguard_tarballs_version \
  tzdb-$(VERSION).tar.lz
rearguard_tarballs_version: \
  tzdata$(VERSION)-rearguard.tar.gz
traditional_tarballs_version: \
  tzcode$(VERSION).tar.gz tzdata$(VERSION).tar.gz
signatures_version: $(ALL_ASC)
rearguard_signatures_version: $(REARGUARD_ASC)
traditional_signatures_version: $(TRADITIONAL_ASC)

tzcode$(VERSION).tar.gz: set-timestamps.out
		LC_ALL=C && export LC_ALL && \
		tar $(TARFLAGS) -cf - \
		    $(COMMON) $(DOCS) $(SOURCES) | \
		  gzip $(GZIPFLAGS) >$@.out
		mv $@.out $@

tzdata$(VERSION).tar.gz: set-timestamps.out
		LC_ALL=C && export LC_ALL && \
		tar $(TARFLAGS) -cf - $(COMMON) $(DATA) $(MISC) | \
		  gzip $(GZIPFLAGS) >$@.out
		mv $@.out $@

tzdata$(VERSION)-rearguard.tar.gz: rearguard.zi set-timestamps.out
		rm -fr tzdata$(VERSION)-rearguard.dir
		mkdir tzdata$(VERSION)-rearguard.dir
		ln $(COMMON) $(DATA) $(MISC) tzdata$(VERSION)-rearguard.dir
		cd tzdata$(VERSION)-rearguard.dir && \
		  rm -f $(TDATA) $(PACKRATDATA) version
		for f in $(TDATA) $(PACKRATDATA); do \
		  rearf=tzdata$(VERSION)-rearguard.dir/$$f; \
		  $(AWK) -v DATAFORM=rearguard -f ziguard.awk $$f >$$rearf && \
		  touch -cmr `ls -t ziguard.awk $$f` $$rearf || exit; \
		done
		sed '1s/$$/-rearguard/' \
		  <version >tzdata$(VERSION)-rearguard.dir/version
		touch -cmr version tzdata$(VERSION)-rearguard.dir/version
		LC_ALL=C && export LC_ALL && \
		  (cd tzdata$(VERSION)-rearguard.dir && \
		   tar $(TARFLAGS) -cf - $(COMMON) $(DATA) $(MISC) | \
		     gzip $(GZIPFLAGS)) >$@.out
		mv $@.out $@

tzdb-$(VERSION).tar.lz: set-timestamps.out set-tzs-timestamp.out
		rm -fr tzdb-$(VERSION)
		mkdir tzdb-$(VERSION)
		ln $(ENCHILADA) tzdb-$(VERSION)
		touch -cmr `ls -t tzdb-$(VERSION)/* | sed 1q` tzdb-$(VERSION)
		LC_ALL=C && export LC_ALL && \
		tar $(TARFLAGS) -cf - tzdb-$(VERSION) | lzip -9 >$@.out
		mv $@.out $@

tzcode$(VERSION).tar.gz.asc: tzcode$(VERSION).tar.gz
tzdata$(VERSION).tar.gz.asc: tzdata$(VERSION).tar.gz
tzdata$(VERSION)-rearguard.tar.gz.asc: tzdata$(VERSION)-rearguard.tar.gz
tzdb-$(VERSION).tar.lz.asc: tzdb-$(VERSION).tar.lz
$(ALL_ASC):
		gpg2 --armor --detach-sign $?

TYPECHECK_CFLAGS = $(CFLAGS) -DTYPECHECK -D__time_t_defined -D_TIME_T
typecheck: typecheck_long_long typecheck_unsigned
typecheck_long_long typecheck_unsigned: $(VERSION_DEPS)
		rm -fr $@.dir
		mkdir $@.dir
		ln $(VERSION_DEPS) $@.dir
		cd $@.dir && \
		  case $@ in \
		    *_long_long) i="long long";; \
		    *_unsigned ) i="unsigned" ;; \
		  esac && \
		  typecheck_cflags='' && \
		  $(MAKE) \
		    CFLAGS="$(TYPECHECK_CFLAGS) \"-Dtime_t=$$i\"" \
		    TOPDIR="`pwd`" \
		    install
		$@.dir/zdump -i -c 1970,1971 Europe/Rome
		touch $@

zonenames:	tzdata.zi
		@$(AWK) '/^Z/ { print $$2 } /^L/ { print $$3 }' tzdata.zi

asctime.o:	private.h tzfile.h
date.o:		private.h
difftime.o:	private.h
localtime.o:	private.h tzfile.h
strftime.o:	private.h tzfile.h
zdump.o:	version.h
zic.o:		private.h tzfile.h version.h

.KEEP_STATE:

.PHONY: ALL INSTALL all
.PHONY: check check_time_t_alternatives
.PHONY: check_web check_zishrink
.PHONY: clean clean_misc dummy.zd force_tzs
.PHONY: install install_data maintainer-clean names
.PHONY: posix_only posix_packrat posix_right public
.PHONY: rearguard_signatures rearguard_signatures_version
.PHONY: rearguard_tarballs rearguard_tarballs_version
.PHONY: right_only right_posix signatures signatures_version
.PHONY: tarballs tarballs_version
.PHONY: traditional_signatures traditional_signatures_version
.PHONY: traditional_tarballs traditional_tarballs_version
.PHONY: typecheck
.PHONY: zonenames zones
.PHONY: $(ZDS)
