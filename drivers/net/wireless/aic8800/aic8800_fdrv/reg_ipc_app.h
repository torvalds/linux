/**
 ****************************************************************************************
 *
 * @file rwnx_ipc_app.h
 *
 * @brief IPC module register definitions
 *
 * Copyright (C) RivieraWaves 2011-2019
 *
 ****************************************************************************************
 */

#ifndef _REG_IPC_APP_H_
#define _REG_IPC_APP_H_

#ifndef __KERNEL__
#include <stdint.h>
#include "arch.h"
#else
#include "ipc_compat.h"
#endif
#include "reg_access.h"

#define REG_IPC_APP_DECODING_MASK 0x0000007F

/**
 * @brief APP2EMB_TRIGGER register definition
 * <pre>
 *   Bits           Field Name   Reset Value
 *  -----   ------------------   -----------
 *  31:00      APP2EMB_TRIGGER   0x0
 * </pre>
 */
#define IPC_APP2EMB_TRIGGER_ADDR   0x12000000
#define IPC_APP2EMB_TRIGGER_OFFSET 0x00000000
#define IPC_APP2EMB_TRIGGER_INDEX  0x00000000
#define IPC_APP2EMB_TRIGGER_RESET  0x00000000

__INLINE u32 ipc_app2emb_trigger_get(void *env)
{
    return REG_IPC_APP_RD(env, IPC_APP2EMB_TRIGGER_INDEX);
}

__INLINE void ipc_app2emb_trigger_set(void *env, u32 value)
{
    REG_IPC_APP_WR(env, IPC_APP2EMB_TRIGGER_INDEX, value);
}

// field definitions
#define IPC_APP2EMB_TRIGGER_MASK   ((u32)0xFFFFFFFF)
#define IPC_APP2EMB_TRIGGER_LSB    0
#define IPC_APP2EMB_TRIGGER_WIDTH  ((u32)0x00000020)

#define IPC_APP2EMB_TRIGGER_RST    0x0

__INLINE u32 ipc_app2emb_trigger_getf(void *env)
{
    u32 localVal = REG_IPC_APP_RD(env, IPC_APP2EMB_TRIGGER_INDEX);
    ASSERT_ERR((localVal & ~((u32)0xFFFFFFFF)) == 0);
    return (localVal >> 0);
}

__INLINE void ipc_app2emb_trigger_setf(void *env, u32 app2embtrigger)
{
    ASSERT_ERR((((u32)app2embtrigger << 0) & ~((u32)0xFFFFFFFF)) == 0);
    REG_IPC_APP_WR(env, IPC_APP2EMB_TRIGGER_INDEX, (u32)app2embtrigger << 0);
}

/**
 * @brief EMB2APP_RAWSTATUS register definition
 * <pre>
 *   Bits           Field Name   Reset Value
 *  -----   ------------------   -----------
 *  31:00    EMB2APP_RAWSTATUS   0x0
 * </pre>
 */
#define IPC_EMB2APP_RAWSTATUS_ADDR   0x12000004
#define IPC_EMB2APP_RAWSTATUS_OFFSET 0x00000004
#define IPC_EMB2APP_RAWSTATUS_INDEX  0x00000001
#define IPC_EMB2APP_RAWSTATUS_RESET  0x00000000

__INLINE u32 ipc_emb2app_rawstatus_get(void *env)
{
    return REG_IPC_APP_RD(env, IPC_EMB2APP_RAWSTATUS_INDEX);
}

__INLINE void ipc_emb2app_rawstatus_set(void *env, u32 value)
{
    REG_IPC_APP_WR(env, IPC_EMB2APP_RAWSTATUS_INDEX, value);
}

// field definitions
#define IPC_EMB2APP_RAWSTATUS_MASK   ((u32)0xFFFFFFFF)
#define IPC_EMB2APP_RAWSTATUS_LSB    0
#define IPC_EMB2APP_RAWSTATUS_WIDTH  ((u32)0x00000020)

#define IPC_EMB2APP_RAWSTATUS_RST    0x0

__INLINE u32 ipc_emb2app_rawstatus_getf(void *env)
{
    u32 localVal = REG_IPC_APP_RD(env, IPC_EMB2APP_RAWSTATUS_INDEX);
    ASSERT_ERR((localVal & ~((u32)0xFFFFFFFF)) == 0);
    return (localVal >> 0);
}

