// SPDX-License-Identifier: GPL-2.0

#include <linux/ctype.h>
#include <linux/types.h>

char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}
