if USE_NATIVE_INCLUDES
INCLUDES = -I$(top_builddir) -I$(top_srcdir)
else
INCLUDES = -I$(top_builddir) -I$(top_srcdir) -I$(top_srcdir)/sys
endif

bin_PROGRAMS = audump
audump_SOURCES = audump.c
audump_LDADD = $(top_builddir)/libbsm/libbsm.la
