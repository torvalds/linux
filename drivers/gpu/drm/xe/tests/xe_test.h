/* SPDX-License-Identifier: GPL-2.0 AND MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_TEST_H_
#define _XE_TEST_H_

#include <linux/types.h>

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
#include <linux/sched.h>
#include <kunit/test.h>

/*
 * Each test that provides a kunit private test structure, place a test id
 * here and point the kunit->priv to an embedded struct xe_test_priv.
 */
enum xe_test_priv_id {
	XE_TEST_LIVE_DMA_BUF,
	XE_TEST_LIVE_MIGRATE,
};

/**
 * struct xe_test_priv - Base class for test private info
 * @id: enum xe_test_priv_id to identify the subclass.
 */
struct xe_test_priv {
	enum xe_test_priv_id id;
};

#define XE_TEST_DECLARE(x) x
#define XE_TEST_ONLY(x) unlikely(x)
#define XE_TEST_EXPORT
#define xe_cur_kunit() current->kunit_test

/**
 * xe_cur_kunit_priv - Obtain the struct xe_test_priv pointed to by
 * current->kunit->priv if it exists and is embedded in the expected subclass.
 * @id: Id of the expected subclass.
 *
 * Return: NULL if the process is not a kunit test, and NULL if the
 * current kunit->priv pointer is not pointing to an object of the expected
 * subclass. A pointer to the embedded struct xe_test_priv otherwise.
 */
static inline struct xe_test_priv *
xe_cur_kunit_priv(enum xe_test_priv_id id)
{
	struct xe_test_priv *priv;

	if (!xe_cur_kunit())
		return NULL;

	priv = xe_cur_kunit()->priv;
	return priv->id == id ? priv : NULL;
}

#else /* if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST) */

#define XE_TEST_DECLARE(x)
#define XE_TEST_ONLY(x) 0
#define XE_TEST_EXPORT static
#define xe_cur_kunit() NULL
#define xe_cur_kunit_priv(_id) NULL

#endif
#endif
