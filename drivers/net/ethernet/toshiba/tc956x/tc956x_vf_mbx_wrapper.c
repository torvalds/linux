/*
 * TC956X ethernet driver.
 *
 * tc956x_mbx_wrapper.c
 *
 * Copyright (C) 2022 Toshiba Electronic Devices & Storage Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  06 Nov 2020 : Initial Version
 *  VERSION     : 00-01
 *
 *  30 Nov 2021 : Base lined for SRIOV
 *  VERSION     : 01-02
 *
 *  20 May 2022 : 1. Automotive Driver, CPE fixes merged and IPA Features supported
 *                2. Base lined version
 *  VERSION     : 03-00
 */

#include "tc956xmac.h"
#include "dwxgmac2.h"

#ifdef TC956X_SRIOV_VF

/**
 * tc956xmac_mbx_dma_tx_mode
 *
 * \brief API to set dma tx mode
 *
 * \details This function is used to send parameters of set dma tx mode to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mode - mode
 * \param[in] channel - DMA channel no
 * \param[in] fifosz - fifo size
 * \param[in] qmode - queue mode
 *
 * \return None
 */
static void tc956xmac_mbx_dma_tx_mode(struct tc956xmac_priv *priv, int mode,
				 u32 channel, int fifosz, u8 qmode)
{

	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return;
	}

	mbx[0] = OPCODE_MBX_SET_DMA_TX_MODE; //opcode
	mbx[1] = SIZE_MBX_SET_DMA_TX_MODE; //size

	memcpy(&mbx[2], (u8 *)&mode, sizeof(mode));
	memcpy(&mbx[6], (u8 *)&channel, sizeof(channel));
	memcpy(&mbx[10], (u8 *)&fifosz, sizeof(fifosz));
	mbx[14] = qmode;

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);

	/* Validation of successfull message posting can be done
	 * by reading the mbx buffer for ACK opcode (0xFF)
	 */

	if (ret > 0)
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n", ret,
								mbx[0], mbx[4]);
	else
		KPRINT_DEBUG2("mailbox write failed");
}

/**
 * tc956xmac_mbx_set_mtl_tx_queue_weight
 *
 * \brief API to set mtl tx queue weight
 *
 * \details This function is used to send parameters of mtl queue weight to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] weight - weight
 * \param[in] traffic_class - traffic_class no
 *
 * \return None
 */
static void tc956xmac_mbx_set_mtl_tx_queue_weight(struct tc956xmac_priv *priv,
				 u32 weight, u32 traffic_class)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return;
	}

	mbx[0] = OPCODE_MBX_SET_TX_Q_WEIGHT; //opcode
	mbx[1] = SIZE_MBX_SET_TX_Q_WEIGHT; //size

	memcpy(&mbx[2], (u8 *)&weight, sizeof(weight));
	memcpy(&mbx[6], (u8 *)&traffic_class, sizeof(traffic_class));

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);

	/* Validation of successfull message posting can be done
	 * by reading the mbx buffer for ACK opcode (0xFE)
	 */

	if (ret > 0)
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n", ret,
								mbx[0], mbx[4]);
	else
		KPRINT_DEBUG2("mailbox write failed");
}

/**
 * tc956xmac_mbx_config_cbs
 *
 * \brief API to configure the cbs parameters for queue
 *
 * \details This function is used to send cbs parameters to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] send_slope - send_slope
 * \param[in] idle_slope - idle_slope
 * \param[in] high_credit - high_credit
 * \param[in] low_credit - low_credit
 * \param[in] queue - queue
 *
 * \return None
 */
static void tc956xmac_mbx_config_cbs(struct tc956xmac_priv *priv,
				u32 send_slope, u32 idle_slope,
				u32 high_credit, u32 low_credit, u32 queue)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return;
	}

	mbx[0] = OPCODE_MBX_CFG_CBS; //opcode
	mbx[1] = SIZE_MBX_CFG_CBS; //size

	memcpy(&mbx[2], (u8 *)&send_slope, sizeof(send_slope));
	memcpy(&mbx[6], (u8 *)&idle_slope, sizeof(idle_slope));
	memcpy(&mbx[10], (u8 *)&high_credit, sizeof(high_credit));
	memcpy(&mbx[14], (u8 *)&low_credit, sizeof(low_credit));
	memcpy(&mbx[18], (u8 *)&queue, sizeof(queue));

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	/* Validation of successfull message posting can be done
	 * by reading the mbx buffer for ACK opcode (0xFE)
	 */

	if (ret > 0)
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n", ret,
								mbx[0], mbx[4]);
	else
		KPRINT_DEBUG2("mailbox write failed");
}

/**
 * tc956xmac_mbx_tx_queue_prio
 *
 * \brief API to set tx queue priority
 *
 * \details This function is used to send tx queue priority values to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] prio - priority
 * \param[in] queue - queue
 *
 * \return None
 */
static void tc956xmac_mbx_tx_queue_prio(struct tc956xmac_priv *priv,
				    u32 prio, u32 queue)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return;
	}

	mbx[0] = OPCODE_MBX_SET_TX_Q_PRIOR; //opcode
	mbx[1] = SIZE_MBX_SET_TX_Q_PRIOR; //size

	memcpy(&mbx[2], (u8 *)&prio, sizeof(prio));
	memcpy(&mbx[6], (u8 *)&queue, sizeof(queue));

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);

	/* Validation of successfull message posting can be done
	 * by reading the mbx buffer for ACK opcode (0xFE)
	 */

	if (ret > 0)
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n",
								ret, mbx[0], mbx[4]);
	else
		KPRINT_DEBUG2("mailbox write failed");
}

/**
 * tc956xmac_mbx_get_link_status
 *
 * \brief API to get pf link status parametrs
 *
 * \details This function is used to get link status from PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[out] link_status - link_status
 * \param[out] speed - speed
 * \param[out] duplex - duplex
 *
 * \return None
 */
static void tc956xmac_mbx_get_link_status(struct tc956xmac_priv *priv,
						u32 *link_status, u32 *speed, u32 *duplex)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL || link_status == NULL || speed == NULL || duplex == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return;
	}

	mbx[0] = OPCODE_MBX_VF_GET_LINK_STATUS; //opcode
	mbx[1] = 0; //size

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);

	/* Validation of successfull message posting can be done
	 * by reading the mbx buffer for ACK opcode (0xFF)
	 */

	if (ret > 0) {
		if (ret == ACK) {
			/* Check the acknowledgement message for opcode and size,
			 * then read the data from the ACK message
			 */
			if ((mbx[4] == OPCODE_MBX_ACK_MSG) && (mbx[5] == SIZE_MBX_PHY_LINK)) {
				memcpy(link_status, &mbx[6], 4);
				memcpy(speed, &mbx[10], 4);
				memcpy(duplex, &mbx[14], 4);
			}
		}
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n", ret, mbx[0], mbx[4]);
	} else {
		KPRINT_DEBUG2("mailbox write failed");
	}
}

/**
 * tc956xmac_mbx_get_umac_addr
 *
 * \brief API to get mac address
 *
 * \details This function is used to get mac address from PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] reg_n - register index
 * \param[out] addr - mac address
 *
 * \return None
 */
static void tc956xmac_mbx_get_umac_addr(struct tc956xmac_priv *priv,
				unsigned char *addr, unsigned int reg_n)
{
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL || addr == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return;
	}

	mbx[0] = OPCODE_MBX_GET_UMAC_ADDR;
	mbx[1] = SIZE_GET_UMAC_ADDR;

	memcpy(&mbx[2], &reg_n, SIZE_GET_UMAC_ADDR);
	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0) {
		if (ret == ACK) {
			if ((mbx[4] == OPCODE_MBX_ACK_MSG) && (mbx[5] == SIZE_MBX_VF_MAC)) {
				memcpy(addr, &mbx[6], SIZE_MBX_VF_MAC);
			}
		}
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n"
					, ret, mbx[0], mbx[4]);
	} else
		KPRINT_DEBUG2("mailbox write failed");
}

