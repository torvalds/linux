/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_osk_bitops.h"
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
 * MMU register numbers
 * Used in the register read/write routines.
 * See the hardware documentation for more information about each register
 */
typedef enum mali_mmu_register {
	MALI_MMU_REGISTER_DTE_ADDR = 0x0000, /**< Current Page Directory Pointer */
	MALI_MMU_REGISTER_STATUS = 0x0004, /**< Status of the MMU */
	MALI_MMU_REGISTER_COMMAND = 0x0008, /**< Command register, used to control the MMU */
	MALI_MMU_REGISTER_PAGE_FAULT_ADDR = 0x000C, /**< Logical address of the last page fault */
	MALI_MMU_REGISTER_ZAP_ONE_LINE = 0x010, /**< Used to invalidate the mapping of a single page from the MMU */
	MALI_MMU_REGISTER_INT_RAWSTAT = 0x0014, /**< Raw interrupt status, all interrupts visible */
	MALI_MMU_REGISTER_INT_CLEAR = 0x0018, /**< Indicate to the MMU that the interrupt has been received */
	MALI_MMU_REGISTER_INT_MASK = 0x001C, /**< Enable/disable types of interrupts */
	MALI_MMU_REGISTER_INT_STATUS = 0x0020 /**< Interrupt status based on the mask */
} mali_mmu_register;

/**
 * MMU interrupt register bits
 * Each cause of the interrupt is reported
 * through the (raw) interrupt status registers.
 * Multiple interrupts can be pending, so multiple bits
 * can be set at once.
 */
typedef enum mali_mmu_interrupt
{
	MALI_MMU_INTERRUPT_PAGE_FAULT = 0x01, /**< A page fault occured */
	MALI_MMU_INTERRUPT_READ_BUS_ERROR = 0x02 /**< A bus read error occured */
} mali_mmu_interrupt;

/**
 * MMU commands
 * These are the commands that can be sent
 * to the MMU unit.
 */
typedef enum mali_mmu_command
{
	MALI_MMU_COMMAND_ENABLE_PAGING = 0x00, /**< Enable paging (memory translation) */
	MALI_MMU_COMMAND_DISABLE_PAGING = 0x01, /**< Disable paging (memory translation) */
	MALI_MMU_COMMAND_ENABLE_STALL = 0x02, /**<  Enable stall on page fault */
	MALI_MMU_COMMAND_DISABLE_STALL = 0x03, /**< Disable stall on page fault */
	MALI_MMU_COMMAND_ZAP_CACHE = 0x04, /**< Zap the entire page table cache */
	MALI_MMU_COMMAND_PAGE_FAULT_DONE = 0x05, /**< Page fault processed */
	MALI_MMU_COMMAND_HARD_RESET = 0x06 /**< Reset the MMU back to power-on settings */
} mali_mmu_command;

typedef enum mali_mmu_status_bits
{
	MALI_MMU_STATUS_BIT_PAGING_ENABLED      = 1 << 0,
	MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE   = 1 << 1,
	MALI_MMU_STATUS_BIT_STALL_ACTIVE        = 1 << 2,
	MALI_MMU_STATUS_BIT_IDLE                = 1 << 3,
	MALI_MMU_STATUS_BIT_REPLAY_BUFFER_EMPTY = 1 << 4,
	MALI_MMU_STATUS_BIT_PAGE_FAULT_IS_WRITE = 1 << 5,
} mali_mmu_status_bits;

/**
 * Definition of the MMU struct
 * Used to track a MMU unit in the system.
 * Contains information about the mapping of the registers
 */
struct mali_mmu_core
{
	struct mali_hw_core hw_core; /**< Common for all HW cores */
	struct mali_group *group;    /**< Parent core group */
	_mali_osk_irq_t *irq;        /**< IRQ handler */
};

