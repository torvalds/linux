include $(top_srcdir)/Makefile.am.common

lib_LTLIBRARIES = libhx509.la
libhx509_la_LDFLAGS = -version-info 5:0:0

BUILT_SOURCES =				\
	sel-gram.h			\
	$(gen_files_ocsp:.x=.c)		\
	$(gen_files_pkcs10:.x=.c)	\
	hx509_err.c			\
	hx509_err.h

gen_files_ocsp = 			\
	asn1_OCSPBasicOCSPResponse.x	\
	asn1_OCSPCertID.x		\
	asn1_OCSPCertStatus.x		\
	asn1_OCSPInnerRequest.x		\
	asn1_OCSPKeyHash.x		\
	asn1_OCSPRequest.x		\
	asn1_OCSPResponderID.x		\
	asn1_OCSPResponse.x		\
	asn1_OCSPResponseBytes.x	\
	asn1_OCSPResponseData.x		\
	asn1_OCSPResponseStatus.x	\
	asn1_OCSPSignature.x		\
	asn1_OCSPSingleResponse.x	\
	asn1_OCSPTBSRequest.x		\
	asn1_OCSPVersion.x		\
	asn1_id_pkix_ocsp.x		\
	asn1_id_pkix_ocsp_basic.x	\
	asn1_id_pkix_ocsp_nonce.x

gen_files_pkcs10 = 			\
	asn1_CertificationRequestInfo.x	\
	asn1_CertificationRequest.x

gen_files_crmf = 			\
	asn1_CRMFRDNSequence.x		\
	asn1_CertReqMessages.x		\
	asn1_CertReqMsg.x		\
	asn1_CertRequest.x		\
	asn1_CertTemplate.x		\
	asn1_Controls.x			\
	asn1_PBMParameter.x		\
	asn1_PKMACValue.x		\
	asn1_POPOPrivKey.x		\
	asn1_POPOSigningKey.x		\
	asn1_POPOSigningKeyInput.x	\
	asn1_ProofOfPossession.x	\
	asn1_SubsequentMessage.x	

AM_YFLAGS = -d

dist_libhx509_la_SOURCES = \
	ca.c \
	cert.c \
	char_map.h \
	cms.c \
	collector.c \
	crypto.c \
	doxygen.c \
	error.c \
	env.c \
	file.c \
	hx509-private.h \
	hx509-protos.h \
	hx509.h \
	hx_locl.h \
	sel.c \
	sel.h \
	sel-gram.y \
	sel-lex.l \
	keyset.c \
	ks_dir.c \
	ks_file.c \
	ks_mem.c \
	ks_null.c \
	ks_p11.c \
	ks_p12.c \
	ks_keychain.c \
	lock.c \
	name.c \
	peer.c \
	print.c \
	softp11.c \
	ref/pkcs11.h \
	req.c \
	revoke.c

sel-lex.c: sel-gram.h

libhx509_la_DEPENDENCIES = version-script.map

libhx509_la_LIBADD = \
	$(LIB_com_err) \
	$(LIB_hcrypto) \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(top_builddir)/lib/wind/libwind.la \
	$(LIBADD_roken) \
	$(LIB_dlopen)

if FRAMEWORK_SECURITY
libhx509_la_LDFLAGS += -framework Security -framework CoreFoundation
endif

if versionscript
libhx509_la_LDFLAGS += $(LDFLAGS_VERSION_SCRIPT)$(srcdir)/version-script.map
endif
$(libhx509_la_OBJECTS): $(srcdir)/version-script.map $(nodist_include_HEADERS) $(priv_headers)

libhx509_la_CPPFLAGS = -I$(srcdir)/ref $(INCLUDE_hcrypto)
nodist_libhx509_la_SOURCES = $(BUILT_SOURCES)

$(gen_files_ocsp) ocsp_asn1.hx ocsp_asn1-priv.hx: ocsp_asn1_files
$(gen_files_pkcs10) pkcs10_asn1.hx pkcs10_asn1-priv.hx: pkcs10_asn1_files
$(gen_files_crmf) crmf_asn1.hx crmf_asn1-priv.hx: crmf_asn1_files

dist_include_HEADERS = hx509.h hx509-protos.h

nodist_include_HEADERS = hx509_err.h
nodist_include_HEADERS += ocsp_asn1.h
nodist_include_HEADERS += pkcs10_asn1.h
nodist_include_HEADERS += crmf_asn1.h

