

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

#endif
