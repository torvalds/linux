/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
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

/**
 * @file hypervisor.h
 * The hypervisor's public API.
 */

#ifndef _HV_HV_H
#define _HV_HV_H

#include <arch/chip.h>

/* Linux builds want unsigned long constants, but assembler wants numbers */
#ifdef __ASSEMBLER__
/** One, for assembler */
#define __HV_SIZE_ONE 1
#elif !defined(__tile__) && CHIP_VA_WIDTH() > 32
/** One, for 64-bit on host */
#define __HV_SIZE_ONE 1ULL
#else
/** One, for Linux */
#define __HV_SIZE_ONE 1UL
#endif

/** The log2 of the span of a level-1 page table, in bytes.
 */
#define HV_LOG2_L1_SPAN 32

/** The span of a level-1 page table, in bytes.
 */
#define HV_L1_SPAN (__HV_SIZE_ONE << HV_LOG2_L1_SPAN)

/** The log2 of the initial size of small pages, in bytes.
 * See HV_DEFAULT_PAGE_SIZE_SMALL.
 */
#define HV_LOG2_DEFAULT_PAGE_SIZE_SMALL 16

/** The initial size of small pages, in bytes. This value should be verified
 * at runtime by calling hv_sysconf(HV_SYSCONF_PAGE_SIZE_SMALL).
 * It may also be modified when installing a new context.
 */
#define HV_DEFAULT_PAGE_SIZE_SMALL \
  (__HV_SIZE_ONE << HV_LOG2_DEFAULT_PAGE_SIZE_SMALL)

/** The log2 of the initial size of large pages, in bytes.
 * See HV_DEFAULT_PAGE_SIZE_LARGE.
 */
#define HV_LOG2_DEFAULT_PAGE_SIZE_LARGE 24

/** The initial size of large pages, in bytes. This value should be verified
 * at runtime by calling hv_sysconf(HV_SYSCONF_PAGE_SIZE_LARGE).
 * It may also be modified when installing a new context.
 */
#define HV_DEFAULT_PAGE_SIZE_LARGE \
  (__HV_SIZE_ONE << HV_LOG2_DEFAULT_PAGE_SIZE_LARGE)

#if CHIP_VA_WIDTH() > 32

/** The log2 of the initial size of jumbo pages, in bytes.
 * See HV_DEFAULT_PAGE_SIZE_JUMBO.
 */
#define HV_LOG2_DEFAULT_PAGE_SIZE_JUMBO 32

/** The initial size of jumbo pages, in bytes. This value should
 * be verified at runtime by calling hv_sysconf(HV_SYSCONF_PAGE_SIZE_JUMBO).
 * It may also be modified when installing a new context.
 */
#define HV_DEFAULT_PAGE_SIZE_JUMBO \
  (__HV_SIZE_ONE << HV_LOG2_DEFAULT_PAGE_SIZE_JUMBO)

#endif

/** The log2 of the granularity at which page tables must be aligned;
 *  in other words, the CPA for a page table must have this many zero
 *  bits at the bottom of the address.
 */
#define HV_LOG2_PAGE_TABLE_ALIGN 11

/** The granularity at which page tables must be aligned.
 */
#define HV_PAGE_TABLE_ALIGN (__HV_SIZE_ONE << HV_LOG2_PAGE_TABLE_ALIGN)

/** Normal start of hypervisor glue in client physical memory. */
#define HV_GLUE_START_CPA 0x10000

/** This much space is reserved at HV_GLUE_START_CPA
 * for the hypervisor glue. The client program must start at
 * some address higher than this, and in particular the address of
 * its text section should be equal to zero modulo HV_PAGE_SIZE_LARGE
 * so that relative offsets to the HV glue are correct.
 */
#define HV_GLUE_RESERVED_SIZE 0x10000

/** Each entry in the hv dispatch array takes this many bytes. */
#define HV_DISPATCH_ENTRY_SIZE 32

/** Version of the hypervisor interface defined by this file */
#define _HV_VERSION 13

/** Last version of the hypervisor interface with old hv_init() ABI.
 *
 * The change from version 12 to version 13 corresponds to launching
 * the client by default at PL2 instead of PL1 (corresponding to the
 * hv itself running at PL3 instead of PL2).  To make this explicit,
 * the hv_init() API was also extended so the client can report its
 * desired PL, resulting in a more helpful failure diagnostic.  If you
 * call hv_init() with _HV_VERSION_OLD_HV_INIT and omit the client_pl
 * argument, the hypervisor will assume client_pl = 1.
 *
 * Note that this is a deprecated solution and we do not expect to
 * support clients of the Tilera hypervisor running at PL1 indefinitely.
 */
#define _HV_VERSION_OLD_HV_INIT 12

/* Index into hypervisor interface dispatch code blocks.
 *
 * Hypervisor calls are invoked from user space by calling code
 * at an address HV_BASE_ADDRESS + (index) * HV_DISPATCH_ENTRY_SIZE,
 * where index is one of these enum values.
 *
 * Normally a supervisor is expected to produce a set of symbols
 * starting at HV_BASE_ADDRESS that obey this convention, but a user
 * program could call directly through function pointers if desired.
 *
 * These numbers are part of the binary API and will not be changed
 * without updating HV_VERSION, which should be a rare event.
 */

/** reserved. */
#define _HV_DISPATCH_RESERVED                     0

/** hv_init  */
#define HV_DISPATCH_INIT                          1

/** hv_install_context */
#define HV_DISPATCH_INSTALL_CONTEXT               2

/** hv_sysconf */
#define HV_DISPATCH_SYSCONF                       3

/** hv_get_rtc */
#define HV_DISPATCH_GET_RTC                       4

/** hv_set_rtc */
#define HV_DISPATCH_SET_RTC                       5

/** hv_flush_asid */
#define HV_DISPATCH_FLUSH_ASID                    6

/** hv_flush_page */
#define HV_DISPATCH_FLUSH_PAGE                    7

/** hv_flush_pages */
#define HV_DISPATCH_FLUSH_PAGES                   8

/** hv_restart */
#define HV_DISPATCH_RESTART                       9

/** hv_halt */
#define HV_DISPATCH_HALT                          10

/** hv_power_off */
#define HV_DISPATCH_POWER_OFF                     11

/** hv_inquire_physical */
#define HV_DISPATCH_INQUIRE_PHYSICAL              12

/** hv_inquire_memory_controller */
#define HV_DISPATCH_INQUIRE_MEMORY_CONTROLLER     13

/** hv_inquire_virtual */
#define HV_DISPATCH_INQUIRE_VIRTUAL               14

/** hv_inquire_asid */
#define HV_DISPATCH_INQUIRE_ASID                  15

/** hv_nanosleep */
#define HV_DISPATCH_NANOSLEEP                     16

/** hv_console_read_if_ready */
#define HV_DISPATCH_CONSOLE_READ_IF_READY         17

/** hv_console_write */
#define HV_DISPATCH_CONSOLE_WRITE                 18

/** hv_downcall_dispatch */
#define HV_DISPATCH_DOWNCALL_DISPATCH             19

/** hv_inquire_topology */
#define HV_DISPATCH_INQUIRE_TOPOLOGY              20

/** hv_fs_findfile */
#define HV_DISPATCH_FS_FINDFILE                   21

/** hv_fs_fstat */
#define HV_DISPATCH_FS_FSTAT                      22

/** hv_fs_pread */
#define HV_DISPATCH_FS_PREAD                      23

/** hv_physaddr_read64 */
#define HV_DISPATCH_PHYSADDR_READ64               24

/** hv_physaddr_write64 */
#define HV_DISPATCH_PHYSADDR_WRITE64              25

/** hv_get_command_line */
#define HV_DISPATCH_GET_COMMAND_LINE              26

/** hv_set_caching */
#define HV_DISPATCH_SET_CACHING                   27

/** hv_bzero_page */
#define HV_DISPATCH_BZERO_PAGE                    28

/** hv_register_message_state */
#define HV_DISPATCH_REGISTER_MESSAGE_STATE        29

/** hv_send_message */
#define HV_DISPATCH_SEND_MESSAGE                  30

/** hv_receive_message */
#define HV_DISPATCH_RECEIVE_MESSAGE               31

/** hv_inquire_context */
#define HV_DISPATCH_INQUIRE_CONTEXT               32

/** hv_start_all_tiles */
#define HV_DISPATCH_START_ALL_TILES               33

/** hv_dev_open */
#define HV_DISPATCH_DEV_OPEN                      34

/** hv_dev_close */
#define HV_DISPATCH_DEV_CLOSE                     35

/** hv_dev_pread */
#define HV_DISPATCH_DEV_PREAD                     36

/** hv_dev_pwrite */
#define HV_DISPATCH_DEV_PWRITE                    37

/** hv_dev_poll */
#define HV_DISPATCH_DEV_POLL                      38

/** hv_dev_poll_cancel */
#define HV_DISPATCH_DEV_POLL_CANCEL               39

/** hv_dev_preada */
#define HV_DISPATCH_DEV_PREADA                    40

/** hv_dev_pwritea */
#define HV_DISPATCH_DEV_PWRITEA                   41

/** hv_flush_remote */
#define HV_DISPATCH_FLUSH_REMOTE                  42

/** hv_console_putc */
#define HV_DISPATCH_CONSOLE_PUTC                  43

/** hv_inquire_tiles */
#define HV_DISPATCH_INQUIRE_TILES                 44

/** hv_confstr */
#define HV_DISPATCH_CONFSTR                       45

/** hv_reexec */
#define HV_DISPATCH_REEXEC                        46

/** hv_set_command_line */
#define HV_DISPATCH_SET_COMMAND_LINE              47

#if !CHIP_HAS_IPI()

/** hv_clear_intr */
#define HV_DISPATCH_CLEAR_INTR                    48

/** hv_enable_intr */
#define HV_DISPATCH_ENABLE_INTR                   49

/** hv_disable_intr */
#define HV_DISPATCH_DISABLE_INTR                  50

/** hv_raise_intr */
#define HV_DISPATCH_RAISE_INTR                    51

/** hv_trigger_ipi */
#define HV_DISPATCH_TRIGGER_IPI                   52

#endif /* !CHIP_HAS_IPI() */

/** hv_store_mapping */
#define HV_DISPATCH_STORE_MAPPING                 53

/** hv_inquire_realpa */
#define HV_DISPATCH_INQUIRE_REALPA                54

/** hv_flush_all */
#define HV_DISPATCH_FLUSH_ALL                     55

#if CHIP_HAS_IPI()
/** hv_get_ipi_pte */
#define HV_DISPATCH_GET_IPI_PTE                   56
#endif

/** hv_set_pte_super_shift */
#define HV_DISPATCH_SET_PTE_SUPER_SHIFT           57

/** One more than the largest dispatch value */
#define _HV_DISPATCH_END                          58


#ifndef __ASSEMBLER__

#ifdef __KERNEL__
#include <asm/types.h>
typedef u32 __hv32;        /**< 32-bit value */
typedef u64 __hv64;        /**< 64-bit value */
#else
#include <stdint.h>
typedef uint32_t __hv32;   /**< 32-bit value */
typedef uint64_t __hv64;   /**< 64-bit value */
#endif


/** Hypervisor physical address. */
typedef __hv64 HV_PhysAddr;

#if CHIP_VA_WIDTH() > 32
/** Hypervisor virtual address. */
typedef __hv64 HV_VirtAddr;
#else
/** Hypervisor virtual address. */
typedef __hv32 HV_VirtAddr;
#endif /* CHIP_VA_WIDTH() > 32 */

/** Hypervisor ASID. */
typedef unsigned int HV_ASID;

/** Hypervisor tile location for a memory access
 * ("location overridden target").
 */
typedef unsigned int HV_LOTAR;

/** Hypervisor size of a page. */
typedef unsigned long HV_PageSize;

/** A page table entry.
 */
typedef struct
{
  __hv64 val;                /**< Value of PTE */
} HV_PTE;

/** Hypervisor error code. */
typedef int HV_Errno;

#endif /* !__ASSEMBLER__ */

#define HV_OK           0    /**< No error */
#define HV_EINVAL      -801  /**< Invalid argument */
#define HV_ENODEV      -802  /**< No such device */
#define HV_ENOENT      -803  /**< No such file or directory */
#define HV_EBADF       -804  /**< Bad file number */
#define HV_EFAULT      -805  /**< Bad address */
#define HV_ERECIP      -806  /**< Bad recipients */
#define HV_E2BIG       -807  /**< Message too big */
#define HV_ENOTSUP     -808  /**< Service not supported */
#define HV_EBUSY       -809  /**< Device busy */
#define HV_ENOSYS      -810  /**< Invalid syscall */
#define HV_EPERM       -811  /**< No permission */
#define HV_ENOTREADY   -812  /**< Device not ready */
#define HV_EIO         -813  /**< I/O error */
#define HV_ENOMEM      -814  /**< Out of memory */
#define HV_EAGAIN      -815  /**< Try again */

