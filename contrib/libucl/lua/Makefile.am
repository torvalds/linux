ucl_common_cflags=	-I$(top_srcdir)/src \
			-I$(top_srcdir)/include \
			-I$(top_srcdir)/uthash \
			-Wall -W -Wno-unused-parameter -Wno-pointer-sign
luaexec_LTLIBRARIES=	ucl.la
ucl_la_SOURCES=	lua_ucl.c
ucl_la_CFLAGS=	$(ucl_common_cflags) \
					@LUA_INCLUDE@
ucl_la_LDFLAGS = -module -export-dynamic -avoid-version
ucl_la_LIBADD=	$(top_srcdir)/src/libucl.la \
					@LIBFETCH_LIBS@ \
					@LIBCRYPTO_LIB@ \
					@LIBREGEX_LIB@ \
					@CURL_LIBS@ \
					@LUA_LIB@

include_HEADERS=	$(top_srcdir)/include/lua_ucl.h

ROCKSPEC = $(PACKAGE)-$(VERSION)-1.rockspec
EXTRA_DIST = $(PACKAGE).rockspec.in \
			test.lua
DISTCLEANFILES = $(PACKAGE).rockspec

$(ROCKSPEC): $(PACKAGE).rockspec dist
	sed -e 's/@MD5@/'`$(MD5SUM) $(distdir).tar.gz | \
	cut -d " " -f 1`'/g' < $(PACKAGE).rockspec > $@