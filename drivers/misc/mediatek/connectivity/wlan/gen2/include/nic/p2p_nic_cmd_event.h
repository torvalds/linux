/*
** Id: //Department/DaVinci/TRUNK/WiFi_P2P_Driver/include/nic/p2p_nic_cmd_event.h#1
*/

/*! \file   p2p_nic_cmd_event.h
    \brief
*/

#ifndef _P2P_NIC_CMD_EVENT_H
#define _P2P_NIC_CMD_EVENT_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

typedef struct _EVENT_P2P_DEV_DISCOVER_RESULT_T {
/* UINT_8                          aucCommunicateAddr[MAC_ADDR_LEN];  // Deprecated. */
	UINT_8 aucDeviceAddr[MAC_ADDR_LEN];	/* Device Address. */
	UINT_8 aucInterfaceAddr[MAC_ADDR_LEN];	/* Device Address. */
	UINT_8 ucDeviceCapabilityBitmap;
	UINT_8 ucGroupCapabilityBitmap;
	UINT_16 u2ConfigMethod;	/* Configure Method. */
	P2P_DEVICE_TYPE_T rPriDevType;
	UINT_8 ucSecDevTypeNum;
	P2P_DEVICE_TYPE_T arSecDevType[2];
	UINT_16 u2NameLength;
	UINT_8 aucName[32];
	PUINT_8 pucIeBuf;
	UINT_16 u2IELength;
	UINT_8 aucBSSID[MAC_ADDR_LEN];
	/* TODO: Service Information or PasswordID valid? */
} EVENT_P2P_DEV_DISCOVER_RESULT_T, *P_EVENT_P2P_DEV_DISCOVER_RESULT_T;

#endif