#define HV_ERR_MAX     -801  /**< Largest HV error code */
#define HV_ERR_MIN     -815  /**< Smallest HV error code */

#ifndef __ASSEMBLER__

/** Pass HV_VERSION to hv_init to request this version of the interface. */
typedef enum {
  HV_VERSION = _HV_VERSION,
  HV_VERSION_OLD_HV_INIT = _HV_VERSION_OLD_HV_INIT,

} HV_VersionNumber;

/** Initializes the hypervisor.
 *
 * @param interface_version_number The version of the hypervisor interface
 * that this program expects, typically HV_VERSION.
 * @param chip_num Architecture number of the chip the client was built for.
 * @param chip_rev_num Revision number of the chip the client was built for.
 * @param client_pl Privilege level the client is built for
 *   (not required if interface_version_number == HV_VERSION_OLD_HV_INIT).
 */
void hv_init(HV_VersionNumber interface_version_number,
             int chip_num, int chip_rev_num, int client_pl);


/** Queries we can make for hv_sysconf().
 *
 * These numbers are part of the binary API and guaranteed not to change.
 */
typedef enum {
  /** An invalid value; do not use. */
  _HV_SYSCONF_RESERVED       = 0,

  /** The length of the glue section containing the hv_ procs, in bytes. */
  HV_SYSCONF_GLUE_SIZE       = 1,

  /** The size of small pages, in bytes. */
  HV_SYSCONF_PAGE_SIZE_SMALL = 2,

  /** The size of large pages, in bytes. */
  HV_SYSCONF_PAGE_SIZE_LARGE = 3,

  /** Processor clock speed, in hertz. */
  HV_SYSCONF_CPU_SPEED       = 4,

  /** Processor temperature, in degrees Kelvin.  The value
   *  HV_SYSCONF_TEMP_KTOC may be subtracted from this to get degrees
   *  Celsius.  If that Celsius value is HV_SYSCONF_OVERTEMP, this indicates
   *  that the temperature has hit an upper limit and is no longer being
   *  accurately tracked.
   */
  HV_SYSCONF_CPU_TEMP        = 5,

  /** Board temperature, in degrees Kelvin.  The value
   *  HV_SYSCONF_TEMP_KTOC may be subtracted from this to get degrees
   *  Celsius.  If that Celsius value is HV_SYSCONF_OVERTEMP, this indicates
   *  that the temperature has hit an upper limit and is no longer being
   *  accurately tracked.
   */
  HV_SYSCONF_BOARD_TEMP      = 6,

  /** Legal page size bitmask for hv_install_context().
   * For example, if 16KB and 64KB small pages are supported,
   * it would return "HV_CTX_PG_SM_16K | HV_CTX_PG_SM_64K".
   */
  HV_SYSCONF_VALID_PAGE_SIZES = 7,

  /** The size of jumbo pages, in bytes.
   * If no jumbo pages are available, zero will be returned.
   */
  HV_SYSCONF_PAGE_SIZE_JUMBO = 8,

} HV_SysconfQuery;

/** Offset to subtract from returned Kelvin temperature to get degrees
    Celsius. */
#define HV_SYSCONF_TEMP_KTOC 273

/** Pseudo-temperature value indicating that the temperature has
 *  pegged at its upper limit and is no longer accurate; note that this is
 *  the value after subtracting HV_SYSCONF_TEMP_KTOC. */
#define HV_SYSCONF_OVERTEMP 999

/** Query a configuration value from the hypervisor.
 * @param query Which value is requested (HV_SYSCONF_xxx).
 * @return The requested value, or -1 the requested value is illegal or
 *         unavailable.
 */
long hv_sysconf(HV_SysconfQuery query);


/** Queries we can make for hv_confstr().
 *
 * These numbers are part of the binary API and guaranteed not to change.
 */
typedef enum {
  /** An invalid value; do not use. */
  _HV_CONFSTR_RESERVED        = 0,

  /** Board part number. */
  HV_CONFSTR_BOARD_PART_NUM   = 1,

  /** Board serial number. */
  HV_CONFSTR_BOARD_SERIAL_NUM = 2,

  /** Chip serial number. */
  HV_CONFSTR_CHIP_SERIAL_NUM  = 3,

  /** Board revision level. */
  HV_CONFSTR_BOARD_REV        = 4,

  /** Hypervisor software version. */
  HV_CONFSTR_HV_SW_VER        = 5,

  /** The name for this chip model. */
  HV_CONFSTR_CHIP_MODEL       = 6,

  /** Human-readable board description. */
  HV_CONFSTR_BOARD_DESC       = 7,

  /** Human-readable description of the hypervisor configuration. */
  HV_CONFSTR_HV_CONFIG        = 8,

  /** Human-readable version string for the boot image (for instance,
   *  who built it and when, what configuration file was used). */
  HV_CONFSTR_HV_CONFIG_VER    = 9,

  /** Mezzanine part number. */
  HV_CONFSTR_MEZZ_PART_NUM   = 10,

  /** Mezzanine serial number. */
  HV_CONFSTR_MEZZ_SERIAL_NUM = 11,

  /** Mezzanine revision level. */
  HV_CONFSTR_MEZZ_REV        = 12,

  /** Human-readable mezzanine description. */
  HV_CONFSTR_MEZZ_DESC       = 13,

  /** Control path for the onboard network switch. */
  HV_CONFSTR_SWITCH_CONTROL  = 14,

  /** Chip revision level. */
  HV_CONFSTR_CHIP_REV        = 15,

  /** CPU module part number. */
  HV_CONFSTR_CPUMOD_PART_NUM = 16,

  /** CPU module serial number. */
  HV_CONFSTR_CPUMOD_SERIAL_NUM = 17,

  /** CPU module revision level. */
  HV_CONFSTR_CPUMOD_REV      = 18,

  /** Human-readable CPU module description. */
  HV_CONFSTR_CPUMOD_DESC     = 19

} HV_ConfstrQuery;

/** Query a configuration string from the hypervisor.
 *
 * @param query Identifier for the specific string to be retrieved
 *        (HV_CONFSTR_xxx).
 * @param buf Buffer in which to place the string.
 * @param len Length of the buffer.
 * @return If query is valid, then the length of the corresponding string,
 *        including the trailing null; if this is greater than len, the string
 *        was truncated.  If query is invalid, HV_EINVAL.  If the specified
 *        buffer is not writable by the client, HV_EFAULT.
 */
int hv_confstr(HV_ConfstrQuery query, HV_VirtAddr buf, int len);

/** Tile coordinate */
typedef struct
{
#ifndef __BIG_ENDIAN__
  /** X coordinate, relative to supervisor's top-left coordinate */
  int x;

  /** Y coordinate, relative to supervisor's top-left coordinate */
  int y;
#else
  int y;
  int x;
#endif
} HV_Coord;


#if CHIP_HAS_IPI()

/** Get the PTE for sending an IPI to a particular tile.
 *
 * @param tile Tile which will receive the IPI.
 * @param pl Indicates which IPI registers: 0 = IPI_0, 1 = IPI_1.
 * @param pte Filled with resulting PTE.
 * @result Zero if no error, non-zero for invalid parameters.
 */
int hv_get_ipi_pte(HV_Coord tile, int pl, HV_PTE* pte);

#else /* !CHIP_HAS_IPI() */

/** A set of interrupts. */
typedef __hv32 HV_IntrMask;

/** The low interrupt numbers are reserved for use by the client in
 *  delivering IPIs.  Any interrupt numbers higher than this value are
 *  reserved for use by HV device drivers. */
#define HV_MAX_IPI_INTERRUPT 7

/** Enable a set of device interrupts.
 *
 * @param enab_mask Bitmap of interrupts to enable.
 */
void hv_enable_intr(HV_IntrMask enab_mask);

/** Disable a set of device interrupts.
 *
 * @param disab_mask Bitmap of interrupts to disable.
 */
void hv_disable_intr(HV_IntrMask disab_mask);

/** Clear a set of device interrupts.
 *
 * @param clear_mask Bitmap of interrupts to clear.
 */
void hv_clear_intr(HV_IntrMask clear_mask);

/** Raise a set of device interrupts.
 *
 * @param raise_mask Bitmap of interrupts to raise.
 */
void hv_raise_intr(HV_IntrMask raise_mask);

/** Trigger a one-shot interrupt on some tile
 *
 * @param tile Which tile to interrupt.
 * @param interrupt Interrupt number to trigger; must be between 0 and
 *        HV_MAX_IPI_INTERRUPT.
 * @return HV_OK on success, or a hypervisor error code.
 */
HV_Errno hv_trigger_ipi(HV_Coord tile, int interrupt);

#endif /* !CHIP_HAS_IPI() */

/** Store memory mapping in debug memory so that external debugger can read it.
 * A maximum of 16 entries can be stored.
 *
 * @param va VA of memory that is mapped.
 * @param len Length of mapped memory.
 * @param pa PA of memory that is mapped.
 * @return 0 on success, -1 if the maximum number of mappings is exceeded.
 */
int hv_store_mapping(HV_VirtAddr va, unsigned int len, HV_PhysAddr pa);

/** Given a client PA and a length, return its real (HV) PA.
 *
 * @param cpa Client physical address.
 * @param len Length of mapped memory.
 * @return physical address, or -1 if cpa or len is not valid.
 */
HV_PhysAddr hv_inquire_realpa(HV_PhysAddr cpa, unsigned int len);

/** RTC return flag for no RTC chip present.
 */
#define HV_RTC_NO_CHIP     0x1

/** RTC return flag for low-voltage condition, indicating that battery had
 * died and time read is unreliable.
 */
#define HV_RTC_LOW_VOLTAGE 0x2

/** Date/Time of day */
typedef struct {
#if CHIP_WORD_SIZE() > 32
  __hv64 tm_sec;   /**< Seconds, 0-59 */
  __hv64 tm_min;   /**< Minutes, 0-59 */
  __hv64 tm_hour;  /**< Hours, 0-23 */
  __hv64 tm_mday;  /**< Day of month, 0-30 */
  __hv64 tm_mon;   /**< Month, 0-11 */
  __hv64 tm_year;  /**< Years since 1900, 0-199 */
  __hv64 flags;    /**< Return flags, 0 if no error */
#else
  __hv32 tm_sec;   /**< Seconds, 0-59 */
  __hv32 tm_min;   /**< Minutes, 0-59 */
  __hv32 tm_hour;  /**< Hours, 0-23 */
  __hv32 tm_mday;  /**< Day of month, 0-30 */
  __hv32 tm_mon;   /**< Month, 0-11 */
  __hv32 tm_year;  /**< Years since 1900, 0-199 */
  __hv32 flags;    /**< Return flags, 0 if no error */
#endif
} HV_RTCTime;

/** Read the current time-of-day clock.
 * @return HV_RTCTime of current time (GMT).
 */
HV_RTCTime hv_get_rtc(void);


/** Set the current time-of-day clock.
 * @param time time to reset time-of-day to (GMT).
 */
void hv_set_rtc(HV_RTCTime time);

/** Installs a context, comprising a page table and other attributes.
 *
 *  Once this service completes, page_table will be used to translate
 *  subsequent virtual address references to physical memory.
 *
 *  Installing a context does not cause an implicit TLB flush.  Before
 *  reusing an ASID value for a different address space, the client is
 *  expected to flush old references from the TLB with hv_flush_asid().
 *  (Alternately, hv_flush_all() may be used to flush many ASIDs at once.)
 *  After invalidating a page table entry, changing its attributes, or
 *  changing its target CPA, the client is expected to flush old references
 *  from the TLB with hv_flush_page() or hv_flush_pages(). Making a
 *  previously invalid page valid does not require a flush.
 *
 *  Specifying an invalid ASID, or an invalid CPA (client physical address)
 *  (either as page_table_pointer, or within the referenced table),
 *  or another page table data item documented as above as illegal may
 *  lead to client termination; since the validation of the table is
 *  done as needed, this may happen before the service returns, or at
 *  some later time, or never, depending upon the client's pattern of
 *  memory references.  Page table entries which supply translations for
 *  invalid virtual addresses may result in client termination, or may
 *  be silently ignored.  "Invalid" in this context means a value which
 *  was not provided to the client via the appropriate hv_inquire_* routine.
 *
 *  To support changing the instruction VAs at the same time as
 *  installing the new page table, this call explicitly supports
 *  setting the "lr" register to a different address and then jumping
 *  directly to the hv_install_context() routine.  In this case, the
 *  new page table does not need to contain any mapping for the
 *  hv_install_context address itself.
 *
 *  At most one HV_CTX_PG_SM_* flag may be specified in "flags";
 *  if multiple flags are specified, HV_EINVAL is returned.
 *  Specifying none of the flags results in using the default page size.
 *  All cores participating in a given client must request the same
 *  page size, or the results are undefined.
 *
 * @param page_table Root of the page table.
 * @param access PTE providing info on how to read the page table.  This
 *   value must be consistent between multiple tiles sharing a page table,
 *   and must also be consistent with any virtual mappings the client
 *   may be using to access the page table.
 * @param asid HV_ASID the page table is to be used for.
 * @param flags Context flags, denoting attributes or privileges of the
 *   current context (HV_CTX_xxx).
 * @return Zero on success, or a hypervisor error code on failure.
 */
