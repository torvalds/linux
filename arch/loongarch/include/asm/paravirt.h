/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LOONGARCH_PARAVIRT_H
#define _ASM_LOONGARCH_PARAVIRT_H

#ifdef CONFIG_PARAVIRT

int __init pv_ipi_init(void);
int __init pv_time_init(void);
int __init pv_spinlock_init(void);

#else

static inline int pv_ipi_init(void)
{
	return 0;
}

static inline int pv_time_init(void)
{
	return 0;
}

static inline int pv_spinlock_init(void)
{
	return 0;
}

#endif // CONFIG_PARAVIRT
#endif
