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
*   @file   dma.c
*
*   @brief  Implements the DMA interface.
*/
/****************************************************************************/

/* ---- Include Files ---------------------------------------------------- */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/irqreturn.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>

#include <mach/timer.h>

#include <linux/pfn.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <mach/dma.h>

/* ---- Public Variables ------------------------------------------------- */

/* ---- Private Constants and Types -------------------------------------- */

#define MAKE_HANDLE(controllerIdx, channelIdx)    (((controllerIdx) << 4) | (channelIdx))

#define CONTROLLER_FROM_HANDLE(handle)    (((handle) >> 4) & 0x0f)
#define CHANNEL_FROM_HANDLE(handle)       ((handle) & 0x0f)


/* ---- Private Variables ------------------------------------------------ */

static DMA_Global_t gDMA;
static struct proc_dir_entry *gDmaDir;

#include "dma_device.c"

/* ---- Private Function Prototypes -------------------------------------- */

/* ---- Functions  ------------------------------------------------------- */

/****************************************************************************/
/**
*   Displays information for /proc/dma/channels
*/
/****************************************************************************/

static int dma_proc_read_channels(char *buf, char **start, off_t offset,
				  int count, int *eof, void *data)
{
	int controllerIdx;
	int channelIdx;
	int limit = count - 200;
	int len = 0;
	DMA_Channel_t *channel;

	if (down_interruptible(&gDMA.lock) < 0) {
		return -ERESTARTSYS;
	}

	for (controllerIdx = 0; controllerIdx < DMA_NUM_CONTROLLERS;
	     controllerIdx++) {
		for (channelIdx = 0; channelIdx < DMA_NUM_CHANNELS;
		     channelIdx++) {
			if (len >= limit) {
				break;
			}

			channel =
			    &gDMA.controller[controllerIdx].channel[channelIdx];

			len +=
			    sprintf(buf + len, "%d:%d ", controllerIdx,
				    channelIdx);

			if ((channel->flags & DMA_CHANNEL_FLAG_IS_DEDICATED) !=
			    0) {
				len +=
				    sprintf(buf + len, "Dedicated for %s ",
					    DMA_gDeviceAttribute[channel->
								 devType].name);
			} else {
				len += sprintf(buf + len, "Shared ");
			}

			if ((channel->flags & DMA_CHANNEL_FLAG_NO_ISR) != 0) {
				len += sprintf(buf + len, "No ISR ");
			}

			if ((channel->flags & DMA_CHANNEL_FLAG_LARGE_FIFO) != 0) {
				len += sprintf(buf + len, "Fifo: 128 ");
			} else {
				len += sprintf(buf + len, "Fifo: 64  ");
			}

			if ((channel->flags & DMA_CHANNEL_FLAG_IN_USE) != 0) {
				len +=
				    sprintf(buf + len, "InUse by %s",
					    DMA_gDeviceAttribute[channel->
								 devType].name);
#if (DMA_DEBUG_TRACK_RESERVATION)
				len +=
				    sprintf(buf + len, " (%s:%d)",
					    channel->fileName,
					    channel->lineNum);
#endif
			} else {
				len += sprintf(buf + len, "Avail ");
			}

			if (channel->lastDevType != DMA_DEVICE_NONE) {
				len +=
				    sprintf(buf + len, "Last use: %s ",
					    DMA_gDeviceAttribute[channel->
								 lastDevType].
					    name);
			}

			len += sprintf(buf + len, "\n");
		}
	}
	up(&gDMA.lock);
	*eof = 1;

	return len;
}

/****************************************************************************/
/**
*   Displays information for /proc/dma/devices
*/
/****************************************************************************/

static int dma_proc_read_devices(char *buf, char **start, off_t offset,
				 int count, int *eof, void *data)
{
	int limit = count - 200;
	int len = 0;
	int devIdx;

	if (down_interruptible(&gDMA.lock) < 0) {
		return -ERESTARTSYS;
	}

	for (devIdx = 0; devIdx < DMA_NUM_DEVICE_ENTRIES; devIdx++) {
		DMA_DeviceAttribute_t *devAttr = &DMA_gDeviceAttribute[devIdx];

		if (devAttr->name == NULL) {
			continue;
		}

		if (len >= limit) {
			break;
		}

		len += sprintf(buf + len, "%-12s ", devAttr->name);

		if ((devAttr->flags & DMA_DEVICE_FLAG_IS_DEDICATED) != 0) {
			len +=
			    sprintf(buf + len, "Dedicated %d:%d ",
				    devAttr->dedicatedController,
				    devAttr->dedicatedChannel);
		} else {
			len += sprintf(buf + len, "Shared DMA:");
			if ((devAttr->flags & DMA_DEVICE_FLAG_ON_DMA0) != 0) {
				len += sprintf(buf + len, "0");
			}
			if ((devAttr->flags & DMA_DEVICE_FLAG_ON_DMA1) != 0) {
				len += sprintf(buf + len, "1");
			}
			len += sprintf(buf + len, " ");
		}
		if ((devAttr->flags & DMA_DEVICE_FLAG_NO_ISR) != 0) {
			len += sprintf(buf + len, "NoISR ");
		}
		if ((devAttr->flags & DMA_DEVICE_FLAG_ALLOW_LARGE_FIFO) != 0) {
			len += sprintf(buf + len, "Allow-128 ");
		}

		len +=
		    sprintf(buf + len,
			    "Xfer #: %Lu Ticks: %Lu Bytes: %Lu DescLen: %u\n",
			    devAttr->numTransfers, devAttr->transferTicks,
			    devAttr->transferBytes,
			    devAttr->ring.bytesAllocated);

	}

	up(&gDMA.lock);
	*eof = 1;

	return len;
}

