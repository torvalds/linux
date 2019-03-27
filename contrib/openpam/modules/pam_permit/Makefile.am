# $OpenPAM: Makefile.am 938 2017-04-30 21:34:42Z des $

if CUSTOM_MODULES_DIR
moduledir = $(OPENPAM_MODULES_DIR)
else
moduledir = $(libdir)
endif
AM_CPPFLAGS = -I$(top_srcdir)/include

module_LTLIBRARIES = pam_permit.la

pam_permit_la_SOURCES = pam_permit.c
pam_permit_la_LDFLAGS = -no-undefined -module -version-info $(LIB_MAJ) \
	-export-symbols-regex '^pam_sm_'
if WITH_SYSTEM_LIBPAM
pam_permit_la_LIBADD = $(SYSTEM_LIBPAM)
else
pam_permit_la_LIBADD = $(top_builddir)/lib/libpam/libpam.la
endif
