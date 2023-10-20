// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */
#include "adf_common_drv.h"
#include "adf_gen4_hw_data.h"
#include "adf_gen4_ras.h"

static void enable_errsou_reporting(void __iomem *csr)
{
	/* Enable correctable error reporting in ERRSOU0 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK0, 0);

	/* Enable uncorrectable error reporting in ERRSOU1 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK1, 0);
}

static void disable_errsou_reporting(void __iomem *csr)
{
	/* Disable correctable error reporting in ERRSOU0 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK0, ADF_GEN4_ERRSOU0_BIT);

	/* Disable uncorrectable error reporting in ERRSOU1 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK1, ADF_GEN4_ERRSOU1_BITMASK);
}

static void enable_ae_error_reporting(struct adf_accel_dev *accel_dev,
				      void __iomem *csr)
{
	u32 ae_mask = GET_HW_DATA(accel_dev)->ae_mask;

	/* Enable Acceleration Engine correctable error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HIAECORERRLOGENABLE_CPP0, ae_mask);

	/* Enable Acceleration Engine uncorrectable error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HIAEUNCERRLOGENABLE_CPP0, ae_mask);
}

static void disable_ae_error_reporting(void __iomem *csr)
{
	/* Disable Acceleration Engine correctable error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HIAECORERRLOGENABLE_CPP0, 0);

	/* Disable Acceleration Engine uncorrectable error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HIAEUNCERRLOGENABLE_CPP0, 0);
}

static void enable_cpp_error_reporting(struct adf_accel_dev *accel_dev,
				       void __iomem *csr)
{
	struct adf_dev_err_mask *err_mask = GET_ERR_MASK(accel_dev);

	/* Enable HI CPP Agents Command Parity Error Reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HICPPAGENTCMDPARERRLOGENABLE,
		   err_mask->cppagentcmdpar_mask);
}

static void disable_cpp_error_reporting(void __iomem *csr)
{
	/* Disable HI CPP Agents Command Parity Error Reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HICPPAGENTCMDPARERRLOGENABLE, 0);
}

static void enable_ti_ri_error_reporting(void __iomem *csr)
{
	/* Enable RI Memory error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_RI_MEM_PAR_ERR_EN0,
		   ADF_GEN4_RIMEM_PARERR_STS_FATAL_BITMASK |
		   ADF_GEN4_RIMEM_PARERR_STS_UNCERR_BITMASK);

	/* Enable IOSF Primary Command Parity error Reporting */
	ADF_CSR_WR(csr, ADF_GEN4_RIMISCCTL, ADF_GEN4_RIMISCSTS_BIT);

	/* Enable TI Internal Memory Parity Error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_TI_CI_PAR_ERR_MASK, 0);
	ADF_CSR_WR(csr, ADF_GEN4_TI_PULL0FUB_PAR_ERR_MASK, 0);
	ADF_CSR_WR(csr, ADF_GEN4_TI_PUSHFUB_PAR_ERR_MASK, 0);
	ADF_CSR_WR(csr, ADF_GEN4_TI_CD_PAR_ERR_MASK, 0);
	ADF_CSR_WR(csr, ADF_GEN4_TI_TRNSB_PAR_ERR_MASK, 0);
}

static void disable_ti_ri_error_reporting(void __iomem *csr)
{
	/* Disable RI Memory error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_RI_MEM_PAR_ERR_EN0, 0);

	/* Disable IOSF Primary Command Parity error Reporting */
	ADF_CSR_WR(csr, ADF_GEN4_RIMISCCTL, 0);

	/* Disable TI Internal Memory Parity Error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_TI_CI_PAR_ERR_MASK,
		   ADF_GEN4_TI_CI_PAR_STS_BITMASK);
	ADF_CSR_WR(csr, ADF_GEN4_TI_PULL0FUB_PAR_ERR_MASK,
		   ADF_GEN4_TI_PULL0FUB_PAR_STS_BITMASK);
	ADF_CSR_WR(csr, ADF_GEN4_TI_PUSHFUB_PAR_ERR_MASK,
		   ADF_GEN4_TI_PUSHFUB_PAR_STS_BITMASK);
	ADF_CSR_WR(csr, ADF_GEN4_TI_CD_PAR_ERR_MASK,
		   ADF_GEN4_TI_CD_PAR_STS_BITMASK);
	ADF_CSR_WR(csr, ADF_GEN4_TI_TRNSB_PAR_ERR_MASK,
		   ADF_GEN4_TI_TRNSB_PAR_STS_BITMASK);
}

static void adf_gen4_enable_ras(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);

	enable_errsou_reporting(csr);
	enable_ae_error_reporting(accel_dev, csr);
	enable_cpp_error_reporting(accel_dev, csr);
	enable_ti_ri_error_reporting(csr);
}

static void adf_gen4_disable_ras(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);

	disable_errsou_reporting(csr);
	disable_ae_error_reporting(csr);
	disable_cpp_error_reporting(csr);
	disable_ti_ri_error_reporting(csr);
}

static void adf_gen4_process_errsou0(struct adf_accel_dev *accel_dev,
				     void __iomem *csr)
{
	u32 aecorrerr = ADF_CSR_RD(csr, ADF_GEN4_HIAECORERRLOG_CPP0);

	aecorrerr &= GET_HW_DATA(accel_dev)->ae_mask;

	dev_warn(&GET_DEV(accel_dev),
		 "Correctable error detected in AE: 0x%x\n",
		 aecorrerr);

	/* Clear interrupt from ERRSOU0 */
	ADF_CSR_WR(csr, ADF_GEN4_HIAECORERRLOG_CPP0, aecorrerr);
}

