#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>

int
main(void)
{
	char	*line = NULL;
	size_t	 linesz = 0;

	fclose(stdin);
	return getline(&line, &linesz, stdin) != -1;
}
