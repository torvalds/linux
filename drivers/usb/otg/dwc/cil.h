/*
 * DesignWare HS OTG controller driver
 * Copyright (C) 2006 Synopsys, Inc.
 * Portions Copyright (C) 2010 Applied Micro Circuits Corporation.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/licenses
 * or write to the Free Software Foundation, Inc., 51 Franklin Street,
 * Suite 500, Boston, MA 02110-1335 USA.
 *
 * Based on Synopsys driver version 2.60a
 * Modified by Mark Miesfeld <mmiesfeld@apm.com>
 * Modified by Stefan Roese <sr@denx.de>, DENX Software Engineering
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING BUT NOT LIMITED TO THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL SYNOPSYS, INC. BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES
 * (INCLUDING BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#if !defined(__DWC_CIL_H__)
#define __DWC_CIL_H__
#include <linux/io.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/interrupt.h>
#include <linux/dmapool.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/usb/otg.h>
#include <linux/module.h>

#include "regs.h"

#ifdef CONFIG_DWC_DEBUG
#define DEBUG
#endif

/**
 * Reads the content of a register.
 */
static inline u32 dwc_reg_read(ulong reg, u32 offset)
{
	return readl((unsigned __iomem *)(reg + offset));
};

static inline void dwc_reg_write(ulong reg, u32 offset, const u32 value)
{
	writel(value, (unsigned __iomem *)(reg + offset));
};
/**
 * This function modifies bit values in a register.  Using the
 * algorithm: (reg_contents & ~clear_mask) | set_mask.
 */
static inline
	void dwc_reg_modify(ulong reg, u32 offset, const u32 _clear_mask, const u32 _set_mask)
{
	writel((readl((unsigned __iomem *)(reg + offset)) & ~_clear_mask) | _set_mask,
		(unsigned __iomem *)(reg + offset));
};

static inline void dwc_write_fifo32(ulong reg, const u32 _value)
{
	writel(_value, (unsigned __iomem *)reg);
};

static inline u32 dwc_read_fifo32(u32 _reg)
{
	return readl((unsigned __iomem *) _reg);
};

/*
 * Debugging support vanishes in non-debug builds.
 */
/* Display CIL Debug messages */
#define dwc_dbg_cil		(0x2)

/* Display CIL Verbose debug messages */
#define dwc_dbg_cilv		(0x20)

/* Display PCD (Device) debug messages */
#define dwc_dbg_pcd		(0x4)

/* Display PCD (Device) Verbose debug  messages */
#define dwc_dbg_pcdv		(0x40)

/* Display Host debug messages */
#define dwc_dbg_hcd		(0x8)

/* Display Verbose Host debug messages */
#define dwc_dbg_hcdv		(0x80)

/* Display enqueued URBs in host mode. */
#define dwc_dbg_hcd_urb		(0x800)

/* Display "special purpose" debug messages */
#define dwc_dbg_sp		(0x400)

/* Display all debug messages */
#define dwc_dbg_any		(0xFF)

/* All debug messages off */
#define dwc_dbg_off		0

/* Prefix string for DWC_DEBUG print macros. */
#define usb_dwc "dwc_otg: "

/*
 * This file contains the interface to the Core Interface Layer.
 */

/*
 * Added-sr: 2007-07-26
 *
 * Since the 405EZ (Ultra) only support 2047 bytes as
 * max transfer size, we have to split up bigger transfers
 * into multiple transfers of 1024 bytes sized messages.
 * I happens often, that transfers of 4096 bytes are
 * required (zero-gadget, file_storage-gadget).
 *
 * MAX_XFER_LEN is set to 1024 right now, but could be 2047,
 * since the xfer-size field in the 405EZ USB device controller
 * implementation has 11 bits. Using 1024 seems to work for now.
 */
#define MAX_XFER_LEN	1024

/*
 * The dwc_ep structure represents the state of a single endpoint when acting in
 * device mode. It contains the data items needed for an endpoint to be
 * activated and transfer packets.
 */
struct dwc_ep {
	/* EP number used for register address lookup */
	u8 num;
	/* EP direction 0 = OUT */
	unsigned is_in:1;
	/* EP active. */
	unsigned active:1;

