/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_PARAVIRT_H
#define _ASM_RISCV_PARAVIRT_H

#ifdef CONFIG_PARAVIRT

int __init pv_time_init(void);

#else

#define pv_time_init() do {} while (0)

#endif /* CONFIG_PARAVIRT */
#endif /* _ASM_RISCV_PARAVIRT_H */
