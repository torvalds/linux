// SPDX-License-Identifier: GPL-2.0

static enum es_result vc_check_opcode_bytes(struct es_em_ctxt *ctxt,
					    unsigned long exit_code)
{
	unsigned int opcode = (unsigned int)ctxt->insn.opcode.value;
	u8 modrm = ctxt->insn.modrm.value;

	switch (exit_code) {

	case SVM_EXIT_IOIO:
	case SVM_EXIT_NPF:
		/* handled separately */
		return ES_OK;

	case SVM_EXIT_CPUID:
		if (opcode == 0xa20f)
			return ES_OK;
		break;

	case SVM_EXIT_INVD:
		if (opcode == 0x080f)
			return ES_OK;
		break;

	case SVM_EXIT_MONITOR:
		/* MONITOR and MONITORX instructions generate the same error code */
		if (opcode == 0x010f && (modrm == 0xc8 || modrm == 0xfa))
			return ES_OK;
		break;

	case SVM_EXIT_MWAIT:
		/* MWAIT and MWAITX instructions generate the same error code */
		if (opcode == 0x010f && (modrm == 0xc9 || modrm == 0xfb))
			return ES_OK;
		break;

	case SVM_EXIT_MSR:
		/* RDMSR */
		if (opcode == 0x320f ||
		/* WRMSR */
		    opcode == 0x300f)
			return ES_OK;
		break;

	case SVM_EXIT_RDPMC:
		if (opcode == 0x330f)
			return ES_OK;
		break;

	case SVM_EXIT_RDTSC:
		if (opcode == 0x310f)
			return ES_OK;
		break;

	case SVM_EXIT_RDTSCP:
		if (opcode == 0x010f && modrm == 0xf9)
			return ES_OK;
		break;

	case SVM_EXIT_READ_DR7:
		if (opcode == 0x210f &&
		    X86_MODRM_REG(ctxt->insn.modrm.value) == 7)
			return ES_OK;
		break;

	case SVM_EXIT_VMMCALL:
		if (opcode == 0x010f && modrm == 0xd9)
			return ES_OK;

		break;

	case SVM_EXIT_WRITE_DR7:
		if (opcode == 0x230f &&
		    X86_MODRM_REG(ctxt->insn.modrm.value) == 7)
			return ES_OK;
		break;

	case SVM_EXIT_WBINVD:
		if (opcode == 0x90f)
			return ES_OK;
		break;

	default:
		break;
	}

	sev_printk(KERN_ERR "Wrong/unhandled opcode bytes: 0x%x, exit_code: 0x%lx, rIP: 0x%lx\n",
		   opcode, exit_code, ctxt->regs->ip);

	return ES_UNSUPPORTED;
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

static enum es_result vc_insn_string_check(struct es_em_ctxt *ctxt,
					   unsigned long address,
					   bool write)
{
	if (user_mode(ctxt->regs) && fault_in_kernel_space(address)) {
		ctxt->fi.vector     = X86_TRAP_PF;
		ctxt->fi.error_code = X86_PF_USER;
		ctxt->fi.cr2        = address;
		if (write)
			ctxt->fi.error_code |= X86_PF_WRITE;

		return ES_EXCEPTION;
	}

	return ES_OK;
}

static enum es_result vc_insn_string_read(struct es_em_ctxt *ctxt,
					  void *src, char *buf,
					  unsigned int data_size,
					  unsigned int count,
					  bool backwards)
{
	int i, b = backwards ? -1 : 1;
	unsigned long address = (unsigned long)src;
	enum es_result ret;

	ret = vc_insn_string_check(ctxt, address, false);
	if (ret != ES_OK)
		return ret;

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
	unsigned long address = (unsigned long)dst;
	enum es_result ret;

	ret = vc_insn_string_check(ctxt, address, true);
	if (ret != ES_OK)
		return ret;

	for (i = 0; i < count; i++) {
		void *d = dst + (i * data_size * s);
		char *b = buf + (i * data_size);

		ret = vc_write_mem(ctxt, d, b, data_size);
		if (ret != ES_OK)
			break;
	}

	return ret;
}

#define IOIO_TYPE_STR  BIT(2)
#define IOIO_TYPE_IN   1
#define IOIO_TYPE_INS  (IOIO_TYPE_IN | IOIO_TYPE_STR)
#define IOIO_TYPE_OUT  0
#define IOIO_TYPE_OUTS (IOIO_TYPE_OUT | IOIO_TYPE_STR)

#define IOIO_REP       BIT(3)

#define IOIO_ADDR_64   BIT(9)
#define IOIO_ADDR_32   BIT(8)
#define IOIO_ADDR_16   BIT(7)

#define IOIO_DATA_32   BIT(6)
#define IOIO_DATA_16   BIT(5)
#define IOIO_DATA_8    BIT(4)

#define IOIO_SEG_ES    (0 << 10)
#define IOIO_SEG_DS    (3 << 10)

static enum es_result vc_ioio_exitinfo(struct es_em_ctxt *ctxt, u64 *exitinfo)
{
	struct insn *insn = &ctxt->insn;
	size_t size;
	u64 port;

	*exitinfo = 0;

	switch (insn->opcode.bytes[0]) {
	/* INS opcodes */
	case 0x6c:
	case 0x6d:
		*exitinfo |= IOIO_TYPE_INS;
		*exitinfo |= IOIO_SEG_ES;
		port	   = ctxt->regs->dx & 0xffff;
		break;

	/* OUTS opcodes */
	case 0x6e:
	case 0x6f:
		*exitinfo |= IOIO_TYPE_OUTS;
		*exitinfo |= IOIO_SEG_DS;
		port	   = ctxt->regs->dx & 0xffff;
		break;

	/* IN immediate opcodes */
	case 0xe4:
	case 0xe5:
		*exitinfo |= IOIO_TYPE_IN;
		port	   = (u8)insn->immediate.value & 0xffff;
		break;

	/* OUT immediate opcodes */
	case 0xe6:
	case 0xe7:
		*exitinfo |= IOIO_TYPE_OUT;
		port	   = (u8)insn->immediate.value & 0xffff;
		break;

	/* IN register opcodes */
	case 0xec:
	case 0xed:
		*exitinfo |= IOIO_TYPE_IN;
		port	   = ctxt->regs->dx & 0xffff;
		break;

	/* OUT register opcodes */
	case 0xee:
	case 0xef:
		*exitinfo |= IOIO_TYPE_OUT;
		port	   = ctxt->regs->dx & 0xffff;
		break;

	default:
		return ES_DECODE_FAILED;
	}

	*exitinfo |= port << 16;

	switch (insn->opcode.bytes[0]) {
	case 0x6c:
	case 0x6e:
	case 0xe4:
	case 0xe6:
	case 0xec:
	case 0xee:
		/* Single byte opcodes */
		*exitinfo |= IOIO_DATA_8;
		size       = 1;
		break;
	default:
		/* Length determined by instruction parsing */
		*exitinfo |= (insn->opnd_bytes == 2) ? IOIO_DATA_16
						     : IOIO_DATA_32;
		size       = (insn->opnd_bytes == 2) ? 2 : 4;
	}

	switch (insn->addr_bytes) {
	case 2:
		*exitinfo |= IOIO_ADDR_16;
		break;
	case 4:
		*exitinfo |= IOIO_ADDR_32;
		break;
	case 8:
		*exitinfo |= IOIO_ADDR_64;
		break;
	}

	if (insn_has_rep_prefix(insn))
		*exitinfo |= IOIO_REP;

	return vc_ioio_check(ctxt, (u16)port, size);
}

static enum es_result vc_handle_ioio(struct ghcb *ghcb, struct es_em_ctxt *ctxt)
{
	struct pt_regs *regs = ctxt->regs;
	u64 exit_info_1, exit_info_2;
	enum es_result ret;

	ret = vc_ioio_exitinfo(ctxt, &exit_info_1);
	if (ret != ES_OK)
		return ret;

	if (exit_info_1 & IOIO_TYPE_STR) {

		/* (REP) INS/OUTS */

		bool df = ((regs->flags & X86_EFLAGS_DF) == X86_EFLAGS_DF);
		unsigned int io_bytes, exit_bytes;
		unsigned int ghcb_count, op_count;
		unsigned long es_base;
		u64 sw_scratch;

		/*
		 * For the string variants with rep prefix the amount of in/out
		 * operations per #VC exception is limited so that the kernel
		 * has a chance to take interrupts and re-schedule while the
		 * instruction is emulated.
		 */
		io_bytes   = (exit_info_1 >> 4) & 0x7;
		ghcb_count = sizeof(ghcb->shared_buffer) / io_bytes;

		op_count    = (exit_info_1 & IOIO_REP) ? regs->cx : 1;
		exit_info_2 = min(op_count, ghcb_count);
		exit_bytes  = exit_info_2 * io_bytes;

		es_base = insn_get_seg_base(ctxt->regs, INAT_SEG_REG_ES);

		/* Read bytes of OUTS into the shared buffer */
		if (!(exit_info_1 & IOIO_TYPE_IN)) {
			ret = vc_insn_string_read(ctxt,
					       (void *)(es_base + regs->si),
					       ghcb->shared_buffer, io_bytes,
					       exit_info_2, df);
			if (ret)
				return ret;
		}

		/*
		 * Issue an VMGEXIT to the HV to consume the bytes from the
		 * shared buffer or to have it write them into the shared buffer
		 * depending on the instruction: OUTS or INS.
		 */
		sw_scratch = __pa(ghcb) + offsetof(struct ghcb, shared_buffer);
		ghcb_set_sw_scratch(ghcb, sw_scratch);
		ret = sev_es_ghcb_hv_call(ghcb, ctxt, SVM_EXIT_IOIO,
					  exit_info_1, exit_info_2);
		if (ret != ES_OK)
			return ret;

		/* Read bytes from shared buffer into the guest's destination. */
		if (exit_info_1 & IOIO_TYPE_IN) {
			ret = vc_insn_string_write(ctxt,
						   (void *)(es_base + regs->di),
						   ghcb->shared_buffer, io_bytes,
						   exit_info_2, df);
			if (ret)
				return ret;

			if (df)
				regs->di -= exit_bytes;
			else
				regs->di += exit_bytes;
		} else {
			if (df)
				regs->si -= exit_bytes;
			else
				regs->si += exit_bytes;
		}

		if (exit_info_1 & IOIO_REP)
			regs->cx -= exit_info_2;

		ret = regs->cx ? ES_RETRY : ES_OK;

	} else {

		/* IN/OUT into/from rAX */

		int bits = (exit_info_1 & 0x70) >> 1;
		u64 rax = 0;

		if (!(exit_info_1 & IOIO_TYPE_IN))
			rax = lower_bits(regs->ax, bits);

		ghcb_set_rax(ghcb, rax);

		ret = sev_es_ghcb_hv_call(ghcb, ctxt, SVM_EXIT_IOIO, exit_info_1, 0);
		if (ret != ES_OK)
			return ret;

		if (exit_info_1 & IOIO_TYPE_IN) {
			if (!ghcb_rax_is_valid(ghcb))
				return ES_VMM_ERROR;
			regs->ax = lower_bits(ghcb->save.rax, bits);
		}
	}

	return ret;
}

enum es_result verify_exception_info(struct ghcb *ghcb, struct es_em_ctxt *ctxt)
{
	u32 ret;

	ret = ghcb->save.sw_exit_info_1 & GENMASK_ULL(31, 0);
	if (!ret)
		return ES_OK;

	if (ret == 1) {
		u64 info = ghcb->save.sw_exit_info_2;
		unsigned long v = info & SVM_EVTINJ_VEC_MASK;

		/* Check if exception information from hypervisor is sane. */
		if ((info & SVM_EVTINJ_VALID) &&
		    ((v == X86_TRAP_GP) || (v == X86_TRAP_UD)) &&
		    ((info & SVM_EVTINJ_TYPE_MASK) == SVM_EVTINJ_TYPE_EXEPT)) {
			ctxt->fi.vector = v;

			if (info & SVM_EVTINJ_VALID_ERR)
				ctxt->fi.error_code = info >> 32;

			return ES_EXCEPTION;
		}
	}

	return ES_VMM_ERROR;
}

enum es_result sev_es_ghcb_hv_call(struct ghcb *ghcb,
				   struct es_em_ctxt *ctxt,
				   u64 exit_code, u64 exit_info_1,
				   u64 exit_info_2)
{
	/* Fill in protocol and format specifiers */
	ghcb->protocol_version = ghcb_version;
	ghcb->ghcb_usage       = GHCB_DEFAULT_USAGE;

	ghcb_set_sw_exit_code(ghcb, exit_code);
	ghcb_set_sw_exit_info_1(ghcb, exit_info_1);
	ghcb_set_sw_exit_info_2(ghcb, exit_info_2);

	sev_es_wr_ghcb_msr(__pa(ghcb));
	VMGEXIT();

	return verify_exception_info(ghcb, ctxt);
}

static int __sev_cpuid_hv_ghcb(struct ghcb *ghcb, struct es_em_ctxt *ctxt, struct cpuid_leaf *leaf)
{
	u32 cr4 = native_read_cr4();
	int ret;

	ghcb_set_rax(ghcb, leaf->fn);
	ghcb_set_rcx(ghcb, leaf->subfn);

	if (cr4 & X86_CR4_OSXSAVE)
		/* Safe to read xcr0 */
		ghcb_set_xcr0(ghcb, xgetbv(XCR_XFEATURE_ENABLED_MASK));
	else
		/* xgetbv will cause #UD - use reset value for xcr0 */
		ghcb_set_xcr0(ghcb, 1);

	ret = sev_es_ghcb_hv_call(ghcb, ctxt, SVM_EXIT_CPUID, 0, 0);
	if (ret != ES_OK)
		return ret;

	if (!(ghcb_rax_is_valid(ghcb) &&
	      ghcb_rbx_is_valid(ghcb) &&
	      ghcb_rcx_is_valid(ghcb) &&
	      ghcb_rdx_is_valid(ghcb)))
		return ES_VMM_ERROR;

	leaf->eax = ghcb->save.rax;
	leaf->ebx = ghcb->save.rbx;
	leaf->ecx = ghcb->save.rcx;
	leaf->edx = ghcb->save.rdx;

	return ES_OK;
}

struct cpuid_ctx {
	struct ghcb *ghcb;
	struct es_em_ctxt *ctxt;
};

static void snp_cpuid_hv_ghcb(void *p, struct cpuid_leaf *leaf)
{
	struct cpuid_ctx *ctx = p;

	if (__sev_cpuid_hv_ghcb(ctx->ghcb, ctx->ctxt, leaf))
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_CPUID_HV);
}

