/*
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * huangxin <huangxin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __ASM_ARCH_DMA_H__
#define __ASM_ARCH_DMA_H__

#include <linux/device.h>
#include <mach/hardware.h>
/* sun[45]i depending defines*/
#include <plat/dma_defs.h>
#define MAX_DMA_TRANSFER_SIZE   0x100000 /* Data Unit is half word  */

#define DMA_CH_VALID		(1<<31)
#define DMA_CH_NEVER		(1<<30)

#define N_DRQSRC_SHIFT		0
#define N_DRQDST_SHIFT		16
#define D_DRQSRC_SHIFT		0
#define D_DRQDST_SHIFT		16
#define DRQ_INVALID			0xff


#define X_SIGLE   0
#define X_BURST   1
#define X_TIPPL	  2
#define X_BYTE    0
#define X_HALF    1
#define X_WORD    2

/* DMAADDRT_(dist)_(increase/fix)_(src)_(increase/fix) */
#define A_INC     0x0
#define A_FIX     0x1
#define A_LN      0x0
#define A_IO      0x1
#define A_PH      0x2
#define A_PV      0x3


/* use this to specifiy hardware channel number */
#define DMACH_LOW_LEVEL	(1<<28)

/* we have 16 dma channels */
#define SW_DMA_CHANNELS		(16)





/*data length and burst length combination in DDMA and NDMA */
enum xferunit {
	/*des:X_SIGLE  src:X_SIGLE*/
	DMAXFER_D_SBYTE_S_SBYTE,
	DMAXFER_D_SBYTE_S_SHALF,
	DMAXFER_D_SBYTE_S_SWORD,
	DMAXFER_D_SHALF_S_SBYTE,
	DMAXFER_D_SHALF_S_SHALF,
	DMAXFER_D_SHALF_S_SWORD,
	DMAXFER_D_SWORD_S_SBYTE,
	DMAXFER_D_SWORD_S_SHALF,
	DMAXFER_D_SWORD_S_SWORD,

	/*des:X_SIGLE  src:X_BURST*/
	DMAXFER_D_SBYTE_S_BBYTE,
	DMAXFER_D_SBYTE_S_BHALF,
	DMAXFER_D_SBYTE_S_BWORD,
	DMAXFER_D_SHALF_S_BBYTE,
	DMAXFER_D_SHALF_S_BHALF,
	DMAXFER_D_SHALF_S_BWORD,
	DMAXFER_D_SWORD_S_BBYTE,
	DMAXFER_D_SWORD_S_BHALF,
	DMAXFER_D_SWORD_S_BWORD,

	/*des:X_SIGLE   src:X_TIPPL*/
	DMAXFER_D_SBYTE_S_TBYTE,
	DMAXFER_D_SBYTE_S_THALF,
	DMAXFER_D_SBYTE_S_TWORD,
	DMAXFER_D_SHALF_S_TBYTE,
	DMAXFER_D_SHALF_S_THALF,
	DMAXFER_D_SHALF_S_TWORD,
	DMAXFER_D_SWORD_S_TBYTE,
	DMAXFER_D_SWORD_S_THALF,
	DMAXFER_D_SWORD_S_TWORD,

	/*des:X_BURST  src:X_BURST*/
	DMAXFER_D_BBYTE_S_BBYTE,
	DMAXFER_D_BBYTE_S_BHALF,
	DMAXFER_D_BBYTE_S_BWORD,
	DMAXFER_D_BHALF_S_BBYTE,
	DMAXFER_D_BHALF_S_BHALF,
	DMAXFER_D_BHALF_S_BWORD,
	DMAXFER_D_BWORD_S_BBYTE,
	DMAXFER_D_BWORD_S_BHALF,
	DMAXFER_D_BWORD_S_BWORD,

	/*des:X_BURST   src:X_SIGLE*/
	DMAXFER_D_BBYTE_S_SBYTE,
	DMAXFER_D_BBYTE_S_SHALF,
	DMAXFER_D_BBYTE_S_SWORD,
	DMAXFER_D_BHALF_S_SBYTE,
	DMAXFER_D_BHALF_S_SHALF,
	DMAXFER_D_BHALF_S_SWORD,
	DMAXFER_D_BWORD_S_SBYTE,
	DMAXFER_D_BWORD_S_SHALF,
	DMAXFER_D_BWORD_S_SWORD,