/**
 * @brief EMB2APP_ACK register definition
 * <pre>
 *   Bits           Field Name   Reset Value
 *  -----   ------------------   -----------
 *  31:00          EMB2APP_ACK   0x0
 * </pre>
 */
#define IPC_EMB2APP_ACK_ADDR   0x12000008
#define IPC_EMB2APP_ACK_OFFSET 0x00000008
#define IPC_EMB2APP_ACK_INDEX  0x00000002
#define IPC_EMB2APP_ACK_RESET  0x00000000

__INLINE u32 ipc_emb2app_ack_get(void *env)
{
    return REG_IPC_APP_RD(env, IPC_EMB2APP_ACK_INDEX);
}

__INLINE void ipc_emb2app_ack_clear(void *env, u32 value)
{
    REG_IPC_APP_WR(env, IPC_EMB2APP_ACK_INDEX, value);
}

// field definitions
#define IPC_EMB2APP_ACK_MASK   ((u32)0xFFFFFFFF)
#define IPC_EMB2APP_ACK_LSB    0
#define IPC_EMB2APP_ACK_WIDTH  ((u32)0x00000020)

#define IPC_EMB2APP_ACK_RST    0x0

__INLINE u32 ipc_emb2app_ack_getf(void *env)
{
    u32 localVal = REG_IPC_APP_RD(env, IPC_EMB2APP_ACK_INDEX);
    ASSERT_ERR((localVal & ~((u32)0xFFFFFFFF)) == 0);
    return (localVal >> 0);
}

__INLINE void ipc_emb2app_ack_clearf(void *env, u32 emb2appack)
{
    ASSERT_ERR((((u32)emb2appack << 0) & ~((u32)0xFFFFFFFF)) == 0);
    REG_IPC_APP_WR(env, IPC_EMB2APP_ACK_INDEX, (u32)emb2appack << 0);
}

/**
 * @brief EMB2APP_UNMASK_SET register definition
 * <pre>
 *   Bits           Field Name   Reset Value
 *  -----   ------------------   -----------
 *  31:00       EMB2APP_UNMASK   0x0
 * </pre>
 */
#define IPC_EMB2APP_UNMASK_SET_ADDR   0x1200000C
#define IPC_EMB2APP_UNMASK_SET_OFFSET 0x0000000C
#define IPC_EMB2APP_UNMASK_SET_INDEX  0x00000003
#define IPC_EMB2APP_UNMASK_SET_RESET  0x00000000

__INLINE u32 ipc_emb2app_unmask_get(void *env)
{
    return REG_IPC_APP_RD(env, IPC_EMB2APP_UNMASK_SET_INDEX);
}

__INLINE void ipc_emb2app_unmask_set(void *env, u32 value)
{
    REG_IPC_APP_WR(env, IPC_EMB2APP_UNMASK_SET_INDEX, value);
}

// field definitions
#define IPC_EMB2APP_UNMASK_MASK   ((u32)0xFFFFFFFF)
#define IPC_EMB2APP_UNMASK_LSB    0
#define IPC_EMB2APP_UNMASK_WIDTH  ((u32)0x00000020)

#define IPC_EMB2APP_UNMASK_RST    0x0

__INLINE u32 ipc_emb2app_unmask_getf(void *env)
{
    u32 localVal = REG_IPC_APP_RD(env, IPC_EMB2APP_UNMASK_SET_INDEX);
    ASSERT_ERR((localVal & ~((u32)0xFFFFFFFF)) == 0);
    return (localVal >> 0);
}

__INLINE void ipc_emb2app_unmask_setf(void *env, u32 emb2appunmask)
{
    ASSERT_ERR((((u32)emb2appunmask << 0) & ~((u32)0xFFFFFFFF)) == 0);
    REG_IPC_APP_WR(env, IPC_EMB2APP_UNMASK_SET_INDEX, (u32)emb2appunmask << 0);
}

/**
 * @brief EMB2APP_UNMASK_CLEAR register definition
 * <pre>
 *   Bits           Field Name   Reset Value
 *  -----   ------------------   -----------
 *  31:00       EMB2APP_UNMASK   0x0
 * </pre>
 */
