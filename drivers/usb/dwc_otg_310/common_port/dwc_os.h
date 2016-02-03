/* =========================================================================
 * $File: //dwh/usb_iip/dev/software/dwc_common_port_2/dwc_os.h $
 * $Revision: #14 $
 * $Date: 2010/11/04 $
 * $Change: 1621695 $
 *
 * Synopsys Portability Library Software and documentation
 * (hereinafter, "Software") is an Unsupported proprietary work of
 * Synopsys, Inc. unless otherwise expressly agreed to in writing
 * between Synopsys and you.
 *
 * The Software IS NOT an item of Licensed Software or Licensed Product
 * under any End User Software License Agreement or Agreement for
 * Licensed Product with Synopsys or any supplement thereto. You are
 * permitted to use and redistribute this Software in source and binary
 * forms, with or without modification, provided that redistributions
 * of source code must retain this notice. You may not view, use,
 * disclose, copy or distribute this file or any information contained
 * herein except pursuant to this license grant from Synopsys. If you
 * do not agree with this notice, including the disclaimer below, then
 * you are not authorized to use the Software.
 *
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 * BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL
 * SYNOPSYS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================= */
#ifndef _DWC_OS_H_
#define _DWC_OS_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @file
 *
 * DWC portability library, low level os-wrapper functions
 *
 */

/* These basic types need to be defined by some OS header file or custom header
 * file for your specific target architecture.
 *
 * uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t
 *
 * Any custom or alternate header file must be added and enabled here.
 */

#ifdef DWC_LINUX
# include <linux/types.h>
# ifdef CONFIG_DEBUG_MUTEXES
#  include <linux/mutex.h>
# endif
# include <linux/errno.h>
# include <stdarg.h>
#endif

#if defined(DWC_FREEBSD) || defined(DWC_NETBSD)
# include <os_dep.h>
#endif

#define DWC_WMB()	wmb()
#define DWC_RMB()	rmb()
/** @name Primitive Types and Values */

/** We define a boolean type for consistency.  Can be either YES or NO */
typedef uint8_t dwc_bool_t;
#define YES  1
#define NO   0

#ifdef DWC_LINUX

/** @name Error Codes */
#define DWC_E_INVALID		EINVAL
#define DWC_E_NO_MEMORY		ENOMEM
#define DWC_E_NO_DEVICE		ENODEV
#define DWC_E_NOT_SUPPORTED	EOPNOTSUPP
#define DWC_E_TIMEOUT		ETIMEDOUT
#define DWC_E_BUSY		EBUSY
#define DWC_E_AGAIN		EAGAIN
#define DWC_E_RESTART		ERESTART
#define DWC_E_ABORT		ECONNABORTED
#define DWC_E_SHUTDOWN		ESHUTDOWN
#define DWC_E_NO_DATA		ENODATA
#define DWC_E_DISCONNECT	ECONNRESET
#define DWC_E_UNKNOWN		EINVAL
#define DWC_E_NO_STREAM_RES	ENOSR
#define DWC_E_COMMUNICATION	ECOMM
#define DWC_E_OVERFLOW		EOVERFLOW
#define DWC_E_PROTOCOL		EPROTO
#define DWC_E_IN_PROGRESS	EINPROGRESS
#define DWC_E_PIPE		EPIPE
#define DWC_E_IO		EIO
#define DWC_E_NO_SPACE		ENOSPC

#else

/** @name Error Codes */
#define DWC_E_INVALID		1001
#define DWC_E_NO_MEMORY		1002
#define DWC_E_NO_DEVICE		1003
#define DWC_E_NOT_SUPPORTED	1004
#define DWC_E_TIMEOUT		1005
#define DWC_E_BUSY		1006
#define DWC_E_AGAIN		1007
#define DWC_E_RESTART		1008
#define DWC_E_ABORT		1009
#define DWC_E_SHUTDOWN		1010
#define DWC_E_NO_DATA		1011
#define DWC_E_DISCONNECT	2000
#define DWC_E_UNKNOWN		3000
#define DWC_E_NO_STREAM_RES	4001
#define DWC_E_COMMUNICATION	4002
#define DWC_E_OVERFLOW		4003
#define DWC_E_PROTOCOL		4004
#define DWC_E_IN_PROGRESS	4005
#define DWC_E_PIPE		4006
#define DWC_E_IO		4007
#define DWC_E_NO_SPACE		4008

#endif


/** @name Tracing/Logging Functions
 *
 * These function provide the capability to add tracing, debugging, and error
 * messages, as well exceptions as assertions.  The WUDEV uses these
 * extensively.  These could be logged to the main console, the serial port, an
 * internal buffer, etc.  These functions could also be no-op if they are too
 * expensive on your system.  By default undefining the DEBUG macro already
 * no-ops some of these functions. */

/** Returns non-zero if in interrupt context. */
extern dwc_bool_t DWC_IN_IRQ(void);
#define dwc_in_irq DWC_IN_IRQ

/** Returns "IRQ" if DWC_IN_IRQ is true. */
static inline char *dwc_irq(void)
{
	return DWC_IN_IRQ() ? "IRQ" : "";
}

/** Returns non-zero if in bottom-half context. */
extern dwc_bool_t DWC_IN_BH(void);
#define dwc_in_bh DWC_IN_BH

/** Returns "BH" if DWC_IN_BH is true. */
static inline char *dwc_bh(void)
{
	return DWC_IN_BH() ? "BH" : "";
}

/**
 * A vprintf() clone.  Just call vprintf if you've got it.
 */
extern void DWC_VPRINTF(char *format, va_list args);
#define dwc_vprintf DWC_VPRINTF

/**
 * A vsnprintf() clone.  Just call vprintf if you've got it.
 */
extern int DWC_VSNPRINTF(char *str, int size, char *format, va_list args);
#define dwc_vsnprintf DWC_VSNPRINTF

/**
 * printf() clone.  Just call printf if you've go it.
 */
extern void DWC_PRINTF(char *format, ...)
/* This provides compiler level static checking of the parameters if you're
 * using GCC. */
#ifdef __GNUC__
	__attribute__ ((format(printf, 1, 2)));
#else
	;
#endif
#define dwc_printf DWC_PRINTF

/**
 * sprintf() clone.  Just call sprintf if you've got it.
 */
extern int DWC_SPRINTF(char *string, char *format, ...)
#ifdef __GNUC__
	__attribute__ ((format(printf, 2, 3)));
#else
	;
#endif
#define dwc_sprintf DWC_SPRINTF

/**
 * snprintf() clone.  Just call snprintf if you've got it.
 */
extern int DWC_SNPRINTF(char *string, int size, char *format, ...)
#ifdef __GNUC__
	__attribute__ ((format(printf, 3, 4)));
