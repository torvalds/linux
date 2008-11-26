//============================================================================
//  IOCTLS.H -
//
//  Description:
//    Define the IOCTL codes.
//
//  Revision history:
//  --------------------------------------------------------------------------
//
//  Copyright (c) 2002-2004 Winbond Electronics Corp. All rights reserved.
//=============================================================================

#ifndef _IOCTLS_H
#define _IOCTLS_H

// PD43 Keep it - Used with the Win33 application
// #include <winioctl.h>

//========================================================
// 20040108 ADD the follow for test
//========================================================
#define INFORMATION_LENGTH sizeof(unsigned int)

#define WB32_IOCTL_INDEX  0x0900 //­×§ďĽHŤKŹŰŽe//

#define Wb32_RegisterRead			CTL_CODE(	\
			FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 0,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb32_RegisterWrite			CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 1,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb32_SendPacket				CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 2,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb32_QuerySendResult		CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 3,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb32_SetFragmentThreshold	CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 4,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb32_SetLinkStatus		CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 5,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb32_SetBulkIn			CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 6,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb32_LoopbackTest			CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 7,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb35_EEPromRead				CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 8,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb35_EEPromWrite			CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 9,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb35_FlashReadData			CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 10,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb35_FlashWrite				CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 11,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb35_FlashWriteBurst		CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 12,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb35_TxBurstStart			CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 13,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb35_TxBurstStop			CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 14,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

#define Wb35_TxBurstStatus			CTL_CODE(	\
            FILE_DEVICE_UNKNOWN,				\
            WB32_IOCTL_INDEX + 15,				\
            METHOD_BUFFERED,					\
            FILE_ANY_ACCESS)

// For IOCTL interface
//================================================
#define LINKNAME_STRING     "\\DosDevices\\W35UND"
#define NTDEVICE_STRING     "\\Device\\W35UND"
#define APPLICATION_LINK	"\\\\.\\W35UND"

#define WB_IOCTL_INDEX      0x0800
#define WB_IOCTL_TS_INDEX   WB_IOCTL_INDEX + 60
#define WB_IOCTL_DUT_INDEX  WB_IOCTL_TS_INDEX + 40

//=============================================================================
// IOCTLS defined for DUT (Device Under Test)

// IOCTL_WB_802_11_DUT_MAC_ADDRESS
// Query: Return the dot11StationID
// Set  : Set the dot11StationID. Demo only.
//
#define IOCTL_WB_802_11_DUT_MAC_ADDRESS     CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                        \
            WB_IOCTL_DUT_INDEX + 1,                     \
            METHOD_BUFFERED,                            \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_BSS_DESCRIPTION
// Query: Return the info. of the current connected BSS.
// Set  : None.
//
#define IOCTL_WB_802_11_DUT_BSS_DESCRIPTION   CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                          \
            WB_IOCTL_DUT_INDEX + 2,                       \
            METHOD_BUFFERED,                              \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_TX_RATE
// Query: Return the current transmission rate.
// Set  : Set the transmission rate of the Tx packets.
//
#define IOCTL_WB_802_11_DUT_TX_RATE             CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 3,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_CURRENT_STA_STATE
// Query: Return the current STA state. (WB_STASTATE type)
// Set  : None.
//
#define IOCTL_WB_802_11_DUT_CURRENT_STA_STATE   CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 4,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

/////////// 10/31/02' Added /////////////////////

// IOCTL_WB_802_11_DUT_START_IBSS_REQUEST
// Query: None.
// Set  : Start a new IBSS
//
#define IOCTL_WB_802_11_DUT_START_IBSS_REQUEST  CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 5,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_JOIN_REQUEST
// Query: None.
// Set  : Synchronize with the selected BSS
//
#define IOCTL_WB_802_11_DUT_JOIN_REQUEST        CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 6,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_AUTHEN_REQUEST
// Query: None.
// Set  : Authenticate with the BSS
//
#define IOCTL_WB_802_11_DUT_AUTHEN_REQUEST      CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 7,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_DEAUTHEN_REQUEST
// Query: None.
// Set  : DeAuthenticate withe the BSS
//
#define IOCTL_WB_802_11_DUT_DEAUTHEN_REQUEST    CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 8,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_ASSOC_REQUEST
// Query: None.
// Set  : Associate withe the BSS
//
#define IOCTL_WB_802_11_DUT_ASSOC_REQUEST       CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 9,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_REASSOC_REQUEST
// Query: None.
// Set  : ReAssociate withe the BSS
//
#define IOCTL_WB_802_11_DUT_REASSOC_REQUEST     CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 10,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)