/**
 * tc956xmac_mbx_set_umac_addr
 *
 * \brief API to add mac address to register
 *
 * \details This function is used to send mac address to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] reg_n - register index
 * \param[in] addr - mac address
 *
 * \return error/success
 */
static int tc956xmac_mbx_set_umac_addr(struct tc956xmac_priv *priv, unsigned char *addr, unsigned int reg_n)
{
	u8 mbx[MBX_TOT_SIZE];
	int ret = 0;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL || addr == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	mbx[0] = OPCODE_MBX_ADD_MAC_ADDR;
	mbx[1] = SIZE_SET_UMAC_ADDR;

	memcpy(&mbx[2], addr, SIZE_MBX_VF_MAC);
	memcpy(&mbx[8], &reg_n, 4);

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0) {
	KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x, %x\n"
					, ret, mbx[0], mbx[4]);
	} else
		KPRINT_DEBUG2("mailbox write failed");

	return ret;
}

/**
 * tc956xmac_vf_ioctl_reg_wr
 *
 * \brief API to write value to register
 *
 * \details This function is used to send register value to set in PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] data - input adata from ioctl call
 *
 * \return success/error
 */
static int tc956xmac_vf_ioctl_reg_wr(struct tc956xmac_priv *priv,
							void __user *data)
{
	struct tc956xmac_ioctl_reg_rd_wr ioctl_data;
	u32 val;
	u8 mbx[MBX_TOT_SIZE];
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;
	int ret;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (priv == NULL || data == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	if (copy_from_user(&val, ioctl_data.ptr, sizeof(unsigned int)))
		return -EFAULT;

	mbx[0] = OPCODE_MBX_VF_IOCTL; /*opcode for ioctl*/
	mbx[1] = SIZE_MBX_VF_REG_WR; /*size*/
	mbx[2] = ioctl_data.cmd;
	memcpy(&mbx[3], (u8 *)&ioctl_data.bar_num, sizeof(ioctl_data.bar_num));
	memcpy(&mbx[7], (u8 *)&ioctl_data.addr, sizeof(ioctl_data.addr));
	memcpy(&mbx[11], (u8 *)&val, sizeof(val));

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0)
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n",
		ret, mbx[0], mbx[4]);
	else {
		KPRINT_DEBUG2("mailbox write failed");
		return ret;
	}

	return 0;
}

/**
 * tc956xmac_vf_ioctl_get_connected_speed
 *
 * \brief API to get link speed
 *
 * \details This function is used to get link speed from PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[out] data - data to ioctl call
 *
 * \return success/error
 */
static int tc956xmac_vf_ioctl_get_connected_speed(struct tc956xmac_priv *priv,
									void __user *data)
{
	struct tc956xmac_ioctl_speed ioctl_data;
	u8 mbx[MBX_TOT_SIZE];
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;
	int ret;

	memset(&ioctl_data, 0, sizeof(ioctl_data));

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (priv == NULL || data == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		return -EFAULT;

	mbx[0] = OPCODE_MBX_VF_IOCTL; /*opcode for ioctl*/
	mbx[1] = SIZE_MBX_VF_SPEED; /*size*/
	mbx[2] = ioctl_data.cmd;/*cmd for get_speed*/


	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);

	/* Validation of successfull message posting can be done
	* by reading the mbx buffer for ACK opcode (0xFE)
	*/
	if (ret > 0) {
		if (ret == ACK) {
			if ((mbx[4] == OPCODE_MBX_ACK_MSG) && (mbx[5] == sizeof(ioctl_data.connected_speed)))
				memcpy(&ioctl_data.connected_speed, &mbx[6], sizeof(ioctl_data.connected_speed));
		}
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n", ret, mbx[0], mbx[4]);
	} else {
		KPRINT_DEBUG2("mailbox write failed");
		return ret;
	}

	if (copy_to_user(data, &ioctl_data, sizeof(ioctl_data)))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);
	return 0;
}

/**
 * tc956xmac_vf_ioctl_set_cbs
 *
 * \brief API to set cbs values for queue
 *
 * \details This function is used to set cbs values for queue to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] data - input data from ioctl call
 *
 * \return success/error
 */
static int tc956xmac_vf_ioctl_set_cbs(struct tc956xmac_priv *priv,
						void __user *data)
{
	u8 mbx[MBX_TOT_SIZE];
	u8 mbx_loc[MBX_TOT_SIZE * 2], seq = 0, ack = 0;
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	u32 tx_qcount = priv->plat->tx_queues_to_use;
	struct tc956xmac_ioctl_cbs_cfg cbs;
	u8 qmode;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (priv == NULL || data == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	if (copy_from_user(&cbs, data, sizeof(cbs)))
		return -EFAULT;

	if ((cbs.queue_idx >= tx_qcount) || (cbs.queue_idx == 0))
		return -EINVAL;

	if (!priv->hw->mac->config_cbs)
		return -EINVAL;

	qmode = priv->plat->tx_queues_cfg[cbs.queue_idx].mode_to_use;

	if (qmode != MTL_QUEUE_AVB)
		return -EINVAL;

	memcpy(&mbx_loc[0], (u8 *)&cbs.speed100cfg.send_slope, sizeof(cbs.speed100cfg.send_slope));
	memcpy(&mbx_loc[4], (u8 *)&cbs.speed100cfg.idle_slope, sizeof(cbs.speed100cfg.idle_slope));
	memcpy(&mbx_loc[8], (u8 *)&cbs.speed100cfg.high_credit, sizeof(cbs.speed100cfg.high_credit));
	memcpy(&mbx_loc[12], (u8 *)&cbs.speed100cfg.low_credit, sizeof(cbs.speed100cfg.low_credit));
	memcpy(&mbx_loc[16], (u8 *)&cbs.speed100cfg.percentage, sizeof(cbs.speed100cfg.percentage));

	memcpy(&mbx_loc[20], (u8 *)&cbs.speed1000cfg.send_slope, sizeof(cbs.speed1000cfg.send_slope));
	memcpy(&mbx_loc[24], (u8 *)&cbs.speed1000cfg.idle_slope, sizeof(cbs.speed1000cfg.idle_slope));
	memcpy(&mbx_loc[28], (u8 *)&cbs.speed1000cfg.high_credit, sizeof(cbs.speed1000cfg.high_credit));
	memcpy(&mbx_loc[32], (u8 *)&cbs.speed1000cfg.low_credit, sizeof(cbs.speed1000cfg.low_credit));
	memcpy(&mbx_loc[36], (u8 *)&cbs.speed1000cfg.percentage, sizeof(cbs.speed1000cfg.percentage));

	memcpy(&mbx_loc[40], (u8 *)&cbs.speed10000cfg.send_slope, sizeof(cbs.speed10000cfg.send_slope));
	memcpy(&mbx_loc[44], (u8 *)&cbs.speed10000cfg.idle_slope, sizeof(cbs.speed10000cfg.idle_slope));
	memcpy(&mbx_loc[48], (u8 *)&cbs.speed10000cfg.high_credit, sizeof(cbs.speed10000cfg.high_credit));
	memcpy(&mbx_loc[52], (u8 *)&cbs.speed10000cfg.low_credit, sizeof(cbs.speed10000cfg.low_credit));
	memcpy(&mbx_loc[56], (u8 *)&cbs.speed10000cfg.percentage, sizeof(cbs.speed10000cfg.percentage));

	memcpy(&mbx_loc[60], (u8 *)&cbs.speed5000cfg.send_slope, sizeof(cbs.speed5000cfg.send_slope));
	memcpy(&mbx_loc[64], (u8 *)&cbs.speed5000cfg.idle_slope, sizeof(cbs.speed5000cfg.idle_slope));
	memcpy(&mbx_loc[68], (u8 *)&cbs.speed5000cfg.high_credit, sizeof(cbs.speed5000cfg.high_credit));
	memcpy(&mbx_loc[72], (u8 *)&cbs.speed5000cfg.low_credit, sizeof(cbs.speed5000cfg.low_credit));
	memcpy(&mbx_loc[76], (u8 *)&cbs.speed5000cfg.percentage, sizeof(cbs.speed5000cfg.percentage));

	memcpy(&mbx_loc[80], (u8 *)&cbs.speed2500cfg.send_slope, sizeof(cbs.speed2500cfg.send_slope));
	memcpy(&mbx_loc[84], (u8 *)&cbs.speed2500cfg.idle_slope, sizeof(cbs.speed2500cfg.idle_slope));
	memcpy(&mbx_loc[88], (u8 *)&cbs.speed2500cfg.high_credit, sizeof(cbs.speed2500cfg.high_credit));
	memcpy(&mbx_loc[92], (u8 *)&cbs.speed2500cfg.low_credit, sizeof(cbs.speed2500cfg.low_credit));
	memcpy(&mbx_loc[96], (u8 *)&cbs.speed2500cfg.percentage, sizeof(cbs.speed2500cfg.percentage));

	mbx[0] = OPCODE_MBX_VF_IOCTL;
	mbx[1] = SIZE_MBX_SET_GET_CBS_1;
	mbx[2] = TC956XMAC_SET_CBS_1;
	mbx[3] = cbs.queue_idx;

	memcpy(&mbx[4], &mbx_loc[0], SIZE_MBX_SET_GET_CBS_1);
	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0) {
		ack |= 1 << seq++;
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n",
		ret, mbx[0], mbx[4]);
	} else
		KPRINT_DEBUG2("mailbox write failed");

	mbx[0] = OPCODE_MBX_VF_IOCTL;
	mbx[1] = SIZE_MBX_SET_GET_CBS_2;
	mbx[2] = TC956XMAC_SET_CBS_2;
	mbx[3] = cbs.queue_idx;

	memcpy(&mbx[4], &mbx_loc[SIZE_MBX_SET_GET_CBS_1], SIZE_MBX_SET_GET_CBS_2);
	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0) {
		ack |= 1 << seq;
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n",
		ret, mbx[0], mbx[4]);
	} else
		KPRINT_DEBUG2("mailbox write failed");

	if (ack == 0x3) {
		return 0;
	} else
		return -EFAULT;
}

