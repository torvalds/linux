/**********************************************************************
* Author: Cavium, Inc.
*
* Contact: support@cavium.com
*          Please include "LiquidIO" in the subject.
*
* Copyright (c) 2003-2015 Cavium, Inc.
*
* This file is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, Version 2, as
* published by the Free Software Foundation.
*
* This file is distributed in the hope that it will be useful, but
* AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
* NONINFRINGEMENT.  See the GNU General Public License for more
* details.
*
* This file may also be available under a different license from Cavium.
* Contact Cavium, Inc. for more information
**********************************************************************/

/*! \file octeon_device.h
 *  \brief Host Driver: This file defines the octeon device structure.
 */

#ifndef _OCTEON_DEVICE_H_
#define  _OCTEON_DEVICE_H_

/** PCI VendorId Device Id */
#define  OCTEON_CN68XX_PCIID          0x91177d
#define  OCTEON_CN66XX_PCIID          0x92177d

/** Driver identifies chips by these Ids, created by clubbing together
 *  DeviceId+RevisionId; Where Revision Id is not used to distinguish
 *  between chips, a value of 0 is used for revision id.
 */
#define  OCTEON_CN68XX                0x0091
#define  OCTEON_CN66XX                0x0092

/** Endian-swap modes supported by Octeon. */
enum octeon_pci_swap_mode {
	OCTEON_PCI_PASSTHROUGH = 0,
	OCTEON_PCI_64BIT_SWAP = 1,
	OCTEON_PCI_32BIT_BYTE_SWAP = 2,
	OCTEON_PCI_32BIT_LW_SWAP = 3
};

/*---------------   PCI BAR1 index registers -------------*/

/* BAR1 Mask */
#define    PCI_BAR1_ENABLE_CA            1
#define    PCI_BAR1_ENDIAN_MODE          OCTEON_PCI_64BIT_SWAP
#define    PCI_BAR1_ENTRY_VALID          1
#define    PCI_BAR1_MASK                 ((PCI_BAR1_ENABLE_CA << 3)   \
					    | (PCI_BAR1_ENDIAN_MODE << 1) \
					    | PCI_BAR1_ENTRY_VALID)

/** Octeon Device state.
 *  Each octeon device goes through each of these states
 *  as it is initialized.
 */
#define    OCT_DEV_BEGIN_STATE            0x0
#define    OCT_DEV_PCI_MAP_DONE           0x1
#define    OCT_DEV_DISPATCH_INIT_DONE     0x2
#define    OCT_DEV_INSTR_QUEUE_INIT_DONE  0x3
#define    OCT_DEV_SC_BUFF_POOL_INIT_DONE 0x4
#define    OCT_DEV_RESP_LIST_INIT_DONE    0x5
#define    OCT_DEV_DROQ_INIT_DONE         0x6
#define    OCT_DEV_IO_QUEUES_DONE         0x7
#define    OCT_DEV_CONSOLE_INIT_DONE      0x8
#define    OCT_DEV_HOST_OK                0x9
#define    OCT_DEV_CORE_OK                0xa
#define    OCT_DEV_RUNNING                0xb
#define    OCT_DEV_IN_RESET               0xc
#define    OCT_DEV_STATE_INVALID          0xd

#define    OCT_DEV_STATES                 OCT_DEV_STATE_INVALID

/** Octeon Device interrupts
  *  These interrupt bits are set in int_status filed of
  *  octeon_device structure
  */
#define	   OCT_DEV_INTR_DMA0_FORCE	  0x01
#define	   OCT_DEV_INTR_DMA1_FORCE	  0x02
#define	   OCT_DEV_INTR_PKT_DATA	  0x04

#define LIO_RESET_SECS (3)

/*---------------------------DISPATCH LIST-------------------------------*/

/** The dispatch list entry.
 *  The driver keeps a record of functions registered for each
 *  response header opcode in this structure. Since the opcode is
 *  hashed to index into the driver's list, more than one opcode
 *  can hash to the same entry, in which case the list field points
 *  to a linked list with the other entries.
 */
struct octeon_dispatch {
	/** List head for this entry */
	struct list_head list;

	/** The opcode for which the dispatch function & arg should be used */
	u16 opcode;

	/** The function to be called for a packet received by the driver */
	octeon_dispatch_fn_t dispatch_fn;

	/* The application specified argument to be passed to the above
	 * function along with the received packet
	 */
	void *arg;
};

