// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/slab.h>

#include "iosm_ipc_mmio.h"

/* Definition of MMIO offsets
 * note that MMIO_CI offsets are relative to end of chip info structure
 */

/* MMIO chip info size in bytes */
#define MMIO_CHIP_INFO_SIZE 60

/* CP execution stage */
#define MMIO_OFFSET_EXECUTION_STAGE 0x00

/* Boot ROM Chip Info struct */
#define MMIO_OFFSET_CHIP_INFO 0x04

#define MMIO_OFFSET_ROM_EXIT_CODE 0x40

#define MMIO_OFFSET_PSI_ADDRESS 0x54

#define MMIO_OFFSET_PSI_SIZE 0x5C

#define MMIO_OFFSET_IPC_STATUS 0x60

#define MMIO_OFFSET_CONTEXT_INFO 0x64

#define MMIO_OFFSET_BASE_ADDR 0x6C

#define MMIO_OFFSET_END_ADDR 0x74

#define MMIO_OFFSET_CP_VERSION 0xF0

#define MMIO_OFFSET_CP_CAPABILITIES 0xF4

/* Timeout in 50 msec to wait for the modem boot code to write a valid
 * execution stage into mmio area
 */
#define IPC_MMIO_EXEC_STAGE_TIMEOUT 50

/* check if exec stage has one of the valid values */
static bool ipc_mmio_is_valid_exec_stage(enum ipc_mem_exec_stage stage)
{
	switch (stage) {
	case IPC_MEM_EXEC_STAGE_BOOT:
	case IPC_MEM_EXEC_STAGE_PSI:
	case IPC_MEM_EXEC_STAGE_EBL:
	case IPC_MEM_EXEC_STAGE_RUN:
	case IPC_MEM_EXEC_STAGE_CRASH:
	case IPC_MEM_EXEC_STAGE_CD_READY:
		return true;
	default:
		return false;
	}
}

void ipc_mmio_update_cp_capability(struct iosm_mmio *ipc_mmio)
{
	u32 cp_cap;
	unsigned int ver;

	ver = ipc_mmio_get_cp_version(ipc_mmio);
	cp_cap = readl(ipc_mmio->base + ipc_mmio->offset.cp_capability);

	ipc_mmio->has_mux_lite = (ver >= IOSM_CP_VERSION) &&
				 !(cp_cap & DL_AGGR) && !(cp_cap & UL_AGGR);

	ipc_mmio->has_ul_flow_credit =
		(ver >= IOSM_CP_VERSION) && (cp_cap & UL_FLOW_CREDIT);
}

struct iosm_mmio *ipc_mmio_init(void __iomem *mmio, struct device *dev)
{
	struct iosm_mmio *ipc_mmio = kzalloc(sizeof(*ipc_mmio), GFP_KERNEL);
	int retries = IPC_MMIO_EXEC_STAGE_TIMEOUT;
	enum ipc_mem_exec_stage stage;

	if (!ipc_mmio)
		return NULL;

	ipc_mmio->dev = dev;

	ipc_mmio->base = mmio;

	ipc_mmio->offset.exec_stage = MMIO_OFFSET_EXECUTION_STAGE;

	/* Check for a valid execution stage to make sure that the boot code
	 * has correctly initialized the MMIO area.
	 */
	do {
		stage = ipc_mmio_get_exec_stage(ipc_mmio);
		if (ipc_mmio_is_valid_exec_stage(stage))
			break;

		msleep(20);
	} while (retries-- > 0);

	if (!retries) {
		dev_err(ipc_mmio->dev, "invalid exec stage %X", stage);
		goto init_fail;
	}

	ipc_mmio->offset.chip_info = MMIO_OFFSET_CHIP_INFO;

	/* read chip info size and version from chip info structure */
	ipc_mmio->chip_info_version =
		ioread8(ipc_mmio->base + ipc_mmio->offset.chip_info);

	/* Increment of 2 is needed as the size value in the chip info
	 * excludes the version and size field, which are always present
	 */
	ipc_mmio->chip_info_size =
		ioread8(ipc_mmio->base + ipc_mmio->offset.chip_info + 1) + 2;

