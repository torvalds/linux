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
*   @file   dma.h
*
*   @brief  API definitions for the linux DMA interface.
*/
/****************************************************************************/

#if !defined(ASM_ARM_ARCH_BCMRING_DMA_H)
#define ASM_ARM_ARCH_BCMRING_DMA_H

/* ---- Include Files ---------------------------------------------------- */

#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <mach/csp/dmacHw.h>
#include <mach/timer.h>

/* ---- Constants and Types ---------------------------------------------- */

/* If DMA_DEBUG_TRACK_RESERVATION is set to a non-zero value, then the filename */
/* and line number of the reservation request will be recorded in the channel table */

#define DMA_DEBUG_TRACK_RESERVATION   1

#define DMA_NUM_CONTROLLERS     2
#define DMA_NUM_CHANNELS        8	/* per controller */

typedef enum {
	DMA_DEVICE_MEM_TO_MEM,	/* For memory to memory transfers */
	DMA_DEVICE_I2S0_DEV_TO_MEM,
	DMA_DEVICE_I2S0_MEM_TO_DEV,
	DMA_DEVICE_I2S1_DEV_TO_MEM,
	DMA_DEVICE_I2S1_MEM_TO_DEV,
	DMA_DEVICE_APM_CODEC_A_DEV_TO_MEM,
	DMA_DEVICE_APM_CODEC_A_MEM_TO_DEV,
	DMA_DEVICE_APM_CODEC_B_DEV_TO_MEM,
	DMA_DEVICE_APM_CODEC_B_MEM_TO_DEV,
	DMA_DEVICE_APM_CODEC_C_DEV_TO_MEM,	/* Additional mic input for beam-forming */
	DMA_DEVICE_APM_PCM0_DEV_TO_MEM,
	DMA_DEVICE_APM_PCM0_MEM_TO_DEV,
	DMA_DEVICE_APM_PCM1_DEV_TO_MEM,
	DMA_DEVICE_APM_PCM1_MEM_TO_DEV,
	DMA_DEVICE_SPUM_DEV_TO_MEM,
	DMA_DEVICE_SPUM_MEM_TO_DEV,
	DMA_DEVICE_SPIH_DEV_TO_MEM,
	DMA_DEVICE_SPIH_MEM_TO_DEV,
	DMA_DEVICE_UART_A_DEV_TO_MEM,
	DMA_DEVICE_UART_A_MEM_TO_DEV,
	DMA_DEVICE_UART_B_DEV_TO_MEM,
	DMA_DEVICE_UART_B_MEM_TO_DEV,
	DMA_DEVICE_PIF_MEM_TO_DEV,
	DMA_DEVICE_PIF_DEV_TO_MEM,
	DMA_DEVICE_ESW_DEV_TO_MEM,
	DMA_DEVICE_ESW_MEM_TO_DEV,
	DMA_DEVICE_VPM_MEM_TO_MEM,
	DMA_DEVICE_CLCD_MEM_TO_MEM,
	DMA_DEVICE_NAND_MEM_TO_MEM,
	DMA_DEVICE_MEM_TO_VRAM,
	DMA_DEVICE_VRAM_TO_MEM,

	/* Add new entries before this line. */

	DMA_NUM_DEVICE_ENTRIES,
	DMA_DEVICE_NONE = 0xff,	/* Special value to indicate that no device is currently assigned. */

} DMA_Device_t;

/****************************************************************************
*
*   The DMA_Handle_t is the primary object used by callers of the API.
*
*****************************************************************************/

#define DMA_INVALID_HANDLE  ((DMA_Handle_t) -1)

typedef int DMA_Handle_t;

/****************************************************************************
*
*   The DMA_DescriptorRing_t contains a ring of descriptors which is used
*   to point to regions of memory.
*
*****************************************************************************/

