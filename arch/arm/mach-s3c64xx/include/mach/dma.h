/* linux/arch/arm/mach-s3c6400/include/mach/dma.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C6400 - DMA support
 */

#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H __FILE__

#ifdef CONFIG_S3C64XX_DMA

#define S3C_DMA_CHANNELS	(16)

/* see mach-s3c2410/dma.h for notes on dma channel numbers */

/* Note, for the S3C64XX architecture we keep the DMACH_
 * defines in the order they are allocated to [S]DMA0/[S]DMA1
 * so that is easy to do DHACH_ -> DMA controller conversion
 */
enum dma_ch {
	/* DMA0/SDMA0 */
	DMACH_UART0 = 0,
	DMACH_UART0_SRC2,
	DMACH_UART1,
	DMACH_UART1_SRC2,
	DMACH_UART2,
	DMACH_UART2_SRC2,
	DMACH_UART3,
	DMACH_UART3_SRC2,
	DMACH_PCM0_TX,
	DMACH_PCM0_RX,
	DMACH_I2S0_OUT,
	DMACH_I2S0_IN,
	DMACH_SPI0_TX,
	DMACH_SPI0_RX,
	DMACH_HSI_I2SV40_TX,
	DMACH_HSI_I2SV40_RX,

	/* DMA1/SDMA1 */
	DMACH_PCM1_TX = 16,
	DMACH_PCM1_RX,
	DMACH_I2S1_OUT,
	DMACH_I2S1_IN,
	DMACH_SPI1_TX,
	DMACH_SPI1_RX,
	DMACH_AC97_PCMOUT,
	DMACH_AC97_PCMIN,
	DMACH_AC97_MICIN,
	DMACH_PWM,
	DMACH_IRDA,
	DMACH_EXTERNAL,
	DMACH_RES1,
	DMACH_RES2,
	DMACH_SECURITY_RX,	/* SDMA1 only */
	DMACH_SECURITY_TX,	/* SDMA1 only */
	DMACH_MAX		/* the end */
};

static inline bool samsung_dma_has_circular(void)
{
	return true;
}

static inline bool samsung_dma_is_dmadev(void)
{
	return false;
}
#define S3C2410_DMAF_CIRCULAR		(1 << 0)

#include <plat/dma.h>

#define DMACH_LOW_LEVEL (1<<28) /* use this to specifiy hardware ch no */

struct s3c64xx_dma_buff;

/** s3c64xx_dma_buff - S3C64XX DMA buffer descriptor
 * @next: Pointer to next buffer in queue or ring.
 * @pw: Client provided identifier
 * @lli: Pointer to hardware descriptor this buffer is associated with.
 * @lli_dma: Hardare address of the descriptor.
 */
struct s3c64xx_dma_buff {
	struct s3c64xx_dma_buff *next;

	void			*pw;
	struct pl080s_lli	*lli;
	dma_addr_t		 lli_dma;
};

struct s3c64xx_dmac;

struct s3c2410_dma_chan {
	unsigned char		 number;      /* number of this dma channel */
	unsigned char		 in_use;      /* channel allocated */
	unsigned char		 bit;	      /* bit for enable/disable/etc */
	unsigned char		 hw_width;
	unsigned char		 peripheral;

	unsigned int		 flags;
	enum dma_data_direction	 source;


	dma_addr_t		dev_addr;

	struct s3c2410_dma_client *client;
	struct s3c64xx_dmac	*dmac;		/* pointer to controller */

	void __iomem		*regs;

	/* cdriver callbacks */
	s3c2410_dma_cbfn_t	 callback_fn;	/* buffer done callback */
	s3c2410_dma_opfn_t	 op_fn;		/* channel op callback */

	/* buffer list and information */
	struct s3c64xx_dma_buff	*curr;		/* current dma buffer */
	struct s3c64xx_dma_buff	*next;		/* next buffer to load */
	struct s3c64xx_dma_buff	*end;		/* end of queue */

