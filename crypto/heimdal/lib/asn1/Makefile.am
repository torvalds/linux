# $Id$

include $(top_srcdir)/Makefile.am.common

YFLAGS = -d -t

lib_LTLIBRARIES = libasn1.la
libasn1_la_LDFLAGS = -version-info 8:0:0

noinst_LTLIBRARIES = libasn1base.la

if versionscript
libasn1_la_LDFLAGS += $(LDFLAGS_VERSION_SCRIPT)$(srcdir)/version-script.map
endif


libasn1_la_LIBADD = \
	libasn1base.la \
	@LIB_com_err@ \
	$(LIBADD_roken)

BUILT_SOURCES =				\
	$(gen_files_rfc2459:.x=.c)	\
	$(gen_files_cms:.x=.c)		\
	$(gen_files_krb5:.x=.c)		\
	$(gen_files_pkinit:.x=.c)	\
	$(gen_files_pkcs8:.x=.c)	\
	$(gen_files_pkcs9:.x=.c)	\
	$(gen_files_pkcs12:.x=.c)	\
	$(gen_files_digest:.x=.c)	\
	$(gen_files_kx509:.x=.c)

gen_files_krb5 = asn1_krb5_asn1.x
gen_files_cms = asn1_cms_asn1.x
gen_files_rfc2459 = asn1_rfc2459_asn1.x
gen_files_pkinit = asn1_pkinit_asn1.x
gen_files_pkcs12 = asn1_pkcs12_asn1.x
gen_files_pkcs8 = asn1_pkcs8_asn1.x
gen_files_pkcs9 = asn1_pkcs9_asn1.x
gen_files_test = asn1_test_asn1.x
gen_files_digest = asn1_digest_asn1.x
gen_files_kx509 = asn1_kx509_asn1.x

noinst_PROGRAMS = asn1_gen

libexec_heimdal_PROGRAMS = asn1_compile asn1_print

TESTS = check-der check-gen check-timegm check-ber check-template
check_PROGRAMS = $(TESTS)

asn1_gen_SOURCES = asn1_gen.c
asn1_print_SOURCES = asn1_print.c
check_der_SOURCES = check-der.c check-common.c check-common.h

check_template_SOURCES = check-template.c check-common.c check-common.h
nodist_check_template_SOURCES = $(gen_files_test:.x=.c)

dist_check_gen_SOURCES = check-gen.c check-common.c check-common.h
nodist_check_gen_SOURCES = $(gen_files_test:.x=.c)

build_HEADERZ = asn1-template.h

asn1_compile_SOURCES = 				\
	asn1_queue.h				\
	asn1parse.y				\
	der.h					\
	gen.c					\
	gen_copy.c				\
	gen_decode.c				\
	gen_encode.c				\
	gen_free.c				\
	gen_glue.c				\
	gen_length.c				\
	gen_locl.h				\
	gen_seq.c				\
	gen_template.c				\
	hash.c					\
	hash.h					\
	lex.l					\
	lex.h					\
	main.c					\
	asn1-template.h				\
	symbol.c				\
	symbol.h

dist_libasn1base_la_SOURCES =			\
	der_locl.h 				\
	der.c					\
	der.h					\
	der_get.c				\
	der_put.c				\
	der_free.c				\
	der_length.c				\
	der_copy.c				\
	der_cmp.c				\
	der_format.c				\
	heim_asn1.h				\
	extra.c					\
	template.c				\
	timegm.c

nodist_libasn1base_la_SOURCES = 	\
	asn1_err.h			\
	asn1_err.c

nodist_libasn1_la_SOURCES = $(BUILT_SOURCES)

asn1_compile_LDADD = \
	$(LIB_roken) $(LEXLIB)

check_der_LDADD = \
	libasn1base.la \
	$(LIB_roken)

check_template_LDADD = $(check_der_LDADD)
asn1_print_LDADD = $(check_der_LDADD) $(LIB_com_err)
asn1_gen_LDADD = $(check_der_LDADD)
check_timegm_LDADD = $(check_der_LDADD)

check_gen_LDADD = \
	libasn1.la \
	$(LIB_roken)

check_ber_LDADD = $(check_gen_LDADD)

