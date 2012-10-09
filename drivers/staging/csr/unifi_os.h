/*
 * ---------------------------------------------------------------------------
 *
 * FILE: os_linux/unifi_os.h
 *
 * PURPOSE:
 *      This header file provides the OS-dependent facilities for a linux
 *      environment.
 *
 * Copyright (C) 2005-2008 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */
#ifndef __UNIFI_OS_LINUX_H__
#define __UNIFI_OS_LINUX_H__ 1

#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/string.h>

/*
 * Needed for core/signals.c
 */
#include <stddef.h>


/* Define INLINE directive*/
#define INLINE      inline

/* Malloc and free */
CsrResult unifi_net_data_malloc(void *ospriv, bulk_data_desc_t *bulk_data_slot, unsigned int size);
void unifi_net_data_free(void *ospriv, bulk_data_desc_t *bulk_data_slot);
#define CSR_WIFI_ALIGN_BYTES    4
CsrResult unifi_net_dma_align(void *ospriv, bulk_data_desc_t *bulk_data_slot);

/*
 * Byte Order
 * Note that __le*_to_cpu and __cpu_to_le* return an unsigned value!
 */
#ifdef __KERNEL__
#define unifi2host_16(n)	(__le16_to_cpu((n)))
#define unifi2host_32(n)	(__le32_to_cpu((n)))
#define host2unifi_16(n)	(__cpu_to_le16((n)))
#define host2unifi_32(n)	(__cpu_to_le32((n)))
#endif

/* Module parameters */
extern int unifi_debug;

/* debugging */
#ifdef UNIFI_DEBUG
/*
 * unifi_debug is a verbosity level for debug messages
 * UDBG0 msgs are always printed if UNIFI_DEBUG is defined
 * UDBG1 msgs are printed if UNIFI_DEBUG is defined and unifi_debug > 0
 * etc.
 */

#define func_enter()                                    \
    do {                                                \
        if (unifi_debug >= 5) {                         \
            printk("unifi: => %s\n", __FUNCTION__);     \
        }                                               \
    } while (0)
#define func_exit()                                     \
    do {                                                \
        if (unifi_debug >= 5) {                         \
            printk("unifi: <= %s\n", __FUNCTION__);     \
        }                                               \
    } while (0)
#define func_exit_r(_rc)                                        \
    do {                                                        \
        if (unifi_debug >= 5) {                                 \
            printk("unifi: <= %s %d\n", __FUNCTION__, (int)(_rc));   \
        }                                                       \
    } while (0)


#define ASSERT(cond)                                            \
    do {                                                            \
        if (!(cond)) {                                              \
            printk("Assertion failed in %s at %s:%d: %s\n",         \
                    __FUNCTION__, __FILE__, __LINE__, #cond);        \
        }                                                           \
    } while (0)


void unifi_dump(void *ospriv, int lvl, const char *msg, void *mem, u16 len);
void dump(void *mem, u16 len);
void dump16(void *mem, u16 len);
#ifdef CSR_WIFI_HIP_DEBUG_OFFLINE
void dump_str(void *mem, u16 len);
#endif /* CSR_WIFI_HIP_DEBUG_OFFLINE */

void unifi_error(void* ospriv, const char *fmt, ...);
void unifi_warning(void* ospriv, const char *fmt, ...);
void unifi_notice(void* ospriv, const char *fmt, ...);
void unifi_info(void* ospriv, const char *fmt, ...);

void unifi_trace(void* ospriv, int level, const char *fmt, ...);

#else

/* Stubs */
#define func_enter()
#define func_exit()
#define func_exit_r(_rc)

#define ASSERT(cond)

static inline void unifi_dump(void *ospriv, int lvl, const char *msg, void *mem, u16 len) {}
static inline void dump(void *mem, u16 len) {}
static inline void dump16(void *mem, u16 len) {}
#ifdef CSR_WIFI_HIP_DEBUG_OFFLINE
static inline void dump_str(void *mem, u16 len) {}
#endif /* CSR_WIFI_HIP_DEBUG_OFFLINE */

void unifi_error_nop(void* ospriv, const char *fmt, ...);
void unifi_trace_nop(void* ospriv, int level, const char *fmt, ...);
#define unifi_error if(1);else unifi_error_nop
#define unifi_warning if(1);else unifi_error_nop
#define unifi_notice if(1);else unifi_error_nop
#define unifi_info if(1);else unifi_error_nop
#define unifi_trace if(1);else unifi_trace_nop

#endif /* UNIFI_DEBUG */


/* Different levels of diagnostic detail... */
#define UDBG0       0   /* always prints in debug build */
#define UDBG1       1
#define UDBG2       2
#define UDBG3       3
#define UDBG4       4
#define UDBG5       5
#define UDBG6       6
#define UDBG7       7


#endif /* __UNIFI_OS_LINUX_H__ */