	/*
	 * Periodic Tx FIFO # for IN EPs For INTR EP set to 0 to use
	 * non-periodic Tx FIFO If dedicated Tx FIFOs are enabled for all
	 * IN Eps - Tx FIFO # FOR IN EPs
	 */
	unsigned tx_fifo_num:4;
	/* EP type: 0 - Control, 1 - ISOC,       2 - BULK,      3 - INTR */
	unsigned type:2;
#define DWC_OTG_EP_TYPE_CONTROL		0
#define DWC_OTG_EP_TYPE_ISOC		1
#define DWC_OTG_EP_TYPE_BULK		2
#define DWC_OTG_EP_TYPE_INTR		3

	/* DATA start PID for INTR and BULK EP */
	unsigned data_pid_start:1;
	/* Frame (even/odd) for ISOC EP */
	unsigned even_odd_frame:1;
	/* Max Packet bytes */
	unsigned maxpacket:11;

	struct mutex xfer_mutex;
	ulong dma_addr;

	/*
	 * Pointer to the beginning of the transfer buffer -- do not modify
	 * during transfer.
	 */
	u8 *start_xfer_buff;
	/* pointer to the transfer buffer */
	u8 *xfer_buff;
	/* Number of bytes to transfer */
	unsigned xfer_len:19;
	/* Number of bytes transferred. */
	unsigned xfer_count:19;
	/* Sent ZLP */
	unsigned sent_zlp:1;
	/* Total len for control transfer */
	unsigned total_len:19;

	/* stall clear flag */
	unsigned stall_clear_flag:1;

	/*
	 * Added-sr: 2007-07-26
	 *
	 * Since the 405EZ (Ultra) only support 2047 bytes as
	 * max transfer size, we have to split up bigger transfers
	 * into multiple transfers of 1024 bytes sized messages.
	 * I happens often, that transfers of 4096 bytes are
	 * required (zero-gadget, file_storage-gadget).
	 *
	 * "bytes_pending" will hold the amount of bytes that are
	 * still pending to be send in further messages to complete
	 * the bigger transfer.
	 */
	u32 bytes_pending;
};

/*
 * States of EP0.
 */
enum ep0_state {
	EP0_DISCONNECT = 0,	/* no host */
	EP0_IDLE = 1,
	EP0_IN_DATA_PHASE = 2,
	EP0_OUT_DATA_PHASE = 3,
	EP0_STATUS = 4,
	EP0_STALL = 5,
};

/* Fordward declaration.*/
struct dwc_pcd;

/*
 * This structure describes an EP, there is an array of EPs in the PCD
 * structure.
 */
struct pcd_ep {
	/* USB EP data */
	struct usb_ep ep;
	/* USB EP Descriptor */
	const struct usb_endpoint_descriptor *desc;

	/* queue of dwc_otg_pcd_requests. */
	struct list_head queue;
	unsigned stopped:1;
	unsigned disabling:1;
	unsigned dma:1;
	unsigned queue_sof:1;
	unsigned wedged:1;

	/* DWC_otg ep data. */
	struct dwc_ep dwc_ep;

	/* Pointer to PCD */
	struct dwc_pcd *pcd;
};

/*
 * DWC_otg PCD Structure.
 * This structure encapsulates the data for the dwc_otg PCD.
 */
struct dwc_pcd {
	/* USB gadget */
	struct usb_gadget gadget;
	/* USB gadget driver pointer */
	struct usb_gadget_driver *driver;
	/* The DWC otg device pointer. */
	struct dwc_otg_device *otg_dev;

	/* State of EP0 */
	enum ep0_state ep0state;
	/* EP0 Request is pending */
	unsigned ep0_pending:1;
	/* Indicates when SET CONFIGURATION Request is in process */
	unsigned request_config:1;
	/* The state of the Remote Wakeup Enable. */
	unsigned remote_wakeup_enable:1;
	/* The state of the B-Device HNP Enable. */
	unsigned b_hnp_enable:1;
	/* The state of A-Device HNP Support. */
	unsigned a_hnp_support:1;
	/* The state of the A-Device Alt HNP support. */
	unsigned a_alt_hnp_support:1;
	/* Count of pending Requests */
	unsigned request_pending;

	/*
	 * SETUP packet for EP0.  This structure is allocated as a DMA buffer on
	 * PCD initialization with enough space for up to 3 setup packets.
	 */
	union {
		struct usb_ctrlrequest req;
		u32 d32[2];
	} *setup_pkt;

	struct dma_pool *dwc_pool;
	dma_addr_t setup_pkt_dma_handle;

