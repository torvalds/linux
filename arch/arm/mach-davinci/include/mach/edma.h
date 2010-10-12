/*
 *  TI DAVINCI dma definitions
 *
 *  Copyright (C) 2006-2009 Texas Instruments.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
 * This EDMA3 programming framework exposes two basic kinds of resource:
 *
 *  Channel	Triggers transfers, usually from a hardware event but
 *		also manually or by "chaining" from DMA completions.
 *		Each channel is coupled to a Parameter RAM (PaRAM) slot.
 *
 *  Slot	Each PaRAM slot holds a DMA transfer descriptor (PaRAM
 *		"set"), source and destination addresses, a link to a
 *		next PaRAM slot (if any), options for the transfer, and
 *		instructions for updating those addresses.  There are
 *		more than twice as many slots as event channels.
 *
 * Each PaRAM set describes a sequence of transfers, either for one large
 * buffer or for several discontiguous smaller buffers.  An EDMA transfer
 * is driven only from a channel, which performs the transfers specified
 * in its PaRAM slot until there are no more transfers.  When that last
 * transfer completes, the "link" field may be used to reload the channel's
 * PaRAM slot with a new transfer descriptor.
 *
 * The EDMA Channel Controller (CC) maps requests from channels into physical
 * Transfer Controller (TC) requests when the channel triggers (by hardware
 * or software events, or by chaining).  The two physical DMA channels provided
 * by the TCs are thus shared by many logical channels.
 *
 * DaVinci hardware also has a "QDMA" mechanism which is not currently
 * supported through this interface.  (DSP firmware uses it though.)
 */

#ifndef EDMA_H_
#define EDMA_H_

/* PaRAM slots are laid out like this */
struct edmacc_param {
	unsigned int opt;
	unsigned int src;
	unsigned int a_b_cnt;
	unsigned int dst;
	unsigned int src_dst_bidx;
	unsigned int link_bcntrld;
	unsigned int src_dst_cidx;
	unsigned int ccnt;
};

#define CCINT0_INTERRUPT     16
#define CCERRINT_INTERRUPT   17
#define TCERRINT0_INTERRUPT   18
#define TCERRINT1_INTERRUPT   19

/* fields in edmacc_param.opt */
#define SAM		BIT(0)
#define DAM		BIT(1)
#define SYNCDIM		BIT(2)
#define STATIC		BIT(3)
#define EDMA_FWID	(0x07 << 8)
#define TCCMODE		BIT(11)
#define EDMA_TCC(t)	((t) << 12)
#define TCINTEN		BIT(20)
#define ITCINTEN	BIT(21)
#define TCCHEN		BIT(22)
#define ITCCHEN		BIT(23)

#define TRWORD (0x7<<2)
#define PAENTRY (0x1ff<<5)

/* Drivers should avoid using these symbolic names for dm644x
 * channels, and use platform_device IORESOURCE_DMA resources
 * instead.  (Other DaVinci chips have different peripherals
 * and thus have different DMA channel mappings.)
 */
