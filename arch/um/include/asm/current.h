/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __UM_CURRENT_H
#define __UM_CURRENT_H

#include "linux/thread_info.h"

#define current (current_thread_info()->task)

#endif
