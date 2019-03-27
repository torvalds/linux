#include <stdio.h>
#include <time.h>

int
main(void)
{
	struct timespec	 timeout;

	timeout.tv_sec = 0;
	timeout.tv_nsec = 100000000;	/* 0.1 seconds */
	
	if (nanosleep(&timeout, NULL)) {
		perror("nanosleep");
		return 1;
	}
	return 0;
}
