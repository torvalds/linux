/*
 * POSIX allows PATH_MAX to not be defined, see
 * http://pubs.opengroup.org/onlinepubs/9699919799/functions/sysconf.html;
 * the GNU Hurd is an example of a system not having it.
 *
 * Arguably, it would be better to test sysconf(_SC_PATH_MAX),
 * but since the individual *.c files include "config.h" before
 * <limits.h>, overriding an excessive value of PATH_MAX from
 * "config.h" is impossible anyway, so for now, the simplest
 * fix is to provide a value only on systems not having any.
 * So far, we encountered no system defining PATH_MAX to an
 * impractically large value, even though POSIX explicitly
 * allows that.
 *
 * The real fix would be to replace all static buffers of size
 * PATH_MAX by dynamically allocated buffers.  But that is
 * somewhat intrusive because it touches several files and
 * because it requires changing struct mlink in mandocdb.c.
 * So i'm postponing that for now.
 */

#include <limits.h>
#include <stdio.h>

int
main(void)
{
	printf("PATH_MAX is defined to be %ld\n", (long)PATH_MAX);
	return 0;
}
