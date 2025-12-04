// SPDX-License-Identifier: GPL-2.0-only
/*
 * SVSM support code
 */

#include <linux/types.h>

#include <asm/sev.h>

#include "internal.h"

/* For early boot SVSM communication */
struct svsm_ca boot_svsm_ca_page __aligned(PAGE_SIZE);
SYM_PIC_ALIAS(boot_svsm_ca_page);

/*
 * SVSM related information:
 *   During boot, the page tables are set up as identity mapped and later
 *   changed to use kernel virtual addresses. Maintain separate virtual and
 *   physical addresses for the CAA to allow SVSM functions to be used during
 *   early boot, both with identity mapped virtual addresses and proper kernel
 *   virtual addresses.
 */
u64 boot_svsm_caa_pa __ro_after_init;
SYM_PIC_ALIAS(boot_svsm_caa_pa);

DEFINE_PER_CPU(struct svsm_ca *, svsm_caa);
DEFINE_PER_CPU(u64, svsm_caa_pa);

static int svsm_perform_ghcb_protocol(struct ghcb *ghcb, struct svsm_call *call)
{
	struct es_em_ctxt ctxt;
	u8 pending = 0;

	vc_ghcb_invalidate(ghcb);

	/*
	 * Fill in protocol and format specifiers. This can be called very early
	 * in the boot, so use rip-relative references as needed.
	 */
	ghcb->protocol_version = ghcb_version;
	ghcb->ghcb_usage       = GHCB_DEFAULT_USAGE;

	ghcb_set_sw_exit_code(ghcb, SVM_VMGEXIT_SNP_RUN_VMPL);
	ghcb_set_sw_exit_info_1(ghcb, 0);
	ghcb_set_sw_exit_info_2(ghcb, 0);

	sev_es_wr_ghcb_msr(__pa(ghcb));

	svsm_issue_call(call, &pending);

	if (pending)
		return -EINVAL;

	switch (verify_exception_info(ghcb, &ctxt)) {
	case ES_OK:
		break;
	case ES_EXCEPTION:
		vc_forward_exception(&ctxt);
		fallthrough;
	default:
		return -EINVAL;
	}

	return svsm_process_result_codes(call);
}

int svsm_perform_call_protocol(struct svsm_call *call)
{
	struct ghcb_state state;
	unsigned long flags;
	struct ghcb *ghcb;
	int ret;

	flags = native_local_irq_save();

	if (sev_cfg.ghcbs_initialized)
		ghcb = __sev_get_ghcb(&state);
	else if (boot_ghcb)
		ghcb = boot_ghcb;
	else
		ghcb = NULL;

	do {
		ret = ghcb ? svsm_perform_ghcb_protocol(ghcb, call)
			   : __pi_svsm_perform_msr_protocol(call);
	} while (ret == -EAGAIN);

	if (sev_cfg.ghcbs_initialized)
		__sev_put_ghcb(&state);

	native_local_irq_restore(flags);

	return ret;
}

static u64 svsm_build_ca_from_pfn_range(u64 pfn, u64 pfn_end, bool action,
					struct svsm_pvalidate_call *pc)
{
	struct svsm_pvalidate_entry *pe;

	/* Nothing in the CA yet */
	pc->num_entries = 0;
	pc->cur_index   = 0;

	pe = &pc->entry[0];

	while (pfn < pfn_end) {
		pe->page_size = RMP_PG_SIZE_4K;
		pe->action    = action;
		pe->ignore_cf = 0;
		pe->rsvd      = 0;
		pe->pfn       = pfn;

		pe++;
		pfn++;

		pc->num_entries++;
		if (pc->num_entries == SVSM_PVALIDATE_MAX_COUNT)
			break;
	}

	return pfn;
}

static int svsm_build_ca_from_psc_desc(struct snp_psc_desc *desc, unsigned int desc_entry,
				       struct svsm_pvalidate_call *pc)
{
	struct svsm_pvalidate_entry *pe;
	struct psc_entry *e;

