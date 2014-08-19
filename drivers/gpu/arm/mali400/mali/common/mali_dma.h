/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2012-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#ifndef __MALI_DMA_H__
#define __MALI_DMA_H__

#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_hw_core.h"

#define MALI_DMA_CMD_BUF_SIZE 1024

typedef struct mali_dma_cmd_buf {
	u32 *virt_addr;           /**< CPU address of command buffer */
	mali_dma_addr phys_addr;  /**< Physical address of command buffer */
	u32 size;                 /**< Number of prepared words in command buffer */
} mali_dma_cmd_buf;

/** @brief Create a new DMA unit
 *
 * This is called from entry point of the driver in order to create and
 * intialize the DMA resource
 *
 * @param resource it will be a pointer to a DMA resource
 * @return DMA object on success, NULL on failure
 */
struct mali_dma_core *mali_dma_create(_mali_osk_resource_t *resource);

/** @brief Delete DMA unit
 *
 * This is called on entry point of driver if the driver initialization fails
 * after initialization of the DMA unit. It is also called on the exit of the
 * driver to delete the DMA resource
 *
 * @param dma Pointer to DMA unit object
 */
void mali_dma_delete(struct mali_dma_core *dma);

/** @brief Retrieves the MALI DMA core object (if there is)
 *
 * @return The Mali DMA object otherwise NULL
 */
struct mali_dma_core *mali_dma_get_global_dma_core(void);

/**
 * @brief Run a command buffer on the DMA unit
 *
 * @param dma Pointer to the DMA unit to use
 * @param buf Pointer to the command buffer to use
 * @return _MALI_OSK_ERR_OK if the buffer was started successfully,
 *         _MALI_OSK_ERR_BUSY if the DMA unit is busy.
 */
_mali_osk_errcode_t mali_dma_start(struct mali_dma_core *dma, mali_dma_cmd_buf *buf);

/**
 * @brief Create a DMA command
 *
 * @param core Mali core
 * @param reg offset to register of core
 * @param n number of registers to write
 */
MALI_STATIC_INLINE u32 mali_dma_command_write(struct mali_hw_core *core, u32 reg, u32 n)
{
	u32 core_offset = core->phys_offset;

	MALI_DEBUG_ASSERT(reg < 0x2000);
	MALI_DEBUG_ASSERT(n < 0x800);
	MALI_DEBUG_ASSERT(core_offset < 0x30000);
	MALI_DEBUG_ASSERT(0 == ((core_offset + reg) & ~0x7FFFF));

	return (n << 20) | (core_offset + reg);
}

/**
 * @brief Add a array write to DMA command buffer
 *
 * @param buf DMA command buffer to fill in
 * @param core Core to do DMA to
 * @param reg Register on core to start writing to
 * @param data Pointer to data to write
 * @param count Number of 4 byte words to write
 */
MALI_STATIC_INLINE void mali_dma_write_array(mali_dma_cmd_buf *buf, struct mali_hw_core *core,
		u32 reg, u32 *data, u32 count)
{
	MALI_DEBUG_ASSERT((buf->size + 1 + count) < MALI_DMA_CMD_BUF_SIZE / 4);

	buf->virt_addr[buf->size++] = mali_dma_command_write(core, reg, count);

	_mali_osk_memcpy(buf->virt_addr + buf->size, data, count * sizeof(*buf->virt_addr));

	buf->size += count;
}

/**
 * @brief Add a conditional array write to DMA command buffer
 *
 * @param buf DMA command buffer to fill in
 * @param core Core to do DMA to
 * @param reg Register on core to start writing to
 * @param data Pointer to data to write
 * @param count Number of 4 byte words to write
 * @param ref Pointer to referance data that can be skipped if equal
 */
MALI_STATIC_INLINE void mali_dma_write_array_conditional(mali_dma_cmd_buf *buf, struct mali_hw_core *core,
		u32 reg, u32 *data, u32 count, const u32 *ref)
{
	/* Do conditional array writes are not yet implemented, fallback to a
	 * normal array write. */
	mali_dma_write_array(buf, core, reg, data, count);
}

/**
 * @brief Add a conditional register write to the DMA command buffer
 *
 * If the data matches the reference the command will be skipped.
 *
 * @param buf DMA command buffer to fill in
 * @param core Core to do DMA to
 * @param reg Register on core to start writing to
 * @param data Pointer to data to write
 * @param ref Pointer to referance data that can be skipped if equal
 */
MALI_STATIC_INLINE void mali_dma_write_conditional(mali_dma_cmd_buf *buf, struct mali_hw_core *core,
		u32 reg, u32 data, const u32 ref)
{
	/* Skip write if reference value is equal to data. */
	if (data == ref) return;

	buf->virt_addr[buf->size++] = mali_dma_command_write(core, reg, 1);

	buf->virt_addr[buf->size++] = data;

	MALI_DEBUG_ASSERT(buf->size < MALI_DMA_CMD_BUF_SIZE / 4);
}

/**
 * @brief Add a register write to the DMA command buffer
 *
 * @param buf DMA command buffer to fill in
 * @param core Core to do DMA to
 * @param reg Register on core to start writing to
 * @param data Pointer to data to write
 */
MALI_STATIC_INLINE void mali_dma_write(mali_dma_cmd_buf *buf, struct mali_hw_core *core,
				       u32 reg, u32 data)
{
	buf->virt_addr[buf->size++] = mali_dma_command_write(core, reg, 1);

	buf->virt_addr[buf->size++] = data;

	MALI_DEBUG_ASSERT(buf->size < MALI_DMA_CMD_BUF_SIZE / 4);
}

/**
 * @brief Prepare DMA command buffer for use
 *
 * This function allocates the DMA buffer itself.
 *
 * @param buf The mali_dma_cmd_buf to prepare
 * @return _MALI_OSK_ERR_OK if the \a buf is ready to use
 */
_mali_osk_errcode_t mali_dma_get_cmd_buf(mali_dma_cmd_buf *buf);

/**
 * @brief Check if a DMA command buffer is ready for use
 *
 * @param buf The mali_dma_cmd_buf to check
 * @return MALI_TRUE if buffer is usable, MALI_FALSE otherwise
 */
MALI_STATIC_INLINE mali_bool mali_dma_cmd_buf_is_valid(mali_dma_cmd_buf *buf)
{
	return NULL != buf->virt_addr;
}

/**
 * @brief Return a DMA command buffer
 *
 * @param buf Pointer to DMA command buffer to return
 */
void mali_dma_put_cmd_buf(mali_dma_cmd_buf *buf);

#endif /* __MALI_DMA_H__ */