	/* note, when channel is running in circular mode, curr is the
	 * first buffer enqueued, end is the last and curr is where the
	 * last buffer-done event is set-at. The buffers are not freed
	 * and the last buffer hardware descriptor points back to the
	 * first.
	 */
};

#include <plat/dma-core.h>

#else

#define S3C64XX_DMA_CHAN(name)		((unsigned long)(name))

/* DMA0/SDMA0 */
#define DMACH_UART0		S3C64XX_DMA_CHAN("uart0_tx")
#define DMACH_UART0_SRC2	S3C64XX_DMA_CHAN("uart0_rx")
#define DMACH_UART1		S3C64XX_DMA_CHAN("uart1_tx")
#define DMACH_UART1_SRC2	S3C64XX_DMA_CHAN("uart1_rx")
#define DMACH_UART2		S3C64XX_DMA_CHAN("uart2_tx")
#define DMACH_UART2_SRC2	S3C64XX_DMA_CHAN("uart2_rx")
#define DMACH_UART3		S3C64XX_DMA_CHAN("uart3_tx")
#define DMACH_UART3_SRC2	S3C64XX_DMA_CHAN("uart3_rx")
#define DMACH_PCM0_TX		S3C64XX_DMA_CHAN("pcm0_tx")
#define DMACH_PCM0_RX		S3C64XX_DMA_CHAN("pcm0_rx")
#define DMACH_I2S0_OUT		S3C64XX_DMA_CHAN("i2s0_tx")
#define DMACH_I2S0_IN		S3C64XX_DMA_CHAN("i2s0_rx")
#define DMACH_SPI0_TX		S3C64XX_DMA_CHAN("spi0_tx")
#define DMACH_SPI0_RX		S3C64XX_DMA_CHAN("spi0_rx")
#define DMACH_HSI_I2SV40_TX	S3C64XX_DMA_CHAN("i2s2_tx")
#define DMACH_HSI_I2SV40_RX	S3C64XX_DMA_CHAN("i2s2_rx")

/* DMA1/SDMA1 */
#define DMACH_PCM1_TX		S3C64XX_DMA_CHAN("pcm1_tx")
#define DMACH_PCM1_RX		S3C64XX_DMA_CHAN("pcm1_rx")
#define DMACH_I2S1_OUT		S3C64XX_DMA_CHAN("i2s1_tx")
#define DMACH_I2S1_IN		S3C64XX_DMA_CHAN("i2s1_rx")
#define DMACH_SPI1_TX		S3C64XX_DMA_CHAN("spi1_tx")
#define DMACH_SPI1_RX		S3C64XX_DMA_CHAN("spi1_rx")
#define DMACH_AC97_PCMOUT	S3C64XX_DMA_CHAN("ac97_out")
#define DMACH_AC97_PCMIN	S3C64XX_DMA_CHAN("ac97_in")
#define DMACH_AC97_MICIN	S3C64XX_DMA_CHAN("ac97_mic")
#define DMACH_PWM		S3C64XX_DMA_CHAN("pwm")
#define DMACH_IRDA		S3C64XX_DMA_CHAN("irda")
#define DMACH_EXTERNAL		S3C64XX_DMA_CHAN("external")
#define DMACH_SECURITY_RX	S3C64XX_DMA_CHAN("sec_rx")
#define DMACH_SECURITY_TX	S3C64XX_DMA_CHAN("sec_tx")

enum dma_ch {
	DMACH_MAX = 32
};

struct s3c2410_dma_client {
	char	*name;
};

static inline bool samsung_dma_has_circular(void)
{
	return true;
}

static inline bool samsung_dma_is_dmadev(void)
{
	return true;
}

#include <linux/amba/pl08x.h>
#include <plat/dma-ops.h>

#endif

#endif /* __ASM_ARCH_IRQ_H */
