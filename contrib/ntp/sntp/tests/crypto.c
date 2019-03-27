#include "config.h"
#include "unity.h"
#include "ntp_types.h"

#include "sntptest.h"
#include "crypto.h"

#define CMAC "AES128CMAC"

#define MD5_LENGTH 16
#define SHA1_LENGTH 20
#define CMAC_LENGTH 16


void test_MakeMd5Mac(void);
void test_MakeSHA1Mac(void);
void test_MakeCMac(void);
void test_VerifyCorrectMD5(void);
void test_VerifySHA1(void);
void test_VerifyCMAC(void);
void test_VerifyFailure(void);
void test_PacketSizeNotMultipleOfFourBytes(void);

void VerifyLocalCMAC(struct key *cmac);
void VerifyOpenSSLCMAC(struct key *cmac);


void
test_MakeMd5Mac(void)
{
	const char* PKT_DATA = "abcdefgh0123";
	const int PKT_LEN = strlen(PKT_DATA);
	const char* EXPECTED_DIGEST =
		"\x52\x6c\xb8\x38\xaf\x06\x5a\xfb\x6c\x98\xbb\xc0\x9b\x0a\x7a\x1b";
	char actual[MD5_LENGTH];

	struct key md5;
	md5.next = NULL;
	md5.key_id = 10;
	md5.key_len = 6;
	memcpy(&md5.key_seq, "md5seq", md5.key_len);
	strlcpy(md5.typen, "MD5", sizeof(md5.typen));
	md5.typei = keytype_from_text(md5.typen, NULL);
	
	TEST_ASSERT_EQUAL(MD5_LENGTH,
			  make_mac(PKT_DATA, PKT_LEN, MD5_LENGTH, &md5, actual));

	TEST_ASSERT_TRUE(memcmp(EXPECTED_DIGEST, actual, MD5_LENGTH) == 0);
}


void
test_MakeSHA1Mac(void)
{
#ifdef OPENSSL

	const char* PKT_DATA = "abcdefgh0123";
	const int PKT_LEN = strlen(PKT_DATA);
	const char* EXPECTED_DIGEST =
		"\x17\xaa\x82\x97\xc7\x17\x13\x6a\x9b\xa9"
		"\x63\x85\xb4\xce\xbe\x94\xa0\x97\x16\x1d";
	char actual[SHA1_LENGTH];

	struct key sha1;
	sha1.next = NULL;
	sha1.key_id = 20;
	sha1.key_len = 7;
	memcpy(&sha1.key_seq, "sha1seq", sha1.key_len);
	strlcpy(sha1.typen, "SHA1", sizeof(sha1.typen));
	sha1.typei = keytype_from_text(sha1.typen, NULL);

	TEST_ASSERT_EQUAL(SHA1_LENGTH,
			  make_mac(PKT_DATA, PKT_LEN, SHA1_LENGTH, &sha1, actual));

	TEST_ASSERT_EQUAL_MEMORY(EXPECTED_DIGEST, actual, SHA1_LENGTH);
	
#else
	
	TEST_IGNORE_MESSAGE("OpenSSL not found, skipping...");
	
#endif	/* OPENSSL */
}


void
test_MakeCMac(void)
{
#if defined(OPENSSL) && defined(ENABLE_CMAC)

	const char* PKT_DATA = "abcdefgh0123";
	const int PKT_LEN = strlen(PKT_DATA);
	const char* EXPECTED_DIGEST =
		"\xdd\x35\xd5\xf5\x14\x23\xd9\xd6"
		"\x38\x5d\x29\x80\xfe\x51\xb9\x6b";
	char actual[CMAC_LENGTH];

	struct key cmac;
	cmac.next = NULL;
	cmac.key_id = 30;
	cmac.key_len = CMAC_LENGTH;
	memcpy(&cmac.key_seq, "aes-128-cmac-seq", cmac.key_len);
	memcpy(&cmac.typen, CMAC, strlen(CMAC) + 1);

	TEST_ASSERT_EQUAL(CMAC_LENGTH,
		    make_mac(PKT_DATA, PKT_LEN, CMAC_LENGTH, &cmac, actual));

	TEST_ASSERT_EQUAL_MEMORY(EXPECTED_DIGEST, actual, CMAC_LENGTH);
	
#else
	
	TEST_IGNORE_MESSAGE("OpenSSL not found, skipping...");
	
#endif	/* OPENSSL */
}


void
test_VerifyCorrectMD5(void)
{
	const char* PKT_DATA =
	    "sometestdata"			/* Data */
	    "\0\0\0\0"				/* Key-ID (unused) */
	    "\xc7\x58\x99\xdd\x99\x32\x0f\x71"	/* MAC */
	    "\x2b\x7b\xfe\x4f\xa2\x32\xcf\xac";
	const int PKT_LEN = 12;

	struct key md5;
	md5.next = NULL;
	md5.key_id = 0;
	md5.key_len = 6;
	memcpy(&md5.key_seq, "md5key", md5.key_len);
	strlcpy(md5.typen, "MD5", sizeof(md5.typen));
	md5.typei = keytype_from_text(md5.typen, NULL);

	TEST_ASSERT_TRUE(auth_md5(PKT_DATA, PKT_LEN, MD5_LENGTH, &md5));
}


