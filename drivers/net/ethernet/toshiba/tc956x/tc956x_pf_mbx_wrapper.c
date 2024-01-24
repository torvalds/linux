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
 *  15 Sep 2020 : Initial Version
 *  VERSION     : 00-01
 *
 *  30 Nov 2021 : Base lined for SRIOV
 *  VERSION     : 01-02
 *
 *  20 May 2022 : 1. Automotive Driver, CPE fixes merged and IPA Features supported
 *                2. Base lined version
 *  VERSION     : 03-00
 */

#include "tc956x_pf_mbx.h"
#include "tc956xmac.h"
#include "common.h"

extern int tc956xmac_ioctl_get_cbs(struct tc956xmac_priv *priv, void *data);
extern int tc956xmac_ioctl_set_cbs(struct tc956xmac_priv *priv, void *data);
extern int tc956xmac_ioctl_set_rxp(struct tc956xmac_priv *priv, void *data);
extern int tc956xmac_ioctl_get_rxp(struct tc956xmac_priv *priv, void *data);
extern int tc956xmac_ioctl_set_est(struct tc956xmac_priv *priv, void *data);
extern int tc956xmac_ioctl_get_est(struct tc956xmac_priv *priv, void *data);
extern int tc956xmac_ioctl_set_fpe(struct tc956xmac_priv *priv, void *data);
extern int tc956xmac_ioctl_get_fpe(struct tc956xmac_priv *priv, void *data);
extern int tc956xmac_ethtool_op_get_eee(struct net_device *dev,
										struct ethtool_eee *edata);


extern int tc956x_pf_set_mac_filter(struct net_device *dev, int vf,
										const u8 *mac);

extern void tc956x_pf_del_mac_filter(struct net_device *dev, int vf, const u8 *mac);

extern void tc956x_pf_set_vlan_filter(struct net_device *dev, u16 vf, u16 vid);

extern void tc956x_pf_del_vlan_filter(struct net_device *dev, u16 vf, u16 vid);

extern void tc956x_pf_del_umac_addr(struct tc956xmac_priv *priv, int index, int vf);

extern void tc956xmac_service_mbx_event_schedule(struct tc956xmac_priv *priv);

#ifdef TC956X_SRIOV_PF
/**
 * tc956x_mbx_mac_link
 *
 * \brief API to send link state and params to VFs
 *
 * \details This function is used to send PHY link parameters to VF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 *
 * \return None
 */
static void tc956x_mbx_phy_link(struct tc956xmac_priv *priv)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MAX_NO_OF_VFS][MBX_TOT_SIZE];
	int ret, i;
	enum mbx_msg_fns msg_dst;

	if (priv == NULL) {
		KPRINT_DEBUG1("NULL pointer error");
		return;
	}

	for (i = 0; i < MAX_NO_OF_VFS; i++) {

		mbx[i][0] = OPCODE_MBX_PHY_LINK; /* opcode */
		mbx[i][1] = SIZE_MBX_PHY_LINK; /* size */

	/* Copy link, speed and duplex values */
		memcpy(&mbx[i][2], &priv->link, SIZE_MBX_PHY_LINK);
	}

	/* Send data to all VFs */
	for (i = vf0; i <= vf2; i++) {

	if (priv->clear_to_send[i-vf0] == VF_UP) {
		msg_dst = (enum mbx_msg_fns) i;

		ret = tc956xmac_mbx_write(priv, &mbx[i-vf0][0], msg_dst,
							&priv->fn_id_info);

		/* Validation of successfull message posting can be done
		 * by reading the mbx buffer for ACK opcode (0xFE)
		 */

		if (ret > 0)
			KPRINT_DEBUG1(
				"mailbox write with ACK or NACK %d msgbuff %x %x\n",
								ret, mbx[i-vf0][0], mbx[i-vf0][4]);
		else
			KPRINT_DEBUG1("mailbox write failed");
		} else
			KPRINT_DEBUG1("VF %d not up", i-vf0);
	}
}

/**
 * tc956x_mbx_rx_dma_ch_tlptr
 *
 * \brief API to send ch numer to VF where overflow detected
 *
 * \details This function is used to send DMA channel no to VF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] ch - DMA channel no
 * \param[in] vf - VF no of DMA channel
 *
 * \return 0 or error
 */
static int tc956x_mbx_rx_dma_ch_tlptr(struct tc956xmac_priv *priv, u32 ch, u8 vf)
{

	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MBX_TOT_SIZE];
	int ret = 0;
	enum mbx_msg_fns msg_dst;

	if (priv == NULL) {
		KPRINT_DEBUG1("NULL pointer error");
		return -EFAULT;
	}

	mbx[0] = OPCODE_MBX_DMA_CH_TLPTR; /* opcode */
	mbx[1] = SIZE_MBX_RX_DMA_TL_PTR; /* size */

	memcpy(&mbx[2], &ch, SIZE_MBX_RX_DMA_TL_PTR);

	if (priv->clear_to_send[vf] == VF_UP) {
		msg_dst = (enum mbx_msg_fns)(vf + 3);

		ret = tc956xmac_mbx_write(priv, &mbx[0], msg_dst,
							&priv->fn_id_info);

		/* Validation of successfull message posting can be done
		 * by reading the mbx buffer for ACK opcode (0xFF)
		 */
		if (ret > 0)
			KPRINT_DEBUG1(
				"mailbox write with ACK or NACK %d msgbuff %x %x\n",
								ret, mbx[0], mbx[4]);
		else
			KPRINT_DEBUG1("mailbox write failed");
	} else
		KPRINT_DEBUG1("VF %d is not UP", vf);

	return ret;
}

/**
 * tc956x_mbx_set_dma_tx_mode
 *
 * \brief API to set dma tx mode
 *
 * \details This function is used to set dma tx mode on VF request using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mbx_buff - Pointer to message
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_set_dma_tx_mode(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff)
{
	u32 txmode = 0;
	u32 chan = 0;
	u8 qmode = 0;
	int txfifosz;

	if (priv == NULL || mbx_buff == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_SET_DMA_TX_MODE)
		return NACK;

	if (mbx_buff[1] != SIZE_MBX_SET_DMA_TX_MODE)
		return NACK;

	memcpy(&txmode, &mbx_buff[2], sizeof(txmode));
	memcpy(&chan, &mbx_buff[6], sizeof(chan));
	memcpy(&txfifosz, &mbx_buff[10], sizeof(txfifosz));
	qmode = mbx_buff[14];

	/* Call core API to set the register */
	tc956xmac_dma_tx_mode(priv, priv->ioaddr, txmode, chan,
				txfifosz, qmode);

	/* Clear the ACK buffer as there is no ACK message */
	ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
	memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);

	/* return ACK as all steps are successfull */
	return ACK;
}

/**
 * tc956x_mbx_set_mtl_tx_queue_weight
 *
 * \brief API to set tx mtl queue weight
 *
 * \details This function is used to set tx mtl queue weight on VF request using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mbx_buff - Pointer to message
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_set_mtl_tx_queue_weight(struct tc956xmac_priv *priv, u8 *mbx_buff,
												u8 *ack_buff)
{
	u32 weight, traffic_class;

	if (priv == NULL || mbx_buff == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_SET_TX_Q_WEIGHT)
		return NACK;

	if (mbx_buff[1] != SIZE_MBX_SET_TX_Q_WEIGHT)
		return NACK;

	memcpy(&weight, &mbx_buff[2], sizeof(weight));
	memcpy(&traffic_class, &mbx_buff[6], sizeof(traffic_class));

	/* Call core API to set the register */
	tc956xmac_set_mtl_tx_queue_weight(priv, priv->hw, weight, traffic_class);

	/* Clear the ACK buffer as there is no ACK message */
	ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
	memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);

	/* return ACK as all steps are successfull */
	return ACK;
}