	/*des:X_BURST   src:X_TIPPL*/
	DMAXFER_D_BBYTE_S_TBYTE,
	DMAXFER_D_BBYTE_S_THALF,
	DMAXFER_D_BBYTE_S_TWORD,
	DMAXFER_D_BHALF_S_TBYTE,
	DMAXFER_D_BHALF_S_THALF,
	DMAXFER_D_BHALF_S_TWORD,
	DMAXFER_D_BWORD_S_TBYTE,
	DMAXFER_D_BWORD_S_THALF,
	DMAXFER_D_BWORD_S_TWORD,

	/*des:X_TIPPL   src:X_TIPPL*/
	DMAXFER_D_TBYTE_S_TBYTE,
	DMAXFER_D_TBYTE_S_THALF,
	DMAXFER_D_TBYTE_S_TWORD,
	DMAXFER_D_THALF_S_TBYTE,
	DMAXFER_D_THALF_S_THALF,
	DMAXFER_D_THALF_S_TWORD,
	DMAXFER_D_TWORD_S_TBYTE,
	DMAXFER_D_TWORD_S_THALF,
	DMAXFER_D_TWORD_S_TWORD,

	/*des:X_TIPPL   src:X_SIGLE*/
	DMAXFER_D_TBYTE_S_SBYTE,
	DMAXFER_D_TBYTE_S_SHALF,
	DMAXFER_D_TBYTE_S_SWORD,
	DMAXFER_D_THALF_S_SBYTE,
	DMAXFER_D_THALF_S_SHALF,
	DMAXFER_D_THALF_S_SWORD,
	DMAXFER_D_TWORD_S_SBYTE,
	DMAXFER_D_TWORD_S_SHALF,
	DMAXFER_D_TWORD_S_SWORD,

	/*des:X_TIPPL   src:X_BURST*/
	DMAXFER_D_TBYTE_S_BBYTE,
	DMAXFER_D_TBYTE_S_BHALF,
	DMAXFER_D_TBYTE_S_BWORD,
	DMAXFER_D_THALF_S_BBYTE,
	DMAXFER_D_THALF_S_BHALF,
	DMAXFER_D_THALF_S_BWORD,
	DMAXFER_D_TWORD_S_BBYTE,
	DMAXFER_D_TWORD_S_BHALF,
	DMAXFER_D_TWORD_S_BWORD,
	DMAXFER_MAX
};


enum addrt {
	/*NDMA address type*/
	DMAADDRT_D_INC_S_INC,
	DMAADDRT_D_INC_S_FIX,
	DMAADDRT_D_FIX_S_INC,
	DMAADDRT_D_FIX_S_FIX,

	/*DDMA address type*/
	DMAADDRT_D_LN_S_LN,
	DMAADDRT_D_LN_S_IO,
	DMAADDRT_D_LN_S_PH,
	DMAADDRT_D_LN_S_PV,

	DMAADDRT_D_IO_S_LN,
	DMAADDRT_D_IO_S_IO,
	DMAADDRT_D_IO_S_PH,
	DMAADDRT_D_IO_S_PV,

	DMAADDRT_D_PH_S_LN,
	DMAADDRT_D_PH_S_IO,
	DMAADDRT_D_PH_S_PH,
	DMAADDRT_D_PH_S_PV,

	DMAADDRT_D_PV_S_LN,
	DMAADDRT_D_PV_S_IO,
	DMAADDRT_D_PV_S_PH,
	DMAADDRT_D_PV_S_PV,

	DMAADDRT_MAX
};

/* types */
enum sw_dma_state {
	SW_DMA_IDLE,
	SW_DMA_RUNNING,
	SW_DMA_PAUSED
};


/* enum sw_dma_loadst
 *
 * This represents the state of the DMA engine, wrt to the loaded / running
 * transfers. Since we don't have any way of knowing exactly the state of
 * the DMA transfers, we need to know the state to make decisions on wether
 * we can
 *
 * SW_DMA_NONE
 *
 * There are no buffers loaded (the channel should be inactive)
 *
 * SW_DMA_1LOADED
 *
 * There is one buffer loaded, however it has not been confirmed to be
 * loaded by the DMA engine. This may be because the channel is not
 * yet running, or the DMA driver decided that it was too costly to
 * sit and wait for it to happen.
 *
 * SW_DMA_1RUNNING
 *
 * The buffer has been confirmed running, and not finisged
 *
 * SW_DMA_1LOADED_1RUNNING
 *
 * There is a buffer waiting to be loaded by the DMA engine, and one
 * currently running.
*/

