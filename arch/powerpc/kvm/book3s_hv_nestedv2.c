// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Jordan Niethe, IBM Corp. <jniethe5@gmail.com>
 *
 * Authors:
 *    Jordan Niethe <jniethe5@gmail.com>
 *
 * Description: KVM functions specific to running on Book 3S
 * processors as a NESTEDv2 guest.
 *
 */

#include "linux/blk-mq.h"
#include "linux/console.h"
#include "linux/gfp_types.h"
#include "linux/signal.h"
#include <linux/kernel.h>
#include <linux/kvm_host.h>
#include <linux/pgtable.h>

#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/hvcall.h>
#include <asm/pgalloc.h>
#include <asm/reg.h>
#include <asm/plpar_wrappers.h>
#include <asm/guest-state-buffer.h>
#include "trace_hv.h"

struct static_key_false __kvmhv_is_nestedv2 __read_mostly;
EXPORT_SYMBOL_GPL(__kvmhv_is_nestedv2);


static size_t
gs_msg_ops_kvmhv_nestedv2_config_get_size(struct kvmppc_gs_msg *gsm)
{
	u16 ids[] = {
		KVMPPC_GSID_RUN_OUTPUT_MIN_SIZE,
		KVMPPC_GSID_RUN_INPUT,
		KVMPPC_GSID_RUN_OUTPUT,

	};
	size_t size = 0;

	for (int i = 0; i < ARRAY_SIZE(ids); i++)
		size += kvmppc_gse_total_size(kvmppc_gsid_size(ids[i]));
	return size;
}

static int
gs_msg_ops_kvmhv_nestedv2_config_fill_info(struct kvmppc_gs_buff *gsb,
					   struct kvmppc_gs_msg *gsm)
{
	struct kvmhv_nestedv2_config *cfg;
	int rc;

	cfg = gsm->data;

	if (kvmppc_gsm_includes(gsm, KVMPPC_GSID_RUN_OUTPUT_MIN_SIZE)) {
		rc = kvmppc_gse_put_u64(gsb, KVMPPC_GSID_RUN_OUTPUT_MIN_SIZE,
					cfg->vcpu_run_output_size);
		if (rc < 0)
			return rc;
	}

	if (kvmppc_gsm_includes(gsm, KVMPPC_GSID_RUN_INPUT)) {
		rc = kvmppc_gse_put_buff_info(gsb, KVMPPC_GSID_RUN_INPUT,
					      cfg->vcpu_run_input_cfg);
		if (rc < 0)
			return rc;
	}

