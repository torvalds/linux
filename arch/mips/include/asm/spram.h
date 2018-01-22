/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MIPS_SPRAM_H
#define _MIPS_SPRAM_H

#if defined(CONFIG_MIPS_SPRAM)
extern __init void spram_config(void);
#else
static inline void spram_config(void) { };
#endif /* CONFIG_MIPS_SPRAM */

#endif /* _MIPS_SPRAM_H */
