// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2025 Intel Corporation */
#include <linux/bitfield.h>
#include <linux/types.h>

#include "adf_common_drv.h"
#include "adf_gen6_ras.h"
#include "adf_sysfs_ras_counters.h"

static void enable_errsou_reporting(void __iomem *csr)
{
	/* Enable correctable error reporting in ERRSOU0 */
	ADF_CSR_WR(csr, ADF_GEN6_ERRMSK0, 0);

	/* Enable uncorrectable error reporting in ERRSOU1 */
	ADF_CSR_WR(csr, ADF_GEN6_ERRMSK1, 0);

	/*
	 * Enable uncorrectable error reporting in ERRSOU2
	 * but disable PM interrupt by default
	 */
	ADF_CSR_WR(csr, ADF_GEN6_ERRMSK2, ADF_GEN6_ERRSOU2_PM_INT_BIT);

	/* Enable uncorrectable error reporting in ERRSOU3 */
	ADF_CSR_WR(csr, ADF_GEN6_ERRMSK3, 0);
}

static void enable_ae_error_reporting(struct adf_accel_dev *accel_dev, void __iomem *csr)
{
	u32 ae_mask = GET_HW_DATA(accel_dev)->ae_mask;

	/* Enable acceleration engine correctable error reporting */
	ADF_CSR_WR(csr, ADF_GEN6_HIAECORERRLOGENABLE_CPP0, ae_mask);

	/* Enable acceleration engine uncorrectable error reporting */
	ADF_CSR_WR(csr, ADF_GEN6_HIAEUNCERRLOGENABLE_CPP0, ae_mask);
}

static void enable_cpp_error_reporting(struct adf_accel_dev *accel_dev, void __iomem *csr)
{
	/* Enable HI CPP agents command parity error reporting */
	ADF_CSR_WR(csr, ADF_GEN6_HICPPAGENTCMDPARERRLOGENABLE,
		   ADF_6XXX_HICPPAGENTCMDPARERRLOG_MASK);

	ADF_CSR_WR(csr, ADF_GEN6_CPP_CFC_ERR_CTRL, ADF_GEN6_CPP_CFC_ERR_CTRL_MASK);
}

static void enable_ti_ri_error_reporting(void __iomem *csr)
{
	u32 reg, mask;

	/* Enable RI memory error reporting */
	mask = ADF_GEN6_RIMEM_PARERR_FATAL_MASK | ADF_GEN6_RIMEM_PARERR_CERR_MASK;
	ADF_CSR_WR(csr, ADF_GEN6_RI_MEM_PAR_ERR_EN0, mask);

	/* Enable IOSF primary command parity error reporting */
	ADF_CSR_WR(csr, ADF_GEN6_RIMISCCTL, ADF_GEN6_RIMISCSTS_BIT);

	/* Enable TI internal memory parity error reporting */
	reg = ADF_CSR_RD(csr, ADF_GEN6_TI_CI_PAR_ERR_MASK);
	reg &= ~ADF_GEN6_TI_CI_PAR_STS_MASK;
	ADF_CSR_WR(csr, ADF_GEN6_TI_CI_PAR_ERR_MASK, reg);

	reg = ADF_CSR_RD(csr, ADF_GEN6_TI_PULL0FUB_PAR_ERR_MASK);
	reg &= ~ADF_GEN6_TI_PULL0FUB_PAR_STS_MASK;
	ADF_CSR_WR(csr, ADF_GEN6_TI_PULL0FUB_PAR_ERR_MASK, reg);

	reg = ADF_CSR_RD(csr, ADF_GEN6_TI_PUSHFUB_PAR_ERR_MASK);
	reg &= ~ADF_GEN6_TI_PUSHFUB_PAR_STS_MASK;
	ADF_CSR_WR(csr, ADF_GEN6_TI_PUSHFUB_PAR_ERR_MASK, reg);

	reg = ADF_CSR_RD(csr, ADF_GEN6_TI_CD_PAR_ERR_MASK);
	reg &= ~ADF_GEN6_TI_CD_PAR_STS_MASK;
	ADF_CSR_WR(csr, ADF_GEN6_TI_CD_PAR_ERR_MASK, reg);

	reg = ADF_CSR_RD(csr, ADF_GEN6_TI_TRNSB_PAR_ERR_MASK);
	reg &= ~ADF_GEN6_TI_TRNSB_PAR_STS_MASK;
	ADF_CSR_WR(csr, ADF_GEN6_TI_TRNSB_PAR_ERR_MASK, reg);

	/* Enable error handling in RI, TI CPP interface control registers */
	ADF_CSR_WR(csr, ADF_GEN6_RICPPINTCTL, ADF_GEN6_RICPPINTCTL_MASK);
	ADF_CSR_WR(csr, ADF_GEN6_TICPPINTCTL, ADF_GEN6_TICPPINTCTL_MASK);

	/*
	 * Enable error detection and reporting in TIMISCSTS
	 * with bits 1, 2 and 30 value preserved
	 */
	reg = ADF_CSR_RD(csr, ADF_GEN6_TIMISCCTL);
	reg &= ADF_GEN6_TIMSCCTL_RELAY_MASK;
	reg |= ADF_GEN6_TIMISCCTL_BIT;
	ADF_CSR_WR(csr, ADF_GEN6_TIMISCCTL, reg);
}

