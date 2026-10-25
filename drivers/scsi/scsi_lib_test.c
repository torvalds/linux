// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for scsi_lib.c.
 *
 * Copyright (C) 2023, Oracle Corporation
 */
#include <kunit/test.h>

#include <scsi/scsi_proto.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>

#define SCSI_LIB_TEST_MAX_ALLOWED 3
#define SCSI_LIB_TEST_TOTAL_MAX_ALLOWED 5

static void scsi_lib_test_multiple_sense(struct kunit *test)
{
	struct scsi_failure multiple_sense_failure_defs[] = {
		{
			.sense_key = DATA_PROTECT,
			.sense_code =
				scsi_sense_code(ASC_NO_INDEX_SECTOR_SIGNAL,
						0x1),
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{
			.sense_key = UNIT_ATTENTION,
			.sense_code = UNRECOVERED_READ_ERROR,
			.allowed = SCSI_LIB_TEST_MAX_ALLOWED,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{
			.sense_key = NOT_READY,
			.sense_code =
				scsi_sense_code(ASC_UNRECOVERED_READ_ERROR,
						0x22),
			.allowed = SCSI_LIB_TEST_MAX_ALLOWED,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{
			.sense_key = ABORTED_COMMAND,
			.sense_code =
				scsi_sense_code(ASC_UNRECOVERED_READ_ERROR,
						SCMD_FAILURE_ASCQ_ANY),
			.allowed = SCSI_LIB_TEST_MAX_ALLOWED,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{
			.sense_key = HARDWARE_ERROR,
			.sense_code = scsi_sense_code(SCMD_FAILURE_ASC_ANY, 0),
			.allowed = SCSI_LIB_TEST_MAX_ALLOWED,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{
			.sense_key = ILLEGAL_REQUEST,
			.sense_code = scsi_sense_code(0x91, 0x36),
			.allowed = SCSI_LIB_TEST_MAX_ALLOWED,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{}
	};
	struct scsi_failures failures = {
		.failure_definitions = multiple_sense_failure_defs,
	};
	u8 sense[SCSI_SENSE_BUFFERSIZE] = {};
	struct scsi_cmnd sc = {
		.sense_buffer = sense,
	};
	int i;

	/* Success */
	sc.result = 0;
	KUNIT_EXPECT_EQ(test, 0, scsi_check_passthrough(&sc, &failures));
	KUNIT_EXPECT_EQ(test, 0, scsi_check_passthrough(&sc, NULL));
	/* Command failed but caller did not pass in a failures array */
	scsi_set_sense(&sc, 0, ILLEGAL_REQUEST, scsi_sense_code(0x91, 0x36));
	KUNIT_EXPECT_EQ(test, 0, scsi_check_passthrough(&sc, NULL));
	/* Match end of array */
	scsi_set_sense(&sc, 0, ILLEGAL_REQUEST, scsi_sense_code(0x91, 0x36));
	KUNIT_EXPECT_EQ(test, -EAGAIN, scsi_check_passthrough(&sc, &failures));
	/* Basic match in array */
	scsi_set_sense(&sc, 0, UNIT_ATTENTION, UNRECOVERED_READ_ERROR);
	KUNIT_EXPECT_EQ(test, -EAGAIN, scsi_check_passthrough(&sc, &failures));
	/* No matching sense entry */
	scsi_set_sense(&sc, 0, MISCOMPARE, READ_ERROR_LOSS_OF_STREAMING);
	KUNIT_EXPECT_EQ(test, 0, scsi_check_passthrough(&sc, &failures));
	/* Match using SCMD_FAILURE_ASCQ_ANY */
	scsi_set_sense(&sc, 0, ABORTED_COMMAND,
		       scsi_sense_code(ASC_UNRECOVERED_READ_ERROR, 0x22));
	KUNIT_EXPECT_EQ(test, -EAGAIN, scsi_check_passthrough(&sc, &failures));
	/* Fail to match */
	scsi_set_sense(&sc, 0, ABORTED_COMMAND,
		       scsi_sense_code(ASC_ILLEGAL_FUNCTION, 0x22));
	KUNIT_EXPECT_EQ(test, 0, scsi_check_passthrough(&sc, &failures));
	/* Match using SCMD_FAILURE_ASC_ANY */
	scsi_set_sense(&sc, 0, HARDWARE_ERROR,
		       scsi_sense_code(ASC_UNRECOVERED_READ_ERROR, 0x22));
	KUNIT_EXPECT_EQ(test, -EAGAIN, scsi_check_passthrough(&sc, &failures));
	/* No matching status entry */
	sc.result = SAM_STAT_RESERVATION_CONFLICT;
	KUNIT_EXPECT_EQ(test, 0, scsi_check_passthrough(&sc, &failures));

	/* Test hitting allowed limit */
	scsi_set_sense(&sc, 0, NOT_READY,
		       scsi_sense_code(ASC_UNRECOVERED_READ_ERROR, 0x22));
	for (i = 0; i < SCSI_LIB_TEST_MAX_ALLOWED; i++)
		KUNIT_EXPECT_EQ(test, -EAGAIN, scsi_check_passthrough(&sc,
				&failures));
	KUNIT_EXPECT_EQ(test, 0, scsi_check_passthrough(&sc, &failures));

	/* reset retries so we can retest */
	failures.failure_definitions = multiple_sense_failure_defs;
	scsi_failures_reset_retries(&failures);

	/* Test no retries allowed */
	scsi_set_sense(&sc, 0, DATA_PROTECT,
		       scsi_sense_code(ASC_NO_INDEX_SECTOR_SIGNAL, 0x1));
	KUNIT_EXPECT_EQ(test, 0, scsi_check_passthrough(&sc, &failures));
}

static void scsi_lib_test_any_sense(struct kunit *test)
{
	struct scsi_failure any_sense_failure_defs[] = {
		{
			.sense_key = SCMD_FAILURE_SENSE_KEY_ANY,
			.allowed = SCSI_LIB_TEST_MAX_ALLOWED,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{}
	};
	struct scsi_failures failures = {
		.failure_definitions = any_sense_failure_defs,
	};
	u8 sense[SCSI_SENSE_BUFFERSIZE] = {};
	struct scsi_cmnd sc = {
		.sense_buffer = sense,
	};

	/* Match using SCMD_FAILURE_SENSE_KEY_ANY */
	failures.failure_definitions = any_sense_failure_defs;
	scsi_set_sense(&sc, 0, MEDIUM_ERROR,
		       scsi_sense_code(ASC_UNRECOVERED_READ_ERROR, 0x22));
	KUNIT_EXPECT_EQ(test, -EAGAIN, scsi_check_passthrough(&sc, &failures));
}

static void scsi_lib_test_host(struct kunit *test)
{
	struct scsi_failure retryable_host_failure_defs[] = {
		{
			.result = DID_TRANSPORT_DISRUPTED << 16,
			.allowed = SCSI_LIB_TEST_MAX_ALLOWED,
		},
		{
			.result = DID_TIME_OUT << 16,
			.allowed = SCSI_LIB_TEST_MAX_ALLOWED,
		},
		{}
	};
	struct scsi_failures failures = {
		.failure_definitions = retryable_host_failure_defs,
	};
	u8 sense[SCSI_SENSE_BUFFERSIZE] = {};
	struct scsi_cmnd sc = {
		.sense_buffer = sense,
	};

	/* No matching host byte entry */
	failures.failure_definitions = retryable_host_failure_defs;
	sc.result = DID_NO_CONNECT << 16;
	KUNIT_EXPECT_EQ(test, 0, scsi_check_passthrough(&sc, &failures));
	/* Matching host byte entry */
	sc.result = DID_TIME_OUT << 16;
	KUNIT_EXPECT_EQ(test, -EAGAIN, scsi_check_passthrough(&sc, &failures));
}

static void scsi_lib_test_any_failure(struct kunit *test)
{
	struct scsi_failure any_failure_defs[] = {
		{
			.result = SCMD_FAILURE_RESULT_ANY,
			.allowed = SCSI_LIB_TEST_MAX_ALLOWED,
		},
		{}
	};
	struct scsi_failures failures = {
		.failure_definitions = any_failure_defs,
	};
	u8 sense[SCSI_SENSE_BUFFERSIZE] = {};
	struct scsi_cmnd sc = {
		.sense_buffer = sense,
	};

	/* Match SCMD_FAILURE_RESULT_ANY */
	failures.failure_definitions = any_failure_defs;
	sc.result = DID_TRANSPORT_FAILFAST << 16;
	KUNIT_EXPECT_EQ(test, -EAGAIN, scsi_check_passthrough(&sc, &failures));
}

static void scsi_lib_test_any_status(struct kunit *test)
{
	struct scsi_failure any_status_failure_defs[] = {
		{
			.result = SCMD_FAILURE_STAT_ANY,
			.allowed = SCSI_LIB_TEST_MAX_ALLOWED,
		},
		{}
	};
	struct scsi_failures failures = {
		.failure_definitions = any_status_failure_defs,
	};
	u8 sense[SCSI_SENSE_BUFFERSIZE] = {};
	struct scsi_cmnd sc = {
		.sense_buffer = sense,
	};

	/* Test any status handling */
	failures.failure_definitions = any_status_failure_defs;
	sc.result = SAM_STAT_RESERVATION_CONFLICT;
	KUNIT_EXPECT_EQ(test, -EAGAIN, scsi_check_passthrough(&sc, &failures));
}

static void scsi_lib_test_total_allowed(struct kunit *test)
{
	struct scsi_failure total_allowed_defs[] = {
		{
			.sense_key = UNIT_ATTENTION,
			.sense_code = SCMD_FAILURE_SENSE_CODE_ANY,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		/* Fail all CCs except the UA above */
		{
			.sense_key = SCMD_FAILURE_SENSE_KEY_ANY,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		/* Retry any other errors not listed above */
		{
			.result = SCMD_FAILURE_RESULT_ANY,
		},
		{}
	};
	struct scsi_failures failures = {
		.failure_definitions = total_allowed_defs,
	};
	u8 sense[SCSI_SENSE_BUFFERSIZE] = {};
	struct scsi_cmnd sc = {
		.sense_buffer = sense,
	};
	int i;

	/* Test total_allowed */
	failures.failure_definitions = total_allowed_defs;
	scsi_failures_reset_retries(&failures);
	failures.total_allowed = SCSI_LIB_TEST_TOTAL_MAX_ALLOWED;

	scsi_set_sense(&sc, 0, UNIT_ATTENTION,
		       NOT_READY_TO_READY_CHANGE_MEDIUM_MAY_HAVE_CHANGED);
	for (i = 0; i < SCSI_LIB_TEST_TOTAL_MAX_ALLOWED; i++)
		/* Retry since we under the total_allowed limit */
		KUNIT_EXPECT_EQ(test, -EAGAIN, scsi_check_passthrough(&sc,
				&failures));
	sc.result = DID_TIME_OUT << 16;
	/* We have now hit the total_allowed limit so no more retries */
	KUNIT_EXPECT_EQ(test, 0, scsi_check_passthrough(&sc, &failures));
}

static void scsi_lib_test_mixed_total(struct kunit *test)
{
	struct scsi_failure mixed_total_defs[] = {
		{
			.sense_key = UNIT_ATTENTION,
			.sense_code =
				NOT_READY_TO_READY_CHANGE_MEDIUM_MAY_HAVE_CHANGED,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{
			.sense_key = UNIT_ATTENTION,
			.sense_code =
				POWER_ON_RESET_OR_BUS_DEVICE_RESET_OCCURRED,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{
			.allowed = 1,
			.result = DID_TIME_OUT << 16,
		},
		{}
	};
	u8 sense[SCSI_SENSE_BUFFERSIZE] = {};
	struct scsi_failures failures = {
		.failure_definitions = mixed_total_defs,
	};
	struct scsi_cmnd sc = {
		.sense_buffer = sense,
	};
	int i;

	/*
	 * Test total_allowed when there is a mix of per failure allowed
	 * and total_allowed limits.
	 */
	failures.failure_definitions = mixed_total_defs;
	scsi_failures_reset_retries(&failures);
	failures.total_allowed = SCSI_LIB_TEST_TOTAL_MAX_ALLOWED;

	scsi_set_sense(&sc, 0, UNIT_ATTENTION,
		       NOT_READY_TO_READY_CHANGE_MEDIUM_MAY_HAVE_CHANGED);
	for (i = 0; i < SCSI_LIB_TEST_TOTAL_MAX_ALLOWED; i++)
		/* Retry since we under the total_allowed limit */
		KUNIT_EXPECT_EQ(test, -EAGAIN, scsi_check_passthrough(&sc,
				&failures));
	/* Do not retry since we are now over total_allowed limit */
	KUNIT_EXPECT_EQ(test, 0, scsi_check_passthrough(&sc, &failures));

	scsi_failures_reset_retries(&failures);
	scsi_set_sense(&sc, 0, UNIT_ATTENTION,
		       NOT_READY_TO_READY_CHANGE_MEDIUM_MAY_HAVE_CHANGED);
	for (i = 0; i < SCSI_LIB_TEST_TOTAL_MAX_ALLOWED; i++)
		/* Retry since we under the total_allowed limit */
		KUNIT_EXPECT_EQ(test, -EAGAIN, scsi_check_passthrough(&sc,
				&failures));
	sc.result = DID_TIME_OUT << 16;
	/* Retry because this failure has a per failure limit */
	KUNIT_EXPECT_EQ(test, -EAGAIN, scsi_check_passthrough(&sc, &failures));
	scsi_set_sense(&sc, 0, UNIT_ATTENTION,
		       POWER_ON_RESET_OR_BUS_DEVICE_RESET_OCCURRED);
	/* total_allowed is now hit so no more retries */
	KUNIT_EXPECT_EQ(test, 0, scsi_check_passthrough(&sc, &failures));
}

static void scsi_lib_test_check_passthough(struct kunit *test)
{
	scsi_lib_test_multiple_sense(test);
	scsi_lib_test_any_sense(test);
	scsi_lib_test_host(test);
	scsi_lib_test_any_failure(test);
	scsi_lib_test_any_status(test);
	scsi_lib_test_total_allowed(test);
	scsi_lib_test_mixed_total(test);
}

static struct kunit_case scsi_lib_test_cases[] = {
	KUNIT_CASE(scsi_lib_test_check_passthough),
	{}
};

static struct kunit_suite scsi_lib_test_suite = {
	.name = "scsi_lib",
	.test_cases = scsi_lib_test_cases,
};

kunit_test_suite(scsi_lib_test_suite);
