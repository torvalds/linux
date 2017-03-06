/*
 * Copyright (C) 2010-2014, 2016-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_ukk.h"

#include "mali_mmu.h"
#include "mali_hw_core.h"
#include "mali_group.h"
#include "mali_mmu_page_directory.h"

/**
 * Size of the MMU registers in bytes
 */
#define MALI_MMU_REGISTERS_SIZE 0x24

/**
 * MMU commands
 * These are the commands that can be sent
 * to the MMU unit.
 */
typedef enum mali_mmu_command {
	MALI_MMU_COMMAND_ENABLE_PAGING = 0x00, /**< Enable paging (memory translation) */
	MALI_MMU_COMMAND_DISABLE_PAGING = 0x01, /**< Disable paging (memory translation) */
	MALI_MMU_COMMAND_ENABLE_STALL = 0x02, /**<  Enable stall on page fault */
	MALI_MMU_COMMAND_DISABLE_STALL = 0x03, /**< Disable stall on page fault */
	MALI_MMU_COMMAND_ZAP_CACHE = 0x04, /**< Zap the entire page table cache */
	MALI_MMU_COMMAND_PAGE_FAULT_DONE = 0x05, /**< Page fault processed */
	MALI_MMU_COMMAND_HARD_RESET = 0x06 /**< Reset the MMU back to power-on settings */
} mali_mmu_command;

static void mali_mmu_probe_trigger(void *data);
static _mali_osk_errcode_t mali_mmu_probe_ack(void *data);

MALI_STATIC_INLINE _mali_osk_errcode_t mali_mmu_raw_reset(struct mali_mmu_core *mmu);

/* page fault queue flush helper pages
 * note that the mapping pointers are currently unused outside of the initialization functions */
static mali_dma_addr mali_page_fault_flush_page_directory = MALI_INVALID_PAGE;
static mali_io_address mali_page_fault_flush_page_directory_mapping = NULL;
static mali_dma_addr mali_page_fault_flush_page_table = MALI_INVALID_PAGE;
static mali_io_address mali_page_fault_flush_page_table_mapping = NULL;
static mali_dma_addr mali_page_fault_flush_data_page = MALI_INVALID_PAGE;
static mali_io_address mali_page_fault_flush_data_page_mapping = NULL;

/* an empty page directory (no address valid) which is active on any MMU not currently marked as in use */
static mali_dma_addr mali_empty_page_directory_phys   = MALI_INVALID_PAGE;
static mali_io_address mali_empty_page_directory_virt = NULL;


_mali_osk_errcode_t mali_mmu_initialize(void)
{
	/* allocate the helper pages */
	mali_empty_page_directory_phys = mali_allocate_empty_page(&mali_empty_page_directory_virt);
	if (0 == mali_empty_page_directory_phys) {
		MALI_DEBUG_PRINT_ERROR(("Mali MMU: Could not allocate empty page directory.\n"));
		mali_empty_page_directory_phys = MALI_INVALID_PAGE;
		return _MALI_OSK_ERR_NOMEM;
	}

	if (_MALI_OSK_ERR_OK != mali_create_fault_flush_pages(&mali_page_fault_flush_page_directory,
			&mali_page_fault_flush_page_directory_mapping,
			&mali_page_fault_flush_page_table,
			&mali_page_fault_flush_page_table_mapping,
			&mali_page_fault_flush_data_page,
			&mali_page_fault_flush_data_page_mapping)) {
		MALI_DEBUG_PRINT_ERROR(("Mali MMU: Could not allocate fault flush pages\n"));
		mali_free_empty_page(mali_empty_page_directory_phys, mali_empty_page_directory_virt);
		mali_empty_page_directory_phys = MALI_INVALID_PAGE;
		mali_empty_page_directory_virt = NULL;
		return _MALI_OSK_ERR_NOMEM;
	}

	return _MALI_OSK_ERR_OK;
}

void mali_mmu_terminate(void)
{
	MALI_DEBUG_PRINT(3, ("Mali MMU: terminating\n"));

	/* Free global helper pages */
	mali_free_empty_page(mali_empty_page_directory_phys, mali_empty_page_directory_virt);
	mali_empty_page_directory_phys = MALI_INVALID_PAGE;
	mali_empty_page_directory_virt = NULL;

	/* Free the page fault flush pages */
	mali_destroy_fault_flush_pages(&mali_page_fault_flush_page_directory,
				       &mali_page_fault_flush_page_directory_mapping,
				       &mali_page_fault_flush_page_table,
				       &mali_page_fault_flush_page_table_mapping,
				       &mali_page_fault_flush_data_page,
				       &mali_page_fault_flush_data_page_mapping);
}

