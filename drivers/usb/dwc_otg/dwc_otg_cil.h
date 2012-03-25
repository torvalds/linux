/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg_ipmate/linux/drivers/dwc_otg_cil.h $
 * $Revision: #12 $
 * $Date: 2007/02/08 $
 * $Change: 792294 $
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

#include "linux/dwc_otg_plat.h"
#include "dwc_otg_regs.h"
#ifdef DEBUG
#include "linux/timer.h"
#endif

struct dwc_otg_device;

/**
 * @file
 * This file contains the interface to the Core Interface Layer.
 */

#define MAX_XFER_LEN	512

/**
 * The <code>dwc_ep</code> structure represents the state of a single
 * endpoint when acting in device mode. It contains the data items
 * needed for an endpoint to be activated and transfer packets.
 */
typedef struct dwc_ep 
{
	/** EP number used for register address lookup */
	uint8_t	 num;
	/** EP direction 0 = OUT */
	unsigned is_in : 1;			  
	/** EP active. */
	unsigned active : 1;

	/** Periodic Tx FIFO # for IN EPs For INTR EP set to 0 to use non-periodic Tx FIFO 
		If dedicated Tx FIFOs are enabled for all IN Eps - Tx FIFO # FOR IN EPs*/
	unsigned tx_fifo_num : 4;  
	/** EP type: 0 - Control, 1 - ISOC,	 2 - BULK,	3 - INTR */
	unsigned type : 2;			  
#define DWC_OTG_EP_TYPE_CONTROL	   0
#define DWC_OTG_EP_TYPE_ISOC	   1
#define DWC_OTG_EP_TYPE_BULK	   2
#define DWC_OTG_EP_TYPE_INTR	   3

	/** DATA start PID for INTR and BULK EP */
	unsigned data_pid_start : 1;  
	/** Frame (even/odd) for ISOC EP */
	unsigned even_odd_frame : 1;  
	/** Max Packet bytes */
	unsigned maxpacket : 11;

	/** @name Transfer state */
	/** @{ */

	/**
	 * Pointer to the beginning of the transfer buffer -- do not modify
	 * during transfer.
	 */
	
	uint32_t dma_addr;

	uint8_t *start_xfer_buff;
	/** pointer to the transfer buffer */
	uint8_t *xfer_buff;			 
	/** Number of bytes to transfer */
	unsigned xfer_len : 19;		  
	/** Number of bytes transferred. */
	unsigned xfer_count : 19;
	/** Sent ZLP */
	unsigned sent_zlp : 1;
	/** Total len for control transfer */
	unsigned total_len : 19;

	/** stall clear flag */
	unsigned stall_clear_flag : 1;
	
	u32 bytes_pending;
	/** @} */
} dwc_ep_t;

/*
 * Reasons for halting a host channel.
 */