typedef struct {
	void *virtAddr;		/* Virtual Address of the descriptor ring */
	dma_addr_t physAddr;	/* Physical address of the descriptor ring */
	int descriptorsAllocated;	/* Number of descriptors allocated in the descriptor ring */
	size_t bytesAllocated;	/* Number of bytes allocated in the descriptor ring */

} DMA_DescriptorRing_t;

/****************************************************************************
*
*   The DMA_DeviceAttribute_t contains information which describes a
*   particular DMA device (or peripheral).
*
*   It is anticipated that the arrary of DMA_DeviceAttribute_t's will be
*   statically initialized.
*
*****************************************************************************/

/* The device handler is called whenever a DMA operation completes. The reaon */
/* for it to be called will be a bitmask with one or more of the following bits */
/* set. */

#define DMA_HANDLER_REASON_BLOCK_COMPLETE       dmacHw_INTERRUPT_STATUS_BLOCK
#define DMA_HANDLER_REASON_TRANSFER_COMPLETE    dmacHw_INTERRUPT_STATUS_TRANS
#define DMA_HANDLER_REASON_ERROR                dmacHw_INTERRUPT_STATUS_ERROR

typedef void (*DMA_DeviceHandler_t) (DMA_Device_t dev, int reason,
				     void *userData);

#define DMA_DEVICE_FLAG_ON_DMA0             0x00000001
#define DMA_DEVICE_FLAG_ON_DMA1             0x00000002
#define DMA_DEVICE_FLAG_PORT_PER_DMAC       0x00000004	/* If set, it means that the port used on DMAC0 is different from the port used on DMAC1 */
#define DMA_DEVICE_FLAG_ALLOC_DMA1_FIRST    0x00000008	/* If set, allocate from DMA1 before allocating from DMA0 */
#define DMA_DEVICE_FLAG_IS_DEDICATED        0x00000100
#define DMA_DEVICE_FLAG_NO_ISR              0x00000200
#define DMA_DEVICE_FLAG_ALLOW_LARGE_FIFO    0x00000400
#define DMA_DEVICE_FLAG_IN_USE              0x00000800	/* If set, device is in use on a channel */

/* Note: Some DMA devices can be used from multiple DMA Controllers. The bitmask is used to */
/*       determine which DMA controllers a given device can be used from, and the interface */
/*       array determeines the actual interface number to use for a given controller. */

typedef struct {
	uint32_t flags;		/* Bitmask of DMA_DEVICE_FLAG_xxx constants */
	uint8_t dedicatedController;	/* Controller number to use if DMA_DEVICE_FLAG_IS_DEDICATED is set. */
	uint8_t dedicatedChannel;	/* Channel number to use if DMA_DEVICE_FLAG_IS_DEDICATED is set. */
	const char *name;	/* Will show up in the /proc entry */

	uint32_t dmacPort[DMA_NUM_CONTROLLERS];	/* Specifies the port number when DMA_DEVICE_FLAG_PORT_PER_DMAC flag is set */

	dmacHw_CONFIG_t config;	/* Configuration to use when DMA'ing using this device */

	void *userData;		/* Passed to the devHandler */
	DMA_DeviceHandler_t devHandler;	/* Called when DMA operations finish. */

	timer_tick_count_t transferStartTime;	/* Time the current transfer was started */

	/* The following statistical information will be collected and presented in a proc entry. */
	/* Note: With a contiuous bandwidth of 1 Gb/sec, it would take 584 years to overflow */
	/*       a 64 bit counter. */

	uint64_t numTransfers;	/* Number of DMA transfers performed */
	uint64_t transferTicks;	/* Total time spent doing DMA transfers (measured in timer_tick_count_t's) */
	uint64_t transferBytes;	/* Total bytes transferred */
	uint32_t timesBlocked;	/* Number of times a channel was unavailable */
	uint32_t numBytes;	/* Last transfer size */

	/* It's not possible to free memory which is allocated for the descriptors from within */
	/* the ISR. So make the presumption that a given device will tend to use the */
	/* same sized buffers over and over again, and we keep them around. */

	DMA_DescriptorRing_t ring;	/* Ring of descriptors allocated for this device */

	/* We stash away some of the information from the previous transfer. If back-to-back */
	/* transfers are performed from the same buffer, then we don't have to keep re-initializing */
	/* the descriptor buffers. */

	uint32_t prevNumBytes;
	dma_addr_t prevSrcData;
	dma_addr_t prevDstData;

} DMA_DeviceAttribute_t;

