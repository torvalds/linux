/*  Winbond WLAN System Configuration defines */

#ifndef SYS_DEF_H
#define SYS_DEF_H

#include <linux/delay.h>

#define WB_LINUX
#define WB_LINUX_WPA_PSK

#define _USE_FALLBACK_RATE_

#define _WPA2_

#ifndef _WPA_PSK_DEBUG
#undef  _WPA_PSK_DEBUG
#endif

/* debug print options, mark what debug you don't need */

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
