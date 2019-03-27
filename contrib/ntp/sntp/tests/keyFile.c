#include "config.h"
#include "fileHandlingTest.h"

#include "ntp_stdlib.h"
#include "ntp_types.h"
#include "crypto.h"

#include "unity.h"

bool CompareKeys(struct key expected, struct key actual);
bool CompareKeysAlternative(int key_id,int key_len,const char* type,const char* key_seq,struct key actual);
void test_ReadEmptyKeyFile(void);
void test_ReadASCIIKeys(void);
void test_ReadHexKeys(void);
void test_ReadKeyFileWithComments(void);
void test_ReadKeyFileWithInvalidHex(void);


bool
CompareKeys(
	struct key	expected,
	struct key	actual
	)
{
	if (expected.key_id != actual.key_id) {
		printf("Expected key_id: %d but was: %d\n",
		       expected.key_id, actual.key_id);
		return FALSE;
	}
	if (expected.key_len != actual.key_len) {
		printf("Expected key_len: %d but was: %d\n",
		       expected.key_len, actual.key_len);
		return FALSE;
	}
	if (strcmp(expected.typen, actual.typen) != 0) {
		printf("Expected key_type: %s but was: %s\n",
		       expected.typen, actual.typen);
		return FALSE;

	}
	if (memcmp(expected.key_seq, actual.key_seq, expected.key_len) != 0) {
		printf("Key mismatch!\n");
		return FALSE;		
	}
	return TRUE;
}


bool
CompareKeysAlternative(
	int		key_id,
	int		key_len,
	const char *	type,
	const char *	key_seq,
	struct key	actual
	)
{
	struct key	temp;

	temp.key_id = key_id;
	temp.key_len = key_len;
	strlcpy(temp.typen, type, sizeof(temp.typen));
	memcpy(temp.key_seq, key_seq, key_len);

	return CompareKeys(temp, actual);
}


void
test_ReadEmptyKeyFile(void)
{
	struct key *	keys = NULL;
	const char *	path = CreatePath("key-test-empty", INPUT_DIR);

	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL(0, auth_init(path, &keys));
	TEST_ASSERT_NULL(keys);

	DestroyPath(path);
}


void
test_ReadASCIIKeys(void)
{
	struct key *	keys = NULL;
	struct key *	result = NULL;
	const char *	path = CreatePath("key-test-ascii", INPUT_DIR);

	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL(2, auth_init(path, &keys));
	TEST_ASSERT_NOT_NULL(keys);

	DestroyPath(path);

	get_key(40, &result);
	TEST_ASSERT_NOT_NULL(result);
	TEST_ASSERT_TRUE(CompareKeysAlternative(40, 11, "MD5", "asciikeyTwo", *result));

	result = NULL;
	get_key(50, &result);
	TEST_ASSERT_NOT_NULL(result);
	TEST_ASSERT_TRUE(CompareKeysAlternative(50, 11, "MD5", "asciikeyOne", *result));
}


void
test_ReadHexKeys(void)
{
	struct key *	keys = NULL;
	struct key *	result = NULL;
	const char *	path = CreatePath("key-test-hex", INPUT_DIR);
	char 		data1[15];
	char 		data2[13];

	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL(3, auth_init(path, &keys));
	TEST_ASSERT_NOT_NULL(keys);
	DestroyPath(path);

	get_key(10, &result);
	TEST_ASSERT_NOT_NULL(result);
	TEST_ASSERT_TRUE(CompareKeysAlternative(10, 13, "MD5",
		 "\x01\x23\x45\x67\x89\xab\xcd\xef\x01\x23\x45\x67\x89", *result));

	result = NULL;
	get_key(20, &result);
	TEST_ASSERT_NOT_NULL(result);

	memset(data1, 0x11, 15);
	TEST_ASSERT_TRUE(CompareKeysAlternative(20, 15, "MD5", data1, *result));

	result = NULL;
	get_key(30, &result);
	TEST_ASSERT_NOT_NULL(result);

	memset(data2, 0x01, 13);
	TEST_ASSERT_TRUE(CompareKeysAlternative(30, 13, "MD5", data2, *result));
}


void
test_ReadKeyFileWithComments(void)
{
	struct key *	keys = NULL;
	struct key *	result = NULL;
	const char *	path = CreatePath("key-test-comments", INPUT_DIR);
	char 		data[15];

	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL(2, auth_init(path, &keys));
	TEST_ASSERT_NOT_NULL(keys);
	DestroyPath(path);

	get_key(10, &result);
	TEST_ASSERT_NOT_NULL(result);

	memset(data, 0x01, 15);
	TEST_ASSERT_TRUE(CompareKeysAlternative(10, 15, "MD5", data, *result));

	result = NULL;
	get_key(34, &result);
	TEST_ASSERT_NOT_NULL(result);
	TEST_ASSERT_TRUE(CompareKeysAlternative(34, 3, "MD5", "xyz", *result));
}


void
test_ReadKeyFileWithInvalidHex(void)
{
	struct key *	keys = NULL;
	struct key *	result = NULL;
	const char *	path = CreatePath("key-test-invalid-hex", INPUT_DIR);
	char 		data[15];

	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL(1, auth_init(path, &keys));
	TEST_ASSERT_NOT_NULL(keys);
	DestroyPath(path);

	get_key(10, &result);
	TEST_ASSERT_NOT_NULL(result);

	memset(data, 0x01, 15);
	TEST_ASSERT_TRUE(CompareKeysAlternative(10, 15, "MD5", data, *result));

	result = NULL;
	get_key(30, &result); /* Should not exist, and result should remain NULL. */
	TEST_ASSERT_NULL(result);
}
