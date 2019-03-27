if USE_NATIVE_INCLUDES
INCLUDES = -I$(top_builddir) -I$(top_srcdir)
else
INCLUDES = -I$(top_builddir) -I$(top_srcdir) -I$(top_srcdir)/sys
endif

lib_LTLIBRARIES = libauditd.la

libauditd_la_SOURCES =	\
	auditd_lib.c

man3_MANS =		\
	libauditd.3
