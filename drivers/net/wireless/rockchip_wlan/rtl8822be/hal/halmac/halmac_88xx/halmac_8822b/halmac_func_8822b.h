/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _HALMAC_FUNC_8822B_H_
#define _HALMAC_FUNC_8822B_H_

#include "../../halmac_type.h"

HALMAC_RET_STATUS
halmac_txdma_queue_mapping_8822b(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_TRX_MODE halmac_trx_mode
);

HALMAC_RET_STATUS
halmac_priority_queue_config_8822b(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_TRX_MODE halmac_trx_mode
);

#endif /* _HALMAC_FUNC_8822B_H_ */
