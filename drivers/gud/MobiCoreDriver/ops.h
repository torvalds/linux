/*
 * MobiCore driver module.(interface to the secure world SWD)
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 * <-- Copyright Trustonic Limited 2013 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MC_OPS_H_
#define _MC_OPS_H_

#include <linux/workqueue.h>
#include "fastcall.h"

int mc_yield(void);
int mc_nsiq(void);
int _nsiq(void);
uint32_t mc_get_version(void);

int mc_info(uint32_t ext_info_id, uint32_t *state, uint32_t *ext_info);
int mc_init(uint32_t base, uint32_t  nq_offset, uint32_t  nq_length,
	    uint32_t mcp_offset, uint32_t  mcp_length);

void mc_fastcall(void *data);

int mc_fastcall_init(struct mc_context *context);
void mc_fastcall_destroy(void);

#endif /* _MC_OPS_H_ */