	/* Nothing in the CA yet */
	pc->num_entries = 0;
	pc->cur_index   = 0;

	pe = &pc->entry[0];
	e  = &desc->entries[desc_entry];

	while (desc_entry <= desc->hdr.end_entry) {
		pe->page_size = e->pagesize ? RMP_PG_SIZE_2M : RMP_PG_SIZE_4K;
		pe->action    = e->operation == SNP_PAGE_STATE_PRIVATE;
		pe->ignore_cf = 0;
		pe->rsvd      = 0;
		pe->pfn       = e->gfn;

		pe++;
		e++;

		desc_entry++;
		pc->num_entries++;
		if (pc->num_entries == SVSM_PVALIDATE_MAX_COUNT)
			break;
	}

	return desc_entry;
}

static void svsm_pval_terminate(struct svsm_pvalidate_call *pc, int ret, u64 svsm_ret)
{
	unsigned int page_size;
	bool action;
	u64 pfn;

	pfn = pc->entry[pc->cur_index].pfn;
	action = pc->entry[pc->cur_index].action;
	page_size = pc->entry[pc->cur_index].page_size;

	__pval_terminate(pfn, action, page_size, ret, svsm_ret);
}

void svsm_pval_pages(struct snp_psc_desc *desc)
{
	struct svsm_pvalidate_entry pv_4k[VMGEXIT_PSC_MAX_ENTRY];
	unsigned int i, pv_4k_count = 0;
	struct svsm_pvalidate_call *pc;
	struct svsm_call call = {};
	unsigned long flags;
	bool action;
	u64 pc_pa;
	int ret;

	/*
	 * This can be called very early in the boot, use native functions in
	 * order to avoid paravirt issues.
	 */
	flags = native_local_irq_save();

	/*
	 * The SVSM calling area (CA) can support processing 510 entries at a
	 * time. Loop through the Page State Change descriptor until the CA is
	 * full or the last entry in the descriptor is reached, at which time
	 * the SVSM is invoked. This repeats until all entries in the descriptor
	 * are processed.
	 */
	call.caa = svsm_get_caa();

	pc = (struct svsm_pvalidate_call *)call.caa->svsm_buffer;
	pc_pa = svsm_get_caa_pa() + offsetof(struct svsm_ca, svsm_buffer);

	/* Protocol 0, Call ID 1 */
	call.rax = SVSM_CORE_CALL(SVSM_CORE_PVALIDATE);
	call.rcx = pc_pa;

	for (i = 0; i <= desc->hdr.end_entry;) {
		i = svsm_build_ca_from_psc_desc(desc, i, pc);

		do {
			ret = svsm_perform_call_protocol(&call);
			if (!ret)
				continue;

			/*
			 * Check if the entry failed because of an RMP mismatch (a
			 * PVALIDATE at 2M was requested, but the page is mapped in
			 * the RMP as 4K).
			 */

			if (call.rax_out == SVSM_PVALIDATE_FAIL_SIZEMISMATCH &&
			    pc->entry[pc->cur_index].page_size == RMP_PG_SIZE_2M) {
				/* Save this entry for post-processing at 4K */
				pv_4k[pv_4k_count++] = pc->entry[pc->cur_index];

				/* Skip to the next one unless at the end of the list */
				pc->cur_index++;
				if (pc->cur_index < pc->num_entries)
					ret = -EAGAIN;
				else
					ret = 0;
			}
		} while (ret == -EAGAIN);

		if (ret)
			svsm_pval_terminate(pc, ret, call.rax_out);
	}

	/* Process any entries that failed to be validated at 2M and validate them at 4K */
	for (i = 0; i < pv_4k_count; i++) {
		u64 pfn, pfn_end;

		action  = pv_4k[i].action;
		pfn     = pv_4k[i].pfn;
		pfn_end = pfn + 512;

		while (pfn < pfn_end) {
			pfn = svsm_build_ca_from_pfn_range(pfn, pfn_end, action, pc);

			ret = svsm_perform_call_protocol(&call);
			if (ret)
				svsm_pval_terminate(pc, ret, call.rax_out);
		}
	}

	native_local_irq_restore(flags);
}

