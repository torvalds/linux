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

	/*
	 * Enable uncorrectable error reporting in ERRSOU2
	 * but disable PM interrupt and CFC attention interrupt by default
	 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK2,
		   ADF_GEN4_ERRSOU2_PM_INT_BIT |
		   ADF_GEN4_ERRSOU2_CPP_CFC_ATT_INT_BITMASK);
}

static void disable_errsou_reporting(void __iomem *csr)
{
	u32 val = 0;

	/* Disable correctable error reporting in ERRSOU0 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK0, ADF_GEN4_ERRSOU0_BIT);

	/* Disable uncorrectable error reporting in ERRSOU1 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK1, ADF_GEN4_ERRSOU1_BITMASK);

	/* Disable uncorrectable error reporting in ERRSOU2 */
	val = ADF_CSR_RD(csr, ADF_GEN4_ERRMSK2);
	val |= ADF_GEN4_ERRSOU2_DIS_BITMASK;
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK2, val);
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

	ADF_CSR_WR(csr, ADF_GEN4_CPP_CFC_ERR_CTRL,
		   ADF_GEN4_CPP_CFC_ERR_CTRL_BITMASK);
}

static void disable_cpp_error_reporting(void __iomem *csr)
{
	/* Disable HI CPP Agents Command Parity Error Reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HICPPAGENTCMDPARERRLOGENABLE, 0);

	ADF_CSR_WR(csr, ADF_GEN4_CPP_CFC_ERR_CTRL,
		   ADF_GEN4_CPP_CFC_ERR_CTRL_DIS_BITMASK);
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

static void enable_rf_error_reporting(struct adf_accel_dev *accel_dev,
				      void __iomem *csr)
{
	struct adf_dev_err_mask *err_mask = GET_ERR_MASK(accel_dev);

	/* Enable RF parity error in Shared RAM */
	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_SRC, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_ATH_CPH, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_CPR_XLT, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_DCPR_UCS, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_PKE, 0);

	if (err_mask->parerr_wat_wcp_mask)
		ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_WAT_WCP, 0);
}

static void disable_rf_error_reporting(struct adf_accel_dev *accel_dev,
				       void __iomem *csr)
{
	struct adf_dev_err_mask *err_mask = GET_ERR_MASK(accel_dev);

	/* Disable RF Parity Error reporting in Shared RAM */
	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_SRC,
		   ADF_GEN4_SSMSOFTERRORPARITY_SRC_BIT);

	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_ATH_CPH,
		   err_mask->parerr_ath_cph_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_CPR_XLT,
		   err_mask->parerr_cpr_xlt_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_DCPR_UCS,
		   err_mask->parerr_dcpr_ucs_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_PKE,
		   err_mask->parerr_pke_mask);

	if (err_mask->parerr_wat_wcp_mask)
		ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_WAT_WCP,
			   err_mask->parerr_wat_wcp_mask);
}

static void enable_ssm_error_reporting(struct adf_accel_dev *accel_dev,
				       void __iomem *csr)
{
	struct adf_dev_err_mask *err_mask = GET_ERR_MASK(accel_dev);
	u32 val = 0;

	/* Enable SSM interrupts */
	ADF_CSR_WR(csr, ADF_GEN4_INTMASKSSM, 0);

	/* Enable shared memory error detection & correction */
	val = ADF_CSR_RD(csr, ADF_GEN4_SSMFEATREN);
	val |= err_mask->ssmfeatren_mask;
	ADF_CSR_WR(csr, ADF_GEN4_SSMFEATREN, val);

	/* Enable SER detection in SER_err_ssmsh register */
	ADF_CSR_WR(csr, ADF_GEN4_SER_EN_SSMSH,
		   ADF_GEN4_SER_EN_SSMSH_BITMASK);

	/* Enable SSM soft parity error */
	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_ATH_CPH, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_CPR_XLT, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_DCPR_UCS, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_PKE, 0);

	if (err_mask->parerr_wat_wcp_mask)
		ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_WAT_WCP, 0);

	/* Enable slice hang interrupt reporting */
	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_ATH_CPH, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_CPR_XLT, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_DCPR_UCS, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_PKE, 0);

	if (err_mask->parerr_wat_wcp_mask)
		ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_WAT_WCP, 0);
}

