
#ifndef _WMT_DBG_H_
#define _WMT_DBG_H_
#include "osal.h"

#define STP_SDIO      0x04
#define STP_UART_MAND 0x02
#define STP_UART_FULL 0x01

#define CFG_WMT_DBG_SUPPORT 1 // support wmt_dbg or not

#if CFG_WMT_DBG_SUPPORT
typedef struct _COEX_BUF{
    UINT8 buffer[128];
    INT32 availSize; 
}COEX_BUF, *P_COEX_BUF;

typedef enum _ENUM_CMD_TYPE_T {
    WMTDRV_CMD_ASSERT = 0,
    WMTDRV_CMD_EXCEPTION = 1,
    WMTDRV_CMD_COEXDBG_00 = 2,
    WMTDRV_CMD_COEXDBG_01 = 3,
    WMTDRV_CMD_COEXDBG_02 = 4,
    WMTDRV_CMD_COEXDBG_03 = 5,
    WMTDRV_CMD_COEXDBG_04 = 6,
    WMTDRV_CMD_COEXDBG_05 = 7,
    WMTDRV_CMD_COEXDBG_06 = 8,
    WMTDRV_CMD_COEXDBG_07 = 9,
    WMTDRV_CMD_COEXDBG_08 = 10,
    WMTDRV_CMD_COEXDBG_09 = 11,
    WMTDRV_CMD_COEXDBG_10 = 12,
    WMTDRV_CMD_COEXDBG_11 = 13,
    WMTDRV_CMD_COEXDBG_12 = 14,
    WMTDRV_CMD_COEXDBG_13 = 15,
    WMTDRV_CMD_COEXDBG_14 = 16,
    WMTDRV_CMD_COEXDBG_15 = 17,    
    WMTDRV_CMD_MAX
} ENUM_WMTDRV_CMD_T, *P_ENUM_WMTDRV_CMD_T;


typedef INT32 (*WMT_DEV_DBG_FUNC)(INT32 par1, INT32 par2, INT32 par3);
INT32 wmt_dev_dbg_setup(VOID);
INT32 wmt_dev_dbg_remove(VOID);


#endif

#endif