/****************************************************************************/
/**
*   Determines if a DMA_Device_t is "valid".
*
*   @return
*       TRUE        - dma device is valid
*       FALSE       - dma device isn't valid
*/
/****************************************************************************/

static inline int IsDeviceValid(DMA_Device_t device)
{
	return (device >= 0) && (device < DMA_NUM_DEVICE_ENTRIES);
}

/****************************************************************************/
/**
*   Translates a DMA handle into a pointer to a channel.
*
*   @return
*       non-NULL    - pointer to DMA_Channel_t
*       NULL        - DMA Handle was invalid
*/
/****************************************************************************/

static inline DMA_Channel_t *HandleToChannel(DMA_Handle_t handle)
{
	int controllerIdx;
	int channelIdx;

	controllerIdx = CONTROLLER_FROM_HANDLE(handle);
	channelIdx = CHANNEL_FROM_HANDLE(handle);

	if ((controllerIdx > DMA_NUM_CONTROLLERS)
	    || (channelIdx > DMA_NUM_CHANNELS)) {
		return NULL;
	}
	return &gDMA.controller[controllerIdx].channel[channelIdx];
}

/****************************************************************************/
/**
*   Interrupt handler which is called to process DMA interrupts.
*/
/****************************************************************************/

static irqreturn_t dma_interrupt_handler(int irq, void *dev_id)
{
	DMA_Channel_t *channel;
	DMA_DeviceAttribute_t *devAttr;
	int irqStatus;

	channel = (DMA_Channel_t *) dev_id;

	/* Figure out why we were called, and knock down the interrupt */

	irqStatus = dmacHw_getInterruptStatus(channel->dmacHwHandle);
	dmacHw_clearInterrupt(channel->dmacHwHandle);

	if ((channel->devType < 0)
	    || (channel->devType > DMA_NUM_DEVICE_ENTRIES)) {
		printk(KERN_ERR "dma_interrupt_handler: Invalid devType: %d\n",
		       channel->devType);
		return IRQ_NONE;
	}
	devAttr = &DMA_gDeviceAttribute[channel->devType];

	/* Update stats */

	if ((irqStatus & dmacHw_INTERRUPT_STATUS_TRANS) != 0) {
		devAttr->transferTicks +=
		    (timer_get_tick_count() - devAttr->transferStartTime);
	}

	if ((irqStatus & dmacHw_INTERRUPT_STATUS_ERROR) != 0) {
		printk(KERN_ERR
		       "dma_interrupt_handler: devType :%d DMA error (%s)\n",
		       channel->devType, devAttr->name);
	} else {
		devAttr->numTransfers++;
		devAttr->transferBytes += devAttr->numBytes;
	}

	/* Call any installed handler */

	if (devAttr->devHandler != NULL) {
		devAttr->devHandler(channel->devType, irqStatus,
				    devAttr->userData);
	}

	return IRQ_HANDLED;
}

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
*       -EINVAL     Invalid parameters passed in
*       -ENOMEM     Unable to allocate memory for the desired number of descriptors.
*/
/****************************************************************************/

int dma_alloc_descriptor_ring(DMA_DescriptorRing_t *ring,	/* Descriptor ring to populate */
			      int numDescriptors	/* Number of descriptors that need to be allocated. */
    ) {
	size_t bytesToAlloc = dmacHw_descriptorLen(numDescriptors);

	if ((ring == NULL) || (numDescriptors <= 0)) {
		return -EINVAL;
	}

	ring->physAddr = 0;
	ring->descriptorsAllocated = 0;
	ring->bytesAllocated = 0;

	ring->virtAddr = dma_alloc_writecombine(NULL,
						     bytesToAlloc,
						     &ring->physAddr,
						     GFP_KERNEL);
	if (ring->virtAddr == NULL) {
		return -ENOMEM;
	}

	ring->bytesAllocated = bytesToAlloc;
	ring->descriptorsAllocated = numDescriptors;

	return dma_init_descriptor_ring(ring, numDescriptors);
}

EXPORT_SYMBOL(dma_alloc_descriptor_ring);

/****************************************************************************/
/**
*   Releases the memory which was previously allocated for a descriptor ring.
*/
/****************************************************************************/

void dma_free_descriptor_ring(DMA_DescriptorRing_t *ring	/* Descriptor to release */
    ) {
	if (ring->virtAddr != NULL) {
		dma_free_writecombine(NULL,
				      ring->bytesAllocated,
				      ring->virtAddr, ring->physAddr);
	}

	ring->bytesAllocated = 0;
	ring->descriptorsAllocated = 0;
	ring->virtAddr = NULL;
	ring->physAddr = 0;
}

EXPORT_SYMBOL(dma_free_descriptor_ring);

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
    ) {
	if (ring->virtAddr == NULL) {
		return -EINVAL;
	}
	if (dmacHw_initDescriptor(ring->virtAddr,
				  ring->physAddr,
				  ring->bytesAllocated, numDescriptors) < 0) {
		printk(KERN_ERR
		       "dma_init_descriptor_ring: dmacHw_initDescriptor failed\n");
		return -ENOMEM;
	}

	return 0;
}

