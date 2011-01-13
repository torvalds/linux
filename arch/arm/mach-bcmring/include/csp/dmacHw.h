/*****************************************************************************
* Copyright 2004 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/****************************************************************************/
/**
*  @file    dmacHw.h
*
*  @brief   API definitions for low level DMA controller driver
*
*/
/****************************************************************************/
#ifndef _DMACHW_H
#define _DMACHW_H

#include <stddef.h>

#include <csp/stdint.h>
#include <mach/csp/dmacHw_reg.h>

/* Define DMA Channel ID using DMA controller number (m) and channel number (c).

   System specific channel ID should be defined as follows

   For example:

   #include <dmacHw.h>
   ...
   #define systemHw_LCD_CHANNEL_ID                dmacHw_MAKE_CHANNEL_ID(0,5)
   #define systemHw_SWITCH_RX_CHANNEL_ID          dmacHw_MAKE_CHANNEL_ID(0,0)
   #define systemHw_SWITCH_TX_CHANNEL_ID          dmacHw_MAKE_CHANNEL_ID(0,1)
   #define systemHw_APM_RX_CHANNEL_ID             dmacHw_MAKE_CHANNEL_ID(0,3)
   #define systemHw_APM_TX_CHANNEL_ID             dmacHw_MAKE_CHANNEL_ID(0,4)
   ...
   #define systemHw_SHARED1_CHANNEL_ID            dmacHw_MAKE_CHANNEL_ID(1,4)
   #define systemHw_SHARED2_CHANNEL_ID            dmacHw_MAKE_CHANNEL_ID(1,5)
   #define systemHw_SHARED3_CHANNEL_ID            dmacHw_MAKE_CHANNEL_ID(0,6)
   ...
*/
#define dmacHw_MAKE_CHANNEL_ID(m, c)         (m << 8 | c)

typedef enum {
	dmacHw_CHANNEL_PRIORITY_0 = dmacHw_REG_CFG_LO_CH_PRIORITY_0,	/* Channel priority 0. Lowest priority DMA channel */
	dmacHw_CHANNEL_PRIORITY_1 = dmacHw_REG_CFG_LO_CH_PRIORITY_1,	/* Channel priority 1 */
	dmacHw_CHANNEL_PRIORITY_2 = dmacHw_REG_CFG_LO_CH_PRIORITY_2,	/* Channel priority 2 */
	dmacHw_CHANNEL_PRIORITY_3 = dmacHw_REG_CFG_LO_CH_PRIORITY_3,	/* Channel priority 3 */
	dmacHw_CHANNEL_PRIORITY_4 = dmacHw_REG_CFG_LO_CH_PRIORITY_4,	/* Channel priority 4 */
	dmacHw_CHANNEL_PRIORITY_5 = dmacHw_REG_CFG_LO_CH_PRIORITY_5,	/* Channel priority 5 */
	dmacHw_CHANNEL_PRIORITY_6 = dmacHw_REG_CFG_LO_CH_PRIORITY_6,	/* Channel priority 6 */
	dmacHw_CHANNEL_PRIORITY_7 = dmacHw_REG_CFG_LO_CH_PRIORITY_7	/* Channel priority 7. Highest priority DMA channel */
} dmacHw_CHANNEL_PRIORITY_e;

/* Source destination master interface */
typedef enum {
	dmacHw_SRC_MASTER_INTERFACE_1 = dmacHw_REG_CTL_SMS_1,	/* Source DMA master interface 1 */
	dmacHw_SRC_MASTER_INTERFACE_2 = dmacHw_REG_CTL_SMS_2,	/* Source DMA master interface 2 */
	dmacHw_DST_MASTER_INTERFACE_1 = dmacHw_REG_CTL_DMS_1,	/* Destination DMA master interface 1 */
	dmacHw_DST_MASTER_INTERFACE_2 = dmacHw_REG_CTL_DMS_2	/* Destination DMA master interface 2 */
} dmacHw_MASTER_INTERFACE_e;

