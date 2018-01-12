/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg/linux/drivers/dwc_otg_cil.h $
 * $Revision: #127 $
 * $Date: 2012/12/21 $
 * $Change: 2131568 $
 *
 * Synopsys HS OTG Linux Software Driver and documentation (hereinafter,
 * "Software") is an Unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you.
 *
 * The Software IS NOT an item of Licensed Software or Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Product
 * with Synopsys or any supplement thereto. You are permitted to use and
 * redistribute this Software in source and binary forms, with or without
 * modification, provided that redistributions of source code must retain this
 * notice. You may not view, use, disclose, copy or distribute this file or
 * any information contained herein except pursuant to this license grant from
 * Synopsys. If you do not agree with this notice, including the disclaimer
 * below, then you are not authorized to use the Software.
 *
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================== */

#if !defined(__DWC_CIL_H__)
#define __DWC_CIL_H__

#include "common_port/dwc_list.h"
#include "dwc_otg_dbg.h"
#include "dwc_otg_regs.h"

#include "dwc_otg_core_if.h"
#include "dwc_otg_adp.h"

/**
 * @file
 * This file contains the interface to the Core Interface Layer.
 */

#ifdef DWC_UTE_CFI

#define MAX_DMA_DESCS_PER_EP	256

/**
 * Enumeration for the data buffer mode
 */
typedef enum _data_buffer_mode {
	BM_STANDARD = 0,	/* data buffer is in normal mode */
	BM_SG = 1,		/* data buffer uses the scatter/gather mode */
	BM_CONCAT = 2,		/* data buffer uses the concatenation mode */
	BM_CIRCULAR = 3,	/* data buffer uses the circular DMA mode */
	BM_ALIGN = 4		/* data buffer is in buffer alignment mode */
} data_buffer_mode_e;
#endif /* DWC_UTE_CFI */

/** Macros defined for DWC OTG HW Release version */

#define OTG_CORE_REV_2_60a	0x4F54260A
#define OTG_CORE_REV_2_71a	0x4F54271A
#define OTG_CORE_REV_2_72a	0x4F54272A
#define OTG_CORE_REV_2_80a	0x4F54280A
#define OTG_CORE_REV_2_81a	0x4F54281A
#define OTG_CORE_REV_2_90a	0x4F54290A
#define OTG_CORE_REV_2_91a	0x4F54291A
#define OTG_CORE_REV_2_92a	0x4F54292A
#define OTG_CORE_REV_2_93a	0x4F54293A
#define OTG_CORE_REV_2_94a	0x4F54294A
#define OTG_CORE_REV_3_00a	0x4F54300A
#define OTG_CORE_REV_3_10a	0x4F54310A

/**
 * Information for each ISOC packet.
 */
typedef struct iso_pkt_info {
	uint32_t offset;
	uint32_t length;
	int32_t status;
} iso_pkt_info_t;

/**
 * The <code>dwc_ep</code> structure represents the state of a single
 * endpoint when acting in device mode. It contains the data items
 * needed for an endpoint to be activated and transfer packets.
 */
typedef struct dwc_ep {
	/** EP number used for register address lookup */
	uint8_t num;
	/** EP direction 0 = OUT */
	unsigned is_in:1;
	/** EP active. */
	unsigned active:1;

	/**
	 * Periodic Tx FIFO # for IN EPs For INTR EP set to 0 to use non-periodic
	 * Tx FIFO. If dedicated Tx FIFOs are enabled Tx FIFO # FOR IN EPs*/
	unsigned tx_fifo_num:4;
	/** EP type: 0 - Control, 1 - ISOC,	 2 - BULK,	3 - INTR */
	unsigned type:2;
#define DWC_OTG_EP_TYPE_CONTROL	   0
#define DWC_OTG_EP_TYPE_ISOC	   1
#define DWC_OTG_EP_TYPE_BULK	   2
#define DWC_OTG_EP_TYPE_INTR	   3

	/** DATA start PID for INTR and BULK EP */
	unsigned data_pid_start:1;
	/** Frame (even/odd) for ISOC EP */
	unsigned even_odd_frame:1;
	/** Max Packet bytes */
	unsigned maxpacket:11;

	/** Max Transfer size */
	uint32_t maxxfer;

	/** @name Transfer state */
	/** @{ */

	/**
	 * Pointer to the beginning of the transfer buffer -- do not modify
	 * during transfer.
	 */

	dwc_dma_t dma_addr;

	dwc_dma_t dma_desc_addr;
	dwc_otg_dev_dma_desc_t *desc_addr;

	uint8_t *start_xfer_buff;
	/** pointer to the transfer buffer */
	uint8_t *xfer_buff;
	/** Number of bytes to transfer */
	unsigned xfer_len:19;
	/** Number of bytes transferred. */
	unsigned xfer_count:19;
	/** Sent ZLP */
	unsigned sent_zlp:1;
	/** Total len for control transfer */
	unsigned total_len:19;

	/** stall clear flag */
	unsigned stall_clear_flag:1;

	/** SETUP pkt cnt rollover flag for EP0 out*/
	unsigned stp_rollover;

#ifdef DWC_UTE_CFI
	/* The buffer mode */
	data_buffer_mode_e buff_mode;

	/* The chain of DMA descriptors.
	 * MAX_DMA_DESCS_PER_EP will be allocated for each active EP.
	 */
	dwc_otg_dma_desc_t *descs;

	/* The DMA address of the descriptors chain start */
	dma_addr_t descs_dma_addr;
	/** This variable stores the length of the last enqueued request */
	uint32_t cfi_req_len;
#endif/* DWC_UTE_CFI */

/** Max DMA Descriptor count for any EP */
#define MAX_DMA_DESC_CNT 256
	/** Allocated DMA Desc count */
	uint32_t desc_cnt;

	/** bInterval */
	uint32_t bInterval;
	/** Next frame num to setup next ISOC transfer */
	uint32_t frame_num;
	/** Indicates SOF number overrun in DSTS */
	uint8_t frm_overrun;

#ifdef DWC_UTE_PER_IO
	/** Next frame num for which will be setup DMA Desc */
	uint32_t xiso_frame_num;
	/** bInterval */
	uint32_t xiso_bInterval;
	/** Count of currently active transfers - shall be either 0 or 1 */
	int xiso_active_xfers;
	int xiso_queued_xfers;
#endif
#ifdef DWC_EN_ISOC
	/**
	 * Variables specific for ISOC EPs
	 *
	 */
	/** DMA addresses of ISOC buffers */
	dwc_dma_t dma_addr0;
	dwc_dma_t dma_addr1;

	dwc_dma_t iso_dma_desc_addr;
	dwc_otg_dev_dma_desc_t *iso_desc_addr;

	/** pointer to the transfer buffers */
	uint8_t *xfer_buff0;
	uint8_t *xfer_buff1;

	/** number of ISOC Buffer is processing */
	uint32_t proc_buf_num;
	/** Interval of ISOC Buffer processing */
	uint32_t buf_proc_intrvl;
	/** Data size for regular frame */
	uint32_t data_per_frame;

	/* todo - pattern data support is to be implemented in the future */
	/** Data size for pattern frame */
	uint32_t data_pattern_frame;
	/** Frame number of pattern data */
	uint32_t sync_frame;

	/** bInterval */
	uint32_t bInterval;
	/** ISO Packet number per frame */
	uint32_t pkt_per_frm;
	/** Next frame num for which will be setup DMA Desc */
	uint32_t next_frame;
	/** Number of packets per buffer processing */
	uint32_t pkt_cnt;
	/** Info for all isoc packets */
	iso_pkt_info_t *pkt_info;
	/** current pkt number */
	uint32_t cur_pkt;
	/** current pkt number */
	uint8_t *cur_pkt_addr;
	/** current pkt number */
	uint32_t cur_pkt_dma_addr;
#endif				/* DWC_EN_ISOC */

/** @} */
} dwc_ep_t;

