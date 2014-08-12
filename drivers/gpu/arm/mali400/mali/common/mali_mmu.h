/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2007-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#ifndef __MALI_MMU_H__
#define __MALI_MMU_H__

#include "mali_osk.h"
#include "mali_mmu_page_directory.h"
#include "mali_hw_core.h"

/* Forward declaration from mali_group.h */
struct mali_group;

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
typedef enum mali_mmu_interrupt {
	MALI_MMU_INTERRUPT_PAGE_FAULT = 0x01, /**< A page fault occured */
	MALI_MMU_INTERRUPT_READ_BUS_ERROR = 0x02 /**< A bus read error occured */
} mali_mmu_interrupt;

typedef enum mali_mmu_status_bits {
	MALI_MMU_STATUS_BIT_PAGING_ENABLED      = 1 << 0,
	MALI_MMU_STATUS_BIT_PAGE_FAULT_ACTIVE   = 1 << 1,
	MALI_MMU_STATUS_BIT_STALL_ACTIVE        = 1 << 2,
	MALI_MMU_STATUS_BIT_IDLE                = 1 << 3,
	MALI_MMU_STATUS_BIT_REPLAY_BUFFER_EMPTY = 1 << 4,
	MALI_MMU_STATUS_BIT_PAGE_FAULT_IS_WRITE = 1 << 5,
	MALI_MMU_STATUS_BIT_STALL_NOT_ACTIVE    = 1 << 31,
} mali_mmu_status_bits;

/**
 * Definition of the MMU struct
 * Used to track a MMU unit in the system.
 * Contains information about the mapping of the registers
 */
struct mali_mmu_core {
	struct mali_hw_core hw_core; /**< Common for all HW cores */
	_mali_osk_irq_t *irq;        /**< IRQ handler */
};

_mali_osk_errcode_t mali_mmu_initialize(void);

void mali_mmu_terminate(void);

struct mali_mmu_core *mali_mmu_create(_mali_osk_resource_t *resource, struct mali_group *group, mali_bool is_virtual);
void mali_mmu_delete(struct mali_mmu_core *mmu);

_mali_osk_errcode_t mali_mmu_reset(struct mali_mmu_core *mmu);
mali_bool mali_mmu_zap_tlb(struct mali_mmu_core *mmu);
void mali_mmu_zap_tlb_without_stall(struct mali_mmu_core *mmu);
void mali_mmu_invalidate_page(struct mali_mmu_core *mmu, u32 mali_address);

void mali_mmu_activate_page_directory(struct mali_mmu_core* mmu, struct mali_page_directory *pagedir);
void mali_mmu_activate_empty_page_directory(struct mali_mmu_core* mmu);
void mali_mmu_activate_fault_flush_page_directory(struct mali_mmu_core* mmu);

void mali_mmu_page_fault_done(struct mali_mmu_core *mmu);

/*** Register reading/writing functions ***/
MALI_STATIC_INLINE u32 mali_mmu_get_int_status(struct mali_mmu_core *mmu)
{
	return mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_INT_STATUS);
}

MALI_STATIC_INLINE u32 mali_mmu_get_rawstat(struct mali_mmu_core *mmu)
{
	return mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_INT_RAWSTAT);
}

MALI_STATIC_INLINE void mali_mmu_mask_all_interrupts(struct mali_mmu_core *mmu)
{
	mali_hw_core_register_write(&mmu->hw_core, MALI_MMU_REGISTER_INT_MASK, 0);
}

MALI_STATIC_INLINE u32 mali_mmu_get_status(struct mali_mmu_core *mmu)
{
	return mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_STATUS);
}

MALI_STATIC_INLINE u32 mali_mmu_get_page_fault_addr(struct mali_mmu_core *mmu)
{
	return mali_hw_core_register_read(&mmu->hw_core, MALI_MMU_REGISTER_PAGE_FAULT_ADDR);
}

#endif /* __MALI_MMU_H__ */
