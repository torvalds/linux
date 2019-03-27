#include "config.h"

#include "ntp_types.h"
#include "ntp_stdlib.h" // For estrdup()
#include "fileHandlingTest.h"
#include "kod_management.h"

#include "unity.h"

/*
 * We access some parts of the kod database directly, without
 * going through the public interface
 */
extern int kod_db_cnt;
extern struct kod_entry** kod_db;
extern char* kod_db_file;

void setUp(void);
void test_ReadEmptyFile(void);
void test_ReadCorrectFile(void);
void test_ReadFileWithBlankLines(void);
void test_WriteEmptyFile(void);
void test_WriteFileWithSingleEntry(void);
void test_WriteFileWithMultipleEntries(void);


void
setUp(void) {
	kod_db_cnt = 0;
	kod_db = NULL;
	init_lib();
}


void
test_ReadEmptyFile(void) {
	kod_init_kod_db(CreatePath("kod-test-empty", INPUT_DIR), TRUE);

	TEST_ASSERT_EQUAL(0, kod_db_cnt);
}


void
test_ReadCorrectFile(void) {
	kod_init_kod_db(CreatePath("kod-test-correct", INPUT_DIR), TRUE);
	
	TEST_ASSERT_EQUAL(2, kod_db_cnt);

	struct kod_entry* res;

	TEST_ASSERT_EQUAL(1, search_entry("192.0.2.5", &res));
	TEST_ASSERT_EQUAL_STRING("DENY", res->type);
	TEST_ASSERT_EQUAL_STRING("192.0.2.5", res->hostname);
	TEST_ASSERT_EQUAL(0x12345678, res->timestamp);

	TEST_ASSERT_EQUAL(1, search_entry("192.0.2.100", &res));
	TEST_ASSERT_EQUAL_STRING("RSTR", res->type);
	TEST_ASSERT_EQUAL_STRING("192.0.2.100", res->hostname);
	TEST_ASSERT_EQUAL(0xfff, res->timestamp);
}


void
test_ReadFileWithBlankLines(void) {
	kod_init_kod_db(CreatePath("kod-test-blanks", INPUT_DIR), TRUE);

	TEST_ASSERT_EQUAL(3, kod_db_cnt);

	struct kod_entry* res;

	TEST_ASSERT_EQUAL(1, search_entry("192.0.2.5", &res));
	TEST_ASSERT_EQUAL_STRING("DENY", res->type);
	TEST_ASSERT_EQUAL_STRING("192.0.2.5", res->hostname);
	TEST_ASSERT_EQUAL(0x12345678, res->timestamp);

	TEST_ASSERT_EQUAL(1, search_entry("192.0.2.100", &res));
	TEST_ASSERT_EQUAL_STRING("RSTR", res->type);
	TEST_ASSERT_EQUAL_STRING("192.0.2.100", res->hostname);
	TEST_ASSERT_EQUAL(0xfff, res->timestamp);

	TEST_ASSERT_EQUAL(1, search_entry("example.com", &res));
	TEST_ASSERT_EQUAL_STRING("DENY", res->type);
	TEST_ASSERT_EQUAL_STRING("example.com", res->hostname);
	TEST_ASSERT_EQUAL(0xabcd, res->timestamp);
}


void
test_WriteEmptyFile(void) {
	kod_db_file = estrdup("kod-output-blank");
	write_kod_db();

	// Open file and ensure that the filesize is 0 bytes.
	FILE * is = fopen(kod_db_file, "rb");
	TEST_ASSERT_NOT_NULL(is);

	TEST_ASSERT_EQUAL(0, GetFileSize(is));

	fclose(is);
}


void
test_WriteFileWithSingleEntry(void) {
	kod_db_file = estrdup("kod-output-single"); 
	add_entry("host1", "DENY");

	// Here we must manipulate the timestamps, so they match the one in
	// the expected file.

	kod_db[0]->timestamp = 1;

	write_kod_db();

	// Open file and compare sizes.
	FILE * actual = fopen(kod_db_file, "rb");
	FILE * expected = fopen(CreatePath("kod-expected-single", INPUT_DIR),"rb");

	TEST_ASSERT_NOT_NULL(actual);
	TEST_ASSERT_NOT_NULL(expected);

	TEST_ASSERT_EQUAL(GetFileSize(expected), GetFileSize(actual));
	
	TEST_ASSERT_TRUE(CompareFileContent(expected, actual));
}


void
test_WriteFileWithMultipleEntries(void) {
	kod_db_file = estrdup("kod-output-multiple");
	add_entry("example.com", "RATE");
	add_entry("192.0.2.1", "DENY");
	add_entry("192.0.2.5", "RSTR");

	//
	// Manipulate timestamps. This is a bit of a hack, ideally these
	// tests should not care about the internal representation.
	//
	kod_db[0]->timestamp = 0xabcd;
	kod_db[1]->timestamp = 0xabcd;
	kod_db[2]->timestamp = 0xabcd;

	write_kod_db();

	// Open file and compare sizes and content.
	FILE * actual = fopen(kod_db_file, "rb");
	FILE * expected = fopen(CreatePath("kod-expected-multiple", INPUT_DIR),"rb");

	TEST_ASSERT_NOT_NULL(actual);
	TEST_ASSERT_NOT_NULL(expected);


	TEST_ASSERT_EQUAL(GetFileSize(expected), GetFileSize(actual));

	TEST_ASSERT_TRUE(CompareFileContent(expected, actual));
}
