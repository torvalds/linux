//------------------------------------------------------------------------------
// <copyright file="hif.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// HIF specific declarations and prototypes
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef _HIF_H_
#define _HIF_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Header files */
#include "a_config.h"
#include "athdefs.h"
#include "a_types.h"
#include "a_osapi.h"
#include "dl_list.h"


typedef struct htc_callbacks HTC_CALLBACKS;
struct hif_device;

/*
 * direction - Direction of transfer (HIF_READ/HIF_WRITE).
 */
#define HIF_READ                    0x00000001
#define HIF_WRITE                   0x00000002
#define HIF_DIR_MASK                (HIF_READ | HIF_WRITE)

/*
 *     type - An interface may support different kind of read/write commands.
 *            For example: SDIO supports CMD52/CMD53s. In case of MSIO it
 *            translates to using different kinds of TPCs. The command type
 *            is thus divided into a basic and an extended command and can
 *            be specified using HIF_BASIC_IO/HIF_EXTENDED_IO.
 */
#define HIF_BASIC_IO                0x00000004
#define HIF_EXTENDED_IO             0x00000008
#define HIF_TYPE_MASK               (HIF_BASIC_IO | HIF_EXTENDED_IO)

/*
 *     emode - This indicates the whether the command is to be executed in a
 *             blocking or non-blocking fashion (HIF_SYNCHRONOUS/
 *             HIF_ASYNCHRONOUS). The read/write data paths in HTC have been
 *             implemented using the asynchronous mode allowing the the bus
 *             driver to indicate the completion of operation through the
 *             registered callback routine. The requirement primarily comes
 *             from the contexts these operations get called from (a driver's
 *             transmit context or the ISR context in case of receive).
 *             Support for both of these modes is essential.
 */
#define HIF_SYNCHRONOUS             0x00000010
#define HIF_ASYNCHRONOUS            0x00000020
#define HIF_EMODE_MASK              (HIF_SYNCHRONOUS | HIF_ASYNCHRONOUS)

/*
 *     dmode - An interface may support different kinds of commands based on
 *             the tradeoff between the amount of data it can carry and the
 *             setup time. Byte and Block modes are supported (HIF_BYTE_BASIS/
 *             HIF_BLOCK_BASIS). In case of latter, the data is rounded off
 *             to the nearest block size by padding. The size of the block is
 *             configurable at compile time using the HIF_BLOCK_SIZE and is
 *             negotiated with the target during initialization after the
 *             AR6000 interrupts are enabled.
 */
#define HIF_BYTE_BASIS              0x00000040
#define HIF_BLOCK_BASIS             0x00000080
#define HIF_DMODE_MASK              (HIF_BYTE_BASIS | HIF_BLOCK_BASIS)

/*
 *     amode - This indicates if the address has to be incremented on AR6000 
 *             after every read/write operation (HIF?FIXED_ADDRESS/
 *             HIF_INCREMENTAL_ADDRESS).
 */
#define HIF_FIXED_ADDRESS           0x00000100
#define HIF_INCREMENTAL_ADDRESS     0x00000200
#define HIF_AMODE_MASK              (HIF_FIXED_ADDRESS | HIF_INCREMENTAL_ADDRESS)

#define HIF_WR_ASYNC_BYTE_FIX   \
    (HIF_WRITE | HIF_ASYNCHRONOUS | HIF_EXTENDED_IO | HIF_BYTE_BASIS | HIF_FIXED_ADDRESS)
#define HIF_WR_ASYNC_BYTE_INC   \
    (HIF_WRITE | HIF_ASYNCHRONOUS | HIF_EXTENDED_IO | HIF_BYTE_BASIS | HIF_INCREMENTAL_ADDRESS)
#define HIF_WR_ASYNC_BLOCK_INC  \
    (HIF_WRITE | HIF_ASYNCHRONOUS | HIF_EXTENDED_IO | HIF_BLOCK_BASIS | HIF_INCREMENTAL_ADDRESS)
#define HIF_WR_SYNC_BYTE_FIX    \
    (HIF_WRITE | HIF_SYNCHRONOUS | HIF_EXTENDED_IO | HIF_BYTE_BASIS | HIF_FIXED_ADDRESS)
#define HIF_WR_SYNC_BYTE_INC    \
    (HIF_WRITE | HIF_SYNCHRONOUS | HIF_EXTENDED_IO | HIF_BYTE_BASIS | HIF_INCREMENTAL_ADDRESS)
