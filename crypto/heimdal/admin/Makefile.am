# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += $(INCLUDE_readline) $(INCLUDE_hcrypto)

man_MANS = ktutil.8

sbin_PROGRAMS = ktutil

dist_ktutil_SOURCES =				\
	add.c					\
	change.c				\
	copy.c					\
	destroy.c				\
	get.c					\
	ktutil.c				\
	ktutil_locl.h				\
	list.c					\
	purge.c					\
	remove.c				\
	rename.c

nodist_ktutil_SOURCES =				\
	ktutil-commands.c

$(ktutil_OBJECTS): ktutil-commands.h

CLEANFILES = ktutil-commands.h ktutil-commands.c

ktutil-commands.c ktutil-commands.h: ktutil-commands.in
	$(SLC) $(srcdir)/ktutil-commands.in

LDADD = \
	$(top_builddir)/lib/kadm5/libkadm5clnt.la \
	$(top_builddir)/lib/krb5/libkrb5.la \
	$(LIB_hcrypto) \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(top_builddir)/lib/sl/libsl.la \
	$(LIB_readline) \
	$(LIB_roken)

EXTRA_DIST = NTMakefile ktutil-version.rc $(man_MANS) ktutil-commands.in