static void enable_ssm_error_reporting(struct adf_accel_dev *accel_dev,
				       void __iomem *csr)
{
	/* Enable SSM interrupts */
	ADF_CSR_WR(csr, ADF_GEN6_INTMASKSSM, 0);
}

static void adf_gen6_enable_ras(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);

	enable_errsou_reporting(csr);
	enable_ae_error_reporting(accel_dev, csr);
	enable_cpp_error_reporting(accel_dev, csr);
	enable_ti_ri_error_reporting(csr);
	enable_ssm_error_reporting(accel_dev, csr);
}

static void disable_errsou_reporting(void __iomem *csr)
{
	u32 val;

	/* Disable correctable error reporting in ERRSOU0 */
	ADF_CSR_WR(csr, ADF_GEN6_ERRMSK0, ADF_GEN6_ERRSOU0_MASK);

	/* Disable uncorrectable error reporting in ERRSOU1 */
	ADF_CSR_WR(csr, ADF_GEN6_ERRMSK1, ADF_GEN6_ERRMSK1_MASK);

	/* Disable uncorrectable error reporting in ERRSOU2 */
	val = ADF_CSR_RD(csr, ADF_GEN6_ERRMSK2);
	val |= ADF_GEN6_ERRSOU2_DIS_MASK;
	ADF_CSR_WR(csr, ADF_GEN6_ERRMSK2, val);

	/* Disable uncorrectable error reporting in ERRSOU3 */
	ADF_CSR_WR(csr, ADF_GEN6_ERRMSK3, ADF_GEN6_ERRSOU3_DIS_MASK);
}

static void disable_ae_error_reporting(void __iomem *csr)
{
	/* Disable acceleration engine correctable error reporting */
	ADF_CSR_WR(csr, ADF_GEN6_HIAECORERRLOGENABLE_CPP0, 0);

	/* Disable acceleration engine uncorrectable error reporting */
	ADF_CSR_WR(csr, ADF_GEN6_HIAEUNCERRLOGENABLE_CPP0, 0);
}

static void disable_cpp_error_reporting(void __iomem *csr)
{
	/* Disable HI CPP agents command parity error reporting */
	ADF_CSR_WR(csr, ADF_GEN6_HICPPAGENTCMDPARERRLOGENABLE, 0);

	ADF_CSR_WR(csr, ADF_GEN6_CPP_CFC_ERR_CTRL, ADF_GEN6_CPP_CFC_ERR_CTRL_DIS_MASK);
}

static void disable_ti_ri_error_reporting(void __iomem *csr)
{
	u32 reg;

	/* Disable RI memory error reporting */
	ADF_CSR_WR(csr, ADF_GEN6_RI_MEM_PAR_ERR_EN0, 0);

	/* Disable IOSF primary command parity error reporting */
	reg = ADF_CSR_RD(csr, ADF_GEN6_RIMISCCTL);
	reg &= ~ADF_GEN6_RIMISCSTS_BIT;
	ADF_CSR_WR(csr, ADF_GEN6_RIMISCCTL, reg);

	/* Disable TI internal memory parity error reporting */
	ADF_CSR_WR(csr, ADF_GEN6_TI_CI_PAR_ERR_MASK, ADF_GEN6_TI_CI_PAR_STS_MASK);
	ADF_CSR_WR(csr, ADF_GEN6_TI_PULL0FUB_PAR_ERR_MASK, ADF_GEN6_TI_PULL0FUB_PAR_STS_MASK);
	ADF_CSR_WR(csr, ADF_GEN6_TI_PUSHFUB_PAR_ERR_MASK, ADF_GEN6_TI_PUSHFUB_PAR_STS_MASK);
	ADF_CSR_WR(csr, ADF_GEN6_TI_CD_PAR_ERR_MASK, ADF_GEN6_TI_CD_PAR_STS_MASK);
	ADF_CSR_WR(csr, ADF_GEN6_TI_TRNSB_PAR_ERR_MASK, ADF_GEN6_TI_TRNSB_PAR_STS_MASK);

	/* Disable error handling in RI, TI CPP interface control registers */
	reg = ADF_CSR_RD(csr, ADF_GEN6_RICPPINTCTL);
	reg &= ~ADF_GEN6_RICPPINTCTL_MASK;
	ADF_CSR_WR(csr, ADF_GEN6_RICPPINTCTL, reg);

	reg = ADF_CSR_RD(csr, ADF_GEN6_TICPPINTCTL);
	reg &= ~ADF_GEN6_TICPPINTCTL_MASK;
	ADF_CSR_WR(csr, ADF_GEN6_TICPPINTCTL, reg);

	/*
	 * Disable error detection and reporting in TIMISCSTS
	 * with bits 1, 2 and 30 value preserved
	 */
	reg = ADF_CSR_RD(csr, ADF_GEN6_TIMISCCTL);
	reg &= ADF_GEN6_TIMSCCTL_RELAY_MASK;
	ADF_CSR_WR(csr, ADF_GEN6_TIMISCCTL, reg);
}

