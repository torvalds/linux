if USE_NATIVE_INCLUDES
INCLUDES = -I$(top_builddir) -I$(top_srcdir)
else
INCLUDES = -I$(top_builddir) -I$(top_srcdir) -I$(top_srcdir)/sys
endif

sbin_PROGRAMS = auditfilterd
auditfilterd_SOURCES = auditfilterd_conf.c auditfilterd.c
auditfilterd_LDADD = $(top_builddir)/libbsm/libbsm.la
man8_MANS = auditfilterd.8
