#include "config.h"

#include "ntp_workimpl.h"
#include "ntp_types.h"
#include "sntptest.h"
#include "ntp_stdlib.h"
#include "sntp-opts.h"
#include "kod_management.h"
#include "ntp_io.h"

#include "unity.h"

void setUp(void);
void test_SingleEntryHandling(void);
void test_MultipleEntryHandling(void);
void test_NoMatchInSearch(void);
void test_AddDuplicate(void);
void test_DeleteEntry(void);


void
setUp(void) {
	kod_init_kod_db("/dev/null", TRUE);
	init_lib();
}


void
test_SingleEntryHandling(void) {
	const char HOST[] = "192.0.2.5";
	const char REASON[] = "DENY";

	add_entry(HOST, REASON);

	struct kod_entry* result;

	TEST_ASSERT_EQUAL(1, search_entry(HOST, &result));
	TEST_ASSERT_EQUAL_STRING(HOST, result->hostname);
	TEST_ASSERT_EQUAL_STRING(REASON, result->type);
}


void
test_MultipleEntryHandling(void) {
	const char HOST1[] = "192.0.2.3";
	const char REASON1[] = "DENY";

	const char HOST2[] = "192.0.5.5";
	const char REASON2[] = "RATE";

	const char HOST3[] = "192.0.10.1";
	const char REASON3[] = "DENY";

	add_entry(HOST1, REASON1);
	add_entry(HOST2, REASON2);
	add_entry(HOST3, REASON3);

	struct kod_entry* result;

	TEST_ASSERT_EQUAL(1, search_entry(HOST1, &result));
	TEST_ASSERT_EQUAL_STRING(HOST1, result->hostname);
	TEST_ASSERT_EQUAL_STRING(REASON1, result->type);

	TEST_ASSERT_EQUAL(1, search_entry(HOST2, &result));
	TEST_ASSERT_EQUAL_STRING(HOST2, result->hostname);
	TEST_ASSERT_EQUAL_STRING(REASON2, result->type);

	TEST_ASSERT_EQUAL(1, search_entry(HOST3, &result));
	TEST_ASSERT_EQUAL_STRING(HOST3, result->hostname);
	TEST_ASSERT_EQUAL_STRING(REASON3, result->type);

	free(result);
}


void
test_NoMatchInSearch(void) {
	const char HOST_ADD[] = "192.0.2.6";
	const char HOST_NOTADD[] = "192.0.6.1";
	const char REASON[] = "DENY";

	add_entry(HOST_ADD, REASON);

	struct kod_entry* result;

	TEST_ASSERT_EQUAL(0, search_entry(HOST_NOTADD, &result));
	TEST_ASSERT_TRUE(result == NULL);
}


void
test_AddDuplicate(void) {
	const char HOST[] = "192.0.2.3";
	const char REASON1[] = "RATE";
	const char REASON2[] = "DENY";

	add_entry(HOST, REASON1);
	struct kod_entry* result1;
	TEST_ASSERT_EQUAL(1, search_entry(HOST, &result1));

	/* 
	 * Sleeps for two seconds since we want to ensure that
	 * the timestamp is updated to a new value.
	 */
	sleep(2);

	add_entry(HOST, REASON2);
	struct kod_entry* result2;
	TEST_ASSERT_EQUAL(1, search_entry(HOST, &result2));

	TEST_ASSERT_FALSE(result1->timestamp == result2->timestamp);

	free(result1);
	free(result2);
}


void
test_DeleteEntry(void) {
	const char HOST1[] = "192.0.2.1";
	const char HOST2[] = "192.0.2.2";
	const char HOST3[] = "192.0.2.3";
	const char REASON[] = "DENY";

	add_entry(HOST1, REASON);
	add_entry(HOST2, REASON);
	add_entry(HOST3, REASON);

	struct kod_entry* result;
	
	TEST_ASSERT_EQUAL(1, search_entry(HOST2, &result));
	free(result);

	delete_entry(HOST2, REASON);

	TEST_ASSERT_EQUAL(0, search_entry(HOST2, &result));

	// Ensure that the other entry is still there.
	TEST_ASSERT_EQUAL(1, search_entry(HOST1, &result));
	free(result);
}
