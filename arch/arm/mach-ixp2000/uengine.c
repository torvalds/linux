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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/string.h>
#include <asm/hardware.h>
#include <asm/arch/ixp2000-regs.h>
#include <asm/arch/uengine.h>
#include <asm/io.h>

#define USTORE_ADDRESS			0x000
#define USTORE_DATA_LOWER		0x004
#define USTORE_DATA_UPPER		0x008
#define CTX_ENABLES			0x018
#define CC_ENABLE			0x01c
#define CSR_CTX_POINTER			0x020
#define INDIRECT_CTX_STS		0x040
#define ACTIVE_CTX_STS			0x044
#define INDIRECT_CTX_SIG_EVENTS		0x048
#define INDIRECT_CTX_WAKEUP_EVENTS	0x050
#define NN_PUT				0x080
#define NN_GET				0x084
#define TIMESTAMP_LOW			0x0c0
#define TIMESTAMP_HIGH			0x0c4
#define T_INDEX_BYTE_INDEX		0x0f4
#define LOCAL_CSR_STATUS		0x180

u32 ixp2000_uengine_mask;

static void *ixp2000_uengine_csr_area(int uengine)
{
	return ((void *)IXP2000_UENGINE_CSR_VIRT_BASE) + (uengine << 10);
}

/*
 * LOCAL_CSR_STATUS=1 after a read or write to a microengine's CSR
 * space means that the microengine we tried to access was also trying
 * to access its own CSR space on the same clock cycle as we did.  When
 * this happens, we lose the arbitration process by default, and the
 * read or write we tried to do was not actually performed, so we try
 * again until it succeeds.
 */
u32 ixp2000_uengine_csr_read(int uengine, int offset)
{
	void *uebase;
	u32 *local_csr_status;
	u32 *reg;
	u32 value;

	uebase = ixp2000_uengine_csr_area(uengine);

	local_csr_status = (u32 *)(uebase + LOCAL_CSR_STATUS);
	reg = (u32 *)(uebase + offset);
	do {
		value = ixp2000_reg_read(reg);
	} while (ixp2000_reg_read(local_csr_status) & 1);

	return value;
}
EXPORT_SYMBOL(ixp2000_uengine_csr_read);

void ixp2000_uengine_csr_write(int uengine, int offset, u32 value)
{
	void *uebase;
	u32 *local_csr_status;
	u32 *reg;

	uebase = ixp2000_uengine_csr_area(uengine);

	local_csr_status = (u32 *)(uebase + LOCAL_CSR_STATUS);
	reg = (u32 *)(uebase + offset);
	do {
		ixp2000_reg_write(reg, value);
	} while (ixp2000_reg_read(local_csr_status) & 1);
}
EXPORT_SYMBOL(ixp2000_uengine_csr_write);

void ixp2000_uengine_reset(u32 uengine_mask)
{
	ixp2000_reg_wrb(IXP2000_RESET1, uengine_mask & ixp2000_uengine_mask);
	ixp2000_reg_wrb(IXP2000_RESET1, 0);
}
EXPORT_SYMBOL(ixp2000_uengine_reset);

void ixp2000_uengine_set_mode(int uengine, u32 mode)
{
	/*
	 * CTL_STR_PAR_EN: unconditionally enable parity checking on
	 * control store.
	 */
	mode |= 0x10000000;
	ixp2000_uengine_csr_write(uengine, CTX_ENABLES, mode);

	/*
	 * Enable updating of condition codes.
	 */
	ixp2000_uengine_csr_write(uengine, CC_ENABLE, 0x00002000);

	/*
	 * Initialise other per-microengine registers.
	 */
	ixp2000_uengine_csr_write(uengine, NN_PUT, 0x00);
	ixp2000_uengine_csr_write(uengine, NN_GET, 0x00);
	ixp2000_uengine_csr_write(uengine, T_INDEX_BYTE_INDEX, 0);
}
EXPORT_SYMBOL(ixp2000_uengine_set_mode);

static int make_even_parity(u32 x)
{
	return hweight32(x) & 1;
}