static void disable_ssm_error_reporting(void __iomem *csr)
{
	/* Disable SSM interrupts */
	ADF_CSR_WR(csr, ADF_GEN6_INTMASKSSM, ADF_GEN6_INTMASKSSM_MASK);
}

static void adf_gen6_disable_ras(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);

	disable_errsou_reporting(csr);
	disable_ae_error_reporting(csr);
	disable_cpp_error_reporting(csr);
	disable_ti_ri_error_reporting(csr);
	disable_ssm_error_reporting(csr);
}

static void adf_gen6_process_errsou0(struct adf_accel_dev *accel_dev, void __iomem *csr)
{
	u32 ae, errsou;

	ae = ADF_CSR_RD(csr, ADF_GEN6_HIAECORERRLOG_CPP0);
	ae &= GET_HW_DATA(accel_dev)->ae_mask;

	dev_warn(&GET_DEV(accel_dev), "Correctable error detected: %#x\n", ae);

	ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_CORR);

	/* Clear interrupt from ERRSOU0 */
	ADF_CSR_WR(csr, ADF_GEN6_HIAECORERRLOG_CPP0, ae);

	errsou = ADF_CSR_RD(csr, ADF_GEN6_ERRSOU0);
	if (errsou & ADF_GEN6_ERRSOU0_MASK)
		dev_warn(&GET_DEV(accel_dev), "errsou0 still set: %#x\n", errsou);
}

static void adf_handle_cpp_ae_unc(struct adf_accel_dev *accel_dev, void __iomem *csr,
				  u32 errsou)
{
	u32 ae;

	if (!(errsou & ADF_GEN6_ERRSOU1_CPP0_MEUNC_BIT))
		return;

	ae = ADF_CSR_RD(csr, ADF_GEN6_HIAEUNCERRLOG_CPP0);
	ae &= GET_HW_DATA(accel_dev)->ae_mask;
	if (ae) {
		dev_err(&GET_DEV(accel_dev), "Uncorrectable error detected: %#x\n", ae);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_UNCORR);
		ADF_CSR_WR(csr, ADF_GEN6_HIAEUNCERRLOG_CPP0, ae);
	}
}

static void adf_handle_cpp_cmd_par_err(struct adf_accel_dev *accel_dev, void __iomem *csr,
				       u32 errsou)
{
	u32 cmd_par_err;

	if (!(errsou & ADF_GEN6_ERRSOU1_CPP_CMDPARERR_BIT))
		return;

	cmd_par_err = ADF_CSR_RD(csr, ADF_GEN6_HICPPAGENTCMDPARERRLOG);
	cmd_par_err &= ADF_6XXX_HICPPAGENTCMDPARERRLOG_MASK;
	if (cmd_par_err) {
		dev_err(&GET_DEV(accel_dev), "HI CPP agent command parity error: %#x\n",
			cmd_par_err);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
		ADF_CSR_WR(csr, ADF_GEN6_HICPPAGENTCMDPARERRLOG, cmd_par_err);
	}
}

static void adf_handle_ri_mem_par_err(struct adf_accel_dev *accel_dev, void __iomem *csr,
				      u32 errsou)
{
	u32 rimem_parerr_sts;

	if (!(errsou & ADF_GEN6_ERRSOU1_RIMEM_PARERR_STS_BIT))
		return;

	rimem_parerr_sts = ADF_CSR_RD(csr, ADF_GEN6_RIMEM_PARERR_STS);
	rimem_parerr_sts &= ADF_GEN6_RIMEM_PARERR_CERR_MASK |
			    ADF_GEN6_RIMEM_PARERR_FATAL_MASK;
	if (rimem_parerr_sts & ADF_GEN6_RIMEM_PARERR_CERR_MASK) {
		dev_err(&GET_DEV(accel_dev), "RI memory parity correctable error: %#x\n",
			rimem_parerr_sts);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_CORR);
	}

	if (rimem_parerr_sts & ADF_GEN6_RIMEM_PARERR_FATAL_MASK) {
		dev_err(&GET_DEV(accel_dev), "RI memory parity fatal error: %#x\n",
			rimem_parerr_sts);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
	}

	ADF_CSR_WR(csr, ADF_GEN6_RIMEM_PARERR_STS, rimem_parerr_sts);
}

