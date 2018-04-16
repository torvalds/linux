/** @file  moal_uap_priv.c
  *
  * @brief This file contains standard ioctl functions
  *
  * Copyright (C) 2010-2017, Marvell International Ltd.
  *
  * This software file (the "File") is distributed by Marvell International
  * Ltd. under the terms of the GNU General Public License Version 2, June 1991
  * (the "License").  You may use, redistribute and/or modify this File in
  * accordance with the terms and conditions of the License, a copy of which
  * is available by writing to the Free Software Foundation, Inc.,
  * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
  * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
  *
  * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
  * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
  * this warranty disclaimer.
  *
  */

/************************************************************************
Change log:
    08/06/2010: initial version
************************************************************************/

#include	"moal_main.h"
#include    "moal_uap.h"
#include    "moal_uap_priv.h"

/********************************************************
			Local Variables
********************************************************/

/********************************************************
			Global Variables
********************************************************/

/********************************************************
			Local Functions
********************************************************/

/********************************************************
			Global Functions
********************************************************/

/**
 *  @brief ioctl function for wireless IOCTLs
 *
 *  @param dev      A pointer to net_device structure
 *  @param req      A pointer to ifreq structure
 *  @param cmd      Command
 *
 *  @return          0 --success, otherwise fail
 */
int
woal_uap_do_priv_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	struct iwreq *wrq = (struct iwreq *)req;
	int ret = 0;

	ENTER();

	switch (cmd) {
	case WOAL_UAP_SETNONE_GETNONE:
		switch (wrq->u.data.flags) {
		case WOAL_UAP_START:
			break;
		case WOAL_UAP_STOP:
			ret = woal_uap_bss_ctrl(priv, MOAL_IOCTL_WAIT,
						UAP_BSS_STOP);
			break;
		case WOAL_AP_BSS_START:
			ret = woal_uap_bss_ctrl(priv, MOAL_IOCTL_WAIT,
						UAP_BSS_START);
			break;
		case WOAL_AP_BSS_STOP:
			ret = woal_uap_bss_ctrl(priv, MOAL_IOCTL_WAIT,
						UAP_BSS_STOP);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	case WOAL_UAP_SETONEINT_GETWORDCHAR:
		switch (wrq->u.data.flags) {
		case WOAL_UAP_VERSION:
			ret = woal_get_driver_version(priv, req);
			break;
		case WOAL_UAP_VEREXT:
			ret = woal_get_driver_verext(priv, req);
			break;
		default:
			ret = -EOPNOTSUPP;
			break;
		}
		break;
	case WOAL_UAP_SET_GET_256_CHAR:
		switch (wrq->u.data.flags) {
		case WOAL_WL_FW_RELOAD:
			break;
		case WOAL_AP_SET_CFG:
			ret = woal_uap_set_ap_cfg(priv, wrq->u.data.pointer,
						  wrq->u.data.length);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
	case WOAL_UAP_SETONEINT_GETONEINT:
		switch (wrq->u.data.flags) {
		case WOAL_UAP_SET_GET_BSS_ROLE:
			ret = woal_set_get_bss_role(priv, wrq);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
#endif
#endif
	case WOAL_UAP_HOST_CMD:
		ret = woal_host_command(priv, wrq);
		break;
	case WOAL_UAP_FROYO_START:
		break;
	case WOAL_UAP_FROYO_STOP:
		ret = woal_uap_bss_ctrl(priv, MOAL_IOCTL_WAIT, UAP_BSS_STOP);
		break;
	case WOAL_UAP_FROYO_AP_BSS_START:
		ret = woal_uap_bss_ctrl(priv, MOAL_IOCTL_WAIT, UAP_BSS_START);
		break;
	case WOAL_UAP_FROYO_AP_BSS_STOP:
		ret = woal_uap_bss_ctrl(priv, MOAL_IOCTL_WAIT, UAP_BSS_STOP);
		break;
	case WOAL_UAP_FROYO_WL_FW_RELOAD:
		break;
	case WOAL_UAP_FROYO_AP_SET_CFG:
		ret = woal_uap_set_ap_cfg(priv, wrq->u.data.pointer,
					  wrq->u.data.length);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Handle get info resp
 *
 *  @param priv     Pointer to moal_private structure
 *  @param info     Pointer to mlan_ds_get_info structure
 *
 *  @return         N/A
 */
void
woal_ioctl_get_uap_info_resp(moal_private *priv, mlan_ds_get_info *info)
{
	ENTER();
	switch (info->sub_command) {
	case MLAN_OID_GET_STATS:
		priv->w_stats.discard.fragment =
			info->param.ustats.fcs_error_count;
		priv->w_stats.discard.retries = info->param.ustats.retry_count;
		priv->w_stats.discard.misc =
			info->param.ustats.ack_failure_count;
		break;
	default:
		break;
	}
	LEAVE();
}