static bool adf_handle_cpp_aeunc(struct adf_accel_dev *accel_dev,
				 void __iomem *csr, u32 errsou)
{
	u32 aeuncorerr;

	if (!(errsou & ADF_GEN4_ERRSOU1_HIAEUNCERRLOG_CPP0_BIT))
		return false;

	aeuncorerr = ADF_CSR_RD(csr, ADF_GEN4_HIAEUNCERRLOG_CPP0);
	aeuncorerr &= GET_HW_DATA(accel_dev)->ae_mask;

	dev_err(&GET_DEV(accel_dev),
		"Uncorrectable error detected in AE: 0x%x\n",
		aeuncorerr);

	ADF_CSR_WR(csr, ADF_GEN4_HIAEUNCERRLOG_CPP0, aeuncorerr);

	return false;
}

static bool adf_handle_cppcmdparerr(struct adf_accel_dev *accel_dev,
				    void __iomem *csr, u32 errsou)
{
	struct adf_dev_err_mask *err_mask = GET_ERR_MASK(accel_dev);
	u32 cmdparerr;

	if (!(errsou & ADF_GEN4_ERRSOU1_HICPPAGENTCMDPARERRLOG_BIT))
		return false;

	cmdparerr = ADF_CSR_RD(csr, ADF_GEN4_HICPPAGENTCMDPARERRLOG);
	cmdparerr &= err_mask->cppagentcmdpar_mask;

	dev_err(&GET_DEV(accel_dev),
		"HI CPP agent command parity error: 0x%x\n",
		cmdparerr);

	ADF_CSR_WR(csr, ADF_GEN4_HICPPAGENTCMDPARERRLOG, cmdparerr);

	return true;
}

static bool adf_handle_ri_mem_par_err(struct adf_accel_dev *accel_dev,
				      void __iomem *csr, u32 errsou)
{
	bool reset_required = false;
	u32 rimem_parerr_sts;

	if (!(errsou & ADF_GEN4_ERRSOU1_RIMEM_PARERR_STS_BIT))
		return false;

	rimem_parerr_sts = ADF_CSR_RD(csr, ADF_GEN4_RIMEM_PARERR_STS);
	rimem_parerr_sts &= ADF_GEN4_RIMEM_PARERR_STS_UNCERR_BITMASK |
			    ADF_GEN4_RIMEM_PARERR_STS_FATAL_BITMASK;

	if (rimem_parerr_sts & ADF_GEN4_RIMEM_PARERR_STS_UNCERR_BITMASK)
		dev_err(&GET_DEV(accel_dev),
			"RI Memory Parity uncorrectable error: 0x%x\n",
			rimem_parerr_sts);

	if (rimem_parerr_sts & ADF_GEN4_RIMEM_PARERR_STS_FATAL_BITMASK) {
		dev_err(&GET_DEV(accel_dev),
			"RI Memory Parity fatal error: 0x%x\n",
			rimem_parerr_sts);
		reset_required = true;
	}

	ADF_CSR_WR(csr, ADF_GEN4_RIMEM_PARERR_STS, rimem_parerr_sts);

	return reset_required;
}

