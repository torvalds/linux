#
# $Id$
#
# Copyright 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

AM_CFLAGS = \
    -I${top_srcdir} \
    -I${top_srcdir}/libxo \
    ${GETTEXT_CFLAGS}

# Ick: maintained by hand!
TEST_CASES = \
gt_01.c

gt_01_test_SOURCES = gt_01.c

# TEST_CASES := $(shell cd ${srcdir} ; echo *.c )

noinst_PROGRAMS = ${TEST_CASES:.c=.test}

LDADD = \
    ${top_builddir}/libxo/libxo.la

if HAVE_HUMANIZE_NUMBER
LDADD += -lutil
endif

EXTRA_DIST = \
    ${TEST_CASES} \
    ${addprefix saved/, ${TEST_CASES:.c=.T.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.T.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.XP.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.XP.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.JP.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.JP.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.HP.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.HP.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.X.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.X.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.J.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.J.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.H.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.H.out}} \
    ${addprefix saved/, ${TEST_CASES:.c=.HIPx.err}} \
    ${addprefix saved/, ${TEST_CASES:.c=.HIPx.out}}

POT_FILES = \
    gt_01.pot \
    ldns.pot \
    strerror.pot

PO_FILES = \
    po/pig_latin/gt_01.po \
    po/pig_latin/ldns.po \
    po/pig_latin/strerror.po

EXTRA_DIST += ${POT_FILES} ${PO_FILES}

S2O = | ${SED} '1,/@@/d'

all:

valgrind:
	@echo '## Running the regression tests under Valgrind'
	${MAKE} CHECKER='valgrind -q' tests

#TEST_TRACE = set -x ; 

TEST_ONE = \
      ${CHECKER} ./$$base.test --libxo:W$$fmt ${TEST_OPTS} \
      > out/$$base.$$fmt.out 2> out/$$base.$$fmt.err ; \
 ${DIFF} -Nu ${srcdir}/saved/$$base.$$fmt.out out/$$base.$$fmt.out ${S2O} ; \
 ${DIFF} -Nu ${srcdir}/saved/$$base.$$fmt.err out/$$base.$$fmt.err ${S2O}

TEST_FORMATS = T XP JP HP X J H HIPx

test tests: ${bin_PROGRAMS} build-mo-files
	@${MKDIR} -p out
	-@ ${TEST_TRACE} (for test in ${TEST_CASES} ; do \
	    base=`${BASENAME} $$test .c` ; \
            (for fmt in ${TEST_FORMATS}; do \
	        echo "... $$test ... $$fmt ..."; \
	        ${TEST_ONE}; \
                true; \
            done) \
	done)

one:
	-@(test=${TEST_CASE}; data=${TEST_DATA}; ${TEST_ONE} ; true)

accept:
	-@(for test in ${TEST_CASES} ; do \
	    base=`${BASENAME} $$test .c` ; \
            (for fmt in ${TEST_FORMATS}; do \
	        echo "... $$test ... $$fmt ..."; \
	        ${CP} out/$$base.$$fmt.out ${srcdir}/saved/$$base.$$fmt.out ; \
	        ${CP} out/$$base.$$fmt.err ${srcdir}/saved/$$base.$$fmt.err ; \
	    done) \
	done)

.c.test:
	$(AM_V_CC)$(LTCOMPILE) -MT $@ -MD -MP -MF $(DEPDIR)/$*.Tpo -o $@ $<

CLEANFILES = ${TEST_CASES:.c=.test}
CLEANDIRS = out

clean-local:
	rm -rf ${CLEANDIRS}

XGETTEXT = ${GETTEXT_BINDIR}/xgettext
MSGFMT = ${GETTEXT_BINDIR}/msgfmt -v
MSGMERGE = ${GETTEXT_BINDIR}/msgmerge
ECHO = echo
DB=set -x;
XOMSGMERGE = ${MSGMERGE} --no-wrap
XODIFF = ${DIFF} -bu

LANGUAGES = \
	es \
	fr \
	pig_latin

# ldns is fake; used only for a gettext domain
FAKE_FILES = ldns strerror
MO_BASE_FILES = ${TEST_CASES:.c=} ${FAKE_FILES}

