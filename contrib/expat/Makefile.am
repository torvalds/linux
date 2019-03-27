#
#                          __  __            _
#                       ___\ \/ /_ __   __ _| |_
#                      / _ \\  /| '_ \ / _` | __|
#                     |  __//  \| |_) | (_| | |_
#                      \___/_/\_\ .__/ \__,_|\__|
#                               |_| XML parser
#
# Copyright (c) 2017 Expat development team
# Licensed under the MIT license:
#
# Permission is  hereby granted,  free of charge,  to any  person obtaining
# a  copy  of  this  software   and  associated  documentation  files  (the
# "Software"),  to  deal in  the  Software  without restriction,  including
# without  limitation the  rights  to use,  copy,  modify, merge,  publish,
# distribute, sublicense, and/or sell copies of the Software, and to permit
# persons  to whom  the Software  is  furnished to  do so,  subject to  the
# following conditions:
#
# The above copyright  notice and this permission notice  shall be included
# in all copies or substantial portions of the Software.
#
# THE  SOFTWARE  IS  PROVIDED  "AS  IS",  WITHOUT  WARRANTY  OF  ANY  KIND,
# EXPRESS  OR IMPLIED,  INCLUDING  BUT  NOT LIMITED  TO  THE WARRANTIES  OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
# NO EVENT SHALL THE AUTHORS OR  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR  OTHER LIABILITY, WHETHER  IN AN  ACTION OF CONTRACT,  TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
# USE OR OTHER DEALINGS IN THE SOFTWARE.

AUTOMAKE_OPTIONS = \
    dist-bzip2 \
    foreign \
    no-dist-gzip \
    subdir-objects

ACLOCAL_AMFLAGS = -I m4
LIBTOOLFLAGS = --verbose

SUBDIRS = lib examples tests  # lib goes first to build first
if WITH_XMLWF
SUBDIRS += xmlwf doc
endif

pkgconfig_DATA = expat.pc
pkgconfigdir = $(libdir)/pkgconfig


_EXTRA_DIST_CMAKE = \
    CMakeLists.txt \
    CMake.README \
    ConfigureChecks.cmake \
    expat_config.h.cmake

_EXTRA_DIST_WINDOWS = \
    examples/elements.vcxproj \
    examples/elements.vcxproj.filters \
    examples/outline.vcxproj \
    examples/outline.vcxproj.filters \
    \
    lib/expat_static.vcxproj \
    lib/expat_static.vcxproj.filters \
    lib/expat.vcxproj \
    lib/expat.vcxproj.filters \
    lib/expatw_static.vcxproj \
    lib/expatw_static.vcxproj.filters \
    lib/expatw.vcxproj \
    lib/expatw.vcxproj.filters \
    \
    tests/benchmark/benchmark.sln \
    tests/benchmark/benchmark.vcxproj \
    \
    tests/runtests.sln \
    tests/runtests.vcxproj \
    tests/runtests.vcxproj.filters \
    \
    win32/expat.iss \
    win32/MANIFEST.txt \
    win32/README.txt \
    \
    xmlwf/xmlwf.vcxproj \
    xmlwf/xmlwf.vcxproj.filters \
    \
    expat.sln

EXTRA_DIST = \
    $(_EXTRA_DIST_CMAKE) \
    $(_EXTRA_DIST_WINDOWS) \
    \
    conftools/expat.m4 \
    conftools/get-version.sh \
    conftools/PrintPath \
    \
    Changes \
    README.md \
    test-driver-wrapper.sh


.PHONY: buildlib
buildlib:
	@echo 'ERROR: Running "make buildlib LIBRARY=libexpatw.la"' >&2
	@echo 'ERROR: is no longer supported.  INSTEAD please:' >&2
	@echo 'ERROR:' >&2
	@echo 'ERROR:  * Mass-patch Makefile.am, e.g.' >&2
	@echo 'ERROR:    # find -name Makefile.am -exec sed \' >&2
	@echo 'ERROR:          -e "s,libexpat\.la,libexpatw.la," \' >&2
	@echo 'ERROR:          -e "s,libexpat_la,libexpatw_la," \' >&2
	@echo 'ERROR:          -i {} +' >&2
	@echo 'ERROR:' >&2
	@echo 'ERROR:  * Run automake to re-generate Makefile.in files' >&2
	@echo 'ERROR:' >&2
	@echo 'ERROR:  * Use "./configure --without-xmlwf" and/or' >&2
	@echo 'ERROR:    "make -C lib all install" to bypass compilation' >&2
	@echo 'ERROR:    of xmlwf (e.g. with -DXML_UNICODE)' >&2
	@echo 'ERROR:' >&2
	@false


.PHONY: run-benchmark
run-benchmark:
	$(MAKE) -C tests/benchmark
	./run.sh tests/benchmark/benchmark@EXEEXT@ -n $(top_srcdir)/../testdata/largefiles/recset.xml 65535 3

tests/xmlts.zip:
	if test "$(XMLTS_ZIP)" = ""; then \
		wget --output-document=tests/xmlts.zip \
			https://www.w3.org/XML/Test/xmlts20080827.zip; \
	else \
		cp $(XMLTS_ZIP) tests/xmlts.zip; \
	fi

tests/xmlconf: tests/xmlts.zip
	cd tests && unzip -q xmlts.zip

.PHONY: run-xmltest
run-xmltest: tests/xmlconf
if WITH_XMLWF
	$(MAKE) -C xmlwf
	tests/xmltest.sh "$(PWD)/run.sh $(PWD)/xmlwf/xmlwf@EXEEXT@" 2>&1 | tee tests/xmltest.log
	dos2unix tests/xmltest.log
	diff -u tests/xmltest.log.expected tests/xmltest.log
else
	@echo 'ERROR: xmlwf is needed for "make run-xmltest".' >&2
	@echo 'ERROR: Please re-configure without --without-xmlwf.' >&2
	@false
endif

.PHONY: qa
qa:
	./qa.sh address
	./qa.sh memory
	./qa.sh undefined
	./qa.sh coverage
