// SPDX-License-Identifier: GPL-2.0-only
/*
 * Erratas to be applied for Andes CPU cores
 *
 *  Copyright (C) 2023 Renesas Electronics Corporation.
 *
 * Author: Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>
 */

#include <linux/memory.h>
#include <linux/module.h>

#include <asm/alternative.h>
#include <asm/cacheflush.h>
#include <asm/errata_list.h>
#include <asm/patch.h>
#include <asm/processor.h>
#include <asm/sbi.h>
#include <asm/vendorid_list.h>

#define ANDESTECH_AX45MP_MARCHID	0x8000000000008a45UL
#define ANDESTECH_AX45MP_MIMPID		0x500UL
#define ANDESTECH_SBI_EXT_ANDES		0x0900031E

#define ANDES_SBI_EXT_IOCP_SW_WORKAROUND	1

static long ax45mp_iocp_sw_workaround(void)
{
	struct sbiret ret;

	/*
	 * ANDES_SBI_EXT_IOCP_SW_WORKAROUND SBI EXT checks if the IOCP is missing and
	 * cache is controllable only then CMO will be applied to the platform.
	 */
	ret = sbi_ecall(ANDESTECH_SBI_EXT_ANDES, ANDES_SBI_EXT_IOCP_SW_WORKAROUND,
			0, 0, 0, 0, 0, 0);

	return ret.error ? 0 : ret.value;
}

static void errata_probe_iocp(unsigned int stage, unsigned long arch_id, unsigned long impid)
{
	static bool done;

	if (!IS_ENABLED(CONFIG_ERRATA_ANDES_CMO))
		return;

	if (done)
		return;

	done = true;

	if (arch_id != ANDESTECH_AX45MP_MARCHID || impid != ANDESTECH_AX45MP_MIMPID)
		return;

	if (!ax45mp_iocp_sw_workaround())
		return;

	/* Set this just to make core cbo code happy */
	riscv_cbom_block_size = 1;
	riscv_noncoherent_supported();
}

void __init_or_module andes_errata_patch_func(struct alt_entry *begin, struct alt_entry *end,
					      unsigned long archid, unsigned long impid,
					      unsigned int stage)
{
	if (stage == RISCV_ALTERNATIVES_BOOT)
		errata_probe_iocp(stage, archid, impid);

	/* we have nothing to patch here ATM so just return back */
}
