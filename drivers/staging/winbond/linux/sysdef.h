

//
// Winbond WLAN System Configuration defines
//

//=====================================================================
// Current directory is Linux
// The definition WB_LINUX is a keyword for this OS
//=====================================================================
#ifndef SYS_DEF_H
#define SYS_DEF_H
#define WB_LINUX
#define WB_LINUX_WPA_PSK


//#define _IBSS_BEACON_SEQ_STICK_
#define _USE_FALLBACK_RATE_
//#define ANTDIV_DEFAULT_ON

#define _WPA2_	// 20061122 It's needed for current Linux driver


#ifndef _WPA_PSK_DEBUG
#undef  _WPA_PSK_DEBUG
#endif

// debug print options, mark what debug you don't need

#ifdef FULL_DEBUG
#define _PE_STATE_DUMP_
#define _PE_TX_DUMP_
#define _PE_RX_DUMP_
#define _PE_OID_DUMP_
#define _PE_DTO_DUMP_
#define _PE_REG_DUMP_
#define _PE_USB_INI_DUMP_
#endif



#include "common.h"	// Individual file depends on OS

#include "../wb35_ver.h"
#include "../mac_structures.h"
#include "../ds_tkip.h"
#include "../localpara.h"
#include "../sme_s.h"
#include "../scan_s.h"
#include "../mds_s.h"
#include "../mlme_s.h"
#include "../bssdscpt.h"
#include "../sme_api.h"
#include "../gl_80211.h"
#include "../mto.h"
#include "../wblinux_s.h"
#include "../wbhal_s.h"


#include "../adapter.h"

#include "../mlme_mib.h"
#include "../mds_f.h"
#include "../bss_f.h"
#include "../mlmetxrx_f.h"
#include "../mto_f.h"
#include "../wbhal_f.h"
#include "../wblinux_f.h"
// Kernel Timer resolution, NDIS is 10ms, 10000us
#define MIN_TIMEOUT_VAL	(10) //ms


#endif
