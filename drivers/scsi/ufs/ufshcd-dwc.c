// SPDX-License-Identifier: GPL-2.0-only
/*
 * UFS Host driver for Synopsys Designware Core
 *
 * Copyright (C) 2015-2016 Synopsys, Inc. (www.synopsys.com)
 *
 * Authors: Joao Pinto <jpinto@synopsys.com>
 */

#include "ufshcd.h"
#include "unipro.h"

#include "ufshcd-dwc.h"
#include "ufshci-dwc.h"

int ufshcd_dwc_dme_set_attrs(struct ufs_hba *hba,
				const struct ufshcd_dme_attr_val *v, int n)
{
	int ret = 0;
	int attr_node = 0;

	for (attr_node = 0; attr_node < n; attr_node++) {
		ret = ufshcd_dme_set_attr(hba, v[attr_node].attr_sel,
			ATTR_SET_NOR, v[attr_node].mib_val, v[attr_node].peer);

		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ufshcd_dwc_dme_set_attrs);

/**
 * ufshcd_dwc_program_clk_div()
 * This function programs the clk divider value. This value is needed to
 * provide 1 microsecond tick to unipro layer.
 * @hba: Private Structure pointer
 * @divider_val: clock divider value to be programmed
 *
 */
static void ufshcd_dwc_program_clk_div(struct ufs_hba *hba, u32 divider_val)
{
	ufshcd_writel(hba, divider_val, DWC_UFS_REG_HCLKDIV);
}

/**
 * ufshcd_dwc_link_is_up()
 * Check if link is up
 * @hba: private structure pointer
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_dwc_link_is_up(struct ufs_hba *hba)
{
	int dme_result = 0;

	ufshcd_dme_get(hba, UIC_ARG_MIB(VS_POWERSTATE), &dme_result);

	if (dme_result == UFSHCD_LINK_IS_UP) {
		ufshcd_set_link_active(hba);
		return 0;
	}

	return 1;
}

/**
 * ufshcd_dwc_connection_setup()
 * This function configures both the local side (host) and the peer side
 * (device) unipro attributes to establish the connection to application/
 * cport.
 * This function is not required if the hardware is properly configured to
 * have this connection setup on reset. But invoking this function does no
 * harm and should be fine even working with any ufs device.
 *
 * @hba: pointer to drivers private data
 *
 * Returns 0 on success non-zero value on failure
 */
static int ufshcd_dwc_connection_setup(struct ufs_hba *hba)
{
	const struct ufshcd_dme_attr_val setup_attrs[] = {
		{ UIC_ARG_MIB(T_CONNECTIONSTATE), 0, DME_LOCAL },
		{ UIC_ARG_MIB(N_DEVICEID), 0, DME_LOCAL },
		{ UIC_ARG_MIB(N_DEVICEID_VALID), 0, DME_LOCAL },
		{ UIC_ARG_MIB(T_PEERDEVICEID), 1, DME_LOCAL },
		{ UIC_ARG_MIB(T_PEERCPORTID), 0, DME_LOCAL },
		{ UIC_ARG_MIB(T_TRAFFICCLASS), 0, DME_LOCAL },
		{ UIC_ARG_MIB(T_CPORTFLAGS), 0x6, DME_LOCAL },
		{ UIC_ARG_MIB(T_CPORTMODE), 1, DME_LOCAL },
		{ UIC_ARG_MIB(T_CONNECTIONSTATE), 1, DME_LOCAL },
		{ UIC_ARG_MIB(T_CONNECTIONSTATE), 0, DME_PEER },
		{ UIC_ARG_MIB(N_DEVICEID), 1, DME_PEER },
		{ UIC_ARG_MIB(N_DEVICEID_VALID), 1, DME_PEER },
		{ UIC_ARG_MIB(T_PEERDEVICEID), 1, DME_PEER },
		{ UIC_ARG_MIB(T_PEERCPORTID), 0, DME_PEER },
		{ UIC_ARG_MIB(T_TRAFFICCLASS), 0, DME_PEER },
		{ UIC_ARG_MIB(T_CPORTFLAGS), 0x6, DME_PEER },
		{ UIC_ARG_MIB(T_CPORTMODE), 1, DME_PEER },
		{ UIC_ARG_MIB(T_CONNECTIONSTATE), 1, DME_PEER }
	};

	return ufshcd_dwc_dme_set_attrs(hba, setup_attrs, ARRAY_SIZE(setup_attrs));
}

/**
 * ufshcd_dwc_link_startup_notify()
 * UFS Host DWC specific link startup sequence
 * @hba: private structure pointer
 * @status: Callback notify status
 *
 * Returns 0 on success, non-zero value on failure
 */
int ufshcd_dwc_link_startup_notify(struct ufs_hba *hba,
					enum ufs_notify_change_status status)
{
	int err = 0;

	if (status == PRE_CHANGE) {
		ufshcd_dwc_program_clk_div(hba, DWC_UFS_REG_HCLKDIV_DIV_125);

		if (hba->vops->phy_initialization) {
			err = hba->vops->phy_initialization(hba);
			if (err) {
				dev_err(hba->dev, "Phy setup failed (%d)\n",
									err);
				goto out;
			}
		}
	} else { /* POST_CHANGE */
		err = ufshcd_dwc_link_is_up(hba);
		if (err) {
			dev_err(hba->dev, "Link is not up\n");
			goto out;
		}

		err = ufshcd_dwc_connection_setup(hba);
		if (err)
			dev_err(hba->dev, "Connection setup failed (%d)\n",
									err);
	}

out:
	return err;
}
EXPORT_SYMBOL(ufshcd_dwc_link_startup_notify);

MODULE_AUTHOR("Joao Pinto <Joao.Pinto@synopsys.com>");
MODULE_DESCRIPTION("UFS Host driver for Synopsys Designware Core");
MODULE_LICENSE("Dual BSD/GPL");