/**
 * tc956x_mbx_config_cbs
 *
 * \brief API to configure te cbs param
 *
 * \details This function is used to configure te cbs param on VF request using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mbx_buff - Pointer to message
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_config_cbs(struct tc956xmac_priv *priv,
				u8 *mbx_buff, u8 *ack_buff)
{
	u32 send_slope, idle_slope, high_credit;
	u32	low_credit, queue;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	if (priv == NULL || mbx_buff == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_CFG_CBS)
		return NACK;

	if (mbx_buff[1] != SIZE_MBX_CFG_CBS)
		return NACK;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.cbs, flags);
#endif
	memcpy(&send_slope, &mbx_buff[2], sizeof(send_slope));
	memcpy(&idle_slope, &mbx_buff[6], sizeof(idle_slope));
	memcpy(&high_credit, &mbx_buff[10], sizeof(high_credit));
	memcpy(&low_credit, &mbx_buff[14], sizeof(low_credit));
	memcpy(&queue, &mbx_buff[18], sizeof(queue));

	/* Call core API to set the register */
	tc956xmac_config_cbs(priv, priv->hw, send_slope,
		idle_slope, high_credit, low_credit, queue);

	if (priv->speed == SPEED_100) {
		priv->cbs_speed100_cfg[queue].send_slope = send_slope;
		priv->cbs_speed100_cfg[queue].idle_slope = idle_slope;
		priv->cbs_speed100_cfg[queue].high_credit = high_credit;
		priv->cbs_speed100_cfg[queue].low_credit = low_credit;
	} else if (priv->speed == SPEED_1000) {
		priv->cbs_speed1000_cfg[queue].send_slope = send_slope;
		priv->cbs_speed1000_cfg[queue].idle_slope = idle_slope;
		priv->cbs_speed1000_cfg[queue].high_credit = high_credit;
		priv->cbs_speed1000_cfg[queue].low_credit = low_credit;
	} else if (priv->speed == SPEED_10000) {
		priv->cbs_speed10000_cfg[queue].send_slope = send_slope;
		priv->cbs_speed10000_cfg[queue].idle_slope = idle_slope;
		priv->cbs_speed10000_cfg[queue].high_credit = high_credit;
		priv->cbs_speed10000_cfg[queue].low_credit = low_credit;
	} else if (priv->speed == SPEED_2500) {
		priv->cbs_speed2500_cfg[queue].send_slope = send_slope;
		priv->cbs_speed2500_cfg[queue].idle_slope = idle_slope;
		priv->cbs_speed2500_cfg[queue].high_credit = high_credit;
		priv->cbs_speed2500_cfg[queue].low_credit = low_credit;
	} else if (priv->speed == SPEED_5000) {
		priv->cbs_speed5000_cfg[queue].send_slope = send_slope;
		priv->cbs_speed5000_cfg[queue].idle_slope = idle_slope;
		priv->cbs_speed5000_cfg[queue].high_credit = high_credit;
		priv->cbs_speed5000_cfg[queue].low_credit = low_credit;
	}
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.cbs, flags);
#endif
	/* Clear the ACK buffer as there is no ACK message */
	ack_buff[0] = OPCODE_MBX_ACK_MSG;
	memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);

	/* return ACK as all steps are successfull */
	return ACK;

}

/**
 * tc956x_mbx_setup_cbs
 *
 * \brief API to configure te cbs param using tc command
 *
 * \details This function is used to configure te cbs param using tc command
 * on VF request using mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mbx_buff - Pointer to message
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_setup_cbs(struct tc956xmac_priv *priv,
				u8 *mbx_buff, u8 *ack_buff)
{
	struct tc_cbs_qopt_offload qopt;

	if (priv == NULL || mbx_buff == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_SETUP_CBS)
		return NACK;

	if (mbx_buff[1] != SIZE_MBX_SETUP_CBS)
		return NACK;

	memcpy(&qopt.enable, &mbx_buff[2], sizeof(qopt.enable));
	memcpy(&qopt.idleslope, &mbx_buff[3], sizeof(qopt.idleslope));
	memcpy(&qopt.sendslope, &mbx_buff[7], sizeof(qopt.sendslope));
	memcpy(&qopt.hicredit, &mbx_buff[11], sizeof(qopt.hicredit));
	memcpy(&qopt.locredit, &mbx_buff[15], sizeof(qopt.locredit));
	memcpy(&qopt.queue, &mbx_buff[19], sizeof(qopt.queue));

	tc956xmac_tc_setup_cbs(priv, &qopt);
	/* Clear the ACK buffer as there is no ACK message */
	ack_buff[0] = OPCODE_MBX_ACK_MSG;
	memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);

	/* return ACK as all steps are successfull */
	return ACK;
}

/**
 * tc956x_mbx_tx_queue_prio
 *
 * \brief API to set tx queue priority
 *
 * \details This function is used to set tx queue priority
 * on VF request using mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mbx_buff - Pointer to message
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_tx_queue_prio(struct tc956xmac_priv *priv,
				u8 *mbx_buff, u8 *ack_buff)
{
	u32 queue;
	u32 prio;

	if (priv == NULL || mbx_buff == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_SET_TX_Q_PRIOR)
		return NACK;

	if (mbx_buff[1] != SIZE_MBX_SET_TX_Q_PRIOR)
		return NACK;

	memcpy(&prio, &mbx_buff[2], sizeof(prio));
	memcpy(&queue, &mbx_buff[6], sizeof(queue));

	/* Call core API to set the register */
	tc956xmac_tx_queue_prio(priv, priv->hw, prio, queue);


	/* Clear the ACK buffer as there is no ACK message */
	ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
	memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);

	/* return ACK as all steps are successfull */
	return ACK;

}

/**
 * tc956x_mbx_vf_get_link_status
 *
 * \brief API to send the pf link status
 *
 * \details This function is used to send the pf link status
 * on VF request using mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mbx_buff - Pointer to message
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_vf_get_link_status(struct tc956xmac_priv *priv,
					u8 *mbx_buff, u8 *ack_buff)
{
	if (priv == NULL || mbx_buff == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_VF_GET_LINK_STATUS)
		return NACK;

	if (mbx_buff[1] != 0)
		return NACK;

	ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
	ack_buff[1] = SIZE_MBX_PHY_LINK; /* set size */

	/* Copy link, speed and duplex values */
	memcpy(&ack_buff[2], &priv->link, SIZE_MBX_PHY_LINK);

	/* return ACK as all steps are successfull */
	return ACK;
}