static void disable_ssm_error_reporting(struct adf_accel_dev *accel_dev,
					void __iomem *csr)
{
	struct adf_dev_err_mask *err_mask = GET_ERR_MASK(accel_dev);
	u32 val = 0;

	/* Disable SSM interrupts */
	ADF_CSR_WR(csr, ADF_GEN4_INTMASKSSM,
		   ADF_GEN4_INTMASKSSM_BITMASK);

	/* Disable shared memory error detection & correction */
	val = ADF_CSR_RD(csr, ADF_GEN4_SSMFEATREN);
	val &= ADF_GEN4_SSMFEATREN_DIS_BITMASK;
	ADF_CSR_WR(csr, ADF_GEN4_SSMFEATREN, val);

	/* Disable SER detection in SER_err_ssmsh register */
	ADF_CSR_WR(csr, ADF_GEN4_SER_EN_SSMSH, 0);

	/* Disable SSM soft parity error */
	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_ATH_CPH,
		   err_mask->parerr_ath_cph_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_CPR_XLT,
		   err_mask->parerr_cpr_xlt_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_DCPR_UCS,
		   err_mask->parerr_dcpr_ucs_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_PKE,
		   err_mask->parerr_pke_mask);

	if (err_mask->parerr_wat_wcp_mask)
		ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_WAT_WCP,
			   err_mask->parerr_wat_wcp_mask);

	/* Disable slice hang interrupt reporting */
	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_ATH_CPH,
		   err_mask->parerr_ath_cph_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_CPR_XLT,
		   err_mask->parerr_cpr_xlt_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_DCPR_UCS,
		   err_mask->parerr_dcpr_ucs_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_PKE,
		   err_mask->parerr_pke_mask);

	if (err_mask->parerr_wat_wcp_mask)
		ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_WAT_WCP,
			   err_mask->parerr_wat_wcp_mask);
}

static void adf_gen4_enable_ras(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);

	enable_errsou_reporting(csr);
	enable_ae_error_reporting(accel_dev, csr);
	enable_cpp_error_reporting(accel_dev, csr);
	enable_ti_ri_error_reporting(csr);
	enable_rf_error_reporting(accel_dev, csr);
	enable_ssm_error_reporting(accel_dev, csr);
}

static void adf_gen4_disable_ras(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);

	disable_errsou_reporting(csr);
	disable_ae_error_reporting(csr);
	disable_cpp_error_reporting(csr);
	disable_ti_ri_error_reporting(csr);
	disable_rf_error_reporting(accel_dev, csr);
	disable_ssm_error_reporting(accel_dev, csr);
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

static bool adf_handle_uerrssmsh(struct adf_accel_dev *accel_dev,
				 void __iomem *csr, u32 iastatssm)
{
	u32 reg;

	if (!(iastatssm & ADF_GEN4_IAINTSTATSSM_UERRSSMSH_BIT))
		return false;

	reg = ADF_CSR_RD(csr, ADF_GEN4_UERRSSMSH);
	reg &= ADF_GEN4_UERRSSMSH_BITMASK;

	dev_err(&GET_DEV(accel_dev),
		"Uncorrectable error on ssm shared memory: 0x%x\n",
		reg);

	ADF_CSR_WR(csr, ADF_GEN4_UERRSSMSH, reg);

	return false;
}

static bool adf_handle_cerrssmsh(struct adf_accel_dev *accel_dev,
				 void __iomem *csr, u32 iastatssm)
{
	u32 reg;

	if (!(iastatssm & ADF_GEN4_IAINTSTATSSM_CERRSSMSH_BIT))
		return false;

	reg = ADF_CSR_RD(csr, ADF_GEN4_CERRSSMSH);
	reg &= ADF_GEN4_CERRSSMSH_ERROR_BIT;

	dev_warn(&GET_DEV(accel_dev),
		 "Correctable error on ssm shared memory: 0x%x\n",
		 reg);

	ADF_CSR_WR(csr, ADF_GEN4_CERRSSMSH, reg);

	return false;
}