	if (kvmppc_gsm_includes(gsm, KVMPPC_GSID_RUN_OUTPUT)) {
		rc = kvmppc_gse_put_buff_info(gsb, KVMPPC_GSID_RUN_OUTPUT,
					      cfg->vcpu_run_output_cfg);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int
gs_msg_ops_kvmhv_nestedv2_config_refresh_info(struct kvmppc_gs_msg *gsm,
					      struct kvmppc_gs_buff *gsb)
{
	struct kvmhv_nestedv2_config *cfg;
	struct kvmppc_gs_parser gsp = { 0 };
	struct kvmppc_gs_elem *gse;
	int rc;

	cfg = gsm->data;

	rc = kvmppc_gse_parse(&gsp, gsb);
	if (rc < 0)
		return rc;

	gse = kvmppc_gsp_lookup(&gsp, KVMPPC_GSID_RUN_OUTPUT_MIN_SIZE);
	if (gse)
		cfg->vcpu_run_output_size = kvmppc_gse_get_u64(gse);
	return 0;
}

static struct kvmppc_gs_msg_ops config_msg_ops = {
	.get_size = gs_msg_ops_kvmhv_nestedv2_config_get_size,
	.fill_info = gs_msg_ops_kvmhv_nestedv2_config_fill_info,
	.refresh_info = gs_msg_ops_kvmhv_nestedv2_config_refresh_info,
};

static size_t gs_msg_ops_vcpu_get_size(struct kvmppc_gs_msg *gsm)
{
	struct kvmppc_gs_bitmap gsbm = { 0 };
	size_t size = 0;
	u16 iden;

	kvmppc_gsbm_fill(&gsbm);
	kvmppc_gsbm_for_each(&gsbm, iden)
	{
		switch (iden) {
		case KVMPPC_GSID_HOST_STATE_SIZE:
		case KVMPPC_GSID_RUN_OUTPUT_MIN_SIZE:
		case KVMPPC_GSID_PARTITION_TABLE:
		case KVMPPC_GSID_PROCESS_TABLE:
		case KVMPPC_GSID_RUN_INPUT:
		case KVMPPC_GSID_RUN_OUTPUT:
			break;
		default:
			size += kvmppc_gse_total_size(kvmppc_gsid_size(iden));
		}
	}
	return size;
}

static int gs_msg_ops_vcpu_fill_info(struct kvmppc_gs_buff *gsb,
				     struct kvmppc_gs_msg *gsm)
{
	struct kvm_vcpu *vcpu;
	vector128 v;
	int rc, i;
	u16 iden;
	u32 arch_compat = 0;

	vcpu = gsm->data;

	kvmppc_gsm_for_each(gsm, iden)
	{
		rc = 0;

		if ((gsm->flags & KVMPPC_GS_FLAGS_WIDE) !=
		    (kvmppc_gsid_flags(iden) & KVMPPC_GS_FLAGS_WIDE))
			continue;

		switch (iden) {
		case KVMPPC_GSID_DSCR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.dscr);
			break;
		case KVMPPC_GSID_MMCRA:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.mmcra);
			break;
		case KVMPPC_GSID_HFSCR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.hfscr);
			break;
		case KVMPPC_GSID_PURR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.purr);
			break;
		case KVMPPC_GSID_SPURR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.spurr);
			break;
		case KVMPPC_GSID_AMR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.amr);
			break;
		case KVMPPC_GSID_UAMOR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.uamor);
			break;
		case KVMPPC_GSID_SIAR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.siar);
			break;
		case KVMPPC_GSID_SDAR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.sdar);
			break;
		case KVMPPC_GSID_IAMR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.iamr);
			break;
		case KVMPPC_GSID_DAWR0:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.dawr0);
			break;
		case KVMPPC_GSID_DAWR1:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.dawr1);
			break;
		case KVMPPC_GSID_DAWRX0:
			rc = kvmppc_gse_put_u32(gsb, iden, vcpu->arch.dawrx0);
			break;
		case KVMPPC_GSID_DAWRX1:
			rc = kvmppc_gse_put_u32(gsb, iden, vcpu->arch.dawrx1);
			break;
		case KVMPPC_GSID_CIABR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.ciabr);
			break;
		case KVMPPC_GSID_WORT:
			rc = kvmppc_gse_put_u32(gsb, iden, vcpu->arch.wort);
			break;
		case KVMPPC_GSID_PPR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.ppr);
			break;
		case KVMPPC_GSID_PSPB:
			rc = kvmppc_gse_put_u32(gsb, iden, vcpu->arch.pspb);
			break;
		case KVMPPC_GSID_TAR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.tar);
			break;
		case KVMPPC_GSID_FSCR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.fscr);
			break;
		case KVMPPC_GSID_EBBHR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.ebbhr);
			break;
		case KVMPPC_GSID_EBBRR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.ebbrr);
			break;
		case KVMPPC_GSID_BESCR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.bescr);
			break;
		case KVMPPC_GSID_IC:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.ic);
			break;
		case KVMPPC_GSID_CTRL:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.ctrl);
			break;
		case KVMPPC_GSID_PIDR:
			rc = kvmppc_gse_put_u32(gsb, iden, vcpu->arch.pid);
			break;
		case KVMPPC_GSID_AMOR: {
			u64 amor = ~0;

			rc = kvmppc_gse_put_u64(gsb, iden, amor);
			break;
		}
		case KVMPPC_GSID_VRSAVE:
			rc = kvmppc_gse_put_u32(gsb, iden, vcpu->arch.vrsave);
			break;
		case KVMPPC_GSID_MMCR(0)... KVMPPC_GSID_MMCR(3):
			i = iden - KVMPPC_GSID_MMCR(0);
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.mmcr[i]);
			break;
		case KVMPPC_GSID_SIER(0)... KVMPPC_GSID_SIER(2):
			i = iden - KVMPPC_GSID_SIER(0);
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.sier[i]);
			break;
		case KVMPPC_GSID_PMC(0)... KVMPPC_GSID_PMC(5):
			i = iden - KVMPPC_GSID_PMC(0);
			rc = kvmppc_gse_put_u32(gsb, iden, vcpu->arch.pmc[i]);
			break;
		case KVMPPC_GSID_GPR(0)... KVMPPC_GSID_GPR(31):
			i = iden - KVMPPC_GSID_GPR(0);
			rc = kvmppc_gse_put_u64(gsb, iden,
						vcpu->arch.regs.gpr[i]);
			break;
		case KVMPPC_GSID_CR:
			rc = kvmppc_gse_put_u32(gsb, iden, vcpu->arch.regs.ccr);
			break;
		case KVMPPC_GSID_XER:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.regs.xer);
			break;
		case KVMPPC_GSID_CTR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.regs.ctr);
			break;
		case KVMPPC_GSID_LR:
			rc = kvmppc_gse_put_u64(gsb, iden,
						vcpu->arch.regs.link);
			break;
		case KVMPPC_GSID_NIA:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.regs.nip);
			break;
		case KVMPPC_GSID_SRR0:
			rc = kvmppc_gse_put_u64(gsb, iden,
						vcpu->arch.shregs.srr0);
			break;
		case KVMPPC_GSID_SRR1:
			rc = kvmppc_gse_put_u64(gsb, iden,
						vcpu->arch.shregs.srr1);
			break;
		case KVMPPC_GSID_SPRG0:
			rc = kvmppc_gse_put_u64(gsb, iden,
						vcpu->arch.shregs.sprg0);
			break;
		case KVMPPC_GSID_SPRG1:
			rc = kvmppc_gse_put_u64(gsb, iden,
						vcpu->arch.shregs.sprg1);
			break;
		case KVMPPC_GSID_SPRG2:
			rc = kvmppc_gse_put_u64(gsb, iden,
						vcpu->arch.shregs.sprg2);
			break;
		case KVMPPC_GSID_SPRG3:
			rc = kvmppc_gse_put_u64(gsb, iden,
						vcpu->arch.shregs.sprg3);
			break;
		case KVMPPC_GSID_DAR:
			rc = kvmppc_gse_put_u64(gsb, iden,
						vcpu->arch.shregs.dar);
			break;
		case KVMPPC_GSID_DSISR:
			rc = kvmppc_gse_put_u32(gsb, iden,
						vcpu->arch.shregs.dsisr);
			break;
		case KVMPPC_GSID_MSR:
			rc = kvmppc_gse_put_u64(gsb, iden,
						vcpu->arch.shregs.msr);
			break;
		case KVMPPC_GSID_VTB:
			rc = kvmppc_gse_put_u64(gsb, iden,
						vcpu->arch.vcore->vtb);
			break;
		case KVMPPC_GSID_LPCR:
			rc = kvmppc_gse_put_u64(gsb, iden,
						vcpu->arch.vcore->lpcr);
			break;
		case KVMPPC_GSID_TB_OFFSET:
			rc = kvmppc_gse_put_u64(gsb, iden,
						vcpu->arch.vcore->tb_offset);
			break;
		case KVMPPC_GSID_FPSCR:
			rc = kvmppc_gse_put_u64(gsb, iden, vcpu->arch.fp.fpscr);
			break;
		case KVMPPC_GSID_VSRS(0)... KVMPPC_GSID_VSRS(31):
			i = iden - KVMPPC_GSID_VSRS(0);
			memcpy(&v, &vcpu->arch.fp.fpr[i],
			       sizeof(vcpu->arch.fp.fpr[i]));
			rc = kvmppc_gse_put_vector128(gsb, iden, &v);
			break;
