/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_S390_PTDUMP_H
#define _ASM_S390_PTDUMP_H

void ptdump_check_wx(void);

static inline void debug_checkwx(void)
{
	if (IS_ENABLED(CONFIG_DEBUG_WX))
		ptdump_check_wx();
}

#endif /* _ASM_S390_PTDUMP_H */
