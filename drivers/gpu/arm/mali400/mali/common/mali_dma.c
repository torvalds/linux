/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2012-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_hw_core.h"
#include "mali_dma.h"

/**
 * Size of the Mali-450 DMA unit registers in bytes.
 */
#define MALI450_DMA_REG_SIZE 0x08

/**
 * Value that appears in MEMSIZE if an error occurs when reading the command list.
 */
#define MALI450_DMA_BUS_ERR_VAL 0xffffffff

/**
 * Mali DMA registers
 * Used in the register read/write routines.
 * See the hardware documentation for more information about each register.
 */
typedef enum mali_dma_register {

	MALI450_DMA_REG_SOURCE_ADDRESS = 0x0000,
	MALI450_DMA_REG_SOURCE_SIZE = 0x0004,
} mali_dma_register;

struct mali_dma_core {
	struct mali_hw_core  hw_core;      /**< Common for all HW cores */
	_mali_osk_spinlock_t *lock;            /**< Lock protecting access to DMA core */
	mali_dma_pool pool;                /**< Memory pool for command buffers */
};

static struct mali_dma_core *mali_global_dma_core = NULL;

struct mali_dma_core *mali_dma_create(_mali_osk_resource_t *resource)
{
	struct mali_dma_core* dma;
	_mali_osk_errcode_t err;

	MALI_DEBUG_ASSERT(NULL == mali_global_dma_core);

	dma = _mali_osk_malloc(sizeof(struct mali_dma_core));
	if (dma == NULL) goto alloc_failed;

	dma->lock = _mali_osk_spinlock_init(_MALI_OSK_LOCKFLAG_ORDERED, _MALI_OSK_LOCK_ORDER_DMA_COMMAND);
	if (NULL == dma->lock) goto lock_init_failed;

	dma->pool = mali_dma_pool_create(MALI_DMA_CMD_BUF_SIZE, 4, 0);
	if (NULL == dma->pool) goto dma_pool_failed;

	err = mali_hw_core_create(&dma->hw_core, resource, MALI450_DMA_REG_SIZE);
	if (_MALI_OSK_ERR_OK != err) goto hw_core_failed;

	mali_global_dma_core = dma;
	MALI_DEBUG_PRINT(2, ("Mali DMA: Created Mali APB DMA unit\n"));
	return dma;

	/* Error handling */

hw_core_failed:
	mali_dma_pool_destroy(dma->pool);
dma_pool_failed:
	_mali_osk_spinlock_term(dma->lock);
lock_init_failed:
	_mali_osk_free(dma);
alloc_failed:
	MALI_DEBUG_PRINT(2, ("Mali DMA: Failed to create APB DMA unit\n"));
	return NULL;
}

void mali_dma_delete(struct mali_dma_core *dma)
{
	MALI_DEBUG_ASSERT_POINTER(dma);

	MALI_DEBUG_PRINT(2, ("Mali DMA: Deleted Mali APB DMA unit\n"));

	mali_hw_core_delete(&dma->hw_core);
	_mali_osk_spinlock_term(dma->lock);
	mali_dma_pool_destroy(dma->pool);
	_mali_osk_free(dma);
}

static void mali_dma_bus_error(struct mali_dma_core *dma)
{
	u32 addr = mali_hw_core_register_read(&dma->hw_core, MALI450_DMA_REG_SOURCE_ADDRESS);

	MALI_PRINT_ERROR(("Mali DMA: Bus error when reading command list from 0x%lx\n", addr));

	/* Clear the bus error */
	mali_hw_core_register_write(&dma->hw_core, MALI450_DMA_REG_SOURCE_SIZE, 0);
}

static mali_bool mali_dma_is_busy(struct mali_dma_core *dma)
{
	u32 val;
	mali_bool dma_busy_flag = MALI_FALSE;

	MALI_DEBUG_ASSERT_POINTER(dma);

	val = mali_hw_core_register_read(&dma->hw_core, MALI450_DMA_REG_SOURCE_SIZE);

	if (MALI450_DMA_BUS_ERR_VAL == val) {
		/* Bus error reading command list */
		mali_dma_bus_error(dma);
		return MALI_FALSE;
	}
	if (val > 0) {
		dma_busy_flag = MALI_TRUE;
	}

	return dma_busy_flag;
}

static void mali_dma_start_transfer(struct mali_dma_core* dma, mali_dma_cmd_buf *buf)
{
	u32 memsize = buf->size * 4;
	u32 addr = buf->phys_addr;

	MALI_DEBUG_ASSERT_POINTER(dma);
	MALI_DEBUG_ASSERT(memsize < (1 << 16));
	MALI_DEBUG_ASSERT(0 == (memsize & 0x3)); /* 4 byte aligned */

	MALI_DEBUG_ASSERT(!mali_dma_is_busy(dma));

	/* Writes the physical source memory address of chunk containing command headers and data */
	mali_hw_core_register_write(&dma->hw_core, MALI450_DMA_REG_SOURCE_ADDRESS, addr);

	/* Writes the length of transfer */
	mali_hw_core_register_write(&dma->hw_core, MALI450_DMA_REG_SOURCE_SIZE, memsize);
}

_mali_osk_errcode_t mali_dma_get_cmd_buf(mali_dma_cmd_buf *buf)
{
	MALI_DEBUG_ASSERT_POINTER(buf);

	buf->virt_addr = (u32*)mali_dma_pool_alloc(mali_global_dma_core->pool, &buf->phys_addr);
	if (NULL == buf->virt_addr) {
		return _MALI_OSK_ERR_NOMEM;
	}

	/* size contains the number of words in the buffer and is incremented
	 * as commands are added to the buffer. */
	buf->size = 0;

	return _MALI_OSK_ERR_OK;
}

void mali_dma_put_cmd_buf(mali_dma_cmd_buf *buf)
{
	MALI_DEBUG_ASSERT_POINTER(buf);

	if (NULL == buf->virt_addr) return;

	mali_dma_pool_free(mali_global_dma_core->pool, buf->virt_addr, buf->phys_addr);

	buf->virt_addr = NULL;
}

_mali_osk_errcode_t mali_dma_start(struct mali_dma_core* dma, mali_dma_cmd_buf *buf)
{
	_mali_osk_errcode_t err = _MALI_OSK_ERR_OK;

	_mali_osk_spinlock_lock(dma->lock);

	if (mali_dma_is_busy(dma)) {
		err = _MALI_OSK_ERR_BUSY;
		goto out;
	}

	mali_dma_start_transfer(dma, buf);

out:
	_mali_osk_spinlock_unlock(dma->lock);
	return err;
}

void mali_dma_debug(struct mali_dma_core *dma)
{
	MALI_DEBUG_ASSERT_POINTER(dma);
	MALI_DEBUG_PRINT(1, ("DMA unit registers:\n\t%08x, %08x\n",
	                     mali_hw_core_register_read(&dma->hw_core, MALI450_DMA_REG_SOURCE_ADDRESS),
	                     mali_hw_core_register_read(&dma->hw_core, MALI450_DMA_REG_SOURCE_SIZE)
	                    ));

}

struct mali_dma_core *mali_dma_get_global_dma_core(void)
{
	/* Returns the global dma core object */
	return mali_global_dma_core;
}
