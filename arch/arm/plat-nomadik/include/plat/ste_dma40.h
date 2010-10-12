/*
 * arch/arm/plat-nomadik/include/plat/ste_dma40.h
 *
 * Copyright (C) ST-Ericsson 2007-2010
 * License terms: GNU General Public License (GPL) version 2
 * Author: Per Friden <per.friden@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 */


#ifndef STE_DMA40_H
#define STE_DMA40_H

#include <linux/dmaengine.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>

/* dev types for memcpy */
#define STEDMA40_DEV_DST_MEMORY (-1)
#define	STEDMA40_DEV_SRC_MEMORY (-1)

/*
 * Description of bitfields of channel_type variable is available in
 * the info structure.
 */

/* Priority */
#define STEDMA40_INFO_PRIO_TYPE_POS 2
#define STEDMA40_HIGH_PRIORITY_CHANNEL (0x1 << STEDMA40_INFO_PRIO_TYPE_POS)
#define STEDMA40_LOW_PRIORITY_CHANNEL (0x2 << STEDMA40_INFO_PRIO_TYPE_POS)

/* Mode  */
#define STEDMA40_INFO_CH_MODE_TYPE_POS 6
#define STEDMA40_CHANNEL_IN_PHY_MODE (0x1 << STEDMA40_INFO_CH_MODE_TYPE_POS)
#define STEDMA40_CHANNEL_IN_LOG_MODE (0x2 << STEDMA40_INFO_CH_MODE_TYPE_POS)
#define STEDMA40_CHANNEL_IN_OPER_MODE (0x3 << STEDMA40_INFO_CH_MODE_TYPE_POS)

/* Mode options */
#define STEDMA40_INFO_CH_MODE_OPT_POS 8
#define STEDMA40_PCHAN_BASIC_MODE (0x1 << STEDMA40_INFO_CH_MODE_OPT_POS)
#define STEDMA40_PCHAN_MODULO_MODE (0x2 << STEDMA40_INFO_CH_MODE_OPT_POS)
#define STEDMA40_PCHAN_DOUBLE_DST_MODE (0x3 << STEDMA40_INFO_CH_MODE_OPT_POS)
#define STEDMA40_LCHAN_SRC_PHY_DST_LOG (0x1 << STEDMA40_INFO_CH_MODE_OPT_POS)
#define STEDMA40_LCHAN_SRC_LOG_DST_PHS (0x2 << STEDMA40_INFO_CH_MODE_OPT_POS)
#define STEDMA40_LCHAN_SRC_LOG_DST_LOG (0x3 << STEDMA40_INFO_CH_MODE_OPT_POS)

/* Interrupt */
#define STEDMA40_INFO_TIM_POS 10
#define STEDMA40_NO_TIM_FOR_LINK (0x0 << STEDMA40_INFO_TIM_POS)
#define STEDMA40_TIM_FOR_LINK (0x1 << STEDMA40_INFO_TIM_POS)

/* End of channel_type configuration */

#define STEDMA40_ESIZE_8_BIT  0x0
#define STEDMA40_ESIZE_16_BIT 0x1
#define STEDMA40_ESIZE_32_BIT 0x2
#define STEDMA40_ESIZE_64_BIT 0x3

/* The value 4 indicates that PEN-reg shall be set to 0 */
#define STEDMA40_PSIZE_PHY_1  0x4
#define STEDMA40_PSIZE_PHY_2  0x0
#define STEDMA40_PSIZE_PHY_4  0x1
#define STEDMA40_PSIZE_PHY_8  0x2
#define STEDMA40_PSIZE_PHY_16 0x3

/*
 * The number of elements differ in logical and
 * physical mode
 */
#define STEDMA40_PSIZE_LOG_1  STEDMA40_PSIZE_PHY_2
#define STEDMA40_PSIZE_LOG_4  STEDMA40_PSIZE_PHY_4
#define STEDMA40_PSIZE_LOG_8  STEDMA40_PSIZE_PHY_8
#define STEDMA40_PSIZE_LOG_16 STEDMA40_PSIZE_PHY_16

enum stedma40_flow_ctrl {
	STEDMA40_NO_FLOW_CTRL,
	STEDMA40_FLOW_CTRL,
};

enum stedma40_endianess {
	STEDMA40_LITTLE_ENDIAN,
	STEDMA40_BIG_ENDIAN
};

enum stedma40_periph_data_width {
	STEDMA40_BYTE_WIDTH = STEDMA40_ESIZE_8_BIT,
	STEDMA40_HALFWORD_WIDTH = STEDMA40_ESIZE_16_BIT,
	STEDMA40_WORD_WIDTH = STEDMA40_ESIZE_32_BIT,
	STEDMA40_DOUBLEWORD_WIDTH = STEDMA40_ESIZE_64_BIT
};

struct stedma40_half_channel_info {
	enum stedma40_endianess endianess;
	enum stedma40_periph_data_width data_width;
	int psize;
	enum stedma40_flow_ctrl flow_ctrl;
};

