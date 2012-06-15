#ifndef __PARISC_BARRIER_H
#define __PARISC_BARRIER_H

/*
** This is simply the barrier() macro from linux/kernel.h but when serial.c
** uses tqueue.h uses smp_mb() defined using barrier(), linux/kernel.h
** hasn't yet been included yet so it fails, thus repeating the macro here.
**
** PA-RISC architecture allows for weakly ordered memory accesses although
** none of the processors use it. There is a strong ordered bit that is
** set in the O-bit of the page directory entry. Operating systems that
** can not tolerate out of order accesses should set this bit when mapping
** pages. The O-bit of the PSW should also be set to 1 (I don't believe any
** of the processor implemented the PSW O-bit). The PCX-W ERS states that
** the TLB O-bit is not implemented so the page directory does not need to
** have the O-bit set when mapping pages (section 3.1). This section also
** states that the PSW Y, Z, G, and O bits are not implemented.
** So it looks like nothing needs to be done for parisc-linux (yet).
** (thanks to chada for the above comment -ggg)
**
** The __asm__ op below simple prevents gcc/ld from reordering
** instructions across the mb() "call".
*/
#define mb()		__asm__ __volatile__("":::"memory")	/* barrier() */
#define rmb()		mb()
#define wmb()		mb()
#define smp_mb()	mb()
#define smp_rmb()	mb()
#define smp_wmb()	mb()
#define smp_read_barrier_depends()	do { } while(0)
#define read_barrier_depends()		do { } while(0)

#define set_mb(var, value)		do { var = value; mb(); } while (0)

#endif /* __PARISC_BARRIER_H */