enum sw_dma_loadst {
	SW_DMALOAD_NONE,
	SW_DMALOAD_1LOADED,
	SW_DMALOAD_1RUNNING,
	SW_DMALOAD_1LOADED_1RUNNING,
};

enum sw_dma_buffresult {
	SW_RES_OK,
	SW_RES_ERR,
	SW_RES_ABORT
};

enum sw_dmadir {
	SW_DMA_RWNULL,
	SW_DMA_RDEV,		/* read from dev */
	SW_DMA_WDEV,		/* write to dev */
	SW_DMA_M2M,
//	SW_DMA_RWDEV		/* can r/w dev */
};

enum dma_hf_irq {
	SW_DMA_IRQ_NO,
	SW_DMA_IRQ_HALF,
	SW_DMA_IRQ_FULL
};
/* enum sw_chan_op
 *
 * operation codes passed to the DMA code by the user, and also used
 * to inform the current channel owner of any changes to the system state
*/

enum sw_chan_op {
	SW_DMAOP_START,
	SW_DMAOP_STOP,
	SW_DMAOP_PAUSE,
	SW_DMAOP_RESUME,
	SW_DMAOP_FLUSH,
	SW_DMAOP_TIMEOUT,		/* internal signal to handler */
	SW_DMAOP_STARTED,		/* indicate channel started */
};

/* flags */

#define SW_DMAF_SLOW         (1<<0)   /* slow, so don't worry about
					    * waiting for reloads */
#define SW_DMAF_AUTOSTART    (1<<1)   /* auto-start if buffer queued */

/* dma buffer */

struct sw_dma_client {
	char                *name;
};

/* sw_dma_buf_s
 *
 * internally used buffer structure to describe a queued or running
 * buffer.
*/

struct sw_dma_buf;
struct sw_dma_buf {
	struct sw_dma_buf	*next;
	int			 magic;		/* magic */
	int			 size;		/* buffer size in bytes */
	dma_addr_t		 data;		/* start of DMA data */
	dma_addr_t		 ptr;		/* where the DMA got to [1] */
	void			*id;		/* client's id */
};

/* [1] is this updated for both recv/send modes? */

struct sw_dma_chan;

/* sw_dma_cbfn_t
 *
 * buffer callback routine type
*/

typedef void (*sw_dma_cbfn_t)(struct sw_dma_chan *,
				   void *buf, int size,
				   enum sw_dma_buffresult result);

typedef int  (*sw_dma_opfn_t)(struct sw_dma_chan *,
				   enum sw_chan_op );

struct sw_dma_stats {
	unsigned long		loads;
	unsigned long		timeout_longest;
	unsigned long		timeout_shortest;
	unsigned long		timeout_avg;
	unsigned long		timeout_failed;
};

struct sw_dma_map;

/* struct sw_dma_chan
 *
 * full state information for each DMA channel
*/

struct sw_dma_chan {
	/* channel state flags and information */
	unsigned char		 number;      /* number of this dma channel */
	unsigned char		 in_use;      /* channel allocated */
	unsigned char		 irq_claimed; /* irq claimed for channel */
	unsigned char		 irq_enabled; /* irq enabled for channel */

	/* channel state */

	enum sw_dma_state	 state;
	enum sw_dma_loadst	 load_state;
	struct sw_dma_client *client;

	/* channel configuration */
	unsigned long		 dev_addr;
	unsigned long		 load_timeout;
	unsigned int		 flags;		/* channel flags */
	unsigned int		 hw_cfg;	/* last hw config */

	struct sw_dma_map	*map;		/* channel hw maps */

	/* channel's hardware position and configuration */
	void __iomem		*regs;		/* channels registers */
	void __iomem		*addr_reg;	/* data address register */
	//unsigned int		 irq;		/* channel irq */
	unsigned long		 dcon;		/* default value of DCON */

	/* driver handles */
	sw_dma_cbfn_t	 callback_fn;	/* buffer done callback */
	sw_dma_cbfn_t	 callback_hd;	/* buffer half done callback */
	sw_dma_opfn_t	 op_fn;		/* channel op callback */

	/* stats gathering */
	struct sw_dma_stats *stats;
	struct sw_dma_stats  stats_store;

