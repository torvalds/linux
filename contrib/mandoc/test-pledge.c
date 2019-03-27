#include <unistd.h>

int
main(void)
{
	return !!pledge("stdio", NULL);
}