	/* 2-byte dma buffer used to return status from GET_STATUS */
	u16 *status_buf;
	dma_addr_t status_buf_dma_handle;

	/* Array of EPs. */
	struct pcd_ep ep0;
	/* Array of IN EPs. */
	struct pcd_ep in_ep[MAX_EPS_CHANNELS - 1];
	/* Array of OUT EPs. */
	struct pcd_ep out_ep[MAX_EPS_CHANNELS - 1];
	spinlock_t lock;
	/*
	 *  Timer for SRP.  If it expires before SRP is successful clear the
	 *  SRP.
	 */
	struct timer_list srp_timer;

	/*
	 * Tasklet to defer starting of TEST mode transmissions until Status
	 * Phase has been completed.
	 */
	struct tasklet_struct test_mode_tasklet;

	/*  Tasklet to delay starting of xfer in DMA mode */
	struct tasklet_struct *start_xfer_tasklet;

	/* The test mode to enter when the tasklet is executed. */
	unsigned test_mode;
};

/*
 * This structure holds the state of the HCD, including the non-periodic and
 * periodic schedules.
 */
struct dwc_hcd {
	spinlock_t lock;

	/* DWC OTG Core Interface Layer */
	struct core_if *core_if;

	/* Internal DWC HCD Flags */
	union dwc_otg_hcd_internal_flags {
		u32 d32;
		struct {
			unsigned port_connect_status_change:1;
			unsigned port_connect_status:1;
			unsigned port_reset_change:1;
			unsigned port_enable_change:1;
			unsigned port_suspend_change:1;
			unsigned port_over_current_change:1;
			unsigned reserved:27;
		} b;
	} flags;

	/*
	 * Inactive items in the non-periodic schedule. This is a list of
	 * Queue Heads. Transfers associated with these Queue Heads are not
	 * currently assigned to a host channel.
	 */
	struct list_head non_periodic_sched_inactive;

	/*
	 * Deferred items in the non-periodic schedule. This is a list of
	 * Queue Heads. Transfers associated with these Queue Heads are not
	 * currently assigned to a host channel.
	 * When we get an NAK, the QH goes here.
	 */
	struct list_head non_periodic_sched_deferred;

	/*
	 * Active items in the non-periodic schedule. This is a list of
	 * Queue Heads. Transfers associated with these Queue Heads are
	 * currently assigned to a host channel.
	 */
	struct list_head non_periodic_sched_active;

	/*
	 * Pointer to the next Queue Head to process in the active
	 * non-periodic schedule.
	 */
	struct list_head *non_periodic_qh_ptr;

	/*
	 * Inactive items in the periodic schedule. This is a list of QHs for
	 * periodic transfers that are _not_ scheduled for the next frame.
	 * Each QH in the list has an interval counter that determines when it
	 * needs to be scheduled for execution. This scheduling mechanism
	 * allows only a simple calculation for periodic bandwidth used (i.e.
	 * must assume that all periodic transfers may need to execute in the
	 * same frame). However, it greatly simplifies scheduling and should
	 * be sufficient for the vast majority of OTG hosts, which need to
	 * connect to a small number of peripherals at one time.
	 *
	 * Items move from this list to periodic_sched_ready when the QH
	 * interval counter is 0 at SOF.
	 */
	struct list_head periodic_sched_inactive;

	/*
	 * List of periodic QHs that are ready for execution in the next
	 * frame, but have not yet been assigned to host channels.
	 *
	 * Items move from this list to periodic_sched_assigned as host
	 * channels become available during the current frame.
	 */
	struct list_head periodic_sched_ready;

	/*
	 * List of periodic QHs to be executed in the next frame that are
	 * assigned to host channels.
	 *
	 * Items move from this list to periodic_sched_queued as the
	 * transactions for the QH are queued to the DWC_otg controller.
	 */
	struct list_head periodic_sched_assigned;

	/*
	 * List of periodic QHs that have been queued for execution.
	 *
	 * Items move from this list to either periodic_sched_inactive or
	 * periodic_sched_ready when the channel associated with the transfer
	 * is released. If the interval for the QH is 1, the item moves to
	 * periodic_sched_ready because it must be rescheduled for the next
	 * frame. Otherwise, the item moves to periodic_sched_inactive.
	 */
	struct list_head periodic_sched_queued;

	/*
	 * Total bandwidth claimed so far for periodic transfers. This value
	 * is in microseconds per (micro)frame. The assumption is that all
	 * periodic transfers may occur in the same (micro)frame.
	 */
	u16 periodic_usecs;

