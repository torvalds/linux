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
	void *i_data;

	/* output */
	int ret;
	int result;
	void *o_data;
	u32 o_data_len;

	/* input */
	/* Must be last -ends in a flexible-array member. */
	struct cros_ec_command msg;
};

extern int cros_kunit_ec_xfer_mock_default_result;
extern int cros_kunit_ec_xfer_mock_default_ret;
extern int cros_kunit_ec_cmd_xfer_mock_called;
extern int cros_kunit_ec_pkt_xfer_mock_called;

int cros_kunit_ec_xfer_mock(struct cros_ec_device *ec_dev, struct cros_ec_command *msg);
int cros_kunit_ec_cmd_xfer_mock(struct cros_ec_device *ec_dev, struct cros_ec_command *msg);
int cros_kunit_ec_pkt_xfer_mock(struct cros_ec_device *ec_dev, struct cros_ec_command *msg);
struct ec_xfer_mock *cros_kunit_ec_xfer_mock_add(struct kunit *test, size_t size);
struct ec_xfer_mock *cros_kunit_ec_xfer_mock_addx(struct kunit *test,
						  int ret, int result, size_t size);
struct ec_xfer_mock *cros_kunit_ec_xfer_mock_next(void);

extern int cros_kunit_readmem_mock_offset;
extern u8 *cros_kunit_readmem_mock_data;
extern int cros_kunit_readmem_mock_ret;

int cros_kunit_readmem_mock(struct cros_ec_device *ec_dev, unsigned int offset,
			    unsigned int bytes, void *dest);

void cros_kunit_mock_reset(void);

#endif
