/*
 * TC956X ethernet driver.
 *
 * tc956x_vf_mbx.c
 *
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
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
 */

#include "tc956x_vf_mbx.h"
#include "tc956xmac.h"
#include "tc956x_vf_rsc_mng.h"

#ifdef TC956X_SRIOV_VF

/**
 * This table maintains the SRAM mapping for PF0->VF0s, VF0s->PF0, PF1->VF1s,
 *  VF1s->PF1, PF0->PF1, PF1->PF0, MCU->PF0, PF0->MCU, PF1->MCU, MCU->PF1
 * Each communication SRAM size is of 64 bytes. In total 30 such SRAMs are
 * required for above mentioned communication path.
 * In general, in Design of HW semaphore: PF uses one SRAM location for
 * any request and ACK message to a particular VF and VF uses other
 * SRAM location for requestand ACK. In Design of No semaphore: PF use one
 * SRAM location to send request, VF to use same location (overwrite) to send
 * the ACK, similarly VF uses other SRAM to send request, PF to use same location
 * (overwrite) to send the ACK.
 */
static u8 PF_VF_mbx_idx[PFS_MAX][VFNS_MAX][2] = {
	{
		{19, 20}, /* MCU <--> VF00 [MCU RQST Msg/MCU ACK Msg,
			   * VF00 ACK Msg/VF00 RQST Msg]
			   */
		{21, 22}, /* MCU <--> VF01 [MCU RQST Msg/MCU ACK Msg,
			   * VF01 ACK Msg/VF01 RQST Msg]
			   */
		{23, 24}, /* MCU <--> VF02 [MCU RQST Msg/MCU ACK Msg,
			   * VF02 ACK Msg/VF02 RQST Msg]
			   */
	},
	{
		{5, 6}, /* PF0 <--> VF00 [PF0 RQST Msg/PF0 ACK Msg,
			 * VF0 ACK Msg/VF0 RQST Msg]
			 */
		{7, 8}, /* PF0 <--> VF01 [PF0 RQST Msg/PF0 ACK Msg,
			 * VF1 ACK Msg/VF1 RQST Msg]
			 */
		{9, 10} /* PF0 <--> VF02 [PF0 RQST Msg/PF0 ACK Msg,
			 * VF2 ACK Msg/VF2 RQST Msg]
			 */
	},
	{
		{25, 26}, /* MCU <--> VF10 [MCU RQST Msg/MCU ACK Msg,
			   * VF10 ACK Msg/VF10 RQST Msg]
			   */
		{27, 28}, /* MCU <--> VF11 [MCU RQST Msg/MCU ACK Msg,
			   * VF11 ACK Msg/VF11 RQST Msg]
			   */
		{29, 30}  /* MCU <--> VF12 [MCU RQST Msg/MCU ACK Msg,
			   * MCU VF12 Msg/VF12 RQST Msg]
			   */
	},
	{
		{13, 14}, /* PF1 <--> VF10 [PF1 RQST Msg/PF1 ACK Msg,
			   * VF0 ACK Msg/VF0 RQST Msg]
			   */
		{15, 16}, /* PF2 <--> VF11 [PF1 RQST Msg/PF1 ACK Msg,
			   * VF1 ACK Msg/VF1 RQST Msg]
			   */
		{17, 18}  /* PF3 <--> VF12 [PF1 RQST Msg/PF1 ACK Msg,
			   * VF2 ACK Msg/VF2 RQST Msg]
			   */
	},

};

/**
 * vf_get_mbx_mem_idx
 *
 * \brief API to return pointer pointing to SRAM index
 *
 * \details This function to return the pointer to the 1D array of
 * PF_VF_mbx_idx where it points to SRAM location of specific PF->Fn
 *
 * \param[in] msg_src - Function to/from where PF needs to write/read
 * \param[in] fn_id_info - Pointer to this function id information
 *
 * \return pointer to the SRAM index
 */