static void adf_handle_ti_ci_par_sts(struct adf_accel_dev *accel_dev, void __iomem *csr)
{
	u32 ti_ci_par_sts;

	ti_ci_par_sts = ADF_CSR_RD(csr, ADF_GEN6_TI_CI_PAR_STS);
	ti_ci_par_sts &= ADF_GEN6_TI_CI_PAR_STS_MASK;
	if (ti_ci_par_sts) {
		dev_err(&GET_DEV(accel_dev), "TI memory parity error: %#x\n", ti_ci_par_sts);
		ADF_CSR_WR(csr, ADF_GEN6_TI_CI_PAR_STS, ti_ci_par_sts);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_UNCORR);
	}
}

static void adf_handle_ti_pullfub_par_sts(struct adf_accel_dev *accel_dev, void __iomem *csr)
{
	u32 ti_pullfub_par_sts;

	ti_pullfub_par_sts = ADF_CSR_RD(csr, ADF_GEN6_TI_PULL0FUB_PAR_STS);
	ti_pullfub_par_sts &= ADF_GEN6_TI_PULL0FUB_PAR_STS_MASK;
	if (ti_pullfub_par_sts) {
		dev_err(&GET_DEV(accel_dev), "TI pull parity error: %#x\n", ti_pullfub_par_sts);
		ADF_CSR_WR(csr, ADF_GEN6_TI_PULL0FUB_PAR_STS, ti_pullfub_par_sts);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_UNCORR);
	}
}

static void adf_handle_ti_pushfub_par_sts(struct adf_accel_dev *accel_dev, void __iomem *csr)
{
	u32 ti_pushfub_par_sts;

	ti_pushfub_par_sts = ADF_CSR_RD(csr, ADF_GEN6_TI_PUSHFUB_PAR_STS);
	ti_pushfub_par_sts &= ADF_GEN6_TI_PUSHFUB_PAR_STS_MASK;
	if (ti_pushfub_par_sts) {
		dev_err(&GET_DEV(accel_dev), "TI push parity error: %#x\n", ti_pushfub_par_sts);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_UNCORR);
		ADF_CSR_WR(csr, ADF_GEN6_TI_PUSHFUB_PAR_STS, ti_pushfub_par_sts);
	}
}

static void adf_handle_ti_cd_par_sts(struct adf_accel_dev *accel_dev, void __iomem *csr)
{
	u32 ti_cd_par_sts;

	ti_cd_par_sts = ADF_CSR_RD(csr, ADF_GEN6_TI_CD_PAR_STS);
	ti_cd_par_sts &= ADF_GEN6_TI_CD_PAR_STS_MASK;
	if (ti_cd_par_sts) {
		dev_err(&GET_DEV(accel_dev), "TI CD parity error: %#x\n", ti_cd_par_sts);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_UNCORR);
		ADF_CSR_WR(csr, ADF_GEN6_TI_CD_PAR_STS, ti_cd_par_sts);
	}
}

static void adf_handle_ti_trnsb_par_sts(struct adf_accel_dev *accel_dev, void __iomem *csr)
{
	u32 ti_trnsb_par_sts;

	ti_trnsb_par_sts = ADF_CSR_RD(csr, ADF_GEN6_TI_TRNSB_PAR_STS);
	ti_trnsb_par_sts &= ADF_GEN6_TI_TRNSB_PAR_STS_MASK;
	if (ti_trnsb_par_sts) {
		dev_err(&GET_DEV(accel_dev), "TI TRNSB parity error: %#x\n", ti_trnsb_par_sts);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_UNCORR);
		ADF_CSR_WR(csr, ADF_GEN6_TI_TRNSB_PAR_STS, ti_trnsb_par_sts);
	}
}

static void adf_handle_iosfp_cmd_parerr(struct adf_accel_dev *accel_dev, void __iomem *csr)
{
	u32 rimiscsts;

	rimiscsts = ADF_CSR_RD(csr, ADF_GEN6_RIMISCSTS);
	rimiscsts &= ADF_GEN6_RIMISCSTS_BIT;
	if (rimiscsts) {
		dev_err(&GET_DEV(accel_dev), "Command parity error detected on IOSFP: %#x\n",
			rimiscsts);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
		ADF_CSR_WR(csr, ADF_GEN6_RIMISCSTS, rimiscsts);
	}
}

