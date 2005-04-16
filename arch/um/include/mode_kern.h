/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __MODE_KERN_H__
#define __MODE_KERN_H__

#include "linux/config.h"

#ifdef CONFIG_MODE_TT
#include "mode_kern-tt.h"
#endif

#ifdef CONFIG_MODE_SKAS
#include "mode_kern-skas.h"
#endif

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