static u8 *vf_get_mbx_mem_idx(enum mbx_msg_fns msg_src,
			      struct fn_id *fn_id_info)
{
	/* for MCU interrupt for VFs associated with pf1, below handling
	 * is required as it is not possible to get correct offset from
	 * the array with the other approach
	 */
	if ((msg_src == mcu) && (fn_id_info->pf_no == 1))
		return *(*(PF_VF_mbx_idx + 2) + (fn_id_info->vf_no - 1));
	else
		return *(*(PF_VF_mbx_idx + (msg_src + fn_id_info->pf_no)) +
			 (fn_id_info->vf_no - 1));
}

/**
 * tc956x_vf_get_fn_idx_from_int_sts
 *
 * \brief Helper function to get function that raised the mailbox interrupt
 *
 * \details This function is called to get the function which raised the
 * mailbox interrupt. Interrupt source can be MCU or associated PF
 *
 * \param[in] msg_dst - Function to which interrupt should be set
 * \param[in] fn_id_info - Pointer to Function info structure
 *
 * \return function type or error
 */
int tc956x_vf_get_fn_idx_from_int_sts(struct tc956xmac_priv *priv,
					     struct fn_id *fn_id_info)
{
	void __iomem *ioaddr = priv->tc956x_BRIDGE_CFG_pci_base_addr;
	u32 rsc_mng_int_sts = readl(ioaddr + RSCMNG_INT_ST_REG);

	if ((rsc_mng_int_sts & RSC_MNG_INT_MCU_MASK) == RSC_MNG_INT_MCU_MASK) {
		writel(RSC_MNG_INT_MCU_MASK, ioaddr + RSCMNG_INT_ST_REG);
		return mcu;
	} else if ((rsc_mng_int_sts & RSC_MNG_INT_OTHR_PF_MASK) ==
		 RSC_MNG_INT_OTHR_PF_MASK) {
		if (fn_id_info->pf_no == 0) {
			writel(RSC_MNG_INT_OTHR_PF_MASK, ioaddr + RSCMNG_INT_ST_REG);
			return pf0;
		} else if (fn_id_info->pf_no == 1) {
			writel(RSC_MNG_INT_OTHR_PF_MASK, ioaddr + RSCMNG_INT_ST_REG);
			return pf1;
		} else
			return -1;
	} else /* Other interrupts are not supported */
		return -1;
}

static int trylock_semaphore(void __iomem *ioaddr, u8 idx)
{
	u32 rd_val = 0;

	/* Read 0 for lock. Else already semaphore is locked
	 * curr value : action  :  next value
	 * 0	      : read  0 :     1
	 * 1	      : read  1 :     1
	 */

	rd_val = readl(ioaddr + NSEM(idx));
	if (rd_val == 0)
		return 0;
	else
		return -EBUSY;
}

static int unlock_semaphore(void __iomem *ioaddr, u8 idx)
{
	u32 wr_val = 1;

	/* Write 1 to unlock
	 * curr value : action  :  next value
	 * 0	      : write 0 :      0
	 * 0	      : write 1 :      0
	 * 1	      : write 0 :      1
	 * 1	      : write 1 :      0
	 */
	writel(wr_val, ioaddr + NSEM(idx));
	return 0;
}

/**
 * tc956x_vf_parse_mbx
 *
 * \brief API to read and process the VF mailbox
 *
 * \details This function should be called from ISR to parse the mailbox for
 * the mail received from particular function. Message will be processed and
 * ACK/NACK will be sent in this function
 *
 * \param[in] priv - Pointer to device private structure
 * \param[in] msg_src - Function to which ACK should be posted
 *
 * \return None
 */
void tc956x_vf_parse_mbx(struct tc956xmac_priv *priv,
				enum mbx_msg_fns msg_src)
{
	u8 msg_buff[MBX_TOT_SIZE];

	/* Get the function id information and read the mailbox.
	 * Function id can be get earlier too
	 */
	if (!tc956xmac_rsc_mng_get_fn_id(priv,
		priv->tc956x_BRIDGE_CFG_pci_base_addr, &priv->fn_id_info)) {
		/* Read and acknowledge the mailbox */
		tc956xmac_mbx_read(priv, msg_buff, msg_src,
				   &priv->fn_id_info);
	}
}

/**
 * tc956x_vf_check_for_ack
 *
 * \brief Helper function to check for ACK
 *
 * \details This function is called from polling function to check if
 * any ACK or NACK is received after writing in the mailbox
 *
 * \param[in] dev - Pointer to device structure
 * \param[in] msg_dst - Function from which ACK/NACK should be read
 * \param[in] fn_id_info - Pointer to Function info structure
 *
 * \return ACK/NACK or error
 */