/**
 * The MMU interrupt handler
 * Upper half of the MMU interrupt processing.
 * Called by the kernel when the MMU has triggered an interrupt.
 * The interrupt function supports IRQ sharing. So it'll probe the MMU in question
 * @param irq The irq number (not used)
 * @param dev_id Points to the MMU object being handled
 * @param regs Registers of interrupted process (not used)
 * @return Standard Linux interrupt result.
 *         Subset used by the driver is IRQ_HANDLED processed
 *                                      IRQ_NONE Not processed
 */
static _mali_osk_errcode_t mali_mmu_upper_half(void * data);

/**
 * The MMU reset hander
 * Bottom half of the MMU interrupt processing for page faults and bus errors
 * @param work The item to operate on, NULL in our case
 */
static void mali_mmu_bottom_half(void *data);

static void mali_mmu_probe_trigger(void *data);
static _mali_osk_errcode_t mali_mmu_probe_ack(void *data);

MALI_STATIC_INLINE _mali_osk_errcode_t mali_mmu_raw_reset(struct mali_mmu_core *mmu);

/* page fault queue flush helper pages
 * note that the mapping pointers are currently unused outside of the initialization functions */
static u32 mali_page_fault_flush_page_directory = MALI_INVALID_PAGE;
static u32 mali_page_fault_flush_page_table = MALI_INVALID_PAGE;
static u32 mali_page_fault_flush_data_page = MALI_INVALID_PAGE;

/* an empty page directory (no address valid) which is active on any MMU not currently marked as in use */
static u32 mali_empty_page_directory = MALI_INVALID_PAGE;

_mali_osk_errcode_t mali_mmu_initialize(void)
{
	/* allocate the helper pages */
	mali_empty_page_directory = mali_allocate_empty_page();
	if(0 == mali_empty_page_directory)
	{
		mali_empty_page_directory = MALI_INVALID_PAGE;
		return _MALI_OSK_ERR_NOMEM;
	}

	if (_MALI_OSK_ERR_OK != mali_create_fault_flush_pages(&mali_page_fault_flush_page_directory,
	                                &mali_page_fault_flush_page_table, &mali_page_fault_flush_data_page))
	{
		mali_free_empty_page(mali_empty_page_directory);
		return _MALI_OSK_ERR_FAULT;
	}

	return _MALI_OSK_ERR_OK;
}

void mali_mmu_terminate(void)
{
	MALI_DEBUG_PRINT(3, ("Mali MMU: terminating\n"));

	/* Free global helper pages */
	mali_free_empty_page(mali_empty_page_directory);

	/* Free the page fault flush pages */
	mali_destroy_fault_flush_pages(&mali_page_fault_flush_page_directory,
	                            &mali_page_fault_flush_page_table, &mali_page_fault_flush_data_page);
}

struct mali_mmu_core *mali_mmu_create(_mali_osk_resource_t *resource)
{
	struct mali_mmu_core* mmu = NULL;

	MALI_DEBUG_ASSERT_POINTER(resource);

	MALI_DEBUG_PRINT(2, ("Mali MMU: Creating Mali MMU: %s\n", resource->description));

	mmu = _mali_osk_calloc(1,sizeof(struct mali_mmu_core));
	if (NULL != mmu)
	{
		if (_MALI_OSK_ERR_OK == mali_hw_core_create(&mmu->hw_core, resource, MALI_MMU_REGISTERS_SIZE))
		{
			if (_MALI_OSK_ERR_OK == mali_mmu_reset(mmu))
			{
				/* Setup IRQ handlers (which will do IRQ probing if needed) */
				mmu->irq = _mali_osk_irq_init(resource->irq,
							      mali_mmu_upper_half,
							      mali_mmu_bottom_half,
							      mali_mmu_probe_trigger,
							      mali_mmu_probe_ack,
							      mmu,
							      "mali_mmu_irq_handlers");
				if (NULL != mmu->irq)
				{
					return mmu;
				}
				else
				{
					MALI_PRINT_ERROR(("Failed to setup interrupt handlers for MMU %s\n", mmu->hw_core.description));
				}
			}
			mali_hw_core_delete(&mmu->hw_core);
		}

		_mali_osk_free(mmu);
	}
	else
	{
		MALI_PRINT_ERROR(("Failed to allocate memory for MMU\n"));
	}

