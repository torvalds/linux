# Automake file

ACLOCAL_AMFLAGS = -I m4

# Nonstandard package files for distribution
EXTRA_DIST = \
  CHANGES \
  CONTRIBUTORS \
  LICENSE \
  include/gtest/gtest-param-test.h.pump \
  include/gtest/internal/gtest-param-util-generated.h.pump \
  include/gtest/internal/gtest-tuple.h.pump \
  include/gtest/internal/gtest-type-util.h.pump \
  make/Makefile \
  scripts/fuse_gtest_files.py \
  scripts/gen_gtest_pred_impl.py \
  scripts/pump.py \
  scripts/test/Makefile

# gtest source files that we don't compile directly.  They are
# #included by gtest-all.cc.
GTEST_SRC = \
  src/gtest-death-test.cc \
  src/gtest-filepath.cc \
  src/gtest-internal-inl.h \
  src/gtest-port.cc \
  src/gtest-printers.cc \
  src/gtest-test-part.cc \
  src/gtest-typed-test.cc \
  src/gtest.cc

EXTRA_DIST += $(GTEST_SRC)

# Sample files that we don't compile.
EXTRA_DIST += \
  samples/prime_tables.h \
  samples/sample1_unittest.cc \
  samples/sample2_unittest.cc \
  samples/sample3_unittest.cc \
  samples/sample4_unittest.cc \
  samples/sample5_unittest.cc \
  samples/sample6_unittest.cc \
  samples/sample7_unittest.cc \
  samples/sample8_unittest.cc \
  samples/sample9_unittest.cc

# C++ test files that we don't compile directly.
EXTRA_DIST += \
  test/gtest-death-test_ex_test.cc \
  test/gtest-death-test_test.cc \
  test/gtest-filepath_test.cc \
  test/gtest-linked_ptr_test.cc \
  test/gtest-listener_test.cc \
  test/gtest-message_test.cc \
  test/gtest-options_test.cc \
  test/googletest-param-test2-test.cc \
  test/googletest-param-test2-test.cc \
  test/googletest-param-test-test.cc \
  test/googletest-param-test-test.cc \
  test/gtest-param-test_test.h \
  test/gtest-port_test.cc \
  test/gtest_premature_exit_test.cc \
  test/gtest-printers_test.cc \
  test/gtest-test-part_test.cc \
  test/googletest-tuple-test.cc \
  test/gtest-typed-test2_test.cc \
  test/gtest-typed-test_test.cc \
  test/gtest-typed-test_test.h \
  test/gtest-unittest-api_test.cc \
  test/googletest-break-on-failure-unittest_.cc \
  test/googletest-catch-exceptions-test_.cc \
  test/googletest-color-test_.cc \
  test/googletest-env-var-test_.cc \
  test/gtest_environment_test.cc \
  test/googletest-filter-unittest_.cc \
  test/gtest_help_test_.cc \
  test/googletest-list-tests-unittest_.cc \
  test/gtest_main_unittest.cc \
  test/gtest_no_test_unittest.cc \
  test/googletest-output-test_.cc \
  test/gtest_pred_impl_unittest.cc \
  test/gtest_prod_test.cc \
  test/gtest_repeat_test.cc \
  test/googletest-shuffle-test_.cc \
  test/gtest_sole_header_test.cc \
  test/gtest_stress_test.cc \
  test/gtest_throw_on_failure_ex_test.cc \
  test/googletest-throw-on-failure-test_.cc \
  test/googletest-uninitialized-test_.cc \
  test/gtest_unittest.cc \
  test/gtest_unittest.cc \
  test/gtest_xml_outfile1_test_.cc \
  test/gtest_xml_outfile2_test_.cc \
  test/gtest_xml_output_unittest_.cc \
  test/production.cc \
  test/production.h

# Python tests that we don't run.
EXTRA_DIST += \
  test/googletest-break-on-failure-unittest.py \
  test/googletest-catch-exceptions-test.py \
  test/googletest-color-test.py \
  test/googletest-env-var-test.py \
  test/googletest-filter-unittest.py \
  test/gtest_help_test.py \
  test/googletest-list-tests-unittest.py \
  test/googletest-output-test.py \
  test/googletest-output-test_golden_lin.txt \
  test/googletest-shuffle-test.py \
  test/gtest_test_utils.py \
  test/googletest-throw-on-failure-test.py \
  test/googletest-uninitialized-test.py \
  test/gtest_xml_outfiles_test.py \
  test/gtest_xml_output_unittest.py \
  test/gtest_xml_test_utils.py

# CMake script
EXTRA_DIST += \
  CMakeLists.txt \
  cmake/internal_utils.cmake

