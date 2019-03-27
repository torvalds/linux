/*
 * Copyright (C) 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

/* ! \file */

#include <config.h>

#include <atf-c.h>

#include <stdio.h>
#include <string.h>

#include <isc/hmacmd5.h>
#include <isc/hmacsha.h>
#include <isc/md5.h>
#include <isc/sha1.h>
#include <isc/util.h>
#include <isc/string.h>

/*
 * Test data from RFC6234
 */

unsigned char digest[ISC_SHA512_DIGESTLENGTH];
unsigned char buffer[1024];
const char *s;
char str[ISC_SHA512_DIGESTLENGTH];
unsigned char key[20];
int i = 0;

isc_result_t
tohexstr(unsigned char *d, unsigned int len, char *out);
/*
 * Precondition: a hexadecimal number in *d, the length of that number in len,
 *   and a pointer to a character array to put the output (*out).
 * Postcondition: A String representation of the given hexadecimal number is
 *   placed into the array *out
 *
 * 'out' MUST point to an array of at least len / 2 + 1
 *
 * Return values: ISC_R_SUCCESS if the operation is sucessful
 */

isc_result_t
tohexstr(unsigned char *d, unsigned int len, char *out) {

	out[0]='\0';
	char c_ret[] = "AA";
	unsigned int i;
	strcat(out, "0x");
	for (i = 0; i < len; i++) {
		sprintf(c_ret, "%02X", d[i]);
		strcat(out, c_ret);
	}
	strcat(out, "\0");
	return (ISC_R_SUCCESS);
}


#define TEST_INPUT(x) (x), sizeof(x)-1

typedef struct hash_testcase {
	const char *input;
	size_t input_len;
	const char *result;
	int repeats;
} hash_testcase_t;

typedef struct hash_test_key {
	const char *key;
	const int len;
} hash_test_key_t;

/* non-hmac tests */

ATF_TC(isc_sha1);
ATF_TC_HEAD(isc_sha1, tc) {
	atf_tc_set_md_var(tc, "descr", "sha1 examples from RFC4634");
}
ATF_TC_BODY(isc_sha1, tc) {
	isc_sha1_t sha1;

	UNUSED(tc);

	/*
	 * These are the various test vectors.  All of these are passed
	 * through the hash function and the results are compared to the
	 * result specified here.
	 */
	hash_testcase_t testcases[] = {
		/* Test 1 */
		{
			TEST_INPUT("abc"),
			"0xA9993E364706816ABA3E25717850C26C9CD0D89D",
			1
		},
		/* Test 2 */
		{
			TEST_INPUT("abcdbcdecdefdefgefghfghighijhijkijk"
				   "ljklmklmnlmnomnopnopq"),
			"0x84983E441C3BD26EBAAE4AA1F95129E5E54670F1",
			1
		},
		/* Test 3 */
		{
			TEST_INPUT("a") /* times 1000000 */,
			"0x34AA973CD4C4DAA4F61EEB2BDBAD27316534016F",
			1000000
		},
		/* Test 4 -- exact multiple of 512 bits */
		{
			TEST_INPUT("01234567012345670123456701234567"),
			"0xDEA356A2CDDD90C7A7ECEDC5EBB563934F460452",
			20 /* 20 times */
		},
#if 0
		/* Test 5 -- optional feature, not implemented */
		{
			TEST_INPUT(""),
			/* "extrabits": 0x98 , "numberextrabits": 5 */
			"0x29826B003B906E660EFF4027CE98AF3531AC75BA",
			1
		},
#endif
		/* Test 6 */
		{
			TEST_INPUT("\x5e"),
			"0x5E6F80A34A9798CAFC6A5DB96CC57BA4C4DB59C2",
			1
		},
#if 0
		/* Test 7 -- optional feature, not implemented */
		{
			TEST_INPUT("\x49\xb2\xae\xc2\x59\x4b\xbe\x3a"
				   "\x3b\x11\x75\x42\xd9\x4a\xc8"),
			/* "extrabits": 0x80, "numberextrabits": 3 */
		  "0x6239781E03729919C01955B3FFA8ACB60B988340", 1 },
#endif
		/* Test 8 */
		{
			TEST_INPUT("\x9a\x7d\xfd\xf1\xec\xea\xd0\x6e\xd6\x46"
				   "\xaa\x55\xfe\x75\x71\x46"),
			"0x82ABFF6605DBE1C17DEF12A394FA22A82B544A35",
			1
		},
#if 0
		/* Test 9 -- optional feature, not implemented */
		{
			TEST_INPUT("\x65\xf9\x32\x99\x5b\xa4\xce\x2c\xb1\xb4"
				   "\xa2\xe7\x1a\xe7\x02\x20\xaa\xce\xc8\x96"
				   "\x2d\xd4\x49\x9c\xbd\x7c\x88\x7a\x94\xea"
				   "\xaa\x10\x1e\xa5\xaa\xbc\x52\x9b\x4e\x7e"
				   "\x43\x66\x5a\x5a\xf2\xcd\x03\xfe\x67\x8e"
				   "\xa6\xa5\x00\x5b\xba\x3b\x08\x22\x04\xc2"
				   "\x8b\x91\x09\xf4\x69\xda\xc9\x2a\xaa\xb3"
				   "\xaa\x7c\x11\xa1\xb3\x2a"),
			/* "extrabits": 0xE0 , "numberextrabits": 3 */
			"0x8C5B2A5DDAE5A97FC7F9D85661C672ADBF7933D4",
			1
		},
#endif
		/* Test 10 */
		{
			TEST_INPUT("\xf7\x8f\x92\x14\x1b\xcd\x17\x0a\xe8\x9b"
				   "\x4f\xba\x15\xa1\xd5\x9f\x3f\xd8\x4d\x22"
				   "\x3c\x92\x51\xbd\xac\xbb\xae\x61\xd0\x5e"
				   "\xd1\x15\xa0\x6a\x7c\xe1\x17\xb7\xbe\xea"
				   "\xd2\x44\x21\xde\xd9\xc3\x25\x92\xbd\x57"
				   "\xed\xea\xe3\x9c\x39\xfa\x1f\xe8\x94\x6a"
				   "\x84\xd0\xcf\x1f\x7b\xee\xad\x17\x13\xe2"
				   "\xe0\x95\x98\x97\x34\x7f\x67\xc8\x0b\x04"
				   "\x00\xc2\x09\x81\x5d\x6b\x10\xa6\x83\x83"
				   "\x6f\xd5\x56\x2a\x56\xca\xb1\xa2\x8e\x81"
				   "\xb6\x57\x66\x54\x63\x1c\xf1\x65\x66\xb8"
				   "\x6e\x3b\x33\xa1\x08\xb0\x53\x07\xc0\x0a"
				   "\xff\x14\xa7\x68\xed\x73\x50\x60\x6a\x0f"
				   "\x85\xe6\xa9\x1d\x39\x6f\x5b\x5c\xbe\x57"
				   "\x7f\x9b\x38\x80\x7c\x7d\x52\x3d\x6d\x79"
				   "\x2f\x6e\xbc\x24\xa4\xec\xf2\xb3\xa4\x27"
				   "\xcd\xbb\xfb"),
			"0xCB0082C8F197D260991BA6A460E76E202BAD27B3",
			1
		},
		{ NULL, 0, NULL, 1 }
	};

	hash_testcase_t *testcase = testcases;

	while (testcase->input != NULL && testcase->result != NULL) {
		isc_sha1_init(&sha1);
		for(i = 0; i < testcase->repeats; i++) {
			isc_sha1_update(&sha1,
					(const isc_uint8_t *) testcase->input,
					testcase->input_len);
		}
		isc_sha1_final(&sha1, digest);
		tohexstr(digest, ISC_SHA1_DIGESTLENGTH, str);
		ATF_CHECK_STREQ(str, testcase->result);

		testcase++;
	}
}


