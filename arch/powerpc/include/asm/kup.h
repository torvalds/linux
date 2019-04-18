/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KUP_H_
#define _ASM_POWERPC_KUP_H_

#ifndef __ASSEMBLY__

void setup_kup(void);

#ifdef CONFIG_PPC_KUEP
void setup_kuep(bool disabled);
#else
static inline void setup_kuep(bool disabled) { }
#endif /* CONFIG_PPC_KUEP */

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_KUP_H_ */
