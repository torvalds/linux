# $Id$

include $(top_srcdir)/Makefile.am.common

AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS += \
	-I$(srcdir)/../krb5 \
	-I$(srcdir) \
	-I$(srcdir)/gssapi \
	-I$(srcdir)/mech \
	-I$(srcdir)/ntlm \
	-I$(srcdir)/krb5 \
	-I$(srcdir)/spnego \
	$(INCLUDE_libintl) \
	$(INCLUDE_hcrypto) \
	$(INCLUDE_krb4)

lib_LTLIBRARIES = libgssapi.la

krb5src = \
	krb5/8003.c \
	krb5/accept_sec_context.c \
	krb5/acquire_cred.c \
	krb5/add_cred.c \
	krb5/address_to_krb5addr.c \
	krb5/aeap.c \
	krb5/arcfour.c \
	krb5/canonicalize_name.c \
	krb5/creds.c \
	krb5/ccache_name.c \
	krb5/cfx.c \
	krb5/cfx.h \
	krb5/compare_name.c \
	krb5/compat.c \
	krb5/context_time.c \
	krb5/copy_ccache.c \
	krb5/decapsulate.c \
	krb5/delete_sec_context.c \
	krb5/display_name.c \
	krb5/display_status.c \
	krb5/duplicate_name.c \
	krb5/encapsulate.c \
	krb5/export_name.c \
	krb5/export_sec_context.c \
	krb5/external.c \
	krb5/get_mic.c \
	krb5/gsskrb5_locl.h \
	krb5/gsskrb5-private.h \
	krb5/import_name.c \
	krb5/import_sec_context.c \
	krb5/indicate_mechs.c \
	krb5/init.c \
	krb5/init_sec_context.c \
	krb5/inquire_context.c \
	krb5/inquire_cred.c \
	krb5/inquire_cred_by_mech.c \
	krb5/inquire_cred_by_oid.c \
	krb5/inquire_mechs_for_name.c \
	krb5/inquire_names_for_mech.c \
	krb5/inquire_sec_context_by_oid.c \
	krb5/pname_to_uid.c \
	krb5/process_context_token.c \
	krb5/prf.c \
	krb5/release_buffer.c \
	krb5/release_cred.c \
	krb5/release_name.c \
	krb5/sequence.c \
	krb5/store_cred.c \
	krb5/set_cred_option.c \
	krb5/set_sec_context_option.c \
	krb5/ticket_flags.c \
	krb5/unwrap.c \
	krb5/authorize_localname.c \
	krb5/verify_mic.c \
	krb5/wrap.c

mechsrc = \
	mech/context.h \
	mech/context.c \
	mech/cred.h \
	mech/compat.h \
	mech/doxygen.c \
	mech/gss_accept_sec_context.c \
	mech/gss_acquire_cred.c \
	mech/gss_acquire_cred_ext.c \
	mech/gss_acquire_cred_with_password.c \
	mech/gss_add_cred.c \
	mech/gss_add_cred_with_password.c \
	mech/gss_add_oid_set_member.c \
	mech/gss_aeap.c \
	mech/gss_buffer_set.c \
	mech/gss_canonicalize_name.c \
	mech/gss_compare_name.c \
	mech/gss_context_time.c \
	mech/gss_create_empty_oid_set.c \
	mech/gss_cred.c \
	mech/gss_decapsulate_token.c \
	mech/gss_delete_name_attribute.c \
	mech/gss_delete_sec_context.c \
	mech/gss_display_name.c \
	mech/gss_display_name_ext.c \
	mech/gss_display_status.c \
	mech/gss_duplicate_name.c \
	mech/gss_duplicate_oid.c \
	mech/gss_encapsulate_token.c \
	mech/gss_export_name.c \
	mech/gss_export_name_composite.c \
	mech/gss_export_sec_context.c \
	mech/gss_get_mic.c \
	mech/gss_get_name_attribute.c \
	mech/gss_import_name.c \
	mech/gss_import_sec_context.c \
	mech/gss_indicate_mechs.c \
	mech/gss_init_sec_context.c \
	mech/gss_inquire_context.c \
	mech/gss_inquire_cred.c \
	mech/gss_inquire_cred_by_mech.c \
	mech/gss_inquire_cred_by_oid.c \
	mech/gss_inquire_mechs_for_name.c \
	mech/gss_inquire_name.c \
	mech/gss_inquire_names_for_mech.c \
	mech/gss_krb5.c \
	mech/gss_mech_switch.c \
	mech/gss_mo.c \
	mech/gss_names.c \
	mech/gss_oid.c \
	mech/gss_oid_equal.c \
	mech/gss_oid_to_str.c \
	mech/gss_pname_to_uid.c \
	mech/gss_process_context_token.c \
	mech/gss_pseudo_random.c \
	mech/gss_release_buffer.c \
	mech/gss_release_cred.c \
	mech/gss_release_name.c \
	mech/gss_release_oid.c \
	mech/gss_release_oid_set.c \
	mech/gss_seal.c \
	mech/gss_set_cred_option.c \
	mech/gss_set_name_attribute.c \
	mech/gss_set_sec_context_option.c \
	mech/gss_sign.c \
	mech/gss_store_cred.c \
	mech/gss_test_oid_set_member.c \
	mech/gss_unseal.c \
	mech/gss_unwrap.c \
	mech/gss_authorize_localname.c \
	mech/gss_utils.c \
	mech/gss_verify.c \
	mech/gss_verify_mic.c \
	mech/gss_wrap.c \
	mech/gss_wrap_size_limit.c \
	mech/gss_inquire_sec_context_by_oid.c \
	mech/mech_switch.h \
	mech/mechqueue.h \
	mech/mech_locl.h \
	mech/name.h \
	mech/utils.h

