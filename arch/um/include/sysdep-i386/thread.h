#ifndef __UM_THREAD_H
#define __UM_THREAD_H

#include <kern_constants.h>

#define TASK_DEBUGREGS(task) ((unsigned long *) &(((char *) (task))[HOST_TASK_DEBUGREGS]))
#ifdef CONFIG_MODE_TT
#define TASK_EXTERN_PID(task) *((int *) &(((char *) (task))[HOST_TASK_EXTERN_PID]))
#endif

#endif
