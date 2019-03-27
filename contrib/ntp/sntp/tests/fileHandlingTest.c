
#include "config.h"
#include "stdlib.h"
#include "sntptest.h"

#include "fileHandlingTest.h" /* required because of the h.in thingy */

#include <string.h>
#include <unistd.h>

const char *
CreatePath(
	const char *		filename,
	enum DirectoryType 	argument
	)
{
	const char 	srcdir[] = SRCDIR_DEF;//"@abs_srcdir@/data/";
	size_t		plen = sizeof(srcdir) + strlen(filename) + 1;
	char * 		path = emalloc(plen);
	ssize_t		retc;

	UNUSED_ARG(argument);

	retc = snprintf(path, plen, "%s%s", srcdir, filename);
	if (retc <= 0 || (size_t)retc >= plen)
		exit(1);
	return path;
}


void
DestroyPath(
	const char *	pathname
	)
{
	/* use a union to get terminally rid of the 'const' attribute */
	union {
		const char *ccp;
		void       *vp;
	} any;

	any.ccp = pathname;
	free(any.vp);
}


int
GetFileSize(
	FILE *	file
	)
{
	fseek(file, 0L, SEEK_END);
	int length = ftell(file);
	fseek(file, 0L, SEEK_SET);

	return length;
}


bool
CompareFileContent(
	FILE *	expected,
	FILE *	actual
	)
{
	int currentLine = 1;

	char actualLine[1024];
	char expectedLine[1024];
	size_t lenAct = sizeof actualLine;
	size_t lenExp = sizeof expectedLine;
	
	while (  ( (fgets(actualLine, lenAct, actual)) != NULL)
	      && ( (fgets(expectedLine, lenExp, expected)) != NULL )
	      ) {

	
		if( strcmp(actualLine,expectedLine) !=0 ){
			printf("Comparision failed on line %d",currentLine);
			return FALSE;
		}

		currentLine++;
	}

	return TRUE;
}


void
ClearFile(
	const char * filename
	)
{
	if (!truncate(filename, 0))
		exit(1);
}
