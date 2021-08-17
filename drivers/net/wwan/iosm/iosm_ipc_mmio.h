/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-21 Intel Corporation.
 */

#ifndef IOSM_IPC_MMIO_H
#define IOSM_IPC_MMIO_H

/* Minimal IOSM CP VERSION which has valid CP_CAPABILITIES field */
#define IOSM_CP_VERSION 0x0100UL

/* DL dir Aggregation support mask */
#define DL_AGGR BIT(9)

/* UL dir Aggregation support mask */
#define UL_AGGR BIT(8)

/* UL flow credit support mask */
#define UL_FLOW_CREDIT BIT(21)

/* Possible states of the IPC finite state machine. */
enum ipc_mem_device_ipc_state {
	IPC_MEM_DEVICE_IPC_UNINIT,
	IPC_MEM_DEVICE_IPC_INIT,
	IPC_MEM_DEVICE_IPC_RUNNING,
	IPC_MEM_DEVICE_IPC_RECOVERY,
	IPC_MEM_DEVICE_IPC_ERROR,
	IPC_MEM_DEVICE_IPC_DONT_CARE,
	IPC_MEM_DEVICE_IPC_INVALID = -1
};

/* Boot ROM exit status. */
enum rom_exit_code {
	IMEM_ROM_EXIT_OPEN_EXT = 0x01,
	IMEM_ROM_EXIT_OPEN_MEM = 0x02,
	IMEM_ROM_EXIT_CERT_EXT = 0x10,
	IMEM_ROM_EXIT_CERT_MEM = 0x20,
	IMEM_ROM_EXIT_FAIL = 0xFF
};

/* Boot stages */
enum ipc_mem_exec_stage {
	IPC_MEM_EXEC_STAGE_RUN = 0x600DF00D,
	IPC_MEM_EXEC_STAGE_CRASH = 0x8BADF00D,
	IPC_MEM_EXEC_STAGE_CD_READY = 0xBADC0DED,
	IPC_MEM_EXEC_STAGE_BOOT = 0xFEEDB007,
	IPC_MEM_EXEC_STAGE_PSI = 0xFEEDBEEF,
	IPC_MEM_EXEC_STAGE_EBL = 0xFEEDCAFE,
	IPC_MEM_EXEC_STAGE_INVALID = 0xFFFFFFFF
};

/* mmio scratchpad info */
struct mmio_offset {
	int exec_stage;
	int chip_info;
	int rom_exit_code;
	int psi_address;
	int psi_size;
	int ipc_status;
	int context_info;
	int ap_win_base;
	int ap_win_end;
	int cp_version;
	int cp_capability;
};

/**
 * struct iosm_mmio - MMIO region mapped to the doorbell scratchpad.
 * @base:		Base address of MMIO region
 * @dev:		Pointer to device structure
 * @offset:		Start offset
 * @context_info_addr:	Physical base address of context info structure
 * @chip_info_version:	Version of chip info structure
 * @chip_info_size:	Size of chip info structure
 * @has_mux_lite:	It doesn't support mux aggergation
 * @has_ul_flow_credit:	Ul flow credit support
 * @has_slp_no_prot:	Device sleep no protocol support
 * @has_mcr_support:	Usage of mcr support
 */
struct iosm_mmio {
	unsigned char __iomem *base;
	struct device *dev;
	struct mmio_offset offset;
	phys_addr_t context_info_addr;
	unsigned int chip_info_version;
	unsigned int chip_info_size;
	u8 has_mux_lite:1,
	   has_ul_flow_credit:1,
	   has_slp_no_prot:1,
	   has_mcr_support:1;
};

/**
 * ipc_mmio_init - Allocate mmio instance data
 * @mmio_addr:	Mapped AP base address of the MMIO area.
 * @dev:	Pointer to device structure
 *
 * Returns: address of mmio instance data or NULL if fails.
 */
struct iosm_mmio *ipc_mmio_init(void __iomem *mmio_addr, struct device *dev);

/**
 * ipc_mmio_set_psi_addr_and_size - Set start address and size of the
 *				    primary system image (PSI) for the
 *				    FW dowload.
 * @ipc_mmio:	Pointer to mmio instance
 * @addr:	PSI address
 * @size:	PSI immage size
 */
void ipc_mmio_set_psi_addr_and_size(struct iosm_mmio *ipc_mmio, dma_addr_t addr,
				    u32 size);

/**
 * ipc_mmio_set_contex_info_addr - Stores the Context Info Address in
 *				   MMIO instance to share it with CP during
 *				   mmio_init.
 * @ipc_mmio:	Pointer to mmio instance
 * @addr:	64-bit address of AP context information.
 */
void ipc_mmio_set_contex_info_addr(struct iosm_mmio *ipc_mmio,
				   phys_addr_t addr);

/**
 * ipc_mmio_get_cp_version - Get the CP IPC version
 * @ipc_mmio:	Pointer to mmio instance
 *
 * Returns: version number on success and failure value on error.
 */
int ipc_mmio_get_cp_version(struct iosm_mmio *ipc_mmio);

/**
 * ipc_mmio_get_rom_exit_code - Get exit code from CP boot rom download app
 * @ipc_mmio:	Pointer to mmio instance
 *
 * Returns: exit code from CP boot rom download APP
 */
enum rom_exit_code ipc_mmio_get_rom_exit_code(struct iosm_mmio *ipc_mmio);

/**
 * ipc_mmio_get_exec_stage - Query CP execution stage
 * @ipc_mmio:	Pointer to mmio instance
 *
 * Returns: CP execution stage
 */
enum ipc_mem_exec_stage ipc_mmio_get_exec_stage(struct iosm_mmio *ipc_mmio);

/**
 * ipc_mmio_get_ipc_state - Query CP IPC state
 * @ipc_mmio:	Pointer to mmio instance
 *
 * Returns: CP IPC state
 */
enum ipc_mem_device_ipc_state
ipc_mmio_get_ipc_state(struct iosm_mmio *ipc_mmio);

/**
 * ipc_mmio_copy_chip_info - Copy size bytes of CP chip info structure
 *			     into caller provided buffer
 * @ipc_mmio:	Pointer to mmio instance
 * @dest:	Pointer to caller provided buff
 * @size:	Number of bytes to copy
 */
void ipc_mmio_copy_chip_info(struct iosm_mmio *ipc_mmio, void *dest,
			     size_t size);

/**
 * ipc_mmio_config - Write context info and AP memory range addresses.
 *		     This needs to be called when CP is in
 *		     IPC_MEM_DEVICE_IPC_INIT state
 *
 * @ipc_mmio:	Pointer to mmio instance
 */
void ipc_mmio_config(struct iosm_mmio *ipc_mmio);

/**
 * ipc_mmio_update_cp_capability - Read and update modem capability, from mmio
 *				   capability offset
 *
 * @ipc_mmio:	Pointer to mmio instance
 */
void ipc_mmio_update_cp_capability(struct iosm_mmio *ipc_mmio);

#endif
