#ifndef _ASM_LKL_HOST_OPS_H
#define _ASM_LKL_HOST_OPS_H

#include "irq.h"
#include <uapi/asm/host_ops.h>

extern struct lkl_host_operations *lkl_ops;

#define lkl_puts(text) lkl_ops->print(text, strlen(text))

#endif
