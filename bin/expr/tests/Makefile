# $FreeBSD$

NETBSD_ATF_TESTS_SH=	expr_test

ATF_TESTS_SH_SED_expr_test+=	-e 's/eval expr/eval expr --/g'
ATF_TESTS_SH_SED_expr_test+=	-e 's/"expr: integer overflow or underflow occurred for operation.*"/"expr: overflow"/g'

.include <netbsd-tests.test.mk>

.include <bsd.test.mk>