struct mali_mmu_core *mali_mmu_create(_mali_osk_resource_t *resource, struct mali_group *group, mali_bool is_virtual)
{
	struct mali_mmu_core *mmu = NULL;

	MALI_DEBUG_ASSERT_POINTER(resource);

	MALI_DEBUG_PRINT(2, ("Mali MMU: Creating Mali MMU: %s\n", resource->description));

	mmu = _mali_osk_calloc(1, sizeof(struct mali_mmu_core));
	if (NULL != mmu) {
		if (_MALI_OSK_ERR_OK == mali_hw_core_create(&mmu->hw_core, resource, MALI_MMU_REGISTERS_SIZE)) {
			if (_MALI_OSK_ERR_OK == mali_group_add_mmu_core(group, mmu)) {
				if (is_virtual) {
					/* Skip reset and IRQ setup for virtual MMU */
					return mmu;
				}

				if (_MALI_OSK_ERR_OK == mali_mmu_reset(mmu)) {
					/* Setup IRQ handlers (which will do IRQ probing if needed) */
					mmu->irq = _mali_osk_irq_init(resource->irq,
								      mali_group_upper_half_mmu,
								      group,
								      mali_mmu_probe_trigger,
								      mali_mmu_probe_ack,
								      mmu,
								      resource->description);
					if (NULL != mmu->irq) {
						return mmu;
					} else {
						MALI_PRINT_ERROR(("Mali MMU: Failed to setup interrupt handlers for MMU %s\n", mmu->hw_core.description));
					}
				}
				mali_group_remove_mmu_core(group);
			} else {
				MALI_PRINT_ERROR(("Mali MMU: Failed to add core %s to group\n", mmu->hw_core.description));
			}
			mali_hw_core_delete(&mmu->hw_core);
		}

		_mali_osk_free(mmu);
	} else {
		MALI_PRINT_ERROR(("Failed to allocate memory for MMU\n"));
	}

	return NULL;
}

void mali_mmu_delete(struct mali_mmu_core *mmu)
{
	if (NULL != mmu->irq) {
		_mali_osk_irq_term(mmu->irq);
	}

	mali_hw_core_delete(&mmu->hw_core);
	_mali_osk_free(mmu);
}

static void mali_mmu_enable_paging(struct mali_mmu_core *mmu)
{
	int i;

	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ENABLE_PAGING);

	for (i = 0; i < MALI_REG_POLL_COUNT_FAST; ++i) {
		if (mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS) & MALI_MMU_STATUS_BIT_PAGING_ENABLED) {
			break;
		}
	}
	if (MALI_REG_POLL_COUNT_FAST == i) {
		MALI_PRINT_ERROR(("Enable paging request failed, MMU status is 0x%08X\n", mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS)));
	}
}

/**
 * Issues the enable stall command to the MMU and waits for HW to complete the request
 * @param mmu The MMU to enable paging for
 * @return MALI_TRUE if HW stall was successfully engaged, otherwise MALI_FALSE (req timed out)
 */
static mali_bool mali_mmu_enable_stall(struct mali_mmu_core *mmu)
{
	int i;
	u32 mmu_status = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS);

	if (0 == (mmu_status & MALI_MMU_STATUS_BIT_PAGING_ENABLED)) {
		MALI_DEBUG_PRINT(4, ("MMU stall is implicit when Paging is not enabled.\n"));
		return MALI_TRUE;
	}

	if (mmu_status & MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE) {
		MALI_DEBUG_PRINT(3, ("Aborting MMU stall request since it is in pagefault state.\n"));
		return MALI_FALSE;
	}

	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ENABLE_STALL);

	for (i = 0; i < MALI_REG_POLL_COUNT_FAST; ++i) {
		mmu_status = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS);
		if (mmu_status & MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE) {
			break;
		}
		if ((mmu_status & MALI_MMU_STATUS_BIT_STALL_ACTIVE) && (0 == (mmu_status & MALI_MMU_STATUS_BIT_STALL_NOT_ACTIVE))) {
			break;
		}
		if (0 == (mmu_status & (MALI_MMU_STATUS_BIT_PAGING_ENABLED))) {
			break;
		}
	}
	if (MALI_REG_POLL_COUNT_FAST == i) {
		MALI_DEBUG_PRINT(2, ("Enable stall request failed, MMU status is 0x%08X\n", mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS)));
		return MALI_FALSE;
	}

	if (mmu_status & MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE) {
		MALI_DEBUG_PRINT(2, ("Aborting MMU stall request since it has a pagefault.\n"));
		return MALI_FALSE;
	}

	return MALI_TRUE;
}