	if (ipc_mmio->chip_info_size != MMIO_CHIP_INFO_SIZE) {
		dev_err(ipc_mmio->dev, "Unexpected Chip Info");
		goto init_fail;
	}

	ipc_mmio->offset.rom_exit_code = MMIO_OFFSET_ROM_EXIT_CODE;

	ipc_mmio->offset.psi_address = MMIO_OFFSET_PSI_ADDRESS;
	ipc_mmio->offset.psi_size = MMIO_OFFSET_PSI_SIZE;
	ipc_mmio->offset.ipc_status = MMIO_OFFSET_IPC_STATUS;
	ipc_mmio->offset.context_info = MMIO_OFFSET_CONTEXT_INFO;
	ipc_mmio->offset.ap_win_base = MMIO_OFFSET_BASE_ADDR;
	ipc_mmio->offset.ap_win_end = MMIO_OFFSET_END_ADDR;

	ipc_mmio->offset.cp_version = MMIO_OFFSET_CP_VERSION;
	ipc_mmio->offset.cp_capability = MMIO_OFFSET_CP_CAPABILITIES;

	return ipc_mmio;

init_fail:
	kfree(ipc_mmio);
	return NULL;
}

enum ipc_mem_exec_stage ipc_mmio_get_exec_stage(struct iosm_mmio *ipc_mmio)
{
	if (!ipc_mmio)
		return IPC_MEM_EXEC_STAGE_INVALID;

	return (enum ipc_mem_exec_stage)readl(ipc_mmio->base +
					      ipc_mmio->offset.exec_stage);
}

void ipc_mmio_copy_chip_info(struct iosm_mmio *ipc_mmio, void *dest,
			     size_t size)
{
	if (ipc_mmio && dest)
		memcpy_fromio(dest, ipc_mmio->base + ipc_mmio->offset.chip_info,
			      size);
}

enum ipc_mem_device_ipc_state ipc_mmio_get_ipc_state(struct iosm_mmio *ipc_mmio)
{
	if (!ipc_mmio)
		return IPC_MEM_DEVICE_IPC_INVALID;

	return (enum ipc_mem_device_ipc_state)
		readl(ipc_mmio->base + ipc_mmio->offset.ipc_status);
}

enum rom_exit_code ipc_mmio_get_rom_exit_code(struct iosm_mmio *ipc_mmio)
{
	if (!ipc_mmio)
		return IMEM_ROM_EXIT_FAIL;

	return (enum rom_exit_code)readl(ipc_mmio->base +
					 ipc_mmio->offset.rom_exit_code);
}

void ipc_mmio_config(struct iosm_mmio *ipc_mmio)
{
	if (!ipc_mmio)
		return;

	/* AP memory window (full window is open and active so that modem checks
	 * each AP address) 0 means don't check on modem side.
	 */
	iowrite64_lo_hi(0, ipc_mmio->base + ipc_mmio->offset.ap_win_base);
	iowrite64_lo_hi(0, ipc_mmio->base + ipc_mmio->offset.ap_win_end);

	iowrite64_lo_hi(ipc_mmio->context_info_addr,
			ipc_mmio->base + ipc_mmio->offset.context_info);
}

void ipc_mmio_set_psi_addr_and_size(struct iosm_mmio *ipc_mmio, dma_addr_t addr,
				    u32 size)
{
	if (!ipc_mmio)
		return;

	iowrite64_lo_hi(addr, ipc_mmio->base + ipc_mmio->offset.psi_address);
	writel(size, ipc_mmio->base + ipc_mmio->offset.psi_size);
}

void ipc_mmio_set_contex_info_addr(struct iosm_mmio *ipc_mmio, phys_addr_t addr)
{
	if (!ipc_mmio)
		return;

	/* store context_info address. This will be stored in the mmio area
	 * during IPC_MEM_DEVICE_IPC_INIT state via ipc_mmio_config()
	 */
	ipc_mmio->context_info_addr = addr;
}

int ipc_mmio_get_cp_version(struct iosm_mmio *ipc_mmio)
{
	return ipc_mmio ? readl(ipc_mmio->base + ipc_mmio->offset.cp_version) :
			  -EFAULT;
}