priv_headers  = ocsp_asn1-priv.h
priv_headers += pkcs10_asn1-priv.h
priv_headers += crmf_asn1-priv.h


ocsp_asn1_files: $(ASN1_COMPILE_DEP) $(srcdir)/ocsp.asn1 $(srcdir)/ocsp.opt
	$(ASN1_COMPILE) --option-file=$(srcdir)/ocsp.opt $(srcdir)/ocsp.asn1 ocsp_asn1 || (rm -f ocsp_asn1_files ; exit 1)

pkcs10_asn1_files: $(ASN1_COMPILE_DEP) $(srcdir)/pkcs10.asn1 $(srcdir)/pkcs10.opt
	$(ASN1_COMPILE) --option-file=$(srcdir)/pkcs10.opt $(srcdir)/pkcs10.asn1 pkcs10_asn1 || (rm -f pkcs10_asn1_files ; exit 1)

crmf_asn1_files: $(ASN1_COMPILE_DEP) $(srcdir)/crmf.asn1
	$(ASN1_COMPILE) $(srcdir)/crmf.asn1 crmf_asn1 || (rm -f crmf_asn1_files ; exit 1)

$(libhx509_la_OBJECTS): $(srcdir)/hx509-protos.h $(srcdir)/hx509-private.h $(srcdir)/hx_locl.h
$(libhx509_la_OBJECTS): ocsp_asn1.h pkcs10_asn1.h

$(srcdir)/hx509-protos.h:
	cd $(srcdir) && perl ../../cf/make-proto.pl -R '^(_|^C)' -E HX509_LIB -q -P comment -o hx509-protos.h $(dist_libhx509_la_SOURCES) || rm -f hx509-protos.h

$(srcdir)/hx509-private.h:
	cd $(srcdir) && perl ../../cf/make-proto.pl -q -P comment -p hx509-private.h $(dist_libhx509_la_SOURCES) || rm -f hx509-private.h

bin_PROGRAMS = hxtool

hxtool-commands.c hxtool-commands.h: hxtool-commands.in $(SLC)
	$(SLC) $(srcdir)/hxtool-commands.in

dist_hxtool_SOURCES = hxtool.c
nodist_hxtool_SOURCES = hxtool-commands.c hxtool-commands.h

$(hxtool_OBJECTS): hxtool-commands.h

hxtool_CPPFLAGS = $(INCLUDE_hcrypto)
hxtool_LDADD = \
	libhx509.la \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(LIB_hcrypto) \
	$(LIB_roken) \
	$(top_builddir)/lib/sl/libsl.la

CLEANFILES = $(BUILT_SOURCES) sel-gram.c sel-lex.c \
	$(gen_files_ocsp) ocsp_asn1_files ocsp_asn1{,-priv}.h* \
	ocsp_asn1-template.[ch]* \
	$(gen_files_pkcs10) pkcs10_asn1_files pkcs10_asn1{,-priv}.h* \
	pkcs10_asn1-template.[ch]* \
	$(gen_files_crmf) crmf_asn1_files crmf_asn1{,-priv}.h* \
	crmf_asn1-template.[ch]* \
	$(TESTS) \
	hxtool-commands.c hxtool-commands.h *.tmp \
	request.out \
	out.pem out2.pem \
	sd sd.pem \
	sd.data sd.data.out \
	ev.data ev.data.out \
	cert-null.pem cert-sub-ca2.pem \
	cert-ee.pem cert-ca.pem \
	cert-sub-ee.pem cert-sub-ca.pem \
	cert-proxy.der cert-ca.der cert-ee.der pkcs10-request.der \
	wca.pem wuser.pem wdc.pem wcrl.crl \
	random-data statfile crl.crl \
	test p11dbg.log pkcs11.cfg \
	test-rc-file.rc

clean-local:
	@echo "cleaning PKITS" ; rm -rf PKITS_data

#
# regression tests
#

check_SCRIPTS = $(SCRIPT_TESTS)
check_PROGRAMS = $(PROGRAM_TESTS) test_soft_pkcs11

LDADD = libhx509.la

test_soft_pkcs11_LDADD = libhx509.la
test_soft_pkcs11_CPPFLAGS = -I$(srcdir)/ref

