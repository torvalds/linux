#ifndef BCM63XX_DEV_SPI_H
#define BCM63XX_DEV_SPI_H

#include <linux/types.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>

int __init bcm63xx_spi_register(void);

struct bcm63xx_spi_pdata {
	unsigned int	fifo_size;
	unsigned int	msg_type_shift;
	unsigned int	msg_ctl_width;
	int		bus_num;
	int		num_chipselect;
};

enum bcm63xx_regs_spi {
	SPI_CMD,
	SPI_INT_STATUS,
	SPI_INT_MASK_ST,
	SPI_INT_MASK,
	SPI_ST,
	SPI_CLK_CFG,
	SPI_FILL_BYTE,
	SPI_MSG_TAIL,
	SPI_RX_TAIL,
	SPI_MSG_CTL,
	SPI_MSG_DATA,
	SPI_RX_DATA,
};

#define __GEN_SPI_REGS_TABLE(__cpu)					\
	[SPI_CMD]		= SPI_## __cpu ##_CMD,			\
	[SPI_INT_STATUS]	= SPI_## __cpu ##_INT_STATUS,		\
	[SPI_INT_MASK_ST]	= SPI_## __cpu ##_INT_MASK_ST,		\
	[SPI_INT_MASK]		= SPI_## __cpu ##_INT_MASK,		\
	[SPI_ST]		= SPI_## __cpu ##_ST,			\
	[SPI_CLK_CFG]		= SPI_## __cpu ##_CLK_CFG,		\
	[SPI_FILL_BYTE]		= SPI_## __cpu ##_FILL_BYTE,		\
	[SPI_MSG_TAIL]		= SPI_## __cpu ##_MSG_TAIL,		\
	[SPI_RX_TAIL]		= SPI_## __cpu ##_RX_TAIL,		\
	[SPI_MSG_CTL]		= SPI_## __cpu ##_MSG_CTL,		\
	[SPI_MSG_DATA]		= SPI_## __cpu ##_MSG_DATA,		\
	[SPI_RX_DATA]		= SPI_## __cpu ##_RX_DATA,

static inline unsigned long bcm63xx_spireg(enum bcm63xx_regs_spi reg)
{
	extern const unsigned long *bcm63xx_regs_spi;

	return bcm63xx_regs_spi[reg];
}

#endif /* BCM63XX_DEV_SPI_H */
