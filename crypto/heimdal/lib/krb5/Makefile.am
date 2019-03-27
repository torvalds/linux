# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += $(INCLUDE_krb4) $(INCLUDE_hcrypto) -I../com_err -I$(srcdir)/../com_err $(INCLUDE_sqlite3) $(INCLUDE_libintl)

bin_PROGRAMS = verify_krb5_conf

noinst_PROGRAMS =				\
	krbhst-test				\
	test_gic				\
	test_alname				\
	test_crypto				\
	test_rfc3961				\
	test_get_addrs				\
	test_kuserok				\
	test_renew				\
	test_forward

noinst_LTLIBRARIES =				\
	librfc3961.la

TESTS =						\
	aes-test				\
	derived-key-test			\
	n-fold-test				\
	parse-name-test				\
	store-test				\
	string-to-key-test			\
	test_acl				\
	test_addr				\
	test_cc					\
	test_config				\
	test_fx					\
	test_prf				\
	test_store				\
	test_crypto_wrapping			\
	test_keytab				\
	test_mem				\
	test_pac				\
	test_plugin				\
	test_princ				\
	test_pkinit_dh2key			\
	test_pknistkdf				\
	test_time				\
	test_x500

check_DATA = test_config_strings.out

check_PROGRAMS = $(TESTS) test_hostname test_ap-req

LDADD = libkrb5.la \
	$(LIB_hcrypto) \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(top_builddir)/lib/wind/libwind.la \
	$(LIB_roken)

if PKINIT
LIB_pkinit = ../hx509/libhx509.la
endif

if have_scc
use_sqlite = $(LIB_sqlite3)
endif

libkrb5_la_LIBADD = \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(top_builddir)/lib/ipc/libheim-ipcc.la \
	$(top_builddir)/lib/wind/libwind.la \
	$(top_builddir)/base/libheimbase.la \
	$(LIB_pkinit) \
	$(use_sqlite) \
	$(LIB_com_err) \
	$(LIB_hcrypto) \
	$(LIB_libintl) \
	$(LIBADD_roken) \
	$(PTHREAD_LIBADD) \
	$(LIB_door_create) \
	$(LIB_dlopen)

librfc3961_la_LIBADD = \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(top_builddir)/lib/ipc/libheim-ipcc.la \
	$(top_builddir)/lib/wind/libwind.la \
	$(LIB_pkinit) \
	$(use_sqlite) \
	$(LIB_com_err) \
	$(LIB_hcrypto) \
	$(LIB_libintl) \
	$(LIBADD_roken) \
	$(PTHREAD_LIBADD) \
	$(LIB_door_create) \
	$(LIB_dlopen)

lib_LTLIBRARIES = libkrb5.la

ERR_FILES = krb5_err.c krb_err.c heim_err.c k524_err.c

libkrb5_la_CPPFLAGS = \
	-DBUILD_KRB5_LIB \
	$(AM_CPPFLAGS) \
	-DHEIMDAL_LOCALEDIR='"$(localedir)"'

librfc3961_la_CPPFLAGS = \
	-DBUILD_KRB5_LIB \
	$(AM_CPPFLAGS) \
	-DHEIMDAL_LOCALEDIR='"$(localedir)"'