test_name_CPPFLAGS = $(INCLUDE_hcrypto)
test_name_LDADD = libhx509.la $(LIB_roken)

TESTS = $(SCRIPT_TESTS) $(PROGRAM_TESTS)

PROGRAM_TESTS = 		\
	test_name		\
	test_expr

SCRIPT_TESTS = 			\
	test_ca			\
	test_cert		\
	test_chain		\
	test_cms		\
	test_crypto		\
	test_nist		\
	test_nist2		\
	test_pkcs11		\
	test_java_pkcs11	\
	test_nist_cert		\
	test_nist_pkcs12	\
	test_req		\
	test_windows		\
	test_query

do_subst = sed -e 's,[@]srcdir[@],$(srcdir),g' \
	-e 's,[@]objdir[@],$(top_builddir)/lib/hx509,g' \
	-e 's,[@]egrep[@],$(EGREP),g'

test_ca: test_ca.in Makefile
	$(do_subst) < $(srcdir)/test_ca.in > test_ca.tmp
	chmod +x test_ca.tmp
	mv test_ca.tmp test_ca

test_cert: test_cert.in Makefile
	$(do_subst) < $(srcdir)/test_cert.in > test_cert.tmp
	chmod +x test_cert.tmp
	mv test_cert.tmp test_cert

test_chain: test_chain.in Makefile
	$(do_subst) < $(srcdir)/test_chain.in > test_chain.tmp
	chmod +x test_chain.tmp
	mv test_chain.tmp test_chain

test_cms: test_cms.in Makefile
	$(do_subst) < $(srcdir)/test_cms.in > test_cms.tmp
	chmod +x test_cms.tmp
	mv test_cms.tmp test_cms

test_crypto: test_crypto.in Makefile
	$(do_subst) < $(srcdir)/test_crypto.in > test_crypto.tmp
	chmod +x test_crypto.tmp
	mv test_crypto.tmp test_crypto

test_nist: test_nist.in Makefile
	$(do_subst) < $(srcdir)/test_nist.in > test_nist.tmp
	chmod +x test_nist.tmp
	mv test_nist.tmp test_nist

test_nist2: test_nist2.in Makefile
	$(do_subst) < $(srcdir)/test_nist2.in > test_nist2.tmp
	chmod +x test_nist2.tmp
	mv test_nist2.tmp test_nist2

test_pkcs11: test_pkcs11.in Makefile
	$(do_subst) < $(srcdir)/test_pkcs11.in > test_pkcs11.tmp
	chmod +x test_pkcs11.tmp
	mv test_pkcs11.tmp test_pkcs11

test_java_pkcs11: test_java_pkcs11.in Makefile
	$(do_subst) < $(srcdir)/test_java_pkcs11.in > test_java_pkcs11.tmp
	chmod +x test_java_pkcs11.tmp
	mv test_java_pkcs11.tmp test_java_pkcs11

test_nist_cert: test_nist_cert.in Makefile
	$(do_subst) < $(srcdir)/test_nist_cert.in > test_nist_cert.tmp
	chmod +x test_nist_cert.tmp
	mv test_nist_cert.tmp test_nist_cert

test_nist_pkcs12: test_nist_pkcs12.in Makefile
	$(do_subst) < $(srcdir)/test_nist_pkcs12.in > test_nist_pkcs12.tmp
	chmod +x test_nist_pkcs12.tmp
	mv test_nist_pkcs12.tmp test_nist_pkcs12

test_req: test_req.in Makefile
	$(do_subst) < $(srcdir)/test_req.in > test_req.tmp
	chmod +x test_req.tmp
	mv test_req.tmp test_req

test_windows: test_windows.in Makefile
	$(do_subst) < $(srcdir)/test_windows.in > test_windows.tmp
	chmod +x test_windows.tmp
	mv test_windows.tmp test_windows

test_query: test_query.in Makefile
	$(do_subst) < $(srcdir)/test_query.in > test_query.tmp
	chmod +x test_query.tmp
	mv test_query.tmp test_query

