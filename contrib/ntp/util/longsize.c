#include <stdio.h>

main()
{
	if (sizeof(long) == 8) { 
		printf("-DLONG8\n");
	} else if (sizeof(long) == 4) {
		printf("-DLONG4\n");
	}
	exit(0);
}