typedef enum {
	dmacHw_SRC_TRANSACTION_WIDTH_8 = dmacHw_REG_CTL_SRC_TR_WIDTH_8,	/* Source 8 bit  (1 byte) per transaction */
	dmacHw_SRC_TRANSACTION_WIDTH_16 = dmacHw_REG_CTL_SRC_TR_WIDTH_16,	/* Source 16 bit (2 byte) per transaction */
	dmacHw_SRC_TRANSACTION_WIDTH_32 = dmacHw_REG_CTL_SRC_TR_WIDTH_32,	/* Source 32 bit (4 byte) per transaction */
	dmacHw_SRC_TRANSACTION_WIDTH_64 = dmacHw_REG_CTL_SRC_TR_WIDTH_64,	/* Source 64 bit (8 byte) per transaction */
	dmacHw_DST_TRANSACTION_WIDTH_8 = dmacHw_REG_CTL_DST_TR_WIDTH_8,	/* Destination 8 bit  (1 byte) per transaction */
	dmacHw_DST_TRANSACTION_WIDTH_16 = dmacHw_REG_CTL_DST_TR_WIDTH_16,	/* Destination 16 bit (2 byte) per transaction */
	dmacHw_DST_TRANSACTION_WIDTH_32 = dmacHw_REG_CTL_DST_TR_WIDTH_32,	/* Destination 32 bit (4 byte) per transaction */
	dmacHw_DST_TRANSACTION_WIDTH_64 = dmacHw_REG_CTL_DST_TR_WIDTH_64	/* Destination 64 bit (8 byte) per transaction */
} dmacHw_TRANSACTION_WIDTH_e;

typedef enum {
	dmacHw_SRC_BURST_WIDTH_0 = dmacHw_REG_CTL_SRC_MSIZE_0,	/* Source No burst */
	dmacHw_SRC_BURST_WIDTH_4 = dmacHw_REG_CTL_SRC_MSIZE_4,	/* Source 4  X dmacHw_TRANSACTION_WIDTH_xxx bytes per burst */
	dmacHw_SRC_BURST_WIDTH_8 = dmacHw_REG_CTL_SRC_MSIZE_8,	/* Source 8  X dmacHw_TRANSACTION_WIDTH_xxx bytes per burst */
	dmacHw_SRC_BURST_WIDTH_16 = dmacHw_REG_CTL_SRC_MSIZE_16,	/* Source 16 X dmacHw_TRANSACTION_WIDTH_xxx bytes per burst */
	dmacHw_DST_BURST_WIDTH_0 = dmacHw_REG_CTL_DST_MSIZE_0,	/* Destination No burst */
	dmacHw_DST_BURST_WIDTH_4 = dmacHw_REG_CTL_DST_MSIZE_4,	/* Destination 4  X dmacHw_TRANSACTION_WIDTH_xxx bytes per burst */
	dmacHw_DST_BURST_WIDTH_8 = dmacHw_REG_CTL_DST_MSIZE_8,	/* Destination 8  X dmacHw_TRANSACTION_WIDTH_xxx bytes per burst */
	dmacHw_DST_BURST_WIDTH_16 = dmacHw_REG_CTL_DST_MSIZE_16	/* Destination 16 X dmacHw_TRANSACTION_WIDTH_xxx bytes per burst */
} dmacHw_BURST_WIDTH_e;

typedef enum {
	dmacHw_TRANSFER_TYPE_MEM_TO_MEM = dmacHw_REG_CTL_TTFC_MM_DMAC,	/* Memory to memory transfer */
	dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM = dmacHw_REG_CTL_TTFC_PM_DMAC,	/* Peripheral to memory transfer */
	dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL = dmacHw_REG_CTL_TTFC_MP_DMAC,	/* Memory to peripheral transfer */
	dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_PERIPHERAL = dmacHw_REG_CTL_TTFC_PP_DMAC	/* Peripheral to peripheral transfer */
} dmacHw_TRANSFER_TYPE_e;

