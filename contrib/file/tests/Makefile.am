check_PROGRAMS = test
test_LDADD = $(top_builddir)/src/libmagic.la
test_CPPFLAGS = -I$(top_builddir)/src

EXTRA_DIST = \
escapevel.result \
escapevel.testfile \
gedcom.result \
gedcom.testfile \
hddrawcopytool.result \
hddrawcopytool.testfile \
issue311docx.result \
issue311docx.testfile \
JW07022A.mp3.result \
JW07022A.mp3.testfile

T = $(top_srcdir)/tests
check-local:
	MAGIC=$(top_builddir)/magic/magic ./test
	set -e; for i in $T/*.testfile; do echo Running test: $$i; TZ=UTC MAGIC=$(top_builddir)/magic/magic ./test $$i $${i%%.testfile}.result; done