#else
	;
#endif
#define dwc_snprintf DWC_SNPRINTF

/**
 * Prints a WARNING message.  On systems that don't differentiate between
 * warnings and regular log messages, just print it.  Indicates that something
 * may be wrong with the driver.  Works like printf().
 *
 * Use the DWC_WARN macro to call this function.
 */
extern void __DWC_WARN(char *format, ...)
#ifdef __GNUC__
	__attribute__ ((format(printf, 1, 2)));
#else
	;
#endif

/**
 * Prints an error message.  On systems that don't differentiate between errors
 * and regular log messages, just print it.  Indicates that something went wrong
 * with the driver.  Works like printf().
 *
 * Use the DWC_ERROR macro to call this function.
 */
extern void __DWC_ERROR(char *format, ...)
#ifdef __GNUC__
	__attribute__ ((format(printf, 1, 2)));
#else
	;
#endif

/**
 * Prints an exception error message and takes some user-defined action such as
 * print out a backtrace or trigger a breakpoint.  Indicates that something went
 * abnormally wrong with the driver such as programmer error, or other
 * exceptional condition.  It should not be ignored so even on systems without
 * printing capability, some action should be taken to notify the developer of
 * it.  Works like printf().
 */
extern void DWC_EXCEPTION(char *format, ...)
#ifdef __GNUC__
	__attribute__ ((format(printf, 1, 2)));
#else
	;
#endif
#define dwc_exception DWC_EXCEPTION

#if 1
/**
 * Prints out a debug message.  Used for logging/trace messages.
 *
 * Use the DWC_DEBUG macro to call this function
 */
extern void __DWC_DEBUG(char *format, ...)
#ifdef __GNUC__
	__attribute__ ((format(printf, 1, 2)));
#else
	;
#endif
#else
#define __DWC_DEBUG(...)
#endif

/**
 * Prints out a Debug message.
 */
#define DWC_DEBUG(_format, _args...) __DWC_DEBUG("DEBUG:%s:%s: " _format "\n", \
						 __func__, dwc_irq(), ## _args)
#define dwc_debug DWC_DEBUG
/**
 * Prints out an informative message.
 */
#define DWC_INFO(_format, _args...) DWC_PRINTF("INFO:%s: " _format "\n", \
					       dwc_irq(), ## _args)
#define dwc_info DWC_INFO
/**
 * Prints out a warning message.
 */
#define DWC_WARN(_format, _args...) __DWC_WARN("WARN:%s:%s:%d: " _format "\n", \
					dwc_irq(), __func__, __LINE__, ## _args)
#define dwc_warn DWC_WARN
/**
 * Prints out an error message.
 */
#define DWC_ERROR(_format, _args...) __DWC_ERROR("ERROR:%s:%s:%d: " _format "\n", \
					dwc_irq(), __func__, __LINE__, ## _args)
#define dwc_error DWC_ERROR

#define DWC_PROTO_ERROR(_format, _args...) __DWC_WARN("ERROR:%s:%s:%d: " _format "\n", \
						dwc_irq(), __func__, __LINE__, ## _args)
#define dwc_proto_error DWC_PROTO_ERROR

#ifdef DEBUG
/** Prints out a exception error message if the _expr expression fails.  Disabled
 * if DEBUG is not enabled. */
