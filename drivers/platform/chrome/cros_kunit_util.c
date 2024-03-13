// SPDX-License-Identifier: GPL-2.0
/*
 * CrOS Kunit tests utilities.
 */

#include <kunit/test.h>

#include <linux/list.h>
#include <linux/minmax.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>

#include "cros_ec.h"
#include "cros_kunit_util.h"

int cros_kunit_ec_xfer_mock_default_result;
int cros_kunit_ec_xfer_mock_default_ret;
int cros_kunit_ec_cmd_xfer_mock_called;
int cros_kunit_ec_pkt_xfer_mock_called;

static struct list_head cros_kunit_ec_xfer_mock_in;
static struct list_head cros_kunit_ec_xfer_mock_out;

int cros_kunit_ec_xfer_mock(struct cros_ec_device *ec_dev, struct cros_ec_command *msg)
{
	struct ec_xfer_mock *mock;

	mock = list_first_entry_or_null(&cros_kunit_ec_xfer_mock_in, struct ec_xfer_mock, list);
	if (!mock) {
		msg->result = cros_kunit_ec_xfer_mock_default_result;
		return cros_kunit_ec_xfer_mock_default_ret;
	}

	list_del(&mock->list);

	memcpy(&mock->msg, msg, sizeof(*msg));
	if (msg->outsize) {
		mock->i_data = kunit_kzalloc(mock->test, msg->outsize, GFP_KERNEL);
		if (mock->i_data)
			memcpy(mock->i_data, msg->data, msg->outsize);
	}

	msg->result = mock->result;
	if (msg->insize)
		memcpy(msg->data, mock->o_data, min(msg->insize, mock->o_data_len));

	list_add_tail(&mock->list, &cros_kunit_ec_xfer_mock_out);

	return mock->ret;
}

int cros_kunit_ec_cmd_xfer_mock(struct cros_ec_device *ec_dev, struct cros_ec_command *msg)
{
	++cros_kunit_ec_cmd_xfer_mock_called;
	return cros_kunit_ec_xfer_mock(ec_dev, msg);
}

int cros_kunit_ec_pkt_xfer_mock(struct cros_ec_device *ec_dev, struct cros_ec_command *msg)
{
	++cros_kunit_ec_pkt_xfer_mock_called;
	return cros_kunit_ec_xfer_mock(ec_dev, msg);
}

struct ec_xfer_mock *cros_kunit_ec_xfer_mock_add(struct kunit *test, size_t size)
{
	return cros_kunit_ec_xfer_mock_addx(test, size, EC_RES_SUCCESS, size);
}

struct ec_xfer_mock *cros_kunit_ec_xfer_mock_addx(struct kunit *test,
						  int ret, int result, size_t size)
{
	struct ec_xfer_mock *mock;

	mock = kunit_kzalloc(test, sizeof(*mock), GFP_KERNEL);
	if (!mock)
		return NULL;

	list_add_tail(&mock->list, &cros_kunit_ec_xfer_mock_in);
	mock->test = test;

	mock->ret = ret;
	mock->result = result;
	mock->o_data = kunit_kzalloc(test, size, GFP_KERNEL);
	if (!mock->o_data)
		return NULL;
	mock->o_data_len = size;

	return mock;
}

struct ec_xfer_mock *cros_kunit_ec_xfer_mock_next(void)
{
	struct ec_xfer_mock *mock;

	mock = list_first_entry_or_null(&cros_kunit_ec_xfer_mock_out, struct ec_xfer_mock, list);
	if (mock)
		list_del(&mock->list);

	return mock;
}

int cros_kunit_readmem_mock_offset;
u8 *cros_kunit_readmem_mock_data;
int cros_kunit_readmem_mock_ret;

int cros_kunit_readmem_mock(struct cros_ec_device *ec_dev, unsigned int offset,
			    unsigned int bytes, void *dest)
{
	cros_kunit_readmem_mock_offset = offset;

	memcpy(dest, cros_kunit_readmem_mock_data, bytes);

	return cros_kunit_readmem_mock_ret;
}

void cros_kunit_mock_reset(void)
{
	cros_kunit_ec_xfer_mock_default_result = 0;
	cros_kunit_ec_xfer_mock_default_ret = 0;
	cros_kunit_ec_cmd_xfer_mock_called = 0;
	cros_kunit_ec_pkt_xfer_mock_called = 0;
	INIT_LIST_HEAD(&cros_kunit_ec_xfer_mock_in);
	INIT_LIST_HEAD(&cros_kunit_ec_xfer_mock_out);

	cros_kunit_readmem_mock_offset = 0;
	cros_kunit_readmem_mock_data = NULL;
	cros_kunit_readmem_mock_ret = 0;
}

MODULE_LICENSE("GPL");