spnegosrc = \
	spnego/accept_sec_context.c \
	spnego/compat.c \
	spnego/context_stubs.c \
	spnego/cred_stubs.c \
	spnego/external.c \
	spnego/init_sec_context.c \
	spnego/spnego_locl.h \
	spnego/spnego-private.h

ntlmsrc = \
	ntlm/accept_sec_context.c \
	ntlm/acquire_cred.c \
	ntlm/add_cred.c \
	ntlm/canonicalize_name.c \
	ntlm/compare_name.c \
	ntlm/context_time.c \
	ntlm/creds.c \
	ntlm/crypto.c \
	ntlm/delete_sec_context.c \
	ntlm/display_name.c \
	ntlm/display_status.c \
	ntlm/duplicate_name.c \
	ntlm/export_name.c \
	ntlm/export_sec_context.c \
	ntlm/external.c \
	ntlm/ntlm.h \
	ntlm/ntlm-private.h \
	ntlm/import_name.c \
	ntlm/import_sec_context.c \
	ntlm/indicate_mechs.c \
	ntlm/init_sec_context.c \
	ntlm/inquire_context.c \
	ntlm/inquire_cred_by_mech.c \
	ntlm/inquire_mechs_for_name.c \
	ntlm/inquire_names_for_mech.c \
	ntlm/inquire_sec_context_by_oid.c \
	ntlm/iter_cred.c \
	ntlm/process_context_token.c \
	ntlm/release_cred.c \
	ntlm/release_name.c \
	ntlm/kdc.c

$(srcdir)/ntlm/ntlm-private.h:
	cd $(srcdir) && perl ../../cf/make-proto.pl -q -P comment -p ntlm/ntlm-private.h $(ntlmsrc) || rm -f ntlm/ntlm-private.h

dist_libgssapi_la_SOURCES  = \
	$(krb5src) \
	$(mechsrc) \
	$(ntlmsrc) \
	$(spnegosrc)

nodist_libgssapi_la_SOURCES  = \
	gkrb5_err.c \
	gkrb5_err.h \
	$(BUILT_SOURCES)

libgssapi_la_DEPENDENCIES = version-script.map

libgssapi_la_LDFLAGS = -version-info 3:0:0

if versionscript
libgssapi_la_LDFLAGS += $(LDFLAGS_VERSION_SCRIPT)$(srcdir)/version-script.map
endif

libgssapi_la_LIBADD = \
	$(top_builddir)/lib/ntlm/libheimntlm.la \
	$(top_builddir)/lib/krb5/libkrb5.la \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(LIB_com_err) \
	$(LIB_hcrypto) \
	$(LIBADD_roken)

man_MANS = gssapi.3 gss_acquire_cred.3 mech/mech.5

include_HEADERS = gssapi.h
noinst_HEADERS = \
	gssapi_mech.h \
	ntlm/ntlm-private.h \
	spnego/spnego-private.h \
	krb5/gsskrb5-private.h

nobase_include_HEADERS = \
	gssapi/gssapi.h \
	gssapi/gssapi_krb5.h \
	gssapi/gssapi_ntlm.h \
	gssapi/gssapi_oid.h \
	gssapi/gssapi_spnego.h

gssapidir = $(includedir)/gssapi
nodist_gssapi_HEADERS = gkrb5_err.h

gssapi_files = asn1_GSSAPIContextToken.x

spnego_files =					\
	asn1_ContextFlags.x			\
	asn1_MechType.x				\
	asn1_MechTypeList.x			\
	asn1_NegotiationToken.x			\
	asn1_NegotiationTokenWin.x		\
	asn1_NegHints.x				\
	asn1_NegTokenInit.x			\
	asn1_NegTokenInitWin.x			\
	asn1_NegTokenResp.x

