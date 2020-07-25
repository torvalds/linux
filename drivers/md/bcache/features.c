// SPDX-License-Identifier: GPL-2.0
/*
 * Feature set bits and string conversion.
 * Inspired by ext4's features compat/incompat/ro_compat related code.
 *
 * Copyright 2020 Coly Li <colyli@suse.de>
 *
 */
#include <linux/bcache.h>
#include "bcache.h"

struct feature {
	int		compat;
	unsigned int	mask;
	const char	*string;
};

static struct feature feature_list[] = {
	{BCH_FEATURE_INCOMPAT, BCH_FEATURE_INCOMPAT_LARGE_BUCKET,
		"large_bucket"},
	{0, 0, 0 },
};