static bool adf_handle_pperr_err(struct adf_accel_dev *accel_dev,
				 void __iomem *csr, u32 iastatssm)
{
	u32 reg;

	if (!(iastatssm & ADF_GEN4_IAINTSTATSSM_PPERR_BIT))
		return false;

	reg = ADF_CSR_RD(csr, ADF_GEN4_PPERR);
	reg &= ADF_GEN4_PPERR_BITMASK;

	dev_err(&GET_DEV(accel_dev),
		"Uncorrectable error CPP transaction on memory target: 0x%x\n",
		reg);

	ADF_CSR_WR(csr, ADF_GEN4_PPERR, reg);

	return false;
}

static void adf_poll_slicehang_csr(struct adf_accel_dev *accel_dev,
				   void __iomem *csr, u32 slice_hang_offset,
				   char *slice_name)
{
	u32 slice_hang_reg = ADF_CSR_RD(csr, slice_hang_offset);

	if (!slice_hang_reg)
		return;

	dev_err(&GET_DEV(accel_dev),
		"Slice %s hang error encountered\n", slice_name);
}

static bool adf_handle_slice_hang_error(struct adf_accel_dev *accel_dev,
					void __iomem *csr, u32 iastatssm)
{
	struct adf_dev_err_mask *err_mask = GET_ERR_MASK(accel_dev);

	if (!(iastatssm & ADF_GEN4_IAINTSTATSSM_SLICEHANG_ERR_BIT))
		return false;

	adf_poll_slicehang_csr(accel_dev, csr,
			       ADF_GEN4_SLICEHANGSTATUS_ATH_CPH, "ath_cph");
	adf_poll_slicehang_csr(accel_dev, csr,
			       ADF_GEN4_SLICEHANGSTATUS_CPR_XLT, "cpr_xlt");
	adf_poll_slicehang_csr(accel_dev, csr,
			       ADF_GEN4_SLICEHANGSTATUS_DCPR_UCS, "dcpr_ucs");
	adf_poll_slicehang_csr(accel_dev, csr,
			       ADF_GEN4_SLICEHANGSTATUS_PKE, "pke");

	if (err_mask->parerr_wat_wcp_mask)
		adf_poll_slicehang_csr(accel_dev, csr,
				       ADF_GEN4_SLICEHANGSTATUS_WAT_WCP,
				       "ath_cph");

	return false;
}

static bool adf_handle_spp_pullcmd_err(struct adf_accel_dev *accel_dev,
				       void __iomem *csr)
{
	struct adf_dev_err_mask *err_mask = GET_ERR_MASK(accel_dev);
	bool reset_required = false;
	u32 reg;

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLCMDPARERR_ATH_CPH);
	reg &= err_mask->parerr_ath_cph_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull command fatal error ATH_CPH: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLCMDPARERR_ATH_CPH, reg);

		reset_required = true;
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLCMDPARERR_CPR_XLT);
	reg &= err_mask->parerr_cpr_xlt_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull command fatal error CPR_XLT: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLCMDPARERR_CPR_XLT, reg);

		reset_required = true;
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLCMDPARERR_DCPR_UCS);
	reg &= err_mask->parerr_dcpr_ucs_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull command fatal error DCPR_UCS: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLCMDPARERR_DCPR_UCS, reg);

		reset_required = true;
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLCMDPARERR_PKE);
	reg &= err_mask->parerr_pke_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull command fatal error PKE: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLCMDPARERR_PKE, reg);

		reset_required = true;
	}

	if (err_mask->parerr_wat_wcp_mask) {
		reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLCMDPARERR_WAT_WCP);
		reg &= err_mask->parerr_wat_wcp_mask;
		if (reg) {
			dev_err(&GET_DEV(accel_dev),
				"SPP pull command fatal error WAT_WCP: 0x%x\n", reg);

			ADF_CSR_WR(csr, ADF_GEN4_SPPPULLCMDPARERR_WAT_WCP, reg);

			reset_required = true;
		}
	}

	return reset_required;
}