static int tc956x_vf_check_for_ack(struct tc956xmac_priv *priv,
				   enum mbx_msg_fns msg_dst,
				   struct fn_id *fn_id_info)
{
	u8 *ptr_mail_box_idx;
	u8 ack_buff[MBX_ACK_SIZE];

	/* Read the correct mailbox access index as per
	 * the destination function
	 */
	ptr_mail_box_idx = vf_get_mbx_mem_idx(msg_dst, fn_id_info);

	/* Copy the ACK inidication data (first 4 bytes) from PF->VF mailbox */
	memcpy_fromio(ack_buff,
	       (priv->tc956x_SRAM_mailbox_base_addr +
		      ((*ptr_mail_box_idx + VF_READ_ACK_OFST) * MBX_TOT_SIZE)),
	       MBX_ACK_SIZE);

	if ((ack_buff[0] & RSC_MNG_ACK_MASK) == RSC_MNG_ACK_MASK)
		return ACK;
	else if ((ack_buff[0] & RSC_MNG_NACK_MASK) == RSC_MNG_NACK_MASK)
		return NACK;
	else
		return -1;
}

/**
 * tc956x_vf_mbx_init
 *
 * \brief API to initialise mailbox memory and parameters
 *
 * \details This function initialise mailbox memory used by this function.
 * Also to initialise parameters used for mailbox control.
 *
 * \param[in] ndev - Pointer to device structure
 *
 * \return None
 */

static void tc956x_vf_mbx_init(struct tc956xmac_priv *priv, void *data)
{
	priv->tc956x_SRAM_mailbox_base_addr = priv->tc956x_SRAM_pci_base_addr + VF_MBX_SRAM_ADDR;
}

/**
 * tc956x_vf_mbx_poll_for_ack
 *
 * \brief API to poll for acknowledgement
 *
 * \details This function will poll for acknowledgement after writing to the
 * mailbox memory. Polling is done by means of reading a
 * resource manager register
 *
 * \param[in] priv - Pointer to device private structure
 * \param[in] msg_dst - Function from which ACK should be polled
 *
 * \return None
 */
static int tc956x_vf_mbx_poll_for_ack(struct tc956xmac_priv *priv,
				      enum mbx_msg_fns msg_dst)
{

	int countdown = MBX_TIMEOUT;
	int ack_status;

	ack_status =
		tc956x_vf_check_for_ack(priv, msg_dst, &priv->fn_id_info);

	while (countdown && (ack_status < 0)) {
		countdown--;
		udelay(1000);
		ack_status = tc956x_vf_check_for_ack(priv, msg_dst,
						     &priv->fn_id_info);
	}

	return ack_status;
}

/**
 * tc956x_vf_mbx_send_ack
 *
 * \brief API to send acknowledgement
 *
 * \details This function will post the acknowledgement message
 * to the mailbox memory.
 * Basically this should be called after every read
 *
 * \param[in] priv - Pointer to device private structure
 * \param[in] msg_buff - Pointer to buffer having the acknowledgement message
 * \param[in] msg_dst - Function to which ACK should be posted
 * \param[in] fn_id_info - Pointer to function id information
 *
 * \return None
 */
static void tc956x_vf_mbx_send_ack(struct tc956xmac_priv *priv, u8 *msg_buff,
				  enum mbx_msg_fns msg_dst,
				  struct fn_id *fn_id_info)
{
	u8 *ptr_mail_box_idx;

	/* Read the correct mailbox access index as per
	 * the destination function
	 */
	ptr_mail_box_idx = vf_get_mbx_mem_idx(msg_dst, fn_id_info);

	/* Copy the ACK message data to PF->VF SRAM area.
	 * Also first 4 bytes of ACK indications also copied here
	 */
	memcpy_toio((priv->tc956x_SRAM_mailbox_base_addr +
		      ((*ptr_mail_box_idx + VF_SEND_ACK_OFST) * MBX_TOT_SIZE)),
	       msg_buff, MBX_TOT_SIZE);

	priv->xstats.mbx_vf_sent_pf++;
}

