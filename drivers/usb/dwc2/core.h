/*
 * core.h - DesignWare HS OTG Controller common declarations
 *
 * Copyright (C) 2004-2013 Synopsys, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DWC2_CORE_H__
#define __DWC2_CORE_H__

#include <linux/phy/phy.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/usb/phy.h>
#include "hw.h"

#ifdef DWC2_LOG_WRITES
static inline void do_write(u32 value, void *addr)
{
	writel(value, addr);
	pr_info("INFO:: wrote %08x to %p\n", value, addr);
}

#undef writel
#define writel(v, a)	do_write(v, a)
#endif

/* Maximum number of Endpoints/HostChannels */
#define MAX_EPS_CHANNELS	16

/* s3c-hsotg declarations */
static const char * const s3c_hsotg_supply_names[] = {
	"vusb_d",               /* digital USB supply, 1.2V */
	"vusb_a",               /* analog USB supply, 1.1V */
};

/*
 * EP0_MPS_LIMIT
 *
 * Unfortunately there seems to be a limit of the amount of data that can
 * be transferred by IN transactions on EP0. This is either 127 bytes or 3
 * packets (which practically means 1 packet and 63 bytes of data) when the
 * MPS is set to 64.
 *
 * This means if we are wanting to move >127 bytes of data, we need to
 * split the transactions up, but just doing one packet at a time does
 * not work (this may be an implicit DATA0 PID on first packet of the
 * transaction) and doing 2 packets is outside the controller's limits.
 *
 * If we try to lower the MPS size for EP0, then no transfers work properly
 * for EP0, and the system will fail basic enumeration. As no cause for this
 * has currently been found, we cannot support any large IN transfers for
 * EP0.
 */
#define EP0_MPS_LIMIT   64

struct s3c_hsotg;
struct s3c_hsotg_req;

/**
 * struct s3c_hsotg_ep - driver endpoint definition.
 * @ep: The gadget layer representation of the endpoint.
 * @name: The driver generated name for the endpoint.
 * @queue: Queue of requests for this endpoint.
 * @parent: Reference back to the parent device structure.
 * @req: The current request that the endpoint is processing. This is
 *       used to indicate an request has been loaded onto the endpoint
 *       and has yet to be completed (maybe due to data move, or simply
 *       awaiting an ack from the core all the data has been completed).
 * @debugfs: File entry for debugfs file for this endpoint.
 * @lock: State lock to protect contents of endpoint.
 * @dir_in: Set to true if this endpoint is of the IN direction, which
 *          means that it is sending data to the Host.
 * @index: The index for the endpoint registers.
 * @mc: Multi Count - number of transactions per microframe
 * @interval - Interval for periodic endpoints
 * @name: The name array passed to the USB core.
 * @halted: Set if the endpoint has been halted.
 * @periodic: Set if this is a periodic ep, such as Interrupt
 * @isochronous: Set if this is a isochronous ep
 * @sent_zlp: Set if we've sent a zero-length packet.
 * @total_data: The total number of data bytes done.
 * @fifo_size: The size of the FIFO (for periodic IN endpoints)
 * @fifo_load: The amount of data loaded into the FIFO (periodic IN)
 * @last_load: The offset of data for the last start of request.
 * @size_loaded: The last loaded size for DxEPTSIZE for periodic IN
 *
 * This is the driver's state for each registered enpoint, allowing it
 * to keep track of transactions that need doing. Each endpoint has a
 * lock to protect the state, to try and avoid using an overall lock
 * for the host controller as much as possible.
 *
 * For periodic IN endpoints, we have fifo_size and fifo_load to try
 * and keep track of the amount of data in the periodic FIFO for each
 * of these as we don't have a status register that tells us how much
 * is in each of them. (note, this may actually be useless information
 * as in shared-fifo mode periodic in acts like a single-frame packet
 * buffer than a fifo)
 */
struct s3c_hsotg_ep {
	struct usb_ep           ep;
	struct list_head        queue;
	struct s3c_hsotg        *parent;
	struct s3c_hsotg_req    *req;
	struct dentry           *debugfs;

	unsigned long           total_data;
	unsigned int            size_loaded;
	unsigned int            last_load;
	unsigned int            fifo_load;
	unsigned short          fifo_size;

	unsigned char           dir_in;
	unsigned char           index;
	unsigned char           mc;
	unsigned char           interval;

	unsigned int            halted:1;
	unsigned int            periodic:1;
	unsigned int            isochronous:1;
	unsigned int            sent_zlp:1;

	char                    name[10];
};

