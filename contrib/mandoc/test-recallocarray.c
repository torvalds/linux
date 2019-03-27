#include <stdlib.h>

int
main(void)
{
	void	*p;

	if ((p = calloc(2, 2)) == NULL)
		return 1;
	return !recallocarray(p, 2, 3, 2);
}