/****************************************************************************
*
*   DMA_Channel_t, DMA_Controller_t, and DMA_State_t are really internal
*   data structures and don't belong in this header file, but are included
*   merely for discussion.
*
*   By the time this is implemented, these structures will be moved out into
*   the appropriate C source file instead.
*
*****************************************************************************/

/****************************************************************************
*
*   The DMA_Channel_t contains state information about each DMA channel. Some
*   of the channels are dedicated. Non-dedicated channels are shared
*   amongst the other devices.
*
*****************************************************************************/

#define DMA_CHANNEL_FLAG_IN_USE         0x00000001
#define DMA_CHANNEL_FLAG_IS_DEDICATED   0x00000002
#define DMA_CHANNEL_FLAG_NO_ISR         0x00000004
#define DMA_CHANNEL_FLAG_LARGE_FIFO     0x00000008

typedef struct {
	uint32_t flags;		/* bitmask of DMA_CHANNEL_FLAG_xxx constants */
	DMA_Device_t devType;	/* Device this channel is currently reserved for */
	DMA_Device_t lastDevType;	/* Device type that used this previously */
	char name[20];		/* Name passed onto request_irq */

#if (DMA_DEBUG_TRACK_RESERVATION)
	const char *fileName;	/* Place where channel reservation took place */
	int lineNum;		/* Place where channel reservation took place */
#endif
	dmacHw_HANDLE_t dmacHwHandle;	/* low level channel handle. */

} DMA_Channel_t;

/****************************************************************************
*
*   The DMA_Controller_t contains state information about each DMA controller.
*
*   The freeChannelQ is stored in the controller data structure rather than
*   the channel data structure since several of the devices are accessible
*   from multiple controllers, and there is no way to know which controller
*   will become available first.
*
*****************************************************************************/

typedef struct {
	DMA_Channel_t channel[DMA_NUM_CHANNELS];

} DMA_Controller_t;

/****************************************************************************
*
*   The DMA_Global_t contains all of the global state information used by
*   the DMA code.
*
*   Callers which need to allocate a shared channel will be queued up
*   on the freeChannelQ until a channel becomes available.
*
*****************************************************************************/

typedef struct {
	struct semaphore lock;	/* acquired when manipulating table entries */
	wait_queue_head_t freeChannelQ;

	DMA_Controller_t controller[DMA_NUM_CONTROLLERS];

} DMA_Global_t;

/* ---- Variable Externs ------------------------------------------------- */

extern DMA_DeviceAttribute_t DMA_gDeviceAttribute[DMA_NUM_DEVICE_ENTRIES];

/* ---- Function Prototypes ---------------------------------------------- */

#if defined(__KERNEL__)

/****************************************************************************/
/**
*   Initializes the DMA module.
*
*   @return
*       0       - Success
*       < 0     - Error
*/
/****************************************************************************/

int dma_init(void);

#if (DMA_DEBUG_TRACK_RESERVATION)
DMA_Handle_t dma_request_channel_dbg(DMA_Device_t dev, const char *fileName,
				     int lineNum);
#define dma_request_channel(dev)  dma_request_channel_dbg(dev, __FILE__, __LINE__)
#else

/****************************************************************************/
/**
*   Reserves a channel for use with @a dev. If the device is setup to use
*   a shared channel, then this function will block until a free channel
*   becomes available.
*
*   @return
*       >= 0    - A valid DMA Handle.
*       -EBUSY  - Device is currently being used.
*       -ENODEV - Device handed in is invalid.
*/
/****************************************************************************/

