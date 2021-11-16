/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-21 Intel Corporation.
 */

#ifndef IOSM_IPC_IMEM_H
#define IOSM_IPC_IMEM_H

#include <linux/skbuff.h>

#include "iosm_ipc_mmio.h"
#include "iosm_ipc_pcie.h"
#include "iosm_ipc_uevent.h"
#include "iosm_ipc_wwan.h"
#include "iosm_ipc_task_queue.h"

struct ipc_chnl_cfg;

/* IRQ moderation in usec */
#define IRQ_MOD_OFF 0
#define IRQ_MOD_NET 1000
#define IRQ_MOD_TRC 4000

/* Either the PSI image is accepted by CP or the suspended flash tool is waken,
 * informed that the CP ROM driver is not ready to process the PSI image.
 * unit : milliseconds
 */
#define IPC_PSI_TRANSFER_TIMEOUT 3000

/* Timeout in 20 msec to wait for the modem to boot up to
 * IPC_MEM_DEVICE_IPC_INIT state.
 * unit : milliseconds (500 * ipc_util_msleep(20))
 */
#define IPC_MODEM_BOOT_TIMEOUT 500

/* Wait timeout for ipc status reflects IPC_MEM_DEVICE_IPC_UNINIT
 * unit : milliseconds
 */
#define IPC_MODEM_UNINIT_TIMEOUT_MS 30

/* Pending time for processing data.
 * unit : milliseconds
 */
#define IPC_PEND_DATA_TIMEOUT 500

/* The timeout in milliseconds for application to wait for remote time. */
#define IPC_REMOTE_TS_TIMEOUT_MS 10

/* Timeout for TD allocation retry.
 * unit : milliseconds
 */
#define IPC_TD_ALLOC_TIMER_PERIOD_MS 100

/* Host sleep target is host */
#define IPC_HOST_SLEEP_HOST 0

/* Host sleep target is device */
#define IPC_HOST_SLEEP_DEVICE 1

/* Sleep message, target host: AP enters sleep / target device: CP is
 * allowed to enter sleep and shall use the host sleep protocol
 */
#define IPC_HOST_SLEEP_ENTER_SLEEP 0

/* Sleep_message, target host: AP exits  sleep / target device: CP is
 * NOT allowed to enter sleep
 */
#define IPC_HOST_SLEEP_EXIT_SLEEP 1

#define IMEM_IRQ_DONT_CARE (-1)

#define IPC_MEM_MAX_CHANNELS 8

#define IPC_MEM_MUX_IP_SESSION_ENTRIES 8

#define IPC_MEM_MUX_IP_CH_IF_ID 0

#define TD_UPDATE_DEFAULT_TIMEOUT_USEC 1900

#define FORCE_UPDATE_DEFAULT_TIMEOUT_USEC 500

/* Sleep_message, target host: not applicable  / target device: CP is
 * allowed to enter sleep and shall NOT use the device sleep protocol
 */
#define IPC_HOST_SLEEP_ENTER_SLEEP_NO_PROTOCOL 2

/* in_band_crash_signal IPC_MEM_INBAND_CRASH_SIG
 * Modem crash notification configuration. If this value is non-zero then
 * FEATURE_SET message will be sent to the Modem as a result the Modem will
 * signal Crash via Execution Stage register. If this value is zero then Modem
 * will use out-of-band method to notify about it's Crash.
 */
#define IPC_MEM_INBAND_CRASH_SIG 1

/* Extra headroom to be allocated for DL SKBs to allow addition of Ethernet
 * header
 */
#define IPC_MEM_DL_ETH_OFFSET 16

#define IPC_CB(skb) ((struct ipc_skb_cb *)((skb)->cb))
#define IOSM_CHIP_INFO_SIZE_MAX 100

#define FULLY_FUNCTIONAL 0

