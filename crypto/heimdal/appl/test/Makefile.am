# $Id$

include $(top_srcdir)/Makefile.am.common

noinst_PROGRAMS = tcp_client tcp_server gssapi_server gssapi_client \
	uu_server uu_client nt_gss_server nt_gss_client http_client

tcp_client_SOURCES = tcp_client.c common.c test_locl.h

tcp_server_SOURCES = tcp_server.c common.c test_locl.h

gssapi_server_SOURCES = gssapi_server.c gss_common.c common.c \
	gss_common.h test_locl.h

gssapi_client_SOURCES = gssapi_client.c gss_common.c common.c \
	gss_common.h test_locl.h

http_client_SOURCES = http_client.c gss_common.c common.c \
	gss_common.h test_locl.h

uu_server_SOURCES = uu_server.c common.c test_locl.h

uu_client_SOURCES = uu_client.c common.c test_locl.h

gssapi_server_LDADD = $(top_builddir)/lib/gssapi/libgssapi.la $(LDADD)

gssapi_client_LDADD = $(gssapi_server_LDADD)

http_client_LDADD = $(top_builddir)/lib/gssapi/libgssapi.la $(LDADD)

nt_gss_client_SOURCES = nt_gss_client.c nt_gss_common.c nt_gss_common.h common.c

nt_gss_server_SOURCES = nt_gss_server.c nt_gss_common.c nt_gss_common.h

nt_gss_client_LDADD = $(gssapi_server_LDADD)

nt_gss_server_LDADD = $(nt_gss_client_LDADD)

LDADD = $(top_builddir)/lib/krb5/libkrb5.la \
	$(LIB_hcrypto) \
	$(top_builddir)/lib/asn1/libasn1.la \
	$(LIB_roken)

EXTRA_DIST = NTMakefile
