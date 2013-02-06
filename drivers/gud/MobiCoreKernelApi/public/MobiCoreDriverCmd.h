/** @addtogroup MCD_MCDIMPL_DAEMON
 * @{
 * @file
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009 - 2011 -->
 */
#ifndef MCDAEMON_H_
#define MCDAEMON_H_




#define SOCK_PATH "/dev/socket/mcdaemon"
#include "mcUuid.h"

typedef enum {
    MC_DRV_CMD_PING            = 0,
    MC_DRV_CMD_GET_INFO        = 1,
    MC_DRV_CMD_OPEN_DEVICE     = 2,
    MC_DRV_CMD_CLOSE_DEVICE    = 3,
    MC_DRV_CMD_NQ_CONNECT      = 4,
	MC_DRV_CMD_OPEN_SESSION    = 5,
	MC_DRV_CMD_CLOSE_SESSION   = 6,
    MC_DRV_CMD_NOTIFY          = 7,
	MC_DRV_CMD_MAP_BULK_BUF    = 8,
	MC_DRV_CMD_UNMAP_BULK_BUF  = 9
} mcDrvCmd_t;


typedef enum {
	MC_DRV_RSP_OK                     = 0,
	MC_DRV_RSP_FAILED                 = 1,
    MC_DRV_RSP_DEVICE_NOT_OPENED      = 2,
    MC_DRV_RSP_DEVICE_ALREADY_OPENED  = 3,
	MC_DRV_RSP_COMMAND_NOT_ALLOWED    = 4,
	MC_DRV_INVALID_DEVICE_NAME        = 5,
	MC_DRV_RSP_MAP_BULK_ERRO          = 6,
	MC_DRV_RSP_TRUSTLET_NOT_FOUND     = 7,
	MC_DRV_RSP_PAYLOAD_LENGTH_ERROR	  = 8,
} mcDrvRsp_t;


typedef struct {
    uint32_t  commandId;
} mcDrvCommandHeader_t, *mcDrvCommandHeader_ptr;

typedef struct {
    uint32_t  responseId;
} mcDrvResponseHeader_t, *mcDrvResponseHeader_ptr;

#define MC_DEVICE_ID_DEFAULT    0 /**< The default device ID */


//--------------------------------------------------------------
typedef struct{
	uint32_t  deviceId;
} mcDrvCmdOpenDevicePayload_t, *mcDrvCmdOpenDevicePayload_ptr;

typedef struct{
    mcDrvCommandHeader_t         header;
    mcDrvCmdOpenDevicePayload_t  payload;
} mcDrvCmdOpenDevice_t, *mcDrvCmdOpenDevice_ptr;


typedef struct{
    // empty
} mcDrvRspOpenDevicePayload_t, *mcDrvRspOpenDevicePayload_ptr;

typedef struct{
    mcDrvResponseHeader_t        header;
    mcDrvRspOpenDevicePayload_t  payload;
} mcDrvRspOpenDevice_t, *mcDrvRspOpenDevice_ptr;


//--------------------------------------------------------------
typedef struct{
    mcDrvCommandHeader_t          header;
    // no payload here because close has none.
    // If we use an empty struct, C++ will count it as 4 bytes.
    // This will write too much into the socket at write(cmd,sizeof(cmd))
} mcDrvCmdCloseDevice_t, *mcDrvCmdCloseDevice_ptr;


typedef struct{
    // empty
} mcDrvRspCloseDevicePayload_t, *mcDrvRspCloseDevicePayload_ptr;

typedef struct{
    mcDrvResponseHeader_t         header;
    mcDrvRspCloseDevicePayload_t  payload;
} mcDrvRspCloseDevice_t, *mcDrvRspCloseDevice_ptr;


//--------------------------------------------------------------
typedef struct{
	uint32_t  deviceId;
	mcUuid_t    uuid;
	uint32_t  tci;
	uint32_t  len;
} mcDrvCmdOpenSessionPayload_t, *mcDrvCmdOpenSessionPayload_ptr;

typedef struct{
    mcDrvCommandHeader_t          header;
    mcDrvCmdOpenSessionPayload_t  payload;
} mcDrvCmdOpenSession_t, *mcDrvCmdOpenSession_ptr;


typedef struct{
	uint32_t  deviceId;
	uint32_t  sessionId;
	uint32_t  deviceSessionId;
	uint32_t  mcResult;
	uint32_t  sessionMagic;
} mcDrvRspOpenSessionPayload_t, *mcDrvRspOpenSessionPayload_ptr;

typedef struct{
    mcDrvResponseHeader_t         header;
    mcDrvRspOpenSessionPayload_t  payload;
} mcDrvRspOpenSession_t, *mcDrvRspOpenSession_ptr;


//--------------------------------------------------------------
typedef struct{
	uint32_t  sessionId;
} mcDrvCmdCloseSessionPayload_t, *mcDrvCmdCloseSessionPayload_ptr;

typedef struct{
    mcDrvCommandHeader_t           header;
    mcDrvCmdCloseSessionPayload_t  payload;
} mcDrvCmdCloseSession_t, *mcDrvCmdCloseSession_ptr;


typedef struct{
    // empty
} mcDrvRspCloseSessionPayload_t, *mcDrvRspCloseSessionPayload_ptr;

