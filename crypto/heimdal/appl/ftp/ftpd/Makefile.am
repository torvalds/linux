# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += -I$(srcdir)/../common $(INCLUDE_krb4) -DFTP_SERVER

libexec_PROGRAMS = ftpd

CHECK_LOCAL = 

if KRB5
krb5_sources = gssapi.c gss_userok.c
endif

ftpd_SOURCES =		\
	extern.h	\
	ftpcmd.y	\
	ftpd.c		\
	ftpd_locl.h	\
	logwtmp.c	\
	ls.c		\
	pathnames.h	\
	popen.c		\
	security.c	\
	kauth.c		\
	klist.c		\
	$(krb4_sources) \
	$(krb5_sources)

EXTRA_ftpd_SOURCES = kauth.c gssapi.c gss_userok.c

$(ftpd_OBJECTS): security.h

security.c:
	@test -f security.c || $(LN_S) $(srcdir)/../ftp/security.c .
security.h:
	@test -f security.h || $(LN_S) $(srcdir)/../ftp/security.h .
gssapi.c:
	@test -f gssapi.c || $(LN_S) $(srcdir)/../ftp/gssapi.c .

CLEANFILES = security.c security.h gssapi.c

man_MANS = ftpd.8 ftpusers.5

LDADD = ../common/libcommon.a \
	$(LIB_otp) \
	$(LIB_gssapi) \
	$(LIB_krb5) \
	$(LIB_kafs) \
	$(LIB_krb4) \
	$(LIB_hcrypto) \
	$(LIB_roken)

EXTRA_DIST = NTMakefile $(man_MANS)
