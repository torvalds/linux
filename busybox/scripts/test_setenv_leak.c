#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv)
{
	char buf[256];

	int i = argv[1] ? atoi(argv[1]) : 999999;
	while (--i > 0) {
		sprintf(buf, "%d", i);
		setenv("VAR", buf, 1);
	}
	printf("Check size of [heap] mapping:\n");
	freopen("/proc/self/maps", "r", stdin);
	while (fgets(buf, sizeof(buf), stdin))
		fputs(buf, stdout);
	return 0;
}
