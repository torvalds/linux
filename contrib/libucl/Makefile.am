ACLOCAL_AMFLAGS = -I m4
EXTRA_DIST = uthash klib README.md

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libucl.pc

if LUA_SUB
  LUA_SUBDIR = lua
endif

COVERAGE_INFO_FILE = $(top_builddir)/coverage.info
COVERAGE_REPORT_DIR = $(top_builddir)/coverage

.PHONY = coverage-requirement-check clean-coverage-report

coverage-requirement-check:
	@if test ! -e $(GCOV); then \
		echo "Cannot find $(GCOV). Please install gcov."; \
		exit 1; \
	fi

coverage: coverage-requirement-check clean-coverage coverage-build coverage-check coverage-report
	@echo "Please execute 'make clean' before 'make' or 'make check' to remove instrumented object files(compiled with -O0 etc.). Note that 'make clean' also remove coverage data."

coverage-build: coverage-requirement-check
	@if test `find $(top_builddir) -name "*.gcno" | wc -l` -eq 0; then \
		echo "Start to remove old non-instrumented object files..."; \
		$(MAKE) $(AM_MAKEFLAGS) clean; \
		echo "Successfully removed old non-instrumented object files."; \
	fi
	@echo "Start to build libraries with coverage options..."
	$(MAKE) $(AM_MAKEFLAGS) \
		CFLAGS="$(CFLAGS) $(COVERAGE_CFLAGS) $(COVERAGE_OPTFLAGS)" \
		CXXFLAGS="$(CXXFLAGS) $(COVERAGE_CXXFLAGS) $(COVERAGE_OPTFLAGS)" \
		LDFLAGS="$(LDFLAGS) $(COVERAGE_LDFLAGS)" \
		LIBS="$(LIBS) $(COVERAGE_LIBS)"
	@echo "Successfully built libraries with coverage options."

coverage-check: coverage-requirement-check
	@echo "Start to run tests with instrumented libraries..."
	$(MAKE) $(AM_MAKEFLAGS) check \
		CFLAGS="$(CFLAGS) $(COVERAGE_CFLAGS) $(COVERAGE_OPTFLAGS)" \
		CXXFLAGS="$(CXXFLAGS) $(COVERAGE_CXXFLAGS) $(COVERAGE_OPTFLAGS)" \
		LDFLAGS="$(LDFLAGS) $(COVERAGE_LDFLAGS)" \
		LIBS="$(LIBS) $(COVERAGE_LIBS)"
	@echo "Successfully run tests with instrumented libraries."

coverage-lcov: coverage-check coverage-requirement-check
	$(LCOV) --capture \
		--directory "$(top_builddir)/" \
		--output-file $(COVERAGE_INFO_FILE) \
		--gcov-tool $(GCOV) \
		--compat-libtool --checksum
	$(LCOV) --extract $(COVERAGE_INFO_FILE) `pwd`/src/ucl_\* \
		--output-file $(COVERAGE_INFO_FILE)

coverage-report: coverage-lcov
	@echo "Start to create coverage reports..."
	$(GENHTML) --prefix "$(top_srcdir)" \
		--output-directory $(COVERAGE_REPORT_DIR) \
		--title $(PACKAGE_NAME) \
		--legend --show-details \
		$(GENHTML_OPTIONS) \
		$(COVERAGE_INFO_FILE)
	@echo "Successfully created coverage reports into $(COVERAGE_REPORT_DIR) directory."

clean-coverage-report:
	-rm -rf $(COVERAGE_INFO_FILE)
	-rm -rf $(COVERAGE_REPORT_DIR)

clean-coverage: clean-coverage-report
	-$(LCOV) --gcov-tool $(GCOV) --zerocounters --directory $(top_builddir)
	@if xargs --version 2>/dev/null; then \
		find $(top_builddir) -name "*.gcno" | xargs --no-run-if-empty rm; \
	else \
		find $(top_builddir) -name "*.gcno" | xargs rm; \
	fi

clean-local: clean-coverage

SUBDIRS = src tests utils doc $(LUA_SUBDIR)