typedef enum {
	dmacHw_TRANSFER_MODE_PERREQUEST,	/* Block transfer per DMA request */
	dmacHw_TRANSFER_MODE_CONTINUOUS,	/* Continuous transfer of streaming data */
	dmacHw_TRANSFER_MODE_PERIODIC	/* Periodic transfer of streaming data */
} dmacHw_TRANSFER_MODE_e;

typedef enum {
	dmacHw_SRC_ADDRESS_UPDATE_MODE_INC = dmacHw_REG_CTL_SINC_INC,	/* Increment source address after every transaction */
	dmacHw_SRC_ADDRESS_UPDATE_MODE_DEC = dmacHw_REG_CTL_SINC_DEC,	/* Decrement source address after every transaction */
	dmacHw_DST_ADDRESS_UPDATE_MODE_INC = dmacHw_REG_CTL_DINC_INC,	/* Increment destination address after every transaction */
	dmacHw_DST_ADDRESS_UPDATE_MODE_DEC = dmacHw_REG_CTL_DINC_DEC,	/* Decrement destination address after every transaction */
	dmacHw_SRC_ADDRESS_UPDATE_MODE_NC = dmacHw_REG_CTL_SINC_NC,	/* No change in source address after every transaction */
	dmacHw_DST_ADDRESS_UPDATE_MODE_NC = dmacHw_REG_CTL_DINC_NC	/* No change in destination address after every transaction */
} dmacHw_ADDRESS_UPDATE_MODE_e;

typedef enum {
	dmacHw_FLOW_CONTROL_DMA,	/* DMA working as flow controller (default) */
	dmacHw_FLOW_CONTROL_PERIPHERAL	/* Peripheral working as flow controller */
} dmacHw_FLOW_CONTROL_e;

typedef enum {
	dmacHw_TRANSFER_STATUS_BUSY,	/* DMA Transfer ongoing */
	dmacHw_TRANSFER_STATUS_DONE,	/* DMA Transfer completed */
	dmacHw_TRANSFER_STATUS_ERROR	/* DMA Transfer error */
} dmacHw_TRANSFER_STATUS_e;

typedef enum {
	dmacHw_INTERRUPT_DISABLE,	/* Interrupt disable  */
	dmacHw_INTERRUPT_ENABLE	/* Interrupt enable */
} dmacHw_INTERRUPT_e;

typedef enum {
	dmacHw_INTERRUPT_STATUS_NONE = 0x0,	/* No DMA interrupt */
	dmacHw_INTERRUPT_STATUS_TRANS = 0x1,	/* End of DMA transfer interrupt */
	dmacHw_INTERRUPT_STATUS_BLOCK = 0x2,	/* End of block transfer interrupt */
	dmacHw_INTERRUPT_STATUS_ERROR = 0x4	/* Error interrupt */
} dmacHw_INTERRUPT_STATUS_e;

typedef enum {
	dmacHw_CONTROLLER_ATTRIB_CHANNEL_NUM,	/* Number of DMA channel */
	dmacHw_CONTROLLER_ATTRIB_CHANNEL_MAX_BLOCK_SIZE,	/* Maximum channel burst size */
	dmacHw_CONTROLLER_ATTRIB_MASTER_INTF_NUM,	/* Number of DMA master interface */
	dmacHw_CONTROLLER_ATTRIB_CHANNEL_BUS_WIDTH,	/* Channel Data bus width */
	dmacHw_CONTROLLER_ATTRIB_CHANNEL_FIFO_SIZE	/* Channel FIFO size */
} dmacHw_CONTROLLER_ATTRIB_e;

typedef unsigned long dmacHw_HANDLE_t;	/* DMA channel handle */
typedef uint32_t dmacHw_ID_t;	/* DMA channel Id.  Must be created using
				   "dmacHw_MAKE_CHANNEL_ID" macro
				 */