/* List of the supported UL/DL pipes. */
enum ipc_mem_pipes {
	IPC_MEM_PIPE_0 = 0,
	IPC_MEM_PIPE_1,
	IPC_MEM_PIPE_2,
	IPC_MEM_PIPE_3,
	IPC_MEM_PIPE_4,
	IPC_MEM_PIPE_5,
	IPC_MEM_PIPE_6,
	IPC_MEM_PIPE_7,
	IPC_MEM_PIPE_8,
	IPC_MEM_PIPE_9,
	IPC_MEM_PIPE_10,
	IPC_MEM_PIPE_11,
	IPC_MEM_PIPE_12,
	IPC_MEM_PIPE_13,
	IPC_MEM_PIPE_14,
	IPC_MEM_PIPE_15,
	IPC_MEM_PIPE_16,
	IPC_MEM_PIPE_17,
	IPC_MEM_PIPE_18,
	IPC_MEM_PIPE_19,
	IPC_MEM_PIPE_20,
	IPC_MEM_PIPE_21,
	IPC_MEM_PIPE_22,
	IPC_MEM_PIPE_23,
	IPC_MEM_MAX_PIPES
};

/* Enum defining channel states. */
enum ipc_channel_state {
	IMEM_CHANNEL_FREE,
	IMEM_CHANNEL_RESERVED,
	IMEM_CHANNEL_ACTIVE,
	IMEM_CHANNEL_CLOSING,
};

/* Time Unit */
enum ipc_time_unit {
	IPC_SEC = 0,
	IPC_MILLI_SEC = 1,
	IPC_MICRO_SEC = 2,
	IPC_NANO_SEC = 3,
	IPC_PICO_SEC = 4,
	IPC_FEMTO_SEC = 5,
	IPC_ATTO_SEC = 6,
};

/**
 * enum ipc_ctype - Enum defining supported channel type needed for control
 *		    /IP traffic.
 * @IPC_CTYPE_WWAN:		Used for IP traffic
 * @IPC_CTYPE_CTRL:		Used for Control Communication
 */
enum ipc_ctype {
	IPC_CTYPE_WWAN,
	IPC_CTYPE_CTRL,
};

/* Pipe direction. */
enum ipc_mem_pipe_dir {
	IPC_MEM_DIR_UL,
	IPC_MEM_DIR_DL,
};

/* HP update identifier. To be used as data for ipc_cp_irq_hpda_update() */
enum ipc_hp_identifier {
	IPC_HP_MR = 0,
	IPC_HP_PM_TRIGGER,
	IPC_HP_WAKEUP_SPEC_TMR,
	IPC_HP_TD_UPD_TMR_START,
	IPC_HP_TD_UPD_TMR,
	IPC_HP_FAST_TD_UPD_TMR,
	IPC_HP_UL_WRITE_TD,
	IPC_HP_DL_PROCESS,
	IPC_HP_NET_CHANNEL_INIT,
	IPC_HP_CDEV_OPEN,
};

/**
 * struct ipc_pipe - Structure for Pipe.
 * @tdr_start:			Ipc private protocol Transfer Descriptor Ring
 * @channel:			Id of the sio device, set by imem_sio_open,
 *				needed to pass DL char to the user terminal
 * @skbr_start:			Circular buffer for skbuf and the buffer
 *				reference in a tdr_start entry.
 * @phy_tdr_start:		Transfer descriptor start address
 * @old_head:			last head pointer reported to CP.
 * @old_tail:			AP read position before CP moves the read
 *				position to write/head. If CP has consumed the
 *				buffers, AP has to freed the skbuf starting at
 *				tdr_start[old_tail].
 * @nr_of_entries:		Number of elements of skb_start and tdr_start.
 * @max_nr_of_queued_entries:	Maximum number of queued entries in TDR
 * @accumulation_backoff:	Accumulation in usec for accumulation
 *				backoff (0 = no acc backoff)
 * @irq_moderation:		timer in usec for irq_moderation
 *				(0=no irq moderation)
 * @pipe_nr:			Pipe identification number
 * @irq:			Interrupt vector
 * @dir:			Direction of data stream in pipe
 * @td_tag:			Unique tag of the buffer queued
 * @buf_size:			Buffer size (in bytes) for preallocated
 *				buffers (for DL pipes)
 * @nr_of_queued_entries:	Aueued number of entries
 * @is_open:			Check for open pipe status
 */
