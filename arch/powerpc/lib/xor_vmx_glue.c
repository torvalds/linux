/*
 * Altivec XOR operations
 *
 * Copyright 2017 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/preempt.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <asm/switch_to.h>
#include "xor_vmx.h"

void xor_altivec_2(unsigned long bytes, unsigned long *v1_in,
		   unsigned long *v2_in)
{
	preempt_disable();
	enable_kernel_altivec();
	__xor_altivec_2(bytes, v1_in, v2_in);
	disable_kernel_altivec();
	preempt_enable();
}
EXPORT_SYMBOL(xor_altivec_2);

void xor_altivec_3(unsigned long bytes,  unsigned long *v1_in,
		   unsigned long *v2_in, unsigned long *v3_in)
{
	preempt_disable();
	enable_kernel_altivec();
	__xor_altivec_3(bytes, v1_in, v2_in, v3_in);
	disable_kernel_altivec();
	preempt_enable();
}
EXPORT_SYMBOL(xor_altivec_3);

void xor_altivec_4(unsigned long bytes,  unsigned long *v1_in,
		   unsigned long *v2_in, unsigned long *v3_in,
		   unsigned long *v4_in)
{
	preempt_disable();
	enable_kernel_altivec();
	__xor_altivec_4(bytes, v1_in, v2_in, v3_in, v4_in);
	disable_kernel_altivec();
	preempt_enable();
}
EXPORT_SYMBOL(xor_altivec_4);

void xor_altivec_5(unsigned long bytes,  unsigned long *v1_in,
		   unsigned long *v2_in, unsigned long *v3_in,
		   unsigned long *v4_in, unsigned long *v5_in)
{
	preempt_disable();
	enable_kernel_altivec();
	__xor_altivec_5(bytes, v1_in, v2_in, v3_in, v4_in, v5_in);
	disable_kernel_altivec();
	preempt_enable();
}
EXPORT_SYMBOL(xor_altivec_5);
