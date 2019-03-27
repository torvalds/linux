# $Id$

include $(top_srcdir)/Makefile.am.common

lib_LTLIBRARIES = libkadm5srv.la libkadm5clnt.la
libkadm5srv_la_LDFLAGS = -version-info 8:1:0
libkadm5clnt_la_LDFLAGS = -version-info 7:1:0

if versionscript
libkadm5srv_la_LDFLAGS += $(LDFLAGS_VERSION_SCRIPT)$(srcdir)/version-script.map
endif

sbin_PROGRAMS = iprop-log
check_PROGRAMS = default_keys
noinst_PROGRAMS = test_pw_quality

noinst_LTLIBRARIES = sample_passwd_check.la

sample_passwd_check_la_SOURCES = sample_passwd_check.c
sample_passwd_check_la_LDFLAGS = -module

libkadm5srv_la_LIBADD = \
	$(LIB_com_err) ../krb5/libkrb5.la \
	../hdb/libhdb.la $(LIBADD_roken)
libkadm5clnt_la_LIBADD = \
	$(LIB_com_err) ../krb5/libkrb5.la $(LIBADD_roken)

libexec_PROGRAMS = ipropd-master ipropd-slave

default_keys_SOURCES = default_keys.c

kadm5includedir = $(includedir)/kadm5
buildkadm5include = $(buildinclude)/kadm5

dist_kadm5include_HEADERS = admin.h private.h kadm5-pwcheck.h
dist_kadm5include_HEADERS += kadm5-protos.h kadm5-private.h

nodist_kadm5include_HEADERS = kadm5_err.h

install-build-headers:: $(dist_kadm5include_HEADERS) $(nodist_kadm5include_HEADERS)
	@foo='$(dist_kadm5include_HEADERS) $(nodist_kadm5include_HEADERS)'; \
	for f in $$foo; do \
		f=`basename $$f`; \
		if test -f "$(srcdir)/$$f"; then file="$(srcdir)/$$f"; \
		else file="$$f"; fi; \
		if cmp -s  $$file $(buildkadm5include)/$$f 2> /dev/null ; then \
		: ; else \
			echo "cp $$file $(buildkadm5include)/$$f";\
			cp $$file $(buildkadm5include)/$$f; \
		fi ; \
	done

dist_libkadm5clnt_la_SOURCES =			\
	ad.c					\
	chpass_c.c				\
	client_glue.c				\
	common_glue.c				\
	create_c.c				\
	delete_c.c				\
	destroy_c.c				\
	flush_c.c				\
	free.c					\
	get_c.c					\
	get_princs_c.c				\
	init_c.c				\
	kadm5_locl.h				\
	marshall.c				\
	modify_c.c				\
	private.h				\
	privs_c.c				\
	randkey_c.c				\
	rename_c.c				\
	send_recv.c				\
	admin.h

nodist_libkadm5clnt_la_SOURCES =		\
	kadm5_err.c				\
	kadm5_err.h

dist_libkadm5srv_la_SOURCES =			\
	acl.c					\
	admin.h					\
	bump_pw_expire.c			\
	chpass_s.c				\
	common_glue.c				\
	context_s.c				\
	create_s.c				\
	delete_s.c				\
	destroy_s.c				\
	ent_setup.c				\
	error.c					\
	flush_s.c				\
	free.c					\
	get_princs_s.c				\
	get_s.c					\
	init_s.c				\
	kadm5_locl.h				\
	keys.c					\
	log.c					\
	marshall.c				\
	modify_s.c				\
	password_quality.c			\
	private.h				\
	privs_s.c				\
	randkey_s.c				\
	rename_s.c				\
	server_glue.c				\
	set_keys.c				\
	set_modifier.c				\
	admin.h

nodist_libkadm5srv_la_SOURCES = 		\
	kadm5_err.c				\
	kadm5_err.h

libkadm5srv_la_DEPENDENCIES =			\
	version-script.map

dist_iprop_log_SOURCES = iprop-log.c
nodist_iprop_log_SOURCES = iprop-commands.c

ipropd_master_SOURCES = ipropd_master.c ipropd_common.c iprop.h kadm5_locl.h

ipropd_slave_SOURCES = ipropd_slave.c ipropd_common.c iprop.h kadm5_locl.h

man_MANS = kadm5_pwcheck.3 iprop.8 iprop-log.8

LDADD = \
	libkadm5srv.la \
	$(top_builddir)/lib/hdb/libhdb.la \
	$(top_builddir)/lib/krb5/libkrb5.la \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(LIB_hcrypto) \
	$(LIB_roken) \
	$(DBLIB) \
	$(LIB_dlopen) \
	$(LIB_pidfile)

iprop_log_LDADD = \
	libkadm5srv.la \
	$(top_builddir)/lib/hdb/libhdb.la \
	$(top_builddir)/lib/krb5/libkrb5.la \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(LIB_hcrypto) \
	$(top_builddir)/lib/sl/libsl.la \
	$(LIB_readline) \
	$(LIB_roken) \
	$(DBLIB) \
	$(LIB_dlopen) \
	$(LIB_pidfile)


iprop-commands.c iprop-commands.h: iprop-commands.in
	$(SLC) $(srcdir)/iprop-commands.in

$(libkadm5srv_la_OBJECTS): kadm5_err.h
$(iprop_log_OBJECTS): iprop-commands.h

client_glue.lo server_glue.lo: $(srcdir)/common_glue.c

CLEANFILES = kadm5_err.c kadm5_err.h iprop-commands.h iprop-commands.c

# to help stupid solaris make

kadm5_err.h: kadm5_err.et

$(libkadm5clnt_la_OBJECTS) $(libkadm5srv_la_OBJECTS): $(srcdir)/kadm5-protos.h $(srcdir)/kadm5-private.h

proto_opts = -q -R '^(_|kadm5_c_|kadm5_s_|kadm5_log)' -P comment
$(srcdir)/kadm5-protos.h:
	cd $(srcdir); perl ../../cf/make-proto.pl $(proto_opts) \
		-o kadm5-protos.h \
		$(dist_libkadm5clnt_la_SOURCES) \
		$(dist_libkadm5srv_la_SOURCES) \
		|| rm -f kadm5-protos.h

$(srcdir)/kadm5-private.h:
	cd $(srcdir); perl ../../cf/make-proto.pl $(proto_opts) \
		-p kadm5-private.h \
		$(dist_libkadm5clnt_la_SOURCES) \
		$(dist_libkadm5srv_la_SOURCES) \
		|| rm -f kadm5-private.h

EXTRA_DIST = \
	NTMakefile \
	iprop-log-version.rc \
	ipropd-master-version.rc \
	ipropd-slave-version.rc \
	libkadm5srv-version.rc \
	libkadm5srv-exports.def \
	kadm5_err.et \
	iprop-commands.in \
	$(man_MANS) \
	check-cracklib.pl \
	flush.c \
	sample_passwd_check.c \
	version-script.map