	return NULL;
}

void mali_mmu_delete(struct mali_mmu_core *mmu)
{
	_mali_osk_irq_term(mmu->irq);
	mali_hw_core_delete(&mmu->hw_core);
	_mali_osk_free(mmu);
}

void mali_mmu_set_group(struct mali_mmu_core *mmu, struct mali_group *group)
{
	mmu->group = group;
}

static void mali_mmu_enable_paging(struct mali_mmu_core *mmu)
{
	const int max_loop_count = 100;
	const int delay_in_usecs = 1;
	int i;

	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ENABLE_PAGING);

	for (i = 0; i < max_loop_count; ++i)
	{
		if (mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS) & MALI_MMU_STATUS_BIT_PAGING_ENABLED)
		{
			break;
		}
		_mali_osk_time_ubusydelay(delay_in_usecs);
	}
	if (max_loop_count == i)
	{
		MALI_PRINT_ERROR(("Enable paging request failed, MMU status is 0x%08X\n", mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS)));
	}
}

mali_bool mali_mmu_enable_stall(struct mali_mmu_core *mmu)
{
	const int max_loop_count = 100;
	const int delay_in_usecs = 999;
	int i;
	u32 mmu_status;

	/* There are no group when it is called from mali_mmu_create */
	if ( mmu->group ) MALI_ASSERT_GROUP_LOCKED(mmu->group);

	mmu_status = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS);

	if ( 0 == (mmu_status & MALI_MMU_STATUS_BIT_PAGING_ENABLED) )
	{
		MALI_DEBUG_PRINT(4, ("MMU stall is implicit when Paging is not enebled.\n"));
		return MALI_TRUE;
	}

	if ( mmu_status & MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE )
	{
		MALI_DEBUG_PRINT(3, ("Aborting MMU stall request since it is in pagefault state.\n"));
		return MALI_FALSE;
	}

	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ENABLE_STALL);

	for (i = 0; i < max_loop_count; ++i)
	{
		mmu_status = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS);
		if ( mmu_status & (MALI_MMU_STATUS_BIT_STALL_ACTIVE|MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE))
		{
			break;
		}
		if ( 0 == (mmu_status & ( MALI_MMU_STATUS_BIT_PAGING_ENABLED )))
		{
			break;
		}
		_mali_osk_time_ubusydelay(delay_in_usecs);
	}
	if (max_loop_count == i)
	{
		MALI_PRINT_ERROR(("Enable stall request failed, MMU status is 0x%08X\n", mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS)));
		return MALI_FALSE;
	}

	if ( mmu_status & MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE )
	{
		MALI_DEBUG_PRINT(3, ("Aborting MMU stall request since it has a pagefault.\n"));
		return MALI_FALSE;
	}

	return MALI_TRUE;
}

void mali_mmu_disable_stall(struct mali_mmu_core *mmu)
{
	const int max_loop_count = 100;
	const int delay_in_usecs = 1;
	int i;
	u32 mmu_status;
	/* There are no group when it is called from mali_mmu_create */
	if ( mmu->group ) MALI_ASSERT_GROUP_LOCKED(mmu->group);

	mmu_status = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS);

	if ( 0 == (mmu_status & MALI_MMU_STATUS_BIT_PAGING_ENABLED ))
	{
		MALI_DEBUG_PRINT(3, ("MMU disable skipped since it was not enabled.\n"));
		return;
	}
	if (mmu_status & MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE)
	{
		MALI_DEBUG_PRINT(2, ("Aborting MMU disable stall request since it is in pagefault state.\n"));
		return;
	}

	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_DISABLE_STALL);

	for (i = 0; i < max_loop_count; ++i)
	{
		u32 status = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS);
		if ( 0 == (status & MALI_MMU_STATUS_BIT_STALL_ACTIVE) )
		{
			break;
		}
		if ( status &  MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE )
		{
			break;
		}
		if ( 0 == (mmu_status & MALI_MMU_STATUS_BIT_PAGING_ENABLED ))
		{
			break;
		}
		_mali_osk_time_ubusydelay(delay_in_usecs);
	}
	if (max_loop_count == i) MALI_DEBUG_PRINT(1,("Disable stall request failed, MMU status is 0x%08X\n", mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS)));
}

