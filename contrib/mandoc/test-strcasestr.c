#if defined(__linux__) || defined(__MINT__)
# define _GNU_SOURCE /* strcasestr() */
#endif

#include <string.h>

int
main(void)
{
	const char *big = "BigString";
	char *cp = strcasestr(big, "Gst");
	return cp != big + 2;
}
