// SPDX-License-Identifier: GPL-2.0
/*
 *  Standard user space access functions based on mvcp/mvcs and doing
 *  interesting things in the secondary space mode.
 *
 *    Copyright IBM Corp. 2006,2014
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *		 Gerald Schaefer (gerald.schaefer@de.ibm.com)
 */

#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <asm/asm-extable.h>
#include <asm/ctlreg.h>
#include <asm/skey.h>

#ifdef CONFIG_DEBUG_ENTRY
void debug_user_asce(int exit)
{
	struct lowcore *lc = get_lowcore();
	struct ctlreg cr1, cr7;

	local_ctl_store(1, &cr1);
	local_ctl_store(7, &cr7);
	if (cr1.val == lc->user_asce.val && cr7.val == lc->user_asce.val)
		return;
	panic("incorrect ASCE on kernel %s\n"
	      "cr1:    %016lx cr7:  %016lx\n"
	      "kernel: %016lx user: %016lx\n",
	      exit ? "exit" : "entry", cr1.val, cr7.val,
	      lc->kernel_asce.val, lc->user_asce.val);
}
#endif /*CONFIG_DEBUG_ENTRY */

#define CMPXCHG_USER_KEY_MAX_LOOPS 128

static nokprobe_inline int __cmpxchg_key_small(void *address, unsigned int *uval,
					       unsigned int old, unsigned int new,
					       unsigned int mask, unsigned long key)
{
	unsigned long count;
	unsigned int prev;
	int rc = 0;

	skey_regions_initialize();
	asm_inline volatile(
		"20:	spka	0(%[key])\n"
		"	llill	%[count],%[max_loops]\n"
		"0:	l	%[prev],%[address]\n"
		"1:	nr	%[prev],%[mask]\n"
		"	xilf	%[mask],0xffffffff\n"
		"	or	%[new],%[prev]\n"
		"	or	%[prev],%[tmp]\n"
		"2:	lr	%[tmp],%[prev]\n"
		"3:	cs	%[prev],%[new],%[address]\n"
		"4:	jnl	5f\n"
		"	xr	%[tmp],%[prev]\n"
		"	xr	%[new],%[tmp]\n"
		"	nr	%[tmp],%[mask]\n"
		"	jnz	5f\n"
		"	brct	%[count],2b\n"
		"5:	spka	%[default_key]\n"
		"21:\n"
		EX_TABLE_UA_LOAD_REG(0b, 5b, %[rc], %[prev])
		EX_TABLE_UA_LOAD_REG(1b, 5b, %[rc], %[prev])
		EX_TABLE_UA_LOAD_REG(3b, 5b, %[rc], %[prev])
		EX_TABLE_UA_LOAD_REG(4b, 5b, %[rc], %[prev])
		SKEY_REGION(20b, 21b)
		: [rc] "+&d" (rc),
		[prev] "=&d" (prev),
		[address] "+Q" (*(int *)address),
		[tmp] "+&d" (old),
		[new] "+&d" (new),
		[mask] "+&d" (mask),
		[count] "=a" (count)
		: [key] "%[count]" (key << 4),
		[default_key] "J" (PAGE_DEFAULT_KEY),
		[max_loops] "J" (CMPXCHG_USER_KEY_MAX_LOOPS)
		: "memory", "cc");
	*uval = prev;
	if (!count)
		rc = -EAGAIN;
	return rc;
}

int __kprobes __cmpxchg_key1(void *addr, unsigned char *uval, unsigned char old,
			     unsigned char new, unsigned long key)
{
	unsigned long address = (unsigned long)addr;
	unsigned int prev, shift, mask, _old, _new;
	int rc;

	shift = (3 ^ (address & 3)) << 3;
	address ^= address & 3;
	_old = (unsigned int)old << shift;
	_new = (unsigned int)new << shift;
	mask = ~(0xff << shift);
	rc = __cmpxchg_key_small((void *)address, &prev, _old, _new, mask, key);
	*uval = prev >> shift;
	return rc;
}
EXPORT_SYMBOL(__cmpxchg_key1);