/**
 * Issues the disable stall command to the MMU and waits for HW to complete the request
 * @param mmu The MMU to enable paging for
 */
static void mali_mmu_disable_stall(struct mali_mmu_core *mmu)
{
	int i;
	u32 mmu_status = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS);

	if (0 == (mmu_status & MALI_MMU_STATUS_BIT_PAGING_ENABLED)) {
		MALI_DEBUG_PRINT(3, ("MMU disable skipped since it was not enabled.\n"));
		return;
	}
	if (mmu_status & MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE) {
		MALI_DEBUG_PRINT(2, ("Aborting MMU disable stall request since it is in pagefault state.\n"));
		return;
	}

	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_DISABLE_STALL);

	for (i = 0; i < MALI_REG_POLL_COUNT_FAST; ++i) {
		u32 status = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS);
		if (0 == (status & MALI_MMU_STATUS_BIT_STALL_ACTIVE)) {
			break;
		}
		if (status &  MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE) {
			break;
		}
		if (0 == (mmu_status & MALI_MMU_STATUS_BIT_PAGING_ENABLED)) {
			break;
		}
	}
	if (MALI_REG_POLL_COUNT_FAST == i) MALI_DEBUG_PRINT(1, ("Disable stall request failed, MMU status is 0x%08X\n", mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS)));
}

void mali_mmu_page_fault_done(struct mali_mmu_core *mmu)
{
	MALI_DEBUG_PRINT(4, ("Mali MMU: %s: Leaving page fault mode\n", mmu->hw_core.description));
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_PAGE_FAULT_DONE);
}

MALI_STATIC_INLINE _mali_osk_errcode_t mali_mmu_raw_reset(struct mali_mmu_core *mmu)
{
	int i;

	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_DTE_ADDR, 0xCAFEBABE);
	MALI_DEBUG_ASSERT(0xCAFEB000 == mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_DTE_ADDR));
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_HARD_RESET);

	for (i = 0; i < MALI_REG_POLL_COUNT_FAST; ++i) {
		if (mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_DTE_ADDR) == 0) {
			break;
		}
	}
	if (MALI_REG_POLL_COUNT_FAST == i) {
		MALI_PRINT_ERROR(("Reset request failed, MMU status is 0x%08X\n", mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS)));
		return _MALI_OSK_ERR_FAULT;
	}

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t mali_mmu_reset(struct mali_mmu_core *mmu)
{
	_mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT;
	mali_bool stall_success;
	MALI_DEBUG_ASSERT_POINTER(mmu);

	stall_success = mali_mmu_enable_stall(mmu);
	if (!stall_success) {
		err = _MALI_OSK_ERR_BUSY;
	}

	MALI_DEBUG_PRINT(3, ("Mali MMU: mali_kernel_mmu_reset: %s\n", mmu->hw_core.description));

	if (_MALI_OSK_ERR_OK == mali_mmu_raw_reset(mmu)) {
		mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_INT_MASK, MALI_MMU_INTERRUPT_PAGE_FAULT | MALI_MMU_INTERRUPT_READ_BUS_ERROR);
		/* no session is active, so just activate the empty page directory */
		mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_DTE_ADDR, mali_empty_page_directory_phys);
		mali_mmu_enable_paging(mmu);
		err = _MALI_OSK_ERR_OK;
	}
	mali_mmu_disable_stall(mmu);

	return err;
}

mali_bool mali_mmu_zap_tlb(struct mali_mmu_core *mmu)
{
	mali_bool stall_success = mali_mmu_enable_stall(mmu);

	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ZAP_CACHE);

	if (MALI_FALSE == stall_success) {
		/* False means that it is in Pagefault state. Not possible to disable_stall then */
		return MALI_FALSE;
	}

	mali_mmu_disable_stall(mmu);
	return MALI_TRUE;
}

void mali_mmu_zap_tlb_without_stall(struct mali_mmu_core *mmu)
{
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ZAP_CACHE);
}


void mali_mmu_invalidate_page(struct mali_mmu_core *mmu, u32 mali_address)
{
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_ZAP_ONE_LINE, MALI_MMU_PDE_ENTRY(mali_address));
}

static void mali_mmu_activate_address_space(struct mali_mmu_core *mmu, u32 page_directory)
{
	/* The MMU must be in stalled or page fault mode, for this writing to work */
	MALI_DEBUG_ASSERT(0 != (mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS)
				& (MALI_MMU_STATUS_BIT_STALL_ACTIVE | MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE)));
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_DTE_ADDR, page_directory);
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ZAP_CACHE);

}

