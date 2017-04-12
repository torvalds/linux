#ifndef _ASM_X86_INTEL_RDT_COMMON_H
#define _ASM_X86_INTEL_RDT_COMMON_H

#define MSR_IA32_PQR_ASSOC	0x0c8f

/**
 * struct intel_pqr_state - State cache for the PQR MSR
 * @rmid:		The cached Resource Monitoring ID
 * @closid:		The cached Class Of Service ID
 * @rmid_usecnt:	The usage counter for rmid
 *
 * The upper 32 bits of MSR_IA32_PQR_ASSOC contain closid and the
 * lower 10 bits rmid. The update to MSR_IA32_PQR_ASSOC always
 * contains both parts, so we need to cache them.
 *
 * The cache also helps to avoid pointless updates if the value does
 * not change.
 */
struct intel_pqr_state {
	u32			rmid;
	u32			closid;
	int			rmid_usecnt;
};

DECLARE_PER_CPU(struct intel_pqr_state, pqr_state);

#endif /* _ASM_X86_INTEL_RDT_COMMON_H */