/* DMA channel configuration parameters */
typedef struct {
	uint32_t srcPeripheralPort;	/* Source peripheral port */
	uint32_t dstPeripheralPort;	/* Destination peripheral port */
	uint32_t srcStatusRegisterAddress;	/* Source status register address */
	uint32_t dstStatusRegisterAddress;	/* Destination status register address of type  */

	uint32_t srcGatherWidth;	/* Number of bytes gathered before successive gather opearation */
	uint32_t srcGatherJump;	/* Number of bytes jumpped before successive gather opearation */
	uint32_t dstScatterWidth;	/* Number of bytes sacattered before successive scatter opearation */
	uint32_t dstScatterJump;	/* Number of bytes jumpped  before successive scatter opearation */
	uint32_t maxDataPerBlock;	/* Maximum number of bytes to be transferred per block/descrptor.
					   0 = Maximum possible.
					 */

	dmacHw_ADDRESS_UPDATE_MODE_e srcUpdate;	/* Source address update mode */
	dmacHw_ADDRESS_UPDATE_MODE_e dstUpdate;	/* Destination address update mode */
	dmacHw_TRANSFER_TYPE_e transferType;	/* DMA transfer type  */
	dmacHw_TRANSFER_MODE_e transferMode;	/* DMA transfer mode */
	dmacHw_MASTER_INTERFACE_e srcMasterInterface;	/* DMA source interface  */
	dmacHw_MASTER_INTERFACE_e dstMasterInterface;	/* DMA destination interface */
	dmacHw_TRANSACTION_WIDTH_e srcMaxTransactionWidth;	/* Source transaction width   */
	dmacHw_TRANSACTION_WIDTH_e dstMaxTransactionWidth;	/* Destination transaction width */
	dmacHw_BURST_WIDTH_e srcMaxBurstWidth;	/* Source burst width */
	dmacHw_BURST_WIDTH_e dstMaxBurstWidth;	/* Destination burst width */
	dmacHw_INTERRUPT_e blockTransferInterrupt;	/* Block trsnafer interrupt */
	dmacHw_INTERRUPT_e completeTransferInterrupt;	/* Complete DMA trsnafer interrupt */
	dmacHw_INTERRUPT_e errorInterrupt;	/* Error interrupt */
	dmacHw_CHANNEL_PRIORITY_e channelPriority;	/* Channel priority */
	dmacHw_FLOW_CONTROL_e flowControler;	/* Data flow controller */
} dmacHw_CONFIG_t;

/****************************************************************************/
/**
*  @brief   Initializes DMA
*
*  This function initializes DMA CSP driver
*
*  @note
*     Must be called before using any DMA channel
*/
/****************************************************************************/
void dmacHw_initDma(void);

/****************************************************************************/
/**
*  @brief   Exit function for  DMA
*
*  This function isolates DMA from the system
*
*/
/****************************************************************************/
void dmacHw_exitDma(void);

/****************************************************************************/
/**
*  @brief   Gets a handle to a DMA channel
*
*  This function returns a handle, representing a control block of a particular DMA channel
*
*  @return  -1       - On Failure
*            handle  - On Success, representing a channel control block
*
*  @note
*     None  Channel ID must be created using "dmacHw_MAKE_CHANNEL_ID" macro
*/
/****************************************************************************/
dmacHw_HANDLE_t dmacHw_getChannelHandle(dmacHw_ID_t channelId	/* [ IN ] DMA Channel Id */
    );

/****************************************************************************/
/**
*  @brief   Initializes a DMA channel for use
*
*  This function initializes and resets a DMA channel for use
*
*  @return  -1     - On Failure
*            0     - On Success
*
*  @note
*     None
*/
/****************************************************************************/
int dmacHw_initChannel(dmacHw_HANDLE_t handle	/*  [ IN ] DMA Channel handle  */
    );

