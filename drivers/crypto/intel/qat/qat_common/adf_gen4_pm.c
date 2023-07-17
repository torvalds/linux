// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2022 Intel Corporation */
#include <linux/bitfield.h>
#include <linux/iopoll.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_gen4_pm.h"
#include "adf_cfg_strings.h"
#include "icp_qat_fw_init_admin.h"
#include "adf_gen4_hw_data.h"
#include "adf_cfg.h"

enum qat_pm_host_msg {
	PM_NO_CHANGE = 0,
	PM_SET_MIN,
};

struct adf_gen4_pm_data {
	struct work_struct pm_irq_work;
	struct adf_accel_dev *accel_dev;
	u32 pm_int_sts;
};

static int send_host_msg(struct adf_accel_dev *accel_dev)
{
	char pm_idle_support_cfg[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = {};
	void __iomem *pmisc = adf_get_pmisc_base(accel_dev);
	bool pm_idle_support;
	u32 msg;
	int ret;

	msg = ADF_CSR_RD(pmisc, ADF_GEN4_PM_HOST_MSG);
	if (msg & ADF_GEN4_PM_MSG_PENDING)
		return -EBUSY;

	adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC,
				ADF_PM_IDLE_SUPPORT, pm_idle_support_cfg);
	ret = kstrtobool(pm_idle_support_cfg, &pm_idle_support);
	if (ret)
		pm_idle_support = true;

	/* Send HOST_MSG */
	msg = FIELD_PREP(ADF_GEN4_PM_MSG_PAYLOAD_BIT_MASK,
			 pm_idle_support ? PM_SET_MIN : PM_NO_CHANGE);
	msg |= ADF_GEN4_PM_MSG_PENDING;
	ADF_CSR_WR(pmisc, ADF_GEN4_PM_HOST_MSG, msg);

	/* Poll status register to make sure the HOST_MSG has been processed */
	return read_poll_timeout(ADF_CSR_RD, msg,
				!(msg & ADF_GEN4_PM_MSG_PENDING),
				ADF_GEN4_PM_MSG_POLL_DELAY_US,
				ADF_GEN4_PM_POLL_TIMEOUT_US, true, pmisc,
				ADF_GEN4_PM_HOST_MSG);
}

static void pm_bh_handler(struct work_struct *work)
{
	struct adf_gen4_pm_data *pm_data =
		container_of(work, struct adf_gen4_pm_data, pm_irq_work);
	struct adf_accel_dev *accel_dev = pm_data->accel_dev;
	void __iomem *pmisc = adf_get_pmisc_base(accel_dev);
	u32 pm_int_sts = pm_data->pm_int_sts;
	u32 val;

	/* PM Idle interrupt */
	if (pm_int_sts & ADF_GEN4_PM_IDLE_STS) {
		/* Issue host message to FW */
		if (send_host_msg(accel_dev))
			dev_warn_ratelimited(&GET_DEV(accel_dev),
					     "Failed to send host msg to FW\n");
	}

	/* Clear interrupt status */
	ADF_CSR_WR(pmisc, ADF_GEN4_PM_INTERRUPT, pm_int_sts);

	/* Reenable PM interrupt */
	val = ADF_CSR_RD(pmisc, ADF_GEN4_ERRMSK2);
	val &= ~ADF_GEN4_PM_SOU;
	ADF_CSR_WR(pmisc, ADF_GEN4_ERRMSK2, val);

	kfree(pm_data);
}

bool adf_gen4_handle_pm_interrupt(struct adf_accel_dev *accel_dev)
{
	void __iomem *pmisc = adf_get_pmisc_base(accel_dev);
	struct adf_gen4_pm_data *pm_data = NULL;
	u32 errsou2;
	u32 errmsk2;
	u32 val;

	/* Only handle the interrupt triggered by PM */
	errmsk2 = ADF_CSR_RD(pmisc, ADF_GEN4_ERRMSK2);
	if (errmsk2 & ADF_GEN4_PM_SOU)
		return false;

	errsou2 = ADF_CSR_RD(pmisc, ADF_GEN4_ERRSOU2);
	if (!(errsou2 & ADF_GEN4_PM_SOU))
		return false;

	/* Disable interrupt */
	val = ADF_CSR_RD(pmisc, ADF_GEN4_ERRMSK2);
	val |= ADF_GEN4_PM_SOU;
	ADF_CSR_WR(pmisc, ADF_GEN4_ERRMSK2, val);

	val = ADF_CSR_RD(pmisc, ADF_GEN4_PM_INTERRUPT);

	pm_data = kzalloc(sizeof(*pm_data), GFP_ATOMIC);
	if (!pm_data)
		return false;

	pm_data->pm_int_sts = val;
	pm_data->accel_dev = accel_dev;

	INIT_WORK(&pm_data->pm_irq_work, pm_bh_handler);
	adf_misc_wq_queue_work(&pm_data->pm_irq_work);

	return true;
}
EXPORT_SYMBOL_GPL(adf_gen4_handle_pm_interrupt);

int adf_gen4_enable_pm(struct adf_accel_dev *accel_dev)
{
	void __iomem *pmisc = adf_get_pmisc_base(accel_dev);
	int ret;
	u32 val;

	ret = adf_init_admin_pm(accel_dev, ADF_GEN4_PM_DEFAULT_IDLE_FILTER);
	if (ret)
		return ret;

	/* Enable default PM interrupts: IDLE, THROTTLE */
	val = ADF_CSR_RD(pmisc, ADF_GEN4_PM_INTERRUPT);
	val |= ADF_GEN4_PM_INT_EN_DEFAULT;

	/* Clear interrupt status */
	val |= ADF_GEN4_PM_INT_STS_MASK;
	ADF_CSR_WR(pmisc, ADF_GEN4_PM_INTERRUPT, val);

	/* Unmask PM Interrupt */
	val = ADF_CSR_RD(pmisc, ADF_GEN4_ERRMSK2);
	val &= ~ADF_GEN4_PM_SOU;
	ADF_CSR_WR(pmisc, ADF_GEN4_ERRMSK2, val);

	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_enable_pm);
