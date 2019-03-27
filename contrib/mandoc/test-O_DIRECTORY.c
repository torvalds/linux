#include <fcntl.h>

int
main(void)
{
	return open(".", O_RDONLY | O_DIRECTORY) == -1;
}
