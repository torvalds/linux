libucl_common_cflags=	-I$(top_srcdir)/src \
			-I$(top_srcdir)/include \
			-I$(top_srcdir)/uthash \
			-I$(top_srcdir)/klib \
			-Wall -W -Wno-unused-parameter -Wno-pointer-sign
lib_LTLIBRARIES=	libucl.la
libucl_la_SOURCES=	ucl_emitter.c \
					ucl_emitter_streamline.c \
					ucl_emitter_utils.c \
					ucl_hash.c \
					ucl_parser.c \
					ucl_schema.c \
					ucl_util.c \
					ucl_msgpack.c \
					ucl_sexp.c
libucl_la_CFLAGS=	$(libucl_common_cflags) \
					@CURL_CFLAGS@
libucl_la_LDFLAGS = -version-info @SO_VERSION@
libucl_la_LIBADD=	@LIBFETCH_LIBS@ \
					@LIBCRYPTO_LIB@ \
					@LIBREGEX_LIB@ \
					@CURL_LIBS@

include_HEADERS=	$(top_srcdir)/include/ucl.h \
					$(top_srcdir)/include/ucl++.h
noinst_HEADERS=	ucl_internal.h \
				mum.h \
				ucl_hash.h \
				ucl_chartable.h \
				tree.h
