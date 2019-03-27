# $Id$

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += $(INCLUDE_libintl) $(INCLUDE_krb4) $(INCLUDE_hcrypto) -I$(srcdir)/../lib/krb5

lib_LTLIBRARIES = libkdc.la

bin_PROGRAMS = string2key

sbin_PROGRAMS = kstash

libexec_PROGRAMS = hprop hpropd kdc digest-service

noinst_PROGRAMS = kdc-replay

man_MANS = kdc.8 kstash.8 hprop.8 hpropd.8 string2key.8

hprop_SOURCES = hprop.c mit_dump.c hprop.h
hpropd_SOURCES = hpropd.c hprop.h

kstash_SOURCES = kstash.c headers.h

string2key_SOURCES = string2key.c headers.h

digest_service_SOURCES = \
	digest-service.c

kdc_SOURCES = connect.c	\
	config.c	\
	announce.c	\
	main.c

libkdc_la_SOURCES = 		\
	kdc-private.h	 	\
	kdc-protos.h	 	\
	default_config.c 	\
	set_dbinfo.c	 	\
	digest.c		\
	kdc_locl.h		\
	kerberos5.c		\
	krb5tgs.c		\
	pkinit.c		\
	log.c			\
	misc.c			\
	kx509.c			\
	process.c		\
	windc.c			\
	rx.h


$(libkdc_la_OBJECTS): $(srcdir)/kdc-protos.h $(srcdir)/kdc-private.h

libkdc_la_LDFLAGS = -version-info 2:0:0

if versionscript
libkdc_la_LDFLAGS += $(LDFLAGS_VERSION_SCRIPT)$(srcdir)/version-script.map
endif
$(libkdc_la_OBJECTS): $(srcdir)/version-script.map

$(srcdir)/kdc-protos.h:
	cd $(srcdir) && perl ../cf/make-proto.pl -q -P comment -o kdc-protos.h $(libkdc_la_SOURCES) || rm -f kdc-protos.h

$(srcdir)/kdc-private.h:
	cd $(srcdir) && perl ../cf/make-proto.pl -q -P comment -p kdc-private.h $(libkdc_la_SOURCES) || rm -f kdc-private.h


hprop_LDADD = \
	$(top_builddir)/lib/hdb/libhdb.la \
	$(top_builddir)/lib/krb5/libkrb5.la \
	$(LIB_kdb) $(LIB_krb4) \
	$(LIB_hcrypto) \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(LIB_roken) \
	$(DBLIB) 

hpropd_LDADD = \
	$(top_builddir)/lib/hdb/libhdb.la \
	$(top_builddir)/lib/krb5/libkrb5.la \
	$(LIB_kdb) $(LIB_krb4) \
	$(LIB_hcrypto) \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(LIB_roken) \
	$(DBLIB) 

if PKINIT
LIB_pkinit = $(top_builddir)/lib/hx509/libhx509.la
endif

libkdc_la_LIBADD = \
	$(LIB_pkinit) \
	$(top_builddir)/lib/hdb/libhdb.la \
	$(top_builddir)/lib/krb5/libkrb5.la \
	$(LIB_kdb) $(LIB_krb4) \
	$(top_builddir)/lib/ntlm/libheimntlm.la \
	$(LIB_hcrypto) \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(LIB_roken) \
	$(DBLIB) 

LDADD = $(top_builddir)/lib/hdb/libhdb.la \
	$(top_builddir)/lib/krb5/libkrb5.la \
	$(LIB_krb4) \
	$(LIB_hcrypto) \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(LIB_roken) \
	$(DBLIB)

kdc_LDADD = libkdc.la $(LDADD) $(LIB_pidfile) $(CAPNG_LIBS)

if FRAMEWORK_SECURITY
kdc_LDFLAGS = -framework SystemConfiguration -framework CoreFoundation
endif
kdc_CFLAGS = $(CAPNG_CFLAGS)

digest_service_LDADD = \
	libkdc.la \
	$(top_builddir)/lib/ntlm/libheimntlm.la \
	$(top_builddir)/lib/ipc/libheim-ipcs.la \
	$(LDADD) $(LIB_pidfile)
kdc_replay_LDADD = libkdc.la $(LDADD) $(LIB_pidfile)

include_HEADERS = kdc.h kdc-protos.h


krb5dir = $(includedir)/krb5
krb5_HEADERS = windc_plugin.h

build_HEADERZ = $(krb5_HEADERS) # XXX

EXTRA_DIST = \
	hprop-version.rc \
	hpropd-version.rc \
	kdc-version.rc \
	kstash-version.rc \
	libkdc-version.rc \
	string2key-version.rc \
	libkdc-exports.def \
	NTMakefile $(man_MANS) version-script.map
