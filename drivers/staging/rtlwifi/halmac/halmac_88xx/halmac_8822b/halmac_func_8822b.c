// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#include "halmac_8822b_cfg.h"
#include "halmac_func_8822b.h"

/*SDIO RQPN Mapping*/
static struct halmac_rqpn_ HALMAC_RQPN_SDIO_8822B[] = {
	/* { mode, vo_map, vi_map, be_map, bk_map, mg_map, hi_map } */
	{HALMAC_TRX_MODE_NORMAL, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_TRXSHARE, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_WMM, HALMAC_MAP2_HQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_P2P, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_LOOPBACK, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
};

/*PCIE RQPN Mapping*/
static struct halmac_rqpn_ HALMAC_RQPN_PCIE_8822B[] = {
	/* { mode, vo_map, vi_map, be_map, bk_map, mg_map, hi_map } */
	{HALMAC_TRX_MODE_NORMAL, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_TRXSHARE, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_WMM, HALMAC_MAP2_HQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_P2P, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_LOOPBACK, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
};

/*USB 2 Bulkout RQPN Mapping*/
static struct halmac_rqpn_ HALMAC_RQPN_2BULKOUT_8822B[] = {
	/* { mode, vo_map, vi_map, be_map, bk_map, mg_map, hi_map } */
	{HALMAC_TRX_MODE_NORMAL, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_TRXSHARE, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_WMM, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_P2P, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_LOOPBACK, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_NQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_NQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
};

/*USB 3 Bulkout RQPN Mapping*/
static struct halmac_rqpn_ HALMAC_RQPN_3BULKOUT_8822B[] = {
	/* { mode, vo_map, vi_map, be_map, bk_map, mg_map, hi_map } */
	{HALMAC_TRX_MODE_NORMAL, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_TRXSHARE, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_LQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_WMM, HALMAC_MAP2_HQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_P2P, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_LOOPBACK, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_NQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_NQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
};

/*USB 4 Bulkout RQPN Mapping*/
static struct halmac_rqpn_ HALMAC_RQPN_4BULKOUT_8822B[] = {
	/* { mode, vo_map, vi_map, be_map, bk_map, mg_map, hi_map } */
	{HALMAC_TRX_MODE_NORMAL, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_TRXSHARE, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_WMM, HALMAC_MAP2_HQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_P2P, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_LOOPBACK, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_LQ, HALMAC_MAP2_LQ, HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
};

/*SDIO Page Number*/
static struct halmac_pg_num_ HALMAC_PG_NUM_SDIO_8822B[] = {
	/* { mode, hq_num, nq_num, lq_num, exq_num, gap_num} */
	{HALMAC_TRX_MODE_NORMAL, 64, 64, 64, 64, 1},
	{HALMAC_TRX_MODE_TRXSHARE, 32, 32, 32, 32, 1},
	{HALMAC_TRX_MODE_WMM, 64, 64, 64, 64, 1},
	{HALMAC_TRX_MODE_P2P, 64, 64, 64, 64, 1},
	{HALMAC_TRX_MODE_LOOPBACK, 64, 64, 64, 64, 640},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, 64, 64, 64, 64, 640},
};

/*PCIE Page Number*/
static struct halmac_pg_num_ HALMAC_PG_NUM_PCIE_8822B[] = {
	/* { mode, hq_num, nq_num, lq_num, exq_num, gap_num} */
	{HALMAC_TRX_MODE_NORMAL, 64, 64, 64, 64, 1},
	{HALMAC_TRX_MODE_TRXSHARE, 64, 64, 64, 64, 1},
	{HALMAC_TRX_MODE_WMM, 64, 64, 64, 64, 1},
	{HALMAC_TRX_MODE_P2P, 64, 64, 64, 64, 1},
	{HALMAC_TRX_MODE_LOOPBACK, 64, 64, 64, 64, 640},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, 64, 64, 64, 64, 640},
};

/*USB 2 Bulkout Page Number*/
static struct halmac_pg_num_ HALMAC_PG_NUM_2BULKOUT_8822B[] = {
	/* { mode, hq_num, nq_num, lq_num, exq_num, gap_num} */
	{HALMAC_TRX_MODE_NORMAL, 64, 64, 0, 0, 1},
	{HALMAC_TRX_MODE_TRXSHARE, 64, 64, 0, 0, 1},
	{HALMAC_TRX_MODE_WMM, 64, 64, 0, 0, 1},
	{HALMAC_TRX_MODE_P2P, 64, 64, 0, 0, 1},
	{HALMAC_TRX_MODE_LOOPBACK, 64, 64, 0, 0, 1024},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, 64, 64, 0, 0, 1024},
};

/*USB 3 Bulkout Page Number*/
static struct halmac_pg_num_ HALMAC_PG_NUM_3BULKOUT_8822B[] = {
	/* { mode, hq_num, nq_num, lq_num, exq_num, gap_num} */
	{HALMAC_TRX_MODE_NORMAL, 64, 64, 64, 0, 1},
	{HALMAC_TRX_MODE_TRXSHARE, 64, 64, 64, 0, 1},
	{HALMAC_TRX_MODE_WMM, 64, 64, 64, 0, 1},
	{HALMAC_TRX_MODE_P2P, 64, 64, 64, 0, 1},
	{HALMAC_TRX_MODE_LOOPBACK, 64, 64, 64, 0, 1024},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, 64, 64, 64, 0, 1024},
};

/*USB 4 Bulkout Page Number*/
static struct halmac_pg_num_ HALMAC_PG_NUM_4BULKOUT_8822B[] = {
	/* { mode, hq_num, nq_num, lq_num, exq_num, gap_num} */
	{HALMAC_TRX_MODE_NORMAL, 64, 64, 64, 64, 1},
	{HALMAC_TRX_MODE_TRXSHARE, 64, 64, 64, 64, 1},
	{HALMAC_TRX_MODE_WMM, 64, 64, 64, 64, 1},
	{HALMAC_TRX_MODE_P2P, 64, 64, 64, 64, 1},
	{HALMAC_TRX_MODE_LOOPBACK, 64, 64, 64, 64, 640},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, 64, 64, 64, 64, 640},
};