/****************************************************************************/
/**
*  @brief  Estimates number of descriptor needed to perform certain DMA transfer
*
*
*  @return  On failure : -1
*           On success : Number of descriptor count
*
*
*/
/****************************************************************************/
int dmacHw_calculateDescriptorCount(dmacHw_CONFIG_t *pConfig,	/*   [ IN ] Configuration settings */
				    void *pSrcAddr,	/*   [ IN ] Source (Peripheral/Memory) address */
				    void *pDstAddr,	/*   [ IN ] Destination (Peripheral/Memory) address */
				    size_t dataLen	/*   [ IN ] Data length in bytes */
    );

/****************************************************************************/
/**
*  @brief   Initializes descriptor ring
*
*  This function will initializes the descriptor ring of a DMA channel
*
*
*  @return   -1 - On failure
*             0 - On success
*  @note
*     - "len" parameter should be obtained from "dmacHw_descriptorLen"
*     - Descriptor buffer MUST be 32 bit aligned and uncached as it
*       is accessed by ARM and DMA
*/
/****************************************************************************/
int dmacHw_initDescriptor(void *pDescriptorVirt,	/*  [ IN ] Virtual address of uncahced buffer allocated to form descriptor ring */
			  uint32_t descriptorPhyAddr,	/*  [ IN ] Physical address of pDescriptorVirt (descriptor buffer) */
			  uint32_t len,	/*  [ IN ] Size of the pBuf */
			  uint32_t num	/*  [ IN ] Number of descriptor in the ring */
    );

/****************************************************************************/
/**
*  @brief  Finds amount of memory required to form a descriptor ring
*
*
*  @return   Number of bytes required to form a descriptor ring
*
*
*  @note
*     None
*/
/****************************************************************************/
uint32_t dmacHw_descriptorLen(uint32_t descCnt	/*  [ IN ] Number of descriptor in the ring */
    );

/****************************************************************************/
/**
*  @brief   Configure DMA channel
*
*  @return  0  : On success
*           -1 : On failure
*/
/****************************************************************************/
int dmacHw_configChannel(dmacHw_HANDLE_t handle,	/*  [ IN ] DMA Channel handle  */
			 dmacHw_CONFIG_t *pConfig	/*   [ IN ] Configuration settings */
    );

/****************************************************************************/
/**
*  @brief   Set descriptors for known data length
*
*  When DMA has to work as a flow controller, this function prepares the
*  descriptor chain to transfer data
*
*  from:
*          - Memory to memory
*          - Peripheral to memory
*          - Memory to Peripheral
*          - Peripheral to Peripheral
*
*  @return   -1 - On failure
*             0 - On success
*
*/
/****************************************************************************/
int dmacHw_setDataDescriptor(dmacHw_CONFIG_t *pConfig,	/*  [ IN ] Configuration settings */
			     void *pDescriptor,	/*  [ IN ] Descriptor buffer  */
			     void *pSrcAddr,	/*  [ IN ] Source (Peripheral/Memory) address */
			     void *pDstAddr,	/*  [ IN ] Destination (Peripheral/Memory) address */
			     size_t dataLen	/*  [ IN ] Length in bytes   */
    );

/****************************************************************************/
/**
*  @brief   Indicates whether DMA transfer is in progress or completed
*
*  @return   DMA transfer status
*          dmacHw_TRANSFER_STATUS_BUSY:         DMA Transfer ongoing
*          dmacHw_TRANSFER_STATUS_DONE:         DMA Transfer completed
*          dmacHw_TRANSFER_STATUS_ERROR:        DMA Transfer error
*
*/
/****************************************************************************/
dmacHw_TRANSFER_STATUS_e dmacHw_transferCompleted(dmacHw_HANDLE_t handle	/*   [ IN ] DMA Channel handle  */
    );

/****************************************************************************/
/**
*  @brief   Set descriptor carrying control information
*
*  This function will be used to send specific control information to the device
*  using the DMA channel
*
*
*  @return  -1 - On failure
*            0 - On success
*
*  @note
*     None
*/
/****************************************************************************/
int dmacHw_setControlDescriptor(dmacHw_CONFIG_t *pConfig,	/*  [ IN ] Configuration settings */
				void *pDescriptor,	/*  [ IN ] Descriptor buffer  */
				uint32_t ctlAddress,	/*  [ IN ] Address of the device control register  */
				uint32_t control	/*  [ IN ] Device control information */
    );