#define HIF_WR_SYNC_BLOCK_INC  \
    (HIF_WRITE | HIF_SYNCHRONOUS | HIF_EXTENDED_IO | HIF_BLOCK_BASIS | HIF_INCREMENTAL_ADDRESS)
#define HIF_WR_ASYNC_BLOCK_FIX \
    (HIF_WRITE | HIF_ASYNCHRONOUS | HIF_EXTENDED_IO | HIF_BLOCK_BASIS | HIF_FIXED_ADDRESS)
#define HIF_WR_SYNC_BLOCK_FIX  \
    (HIF_WRITE | HIF_SYNCHRONOUS | HIF_EXTENDED_IO | HIF_BLOCK_BASIS | HIF_FIXED_ADDRESS)
#define HIF_RD_SYNC_BYTE_INC    \
    (HIF_READ | HIF_SYNCHRONOUS | HIF_EXTENDED_IO | HIF_BYTE_BASIS | HIF_INCREMENTAL_ADDRESS)
#define HIF_RD_SYNC_BYTE_FIX    \
    (HIF_READ | HIF_SYNCHRONOUS | HIF_EXTENDED_IO | HIF_BYTE_BASIS | HIF_FIXED_ADDRESS)
#define HIF_RD_ASYNC_BYTE_FIX   \
    (HIF_READ | HIF_ASYNCHRONOUS | HIF_EXTENDED_IO | HIF_BYTE_BASIS | HIF_FIXED_ADDRESS)
#define HIF_RD_ASYNC_BLOCK_FIX  \
    (HIF_READ | HIF_ASYNCHRONOUS | HIF_EXTENDED_IO | HIF_BLOCK_BASIS | HIF_FIXED_ADDRESS)
#define HIF_RD_ASYNC_BYTE_INC   \
    (HIF_READ | HIF_ASYNCHRONOUS | HIF_EXTENDED_IO | HIF_BYTE_BASIS | HIF_INCREMENTAL_ADDRESS)
#define HIF_RD_ASYNC_BLOCK_INC  \
    (HIF_READ | HIF_ASYNCHRONOUS | HIF_EXTENDED_IO | HIF_BLOCK_BASIS | HIF_INCREMENTAL_ADDRESS)
#define HIF_RD_SYNC_BLOCK_INC  \
    (HIF_READ | HIF_SYNCHRONOUS | HIF_EXTENDED_IO | HIF_BLOCK_BASIS | HIF_INCREMENTAL_ADDRESS)
#define HIF_RD_SYNC_BLOCK_FIX  \
    (HIF_READ | HIF_SYNCHRONOUS | HIF_EXTENDED_IO | HIF_BLOCK_BASIS | HIF_FIXED_ADDRESS)
    
typedef enum {
    HIF_DEVICE_POWER_STATE = 0,
    HIF_DEVICE_GET_MBOX_BLOCK_SIZE,
    HIF_DEVICE_GET_MBOX_ADDR,
    HIF_DEVICE_GET_PENDING_EVENTS_FUNC,
    HIF_DEVICE_GET_IRQ_PROC_MODE,
    HIF_DEVICE_GET_RECV_EVENT_MASK_UNMASK_FUNC,
    HIF_DEVICE_POWER_STATE_CHANGE,
    HIF_DEVICE_GET_IRQ_YIELD_PARAMS,
    HIF_CONFIGURE_QUERY_SCATTER_REQUEST_SUPPORT,
    HIF_DEVICE_GET_OS_DEVICE,
    HIF_DEVICE_DEBUG_BUS_STATE,
} HIF_DEVICE_CONFIG_OPCODE;