ATF_TC(isc_sha224);
ATF_TC_HEAD(isc_sha224, tc) {
	atf_tc_set_md_var(tc, "descr", "sha224 examples from RFC4634");
}
ATF_TC_BODY(isc_sha224, tc) {
	isc_sha224_t sha224;

	UNUSED(tc);

	/*
	 * These are the various test vectors.  All of these are passed
	 * through the hash function and the results are compared to the
	 * result specified here.
	 */
	hash_testcase_t testcases[] = {
		/* Test 1 */
		{
			TEST_INPUT("abc"),
			"0x23097D223405D8228642A477BDA255B32AADBCE4BDA0B3F7"
				"E36C9DA7",
			1
		},
		/* Test 2 */
		{
			TEST_INPUT("abcdbcdecdefdefgefghfghighijhijkijklj"
				   "klmklmnlmnomnopnopq"),
			"0x75388B16512776CC5DBA5DA1FD890150B0C6455CB4F58B"
				"1952522525",
			1
		},
		/* Test 3 */
		{
			TEST_INPUT("a"),
			"0x20794655980C91D8BBB4C1EA97618A4BF03F42581948B2"
				"EE4EE7AD67",
			1000000
		},
		/* Test 4 */
		{
			TEST_INPUT("01234567012345670123456701234567"),
			"0x567F69F168CD7844E65259CE658FE7AADFA25216E68ECA"
				"0EB7AB8262",
			20
		},
#if 0
		/* Test 5 -- unimplemented optional functionality */
		{
			TEST_INPUT(""),
			"0xXXX",
			1
		},
#endif
		/* Test 6 */
		{
			TEST_INPUT("\x07"),
			"0x00ECD5F138422B8AD74C9799FD826C531BAD2FCABC7450"
				"BEE2AA8C2A",
			1
		},
#if 0
		/* Test 7 -- unimplemented optional functionality */
		{
			TEST_INPUT(""),
			"0xXXX",
			1
		},
#endif
		/* Test 8 */
		{
			TEST_INPUT("\x18\x80\x40\x05\xdd\x4f\xbd\x15\x56\x29"
				   "\x9d\x6f\x9d\x93\xdf\x62"),
			"0xDF90D78AA78821C99B40BA4C966921ACCD8FFB1E98AC38"
				"8E56191DB1",
			1
		},
#if 0
		/* Test 9 */
		{
			TEST_INPUT(""),
			"0xXXX",
			1
		},
#endif
		/* Test 10 */
		{
			TEST_INPUT("\x55\xb2\x10\x07\x9c\x61\xb5\x3a\xdd\x52"
				   "\x06\x22\xd1\xac\x97\xd5\xcd\xbe\x8c\xb3"
				   "\x3a\xa0\xae\x34\x45\x17\xbe\xe4\xd7\xba"
				   "\x09\xab\xc8\x53\x3c\x52\x50\x88\x7a\x43"
				   "\xbe\xbb\xac\x90\x6c\x2e\x18\x37\xf2\x6b"
				   "\x36\xa5\x9a\xe3\xbe\x78\x14\xd5\x06\x89"
				   "\x6b\x71\x8b\x2a\x38\x3e\xcd\xac\x16\xb9"
				   "\x61\x25\x55\x3f\x41\x6f\xf3\x2c\x66\x74"
				   "\xc7\x45\x99\xa9\x00\x53\x86\xd9\xce\x11"
				   "\x12\x24\x5f\x48\xee\x47\x0d\x39\x6c\x1e"
				   "\xd6\x3b\x92\x67\x0c\xa5\x6e\xc8\x4d\xee"
				   "\xa8\x14\xb6\x13\x5e\xca\x54\x39\x2b\xde"
				   "\xdb\x94\x89\xbc\x9b\x87\x5a\x8b\xaf\x0d"
				   "\xc1\xae\x78\x57\x36\x91\x4a\xb7\xda\xa2"
				   "\x64\xbc\x07\x9d\x26\x9f\x2c\x0d\x7e\xdd"
				   "\xd8\x10\xa4\x26\x14\x5a\x07\x76\xf6\x7c"
				   "\x87\x82\x73"),
			"0x0B31894EC8937AD9B91BDFBCBA294D9ADEFAA18E09305E"
				"9F20D5C3A4",
			1
		},
		{ NULL, 0, NULL, 1 }
	};

	hash_testcase_t *testcase = testcases;

	while (testcase->input != NULL && testcase->result != NULL) {
		isc_sha224_init(&sha224);
		for(i = 0; i < testcase->repeats; i++) {
			isc_sha224_update(&sha224,
					  (const isc_uint8_t *) testcase->input,
					  testcase->input_len);
		}
		isc_sha224_final(digest, &sha224);
		/*
		*API inconsistency BUG HERE
		* in order to be consistant with the other isc_hash_final
		* functions the call should be
		* isc_sha224_final(&sha224, digest);
		 */
		tohexstr(digest, ISC_SHA224_DIGESTLENGTH, str);
		ATF_CHECK_STREQ(str, testcase->result);

		testcase++;
	}

}

ATF_TC(isc_sha256);
ATF_TC_HEAD(isc_sha256, tc) {
	atf_tc_set_md_var(tc, "descr", "sha224 examples from RFC4634");
}
ATF_TC_BODY(isc_sha256, tc) {
	isc_sha256_t sha256;

	UNUSED(tc);

	/*
	 * These are the various test vectors.  All of these are passed
	 * through the hash function and the results are compared to the
	 * result specified here.
	 */
	hash_testcase_t testcases[] = {
		/* Test 1 */
		{
			TEST_INPUT("abc"),
			"0xBA7816BF8F01CFEA414140DE5DAE2223B00361A396177A"
				"9CB410FF61F20015AD",
			1
		},
		/* Test 2 */
		{
			TEST_INPUT("abcdbcdecdefdefgefghfghighijhijkijkljk"
				   "lmklmnlmnomnopnopq"),
			"0x248D6A61D20638B8E5C026930C3E6039A33CE45964FF21"
				"67F6ECEDD419DB06C1",
			1
		},
		/* Test 3 */
		{
			TEST_INPUT("a"),
			"0xCDC76E5C9914FB9281A1C7E284D73E67F1809A48A49720"
				"0E046D39CCC7112CD0",
			1000000 },
		/* Test 4 */
		{
			TEST_INPUT("01234567012345670123456701234567"),
			"0x594847328451BDFA85056225462CC1D867D877FB388DF0"
				"CE35F25AB5562BFBB5",
			20
		},
#if 0
		/* Test 5 -- unimplemented optional functionality */
		{
			TEST_INPUT(""),
			"0xXXX",
			1
		},
#endif
		/* Test 6 */
		{
			TEST_INPUT("\x19"),
			"0x68AA2E2EE5DFF96E3355E6C7EE373E3D6A4E17F75F9518"
				"D843709C0C9BC3E3D4",
			1
		},
#if 0
		/* Test 7 -- unimplemented optional functionality */
		{
			TEST_INPUT(""),
			"0xXXX",
			1
		},
#endif
		/* Test 8 */
		{
			TEST_INPUT("\xe3\xd7\x25\x70\xdc\xdd\x78\x7c\xe3"
				   "\x88\x7a\xb2\xcd\x68\x46\x52"),
			"0x175EE69B02BA9B58E2B0A5FD13819CEA573F3940A94F82"
				"5128CF4209BEABB4E8",
			1
		},
#if 0
		/* Test 9 -- unimplemented optional functionality */
		{
			TEST_INPUT(""),
			"0xXXX",
			1
		},
#endif
		/* Test 10 */
		{
			TEST_INPUT("\x83\x26\x75\x4e\x22\x77\x37\x2f\x4f\xc1"
				   "\x2b\x20\x52\x7a\xfe\xf0\x4d\x8a\x05\x69"
				   "\x71\xb1\x1a\xd5\x71\x23\xa7\xc1\x37\x76"
				   "\x00\x00\xd7\xbe\xf6\xf3\xc1\xf7\xa9\x08"
				   "\x3a\xa3\x9d\x81\x0d\xb3\x10\x77\x7d\xab"
				   "\x8b\x1e\x7f\x02\xb8\x4a\x26\xc7\x73\x32"
				   "\x5f\x8b\x23\x74\xde\x7a\x4b\x5a\x58\xcb"
				   "\x5c\x5c\xf3\x5b\xce\xe6\xfb\x94\x6e\x5b"
				   "\xd6\x94\xfa\x59\x3a\x8b\xeb\x3f\x9d\x65"
				   "\x92\xec\xed\xaa\x66\xca\x82\xa2\x9d\x0c"
				   "\x51\xbc\xf9\x33\x62\x30\xe5\xd7\x84\xe4"
				   "\xc0\xa4\x3f\x8d\x79\xa3\x0a\x16\x5c\xba"
				   "\xbe\x45\x2b\x77\x4b\x9c\x71\x09\xa9\x7d"
				   "\x13\x8f\x12\x92\x28\x96\x6f\x6c\x0a\xdc"
				   "\x10\x6a\xad\x5a\x9f\xdd\x30\x82\x57\x69"
				   "\xb2\xc6\x71\xaf\x67\x59\xdf\x28\xeb\x39"
				   "\x3d\x54\xd6"),
			"0x97DBCA7DF46D62C8A422C941DD7E835B8AD3361763F7E9"
				"B2D95F4F0DA6E1CCBC",
			1
		},
		{ NULL, 0, NULL, 1 }
	};

	hash_testcase_t *testcase = testcases;

	while (testcase->input != NULL && testcase->result != NULL) {
		isc_sha256_init(&sha256);
		for(i = 0; i < testcase->repeats; i++) {
			isc_sha256_update(&sha256,
					  (const isc_uint8_t *) testcase->input,
					  testcase->input_len);
		}
		isc_sha256_final(digest, &sha256);
		/*
		*API inconsistency BUG HERE
		* in order to be consistant with the other isc_hash_final
		* functions the call should be
		* isc_sha224_final(&sha224, digest);
		 */
		tohexstr(digest, ISC_SHA256_DIGESTLENGTH, str);
		ATF_CHECK_STREQ(str, testcase->result);

		testcase++;
	}

}