static void ustore_write(int uengine, u64 insn)
{
	/*
	 * Generate even parity for top and bottom 20 bits.
	 */
	insn |= (u64)make_even_parity((insn >> 20) & 0x000fffff) << 41;
	insn |= (u64)make_even_parity(insn & 0x000fffff) << 40;

	/*
	 * Write to microstore.  The second write auto-increments
	 * the USTORE_ADDRESS index register.
	 */
	ixp2000_uengine_csr_write(uengine, USTORE_DATA_LOWER, (u32)insn);
	ixp2000_uengine_csr_write(uengine, USTORE_DATA_UPPER, (u32)(insn >> 32));
}

void ixp2000_uengine_load_microcode(int uengine, u8 *ucode, int insns)
{
	int i;

	/*
	 * Start writing to microstore at address 0.
	 */
	ixp2000_uengine_csr_write(uengine, USTORE_ADDRESS, 0x80000000);
	for (i = 0; i < insns; i++) {
		u64 insn;

		insn = (((u64)ucode[0]) << 32) |
			(((u64)ucode[1]) << 24) |
			(((u64)ucode[2]) << 16) |
			(((u64)ucode[3]) << 8) |
			((u64)ucode[4]);
		ucode += 5;

		ustore_write(uengine, insn);
	}

	/*
 	 * Pad with a few NOPs at the end (to avoid the microengine
	 * aborting as it prefetches beyond the last instruction), unless
	 * we run off the end of the instruction store first, at which
	 * point the address register will wrap back to zero.
	 */
	for (i = 0; i < 4; i++) {
		u32 addr;

		addr = ixp2000_uengine_csr_read(uengine, USTORE_ADDRESS);
		if (addr == 0x80000000)
			break;
		ustore_write(uengine, 0xf0000c0300ULL);
	}

	/*
	 * End programming.
	 */
	ixp2000_uengine_csr_write(uengine, USTORE_ADDRESS, 0x00000000);
}
EXPORT_SYMBOL(ixp2000_uengine_load_microcode);

void ixp2000_uengine_init_context(int uengine, int context, int pc)
{
	/*
	 * Select the right context for indirect access.
	 */
	ixp2000_uengine_csr_write(uengine, CSR_CTX_POINTER, context);

	/*
	 * Initialise signal masks to immediately go to Ready state.
	 */
	ixp2000_uengine_csr_write(uengine, INDIRECT_CTX_SIG_EVENTS, 1);
	ixp2000_uengine_csr_write(uengine, INDIRECT_CTX_WAKEUP_EVENTS, 1);

	/*
	 * Set program counter.
	 */
	ixp2000_uengine_csr_write(uengine, INDIRECT_CTX_STS, pc);
}
EXPORT_SYMBOL(ixp2000_uengine_init_context);

void ixp2000_uengine_start_contexts(int uengine, u8 ctx_mask)
{
	u32 mask;

	/*
	 * Enable the specified context to go to Executing state.
	 */
	mask = ixp2000_uengine_csr_read(uengine, CTX_ENABLES);
	mask |= ctx_mask << 8;
	ixp2000_uengine_csr_write(uengine, CTX_ENABLES, mask);
}
EXPORT_SYMBOL(ixp2000_uengine_start_contexts);

void ixp2000_uengine_stop_contexts(int uengine, u8 ctx_mask)
{
	u32 mask;

	/*
	 * Disable the Ready->Executing transition.  Note that this
	 * does not stop the context until it voluntarily yields.
	 */
	mask = ixp2000_uengine_csr_read(uengine, CTX_ENABLES);
	mask &= ~(ctx_mask << 8);
	ixp2000_uengine_csr_write(uengine, CTX_ENABLES, mask);
}
EXPORT_SYMBOL(ixp2000_uengine_stop_contexts);

