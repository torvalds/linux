/*
 *  linux/arch/m68k/sun3x/time.c
 *
 *  Sun3x-specific time handling
 */

#include <linux/types.h>
#include <linux/kd.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/bcd.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/traps.h>
#include <asm/sun3x.h>
#include <asm/sun3ints.h>
#include <asm/rtc.h>

#include "time.h"

#define M_CONTROL 0xf8
#define M_SEC     0xf9
#define M_MIN     0xfa
#define M_HOUR    0xfb
#define M_DAY     0xfc
#define M_DATE    0xfd
#define M_MONTH   0xfe
#define M_YEAR    0xff

#define C_WRITE   0x80
#define C_READ    0x40
#define C_SIGN    0x20
#define C_CALIB   0x1f

int sun3x_hwclk(int set, struct rtc_time *t)
{
	volatile struct mostek_dt *h =
		(struct mostek_dt *)(SUN3X_EEPROM+M_CONTROL);
	unsigned long flags;

	local_irq_save(flags);

	if(set) {
		h->csr |= C_WRITE;
		h->sec = bin2bcd(t->tm_sec);
		h->min = bin2bcd(t->tm_min);
		h->hour = bin2bcd(t->tm_hour);
		h->wday = bin2bcd(t->tm_wday);
		h->mday = bin2bcd(t->tm_mday);
		h->month = bin2bcd(t->tm_mon);
		h->year = bin2bcd(t->tm_year);
		h->csr &= ~C_WRITE;
	} else {
		h->csr |= C_READ;
		t->tm_sec = bcd2bin(h->sec);
		t->tm_min = bcd2bin(h->min);
		t->tm_hour = bcd2bin(h->hour);
		t->tm_wday = bcd2bin(h->wday);
		t->tm_mday = bcd2bin(h->mday);
		t->tm_mon = bcd2bin(h->month);
		t->tm_year = bcd2bin(h->year);
		h->csr &= ~C_READ;
	}

	local_irq_restore(flags);

	return 0;
}
/* Not much we can do here */
u32 sun3x_gettimeoffset(void)
{
    return 0L;
}

#if 0
static void sun3x_timer_tick(int irq, void *dev_id, struct pt_regs *regs)
{
    void (*vector)(int, void *, struct pt_regs *) = dev_id;

    /* Clear the pending interrupt - pulse the enable line low */
    disable_irq(5);
    enable_irq(5);

    vector(irq, NULL, regs);
}
#endif

void __init sun3x_sched_init(irq_handler_t vector)
{

	sun3_disable_interrupts();


    /* Pulse enable low to get the clock started */
	sun3_disable_irq(5);
	sun3_enable_irq(5);
	sun3_enable_interrupts();
}
