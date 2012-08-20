/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */
#ifndef _HV_IORPC_H_
#define _HV_IORPC_H_

/**
 *
 * Error codes and struct definitions for the IO RPC library.
 *
 * The hypervisor's IO RPC component provides a convenient way for
 * driver authors to proxy system calls between user space, linux, and
 * the hypervisor driver.  The core of the system is a set of Python
 * files that take ".idl" files as input and generates the following
 * source code:
 *
 * - _rpc_call() routines for use in userspace IO libraries.  These
 * routines take an argument list specified in the .idl file, pack the
 * arguments in to a buffer, and read or write that buffer via the
 * Linux iorpc driver.
 *
 * - dispatch_read() and dispatch_write() routines that hypervisor
 * drivers can use to implement most of their dev_pread() and
 * dev_pwrite() methods.  These routines decode the incoming parameter
 * blob, permission check and translate parameters where appropriate,
 * and then invoke a callback routine for whichever RPC call has
 * arrived.  The driver simply implements the set of callback
 * routines.
 *
 * The IO RPC system also includes the Linux 'iorpc' driver, which
 * proxies calls between the userspace library and the hypervisor
 * driver.  The Linux driver is almost entirely device agnostic; it
 * watches for special flags indicating cases where a memory buffer
 * address might need to be translated, etc.  As a result, driver
 * writers can avoid many of the problem cases related to registering
 * hardware resources like memory pages or interrupts.  However, the
 * drivers must be careful to obey the conventions documented below in
 * order to work properly with the generic Linux iorpc driver.
 *
 * @section iorpc_domains Service Domains
 *
 * All iorpc-based drivers must support a notion of service domains.
 * A service domain is basically an application context - state
 * indicating resources that are allocated to that particular app
 * which it may access and (perhaps) other applications may not
 * access.  Drivers can support any number of service domains they
 * choose.  In some cases the design is limited by a number of service
 * domains supported by the IO hardware; in other cases the service
 * domains are a purely software concept and the driver chooses a
 * maximum number of domains based on how much state memory it is
 * willing to preallocate.
 *
 * For example, the mPIPE driver only supports as many service domains
 * as are supported by the mPIPE hardware.  This limitation is
 * required because the hardware implements its own MMIO protection
 * scheme to allow large MMIO mappings while still protecting small
 * register ranges within the page that should only be accessed by the
 * hypervisor.
 *
 * In contrast, drivers with no hardware service domain limitations
 * (for instance the TRIO shim) can implement an arbitrary number of
 * service domains.  In these cases, each service domain is limited to
 * a carefully restricted set of legal MMIO addresses if necessary to
 * keep one application from corrupting another application's state.
 *
 * @section iorpc_conventions System Call Conventions
 *
 * The driver's open routine is responsible for allocating a new
 * service domain for each hv_dev_open() call.  By convention, the
 * return value from open() should be the service domain number on
 * success, or GXIO_ERR_NO_SVC_DOM if no more service domains are
 * available.
 *
 * The implementations of hv_dev_pread() and hv_dev_pwrite() are
 * responsible for validating the devhdl value passed up by the
 * client.  Since the device handle returned by hv_dev_open() should
 * embed the positive service domain number, drivers should make sure
 * that DRV_HDL2BITS(devhdl) is a legal service domain.  If the client
 * passes an illegal service domain number, the routine should return
 * GXIO_ERR_INVAL_SVC_DOM.  Once the service domain number has been
 * validated, the driver can copy to/from the client buffer and call
 * the dispatch_read() or dispatch_write() methods created by the RPC
 * generator.
 *
 * The hv_dev_close() implementation should reset all service domain
 * state and put the service domain back on a free list for
 * reallocation by a future application.  In most cases, this will
 * require executing a hardware reset or drain flow and denying any
 * MMIO regions that were created for the service domain.
 *
 * @section iorpc_data Special Data Types
 *
 * The .idl file syntax allows the creation of syscalls with special
 * parameters that require permission checks or translations as part
 * of the system call path.  Because of limitations in the code
 * generator, APIs are generally limited to just one of these special
 * parameters per system call, and they are sometimes required to be
 * the first or last parameter to the call.  Special parameters
 * include:
 *
 * @subsection iorpc_mem_buffer MEM_BUFFER
 *
 * The MEM_BUFFER() datatype allows user space to "register" memory
 * buffers with a device.  Registering memory accomplishes two tasks:
 * Linux keeps track of all buffers that might be modified by a
 * hardware device, and the hardware device drivers bind registered
 * buffers to particular hardware resources like ingress NotifRings.
 * The MEM_BUFFER() idl syntax can take extra flags like ALIGN_64KB,
 * ALIGN_SELF_SIZE, and FLAGS indicating that memory buffers must have
 * certain alignment or that the user should be able to pass a "memory
 * flags" word specifying attributes like nt_hint or IO cache pinning.
 * The parser will accept multiple MEM_BUFFER() flags.
 *
 * Implementations must obey the following conventions when
 * registering memory buffers via the iorpc flow.  These rules are a
 * result of the Linux driver implementation, which needs to keep
 * track of how many times a particular page has been registered with
 * the hardware so that it can release the page when all those
 * registrations are cleared.
 *
 * - Memory registrations that refer to a resource which has already
 * been bound must return GXIO_ERR_ALREADY_INIT.  Thus, it is an
 * error to register memory twice without resetting (i.e. closing) the
 * resource in between.  This convention keeps the Linux driver from
 * having to track which particular devices a page is bound to.
 *
 * - At present, a memory registration is only cleared when the
 * service domain is reset.  In this case, the Linux driver simply
 * closes the HV device file handle and then decrements the reference
 * counts of all pages that were previously registered with the
 * device.
 *
 * - In the future, we may add a mechanism for unregistering memory.
 * One possible implementation would require that the user specify
 * which buffer is currently registered.  The HV would then verify
 * that that page was actually the one currently mapped and return
 * success or failure to Linux, which would then only decrement the
 * page reference count if the addresses were mapped.  Another scheme
 * might allow Linux to pass a token to the HV to be returned when the
 * resource is unmapped.
 *
 * @subsection iorpc_interrupt INTERRUPT
 *
 * The INTERRUPT .idl datatype allows the client to bind hardware
 * interrupts to a particular combination of IPI parameters - CPU, IPI
 * PL, and event bit number.  This data is passed via a special
 * datatype so that the Linux driver can validate the CPU and PL and
 * the HV generic iorpc code can translate client CPUs to real CPUs.
 *
 * @subsection iorpc_pollfd_setup POLLFD_SETUP
 *
 * The POLLFD_SETUP .idl datatype allows the client to set up hardware
 * interrupt bindings which are received by Linux but which are made
 * visible to user processes as state transitions on a file descriptor;
 * this allows user processes to use Linux primitives, such as poll(), to
 * await particular hardware events.  This data is passed via a special
 * datatype so that the Linux driver may recognize the pollable file
 * descriptor and translate it to a set of interrupt target information,
 * and so that the HV generic iorpc code can translate client CPUs to real
 * CPUs.
 *
 * @subsection iorpc_pollfd POLLFD
 *
 * The POLLFD .idl datatype allows manipulation of hardware interrupt
 * bindings set up via the POLLFD_SETUP datatype; common operations are
 * resetting the state of the requested interrupt events, and unbinding any
 * bound interrupts.  This data is passed via a special datatype so that
 * the Linux driver may recognize the pollable file descriptor and
 * translate it to an interrupt identifier previously supplied by the
 * hypervisor as the result of an earlier pollfd_setup operation.
 *
 * @subsection iorpc_blob BLOB
 *
 * The BLOB .idl datatype allows the client to write an arbitrary
 * length string of bytes up to the hypervisor driver.  This can be
 * useful for passing up large, arbitrarily structured data like
 * classifier programs.  The iorpc stack takes care of validating the
 * buffer VA and CPA as the data passes up to the hypervisor.  Unlike
 * MEM_BUFFER(), the buffer is not registered - Linux does not bump
 * page refcounts and the HV driver should not reuse the buffer once
 * the system call is complete.
 *
 * @section iorpc_translation Translating User Space Calls
 *
 * The ::iorpc_offset structure describes the formatting of the offset
 * that is passed to pread() or pwrite() as part of the generated RPC code.
 * When the user calls up to Linux, the rpc code fills in all the fields of
 * the offset, including a 16-bit opcode, a 16 bit format indicator, and 32
 * bits of user-specified "sub-offset".  The opcode indicates which syscall
 * is being requested.  The format indicates whether there is a "prefix
 * struct" at the start of the memory buffer passed to pwrite(), and if so
 * what data is in that prefix struct.  These prefix structs are used to
 * implement special datatypes like MEM_BUFFER() and INTERRUPT - we arrange
 * to put data that needs translation and permission checks at the start of
 * the buffer so that the Linux driver and generic portions of the HV iorpc
 * code can easily access the data.  The 32 bits of user-specified
 * "sub-offset" are most useful for pread() calls where the user needs to
 * also pass in a few bits indicating which register to read, etc.
 *
 * The Linux iorpc driver watches for system calls that contain prefix
 * structs so that it can translate parameters and bump reference
 * counts as appropriate.  It does not (currently) have any knowledge
 * of the per-device opcodes - it doesn't care what operation you're
 * doing to mPIPE, so long as it can do all the generic book-keeping.
 * The hv/iorpc.h header file defines all of the generic encoding bits
 * needed to translate iorpc calls without knowing which particular
 * opcode is being issued.
 *
 * @section iorpc_globals Global iorpc Calls
 *
 * Implementing mmap() required adding some special iorpc syscalls
 * that are only called by the Linux driver, never by userspace.
 * These include get_mmio_base() and check_mmio_offset().  These
 * routines are described in globals.idl and must be included in every
 * iorpc driver.  By providing these routines in every driver, Linux's
 * mmap implementation can easily get the PTE bits it needs and
 * validate the PA offset without needing to know the per-device
 * opcodes to perform those tasks.
 *
 * @section iorpc_kernel Supporting gxio APIs in the Kernel
 *
 * The iorpc code generator also supports generation of kernel code
 * implementing the gxio APIs.  This capability is currently used by
 * the mPIPE network driver, and will likely be used by the TRIO root
 * complex and endpoint drivers and perhaps an in-kernel crypto
 * driver.  Each driver that wants to instantiate iorpc calls in the
 * kernel needs to generate a kernel version of the generate rpc code
 * and (probably) copy any related gxio source files into the kernel.
 * The mPIPE driver provides a good example of this pattern.
 */