static void adf_handle_ti_err(struct adf_accel_dev *accel_dev, void __iomem *csr,
			      u32 errsou)
{
	if (!(errsou & ADF_GEN6_ERRSOU1_TIMEM_PARERR_STS_BIT))
		return;

	adf_handle_ti_ci_par_sts(accel_dev, csr);
	adf_handle_ti_pullfub_par_sts(accel_dev, csr);
	adf_handle_ti_pushfub_par_sts(accel_dev, csr);
	adf_handle_ti_cd_par_sts(accel_dev, csr);
	adf_handle_ti_trnsb_par_sts(accel_dev, csr);
	adf_handle_iosfp_cmd_parerr(accel_dev, csr);
}

static void adf_handle_sfi_cmd_parerr(struct adf_accel_dev *accel_dev, void __iomem *csr,
				      u32 errsou)
{
	if (!(errsou & ADF_GEN6_ERRSOU1_SFICMD_PARERR_BIT))
		return;

	dev_err(&GET_DEV(accel_dev),
		"Command parity error detected on streaming fabric interface\n");

	ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
}

static void adf_gen6_process_errsou1(struct adf_accel_dev *accel_dev, void __iomem *csr,
				     u32 errsou)
{
	adf_handle_cpp_ae_unc(accel_dev, csr, errsou);
	adf_handle_cpp_cmd_par_err(accel_dev, csr, errsou);
	adf_handle_ri_mem_par_err(accel_dev, csr, errsou);
	adf_handle_ti_err(accel_dev, csr, errsou);
	adf_handle_sfi_cmd_parerr(accel_dev, csr, errsou);

	errsou = ADF_CSR_RD(csr, ADF_GEN6_ERRSOU1);
	if (errsou & ADF_GEN6_ERRSOU1_MASK)
		dev_warn(&GET_DEV(accel_dev), "errsou1 still set: %#x\n", errsou);
}

static void adf_handle_cerrssmsh(struct adf_accel_dev *accel_dev, void __iomem *csr)
{
	u32 reg;

	reg = ADF_CSR_RD(csr, ADF_GEN6_CERRSSMSH);
	reg &= ADF_GEN6_CERRSSMSH_ERROR_BIT;
	if (reg) {
		dev_warn(&GET_DEV(accel_dev),
			 "Correctable error on ssm shared memory: %#x\n", reg);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_CORR);
		ADF_CSR_WR(csr, ADF_GEN6_CERRSSMSH, reg);
	}
}

static void adf_handle_uerrssmsh(struct adf_accel_dev *accel_dev, void __iomem *csr,
				 u32 iastatssm)
{
	u32 reg;

	if (!(iastatssm & ADF_GEN6_IAINTSTATSSM_SH_ERR_BIT))
		return;

	reg = ADF_CSR_RD(csr, ADF_GEN6_UERRSSMSH);
	reg &= ADF_GEN6_UERRSSMSH_MASK;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"Fatal error on ssm shared memory: %#x\n", reg);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
		ADF_CSR_WR(csr, ADF_GEN6_UERRSSMSH, reg);
	}
}

static void adf_handle_pperr_err(struct adf_accel_dev *accel_dev, void __iomem *csr,
				 u32 iastatssm)
{
	u32 reg;

	if (!(iastatssm & ADF_GEN6_IAINTSTATSSM_PPERR_BIT))
		return;

	reg = ADF_CSR_RD(csr, ADF_GEN6_PPERR);
	reg &= ADF_GEN6_PPERR_MASK;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"Fatal push or pull data error: %#x\n", reg);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
		ADF_CSR_WR(csr, ADF_GEN6_PPERR, reg);
	}
}

static void adf_handle_scmpar_err(struct adf_accel_dev *accel_dev, void __iomem *csr,
				  u32 iastatssm)
{
	u32 reg;

	if (!(iastatssm & ADF_GEN6_IAINTSTATSSM_SCMPAR_ERR_BIT))
		return;

	reg = ADF_CSR_RD(csr, ADF_GEN6_SSM_FERR_STATUS);
	reg &= ADF_GEN6_SCM_PAR_ERR_MASK;
	if (reg) {
		dev_err(&GET_DEV(accel_dev), "Fatal error on SCM: %#x\n", reg);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
		ADF_CSR_WR(csr, ADF_GEN6_SSM_FERR_STATUS, reg);
	}
}

static void adf_handle_cpppar_err(struct adf_accel_dev *accel_dev, void __iomem *csr,
				  u32 iastatssm)
{
	u32 reg;

	if (!(iastatssm & ADF_GEN6_IAINTSTATSSM_CPPPAR_ERR_BIT))
		return;

	reg = ADF_CSR_RD(csr, ADF_GEN6_SSM_FERR_STATUS);
	reg &= ADF_GEN6_CPP_PAR_ERR_MASK;
	if (reg) {
		dev_err(&GET_DEV(accel_dev), "Fatal error on CPP: %#x\n", reg);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
		ADF_CSR_WR(csr, ADF_GEN6_SSM_FERR_STATUS, reg);
	}
}