DMA_Handle_t dma_request_channel(DMA_Device_t dev	/* Device to use with the allocated channel. */
    );
#endif

/****************************************************************************/
/**
*   Frees a previously allocated DMA Handle.
*
*   @return
*        0      - DMA Handle was released successfully.
*       -EINVAL - Invalid DMA handle
*/
/****************************************************************************/

int dma_free_channel(DMA_Handle_t channel	/* DMA handle. */
    );

/****************************************************************************/
/**
*   Determines if a given device has been configured as using a shared
*   channel.
*
*   @return boolean
*       0           Device uses a dedicated channel
*       non-zero    Device uses a shared channel
*/
/****************************************************************************/

int dma_device_is_channel_shared(DMA_Device_t dev	/* Device to check. */
    );

/****************************************************************************/
/**
*   Allocates memory to hold a descriptor ring. The descriptor ring then
*   needs to be populated by making one or more calls to
*   dna_add_descriptors.
*
*   The returned descriptor ring will be automatically initialized.
*
*   @return
*       0           Descriptor ring was allocated successfully
*       -ENOMEM     Unable to allocate memory for the desired number of descriptors.
*/
/****************************************************************************/

int dma_alloc_descriptor_ring(DMA_DescriptorRing_t *ring,	/* Descriptor ring to populate */
			      int numDescriptors	/* Number of descriptors that need to be allocated. */
    );

/****************************************************************************/
/**
*   Releases the memory which was previously allocated for a descriptor ring.
*/
/****************************************************************************/

void dma_free_descriptor_ring(DMA_DescriptorRing_t *ring	/* Descriptor to release */
    );

/****************************************************************************/
/**
*   Initializes a descriptor ring, so that descriptors can be added to it.
*   Once a descriptor ring has been allocated, it may be reinitialized for
*   use with additional/different regions of memory.
*
*   Note that if 7 descriptors are allocated, it's perfectly acceptable to
*   initialize the ring with a smaller number of descriptors. The amount
*   of memory allocated for the descriptor ring will not be reduced, and
*   the descriptor ring may be reinitialized later
*
*   @return
*       0           Descriptor ring was initialized successfully
*       -ENOMEM     The descriptor which was passed in has insufficient space
*                   to hold the desired number of descriptors.
*/
/****************************************************************************/

int dma_init_descriptor_ring(DMA_DescriptorRing_t *ring,	/* Descriptor ring to initialize */
			     int numDescriptors	/* Number of descriptors to initialize. */
    );

/****************************************************************************/
/**
*   Determines the number of descriptors which would be required for a
*   transfer of the indicated memory region.
*
*   This function also needs to know which DMA device this transfer will
*   be destined for, so that the appropriate DMA configuration can be retrieved.
*   DMA parameters such as transfer width, and whether this is a memory-to-memory
*   or memory-to-peripheral, etc can all affect the actual number of descriptors
*   required.
*
*   @return
*       > 0     Returns the number of descriptors required for the indicated transfer
*       -EINVAL Invalid device type for this kind of transfer
*               (i.e. the device is _MEM_TO_DEV and not _DEV_TO_MEM)
*       -ENOMEM Memory exhausted
*/
/****************************************************************************/

int dma_calculate_descriptor_count(DMA_Device_t device,	/* DMA Device that this will be associated with */
				   dma_addr_t srcData,	/* Place to get data to write to device */
				   dma_addr_t dstData,	/* Pointer to device data address */
				   size_t numBytes	/* Number of bytes to transfer to the device */
    );

/****************************************************************************/
/**
*   Adds a region of memory to the descriptor ring. Note that it may take
*   multiple descriptors for each region of memory. It is the callers
*   responsibility to allocate a sufficiently large descriptor ring.
*
*   @return
*       0       Descriptors were added successfully
*       -EINVAL Invalid device type for this kind of transfer
*               (i.e. the device is _MEM_TO_DEV and not _DEV_TO_MEM)
*       -ENOMEM Memory exhausted
*/
/****************************************************************************/

