# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += -I$(srcdir)/../common $(INCLUDE_readline) $(INCLUDE_hcrypto)

bin_PROGRAMS = ftp

CHECK_LOCAL = 

if KRB5
krb5_sources = gssapi.c
endif

ftp_SOURCES = \
	cmds.c \
	cmdtab.c \
	extern.h \
	ftp.c \
	ftp_locl.h \
	ftp_var.h \
	main.c \
	pathnames.h \
	ruserpass.c \
	domacro.c \
	globals.c \
	security.c \
	security.h \
	kauth.c \
	$(krb5_sources)

EXTRA_ftp_SOURCES = gssapi.c

man_MANS = ftp.1

LDADD = \
	../common/libcommon.a \
	$(LIB_gssapi) \
	$(LIB_krb5) \
	$(LIB_hcrypto) \
	$(LIB_roken) \
	$(LIB_readline)

EXTRA_DIST = NTMakefile $(man_MANS)