EXPORT_SYMBOL(dma_init_descriptor_ring);

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
*       -ENODEV - Device handed in is invalid.
*       -EINVAL Invalid parameters
*       -ENOMEM Memory exhausted
*/
/****************************************************************************/

int dma_calculate_descriptor_count(DMA_Device_t device,	/* DMA Device that this will be associated with */
				   dma_addr_t srcData,	/* Place to get data to write to device */
				   dma_addr_t dstData,	/* Pointer to device data address */
				   size_t numBytes	/* Number of bytes to transfer to the device */
    ) {
	int numDescriptors;
	DMA_DeviceAttribute_t *devAttr;

	if (!IsDeviceValid(device)) {
		return -ENODEV;
	}
	devAttr = &DMA_gDeviceAttribute[device];

	numDescriptors = dmacHw_calculateDescriptorCount(&devAttr->config,
							      (void *)srcData,
							      (void *)dstData,
							      numBytes);
	if (numDescriptors < 0) {
		printk(KERN_ERR
		       "dma_calculate_descriptor_count: dmacHw_calculateDescriptorCount failed\n");
		return -EINVAL;
	}

	return numDescriptors;
}

EXPORT_SYMBOL(dma_calculate_descriptor_count);

/****************************************************************************/
/**
*   Adds a region of memory to the descriptor ring. Note that it may take
*   multiple descriptors for each region of memory. It is the callers
*   responsibility to allocate a sufficiently large descriptor ring.
*
*   @return
*       0       Descriptors were added successfully
*       -ENODEV Device handed in is invalid.
*       -EINVAL Invalid parameters
*       -ENOMEM Memory exhausted
*/
/****************************************************************************/

int dma_add_descriptors(DMA_DescriptorRing_t *ring,	/* Descriptor ring to add descriptors to */
			DMA_Device_t device,	/* DMA Device that descriptors are for */
			dma_addr_t srcData,	/* Place to get data (memory or device) */
			dma_addr_t dstData,	/* Place to put data (memory or device) */
			size_t numBytes	/* Number of bytes to transfer to the device */
    ) {
	int rc;
	DMA_DeviceAttribute_t *devAttr;

	if (!IsDeviceValid(device)) {
		return -ENODEV;
	}
	devAttr = &DMA_gDeviceAttribute[device];

	rc = dmacHw_setDataDescriptor(&devAttr->config,
				      ring->virtAddr,
				      (void *)srcData,
				      (void *)dstData, numBytes);
	if (rc < 0) {
		printk(KERN_ERR
		       "dma_add_descriptors: dmacHw_setDataDescriptor failed with code: %d\n",
		       rc);
		return -ENOMEM;
	}

	return 0;
}

EXPORT_SYMBOL(dma_add_descriptors);

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
*
*   @return
*       0       Descriptors were added successfully
*       -ENODEV Device handed in is invalid.
*/
/****************************************************************************/

int dma_set_device_descriptor_ring(DMA_Device_t device,	/* Device to update the descriptor ring for. */
				   DMA_DescriptorRing_t *ring	/* Descriptor ring to add descriptors to */
    ) {
	DMA_DeviceAttribute_t *devAttr;

	if (!IsDeviceValid(device)) {
		return -ENODEV;
	}
	devAttr = &DMA_gDeviceAttribute[device];

	/* Free the previously allocated descriptor ring */

	dma_free_descriptor_ring(&devAttr->ring);

	if (ring != NULL) {
		/* Copy in the new one */

		devAttr->ring = *ring;
	}

	/* Set things up so that if dma_transfer is called then this descriptor */
	/* ring will get freed. */

	devAttr->prevSrcData = 0;
	devAttr->prevDstData = 0;
	devAttr->prevNumBytes = 0;

	return 0;
}

EXPORT_SYMBOL(dma_set_device_descriptor_ring);

/****************************************************************************/
/**
*   Retrieves the descriptor ring associated with a device.
*
*   @return
*       0       Descriptors were added successfully
*       -ENODEV Device handed in is invalid.
*/
/****************************************************************************/

int dma_get_device_descriptor_ring(DMA_Device_t device,	/* Device to retrieve the descriptor ring for. */
				   DMA_DescriptorRing_t *ring	/* Place to store retrieved ring */
    ) {
	DMA_DeviceAttribute_t *devAttr;

	memset(ring, 0, sizeof(*ring));

	if (!IsDeviceValid(device)) {
		return -ENODEV;
	}
	devAttr = &DMA_gDeviceAttribute[device];

	*ring = devAttr->ring;

	return 0;
}

EXPORT_SYMBOL(dma_get_device_descriptor_ring);

/****************************************************************************/
/**
*   Configures a DMA channel.
*
*   @return
*       >= 0    - Initialization was successful.
*
*       -EBUSY  - Device is currently being used.
*       -ENODEV - Device handed in is invalid.
*/
/****************************************************************************/