#ifdef CONFIG_VSX
		case KVMPPC_GSID_VSCR:
			rc = kvmppc_gse_put_u32(gsb, iden,
						vcpu->arch.vr.vscr.u[3]);
			break;
		case KVMPPC_GSID_VSRS(32)... KVMPPC_GSID_VSRS(63):
			i = iden - KVMPPC_GSID_VSRS(32);
			rc = kvmppc_gse_put_vector128(gsb, iden,
						      &vcpu->arch.vr.vr[i]);
			break;
#endif
		case KVMPPC_GSID_DEC_EXPIRY_TB: {
			u64 dw;

			dw = vcpu->arch.dec_expires -
			     vcpu->arch.vcore->tb_offset;
			rc = kvmppc_gse_put_u64(gsb, iden, dw);
			break;
		}
		case KVMPPC_GSID_LOGICAL_PVR:
			/*
			 * Though 'arch_compat == 0' would mean the default
			 * compatibility, arch_compat, being a Guest Wide
			 * Element, cannot be filled with a value of 0 in GSB
			 * as this would result into a kernel trap.
			 * Hence, when `arch_compat == 0`, arch_compat should
			 * default to L1's PVR.
			 */
			if (!vcpu->arch.vcore->arch_compat) {
				if (cpu_has_feature(CPU_FTR_ARCH_31))
					arch_compat = PVR_ARCH_31;
				else if (cpu_has_feature(CPU_FTR_ARCH_300))
					arch_compat = PVR_ARCH_300;
			} else {
				arch_compat = vcpu->arch.vcore->arch_compat;
			}
			rc = kvmppc_gse_put_u32(gsb, iden, arch_compat);
			break;
		}

		if (rc < 0)
			return rc;
	}

	return 0;
}

static int gs_msg_ops_vcpu_refresh_info(struct kvmppc_gs_msg *gsm,
					struct kvmppc_gs_buff *gsb)
{
	struct kvmppc_gs_parser gsp = { 0 };
	struct kvmhv_nestedv2_io *io;
	struct kvmppc_gs_bitmap *valids;
	struct kvm_vcpu *vcpu;
	struct kvmppc_gs_elem *gse;
	vector128 v;
	int rc, i;
	u16 iden;

	vcpu = gsm->data;

	rc = kvmppc_gse_parse(&gsp, gsb);
	if (rc < 0)
		return rc;

