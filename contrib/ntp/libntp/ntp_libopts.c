/*
 * ntp_libopts.c
 *
 * Common code interfacing with Autogen's libopts command-line option
 * processing.
 */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stddef.h>
#include "ntp_libopts.h"
#include "ntp_stdlib.h"

extern const char *Version;	/* version.c for each program */


/*
 * ntpOptionProcess() was a clone of libopts' optionProcess which
 * overrode the --version output, appending detail from version.c
 * which was not available at Autogen time.  This is now done via
 * AutoOpts' version-proc = override in copyright.def, so this
 * routine is a straightforward wrapper of optionProcess().
 */
int
ntpOptionProcess(
	tOptions *	pOpts,
	int		argc,
	char **		argv
	)
{
	return optionProcess(pOpts, argc, argv);
}


/*
 * ntpOptionPrintVersion() replaces the stock optionPrintVersion() via
 * version-proc = ntpOptionPrintVersion; in copyright.def.  It differs
 * from the stock function by displaying the complete version string,
 * including compile time which was unknown when Autogen ran.
 *
 * Like optionPrintVersion() this function must exit(0) rather than
 * return.
 */
void
ntpOptionPrintVersion(
	tOptions *	pOpts,
	tOptDesc *	pOD
	)
{
	UNUSED_ARG(pOpts);
	UNUSED_ARG(pOD);

	printf("%s\n", Version);
	fflush(stdout);
	exit(EXIT_SUCCESS);
}
