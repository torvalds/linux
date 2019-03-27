#include <string.h>

int
main(void)
{
	char buf[6] = "aybxc";
	char *workp = buf;
	char *retp = strsep(&workp, "xy");
	return ! (retp == buf && buf[1] == '\0' && workp == buf + 2);
}