/**
 * struct s3c_hsotg - driver state.
 * @dev: The parent device supplied to the probe function
 * @driver: USB gadget driver
 * @phy: The otg phy transceiver structure for phy control.
 * @uphy: The otg phy transceiver structure for old USB phy control.
 * @plat: The platform specific configuration data. This can be removed once
 * all SoCs support usb transceiver.
 * @regs: The memory area mapped for accessing registers.
 * @irq: The IRQ number we are using
 * @supplies: Definition of USB power supplies
 * @phyif: PHY interface width
 * @dedicated_fifos: Set if the hardware has dedicated IN-EP fifos.
 * @num_of_eps: Number of available EPs (excluding EP0)
 * @debug_root: root directrory for debugfs.
 * @debug_file: main status file for debugfs.
 * @debug_fifo: FIFO status file for debugfs.
 * @ep0_reply: Request used for ep0 reply.
 * @ep0_buff: Buffer for EP0 reply data, if needed.
 * @ctrl_buff: Buffer for EP0 control requests.
 * @ctrl_req: Request for EP0 control packets.
 * @setup: NAK management for EP0 SETUP
 * @last_rst: Time of last reset
 * @eps: The endpoints being supplied to the gadget framework
 */
struct s3c_hsotg {
	struct device            *dev;
	struct usb_gadget_driver *driver;
	struct phy               *phy;
	struct usb_phy           *uphy;
	struct s3c_hsotg_plat    *plat;

	spinlock_t              lock;

	void __iomem            *regs;
	int                     irq;
	struct clk              *clk;

	struct regulator_bulk_data supplies[ARRAY_SIZE(s3c_hsotg_supply_names)];

	u32                     phyif;
	int			fifo_mem;
	unsigned int            dedicated_fifos:1;
	unsigned char           num_of_eps;

	struct dentry           *debug_root;
	struct dentry           *debug_file;
	struct dentry           *debug_fifo;

	struct usb_request      *ep0_reply;
	struct usb_request      *ctrl_req;
	u8                      ep0_buff[8];
	u8                      ctrl_buff[8];

	struct usb_gadget       gadget;
	unsigned int            setup;
	unsigned long           last_rst;
	struct s3c_hsotg_ep     *eps;
};

/**
 * struct s3c_hsotg_req - data transfer request
 * @req: The USB gadget request
 * @queue: The list of requests for the endpoint this is queued for.
 * @in_progress: Has already had size/packets written to core
 * @mapped: DMA buffer for this request has been mapped via dma_map_single().
 */
struct s3c_hsotg_req {
	struct usb_request      req;
	struct list_head        queue;
	unsigned char           in_progress;
	unsigned char           mapped;
};

#define call_gadget(_hs, _entry) \
do { \
	if ((_hs)->gadget.speed != USB_SPEED_UNKNOWN && \
		(_hs)->driver && (_hs)->driver->_entry) { \
		spin_unlock(&_hs->lock); \
		(_hs)->driver->_entry(&(_hs)->gadget); \
		spin_lock(&_hs->lock); \
	} \
} while (0)

struct dwc2_hsotg;
struct dwc2_host_chan;

/* Device States */
enum dwc2_lx_state {
	DWC2_L0,	/* On state */
	DWC2_L1,	/* LPM sleep state */
	DWC2_L2,	/* USB suspend state */
	DWC2_L3,	/* Off state */
};