#define DWC_ASSERT(_expr, _format, _args...) do { \
	if (!(_expr)) { \
		DWC_EXCEPTION("%s:%s:%d: " _format "\n", dwc_irq(), \
				__FILE__, __LINE__, ## _args); } \
	} while (0)
#else
#define DWC_ASSERT(_x...)
#endif
#define dwc_assert DWC_ASSERT


/** @name Byte Ordering
 * The following functions are for conversions between processor's byte ordering
 * and specific ordering you want.
 */

/** Converts 32 bit data in CPU byte ordering to little endian. */
extern uint32_t DWC_CPU_TO_LE32(uint32_t *p);
#define dwc_cpu_to_le32 DWC_CPU_TO_LE32

/** Converts 32 bit data in CPU byte orderint to big endian. */
extern uint32_t DWC_CPU_TO_BE32(uint32_t *p);
#define dwc_cpu_to_be32 DWC_CPU_TO_BE32

/** Converts 32 bit little endian data to CPU byte ordering. */
extern uint32_t DWC_LE32_TO_CPU(uint32_t *p);
#define dwc_le32_to_cpu DWC_LE32_TO_CPU

/** Converts 32 bit big endian data to CPU byte ordering. */
extern uint32_t DWC_BE32_TO_CPU(uint32_t *p);
#define dwc_be32_to_cpu DWC_BE32_TO_CPU

/** Converts 16 bit data in CPU byte ordering to little endian. */
extern uint16_t DWC_CPU_TO_LE16(uint16_t *p);
#define dwc_cpu_to_le16 DWC_CPU_TO_LE16

/** Converts 16 bit data in CPU byte orderint to big endian. */
extern uint16_t DWC_CPU_TO_BE16(uint16_t *p);
#define dwc_cpu_to_be16 DWC_CPU_TO_BE16

/** Converts 16 bit little endian data to CPU byte ordering. */
extern uint16_t DWC_LE16_TO_CPU(uint16_t *p);
#define dwc_le16_to_cpu DWC_LE16_TO_CPU

/** Converts 16 bit bi endian data to CPU byte ordering. */
extern uint16_t DWC_BE16_TO_CPU(uint16_t *p);
#define dwc_be16_to_cpu DWC_BE16_TO_CPU


/** @name Register Read/Write
 *
 * The following six functions should be implemented to read/write registers of
 * 32-bit and 64-bit sizes.  All modules use this to read/write register values.
 * The reg value is a pointer to the register calculated from the void *base
 * variable passed into the driver when it is started.  */

#ifdef DWC_LINUX
/* Linux doesn't need any extra parameters for register read/write, so we
 * just throw away the IO context parameter.
 */
/** Reads the content of a 32-bit register. */
extern uint32_t DWC_READ_REG32(volatile uint32_t *reg);
#define dwc_read_reg32(_ctx_, _reg_) DWC_READ_REG32(_reg_)

/** Reads the content of a 64-bit register. */
extern uint64_t DWC_READ_REG64(volatile uint64_t *reg);
#define dwc_read_reg64(_ctx_, _reg_) DWC_READ_REG64(_reg_)

/** Writes to a 32-bit register. */
extern void DWC_WRITE_REG32(volatile uint32_t *reg, uint32_t value);
#define dwc_write_reg32(_ctx_, _reg_, _val_) DWC_WRITE_REG32(_reg_, _val_)

/** Writes to a 64-bit register. */
extern void DWC_WRITE_REG64(volatile uint64_t  *reg, uint64_t value);
#define dwc_write_reg64(_ctx_, _reg_, _val_) DWC_WRITE_REG64(_reg_, _val_)

/**
 * Modify bit values in a register.  Using the
 * algorithm: (reg_contents & ~clear_mask) | set_mask.
 */
extern void DWC_MODIFY_REG32(volatile uint32_t *reg, uint32_t clear_mask, uint32_t set_mask);
#define dwc_modify_reg32(_ctx_, _reg_, _cmsk_, _smsk_) DWC_MODIFY_REG32(_reg_, _cmsk_, _smsk_)
extern void DWC_MODIFY_REG64(volatile uint64_t  *reg, uint64_t clear_mask, uint64_t set_mask);
#define dwc_modify_reg64(_ctx_, _reg_, _cmsk_, _smsk_) DWC_MODIFY_REG64(_reg_, _cmsk_, _smsk_)

#endif	/* DWC_LINUX */

#if defined(DWC_FREEBSD) || defined(DWC_NETBSD)
typedef struct dwc_ioctx {
	struct device *dev;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
} dwc_ioctx_t;

/** BSD needs two extra parameters for register read/write, so we pass
 * them in using the IO context parameter.
 */
/** Reads the content of a 32-bit register. */
extern uint32_t DWC_READ_REG32(void *io_ctx, volatile uint32_t *reg);
#define dwc_read_reg32 DWC_READ_REG32

/** Reads the content of a 64-bit register. */
extern uint64_t DWC_READ_REG64(void *io_ctx, volatile uint64_t *reg);
#define dwc_read_reg64 DWC_READ_REG64

/** Writes to a 32-bit register. */
extern void DWC_WRITE_REG32(void *io_ctx, volatile uint32_t *reg, uint32_t value);
#define dwc_write_reg32 DWC_WRITE_REG32

/** Writes to a 64-bit register. */
extern void DWC_WRITE_REG64(void *io_ctx, volatile uint64_t *reg, uint64_t value);
#define dwc_write_reg64 DWC_WRITE_REG64

/**
 * Modify bit values in a register.  Using the
 * algorithm: (reg_contents & ~clear_mask) | set_mask.
 */
extern void DWC_MODIFY_REG32(void *io_ctx, volatile uint32_t *reg, uint32_t clear_mask, uint32_t set_mask);
#define dwc_modify_reg32 DWC_MODIFY_REG32
extern void DWC_MODIFY_REG64(void *io_ctx, volatile uint64_t *reg, uint64_t clear_mask, uint64_t set_mask);
#define dwc_modify_reg64 DWC_MODIFY_REG64

#endif	/* DWC_FREEBSD || DWC_NETBSD */

/** @cond */

/** @name Some convenience MACROS used internally.  Define DWC_DEBUG_REGS to log the
 * register writes. */

#ifdef DWC_LINUX

# ifdef DWC_DEBUG_REGS

#define dwc_define_read_write_reg_n(_reg, _container_type) \
static inline uint32_t dwc_read_##_reg##_n(_container_type * container, int num) \
{ \
	return DWC_READ_REG32(&container->regs->_reg[num]); \
} \
static inline void dwc_write_##_reg##_n(_container_type *container, int num, uint32_t data) \
{ \
	DWC_DEBUG("WRITING %8s[%d]: %p: %08x", #_reg, num, \
		  &(((uint32_t *)container->regs->_reg)[num]), data); \
	DWC_WRITE_REG32(&(((uint32_t *)container->regs->_reg)[num]), data); \
}

#define dwc_define_read_write_reg(_reg, _container_type) \
static inline uint32_t dwc_read_##_reg(_container_type *container) \
{ \
	return DWC_READ_REG32(&container->regs->_reg); \
} \
static inline void dwc_write_##_reg(_container_type *container, uint32_t data) \
{ \
	DWC_DEBUG("WRITING %11s: %p: %08x", #_reg, &container->regs->_reg, data); \
	DWC_WRITE_REG32(&container->regs->_reg, data); \
}

# else	/* DWC_DEBUG_REGS */

#define dwc_define_read_write_reg_n(_reg, _container_type) \
static inline uint32_t dwc_read_##_reg##_n(_container_type *container, int num) \
{ \
	return DWC_READ_REG32(&container->regs->_reg[num]); \
} \
static inline void dwc_write_##_reg##_n(_container_type *container, int num, uint32_t data) \
{ \
	DWC_WRITE_REG32(&(((uint32_t *)container->regs->_reg)[num]), data); \
}

#define dwc_define_read_write_reg(_reg, _container_type) \
static inline uint32_t dwc_read_##_reg(_container_type *container) \
{ \
	return DWC_READ_REG32(&container->regs->_reg); \
} \
static inline void dwc_write_##_reg(_container_type *container, uint32_t data) \
{ \
	DWC_WRITE_REG32(&container->regs->_reg, data); \
}

# endif	/* DWC_DEBUG_REGS */

#endif	/* DWC_LINUX */

#if defined(DWC_FREEBSD) || defined(DWC_NETBSD)

# ifdef DWC_DEBUG_REGS

#define dwc_define_read_write_reg_n(_reg, _container_type) \
static inline uint32_t dwc_read_##_reg##_n(void *io_ctx, _container_type *container, int num) \
{ \
	return DWC_READ_REG32(io_ctx, &container->regs->_reg[num]); \
} \
static inline void dwc_write_##_reg##_n(void *io_ctx, _container_type *container, int num, uint32_t data) \
{ \
	DWC_DEBUG("WRITING %8s[%d]: %p: %08x", #_reg, num, \
		  &(((uint32_t *)container->regs->_reg)[num]), data); \
	DWC_WRITE_REG32(io_ctx, &(((uint32_t *)container->regs->_reg)[num]), data); \
}

#define dwc_define_read_write_reg(_reg, _container_type) \
static inline uint32_t dwc_read_##_reg(void *io_ctx, _container_type *container) \
{ \
	return DWC_READ_REG32(io_ctx, &container->regs->_reg); \
} \
static inline void dwc_write_##_reg(void *io_ctx, _container_type *container, uint32_t data) \
{ \
	DWC_DEBUG("WRITING %11s: %p: %08x", #_reg, &container->regs->_reg, data); \
	DWC_WRITE_REG32(io_ctx, &container->regs->_reg, data); \
}

# else	/* DWC_DEBUG_REGS */

#define dwc_define_read_write_reg_n(_reg, _container_type) \
static inline uint32_t dwc_read_##_reg##_n(void *io_ctx, _container_type *container, int num) \
{ \
	return DWC_READ_REG32(io_ctx, &container->regs->_reg[num]); \
} \
static inline void dwc_write_##_reg##_n(void *io_ctx, _container_type *container, int num, uint32_t data) \
{ \
	DWC_WRITE_REG32(io_ctx, &(((uint32_t *)container->regs->_reg)[num]), data); \
}

#define dwc_define_read_write_reg(_reg, _container_type) \
static inline uint32_t dwc_read_##_reg(void *io_ctx, _container_type *container) \
{ \
	return DWC_READ_REG32(io_ctx, &container->regs->_reg); \
} \
static inline void dwc_write_##_reg(void *io_ctx, _container_type *container, uint32_t data) \
{ \
	DWC_WRITE_REG32(io_ctx, &container->regs->_reg, data); \
}

# endif	/* DWC_DEBUG_REGS */

#endif	/* DWC_FREEBSD || DWC_NETBSD */

/** @endcond */


#ifdef DWC_CRYPTOLIB
/** @name Crypto Functions
 *
 * These are the low-level cryptographic functions used by the driver. */

/** Perform AES CBC */
extern int DWC_AES_CBC(uint8_t *message, uint32_t messagelen, uint8_t *key, uint32_t keylen, uint8_t iv[16], uint8_t *out);
#define dwc_aes_cbc DWC_AES_CBC

/** Fill the provided buffer with random bytes.  These should be cryptographic grade random numbers. */
extern void DWC_RANDOM_BYTES(uint8_t *buffer, uint32_t length);
#define dwc_random_bytes DWC_RANDOM_BYTES

/** Perform the SHA-256 hash function */
extern int DWC_SHA256(uint8_t *message, uint32_t len, uint8_t *out);
#define dwc_sha256 DWC_SHA256

/** Calculated the HMAC-SHA256 */
extern int DWC_HMAC_SHA256(uint8_t *message, uint32_t messagelen, uint8_t *key, uint32_t keylen, uint8_t *out);
#define dwc_hmac_sha256 DWC_HMAC_SHA256

#endif	/* DWC_CRYPTOLIB */


/** @name Memory Allocation
 *
 * These function provide access to memory allocation.  There are only 2 DMA
 * functions and 3 Regular memory functions that need to be implemented.  None
 * of the memory debugging routines need to be implemented.  The allocation
 * routines all ZERO the contents of the memory.
 *
 * Defining DWC_DEBUG_MEMORY turns on memory debugging and statistic gathering.
 * This checks for memory leaks, keeping track of alloc/free pairs.  It also
 * keeps track of how much memory the driver is using at any given time. */

#define DWC_PAGE_SIZE 4096
#define DWC_PAGE_OFFSET(addr) (((uint32_t)addr) & 0xfff)
#define DWC_PAGE_ALIGNED(addr) ((((uint32_t)addr) & 0xfff) == 0)

#define DWC_INVALID_DMA_ADDR 0x0

#ifdef DWC_LINUX
/** Type for a DMA address */
typedef dma_addr_t dwc_dma_t;
#endif

#if defined(DWC_FREEBSD) || defined(DWC_NETBSD)
typedef bus_addr_t dwc_dma_t;
#endif

#ifdef DWC_FREEBSD
typedef struct dwc_dmactx {
	struct device *dev;
	bus_dma_tag_t dma_tag;
	bus_dmamap_t dma_map;
	bus_addr_t dma_paddr;
	void *dma_vaddr;
} dwc_dmactx_t;
#endif

#ifdef DWC_NETBSD
typedef struct dwc_dmactx {
	struct device *dev;
	bus_dma_tag_t dma_tag;
	bus_dmamap_t dma_map;
	bus_dma_segment_t segs[1];
	int nsegs;
	bus_addr_t dma_paddr;
	void *dma_vaddr;
} dwc_dmactx_t;
#endif

/* @todo these functions will be added in the future */
#if 0
/**
 * Creates a DMA pool from which you can allocate DMA buffers.  Buffers
 * allocated from this pool will be guaranteed to meet the size, alignment, and
 * boundary requirements specified.
 *
 * @param[in] size Specifies the size of the buffers that will be allocated from
 * this pool.
 * @param[in] align Specifies the byte alignment requirements of the buffers
 * allocated from this pool.  Must be a power of 2.
 * @param[in] boundary Specifies the N-byte boundary that buffers allocated from
 * this pool must not cross.
 *
 * @returns A pointer to an internal opaque structure which is not to be
 * accessed outside of these library functions.  Use this handle to specify
 * which pools to allocate/free DMA buffers from and also to destroy the pool,
 * when you are done with it.
 */
extern dwc_pool_t *DWC_DMA_POOL_CREATE(uint32_t size, uint32_t align, uint32_t boundary);

/**
 * Destroy a DMA pool.  All buffers allocated from that pool must be freed first.
 */
extern void DWC_DMA_POOL_DESTROY(dwc_pool_t *pool);

/**
 * Allocate a buffer from the specified DMA pool and zeros its contents.
 */
extern void *DWC_DMA_POOL_ALLOC(dwc_pool_t *pool, uint64_t *dma_addr);

/**
 * Free a previously allocated buffer from the DMA pool.
 */
extern void DWC_DMA_POOL_FREE(dwc_pool_t *pool, void *vaddr, void *daddr);
#endif

/** Allocates a DMA capable buffer and zeroes its contents. */
extern void *__DWC_DMA_ALLOC(void *dma_ctx, uint32_t size, dwc_dma_t *dma_addr);

/** Allocates a DMA capable buffer and zeroes its contents in atomic contest */
extern void *__DWC_DMA_ALLOC_ATOMIC(void *dma_ctx, uint32_t size, dwc_dma_t *dma_addr);

/** Frees a previously allocated buffer. */
extern void __DWC_DMA_FREE(void *dma_ctx, uint32_t size, void *virt_addr, dwc_dma_t dma_addr);

/** Allocates a block of memory and zeroes its contents. */
extern void *__DWC_ALLOC(void *mem_ctx, uint32_t size);

/** Allocates a block of memory and zeroes its contents, in an atomic manner
 * which can be used inside interrupt context.  The size should be sufficiently
 * small, a few KB at most, such that failures are not likely to occur.  Can just call
 * __DWC_ALLOC if it is atomic. */
extern void *__DWC_ALLOC_ATOMIC(void *mem_ctx, uint32_t size);

/** Frees a previously allocated buffer. */
extern void __DWC_FREE(void *mem_ctx, void *addr);

#ifndef DWC_DEBUG_MEMORY

#define DWC_ALLOC(_size_) __DWC_ALLOC(NULL, _size_)
#define DWC_ALLOC_ATOMIC(_size_) __DWC_ALLOC_ATOMIC(NULL, _size_)
#define DWC_FREE(_addr_) __DWC_FREE(NULL, _addr_)

# ifdef DWC_LINUX
extern void *dwc_otg_dev;
#define DWC_DMA_ALLOC(_size_, _dma_) __DWC_DMA_ALLOC(NULL, _size_, _dma_)
#define DWC_DMA_ALLOC_ATOMIC(_size_, _dma_) __DWC_DMA_ALLOC_ATOMIC(NULL, _size_, _dma_)
#define DWC_DMA_FREE(_size_, _virt_, _dma_) __DWC_DMA_FREE(NULL, _size_, _virt_, _dma_)
#define DWC_DEV_DMA_ALLOC(_size_, _dma_) \
	__DWC_DMA_ALLOC(dwc_otg_dev, _size_, _dma_)
#define DWC_DEV_DMA_ALLOC_ATOMIC(_size_, _dma_) \
	__DWC_DMA_ALLOC_ATOMIC(dwc_otg_dev, _size_, _dma_)
#define DWC_DEV_DMA_FREE(_size_, _virt_, _dma_) \
	__DWC_DMA_FREE(dwc_otg_dev, _size_, _virt_, _dma_)
# endif

# if defined(DWC_FREEBSD) || defined(DWC_NETBSD)
#define DWC_DMA_ALLOC __DWC_DMA_ALLOC
#define DWC_DMA_FREE __DWC_DMA_FREE
# endif

#else	/* DWC_DEBUG_MEMORY */

extern void *dwc_alloc_debug(void *mem_ctx, uint32_t size, char const *func, int line);
extern void *dwc_alloc_atomic_debug(void *mem_ctx, uint32_t size, char const *func, int line);
extern void dwc_free_debug(void *mem_ctx, void *addr, char const *func, int line);
extern void *dwc_dma_alloc_debug(void *dma_ctx, uint32_t size, dwc_dma_t *dma_addr,
				 char const *func, int line);
extern void *dwc_dma_alloc_atomic_debug(void *dma_ctx, uint32_t size, dwc_dma_t *dma_addr,
				char const *func, int line);
extern void dwc_dma_free_debug(void *dma_ctx, uint32_t size, void *virt_addr,
			       dwc_dma_t dma_addr, char const *func, int line);

extern int dwc_memory_debug_start(void *mem_ctx);
extern void dwc_memory_debug_stop(void);
extern void dwc_memory_debug_report(void);

#define DWC_ALLOC(_size_) dwc_alloc_debug(NULL, _size_, __func__, __LINE__)
#define DWC_ALLOC_ATOMIC(_size_) dwc_alloc_atomic_debug(NULL, _size_, \
							__func__, __LINE__)
#define DWC_FREE(_addr_) dwc_free_debug(NULL, _addr_, __func__, __LINE__)

# ifdef DWC_LINUX
#define DWC_DMA_ALLOC(_size_, _dma_) dwc_dma_alloc_debug(NULL, _size_, \
						_dma_, __func__, __LINE__)
#define DWC_DMA_ALLOC_ATOMIC(_size_, _dma_) dwc_dma_alloc_atomic_debug(NULL, _size_, \
						_dma_, __func__, __LINE__)