static void adf_handle_rfpar_err(struct adf_accel_dev *accel_dev, void __iomem *csr,
				 u32 iastatssm)
{
	u32 reg;

	if (!(iastatssm & ADF_GEN6_IAINTSTATSSM_RFPAR_ERR_BIT))
		return;

	reg = ADF_CSR_RD(csr, ADF_GEN6_SSM_FERR_STATUS);
	reg &= ADF_GEN6_RF_PAR_ERR_MASK;
	if (reg) {
		dev_err(&GET_DEV(accel_dev), "Fatal error on RF Parity: %#x\n", reg);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
		ADF_CSR_WR(csr, ADF_GEN6_SSM_FERR_STATUS, reg);
	}
}

static void adf_handle_unexp_cpl_err(struct adf_accel_dev *accel_dev, void __iomem *csr,
				     u32 iastatssm)
{
	u32 reg;

	if (!(iastatssm & ADF_GEN6_IAINTSTATSSM_UNEXP_CPL_ERR_BIT))
		return;

	reg = ADF_CSR_RD(csr, ADF_GEN6_SSM_FERR_STATUS);
	reg &= ADF_GEN6_UNEXP_CPL_ERR_MASK;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"Fatal error for AXI unexpected tag/length: %#x\n", reg);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
		ADF_CSR_WR(csr, ADF_GEN6_SSM_FERR_STATUS, reg);
	}
}

static void adf_handle_iaintstatssm(struct adf_accel_dev *accel_dev, void __iomem *csr)
{
	u32 iastatssm = ADF_CSR_RD(csr, ADF_GEN6_IAINTSTATSSM);

	iastatssm &= ADF_GEN6_IAINTSTATSSM_MASK;
	if (!iastatssm)
		return;

	adf_handle_uerrssmsh(accel_dev, csr, iastatssm);
	adf_handle_pperr_err(accel_dev, csr, iastatssm);
	adf_handle_scmpar_err(accel_dev, csr, iastatssm);
	adf_handle_cpppar_err(accel_dev, csr, iastatssm);
	adf_handle_rfpar_err(accel_dev, csr, iastatssm);
	adf_handle_unexp_cpl_err(accel_dev, csr, iastatssm);

	ADF_CSR_WR(csr, ADF_GEN6_IAINTSTATSSM, iastatssm);
}

static void adf_handle_ssm(struct adf_accel_dev *accel_dev, void __iomem *csr, u32 errsou)
{
	if (!(errsou & ADF_GEN6_ERRSOU2_SSM_ERR_BIT))
		return;

	adf_handle_cerrssmsh(accel_dev, csr);
	adf_handle_iaintstatssm(accel_dev, csr);
}

static void adf_handle_cpp_cfc_err(struct adf_accel_dev *accel_dev, void __iomem *csr,
				   u32 errsou)
{
	u32 reg;

	if (!(errsou & ADF_GEN6_ERRSOU2_CPP_CFC_ERR_STATUS_BIT))
		return;

	reg = ADF_CSR_RD(csr, ADF_GEN6_CPP_CFC_ERR_STATUS);
	if (reg & ADF_GEN6_CPP_CFC_ERR_STATUS_DATAPAR_BIT) {
		dev_err(&GET_DEV(accel_dev), "CPP_CFC_ERR: data parity: %#x", reg);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_UNCORR);
	}

	if (reg & ADF_GEN6_CPP_CFC_ERR_STATUS_CMDPAR_BIT) {
		dev_err(&GET_DEV(accel_dev), "CPP_CFC_ERR: command parity: %#x", reg);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
	}

	if (reg & ADF_GEN6_CPP_CFC_FATAL_ERR_BIT) {
		dev_err(&GET_DEV(accel_dev), "CPP_CFC_ERR: errors: %#x", reg);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
	}

	ADF_CSR_WR(csr, ADF_GEN6_CPP_CFC_ERR_STATUS_CLR,
		   ADF_GEN6_CPP_CFC_ERR_STATUS_CLR_MASK);
}

static void adf_gen6_process_errsou2(struct adf_accel_dev *accel_dev, void __iomem *csr,
				     u32 errsou)
{
	adf_handle_ssm(accel_dev, csr, errsou);
	adf_handle_cpp_cfc_err(accel_dev, csr, errsou);

	errsou = ADF_CSR_RD(csr, ADF_GEN6_ERRSOU2);
	if (errsou & ADF_GEN6_ERRSOU2_MASK)
		dev_warn(&GET_DEV(accel_dev), "errsou2 still set: %#x\n", errsou);
}

static void adf_handle_timiscsts(struct adf_accel_dev *accel_dev, void __iomem *csr,
				 u32 errsou)
{
	u32 timiscsts;

	if (!(errsou & ADF_GEN6_ERRSOU3_TIMISCSTS_BIT))
		return;

	timiscsts = ADF_CSR_RD(csr, ADF_GEN6_TIMISCSTS);
	if (timiscsts) {
		dev_err(&GET_DEV(accel_dev), "Fatal error in transmit interface: %#x\n",
			timiscsts);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
	}
}