dist_libkrb5_la_SOURCES =			\
	acache.c				\
	acl.c					\
	add_et_list.c				\
	addr_families.c				\
	aname_to_localname.c			\
	appdefault.c				\
	asn1_glue.c				\
	auth_context.c				\
	build_ap_req.c				\
	build_auth.c				\
	cache.c					\
	changepw.c				\
	codec.c					\
	config_file.c				\
	convert_creds.c				\
	constants.c				\
	context.c				\
	copy_host_realm.c			\
	crc.c					\
	creds.c					\
	crypto.c				\
	crypto.h				\
	crypto-aes.c				\
	crypto-algs.c				\
	crypto-arcfour.c			\
	crypto-des.c				\
	crypto-des-common.c			\
	crypto-des3.c				\
	crypto-evp.c				\
	crypto-null.c				\
	crypto-pk.c				\
	crypto-rand.c				\
	doxygen.c				\
	data.c					\
	deprecated.c				\
	digest.c				\
	eai_to_heim_errno.c			\
	error_string.c				\
	expand_hostname.c			\
	expand_path.c				\
	fcache.c				\
	free.c					\
	free_host_realm.c			\
	generate_seq_number.c			\
	generate_subkey.c			\
	get_addrs.c				\
	get_cred.c				\
	get_default_principal.c			\
	get_default_realm.c			\
	get_for_creds.c				\
	get_host_realm.c			\
	get_in_tkt.c				\
	get_port.c				\
	init_creds.c				\
	init_creds_pw.c				\
	kcm.c					\
	kcm.h					\
	keyblock.c				\
	keytab.c				\
	keytab_any.c				\
	keytab_file.c				\
	keytab_keyfile.c			\
	keytab_memory.c				\
	krb5_locl.h				\
	krb5-v4compat.h				\
	krbhst.c				\
	kuserok.c				\
	log.c					\
	mcache.c				\
	misc.c					\
	mk_error.c				\
	mk_priv.c				\
	mk_rep.c				\
	mk_req.c				\
	mk_req_ext.c				\
	mk_safe.c				\
	mit_glue.c				\
	net_read.c				\
	net_write.c				\
	n-fold.c				\
	pac.c					\
	padata.c				\
	pcache.c				\
	pkinit.c				\
	principal.c				\
	prog_setup.c				\
	prompter_posix.c			\
	rd_cred.c				\
	rd_error.c				\
	rd_priv.c				\
	rd_rep.c				\
	rd_req.c				\
	rd_safe.c				\
	read_message.c				\
	recvauth.c				\
	replay.c				\
	salt.c					\
	salt-aes.c				\
	salt-arcfour.c				\
	salt-des.c				\
	salt-des3.c				\
	scache.c				\
	send_to_kdc.c				\
	sendauth.c				\
	set_default_realm.c			\
	sock_principal.c			\
	store.c					\
	store-int.c				\
	store-int.h				\
	store_emem.c				\
	store_fd.c				\
	store_mem.c				\
	plugin.c				\
	ticket.c				\
	time.c					\
	transited.c				\
	verify_init.c				\
	verify_user.c				\
	version.c				\
	warn.c					\
	write_message.c

nodist_libkrb5_la_SOURCES =			\
	$(ERR_FILES)

libkrb5_la_DEPENDENCIES =			\
	version-script.map

libkrb5_la_LDFLAGS = -version-info 26:0:0

if versionscript
libkrb5_la_LDFLAGS += $(LDFLAGS_VERSION_SCRIPT)$(srcdir)/version-script.map
endif

$(libkrb5_la_OBJECTS) $(verify_krb5_conf_OBJECTS) $(librfc3961_la_OBJECTS): $(srcdir)/krb5-protos.h $(srcdir)/krb5-private.h krb5_err.h heim_err.h k524_err.h krb5_err.h krb_err.h k524_err.h

librfc3961_la_SOURCES =				\
	crc.c					\
	crypto.c				\
	crypto.h				\
	crypto-aes.c				\
	crypto-algs.c				\
	crypto-arcfour.c			\
	crypto-des.c				\
	crypto-des-common.c			\
	crypto-des3.c				\
	crypto-evp.c				\
	crypto-null.c				\
	crypto-pk.c				\
	crypto-rand.c				\
	crypto-stubs.c				\
	data.c					\
	error_string.c				\
	keyblock.c				\
	n-fold.c				\
	salt.c					\
	salt-aes.c				\
	salt-arcfour.c				\
	salt-des.c				\
	salt-des3.c				\
	store-int.c				\
	warn.c

