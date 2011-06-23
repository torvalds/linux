#ifndef __ASMARM_I8253_H
#define __ASMARM_I8253_H

/* i8253A PIT registers */
#define PIT_MODE	0x43
#define PIT_CH0		0x40

#define PIT_LATCH	((PIT_TICK_RATE + HZ / 2) / HZ)

extern raw_spinlock_t i8253_lock;

#define outb_pit	outb_p
#define inb_pit		inb_p

#endif
