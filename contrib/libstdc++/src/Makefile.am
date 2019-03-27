## Makefile for the src subdirectory of the GNU C++ Standard library.
##
## Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2005
## Free Software Foundation, Inc.
##
## This file is part of the libstdc++ version 3 distribution.
## Process this file with automake to produce Makefile.in.

## This file is part of the GNU ISO C++ Library.  This library is free
## software; you can redistribute it and/or modify it under the
## terms of the GNU General Public License as published by the
## Free Software Foundation; either version 2, or (at your option)
## any later version.

## This library is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.

## You should have received a copy of the GNU General Public License along
## with this library; see the file COPYING.  If not, write to the Free
## Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
## USA.

include $(top_srcdir)/fragment.am

# Cross compiler support.
toolexeclib_LTLIBRARIES = libstdc++.la

# Symbol versioning for shared libraries.
if ENABLE_SYMVERS
libstdc++-symbols.ver:  ${glibcxx_srcdir}/$(SYMVER_FILE) \
		$(port_specific_symbol_files)
	cp ${glibcxx_srcdir}/$(SYMVER_FILE) ./libstdc++-symbols.ver
	if test "x$(port_specific_symbol_files)" != x; then \
	  if grep '^# Appended to version file.' \
	       $(port_specific_symbol_files) /dev/null > /dev/null 2>&1; then \
	    cat $(port_specific_symbol_files) >> $@; \
	  else \
	    sed -n '1,/DO NOT DELETE/p' $@ > tmp.top; \
	    sed -n '/DO NOT DELETE/,$$p' $@ > tmp.bottom; \
	    cat tmp.top $(port_specific_symbol_files) tmp.bottom > $@; \
	    rm tmp.top tmp.bottom; \
	  fi; \
	fi

if ENABLE_SYMVERS_GNU
version_arg = -Wl,--version-script=libstdc++-symbols.ver
version_dep = libstdc++-symbols.ver
endif
if ENABLE_SYMVERS_GNU_NAMESPACE
version_arg = -Wl,--version-script=libstdc++-symbols.ver
version_dep = libstdc++-symbols.ver
endif
if ENABLE_SYMVERS_DARWIN
version_arg = -Wl,-exported_symbols_list,libstdc++-symbols.explist
version_dep = libstdc++-symbols.explist
libstdc++-symbols.explist : libstdc++-symbols.ver \
		${glibcxx_srcdir}/scripts/make_exports.pl \
		$(libstdc___la_OBJECTS) $(libstdc___la_LIBADD)
	perl ${glibcxx_srcdir}/scripts/make_exports.pl \
	  libstdc++-symbols.ver \
	  $(libstdc___la_OBJECTS:%.lo=.libs/%.o) \
	 `echo $(libstdc___la_LIBADD) | \
	    sed 's,/\([^/.]*\)\.la,/.libs/\1.a,g'` \
	 > $@ || (rm -f $@ ; exit 1)
endif
else
version_arg =
version_dep =
endif


# Source files linked in via configuration/make substitution for a
# particular host.
host_sources = \
	atomicity.cc \
	codecvt_members.cc \
	collate_members.cc \
	ctype_members.cc \
	messages_members.cc \
	monetary_members.cc \
	numeric_members.cc \
	time_members.cc 

codecvt_members.cc: ${glibcxx_srcdir}/$(CCODECVT_CC)
	$(LN_S) ${glibcxx_srcdir}/$(CCODECVT_CC) . || true

collate_members.cc: ${glibcxx_srcdir}/$(CCOLLATE_CC)
	$(LN_S) ${glibcxx_srcdir}/$(CCOLLATE_CC) . || true

ctype_members.cc: ${glibcxx_srcdir}/$(CCTYPE_CC)
	$(LN_S) ${glibcxx_srcdir}/$(CCTYPE_CC) . || true

messages_members.cc: ${glibcxx_srcdir}/$(CMESSAGES_CC)
	$(LN_S) ${glibcxx_srcdir}/$(CMESSAGES_CC) . || true

