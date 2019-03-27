# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += -I$(srcdir)/../login $(INCLUDE_hcrypto)

bin_PROGRAMS = rsh

man_MANS = rsh.1 rshd.8

libexec_PROGRAMS = rshd

rsh_SOURCES  = rsh.c common.c rsh_locl.h

rshd_SOURCES = rshd.c common.c login_access.c limits_conf.c rsh_locl.h

login_access.c:
	$(LN_S) $(srcdir)/../login/login_access.c .

limits_conf.c:
	$(LN_S) $(srcdir)/../login/limits_conf.c .

LDADD = $(LIB_kafs) \
	$(LIB_krb5) \
	$(LIB_hcrypto) \
	$(LIB_roken)

EXTRA_DIST = NTMakefile $(man_MANS)
