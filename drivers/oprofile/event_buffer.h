/**
 * @file event_buffer.h
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#ifndef EVENT_BUFFER_H
#define EVENT_BUFFER_H

#include <linux/types.h> 
#include <asm/semaphore.h>
 
int alloc_event_buffer(void);

void free_event_buffer(void);
 
/* wake up the process sleeping on the event file */
void wake_up_buffer_waiter(void);
 
/* Each escaped entry is prefixed by ESCAPE_CODE
 * then one of the following codes, then the
 * relevant data.
 */
#define ESCAPE_CODE			~0UL
#define CTX_SWITCH_CODE 		1
#define CPU_SWITCH_CODE 		2
#define COOKIE_SWITCH_CODE 		3
#define KERNEL_ENTER_SWITCH_CODE	4
#define KERNEL_EXIT_SWITCH_CODE		5
#define MODULE_LOADED_CODE		6
#define CTX_TGID_CODE			7
#define TRACE_BEGIN_CODE		8
#define TRACE_END_CODE			9
 
/* add data to the event buffer */
void add_event_entry(unsigned long data);
 
extern struct file_operations event_buffer_fops;
 
/* mutex between sync_cpu_buffers() and the
 * file reading code.
 */
extern struct semaphore buffer_sem;
 
#endif /* EVENT_BUFFER_H */