monetary_members.cc: ${glibcxx_srcdir}/$(CMONEY_CC)
	$(LN_S) ${glibcxx_srcdir}/$(CMONEY_CC) . || true

numeric_members.cc: ${glibcxx_srcdir}/$(CNUMERIC_CC)
	$(LN_S) ${glibcxx_srcdir}/$(CNUMERIC_CC) . || true

time_members.cc: ${glibcxx_srcdir}/$(CTIME_CC)
	$(LN_S) ${glibcxx_srcdir}/$(CTIME_CC) . || true

atomicity_file = ${glibcxx_srcdir}/$(ATOMICITY_SRCDIR)/atomicity.h
atomicity.cc: ${atomicity_file}
	$(LN_S) ${atomicity_file} ./atomicity.cc || true

# Source files linked in via configuration/make substitution for a
# particular host, but with ad hoc naming rules.
host_sources_extra = \
	basic_file.cc \
	c++locale.cc

c++locale.cc: ${glibcxx_srcdir}/$(CLOCALE_CC)
	$(LN_S) ${glibcxx_srcdir}/$(CLOCALE_CC) ./$@ || true

basic_file.cc: ${glibcxx_srcdir}/$(BASIC_FILE_CC)
	$(LN_S) ${glibcxx_srcdir}/$(BASIC_FILE_CC) ./$@ || true

if GLIBCXX_LDBL_COMPAT
ldbl_compat_sources = compatibility-ldbl.cc
else
ldbl_compat_sources =
endif

# Sources present in the src directory.
sources = \
	bitmap_allocator.cc \
	pool_allocator.cc \
	mt_allocator.cc \
	codecvt.cc \
	compatibility.cc \
	complex_io.cc \
	ctype.cc \
	debug.cc \
	debug_list.cc \
	functexcept.cc \
	globals_io.cc \
	ios.cc \
	ios_failure.cc \
	ios_init.cc \
	ios_locale.cc \
	limits.cc \
	list.cc \
	locale.cc \
	locale_init.cc \
	locale_facets.cc \
	localename.cc \
	stdexcept.cc \
	strstream.cc \
	tree.cc \
	allocator-inst.cc \
	concept-inst.cc \
	fstream-inst.cc \
	ext-inst.cc \
	ios-inst.cc \
	iostream-inst.cc \
	istream-inst.cc \
	istream.cc \
	locale-inst.cc \
	misc-inst.cc \
	ostream-inst.cc \
	sstream-inst.cc \
	streambuf-inst.cc \
	streambuf.cc \
	string-inst.cc \
	valarray-inst.cc \
	wlocale-inst.cc \
	wstring-inst.cc \
	${host_sources} \
	${host_sources_extra} \
	${ldbl_compat_sources}

VPATH = $(top_srcdir)/src:$(top_srcdir)

libstdc___la_SOURCES = $(sources)

libstdc___la_LIBADD = \
	$(top_builddir)/libmath/libmath.la \
	$(top_builddir)/libsupc++/libsupc++convenience.la

libstdc___la_DEPENDENCIES = ${version_dep} $(libstdc___la_LIBADD)

libstdc___la_LDFLAGS = \
	-version-info $(libtool_VERSION) ${version_arg} -lm 

# Use special rules for the deprecated source files so that they find
# deprecated include files.
GLIBCXX_INCLUDE_DIR=$(glibcxx_builddir)/include
strstream.lo: strstream.cc
	$(LTCXXCOMPILE) -I$(GLIBCXX_INCLUDE_DIR)/backward -Wno-deprecated -c $<
strstream.o: strstream.cc
	$(CXXCOMPILE) -I$(GLIBCXX_INCLUDE_DIR)/backward -Wno-deprecated -c $<

# Use special rules for the concept-checking instantiations so that all
# the generated template functions are also instantiated.  Force the checks
# to be on so that the instantiations are actually seen.
concept-inst.lo: concept-inst.cc
	$(LTCXXCOMPILE) -D_GLIBCXX_CONCEPT_CHECKS -fimplicit-templates -c $<
