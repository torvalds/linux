#ifndef __NOUVEAU_PRINTK_H__
#define __NOUVEAU_PRINTK_H__

#include <core/os.h>
#include <core/debug.h>

struct nouveau_object;

#define NV_PRINTK_FATAL    KERN_CRIT
#define NV_PRINTK_ERROR    KERN_ERR
#define NV_PRINTK_WARN     KERN_WARNING
#define NV_PRINTK_INFO     KERN_INFO
#define NV_PRINTK_DEBUG    KERN_DEBUG
#define NV_PRINTK_PARANOIA KERN_DEBUG
#define NV_PRINTK_TRACE    KERN_DEBUG
#define NV_PRINTK_SPAM     KERN_DEBUG

void __printf(4, 5)
nv_printk_(struct nouveau_object *, const char *, int, const char *, ...);

#define nv_printk(o,l,f,a...) do {                                             \
	if (NV_DBG_##l <= CONFIG_NOUVEAU_DEBUG)                                \
		nv_printk_(nv_object(o), NV_PRINTK_##l, NV_DBG_##l, f, ##a);   \
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
		nv_printk_(NULL, NV_PRINTK_FATAL, NV_DBG_FATAL, f "\n", ##a);  \
	BUG_ON(1);                                                             \
} while(0)

#endif