/** The dispatch list structure. */
struct octeon_dispatch_list {
	/** access to dispatch list must be atomic */
	spinlock_t lock;

	/** Count of dispatch functions currently registered */
	u32 count;

	/** The list of dispatch functions */
	struct octeon_dispatch *dlist;
};

/*-----------------------  THE OCTEON DEVICE  ---------------------------*/

#define OCT_MEM_REGIONS     3
/** PCI address space mapping information.
 *  Each of the 3 address spaces given by BAR0, BAR2 and BAR4 of
 *  Octeon gets mapped to different physical address spaces in
 *  the kernel.
 */
struct octeon_mmio {
	/** PCI address to which the BAR is mapped. */
	u64 start;

	/** Length of this PCI address space. */
	u32 len;

	/** Length that has been mapped to phys. address space. */
	u32 mapped_len;

	/** The physical address to which the PCI address space is mapped. */
	u8 __iomem *hw_addr;

	/** Flag indicating the mapping was successful. */
	u32 done;
};

#define   MAX_OCTEON_MAPS    32

struct octeon_io_enable {
	u64 iq;
	u64 oq;
	u64 iq64B;
};

struct octeon_reg_list {
	u32 __iomem *pci_win_wr_addr_hi;
	u32 __iomem *pci_win_wr_addr_lo;
	u64 __iomem *pci_win_wr_addr;

	u32 __iomem *pci_win_rd_addr_hi;
	u32 __iomem *pci_win_rd_addr_lo;
	u64 __iomem *pci_win_rd_addr;

	u32 __iomem *pci_win_wr_data_hi;
	u32 __iomem *pci_win_wr_data_lo;
	u64 __iomem *pci_win_wr_data;

	u32 __iomem *pci_win_rd_data_hi;
	u32 __iomem *pci_win_rd_data_lo;
	u64 __iomem *pci_win_rd_data;
};

#define OCTEON_CONSOLE_MAX_READ_BYTES 512
struct octeon_console {
	u32 active;
	u32 waiting;
	u64 addr;
	u32 buffer_size;
	u64 input_base_addr;
	u64 output_base_addr;
	char leftover[OCTEON_CONSOLE_MAX_READ_BYTES];
};

struct octeon_board_info {
	char name[OCT_BOARD_NAME];
	char serial_number[OCT_SERIAL_LEN];
	u64 major;
	u64 minor;
};

struct octeon_fn_list {
	void (*setup_iq_regs)(struct octeon_device *, u32);
	void (*setup_oq_regs)(struct octeon_device *, u32);

	irqreturn_t (*process_interrupt_regs)(void *);
	int (*soft_reset)(struct octeon_device *);
	int (*setup_device_regs)(struct octeon_device *);
	void (*reinit_regs)(struct octeon_device *);
	void (*bar1_idx_setup)(struct octeon_device *, u64, u32, int);
	void (*bar1_idx_write)(struct octeon_device *, u32, u32);
	u32 (*bar1_idx_read)(struct octeon_device *, u32);
	u32 (*update_iq_read_idx)(struct octeon_instr_queue *);

	void (*enable_oq_pkt_time_intr)(struct octeon_device *, u32);
	void (*disable_oq_pkt_time_intr)(struct octeon_device *, u32);

	void (*enable_interrupt)(void *);
	void (*disable_interrupt)(void *);

	void (*enable_io_queues)(struct octeon_device *);
	void (*disable_io_queues)(struct octeon_device *);
};

/* Must be multiple of 8, changing breaks ABI */
#define CVMX_BOOTMEM_NAME_LEN 128

/* Structure for named memory blocks
 * Number of descriptors
 * available can be changed without affecting compatibility,
 * but name length changes require a bump in the bootmem
 * descriptor version
 * Note: This structure must be naturally 64 bit aligned, as a single
 * memory image will be used by both 32 and 64 bit programs.
 */
struct cvmx_bootmem_named_block_desc {
	/** Base address of named block */
	u64 base_addr;

	/** Size actually allocated for named block */
	u64 size;

	/** name of named block */
	char name[CVMX_BOOTMEM_NAME_LEN];
};

struct oct_fw_info {
	u32 max_nic_ports;      /** max nic ports for the device */
	u32 num_gmx_ports;      /** num gmx ports */
	u64 app_cap_flags;      /** firmware cap flags */

	/** The core application is running in this mode.
	 * See octeon-drv-opcodes.h for values.
	 */
	u32 app_mode;
	char   liquidio_firmware_version[32];
};