concept-inst.o: concept-inst.cc
	$(CXXCOMPILE) -D_GLIBCXX_CONCEPT_CHECKS -fimplicit-templates -c $<

if GLIBCXX_LDBL_COMPAT
# Use special rules for compatibility-ldbl.cc compilation, as we need to
# pass -mlong-double-64.
compatibility-ldbl.lo: compatibility-ldbl.cc
	$(LTCXXCOMPILE) -mlong-double-64 -c $<
compatibility-ldbl.o: compatibility-ldbl.cc
	$(CXXCOMPILE) -mlong-double-64 -c $<
endif

# AM_CXXFLAGS needs to be in each subdirectory so that it can be
# modified in a per-library or per-sub-library way.  Need to manually
# set this option because CONFIG_CXXFLAGS has to be after
# OPTIMIZE_CXXFLAGS on the compile line so that -O2 can be overridden
# as the occasion calls for it.
AM_CXXFLAGS = \
	-fno-implicit-templates \
	$(WARN_CXXFLAGS) \
	$(OPTIMIZE_CXXFLAGS) \
	$(CONFIG_CXXFLAGS)


# libstdc++ libtool notes

# 1) Need to explicitly set LTCXXCOMPILE so that AM_CXXFLAGS is
# last. (That way, things like -O2 passed down from the toplevel can
# be overridden by --enable-debug.)

# 2) In general, libtool expects an argument such as `--tag=CXX' when
# using the C++ compiler, because that will enable the settings
# detected when C++ support was being configured.  However, when no
# such flag is given in the command line, libtool attempts to figure
# it out by matching the compiler name in each configuration section
# against a prefix of the command line.  The problem is that, if the
# compiler name and its initial flags stored in the libtool
# configuration file don't match those in the command line, libtool
# can't decide which configuration to use, and it gives up.  The
# correct solution is to add `--tag CXX' to LTCXXCOMPILE and maybe
# CXXLINK, just after $(LIBTOOL), so that libtool doesn't have to
# attempt to infer which configuration to use
LTCXXCOMPILE = $(LIBTOOL) --tag CXX --mode=compile $(CXX) $(INCLUDES) \
	       $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) 

LTLDFLAGS = $(shell $(SHELL) $(top_srcdir)/../libtool-ldflags $(LDFLAGS))

# 3) We'd have a problem when building the shared libstdc++ object if
# the rules automake generates would be used.  We cannot allow g++ to
# be used since this would add -lstdc++ to the link line which of
# course is problematic at this point.  So, we get the top-level
# directory to configure libstdc++-v3 to use gcc as the C++
# compilation driver.
CXXLINK = $(LIBTOOL) --tag CXX --mode=link $(CXX) \
	  $(OPT_LDFLAGS) $(SECTION_LDFLAGS) $(AM_CXXFLAGS) $(LTLDFLAGS) -o $@


# Added bits to build debug library.
if GLIBCXX_BUILD_DEBUG
all-local: build_debug
install-data-local: install_debug
else
all-local:
install-data-local:
endif

debugdir = debug

# Build parallel set of debug objects here.
stamp-debug:
	if test ! -d ${debugdir}; then \
	  mkdir -p ${debugdir}; \
	  (cd ${debugdir}; \
	  sed -e 's/top_builddir = \.\./top_builddir = ..\/../' \
	      -e 's/srcdir = \.\./srcdir = ..\/../' \
	      -e 's/glibcxx_basedir = \.\./glibcxx_basedir = ..\/../' \
	      -e 's/all-local: build_debug/all-local:/' \
	      -e 's/install-data-local: install_debug/install-data-local:/' \
	  < ../Makefile > Makefile) ; \
	fi; \
	echo `date` > stamp-debug;

build_debug: stamp-debug
	(cd ${debugdir} && $(MAKE) CXXFLAGS='$(DEBUG_FLAGS)' all)

# Install debug library here.
install_debug:
	(cd ${debugdir} && $(MAKE) \
	toolexeclibdir=$(glibcxx_toolexeclibdir)/debug install)