	/*
	 * Total bandwidth claimed so far for all periodic transfers
	 * in a frame.
	 * This will include a mixture of HS and FS transfers.
	 * Units are microseconds per (micro)frame.
	 * We have a budget per frame and have to schedule
	 * transactions accordingly.
	 * Watch out for the fact that things are actually scheduled for the
	 * "next frame".
	 */
	u16 frame_usecs[8];

	/*
	 * Frame number read from the core at SOF. The value ranges from 0 to
	 * DWC_HFNUM_MAX_FRNUM.
	 */
	u16 frame_number;

	/*
	 * Free host channels in the controller. This is a list of
	 * struct dwc_hc items.
	 */
	struct list_head free_hc_list;

	/*
	 * Number of available host channels.
	 */
	u32 available_host_channels;

	/*
	 * Array of pointers to the host channel descriptors. Allows accessing
	 * a host channel descriptor given the host channel number. This is
	 * useful in interrupt handlers.
	 */
	struct dwc_hc *hc_ptr_array[MAX_EPS_CHANNELS];

	/*
	 * Buffer to use for any data received during the status phase of a
	 * control transfer. Normally no data is transferred during the status
	 * phase. This buffer is used as a bit bucket.
	 */
	u8 *status_buf;

	/*
	 * DMA address for status_buf.
	 */
	dma_addr_t status_buf_dma;
#define DWC_OTG_HCD_STATUS_BUF_SIZE		64

	/*
	 * Structure to allow starting the HCD in a non-interrupt context
	 * during an OTG role change.
	 */
	struct work_struct start_work;
	struct usb_hcd *_p;

	/*
	 * Connection timer. An OTG host must display a message if the device
	 * does not connect. Started when the VBus power is turned on via
	 * sysfs attribute "buspower".
	 */
	struct timer_list conn_timer;

	/* workqueue for port wakeup */
	struct work_struct usb_port_reset;

	/* Addition HCD interrupt */
	int cp_irq;		/* charge pump interrupt */
	int cp_irq_installed;
};

/*
 * Reasons for halting a host channel.
 */
enum dwc_halt_status {
	DWC_OTG_HC_XFER_NO_HALT_STATUS,
	DWC_OTG_HC_XFER_COMPLETE,
	DWC_OTG_HC_XFER_URB_COMPLETE,
	DWC_OTG_HC_XFER_ACK,
	DWC_OTG_HC_XFER_NAK,
	DWC_OTG_HC_XFER_NYET,
	DWC_OTG_HC_XFER_STALL,
	DWC_OTG_HC_XFER_XACT_ERR,
	DWC_OTG_HC_XFER_FRAME_OVERRUN,
	DWC_OTG_HC_XFER_BABBLE_ERR,
	DWC_OTG_HC_XFER_DATA_TOGGLE_ERR,
	DWC_OTG_HC_XFER_AHB_ERR,
	DWC_OTG_HC_XFER_PERIODIC_INCOMPLETE,
	DWC_OTG_HC_XFER_URB_DEQUEUE
};

/*
 * Host channel descriptor. This structure represents the state of a single
 * host channel when acting in host mode. It contains the data items needed to
 * transfer packets to an endpoint via a host channel.
 */
struct dwc_hc {
	/* Host channel number used for register address lookup */
	u8 hc_num;

	/* Device to access */
	unsigned dev_addr:7;

	/* EP to access */
	unsigned ep_num:4;

	/* EP direction. 0: OUT, 1: IN */
	unsigned ep_is_in:1;

	/*
	 * EP speed.
	 * One of the following values:
	 *      - DWC_OTG_EP_SPEED_LOW
	 *      - DWC_OTG_EP_SPEED_FULL
	 *      - DWC_OTG_EP_SPEED_HIGH
	 */
	unsigned speed:2;
#define DWC_OTG_EP_SPEED_LOW		0
#define DWC_OTG_EP_SPEED_FULL		1
#define DWC_OTG_EP_SPEED_HIGH		2

	/*
	 * Endpoint type.
	 * One of the following values:
	 *      - DWC_OTG_EP_TYPE_CONTROL: 0
	 *      - DWC_OTG_EP_TYPE_ISOC: 1
	 *      - DWC_OTG_EP_TYPE_BULK: 2
	 *      - DWC_OTG_EP_TYPE_INTR: 3
	 */
	unsigned ep_type:2;