int dma_add_descriptors(DMA_DescriptorRing_t *ring,	/* Descriptor ring to add descriptors to */
			DMA_Device_t device,	/* DMA Device that descriptors are for */
			dma_addr_t srcData,	/* Place to get data (memory or device) */
			dma_addr_t dstData,	/* Place to put data (memory or device) */
			size_t numBytes	/* Number of bytes to transfer to the device */
    );

/****************************************************************************/
/**
*   Sets the descriptor ring associated with a device.
*
*   Once set, the descriptor ring will be associated with the device, even
*   across channel request/free calls. Passing in a NULL descriptor ring
*   will release any descriptor ring currently associated with the device.
*
*   Note: If you call dma_transfer, or one of the other dma_alloc_ functions
*         the descriptor ring may be released and reallocated.
*
*   Note: This function will release the descriptor memory for any current
*         descriptor ring associated with this device.
*/
/****************************************************************************/

int dma_set_device_descriptor_ring(DMA_Device_t device,	/* Device to update the descriptor ring for. */
				   DMA_DescriptorRing_t *ring	/* Descriptor ring to add descriptors to */
    );

/****************************************************************************/
/**
*   Retrieves the descriptor ring associated with a device.
*/
/****************************************************************************/

int dma_get_device_descriptor_ring(DMA_Device_t device,	/* Device to retrieve the descriptor ring for. */
				   DMA_DescriptorRing_t *ring	/* Place to store retrieved ring */
    );

/****************************************************************************/
/**
*   Allocates buffers for the descriptors. This is normally done automatically
*   but needs to be done explicitly when initiating a dma from interrupt
*   context.
*
*   @return
*       0       Descriptors were allocated successfully
*       -EINVAL Invalid device type for this kind of transfer
*               (i.e. the device is _MEM_TO_DEV and not _DEV_TO_MEM)
*       -ENOMEM Memory exhausted
*/
/****************************************************************************/

int dma_alloc_descriptors(DMA_Handle_t handle,	/* DMA Handle */
			  dmacHw_TRANSFER_TYPE_e transferType,	/* Type of transfer being performed */
			  dma_addr_t srcData,	/* Place to get data to write to device */
			  dma_addr_t dstData,	/* Pointer to device data address */
			  size_t numBytes	/* Number of bytes to transfer to the device */
    );

/****************************************************************************/
/**
*   Allocates and sets up descriptors for a double buffered circular buffer.
*
*   This is primarily intended to be used for things like the ingress samples
*   from a microphone.
*
*   @return
*       > 0     Number of descriptors actually allocated.
*       -EINVAL Invalid device type for this kind of transfer
*               (i.e. the device is _MEM_TO_DEV and not _DEV_TO_MEM)
*       -ENOMEM Memory exhausted
*/
/****************************************************************************/

int dma_alloc_double_dst_descriptors(DMA_Handle_t handle,	/* DMA Handle */
				     dma_addr_t srcData,	/* Physical address of source data */
				     dma_addr_t dstData1,	/* Physical address of first destination buffer */
				     dma_addr_t dstData2,	/* Physical address of second destination buffer */
				     size_t numBytes	/* Number of bytes in each destination buffer */
    );

/****************************************************************************/
/**
*   Initiates a transfer when the descriptors have already been setup.
*
*   This is a special case, and normally, the dma_transfer_xxx functions should
*   be used.
*
*   @return
*       0       Transfer was started successfully
*       -ENODEV Invalid handle
*/
/****************************************************************************/

int dma_start_transfer(DMA_Handle_t handle);

/****************************************************************************/
/**
*   Stops a previously started DMA transfer.
*
*   @return
*       0       Transfer was stopped successfully
*       -ENODEV Invalid handle
*/
/****************************************************************************/

int dma_stop_transfer(DMA_Handle_t handle);

