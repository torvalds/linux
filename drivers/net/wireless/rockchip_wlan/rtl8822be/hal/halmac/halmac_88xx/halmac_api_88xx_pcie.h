#ifndef _HALMAC_API_88XX_PCIE_H_
#define _HALMAC_API_88XX_PCIE_H_

#include "../halmac_2_platform.h"
#include "../halmac_type.h"

HALMAC_RET_STATUS
halmac_init_pcie_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_deinit_pcie_cfg_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_cfg_rx_aggregation_88xx_pcie(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN PHALMAC_RXAGG_CFG phalmac_rxagg_cfg
);

u8
halmac_reg_read_8_pcie_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset
);

HALMAC_RET_STATUS
halmac_reg_write_8_pcie_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u8 halmac_data
);

u16
halmac_reg_read_16_pcie_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset
);

HALMAC_RET_STATUS
halmac_reg_write_16_pcie_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u16 halmac_data
);

u32
halmac_reg_read_32_pcie_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset
);

HALMAC_RET_STATUS
halmac_reg_write_32_pcie_88xx(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN u32 halmac_offset,
	IN u32 halmac_data
);

HALMAC_RET_STATUS
halmac_cfg_tx_agg_align_pcie_not_support_88xx(
	IN PHALMAC_ADAPTER	pHalmac_adapter,
	IN u8	enable,
	IN u16	align_size
);

#endif/* _HALMAC_API_88XX_PCIE_H_ */
