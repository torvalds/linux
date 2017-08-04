/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include "nfp_cpp.h"
#include "nfp6000/nfp6000.h"

struct nfp_cpp_mutex {
	struct nfp_cpp *cpp;
	int target;
	u16 depth;
	unsigned long long address;
	u32 key;
};

static u32 nfp_mutex_locked(u16 interface)
{
	return (u32)interface << 16 | 0x000f;
}

static u32 nfp_mutex_unlocked(u16 interface)
{
	return (u32)interface << 16 | 0x0000;
}

static bool nfp_mutex_is_locked(u32 val)
{
	return (val & 0xffff) == 0x000f;
}

static bool nfp_mutex_is_unlocked(u32 val)
{
	return (val & 0xffff) == 0000;
}

/* If you need more than 65536 recursive locks, please rethink your code. */
#define NFP_MUTEX_DEPTH_MAX         0xffff

static int
nfp_cpp_mutex_validate(u16 interface, int *target, unsigned long long address)
{
	/* Not permitted on invalid interfaces */
	if (NFP_CPP_INTERFACE_TYPE_of(interface) ==
	    NFP_CPP_INTERFACE_TYPE_INVALID)
		return -EINVAL;

	/* Address must be 64-bit aligned */
	if (address & 7)
		return -EINVAL;

	if (*target != NFP_CPP_TARGET_MU)
		return -EINVAL;

	return 0;
}

/**
 * nfp_cpp_mutex_init() - Initialize a mutex location
 * @cpp:	NFP CPP handle
 * @target:	NFP CPP target ID (ie NFP_CPP_TARGET_CLS or NFP_CPP_TARGET_MU)
 * @address:	Offset into the address space of the NFP CPP target ID
 * @key:	Unique 32-bit value for this mutex
 *
 * The CPP target:address must point to a 64-bit aligned location, and
 * will initialize 64 bits of data at the location.
 *
 * This creates the initial mutex state, as locked by this
 * nfp_cpp_interface().
 *
 * This function should only be called when setting up
 * the initial lock state upon boot-up of the system.
 *
 * Return: 0 on success, or -errno on failure
 */
int nfp_cpp_mutex_init(struct nfp_cpp *cpp,
		       int target, unsigned long long address, u32 key)
{
	const u32 muw = NFP_CPP_ID(target, 4, 0);    /* atomic_write */
	u16 interface = nfp_cpp_interface(cpp);
	int err;

	err = nfp_cpp_mutex_validate(interface, &target, address);
	if (err)
		return err;

	err = nfp_cpp_writel(cpp, muw, address + 4, key);
	if (err)
		return err;

	err = nfp_cpp_writel(cpp, muw, address, nfp_mutex_locked(interface));
	if (err)
		return err;

	return 0;
}

/**
 * nfp_cpp_mutex_alloc() - Create a mutex handle
 * @cpp:	NFP CPP handle
 * @target:	NFP CPP target ID (ie NFP_CPP_TARGET_CLS or NFP_CPP_TARGET_MU)
 * @address:	Offset into the address space of the NFP CPP target ID
 * @key:	32-bit unique key (must match the key at this location)
 *
 * The CPP target:address must point to a 64-bit aligned location, and
 * reserve 64 bits of data at the location for use by the handle.
 *
 * Only target/address pairs that point to entities that support the
 * MU Atomic Engine's CmpAndSwap32 command are supported.
 *
 * Return:	A non-NULL struct nfp_cpp_mutex * on success, NULL on failure.
 */
struct nfp_cpp_mutex *nfp_cpp_mutex_alloc(struct nfp_cpp *cpp, int target,
					  unsigned long long address, u32 key)
{
	const u32 mur = NFP_CPP_ID(target, 3, 0);    /* atomic_read */
	u16 interface = nfp_cpp_interface(cpp);
	struct nfp_cpp_mutex *mutex;
	int err;
	u32 tmp;

	err = nfp_cpp_mutex_validate(interface, &target, address);
	if (err)
		return NULL;

	err = nfp_cpp_readl(cpp, mur, address + 4, &tmp);
	if (err < 0)
		return NULL;

	if (tmp != key)
		return NULL;

	mutex = kzalloc(sizeof(*mutex), GFP_KERNEL);
	if (!mutex)
		return NULL;

	mutex->cpp = cpp;
	mutex->target = target;
	mutex->address = address;
	mutex->key = key;
	mutex->depth = 0;

	return mutex;
}

/**
 * nfp_cpp_mutex_free() - Free a mutex handle - does not alter the lock state
 * @mutex:	NFP CPP Mutex handle
 */
void nfp_cpp_mutex_free(struct nfp_cpp_mutex *mutex)
{
	kfree(mutex);
}

/**
 * nfp_cpp_mutex_lock() - Lock a mutex handle, using the NFP MU Atomic Engine
 * @mutex:	NFP CPP Mutex handle
 *
 * Return: 0 on success, or -errno on failure
 */
