/*
 * Operating system kernel abstraction -- all functions
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_ALL_H
#define __OSKA_ALL_H

/**
 * @mainpage Operating System Kernel Abstraction
 *
 * @section intro Introduction
 *
 * The Operating System Kernel Abstraction (oska) is a software
 * package providing an abstraction for various operating system
 * kernel facilities for use by device drivers and other OS kernel
 * software (e.g., SDIO stacks).  Oska is modularized and intended to
 * be a lightweight wrapper around an OSes interfaces.
 *
 * @section modules Modules
 *
 * Oska is organized into the modules, each of which has it's own
 * header file providing the interface.
 *
 *   - \ref alloc "Memory allocation" <oska/alloc.h>
 *   - \ref event "Events" <oska/event.h>
 *   - \ref mutex "Mutexes" <oska/mutex.h>
 *   - \ref print "Console output" <oska/print.h>
 *   - \ref spinlock "Spinlocks" <oska/spinlock.h>
 *   - \ref thread "Threading" <oska/thread.h>
 *   - \ref time "Timing and delays" <oska/time.h>
 *   - \ref timer "Timers" <oska/timer.h>
 *   - \ref types "Standard Types" <oska/types.h>
 *   - \ref util "Miscellaneous utilities" <oska/util.h>
 *
 * An <oska/all.h> header is provided which includes all the above
 * modules.
 *
 * There are additional modules that are not included in <oska/all.h>.
 *
 *   - \ref refcount "Reference Counting" <oska/refcount.h>
 *   - \ref list "Linked lists" <oska/list.h>
 *   - \ref trace "Tracing messages" <oska/trace.h>
 */

#include "alloc.h"
#include "event.h"
#include "mutex.h"
#include "print.h"
#include "spinlock.h"
#include "thread.h"
#include "time.h"
#include "timer.h"
#include "types.h"
#include "util.h"

#endif /* __OSKA_ALL_H */
