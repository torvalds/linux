// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Encrypted Register State Support
 *
 * Author: Joerg Roedel <jroedel@suse.de>
 *
 * This file is not compiled stand-alone. It contains code shared
 * between the pre-decompression boot code and the running Linux kernel
 * and is included directly into both code-bases.
 */

/*
 * Boot VC Handler - This is the first VC handler during boot, there is no GHCB
 * page yet, so it only supports the MSR based communication with the
 * hypervisor and only the CPUID exit-code.
 */
void __init do_vc_no_ghcb(struct pt_regs *regs, unsigned long exit_code)
{
	unsigned int fn = lower_bits(regs->ax, 32);
	unsigned long val;

	/* Only CPUID is supported via MSR protocol */
	if (exit_code != SVM_EXIT_CPUID)
		goto fail;

	sev_es_wr_ghcb_msr(GHCB_CPUID_REQ(fn, GHCB_CPUID_REQ_EAX));
	VMGEXIT();
	val = sev_es_rd_ghcb_msr();
	if (GHCB_SEV_GHCB_RESP_CODE(val) != GHCB_SEV_CPUID_RESP)
		goto fail;
	regs->ax = val >> 32;

	sev_es_wr_ghcb_msr(GHCB_CPUID_REQ(fn, GHCB_CPUID_REQ_EBX));
	VMGEXIT();
	val = sev_es_rd_ghcb_msr();
	if (GHCB_SEV_GHCB_RESP_CODE(val) != GHCB_SEV_CPUID_RESP)
		goto fail;
	regs->bx = val >> 32;

	sev_es_wr_ghcb_msr(GHCB_CPUID_REQ(fn, GHCB_CPUID_REQ_ECX));
	VMGEXIT();
	val = sev_es_rd_ghcb_msr();
	if (GHCB_SEV_GHCB_RESP_CODE(val) != GHCB_SEV_CPUID_RESP)
		goto fail;
	regs->cx = val >> 32;

	sev_es_wr_ghcb_msr(GHCB_CPUID_REQ(fn, GHCB_CPUID_REQ_EDX));
	VMGEXIT();
	val = sev_es_rd_ghcb_msr();
	if (GHCB_SEV_GHCB_RESP_CODE(val) != GHCB_SEV_CPUID_RESP)
		goto fail;
	regs->dx = val >> 32;

	/* Skip over the CPUID two-byte opcode */
	regs->ip += 2;

	return;

fail:
	sev_es_wr_ghcb_msr(GHCB_SEV_TERMINATE);
	VMGEXIT();

	/* Shouldn't get here - if we do halt the machine */
	while (true)
		asm volatile("hlt\n");
}
