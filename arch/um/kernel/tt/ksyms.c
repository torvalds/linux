/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/module.h"
#include "asm/uaccess.h"
#include "mode.h"

EXPORT_SYMBOL(__do_copy_from_user);
EXPORT_SYMBOL(__do_copy_to_user);
EXPORT_SYMBOL(__do_strncpy_from_user);
EXPORT_SYMBOL(__do_strnlen_user); 
EXPORT_SYMBOL(__do_clear_user);
EXPORT_SYMBOL(clear_user_tt);

EXPORT_SYMBOL(tracing_pid);
EXPORT_SYMBOL(honeypot);

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