/**
 * struct dwc2_core_params - Parameters for configuring the core
 *
 * @otg_cap:            Specifies the OTG capabilities.
 *                       0 - HNP and SRP capable
 *                       1 - SRP Only capable
 *                       2 - No HNP/SRP capable (always available)
 *                      Defaults to best available option (0, 1, then 2)
 * @otg_ver:            OTG version supported
 *                       0 - 1.3 (default)
 *                       1 - 2.0
 * @dma_enable:         Specifies whether to use slave or DMA mode for accessing
 *                      the data FIFOs. The driver will automatically detect the
 *                      value for this parameter if none is specified.
 *                       0 - Slave (always available)
 *                       1 - DMA (default, if available)
 * @dma_desc_enable:    When DMA mode is enabled, specifies whether to use
 *                      address DMA mode or descriptor DMA mode for accessing
 *                      the data FIFOs. The driver will automatically detect the
 *                      value for this if none is specified.
 *                       0 - Address DMA
 *                       1 - Descriptor DMA (default, if available)
 * @speed:              Specifies the maximum speed of operation in host and
 *                      device mode. The actual speed depends on the speed of
 *                      the attached device and the value of phy_type.
 *                       0 - High Speed
 *                           (default when phy_type is UTMI+ or ULPI)
 *                       1 - Full Speed
 *                           (default when phy_type is Full Speed)
 * @enable_dynamic_fifo: 0 - Use coreConsultant-specified FIFO size parameters
 *                       1 - Allow dynamic FIFO sizing (default, if available)
 * @en_multiple_tx_fifo: Specifies whether dedicated per-endpoint transmit FIFOs
 *                      are enabled
 * @host_rx_fifo_size:  Number of 4-byte words in the Rx FIFO in host mode when
 *                      dynamic FIFO sizing is enabled
 *                       16 to 32768
 *                      Actual maximum value is autodetected and also
 *                      the default.
 * @host_nperio_tx_fifo_size: Number of 4-byte words in the non-periodic Tx FIFO
 *                      in host mode when dynamic FIFO sizing is enabled
 *                       16 to 32768
 *                      Actual maximum value is autodetected and also
 *                      the default.
 * @host_perio_tx_fifo_size: Number of 4-byte words in the periodic Tx FIFO in
 *                      host mode when dynamic FIFO sizing is enabled
 *                       16 to 32768
 *                      Actual maximum value is autodetected and also
 *                      the default.
 * @max_transfer_size:  The maximum transfer size supported, in bytes
 *                       2047 to 65,535
 *                      Actual maximum value is autodetected and also
 *                      the default.
 * @max_packet_count:   The maximum number of packets in a transfer
 *                       15 to 511
 *                      Actual maximum value is autodetected and also
 *                      the default.
 * @host_channels:      The number of host channel registers to use
 *                       1 to 16
 *                      Actual maximum value is autodetected and also
 *                      the default.
 * @phy_type:           Specifies the type of PHY interface to use. By default,
 *                      the driver will automatically detect the phy_type.
 *                       0 - Full Speed Phy
 *                       1 - UTMI+ Phy
 *                       2 - ULPI Phy
 *                      Defaults to best available option (2, 1, then 0)
 * @phy_utmi_width:     Specifies the UTMI+ Data Width (in bits). This parameter
 *                      is applicable for a phy_type of UTMI+ or ULPI. (For a
 *                      ULPI phy_type, this parameter indicates the data width
 *                      between the MAC and the ULPI Wrapper.) Also, this
 *                      parameter is applicable only if the OTG_HSPHY_WIDTH cC
 *                      parameter was set to "8 and 16 bits", meaning that the
 *                      core has been configured to work at either data path
 *                      width.
 *                       8 or 16 (default 16 if available)
 * @phy_ulpi_ddr:       Specifies whether the ULPI operates at double or single
 *                      data rate. This parameter is only applicable if phy_type
 *                      is ULPI.
 *                       0 - single data rate ULPI interface with 8 bit wide
 *                           data bus (default)
 *                       1 - double data rate ULPI interface with 4 bit wide
 *                           data bus
 * @phy_ulpi_ext_vbus:  For a ULPI phy, specifies whether to use the internal or
 *                      external supply to drive the VBus
 *                       0 - Internal supply (default)
 *                       1 - External supply
 * @i2c_enable:         Specifies whether to use the I2Cinterface for a full
 *                      speed PHY. This parameter is only applicable if phy_type
 *                      is FS.
 *                       0 - No (default)
 *                       1 - Yes
 * @ulpi_fs_ls:         Make ULPI phy operate in FS/LS mode only
 *                       0 - No (default)
 *                       1 - Yes
 * @host_support_fs_ls_low_power: Specifies whether low power mode is supported
 *                      when attached to a Full Speed or Low Speed device in
 *                      host mode.
 *                       0 - Don't support low power mode (default)
 *                       1 - Support low power mode
 * @host_ls_low_power_phy_clk: Specifies the PHY clock rate in low power mode
 *                      when connected to a Low Speed device in host
 *                      mode. This parameter is applicable only if
 *                      host_support_fs_ls_low_power is enabled.
 *                       0 - 48 MHz
 *                           (default when phy_type is UTMI+ or ULPI)
 *                       1 - 6 MHz
 *                           (default when phy_type is Full Speed)
 * @ts_dline:           Enable Term Select Dline pulsing
 *                       0 - No (default)
 *                       1 - Yes
 * @reload_ctl:         Allow dynamic reloading of HFIR register during runtime
 *                       0 - No (default for core < 2.92a)
 *                       1 - Yes (default for core >= 2.92a)
 * @ahbcfg:             This field allows the default value of the GAHBCFG
 *                      register to be overridden
 *                       -1         - GAHBCFG value will be set to 0x06
 *                                    (INCR4, default)
 *                       all others - GAHBCFG value will be overridden with
 *                                    this value
 *                      Not all bits can be controlled like this, the
 *                      bits defined by GAHBCFG_CTRL_MASK are controlled
 *                      by the driver and are ignored in this
 *                      configuration value.
 * @uframe_sched:       True to enable the microframe scheduler
 *
 * The following parameters may be specified when starting the module. These
 * parameters define how the DWC_otg controller should be configured. A
 * value of -1 (or any other out of range value) for any parameter means
 * to read the value from hardware (if possible) or use the builtin
 * default described above.
 */
