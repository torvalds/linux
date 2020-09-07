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

static void sev_es_terminate(unsigned int reason)
{
	u64 val = GHCB_SEV_TERMINATE;

	/*
	 * Tell the hypervisor what went wrong - only reason-set 0 is
	 * currently supported.
	 */
	val |= GHCB_SEV_TERMINATE_REASON(0, reason);

	/* Request Guest Termination from Hypvervisor */
	sev_es_wr_ghcb_msr(val);
	VMGEXIT();

	while (true)
		asm volatile("hlt\n" : : : "memory");
}

static bool sev_es_negotiate_protocol(void)
{
	u64 val;

	/* Do the GHCB protocol version negotiation */
	sev_es_wr_ghcb_msr(GHCB_SEV_INFO_REQ);
	VMGEXIT();
	val = sev_es_rd_ghcb_msr();

	if (GHCB_INFO(val) != GHCB_SEV_INFO)
		return false;

	if (GHCB_PROTO_MAX(val) < GHCB_PROTO_OUR ||
	    GHCB_PROTO_MIN(val) > GHCB_PROTO_OUR)
		return false;

	return true;
}

static void vc_ghcb_invalidate(struct ghcb *ghcb)
{
	memset(ghcb->save.valid_bitmap, 0, sizeof(ghcb->save.valid_bitmap));
}

static bool vc_decoding_needed(unsigned long exit_code)
{
	/* Exceptions don't require to decode the instruction */
	return !(exit_code >= SVM_EXIT_EXCP_BASE &&
		 exit_code <= SVM_EXIT_LAST_EXCP);
}

static enum es_result vc_init_em_ctxt(struct es_em_ctxt *ctxt,
				      struct pt_regs *regs,
				      unsigned long exit_code)
{
	enum es_result ret = ES_OK;

	memset(ctxt, 0, sizeof(*ctxt));
	ctxt->regs = regs;

	if (vc_decoding_needed(exit_code))
		ret = vc_decode_insn(ctxt);

	return ret;
}

static void vc_finish_insn(struct es_em_ctxt *ctxt)
{
	ctxt->regs->ip += ctxt->insn.length;
}

static enum es_result sev_es_ghcb_hv_call(struct ghcb *ghcb,
					  struct es_em_ctxt *ctxt,
					  u64 exit_code, u64 exit_info_1,
					  u64 exit_info_2)
{
	enum es_result ret;

	/* Fill in protocol and format specifiers */
	ghcb->protocol_version = GHCB_PROTOCOL_MAX;
	ghcb->ghcb_usage       = GHCB_DEFAULT_USAGE;

	ghcb_set_sw_exit_code(ghcb, exit_code);
	ghcb_set_sw_exit_info_1(ghcb, exit_info_1);
	ghcb_set_sw_exit_info_2(ghcb, exit_info_2);

	sev_es_wr_ghcb_msr(__pa(ghcb));
	VMGEXIT();

	if ((ghcb->save.sw_exit_info_1 & 0xffffffff) == 1) {
		u64 info = ghcb->save.sw_exit_info_2;
		unsigned long v;

		info = ghcb->save.sw_exit_info_2;
		v = info & SVM_EVTINJ_VEC_MASK;

		/* Check if exception information from hypervisor is sane. */
		if ((info & SVM_EVTINJ_VALID) &&
		    ((v == X86_TRAP_GP) || (v == X86_TRAP_UD)) &&
		    ((info & SVM_EVTINJ_TYPE_MASK) == SVM_EVTINJ_TYPE_EXEPT)) {
			ctxt->fi.vector = v;
			if (info & SVM_EVTINJ_VALID_ERR)
				ctxt->fi.error_code = info >> 32;
			ret = ES_EXCEPTION;
		} else {
			ret = ES_VMM_ERROR;
		}
	} else {
		ret = ES_OK;
	}

	return ret;
}

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

static enum es_result vc_insn_string_read(struct es_em_ctxt *ctxt,
					  void *src, char *buf,
					  unsigned int data_size,
					  unsigned int count,
					  bool backwards)
{
	int i, b = backwards ? -1 : 1;
	enum es_result ret = ES_OK;

	for (i = 0; i < count; i++) {
		void *s = src + (i * data_size * b);
		char *d = buf + (i * data_size);

		ret = vc_read_mem(ctxt, s, d, data_size);
		if (ret != ES_OK)
			break;
	}

	return ret;
}

static enum es_result vc_insn_string_write(struct es_em_ctxt *ctxt,
					   void *dst, char *buf,
					   unsigned int data_size,
					   unsigned int count,
					   bool backwards)
{
	int i, s = backwards ? -1 : 1;
	enum es_result ret = ES_OK;

	for (i = 0; i < count; i++) {
		void *d = dst + (i * data_size * s);
		char *b = buf + (i * data_size);

		ret = vc_write_mem(ctxt, d, b, data_size);
		if (ret != ES_OK)
			break;
	}

	return ret;
}
