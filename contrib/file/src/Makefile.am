MAGIC = $(pkgdatadir)/magic
lib_LTLIBRARIES = libmagic.la
nodist_include_HEADERS = magic.h

bin_PROGRAMS = file

AM_CPPFLAGS = -DMAGIC='"$(MAGIC)"'
AM_CFLAGS = $(CFLAG_VISIBILITY) @WARNINGS@

libmagic_la_SOURCES = buffer.c magic.c apprentice.c softmagic.c ascmagic.c \
	encoding.c compress.c is_tar.c readelf.c print.c fsmagic.c \
	funcs.c file.h readelf.h tar.h apptype.c der.c der.h \
	file_opts.h elfclass.h mygetopt.h cdf.c cdf_time.c readcdf.c cdf.h
libmagic_la_LDFLAGS = -no-undefined -version-info 1:0:0
if MINGW
MINGWLIBS = -lgnurx -lshlwapi
else
MINGWLIBS = 
endif
libmagic_la_LIBADD = $(LTLIBOBJS) $(MINGWLIBS)

file_SOURCES = file.c seccomp.c
file_LDADD = libmagic.la
CLEANFILES = magic.h
EXTRA_DIST = magic.h.in
HDR= $(top_srcdir)/src/magic.h.in
BUILT_SOURCES = magic.h

magic.h:	${HDR}
	sed -e "s/X.YY/$$(echo @VERSION@ | tr -d .)/" < ${HDR} > $@