struct ipc_pipe {
	struct ipc_protocol_td *tdr_start;
	struct ipc_mem_channel *channel;
	struct sk_buff **skbr_start;
	dma_addr_t phy_tdr_start;
	u32 old_head;
	u32 old_tail;
	u32 nr_of_entries;
	u32 max_nr_of_queued_entries;
	u32 accumulation_backoff;
	u32 irq_moderation;
	u32 pipe_nr;
	u32 irq;
	enum ipc_mem_pipe_dir dir;
	u32 td_tag;
	u32 buf_size;
	u16 nr_of_queued_entries;
	u8 is_open:1;
};

/**
 * struct ipc_mem_channel - Structure for Channel.
 * @channel_id:		Instance of the channel list and is return to the user
 *			at the end of the open operation.
 * @ctype:		Control or netif channel.
 * @index:		unique index per ctype
 * @ul_pipe:		pipe objects
 * @dl_pipe:		pipe objects
 * @if_id:		Interface ID
 * @net_err_count:	Number of downlink errors returned by ipc_wwan_receive
 *			interface at the entry point of the IP stack.
 * @state:		Free, reserved or busy (in use).
 * @ul_sem:		Needed for the blocking write or uplink transfer.
 * @ul_list:		Uplink accumulator which is filled by the uplink
 *			char app or IP stack. The socket buffer pointer are
 *			added to the descriptor list in the kthread context.
 */
struct ipc_mem_channel {
	int channel_id;
	enum ipc_ctype ctype;
	int index;
	struct ipc_pipe ul_pipe;
	struct ipc_pipe dl_pipe;
	int if_id;
	u32 net_err_count;
	enum ipc_channel_state state;
	struct completion ul_sem;
	struct sk_buff_head ul_list;
};

/**
 * enum ipc_phase - Different AP and CP phases.
 *		    The enums defined after "IPC_P_ROM" and before
 *		    "IPC_P_RUN" indicates the operating state where CP can
 *		    respond to any requests. So while introducing new phase
 *		    this shall be taken into consideration.
 * @IPC_P_OFF:		On host PC, the PCIe device link settings are known
 *			about the combined power on. PC is running, the driver
 *			is loaded and CP is in power off mode. The PCIe bus
 *			driver call the device power mode D3hot. In this phase
 *			the driver the polls the device, until the device is in
 *			the power on state and signals the power mode D0.
 * @IPC_P_OFF_REQ:	The intermediate phase between cleanup activity starts
 *			and ends.
 * @IPC_P_CRASH:	The phase indicating CP crash
 * @IPC_P_CD_READY:	The phase indicating CP core dump is ready
 * @IPC_P_ROM:		After power on, CP starts in ROM mode and the IPC ROM
 *			driver is waiting 150 ms for the AP active notification
 *			saved in the PCI link status register.
 * @IPC_P_PSI:		Primary signed image download phase
 * @IPC_P_EBL:		Extended bootloader pahse
 * @IPC_P_RUN:		The phase after flashing to RAM is the RUNTIME phase.
 */
enum ipc_phase {
	IPC_P_OFF,
	IPC_P_OFF_REQ,
	IPC_P_CRASH,
	IPC_P_CD_READY,
	IPC_P_ROM,
	IPC_P_PSI,
	IPC_P_EBL,
	IPC_P_RUN,
};