int __kprobes __cmpxchg_key2(void *addr, unsigned short *uval, unsigned short old,
			     unsigned short new, unsigned long key)
{
	unsigned long address = (unsigned long)addr;
	unsigned int prev, shift, mask, _old, _new;
	int rc;

	shift = (2 ^ (address & 2)) << 3;
	address ^= address & 2;
	_old = (unsigned int)old << shift;
	_new = (unsigned int)new << shift;
	mask = ~(0xffff << shift);
	rc = __cmpxchg_key_small((void *)address, &prev, _old, _new, mask, key);
	*uval = prev >> shift;
	return rc;
}
EXPORT_SYMBOL(__cmpxchg_key2);

int __kprobes __cmpxchg_key4(void *address, unsigned int *uval, unsigned int old,
			     unsigned int new, unsigned long key)
{
	unsigned int prev = old;
	int rc = 0;

	skey_regions_initialize();
	asm_inline volatile(
		"20:	spka	0(%[key])\n"
		"0:	cs	%[prev],%[new],%[address]\n"
		"1:	spka	%[default_key]\n"
		"21:\n"
		EX_TABLE_UA_LOAD_REG(0b, 1b, %[rc], %[prev])
		EX_TABLE_UA_LOAD_REG(1b, 1b, %[rc], %[prev])
		SKEY_REGION(20b, 21b)
		: [rc] "+&d" (rc),
		[prev] "+&d" (prev),
		[address] "+Q" (*(int *)address)
		: [new] "d" (new),
		[key] "a" (key << 4),
		[default_key] "J" (PAGE_DEFAULT_KEY)
		: "memory", "cc");
	*uval = prev;
	return rc;
}
EXPORT_SYMBOL(__cmpxchg_key4);

int __kprobes __cmpxchg_key8(void *address, unsigned long *uval, unsigned long old,
			     unsigned long new, unsigned long key)
{
	unsigned long prev = old;
	int rc = 0;

	skey_regions_initialize();
	asm_inline volatile(
		"20:	spka	0(%[key])\n"
		"0:	csg	%[prev],%[new],%[address]\n"
		"1:	spka	%[default_key]\n"
		"21:\n"
		EX_TABLE_UA_LOAD_REG(0b, 1b, %[rc], %[prev])
		EX_TABLE_UA_LOAD_REG(1b, 1b, %[rc], %[prev])
		SKEY_REGION(20b, 21b)
		: [rc] "+&d" (rc),
		[prev] "+&d" (prev),
		[address] "+QS" (*(long *)address)
		: [new] "d" (new),
		[key] "a" (key << 4),
		[default_key] "J" (PAGE_DEFAULT_KEY)
		: "memory", "cc");
	*uval = prev;
	return rc;
}
EXPORT_SYMBOL(__cmpxchg_key8);

int __kprobes __cmpxchg_key16(void *address, __uint128_t *uval, __uint128_t old,
			      __uint128_t new, unsigned long key)
{
	__uint128_t prev = old;
	int rc = 0;

	skey_regions_initialize();
	asm_inline volatile(
		"20:	spka	0(%[key])\n"
		"0:	cdsg	%[prev],%[new],%[address]\n"
		"1:	spka	%[default_key]\n"
		"21:\n"
		EX_TABLE_UA_LOAD_REGPAIR(0b, 1b, %[rc], %[prev])
		EX_TABLE_UA_LOAD_REGPAIR(1b, 1b, %[rc], %[prev])
		SKEY_REGION(20b, 21b)
		: [rc] "+&d" (rc),
		[prev] "+&d" (prev),
		[address] "+QS" (*(__int128_t *)address)
		: [new] "d" (new),
		[key] "a" (key << 4),
		[default_key] "J" (PAGE_DEFAULT_KEY)
		: "memory", "cc");
	*uval = prev;
	return rc;
}
EXPORT_SYMBOL(__cmpxchg_key16);
