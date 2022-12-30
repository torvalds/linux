/**
 ****************************************************************************************
 *
 * @file ipc_compat.h
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ****************************************************************************************
 */

#ifndef _IPC_H_
#define _IPC_H_

#define __INLINE static __attribute__((__always_inline__)) inline

#define __ALIGN4 __aligned(4)

#define ASSERT_ERR(condition)                                                           \
    do {                                                                                \
        if (unlikely(!(condition))) {                                                   \
            printk(DBG_PREFIX KERN_ERR "%s:%d:ASSERT_ERR(" #condition ")\n", __FILE__,  __LINE__); \
        }                                                                               \
    } while(0)

#endif /* _IPC_H_ */
