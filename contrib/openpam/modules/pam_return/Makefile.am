# $OpenPAM: Makefile.am 938 2017-04-30 21:34:42Z des $

if CUSTOM_MODULES_DIR
moduledir = $(OPENPAM_MODULES_DIR)
else
moduledir = $(libdir)
endif
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/lib/libpam

module_LTLIBRARIES = pam_return.la

pam_return_la_SOURCES = pam_return.c
pam_return_la_LDFLAGS = -no-undefined -module -version-info $(LIB_MAJ) \
	-export-symbols-regex '^pam_sm_'
if WITH_SYSTEM_LIBPAM
pam_return_la_LIBADD = $(SYSTEM_LIBPAM)
else
pam_return_la_LIBADD = $(top_builddir)/lib/libpam/libpam.la
endif