CLEANFILES = \
	$(BUILT_SOURCES) \
	$(gen_files_rfc2459) \
	$(gen_files_cms) \
	$(gen_files_krb5) \
	$(gen_files_pkinit) \
	$(gen_files_pkcs8) \
	$(gen_files_pkcs9) \
	$(gen_files_pkcs12) \
	$(gen_files_digest) \
	$(gen_files_kx509) \
	$(gen_files_test) $(nodist_check_gen_SOURCES) \
	asn1_err.c asn1_err.h \
	rfc2459_asn1_files rfc2459_asn1*.h* \
	cms_asn1_files cms_asn1*.h* \
	krb5_asn1_files krb5_asn1*.h* \
	pkinit_asn1_files pkinit_asn1*.h* \
	pkcs8_asn1_files pkcs8_asn1*.h* \
	pkcs9_asn1_files pkcs9_asn1*.h* \
	pkcs12_asn1_files pkcs12_asn1*.h* \
	digest_asn1_files digest_asn1*.h* \
	kx509_asn1_files kx509_asn1*.h* \
	test_asn1_files test_asn1*.h*

dist_include_HEADERS = der.h heim_asn1.h der-protos.h der-private.h
dist_include_HEADERS += asn1-common.h

nodist_include_HEADERS = asn1_err.h
nodist_include_HEADERS += krb5_asn1.h
nodist_include_HEADERS += pkinit_asn1.h
nodist_include_HEADERS += cms_asn1.h
nodist_include_HEADERS += rfc2459_asn1.h
nodist_include_HEADERS += pkcs8_asn1.h
nodist_include_HEADERS += pkcs9_asn1.h
nodist_include_HEADERS += pkcs12_asn1.h
nodist_include_HEADERS += digest_asn1.h
nodist_include_HEADERS += kx509_asn1.h

priv_headers  = krb5_asn1-priv.h
priv_headers += pkinit_asn1-priv.h
priv_headers += cms_asn1-priv.h
priv_headers += rfc2459_asn1-priv.h
priv_headers += pkcs8_asn1-priv.h
priv_headers += pkcs9_asn1-priv.h
priv_headers += pkcs12_asn1-priv.h
priv_headers += digest_asn1-priv.h
priv_headers += kx509_asn1-priv.h
priv_headers += test_asn1.h test_asn1-priv.h



$(asn1_compile_OBJECTS): asn1parse.h asn1parse.c $(srcdir)/der-protos.h $(srcdir)/der-private.h
$(libasn1_la_OBJECTS): $(nodist_include_HEADERS) $(priv_headers) asn1_err.h $(srcdir)/der-protos.h $(srcdir)/der-private.h
$(libasn1base_la_OBJECTS): asn1_err.h $(srcdir)/der-protos.h $(srcdir)/der-private.h
$(check_gen_OBJECTS): test_asn1.h
$(check_template_OBJECTS): test_asn1_files
$(asn1_print_OBJECTS): krb5_asn1.h

asn1parse.h: asn1parse.c

$(gen_files_krb5) krb5_asn1.hx krb5_asn1-priv.hx: krb5_asn1_files
$(gen_files_pkinit) pkinit_asn1.hx pkinit_asn1-priv.hx: pkinit_asn1_files
$(gen_files_pkcs8) pkcs8_asn1.hx pkcs8_asn1-priv.hx: pkcs8_asn1_files
$(gen_files_pkcs9) pkcs9_asn1.hx pkcs9_asn1-priv.hx: pkcs9_asn1_files
$(gen_files_pkcs12) pkcs12_asn1.hx pkcs12_asn1-priv.hx: pkcs12_asn1_files
$(gen_files_digest) digest_asn1.hx digest_asn1-priv.hx: digest_asn1_files
$(gen_files_kx509) kx509_asn1.hx kx509_asn1-priv.hx: kx509_asn1_files
$(gen_files_rfc2459) rfc2459_asn1.hx rfc2459_asn1-priv.hx: rfc2459_asn1_files
$(gen_files_cms) cms_asn1.hx cms_asn1-priv.hx: cms_asn1_files
$(gen_files_test) test_asn1.hx test_asn1-priv.hx: test_asn1_files