static int ConfigChannel(DMA_Handle_t handle)
{
	DMA_Channel_t *channel;
	DMA_DeviceAttribute_t *devAttr;
	int controllerIdx;

	channel = HandleToChannel(handle);
	if (channel == NULL) {
		return -ENODEV;
	}
	devAttr = &DMA_gDeviceAttribute[channel->devType];
	controllerIdx = CONTROLLER_FROM_HANDLE(handle);

	if ((devAttr->flags & DMA_DEVICE_FLAG_PORT_PER_DMAC) != 0) {
		if (devAttr->config.transferType ==
		    dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL) {
			devAttr->config.dstPeripheralPort =
			    devAttr->dmacPort[controllerIdx];
		} else if (devAttr->config.transferType ==
			   dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM) {
			devAttr->config.srcPeripheralPort =
			    devAttr->dmacPort[controllerIdx];
		}
	}

	if (dmacHw_configChannel(channel->dmacHwHandle, &devAttr->config) != 0) {
		printk(KERN_ERR "ConfigChannel: dmacHw_configChannel failed\n");
		return -EIO;
	}

	return 0;
}

/****************************************************************************/
/**
*   Initializes all of the data structures associated with the DMA.
*   @return
*       >= 0    - Initialization was successful.
*
*       -EBUSY  - Device is currently being used.
*       -ENODEV - Device handed in is invalid.
*/
/****************************************************************************/

int dma_init(void)
{
	int rc = 0;
	int controllerIdx;
	int channelIdx;
	DMA_Device_t devIdx;
	DMA_Channel_t *channel;
	DMA_Handle_t dedicatedHandle;

	memset(&gDMA, 0, sizeof(gDMA));

	sema_init(&gDMA.lock, 0);
	init_waitqueue_head(&gDMA.freeChannelQ);

	/* Initialize the Hardware */

	dmacHw_initDma();

	/* Start off by marking all of the DMA channels as shared. */

	for (controllerIdx = 0; controllerIdx < DMA_NUM_CONTROLLERS;
	     controllerIdx++) {
		for (channelIdx = 0; channelIdx < DMA_NUM_CHANNELS;
		     channelIdx++) {
			channel =
			    &gDMA.controller[controllerIdx].channel[channelIdx];

			channel->flags = 0;
			channel->devType = DMA_DEVICE_NONE;
			channel->lastDevType = DMA_DEVICE_NONE;

#if (DMA_DEBUG_TRACK_RESERVATION)
			channel->fileName = "";
			channel->lineNum = 0;
#endif

			channel->dmacHwHandle =
			    dmacHw_getChannelHandle(dmacHw_MAKE_CHANNEL_ID
						    (controllerIdx,
						     channelIdx));
			dmacHw_initChannel(channel->dmacHwHandle);
		}
	}

	/* Record any special attributes that channels may have */

	gDMA.controller[0].channel[0].flags |= DMA_CHANNEL_FLAG_LARGE_FIFO;
	gDMA.controller[0].channel[1].flags |= DMA_CHANNEL_FLAG_LARGE_FIFO;
	gDMA.controller[1].channel[0].flags |= DMA_CHANNEL_FLAG_LARGE_FIFO;
	gDMA.controller[1].channel[1].flags |= DMA_CHANNEL_FLAG_LARGE_FIFO;

	/* Now walk through and record the dedicated channels. */

	for (devIdx = 0; devIdx < DMA_NUM_DEVICE_ENTRIES; devIdx++) {
		DMA_DeviceAttribute_t *devAttr = &DMA_gDeviceAttribute[devIdx];

		if (((devAttr->flags & DMA_DEVICE_FLAG_NO_ISR) != 0)
		    && ((devAttr->flags & DMA_DEVICE_FLAG_IS_DEDICATED) == 0)) {
			printk(KERN_ERR
			       "DMA Device: %s Can only request NO_ISR for dedicated devices\n",
			       devAttr->name);
			rc = -EINVAL;
			goto out;
		}

		if ((devAttr->flags & DMA_DEVICE_FLAG_IS_DEDICATED) != 0) {
			/* This is a dedicated device. Mark the channel as being reserved. */

			if (devAttr->dedicatedController >= DMA_NUM_CONTROLLERS) {
				printk(KERN_ERR
				       "DMA Device: %s DMA Controller %d is out of range\n",
				       devAttr->name,
				       devAttr->dedicatedController);
				rc = -EINVAL;
				goto out;
			}

			if (devAttr->dedicatedChannel >= DMA_NUM_CHANNELS) {
				printk(KERN_ERR
				       "DMA Device: %s DMA Channel %d is out of range\n",
				       devAttr->name,
				       devAttr->dedicatedChannel);
				rc = -EINVAL;
				goto out;
			}

			dedicatedHandle =
			    MAKE_HANDLE(devAttr->dedicatedController,
					devAttr->dedicatedChannel);
			channel = HandleToChannel(dedicatedHandle);

			if ((channel->flags & DMA_CHANNEL_FLAG_IS_DEDICATED) !=
			    0) {
				printk
				    ("DMA Device: %s attempting to use same DMA Controller:Channel (%d:%d) as %s\n",
				     devAttr->name,
				     devAttr->dedicatedController,
				     devAttr->dedicatedChannel,
				     DMA_gDeviceAttribute[channel->devType].
				     name);
				rc = -EBUSY;
				goto out;
			}

			channel->flags |= DMA_CHANNEL_FLAG_IS_DEDICATED;
			channel->devType = devIdx;

			if (devAttr->flags & DMA_DEVICE_FLAG_NO_ISR) {
				channel->flags |= DMA_CHANNEL_FLAG_NO_ISR;
			}

			/* For dedicated channels, we can go ahead and configure the DMA channel now */
			/* as well. */

			ConfigChannel(dedicatedHandle);
		}
	}

	/* Go through and register the interrupt handlers */

	for (controllerIdx = 0; controllerIdx < DMA_NUM_CONTROLLERS;
	     controllerIdx++) {
		for (channelIdx = 0; channelIdx < DMA_NUM_CHANNELS;
		     channelIdx++) {
			channel =
			    &gDMA.controller[controllerIdx].channel[channelIdx];

			if ((channel->flags & DMA_CHANNEL_FLAG_NO_ISR) == 0) {
				snprintf(channel->name, sizeof(channel->name),
					 "dma %d:%d %s", controllerIdx,
					 channelIdx,
					 channel->devType ==
					 DMA_DEVICE_NONE ? "" :
					 DMA_gDeviceAttribute[channel->devType].
					 name);

				rc =
				     request_irq(IRQ_DMA0C0 +
						 (controllerIdx *
						  DMA_NUM_CHANNELS) +
						 channelIdx,
						 dma_interrupt_handler,
						 IRQF_DISABLED, channel->name,
						 channel);
				if (rc != 0) {
					printk(KERN_ERR
					       "request_irq for IRQ_DMA%dC%d failed\n",
					       controllerIdx, channelIdx);
				}
			}
		}
	}

	/* Create /proc/dma/channels and /proc/dma/devices */

	gDmaDir = proc_mkdir("dma", NULL);

	if (gDmaDir == NULL) {
		printk(KERN_ERR "Unable to create /proc/dma\n");
	} else {
		create_proc_read_entry("channels", 0, gDmaDir,
				       dma_proc_read_channels, NULL);
		create_proc_read_entry("devices", 0, gDmaDir,
				       dma_proc_read_devices, NULL);
	}

out:

	up(&gDMA.lock);

	return rc;
}

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

