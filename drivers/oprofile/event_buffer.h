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
#include <asm/mutex.h>
 
int alloc_event_buffer(void);

void free_event_buffer(void);
 
/* wake up the process sleeping on the event file */
void wake_up_buffer_waiter(void);

#define INVALID_COOKIE ~0UL
#define NO_COOKIE 0UL

extern const struct file_operations event_buffer_fops;
 
/* mutex between sync_cpu_buffers() and the
 * file reading code.
 */
extern struct mutex buffer_mutex;
 
#endif /* EVENT_BUFFER_H */