/**
 * tc956xmac_vf_ioctl_get_cbs
 *
 * \brief API to get cbs values for queue
 *
 * \details This function is used to get cbs values for queue from PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[out] data - data to ioctl call
 *
 * \return success/error
 */
static int tc956xmac_vf_ioctl_get_cbs(struct tc956xmac_priv *priv,
								void __user *data)
{
	/* Prepare mailbox message and call mailbox API for posting
	* and getting the ACK
	*/
	u8 mbx[MBX_TOT_SIZE];
	u8 mbx_loc[MBX_TOT_SIZE * 2], seq = 0, ack = 0;

	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	u32 tx_qcount = priv->plat->tx_queues_to_use;
	struct tc956xmac_ioctl_cbs_cfg cbs;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (priv == NULL || data == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	if (copy_from_user(&cbs, data, sizeof(cbs)))
		return -EFAULT;
	if (cbs.queue_idx >= tx_qcount)
		return -EINVAL;

	mbx[0] = OPCODE_MBX_VF_IOCTL;
	mbx[1] = SIZE_MBX_VF_GET_CBS;
	mbx[2] = TC956XMAC_GET_CBS_1;
	mbx[3] = cbs.queue_idx;

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0) {
		if (ret == ACK) {
			if ((mbx[4] == OPCODE_MBX_ACK_MSG) && (mbx[5] == SIZE_MBX_SET_GET_CBS_1)) {
				ack |= 1 << seq++;
				memcpy(&mbx_loc[0], &mbx[6], SIZE_MBX_SET_GET_CBS_1);
			}
		}
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d  msgbuff %x %x\n", ret, mbx[0], mbx[4]);
	} else
		KPRINT_DEBUG2("mailbox write failed");

	mbx[0] = OPCODE_MBX_VF_IOCTL;
	mbx[1] = SIZE_MBX_VF_GET_CBS;
	mbx[2] = TC956XMAC_GET_CBS_2;
	mbx[3] = cbs.queue_idx;

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0) {
		if (ret == ACK) {
			if ((mbx[4] == OPCODE_MBX_ACK_MSG) && (mbx[5] == SIZE_MBX_SET_GET_CBS_2)) {
				ack |= 1 << seq;
				memcpy(&mbx_loc[SIZE_MBX_SET_GET_CBS_1], &mbx[6], SIZE_MBX_SET_GET_CBS_2);
			}
		}
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d  msgbuff %x %x\n", ret, mbx[0], mbx[4]);
	} else
		KPRINT_DEBUG2("mailbox write failed");

	if (ack == 0x3) {
		memcpy(&cbs.speed100cfg.send_slope, &mbx_loc[0], sizeof(cbs.speed100cfg.send_slope));
		memcpy(&cbs.speed100cfg.idle_slope, &mbx_loc[4], sizeof(cbs.speed100cfg.idle_slope));
		memcpy(&cbs.speed100cfg.high_credit, &mbx_loc[8], sizeof(cbs.speed100cfg.high_credit));
		memcpy(&cbs.speed100cfg.low_credit, &mbx_loc[12], sizeof(cbs.speed100cfg.low_credit));
		memcpy(&cbs.speed100cfg.percentage, &mbx_loc[16], sizeof(cbs.speed100cfg.percentage));

		memcpy(&cbs.speed1000cfg.send_slope, &mbx_loc[20], sizeof(cbs.speed1000cfg.send_slope));
		memcpy(&cbs.speed1000cfg.idle_slope, &mbx_loc[24], sizeof(cbs.speed1000cfg.idle_slope));
		memcpy(&cbs.speed1000cfg.high_credit, &mbx_loc[28], sizeof(cbs.speed1000cfg.high_credit));
		memcpy(&cbs.speed1000cfg.low_credit, &mbx_loc[32], sizeof(cbs.speed1000cfg.low_credit));
		memcpy(&cbs.speed1000cfg.percentage, &mbx_loc[36], sizeof(cbs.speed1000cfg.percentage));

		memcpy(&cbs.speed10000cfg.send_slope, &mbx_loc[40], sizeof(cbs.speed10000cfg.send_slope));
		memcpy(&cbs.speed10000cfg.idle_slope, &mbx_loc[44], sizeof(cbs.speed10000cfg.idle_slope));
		memcpy(&cbs.speed10000cfg.high_credit, &mbx_loc[48], sizeof(cbs.speed10000cfg.high_credit));
		memcpy(&cbs.speed10000cfg.low_credit, &mbx_loc[52], sizeof(cbs.speed10000cfg.low_credit));
		memcpy(&cbs.speed10000cfg.percentage, &mbx_loc[56], sizeof(cbs.speed10000cfg.percentage));

		memcpy(&cbs.speed5000cfg.send_slope, &mbx_loc[60], sizeof(cbs.speed5000cfg.send_slope));
		memcpy(&cbs.speed5000cfg.idle_slope, &mbx_loc[64], sizeof(cbs.speed5000cfg.idle_slope));
		memcpy(&cbs.speed5000cfg.high_credit, &mbx_loc[68], sizeof(cbs.speed5000cfg.high_credit));
		memcpy(&cbs.speed5000cfg.low_credit, &mbx_loc[72], sizeof(cbs.speed5000cfg.low_credit));
		memcpy(&cbs.speed5000cfg.percentage, &mbx_loc[76], sizeof(cbs.speed5000cfg.percentage));

		memcpy(&cbs.speed2500cfg.send_slope, &mbx_loc[80], sizeof(cbs.speed2500cfg.send_slope));
		memcpy(&cbs.speed2500cfg.idle_slope, &mbx_loc[84], sizeof(cbs.speed2500cfg.idle_slope));
		memcpy(&cbs.speed2500cfg.high_credit, &mbx_loc[88], sizeof(cbs.speed2500cfg.high_credit));
		memcpy(&cbs.speed2500cfg.low_credit, &mbx_loc[92], sizeof(cbs.speed2500cfg.low_credit));
		memcpy(&cbs.speed2500cfg.percentage, &mbx_loc[96], sizeof(cbs.speed2500cfg.percentage));
	} else
		return -EFAULT;

	if (copy_to_user(data, &cbs, sizeof(cbs)))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