#define DAVINCI_DMA_MCBSP_TX              2
#define DAVINCI_DMA_MCBSP_RX              3
#define DAVINCI_DMA_VPSS_HIST             4
#define DAVINCI_DMA_VPSS_H3A              5
#define DAVINCI_DMA_VPSS_PRVU             6
#define DAVINCI_DMA_VPSS_RSZ              7
#define DAVINCI_DMA_IMCOP_IMXINT          8
#define DAVINCI_DMA_IMCOP_VLCDINT         9
#define DAVINCI_DMA_IMCO_PASQINT         10
#define DAVINCI_DMA_IMCOP_DSQINT         11
#define DAVINCI_DMA_SPI_SPIX             16
#define DAVINCI_DMA_SPI_SPIR             17
#define DAVINCI_DMA_UART0_URXEVT0        18
#define DAVINCI_DMA_UART0_UTXEVT0        19
#define DAVINCI_DMA_UART1_URXEVT1        20
#define DAVINCI_DMA_UART1_UTXEVT1        21
#define DAVINCI_DMA_UART2_URXEVT2        22
#define DAVINCI_DMA_UART2_UTXEVT2        23
#define DAVINCI_DMA_MEMSTK_MSEVT         24
#define DAVINCI_DMA_MMCRXEVT             26
#define DAVINCI_DMA_MMCTXEVT             27
#define DAVINCI_DMA_I2C_ICREVT           28
#define DAVINCI_DMA_I2C_ICXEVT           29
#define DAVINCI_DMA_GPIO_GPINT0          32
#define DAVINCI_DMA_GPIO_GPINT1          33
#define DAVINCI_DMA_GPIO_GPINT2          34
#define DAVINCI_DMA_GPIO_GPINT3          35
#define DAVINCI_DMA_GPIO_GPINT4          36
#define DAVINCI_DMA_GPIO_GPINT5          37
#define DAVINCI_DMA_GPIO_GPINT6          38
#define DAVINCI_DMA_GPIO_GPINT7          39
#define DAVINCI_DMA_GPIO_GPBNKINT0       40
#define DAVINCI_DMA_GPIO_GPBNKINT1       41
#define DAVINCI_DMA_GPIO_GPBNKINT2       42
#define DAVINCI_DMA_GPIO_GPBNKINT3       43
#define DAVINCI_DMA_GPIO_GPBNKINT4       44
#define DAVINCI_DMA_TIMER0_TINT0         48
#define DAVINCI_DMA_TIMER1_TINT1         49
#define DAVINCI_DMA_TIMER2_TINT2         50
#define DAVINCI_DMA_TIMER3_TINT3         51
#define DAVINCI_DMA_PWM0                 52
#define DAVINCI_DMA_PWM1                 53
#define DAVINCI_DMA_PWM2                 54

/* DA830 specific EDMA3 information */
#define EDMA_DA830_NUM_DMACH		32
#define EDMA_DA830_NUM_TCC		32
#define EDMA_DA830_NUM_PARAMENTRY	128
#define EDMA_DA830_NUM_EVQUE		2
#define EDMA_DA830_NUM_TC		2
#define EDMA_DA830_CHMAP_EXIST		0
#define EDMA_DA830_NUM_REGIONS		4
#define DA830_DMACH2EVENT_MAP0		0x000FC03Fu
#define DA830_DMACH2EVENT_MAP1		0x00000000u
#define DA830_EDMA_ARM_OWN		0x30FFCCFFu

/* DA830 specific EDMA3 Events Information */
enum DA830_edma_ch {
	DA830_DMACH_MCASP0_RX,
	DA830_DMACH_MCASP0_TX,
	DA830_DMACH_MCASP1_RX,
	DA830_DMACH_MCASP1_TX,
	DA830_DMACH_MCASP2_RX,
	DA830_DMACH_MCASP2_TX,
	DA830_DMACH_GPIO_BNK0INT,
	DA830_DMACH_GPIO_BNK1INT,
	DA830_DMACH_UART0_RX,
	DA830_DMACH_UART0_TX,
	DA830_DMACH_TMR64P0_EVTOUT12,
	DA830_DMACH_TMR64P0_EVTOUT34,
	DA830_DMACH_UART1_RX,
	DA830_DMACH_UART1_TX,
	DA830_DMACH_SPI0_RX,
	DA830_DMACH_SPI0_TX,
	DA830_DMACH_MMCSD_RX,
	DA830_DMACH_MMCSD_TX,
	DA830_DMACH_SPI1_RX,
	DA830_DMACH_SPI1_TX,
	DA830_DMACH_DMAX_EVTOUT6,
	DA830_DMACH_DMAX_EVTOUT7,
	DA830_DMACH_GPIO_BNK2INT,
	DA830_DMACH_GPIO_BNK3INT,
	DA830_DMACH_I2C0_RX,
	DA830_DMACH_I2C0_TX,
	DA830_DMACH_I2C1_RX,
	DA830_DMACH_I2C1_TX,
	DA830_DMACH_GPIO_BNK4INT,
	DA830_DMACH_GPIO_BNK5INT,
	DA830_DMACH_UART2_RX,
	DA830_DMACH_UART2_TX
};

