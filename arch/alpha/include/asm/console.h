/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __AXP_CONSOLE_H
#define __AXP_CONSOLE_H

#include <uapi/asm/console.h>

#ifndef __ASSEMBLY__
extern long callback_puts(long unit, const char *s, long length);
extern long callback_getc(long unit);
extern long callback_open_console(void);
extern long callback_close_console(void);
extern long callback_open(const char *device, long length);
extern long callback_close(long unit);
extern long callback_read(long channel, long count, const char *buf, long lbn);
extern long callback_getenv(long id, const char *buf, unsigned long buf_size);
extern long callback_setenv(long id, const char *buf, unsigned long buf_size);
extern long callback_save_env(void);

extern int srm_fixup(unsigned long new_callback_addr,
		     unsigned long new_hwrpb_addr);
extern long srm_puts(const char *, long);
extern long srm_printk(const char *, ...)
	__attribute__ ((format (printf, 1, 2)));

struct crb_struct;
struct hwrpb_struct;
extern int callback_init_done;
extern void * callback_init(void *);
#endif /* __ASSEMBLY__ */
#endif /* __AXP_CONSOLE_H */