#ifdef __KERNEL__
#include <linux/stddef.h>
#else
#include <stddef.h>
#endif

#if defined(__HV__)
#include <hv/hypervisor.h>
#elif defined(__KERNEL__)
#include "hypervisor.h"
#include <linux/types.h>
#else
#include <stdint.h>
#endif


/** Code indicating translation services required within the RPC path.
 * These indicate whether there is a translatable struct at the start
 * of the RPC buffer and what information that struct contains.
 */
enum iorpc_format_e
{
  /** No translation required, no prefix struct. */
  IORPC_FORMAT_NONE,

  /** No translation required, no prefix struct, no access to this
   *  operation from user space. */
  IORPC_FORMAT_NONE_NOUSER,

  /** Prefix struct contains user VA and size. */
  IORPC_FORMAT_USER_MEM,

  /** Prefix struct contains CPA, size, and homing bits. */
  IORPC_FORMAT_KERNEL_MEM,

  /** Prefix struct contains interrupt. */
  IORPC_FORMAT_KERNEL_INTERRUPT,

  /** Prefix struct contains user-level interrupt. */
  IORPC_FORMAT_USER_INTERRUPT,

  /** Prefix struct contains pollfd_setup (interrupt information). */
  IORPC_FORMAT_KERNEL_POLLFD_SETUP,

