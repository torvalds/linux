# $NetBSD: modorder.mk,v 1.1 2014/08/21 13:44:51 apb Exp $

LIST=		one two three four five six seven eight nine ten
LISTX=		${LIST:Ox}
LISTSX:=	${LIST:Ox}
TEST_RESULT= && echo Ok || echo Failed

# unit-tests have to produce the same results on each run
# so we cannot actually include :Ox output.
all:
	@echo "LIST      = ${LIST}"
	@echo "LIST:O    = ${LIST:O}"
	# Note that 1 in every 10! trials two independently generated
	# randomized orderings will be the same.  The test framework doesn't
	# support checking probabilistic output, so we accept that the test
	# will incorrectly fail with probability 2.8E-7.
	@echo "LIST:Ox   = `test '${LIST:Ox}' != '${LIST:Ox}' ${TEST_RESULT}`"
	@echo "LIST:O:Ox = `test '${LIST:O:Ox}' != '${LIST:O:Ox}' ${TEST_RESULT}`"
	@echo "LISTX     = `test '${LISTX}' != '${LISTX}' ${TEST_RESULT}`"
	@echo "LISTSX    = `test '${LISTSX}' = '${LISTSX}' ${TEST_RESULT}`"
	@echo "BADMOD 1  = ${LIST:OX}"
	@echo "BADMOD 2  = ${LIST:OxXX}"
