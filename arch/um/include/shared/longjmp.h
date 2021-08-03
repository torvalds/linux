/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UML_LONGJMP_H
#define __UML_LONGJMP_H

#include <sysdep/archsetjmp.h>
#include <os.h>

extern int signals_enabled;
extern int setjmp(jmp_buf);
extern void longjmp(jmp_buf, int);

#define UML_LONGJMP(buf, val) do { \
	longjmp(*buf, val);	\
} while(0)

#define UML_SETJMP(buf) ({				\
	int n, enable;					\
	enable = *(volatile int *)&signals_enabled;	\
	n = setjmp(*buf);				\
	if(n != 0)					\
		set_signals_trace(enable);		\
	n; })

#endif