struct dwc2_core_params {
	/*
	 * Don't add any non-int members here, this will break
	 * dwc2_set_all_params!
	 */
	int otg_cap;
	int otg_ver;
	int dma_enable;
	int dma_desc_enable;
	int speed;
	int enable_dynamic_fifo;
	int en_multiple_tx_fifo;
	int host_rx_fifo_size;
	int host_nperio_tx_fifo_size;
	int host_perio_tx_fifo_size;
	int max_transfer_size;
	int max_packet_count;
	int host_channels;
	int phy_type;
	int phy_utmi_width;
	int phy_ulpi_ddr;
	int phy_ulpi_ext_vbus;
	int i2c_enable;
	int ulpi_fs_ls;
	int host_support_fs_ls_low_power;
	int host_ls_low_power_phy_clk;
	int ts_dline;
	int reload_ctl;
	int ahbcfg;
	int uframe_sched;
};

/**
 * struct dwc2_hw_params - Autodetected parameters.
 *
 * These parameters are the various parameters read from hardware
 * registers during initialization. They typically contain the best
 * supported or maximum value that can be configured in the
 * corresponding dwc2_core_params value.
 *
 * The values that are not in dwc2_core_params are documented below.
 *
 * @op_mode             Mode of Operation
 *                       0 - HNP- and SRP-Capable OTG (Host & Device)
 *                       1 - SRP-Capable OTG (Host & Device)
 *                       2 - Non-HNP and Non-SRP Capable OTG (Host & Device)
 *                       3 - SRP-Capable Device
 *                       4 - Non-OTG Device
 *                       5 - SRP-Capable Host
 *                       6 - Non-OTG Host
 * @arch                Architecture
 *                       0 - Slave only
 *                       1 - External DMA
 *                       2 - Internal DMA
 * @power_optimized     Are power optimizations enabled?
 * @num_dev_ep          Number of device endpoints available
 * @num_dev_perio_in_ep Number of device periodic IN endpoints
 *                      avaialable
 * @dev_token_q_depth   Device Mode IN Token Sequence Learning Queue
 *                      Depth
 *                       0 to 30
 * @host_perio_tx_q_depth
 *                      Host Mode Periodic Request Queue Depth
 *                       2, 4 or 8
 * @nperio_tx_q_depth
 *                      Non-Periodic Request Queue Depth
 *                       2, 4 or 8
 * @hs_phy_type         High-speed PHY interface type
 *                       0 - High-speed interface not supported
 *                       1 - UTMI+
 *                       2 - ULPI
 *                       3 - UTMI+ and ULPI
 * @fs_phy_type         Full-speed PHY interface type
 *                       0 - Full speed interface not supported
 *                       1 - Dedicated full speed interface
 *                       2 - FS pins shared with UTMI+ pins
 *                       3 - FS pins shared with ULPI pins
 * @total_fifo_size:    Total internal RAM for FIFOs (bytes)
 * @utmi_phy_data_width UTMI+ PHY data width
 *                       0 - 8 bits
 *                       1 - 16 bits
 *                       2 - 8 or 16 bits
 * @snpsid:             Value from SNPSID register
 */
struct dwc2_hw_params {
	unsigned op_mode:3;
	unsigned arch:2;
	unsigned dma_desc_enable:1;
	unsigned enable_dynamic_fifo:1;
	unsigned en_multiple_tx_fifo:1;
	unsigned host_rx_fifo_size:16;
	unsigned host_nperio_tx_fifo_size:16;
	unsigned host_perio_tx_fifo_size:16;
	unsigned nperio_tx_q_depth:3;
	unsigned host_perio_tx_q_depth:3;
	unsigned dev_token_q_depth:5;
	unsigned max_transfer_size:26;
	unsigned max_packet_count:11;
	unsigned host_channels:5;
	unsigned hs_phy_type:2;
	unsigned fs_phy_type:2;
	unsigned i2c_enable:1;
	unsigned num_dev_ep:4;
	unsigned num_dev_perio_in_ep:4;
	unsigned total_fifo_size:16;
	unsigned power_optimized:1;
	unsigned utmi_phy_data_width:2;
	u32 snpsid;
};