	/* Max packet size in bytes */
	unsigned max_packet:11;

	/*
	 * PID for initial transaction.
	 * 0: DATA0,
	 * 1: DATA2,
	 * 2: DATA1,
	 * 3: MDATA (non-Control EP),
	 *      SETUP (Control EP)
	 */
	unsigned data_pid_start:2;
#define DWC_OTG_HC_PID_DATA0		0
#define DWC_OTG_HC_PID_DATA2		1
#define DWC_OTG_HC_PID_DATA1		2
#define DWC_OTG_HC_PID_MDATA		3
#define DWC_OTG_HC_PID_SETUP		3

	/* Number of periodic transactions per (micro)frame */
	unsigned multi_count:2;

	/* Pointer to the current transfer buffer position. */
	u8 *xfer_buff;
	/* Total number of bytes to transfer. */
	u32 xfer_len;
	/* Number of bytes transferred so far. */
	u32 xfer_count;
	/* Packet count at start of transfer. */
	u16 start_pkt_count;

	/*
	 * Flag to indicate whether the transfer has been started. Set to 1 if
	 * it has been started, 0 otherwise.
	 */
	u8 xfer_started;

	/*
	 * Set to 1 to indicate that a PING request should be issued on this
	 * channel. If 0, process normally.
	 */
	u8 do_ping;

	/*
	 * Set to 1 to indicate that the error count for this transaction is
	 * non-zero. Set to 0 if the error count is 0.
	 */
	u8 error_state;

	/*
	 * Set to 1 to indicate that this channel should be halted the next
	 * time a request is queued for the channel. This is necessary in
	 * slave mode if no request queue space is available when an attempt
	 * is made to halt the channel.
	 */
	u8 halt_on_queue;

	/*
	 * Set to 1 if the host channel has been halted, but the core is not
	 * finished flushing queued requests. Otherwise 0.
	 */
	u8 halt_pending;

	/* Reason for halting the host channel. */
	enum dwc_halt_status halt_status;

	/*  Split settings for the host channel */
	u8 do_split;		/* Enable split for the channel */
	u8 complete_split;	/* Enable complete split */
	u8 hub_addr;		/* Address of high speed hub */
	u8 port_addr;		/* Port of the low/full speed device */

	/*
	 * Split transaction position. One of the following values:
	 *      - DWC_HCSPLIT_XACTPOS_MID
	 *      - DWC_HCSPLIT_XACTPOS_BEGIN
	 *      - DWC_HCSPLIT_XACTPOS_END
	 *      - DWC_HCSPLIT_XACTPOS_ALL */
	u8 xact_pos;

	/* Set when the host channel does a short read. */
	u8 short_read;

	/*
	 * Number of requests issued for this channel since it was assigned to
	 * the current transfer (not counting PINGs).
	 */
	u8 requests;

	/* Queue Head for the transfer being processed by this channel. */
	struct dwc_qh *qh;

	/* Entry in list of host channels. */
	struct list_head hc_list_entry;
};

/*
 * The following parameters may be specified when starting the module. These
 * parameters define how the DWC_otg controller should be configured.  Parameter
 * values are passed to the CIL initialization function dwc_otg_cil_init.
 */
struct core_params {
	/*
	 * Specifies whether to use slave or DMA mode for accessing the data
	 * FIFOs. The driver will automatically detect the value for this
	 * parameter if none is specified.
	 * 0 - Slave
	 * 1 - DMA (default, if available)
	 */
	int dma_enable;
#ifdef CONFIG_DWC_SLAVE
#define dwc_param_dma_enable_default			0
#else
#define dwc_param_dma_enable_default			1
#endif

	/*
	 * The DMA Burst size (applicable only for External DMA Mode).
	 * 1, 4, 8 16, 32, 64, 128, 256 (default 32)
	 */
	int dma_burst_size;	/* Translate this to GAHBCFG values */
#define dwc_param_dma_burst_size_default		32

#define DWC_SPEED_PARAM_HIGH				0
#define DWC_SPEED_PARAM_FULL				1

	/*
	 * 0 - Use cC FIFO size parameters
	 * 1 - Allow dynamic FIFO sizing (default)
	 */
	int enable_dynamic_fifo;
#define dwc_param_enable_dynamic_fifo_default		1