# MSVC project files
EXTRA_DIST += \
  msvc/2010/gtest-md.sln \
  msvc/2010/gtest-md.vcxproj \
  msvc/2010/gtest.sln \
  msvc/2010/gtest.vcxproj \
  msvc/2010/gtest_main-md.vcxproj \
  msvc/2010/gtest_main.vcxproj \
  msvc/2010/gtest_prod_test-md.vcxproj \
  msvc/2010/gtest_prod_test.vcxproj \
  msvc/2010/gtest_unittest-md.vcxproj \
  msvc/2010/gtest_unittest.vcxproj

# xcode project files
EXTRA_DIST += \
  xcode/Config/DebugProject.xcconfig \
  xcode/Config/FrameworkTarget.xcconfig \
  xcode/Config/General.xcconfig \
  xcode/Config/ReleaseProject.xcconfig \
  xcode/Config/StaticLibraryTarget.xcconfig \
  xcode/Config/TestTarget.xcconfig \
  xcode/Resources/Info.plist \
  xcode/Scripts/runtests.sh \
  xcode/Scripts/versiongenerate.py \
  xcode/gtest.xcodeproj/project.pbxproj

# xcode sample files
EXTRA_DIST += \
  xcode/Samples/FrameworkSample/Info.plist \
  xcode/Samples/FrameworkSample/WidgetFramework.xcodeproj/project.pbxproj \
  xcode/Samples/FrameworkSample/runtests.sh \
  xcode/Samples/FrameworkSample/widget.cc \
  xcode/Samples/FrameworkSample/widget.h \
  xcode/Samples/FrameworkSample/widget_test.cc

# C++Builder project files
EXTRA_DIST += \
  codegear/gtest.cbproj \
  codegear/gtest.groupproj \
  codegear/gtest_all.cc \
  codegear/gtest_link.cc \
  codegear/gtest_main.cbproj \
  codegear/gtest_unittest.cbproj

# Distribute and install M4 macro
m4datadir = $(datadir)/aclocal
m4data_DATA = m4/gtest.m4
EXTRA_DIST += $(m4data_DATA)

# We define the global AM_CPPFLAGS as everything we compile includes from these
# directories.
AM_CPPFLAGS = -I$(srcdir) -I$(srcdir)/include

# Modifies compiler and linker flags for pthreads compatibility.
if HAVE_PTHREADS
  AM_CXXFLAGS = @PTHREAD_CFLAGS@ -DGTEST_HAS_PTHREAD=1
  AM_LIBS = @PTHREAD_LIBS@
else
  AM_CXXFLAGS = -DGTEST_HAS_PTHREAD=0
endif

# Build rules for libraries.
lib_LTLIBRARIES = lib/libgtest.la lib/libgtest_main.la

lib_libgtest_la_SOURCES = src/gtest-all.cc

pkginclude_HEADERS = \
  include/gtest/gtest-death-test.h \
  include/gtest/gtest-message.h \
  include/gtest/gtest-param-test.h \
  include/gtest/gtest-printers.h \
  include/gtest/gtest-spi.h \
  include/gtest/gtest-test-part.h \
  include/gtest/gtest-typed-test.h \
  include/gtest/gtest.h \
  include/gtest/gtest_pred_impl.h \
  include/gtest/gtest_prod.h

pkginclude_internaldir = $(pkgincludedir)/internal
pkginclude_internal_HEADERS = \
  include/gtest/internal/gtest-death-test-internal.h \
  include/gtest/internal/gtest-filepath.h \
  include/gtest/internal/gtest-internal.h \
  include/gtest/internal/gtest-linked_ptr.h \
  include/gtest/internal/gtest-param-util-generated.h \
  include/gtest/internal/gtest-param-util.h \
  include/gtest/internal/gtest-port.h \
  include/gtest/internal/gtest-port-arch.h \
  include/gtest/internal/gtest-string.h \
  include/gtest/internal/gtest-tuple.h \
  include/gtest/internal/gtest-type-util.h \
  include/gtest/internal/custom/gtest.h \
  include/gtest/internal/custom/gtest-port.h \
  include/gtest/internal/custom/gtest-printers.h

lib_libgtest_main_la_SOURCES = src/gtest_main.cc
lib_libgtest_main_la_LIBADD = lib/libgtest.la

# Build rules for samples and tests. Automake's naming for some of
# these variables isn't terribly obvious, so this is a brief
# reference:
#
# TESTS -- Programs run automatically by "make check"
# check_PROGRAMS -- Programs built by "make check" but not necessarily run

TESTS=
TESTS_ENVIRONMENT = GTEST_SOURCE_DIR="$(srcdir)/test" \
                    GTEST_BUILD_DIR="$(top_builddir)/test"
check_PROGRAMS=

# A simple sample on using gtest.
TESTS += samples/sample1_unittest \
    samples/sample2_unittest \
    samples/sample3_unittest \
    samples/sample4_unittest \
    samples/sample5_unittest \
    samples/sample6_unittest \
    samples/sample7_unittest \
    samples/sample8_unittest \
    samples/sample9_unittest \
    samples/sample10_unittest