#define DWC_DMA_FREE(_size_, _virt_, _dma_) dwc_dma_free_debug(NULL, _size_, \
						_virt_, _dma_, __func__, __LINE__)
# endif

# if defined(DWC_FREEBSD) || defined(DWC_NETBSD)
#define DWC_DMA_ALLOC(_ctx_, _size_, _dma_) dwc_dma_alloc_debug(_ctx_, _size_, \
						_dma_, __func__, __LINE__)
#define DWC_DMA_FREE(_ctx_, _size_, _virt_, _dma_) dwc_dma_free_debug(_ctx_, _size_, \
						 _virt_, _dma_, __func__, __LINE__)
# endif

#endif /* DWC_DEBUG_MEMORY */

#define dwc_alloc(_ctx_, _size_) DWC_ALLOC(_size_)
#define dwc_alloc_atomic(_ctx_, _size_) DWC_ALLOC_ATOMIC(_size_)
#define dwc_free(_ctx_, _addr_) DWC_FREE(_addr_)

#ifdef DWC_LINUX
/* Linux doesn't need any extra parameters for DMA buffer allocation, so we
 * just throw away the DMA context parameter.
 */
#define dwc_dma_alloc(_ctx_, _size_, _dma_) DWC_DMA_ALLOC(_size_, _dma_)
#define dwc_dma_alloc_atomic(_ctx_, _size_, _dma_) DWC_DMA_ALLOC_ATOMIC(_size_, _dma_)
#define dwc_dma_free(_ctx_, _size_, _virt_, _dma_) DWC_DMA_FREE(_size_, _virt_, _dma_)
#endif

