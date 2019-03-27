#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

extern char *__progname;

const char *
getprogname(void)
{
	return __progname;
}

void
setprogname(char *p)
{
	char *q;
	if (p == NULL)
		return;
	if ((q = strrchr(p, '/')) != NULL)
		__progname = ++q;
	else
		__progname = p;
}
