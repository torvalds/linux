/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2008 Denys Vlasenko.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "autoconf.h"

/* Since we can't use platform.h, have to do this again by hand: */
#if ENABLE_NOMMU
# define BB_MMU 0
# define USE_FOR_NOMMU(...) __VA_ARGS__
# define USE_FOR_MMU(...)
#else
# define BB_MMU 1
# define USE_FOR_NOMMU(...)
# define USE_FOR_MMU(...) __VA_ARGS__
#endif

#include "usage.h"
#define MAKE_USAGE(aname, usage) { aname, usage },
static struct usage_data {
	const char *aname;
	const char *usage;
} usage_array[] = {
#include "applets.h"
};

static int compare_func(const void *a, const void *b)
{
	const struct usage_data *ua = a;
	const struct usage_data *ub = b;
	return strcmp(ua->aname, ub->aname);
}

int main(void)
{
	int i;
	int num_messages = sizeof(usage_array) / sizeof(usage_array[0]);

	if (num_messages == 0)
		return 0;

	qsort(usage_array,
		num_messages, sizeof(usage_array[0]),
		compare_func);
	for (i = 0; i < num_messages; i++)
		write(STDOUT_FILENO, usage_array[i].usage, strlen(usage_array[i].usage) + 1);

	return 0;
}
