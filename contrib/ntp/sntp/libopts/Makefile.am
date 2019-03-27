## LIBOPTS Makefile
MAINTAINERCLEANFILES    = Makefile.in
if INSTALL_LIBOPTS
lib_LTLIBRARIES         = libopts.la
else
noinst_LTLIBRARIES      = libopts.la
endif
libopts_la_SOURCES      = libopts.c
libopts_la_CPPFLAGS     = -I$(srcdir)
libopts_la_LDFLAGS      = -version-info  41:0:16
EXTRA_DIST		=
BUILT_SOURCES		=
MOSTLYCLEANFILES	=

AM_CFLAGS	= $(NTP_HARD_CFLAGS)
AM_CPPFLAGS	= $(NTP_HARD_CPPFLAGS)
AM_LDFLAGS	= $(NTP_HARD_LDFLAGS)

libopts.c:		$(BUILT_SOURCES)
	@: do-nothing rule to avoid default SCCS get


# Makefile fragment from gnulib-s stdnoreturn module:
#
_NORETURN_H=$(srcdir)/compat/_Noreturn.h
EXTRA_DIST += $(srcdir)/compat/_Noreturn.h
BUILT_SOURCES += $(STDNORETURN_H)
if GL_GENERATE_STDNORETURN_H
stdnoreturn.h: stdnoreturn.in.h $(top_builddir)/config.status $(_NORETURN_H)
	$(AM_V_GEN)rm -f $@-t $@ && \
	{ echo '/* DO NOT EDIT! GENERATED AUTOMATICALLY! */' && \
	  sed -e '/definition of _Noreturn/r $(_NORETURN_H)' \
              < $(srcdir)/stdnoreturn.in.h; \
	} > $@-t && \
	mv $@-t $@
else
stdnoreturn.h: $(top_builddir)/config.status
	rm -f $@
endif
MOSTLYCLEANFILES += stdnoreturn.h stdnoreturn.h-t
EXTRA_DIST += stdnoreturn.in.h

EXTRA_DIST += \
    COPYING.gplv3           COPYING.lgplv3          COPYING.mbsd  \
    MakeDefs.inc            README                  ag-char-map.h  \
    alias.c                 ao-strs.c               ao-strs.h  \
    autoopts/project.h      autoopts/options.h      autoopts/usage-txt.h  \
    autoopts.c              autoopts.h              boolean.c  \
    check.c                 compat/snprintf.c       compat/strchr.c  \
    compat/strdup.c         compat/_Noreturn.h      compat/pathfind.c  \
    compat/compat.h         compat/windows-config.h configfile.c  \
    cook.c                  enum.c                  env.c  \
    file.c                  find.c                  genshell.c  \
    genshell.h              gettext.h               init.c  \
    intprops.h              load.c                  m4/stdnoreturn.m4  \
    m4/liboptschk.m4        m4/libopts.m4           makeshell.c  \
    nested.c                numeric.c               option-value-type.c  \
    option-value-type.h     option-xat-attribute.c  option-xat-attribute.h  \
    parse-duration.c        parse-duration.h        pgusage.c  \
    proto.h                 putshell.c              reset.c  \
    restore.c               save.c                  sort.c  \
    stack.c                 stdnoreturn.in.h        streqvcmp.c  \
    text_mmap.c             time.c                  tokenize.c  \
    usage.c                 version.c
