#ifndef __ASMARM_SMP_TWD_H
#define __ASMARM_SMP_TWD_H

#define TWD_TIMER_LOAD			0x00
#define TWD_TIMER_COUNTER		0x04
#define TWD_TIMER_CONTROL		0x08
#define TWD_TIMER_INTSTAT		0x0C

#define TWD_WDOG_LOAD			0x20
#define TWD_WDOG_COUNTER		0x24
#define TWD_WDOG_CONTROL		0x28
#define TWD_WDOG_INTSTAT		0x2C
#define TWD_WDOG_RESETSTAT		0x30
#define TWD_WDOG_DISABLE		0x34

#define TWD_TIMER_CONTROL_ENABLE	(1 << 0)
#define TWD_TIMER_CONTROL_ONESHOT	(0 << 1)
#define TWD_TIMER_CONTROL_PERIODIC	(1 << 1)
#define TWD_TIMER_CONTROL_IT_ENABLE	(1 << 2)
#define TWD_TIMER_CONTROL_PRESCALE_MASK	(0xFF << 8)

struct clock_event_device;

extern void __iomem *twd_base;

void twd_timer_stop(void);
int twd_timer_ack(void);
void twd_timer_setup(struct clock_event_device *);

/*
 * Use this setup function on systems where the cpu clock frequency may
 * change.  periphclk_prescaler is the fixed divider value between the cpu
 * clock and the PERIPHCLK clock that feeds the TWD.  target_rate should be
 * low enough that the prescaler can accurately reach the target rate from the
 * lowest cpu frequency.
 */
void twd_timer_setup_scalable(struct clock_event_device *,
	unsigned long target_rate, unsigned int periphclk_prescaler);

#endif
