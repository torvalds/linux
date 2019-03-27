# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += $(INCLUDE_hcrypto)

lib_LTLIBRARIES = libheimntlm.la

dist_include_HEADERS = heimntlm.h heimntlm-protos.h

nodist_include_HEADERS = ntlm_err.h

dist_libheimntlm_la_SOURCES = ntlm.c heimntlm.h

nodist_libheimntlm_la_SOURCES = ntlm_err.c

libheimntlm_la_LDFLAGS = -version-info 1:0:1

if versionscript
libheimntlm_la_LDFLAGS += $(LDFLAGS_VERSION_SCRIPT)$(srcdir)/version-script.map
endif
$(libheimntlm_la_OBJECTS): $(srcdir)/version-script.map

libheimntlm_la_LIBADD = \
	../krb5/libkrb5.la \
	$(LIB_hcrypto) \
	$(LIBADD_roken)

$(srcdir)/heimntlm-protos.h:
	cd $(srcdir) && perl ../../cf/make-proto.pl -q -P comment -o heimntlm-protos.h $(dist_libheimntlm_la_SOURCES) || rm -f heimntlm-protos.h

$(libheimntlm_la_OBJECTS): $(srcdir)/heimntlm-protos.h ntlm_err.h

TESTS = test_ntlm

check_PROGRAMS = test_ntlm

LDADD = libheimntlm.la $(LIB_roken)

EXTRA_DIST = \
	NTMakefile \
	libheimntlm-version.rc \
	libheimntlm-exports.def \
	version-script.map \
	ntlm_err.et

CLEANFILES = \
	ntlm_err.c ntlm_err.h

ntlm_err.h: ntlm_err.et