// IOCTL_WB_802_11_DUT_DISASSOC_REQUEST
// Query: None.
// Set  : DisAssociate withe the BSS
//
#define IOCTL_WB_802_11_DUT_DISASSOC_REQUEST    CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 11,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_FRAG_THRESHOLD
// Query: Return the dot11FragmentThreshold
// Set  : Set the dot11FragmentThreshold
//
#define IOCTL_WB_802_11_DUT_FRAG_THRESHOLD      CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 12,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_RTS_THRESHOLD
// Query: Return the dot11RTSThreshold
// Set  : Set the dot11RTSThresold
//
#define IOCTL_WB_802_11_DUT_RTS_THRESHOLD       CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 13,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_WEP_KEYMODE
// Query: Get the WEP key mode.
// Set  : Set the WEP key mode: disable/64 bits/128 bits
//
#define IOCTL_WB_802_11_DUT_WEP_KEYMODE         CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 14,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_WEP_KEYVALUE
// Query: None.
// Set  : fill in the WEP key value
//
#define IOCTL_WB_802_11_DUT_WEP_KEYVALUE        CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 15,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_RESET
// Query: None.
// Set  : Reset S/W and H/W
//
#define IOCTL_WB_802_11_DUT_RESET          CTL_CODE(       \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 16,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_POWER_SAVE
// Query: None.
// Set  : Set Power Save Mode
//
#define IOCTL_WB_802_11_DUT_POWER_SAVE    CTL_CODE(        \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 17,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_BSSID_LIST_SCAN
// Query: None.
// Set  :
//
#define IOCTL_WB_802_11_DUT_BSSID_LIST_SCAN CTL_CODE(      \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 18,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_BSSID_LIST
// Query: Return the BSS info of BSSs in the last scanning process
// Set  : None.
//
#define IOCTL_WB_802_11_DUT_BSSID_LIST    CTL_CODE(        \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 19,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_STATISTICS
// Query: Return the statistics of Tx/Rx.
// Set  : None.
//
#define IOCTL_WB_802_11_DUT_STATISTICS    CTL_CODE(        \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 20,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_ACCEPT_BEACON
// Query: Return the current mode to accept beacon or not.
// Set  : Enable or disable allowing the HW-MAC to pass the beacon to the SW-MAC
// Arguments: unsigned char
//
#define IOCTL_WB_802_11_DUT_ACCEPT_BEACON  CTL_CODE(       \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 21,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_ROAMING
// Query: Return the roaming function status
// Set  : Enable/Disable the roaming function.
#define IOCTL_WB_802_11_DUT_ROAMING        CTL_CODE(       \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 22,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_DTO
// Query: Return the DTO(Data Throughput Optimization)
//        function status (TRUE or FALSE)
// Set  : Enable/Disable the DTO function.
//
#define IOCTL_WB_802_11_DUT_DTO            CTL_CODE(       \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 23,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_ANTENNA_DIVERSITY
// Query: Return the antenna diversity status. (TRUE/ON or FALSE/OFF)
// Set  : Enable/Disable the antenna diversity.
//
#define IOCTL_WB_802_11_DUT_ANTENNA_DIVERSITY CTL_CODE(    \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 24,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

//-------------- new added for a+b+g ---------------------
// IOCTL_WB_802_11_DUT_MAC_OPERATION_MODE
// Query: Return the MAC operation mode. (MODE_802_11_BG, MODE_802_11_A,
//			 MODE_802_11_ABG, MODE_802_11_BG_IBSS)
// Set  : Set the MAC operation mode.
//
#define IOCTL_WB_802_11_DUT_MAC_OPERATION_MODE CTL_CODE(    \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 25,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_TX_RATE_REDEFINED
// Query: Return the current tx rate which follows the definition in spec. (for
//			example, 5.5M => 0x0b)
// Set  : None
//
#define IOCTL_WB_802_11_DUT_TX_RATE_REDEFINED CTL_CODE(    \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 26,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_PREAMBLE_MODE
// Query: Return the preamble mode. (auto or long)
// Set  : Set the preamble mode.
//
#define IOCTL_WB_802_11_DUT_PREAMBLE_MODE CTL_CODE(    \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 27,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_SLOT_TIME_MODE
// Query: Return the slot time mode. (auto or long)
// Set  : Set the slot time mode.
//
#define IOCTL_WB_802_11_DUT_SLOT_TIME_MODE CTL_CODE(    \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 28,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)
//------------------------------------------------------------------

