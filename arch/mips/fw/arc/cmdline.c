/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * cmdline.c: Kernel command line creation using ARCS argc/argv.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 */
#include <linux/bug.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/sgialib.h>
#include <asm/bootinfo.h>

#undef DEBUG_CMDLINE

/*
 * A 32-bit ARC PROM pass arguments and environment as 32-bit pointer.
 * These macro take care of sign extension.
 */
#define prom_argv(index) ((char *) (long)argv[(index)])

static char *ignored[] = {
	"ConsoleIn=",
	"ConsoleOut=",
	"SystemPartition=",
	"OSLoader=",
	"OSLoadPartition=",
	"OSLoadFilename=",
	"OSLoadOptions="
};

static char *used_arc[][2] = {
	{ "OSLoadPartition=", "root=" },
	{ "OSLoadOptions=", "" }
};

static char __init *move_firmware_args(int argc, LONG *argv, char *cp)
{
	char *s;
	int actr, i;
	size_t len;

	actr = 1; /* Always ignore argv[0] */

	while (actr < argc) {
		for (i = 0; i < ARRAY_SIZE(used_arc); i++) {
			len = strlen(used_arc[i][0]);

			if (!strncmp(prom_argv(actr), used_arc[i][0], len)) {
			/* Ok, we want it. First append the replacement... */
				strcat(cp, used_arc[i][1]);
				cp += strlen(used_arc[i][1]);
				/* ... and now the argument */
				s = strchr(prom_argv(actr), '=');
				if (s) {
					s++;
					len = strlen(s);
					memcpy(cp, s, len + 1);
					cp += len;
				}
				*cp++ = ' ';
				break;
			}
		}
		actr++;
	}

	return cp;
}

void __init prom_init_cmdline(int argc, LONG *argv)
{
	char *cp;
	int actr, i;
	size_t len;

	actr = 1; /* Always ignore argv[0] */

	cp = arcs_cmdline;
	/*
	 * Move ARC variables to the beginning to make sure they can be
	 * overridden by later arguments.
	 */
	cp = move_firmware_args(argc, argv, cp);

	while (actr < argc) {
		for (i = 0; i < ARRAY_SIZE(ignored); i++) {
			len = strlen(ignored[i]);
			if (!strncmp(prom_argv(actr), ignored[i], len))
				goto pic_cont;
		}

		/* Ok, we want it. */
		len = strlen(prom_argv(actr));
		memcpy(cp, prom_argv(actr), len + 1);
		cp += len;
		*cp++ = ' ';

	pic_cont:
		actr++;
	}

	if (cp != arcs_cmdline)		/* get rid of trailing space */
		--cp;
	*cp = '\0';

#ifdef DEBUG_CMDLINE
	pr_debug("prom cmdline: %s\n", arcs_cmdline);
#endif
}