/****************************************************************************/
/**
*   Waits for a DMA to complete by polling. This function is only intended
*   to be used for testing. Interrupts should be used for most DMA operations.
*/
/****************************************************************************/

int dma_wait_transfer_done(DMA_Handle_t handle);

/****************************************************************************/
/**
*   Initiates a DMA transfer
*
*   @return
*       0       Transfer was started successfully
*       -EINVAL Invalid device type for this kind of transfer
*               (i.e. the device is _MEM_TO_DEV and not _DEV_TO_MEM)
*/
/****************************************************************************/

int dma_transfer(DMA_Handle_t handle,	/* DMA Handle */
		 dmacHw_TRANSFER_TYPE_e transferType,	/* Type of transfer being performed */
		 dma_addr_t srcData,	/* Place to get data to write to device */
		 dma_addr_t dstData,	/* Pointer to device data address */
		 size_t numBytes	/* Number of bytes to transfer to the device */
    );

/****************************************************************************/
/**
*   Initiates a transfer from memory to a device.
*
*   @return
*       0       Transfer was started successfully
*       -EINVAL Invalid device type for this kind of transfer
*               (i.e. the device is _DEV_TO_MEM and not _MEM_TO_DEV)
*/
/****************************************************************************/

static inline int dma_transfer_to_device(DMA_Handle_t handle,	/* DMA Handle */
					 dma_addr_t srcData,	/* Place to get data to write to device (physical address) */
					 dma_addr_t dstData,	/* Pointer to device data address (physical address) */
					 size_t numBytes	/* Number of bytes to transfer to the device */
    ) {
	return dma_transfer(handle,
			    dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL,
			    srcData, dstData, numBytes);
}

/****************************************************************************/
/**
*   Initiates a transfer from a device to memory.
*
*   @return
*       0       Transfer was started successfully
*       -EINVAL Invalid device type for this kind of transfer
*               (i.e. the device is _MEM_TO_DEV and not _DEV_TO_MEM)
*/
/****************************************************************************/

static inline int dma_transfer_from_device(DMA_Handle_t handle,	/* DMA Handle */
					   dma_addr_t srcData,	/* Pointer to the device data address (physical address) */
					   dma_addr_t dstData,	/* Place to store data retrieved from the device (physical address) */
					   size_t numBytes	/* Number of bytes to retrieve from the device */
    ) {
	return dma_transfer(handle,
			    dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM,
			    srcData, dstData, numBytes);
}

/****************************************************************************/
/**
*   Initiates a memory to memory transfer.
*
*   @return
*       0       Transfer was started successfully
*       -EINVAL Invalid device type for this kind of transfer
*               (i.e. the device wasn't DMA_DEVICE_MEM_TO_MEM)
*/
/****************************************************************************/

static inline int dma_transfer_mem_to_mem(DMA_Handle_t handle,	/* DMA Handle */
					  dma_addr_t srcData,	/* Place to transfer data from (physical address) */
					  dma_addr_t dstData,	/* Place to transfer data to (physical address) */
					  size_t numBytes	/* Number of bytes to transfer */
    ) {
	return dma_transfer(handle,
			    dmacHw_TRANSFER_TYPE_MEM_TO_MEM,
			    srcData, dstData, numBytes);
}

/****************************************************************************/
/**
*   Set the callback function which will be called when a transfer completes.
*   If a NULL callback function is set, then no callback will occur.
*
*   @note   @a devHandler will be called from IRQ context.
*
*   @return
*       0       - Success
*       -ENODEV - Device handed in is invalid.
*/
/****************************************************************************/

int dma_set_device_handler(DMA_Device_t dev,	/* Device to set the callback for. */
			   DMA_DeviceHandler_t devHandler,	/* Function to call when the DMA completes */
			   void *userData	/* Pointer which will be passed to devHandler. */
    );

#endif

#endif /* ASM_ARM_ARCH_BCMRING_DMA_H */