#if (DMA_DEBUG_TRACK_RESERVATION)
DMA_Handle_t dma_request_channel_dbg
    (DMA_Device_t dev, const char *fileName, int lineNum)
#else
DMA_Handle_t dma_request_channel(DMA_Device_t dev)
#endif
{
	DMA_Handle_t handle;
	DMA_DeviceAttribute_t *devAttr;
	DMA_Channel_t *channel;
	int controllerIdx;
	int controllerIdx2;
	int channelIdx;

	if (down_interruptible(&gDMA.lock) < 0) {
		return -ERESTARTSYS;
	}

	if ((dev < 0) || (dev >= DMA_NUM_DEVICE_ENTRIES)) {
		handle = -ENODEV;
		goto out;
	}
	devAttr = &DMA_gDeviceAttribute[dev];

#if (DMA_DEBUG_TRACK_RESERVATION)
	{
		char *s;

		s = strrchr(fileName, '/');
		if (s != NULL) {
			fileName = s + 1;
		}
	}
#endif
	if ((devAttr->flags & DMA_DEVICE_FLAG_IN_USE) != 0) {
		/* This device has already been requested and not been freed */

		printk(KERN_ERR "%s: device %s is already requested\n",
		       __func__, devAttr->name);
		handle = -EBUSY;
		goto out;
	}

	if ((devAttr->flags & DMA_DEVICE_FLAG_IS_DEDICATED) != 0) {
		/* This device has a dedicated channel. */

		channel =
		    &gDMA.controller[devAttr->dedicatedController].
		    channel[devAttr->dedicatedChannel];
		if ((channel->flags & DMA_CHANNEL_FLAG_IN_USE) != 0) {
			handle = -EBUSY;
			goto out;
		}

		channel->flags |= DMA_CHANNEL_FLAG_IN_USE;
		devAttr->flags |= DMA_DEVICE_FLAG_IN_USE;

#if (DMA_DEBUG_TRACK_RESERVATION)
		channel->fileName = fileName;
		channel->lineNum = lineNum;
#endif
		handle =
		    MAKE_HANDLE(devAttr->dedicatedController,
				devAttr->dedicatedChannel);
		goto out;
	}

	/* This device needs to use one of the shared channels. */

	handle = DMA_INVALID_HANDLE;
	while (handle == DMA_INVALID_HANDLE) {
		/* Scan through the shared channels and see if one is available */

		for (controllerIdx2 = 0; controllerIdx2 < DMA_NUM_CONTROLLERS;
		     controllerIdx2++) {
			/* Check to see if we should try on controller 1 first. */

			controllerIdx = controllerIdx2;
			if ((devAttr->
			     flags & DMA_DEVICE_FLAG_ALLOC_DMA1_FIRST) != 0) {
				controllerIdx = 1 - controllerIdx;
			}

			/* See if the device is available on the controller being tested */

			if ((devAttr->
			     flags & (DMA_DEVICE_FLAG_ON_DMA0 << controllerIdx))
			    != 0) {
				for (channelIdx = 0;
				     channelIdx < DMA_NUM_CHANNELS;
				     channelIdx++) {
					channel =
					    &gDMA.controller[controllerIdx].
					    channel[channelIdx];

					if (((channel->
					      flags &
					      DMA_CHANNEL_FLAG_IS_DEDICATED) ==
					     0)
					    &&
					    ((channel->
					      flags & DMA_CHANNEL_FLAG_IN_USE)
					     == 0)) {
						if (((channel->
						      flags &
						      DMA_CHANNEL_FLAG_LARGE_FIFO)
						     != 0)
						    &&
						    ((devAttr->
						      flags &
						      DMA_DEVICE_FLAG_ALLOW_LARGE_FIFO)
						     == 0)) {
							/* This channel is a large fifo - don't tie it up */
							/* with devices that we don't want using it. */

							continue;
						}

						channel->flags |=
						    DMA_CHANNEL_FLAG_IN_USE;
						channel->devType = dev;
						devAttr->flags |=
						    DMA_DEVICE_FLAG_IN_USE;

#if (DMA_DEBUG_TRACK_RESERVATION)
						channel->fileName = fileName;
						channel->lineNum = lineNum;
#endif
						handle =
						    MAKE_HANDLE(controllerIdx,
								channelIdx);

						/* Now that we've reserved the channel - we can go ahead and configure it */

						if (ConfigChannel(handle) != 0) {
							handle = -EIO;
							printk(KERN_ERR
							       "dma_request_channel: ConfigChannel failed\n");
						}
						goto out;
					}
				}
			}
		}

		/* No channels are currently available. Let's wait for one to free up. */

		{
			DEFINE_WAIT(wait);

			prepare_to_wait(&gDMA.freeChannelQ, &wait,
					TASK_INTERRUPTIBLE);
			up(&gDMA.lock);
			schedule();
			finish_wait(&gDMA.freeChannelQ, &wait);

			if (signal_pending(current)) {
				/* We don't currently hold gDMA.lock, so we return directly */

				return -ERESTARTSYS;
			}
		}

		if (down_interruptible(&gDMA.lock)) {
			return -ERESTARTSYS;
		}
	}

out:
	up(&gDMA.lock);

	return handle;
}