static int vc_handle_cpuid_snp(struct ghcb *ghcb, struct es_em_ctxt *ctxt)
{
	struct cpuid_ctx ctx = { ghcb, ctxt };
	struct pt_regs *regs = ctxt->regs;
	struct cpuid_leaf leaf;
	int ret;

	leaf.fn = regs->ax;
	leaf.subfn = regs->cx;
	ret = snp_cpuid(snp_cpuid_hv_ghcb, &ctx, &leaf);
	if (!ret) {
		regs->ax = leaf.eax;
		regs->bx = leaf.ebx;
		regs->cx = leaf.ecx;
		regs->dx = leaf.edx;
	}

	return ret;
}

static enum es_result vc_handle_cpuid(struct ghcb *ghcb,
				      struct es_em_ctxt *ctxt)
{
	struct pt_regs *regs = ctxt->regs;
	u32 cr4 = native_read_cr4();
	enum es_result ret;
	int snp_cpuid_ret;

	snp_cpuid_ret = vc_handle_cpuid_snp(ghcb, ctxt);
	if (!snp_cpuid_ret)
		return ES_OK;
	if (snp_cpuid_ret != -EOPNOTSUPP)
		return ES_VMM_ERROR;

	ghcb_set_rax(ghcb, regs->ax);
	ghcb_set_rcx(ghcb, regs->cx);

