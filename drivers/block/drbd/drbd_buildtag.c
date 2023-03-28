// SPDX-License-Identifier: GPL-2.0-only
#include <linux/drbd_config.h>
#include <linux/module.h>

const char *drbd_buildtag(void)
{
	/* DRBD built from external sources has here a reference to the
	 * git hash of the source code.
	 */

	static char buildtag[38] = "\0uilt-in";

	if (buildtag[0] == 0) {
#ifdef MODULE
		sprintf(buildtag, "srcversion: %-24s", THIS_MODULE->srcversion);
#else
		buildtag[0] = 'b';
#endif
	}

	return buildtag;
}