	/*
	 * Number of 4-byte words in the Rx FIFO in device mode when dynamic
	 * FIFO sizing is enabled.  16 to 32768 (default 1064)
	 */
	int dev_rx_fifo_size;
#define dwc_param_dev_rx_fifo_size_default		1064

	/*
	 * Number of 4-byte words in the non-periodic Tx FIFO in device mode
	 * when dynamic FIFO sizing is enabled.  16 to 32768 (default 1024)
	 */
	int dev_nperio_tx_fifo_size;
#define dwc_param_dev_nperio_tx_fifo_size_default	1024

	/*
	 * Number of 4-byte words in each of the periodic Tx FIFOs in device
	 * mode when dynamic FIFO sizing is enabled.  4 to 768 (default 256)
	 */
	u32 dev_perio_tx_fifo_size[MAX_PERIO_FIFOS];
#define dwc_param_dev_perio_tx_fifo_size_default	256

	/*
	 * Number of 4-byte words in the Rx FIFO in host mode when dynamic
	 * FIFO sizing is enabled.  16 to 32768 (default 1024)
	 */
	int host_rx_fifo_size;
#define dwc_param_host_rx_fifo_size_default		1024

	/*
	 * Number of 4-byte words in the non-periodic Tx FIFO in host mode
	 * when Dynamic FIFO sizing is enabled in the core.  16 to 32768
	 * (default 1024)
	 */
	int host_nperio_tx_fifo_size;
#define dwc_param_host_nperio_tx_fifo_size_default	1024

	/*
	   Number of 4-byte words in the host periodic Tx FIFO when dynamic
	   * FIFO sizing is enabled.  16 to 32768 (default 1024)
	 */
	int host_perio_tx_fifo_size;
#define dwc_param_host_perio_tx_fifo_size_default	1024

	/*
	 * The maximum transfer size supported in bytes. 2047 to 65,535
	 * (default 65,535)
	 */
	int max_transfer_size;
#define dwc_param_max_transfer_size_default		65535

	/*
	 * The maximum number of packets in a transfer. 15 to 511  (default 511)
	 */
	int max_packet_count;
#define dwc_param_max_packet_count_default		511

	/*
	 * The number of host channel registers to use.
	 * 1 to 16 (default 12)
	 * Note: The FPGA configuration supports a maximum of 12 host channels.
	 */
	int host_channels;
#define dwc_param_host_channels_default			12

	/*
	 * The number of endpoints in addition to EP0 available for device
	 * mode operations.
	 * 1 to 15 (default 6 IN and OUT)
	 * Note: The FPGA configuration supports a maximum of 6 IN and OUT
	 * endpoints in addition to EP0.
	 */
	int dev_endpoints;
#define dwc_param_dev_endpoints_default			6

#define DWC_PHY_TYPE_PARAM_FS			0
#define DWC_PHY_TYPE_PARAM_UTMI			1
#define DWC_PHY_TYPE_PARAM_ULPI			2

	/*
	 * Specifies the UTMI+ Data Width.  This parameter is applicable for a
	 * PHY_TYPE of UTMI+ or ULPI. (For a ULPI PHY_TYPE, this parameter
	 * indicates the data width between the MAC and the ULPI Wrapper.) Also,
	 * this parameter is applicable only if the OTG_HSPHY_WIDTH cC parameter
	 * was set to "8 and 16 bits", meaning that the core has been configured
	 * to work at either data path width.
	 *
	 * 8 or 16 bits (default 16)
	 */
	int phy_utmi_width;
#define dwc_param_phy_utmi_width_default	16

	/*
	 * Specifies whether the ULPI operates at double or single
	 * data rate. This parameter is only applicable if PHY_TYPE is
	 * ULPI.
	 *
	 *      0 - single data rate ULPI interface with 8 bit wide data
	 *              bus (default)
	 *      1 - double data rate ULPI interface with 4 bit wide data
	 *              bus
	 */
	int phy_ulpi_ddr;
#define dwc_param_phy_ulpi_ddr_default		0

	/*
	 * Specifies whether dedicated transmit FIFOs are enabled for non
	 * periodic IN endpoints in device mode
	 *      0 - No
	 *      1 - Yes
	 */
	int en_multiple_tx_fifo;
#define dwc_param_en_multiple_tx_fifo_default	1

	/*
	 * Number of 4-byte words in each of the Tx FIFOs in device
	 * mode when dynamic FIFO sizing is enabled. 4 to 768 (default 256)
	 */
	u32 dev_tx_fifo_size[MAX_TX_FIFOS];
#define dwc_param_dev_tx_fifo_size_default	256

};