/**
 * tc956xmac_pf_ioctl_interface
 *
 * \brief API to process ioctl requests
 *
 * \details This function is used to process ioctl requests
 * on VF request using mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mbx_buff - Pointer to message
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956xmac_pf_ioctl_interface(struct tc956xmac_priv *priv,
							u8 *mbx, u8 *ack_buff, u8 vf_no)
{
	u32 val;
	u32 addr;
	u32 bar_num;
	unsigned long flags;
	struct hwtstamp_config *config = &priv->tstamp_config;

	DBGPR_FUNC(priv->device, "-->%s\n", __func__);

	if (priv == NULL || mbx == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	switch (mbx[2]) {
	case TC956XMAC_SET_CBS_1:
	case TC956XMAC_SET_CBS_2:
		{
			static u8 msg_seq[TC956X_TOTAL_VFS], mbx_loc[TC956X_TOTAL_VFS][MBX_TOT_SIZE * TC956X_TWO];
			static struct tc956xmac_ioctl_cbs_cfg cbs;

			vf_no -= TC956X_ONE;
			if (mbx[2] == TC956XMAC_SET_CBS_1) {

				memset(&mbx_loc[vf_no][0], 0, MBX_TOT_SIZE * TC956X_TWO);

				memcpy(&mbx_loc[vf_no][0], &mbx[4], SIZE_MBX_SET_GET_CBS_1);
				msg_seq[vf_no] = TC956X_ONE << TC956X_ZERO;

			} else if (mbx[2] == TC956XMAC_SET_CBS_2) {
				memcpy(&mbx_loc[vf_no][SIZE_MBX_SET_GET_CBS_1], &mbx[4], SIZE_MBX_SET_GET_CBS_2);
				msg_seq[vf_no] |= TC956X_ONE << TC956X_ONE;

				if (msg_seq[vf_no] == TC956X_THREE) {

					msg_seq[vf_no] = TC956X_ZERO;

					memset(&cbs, 0, sizeof(cbs));

					cbs.queue_idx = mbx[3];

					memcpy(&cbs.speed100cfg.send_slope, &mbx_loc[vf_no][0], sizeof(cbs.speed100cfg.send_slope));
					memcpy(&cbs.speed100cfg.idle_slope, &mbx_loc[vf_no][4], sizeof(cbs.speed100cfg.idle_slope));
					memcpy(&cbs.speed100cfg.high_credit, &mbx_loc[vf_no][8], sizeof(cbs.speed100cfg.high_credit));
					memcpy(&cbs.speed100cfg.low_credit, &mbx_loc[vf_no][12], sizeof(cbs.speed100cfg.low_credit));
					memcpy(&cbs.speed100cfg.percentage, &mbx_loc[vf_no][16], sizeof(cbs.speed100cfg.percentage));

					memcpy(&cbs.speed1000cfg.send_slope, &mbx_loc[vf_no][20], sizeof(cbs.speed1000cfg.send_slope));
					memcpy(&cbs.speed1000cfg.idle_slope, &mbx_loc[vf_no][24], sizeof(cbs.speed1000cfg.idle_slope));
					memcpy(&cbs.speed1000cfg.high_credit, &mbx_loc[vf_no][28], sizeof(cbs.speed1000cfg.high_credit));
					memcpy(&cbs.speed1000cfg.low_credit, &mbx_loc[vf_no][32], sizeof(cbs.speed1000cfg.low_credit));
					memcpy(&cbs.speed1000cfg.percentage, &mbx_loc[vf_no][36], sizeof(cbs.speed1000cfg.percentage));

					memcpy(&cbs.speed10000cfg.send_slope, &mbx_loc[vf_no][40], sizeof(cbs.speed10000cfg.send_slope));
					memcpy(&cbs.speed10000cfg.idle_slope, &mbx_loc[vf_no][44], sizeof(cbs.speed10000cfg.idle_slope));
					memcpy(&cbs.speed10000cfg.high_credit, &mbx_loc[vf_no][48], sizeof(cbs.speed10000cfg.high_credit));
					memcpy(&cbs.speed10000cfg.low_credit, &mbx_loc[vf_no][52], sizeof(cbs.speed10000cfg.low_credit));
					memcpy(&cbs.speed10000cfg.percentage, &mbx_loc[vf_no][56], sizeof(cbs.speed10000cfg.percentage));

					memcpy(&cbs.speed5000cfg.send_slope, &mbx_loc[vf_no][60], sizeof(cbs.speed5000cfg.send_slope));
					memcpy(&cbs.speed5000cfg.idle_slope, &mbx_loc[vf_no][64], sizeof(cbs.speed5000cfg.idle_slope));
					memcpy(&cbs.speed5000cfg.high_credit, &mbx_loc[vf_no][68], sizeof(cbs.speed5000cfg.high_credit));
					memcpy(&cbs.speed5000cfg.low_credit, &mbx_loc[vf_no][72], sizeof(cbs.speed5000cfg.low_credit));
					memcpy(&cbs.speed5000cfg.percentage, &mbx_loc[vf_no][76], sizeof(cbs.speed5000cfg.percentage));

					memcpy(&cbs.speed2500cfg.send_slope, &mbx_loc[vf_no][80], sizeof(cbs.speed2500cfg.send_slope));
					memcpy(&cbs.speed2500cfg.idle_slope, &mbx_loc[vf_no][84], sizeof(cbs.speed2500cfg.idle_slope));
					memcpy(&cbs.speed2500cfg.high_credit, &mbx_loc[vf_no][88], sizeof(cbs.speed2500cfg.high_credit));
					memcpy(&cbs.speed2500cfg.low_credit, &mbx_loc[vf_no][92], sizeof(cbs.speed2500cfg.low_credit));
					memcpy(&cbs.speed2500cfg.percentage, &mbx_loc[vf_no][96], sizeof(cbs.speed2500cfg.percentage));

					tc956xmac_ioctl_set_cbs(priv, &cbs);
				}
			}

		/* Clear the ACK buffer as there is no ACK message */
		ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
		memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);
		break;
		}
	case TC956XMAC_GET_CBS_1:
	case TC956XMAC_GET_CBS_2:
	{
		static u8 mbx_loc[TC956X_TOTAL_VFS][MBX_TOT_SIZE * TC956X_TWO];
		struct tc956xmac_ioctl_cbs_cfg cbs;
		int ret = 0;

		vf_no -= TC956X_ONE;
		if (mbx[2] == TC956XMAC_GET_CBS_1) {
			memset(&mbx_loc[vf_no][0], 0, sizeof(struct tc956xmac_ioctl_cbs_cfg));
			cbs.queue_idx = mbx[3];
			ret = tc956xmac_ioctl_get_cbs(priv, &cbs);
			memcpy(&mbx_loc[vf_no][0], (u8 *)&cbs.speed100cfg, sizeof(cbs) - TC956X_EIGHT);

			ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
			ack_buff[1] = SIZE_MBX_SET_GET_CBS_1;	/* set size */

			memcpy(&ack_buff[2], &mbx_loc[vf_no][0], SIZE_MBX_SET_GET_CBS_1);

		} else if (mbx[2] == TC956XMAC_GET_CBS_2) {

			ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
			ack_buff[1] = SIZE_MBX_SET_GET_CBS_2;	/* set size */
			memcpy(&ack_buff[2], &mbx_loc[vf_no][SIZE_MBX_SET_GET_CBS_1], SIZE_MBX_SET_GET_CBS_2);

		}

		break;
	}
	case TC956XMAC_SET_EST_1:
	case TC956XMAC_SET_EST_2:
	case TC956XMAC_SET_EST_3:
	case TC956XMAC_SET_EST_4:
	case TC956XMAC_SET_EST_5:
	case TC956XMAC_SET_EST_6:
	case TC956XMAC_SET_EST_7:
	case TC956XMAC_SET_EST_8:
	case TC956XMAC_SET_EST_9:
	case TC956XMAC_SET_EST_10:
	{
		static u16 total_size, msg_size, offset = 1;
		static u8 mbx_loc[MBX_TOT_SIZE * TC956X_TEN];
		static struct tc956xmac_ioctl_est_cfg *est;

		if (vf_no != TC956XMAC_VF_ADAS)
			return NACK;

		vf_no -= TC956X_ONE;

		if (mbx[2] == TC956XMAC_SET_EST_1) {
			est = kzalloc(sizeof(*est), GFP_ATOMIC);
			if (!est)
				return -ENOMEM;

			memset(&mbx_loc[0], 0, MBX_TOT_SIZE * TC956X_TEN);
			memcpy(&mbx_loc[0], &mbx[3], SIZE_MBX_SET_GET_EST_1);

			memset(est, 0, sizeof(struct tc956xmac_est));
			memcpy(&est->enabled, &mbx_loc[0], sizeof(est->enabled));
			memcpy(&est->btr_offset[0], &mbx_loc[4], sizeof(est->btr_offset));
			memcpy(&est->ctr[0], &mbx_loc[12], sizeof(est->ctr));
			memcpy(&est->ter, &mbx_loc[20], sizeof(est->ter));
			memcpy(&est->gcl_size, &mbx_loc[24], sizeof(est->gcl_size));

			total_size = est->gcl_size * sizeof(*est->gcl);
			msg_size = mbx[1] - EST_FIX_MSG_LEN;
		} else {
			memcpy(&mbx_loc[offset * SIZE_MBX_SET_GET_EST_1], &mbx[3], SIZE_MBX_SET_GET_EST_1);
			msg_size += mbx[1];
			offset++;
		}
		if (msg_size == total_size) {
			memcpy(&est->gcl[0], &mbx_loc[28], est->gcl_size * sizeof(*est->gcl));
			tc956xmac_ioctl_set_est(priv, est);
			msg_size = 0;
			total_size = 0;
			offset = 1;
		}
		/* Clear the ACK buffer as there is no ACK message */
		ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
		memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);
		break;
	}
	case TC956XMAC_GET_EST_1:
	case TC956XMAC_GET_EST_2:
	case TC956XMAC_GET_EST_3:
	case TC956XMAC_GET_EST_4:
	case TC956XMAC_GET_EST_5:
	case TC956XMAC_GET_EST_6:
	case TC956XMAC_GET_EST_7:
	case TC956XMAC_GET_EST_8:
	case TC956XMAC_GET_EST_9:
	case TC956XMAC_GET_EST_10:
	{
		static u16 gcl_size, msg_size, toatl_msg_size, offset;
		static u8 mbx_loc[MBX_TOT_SIZE * TC956X_TEN];
		static struct tc956xmac_ioctl_est_cfg *est;

		if (vf_no != TC956XMAC_VF_ADAS)
			return NACK;

		vf_no -= TC956X_ONE;

		if (mbx[2] == TC956XMAC_GET_EST_1) {
			est = kzalloc(sizeof(*est), GFP_ATOMIC);
			if (!est)
				return -ENOMEM;
			tc956xmac_ioctl_get_est(priv, est);

			memcpy(&mbx_loc[0], (u8 *)&est->enabled, sizeof(est->enabled));
			memcpy(&mbx_loc[4], (u8 *)&est->estwid, sizeof(est->estwid));
			memcpy(&mbx_loc[8], (u8 *)&est->estdep, sizeof(est->estdep));
			memcpy(&mbx_loc[12], (u8 *)&est->btr_offset[0], sizeof(est->btr_offset));
			memcpy(&mbx_loc[20], (u8 *)&est->ctr[0], sizeof(est->ctr));
			memcpy(&mbx_loc[28], (u8 *)&est->ter, sizeof(est->ter));
			memcpy(&mbx_loc[32], (u8 *)&est->gcl_size, sizeof(est->gcl_size));
			memcpy(&mbx_loc[36], (u8 *)&est->gcl, (est->gcl_size * sizeof(*est->gcl)));

			gcl_size = est->gcl_size * sizeof(*est->gcl);
			toatl_msg_size = gcl_size + EST_FIX_MSG_LEN + 8;
			msg_size = toatl_msg_size >= SIZE_MBX_SET_GET_EST_1 ? SIZE_MBX_SET_GET_EST_1 : toatl_msg_size;
			offset = 0;
		} else {
			offset += msg_size;
			msg_size = (toatl_msg_size - offset) >= SIZE_MBX_SET_GET_EST_1 ? SIZE_MBX_SET_GET_EST_1 : (toatl_msg_size - offset);
		}

		if (toatl_msg_size == (offset + msg_size)) {
			kfree(est);
		}

		ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
		ack_buff[1] = msg_size; /* set ACK size */
		memcpy(&ack_buff[2], &mbx_loc[offset], msg_size);

		break;
	}
	case TC956XMAC_SET_RXP_1:
	case TC956XMAC_SET_RXP_2:
	case TC956XMAC_SET_RXP_3:
	case TC956XMAC_SET_RXP_4:
	case TC956XMAC_SET_RXP_5:
	case TC956XMAC_SET_RXP_6:
	case TC956XMAC_SET_RXP_7:
	case TC956XMAC_SET_RXP_8:
	case TC956XMAC_SET_RXP_9:
	case TC956XMAC_SET_RXP_10:
	case TC956XMAC_SET_RXP_11:
	case TC956XMAC_SET_RXP_12:
	case TC956XMAC_SET_RXP_13:
	case TC956XMAC_SET_RXP_14:
	case TC956XMAC_SET_RXP_15:
	case TC956XMAC_SET_RXP_16:
	case TC956XMAC_SET_RXP_17:
	case TC956XMAC_SET_RXP_18:
	case TC956XMAC_SET_RXP_19:
	case TC956XMAC_SET_RXP_20:
	case TC956XMAC_SET_RXP_21:
	case TC956XMAC_SET_RXP_22:
	case TC956XMAC_SET_RXP_23:
	case TC956XMAC_SET_RXP_24:
	case TC956XMAC_SET_RXP_25:
	case TC956XMAC_SET_RXP_26:
	case TC956XMAC_SET_RXP_27:
	case TC956XMAC_SET_RXP_28:
	case TC956XMAC_SET_RXP_29:
	case TC956XMAC_SET_RXP_30:
	case TC956XMAC_SET_RXP_31:
	case TC956XMAC_SET_RXP_32:
	case TC956XMAC_SET_RXP_33:
	case TC956XMAC_SET_RXP_34:
	case TC956XMAC_SET_RXP_35:
	case TC956XMAC_SET_RXP_36:
	case TC956XMAC_SET_RXP_37:
	{
		static u16 total_size, msg_size, offset = 1;
		static u8 *mbx_loc;
		static struct tc956xmac_ioctl_rxp_cfg *rxp;

		if (mbx[2] == TC956XMAC_SET_RXP_1) {
			rxp = kzalloc(sizeof(*rxp), GFP_ATOMIC);
			if (!rxp)
				return -ENOMEM;

			mbx_loc = kzalloc(sizeof(*rxp), GFP_ATOMIC);
			if (!mbx_loc) {
				kfree(rxp);
				return -ENOMEM;
			}

			memset(&mbx_loc[0], 0, sizeof(*rxp));
			memcpy(&mbx_loc[0], &mbx[3], SIZE_MBX_SET_GET_RXP_1);

			memset(rxp, 0, sizeof(struct tc956xmac_ioctl_rxp_cfg));
			memcpy(&rxp->frpes, &mbx_loc[0], sizeof(rxp->frpes));
			memcpy(&rxp->enabled, &mbx_loc[4], sizeof(rxp->enabled));
			memcpy(&rxp->nve, &mbx_loc[8], sizeof(rxp->nve));
			memcpy(&rxp->npe, &mbx_loc[12], sizeof(rxp->npe));

			total_size = rxp->nve * sizeof(*rxp->entries);
			msg_size = mbx[1] - RXP_FIX_MSG_LEN;
		} else {
			memcpy(&mbx_loc[offset*SIZE_MBX_SET_GET_RXP_1], &mbx[3], SIZE_MBX_SET_GET_RXP_1);
			msg_size += mbx[1];
			offset++;
		}

		if (msg_size == total_size) {
			memcpy(&rxp->entries[0], &mbx_loc[16], total_size);
			tc956xmac_ioctl_set_rxp(priv, rxp);
			msg_size = 0;
			total_size = 0;
			offset = 1;
			kfree(mbx_loc);
		}
		/* Clear the ACK buffer as there is no ACK message */
		ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
		memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);
		break;
	}
	case TC956XMAC_GET_RXP_1:
	case TC956XMAC_GET_RXP_2:
	case TC956XMAC_GET_RXP_3:
	case TC956XMAC_GET_RXP_4:
	case TC956XMAC_GET_RXP_5:
	case TC956XMAC_GET_RXP_6:
	case TC956XMAC_GET_RXP_7:
	case TC956XMAC_GET_RXP_8:
	case TC956XMAC_GET_RXP_9:
	case TC956XMAC_GET_RXP_10:
	case TC956XMAC_GET_RXP_11:
	case TC956XMAC_GET_RXP_12:
	case TC956XMAC_GET_RXP_13:
	case TC956XMAC_GET_RXP_14:
	case TC956XMAC_GET_RXP_15:
	case TC956XMAC_GET_RXP_16:
	case TC956XMAC_GET_RXP_17:
	case TC956XMAC_GET_RXP_18:
	case TC956XMAC_GET_RXP_19:
	case TC956XMAC_GET_RXP_20:
	case TC956XMAC_GET_RXP_21:
	case TC956XMAC_GET_RXP_22:
	case TC956XMAC_GET_RXP_23:
	case TC956XMAC_GET_RXP_24:
	case TC956XMAC_GET_RXP_25:
	case TC956XMAC_GET_RXP_26:
	case TC956XMAC_GET_RXP_27:
	case TC956XMAC_GET_RXP_28:
	case TC956XMAC_GET_RXP_29:
	case TC956XMAC_GET_RXP_30:
	case TC956XMAC_GET_RXP_31:
	case TC956XMAC_GET_RXP_32:
	case TC956XMAC_GET_RXP_33:
	case TC956XMAC_GET_RXP_34:
	case TC956XMAC_GET_RXP_35:
	case TC956XMAC_GET_RXP_36:
	case TC956XMAC_GET_RXP_37:
	{
		static u16 rxp_size, msg_size, toatl_msg_size, offset;
		static u8 *mbx_loc;
		static struct tc956xmac_ioctl_rxp_cfg *rxp;

		if (mbx[2] == TC956XMAC_GET_RXP_1) {
			rxp = kzalloc(sizeof(*rxp), GFP_ATOMIC);
			if (!rxp)
				return -ENOMEM;

			mbx_loc = kzalloc(sizeof(*rxp), GFP_ATOMIC);
			if (!mbx_loc) {
				kfree(rxp);
				return -ENOMEM;
			}
			tc956xmac_ioctl_get_rxp(priv, rxp);

			memcpy(&mbx_loc[0], (u8 *)&rxp->frpes, sizeof(rxp->frpes));
			memcpy(&mbx_loc[4], (u8 *)&rxp->enabled, sizeof(rxp->enabled));
			memcpy(&mbx_loc[8], (u8 *)&rxp->nve, sizeof(rxp->nve));
			memcpy(&mbx_loc[12], (u8 *)&rxp->npe, sizeof(rxp->npe));
			memcpy(&mbx_loc[16], (u8 *)&rxp->entries[0], (rxp->nve * sizeof(*rxp->entries)));

			rxp_size = rxp->nve * sizeof(*rxp->entries);
			toatl_msg_size = rxp_size + RXP_FIX_MSG_LEN;
			msg_size = toatl_msg_size >= SIZE_MBX_SET_GET_RXP_1 ? SIZE_MBX_SET_GET_RXP_1 : toatl_msg_size;

			offset = 0;
		} else {
			offset += msg_size;
			msg_size = (toatl_msg_size - offset) >= SIZE_MBX_SET_GET_RXP_1 ? SIZE_MBX_SET_GET_RXP_1 : (toatl_msg_size - offset);
		}

		ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
		ack_buff[1] = msg_size; /* set ACK size */
		memcpy(&ack_buff[2], &mbx_loc[offset], msg_size);

		if (toatl_msg_size == (offset + msg_size)) {
			kfree(rxp);
			kfree(mbx_loc);
		}

		break;
	}
	case TC956XMAC_SET_FPE_1:
	{
		struct tc956xmac_ioctl_fpe_cfg fpe;

		if (vf_no != TC956XMAC_VF_ADAS)
			return NACK;

		vf_no -= TC956X_ONE;

		memcpy(&fpe.enabled, &mbx[3], sizeof(fpe.enabled));
		memcpy(&fpe.pec, &mbx[7], sizeof(fpe.pec));
		memcpy(&fpe.afsz, &mbx[11], sizeof(fpe.afsz));
		memcpy(&fpe.RA_time, &mbx[15], sizeof(fpe.RA_time));
		memcpy(&fpe.HA_time, &mbx[19], sizeof(fpe.HA_time));

		tc956xmac_ioctl_set_fpe(priv, &fpe);

		/* Clear the ACK buffer as there is no ACK message */
		ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
		memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);
		break;
	}
	case TC956XMAC_GET_FPE_1:
	{
		struct tc956xmac_ioctl_fpe_cfg fpe;

		if (vf_no != TC956XMAC_VF_ADAS)
			return NACK;

		vf_no -= TC956X_ONE;

		tc956xmac_ioctl_get_fpe(priv, &fpe);

		/* Clear the ACK buffer as there is no ACK message */
		ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
		ack_buff[1] = SIZE_MBX_SET_GET_FPE_1;
		memcpy(&ack_buff[2], (u8 *)&fpe.enabled, sizeof(fpe.enabled));
		memcpy(&ack_buff[6], (u8 *)&fpe.pec, sizeof(fpe.pec));
		memcpy(&ack_buff[10], (u8 *)&fpe.afsz, sizeof(fpe.afsz));
		memcpy(&ack_buff[14], (u8 *)&fpe.RA_time, sizeof(fpe.RA_time));
		memcpy(&ack_buff[18], (u8 *)&fpe.HA_time, sizeof(fpe.HA_time));
		break;
	}
	case TC956XMAC_GET_SPEED:
		ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
		ack_buff[1] = sizeof(priv->speed); /* set size */
		memcpy(&ack_buff[2], (u8 *)&priv->speed,
			sizeof(priv->speed));
		break;

	case TC956XMAC_REG_WR:
		memcpy(&bar_num, &mbx[3], sizeof(bar_num));
		memcpy(&addr, &mbx[7], sizeof(addr));
		memcpy(&val, &mbx[11], sizeof(val));

		if (bar_num == 4)
			writel(val, (void __iomem *)(priv->dev->base_addr + addr));
		else if (bar_num == 2)/*SRAM bar number 2*/
			writel(val, (void __iomem *)(priv->tc956x_SRAM_pci_base_addr + addr));
		ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
		memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);
		break;
	case OPCODE_MBX_VF_GET_MII_PHY:
		ack_buff[0] = OPCODE_MBX_ACK_MSG;
		ack_buff[1] = sizeof(priv->plat->phy_addr);
		memcpy(&ack_buff[2], (u8 *)&priv->plat->phy_addr,
			sizeof(priv->plat->phy_addr));
		break;
	case OPCODE_MBX_VF_GET_MII_REG_1:
		vf_no -= TC956X_ONE;
		memcpy(&priv->mbx_wq_param.rq.ifr_ifru.ifru_data, &mbx[3], 6);
		memcpy(&priv->mbx_wq_param.rq.ifr_ifrn.ifrn_name, &mbx[9], 16);

		spin_lock_irqsave(&priv->wq_lock, flags);
		priv->mbx_wq_param.fn_id = SCH_WQ_PHY_REG_RD;
		priv->mbx_wq_param.val_out[vf_no] = 0;
		priv->mbx_wq_param.vf_no = vf_no;

		tc956xmac_service_mbx_event_schedule(priv);
		spin_unlock_irqrestore(&priv->wq_lock, flags);

		ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
		memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);
		break;
	case OPCODE_MBX_VF_GET_MII_REG_2:
		vf_no -= TC956X_ONE;
		ack_buff[0] = OPCODE_MBX_ACK_MSG;
		ack_buff[1] = sizeof(priv->mbx_wq_param.val_out[vf_no]);
		memcpy(&ack_buff[2], (u8 *)&priv->mbx_wq_param.val_out[vf_no], sizeof(priv->mbx_wq_param.val_out[vf_no]));
		break;
	case OPCODE_MBX_VF_GET_HW_STMP:
		ack_buff[0] = OPCODE_MBX_ACK_MSG;
		ack_buff[1] = sizeof(*config);
		memcpy(&ack_buff[2], (u8 *)config, sizeof(*config));
		break;
	}
	return ACK;
}