/**
 * struct iosm_imem - Current state of the IPC shared memory.
 * @mmio:			mmio instance to access CP MMIO area /
 *				doorbell scratchpad.
 * @ipc_protocol:		IPC Protocol instance
 * @ipc_task:			Task for entry into ipc task queue
 * @wwan:			WWAN device pointer
 * @mux:			IP Data multiplexing state.
 * @sio:			IPC SIO data structure pointer
 * @ipc_port:			IPC PORT data structure pointer
 * @pcie:			IPC PCIe
 * @dev:			Pointer to device structure
 * @ipc_requested_state:	Expected IPC state on CP.
 * @channels:			Channel list with UL/DL pipe pairs.
 * @ipc_devlink:		IPC Devlink data structure pointer
 * @ipc_status:			local ipc_status
 * @nr_of_channels:		number of configured channels
 * @startup_timer:		startup timer for NAND support.
 * @hrtimer_period:		Hr timer period
 * @tdupdate_timer:		Delay the TD update doorbell.
 * @fast_update_timer:		forced head pointer update delay timer.
 * @td_alloc_timer:		Timer for DL pipe TD allocation retry
 * @rom_exit_code:		Mapped boot rom exit code.
 * @enter_runtime:		1 means the transition to runtime phase was
 *				executed.
 * @ul_pend_sem:		Semaphore to wait/complete of UL TDs
 *				before closing pipe.
 * @app_notify_ul_pend:		Signal app if UL TD is pending
 * @dl_pend_sem:		Semaphore to wait/complete of DL TDs
 *				before closing pipe.
 * @app_notify_dl_pend:		Signal app if DL TD is pending
 * @phase:			Operating phase like runtime.
 * @pci_device_id:		Device ID
 * @cp_version:			CP version
 * @device_sleep:		Device sleep state
 * @run_state_worker:		Pointer to worker component for device
 *				setup operations to be called when modem
 *				reaches RUN state
 * @ev_irq_pending:		0 means inform the IPC tasklet to
 *				process the irq actions.
 * @flag:			Flag to monitor the state of driver
 * @td_update_timer_suspended:	if true then td update timer suspend
 * @ev_cdev_write_pending:	0 means inform the IPC tasklet to pass
 *				the accumulated uplink buffers to CP.
 * @ev_mux_net_transmit_pending:0 means inform the IPC tasklet to pass
 * @reset_det_n:		Reset detect flag
 * @pcie_wake_n:		Pcie wake flag
 */
struct iosm_imem {
	struct iosm_mmio *mmio;
	struct iosm_protocol *ipc_protocol;
	struct ipc_task *ipc_task;
	struct iosm_wwan *wwan;
	struct iosm_mux *mux;
	struct iosm_cdev *ipc_port[IPC_MEM_MAX_CHANNELS];
	struct iosm_pcie *pcie;
	struct device *dev;
	enum ipc_mem_device_ipc_state ipc_requested_state;
	struct ipc_mem_channel channels[IPC_MEM_MAX_CHANNELS];
	struct iosm_devlink *ipc_devlink;
	u32 ipc_status;
	u32 nr_of_channels;
	struct hrtimer startup_timer;
	ktime_t hrtimer_period;
	struct hrtimer tdupdate_timer;
	struct hrtimer fast_update_timer;
	struct hrtimer td_alloc_timer;
	enum rom_exit_code rom_exit_code;
	u32 enter_runtime;
	struct completion ul_pend_sem;
	u32 app_notify_ul_pend;
	struct completion dl_pend_sem;
	u32 app_notify_dl_pend;
	enum ipc_phase phase;
	u16 pci_device_id;
	int cp_version;
	int device_sleep;
	struct work_struct run_state_worker;
	u8 ev_irq_pending[IPC_IRQ_VECTORS];
	unsigned long flag;
	u8 td_update_timer_suspended:1,
	   ev_cdev_write_pending:1,
	   ev_mux_net_transmit_pending:1,
	   reset_det_n:1,
	   pcie_wake_n:1;
};

/**
 * ipc_imem_init - Initialize the shared memory region
 * @pcie:	Pointer to core driver data-struct
 * @device_id:	PCI device ID
 * @mmio:	Pointer to the mmio area
 * @dev:	Pointer to device structure
 *
 * Returns:  Initialized imem pointer on success else NULL
 */
