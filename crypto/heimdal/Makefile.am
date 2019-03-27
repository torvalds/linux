# $Id$

include $(top_srcdir)/Makefile.am.common

if KCM
kcm_dir = kcm
endif

SUBDIRS=  include base lib kuser kdc admin kadmin kpasswd 
SUBDIRS+= $(kcm_dir) appl tools tests packages etc po

if HEIMDAL_DOCUMENTATION
SUBDIRS+= doc
endif



## ACLOCAL = @ACLOCAL@ -I cf
ACLOCAL_AMFLAGS = -I cf

EXTRA_DIST = \
	NTMakefile \
	windows \
	TODO \
	LICENSE \
	README \
	ChangeLog \
	ChangeLog.1998 \
	ChangeLog.1999 \
	ChangeLog.2000 \
	ChangeLog.2001 \
	ChangeLog.2002 \
	ChangeLog.2003 \
	ChangeLog.2004 \
	ChangeLog.2005 \
	ChangeLog.2006 \
	Makefile.am.common \
	autogen.sh \
	krb5.conf \
	cf/make-proto.pl \
	cf/install-catman.sh \
	cf/ChangeLog \
	cf/c-function.m4 \
	cf/ChangeLog \
	cf/have-pragma-weak.m4 \
	cf/have-types.m4 \
	cf/krb-func-getcwd-broken.m4 \
	cf/krb-prog-ranlib.m4 \
	cf/krb-prog-yacc.m4 \
	cf/krb-sys-aix.m4 \
	cf/krb-sys-nextstep.m4 \
	cf/krb-version.m4 \
	cf/roken.m4 \
	cf/valgrind-suppressions \
	cf/vararray.m4

print-distdir:
	@echo $(distdir)
