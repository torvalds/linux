#       @(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD$

.include <src.opts.mk>

# To install on versions prior to BSD 4.4 the following may have to be
# defined with CFLAGS +=
#
# -DNET2_STAT	Use NET2 or older stat structure. The version of the
# 		stat structure is easily determined by looking at the
# 		basic type of an off_t (often defined in the file:
# 		/usr/include/sys/types.h). If off_t is a long (and is
# 		NOT A quad) then you must define NET2_STAT.
# 		This define is important, as if you do have a quad_t
# 		off_t and define NET2_STAT, pax will compile but will
# 		NOT RUN PROPERLY.
#
# -DNET2_FTS	Use the older NET2 fts. To identify the version,
# 		examine the file: /usr/include/fts.h. If FTS_COMFOLLOW
# 		is not defined then you must define NET2_FTS.
# 		Pax may not compile if this not (un)defined properly.
#
# -DNET2_REGEX	Use the older regexp.h not regex.h. The regex version
# 		is determined by looking at the value returned by
# 		regexec() (man 3 regexec). If regexec return a 1 for
# 		success (and NOT a 0 for success) you have the older
# 		regex routines and must define NET2_REGEX.
# 		Pax may not compile if this not (un)defined properly.

PACKAGE=runtime
PROG=   pax
SRCS=	ar_io.c ar_subs.c buf_subs.c cache.c cpio.c file_subs.c ftree.c \
	gen_subs.c getoldopt.c options.c pat_rep.c pax.c sel_subs.c \
	tables.c tar.c tty_subs.c

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