/*
 * HIF CONFIGURE definitions:
 *
 *   HIF_DEVICE_GET_MBOX_BLOCK_SIZE
 *   input : none
 *   output : array of 4 A_UINT32s
 *   notes: block size is returned for each mailbox (4)
 *
 *   HIF_DEVICE_GET_MBOX_ADDR
 *   input : none
 *   output : struct hif_device_mbox_info
 *   notes: 
 *
 *   HIF_DEVICE_GET_PENDING_EVENTS_FUNC
 *   input : none
 *   output: HIF_PENDING_EVENTS_FUNC function pointer
 *   notes: this is optional for the HIF layer, if the request is
 *          not handled then it indicates that the upper layer can use
 *          the standard device methods to get pending events (IRQs, mailbox messages etc..)
 *          otherwise it can call the function pointer to check pending events.
 *
 *   HIF_DEVICE_GET_IRQ_PROC_MODE
 *   input : none
 *   output : HIF_DEVICE_IRQ_PROCESSING_MODE (interrupt processing mode)
 *   note: the hif layer interfaces with the underlying OS-specific bus driver. The HIF
 *         layer can report whether IRQ processing is requires synchronous behavior or
 *         can be processed using asynchronous bus requests (typically faster).
 *
 *   HIF_DEVICE_GET_RECV_EVENT_MASK_UNMASK_FUNC
 *   input :
 *   output : HIF_MASK_UNMASK_RECV_EVENT function pointer
 *   notes: this is optional for the HIF layer.  The HIF layer may require a special mechanism
 *          to mask receive message events.  The upper layer can call this pointer when it needs
 *          to mask/unmask receive events (in case it runs out of buffers).
 *
 *   HIF_DEVICE_POWER_STATE_CHANGE
 *
 *   input : HIF_DEVICE_POWER_CHANGE_TYPE
 *   output : none
 *   note: this is optional for the HIF layer.  The HIF layer can handle power on/off state change
 *         requests in an interconnect specific way.  This is highly OS and bus driver dependent.
 *         The caller must guarantee that no HIF read/write requests will be made after the device
 *         is powered down.
 *
 *   HIF_DEVICE_GET_IRQ_YIELD_PARAMS
 * 
 *   input : none
 *   output : struct hif_device_irq_yield_params
 *   note: This query checks if the HIF layer wishes to impose a processing yield count for the DSR handler.
 *   The DSR callback handler will exit after a fixed number of RX packets or events are processed.  
 *   This query is only made if the device reports an IRQ processing mode of HIF_DEVICE_IRQ_SYNC_ONLY. 
 *   The HIF implementation can ignore this command if it does not desire the DSR callback to yield.
 *   The HIF layer can indicate the maximum number of IRQ processing units (RX packets) before the
 *   DSR handler callback must yield and return control back to the HIF layer.  When a yield limit is 
 *   used the DSR callback will not call HIFAckInterrupts() as it would normally do before returning.  
 *   The HIF implementation that requires a yield count must call HIFAckInterrupt() when it is prepared
 *   to process interrupts again.
 *   
 *   HIF_CONFIGURE_QUERY_SCATTER_REQUEST_SUPPORT
 *   input : none
 *   output : struct hif_device_scatter_support_info
 *   note:  This query checks if the HIF layer implements the SCATTER request interface.  Scatter requests
 *   allows upper layers to submit mailbox I/O operations using a list of buffers.  This is useful for
 *   multi-message transfers that can better utilize the bus interconnect.
 * 
 * 
 *   HIF_DEVICE_GET_OS_DEVICE
 *   intput : none
 *   output : struct hif_device_os_device_info;
 *   note: On some operating systems, the HIF layer has a parent device object for the bus.  This object
 *         may be required to register certain types of logical devices.
 * 
 *   HIF_DEVICE_DEBUG_BUS_STATE
 *   input : none
 *   output : none
 *   note: This configure option triggers the HIF interface to dump as much bus interface state.  This 
 *   configuration request is optional (No-OP on some HIF implementations)
 * 
 */

struct hif_mbox_properties {
    u32 ExtendedAddress;  /* extended address for larger writes */
    u32 ExtendedSize;
};

#define HIF_MBOX_FLAG_NO_BUNDLING   (1 << 0)   /* do not allow bundling over the mailbox */

typedef enum _MBOX_BUF_IF_TYPE {
    MBOX_BUS_IF_SDIO = 0,
    MBOX_BUS_IF_SPI = 1,    
} MBOX_BUF_IF_TYPE;

struct hif_device_mbox_info {
    u32 MboxAddresses[4];  /* must be first element for legacy HIFs that return the address in
                                   and ARRAY of 32-bit words */
    
        /* the following describe extended mailbox properties */
    struct hif_mbox_properties MboxProp[4];
        /* if the HIF supports the GMbox extended address region it can report it
         * here, some interfaces cannot support the GMBOX address range and not set this */
    u32 GMboxAddress;
    u32 GMboxSize;
    u32 Flags;             /* flags to describe mbox behavior or usage */
    MBOX_BUF_IF_TYPE MboxBusIFType;   /* mailbox bus interface type */
};