  /** Prefix struct contains user-level pollfd_setup (file descriptor). */
  IORPC_FORMAT_USER_POLLFD_SETUP,

  /** Prefix struct contains pollfd (interrupt cookie). */
  IORPC_FORMAT_KERNEL_POLLFD,

  /** Prefix struct contains user-level pollfd (file descriptor). */
  IORPC_FORMAT_USER_POLLFD,
};


/** Generate an opcode given format and code. */
#define IORPC_OPCODE(FORMAT, CODE) (((FORMAT) << 16) | (CODE))

/** The offset passed through the read() and write() system calls
    combines an opcode with 32 bits of user-specified offset. */
union iorpc_offset
{
#ifndef __BIG_ENDIAN__
  uint64_t offset;              /**< All bits. */

  struct
  {
    uint16_t code;              /**< RPC code. */
    uint16_t format;            /**< iorpc_format_e */
    uint32_t sub_offset;        /**< caller-specified offset. */
  };

  uint32_t opcode;              /**< Opcode combines code & format. */
#else
  uint64_t offset;              /**< All bits. */

  struct
  {
    uint32_t sub_offset;        /**< caller-specified offset. */
    uint16_t format;            /**< iorpc_format_e */
    uint16_t code;              /**< RPC code. */
  };

  struct
  {
    uint32_t padding;
    uint32_t opcode;              /**< Opcode combines code & format. */
  };
#endif
};