/*
 * The core_if structure contains information needed to manage the
 * DWC_otg controller acting in either host or device mode. It represents the
 * programming view of the controller as a whole.
 */
struct core_if {
	/* Parameters that define how the core should be configured. */
	struct core_params *core_params;

	/* Core Global registers starting at offset 000h. */
	ulong core_global_regs;

	/* Device-specific information */
	struct device_if *dev_if;
	/* Host-specific information */
	struct dwc_host_if *host_if;

	/*
	 * Set to 1 if the core PHY interface bits in USBCFG have been
	 * initialized.
	 */
	u8 phy_init_done;

	/*
	 * SRP Success flag, set by srp success interrupt in FS I2C mode
	 */
	u8 srp_success;
	u8 srp_timer_started;

	/* Common configuration information */
	/* Power and Clock Gating Control Register */
	ulong pcgcctl;
#define DWC_OTG_PCGCCTL_OFFSET			0xE00

	/* Push/pop addresses for endpoints or host channels. */
	ulong data_fifo[MAX_EPS_CHANNELS];
#define DWC_OTG_DATA_FIFO_OFFSET		0x1000
#define DWC_OTG_DATA_FIFO_SIZE			0x1000

	/* Total RAM for FIFOs (Bytes) */
	u16 total_fifo_size;
	/* Size of Rx FIFO (Bytes) */
	u16 rx_fifo_size;
	/* Size of Non-periodic Tx FIFO (Bytes) */
	u16 nperio_tx_fifo_size;

	/* 1 if DMA is enabled, 0 otherwise. */
	u8 dma_enable;

	/* 1 if dedicated Tx FIFOs are enabled, 0 otherwise. */
	u8 en_multiple_tx_fifo;

	/*
	 *  Set to 1 if multiple packets of a high-bandwidth transfer is in
	 * process of being queued
	 */
	u8 queuing_high_bandwidth;

	/* Hardware Configuration -- stored here for convenience. */
	ulong hwcfg1;
	ulong hwcfg2;
	ulong hwcfg3;
	ulong hwcfg4;

	/* HCD callbacks */
	/* include/linux/usb/otg.h */

	/* HCD callbacks */
	struct cil_callbacks *hcd_cb;
	/* PCD callbacks */
	struct cil_callbacks *pcd_cb;

	/* Device mode Periodic Tx FIFO Mask */
	u32 p_tx_msk;
	/* Device mode Periodic Tx FIFO Mask */
	u32 tx_msk;

	/* Features of various DWC implementation */
	u32 features;

	/* Added to support PLB DMA : phys-virt mapping */
	resource_size_t phys_addr;

	struct delayed_work usb_port_wakeup;
	struct work_struct usb_port_otg;
	struct usb_phy *xceiv;
	bool wqfunc_setup_done;
};

/*
 * The following functions support initialization of the CIL driver component
 * and the DWC_otg controller.
 */
extern void dwc_otg_core_init(struct core_if *core_if);
extern void init_fslspclksel(struct core_if *core_if);
extern void dwc_otg_core_dev_init(struct core_if *core_if);
extern void dwc_otg_enable_global_interrupts(struct core_if *core_if);
extern void dwc_otg_disable_global_interrupts(struct core_if *core_if);
extern void dwc_otg_enable_common_interrupts(struct core_if *core_if);

/**
 * This function Reads HPRT0 in preparation to modify.  It keeps the WC bits 0
 * so that if they are read as 1, they won't clear when you write it back
 */
static inline u32 dwc_otg_read_hprt0(struct core_if *core_if)
{
	u32 hprt0 = 0;
	hprt0 = dwc_reg_read(core_if->host_if->hprt0, 0);
	hprt0 = DWC_HPRT0_PRT_ENA_RW(hprt0, 0);
	hprt0 = DWC_HPRT0_PRT_CONN_DET_RW(hprt0, 0);
	hprt0 = DWC_HPRT0_PRT_ENA_DIS_CHG_RW(hprt0, 0);
	hprt0 = DWC_HPRT0_PRT_OVRCURR_ACT_RW(hprt0, 0);
	return hprt0;
}

/*
 * The following functions support managing the DWC_otg controller in either
 * device or host mode.
 */
