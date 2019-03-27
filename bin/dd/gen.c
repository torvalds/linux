/*-
 * This program is in the public domain
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <string.h>

int
main(int argc, char **argv)
{
	int i;

	if (argc > 1 && !strcmp(argv[1], "189284")) {
		fputs("ABCDEFGH", stdout);
		for (i = 0; i < 8; i++)
			putchar(0);
	} else {
		for (i = 0; i < 256; i++)
			putchar(i);
	}
	return (0);
}
