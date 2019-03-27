# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += $(INCLUDE_libintl) $(INCLUDE_readline) $(INCLUDE_hcrypto) -I$(srcdir)/../lib/krb5 -I$(top_builddir)/include/gssapi

sbin_PROGRAMS = kadmin

libexec_PROGRAMS = kadmind

man_MANS = kadmin.8 kadmind.8

noinst_PROGRAMS = add_random_users

dist_kadmin_SOURCES =				\
	ank.c					\
	add_enctype.c				\
	check.c					\
	cpw.c					\
	del.c					\
	del_enctype.c				\
	dump.c					\
	ext.c					\
	get.c					\
	init.c					\
	kadmin.c				\
	load.c					\
	mod.c					\
	rename.c				\
	stash.c					\
	util.c					\
	pw_quality.c				\
	random_password.c			\
	kadmin_locl.h

nodist_kadmin_SOURCES =				\
	kadmin-commands.c			\
	kadmin-commands.h

$(kadmin_OBJECTS): kadmin-commands.h

CLEANFILES = kadmin-commands.h kadmin-commands.c

kadmin-commands.c kadmin-commands.h: kadmin-commands.in
	$(SLC) $(srcdir)/kadmin-commands.in

kadmind_SOURCES =				\
	rpc.c					\
	server.c				\
	kadmind.c				\
	kadmin_locl.h				\
	kadm_conn.c

add_random_users_SOURCES = add-random-users.c

test_util_SOURCES = test_util.c util.c

TESTS = test_util

check_PROGRAMS = $(TESTS)

LDADD_common = \
	$(top_builddir)/lib/hdb/libhdb.la \
	$(top_builddir)/lib/krb5/libkrb5.la \
	$(LIB_hcrypto) \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(LIB_roken) \
	$(DBLIB)

kadmind_LDADD = $(top_builddir)/lib/kadm5/libkadm5srv.la \
	../lib/gssapi/libgssapi.la \
	$(LDADD_common) \
	$(LIB_pidfile) \
	$(LIB_dlopen)

kadmin_LDADD = \
	$(top_builddir)/lib/kadm5/libkadm5clnt.la \
	$(top_builddir)/lib/kadm5/libkadm5srv.la \
	$(top_builddir)/lib/sl/libsl.la \
	$(LIB_readline) \
	$(LDADD_common) \
	$(LIB_dlopen)

add_random_users_LDADD = \
	$(top_builddir)/lib/kadm5/libkadm5clnt.la \
	$(top_builddir)/lib/kadm5/libkadm5srv.la \
	$(LDADD_common) \
	$(LIB_dlopen)

test_util_LDADD = $(kadmin_LDADD)

EXTRA_DIST = \
	NTMakefile \
	kadmin-version.rc \
	kadmind-version.rc \
	$(man_MANS) \
	kadmin-commands.in
