#ifndef _HALMAC_API_H_
#define _HALMAC_API_H_

#define HALMAC_SVN_VER  "11974M"

/* major version, ver_1 for async_api */
#define HALMAC_MAJOR_VER        0x0001
/* For halmac_api num change or prototype change, increment prototype version */
#define HALMAC_PROTOTYPE_VER    0x0002
/* else increment minor version */
#define HALMAC_MINOR_VER        0x0000

#include "halmac_2_platform.h"
#include "halmac_hw_cfg.h"
#include "halmac_type.h"

#include "halmac_usb_reg.h"
#include "halmac_sdio_reg.h"
#include "halmac_pcie_reg.h"

#include "halmac_bit2.h"
#include "halmac_reg2.h"

#if (HALMAC_PLATFORM_WINDOWS || HALMAC_PLATFORM_LINUX)
#include "halmac_tx_desc_nic.h"
#include "halmac_rx_desc_nic.h"
#include "halmac_tx_bd_nic.h"
#include "halmac_rx_bd_nic.h"
#include "halmac_fw_offload_c2h_nic.h"
#include "halmac_fw_offload_h2c_nic.h"
#include "halmac_h2c_extra_info_nic.h"
#include "halmac_original_c2h_nic.h"
#include "halmac_original_h2c_nic.h"
#endif

#if (HALMAC_PLATFORM_AP)
#include "halmac_rx_desc_ap.h"
#include "halmac_tx_desc_ap.h"
#include "halmac_rx_bd_ap.h"
#include "halmac_tx_bd_ap.h"
#include "halmac_fw_offload_c2h_ap.h"
#include "halmac_fw_offload_h2c_ap.h"
#include "halmac_h2c_extra_info_ap.h"
#include "halmac_original_c2h_ap.h"
#include "halmac_original_h2c_ap.h"
#endif

#include "halmac_tx_desc_chip.h"
#include "halmac_rx_desc_chip.h"
#include "halmac_tx_bd_chip.h"
#include "halmac_rx_bd_chip.h"
#if HALMAC_PLATFORM_WINDOWS == 1
#include "halmac_88xx/halmac_win8822b_cfg.h"
#include "halmac_88xx/halmac_win8821c_cfg.h"
#include "halmac_88xx/halmac_win8197f_cfg.h"
#else
#include "halmac_88xx/halmac_88xx_cfg.h"
#endif

#include "halmac_88xx/halmac_8822b/halmac_8822b_cfg.h"
#include "halmac_88xx/halmac_8821c/halmac_8821c_cfg.h"
#include "halmac_88xx/halmac_8197f/halmac_8197f_cfg.h"
#include "halmac_reg_8822b.h"
#include "halmac_bit_8822b.h"
#include "halmac_reg_8821c.h"
#include "halmac_bit_8821c.h"


HALMAC_RET_STATUS
halmac_init_adapter(
	IN VOID *pDriver_adapter,
	IN PHALMAC_PLATFORM_API pHalmac_platform_api,
	IN HALMAC_INTERFACE halmac_interface,
	OUT PHALMAC_ADAPTER *ppHalmac_adapter,
	OUT PHALMAC_API *ppHalmac_api
);

HALMAC_RET_STATUS
halmac_deinit_adapter(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_halt_api(
	IN PHALMAC_ADAPTER pHalmac_adapter
);

HALMAC_RET_STATUS
halmac_get_version(
	OUT HALMAC_VER * version
);

#endif
