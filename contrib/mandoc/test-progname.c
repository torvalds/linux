#include <stdlib.h>

int
main(void)
{
	const char * progname;

	progname = getprogname();
	return progname == NULL;
}