enum halmac_ret_status
halmac_txdma_queue_mapping_8822b(struct halmac_adapter *halmac_adapter,
				 enum halmac_trx_mode halmac_trx_mode)
{
	u16 value16;
	void *driver_adapter = NULL;
	struct halmac_rqpn_ *curr_rqpn_sel = NULL;
	enum halmac_ret_status status;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_SDIO) {
		curr_rqpn_sel = HALMAC_RQPN_SDIO_8822B;
	} else if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_PCIE) {
		curr_rqpn_sel = HALMAC_RQPN_PCIE_8822B;
	} else if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_USB) {
		if (halmac_adapter->halmac_bulkout_num == 2) {
			curr_rqpn_sel = HALMAC_RQPN_2BULKOUT_8822B;
		} else if (halmac_adapter->halmac_bulkout_num == 3) {
			curr_rqpn_sel = HALMAC_RQPN_3BULKOUT_8822B;
		} else if (halmac_adapter->halmac_bulkout_num == 4) {
			curr_rqpn_sel = HALMAC_RQPN_4BULKOUT_8822B;
		} else {
			pr_err("[ERR]interface not support\n");
			return HALMAC_RET_NOT_SUPPORT;
		}
	} else {
		return HALMAC_RET_NOT_SUPPORT;
	}

	status = halmac_rqpn_parser_88xx(halmac_adapter, halmac_trx_mode,
					 curr_rqpn_sel);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	value16 = 0;
	value16 |= BIT_TXDMA_HIQ_MAP(
		halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI]);
	value16 |= BIT_TXDMA_MGQ_MAP(
		halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG]);
	value16 |= BIT_TXDMA_BKQ_MAP(
		halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK]);
	value16 |= BIT_TXDMA_BEQ_MAP(
		halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE]);
	value16 |= BIT_TXDMA_VIQ_MAP(
		halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI]);
	value16 |= BIT_TXDMA_VOQ_MAP(
		halmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO]);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_TXDMA_PQ_MAP, value16);

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
halmac_priority_queue_config_8822b(struct halmac_adapter *halmac_adapter,
				   enum halmac_trx_mode halmac_trx_mode)
{
	u8 transfer_mode = 0;
	u8 value8;
	u32 counter;
	enum halmac_ret_status status;
	struct halmac_pg_num_ *curr_pg_num = NULL;
	void *driver_adapter = NULL;
	struct halmac_api *halmac_api;

