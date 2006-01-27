/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __MODE_KERN_H__
#define __MODE_KERN_H__

#include "linux/config.h"

#ifdef CONFIG_MODE_TT
#include "mode_kern_tt.h"
#endif

#ifdef CONFIG_MODE_SKAS
#include "mode_kern_skas.h"
#endif

#endif