build-pot-files:
	for file in ${TEST_CASES} ; do set -x ;\
	    base=`${BASENAME} $$file .c` ; \
	    ${XGETTEXT} --default-domain=$$base \
            --directory=${srcdir} --no-wrap \
	    --add-comments --keyword=xo_emit --keyword=xo_emit_h \
	    --keyword=xo_emit_warn \
	    -C -E -n --foreign-user \
	    -o $$base.pot.new $$base.c ; \
	done

accept-pot-files:
	for base in ${MO_BASE_FILES} ; do set -x ;\
	    ${CP} $$base.pot.new ${srcdir}/$$base.pot ; \
	done

merge-po-files:
	for base in ${MO_BASE_FILES} ; do set -x ;\
	    for lang in ${LANGUAGES} ; do \
		if [ -f po/$$lang/$$base.po ]; then \
		    ${ECHO} "merging $$base.pot into po/$$lang/$$base.po ..." ; \
		    if ${XOMSGMERGE} po/$$lang/$$base.po \
				${srcdir}/$$base.pot \
				-o po/$$lang/$$base.new.po; then \
			${MV} po/$$lang/$$base.po \
				po/$$lang/$$base.po.old ; \
			${MV} po/$$lang/$$base.new.po \
				po/$$lang/$$base.po ; \
		    else \
		      echo "error: msgmerge for $$base failed"; \
		    fi ; \
		elif [ -f ${srcdir}/po/$$lang/$$base.po ]; then \
		    ${ECHO} "merging (srcdir) $$base.pot into po/$$lang/$$base.po ..." ; \
		    if ${XOMSGMERGE} ${srcdir}/po/$$lang/$$base.po \
				${srcdir}/$$base.pot \
				-o po/$$lang/$$base.new.po; then \
			${MV} po/$$lang/$$base.po \
				po/$$lang/$$base.po.old ; \
			${MV} po/$$lang/$$base.new.po \
				po/$$lang/$$base.po ; \
		    else \
		      echo "error: msgmerge for $$base failed"; \
		    fi ; \
		fi ; \
            done ; \
        done

accept-po-files:
	@(for base in ${MO_BASE_FILES} ; do \
	    for lang in ${LANGUAGES} ; do \
		if [ -f po/$$lang/$$base.po ]; then \
		    ${MKDIR} -p ${srcdir}/po/$$lang ; \
		    (${DB} ${CP} po/$$lang/$$base.po ${srcdir}/po/$$lang/$$base.po ); \
                fi ; \
	    done ; \
	done)

new-po-file:
	@(for base in ${MO_BASE_FILES} ; do set -x ;\
	    for lang in ${LANGUAGES} ; do \
		if [ ! -f po/$$lang/$$base.po ]; then \
		    ${MKDIR} -p po/$$lang ; \
		    (${DB} ${CP} $$base.pot po/$$lang/$$base.po ); \
                fi ; \
	    done ; \
	done)

diff:
	@(for base in ${MO_BASE_FILES} ; do \
	    if [ -f $$base.pot.new ] ; then \
	        ${XODIFF} ${srcdir}/$$base.pot $$base.pot.new ; \
            fi ; \
	    for lang in ${LANGUAGES} ; do \
		if [ -f po/$$lang/$$base.po ] ; then \
		    ${XODIFF} ${srcdir}/po/$$lang/$$base.po po/$$lang/$$base.po ; \
                fi ; \
	    done ; \
	done)

mo build-mo-files:
	@(for base in ${MO_BASE_FILES} ; do \
	    for lang in ${LANGUAGES} ; do \
		${MKDIR} -p po/$$lang/LC_MESSAGES ; \
	 	if [ -f po/$$lang/$$base.po ] ; then \
		    (${DB} ${MSGFMT} -o po/$$lang/LC_MESSAGES/$$base.mo \
				po/$$lang/$$base.po ); \
		elif [ -f ${srcdir}/po/$$lang/$$base.po ]; then \
		    (${DB} ${MSGFMT} -o po/$$lang/LC_MESSAGES/$$base.mo \
			${srcdir}/po/$$lang/$$base.po ;) \
                fi ; \
	    done ; \
	done)