	/* buffer list and information */
	struct sw_dma_buf	*curr;		/* current dma buffer */
	struct sw_dma_buf	*next;		/* next buffer to load */
	struct sw_dma_buf	*end;		/* end of queue */

	/* system device */
	struct device	dev;
	void * dev_id;
};

/*the channel number of above 8 is DDMA channel.*/
#define IS_DADECATE_DMA(ch) (ch->number >= 8)

struct dma_hw_conf{
	unsigned char		drqsrc_type;
	unsigned char		drqdst_type;

	unsigned char		xfer_type;
	unsigned char		address_type;
	unsigned char           dir;
	unsigned char		hf_irq;
	unsigned char		reload;

	unsigned long		from;
	unsigned long		to;
	unsigned long		cmbk;
};

extern inline void DMA_COPY_HW_CONF(struct dma_hw_conf *to, struct dma_hw_conf *from);

/* struct sw_dma_map
 *
 * this holds the mapping information for the channel selected
 * to be connected to the specified device
*/
struct sw_dma_map {
	const char		*name;
	struct dma_hw_conf  user_hw_conf;
	const struct dma_hw_conf*  default_hw_conf;
	struct dma_hw_conf* conf_ptr;
	unsigned long channels[SW_DMA_CHANNELS];
};

struct sw_dma_selection {
	struct sw_dma_map	*map;
	unsigned long		 map_size;
	unsigned long		 dcon_mask;
};

/* struct sw_dma_order_ch
 *
 * channel map for one of the `enum dma_ch` dma channels. the list
 * entry contains a set of low-level channel numbers, orred with
 * DMA_CH_VALID, which are checked in the order in the array.
*/

struct sw_dma_order_ch {
	unsigned int	list[SW_DMA_CHANNELS];	/* list of channels */
	unsigned int	flags;				/* flags */
};

/* struct s3c24xx_dma_order
 *
 * information provided by either the core or the board to give the
 * dma system a hint on how to allocate channels
*/

struct sw_dma_order {
	struct sw_dma_order_ch	channels[DMACH_MAX];
};

/* the currently allocated channel information */
extern struct sw_dma_chan sw_chans[];

/* note, we don't really use dma_device_t at the moment */
typedef unsigned long dma_device_t;

/* functions --------------------------------------------------------------- */

/* sw_dma_request
 *
 * request a dma channel exclusivley
*/

extern int sw_dma_request(unsigned int channel,
			       struct sw_dma_client *, void *dev);


/* sw_dma_ctrl
 *
 * change the state of the dma channel
*/

extern int sw_dma_ctrl(unsigned int channel, enum sw_chan_op op);

/* sw_dma_setflags
 *
 * set the channel's flags to a given state
*/

extern int sw_dma_setflags(unsigned int channel,
				unsigned int flags);

/* sw_dma_free
 *
 * free the dma channel (will also abort any outstanding operations)
*/

extern int sw_dma_free(unsigned int channel, struct sw_dma_client *);

/* sw_dma_enqueue
 *
 * place the given buffer onto the queue of operations for the channel.
 * The buffer must be allocated from dma coherent memory, or the Dcache/WB
 * drained before the buffer is given to the DMA system.
*/

extern int sw_dma_enqueue(unsigned int channel, void *id,
			       dma_addr_t data, int size);

/* sw_dma_config
 *
 * configure the dma channel
*/
extern void poll_dma_pending(int chan_nr);

extern int sw_dma_config(unsigned int channel, struct dma_hw_conf* user_conf);

extern int sw15_dma_init(void);

extern int sw_dma_order_set(struct sw_dma_order *ord);

extern int sw_dma_init_map(struct sw_dma_selection *sel);

/* sw_dma_getposition
 *
 * get the position that the dma transfer is currently at
*/

extern int sw_dma_getposition(unsigned int channel,
				   dma_addr_t *src, dma_addr_t *dest);

extern int sw_dma_set_opfn(unsigned int, sw_dma_opfn_t rtn);
extern int sw_dma_set_buffdone_fn(unsigned int, sw_dma_cbfn_t rtn);
extern int sw_dma_set_halfdone_fn(unsigned int, sw_dma_cbfn_t rtn);
extern int sw_dma_getcurposition(unsigned int channel,
				   dma_addr_t *src, dma_addr_t *dest);

#endif /* __ASM_ARCH_DMA_H */
