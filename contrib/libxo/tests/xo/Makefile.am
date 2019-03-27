#
# $Id$
#
# Copyright 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

AM_CFLAGS = -I${top_srcdir} -I${top_srcdir}/libxo

# Ick: maintained by hand!
TEST_CASES = \
xo_01.sh

X=\
xo_02.sh \
xo_03.sh \
xo_04.sh \
xo_05.sh \
xo_06.sh

# TEST_CASES := $(shell cd ${srcdir} ; echo *.c )

EXTRA_DIST = \
    ${TEST_CASES} \
    ${addprefix saved/, ${TEST_CASES:.sh=.T.err}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.T.out}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.XP.err}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.XP.out}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.JP.err}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.JP.out}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.HP.err}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.HP.out}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.X.err}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.X.out}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.J.err}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.J.out}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.H.err}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.H.out}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.HIPx.err}} \
    ${addprefix saved/, ${TEST_CASES:.sh=.HIPx.out}}

S2O = | ${SED} '1,/@@/d'

all:

#TEST_TRACE = set -x ; 

XO=../../xo/xo

TEST_ONE = \
      ${CHECKER} sh ${srcdir}/$$base.sh "${XO} --libxo:W$$fmt" ${TEST_OPTS} \
      > out/$$base.$$fmt.out 2> out/$$base.$$fmt.err ; \
 ${DIFF} -Nu ${srcdir}/saved/$$base.$$fmt.out out/$$base.$$fmt.out ${S2O} ; \
 ${DIFF} -Nu ${srcdir}/saved/$$base.$$fmt.err out/$$base.$$fmt.err ${S2O}

TEST_FORMATS = T XP JP HP X J H HIPx

test tests: ${bin_PROGRAMS}
	@${MKDIR} -p out
	-@ ${TEST_TRACE} (for test in ${TEST_CASES} ; do \
	    base=`${BASENAME} $$test .sh` ; \
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
	    base=`${BASENAME} $$test .sh` ; \
            (for fmt in ${TEST_FORMATS}; do \
	        echo "... $$test ... $$fmt ..."; \
	        ${CP} out/$$base.$$fmt.out ${srcdir}/saved/$$base.$$fmt.out ; \
	        ${CP} out/$$base.$$fmt.err ${srcdir}/saved/$$base.$$fmt.err ; \
	    done) \
	done)

CLEANFILES = 
CLEANDIRS = out

clean-local:
	rm -rf ${CLEANDIRS}