/**
 * tc956xmac_vf_ioctl_set_est
 *
 * \brief API to add est entries
 *
 * \details This function is used to send add est entries to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] data - input data from ioctl call
 *
 * \return success/error
 */
static int tc956xmac_vf_ioctl_set_est(struct tc956xmac_priv *priv,
						void __user *data)
{
	u8 mbx[MBX_TOT_SIZE];
	u8 mbx_loc[MBX_MSG_SIZE * 10];
	int ret = 0, offset = 1, total_size;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;
	u32 opcode, rem_gcl_cnt, msg_size;
	struct tc956xmac_ioctl_est_cfg *est;

	if (!priv->plat->tsn_application)
		return -EFAULT;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (priv == NULL || data == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	est = kzalloc(sizeof(*est), GFP_KERNEL);
	if (!est)
		return -ENOMEM;

	if (copy_from_user(est, data, sizeof(struct tc956xmac_ioctl_est_cfg))) {
		ret = -EINVAL;
		goto out_free;
	}

	if (est->gcl_size > TC956XMAC_IOCTL_EST_GCL_MAX_ENTRIES) {
		ret = -EINVAL;
		goto out_free;
	}

	if (est->enabled) {
		memcpy(&mbx_loc[0], (u8 *)&est->enabled, sizeof(est->enabled));
		memcpy(&mbx_loc[4], (u8 *)&est->btr_offset[0], sizeof(est->btr_offset));
		memcpy(&mbx_loc[12], (u8 *)&est->ctr[0], sizeof(est->ctr));
		memcpy(&mbx_loc[20], (u8 *)&est->ter, sizeof(est->ter));
		memcpy(&mbx_loc[24], (u8 *)&est->gcl_size, sizeof(est->gcl_size));
		memcpy(&mbx_loc[28], (u8 *)&est->gcl, (est->gcl_size * sizeof(*est->gcl)));

		total_size = (est->gcl_size * sizeof(*est->gcl)) + EST_FIX_MSG_LEN;
		msg_size = est->gcl_size >= ((SIZE_MBX_SET_GET_EST_1 - EST_FIX_MSG_LEN) / sizeof(est->gcl_size)) ?
								SIZE_MBX_SET_GET_EST_1 : ((est->gcl_size * sizeof(*est->gcl)) + EST_FIX_MSG_LEN);

		mbx[0] = OPCODE_MBX_VF_IOCTL;
		mbx[1] = msg_size;
		mbx[2] = TC956XMAC_SET_EST_1;
		memcpy(&mbx[3], &mbx_loc[0], mbx[1]);

		ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
		if (ret > 0) {
			KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n",
			ret, mbx[0], mbx[4]);
		} else {
			KPRINT_DEBUG2("mailbox write failed");
			goto out_free;
		}

		opcode = TC956XMAC_SET_EST_1;
		total_size -= msg_size;
		rem_gcl_cnt = est->gcl_size > ((SIZE_MBX_SET_GET_EST_1 - EST_FIX_MSG_LEN) / sizeof(est->gcl_size)) ?
						(est->gcl_size - ((SIZE_MBX_SET_GET_EST_1 - EST_FIX_MSG_LEN) / sizeof(est->gcl_size))) : 0;

		while (total_size > 0) {
			msg_size = rem_gcl_cnt >= MAX_SIZE_GCL_MSG ? SIZE_MBX_SET_GET_EST_1 : (rem_gcl_cnt * sizeof(*est->gcl));
			opcode += 1;
			mbx[0] = OPCODE_MBX_VF_IOCTL;
			mbx[1] = msg_size;
			mbx[2] = opcode;

			memcpy(&mbx[3], &mbx_loc[offset * SIZE_MBX_SET_GET_EST_1], msg_size);
			ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
			if (ret > 0) {
				KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n",
				ret, mbx[0], mbx[4]);
			} else {
				KPRINT_DEBUG2("mailbox write failed");
				goto out_free;
			}

			total_size -= msg_size;
			rem_gcl_cnt = rem_gcl_cnt > MAX_SIZE_GCL_MSG ?  (rem_gcl_cnt - MAX_SIZE_GCL_MSG) : 0;
			offset++;
		}
		ret = 0;
	}

out_free:
	kfree(est);

	return ret;
}

/**
 * tc956xmac_vf_ioctl_get_est
 *
 * \brief API to get est entries
 *
 * \details This function is used to get est entries from PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[out] data - data to ioctl call
 *
 * \return success/error
 */
static int tc956xmac_vf_ioctl_get_est(struct tc956xmac_priv *priv,	void __user *data)
{
	u8 mbx[MBX_TOT_SIZE];
	u8 mbx_loc[MBX_MSG_SIZE * 10];
	u16 msg_size, total_msg_size = 0;
	int ret, offset = 0;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;
	u32 gcl_size = 0, opcode;
	struct tc956xmac_ioctl_est_cfg *est;

	if (!priv->plat->tsn_application)
		return -EFAULT;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (priv == NULL || data == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	est = kzalloc(sizeof(*est), GFP_KERNEL);
	if (!est)
		return -ENOMEM;

	opcode = TC956XMAC_GET_EST_1;

	do {
		mbx[0] = OPCODE_MBX_VF_IOCTL;
		mbx[1] = SIZE_MBX_VF_GET_EST;
		mbx[2] = opcode++;

		ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
		if (ret > 0) {
			if (ret == ACK) {
				if (mbx[4] == OPCODE_MBX_ACK_MSG) {
					msg_size = mbx[5];
					memcpy(&mbx_loc[offset], &mbx[6], msg_size);
					offset += msg_size;
					if ((opcode - 1) == TC956XMAC_GET_EST_1) {
						memcpy(&est->enabled, &mbx_loc[0], sizeof(est->enabled));
						memcpy(&est->estwid, &mbx_loc[4], sizeof(est->estwid));
						memcpy(&est->estdep, &mbx_loc[8], sizeof(est->estdep));
						memcpy(&est->btr_offset[0], &mbx_loc[12], sizeof(est->btr_offset));
						memcpy(&est->ctr[0], &mbx_loc[20], sizeof(est->ctr));
						memcpy(&est->ter, &mbx_loc[28], sizeof(est->ter));
						memcpy(&est->gcl_size, &mbx_loc[32], sizeof(est->gcl_size));

						gcl_size = est->gcl_size * sizeof(*est->gcl);
						total_msg_size = gcl_size + EST_FIX_MSG_LEN + 8; //8 due to estwid and estdep
					}
				}
			}
			KPRINT_DEBUG2("mailbox write with ACK or NACK %d  msgbuff %x %x\n", ret, mbx[0], mbx[4]);
		} else {
			KPRINT_DEBUG2("mailbox write failed");
			goto out_free;
		}
	} while (offset != total_msg_size);

	memcpy(&est->gcl, &mbx_loc[36], est->gcl_size * sizeof(*est->gcl));
	ret = 0;

	if (copy_to_user(data, est, sizeof(struct tc956xmac_ioctl_est_cfg))) {
		goto out_free;
		ret = -EFAULT;
	}

out_free:
	kfree(est);

	return ret;
}

/**
 * tc956xmac_vf_ioctl_set_fpe
 *
 * \brief API to set fpe param
 *
 * \details This function is used to send fpe param to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] data - input data from ioctl call
 *
 * \return success/error
 */
static int tc956xmac_vf_ioctl_set_fpe(struct tc956xmac_priv *priv, void __user *data)
{
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	struct tc956xmac_ioctl_fpe_cfg fpe;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (priv == NULL || data == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	if (copy_from_user(&fpe, data, sizeof(struct tc956xmac_ioctl_fpe_cfg)))
		return -EFAULT;

	mbx[0] = OPCODE_MBX_VF_IOCTL;
	mbx[1] = SIZE_MBX_SET_GET_FPE_1;
	mbx[2] = TC956XMAC_SET_FPE_1;

	memcpy(&mbx[3], (u8 *)&fpe.enabled, sizeof(fpe.enabled));
	memcpy(&mbx[7], (u8 *)&fpe.pec, sizeof(fpe.pec));
	memcpy(&mbx[11], (u8 *)&fpe.afsz, sizeof(fpe.afsz));
	memcpy(&mbx[15], (u8 *)&fpe.RA_time, sizeof(fpe.RA_time));
	memcpy(&mbx[19], (u8 *)&fpe.HA_time, sizeof(fpe.RA_time));

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0) {
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n",
		ret, mbx[0], mbx[4]);
	} else {
		KPRINT_DEBUG2("mailbox write failed");
		return -EFAULT;
	}

	return 0;
}