void mali_mmu_page_fault_done(struct mali_mmu_core *mmu)
{
	MALI_ASSERT_GROUP_LOCKED(mmu->group);
	MALI_DEBUG_PRINT(4, ("Mali MMU: %s: Leaving page fault mode\n", mmu->hw_core.description));
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_PAGE_FAULT_DONE);
}

MALI_STATIC_INLINE _mali_osk_errcode_t mali_mmu_raw_reset(struct mali_mmu_core *mmu)
{
	const int max_loop_count = 100;
	const int delay_in_usecs = 1;
	int i;
	/* The _if_ is neccessary when called from mali_mmu_create and NULL==group */
	if (mmu->group)MALI_ASSERT_GROUP_LOCKED(mmu->group);

	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_DTE_ADDR, 0xCAFEBABE);
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_HARD_RESET);

	for (i = 0; i < max_loop_count; ++i)
	{
		if (mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_DTE_ADDR) == 0)
		{
			break;
		}
		_mali_osk_time_ubusydelay(delay_in_usecs);
	}
	if (max_loop_count == i)
	{
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
	/* The _if_ is neccessary when called from mali_mmu_create and NULL==group */
	if (mmu->group)
	{
		MALI_ASSERT_GROUP_LOCKED(mmu->group);
	}

	stall_success = mali_mmu_enable_stall(mmu);

	/* The stall can not fail in current hw-state */
	MALI_DEBUG_ASSERT(stall_success);

	MALI_DEBUG_PRINT(3, ("Mali MMU: mali_kernel_mmu_reset: %s\n", mmu->hw_core.description));

	if (_MALI_OSK_ERR_OK == mali_mmu_raw_reset(mmu))
	{
		mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_INT_MASK, MALI_MMU_INTERRUPT_PAGE_FAULT | MALI_MMU_INTERRUPT_READ_BUS_ERROR);
		/* no session is active, so just activate the empty page directory */
		mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_DTE_ADDR, mali_empty_page_directory);
		mali_mmu_enable_paging(mmu);
		err = _MALI_OSK_ERR_OK;
	}
	mali_mmu_disable_stall(mmu);

	return err;
}


/* ------------- interrupt handling below ------------------ */

static _mali_osk_errcode_t mali_mmu_upper_half(void * data)
{
	struct mali_mmu_core *mmu = (struct mali_mmu_core *)data;
	u32 int_stat;

	MALI_DEBUG_ASSERT_POINTER(mmu);

	/* Check if it was our device which caused the interrupt (we could be sharing the IRQ line) */
	int_stat = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_INT_STATUS);
	if (0 != int_stat)
	{
		mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_INT_MASK, 0);
		mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS);

		if (int_stat & MALI_MMU_INTERRUPT_PAGE_FAULT)
		{
			_mali_osk_irq_schedulework(mmu->irq);
		}

		if (int_stat & MALI_MMU_INTERRUPT_READ_BUS_ERROR)
		{
			/* clear interrupt flag */
			mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_INT_CLEAR, MALI_MMU_INTERRUPT_READ_BUS_ERROR);
			/* reenable it */
			mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_INT_MASK,
			                            mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_INT_MASK) | MALI_MMU_INTERRUPT_READ_BUS_ERROR);
			MALI_PRINT_ERROR(("Mali MMU: Read bus error\n"));
		}
		return _MALI_OSK_ERR_OK;
	}

	return _MALI_OSK_ERR_FAULT;
}