/* Create both _dbg and non _dbg functions for modules. */

#if (DMA_DEBUG_TRACK_RESERVATION)
#undef dma_request_channel
DMA_Handle_t dma_request_channel(DMA_Device_t dev)
{
	return dma_request_channel_dbg(dev, __FILE__, __LINE__);
}

EXPORT_SYMBOL(dma_request_channel_dbg);
#endif
EXPORT_SYMBOL(dma_request_channel);

/****************************************************************************/
/**
*   Frees a previously allocated DMA Handle.
*/
/****************************************************************************/

int dma_free_channel(DMA_Handle_t handle	/* DMA handle. */
    ) {
	int rc = 0;
	DMA_Channel_t *channel;
	DMA_DeviceAttribute_t *devAttr;

	if (down_interruptible(&gDMA.lock) < 0) {
		return -ERESTARTSYS;
	}

	channel = HandleToChannel(handle);
	if (channel == NULL) {
		rc = -EINVAL;
		goto out;
	}

	devAttr = &DMA_gDeviceAttribute[channel->devType];

	if ((channel->flags & DMA_CHANNEL_FLAG_IS_DEDICATED) == 0) {
		channel->lastDevType = channel->devType;
		channel->devType = DMA_DEVICE_NONE;
	}
	channel->flags &= ~DMA_CHANNEL_FLAG_IN_USE;
	devAttr->flags &= ~DMA_DEVICE_FLAG_IN_USE;

out:
	up(&gDMA.lock);

	wake_up_interruptible(&gDMA.freeChannelQ);

	return rc;
}

EXPORT_SYMBOL(dma_free_channel);

/****************************************************************************/
/**
*   Determines if a given device has been configured as using a shared
*   channel.
*
*   @return
*       0           Device uses a dedicated channel
*       > zero      Device uses a shared channel
*       < zero      Error code
*/
/****************************************************************************/

int dma_device_is_channel_shared(DMA_Device_t device	/* Device to check. */
    ) {
	DMA_DeviceAttribute_t *devAttr;

	if (!IsDeviceValid(device)) {
		return -ENODEV;
	}
	devAttr = &DMA_gDeviceAttribute[device];

	return ((devAttr->flags & DMA_DEVICE_FLAG_IS_DEDICATED) == 0);
}

