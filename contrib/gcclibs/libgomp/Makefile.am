## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I .. -I ../config
SUBDIRS = testsuite

## May be used by toolexeclibdir.
gcc_version := $(shell cat $(top_srcdir)/../gcc/BASE-VER)

config_path = @config_path@
search_path = $(addprefix $(top_srcdir)/config/, $(config_path)) $(top_srcdir)

fincludedir = $(libdir)/gcc/$(target_alias)/$(gcc_version)/finclude
libsubincludedir = $(libdir)/gcc/$(target_alias)/$(gcc_version)/include

vpath % $(strip $(search_path))

AM_CPPFLAGS = $(addprefix -I, $(search_path))
AM_CFLAGS = $(XCFLAGS)
AM_LDFLAGS = $(XLDFLAGS) $(SECTION_LDFLAGS) $(OPT_LDFLAGS)

toolexeclib_LTLIBRARIES = libgomp.la
nodist_toolexeclib_HEADERS = libgomp.spec

if LIBGOMP_BUILD_VERSIONED_SHLIB
libgomp_version_script = -Wl,--version-script,$(top_srcdir)/libgomp.map
else
libgomp_version_script =
endif
libgomp_version_info = -version-info $(libtool_VERSION)
libgomp_la_LDFLAGS = $(libgomp_version_info) $(libgomp_version_script)

libgomp_la_SOURCES = alloc.c barrier.c critical.c env.c error.c iter.c \
	loop.c ordered.c parallel.c sections.c single.c team.c work.c \
	lock.c mutex.c proc.c sem.c bar.c time.c fortran.c affinity.c

nodist_noinst_HEADERS = libgomp_f.h
nodist_libsubinclude_HEADERS = omp.h
if USE_FORTRAN
nodist_finclude_HEADERS = omp_lib.h omp_lib.f90 omp_lib.mod omp_lib_kinds.mod
endif

omp_lib_kinds.mod: omp_lib.mod
	:
omp_lib.mod: omp_lib.f90
	$(FC) $(FCFLAGS) -fsyntax-only omp_lib.f90
fortran.lo: libgomp_f.h
fortran.o: libgomp_f.h
env.lo: libgomp_f.h
env.o: libgomp_f.h


# No install-html or install-pdf support in automake yet
.PHONY: install-html install-pdf
install-html:

install-pdf: $(PDFS)
	@$(NORMAL_INSTALL)
	test -z "$(pdfdir)" || $(mkinstalldirs) "$(DESTDIR)$(pdfdir)"
	@list='$(PDFS)'; for p in $$list; do \
	  if test -f "$$p"; then d=; else d="$(srcdir)/"; fi; \
	  f=$(am__strip_dir) \
	  echo " $(INSTALL_DATA) '$$d$$p' '$(DESTDIR)$(pdfdir)/$$f'"; \
	  $(INSTALL_DATA) "$$d$$p" "$(DESTDIR)$(pdfdir)/$$f"; \
	done

# Automake Documentation:
# If your package has Texinfo files in many directories, you can use the
# variable TEXINFO_TEX to tell Automake where to find the canonical
# `texinfo.tex' for your package. The value of this variable should be
# the relative path from the current `Makefile.am' to `texinfo.tex'.
TEXINFO_TEX   = ../gcc/doc/include/texinfo.tex

# Defines info, dvi, pdf and html targets
MAKEINFOFLAGS = -I $(srcdir)/../gcc/doc/include
info_TEXINFOS = libgomp.texi

# AM_CONDITIONAL on configure option --generated-files-in-srcdir
if GENINSRC
STAMP_GENINSRC = stamp-geninsrc
else
STAMP_GENINSRC =
endif

# AM_CONDITIONAL on configure check ACX_CHECK_PROG_VER([MAKEINFO])
if BUILD_INFO
STAMP_BUILD_INFO = stamp-build-info
else
STAMP_BUILD_INFO =
endif


all-local: $(STAMP_GENINSRC)

stamp-geninsrc: libgomp.info
	cp -p $(top_builddir)/libgomp.info $(srcdir)/libgomp.info
	@touch $@

libgomp.info: $(STAMP_BUILD_INFO)

stamp-build-info: libgomp.texi
	$(MAKEINFO) $(AM_MAKEINFOFLAGS) $(MAKEINFOFLAGS) -I $(srcdir) -o libgomp.info $(srcdir)/libgomp.texi
	@touch $@


CLEANFILES = $(STAMP_GENINSRC) $(STAMP_BUILD_INFO) libgomp.info
MAINTAINERCLEANFILES = $(srcdir)/libgomp.info
