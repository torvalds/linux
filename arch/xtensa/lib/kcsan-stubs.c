// SPDX-License-Identifier: GPL-2.0

#include <linux/bug.h>
#include <linux/types.h>

void __atomic_store_8(volatile void *p, u64 v, int i)
{
	BUG();
}

u64 __atomic_load_8(const volatile void *p, int i)
{
	BUG();
}

u64 __atomic_exchange_8(volatile void *p, u64 v, int i)
{
	BUG();
}

bool __atomic_compare_exchange_8(volatile void *p1, void *p2, u64 v, bool b, int i1, int i2)
{
	BUG();
}

u64 __atomic_fetch_add_8(volatile void *p, u64 v, int i)
{
	BUG();
}

u64 __atomic_fetch_sub_8(volatile void *p, u64 v, int i)
{
	BUG();
}

u64 __atomic_fetch_and_8(volatile void *p, u64 v, int i)
{
	BUG();
}

u64 __atomic_fetch_or_8(volatile void *p, u64 v, int i)
{
	BUG();
}

u64 __atomic_fetch_xor_8(volatile void *p, u64 v, int i)
{
	BUG();
}

u64 __atomic_fetch_nand_8(volatile void *p, u64 v, int i)
{
	BUG();
}
