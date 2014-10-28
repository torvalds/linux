#ifndef __ATH25_DEVICES_H
#define __ATH25_DEVICES_H

#include <linux/cpu.h>

#define ATH25_REG_MS(_val, _field)	(((_val) & _field##_M) >> _field##_S)

#define ATH25_IRQ_CPU_CLOCK	(MIPS_CPU_IRQ_BASE + 7)	/* C0_CAUSE: 0x8000 */

extern void (*ath25_irq_dispatch)(void);

void ath25_serial_setup(u32 mapbase, int irq, unsigned int uartclk);

static inline bool is_ar2315(void)
{
	return (current_cpu_data.cputype == CPU_4KEC);
}

static inline bool is_ar5312(void)
{
	return !is_ar2315();
}

#endif