rfc2459_asn1_files: asn1_compile$(EXEEXT) $(srcdir)/rfc2459.asn1
	$(ASN1_COMPILE) --one-code-file --preserve-binary=TBSCertificate --preserve-binary=TBSCRLCertList --preserve-binary=Name --sequence=GeneralNames --sequence=Extensions --sequence=CRLDistributionPoints $(srcdir)/rfc2459.asn1 rfc2459_asn1 || (rm -f rfc2459_asn1_files ; exit 1)

cms_asn1_files: asn1_compile$(EXEEXT) $(srcdir)/cms.asn1 $(srcdir)/cms.opt
	$(ASN1_COMPILE) --one-code-file --option-file=$(srcdir)/cms.opt $(srcdir)/cms.asn1 cms_asn1 || (rm -f cms_asn1_files ; exit 1)

krb5_asn1_files: asn1_compile$(EXEEXT) $(srcdir)/krb5.asn1 $(srcdir)/krb5.opt
	$(ASN1_COMPILE) --one-code-file --option-file=$(srcdir)/krb5.opt $(srcdir)/krb5.asn1 krb5_asn1 || (rm -f krb5_asn1_files ; exit 1)

pkinit_asn1_files: asn1_compile$(EXEEXT) $(srcdir)/pkinit.asn1
	$(ASN1_COMPILE) --one-code-file $(srcdir)/pkinit.asn1 pkinit_asn1 || (rm -f pkinit_asn1_files ; exit 1)

pkcs8_asn1_files: asn1_compile$(EXEEXT) $(srcdir)/pkcs8.asn1
	$(ASN1_COMPILE) --one-code-file $(srcdir)/pkcs8.asn1 pkcs8_asn1 || (rm -f pkcs8_asn1_files ; exit 1)

pkcs9_asn1_files: asn1_compile$(EXEEXT) $(srcdir)/pkcs9.asn1
	$(ASN1_COMPILE) --one-code-file $(srcdir)/pkcs9.asn1 pkcs9_asn1 || (rm -f pkcs9_asn1_files ; exit 1)

pkcs12_asn1_files: asn1_compile$(EXEEXT) $(srcdir)/pkcs12.asn1
	$(ASN1_COMPILE) --one-code-file $(srcdir)/pkcs12.asn1 pkcs12_asn1 || (rm -f pkcs12_asn1_files ; exit 1)

digest_asn1_files: asn1_compile$(EXEEXT) $(srcdir)/digest.asn1
	$(ASN1_COMPILE) --one-code-file $(srcdir)/digest.asn1 digest_asn1 || (rm -f digest_asn1_files ; exit 1)

kx509_asn1_files: asn1_compile$(EXEEXT) $(srcdir)/kx509.asn1
	$(ASN1_COMPILE) --one-code-file $(srcdir)/kx509.asn1 kx509_asn1 || (rm -f kx509_asn1_files ; exit 1)

test_asn1_files: asn1_compile$(EXEEXT) $(srcdir)/test.asn1
	$(ASN1_COMPILE) --one-code-file --sequence=TESTSeqOf $(srcdir)/test.asn1 test_asn1 || (rm -f test_asn1_files ; exit 1)


EXTRA_DIST =		\
	NTMakefile	\
	asn1_compile-version.rc \
	libasn1-exports.def \
	cms.asn1	\
	cms.opt		\
	asn1_err.et	\
	canthandle.asn1	\
	digest.asn1	\
	krb5.asn1	\
	krb5.opt	\
	kx509.asn1	\
	pkcs12.asn1	\
	pkcs8.asn1	\
	pkcs9.asn1	\
	pkinit.asn1	\
	rfc2459.asn1	\
	setchgpw2.asn1	\
	test.asn1	\
	test.gen	\
	version-script.map

$(srcdir)/der-protos.h:
	cd $(srcdir) && perl ../../cf/make-proto.pl -q -P comment -o der-protos.h $(dist_libasn1base_la_SOURCES) || rm -f der-protos.h

$(srcdir)/der-private.h:
	cd $(srcdir) && perl ../../cf/make-proto.pl -q -P comment -p der-private.h $(dist_libasn1base_la_SOURCES) || rm -f der-private.h
