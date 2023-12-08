// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2023 Intel Corporation.
 *
 * Intel Trusted Domain Extensions (TDX) support
 */

#define pr_fmt(fmt)	"virt/tdx: " fmt

#include <linux/types.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <asm/msr-index.h>
#include <asm/msr.h>
#include <asm/cpufeature.h>
#include <asm/tdx.h>

static u32 tdx_global_keyid __ro_after_init;
static u32 tdx_guest_keyid_start __ro_after_init;
static u32 tdx_nr_guest_keyids __ro_after_init;

typedef void (*sc_err_func_t)(u64 fn, u64 err, struct tdx_module_args *args);

static inline void seamcall_err(u64 fn, u64 err, struct tdx_module_args *args)
{
	pr_err("SEAMCALL (0x%016llx) failed: 0x%016llx\n", fn, err);
}

static inline void seamcall_err_ret(u64 fn, u64 err,
				    struct tdx_module_args *args)
{
	seamcall_err(fn, err, args);
	pr_err("RCX 0x%016llx RDX 0x%016llx R08 0x%016llx\n",
			args->rcx, args->rdx, args->r8);
	pr_err("R09 0x%016llx R10 0x%016llx R11 0x%016llx\n",
			args->r9, args->r10, args->r11);
}

static inline int sc_retry_prerr(sc_func_t func, sc_err_func_t err_func,
				 u64 fn, struct tdx_module_args *args)
{
	u64 sret = sc_retry(func, fn, args);

	if (sret == TDX_SUCCESS)
		return 0;

	if (sret == TDX_SEAMCALL_VMFAILINVALID)
		return -ENODEV;

	if (sret == TDX_SEAMCALL_GP)
		return -EOPNOTSUPP;

	if (sret == TDX_SEAMCALL_UD)
		return -EACCES;

	err_func(fn, sret, args);
	return -EIO;
}

#define seamcall_prerr(__fn, __args)						\
	sc_retry_prerr(__seamcall, seamcall_err, (__fn), (__args))

#define seamcall_prerr_ret(__fn, __args)					\
	sc_retry_prerr(__seamcall_ret, seamcall_err_ret, (__fn), (__args))

static __init int record_keyid_partitioning(u32 *tdx_keyid_start,
					    u32 *nr_tdx_keyids)
{
	u32 _nr_mktme_keyids, _tdx_keyid_start, _nr_tdx_keyids;
	int ret;

	/*
	 * IA32_MKTME_KEYID_PARTIONING:
	 *   Bit [31:0]:	Number of MKTME KeyIDs.
	 *   Bit [63:32]:	Number of TDX private KeyIDs.
	 */
	ret = rdmsr_safe(MSR_IA32_MKTME_KEYID_PARTITIONING, &_nr_mktme_keyids,
			&_nr_tdx_keyids);
	if (ret || !_nr_tdx_keyids)
		return -EINVAL;

	/* TDX KeyIDs start after the last MKTME KeyID. */
	_tdx_keyid_start = _nr_mktme_keyids + 1;

	*tdx_keyid_start = _tdx_keyid_start;
	*nr_tdx_keyids = _nr_tdx_keyids;

	return 0;
}

void __init tdx_init(void)
{
	u32 tdx_keyid_start, nr_tdx_keyids;
	int err;

	err = record_keyid_partitioning(&tdx_keyid_start, &nr_tdx_keyids);
	if (err)
		return;

	pr_info("BIOS enabled: private KeyID range [%u, %u)\n",
			tdx_keyid_start, tdx_keyid_start + nr_tdx_keyids);

	/*
	 * The TDX module itself requires one 'global KeyID' to protect
	 * its metadata.  If there's only one TDX KeyID, there won't be
	 * any left for TDX guests thus there's no point to enable TDX
	 * at all.
	 */
	if (nr_tdx_keyids < 2) {
		pr_err("initialization failed: too few private KeyIDs available.\n");
		return;
	}

	/*
	 * Just use the first TDX KeyID as the 'global KeyID' and
	 * leave the rest for TDX guests.
	 */
	tdx_global_keyid = tdx_keyid_start;
	tdx_guest_keyid_start = tdx_keyid_start + 1;
	tdx_nr_guest_keyids = nr_tdx_keyids - 1;

	setup_force_cpu_cap(X86_FEATURE_TDX_HOST_PLATFORM);
}