/*
 * Reasons for halting a host channel.
 */
typedef enum dwc_otg_halt_status {
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
} dwc_otg_halt_status_e;

/**
 * Host channel descriptor. This structure represents the state of a single
 * host channel when acting in host mode. It contains the data items needed to
 * transfer packets to an endpoint via a host channel.
 */
typedef struct dwc_hc {
	/** Host channel number used for register address lookup */
	uint8_t hc_num;

	/** Device to access */
	unsigned dev_addr:7;

	/** EP to access */
	unsigned ep_num:4;

	/** EP direction. 0: OUT, 1: IN */
	unsigned ep_is_in:1;

	/**
	 * EP speed.
	 * One of the following values:
	 *	- DWC_OTG_EP_SPEED_LOW
	 *	- DWC_OTG_EP_SPEED_FULL
	 *	- DWC_OTG_EP_SPEED_HIGH
	 */
	unsigned speed:2;
#define DWC_OTG_EP_SPEED_LOW	0
#define DWC_OTG_EP_SPEED_FULL	1
#define DWC_OTG_EP_SPEED_HIGH	2

	/**
	 * Endpoint type.
	 * One of the following values:
	 *	- DWC_OTG_EP_TYPE_CONTROL: 0
	 *	- DWC_OTG_EP_TYPE_ISOC: 1
	 *	- DWC_OTG_EP_TYPE_BULK: 2
	 *	- DWC_OTG_EP_TYPE_INTR: 3
	 */
	unsigned ep_type:2;

	/** Max packet size in bytes */
	unsigned max_packet:11;

	/**
	 * PID for initial transaction.
	 * 0: DATA0,<br>
	 * 1: DATA2,<br>
	 * 2: DATA1,<br>
	 * 3: MDATA (non-Control EP),
	 *	  SETUP (Control EP)
	 */
	unsigned data_pid_start:2;
#define DWC_OTG_HC_PID_DATA0 0
#define DWC_OTG_HC_PID_DATA2 1
#define DWC_OTG_HC_PID_DATA1 2
#define DWC_OTG_HC_PID_MDATA 3
#define DWC_OTG_HC_PID_SETUP 3

	/** Number of periodic transactions per (micro)frame */
	unsigned multi_count:2;

	/** @name Transfer State */
	/** @{ */

	/** Pointer to the current transfer buffer position. */
	uint8_t *xfer_buff;
	/**
	 * In Buffer DMA mode this buffer will be used
	 * if xfer_buff is not DWORD aligned.
	 */
	dwc_dma_t align_buff;
	/** Total number of bytes to transfer. */
	uint32_t xfer_len;
	/** Number of bytes transferred so far. */
	uint32_t xfer_count;
	/** Packet count at start of transfer.*/
	uint16_t start_pkt_count;

	/**
	 * Flag to indicate whether the transfer has been started. Set to 1 if
	 * it has been started, 0 otherwise.
	 */
	uint8_t xfer_started;

	/**
	 * Set to 1 to indicate that a PING request should be issued on this
	 * channel. If 0, process normally.
	 */
	uint8_t do_ping;

	/**
	 * Set to 1 to indicate that the error count for this transaction is
	 * non-zero. Set to 0 if the error count is 0.
	 */
	uint8_t error_state;

	/**
	 * Set to 1 to indicate that this channel should be halted the next
	 * time a request is queued for the channel. This is necessary in
	 * slave mode if no request queue space is available when an attempt
	 * is made to halt the channel.
	 */
	uint8_t halt_on_queue;

	/**
	 * Set to 1 if the host channel has been halted, but the core is not
	 * finished flushing queued requests. Otherwise 0.
	 */
	uint8_t halt_pending;

	/**
	 * Reason for halting the host channel.
	 */
	dwc_otg_halt_status_e halt_status;

	/*
	 * Split settings for the host channel
	 */
	uint8_t do_split;		   /**< Enable split for the channel */
	uint8_t complete_split;	   /**< Enable complete split */
	uint8_t csplit_nak;
	uint8_t hub_addr;		   /**< Address of high speed hub */

	uint8_t port_addr;		   /**< Port of the low/full speed device */
	/** Split transaction position
	 * One of the following values:
	 *	  - DWC_HCSPLIT_XACTPOS_MID
	 *	  - DWC_HCSPLIT_XACTPOS_BEGIN
	 *	  - DWC_HCSPLIT_XACTPOS_END
	 *	  - DWC_HCSPLIT_XACTPOS_ALL */
	uint8_t xact_pos;

	/** Set when the host channel does a short read. */
	uint8_t short_read;

	/**
	 * Number of requests issued for this channel since it was assigned to
	 * the current transfer (not counting PINGs).
	 */
	uint8_t requests;

	/**
	 * Queue Head for the transfer being processed by this channel.
	 */
	struct dwc_otg_qh *qh;

	/** @} */

	/** Entry in list of host channels. */
	 DWC_CIRCLEQ_ENTRY(dwc_hc) hc_list_entry;

	/** @name Descriptor DMA support */
	/** @{ */

	/** Number of Transfer Descriptors */
	uint16_t ntd;

	/** Descriptor List DMA address */
	dwc_dma_t desc_list_addr;

	/** Scheduling micro-frame bitmap. */
	uint8_t schinfo;

	/** @} */
} dwc_hc_t;

