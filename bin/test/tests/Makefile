# $FreeBSD$

.include <bsd.own.mk>

TAP_TESTS_SH=	legacy_test
# Some tests in here are silently not run when the tests are executed as
# root.  Explicitly tell Kyua to drop privileges.
#
# TODO(jmmv): Kyua needs to do this by default, not only when explicitly
# requested.  See https://code.google.com/p/kyua/issues/detail?id=6
TEST_METADATA.legacy_test+= required_user="unprivileged"

.include <bsd.test.mk>
