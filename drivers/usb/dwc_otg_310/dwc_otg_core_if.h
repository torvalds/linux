/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg/linux/drivers/dwc_otg_core_if.h $
 * $Revision: #15 $
 * $Date: 2012/12/10 $
 * $Change: 2123206 $
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
#if !defined(__DWC_CORE_IF_H__)
#define __DWC_CORE_IF_H__

#include "common_port/dwc_os.h"

/** @file
 * This file defines DWC_OTG Core API
 */

struct dwc_otg_core_if;
typedef struct dwc_otg_core_if dwc_otg_core_if_t;

/** Maximum number of Periodic FIFOs */
#define MAX_PERIO_FIFOS 15
/** Maximum number of Periodic FIFOs */
#define MAX_TX_FIFOS 15

/** Maximum number of Endpoints/HostChannels */
#define MAX_EPS_CHANNELS 16

extern dwc_otg_core_if_t *dwc_otg_cil_init(const uint32_t *_reg_base_addr);
extern void dwc_otg_core_init(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_core_init_no_reset(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_cil_remove(dwc_otg_core_if_t *_core_if);

extern void dwc_otg_enable_global_interrupts(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_disable_global_interrupts(dwc_otg_core_if_t *_core_if);

extern uint8_t dwc_otg_is_device_mode(dwc_otg_core_if_t *_core_if);
extern uint8_t dwc_otg_is_host_mode(dwc_otg_core_if_t *_core_if);

extern uint8_t dwc_otg_is_dma_enable(dwc_otg_core_if_t *core_if);

/** This function should be called on every hardware interrupt. */
extern int32_t dwc_otg_handle_common_intr(void *otg_dev);

/** @name OTG Core Parameters */
/** @{ */

/**
 * Specifies the OTG capabilities. The driver will automatically
 * detect the value for this parameter if none is specified.
 * 0 - HNP and SRP capable (default)
 * 1 - SRP Only capable
 * 2 - No HNP/SRP capable
 */
extern int dwc_otg_set_param_otg_cap(dwc_otg_core_if_t *core_if, int32_t val);
extern int32_t dwc_otg_get_param_otg_cap(dwc_otg_core_if_t *core_if);
#define DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE 0
#define DWC_OTG_CAP_PARAM_SRP_ONLY_CAPABLE 1
#define DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE 2
#define dwc_param_otg_cap_default DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE

extern int dwc_otg_set_param_opt(dwc_otg_core_if_t *core_if, int32_t val);
extern int32_t dwc_otg_get_param_opt(dwc_otg_core_if_t *core_if);
#define dwc_param_opt_default 1

/**
 * Specifies whether to use slave or DMA mode for accessing the data
 * FIFOs. The driver will automatically detect the value for this
 * parameter if none is specified.
 * 0 - Slave
 * 1 - DMA (default, if available)
 */
extern int dwc_otg_set_param_dma_enable(dwc_otg_core_if_t *core_if,
					int32_t val);
extern int32_t dwc_otg_get_param_dma_enable(dwc_otg_core_if_t *core_if);
#define dwc_param_dma_enable_default 1

/**
 * When DMA mode is enabled specifies whether to use
 * address DMA or DMA Descritor mode for accessing the data
 * FIFOs in device mode. The driver will automatically detect
 * the value for this parameter if none is specified.
 * 0 - address DMA
 * 1 - DMA Descriptor(default, if available)
 */
extern int dwc_otg_set_param_dma_desc_enable(dwc_otg_core_if_t *core_if,
					     int32_t val);
extern int32_t dwc_otg_get_param_dma_desc_enable(dwc_otg_core_if_t *core_if);
#define dwc_param_dma_desc_enable_default 0

/** The DMA Burst size (applicable only for External DMA
 * Mode). 1, 4, 8 16, 32, 64, 128, 256 (default 32)
 */
extern int dwc_otg_set_param_dma_burst_size(dwc_otg_core_if_t *core_if,
					    int32_t val);
extern int32_t dwc_otg_get_param_dma_burst_size(dwc_otg_core_if_t *core_if);
#define dwc_param_dma_burst_size_default 32

/**
 * Specifies the maximum speed of operation in host and device mode.
 * The actual speed depends on the speed of the attached device and
 * the value of phy_type. The actual speed depends on the speed of the
 * attached device.
 * 0 - High Speed (default)
 * 1 - Full Speed
 */
extern int dwc_otg_set_param_speed(dwc_otg_core_if_t *core_if, int32_t val);
extern int32_t dwc_otg_get_param_speed(dwc_otg_core_if_t *core_if);
#define dwc_param_speed_default 0
#define DWC_SPEED_PARAM_HIGH 0
#define DWC_SPEED_PARAM_FULL 1

/** Specifies whether low power mode is supported when attached
 *	to a Full Speed or Low Speed device in host mode.
 * 0 - Don't support low power mode (default)
 * 1 - Support low power mode
 */
extern int dwc_otg_set_param_host_support_fs_ls_low_power(dwc_otg_core_if_t *
							  core_if, int32_t val);
extern int32_t dwc_otg_get_param_host_support_fs_ls_low_power(dwc_otg_core_if_t
							      *core_if);
#define dwc_param_host_support_fs_ls_low_power_default 0

/** Specifies the PHY clock rate in low power mode when connected to a
 * Low Speed device in host mode. This parameter is applicable only if
 * HOST_SUPPORT_FS_LS_LOW_POWER is enabled. If PHY_TYPE is set to FS
 * then defaults to 6 MHZ otherwise 48 MHZ.
 *
 * 0 - 48 MHz
 * 1 - 6 MHz
 */
extern int dwc_otg_set_param_host_ls_low_power_phy_clk(dwc_otg_core_if_t *
						       core_if, int32_t val);
extern int32_t dwc_otg_get_param_host_ls_low_power_phy_clk(dwc_otg_core_if_t *
							   core_if);
#define dwc_param_host_ls_low_power_phy_clk_default 0
#define DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ 0
#define DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ 1

/**
 * 0 - Use cC FIFO size parameters
 * 1 - Allow dynamic FIFO sizing (default)
 */
extern int dwc_otg_set_param_enable_dynamic_fifo(dwc_otg_core_if_t *core_if,
						 int32_t val);
extern int32_t dwc_otg_get_param_enable_dynamic_fifo(dwc_otg_core_if_t *
						     core_if);
#define dwc_param_enable_dynamic_fifo_default 1

/** Total number of 4-byte words in the data FIFO memory. This
 * memory includes the Rx FIFO, non-periodic Tx FIFO, and periodic
 * Tx FIFOs.
 * 32 to 32768 (default 8192)
 * Note: The total FIFO memory depth in the FPGA configuration is 8192.
 */
extern int dwc_otg_set_param_data_fifo_size(dwc_otg_core_if_t *core_if,
					    int32_t val);
extern int32_t dwc_otg_get_param_data_fifo_size(dwc_otg_core_if_t *core_if);
#define dwc_param_data_fifo_size_default 8192

/** Number of 4-byte words in the Rx FIFO in device mode when dynamic
 * FIFO sizing is enabled.
 * 16 to 32768 (default 1064)
 */
extern int dwc_otg_set_param_dev_rx_fifo_size(dwc_otg_core_if_t *core_if,
					      int32_t val);
extern int32_t dwc_otg_get_param_dev_rx_fifo_size(dwc_otg_core_if_t *core_if);
#define dwc_param_dev_rx_fifo_size_default 1064

/** Number of 4-byte words in the non-periodic Tx FIFO in device mode
 * when dynamic FIFO sizing is enabled.
 * 16 to 32768 (default 1024)
 */
extern int dwc_otg_set_param_dev_nperio_tx_fifo_size(dwc_otg_core_if_t *
						     core_if, int32_t val);
extern int32_t dwc_otg_get_param_dev_nperio_tx_fifo_size(dwc_otg_core_if_t *
							 core_if);
#define dwc_param_dev_nperio_tx_fifo_size_default 1024

/** Number of 4-byte words in each of the periodic Tx FIFOs in device
 * mode when dynamic FIFO sizing is enabled.
 * 4 to 768 (default 256)
 */
extern int dwc_otg_set_param_dev_perio_tx_fifo_size(dwc_otg_core_if_t *core_if,
						    int32_t val, int fifo_num);
extern int32_t dwc_otg_get_param_dev_perio_tx_fifo_size(dwc_otg_core_if_t *
							core_if, int fifo_num);
#define dwc_param_dev_perio_tx_fifo_size_default 256

/** Number of 4-byte words in the Rx FIFO in host mode when dynamic
 * FIFO sizing is enabled.
 * 16 to 32768 (default 1024)
 */
extern int dwc_otg_set_param_host_rx_fifo_size(dwc_otg_core_if_t *core_if,
					       int32_t val);
extern int32_t dwc_otg_get_param_host_rx_fifo_size(dwc_otg_core_if_t *core_if);
#define dwc_param_host_rx_fifo_size_default 1024

/** Number of 4-byte words in the non-periodic Tx FIFO in host mode
 * when Dynamic FIFO sizing is enabled in the core.
 * 16 to 32768 (default 1024)
 */
extern int dwc_otg_set_param_host_nperio_tx_fifo_size(dwc_otg_core_if_t *
						      core_if, int32_t val);
extern int32_t dwc_otg_get_param_host_nperio_tx_fifo_size(dwc_otg_core_if_t *
							  core_if);
#define dwc_param_host_nperio_tx_fifo_size_default 1024

/** Number of 4-byte words in the host periodic Tx FIFO when dynamic
 * FIFO sizing is enabled.
 * 16 to 32768 (default 1024)
 */
extern int dwc_otg_set_param_host_perio_tx_fifo_size(dwc_otg_core_if_t *
						     core_if, int32_t val);
extern int32_t dwc_otg_get_param_host_perio_tx_fifo_size(dwc_otg_core_if_t *
							 core_if);
#define dwc_param_host_perio_tx_fifo_size_default 1024

/** The maximum transfer size supported in bytes.
 * 2047 to 65,535  (default 65,535)
 */
extern int dwc_otg_set_param_max_transfer_size(dwc_otg_core_if_t *core_if,
					       int32_t val);
extern int32_t dwc_otg_get_param_max_transfer_size(dwc_otg_core_if_t *core_if);
#define dwc_param_max_transfer_size_default 65535

/** The maximum number of packets in a transfer.
 * 15 to 511  (default 511)
 */
extern int dwc_otg_set_param_max_packet_count(dwc_otg_core_if_t *core_if,
					      int32_t val);
extern int32_t dwc_otg_get_param_max_packet_count(dwc_otg_core_if_t *core_if);
#define dwc_param_max_packet_count_default 511

/** The number of host channel registers to use.
 * 1 to 16 (default 12)
 * Note: The FPGA configuration supports a maximum of 12 host channels.
 */
extern int dwc_otg_set_param_host_channels(dwc_otg_core_if_t *core_if,
					   int32_t val);
extern int32_t dwc_otg_get_param_host_channels(dwc_otg_core_if_t *core_if);
#define dwc_param_host_channels_default 16

/** The number of endpoints in addition to EP0 available for device
 * mode operations.
 * 1 to 15 (default 6 IN and OUT)
 * Note: The FPGA configuration supports a maximum of 6 IN and OUT
 * endpoints in addition to EP0.
 */
extern int dwc_otg_set_param_dev_endpoints(dwc_otg_core_if_t *core_if,
					   int32_t val);
extern int32_t dwc_otg_get_param_dev_endpoints(dwc_otg_core_if_t *core_if);
#define dwc_param_dev_endpoints_default 9

/**
 * Specifies the type of PHY interface to use. By default, the driver
 * will automatically detect the phy_type.
 *
 * 0 - Full Speed PHY
 * 1 - UTMI+ (default)
 * 2 - ULPI
 */
extern int dwc_otg_set_param_phy_type(dwc_otg_core_if_t *core_if, int32_t val);
extern int32_t dwc_otg_get_param_phy_type(dwc_otg_core_if_t *core_if);
#define DWC_PHY_TYPE_PARAM_FS 0
#define DWC_PHY_TYPE_PARAM_UTMI 1
#define DWC_PHY_TYPE_PARAM_ULPI 2
#define dwc_param_phy_type_default DWC_PHY_TYPE_PARAM_UTMI

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
extern int dwc_otg_set_param_phy_utmi_width(dwc_otg_core_if_t *core_if,
					    int32_t val);
extern int32_t dwc_otg_get_param_phy_utmi_width(dwc_otg_core_if_t *core_if);
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
extern int dwc_otg_set_param_phy_ulpi_ddr(dwc_otg_core_if_t *core_if,
					  int32_t val);
extern int32_t dwc_otg_get_param_phy_ulpi_ddr(dwc_otg_core_if_t *core_if);
#define dwc_param_phy_ulpi_ddr_default 0

/**
 * Specifies whether to use the internal or external supply to
 * drive the vbus with a ULPI phy.
 */
extern int dwc_otg_set_param_phy_ulpi_ext_vbus(dwc_otg_core_if_t *core_if,
					       int32_t val);
extern int32_t dwc_otg_get_param_phy_ulpi_ext_vbus(dwc_otg_core_if_t *core_if);
#define DWC_PHY_ULPI_INTERNAL_VBUS 0
#define DWC_PHY_ULPI_EXTERNAL_VBUS 1
#define dwc_param_phy_ulpi_ext_vbus_default DWC_PHY_ULPI_INTERNAL_VBUS

/**
 * Specifies whether to use the I2Cinterface for full speed PHY. This
 * parameter is only applicable if PHY_TYPE is FS.
 * 0 - No (default)
 * 1 - Yes
 */
extern int dwc_otg_set_param_i2c_enable(dwc_otg_core_if_t *core_if,
					int32_t val);
extern int32_t dwc_otg_get_param_i2c_enable(dwc_otg_core_if_t *core_if);
#define dwc_param_i2c_enable_default 0

extern int dwc_otg_set_param_ulpi_fs_ls(dwc_otg_core_if_t *core_if,
					int32_t val);
extern int32_t dwc_otg_get_param_ulpi_fs_ls(dwc_otg_core_if_t *core_if);
#define dwc_param_ulpi_fs_ls_default 0

extern int dwc_otg_set_param_ts_dline(dwc_otg_core_if_t *core_if, int32_t val);
extern int32_t dwc_otg_get_param_ts_dline(dwc_otg_core_if_t *core_if);
#define dwc_param_ts_dline_default 0

/**
 * Specifies whether dedicated transmit FIFOs are
 * enabled for non periodic IN endpoints in device mode
 * 0 - No
 * 1 - Yes
 */
extern int dwc_otg_set_param_en_multiple_tx_fifo(dwc_otg_core_if_t *core_if,
						 int32_t val);
extern int32_t dwc_otg_get_param_en_multiple_tx_fifo(dwc_otg_core_if_t *
						     core_if);
#define dwc_param_en_multiple_tx_fifo_default 1

/** Number of 4-byte words in each of the Tx FIFOs in device
 * mode when dynamic FIFO sizing is enabled.
 * 4 to 768 (default 256)
 */
extern int dwc_otg_set_param_dev_tx_fifo_size(dwc_otg_core_if_t *core_if,
					      int fifo_num, int32_t val);
extern int32_t dwc_otg_get_param_dev_tx_fifo_size(dwc_otg_core_if_t *core_if,
						  int fifo_num);
#define dwc_param_dev_tx_fifo_size_default 256

/** Thresholding enable flag-
 * bit 0 - enable non-ISO Tx thresholding
 * bit 1 - enable ISO Tx thresholding
 * bit 2 - enable Rx thresholding
 */
extern int dwc_otg_set_param_thr_ctl(dwc_otg_core_if_t *core_if, int32_t val);
extern int32_t dwc_otg_get_thr_ctl(dwc_otg_core_if_t *core_if, int fifo_num);
#define dwc_param_thr_ctl_default 0

/** Thresholding length for Tx
 * FIFOs in 32 bit DWORDs
 */
extern int dwc_otg_set_param_tx_thr_length(dwc_otg_core_if_t *core_if,
					   int32_t val);
extern int32_t dwc_otg_get_tx_thr_length(dwc_otg_core_if_t *core_if);
#define dwc_param_tx_thr_length_default 64

/** Thresholding length for Rx
 *	FIFOs in 32 bit DWORDs
 */
extern int dwc_otg_set_param_rx_thr_length(dwc_otg_core_if_t *core_if,
					   int32_t val);
extern int32_t dwc_otg_get_rx_thr_length(dwc_otg_core_if_t *core_if);
#define dwc_param_rx_thr_length_default 64

/**
 * Specifies whether LPM (Link Power Management) support is enabled
 */
extern int dwc_otg_set_param_lpm_enable(dwc_otg_core_if_t *core_if,
					int32_t val);
extern int32_t dwc_otg_get_param_lpm_enable(dwc_otg_core_if_t *core_if);
#define dwc_param_lpm_enable_default 1

/**
 * Specifies whether LPM Errata (Link Power Management) support is enabled
 */
extern int dwc_otg_set_param_besl_enable(dwc_otg_core_if_t *core_if,
					 int32_t val);
extern int32_t dwc_otg_get_param_besl_enable(dwc_otg_core_if_t *core_if);
#define dwc_param_besl_enable_default 0

/**
 * Specifies baseline_besl default value
 */
extern int dwc_otg_set_param_baseline_besl(dwc_otg_core_if_t *core_if,
					   int32_t val);
extern int32_t dwc_otg_get_param_baseline_besl(dwc_otg_core_if_t *core_if);
#define dwc_param_baseline_besl_default 0

/**
 * Specifies deep_besl default value
 */
extern int dwc_otg_set_param_deep_besl(dwc_otg_core_if_t *core_if,
				       int32_t val);
extern int32_t dwc_otg_get_param_deep_besl(dwc_otg_core_if_t *core_if);
#define dwc_param_deep_besl_default 15

/**
 * Specifies whether PTI enhancement is enabled
 */
extern int dwc_otg_set_param_pti_enable(dwc_otg_core_if_t *core_if,
					int32_t val);
extern int32_t dwc_otg_get_param_pti_enable(dwc_otg_core_if_t *core_if);
#define dwc_param_pti_enable_default 0

/**
 * Specifies whether MPI enhancement is enabled
 */
extern int dwc_otg_set_param_mpi_enable(dwc_otg_core_if_t *core_if,
					int32_t val);
extern int32_t dwc_otg_get_param_mpi_enable(dwc_otg_core_if_t *core_if);
#define dwc_param_mpi_enable_default 0

/**
 * Specifies whether ADP capability is enabled
 */
extern int dwc_otg_set_param_adp_enable(dwc_otg_core_if_t *core_if,
					int32_t val);
extern int32_t dwc_otg_get_param_adp_enable(dwc_otg_core_if_t *core_if);
#define dwc_param_adp_enable_default 0

/**
 * Specifies whether IC_USB capability is enabled
 */

extern int dwc_otg_set_param_ic_usb_cap(dwc_otg_core_if_t *core_if,
					int32_t val);
extern int32_t dwc_otg_get_param_ic_usb_cap(dwc_otg_core_if_t *core_if);
#define dwc_param_ic_usb_cap_default 0

extern int dwc_otg_set_param_ahb_thr_ratio(dwc_otg_core_if_t *core_if,
					   int32_t val);
extern int32_t dwc_otg_get_param_ahb_thr_ratio(dwc_otg_core_if_t *core_if);
#define dwc_param_ahb_thr_ratio_default 0

extern int dwc_otg_set_param_power_down(dwc_otg_core_if_t *core_if,
					int32_t val);
extern int32_t dwc_otg_get_param_power_down(dwc_otg_core_if_t *core_if);
#define dwc_param_power_down_default 0

extern int dwc_otg_set_param_reload_ctl(dwc_otg_core_if_t *core_if,
					int32_t val);
extern int32_t dwc_otg_get_param_reload_ctl(dwc_otg_core_if_t *core_if);
#define dwc_param_reload_ctl_default 0

extern int dwc_otg_set_param_dev_out_nak(dwc_otg_core_if_t *core_if,
					 int32_t val);
extern int32_t dwc_otg_get_param_dev_out_nak(dwc_otg_core_if_t *core_if);
#define dwc_param_dev_out_nak_default 0

extern int dwc_otg_set_param_cont_on_bna(dwc_otg_core_if_t *core_if,
					 int32_t val);
extern int32_t dwc_otg_get_param_cont_on_bna(dwc_otg_core_if_t *core_if);
#define dwc_param_cont_on_bna_default 0

extern int dwc_otg_set_param_ahb_single(dwc_otg_core_if_t *core_if,
					int32_t val);
extern int32_t dwc_otg_get_param_ahb_single(dwc_otg_core_if_t *core_if);
#define dwc_param_ahb_single_default 0

extern int dwc_otg_set_param_otg_ver(dwc_otg_core_if_t *core_if, int32_t val);
extern int32_t dwc_otg_get_param_otg_ver(dwc_otg_core_if_t *core_if);
#define dwc_param_otg_ver_default 0

/** @} */

/** @name Access to registers and bit-fields */

/**
 * Dump core registers and SPRAM
 */
extern void dwc_otg_dump_dev_registers(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_dump_spram(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_dump_host_registers(dwc_otg_core_if_t *_core_if);
extern void dwc_otg_dump_global_registers(dwc_otg_core_if_t *_core_if);

/**
 * Get host negotiation status.
 */
extern uint32_t dwc_otg_get_hnpstatus(dwc_otg_core_if_t *core_if);

/**
 * Get srp status
 */
extern uint32_t dwc_otg_get_srpstatus(dwc_otg_core_if_t *core_if);

/**
 * Set hnpreq bit in the GOTGCTL register.
 */
extern void dwc_otg_set_hnpreq(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * Get Content of SNPSID register.
 */
extern uint32_t dwc_otg_get_gsnpsid(dwc_otg_core_if_t *core_if);

/**
 * Get current mode.
 * Returns 0 if in device mode, and 1 if in host mode.
 */
extern uint32_t dwc_otg_get_mode(dwc_otg_core_if_t *core_if);

/**
 * Get value of hnpcapable field in the GUSBCFG register
 */
extern uint32_t dwc_otg_get_hnpcapable(dwc_otg_core_if_t *core_if);
/**
 * Set value of hnpcapable field in the GUSBCFG register
 */
extern void dwc_otg_set_hnpcapable(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * Get value of srpcapable field in the GUSBCFG register
 */
extern uint32_t dwc_otg_get_srpcapable(dwc_otg_core_if_t *core_if);
/**
 * Set value of srpcapable field in the GUSBCFG register
 */
extern void dwc_otg_set_srpcapable(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * Get value of devspeed field in the DCFG register
 */
extern uint32_t dwc_otg_get_devspeed(dwc_otg_core_if_t *core_if);
/**
 * Set value of devspeed field in the DCFG register
 */
extern void dwc_otg_set_devspeed(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * Get the value of busconnected field from the HPRT0 register
 */
extern uint32_t dwc_otg_get_busconnected(dwc_otg_core_if_t *core_if);

/**
 * Gets the device enumeration Speed.
 */
extern uint32_t dwc_otg_get_enumspeed(dwc_otg_core_if_t *core_if);

/**
 * Get value of prtpwr field from the HPRT0 register
 */
extern uint32_t dwc_otg_get_prtpower(dwc_otg_core_if_t *core_if);

/**
 * Get value of flag indicating core state - hibernated or not
 */
extern uint32_t dwc_otg_get_core_state(dwc_otg_core_if_t *core_if);

/**
 * Set value of prtpwr field from the HPRT0 register
 */
extern void dwc_otg_set_prtpower(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * Get value of prtsusp field from the HPRT0 regsiter
 */
extern uint32_t dwc_otg_get_prtsuspend(dwc_otg_core_if_t *core_if);
/**
 * Set value of prtpwr field from the HPRT0 register
 */
extern void dwc_otg_set_prtsuspend(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * Get value of ModeChTimEn field from the HCFG regsiter
 */
extern uint32_t dwc_otg_get_mode_ch_tim(dwc_otg_core_if_t *core_if);
/**
 * Set value of ModeChTimEn field from the HCFG regsiter
 */
extern void dwc_otg_set_mode_ch_tim(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * Get value of Fram Interval field from the HFIR regsiter
 */
extern uint32_t dwc_otg_get_fr_interval(dwc_otg_core_if_t *core_if);
/**
 * Set value of Frame Interval field from the HFIR regsiter
 */
extern void dwc_otg_set_fr_interval(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * Set value of prtres field from the HPRT0 register
 *FIXME Remove?
 */
extern void dwc_otg_set_prtresume(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * Get value of rmtwkupsig bit in DCTL register
 */
extern uint32_t dwc_otg_get_remotewakesig(dwc_otg_core_if_t *core_if);

/**
 * Get value of besl_reject bit in DCTL register
 */

extern uint32_t dwc_otg_get_beslreject(dwc_otg_core_if_t *core_if);

/**
 * Set value of besl_reject bit in DCTL register
 */

extern void dwc_otg_set_beslreject(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * Get value of prt_sleep_sts field from the GLPMCFG register
 */
extern uint32_t dwc_otg_get_lpm_portsleepstatus(dwc_otg_core_if_t *core_if);

/**
 * Get value of rem_wkup_en field from the GLPMCFG register
 */
extern uint32_t dwc_otg_get_lpm_remotewakeenabled(dwc_otg_core_if_t *core_if);

/**
 * Get value of appl_resp field from the GLPMCFG register
 */
extern uint32_t dwc_otg_get_lpmresponse(dwc_otg_core_if_t *core_if);
/**
 * Set value of appl_resp field from the GLPMCFG register
 */
extern void dwc_otg_set_lpmresponse(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * Get value of hsic_connect field from the GLPMCFG register
 */
extern uint32_t dwc_otg_get_hsic_connect(dwc_otg_core_if_t *core_if);
/**
 * Set value of hsic_connect field from the GLPMCFG register
 */
extern void dwc_otg_set_hsic_connect(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * Get value of inv_sel_hsic field from the GLPMCFG register.
 */
extern uint32_t dwc_otg_get_inv_sel_hsic(dwc_otg_core_if_t *core_if);
/**
 * Set value of inv_sel_hsic field from the GLPMFG register.
 */
extern void dwc_otg_set_inv_sel_hsic(dwc_otg_core_if_t *core_if, uint32_t val);
/**
 * Set value of hird_thresh field from the GLPMFG register.
 */
extern void dwc_otg_set_hirdthresh(dwc_otg_core_if_t *core_if, uint32_t val);
/**
 * Get value of hird_thresh field from the GLPMFG register.
 */
extern uint32_t dwc_otg_get_hirdthresh(dwc_otg_core_if_t *core_if);

/*
 * Some functions for accessing registers
 */

/**
 *  GOTGCTL register
 */
extern uint32_t dwc_otg_get_gotgctl(dwc_otg_core_if_t *core_if);
extern void dwc_otg_set_gotgctl(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * GUSBCFG register
 */
extern uint32_t dwc_otg_get_gusbcfg(dwc_otg_core_if_t *core_if);
extern void dwc_otg_set_gusbcfg(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * GRXFSIZ register
 */
extern uint32_t dwc_otg_get_grxfsiz(dwc_otg_core_if_t *core_if);
extern void dwc_otg_set_grxfsiz(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * GNPTXFSIZ register
 */
extern uint32_t dwc_otg_get_gnptxfsiz(dwc_otg_core_if_t *core_if);
extern void dwc_otg_set_gnptxfsiz(dwc_otg_core_if_t *core_if, uint32_t val);

extern uint32_t dwc_otg_get_gpvndctl(dwc_otg_core_if_t *core_if);
extern void dwc_otg_set_gpvndctl(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * GGPIO register
 */
extern uint32_t dwc_otg_get_ggpio(dwc_otg_core_if_t *core_if);
extern void dwc_otg_set_ggpio(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * GUID register
 */
extern uint32_t dwc_otg_get_guid(dwc_otg_core_if_t *core_if);
extern void dwc_otg_set_guid(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * HPRT0 register
 */
extern uint32_t dwc_otg_get_hprt0(dwc_otg_core_if_t *core_if);
extern void dwc_otg_set_hprt0(dwc_otg_core_if_t *core_if, uint32_t val);

/**
 * GHPTXFSIZE
 */
extern uint32_t dwc_otg_get_hptxfsiz(dwc_otg_core_if_t *core_if);

/** @} */

#endif /* __DWC_CORE_IF_H__ */
