#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>

int
main(void)
{
	size_t sz;
	fclose(stdin);
	return(NULL != fgetln(stdin, &sz));
}
