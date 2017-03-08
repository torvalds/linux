#include "halmac_8822b_cfg.h"

/**
 * halmac_mac_power_switch_8822b_sdio() - change mac power
 * @pHalmac_adapter
 * @halmac_power
 * Author : KaiYuan Chang/Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_mac_power_switch_8822b_sdio(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_MAC_POWER	halmac_power
)
{
	u8 interface_mask;
	u8 rpwm;
	VOID *pDriver_adapter = NULL;
	PHALMAC_API pHalmac_api;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_MAC_POWER_SWITCH);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;
	pHalmac_api = (PHALMAC_API)pHalmac_adapter->pHalmac_api;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_ERR, "halmac_mac_power_switch_88xx_sdio halmac_power = %x ==========>\n", halmac_power);

	interface_mask = HALMAC_PWR_INTF_SDIO_MSK;

	pHalmac_adapter->rpwm_record = HALMAC_REG_READ_8(pHalmac_adapter, REG_SDIO_HRPWM1);

	/* Check FW still exist or not */
	if (0xC078 == HALMAC_REG_READ_16(pHalmac_adapter, REG_MCUFW_CTRL)) {
		/* Leave 32K */
		rpwm = (u8)((pHalmac_adapter->rpwm_record ^ BIT(7)) & 0x80);
		HALMAC_REG_WRITE_8(pHalmac_adapter, REG_SDIO_HRPWM1, rpwm);
	}

	if (0xEA == HALMAC_REG_READ_8(pHalmac_adapter, REG_CR))
		pHalmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_OFF;

	/*Check if power switch is needed*/
	if (halmac_power == pHalmac_adapter->halmac_state.mac_power) {
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_WARN, "halmac_mac_power_switch power state unchange!\n");
	} else {
		if (HALMAC_MAC_POWER_OFF == halmac_power) {
			if (HALMAC_RET_SUCCESS != halmac_pwr_seq_parser_88xx(pHalmac_adapter, HALMAC_PWR_CUT_TESTCHIP_MSK, HALMAC_PWR_FAB_TSMC_MSK,
				    interface_mask, halmac_8822b_card_disable_flow)) {
				PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_ERR, "Handle power off cmd error\n");
				return HALMAC_RET_POWER_OFF_FAIL;
			}

			pHalmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_OFF;
			pHalmac_adapter->halmac_state.ps_state = HALMAC_PS_STATE_UNDEFINE;
			pHalmac_adapter->halmac_state.dlfw_state = HALMAC_DLFW_NONE;
		} else {
			if (HALMAC_RET_SUCCESS != halmac_pwr_seq_parser_88xx(pHalmac_adapter, HALMAC_PWR_CUT_TESTCHIP_MSK, HALMAC_PWR_FAB_TSMC_MSK,
				    interface_mask, halmac_8822b_card_enable_flow)) {
				PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_ERR, "Handle power on cmd error\n");
				return HALMAC_RET_POWER_ON_FAIL;
			}

			pHalmac_adapter->halmac_state.mac_power = HALMAC_MAC_POWER_ON;
			pHalmac_adapter->halmac_state.ps_state = HALMAC_PS_STATE_ACT;
		}
	}

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_PWR, HALMAC_DBG_ERR, "halmac_mac_power_switch_88xx_sdio <==========\n");

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_tx_allowed_sdio_8822b() - check sdio tx reserved page
 * @pHalmac_adapter
 * @pHalmac_buf
 * @halmac_size
 * Author : Ivan Lin
 * Return : HALMAC_RET_STATUS
 */