void
test_VerifySHA1(void)
{
#ifdef OPENSSL

	const char* PKT_DATA =
	    "sometestdata"				/* Data */
	    "\0\0\0\0"					/* Key-ID (unused) */
	    "\xad\x07\xde\x36\x39\xa6\x77\xfa\x5b\xce"	/* MAC */
	    "\x2d\x8a\x7d\x06\x96\xe6\x0c\xbc\xed\xe1";
	const int PKT_LEN = 12;

	struct key sha1;
	sha1.next = NULL;
	sha1.key_id = 0;
	sha1.key_len = 7;
	memcpy(&sha1.key_seq, "sha1key", sha1.key_len);
	strlcpy(sha1.typen, "SHA1", sizeof(sha1.typen));	
	sha1.typei = keytype_from_text(sha1.typen, NULL);

	TEST_ASSERT_TRUE(auth_md5(PKT_DATA, PKT_LEN, SHA1_LENGTH, &sha1));
	
#else
	
	TEST_IGNORE_MESSAGE("OpenSSL not found, skipping...");
	
#endif	/* OPENSSL */
}


void
test_VerifyCMAC(void)
{
	const char* PKT_DATA =
	    "sometestdata"				/* Data */
	    "\0\0\0\0"					/* Key-ID (unused) */
	    "\x4e\x0c\xf0\xe2\xc7\x8e\xbb\xbf"		/* MAC */
	    "\x79\xfc\x87\xc7\x8b\xb7\x4a\x0b";
	const int PKT_LEN = 12;
	struct key cmac;

	cmac.next = NULL;
	cmac.key_id = 0;
	cmac.key_len = CMAC_LENGTH;
	memcpy(&cmac.key_seq, "aes-128-cmac-key", cmac.key_len);
	memcpy(&cmac.typen, CMAC, strlen(CMAC) + 1);

	VerifyOpenSSLCMAC(&cmac);
	VerifyLocalCMAC(&cmac);
}


void
VerifyOpenSSLCMAC(struct key *cmac)
{
#if defined(OPENSSL) && defined(ENABLE_CMAC)

	/* XXX: HMS: auth_md5 must be renamed/incorrect. */
	// TEST_ASSERT_TRUE(auth_md5(PKT_DATA, PKT_LEN, CMAC_LENGTH, cmac));
	TEST_IGNORE_MESSAGE("VerifyOpenSSLCMAC needs to be implemented, skipping...");

#else
	
	TEST_IGNORE_MESSAGE("OpenSSL not found, skipping...");
	
#endif	/* OPENSSL */
	return;
}


void
VerifyLocalCMAC(struct key *cmac)
{

	/* XXX: HMS: auth_md5 must be renamed/incorrect. */
	// TEST_ASSERT_TRUE(auth_md5(PKT_DATA, PKT_LEN, CMAC_LENGTH, cmac));

	TEST_IGNORE_MESSAGE("Hook in the local AES-128-CMAC check!");

	return;
}


void
test_VerifyFailure(void)
{
	/* We use a copy of the MD5 verification code, but modify the
	 * last bit to make sure verification fails.
	 */
	const char* PKT_DATA =
	    "sometestdata"			/* Data */
	    "\0\0\0\0"				/* Key-ID (unused) */
	    "\xc7\x58\x99\xdd\x99\x32\x0f\x71"	/* MAC */
	    "\x2b\x7b\xfe\x4f\xa2\x32\xcf\x00"; /* Last byte is wrong! */
	const int PKT_LEN = 12;

	struct key md5;
	md5.next = NULL;
	md5.key_id = 0;
	md5.key_len = 6;
	memcpy(&md5.key_seq, "md5key", md5.key_len);
	strlcpy(md5.typen, "MD5", sizeof(md5.typen));
	md5.typei = keytype_from_text(md5.typen, NULL);

	TEST_ASSERT_FALSE(auth_md5(PKT_DATA, PKT_LEN, MD5_LENGTH, &md5));
}


void
test_PacketSizeNotMultipleOfFourBytes(void)
{
	const char* PKT_DATA = "123456";
	const int PKT_LEN = 6;
	char actual[MD5_LENGTH];

	struct key md5;
	md5.next = NULL;
	md5.key_id = 10;
	md5.key_len = 6;
	memcpy(&md5.key_seq, "md5seq", md5.key_len);
	strlcpy(md5.typen, "MD5", sizeof(md5.typen));
	md5.typei = keytype_from_text(md5.typen, NULL);

	TEST_ASSERT_EQUAL(0, make_mac(PKT_DATA, PKT_LEN, MD5_LENGTH, &md5, actual));
}
