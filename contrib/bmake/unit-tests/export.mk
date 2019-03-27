# $Id: export.mk,v 1.1.1.1 2014/08/30 18:57:18 sjg Exp $

UT_TEST=export
UT_FOO=foo${BAR}
UT_FU=fubar
UT_ZOO=hoopie
UT_NO=all
# belive it or not, we expect this one to come out with $UT_FU unexpanded.
UT_DOLLAR= This is $$UT_FU

.export UT_FU UT_FOO
.export UT_DOLLAR
# this one will be ignored
.export .MAKE.PID

BAR=bar is ${UT_FU}

.MAKE.EXPORTED+= UT_ZOO UT_TEST

all:
	@env | grep '^UT_' | sort

