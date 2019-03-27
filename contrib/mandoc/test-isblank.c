#include <ctype.h>

int
main(void)
{
	return !isblank(' ') || !isblank('\t') || isblank('_');
}