/**
 * The following parameters may be specified when starting the module. These
 * parameters define how the DWC_otg controller should be configured.
 */
typedef struct dwc_otg_core_params {
	int32_t opt;

	/**
	 * Specifies the OTG capabilities. The driver will automatically
	 * detect the value for this parameter if none is specified.
	 * 0 - HNP and SRP capable (default)
	 * 1 - SRP Only capable
	 * 2 - No HNP/SRP capable
	 */
	int32_t otg_cap;

	/**
	 * Specifies whether to use slave or DMA mode for accessing the data
	 * FIFOs. The driver will automatically detect the value for this
	 * parameter if none is specified.
	 * 0 - Slave
	 * 1 - DMA (default, if available)
	 */
	int32_t dma_enable;

	/**
	 * When DMA mode is enabled specifies whether to use address DMA or DMA
	 * Descriptor mode for accessing the data FIFOs in device mode. The driver
	 * will automatically detect the value for this if none is specified.
	 * 0 - address DMA
	 * 1 - DMA Descriptor(default, if available)
	 */
	int32_t dma_desc_enable;
	/** The DMA Burst size (applicable only for External DMA
	 * Mode). 1, 4, 8 16, 32, 64, 128, 256 (default 32)
	 */
	int32_t dma_burst_size;	/* Translate this to GAHBCFG values */

	/**
	 * Specifies the maximum speed of operation in host and device mode.
	 * The actual speed depends on the speed of the attached device and
	 * the value of phy_type. The actual speed depends on the speed of the
	 * attached device.
	 * 0 - High Speed (default)
	 * 1 - Full Speed
	 */
	int32_t speed;
	/** Specifies whether low power mode is supported when attached
	 *	to a Full Speed or Low Speed device in host mode.
	 * 0 - Don't support low power mode (default)
	 * 1 - Support low power mode
	 */
	int32_t host_support_fs_ls_low_power;

	/** Specifies the PHY clock rate in low power mode when connected to a
	 * Low Speed device in host mode. This parameter is applicable only if
	 * HOST_SUPPORT_FS_LS_LOW_POWER is enabled. If PHY_TYPE is set to FS
	 * then defaults to 6 MHZ otherwise 48 MHZ.
	 *
	 * 0 - 48 MHz
	 * 1 - 6 MHz
	 */
	int32_t host_ls_low_power_phy_clk;

	/**
	 * 0 - Use cC FIFO size parameters
	 * 1 - Allow dynamic FIFO sizing (default)
	 */
	int32_t enable_dynamic_fifo;

	/** Total number of 4-byte words in the data FIFO memory. This
	 * memory includes the Rx FIFO, non-periodic Tx FIFO, and periodic
	 * Tx FIFOs.
	 * 32 to 32768 (default 8192)
	 * Note: The total FIFO memory depth in the FPGA configuration is 8192.
	 */
	int32_t data_fifo_size;

	/** Number of 4-byte words in the Rx FIFO in device mode when dynamic
	 * FIFO sizing is enabled.
	 * 16 to 32768 (default 1064)
	 */
	int32_t dev_rx_fifo_size;

	/** Number of 4-byte words in the non-periodic Tx FIFO in device mode
	 * when dynamic FIFO sizing is enabled.
	 * 16 to 32768 (default 1024)
	 */
	int32_t dev_nperio_tx_fifo_size;

	/** Number of 4-byte words in each of the periodic Tx FIFOs in device
	 * mode when dynamic FIFO sizing is enabled.
	 * 4 to 768 (default 256)
	 */
	uint32_t dev_perio_tx_fifo_size[MAX_PERIO_FIFOS];

	/** Number of 4-byte words in the Rx FIFO in host mode when dynamic
	 * FIFO sizing is enabled.
	 * 16 to 32768 (default 1024)
	 */
	int32_t host_rx_fifo_size;

	/** Number of 4-byte words in the non-periodic Tx FIFO in host mode
	 * when Dynamic FIFO sizing is enabled in the core.
	 * 16 to 32768 (default 1024)
	 */
	int32_t host_nperio_tx_fifo_size;

	/** Number of 4-byte words in the host periodic Tx FIFO when dynamic
	 * FIFO sizing is enabled.
	 * 16 to 32768 (default 1024)
	 */
	int32_t host_perio_tx_fifo_size;

	/** The maximum transfer size supported in bytes.
	 * 2047 to 65,535  (default 65,535)
	 */
	int32_t max_transfer_size;

	/** The maximum number of packets in a transfer.
	 * 15 to 511  (default 511)
	 */
	int32_t max_packet_count;

	/** The number of host channel registers to use.
	 * 1 to 16 (default 12)
	 * Note: The FPGA configuration supports a maximum of 12 host channels.
	 */
	int32_t host_channels;

	/** The number of endpoints in addition to EP0 available for device
	 * mode operations.
	 * 1 to 15 (default 6 IN and OUT)
	 * Note: The FPGA configuration supports a maximum of 6 IN and OUT
	 * endpoints in addition to EP0.
	 */
	int32_t dev_endpoints;

		/**
		 * Specifies the type of PHY interface to use. By default, the driver
		 * will automatically detect the phy_type.
		 *
		 * 0 - Full Speed PHY
		 * 1 - UTMI+ (default)
		 * 2 - ULPI
		 */
	int32_t phy_type;

	/**
	 * Specifies the UTMI+ Data Width. This parameter is
	 * applicable for a PHY_TYPE of UTMI+ or ULPI. (For a ULPI
	 * PHY_TYPE, this parameter indicates the data width between
	 * the MAC and the ULPI Wrapper.) Also, this parameter is
	 * applicable only if the OTG_HSPHY_WIDTH cC parameter was set
	 * to "8 and 16 bits", meaning that the core has been
	 * configured to work at either data path width.
	 *
	 * 8 or 16 bits (default 16)
	 */
	int32_t phy_utmi_width;

	/**
	 * Specifies whether the ULPI operates at double or single
	 * data rate. This parameter is only applicable if PHY_TYPE is
	 * ULPI.
	 *
	 * 0 - single data rate ULPI interface with 8 bit wide data
	 * bus (default)
	 * 1 - double data rate ULPI interface with 4 bit wide data
	 * bus
	 */
	int32_t phy_ulpi_ddr;

	/**
	 * Specifies whether to use the internal or external supply to
	 * drive the vbus with a ULPI phy.
	 */
	int32_t phy_ulpi_ext_vbus;

	/**
	 * Specifies whether to use the I2Cinterface for full speed PHY. This
	 * parameter is only applicable if PHY_TYPE is FS.
	 * 0 - No (default)
	 * 1 - Yes
	 */
	int32_t i2c_enable;

	int32_t ulpi_fs_ls;

	int32_t ts_dline;

	/**
	 * Specifies whether dedicated transmit FIFOs are
	 * enabled for non periodic IN endpoints in device mode
	 * 0 - No
	 * 1 - Yes
	 */
	int32_t en_multiple_tx_fifo;

	/** Number of 4-byte words in each of the Tx FIFOs in device
	 * mode when dynamic FIFO sizing is enabled.
	 * 4 to 768 (default 256)
	 */
	uint32_t dev_tx_fifo_size[MAX_TX_FIFOS];

	/** Thresholding enable flag-
	 * bit 0 - enable non-ISO Tx thresholding
	 * bit 1 - enable ISO Tx thresholding
	 * bit 2 - enable Rx thresholding
	 */
	uint32_t thr_ctl;

	/** Thresholding length for Tx
	 *	FIFOs in 32 bit DWORDs
	 */
	uint32_t tx_thr_length;

	/** Thresholding length for Rx
	 *	FIFOs in 32 bit DWORDs
	 */
	uint32_t rx_thr_length;

	/**
	 * Specifies whether LPM (Link Power Management) support is enabled
	 */
	int32_t lpm_enable;

	/**
	* Specifies whether LPM Errata (Link Power Management) support is enabled
	*/
	int32_t besl_enable;

	/**
	* Specifies the baseline besl value
	*/
	int32_t baseline_besl;

	/**
	* Specifies the deep besl value
	*/
	int32_t deep_besl;
	/** Per Transfer Interrupt
	 *	mode enable flag
	 * 1 - Enabled
	 * 0 - Disabled
	 */
	int32_t pti_enable;

	/** Multi Processor Interrupt
	 *	mode enable flag
	 * 1 - Enabled
	 * 0 - Disabled
	 */
	int32_t mpi_enable;

	/** IS_USB Capability
	 * 1 - Enabled
	 * 0 - Disabled
	 */
	int32_t ic_usb_cap;

	/** AHB Threshold Ratio
	 * 2'b00 AHB Threshold = 	MAC Threshold
	 * 2'b01 AHB Threshold = 1/2 	MAC Threshold
	 * 2'b10 AHB Threshold = 1/4	MAC Threshold
	 * 2'b11 AHB Threshold = 1/8	MAC Threshold
	 */
	int32_t ahb_thr_ratio;

	/** ADP Support
	 * 1 - Enabled
	 * 0 - Disabled
	 */
	int32_t adp_supp_enable;

	/** HFIR Reload Control
	 * 0 - The HFIR cannot be reloaded dynamically.
	 * 1 - Allow dynamic reloading of the HFIR register during runtime.
	 */
	int32_t reload_ctl;

	/** DCFG: Enable device Out NAK
	 * 0 - The core does not set NAK after Bulk Out transfer complete.
	 * 1 - The core sets NAK after Bulk OUT transfer complete.
	 */
	int32_t dev_out_nak;

	/** DCFG: Enable Continue on BNA
	 * After receiving BNA interrupt the core disables the endpoint,when the
	 * endpoint is re-enabled by the application the core starts processing
	 * 0 - from the DOEPDMA descriptor
	 * 1 - from the descriptor which received the BNA.
	 */
	int32_t cont_on_bna;

	/** GAHBCFG: AHB Single Support
	 * This bit when programmed supports SINGLE transfers for remainder
	 * data in a transfer for DMA mode of operation.
	 * 0 - in this case the remainder data will be sent using INCR burst size.
	 * 1 - in this case the remainder data will be sent using SINGLE burst size.
	 */
	int32_t ahb_single;

	/** Core Power down mode
	 * 0 - No Power Down is enabled
	 * 1 - Reserved
	 * 2 - Complete Power Down (Hibernation)
	 */
	int32_t power_down;

	/** OTG revision supported
	 * 0 - OTG 1.3 revision
	 * 1 - OTG 2.0 revision
	 */
	int32_t otg_ver;

} dwc_otg_core_params_t;