EXPORT_SYMBOL(dma_device_is_channel_shared);

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
    ) {
	DMA_Channel_t *channel;
	DMA_DeviceAttribute_t *devAttr;
	int numDescriptors;
	size_t ringBytesRequired;
	int rc = 0;

	channel = HandleToChannel(handle);
	if (channel == NULL) {
		return -ENODEV;
	}

	devAttr = &DMA_gDeviceAttribute[channel->devType];

	if (devAttr->config.transferType != transferType) {
		return -EINVAL;
	}

	/* Figure out how many descriptors we need. */

	/* printk("srcData: 0x%08x dstData: 0x%08x, numBytes: %d\n", */
	/*        srcData, dstData, numBytes); */

	numDescriptors = dmacHw_calculateDescriptorCount(&devAttr->config,
							      (void *)srcData,
							      (void *)dstData,
							      numBytes);
	if (numDescriptors < 0) {
		printk(KERN_ERR "%s: dmacHw_calculateDescriptorCount failed\n",
		       __func__);
		return -EINVAL;
	}

	/* Check to see if we can reuse the existing descriptor ring, or if we need to allocate */
	/* a new one. */

	ringBytesRequired = dmacHw_descriptorLen(numDescriptors);

	/* printk("ringBytesRequired: %d\n", ringBytesRequired); */

	if (ringBytesRequired > devAttr->ring.bytesAllocated) {
		/* Make sure that this code path is never taken from interrupt context. */
		/* It's OK for an interrupt to initiate a DMA transfer, but the descriptor */
		/* allocation needs to have already been done. */

		might_sleep();

		/* Free the old descriptor ring and allocate a new one. */

		dma_free_descriptor_ring(&devAttr->ring);

		/* And allocate a new one. */

		rc =
		     dma_alloc_descriptor_ring(&devAttr->ring,
					       numDescriptors);
		if (rc < 0) {
			printk(KERN_ERR
			       "%s: dma_alloc_descriptor_ring(%d) failed\n",
			       __func__, numDescriptors);
			return rc;
		}
		/* Setup the descriptor for this transfer */

		if (dmacHw_initDescriptor(devAttr->ring.virtAddr,
					  devAttr->ring.physAddr,
					  devAttr->ring.bytesAllocated,
					  numDescriptors) < 0) {
			printk(KERN_ERR "%s: dmacHw_initDescriptor failed\n",
			       __func__);
			return -EINVAL;
		}
	} else {
		/* We've already got enough ring buffer allocated. All we need to do is reset */
		/* any control information, just in case the previous DMA was stopped. */

		dmacHw_resetDescriptorControl(devAttr->ring.virtAddr);
	}

	/* dma_alloc/free both set the prevSrc/DstData to 0. If they happen to be the same */
	/* as last time, then we don't need to call setDataDescriptor again. */

	if (dmacHw_setDataDescriptor(&devAttr->config,
				     devAttr->ring.virtAddr,
				     (void *)srcData,
				     (void *)dstData, numBytes) < 0) {
		printk(KERN_ERR "%s: dmacHw_setDataDescriptor failed\n",
		       __func__);
		return -EINVAL;
	}

	/* Remember the critical information for this transfer so that we can eliminate */
	/* another call to dma_alloc_descriptors if the caller reuses the same buffers */

	devAttr->prevSrcData = srcData;
	devAttr->prevDstData = dstData;
	devAttr->prevNumBytes = numBytes;

	return 0;
}

EXPORT_SYMBOL(dma_alloc_descriptors);

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
    ) {
	DMA_Channel_t *channel;
	DMA_DeviceAttribute_t *devAttr;
	int numDst1Descriptors;
	int numDst2Descriptors;
	int numDescriptors;
	size_t ringBytesRequired;
	int rc = 0;

	channel = HandleToChannel(handle);
	if (channel == NULL) {
		return -ENODEV;
	}

	devAttr = &DMA_gDeviceAttribute[channel->devType];

	/* Figure out how many descriptors we need. */

	/* printk("srcData: 0x%08x dstData: 0x%08x, numBytes: %d\n", */
	/*        srcData, dstData, numBytes); */

	numDst1Descriptors =
	     dmacHw_calculateDescriptorCount(&devAttr->config, (void *)srcData,
					     (void *)dstData1, numBytes);
	if (numDst1Descriptors < 0) {
		return -EINVAL;
	}
	numDst2Descriptors =
	     dmacHw_calculateDescriptorCount(&devAttr->config, (void *)srcData,
					     (void *)dstData2, numBytes);
	if (numDst2Descriptors < 0) {
		return -EINVAL;
	}
	numDescriptors = numDst1Descriptors + numDst2Descriptors;
	/* printk("numDescriptors: %d\n", numDescriptors); */

	/* Check to see if we can reuse the existing descriptor ring, or if we need to allocate */
	/* a new one. */

	ringBytesRequired = dmacHw_descriptorLen(numDescriptors);

	/* printk("ringBytesRequired: %d\n", ringBytesRequired); */

	if (ringBytesRequired > devAttr->ring.bytesAllocated) {
		/* Make sure that this code path is never taken from interrupt context. */
		/* It's OK for an interrupt to initiate a DMA transfer, but the descriptor */
		/* allocation needs to have already been done. */

		might_sleep();

		/* Free the old descriptor ring and allocate a new one. */

		dma_free_descriptor_ring(&devAttr->ring);

		/* And allocate a new one. */

		rc =
		     dma_alloc_descriptor_ring(&devAttr->ring,
					       numDescriptors);
		if (rc < 0) {
			printk(KERN_ERR
			       "%s: dma_alloc_descriptor_ring(%d) failed\n",
			       __func__, ringBytesRequired);
			return rc;
		}
	}

	/* Setup the descriptor for this transfer. Since this function is used with */
	/* CONTINUOUS DMA operations, we need to reinitialize every time, otherwise */
	/* setDataDescriptor will keep trying to append onto the end. */

	if (dmacHw_initDescriptor(devAttr->ring.virtAddr,
				  devAttr->ring.physAddr,
				  devAttr->ring.bytesAllocated,
				  numDescriptors) < 0) {
		printk(KERN_ERR "%s: dmacHw_initDescriptor failed\n", __func__);
		return -EINVAL;
	}

	/* dma_alloc/free both set the prevSrc/DstData to 0. If they happen to be the same */
	/* as last time, then we don't need to call setDataDescriptor again. */

	if (dmacHw_setDataDescriptor(&devAttr->config,
				     devAttr->ring.virtAddr,
				     (void *)srcData,
				     (void *)dstData1, numBytes) < 0) {
		printk(KERN_ERR "%s: dmacHw_setDataDescriptor 1 failed\n",
		       __func__);
		return -EINVAL;
	}
	if (dmacHw_setDataDescriptor(&devAttr->config,
				     devAttr->ring.virtAddr,
				     (void *)srcData,
				     (void *)dstData2, numBytes) < 0) {
		printk(KERN_ERR "%s: dmacHw_setDataDescriptor 2 failed\n",
		       __func__);
		return -EINVAL;
	}

	/* You should use dma_start_transfer rather than dma_transfer_xxx so we don't */
	/* try to make the 'prev' variables right. */

	devAttr->prevSrcData = 0;
	devAttr->prevDstData = 0;
	devAttr->prevNumBytes = 0;

	return numDescriptors;
}