check_PROGRAMS += samples/sample1_unittest \
    samples/sample2_unittest \
    samples/sample3_unittest \
    samples/sample4_unittest \
    samples/sample5_unittest \
    samples/sample6_unittest \
    samples/sample7_unittest \
    samples/sample8_unittest \
    samples/sample9_unittest \
    samples/sample10_unittest

samples_sample1_unittest_SOURCES = samples/sample1_unittest.cc samples/sample1.cc
samples_sample1_unittest_LDADD = lib/libgtest_main.la \
                                 lib/libgtest.la
samples_sample2_unittest_SOURCES = samples/sample2_unittest.cc samples/sample2.cc
samples_sample2_unittest_LDADD = lib/libgtest_main.la \
                                 lib/libgtest.la
samples_sample3_unittest_SOURCES = samples/sample3_unittest.cc
samples_sample3_unittest_LDADD = lib/libgtest_main.la \
                                 lib/libgtest.la
samples_sample4_unittest_SOURCES = samples/sample4_unittest.cc samples/sample4.cc
samples_sample4_unittest_LDADD = lib/libgtest_main.la \
                                 lib/libgtest.la
samples_sample5_unittest_SOURCES = samples/sample5_unittest.cc samples/sample1.cc
samples_sample5_unittest_LDADD = lib/libgtest_main.la \
                                 lib/libgtest.la
samples_sample6_unittest_SOURCES = samples/sample6_unittest.cc
samples_sample6_unittest_LDADD = lib/libgtest_main.la \
                                 lib/libgtest.la
samples_sample7_unittest_SOURCES = samples/sample7_unittest.cc
samples_sample7_unittest_LDADD = lib/libgtest_main.la \
                                 lib/libgtest.la
samples_sample8_unittest_SOURCES = samples/sample8_unittest.cc
samples_sample8_unittest_LDADD = lib/libgtest_main.la \
                                 lib/libgtest.la

# Also verify that libgtest works by itself.
samples_sample9_unittest_SOURCES = samples/sample9_unittest.cc
samples_sample9_unittest_LDADD = lib/libgtest.la
samples_sample10_unittest_SOURCES = samples/sample10_unittest.cc
samples_sample10_unittest_LDADD = lib/libgtest.la

# This tests most constructs of gtest and verifies that libgtest_main
# and libgtest work.
TESTS += test/gtest_all_test
check_PROGRAMS += test/gtest_all_test
test_gtest_all_test_SOURCES = test/gtest_all_test.cc
test_gtest_all_test_LDADD = lib/libgtest_main.la \
                            lib/libgtest.la

# Tests that fused gtest files compile and work.
FUSED_GTEST_SRC = \
  fused-src/gtest/gtest-all.cc \
  fused-src/gtest/gtest.h \
  fused-src/gtest/gtest_main.cc

if HAVE_PYTHON
TESTS += test/fused_gtest_test
check_PROGRAMS += test/fused_gtest_test
test_fused_gtest_test_SOURCES = $(FUSED_GTEST_SRC) \
                                samples/sample1.cc samples/sample1_unittest.cc
test_fused_gtest_test_CPPFLAGS = -I"$(srcdir)/fused-src"

# Build rules for putting fused Google Test files into the distribution
# package. The user can also create those files by manually running
# scripts/fuse_gtest_files.py.
$(test_fused_gtest_test_SOURCES): fused-gtest

fused-gtest: $(pkginclude_HEADERS) $(pkginclude_internal_HEADERS) \
             $(GTEST_SRC) src/gtest-all.cc src/gtest_main.cc \
             scripts/fuse_gtest_files.py
	mkdir -p "$(srcdir)/fused-src"
	chmod -R u+w "$(srcdir)/fused-src"
	rm -f "$(srcdir)/fused-src/gtest/gtest-all.cc"
	rm -f "$(srcdir)/fused-src/gtest/gtest.h"
	"$(srcdir)/scripts/fuse_gtest_files.py" "$(srcdir)/fused-src"
	cp -f "$(srcdir)/src/gtest_main.cc" "$(srcdir)/fused-src/gtest/"

maintainer-clean-local:
	rm -rf "$(srcdir)/fused-src"
endif

# Death tests may produce core dumps in the build directory. In case
# this happens, clean them to keep distcleancheck happy.
CLEANFILES = core

# Disables 'make install' as installing a compiled version of Google
# Test can lead to undefined behavior due to violation of the
# One-Definition Rule.

install-exec-local:
	echo "'make install' is dangerous and not supported. Instead, see README for how to integrate Google Test into your build system."
	false

install-data-local:
	echo "'make install' is dangerous and not supported. Instead, see README for how to integrate Google Test into your build system."
	false