void mali_mmu_activate_page_directory(struct mali_mmu_core *mmu, struct mali_page_directory *pagedir)
{
	mali_bool stall_success;
	MALI_DEBUG_ASSERT_POINTER(mmu);

	MALI_DEBUG_PRINT(5, ("Asked to activate page directory 0x%x on MMU %s\n", pagedir, mmu->hw_core.description));

	stall_success = mali_mmu_enable_stall(mmu);
	MALI_DEBUG_ASSERT(stall_success);
	MALI_IGNORE(stall_success);
	mali_mmu_activate_address_space(mmu, pagedir->page_directory);
	mali_mmu_disable_stall(mmu);
}

void mali_mmu_activate_empty_page_directory(struct mali_mmu_core *mmu)
{
	mali_bool stall_success;

	MALI_DEBUG_ASSERT_POINTER(mmu);
	MALI_DEBUG_PRINT(3, ("Activating the empty page directory on MMU %s\n", mmu->hw_core.description));

	stall_success = mali_mmu_enable_stall(mmu);

	/* This function can only be called when the core is idle, so it could not fail. */
	MALI_DEBUG_ASSERT(stall_success);
	MALI_IGNORE(stall_success);

	mali_mmu_activate_address_space(mmu, mali_empty_page_directory_phys);
	mali_mmu_disable_stall(mmu);
}

void mali_mmu_activate_fault_flush_page_directory(struct mali_mmu_core *mmu)
{
	mali_bool stall_success;
	MALI_DEBUG_ASSERT_POINTER(mmu);

	MALI_DEBUG_PRINT(3, ("Activating the page fault flush page directory on MMU %s\n", mmu->hw_core.description));
	stall_success = mali_mmu_enable_stall(mmu);
	/* This function is expect to fail the stalling, since it might be in PageFault mode when it is called */
	mali_mmu_activate_address_space(mmu, mali_page_fault_flush_page_directory);
	if (MALI_TRUE == stall_success) mali_mmu_disable_stall(mmu);
}

/* Is called when we want the mmu to give an interrupt */
static void mali_mmu_probe_trigger(void *data)
{
	struct mali_mmu_core *mmu = (struct mali_mmu_core *)data;
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_INT_RAWSTAT, MALI_MMU_INTERRUPT_PAGE_FAULT | MALI_MMU_INTERRUPT_READ_BUS_ERROR);
}

/* Is called when the irq probe wants the mmu to acknowledge an interrupt from the hw */
static _mali_osk_errcode_t mali_mmu_probe_ack(void *data)
{
	struct mali_mmu_core *mmu = (struct mali_mmu_core *)data;
	u32 int_stat;

	int_stat = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_INT_STATUS);

	MALI_DEBUG_PRINT(2, ("mali_mmu_probe_irq_acknowledge: intstat 0x%x\n", int_stat));
	if (int_stat & MALI_MMU_INTERRUPT_PAGE_FAULT) {
		MALI_DEBUG_PRINT(2, ("Probe: Page fault detect: PASSED\n"));
		mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_INT_CLEAR, MALI_MMU_INTERRUPT_PAGE_FAULT);
	} else {
		MALI_DEBUG_PRINT(1, ("Probe: Page fault detect: FAILED\n"));
	}

	if (int_stat & MALI_MMU_INTERRUPT_READ_BUS_ERROR) {
		MALI_DEBUG_PRINT(2, ("Probe: Bus read error detect: PASSED\n"));
		mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_INT_CLEAR, MALI_MMU_INTERRUPT_READ_BUS_ERROR);
	} else {
		MALI_DEBUG_PRINT(1, ("Probe: Bus read error detect: FAILED\n"));
	}

	if ((int_stat & (MALI_MMU_INTERRUPT_PAGE_FAULT | MALI_MMU_INTERRUPT_READ_BUS_ERROR)) ==
	    (MALI_MMU_INTERRUPT_PAGE_FAULT | MALI_MMU_INTERRUPT_READ_BUS_ERROR)) {
		return _MALI_OSK_ERR_OK;
	}

	return _MALI_OSK_ERR_FAULT;
}

#if 0
void mali_mmu_print_state(struct mali_mmu_core *mmu)
{
	MALI_DEBUG_PRINT(2, ("MMU: State of %s is 0x%08x\n", mmu->hw_core.description, mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS)));
}
#endif