	io = &vcpu->arch.nestedv2_io;
	valids = &io->valids;

	kvmppc_gsp_for_each(&gsp, iden, gse)
	{
		switch (iden) {
		case KVMPPC_GSID_DSCR:
			vcpu->arch.dscr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_MMCRA:
			vcpu->arch.mmcra = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_HFSCR:
			vcpu->arch.hfscr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_PURR:
			vcpu->arch.purr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_SPURR:
			vcpu->arch.spurr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_AMR:
			vcpu->arch.amr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_UAMOR:
			vcpu->arch.uamor = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_SIAR:
			vcpu->arch.siar = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_SDAR:
			vcpu->arch.sdar = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_IAMR:
			vcpu->arch.iamr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_DAWR0:
			vcpu->arch.dawr0 = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_DAWR1:
			vcpu->arch.dawr1 = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_DAWRX0:
			vcpu->arch.dawrx0 = kvmppc_gse_get_u32(gse);
			break;
		case KVMPPC_GSID_DAWRX1:
			vcpu->arch.dawrx1 = kvmppc_gse_get_u32(gse);
			break;
		case KVMPPC_GSID_CIABR:
			vcpu->arch.ciabr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_WORT:
			vcpu->arch.wort = kvmppc_gse_get_u32(gse);
			break;
		case KVMPPC_GSID_PPR:
			vcpu->arch.ppr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_PSPB:
			vcpu->arch.pspb = kvmppc_gse_get_u32(gse);
			break;
		case KVMPPC_GSID_TAR:
			vcpu->arch.tar = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_FSCR:
			vcpu->arch.fscr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_EBBHR:
			vcpu->arch.ebbhr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_EBBRR:
			vcpu->arch.ebbrr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_BESCR:
			vcpu->arch.bescr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_IC:
			vcpu->arch.ic = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_CTRL:
			vcpu->arch.ctrl = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_PIDR:
			vcpu->arch.pid = kvmppc_gse_get_u32(gse);
			break;
		case KVMPPC_GSID_AMOR:
			break;
		case KVMPPC_GSID_VRSAVE:
			vcpu->arch.vrsave = kvmppc_gse_get_u32(gse);
			break;
		case KVMPPC_GSID_MMCR(0)... KVMPPC_GSID_MMCR(3):
			i = iden - KVMPPC_GSID_MMCR(0);
			vcpu->arch.mmcr[i] = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_SIER(0)... KVMPPC_GSID_SIER(2):
			i = iden - KVMPPC_GSID_SIER(0);
			vcpu->arch.sier[i] = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_PMC(0)... KVMPPC_GSID_PMC(5):
			i = iden - KVMPPC_GSID_PMC(0);
			vcpu->arch.pmc[i] = kvmppc_gse_get_u32(gse);
			break;
		case KVMPPC_GSID_GPR(0)... KVMPPC_GSID_GPR(31):
			i = iden - KVMPPC_GSID_GPR(0);
			vcpu->arch.regs.gpr[i] = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_CR:
			vcpu->arch.regs.ccr = kvmppc_gse_get_u32(gse);
			break;
		case KVMPPC_GSID_XER:
			vcpu->arch.regs.xer = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_CTR:
			vcpu->arch.regs.ctr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_LR:
			vcpu->arch.regs.link = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_NIA:
			vcpu->arch.regs.nip = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_SRR0:
			vcpu->arch.shregs.srr0 = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_SRR1:
			vcpu->arch.shregs.srr1 = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_SPRG0:
			vcpu->arch.shregs.sprg0 = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_SPRG1:
			vcpu->arch.shregs.sprg1 = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_SPRG2:
			vcpu->arch.shregs.sprg2 = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_SPRG3:
			vcpu->arch.shregs.sprg3 = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_DAR:
			vcpu->arch.shregs.dar = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_DSISR:
			vcpu->arch.shregs.dsisr = kvmppc_gse_get_u32(gse);
			break;
		case KVMPPC_GSID_MSR:
			vcpu->arch.shregs.msr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_VTB:
			vcpu->arch.vcore->vtb = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_LPCR:
			vcpu->arch.vcore->lpcr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_TB_OFFSET:
			vcpu->arch.vcore->tb_offset = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_FPSCR:
			vcpu->arch.fp.fpscr = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_VSRS(0)... KVMPPC_GSID_VSRS(31):
			kvmppc_gse_get_vector128(gse, &v);
			i = iden - KVMPPC_GSID_VSRS(0);
			memcpy(&vcpu->arch.fp.fpr[i], &v,
			       sizeof(vcpu->arch.fp.fpr[i]));
			break;
#ifdef CONFIG_VSX
		case KVMPPC_GSID_VSCR:
			vcpu->arch.vr.vscr.u[3] = kvmppc_gse_get_u32(gse);
			break;
		case KVMPPC_GSID_VSRS(32)... KVMPPC_GSID_VSRS(63):
			i = iden - KVMPPC_GSID_VSRS(32);
			kvmppc_gse_get_vector128(gse, &vcpu->arch.vr.vr[i]);
			break;
#endif
		case KVMPPC_GSID_HDAR:
			vcpu->arch.fault_dar = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_HDSISR:
			vcpu->arch.fault_dsisr = kvmppc_gse_get_u32(gse);
			break;
		case KVMPPC_GSID_ASDR:
			vcpu->arch.fault_gpa = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_HEIR:
			vcpu->arch.emul_inst = kvmppc_gse_get_u64(gse);
			break;
		case KVMPPC_GSID_DEC_EXPIRY_TB: {
			u64 dw;

			dw = kvmppc_gse_get_u64(gse);
			vcpu->arch.dec_expires =
				dw + vcpu->arch.vcore->tb_offset;
			break;
		}
		case KVMPPC_GSID_LOGICAL_PVR:
			vcpu->arch.vcore->arch_compat = kvmppc_gse_get_u32(gse);
			break;
		default:
			continue;
		}
		kvmppc_gsbm_set(valids, iden);
	}