int hv_install_context(HV_PhysAddr page_table, HV_PTE access, HV_ASID asid,
                       __hv32 flags);

#endif /* !__ASSEMBLER__ */

#define HV_CTX_DIRECTIO     0x1   /**< Direct I/O requests are accepted from
                                       PL0. */

#define HV_CTX_PG_SM_4K     0x10  /**< Use 4K small pages, if available. */
#define HV_CTX_PG_SM_16K    0x20  /**< Use 16K small pages, if available. */
#define HV_CTX_PG_SM_64K    0x40  /**< Use 64K small pages, if available. */
#define HV_CTX_PG_SM_MASK   0xf0  /**< Mask of all possible small pages. */

#ifndef __ASSEMBLER__


/** Set the number of pages ganged together by HV_PTE_SUPER at a
 * particular level of the page table.
 *
 * The current TILE-Gx hardware only supports powers of four
 * (i.e. log2_count must be a multiple of two), and the requested
 * "super" page size must be less than the span of the next level in
 * the page table.  The largest size that can be requested is 64GB.
 *
 * The shift value is initially "0" for all page table levels,
 * indicating that the HV_PTE_SUPER bit is effectively ignored.
 *
 * If you change the count from one non-zero value to another, the
 * hypervisor will flush the entire TLB and TSB to avoid confusion.
 *
 * @param level Page table level (0, 1, or 2)
 * @param log2_count Base-2 log of the number of pages to gang together,
 * i.e. how much to shift left the base page size for the super page size.
 * @return Zero on success, or a hypervisor error code on failure.
 */
int hv_set_pte_super_shift(int level, int log2_count);


/** Value returned from hv_inquire_context(). */
typedef struct
{
  /** Physical address of page table */
  HV_PhysAddr page_table;

  /** PTE which defines access method for top of page table */
  HV_PTE access;

  /** ASID associated with this page table */
  HV_ASID asid;

  /** Context flags */
  __hv32 flags;
} HV_Context;

/** Retrieve information about the currently installed context.
 * @return The data passed to the last successful hv_install_context call.
 */
HV_Context hv_inquire_context(void);


/** Flushes all translations associated with the named address space
 *  identifier from the TLB and any other hypervisor data structures.
 *  Translations installed with the "global" bit are not flushed.
 *
 *  Specifying an invalid ASID may lead to client termination.  "Invalid"
 *  in this context means a value which was not provided to the client
 *  via <tt>hv_inquire_asid()</tt>.
 *
 * @param asid HV_ASID whose entries are to be flushed.
 * @return Zero on success, or a hypervisor error code on failure.
*/
int hv_flush_asid(HV_ASID asid);


/** Flushes all translations associated with the named virtual address
 *  and page size from the TLB and other hypervisor data structures. Only
 *  pages visible to the current ASID are affected; note that this includes
 *  global pages in addition to pages specific to the current ASID.
 *
 *  The supplied VA need not be aligned; it may be anywhere in the
 *  subject page.
 *
 *  Specifying an invalid virtual address may lead to client termination,
 *  or may silently succeed.  "Invalid" in this context means a value
 *  which was not provided to the client via hv_inquire_virtual.
 *
 * @param address Address of the page to flush.
 * @param page_size Size of pages to assume.
 * @return Zero on success, or a hypervisor error code on failure.
 */
int hv_flush_page(HV_VirtAddr address, HV_PageSize page_size);


/** Flushes all translations associated with the named virtual address range
 *  and page size from the TLB and other hypervisor data structures. Only
 *  pages visible to the current ASID are affected; note that this includes
 *  global pages in addition to pages specific to the current ASID.
 *
 *  The supplied VA need not be aligned; it may be anywhere in the
 *  subject page.
 *
 *  Specifying an invalid virtual address may lead to client termination,
 *  or may silently succeed.  "Invalid" in this context means a value
 *  which was not provided to the client via hv_inquire_virtual.
 *
 * @param start Address to flush.
 * @param page_size Size of pages to assume.
 * @param size The number of bytes to flush. Any page in the range
 *        [start, start + size) will be flushed from the TLB.
 * @return Zero on success, or a hypervisor error code on failure.
 */
int hv_flush_pages(HV_VirtAddr start, HV_PageSize page_size,
                   unsigned long size);


/** Flushes all non-global translations (if preserve_global is true),
 *  or absolutely all translations (if preserve_global is false).
 *
 * @param preserve_global Non-zero if we want to preserve "global" mappings.
 * @return Zero on success, or a hypervisor error code on failure.
*/
int hv_flush_all(int preserve_global);


/** Restart machine with optional restart command and optional args.
 * @param cmd Const pointer to command to restart with, or NULL
 * @param args Const pointer to argument string to restart with, or NULL
 */
void hv_restart(HV_VirtAddr cmd, HV_VirtAddr args);


/** Halt machine. */
void hv_halt(void);


/** Power off machine. */
void hv_power_off(void);


/** Re-enter virtual-is-physical memory translation mode and restart
 *  execution at a given address.
 * @param entry Client physical address at which to begin execution.
 * @return A hypervisor error code on failure; if the operation is
 *         successful the call does not return.
 */
int hv_reexec(HV_PhysAddr entry);


/** Chip topology */
typedef struct
{
  /** Relative coordinates of the querying tile */
  HV_Coord coord;

  /** Width of the querying supervisor's tile rectangle. */
  int width;

  /** Height of the querying supervisor's tile rectangle. */
  int height;

} HV_Topology;

/** Returns information about the tile coordinate system.
 *
 * Each supervisor is given a rectangle of tiles it potentially controls.
 * These tiles are labeled using a relative coordinate system with (0,0) as
 * the upper left tile regardless of their physical location on the chip.
 *
 * This call returns both the size of that rectangle and the position
 * within that rectangle of the querying tile.
 *
 * Not all tiles within that rectangle may be available to the supervisor;
 * to get the precise set of available tiles, you must also call
 * hv_inquire_tiles(HV_INQ_TILES_AVAIL, ...).
 **/
HV_Topology hv_inquire_topology(void);

/** Sets of tiles we can retrieve with hv_inquire_tiles().
 *
 * These numbers are part of the binary API and guaranteed not to change.
 */
typedef enum {
  /** An invalid value; do not use. */
  _HV_INQ_TILES_RESERVED       = 0,

  /** All available tiles within the supervisor's tile rectangle. */
  HV_INQ_TILES_AVAIL           = 1,

  /** The set of tiles used for hash-for-home caching. */
  HV_INQ_TILES_HFH_CACHE       = 2,

  /** The set of tiles that can be legally used as a LOTAR for a PTE. */
  HV_INQ_TILES_LOTAR           = 3
} HV_InqTileSet;

/** Returns specific information about various sets of tiles within the
 *  supervisor's tile rectangle.
 *
 * @param set Which set of tiles to retrieve.
 * @param cpumask Pointer to a returned bitmask (in row-major order,
 *        supervisor-relative) of tiles.  The low bit of the first word
 *        corresponds to the tile at the upper left-hand corner of the
 *        supervisor's rectangle.  In order for the supervisor to know the
 *        buffer length to supply, it should first call hv_inquire_topology.
 * @param length Number of bytes available for the returned bitmask.
 **/
HV_Errno hv_inquire_tiles(HV_InqTileSet set, HV_VirtAddr cpumask, int length);


/** An identifier for a memory controller. Multiple memory controllers
 * may be connected to one chip, and this uniquely identifies each one.
 */
typedef int HV_MemoryController;

/** A range of physical memory. */
typedef struct
{
  HV_PhysAddr start;   /**< Starting address. */
  __hv64 size;         /**< Size in bytes. */
  HV_MemoryController controller;  /**< Which memory controller owns this. */
} HV_PhysAddrRange;

/** Returns information about a range of physical memory.
 *
 * hv_inquire_physical() returns one of the ranges of client
 * physical addresses which are available to this client.
 *
 * The first range is retrieved by specifying an idx of 0, and
 * successive ranges are returned with subsequent idx values.  Ranges
 * are ordered by increasing start address (i.e., as idx increases,
 * so does start), do not overlap, and do not touch (i.e., the
 * available memory is described with the fewest possible ranges).
 *
 * If an out-of-range idx value is specified, the returned size will be zero.
 * A client can count the number of ranges by increasing idx until the
 * returned size is zero. There will always be at least one valid range.
 *
 * Some clients might not be prepared to deal with more than one
 * physical address range; they still ought to call this routine and
 * issue a warning message if they're given more than one range, on the
 * theory that whoever configured the hypervisor to provide that memory
 * should know that it's being wasted.
 */
HV_PhysAddrRange hv_inquire_physical(int idx);

/** Possible DIMM types. */
typedef enum
{
  NO_DIMM                    = 0,  /**< No DIMM */
  DDR2                       = 1,  /**< DDR2 */
  DDR3                       = 2   /**< DDR3 */
} HV_DIMM_Type;

#ifdef __tilegx__

/** Log2 of minimum DIMM bytes supported by the memory controller. */
#define HV_MSH_MIN_DIMM_SIZE_SHIFT 29

/** Max number of DIMMs contained by one memory controller. */
#define HV_MSH_MAX_DIMMS 8

#else

/** Log2 of minimum DIMM bytes supported by the memory controller. */
#define HV_MSH_MIN_DIMM_SIZE_SHIFT 26

/** Max number of DIMMs contained by one memory controller. */
#define HV_MSH_MAX_DIMMS 2

#endif

/** Number of bits to right-shift to get the DIMM type. */
#define HV_DIMM_TYPE_SHIFT 0

/** Bits to mask to get the DIMM type. */
#define HV_DIMM_TYPE_MASK 0xf

/** Number of bits to right-shift to get the DIMM size. */
#define HV_DIMM_SIZE_SHIFT 4

/** Bits to mask to get the DIMM size. */
#define HV_DIMM_SIZE_MASK 0xf

/** Memory controller information. */
typedef struct
{
  HV_Coord coord;   /**< Relative tile coordinates of the port used by a
                         specified tile to communicate with this controller. */
  __hv64 speed;     /**< Speed of this controller in bytes per second. */
} HV_MemoryControllerInfo;

/** Returns information about a particular memory controller.
 *
 *  hv_inquire_memory_controller(coord,idx) returns information about a
 *  particular controller.  Two pieces of information are returned:
 *  - The relative coordinates of the port on the controller that the specified
 *    tile would use to contact it.  The relative coordinates may lie
 *    outside the supervisor's rectangle, i.e. the controller may not
 *    be attached to a node managed by the querying node's supervisor.
 *    In particular note that x or y may be negative.
 *  - The speed of the memory controller.  (This is a not-to-exceed value
 *    based on the raw hardware data rate, and may not be achievable in
 *    practice; it is provided to give clients information on the relative
 *    performance of the available controllers.)
 *
 *  Clients should avoid calling this interface with invalid values.
 *  A client who does may be terminated.
 * @param coord Tile for which to calculate the relative port position.
 * @param controller Index of the controller; identical to value returned
 *        from other routines like hv_inquire_physical.
 * @return Information about the controller.
 */
HV_MemoryControllerInfo hv_inquire_memory_controller(HV_Coord coord,
                                                     int controller);


/** A range of virtual memory. */
typedef struct
{
  HV_VirtAddr start;   /**< Starting address. */
  __hv64 size;         /**< Size in bytes. */
} HV_VirtAddrRange;

/** Returns information about a range of virtual memory.
 *
 * hv_inquire_virtual() returns one of the ranges of client
 * virtual addresses which are available to this client.
 *
 * The first range is retrieved by specifying an idx of 0, and
 * successive ranges are returned with subsequent idx values.  Ranges
 * are ordered by increasing start address (i.e., as idx increases,
 * so does start), do not overlap, and do not touch (i.e., the
 * available memory is described with the fewest possible ranges).
 *
 * If an out-of-range idx value is specified, the returned size will be zero.
 * A client can count the number of ranges by increasing idx until the
 * returned size is zero. There will always be at least one valid range.
 *
 * Some clients may well have various virtual addresses hardwired
 * into themselves; for instance, their instruction stream may
 * have been compiled expecting to live at a particular address.
 * Such clients should use this interface to verify they've been
 * given the virtual address space they expect, and issue a (potentially
 * fatal) warning message otherwise.
 *
 * Note that the returned size is a __hv64, not a __hv32, so it is
 * possible to express a single range spanning the entire 32-bit
 * address space.
 */
