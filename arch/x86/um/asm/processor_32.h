/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_PROCESSOR_I386_H
#define __UM_PROCESSOR_I386_H

#include <linux/string.h>
#include <asm/segment.h>
#include <asm/ldt.h>

extern int host_has_cmov;

struct uml_tls_struct {
	struct user_desc tls;
	unsigned flushed:1;
	unsigned present:1;
};

struct arch_thread {
	struct uml_tls_struct tls_array[GDT_ENTRY_TLS_ENTRIES];
	unsigned long debugregs[8];
	int debugregs_seq;
	struct faultinfo faultinfo;
};

#define INIT_ARCH_THREAD { \
	.tls_array  		= { [ 0 ... GDT_ENTRY_TLS_ENTRIES - 1 ] = \
				    { .present = 0, .flushed = 0 } }, \
	.debugregs  		= { [ 0 ... 7 ] = 0 }, \
	.debugregs_seq		= 0, \
	.faultinfo		= { 0, 0, 0 } \
}

#define STACKSLOTS_PER_LINE 8

static inline void arch_flush_thread(struct arch_thread *thread)
{
	/* Clear any TLS still hanging */
	memset(&thread->tls_array, 0, sizeof(thread->tls_array));
}

static inline void arch_copy_thread(struct arch_thread *from,
                                    struct arch_thread *to)
{
        memcpy(&to->tls_array, &from->tls_array, sizeof(from->tls_array));
}

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter"). Stolen
 * from asm-i386/processor.h
 */
#define current_text_addr() \
	({ void *pc; __asm__("movl $1f,%0\n1:":"=g" (pc)); pc; })

#define current_sp() ({ void *sp; __asm__("movl %%esp, %0" : "=r" (sp) : ); sp; })
#define current_bp() ({ unsigned long bp; __asm__("movl %%ebp, %0" : "=r" (bp) : ); bp; })

#endif
