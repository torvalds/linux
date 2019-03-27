/* 	$OpenBSD: tests.c,v 1.1 2014/04/30 05:32:00 djm Exp $ */
/*
 * Regress test for sshbuf.h buffer API
 *
 * Placed in the public domain
 */

#include "../test_helper/test_helper.h"

void sshbuf_tests(void);
void sshbuf_getput_basic_tests(void);
void sshbuf_getput_crypto_tests(void);
void sshbuf_misc_tests(void);
void sshbuf_fuzz_tests(void);
void sshbuf_getput_fuzz_tests(void);
void sshbuf_fixed(void);

void
tests(void)
{
	sshbuf_tests();
	sshbuf_getput_basic_tests();
	sshbuf_getput_crypto_tests();
	sshbuf_misc_tests();
	sshbuf_fuzz_tests();
	sshbuf_getput_fuzz_tests();
	sshbuf_fixed();
}
