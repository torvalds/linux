// SPDX-License-Identifier: GPL-2.0
/*
 *	Copyright IBM Corp. 2001, 2007
 *	Authors:	Peter Tiedemann (ptiedem@de.ibm.com)
 *
 */

#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ctype.h>
#include <linux/sysctl.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include "ctcm_dbug.h"

/*
 * Debug Facility Stuff
 */

struct ctcm_dbf_info ctcm_dbf[CTCM_DBF_INFOS] = {
	[CTCM_DBF_SETUP]     = {"ctc_setup", 8, 1, 64, CTC_DBF_INFO, NULL},
	[CTCM_DBF_ERROR]     = {"ctc_error", 8, 1, 64, CTC_DBF_ERROR, NULL},
	[CTCM_DBF_TRACE]     = {"ctc_trace", 8, 1, 64, CTC_DBF_ERROR, NULL},
	[CTCM_DBF_MPC_SETUP] = {"mpc_setup", 8, 1, 80, CTC_DBF_INFO, NULL},
	[CTCM_DBF_MPC_ERROR] = {"mpc_error", 8, 1, 80, CTC_DBF_ERROR, NULL},
	[CTCM_DBF_MPC_TRACE] = {"mpc_trace", 8, 1, 80, CTC_DBF_ERROR, NULL},
};

void ctcm_unregister_dbf_views(void)
{
	int x;
	for (x = 0; x < CTCM_DBF_INFOS; x++) {
		debug_unregister(ctcm_dbf[x].id);
		ctcm_dbf[x].id = NULL;
	}
}

int ctcm_register_dbf_views(void)
{
	int x;
	for (x = 0; x < CTCM_DBF_INFOS; x++) {
		/* register the areas */
		ctcm_dbf[x].id = debug_register(ctcm_dbf[x].name,
						ctcm_dbf[x].pages,
						ctcm_dbf[x].areas,
						ctcm_dbf[x].len);
		if (ctcm_dbf[x].id == NULL) {
			ctcm_unregister_dbf_views();
			return -ENOMEM;
		}

		/* register a view */
		debug_register_view(ctcm_dbf[x].id, &debug_hex_ascii_view);
		/* set a passing level */
		debug_set_level(ctcm_dbf[x].id, ctcm_dbf[x].level);
	}

	return 0;
}

void ctcm_dbf_longtext(enum ctcm_dbf_names dbf_nix, int level, char *fmt, ...)
{
	char dbf_txt_buf[64];
	va_list args;

	if (!debug_level_enabled(ctcm_dbf[dbf_nix].id, level))
		return;
	va_start(args, fmt);
	vscnprintf(dbf_txt_buf, sizeof(dbf_txt_buf), fmt, args);
	va_end(args);

	debug_text_event(ctcm_dbf[dbf_nix].id, level, dbf_txt_buf);
}

