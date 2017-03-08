#include "halmac_8822b_cfg.h"

HALMAC_RET_STATUS
halmac_txdma_queue_mapping_8822b(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_TRX_MODE halmac_trx_mode
)
{
	u16 value16;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	/* Default mapping (PCIE + SDIO + USB 4 bulkout) */
	switch (halmac_trx_mode) {
	case HALMAC_TRX_MODE_NORMAL:
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] = HALMAC_DMA_MAPPING_NORMAL;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] = HALMAC_DMA_MAPPING_NORMAL;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] = HALMAC_DMA_MAPPING_LOW;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] = HALMAC_DMA_MAPPING_LOW;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] = HALMAC_DMA_MAPPING_EXTRA;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;
		break;
	case HALMAC_TRX_MODE_TRXSHARE:
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] = HALMAC_DMA_MAPPING_HIGH;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] = HALMAC_DMA_MAPPING_HIGH;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] = HALMAC_DMA_MAPPING_HIGH;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] = HALMAC_DMA_MAPPING_HIGH;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] = HALMAC_DMA_MAPPING_HIGH;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;
		break;
	case HALMAC_TRX_MODE_WMM:
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] = HALMAC_DMA_MAPPING_NORMAL;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] = HALMAC_DMA_MAPPING_NORMAL;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] = HALMAC_DMA_MAPPING_LOW;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] = HALMAC_DMA_MAPPING_LOW;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] = HALMAC_DMA_MAPPING_EXTRA;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;
		break;
	case HALMAC_TRX_MODE_P2P:
	case HALMAC_TRX_MODE_LOOPBACK:
	case HALMAC_TRX_MODE_DELAY_LOOPBACK:
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] = HALMAC_DMA_MAPPING_NORMAL;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] = HALMAC_DMA_MAPPING_NORMAL;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] = HALMAC_DMA_MAPPING_LOW;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] = HALMAC_DMA_MAPPING_LOW;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] = HALMAC_DMA_MAPPING_EXTRA;
		pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_init_trx_cfg_8822b 0 switch case not support\n");
		return HALMAC_RET_TRX_MODE_NOT_SUPPORT;
	}

	/* Extra mapping */
	if (HALMAC_INTERFACE_USB == pHalmac_adapter->halmac_interface) {
		if (pHalmac_adapter->halmac_bulkout_num == 2) {
			/* In USB 2 bulkout, only High and Normal queue can be used */
			switch (halmac_trx_mode) {
			case HALMAC_TRX_MODE_NORMAL:
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] = HALMAC_DMA_MAPPING_NORMAL;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] = HALMAC_DMA_MAPPING_NORMAL;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] = HALMAC_DMA_MAPPING_NORMAL;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;

				break;
			case HALMAC_TRX_MODE_TRXSHARE:
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;
				break;
			case HALMAC_TRX_MODE_WMM:
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] = HALMAC_DMA_MAPPING_NORMAL;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] = HALMAC_DMA_MAPPING_NORMAL;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] = HALMAC_DMA_MAPPING_NORMAL;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;
				break;
			case HALMAC_TRX_MODE_P2P:
			case HALMAC_TRX_MODE_LOOPBACK:
			case HALMAC_TRX_MODE_DELAY_LOOPBACK:
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] = HALMAC_DMA_MAPPING_NORMAL;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;

				break;
			default:
				PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_init_trx_cfg_8822b 1 switch case not support\n");
				return HALMAC_RET_TRX_MODE_NOT_SUPPORT;
			}
		} else if (pHalmac_adapter->halmac_bulkout_num == 3) {
			/* in USB 3 bulkout, only High, Normal, Low queue can be used */
			switch (halmac_trx_mode) {
			case HALMAC_TRX_MODE_NORMAL:
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] = HALMAC_DMA_MAPPING_NORMAL;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] = HALMAC_DMA_MAPPING_NORMAL;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] = HALMAC_DMA_MAPPING_LOW;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] = HALMAC_DMA_MAPPING_LOW;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;

				break;
			case HALMAC_TRX_MODE_TRXSHARE:
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;
				break;
			case HALMAC_TRX_MODE_WMM:
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] = HALMAC_DMA_MAPPING_NORMAL;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] = HALMAC_DMA_MAPPING_LOW;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] = HALMAC_DMA_MAPPING_NORMAL;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] = HALMAC_DMA_MAPPING_EXTRA;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;
				break;
			case HALMAC_TRX_MODE_P2P:
			case HALMAC_TRX_MODE_LOOPBACK:
			case HALMAC_TRX_MODE_DELAY_LOOPBACK:
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE] = HALMAC_DMA_MAPPING_LOW;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK] = HALMAC_DMA_MAPPING_NORMAL;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG] = HALMAC_DMA_MAPPING_HIGH;
				pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI] = HALMAC_DMA_MAPPING_HIGH;

				break;
			default:
				PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_init_trx_cfg_8822b 2 switch case not support\n");
				return HALMAC_RET_TRX_MODE_NOT_SUPPORT;
			}
		}
	}

	value16 = 0;
	value16 |= BIT_TXDMA_HIQ_MAP(pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI]);
	value16 |= BIT_TXDMA_MGQ_MAP(pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG]);
	value16 |= BIT_TXDMA_BKQ_MAP(pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK]);
	value16 |= BIT_TXDMA_BEQ_MAP(pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE]);
	value16 |= BIT_TXDMA_VIQ_MAP(pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI]);
	value16 |= BIT_TXDMA_VOQ_MAP(pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO]);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_TXDMA_PQ_MAP, value16);

	return HALMAC_RET_SUCCESS;
}

