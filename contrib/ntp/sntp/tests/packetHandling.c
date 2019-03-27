#include "config.h"
#include "ntp_debug.h"
#include "ntp_stdlib.h"
#include "ntp_types.h"

#include "sntptest.h"

#include "kod_management.h"
#include "main.h"
#include "networking.h"
#include "ntp.h"

#include "unity.h"

void setUp(void);
int LfpEquality(const l_fp expected, const l_fp actual);
void test_GenerateUnauthenticatedPacket(void);
void test_GenerateAuthenticatedPacket(void);
void test_OffsetCalculationPositiveOffset(void);
void test_OffsetCalculationNegativeOffset(void);
void test_HandleUnusableServer(void);
void test_HandleUnusablePacket(void);
void test_HandleServerAuthenticationFailure(void);
void test_HandleKodDemobilize(void);
void test_HandleKodRate(void);
void test_HandleCorrectPacket(void);


void
setUp(void)
{ 
	init_lib(); 
}


int
LfpEquality(
	const l_fp	expected,
	const l_fp 	actual
	)
{
	return !!(L_ISEQU(&expected, &actual));
}


void
test_GenerateUnauthenticatedPacket(void)
{
	struct pkt	testpkt;
	struct timeval	xmt;
	l_fp		expected_xmt, actual_xmt;

	GETTIMEOFDAY(&xmt, NULL);
	xmt.tv_sec += JAN_1970;

	TEST_ASSERT_EQUAL(LEN_PKT_NOMAC,
			  generate_pkt(&testpkt, &xmt, 0, NULL));

	TEST_ASSERT_EQUAL(LEAP_NOTINSYNC, PKT_LEAP(testpkt.li_vn_mode));
	TEST_ASSERT_EQUAL(NTP_VERSION, PKT_VERSION(testpkt.li_vn_mode));
	TEST_ASSERT_EQUAL(MODE_CLIENT, PKT_MODE(testpkt.li_vn_mode));

	TEST_ASSERT_EQUAL(STRATUM_UNSPEC, PKT_TO_STRATUM(testpkt.stratum));
	TEST_ASSERT_EQUAL(8, testpkt.ppoll);

	TVTOTS(&xmt, &expected_xmt);
	NTOHL_FP(&testpkt.xmt, &actual_xmt);
	TEST_ASSERT_TRUE(LfpEquality(expected_xmt, actual_xmt));
}


void
test_GenerateAuthenticatedPacket(void)
{
	static const int EXPECTED_PKTLEN = LEN_PKT_NOMAC + MAX_MD5_LEN;
	
	struct key	testkey;
	struct pkt	testpkt;
	struct timeval	xmt;
	l_fp		expected_xmt, actual_xmt;
	char 		expected_mac[MAX_MD5_LEN];
	
	testkey.next = NULL;
	testkey.key_id = 30;
	testkey.key_len = 9;
	memcpy(testkey.key_seq, "123456789", testkey.key_len);
	strlcpy(testkey.typen, "MD5", sizeof(testkey.typen));
	testkey.typei = keytype_from_text(testkey.typen, NULL);

	GETTIMEOFDAY(&xmt, NULL);
	xmt.tv_sec += JAN_1970;

	TEST_ASSERT_EQUAL(EXPECTED_PKTLEN,
			  generate_pkt(&testpkt, &xmt, testkey.key_id, &testkey));

	TEST_ASSERT_EQUAL(LEAP_NOTINSYNC, PKT_LEAP(testpkt.li_vn_mode));
	TEST_ASSERT_EQUAL(NTP_VERSION, PKT_VERSION(testpkt.li_vn_mode));
	TEST_ASSERT_EQUAL(MODE_CLIENT, PKT_MODE(testpkt.li_vn_mode));

	TEST_ASSERT_EQUAL(STRATUM_UNSPEC, PKT_TO_STRATUM(testpkt.stratum));
	TEST_ASSERT_EQUAL(8, testpkt.ppoll);

	TVTOTS(&xmt, &expected_xmt);
	NTOHL_FP(&testpkt.xmt, &actual_xmt);
	TEST_ASSERT_TRUE(LfpEquality(expected_xmt, actual_xmt));

	TEST_ASSERT_EQUAL(testkey.key_id, ntohl(testpkt.exten[0]));
	
	TEST_ASSERT_EQUAL(MAX_MD5_LEN - 4, /* Remove the key_id, only keep the mac. */
			  make_mac(&testpkt, LEN_PKT_NOMAC, MAX_MD5_LEN-4, &testkey, expected_mac));
	TEST_ASSERT_EQUAL_MEMORY(expected_mac, (char*)&testpkt.exten[1], MAX_MD5_LEN -4);
}


