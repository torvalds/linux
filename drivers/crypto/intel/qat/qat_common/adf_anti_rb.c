// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2026 Intel Corporation */
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kstrtox.h>

#include "adf_accel_devices.h"
#include "adf_admin.h"
#include "adf_anti_rb.h"
#include "adf_common_drv.h"
#include "icp_qat_fw_init_admin.h"

#define ADF_SVN_RETRY_MAX	60

int adf_anti_rb_commit(struct adf_accel_dev *accel_dev)
{
	return adf_send_admin_arb_commit(accel_dev);
}

int adf_anti_rb_query(struct adf_accel_dev *accel_dev, enum anti_rb cmd, u8 *svn)
{
	return adf_send_admin_arb_query(accel_dev, cmd, svn);
}

int adf_anti_rb_check(struct pci_dev *pdev)
{
	struct adf_anti_rb_hw_data *anti_rb;
	u32 svncheck_sts, cfc_svncheck_sts;
	struct adf_accel_dev *accel_dev;
	void __iomem *pmisc_addr;

	accel_dev = adf_devmgr_pci_to_accel_dev(pdev);
	if (!accel_dev)
		return -EINVAL;

	anti_rb = GET_ANTI_RB_DATA(accel_dev);
	if (!anti_rb->anti_rb_enabled || !anti_rb->anti_rb_enabled(accel_dev))
		return 0;

	pmisc_addr = adf_get_pmisc_base(accel_dev);

	cfc_svncheck_sts = ADF_CSR_RD(pmisc_addr, anti_rb->svncheck_offset);

	svncheck_sts = FIELD_GET(ADF_SVN_STS_MASK, cfc_svncheck_sts);
	switch (svncheck_sts) {
	case ADF_SVN_NO_STS:
		return 0;
	case ADF_SVN_PASS_STS:
		anti_rb->svncheck_retry = 0;
		return 0;
	case ADF_SVN_FAIL_STS:
		dev_err(&GET_DEV(accel_dev), "Security Version Number failure\n");
		return -EIO;
	case ADF_SVN_RETRY_STS:
		if (anti_rb->svncheck_retry++ >= ADF_SVN_RETRY_MAX) {
			anti_rb->svncheck_retry = 0;
			return -ETIMEDOUT;
		}
		msleep(ADF_SVN_RETRY_MS);
		return -EAGAIN;
	default:
		dev_err(&GET_DEV(accel_dev), "Invalid SVN check status\n");
		return -EINVAL;
	}
}