/**
 * struct dwc2_hsotg - Holds the state of the driver, including the non-periodic
 * and periodic schedules
 *
 * @dev:                The struct device pointer
 * @regs:		Pointer to controller regs
 * @core_params:        Parameters that define how the core should be configured
 * @hw_params:          Parameters that were autodetected from the
 *                      hardware registers
 * @op_state:           The operational State, during transitions (a_host=>
 *                      a_peripheral and b_device=>b_host) this may not match
 *                      the core, but allows the software to determine
 *                      transitions
 * @dr_mode:            Requested mode of operation, one of following:
 *                      - USB_DR_MODE_PERIPHERAL
 *                      - USB_DR_MODE_HOST
 *                      - USB_DR_MODE_OTG
 * @queuing_high_bandwidth: True if multiple packets of a high-bandwidth
 *                      transfer are in process of being queued
 * @srp_success:        Stores status of SRP request in the case of a FS PHY
 *                      with an I2C interface
 * @wq_otg:             Workqueue object used for handling of some interrupts
 * @wf_otg:             Work object for handling Connector ID Status Change
 *                      interrupt
 * @wkp_timer:          Timer object for handling Wakeup Detected interrupt
 * @lx_state:           Lx state of connected device
 * @flags:              Flags for handling root port state changes
 * @non_periodic_sched_inactive: Inactive QHs in the non-periodic schedule.
 *                      Transfers associated with these QHs are not currently
 *                      assigned to a host channel.
 * @non_periodic_sched_active: Active QHs in the non-periodic schedule.
 *                      Transfers associated with these QHs are currently
 *                      assigned to a host channel.
 * @non_periodic_qh_ptr: Pointer to next QH to process in the active
 *                      non-periodic schedule
 * @periodic_sched_inactive: Inactive QHs in the periodic schedule. This is a
 *                      list of QHs for periodic transfers that are _not_
 *                      scheduled for the next frame. Each QH in the list has an
 *                      interval counter that determines when it needs to be
 *                      scheduled for execution. This scheduling mechanism
 *                      allows only a simple calculation for periodic bandwidth
 *                      used (i.e. must assume that all periodic transfers may
 *                      need to execute in the same frame). However, it greatly
 *                      simplifies scheduling and should be sufficient for the
 *                      vast majority of OTG hosts, which need to connect to a
 *                      small number of peripherals at one time. Items move from
 *                      this list to periodic_sched_ready when the QH interval
 *                      counter is 0 at SOF.
 * @periodic_sched_ready:  List of periodic QHs that are ready for execution in
 *                      the next frame, but have not yet been assigned to host
 *                      channels. Items move from this list to
 *                      periodic_sched_assigned as host channels become
 *                      available during the current frame.
 * @periodic_sched_assigned: List of periodic QHs to be executed in the next
 *                      frame that are assigned to host channels. Items move
 *                      from this list to periodic_sched_queued as the
 *                      transactions for the QH are queued to the DWC_otg
 *                      controller.
 * @periodic_sched_queued: List of periodic QHs that have been queued for
 *                      execution. Items move from this list to either
 *                      periodic_sched_inactive or periodic_sched_ready when the
 *                      channel associated with the transfer is released. If the
 *                      interval for the QH is 1, the item moves to
 *                      periodic_sched_ready because it must be rescheduled for
 *                      the next frame. Otherwise, the item moves to
 *                      periodic_sched_inactive.
 * @periodic_usecs:     Total bandwidth claimed so far for periodic transfers.
 *                      This value is in microseconds per (micro)frame. The
 *                      assumption is that all periodic transfers may occur in
 *                      the same (micro)frame.
 * @frame_usecs:        Internal variable used by the microframe scheduler
 * @frame_number:       Frame number read from the core at SOF. The value ranges
 *                      from 0 to HFNUM_MAX_FRNUM.
 * @periodic_qh_count:  Count of periodic QHs, if using several eps. Used for
 *                      SOF enable/disable.
 * @free_hc_list:       Free host channels in the controller. This is a list of
 *                      struct dwc2_host_chan items.
 * @periodic_channels:  Number of host channels assigned to periodic transfers.
 *                      Currently assuming that there is a dedicated host
 *                      channel for each periodic transaction and at least one
 *                      host channel is available for non-periodic transactions.
 * @non_periodic_channels: Number of host channels assigned to non-periodic
 *                      transfers
 * @available_host_channels Number of host channels available for the microframe
 *                      scheduler to use
 * @hc_ptr_array:       Array of pointers to the host channel descriptors.
 *                      Allows accessing a host channel descriptor given the
 *                      host channel number. This is useful in interrupt
 *                      handlers.
 * @status_buf:         Buffer used for data received during the status phase of
 *                      a control transfer.
 * @status_buf_dma:     DMA address for status_buf
 * @start_work:         Delayed work for handling host A-cable connection
 * @reset_work:         Delayed work for handling a port reset
 * @lock:               Spinlock that protects all the driver data structures
 * @priv:               Stores a pointer to the struct usb_hcd
 * @otg_port:           OTG port number
 * @frame_list:         Frame list
 * @frame_list_dma:     Frame list DMA address
 */