/****************************************************************************/
/**
*  @brief   Read data DMA transferred to memory
*
*  This function will read data that has been DMAed to memory while transfering from:
*          - Memory to memory
*          - Peripheral to memory
*
*  @return  0 - No more data is available to read
*           1 - More data might be available to read
*
*/
/****************************************************************************/
int dmacHw_readTransferredData(dmacHw_HANDLE_t handle,	/*  [ IN ] DMA Channel handle    */
			       dmacHw_CONFIG_t *pConfig,	/*  [ IN ]  Configuration settings */
			       void *pDescriptor,	/*  [ IN ] Descriptor buffer  */
			       void **ppBbuf,	/*  [ OUT ] Data received */
			       size_t *pLlen	/*  [ OUT ] Length of the data received */
    );

/****************************************************************************/
/**
*  @brief   Prepares descriptor ring, when source peripheral working as a flow controller
*
*  This function will form the descriptor ring by allocating buffers, when source peripheral
*  has to work as a flow controller to transfer data from:
*           - Peripheral to memory.
*
*  @return  -1 - On failure
*            0 - On success
*
*
*  @note
*     None
*/
/****************************************************************************/
int dmacHw_setVariableDataDescriptor(dmacHw_HANDLE_t handle,	/*  [ IN ] DMA Channel handle   */
				     dmacHw_CONFIG_t *pConfig,	/*  [ IN ] Configuration settings */
				     void *pDescriptor,	/*  [ IN ] Descriptor buffer  */
				     uint32_t srcAddr,	/*  [ IN ] Source peripheral address */
				     void *(*fpAlloc) (int len),	/*  [ IN ] Function pointer  that provides destination memory */
				     int len,	/*  [ IN ] Number of bytes "fpAlloc" will allocate for destination */
				     int num	/*  [ IN ] Number of descriptor to set */
    );

/****************************************************************************/
/**
*  @brief   Program channel register to initiate transfer
*
*  @return  void
*
*
*  @note
*     - Descriptor buffer MUST ALWAYS be flushed before calling this function
*     - This function should also be called from ISR to program the channel with
*       pending descriptors
*/
/****************************************************************************/
void dmacHw_initiateTransfer(dmacHw_HANDLE_t handle,	/*   [ IN ] DMA Channel handle */
			     dmacHw_CONFIG_t *pConfig,	/*   [ IN ] Configuration settings */
			     void *pDescriptor	/*   [ IN ] Descriptor buffer  */
    );

/****************************************************************************/
/**
*  @brief   Resets descriptor control information
*
*  @return  void
*/
/****************************************************************************/
void dmacHw_resetDescriptorControl(void *pDescriptor	/*   [ IN ] Descriptor buffer  */
    );

/****************************************************************************/
/**
*  @brief   Program channel register to stop transfer
*
*  Ensures the channel is not doing any transfer after calling this function
*
*  @return  void
*
*/
/****************************************************************************/
void dmacHw_stopTransfer(dmacHw_HANDLE_t handle	/*   [ IN ] DMA Channel handle */
    );

/****************************************************************************/
/**
*  @brief   Check the existance of pending descriptor
*
*  This function confirmes if there is any pending descriptor in the chain
*  to program the channel
*
*  @return  1 : Channel need to be programmed with pending descriptor
*           0 : No more pending descriptor to programe the channel
*
*  @note
*     - This function should be called from ISR in case there are pending
*       descriptor to program the channel.
*
*     Example:
*
*     dmac_isr ()
*     {
*         ...
*         if (dmacHw_descriptorPending (handle))
*         {
*            dmacHw_initiateTransfer (handle);
*         }
*     }
*
*/
/****************************************************************************/
uint32_t dmacHw_descriptorPending(dmacHw_HANDLE_t handle,	/*   [ IN ] DMA Channel handle */
				  void *pDescriptor	/*   [ IN ] Descriptor buffer */
    );

