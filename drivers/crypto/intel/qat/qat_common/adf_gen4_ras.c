// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */
#include "adf_common_drv.h"
#include "adf_gen4_hw_data.h"
#include "adf_gen4_ras.h"

static void enable_errsou_reporting(void __iomem *csr)
{
	/* Enable correctable error reporting in ERRSOU0 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK0, 0);
}

static void disable_errsou_reporting(void __iomem *csr)
{
	/* Disable correctable error reporting in ERRSOU0 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK0, ADF_GEN4_ERRSOU0_BIT);
}

static void enable_ae_error_reporting(struct adf_accel_dev *accel_dev,
				      void __iomem *csr)
{
	u32 ae_mask = GET_HW_DATA(accel_dev)->ae_mask;

	/* Enable Acceleration Engine correctable error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HIAECORERRLOGENABLE_CPP0, ae_mask);
}

static void disable_ae_error_reporting(void __iomem *csr)
{
	/* Disable Acceleration Engine correctable error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HIAECORERRLOGENABLE_CPP0, 0);
}

static void adf_gen4_enable_ras(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);

	enable_errsou_reporting(csr);
	enable_ae_error_reporting(accel_dev, csr);
}

static void adf_gen4_disable_ras(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);

	disable_errsou_reporting(csr);
	disable_ae_error_reporting(csr);
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

	return handled;
}

void adf_gen4_init_ras_ops(struct adf_ras_ops *ras_ops)
{
	ras_ops->enable_ras_errors = adf_gen4_enable_ras;
	ras_ops->disable_ras_errors = adf_gen4_disable_ras;
	ras_ops->handle_interrupt = adf_gen4_handle_interrupt;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_ras_ops);