enum stedma40_xfer_dir {
	STEDMA40_MEM_TO_MEM,
	STEDMA40_MEM_TO_PERIPH,
	STEDMA40_PERIPH_TO_MEM,
	STEDMA40_PERIPH_TO_PERIPH
};


/**
 * struct stedma40_chan_cfg - Structure to be filled by client drivers.
 *
 * @dir: MEM 2 MEM, PERIPH 2 MEM , MEM 2 PERIPH, PERIPH 2 PERIPH
 * @channel_type: priority, mode, mode options and interrupt configuration.
 * @src_dev_type: Src device type
 * @dst_dev_type: Dst device type
 * @src_info: Parameters for dst half channel
 * @dst_info: Parameters for dst half channel
 * @pre_transfer_data: Data to be passed on to the pre_transfer() function.
 * @pre_transfer: Callback used if needed before preparation of transfer.
 * Only called if device is set. size of bytes to transfer
 * (in case of multiple element transfer size is size of the first element).
 *
 *
 * This structure has to be filled by the client drivers.
 * It is recommended to do all dma configurations for clients in the machine.
 *
 */
struct stedma40_chan_cfg {
	enum stedma40_xfer_dir			 dir;
	unsigned int				 channel_type;
	int					 src_dev_type;
	int					 dst_dev_type;
	struct stedma40_half_channel_info	 src_info;
	struct stedma40_half_channel_info	 dst_info;
	void					*pre_transfer_data;
	int (*pre_transfer)			(struct dma_chan *chan,
						 void *data,
						 int size);
};

/**
 * struct stedma40_platform_data - Configuration struct for the dma device.
 *
 * @dev_len: length of dev_tx and dev_rx
 * @dev_tx: mapping between destination event line and io address
 * @dev_rx: mapping between source event line and io address
 * @memcpy: list of memcpy event lines
 * @memcpy_len: length of memcpy
 * @memcpy_conf_phy: default configuration of physical channel memcpy
 * @memcpy_conf_log: default configuration of logical channel memcpy
 * @llis_per_log: number of max linked list items per logical channel
 * @disabled_channels: A vector, ending with -1, that marks physical channels
 * that are for different reasons not available for the driver.
 */
struct stedma40_platform_data {
	u32				 dev_len;
	const dma_addr_t		*dev_tx;
	const dma_addr_t		*dev_rx;
	int				*memcpy;
	u32				 memcpy_len;
	struct stedma40_chan_cfg	*memcpy_conf_phy;
	struct stedma40_chan_cfg	*memcpy_conf_log;
	unsigned int			 llis_per_log;
	int				 disabled_channels[8];
};

/**
 * setdma40_set_psize() - Used for changing the package size of an
 * already configured dma channel.
 *
 * @chan: dmaengine handle
 * @src_psize: new package side for src. (STEDMA40_PSIZE*)
 * @src_psize: new package side for dst. (STEDMA40_PSIZE*)
 *
 * returns 0 on ok, otherwise negative error number.
 */
int stedma40_set_psize(struct dma_chan *chan,
		       int src_psize,
		       int dst_psize);

/**
 * stedma40_filter() - Provides stedma40_chan_cfg to the
 * ste_dma40 dma driver via the dmaengine framework.
 * does some checking of what's provided.
 *
 * Never directly called by client. It used by dmaengine.
 * @chan: dmaengine handle.
 * @data: Must be of type: struct stedma40_chan_cfg and is
 * the configuration of the framework.
 *
 *
 */

bool stedma40_filter(struct dma_chan *chan, void *data);

/**
 * stedma40_memcpy_sg() - extension of the dma framework, memcpy to/from
 * scattergatter lists.
 *
 * @chan: dmaengine handle
 * @sgl_dst: Destination scatter list
 * @sgl_src: Source scatter list
 * @sgl_len: The length of each scatterlist. Both lists must be of equal length
 * and each element must match the corresponding element in the other scatter
 * list.
 * @flags: is actually enum dma_ctrl_flags. See dmaengine.h
 */

struct dma_async_tx_descriptor *stedma40_memcpy_sg(struct dma_chan *chan,
						   struct scatterlist *sgl_dst,
						   struct scatterlist *sgl_src,
						   unsigned int sgl_len,
						   unsigned long flags);

/**
 * stedma40_slave_mem() - Transfers a raw data buffer to or from a slave
 * (=device)
 *
 * @chan: dmaengine handle
 * @addr: source or destination physicall address.
 * @size: bytes to transfer
 * @direction: direction of transfer
 * @flags: is actually enum dma_ctrl_flags. See dmaengine.h
 */

static inline struct
dma_async_tx_descriptor *stedma40_slave_mem(struct dma_chan *chan,
					    dma_addr_t addr,
					    unsigned int size,
					    enum dma_data_direction direction,
					    unsigned long flags)
{
	struct scatterlist sg;
	sg_init_table(&sg, 1);
	sg.dma_address = addr;
	sg.length = size;

	return chan->device->device_prep_slave_sg(chan, &sg, 1,
						  direction, flags);
}

#endif