	driver_adapter = halmac_adapter->driver_adapter;
	halmac_api = (struct halmac_api *)halmac_adapter->halmac_api;

	if (halmac_adapter->txff_allocation.la_mode == HALMAC_LA_MODE_DISABLE) {
		if (halmac_adapter->txff_allocation.rx_fifo_expanding_mode ==
		    HALMAC_RX_FIFO_EXPANDING_MODE_DISABLE) {
			halmac_adapter->txff_allocation.tx_fifo_pg_num =
				HALMAC_TX_FIFO_SIZE_8822B >>
				HALMAC_TX_PAGE_SIZE_2_POWER_8822B;
		} else if (halmac_adapter->txff_allocation
				   .rx_fifo_expanding_mode ==
			   HALMAC_RX_FIFO_EXPANDING_MODE_1_BLOCK) {
			halmac_adapter->txff_allocation.tx_fifo_pg_num =
				HALMAC_TX_FIFO_SIZE_EX_1_BLK_8822B >>
				HALMAC_TX_PAGE_SIZE_2_POWER_8822B;
			halmac_adapter->hw_config_info.tx_fifo_size =
				HALMAC_TX_FIFO_SIZE_EX_1_BLK_8822B;
			if (HALMAC_RX_FIFO_SIZE_EX_1_BLK_8822B <=
			    HALMAC_RX_FIFO_SIZE_EX_1_BLK_MAX_8822B)
				halmac_adapter->hw_config_info.rx_fifo_size =
					HALMAC_RX_FIFO_SIZE_EX_1_BLK_8822B;
			else
				halmac_adapter->hw_config_info.rx_fifo_size =
					HALMAC_RX_FIFO_SIZE_EX_1_BLK_MAX_8822B;
		} else {
			halmac_adapter->txff_allocation.tx_fifo_pg_num =
				HALMAC_TX_FIFO_SIZE_8822B >>
				HALMAC_TX_PAGE_SIZE_2_POWER_8822B;
			pr_err("[ERR]rx_fifo_expanding_mode = %d not support\n",
			       halmac_adapter->txff_allocation
				       .rx_fifo_expanding_mode);
		}
	} else {
		halmac_adapter->txff_allocation.tx_fifo_pg_num =
			HALMAC_TX_FIFO_SIZE_LA_8822B >>
			HALMAC_TX_PAGE_SIZE_2_POWER_8822B;
	}
	halmac_adapter->txff_allocation.rsvd_pg_num =
		(halmac_adapter->txff_allocation.rsvd_drv_pg_num +
		 HALMAC_RSVD_H2C_EXTRAINFO_PGNUM_8822B +
		 HALMAC_RSVD_H2C_QUEUE_PGNUM_8822B +
		 HALMAC_RSVD_CPU_INSTRUCTION_PGNUM_8822B +
		 HALMAC_RSVD_FW_TXBUFF_PGNUM_8822B);
	if (halmac_adapter->txff_allocation.rsvd_pg_num >
	    halmac_adapter->txff_allocation.tx_fifo_pg_num)
		return HALMAC_RET_CFG_TXFIFO_PAGE_FAIL;