/**
 * tc956xmac_pf_ethtool_interface
 *
 * \brief API to process ethtool requests
 *
 * \details This function is used to process ethtool requests
 * on VF request using mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mbx_buff - Pointer to message
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956xmac_pf_ethtool_interface(struct tc956xmac_priv *priv, struct net_device *netdev,
								u8 *mbx, u8 *ack_buff)
{
	struct ethtool_eee edata;
	unsigned long flags;

	if (priv == NULL || mbx == NULL || netdev == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	switch (mbx[2]) {
	case TC956XMAC_GET_PAUSE_PARAM:
		spin_lock_irqsave(&priv->wq_lock, flags);
		priv->mbx_wq_param.fn_id = SCH_WQ_GET_PAUSE_PARAM;
		tc956xmac_service_mbx_event_schedule(priv);
		spin_unlock_irqrestore(&priv->wq_lock, flags);

		ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
		memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);
		break;
	case TC956XMAC_GET_PAUSE_PARAM_2:
		ack_buff[0] = OPCODE_MBX_ACK_MSG;
		ack_buff[1] = sizeof(struct ethtool_pauseparam);
		memcpy(&ack_buff[2], (u8 *)&priv->mbx_wq_param.pause,
			sizeof(struct ethtool_pauseparam));
		break;
	case TC956XMAC_GET_EEE:
		tc956xmac_ethtool_op_get_eee(netdev, &edata);
		ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
		ack_buff[1] = sizeof(struct ethtool_eee); /* set size */
		memcpy(&ack_buff[2], (u8 *)&edata,
			sizeof(struct ethtool_eee));
		break;

	default:
		break;
	}

	return ACK;
}

