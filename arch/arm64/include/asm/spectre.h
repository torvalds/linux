/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Interface for managing mitigations for Spectre vulnerabilities.
 *
 * Copyright (C) 2020 Google LLC
 * Author: Will Deacon <will@kernel.org>
 */

#ifndef __ASM_SPECTRE_H
#define __ASM_SPECTRE_H

#define BP_HARDEN_EL2_SLOTS 4
#define __BP_HARDEN_HYP_VECS_SZ	((BP_HARDEN_EL2_SLOTS - 1) * SZ_2K)

#ifndef __ASSEMBLY__
#include <linux/smp.h>
#include <asm/percpu.h>

#include <asm/cpufeature.h>
#include <asm/virt.h>

/* Watch out, ordering is important here. */
enum mitigation_state {
	SPECTRE_UNAFFECTED,
	SPECTRE_MITIGATED,
	SPECTRE_VULNERABLE,
};

struct pt_regs;
struct task_struct;

/*
 * Note: the order of this enum corresponds to __bp_harden_hyp_vecs and
 * we rely on having the direct vectors first.
 */
enum arm64_hyp_spectre_vector {
	/*
	 * Take exceptions directly to __kvm_hyp_vector. This must be
	 * 0 so that it used by default when mitigations are not needed.
	 */
	HYP_VECTOR_DIRECT,

	/*
	 * Bounce via a slot in the hypervisor text mapping of
	 * __bp_harden_hyp_vecs, which contains an SMC call.
	 */
	HYP_VECTOR_SPECTRE_DIRECT,

	/*
	 * Bounce via a slot in a special mapping of __bp_harden_hyp_vecs
	 * next to the idmap page.
	 */
	HYP_VECTOR_INDIRECT,

	/*
	 * Bounce via a slot in a special mapping of __bp_harden_hyp_vecs
	 * next to the idmap page, which contains an SMC call.
	 */
	HYP_VECTOR_SPECTRE_INDIRECT,
};

typedef void (*bp_hardening_cb_t)(void);

struct bp_hardening_data {
	enum arm64_hyp_spectre_vector	slot;
	bp_hardening_cb_t		fn;
};

DECLARE_PER_CPU_READ_MOSTLY(struct bp_hardening_data, bp_hardening_data);

/* Called during entry so must be __always_inline */
static __always_inline void arm64_apply_bp_hardening(void)
{
	struct bp_hardening_data *d;

	if (!alternative_has_cap_unlikely(ARM64_SPECTRE_V2))
		return;

	d = this_cpu_ptr(&bp_hardening_data);
	if (d->fn)
		d->fn();
}

enum mitigation_state arm64_get_spectre_v2_state(void);
bool has_spectre_v2(const struct arm64_cpu_capabilities *cap, int scope);
void spectre_v2_enable_mitigation(const struct arm64_cpu_capabilities *__unused);

bool has_spectre_v3a(const struct arm64_cpu_capabilities *cap, int scope);
void spectre_v3a_enable_mitigation(const struct arm64_cpu_capabilities *__unused);

enum mitigation_state arm64_get_spectre_v4_state(void);
bool has_spectre_v4(const struct arm64_cpu_capabilities *cap, int scope);
void spectre_v4_enable_mitigation(const struct arm64_cpu_capabilities *__unused);
void spectre_v4_enable_task_mitigation(struct task_struct *tsk);

enum mitigation_state arm64_get_meltdown_state(void);

enum mitigation_state arm64_get_spectre_bhb_state(void);
bool is_spectre_bhb_affected(const struct arm64_cpu_capabilities *entry, int scope);
extern bool __nospectre_bhb;
u8 get_spectre_bhb_loop_value(void);
bool is_spectre_bhb_fw_mitigated(void);
void spectre_bhb_enable_mitigation(const struct arm64_cpu_capabilities *__unused);
bool try_emulate_el1_ssbs(struct pt_regs *regs, u32 instr);

void spectre_v4_patch_fw_mitigation_enable(struct alt_instr *alt, __le32 *origptr,
					   __le32 *updptr, int nr_inst);
void smccc_patch_fw_mitigation_conduit(struct alt_instr *alt, __le32 *origptr,
				       __le32 *updptr, int nr_inst);
void spectre_bhb_patch_loop_mitigation_enable(struct alt_instr *alt, __le32 *origptr,
					      __le32 *updptr, int nr_inst);
void spectre_bhb_patch_fw_mitigation_enabled(struct alt_instr *alt, __le32 *origptr,
					     __le32 *updptr, int nr_inst);
void spectre_bhb_patch_loop_iter(struct alt_instr *alt,
				 __le32 *origptr, __le32 *updptr, int nr_inst);
void spectre_bhb_patch_wa3(struct alt_instr *alt,
			   __le32 *origptr, __le32 *updptr, int nr_inst);
void spectre_bhb_patch_clearbhb(struct alt_instr *alt,
				__le32 *origptr, __le32 *updptr, int nr_inst);

#endif	/* __ASSEMBLY__ */
#endif	/* __ASM_SPECTRE_H */
