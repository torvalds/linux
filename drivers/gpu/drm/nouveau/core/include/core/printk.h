#ifndef __NOUVEAU_PRINTK_H__
#define __NOUVEAU_PRINTK_H__

#include <core/os.h>
#include <core/debug.h>

struct nouveau_object;

void __printf(3, 4)
nv_printk_(struct nouveau_object *, int, const char *, ...);

#define nv_printk(o,l,f,a...) do {                                             \
	if (NV_DBG_##l <= CONFIG_NOUVEAU_DEBUG)                                \
		nv_printk_(nv_object(o), NV_DBG_##l, f, ##a);                  \
} while(0)

#define nv_fatal(o,f,a...) nv_printk((o), FATAL, f, ##a)
#define nv_error(o,f,a...) nv_printk((o), ERROR, f, ##a)
#define nv_warn(o,f,a...) nv_printk((o), WARN, f, ##a)
#define nv_info(o,f,a...) nv_printk((o), INFO, f, ##a)
#define nv_debug(o,f,a...) nv_printk((o), DEBUG, f, ##a)
#define nv_trace(o,f,a...) nv_printk((o), TRACE, f, ##a)
#define nv_spam(o,f,a...) nv_printk((o), SPAM, f, ##a)

#define nv_assert(f,a...) do {                                                 \
	if (NV_DBG_FATAL <= CONFIG_NOUVEAU_DEBUG)                              \
		nv_printk_(NULL, NV_DBG_FATAL, f "\n", ##a);                   \
	BUG_ON(1);                                                             \
} while(0)

#endif