#ifdef DEBUG
struct dwc_otg_core_if;
typedef struct hc_xfer_info {
	struct dwc_otg_core_if *core_if;
	dwc_hc_t *hc;
} hc_xfer_info_t;
#endif

typedef struct ep_xfer_info {
	struct dwc_otg_core_if *core_if;
	dwc_ep_t *ep;
	uint8_t state;
} ep_xfer_info_t;
/*
 * Device States
 */
typedef enum dwc_otg_lx_state {
	/** On state */
	DWC_OTG_L0,
	/** LPM sleep state*/
	DWC_OTG_L1,
	/** USB suspend state*/
	DWC_OTG_L2,
	/** Off state*/
	DWC_OTG_L3
} dwc_otg_lx_state_e;

struct dwc_otg_global_regs_backup {
	uint32_t gotgctl_local;
	uint32_t gintmsk_local;
	uint32_t gahbcfg_local;
	uint32_t gusbcfg_local;
	uint32_t grxfsiz_local;
	uint32_t gnptxfsiz_local;
#ifdef CONFIG_USB_DWC_OTG_LPM
	uint32_t glpmcfg_local;
#endif
	uint32_t gi2cctl_local;
	uint32_t hptxfsiz_local;
	uint32_t pcgcctl_local;
	uint32_t gdfifocfg_local;
	uint32_t dtxfsiz_local[MAX_TX_FIFOS];
	uint32_t gpwrdn_local;
	uint32_t xhib_pcgcctl;
	uint32_t xhib_gpwrdn;
};

struct dwc_otg_host_regs_backup {
	uint32_t hcfg_local;
	uint32_t haintmsk_local;
	uint32_t hcintmsk_local[MAX_EPS_CHANNELS];
	uint32_t hprt0_local;
	uint32_t hfir_local;
};