typedef enum dwc_otg_halt_status 
{
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
typedef struct dwc_hc 
{
	/** Host channel number used for register address lookup */
	uint8_t	 hc_num;

	/** Device to access */
	unsigned dev_addr : 7;

	/** EP to access */
	unsigned ep_num : 4;

	/** EP direction. 0: OUT, 1: IN */
	unsigned ep_is_in : 1;

	/**
	 * EP speed.
	 * One of the following values:
	 *	- DWC_OTG_EP_SPEED_LOW
	 *	- DWC_OTG_EP_SPEED_FULL
	 *	- DWC_OTG_EP_SPEED_HIGH
	 */
	unsigned speed : 2;
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
	unsigned ep_type : 2;

	/** Max packet size in bytes */
	unsigned max_packet : 11;

	/**
	 * PID for initial transaction.
	 * 0: DATA0,<br>
	 * 1: DATA2,<br>
	 * 2: DATA1,<br>
	 * 3: MDATA (non-Control EP),
	 *	  SETUP (Control EP)
	 */
	unsigned data_pid_start : 2;
#define DWC_OTG_HC_PID_DATA0 0
#define DWC_OTG_HC_PID_DATA2 1
#define DWC_OTG_HC_PID_DATA1 2
#define DWC_OTG_HC_PID_MDATA 3
#define DWC_OTG_HC_PID_SETUP 3

	/** Number of periodic transactions per (micro)frame */
	unsigned multi_count: 2;

	/** @name Transfer State */
	/** @{ */

	/** Pointer to the current transfer buffer position. */
	uint8_t *xfer_buff;
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
	dwc_otg_halt_status_e	halt_status;

	/*
	 * Split settings for the host channel
	 */
	uint8_t do_split;		   /**< Enable split for the channel */
	uint8_t complete_split;	   /**< Enable complete split */
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
	struct list_head	hc_list_entry;
} dwc_hc_t;

/**
 * The following parameters may be specified when starting the module. These
 * parameters define how the DWC_otg controller should be configured.
 * Parameter values are passed to the CIL initialization function
 * dwc_otg_cil_init.
 */
typedef struct dwc_otg_core_params 
{
	int32_t opt;
#define dwc_param_opt_default 1

	/**
	 * Specifies the OTG capabilities. The driver will automatically
	 * detect the value for this parameter if none is specified.
	 * 0 - HNP and SRP capable (default)
	 * 1 - SRP Only capable
	 * 2 - No HNP/SRP capable
	 */
	int32_t otg_cap;
#define DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE 0
#define DWC_OTG_CAP_PARAM_SRP_ONLY_CAPABLE 1
#define DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE 2
#define dwc_param_otg_cap_default DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE

	/**
	 * Specifies whether to use slave or DMA mode for accessing the data
	 * FIFOs. The driver will automatically detect the value for this
	 * parameter if none is specified.
	 * 0 - Slave
	 * 1 - DMA (default, if available)
	 */
	int32_t dma_enable;
#define dwc_param_dma_enable_default 1

	/** The DMA Burst size (applicable only for External DMA
	 * Mode). 1, 4, 8 16, 32, 64, 128, 256 (default 32)
	 */
	int32_t dma_burst_size;	 /* Translate this to GAHBCFG values */
#define dwc_param_dma_burst_size_default 32

	/**
	 * Specifies the maximum speed of operation in host and device mode.
	 * The actual speed depends on the speed of the attached device and
	 * the value of phy_type. The actual speed depends on the speed of the
	 * attached device.
	 * 0 - High Speed (default)
	 * 1 - Full Speed
	 */
	int32_t speed;
#define dwc_param_speed_default 0
#define DWC_SPEED_PARAM_HIGH 0
#define DWC_SPEED_PARAM_FULL 1

	/** Specifies whether low power mode is supported when attached 
	 *	to a Full Speed or Low Speed device in host mode.
	 * 0 - Don't support low power mode (default)
	 * 1 - Support low power mode
	 */
	int32_t host_support_fs_ls_low_power;
#define dwc_param_host_support_fs_ls_low_power_default 0

	/** Specifies the PHY clock rate in low power mode when connected to a
	 * Low Speed device in host mode. This parameter is applicable only if
	 * HOST_SUPPORT_FS_LS_LOW_POWER is enabled.	 If PHY_TYPE is set to FS
	 * then defaults to 6 MHZ otherwise 48 MHZ.
	 *
	 * 0 - 48 MHz
	 * 1 - 6 MHz
	 */
	int32_t host_ls_low_power_phy_clk;
#define dwc_param_host_ls_low_power_phy_clk_default 0
#define DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ 0
#define DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ 1

	/**
	 * 0 - Use cC FIFO size parameters
	 * 1 - Allow dynamic FIFO sizing (default)
	 */
	int32_t enable_dynamic_fifo;
#define dwc_param_enable_dynamic_fifo_default 1

	/** Total number of 4-byte words in the data FIFO memory. This 
	 * memory includes the Rx FIFO, non-periodic Tx FIFO, and periodic 
	 * Tx FIFOs.
	 * 32 to 32768 (default 8192)
	 * Note: The total FIFO memory depth in the FPGA configuration is 8192.
	 */
	int32_t data_fifo_size;
#define dwc_param_data_fifo_size_default 8192

	/** Number of 4-byte words in the Rx FIFO in device mode when dynamic 
	 * FIFO sizing is enabled.
	 * 16 to 32768 (default 1064)
	 */
	int32_t dev_rx_fifo_size;
#define dwc_param_dev_rx_fifo_size_default 1064

	/** Number of 4-byte words in the non-periodic Tx FIFO in device mode 
	 * when dynamic FIFO sizing is enabled.
	 * 16 to 32768 (default 1024)
	 */
	int32_t dev_nperio_tx_fifo_size;
#define dwc_param_dev_nperio_tx_fifo_size_default 1024

	/** Number of 4-byte words in each of the periodic Tx FIFOs in device
	 * mode when dynamic FIFO sizing is enabled.
	 * 4 to 768 (default 256)
	 */
	uint32_t dev_perio_tx_fifo_size[MAX_PERIO_FIFOS];
#define dwc_param_dev_perio_tx_fifo_size_default 256

	/** Number of 4-byte words in the Rx FIFO in host mode when dynamic 
	 * FIFO sizing is enabled.
	 * 16 to 32768 (default 1024)  
	 */
	int32_t host_rx_fifo_size;
//colin
#define dwc_param_host_rx_fifo_size_default 512

		/** Number of 4-byte words in the non-periodic Tx FIFO in host mode 
	 * when Dynamic FIFO sizing is enabled in the core. 
	 * 16 to 32768 (default 1024)
	 */
	int32_t host_nperio_tx_fifo_size;
//colin
#define dwc_param_host_nperio_tx_fifo_size_default 512

	/** Number of 4-byte words in the host periodic Tx FIFO when dynamic 
	 * FIFO sizing is enabled. 
	 * 16 to 32768 (default 1024)
	 */
	int32_t host_perio_tx_fifo_size;
//colin 
#define dwc_param_host_perio_tx_fifo_size_default 512

	/** The maximum transfer size supported in bytes.  
	 * 2047 to 65,535  (default 65,535)
	 */
	int32_t max_transfer_size;
#define dwc_param_max_transfer_size_default 65535

	/** The maximum number of packets in a transfer.  
	 * 15 to 511  (default 511)
	 */
	int32_t max_packet_count;
#define dwc_param_max_packet_count_default 511

	/** The number of host channel registers to use.  
	 * 1 to 16 (default 12) 
	 * Note: The FPGA configuration supports a maximum of 12 host channels.
	 */
	int32_t host_channels;
#define dwc_param_host_channels_default 16

	/** The number of endpoints in addition to EP0 available for device 
	 * mode operations. 
	 * 1 to 15 (default 6 IN and OUT) 
	 * Note: The FPGA configuration supports a maximum of 6 IN and OUT 
	 * endpoints in addition to EP0.
	 */
	int32_t dev_endpoints;
#define dwc_param_dev_endpoints_default 6

		/** 
		 * Specifies the type of PHY interface to use. By default, the driver
		 * will automatically detect the phy_type.
		 * 
		 * 0 - Full Speed PHY
		 * 1 - UTMI+ (default)
		 * 2 - ULPI
		 */
	int32_t phy_type; 
#define DWC_PHY_TYPE_PARAM_FS 0
#define DWC_PHY_TYPE_PARAM_UTMI 1
#define DWC_PHY_TYPE_PARAM_ULPI 2
#define dwc_param_phy_type_default DWC_PHY_TYPE_PARAM_UTMI

	/**
	 * Specifies the UTMI+ Data Width.	This parameter is
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
#define dwc_param_phy_utmi_width_default 16

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
#define dwc_param_phy_ulpi_ddr_default 0

	/**
	 * Specifies whether to use the internal or external supply to 
	 * drive the vbus with a ULPI phy.
	 */
	int32_t phy_ulpi_ext_vbus;
#define DWC_PHY_ULPI_INTERNAL_VBUS 0
#define DWC_PHY_ULPI_EXTERNAL_VBUS 1
#define dwc_param_phy_ulpi_ext_vbus_default DWC_PHY_ULPI_INTERNAL_VBUS

	/**
	 * Specifies whether to use the I2Cinterface for full speed PHY. This
	 * parameter is only applicable if PHY_TYPE is FS.
	 * 0 - No (default)
	 * 1 - Yes
	 */
	int32_t i2c_enable;
#define dwc_param_i2c_enable_default 0

	int32_t ulpi_fs_ls;
#define dwc_param_ulpi_fs_ls_default 0

	int32_t ts_dline;
#define dwc_param_ts_dline_default 0

	/**
	 * Specifies whether dedicated transmit FIFOs are 
	 * enabled for non periodic IN endpoints in device mode
	 * 0 - No 
	 * 1 - Yes
	 */
	 int32_t en_multiple_tx_fifo;
#define dwc_param_en_multiple_tx_fifo_default 1
	
	/** Number of 4-byte words in each of the Tx FIFOs in device
	 * mode when dynamic FIFO sizing is enabled.
	 * 4 to 768 (default 256)
	 */
	uint32_t dev_tx_fifo_size[MAX_TX_FIFOS];	 
#define dwc_param_dev_tx_fifo_size_default 256

	/** Thresholding enable flag- 
	 * bit 0 - enable non-ISO Tx thresholding
	 * bit 1 - enable ISO Tx thresholding
	 * bit 2 - enable Rx thresholding
	 */
	uint32_t thr_ctl;	 
#define dwc_param_thr_ctl_default 0

	/** Thresholding length for Tx 
	 *	FIFOs in 32 bit DWORDs
	 */
	uint32_t tx_thr_length;	 
#define dwc_param_tx_thr_length_default 64

	/** Thresholding length for Rx 
	 *	FIFOs in 32 bit DWORDs
	 */
	uint32_t rx_thr_length;	 
#define dwc_param_rx_thr_length_default 64

} dwc_otg_core_params_t;

#ifdef DEBUG
struct dwc_otg_core_if;
typedef struct hc_xfer_info
{
	struct dwc_otg_core_if	*core_if;
	dwc_hc_t		*hc;
} hc_xfer_info_t;
#endif

/**
 * The <code>dwc_otg_core_if</code> structure contains information needed to manage
 * the DWC_otg controller acting in either host or device mode. It
 * represents the programming view of the controller as a whole.
 */
typedef struct dwc_otg_core_if 
{
	/** Parameters that define how the core should be configured.*/
	dwc_otg_core_params_t	   *core_params;

	/** Core Global registers starting at offset 000h. */
	dwc_otg_core_global_regs_t *core_global_regs;
  
	/** Device-specific information */
	dwc_otg_dev_if_t		   *dev_if;
	/** Host-specific information */
	dwc_otg_host_if_t		   *host_if;

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

	/* DWC OTG WORK MODE
	 * yk@rk 20100513
	 */
	uint8_t usb_mode;
#define USB_MODE_NORMAL (0)
#define USB_MODE_FORCE_HOST (1)
#define USB_MODE_FORCE_DEVICE (2)

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
		
	/** 1 if wakeup in system sleep mode, 0 otherwise */
	uint8_t usb_wakeup;
	
	/** 1 if DMA is enabled, 0 otherwise. */
	uint8_t dma_enable;

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

	/** The operational State, during transations
	 * (a_host>>a_peripherial and b_device=>b_host) this may not
	 * match the core but allows the software to determine
	 * transitions.
	 */
	uint8_t op_state;
		
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
	/** PCD callbacks */
	struct dwc_otg_cil_callbacks *pcd_cb;		 

	/** Device mode Periodic Tx FIFO Mask */
	uint32_t p_tx_msk;
	/** Device mode Periodic Tx FIFO Mask */
	uint32_t tx_msk;
	
#ifdef DEBUG
	uint32_t		start_hcchar_val[MAX_EPS_CHANNELS];

	hc_xfer_info_t		hc_xfer_info[MAX_EPS_CHANNELS];
	struct timer_list	hc_xfer_timer[MAX_EPS_CHANNELS];

	uint32_t		hfnum_7_samples;
	uint64_t		hfnum_7_frrem_accum;
	uint32_t		hfnum_0_samples;
	uint64_t		hfnum_0_frrem_accum;
	uint32_t		hfnum_other_samples;
	uint64_t		hfnum_other_frrem_accum;
#endif	


} dwc_otg_core_if_t;

/*
 * The following functions support initialization of the CIL driver component
 * and the DWC_otg controller.
 */
extern dwc_otg_core_if_t *dwc_otg_cil_init(const uint32_t *_reg_base_addr,
										   dwc_otg_core_params_t *_core_params);
extern void dwc_otg_cil_remove(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_core_init(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_core_host_init(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_core_dev_init(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_enable_global_interrupts( dwc_otg_core_if_t *_core_if );
extern void dwc_otg_disable_global_interrupts( dwc_otg_core_if_t *_core_if );

/** @name Device CIL Functions
 * The following functions support managing the DWC_otg controller in device
 * mode.
 */
/**@{*/
extern void dwc_otg_wakeup(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_read_setup_packet (dwc_otg_core_if_t *_core_if, uint32_t *_dest);
extern uint32_t dwc_otg_get_frame_number(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_ep0_activate(dwc_otg_core_if_t *_core_if, dwc_ep_t *_ep);
extern void dwc_otg_ep_activate(dwc_otg_core_if_t *_core_if, dwc_ep_t *_ep);
extern void dwc_otg_ep_deactivate(dwc_otg_core_if_t *_core_if, dwc_ep_t *_ep);
extern void dwc_otg_ep_start_transfer(dwc_otg_core_if_t *_core_if, dwc_ep_t *_ep);
extern void dwc_otg_ep0_start_transfer(dwc_otg_core_if_t *_core_if, dwc_ep_t *_ep);
extern void dwc_otg_ep0_continue_transfer(dwc_otg_core_if_t *_core_if, dwc_ep_t *_ep);
extern void dwc_otg_ep_write_packet(dwc_otg_core_if_t *_core_if, dwc_ep_t *_ep, int _dma);
extern void dwc_otg_ep_set_stall(dwc_otg_core_if_t *_core_if, dwc_ep_t *_ep);
extern void dwc_otg_ep_clear_stall(dwc_otg_core_if_t *_core_if, dwc_ep_t *_ep);
extern void dwc_otg_enable_device_interrupts(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_dump_dev_registers(dwc_otg_core_if_t *_core_if);
/**@}*/

/** @name Host CIL Functions
 * The following functions support managing the DWC_otg controller in host
 * mode.
 */
/**@{*/
extern void dwc_otg_hc_init(dwc_otg_core_if_t *_core_if, dwc_hc_t *_hc);
extern void dwc_otg_hc_halt(dwc_otg_core_if_t *_core_if,
				dwc_hc_t *_hc,
				dwc_otg_halt_status_e _halt_status);
extern void dwc_otg_hc_cleanup(dwc_otg_core_if_t *_core_if, dwc_hc_t *_hc);
extern void dwc_otg_hc_start_transfer(dwc_otg_core_if_t *_core_if, dwc_hc_t *_hc);
extern int dwc_otg_hc_continue_transfer(dwc_otg_core_if_t *_core_if, dwc_hc_t *_hc);
extern void dwc_otg_hc_do_ping(dwc_otg_core_if_t *_core_if, dwc_hc_t *_hc);
extern void dwc_otg_hc_write_packet(dwc_otg_core_if_t *_core_if, dwc_hc_t *_hc);
extern void dwc_otg_enable_host_interrupts(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_disable_host_interrupts(dwc_otg_core_if_t *_core_if);

/**
 * This function Reads HPRT0 in preparation to modify.	It keeps the
 * WC bits 0 so that if they are read as 1, they won't clear when you
 * write it back 
 */
static inline uint32_t dwc_otg_read_hprt0(dwc_otg_core_if_t *_core_if) 
{
	hprt0_data_t hprt0;
	hprt0.d32 = dwc_read_reg32(_core_if->host_if->hprt0);
	hprt0.b.prtena = 0;
	hprt0.b.prtconndet = 0;
	hprt0.b.prtenchng = 0;
	hprt0.b.prtovrcurrchng = 0;
	return hprt0.d32;
}

extern void dwc_otg_dump_host_registers(dwc_otg_core_if_t *_core_if);
/**@}*/

/** @name Common CIL Functions
 * The following functions support managing the DWC_otg controller in either
 * device or host mode.
 */
/**@{*/

extern void dwc_otg_read_packet(dwc_otg_core_if_t *core_if,
				uint8_t *dest, 
				uint16_t bytes);

extern void dwc_otg_dump_global_registers(dwc_otg_core_if_t *_core_if);

extern void dwc_otg_flush_tx_fifo( dwc_otg_core_if_t *_core_if, 
								   const int _num );
extern void dwc_otg_flush_rx_fifo( dwc_otg_core_if_t *_core_if );
extern void dwc_otg_core_reset( dwc_otg_core_if_t *_core_if );

/**
 * This function returns the Core Interrupt register.
 */
static inline uint32_t dwc_otg_read_core_intr(dwc_otg_core_if_t *_core_if) 
{
	return (dwc_read_reg32(&_core_if->core_global_regs->gintsts) &
		dwc_read_reg32(&_core_if->core_global_regs->gintmsk));
}

/**
 * This function returns the OTG Interrupt register.
 */
static inline uint32_t dwc_otg_read_otg_intr (dwc_otg_core_if_t *_core_if) 
{
	return (dwc_read_reg32 (&_core_if->core_global_regs->gotgint));
}

/**
 * This function reads the Device All Endpoints Interrupt register and
 * returns the IN endpoint interrupt bits.
 */
static inline uint32_t dwc_otg_read_dev_all_in_ep_intr(dwc_otg_core_if_t *_core_if) 
{
	uint32_t v;
	v = dwc_read_reg32(&_core_if->dev_if->dev_global_regs->daint) &
			dwc_read_reg32(&_core_if->dev_if->dev_global_regs->daintmsk);
	return (v & 0xffff);
		
}

/**
 * This function reads the Device All Endpoints Interrupt register and
 * returns the OUT endpoint interrupt bits.
 */
static inline uint32_t dwc_otg_read_dev_all_out_ep_intr(dwc_otg_core_if_t *_core_if) 
{
	uint32_t v;
	v = dwc_read_reg32(&_core_if->dev_if->dev_global_regs->daint) &
			dwc_read_reg32(&_core_if->dev_if->dev_global_regs->daintmsk);
	return ((v & 0xffff0000) >> 16);
}

/**
 * This function returns the Device IN EP Interrupt register
 */
static inline uint32_t dwc_otg_read_dev_in_ep_intr(dwc_otg_core_if_t *_core_if,
												   dwc_ep_t *_ep)
{
	dwc_otg_dev_if_t *dev_if = _core_if->dev_if;
	uint32_t v, msk, emp;
	msk = dwc_read_reg32(&dev_if->dev_global_regs->diepmsk);
	emp = dwc_read_reg32(&dev_if->dev_global_regs->dtknqr4_fifoemptymsk);
	msk |= ((emp >> _ep->num) & 0x1) << 7;
	v = dwc_read_reg32(&dev_if->in_ep_regs[_ep->num]->diepint) & msk;
/*
	dwc_otg_dev_if_t *dev_if = _core_if->dev_if;
	uint32_t v;
	v = dwc_read_reg32(&dev_if->in_ep_regs[_ep->num]->diepint) &
			dwc_read_reg32(&dev_if->dev_global_regs->diepmsk);
*/
	return v;		 
}
/**
 * This function returns the Device OUT EP Interrupt register
 */
static inline uint32_t dwc_otg_read_dev_out_ep_intr(dwc_otg_core_if_t *_core_if, 
													dwc_ep_t *_ep)
{
	dwc_otg_dev_if_t *dev_if = _core_if->dev_if;
	uint32_t v;
	v = dwc_read_reg32( &dev_if->out_ep_regs[_ep->num]->doepint) &
			dwc_read_reg32(&dev_if->dev_global_regs->doepmsk);		
	return v;		 
}

/**
 * This function returns the Host All Channel Interrupt register
 */
static inline uint32_t dwc_otg_read_host_all_channels_intr (dwc_otg_core_if_t *_core_if)
{
	return (dwc_read_reg32 (&_core_if->host_if->host_global_regs->haint));
}

static inline uint32_t dwc_otg_read_host_channel_intr (dwc_otg_core_if_t *_core_if, dwc_hc_t *_hc)
{
	return (dwc_read_reg32 (&_core_if->host_if->hc_regs[_hc->hc_num]->hcint));
}


/**
 * This function returns the mode of the operation, host or device.
 *
 * @return 0 - Device Mode, 1 - Host Mode 
 */
static inline uint32_t dwc_otg_mode(dwc_otg_core_if_t *_core_if) 
{
	return (dwc_read_reg32( &_core_if->core_global_regs->gintsts ) & 0x1);
}

static inline uint8_t dwc_otg_is_device_mode(dwc_otg_core_if_t *_core_if) 
{
	return (dwc_otg_mode(_core_if) != DWC_HOST_MODE);
}
static inline uint8_t dwc_otg_is_host_mode(dwc_otg_core_if_t *_core_if) 
{
	return (dwc_otg_mode(_core_if) == DWC_HOST_MODE);
}

/**
 * This function returns the usb connector id status.
 *
 * @return 0 - A-Device Mode(Host), 
 *  	   1 - B-Device mode(Device)
 */
static inline uint8_t dwc_otg_connid(dwc_otg_core_if_t *_core_if) 
{
	return ((dwc_read_reg32( &_core_if->core_global_regs->gotgctl )>>16) & 0x1);
}

extern int32_t dwc_otg_handle_common_intr( dwc_otg_core_if_t *_core_if );


/**@}*/

/**
 * DWC_otg CIL callback structure.	This structure allows the HCD and
 * PCD to register functions used for starting and stopping the PCD
 * and HCD for role change on for a DRD.
 */
typedef struct dwc_otg_cil_callbacks 
{
	/** Start function for role change */
	int (*start) (void *_p);
	/** Stop Function for role change */
	int (*stop) (void *_p);
	/** Disconnect Function for role change */
	int (*disconnect) (void *_p);
	/** Resume/Remote wakeup Function */
	int (*resume_wakeup) (void *_p);
	/** Suspend function */
	int (*suspend) (void *_p, int suspend);
	/** Session Start (SRP) */
	int (*session_start) (void *_p);
	/** Pointer passed to start() and stop() */
	void *p;
} dwc_otg_cil_callbacks_t;

extern void dwc_otg_cil_register_pcd_callbacks( dwc_otg_core_if_t *_core_if,
												dwc_otg_cil_callbacks_t *_cb,
												void *_p);
extern void dwc_otg_cil_register_hcd_callbacks( dwc_otg_core_if_t *_core_if,
												dwc_otg_cil_callbacks_t *_cb,
												void *_p);
extern int dwc_debug(dwc_otg_core_if_t *core_if, int flag);												
#endif