	halmac_adapter->txff_allocation.ac_q_pg_num =
		halmac_adapter->txff_allocation.tx_fifo_pg_num -
		halmac_adapter->txff_allocation.rsvd_pg_num;
	halmac_adapter->txff_allocation.rsvd_pg_bndy =
		halmac_adapter->txff_allocation.tx_fifo_pg_num -
		halmac_adapter->txff_allocation.rsvd_pg_num;
	halmac_adapter->txff_allocation.rsvd_fw_txbuff_pg_bndy =
		halmac_adapter->txff_allocation.tx_fifo_pg_num -
		HALMAC_RSVD_FW_TXBUFF_PGNUM_8822B;
	halmac_adapter->txff_allocation.rsvd_cpu_instr_pg_bndy =
		halmac_adapter->txff_allocation.rsvd_fw_txbuff_pg_bndy -
		HALMAC_RSVD_CPU_INSTRUCTION_PGNUM_8822B;
	halmac_adapter->txff_allocation.rsvd_h2c_queue_pg_bndy =
		halmac_adapter->txff_allocation.rsvd_cpu_instr_pg_bndy -
		HALMAC_RSVD_H2C_QUEUE_PGNUM_8822B;
	halmac_adapter->txff_allocation.rsvd_h2c_extra_info_pg_bndy =
		halmac_adapter->txff_allocation.rsvd_h2c_queue_pg_bndy -
		HALMAC_RSVD_H2C_EXTRAINFO_PGNUM_8822B;
	halmac_adapter->txff_allocation.rsvd_drv_pg_bndy =
		halmac_adapter->txff_allocation.rsvd_h2c_extra_info_pg_bndy -
		halmac_adapter->txff_allocation.rsvd_drv_pg_num;

