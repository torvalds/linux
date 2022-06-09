/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CrOS Kunit tests utilities.
 */

#ifndef _CROS_KUNIT_UTIL_H_
#define _CROS_KUNIT_UTIL_H_

#include <linux/platform_data/cros_ec_proto.h>

struct ec_xfer_mock {
	struct list_head list;
	struct kunit *test;

	/* input */
	struct cros_ec_command msg;
	void *i_data;

	/* output */
	int ret;
	int result;
	void *o_data;
	u32 o_data_len;
};

extern int cros_kunit_ec_xfer_mock_default_ret;

int cros_kunit_ec_xfer_mock(struct cros_ec_device *ec_dev, struct cros_ec_command *msg);
struct ec_xfer_mock *cros_kunit_ec_xfer_mock_add(struct kunit *test, size_t size);
struct ec_xfer_mock *cros_kunit_ec_xfer_mock_addx(struct kunit *test,
						  int ret, int result, size_t size);
struct ec_xfer_mock *cros_kunit_ec_xfer_mock_next(void);

void cros_kunit_mock_reset(void);

#endif