/**
 * tc956xmac_vf_ioctl_get_fpe
 *
 * \brief API to get fpe parametrs
 *
 * \details This function is used to get fpe param from PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[out] data - data to ioctl call
 *
 * \return success/error
 */
static int tc956xmac_vf_ioctl_get_fpe(struct tc956xmac_priv *priv, void __user *data)
{
	/* Prepare mailbox message and call mailbox API for posting
	* and getting the ACK
	*/
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;
	struct tc956xmac_ioctl_fpe_cfg fpe;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (priv == NULL || data == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	mbx[0] = OPCODE_MBX_VF_IOCTL;
	mbx[1] = SIZE_MBX_VF_GET_FPE;
	mbx[2] = TC956XMAC_GET_FPE_1;

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0) {
		if (ret == ACK) {
			if ((mbx[4] == OPCODE_MBX_ACK_MSG) && (mbx[5] == SIZE_MBX_SET_GET_FPE_1)) {
				memcpy(&fpe.enabled, &mbx[6], sizeof(fpe.enabled));
				memcpy(&fpe.pec, &mbx[10], sizeof(fpe.pec));
				memcpy(&fpe.afsz, &mbx[14], sizeof(fpe.afsz));
				memcpy(&fpe.RA_time, &mbx[18], sizeof(fpe.RA_time));
				memcpy(&fpe.HA_time, &mbx[22], sizeof(fpe.HA_time));
			}
		}
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d  msgbuff %x %x\n", ret, mbx[0], mbx[4]);
	} else {
		KPRINT_DEBUG2("mailbox write failed");
		return -EFAULT;
	}

	if (copy_to_user(data, &fpe, sizeof(struct tc956xmac_ioctl_fpe_cfg)))
		return -EFAULT;

	DBGPR_FUNC(priv->device, "<--%s\n", __func__);

	return 0;
}

/**
 * tc956xmac_vf_ioctl_set_rxp
 *
 * \brief API to set frp entries
 *
 * \details This function is used to send frp entries to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] data - input data from ioctl call
 *
 * \return success/error
 */
static int tc956xmac_vf_ioctl_set_rxp(struct tc956xmac_priv *priv, void __user *data)
{
	u8 mbx[MBX_TOT_SIZE];
	u8 *mbx_loc;   //sizeof(struct tc956xmac_ioctl_rxp_cfg)
	int ret = 0, offset = 1, total_size;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;
	u32 opcode, msg_size;
	struct tc956xmac_ioctl_rxp_cfg *rxp;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (priv == NULL || data == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	rxp = kzalloc(sizeof(*rxp), GFP_KERNEL);
	if (!rxp)
		return -ENOMEM;

	mbx_loc = kzalloc(sizeof(*rxp), GFP_KERNEL);
	if (!mbx_loc) {
		ret = -ENOMEM;
		goto out_free;
	}

	if (copy_from_user(rxp, data, sizeof(struct tc956xmac_ioctl_rxp_cfg))) {
		return -EINVAL;
	}

	if (rxp->nve > TC956XMAC_RX_PARSER_MAX_ENTRIES) {
		return -EINVAL;
	}

	if (rxp->enabled) {
		memcpy(&mbx_loc[0], (u8 *)&rxp->frpes, sizeof(rxp->frpes));
		memcpy(&mbx_loc[4], (u8 *)&rxp->enabled, sizeof(rxp->enabled));
		memcpy(&mbx_loc[8], (u8 *)&rxp->nve, sizeof(rxp->nve));
		memcpy(&mbx_loc[12], (u8 *)&rxp->npe, sizeof(rxp->npe));
		memcpy(&mbx_loc[16], (u8 *)&rxp->entries, (rxp->nve * sizeof(*rxp->entries)));

		total_size = (rxp->nve * sizeof(*rxp->entries)) + RXP_FIX_MSG_LEN;
		msg_size = total_size >= SIZE_MBX_SET_GET_RXP_1 ? SIZE_MBX_SET_GET_RXP_1 : total_size;

		mbx[0] = OPCODE_MBX_VF_IOCTL;
		mbx[1] = msg_size;
		mbx[2] = TC956XMAC_SET_RXP_1;

		memcpy(&mbx[3], &mbx_loc[0], mbx[1]);

		ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
		if (ret > 0) {
			KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n",
			ret, mbx[0], mbx[4]);
		} else {
			KPRINT_DEBUG2("mailbox write failed");
			goto out_free;
		}

		opcode = TC956XMAC_SET_RXP_1;
		total_size -= msg_size;

		while (total_size > 0) {
			msg_size = total_size >= SIZE_MBX_SET_GET_RXP_1 ? SIZE_MBX_SET_GET_RXP_1 : total_size;
			opcode += 1;
			mbx[0] = OPCODE_MBX_VF_IOCTL;
			mbx[1] = msg_size;
			mbx[2] = opcode;

			memcpy(&mbx[3], &mbx_loc[offset * SIZE_MBX_SET_GET_RXP_1], msg_size);
			ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
			if (ret > 0) {
				KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n",
				ret, mbx[0], mbx[4]);
			} else {
				KPRINT_DEBUG2("mailbox write failed");
				goto out_free;
			}

			total_size -= msg_size;
			offset++;
		}
		ret = 0;
	}

out_free:
	kfree(rxp);
	kfree(mbx_loc);
	return ret;
}

/**
 * tc956xmac_vf_ioctl_get_rxp
 *
 * \brief API to get frp entries
 *
 * \details This function is used to get frp entries from PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[out] data - data to ioctl call
 *
 * \return success/error
 */
