#ifndef _HALMAC_API_88XX_SDIO_H_
#define _HALMAC_API_88XX_SDIO_H_

#include "../halmac_2_platform.h"
#include "../halmac_type.h"

HALMAC_RET_STATUS
halmac_init_sdio_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_deinit_sdio_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_cfg_rx_aggregation_88xx_sdio(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_RXAGG_CFG phalmac_rxagg_cfg
);

u8
halmac_reg_read_8_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset
);

HALMAC_RET_STATUS
halmac_reg_write_8_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u8 halmac_data
);

u16
halmac_reg_read_16_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset
);

HALMAC_RET_STATUS
halmac_reg_write_16_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u16 halmac_data
);

u32
halmac_reg_read_32_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset
);

HALMAC_RET_STATUS
halmac_reg_write_32_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u32 halmac_data
);

HALMAC_RET_STATUS
halmac_get_sdio_tx_addr_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 *halmac_buf,
	IN u32 halmac_size,
	OUT u32 *pcmd53_addr
);

HALMAC_RET_STATUS
halmac_cfg_tx_agg_align_sdio_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u8 enable,
	IN u16 align_size
);

HALMAC_RET_STATUS
halmac_cfg_tx_agg_align_sdio_not_support_88xx(
	IN PHALMAC_ADAPTER	pHalmac_adapter,
	IN u8	enable,
	IN u16	align_size
);

#endif/* _HALMAC_API_88XX_SDIO_H_ */
