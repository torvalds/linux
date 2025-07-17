/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <as-layout.h>
#include <stub-data.h>

#ifdef __i386__
#include "stub_32.h"
#else
#include "stub_64.h"
#endif

extern void stub_segv_handler(int, siginfo_t *, void *);
extern void stub_syscall_handler(void);
extern void stub_signal_interrupt(int, siginfo_t *, void *);
extern void stub_signal_restorer(void);
