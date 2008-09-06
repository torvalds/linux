/*
 * Strings for the various x86 power flags
 *
 * This file must not contain any executable code.
 */

#include <asm/cpufeature.h>

const char *const x86_power_flags[32] = {
	"ts",	/* temperature sensor */
	"fid",  /* frequency id control */
	"vid",  /* voltage id control */
	"ttp",  /* thermal trip */
	"tm",
	"stc",
	"100mhzsteps",
	"hwpstate",
	"",	/* tsc invariant mapped to constant_tsc */
		/* nothing */
};