/****************************************************************************/
/**
*  @brief   Deallocates source or destination memory, allocated
*
*  This function can be called to deallocate data memory that was DMAed successfully
*
*  @return  -1  - On failure
*            0  - On success
*
*  @note
*     This function will be called ONLY, when source OR destination address is pointing
*     to dynamic memory
*/
/****************************************************************************/
int dmacHw_freeMem(dmacHw_CONFIG_t *pConfig,	/*  [ IN ] Configuration settings */
		   void *pDescriptor,	/*  [ IN ] Descriptor buffer  */
		   void (*fpFree) (void *)	/*  [ IN ] Function pointer to free data memory */
    );

/****************************************************************************/
/**
*  @brief   Clears the interrupt
*
*  This function clears the DMA channel specific interrupt
*
*  @return   N/A
*
*  @note
*     Must be called under the context of ISR
*/
/****************************************************************************/
void dmacHw_clearInterrupt(dmacHw_HANDLE_t handle	/*  [ IN ] DMA Channel handle  */
    );

/****************************************************************************/
/**
*  @brief   Returns the cause of channel specific DMA interrupt
*
*  This function returns the cause of interrupt
*
*  @return  Interrupt status, each bit representing a specific type of interrupt
*           of type dmacHw_INTERRUPT_STATUS_e
*  @note
*           This function should be called under the context of ISR
*/
/****************************************************************************/
dmacHw_INTERRUPT_STATUS_e dmacHw_getInterruptStatus(dmacHw_HANDLE_t handle	/*  [ IN ] DMA Channel handle  */
    );

/****************************************************************************/
/**
*  @brief   Indentifies a DMA channel causing interrupt
*
*  This functions returns a channel causing interrupt of type dmacHw_INTERRUPT_STATUS_e
*
*  @return  NULL   : No channel causing DMA interrupt
*           ! NULL : Handle to a channel causing DMA interrupt
*  @note
*     dmacHw_clearInterrupt() must be called with a valid handle after calling this function
*/
/****************************************************************************/
dmacHw_HANDLE_t dmacHw_getInterruptSource(void);

/****************************************************************************/
/**
*  @brief   Sets channel specific user data
*
*  This function associates user data to a specif DMA channel
*
*/
/****************************************************************************/
void dmacHw_setChannelUserData(dmacHw_HANDLE_t handle,	/*  [ IN ] DMA Channel handle  */
			       void *userData	/*  [ IN ] User data  */
    );

/****************************************************************************/
/**
*  @brief   Gets channel specific user data
*
*  This function returns user data specific to a DMA channel
*
*  @return   user data
*/
/****************************************************************************/
void *dmacHw_getChannelUserData(dmacHw_HANDLE_t handle	/*  [ IN ] DMA Channel handle  */
    );

/****************************************************************************/
/**
*  @brief   Displays channel specific registers and other control parameters
*
*
*  @return  void
*
*  @note
*     None
*/
/****************************************************************************/
void dmacHw_printDebugInfo(dmacHw_HANDLE_t handle,	/*  [ IN ] DMA Channel handle  */
			   void *pDescriptor,	/*  [ IN ] Descriptor buffer  */
			   int (*fpPrint) (const char *, ...)	/*  [ IN ] Print callback function */
    );

/****************************************************************************/
/**
*  @brief   Provides DMA controller attributes
*
*
*  @return  DMA controller attributes
*
*  @note
*     None
*/
/****************************************************************************/
uint32_t dmacHw_getDmaControllerAttribute(dmacHw_HANDLE_t handle,	/*  [ IN ]  DMA Channel handle  */
					  dmacHw_CONTROLLER_ATTRIB_e attr	/*  [ IN ]  DMA Controler attribute of type  dmacHw_CONTROLLER_ATTRIB_e */
    );

#endif /* _DMACHW_H */