struct dwc2_hsotg {
	struct device *dev;
	void __iomem *regs;
	/** Params detected from hardware */
	struct dwc2_hw_params hw_params;
	/** Params to actually use */
	struct dwc2_core_params *core_params;
	enum usb_otg_state op_state;
	enum usb_dr_mode dr_mode;

	unsigned int queuing_high_bandwidth:1;
	unsigned int srp_success:1;

	struct workqueue_struct *wq_otg;
	struct work_struct wf_otg;
	struct timer_list wkp_timer;
	enum dwc2_lx_state lx_state;

	union dwc2_hcd_internal_flags {
		u32 d32;
		struct {
			unsigned port_connect_status_change:1;
			unsigned port_connect_status:1;
			unsigned port_reset_change:1;
			unsigned port_enable_change:1;
			unsigned port_suspend_change:1;
			unsigned port_over_current_change:1;
			unsigned port_l1_change:1;
			unsigned reserved:26;
		} b;
	} flags;

	struct list_head non_periodic_sched_inactive;
	struct list_head non_periodic_sched_active;
	struct list_head *non_periodic_qh_ptr;
	struct list_head periodic_sched_inactive;
	struct list_head periodic_sched_ready;
	struct list_head periodic_sched_assigned;
	struct list_head periodic_sched_queued;
	u16 periodic_usecs;
	u16 frame_usecs[8];
	u16 frame_number;
	u16 periodic_qh_count;

#ifdef CONFIG_USB_DWC2_TRACK_MISSED_SOFS
#define FRAME_NUM_ARRAY_SIZE 1000
	u16 last_frame_num;
	u16 *frame_num_array;
	u16 *last_frame_num_array;
	int frame_num_idx;
	int dumped_frame_num_array;
#endif

	struct list_head free_hc_list;
	int periodic_channels;
	int non_periodic_channels;
	int available_host_channels;
	struct dwc2_host_chan *hc_ptr_array[MAX_EPS_CHANNELS];
	u8 *status_buf;
	dma_addr_t status_buf_dma;
#define DWC2_HCD_STATUS_BUF_SIZE 64

	struct delayed_work start_work;
	struct delayed_work reset_work;
	spinlock_t lock;
	void *priv;
	u8 otg_port;
	u32 *frame_list;
	dma_addr_t frame_list_dma;

	/* DWC OTG HW Release versions */
#define DWC2_CORE_REV_2_71a	0x4f54271a
#define DWC2_CORE_REV_2_90a	0x4f54290a
#define DWC2_CORE_REV_2_92a	0x4f54292a
#define DWC2_CORE_REV_2_94a	0x4f54294a
#define DWC2_CORE_REV_3_00a	0x4f54300a

#ifdef DEBUG
	u32 frrem_samples;
	u64 frrem_accum;

	u32 hfnum_7_samples_a;
	u64 hfnum_7_frrem_accum_a;
	u32 hfnum_0_samples_a;
	u64 hfnum_0_frrem_accum_a;
	u32 hfnum_other_samples_a;
	u64 hfnum_other_frrem_accum_a;

	u32 hfnum_7_samples_b;
	u64 hfnum_7_frrem_accum_b;
	u32 hfnum_0_samples_b;
	u64 hfnum_0_frrem_accum_b;
	u32 hfnum_other_samples_b;
	u64 hfnum_other_frrem_accum_b;
#endif
};

/* Reasons for halting a host channel */
enum dwc2_halt_status {
	DWC2_HC_XFER_NO_HALT_STATUS,
	DWC2_HC_XFER_COMPLETE,
	DWC2_HC_XFER_URB_COMPLETE,
	DWC2_HC_XFER_ACK,
	DWC2_HC_XFER_NAK,
	DWC2_HC_XFER_NYET,
	DWC2_HC_XFER_STALL,
	DWC2_HC_XFER_XACT_ERR,
	DWC2_HC_XFER_FRAME_OVERRUN,
	DWC2_HC_XFER_BABBLE_ERR,
	DWC2_HC_XFER_DATA_TOGGLE_ERR,
	DWC2_HC_XFER_AHB_ERR,
	DWC2_HC_XFER_PERIODIC_INCOMPLETE,
	DWC2_HC_XFER_URB_DEQUEUE,
};

/*
 * The following functions support initialization of the core driver component
 * and the DWC_otg controller
 */
extern void dwc2_core_host_init(struct dwc2_hsotg *hsotg);

/*
 * Host core Functions.
 * The following functions support managing the DWC_otg controller in host
 * mode.
 */
extern void dwc2_hc_init(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan);
extern void dwc2_hc_halt(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan,
			 enum dwc2_halt_status halt_status);
