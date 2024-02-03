/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_POWERPC_PAPR_SYSPARM_H
#define _ASM_POWERPC_PAPR_SYSPARM_H

#include <uapi/asm/papr-sysparm.h>

typedef struct {
	u32 token;
} papr_sysparm_t;

#define mk_papr_sysparm(x_) ((papr_sysparm_t){ .token = x_, })

/*
 * Derived from the "Defined Parameters" table in PAPR 7.3.16 System
 * Parameters Option. Where the spec says "characteristics", we use
 * "attrs" in the symbolic names to keep them from getting too
 * unwieldy.
 */
#define PAPR_SYSPARM_SHARED_PROC_LPAR_ATTRS        mk_papr_sysparm(20)
#define PAPR_SYSPARM_PROC_MODULE_INFO              mk_papr_sysparm(43)
#define PAPR_SYSPARM_COOP_MEM_OVERCOMMIT_ATTRS     mk_papr_sysparm(44)
#define PAPR_SYSPARM_TLB_BLOCK_INVALIDATE_ATTRS    mk_papr_sysparm(50)
#define PAPR_SYSPARM_LPAR_NAME                     mk_papr_sysparm(55)

/**
 * struct papr_sysparm_buf - RTAS work area layout for system parameter functions.
 *
 * This is the memory layout of the buffers passed to/from
 * ibm,get-system-parameter and ibm,set-system-parameter. It is
 * distinct from the papr_sysparm_io_block structure that is passed
 * between user space and the kernel.
 */
struct papr_sysparm_buf {
	__be16 len;
	u8 val[PAPR_SYSPARM_MAX_OUTPUT];
};

struct papr_sysparm_buf *papr_sysparm_buf_alloc(void);
void papr_sysparm_buf_free(struct papr_sysparm_buf *buf);
int papr_sysparm_set(papr_sysparm_t param, const struct papr_sysparm_buf *buf);
int papr_sysparm_get(papr_sysparm_t param, struct papr_sysparm_buf *buf);

#endif /* _ASM_POWERPC_PAPR_SYSPARM_H */