static int tc956xmac_vf_ioctl_get_rxp(struct tc956xmac_priv *priv,	void __user *data)
{
	u8 mbx[MBX_TOT_SIZE];
	u8 *mbx_loc;
	u16 msg_size, total_msg_size = 0;
	int ret, offset = 0;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;
	u32 rxp_size = 0, opcode;
	struct tc956xmac_ioctl_rxp_cfg *rxp;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (priv == NULL || data == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	rxp = kzalloc(sizeof(*rxp), GFP_KERNEL);
	if (!rxp)
		return -ENOMEM;

	mbx_loc = kzalloc(sizeof(*rxp), GFP_KERNEL);
	if (!mbx_loc) {
		ret = -ENOMEM;
		goto out_free;
	}
	opcode = TC956XMAC_GET_RXP_1;

	do {
		mbx[0] = OPCODE_MBX_VF_IOCTL;
		mbx[1] = SIZE_MBX_VF_GET_RXP;
		mbx[2] = opcode++;

		ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
		if (ret > 0) {
			if (ret == ACK) {
				if (mbx[4] == OPCODE_MBX_ACK_MSG) {
					msg_size = mbx[5];
					memcpy(&mbx_loc[offset], &mbx[6], msg_size);
					offset += msg_size;
					if ((opcode - 1) == TC956XMAC_GET_RXP_1) {
						memcpy(&rxp->frpes, &mbx_loc[0], sizeof(rxp->frpes));
						memcpy(&rxp->enabled, &mbx_loc[4], sizeof(rxp->enabled));
						memcpy(&rxp->nve, &mbx_loc[8], sizeof(rxp->nve));
						memcpy(&rxp->npe, &mbx_loc[12], sizeof(rxp->npe));
						rxp_size = rxp->nve * sizeof(*rxp->entries);
						total_msg_size = rxp_size + RXP_FIX_MSG_LEN;
					}
				}
			}
			KPRINT_DEBUG2("mailbox write with ACK or NACK %d  msgbuff %x %x\n", ret, mbx[0], mbx[4]);
		} else {
			KPRINT_DEBUG2("mailbox write failed");
			goto out_free;
		}
	} while (offset != total_msg_size);

	memcpy(&rxp->entries[0], &mbx_loc[16], rxp->nve * sizeof(*rxp->entries));
	ret = 0;

	if (copy_to_user(data, rxp, sizeof(struct tc956xmac_ioctl_rxp_cfg))) {
		goto out_free;
		ret = -EFAULT;
	}

out_free:
	kfree(rxp);
	kfree(mbx_loc);
	return ret;
}

/**
 * tc956xmac_vf_ethtool_get_pauseparam
 *
 * \brief API to get pauseparameters
 *
 * \details This function is used to get ethtool pauseparam from PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[out] pause - pause parameters
 *
 * \return success/error
 */
static int tc956xmac_vf_ethtool_get_pauseparam(struct tc956xmac_priv *priv,
					struct ethtool_pauseparam *pause)
{
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (priv == NULL || pause == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	mbx[0] = OPCODE_MBX_VF_ETHTOOL;
	mbx[1] = SIZE_MBX_VF_PAUSE_PARAM;
	mbx[2] = OPCODE_MBX_VF_GET_PAUSE_PARAM;

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0) {
		if (ret == ACK) {
			mbx[0] = OPCODE_MBX_VF_ETHTOOL;
			mbx[1] = SIZE_MBX_VF_PAUSE_PARAM;
			mbx[2] = OPCODE_MBX_VF_GET_PAUSE_PARAM_2;

			ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
			if (ret > 0) {
				if (ret == ACK) {
					if ((mbx[4] == OPCODE_MBX_ACK_MSG) &&
						(mbx[5] == sizeof(struct ethtool_pauseparam))) {
						memcpy(pause, &mbx[6], sizeof(struct ethtool_pauseparam));
					}
				}
			} else {
				KPRINT_DEBUG2("mailbox write failed");
				return ret;
			}
		}
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d  msgbuff %x %x\n", ret, mbx[0], mbx[4]);
	} else {
		KPRINT_DEBUG2("mailbox write failed");
		return ret;
	}

	return 0;
}

/**
 * tc956xmac_vf_ethtool_get_eee
 *
 * \brief API to get eee parametrs
 *
 * \details This function is used to get ethtool eee param from PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[out] edata - edata parameters
 *
 * \return success/error
 */
static int tc956xmac_vf_ethtool_get_eee(struct tc956xmac_priv *priv,
				     struct ethtool_eee *edata)
{
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (priv == NULL || edata == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	mbx[0] = OPCODE_MBX_VF_ETHTOOL;
	mbx[1] = SIZE_MBX_VF_EEE;
	mbx[2] = OPCODE_MBX_VF_GET_EEE;

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0) {
		if (ret == ACK) {
			if ((mbx[4] == OPCODE_MBX_ACK_MSG) &&
				(mbx[5] == sizeof(struct ethtool_eee))) {
				memcpy(edata, &mbx[6], sizeof(struct ethtool_eee));
			}
		}
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n", ret, mbx[0], mbx[4]);
	} else {
		KPRINT_DEBUG2("mailbox write failed");
		return ret;
	}

	return 0;
}

/**
 * tc956xmac_vf_add_mac
 *
 * \brief API to add mac address
 *
 * \details This function is used to send mac address to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mac - mac address
 *
 * \return success/error
 */
static int tc956xmac_vf_add_mac(struct tc956xmac_priv *priv, const u8 *mac)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MBX_TOT_SIZE];
	int ret;

	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL || mac == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	mbx[0] = OPCODE_MBX_VF_ADD_MAC;
	mbx[1] = SIZE_MBX_VF_MAC;

	memcpy(&mbx[2], mac, SIZE_MBX_VF_MAC);

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0)
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n",
		ret, mbx[0], mbx[4]);
	else {
		KPRINT_DEBUG2("mailbox write failed");
		return ret;
	}

	return 0;
}

/**
 * tc956xmac_vf_delete_mac
 *
 * \brief API to delete mac address
 *
 * \details This function is used to send mac address to delete to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mac - mac address
 *
 * \return None
 */
static void tc956xmac_vf_delete_mac(struct tc956xmac_priv *priv, const u8 *mac)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MBX_TOT_SIZE];
	int ret;

	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL || mac == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return;
	}

	mbx[0] = OPCODE_MBX_VF_DELETE_MAC;
	mbx[1] = SIZE_MBX_VF_MAC;

	memcpy(&mbx[2], mac, SIZE_MBX_VF_MAC);

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0)
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n",
		ret, mbx[0], mbx[4]);
	else
		KPRINT_DEBUG2("mailbox write failed");
}

/**
 * tc956xmac_vf_add_vlan
 *
 * \brief API to add vlan id
 *
 * \details This function is used to send vlan id to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] vid - vlan id
 *
 * \return None
 */
static void tc956xmac_vf_add_vlan(struct tc956xmac_priv *priv, u16 vid)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MBX_TOT_SIZE];
	int ret;

	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return;
	}

	mbx[0] = OPCODE_MBX_VF_ADD_VLAN;
	mbx[1] = SIZE_MBX_VF_VLAN;

	memcpy(&mbx[2], (u8 *)&vid, SIZE_MBX_VF_VLAN);

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0)
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n",
		ret, mbx[0], mbx[4]);
	else
		KPRINT_DEBUG2("mailbox write failed");
}

/**
 * tc956xmac_vf_delete_vlan
 *
 * \brief API to delete vlan id
 *
 * \details This function is used to send vlan id to delete to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] vid - vlan id
 *
 * \return None
 */
static void tc956xmac_vf_delete_vlan(struct tc956xmac_priv *priv, u16 vid)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MBX_TOT_SIZE];
	int ret;

	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return;
	}

	mbx[0] = OPCODE_MBX_VF_DELETE_VLAN;
	mbx[1] = SIZE_MBX_VF_VLAN;

	memcpy(&mbx[2], (u8 *)&vid, SIZE_MBX_VF_VLAN);

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0)
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n",
		ret, mbx[0], mbx[4]);
	else
		KPRINT_DEBUG2("mailbox write failed");
}

/**
 * tc956x_mbx_get_drv_cap
 *
 * \brief API to receive EMAC features state from PF
 *
 * \details This function is used to receive states for Jumbo frames, TSO,
 * RX CRC and RC Checksum from PF using mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 *
 * \return None
 */
static void tc956x_mbx_get_drv_cap(struct tc956xmac_priv *priv, struct tc956xmac_priv *priv1)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return;
	}

	mbx[0] = OPCODE_MBX_DRV_CAP;
	mbx[1] = 0;

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);

	/* If ACK received, copy data*/
	if (ret > 0) {
		if (ret == ACK) {
			if ((mbx[4] == OPCODE_MBX_ACK_MSG) && (mbx[5] == SIZE_MBX_DRV_CAP)) {

			/* Copy PF drv capabiliites to priv*/
			    memcpy((u8 *)&priv->pf_drv_cap, &mbx[6],
							SIZE_MBX_DRV_CAP);
			}
		}
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n"
					, ret, mbx[0], mbx[4]);
	} else {
		priv->pf_drv_cap.jumbo_en = false;
		priv->pf_drv_cap.tso_en = false;
		priv->pf_drv_cap.crc_en = false;
		priv->pf_drv_cap.csum_en = false;
		KPRINT_DEBUG2("mailbox write failed");
	}
}