static int check_ixp_type(struct ixp2000_uengine_code *c)
{
	u32 product_id;
	u32 rev;

	product_id = ixp2000_reg_read(IXP2000_PRODUCT_ID);
	if (((product_id >> 16) & 0x1f) != 0)
		return 0;

	switch ((product_id >> 8) & 0xff) {
	case 0:		/* IXP2800 */
		if (!(c->cpu_model_bitmask & 4))
			return 0;
		break;

	case 1:		/* IXP2850 */
		if (!(c->cpu_model_bitmask & 8))
			return 0;
		break;

	case 2:		/* IXP2400 */
		if (!(c->cpu_model_bitmask & 2))
			return 0;
		break;

	default:
		return 0;
	}

	rev = product_id & 0xff;
	if (rev < c->cpu_min_revision || rev > c->cpu_max_revision)
		return 0;

	return 1;
}

static void generate_ucode(u8 *ucode, u32 *gpr_a, u32 *gpr_b)
{
	int offset;
	int i;

	offset = 0;

	for (i = 0; i < 128; i++) {
		u8 b3;
		u8 b2;
		u8 b1;
		u8 b0;

		b3 = (gpr_a[i] >> 24) & 0xff;
		b2 = (gpr_a[i] >> 16) & 0xff;
		b1 = (gpr_a[i] >> 8) & 0xff;
		b0 = gpr_a[i] & 0xff;

		// immed[@ai, (b1 << 8) | b0]
		// 11110000 0000VVVV VVVV11VV VVVVVV00 1IIIIIII
		ucode[offset++] = 0xf0;
		ucode[offset++] = (b1 >> 4);
		ucode[offset++] = (b1 << 4) | 0x0c | (b0 >> 6);
		ucode[offset++] = (b0 << 2);
		ucode[offset++] = 0x80 | i;

		// immed_w1[@ai, (b3 << 8) | b2]
		// 11110100 0100VVVV VVVV11VV VVVVVV00 1IIIIIII
		ucode[offset++] = 0xf4;
		ucode[offset++] = 0x40 | (b3 >> 4);
		ucode[offset++] = (b3 << 4) | 0x0c | (b2 >> 6);
		ucode[offset++] = (b2 << 2);
		ucode[offset++] = 0x80 | i;
	}

	for (i = 0; i < 128; i++) {
		u8 b3;
		u8 b2;
		u8 b1;
		u8 b0;

		b3 = (gpr_b[i] >> 24) & 0xff;
		b2 = (gpr_b[i] >> 16) & 0xff;
		b1 = (gpr_b[i] >> 8) & 0xff;
		b0 = gpr_b[i] & 0xff;

		// immed[@bi, (b1 << 8) | b0]
		// 11110000 0000VVVV VVVV001I IIIIII11 VVVVVVVV
		ucode[offset++] = 0xf0;
		ucode[offset++] = (b1 >> 4);
		ucode[offset++] = (b1 << 4) | 0x02 | (i >> 6);
		ucode[offset++] = (i << 2) | 0x03;
		ucode[offset++] = b0;

		// immed_w1[@bi, (b3 << 8) | b2]
		// 11110100 0100VVVV VVVV001I IIIIII11 VVVVVVVV
		ucode[offset++] = 0xf4;
		ucode[offset++] = 0x40 | (b3 >> 4);
		ucode[offset++] = (b3 << 4) | 0x02 | (i >> 6);
		ucode[offset++] = (i << 2) | 0x03;
		ucode[offset++] = b2;
	}

	// ctx_arb[kill]
	ucode[offset++] = 0xe0;
	ucode[offset++] = 0x00;
	ucode[offset++] = 0x01;
	ucode[offset++] = 0x00;
	ucode[offset++] = 0x00;
}