HALMAC_RET_STATUS
halmac_tx_allowed_sdio_8822b(
	IN PHALMAC_ADAPTER	pHalmac_adapter,
	IN u8			*pHalmac_buf,
	IN u32			halmac_size
)
{
	u8 *pCurr_packet;
	u16 *pCurr_free_space;
	u32 i, counter;
	u32 tx_agg_num, packet_size;
	u32 tx_required_page_num, total_required_page_num = 0;
	VOID *pDriver_adapter = NULL;
	HALMAC_DMA_MAPPING dma_mapping;

	if (HALMAC_RET_SUCCESS != halmac_adapter_validate(pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	if (HALMAC_RET_SUCCESS != halmac_api_validate(pHalmac_adapter))
		return HALMAC_RET_API_INVALID;

	halmac_api_record_id_88xx(pHalmac_adapter, HALMAC_API_TX_ALLOWED_SDIO);

	pDriver_adapter = pHalmac_adapter->pDriver_adapter;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_tx_allowed_sdio_8822b ==========>\n");

	tx_agg_num = GET_TX_DESC_DMA_TXAGG_NUM(pHalmac_buf);
	pCurr_packet = pHalmac_buf;

	tx_agg_num = (tx_agg_num == 0) ? 1 : tx_agg_num;

	switch ((HALMAC_QUEUE_SELECT)GET_TX_DESC_QSEL(pCurr_packet)) {
	case HALMAC_QUEUE_SELECT_VO:
	case HALMAC_QUEUE_SELECT_VO_V2:
		dma_mapping = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VO];
		break;
	case HALMAC_QUEUE_SELECT_VI:
	case HALMAC_QUEUE_SELECT_VI_V2:
		dma_mapping = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_VI];
		break;
	case HALMAC_QUEUE_SELECT_BE:
	case HALMAC_QUEUE_SELECT_BE_V2:
		dma_mapping = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BE];
		break;
	case HALMAC_QUEUE_SELECT_BK:
	case HALMAC_QUEUE_SELECT_BK_V2:
		dma_mapping = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_BK];
		break;
	case HALMAC_QUEUE_SELECT_MGNT:
		dma_mapping = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_MG];
		break;
	case HALMAC_QUEUE_SELECT_HIGH:
		dma_mapping = pHalmac_adapter->halmac_ptcl_queue[HALMAC_PTCL_QUEUE_HI];
		break;
	case HALMAC_QUEUE_SELECT_BCN:
	case HALMAC_QUEUE_SELECT_CMD:
		return HALMAC_RET_SUCCESS;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "Qsel is out of range\n");
		return HALMAC_RET_QSEL_INCORRECT;
	}

	switch (dma_mapping) {
	case HALMAC_DMA_MAPPING_HIGH:
		pCurr_free_space = &(pHalmac_adapter->sdio_free_space.high_queue_number);
		break;
	case HALMAC_DMA_MAPPING_NORMAL:
		pCurr_free_space = &(pHalmac_adapter->sdio_free_space.normal_queue_number);
		break;
	case HALMAC_DMA_MAPPING_LOW:
		pCurr_free_space = &(pHalmac_adapter->sdio_free_space.low_queue_number);
		break;
	case HALMAC_DMA_MAPPING_EXTRA:
		pCurr_free_space = &(pHalmac_adapter->sdio_free_space.extra_queue_number);
		break;
	default:
		PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_ERR, "DmaMapping is out of range\n");
		return HALMAC_RET_DMA_MAP_INCORRECT;
	}

	for (i = 0; i < tx_agg_num; i++) {
		packet_size = GET_TX_DESC_TXPKTSIZE(pCurr_packet) + GET_TX_DESC_OFFSET(pCurr_packet);
		tx_required_page_num = (packet_size >> HALMAC_TX_PAGE_SIZE_2_POWER_8822B) + ((packet_size & (HALMAC_TX_PAGE_SIZE_8822B - 1)) ? 1 : 0);
		total_required_page_num += tx_required_page_num;

		packet_size = HALMAC_ALIGN(packet_size, 8);

		pCurr_packet += packet_size;
	}

	counter = 10;
	while (1) {
		if ((u32)(*pCurr_free_space + pHalmac_adapter->sdio_free_space.public_queue_number) >= total_required_page_num) {
			if (*pCurr_free_space >= total_required_page_num) {
				*pCurr_free_space -= (u16)total_required_page_num;
			} else {
				pHalmac_adapter->sdio_free_space.public_queue_number -= (u16)(total_required_page_num - *pCurr_free_space);
				*pCurr_free_space = 0;
			}
			break;
		} else {
			halmac_update_sdio_free_page_88xx(pHalmac_adapter);
		}

		counter--;
		if (0 == counter)
			return HALMAC_RET_FREE_SPACE_NOT_ENOUGH;
	}

	counter = 10;
	while (pHalmac_adapter->sdio_free_space.ac_oqt_number < tx_agg_num) {
		halmac_update_oqt_free_space_88xx(pHalmac_adapter);

		counter--;
		if (0 == counter)
			return HALMAC_RET_FREE_SPACE_NOT_ENOUGH;
	}
	pHalmac_adapter->sdio_free_space.ac_oqt_number -= (u8)tx_agg_num;

	PLATFORM_MSG_PRINT(pDriver_adapter, HALMAC_MSG_INIT, HALMAC_DBG_TRACE, "halmac_tx_allowed_sdio_8822b <==========\n");

	return HALMAC_RET_SUCCESS;
}


