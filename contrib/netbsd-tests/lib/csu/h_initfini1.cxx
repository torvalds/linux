#include <unistd.h>

int
main(void)
{
	static const char msg[] = "main executed\n";
	write(STDOUT_FILENO, msg, sizeof(msg) - 1);
	return 0;
}