static bool adf_handle_spp_pulldata_err(struct adf_accel_dev *accel_dev,
					void __iomem *csr)
{
	struct adf_dev_err_mask *err_mask = GET_ERR_MASK(accel_dev);
	u32 reg;

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLDATAPARERR_ATH_CPH);
	reg &= err_mask->parerr_ath_cph_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull data err ATH_CPH: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLDATAPARERR_ATH_CPH, reg);
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLDATAPARERR_CPR_XLT);
	reg &= err_mask->parerr_cpr_xlt_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull data err CPR_XLT: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLDATAPARERR_CPR_XLT, reg);
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLDATAPARERR_DCPR_UCS);
	reg &= err_mask->parerr_dcpr_ucs_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull data err DCPR_UCS: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLDATAPARERR_DCPR_UCS, reg);
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLDATAPARERR_PKE);
	reg &= err_mask->parerr_pke_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull data err PKE: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLDATAPARERR_PKE, reg);
	}

	if (err_mask->parerr_wat_wcp_mask) {
		reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLDATAPARERR_WAT_WCP);
		reg &= err_mask->parerr_wat_wcp_mask;
		if (reg) {
			dev_err(&GET_DEV(accel_dev),
				"SPP pull data err WAT_WCP: 0x%x\n", reg);

			ADF_CSR_WR(csr, ADF_GEN4_SPPPULLDATAPARERR_WAT_WCP, reg);
		}
	}

	return false;
}

static bool adf_handle_spp_pushcmd_err(struct adf_accel_dev *accel_dev,
				       void __iomem *csr)
{
	struct adf_dev_err_mask *err_mask = GET_ERR_MASK(accel_dev);
	bool reset_required = false;
	u32 reg;

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHCMDPARERR_ATH_CPH);
	reg &= err_mask->parerr_ath_cph_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push command fatal error ATH_CPH: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHCMDPARERR_ATH_CPH, reg);

		reset_required = true;
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHCMDPARERR_CPR_XLT);
	reg &= err_mask->parerr_cpr_xlt_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push command fatal error CPR_XLT: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHCMDPARERR_CPR_XLT, reg);

		reset_required = true;
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHCMDPARERR_DCPR_UCS);
	reg &= err_mask->parerr_dcpr_ucs_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push command fatal error DCPR_UCS: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHCMDPARERR_DCPR_UCS, reg);

		reset_required = true;
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHCMDPARERR_PKE);
	reg &= err_mask->parerr_pke_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push command fatal error PKE: 0x%x\n",
			reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHCMDPARERR_PKE, reg);

		reset_required = true;
	}

	if (err_mask->parerr_wat_wcp_mask) {
		reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHCMDPARERR_WAT_WCP);
		reg &= err_mask->parerr_wat_wcp_mask;
		if (reg) {
			dev_err(&GET_DEV(accel_dev),
				"SPP push command fatal error WAT_WCP: 0x%x\n", reg);

			ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHCMDPARERR_WAT_WCP, reg);

			reset_required = true;
		}
	}

	return reset_required;
}

static bool adf_handle_spp_pushdata_err(struct adf_accel_dev *accel_dev,
					void __iomem *csr)
{
	struct adf_dev_err_mask *err_mask = GET_ERR_MASK(accel_dev);
	u32 reg;

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHDATAPARERR_ATH_CPH);
	reg &= err_mask->parerr_ath_cph_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push data err ATH_CPH: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHDATAPARERR_ATH_CPH, reg);
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHDATAPARERR_CPR_XLT);
	reg &= err_mask->parerr_cpr_xlt_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push data err CPR_XLT: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHDATAPARERR_CPR_XLT, reg);
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHDATAPARERR_DCPR_UCS);
	reg &= err_mask->parerr_dcpr_ucs_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push data err DCPR_UCS: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHDATAPARERR_DCPR_UCS, reg);
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHDATAPARERR_PKE);
	reg &= err_mask->parerr_pke_mask;
	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push data err PKE: 0x%x\n", reg);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHDATAPARERR_PKE, reg);
	}

	if (err_mask->parerr_wat_wcp_mask) {
		reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHDATAPARERR_WAT_WCP);
		reg &= err_mask->parerr_wat_wcp_mask;
		if (reg) {
			dev_err(&GET_DEV(accel_dev),
				"SPP push data err WAT_WCP: 0x%x\n", reg);

			ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHDATAPARERR_WAT_WCP,
				   reg);
		}
	}

	return false;
}