// IOCTL_WB_802_11_DUT_ADVANCE_STATUS
// Query:
// Set  : NONE
//
#define IOCTL_WB_802_11_DUT_ADVANCE_STATUS CTL_CODE(    \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 29,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_TX_RATE_MODE
// Query: Return the tx rate mode. (RATE_AUTO, RATE_1M, .., RATE_54M, RATE_MAX)
// Set  : Set the tx rate mode.  (RATE_AUTO, RATE_1M, .., RATE_54M, RATE_MAX)
//
#define IOCTL_WB_802_11_DUT_TX_RATE_MODE CTL_CODE(    \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 30,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_DTO_PARA
// Query: Return the DTO parameters
// Set  : Set the DTO parameters
//
#define IOCTL_WB_802_11_DUT_DTO_PARA CTL_CODE(    \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 31,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_EVENT_LOG
// Query: Return event log
// Set  : Reset event log
//
#define IOCTL_WB_802_11_DUT_EVENT_LOG CTL_CODE(    \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 32,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_CWMIN
// Query: NONE(It will be obtained by IOCTL_WB_802_11_DUT_ADVANCE_STATUS)
// Set  : Set CWMin value
//
#define IOCTL_WB_802_11_DUT_CWMIN CTL_CODE(    \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 33,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_DUT_CWMAX
// Query: NONE(It will be obtained by IOCTL_WB_802_11_DUT_ADVANCE_STATUS)
// Set  : Set CWMax value
//
#define IOCTL_WB_802_11_DUT_CWMAX CTL_CODE(    \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_DUT_INDEX + 34,                        \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)


//==========================================================
// IOCTLs for Testing

// IOCTL_WB_802_11_TS_SET_CXX_REG
// Query: None
// Set  : Write the value to one of Cxx register.
//
#define IOCTL_WB_802_11_TS_SET_CXX_REG  CTL_CODE(          \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 0,                          \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_TS_GET_CXX_REG
// Query: Return the value of the Cxx register.
// Set  : Write the reg no. (0x00, 0x04, 0x08 etc)
//
#define IOCTL_WB_802_11_TS_GET_CXX_REG  CTL_CODE(          \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 1,                          \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_TS_SET_DXX_REG
// Query: None
// Set  : Write the value to one of Dxx register.
//
#define IOCTL_WB_802_11_TS_SET_DXX_REG  CTL_CODE(          \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 2,                          \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// IOCTL_WB_802_11_TS_GET_DXX_REG
// Query: Return the value of the Dxx register.
// Set  : Write the reg no. (0x00, 0x04, 0x08 etc)
//
#define IOCTL_WB_802_11_TS_GET_DXX_REG  CTL_CODE(          \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 3,                          \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

//============================================================
// [TS]

#define IOCTL_WB_802_11_TS_TX_RATE              CTL_CODE(   \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 4,                          \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_CURRENT_CHANNEL      CTL_CODE(   \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 5,                          \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_ENABLE_SEQNO         CTL_CODE(   \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 6,                          \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_ENALBE_ACKEDPACKET   CTL_CODE(   \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 7,                          \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_INHIBIT_CRC          CTL_CODE(   \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 8,                          \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_RESET_RCV_COUNTER    CTL_CODE(   \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 9,                          \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_SET_TX_TRIGGER       CTL_CODE(   \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 10,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_FAILED_TX_COUNT       CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 11,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// [TS1]
#define IOCTL_WB_802_11_TS_TX_POWER             CTL_CODE(   \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 12,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_MODE_ENABLE			 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 13,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_MODE_DISABLE			 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 14,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_ANTENNA				 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 15,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_ADAPTER_INFO			 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 16,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_MAC_ADDRESS			 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 17,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_BSSID				 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 18,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_RF_PARAMETER			 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 19,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_FILTER				 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 20,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_CALIBRATION			 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 21,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_BSS_MODE				 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 22,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_SET_SSID				 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 23,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_IBSS_CHANNEL			 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 24,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

// set/query the slot time value(short or long slot time)
#define IOCTL_WB_802_11_TS_SLOT_TIME			 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 25,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_SLOT_TIME			 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 25,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#define IOCTL_WB_802_11_TS_RX_STATISTICS		 CTL_CODE(  \
            FILE_DEVICE_UNKNOWN,                            \
            WB_IOCTL_TS_INDEX + 26,                         \
            METHOD_BUFFERED,                                \
            FILE_ANY_ACCESS)

#endif  // #ifndef _IOCTLS_H