HALMAC_RET_STATUS
halmac_priority_queue_config_8822b(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_TRX_MODE halmac_trx_mode
)
{
	u8 transfer_mode = 0;
	u8 value8;
	u16 HPQ_num = 0, LPQ_Nnum = 0, NPQ_num = 0, GAPQ_num = 0;
	u16 EXPQ_num = 0, PUBQ_num = 0;
	u16 tx_page_boundary = 0, rx_f_ifo_boundary = 0;
	u16 h2c_extra_info_boundary = 0, fw_txbuff_boundary = 0;
	u32 counter;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	if (HALMAC_LA_MODE_DISABLE == pHalmac_adapter->txff_allocation.la_mode)
		pHalmac_adapter->txff_allocation.tx_fifo_pg_num = HALMAC_TX_FIFO_SIZE_8822B >> HALMAC_TX_PAGE_SIZE_2_POWER_8822B;
	else
		pHalmac_adapter->txff_allocation.tx_fifo_pg_num = HALMAC_TX_FIFO_SIZE_LA_8822B >> HALMAC_TX_PAGE_SIZE_2_POWER_8822B;

	pHalmac_adapter->txff_allocation.rsvd_pg_num = (pHalmac_adapter->txff_allocation.rsvd_drv_pg_num +
							HALMAC_RSVD_H2C_EXTRAINFO_PGNUM_8822B +
							HALMAC_RSVD_H2C_QUEUE_PGNUM_8822B +
							HALMAC_RSVD_CPU_INSTRUCTION_PGNUM_8822B +
							HALMAC_RSVD_FW_TXBUFF_PGNUM_8822B);
	if (pHalmac_adapter->txff_allocation.rsvd_pg_num > pHalmac_adapter->txff_allocation.tx_fifo_pg_num)
		return HALMAC_RET_CFG_TXFIFO_PAGE_FAIL;

	pHalmac_adapter->txff_allocation.ac_q_pg_num = pHalmac_adapter->txff_allocation.tx_fifo_pg_num - pHalmac_adapter->txff_allocation.rsvd_pg_num;
	pHalmac_adapter->txff_allocation.rsvd_pg_bndy = pHalmac_adapter->txff_allocation.tx_fifo_pg_num - pHalmac_adapter->txff_allocation.rsvd_pg_num;
	pHalmac_adapter->txff_allocation.rsvd_fw_txbuff_pg_bndy = pHalmac_adapter->txff_allocation.tx_fifo_pg_num - HALMAC_RSVD_FW_TXBUFF_PGNUM_8822B;
	pHalmac_adapter->txff_allocation.rsvd_cpu_instr_pg_bndy = pHalmac_adapter->txff_allocation.rsvd_fw_txbuff_pg_bndy - HALMAC_RSVD_CPU_INSTRUCTION_PGNUM_8822B;
	pHalmac_adapter->txff_allocation.rsvd_h2c_queue_pg_bndy = pHalmac_adapter->txff_allocation.rsvd_cpu_instr_pg_bndy - HALMAC_RSVD_H2C_QUEUE_PGNUM_8822B;
	pHalmac_adapter->txff_allocation.rsvd_h2c_extra_info_pg_bndy = pHalmac_adapter->txff_allocation.rsvd_h2c_queue_pg_bndy - HALMAC_RSVD_H2C_EXTRAINFO_PGNUM_8822B;
	pHalmac_adapter->txff_allocation.rsvd_drv_pg_bndy = pHalmac_adapter->txff_allocation.rsvd_h2c_extra_info_pg_bndy - pHalmac_adapter->txff_allocation.rsvd_drv_pg_num;

	/* Default setting (PCIE + SDIO + USB 4 bulkout) */
	switch (halmac_trx_mode) {
	case HALMAC_TRX_MODE_NORMAL:
		HPQ_num = HALMAC_NORMAL_HPQ_PGNUM_8822B;
		LPQ_Nnum = HALMAC_NORMAL_LPQ_PGNUM_8822B;
		NPQ_num = HALMAC_NORMAL_NPQ_PGNUM_8822B;
		EXPQ_num = HALMAC_NORMAL_EXPQ_PGNUM_8822B;
		GAPQ_num = HALMAC_NORMAL_GAP_PGNUM_8822B;
		PUBQ_num = pHalmac_adapter->txff_allocation.ac_q_pg_num - HPQ_num - LPQ_Nnum - NPQ_num - EXPQ_num - GAPQ_num;
		rx_f_ifo_boundary = HALMAC_RX_FIFO_SIZE_8822B - HALMAC_WOWLAN_PATTERN_SIZE_8822B - 1;
		transfer_mode = HALMAC_TRNSFER_NORMAL;
		break;
	case HALMAC_TRX_MODE_TRXSHARE:
		break;
	case HALMAC_TRX_MODE_WMM:
		HPQ_num = HALMAC_WMM_HPQ_PGNUM_8822B;
		LPQ_Nnum = HALMAC_WMM_LPQ_PGNUM_8822B;
		NPQ_num = HALMAC_WMM_NPQ_PGNUM_8822B;
		EXPQ_num = HALMAC_WMM_EXPQ_PGNUM_8822B;
		GAPQ_num = HALMAC_WMM_GAP_PGNUM_8822B;
		PUBQ_num = pHalmac_adapter->txff_allocation.ac_q_pg_num - HPQ_num - LPQ_Nnum - NPQ_num - EXPQ_num - GAPQ_num;
		rx_f_ifo_boundary = HALMAC_RX_FIFO_SIZE_8822B - HALMAC_WOWLAN_PATTERN_SIZE_8822B - 1;
		transfer_mode = HALMAC_TRNSFER_NORMAL;
		break;
	case HALMAC_TRX_MODE_P2P:
	case HALMAC_TRX_MODE_LOOPBACK:
	case HALMAC_TRX_MODE_DELAY_LOOPBACK:
		HPQ_num = HALMAC_LB_HPQ_PGNUM_8822B;
		LPQ_Nnum = HALMAC_LB_LPQ_PGNUM_8822B;
		NPQ_num = HALMAC_LB_NPQ_PGNUM_8822B;
		EXPQ_num = HALMAC_LB_EXPQ_PGNUM_8822B;
		GAPQ_num = HALMAC_LB_GAP_PGNUM_8822B;
		PUBQ_num = pHalmac_adapter->txff_allocation.ac_q_pg_num - HPQ_num - LPQ_Nnum - NPQ_num - EXPQ_num - GAPQ_num;
		rx_f_ifo_boundary = HALMAC_RX_FIFO_SIZE_8822B - 1;
		transfer_mode = HALMAC_TRNSFER_LOOPBACK_DIRECT;
		HALMAC_REG_WRITE_16(pHalmac_adapter, REG_WMAC_LBK_BUF_HD_V1, (u16)pHalmac_adapter->txff_allocation.rsvd_pg_bndy);
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_init_trx_cfg_8822b 3 switch case not support\n");
		return HALMAC_RET_TRX_MODE_NOT_SUPPORT;
	}

	/* Extra setting */
	if (HALMAC_INTERFACE_USB == pHalmac_adapter->halmac_interface) {
		if (pHalmac_adapter->halmac_bulkout_num == 2) {
			/* In USB 2 bulkout, only High and Normal queue can be used */
			switch (halmac_trx_mode) {
			case HALMAC_TRX_MODE_NORMAL:
				HPQ_num = HALMAC_NORMAL_2BULKOUT_HPQ_PGNUM_8822B;
				LPQ_Nnum = HALMAC_NORMAL_2BULKOUT_LPQ_PGNUM_8822B;
				NPQ_num = HALMAC_NORMAL_2BULKOUT_NPQ_PGNUM_8822B;
				EXPQ_num = HALMAC_NORMAL_2BULKOUT_EXPQ_PGNUM_8822B;
				GAPQ_num = HALMAC_NORMAL_2BULKOUT_GAP_PGNUM_8822B;
				PUBQ_num = pHalmac_adapter->txff_allocation.ac_q_pg_num - HPQ_num - LPQ_Nnum - NPQ_num - EXPQ_num - GAPQ_num;
				rx_f_ifo_boundary = HALMAC_RX_FIFO_SIZE_8822B - HALMAC_WOWLAN_PATTERN_SIZE_8822B - 1;
				transfer_mode = HALMAC_TRNSFER_NORMAL;
				break;
			case HALMAC_TRX_MODE_TRXSHARE:
				break;
			case HALMAC_TRX_MODE_WMM:
				HPQ_num = HALMAC_WMM_HPQ_PGNUM_8822B;
				LPQ_Nnum = HALMAC_WMM_LPQ_PGNUM_8822B;
				NPQ_num = HALMAC_WMM_NPQ_PGNUM_8822B;
				EXPQ_num = HALMAC_WMM_EXPQ_PGNUM_8822B;
				GAPQ_num = HALMAC_WMM_GAP_PGNUM_8822B;
				PUBQ_num = pHalmac_adapter->txff_allocation.ac_q_pg_num - HPQ_num - LPQ_Nnum - NPQ_num - EXPQ_num - GAPQ_num;
				rx_f_ifo_boundary = HALMAC_RX_FIFO_SIZE_8822B - HALMAC_WOWLAN_PATTERN_SIZE_8822B - 1;
				transfer_mode = HALMAC_TRNSFER_NORMAL;
				break;
			case HALMAC_TRX_MODE_P2P:
			case HALMAC_TRX_MODE_LOOPBACK:
			case HALMAC_TRX_MODE_DELAY_LOOPBACK:
				HPQ_num = HALMAC_LB_2BULKOUT_HPQ_PGNUM_8822B;
				LPQ_Nnum = HALMAC_LB_2BULKOUT_LPQ_PGNUM_8822B;
				NPQ_num = HALMAC_LB_2BULKOUT_NPQ_PGNUM_8822B;
				EXPQ_num = HALMAC_LB_2BULKOUT_EXPQ_PGNUM_8822B;
				GAPQ_num = HALMAC_LB_2BULKOUT_GAP_PGNUM_8822B;
				PUBQ_num = pHalmac_adapter->txff_allocation.ac_q_pg_num - HPQ_num - LPQ_Nnum - NPQ_num - EXPQ_num - GAPQ_num;
				rx_f_ifo_boundary = HALMAC_RX_FIFO_SIZE_8822B - 1;
				transfer_mode = HALMAC_TRNSFER_LOOPBACK_DIRECT;
				HALMAC_REG_WRITE_16(pHalmac_adapter, REG_WMAC_LBK_BUF_HD_V1, (u16)pHalmac_adapter->txff_allocation.rsvd_pg_bndy);
				break;
			default:
				PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_init_trx_cfg_8822b 4 switch case not support\n");
				return HALMAC_RET_TRX_MODE_NOT_SUPPORT;
			}
		} else if (pHalmac_adapter->halmac_bulkout_num == 3) {
			/* in USB 3 bulkout, only High, Normal, Low queue can be used */
			switch (halmac_trx_mode) {
			case HALMAC_TRX_MODE_NORMAL:
				HPQ_num = HALMAC_NORMAL_3BULKOUT_HPQ_PGNUM_8822B;
				LPQ_Nnum = HALMAC_NORMAL_3BULKOUT_LPQ_PGNUM_8822B;
				NPQ_num = HALMAC_NORMAL_3BULKOUT_NPQ_PGNUM_8822B;
				EXPQ_num = HALMAC_NORMAL_3BULKOUT_EXPQ_PGNUM_8822B;
				GAPQ_num = HALMAC_NORMAL_3BULKOUT_GAP_PGNUM_8822B;
				PUBQ_num = pHalmac_adapter->txff_allocation.ac_q_pg_num - HPQ_num - LPQ_Nnum - NPQ_num - EXPQ_num - GAPQ_num;
				rx_f_ifo_boundary = HALMAC_RX_FIFO_SIZE_8822B - HALMAC_WOWLAN_PATTERN_SIZE_8822B - 1;
				transfer_mode = HALMAC_TRNSFER_NORMAL;
				break;
			case HALMAC_TRX_MODE_TRXSHARE:
				break;
			case HALMAC_TRX_MODE_WMM:
				HPQ_num = HALMAC_WMM_HPQ_PGNUM_8822B;
				LPQ_Nnum = HALMAC_WMM_LPQ_PGNUM_8822B;
				NPQ_num = HALMAC_WMM_NPQ_PGNUM_8822B;
				EXPQ_num = HALMAC_WMM_EXPQ_PGNUM_8822B;
				GAPQ_num = HALMAC_WMM_GAP_PGNUM_8822B;
				PUBQ_num = pHalmac_adapter->txff_allocation.ac_q_pg_num - HPQ_num - LPQ_Nnum - NPQ_num - EXPQ_num - GAPQ_num;
				rx_f_ifo_boundary = HALMAC_RX_FIFO_SIZE_8822B - HALMAC_WOWLAN_PATTERN_SIZE_8822B - 1;
				transfer_mode = HALMAC_TRNSFER_NORMAL;
				break;
			case HALMAC_TRX_MODE_P2P:
			case HALMAC_TRX_MODE_LOOPBACK:
			case HALMAC_TRX_MODE_DELAY_LOOPBACK:
				HPQ_num = HALMAC_LB_3BULKOUT_HPQ_PGNUM_8822B;
				LPQ_Nnum = HALMAC_LB_3BULKOUT_LPQ_PGNUM_8822B;
				NPQ_num = HALMAC_LB_3BULKOUT_NPQ_PGNUM_8822B;
				EXPQ_num = HALMAC_LB_3BULKOUT_EXPQ_PGNUM_8822B;
				GAPQ_num = HALMAC_LB_3BULKOUT_GAP_PGNUM_8822B;
				PUBQ_num = pHalmac_adapter->txff_allocation.ac_q_pg_num - HPQ_num - LPQ_Nnum - NPQ_num - EXPQ_num - GAPQ_num;
				rx_f_ifo_boundary = HALMAC_RX_FIFO_SIZE_8822B - 1;
				transfer_mode = HALMAC_TRNSFER_LOOPBACK_DIRECT;
				HALMAC_REG_WRITE_16(pHalmac_adapter, REG_WMAC_LBK_BUF_HD_V1, (u16)pHalmac_adapter->txff_allocation.rsvd_pg_bndy);
				break;
			default:
				PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "halmac_init_trx_cfg_8822b 5 switch case not support\n");
				return HALMAC_RET_TRX_MODE_NOT_SUPPORT;
			}
		}
	}


	if (pHalmac_adapter->txff_allocation.ac_q_pg_num < HPQ_num + LPQ_Nnum + NPQ_num + EXPQ_num + GAPQ_num)
		return HALMAC_RET_CFG_TXFIFO_PAGE_FAIL;

	pHalmac_adapter->txff_allocation.high_queue_pg_num = HPQ_num;
	pHalmac_adapter->txff_allocation.low_queue_pg_num = LPQ_Nnum;
	pHalmac_adapter->txff_allocation.normal_queue_pg_num = NPQ_num;
	pHalmac_adapter->txff_allocation.extra_queue_pg_num = EXPQ_num;
	pHalmac_adapter->txff_allocation.pub_queue_pg_num = PUBQ_num;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Set FIFO page\n");

	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_FIFOPAGE_INFO_1, HPQ_num);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_FIFOPAGE_INFO_2, LPQ_Nnum);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_FIFOPAGE_INFO_3, NPQ_num);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_FIFOPAGE_INFO_4, EXPQ_num);
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_FIFOPAGE_INFO_5, PUBQ_num);

	pHalmac_adapter->sdio_free_space.high_queue_number = HPQ_num;
	pHalmac_adapter->sdio_free_space.normal_queue_number = NPQ_num;
	pHalmac_adapter->sdio_free_space.low_queue_number = LPQ_Nnum;
	pHalmac_adapter->sdio_free_space.public_queue_number = PUBQ_num;
	pHalmac_adapter->sdio_free_space.extra_queue_number = EXPQ_num;

	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_RQPN_CTRL_2, HALMAC_REG_READ_32(pHalmac_adapter, REG_RQPN_CTRL_2) | BIT(31));

	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_FIFOPAGE_CTRL_2, (u16)(pHalmac_adapter->txff_allocation.rsvd_pg_bndy & BIT_MASK_BCN_HEAD_1_V1));
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_BCNQ_BDNY_V1, (u16)(pHalmac_adapter->txff_allocation.rsvd_pg_bndy & BIT_MASK_BCNQ_PGBNDY_V1));
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_FIFOPAGE_CTRL_2 + 2, (u16)(pHalmac_adapter->txff_allocation.rsvd_pg_bndy & BIT_MASK_BCN_HEAD_1_V1));
	HALMAC_REG_WRITE_16(pHalmac_adapter, REG_BCNQ1_BDNY_V1, (u16)(pHalmac_adapter->txff_allocation.rsvd_pg_bndy & BIT_MASK_BCNQ_PGBNDY_V1));

	HALMAC_REG_WRITE_32(pHalmac_adapter, REG_RXFF_BNDY, rx_f_ifo_boundary);

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "Init LLT table\n");

	if (HALMAC_INTERFACE_USB == pHalmac_adapter->halmac_interface) {
		value8 = (u8)(HALMAC_REG_READ_8(pHalmac_adapter, REG_AUTO_LLT_V1) & ~(BIT_MASK_BLK_DESC_NUM << BIT_SHIFT_BLK_DESC_NUM));
		value8 = (u8)(value8 | (HALMAC_BLK_DESC_NUM_8822B << BIT_SHIFT_BLK_DESC_NUM));
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_AUTO_LLT_V1, value8);

		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_AUTO_LLT_V1 + 3, HALMAC_BLK_DESC_NUM_8822B);
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_TXDMA_OFFSET_CHK + 1, HALMAC_REG_READ_8(pHalmac_adapter, REG_TXDMA_OFFSET_CHK + 1) | BIT(1));
	}

	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_AUTO_LLT_V1, (u8)(HALMAC_REG_READ_8(pHalmac_adapter, REG_AUTO_LLT_V1) | BIT_AUTO_INIT_LLT_V1));
	counter = 1000;
	while (HALMAC_REG_READ_8(pHalmac_adapter, REG_AUTO_LLT_V1) & BIT_AUTO_INIT_LLT_V1) {
		counter--;
		if (counter == 0)
			return HALMAC_RET_INIT_LLT_FAIL;
	}

	if (HALMAC_TRX_MODE_DELAY_LOOPBACK == halmac_trx_mode)
		transfer_mode = HALMAC_TRNSFER_LOOPBACK_DELAY;

	HALMAC_REG_WRITE_8(pHalmac_adapter, REG_CR + 3, (u8)transfer_mode);

	return HALMAC_RET_SUCCESS;
}