EXPORT_SYMBOL(dma_alloc_double_dst_descriptors);

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

int dma_start_transfer(DMA_Handle_t handle)
{
	DMA_Channel_t *channel;
	DMA_DeviceAttribute_t *devAttr;

	channel = HandleToChannel(handle);
	if (channel == NULL) {
		return -ENODEV;
	}
	devAttr = &DMA_gDeviceAttribute[channel->devType];

	dmacHw_initiateTransfer(channel->dmacHwHandle, &devAttr->config,
				devAttr->ring.virtAddr);

	/* Since we got this far, everything went successfully */

	return 0;
}

EXPORT_SYMBOL(dma_start_transfer);

/****************************************************************************/
/**
*   Stops a previously started DMA transfer.
*
*   @return
*       0       Transfer was stopped successfully
*       -ENODEV Invalid handle
*/
/****************************************************************************/

int dma_stop_transfer(DMA_Handle_t handle)
{
	DMA_Channel_t *channel;

	channel = HandleToChannel(handle);
	if (channel == NULL) {
		return -ENODEV;
	}

	dmacHw_stopTransfer(channel->dmacHwHandle);

	return 0;
}

EXPORT_SYMBOL(dma_stop_transfer);

/****************************************************************************/
/**
*   Waits for a DMA to complete by polling. This function is only intended
*   to be used for testing. Interrupts should be used for most DMA operations.
*/
/****************************************************************************/

int dma_wait_transfer_done(DMA_Handle_t handle)
{
	DMA_Channel_t *channel;
	dmacHw_TRANSFER_STATUS_e status;

	channel = HandleToChannel(handle);
	if (channel == NULL) {
		return -ENODEV;
	}

	while ((status =
		dmacHw_transferCompleted(channel->dmacHwHandle)) ==
	       dmacHw_TRANSFER_STATUS_BUSY) {
		;
	}

	if (status == dmacHw_TRANSFER_STATUS_ERROR) {
		printk(KERN_ERR "%s: DMA transfer failed\n", __func__);
		return -EIO;
	}
	return 0;
}

EXPORT_SYMBOL(dma_wait_transfer_done);

/****************************************************************************/
/**
*   Initiates a DMA, allocating the descriptors as required.
*
*   @return
*       0       Transfer was started successfully
*       -EINVAL Invalid device type for this kind of transfer
*               (i.e. the device is _DEV_TO_MEM and not _MEM_TO_DEV)
*/
/****************************************************************************/

int dma_transfer(DMA_Handle_t handle,	/* DMA Handle */
		 dmacHw_TRANSFER_TYPE_e transferType,	/* Type of transfer being performed */
		 dma_addr_t srcData,	/* Place to get data to write to device */
		 dma_addr_t dstData,	/* Pointer to device data address */
		 size_t numBytes	/* Number of bytes to transfer to the device */
    ) {
	DMA_Channel_t *channel;
	DMA_DeviceAttribute_t *devAttr;
	int rc = 0;

	channel = HandleToChannel(handle);
	if (channel == NULL) {
		return -ENODEV;
	}

	devAttr = &DMA_gDeviceAttribute[channel->devType];

	if (devAttr->config.transferType != transferType) {
		return -EINVAL;
	}

	/* We keep track of the information about the previous request for this */
	/* device, and if the attributes match, then we can use the descriptors we setup */
	/* the last time, and not have to reinitialize everything. */

	{
		rc =
		     dma_alloc_descriptors(handle, transferType, srcData,
					   dstData, numBytes);
		if (rc != 0) {
			return rc;
		}
	}

	/* And kick off the transfer */

	devAttr->numBytes = numBytes;
	devAttr->transferStartTime = timer_get_tick_count();

	dmacHw_initiateTransfer(channel->dmacHwHandle, &devAttr->config,
				devAttr->ring.virtAddr);

	/* Since we got this far, everything went successfully */

	return 0;
}

EXPORT_SYMBOL(dma_transfer);

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
    ) {
	DMA_DeviceAttribute_t *devAttr;
	unsigned long flags;

	if (!IsDeviceValid(dev)) {
		return -ENODEV;
	}
	devAttr = &DMA_gDeviceAttribute[dev];

	local_irq_save(flags);

	devAttr->userData = userData;
	devAttr->devHandler = devHandler;

	local_irq_restore(flags);

	return 0;
}

EXPORT_SYMBOL(dma_set_device_handler);
