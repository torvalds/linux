/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ATH25_DEVICES_H
#define __ATH25_DEVICES_H

#include <linux/cpu.h>

#define ATH25_REG_MS(_val, _field)	(((_val) & _field##_M) >> _field##_S)

#define ATH25_IRQ_CPU_CLOCK	(MIPS_CPU_IRQ_BASE + 7)	/* C0_CAUSE: 0x8000 */

enum ath25_soc_type {
	/* handled by ar5312.c */
	ATH25_SOC_AR2312,
	ATH25_SOC_AR2313,
	ATH25_SOC_AR5312,

	/* handled by ar2315.c */
	ATH25_SOC_AR2315,
	ATH25_SOC_AR2316,
	ATH25_SOC_AR2317,
	ATH25_SOC_AR2318,

	ATH25_SOC_UNKNOWN
};

extern enum ath25_soc_type ath25_soc;
extern struct ar231x_board_config ath25_board;
extern void (*ath25_irq_dispatch)(void);

int ath25_find_config(phys_addr_t offset, unsigned long size);
void ath25_serial_setup(u32 mapbase, int irq, unsigned int uartclk);
int ath25_add_wmac(int nr, u32 base, int irq);

static inline bool is_ar2315(void)
{
	return (current_cpu_data.cputype == CPU_4KEC);
}

static inline bool is_ar5312(void)
{
	return !is_ar2315();
}

#endif