struct dwc_otg_dev_regs_backup {
	uint32_t dcfg;
	uint32_t dctl;
	uint32_t daintmsk;
	uint32_t diepmsk;
	uint32_t doepmsk;
	uint32_t diepctl[MAX_EPS_CHANNELS];
	uint32_t dieptsiz[MAX_EPS_CHANNELS];
	uint32_t diepdma[MAX_EPS_CHANNELS];
};
/**
 * The <code>dwc_otg_core_if</code> structure contains information needed to manage
 * the DWC_otg controller acting in either host or device mode. It
 * represents the programming view of the controller as a whole.
 */
struct dwc_otg_core_if {
	/** Parameters that define how the core should be configured.*/
	dwc_otg_core_params_t *core_params;

	/** Core Global registers starting at offset 000h. */
	dwc_otg_core_global_regs_t *core_global_regs;

	/** Device-specific information */
	dwc_otg_dev_if_t *dev_if;
	/** Host-specific information */
	dwc_otg_host_if_t *host_if;

	/** Value from SNPSID register */
	uint32_t snpsid;

	/** The DWC otg device pointer. */
	struct dwc_otg_device *otg_dev;

	/*
	 * Set to 1 if the core PHY interface bits in USBCFG have been
	 * initialized.
	 */
	uint8_t phy_init_done;

	/*
	 * SRP Success flag, set by srp success interrupt in FS I2C mode
	 */
	uint8_t srp_success;
	uint8_t srp_timer_started;
	/** Timer for SRP. If it expires before SRP is successful
	 * clear the SRP. */
	dwc_timer_t *srp_timer;

	uint8_t usb_mode;
#define USB_MODE_NORMAL (0)
#define USB_MODE_FORCE_HOST (1)
#define USB_MODE_FORCE_DEVICE (2)

	/* Indicates need to force a host channel halt */
	bool hc_halt_quirk;

	/* Indicate USB get VBUS 5V from PMIC(e.g. rk81x) */
	bool pmic_vbus;

#ifdef DWC_DEV_SRPCAP
	/* This timer is needed to power on the hibernated host core if SRP is not
	 * initiated on connected SRP capable device for limited period of time
	 */
	uint8_t pwron_timer_started;
	dwc_timer_t *pwron_timer;
#endif
	/* Common configuration information */
	/** Power and Clock Gating Control Register */
	volatile uint32_t *pcgcctl;
#define DWC_OTG_PCGCCTL_OFFSET 0xE00

	/** Push/pop addresses for endpoints or host channels.*/
	uint32_t *data_fifo[MAX_EPS_CHANNELS];
#define DWC_OTG_DATA_FIFO_OFFSET 0x1000
#define DWC_OTG_DATA_FIFO_SIZE 0x1000

	/** Total RAM for FIFOs (Bytes) */
	uint16_t total_fifo_size;
	/** Size of Rx FIFO (Bytes) */
	uint16_t rx_fifo_size;
	/** Size of Non-periodic Tx FIFO (Bytes) */
	uint16_t nperio_tx_fifo_size;

	/** 1 if DMA is enabled, 0 otherwise. */
	uint8_t dma_enable;

	/** 1 if DMA descriptor is enabled, 0 otherwise. */
	uint8_t dma_desc_enable;

	/** 1 if PTI Enhancement mode is enabled, 0 otherwise. */
	uint8_t pti_enh_enable;

	/** 1 if MPI Enhancement mode is enabled, 0 otherwise. */
	uint8_t multiproc_int_enable;

	/** 1 if dedicated Tx FIFOs are enabled, 0 otherwise. */
	uint8_t en_multiple_tx_fifo;

	/** Set to 1 if multiple packets of a high-bandwidth transfer is in
	 * process of being queued */
	uint8_t queuing_high_bandwidth;

	/** Hardware Configuration -- stored here for convenience.*/
	hwcfg1_data_t hwcfg1;
	hwcfg2_data_t hwcfg2;
	hwcfg3_data_t hwcfg3;
	hwcfg4_data_t hwcfg4;
	fifosize_data_t hptxfsiz;

	/** Host and Device Configuration -- stored here for convenience.*/
	hcfg_data_t hcfg;
	dcfg_data_t dcfg;

	/** The operational State, during transations
	 * (a_host>>a_peripherial and b_device=>b_host) this may not
	 * match the core but allows the software to determine
	 * transitions.
	 */
	uint8_t op_state;

	/** Test mode for PET testing */
	uint8_t test_mode;

	/**
	 * Set to 1 if the HCD needs to be restarted on a session request
	 * interrupt. This is required if no connector ID status change has
	 * occurred since the HCD was last disconnected.
	 */
	uint8_t restart_hcd_on_session_req;

	/** HCD callbacks */
	/** A-Device is a_host */
#define A_HOST		(1)
	/** A-Device is a_suspend */
#define A_SUSPEND	(2)
	/** A-Device is a_peripherial */
#define A_PERIPHERAL	(3)
	/** B-Device is operating as a Peripheral. */
#define B_PERIPHERAL	(4)
	/** B-Device is operating as a Host. */
#define B_HOST		(5)

	/** HCD callbacks */
	struct dwc_otg_cil_callbacks *hcd_cb;
	void *hcd_cb_p;
	/** PCD callbacks */
	struct dwc_otg_cil_callbacks *pcd_cb;

	/** Device mode Periodic Tx FIFO Mask */
	uint32_t p_tx_msk;
	/** Device mode Periodic Tx FIFO Mask */
	uint32_t tx_msk;

	/** Workqueue object used for handling several interrupts */
	dwc_workq_t *wq_otg;

	/** Tasklet used for handling "Wakeup Detected" Interrupt*/
	dwc_tasklet_t *wkp_tasklet;
	/** This arrays used for debug purposes for DEV OUT NAK enhancement */
	uint32_t start_doeptsiz_val[MAX_EPS_CHANNELS];
	ep_xfer_info_t ep_xfer_info[MAX_EPS_CHANNELS];
	dwc_timer_t *ep_xfer_timer[MAX_EPS_CHANNELS];
#ifdef DEBUG
	uint32_t start_hcchar_val[MAX_EPS_CHANNELS];

	hc_xfer_info_t hc_xfer_info[MAX_EPS_CHANNELS];
	dwc_timer_t *hc_xfer_timer[MAX_EPS_CHANNELS];

	uint32_t hfnum_7_samples;
	uint64_t hfnum_7_frrem_accum;
	uint32_t hfnum_0_samples;
	uint64_t hfnum_0_frrem_accum;
	uint32_t hfnum_other_samples;
	uint64_t hfnum_other_frrem_accum;
#endif

#ifdef DWC_UTE_CFI
	uint16_t pwron_rxfsiz;
	uint16_t pwron_gnptxfsiz;
	uint16_t pwron_txfsiz[15];