/**
 * tc956xmac_pf_set_mac_filter
 *
 * \brief API to add the mac address filter value
 *
 * \details This function is used to add the mac address filter value
 * on VF request using mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] dev - Pointer to device
 * \param[in] mbx_buff - Pointer to message
 * \param[in] vf - VF no
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956xmac_pf_set_mac_filter(struct tc956xmac_priv *priv, struct net_device *dev,
				u8 *mbx_buff, u8 *ack_buff, u8 vf)
{
	u8 mac[SIZE_MBX_VF_MAC];
	int ret;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

	if (priv == NULL || mbx_buff == NULL || dev == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_VF_ADD_MAC)
		return NACK;

	if (mbx_buff[1] != SIZE_MBX_VF_MAC)
		return NACK;

	memcpy(&mac[0], &mbx_buff[2], SIZE_MBX_VF_MAC);
	/* Call core API to set the register */
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif
	ret = tc956x_pf_set_mac_filter(dev, vf, &mac[0]);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

	if (ret)
		return NACK;

	/* Clear the ACK buffer as there is no ACK message */
	ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
	memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);

	/* return ACK as all steps are successfull */
	return ACK;
}

/**
 * tc956xmac_pf_del_mac_filter
 *
 * \brief API to delete the mac address filter value
 *
 * \details This function is used to delete the mac address filter value
 * on VF request using mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] dev - Pointer to device
 * \param[in] mbx_buff - Pointer to message
 * \param[in] vf - VF no
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956xmac_pf_del_mac_filter(struct tc956xmac_priv *priv, struct net_device *dev,
					u8 *mbx_buff, u8 *ack_buff, u8 vf)
{

	u8 mac[SIZE_MBX_VF_MAC];
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

	if (priv == NULL || mbx_buff == NULL || dev == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_VF_DELETE_MAC)
		return NACK;

	if (mbx_buff[1] != SIZE_MBX_VF_MAC)
		return NACK;

	memcpy(&mac[0], &mbx_buff[2], SIZE_MBX_VF_MAC);
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif
	/* Call core API to set the register */
	tc956x_pf_del_mac_filter(dev, vf, &mac[0]);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

	/* Clear the ACK buffer as there is no ACK message */
	ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
	memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);

	/* return ACK as all steps are successfull */
	return ACK;
}