static int set_initial_registers(int uengine, struct ixp2000_uengine_code *c)
{
	int per_ctx_regs;
	u32 *gpr_a;
	u32 *gpr_b;
	u8 *ucode;
	int i;

	gpr_a = kmalloc(128 * sizeof(u32), GFP_KERNEL);
	gpr_b = kmalloc(128 * sizeof(u32), GFP_KERNEL);
	ucode = kmalloc(513 * 5, GFP_KERNEL);
	if (gpr_a == NULL || gpr_b == NULL || ucode == NULL) {
		kfree(ucode);
		kfree(gpr_b);
		kfree(gpr_a);
		return 1;
	}

	per_ctx_regs = 16;
	if (c->uengine_parameters & IXP2000_UENGINE_4_CONTEXTS)
		per_ctx_regs = 32;

	memset(gpr_a, 0, sizeof(gpr_a));
	memset(gpr_b, 0, sizeof(gpr_b));
	for (i = 0; i < 256; i++) {
		struct ixp2000_reg_value *r = c->initial_reg_values + i;
		u32 *bank;
		int inc;
		int j;

		if (r->reg == -1)
			break;

		bank = (r->reg & 0x400) ? gpr_b : gpr_a;
		inc = (r->reg & 0x80) ? 128 : per_ctx_regs;

		j = r->reg & 0x7f;
		while (j < 128) {
			bank[j] = r->value;
			j += inc;
		}
	}

	generate_ucode(ucode, gpr_a, gpr_b);
	ixp2000_uengine_load_microcode(uengine, ucode, 513);
	ixp2000_uengine_init_context(uengine, 0, 0);
	ixp2000_uengine_start_contexts(uengine, 0x01);
	for (i = 0; i < 100; i++) {
		u32 status;

		status = ixp2000_uengine_csr_read(uengine, ACTIVE_CTX_STS);
		if (!(status & 0x80000000))
			break;
	}
	ixp2000_uengine_stop_contexts(uengine, 0x01);

	kfree(ucode);
	kfree(gpr_b);
	kfree(gpr_a);

	return !!(i == 100);
}

int ixp2000_uengine_load(int uengine, struct ixp2000_uengine_code *c)
{
	int ctx;

	if (!check_ixp_type(c))
		return 1;

	if (!(ixp2000_uengine_mask & (1 << uengine)))
		return 1;

	ixp2000_uengine_reset(1 << uengine);
	ixp2000_uengine_set_mode(uengine, c->uengine_parameters);
	if (set_initial_registers(uengine, c))
		return 1;
	ixp2000_uengine_load_microcode(uengine, c->insns, c->num_insns);

	for (ctx = 0; ctx < 8; ctx++)
		ixp2000_uengine_init_context(uengine, ctx, 0);

	return 0;
}
EXPORT_SYMBOL(ixp2000_uengine_load);


static int __init ixp2000_uengine_init(void)
{
	int uengine;
	u32 value;

	/*
	 * Determine number of microengines present.
	 */
	switch ((ixp2000_reg_read(IXP2000_PRODUCT_ID) >> 8) & 0x1fff) {
	case 0:		/* IXP2800 */
	case 1:		/* IXP2850 */
		ixp2000_uengine_mask = 0x00ff00ff;
		break;

	case 2:		/* IXP2400 */
		ixp2000_uengine_mask = 0x000f000f;
		break;

	default:
		printk(KERN_INFO "Detected unknown IXP2000 model (%.8x)\n",
			(unsigned int)ixp2000_reg_read(IXP2000_PRODUCT_ID));
		ixp2000_uengine_mask = 0x00000000;
		break;
	}

	/*
	 * Reset microengines.
	 */
	ixp2000_uengine_reset(ixp2000_uengine_mask);

	/*
	 * Synchronise timestamp counters across all microengines.
	 */
	value = ixp2000_reg_read(IXP2000_MISC_CONTROL);
	ixp2000_reg_wrb(IXP2000_MISC_CONTROL, value & ~0x80);
	for (uengine = 0; uengine < 32; uengine++) {
		if (ixp2000_uengine_mask & (1 << uengine)) {
			ixp2000_uengine_csr_write(uengine, TIMESTAMP_LOW, 0);
			ixp2000_uengine_csr_write(uengine, TIMESTAMP_HIGH, 0);
		}
	}
	ixp2000_reg_wrb(IXP2000_MISC_CONTROL, value | 0x80);

	return 0;
}

subsys_initcall(ixp2000_uengine_init);