	uint16_t init_rxfsiz;
	uint16_t init_gnptxfsiz;
	uint16_t init_txfsiz[15];
#endif

	/** Lx state of device */
	dwc_otg_lx_state_e lx_state;

	/** Saved Core Global registers */
	struct dwc_otg_global_regs_backup *gr_backup;
	/** Saved Host registers */
	struct dwc_otg_host_regs_backup *hr_backup;
	/** Saved Device registers */
	struct dwc_otg_dev_regs_backup *dr_backup;

	/** Power Down Enable */
	uint32_t power_down;

	/** ADP support Enable */
	uint32_t adp_enable;

	/** ADP structure object */
	dwc_otg_adp_t adp;

	/** hibernation/suspend flag */
	int hibernation_suspend;

	/** Device mode extended hibernation flag */
	int xhib;

	/** OTG revision supported */
	uint32_t otg_ver;

	/** OTG status flag used for HNP polling */
	uint8_t otg_sts;

	/** Pointer to either hcd->lock or pcd->lock */
	dwc_spinlock_t *lock;

	/** Start predict NextEP based on Learning Queue if equal 1,
	 * also used as counter of disabled NP IN EP's */
	uint8_t start_predict;

	/** NextEp sequence, including EP0: nextep_seq[] = EP if non-periodic and
	 * active, 0xff otherwise */
	uint8_t nextep_seq[MAX_EPS_CHANNELS];

	/** Index of fisrt EP in nextep_seq array which should be re-enabled **/
	uint8_t first_in_nextep_seq;

	/** Frame number while entering to ISR - needed for ISOCs **/
	uint32_t frame_num;

	/** Flag to not perform ADP probing if IDSTS event happened */
	uint8_t stop_adpprb;

};

#ifdef DEBUG
/*
 * This function is called when transfer is timed out.
 */
extern void hc_xfer_timeout(void *ptr);
#endif

/*
 * This function is called when transfer is timed out on endpoint.
 */
extern void ep_xfer_timeout(void *ptr);

/*
 * The following functions are functions for works
 * using during handling some interrupts
 */
extern void w_conn_id_status_change(void *p);

extern void w_wakeup_detected(void *data);

/** Saves global register values into system memory. */
extern int dwc_otg_save_global_regs(dwc_otg_core_if_t *core_if);
/** Saves device register values into system memory. */
extern int dwc_otg_save_dev_regs(dwc_otg_core_if_t *core_if);
/** Saves host register values into system memory. */
extern int dwc_otg_save_host_regs(dwc_otg_core_if_t *core_if);
/** Restore global register values. */
extern int dwc_otg_restore_global_regs(dwc_otg_core_if_t *core_if);
/** Restore host register values. */
extern int dwc_otg_restore_host_regs(dwc_otg_core_if_t *core_if, int reset);
/** Restore device register values. */
extern int dwc_otg_restore_dev_regs(dwc_otg_core_if_t *core_if,
				    int rem_wakeup);
extern int restore_lpm_i2c_regs(dwc_otg_core_if_t *core_if);
extern int restore_essential_regs(dwc_otg_core_if_t *core_if, int rmode,
				  int is_host);

extern int dwc_otg_host_hibernation_restore(dwc_otg_core_if_t *core_if,
					    int restore_mode, int reset);
extern int dwc_otg_device_hibernation_restore(dwc_otg_core_if_t *core_if,
					      int rem_wakeup, int reset);

/*
 * The following functions support initialization of the CIL driver component
 * and the DWC_otg controller.
 */