/**
 * tc956xmac_pf_set_vlan_filter
 *
 * \brief API to add the vlan ids filter value
 *
 * \details This function is used to add the vlan filter value
 * on VF request using mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] dev - Pointer to device
 * \param[in] mbx_buff - Pointer to message
 * \param[in] vf - VF no
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956xmac_pf_set_vlan_filter(struct tc956xmac_priv *priv, struct net_device *dev,
				u8 *mbx_buff, u8 *ack_buff, u8 vf)
{
	u16 vid;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	if (priv == NULL || mbx_buff == NULL || dev == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_VF_ADD_VLAN)
		return NACK;

	if (mbx_buff[1] != SIZE_MBX_VF_VLAN)
		return NACK;

	memcpy(&vid, &mbx_buff[2], SIZE_MBX_VF_VLAN);
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.vlan_filter, flags);
#endif

	/* Call core API to set the register */
	tc956x_pf_set_vlan_filter(dev, vf, vid);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.vlan_filter, flags);
#endif

	/* Clear the ACK buffer as there is no ACK message */
	ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
	memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);

	/* return ACK as all steps are successfull */
	return ACK;

}

/**
 * tc956xmac_pf_set_vlan_filter
 *
 * \brief API to add the vlan ids filter value
 *
 * \details This function is used to add the vlan filter value
 * on VF request using mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] dev - Pointer to device
 * \param[in] mbx_buff - Pointer to message
 * \param[in] vf - VF no
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956xmac_pf_del_vlan_filter(struct tc956xmac_priv *priv, struct net_device *dev,
						u8 *mbx_buff, u8 *ack_buff, u8 vf)
{
	u16 vid;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

	if (priv == NULL || mbx_buff == NULL || dev == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_VF_DELETE_VLAN)
		return NACK;

	if (mbx_buff[1] != SIZE_MBX_VF_VLAN)
		return NACK;

	memcpy(&vid, &mbx_buff[2], SIZE_MBX_VF_VLAN);
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.vlan_filter, flags);
#endif
	/* Call core API to set the register */
	tc956x_pf_del_vlan_filter(dev, vf, vid);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.vlan_filter, flags);
#endif
	/* Clear the ACK buffer as there is no ACK message */
	ack_buff[0] = OPCODE_MBX_ACK_MSG; /* set ACK opcode */
	memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);

	/* return ACK as all steps are successfull */
	return ACK;

}
/**
 * tc956x_mbx_rx_crc
 *
 * \brief API to send Rx CRC state to VFs
 *
 * \details This function is used to send Rx CRC states to VF using
 * mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 *
 * \return None
 */
static void tc956x_mbx_rx_crc(struct tc956xmac_priv *priv)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MAX_NO_OF_VFS][MBX_TOT_SIZE];
	int ret, i;
	enum mbx_msg_fns msg_dst;

	if (priv == NULL) {
		KPRINT_DEBUG1("NULL pointer error");
		return;
	}

	for (i = 0; i < MAX_NO_OF_VFS; i++) {

		mbx[i][0] = OPCODE_MBX_RX_CRC; /* opcode */
		mbx[i][1] = SIZE_MBX_RX_CRC; /* size */

	/* Copy Rx crc pad values */
		memcpy(&mbx[i][2], &priv->rx_crc_pad_state, SIZE_MBX_RX_CRC);
	}

	/* Send data to all VFs */
	for (i = vf0; i <= vf2; i++) {
	if (priv->clear_to_send[i-vf0] == VF_UP) {

		msg_dst = (enum mbx_msg_fns) i;
		ret = tc956xmac_mbx_write(priv, &mbx[i-vf0][0], msg_dst,
							&priv->fn_id_info);

		/* Validation of successfull message posting can be done
		 * by reading the mbx buffer for ACK opcode (0xFE)
		 */

		if (ret > 0)
			KPRINT_DEBUG1(
			"mailbox write with ACK or NACK %d msgbuff %x %x", ret, mbx[i-vf0][0], mbx[i-vf0][4]);
		else
			KPRINT_DEBUG1("mailbox write failed");
		} else
			KPRINT_DEBUG1("VF %d not up", i-vf0);
	}
}

