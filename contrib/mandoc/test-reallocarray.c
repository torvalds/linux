#include <stdlib.h>

int
main(void)
{
	return !reallocarray(NULL, 2, 2);
}
