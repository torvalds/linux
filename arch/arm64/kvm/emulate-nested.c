// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 - Linaro and Columbia University
 * Author: Jintack Lim <jintack.lim@linaro.org>
 */

#include <linux/kvm.h>
#include <linux/kvm_host.h>

#include <asm/kvm_emulate.h>
#include <asm/kvm_nested.h>

#include "hyp/include/hyp/adjust_pc.h"

#include "trace.h"

enum trap_behaviour {
	BEHAVE_HANDLE_LOCALLY	= 0,
	BEHAVE_FORWARD_READ	= BIT(0),
	BEHAVE_FORWARD_WRITE	= BIT(1),
	BEHAVE_FORWARD_ANY	= BEHAVE_FORWARD_READ | BEHAVE_FORWARD_WRITE,
};

struct trap_bits {
	const enum vcpu_sysreg		index;
	const enum trap_behaviour	behaviour;
	const u64			value;
	const u64			mask;
};

/* Coarse Grained Trap definitions */
enum cgt_group_id {
	/* Indicates no coarse trap control */
	__RESERVED__,

	/*
	 * The first batch of IDs denote coarse trapping that are used
	 * on their own instead of being part of a combination of
	 * trap controls.
	 */
	CGT_HCR_TID1,
	CGT_HCR_TID2,
	CGT_HCR_TID3,
	CGT_HCR_IMO,
	CGT_HCR_FMO,
	CGT_HCR_TIDCP,
	CGT_HCR_TACR,
	CGT_HCR_TSW,
	CGT_HCR_TPC,
	CGT_HCR_TPU,
	CGT_HCR_TTLB,
	CGT_HCR_TVM,
	CGT_HCR_TDZ,
	CGT_HCR_TRVM,
	CGT_HCR_TLOR,
	CGT_HCR_TERR,
	CGT_HCR_APK,
	CGT_HCR_NV,
	CGT_HCR_NV_nNV2,
	CGT_HCR_NV1_nNV2,
	CGT_HCR_AT,
	CGT_HCR_nFIEN,
	CGT_HCR_TID4,
	CGT_HCR_TICAB,
	CGT_HCR_TOCU,
	CGT_HCR_ENSCXT,
	CGT_HCR_TTLBIS,
	CGT_HCR_TTLBOS,

	CGT_MDCR_TPMCR,
	CGT_MDCR_TPM,
	CGT_MDCR_TDE,
	CGT_MDCR_TDA,
	CGT_MDCR_TDOSA,
	CGT_MDCR_TDRA,
	CGT_MDCR_E2PB,
	CGT_MDCR_TPMS,
	CGT_MDCR_TTRF,
	CGT_MDCR_E2TB,
	CGT_MDCR_TDCC,

	/*
	 * Anything after this point is a combination of coarse trap
	 * controls, which must all be evaluated to decide what to do.
	 */
	__MULTIPLE_CONTROL_BITS__,
	CGT_HCR_IMO_FMO = __MULTIPLE_CONTROL_BITS__,
	CGT_HCR_TID2_TID4,
	CGT_HCR_TTLB_TTLBIS,
	CGT_HCR_TTLB_TTLBOS,
	CGT_HCR_TVM_TRVM,
	CGT_HCR_TPU_TICAB,
	CGT_HCR_TPU_TOCU,
	CGT_HCR_NV1_nNV2_ENSCXT,
	CGT_MDCR_TPM_TPMCR,
	CGT_MDCR_TDE_TDA,
	CGT_MDCR_TDE_TDOSA,
	CGT_MDCR_TDE_TDRA,
	CGT_MDCR_TDCC_TDE_TDA,

	/*
	 * Anything after this point requires a callback evaluating a
	 * complex trap condition. Hopefully we'll never need this...
	 */
	__COMPLEX_CONDITIONS__,

	/* Must be last */
	__NR_CGT_GROUP_IDS__
};