ATF_TC(isc_sha384);
ATF_TC_HEAD(isc_sha384, tc) {
	atf_tc_set_md_var(tc, "descr", "sha224 examples from RFC4634");
}
ATF_TC_BODY(isc_sha384, tc) {
	isc_sha384_t sha384;

	UNUSED(tc);

	/*
	 * These are the various test vectors.  All of these are passed
	 * through the hash function and the results are compared to the
	 * result specified here.
	 */
	hash_testcase_t testcases[] = {
		/* Test 1 */
		{
			TEST_INPUT("abc"),
			"0xCB00753F45A35E8BB5A03D699AC65007272C32AB0EDED1"
				"631A8B605A43FF5BED8086072BA1E7CC2358BAEC"
				"A134C825A7",
			1
		},
		/* Test 2 */
		{
			TEST_INPUT("abcdefghbcdefghicdefghijdefghijkefghijkl"
				   "fghijklmghijklmnhijklmnoijklmnopjklmnopq"
				   "klmnopqrlmnopqrsmnopqrstnopqrstu"),
			"0x09330C33F71147E83D192FC782CD1B4753111B173B3B05"
				"D22FA08086E3B0F712FCC7C71A557E2DB966C3E9"
				"FA91746039",
			1
		},
		/* Test 3 */
		{
			TEST_INPUT("a"),
			"0x9D0E1809716474CB086E834E310A4A1CED149E9C00F248"
				"527972CEC5704C2A5B07B8B3DC38ECC4EBAE97DD"
				"D87F3D8985",
			1000000
		},
		/* Test 4 */
		{
			TEST_INPUT("01234567012345670123456701234567"),
			"0x2FC64A4F500DDB6828F6A3430B8DD72A368EB7F3A8322A"
				"70BC84275B9C0B3AB00D27A5CC3C2D224AA6B61A"
				"0D79FB4596",
			20
		},
#if 0
		/* Test 5 -- unimplemented optional functionality */
		{
			TEST_INPUT(""),
			"0xXXX",
			1
		},
#endif
		/* Test 6 */
		{ TEST_INPUT("\xb9"),
			"0xBC8089A19007C0B14195F4ECC74094FEC64F01F9092928"
				"2C2FB392881578208AD466828B1C6C283D2722CF"
				"0AD1AB6938",
			1
		},
#if 0
		/* Test 7 -- unimplemented optional functionality */
		{
			TEST_INPUT(""),
			"0xXXX",
			1
		},
#endif
		/* Test 8 */
		{
			TEST_INPUT("\xa4\x1c\x49\x77\x79\xc0\x37\x5f\xf1"
				   "\x0a\x7f\x4e\x08\x59\x17\x39"),
			"0xC9A68443A005812256B8EC76B00516F0DBB74FAB26D665"
				"913F194B6FFB0E91EA9967566B58109CBC675CC2"
				"08E4C823F7",
			1
		},
#if 0
		/* Test 9 -- unimplemented optional functionality */
		{
			TEST_INPUT(""),
			"0xXXX",
			1
		},
#endif
		/* Test 10 */
		{
			TEST_INPUT("\x39\x96\x69\xe2\x8f\x6b\x9c\x6d\xbc\xbb"
				   "\x69\x12\xec\x10\xff\xcf\x74\x79\x03\x49"
				   "\xb7\xdc\x8f\xbe\x4a\x8e\x7b\x3b\x56\x21"
				   "\xdb\x0f\x3e\x7d\xc8\x7f\x82\x32\x64\xbb"
				   "\xe4\x0d\x18\x11\xc9\xea\x20\x61\xe1\xc8"
				   "\x4a\xd1\x0a\x23\xfa\xc1\x72\x7e\x72\x02"
				   "\xfc\x3f\x50\x42\xe6\xbf\x58\xcb\xa8\xa2"
				   "\x74\x6e\x1f\x64\xf9\xb9\xea\x35\x2c\x71"
				   "\x15\x07\x05\x3c\xf4\xe5\x33\x9d\x52\x86"
				   "\x5f\x25\xcc\x22\xb5\xe8\x77\x84\xa1\x2f"
				   "\xc9\x61\xd6\x6c\xb6\xe8\x95\x73\x19\x9a"
				   "\x2c\xe6\x56\x5c\xbd\xf1\x3d\xca\x40\x38"
				   "\x32\xcf\xcb\x0e\x8b\x72\x11\xe8\x3a\xf3"
				   "\x2a\x11\xac\x17\x92\x9f\xf1\xc0\x73\xa5"
				   "\x1c\xc0\x27\xaa\xed\xef\xf8\x5a\xad\x7c"
				   "\x2b\x7c\x5a\x80\x3e\x24\x04\xd9\x6d\x2a"
				   "\x77\x35\x7b\xda\x1a\x6d\xae\xed\x17\x15"
				   "\x1c\xb9\xbc\x51\x25\xa4\x22\xe9\x41\xde"
				   "\x0c\xa0\xfc\x50\x11\xc2\x3e\xcf\xfe\xfd"
				   "\xd0\x96\x76\x71\x1c\xf3\xdb\x0a\x34\x40"
				   "\x72\x0e\x16\x15\xc1\xf2\x2f\xbc\x3c\x72"
				   "\x1d\xe5\x21\xe1\xb9\x9b\xa1\xbd\x55\x77"
				   "\x40\x86\x42\x14\x7e\xd0\x96"),
			"0x4F440DB1E6EDD2899FA335F09515AA025EE177A79F4B4A"
				"AF38E42B5C4DE660F5DE8FB2A5B2FBD2A3CBFFD2"
				"0CFF1288C0",
			1
		},
		{ NULL, 0, NULL, 1 }
	};

	hash_testcase_t *testcase = testcases;

	while (testcase->input != NULL && testcase->result != NULL) {
		isc_sha384_init(&sha384);
		for(i = 0; i < testcase->repeats; i++) {
			isc_sha384_update(&sha384,
					  (const isc_uint8_t *) testcase->input,
					  testcase->input_len);
		}
		isc_sha384_final(digest, &sha384);
		/*
		*API inconsistency BUG HERE
		* in order to be consistant with the other isc_hash_final
		* functions the call should be
		* isc_sha224_final(&sha224, digest);
		 */
		tohexstr(digest, ISC_SHA384_DIGESTLENGTH, str);
		ATF_CHECK_STREQ(str, testcase->result);

		testcase++;
	}

}