	return 0;
}

static struct kvmppc_gs_msg_ops vcpu_message_ops = {
	.get_size = gs_msg_ops_vcpu_get_size,
	.fill_info = gs_msg_ops_vcpu_fill_info,
	.refresh_info = gs_msg_ops_vcpu_refresh_info,
};

static int kvmhv_nestedv2_host_create(struct kvm_vcpu *vcpu,
				      struct kvmhv_nestedv2_io *io)
{
	struct kvmhv_nestedv2_config *cfg;
	struct kvmppc_gs_buff *gsb, *vcpu_run_output, *vcpu_run_input;
	unsigned long guest_id, vcpu_id;
	struct kvmppc_gs_msg *gsm, *vcpu_message, *vcore_message;
	int rc;

	cfg = &io->cfg;
	guest_id = vcpu->kvm->arch.lpid;
	vcpu_id = vcpu->vcpu_id;

	gsm = kvmppc_gsm_new(&config_msg_ops, cfg, KVMPPC_GS_FLAGS_WIDE,
			     GFP_KERNEL);
	if (!gsm) {
		rc = -ENOMEM;
		goto err;
	}

	gsb = kvmppc_gsb_new(kvmppc_gsm_size(gsm), guest_id, vcpu_id,
			     GFP_KERNEL);
	if (!gsb) {
		rc = -ENOMEM;
		goto free_gsm;
	}

	rc = kvmppc_gsb_receive_datum(gsb, gsm,
				      KVMPPC_GSID_RUN_OUTPUT_MIN_SIZE);
	if (rc < 0) {
		pr_err("KVM-NESTEDv2: couldn't get vcpu run output buffer minimum size\n");
		goto free_gsb;
	}

	vcpu_run_output = kvmppc_gsb_new(cfg->vcpu_run_output_size, guest_id,
					 vcpu_id, GFP_KERNEL);
	if (!vcpu_run_output) {
		rc = -ENOMEM;
		goto free_gsb;
	}

	cfg->vcpu_run_output_cfg.address = kvmppc_gsb_paddress(vcpu_run_output);
	cfg->vcpu_run_output_cfg.size = kvmppc_gsb_capacity(vcpu_run_output);
	io->vcpu_run_output = vcpu_run_output;

	gsm->flags = 0;
	rc = kvmppc_gsb_send_datum(gsb, gsm, KVMPPC_GSID_RUN_OUTPUT);
	if (rc < 0) {
		pr_err("KVM-NESTEDv2: couldn't set vcpu run output buffer\n");
		goto free_gs_out;
	}

	vcpu_message = kvmppc_gsm_new(&vcpu_message_ops, vcpu, 0, GFP_KERNEL);
	if (!vcpu_message) {
		rc = -ENOMEM;
		goto free_gs_out;
	}
	kvmppc_gsm_include_all(vcpu_message);

	io->vcpu_message = vcpu_message;

	vcpu_run_input = kvmppc_gsb_new(kvmppc_gsm_size(vcpu_message), guest_id,
					vcpu_id, GFP_KERNEL);
	if (!vcpu_run_input) {
		rc = -ENOMEM;
		goto free_vcpu_message;
	}

	io->vcpu_run_input = vcpu_run_input;
	cfg->vcpu_run_input_cfg.address = kvmppc_gsb_paddress(vcpu_run_input);
	cfg->vcpu_run_input_cfg.size = kvmppc_gsb_capacity(vcpu_run_input);
	rc = kvmppc_gsb_send_datum(gsb, gsm, KVMPPC_GSID_RUN_INPUT);
	if (rc < 0) {
		pr_err("KVM-NESTEDv2: couldn't set vcpu run input buffer\n");
		goto free_vcpu_run_input;
	}

