/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Intel Corporation.
 */

#ifndef __INTEL_PCH__
#define __INTEL_PCH__

struct intel_display;

/*
 * Sorted by south display engine compatibility.
 * If the new PCH comes with a south display engine that is not
 * inherited from the latest item, please do not add it to the
 * end. Instead, add it right after its "parent" PCH.
 */
enum intel_pch {
	PCH_NOP = -1,	/* PCH without south display */
	PCH_NONE = 0,	/* No PCH present */
	PCH_IBX,	/* Ibexpeak PCH */
	PCH_CPT,	/* Cougarpoint/Pantherpoint PCH */
	PCH_LPT_H,	/* Lynxpoint/Wildcatpoint H PCH */
	PCH_LPT_LP,	/* Lynxpoint/Wildcatpoint LP PCH */
	PCH_SPT,        /* Sunrisepoint/Kaby Lake PCH */
	PCH_CNP,        /* Cannon/Comet Lake PCH */
	PCH_ICP,	/* Ice Lake/Jasper Lake PCH */
	PCH_TGP,	/* Tiger Lake/Mule Creek Canyon PCH */
	PCH_ADP,	/* Alder Lake PCH */

	/* Fake PCHs, functionality handled on the same PCI dev */
	PCH_DG1 = 1024,
	PCH_DG2,
	PCH_MTL,
	PCH_LNL,
};

#define INTEL_PCH_TYPE(_display)		((_display)->pch_type)
#define HAS_PCH_DG2(display)			(INTEL_PCH_TYPE(display) == PCH_DG2)
#define HAS_PCH_ADP(display)			(INTEL_PCH_TYPE(display) == PCH_ADP)
#define HAS_PCH_DG1(display)			(INTEL_PCH_TYPE(display) == PCH_DG1)
#define HAS_PCH_TGP(display)			(INTEL_PCH_TYPE(display) == PCH_TGP)
#define HAS_PCH_ICP(display)			(INTEL_PCH_TYPE(display) == PCH_ICP)
#define HAS_PCH_CNP(display)			(INTEL_PCH_TYPE(display) == PCH_CNP)
#define HAS_PCH_SPT(display)			(INTEL_PCH_TYPE(display) == PCH_SPT)
#define HAS_PCH_LPT_H(display)			(INTEL_PCH_TYPE(display) == PCH_LPT_H)
#define HAS_PCH_LPT_LP(display)			(INTEL_PCH_TYPE(display) == PCH_LPT_LP)
#define HAS_PCH_LPT(display)			(INTEL_PCH_TYPE(display) == PCH_LPT_H || \
						 INTEL_PCH_TYPE(display) == PCH_LPT_LP)
#define HAS_PCH_CPT(display)			(INTEL_PCH_TYPE(display) == PCH_CPT)
#define HAS_PCH_IBX(display)			(INTEL_PCH_TYPE(display) == PCH_IBX)
#define HAS_PCH_NOP(display)			(INTEL_PCH_TYPE(display) == PCH_NOP)
#define HAS_PCH_SPLIT(display)			(INTEL_PCH_TYPE(display) != PCH_NONE)

void intel_pch_detect(struct intel_display *display);

#endif /* __INTEL_PCH__ */