test_rfc3961_LDADD = 				\
	librfc3961.la 				\
	$(top_builddir)/lib/asn1/libasn1.la	\
	$(top_builddir)/lib/wind/libwind.la 	\
	$(LIB_hcrypto)				\
	$(LIB_roken)

$(srcdir)/krb5-protos.h:
	cd $(srcdir) && perl ../../cf/make-proto.pl -E KRB5_LIB -q -P comment -o krb5-protos.h $(dist_libkrb5_la_SOURCES) || rm -f krb5-protos.h

$(srcdir)/krb5-private.h:
	cd $(srcdir) && perl ../../cf/make-proto.pl -q -P comment -p krb5-private.h $(dist_libkrb5_la_SOURCES) || rm -f krb5-private.h

man_MANS =					\
	kerberos.8				\
	krb5.conf.5				\
	krb524_convert_creds_kdc.3		\
	krb5_425_conv_principal.3		\
	krb5_acl_match_file.3			\
	krb5_aname_to_localname.3		\
	krb5_appdefault.3			\
	krb5_auth_context.3			\
	krb5_c_make_checksum.3			\
	krb5_check_transited.3			\
	krb5_create_checksum.3			\
	krb5_creds.3				\
	krb5_digest.3				\
	krb5_eai_to_heim_errno.3		\
	krb5_encrypt.3				\
	krb5_find_padata.3			\
	krb5_generate_random_block.3		\
	krb5_get_all_client_addrs.3		\
	krb5_get_credentials.3			\
	krb5_get_creds.3			\
	krb5_get_forwarded_creds.3		\
	krb5_get_in_cred.3			\
	krb5_get_init_creds.3			\
	krb5_get_krbhst.3			\
	krb5_getportbyname.3			\
	krb5_init_context.3			\
	krb5_is_thread_safe.3			\
	krb5_krbhst_init.3			\
	krb5_mk_req.3				\
	krb5_mk_safe.3				\
	krb5_openlog.3				\
	krb5_parse_name.3			\
	krb5_principal.3			\
	krb5_rcache.3				\
	krb5_rd_error.3				\
	krb5_rd_safe.3				\
	krb5_set_default_realm.3		\
	krb5_set_password.3			\
	krb5_string_to_key.3			\
	krb5_timeofday.3			\
	krb5_verify_init_creds.3		\
	krb5_verify_user.3			\
	verify_krb5_conf.8

dist_include_HEADERS = \
	krb5.h \
	krb5-protos.h \
	krb5-private.h \
	krb5_ccapi.h

nodist_include_HEADERS = krb5_err.h heim_err.h k524_err.h

# XXX use nobase_include_HEADERS = krb5/locate_plugin.h
krb5dir = $(includedir)/krb5
krb5_HEADERS = locate_plugin.h send_to_kdc_plugin.h ccache_plugin.h

build_HEADERZ = \
	$(krb5_HEADERS) \
	krb_err.h

CLEANFILES = \
	test_config_strings.out \
	test-store-data \
	krb5_err.c krb5_err.h \
	krb_err.c krb_err.h \
	heim_err.c heim_err.h \
	k524_err.c k524_err.h

$(libkrb5_la_OBJECTS): krb5_err.h krb_err.h heim_err.h k524_err.h

test_config_strings.out: test_config_strings.cfg
	$(CP) $(srcdir)/test_config_strings.cfg test_config_strings.out

EXTRA_DIST = \
	NTMakefile \
	verify_krb5_conf-version.rc \
	krb5_err.et \
	krb_err.et \
	heim_err.et \
	k524_err.et \
	$(man_MANS) \
	version-script.map \
	test_config_strings.cfg \
	krb5.moduli

#sysconf_DATA = krb5.moduli

# to help stupid solaris make

krb5_err.h: krb5_err.et

krb_err.h: krb_err.et

heim_err.h: heim_err.et

k524_err.h: k524_err.et