static bool adf_handle_ti_ci_par_sts(struct adf_accel_dev *accel_dev,
				     void __iomem *csr, u32 errsou)
{
	u32 ti_ci_par_sts;

	if (!(errsou & ADF_GEN4_ERRSOU1_TIMEM_PARERR_STS_BIT))
		return false;

	ti_ci_par_sts = ADF_CSR_RD(csr, ADF_GEN4_TI_CI_PAR_STS);
	ti_ci_par_sts &= ADF_GEN4_TI_CI_PAR_STS_BITMASK;

	if (ti_ci_par_sts) {
		dev_err(&GET_DEV(accel_dev),
			"TI Memory Parity Error: 0x%x\n", ti_ci_par_sts);
		ADF_CSR_WR(csr, ADF_GEN4_TI_CI_PAR_STS, ti_ci_par_sts);
	}

	return false;
}

static bool adf_handle_ti_pullfub_par_sts(struct adf_accel_dev *accel_dev,
					  void __iomem *csr, u32 errsou)
{
	u32 ti_pullfub_par_sts;

	if (!(errsou & ADF_GEN4_ERRSOU1_TIMEM_PARERR_STS_BIT))
		return false;

	ti_pullfub_par_sts = ADF_CSR_RD(csr, ADF_GEN4_TI_PULL0FUB_PAR_STS);
	ti_pullfub_par_sts &= ADF_GEN4_TI_PULL0FUB_PAR_STS_BITMASK;

	if (ti_pullfub_par_sts) {
		dev_err(&GET_DEV(accel_dev),
			"TI Pull Parity Error: 0x%x\n", ti_pullfub_par_sts);

		ADF_CSR_WR(csr, ADF_GEN4_TI_PULL0FUB_PAR_STS,
			   ti_pullfub_par_sts);
	}

	return false;
}

static bool adf_handle_ti_pushfub_par_sts(struct adf_accel_dev *accel_dev,
					  void __iomem *csr, u32 errsou)
{
	u32 ti_pushfub_par_sts;

	if (!(errsou & ADF_GEN4_ERRSOU1_TIMEM_PARERR_STS_BIT))
		return false;

	ti_pushfub_par_sts = ADF_CSR_RD(csr, ADF_GEN4_TI_PUSHFUB_PAR_STS);
	ti_pushfub_par_sts &= ADF_GEN4_TI_PUSHFUB_PAR_STS_BITMASK;

	if (ti_pushfub_par_sts) {
		dev_err(&GET_DEV(accel_dev),
			"TI Push Parity Error: 0x%x\n", ti_pushfub_par_sts);

		ADF_CSR_WR(csr, ADF_GEN4_TI_PUSHFUB_PAR_STS,
			   ti_pushfub_par_sts);
	}

	return false;
}

static bool adf_handle_ti_cd_par_sts(struct adf_accel_dev *accel_dev,
				     void __iomem *csr, u32 errsou)
{
	u32 ti_cd_par_sts;

	if (!(errsou & ADF_GEN4_ERRSOU1_TIMEM_PARERR_STS_BIT))
		return false;

	ti_cd_par_sts = ADF_CSR_RD(csr, ADF_GEN4_TI_CD_PAR_STS);
	ti_cd_par_sts &= ADF_GEN4_TI_CD_PAR_STS_BITMASK;

	if (ti_cd_par_sts) {
		dev_err(&GET_DEV(accel_dev),
			"TI CD Parity Error: 0x%x\n", ti_cd_par_sts);

		ADF_CSR_WR(csr, ADF_GEN4_TI_CD_PAR_STS, ti_cd_par_sts);
	}

	return false;
}