typedef enum {
    HIF_DEVICE_IRQ_SYNC_ONLY,   /* for HIF implementations that require the DSR to process all
                                   interrupts before returning */
    HIF_DEVICE_IRQ_ASYNC_SYNC,  /* for HIF implementations that allow DSR to process interrupts
                                   using ASYNC I/O (that is HIFAckInterrupt can be called at a
                                   later time */
} HIF_DEVICE_IRQ_PROCESSING_MODE;

typedef enum {
    HIF_DEVICE_POWER_UP,    /* HIF layer should power up interface and/or module */
    HIF_DEVICE_POWER_DOWN,  /* HIF layer should initiate bus-specific measures to minimize power */
    HIF_DEVICE_POWER_CUT    /* HIF layer should initiate bus-specific AND/OR platform-specific measures
                               to completely power-off the module and associated hardware (i.e. cut power supplies)
                            */
} HIF_DEVICE_POWER_CHANGE_TYPE;

struct hif_device_irq_yield_params {
    int     RecvPacketYieldCount; /* max number of packets to force DSR to return */
};


struct hif_scatter_item {
    u8 *pBuffer;             /* CPU accessible address of buffer */
    int          Length;              /* length of transfer to/from this buffer */
    void        *pCallerContexts[2];  /* space for caller to insert a context associated with this item */
};

struct hif_scatter_req;
typedef void ( *HIF_SCATTER_COMP_CB)(struct hif_scatter_req *);

typedef enum _HIF_SCATTER_METHOD {
    HIF_SCATTER_NONE = 0,
    HIF_SCATTER_DMA_REAL,              /* Real SG support no restrictions */
    HIF_SCATTER_DMA_BOUNCE,            /* Uses SG DMA but HIF layer uses an internal bounce buffer */    
} HIF_SCATTER_METHOD;

struct hif_scatter_req {
    struct dl_list             ListLink;           /* link management */
    u32 Address;            /* address for the read/write operation */
    u32 Request;            /* request flags */
    u32 TotalLength;        /* total length of entire transfer */
    u32 CallerFlags;        /* caller specific flags can be stored here */
    HIF_SCATTER_COMP_CB CompletionRoutine;  /* completion routine set by caller */
    int            CompletionStatus;   /* status of completion */
    void                *Context;           /* caller context for this request */
    int                 ValidScatterEntries;  /* number of valid entries set by caller */
    HIF_SCATTER_METHOD  ScatterMethod;        /* scatter method handled by HIF */  
    void                *HIFPrivate[4];     /* HIF private area */
    u8 *pScatterBounceBuffer;  /* bounce buffer for upper layers to copy to/from */
    struct hif_scatter_item    ScatterList[1];     /* start of scatter list */
};

typedef struct hif_scatter_req * ( *HIF_ALLOCATE_SCATTER_REQUEST)(struct hif_device *device);
typedef void ( *HIF_FREE_SCATTER_REQUEST)(struct hif_device *device, struct hif_scatter_req *request);
typedef int ( *HIF_READWRITE_SCATTER)(struct hif_device *device, struct hif_scatter_req *request);

struct hif_device_scatter_support_info {
        /* information returned from HIF layer */
    HIF_ALLOCATE_SCATTER_REQUEST    pAllocateReqFunc;
    HIF_FREE_SCATTER_REQUEST        pFreeReqFunc;
    HIF_READWRITE_SCATTER           pReadWriteScatterFunc;    
    int                             MaxScatterEntries;
    int                             MaxTransferSizePerScatterReq;
};
                      
struct hif_device_os_device_info {
    void    *pOSDevice;
};
                      
#define HIF_MAX_DEVICES                 1

struct htc_callbacks {
    void      *context;     /* context to pass to the dsrhandler
                               note : rwCompletionHandler is provided the context passed to HIFReadWrite  */
    int (* rwCompletionHandler)(void *rwContext, int status);
    int (* dsrHandler)(void *context);
};

