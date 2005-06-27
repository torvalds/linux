/*
 * arch/s390/kernel/crash.c
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 *
 */

#include <linux/threads.h>
#include <linux/kexec.h>

note_buf_t crash_notes[NR_CPUS];

void machine_crash_shutdown(struct pt_regs *regs)
{
}