/* wrappers around work structs */
struct cavium_wk {
	struct delayed_work work;
	void *ctxptr;
	u64 ctxul;
};

struct cavium_wq {
	struct workqueue_struct *wq;
	struct cavium_wk wk;
};

struct octdev_props {
	/* Each interface in the Octeon device has a network
	 * device pointer (used for OS specific calls).
	 */
	int    napi_enabled;
	int    gmxport;
	struct net_device *netdev;
};

/** The Octeon device.
 *  Each Octeon device has this structure to represent all its
 *  components.
 */
struct octeon_device {
	/** Lock for PCI window configuration accesses */
	spinlock_t pci_win_lock;

	/** Lock for memory accesses */
	spinlock_t mem_access_lock;

	/** PCI device pointer */
	struct pci_dev *pci_dev;

	/** Chip specific information. */
	void *chip;

	/** Number of interfaces detected in this octeon device. */
	u32 ifcount;

	struct octdev_props props[MAX_OCTEON_LINKS];

	/** Octeon Chip type. */
	u16 chip_id;
	u16 rev_id;

	/** This device's id - set by the driver. */
	u32 octeon_id;

	/** This device's PCIe port used for traffic. */
	u16 pcie_port;

	u16 flags;
#define LIO_FLAG_MSI_ENABLED                  (u32)(1 << 1)
#define LIO_FLAG_MSIX_ENABLED                 (u32)(1 << 2)

	/** The state of this device */
	atomic_t status;

	/** memory mapped io range */
	struct octeon_mmio mmio[OCT_MEM_REGIONS];

	struct octeon_reg_list reg_list;

	struct octeon_fn_list fn_list;

	struct octeon_board_info boardinfo;

	u32 num_iqs;

	/* The pool containing pre allocated buffers used for soft commands */
	struct octeon_sc_buffer_pool	sc_buf_pool;

	/** The input instruction queues */
	struct octeon_instr_queue *instr_queue
		[MAX_POSSIBLE_OCTEON_INSTR_QUEUES];

	/** The doubly-linked list of instruction response */
	struct octeon_response_list response_list[MAX_RESPONSE_LISTS];

	u32 num_oqs;

	/** The DROQ output queues  */
	struct octeon_droq *droq[MAX_POSSIBLE_OCTEON_OUTPUT_QUEUES];

	struct octeon_io_enable io_qmask;

	/** List of dispatch functions */
	struct octeon_dispatch_list dispatch;

	/* Interrupt Moderation */
	struct oct_intrmod_cfg intrmod;

	u32 int_status;

	u64 droq_intr;

	/** Physical location of the cvmx_bootmem_desc_t in octeon memory */
	u64 bootmem_desc_addr;

	/** Placeholder memory for named blocks.
	 * Assumes single-threaded access
	 */
	struct cvmx_bootmem_named_block_desc bootmem_named_block_desc;

	/** Address of consoles descriptor */
	u64 console_desc_addr;

	/** Number of consoles available. 0 means they are inaccessible */
	u32 num_consoles;

	/* Console caches */
	struct octeon_console console[MAX_OCTEON_MAPS];

	/* Coprocessor clock rate. */
	u64 coproc_clock_rate;

	/** The core application is running in this mode. See liquidio_common.h
	 * for values.
	 */
	u32 app_mode;

	struct oct_fw_info fw_info;

	/** The name given to this device. */
	char device_name[32];

	/** Application Context */
	void *app_ctx;

	struct cavium_wq dma_comp_wq;

	/** Lock for dma response list */
	spinlock_t cmd_resp_wqlock;
	u32 cmd_resp_state;

	struct cavium_wq check_db_wq[MAX_POSSIBLE_OCTEON_INSTR_QUEUES];

	struct cavium_wk nic_poll_work;

	struct cavium_wk console_poll_work[MAX_OCTEON_MAPS];

	void *priv;

	int rx_pause;
	int tx_pause;

	struct oct_link_stats link_stats; /*stastics from firmware*/

	/* private flags to control driver-specific features through ethtool */
	u32 priv_flags;
};

#define  OCT_DRV_ONLINE 1
#define  OCT_DRV_OFFLINE 2
#define  OCTEON_CN6XXX(oct)           ((oct->chip_id == OCTEON_CN66XX) || \
				       (oct->chip_id == OCTEON_CN68XX))