ATF_TC(isc_sha512);
ATF_TC_HEAD(isc_sha512, tc) {
	atf_tc_set_md_var(tc, "descr", "sha224 examples from RFC4634");
}
ATF_TC_BODY(isc_sha512, tc) {
	isc_sha512_t sha512;

	UNUSED(tc);

	/*
	 * These are the various test vectors.  All of these are passed
	 * through the hash function and the results are compared to the
	 * result specified here.
	 */
	hash_testcase_t testcases[] = {
		/* Test 1 */
		{
			TEST_INPUT("abc"),
			"0xDDAF35A193617ABACC417349AE20413112E6FA4E89A97E"
				"A20A9EEEE64B55D39A2192992A274FC1A836BA3C"
				"23A3FEEBBD454D4423643CE80E2A9AC94FA54CA49F",
			1
		},
		/* Test 2 */
		{
			TEST_INPUT("abcdefghbcdefghicdefghijdefghijkefghijkl"
				   "fghijklmghijklmnhijklmnoijklmnopjklmnopq"
				   "klmnopqrlmnopqrsmnopqrstnopqrstu"),
			"0x8E959B75DAE313DA8CF4F72814FC143F8F7779C6EB9F7F"
				"A17299AEADB6889018501D289E4900F7E4331B99"
				"DEC4B5433AC7D329EEB6DD26545E96E55B874BE909",
			1
		},
		/* Test 3 */
		{
			TEST_INPUT("a"),
			"0xE718483D0CE769644E2E42C7BC15B4638E1F98B13B2044"
				"285632A803AFA973EBDE0FF244877EA60A4CB043"
				"2CE577C31BEB009C5C2C49AA2E4EADB217AD8CC09B",
			1000000
		},
		/* Test 4 */
		{
			TEST_INPUT("01234567012345670123456701234567"),
			"0x89D05BA632C699C31231DED4FFC127D5A894DAD412C0E0"
				"24DB872D1ABD2BA8141A0F85072A9BE1E2AA04CF"
				"33C765CB510813A39CD5A84C4ACAA64D3F3FB7BAE9",
			20
		},
#if 0
		/* Test 5 -- unimplemented optional functionality */
		{
			TEST_INPUT(""),
			"0xXXX",
			1
		},
#endif
		/* Test 6 */
		{
			TEST_INPUT("\xD0"),
			"0x9992202938E882E73E20F6B69E68A0A7149090423D93C8"
				"1BAB3F21678D4ACEEEE50E4E8CAFADA4C85A54EA"
				"8306826C4AD6E74CECE9631BFA8A549B4AB3FBBA15",
			1
		},
#if 0
		/* Test 7 -- unimplemented optional functionality */
		{
			TEST_INPUT(""),
			"0xXXX",
			1
		},
#endif
		/* Test 8 */
		{
			TEST_INPUT("\x8d\x4e\x3c\x0e\x38\x89\x19\x14\x91\x81"
				   "\x6e\x9d\x98\xbf\xf0\xa0"),
			"0xCB0B67A4B8712CD73C9AABC0B199E9269B20844AFB75AC"
				"BDD1C153C9828924C3DDEDAAFE669C5FDD0BC66F"
				"630F6773988213EB1B16F517AD0DE4B2F0C95C90F8",
			1
		},
#if 0
		/* Test 9 -- unimplemented optional functionality */
		{
			TEST_INPUT(""),
			"0xXXX",
			1
		},
#endif
		/* Test 10 */
		{
			TEST_INPUT("\xa5\x5f\x20\xc4\x11\xaa\xd1\x32\x80\x7a"
				   "\x50\x2d\x65\x82\x4e\x31\xa2\x30\x54\x32"
				   "\xaa\x3d\x06\xd3\xe2\x82\xa8\xd8\x4e\x0d"
				   "\xe1\xde\x69\x74\xbf\x49\x54\x69\xfc\x7f"
				   "\x33\x8f\x80\x54\xd5\x8c\x26\xc4\x93\x60"
				   "\xc3\xe8\x7a\xf5\x65\x23\xac\xf6\xd8\x9d"
				   "\x03\xe5\x6f\xf2\xf8\x68\x00\x2b\xc3\xe4"
				   "\x31\xed\xc4\x4d\xf2\xf0\x22\x3d\x4b\xb3"
				   "\xb2\x43\x58\x6e\x1a\x7d\x92\x49\x36\x69"
				   "\x4f\xcb\xba\xf8\x8d\x95\x19\xe4\xeb\x50"
				   "\xa6\x44\xf8\xe4\xf9\x5e\xb0\xea\x95\xbc"
				   "\x44\x65\xc8\x82\x1a\xac\xd2\xfe\x15\xab"
				   "\x49\x81\x16\x4b\xbb\x6d\xc3\x2f\x96\x90"
				   "\x87\xa1\x45\xb0\xd9\xcc\x9c\x67\xc2\x2b"
				   "\x76\x32\x99\x41\x9c\xc4\x12\x8b\xe9\xa0"
				   "\x77\xb3\xac\xe6\x34\x06\x4e\x6d\x99\x28"
				   "\x35\x13\xdc\x06\xe7\x51\x5d\x0d\x73\x13"
				   "\x2e\x9a\x0d\xc6\xd3\xb1\xf8\xb2\x46\xf1"
				   "\xa9\x8a\x3f\xc7\x29\x41\xb1\xe3\xbb\x20"
				   "\x98\xe8\xbf\x16\xf2\x68\xd6\x4f\x0b\x0f"
				   "\x47\x07\xfe\x1e\xa1\xa1\x79\x1b\xa2\xf3"
				   "\xc0\xc7\x58\xe5\xf5\x51\x86\x3a\x96\xc9"
				   "\x49\xad\x47\xd7\xfb\x40\xd2"),
		  "0xC665BEFB36DA189D78822D10528CBF3B12B3EEF7260399"
			  "09C1A16A270D48719377966B957A878E72058477"
			  "9A62825C18DA26415E49A7176A894E7510FD1451F5",
		  1
		},
		{ NULL, 0, NULL, 1 }
	};

	hash_testcase_t *testcase = testcases;

	while (testcase->input != NULL && testcase->result != NULL) {
		isc_sha512_init(&sha512);
		for(i = 0; i < testcase->repeats; i++) {
			isc_sha512_update(&sha512,
					  (const isc_uint8_t *) testcase->input,
					  testcase->input_len);
		}
		isc_sha512_final(digest, &sha512);
		/*
		*API inconsistency BUG HERE
		* in order to be consistant with the other isc_hash_final
		* functions the call should be
		* isc_sha224_final(&sha224, digest);
		 */
		tohexstr(digest, ISC_SHA512_DIGESTLENGTH, str);
		ATF_CHECK_STREQ(str, testcase->result);

		testcase++;
	}

}