EXTRA_DIST = \
	NTMakefile \
	hxtool-version.rc \
	libhx509-exports.def \
	version-script.map \
	crmf.asn1 \
	hx509_err.et \
	hxtool-commands.in \
	quote.py \
	ocsp.asn1 \
	ocsp.opt \
	pkcs10.asn1 \
	pkcs10.opt \
	test_ca.in \
	test_chain.in \
	test_cert.in \
	test_cms.in \
	test_crypto.in \
	test_nist.in \
	test_nist2.in \
	test_nist_cert.in \
	test_nist_pkcs12.in \
	test_pkcs11.in \
	test_java_pkcs11.in \
	test_query.in \
	test_req.in \
	test_windows.in \
	tst-crypto-available1 \
	tst-crypto-available2 \
	tst-crypto-available3 \
	tst-crypto-select \
	tst-crypto-select1 \
	tst-crypto-select2 \
	tst-crypto-select3 \
	tst-crypto-select4 \
	tst-crypto-select5 \
	tst-crypto-select6 \
	tst-crypto-select7 \
	data/n0ll.pem \
	data/secp160r1TestCA.cert.pem \
	data/secp160r1TestCA.key.pem \
	data/secp160r1TestCA.pem \
	data/secp160r2TestClient.cert.pem \
	data/secp160r2TestClient.key.pem \
	data/secp160r2TestClient.pem \
	data/secp160r2TestServer.cert.pem \
	data/secp160r2TestServer.key.pem \
	data/secp160r2TestServer.pem \
	data/bleichenbacher-bad.pem \
	data/bleichenbacher-good.pem \
	data/bleichenbacher-sf-pad-correct.pem \
	data/ca.crt \
	data/ca.key \
	data/crl1.crl \
	data/crl1.der \
	data/gen-req.sh \
	data/j.pem \
	data/kdc.crt \
	data/kdc.key \
	data/key.der \
	data/key2.der \
	data/nist-data \
	data/nist-data2 \
	data/no-proxy-test.crt \
	data/no-proxy-test.key \
	data/ocsp-req1.der \
	data/ocsp-req2.der \
	data/ocsp-resp1-2.der \
	data/ocsp-resp1-3.der \
	data/ocsp-resp1-ca.der \
	data/ocsp-resp1-keyhash.der \
	data/ocsp-resp1-ocsp-no-cert.der \
	data/ocsp-resp1-ocsp.der \
	data/ocsp-resp1.der \
	data/ocsp-resp2.der \
	data/ocsp-responder.crt \
	data/ocsp-responder.key \
	data/openssl.cnf \
	data/pkinit-proxy-chain.crt \
	data/pkinit-proxy.crt \
	data/pkinit-proxy.key \
	data/pkinit-pw.key \
	data/pkinit.crt \
	data/pkinit.key \
	data/pkinit-ec.crt \
	data/pkinit-ec.key \
	data/proxy-level-test.crt \
	data/proxy-level-test.key \
	data/proxy-test.crt \
	data/proxy-test.key \
	data/proxy10-child-test.crt \
	data/proxy10-child-test.key \
	data/proxy10-child-child-test.crt \
	data/proxy10-child-child-test.key \
	data/proxy10-test.crt \
	data/proxy10-test.key \
	data/revoke.crt \
	data/revoke.key \
	data/sf-class2-root.pem \
	data/static-file \
	data/sub-ca.crt \
	data/sub-ca.key \
	data/sub-cert.crt \
	data/sub-cert.key \
	data/sub-cert.p12 \
	data/test-ds-only.crt \
	data/test-ds-only.key \
	data/test-enveloped-aes-128 \
	data/test-enveloped-aes-256 \
	data/test-enveloped-des \
	data/test-enveloped-des-ede3 \
	data/test-enveloped-rc2-128 \
	data/test-enveloped-rc2-40 \
	data/test-enveloped-rc2-64 \
	data/test-ke-only.crt \
	data/test-ke-only.key \
	data/test-nopw.p12 \
	data/test-pw.key \
	data/test-signed-data \
	data/test-signed-data-noattr \
	data/test-signed-data-noattr-nocerts \
	data/test-signed-sha-1 \
	data/test-signed-sha-256 \
	data/test-signed-sha-512 \
	data/test.combined.crt \
	data/test.crt \
	data/test.key \
	data/test.p12 \
	data/win-u16-in-printablestring.der \
	data/yutaka-pad-broken-ca.pem \
	data/yutaka-pad-broken-cert.pem \
	data/yutaka-pad-ok-ca.pem \
	data/yutaka-pad-ok-cert.pem \
	data/yutaka-pad.key