extern void dwc2_hc_cleanup(struct dwc2_hsotg *hsotg,
			    struct dwc2_host_chan *chan);
extern void dwc2_hc_start_transfer(struct dwc2_hsotg *hsotg,
				   struct dwc2_host_chan *chan);
extern void dwc2_hc_start_transfer_ddma(struct dwc2_hsotg *hsotg,
					struct dwc2_host_chan *chan);
extern int dwc2_hc_continue_transfer(struct dwc2_hsotg *hsotg,
				     struct dwc2_host_chan *chan);
extern void dwc2_hc_do_ping(struct dwc2_hsotg *hsotg,
			    struct dwc2_host_chan *chan);
extern void dwc2_enable_host_interrupts(struct dwc2_hsotg *hsotg);
extern void dwc2_disable_host_interrupts(struct dwc2_hsotg *hsotg);

extern u32 dwc2_calc_frame_interval(struct dwc2_hsotg *hsotg);
extern bool dwc2_is_controller_alive(struct dwc2_hsotg *hsotg);

/*
 * Common core Functions.
 * The following functions support managing the DWC_otg controller in either
 * device or host mode.
 */
extern void dwc2_read_packet(struct dwc2_hsotg *hsotg, u8 *dest, u16 bytes);
extern void dwc2_flush_tx_fifo(struct dwc2_hsotg *hsotg, const int num);
extern void dwc2_flush_rx_fifo(struct dwc2_hsotg *hsotg);

extern int dwc2_core_init(struct dwc2_hsotg *hsotg, bool select_phy, int irq);
extern void dwc2_enable_global_interrupts(struct dwc2_hsotg *hcd);
extern void dwc2_disable_global_interrupts(struct dwc2_hsotg *hcd);

/* This function should be called on every hardware interrupt. */
extern irqreturn_t dwc2_handle_common_intr(int irq, void *dev);

/* OTG Core Parameters */

/*
 * Specifies the OTG capabilities. The driver will automatically
 * detect the value for this parameter if none is specified.
 * 0 - HNP and SRP capable (default)
 * 1 - SRP Only capable
 * 2 - No HNP/SRP capable
 */
extern void dwc2_set_param_otg_cap(struct dwc2_hsotg *hsotg, int val);
#define DWC2_CAP_PARAM_HNP_SRP_CAPABLE		0
#define DWC2_CAP_PARAM_SRP_ONLY_CAPABLE		1
#define DWC2_CAP_PARAM_NO_HNP_SRP_CAPABLE	2

/*
 * Specifies whether to use slave or DMA mode for accessing the data
 * FIFOs. The driver will automatically detect the value for this
 * parameter if none is specified.
 * 0 - Slave
 * 1 - DMA (default, if available)
 */
extern void dwc2_set_param_dma_enable(struct dwc2_hsotg *hsotg, int val);

/*
 * When DMA mode is enabled specifies whether to use
 * address DMA or DMA Descritor mode for accessing the data
 * FIFOs in device mode. The driver will automatically detect
 * the value for this parameter if none is specified.
 * 0 - address DMA
 * 1 - DMA Descriptor(default, if available)
 */
extern void dwc2_set_param_dma_desc_enable(struct dwc2_hsotg *hsotg, int val);

/*
 * Specifies the maximum speed of operation in host and device mode.
 * The actual speed depends on the speed of the attached device and
 * the value of phy_type. The actual speed depends on the speed of the
 * attached device.
 * 0 - High Speed (default)
 * 1 - Full Speed
 */
extern void dwc2_set_param_speed(struct dwc2_hsotg *hsotg, int val);
#define DWC2_SPEED_PARAM_HIGH	0
#define DWC2_SPEED_PARAM_FULL	1

/*
 * Specifies whether low power mode is supported when attached
 * to a Full Speed or Low Speed device in host mode.
 *
 * 0 - Don't support low power mode (default)
 * 1 - Support low power mode
 */
extern void dwc2_set_param_host_support_fs_ls_low_power(
		struct dwc2_hsotg *hsotg, int val);

/*
 * Specifies the PHY clock rate in low power mode when connected to a
 * Low Speed device in host mode. This parameter is applicable only if
 * HOST_SUPPORT_FS_LS_LOW_POWER is enabled. If PHY_TYPE is set to FS
 * then defaults to 6 MHZ otherwise 48 MHZ.
 *
 * 0 - 48 MHz
 * 1 - 6 MHz
 */
extern void dwc2_set_param_host_ls_low_power_phy_clk(struct dwc2_hsotg *hsotg,
						     int val);
#define DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ	0
#define DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ	1

/*
 * 0 - Use cC FIFO size parameters
 * 1 - Allow dynamic FIFO sizing (default)
 */