/**
 * tc956x_vf_trigger_interrupt
 *
 * \brief Helper function to set mailbox interrupt bit
 *
 * \details This function is called to set the mailbox interrupt bits while
 * writing the mailbox. Interrupt targets can be MCU, VFs or other PF
 *
 * \param[in] msg_dst - Function to which interrupt should be set
 *
 * \return None
 */
static void tc956x_vf_trigger_interrupt(struct tc956xmac_priv *priv,
					enum mbx_msg_fns msg_dst)
{
	void __iomem *ioaddr = priv->tc956x_BRIDGE_CFG_pci_base_addr;
	u32 rsc_mng_interrupt_ctl = readl(ioaddr + RSCMNG_INT_CTRL_REG);

	if (mcu == msg_dst)
		rsc_mng_interrupt_ctl |= MBX_MCU_INTERRUPT;
	else if ((pf0 == msg_dst) || (pf1 == msg_dst))
		rsc_mng_interrupt_ctl |= MBX_PF_INTERRUPT;

	writel(rsc_mng_interrupt_ctl, ioaddr + RSCMNG_INT_CTRL_REG);
}

/**
 * tc956x_vf_mbx_write
 *
 * \brief API to write a message to particular mailbox (sram)
 *
 * \details This function will post the request message to the mailbox memory.
 *
 * \param[in] priv - Pointer to device private structure
 * \param[in] msg_buff - Pointer to buffer having the request message
 * Skip the first 4 bytes while preparing data in this buffer
 * \param[in] msg_dst - Function to which message should be posted
 * \param[in] fn_id_info - Pointer to function id information
 *
 * \return success or error code
 */
static int tc956x_vf_mbx_write(struct tc956xmac_priv *priv, u8 *msg_buff,
			       enum mbx_msg_fns msg_dst,
			       struct fn_id *fn_id_info)
{
	u8 *ptr_mail_box_idx;
	int ret;


	/* Read the correct mailbox access index as per
	 * the destination function
	 */
	ptr_mail_box_idx = vf_get_mbx_mem_idx(msg_dst, fn_id_info);

	/* Get the semaphore lock before udpating mailbox */
	ret = trylock_semaphore(priv->ioaddr, ((*ptr_mail_box_idx + 1) >> 1));
	if (ret != 0)
		return ret;

	/* Clear the mailbox including ACK area (first 4 bytes) */
	memset_io((priv->tc956x_SRAM_mailbox_base_addr +
		      ((*ptr_mail_box_idx + VF_SEND_RQST_OFST) * MBX_TOT_SIZE)),
	       0, MBX_TOT_SIZE);

	/* Copy the mailbox data from local buffer to VF->PF or VF->MCU
	 * destination mailbox SRAM memory
	 */
	memcpy_toio((priv->tc956x_SRAM_mailbox_base_addr +
	((*ptr_mail_box_idx + VF_SEND_RQST_OFST) * MBX_TOT_SIZE) + MBX_MSG_OFST),
	       msg_buff, MBX_MSG_SIZE);

	priv->xstats.mbx_vf_sent_pf++;

	/*Set the interrupt bit in resource manager interrupt register*/
	tc956x_vf_trigger_interrupt(priv, msg_dst);

	/*poll for ack/nack from the VF/MCU/Other PF*/
	ret = tc956xmac_mbx_poll_for_ack(priv, msg_dst);

	/* Read for ACK message */
	if (ret > 0) {
		/* msg_buff to be overwritten here with ACK notification data
		 * and ACK message data
		 */
		tc956xmac_mbx_read(priv, msg_buff, msg_dst,
				   &priv->fn_id_info);
	}

	unlock_semaphore(priv->ioaddr, ((*ptr_mail_box_idx + 1) >> 1));

	return ret;
}

/**
 * tc956x_vf_mbx_read
 *
 * \brief API to read a message from particular mailbox (sram)
 *
 * \details This function will read request message from the mailbox
 * memory. This function to be called from mailbox interrupt routine.
 * This API can be used in case of ACK message read as well.
 *
 * \param[in] priv - Pointer to device private structure
 * \param[in] msg_buff - Pointer to read the mailbox message of max size
 * 64 bytes
 * \param[in] msg_src - Function from which mailbox message should be read
 * \param[in] fn_id_info - Pointer to function id information
 *
 * \return success or error code
 */
