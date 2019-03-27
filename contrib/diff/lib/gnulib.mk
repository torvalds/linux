# This file is generated automatically by "bootstrap".
BUILT_SOURCES += $(ALLOCA_H)
EXTRA_DIST += alloca_.h

# We need the following in order to create an <alloca.h> when the system
# doesn't have one that works with the given compiler.
all-local $(lib_OBJECTS): $(ALLOCA_H)
alloca.h: alloca_.h
	cp $(srcdir)/alloca_.h $@-t
	mv $@-t $@
MOSTLYCLEANFILES += alloca.h alloca.h-t

lib_SOURCES += c-stack.h c-stack.c

lib_SOURCES += dirname.h dirname.c basename.c stripslash.c


lib_SOURCES += exclude.h exclude.c

lib_SOURCES += exit.h

lib_SOURCES += exitfail.h exitfail.c


lib_SOURCES += file-type.h file-type.c

BUILT_SOURCES += $(FNMATCH_H)
EXTRA_DIST += fnmatch_.h fnmatch_loop.c

# We need the following in order to create an <fnmatch.h> when the system
# doesn't have one that supports the required API.
all-local $(lib_OBJECTS): $(FNMATCH_H)
fnmatch.h: fnmatch_.h
	cp $(srcdir)/fnmatch_.h $@-t
	mv $@-t $@
MOSTLYCLEANFILES += fnmatch.h fnmatch.h-t


lib_SOURCES += getopt.h getopt.c getopt1.c getopt_int.h

lib_SOURCES += gettext.h


lib_SOURCES += hard-locale.h hard-locale.c

EXTRA_DIST += inttostr.c
lib_SOURCES += imaxtostr.c inttostr.h offtostr.c umaxtostr.c



lib_SOURCES += posixver.h posixver.c


lib_SOURCES += regex.h


BUILT_SOURCES += $(STDBOOL_H)
EXTRA_DIST += stdbool_.h

# We need the following in order to create an <stdbool.h> when the system
# doesn't have one that works.
all-local $(lib_OBJECTS): $(STDBOOL_H)
stdbool.h: stdbool_.h
	sed -e 's/@''HAVE__BOOL''@/$(HAVE__BOOL)/g' < $(srcdir)/stdbool_.h > $@-t
	mv $@-t $@
MOSTLYCLEANFILES += stdbool.h stdbool.h-t

lib_SOURCES += strcase.h

lib_SOURCES += strftime.c







lib_SOURCES += time_r.h


lib_SOURCES += unlocked-io.h

lib_SOURCES += version-etc.h version-etc.c

lib_SOURCES += xalloc.h xmalloc.c xstrdup.c

lib_SOURCES += xstrtol.h xstrtol.c xstrtoul.c

lib_SOURCES += xstrtoumax.c