/** Homing and cache hinting bits that can be used by IO devices. */
struct iorpc_mem_attr
{
  unsigned int lotar_x:4;       /**< lotar X bits (or Gx page_mask). */
  unsigned int lotar_y:4;       /**< lotar Y bits (or Gx page_offset). */
  unsigned int hfh:1;           /**< Uses hash-for-home. */
  unsigned int nt_hint:1;       /**< Non-temporal hint. */
  unsigned int io_pin:1;        /**< Only fill 'IO' cache ways. */
};

/** Set the nt_hint bit. */
#define IORPC_MEM_BUFFER_FLAG_NT_HINT (1 << 0)

/** Set the IO pin bit. */
#define IORPC_MEM_BUFFER_FLAG_IO_PIN (1 << 1)


/** A structure used to describe memory registration.  Different
    protection levels describe memory differently, so this union
    contains all the different possible descriptions.  As a request
    moves up the call chain, each layer translates from one
    description format to the next.  In particular, the Linux iorpc
    driver translates user VAs into CPAs and homing parameters. */
union iorpc_mem_buffer
{
  struct
  {
    uint64_t va;                /**< User virtual address. */
    uint64_t size;              /**< Buffer size. */
    unsigned int flags;         /**< nt_hint, IO pin. */
  }
  user;                         /**< Buffer as described by user apps. */

  struct
  {
    unsigned long long cpa;     /**< Client physical address. */
#if defined(__KERNEL__) || defined(__HV__)
    size_t size;                /**< Buffer size. */
    HV_PTE pte;                 /**< PTE describing memory homing. */
#else
    uint64_t size;
    uint64_t pte;
#endif
    unsigned int flags;         /**< nt_hint, IO pin. */
  }
  kernel;                       /**< Buffer as described by kernel. */

  struct
  {
    unsigned long long pa;      /**< Physical address. */
    size_t size;                /**< Buffer size. */
    struct iorpc_mem_attr attr;      /**< Homing and locality hint bits. */
  }
  hv;                           /**< Buffer parameters for HV driver. */
};


/** A structure used to describe interrupts.  The format differs slightly
 *  for user and kernel interrupts.  As with the mem_buffer_t, translation
 *  between the formats is done at each level. */
union iorpc_interrupt
{
  struct
  {
    int cpu;   /**< CPU. */
    int event; /**< evt_num */
  }
  user;        /**< Interrupt as described by user applications. */

  struct
  {
    int x;     /**< X coord. */
    int y;     /**< Y coord. */
    int ipi;   /**< int_num */
    int event; /**< evt_num */
  }
  kernel;      /**< Interrupt as described by the kernel. */

};


/** A structure used to describe interrupts used with poll().  The format
 *  differs significantly for requests from user to kernel, and kernel to
 *  hypervisor.  As with the mem_buffer_t, translation between the formats
 *  is done at each level. */
union iorpc_pollfd_setup
{
  struct
  {
    int fd;    /**< Pollable file descriptor. */
  }
  user;        /**< pollfd_setup as described by user applications. */

  struct
  {
    int x;     /**< X coord. */
    int y;     /**< Y coord. */
    int ipi;   /**< int_num */
    int event; /**< evt_num */
  }
  kernel;      /**< pollfd_setup as described by the kernel. */

};


/** A structure used to describe previously set up interrupts used with
 *  poll().  The format differs significantly for requests from user to
 *  kernel, and kernel to hypervisor.  As with the mem_buffer_t, translation
 *  between the formats is done at each level. */
union iorpc_pollfd
{
  struct
  {
    int fd;    /**< Pollable file descriptor. */
  }
  user;        /**< pollfd as described by user applications. */

  struct
  {
    int cookie; /**< hv cookie returned by the pollfd_setup operation. */
  }
  kernel;      /**< pollfd as described by the kernel. */

};


/** The various iorpc devices use error codes from -1100 to -1299.
 *
 * This range is distinct from netio (-700 to -799), the hypervisor
 * (-800 to -899), tilepci (-900 to -999), ilib (-1000 to -1099),
 * gxcr (-1300 to -1399) and gxpci (-1400 to -1499).
 */
enum gxio_err_e {

  /** Largest iorpc error number. */
  GXIO_ERR_MAX = -1101,


  /********************************************************/
  /*                   Generic Error Codes                */
  /********************************************************/