	vcore_message = kvmppc_gsm_new(&vcpu_message_ops, vcpu,
				       KVMPPC_GS_FLAGS_WIDE, GFP_KERNEL);
	if (!vcore_message) {
		rc = -ENOMEM;
		goto free_vcpu_run_input;
	}

	kvmppc_gsm_include_all(vcore_message);
	kvmppc_gsbm_clear(&vcore_message->bitmap, KVMPPC_GSID_LOGICAL_PVR);
	io->vcore_message = vcore_message;

	kvmppc_gsbm_fill(&io->valids);
	kvmppc_gsm_free(gsm);
	kvmppc_gsb_free(gsb);
	return 0;

free_vcpu_run_input:
	kvmppc_gsb_free(vcpu_run_input);
free_vcpu_message:
	kvmppc_gsm_free(vcpu_message);
free_gs_out:
	kvmppc_gsb_free(vcpu_run_output);
free_gsb:
	kvmppc_gsb_free(gsb);
free_gsm:
	kvmppc_gsm_free(gsm);
err:
	return rc;
}

/**
 * __kvmhv_nestedv2_mark_dirty() - mark a Guest State ID to be sent to the host
 * @vcpu: vcpu
 * @iden: guest state ID
 *
 * Mark a guest state ID as having been changed by the L1 host and thus
 * the new value must be sent to the L0 hypervisor. See kvmhv_nestedv2_flush_vcpu()
 */
int __kvmhv_nestedv2_mark_dirty(struct kvm_vcpu *vcpu, u16 iden)
{
	struct kvmhv_nestedv2_io *io;
	struct kvmppc_gs_bitmap *valids;
	struct kvmppc_gs_msg *gsm;

	if (!iden)
		return 0;

	io = &vcpu->arch.nestedv2_io;
	valids = &io->valids;
	gsm = io->vcpu_message;
	kvmppc_gsm_include(gsm, iden);
	gsm = io->vcore_message;
	kvmppc_gsm_include(gsm, iden);
	kvmppc_gsbm_set(valids, iden);
	return 0;
}
EXPORT_SYMBOL_GPL(__kvmhv_nestedv2_mark_dirty);

/**
 * __kvmhv_nestedv2_cached_reload() - reload a Guest State ID from the host
 * @vcpu: vcpu
 * @iden: guest state ID
 *
 * Reload the value for the guest state ID from the L0 host into the L1 host.
 * This is cached so that going out to the L0 host only happens if necessary.
 */
