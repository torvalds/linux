#include <sys/types.h>
#include <dirent.h>

int
main(void)
{
	struct dirent	 entry;

	return sizeof(entry.d_namlen) == 0;
}