HV_VirtAddrRange hv_inquire_virtual(int idx);


/** A range of ASID values. */
typedef struct
{
#ifndef __BIG_ENDIAN__
  HV_ASID start;        /**< First ASID in the range. */
  unsigned int size;    /**< Number of ASIDs. Zero for an invalid range. */
#else
  unsigned int size;    /**< Number of ASIDs. Zero for an invalid range. */
  HV_ASID start;        /**< First ASID in the range. */
#endif
} HV_ASIDRange;

/** Returns information about a range of ASIDs.
 *
 * hv_inquire_asid() returns one of the ranges of address
 * space identifiers which are available to this client.
 *
 * The first range is retrieved by specifying an idx of 0, and
 * successive ranges are returned with subsequent idx values.  Ranges
 * are ordered by increasing start value (i.e., as idx increases,
 * so does start), do not overlap, and do not touch (i.e., the
 * available ASIDs are described with the fewest possible ranges).
 *
 * If an out-of-range idx value is specified, the returned size will be zero.
 * A client can count the number of ranges by increasing idx until the
 * returned size is zero. There will always be at least one valid range.
 */
HV_ASIDRange hv_inquire_asid(int idx);


/** Waits for at least the specified number of nanoseconds then returns.
 *
 * NOTE: this deprecated function currently assumes a 750 MHz clock,
 * and is thus not generally suitable for use.  New code should call
 * hv_sysconf(HV_SYSCONF_CPU_SPEED), compute a cycle count to wait for,
 * and delay by looping while checking the cycle counter SPR.
 *
 * @param nanosecs The number of nanoseconds to sleep.
 */
void hv_nanosleep(int nanosecs);


/** Reads a character from the console without blocking.
 *
 * @return A value from 0-255 indicates the value successfully read.
 * A negative value means no value was ready.
 */
int hv_console_read_if_ready(void);


/** Writes a character to the console, blocking if the console is busy.
 *
 *  This call cannot fail. If the console is broken for some reason,
 *  output will simply vanish.
 * @param byte Character to write.
 */
void hv_console_putc(int byte);


/** Writes a string to the console, blocking if the console is busy.
 * @param bytes Pointer to characters to write.
 * @param len Number of characters to write.
 * @return Number of characters written, or HV_EFAULT if the buffer is invalid.
 */
int hv_console_write(HV_VirtAddr bytes, int len);


/** Dispatch the next interrupt from the client downcall mechanism.
 *
 *  The hypervisor uses downcalls to notify the client of asynchronous
 *  events.  Some of these events are hypervisor-created (like incoming
 *  messages).  Some are regular interrupts which initially occur in
 *  the hypervisor, and are normally handled directly by the client;
 *  when these occur in a client's interrupt critical section, they must
 *  be delivered through the downcall mechanism.
 *
 *  A downcall is initially delivered to the client as an INTCTRL_CL
 *  interrupt, where CL is the client's PL.  Upon entry to the INTCTRL_CL
 *  vector, the client must immediately invoke the hv_downcall_dispatch
 *  service.  This service will not return; instead it will cause one of
 *  the client's actual downcall-handling interrupt vectors to be entered.
 *  The EX_CONTEXT registers in the client will be set so that when the
 *  client irets, it will return to the code which was interrupted by the
 *  INTCTRL_CL interrupt.
 *
 *  Under some circumstances, the firing of INTCTRL_CL can race with
 *  the lowering of a device interrupt.  In such a case, the
 *  hv_downcall_dispatch service may issue an iret instruction instead
 *  of entering one of the client's actual downcall-handling interrupt
 *  vectors.  This will return execution to the location that was
 *  interrupted by INTCTRL_CL.
 *
 *  Any saving of registers should be done by the actual handling
 *  vectors; no registers should be changed by the INTCTRL_CL handler.
 *  In particular, the client should not use a jal instruction to invoke
 *  the hv_downcall_dispatch service, as that would overwrite the client's
 *  lr register.  Note that the hv_downcall_dispatch service may overwrite
 *  one or more of the client's system save registers.
 *
 *  The client must not modify the INTCTRL_CL_STATUS SPR.  The hypervisor
 *  will set this register to cause a downcall to happen, and will clear
 *  it when no further downcalls are pending.
 *
 *  When a downcall vector is entered, the INTCTRL_CL interrupt will be
 *  masked.  When the client is done processing a downcall, and is ready
 *  to accept another, it must unmask this interrupt; if more downcalls
 *  are pending, this will cause the INTCTRL_CL vector to be reentered.
 *  Currently the following interrupt vectors can be entered through a
 *  downcall:
 *
 *  INT_MESSAGE_RCV_DWNCL   (hypervisor message available)
 *  INT_DEV_INTR_DWNCL      (device interrupt)
 *  INT_DMATLB_MISS_DWNCL   (DMA TLB miss)
 *  INT_SNITLB_MISS_DWNCL   (SNI TLB miss)
 *  INT_DMATLB_ACCESS_DWNCL (DMA TLB access violation)
 */
void hv_downcall_dispatch(void);

#endif /* !__ASSEMBLER__ */

/** We use actual interrupt vectors which never occur (they're only there
 *  to allow setting MPLs for related SPRs) for our downcall vectors.
 */
/** Message receive downcall interrupt vector */
#define INT_MESSAGE_RCV_DWNCL    INT_BOOT_ACCESS
/** DMA TLB miss downcall interrupt vector */
#define INT_DMATLB_MISS_DWNCL    INT_DMA_ASID
/** Static nework processor instruction TLB miss interrupt vector */
#define INT_SNITLB_MISS_DWNCL    INT_SNI_ASID
/** DMA TLB access violation downcall interrupt vector */
#define INT_DMATLB_ACCESS_DWNCL  INT_DMA_CPL
/** Device interrupt downcall interrupt vector */
#define INT_DEV_INTR_DWNCL       INT_WORLD_ACCESS

#ifndef __ASSEMBLER__

/** Requests the inode for a specific full pathname.
 *
 * Performs a lookup in the hypervisor filesystem for a given filename.
 * Multiple calls with the same filename will always return the same inode.
 * If there is no such filename, HV_ENOENT is returned.
 * A bad filename pointer may result in HV_EFAULT instead.
 *
 * @param filename Constant pointer to name of requested file
 * @return Inode of requested file
 */
int hv_fs_findfile(HV_VirtAddr filename);


/** Data returned from an fstat request.
 * Note that this structure should be no more than 40 bytes in size so
 * that it can always be returned completely in registers.
 */
typedef struct
{
  int size;             /**< Size of file (or HV_Errno on error) */
  unsigned int flags;   /**< Flags (see HV_FS_FSTAT_FLAGS) */
} HV_FS_StatInfo;

/** Bitmask flags for fstat request */
typedef enum
{
  HV_FS_ISDIR    = 0x0001   /**< Is the entry a directory? */
} HV_FS_FSTAT_FLAGS;

/** Get stat information on a given file inode.
 *
 * Return information on the file with the given inode.
 *
 * IF the HV_FS_ISDIR bit is set, the "file" is a directory.  Reading
 * it will return NUL-separated filenames (no directory part) relative
 * to the path to the inode of the directory "file".  These can be
 * appended to the path to the directory "file" after a forward slash
 * to create additional filenames.  Note that it is not required
 * that all valid paths be decomposable into valid parent directories;
 * a filesystem may validly have just a few files, none of which have
 * HV_FS_ISDIR set.  However, if clients may wish to enumerate the
 * files in the filesystem, it is recommended to include all the
 * appropriate parent directory "files" to give a consistent view.
 *
 * An invalid file inode will cause an HV_EBADF error to be returned.
 *
 * @param inode The inode number of the query
 * @return An HV_FS_StatInfo structure
 */
HV_FS_StatInfo hv_fs_fstat(int inode);


/** Read data from a specific hypervisor file.
 * On error, may return HV_EBADF for a bad inode or HV_EFAULT for a bad buf.
 * Reads near the end of the file will return fewer bytes than requested.
 * Reads at or beyond the end of a file will return zero.
 *
 * @param inode the hypervisor file to read
 * @param buf the buffer to read data into
 * @param length the number of bytes of data to read
 * @param offset the offset into the file to read the data from
 * @return number of bytes successfully read, or an HV_Errno code
 */
int hv_fs_pread(int inode, HV_VirtAddr buf, int length, int offset);


/** Read a 64-bit word from the specified physical address.
 * The address must be 8-byte aligned.
 * Specifying an invalid physical address will lead to client termination.
 * @param addr The physical address to read
 * @param access The PTE describing how to read the memory
 * @return The 64-bit value read from the given address
 */
unsigned long long hv_physaddr_read64(HV_PhysAddr addr, HV_PTE access);


/** Write a 64-bit word to the specified physical address.
 * The address must be 8-byte aligned.
 * Specifying an invalid physical address will lead to client termination.
 * @param addr The physical address to write
 * @param access The PTE that says how to write the memory
 * @param val The 64-bit value to write to the given address
 */
void hv_physaddr_write64(HV_PhysAddr addr, HV_PTE access,
                         unsigned long long val);


/** Get the value of the command-line for the supervisor, if any.
 * This will not include the filename of the booted supervisor, but may
 * include configured-in boot arguments or the hv_restart() arguments.
 * If the buffer is not long enough the hypervisor will NUL the first
 * character of the buffer but not write any other data.
 * @param buf The virtual address to write the command-line string to.
 * @param length The length of buf, in characters.
 * @return The actual length of the command line, including the trailing NUL
 *         (may be larger than "length").
 */
int hv_get_command_line(HV_VirtAddr buf, int length);


/** Set a new value for the command-line for the supervisor, which will
 *  be returned from subsequent invocations of hv_get_command_line() on
 *  this tile.
 * @param buf The virtual address to read the command-line string from.
 * @param length The length of buf, in characters; must be no more than
 *        HV_COMMAND_LINE_LEN.
 * @return Zero if successful, or a hypervisor error code.
 */
HV_Errno hv_set_command_line(HV_VirtAddr buf, int length);

/** Maximum size of a command line passed to hv_set_command_line(); note
 *  that a line returned from hv_get_command_line() could be larger than
 *  this.*/
#define HV_COMMAND_LINE_LEN  256

/** Tell the hypervisor how to cache non-priority pages
 * (its own as well as pages explicitly represented in page tables).
 * Normally these will be represented as red/black pages, but
 * when the supervisor starts to allocate "priority" pages in the PTE
 * the hypervisor will need to start marking those pages as (e.g.) "red"
 * and non-priority pages as either "black" (if they cache-alias
 * with the existing priority pages) or "red/black" (if they don't).
 * The bitmask provides information on which parts of the cache
 * have been used for pinned pages so far on this tile; if (1 << N)
 * appears in the bitmask, that indicates that a 4KB region of the
 * cache starting at (N * 4KB) is in use by a "priority" page.
 * The portion of cache used by a particular page can be computed
 * by taking the page's PA, modulo CHIP_L2_CACHE_SIZE(), and setting
 * all the "4KB" bits corresponding to the actual page size.
 * @param bitmask A bitmap of priority page set values
 */
void hv_set_caching(unsigned long bitmask);


/** Zero out a specified number of pages.
 * The va and size must both be multiples of 4096.
 * Caches are bypassed and memory is directly set to zero.
 * This API is implemented only in the magic hypervisor and is intended
 * to provide a performance boost to the minimal supervisor by
 * giving it a fast way to zero memory pages when allocating them.
 * @param va Virtual address where the page has been mapped
 * @param size Number of bytes (must be a page size multiple)
 */
void hv_bzero_page(HV_VirtAddr va, unsigned int size);


/** State object for the hypervisor messaging subsystem. */
typedef struct
{
#if CHIP_VA_WIDTH() > 32
  __hv64 opaque[2]; /**< No user-serviceable parts inside */
#else
  __hv32 opaque[2]; /**< No user-serviceable parts inside */
#endif
}
HV_MsgState;

/** Register to receive incoming messages.
 *
 *  This routine configures the current tile so that it can receive
 *  incoming messages.  It must be called before the client can receive
 *  messages with the hv_receive_message routine, and must be called on
 *  each tile which will receive messages.
 *
 *  msgstate is the virtual address of a state object of type HV_MsgState.
 *  Once the state is registered, the client must not read or write the
 *  state object; doing so will cause undefined results.
 *
 *  If this routine is called with msgstate set to 0, the client's message
 *  state will be freed and it will no longer be able to receive messages.
 *  Note that this may cause the loss of any as-yet-undelivered messages
 *  for the client.
 *
 *  If another client attempts to send a message to a client which has
 *  not yet called hv_register_message_state, or which has freed its
 *  message state, the message will not be delivered, as if the client
 *  had insufficient buffering.
 *
 *  This routine returns HV_OK if the registration was successful, and
 *  HV_EINVAL if the supplied state object is unsuitable.  Note that some
 *  errors may not be detected during this routine, but might be detected
 *  during a subsequent message delivery.
 * @param msgstate State object.
 **/
