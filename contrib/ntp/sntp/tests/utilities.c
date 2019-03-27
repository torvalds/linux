#include "config.h"

#include "sntptest.h"
#include "fileHandlingTest.h"
#include "main.h"
#include "utilities.h"

#include "unity.h"

#include <math.h>

sockaddr_u CreateSockaddr4(const char* address);
struct addrinfo CreateAddrinfo(sockaddr_u* sock);
void InitDebugTest(const char * filename);
void FinishDebugTest(const char * expected,const char * actual);
void test_IPv4Address(void);
void test_IPv6Address(void);
void test_SetLiVnMode1(void);
void test_SetLiVnMode2(void);
void test_PktOutput(void);
void test_LfpOutputBinaryFormat(void);
void test_LfpOutputDecimalFormat(void);


const char * Version = "stub unit test Version string";


sockaddr_u
CreateSockaddr4(const char* address) {
	sockaddr_u s;
	s.sa4.sin_family = AF_INET;
	s.sa4.sin_addr.s_addr = inet_addr(address);
	SET_PORT(&s, 123);

	return s;
}


struct addrinfo
CreateAddrinfo(sockaddr_u* sock) {
	struct addrinfo a;
	a.ai_family = sock->sa.sa_family;
	a.ai_addrlen = SIZEOF_SOCKADDR(a.ai_family);
	a.ai_addr = &sock->sa;
	return a;
}


bool outputFileOpened;
FILE* outputFile;


void
InitDebugTest(const char * filename) {
	// Clear the contents of the current file.
	// Open the output file
	outputFile = fopen(filename, "w+");
	TEST_ASSERT_NOT_NULL(outputFile);
	outputFileOpened = true;
}


// Closes outputFile, and compare contents.
void
FinishDebugTest(const char * expected,
		     const char * actual) {
	if (outputFileOpened)
		fclose(outputFile);

	FILE * e = fopen(expected,"rb");
	FILE * a = fopen(actual,"rb");
	TEST_ASSERT_NOT_NULL(e);
	TEST_ASSERT_NOT_NULL(a);

	CompareFileContent(e, a);
}


/* 
 * These tests are essentially a copy of the tests for socktoa()
 * in libntp. If sntp switches to using that functions, these
 * tests can be removed.
 */

void
test_IPv4Address(void) {
	const char* ADDR = "192.0.2.10";

	sockaddr_u input = CreateSockaddr4(ADDR);
	struct addrinfo inputA = CreateAddrinfo(&input);

	TEST_ASSERT_EQUAL_STRING(ADDR, ss_to_str(&input));
	TEST_ASSERT_EQUAL_STRING(ADDR, addrinfo_to_str(&inputA));
}


void
test_IPv6Address(void) {
	const struct in6_addr address = { { {
						0x20, 0x01, 0x0d, 0xb8,
						0x85, 0xa3, 0x08, 0xd3, 
						0x13, 0x19, 0x8a, 0x2e,
						0x03, 0x70, 0x73, 0x34
					} } };
	const char * expected = "2001:db8:85a3:8d3:1319:8a2e:370:7334";
	sockaddr_u	input;
	struct addrinfo	inputA;

	memset(&input, 0, sizeof(input));
	input.sa6.sin6_family = AF_INET6;
	input.sa6.sin6_addr = address;
	TEST_ASSERT_EQUAL_STRING(expected, ss_to_str(&input));

	inputA = CreateAddrinfo(&input);
	TEST_ASSERT_EQUAL_STRING(expected, addrinfo_to_str(&inputA));
}


void
test_SetLiVnMode1(void) {
	struct pkt expected;
	expected.li_vn_mode = PKT_LI_VN_MODE(LEAP_NOWARNING,
					     NTP_VERSION,
					     MODE_SERVER);

	struct pkt actual;
	set_li_vn_mode(&actual, LEAP_NOWARNING, NTP_VERSION,
				   MODE_SERVER);

	TEST_ASSERT_EQUAL(expected.li_vn_mode, actual.li_vn_mode);
}


void
test_SetLiVnMode2(void) {
	struct pkt expected;
	expected.li_vn_mode = PKT_LI_VN_MODE(LEAP_NOTINSYNC,
					     NTP_OLDVERSION,
					     MODE_BROADCAST);

	struct pkt actual;
	set_li_vn_mode(&actual, LEAP_NOTINSYNC, NTP_OLDVERSION,
				   MODE_BROADCAST);

	TEST_ASSERT_EQUAL(expected.li_vn_mode, actual.li_vn_mode);
}

/* Debug utilities tests */

void
test_PktOutput(void) {
	char * filename = "debug-output-pkt";
	InitDebugTest(filename);

	struct pkt testpkt;
	memset(&testpkt, 0, sizeof(struct pkt));
	testpkt.li_vn_mode = PKT_LI_VN_MODE(LEAP_NOWARNING,
					    NTP_VERSION,
					    MODE_SERVER);

	l_fp test;
	test.l_ui = 8;
	test.l_uf = 2147483647; // Lots of ones.
	HTONL_FP(&test, &testpkt.xmt);

	pkt_output(&testpkt, LEN_PKT_NOMAC, outputFile);

	FinishDebugTest(CreatePath("debug-input-pkt", INPUT_DIR), filename);
}


void
test_LfpOutputBinaryFormat(void) {
	char * filename = "debug-output-lfp-bin";//CreatePath("debug-output-lfp-bin", OUTPUT_DIR);
	InitDebugTest(filename);

	l_fp test;
	test.l_ui = 63;  // 00000000 00000000 00000000 00111111
	test.l_uf = 127; // 00000000 00000000 00000000 01111111

	l_fp network;
	HTONL_FP(&test, &network);

	l_fp_output_bin(&network, outputFile);

	FinishDebugTest(CreatePath("debug-input-lfp-bin", INPUT_DIR), filename);
}


void
test_LfpOutputDecimalFormat(void) {
	char * filename = "debug-output-lfp-dec";
	InitDebugTest(filename);

	l_fp test;
	test.l_ui = 6310; // 0x000018A6
	test.l_uf = 308502; // 0x00004B516

	l_fp network;
	HTONL_FP(&test, &network);

	l_fp_output_dec(&network, outputFile);

	FinishDebugTest(CreatePath("debug-input-lfp-dec", INPUT_DIR), filename);
}
