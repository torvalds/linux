/*
 * Callers outside of misc.c need access to the error reporting routines,
 * but the *_putstr() functions need to stay in misc.c because of how
 * memcpy() and memmove() are defined for the compressed boot environment.
 */
#include "misc.h"
#include "error.h"

void warn(char *m)
{
	error_putstr("\n\n");
	error_putstr(m);
	error_putstr("\n\n");
}

void error(char *m)
{
	warn(m);
	error_putstr(" -- System halted");

	while (1)
		asm("hlt");
}