HV_Errno hv_register_message_state(HV_MsgState* msgstate);

/** Possible message recipient states. */
typedef enum
{
  HV_TO_BE_SENT,    /**< Not sent (not attempted, or recipient not ready) */
  HV_SENT,          /**< Successfully sent */
  HV_BAD_RECIP      /**< Bad recipient coordinates (permanent error) */
} HV_Recip_State;

/** Message recipient. */
typedef struct
{
#ifndef __BIG_ENDIAN__
  /** X coordinate, relative to supervisor's top-left coordinate */
  unsigned int x:11;

  /** Y coordinate, relative to supervisor's top-left coordinate */
  unsigned int y:11;

  /** Status of this recipient */
  HV_Recip_State state:10;
#else //__BIG_ENDIAN__
  HV_Recip_State state:10;
  unsigned int y:11;
  unsigned int x:11;
#endif
} HV_Recipient;

/** Send a message to a set of recipients.
 *
 *  This routine sends a message to a set of recipients.
 *
 *  recips is an array of HV_Recipient structures.  Each specifies a tile,
 *  and a message state; initially, it is expected that the state will
 *  be set to HV_TO_BE_SENT.  nrecip specifies the number of recipients
 *  in the recips array.
 *
 *  For each recipient whose state is HV_TO_BE_SENT, the hypervisor attempts
 *  to send that tile the specified message.  In order to successfully
 *  receive the message, the receiver must be a valid tile to which the
 *  sender has access, must not be the sending tile itself, and must have
 *  sufficient free buffer space.  (The hypervisor guarantees that each
 *  tile which has called hv_register_message_state() will be able to
 *  buffer one message from every other tile which can legally send to it;
 *  more space may be provided but is not guaranteed.)  If an invalid tile
 *  is specified, the recipient's state is set to HV_BAD_RECIP; this is a
 *  permanent delivery error.  If the message is successfully delivered
 *  to the recipient's buffer, the recipient's state is set to HV_SENT.
 *  Otherwise, the recipient's state is unchanged.  Message delivery is
 *  synchronous; all attempts to send messages are completed before this
 *  routine returns.
 *
 *  If no permanent delivery errors were encountered, the routine returns
 *  the number of messages successfully sent: that is, the number of
 *  recipients whose states changed from HV_TO_BE_SENT to HV_SENT during
 *  this operation.  If any permanent delivery errors were encountered,
 *  the routine returns HV_ERECIP.  In the event of permanent delivery
 *  errors, it may be the case that delivery was not attempted to all
 *  recipients; if any messages were successfully delivered, however,
 *  recipients' state values will be updated appropriately.
 *
 *  It is explicitly legal to specify a recipient structure whose state
 *  is not HV_TO_BE_SENT; such a recipient is ignored.  One suggested way
 *  of using hv_send_message to send a message to multiple tiles is to set
 *  up a list of recipients, and then call the routine repeatedly with the
 *  same list, each time accumulating the number of messages successfully
 *  sent, until all messages are sent, a permanent error is encountered,
 *  or the desired number of attempts have been made.  When used in this
 *  way, the routine will deliver each message no more than once to each
 *  recipient.
 *
 *  Note that a message being successfully delivered to the recipient's
 *  buffer space does not guarantee that it is received by the recipient,
 *  either immediately or at any time in the future; the recipient might
 *  never call hv_receive_message, or could register a different state
 *  buffer, losing the message.
 *
 *  Specifying the same recipient more than once in the recipient list
 *  is an error, which will not result in an error return but which may
 *  or may not result in more than one message being delivered to the
 *  recipient tile.
 *
 *  buf and buflen specify the message to be sent.  buf is a virtual address
 *  which must be currently mapped in the client's page table; if not, the
 *  routine returns HV_EFAULT.  buflen must be greater than zero and less
 *  than or equal to HV_MAX_MESSAGE_SIZE, and nrecip must be less than the
 *  number of tiles to which the sender has access; if not, the routine
 *  returns HV_EINVAL.
 * @param recips List of recipients.
 * @param nrecip Number of recipients.
 * @param buf Address of message data.
 * @param buflen Length of message data.
 **/
int hv_send_message(HV_Recipient *recips, int nrecip,
                    HV_VirtAddr buf, int buflen);

/** Maximum hypervisor message size, in bytes */
#define HV_MAX_MESSAGE_SIZE 28


/** Return value from hv_receive_message() */
typedef struct
{
  int msglen;     /**< Message length in bytes, or an error code */
  __hv32 source;  /**< Code identifying message sender (HV_MSG_xxx) */
} HV_RcvMsgInfo;

#define HV_MSG_TILE 0x0         /**< Message source is another tile */
#define HV_MSG_INTR 0x1         /**< Message source is a driver interrupt */

/** Receive a message.
 *
 * This routine retrieves a message from the client's incoming message
 * buffer.
 *
 * Multiple messages sent from a particular sending tile to a particular
 * receiving tile are received in the order that they were sent; however,
 * no ordering is guaranteed between messages sent by different tiles.
 *
 * Whenever the a client's message buffer is empty, the first message
 * subsequently received will cause the client's MESSAGE_RCV_DWNCL
 * interrupt vector to be invoked through the interrupt downcall mechanism
 * (see the description of the hv_downcall_dispatch() routine for details
 * on downcalls).
 *
 * Another message-available downcall will not occur until a call to
 * this routine is made when the message buffer is empty, and a message
 * subsequently arrives.  Note that such a downcall could occur while
 * this routine is executing.  If the calling code does not wish this
 * to happen, it is recommended that this routine be called with the
 * INTCTRL_1 interrupt masked, or inside an interrupt critical section.
 *
 * msgstate is the value previously passed to hv_register_message_state().
 * buf is the virtual address of the buffer into which the message will
 * be written; buflen is the length of the buffer.
 *
 * This routine returns an HV_RcvMsgInfo structure.  The msglen member
 * of that structure is the length of the message received, zero if no
 * message is available, or HV_E2BIG if the message is too large for the
 * specified buffer.  If the message is too large, it is not consumed,
 * and may be retrieved by a subsequent call to this routine specifying
 * a sufficiently large buffer.  A buffer which is HV_MAX_MESSAGE_SIZE
 * bytes long is guaranteed to be able to receive any possible message.
 *
 * The source member of the HV_RcvMsgInfo structure describes the sender
 * of the message.  For messages sent by another client tile via an
 * hv_send_message() call, this value is HV_MSG_TILE; for messages sent
 * as a result of a device interrupt, this value is HV_MSG_INTR.
 */

HV_RcvMsgInfo hv_receive_message(HV_MsgState msgstate, HV_VirtAddr buf,
                                 int buflen);


/** Start remaining tiles owned by this supervisor.  Initially, only one tile
 *  executes the client program; after it calls this service, the other tiles
 *  are started.  This allows the initial tile to do one-time configuration
 *  of shared data structures without having to lock them against simultaneous
 *  access.
 */
void hv_start_all_tiles(void);


/** Open a hypervisor device.
 *
 *  This service initializes an I/O device and its hypervisor driver software,
 *  and makes it available for use.  The open operation is per-device per-chip;
 *  once it has been performed, the device handle returned may be used in other
 *  device services calls made by any tile.
 *
 * @param name Name of the device.  A base device name is just a text string
 *        (say, "pcie").  If there is more than one instance of a device, the
 *        base name is followed by a slash and a device number (say, "pcie/0").
 *        Some devices may support further structure beneath those components;
 *        most notably, devices which require control operations do so by
 *        supporting reads and/or writes to a control device whose name
 *        includes a trailing "/ctl" (say, "pcie/0/ctl").
 * @param flags Flags (HV_DEV_xxx).
 * @return A positive integer device handle, or a negative error code.
 */
int hv_dev_open(HV_VirtAddr name, __hv32 flags);


/** Close a hypervisor device.
 *
 *  This service uninitializes an I/O device and its hypervisor driver
 *  software, and makes it unavailable for use.  The close operation is
 *  per-device per-chip; once it has been performed, the device is no longer
 *  available.  Normally there is no need to ever call the close service.
 *
 * @param devhdl Device handle of the device to be closed.
 * @return Zero if the close is successful, otherwise, a negative error code.
 */
int hv_dev_close(int devhdl);


/** Read data from a hypervisor device synchronously.
 *
 *  This service transfers data from a hypervisor device to a memory buffer.
 *  When the service returns, the data has been written from the memory buffer,
 *  and the buffer will not be further modified by the driver.
 *
 *  No ordering is guaranteed between requests issued from different tiles.
 *
 *  Devices may choose to support both the synchronous and asynchronous read
 *  operations, only one of them, or neither of them.
 *
 * @param devhdl Device handle of the device to be read from.
 * @param flags Flags (HV_DEV_xxx).
 * @param va Virtual address of the target data buffer.  This buffer must
 *        be mapped in the currently installed page table; if not, HV_EFAULT
 *        may be returned.
 * @param len Number of bytes to be transferred.
 * @param offset Driver-dependent offset.  For a random-access device, this is
 *        often a byte offset from the beginning of the device; in other cases,
 *        like on a control device, it may have a different meaning.
 * @return A non-negative value if the read was at least partially successful;
 *         otherwise, a negative error code.  The precise interpretation of
 *         the return value is driver-dependent, but many drivers will return
 *         the number of bytes successfully transferred.
 */
int hv_dev_pread(int devhdl, __hv32 flags, HV_VirtAddr va, __hv32 len,
                 __hv64 offset);

#define HV_DEV_NB_EMPTY     0x1   /**< Don't block when no bytes of data can
                                       be transferred. */
#define HV_DEV_NB_PARTIAL   0x2   /**< Don't block when some bytes, but not all
                                       of the requested bytes, can be
                                       transferred. */
#define HV_DEV_NOCACHE      0x4   /**< The caller warrants that none of the
                                       cache lines which might contain data
                                       from the requested buffer are valid.
                                       Useful with asynchronous operations
                                       only. */

#define HV_DEV_ALLFLAGS     (HV_DEV_NB_EMPTY | HV_DEV_NB_PARTIAL | \
                             HV_DEV_NOCACHE)   /**< All HV_DEV_xxx flags */

/** Write data to a hypervisor device synchronously.
 *
 *  This service transfers data from a memory buffer to a hypervisor device.
 *  When the service returns, the data has been read from the memory buffer,
 *  and the buffer may be overwritten by the client; the data may not
 *  necessarily have been conveyed to the actual hardware I/O interface.
 *
 *  No ordering is guaranteed between requests issued from different tiles.
 *
 *  Devices may choose to support both the synchronous and asynchronous write
 *  operations, only one of them, or neither of them.
 *
 * @param devhdl Device handle of the device to be written to.
 * @param flags Flags (HV_DEV_xxx).
 * @param va Virtual address of the source data buffer.  This buffer must
 *        be mapped in the currently installed page table; if not, HV_EFAULT
 *        may be returned.
 * @param len Number of bytes to be transferred.
 * @param offset Driver-dependent offset.  For a random-access device, this is
 *        often a byte offset from the beginning of the device; in other cases,
 *        like on a control device, it may have a different meaning.
 * @return A non-negative value if the write was at least partially successful;
 *         otherwise, a negative error code.  The precise interpretation of
 *         the return value is driver-dependent, but many drivers will return
 *         the number of bytes successfully transferred.
 */
int hv_dev_pwrite(int devhdl, __hv32 flags, HV_VirtAddr va, __hv32 len,
                  __hv64 offset);


/** Interrupt arguments, used in the asynchronous I/O interfaces. */
#if CHIP_VA_WIDTH() > 32
typedef __hv64 HV_IntArg;
#else
typedef __hv32 HV_IntArg;
#endif

/** Interrupt messages are delivered via the mechanism as normal messages,
 *  but have a message source of HV_DEV_INTR.  The message is formatted
 *  as an HV_IntrMsg structure.
 */

typedef struct
{
  HV_IntArg intarg;  /**< Interrupt argument, passed to the poll/preada/pwritea
                          services */
  HV_IntArg intdata; /**< Interrupt-specific interrupt data */
} HV_IntrMsg;