ATF_TC(isc_md5);
ATF_TC_HEAD(isc_md5, tc) {
	atf_tc_set_md_var(tc, "descr", "md5 example from RFC1321");
}
ATF_TC_BODY(isc_md5, tc) {
	isc_md5_t md5;

	UNUSED(tc);

	/*
	 * These are the various test vectors.  All of these are passed
	 * through the hash function and the results are compared to the
	 * result specified here.
	 */
	hash_testcase_t testcases[] = {
		{
			TEST_INPUT(""),
			"0xD41D8CD98F00B204E9800998ECF8427E",
			1
		},
		{
			TEST_INPUT("a"),
			"0x0CC175B9C0F1B6A831C399E269772661",
			1
		},
		{
			TEST_INPUT("abc"),
			"0x900150983CD24FB0D6963F7D28E17F72",
			1
		},
		{
			TEST_INPUT("message digest"),
			"0xF96B697D7CB7938D525A2F31AAF161D0",
			1
		},
		{
			TEST_INPUT("abcdefghijklmnopqrstuvwxyz"),
			"0xC3FCD3D76192E4007DFB496CCA67E13B",
			1
		},
		{
			TEST_INPUT("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklm"
				   "nopqrstuvwxyz0123456789"),
			"0xD174AB98D277D9F5A5611C2C9F419D9F",
			1
		},
		{
			TEST_INPUT("123456789012345678901234567890123456789"
				   "01234567890123456789012345678901234567890"),
			"0x57EDF4A22BE3C955AC49DA2E2107B67A",
			1
		},
		{ NULL, 0, NULL, 1 }
	};

	hash_testcase_t *testcase = testcases;

	while (testcase->input != NULL && testcase->result != NULL) {
		isc_md5_init(&md5);
		for(i = 0; i < testcase->repeats; i++) {
			isc_md5_update(&md5,
				       (const isc_uint8_t *) testcase->input,
				       testcase->input_len);
		}
		isc_md5_final(&md5, digest);
		tohexstr(digest, ISC_MD5_DIGESTLENGTH, str);
		ATF_CHECK_STREQ(str, testcase->result);

		testcase++;
	}
}