static void mali_mmu_bottom_half(void * data)
{
	struct mali_mmu_core *mmu = (struct mali_mmu_core*)data;
	u32 raw, status, fault_address;

	MALI_DEBUG_ASSERT_POINTER(mmu);

	MALI_DEBUG_PRINT(3, ("Mali MMU: Page fault bottom half: Locking subsystems\n"));

	mali_group_lock(mmu->group); /* Unlocked in mali_group_bottom_half */

	if ( MALI_FALSE == mali_group_power_is_on(mmu->group) )
	{
		MALI_PRINT_ERROR(("Interrupt bottom half of %s when core is OFF.",mmu->hw_core.description));
		mali_group_unlock(mmu->group);
		return;
	}

	raw = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_INT_RAWSTAT);
	status = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS);

	if ( (0==(raw & MALI_MMU_INTERRUPT_PAGE_FAULT)) &&  (0==(status & MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE)) )
	{
		MALI_DEBUG_PRINT(2, ("Mali MMU: Page fault bottom half: No Irq found.\n"));
		mali_group_unlock(mmu->group);
		/* MALI_DEBUG_ASSERT(0); */
		return;
	}

	/* An actual page fault has occurred. */

	fault_address = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_PAGE_FAULT_ADDR);

	MALI_DEBUG_PRINT(2,("Mali MMU: Page fault detected at 0x%x from bus id %d of type %s on %s\n",
	           (void*)fault_address,
	           (status >> 6) & 0x1F,
	           (status & 32) ? "write" : "read",
	           mmu->hw_core.description));

	mali_group_bottom_half(mmu->group, GROUP_EVENT_MMU_PAGE_FAULT); /* Unlocks the group lock */
}

mali_bool mali_mmu_zap_tlb(struct mali_mmu_core *mmu)
{
	mali_bool stall_success;
	MALI_ASSERT_GROUP_LOCKED(mmu->group);

	stall_success = mali_mmu_enable_stall(mmu);

	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ZAP_CACHE);

	if (MALI_FALSE == stall_success)
	{
		/* False means that it is in Pagefault state. Not possible to disable_stall then */
		return MALI_FALSE;
	}

	mali_mmu_disable_stall(mmu);
	return MALI_TRUE;
}

void mali_mmu_zap_tlb_without_stall(struct mali_mmu_core *mmu)
{
	MALI_ASSERT_GROUP_LOCKED(mmu->group);
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ZAP_CACHE);
}


void mali_mmu_invalidate_page(struct mali_mmu_core *mmu, u32 mali_address)
{
	MALI_ASSERT_GROUP_LOCKED(mmu->group);
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_ZAP_ONE_LINE, MALI_MMU_PDE_ENTRY(mali_address));
}

static void mali_mmu_activate_address_space(struct mali_mmu_core *mmu, u32 page_directory)
{
	MALI_ASSERT_GROUP_LOCKED(mmu->group);
	/* The MMU must be in stalled or page fault mode, for this writing to work */
	MALI_DEBUG_ASSERT( 0 != ( mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS)
							  & (MALI_MMU_STATUS_BIT_STALL_ACTIVE|MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE) ) );
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_DTE_ADDR, page_directory);
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_COMMAND, MALI_MMU_COMMAND_ZAP_CACHE);

}

