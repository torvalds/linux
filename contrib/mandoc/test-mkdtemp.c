#include <stdlib.h>
#include <unistd.h>

int
main(void)
{
	char	dirname[] = "/tmp/temp.XXXXXX";

	if (mkdtemp(dirname) != dirname)
		return 1;
	return rmdir(dirname) == -1;
}