  /** Bad RPC opcode - possible version incompatibility. */
  GXIO_ERR_OPCODE = -1101,

  /** Invalid parameter. */
  GXIO_ERR_INVAL = -1102,

  /** Memory buffer did not meet alignment requirements. */
  GXIO_ERR_ALIGNMENT = -1103,

  /** Memory buffers must be coherent and cacheable. */
  GXIO_ERR_COHERENCE = -1104,

  /** Resource already initialized. */
  GXIO_ERR_ALREADY_INIT = -1105,

  /** No service domains available. */
  GXIO_ERR_NO_SVC_DOM = -1106,

  /** Illegal service domain number. */
  GXIO_ERR_INVAL_SVC_DOM = -1107,

  /** Illegal MMIO address. */
  GXIO_ERR_MMIO_ADDRESS = -1108,

  /** Illegal interrupt binding. */
  GXIO_ERR_INTERRUPT = -1109,

  /** Unreasonable client memory. */
  GXIO_ERR_CLIENT_MEMORY = -1110,

  /** No more IOTLB entries. */
  GXIO_ERR_IOTLB_ENTRY = -1111,

  /** Invalid memory size. */
  GXIO_ERR_INVAL_MEMORY_SIZE = -1112,

  /** Unsupported operation. */
  GXIO_ERR_UNSUPPORTED_OP = -1113,

  /** Insufficient DMA credits. */
  GXIO_ERR_DMA_CREDITS = -1114,

  /** Operation timed out. */
  GXIO_ERR_TIMEOUT = -1115,

  /** No such device or object. */
  GXIO_ERR_NO_DEVICE = -1116,

  /** Device or resource busy. */
  GXIO_ERR_BUSY = -1117,

  /** I/O error. */
  GXIO_ERR_IO = -1118,

  /** Permissions error. */
  GXIO_ERR_PERM = -1119,



  /********************************************************/
  /*                 Test Device Error Codes              */
  /********************************************************/

  /** Illegal register number. */
  GXIO_TEST_ERR_REG_NUMBER = -1120,

  /** Illegal buffer slot. */
  GXIO_TEST_ERR_BUFFER_SLOT = -1121,


  /********************************************************/
  /*                    MPIPE Error Codes                 */
  /********************************************************/


  /** Invalid buffer size. */
  GXIO_MPIPE_ERR_INVAL_BUFFER_SIZE = -1131,

  /** Cannot allocate buffer stack. */
  GXIO_MPIPE_ERR_NO_BUFFER_STACK = -1140,

  /** Invalid buffer stack number. */
  GXIO_MPIPE_ERR_BAD_BUFFER_STACK = -1141,

  /** Cannot allocate NotifRing. */
  GXIO_MPIPE_ERR_NO_NOTIF_RING = -1142,

  /** Invalid NotifRing number. */
  GXIO_MPIPE_ERR_BAD_NOTIF_RING = -1143,

  /** Cannot allocate NotifGroup. */
  GXIO_MPIPE_ERR_NO_NOTIF_GROUP = -1144,

  /** Invalid NotifGroup number. */
  GXIO_MPIPE_ERR_BAD_NOTIF_GROUP = -1145,

  /** Cannot allocate bucket. */
  GXIO_MPIPE_ERR_NO_BUCKET = -1146,

  /** Invalid bucket number. */
  GXIO_MPIPE_ERR_BAD_BUCKET = -1147,

  /** Cannot allocate eDMA ring. */
  GXIO_MPIPE_ERR_NO_EDMA_RING = -1148,

  /** Invalid eDMA ring number. */
  GXIO_MPIPE_ERR_BAD_EDMA_RING = -1149,

  /** Invalid channel number. */
  GXIO_MPIPE_ERR_BAD_CHANNEL = -1150,

  /** Bad configuration. */
  GXIO_MPIPE_ERR_BAD_CONFIG = -1151,

  /** Empty iqueue. */
  GXIO_MPIPE_ERR_IQUEUE_EMPTY = -1152,

  /** Empty rules. */
  GXIO_MPIPE_ERR_RULES_EMPTY = -1160,

  /** Full rules. */
  GXIO_MPIPE_ERR_RULES_FULL = -1161,

  /** Corrupt rules. */
  GXIO_MPIPE_ERR_RULES_CORRUPT = -1162,

  /** Invalid rules. */
  GXIO_MPIPE_ERR_RULES_INVALID = -1163,