static void adf_handle_ricppintsts(struct adf_accel_dev *accel_dev, void __iomem *csr,
				   u32 errsou)
{
	u32 ricppintsts;

	if (!(errsou & ADF_GEN6_ERRSOU3_RICPPINTSTS_MASK))
		return;

	ricppintsts = ADF_CSR_RD(csr, ADF_GEN6_RICPPINTSTS);
	ricppintsts &= ADF_GEN6_RICPPINTSTS_MASK;
	if (ricppintsts) {
		dev_err(&GET_DEV(accel_dev), "RI push pull error: %#x\n", ricppintsts);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_UNCORR);
		ADF_CSR_WR(csr, ADF_GEN6_RICPPINTSTS, ricppintsts);
	}
}

static void adf_handle_ticppintsts(struct adf_accel_dev *accel_dev, void __iomem *csr,
				   u32 errsou)
{
	u32 ticppintsts;

	if (!(errsou & ADF_GEN6_ERRSOU3_TICPPINTSTS_MASK))
		return;

	ticppintsts = ADF_CSR_RD(csr, ADF_GEN6_TICPPINTSTS);
	ticppintsts &= ADF_GEN6_TICPPINTSTS_MASK;
	if (ticppintsts) {
		dev_err(&GET_DEV(accel_dev), "TI push pull error: %#x\n", ticppintsts);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
		ADF_CSR_WR(csr, ADF_GEN6_TICPPINTSTS, ticppintsts);
	}
}

static void adf_handle_atufaultstatus(struct adf_accel_dev *accel_dev, void __iomem *csr,
				      u32 errsou)
{
	u32 max_rp_num = GET_HW_DATA(accel_dev)->num_banks;
	u32 atufaultstatus;
	u32 i;

	if (!(errsou & ADF_GEN6_ERRSOU3_ATUFAULTSTATUS_BIT))
		return;

	for (i = 0; i < max_rp_num; i++) {
		atufaultstatus = ADF_CSR_RD(csr, ADF_GEN6_ATUFAULTSTATUS(i));

		atufaultstatus &= ADF_GEN6_ATUFAULTSTATUS_BIT;
		if (atufaultstatus) {
			dev_err(&GET_DEV(accel_dev), "Ring pair (%u) ATU detected fault: %#x\n", i,
				atufaultstatus);
			ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_UNCORR);
			ADF_CSR_WR(csr, ADF_GEN6_ATUFAULTSTATUS(i), atufaultstatus);
		}
	}
}

static void adf_handle_rlterror(struct adf_accel_dev *accel_dev, void __iomem *csr,
				u32 errsou)
{
	u32 rlterror;

	if (!(errsou & ADF_GEN6_ERRSOU3_RLTERROR_BIT))
		return;

	rlterror = ADF_CSR_RD(csr, ADF_GEN6_RLT_ERRLOG);
	rlterror &= ADF_GEN6_RLT_ERRLOG_MASK;
	if (rlterror) {
		dev_err(&GET_DEV(accel_dev), "Error in rate limiting block: %#x\n", rlterror);
		ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_UNCORR);
		ADF_CSR_WR(csr, ADF_GEN6_RLT_ERRLOG, rlterror);
	}
}

static void adf_handle_vflr(struct adf_accel_dev *accel_dev, void __iomem *csr, u32 errsou)
{
	if (!(errsou & ADF_GEN6_ERRSOU3_VFLRNOTIFY_BIT))
		return;

	dev_err(&GET_DEV(accel_dev), "Uncorrectable error in VF\n");
	ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_UNCORR);
}

static void adf_handle_tc_vc_map_error(struct adf_accel_dev *accel_dev, void __iomem *csr,
				       u32 errsou)
{
	if (!(errsou & ADF_GEN6_ERRSOU3_TC_VC_MAP_ERROR_BIT))
		return;

	dev_err(&GET_DEV(accel_dev), "Violation of PCIe TC VC mapping\n");
	ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
}

static void adf_handle_pcie_devhalt(struct adf_accel_dev *accel_dev, void __iomem *csr,
				    u32 errsou)
{
	if (!(errsou & ADF_GEN6_ERRSOU3_PCIE_DEVHALT_BIT))
		return;

	dev_err(&GET_DEV(accel_dev),
		"DEVHALT due to an error in an incoming transaction\n");
	ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
}

static void adf_handle_pg_req_devhalt(struct adf_accel_dev *accel_dev, void __iomem *csr,
				      u32 errsou)
{
	if (!(errsou & ADF_GEN6_ERRSOU3_PG_REQ_DEVHALT_BIT))
		return;

	dev_err(&GET_DEV(accel_dev),
		"Error due to response failure in response to a page request\n");
	ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
}