BUILTHEADERS = \
	$(srcdir)/krb5/gsskrb5-private.h \
	$(srcdir)/spnego/spnego-private.h \
	$(srcdir)/ntlm/ntlm-private.h

$(libgssapi_la_OBJECTS): $(BUILTHEADERS)
$(test_context_OBJECTS): $(BUILTHEADERS)

$(libgssapi_la_OBJECTS): $(srcdir)/version-script.map

BUILT_SOURCES = $(spnego_files:.x=.c) $(gssapi_files:.x=.c)

CLEANFILES = $(BUILT_SOURCES) \
	gkrb5_err.h gkrb5_err.c \
	$(spnego_files) spnego_asn1*.h* spnego_asn1_files spnego_asn1-template.c \
	$(gssapi_files) gssapi_asn1*.h* gssapi_asn1_files gssapi_asn1-template.c \
	gss-commands.h gss-commands.c

$(spnego_files) spnego_asn1.hx spnego_asn1-priv.hx: spnego_asn1_files
$(gssapi_files) gssapi_asn1.hx gssapi_asn1-priv.hx: gssapi_asn1_files

spnego_asn1_files: $(ASN1_COMPILE_DEP) $(srcdir)/spnego/spnego.asn1 $(srcdir)/spnego/spnego.opt
	$(ASN1_COMPILE) --option-file=$(srcdir)/spnego/spnego.opt $(srcdir)/spnego/spnego.asn1 spnego_asn1

gssapi_asn1_files: $(ASN1_COMPILE_DEP) $(srcdir)/mech/gssapi.asn1
	$(ASN1_COMPILE) $(srcdir)/mech/gssapi.asn1 gssapi_asn1

$(srcdir)/krb5/gsskrb5-private.h:
	cd $(srcdir) && perl ../../cf/make-proto.pl -q -P comment -p krb5/gsskrb5-private.h $(krb5src) || rm -f krb5/gsskrb5-private.h

$(srcdir)/spnego/spnego-private.h:
	cd $(srcdir) && perl ../../cf/make-proto.pl -q -P comment -p spnego/spnego-private.h $(spnegosrc) || rm -f spnego/spnego-private.h


TESTS = test_oid test_names test_cfx
# test_sequence 

test_cfx_SOURCES = krb5/test_cfx.c

check_PROGRAMS = test_acquire_cred $(TESTS)

bin_PROGRAMS = gsstool
noinst_PROGRAMS = test_cred test_kcred test_context test_ntlm

test_context_SOURCES = test_context.c test_common.c test_common.h
test_ntlm_SOURCES = test_ntlm.c test_common.c test_common.h
test_acquire_cred_SOURCES = test_acquire_cred.c test_common.c test_common.h

test_ntlm_LDADD = \
	$(top_builddir)/lib/ntlm/libheimntlm.la \
	$(LDADD)

LDADD = libgssapi.la \
	$(top_builddir)/lib/krb5/libkrb5.la \
	$(LIB_roken)

# gss

dist_gsstool_SOURCES = gsstool.c
nodist_gsstool_SOURCES = gss-commands.c gss-commands.h

gsstool_LDADD = libgssapi.la \
	$(top_builddir)/lib/sl/libsl.la \
	$(top_builddir)/lib/krb5/libkrb5.la \
	$(LIB_readline) \
	$(LIB_roken)

gss-commands.c gss-commands.h: gss-commands.in
	$(SLC) $(srcdir)/gss-commands.in

$(gsstool_OBJECTS): gss-commands.h

EXTRA_DIST = \
	NTMakefile \
	libgssapi-version.rc \
	libgssapi-exports.def \
	$(man_MANS) \
	krb5/gkrb5_err.et \
	mech/gssapi.asn1 \
	spnego/spnego.asn1 \
	spnego/spnego.opt \
	version-script.map \
	gss-commands.in

$(libgssapi_la_OBJECTS): gkrb5_err.h gssapi_asn1.h gssapi_asn1-priv.h
$(libgssapi_la_OBJECTS): spnego_asn1.h spnego_asn1-priv.h
$(libgssapi_la_OBJECTS): $(srcdir)/gssapi/gssapi_oid.h

gkrb5_err.h gkrb5_err.c: $(srcdir)/krb5/gkrb5_err.et
	$(COMPILE_ET) $(srcdir)/krb5/gkrb5_err.et

$(srcdir)/gssapi/gssapi_oid.h $(srcdir)/mech/gss_oid.c:
	perl $(srcdir)/gen-oid.pl -b base -h $(srcdir)/oid.txt > $(srcdir)/gssapi/gssapi_oid.h
	perl $(srcdir)/gen-oid.pl -b base $(srcdir)/oid.txt > $(srcdir)/mech/gss_oid.c