static bool adf_handle_spppar_err(struct adf_accel_dev *accel_dev,
				  void __iomem *csr, u32 iastatssm)
{
	bool reset_required;

	if (!(iastatssm & ADF_GEN4_IAINTSTATSSM_SPPPARERR_BIT))
		return false;

	reset_required = adf_handle_spp_pullcmd_err(accel_dev, csr);
	reset_required |= adf_handle_spp_pulldata_err(accel_dev, csr);
	reset_required |= adf_handle_spp_pushcmd_err(accel_dev, csr);
	reset_required |= adf_handle_spp_pushdata_err(accel_dev, csr);

	return reset_required;
}

static bool adf_handle_ssmcpppar_err(struct adf_accel_dev *accel_dev,
				     void __iomem *csr, u32 iastatssm)
{
	bool reset_required = false;
	u32 reg;

	if (!(iastatssm & ADF_GEN4_IAINTSTATSSM_SSMCPPERR_BIT))
		return false;

	reg = ADF_CSR_RD(csr, ADF_GEN4_SSMCPPERR);
	reg &= ADF_GEN4_SSMCPPERR_FATAL_BITMASK | ADF_GEN4_SSMCPPERR_UNCERR_BITMASK;
	if (reg & ADF_GEN4_SSMCPPERR_FATAL_BITMASK) {
		dev_err(&GET_DEV(accel_dev),
			"Fatal SSM CPP parity error: 0x%x\n", reg);

		reset_required = true;
	}

	if (reg & ADF_GEN4_SSMCPPERR_UNCERR_BITMASK)
		dev_err(&GET_DEV(accel_dev),
			"non-Fatal SSM CPP parity error: 0x%x\n", reg);

	ADF_CSR_WR(csr, ADF_GEN4_SSMCPPERR, reg);

	return reset_required;
}

static bool adf_handle_rf_parr_err(struct adf_accel_dev *accel_dev,
				   void __iomem *csr, u32 iastatssm)
{
	struct adf_dev_err_mask *err_mask = GET_ERR_MASK(accel_dev);
	u32 reg;

	if (!(iastatssm & ADF_GEN4_IAINTSTATSSM_SSMSOFTERRORPARITY_BIT))
		return false;

	reg = ADF_CSR_RD(csr, ADF_GEN4_SSMSOFTERRORPARITY_SRC);
	reg &= ADF_GEN4_SSMSOFTERRORPARITY_SRC_BIT;
	if (reg)
		ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITY_SRC, reg);

	reg = ADF_CSR_RD(csr, ADF_GEN4_SSMSOFTERRORPARITY_ATH_CPH);
	reg &= err_mask->parerr_ath_cph_mask;
	if (reg)
		ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITY_ATH_CPH, reg);

	reg = ADF_CSR_RD(csr, ADF_GEN4_SSMSOFTERRORPARITY_CPR_XLT);
	reg &= err_mask->parerr_cpr_xlt_mask;
	if (reg)
		ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITY_CPR_XLT, reg);

	reg = ADF_CSR_RD(csr, ADF_GEN4_SSMSOFTERRORPARITY_DCPR_UCS);
	reg &= err_mask->parerr_dcpr_ucs_mask;
	if (reg)
		ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITY_DCPR_UCS, reg);

	reg = ADF_CSR_RD(csr, ADF_GEN4_SSMSOFTERRORPARITY_PKE);
	reg &= err_mask->parerr_pke_mask;
	if (reg)
		ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITY_PKE, reg);

	if (err_mask->parerr_wat_wcp_mask) {
		reg = ADF_CSR_RD(csr, ADF_GEN4_SSMSOFTERRORPARITY_WAT_WCP);
		reg &= err_mask->parerr_wat_wcp_mask;
		if (reg)
			ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITY_WAT_WCP,
				   reg);
	}

	dev_err(&GET_DEV(accel_dev), "Slice ssm soft parity error reported");

	return false;
}