mali_bool mali_mmu_activate_page_directory(struct mali_mmu_core *mmu, struct mali_page_directory *pagedir)
{
	mali_bool stall_success;
	MALI_DEBUG_ASSERT_POINTER(mmu);
	MALI_ASSERT_GROUP_LOCKED(mmu->group);

	MALI_DEBUG_PRINT(5, ("Asked to activate page directory 0x%x on MMU %s\n", pagedir, mmu->hw_core.description));
	stall_success = mali_mmu_enable_stall(mmu);

	if ( MALI_FALSE==stall_success ) return MALI_FALSE;
	mali_mmu_activate_address_space(mmu, pagedir->page_directory);
	mali_mmu_disable_stall(mmu);
	return MALI_TRUE;
}

void mali_mmu_activate_empty_page_directory(struct mali_mmu_core* mmu)
{
	mali_bool stall_success;
	MALI_DEBUG_ASSERT_POINTER(mmu);
	MALI_ASSERT_GROUP_LOCKED(mmu->group);
	MALI_DEBUG_PRINT(3, ("Activating the empty page directory on MMU %s\n", mmu->hw_core.description));

	stall_success = mali_mmu_enable_stall(mmu);
	/* This function can only be called when the core is idle, so it could not fail. */
	MALI_DEBUG_ASSERT( stall_success );
	mali_mmu_activate_address_space(mmu, mali_empty_page_directory);
	mali_mmu_disable_stall(mmu);
}

void mali_mmu_activate_fault_flush_page_directory(struct mali_mmu_core* mmu)
{
	mali_bool stall_success;
	MALI_DEBUG_ASSERT_POINTER(mmu);
	MALI_ASSERT_GROUP_LOCKED(mmu->group);

	MALI_DEBUG_PRINT(3, ("Activating the page fault flush page directory on MMU %s\n", mmu->hw_core.description));
	stall_success = mali_mmu_enable_stall(mmu);
	/* This function is expect to fail the stalling, since it might be in PageFault mode when it is called */
	mali_mmu_activate_address_space(mmu, mali_page_fault_flush_page_directory);
	if ( MALI_TRUE==stall_success ) mali_mmu_disable_stall(mmu);
}

/* Is called when we want the mmu to give an interrupt */
static void mali_mmu_probe_trigger(void *data)
{
	struct mali_mmu_core *mmu = (struct mali_mmu_core *)data;
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_INT_RAWSTAT, MALI_MMU_INTERRUPT_PAGE_FAULT|MALI_MMU_INTERRUPT_READ_BUS_ERROR);
}

/* Is called when the irq probe wants the mmu to acknowledge an interrupt from the hw */
static _mali_osk_errcode_t mali_mmu_probe_ack(void *data)
{
	struct mali_mmu_core *mmu = (struct mali_mmu_core *)data;
	u32 int_stat;

	int_stat = mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_INT_STATUS);

	MALI_DEBUG_PRINT(2, ("mali_mmu_probe_irq_acknowledge: intstat 0x%x\n", int_stat));
	if (int_stat & MALI_MMU_INTERRUPT_PAGE_FAULT)
	{
		MALI_DEBUG_PRINT(2, ("Probe: Page fault detect: PASSED\n"));
		mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_INT_CLEAR, MALI_MMU_INTERRUPT_PAGE_FAULT);
	}
	else
	{
		MALI_DEBUG_PRINT(1, ("Probe: Page fault detect: FAILED\n"));
	}

	if (int_stat & MALI_MMU_INTERRUPT_READ_BUS_ERROR)
	{
		MALI_DEBUG_PRINT(2, ("Probe: Bus read error detect: PASSED\n"));
		mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_INT_CLEAR, MALI_MMU_INTERRUPT_READ_BUS_ERROR);
	}
	else
	{
		MALI_DEBUG_PRINT(1, ("Probe: Bus read error detect: FAILED\n"));
	}

	if ( (int_stat & (MALI_MMU_INTERRUPT_PAGE_FAULT|MALI_MMU_INTERRUPT_READ_BUS_ERROR)) ==
	                 (MALI_MMU_INTERRUPT_PAGE_FAULT|MALI_MMU_INTERRUPT_READ_BUS_ERROR))
	{
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
