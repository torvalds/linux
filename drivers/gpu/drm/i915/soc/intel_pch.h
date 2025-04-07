/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2019 Intel Corporation.
 */

#ifndef __INTEL_PCH__
#define __INTEL_PCH__

struct drm_i915_private;

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

#define INTEL_PCH_TYPE(dev_priv)		((dev_priv)->pch_type)
#define HAS_PCH_DG2(dev_priv)			(INTEL_PCH_TYPE(dev_priv) == PCH_DG2)
#define HAS_PCH_ADP(dev_priv)			(INTEL_PCH_TYPE(dev_priv) == PCH_ADP)
#define HAS_PCH_DG1(dev_priv)			(INTEL_PCH_TYPE(dev_priv) == PCH_DG1)
#define HAS_PCH_TGP(dev_priv)			(INTEL_PCH_TYPE(dev_priv) == PCH_TGP)
#define HAS_PCH_ICP(dev_priv)			(INTEL_PCH_TYPE(dev_priv) == PCH_ICP)
#define HAS_PCH_CNP(dev_priv)			(INTEL_PCH_TYPE(dev_priv) == PCH_CNP)
#define HAS_PCH_SPT(dev_priv)			(INTEL_PCH_TYPE(dev_priv) == PCH_SPT)
#define HAS_PCH_LPT_H(dev_priv)			(INTEL_PCH_TYPE(dev_priv) == PCH_LPT_H)
#define HAS_PCH_LPT_LP(dev_priv)		(INTEL_PCH_TYPE(dev_priv) == PCH_LPT_LP)
#define HAS_PCH_LPT(dev_priv)			(INTEL_PCH_TYPE(dev_priv) == PCH_LPT_H || \
						 INTEL_PCH_TYPE(dev_priv) == PCH_LPT_LP)
#define HAS_PCH_CPT(dev_priv)			(INTEL_PCH_TYPE(dev_priv) == PCH_CPT)
#define HAS_PCH_IBX(dev_priv)			(INTEL_PCH_TYPE(dev_priv) == PCH_IBX)
#define HAS_PCH_NOP(dev_priv)			(INTEL_PCH_TYPE(dev_priv) == PCH_NOP)
#define HAS_PCH_SPLIT(dev_priv)			(INTEL_PCH_TYPE(dev_priv) != PCH_NONE)

void intel_detect_pch(struct drm_i915_private *dev_priv);

#endif /* __INTEL_PCH__ */
