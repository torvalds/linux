/* vi set: sw=4 ts=4: */
/* Convert string str to lowercase, return str.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

char* FAST_FUNC str_tolower(char *str)
{
	char *c;
	for (c = str; *c; ++c)
		*c = tolower(*c);
	return str;
}
