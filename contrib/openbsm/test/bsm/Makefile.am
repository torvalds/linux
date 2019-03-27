if USE_NATIVE_INCLUDES
INCLUDES = -I$(top_builddir) -I$(top_srcdir)
else
INCLUDES = -I$(top_builddir) -I$(top_srcdir) -I$(top_srcdir)/sys
endif

bin_PROGRAMS = generate
generate_SOURCES = generate.c
generate_LDADD = $(top_builddir)/libbsm/libbsm.la
