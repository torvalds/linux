#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

#include <linux/list.h>
#include <linux/types.h>

struct ep93xx_dma_buffer {
	struct list_head	list;
	u32			bus_addr;
	u16			size;
};

struct ep93xx_dma_m2p_client {
	char			*name;
	u8			flags;
	void			*cookie;
	void			(*buffer_started)(void *cookie,
					struct ep93xx_dma_buffer *buf);
	void			(*buffer_finished)(void *cookie,
					struct ep93xx_dma_buffer *buf,
					int bytes, int error);

	/* Internal to the DMA code.  */
	void			*channel;
};

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
#define EP93XX_DMA_M2P_TX		0x00
#define EP93XX_DMA_M2P_RX		0x10
#define EP93XX_DMA_M2P_ABORT_ON_ERROR	0x20
#define EP93XX_DMA_M2P_IGNORE_ERROR	0x40
#define EP93XX_DMA_M2P_ERROR_MASK	0x60

int  ep93xx_dma_m2p_client_register(struct ep93xx_dma_m2p_client *m2p);
void ep93xx_dma_m2p_client_unregister(struct ep93xx_dma_m2p_client *m2p);
void ep93xx_dma_m2p_submit(struct ep93xx_dma_m2p_client *m2p,
			   struct ep93xx_dma_buffer *buf);
void ep93xx_dma_m2p_submit_recursive(struct ep93xx_dma_m2p_client *m2p,
				     struct ep93xx_dma_buffer *buf);
void ep93xx_dma_m2p_flush(struct ep93xx_dma_m2p_client *m2p);

#endif /* __ASM_ARCH_DMA_H */