/**
 * tc956x_mbx_rx_csum
 *
 * \brief API to send Rx checksum state to VFs
 *
 * \details This function is used to send Rx Checksum states to VF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 *
 * \return None
 */
static void tc956x_mbx_rx_csum(struct tc956xmac_priv *priv)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MAX_NO_OF_VFS][MBX_TOT_SIZE];
	int ret, i;
	enum mbx_msg_fns msg_dst;

	if (priv == NULL) {
		KPRINT_DEBUG1("NULL pointer error");
		return;
	}

	for (i = 0; i < MAX_NO_OF_VFS; i++) {
		mbx[i][0] = OPCODE_MBX_RX_CSUM; /* opcode */
		mbx[i][1] = SIZE_MBX_RX_CSUM; /* size */

		/* Copy link, speed and duplex values */
		memcpy(&mbx[i][2], &priv->rx_csum_state, SIZE_MBX_RX_CSUM);
	}

	/* Send data to all VFs */
	for (i = vf0; i <= vf2; i++) {
		if (priv->clear_to_send[i-vf0] == VF_UP) {
		msg_dst = (enum mbx_msg_fns) i;
		ret = tc956xmac_mbx_write(priv, &mbx[i-vf0][0], msg_dst,
							&priv->fn_id_info);

		/* Validation of successfull message posting can be done
		 * by reading the mbx buffer for ACK opcode (0xFE)
		 */
		if (ret > 0)
			KPRINT_DEBUG1(
			"mailbox write with ACK or NACK %d msgbuff %x %x\n", ret, mbx[i-vf0][0], mbx[i-vf0][4]);
		else
			KPRINT_DEBUG1("mailbox write failed");
		} else
			KPRINT_DEBUG1("VF %d not up", i-vf0);
	}
}

/**
 * tc956x_mbx_get_drv_cap
 *
 * \brief API to send PF driver featues state to VFs
 *
 * \details This function is used to send PF driver feature states to VF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mbx_buff - Pointer to message
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_get_drv_cap(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff)
{
	if (priv == NULL || mbx_buff == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_DRV_CAP)
		return NACK;

	if (mbx_buff[1] != 0)
		return NACK;

	priv->pf_drv_cap.crc_en = priv->rx_crc_pad_state;
	priv->pf_drv_cap.csum_en = priv->rx_csum_state;

	ack_buff[0] = OPCODE_MBX_ACK_MSG;
	ack_buff[1] = SIZE_MBX_DRV_CAP;
	/* Copy driver capability to ack buffer */
	memcpy(&ack_buff[2], &priv->pf_drv_cap, SIZE_MBX_DRV_CAP);

	return ACK;
}

/**
 * tc956x_mbx_get_umac_addr
 *
 * \brief API to send PF unicast mac addr to VFs
 *
 * \details This function is used to send PF unicast mac addr to VF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mbx_buff - Pointer to message
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_get_umac_addr(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff)
{
	unsigned char addr[SIZE_MBX_VF_MAC];
	unsigned int reg_n;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

	if (priv == NULL || mbx_buff == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_GET_UMAC_ADDR)
		return NACK;

	if (mbx_buff[1] != SIZE_GET_UMAC_ADDR)
		return NACK;

	memcpy(&reg_n, &mbx_buff[2], SIZE_GET_UMAC_ADDR);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif

	tc956xmac_get_umac_addr(priv, priv->hw, addr, reg_n);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

	ack_buff[0] = OPCODE_MBX_ACK_MSG;
	ack_buff[1] = SIZE_MBX_VF_MAC;

	memcpy(&ack_buff[2], addr, SIZE_MBX_VF_MAC);

	return ACK;
}

/**
 * tc956x_mbx_set_umac_addr
 *
 * \brief API to set unicast mac addr of VFs
 *
 * \details This function is used to send PF unicast mac addr to VF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mbx_buff - Pointer to message
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_set_umac_addr(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff, u8 vf)
{
	unsigned char addr[SIZE_MBX_VF_MAC];
	unsigned int reg_n;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

	if (priv == NULL || mbx_buff == NULL || ack_buff == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_ADD_MAC_ADDR)
		return NACK;

	if (mbx_buff[1] != SIZE_SET_UMAC_ADDR)
		return NACK;

	memcpy(addr, &mbx_buff[2], SIZE_MBX_VF_MAC);
	memcpy(&reg_n, &mbx_buff[8], 4);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif

	tc956xmac_set_umac_addr(priv, priv->hw, addr, reg_n, vf);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

	ack_buff[0] = OPCODE_MBX_ACK_MSG;
	memset(&ack_buff[1], 0, MBX_MSG_SIZE-1);

	return ACK;
}

/**
 * tc956x_mbx_reset_eee
 *
 * \brief API to reset eee
 *
 * \details This function is used to reset eee mode on VF request using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] mbx_buff - Pointer to message
 * \param[out] ack_msg - Pointer to output ack
 *
 * \return ACK/NACK
 */
static int tc956x_mbx_reset_eee_mode(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_msg)
{
	if (priv == NULL || mbx_buff == NULL || ack_msg == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_MBX_RESET_EEE_MODE)
		return NACK;

	if (mbx_buff[1] != 0)
		return NACK;

	tc956xmac_reset_eee_mode(priv, priv->hw);

	ack_msg[0] = OPCODE_MBX_ACK_MSG;
	memset(&ack_msg[1], 0, MBX_MSG_SIZE-1);

	return ACK;
}

/**
 * tc956xmac_mbx_vf_reset
 *
 * \brief API to send tbs sate change to VF
 *
 * \details This function is used to send TBS state change to VF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] ch - DMA channel no
 * \param[in] vf - VF of DMA channel
 *
 * \return 0 or error
 */
static int tc956xmac_mbx_vf_reset(struct tc956xmac_priv *priv, u8 *mbx_buff,
					u8 *ack_msg, u8 vf)
{
	struct tc956x_mac_addr *mac_table = &priv->mac_table[0];
	struct tc956x_vlan_id *vlan_table = &priv->vlan_table[0];
	u8 i, vf_number;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

	if (priv == NULL || mbx_buff == NULL || ack_msg == NULL) {
		KPRINT_DEBUG1("NULL pointer error\n");
		return NACK;
	}

	if (mbx_buff[0] != OPCODE_VF_RESET)
		return NACK;

	if (mbx_buff[1] != SIZE_VF_RESET)
		return NACK;

	if (mbx_buff[2] == VF_UP) {
		priv->clear_to_send[vf-1] = VF_UP;
		KPRINT_DEBUG1("P%d VF%d MBX State : UP\n", priv->port_num, vf-1);
	} else if (mbx_buff[2] == VF_DOWN) {
		priv->clear_to_send[vf-1] = VF_DOWN;

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
		spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif
		tc956x_pf_del_umac_addr(priv, HOST_MAC_ADDR_OFFSET + vf, vf);   //VF device address
		for (i = XGMAC_ADDR_ADD_SKIP_OFST; i < (TC956X_MAX_PERFECT_ADDRESSES);
			i++, mac_table++) {
			for (vf_number = 0; vf_number < 4; vf_number++) {
				if (mac_table->vf[vf_number] == vf)
					tc956x_pf_del_mac_filter(priv->dev, vf, (u8 *)&mac_table->mac_address);
			}
		}

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
		spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
		spin_lock_irqsave(&priv->spn_lock.vlan_filter, flags);
#endif
		for (i = 0; i < TC956X_MAX_PERFECT_VLAN; i++, vlan_table++) {
			for (vf_number = 0; vf_number < 4; vf_number++) {
				if (vlan_table->vf[vf_number].vf_number == vf)
					tc956x_pf_del_vlan_filter(priv->dev, vf, vlan_table->vid);
			}
		}
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
		spin_unlock_irqrestore(&priv->spn_lock.vlan_filter, flags);
#endif
		KPRINT_DEBUG1("P%d VF%d MBX State : DOWN\n", priv->port_num, vf-1);
	} else if (mbx_buff[2] == VF_SUSPEND || mbx_buff[2] == VF_RELEASE) {
		priv->clear_to_send[vf-1] = mbx_buff[2];

		if (mbx_buff[2] == VF_SUSPEND)
			KPRINT_DEBUG1("P%d VF%d MBX State : SUSPEND\n", priv->port_num, vf-1);
		else
			KPRINT_DEBUG1("P%d VF%d MBX State : RELEASE\n", priv->port_num, vf-1);
	}

	ack_msg[0] = OPCODE_MBX_ACK_MSG;
	memset(&ack_msg[1], 0, MBX_MSG_SIZE-1);

	return ACK;
}