static int tc956x_vf_mbx_read(struct tc956xmac_priv *priv, u8 *msg_buff,
			      enum mbx_msg_fns msg_src,
			      struct fn_id *fn_id_info)
{
	u8 *ptr_mail_box_idx;
	enum mbx_msg_fns msg_dst;
	u8 ack_msg[MBX_MSG_SIZE];
	u8 ret_val = 0;

	/* Read the correct mailbox access index as per
	 * the destination function
	 */
	ptr_mail_box_idx = vf_get_mbx_mem_idx(msg_src, fn_id_info);

	/* Copy the mailbox data from PF->VF mailbox*/
	memcpy_fromio(msg_buff,
	       (priv->tc956x_SRAM_mailbox_base_addr +
		      ((*ptr_mail_box_idx + VF_READ_RQST_OFST) * MBX_TOT_SIZE)),
	       MBX_TOT_SIZE);

	priv->xstats.mbx_vf_rcvd_pf++;

	switch (msg_buff[MBX_MSG_OFST]) {
	case OPCODE_MBX_PHY_LINK:
		ret_val = tc956x_phy_link(priv, &msg_buff[MBX_MSG_OFST], &ack_msg[0]);
		break;
	case OPCODE_MBX_RX_CSUM:
		ret_val =  tc956x_rx_csum(priv, &msg_buff[MBX_MSG_OFST], &ack_msg[0]);
		break;
	case OPCODE_MBX_RX_CRC:
		ret_val = tc956x_rx_crc(priv, &msg_buff[MBX_MSG_OFST], &ack_msg[0]);
		break;
	case OPCODE_MBX_DMA_CH_TLPTR:
		ret_val = tc956x_rx_dma_ch_tlptr(priv, &msg_buff[MBX_MSG_OFST], &ack_msg[0]);
		break;
	case OPCODE_MBX_SETUP_ETF:
		ret_val = tc956x_setup_mbx_etf(priv, &msg_buff[MBX_MSG_OFST], &ack_msg[0]);
		break;
	case OPCODE_MBX_FLR:
		ret_val = tc956x_mbx_flr(priv, &msg_buff[MBX_MSG_OFST], &ack_msg[0]);
		break;
	case OPCODE_MBX_DMA_ERR:
		ret_val = tc956x_rx_dma_err(priv, &msg_buff[MBX_MSG_OFST], &ack_msg[0]);
		break;
	case OPCODE_MBX_ACK_MSG:
		/* ACK message */
		memset_io((priv->tc956x_SRAM_mailbox_base_addr +
		      ((*ptr_mail_box_idx + VF_READ_RQST_OFST) * MBX_TOT_SIZE)), 0,
	       MBX_TOT_SIZE);
		break;
	default:
		break;
	}

	/* Send the Acknowldegment if message read is not ACK message */
	if (msg_buff[MBX_MSG_OFST] != OPCODE_MBX_ACK_MSG) {
		/* For sending ACK, source of request message becomes
		 * the destination
		 */
		msg_dst = msg_src;

		/* Prepare first 4 bytes of ACK inidication data */
		msg_buff[0] = ret_val;
		msg_buff[1] = 0x0;
		msg_buff[2] = 0x0;
		msg_buff[3] = 0x0;

		/* Prepare 60 bytes of acknowledgment message */
		memcpy(&msg_buff[MBX_MSG_OFST], ack_msg,
			MBX_MSG_SIZE);

		/* After successful parsing, ACK/NACK the functions which
		 * originated the message
		 */
		tc956xmac_mbx_send_ack(priv, msg_buff, msg_dst,
				       fn_id_info);
	}
	return 0;
}

const struct mac_mbx_ops tc956xmac_mbx_ops = {
	.init = tc956x_vf_mbx_init,
	.read = tc956x_vf_mbx_read,
	.write = tc956x_vf_mbx_write,
	.send_ack = tc956x_vf_mbx_send_ack,
	.poll_for_ack = tc956x_vf_mbx_poll_for_ack,

};
#endif /* #ifdef TC956X_SRIOV_VF */