int __kvmhv_nestedv2_cached_reload(struct kvm_vcpu *vcpu, u16 iden)
{
	struct kvmhv_nestedv2_io *io;
	struct kvmppc_gs_bitmap *valids;
	struct kvmppc_gs_buff *gsb;
	struct kvmppc_gs_msg gsm;
	int rc;

	if (!iden)
		return 0;

	io = &vcpu->arch.nestedv2_io;
	valids = &io->valids;
	if (kvmppc_gsbm_test(valids, iden))
		return 0;

	gsb = io->vcpu_run_input;
	kvmppc_gsm_init(&gsm, &vcpu_message_ops, vcpu, kvmppc_gsid_flags(iden));
	rc = kvmppc_gsb_receive_datum(gsb, &gsm, iden);
	if (rc < 0) {
		pr_err("KVM-NESTEDv2: couldn't get GSID: 0x%x\n", iden);
		return rc;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(__kvmhv_nestedv2_cached_reload);

/**
 * kvmhv_nestedv2_flush_vcpu() - send modified Guest State IDs to the host
 * @vcpu: vcpu
 * @time_limit: hdec expiry tb
 *
 * Send the values marked by __kvmhv_nestedv2_mark_dirty() to the L0 host.
 * Thread wide values are copied to the H_GUEST_RUN_VCPU input buffer. Guest
 * wide values need to be sent with H_GUEST_SET first.
 *
 * The hdec tb offset is always sent to L0 host.
 */
int kvmhv_nestedv2_flush_vcpu(struct kvm_vcpu *vcpu, u64 time_limit)
{
	struct kvmhv_nestedv2_io *io;
	struct kvmppc_gs_buff *gsb;
	struct kvmppc_gs_msg *gsm;
	int rc;

	io = &vcpu->arch.nestedv2_io;
	gsb = io->vcpu_run_input;
	gsm = io->vcore_message;
	rc = kvmppc_gsb_send_data(gsb, gsm);
	if (rc < 0) {
		pr_err("KVM-NESTEDv2: couldn't set guest wide elements\n");
		return rc;
	}

	gsm = io->vcpu_message;
	kvmppc_gsb_reset(gsb);
	rc = kvmppc_gsm_fill_info(gsm, gsb);
	if (rc < 0) {
		pr_err("KVM-NESTEDv2: couldn't fill vcpu run input buffer\n");
		return rc;
	}

	rc = kvmppc_gse_put_u64(gsb, KVMPPC_GSID_HDEC_EXPIRY_TB, time_limit);
	if (rc < 0)
		return rc;
	return 0;
}
EXPORT_SYMBOL_GPL(kvmhv_nestedv2_flush_vcpu);

/**
 * kvmhv_nestedv2_set_ptbl_entry() - send partition and process table state to
 * L0 host
 * @lpid: guest id
 * @dw0: partition table double word
 * @dw1: process table double word
 */
int kvmhv_nestedv2_set_ptbl_entry(unsigned long lpid, u64 dw0, u64 dw1)
{
	struct kvmppc_gs_part_table patbl;
	struct kvmppc_gs_proc_table prtbl;
	struct kvmppc_gs_buff *gsb;
	size_t size;
	int rc;

	size = kvmppc_gse_total_size(
		       kvmppc_gsid_size(KVMPPC_GSID_PARTITION_TABLE)) +
	       kvmppc_gse_total_size(
		       kvmppc_gsid_size(KVMPPC_GSID_PROCESS_TABLE)) +
	       sizeof(struct kvmppc_gs_header);
	gsb = kvmppc_gsb_new(size, lpid, 0, GFP_KERNEL);
	if (!gsb)
		return -ENOMEM;

	patbl.address = dw0 & RPDB_MASK;
	patbl.ea_bits = ((((dw0 & RTS1_MASK) >> (RTS1_SHIFT - 3)) |
			  ((dw0 & RTS2_MASK) >> RTS2_SHIFT)) +
			 31);
	patbl.gpd_size = 1ul << ((dw0 & RPDS_MASK) + 3);
	rc = kvmppc_gse_put_part_table(gsb, KVMPPC_GSID_PARTITION_TABLE, patbl);
	if (rc < 0)
		goto free_gsb;

	prtbl.address = dw1 & PRTB_MASK;
	prtbl.gpd_size = 1ul << ((dw1 & PRTS_MASK) + 12);
	rc = kvmppc_gse_put_proc_table(gsb, KVMPPC_GSID_PROCESS_TABLE, prtbl);
	if (rc < 0)
		goto free_gsb;

	rc = kvmppc_gsb_send(gsb, KVMPPC_GS_FLAGS_WIDE);
	if (rc < 0) {
		pr_err("KVM-NESTEDv2: couldn't set the PATE\n");
		goto free_gsb;
	}

	kvmppc_gsb_free(gsb);
	return 0;

free_gsb:
	kvmppc_gsb_free(gsb);
	return rc;
}
EXPORT_SYMBOL_GPL(kvmhv_nestedv2_set_ptbl_entry);

/**
 * kvmhv_nestedv2_set_vpa() - register L2 VPA with L0
 * @vcpu: vcpu
 * @vpa: L1 logical real address
 */
int kvmhv_nestedv2_set_vpa(struct kvm_vcpu *vcpu, unsigned long vpa)
{
	struct kvmhv_nestedv2_io *io;
	struct kvmppc_gs_buff *gsb;
	int rc = 0;

	io = &vcpu->arch.nestedv2_io;
	gsb = io->vcpu_run_input;

	kvmppc_gsb_reset(gsb);
	rc = kvmppc_gse_put_u64(gsb, KVMPPC_GSID_VPA, vpa);
	if (rc < 0)
		goto out;

	rc = kvmppc_gsb_send(gsb, 0);
	if (rc < 0)
		pr_err("KVM-NESTEDv2: couldn't register the L2 VPA (rc=%d)\n", rc);

out:
	kvmppc_gsb_reset(gsb);
	return rc;
}
EXPORT_SYMBOL_GPL(kvmhv_nestedv2_set_vpa);

/**
 * kvmhv_nestedv2_parse_output() - receive values from H_GUEST_RUN_VCPU output
 * @vcpu: vcpu
 *
 * Parse the output buffer from H_GUEST_RUN_VCPU to update vcpu.
 */
int kvmhv_nestedv2_parse_output(struct kvm_vcpu *vcpu)
{
	struct kvmhv_nestedv2_io *io;
	struct kvmppc_gs_buff *gsb;
	struct kvmppc_gs_msg gsm;

	io = &vcpu->arch.nestedv2_io;
	gsb = io->vcpu_run_output;

	vcpu->arch.fault_dar = 0;
	vcpu->arch.fault_dsisr = 0;
	vcpu->arch.fault_gpa = 0;
	vcpu->arch.emul_inst = KVM_INST_FETCH_FAILED;

	kvmppc_gsm_init(&gsm, &vcpu_message_ops, vcpu, 0);
	return kvmppc_gsm_refresh_info(&gsm, gsb);
}
EXPORT_SYMBOL_GPL(kvmhv_nestedv2_parse_output);

static void kvmhv_nestedv2_host_free(struct kvm_vcpu *vcpu,
				     struct kvmhv_nestedv2_io *io)
{
	kvmppc_gsm_free(io->vcpu_message);
	kvmppc_gsm_free(io->vcore_message);
	kvmppc_gsb_free(io->vcpu_run_input);
	kvmppc_gsb_free(io->vcpu_run_output);
}

int __kvmhv_nestedv2_reload_ptregs(struct kvm_vcpu *vcpu, struct pt_regs *regs)
{
	struct kvmhv_nestedv2_io *io;
	struct kvmppc_gs_bitmap *valids;
	struct kvmppc_gs_buff *gsb;
	struct kvmppc_gs_msg gsm;
	int rc = 0;


	io = &vcpu->arch.nestedv2_io;
	valids = &io->valids;

	gsb = io->vcpu_run_input;
	kvmppc_gsm_init(&gsm, &vcpu_message_ops, vcpu, 0);

	for (int i = 0; i < 32; i++) {
		if (!kvmppc_gsbm_test(valids, KVMPPC_GSID_GPR(i)))
			kvmppc_gsm_include(&gsm, KVMPPC_GSID_GPR(i));
	}

	if (!kvmppc_gsbm_test(valids, KVMPPC_GSID_CR))
		kvmppc_gsm_include(&gsm, KVMPPC_GSID_CR);

	if (!kvmppc_gsbm_test(valids, KVMPPC_GSID_XER))
		kvmppc_gsm_include(&gsm, KVMPPC_GSID_XER);

	if (!kvmppc_gsbm_test(valids, KVMPPC_GSID_CTR))
		kvmppc_gsm_include(&gsm, KVMPPC_GSID_CTR);

	if (!kvmppc_gsbm_test(valids, KVMPPC_GSID_LR))
		kvmppc_gsm_include(&gsm, KVMPPC_GSID_LR);

	if (!kvmppc_gsbm_test(valids, KVMPPC_GSID_NIA))
		kvmppc_gsm_include(&gsm, KVMPPC_GSID_NIA);

	rc = kvmppc_gsb_receive_data(gsb, &gsm);
	if (rc < 0)
		pr_err("KVM-NESTEDv2: couldn't reload ptregs\n");

	return rc;
}
EXPORT_SYMBOL_GPL(__kvmhv_nestedv2_reload_ptregs);

int __kvmhv_nestedv2_mark_dirty_ptregs(struct kvm_vcpu *vcpu,
				       struct pt_regs *regs)
{
	for (int i = 0; i < 32; i++)
		kvmhv_nestedv2_mark_dirty(vcpu, KVMPPC_GSID_GPR(i));

	kvmhv_nestedv2_mark_dirty(vcpu, KVMPPC_GSID_CR);
	kvmhv_nestedv2_mark_dirty(vcpu, KVMPPC_GSID_XER);
	kvmhv_nestedv2_mark_dirty(vcpu, KVMPPC_GSID_CTR);
	kvmhv_nestedv2_mark_dirty(vcpu, KVMPPC_GSID_LR);
	kvmhv_nestedv2_mark_dirty(vcpu, KVMPPC_GSID_NIA);

	return 0;
}
EXPORT_SYMBOL_GPL(__kvmhv_nestedv2_mark_dirty_ptregs);

/**
 * kvmhv_nestedv2_vcpu_create() - create nested vcpu for the NESTEDv2 API
 * @vcpu: vcpu
 * @io: NESTEDv2 nested io state
 *
 * Parse the output buffer from H_GUEST_RUN_VCPU to update vcpu.
 */
int kvmhv_nestedv2_vcpu_create(struct kvm_vcpu *vcpu,
			       struct kvmhv_nestedv2_io *io)
{
	long rc;

	rc = plpar_guest_create_vcpu(0, vcpu->kvm->arch.lpid, vcpu->vcpu_id);

	if (rc != H_SUCCESS) {
		pr_err("KVM: Create Guest vcpu hcall failed, rc=%ld\n", rc);
		switch (rc) {
		case H_NOT_ENOUGH_RESOURCES:
		case H_ABORTED:
			return -ENOMEM;
		case H_AUTHORITY:
			return -EPERM;
		default:
			return -EINVAL;
		}
	}

	rc = kvmhv_nestedv2_host_create(vcpu, io);

	return rc;
}
EXPORT_SYMBOL_GPL(kvmhv_nestedv2_vcpu_create);

/**
 * kvmhv_nestedv2_vcpu_free() - free the NESTEDv2 state
 * @vcpu: vcpu
 * @io: NESTEDv2 nested io state
 */
void kvmhv_nestedv2_vcpu_free(struct kvm_vcpu *vcpu,
			      struct kvmhv_nestedv2_io *io)
{
	kvmhv_nestedv2_host_free(vcpu, io);
}
EXPORT_SYMBOL_GPL(kvmhv_nestedv2_vcpu_free);