	if (cr4 & X86_CR4_OSXSAVE)
		/* Safe to read xcr0 */
		ghcb_set_xcr0(ghcb, xgetbv(XCR_XFEATURE_ENABLED_MASK));
	else
		/* xgetbv will cause #GP - use reset value for xcr0 */
		ghcb_set_xcr0(ghcb, 1);

	ret = sev_es_ghcb_hv_call(ghcb, ctxt, SVM_EXIT_CPUID, 0, 0);
	if (ret != ES_OK)
		return ret;

	if (!(ghcb_rax_is_valid(ghcb) &&
	      ghcb_rbx_is_valid(ghcb) &&
	      ghcb_rcx_is_valid(ghcb) &&
	      ghcb_rdx_is_valid(ghcb)))
		return ES_VMM_ERROR;

	regs->ax = ghcb->save.rax;
	regs->bx = ghcb->save.rbx;
	regs->cx = ghcb->save.rcx;
	regs->dx = ghcb->save.rdx;

	return ES_OK;
}

static enum es_result vc_handle_rdtsc(struct ghcb *ghcb,
				      struct es_em_ctxt *ctxt,
				      unsigned long exit_code)
{
	bool rdtscp = (exit_code == SVM_EXIT_RDTSCP);
	enum es_result ret;

	/*
	 * The hypervisor should not be intercepting RDTSC/RDTSCP when Secure
	 * TSC is enabled. A #VC exception will be generated if the RDTSC/RDTSCP
	 * instructions are being intercepted. If this should occur and Secure
	 * TSC is enabled, guest execution should be terminated as the guest
	 * cannot rely on the TSC value provided by the hypervisor.
	 */
	if (sev_status & MSR_AMD64_SNP_SECURE_TSC)
		return ES_VMM_ERROR;

	ret = sev_es_ghcb_hv_call(ghcb, ctxt, exit_code, 0, 0);
	if (ret != ES_OK)
		return ret;

	if (!(ghcb_rax_is_valid(ghcb) && ghcb_rdx_is_valid(ghcb) &&
	     (!rdtscp || ghcb_rcx_is_valid(ghcb))))
		return ES_VMM_ERROR;

	ctxt->regs->ax = ghcb->save.rax;
	ctxt->regs->dx = ghcb->save.rdx;
	if (rdtscp)
		ctxt->regs->cx = ghcb->save.rcx;

	return ES_OK;
}

