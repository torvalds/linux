/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/linux/include/gl_rst.h#1 $
*/

/*! \file   gl_rst.h
    \brief  Declaration of functions and finite state machine for 
            MT6620 Whole-Chip Reset Mechanism
*/




#ifndef _GL_RST_H
#define _GL_RST_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "gl_typedef.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
/* duplicated from wmt_exp.h for better driver isolation */
typedef enum _ENUM_WMTDRV_TYPE_T {
    WMTDRV_TYPE_BT = 0,
    WMTDRV_TYPE_FM = 1,
    WMTDRV_TYPE_GPS = 2,
    WMTDRV_TYPE_WIFI = 3,
    WMTDRV_TYPE_WMT = 4,
    WMTDRV_TYPE_STP = 5,
    WMTDRV_TYPE_SDIO1 = 6,
    WMTDRV_TYPE_SDIO2 = 7,
    WMTDRV_TYPE_LPBK = 8,
    WMTDRV_TYPE_MAX
} ENUM_WMTDRV_TYPE_T, *P_ENUM_WMTDRV_TYPE_T;

typedef enum _ENUM_WMTMSG_TYPE_T {
    WMTMSG_TYPE_POWER_ON = 0,
    WMTMSG_TYPE_POWER_OFF = 1,
    WMTMSG_TYPE_RESET = 2,
    WMTMSG_TYPE_STP_RDY= 3,
    WMTMSG_TYPE_HW_FUNC_ON= 4,
    WMTMSG_TYPE_MAX
} ENUM_WMTMSG_TYPE_T, *P_ENUM_WMTMSG_TYPE_T;

typedef enum _ENUM_WMTRSTMSG_TYPE_T{
    WMTRSTMSG_RESET_START = 0x0,
    WMTRSTMSG_RESET_END = 0x1,
    WMTRSTMSG_RESET_MAX,
    WMTRSTMSG_RESET_INVALID = 0xff
} ENUM_WMTRSTMSG_TYPE_T, *P_ENUM_WMTRSTMSG_TYPE_T;

typedef void (*PF_WMT_CB)(
    ENUM_WMTDRV_TYPE_T, /* Source driver type */
    ENUM_WMTDRV_TYPE_T, /* Destination driver type */
    ENUM_WMTMSG_TYPE_T, /* Message type */
    void *, /* READ-ONLY buffer. Buffer is allocated and freed by WMT_drv. Client
               can't touch this buffer after this function return. */
    unsigned int /* Buffer size in unit of byte */
    );


typedef enum _ENUM_WIFI_NETLINK_GRP_T{
    WIFI_NETLINK_GRP_RESET,
    WIFI_NETLINK_GRP_MAX
} ENUM_WIFI_NETLINK_GRP_T, *P_ENUM_WIFI_NETLINK_GRP_T;


typedef int (*set_p2p_mode)(struct net_device * netdev, PARAM_CUSTOM_P2P_SET_STRUC_T p2pmode);


/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/* Reset Initialization/Uninitialization                                      */
/*----------------------------------------------------------------------------*/
VOID
glResetInit(
    VOID
    );

VOID
glResetUninit(
    VOID
    );

VOID
glSendResetRequest(
    VOID
    );

BOOLEAN
kalIsResetting(
    VOID
    );


#endif /* _GL_RST_H */