static const struct trap_bits coarse_trap_bits[] = {
	[CGT_HCR_TID1] = {
		.index		= HCR_EL2,
		.value 		= HCR_TID1,
		.mask		= HCR_TID1,
		.behaviour	= BEHAVE_FORWARD_READ,
	},
	[CGT_HCR_TID2] = {
		.index		= HCR_EL2,
		.value 		= HCR_TID2,
		.mask		= HCR_TID2,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TID3] = {
		.index		= HCR_EL2,
		.value 		= HCR_TID3,
		.mask		= HCR_TID3,
		.behaviour	= BEHAVE_FORWARD_READ,
	},
	[CGT_HCR_IMO] = {
		.index		= HCR_EL2,
		.value 		= HCR_IMO,
		.mask		= HCR_IMO,
		.behaviour	= BEHAVE_FORWARD_WRITE,
	},
	[CGT_HCR_FMO] = {
		.index		= HCR_EL2,
		.value 		= HCR_FMO,
		.mask		= HCR_FMO,
		.behaviour	= BEHAVE_FORWARD_WRITE,
	},
	[CGT_HCR_TIDCP] = {
		.index		= HCR_EL2,
		.value		= HCR_TIDCP,
		.mask		= HCR_TIDCP,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TACR] = {
		.index		= HCR_EL2,
		.value		= HCR_TACR,
		.mask		= HCR_TACR,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TSW] = {
		.index		= HCR_EL2,
		.value		= HCR_TSW,
		.mask		= HCR_TSW,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TPC] = { /* Also called TCPC when FEAT_DPB is implemented */
		.index		= HCR_EL2,
		.value		= HCR_TPC,
		.mask		= HCR_TPC,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TPU] = {
		.index		= HCR_EL2,
		.value		= HCR_TPU,
		.mask		= HCR_TPU,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TTLB] = {
		.index		= HCR_EL2,
		.value		= HCR_TTLB,
		.mask		= HCR_TTLB,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TVM] = {
		.index		= HCR_EL2,
		.value		= HCR_TVM,
		.mask		= HCR_TVM,
		.behaviour	= BEHAVE_FORWARD_WRITE,
	},
	[CGT_HCR_TDZ] = {
		.index		= HCR_EL2,
		.value		= HCR_TDZ,
		.mask		= HCR_TDZ,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TRVM] = {
		.index		= HCR_EL2,
		.value		= HCR_TRVM,
		.mask		= HCR_TRVM,
		.behaviour	= BEHAVE_FORWARD_READ,
	},
	[CGT_HCR_TLOR] = {
		.index		= HCR_EL2,
		.value		= HCR_TLOR,
		.mask		= HCR_TLOR,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TERR] = {
		.index		= HCR_EL2,
		.value		= HCR_TERR,
		.mask		= HCR_TERR,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_APK] = {
		.index		= HCR_EL2,
		.value		= 0,
		.mask		= HCR_APK,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_NV] = {
		.index		= HCR_EL2,
		.value		= HCR_NV,
		.mask		= HCR_NV,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_NV_nNV2] = {
		.index		= HCR_EL2,
		.value		= HCR_NV,
		.mask		= HCR_NV | HCR_NV2,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_NV1_nNV2] = {
		.index		= HCR_EL2,
		.value		= HCR_NV | HCR_NV1,
		.mask		= HCR_NV | HCR_NV1 | HCR_NV2,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_AT] = {
		.index		= HCR_EL2,
		.value		= HCR_AT,
		.mask		= HCR_AT,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_nFIEN] = {
		.index		= HCR_EL2,
		.value		= 0,
		.mask		= HCR_FIEN,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TID4] = {
		.index		= HCR_EL2,
		.value 		= HCR_TID4,
		.mask		= HCR_TID4,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TICAB] = {
		.index		= HCR_EL2,
		.value 		= HCR_TICAB,
		.mask		= HCR_TICAB,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TOCU] = {
		.index		= HCR_EL2,
		.value 		= HCR_TOCU,
		.mask		= HCR_TOCU,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_ENSCXT] = {
		.index		= HCR_EL2,
		.value 		= 0,
		.mask		= HCR_ENSCXT,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TTLBIS] = {
		.index		= HCR_EL2,
		.value		= HCR_TTLBIS,
		.mask		= HCR_TTLBIS,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_HCR_TTLBOS] = {
		.index		= HCR_EL2,
		.value		= HCR_TTLBOS,
		.mask		= HCR_TTLBOS,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_MDCR_TPMCR] = {
		.index		= MDCR_EL2,
		.value		= MDCR_EL2_TPMCR,
		.mask		= MDCR_EL2_TPMCR,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_MDCR_TPM] = {
		.index		= MDCR_EL2,
		.value		= MDCR_EL2_TPM,
		.mask		= MDCR_EL2_TPM,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_MDCR_TDE] = {
		.index		= MDCR_EL2,
		.value		= MDCR_EL2_TDE,
		.mask		= MDCR_EL2_TDE,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_MDCR_TDA] = {
		.index		= MDCR_EL2,
		.value		= MDCR_EL2_TDA,
		.mask		= MDCR_EL2_TDA,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_MDCR_TDOSA] = {
		.index		= MDCR_EL2,
		.value		= MDCR_EL2_TDOSA,
		.mask		= MDCR_EL2_TDOSA,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_MDCR_TDRA] = {
		.index		= MDCR_EL2,
		.value		= MDCR_EL2_TDRA,
		.mask		= MDCR_EL2_TDRA,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_MDCR_E2PB] = {
		.index		= MDCR_EL2,
		.value		= 0,
		.mask		= BIT(MDCR_EL2_E2PB_SHIFT),
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_MDCR_TPMS] = {
		.index		= MDCR_EL2,
		.value		= MDCR_EL2_TPMS,
		.mask		= MDCR_EL2_TPMS,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_MDCR_TTRF] = {
		.index		= MDCR_EL2,
		.value		= MDCR_EL2_TTRF,
		.mask		= MDCR_EL2_TTRF,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_MDCR_E2TB] = {
		.index		= MDCR_EL2,
		.value		= 0,
		.mask		= BIT(MDCR_EL2_E2TB_SHIFT),
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
	[CGT_MDCR_TDCC] = {
		.index		= MDCR_EL2,
		.value		= MDCR_EL2_TDCC,
		.mask		= MDCR_EL2_TDCC,
		.behaviour	= BEHAVE_FORWARD_ANY,
	},
};

#define MCB(id, ...)						\
	[id - __MULTIPLE_CONTROL_BITS__]	=		\
		(const enum cgt_group_id[]){			\
		__VA_ARGS__, __RESERVED__			\
		}

static const enum cgt_group_id *coarse_control_combo[] = {
	MCB(CGT_HCR_IMO_FMO,		CGT_HCR_IMO, CGT_HCR_FMO),
	MCB(CGT_HCR_TID2_TID4,		CGT_HCR_TID2, CGT_HCR_TID4),
	MCB(CGT_HCR_TTLB_TTLBIS,	CGT_HCR_TTLB, CGT_HCR_TTLBIS),
	MCB(CGT_HCR_TTLB_TTLBOS,	CGT_HCR_TTLB, CGT_HCR_TTLBOS),
	MCB(CGT_HCR_TVM_TRVM,		CGT_HCR_TVM, CGT_HCR_TRVM),
	MCB(CGT_HCR_TPU_TICAB,		CGT_HCR_TPU, CGT_HCR_TICAB),
	MCB(CGT_HCR_TPU_TOCU,		CGT_HCR_TPU, CGT_HCR_TOCU),
	MCB(CGT_HCR_NV1_nNV2_ENSCXT,	CGT_HCR_NV1_nNV2, CGT_HCR_ENSCXT),
	MCB(CGT_MDCR_TPM_TPMCR,		CGT_MDCR_TPM, CGT_MDCR_TPMCR),
	MCB(CGT_MDCR_TDE_TDA,		CGT_MDCR_TDE, CGT_MDCR_TDA),
	MCB(CGT_MDCR_TDE_TDOSA,		CGT_MDCR_TDE, CGT_MDCR_TDOSA),
	MCB(CGT_MDCR_TDE_TDRA,		CGT_MDCR_TDE, CGT_MDCR_TDRA),
	MCB(CGT_MDCR_TDCC_TDE_TDA,	CGT_MDCR_TDCC, CGT_MDCR_TDE, CGT_MDCR_TDA),
};

typedef enum trap_behaviour (*complex_condition_check)(struct kvm_vcpu *);

#define CCC(id, fn)				\
	[id - __COMPLEX_CONDITIONS__] = fn

static const complex_condition_check ccc[] = {
};

/*
 * Bit assignment for the trap controls. We use a 64bit word with the
 * following layout for each trapped sysreg:
 *
 * [9:0]	enum cgt_group_id (10 bits)
 * [62:10]	Unused (53 bits)
 * [63]		RES0 - Must be zero, as lost on insertion in the xarray
 */
#define TC_CGT_BITS	10

union trap_config {
	u64	val;
	struct {
		unsigned long	cgt:TC_CGT_BITS; /* Coarse Grained Trap id */
		unsigned long	unused:53;	 /* Unused, should be zero */
		unsigned long	mbz:1;		 /* Must Be Zero */
	};
};

struct encoding_to_trap_config {
	const u32			encoding;
	const u32			end;
	const union trap_config		tc;
	const unsigned int		line;
};

#define SR_RANGE_TRAP(sr_start, sr_end, trap_id)			\
	{								\
		.encoding	= sr_start,				\
		.end		= sr_end,				\
		.tc		= {					\
			.cgt		= trap_id,			\
		},							\
		.line = __LINE__,					\
	}

#define SR_TRAP(sr, trap_id)		SR_RANGE_TRAP(sr, sr, trap_id)

/*
 * Map encoding to trap bits for exception reported with EC=0x18.
 * These must only be evaluated when running a nested hypervisor, but
 * that the current context is not a hypervisor context. When the
 * trapped access matches one of the trap controls, the exception is
 * re-injected in the nested hypervisor.
 */
static const struct encoding_to_trap_config encoding_to_cgt[] __initconst = {
	SR_TRAP(SYS_REVIDR_EL1,		CGT_HCR_TID1),
	SR_TRAP(SYS_AIDR_EL1,		CGT_HCR_TID1),
	SR_TRAP(SYS_SMIDR_EL1,		CGT_HCR_TID1),
	SR_TRAP(SYS_CTR_EL0,		CGT_HCR_TID2),
	SR_TRAP(SYS_CCSIDR_EL1,		CGT_HCR_TID2_TID4),
	SR_TRAP(SYS_CCSIDR2_EL1,	CGT_HCR_TID2_TID4),
	SR_TRAP(SYS_CLIDR_EL1,		CGT_HCR_TID2_TID4),
	SR_TRAP(SYS_CSSELR_EL1,		CGT_HCR_TID2_TID4),
	SR_RANGE_TRAP(SYS_ID_PFR0_EL1,
		      sys_reg(3, 0, 0, 7, 7), CGT_HCR_TID3),
	SR_TRAP(SYS_ICC_SGI0R_EL1,	CGT_HCR_IMO_FMO),
	SR_TRAP(SYS_ICC_ASGI1R_EL1,	CGT_HCR_IMO_FMO),
	SR_TRAP(SYS_ICC_SGI1R_EL1,	CGT_HCR_IMO_FMO),
	SR_RANGE_TRAP(sys_reg(3, 0, 11, 0, 0),
		      sys_reg(3, 0, 11, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 1, 11, 0, 0),
		      sys_reg(3, 1, 11, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 2, 11, 0, 0),
		      sys_reg(3, 2, 11, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 3, 11, 0, 0),
		      sys_reg(3, 3, 11, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 4, 11, 0, 0),
		      sys_reg(3, 4, 11, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 5, 11, 0, 0),
		      sys_reg(3, 5, 11, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 6, 11, 0, 0),
		      sys_reg(3, 6, 11, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 7, 11, 0, 0),
		      sys_reg(3, 7, 11, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 0, 15, 0, 0),
		      sys_reg(3, 0, 15, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 1, 15, 0, 0),
		      sys_reg(3, 1, 15, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 2, 15, 0, 0),
		      sys_reg(3, 2, 15, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 3, 15, 0, 0),
		      sys_reg(3, 3, 15, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 4, 15, 0, 0),
		      sys_reg(3, 4, 15, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 5, 15, 0, 0),
		      sys_reg(3, 5, 15, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 6, 15, 0, 0),
		      sys_reg(3, 6, 15, 15, 7), CGT_HCR_TIDCP),
	SR_RANGE_TRAP(sys_reg(3, 7, 15, 0, 0),
		      sys_reg(3, 7, 15, 15, 7), CGT_HCR_TIDCP),
	SR_TRAP(SYS_ACTLR_EL1,		CGT_HCR_TACR),
	SR_TRAP(SYS_DC_ISW,		CGT_HCR_TSW),
	SR_TRAP(SYS_DC_CSW,		CGT_HCR_TSW),
	SR_TRAP(SYS_DC_CISW,		CGT_HCR_TSW),
	SR_TRAP(SYS_DC_IGSW,		CGT_HCR_TSW),
	SR_TRAP(SYS_DC_IGDSW,		CGT_HCR_TSW),
	SR_TRAP(SYS_DC_CGSW,		CGT_HCR_TSW),
	SR_TRAP(SYS_DC_CGDSW,		CGT_HCR_TSW),
	SR_TRAP(SYS_DC_CIGSW,		CGT_HCR_TSW),
	SR_TRAP(SYS_DC_CIGDSW,		CGT_HCR_TSW),
	SR_TRAP(SYS_DC_CIVAC,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_CVAC,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_CVAP,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_CVADP,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_IVAC,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_CIGVAC,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_CIGDVAC,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_IGVAC,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_IGDVAC,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_CGVAC,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_CGDVAC,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_CGVAP,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_CGDVAP,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_CGVADP,		CGT_HCR_TPC),
	SR_TRAP(SYS_DC_CGDVADP,		CGT_HCR_TPC),
	SR_TRAP(SYS_IC_IVAU,		CGT_HCR_TPU_TOCU),
	SR_TRAP(SYS_IC_IALLU,		CGT_HCR_TPU_TOCU),
	SR_TRAP(SYS_IC_IALLUIS,		CGT_HCR_TPU_TICAB),
	SR_TRAP(SYS_DC_CVAU,		CGT_HCR_TPU_TOCU),
	SR_TRAP(OP_TLBI_RVAE1,		CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_RVAAE1,		CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_RVALE1,		CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_RVAALE1,	CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_VMALLE1,	CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_VAE1,		CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_ASIDE1,		CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_VAAE1,		CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_VALE1,		CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_VAALE1,		CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_RVAE1NXS,	CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_RVAAE1NXS,	CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_RVALE1NXS,	CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_RVAALE1NXS,	CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_VMALLE1NXS,	CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_VAE1NXS,	CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_ASIDE1NXS,	CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_VAAE1NXS,	CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_VALE1NXS,	CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_VAALE1NXS,	CGT_HCR_TTLB),
	SR_TRAP(OP_TLBI_RVAE1IS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_RVAAE1IS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_RVALE1IS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_RVAALE1IS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_VMALLE1IS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_VAE1IS,		CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_ASIDE1IS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_VAAE1IS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_VALE1IS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_VAALE1IS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_RVAE1ISNXS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_RVAAE1ISNXS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_RVALE1ISNXS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_RVAALE1ISNXS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_VMALLE1ISNXS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_VAE1ISNXS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_ASIDE1ISNXS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_VAAE1ISNXS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_VALE1ISNXS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_VAALE1ISNXS,	CGT_HCR_TTLB_TTLBIS),
	SR_TRAP(OP_TLBI_VMALLE1OS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_VAE1OS,		CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_ASIDE1OS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_VAAE1OS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_VALE1OS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_VAALE1OS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_RVAE1OS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_RVAAE1OS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_RVALE1OS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_RVAALE1OS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_VMALLE1OSNXS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_VAE1OSNXS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_ASIDE1OSNXS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_VAAE1OSNXS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_VALE1OSNXS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_VAALE1OSNXS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_RVAE1OSNXS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_RVAAE1OSNXS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_RVALE1OSNXS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(OP_TLBI_RVAALE1OSNXS,	CGT_HCR_TTLB_TTLBOS),
	SR_TRAP(SYS_SCTLR_EL1,		CGT_HCR_TVM_TRVM),
	SR_TRAP(SYS_TTBR0_EL1,		CGT_HCR_TVM_TRVM),
	SR_TRAP(SYS_TTBR1_EL1,		CGT_HCR_TVM_TRVM),
	SR_TRAP(SYS_TCR_EL1,		CGT_HCR_TVM_TRVM),
	SR_TRAP(SYS_ESR_EL1,		CGT_HCR_TVM_TRVM),
	SR_TRAP(SYS_FAR_EL1,		CGT_HCR_TVM_TRVM),
	SR_TRAP(SYS_AFSR0_EL1,		CGT_HCR_TVM_TRVM),
	SR_TRAP(SYS_AFSR1_EL1,		CGT_HCR_TVM_TRVM),
	SR_TRAP(SYS_MAIR_EL1,		CGT_HCR_TVM_TRVM),
	SR_TRAP(SYS_AMAIR_EL1,		CGT_HCR_TVM_TRVM),
	SR_TRAP(SYS_CONTEXTIDR_EL1,	CGT_HCR_TVM_TRVM),
	SR_TRAP(SYS_DC_ZVA,		CGT_HCR_TDZ),
	SR_TRAP(SYS_DC_GVA,		CGT_HCR_TDZ),
	SR_TRAP(SYS_DC_GZVA,		CGT_HCR_TDZ),
	SR_TRAP(SYS_LORSA_EL1,		CGT_HCR_TLOR),
	SR_TRAP(SYS_LOREA_EL1, 		CGT_HCR_TLOR),
	SR_TRAP(SYS_LORN_EL1, 		CGT_HCR_TLOR),
	SR_TRAP(SYS_LORC_EL1, 		CGT_HCR_TLOR),
	SR_TRAP(SYS_LORID_EL1,		CGT_HCR_TLOR),
	SR_TRAP(SYS_ERRIDR_EL1,		CGT_HCR_TERR),
	SR_TRAP(SYS_ERRSELR_EL1,	CGT_HCR_TERR),
	SR_TRAP(SYS_ERXADDR_EL1,	CGT_HCR_TERR),
	SR_TRAP(SYS_ERXCTLR_EL1,	CGT_HCR_TERR),
	SR_TRAP(SYS_ERXFR_EL1,		CGT_HCR_TERR),
	SR_TRAP(SYS_ERXMISC0_EL1,	CGT_HCR_TERR),
	SR_TRAP(SYS_ERXMISC1_EL1,	CGT_HCR_TERR),
	SR_TRAP(SYS_ERXMISC2_EL1,	CGT_HCR_TERR),
	SR_TRAP(SYS_ERXMISC3_EL1,	CGT_HCR_TERR),
	SR_TRAP(SYS_ERXSTATUS_EL1,	CGT_HCR_TERR),
	SR_TRAP(SYS_APIAKEYLO_EL1,	CGT_HCR_APK),
	SR_TRAP(SYS_APIAKEYHI_EL1,	CGT_HCR_APK),
	SR_TRAP(SYS_APIBKEYLO_EL1,	CGT_HCR_APK),
	SR_TRAP(SYS_APIBKEYHI_EL1,	CGT_HCR_APK),
	SR_TRAP(SYS_APDAKEYLO_EL1,	CGT_HCR_APK),
	SR_TRAP(SYS_APDAKEYHI_EL1,	CGT_HCR_APK),
	SR_TRAP(SYS_APDBKEYLO_EL1,	CGT_HCR_APK),
	SR_TRAP(SYS_APDBKEYHI_EL1,	CGT_HCR_APK),
	SR_TRAP(SYS_APGAKEYLO_EL1,	CGT_HCR_APK),
	SR_TRAP(SYS_APGAKEYHI_EL1,	CGT_HCR_APK),
	/* All _EL2 registers */
	SR_RANGE_TRAP(sys_reg(3, 4, 0, 0, 0),
		      sys_reg(3, 4, 3, 15, 7), CGT_HCR_NV),
	/* Skip the SP_EL1 encoding... */
	SR_RANGE_TRAP(sys_reg(3, 4, 4, 1, 1),
		      sys_reg(3, 4, 10, 15, 7), CGT_HCR_NV),
	SR_RANGE_TRAP(sys_reg(3, 4, 12, 0, 0),
		      sys_reg(3, 4, 14, 15, 7), CGT_HCR_NV),
	/* All _EL02, _EL12 registers */
	SR_RANGE_TRAP(sys_reg(3, 5, 0, 0, 0),
		      sys_reg(3, 5, 10, 15, 7), CGT_HCR_NV),
	SR_RANGE_TRAP(sys_reg(3, 5, 12, 0, 0),
		      sys_reg(3, 5, 14, 15, 7), CGT_HCR_NV),
	SR_TRAP(OP_AT_S1E2R,		CGT_HCR_NV),
	SR_TRAP(OP_AT_S1E2W,		CGT_HCR_NV),
	SR_TRAP(OP_AT_S12E1R,		CGT_HCR_NV),
	SR_TRAP(OP_AT_S12E1W,		CGT_HCR_NV),
	SR_TRAP(OP_AT_S12E0R,		CGT_HCR_NV),
	SR_TRAP(OP_AT_S12E0W,		CGT_HCR_NV),
	SR_TRAP(OP_TLBI_IPAS2E1,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RIPAS2E1,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_IPAS2LE1,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RIPAS2LE1,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RVAE2,		CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RVALE2,		CGT_HCR_NV),
	SR_TRAP(OP_TLBI_ALLE2,		CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VAE2,		CGT_HCR_NV),
	SR_TRAP(OP_TLBI_ALLE1,		CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VALE2,		CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VMALLS12E1,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_IPAS2E1NXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RIPAS2E1NXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_IPAS2LE1NXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RIPAS2LE1NXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RVAE2NXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RVALE2NXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_ALLE2NXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VAE2NXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_ALLE1NXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VALE2NXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VMALLS12E1NXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_IPAS2E1IS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RIPAS2E1IS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_IPAS2LE1IS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RIPAS2LE1IS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RVAE2IS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RVALE2IS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_ALLE2IS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VAE2IS,		CGT_HCR_NV),
	SR_TRAP(OP_TLBI_ALLE1IS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VALE2IS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VMALLS12E1IS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_IPAS2E1ISNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RIPAS2E1ISNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_IPAS2LE1ISNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RIPAS2LE1ISNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RVAE2ISNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RVALE2ISNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_ALLE2ISNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VAE2ISNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_ALLE1ISNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VALE2ISNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VMALLS12E1ISNXS,CGT_HCR_NV),
	SR_TRAP(OP_TLBI_ALLE2OS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VAE2OS,		CGT_HCR_NV),
	SR_TRAP(OP_TLBI_ALLE1OS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VALE2OS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VMALLS12E1OS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_IPAS2E1OS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RIPAS2E1OS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_IPAS2LE1OS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RIPAS2LE1OS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RVAE2OS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RVALE2OS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_ALLE2OSNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VAE2OSNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_ALLE1OSNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VALE2OSNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_VMALLS12E1OSNXS,CGT_HCR_NV),
	SR_TRAP(OP_TLBI_IPAS2E1OSNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RIPAS2E1OSNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_IPAS2LE1OSNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RIPAS2LE1OSNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RVAE2OSNXS,	CGT_HCR_NV),
	SR_TRAP(OP_TLBI_RVALE2OSNXS,	CGT_HCR_NV),
	SR_TRAP(OP_CPP_RCTX, 		CGT_HCR_NV),
	SR_TRAP(OP_DVP_RCTX, 		CGT_HCR_NV),
	SR_TRAP(OP_CFP_RCTX, 		CGT_HCR_NV),
	SR_TRAP(SYS_SP_EL1,		CGT_HCR_NV_nNV2),
	SR_TRAP(SYS_VBAR_EL1,		CGT_HCR_NV1_nNV2),
	SR_TRAP(SYS_ELR_EL1,		CGT_HCR_NV1_nNV2),
	SR_TRAP(SYS_SPSR_EL1,		CGT_HCR_NV1_nNV2),
	SR_TRAP(SYS_SCXTNUM_EL1,	CGT_HCR_NV1_nNV2_ENSCXT),
	SR_TRAP(SYS_SCXTNUM_EL0,	CGT_HCR_ENSCXT),
	SR_TRAP(OP_AT_S1E1R, 		CGT_HCR_AT),
	SR_TRAP(OP_AT_S1E1W, 		CGT_HCR_AT),
	SR_TRAP(OP_AT_S1E0R, 		CGT_HCR_AT),
	SR_TRAP(OP_AT_S1E0W, 		CGT_HCR_AT),
	SR_TRAP(OP_AT_S1E1RP, 		CGT_HCR_AT),
	SR_TRAP(OP_AT_S1E1WP, 		CGT_HCR_AT),
	SR_TRAP(SYS_ERXPFGF_EL1,	CGT_HCR_nFIEN),
	SR_TRAP(SYS_ERXPFGCTL_EL1,	CGT_HCR_nFIEN),
	SR_TRAP(SYS_ERXPFGCDN_EL1,	CGT_HCR_nFIEN),
	SR_TRAP(SYS_PMCR_EL0,		CGT_MDCR_TPM_TPMCR),
	SR_TRAP(SYS_PMCNTENSET_EL0,	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMCNTENCLR_EL0,	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMOVSSET_EL0,	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMOVSCLR_EL0,	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMCEID0_EL0,	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMCEID1_EL0,	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMXEVTYPER_EL0,	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMSWINC_EL0,	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMSELR_EL0,		CGT_MDCR_TPM),
	SR_TRAP(SYS_PMXEVCNTR_EL0,	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMCCNTR_EL0,	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMUSERENR_EL0,	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMINTENSET_EL1,	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMINTENCLR_EL1,	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMMIR_EL1,		CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(0),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(1),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(2),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(3),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(4),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(5),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(6),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(7),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(8),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(9),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(10),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(11),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(12),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(13),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(14),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(15),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(16),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(17),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(18),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(19),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(20),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(21),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(22),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(23),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(24),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(25),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(26),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(27),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(28),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(29),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVCNTRn_EL0(30),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(0),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(1),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(2),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(3),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(4),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(5),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(6),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(7),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(8),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(9),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(10),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(11),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(12),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(13),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(14),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(15),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(16),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(17),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(18),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(19),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(20),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(21),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(22),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(23),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(24),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(25),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(26),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(27),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(28),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(29),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMEVTYPERn_EL0(30),	CGT_MDCR_TPM),
	SR_TRAP(SYS_PMCCFILTR_EL0,	CGT_MDCR_TPM),
	SR_TRAP(SYS_MDCCSR_EL0,		CGT_MDCR_TDCC_TDE_TDA),
	SR_TRAP(SYS_MDCCINT_EL1,	CGT_MDCR_TDCC_TDE_TDA),
	SR_TRAP(SYS_OSDTRRX_EL1,	CGT_MDCR_TDCC_TDE_TDA),
	SR_TRAP(SYS_OSDTRTX_EL1,	CGT_MDCR_TDCC_TDE_TDA),
	SR_TRAP(SYS_DBGDTR_EL0,		CGT_MDCR_TDCC_TDE_TDA),
	/*
	 * Also covers DBGDTRRX_EL0, which has the same encoding as
	 * SYS_DBGDTRTX_EL0...
	 */
	SR_TRAP(SYS_DBGDTRTX_EL0,	CGT_MDCR_TDCC_TDE_TDA),
	SR_TRAP(SYS_MDSCR_EL1,		CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_OSECCR_EL1,		CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(0),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(1),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(2),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(3),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(4),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(5),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(6),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(7),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(8),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(9),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(10),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(11),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(12),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(13),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(14),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBVRn_EL1(15),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(0),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(1),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(2),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(3),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(4),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(5),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(6),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(7),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(8),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(9),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(10),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(11),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(12),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(13),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(14),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGBCRn_EL1(15),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(0),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(1),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(2),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(3),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(4),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(5),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(6),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(7),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(8),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(9),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(10),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(11),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(12),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(13),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(14),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWVRn_EL1(15),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(0),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(1),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(2),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(3),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(4),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(5),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(6),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(7),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(8),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(9),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(10),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(11),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(12),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(13),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGWCRn_EL1(14),	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGCLAIMSET_EL1,	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGCLAIMCLR_EL1,	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_DBGAUTHSTATUS_EL1,	CGT_MDCR_TDE_TDA),
	SR_TRAP(SYS_OSLAR_EL1,		CGT_MDCR_TDE_TDOSA),
	SR_TRAP(SYS_OSLSR_EL1,		CGT_MDCR_TDE_TDOSA),
	SR_TRAP(SYS_OSDLR_EL1,		CGT_MDCR_TDE_TDOSA),
	SR_TRAP(SYS_DBGPRCR_EL1,	CGT_MDCR_TDE_TDOSA),
	SR_TRAP(SYS_MDRAR_EL1,		CGT_MDCR_TDE_TDRA),
	SR_TRAP(SYS_PMBLIMITR_EL1,	CGT_MDCR_E2PB),
	SR_TRAP(SYS_PMBPTR_EL1,		CGT_MDCR_E2PB),
	SR_TRAP(SYS_PMBSR_EL1,		CGT_MDCR_E2PB),
	SR_TRAP(SYS_PMSCR_EL1,		CGT_MDCR_TPMS),
	SR_TRAP(SYS_PMSEVFR_EL1,	CGT_MDCR_TPMS),
	SR_TRAP(SYS_PMSFCR_EL1,		CGT_MDCR_TPMS),
	SR_TRAP(SYS_PMSICR_EL1,		CGT_MDCR_TPMS),
	SR_TRAP(SYS_PMSIDR_EL1,		CGT_MDCR_TPMS),
	SR_TRAP(SYS_PMSIRR_EL1,		CGT_MDCR_TPMS),
	SR_TRAP(SYS_PMSLATFR_EL1,	CGT_MDCR_TPMS),
	SR_TRAP(SYS_PMSNEVFR_EL1,	CGT_MDCR_TPMS),
	SR_TRAP(SYS_TRFCR_EL1,		CGT_MDCR_TTRF),
	SR_TRAP(SYS_TRBBASER_EL1,	CGT_MDCR_E2TB),
	SR_TRAP(SYS_TRBLIMITR_EL1,	CGT_MDCR_E2TB),
	SR_TRAP(SYS_TRBMAR_EL1, 	CGT_MDCR_E2TB),
	SR_TRAP(SYS_TRBPTR_EL1, 	CGT_MDCR_E2TB),
	SR_TRAP(SYS_TRBSR_EL1, 		CGT_MDCR_E2TB),
	SR_TRAP(SYS_TRBTRG_EL1,		CGT_MDCR_E2TB),
};

static DEFINE_XARRAY(sr_forward_xa);

static union trap_config get_trap_config(u32 sysreg)
{
	return (union trap_config) {
		.val = xa_to_value(xa_load(&sr_forward_xa, sysreg)),
	};
}

static __init void print_nv_trap_error(const struct encoding_to_trap_config *tc,
				       const char *type, int err)
{
	kvm_err("%s line %d encoding range "
		"(%d, %d, %d, %d, %d) - (%d, %d, %d, %d, %d) (err=%d)\n",
		type, tc->line,
		sys_reg_Op0(tc->encoding), sys_reg_Op1(tc->encoding),
		sys_reg_CRn(tc->encoding), sys_reg_CRm(tc->encoding),
		sys_reg_Op2(tc->encoding),
		sys_reg_Op0(tc->end), sys_reg_Op1(tc->end),
		sys_reg_CRn(tc->end), sys_reg_CRm(tc->end),
		sys_reg_Op2(tc->end),
		err);
}

int __init populate_nv_trap_config(void)
{
	int ret = 0;

	BUILD_BUG_ON(sizeof(union trap_config) != sizeof(void *));
	BUILD_BUG_ON(__NR_CGT_GROUP_IDS__ > BIT(TC_CGT_BITS));

	for (int i = 0; i < ARRAY_SIZE(encoding_to_cgt); i++) {
		const struct encoding_to_trap_config *cgt = &encoding_to_cgt[i];
		void *prev;

		if (cgt->tc.val & BIT(63)) {
			kvm_err("CGT[%d] has MBZ bit set\n", i);
			ret = -EINVAL;
		}

		if (cgt->encoding != cgt->end) {
			prev = xa_store_range(&sr_forward_xa,
					      cgt->encoding, cgt->end,
					      xa_mk_value(cgt->tc.val),
					      GFP_KERNEL);
		} else {
			prev = xa_store(&sr_forward_xa, cgt->encoding,
					xa_mk_value(cgt->tc.val), GFP_KERNEL);
			if (prev && !xa_is_err(prev)) {
				ret = -EINVAL;
				print_nv_trap_error(cgt, "Duplicate CGT", ret);
			}
		}

		if (xa_is_err(prev)) {
			ret = xa_err(prev);
			print_nv_trap_error(cgt, "Failed CGT insertion", ret);
		}
	}

	kvm_info("nv: %ld coarse grained trap handlers\n",
		 ARRAY_SIZE(encoding_to_cgt));

	for (int id = __MULTIPLE_CONTROL_BITS__; id < __COMPLEX_CONDITIONS__; id++) {
		const enum cgt_group_id *cgids;

		cgids = coarse_control_combo[id - __MULTIPLE_CONTROL_BITS__];

		for (int i = 0; cgids[i] != __RESERVED__; i++) {
			if (cgids[i] >= __MULTIPLE_CONTROL_BITS__) {
				kvm_err("Recursive MCB %d/%d\n", id, cgids[i]);
				ret = -EINVAL;
			}
		}
	}

	if (ret)
		xa_destroy(&sr_forward_xa);

	return ret;
}

static enum trap_behaviour get_behaviour(struct kvm_vcpu *vcpu,
					 const struct trap_bits *tb)
{
	enum trap_behaviour b = BEHAVE_HANDLE_LOCALLY;
	u64 val;

	val = __vcpu_sys_reg(vcpu, tb->index);
	if ((val & tb->mask) == tb->value)
		b |= tb->behaviour;

	return b;
}

static enum trap_behaviour __compute_trap_behaviour(struct kvm_vcpu *vcpu,
						    const enum cgt_group_id id,
						    enum trap_behaviour b)
{
	switch (id) {
		const enum cgt_group_id *cgids;

	case __RESERVED__ ... __MULTIPLE_CONTROL_BITS__ - 1:
		if (likely(id != __RESERVED__))
			b |= get_behaviour(vcpu, &coarse_trap_bits[id]);
		break;
	case __MULTIPLE_CONTROL_BITS__ ... __COMPLEX_CONDITIONS__ - 1:
		/* Yes, this is recursive. Don't do anything stupid. */
		cgids = coarse_control_combo[id - __MULTIPLE_CONTROL_BITS__];
		for (int i = 0; cgids[i] != __RESERVED__; i++)
			b |= __compute_trap_behaviour(vcpu, cgids[i], b);
		break;
	default:
		if (ARRAY_SIZE(ccc))
			b |= ccc[id -  __COMPLEX_CONDITIONS__](vcpu);
		break;
	}

	return b;
}

static enum trap_behaviour compute_trap_behaviour(struct kvm_vcpu *vcpu,
						  const union trap_config tc)
{
	enum trap_behaviour b = BEHAVE_HANDLE_LOCALLY;

	return __compute_trap_behaviour(vcpu, tc.cgt, b);
}

bool __check_nv_sr_forward(struct kvm_vcpu *vcpu)
{
	union trap_config tc;
	enum trap_behaviour b;
	bool is_read;
	u32 sysreg;
	u64 esr;

	if (!vcpu_has_nv(vcpu) || is_hyp_ctxt(vcpu))
		return false;

	esr = kvm_vcpu_get_esr(vcpu);
	sysreg = esr_sys64_to_sysreg(esr);
	is_read = (esr & ESR_ELx_SYS64_ISS_DIR_MASK) == ESR_ELx_SYS64_ISS_DIR_READ;

	tc = get_trap_config(sysreg);

	/*
	 * A value of 0 for the whole entry means that we know nothing
	 * for this sysreg, and that it cannot be re-injected into the
	 * nested hypervisor. In this situation, let's cut it short.
	 *
	 * Note that ultimately, we could also make use of the xarray
	 * to store the index of the sysreg in the local descriptor
	 * array, avoiding another search... Hint, hint...
	 */
	if (!tc.val)
		return false;

	b = compute_trap_behaviour(vcpu, tc);

	if (((b & BEHAVE_FORWARD_READ) && is_read) ||
	    ((b & BEHAVE_FORWARD_WRITE) && !is_read))
		goto inject;

	return false;

inject:
	trace_kvm_forward_sysreg_trap(vcpu, sysreg, is_read);

	kvm_inject_nested_sync(vcpu, kvm_vcpu_get_esr(vcpu));
	return true;
}

static u64 kvm_check_illegal_exception_return(struct kvm_vcpu *vcpu, u64 spsr)
{
	u64 mode = spsr & PSR_MODE_MASK;

	/*
	 * Possible causes for an Illegal Exception Return from EL2:
	 * - trying to return to EL3
	 * - trying to return to an illegal M value
	 * - trying to return to a 32bit EL
	 * - trying to return to EL1 with HCR_EL2.TGE set
	 */
	if (mode == PSR_MODE_EL3t   || mode == PSR_MODE_EL3h ||
	    mode == 0b00001         || (mode & BIT(1))       ||
	    (spsr & PSR_MODE32_BIT) ||
	    (vcpu_el2_tge_is_set(vcpu) && (mode == PSR_MODE_EL1t ||
					   mode == PSR_MODE_EL1h))) {
		/*
		 * The guest is playing with our nerves. Preserve EL, SP,
		 * masks, flags from the existing PSTATE, and set IL.
		 * The HW will then generate an Illegal State Exception
		 * immediately after ERET.
		 */
		spsr = *vcpu_cpsr(vcpu);

		spsr &= (PSR_D_BIT | PSR_A_BIT | PSR_I_BIT | PSR_F_BIT |
			 PSR_N_BIT | PSR_Z_BIT | PSR_C_BIT | PSR_V_BIT |
			 PSR_MODE_MASK | PSR_MODE32_BIT);
		spsr |= PSR_IL_BIT;
	}

	return spsr;
}

void kvm_emulate_nested_eret(struct kvm_vcpu *vcpu)
{
	u64 spsr, elr, mode;
	bool direct_eret;

	/*
	 * Going through the whole put/load motions is a waste of time
	 * if this is a VHE guest hypervisor returning to its own
	 * userspace, or the hypervisor performing a local exception
	 * return. No need to save/restore registers, no need to
	 * switch S2 MMU. Just do the canonical ERET.
	 */
	spsr = vcpu_read_sys_reg(vcpu, SPSR_EL2);
	spsr = kvm_check_illegal_exception_return(vcpu, spsr);

	mode = spsr & (PSR_MODE_MASK | PSR_MODE32_BIT);

	direct_eret  = (mode == PSR_MODE_EL0t &&
			vcpu_el2_e2h_is_set(vcpu) &&
			vcpu_el2_tge_is_set(vcpu));
	direct_eret |= (mode == PSR_MODE_EL2h || mode == PSR_MODE_EL2t);

	if (direct_eret) {
		*vcpu_pc(vcpu) = vcpu_read_sys_reg(vcpu, ELR_EL2);
		*vcpu_cpsr(vcpu) = spsr;
		trace_kvm_nested_eret(vcpu, *vcpu_pc(vcpu), spsr);
		return;
	}

	preempt_disable();
	kvm_arch_vcpu_put(vcpu);

	elr = __vcpu_sys_reg(vcpu, ELR_EL2);

	trace_kvm_nested_eret(vcpu, elr, spsr);

	/*
	 * Note that the current exception level is always the virtual EL2,
	 * since we set HCR_EL2.NV bit only when entering the virtual EL2.
	 */
	*vcpu_pc(vcpu) = elr;
	*vcpu_cpsr(vcpu) = spsr;

	kvm_arch_vcpu_load(vcpu, smp_processor_id());
	preempt_enable();
}

static void kvm_inject_el2_exception(struct kvm_vcpu *vcpu, u64 esr_el2,
				     enum exception_type type)
{
	trace_kvm_inject_nested_exception(vcpu, esr_el2, type);

	switch (type) {
	case except_type_sync:
		kvm_pend_exception(vcpu, EXCEPT_AA64_EL2_SYNC);
		vcpu_write_sys_reg(vcpu, esr_el2, ESR_EL2);
		break;
	case except_type_irq:
		kvm_pend_exception(vcpu, EXCEPT_AA64_EL2_IRQ);
		break;
	default:
		WARN_ONCE(1, "Unsupported EL2 exception injection %d\n", type);
	}
}

/*
 * Emulate taking an exception to EL2.
 * See ARM ARM J8.1.2 AArch64.TakeException()
 */
static int kvm_inject_nested(struct kvm_vcpu *vcpu, u64 esr_el2,
			     enum exception_type type)
{
	u64 pstate, mode;
	bool direct_inject;

	if (!vcpu_has_nv(vcpu)) {
		kvm_err("Unexpected call to %s for the non-nesting configuration\n",
				__func__);
		return -EINVAL;
	}

	/*
	 * As for ERET, we can avoid doing too much on the injection path by
	 * checking that we either took the exception from a VHE host
	 * userspace or from vEL2. In these cases, there is no change in
	 * translation regime (or anything else), so let's do as little as
	 * possible.
	 */
	pstate = *vcpu_cpsr(vcpu);
	mode = pstate & (PSR_MODE_MASK | PSR_MODE32_BIT);

	direct_inject  = (mode == PSR_MODE_EL0t &&
			  vcpu_el2_e2h_is_set(vcpu) &&
			  vcpu_el2_tge_is_set(vcpu));
	direct_inject |= (mode == PSR_MODE_EL2h || mode == PSR_MODE_EL2t);

	if (direct_inject) {
		kvm_inject_el2_exception(vcpu, esr_el2, type);
		return 1;
	}

	preempt_disable();

	/*
	 * We may have an exception or PC update in the EL0/EL1 context.
	 * Commit it before entering EL2.
	 */
	__kvm_adjust_pc(vcpu);

	kvm_arch_vcpu_put(vcpu);

	kvm_inject_el2_exception(vcpu, esr_el2, type);

	/*
	 * A hard requirement is that a switch between EL1 and EL2
	 * contexts has to happen between a put/load, so that we can
	 * pick the correct timer and interrupt configuration, among
	 * other things.
	 *
	 * Make sure the exception actually took place before we load
	 * the new context.
	 */
	__kvm_adjust_pc(vcpu);

	kvm_arch_vcpu_load(vcpu, smp_processor_id());
	preempt_enable();

	return 1;
}

int kvm_inject_nested_sync(struct kvm_vcpu *vcpu, u64 esr_el2)
{
	return kvm_inject_nested(vcpu, esr_el2, except_type_sync);
}

int kvm_inject_nested_irq(struct kvm_vcpu *vcpu)
{
	/*
	 * Do not inject an irq if the:
	 *  - Current exception level is EL2, and
	 *  - virtual HCR_EL2.TGE == 0
	 *  - virtual HCR_EL2.IMO == 0
	 *
	 * See Table D1-17 "Physical interrupt target and masking when EL3 is
	 * not implemented and EL2 is implemented" in ARM DDI 0487C.a.
	 */

	if (vcpu_is_el2(vcpu) && !vcpu_el2_tge_is_set(vcpu) &&
	    !(__vcpu_sys_reg(vcpu, HCR_EL2) & HCR_IMO))
		return 1;

	/* esr_el2 value doesn't matter for exits due to irqs. */
	return kvm_inject_nested(vcpu, 0, except_type_irq);
}