extern void dwc_otg_read_packet(struct core_if *core_if, u8 * dest, u16 bytes);
extern void dwc_otg_flush_tx_fifo(struct core_if *core_if, const int _num);
extern void dwc_otg_flush_rx_fifo(struct core_if *core_if);

#define NP_TXFIFO_EMPTY			-1
#define MAX_NP_TXREQUEST_Q_SLOTS	8

/**
 * This function returns the Core Interrupt register.
 */
static inline u32 dwc_otg_read_core_intr(struct core_if *core_if)
{
	u32 global_regs = (u32) core_if->core_global_regs;
	return dwc_reg_read(global_regs, DWC_GINTSTS) &
	    dwc_reg_read(global_regs, DWC_GINTMSK);
}

/**
 * This function returns the mode of the operation, host or device.
 */
static inline u32 dwc_otg_mode(struct core_if *core_if)
{
	u32 global_regs = (u32) core_if->core_global_regs;
	return dwc_reg_read(global_regs, DWC_GINTSTS) & 0x1;
}

static inline u8 dwc_otg_is_device_mode(struct core_if *core_if)
{
	return dwc_otg_mode(core_if) != DWC_HOST_MODE;
}
static inline u8 dwc_otg_is_host_mode(struct core_if *core_if)
{
	return dwc_otg_mode(core_if) == DWC_HOST_MODE;
}

extern int dwc_otg_handle_common_intr(struct core_if *core_if);

/*
 * DWC_otg CIL callback structure.  This structure allows the HCD and PCD to
 * register functions used for starting and stopping the PCD and HCD for role
 * change on for a DRD.
 */
struct cil_callbacks {
	/* Start function for role change */
	int (*start) (void *_p);
	/* Stop Function for role change */
	int (*stop) (void *_p);
	/* Disconnect Function for role change */
	int (*disconnect) (void *_p);
	/* Resume/Remote wakeup Function */
	int (*resume_wakeup) (void *_p);
	/* Suspend function */
	int (*suspend) (void *_p);
	/* Session Start (SRP) */
	int (*session_start) (void *_p);
	/* Pointer passed to start() and stop() */
	void *p;
};

extern void dwc_otg_cil_register_pcd_callbacks(struct core_if *core_if,
					       struct cil_callbacks *cb,
					       void *p);
extern void dwc_otg_cil_register_hcd_callbacks(struct core_if *core_if,
					       struct cil_callbacks *cb,
					       void *p);

#define DWC_LIMITED_XFER		0x00000000
#define DWC_DEVICE_ONLY			0x00000000
#define DWC_HOST_ONLY			0x00000000

#ifdef DWC_LIMITED_XFER_SIZE
#undef DWC_LIMITED_XFER
#define DWC_LIMITED_XFER		0x00000001
#endif

#ifdef CONFIG_DWC_DEVICE_ONLY
#undef DWC_DEVICE_ONLY
#define DWC_DEVICE_ONLY			0x00000002
static inline void dwc_otg_hcd_remove(struct device *dev)
{
}
static inline int dwc_otg_hcd_init(struct device *_dev,
				   struct dwc_otg_device *dwc_dev)
{
	return 0;
}
#else
extern int __init dwc_otg_hcd_init(struct device *_dev,
				   struct dwc_otg_device *dwc_dev);
extern void dwc_otg_hcd_remove(struct device *_dev);
#endif

#ifdef CONFIG_DWC_HOST_ONLY
#undef DWC_HOST_ONLY
#define DWC_HOST_ONLY			0x00000004
static inline void dwc_otg_pcd_remove(struct device *dev)
{
}
static inline int dwc_otg_pcd_init(struct device *dev)
{
	return 0;
}
#else
extern void dwc_otg_pcd_remove(struct device *dev);
extern int __init dwc_otg_pcd_init(struct device *dev);
#endif

extern void dwc_otg_cil_remove(struct core_if *core_if);
extern struct core_if *dwc_otg_cil_init(const __iomem u32 *base,
						  struct core_params *params);

static inline void dwc_set_feature(struct core_if *core_if)
{
	core_if->features = DWC_LIMITED_XFER | DWC_DEVICE_ONLY | DWC_HOST_ONLY;
}

static inline int dwc_has_feature(struct core_if *core_if,
				  unsigned long feature)
{
	return core_if->features & feature;
}
extern struct core_params dwc_otg_module_params;
extern int check_parameters(struct core_if *core_if);
#endif