	if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_SDIO) {
		curr_pg_num = HALMAC_PG_NUM_SDIO_8822B;
	} else if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_PCIE) {
		curr_pg_num = HALMAC_PG_NUM_PCIE_8822B;
	} else if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_USB) {
		if (halmac_adapter->halmac_bulkout_num == 2) {
			curr_pg_num = HALMAC_PG_NUM_2BULKOUT_8822B;
		} else if (halmac_adapter->halmac_bulkout_num == 3) {
			curr_pg_num = HALMAC_PG_NUM_3BULKOUT_8822B;
		} else if (halmac_adapter->halmac_bulkout_num == 4) {
			curr_pg_num = HALMAC_PG_NUM_4BULKOUT_8822B;
		} else {
			pr_err("[ERR]interface not support\n");
			return HALMAC_RET_NOT_SUPPORT;
		}
	} else {
		return HALMAC_RET_NOT_SUPPORT;
	}

	status = halmac_pg_num_parser_88xx(halmac_adapter, halmac_trx_mode,
					   curr_pg_num);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_INFO_1,
			    halmac_adapter->txff_allocation.high_queue_pg_num);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_INFO_2,
			    halmac_adapter->txff_allocation.low_queue_pg_num);
	HALMAC_REG_WRITE_16(
		halmac_adapter, REG_FIFOPAGE_INFO_3,
		halmac_adapter->txff_allocation.normal_queue_pg_num);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_INFO_4,
			    halmac_adapter->txff_allocation.extra_queue_pg_num);
	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_INFO_5,
			    halmac_adapter->txff_allocation.pub_queue_pg_num);

	halmac_adapter->sdio_free_space.high_queue_number =
		halmac_adapter->txff_allocation.high_queue_pg_num;
	halmac_adapter->sdio_free_space.normal_queue_number =
		halmac_adapter->txff_allocation.normal_queue_pg_num;
	halmac_adapter->sdio_free_space.low_queue_number =
		halmac_adapter->txff_allocation.low_queue_pg_num;
	halmac_adapter->sdio_free_space.public_queue_number =
		halmac_adapter->txff_allocation.pub_queue_pg_num;
	halmac_adapter->sdio_free_space.extra_queue_number =
		halmac_adapter->txff_allocation.extra_queue_pg_num;

	HALMAC_REG_WRITE_32(
		halmac_adapter, REG_RQPN_CTRL_2,
		HALMAC_REG_READ_32(halmac_adapter, REG_RQPN_CTRL_2) | BIT(31));

	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_CTRL_2,
			    (u16)(halmac_adapter->txff_allocation.rsvd_pg_bndy &
				  BIT_MASK_BCN_HEAD_1_V1));
	HALMAC_REG_WRITE_16(halmac_adapter, REG_BCNQ_BDNY_V1,
			    (u16)(halmac_adapter->txff_allocation.rsvd_pg_bndy &
				  BIT_MASK_BCNQ_PGBNDY_V1));
	HALMAC_REG_WRITE_16(halmac_adapter, REG_FIFOPAGE_CTRL_2 + 2,
			    (u16)(halmac_adapter->txff_allocation.rsvd_pg_bndy &
				  BIT_MASK_BCN_HEAD_1_V1));
	HALMAC_REG_WRITE_16(halmac_adapter, REG_BCNQ1_BDNY_V1,
			    (u16)(halmac_adapter->txff_allocation.rsvd_pg_bndy &
				  BIT_MASK_BCNQ_PGBNDY_V1));

	HALMAC_REG_WRITE_32(halmac_adapter, REG_RXFF_BNDY,
			    halmac_adapter->hw_config_info.rx_fifo_size -
				    HALMAC_C2H_PKT_BUF_8822B - 1);

	if (halmac_adapter->halmac_interface == HALMAC_INTERFACE_USB) {
		value8 = (u8)(
			HALMAC_REG_READ_8(halmac_adapter, REG_AUTO_LLT_V1) &
			~(BIT_MASK_BLK_DESC_NUM << BIT_SHIFT_BLK_DESC_NUM));
		value8 = (u8)(value8 | (HALMAC_BLK_DESC_NUM_8822B
					<< BIT_SHIFT_BLK_DESC_NUM));
		HALMAC_REG_WRITE_8(halmac_adapter, REG_AUTO_LLT_V1, value8);

		HALMAC_REG_WRITE_8(halmac_adapter, REG_AUTO_LLT_V1 + 3,
				   HALMAC_BLK_DESC_NUM_8822B);
		HALMAC_REG_WRITE_8(halmac_adapter, REG_TXDMA_OFFSET_CHK + 1,
				   HALMAC_REG_READ_8(halmac_adapter,
						     REG_TXDMA_OFFSET_CHK + 1) |
					   BIT(1));
	}

	HALMAC_REG_WRITE_8(
		halmac_adapter, REG_AUTO_LLT_V1,
		(u8)(HALMAC_REG_READ_8(halmac_adapter, REG_AUTO_LLT_V1) |
		     BIT_AUTO_INIT_LLT_V1));
	counter = 1000;
	while (HALMAC_REG_READ_8(halmac_adapter, REG_AUTO_LLT_V1) &
	       BIT_AUTO_INIT_LLT_V1) {
		counter--;
		if (counter == 0)
			return HALMAC_RET_INIT_LLT_FAIL;
	}

	if (halmac_trx_mode == HALMAC_TRX_MODE_DELAY_LOOPBACK) {
		transfer_mode = HALMAC_TRNSFER_LOOPBACK_DELAY;
		HALMAC_REG_WRITE_16(
			halmac_adapter, REG_WMAC_LBK_BUF_HD_V1,
			(u16)halmac_adapter->txff_allocation.rsvd_pg_bndy);
	} else if (halmac_trx_mode == HALMAC_TRX_MODE_LOOPBACK) {
		transfer_mode = HALMAC_TRNSFER_LOOPBACK_DIRECT;
	} else {
		transfer_mode = HALMAC_TRNSFER_NORMAL;
	}

	HALMAC_REG_WRITE_8(halmac_adapter, REG_CR + 3, (u8)transfer_mode);

	return HALMAC_RET_SUCCESS;
}