typedef struct{
    mcDrvResponseHeader_t         header;
    mcDrvRspCloseSessionPayload_t  payload;
} mcDrvRspCloseSession_t, *mcDrvRspCloseSession_ptr;


//--------------------------------------------------------------
typedef struct{
	uint32_t sessionId;
} mcDrvCmdNotifyPayload_t, *mcDrvCmdNotifyPayload_ptr;

typedef struct{
    mcDrvCommandHeader_t     header;
    mcDrvCmdNotifyPayload_t  payload;
} mcDrvCmdNotify_t, *mcDrvCmdNotify_ptr;


typedef struct{
    // empty
} mcDrvRspNotifyPayload_t, *mcDrvRspNotifyPayload_ptr;

typedef struct{
    mcDrvResponseHeader_t    header;
    mcDrvRspNotifyPayload_t  payload;
} mcDrvRspNotify_t, *mcDrvRspNotify_ptr;


//--------------------------------------------------------------
typedef struct{
	uint32_t  sessionId;
	uint32_t  pAddrL2;
	uint32_t  offsetPayload;
	uint32_t  lenBulkMem;
} mcDrvCmdMapBulkMemPayload_t, *mcDrvCmdMapBulkMemPayload_ptr;

typedef struct{
    mcDrvCommandHeader_t         header;
    mcDrvCmdMapBulkMemPayload_t  payload;
} mcDrvCmdMapBulkMem_t, *mcDrvCmdMapBulkMem_ptr;


typedef struct{
	uint32_t  sessionId;
	uint32_t  secureVirtualAdr;
	uint32_t  mcResult;
} mcDrvRspMapBulkMemPayload_t, *mcDrvRspMapBulkMemPayload_ptr;

typedef struct{
    mcDrvResponseHeader_t        header;
    mcDrvRspMapBulkMemPayload_t  payload;
} mcDrvRspMapBulkMem_t, *mcDrvRspMapBulkMem_ptr;


//--------------------------------------------------------------
typedef struct{
	uint32_t  sessionId;
	uint32_t  secureVirtualAdr;
	uint32_t  lenBulkMem;
} mcDrvCmdUnmapBulkMemPayload_t, *mcDrvCmdUnmapBulkMemPayload_ptr;

typedef struct{
    mcDrvCommandHeader_t           header;
    mcDrvCmdUnmapBulkMemPayload_t  payload;
} mcDrvCmdUnmapBulkMem_t, *mcDrvCmdUnmapBulkMem_ptr;


typedef struct{
    uint32_t  responseId;
	uint32_t  sessionId;
	uint32_t  mcResult;
} mcDrvRspUnmapBulkMemPayload_t, *mcDrvRspUnmapBulkMemPayload_ptr;

typedef struct{
    mcDrvResponseHeader_t          header;
    mcDrvRspUnmapBulkMemPayload_t  payload;
} mcDrvRspUnmapBulkMem_t, *mcDrvRspUnmapBulkMem_ptr;


//--------------------------------------------------------------
typedef struct {
    uint32_t  deviceId;
    uint32_t  sessionId;
    uint32_t  deviceSessionId;
    uint32_t  sessionMagic; //Random data
} mcDrvCmdNqConnectPayload_t, *mcDrvCmdNqConnectPayload_ptr;

typedef struct {
    mcDrvCommandHeader_t        header;
    mcDrvCmdNqConnectPayload_t  payload;
} mcDrvCmdNqConnect_t, *mcDrvCmdNqConnect_ptr;


typedef struct {
    // empty;
} mcDrvRspNqConnectPayload_t, *mcDrvRspNqConnectPayload_ptr;

typedef struct{
    mcDrvResponseHeader_t       header;
    mcDrvRspNqConnectPayload_t  payload;
} mcDrvRspNqConnect_t, *mcDrvRspNqConnect_ptr;


//--------------------------------------------------------------
typedef union {
    mcDrvCommandHeader_t    header;
    mcDrvCmdOpenDevice_t    mcDrvCmdOpenDevice;
    mcDrvCmdCloseDevice_t   mcDrvCmdCloseDevice;
    mcDrvCmdOpenSession_t   mcDrvCmdOpenSession;
    mcDrvCmdCloseSession_t  mcDrvCmdCloseSession;
    mcDrvCmdNqConnect_t     mcDrvCmdNqConnect;
    mcDrvCmdNotify_t        mcDrvCmdNotify;
    mcDrvCmdMapBulkMem_t    mcDrvCmdMapBulkMem;
    mcDrvCmdUnmapBulkMem_t  mcDrvCmdUnmapBulkMem;
} mcDrvCommand_t, *mcDrvCommand_ptr;

typedef union {
    mcDrvResponseHeader_t   header;
    mcDrvRspOpenDevice_t    mcDrvRspOpenDevice;
    mcDrvRspCloseDevice_t   mcDrvRspCloseDevice;
    mcDrvRspOpenSession_t   mcDrvRspOpenSession;
    mcDrvRspCloseSession_t  mcDrvRspCloseSession;
    mcDrvRspNqConnect_t     mcDrvRspNqConnect;
    mcDrvRspNotify_t        mcDrvRspNotify;
    mcDrvRspMapBulkMem_t    mcDrvRspMapBulkMem;
    mcDrvRspUnmapBulkMem_t  mcDrvRspUnmapBulkMem;
} mcDrvResponse_t, *mcDrvResponse_ptr;

#endif /* MCDAEMON_H_ */

/** @} */