static bool adf_handle_ti_trnsb_par_sts(struct adf_accel_dev *accel_dev,
					void __iomem *csr, u32 errsou)
{
	u32 ti_trnsb_par_sts;

	if (!(errsou & ADF_GEN4_ERRSOU1_TIMEM_PARERR_STS_BIT))
		return false;

	ti_trnsb_par_sts = ADF_CSR_RD(csr, ADF_GEN4_TI_TRNSB_PAR_STS);
	ti_trnsb_par_sts &= ADF_GEN4_TI_TRNSB_PAR_STS_BITMASK;

	if (ti_trnsb_par_sts) {
		dev_err(&GET_DEV(accel_dev),
			"TI TRNSB Parity Error: 0x%x\n", ti_trnsb_par_sts);

		ADF_CSR_WR(csr, ADF_GEN4_TI_TRNSB_PAR_STS, ti_trnsb_par_sts);
	}

	return false;
}

static bool adf_handle_iosfp_cmd_parerr(struct adf_accel_dev *accel_dev,
					void __iomem *csr, u32 errsou)
{
	u32 rimiscsts;

	if (!(errsou & ADF_GEN4_ERRSOU1_TIMEM_PARERR_STS_BIT))
		return false;

	rimiscsts = ADF_CSR_RD(csr, ADF_GEN4_RIMISCSTS);
	rimiscsts &= ADF_GEN4_RIMISCSTS_BIT;

	dev_err(&GET_DEV(accel_dev),
		"Command Parity error detected on IOSFP: 0x%x\n",
		rimiscsts);

	ADF_CSR_WR(csr, ADF_GEN4_RIMISCSTS, rimiscsts);

	return true;
}

static void adf_gen4_process_errsou1(struct adf_accel_dev *accel_dev,
				     void __iomem *csr, u32 errsou,
				     bool *reset_required)
{
	*reset_required |= adf_handle_cpp_aeunc(accel_dev, csr, errsou);
	*reset_required |= adf_handle_cppcmdparerr(accel_dev, csr, errsou);
	*reset_required |= adf_handle_ri_mem_par_err(accel_dev, csr, errsou);
	*reset_required |= adf_handle_ti_ci_par_sts(accel_dev, csr, errsou);
	*reset_required |= adf_handle_ti_pullfub_par_sts(accel_dev, csr, errsou);
	*reset_required |= adf_handle_ti_pushfub_par_sts(accel_dev, csr, errsou);
	*reset_required |= adf_handle_ti_cd_par_sts(accel_dev, csr, errsou);
	*reset_required |= adf_handle_ti_trnsb_par_sts(accel_dev, csr, errsou);
	*reset_required |= adf_handle_iosfp_cmd_parerr(accel_dev, csr, errsou);
}

static bool adf_gen4_handle_interrupt(struct adf_accel_dev *accel_dev,
				      bool *reset_required)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);
	u32 errsou = ADF_CSR_RD(csr, ADF_GEN4_ERRSOU0);
	bool handled = false;

	*reset_required = false;

	if (errsou & ADF_GEN4_ERRSOU0_BIT) {
		adf_gen4_process_errsou0(accel_dev, csr);
		handled = true;
	}

	errsou = ADF_CSR_RD(csr, ADF_GEN4_ERRSOU1);
	if (errsou & ADF_GEN4_ERRSOU1_BITMASK) {
		adf_gen4_process_errsou1(accel_dev, csr, errsou, reset_required);
		handled = true;
	}

	return handled;
}

void adf_gen4_init_ras_ops(struct adf_ras_ops *ras_ops)
{
	ras_ops->enable_ras_errors = adf_gen4_enable_ras;
	ras_ops->disable_ras_errors = adf_gen4_disable_ras;
	ras_ops->handle_interrupt = adf_gen4_handle_interrupt;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_ras_ops);
