// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2019 SUSE
 *
 * Author: Joerg Roedel <jroedel@suse.de>
 */

#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/sev-es.h>
#include <asm/insn-eval.h>
#include <asm/fpu/internal.h>
#include <asm/processor.h>
#include <asm/trap_pf.h>
#include <asm/trapnr.h>
#include <asm/svm.h>

static inline u64 sev_es_rd_ghcb_msr(void)
{
	return __rdmsr(MSR_AMD64_SEV_ES_GHCB);
}

static inline void sev_es_wr_ghcb_msr(u64 val)
{
	u32 low, high;

	low  = (u32)(val);
	high = (u32)(val >> 32);

	native_wrmsr(MSR_AMD64_SEV_ES_GHCB, low, high);
}

static int vc_fetch_insn_kernel(struct es_em_ctxt *ctxt,
				unsigned char *buffer)
{
	return copy_from_kernel_nofault(buffer, (unsigned char *)ctxt->regs->ip, MAX_INSN_SIZE);
}

static enum es_result vc_decode_insn(struct es_em_ctxt *ctxt)
{
	char buffer[MAX_INSN_SIZE];
	enum es_result ret;
	int res;

	res = vc_fetch_insn_kernel(ctxt, buffer);
	if (unlikely(res == -EFAULT)) {
		ctxt->fi.vector     = X86_TRAP_PF;
		ctxt->fi.error_code = 0;
		ctxt->fi.cr2        = ctxt->regs->ip;
		return ES_EXCEPTION;
	}

	insn_init(&ctxt->insn, buffer, MAX_INSN_SIZE - res, 1);
	insn_get_length(&ctxt->insn);

	ret = ctxt->insn.immediate.got ? ES_OK : ES_DECODE_FAILED;

	return ret;
}

static enum es_result vc_write_mem(struct es_em_ctxt *ctxt,
				   char *dst, char *buf, size_t size)
{
	unsigned long error_code = X86_PF_PROT | X86_PF_WRITE;
	char __user *target = (char __user *)dst;
	u64 d8;
	u32 d4;
	u16 d2;
	u8  d1;

	switch (size) {
	case 1:
		memcpy(&d1, buf, 1);
		if (put_user(d1, target))
			goto fault;
		break;
	case 2:
		memcpy(&d2, buf, 2);
		if (put_user(d2, target))
			goto fault;
		break;
	case 4:
		memcpy(&d4, buf, 4);
		if (put_user(d4, target))
			goto fault;
		break;
	case 8:
		memcpy(&d8, buf, 8);
		if (put_user(d8, target))
			goto fault;
		break;
	default:
		WARN_ONCE(1, "%s: Invalid size: %zu\n", __func__, size);
		return ES_UNSUPPORTED;
	}

	return ES_OK;

fault:
	if (user_mode(ctxt->regs))
		error_code |= X86_PF_USER;

	ctxt->fi.vector = X86_TRAP_PF;
	ctxt->fi.error_code = error_code;
	ctxt->fi.cr2 = (unsigned long)dst;

	return ES_EXCEPTION;
}

static enum es_result vc_read_mem(struct es_em_ctxt *ctxt,
				  char *src, char *buf, size_t size)
{
	unsigned long error_code = X86_PF_PROT;
	char __user *s = (char __user *)src;
	u64 d8;
	u32 d4;
	u16 d2;
	u8  d1;

	switch (size) {
	case 1:
		if (get_user(d1, s))
			goto fault;
		memcpy(buf, &d1, 1);
		break;
	case 2:
		if (get_user(d2, s))
			goto fault;
		memcpy(buf, &d2, 2);
		break;
	case 4:
		if (get_user(d4, s))
			goto fault;
		memcpy(buf, &d4, 4);
		break;
	case 8:
		if (get_user(d8, s))
			goto fault;
		memcpy(buf, &d8, 8);
		break;
	default:
		WARN_ONCE(1, "%s: Invalid size: %zu\n", __func__, size);
		return ES_UNSUPPORTED;
	}

	return ES_OK;

fault:
	if (user_mode(ctxt->regs))
		error_code |= X86_PF_USER;

	ctxt->fi.vector = X86_TRAP_PF;
	ctxt->fi.error_code = error_code;
	ctxt->fi.cr2 = (unsigned long)src;

	return ES_EXCEPTION;
}

/* Include code shared with pre-decompression boot stage */
#include "sev-es-shared.c"
