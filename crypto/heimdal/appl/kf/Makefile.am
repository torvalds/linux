# $Id$

include $(top_srcdir)/Makefile.am.common

bin_PROGRAMS = kf

libexec_PROGRAMS = kfd

man_MANS = kf.1 kfd.8

kf_SOURCES = kf.c kf_locl.h

kfd_SOURCES = kfd.c kf_locl.h

LDADD = $(top_builddir)/lib/krb5/libkrb5.la \
	$(LIB_hcrypto) \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(LIB_roken)

EXTRA_DIST = NTMakefile $(man_MANS)