/** Request an interrupt message when a device condition is satisfied.
 *
 *  This service requests that an interrupt message be delivered to the
 *  requesting tile when a device becomes readable or writable, or when any
 *  data queued to the device via previous write operations from this tile
 *  has been actually sent out on the hardware I/O interface.  Devices may
 *  choose to support any, all, or none of the available conditions.
 *
 *  If multiple conditions are specified, only one message will be
 *  delivered.  If the event mask delivered to that interrupt handler
 *  indicates that some of the conditions have not yet occurred, the
 *  client must issue another poll() call if it wishes to wait for those
 *  conditions.
 *
 *  Only one poll may be outstanding per device handle per tile.  If more than
 *  one tile is polling on the same device and condition, they will all be
 *  notified when it happens.  Because of this, clients may not assume that
 *  the condition signaled is necessarily still true when they request a
 *  subsequent service; for instance, the readable data which caused the
 *  poll call to interrupt may have been read by another tile in the interim.
 *
 *  The notification interrupt message could come directly, or via the
 *  downcall (intctrl1) method, depending on what the tile is doing
 *  when the condition is satisfied.  Note that it is possible for the
 *  requested interrupt to be delivered after this service is called but
 *  before it returns.
 *
 * @param devhdl Device handle of the device to be polled.
 * @param events Flags denoting the events which will cause the interrupt to
 *        be delivered (HV_DEVPOLL_xxx).
 * @param intarg Value which will be delivered as the intarg member of the
 *        eventual interrupt message; the intdata member will be set to a
 *        mask of HV_DEVPOLL_xxx values indicating which conditions have been
 *        satisifed.
 * @return Zero if the interrupt was successfully scheduled; otherwise, a
 *         negative error code.
 */
int hv_dev_poll(int devhdl, __hv32 events, HV_IntArg intarg);

#define HV_DEVPOLL_READ     0x1   /**< Test device for readability */
#define HV_DEVPOLL_WRITE    0x2   /**< Test device for writability */
#define HV_DEVPOLL_FLUSH    0x4   /**< Test device for output drained */


/** Cancel a request for an interrupt when a device event occurs.
 *
 *  This service requests that no interrupt be delivered when the events
 *  noted in the last-issued poll() call happen.  Once this service returns,
 *  the interrupt has been canceled; however, it is possible for the interrupt
 *  to be delivered after this service is called but before it returns.
 *
 * @param devhdl Device handle of the device on which to cancel polling.
 * @return Zero if the poll was successfully canceled; otherwise, a negative
 *         error code.
 */
int hv_dev_poll_cancel(int devhdl);


/** Scatter-gather list for preada/pwritea calls. */
typedef struct
#if CHIP_VA_WIDTH() <= 32
__attribute__ ((packed, aligned(4)))
#endif
{
  HV_PhysAddr pa;  /**< Client physical address of the buffer segment. */
  HV_PTE pte;      /**< Page table entry describing the caching and location
                        override characteristics of the buffer segment.  Some
                        drivers ignore this element and will require that
                        the NOCACHE flag be set on their requests. */
  __hv32 len;      /**< Length of the buffer segment. */
} HV_SGL;

#define HV_SGL_MAXLEN 16  /**< Maximum number of entries in a scatter-gather
                               list */

/** Read data from a hypervisor device asynchronously.
 *
 *  This service transfers data from a hypervisor device to a memory buffer.
 *  When the service returns, the read has been scheduled.  When the read
 *  completes, an interrupt message will be delivered, and the buffer will
 *  not be further modified by the driver.
 *
 *  The number of possible outstanding asynchronous requests is defined by
 *  each driver, but it is recommended that it be at least two requests
 *  per tile per device.
 *
 *  No ordering is guaranteed between synchronous and asynchronous requests,
 *  even those issued on the same tile.
 *
 *  The completion interrupt message could come directly, or via the downcall
 *  (intctrl1) method, depending on what the tile is doing when the read
 *  completes.  Interrupts do not coalesce; one is delivered for each
 *  asynchronous I/O request.  Note that it is possible for the requested
 *  interrupt to be delivered after this service is called but before it
 *  returns.
 *
 *  Devices may choose to support both the synchronous and asynchronous read
 *  operations, only one of them, or neither of them.
 *
 * @param devhdl Device handle of the device to be read from.
 * @param flags Flags (HV_DEV_xxx).
 * @param sgl_len Number of elements in the scatter-gather list.
 * @param sgl Scatter-gather list describing the memory to which data will be
 *        written.
 * @param offset Driver-dependent offset.  For a random-access device, this is
 *        often a byte offset from the beginning of the device; in other cases,
 *        like on a control device, it may have a different meaning.
 * @param intarg Value which will be delivered as the intarg member of the
 *        eventual interrupt message; the intdata member will be set to the
 *        normal return value from the read request.
 * @return Zero if the read was successfully scheduled; otherwise, a negative
 *         error code.  Note that some drivers may choose to pre-validate
 *         their arguments, and may thus detect certain device error
 *         conditions at this time rather than when the completion notification
 *         occurs, but this is not required.
 */
int hv_dev_preada(int devhdl, __hv32 flags, __hv32 sgl_len,
                  HV_SGL sgl[/* sgl_len */], __hv64 offset, HV_IntArg intarg);


/** Write data to a hypervisor device asynchronously.
 *
 *  This service transfers data from a memory buffer to a hypervisor
 *  device.  When the service returns, the write has been scheduled.
 *  When the write completes, an interrupt message will be delivered,
 *  and the buffer may be overwritten by the client; the data may not
 *  necessarily have been conveyed to the actual hardware I/O interface.
 *
 *  The number of possible outstanding asynchronous requests is defined by
 *  each driver, but it is recommended that it be at least two requests
 *  per tile per device.
 *
 *  No ordering is guaranteed between synchronous and asynchronous requests,
 *  even those issued on the same tile.
 *
 *  The completion interrupt message could come directly, or via the downcall
 *  (intctrl1) method, depending on what the tile is doing when the read
 *  completes.  Interrupts do not coalesce; one is delivered for each
 *  asynchronous I/O request.  Note that it is possible for the requested
 *  interrupt to be delivered after this service is called but before it
 *  returns.
 *
 *  Devices may choose to support both the synchronous and asynchronous write
 *  operations, only one of them, or neither of them.
 *
 * @param devhdl Device handle of the device to be read from.
 * @param flags Flags (HV_DEV_xxx).
 * @param sgl_len Number of elements in the scatter-gather list.
 * @param sgl Scatter-gather list describing the memory from which data will be
 *        read.
 * @param offset Driver-dependent offset.  For a random-access device, this is
 *        often a byte offset from the beginning of the device; in other cases,
 *        like on a control device, it may have a different meaning.
 * @param intarg Value which will be delivered as the intarg member of the
 *        eventual interrupt message; the intdata member will be set to the
 *        normal return value from the write request.
 * @return Zero if the write was successfully scheduled; otherwise, a negative
 *         error code.  Note that some drivers may choose to pre-validate
 *         their arguments, and may thus detect certain device error
 *         conditions at this time rather than when the completion notification
 *         occurs, but this is not required.
 */
int hv_dev_pwritea(int devhdl, __hv32 flags, __hv32 sgl_len,
                   HV_SGL sgl[/* sgl_len */], __hv64 offset, HV_IntArg intarg);


/** Define a pair of tile and ASID to identify a user process context. */
typedef struct
{
  /** X coordinate, relative to supervisor's top-left coordinate */
  unsigned int x:11;

  /** Y coordinate, relative to supervisor's top-left coordinate */
  unsigned int y:11;

  /** ASID of the process on this x,y tile */
  HV_ASID asid:10;
} HV_Remote_ASID;

/** Flush cache and/or TLB state on remote tiles.
 *
 * @param cache_pa Client physical address to flush from cache (ignored if
 *        the length encoded in cache_control is zero, or if
 *        HV_FLUSH_EVICT_L2 is set, or if cache_cpumask is NULL).
 * @param cache_control This argument allows you to specify a length of
 *        physical address space to flush (maximum HV_FLUSH_MAX_CACHE_LEN).
 *        You can "or" in HV_FLUSH_EVICT_L2 to flush the whole L2 cache.
 *        You can "or" in HV_FLUSH_EVICT_L1I to flush the whole L1I cache.
 *        HV_FLUSH_ALL flushes all caches.
 * @param cache_cpumask Bitmask (in row-major order, supervisor-relative) of
 *        tile indices to perform cache flush on.  The low bit of the first
 *        word corresponds to the tile at the upper left-hand corner of the
 *        supervisor's rectangle.  If passed as a NULL pointer, equivalent
 *        to an empty bitmask.  On chips which support hash-for-home caching,
 *        if passed as -1, equivalent to a mask containing tiles which could
 *        be doing hash-for-home caching.
 * @param tlb_va Virtual address to flush from TLB (ignored if
 *        tlb_length is zero or tlb_cpumask is NULL).
 * @param tlb_length Number of bytes of data to flush from the TLB.
 * @param tlb_pgsize Page size to use for TLB flushes.
 *        tlb_va and tlb_length need not be aligned to this size.
 * @param tlb_cpumask Bitmask for tlb flush, like cache_cpumask.
 *        If passed as a NULL pointer, equivalent to an empty bitmask.
 * @param asids Pointer to an HV_Remote_ASID array of tile/ASID pairs to flush.
 * @param asidcount Number of HV_Remote_ASID entries in asids[].
 * @return Zero for success, or else HV_EINVAL or HV_EFAULT for errors that
 *        are detected while parsing the arguments.
 */
int hv_flush_remote(HV_PhysAddr cache_pa, unsigned long cache_control,
                    unsigned long* cache_cpumask,
                    HV_VirtAddr tlb_va, unsigned long tlb_length,
                    unsigned long tlb_pgsize, unsigned long* tlb_cpumask,
                    HV_Remote_ASID* asids, int asidcount);

/** Include in cache_control to ensure a flush of the entire L2. */
#define HV_FLUSH_EVICT_L2 (1UL << 31)

/** Include in cache_control to ensure a flush of the entire L1I. */
#define HV_FLUSH_EVICT_L1I (1UL << 30)

/** Maximum legal size to use for the "length" component of cache_control. */
#define HV_FLUSH_MAX_CACHE_LEN ((1UL << 30) - 1)

/** Use for cache_control to ensure a flush of all caches. */
#define HV_FLUSH_ALL -1UL

#else   /* __ASSEMBLER__ */

/** Include in cache_control to ensure a flush of the entire L2. */
#define HV_FLUSH_EVICT_L2 (1 << 31)

/** Include in cache_control to ensure a flush of the entire L1I. */
#define HV_FLUSH_EVICT_L1I (1 << 30)

/** Maximum legal size to use for the "length" component of cache_control. */
#define HV_FLUSH_MAX_CACHE_LEN ((1 << 30) - 1)

/** Use for cache_control to ensure a flush of all caches. */
#define HV_FLUSH_ALL -1

#endif  /* __ASSEMBLER__ */

#ifndef __ASSEMBLER__

/** Return a 64-bit value corresponding to the PTE if needed */
#define hv_pte_val(pte) ((pte).val)

/** Cast a 64-bit value to an HV_PTE */
#define hv_pte(val) ((HV_PTE) { val })

#endif  /* !__ASSEMBLER__ */


/** Bits in the size of an HV_PTE */
#define HV_LOG2_PTE_SIZE 3

/** Size of an HV_PTE */
#define HV_PTE_SIZE (1 << HV_LOG2_PTE_SIZE)


/* Bits in HV_PTE's low word. */
#define HV_PTE_INDEX_PRESENT          0  /**< PTE is valid */
#define HV_PTE_INDEX_MIGRATING        1  /**< Page is migrating */
#define HV_PTE_INDEX_CLIENT0          2  /**< Page client state 0 */
#define HV_PTE_INDEX_CLIENT1          3  /**< Page client state 1 */
#define HV_PTE_INDEX_NC               4  /**< L1$/L2$ incoherent with L3$ */
#define HV_PTE_INDEX_NO_ALLOC_L1      5  /**< Page is uncached in local L1$ */
#define HV_PTE_INDEX_NO_ALLOC_L2      6  /**< Page is uncached in local L2$ */
#define HV_PTE_INDEX_CACHED_PRIORITY  7  /**< Page is priority cached */
#define HV_PTE_INDEX_PAGE             8  /**< PTE describes a page */
#define HV_PTE_INDEX_GLOBAL           9  /**< Page is global */
#define HV_PTE_INDEX_USER            10  /**< Page is user-accessible */
#define HV_PTE_INDEX_ACCESSED        11  /**< Page has been accessed */
#define HV_PTE_INDEX_DIRTY           12  /**< Page has been written */
                                         /*   Bits 13-14 are reserved for
                                              future use. */
#define HV_PTE_INDEX_SUPER           15  /**< Pages ganged together for TLB */
#define HV_PTE_INDEX_MODE            16  /**< Page mode; see HV_PTE_MODE_xxx */
#define HV_PTE_MODE_BITS              3  /**< Number of bits in mode */
#define HV_PTE_INDEX_CLIENT2         19  /**< Page client state 2 */
#define HV_PTE_INDEX_LOTAR           20  /**< Page's LOTAR; must be high bits
                                              of word */
