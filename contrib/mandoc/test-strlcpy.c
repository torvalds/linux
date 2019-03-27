#include <string.h>

int
main(void)
{
	char buf[2] = "";
	return ! (strlcpy(buf, "a", sizeof(buf)) == 1 &&
	    buf[0] == 'a' && buf[1] == '\0');
}