static bool adf_handle_ser_err_ssmsh(struct adf_accel_dev *accel_dev,
				     void __iomem *csr, u32 iastatssm)
{
	bool reset_required = false;
	u32 reg;

	if (!(iastatssm & (ADF_GEN4_IAINTSTATSSM_SER_ERR_SSMSH_CERR_BIT |
			 ADF_GEN4_IAINTSTATSSM_SER_ERR_SSMSH_UNCERR_BIT)))
		return false;

	reg = ADF_CSR_RD(csr, ADF_GEN4_SER_ERR_SSMSH);
	reg &= ADF_GEN4_SER_ERR_SSMSH_FATAL_BITMASK |
	       ADF_GEN4_SER_ERR_SSMSH_UNCERR_BITMASK |
	       ADF_GEN4_SER_ERR_SSMSH_CERR_BITMASK;
	if (reg & ADF_GEN4_SER_ERR_SSMSH_FATAL_BITMASK) {
		dev_err(&GET_DEV(accel_dev),
			"Fatal SER_SSMSH_ERR: 0x%x\n", reg);

		reset_required = true;
	}

	if (reg & ADF_GEN4_SER_ERR_SSMSH_UNCERR_BITMASK)
		dev_err(&GET_DEV(accel_dev),
			"non-fatal SER_SSMSH_ERR: 0x%x\n", reg);

	if (reg & ADF_GEN4_SER_ERR_SSMSH_CERR_BITMASK)
		dev_warn(&GET_DEV(accel_dev),
			 "Correctable SER_SSMSH_ERR: 0x%x\n", reg);

	ADF_CSR_WR(csr, ADF_GEN4_SER_ERR_SSMSH, reg);

	return reset_required;
}

static bool adf_handle_iaintstatssm(struct adf_accel_dev *accel_dev,
				    void __iomem *csr)
{
	u32 iastatssm = ADF_CSR_RD(csr, ADF_GEN4_IAINTSTATSSM);
	bool reset_required;

	iastatssm &= ADF_GEN4_IAINTSTATSSM_BITMASK;
	if (!iastatssm)
		return false;

	reset_required = adf_handle_uerrssmsh(accel_dev, csr, iastatssm);
	reset_required |= adf_handle_cerrssmsh(accel_dev, csr, iastatssm);
	reset_required |= adf_handle_pperr_err(accel_dev, csr, iastatssm);
	reset_required |= adf_handle_slice_hang_error(accel_dev, csr, iastatssm);
	reset_required |= adf_handle_spppar_err(accel_dev, csr, iastatssm);
	reset_required |= adf_handle_ssmcpppar_err(accel_dev, csr, iastatssm);
	reset_required |= adf_handle_rf_parr_err(accel_dev, csr, iastatssm);
	reset_required |= adf_handle_ser_err_ssmsh(accel_dev, csr, iastatssm);

	ADF_CSR_WR(csr, ADF_GEN4_IAINTSTATSSM, iastatssm);

	return reset_required;
}

static bool adf_handle_exprpssmcmpr(struct adf_accel_dev *accel_dev,
				    void __iomem *csr)
{
	u32 reg = ADF_CSR_RD(csr, ADF_GEN4_EXPRPSSMCPR);

	reg &= ADF_GEN4_EXPRPSSMCPR_UNCERR_BITMASK;
	if (!reg)
		return false;

	dev_err(&GET_DEV(accel_dev),
		"Uncorrectable error exception in SSM CMP: 0x%x", reg);

	ADF_CSR_WR(csr, ADF_GEN4_EXPRPSSMCPR, reg);

	return false;
}

static bool adf_handle_exprpssmxlt(struct adf_accel_dev *accel_dev,
				   void __iomem *csr)
{
	u32 reg = ADF_CSR_RD(csr, ADF_GEN4_EXPRPSSMXLT);

	reg &= ADF_GEN4_EXPRPSSMXLT_UNCERR_BITMASK |
	       ADF_GEN4_EXPRPSSMXLT_CERR_BIT;
	if (!reg)
		return false;

	if (reg & ADF_GEN4_EXPRPSSMXLT_UNCERR_BITMASK)
		dev_err(&GET_DEV(accel_dev),
			"Uncorrectable error exception in SSM XLT: 0x%x", reg);

	if (reg & ADF_GEN4_EXPRPSSMXLT_CERR_BIT)
		dev_warn(&GET_DEV(accel_dev),
			 "Correctable error exception in SSM XLT: 0x%x", reg);

	ADF_CSR_WR(csr, ADF_GEN4_EXPRPSSMXLT, reg);

	return false;
}