void
test_OffsetCalculationPositiveOffset(void)
{
	struct pkt	rpkt;
	l_fp		reftime, tmp;
	struct timeval	dst;
	double		offset, precision, synch_distance;

	rpkt.precision = -16; /* 0,000015259 */
	rpkt.rootdelay = HTONS_FP(DTOUFP(0.125));
	rpkt.rootdisp = HTONS_FP(DTOUFP(0.25));

	/* Synch Distance: (0.125+0.25)/2.0 == 0.1875 */
	get_systime(&reftime);
	HTONL_FP(&reftime, &rpkt.reftime);

	/* T1 - Originate timestamp */
	tmp.l_ui = 1000000000UL;
	tmp.l_uf = 0UL;
	HTONL_FP(&tmp, &rpkt.org);

	/* T2 - Receive timestamp */
	tmp.l_ui = 1000000001UL;
	tmp.l_uf = 2147483648UL;
	HTONL_FP(&tmp, &rpkt.rec);

	/* T3 - Transmit timestamp */
	tmp.l_ui = 1000000002UL;
	tmp.l_uf = 0UL;
	HTONL_FP(&tmp, &rpkt.xmt);

	/* T4 - Destination timestamp as standard timeval */
	tmp.l_ui = 1000000001UL;
	tmp.l_uf = 0UL;
	TSTOTV(&tmp, &dst);
	dst.tv_sec -= JAN_1970;

	offset_calculation(&rpkt, LEN_PKT_NOMAC, &dst, &offset, &precision, &synch_distance);

	TEST_ASSERT_EQUAL_DOUBLE(1.25, offset);
	TEST_ASSERT_EQUAL_DOUBLE(1. / ULOGTOD(16), precision);
	/* 1.1250150000000001 ? */
	TEST_ASSERT_EQUAL_DOUBLE(1.125015, synch_distance);
}


void
test_OffsetCalculationNegativeOffset(void)
{
	struct pkt	rpkt;
	l_fp		reftime, tmp;
	struct timeval	dst;
	double		offset, precision, synch_distance;

	rpkt.precision = -1;
	rpkt.rootdelay = HTONS_FP(DTOUFP(0.5));
	rpkt.rootdisp = HTONS_FP(DTOUFP(0.5));
	
	/* Synch Distance is (0.5+0.5)/2.0, or 0.5 */
	get_systime(&reftime);
	HTONL_FP(&reftime, &rpkt.reftime);

	/* T1 - Originate timestamp */
	tmp.l_ui = 1000000001UL;
	tmp.l_uf = 0UL;
	HTONL_FP(&tmp, &rpkt.org);

	/* T2 - Receive timestamp */
	tmp.l_ui = 1000000000UL;
	tmp.l_uf = 2147483648UL;
	HTONL_FP(&tmp, &rpkt.rec);

	/*/ T3 - Transmit timestamp */
	tmp.l_ui = 1000000001UL;
	tmp.l_uf = 2147483648UL;
	HTONL_FP(&tmp, &rpkt.xmt);

	/* T4 - Destination timestamp as standard timeval */
	tmp.l_ui = 1000000003UL;
	tmp.l_uf = 0UL;

	TSTOTV(&tmp, &dst);
	dst.tv_sec -= JAN_1970;

	offset_calculation(&rpkt, LEN_PKT_NOMAC, &dst, &offset, &precision, &synch_distance);

	TEST_ASSERT_EQUAL_DOUBLE(-1, offset);
	TEST_ASSERT_EQUAL_DOUBLE(1. / ULOGTOD(1), precision);
	TEST_ASSERT_EQUAL_DOUBLE(1.3333483333333334, synch_distance);
}


