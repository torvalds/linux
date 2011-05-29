/**
 * DOC: EP93xx DMA M2P memory to peripheral and peripheral to memory engine
 *
 * The EP93xx DMA M2P subsystem handles DMA transfers between memory and
 * peripherals. DMA M2P channels are available for audio, UARTs and IrDA.
 * See chapter 10 of the EP93xx users guide for full details on the DMA M2P
 * engine.
 *
 * See sound/soc/ep93xx/ep93xx-pcm.c for an example use of the DMA M2P code.
 *
 */

#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

#include <linux/list.h>
#include <linux/types.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

/**
 * struct ep93xx_dma_buffer - Information about a buffer to be transferred
 * using the DMA M2P engine
 *
 * @list: Entry in DMA buffer list
 * @bus_addr: Physical address of the buffer
 * @size: Size of the buffer in bytes
 */
struct ep93xx_dma_buffer {
	struct list_head	list;
	u32			bus_addr;
	u16			size;
};

/**
 * struct ep93xx_dma_m2p_client - Information about a DMA M2P client
 *
 * @name: Unique name for this client
 * @flags: Client flags
 * @cookie: User data to pass to callback functions
 * @buffer_started: Non NULL function to call when a transfer is started.
 * 			The arguments are the user data cookie and the DMA
 *			buffer which is starting.
 * @buffer_finished: Non NULL function to call when a transfer is completed.
 *			The arguments are the user data cookie, the DMA buffer
 *			which has completed, and a boolean flag indicating if
 *			the transfer had an error.
 */
struct ep93xx_dma_m2p_client {
	char			*name;
	u8			flags;
	void			*cookie;
	void			(*buffer_started)(void *cookie,
					struct ep93xx_dma_buffer *buf);
	void			(*buffer_finished)(void *cookie,
					struct ep93xx_dma_buffer *buf,
					int bytes, int error);

	/* private: Internal use only */
	void			*channel;
};

/* DMA M2P ports */
#define EP93XX_DMA_M2P_PORT_I2S1	0x00
#define EP93XX_DMA_M2P_PORT_I2S2	0x01
#define EP93XX_DMA_M2P_PORT_AAC1	0x02
#define EP93XX_DMA_M2P_PORT_AAC2	0x03
#define EP93XX_DMA_M2P_PORT_AAC3	0x04
#define EP93XX_DMA_M2P_PORT_I2S3	0x05
#define EP93XX_DMA_M2P_PORT_UART1	0x06
#define EP93XX_DMA_M2P_PORT_UART2	0x07
#define EP93XX_DMA_M2P_PORT_UART3	0x08
#define EP93XX_DMA_M2P_PORT_IRDA	0x09
#define EP93XX_DMA_M2P_PORT_MASK	0x0f

/* DMA M2P client flags */
#define EP93XX_DMA_M2P_TX		0x00	/* Memory to peripheral */
#define EP93XX_DMA_M2P_RX		0x10	/* Peripheral to memory */

/*
 * DMA M2P client error handling flags. See the EP93xx users guide
 * documentation on the DMA M2P CONTROL register for more details
 */
#define EP93XX_DMA_M2P_ABORT_ON_ERROR	0x20	/* Abort on peripheral error */
#define EP93XX_DMA_M2P_IGNORE_ERROR	0x40	/* Ignore peripheral errors */
#define EP93XX_DMA_M2P_ERROR_MASK	0x60	/* Mask of error bits */

/**
 * ep93xx_dma_m2p_client_register - Register a client with the DMA M2P
 * subsystem
 *
 * @m2p: Client information to register
 * returns 0 on success
 *
 * The DMA M2P subsystem allocates a channel and an interrupt line for the DMA
 * client
 */
int ep93xx_dma_m2p_client_register(struct ep93xx_dma_m2p_client *m2p);

/**
 * ep93xx_dma_m2p_client_unregister - Unregister a client from the DMA M2P
 * subsystem
 *
 * @m2p: Client to unregister
 *
 * Any transfers currently in progress will be completed in hardware, but
 * ignored in software.
 */
void ep93xx_dma_m2p_client_unregister(struct ep93xx_dma_m2p_client *m2p);