static bool adf_handle_exprpssmdcpr(struct adf_accel_dev *accel_dev,
				    void __iomem *csr)
{
	u32 reg;
	int i;

	for (i = 0; i < ADF_GEN4_DCPR_SLICES_NUM; i++) {
		reg = ADF_CSR_RD(csr, ADF_GEN4_EXPRPSSMDCPR(i));
		reg &= ADF_GEN4_EXPRPSSMDCPR_UNCERR_BITMASK |
		       ADF_GEN4_EXPRPSSMDCPR_CERR_BITMASK;
		if (!reg)
			continue;

		if (reg & ADF_GEN4_EXPRPSSMDCPR_UNCERR_BITMASK)
			dev_err(&GET_DEV(accel_dev),
				"Uncorrectable error exception in SSM DCMP: 0x%x", reg);

		if (reg & ADF_GEN4_EXPRPSSMDCPR_CERR_BITMASK)
			dev_warn(&GET_DEV(accel_dev),
				 "Correctable error exception in SSM DCMP: 0x%x", reg);

		ADF_CSR_WR(csr, ADF_GEN4_EXPRPSSMDCPR(i), reg);
	}

	return false;
}

static bool adf_handle_ssm(struct adf_accel_dev *accel_dev, void __iomem *csr,
			   u32 errsou)
{
	bool reset_required;

	if (!(errsou & ADF_GEN4_ERRSOU2_SSM_ERR_BIT))
		return false;

	reset_required = adf_handle_iaintstatssm(accel_dev, csr);
	reset_required |= adf_handle_exprpssmcmpr(accel_dev, csr);
	reset_required |= adf_handle_exprpssmxlt(accel_dev, csr);
	reset_required |= adf_handle_exprpssmdcpr(accel_dev, csr);

	return reset_required;
}

static bool adf_handle_cpp_cfc_err(struct adf_accel_dev *accel_dev,
				   void __iomem *csr, u32 errsou)
{
	bool reset_required = false;
	u32 reg;

	if (!(errsou & ADF_GEN4_ERRSOU2_CPP_CFC_ERR_STATUS_BIT))
		return false;

	reg = ADF_CSR_RD(csr, ADF_GEN4_CPP_CFC_ERR_STATUS);
	if (reg & ADF_GEN4_CPP_CFC_ERR_STATUS_DATAPAR_BIT) {
		dev_err(&GET_DEV(accel_dev),
			"CPP_CFC_ERR: data parity: 0x%x", reg);
	}

	if (reg & ADF_GEN4_CPP_CFC_ERR_STATUS_CMDPAR_BIT) {
		dev_err(&GET_DEV(accel_dev),
			"CPP_CFC_ERR: command parity: 0x%x", reg);

		reset_required = true;
	}

	if (reg & ADF_GEN4_CPP_CFC_ERR_STATUS_MERR_BIT) {
		dev_err(&GET_DEV(accel_dev),
			"CPP_CFC_ERR: multiple errors: 0x%x", reg);

		reset_required = true;
	}

	ADF_CSR_WR(csr, ADF_GEN4_CPP_CFC_ERR_STATUS_CLR,
		   ADF_GEN4_CPP_CFC_ERR_STATUS_CLR_BITMASK);

	return reset_required;
}

static void adf_gen4_process_errsou2(struct adf_accel_dev *accel_dev,
				     void __iomem *csr, u32 errsou,
				     bool *reset_required)
{
	*reset_required |= adf_handle_ssm(accel_dev, csr, errsou);
	*reset_required |= adf_handle_cpp_cfc_err(accel_dev, csr, errsou);
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

	errsou = ADF_CSR_RD(csr, ADF_GEN4_ERRSOU2);
	if (errsou & ADF_GEN4_ERRSOU2_BITMASK) {
		adf_gen4_process_errsou2(accel_dev, csr, errsou, reset_required);
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
