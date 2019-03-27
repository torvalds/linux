/* Just a replacement, if the original isblank is not
   present */

#if HAVE_CONFIG_H
#include <ldns/config.h>
#endif

int isblank(int c);

/* true if character is a blank (space or tab). C99. */
int
isblank(int c)
{
	return (c == ' ') || (c == '\t');
}