/**
 * ep93xx_dma_m2p_submit - Submit a DMA M2P transfer
 *
 * @m2p: DMA Client to submit the transfer on
 * @buf: DMA Buffer to submit
 *
 * If the current or next transfer positions are free on the M2P client then
 * the transfer is started immediately. If not, the transfer is added to the
 * list of pending transfers. This function must not be called from the
 * buffer_finished callback for an M2P channel.
 *
 */
void ep93xx_dma_m2p_submit(struct ep93xx_dma_m2p_client *m2p,
			   struct ep93xx_dma_buffer *buf);

/**
 * ep93xx_dma_m2p_submit_recursive - Put a DMA transfer on the pending list
 * for an M2P channel
 *
 * @m2p: DMA Client to submit the transfer on
 * @buf: DMA Buffer to submit
 *
 * This function must only be called from the buffer_finished callback for an
 * M2P channel. It is commonly used to add the next transfer in a chained list
 * of DMA transfers.
 */
void ep93xx_dma_m2p_submit_recursive(struct ep93xx_dma_m2p_client *m2p,
				     struct ep93xx_dma_buffer *buf);

/**
 * ep93xx_dma_m2p_flush - Flush all pending transfers on a DMA M2P client
 *
 * @m2p: DMA client to flush transfers on
 *
 * Any transfers currently in progress will be completed in hardware, but
 * ignored in software.
 *
 */
void ep93xx_dma_m2p_flush(struct ep93xx_dma_m2p_client *m2p);

/*
 * M2P channels.
 *
 * Note that these values are also directly used for setting the PPALLOC
 * register.
 */
#define EP93XX_DMA_I2S1		0
#define EP93XX_DMA_I2S2		1
#define EP93XX_DMA_AAC1		2
#define EP93XX_DMA_AAC2		3
#define EP93XX_DMA_AAC3		4
#define EP93XX_DMA_I2S3		5
#define EP93XX_DMA_UART1	6
#define EP93XX_DMA_UART2	7
#define EP93XX_DMA_UART3	8
#define EP93XX_DMA_IRDA		9
/* M2M channels */
#define EP93XX_DMA_SSP		10
#define EP93XX_DMA_IDE		11

/**
 * struct ep93xx_dma_data - configuration data for the EP93xx dmaengine
 * @port: peripheral which is requesting the channel
 * @direction: TX/RX channel
 * @name: optional name for the channel, this is displayed in /proc/interrupts
 *
 * This information is passed as private channel parameter in a filter
 * function. Note that this is only needed for slave/cyclic channels.  For
 * memcpy channels %NULL data should be passed.
 */
struct ep93xx_dma_data {
	int				port;
	enum dma_data_direction		direction;
	const char			*name;
};

/**
 * struct ep93xx_dma_chan_data - platform specific data for a DMA channel
 * @name: name of the channel, used for getting the right clock for the channel
 * @base: mapped registers
 * @irq: interrupt number used by this channel
 */
struct ep93xx_dma_chan_data {
	const char			*name;
	void __iomem			*base;
	int				irq;
};

/**
 * struct ep93xx_dma_platform_data - platform data for the dmaengine driver
 * @channels: array of channels which are passed to the driver
 * @num_channels: number of channels in the array
 *
 * This structure is passed to the DMA engine driver via platform data. For
 * M2P channels, contract is that even channels are for TX and odd for RX.
 * There is no requirement for the M2M channels.
 */
struct ep93xx_dma_platform_data {
	struct ep93xx_dma_chan_data	*channels;
	size_t				num_channels;
};

static inline bool ep93xx_dma_chan_is_m2p(struct dma_chan *chan)
{
	return !strcmp(dev_name(chan->device->dev), "ep93xx-dma-m2p");
}

/**
 * ep93xx_dma_chan_direction - returns direction the channel can be used
 * @chan: channel
 *
 * This function can be used in filter functions to find out whether the
 * channel supports given DMA direction. Only M2P channels have such
 * limitation, for M2M channels the direction is configurable.
 */
static inline enum dma_data_direction
ep93xx_dma_chan_direction(struct dma_chan *chan)
{
	if (!ep93xx_dma_chan_is_m2p(chan))
		return DMA_NONE;

	/* even channels are for TX, odd for RX */
	return (chan->chan_id % 2 == 0) ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
}

#endif /* __ASM_ARCH_DMA_H */