/*ch_status paramater of callback function possible values*/
#define DMA_COMPLETE 1
#define DMA_CC_ERROR 2
#define DMA_TC1_ERROR 3
#define DMA_TC2_ERROR 4

enum address_mode {
	INCR = 0,
	FIFO = 1
};

enum fifo_width {
	W8BIT = 0,
	W16BIT = 1,
	W32BIT = 2,
	W64BIT = 3,
	W128BIT = 4,
	W256BIT = 5
};

enum dma_event_q {
	EVENTQ_0 = 0,
	EVENTQ_1 = 1,
	EVENTQ_2 = 2,
	EVENTQ_3 = 3,
	EVENTQ_DEFAULT = -1
};

enum sync_dimension {
	ASYNC = 0,
	ABSYNC = 1
};

#define EDMA_CTLR_CHAN(ctlr, chan)	(((ctlr) << 16) | (chan))
#define EDMA_CTLR(i)			((i) >> 16)
#define EDMA_CHAN_SLOT(i)		((i) & 0xffff)

#define EDMA_CHANNEL_ANY		-1	/* for edma_alloc_channel() */
#define EDMA_SLOT_ANY			-1	/* for edma_alloc_slot() */
#define EDMA_CONT_PARAMS_ANY		 1001
#define EDMA_CONT_PARAMS_FIXED_EXACT	 1002
#define EDMA_CONT_PARAMS_FIXED_NOT_EXACT 1003

#define EDMA_MAX_CC               2

/* alloc/free DMA channels and their dedicated parameter RAM slots */
int edma_alloc_channel(int channel,
	void (*callback)(unsigned channel, u16 ch_status, void *data),
	void *data, enum dma_event_q);
void edma_free_channel(unsigned channel);

/* alloc/free parameter RAM slots */
int edma_alloc_slot(unsigned ctlr, int slot);
void edma_free_slot(unsigned slot);

/* alloc/free a set of contiguous parameter RAM slots */
int edma_alloc_cont_slots(unsigned ctlr, unsigned int id, int slot, int count);
int edma_free_cont_slots(unsigned slot, int count);

/* calls that operate on part of a parameter RAM slot */
void edma_set_src(unsigned slot, dma_addr_t src_port,
				enum address_mode mode, enum fifo_width);
void edma_set_dest(unsigned slot, dma_addr_t dest_port,
				 enum address_mode mode, enum fifo_width);
void edma_get_position(unsigned slot, dma_addr_t *src, dma_addr_t *dst);
void edma_set_src_index(unsigned slot, s16 src_bidx, s16 src_cidx);
void edma_set_dest_index(unsigned slot, s16 dest_bidx, s16 dest_cidx);
void edma_set_transfer_params(unsigned slot, u16 acnt, u16 bcnt, u16 ccnt,
		u16 bcnt_rld, enum sync_dimension sync_mode);
void edma_link(unsigned from, unsigned to);
void edma_unlink(unsigned from);

/* calls that operate on an entire parameter RAM slot */
void edma_write_slot(unsigned slot, const struct edmacc_param *params);
void edma_read_slot(unsigned slot, struct edmacc_param *params);

/* channel control operations */
int edma_start(unsigned channel);
void edma_stop(unsigned channel);
void edma_clean_channel(unsigned channel);
void edma_clear_event(unsigned channel);
void edma_pause(unsigned channel);
void edma_resume(unsigned channel);

struct edma_rsv_info {

	const s16	(*rsv_chans)[2];
	const s16	(*rsv_slots)[2];
};

/* platform_data for EDMA driver */
struct edma_soc_info {

	/* how many dma resources of each type */
	unsigned	n_channel;
	unsigned	n_region;
	unsigned	n_slot;
	unsigned	n_tc;
	unsigned	n_cc;
	enum dma_event_q	default_queue;

	/* Resource reservation for other cores */
	struct edma_rsv_info	*rsv;

	const s8	(*queue_tc_mapping)[2];
	const s8	(*queue_priority_mapping)[2];
};

#endif
