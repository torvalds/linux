/* 	$OpenBSD: tests.c,v 1.1 2014/06/24 01:14:18 djm Exp $ */
/*
 * Regress test for sshbuf.h buffer API
 *
 * Placed in the public domain
 */

#include "includes.h"

#include <openssl/evp.h>

#include "../test_helper/test_helper.h"

void sshkey_tests(void);
void sshkey_file_tests(void);
void sshkey_fuzz_tests(void);

void
tests(void)
{
	OpenSSL_add_all_algorithms();
	ERR_load_CRYPTO_strings();

	sshkey_tests();
	sshkey_file_tests();
	sshkey_fuzz_tests();
}
