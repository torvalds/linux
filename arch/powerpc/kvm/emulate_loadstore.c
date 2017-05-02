/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2007
 * Copyright 2011 Freescale Semiconductor, Inc.
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#include <linux/jiffies.h>
#include <linux/hrtimer.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kvm_host.h>
#include <linux/clockchips.h>

#include <asm/reg.h>
#include <asm/time.h>
#include <asm/byteorder.h>
#include <asm/kvm_ppc.h>
#include <asm/disassemble.h>
#include <asm/ppc-opcode.h>
#include "timing.h"
#include "trace.h"

#ifdef CONFIG_PPC_FPU
static bool kvmppc_check_fp_disabled(struct kvm_vcpu *vcpu)
{
	if (!(kvmppc_get_msr(vcpu) & MSR_FP)) {
		kvmppc_core_queue_fpunavail(vcpu);
		return true;
	}

	return false;
}
#endif /* CONFIG_PPC_FPU */

#ifdef CONFIG_VSX
static bool kvmppc_check_vsx_disabled(struct kvm_vcpu *vcpu)
{
	if (!(kvmppc_get_msr(vcpu) & MSR_VSX)) {
		kvmppc_core_queue_vsx_unavail(vcpu);
		return true;
	}

	return false;
}
#endif /* CONFIG_VSX */

/*
 * XXX to do:
 * lfiwax, lfiwzx
 * vector loads and stores
 *
 * Instructions that trap when used on cache-inhibited mappings
 * are not emulated here: multiple and string instructions,
 * lq/stq, and the load-reserve/store-conditional instructions.
 */