  /** Classifier is too big. */
  GXIO_MPIPE_ERR_CLASSIFIER_TOO_BIG = -1170,

  /** Classifier is too complex. */
  GXIO_MPIPE_ERR_CLASSIFIER_TOO_COMPLEX = -1171,

  /** Classifier has bad header. */
  GXIO_MPIPE_ERR_CLASSIFIER_BAD_HEADER = -1172,

  /** Classifier has bad contents. */
  GXIO_MPIPE_ERR_CLASSIFIER_BAD_CONTENTS = -1173,

  /** Classifier encountered invalid symbol. */
  GXIO_MPIPE_ERR_CLASSIFIER_INVAL_SYMBOL = -1174,

  /** Classifier encountered invalid bounds. */
  GXIO_MPIPE_ERR_CLASSIFIER_INVAL_BOUNDS = -1175,

  /** Classifier encountered invalid relocation. */
  GXIO_MPIPE_ERR_CLASSIFIER_INVAL_RELOCATION = -1176,

  /** Classifier encountered undefined symbol. */
  GXIO_MPIPE_ERR_CLASSIFIER_UNDEF_SYMBOL = -1177,


  /********************************************************/
  /*                    TRIO  Error Codes                 */
  /********************************************************/

  /** Cannot allocate memory map region. */
  GXIO_TRIO_ERR_NO_MEMORY_MAP = -1180,

  /** Invalid memory map region number. */
  GXIO_TRIO_ERR_BAD_MEMORY_MAP = -1181,

  /** Cannot allocate scatter queue. */
  GXIO_TRIO_ERR_NO_SCATTER_QUEUE = -1182,

  /** Invalid scatter queue number. */
  GXIO_TRIO_ERR_BAD_SCATTER_QUEUE = -1183,

  /** Cannot allocate push DMA ring. */
  GXIO_TRIO_ERR_NO_PUSH_DMA_RING = -1184,

  /** Invalid push DMA ring index. */
  GXIO_TRIO_ERR_BAD_PUSH_DMA_RING = -1185,

  /** Cannot allocate pull DMA ring. */
  GXIO_TRIO_ERR_NO_PULL_DMA_RING = -1186,

  /** Invalid pull DMA ring index. */
  GXIO_TRIO_ERR_BAD_PULL_DMA_RING = -1187,

  /** Cannot allocate PIO region. */
  GXIO_TRIO_ERR_NO_PIO = -1188,

  /** Invalid PIO region index. */
  GXIO_TRIO_ERR_BAD_PIO = -1189,

  /** Cannot allocate ASID. */
  GXIO_TRIO_ERR_NO_ASID = -1190,

  /** Invalid ASID. */
  GXIO_TRIO_ERR_BAD_ASID = -1191,


  /********************************************************/
  /*                    MICA Error Codes                  */
  /********************************************************/

  /** No such accelerator type. */
  GXIO_MICA_ERR_BAD_ACCEL_TYPE = -1220,

  /** Cannot allocate context. */
  GXIO_MICA_ERR_NO_CONTEXT = -1221,

  /** PKA command queue is full, can't add another command. */
  GXIO_MICA_ERR_PKA_CMD_QUEUE_FULL = -1222,

  /** PKA result queue is empty, can't get a result from the queue. */
  GXIO_MICA_ERR_PKA_RESULT_QUEUE_EMPTY = -1223,

  /********************************************************/
  /*                    GPIO Error Codes                  */
  /********************************************************/

  /** Pin not available.  Either the physical pin does not exist, or
   *  it is reserved by the hypervisor for system usage. */
  GXIO_GPIO_ERR_PIN_UNAVAILABLE = -1240,

  /** Pin busy.  The pin exists, and is available for use via GXIO, but
   *  it has been attached by some other process or driver. */
  GXIO_GPIO_ERR_PIN_BUSY = -1241,

  /** Cannot access unattached pin.  One or more of the pins being
   *  manipulated by this call are not attached to the requesting
   *  context. */
  GXIO_GPIO_ERR_PIN_UNATTACHED = -1242,

  /** Invalid I/O mode for pin.  The wiring of the pin in the system
   *  is such that the I/O mode or electrical control parameters
   *  requested could cause damage. */
  GXIO_GPIO_ERR_PIN_INVALID_MODE = -1243,

  /** Smallest iorpc error number. */
  GXIO_ERR_MIN = -1299
};


#endif /* !_HV_IORPC_H_ */
