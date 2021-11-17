// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2021 Intel Corporation */
#include <linux/types.h>
#include "adf_accel_devices.h"
#include "adf_gen2_pfvf.h"

 /* VF2PF interrupts */
#define ADF_GEN2_ERR_REG_VF2PF(vf_src)	(((vf_src) & 0x01FFFE00) >> 9)
#define ADF_GEN2_ERR_MSK_VF2PF(vf_mask)	(((vf_mask) & 0xFFFF) << 9)

#define ADF_GEN2_PF_PF2VF_OFFSET(i)	(0x3A000 + 0x280 + ((i) * 0x04))
#define ADF_GEN2_VF_PF2VF_OFFSET	0x200

u32 adf_gen2_pf_get_pf2vf_offset(u32 i)
{
	return ADF_GEN2_PF_PF2VF_OFFSET(i);
}
EXPORT_SYMBOL_GPL(adf_gen2_pf_get_pf2vf_offset);

u32 adf_gen2_vf_get_pf2vf_offset(u32 i)
{
	return ADF_GEN2_VF_PF2VF_OFFSET;
}
EXPORT_SYMBOL_GPL(adf_gen2_vf_get_pf2vf_offset);

u32 adf_gen2_get_vf2pf_sources(void __iomem *pmisc_addr)
{
	u32 errsou3, errmsk3, vf_int_mask;

	/* Get the interrupt sources triggered by VFs */
	errsou3 = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRSOU3);
	vf_int_mask = ADF_GEN2_ERR_REG_VF2PF(errsou3);

	/* To avoid adding duplicate entries to work queue, clear
	 * vf_int_mask_sets bits that are already masked in ERRMSK register.
	 */
	errmsk3 = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRMSK3);
	vf_int_mask &= ~ADF_GEN2_ERR_REG_VF2PF(errmsk3);

	return vf_int_mask;
}
EXPORT_SYMBOL_GPL(adf_gen2_get_vf2pf_sources);

void adf_gen2_enable_vf2pf_interrupts(void __iomem *pmisc_addr, u32 vf_mask)
{
	/* Enable VF2PF Messaging Ints - VFs 0 through 15 per vf_mask[15:0] */
	if (vf_mask & 0xFFFF) {
		u32 val = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRMSK3)
			  & ~ADF_GEN2_ERR_MSK_VF2PF(vf_mask);
		ADF_CSR_WR(pmisc_addr, ADF_GEN2_ERRMSK3, val);
	}
}
EXPORT_SYMBOL_GPL(adf_gen2_enable_vf2pf_interrupts);

void adf_gen2_disable_vf2pf_interrupts(void __iomem *pmisc_addr, u32 vf_mask)
{
	/* Disable VF2PF interrupts for VFs 0 through 15 per vf_mask[15:0] */
	if (vf_mask & 0xFFFF) {
		u32 val = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRMSK3)
			  | ADF_GEN2_ERR_MSK_VF2PF(vf_mask);
		ADF_CSR_WR(pmisc_addr, ADF_GEN2_ERRMSK3, val);
	}
}
EXPORT_SYMBOL_GPL(adf_gen2_disable_vf2pf_interrupts);