extern void dwc_otg_core_host_init(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_core_dev_init(dwc_otg_core_if_t *_core_if);

/** @name Device CIL Functions
 * The following functions support managing the DWC_otg controller in device
 * mode.
 */
/**@{*/
extern void dwc_otg_wakeup(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_read_setup_packet(dwc_otg_core_if_t *_core_if,
				      uint32_t *_dest);
extern uint32_t dwc_otg_get_frame_number(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_ep0_activate(dwc_otg_core_if_t *_core_if, dwc_ep_t *_ep);
extern void dwc_otg_ep_activate(dwc_otg_core_if_t *_core_if, dwc_ep_t *_ep);
extern void dwc_otg_ep_deactivate(dwc_otg_core_if_t *_core_if, dwc_ep_t *_ep);
extern void dwc_otg_ep_start_transfer(dwc_otg_core_if_t *_core_if,
				      dwc_ep_t *_ep);
extern void dwc_otg_ep_start_zl_transfer(dwc_otg_core_if_t *_core_if,
					 dwc_ep_t *_ep);
extern void dwc_otg_ep0_start_transfer(dwc_otg_core_if_t *_core_if,
				       dwc_ep_t *_ep);
extern void dwc_otg_ep0_continue_transfer(dwc_otg_core_if_t *_core_if,
					  dwc_ep_t *_ep);
extern void dwc_otg_ep_write_packet(dwc_otg_core_if_t *_core_if,
				    dwc_ep_t *_ep, int _dma);
extern void dwc_otg_ep_set_stall(dwc_otg_core_if_t *_core_if, dwc_ep_t *_ep);
extern void dwc_otg_ep_clear_stall(dwc_otg_core_if_t *_core_if,
				   dwc_ep_t *_ep);
extern void dwc_otg_enable_device_interrupts(dwc_otg_core_if_t *_core_if);

#ifdef DWC_EN_ISOC
extern void dwc_otg_iso_ep_start_frm_transfer(dwc_otg_core_if_t *core_if,
					      dwc_ep_t *ep);
extern void dwc_otg_iso_ep_start_buf_transfer(dwc_otg_core_if_t *core_if,
					      dwc_ep_t *ep);
#endif /* DWC_EN_ISOC */
/**@}*/

/** @name Host CIL Functions
 * The following functions support managing the DWC_otg controller in host
 * mode.
 */
/**@{*/
extern void dwc_otg_hc_init(dwc_otg_core_if_t *_core_if, dwc_hc_t *_hc);
extern void dwc_otg_hc_halt(dwc_otg_core_if_t *_core_if,
			    dwc_hc_t *_hc, dwc_otg_halt_status_e _halt_status);
extern void dwc_otg_hc_cleanup(dwc_otg_core_if_t *_core_if, dwc_hc_t *_hc);
extern void dwc_otg_hc_start_transfer(dwc_otg_core_if_t *_core_if,
				      dwc_hc_t *_hc);
extern int dwc_otg_hc_continue_transfer(dwc_otg_core_if_t *_core_if,
					dwc_hc_t *_hc);
extern void dwc_otg_hc_do_ping(dwc_otg_core_if_t *_core_if, dwc_hc_t *_hc);
extern void dwc_otg_hc_write_packet(dwc_otg_core_if_t *_core_if,
				    dwc_hc_t *_hc);
extern void dwc_otg_enable_host_interrupts(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_disable_host_interrupts(dwc_otg_core_if_t *_core_if);

extern void dwc_otg_hc_start_transfer_ddma(dwc_otg_core_if_t *core_if,
					   dwc_hc_t *hc);

extern uint32_t calc_frame_interval(dwc_otg_core_if_t *core_if);
extern int dwc_otg_check_haps_status(dwc_otg_core_if_t *core_if);

/* Macro used to clear one channel interrupt */
#define clear_hc_int(_hc_regs_, _intr_) \
do { \
	hcint_data_t hcint_clear = {.d32 = 0}; \
	hcint_clear.b._intr_ = 1; \
	DWC_WRITE_REG32(&(_hc_regs_)->hcint, hcint_clear.d32); \
} while (0)

/*
 * Macro used to disable one channel interrupt. Channel interrupts are
 * disabled when the channel is halted or released by the interrupt handler.
 * There is no need to handle further interrupts of that type until the
 * channel is re-assigned. In fact, subsequent handling may cause crashes
 * because the channel structures are cleaned up when the channel is released.
 */
#define disable_hc_int(_hc_regs_, _intr_) \
do { \
	hcintmsk_data_t hcintmsk = {.d32 = 0}; \
	hcintmsk.b._intr_ = 1; \
	DWC_MODIFY_REG32(&(_hc_regs_)->hcintmsk, hcintmsk.d32, 0); \
} while (0)

/**
 * This function Reads HPRT0 in preparation to modify. It keeps the
 * WC bits 0 so that if they are read as 1, they won't clear when you
 * write it back
 */
static inline uint32_t dwc_otg_read_hprt0(dwc_otg_core_if_t *_core_if)
{
	hprt0_data_t hprt0;
	hprt0.d32 = DWC_READ_REG32(_core_if->host_if->hprt0);
	hprt0.b.prtena = 0;
	hprt0.b.prtconndet = 0;
	hprt0.b.prtenchng = 0;
	hprt0.b.prtovrcurrchng = 0;
	return hprt0.d32;
}

/**@}*/

/** @name Common CIL Functions
 * The following functions support managing the DWC_otg controller in either
 * device or host mode.
 */
/**@{*/

extern void dwc_otg_read_packet(dwc_otg_core_if_t *core_if,
				uint8_t *dest, uint16_t bytes);

extern void dwc_otg_flush_tx_fifo(dwc_otg_core_if_t *_core_if, const int _num);
extern void dwc_otg_flush_rx_fifo(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_core_reset(dwc_otg_core_if_t *_core_if);

/**
 * This function returns the Core Interrupt register.
 */
static inline uint32_t dwc_otg_read_core_intr(dwc_otg_core_if_t *core_if)
{
	uint32_t retval;
	retval = DWC_READ_REG32(&core_if->core_global_regs->gintsts) &
		 DWC_READ_REG32(&core_if->core_global_regs->gintmsk);
	return retval;
}

/**
 * This function returns the OTG Interrupt register.
 */
static inline uint32_t dwc_otg_read_otg_intr(dwc_otg_core_if_t *core_if)
{
	uint32_t retval;
	retval = DWC_READ_REG32(&core_if->core_global_regs->gotgint);
	return retval;
}

/**
 * This function reads the Device All Endpoints Interrupt register and
 * returns the IN endpoint interrupt bits.
 */
static inline uint32_t dwc_otg_read_dev_all_in_ep_intr(dwc_otg_core_if_t
						       *core_if)
{

	uint32_t v;

	if (core_if->multiproc_int_enable) {
		v = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->
				   deachint) & DWC_READ_REG32(&core_if->dev_if->
							      dev_global_regs->
							      deachintmsk);
	} else {
		v = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->daint) &
		    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->daintmsk);
	}
	v &= 0xffff;
	return v;
}

/**
 * This function reads the Device All Endpoints Interrupt register and
 * returns the OUT endpoint interrupt bits.
 */
static inline uint32_t dwc_otg_read_dev_all_out_ep_intr(dwc_otg_core_if_t
							*core_if)
{
	uint32_t v;

	if (core_if->multiproc_int_enable) {
		v = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->
				   deachint) & DWC_READ_REG32(&core_if->dev_if->
							      dev_global_regs->
							      deachintmsk);
	} else {
		v = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->daint) &
		    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->daintmsk);
	}

	v = (v & 0xffff0000) >> 16;
	return v;
}

/**
 * This function returns the Device IN EP Interrupt register
 */
static inline uint32_t dwc_otg_read_dev_in_ep_intr(dwc_otg_core_if_t *core_if,
						   dwc_ep_t *ep)
{
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	uint32_t v, msk, emp;

	if (core_if->multiproc_int_enable) {
		msk =
		    DWC_READ_REG32(&dev_if->dev_global_regs->
				   diepeachintmsk[ep->num]);
		emp =
		    DWC_READ_REG32(&dev_if->dev_global_regs->
				   dtknqr4_fifoemptymsk);
		msk |= ((emp >> ep->num) & 0x1) << 7;
		v = DWC_READ_REG32(&dev_if->in_ep_regs[ep->num]->diepint) & msk;
	} else {
		msk = DWC_READ_REG32(&dev_if->dev_global_regs->diepmsk);
		emp =
		    DWC_READ_REG32(&dev_if->dev_global_regs->
				   dtknqr4_fifoemptymsk);
		msk |= ((emp >> ep->num) & 0x1) << 7;
		v = DWC_READ_REG32(&dev_if->in_ep_regs[ep->num]->diepint) & msk;
	}

	return v;
}

/**
 * This function returns the Device OUT EP Interrupt register
 */
