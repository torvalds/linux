#ifndef __UM_THREAD_H
#define __UM_THREAD_H

#include <kern_constants.h>

#define TASK_DEBUGREGS(task) ((unsigned long *) &(((char *) (task))[HOST_TASK_DEBUGREGS]))

#endif