static void adf_handle_xlt_cpl_devhalt(struct adf_accel_dev *accel_dev, void __iomem *csr,
				       u32 errsou)
{
	if (!(errsou & ADF_GEN6_ERRSOU3_XLT_CPL_DEVHALT_BIT))
		return;

	dev_err(&GET_DEV(accel_dev), "Error status for a address translation request\n");
	ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
}

static void adf_handle_ti_int_err_devhalt(struct adf_accel_dev *accel_dev, void __iomem *csr,
					  u32 errsou)
{
	if (!(errsou & ADF_GEN6_ERRSOU3_TI_INT_ERR_DEVHALT_BIT))
		return;

	dev_err(&GET_DEV(accel_dev), "DEVHALT due to a TI internal memory error\n");
	ADF_RAS_ERR_CTR_INC(accel_dev->ras_errors, ADF_RAS_FATAL);
}

static void adf_gen6_process_errsou3(struct adf_accel_dev *accel_dev, void __iomem *csr,
				     u32 errsou)
{
	adf_handle_timiscsts(accel_dev, csr, errsou);
	adf_handle_ricppintsts(accel_dev, csr, errsou);
	adf_handle_ticppintsts(accel_dev, csr, errsou);
	adf_handle_atufaultstatus(accel_dev, csr, errsou);
	adf_handle_rlterror(accel_dev, csr, errsou);
	adf_handle_vflr(accel_dev, csr, errsou);
	adf_handle_tc_vc_map_error(accel_dev, csr, errsou);
	adf_handle_pcie_devhalt(accel_dev, csr, errsou);
	adf_handle_pg_req_devhalt(accel_dev, csr, errsou);
	adf_handle_xlt_cpl_devhalt(accel_dev, csr, errsou);
	adf_handle_ti_int_err_devhalt(accel_dev, csr, errsou);

	errsou = ADF_CSR_RD(csr, ADF_GEN6_ERRSOU3);
	if (errsou & ADF_GEN6_ERRSOU3_MASK)
		dev_warn(&GET_DEV(accel_dev), "errsou3 still set: %#x\n", errsou);
}

static void adf_gen6_is_reset_required(struct adf_accel_dev *accel_dev, void __iomem *csr,
				       bool *reset_required)
{
	u8 reset, dev_state;
	u32 gensts;

	gensts = ADF_CSR_RD(csr, ADF_GEN6_GENSTS);
	dev_state = FIELD_GET(ADF_GEN6_GENSTS_DEVICE_STATE_MASK, gensts);
	reset = FIELD_GET(ADF_GEN6_GENSTS_RESET_TYPE_MASK, gensts);
	if (dev_state == ADF_GEN6_GENSTS_DEVHALT && reset == ADF_GEN6_GENSTS_PFLR) {
		*reset_required = true;
		return;
	}

	if (reset == ADF_GEN6_GENSTS_COLD_RESET)
		dev_err(&GET_DEV(accel_dev), "Fatal error, cold reset required\n");

	*reset_required = false;
}

static bool adf_gen6_handle_interrupt(struct adf_accel_dev *accel_dev, bool *reset_required)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);
	bool handled = false;
	u32 errsou;

	errsou = ADF_CSR_RD(csr, ADF_GEN6_ERRSOU0);
	if (errsou & ADF_GEN6_ERRSOU0_MASK) {
		adf_gen6_process_errsou0(accel_dev, csr);
		handled = true;
	}

	errsou = ADF_CSR_RD(csr, ADF_GEN6_ERRSOU1);
	if (errsou & ADF_GEN6_ERRSOU1_MASK) {
		adf_gen6_process_errsou1(accel_dev, csr, errsou);
		handled = true;
	}

	errsou = ADF_CSR_RD(csr, ADF_GEN6_ERRSOU2);
	if (errsou & ADF_GEN6_ERRSOU2_MASK) {
		adf_gen6_process_errsou2(accel_dev, csr, errsou);
		handled = true;
	}

	errsou = ADF_CSR_RD(csr, ADF_GEN6_ERRSOU3);
	if (errsou & ADF_GEN6_ERRSOU3_MASK) {
		adf_gen6_process_errsou3(accel_dev, csr, errsou);
		handled = true;
	}

	adf_gen6_is_reset_required(accel_dev, csr, reset_required);

	return handled;
}

void adf_gen6_init_ras_ops(struct adf_ras_ops *ras_ops)
{
	ras_ops->enable_ras_errors = adf_gen6_enable_ras;
	ras_ops->disable_ras_errors = adf_gen6_disable_ras;
	ras_ops->handle_interrupt = adf_gen6_handle_interrupt;
}
EXPORT_SYMBOL_GPL(adf_gen6_init_ras_ops);