/**
 * tc956xmac_vf_mbx_reset_eee_mode
 *
 * \brief API to reset eee mode
 *
 * \details This function is used to param of eee reset to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] hw - pointer to device information structure
 *
 * \return None
 */
static void tc956xmac_vf_mbx_reset_eee_mode(struct tc956xmac_priv *priv, struct mac_device_info *hw)
{
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL || hw == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return;
	}

	mbx[0] = OPCODE_MBX_RESET_EEE_MODE;
	mbx[1] = 0;

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);

	if (ret > 0)
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n", ret,
								mbx[0], mbx[4]);
	else
		KPRINT_DEBUG2("mailbox write failed");
}

/**
 * tc956xmac_vf_mbx_reset
 *
 * \brief API to send VF status to PF
 *
 * \details This function is used to send VF status to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] vf_status - vf_status
 *
 * \return None
 */
static void tc956xmac_vf_mbx_reset(struct tc956xmac_priv *priv, u8 vf_status)
{
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return;
	}

	mbx[0] = OPCODE_VF_RESET;
	mbx[1] = SIZE_VF_RESET;
	mbx[2] = vf_status;

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);

	if (ret > 0)
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n", ret,
								mbx[0], mbx[4]);
	else
		KPRINT_DEBUG2("mailbox write failed");
}

/**
 * tc956x_mbx_setup_cbs
 *
 * \brief API to setup cbs param for queue using TC command
 *
 * \details This function is used to send VF status to PF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] qopt - pointer to tc_cbs_qopt_offload structure
 *
 * \return success/error
 */
static int tc956x_mbx_setup_cbs(struct tc956xmac_priv *priv, struct tc_cbs_qopt_offload *qopt)
{
	u8 mbx[MBX_TOT_SIZE];
	int ret;
	enum mbx_msg_fns msg_dst = priv->fn_id_info.pf_no + 1;

	if (priv == NULL || qopt == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return -EFAULT;
	}

	mbx[0] = OPCODE_MBX_SETUP_CBS; //opcode
	mbx[1] = SIZE_MBX_SETUP_CBS; //size

	memcpy(&mbx[2], (u8 *)&qopt->enable, sizeof(qopt->enable));
	memcpy(&mbx[3], (u8 *)&qopt->idleslope, sizeof(qopt->idleslope));
	memcpy(&mbx[7], (u8 *)&qopt->sendslope, sizeof(qopt->sendslope));
	memcpy(&mbx[11], (u8 *)&qopt->hicredit, sizeof(qopt->hicredit));
	memcpy(&mbx[15], (u8 *)&qopt->locredit, sizeof(qopt->locredit));
	memcpy(&mbx[19], (u8 *)&qopt->queue, sizeof(qopt->queue));

	if (priv->plat->ch_in_use[qopt->queue] == 0)
		return -EFAULT;

	ret = tc956xmac_mbx_write(priv, mbx, msg_dst, &priv->fn_id_info);
	if (ret > 0)
		KPRINT_DEBUG2("mailbox write with ACK or NACK %d msgbuff %x %x\n", ret,
								mbx[0], mbx[4]);
	else {
		KPRINT_DEBUG2("mailbox write failed");
		return ret;
	}

	return 0;
}

/* Parsing PF->VF Message */
/**
 * tc956x_mbx_phy_link
 *
 * \brief API to receive link state and params from PF
 *
 * \details This function is used to receive PHY link parameters from PF using
 *		mailbox interface during state change.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] msg_buf - Pointer to mailboc message buffer
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_phy_link(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg)
{
	u32 datal = 0, datah = 0;
	u8 vf_mac_addr[6];
	u8 comp_addr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	unsigned long flags;

	if (priv == NULL || msg_buf == NULL || ack_msg == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return NACK;
	}

	if (msg_buf[0] != OPCODE_MBX_PHY_LINK)
		return NACK;

	if (msg_buf[1] != SIZE_MBX_PHY_LINK)
		return NACK;

	/* Copy link, speed and duplex values from mailbox */

	memcpy((u8 *)&priv->link, &msg_buf[2], SIZE_MBX_PHY_LINK);

	/* Indicate state change to NW layer */
	if (priv->link) {
		netif_carrier_on(priv->dev);
		NMSGPR_INFO(priv->device, "PHY Link : UP\n");

		datal = readl(priv->ioaddr + XGMAC_ADDRx_LOW(HOST_MAC_ADDR_OFFSET + priv->fn_id_info.vf_no));
		datah = readl(priv->ioaddr + XGMAC_ADDRx_HIGH(HOST_MAC_ADDR_OFFSET + priv->fn_id_info.vf_no));

		vf_mac_addr[0] = datal & 0xff;
		vf_mac_addr[1] = (datal >> 8) & 0xff;
		vf_mac_addr[2] = (datal >> 16) & 0xff;
		vf_mac_addr[3] = (datal >> 24) & 0xff;
		vf_mac_addr[4] = datah & 0xff;
		vf_mac_addr[5] = (datah >> 8) & 0xff;

		if (!(memcmp(vf_mac_addr, comp_addr, 6))) {
			spin_lock_irqsave(&priv->wq_lock, flags);
			priv->flag = SCH_WQ_LINK_STATE_UP;
			tc956xmac_mailbox_service_event_schedule(priv);
			spin_unlock_irqrestore(&priv->wq_lock, flags);

		}
	} else {
		netif_carrier_off(priv->dev);
		NMSGPR_INFO(priv->device, "PHY Link : DOWN\n");
	}

	KPRINT_DEBUG2("Link state change update received from PF\n");

	ack_msg[0] = OPCODE_MBX_ACK_MSG;
	memset(&ack_msg[1], 0, MBX_MSG_SIZE-1);

	return ACK;
}

/**
 * tc956x_mbx_rx_crc
 *
 * \brief API to receive Rx CRC state from PF
 *
 * \details This function is used to receive Rx CRC state from PF using
 * mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] msg_buf - Pointer to mailbox message buffer
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_rx_crc(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg)
{
	unsigned long flags;

	if (priv == NULL || msg_buf == NULL || ack_msg == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return NACK;
	}

	if (msg_buf[0] != OPCODE_MBX_RX_CRC)
		return NACK;

	if (msg_buf[1] != SIZE_MBX_RX_CRC)
		return NACK;

	/* Copy RX CRC state values from mailbox */
	memcpy(&priv->rx_crc_pad_state, &msg_buf[2],
						SIZE_MBX_RX_CRC);
	priv->pf_drv_cap.crc_en = priv->rx_crc_pad_state;

	spin_lock_irqsave(&priv->wq_lock, flags);
	tc956xmac_mailbox_service_event_schedule(priv);
	spin_unlock_irqrestore(&priv->wq_lock, flags);

	KPRINT_DEBUG2("Rx CRC state received from PF");
	ack_msg[0] = OPCODE_MBX_ACK_MSG;
	memset(&ack_msg[1], 0, MBX_MSG_SIZE-1);

	return ACK;
}

