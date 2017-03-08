#ifndef _HALMAC_API_8821C_H_
#define _HALMAC_API_8821C_H_

#include "../../halmac_2_platform.h"
#include "../../halmac_type.h"

HALMAC_RET_STATUS
halmac_mount_api_8821c(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_init_trx_cfg_8821C(
	IN PHALMAC_ADAPTER pHalmac_adapter,
	IN HALMAC_TRX_MODE halmac_trx_mode
);

HALMAC_RET_STATUS
halmac_init_h2c_8821c(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

#endif/* _HALMAC_API_8821C_H_ */