#if defined(DWC_FREEBSD) || defined(DWC_NETBSD)
/** BSD needs several extra parameters for DMA buffer allocation, so we pass
 * them in using the DMA context parameter.
 */
#define dwc_dma_alloc DWC_DMA_ALLOC
#define dwc_dma_free DWC_DMA_FREE
#endif


/** @name Memory and String Processing */

/** memset() clone */
extern void *DWC_MEMSET(void *dest, uint8_t byte, uint32_t size);
#define dwc_memset DWC_MEMSET

/** memcpy() clone */
extern void *DWC_MEMCPY(void *dest, void const *src, uint32_t size);
#define dwc_memcpy DWC_MEMCPY

/** memmove() clone */
extern void *DWC_MEMMOVE(void *dest, void *src, uint32_t size);
#define dwc_memmove DWC_MEMMOVE

/** memcmp() clone */
extern int DWC_MEMCMP(void *m1, void *m2, uint32_t size);
#define dwc_memcmp DWC_MEMCMP

/** strcmp() clone */
extern int DWC_STRCMP(void *s1, void *s2);
#define dwc_strcmp DWC_STRCMP

/** strncmp() clone */
extern int DWC_STRNCMP(void *s1, void *s2, uint32_t size);
#define dwc_strncmp DWC_STRNCMP

/** strlen() clone, for NULL terminated ASCII strings */
extern int DWC_STRLEN(char const *str);
#define dwc_strlen DWC_STRLEN

/** strcpy() clone, for NULL terminated ASCII strings */
extern char *DWC_STRCPY(char *to, const char *from);
#define dwc_strcpy DWC_STRCPY

/** strdup() clone.  If you wish to use memory allocation debugging, this
 * implementation of strdup should use the DWC_* memory routines instead of
 * calling a predefined strdup.  Otherwise the memory allocated by this routine
 * will not be seen by the debugging routines. */
extern char *DWC_STRDUP(char const *str);
#define dwc_strdup(_ctx_, _str_) DWC_STRDUP(_str_)

