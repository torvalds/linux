# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += -I../asn1 -I$(srcdir)/../asn1 $(INCLUDE_hcrypto)
AM_CPPFLAGS += $(INCLUDE_openldap) -DHDB_DB_DIR=\"$(DIR_hdbdir)\"
AM_CPPFLAGS += -I$(srcdir)/../krb5
AM_CPPFLAGS += $(INCLUDE_sqlite3)
AM_CPPFLAGS += $(INCLUDE_libintl)
if HAVE_DBHEADER
AM_CPPFLAGS += -I$(DBHEADER)
endif

BUILT_SOURCES = \
	$(gen_files_hdb:.x=.c)	\
	hdb_err.c \
	hdb_err.h

gen_files_hdb = \
	asn1_Salt.x \
	asn1_Key.x \
	asn1_Event.x \
	asn1_HDBFlags.x \
	asn1_GENERATION.x \
	asn1_HDB_Ext_PKINIT_acl.x \
	asn1_HDB_Ext_PKINIT_cert.x \
	asn1_HDB_Ext_PKINIT_hash.x \
	asn1_HDB_Ext_Constrained_delegation_acl.x \
	asn1_HDB_Ext_Lan_Manager_OWF.x \
	asn1_HDB_Ext_Password.x \
	asn1_HDB_Ext_Aliases.x \
	asn1_HDB_extension.x \
	asn1_HDB_extensions.x \
	asn1_hdb_entry.x \
	asn1_hdb_entry_alias.x \
	asn1_hdb_keyset.x

CLEANFILES = $(BUILT_SOURCES) $(gen_files_hdb) \
	hdb_asn1{,-priv}.h* hdb_asn1_files hdb_asn1-template.c*

LDADD = libhdb.la \
	$(LIB_openldap) \
	$(LIB_libintl) \
	../krb5/libkrb5.la \
	../asn1/libasn1.la \
	$(LIB_hcrypto) \
	$(LIB_roken) \
	$(LIB_ldopen)


if OPENLDAP_MODULE

ldap_so = hdb_ldap.la
hdb_ldap_la_SOURCES = hdb-ldap.c
hdb_ldap_la_LDFLAGS = -module -avoid-version
hdb_ldap_la_LIBADD = $(LIB_openldap) libhdb.la

else

ldap = hdb-ldap.c
ldap_lib = $(LIB_openldap)

endif


lib_LTLIBRARIES = libhdb.la $(ldap_so)
libhdb_la_LDFLAGS = -version-info 11:0:2

if versionscript
libhdb_la_LDFLAGS += $(LDFLAGS_VERSION_SCRIPT)$(srcdir)/version-script.map
endif

noinst_PROGRAMS = test_dbinfo test_hdbkeys test_mkey

dist_libhdb_la_SOURCES =			\
	common.c				\
	db.c					\
	db3.c					\
	ext.c					\
	$(ldap)					\
	hdb.c					\
	hdb-sqlite.c				\
	hdb-keytab.c				\
	hdb-mitdb.c				\
	hdb_locl.h				\
	hdb-private.h				\
	keys.c					\
	keytab.c				\
	dbinfo.c				\
	mkey.c					\
	ndbm.c					\
	print.c

nodist_libhdb_la_SOURCES = $(BUILT_SOURCES)

libhdb_la_DEPENDENCIES = version-script.map

include_HEADERS = hdb.h hdb-protos.h
nodist_include_HEADERS =  hdb_err.h hdb_asn1.h

libhdb_la_LIBADD = \
	$(LIB_com_err) \
	../krb5/libkrb5.la \
	../asn1/libasn1.la \
	$(LIB_sqlite3) \
	$(LIBADD_roken) \
	$(ldap_lib) \
	$(LIB_dlopen) \
	$(DBLIB) \
	$(LIB_NDBM)

$(libhdb_la_OBJECTS): $(srcdir)/hdb-protos.h $(srcdir)/hdb-private.h
$(libhdb_la_OBJECTS): hdb_asn1.h hdb_asn1-priv.h hdb_err.h

$(srcdir)/hdb-protos.h:
	cd $(srcdir); perl ../../cf/make-proto.pl -q -P comment -o hdb-protos.h $(dist_libhdb_la_SOURCES) || rm -f hdb-protos.h

$(srcdir)/hdb-private.h:
	cd $(srcdir); perl ../../cf/make-proto.pl -q -P comment -p hdb-private.h $(dist_libhdb_la_SOURCES) || rm -f hdb-private.h

$(gen_files_hdb) hdb_asn1.hx hdb_asn1-priv.hx: hdb_asn1_files

hdb_asn1_files: $(ASN1_COMPILE_DEP) $(srcdir)/hdb.asn1
	$(ASN1_COMPILE) $(srcdir)/hdb.asn1 hdb_asn1

test_dbinfo_LIBS = libhdb.la

test_hdbkeys_LIBS = ../krb5/libkrb5.la libhdb.la
test_mkey_LIBS = $(test_hdbkeys_LIBS)

# to help stupid solaris make

hdb_err.h: hdb_err.et

EXTRA_DIST = \
	NTMakefile \
	libhdb-version.rc \
	libhdb-exports.def \
	hdb.asn1 \
	hdb_err.et \
	hdb.schema \
	version-script.map \
	data-mkey.mit.des3.le \
	data-mkey.mit.des3.be