int kvmppc_emulate_loadstore(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	u32 inst;
	int ra, rs, rt;
	enum emulation_result emulated;
	int advance = 1;

	/* this default type might be overwritten by subcategories */
	kvmppc_set_exit_type(vcpu, EMULATED_INST_EXITS);

	emulated = kvmppc_get_last_inst(vcpu, INST_GENERIC, &inst);
	if (emulated != EMULATE_DONE)
		return emulated;

	ra = get_ra(inst);
	rs = get_rs(inst);
	rt = get_rt(inst);

	/*
	 * if mmio_vsx_tx_sx_enabled == 0, copy data between
	 * VSR[0..31] and memory
	 * if mmio_vsx_tx_sx_enabled == 1, copy data between
	 * VSR[32..63] and memory
	 */
	vcpu->arch.mmio_vsx_tx_sx_enabled = get_tx_or_sx(inst);
	vcpu->arch.mmio_vsx_copy_nums = 0;
	vcpu->arch.mmio_vsx_offset = 0;
	vcpu->arch.mmio_vsx_copy_type = KVMPPC_VSX_COPY_NONE;
	vcpu->arch.mmio_sp64_extend = 0;
	vcpu->arch.mmio_sign_extend = 0;

	switch (get_op(inst)) {
	case 31:
		switch (get_xop(inst)) {
		case OP_31_XOP_LWZX:
			emulated = kvmppc_handle_load(run, vcpu, rt, 4, 1);
			break;

		case OP_31_XOP_LWZUX:
			emulated = kvmppc_handle_load(run, vcpu, rt, 4, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

		case OP_31_XOP_LBZX:
			emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
			break;

		case OP_31_XOP_LBZUX:
			emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

		case OP_31_XOP_STDX:
			emulated = kvmppc_handle_store(run, vcpu,
					kvmppc_get_gpr(vcpu, rs), 8, 1);
			break;

		case OP_31_XOP_STDUX:
			emulated = kvmppc_handle_store(run, vcpu,
					kvmppc_get_gpr(vcpu, rs), 8, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

		case OP_31_XOP_STWX:
			emulated = kvmppc_handle_store(run, vcpu,
					kvmppc_get_gpr(vcpu, rs), 4, 1);
			break;

		case OP_31_XOP_STWUX:
			emulated = kvmppc_handle_store(run, vcpu,
					kvmppc_get_gpr(vcpu, rs), 4, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

		case OP_31_XOP_STBX:
			emulated = kvmppc_handle_store(run, vcpu,
					kvmppc_get_gpr(vcpu, rs), 1, 1);
			break;

		case OP_31_XOP_STBUX:
			emulated = kvmppc_handle_store(run, vcpu,
					kvmppc_get_gpr(vcpu, rs), 1, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

		case OP_31_XOP_LHAX:
			emulated = kvmppc_handle_loads(run, vcpu, rt, 2, 1);
			break;

		case OP_31_XOP_LHAUX:
			emulated = kvmppc_handle_loads(run, vcpu, rt, 2, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

		case OP_31_XOP_LHZX:
			emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
			break;

		case OP_31_XOP_LHZUX:
			emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

		case OP_31_XOP_STHX:
			emulated = kvmppc_handle_store(run, vcpu,
					kvmppc_get_gpr(vcpu, rs), 2, 1);
			break;

		case OP_31_XOP_STHUX:
			emulated = kvmppc_handle_store(run, vcpu,
					kvmppc_get_gpr(vcpu, rs), 2, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

		case OP_31_XOP_DCBST:
		case OP_31_XOP_DCBF:
		case OP_31_XOP_DCBI:
			/* Do nothing. The guest is performing dcbi because
			 * hardware DMA is not snooped by the dcache, but
			 * emulated DMA either goes through the dcache as
			 * normal writes, or the host kernel has handled dcache
			 * coherence. */
			break;

		case OP_31_XOP_LWBRX:
			emulated = kvmppc_handle_load(run, vcpu, rt, 4, 0);
			break;

		case OP_31_XOP_STWBRX:
			emulated = kvmppc_handle_store(run, vcpu,
					kvmppc_get_gpr(vcpu, rs), 4, 0);
			break;

		case OP_31_XOP_LHBRX:
			emulated = kvmppc_handle_load(run, vcpu, rt, 2, 0);
			break;

		case OP_31_XOP_STHBRX:
			emulated = kvmppc_handle_store(run, vcpu,
					kvmppc_get_gpr(vcpu, rs), 2, 0);
			break;

		case OP_31_XOP_LDBRX:
			emulated = kvmppc_handle_load(run, vcpu, rt, 8, 0);
			break;

		case OP_31_XOP_STDBRX:
			emulated = kvmppc_handle_store(run, vcpu,
					kvmppc_get_gpr(vcpu, rs), 8, 0);
			break;

		case OP_31_XOP_LDX:
			emulated = kvmppc_handle_load(run, vcpu, rt, 8, 1);
			break;

		case OP_31_XOP_LDUX:
			emulated = kvmppc_handle_load(run, vcpu, rt, 8, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

		case OP_31_XOP_LWAX:
			emulated = kvmppc_handle_loads(run, vcpu, rt, 4, 1);
			break;

		case OP_31_XOP_LWAUX:
			emulated = kvmppc_handle_loads(run, vcpu, rt, 4, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

#ifdef CONFIG_PPC_FPU
		case OP_31_XOP_LFSX:
			if (kvmppc_check_fp_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_sp64_extend = 1;
			emulated = kvmppc_handle_load(run, vcpu,
				KVM_MMIO_REG_FPR|rt, 4, 1);
			break;

		case OP_31_XOP_LFSUX:
			if (kvmppc_check_fp_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_sp64_extend = 1;
			emulated = kvmppc_handle_load(run, vcpu,
				KVM_MMIO_REG_FPR|rt, 4, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

		case OP_31_XOP_LFDX:
			if (kvmppc_check_fp_disabled(vcpu))
				return EMULATE_DONE;
			emulated = kvmppc_handle_load(run, vcpu,
				KVM_MMIO_REG_FPR|rt, 8, 1);
			break;

		case OP_31_XOP_LFDUX:
			if (kvmppc_check_fp_disabled(vcpu))
				return EMULATE_DONE;
			emulated = kvmppc_handle_load(run, vcpu,
				KVM_MMIO_REG_FPR|rt, 8, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

		case OP_31_XOP_LFIWAX:
			if (kvmppc_check_fp_disabled(vcpu))
				return EMULATE_DONE;
			emulated = kvmppc_handle_loads(run, vcpu,
				KVM_MMIO_REG_FPR|rt, 4, 1);
			break;

		case OP_31_XOP_LFIWZX:
			if (kvmppc_check_fp_disabled(vcpu))
				return EMULATE_DONE;
			emulated = kvmppc_handle_load(run, vcpu,
				KVM_MMIO_REG_FPR|rt, 4, 1);
			break;

		case OP_31_XOP_STFSX:
			if (kvmppc_check_fp_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_sp64_extend = 1;
			emulated = kvmppc_handle_store(run, vcpu,
				VCPU_FPR(vcpu, rs), 4, 1);
			break;

		case OP_31_XOP_STFSUX:
			if (kvmppc_check_fp_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_sp64_extend = 1;
			emulated = kvmppc_handle_store(run, vcpu,
				VCPU_FPR(vcpu, rs), 4, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

		case OP_31_XOP_STFDX:
			if (kvmppc_check_fp_disabled(vcpu))
				return EMULATE_DONE;
			emulated = kvmppc_handle_store(run, vcpu,
				VCPU_FPR(vcpu, rs), 8, 1);
			break;

		case OP_31_XOP_STFDUX:
			if (kvmppc_check_fp_disabled(vcpu))
				return EMULATE_DONE;
			emulated = kvmppc_handle_store(run, vcpu,
				VCPU_FPR(vcpu, rs), 8, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;

		case OP_31_XOP_STFIWX:
			if (kvmppc_check_fp_disabled(vcpu))
				return EMULATE_DONE;
			emulated = kvmppc_handle_store(run, vcpu,
				VCPU_FPR(vcpu, rs), 4, 1);
			break;
#endif

#ifdef CONFIG_VSX
		case OP_31_XOP_LXSDX:
			if (kvmppc_check_vsx_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_vsx_copy_nums = 1;
			vcpu->arch.mmio_vsx_copy_type = KVMPPC_VSX_COPY_DWORD;
			emulated = kvmppc_handle_vsx_load(run, vcpu,
				KVM_MMIO_REG_VSX|rt, 8, 1, 0);
			break;

		case OP_31_XOP_LXSSPX:
			if (kvmppc_check_vsx_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_vsx_copy_nums = 1;
			vcpu->arch.mmio_vsx_copy_type = KVMPPC_VSX_COPY_DWORD;
			vcpu->arch.mmio_sp64_extend = 1;
			emulated = kvmppc_handle_vsx_load(run, vcpu,
				KVM_MMIO_REG_VSX|rt, 4, 1, 0);
			break;

		case OP_31_XOP_LXSIWAX:
			if (kvmppc_check_vsx_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_vsx_copy_nums = 1;
			vcpu->arch.mmio_vsx_copy_type = KVMPPC_VSX_COPY_DWORD;
			emulated = kvmppc_handle_vsx_load(run, vcpu,
				KVM_MMIO_REG_VSX|rt, 4, 1, 1);
			break;

		case OP_31_XOP_LXSIWZX:
			if (kvmppc_check_vsx_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_vsx_copy_nums = 1;
			vcpu->arch.mmio_vsx_copy_type = KVMPPC_VSX_COPY_DWORD;
			emulated = kvmppc_handle_vsx_load(run, vcpu,
				KVM_MMIO_REG_VSX|rt, 4, 1, 0);
			break;

		case OP_31_XOP_LXVD2X:
		/*
		 * In this case, the official load/store process is like this:
		 * Step1, exit from vm by page fault isr, then kvm save vsr.
		 * Please see guest_exit_cont->store_fp_state->SAVE_32VSRS
		 * as reference.
		 *
		 * Step2, copy data between memory and VCPU
		 * Notice: for LXVD2X/STXVD2X/LXVW4X/STXVW4X, we use
		 * 2copies*8bytes or 4copies*4bytes
		 * to simulate one copy of 16bytes.
		 * Also there is an endian issue here, we should notice the
		 * layout of memory.
		 * Please see MARCO of LXVD2X_ROT/STXVD2X_ROT as more reference.
		 * If host is little-endian, kvm will call XXSWAPD for
		 * LXVD2X_ROT/STXVD2X_ROT.
		 * So, if host is little-endian,
		 * the postion of memeory should be swapped.
		 *
		 * Step3, return to guest, kvm reset register.
		 * Please see kvmppc_hv_entry->load_fp_state->REST_32VSRS
		 * as reference.
		 */
			if (kvmppc_check_vsx_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_vsx_copy_nums = 2;
			vcpu->arch.mmio_vsx_copy_type = KVMPPC_VSX_COPY_DWORD;
			emulated = kvmppc_handle_vsx_load(run, vcpu,
				KVM_MMIO_REG_VSX|rt, 8, 1, 0);
			break;

		case OP_31_XOP_LXVW4X:
			if (kvmppc_check_vsx_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_vsx_copy_nums = 4;
			vcpu->arch.mmio_vsx_copy_type = KVMPPC_VSX_COPY_WORD;
			emulated = kvmppc_handle_vsx_load(run, vcpu,
				KVM_MMIO_REG_VSX|rt, 4, 1, 0);
			break;

		case OP_31_XOP_LXVDSX:
			if (kvmppc_check_vsx_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_vsx_copy_nums = 1;
			vcpu->arch.mmio_vsx_copy_type =
				 KVMPPC_VSX_COPY_DWORD_LOAD_DUMP;
			emulated = kvmppc_handle_vsx_load(run, vcpu,
				KVM_MMIO_REG_VSX|rt, 8, 1, 0);
			break;

		case OP_31_XOP_STXSDX:
			if (kvmppc_check_vsx_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_vsx_copy_nums = 1;
			vcpu->arch.mmio_vsx_copy_type = KVMPPC_VSX_COPY_DWORD;
			emulated = kvmppc_handle_vsx_store(run, vcpu,
						 rs, 8, 1);
			break;

		case OP_31_XOP_STXSSPX:
			if (kvmppc_check_vsx_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_vsx_copy_nums = 1;
			vcpu->arch.mmio_vsx_copy_type = KVMPPC_VSX_COPY_DWORD;
			vcpu->arch.mmio_sp64_extend = 1;
			emulated = kvmppc_handle_vsx_store(run, vcpu,
						 rs, 4, 1);
			break;

		case OP_31_XOP_STXSIWX:
			if (kvmppc_check_vsx_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_vsx_offset = 1;
			vcpu->arch.mmio_vsx_copy_nums = 1;
			vcpu->arch.mmio_vsx_copy_type = KVMPPC_VSX_COPY_WORD;
			emulated = kvmppc_handle_vsx_store(run, vcpu,
							 rs, 4, 1);
			break;

		case OP_31_XOP_STXVD2X:
			if (kvmppc_check_vsx_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_vsx_copy_nums = 2;
			vcpu->arch.mmio_vsx_copy_type = KVMPPC_VSX_COPY_DWORD;
			emulated = kvmppc_handle_vsx_store(run, vcpu,
							 rs, 8, 1);
			break;

		case OP_31_XOP_STXVW4X:
			if (kvmppc_check_vsx_disabled(vcpu))
				return EMULATE_DONE;
			vcpu->arch.mmio_vsx_copy_nums = 4;
			vcpu->arch.mmio_vsx_copy_type = KVMPPC_VSX_COPY_WORD;
			emulated = kvmppc_handle_vsx_store(run, vcpu,
							 rs, 4, 1);
			break;
#endif /* CONFIG_VSX */
		default:
			emulated = EMULATE_FAIL;
			break;
		}
		break;

	case OP_LWZ:
		emulated = kvmppc_handle_load(run, vcpu, rt, 4, 1);
		break;

#ifdef CONFIG_PPC_FPU
	case OP_STFS:
		if (kvmppc_check_fp_disabled(vcpu))
			return EMULATE_DONE;
		vcpu->arch.mmio_sp64_extend = 1;
		emulated = kvmppc_handle_store(run, vcpu,
			VCPU_FPR(vcpu, rs),
			4, 1);
		break;

	case OP_STFSU:
		if (kvmppc_check_fp_disabled(vcpu))
			return EMULATE_DONE;
		vcpu->arch.mmio_sp64_extend = 1;
		emulated = kvmppc_handle_store(run, vcpu,
			VCPU_FPR(vcpu, rs),
			4, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
		break;

	case OP_STFD:
		if (kvmppc_check_fp_disabled(vcpu))
			return EMULATE_DONE;
		emulated = kvmppc_handle_store(run, vcpu,
			VCPU_FPR(vcpu, rs),
	                               8, 1);
		break;

	case OP_STFDU:
		if (kvmppc_check_fp_disabled(vcpu))
			return EMULATE_DONE;
		emulated = kvmppc_handle_store(run, vcpu,
			VCPU_FPR(vcpu, rs),
	                               8, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
		break;
#endif

	case OP_LD:
		rt = get_rt(inst);
		switch (inst & 3) {
		case 0:	/* ld */
			emulated = kvmppc_handle_load(run, vcpu, rt, 8, 1);
			break;
		case 1: /* ldu */
			emulated = kvmppc_handle_load(run, vcpu, rt, 8, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;
		case 2:	/* lwa */
			emulated = kvmppc_handle_loads(run, vcpu, rt, 4, 1);
			break;
		default:
			emulated = EMULATE_FAIL;
		}
		break;

	case OP_LWZU:
		emulated = kvmppc_handle_load(run, vcpu, rt, 4, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
		break;

	case OP_LBZ:
		emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
		break;

	case OP_LBZU:
		emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
		break;

	case OP_STW:
		emulated = kvmppc_handle_store(run, vcpu,
					       kvmppc_get_gpr(vcpu, rs),
		                               4, 1);
		break;

	case OP_STD:
		rs = get_rs(inst);
		switch (inst & 3) {
		case 0:	/* std */
			emulated = kvmppc_handle_store(run, vcpu,
				kvmppc_get_gpr(vcpu, rs), 8, 1);
			break;
		case 1: /* stdu */
			emulated = kvmppc_handle_store(run, vcpu,
				kvmppc_get_gpr(vcpu, rs), 8, 1);
			kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
			break;
		default:
			emulated = EMULATE_FAIL;
		}
		break;

	case OP_STWU:
		emulated = kvmppc_handle_store(run, vcpu,
				kvmppc_get_gpr(vcpu, rs), 4, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
		break;

	case OP_STB:
		emulated = kvmppc_handle_store(run, vcpu,
				kvmppc_get_gpr(vcpu, rs), 1, 1);
		break;

	case OP_STBU:
		emulated = kvmppc_handle_store(run, vcpu,
				kvmppc_get_gpr(vcpu, rs), 1, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
		break;

	case OP_LHZ:
		emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
		break;

	case OP_LHZU:
		emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
		break;

	case OP_LHA:
		emulated = kvmppc_handle_loads(run, vcpu, rt, 2, 1);
		break;

	case OP_LHAU:
		emulated = kvmppc_handle_loads(run, vcpu, rt, 2, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
		break;

	case OP_STH:
		emulated = kvmppc_handle_store(run, vcpu,
				kvmppc_get_gpr(vcpu, rs), 2, 1);
		break;

	case OP_STHU:
		emulated = kvmppc_handle_store(run, vcpu,
				kvmppc_get_gpr(vcpu, rs), 2, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
		break;

#ifdef CONFIG_PPC_FPU
	case OP_LFS:
		if (kvmppc_check_fp_disabled(vcpu))
			return EMULATE_DONE;
		vcpu->arch.mmio_sp64_extend = 1;
		emulated = kvmppc_handle_load(run, vcpu,
			KVM_MMIO_REG_FPR|rt, 4, 1);
		break;

	case OP_LFSU:
		if (kvmppc_check_fp_disabled(vcpu))
			return EMULATE_DONE;
		vcpu->arch.mmio_sp64_extend = 1;
		emulated = kvmppc_handle_load(run, vcpu,
			KVM_MMIO_REG_FPR|rt, 4, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
		break;

	case OP_LFD:
		if (kvmppc_check_fp_disabled(vcpu))
			return EMULATE_DONE;
		emulated = kvmppc_handle_load(run, vcpu,
			KVM_MMIO_REG_FPR|rt, 8, 1);
		break;

	case OP_LFDU:
		if (kvmppc_check_fp_disabled(vcpu))
			return EMULATE_DONE;
		emulated = kvmppc_handle_load(run, vcpu,
			KVM_MMIO_REG_FPR|rt, 8, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.vaddr_accessed);
		break;
#endif

	default:
		emulated = EMULATE_FAIL;
		break;
	}

	if (emulated == EMULATE_FAIL) {
		advance = 0;
		kvmppc_core_queue_program(vcpu, 0);
	}

	trace_kvm_ppc_instr(inst, kvmppc_get_pc(vcpu), emulated);

	/* Advance past emulated instruction. */
	if (advance)
		kvmppc_set_pc(vcpu, kvmppc_get_pc(vcpu) + 4);

	return emulated;
}