/** NOT an atoi() clone.  Read the description carefully.  Returns an integer
 * converted from the string str in base 10 unless the string begins with a "0x"
 * in which case it is base 16.  String must be a NULL terminated sequence of
 * ASCII characters and may optionally begin with whitespace, a + or -, and a
 * "0x" prefix if base 16.  The remaining characters must be valid digits for
 * the number and end with a NULL character.  If any invalid characters are
 * encountered or it returns with a negative error code and the results of the
 * conversion are undefined.  On sucess it returns 0.  Overflow conditions are
 * undefined.  An example implementation using atoi() can be referenced from the
 * Linux implementation. */
extern int DWC_ATOI(const char *str, int32_t *value);
#define dwc_atoi DWC_ATOI

/** Same as above but for unsigned. */
extern int DWC_ATOUI(const char *str, uint32_t *value);
#define dwc_atoui DWC_ATOUI

#ifdef DWC_UTFLIB
/** This routine returns a UTF16LE unicode encoded string from a UTF8 string. */
extern int DWC_UTF8_TO_UTF16LE(uint8_t const *utf8string, uint16_t *utf16string, unsigned len);
#define dwc_utf8_to_utf16le DWC_UTF8_TO_UTF16LE
#endif


/** @name Wait queues
 *
 * Wait queues provide a means of synchronizing between threads or processes.  A
 * process can block on a waitq if some condition is not true, waiting for it to
 * become true.  When the waitq is triggered all waiting process will get
 * unblocked and the condition will be check again.  Waitqs should be triggered
 * every time a condition can potentially change.*/
struct dwc_waitq;

/** Type for a waitq */
typedef struct dwc_waitq dwc_waitq_t;

/** The type of waitq condition callback function.  This is called every time
 * condition is evaluated. */
typedef int (*dwc_waitq_condition_t)(void *data);

/** Allocate a waitq */
extern dwc_waitq_t *DWC_WAITQ_ALLOC(void);
#define dwc_waitq_alloc(_ctx_) DWC_WAITQ_ALLOC()

/** Free a waitq */
extern void DWC_WAITQ_FREE(dwc_waitq_t *wq);
#define dwc_waitq_free DWC_WAITQ_FREE

/** Check the condition and if it is false, block on the waitq.  When unblocked, check the
 * condition again.  The function returns when the condition becomes true.  The return value
 * is 0 on condition true, DWC_WAITQ_ABORTED on abort or killed, or DWC_WAITQ_UNKNOWN on error. */
extern int32_t DWC_WAITQ_WAIT(dwc_waitq_t *wq, dwc_waitq_condition_t cond, void *data);
#define dwc_waitq_wait DWC_WAITQ_WAIT

/** Check the condition and if it is false, block on the waitq.  When unblocked,
 * check the condition again.  The function returns when the condition become
 * true or the timeout has passed.  The return value is 0 on condition true or
 * DWC_TIMED_OUT on timeout, or DWC_WAITQ_ABORTED, or DWC_WAITQ_UNKNOWN on
 * error. */
extern int32_t DWC_WAITQ_WAIT_TIMEOUT(dwc_waitq_t *wq, dwc_waitq_condition_t cond,
				      void *data, int32_t msecs);
#define dwc_waitq_wait_timeout DWC_WAITQ_WAIT_TIMEOUT

/** Trigger a waitq, unblocking all processes.  This should be called whenever a condition
 * has potentially changed. */
extern void DWC_WAITQ_TRIGGER(dwc_waitq_t *wq);
#define dwc_waitq_trigger DWC_WAITQ_TRIGGER

/** Unblock all processes waiting on the waitq with an ABORTED result. */
extern void DWC_WAITQ_ABORT(dwc_waitq_t *wq);
#define dwc_waitq_abort DWC_WAITQ_ABORT


/** @name Threads
 *
 * A thread must be explicitly stopped.  It must check DWC_THREAD_SHOULD_STOP
 * whenever it is woken up, and then return.  The DWC_THREAD_STOP function
 * returns the value from the thread.
 */

struct dwc_thread;

/** Type for a thread */
typedef struct dwc_thread dwc_thread_t;

/** The thread function */
typedef int (*dwc_thread_function_t)(void *data);

/** Create a thread and start it running the thread_function.  Returns a handle
 * to the thread */
extern dwc_thread_t *DWC_THREAD_RUN(dwc_thread_function_t func, char *name, void *data);
#define dwc_thread_run(_ctx_, _func_, _name_, _data_) DWC_THREAD_RUN(_func_, _name_, _data_)

/** Stops a thread.  Return the value returned by the thread.  Or will return
 * DWC_ABORT if the thread never started. */
extern int DWC_THREAD_STOP(dwc_thread_t *thread);
#define dwc_thread_stop DWC_THREAD_STOP

/** Signifies to the thread that it must stop. */
#ifdef DWC_LINUX
/* Linux doesn't need any parameters for kthread_should_stop() */
extern dwc_bool_t DWC_THREAD_SHOULD_STOP(void);
#define dwc_thread_should_stop(_thrd_) DWC_THREAD_SHOULD_STOP()

/* No thread_exit function in Linux */
#define dwc_thread_exit(_thrd_)
#endif

#if defined(DWC_FREEBSD) || defined(DWC_NETBSD)
/** BSD needs the thread pointer for kthread_suspend_check() */
extern dwc_bool_t DWC_THREAD_SHOULD_STOP(dwc_thread_t *thread);
#define dwc_thread_should_stop DWC_THREAD_SHOULD_STOP

/** The thread must call this to exit. */
extern void DWC_THREAD_EXIT(dwc_thread_t *thread);
#define dwc_thread_exit DWC_THREAD_EXIT
#endif


/** @name Work queues
 *
 * Workqs are used to queue a callback function to be called at some later time,
 * in another thread. */
struct dwc_workq;

/** Type for a workq */
typedef struct dwc_workq dwc_workq_t;

/** The type of the callback function to be called. */
typedef void (*dwc_work_callback_t)(void *data);

/** Allocate a workq */
extern dwc_workq_t *DWC_WORKQ_ALLOC(char *name);
#define dwc_workq_alloc(_ctx_, _name_) DWC_WORKQ_ALLOC(_name_)

/** Free a workq.  All work must be completed before being freed. */
extern void DWC_WORKQ_FREE(dwc_workq_t *workq);
#define dwc_workq_free DWC_WORKQ_FREE

/** Schedule a callback on the workq, passing in data.  The function will be
 * scheduled at some later time. */
extern void DWC_WORKQ_SCHEDULE(dwc_workq_t *workq, dwc_work_callback_t cb,
			       void *data, char *format, ...)