int nfp_cpp_mutex_lock(struct nfp_cpp_mutex *mutex)
{
	unsigned long warn_at = jiffies + NFP_MUTEX_WAIT_FIRST_WARN * HZ;
	unsigned long err_at = jiffies + NFP_MUTEX_WAIT_ERROR * HZ;
	unsigned int timeout_ms = 1;
	int err;

	/* We can't use a waitqueue here, because the unlocker
	 * might be on a separate CPU.
	 *
	 * So just wait for now.
	 */
	for (;;) {
		err = nfp_cpp_mutex_trylock(mutex);
		if (err != -EBUSY)
			break;

		err = msleep_interruptible(timeout_ms);
		if (err != 0)
			return -ERESTARTSYS;

		if (time_is_before_eq_jiffies(warn_at)) {
			warn_at = jiffies + NFP_MUTEX_WAIT_NEXT_WARN * HZ;
			nfp_warn(mutex->cpp,
				 "Warning: waiting for NFP mutex [depth:%hd target:%d addr:%llx key:%08x]\n",
				 mutex->depth,
				 mutex->target, mutex->address, mutex->key);
		}
		if (time_is_before_eq_jiffies(err_at)) {
			nfp_err(mutex->cpp, "Error: mutex wait timed out\n");
			return -EBUSY;
		}
	}

	return err;
}

/**
 * nfp_cpp_mutex_unlock() - Unlock a mutex handle, using the MU Atomic Engine
 * @mutex:	NFP CPP Mutex handle
 *
 * Return: 0 on success, or -errno on failure
 */
int nfp_cpp_mutex_unlock(struct nfp_cpp_mutex *mutex)
{
	const u32 muw = NFP_CPP_ID(mutex->target, 4, 0);    /* atomic_write */
	const u32 mur = NFP_CPP_ID(mutex->target, 3, 0);    /* atomic_read */
	struct nfp_cpp *cpp = mutex->cpp;
	u32 key, value;
	u16 interface;
	int err;

	interface = nfp_cpp_interface(cpp);

	if (mutex->depth > 1) {
		mutex->depth--;
		return 0;
	}

	err = nfp_cpp_readl(mutex->cpp, mur, mutex->address + 4, &key);
	if (err < 0)
		return err;

	if (key != mutex->key)
		return -EPERM;

	err = nfp_cpp_readl(mutex->cpp, mur, mutex->address, &value);
	if (err < 0)
		return err;

	if (value != nfp_mutex_locked(interface))
		return -EACCES;

	err = nfp_cpp_writel(cpp, muw, mutex->address,
			     nfp_mutex_unlocked(interface));
	if (err < 0)
		return err;

	mutex->depth = 0;
	return 0;
}

/**
 * nfp_cpp_mutex_trylock() - Attempt to lock a mutex handle
 * @mutex:	NFP CPP Mutex handle
 *
 * Return:      0 if the lock succeeded, -errno on failure
 */
int nfp_cpp_mutex_trylock(struct nfp_cpp_mutex *mutex)
{
	const u32 muw = NFP_CPP_ID(mutex->target, 4, 0);    /* atomic_write */
	const u32 mus = NFP_CPP_ID(mutex->target, 5, 3);    /* test_set_imm */
	const u32 mur = NFP_CPP_ID(mutex->target, 3, 0);    /* atomic_read */
	struct nfp_cpp *cpp = mutex->cpp;
	u32 key, value, tmp;
	int err;

	if (mutex->depth > 0) {
		if (mutex->depth == NFP_MUTEX_DEPTH_MAX)
			return -E2BIG;
		mutex->depth++;
		return 0;
	}

	/* Verify that the lock marker is not damaged */
	err = nfp_cpp_readl(cpp, mur, mutex->address + 4, &key);
	if (err < 0)
		return err;

	if (key != mutex->key)
		return -EPERM;

	/* Compare against the unlocked state, and if true,
	 * write the interface id into the top 16 bits, and
	 * mark as locked.
	 */
	value = nfp_mutex_locked(nfp_cpp_interface(cpp));

	/* We use test_set_imm here, as it implies a read
	 * of the current state, and sets the bits in the
	 * bytemask of the command to 1s. Since the mutex
	 * is guaranteed to be 64-bit aligned, the bytemask
	 * of this 32-bit command is ensured to be 8'b00001111,
	 * which implies that the lower 4 bits will be set to
	 * ones regardless of the initial state.
	 *
	 * Since this is a 'Readback' operation, with no Pull
	 * data, we can treat this as a normal Push (read)
	 * atomic, which returns the original value.
	 */
	err = nfp_cpp_readl(cpp, mus, mutex->address, &tmp);
	if (err < 0)
		return err;

	/* Was it unlocked? */
	if (nfp_mutex_is_unlocked(tmp)) {
		/* The read value can only be 0x....0000 in the unlocked state.
		 * If there was another contending for this lock, then
		 * the lock state would be 0x....000f
		 */

		/* Write our owner ID into the lock
		 * While not strictly necessary, this helps with
		 * debug and bookkeeping.
		 */
		err = nfp_cpp_writel(cpp, muw, mutex->address, value);
		if (err < 0)
			return err;

		mutex->depth = 1;
		return 0;
	}

	return nfp_mutex_is_locked(tmp) ? -EBUSY : -EINVAL;
}
