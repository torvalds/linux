#ifndef _ASM_X86_I8253_H
#define _ASM_X86_I8253_H

#define PIT_LATCH	LATCH

extern struct clock_event_device *global_clock_event;

extern void setup_pit_timer(void);

#endif /* _ASM_X86_I8253_H */