void snp_register_ghcb_early(unsigned long paddr)
{
	unsigned long pfn = paddr >> PAGE_SHIFT;
	u64 val;

	sev_es_wr_ghcb_msr(GHCB_MSR_REG_GPA_REQ_VAL(pfn));
	VMGEXIT();

	val = sev_es_rd_ghcb_msr();

	/* If the response GPA is not ours then abort the guest */
	if ((GHCB_RESP_CODE(val) != GHCB_MSR_REG_GPA_RESP) ||
	    (GHCB_MSR_REG_GPA_RESP_VAL(val) != pfn))
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_REGISTER);
}

bool __init sev_es_check_cpu_features(void)
{
	if (!has_cpuflag(X86_FEATURE_RDRAND)) {
		error("RDRAND instruction not supported - no trusted source of randomness available\n");
		return false;
	}

	return true;
}

bool sev_es_negotiate_protocol(void)
{
	u64 val;

	/* Do the GHCB protocol version negotiation */
	sev_es_wr_ghcb_msr(GHCB_MSR_SEV_INFO_REQ);
	VMGEXIT();
	val = sev_es_rd_ghcb_msr();

	if (GHCB_MSR_INFO(val) != GHCB_MSR_SEV_INFO_RESP)
		return false;

	if (GHCB_MSR_PROTO_MAX(val) < GHCB_PROTOCOL_MIN ||
	    GHCB_MSR_PROTO_MIN(val) > GHCB_PROTOCOL_MAX)
		return false;

	ghcb_version = min_t(size_t, GHCB_MSR_PROTO_MAX(val), GHCB_PROTOCOL_MAX);

	return true;
}
