/* SPDX-License-Identifier: GPL-2.0 AND MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_BO_TEST_H_
#define _XE_BO_TEST_H_

struct kunit;

void xe_ccs_migrate_kunit(struct kunit *test);
void xe_bo_evict_kunit(struct kunit *test);

#endif
