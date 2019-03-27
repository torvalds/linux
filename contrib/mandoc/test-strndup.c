#include <string.h>

int
main(void)
{
	char *s;

	s = strndup("123", 2);
	return s[0] != '1' ? 1 : s[1] != '2' ? 2 : s[2] != '\0' ? 3 : 0;
}