/**
 * tc956x_mbx_rx_csum
 *
 * \brief API to receive Rx Checksum state from PF
 *
 * \details This function is used to receive Rx Checksum state from PF using
 * mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] msg_buf - Pointer to mailbox message buffer
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_rx_csum(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg)
{
	unsigned long flags;

	if (priv == NULL || msg_buf == NULL || ack_msg == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return NACK;
	}

	if (msg_buf[0] != OPCODE_MBX_RX_CSUM)
		return NACK;

	if (msg_buf[1] != SIZE_MBX_RX_CSUM)
		return NACK;

	/* Copy RX checksum state from mailbox */
	memcpy(&priv->rx_csum_state, &msg_buf[2], SIZE_MBX_RX_CSUM);
	priv->pf_drv_cap.csum_en = priv->rx_csum_state;

	spin_lock_irqsave(&priv->wq_lock, flags);
	tc956xmac_mailbox_service_event_schedule(priv);
	spin_unlock_irqrestore(&priv->wq_lock, flags);

	KPRINT_DEBUG2("Rx CSum state received from PF");

	ack_msg[0] = OPCODE_MBX_ACK_MSG;
	memset(&ack_msg[1], 0, MBX_MSG_SIZE-1);

	return ACK;
}

/**
 * tc956x_mbx_rx_dma_ch_tlptr
 *
 * \brief API to receive dma ch no from PF to update tail pointer
 *
 * \details This function is used to receive dma ch no from PF using
 * mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] msg_buf - Pointer to mailbox message buffer
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_rx_dma_ch_tlptr(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg)
{
	u32 ch;
	struct tc956xmac_rx_queue *rx_q;

	if (priv == NULL || msg_buf == NULL || ack_msg == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return NACK;
	}

	if (msg_buf[0] != OPCODE_MBX_DMA_CH_TLPTR)
		return NACK;

	if (msg_buf[1] != SIZE_MBX_RX_DMA_TL_PTR)
		return NACK;

	memcpy(&ch, &msg_buf[2], SIZE_MBX_RX_DMA_TL_PTR);

	if (priv->plat->ch_in_use[ch] == 0)
		return NACK;

	rx_q = &priv->rx_queue[ch];
	tc956xmac_set_rx_tail_ptr(priv, priv->ioaddr, rx_q->rx_tail_addr, ch);

	ack_msg[0] = OPCODE_MBX_ACK_MSG;
	memset(&ack_msg[1], 0, MBX_MSG_SIZE-1);

	return ACK;
}

/**
 * tc956x_mbx_setup_etf
 *
 * \brief API to receive tbs status from PF to update tail pointer
 *
 * \details This function is used to receive tbs status from PF using
 * mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] msg_buf - Pointer to mailbox message buffer
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_setup_etf(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg)
{
	u32 ch;
	u8 tbs_status;

	if (priv == NULL || msg_buf == NULL || ack_msg == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return NACK;
	}

	if (msg_buf[0] != OPCODE_MBX_SETUP_ETF)
		return NACK;

	if (msg_buf[1] != SIZE_MBX_SETUP_ETF)
		return NACK;

	memcpy(&ch, &msg_buf[2], 4);
	memcpy(&tbs_status, &msg_buf[6], 1);

	if (priv->plat->tx_queues_cfg[ch].mode_to_use != MTL_QUEUE_DISABLE) {
		if (priv->tx_queue[ch].tbs & TC956XMAC_TBS_AVAIL) {
			if (tbs_status)
				priv->tx_queue[ch].tbs |= TC956XMAC_TBS_EN;
			else
				priv->tx_queue[ch].tbs &= ~TC956XMAC_TBS_EN;
		}
	}

	ack_msg[0] = OPCODE_MBX_ACK_MSG;
	memset(&ack_msg[1], 0, MBX_MSG_SIZE-1);

	return ACK;
}

/**
 * tc956x_mbx_pf_flr
 *
 * \brief API to receive PF FLR trigger from PF
 *
 * \details This function is used to receive PF FLR trigger from PF using
 * mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] msg_buf - Pointer to mailbox message buffer
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_pf_flr(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg)
{
	unsigned long flags;

	if (priv == NULL || msg_buf == NULL || ack_msg == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return NACK;
	}

	if (msg_buf[0] != OPCODE_MBX_FLR)
		return NACK;

	if (msg_buf[1] != 0)
		return NACK;

	spin_lock_irqsave(&priv->wq_lock, flags);
	priv->flag = SCH_WQ_PF_FLR;
	tc956xmac_mailbox_service_event_schedule(priv);
	spin_unlock_irqrestore(&priv->wq_lock, flags);

	ack_msg[0] = OPCODE_MBX_ACK_MSG;
	memset(&ack_msg[1], 0, MBX_MSG_SIZE-1);

	return ACK;
}

/**
 * tc956x_mbx_rx_dma_err
 *
 * \brief API to receive DMA err state from PF
 *
 * \details This function is used to receive DMA err state from PF using
 * mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] msg_buf - Pointer to mailbox message buffer
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_rx_dma_err(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg)
{
	unsigned long flags;

	if (priv == NULL || msg_buf == NULL || ack_msg == NULL) {
		KPRINT_DEBUG2("NULL pointer error\n");
		return NACK;
	}

	if (msg_buf[0] != OPCODE_MBX_DMA_ERR)
		return NACK;

	if (msg_buf[1] != 0)
		return NACK;

	spin_lock_irqsave(&priv->wq_lock, flags);
	priv->flag = SCH_WQ_RX_DMA_ERR;
	tc956xmac_mailbox_service_event_schedule(priv);
	spin_unlock_irqrestore(&priv->wq_lock, flags);

	ack_msg[0] = OPCODE_MBX_ACK_MSG;
	memset(&ack_msg[1], 0, MBX_MSG_SIZE-1);

	return ACK;
}

const struct tc956xmac_mbx_wrapper_ops tc956xmac_mbx_wrapper_ops = {
	.dma_tx_mode = tc956xmac_mbx_dma_tx_mode,
	.get_umac_addr = tc956xmac_mbx_get_umac_addr,
	.set_umac_addr = tc956xmac_mbx_set_umac_addr,
	.set_mtl_tx_queue_weight = tc956xmac_mbx_set_mtl_tx_queue_weight,
	.config_cbs = tc956xmac_mbx_config_cbs,
	.tx_queue_prio = tc956xmac_mbx_tx_queue_prio,
	.get_link_status = tc956xmac_mbx_get_link_status,
	.phy_link = tc956x_mbx_phy_link,
	.get_drv_cap = tc956x_mbx_get_drv_cap,
	.rx_crc = tc956x_mbx_rx_crc,
	.rx_csum = tc956x_mbx_rx_csum,
	.get_cbs = tc956xmac_vf_ioctl_get_cbs,
	.set_cbs = tc956xmac_vf_ioctl_set_cbs,
	.set_est = tc956xmac_vf_ioctl_set_est,
	.get_est = tc956xmac_vf_ioctl_get_est,
	.set_rxp = tc956xmac_vf_ioctl_set_rxp,
	.get_rxp = tc956xmac_vf_ioctl_get_rxp,
	.get_fpe = tc956xmac_vf_ioctl_get_fpe,
	.set_fpe = tc956xmac_vf_ioctl_set_fpe,
	.get_speed = tc956xmac_vf_ioctl_get_connected_speed,
	.reg_wr = tc956xmac_vf_ioctl_reg_wr,
	.get_pause_param = tc956xmac_vf_ethtool_get_pauseparam,
	.get_eee = tc956xmac_vf_ethtool_get_eee,
	.add_mac = tc956xmac_vf_add_mac,
	.delete_mac = tc956xmac_vf_delete_mac,
	.add_vlan = tc956xmac_vf_add_vlan,
	.delete_vlan = tc956xmac_vf_delete_vlan,
	.reset_eee_mode = tc956xmac_vf_mbx_reset_eee_mode,
	.vf_reset = tc956xmac_vf_mbx_reset,
	.rx_dma_ch_tlptr = tc956x_mbx_rx_dma_ch_tlptr,
	.setup_cbs = tc956x_mbx_setup_cbs,
	.setup_mbx_etf = tc956x_mbx_setup_etf,
	.pf_flr = tc956x_mbx_pf_flr,
	.rx_dma_err = tc956x_mbx_rx_dma_err,
};

#endif