/* HMAC-SHA1 test */
ATF_TC(isc_hmacsha1);
ATF_TC_HEAD(isc_hmacsha1, tc) {
	atf_tc_set_md_var(tc, "descr", "HMAC-SHA1 examples from RFC2104");
}
ATF_TC_BODY(isc_hmacsha1, tc) {
	isc_hmacsha1_t hmacsha1;

	UNUSED(tc);
	/*
	 * These are the various test vectors.  All of these are passed
	 * through the hash function and the results are compared to the
	 * result specified here.
	 */
	hash_testcase_t testcases[] = {
		/* Test 1 */
		{
			TEST_INPUT("\x48\x69\x20\x54\x68\x65\x72\x65"),
			"0xB617318655057264E28BC0B6FB378C8EF146BE00",
			1
		},
		/* Test 2 */
		{
			TEST_INPUT("\x77\x68\x61\x74\x20\x64\x6f\x20\x79\x61"
				   "\x20\x77\x61\x6e\x74\x20\x66\x6f\x72\x20"
				   "\x6e\x6f\x74\x68\x69\x6e\x67\x3f"),
			"0xEFFCDF6AE5EB2FA2D27416D5F184DF9C259A7C79",
			1
		},
		/* Test 3 */
		{
			TEST_INPUT("\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"),
			"0x125D7342B9AC11CD91A39AF48AA17B4F63F175D3",
			1
		},
		/* Test 4 */
		{
			TEST_INPUT("\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"),
			"0x4C9007F4026250C6BC8414F9BF50C86C2D7235DA",
			1
		},
#if 0
		/* Test 5 -- unimplemented optional functionality */
		{
			TEST_INPUT("Test With Truncation"),
			"0x4C1A03424B55E07FE7F27BE1",
			1
		},
#endif
		/* Test 6 */
		{
			TEST_INPUT("Test Using Larger Than Block-Size Key - "
				   "Hash Key First"),
			"0xAA4AE5E15272D00E95705637CE8A3B55ED402112", 1 },
		/* Test 7 */
		{
			TEST_INPUT("Test Using Larger Than Block-Size Key and "
				   "Larger Than One Block-Size Data"),
			"0xE8E99D0F45237D786D6BBAA7965C7808BBFF1A91",
			1
		},
		{ NULL, 0, NULL, 1 }
	};

	hash_testcase_t *testcase = testcases;

	hash_test_key_t test_keys[] = {
		/* Key 1 */
		{ "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b"
		  "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b", 20 },
		/* Key 2 */
		{ "Jefe", 4 },
		/* Key 3 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 20 },
		/* Key 4 */
		{ "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a"
		  "\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14"
		  "\x15\x16\x17\x18\x19", 25 },
#if 0
		/* Key 5 */
		{ "\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c"
		  "\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c", 20 },
#endif
		/* Key 6 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 80 },
		/* Key 7 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 80 },
		{ "", 0 }
	};

	hash_test_key_t *test_key = test_keys;

	while (testcase->input != NULL && testcase->result != NULL) {
		memcpy(buffer, test_key->key, test_key->len);
		isc_hmacsha1_init(&hmacsha1, buffer, test_key->len);
		isc_hmacsha1_update(&hmacsha1,
				    (const isc_uint8_t *) testcase->input,
				    testcase->input_len);
		isc_hmacsha1_sign(&hmacsha1, digest, ISC_SHA1_DIGESTLENGTH);
		tohexstr(digest, ISC_SHA1_DIGESTLENGTH, str);
		ATF_CHECK_STREQ(str, testcase->result);

		testcase++;
		test_key++;
	}
}

/* HMAC-SHA224 test */
ATF_TC(isc_hmacsha224);
ATF_TC_HEAD(isc_hmacsha224, tc) {
	atf_tc_set_md_var(tc, "descr", "HMAC-SHA224 examples from RFC4634");
}
ATF_TC_BODY(isc_hmacsha224, tc) {
	isc_hmacsha224_t hmacsha224;

	UNUSED(tc);

	/*
	 * These are the various test vectors.  All of these are passed
	 * through the hash function and the results are compared to the
	 * result specified here.
	 */
	hash_testcase_t testcases[] = {
		/* Test 1 */
		{
			TEST_INPUT("\x48\x69\x20\x54\x68\x65\x72\x65"),
			"0x896FB1128ABBDF196832107CD49DF33F47B4B1169912BA"
				"4F53684B22",
			1
		},
		/* Test 2 */
		{
			TEST_INPUT("\x77\x68\x61\x74\x20\x64\x6f\x20\x79\x61"
				   "\x20\x77\x61\x6e\x74\x20\x66\x6f\x72\x20"
				   "\x6e\x6f\x74\x68\x69\x6e\x67\x3f"),
			"0xA30E01098BC6DBBF45690F3A7E9E6D0F8BBEA2A39E61480"
				"08FD05E44",
			1
		},
		/* Test 3 */
		{
			TEST_INPUT("\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"),
			"0x7FB3CB3588C6C1F6FFA9694D7D6AD2649365B0C1F65D69"
				"D1EC8333EA",
			1
		},
		/* Test 4 */
		{
			TEST_INPUT("\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"),
			"0x6C11506874013CAC6A2ABC1BB382627CEC6A90D86EFC01"
				"2DE7AFEC5A",
			1
		},
#if 0
		/* Test 5 -- unimplemented optional functionality */
		{
			TEST_INPUT("Test With Truncation"),
			"0x4C1A03424B55E07FE7F27BE1",
			1
		},
#endif
		/* Test 6 */
		{
			TEST_INPUT("Test Using Larger Than Block-Size Key - "
				   "Hash Key First"),
			"0x95E9A0DB962095ADAEBE9B2D6F0DBCE2D499F112F2D2B7"
				"273FA6870E",
			1
		},
		/* Test 7 */
		{
			TEST_INPUT("\x54\x68\x69\x73\x20\x69\x73\x20\x61\x20"
				   "\x74\x65\x73\x74\x20\x75\x73\x69\x6e\x67"
				   "\x20\x61\x20\x6c\x61\x72\x67\x65\x72\x20"
				   "\x74\x68\x61\x6e\x20\x62\x6c\x6f\x63\x6b"
				   "\x2d\x73\x69\x7a\x65\x20\x6b\x65\x79\x20"
				   "\x61\x6e\x64\x20\x61\x20\x6c\x61\x72\x67"
				   "\x65\x72\x20\x74\x68\x61\x6e\x20\x62\x6c"
				   "\x6f\x63\x6b\x2d\x73\x69\x7a\x65\x20\x64"
				   "\x61\x74\x61\x2e\x20\x54\x68\x65\x20\x6b"
				   "\x65\x79\x20\x6e\x65\x65\x64\x73\x20\x74"
				   "\x6f\x20\x62\x65\x20\x68\x61\x73\x68\x65"
				   "\x64\x20\x62\x65\x66\x6f\x72\x65\x20\x62"
				   "\x65\x69\x6e\x67\x20\x75\x73\x65\x64\x20"
				   "\x62\x79\x20\x74\x68\x65\x20\x48\x4d\x41"
				   "\x43\x20\x61\x6c\x67\x6f\x72\x69\x74\x68"
				   "\x6d\x2e"),
			"0x3A854166AC5D9F023F54D517D0B39DBD946770DB9C2B95"
				"C9F6F565D1",
			1
		},
		{ NULL, 0, NULL, 1 }
	};

	hash_testcase_t *testcase = testcases;

	hash_test_key_t test_keys[] = {
		/* Key 1 */
		{ "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b"
		  "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b", 20 },
		/* Key 2 */
		{ "Jefe", 4 },
		/* Key 3 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 20 },
		/* Key 4 */
		{ "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a"
		  "\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14"
		  "\x15\x16\x17\x18\x19", 25 },
#if 0
		/* Key 5 */
		{ "\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c"
		  "\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c", 20 },
#endif
		/* Key 6 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 131 },
		/* Key 7 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 131 },
		{ "", 0 }
	};

	hash_test_key_t *test_key = test_keys;

	while (testcase->input != NULL && testcase->result != NULL) {
		memcpy(buffer, test_key->key, test_key->len);
		isc_hmacsha224_init(&hmacsha224, buffer, test_key->len);
		isc_hmacsha224_update(&hmacsha224,
				      (const isc_uint8_t *) testcase->input,
				      testcase->input_len);
		isc_hmacsha224_sign(&hmacsha224, digest, ISC_SHA224_DIGESTLENGTH);
		tohexstr(digest, ISC_SHA224_DIGESTLENGTH, str);
		ATF_CHECK_STREQ(str, testcase->result);

		testcase++;
		test_key++;
	}
}

/* HMAC-SHA256 test */
ATF_TC(isc_hmacsha256);
ATF_TC_HEAD(isc_hmacsha256, tc) {
	atf_tc_set_md_var(tc, "descr", "HMAC-SHA256 examples from RFC4634");
}
ATF_TC_BODY(isc_hmacsha256, tc) {
	isc_hmacsha256_t hmacsha256;

	UNUSED(tc);

	/*
	 * These are the various test vectors.  All of these are passed
	 * through the hash function and the results are compared to the
	 * result specified here.
	 */
	hash_testcase_t testcases[] = {
		/* Test 1 */
		{
			TEST_INPUT("\x48\x69\x20\x54\x68\x65\x72\x65"),
			"0xB0344C61D8DB38535CA8AFCEAF0BF12B881DC200C9833D"
				"A726E9376C2E32CFF7",
			1
		},
		/* Test 2 */
		{
			TEST_INPUT("\x77\x68\x61\x74\x20\x64\x6f\x20\x79\x61"
				   "\x20\x77\x61\x6e\x74\x20\x66\x6f\x72\x20"
				   "\x6e\x6f\x74\x68\x69\x6e\x67\x3f"),
			"0x5BDCC146BF60754E6A042426089575C75A003F089D2739"
				"839DEC58B964EC3843",
			1
		},
		/* Test 3 */
		{
			TEST_INPUT("\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"),
			"0x773EA91E36800E46854DB8EBD09181A72959098B3EF8C1"
				"22D9635514CED565FE",
			1
		},
		/* Test 4 */
		{
			TEST_INPUT("\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"),
			"0x82558A389A443C0EA4CC819899F2083A85F0FAA3E578F8"
				"077A2E3FF46729665B",
			1
		},
#if 0
		/* Test 5 -- unimplemented optional functionality */
		{
			TEST_INPUT("Test With Truncation"),
			"0x4C1A03424B55E07FE7F27BE1",
			1
		},
#endif
		/* Test 6 */
		{
			TEST_INPUT("Test Using Larger Than Block-Size Key - "
				   "Hash Key First"),
			"0x60E431591EE0B67F0D8A26AACBF5B77F8E0BC6213728C5"
				"140546040F0EE37F54",
			1
		},
		/* Test 7 */
		{
			TEST_INPUT("\x54\x68\x69\x73\x20\x69\x73\x20\x61\x20"
				   "\x74\x65\x73\x74\x20\x75\x73\x69\x6e\x67"
				   "\x20\x61\x20\x6c\x61\x72\x67\x65\x72\x20"
				   "\x74\x68\x61\x6e\x20\x62\x6c\x6f\x63\x6b"
				   "\x2d\x73\x69\x7a\x65\x20\x6b\x65\x79\x20"
				   "\x61\x6e\x64\x20\x61\x20\x6c\x61\x72\x67"
				   "\x65\x72\x20\x74\x68\x61\x6e\x20\x62\x6c"
				   "\x6f\x63\x6b\x2d\x73\x69\x7a\x65\x20\x64"
				   "\x61\x74\x61\x2e\x20\x54\x68\x65\x20\x6b"
				   "\x65\x79\x20\x6e\x65\x65\x64\x73\x20\x74"
				   "\x6f\x20\x62\x65\x20\x68\x61\x73\x68\x65"
				   "\x64\x20\x62\x65\x66\x6f\x72\x65\x20\x62"
				   "\x65\x69\x6e\x67\x20\x75\x73\x65\x64\x20"
				   "\x62\x79\x20\x74\x68\x65\x20\x48\x4d\x41"
				   "\x43\x20\x61\x6c\x67\x6f\x72\x69\x74\x68"
				   "\x6d\x2e"),
			"0x9B09FFA71B942FCB27635FBCD5B0E944BFDC63644F0713"
				"938A7F51535C3A35E2",
			1
		},
		{ NULL, 0, NULL, 1 }
	};

	hash_testcase_t *testcase = testcases;

	hash_test_key_t test_keys[] = {
		/* Key 1 */
		{ "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b"
		  "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b", 20 },
		/* Key 2 */
		{ "Jefe", 4 },
		/* Key 3 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 20 },
		/* Key 4 */
		{ "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a"
		  "\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14"
		  "\x15\x16\x17\x18\x19", 25 },
#if 0
		/* Key 5 */
		{ "\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c"
		  "\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c", 20 },
#endif
		/* Key 6 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 131 },
		/* Key 7 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 131 },
		{ "", 0 }
	};

	hash_test_key_t *test_key = test_keys;

	while (testcase->input != NULL && testcase->result != NULL) {
		memcpy(buffer, test_key->key, test_key->len);
		isc_hmacsha256_init(&hmacsha256, buffer, test_key->len);
		isc_hmacsha256_update(&hmacsha256,
				      (const isc_uint8_t *) testcase->input,
				      testcase->input_len);
		isc_hmacsha256_sign(&hmacsha256, digest, ISC_SHA256_DIGESTLENGTH);
		tohexstr(digest, ISC_SHA256_DIGESTLENGTH, str);
		ATF_CHECK_STREQ(str, testcase->result);

		testcase++;
		test_key++;
	}
}

/* HMAC-SHA384 test */
ATF_TC(isc_hmacsha384);
ATF_TC_HEAD(isc_hmacsha384, tc) {
	atf_tc_set_md_var(tc, "descr", "HMAC-SHA384 examples from RFC4634");
}
ATF_TC_BODY(isc_hmacsha384, tc) {
	isc_hmacsha384_t hmacsha384;

	UNUSED(tc);

	/*
	 * These are the various test vectors.  All of these are passed
	 * through the hash function and the results are compared to the
	 * result specified here.
	 */
	hash_testcase_t testcases[] = {
		/* Test 1 */
		{
			TEST_INPUT("\x48\x69\x20\x54\x68\x65\x72\x65"),
			"0xAFD03944D84895626B0825F4AB46907F15F9DADBE4101E"
				"C682AA034C7CEBC59CFAEA9EA9076EDE7F4AF152"
				"E8B2FA9CB6",
			1
		},
		/* Test 2 */
		{
			TEST_INPUT("\x77\x68\x61\x74\x20\x64\x6f\x20\x79\x61"
				   "\x20\x77\x61\x6e\x74\x20\x66\x6f\x72\x20"
				   "\x6e\x6f\x74\x68\x69\x6e\x67\x3f"),
			"0xAF45D2E376484031617F78D2B58A6B1B9C7EF464F5A01B"
				"47E42EC3736322445E8E2240CA5E69E2C78B3239"
				"ECFAB21649",
			1
		},
		/* Test 3 */
		{
			TEST_INPUT("\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"),
			"0x88062608D3E6AD8A0AA2ACE014C8A86F0AA635D947AC9F"
				"EBE83EF4E55966144B2A5AB39DC13814B94E3AB6"
				"E101A34F27",
			1
		},
		/* Test 4 */
		{
			TEST_INPUT("\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"),
			"0x3E8A69B7783C25851933AB6290AF6CA77A998148085000"
				"9CC5577C6E1F573B4E6801DD23C4A7D679CCF8A3"
				"86C674CFFB",
			1
		},
#if 0
		/* Test 5 -- unimplemented optional functionality */
		{
			TEST_INPUT("Test With Truncation"),
			"0x4C1A03424B55E07FE7F27BE1",
			1
		},
#endif
		/* Test 6 */
		{
			TEST_INPUT("Test Using Larger Than Block-Size Key - "
				   "Hash Key First"),
			"0x4ECE084485813E9088D2C63A041BC5B44F9EF1012A2B58"
				"8F3CD11F05033AC4C60C2EF6AB4030FE8296248D"
				"F163F44952",
			1
		},
		/* Test 7 */
		{
			TEST_INPUT("\x54\x68\x69\x73\x20\x69\x73\x20\x61\x20"
				   "\x74\x65\x73\x74\x20\x75\x73\x69\x6e\x67"
				   "\x20\x61\x20\x6c\x61\x72\x67\x65\x72\x20"
				   "\x74\x68\x61\x6e\x20\x62\x6c\x6f\x63\x6b"
				   "\x2d\x73\x69\x7a\x65\x20\x6b\x65\x79\x20"
				   "\x61\x6e\x64\x20\x61\x20\x6c\x61\x72\x67"
				   "\x65\x72\x20\x74\x68\x61\x6e\x20\x62\x6c"
				   "\x6f\x63\x6b\x2d\x73\x69\x7a\x65\x20\x64"
				   "\x61\x74\x61\x2e\x20\x54\x68\x65\x20\x6b"
				   "\x65\x79\x20\x6e\x65\x65\x64\x73\x20\x74"
				   "\x6f\x20\x62\x65\x20\x68\x61\x73\x68\x65"
				   "\x64\x20\x62\x65\x66\x6f\x72\x65\x20\x62"
				   "\x65\x69\x6e\x67\x20\x75\x73\x65\x64\x20"
				   "\x62\x79\x20\x74\x68\x65\x20\x48\x4d\x41"
				   "\x43\x20\x61\x6c\x67\x6f\x72\x69\x74\x68"
				   "\x6d\x2e"),
			"0x6617178E941F020D351E2F254E8FD32C602420FEB0B8FB"
				"9ADCCEBB82461E99C5A678CC31E799176D3860E6"
				"110C46523E",
			1
		},
		{ NULL, 0, NULL, 1 }
	};

	hash_testcase_t *testcase = testcases;

	hash_test_key_t test_keys[] = {
		/* Key 1 */
		{ "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b"
		  "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b", 20 },
		/* Key 2 */
		{ "Jefe", 4 },
		/* Key 3 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 20 },
		/* Key 4 */
		{ "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a"
		  "\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14"
		  "\x15\x16\x17\x18\x19", 25 },
#if 0
		/* Key 5 */
		{ "\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c"
		  "\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c", 20 },
#endif
		/* Key 6 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 131 },
		/* Key 7 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 131 },
		{ "", 0 }
	};

	hash_test_key_t *test_key = test_keys;

	while (testcase->input != NULL && testcase->result != NULL) {
		memcpy(buffer, test_key->key, test_key->len);
		isc_hmacsha384_init(&hmacsha384, buffer, test_key->len);
		isc_hmacsha384_update(&hmacsha384,
				      (const isc_uint8_t *) testcase->input,
				      testcase->input_len);
		isc_hmacsha384_sign(&hmacsha384, digest, ISC_SHA384_DIGESTLENGTH);
		tohexstr(digest, ISC_SHA384_DIGESTLENGTH, str);
		ATF_CHECK_STREQ(str, testcase->result);

		testcase++;
		test_key++;
	}
}

/* HMAC-SHA512 test */
ATF_TC(isc_hmacsha512);
ATF_TC_HEAD(isc_hmacsha512, tc) {
	atf_tc_set_md_var(tc, "descr", "HMAC-SHA512 examples from RFC4634");
}
ATF_TC_BODY(isc_hmacsha512, tc) {
	isc_hmacsha512_t hmacsha512;

	UNUSED(tc);

	/*
	 * These are the various test vectors.  All of these are passed
	 * through the hash function and the results are compared to the
	 * result specified here.
	 */
	hash_testcase_t testcases[] = {
		/* Test 1 */
		{
			TEST_INPUT("\x48\x69\x20\x54\x68\x65\x72\x65"),
			"0x87AA7CDEA5EF619D4FF0B4241A1D6CB02379F4E2CE4EC2"
				"787AD0B30545E17CDEDAA833B7D6B8A702038B27"
				"4EAEA3F4E4BE9D914EEB61F1702E696C203A126854",
			1
		},
		/* Test 2 */
		{
			TEST_INPUT("\x77\x68\x61\x74\x20\x64\x6f\x20\x79\x61"
				   "\x20\x77\x61\x6e\x74\x20\x66\x6f\x72\x20"
				   "\x6e\x6f\x74\x68\x69\x6e\x67\x3f"),
			"0x164B7A7BFCF819E2E395FBE73B56E0A387BD64222E831F"
				"D610270CD7EA2505549758BF75C05A994A6D034F"
				"65F8F0E6FDCAEAB1A34D4A6B4B636E070A38BCE737",
			1
		},
		/* Test 3 */
		{
			TEST_INPUT("\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"),
			"0xFA73B0089D56A284EFB0F0756C890BE9B1B5DBDD8EE81A"
				"3655F83E33B2279D39BF3E848279A722C806B485"
				"A47E67C807B946A337BEE8942674278859E13292FB",
			1
		},
		/* Test 4 */
		{
			TEST_INPUT("\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"),
			"0xB0BA465637458C6990E5A8C5F61D4AF7E576D97FF94B87"
				"2DE76F8050361EE3DBA91CA5C11AA25EB4D67927"
				"5CC5788063A5F19741120C4F2DE2ADEBEB10A298DD",
			1
		},
#if 0
		/* Test 5 -- unimplemented optional functionality */
		{
			TEST_INPUT("Test With Truncation"),
			"0x4C1A03424B55E07FE7F27BE1",
			1
		},
#endif
		/* Test 6 */
		{
			TEST_INPUT("Test Using Larger Than Block-Size Key - "
				   "Hash Key First"),
			"0x80B24263C7C1A3EBB71493C1DD7BE8B49B46D1F41B4AEE"
				"C1121B013783F8F3526B56D037E05F2598BD0FD2"
				"215D6A1E5295E64F73F63F0AEC8B915A985D786598",
			1
		},
		/* Test 7 */
		{
			TEST_INPUT("\x54\x68\x69\x73\x20\x69\x73\x20\x61\x20"
				   "\x74\x65\x73\x74\x20\x75\x73\x69\x6e\x67"
				   "\x20\x61\x20\x6c\x61\x72\x67\x65\x72\x20"
				   "\x74\x68\x61\x6e\x20\x62\x6c\x6f\x63\x6b"
				   "\x2d\x73\x69\x7a\x65\x20\x6b\x65\x79\x20"
				   "\x61\x6e\x64\x20\x61\x20\x6c\x61\x72\x67"
				   "\x65\x72\x20\x74\x68\x61\x6e\x20\x62\x6c"
				   "\x6f\x63\x6b\x2d\x73\x69\x7a\x65\x20\x64"
				   "\x61\x74\x61\x2e\x20\x54\x68\x65\x20\x6b"
				   "\x65\x79\x20\x6e\x65\x65\x64\x73\x20\x74"
				   "\x6f\x20\x62\x65\x20\x68\x61\x73\x68\x65"
				   "\x64\x20\x62\x65\x66\x6f\x72\x65\x20\x62"
				   "\x65\x69\x6e\x67\x20\x75\x73\x65\x64\x20"
				   "\x62\x79\x20\x74\x68\x65\x20\x48\x4d\x41"
				   "\x43\x20\x61\x6c\x67\x6f\x72\x69\x74\x68"
				   "\x6d\x2e"),
			"0xE37B6A775DC87DBAA4DFA9F96E5E3FFDDEBD71F8867289"
				"865DF5A32D20CDC944B6022CAC3C4982B10D5EEB"
				"55C3E4DE15134676FB6DE0446065C97440FA8C6A58",
			1
		},
		{ NULL, 0, NULL, 1 }
	};

	hash_testcase_t *testcase = testcases;

	hash_test_key_t test_keys[] = {
		/* Key 1 */
		{ "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b"
		  "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b", 20 },
		/* Key 2 */
		{ "Jefe", 4 },
		/* Key 3 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 20 },
		/* Key 4 */
		{ "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a"
		  "\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14"
		  "\x15\x16\x17\x18\x19", 25 },
#if 0
		/* Key 5 */
		{ "\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c"
		  "\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c", 20 },
#endif
		/* Key 6 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 131 },
		/* Key 7 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 131 },
		{ "", 0 }
	};

	hash_test_key_t *test_key = test_keys;

	while (testcase->input != NULL && testcase->result != NULL) {
		memcpy(buffer, test_key->key, test_key->len);
		isc_hmacsha512_init(&hmacsha512, buffer, test_key->len);
		isc_hmacsha512_update(&hmacsha512,
				      (const isc_uint8_t *) testcase->input,
				      testcase->input_len);
		isc_hmacsha512_sign(&hmacsha512, digest, ISC_SHA512_DIGESTLENGTH);
		tohexstr(digest, ISC_SHA512_DIGESTLENGTH, str);
		ATF_CHECK_STREQ(str, testcase->result);

		testcase++;
		test_key++;
	}
}


/* HMAC-MD5 Test */
ATF_TC(isc_hmacmd5);
ATF_TC_HEAD(isc_hmacmd5, tc) {
	atf_tc_set_md_var(tc, "descr", "HMAC-MD5 examples from RFC2104");
}
ATF_TC_BODY(isc_hmacmd5, tc) {
	isc_hmacmd5_t hmacmd5;

	UNUSED(tc);

	/*
	 * These are the various test vectors.  All of these are passed
	 * through the hash function and the results are compared to the
	 * result specified here.
	 */
	hash_testcase_t testcases[] = {
		/* Test 1 */
		{
			TEST_INPUT("\x48\x69\x20\x54\x68\x65\x72\x65"),
			"0x9294727A3638BB1C13F48EF8158BFC9D",
			1
		},
		/* Test 2 */
		{
			TEST_INPUT("\x77\x68\x61\x74\x20\x64\x6f\x20\x79"
				   "\x61\x20\x77\x61\x6e\x74\x20\x66\x6f"
				   "\x72\x20\x6e\x6f\x74\x68\x69\x6e\x67\x3f"),
			"0x750C783E6AB0B503EAA86E310A5DB738", 1
		},
		/* Test 3 */
		{
			TEST_INPUT("\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
				   "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"),
			"0x56BE34521D144C88DBB8C733F0E8B3F6",
			1
		},
		/* Test 4 */
		{
			TEST_INPUT("\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
				   "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"),
			"0x697EAF0ACA3A3AEA3A75164746FFAA79",
			1
		},
#if 0
		/* Test 5 -- unimplemented optional functionality */
		{
			TEST_INPUT("Test With Truncation"),
			"0x4C1A03424B55E07FE7F27BE1",
			1
		},
		/* Test 6 -- unimplemented optional functionality */
		{
			TEST_INPUT("Test Using Larger Than Block-Size Key - "
				   "Hash Key First"),
			"0xAA4AE5E15272D00E95705637CE8A3B55ED402112",
			1
		 },
		/* Test 7 -- unimplemented optional functionality */
		{
			TEST_INPUT("Test Using Larger Than Block-Size Key and "
				   "Larger Than One Block-Size Data"),
			"0xE8E99D0F45237D786D6BBAA7965C7808BBFF1A91",
			1
		},
#endif
		{ NULL, 0, NULL, 1 }
	};

	hash_testcase_t *testcase = testcases;

	hash_test_key_t test_keys[] = {
		/* Key 1 */
		{ "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b"
		  "\x0b\x0b\x0b\x0b\x0b\x0b", 16 },
		/* Key 2 */
		{ "Jefe", 4 },
		/* Key 3 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa", 16 },
		/* Key 4 */
		{ "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a"
		  "\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14"
		  "\x15\x16\x17\x18\x19", 25 },
#if 0
		/* Key 5 */
		{ "\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c"
		  "\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c\x0c", 20 },
		/* Key 6 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 131 },
		/* Key 7 */
		{ "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
		  "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa", 131 },
#endif
		{ "", 0 }
	};

	hash_test_key_t *test_key = test_keys;

	while (testcase->input != NULL && testcase->result != NULL) {
		memcpy(buffer, test_key->key, test_key->len);
		isc_hmacmd5_init(&hmacmd5, buffer, test_key->len);
		isc_hmacmd5_update(&hmacmd5,
				   (const isc_uint8_t *) testcase->input,
				   testcase->input_len);
		isc_hmacmd5_sign(&hmacmd5, digest);
		tohexstr(digest, ISC_MD5_DIGESTLENGTH, str);
		ATF_CHECK_STREQ(str, testcase->result);

		testcase++;
		test_key++;
	}
}

/*
 * Main
 */
ATF_TP_ADD_TCS(tp) {
	ATF_TP_ADD_TC(tp, isc_hmacmd5);
	ATF_TP_ADD_TC(tp, isc_hmacsha1);
	ATF_TP_ADD_TC(tp, isc_hmacsha224);
	ATF_TP_ADD_TC(tp, isc_hmacsha256);
	ATF_TP_ADD_TC(tp, isc_hmacsha384);
	ATF_TP_ADD_TC(tp, isc_hmacsha512);
	ATF_TP_ADD_TC(tp, isc_md5);
	ATF_TP_ADD_TC(tp, isc_sha1);
	ATF_TP_ADD_TC(tp, isc_sha224);
	ATF_TP_ADD_TC(tp, isc_sha256);
	ATF_TP_ADD_TC(tp, isc_sha384);
	ATF_TP_ADD_TC(tp, isc_sha512);
	return (atf_no_error());
}