struct iosm_imem *ipc_imem_init(struct iosm_pcie *pcie, unsigned int device_id,
				void __iomem *mmio, struct device *dev);

/**
 * ipc_imem_pm_s2idle_sleep - Set PM variables to sleep/active for
 *			      s2idle sleep/active
 * @ipc_imem:	Pointer to imem data-struct
 * @sleep:	Set PM Variable to sleep/active
 */
void ipc_imem_pm_s2idle_sleep(struct iosm_imem *ipc_imem, bool sleep);

/**
 * ipc_imem_pm_suspend - The HAL shall ask the shared memory layer
 *			 whether D3 is allowed.
 * @ipc_imem:	Pointer to imem data-struct
 */
void ipc_imem_pm_suspend(struct iosm_imem *ipc_imem);

/**
 * ipc_imem_pm_resume - The HAL shall inform the shared memory layer
 *			that the device is active.
 * @ipc_imem:	Pointer to imem data-struct
 */
void ipc_imem_pm_resume(struct iosm_imem *ipc_imem);

/**
 * ipc_imem_cleanup -	Inform CP and free the shared memory resources.
 * @ipc_imem:	Pointer to imem data-struct
 */
void ipc_imem_cleanup(struct iosm_imem *ipc_imem);

/**
 * ipc_imem_irq_process - Shift the IRQ actions to the IPC thread.
 * @ipc_imem:	Pointer to imem data-struct
 * @irq:	Irq number
 */
void ipc_imem_irq_process(struct iosm_imem *ipc_imem, int irq);

/**
 * imem_get_device_sleep_state - Get the device sleep state value.
 * @ipc_imem:	Pointer to imem instance
 *
 * Returns: device sleep state
 */
int imem_get_device_sleep_state(struct iosm_imem *ipc_imem);

/**
 * ipc_imem_td_update_timer_suspend - Updates the TD Update Timer suspend flag.
 * @ipc_imem:	Pointer to imem data-struct
 * @suspend:	Flag to update. If TRUE then HP update doorbell is triggered to
 *		device without any wait. If FALSE then HP update doorbell is
 *		delayed until timeout.
 */
void ipc_imem_td_update_timer_suspend(struct iosm_imem *ipc_imem, bool suspend);

/**
 * ipc_imem_channel_close - Release the channel resources.
 * @ipc_imem:		Pointer to imem data-struct
 * @channel_id:		Channel ID to be cleaned up.
 */
void ipc_imem_channel_close(struct iosm_imem *ipc_imem, int channel_id);

/**
 * ipc_imem_channel_alloc - Reserves a channel
 * @ipc_imem:	Pointer to imem data-struct
 * @index:	ID to lookup from the preallocated list.
 * @ctype:	Channel type.
 *
 * Returns: Index on success and failure value on error
 */
int ipc_imem_channel_alloc(struct iosm_imem *ipc_imem, int index,
			   enum ipc_ctype ctype);

/**
 * ipc_imem_channel_open - Establish the pipes.
 * @ipc_imem:		Pointer to imem data-struct
 * @channel_id:		Channel ID returned during alloc.
 * @db_id:		Doorbell ID for trigger identifier.
 *
 * Returns: Pointer of ipc_mem_channel on success and NULL on failure.
 */
struct ipc_mem_channel *ipc_imem_channel_open(struct iosm_imem *ipc_imem,
					      int channel_id, u32 db_id);

/**
 * ipc_imem_td_update_timer_start - Starts the TD Update Timer if not running.
 * @ipc_imem:	Pointer to imem data-struct
 */
void ipc_imem_td_update_timer_start(struct iosm_imem *ipc_imem);

/**
 * ipc_imem_ul_write_td - Pass the channel UL list to protocol layer for TD
 *		      preparation and sending them to the device.
 * @ipc_imem:	Pointer to imem data-struct
 *
 * Returns: TRUE of HP Doorbell trigger is pending. FALSE otherwise.
 */
bool ipc_imem_ul_write_td(struct iosm_imem *ipc_imem);