#define IPC_EMB2APP_UNMASK_CLEAR_ADDR   0x12000010
#define IPC_EMB2APP_UNMASK_CLEAR_OFFSET 0x00000010
#define IPC_EMB2APP_UNMASK_CLEAR_INDEX  0x00000004
#define IPC_EMB2APP_UNMASK_CLEAR_RESET  0x00000000

__INLINE void ipc_emb2app_unmask_clear(void *env, u32 value)
{
    REG_IPC_APP_WR(env, IPC_EMB2APP_UNMASK_CLEAR_INDEX, value);
}

// fields defined in symmetrical set/clear register
__INLINE void ipc_emb2app_unmask_clearf(void *env, u32 emb2appunmask)
{
    ASSERT_ERR((((u32)emb2appunmask << 0) & ~((u32)0xFFFFFFFF)) == 0);
    REG_IPC_APP_WR(env, IPC_EMB2APP_UNMASK_CLEAR_INDEX, (u32)emb2appunmask << 0);
}

/**
 * @brief EMB2APP_STATUS register definition
 * <pre>
 *   Bits           Field Name   Reset Value
 *  -----   ------------------   -----------
 *  31:00       EMB2APP_STATUS   0x0
 * </pre>
 */
#ifdef CONFIG_RWNX_OLD_IPC
#define IPC_EMB2APP_STATUS_ADDR   0x12000014
#define IPC_EMB2APP_STATUS_OFFSET 0x00000014
#define IPC_EMB2APP_STATUS_INDEX  0x00000005
#else
#define IPC_EMB2APP_STATUS_ADDR   0x1200001C
#define IPC_EMB2APP_STATUS_OFFSET 0x0000001C
#define IPC_EMB2APP_STATUS_INDEX  0x00000007
#endif
#define IPC_EMB2APP_STATUS_RESET  0x00000000

__INLINE u32 ipc_emb2app_status_get(void *env)
{
    return REG_IPC_APP_RD(env, IPC_EMB2APP_STATUS_INDEX);
}

__INLINE void ipc_emb2app_status_set(void *env, u32 value)
{
    REG_IPC_APP_WR(env, IPC_EMB2APP_STATUS_INDEX, value);
}

// field definitions
#define IPC_EMB2APP_STATUS_MASK   ((u32)0xFFFFFFFF)
#define IPC_EMB2APP_STATUS_LSB    0
#define IPC_EMB2APP_STATUS_WIDTH  ((u32)0x00000020)

#define IPC_EMB2APP_STATUS_RST    0x0

__INLINE u32 ipc_emb2app_status_getf(void *env)
{
    u32 localVal = REG_IPC_APP_RD(env, IPC_EMB2APP_STATUS_INDEX);
    ASSERT_ERR((localVal & ~((u32)0xFFFFFFFF)) == 0);
    return (localVal >> 0);
}

/**
 * @brief APP_SIGNATURE register definition
 * <pre>
 *   Bits           Field Name   Reset Value
 *  -----   ------------------   ----------
 *  31:00        APP_SIGNATURE   0x0
 * </pre>
 */
#define IPC_APP_SIGNATURE_ADDR   0x12000040
#define IPC_APP_SIGNATURE_OFFSET 0x00000040
#define IPC_APP_SIGNATURE_INDEX  0x00000010
#define IPC_APP_SIGNATURE_RESET  0x00000000

__INLINE u32 ipc_app_signature_get(void *env)
{
      return REG_IPC_APP_RD(env, IPC_APP_SIGNATURE_INDEX);
}

__INLINE void ipc_app_signature_set(void *env, u32 value)
{
    REG_IPC_APP_WR(env, IPC_APP_SIGNATURE_INDEX, value);
}

// field definitions
#define IPC_APP_SIGNATURE_MASK   ((u32)0xFFFFFFFF)
#define IPC_APP_SIGNATURE_LSB    0
#define IPC_APP_SIGNATURE_WIDTH  ((u32)0x00000020)

#define IPC_APP_SIGNATURE_RST    0x0

__INLINE u32 ipc_app_signature_getf(void *env)
{
    u32 localVal = REG_IPC_APP_RD(env, IPC_APP_SIGNATURE_INDEX);
    ASSERT_ERR((localVal & ~((u32)0xFFFFFFFF)) == 0);
    return (localVal >> 0);
}


#endif // _REG_IPC_APP_H_