static void update_attest_input(struct svsm_call *call, struct svsm_attest_call *input)
{
	/* If (new) lengths have been returned, propagate them up */
	if (call->rcx_out != call->rcx)
		input->manifest_buf.len = call->rcx_out;

	if (call->rdx_out != call->rdx)
		input->certificates_buf.len = call->rdx_out;

	if (call->r8_out != call->r8)
		input->report_buf.len = call->r8_out;
}

int snp_issue_svsm_attest_req(u64 call_id, struct svsm_call *call,
			      struct svsm_attest_call *input)
{
	struct svsm_attest_call *ac;
	unsigned long flags;
	u64 attest_call_pa;
	int ret;

	if (!snp_vmpl)
		return -EINVAL;

	local_irq_save(flags);

	call->caa = svsm_get_caa();

	ac = (struct svsm_attest_call *)call->caa->svsm_buffer;
	attest_call_pa = svsm_get_caa_pa() + offsetof(struct svsm_ca, svsm_buffer);

	*ac = *input;

	/*
	 * Set input registers for the request and set RDX and R8 to known
	 * values in order to detect length values being returned in them.
	 */
	call->rax = call_id;
	call->rcx = attest_call_pa;
	call->rdx = -1;
	call->r8 = -1;
	ret = svsm_perform_call_protocol(call);
	update_attest_input(call, input);

	local_irq_restore(flags);

	return ret;
}
EXPORT_SYMBOL_GPL(snp_issue_svsm_attest_req);

/**
 * snp_svsm_vtpm_send_command() - Execute a vTPM operation on SVSM
 * @buffer: A buffer used to both send the command and receive the response.
 *
 * Execute a SVSM_VTPM_CMD call as defined by
 * "Secure VM Service Module for SEV-SNP Guests" Publication # 58019 Revision: 1.00
 *
 * All command request/response buffers have a common structure as specified by
 * the following table:
 *     Byte      Size       In/Out    Description
 *     Offset    (Bytes)
 *     0x000     4          In        Platform command
 *                          Out       Platform command response size
 *
 * Each command can build upon this common request/response structure to create
 * a structure specific to the command. See include/linux/tpm_svsm.h for more
 * details.
 *
 * Return: 0 on success, -errno on failure
 */
int snp_svsm_vtpm_send_command(u8 *buffer)
{
	struct svsm_call call = {};

	call.caa = svsm_get_caa();
	call.rax = SVSM_VTPM_CALL(SVSM_VTPM_CMD);
	call.rcx = __pa(buffer);

	return svsm_perform_call_protocol(&call);
}
EXPORT_SYMBOL_GPL(snp_svsm_vtpm_send_command);

/**
 * snp_svsm_vtpm_probe() - Probe if SVSM provides a vTPM device
 *
 * Check that there is SVSM and that it supports at least TPM_SEND_COMMAND
 * which is the only request used so far.
 *
 * Return: true if the platform provides a vTPM SVSM device, false otherwise.
 */
bool snp_svsm_vtpm_probe(void)
{
	struct svsm_call call = {};

	/* The vTPM device is available only if a SVSM is present */
	if (!snp_vmpl)
		return false;

	call.caa = svsm_get_caa();
	call.rax = SVSM_VTPM_CALL(SVSM_VTPM_QUERY);

	if (svsm_perform_call_protocol(&call))
		return false;

	/* Check platform commands contains TPM_SEND_COMMAND - platform command 8 */
	return call.rcx_out & BIT_ULL(8);
}