#ifdef __GNUC__
	__attribute__ ((format(printf, 4, 5)));
#else
	;
#endif
#define dwc_workq_schedule DWC_WORKQ_SCHEDULE

/** Schedule a callback on the workq, that will be called until at least
 * given number miliseconds have passed. */
extern void DWC_WORKQ_SCHEDULE_DELAYED(dwc_workq_t *workq, dwc_work_callback_t cb,
				       void *data, uint32_t time, char *format, ...)
#ifdef __GNUC__
	__attribute__ ((format(printf, 5, 6)));
#else
	;
#endif
#define dwc_workq_schedule_delayed DWC_WORKQ_SCHEDULE_DELAYED

/** The number of processes in the workq */
extern int DWC_WORKQ_PENDING(dwc_workq_t *workq);
#define dwc_workq_pending DWC_WORKQ_PENDING

/** Blocks until all the work in the workq is complete or timed out.  Returns <
 * 0 on timeout. */
extern int DWC_WORKQ_WAIT_WORK_DONE(dwc_workq_t *workq, int timeout);
#define dwc_workq_wait_work_done DWC_WORKQ_WAIT_WORK_DONE


/** @name Tasklets
 *
 */
struct dwc_tasklet;

/** Type for a tasklet */
typedef struct dwc_tasklet dwc_tasklet_t;

/** The type of the callback function to be called */
typedef void (*dwc_tasklet_callback_t)(void *data);

/** Allocates a tasklet */
extern dwc_tasklet_t *DWC_TASK_ALLOC(char *name, dwc_tasklet_callback_t cb, void *data);
#define dwc_task_alloc(_ctx_, _name_, _cb_, _data_) DWC_TASK_ALLOC(_name_, _cb_, _data_)

/** Frees a tasklet */
extern void DWC_TASK_FREE(dwc_tasklet_t *task);
#define dwc_task_free DWC_TASK_FREE

/** Schedules a tasklet to run */
extern void DWC_TASK_SCHEDULE(dwc_tasklet_t *task);
#define dwc_task_schedule DWC_TASK_SCHEDULE


/** @name Timer
 *
 * Callbacks must be small and atomic.
 */
struct dwc_timer;

/** Type for a timer */
typedef struct dwc_timer dwc_timer_t;

/** The type of the callback function to be called */
typedef void (*dwc_timer_callback_t)(void *data);

/** Allocates a timer */
extern dwc_timer_t *DWC_TIMER_ALLOC(char *name, dwc_timer_callback_t cb, void *data);
#define dwc_timer_alloc(_ctx_, _name_, _cb_, _data_) DWC_TIMER_ALLOC(_name_, _cb_, _data_)

/** Frees a timer */
extern void DWC_TIMER_FREE(dwc_timer_t *timer);
#define dwc_timer_free DWC_TIMER_FREE

/** Schedules the timer to run at time ms from now.  And will repeat at every
 * repeat_interval msec therafter
 *
 * Modifies a timer that is still awaiting execution to a new expiration time.
 * The mod_time is added to the old time.  */
extern void DWC_TIMER_SCHEDULE(dwc_timer_t *timer, uint32_t time);
#define dwc_timer_schedule DWC_TIMER_SCHEDULE

/** Disables the timer from execution. */
extern void DWC_TIMER_CANCEL(dwc_timer_t *timer);
#define dwc_timer_cancel DWC_TIMER_CANCEL


/** @name Spinlocks
 *
 * These locks are used when the work between the lock/unlock is atomic and
 * short.  Interrupts are also disabled during the lock/unlock and thus they are
 * suitable to lock between interrupt/non-interrupt context.  They also lock
 * between processes if you have multiple CPUs or Preemption.  If you don't have
 * multiple CPUS or Preemption, then the you can simply implement the
 * DWC_SPINLOCK and DWC_SPINUNLOCK to disable and enable interrupts.  Because
 * the work between the lock/unlock is atomic, the process context will never
 * change, and so you never have to lock between processes.  */

struct dwc_spinlock;

/** Type for a spinlock */
typedef struct dwc_spinlock dwc_spinlock_t;

/** Type for the 'flags' argument to spinlock funtions */
typedef unsigned long dwc_irqflags_t;

/** Returns an initialized lock variable.  This function should allocate and
 * initialize the OS-specific data structure used for locking.  This data
 * structure is to be used for the DWC_LOCK and DWC_UNLOCK functions and should
 * be freed by the DWC_FREE_LOCK when it is no longer used. */
extern dwc_spinlock_t *DWC_SPINLOCK_ALLOC(void);
#define dwc_spinlock_alloc(_ctx_) DWC_SPINLOCK_ALLOC()

/** Frees an initialized lock variable. */
extern void DWC_SPINLOCK_FREE(dwc_spinlock_t *lock);
#define dwc_spinlock_free(_ctx_, _lock_) DWC_SPINLOCK_FREE(_lock_)

/** Disables interrupts and blocks until it acquires the lock.
 *
 * @param lock Pointer to the spinlock.
 * @param flags Unsigned long for irq flags storage.
 */
extern void DWC_SPINLOCK_IRQSAVE(dwc_spinlock_t *lock, dwc_irqflags_t *flags);
#define dwc_spinlock_irqsave DWC_SPINLOCK_IRQSAVE

/** Re-enables the interrupt and releases the lock.
 *
 * @param lock Pointer to the spinlock.
 * @param flags Unsigned long for irq flags storage.  Must be the same as was
 * passed into DWC_LOCK.
 */
extern void DWC_SPINUNLOCK_IRQRESTORE(dwc_spinlock_t *lock, dwc_irqflags_t flags);
#define dwc_spinunlock_irqrestore DWC_SPINUNLOCK_IRQRESTORE

/** Blocks until it acquires the lock.
 *
 * @param lock Pointer to the spinlock.
 */
extern void DWC_SPINLOCK(dwc_spinlock_t *lock);
#define dwc_spinlock DWC_SPINLOCK

/** Releases the lock.
 *
 * @param lock Pointer to the spinlock.
 */
extern void DWC_SPINUNLOCK(dwc_spinlock_t *lock);
#define dwc_spinunlock DWC_SPINUNLOCK


/** @name Mutexes
 *
 * Unlike spinlocks Mutexes lock only between processes and the work between the
 * lock/unlock CAN block, therefore it CANNOT be called from interrupt context.
 */

struct dwc_mutex;

/** Type for a mutex */
typedef struct dwc_mutex dwc_mutex_t;

/* For Linux Mutex Debugging make it inline because the debugging routines use
 * the symbol to determine recursive locking.  This makes it falsely think
 * recursive locking occurs. */