#define HV_PTE_LOTAR_BITS            12  /**< Number of bits in a LOTAR */

/* Bits in HV_PTE's high word. */
#define HV_PTE_INDEX_READABLE        32  /**< Page is readable */
#define HV_PTE_INDEX_WRITABLE        33  /**< Page is writable */
#define HV_PTE_INDEX_EXECUTABLE      34  /**< Page is executable */
#define HV_PTE_INDEX_PTFN            35  /**< Page's PTFN; must be high bits
                                              of word */
#define HV_PTE_PTFN_BITS             29  /**< Number of bits in a PTFN */

/*
 * Legal values for the PTE's mode field
 */
/** Data is not resident in any caches; loads and stores access memory
 *  directly.
 */
#define HV_PTE_MODE_UNCACHED          1

/** Data is resident in the tile's local L1 and/or L2 caches; if a load
 *  or store misses there, it goes to memory.
 *
 *  The copy in the local L1$/L2$ is not invalidated when the copy in
 *  memory is changed.
 */
#define HV_PTE_MODE_CACHE_NO_L3       2

/** Data is resident in the tile's local L1 and/or L2 caches.  If a load
 *  or store misses there, it goes to an L3 cache in a designated tile;
 *  if it misses there, it goes to memory.
 *
 *  If the NC bit is not set, the copy in the local L1$/L2$ is invalidated
 *  when the copy in the remote L3$ is changed.  Otherwise, such
 *  invalidation will not occur.
 *
 *  Chips for which CHIP_HAS_COHERENT_LOCAL_CACHE() is 0 do not support
 *  invalidation from an L3$ to another tile's L1$/L2$.  If the NC bit is
 *  clear on such a chip, no copy is kept in the local L1$/L2$ in this mode.
 */
#define HV_PTE_MODE_CACHE_TILE_L3     3

/** Data is resident in the tile's local L1 and/or L2 caches.  If a load
 *  or store misses there, it goes to an L3 cache in one of a set of
 *  designated tiles; if it misses there, it goes to memory.  Which tile
 *  is chosen from the set depends upon a hash function applied to the
 *  physical address.  This mode is not supported on chips for which
 *  CHIP_HAS_CBOX_HOME_MAP() is 0.
 *
 *  If the NC bit is not set, the copy in the local L1$/L2$ is invalidated
 *  when the copy in the remote L3$ is changed.  Otherwise, such
 *  invalidation will not occur.
 *
 *  Chips for which CHIP_HAS_COHERENT_LOCAL_CACHE() is 0 do not support
 *  invalidation from an L3$ to another tile's L1$/L2$.  If the NC bit is
 *  clear on such a chip, no copy is kept in the local L1$/L2$ in this mode.
 */
#define HV_PTE_MODE_CACHE_HASH_L3     4

/** Data is not resident in memory; accesses are instead made to an I/O
 *  device, whose tile coordinates are given by the PTE's LOTAR field.
 *  This mode is only supported on chips for which CHIP_HAS_MMIO() is 1.
 *  The EXECUTABLE bit may not be set in an MMIO PTE.
 */
#define HV_PTE_MODE_MMIO              5


/* C wants 1ULL so it is typed as __hv64, but the assembler needs just numbers.
 * The assembler can't handle shifts greater than 31, but treats them
 * as shifts mod 32, so assembler code must be aware of which word
 * the bit belongs in when using these macros.
 */
#ifdef __ASSEMBLER__
#define __HV_PTE_ONE 1        /**< One, for assembler */
#else
#define __HV_PTE_ONE 1ULL     /**< One, for C */
#endif

/** Is this PTE present?
 *
 * If this bit is set, this PTE represents a valid translation or level-2
 * page table pointer.  Otherwise, the page table does not contain a
 * translation for the subject virtual pages.
 *
 * If this bit is not set, the other bits in the PTE are not
 * interpreted by the hypervisor, and may contain any value.
 */
#define HV_PTE_PRESENT               (__HV_PTE_ONE << HV_PTE_INDEX_PRESENT)

/** Does this PTE map a page?
 *
 * If this bit is set in a level-0 page table, the entry should be
 * interpreted as a level-2 page table entry mapping a jumbo page.
 *
 * If this bit is set in a level-1 page table, the entry should be
 * interpreted as a level-2 page table entry mapping a large page.
 *
 * This bit should not be modified by the client while PRESENT is set, as
 * doing so may race with the hypervisor's update of ACCESSED and DIRTY bits.
 *
 * In a level-2 page table, this bit is ignored and must be zero.
 */
#define HV_PTE_PAGE                  (__HV_PTE_ONE << HV_PTE_INDEX_PAGE)

/** Does this PTE implicitly reference multiple pages?
 *
 * If this bit is set in the page table (either in the level-2 page table,
 * or in a higher level page table in conjunction with the PAGE bit)
 * then the PTE specifies a range of contiguous pages, not a single page.
 * The hv_set_pte_super_shift() allows you to specify the count for
 * each level of the page table.
 *
 * Note: this bit is not supported on TILEPro systems.
 */
#define HV_PTE_SUPER                 (__HV_PTE_ONE << HV_PTE_INDEX_SUPER)

/** Is this a global (non-ASID) mapping?
 *
 * If this bit is set, the translations established by this PTE will
 * not be flushed from the TLB by the hv_flush_asid() service; they
 * will be flushed by the hv_flush_page() or hv_flush_pages() services.
 *
 * Setting this bit for translations which are identical in all page
 * tables (for instance, code and data belonging to a client OS) can
 * be very beneficial, as it will reduce the number of TLB misses.
 * Note that, while it is not an error which will be detected by the
 * hypervisor, it is an extremely bad idea to set this bit for
 * translations which are _not_ identical in all page tables.
 *
 * This bit should not be modified by the client while PRESENT is set, as
 * doing so may race with the hypervisor's update of ACCESSED and DIRTY bits.
 *
 * This bit is ignored in level-1 PTEs unless the Page bit is set.
 */
#define HV_PTE_GLOBAL                (__HV_PTE_ONE << HV_PTE_INDEX_GLOBAL)

/** Is this mapping accessible to users?
 *
 * If this bit is set, code running at any PL will be permitted to
 * access the virtual addresses mapped by this PTE.  Otherwise, only
 * code running at PL 1 or above will be allowed to do so.
 *
 * This bit should not be modified by the client while PRESENT is set, as
 * doing so may race with the hypervisor's update of ACCESSED and DIRTY bits.
 *
 * This bit is ignored in level-1 PTEs unless the Page bit is set.
 */
#define HV_PTE_USER                  (__HV_PTE_ONE << HV_PTE_INDEX_USER)

/** Has this mapping been accessed?
 *
 * This bit is set by the hypervisor when the memory described by the
 * translation is accessed for the first time.  It is never cleared by
 * the hypervisor, but may be cleared by the client.  After the bit
 * has been cleared, subsequent references are not guaranteed to set
 * it again until the translation has been flushed from the TLB.
 *
 * This bit is ignored in level-1 PTEs unless the Page bit is set.
 */
#define HV_PTE_ACCESSED              (__HV_PTE_ONE << HV_PTE_INDEX_ACCESSED)

/** Is this mapping dirty?
 *
 * This bit is set by the hypervisor when the memory described by the
 * translation is written for the first time.  It is never cleared by
 * the hypervisor, but may be cleared by the client.  After the bit
 * has been cleared, subsequent references are not guaranteed to set
 * it again until the translation has been flushed from the TLB.
 *
 * This bit is ignored in level-1 PTEs unless the Page bit is set.
 */
#define HV_PTE_DIRTY                 (__HV_PTE_ONE << HV_PTE_INDEX_DIRTY)

/** Migrating bit in PTE.
 *
 * This bit is guaranteed not to be inspected or modified by the
 * hypervisor.  The name is indicative of the suggested use by the client
 * to tag pages whose L3 cache is being migrated from one cpu to another.
 */
#define HV_PTE_MIGRATING             (__HV_PTE_ONE << HV_PTE_INDEX_MIGRATING)

/** Client-private bit in PTE.
 *
 * This bit is guaranteed not to be inspected or modified by the
 * hypervisor.
 */
#define HV_PTE_CLIENT0               (__HV_PTE_ONE << HV_PTE_INDEX_CLIENT0)

/** Client-private bit in PTE.
 *
 * This bit is guaranteed not to be inspected or modified by the
 * hypervisor.
 */
#define HV_PTE_CLIENT1               (__HV_PTE_ONE << HV_PTE_INDEX_CLIENT1)

/** Client-private bit in PTE.
 *
 * This bit is guaranteed not to be inspected or modified by the
 * hypervisor.
 */
#define HV_PTE_CLIENT2               (__HV_PTE_ONE << HV_PTE_INDEX_CLIENT2)

/** Non-coherent (NC) bit in PTE.
 *
 * If this bit is set, the mapping that is set up will be non-coherent
 * (also known as non-inclusive).  This means that changes to the L3
 * cache will not cause a local copy to be invalidated.  It is generally
 * recommended only for read-only mappings.
 *
 * In level-1 PTEs, if the Page bit is clear, this bit determines how the
 * level-2 page table is accessed.
 */
#define HV_PTE_NC                    (__HV_PTE_ONE << HV_PTE_INDEX_NC)

/** Is this page prevented from filling the L1$?
 *
 * If this bit is set, the page described by the PTE will not be cached
 * the local cpu's L1 cache.
 *
 * If CHIP_HAS_NC_AND_NOALLOC_BITS() is not true in <chip.h> for this chip,
 * it is illegal to use this attribute, and may cause client termination.
 *
 * In level-1 PTEs, if the Page bit is clear, this bit
 * determines how the level-2 page table is accessed.
 */
#define HV_PTE_NO_ALLOC_L1           (__HV_PTE_ONE << HV_PTE_INDEX_NO_ALLOC_L1)

/** Is this page prevented from filling the L2$?
 *
 * If this bit is set, the page described by the PTE will not be cached
 * the local cpu's L2 cache.
 *
 * If CHIP_HAS_NC_AND_NOALLOC_BITS() is not true in <chip.h> for this chip,
 * it is illegal to use this attribute, and may cause client termination.
 *
 * In level-1 PTEs, if the Page bit is clear, this bit determines how the
 * level-2 page table is accessed.
 */
#define HV_PTE_NO_ALLOC_L2           (__HV_PTE_ONE << HV_PTE_INDEX_NO_ALLOC_L2)

/** Is this a priority page?
 *
 * If this bit is set, the page described by the PTE will be given
 * priority in the cache.  Normally this translates into allowing the
 * page to use only the "red" half of the cache.  The client may wish to
 * then use the hv_set_caching service to specify that other pages which
 * alias this page will use only the "black" half of the cache.
 *
 * If the Cached Priority bit is clear, the hypervisor uses the
 * current hv_set_caching() value to choose how to cache the page.
 *
 * It is illegal to set the Cached Priority bit if the Non-Cached bit
 * is set and the Cached Remotely bit is clear, i.e. if requests to
 * the page map directly to memory.
 *
 * This bit is ignored in level-1 PTEs unless the Page bit is set.
 */
#define HV_PTE_CACHED_PRIORITY       (__HV_PTE_ONE << \
                                      HV_PTE_INDEX_CACHED_PRIORITY)

/** Is this a readable mapping?
 *
 * If this bit is set, code will be permitted to read from (e.g.,
 * issue load instructions against) the virtual addresses mapped by
 * this PTE.
 *
 * It is illegal for this bit to be clear if the Writable bit is set.
 *
 * This bit is ignored in level-1 PTEs unless the Page bit is set.
 */
#define HV_PTE_READABLE              (__HV_PTE_ONE << HV_PTE_INDEX_READABLE)

/** Is this a writable mapping?
 *
 * If this bit is set, code will be permitted to write to (e.g., issue
 * store instructions against) the virtual addresses mapped by this
 * PTE.
 *
 * This bit is ignored in level-1 PTEs unless the Page bit is set.
 */
#define HV_PTE_WRITABLE              (__HV_PTE_ONE << HV_PTE_INDEX_WRITABLE)

/** Is this an executable mapping?
 *
 * If this bit is set, code will be permitted to execute from
 * (e.g., jump to) the virtual addresses mapped by this PTE.
 *
 * This bit applies to any processor on the tile, if there are more
 * than one.
 *
 * This bit is ignored in level-1 PTEs unless the Page bit is set.
 */
#define HV_PTE_EXECUTABLE            (__HV_PTE_ONE << HV_PTE_INDEX_EXECUTABLE)

/** The width of a LOTAR's x or y bitfield. */
#define HV_LOTAR_WIDTH 11

/** Converts an x,y pair to a LOTAR value. */
#define HV_XY_TO_LOTAR(x, y) ((HV_LOTAR)(((x) << HV_LOTAR_WIDTH) | (y)))

