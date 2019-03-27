
AM_CFLAGS = @BUILD_CFLAGS@ 
lib_LTLIBRARIES = libsqlite3.la
libsqlite3_la_SOURCES = sqlite3.c
libsqlite3_la_LDFLAGS = -no-undefined -version-info 8:6:8

bin_PROGRAMS = sqlite3
sqlite3_SOURCES = shell.c sqlite3.h
EXTRA_sqlite3_SOURCES = sqlite3.c
sqlite3_LDADD = @EXTRA_SHELL_OBJ@ @READLINE_LIBS@
sqlite3_DEPENDENCIES = @EXTRA_SHELL_OBJ@
sqlite3_CFLAGS = $(AM_CFLAGS) -DSQLITE_ENABLE_EXPLAIN_COMMENTS -DSQLITE_ENABLE_DBPAGE_VTAB -DSQLITE_ENABLE_STMTVTAB -DSQLITE_ENABLE_DBSTAT_VTAB $(SHELL_CFLAGS)

include_HEADERS = sqlite3.h sqlite3ext.h

EXTRA_DIST = sqlite3.1 tea Makefile.msc sqlite3.rc README.txt Replace.cs Makefile.fallback
pkgconfigdir = ${libdir}/pkgconfig
pkgconfig_DATA = sqlite3.pc

man_MANS = sqlite3.1
