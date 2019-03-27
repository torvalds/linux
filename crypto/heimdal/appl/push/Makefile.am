# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += $(INCLUDE_hesiod)

bin_SCRIPTS		= pfrom

libexec_PROGRAMS	= push

push_SOURCES = push.c push_locl.h

pfrom: pfrom.in
	sed -e "s!%libexecdir%!$(libexecdir)!" $(srcdir)/pfrom.in > $@
	chmod +x $@

man_MANS = push.8 pfrom.1

CLEANFILES = pfrom

EXTRA_DIST = NTMakefile pfrom.in $(man_MANS)

LDADD = $(LIB_krb5) \
	$(LIB_hcrypto) \
	$(LIB_roken) \
	$(LIB_hesiod)
