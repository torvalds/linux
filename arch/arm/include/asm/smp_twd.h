#ifndef __ASMARM_SMP_TWD_H
#define __ASMARM_SMP_TWD_H

struct clock_event_device;

extern void __iomem *twd_base;

void twd_timer_stop(void);
int twd_timer_ack(void);
void twd_timer_setup(struct clock_event_device *);

#endif