void
test_HandleUnusableServer(void)
{
	struct pkt	rpkt;
	sockaddr_u	host;
	int		rpktl;

	ZERO(rpkt);
	ZERO(host);
	rpktl = SERVER_UNUSEABLE;
	TEST_ASSERT_EQUAL(-1, handle_pkt(rpktl, &rpkt, &host, ""));
}


void
test_HandleUnusablePacket(void)
{
	struct pkt	rpkt;
	sockaddr_u	host;
	int		rpktl;

	ZERO(rpkt);
	ZERO(host);
	rpktl = PACKET_UNUSEABLE;
	TEST_ASSERT_EQUAL(1, handle_pkt(rpktl, &rpkt, &host, ""));
}


void
test_HandleServerAuthenticationFailure(void)
{
	struct pkt	rpkt;
	sockaddr_u	host;
	int		rpktl;

	ZERO(rpkt);
	ZERO(host);
	rpktl = SERVER_AUTH_FAIL;
	TEST_ASSERT_EQUAL(1, handle_pkt(rpktl, &rpkt, &host, ""));
}


void
test_HandleKodDemobilize(void)
{
	static const char *	HOSTNAME = "192.0.2.1";
	static const char *	REASON = "DENY";
	struct pkt		rpkt;
	sockaddr_u		host;
	int			rpktl;
	struct kod_entry *	entry;

	rpktl = KOD_DEMOBILIZE;
	ZERO(rpkt);
	memcpy(&rpkt.refid, REASON, 4);
	ZERO(host);
	host.sa4.sin_family = AF_INET;
	host.sa4.sin_addr.s_addr = inet_addr(HOSTNAME);

	/* Test that the KOD-entry is added to the database. */
	kod_init_kod_db("/dev/null", TRUE);

	TEST_ASSERT_EQUAL(1, handle_pkt(rpktl, &rpkt, &host, HOSTNAME));

	TEST_ASSERT_EQUAL(1, search_entry(HOSTNAME, &entry));
	TEST_ASSERT_EQUAL_MEMORY(REASON, entry->type, 4);
}


void
test_HandleKodRate(void)
{
	struct 	pkt	rpkt;
	sockaddr_u	host;
	int		rpktl;

	ZERO(rpkt);
	ZERO(host);
	rpktl = KOD_RATE;
	TEST_ASSERT_EQUAL(1, handle_pkt(rpktl, &rpkt, &host, ""));
}


void
test_HandleCorrectPacket(void)
{
	struct pkt	rpkt;
	sockaddr_u	host;
	int		rpktl;
	l_fp		now;

	/* We don't want our testing code to actually change the system clock. */
	TEST_ASSERT_FALSE(ENABLED_OPT(STEP));
	TEST_ASSERT_FALSE(ENABLED_OPT(SLEW));

	get_systime(&now);
	HTONL_FP(&now, &rpkt.reftime);
	HTONL_FP(&now, &rpkt.org);
	HTONL_FP(&now, &rpkt.rec);
	HTONL_FP(&now, &rpkt.xmt);
	rpktl = LEN_PKT_NOMAC;
	ZERO(host);
	AF(&host) = AF_INET;

	TEST_ASSERT_EQUAL(0, handle_pkt(rpktl, &rpkt, &host, ""));
}

/* packetHandling.c */
