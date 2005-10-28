#ifndef __UM_THREAD_H
#define __UM_THREAD_H

#include <kern_constants.h>

#ifdef UML_CONFIG_MODE_TT
#define TASK_EXTERN_PID(task) *((int *) &(((char *) (task))[HOST_TASK_EXTERN_PID]))
#endif

#endif