#define CHIP_FIELD(oct, TYPE, field)             \
	(((struct octeon_ ## TYPE  *)(oct->chip))->field)

struct oct_intrmod_cmd {
	struct octeon_device *oct_dev;
	struct octeon_soft_command *sc;
	struct oct_intrmod_cfg *cfg;
};

/*------------------ Function Prototypes ----------------------*/

/** Initialize device list memory */
void octeon_init_device_list(int conf_type);

/** Free memory for Input and Output queue structures for a octeon device */
void octeon_free_device_mem(struct octeon_device *);

/* Look up a free entry in the octeon_device table and allocate resources
 * for the octeon_device structure for an octeon device. Called at init
 * time.
 */
struct octeon_device *octeon_allocate_device(u32 pci_id,
					     u32 priv_size);

/**  Initialize the driver's dispatch list which is a mix of a hash table
 *  and a linked list. This is done at driver load time.
 *  @param octeon_dev - pointer to the octeon device structure.
 *  @return 0 on success, else -ve error value
 */
int octeon_init_dispatch_list(struct octeon_device *octeon_dev);

/**  Delete the driver's dispatch list and all registered entries.
 * This is done at driver unload time.
 *  @param octeon_dev - pointer to the octeon device structure.
 */
void octeon_delete_dispatch_list(struct octeon_device *octeon_dev);

/** Initialize the core device fields with the info returned by the FW.
 * @param recv_info - Receive info structure
 * @param buf       - Receive buffer
 */
int octeon_core_drv_init(struct octeon_recv_info *recv_info, void *buf);

/** Gets the dispatch function registered to receive packets with a
 *  given opcode/subcode.
 *  @param  octeon_dev  - the octeon device pointer.
 *  @param  opcode      - the opcode for which the dispatch function
 *                        is to checked.
 *  @param  subcode     - the subcode for which the dispatch function
 *                        is to checked.
 *
 *  @return Success: octeon_dispatch_fn_t (dispatch function pointer)
 *  @return Failure: NULL
 *
 *  Looks up the dispatch list to get the dispatch function for a
 *  given opcode.
 */
octeon_dispatch_fn_t
octeon_get_dispatch(struct octeon_device *octeon_dev, u16 opcode,
		    u16 subcode);

/** Get the octeon device pointer.
 *  @param octeon_id  - The id for which the octeon device pointer is required.
 *  @return Success: Octeon device pointer.
 *  @return Failure: NULL.
 */
struct octeon_device *lio_get_device(u32 octeon_id);

/** Get the octeon id assigned to the octeon device passed as argument.
 *  This function is exported to other modules.
 *  @param dev - octeon device pointer passed as a void *.
 *  @return octeon device id
 */
int lio_get_device_id(void *dev);

static inline u16 OCTEON_MAJOR_REV(struct octeon_device *oct)
{
	u16 rev = (oct->rev_id & 0xC) >> 2;

	return (rev == 0) ? 1 : rev;
}

static inline u16 OCTEON_MINOR_REV(struct octeon_device *oct)
{
	return oct->rev_id & 0x3;
}

/** Read windowed register.
 *  @param  oct   -  pointer to the Octeon device.
 *  @param  addr  -  Address of the register to read.
 *
 *  This routine is called to read from the indirectly accessed
 *  Octeon registers that are visible through a PCI BAR0 mapped window
 *  register.
 *  @return  - 64 bit value read from the register.
 */

u64 lio_pci_readq(struct octeon_device *oct, u64 addr);

/** Write windowed register.
 *  @param  oct  -  pointer to the Octeon device.
 *  @param  val  -  Value to write
 *  @param  addr -  Address of the register to write
 *
 *  This routine is called to write to the indirectly accessed
 *  Octeon registers that are visible through a PCI BAR0 mapped window
 *  register.
 *  @return   Nothing.
 */
void lio_pci_writeq(struct octeon_device *oct, u64 val, u64 addr);

/* Routines for reading and writing CSRs */
#define   octeon_write_csr(oct_dev, reg_off, value) \
		writel(value, oct_dev->mmio[0].hw_addr + reg_off)

#define   octeon_write_csr64(oct_dev, reg_off, val64) \
		writeq(val64, oct_dev->mmio[0].hw_addr + reg_off)

#define   octeon_read_csr(oct_dev, reg_off)         \
		readl(oct_dev->mmio[0].hw_addr + reg_off)

#define   octeon_read_csr64(oct_dev, reg_off)         \
		readq(oct_dev->mmio[0].hw_addr + reg_off)

/**
 * Checks if memory access is okay
 *
 * @param oct which octeon to send to
 * @return Zero on success, negative on failure.
 */
int octeon_mem_access_ok(struct octeon_device *oct);

/**
 * Waits for DDR initialization.
 *
 * @param oct which octeon to send to
 * @param timeout_in_ms pointer to how long to wait until DDR is initialized
 * in ms.
 *                      If contents are 0, it waits until contents are non-zero
 *                      before starting to check.
 * @return Zero on success, negative on failure.
 */
int octeon_wait_for_ddr_init(struct octeon_device *oct,
			     u32 *timeout_in_ms);

/**
 * Wait for u-boot to boot and be waiting for a command.
 *
 * @param wait_time_hundredths
 *               Maximum time to wait
 *
 * @return Zero on success, negative on failure.
 */
int octeon_wait_for_bootloader(struct octeon_device *oct,
			       u32 wait_time_hundredths);

/**
 * Initialize console access
 *
 * @param oct which octeon initialize
 * @return Zero on success, negative on failure.
 */
int octeon_init_consoles(struct octeon_device *oct);

/**
 * Adds access to a console to the device.
 *
 * @param oct which octeon to add to
 * @param console_num which console
 * @return Zero on success, negative on failure.
 */
int octeon_add_console(struct octeon_device *oct, u32 console_num);

/** write or read from a console */
int octeon_console_write(struct octeon_device *oct, u32 console_num,
			 char *buffer, u32 write_request_size, u32 flags);
int octeon_console_write_avail(struct octeon_device *oct, u32 console_num);

int octeon_console_read_avail(struct octeon_device *oct, u32 console_num);

/** Removes all attached consoles. */
void octeon_remove_consoles(struct octeon_device *oct);

/**
 * Send a string to u-boot on console 0 as a command.
 *
 * @param oct which octeon to send to
 * @param cmd_str String to send
 * @param wait_hundredths Time to wait for u-boot to accept the command.
 *
 * @return Zero on success, negative on failure.
 */
int octeon_console_send_cmd(struct octeon_device *oct, char *cmd_str,
			    u32 wait_hundredths);

/** Parses, validates, and downloads firmware, then boots associated cores.
 *  @param oct which octeon to download firmware to
 *  @param data  - The complete firmware file image
 *  @param size  - The size of the data
 *
 *  @return 0 if success.
 *         -EINVAL if file is incompatible or badly formatted.
 *         -ENODEV if no handler was found for the application type or an
 *         invalid octeon id was passed.
 */
int octeon_download_firmware(struct octeon_device *oct, const u8 *data,
			     size_t size);

char *lio_get_state_string(atomic_t *state_ptr);

/** Sets up instruction queues for the device
 *  @param oct which octeon to setup
 *
 *  @return 0 if success. 1 if fails
 */
int octeon_setup_instr_queues(struct octeon_device *oct);

/** Sets up output queues for the device
 *  @param oct which octeon to setup
 *
 *  @return 0 if success. 1 if fails
 */
int octeon_setup_output_queues(struct octeon_device *oct);

int octeon_get_tx_qsize(struct octeon_device *oct, u32 q_no);

int octeon_get_rx_qsize(struct octeon_device *oct, u32 q_no);

/** Turns off the input and output queues for the device
 *  @param oct which octeon to disable
 */
void octeon_set_io_queues_off(struct octeon_device *oct);

/** Turns on or off the given output queue for the device
 *  @param oct which octeon to change
 *  @param q_no which queue
 *  @param enable 1 to enable, 0 to disable
 */
void octeon_set_droq_pkt_op(struct octeon_device *oct, u32 q_no, u32 enable);

/** Retrieve the config for the device
 *  @param oct which octeon
 *  @param card_type type of card
 *
 *  @returns pointer to configuration
 */
void *oct_get_config_info(struct octeon_device *oct, u16 card_type);

/** Gets the octeon device configuration
 *  @return - pointer to the octeon configuration struture
 */
struct octeon_config *octeon_get_conf(struct octeon_device *oct);

/* LiquidIO driver pivate flags */
enum {
	OCT_PRIV_FLAG_TX_BYTES = 0, /* Tx interrupts by pending byte count */
};

static inline void lio_set_priv_flag(struct octeon_device *octdev, u32 flag,
				     u32 val)
{
	if (val)
		octdev->priv_flags |= (0x1 << flag);
	else
		octdev->priv_flags &= ~(0x1 << flag);
}
#endif
