// SPDX-License-Identifier: GPL-2.0+

#include <linux/export.h>

#include "test_modules.h"

#define DEFINE_RETURN(i) \
	int test_modules_return_ ## i(void) \
	{ \
		return 1 ## i - 10000; \
	} \
	EXPORT_SYMBOL_GPL(test_modules_return_ ## i)
REPEAT_10000(DEFINE_RETURN);
