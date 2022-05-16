/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Synopsys, Inc. (www.synopsys.com)
 *
 * Author: Eugeniy Paltsev <Eugeniy.Paltsev@synopsys.com>
 */
#ifndef __ASM_ARC_ASSERTS_H
#define __ASM_ARC_ASSERTS_H

/* Helpers to sanitize config options. */

void chk_opt_strict(char *opt_name, bool hw_exists, bool opt_ena);
void chk_opt_weak(char *opt_name, bool hw_exists, bool opt_ena);

/*
 * Check required config option:
 *  - panic in case of OPT enabled but corresponding HW absent.
 *  - warn in case of OPT disabled but corresponding HW exists.
*/
#define CHK_OPT_STRICT(opt_name, hw_exists)				\
({									\
	chk_opt_strict(#opt_name, hw_exists, IS_ENABLED(opt_name));	\
})

/*
 * Check optional config option:
 *  - panic in case of OPT enabled but corresponding HW absent.
*/
#define CHK_OPT_WEAK(opt_name, hw_exists)				\
({									\
	chk_opt_weak(#opt_name, hw_exists, IS_ENABLED(opt_name));	\
})

#endif /* __ASM_ARC_ASSERTS_H */