/**
 * ipc_imem_ul_send - Dequeue SKB from channel list and start with
 *		  the uplink transfer.If HP Doorbell is pending to be
 *		  triggered then starts the TD Update Timer.
 * @ipc_imem:	Pointer to imem data-struct
 */
void ipc_imem_ul_send(struct iosm_imem *ipc_imem);

/**
 * ipc_imem_channel_update - Set or modify pipe config of an existing channel
 * @ipc_imem:		Pointer to imem data-struct
 * @id:			Channel config index
 * @chnl_cfg:		Channel config struct
 * @irq_moderation:	Timer in usec for irq_moderation
 */
void ipc_imem_channel_update(struct iosm_imem *ipc_imem, int id,
			     struct ipc_chnl_cfg chnl_cfg, u32 irq_moderation);

/**
 * ipc_imem_channel_free -Free an IPC channel.
 * @channel:	Channel to be freed
 */
void ipc_imem_channel_free(struct ipc_mem_channel *channel);

/**
 * ipc_imem_hrtimer_stop - Stop the hrtimer
 * @hr_timer:	Pointer to hrtimer instance
 */
void ipc_imem_hrtimer_stop(struct hrtimer *hr_timer);

/**
 * ipc_imem_pipe_cleanup - Reset volatile pipe content for all channels
 * @ipc_imem:	Pointer to imem data-struct
 * @pipe:	Pipe to cleaned up
 */
void ipc_imem_pipe_cleanup(struct iosm_imem *ipc_imem, struct ipc_pipe *pipe);

/**
 * ipc_imem_pipe_close - Send msg to device to close pipe
 * @ipc_imem:	Pointer to imem data-struct
 * @pipe:	Pipe to be closed
 */
void ipc_imem_pipe_close(struct iosm_imem *ipc_imem, struct ipc_pipe *pipe);

/**
 * ipc_imem_phase_update - Get the CP execution state
 *			  and map it to the AP phase.
 * @ipc_imem:	Pointer to imem data-struct
 *
 * Returns: Current ap updated phase
 */
enum ipc_phase ipc_imem_phase_update(struct iosm_imem *ipc_imem);

/**
 * ipc_imem_phase_get_string - Return the current operation
 *			     phase as string.
 * @phase:	AP phase
 *
 * Returns: AP phase string
 */
const char *ipc_imem_phase_get_string(enum ipc_phase phase);

/**
 * ipc_imem_msg_send_feature_set - Send feature set message to modem
 * @ipc_imem:		Pointer to imem data-struct
 * @reset_enable:	0 = out-of-band, 1 = in-band-crash notification
 * @atomic_ctx:		if disabled call in tasklet context
 *
 */
void ipc_imem_msg_send_feature_set(struct iosm_imem *ipc_imem,
				   unsigned int reset_enable, bool atomic_ctx);

/**
 * ipc_imem_ipc_init_check - Send the init event to CP, wait a certain time and
 *			     set CP to runtime with the context information
 * @ipc_imem:	Pointer to imem data-struct
 */
void ipc_imem_ipc_init_check(struct iosm_imem *ipc_imem);

/**
 * ipc_imem_channel_init - Initialize the channel list with UL/DL pipe pairs.
 * @ipc_imem:		Pointer to imem data-struct
 * @ctype:		Channel type
 * @chnl_cfg:		Channel configuration struct
 * @irq_moderation:	Timer in usec for irq_moderation
 */
void ipc_imem_channel_init(struct iosm_imem *ipc_imem, enum ipc_ctype ctype,
			   struct ipc_chnl_cfg chnl_cfg, u32 irq_moderation);

/**
 * ipc_imem_devlink_trigger_chip_info - Inform devlink that the chip
 *					information are available if the
 *					flashing to RAM interworking shall be
 *					executed.
 * @ipc_imem:	Pointer to imem structure
 *
 * Returns: 0 on success, -1 on failure
 */
int ipc_imem_devlink_trigger_chip_info(struct iosm_imem *ipc_imem);
#endif