/**
 * tc956x_mbx_setup_etf
 *
 * \brief API to send tbs sate change to VF
 *
 * \details This function is used to send TBS state change to VF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] ch - DMA channel no
 * \param[in] vf - VF of DMA channel
 *
 * \return 0 or error
 */
static int tc956x_mbx_setup_etf(struct tc956xmac_priv *priv, u32 ch, u8 vf)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MBX_TOT_SIZE];
	int ret = 0;
	enum mbx_msg_fns msg_dst;

	if (priv == NULL) {
		KPRINT_DEBUG1("NULL pointer error");
		return -EFAULT;
	}

	mbx[0] = OPCODE_MBX_SETUP_ETF; /* opcode */
	mbx[1] = SIZE_MBX_SETUP_ETF; /* size */

	memcpy(&mbx[2], &ch, 4);
	memcpy(&mbx[6], &priv->tx_queue[ch].tbs, 1);

	if (priv->clear_to_send[vf] == VF_UP) {
		msg_dst = (enum mbx_msg_fns)(vf + 3);

		ret = tc956xmac_mbx_write(priv, &mbx[0], msg_dst,
							&priv->fn_id_info);

		/* Validation of successfull message posting can be done
		 * by reading the mbx buffer for ACK opcode (0xFF)
		 */
		if (ret > 0)
			KPRINT_DEBUG1(
				"mailbox write with ACK or NACK %d msgbuff %x %x\n",
								ret, mbx[0], mbx[4]);
		else
			KPRINT_DEBUG1("mailbox write failed");
	} else
		KPRINT_DEBUG1("VF %d is not UP", vf);
	return ret;
}

/**
 * tc956x_mbx_flr
 *
 * \brief API to infor VF of PF FLR
 *
 * \details This function is used to inform VF about PF FLR using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 *
 * \return none
 */
static void tc956x_mbx_flr(struct tc956xmac_priv *priv)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MAX_NO_OF_VFS][MBX_TOT_SIZE];
	int ret, i;
	enum mbx_msg_fns msg_dst;

	if (priv == NULL) {
		KPRINT_DEBUG1("NULL pointer error");
		return;
	}

	for (i = 0; i < MAX_NO_OF_VFS; i++) {
		mbx[i][0] = OPCODE_MBX_FLR; /* opcode */
		mbx[i][1] = 0; /* size */
	}

	/* Send data to all VFs */
	for (i = vf0; i <= vf2; i++) {
		if (priv->clear_to_send[i-vf0] == VF_UP) {
			msg_dst = (enum mbx_msg_fns) i;
				ret = tc956xmac_mbx_write(priv, &mbx[i-vf0][0], msg_dst,
								&priv->fn_id_info);

			/* Validation of successfull message posting can be done
			 * by reading the mbx buffer for ACK opcode (0xFF)
			 */
			if (ret > 0) {
				priv->clear_to_send[i-vf0] = VF_DOWN;
				KPRINT_DEBUG1(
				"mailbox write with ACK or NACK %d msgbuff %x %x\n", ret, mbx[i-vf0][0], mbx[i-vf0][4]);
			} else
				KPRINT_DEBUG1("mailbox write failed");
		} else
			KPRINT_DEBUG1("VF %d not up", i-vf0);
	}
}

/**
 * tc956x_mbx_rx_dma_ch_tlptr
 *
 * \brief API to send DMA err information to VF
 *
 * \details This function is used to send DMA error info to VF using
 *		mailbox interface.
 *
 * \param[in] priv - Pointer to device's private structure
 * \param[in] vf - VF no of DMA channel
 *
 * \return 0 or error
 */
static int tc956x_mbx_rx_dma_err(struct tc956xmac_priv *priv, u8 vf)
{
	/* Prepare mailbox message and call mailbox API for posting
	 * and getting the ACK
	 */
	u8 mbx[MBX_TOT_SIZE];
	int ret = 0;
	enum mbx_msg_fns msg_dst;

	if (priv == NULL) {
		KPRINT_DEBUG1("NULL pointer error");
		return -EFAULT;
	}

	mbx[0] = OPCODE_MBX_DMA_ERR; /* opcode */
	mbx[1] = 0; /* size */

	if (priv->clear_to_send[vf] == VF_UP) {
		msg_dst = (enum mbx_msg_fns)(vf + 3);
		ret = tc956xmac_mbx_write(priv, &mbx[0], msg_dst,
							&priv->fn_id_info);

		/* Validation of successfull message posting can be done
		 * by reading the mbx buffer for ACK opcode (0xFF)
		 */

		if (ret > 0)
			KPRINT_DEBUG1(
				"mailbox write with ACK or NACK %d msgbuff %x %x\n",
								ret, mbx[0], mbx[4]);
		else
			KPRINT_DEBUG1("mailbox write failed\n");
	} else
		KPRINT_DEBUG1("VF %d is not UP", vf);

	return ret;
}

const struct tc956x_mbx_wrapper_ops tc956xmac_mbx_wrapper_ops = {
	.phy_link = tc956x_mbx_phy_link,
	.set_dma_tx_mode = tc956x_mbx_set_dma_tx_mode,
	.set_mtl_tx_queue_weight = tc956x_mbx_set_mtl_tx_queue_weight,
	.config_cbs = tc956x_mbx_config_cbs,
	.setup_cbs = tc956x_mbx_setup_cbs,
	.setup_mbx_etf = tc956x_mbx_setup_etf,
	.tx_queue_prio = tc956x_mbx_tx_queue_prio,
	.vf_get_link_status = tc956x_mbx_vf_get_link_status,
	.rx_crc = tc956x_mbx_rx_crc,
	.rx_csum = tc956x_mbx_rx_csum,
	.reset_eee_mode = tc956x_mbx_reset_eee_mode,
	.get_umac_addr = tc956x_mbx_get_umac_addr,
	.set_umac_addr = tc956x_mbx_set_umac_addr,
	.get_drv_cap = tc956x_mbx_get_drv_cap,
	.vf_ioctl = tc956xmac_pf_ioctl_interface,
	.vf_ethtool = tc956xmac_pf_ethtool_interface,
	.add_mac = tc956xmac_pf_set_mac_filter,
	.delete_mac = tc956xmac_pf_del_mac_filter,
	.add_vlan = tc956xmac_pf_set_vlan_filter,
	.delete_vlan = tc956xmac_pf_del_vlan_filter,
	.vf_reset = tc956xmac_mbx_vf_reset,
	.rx_dma_ch_tlptr = tc956x_mbx_rx_dma_ch_tlptr,
	.pf_flr = tc956x_mbx_flr,
	.rx_dma_err = tc956x_mbx_rx_dma_err,
};
#endif