extern void dwc2_set_param_enable_dynamic_fifo(struct dwc2_hsotg *hsotg,
					       int val);

/*
 * Number of 4-byte words in the Rx FIFO in host mode when dynamic
 * FIFO sizing is enabled.
 * 16 to 32768 (default 1024)
 */
extern void dwc2_set_param_host_rx_fifo_size(struct dwc2_hsotg *hsotg, int val);

/*
 * Number of 4-byte words in the non-periodic Tx FIFO in host mode
 * when Dynamic FIFO sizing is enabled in the core.
 * 16 to 32768 (default 256)
 */
extern void dwc2_set_param_host_nperio_tx_fifo_size(struct dwc2_hsotg *hsotg,
						    int val);

/*
 * Number of 4-byte words in the host periodic Tx FIFO when dynamic
 * FIFO sizing is enabled.
 * 16 to 32768 (default 256)
 */
extern void dwc2_set_param_host_perio_tx_fifo_size(struct dwc2_hsotg *hsotg,
						   int val);

/*
 * The maximum transfer size supported in bytes.
 * 2047 to 65,535  (default 65,535)
 */
extern void dwc2_set_param_max_transfer_size(struct dwc2_hsotg *hsotg, int val);

/*
 * The maximum number of packets in a transfer.
 * 15 to 511  (default 511)
 */
extern void dwc2_set_param_max_packet_count(struct dwc2_hsotg *hsotg, int val);

/*
 * The number of host channel registers to use.
 * 1 to 16 (default 11)
 * Note: The FPGA configuration supports a maximum of 11 host channels.
 */
extern void dwc2_set_param_host_channels(struct dwc2_hsotg *hsotg, int val);

/*
 * Specifies the type of PHY interface to use. By default, the driver
 * will automatically detect the phy_type.
 *
 * 0 - Full Speed PHY
 * 1 - UTMI+ (default)
 * 2 - ULPI
 */
extern void dwc2_set_param_phy_type(struct dwc2_hsotg *hsotg, int val);
#define DWC2_PHY_TYPE_PARAM_FS		0
#define DWC2_PHY_TYPE_PARAM_UTMI	1
#define DWC2_PHY_TYPE_PARAM_ULPI	2

/*
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
extern void dwc2_set_param_phy_utmi_width(struct dwc2_hsotg *hsotg, int val);

/*
 * Specifies whether the ULPI operates at double or single
 * data rate. This parameter is only applicable if PHY_TYPE is
 * ULPI.
 *
 * 0 - single data rate ULPI interface with 8 bit wide data
 * bus (default)
 * 1 - double data rate ULPI interface with 4 bit wide data
 * bus
 */
extern void dwc2_set_param_phy_ulpi_ddr(struct dwc2_hsotg *hsotg, int val);

/*
 * Specifies whether to use the internal or external supply to
 * drive the vbus with a ULPI phy.
 */
extern void dwc2_set_param_phy_ulpi_ext_vbus(struct dwc2_hsotg *hsotg, int val);
#define DWC2_PHY_ULPI_INTERNAL_VBUS	0
#define DWC2_PHY_ULPI_EXTERNAL_VBUS	1

/*
 * Specifies whether to use the I2Cinterface for full speed PHY. This
 * parameter is only applicable if PHY_TYPE is FS.
 * 0 - No (default)
 * 1 - Yes
 */
extern void dwc2_set_param_i2c_enable(struct dwc2_hsotg *hsotg, int val);

extern void dwc2_set_param_ulpi_fs_ls(struct dwc2_hsotg *hsotg, int val);

extern void dwc2_set_param_ts_dline(struct dwc2_hsotg *hsotg, int val);

/*
 * Specifies whether dedicated transmit FIFOs are
 * enabled for non periodic IN endpoints in device mode
 * 0 - No
 * 1 - Yes
 */
extern void dwc2_set_param_en_multiple_tx_fifo(struct dwc2_hsotg *hsotg,
					       int val);

extern void dwc2_set_param_reload_ctl(struct dwc2_hsotg *hsotg, int val);

extern void dwc2_set_param_ahbcfg(struct dwc2_hsotg *hsotg, int val);

extern void dwc2_set_param_otg_ver(struct dwc2_hsotg *hsotg, int val);

/*
 * Dump core registers and SPRAM
 */
extern void dwc2_dump_dev_registers(struct dwc2_hsotg *hsotg);
extern void dwc2_dump_host_registers(struct dwc2_hsotg *hsotg);
extern void dwc2_dump_global_registers(struct dwc2_hsotg *hsotg);

/*
 * Return OTG version - either 1.3 or 2.0
 */
extern u16 dwc2_get_otg_version(struct dwc2_hsotg *hsotg);

#endif /* __DWC2_CORE_H__ */
