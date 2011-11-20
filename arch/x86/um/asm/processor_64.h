/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#ifndef __UM_PROCESSOR_X86_64_H
#define __UM_PROCESSOR_X86_64_H

struct arch_thread {
        unsigned long debugregs[8];
        int debugregs_seq;
        unsigned long fs;
        struct faultinfo faultinfo;
};

#define INIT_ARCH_THREAD { .debugregs  		= { [ 0 ... 7 ] = 0 }, \
			   .debugregs_seq	= 0, \
			   .fs			= 0, \
			   .faultinfo		= { 0, 0, 0 } }

static inline void arch_flush_thread(struct arch_thread *thread)
{
}

static inline void arch_copy_thread(struct arch_thread *from,
                                    struct arch_thread *to)
{
	to->fs = from->fs;
}

#define current_text_addr() \
	({ void *pc; __asm__("movq $1f,%0\n1:":"=g" (pc)); pc; })

#endif
