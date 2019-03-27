# $Id$

include $(top_srcdir)/Makefile.am.common

man_MANS = login.1 login.access.5

bin_PROGRAMS = login

login_SOURCES =					\
		conf.c				\
		env.c				\
		login.c				\
		login_access.c			\
		login_locl.h			\
		login-protos.h			\
		loginpaths.h			\
		limits_conf.c			\
		osfc2.c				\
		read_string.c			\
		shadow.c			\
		stty_default.c			\
		tty.c				\
		utmp_login.c			\
		utmpx_login.c

LDADD = $(LIB_otp) \
	$(LIB_kafs) \
	$(top_builddir)/lib/krb5/libkrb5.la \
	$(LIB_hcrypto) \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(LIB_roken) \
	$(LIB_security) \
	$(DBLIB)

$(srcdir)/login-protos.h:
	cd $(srcdir); perl ../../cf/make-proto.pl -o login-protos.h -q -P comment $(login_SOURCES) || rm -f login-protos.h

$(login_OBJECTS): $(srcdir)/login-protos.h

EXTRA_DIST = NTMakefile $(man_MANS)