/** Extracts the X component of a lotar. */
#define HV_LOTAR_X(lotar) ((lotar) >> HV_LOTAR_WIDTH)

/** Extracts the Y component of a lotar. */
#define HV_LOTAR_Y(lotar) ((lotar) & ((1 << HV_LOTAR_WIDTH) - 1))

#ifndef __ASSEMBLER__

/** Define accessor functions for a PTE bit. */
#define _HV_BIT(name, bit)                                      \
static __inline int                                             \
hv_pte_get_##name(HV_PTE pte)                                   \
{                                                               \
  return (pte.val >> HV_PTE_INDEX_##bit) & 1;                   \
}                                                               \
                                                                \
static __inline HV_PTE                                          \
hv_pte_set_##name(HV_PTE pte)                                   \
{                                                               \
  pte.val |= 1ULL << HV_PTE_INDEX_##bit;                        \
  return pte;                                                   \
}                                                               \
                                                                \
static __inline HV_PTE                                          \
hv_pte_clear_##name(HV_PTE pte)                                 \
{                                                               \
  pte.val &= ~(1ULL << HV_PTE_INDEX_##bit);                     \
  return pte;                                                   \
}

/* Generate accessors to get, set, and clear various PTE flags.
 */
_HV_BIT(present,         PRESENT)
_HV_BIT(page,            PAGE)
_HV_BIT(super,           SUPER)
_HV_BIT(client0,         CLIENT0)
_HV_BIT(client1,         CLIENT1)
_HV_BIT(client2,         CLIENT2)
_HV_BIT(migrating,       MIGRATING)
_HV_BIT(nc,              NC)
_HV_BIT(readable,        READABLE)
_HV_BIT(writable,        WRITABLE)
_HV_BIT(executable,      EXECUTABLE)
_HV_BIT(accessed,        ACCESSED)
_HV_BIT(dirty,           DIRTY)
_HV_BIT(no_alloc_l1,     NO_ALLOC_L1)
_HV_BIT(no_alloc_l2,     NO_ALLOC_L2)
_HV_BIT(cached_priority, CACHED_PRIORITY)
_HV_BIT(global,          GLOBAL)
_HV_BIT(user,            USER)

#undef _HV_BIT

/** Get the page mode from the PTE.
 *
 * This field generally determines whether and how accesses to the page
 * are cached; the HV_PTE_MODE_xxx symbols define the legal values for the
 * page mode.  The NC, NO_ALLOC_L1, and NO_ALLOC_L2 bits modify this
 * general policy.
 */
static __inline unsigned int
hv_pte_get_mode(const HV_PTE pte)
{
  return (((__hv32) pte.val) >> HV_PTE_INDEX_MODE) &
         ((1 << HV_PTE_MODE_BITS) - 1);
}

/** Set the page mode into a PTE.  See hv_pte_get_mode. */
static __inline HV_PTE
hv_pte_set_mode(HV_PTE pte, unsigned int val)
{
  pte.val &= ~(((1ULL << HV_PTE_MODE_BITS) - 1) << HV_PTE_INDEX_MODE);
  pte.val |= val << HV_PTE_INDEX_MODE;
  return pte;
}

/** Get the page frame number from the PTE.
 *
 * This field contains the upper bits of the CPA (client physical
 * address) of the target page; the complete CPA is this field with
 * HV_LOG2_PAGE_TABLE_ALIGN zero bits appended to it.
 *
 * For all PTEs in the lowest-level page table, and for all PTEs with
 * the Page bit set in all page tables, the CPA must be aligned modulo
 * the relevant page size.
 */
static __inline unsigned long
hv_pte_get_ptfn(const HV_PTE pte)
{
  return pte.val >> HV_PTE_INDEX_PTFN;
}

/** Set the page table frame number into a PTE.  See hv_pte_get_ptfn. */
static __inline HV_PTE
hv_pte_set_ptfn(HV_PTE pte, unsigned long val)
{
  pte.val &= ~(((1ULL << HV_PTE_PTFN_BITS)-1) << HV_PTE_INDEX_PTFN);
  pte.val |= (__hv64) val << HV_PTE_INDEX_PTFN;
  return pte;
}

/** Get the client physical address from the PTE.  See hv_pte_set_ptfn. */
static __inline HV_PhysAddr
hv_pte_get_pa(const HV_PTE pte)
{
  return (__hv64) hv_pte_get_ptfn(pte) << HV_LOG2_PAGE_TABLE_ALIGN;
}

/** Set the client physical address into a PTE.  See hv_pte_get_ptfn. */
static __inline HV_PTE
hv_pte_set_pa(HV_PTE pte, HV_PhysAddr pa)
{
  return hv_pte_set_ptfn(pte, pa >> HV_LOG2_PAGE_TABLE_ALIGN);
}


/** Get the remote tile caching this page.
 *
 * Specifies the remote tile which is providing the L3 cache for this page.
 *
 * This field is ignored unless the page mode is HV_PTE_MODE_CACHE_TILE_L3.
 *
 * In level-1 PTEs, if the Page bit is clear, this field determines how the
 * level-2 page table is accessed.
 */
static __inline unsigned int
hv_pte_get_lotar(const HV_PTE pte)
{
  unsigned int lotar = ((__hv32) pte.val) >> HV_PTE_INDEX_LOTAR;

  return HV_XY_TO_LOTAR( (lotar >> (HV_PTE_LOTAR_BITS / 2)),
                         (lotar & ((1 << (HV_PTE_LOTAR_BITS / 2)) - 1)) );
}


/** Set the remote tile caching a page into a PTE.  See hv_pte_get_lotar. */
static __inline HV_PTE
hv_pte_set_lotar(HV_PTE pte, unsigned int val)
{
  unsigned int x = HV_LOTAR_X(val);
  unsigned int y = HV_LOTAR_Y(val);

  pte.val &= ~(((1ULL << HV_PTE_LOTAR_BITS)-1) << HV_PTE_INDEX_LOTAR);
  pte.val |= (x << (HV_PTE_INDEX_LOTAR + HV_PTE_LOTAR_BITS / 2)) |
             (y << HV_PTE_INDEX_LOTAR);
  return pte;
}

#endif  /* !__ASSEMBLER__ */

/** Converts a client physical address to a ptfn. */
#define HV_CPA_TO_PTFN(p) ((p) >> HV_LOG2_PAGE_TABLE_ALIGN)

/** Converts a ptfn to a client physical address. */
#define HV_PTFN_TO_CPA(p) (((HV_PhysAddr)(p)) << HV_LOG2_PAGE_TABLE_ALIGN)

#if CHIP_VA_WIDTH() > 32

/*
 * Note that we currently do not allow customizing the page size
 * of the L0 pages, but fix them at 4GB, so we do not use the
 * "_HV_xxx" nomenclature for the L0 macros.
 */

/** Log number of HV_PTE entries in L0 page table */
#define HV_LOG2_L0_ENTRIES (CHIP_VA_WIDTH() - HV_LOG2_L1_SPAN)

/** Number of HV_PTE entries in L0 page table */
#define HV_L0_ENTRIES (1 << HV_LOG2_L0_ENTRIES)

/** Log size of L0 page table in bytes */
#define HV_LOG2_L0_SIZE (HV_LOG2_PTE_SIZE + HV_LOG2_L0_ENTRIES)

/** Size of L0 page table in bytes */
#define HV_L0_SIZE (1 << HV_LOG2_L0_SIZE)

#ifdef __ASSEMBLER__

/** Index in L0 for a specific VA */
#define HV_L0_INDEX(va) \
  (((va) >> HV_LOG2_L1_SPAN) & (HV_L0_ENTRIES - 1))

#else

/** Index in L1 for a specific VA */
#define HV_L0_INDEX(va) \
  (((HV_VirtAddr)(va) >> HV_LOG2_L1_SPAN) & (HV_L0_ENTRIES - 1))

#endif

#endif /* CHIP_VA_WIDTH() > 32 */

/** Log number of HV_PTE entries in L1 page table */
#define _HV_LOG2_L1_ENTRIES(log2_page_size_large) \
  (HV_LOG2_L1_SPAN - log2_page_size_large)

/** Number of HV_PTE entries in L1 page table */
#define _HV_L1_ENTRIES(log2_page_size_large) \
  (1 << _HV_LOG2_L1_ENTRIES(log2_page_size_large))

/** Log size of L1 page table in bytes */
#define _HV_LOG2_L1_SIZE(log2_page_size_large) \
  (HV_LOG2_PTE_SIZE + _HV_LOG2_L1_ENTRIES(log2_page_size_large))

/** Size of L1 page table in bytes */
#define _HV_L1_SIZE(log2_page_size_large) \
  (1 << _HV_LOG2_L1_SIZE(log2_page_size_large))

/** Log number of HV_PTE entries in level-2 page table */
#define _HV_LOG2_L2_ENTRIES(log2_page_size_large, log2_page_size_small) \
  (log2_page_size_large - log2_page_size_small)

/** Number of HV_PTE entries in level-2 page table */
#define _HV_L2_ENTRIES(log2_page_size_large, log2_page_size_small) \
  (1 << _HV_LOG2_L2_ENTRIES(log2_page_size_large, log2_page_size_small))

/** Log size of level-2 page table in bytes */
#define _HV_LOG2_L2_SIZE(log2_page_size_large, log2_page_size_small) \
  (HV_LOG2_PTE_SIZE + \
   _HV_LOG2_L2_ENTRIES(log2_page_size_large, log2_page_size_small))

/** Size of level-2 page table in bytes */
#define _HV_L2_SIZE(log2_page_size_large, log2_page_size_small) \
  (1 << _HV_LOG2_L2_SIZE(log2_page_size_large, log2_page_size_small))

#ifdef __ASSEMBLER__

#if CHIP_VA_WIDTH() > 32

/** Index in L1 for a specific VA */
#define _HV_L1_INDEX(va, log2_page_size_large) \
  (((va) >> log2_page_size_large) & (_HV_L1_ENTRIES(log2_page_size_large) - 1))

#else /* CHIP_VA_WIDTH() > 32 */

/** Index in L1 for a specific VA */
#define _HV_L1_INDEX(va, log2_page_size_large) \
  (((va) >> log2_page_size_large))

#endif /* CHIP_VA_WIDTH() > 32 */

/** Index in level-2 page table for a specific VA */
#define _HV_L2_INDEX(va, log2_page_size_large, log2_page_size_small) \
  (((va) >> log2_page_size_small) & \
   (_HV_L2_ENTRIES(log2_page_size_large, log2_page_size_small) - 1))

#else /* __ASSEMBLER __ */

#if CHIP_VA_WIDTH() > 32

/** Index in L1 for a specific VA */
#define _HV_L1_INDEX(va, log2_page_size_large) \
  (((HV_VirtAddr)(va) >> log2_page_size_large) & \
   (_HV_L1_ENTRIES(log2_page_size_large) - 1))

#else /* CHIP_VA_WIDTH() > 32 */

/** Index in L1 for a specific VA */
#define _HV_L1_INDEX(va, log2_page_size_large) \
  (((HV_VirtAddr)(va) >> log2_page_size_large))

#endif /* CHIP_VA_WIDTH() > 32 */

/** Index in level-2 page table for a specific VA */
#define _HV_L2_INDEX(va, log2_page_size_large, log2_page_size_small) \
  (((HV_VirtAddr)(va) >> log2_page_size_small) & \
   (_HV_L2_ENTRIES(log2_page_size_large, log2_page_size_small) - 1))

#endif /* __ASSEMBLER __ */

/** Position of the PFN field within the PTE (subset of the PTFN). */
#define _HV_PTE_INDEX_PFN(log2_page_size) \
  (HV_PTE_INDEX_PTFN + (log2_page_size - HV_LOG2_PAGE_TABLE_ALIGN))

/** Length of the PFN field within the PTE (subset of the PTFN). */
#define _HV_PTE_INDEX_PFN_BITS(log2_page_size) \
  (HV_PTE_INDEX_PTFN_BITS - (log2_page_size - HV_LOG2_PAGE_TABLE_ALIGN))

/** Converts a client physical address to a pfn. */
#define _HV_CPA_TO_PFN(p, log2_page_size) ((p) >> log2_page_size)

/** Converts a pfn to a client physical address. */
#define _HV_PFN_TO_CPA(p, log2_page_size) \
  (((HV_PhysAddr)(p)) << log2_page_size)

/** Converts a ptfn to a pfn. */
#define _HV_PTFN_TO_PFN(p, log2_page_size) \
  ((p) >> (log2_page_size - HV_LOG2_PAGE_TABLE_ALIGN))

/** Converts a pfn to a ptfn. */
#define _HV_PFN_TO_PTFN(p, log2_page_size) \
  ((p) << (log2_page_size - HV_LOG2_PAGE_TABLE_ALIGN))

#endif /* _HV_HV_H */