static inline uint32_t dwc_otg_read_dev_out_ep_intr(dwc_otg_core_if_t *
						    _core_if, dwc_ep_t *_ep)
{
	dwc_otg_dev_if_t *dev_if = _core_if->dev_if;
	uint32_t v;
	doepmsk_data_t msk = {.d32 = 0 };

	if (_core_if->multiproc_int_enable) {
		msk.d32 =
		    DWC_READ_REG32(&dev_if->dev_global_regs->
				   doepeachintmsk[_ep->num]);
		if (_core_if->pti_enh_enable) {
			msk.b.pktdrpsts = 1;
		}
		v = DWC_READ_REG32(&dev_if->out_ep_regs[_ep->num]->
				   doepint) & msk.d32;
	} else {
		msk.d32 = DWC_READ_REG32(&dev_if->dev_global_regs->doepmsk);
		if (_core_if->pti_enh_enable) {
			msk.b.pktdrpsts = 1;
		}
		v = DWC_READ_REG32(&dev_if->out_ep_regs[_ep->num]->
				   doepint) & msk.d32;
	}
	return v;
}

/**
 * This function returns the Host All Channel Interrupt register
 */
static inline uint32_t dwc_otg_read_host_all_channels_intr(dwc_otg_core_if_t *
							   _core_if)
{
	uint32_t retval;
	retval = DWC_READ_REG32(&_core_if->host_if->host_global_regs->haint);
	return retval;
}

static inline uint32_t dwc_otg_read_host_channel_intr(dwc_otg_core_if_t *
						      _core_if, dwc_hc_t *_hc)
{
	uint32_t retval;
	retval = DWC_READ_REG32(&_core_if->host_if->hc_regs[_hc->hc_num]->hcint);
	return retval;
}

/**
 * This function returns the mode of the operation, host or device.
 *
 * @return 0 - Device Mode, 1 - Host Mode
 */
static inline uint32_t dwc_otg_mode(dwc_otg_core_if_t *_core_if)
{
	uint32_t retval;
	retval = DWC_READ_REG32(&_core_if->core_global_regs->gintsts) & 0x1;
	return retval;
}

/**@}*/

/**
 * DWC_otg CIL callback structure. This structure allows the HCD and
 * PCD to register functions used for starting and stopping the PCD
 * and HCD for role change on for a DRD.
 */
typedef struct dwc_otg_cil_callbacks {
	/** Start function for role change */
	int (*start) (void *_p);
	/** Stop Function for role change */
	int (*stop) (void *_p);
	/** Disconnect Function for role change */
	int (*disconnect) (void *_p);
	/** Resume/Remote wakeup Function */
	int (*resume_wakeup) (void *_p);
	/** Suspend function */
	int (*suspend) (void *_p);
	/** Session Start (SRP) */
	int (*session_start) (void *_p);
#ifdef CONFIG_USB_DWC_OTG_LPM
	/** Sleep (switch to L0 state) */
	int (*sleep) (void *_p);
#endif
	/** Pointer passed to start() and stop() */
	void *p;
} dwc_otg_cil_callbacks_t;

extern void dwc_otg_cil_register_pcd_callbacks(dwc_otg_core_if_t *_core_if,
					       dwc_otg_cil_callbacks_t *_cb,
					       void *_p);
extern void dwc_otg_cil_register_hcd_callbacks(dwc_otg_core_if_t *_core_if,
					       dwc_otg_cil_callbacks_t *_cb,
					       void *_p);

void dwc_otg_initiate_srp(void *core_if);

/** Start the HCD.  Helper function for using the HCD callbacks.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
static inline void cil_hcd_start(dwc_otg_core_if_t *core_if)
{
	if (core_if->hcd_cb && core_if->hcd_cb->start) {
		core_if->hcd_cb->start(core_if->hcd_cb_p);
	}
}

/** Stop the HCD.  Helper function for using the HCD callbacks.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
static inline void cil_hcd_stop(dwc_otg_core_if_t *core_if)
{
	if (core_if->hcd_cb && core_if->hcd_cb->stop) {
		core_if->hcd_cb->stop(core_if->hcd_cb_p);
	}
}

/** Disconnect the HCD.  Helper function for using the HCD callbacks.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
static inline void cil_hcd_disconnect(dwc_otg_core_if_t *core_if)
{
	if (core_if->hcd_cb && core_if->hcd_cb->disconnect) {
		core_if->hcd_cb->disconnect(core_if->hcd_cb_p);
	}
}

/** Inform the HCD the a New Session has begun.  Helper function for
 * using the HCD callbacks.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
static inline void cil_hcd_session_start(dwc_otg_core_if_t *core_if)
{
	if (core_if->hcd_cb && core_if->hcd_cb->session_start) {
		core_if->hcd_cb->session_start(core_if->hcd_cb_p);
	}
}

#ifdef CONFIG_USB_DWC_OTG_LPM
/**
 * Inform the HCD about LPM sleep.
 * Helper function for using the HCD callbacks.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
static inline void cil_hcd_sleep(dwc_otg_core_if_t *core_if)
{
	if (core_if->hcd_cb && core_if->hcd_cb->sleep) {
		core_if->hcd_cb->sleep(core_if->hcd_cb_p);
	}
}
#endif

/** Resume the HCD.  Helper function for using the HCD callbacks.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
static inline void cil_hcd_resume(dwc_otg_core_if_t *core_if)
{
	if (core_if->hcd_cb && core_if->hcd_cb->resume_wakeup) {
		core_if->hcd_cb->resume_wakeup(core_if->hcd_cb_p);
	}
}

/** Start the PCD.  Helper function for using the PCD callbacks.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
static inline void cil_pcd_start(dwc_otg_core_if_t *core_if)
{
	if (core_if->pcd_cb && core_if->pcd_cb->start) {
		core_if->pcd_cb->start(core_if->pcd_cb->p);
	}
}

/** Stop the PCD.  Helper function for using the PCD callbacks.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
static inline void cil_pcd_stop(dwc_otg_core_if_t *core_if)
{
	if (core_if->pcd_cb && core_if->pcd_cb->stop) {
		core_if->pcd_cb->stop(core_if->pcd_cb->p);
	}
}

/** Suspend the PCD.  Helper function for using the PCD callbacks.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
static inline void cil_pcd_suspend(dwc_otg_core_if_t *core_if)
{
	if (core_if->pcd_cb && core_if->pcd_cb->suspend) {
		core_if->pcd_cb->suspend(core_if->pcd_cb->p);
	}
}

/** Resume the PCD.  Helper function for using the PCD callbacks.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
static inline void cil_pcd_resume(dwc_otg_core_if_t *core_if)
{
	if (core_if->pcd_cb && core_if->pcd_cb->resume_wakeup) {
		core_if->pcd_cb->resume_wakeup(core_if->pcd_cb->p);
	}
}

void dwc_otg_set_force_mode(dwc_otg_core_if_t *core_if, int mode);

#endif
