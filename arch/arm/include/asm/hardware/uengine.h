/*
 * Generic library functions for the microengines found on the Intel
 * IXP2000 series of network processors.
 *
 * Copyright (C) 2004, 2005 Lennert Buytenhek <buytenh@wantstofly.org>
 * Dedicated to Marija Kulikova.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 */

#ifndef __IXP2000_UENGINE_H
#define __IXP2000_UENGINE_H

extern u32 ixp2000_uengine_mask;

struct ixp2000_uengine_code
{
	u32	cpu_model_bitmask;
	u8	cpu_min_revision;
	u8	cpu_max_revision;

	u32	uengine_parameters;

	struct ixp2000_reg_value {
		int	reg;
		u32	value;
	} *initial_reg_values;

	int	num_insns;
	u8	*insns;
};

u32 ixp2000_uengine_csr_read(int uengine, int offset);
void ixp2000_uengine_csr_write(int uengine, int offset, u32 value);
void ixp2000_uengine_reset(u32 uengine_mask);
void ixp2000_uengine_set_mode(int uengine, u32 mode);
void ixp2000_uengine_load_microcode(int uengine, u8 *ucode, int insns);
void ixp2000_uengine_init_context(int uengine, int context, int pc);
void ixp2000_uengine_start_contexts(int uengine, u8 ctx_mask);
void ixp2000_uengine_stop_contexts(int uengine, u8 ctx_mask);
int ixp2000_uengine_load(int uengine, struct ixp2000_uengine_code *c);

#define IXP2000_UENGINE_8_CONTEXTS		0x00000000
#define IXP2000_UENGINE_4_CONTEXTS		0x80000000
#define IXP2000_UENGINE_PRN_UPDATE_EVERY	0x40000000
#define IXP2000_UENGINE_PRN_UPDATE_ON_ACCESS	0x00000000
#define IXP2000_UENGINE_NN_FROM_SELF		0x00100000
#define IXP2000_UENGINE_NN_FROM_PREVIOUS	0x00000000
#define IXP2000_UENGINE_ASSERT_EMPTY_AT_3	0x000c0000
#define IXP2000_UENGINE_ASSERT_EMPTY_AT_2	0x00080000
#define IXP2000_UENGINE_ASSERT_EMPTY_AT_1	0x00040000
#define IXP2000_UENGINE_ASSERT_EMPTY_AT_0	0x00000000
#define IXP2000_UENGINE_LM_ADDR1_GLOBAL		0x00020000
#define IXP2000_UENGINE_LM_ADDR1_PER_CONTEXT	0x00000000
#define IXP2000_UENGINE_LM_ADDR0_GLOBAL		0x00010000
#define IXP2000_UENGINE_LM_ADDR0_PER_CONTEXT	0x00000000


#endif