#if defined(DWC_LINUX) && defined(CONFIG_DEBUG_MUTEXES)
#define DWC_MUTEX_ALLOC_LINUX_DEBUG(__mutexp) ({ \
	__mutexp = (dwc_mutex_t *)DWC_ALLOC(sizeof(struct mutex)); \
	mutex_init((struct mutex *)__mutexp); \
})
#endif

/** Allocate a mutex */
extern dwc_mutex_t *DWC_MUTEX_ALLOC(void);
#define dwc_mutex_alloc(_ctx_) DWC_MUTEX_ALLOC()

/* For memory leak debugging when using Linux Mutex Debugging */
#if defined(DWC_LINUX) && defined(CONFIG_DEBUG_MUTEXES)
#define DWC_MUTEX_FREE(__mutexp) do { \
	mutex_destroy((struct mutex *)__mutexp); \
	DWC_FREE(__mutexp); \
} while (0)
#else
/** Free a mutex */
extern void DWC_MUTEX_FREE(dwc_mutex_t *mutex);
#define dwc_mutex_free(_ctx_, _mutex_) DWC_MUTEX_FREE(_mutex_)
#endif

/** Lock a mutex */
extern void DWC_MUTEX_LOCK(dwc_mutex_t *mutex);
#define dwc_mutex_lock DWC_MUTEX_LOCK

/** Non-blocking lock returns 1 on successful lock. */
extern int DWC_MUTEX_TRYLOCK(dwc_mutex_t *mutex);
#define dwc_mutex_trylock DWC_MUTEX_TRYLOCK

/** Unlock a mutex */
extern void DWC_MUTEX_UNLOCK(dwc_mutex_t *mutex);
#define dwc_mutex_unlock DWC_MUTEX_UNLOCK


/** @name Time */

/** Microsecond delay.
 *
 * @param usecs  Microseconds to delay.
 */
extern void DWC_UDELAY(uint32_t usecs);
#define dwc_udelay DWC_UDELAY

/** Millisecond delay.
 *
 * @param msecs  Milliseconds to delay.
 */
extern void DWC_MDELAY(uint32_t msecs);
#define dwc_mdelay DWC_MDELAY

/** Non-busy waiting.
 * Sleeps for specified number of milliseconds.
 *
 * @param msecs Milliseconds to sleep.
 */
extern void DWC_MSLEEP(uint32_t msecs);
#define dwc_msleep DWC_MSLEEP

/**
 * Returns number of milliseconds since boot.
 */
extern uint32_t DWC_TIME(void);
#define dwc_time DWC_TIME




/* @mainpage DWC Portability and Common Library
 *
 * This is the documentation for the DWC Portability and Common Library.
 *
 * @section intro Introduction
 *
 * The DWC Portability library consists of wrapper calls and data structures to
 * all low-level functions which are typically provided by the OS.  The WUDEV
 * driver uses only these functions.  In order to port the WUDEV driver, only
 * the functions in this library need to be re-implemented, with the same
 * behavior as documented here.
 *
 * The Common library consists of higher level functions, which rely only on
 * calling the functions from the DWC Portability library.  These common
 * routines are shared across modules.  Some of the common libraries need to be
 * used directly by the driver programmer when porting WUDEV.  Such as the
 * parameter and notification libraries.
 *
 * @section low Portability Library OS Wrapper Functions
 *
 * Any function starting with DWC and in all CAPS is a low-level OS-wrapper that
 * needs to be implemented when porting, for example DWC_MUTEX_ALLOC().  All of
 * these functions are included in the dwc_os.h file.
 *
 * There are many functions here covering a wide array of OS services.  Please
 * see dwc_os.h for details, and implementation notes for each function.
 *
 * @section common Common Library Functions
 *
 * Any function starting with dwc and in all lowercase is a common library
 * routine.  These functions have a portable implementation and do not need to
 * be reimplemented when porting.  The common routines can be used by any
 * driver, and some must be used by the end user to control the drivers.  For
 * example, you must use the Parameter common library in order to set the
 * parameters in the WUDEV module.
 *
 * The common libraries consist of the following:
 *
 * - Connection Contexts - Used internally and can be used by end-user.  See dwc_cc.h
 * - Parameters - Used internally and can be used by end-user.  See dwc_params.h
 * - Notifications - Used internally and can be used by end-user.  See dwc_notifier.h
 * - Lists - Used internally and can be used by end-user.  See dwc_list.h
 * - Memory Debugging - Used internally and can be used by end-user.  See dwc_os.h
 * - Modpow - Used internally only.  See dwc_modpow.h
 * - DH - Used internally only.  See dwc_dh.h
 * - Crypto - Used internally only.  See dwc_crypto.h
 *
 *
 * @section prereq Prerequistes For dwc_os.h
 * @subsection types Data Types
 *
 * The dwc_os.h file assumes that several low-level data types are pre defined for the
 * compilation environment.  These data types are:
 *
 * - uint8_t - unsigned 8-bit data type
 * - int8_t - signed 8-bit data type
 * - uint16_t - unsigned 16-bit data type
 * - int16_t - signed 16-bit data type
 * - uint32_t - unsigned 32-bit data type
 * - int32_t - signed 32-bit data type
 * - uint64_t - unsigned 64-bit data type
 * - int64_t - signed 64-bit data type
 *
 * Ensure that these are defined before using dwc_os.h.  The easiest way to do
 * that is to modify the top of the file to include the appropriate header.
 * This is already done for the Linux environment.  If the DWC_LINUX macro is
 * defined, the correct header will be added.  A standard header <stdint.h> is
 * also used for environments where standard C headers are available.
 *
 * @subsection stdarg Variable Arguments
 *
 * Variable arguments are provided by a standard C header <stdarg.h>.  it is
 * available in Both the Linux and ANSI C enviornment.  An equivalent must be
 * provided in your enviornment in order to use dwc_os.h with the debug and
 * tracing message functionality.
 *
 * @subsection thread Threading
 *
 * WUDEV Core must be run on an operating system that provides for multiple
 * threads/processes.  Threading can be implemented in many ways, even in
 * embedded systems without an operating system.  At the bare minimum, the
 * system should be able to start any number of processes at any time to handle
 * special work.  It need not be a pre-emptive system.  Process context can
 * change upon a call to a blocking function.  The hardware interrupt context
 * that calls the module's ISR() function must be differentiable from process
 * context, even if your processes are impemented via a hardware interrupt.
 * Further locking mechanism between process must exist (or be implemented), and
 * process context must have a way to disable interrupts for a period of time to
 * lock them out.  If all of this exists, the functions in dwc_os.h related to
 * threading should be able to be implemented with the defined behavior.
 *
 */

#ifdef __cplusplus
}
#endif

#endif /* _DWC_OS_H_ */