typedef struct osdrv_callbacks {
    void      *context;     /* context to pass for all callbacks except deviceRemovedHandler 
                               the deviceRemovedHandler is only called if the device is claimed */
    int (* deviceInsertedHandler)(void *context, void *hif_handle);
    int (* deviceRemovedHandler)(void *claimedContext, void *hif_handle);
    int (* deviceSuspendHandler)(void *context);
    int (* deviceResumeHandler)(void *context);
    int (* deviceWakeupHandler)(void *context);
    int (* devicePowerChangeHandler)(void *context, HIF_DEVICE_POWER_CHANGE_TYPE config);
} OSDRV_CALLBACKS;

#define HIF_OTHER_EVENTS     (1 << 0)   /* other interrupts (non-Recv) are pending, host
                                           needs to read the register table to figure out what */
#define HIF_RECV_MSG_AVAIL   (1 << 1)   /* pending recv packet */

struct hif_pending_events_info {
    u32 Events;
    u32 LookAhead;
    u32 AvailableRecvBytes;
#ifdef THREAD_X
    u32 Polling;
    u32 INT_CAUSE_REG;
#endif
};

    /* function to get pending events , some HIF modules use special mechanisms
     * to detect packet available and other interrupts */
typedef int ( *HIF_PENDING_EVENTS_FUNC)(struct hif_device              *device,
                                             struct hif_pending_events_info *pEvents,
                                             void                    *AsyncContext);

#define HIF_MASK_RECV    true
#define HIF_UNMASK_RECV  false
    /* function to mask recv events */
typedef int ( *HIF_MASK_UNMASK_RECV_EVENT)(struct hif_device  *device,
                                                bool      Mask,
                                                void        *AsyncContext);


/*
 * This API is used to perform any global initialization of the HIF layer
 * and to set OS driver callbacks (i.e. insertion/removal) to the HIF layer
 * 
 */
int HIFInit(OSDRV_CALLBACKS *callbacks);

/* This API claims the HIF device and provides a context for handling removal.
 * The device removal callback is only called when the OSDRV layer claims
 * a device.  The claimed context must be non-NULL */
void HIFClaimDevice(struct hif_device *device, void *claimedContext);
/* release the claimed device */
void HIFReleaseDevice(struct hif_device *device);

/* This API allows the HTC layer to attach to the HIF device */
int HIFAttachHTC(struct hif_device *device, HTC_CALLBACKS *callbacks);
/* This API detaches the HTC layer from the HIF device */
void     HIFDetachHTC(struct hif_device *device);

/*
 * This API is used to provide the read/write interface over the specific bus
 * interface.
 * address - Starting address in the AR6000's address space. For mailbox
 *           writes, it refers to the start of the mbox boundary. It should
 *           be ensured that the last byte falls on the mailbox's EOM. For
 *           mailbox reads, it refers to the end of the mbox boundary.
 * buffer - Pointer to the buffer containg the data to be transmitted or
 *          received.
 * length - Amount of data to be transmitted or received.
 * request - Characterizes the attributes of the command.
 */
int
HIFReadWrite(struct hif_device    *device,
             u32 address,
             u8       *buffer,
             u32 length,
             u32 request,
             void          *context);

/*
 * This can be initiated from the unload driver context when the OSDRV layer has no more use for
 * the device.
 */
void HIFShutDownDevice(struct hif_device *device);

/*
 * This should translate to an acknowledgment to the bus driver indicating that
 * the previous interrupt request has been serviced and the all the relevant
 * sources have been cleared. HTC is ready to process more interrupts.
 * This should prevent the bus driver from raising an interrupt unless the
 * previous one has been serviced and acknowledged using the previous API.
 */
void HIFAckInterrupt(struct hif_device *device);

void HIFMaskInterrupt(struct hif_device *device);

void HIFUnMaskInterrupt(struct hif_device *device);
 
#ifdef THREAD_X
/*
 * This set of functions are to be used by the bus driver to notify
 * the HIF module about various events.
 * These are not implemented if the bus driver provides an alternative
 * way for this notification though callbacks for instance.
 */
int HIFInsertEventNotify(void);

int HIFRemoveEventNotify(void);

int HIFIRQEventNotify(void);

int HIFRWCompleteEventNotify(void);
#endif

int
HIFConfigureDevice(struct hif_device *device, HIF_DEVICE_CONFIG_OPCODE opcode,
                   void *config, u32 configLen);

/* 
 * This API wait for the remaining MBOX messages to be drained
 * This should be moved to HTC AR6K layer
 */
int hifWaitForPendingRecv(struct hif_device *device);

#ifdef __cplusplus
}
#endif

#endif /* _HIF_H_ */
