#define ASC_VERSION "3.3K"    /* AdvanSys Driver Version */

/*
 * advansys.c - Linux Host Driver for AdvanSys SCSI Adapters
 *
 * Copyright (c) 1995-2000 Advanced System Products, Inc.
 * Copyright (c) 2000-2001 ConnectCom Solutions, Inc.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain the above copyright notice and this comment without
 * modification.
 *
 * As of March 8, 2000 Advanced System Products, Inc. (AdvanSys)
 * changed its name to ConnectCom Solutions, Inc.
 *
 */

/*

  Documentation for the AdvanSys Driver

  A. Linux Kernels Supported by this Driver
  B. Adapters Supported by this Driver
  C. Linux source files modified by AdvanSys Driver
  D. Source Comments
  E. Driver Compile Time Options and Debugging
  F. Driver LILO Option
  G. Tests to run before releasing new driver
  H. Release History
  I. Known Problems/Fix List
  J. Credits (Chronological Order)

  A. Linux Kernels Supported by this Driver

     This driver has been tested in the following Linux kernels: v2.2.18
     v2.4.0. The driver is supported on v2.2 and v2.4 kernels and on x86,
     alpha, and PowerPC platforms.

  B. Adapters Supported by this Driver

     AdvanSys (Advanced System Products, Inc.) manufactures the following
     RISC-based, Bus-Mastering, Fast (10 Mhz) and Ultra (20 Mhz) Narrow
     (8-bit transfer) SCSI Host Adapters for the ISA, EISA, VL, and PCI
     buses and RISC-based, Bus-Mastering, Ultra (20 Mhz) Wide (16-bit
     transfer) SCSI Host Adapters for the PCI bus.

     The CDB counts below indicate the number of SCSI CDB (Command
     Descriptor Block) requests that can be stored in the RISC chip
     cache and board LRAM. A CDB is a single SCSI command. The driver
     detect routine will display the number of CDBs available for each
     adapter detected. The number of CDBs used by the driver can be
     lowered in the BIOS by changing the 'Host Queue Size' adapter setting.

     Laptop Products:
        ABP-480 - Bus-Master CardBus (16 CDB) (2.4 kernel and greater)

     Connectivity Products:
        ABP510/5150 - Bus-Master ISA (240 CDB)
        ABP5140 - Bus-Master ISA PnP (16 CDB)
        ABP5142 - Bus-Master ISA PnP with floppy (16 CDB)
        ABP902/3902 - Bus-Master PCI (16 CDB)
        ABP3905 - Bus-Master PCI (16 CDB)
        ABP915 - Bus-Master PCI (16 CDB)
        ABP920 - Bus-Master PCI (16 CDB)
        ABP3922 - Bus-Master PCI (16 CDB)
        ABP3925 - Bus-Master PCI (16 CDB)
        ABP930 - Bus-Master PCI (16 CDB)
        ABP930U - Bus-Master PCI Ultra (16 CDB)
        ABP930UA - Bus-Master PCI Ultra (16 CDB)
        ABP960 - Bus-Master PCI MAC/PC (16 CDB)
        ABP960U - Bus-Master PCI MAC/PC Ultra (16 CDB)

     Single Channel Products:
        ABP542 - Bus-Master ISA with floppy (240 CDB)
        ABP742 - Bus-Master EISA (240 CDB)
        ABP842 - Bus-Master VL (240 CDB)
        ABP940 - Bus-Master PCI (240 CDB)
        ABP940U - Bus-Master PCI Ultra (240 CDB)
        ABP940UA/3940UA - Bus-Master PCI Ultra (240 CDB)
        ABP970 - Bus-Master PCI MAC/PC (240 CDB)
        ABP970U - Bus-Master PCI MAC/PC Ultra (240 CDB)
        ABP3960UA - Bus-Master PCI MAC/PC Ultra (240 CDB)
        ABP940UW/3940UW - Bus-Master PCI Ultra-Wide (253 CDB)
        ABP970UW - Bus-Master PCI MAC/PC Ultra-Wide (253 CDB)
        ABP3940U2W - Bus-Master PCI LVD/Ultra2-Wide (253 CDB)

     Multi-Channel Products:
        ABP752 - Dual Channel Bus-Master EISA (240 CDB Per Channel)
        ABP852 - Dual Channel Bus-Master VL (240 CDB Per Channel)
        ABP950 - Dual Channel Bus-Master PCI (240 CDB Per Channel)
        ABP950UW - Dual Channel Bus-Master PCI Ultra-Wide (253 CDB Per Channel)
        ABP980 - Four Channel Bus-Master PCI (240 CDB Per Channel)
        ABP980U - Four Channel Bus-Master PCI Ultra (240 CDB Per Channel)
        ABP980UA/3980UA - Four Channel Bus-Master PCI Ultra (16 CDB Per Chan.)
        ABP3950U2W - Bus-Master PCI LVD/Ultra2-Wide and Ultra-Wide (253 CDB)
        ABP3950U3W - Bus-Master PCI Dual LVD2/Ultra3-Wide (253 CDB)

  C. Linux source files modified by AdvanSys Driver

     This section for historical purposes documents the changes
     originally made to the Linux kernel source to add the advansys
     driver. As Linux has changed some of these files have also
     been modified.

     1. linux/arch/i386/config.in:

          bool 'AdvanSys SCSI support' CONFIG_SCSI_ADVANSYS y

     2. linux/drivers/scsi/hosts.c:

          #ifdef CONFIG_SCSI_ADVANSYS
          #include "advansys.h"
          #endif

        and after "static struct scsi_host_template builtin_scsi_hosts[] =":

          #ifdef CONFIG_SCSI_ADVANSYS
          ADVANSYS,
          #endif

     3. linux/drivers/scsi/Makefile:

          ifdef CONFIG_SCSI_ADVANSYS
          SCSI_SRCS := $(SCSI_SRCS) advansys.c
          SCSI_OBJS := $(SCSI_OBJS) advansys.o
          else
          SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) advansys.o
          endif

     4. linux/init/main.c:

          extern void advansys_setup(char *str, int *ints);

        and add the following lines to the bootsetups[] array.

          #ifdef CONFIG_SCSI_ADVANSYS
             { "advansys=", advansys_setup },
          #endif

  D. Source Comments

     1. Use tab stops set to 4 for the source files. For vi use 'se tabstops=4'.

     2. This driver should be maintained in multiple files. But to make
        it easier to include with Linux and to follow Linux conventions,
        the whole driver is maintained in the source files advansys.h and
        advansys.c. In this file logical sections of the driver begin with
        a comment that contains '---'. The following are the logical sections
        of the driver below.

           --- Linux Version
           --- Linux Include File
           --- Driver Options
           --- Debugging Header
           --- Asc Library Constants and Macros
           --- Adv Library Constants and Macros
           --- Driver Constants and Macros
           --- Driver Structures
           --- Driver Data
           --- Driver Function Prototypes
           --- Linux 'struct scsi_host_template' and advansys_setup() Functions
           --- Loadable Driver Support
           --- Miscellaneous Driver Functions
           --- Functions Required by the Asc Library
           --- Functions Required by the Adv Library
           --- Tracing and Debugging Functions
           --- Asc Library Functions
           --- Adv Library Functions

     3. The string 'XXX' is used to flag code that needs to be re-written
        or that contains a problem that needs to be addressed.

     4. I have stripped comments from and reformatted the source for the
        Asc Library and Adv Library to reduce the size of this file. This
        source can be found under the following headings. The Asc Library
        is used to support Narrow Boards. The Adv Library is used to
        support Wide Boards.

           --- Asc Library Constants and Macros
           --- Adv Library Constants and Macros
           --- Asc Library Functions
           --- Adv Library Functions

  E. Driver Compile Time Options and Debugging

     In this source file the following constants can be defined. They are
     defined in the source below. Both of these options are enabled by
     default.

     1. ADVANSYS_ASSERT - Enable driver assertions (Def: Enabled)

        Enabling this option adds assertion logic statements to the
        driver. If an assertion fails a message will be displayed to
        the console, but the system will continue to operate. Any
        assertions encountered should be reported to the person
        responsible for the driver. Assertion statements may proactively
        detect problems with the driver and facilitate fixing these
        problems. Enabling assertions will add a small overhead to the
        execution of the driver.

     2. ADVANSYS_DEBUG - Enable driver debugging (Def: Disabled)

        Enabling this option adds tracing functions to the driver and
        the ability to set a driver tracing level at boot time. This
        option will also export symbols not required outside the driver to
        the kernel name space. This option is very useful for debugging
        the driver, but it will add to the size of the driver execution
        image and add overhead to the execution of the driver.

        The amount of debugging output can be controlled with the global
        variable 'asc_dbglvl'. The higher the number the more output. By
        default the debug level is 0.

        If the driver is loaded at boot time and the LILO Driver Option
        is included in the system, the debug level can be changed by
        specifying a 5th (ASC_NUM_IOPORT_PROBE + 1) I/O Port. The
        first three hex digits of the pseudo I/O Port must be set to
        'deb' and the fourth hex digit specifies the debug level: 0 - F.
        The following command line will look for an adapter at 0x330
        and set the debug level to 2.

           linux advansys=0x330,0,0,0,0xdeb2

        If the driver is built as a loadable module this variable can be
        defined when the driver is loaded. The following insmod command
        will set the debug level to one.

           insmod advansys.o asc_dbglvl=1

        Debugging Message Levels:
           0: Errors Only
           1: High-Level Tracing
           2-N: Verbose Tracing

        To enable debug output to console, please make sure that:

        a. System and kernel logging is enabled (syslogd, klogd running).
        b. Kernel messages are routed to console output. Check
           /etc/syslog.conf for an entry similar to this:

                kern.*                  /dev/console

        c. klogd is started with the appropriate -c parameter
           (e.g. klogd -c 8)

        This will cause printk() messages to be be displayed on the
        current console. Refer to the klogd(8) and syslogd(8) man pages
        for details.

        Alternatively you can enable printk() to console with this
        program. However, this is not the 'official' way to do this.
        Debug output is logged in /var/log/messages.

          main()
          {
                  syscall(103, 7, 0, 0);
          }

        Increasing LOG_BUF_LEN in kernel/printk.c to something like
        40960 allows more debug messages to be buffered in the kernel
        and written to the console or log file.

     3. ADVANSYS_STATS - Enable statistics (Def: Enabled >= v1.3.0)

        Enabling this option adds statistics collection and display
        through /proc to the driver. The information is useful for
        monitoring driver and device performance. It will add to the
        size of the driver execution image and add minor overhead to
        the execution of the driver.

        Statistics are maintained on a per adapter basis. Driver entry
        point call counts and transfer size counts are maintained.
        Statistics are only available for kernels greater than or equal
        to v1.3.0 with the CONFIG_PROC_FS (/proc) file system configured.

        AdvanSys SCSI adapter files have the following path name format:

           /proc/scsi/advansys/[0-(ASC_NUM_BOARD_SUPPORTED-1)]

        This information can be displayed with cat. For example:

           cat /proc/scsi/advansys/0

        When ADVANSYS_STATS is not defined the AdvanSys /proc files only
        contain adapter and device configuration information.

  F. Driver LILO Option

     If init/main.c is modified as described in the 'Directions for Adding
     the AdvanSys Driver to Linux' section (B.4.) above, the driver will
     recognize the 'advansys' LILO command line and /etc/lilo.conf option.
     This option can be used to either disable I/O port scanning or to limit
     scanning to 1 - 4 I/O ports. Regardless of the option setting EISA and
     PCI boards will still be searched for and detected. This option only
     affects searching for ISA and VL boards.

     Examples:
       1. Eliminate I/O port scanning:
            boot: linux advansys=
              or
            boot: linux advansys=0x0
       2. Limit I/O port scanning to one I/O port:
            boot: linux advansys=0x110
       3. Limit I/O port scanning to four I/O ports:
            boot: linux advansys=0x110,0x210,0x230,0x330

     For a loadable module the same effect can be achieved by setting
     the 'asc_iopflag' variable and 'asc_ioport' array when loading
     the driver, e.g.

           insmod advansys.o asc_iopflag=1 asc_ioport=0x110,0x330

     If ADVANSYS_DEBUG is defined a 5th (ASC_NUM_IOPORT_PROBE + 1)
     I/O Port may be added to specify the driver debug level. Refer to
     the 'Driver Compile Time Options and Debugging' section above for
     more information.

  G. Tests to run before releasing new driver

     1. In the supported kernels verify there are no warning or compile
        errors when the kernel is built as both a driver and as a module
        and with the following options:

        ADVANSYS_DEBUG - enabled and disabled
        CONFIG_SMP - enabled and disabled
        CONFIG_PROC_FS - enabled and disabled

     2. Run tests on an x86, alpha, and PowerPC with at least one narrow
        card and one wide card attached to a hard disk and CD-ROM drive:
        fdisk, mkfs, fsck, bonnie, copy/compare test from the
        CD-ROM to the hard drive.

  H. Release History

     BETA-1.0 (12/23/95):
         First Release

     BETA-1.1 (12/28/95):
         1. Prevent advansys_detect() from being called twice.
         2. Add LILO 0xdeb[0-f] option to set 'asc_dbglvl'.

     1.2 (1/12/96):
         1. Prevent re-entrancy in the interrupt handler which
            resulted in the driver hanging Linux.
         2. Fix problem that prevented ABP-940 cards from being
            recognized on some PCI motherboards.
         3. Add support for the ABP-5140 PnP ISA card.
         4. Fix check condition return status.
         5. Add conditionally compiled code for Linux v1.3.X.

     1.3 (2/23/96):
         1. Fix problem in advansys_biosparam() that resulted in the
            wrong drive geometry being returned for drives > 1GB with
            extended translation enabled.
         2. Add additional tracing during device initialization.
         3. Change code that only applies to ISA PnP adapter.
         4. Eliminate 'make dep' warning.
         5. Try to fix problem with handling resets by increasing their
            timeout value.

     1.4 (5/8/96):
         1. Change definitions to eliminate conflicts with other subsystems.
         2. Add versioning code for the shared interrupt changes.
         3. Eliminate problem in asc_rmqueue() with iterating after removing
            a request.
         4. Remove reset request loop problem from the "Known Problems or
            Issues" section. This problem was isolated and fixed in the
            mid-level SCSI driver.

     1.5 (8/8/96):
         1. Add support for ABP-940U (PCI Ultra) adapter.
         2. Add support for IRQ sharing by setting the IRQF_SHARED flag for
            request_irq and supplying a dev_id pointer to both request_irq()
            and free_irq().
         3. In AscSearchIOPortAddr11() restore a call to check_region() which
            should be used before I/O port probing.
         4. Fix bug in asc_prt_hex() which resulted in the displaying
            the wrong data.
         5. Incorporate miscellaneous Asc Library bug fixes and new microcode.
         6. Change driver versioning to be specific to each Linux sub-level.
         7. Change statistics gathering to be per adapter instead of global
            to the driver.
         8. Add more information and statistics to the adapter /proc file:
            /proc/scsi/advansys[0...].
         9. Remove 'cmd_per_lun' from the "Known Problems or Issues" list.
            This problem has been addressed with the SCSI mid-level changes
            made in v1.3.89. The advansys_select_queue_depths() function
            was added for the v1.3.89 changes.

     1.6 (9/10/96):
         1. Incorporate miscellaneous Asc Library bug fixes and new microcode.

     1.7 (9/25/96):
         1. Enable clustering and optimize the setting of the maximum number
            of scatter gather elements for any particular board. Clustering
            increases CPU utilization, but results in a relatively larger
            increase in I/O throughput.
         2. Improve the performance of the request queuing functions by
            adding a last pointer to the queue structure.
         3. Correct problems with reset and abort request handling that
            could have hung or crashed Linux.
         4. Add more information to the adapter /proc file:
            /proc/scsi/advansys[0...].
         5. Remove the request timeout issue form the driver issues list.
         6. Miscellaneous documentation additions and changes.

     1.8 (10/4/96):
         1. Make changes to handle the new v2.1.0 kernel memory mapping
            in which a kernel virtual address may not be equivalent to its
            bus or DMA memory address.
         2. Change abort and reset request handling to make it yet even
            more robust.
         3. Try to mitigate request starvation by sending ordered requests
            to heavily loaded, tag queuing enabled devices.
         4. Maintain statistics on request response time.
         5. Add request response time statistics and other information to
            the adapter /proc file: /proc/scsi/advansys[0...].

     1.9 (10/21/96):
         1. Add conditionally compiled code (ASC_QUEUE_FLOW_CONTROL) to
            make use of mid-level SCSI driver device queue depth flow
            control mechanism. This will eliminate aborts caused by a
            device being unable to keep up with requests and eliminate
            repeat busy or QUEUE FULL status returned by a device.
         2. Incorporate miscellaneous Asc Library bug fixes.
         3. To allow the driver to work in kernels with broken module
            support set 'cmd_per_lun' if the driver is compiled as a
            module. This change affects kernels v1.3.89 to present.
         4. Remove PCI BIOS address from the driver banner. The PCI BIOS
            is relocated by the motherboard BIOS and its new address can
            not be determined by the driver.
         5. Add mid-level SCSI queue depth information to the adapter
            /proc file: /proc/scsi/advansys[0...].

     2.0 (11/14/96):
         1. Change allocation of global structures used for device
            initialization to guarantee they are in DMA-able memory.
            Previously when the driver was loaded as a module these
            structures might not have been in DMA-able memory, causing
            device initialization to fail.

     2.1 (12/30/96):
         1. In advansys_reset(), if the request is a synchronous reset
            request, even if the request serial number has changed, then
            complete the request.
         2. Add Asc Library bug fixes including new microcode.
         3. Clear inquiry buffer before using it.
         4. Correct ifdef typo.

     2.2 (1/15/97):
         1. Add Asc Library bug fixes including new microcode.
         2. Add synchronous data transfer rate information to the
            adapter /proc file: /proc/scsi/advansys[0...].
         3. Change ADVANSYS_DEBUG to be disabled by default. This
            will reduce the size of the driver image, eliminate execution
            overhead, and remove unneeded symbols from the kernel symbol
            space that were previously added by the driver.
         4. Add new compile-time option ADVANSYS_ASSERT for assertion
            code that used to be defined within ADVANSYS_DEBUG. This
            option is enabled by default.

     2.8 (5/26/97):
         1. Change version number to 2.8 to synchronize the Linux driver
            version numbering with other AdvanSys drivers.
         2. Reformat source files without tabs to present the same view
            of the file to everyone regardless of the editor tab setting
            being used.
         3. Add Asc Library bug fixes.

     3.1A (1/8/98):
         1. Change version number to 3.1 to indicate that support for
            Ultra-Wide adapters (ABP-940UW) is included in this release.
         2. Add Asc Library (Narrow Board) bug fixes.
         3. Report an underrun condition with the host status byte set
            to DID_UNDERRUN. Currently DID_UNDERRUN is defined to 0 which
            causes the underrun condition to be ignored. When Linux defines
            its own DID_UNDERRUN the constant defined in this file can be
            removed.
         4. Add patch to AscWaitTixISRDone().
         5. Add support for up to 16 different AdvanSys host adapter SCSI
            channels in one system. This allows four cards with four channels
            to be used in one system.

     3.1B (1/9/98):
         1. Handle that PCI register base addresses are not always page
            aligned even though ioremap() requires that the address argument
            be page aligned.

     3.1C (1/10/98):
         1. Update latest BIOS version checked for from the /proc file.
         2. Don't set microcode SDTR variable at initialization. Instead
            wait until device capabilities have been detected from an Inquiry
            command.

     3.1D (1/21/98):
         1. Improve performance when the driver is compiled as module by
            allowing up to 64 scatter-gather elements instead of 8.

     3.1E (5/1/98):
         1. Set time delay in AscWaitTixISRDone() to 1000 ms.
         2. Include SMP locking changes.
         3. For v2.1.93 and newer kernels use CONFIG_PCI and new PCI BIOS
            access functions.
         4. Update board serial number printing.
         5. Try allocating an IRQ both with and without the IRQF_DISABLED
            flag set to allow IRQ sharing with drivers that do not set
            the IRQF_DISABLED flag. Also display a more descriptive error
            message if request_irq() fails.
         6. Update to latest Asc and Adv Libraries.

     3.2A (7/22/99):
         1. Update Adv Library to 4.16 which includes support for
            the ASC38C0800 (Ultra2/LVD) IC.

     3.2B (8/23/99):
         1. Correct PCI compile time option for v2.1.93 and greater
            kernels, advansys_info() string, and debug compile time
            option.
         2. Correct DvcSleepMilliSecond() for v2.1.0 and greater
            kernels. This caused an LVD detection/BIST problem problem
            among other things.
         3. Sort PCI cards by PCI Bus, Slot, Function ascending order
            to be consistent with the BIOS.
         4. Update to Asc Library S121 and Adv Library 5.2.

     3.2C (8/24/99):
         1. Correct PCI card detection bug introduced in 3.2B that
            prevented PCI cards from being detected in kernels older
            than v2.1.93.

     3.2D (8/26/99):
         1. Correct /proc device synchronous speed information display.
            Also when re-negotiation is pending for a target device
            note this condition with an * and footnote.
         2. Correct initialization problem with Ultra-Wide cards that
            have a pre-3.2 BIOS. A microcode variable changed locations
            in 3.2 and greater BIOSes which caused WDTR to be attempted
            erroneously with drives that don't support WDTR.

     3.2E (8/30/99):
         1. Fix compile error caused by v2.3.13 PCI structure change.
         2. Remove field from ASCEEP_CONFIG that resulted in an EEPROM
            checksum error for ISA cards.
         3. Remove ASC_QUEUE_FLOW_CONTROL conditional code. The mid-level
            SCSI changes that it depended on were never included in Linux.

     3.2F (9/3/99):
         1. Handle new initial function code added in v2.3.16 for all
            driver versions.

     3.2G (9/8/99):
         1. Fix PCI board detection in v2.3.13 and greater kernels.
         2. Fix comiple errors in v2.3.X with debugging enabled.

     3.2H (9/13/99):
         1. Add 64-bit address, long support for Alpha and UltraSPARC.
            The driver has been verified to work on an Alpha system.
         2. Add partial byte order handling support for Power PC and
            other big-endian platforms. This support has not yet been
            completed or verified.
         3. For wide boards replace block zeroing of request and
            scatter-gather structures with individual field initialization
            to improve performance.
         4. Correct and clarify ROM BIOS version detection.

     3.2I (10/8/99):
         1. Update to Adv Library 5.4.
         2. Add v2.3.19 underrun reporting to asc_isr_callback() and
            adv_isr_callback().  Remove DID_UNDERRUN constant and other
            no longer needed code that previously documented the lack
            of underrun handling.

     3.2J (10/14/99):
         1. Eliminate compile errors for v2.0 and earlier kernels.

     3.2K (11/15/99):
         1. Correct debug compile error in asc_prt_adv_scsi_req_q().
         2. Update Adv Library to 5.5.
         3. Add ifdef handling for /proc changes added in v2.3.28.
         4. Increase Wide board scatter-gather list maximum length to
            255 when the driver is compiled into the kernel.

     3.2L (11/18/99):
         1. Fix bug in adv_get_sglist() that caused an assertion failure
            at line 7475. The reqp->sgblkp pointer must be initialized
            to NULL in adv_get_sglist().

     3.2M (11/29/99):
         1. Really fix bug in adv_get_sglist().
         2. Incorporate v2.3.29 changes into driver.

     3.2N (4/1/00):
         1. Add CONFIG_ISA ifdef code.
         2. Include advansys_interrupts_enabled name change patch.
         3. For >= v2.3.28 use new SCSI error handling with new function
            advansys_eh_bus_reset(). Don't include an abort function
            because of base library limitations.
         4. For >= v2.3.28 use per board lock instead of io_request_lock.
         5. For >= v2.3.28 eliminate advansys_command() and
            advansys_command_done().
         6. Add some changes for PowerPC (Big Endian) support, but it isn't
            working yet.
         7. Fix "nonexistent resource free" problem that occurred on a module
            unload for boards with an I/O space >= 255. The 'n_io_port' field
            is only one byte and can not be used to hold an ioport length more
            than 255.

     3.3A (4/4/00):
         1. Update to Adv Library 5.8.
         2. For wide cards add support for CDBs up to 16 bytes.
         3. Eliminate warnings when CONFIG_PROC_FS is not defined.

     3.3B (5/1/00):
         1. Support for PowerPC (Big Endian) wide cards. Narrow cards
            still need work.
         2. Change bitfields to shift and mask access for endian
            portability.

     3.3C (10/13/00):
         1. Update for latest 2.4 kernel.
         2. Test ABP-480 CardBus support in 2.4 kernel - works!
         3. Update to Asc Library S123.
         4. Update to Adv Library 5.12.

     3.3D (11/22/00):
         1. Update for latest 2.4 kernel.
         2. Create patches for 2.2 and 2.4 kernels.

     3.3E (1/9/01):
         1. Now that 2.4 is released remove ifdef code for kernel versions
            less than 2.2. The driver is now only supported in kernels 2.2,
            2.4, and greater.
         2. Add code to release and acquire the io_request_lock in
            the driver entrypoint functions: advansys_detect and
            advansys_queuecommand. In kernel 2.4 the SCSI mid-level driver
            still holds the io_request_lock on entry to SCSI low-level drivers.
            This was supposed to be removed before 2.4 was released but never
            happened. When the mid-level SCSI driver is changed all references
            to the io_request_lock should be removed from the driver.
         3. Simplify error handling by removing advansys_abort(),
            AscAbortSRB(), AscResetDevice(). SCSI bus reset requests are
            now handled by resetting the SCSI bus and fully re-initializing
            the chip. This simple method of error recovery has proven to work
            most reliably after attempts at different methods. Also now only
            support the "new" error handling method and remove the obsolete
            error handling interface.
         4. Fix debug build errors.

     3.3F (1/24/01):
         1. Merge with ConnectCom version from Andy Kellner which
            updates Adv Library to 5.14.
         2. Make PowerPC (Big Endian) work for narrow cards and
            fix problems writing EEPROM for wide cards.
         3. Remove interrupts_enabled assertion function.

     3.3G (2/16/01):
         1. Return an error from narrow boards if passed a 16 byte
            CDB. The wide board can already handle 16 byte CDBs.

     3.3GJ (4/15/02):
	 1. hacks for lk 2.5 series (D. Gilbert)

     3.3GJD (10/14/02):
         1. change select_queue_depths to slave_configure
	 2. make cmd_per_lun be sane again

     3.3K [2004/06/24]:
         1. continuing cleanup for lk 2.6 series
         2. Fix problem in lk 2.6.7-bk2 that broke PCI wide cards
         3. Fix problem that oopsed ISA cards

  I. Known Problems/Fix List (XXX)

     1. Need to add memory mapping workaround. Test the memory mapping.
        If it doesn't work revert to I/O port access. Can a test be done
        safely?
     2. Handle an interrupt not working. Keep an interrupt counter in
        the interrupt handler. In the timeout function if the interrupt
        has not occurred then print a message and run in polled mode.
     3. Allow bus type scanning order to be changed.
     4. Need to add support for target mode commands, cf. CAM XPT.

  J. Credits (Chronological Order)

     Bob Frey <bfrey@turbolinux.com.cn> wrote the AdvanSys SCSI driver
     and maintained it up to 3.3F. He continues to answer questions
     and help maintain the driver.

     Nathan Hartwell <mage@cdc3.cdc.net> provided the directions and
     basis for the Linux v1.3.X changes which were included in the
     1.2 release.

     Thomas E Zerucha <zerucha@shell.portal.com> pointed out a bug
     in advansys_biosparam() which was fixed in the 1.3 release.

     Erik Ratcliffe <erik@caldera.com> has done testing of the
     AdvanSys driver in the Caldera releases.

     Rik van Riel <H.H.vanRiel@fys.ruu.nl> provided a patch to
     AscWaitTixISRDone() which he found necessary to make the
     driver work with a SCSI-1 disk.

     Mark Moran <mmoran@mmoran.com> has helped test Ultra-Wide
     support in the 3.1A driver.

     Doug Gilbert <dgilbert@interlog.com> has made changes and
     suggestions to improve the driver and done a lot of testing.

     Ken Mort <ken@mort.net> reported a DEBUG compile bug fixed
     in 3.2K.

     Tom Rini <trini@kernel.crashing.org> provided the CONFIG_ISA
     patch and helped with PowerPC wide and narrow board support.

     Philip Blundell <philb@gnu.org> provided an
     advansys_interrupts_enabled patch.

     Dave Jones <dave@denial.force9.co.uk> reported the compiler
     warnings generated when CONFIG_PROC_FS was not defined in
     the 3.2M driver.

     Jerry Quinn <jlquinn@us.ibm.com> fixed PowerPC support (endian
     problems) for wide cards.

     Bryan Henderson <bryanh@giraffe-data.com> helped debug narrow
     card error handling.

     Manuel Veloso <veloso@pobox.com> worked hard on PowerPC narrow
     board support and fixed a bug in AscGetEEPConfig().

     Arnaldo Carvalho de Melo <acme@conectiva.com.br> made
     save_flags/restore_flags changes.

     Andy Kellner <AKellner@connectcom.net> continues the Advansys SCSI
     driver development for ConnectCom (Version > 3.3F).

  K. ConnectCom (AdvanSys) Contact Information

     Mail:                   ConnectCom Solutions, Inc.
                             1150 Ringwood Court
                             San Jose, CA 95131
     Operator/Sales:         1-408-383-9400
     FAX:                    1-408-383-9612
     Tech Support:           1-408-467-2930
     Tech Support E-Mail:    linux@connectcom.net
     FTP Site:               ftp.connectcom.net (login: anonymous)
     Web Site:               http://www.connectcom.net

*/

/*
 * --- Linux Include Files
 */

#include <linux/module.h>

#if defined(CONFIG_X86) && !defined(CONFIG_ISA)
#define CONFIG_ISA
#endif /* CONFIG_X86 && !CONFIG_ISA */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/stat.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/dma.h>

/* FIXME: (by jejb@steeleye.com) This warning is present for two
 * reasons:
 *
 * 1) This driver badly needs converting to the correct driver model
 *    probing API
 *
 * 2) Although all of the necessary command mapping places have the
 * appropriate dma_map.. APIs, the driver still processes its internal
 * queue using bus_to_virt() and virt_to_bus() which are illegal under
 * the API.  The entire queue processing structure will need to be
 * altered to fix this.
 */
#warning this driver is still not properly converted to the DMA API

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#ifdef CONFIG_PCI
#include <linux/pci.h>
#endif /* CONFIG_PCI */


/*
 * --- Driver Options
 */

/* Enable driver assertions. */
#define ADVANSYS_ASSERT

/* Enable driver /proc statistics. */
#define ADVANSYS_STATS

/* Enable driver tracing. */
/* #define ADVANSYS_DEBUG */


/*
 * --- Debugging Header
 */

#ifdef ADVANSYS_DEBUG
#define STATIC
#else /* ADVANSYS_DEBUG */
#define STATIC static
#endif /* ADVANSYS_DEBUG */


/*
 * --- Asc Library Constants and Macros
 */

#define ASC_LIB_VERSION_MAJOR  1
#define ASC_LIB_VERSION_MINOR  24
#define ASC_LIB_SERIAL_NUMBER  123

/*
 * Portable Data Types
 *
 * Any instance where a 32-bit long or pointer type is assumed
 * for precision or HW defined structures, the following define
 * types must be used. In Linux the char, short, and int types
 * are all consistent at 8, 16, and 32 bits respectively. Pointers
 * and long types are 64 bits on Alpha and UltraSPARC.
 */
#define ASC_PADDR __u32         /* Physical/Bus address data type. */
#define ASC_VADDR __u32         /* Virtual address data type. */
#define ASC_DCNT  __u32         /* Unsigned Data count type. */
#define ASC_SDCNT __s32         /* Signed Data count type. */

/*
 * These macros are used to convert a virtual address to a
 * 32-bit value. This currently can be used on Linux Alpha
 * which uses 64-bit virtual address but a 32-bit bus address.
 * This is likely to break in the future, but doing this now
 * will give us time to change the HW and FW to handle 64-bit
 * addresses.
 */
#define ASC_VADDR_TO_U32   virt_to_bus
#define ASC_U32_TO_VADDR   bus_to_virt

typedef unsigned char uchar;

#ifndef TRUE
#define TRUE     (1)
#endif
#ifndef FALSE
#define FALSE    (0)
#endif

#define EOF      (-1)
#define ERR      (-1)
#define UW_ERR   (uint)(0xFFFF)
#define isodd_word(val)   ((((uint)val) & (uint)0x0001) != 0)
#define AscPCIConfigVendorIDRegister      0x0000
#define AscPCIConfigDeviceIDRegister      0x0002
#define AscPCIConfigCommandRegister       0x0004
#define AscPCIConfigStatusRegister        0x0006
#define AscPCIConfigRevisionIDRegister    0x0008
#define AscPCIConfigCacheSize             0x000C
#define AscPCIConfigLatencyTimer          0x000D
#define AscPCIIOBaseRegister              0x0010
#define AscPCICmdRegBits_IOMemBusMaster   0x0007
#define ASC_PCI_ID2BUS(id)    ((id) & 0xFF)
#define ASC_PCI_ID2DEV(id)    (((id) >> 11) & 0x1F)
#define ASC_PCI_ID2FUNC(id)   (((id) >> 8) & 0x7)
#define ASC_PCI_MKID(bus, dev, func) ((((dev) & 0x1F) << 11) | (((func) & 0x7) << 8) | ((bus) & 0xFF))
#define ASC_PCI_REVISION_3150             0x02
#define ASC_PCI_REVISION_3050             0x03

#define  ASC_DVCLIB_CALL_DONE     (1)
#define  ASC_DVCLIB_CALL_FAILED   (0)
#define  ASC_DVCLIB_CALL_ERROR    (-1)

#define PCI_VENDOR_ID_ASP		0x10cd
#define PCI_DEVICE_ID_ASP_1200A		0x1100
#define PCI_DEVICE_ID_ASP_ABP940	0x1200
#define PCI_DEVICE_ID_ASP_ABP940U	0x1300
#define PCI_DEVICE_ID_ASP_ABP940UW	0x2300
#define PCI_DEVICE_ID_38C0800_REV1	0x2500
#define PCI_DEVICE_ID_38C1600_REV1	0x2700

/*
 * Enable CC_VERY_LONG_SG_LIST to support up to 64K element SG lists.
 * The SRB structure will have to be changed and the ASC_SRB2SCSIQ()
 * macro re-defined to be able to obtain a ASC_SCSI_Q pointer from the
 * SRB structure.
 */
#define CC_VERY_LONG_SG_LIST 0
#define ASC_SRB2SCSIQ(srb_ptr)  (srb_ptr)

#define PortAddr                 unsigned short    /* port address size  */
#define inp(port)                inb(port)
#define outp(port, byte)         outb((byte), (port))

#define inpw(port)               inw(port)
#define outpw(port, word)        outw((word), (port))

#define ASC_MAX_SG_QUEUE    7
#define ASC_MAX_SG_LIST     255

#define ASC_CS_TYPE  unsigned short

#define ASC_IS_ISA          (0x0001)
#define ASC_IS_ISAPNP       (0x0081)
#define ASC_IS_EISA         (0x0002)
#define ASC_IS_PCI          (0x0004)
#define ASC_IS_PCI_ULTRA    (0x0104)
#define ASC_IS_PCMCIA       (0x0008)
#define ASC_IS_MCA          (0x0020)
#define ASC_IS_VL           (0x0040)
#define ASC_ISA_PNP_PORT_ADDR  (0x279)
#define ASC_ISA_PNP_PORT_WRITE (ASC_ISA_PNP_PORT_ADDR+0x800)
#define ASC_IS_WIDESCSI_16  (0x0100)
#define ASC_IS_WIDESCSI_32  (0x0200)
#define ASC_IS_BIG_ENDIAN   (0x8000)
#define ASC_CHIP_MIN_VER_VL      (0x01)
#define ASC_CHIP_MAX_VER_VL      (0x07)
#define ASC_CHIP_MIN_VER_PCI     (0x09)
#define ASC_CHIP_MAX_VER_PCI     (0x0F)
#define ASC_CHIP_VER_PCI_BIT     (0x08)
#define ASC_CHIP_MIN_VER_ISA     (0x11)
#define ASC_CHIP_MIN_VER_ISA_PNP (0x21)
#define ASC_CHIP_MAX_VER_ISA     (0x27)
#define ASC_CHIP_VER_ISA_BIT     (0x30)
#define ASC_CHIP_VER_ISAPNP_BIT  (0x20)
#define ASC_CHIP_VER_ASYN_BUG    (0x21)
#define ASC_CHIP_VER_PCI             0x08
#define ASC_CHIP_VER_PCI_ULTRA_3150  (ASC_CHIP_VER_PCI | 0x02)
#define ASC_CHIP_VER_PCI_ULTRA_3050  (ASC_CHIP_VER_PCI | 0x03)
#define ASC_CHIP_MIN_VER_EISA (0x41)
#define ASC_CHIP_MAX_VER_EISA (0x47)
#define ASC_CHIP_VER_EISA_BIT (0x40)
#define ASC_CHIP_LATEST_VER_EISA   ((ASC_CHIP_MIN_VER_EISA - 1) + 3)
#define ASC_MAX_LIB_SUPPORTED_ISA_CHIP_VER   0x21
#define ASC_MAX_LIB_SUPPORTED_PCI_CHIP_VER   0x0A
#define ASC_MAX_VL_DMA_ADDR     (0x07FFFFFFL)
#define ASC_MAX_VL_DMA_COUNT    (0x07FFFFFFL)
#define ASC_MAX_PCI_DMA_ADDR    (0xFFFFFFFFL)
#define ASC_MAX_PCI_DMA_COUNT   (0xFFFFFFFFL)
#define ASC_MAX_ISA_DMA_ADDR    (0x00FFFFFFL)
#define ASC_MAX_ISA_DMA_COUNT   (0x00FFFFFFL)
#define ASC_MAX_EISA_DMA_ADDR   (0x07FFFFFFL)
#define ASC_MAX_EISA_DMA_COUNT  (0x07FFFFFFL)

#define ASC_SCSI_ID_BITS  3
#define ASC_SCSI_TIX_TYPE     uchar
#define ASC_ALL_DEVICE_BIT_SET  0xFF
#define ASC_SCSI_BIT_ID_TYPE  uchar
#define ASC_MAX_TID       7
#define ASC_MAX_LUN       7
#define ASC_SCSI_WIDTH_BIT_SET  0xFF
#define ASC_MAX_SENSE_LEN   32
#define ASC_MIN_SENSE_LEN   14
#define ASC_MAX_CDB_LEN     12
#define ASC_SCSI_RESET_HOLD_TIME_US  60

#define ADV_INQ_CLOCKING_ST_ONLY    0x0
#define ADV_INQ_CLOCKING_DT_ONLY    0x1
#define ADV_INQ_CLOCKING_ST_AND_DT  0x3

/*
 * Inquiry SPC-2 SPI Byte 1 EVPD (Enable Vital Product Data)
 * and CmdDt (Command Support Data) field bit definitions.
 */
#define ADV_INQ_RTN_VPD_AND_CMDDT           0x3
#define ADV_INQ_RTN_CMDDT_FOR_OP_CODE       0x2
#define ADV_INQ_RTN_VPD_FOR_PG_CODE         0x1
#define ADV_INQ_RTN_STD_INQUIRY_DATA        0x0

#define ASC_SCSIDIR_NOCHK    0x00
#define ASC_SCSIDIR_T2H      0x08
#define ASC_SCSIDIR_H2T      0x10
#define ASC_SCSIDIR_NODATA   0x18
#define SCSI_ASC_NOMEDIA          0x3A
#define ASC_SRB_HOST(x)  ((uchar)((uchar)(x) >> 4))
#define ASC_SRB_TID(x)   ((uchar)((uchar)(x) & (uchar)0x0F))
#define ASC_SRB_LUN(x)   ((uchar)((uint)(x) >> 13))
#define PUT_CDB1(x)   ((uchar)((uint)(x) >> 8))
#define MS_CMD_DONE    0x00
#define MS_EXTEND      0x01
#define MS_SDTR_LEN    0x03
#define MS_SDTR_CODE   0x01
#define MS_WDTR_LEN    0x02
#define MS_WDTR_CODE   0x03
#define MS_MDP_LEN    0x05
#define MS_MDP_CODE   0x00

/*
 * Inquiry data structure and bitfield macros
 *
 * Only quantities of more than 1 bit are shifted, since the others are
 * just tested for true or false. C bitfields aren't portable between big
 * and little-endian platforms so they are not used.
 */

#define ASC_INQ_DVC_TYPE(inq)       ((inq)->periph & 0x1f)
#define ASC_INQ_QUALIFIER(inq)      (((inq)->periph & 0xe0) >> 5)
#define ASC_INQ_DVC_TYPE_MOD(inq)   ((inq)->devtype & 0x7f)
#define ASC_INQ_REMOVABLE(inq)      ((inq)->devtype & 0x80)
#define ASC_INQ_ANSI_VER(inq)       ((inq)->ver & 0x07)
#define ASC_INQ_ECMA_VER(inq)       (((inq)->ver & 0x38) >> 3)
#define ASC_INQ_ISO_VER(inq)        (((inq)->ver & 0xc0) >> 6)
#define ASC_INQ_RESPONSE_FMT(inq)   ((inq)->byte3 & 0x0f)
#define ASC_INQ_TERM_IO(inq)        ((inq)->byte3 & 0x40)
#define ASC_INQ_ASYNC_NOTIF(inq)    ((inq)->byte3 & 0x80)
#define ASC_INQ_SOFT_RESET(inq)     ((inq)->flags & 0x01)
#define ASC_INQ_CMD_QUEUE(inq)      ((inq)->flags & 0x02)
#define ASC_INQ_LINK_CMD(inq)       ((inq)->flags & 0x08)
#define ASC_INQ_SYNC(inq)           ((inq)->flags & 0x10)
#define ASC_INQ_WIDE16(inq)         ((inq)->flags & 0x20)
#define ASC_INQ_WIDE32(inq)         ((inq)->flags & 0x40)
#define ASC_INQ_REL_ADDR(inq)       ((inq)->flags & 0x80)
#define ASC_INQ_INFO_UNIT(inq)      ((inq)->info & 0x01)
#define ASC_INQ_QUICK_ARB(inq)      ((inq)->info & 0x02)
#define ASC_INQ_CLOCKING(inq)       (((inq)->info & 0x0c) >> 2)

typedef struct {
    uchar               periph;
    uchar               devtype;
    uchar               ver;
    uchar               byte3;
    uchar               add_len;
    uchar               res1;
    uchar               res2;
    uchar               flags;
    uchar               vendor_id[8];
    uchar               product_id[16];
    uchar               product_rev_level[4];
} ASC_SCSI_INQUIRY;

#define ASC_SG_LIST_PER_Q   7
#define QS_FREE        0x00
#define QS_READY       0x01
#define QS_DISC1       0x02
#define QS_DISC2       0x04
#define QS_BUSY        0x08
#define QS_ABORTED     0x40
#define QS_DONE        0x80
#define QC_NO_CALLBACK   0x01
#define QC_SG_SWAP_QUEUE 0x02
#define QC_SG_HEAD       0x04
#define QC_DATA_IN       0x08
#define QC_DATA_OUT      0x10
#define QC_URGENT        0x20
#define QC_MSG_OUT       0x40
#define QC_REQ_SENSE     0x80
#define QCSG_SG_XFER_LIST  0x02
#define QCSG_SG_XFER_MORE  0x04
#define QCSG_SG_XFER_END   0x08
#define QD_IN_PROGRESS       0x00
#define QD_NO_ERROR          0x01
#define QD_ABORTED_BY_HOST   0x02
#define QD_WITH_ERROR        0x04
#define QD_INVALID_REQUEST   0x80
#define QD_INVALID_HOST_NUM  0x81
#define QD_INVALID_DEVICE    0x82
#define QD_ERR_INTERNAL      0xFF
#define QHSTA_NO_ERROR               0x00
#define QHSTA_M_SEL_TIMEOUT          0x11
#define QHSTA_M_DATA_OVER_RUN        0x12
#define QHSTA_M_DATA_UNDER_RUN       0x12
#define QHSTA_M_UNEXPECTED_BUS_FREE  0x13
#define QHSTA_M_BAD_BUS_PHASE_SEQ    0x14
#define QHSTA_D_QDONE_SG_LIST_CORRUPTED 0x21
#define QHSTA_D_ASC_DVC_ERROR_CODE_SET  0x22
#define QHSTA_D_HOST_ABORT_FAILED       0x23
#define QHSTA_D_EXE_SCSI_Q_FAILED       0x24
#define QHSTA_D_EXE_SCSI_Q_BUSY_TIMEOUT 0x25
#define QHSTA_D_ASPI_NO_BUF_POOL        0x26
#define QHSTA_M_WTM_TIMEOUT         0x41
#define QHSTA_M_BAD_CMPL_STATUS_IN  0x42
#define QHSTA_M_NO_AUTO_REQ_SENSE   0x43
#define QHSTA_M_AUTO_REQ_SENSE_FAIL 0x44
#define QHSTA_M_TARGET_STATUS_BUSY  0x45
#define QHSTA_M_BAD_TAG_CODE        0x46
#define QHSTA_M_BAD_QUEUE_FULL_OR_BUSY  0x47
#define QHSTA_M_HUNG_REQ_SCSI_BUS_RESET 0x48
#define QHSTA_D_LRAM_CMP_ERROR        0x81
#define QHSTA_M_MICRO_CODE_ERROR_HALT 0xA1
#define ASC_FLAG_SCSIQ_REQ        0x01
#define ASC_FLAG_BIOS_SCSIQ_REQ   0x02
#define ASC_FLAG_BIOS_ASYNC_IO    0x04
#define ASC_FLAG_SRB_LINEAR_ADDR  0x08
#define ASC_FLAG_WIN16            0x10
#define ASC_FLAG_WIN32            0x20
#define ASC_FLAG_ISA_OVER_16MB    0x40
#define ASC_FLAG_DOS_VM_CALLBACK  0x80
#define ASC_TAG_FLAG_EXTRA_BYTES               0x10
#define ASC_TAG_FLAG_DISABLE_DISCONNECT        0x04
#define ASC_TAG_FLAG_DISABLE_ASYN_USE_SYN_FIX  0x08
#define ASC_TAG_FLAG_DISABLE_CHK_COND_INT_HOST 0x40
#define ASC_SCSIQ_CPY_BEG              4
#define ASC_SCSIQ_SGHD_CPY_BEG         2
#define ASC_SCSIQ_B_FWD                0
#define ASC_SCSIQ_B_BWD                1
#define ASC_SCSIQ_B_STATUS             2
#define ASC_SCSIQ_B_QNO                3
#define ASC_SCSIQ_B_CNTL               4
#define ASC_SCSIQ_B_SG_QUEUE_CNT       5
#define ASC_SCSIQ_D_DATA_ADDR          8
#define ASC_SCSIQ_D_DATA_CNT          12
#define ASC_SCSIQ_B_SENSE_LEN         20
#define ASC_SCSIQ_DONE_INFO_BEG       22
#define ASC_SCSIQ_D_SRBPTR            22
#define ASC_SCSIQ_B_TARGET_IX         26
#define ASC_SCSIQ_B_CDB_LEN           28
#define ASC_SCSIQ_B_TAG_CODE          29
#define ASC_SCSIQ_W_VM_ID             30
#define ASC_SCSIQ_DONE_STATUS         32
#define ASC_SCSIQ_HOST_STATUS         33
#define ASC_SCSIQ_SCSI_STATUS         34
#define ASC_SCSIQ_CDB_BEG             36
#define ASC_SCSIQ_DW_REMAIN_XFER_ADDR 56
#define ASC_SCSIQ_DW_REMAIN_XFER_CNT  60
#define ASC_SCSIQ_B_FIRST_SG_WK_QP    48
#define ASC_SCSIQ_B_SG_WK_QP          49
#define ASC_SCSIQ_B_SG_WK_IX          50
#define ASC_SCSIQ_W_ALT_DC1           52
#define ASC_SCSIQ_B_LIST_CNT          6
#define ASC_SCSIQ_B_CUR_LIST_CNT      7
#define ASC_SGQ_B_SG_CNTL             4
#define ASC_SGQ_B_SG_HEAD_QP          5
#define ASC_SGQ_B_SG_LIST_CNT         6
#define ASC_SGQ_B_SG_CUR_LIST_CNT     7
#define ASC_SGQ_LIST_BEG              8
#define ASC_DEF_SCSI1_QNG    4
#define ASC_MAX_SCSI1_QNG    4
#define ASC_DEF_SCSI2_QNG    16
#define ASC_MAX_SCSI2_QNG    32
#define ASC_TAG_CODE_MASK    0x23
#define ASC_STOP_REQ_RISC_STOP      0x01
#define ASC_STOP_ACK_RISC_STOP      0x03
#define ASC_STOP_CLEAN_UP_BUSY_Q    0x10
#define ASC_STOP_CLEAN_UP_DISC_Q    0x20
#define ASC_STOP_HOST_REQ_RISC_HALT 0x40
#define ASC_TIDLUN_TO_IX(tid, lun)  (ASC_SCSI_TIX_TYPE)((tid) + ((lun)<<ASC_SCSI_ID_BITS))
#define ASC_TID_TO_TARGET_ID(tid)   (ASC_SCSI_BIT_ID_TYPE)(0x01 << (tid))
#define ASC_TIX_TO_TARGET_ID(tix)   (0x01 << ((tix) & ASC_MAX_TID))
#define ASC_TIX_TO_TID(tix)         ((tix) & ASC_MAX_TID)
#define ASC_TID_TO_TIX(tid)         ((tid) & ASC_MAX_TID)
#define ASC_TIX_TO_LUN(tix)         (((tix) >> ASC_SCSI_ID_BITS) & ASC_MAX_LUN)
#define ASC_QNO_TO_QADDR(q_no)      ((ASC_QADR_BEG)+((int)(q_no) << 6))

typedef struct asc_scsiq_1 {
    uchar               status;
    uchar               q_no;
    uchar               cntl;
    uchar               sg_queue_cnt;
    uchar               target_id;
    uchar               target_lun;
    ASC_PADDR           data_addr;
    ASC_DCNT            data_cnt;
    ASC_PADDR           sense_addr;
    uchar               sense_len;
    uchar               extra_bytes;
} ASC_SCSIQ_1;

typedef struct asc_scsiq_2 {
    ASC_VADDR           srb_ptr;
    uchar               target_ix;
    uchar               flag;
    uchar               cdb_len;
    uchar               tag_code;
    ushort              vm_id;
} ASC_SCSIQ_2;

typedef struct asc_scsiq_3 {
    uchar               done_stat;
    uchar               host_stat;
    uchar               scsi_stat;
    uchar               scsi_msg;
} ASC_SCSIQ_3;

typedef struct asc_scsiq_4 {
    uchar               cdb[ASC_MAX_CDB_LEN];
    uchar               y_first_sg_list_qp;
    uchar               y_working_sg_qp;
    uchar               y_working_sg_ix;
    uchar               y_res;
    ushort              x_req_count;
    ushort              x_reconnect_rtn;
    ASC_PADDR           x_saved_data_addr;
    ASC_DCNT            x_saved_data_cnt;
} ASC_SCSIQ_4;

typedef struct asc_q_done_info {
    ASC_SCSIQ_2         d2;
    ASC_SCSIQ_3         d3;
    uchar               q_status;
    uchar               q_no;
    uchar               cntl;
    uchar               sense_len;
    uchar               extra_bytes;
    uchar               res;
    ASC_DCNT            remain_bytes;
} ASC_QDONE_INFO;

typedef struct asc_sg_list {
    ASC_PADDR           addr;
    ASC_DCNT            bytes;
} ASC_SG_LIST;

typedef struct asc_sg_head {
    ushort              entry_cnt;
    ushort              queue_cnt;
    ushort              entry_to_copy;
    ushort              res;
    ASC_SG_LIST         sg_list[ASC_MAX_SG_LIST];
} ASC_SG_HEAD;

#define ASC_MIN_SG_LIST   2

typedef struct asc_min_sg_head {
    ushort              entry_cnt;
    ushort              queue_cnt;
    ushort              entry_to_copy;
    ushort              res;
    ASC_SG_LIST         sg_list[ASC_MIN_SG_LIST];
} ASC_MIN_SG_HEAD;

#define QCX_SORT        (0x0001)
#define QCX_COALEASE    (0x0002)

typedef struct asc_scsi_q {
    ASC_SCSIQ_1         q1;
    ASC_SCSIQ_2         q2;
    uchar               *cdbptr;
    ASC_SG_HEAD         *sg_head;
    ushort              remain_sg_entry_cnt;
    ushort              next_sg_index;
} ASC_SCSI_Q;

typedef struct asc_scsi_req_q {
    ASC_SCSIQ_1         r1;
    ASC_SCSIQ_2         r2;
    uchar               *cdbptr;
    ASC_SG_HEAD         *sg_head;
    uchar               *sense_ptr;
    ASC_SCSIQ_3         r3;
    uchar               cdb[ASC_MAX_CDB_LEN];
    uchar               sense[ASC_MIN_SENSE_LEN];
} ASC_SCSI_REQ_Q;

typedef struct asc_scsi_bios_req_q {
    ASC_SCSIQ_1         r1;
    ASC_SCSIQ_2         r2;
    uchar               *cdbptr;
    ASC_SG_HEAD         *sg_head;
    uchar               *sense_ptr;
    ASC_SCSIQ_3         r3;
    uchar               cdb[ASC_MAX_CDB_LEN];
    uchar               sense[ASC_MIN_SENSE_LEN];
} ASC_SCSI_BIOS_REQ_Q;

typedef struct asc_risc_q {
    uchar               fwd;
    uchar               bwd;
    ASC_SCSIQ_1         i1;
    ASC_SCSIQ_2         i2;
    ASC_SCSIQ_3         i3;
    ASC_SCSIQ_4         i4;
} ASC_RISC_Q;

typedef struct asc_sg_list_q {
    uchar               seq_no;
    uchar               q_no;
    uchar               cntl;
    uchar               sg_head_qp;
    uchar               sg_list_cnt;
    uchar               sg_cur_list_cnt;
} ASC_SG_LIST_Q;

typedef struct asc_risc_sg_list_q {
    uchar               fwd;
    uchar               bwd;
    ASC_SG_LIST_Q       sg;
    ASC_SG_LIST         sg_list[7];
} ASC_RISC_SG_LIST_Q;

#define ASC_EXE_SCSI_IO_MAX_IDLE_LOOP  0x1000000UL
#define ASC_EXE_SCSI_IO_MAX_WAIT_LOOP  1024
#define ASCQ_ERR_NO_ERROR             0
#define ASCQ_ERR_IO_NOT_FOUND         1
#define ASCQ_ERR_LOCAL_MEM            2
#define ASCQ_ERR_CHKSUM               3
#define ASCQ_ERR_START_CHIP           4
#define ASCQ_ERR_INT_TARGET_ID        5
#define ASCQ_ERR_INT_LOCAL_MEM        6
#define ASCQ_ERR_HALT_RISC            7
#define ASCQ_ERR_GET_ASPI_ENTRY       8
#define ASCQ_ERR_CLOSE_ASPI           9
#define ASCQ_ERR_HOST_INQUIRY         0x0A
#define ASCQ_ERR_SAVED_SRB_BAD        0x0B
#define ASCQ_ERR_QCNTL_SG_LIST        0x0C
#define ASCQ_ERR_Q_STATUS             0x0D
#define ASCQ_ERR_WR_SCSIQ             0x0E
#define ASCQ_ERR_PC_ADDR              0x0F
#define ASCQ_ERR_SYN_OFFSET           0x10
#define ASCQ_ERR_SYN_XFER_TIME        0x11
#define ASCQ_ERR_LOCK_DMA             0x12
#define ASCQ_ERR_UNLOCK_DMA           0x13
#define ASCQ_ERR_VDS_CHK_INSTALL      0x14
#define ASCQ_ERR_MICRO_CODE_HALT      0x15
#define ASCQ_ERR_SET_LRAM_ADDR        0x16
#define ASCQ_ERR_CUR_QNG              0x17
#define ASCQ_ERR_SG_Q_LINKS           0x18
#define ASCQ_ERR_SCSIQ_PTR            0x19
#define ASCQ_ERR_ISR_RE_ENTRY         0x1A
#define ASCQ_ERR_CRITICAL_RE_ENTRY    0x1B
#define ASCQ_ERR_ISR_ON_CRITICAL      0x1C
#define ASCQ_ERR_SG_LIST_ODD_ADDRESS  0x1D
#define ASCQ_ERR_XFER_ADDRESS_TOO_BIG 0x1E
#define ASCQ_ERR_SCSIQ_NULL_PTR       0x1F
#define ASCQ_ERR_SCSIQ_BAD_NEXT_PTR   0x20
#define ASCQ_ERR_GET_NUM_OF_FREE_Q    0x21
#define ASCQ_ERR_SEND_SCSI_Q          0x22
#define ASCQ_ERR_HOST_REQ_RISC_HALT   0x23
#define ASCQ_ERR_RESET_SDTR           0x24

/*
 * Warning code values are set in ASC_DVC_VAR  'warn_code'.
 */
#define ASC_WARN_NO_ERROR             0x0000
#define ASC_WARN_IO_PORT_ROTATE       0x0001
#define ASC_WARN_EEPROM_CHKSUM        0x0002
#define ASC_WARN_IRQ_MODIFIED         0x0004
#define ASC_WARN_AUTO_CONFIG          0x0008
#define ASC_WARN_CMD_QNG_CONFLICT     0x0010
#define ASC_WARN_EEPROM_RECOVER       0x0020
#define ASC_WARN_CFG_MSW_RECOVER      0x0040
#define ASC_WARN_SET_PCI_CONFIG_SPACE 0x0080

/*
 * Error code values are set in ASC_DVC_VAR  'err_code'.
 */
#define ASC_IERR_WRITE_EEPROM         0x0001
#define ASC_IERR_MCODE_CHKSUM         0x0002
#define ASC_IERR_SET_PC_ADDR          0x0004
#define ASC_IERR_START_STOP_CHIP      0x0008
#define ASC_IERR_IRQ_NO               0x0010
#define ASC_IERR_SET_IRQ_NO           0x0020
#define ASC_IERR_CHIP_VERSION         0x0040
#define ASC_IERR_SET_SCSI_ID          0x0080
#define ASC_IERR_GET_PHY_ADDR         0x0100
#define ASC_IERR_BAD_SIGNATURE        0x0200
#define ASC_IERR_NO_BUS_TYPE          0x0400
#define ASC_IERR_SCAM                 0x0800
#define ASC_IERR_SET_SDTR             0x1000
#define ASC_IERR_RW_LRAM              0x8000

#define ASC_DEF_IRQ_NO  10
#define ASC_MAX_IRQ_NO  15
#define ASC_MIN_IRQ_NO  10
#define ASC_MIN_REMAIN_Q        (0x02)
#define ASC_DEF_MAX_TOTAL_QNG   (0xF0)
#define ASC_MIN_TAG_Q_PER_DVC   (0x04)
#define ASC_DEF_TAG_Q_PER_DVC   (0x04)
#define ASC_MIN_FREE_Q        ASC_MIN_REMAIN_Q
#define ASC_MIN_TOTAL_QNG     ((ASC_MAX_SG_QUEUE)+(ASC_MIN_FREE_Q))
#define ASC_MAX_TOTAL_QNG 240
#define ASC_MAX_PCI_ULTRA_INRAM_TOTAL_QNG 16
#define ASC_MAX_PCI_ULTRA_INRAM_TAG_QNG   8
#define ASC_MAX_PCI_INRAM_TOTAL_QNG  20
#define ASC_MAX_INRAM_TAG_QNG   16
#define ASC_IOADR_TABLE_MAX_IX  11
#define ASC_IOADR_GAP   0x10
#define ASC_SEARCH_IOP_GAP 0x10
#define ASC_MIN_IOP_ADDR   (PortAddr)0x0100
#define ASC_MAX_IOP_ADDR   (PortAddr)0x3F0
#define ASC_IOADR_1     (PortAddr)0x0110
#define ASC_IOADR_2     (PortAddr)0x0130
#define ASC_IOADR_3     (PortAddr)0x0150
#define ASC_IOADR_4     (PortAddr)0x0190
#define ASC_IOADR_5     (PortAddr)0x0210
#define ASC_IOADR_6     (PortAddr)0x0230
#define ASC_IOADR_7     (PortAddr)0x0250
#define ASC_IOADR_8     (PortAddr)0x0330
#define ASC_IOADR_DEF   ASC_IOADR_8
#define ASC_LIB_SCSIQ_WK_SP        256
#define ASC_MAX_SYN_XFER_NO        16
#define ASC_SYN_MAX_OFFSET         0x0F
#define ASC_DEF_SDTR_OFFSET        0x0F
#define ASC_DEF_SDTR_INDEX         0x00
#define ASC_SDTR_ULTRA_PCI_10MB_INDEX  0x02
#define SYN_XFER_NS_0  25
#define SYN_XFER_NS_1  30
#define SYN_XFER_NS_2  35
#define SYN_XFER_NS_3  40
#define SYN_XFER_NS_4  50
#define SYN_XFER_NS_5  60
#define SYN_XFER_NS_6  70
#define SYN_XFER_NS_7  85
#define SYN_ULTRA_XFER_NS_0    12
#define SYN_ULTRA_XFER_NS_1    19
#define SYN_ULTRA_XFER_NS_2    25
#define SYN_ULTRA_XFER_NS_3    32
#define SYN_ULTRA_XFER_NS_4    38
#define SYN_ULTRA_XFER_NS_5    44
#define SYN_ULTRA_XFER_NS_6    50
#define SYN_ULTRA_XFER_NS_7    57
#define SYN_ULTRA_XFER_NS_8    63
#define SYN_ULTRA_XFER_NS_9    69
#define SYN_ULTRA_XFER_NS_10   75
#define SYN_ULTRA_XFER_NS_11   82
#define SYN_ULTRA_XFER_NS_12   88
#define SYN_ULTRA_XFER_NS_13   94
#define SYN_ULTRA_XFER_NS_14  100
#define SYN_ULTRA_XFER_NS_15  107

typedef struct ext_msg {
    uchar               msg_type;
    uchar               msg_len;
    uchar               msg_req;
    union {
        struct {
            uchar               sdtr_xfer_period;
            uchar               sdtr_req_ack_offset;
        } sdtr;
        struct {
            uchar               wdtr_width;
        } wdtr;
        struct {
            uchar               mdp_b3;
            uchar               mdp_b2;
            uchar               mdp_b1;
            uchar               mdp_b0;
        } mdp;
    } u_ext_msg;
    uchar               res;
} EXT_MSG;

#define xfer_period     u_ext_msg.sdtr.sdtr_xfer_period
#define req_ack_offset  u_ext_msg.sdtr.sdtr_req_ack_offset
#define wdtr_width      u_ext_msg.wdtr.wdtr_width
#define mdp_b3          u_ext_msg.mdp_b3
#define mdp_b2          u_ext_msg.mdp_b2
#define mdp_b1          u_ext_msg.mdp_b1
#define mdp_b0          u_ext_msg.mdp_b0

typedef struct asc_dvc_cfg {
    ASC_SCSI_BIT_ID_TYPE can_tagged_qng;
    ASC_SCSI_BIT_ID_TYPE cmd_qng_enabled;
    ASC_SCSI_BIT_ID_TYPE disc_enable;
    ASC_SCSI_BIT_ID_TYPE sdtr_enable;
    uchar               chip_scsi_id;
    uchar               isa_dma_speed;
    uchar               isa_dma_channel;
    uchar               chip_version;
    ushort              lib_serial_no;
    ushort              lib_version;
    ushort              mcode_date;
    ushort              mcode_version;
    uchar               max_tag_qng[ASC_MAX_TID + 1];
    uchar               *overrun_buf;
    uchar               sdtr_period_offset[ASC_MAX_TID + 1];
    ushort              pci_slot_info;
    uchar               adapter_info[6];
    struct device	*dev;
} ASC_DVC_CFG;

#define ASC_DEF_DVC_CNTL       0xFFFF
#define ASC_DEF_CHIP_SCSI_ID   7
#define ASC_DEF_ISA_DMA_SPEED  4
#define ASC_INIT_STATE_NULL          0x0000
#define ASC_INIT_STATE_BEG_GET_CFG   0x0001
#define ASC_INIT_STATE_END_GET_CFG   0x0002
#define ASC_INIT_STATE_BEG_SET_CFG   0x0004
#define ASC_INIT_STATE_END_SET_CFG   0x0008
#define ASC_INIT_STATE_BEG_LOAD_MC   0x0010
#define ASC_INIT_STATE_END_LOAD_MC   0x0020
#define ASC_INIT_STATE_BEG_INQUIRY   0x0040
#define ASC_INIT_STATE_END_INQUIRY   0x0080
#define ASC_INIT_RESET_SCSI_DONE     0x0100
#define ASC_INIT_STATE_WITHOUT_EEP   0x8000
#define ASC_BUG_FIX_IF_NOT_DWB       0x0001
#define ASC_BUG_FIX_ASYN_USE_SYN     0x0002
#define ASYN_SDTR_DATA_FIX_PCI_REV_AB 0x41
#define ASC_MIN_TAGGED_CMD  7
#define ASC_MAX_SCSI_RESET_WAIT      30

struct asc_dvc_var;     /* Forward Declaration. */

typedef void (* ASC_ISR_CALLBACK)(struct asc_dvc_var *, ASC_QDONE_INFO *);
typedef int (* ASC_EXE_CALLBACK)(struct asc_dvc_var *, ASC_SCSI_Q *);

typedef struct asc_dvc_var {
    PortAddr            iop_base;
    ushort              err_code;
    ushort              dvc_cntl;
    ushort              bug_fix_cntl;
    ushort              bus_type;
    ASC_ISR_CALLBACK    isr_callback;
    ASC_EXE_CALLBACK    exe_callback;
    ASC_SCSI_BIT_ID_TYPE init_sdtr;
    ASC_SCSI_BIT_ID_TYPE sdtr_done;
    ASC_SCSI_BIT_ID_TYPE use_tagged_qng;
    ASC_SCSI_BIT_ID_TYPE unit_not_ready;
    ASC_SCSI_BIT_ID_TYPE queue_full_or_busy;
    ASC_SCSI_BIT_ID_TYPE start_motor;
    uchar               scsi_reset_wait;
    uchar               chip_no;
    char                is_in_int;
    uchar               max_total_qng;
    uchar               cur_total_qng;
    uchar               in_critical_cnt;
    uchar               irq_no;
    uchar               last_q_shortage;
    ushort              init_state;
    uchar               cur_dvc_qng[ASC_MAX_TID + 1];
    uchar               max_dvc_qng[ASC_MAX_TID + 1];
    ASC_SCSI_Q  *scsiq_busy_head[ASC_MAX_TID + 1];
    ASC_SCSI_Q  *scsiq_busy_tail[ASC_MAX_TID + 1];
    uchar               sdtr_period_tbl[ASC_MAX_SYN_XFER_NO];
    ASC_DVC_CFG *cfg;
    ASC_SCSI_BIT_ID_TYPE pci_fix_asyn_xfer_always;
    char                redo_scam;
    ushort              res2;
    uchar               dos_int13_table[ASC_MAX_TID + 1];
    ASC_DCNT            max_dma_count;
    ASC_SCSI_BIT_ID_TYPE no_scam;
    ASC_SCSI_BIT_ID_TYPE pci_fix_asyn_xfer;
    uchar               max_sdtr_index;
    uchar               host_init_sdtr_index;
    struct asc_board    *drv_ptr;
    ASC_DCNT            uc_break;
} ASC_DVC_VAR;

typedef struct asc_dvc_inq_info {
    uchar               type[ASC_MAX_TID + 1][ASC_MAX_LUN + 1];
} ASC_DVC_INQ_INFO;

typedef struct asc_cap_info {
    ASC_DCNT            lba;
    ASC_DCNT            blk_size;
} ASC_CAP_INFO;

typedef struct asc_cap_info_array {
    ASC_CAP_INFO        cap_info[ASC_MAX_TID + 1][ASC_MAX_LUN + 1];
} ASC_CAP_INFO_ARRAY;

#define ASC_MCNTL_NO_SEL_TIMEOUT  (ushort)0x0001
#define ASC_MCNTL_NULL_TARGET     (ushort)0x0002
#define ASC_CNTL_INITIATOR         (ushort)0x0001
#define ASC_CNTL_BIOS_GT_1GB       (ushort)0x0002
#define ASC_CNTL_BIOS_GT_2_DISK    (ushort)0x0004
#define ASC_CNTL_BIOS_REMOVABLE    (ushort)0x0008
#define ASC_CNTL_NO_SCAM           (ushort)0x0010
#define ASC_CNTL_INT_MULTI_Q       (ushort)0x0080
#define ASC_CNTL_NO_LUN_SUPPORT    (ushort)0x0040
#define ASC_CNTL_NO_VERIFY_COPY    (ushort)0x0100
#define ASC_CNTL_RESET_SCSI        (ushort)0x0200
#define ASC_CNTL_INIT_INQUIRY      (ushort)0x0400
#define ASC_CNTL_INIT_VERBOSE      (ushort)0x0800
#define ASC_CNTL_SCSI_PARITY       (ushort)0x1000
#define ASC_CNTL_BURST_MODE        (ushort)0x2000
#define ASC_CNTL_SDTR_ENABLE_ULTRA (ushort)0x4000
#define ASC_EEP_DVC_CFG_BEG_VL    2
#define ASC_EEP_MAX_DVC_ADDR_VL   15
#define ASC_EEP_DVC_CFG_BEG      32
#define ASC_EEP_MAX_DVC_ADDR     45
#define ASC_EEP_DEFINED_WORDS    10
#define ASC_EEP_MAX_ADDR         63
#define ASC_EEP_RES_WORDS         0
#define ASC_EEP_MAX_RETRY        20
#define ASC_MAX_INIT_BUSY_RETRY   8
#define ASC_EEP_ISA_PNP_WSIZE    16

/*
 * These macros keep the chip SCSI id and ISA DMA speed
 * bitfields in board order. C bitfields aren't portable
 * between big and little-endian platforms so they are
 * not used.
 */

#define ASC_EEP_GET_CHIP_ID(cfg)    ((cfg)->id_speed & 0x0f)
#define ASC_EEP_GET_DMA_SPD(cfg)    (((cfg)->id_speed & 0xf0) >> 4)
#define ASC_EEP_SET_CHIP_ID(cfg, sid) \
   ((cfg)->id_speed = ((cfg)->id_speed & 0xf0) | ((sid) & ASC_MAX_TID))
#define ASC_EEP_SET_DMA_SPD(cfg, spd) \
   ((cfg)->id_speed = ((cfg)->id_speed & 0x0f) | ((spd) & 0x0f) << 4)

typedef struct asceep_config {
    ushort              cfg_lsw;
    ushort              cfg_msw;
    uchar               init_sdtr;
    uchar               disc_enable;
    uchar               use_cmd_qng;
    uchar               start_motor;
    uchar               max_total_qng;
    uchar               max_tag_qng;
    uchar               bios_scan;
    uchar               power_up_wait;
    uchar               no_scam;
    uchar               id_speed; /* low order 4 bits is chip scsi id */
                                  /* high order 4 bits is isa dma speed */
    uchar               dos_int13_table[ASC_MAX_TID + 1];
    uchar               adapter_info[6];
    ushort              cntl;
    ushort              chksum;
} ASCEEP_CONFIG;

#define ASC_PCI_CFG_LSW_SCSI_PARITY  0x0800
#define ASC_PCI_CFG_LSW_BURST_MODE   0x0080
#define ASC_PCI_CFG_LSW_INTR_ABLE    0x0020

#define ASC_EEP_CMD_READ          0x80
#define ASC_EEP_CMD_WRITE         0x40
#define ASC_EEP_CMD_WRITE_ABLE    0x30
#define ASC_EEP_CMD_WRITE_DISABLE 0x00
#define ASC_OVERRUN_BSIZE  0x00000048UL
#define ASC_CTRL_BREAK_ONCE        0x0001
#define ASC_CTRL_BREAK_STAY_IDLE   0x0002
#define ASCV_MSGOUT_BEG         0x0000
#define ASCV_MSGOUT_SDTR_PERIOD (ASCV_MSGOUT_BEG+3)
#define ASCV_MSGOUT_SDTR_OFFSET (ASCV_MSGOUT_BEG+4)
#define ASCV_BREAK_SAVED_CODE   (ushort)0x0006
#define ASCV_MSGIN_BEG          (ASCV_MSGOUT_BEG+8)
#define ASCV_MSGIN_SDTR_PERIOD  (ASCV_MSGIN_BEG+3)
#define ASCV_MSGIN_SDTR_OFFSET  (ASCV_MSGIN_BEG+4)
#define ASCV_SDTR_DATA_BEG      (ASCV_MSGIN_BEG+8)
#define ASCV_SDTR_DONE_BEG      (ASCV_SDTR_DATA_BEG+8)
#define ASCV_MAX_DVC_QNG_BEG    (ushort)0x0020
#define ASCV_BREAK_ADDR           (ushort)0x0028
#define ASCV_BREAK_NOTIFY_COUNT   (ushort)0x002A
#define ASCV_BREAK_CONTROL        (ushort)0x002C
#define ASCV_BREAK_HIT_COUNT      (ushort)0x002E

#define ASCV_ASCDVC_ERR_CODE_W  (ushort)0x0030
#define ASCV_MCODE_CHKSUM_W   (ushort)0x0032
#define ASCV_MCODE_SIZE_W     (ushort)0x0034
#define ASCV_STOP_CODE_B      (ushort)0x0036
#define ASCV_DVC_ERR_CODE_B   (ushort)0x0037
#define ASCV_OVERRUN_PADDR_D  (ushort)0x0038
#define ASCV_OVERRUN_BSIZE_D  (ushort)0x003C
#define ASCV_HALTCODE_W       (ushort)0x0040
#define ASCV_CHKSUM_W         (ushort)0x0042
#define ASCV_MC_DATE_W        (ushort)0x0044
#define ASCV_MC_VER_W         (ushort)0x0046
#define ASCV_NEXTRDY_B        (ushort)0x0048
#define ASCV_DONENEXT_B       (ushort)0x0049
#define ASCV_USE_TAGGED_QNG_B (ushort)0x004A
#define ASCV_SCSIBUSY_B       (ushort)0x004B
#define ASCV_Q_DONE_IN_PROGRESS_B  (ushort)0x004C
#define ASCV_CURCDB_B         (ushort)0x004D
#define ASCV_RCLUN_B          (ushort)0x004E
#define ASCV_BUSY_QHEAD_B     (ushort)0x004F
#define ASCV_DISC1_QHEAD_B    (ushort)0x0050
#define ASCV_DISC_ENABLE_B    (ushort)0x0052
#define ASCV_CAN_TAGGED_QNG_B (ushort)0x0053
#define ASCV_HOSTSCSI_ID_B    (ushort)0x0055
#define ASCV_MCODE_CNTL_B     (ushort)0x0056
#define ASCV_NULL_TARGET_B    (ushort)0x0057
#define ASCV_FREE_Q_HEAD_W    (ushort)0x0058
#define ASCV_DONE_Q_TAIL_W    (ushort)0x005A
#define ASCV_FREE_Q_HEAD_B    (ushort)(ASCV_FREE_Q_HEAD_W+1)
#define ASCV_DONE_Q_TAIL_B    (ushort)(ASCV_DONE_Q_TAIL_W+1)
#define ASCV_HOST_FLAG_B      (ushort)0x005D
#define ASCV_TOTAL_READY_Q_B  (ushort)0x0064
#define ASCV_VER_SERIAL_B     (ushort)0x0065
#define ASCV_HALTCODE_SAVED_W (ushort)0x0066
#define ASCV_WTM_FLAG_B       (ushort)0x0068
#define ASCV_RISC_FLAG_B      (ushort)0x006A
#define ASCV_REQ_SG_LIST_QP   (ushort)0x006B
#define ASC_HOST_FLAG_IN_ISR        0x01
#define ASC_HOST_FLAG_ACK_INT       0x02
#define ASC_RISC_FLAG_GEN_INT      0x01
#define ASC_RISC_FLAG_REQ_SG_LIST  0x02
#define IOP_CTRL         (0x0F)
#define IOP_STATUS       (0x0E)
#define IOP_INT_ACK      IOP_STATUS
#define IOP_REG_IFC      (0x0D)
#define IOP_SYN_OFFSET    (0x0B)
#define IOP_EXTRA_CONTROL (0x0D)
#define IOP_REG_PC        (0x0C)
#define IOP_RAM_ADDR      (0x0A)
#define IOP_RAM_DATA      (0x08)
#define IOP_EEP_DATA      (0x06)
#define IOP_EEP_CMD       (0x07)
#define IOP_VERSION       (0x03)
#define IOP_CONFIG_HIGH   (0x04)
#define IOP_CONFIG_LOW    (0x02)
#define IOP_SIG_BYTE      (0x01)
#define IOP_SIG_WORD      (0x00)
#define IOP_REG_DC1      (0x0E)
#define IOP_REG_DC0      (0x0C)
#define IOP_REG_SB       (0x0B)
#define IOP_REG_DA1      (0x0A)
#define IOP_REG_DA0      (0x08)
#define IOP_REG_SC       (0x09)
#define IOP_DMA_SPEED    (0x07)
#define IOP_REG_FLAG     (0x07)
#define IOP_FIFO_H       (0x06)
#define IOP_FIFO_L       (0x04)
#define IOP_REG_ID       (0x05)
#define IOP_REG_QP       (0x03)
#define IOP_REG_IH       (0x02)
#define IOP_REG_IX       (0x01)
#define IOP_REG_AX       (0x00)
#define IFC_REG_LOCK      (0x00)
#define IFC_REG_UNLOCK    (0x09)
#define IFC_WR_EN_FILTER  (0x10)
#define IFC_RD_NO_EEPROM  (0x10)
#define IFC_SLEW_RATE     (0x20)
#define IFC_ACT_NEG       (0x40)
#define IFC_INP_FILTER    (0x80)
#define IFC_INIT_DEFAULT  (IFC_ACT_NEG | IFC_REG_UNLOCK)
#define SC_SEL   (uchar)(0x80)
#define SC_BSY   (uchar)(0x40)
#define SC_ACK   (uchar)(0x20)
#define SC_REQ   (uchar)(0x10)
#define SC_ATN   (uchar)(0x08)
#define SC_IO    (uchar)(0x04)
#define SC_CD    (uchar)(0x02)
#define SC_MSG   (uchar)(0x01)
#define SEC_SCSI_CTL         (uchar)(0x80)
#define SEC_ACTIVE_NEGATE    (uchar)(0x40)
#define SEC_SLEW_RATE        (uchar)(0x20)
#define SEC_ENABLE_FILTER    (uchar)(0x10)
#define ASC_HALT_EXTMSG_IN     (ushort)0x8000
#define ASC_HALT_CHK_CONDITION (ushort)0x8100
#define ASC_HALT_SS_QUEUE_FULL (ushort)0x8200
#define ASC_HALT_DISABLE_ASYN_USE_SYN_FIX  (ushort)0x8300
#define ASC_HALT_ENABLE_ASYN_USE_SYN_FIX   (ushort)0x8400
#define ASC_HALT_SDTR_REJECTED (ushort)0x4000
#define ASC_HALT_HOST_COPY_SG_LIST_TO_RISC ( ushort )0x2000
#define ASC_MAX_QNO        0xF8
#define ASC_DATA_SEC_BEG   (ushort)0x0080
#define ASC_DATA_SEC_END   (ushort)0x0080
#define ASC_CODE_SEC_BEG   (ushort)0x0080
#define ASC_CODE_SEC_END   (ushort)0x0080
#define ASC_QADR_BEG       (0x4000)
#define ASC_QADR_USED      (ushort)(ASC_MAX_QNO * 64)
#define ASC_QADR_END       (ushort)0x7FFF
#define ASC_QLAST_ADR      (ushort)0x7FC0
#define ASC_QBLK_SIZE      0x40
#define ASC_BIOS_DATA_QBEG 0xF8
#define ASC_MIN_ACTIVE_QNO 0x01
#define ASC_QLINK_END      0xFF
#define ASC_EEPROM_WORDS   0x10
#define ASC_MAX_MGS_LEN    0x10
#define ASC_BIOS_ADDR_DEF  0xDC00
#define ASC_BIOS_SIZE      0x3800
#define ASC_BIOS_RAM_OFF   0x3800
#define ASC_BIOS_RAM_SIZE  0x800
#define ASC_BIOS_MIN_ADDR  0xC000
#define ASC_BIOS_MAX_ADDR  0xEC00
#define ASC_BIOS_BANK_SIZE 0x0400
#define ASC_MCODE_START_ADDR  0x0080
#define ASC_CFG0_HOST_INT_ON    0x0020
#define ASC_CFG0_BIOS_ON        0x0040
#define ASC_CFG0_VERA_BURST_ON  0x0080
#define ASC_CFG0_SCSI_PARITY_ON 0x0800
#define ASC_CFG1_SCSI_TARGET_ON 0x0080
#define ASC_CFG1_LRAM_8BITS_ON  0x0800
#define ASC_CFG_MSW_CLR_MASK    0x3080
#define CSW_TEST1             (ASC_CS_TYPE)0x8000
#define CSW_AUTO_CONFIG       (ASC_CS_TYPE)0x4000
#define CSW_RESERVED1         (ASC_CS_TYPE)0x2000
#define CSW_IRQ_WRITTEN       (ASC_CS_TYPE)0x1000
#define CSW_33MHZ_SELECTED    (ASC_CS_TYPE)0x0800
#define CSW_TEST2             (ASC_CS_TYPE)0x0400
#define CSW_TEST3             (ASC_CS_TYPE)0x0200
#define CSW_RESERVED2         (ASC_CS_TYPE)0x0100
#define CSW_DMA_DONE          (ASC_CS_TYPE)0x0080
#define CSW_FIFO_RDY          (ASC_CS_TYPE)0x0040
#define CSW_EEP_READ_DONE     (ASC_CS_TYPE)0x0020
#define CSW_HALTED            (ASC_CS_TYPE)0x0010
#define CSW_SCSI_RESET_ACTIVE (ASC_CS_TYPE)0x0008
#define CSW_PARITY_ERR        (ASC_CS_TYPE)0x0004
#define CSW_SCSI_RESET_LATCH  (ASC_CS_TYPE)0x0002
#define CSW_INT_PENDING       (ASC_CS_TYPE)0x0001
#define CIW_CLR_SCSI_RESET_INT (ASC_CS_TYPE)0x1000
#define CIW_INT_ACK      (ASC_CS_TYPE)0x0100
#define CIW_TEST1        (ASC_CS_TYPE)0x0200
#define CIW_TEST2        (ASC_CS_TYPE)0x0400
#define CIW_SEL_33MHZ    (ASC_CS_TYPE)0x0800
#define CIW_IRQ_ACT      (ASC_CS_TYPE)0x1000
#define CC_CHIP_RESET   (uchar)0x80
#define CC_SCSI_RESET   (uchar)0x40
#define CC_HALT         (uchar)0x20
#define CC_SINGLE_STEP  (uchar)0x10
#define CC_DMA_ABLE     (uchar)0x08
#define CC_TEST         (uchar)0x04
#define CC_BANK_ONE     (uchar)0x02
#define CC_DIAG         (uchar)0x01
#define ASC_1000_ID0W      0x04C1
#define ASC_1000_ID0W_FIX  0x00C1
#define ASC_1000_ID1B      0x25
#define ASC_EISA_BIG_IOP_GAP   (0x1C30-0x0C50)
#define ASC_EISA_SMALL_IOP_GAP (0x0020)
#define ASC_EISA_MIN_IOP_ADDR  (0x0C30)
#define ASC_EISA_MAX_IOP_ADDR  (0xFC50)
#define ASC_EISA_REV_IOP_MASK  (0x0C83)
#define ASC_EISA_PID_IOP_MASK  (0x0C80)
#define ASC_EISA_CFG_IOP_MASK  (0x0C86)
#define ASC_GET_EISA_SLOT(iop)  (PortAddr)((iop) & 0xF000)
#define ASC_EISA_ID_740    0x01745004UL
#define ASC_EISA_ID_750    0x01755004UL
#define INS_HALTINT        (ushort)0x6281
#define INS_HALT           (ushort)0x6280
#define INS_SINT           (ushort)0x6200
#define INS_RFLAG_WTM      (ushort)0x7380
#define ASC_MC_SAVE_CODE_WSIZE  0x500
#define ASC_MC_SAVE_DATA_WSIZE  0x40

typedef struct asc_mc_saved {
    ushort              data[ASC_MC_SAVE_DATA_WSIZE];
    ushort              code[ASC_MC_SAVE_CODE_WSIZE];
} ASC_MC_SAVED;

#define AscGetQDoneInProgress(port)         AscReadLramByte((port), ASCV_Q_DONE_IN_PROGRESS_B)
#define AscPutQDoneInProgress(port, val)    AscWriteLramByte((port), ASCV_Q_DONE_IN_PROGRESS_B, val)
#define AscGetVarFreeQHead(port)            AscReadLramWord((port), ASCV_FREE_Q_HEAD_W)
#define AscGetVarDoneQTail(port)            AscReadLramWord((port), ASCV_DONE_Q_TAIL_W)
#define AscPutVarFreeQHead(port, val)       AscWriteLramWord((port), ASCV_FREE_Q_HEAD_W, val)
#define AscPutVarDoneQTail(port, val)       AscWriteLramWord((port), ASCV_DONE_Q_TAIL_W, val)
#define AscGetRiscVarFreeQHead(port)        AscReadLramByte((port), ASCV_NEXTRDY_B)
#define AscGetRiscVarDoneQTail(port)        AscReadLramByte((port), ASCV_DONENEXT_B)
#define AscPutRiscVarFreeQHead(port, val)   AscWriteLramByte((port), ASCV_NEXTRDY_B, val)
#define AscPutRiscVarDoneQTail(port, val)   AscWriteLramByte((port), ASCV_DONENEXT_B, val)
#define AscPutMCodeSDTRDoneAtID(port, id, data)  AscWriteLramByte((port), (ushort)((ushort)ASCV_SDTR_DONE_BEG+(ushort)id), (data));
#define AscGetMCodeSDTRDoneAtID(port, id)        AscReadLramByte((port), (ushort)((ushort)ASCV_SDTR_DONE_BEG+(ushort)id));
#define AscPutMCodeInitSDTRAtID(port, id, data)  AscWriteLramByte((port), (ushort)((ushort)ASCV_SDTR_DATA_BEG+(ushort)id), data);
#define AscGetMCodeInitSDTRAtID(port, id)        AscReadLramByte((port), (ushort)((ushort)ASCV_SDTR_DATA_BEG+(ushort)id));
#define AscSynIndexToPeriod(index)        (uchar)(asc_dvc->sdtr_period_tbl[ (index) ])
#define AscGetChipSignatureByte(port)     (uchar)inp((port)+IOP_SIG_BYTE)
#define AscGetChipSignatureWord(port)     (ushort)inpw((port)+IOP_SIG_WORD)
#define AscGetChipVerNo(port)             (uchar)inp((port)+IOP_VERSION)
#define AscGetChipCfgLsw(port)            (ushort)inpw((port)+IOP_CONFIG_LOW)
#define AscGetChipCfgMsw(port)            (ushort)inpw((port)+IOP_CONFIG_HIGH)
#define AscSetChipCfgLsw(port, data)      outpw((port)+IOP_CONFIG_LOW, data)
#define AscSetChipCfgMsw(port, data)      outpw((port)+IOP_CONFIG_HIGH, data)
#define AscGetChipEEPCmd(port)            (uchar)inp((port)+IOP_EEP_CMD)
#define AscSetChipEEPCmd(port, data)      outp((port)+IOP_EEP_CMD, data)
#define AscGetChipEEPData(port)           (ushort)inpw((port)+IOP_EEP_DATA)
#define AscSetChipEEPData(port, data)     outpw((port)+IOP_EEP_DATA, data)
#define AscGetChipLramAddr(port)          (ushort)inpw((PortAddr)((port)+IOP_RAM_ADDR))
#define AscSetChipLramAddr(port, addr)    outpw((PortAddr)((port)+IOP_RAM_ADDR), addr)
#define AscGetChipLramData(port)          (ushort)inpw((port)+IOP_RAM_DATA)
#define AscSetChipLramData(port, data)    outpw((port)+IOP_RAM_DATA, data)
#define AscGetChipIFC(port)               (uchar)inp((port)+IOP_REG_IFC)
#define AscSetChipIFC(port, data)          outp((port)+IOP_REG_IFC, data)
#define AscGetChipStatus(port)            (ASC_CS_TYPE)inpw((port)+IOP_STATUS)
#define AscSetChipStatus(port, cs_val)    outpw((port)+IOP_STATUS, cs_val)
#define AscGetChipControl(port)           (uchar)inp((port)+IOP_CTRL)
#define AscSetChipControl(port, cc_val)   outp((port)+IOP_CTRL, cc_val)
#define AscGetChipSyn(port)               (uchar)inp((port)+IOP_SYN_OFFSET)
#define AscSetChipSyn(port, data)         outp((port)+IOP_SYN_OFFSET, data)
#define AscSetPCAddr(port, data)          outpw((port)+IOP_REG_PC, data)
#define AscGetPCAddr(port)                (ushort)inpw((port)+IOP_REG_PC)
#define AscIsIntPending(port)             (AscGetChipStatus(port) & (CSW_INT_PENDING | CSW_SCSI_RESET_LATCH))
#define AscGetChipScsiID(port)            ((AscGetChipCfgLsw(port) >> 8) & ASC_MAX_TID)
#define AscGetExtraControl(port)          (uchar)inp((port)+IOP_EXTRA_CONTROL)
#define AscSetExtraControl(port, data)    outp((port)+IOP_EXTRA_CONTROL, data)
#define AscReadChipAX(port)               (ushort)inpw((port)+IOP_REG_AX)
#define AscWriteChipAX(port, data)        outpw((port)+IOP_REG_AX, data)
#define AscReadChipIX(port)               (uchar)inp((port)+IOP_REG_IX)
#define AscWriteChipIX(port, data)        outp((port)+IOP_REG_IX, data)
#define AscReadChipIH(port)               (ushort)inpw((port)+IOP_REG_IH)
#define AscWriteChipIH(port, data)        outpw((port)+IOP_REG_IH, data)
#define AscReadChipQP(port)               (uchar)inp((port)+IOP_REG_QP)
#define AscWriteChipQP(port, data)        outp((port)+IOP_REG_QP, data)
#define AscReadChipFIFO_L(port)           (ushort)inpw((port)+IOP_REG_FIFO_L)
#define AscWriteChipFIFO_L(port, data)    outpw((port)+IOP_REG_FIFO_L, data)
#define AscReadChipFIFO_H(port)           (ushort)inpw((port)+IOP_REG_FIFO_H)
#define AscWriteChipFIFO_H(port, data)    outpw((port)+IOP_REG_FIFO_H, data)
#define AscReadChipDmaSpeed(port)         (uchar)inp((port)+IOP_DMA_SPEED)
#define AscWriteChipDmaSpeed(port, data)  outp((port)+IOP_DMA_SPEED, data)
#define AscReadChipDA0(port)              (ushort)inpw((port)+IOP_REG_DA0)
#define AscWriteChipDA0(port)             outpw((port)+IOP_REG_DA0, data)
#define AscReadChipDA1(port)              (ushort)inpw((port)+IOP_REG_DA1)
#define AscWriteChipDA1(port)             outpw((port)+IOP_REG_DA1, data)
#define AscReadChipDC0(port)              (ushort)inpw((port)+IOP_REG_DC0)
#define AscWriteChipDC0(port)             outpw((port)+IOP_REG_DC0, data)
#define AscReadChipDC1(port)              (ushort)inpw((port)+IOP_REG_DC1)
#define AscWriteChipDC1(port)             outpw((port)+IOP_REG_DC1, data)
#define AscReadChipDvcID(port)            (uchar)inp((port)+IOP_REG_ID)
#define AscWriteChipDvcID(port, data)     outp((port)+IOP_REG_ID, data)

STATIC int       AscWriteEEPCmdReg(PortAddr iop_base, uchar cmd_reg);
STATIC int       AscWriteEEPDataReg(PortAddr iop_base, ushort data_reg);
STATIC void      AscWaitEEPRead(void);
STATIC void      AscWaitEEPWrite(void);
STATIC ushort    AscReadEEPWord(PortAddr, uchar);
STATIC ushort    AscWriteEEPWord(PortAddr, uchar, ushort);
STATIC ushort    AscGetEEPConfig(PortAddr, ASCEEP_CONFIG *, ushort);
STATIC int       AscSetEEPConfigOnce(PortAddr, ASCEEP_CONFIG *, ushort);
STATIC int       AscSetEEPConfig(PortAddr, ASCEEP_CONFIG *, ushort);
STATIC int       AscStartChip(PortAddr);
STATIC int       AscStopChip(PortAddr);
STATIC void      AscSetChipIH(PortAddr, ushort);
STATIC int       AscIsChipHalted(PortAddr);
STATIC void      AscAckInterrupt(PortAddr);
STATIC void      AscDisableInterrupt(PortAddr);
STATIC void      AscEnableInterrupt(PortAddr);
STATIC void      AscSetBank(PortAddr, uchar);
STATIC int       AscResetChipAndScsiBus(ASC_DVC_VAR *);
#ifdef CONFIG_ISA
STATIC ushort    AscGetIsaDmaChannel(PortAddr);
STATIC ushort    AscSetIsaDmaChannel(PortAddr, ushort);
STATIC uchar     AscSetIsaDmaSpeed(PortAddr, uchar);
STATIC uchar     AscGetIsaDmaSpeed(PortAddr);
#endif /* CONFIG_ISA */
STATIC uchar     AscReadLramByte(PortAddr, ushort);
STATIC ushort    AscReadLramWord(PortAddr, ushort);
#if CC_VERY_LONG_SG_LIST
STATIC ASC_DCNT  AscReadLramDWord(PortAddr, ushort);
#endif /* CC_VERY_LONG_SG_LIST */
STATIC void      AscWriteLramWord(PortAddr, ushort, ushort);
STATIC void      AscWriteLramByte(PortAddr, ushort, uchar);
STATIC ASC_DCNT  AscMemSumLramWord(PortAddr, ushort, int);
STATIC void      AscMemWordSetLram(PortAddr, ushort, ushort, int);
STATIC void      AscMemWordCopyPtrToLram(PortAddr, ushort, uchar *, int);
STATIC void      AscMemDWordCopyPtrToLram(PortAddr, ushort, uchar *, int);
STATIC void      AscMemWordCopyPtrFromLram(PortAddr, ushort, uchar *, int);
STATIC ushort    AscInitAscDvcVar(ASC_DVC_VAR *);
STATIC ushort    AscInitFromEEP(ASC_DVC_VAR *);
STATIC ushort    AscInitFromAscDvcVar(ASC_DVC_VAR *);
STATIC ushort    AscInitMicroCodeVar(ASC_DVC_VAR *);
STATIC int       AscTestExternalLram(ASC_DVC_VAR *);
STATIC uchar     AscMsgOutSDTR(ASC_DVC_VAR *, uchar, uchar);
STATIC uchar     AscCalSDTRData(ASC_DVC_VAR *, uchar, uchar);
STATIC void      AscSetChipSDTR(PortAddr, uchar, uchar);
STATIC uchar     AscGetSynPeriodIndex(ASC_DVC_VAR *, uchar);
STATIC uchar     AscAllocFreeQueue(PortAddr, uchar);
STATIC uchar     AscAllocMultipleFreeQueue(PortAddr, uchar, uchar);
STATIC int       AscHostReqRiscHalt(PortAddr);
STATIC int       AscStopQueueExe(PortAddr);
STATIC int       AscSendScsiQueue(ASC_DVC_VAR *,
                    ASC_SCSI_Q * scsiq,
                    uchar n_q_required);
STATIC int       AscPutReadyQueue(ASC_DVC_VAR *,
                    ASC_SCSI_Q *, uchar);
STATIC int       AscPutReadySgListQueue(ASC_DVC_VAR *,
                    ASC_SCSI_Q *, uchar);
STATIC int       AscSetChipSynRegAtID(PortAddr, uchar, uchar);
STATIC int       AscSetRunChipSynRegAtID(PortAddr, uchar, uchar);
STATIC ushort    AscInitLram(ASC_DVC_VAR *);
STATIC ushort    AscInitQLinkVar(ASC_DVC_VAR *);
STATIC int       AscSetLibErrorCode(ASC_DVC_VAR *, ushort);
STATIC int       AscIsrChipHalted(ASC_DVC_VAR *);
STATIC uchar     _AscCopyLramScsiDoneQ(PortAddr, ushort,
                    ASC_QDONE_INFO *, ASC_DCNT);
STATIC int       AscIsrQDone(ASC_DVC_VAR *);
STATIC int       AscCompareString(uchar *, uchar *, int);
#ifdef CONFIG_ISA
STATIC ushort    AscGetEisaChipCfg(PortAddr);
STATIC ASC_DCNT  AscGetEisaProductID(PortAddr);
STATIC PortAddr  AscSearchIOPortAddrEISA(PortAddr);
STATIC PortAddr  AscSearchIOPortAddr11(PortAddr);
STATIC PortAddr  AscSearchIOPortAddr(PortAddr, ushort);
STATIC void      AscSetISAPNPWaitForKey(void);
#endif /* CONFIG_ISA */
STATIC uchar     AscGetChipScsiCtrl(PortAddr);
STATIC uchar     AscSetChipScsiID(PortAddr, uchar);
STATIC uchar     AscGetChipVersion(PortAddr, ushort);
STATIC ushort    AscGetChipBusType(PortAddr);
STATIC ASC_DCNT  AscLoadMicroCode(PortAddr, ushort, uchar *, ushort);
STATIC int       AscFindSignature(PortAddr);
STATIC void      AscToggleIRQAct(PortAddr);
STATIC uchar     AscGetChipIRQ(PortAddr, ushort);
STATIC uchar     AscSetChipIRQ(PortAddr, uchar, ushort);
STATIC ushort    AscGetChipBiosAddress(PortAddr, ushort);
STATIC inline ulong DvcEnterCritical(void);
STATIC inline void DvcLeaveCritical(ulong);
#ifdef CONFIG_PCI
STATIC uchar     DvcReadPCIConfigByte(ASC_DVC_VAR *, ushort);
STATIC void      DvcWritePCIConfigByte(ASC_DVC_VAR *,
                    ushort, uchar);
#endif /* CONFIG_PCI */
STATIC ushort      AscGetChipBiosAddress(PortAddr, ushort);
STATIC void      DvcSleepMilliSecond(ASC_DCNT);
STATIC void      DvcDelayNanoSecond(ASC_DVC_VAR *, ASC_DCNT);
STATIC void      DvcPutScsiQ(PortAddr, ushort, uchar *, int);
STATIC void      DvcGetQinfo(PortAddr, ushort, uchar *, int);
STATIC ushort    AscInitGetConfig(ASC_DVC_VAR *);
STATIC ushort    AscInitSetConfig(ASC_DVC_VAR *);
STATIC ushort    AscInitAsc1000Driver(ASC_DVC_VAR *);
STATIC void      AscAsyncFix(ASC_DVC_VAR *, uchar,
                    ASC_SCSI_INQUIRY *);
STATIC int       AscTagQueuingSafe(ASC_SCSI_INQUIRY *);
STATIC void      AscInquiryHandling(ASC_DVC_VAR *,
                    uchar, ASC_SCSI_INQUIRY *);
STATIC int       AscExeScsiQueue(ASC_DVC_VAR *, ASC_SCSI_Q *);
STATIC int       AscISR(ASC_DVC_VAR *);
STATIC uint      AscGetNumOfFreeQueue(ASC_DVC_VAR *, uchar,
                    uchar);
STATIC int       AscSgListToQueue(int);
#ifdef CONFIG_ISA
STATIC void      AscEnableIsaDma(uchar);
#endif /* CONFIG_ISA */
STATIC ASC_DCNT  AscGetMaxDmaCount(ushort);
static const char *advansys_info(struct Scsi_Host *shp);

/*
 * --- Adv Library Constants and Macros
 */

#define ADV_LIB_VERSION_MAJOR  5
#define ADV_LIB_VERSION_MINOR  14

/*
 * Define Adv Library required special types.
 */

/*
 * Portable Data Types
 *
 * Any instance where a 32-bit long or pointer type is assumed
 * for precision or HW defined structures, the following define
 * types must be used. In Linux the char, short, and int types
 * are all consistent at 8, 16, and 32 bits respectively. Pointers
 * and long types are 64 bits on Alpha and UltraSPARC.
 */
#define ADV_PADDR __u32         /* Physical address data type. */
#define ADV_VADDR __u32         /* Virtual address data type. */
#define ADV_DCNT  __u32         /* Unsigned Data count type. */
#define ADV_SDCNT __s32         /* Signed Data count type. */

/*
 * These macros are used to convert a virtual address to a
 * 32-bit value. This currently can be used on Linux Alpha
 * which uses 64-bit virtual address but a 32-bit bus address.
 * This is likely to break in the future, but doing this now
 * will give us time to change the HW and FW to handle 64-bit
 * addresses.
 */
#define ADV_VADDR_TO_U32   virt_to_bus
#define ADV_U32_TO_VADDR   bus_to_virt

#define AdvPortAddr  void __iomem *     /* Virtual memory address size */

/*
 * Define Adv Library required memory access macros.
 */
#define ADV_MEM_READB(addr) readb(addr)
#define ADV_MEM_READW(addr) readw(addr)
#define ADV_MEM_WRITEB(addr, byte) writeb(byte, addr)
#define ADV_MEM_WRITEW(addr, word) writew(word, addr)
#define ADV_MEM_WRITEDW(addr, dword) writel(dword, addr)

#define ADV_CARRIER_COUNT (ASC_DEF_MAX_HOST_QNG + 15)

/*
 * For wide  boards a CDB length maximum of 16 bytes
 * is supported.
 */
#define ADV_MAX_CDB_LEN     16

/*
 * Define total number of simultaneous maximum element scatter-gather
 * request blocks per wide adapter. ASC_DEF_MAX_HOST_QNG (253) is the
 * maximum number of outstanding commands per wide host adapter. Each
 * command uses one or more ADV_SG_BLOCK each with 15 scatter-gather
 * elements. Allow each command to have at least one ADV_SG_BLOCK structure.
 * This allows about 15 commands to have the maximum 17 ADV_SG_BLOCK
 * structures or 255 scatter-gather elements.
 *
 */
#define ADV_TOT_SG_BLOCK        ASC_DEF_MAX_HOST_QNG

/*
 * Define Adv Library required maximum number of scatter-gather
 * elements per request.
 */
#define ADV_MAX_SG_LIST         255

/* Number of SG blocks needed. */
#define ADV_NUM_SG_BLOCK \
    ((ADV_MAX_SG_LIST + (NO_OF_SG_PER_BLOCK - 1))/NO_OF_SG_PER_BLOCK)

/* Total contiguous memory needed for SG blocks. */
#define ADV_SG_TOTAL_MEM_SIZE \
    (sizeof(ADV_SG_BLOCK) *  ADV_NUM_SG_BLOCK)

#define ADV_PAGE_SIZE PAGE_SIZE

#define ADV_NUM_PAGE_CROSSING \
    ((ADV_SG_TOTAL_MEM_SIZE + (ADV_PAGE_SIZE - 1))/ADV_PAGE_SIZE)

#define ADV_EEP_DVC_CFG_BEGIN           (0x00)
#define ADV_EEP_DVC_CFG_END             (0x15)
#define ADV_EEP_DVC_CTL_BEGIN           (0x16)  /* location of OEM name */
#define ADV_EEP_MAX_WORD_ADDR           (0x1E)

#define ADV_EEP_DELAY_MS                100

#define ADV_EEPROM_BIG_ENDIAN          0x8000   /* EEPROM Bit 15 */
#define ADV_EEPROM_BIOS_ENABLE         0x4000   /* EEPROM Bit 14 */
/*
 * For the ASC3550 Bit 13 is Termination Polarity control bit.
 * For later ICs Bit 13 controls whether the CIS (Card Information
 * Service Section) is loaded from EEPROM.
 */
#define ADV_EEPROM_TERM_POL            0x2000   /* EEPROM Bit 13 */
#define ADV_EEPROM_CIS_LD              0x2000   /* EEPROM Bit 13 */
/*
 * ASC38C1600 Bit 11
 *
 * If EEPROM Bit 11 is 0 for Function 0, then Function 0 will specify
 * INT A in the PCI Configuration Space Int Pin field. If it is 1, then
 * Function 0 will specify INT B.
 *
 * If EEPROM Bit 11 is 0 for Function 1, then Function 1 will specify
 * INT B in the PCI Configuration Space Int Pin field. If it is 1, then
 * Function 1 will specify INT A.
 */
#define ADV_EEPROM_INTAB               0x0800   /* EEPROM Bit 11 */

typedef struct adveep_3550_config
{
                                /* Word Offset, Description */

  ushort cfg_lsw;               /* 00 power up initialization */
                                /*  bit 13 set - Term Polarity Control */
                                /*  bit 14 set - BIOS Enable */
                                /*  bit 15 set - Big Endian Mode */
  ushort cfg_msw;               /* 01 unused      */
  ushort disc_enable;           /* 02 disconnect enable */
  ushort wdtr_able;             /* 03 Wide DTR able */
  ushort sdtr_able;             /* 04 Synchronous DTR able */
  ushort start_motor;           /* 05 send start up motor */
  ushort tagqng_able;           /* 06 tag queuing able */
  ushort bios_scan;             /* 07 BIOS device control */
  ushort scam_tolerant;         /* 08 no scam */

  uchar  adapter_scsi_id;       /* 09 Host Adapter ID */
  uchar  bios_boot_delay;       /*    power up wait */

  uchar  scsi_reset_delay;      /* 10 reset delay */
  uchar  bios_id_lun;           /*    first boot device scsi id & lun */
                                /*    high nibble is lun */
                                /*    low nibble is scsi id */

  uchar  termination;           /* 11 0 - automatic */
                                /*    1 - low off / high off */
                                /*    2 - low off / high on */
                                /*    3 - low on  / high on */
                                /*    There is no low on  / high off */

  uchar  reserved1;             /*    reserved byte (not used) */

  ushort bios_ctrl;             /* 12 BIOS control bits */
                                /*  bit 0  BIOS don't act as initiator. */
                                /*  bit 1  BIOS > 1 GB support */
                                /*  bit 2  BIOS > 2 Disk Support */
                                /*  bit 3  BIOS don't support removables */
                                /*  bit 4  BIOS support bootable CD */
                                /*  bit 5  BIOS scan enabled */
                                /*  bit 6  BIOS support multiple LUNs */
                                /*  bit 7  BIOS display of message */
                                /*  bit 8  SCAM disabled */
                                /*  bit 9  Reset SCSI bus during init. */
                                /*  bit 10 */
                                /*  bit 11 No verbose initialization. */
                                /*  bit 12 SCSI parity enabled */
                                /*  bit 13 */
                                /*  bit 14 */
                                /*  bit 15 */
  ushort  ultra_able;           /* 13 ULTRA speed able */
  ushort  reserved2;            /* 14 reserved */
  uchar   max_host_qng;         /* 15 maximum host queuing */
  uchar   max_dvc_qng;          /*    maximum per device queuing */
  ushort  dvc_cntl;             /* 16 control bit for driver */
  ushort  bug_fix;              /* 17 control bit for bug fix */
  ushort  serial_number_word1;  /* 18 Board serial number word 1 */
  ushort  serial_number_word2;  /* 19 Board serial number word 2 */
  ushort  serial_number_word3;  /* 20 Board serial number word 3 */
  ushort  check_sum;            /* 21 EEP check sum */
  uchar   oem_name[16];         /* 22 OEM name */
  ushort  dvc_err_code;         /* 30 last device driver error code */
  ushort  adv_err_code;         /* 31 last uc and Adv Lib error code */
  ushort  adv_err_addr;         /* 32 last uc error address */
  ushort  saved_dvc_err_code;   /* 33 saved last dev. driver error code   */
  ushort  saved_adv_err_code;   /* 34 saved last uc and Adv Lib error code */
  ushort  saved_adv_err_addr;   /* 35 saved last uc error address         */
  ushort  num_of_err;           /* 36 number of error */
} ADVEEP_3550_CONFIG;

typedef struct adveep_38C0800_config
{
                                /* Word Offset, Description */

  ushort cfg_lsw;               /* 00 power up initialization */
                                /*  bit 13 set - Load CIS */
                                /*  bit 14 set - BIOS Enable */
                                /*  bit 15 set - Big Endian Mode */
  ushort cfg_msw;               /* 01 unused      */
  ushort disc_enable;           /* 02 disconnect enable */
  ushort wdtr_able;             /* 03 Wide DTR able */
  ushort sdtr_speed1;           /* 04 SDTR Speed TID 0-3 */
  ushort start_motor;           /* 05 send start up motor */
  ushort tagqng_able;           /* 06 tag queuing able */
  ushort bios_scan;             /* 07 BIOS device control */
  ushort scam_tolerant;         /* 08 no scam */

  uchar  adapter_scsi_id;       /* 09 Host Adapter ID */
  uchar  bios_boot_delay;       /*    power up wait */

  uchar  scsi_reset_delay;      /* 10 reset delay */
  uchar  bios_id_lun;           /*    first boot device scsi id & lun */
                                /*    high nibble is lun */
                                /*    low nibble is scsi id */

  uchar  termination_se;        /* 11 0 - automatic */
                                /*    1 - low off / high off */
                                /*    2 - low off / high on */
                                /*    3 - low on  / high on */
                                /*    There is no low on  / high off */

  uchar  termination_lvd;       /* 11 0 - automatic */
                                /*    1 - low off / high off */
                                /*    2 - low off / high on */
                                /*    3 - low on  / high on */
                                /*    There is no low on  / high off */

  ushort bios_ctrl;             /* 12 BIOS control bits */
                                /*  bit 0  BIOS don't act as initiator. */
                                /*  bit 1  BIOS > 1 GB support */
                                /*  bit 2  BIOS > 2 Disk Support */
                                /*  bit 3  BIOS don't support removables */
                                /*  bit 4  BIOS support bootable CD */
                                /*  bit 5  BIOS scan enabled */
                                /*  bit 6  BIOS support multiple LUNs */
                                /*  bit 7  BIOS display of message */
                                /*  bit 8  SCAM disabled */
                                /*  bit 9  Reset SCSI bus during init. */
                                /*  bit 10 */
                                /*  bit 11 No verbose initialization. */
                                /*  bit 12 SCSI parity enabled */
                                /*  bit 13 */
                                /*  bit 14 */
                                /*  bit 15 */
  ushort  sdtr_speed2;          /* 13 SDTR speed TID 4-7 */
  ushort  sdtr_speed3;          /* 14 SDTR speed TID 8-11 */
  uchar   max_host_qng;         /* 15 maximum host queueing */
  uchar   max_dvc_qng;          /*    maximum per device queuing */
  ushort  dvc_cntl;             /* 16 control bit for driver */
  ushort  sdtr_speed4;          /* 17 SDTR speed 4 TID 12-15 */
  ushort  serial_number_word1;  /* 18 Board serial number word 1 */
  ushort  serial_number_word2;  /* 19 Board serial number word 2 */
  ushort  serial_number_word3;  /* 20 Board serial number word 3 */
  ushort  check_sum;            /* 21 EEP check sum */
  uchar   oem_name[16];         /* 22 OEM name */
  ushort  dvc_err_code;         /* 30 last device driver error code */
  ushort  adv_err_code;         /* 31 last uc and Adv Lib error code */
  ushort  adv_err_addr;         /* 32 last uc error address */
  ushort  saved_dvc_err_code;   /* 33 saved last dev. driver error code   */
  ushort  saved_adv_err_code;   /* 34 saved last uc and Adv Lib error code */
  ushort  saved_adv_err_addr;   /* 35 saved last uc error address         */
  ushort  reserved36;           /* 36 reserved */
  ushort  reserved37;           /* 37 reserved */
  ushort  reserved38;           /* 38 reserved */
  ushort  reserved39;           /* 39 reserved */
  ushort  reserved40;           /* 40 reserved */
  ushort  reserved41;           /* 41 reserved */
  ushort  reserved42;           /* 42 reserved */
  ushort  reserved43;           /* 43 reserved */
  ushort  reserved44;           /* 44 reserved */
  ushort  reserved45;           /* 45 reserved */
  ushort  reserved46;           /* 46 reserved */
  ushort  reserved47;           /* 47 reserved */
  ushort  reserved48;           /* 48 reserved */
  ushort  reserved49;           /* 49 reserved */
  ushort  reserved50;           /* 50 reserved */
  ushort  reserved51;           /* 51 reserved */
  ushort  reserved52;           /* 52 reserved */
  ushort  reserved53;           /* 53 reserved */
  ushort  reserved54;           /* 54 reserved */
  ushort  reserved55;           /* 55 reserved */
  ushort  cisptr_lsw;           /* 56 CIS PTR LSW */
  ushort  cisprt_msw;           /* 57 CIS PTR MSW */
  ushort  subsysvid;            /* 58 SubSystem Vendor ID */
  ushort  subsysid;             /* 59 SubSystem ID */
  ushort  reserved60;           /* 60 reserved */
  ushort  reserved61;           /* 61 reserved */
  ushort  reserved62;           /* 62 reserved */
  ushort  reserved63;           /* 63 reserved */
} ADVEEP_38C0800_CONFIG;

typedef struct adveep_38C1600_config
{
                                /* Word Offset, Description */

  ushort cfg_lsw;               /* 00 power up initialization */
                                /*  bit 11 set - Func. 0 INTB, Func. 1 INTA */
                                /*       clear - Func. 0 INTA, Func. 1 INTB */
                                /*  bit 13 set - Load CIS */
                                /*  bit 14 set - BIOS Enable */
                                /*  bit 15 set - Big Endian Mode */
  ushort cfg_msw;               /* 01 unused */
  ushort disc_enable;           /* 02 disconnect enable */
  ushort wdtr_able;             /* 03 Wide DTR able */
  ushort sdtr_speed1;           /* 04 SDTR Speed TID 0-3 */
  ushort start_motor;           /* 05 send start up motor */
  ushort tagqng_able;           /* 06 tag queuing able */
  ushort bios_scan;             /* 07 BIOS device control */
  ushort scam_tolerant;         /* 08 no scam */

  uchar  adapter_scsi_id;       /* 09 Host Adapter ID */
  uchar  bios_boot_delay;       /*    power up wait */

  uchar  scsi_reset_delay;      /* 10 reset delay */
  uchar  bios_id_lun;           /*    first boot device scsi id & lun */
                                /*    high nibble is lun */
                                /*    low nibble is scsi id */

  uchar  termination_se;        /* 11 0 - automatic */
                                /*    1 - low off / high off */
                                /*    2 - low off / high on */
                                /*    3 - low on  / high on */
                                /*    There is no low on  / high off */

  uchar  termination_lvd;       /* 11 0 - automatic */
                                /*    1 - low off / high off */
                                /*    2 - low off / high on */
                                /*    3 - low on  / high on */
                                /*    There is no low on  / high off */

  ushort bios_ctrl;             /* 12 BIOS control bits */
                                /*  bit 0  BIOS don't act as initiator. */
                                /*  bit 1  BIOS > 1 GB support */
                                /*  bit 2  BIOS > 2 Disk Support */
                                /*  bit 3  BIOS don't support removables */
                                /*  bit 4  BIOS support bootable CD */
                                /*  bit 5  BIOS scan enabled */
                                /*  bit 6  BIOS support multiple LUNs */
                                /*  bit 7  BIOS display of message */
                                /*  bit 8  SCAM disabled */
                                /*  bit 9  Reset SCSI bus during init. */
                                /*  bit 10 Basic Integrity Checking disabled */
                                /*  bit 11 No verbose initialization. */
                                /*  bit 12 SCSI parity enabled */
                                /*  bit 13 AIPP (Asyn. Info. Ph. Prot.) dis. */
                                /*  bit 14 */
                                /*  bit 15 */
  ushort  sdtr_speed2;          /* 13 SDTR speed TID 4-7 */
  ushort  sdtr_speed3;          /* 14 SDTR speed TID 8-11 */
  uchar   max_host_qng;         /* 15 maximum host queueing */
  uchar   max_dvc_qng;          /*    maximum per device queuing */
  ushort  dvc_cntl;             /* 16 control bit for driver */
  ushort  sdtr_speed4;          /* 17 SDTR speed 4 TID 12-15 */
  ushort  serial_number_word1;  /* 18 Board serial number word 1 */
  ushort  serial_number_word2;  /* 19 Board serial number word 2 */
  ushort  serial_number_word3;  /* 20 Board serial number word 3 */
  ushort  check_sum;            /* 21 EEP check sum */
  uchar   oem_name[16];         /* 22 OEM name */
  ushort  dvc_err_code;         /* 30 last device driver error code */
  ushort  adv_err_code;         /* 31 last uc and Adv Lib error code */
  ushort  adv_err_addr;         /* 32 last uc error address */
  ushort  saved_dvc_err_code;   /* 33 saved last dev. driver error code   */
  ushort  saved_adv_err_code;   /* 34 saved last uc and Adv Lib error code */
  ushort  saved_adv_err_addr;   /* 35 saved last uc error address         */
  ushort  reserved36;           /* 36 reserved */
  ushort  reserved37;           /* 37 reserved */
  ushort  reserved38;           /* 38 reserved */
  ushort  reserved39;           /* 39 reserved */
  ushort  reserved40;           /* 40 reserved */
  ushort  reserved41;           /* 41 reserved */
  ushort  reserved42;           /* 42 reserved */
  ushort  reserved43;           /* 43 reserved */
  ushort  reserved44;           /* 44 reserved */
  ushort  reserved45;           /* 45 reserved */
  ushort  reserved46;           /* 46 reserved */
  ushort  reserved47;           /* 47 reserved */
  ushort  reserved48;           /* 48 reserved */
  ushort  reserved49;           /* 49 reserved */
  ushort  reserved50;           /* 50 reserved */
  ushort  reserved51;           /* 51 reserved */
  ushort  reserved52;           /* 52 reserved */
  ushort  reserved53;           /* 53 reserved */
  ushort  reserved54;           /* 54 reserved */
  ushort  reserved55;           /* 55 reserved */
  ushort  cisptr_lsw;           /* 56 CIS PTR LSW */
  ushort  cisprt_msw;           /* 57 CIS PTR MSW */
  ushort  subsysvid;            /* 58 SubSystem Vendor ID */
  ushort  subsysid;             /* 59 SubSystem ID */
  ushort  reserved60;           /* 60 reserved */
  ushort  reserved61;           /* 61 reserved */
  ushort  reserved62;           /* 62 reserved */
  ushort  reserved63;           /* 63 reserved */
} ADVEEP_38C1600_CONFIG;

/*
 * EEPROM Commands
 */
#define ASC_EEP_CMD_DONE             0x0200
#define ASC_EEP_CMD_DONE_ERR         0x0001

/* cfg_word */
#define EEP_CFG_WORD_BIG_ENDIAN      0x8000

/* bios_ctrl */
#define BIOS_CTRL_BIOS               0x0001
#define BIOS_CTRL_EXTENDED_XLAT      0x0002
#define BIOS_CTRL_GT_2_DISK          0x0004
#define BIOS_CTRL_BIOS_REMOVABLE     0x0008
#define BIOS_CTRL_BOOTABLE_CD        0x0010
#define BIOS_CTRL_MULTIPLE_LUN       0x0040
#define BIOS_CTRL_DISPLAY_MSG        0x0080
#define BIOS_CTRL_NO_SCAM            0x0100
#define BIOS_CTRL_RESET_SCSI_BUS     0x0200
#define BIOS_CTRL_INIT_VERBOSE       0x0800
#define BIOS_CTRL_SCSI_PARITY        0x1000
#define BIOS_CTRL_AIPP_DIS           0x2000

#define ADV_3550_MEMSIZE   0x2000       /* 8 KB Internal Memory */
#define ADV_3550_IOLEN     0x40         /* I/O Port Range in bytes */

#define ADV_38C0800_MEMSIZE  0x4000     /* 16 KB Internal Memory */
#define ADV_38C0800_IOLEN    0x100      /* I/O Port Range in bytes */

/*
 * XXX - Since ASC38C1600 Rev.3 has a local RAM failure issue, there is
 * a special 16K Adv Library and Microcode version. After the issue is
 * resolved, should restore 32K support.
 *
 * #define ADV_38C1600_MEMSIZE  0x8000L   * 32 KB Internal Memory *
 */
#define ADV_38C1600_MEMSIZE  0x4000   /* 16 KB Internal Memory */
#define ADV_38C1600_IOLEN    0x100     /* I/O Port Range 256 bytes */
#define ADV_38C1600_MEMLEN   0x1000    /* Memory Range 4KB bytes */

/*
 * Byte I/O register address from base of 'iop_base'.
 */
#define IOPB_INTR_STATUS_REG    0x00
#define IOPB_CHIP_ID_1          0x01
#define IOPB_INTR_ENABLES       0x02
#define IOPB_CHIP_TYPE_REV      0x03
#define IOPB_RES_ADDR_4         0x04
#define IOPB_RES_ADDR_5         0x05
#define IOPB_RAM_DATA           0x06
#define IOPB_RES_ADDR_7         0x07
#define IOPB_FLAG_REG           0x08
#define IOPB_RES_ADDR_9         0x09
#define IOPB_RISC_CSR           0x0A
#define IOPB_RES_ADDR_B         0x0B
#define IOPB_RES_ADDR_C         0x0C
#define IOPB_RES_ADDR_D         0x0D
#define IOPB_SOFT_OVER_WR       0x0E
#define IOPB_RES_ADDR_F         0x0F
#define IOPB_MEM_CFG            0x10
#define IOPB_RES_ADDR_11        0x11
#define IOPB_GPIO_DATA          0x12
#define IOPB_RES_ADDR_13        0x13
#define IOPB_FLASH_PAGE         0x14
#define IOPB_RES_ADDR_15        0x15
#define IOPB_GPIO_CNTL          0x16
#define IOPB_RES_ADDR_17        0x17
#define IOPB_FLASH_DATA         0x18
#define IOPB_RES_ADDR_19        0x19
#define IOPB_RES_ADDR_1A        0x1A
#define IOPB_RES_ADDR_1B        0x1B
#define IOPB_RES_ADDR_1C        0x1C
#define IOPB_RES_ADDR_1D        0x1D
#define IOPB_RES_ADDR_1E        0x1E
#define IOPB_RES_ADDR_1F        0x1F
#define IOPB_DMA_CFG0           0x20
#define IOPB_DMA_CFG1           0x21
#define IOPB_TICKLE             0x22
#define IOPB_DMA_REG_WR         0x23
#define IOPB_SDMA_STATUS        0x24
#define IOPB_SCSI_BYTE_CNT      0x25
#define IOPB_HOST_BYTE_CNT      0x26
#define IOPB_BYTE_LEFT_TO_XFER  0x27
#define IOPB_BYTE_TO_XFER_0     0x28
#define IOPB_BYTE_TO_XFER_1     0x29
#define IOPB_BYTE_TO_XFER_2     0x2A
#define IOPB_BYTE_TO_XFER_3     0x2B
#define IOPB_ACC_GRP            0x2C
#define IOPB_RES_ADDR_2D        0x2D
#define IOPB_DEV_ID             0x2E
#define IOPB_RES_ADDR_2F        0x2F
#define IOPB_SCSI_DATA          0x30
#define IOPB_RES_ADDR_31        0x31
#define IOPB_RES_ADDR_32        0x32
#define IOPB_SCSI_DATA_HSHK     0x33
#define IOPB_SCSI_CTRL          0x34
#define IOPB_RES_ADDR_35        0x35
#define IOPB_RES_ADDR_36        0x36
#define IOPB_RES_ADDR_37        0x37
#define IOPB_RAM_BIST           0x38
#define IOPB_PLL_TEST           0x39
#define IOPB_PCI_INT_CFG        0x3A
#define IOPB_RES_ADDR_3B        0x3B
#define IOPB_RFIFO_CNT          0x3C
#define IOPB_RES_ADDR_3D        0x3D
#define IOPB_RES_ADDR_3E        0x3E
#define IOPB_RES_ADDR_3F        0x3F

/*
 * Word I/O register address from base of 'iop_base'.
 */
#define IOPW_CHIP_ID_0          0x00  /* CID0  */
#define IOPW_CTRL_REG           0x02  /* CC    */
#define IOPW_RAM_ADDR           0x04  /* LA    */
#define IOPW_RAM_DATA           0x06  /* LD    */
#define IOPW_RES_ADDR_08        0x08
#define IOPW_RISC_CSR           0x0A  /* CSR   */
#define IOPW_SCSI_CFG0          0x0C  /* CFG0  */
#define IOPW_SCSI_CFG1          0x0E  /* CFG1  */
#define IOPW_RES_ADDR_10        0x10
#define IOPW_SEL_MASK           0x12  /* SM    */
#define IOPW_RES_ADDR_14        0x14
#define IOPW_FLASH_ADDR         0x16  /* FA    */
#define IOPW_RES_ADDR_18        0x18
#define IOPW_EE_CMD             0x1A  /* EC    */
#define IOPW_EE_DATA            0x1C  /* ED    */
#define IOPW_SFIFO_CNT          0x1E  /* SFC   */
#define IOPW_RES_ADDR_20        0x20
#define IOPW_Q_BASE             0x22  /* QB    */
#define IOPW_QP                 0x24  /* QP    */
#define IOPW_IX                 0x26  /* IX    */
#define IOPW_SP                 0x28  /* SP    */
#define IOPW_PC                 0x2A  /* PC    */
#define IOPW_RES_ADDR_2C        0x2C
#define IOPW_RES_ADDR_2E        0x2E
#define IOPW_SCSI_DATA          0x30  /* SD    */
#define IOPW_SCSI_DATA_HSHK     0x32  /* SDH   */
#define IOPW_SCSI_CTRL          0x34  /* SC    */
#define IOPW_HSHK_CFG           0x36  /* HCFG  */
#define IOPW_SXFR_STATUS        0x36  /* SXS   */
#define IOPW_SXFR_CNTL          0x38  /* SXL   */
#define IOPW_SXFR_CNTH          0x3A  /* SXH   */
#define IOPW_RES_ADDR_3C        0x3C
#define IOPW_RFIFO_DATA         0x3E  /* RFD   */

/*
 * Doubleword I/O register address from base of 'iop_base'.
 */
#define IOPDW_RES_ADDR_0         0x00
#define IOPDW_RAM_DATA           0x04
#define IOPDW_RES_ADDR_8         0x08
#define IOPDW_RES_ADDR_C         0x0C
#define IOPDW_RES_ADDR_10        0x10
#define IOPDW_COMMA              0x14
#define IOPDW_COMMB              0x18
#define IOPDW_RES_ADDR_1C        0x1C
#define IOPDW_SDMA_ADDR0         0x20
#define IOPDW_SDMA_ADDR1         0x24
#define IOPDW_SDMA_COUNT         0x28
#define IOPDW_SDMA_ERROR         0x2C
#define IOPDW_RDMA_ADDR0         0x30
#define IOPDW_RDMA_ADDR1         0x34
#define IOPDW_RDMA_COUNT         0x38
#define IOPDW_RDMA_ERROR         0x3C

#define ADV_CHIP_ID_BYTE         0x25
#define ADV_CHIP_ID_WORD         0x04C1

#define ADV_SC_SCSI_BUS_RESET    0x2000

#define ADV_INTR_ENABLE_HOST_INTR                   0x01
#define ADV_INTR_ENABLE_SEL_INTR                    0x02
#define ADV_INTR_ENABLE_DPR_INTR                    0x04
#define ADV_INTR_ENABLE_RTA_INTR                    0x08
#define ADV_INTR_ENABLE_RMA_INTR                    0x10
#define ADV_INTR_ENABLE_RST_INTR                    0x20
#define ADV_INTR_ENABLE_DPE_INTR                    0x40
#define ADV_INTR_ENABLE_GLOBAL_INTR                 0x80

#define ADV_INTR_STATUS_INTRA            0x01
#define ADV_INTR_STATUS_INTRB            0x02
#define ADV_INTR_STATUS_INTRC            0x04

#define ADV_RISC_CSR_STOP           (0x0000)
#define ADV_RISC_TEST_COND          (0x2000)
#define ADV_RISC_CSR_RUN            (0x4000)
#define ADV_RISC_CSR_SINGLE_STEP    (0x8000)

#define ADV_CTRL_REG_HOST_INTR      0x0100
#define ADV_CTRL_REG_SEL_INTR       0x0200
#define ADV_CTRL_REG_DPR_INTR       0x0400
#define ADV_CTRL_REG_RTA_INTR       0x0800
#define ADV_CTRL_REG_RMA_INTR       0x1000
#define ADV_CTRL_REG_RES_BIT14      0x2000
#define ADV_CTRL_REG_DPE_INTR       0x4000
#define ADV_CTRL_REG_POWER_DONE     0x8000
#define ADV_CTRL_REG_ANY_INTR       0xFF00

#define ADV_CTRL_REG_CMD_RESET             0x00C6
#define ADV_CTRL_REG_CMD_WR_IO_REG         0x00C5
#define ADV_CTRL_REG_CMD_RD_IO_REG         0x00C4
#define ADV_CTRL_REG_CMD_WR_PCI_CFG_SPACE  0x00C3
#define ADV_CTRL_REG_CMD_RD_PCI_CFG_SPACE  0x00C2

#define ADV_TICKLE_NOP                      0x00
#define ADV_TICKLE_A                        0x01
#define ADV_TICKLE_B                        0x02
#define ADV_TICKLE_C                        0x03

#define ADV_SCSI_CTRL_RSTOUT        0x2000

#define AdvIsIntPending(port) \
    (AdvReadWordRegister(port, IOPW_CTRL_REG) & ADV_CTRL_REG_HOST_INTR)

/*
 * SCSI_CFG0 Register bit definitions
 */
#define TIMER_MODEAB    0xC000  /* Watchdog, Second, and Select. Timer Ctrl. */
#define PARITY_EN       0x2000  /* Enable SCSI Parity Error detection */
#define EVEN_PARITY     0x1000  /* Select Even Parity */
#define WD_LONG         0x0800  /* Watchdog Interval, 1: 57 min, 0: 13 sec */
#define QUEUE_128       0x0400  /* Queue Size, 1: 128 byte, 0: 64 byte */
#define PRIM_MODE       0x0100  /* Primitive SCSI mode */
#define SCAM_EN         0x0080  /* Enable SCAM selection */
#define SEL_TMO_LONG    0x0040  /* Sel/Resel Timeout, 1: 400 ms, 0: 1.6 ms */
#define CFRM_ID         0x0020  /* SCAM id sel. confirm., 1: fast, 0: 6.4 ms */
#define OUR_ID_EN       0x0010  /* Enable OUR_ID bits */
#define OUR_ID          0x000F  /* SCSI ID */

/*
 * SCSI_CFG1 Register bit definitions
 */
#define BIG_ENDIAN      0x8000  /* Enable Big Endian Mode MIO:15, EEP:15 */
#define TERM_POL        0x2000  /* Terminator Polarity Ctrl. MIO:13, EEP:13 */
#define SLEW_RATE       0x1000  /* SCSI output buffer slew rate */
#define FILTER_SEL      0x0C00  /* Filter Period Selection */
#define  FLTR_DISABLE    0x0000  /* Input Filtering Disabled */
#define  FLTR_11_TO_20NS 0x0800  /* Input Filtering 11ns to 20ns */
#define  FLTR_21_TO_39NS 0x0C00  /* Input Filtering 21ns to 39ns */
#define ACTIVE_DBL      0x0200  /* Disable Active Negation */
#define DIFF_MODE       0x0100  /* SCSI differential Mode (Read-Only) */
#define DIFF_SENSE      0x0080  /* 1: No SE cables, 0: SE cable (Read-Only) */
#define TERM_CTL_SEL    0x0040  /* Enable TERM_CTL_H and TERM_CTL_L */
#define TERM_CTL        0x0030  /* External SCSI Termination Bits */
#define  TERM_CTL_H      0x0020  /* Enable External SCSI Upper Termination */
#define  TERM_CTL_L      0x0010  /* Enable External SCSI Lower Termination */
#define CABLE_DETECT    0x000F  /* External SCSI Cable Connection Status */

/*
 * Addendum for ASC-38C0800 Chip
 *
 * The ASC-38C1600 Chip uses the same definitions except that the
 * bus mode override bits [12:10] have been moved to byte register
 * offset 0xE (IOPB_SOFT_OVER_WR) bits [12:10]. The [12:10] bits in
 * SCSI_CFG1 are read-only and always available. Bit 14 (DIS_TERM_DRV)
 * is not needed. The [12:10] bits in IOPB_SOFT_OVER_WR are write-only.
 * Also each ASC-38C1600 function or channel uses only cable bits [5:4]
 * and [1:0]. Bits [14], [7:6], [3:2] are unused.
 */
#define DIS_TERM_DRV    0x4000  /* 1: Read c_det[3:0], 0: cannot read */
#define HVD_LVD_SE      0x1C00  /* Device Detect Bits */
#define  HVD             0x1000  /* HVD Device Detect */
#define  LVD             0x0800  /* LVD Device Detect */
#define  SE              0x0400  /* SE Device Detect */
#define TERM_LVD        0x00C0  /* LVD Termination Bits */
#define  TERM_LVD_HI     0x0080  /* Enable LVD Upper Termination */
#define  TERM_LVD_LO     0x0040  /* Enable LVD Lower Termination */
#define TERM_SE         0x0030  /* SE Termination Bits */
#define  TERM_SE_HI      0x0020  /* Enable SE Upper Termination */
#define  TERM_SE_LO      0x0010  /* Enable SE Lower Termination */
#define C_DET_LVD       0x000C  /* LVD Cable Detect Bits */
#define  C_DET3          0x0008  /* Cable Detect for LVD External Wide */
#define  C_DET2          0x0004  /* Cable Detect for LVD Internal Wide */
#define C_DET_SE        0x0003  /* SE Cable Detect Bits */
#define  C_DET1          0x0002  /* Cable Detect for SE Internal Wide */
#define  C_DET0          0x0001  /* Cable Detect for SE Internal Narrow */


#define CABLE_ILLEGAL_A 0x7
    /* x 0 0 0  | on  on | Illegal (all 3 connectors are used) */

#define CABLE_ILLEGAL_B 0xB
    /* 0 x 0 0  | on  on | Illegal (all 3 connectors are used) */

/*
 * MEM_CFG Register bit definitions
 */
#define BIOS_EN         0x40    /* BIOS Enable MIO:14,EEP:14 */
#define FAST_EE_CLK     0x20    /* Diagnostic Bit */
#define RAM_SZ          0x1C    /* Specify size of RAM to RISC */
#define  RAM_SZ_2KB      0x00    /* 2 KB */
#define  RAM_SZ_4KB      0x04    /* 4 KB */
#define  RAM_SZ_8KB      0x08    /* 8 KB */
#define  RAM_SZ_16KB     0x0C    /* 16 KB */
#define  RAM_SZ_32KB     0x10    /* 32 KB */
#define  RAM_SZ_64KB     0x14    /* 64 KB */

/*
 * DMA_CFG0 Register bit definitions
 *
 * This register is only accessible to the host.
 */
#define BC_THRESH_ENB   0x80    /* PCI DMA Start Conditions */
#define FIFO_THRESH     0x70    /* PCI DMA FIFO Threshold */
#define  FIFO_THRESH_16B  0x00   /* 16 bytes */
#define  FIFO_THRESH_32B  0x20   /* 32 bytes */
#define  FIFO_THRESH_48B  0x30   /* 48 bytes */
#define  FIFO_THRESH_64B  0x40   /* 64 bytes */
#define  FIFO_THRESH_80B  0x50   /* 80 bytes (default) */
#define  FIFO_THRESH_96B  0x60   /* 96 bytes */
#define  FIFO_THRESH_112B 0x70   /* 112 bytes */
#define START_CTL       0x0C    /* DMA start conditions */
#define  START_CTL_TH    0x00    /* Wait threshold level (default) */
#define  START_CTL_ID    0x04    /* Wait SDMA/SBUS idle */
#define  START_CTL_THID  0x08    /* Wait threshold and SDMA/SBUS idle */
#define  START_CTL_EMFU  0x0C    /* Wait SDMA FIFO empty/full */
#define READ_CMD        0x03    /* Memory Read Method */
#define  READ_CMD_MR     0x00    /* Memory Read */
#define  READ_CMD_MRL    0x02    /* Memory Read Long */
#define  READ_CMD_MRM    0x03    /* Memory Read Multiple (default) */

/*
 * ASC-38C0800 RAM BIST Register bit definitions
 */
#define RAM_TEST_MODE         0x80
#define PRE_TEST_MODE         0x40
#define NORMAL_MODE           0x00
#define RAM_TEST_DONE         0x10
#define RAM_TEST_STATUS       0x0F
#define  RAM_TEST_HOST_ERROR   0x08
#define  RAM_TEST_INTRAM_ERROR 0x04
#define  RAM_TEST_RISC_ERROR   0x02
#define  RAM_TEST_SCSI_ERROR   0x01
#define  RAM_TEST_SUCCESS      0x00
#define PRE_TEST_VALUE        0x05
#define NORMAL_VALUE          0x00

/*
 * ASC38C1600 Definitions
 *
 * IOPB_PCI_INT_CFG Bit Field Definitions
 */

#define INTAB_LD        0x80    /* Value loaded from EEPROM Bit 11. */

/*
 * Bit 1 can be set to change the interrupt for the Function to operate in
 * Totem Pole mode. By default Bit 1 is 0 and the interrupt operates in
 * Open Drain mode. Both functions of the ASC38C1600 must be set to the same
 * mode, otherwise the operating mode is undefined.
 */
#define TOTEMPOLE       0x02

/*
 * Bit 0 can be used to change the Int Pin for the Function. The value is
 * 0 by default for both Functions with Function 0 using INT A and Function
 * B using INT B. For Function 0 if set, INT B is used. For Function 1 if set,
 * INT A is used.
 *
 * EEPROM Word 0 Bit 11 for each Function may change the initial Int Pin
 * value specified in the PCI Configuration Space.
 */
#define INTAB           0x01

/* a_advlib.h */

/*
 * Adv Library Status Definitions
 */
#define ADV_TRUE        1
#define ADV_FALSE       0
#define ADV_NOERROR     1
#define ADV_SUCCESS     1
#define ADV_BUSY        0
#define ADV_ERROR       (-1)


/*
 * ADV_DVC_VAR 'warn_code' values
 */
#define ASC_WARN_BUSRESET_ERROR         0x0001 /* SCSI Bus Reset error */
#define ASC_WARN_EEPROM_CHKSUM          0x0002 /* EEP check sum error */
#define ASC_WARN_EEPROM_TERMINATION     0x0004 /* EEP termination bad field */
#define ASC_WARN_SET_PCI_CONFIG_SPACE   0x0080 /* PCI config space set error */
#define ASC_WARN_ERROR                  0xFFFF /* ADV_ERROR return */

#define ADV_MAX_TID                     15 /* max. target identifier */
#define ADV_MAX_LUN                     7  /* max. logical unit number */

/*
 * Error code values are set in ADV_DVC_VAR 'err_code'.
 */
#define ASC_IERR_WRITE_EEPROM       0x0001 /* write EEPROM error */
#define ASC_IERR_MCODE_CHKSUM       0x0002 /* micro code check sum error */
#define ASC_IERR_NO_CARRIER         0x0004 /* No more carrier memory. */
#define ASC_IERR_START_STOP_CHIP    0x0008 /* start/stop chip failed */
#define ASC_IERR_CHIP_VERSION       0x0040 /* wrong chip version */
#define ASC_IERR_SET_SCSI_ID        0x0080 /* set SCSI ID failed */
#define ASC_IERR_HVD_DEVICE         0x0100 /* HVD attached to LVD connector. */
#define ASC_IERR_BAD_SIGNATURE      0x0200 /* signature not found */
#define ASC_IERR_ILLEGAL_CONNECTION 0x0400 /* Illegal cable connection */
#define ASC_IERR_SINGLE_END_DEVICE  0x0800 /* Single-end used w/differential */
#define ASC_IERR_REVERSED_CABLE     0x1000 /* Narrow flat cable reversed */
#define ASC_IERR_BIST_PRE_TEST      0x2000 /* BIST pre-test error */
#define ASC_IERR_BIST_RAM_TEST      0x4000 /* BIST RAM test error */
#define ASC_IERR_BAD_CHIPTYPE       0x8000 /* Invalid 'chip_type' setting. */

/*
 * Fixed locations of microcode operating variables.
 */
#define ASC_MC_CODE_BEGIN_ADDR          0x0028 /* microcode start address */
#define ASC_MC_CODE_END_ADDR            0x002A /* microcode end address */
#define ASC_MC_CODE_CHK_SUM             0x002C /* microcode code checksum */
#define ASC_MC_VERSION_DATE             0x0038 /* microcode version */
#define ASC_MC_VERSION_NUM              0x003A /* microcode number */
#define ASC_MC_BIOSMEM                  0x0040 /* BIOS RISC Memory Start */
#define ASC_MC_BIOSLEN                  0x0050 /* BIOS RISC Memory Length */
#define ASC_MC_BIOS_SIGNATURE           0x0058 /* BIOS Signature 0x55AA */
#define ASC_MC_BIOS_VERSION             0x005A /* BIOS Version (2 bytes) */
#define ASC_MC_SDTR_SPEED1              0x0090 /* SDTR Speed for TID 0-3 */
#define ASC_MC_SDTR_SPEED2              0x0092 /* SDTR Speed for TID 4-7 */
#define ASC_MC_SDTR_SPEED3              0x0094 /* SDTR Speed for TID 8-11 */
#define ASC_MC_SDTR_SPEED4              0x0096 /* SDTR Speed for TID 12-15 */
#define ASC_MC_CHIP_TYPE                0x009A
#define ASC_MC_INTRB_CODE               0x009B
#define ASC_MC_WDTR_ABLE                0x009C
#define ASC_MC_SDTR_ABLE                0x009E
#define ASC_MC_TAGQNG_ABLE              0x00A0
#define ASC_MC_DISC_ENABLE              0x00A2
#define ASC_MC_IDLE_CMD_STATUS          0x00A4
#define ASC_MC_IDLE_CMD                 0x00A6
#define ASC_MC_IDLE_CMD_PARAMETER       0x00A8
#define ASC_MC_DEFAULT_SCSI_CFG0        0x00AC
#define ASC_MC_DEFAULT_SCSI_CFG1        0x00AE
#define ASC_MC_DEFAULT_MEM_CFG          0x00B0
#define ASC_MC_DEFAULT_SEL_MASK         0x00B2
#define ASC_MC_SDTR_DONE                0x00B6
#define ASC_MC_NUMBER_OF_QUEUED_CMD     0x00C0
#define ASC_MC_NUMBER_OF_MAX_CMD        0x00D0
#define ASC_MC_DEVICE_HSHK_CFG_TABLE    0x0100
#define ASC_MC_CONTROL_FLAG             0x0122 /* Microcode control flag. */
#define ASC_MC_WDTR_DONE                0x0124
#define ASC_MC_CAM_MODE_MASK            0x015E /* CAM mode TID bitmask. */
#define ASC_MC_ICQ                      0x0160
#define ASC_MC_IRQ                      0x0164
#define ASC_MC_PPR_ABLE                 0x017A

/*
 * BIOS LRAM variable absolute offsets.
 */
#define BIOS_CODESEG    0x54
#define BIOS_CODELEN    0x56
#define BIOS_SIGNATURE  0x58
#define BIOS_VERSION    0x5A

/*
 * Microcode Control Flags
 *
 * Flags set by the Adv Library in RISC variable 'control_flag' (0x122)
 * and handled by the microcode.
 */
#define CONTROL_FLAG_IGNORE_PERR        0x0001 /* Ignore DMA Parity Errors */
#define CONTROL_FLAG_ENABLE_AIPP        0x0002 /* Enabled AIPP checking. */

/*
 * ASC_MC_DEVICE_HSHK_CFG_TABLE microcode table or HSHK_CFG register format
 */
#define HSHK_CFG_WIDE_XFR       0x8000
#define HSHK_CFG_RATE           0x0F00
#define HSHK_CFG_OFFSET         0x001F

#define ASC_DEF_MAX_HOST_QNG    0xFD /* Max. number of host commands (253) */
#define ASC_DEF_MIN_HOST_QNG    0x10 /* Min. number of host commands (16) */
#define ASC_DEF_MAX_DVC_QNG     0x3F /* Max. number commands per device (63) */
#define ASC_DEF_MIN_DVC_QNG     0x04 /* Min. number commands per device (4) */

#define ASC_QC_DATA_CHECK  0x01 /* Require ASC_QC_DATA_OUT set or clear. */
#define ASC_QC_DATA_OUT    0x02 /* Data out DMA transfer. */
#define ASC_QC_START_MOTOR 0x04 /* Send auto-start motor before request. */
#define ASC_QC_NO_OVERRUN  0x08 /* Don't report overrun. */
#define ASC_QC_FREEZE_TIDQ 0x10 /* Freeze TID queue after request. XXX TBD */

#define ASC_QSC_NO_DISC     0x01 /* Don't allow disconnect for request. */
#define ASC_QSC_NO_TAGMSG   0x02 /* Don't allow tag queuing for request. */
#define ASC_QSC_NO_SYNC     0x04 /* Don't use Synch. transfer on request. */
#define ASC_QSC_NO_WIDE     0x08 /* Don't use Wide transfer on request. */
#define ASC_QSC_REDO_DTR    0x10 /* Renegotiate WDTR/SDTR before request. */
/*
 * Note: If a Tag Message is to be sent and neither ASC_QSC_HEAD_TAG or
 * ASC_QSC_ORDERED_TAG is set, then a Simple Tag Message (0x20) is used.
 */
#define ASC_QSC_HEAD_TAG    0x40 /* Use Head Tag Message (0x21). */
#define ASC_QSC_ORDERED_TAG 0x80 /* Use Ordered Tag Message (0x22). */

/*
 * All fields here are accessed by the board microcode and need to be
 * little-endian.
 */
typedef struct adv_carr_t
{
    ADV_VADDR   carr_va;       /* Carrier Virtual Address */
    ADV_PADDR   carr_pa;       /* Carrier Physical Address */
    ADV_VADDR   areq_vpa;      /* ASC_SCSI_REQ_Q Virtual or Physical Address */
    /*
     * next_vpa [31:4]            Carrier Virtual or Physical Next Pointer
     *
     * next_vpa [3:1]             Reserved Bits
     * next_vpa [0]               Done Flag set in Response Queue.
     */
    ADV_VADDR   next_vpa;
} ADV_CARR_T;

/*
 * Mask used to eliminate low 4 bits of carrier 'next_vpa' field.
 */
#define ASC_NEXT_VPA_MASK       0xFFFFFFF0

#define ASC_RQ_DONE             0x00000001
#define ASC_RQ_GOOD             0x00000002
#define ASC_CQ_STOPPER          0x00000000

#define ASC_GET_CARRP(carrp) ((carrp) & ASC_NEXT_VPA_MASK)

#define ADV_CARRIER_NUM_PAGE_CROSSING \
    (((ADV_CARRIER_COUNT * sizeof(ADV_CARR_T)) + \
        (ADV_PAGE_SIZE - 1))/ADV_PAGE_SIZE)

#define ADV_CARRIER_BUFSIZE \
    ((ADV_CARRIER_COUNT + ADV_CARRIER_NUM_PAGE_CROSSING) * sizeof(ADV_CARR_T))

/*
 * ASC_SCSI_REQ_Q 'a_flag' definitions
 *
 * The Adv Library should limit use to the lower nibble (4 bits) of
 * a_flag. Drivers are free to use the upper nibble (4 bits) of a_flag.
 */
#define ADV_POLL_REQUEST                0x01   /* poll for request completion */
#define ADV_SCSIQ_DONE                  0x02   /* request done */
#define ADV_DONT_RETRY                  0x08   /* don't do retry */

#define ADV_CHIP_ASC3550          0x01   /* Ultra-Wide IC */
#define ADV_CHIP_ASC38C0800       0x02   /* Ultra2-Wide/LVD IC */
#define ADV_CHIP_ASC38C1600       0x03   /* Ultra3-Wide/LVD2 IC */

/*
 * Adapter temporary configuration structure
 *
 * This structure can be discarded after initialization. Don't add
 * fields here needed after initialization.
 *
 * Field naming convention:
 *
 *  *_enable indicates the field enables or disables a feature. The
 *  value of the field is never reset.
 */
typedef struct adv_dvc_cfg {
  ushort disc_enable;       /* enable disconnection */
  uchar  chip_version;      /* chip version */
  uchar  termination;       /* Term. Ctrl. bits 6-5 of SCSI_CFG1 register */
  ushort lib_version;       /* Adv Library version number */
  ushort control_flag;      /* Microcode Control Flag */
  ushort mcode_date;        /* Microcode date */
  ushort mcode_version;     /* Microcode version */
  ushort pci_slot_info;     /* high byte device/function number */
                            /* bits 7-3 device num., bits 2-0 function num. */
                            /* low byte bus num. */
  ushort serial1;           /* EEPROM serial number word 1 */
  ushort serial2;           /* EEPROM serial number word 2 */
  ushort serial3;           /* EEPROM serial number word 3 */
  struct device *dev;  /* pointer to the pci dev structure for this board */
} ADV_DVC_CFG;

struct adv_dvc_var;
struct adv_scsi_req_q;

typedef void (* ADV_ISR_CALLBACK)
    (struct adv_dvc_var *, struct adv_scsi_req_q *);

typedef void (* ADV_ASYNC_CALLBACK)
    (struct adv_dvc_var *, uchar);

/*
 * Adapter operation variable structure.
 *
 * One structure is required per host adapter.
 *
 * Field naming convention:
 *
 *  *_able indicates both whether a feature should be enabled or disabled
 *  and whether a device isi capable of the feature. At initialization
 *  this field may be set, but later if a device is found to be incapable
 *  of the feature, the field is cleared.
 */
typedef struct adv_dvc_var {
  AdvPortAddr iop_base;   /* I/O port address */
  ushort err_code;        /* fatal error code */
  ushort bios_ctrl;       /* BIOS control word, EEPROM word 12 */
  ADV_ISR_CALLBACK isr_callback;
  ADV_ASYNC_CALLBACK async_callback;
  ushort wdtr_able;       /* try WDTR for a device */
  ushort sdtr_able;       /* try SDTR for a device */
  ushort ultra_able;      /* try SDTR Ultra speed for a device */
  ushort sdtr_speed1;     /* EEPROM SDTR Speed for TID 0-3   */
  ushort sdtr_speed2;     /* EEPROM SDTR Speed for TID 4-7   */
  ushort sdtr_speed3;     /* EEPROM SDTR Speed for TID 8-11  */
  ushort sdtr_speed4;     /* EEPROM SDTR Speed for TID 12-15 */
  ushort tagqng_able;     /* try tagged queuing with a device */
  ushort ppr_able;        /* PPR message capable per TID bitmask. */
  uchar  max_dvc_qng;     /* maximum number of tagged commands per device */
  ushort start_motor;     /* start motor command allowed */
  uchar  scsi_reset_wait; /* delay in seconds after scsi bus reset */
  uchar  chip_no;         /* should be assigned by caller */
  uchar  max_host_qng;    /* maximum number of Q'ed command allowed */
  uchar  irq_no;          /* IRQ number */
  ushort no_scam;         /* scam_tolerant of EEPROM */
  struct asc_board *drv_ptr; /* driver pointer to private structure */
  uchar  chip_scsi_id;    /* chip SCSI target ID */
  uchar  chip_type;
  uchar  bist_err_code;
  ADV_CARR_T *carrier_buf;
  ADV_CARR_T *carr_freelist; /* Carrier free list. */
  ADV_CARR_T *icq_sp;  /* Initiator command queue stopper pointer. */
  ADV_CARR_T *irq_sp;  /* Initiator response queue stopper pointer. */
  ushort carr_pending_cnt;    /* Count of pending carriers. */
 /*
  * Note: The following fields will not be used after initialization. The
  * driver may discard the buffer after initialization is done.
  */
  ADV_DVC_CFG *cfg; /* temporary configuration structure  */
} ADV_DVC_VAR;

#define NO_OF_SG_PER_BLOCK              15

typedef struct asc_sg_block {
    uchar reserved1;
    uchar reserved2;
    uchar reserved3;
    uchar sg_cnt;                     /* Valid entries in block. */
    ADV_PADDR sg_ptr;                 /* Pointer to next sg block. */
    struct  {
        ADV_PADDR sg_addr;                  /* SG element address. */
        ADV_DCNT  sg_count;                 /* SG element count. */
    } sg_list[NO_OF_SG_PER_BLOCK];
} ADV_SG_BLOCK;

/*
 * ADV_SCSI_REQ_Q - microcode request structure
 *
 * All fields in this structure up to byte 60 are used by the microcode.
 * The microcode makes assumptions about the size and ordering of fields
 * in this structure. Do not change the structure definition here without
 * coordinating the change with the microcode.
 *
 * All fields accessed by microcode must be maintained in little_endian
 * order.
 */
typedef struct adv_scsi_req_q {
    uchar       cntl;           /* Ucode flags and state (ASC_MC_QC_*). */
    uchar       target_cmd;
    uchar       target_id;      /* Device target identifier. */
    uchar       target_lun;     /* Device target logical unit number. */
    ADV_PADDR   data_addr;      /* Data buffer physical address. */
    ADV_DCNT    data_cnt;       /* Data count. Ucode sets to residual. */
    ADV_PADDR   sense_addr;
    ADV_PADDR   carr_pa;
    uchar       mflag;
    uchar       sense_len;
    uchar       cdb_len;        /* SCSI CDB length. Must <= 16 bytes. */
    uchar       scsi_cntl;
    uchar       done_status;    /* Completion status. */
    uchar       scsi_status;    /* SCSI status byte. */
    uchar       host_status;    /* Ucode host status. */
    uchar       sg_working_ix;
    uchar       cdb[12];        /* SCSI CDB bytes 0-11. */
    ADV_PADDR   sg_real_addr;   /* SG list physical address. */
    ADV_PADDR   scsiq_rptr;
    uchar       cdb16[4];       /* SCSI CDB bytes 12-15. */
    ADV_VADDR   scsiq_ptr;
    ADV_VADDR   carr_va;
    /*
     * End of microcode structure - 60 bytes. The rest of the structure
     * is used by the Adv Library and ignored by the microcode.
     */
    ADV_VADDR   srb_ptr;
    ADV_SG_BLOCK *sg_list_ptr; /* SG list virtual address. */
    char        *vdata_addr;   /* Data buffer virtual address. */
    uchar       a_flag;
    uchar       pad[2];        /* Pad out to a word boundary. */
} ADV_SCSI_REQ_Q;

/*
 * Microcode idle loop commands
 */
#define IDLE_CMD_COMPLETED           0
#define IDLE_CMD_STOP_CHIP           0x0001
#define IDLE_CMD_STOP_CHIP_SEND_INT  0x0002
#define IDLE_CMD_SEND_INT            0x0004
#define IDLE_CMD_ABORT               0x0008
#define IDLE_CMD_DEVICE_RESET        0x0010
#define IDLE_CMD_SCSI_RESET_START    0x0020 /* Assert SCSI Bus Reset */
#define IDLE_CMD_SCSI_RESET_END      0x0040 /* Deassert SCSI Bus Reset */
#define IDLE_CMD_SCSIREQ             0x0080

#define IDLE_CMD_STATUS_SUCCESS      0x0001
#define IDLE_CMD_STATUS_FAILURE      0x0002

/*
 * AdvSendIdleCmd() flag definitions.
 */
#define ADV_NOWAIT     0x01

/*
 * Wait loop time out values.
 */
#define SCSI_WAIT_10_SEC             10UL    /* 10 seconds */
#define SCSI_WAIT_100_MSEC           100UL   /* 100 milliseconds */
#define SCSI_US_PER_MSEC             1000    /* microseconds per millisecond */
#define SCSI_MS_PER_SEC              1000UL  /* milliseconds per second */
#define SCSI_MAX_RETRY               10      /* retry count */

#define ADV_ASYNC_RDMA_FAILURE          0x01 /* Fatal RDMA failure. */
#define ADV_ASYNC_SCSI_BUS_RESET_DET    0x02 /* Detected SCSI Bus Reset. */
#define ADV_ASYNC_CARRIER_READY_FAILURE 0x03 /* Carrier Ready failure. */
#define ADV_RDMA_IN_CARR_AND_Q_INVALID  0x04 /* RDMAed-in data invalid. */


#define ADV_HOST_SCSI_BUS_RESET      0x80 /* Host Initiated SCSI Bus Reset. */

/*
 * Device drivers must define the following functions.
 */
STATIC inline ulong DvcEnterCritical(void);
STATIC inline void  DvcLeaveCritical(ulong);
STATIC void  DvcSleepMilliSecond(ADV_DCNT);
STATIC uchar DvcAdvReadPCIConfigByte(ADV_DVC_VAR *, ushort);
STATIC void  DvcAdvWritePCIConfigByte(ADV_DVC_VAR *, ushort, uchar);
STATIC ADV_PADDR DvcGetPhyAddr(ADV_DVC_VAR *, ADV_SCSI_REQ_Q *,
                uchar *, ASC_SDCNT *, int);
STATIC void  DvcDelayMicroSecond(ADV_DVC_VAR *, ushort);

/*
 * Adv Library functions available to drivers.
 */
STATIC int     AdvExeScsiQueue(ADV_DVC_VAR *, ADV_SCSI_REQ_Q *);
STATIC int     AdvISR(ADV_DVC_VAR *);
STATIC int     AdvInitGetConfig(ADV_DVC_VAR *);
STATIC int     AdvInitAsc3550Driver(ADV_DVC_VAR *);
STATIC int     AdvInitAsc38C0800Driver(ADV_DVC_VAR *);
STATIC int     AdvInitAsc38C1600Driver(ADV_DVC_VAR *);
STATIC int     AdvResetChipAndSB(ADV_DVC_VAR *);
STATIC int     AdvResetSB(ADV_DVC_VAR *asc_dvc);

/*
 * Internal Adv Library functions.
 */
STATIC int    AdvSendIdleCmd(ADV_DVC_VAR *, ushort, ADV_DCNT);
STATIC void   AdvInquiryHandling(ADV_DVC_VAR *, ADV_SCSI_REQ_Q *);
STATIC int    AdvInitFrom3550EEP(ADV_DVC_VAR *);
STATIC int    AdvInitFrom38C0800EEP(ADV_DVC_VAR *);
STATIC int    AdvInitFrom38C1600EEP(ADV_DVC_VAR *);
STATIC ushort AdvGet3550EEPConfig(AdvPortAddr, ADVEEP_3550_CONFIG *);
STATIC void   AdvSet3550EEPConfig(AdvPortAddr, ADVEEP_3550_CONFIG *);
STATIC ushort AdvGet38C0800EEPConfig(AdvPortAddr, ADVEEP_38C0800_CONFIG *);
STATIC void   AdvSet38C0800EEPConfig(AdvPortAddr, ADVEEP_38C0800_CONFIG *);
STATIC ushort AdvGet38C1600EEPConfig(AdvPortAddr, ADVEEP_38C1600_CONFIG *);
STATIC void   AdvSet38C1600EEPConfig(AdvPortAddr, ADVEEP_38C1600_CONFIG *);
STATIC void   AdvWaitEEPCmd(AdvPortAddr);
STATIC ushort AdvReadEEPWord(AdvPortAddr, int);

/*
 * PCI Bus Definitions
 */
#define AscPCICmdRegBits_BusMastering     0x0007
#define AscPCICmdRegBits_ParErrRespCtrl   0x0040

/* Read byte from a register. */
#define AdvReadByteRegister(iop_base, reg_off) \
     (ADV_MEM_READB((iop_base) + (reg_off)))

/* Write byte to a register. */
#define AdvWriteByteRegister(iop_base, reg_off, byte) \
     (ADV_MEM_WRITEB((iop_base) + (reg_off), (byte)))

/* Read word (2 bytes) from a register. */
#define AdvReadWordRegister(iop_base, reg_off) \
     (ADV_MEM_READW((iop_base) + (reg_off)))

/* Write word (2 bytes) to a register. */
#define AdvWriteWordRegister(iop_base, reg_off, word) \
     (ADV_MEM_WRITEW((iop_base) + (reg_off), (word)))

/* Write dword (4 bytes) to a register. */
#define AdvWriteDWordRegister(iop_base, reg_off, dword) \
     (ADV_MEM_WRITEDW((iop_base) + (reg_off), (dword)))

/* Read byte from LRAM. */
#define AdvReadByteLram(iop_base, addr, byte) \
do { \
    ADV_MEM_WRITEW((iop_base) + IOPW_RAM_ADDR, (addr)); \
    (byte) = ADV_MEM_READB((iop_base) + IOPB_RAM_DATA); \
} while (0)

/* Write byte to LRAM. */
#define AdvWriteByteLram(iop_base, addr, byte) \
    (ADV_MEM_WRITEW((iop_base) + IOPW_RAM_ADDR, (addr)), \
     ADV_MEM_WRITEB((iop_base) + IOPB_RAM_DATA, (byte)))

/* Read word (2 bytes) from LRAM. */
#define AdvReadWordLram(iop_base, addr, word) \
do { \
    ADV_MEM_WRITEW((iop_base) + IOPW_RAM_ADDR, (addr)); \
    (word) = (ADV_MEM_READW((iop_base) + IOPW_RAM_DATA)); \
} while (0)

/* Write word (2 bytes) to LRAM. */
#define AdvWriteWordLram(iop_base, addr, word) \
    (ADV_MEM_WRITEW((iop_base) + IOPW_RAM_ADDR, (addr)), \
     ADV_MEM_WRITEW((iop_base) + IOPW_RAM_DATA, (word)))

/* Write little-endian double word (4 bytes) to LRAM */
/* Because of unspecified C language ordering don't use auto-increment. */
#define AdvWriteDWordLramNoSwap(iop_base, addr, dword) \
    ((ADV_MEM_WRITEW((iop_base) + IOPW_RAM_ADDR, (addr)), \
      ADV_MEM_WRITEW((iop_base) + IOPW_RAM_DATA, \
                     cpu_to_le16((ushort) ((dword) & 0xFFFF)))), \
     (ADV_MEM_WRITEW((iop_base) + IOPW_RAM_ADDR, (addr) + 2), \
      ADV_MEM_WRITEW((iop_base) + IOPW_RAM_DATA, \
                     cpu_to_le16((ushort) ((dword >> 16) & 0xFFFF)))))

/* Read word (2 bytes) from LRAM assuming that the address is already set. */
#define AdvReadWordAutoIncLram(iop_base) \
     (ADV_MEM_READW((iop_base) + IOPW_RAM_DATA))

/* Write word (2 bytes) to LRAM assuming that the address is already set. */
#define AdvWriteWordAutoIncLram(iop_base, word) \
     (ADV_MEM_WRITEW((iop_base) + IOPW_RAM_DATA, (word)))


/*
 * Define macro to check for Condor signature.
 *
 * Evaluate to ADV_TRUE if a Condor chip is found the specified port
 * address 'iop_base'. Otherwise evalue to ADV_FALSE.
 */
#define AdvFindSignature(iop_base) \
    (((AdvReadByteRegister((iop_base), IOPB_CHIP_ID_1) == \
    ADV_CHIP_ID_BYTE) && \
     (AdvReadWordRegister((iop_base), IOPW_CHIP_ID_0) == \
    ADV_CHIP_ID_WORD)) ?  ADV_TRUE : ADV_FALSE)

/*
 * Define macro to Return the version number of the chip at 'iop_base'.
 *
 * The second parameter 'bus_type' is currently unused.
 */
#define AdvGetChipVersion(iop_base, bus_type) \
    AdvReadByteRegister((iop_base), IOPB_CHIP_TYPE_REV)

/*
 * Abort an SRB in the chip's RISC Memory. The 'srb_ptr' argument must
 * match the ASC_SCSI_REQ_Q 'srb_ptr' field.
 *
 * If the request has not yet been sent to the device it will simply be
 * aborted from RISC memory. If the request is disconnected it will be
 * aborted on reselection by sending an Abort Message to the target ID.
 *
 * Return value:
 *      ADV_TRUE(1) - Queue was successfully aborted.
 *      ADV_FALSE(0) - Queue was not found on the active queue list.
 */
#define AdvAbortQueue(asc_dvc, scsiq) \
        AdvSendIdleCmd((asc_dvc), (ushort) IDLE_CMD_ABORT, \
                       (ADV_DCNT) (scsiq))

/*
 * Send a Bus Device Reset Message to the specified target ID.
 *
 * All outstanding commands will be purged if sending the
 * Bus Device Reset Message is successful.
 *
 * Return Value:
 *      ADV_TRUE(1) - All requests on the target are purged.
 *      ADV_FALSE(0) - Couldn't issue Bus Device Reset Message; Requests
 *                     are not purged.
 */
#define AdvResetDevice(asc_dvc, target_id) \
        AdvSendIdleCmd((asc_dvc), (ushort) IDLE_CMD_DEVICE_RESET, \
                    (ADV_DCNT) (target_id))

/*
 * SCSI Wide Type definition.
 */
#define ADV_SCSI_BIT_ID_TYPE   ushort

/*
 * AdvInitScsiTarget() 'cntl_flag' options.
 */
#define ADV_SCAN_LUN           0x01
#define ADV_CAPINFO_NOLUN      0x02

/*
 * Convert target id to target id bit mask.
 */
#define ADV_TID_TO_TIDMASK(tid)   (0x01 << ((tid) & ADV_MAX_TID))

/*
 * ASC_SCSI_REQ_Q 'done_status' and 'host_status' return values.
 */

#define QD_NO_STATUS         0x00       /* Request not completed yet. */
#define QD_NO_ERROR          0x01
#define QD_ABORTED_BY_HOST   0x02
#define QD_WITH_ERROR        0x04

#define QHSTA_NO_ERROR              0x00
#define QHSTA_M_SEL_TIMEOUT         0x11
#define QHSTA_M_DATA_OVER_RUN       0x12
#define QHSTA_M_UNEXPECTED_BUS_FREE 0x13
#define QHSTA_M_QUEUE_ABORTED       0x15
#define QHSTA_M_SXFR_SDMA_ERR       0x16 /* SXFR_STATUS SCSI DMA Error */
#define QHSTA_M_SXFR_SXFR_PERR      0x17 /* SXFR_STATUS SCSI Bus Parity Error */
#define QHSTA_M_RDMA_PERR           0x18 /* RISC PCI DMA parity error */
#define QHSTA_M_SXFR_OFF_UFLW       0x19 /* SXFR_STATUS Offset Underflow */
#define QHSTA_M_SXFR_OFF_OFLW       0x20 /* SXFR_STATUS Offset Overflow */
#define QHSTA_M_SXFR_WD_TMO         0x21 /* SXFR_STATUS Watchdog Timeout */
#define QHSTA_M_SXFR_DESELECTED     0x22 /* SXFR_STATUS Deselected */
/* Note: QHSTA_M_SXFR_XFR_OFLW is identical to QHSTA_M_DATA_OVER_RUN. */
#define QHSTA_M_SXFR_XFR_OFLW       0x12 /* SXFR_STATUS Transfer Overflow */
#define QHSTA_M_SXFR_XFR_PH_ERR     0x24 /* SXFR_STATUS Transfer Phase Error */
#define QHSTA_M_SXFR_UNKNOWN_ERROR  0x25 /* SXFR_STATUS Unknown Error */
#define QHSTA_M_SCSI_BUS_RESET      0x30 /* Request aborted from SBR */
#define QHSTA_M_SCSI_BUS_RESET_UNSOL 0x31 /* Request aborted from unsol. SBR */
#define QHSTA_M_BUS_DEVICE_RESET    0x32 /* Request aborted from BDR */
#define QHSTA_M_DIRECTION_ERR       0x35 /* Data Phase mismatch */
#define QHSTA_M_DIRECTION_ERR_HUNG  0x36 /* Data Phase mismatch and bus hang */
#define QHSTA_M_WTM_TIMEOUT         0x41
#define QHSTA_M_BAD_CMPL_STATUS_IN  0x42
#define QHSTA_M_NO_AUTO_REQ_SENSE   0x43
#define QHSTA_M_AUTO_REQ_SENSE_FAIL 0x44
#define QHSTA_M_INVALID_DEVICE      0x45 /* Bad target ID */
#define QHSTA_M_FROZEN_TIDQ         0x46 /* TID Queue frozen. */
#define QHSTA_M_SGBACKUP_ERROR      0x47 /* Scatter-Gather backup error */


/*
 * Default EEPROM Configuration structure defined in a_init.c.
 */
static ADVEEP_3550_CONFIG Default_3550_EEPROM_Config;
static ADVEEP_38C0800_CONFIG Default_38C0800_EEPROM_Config;
static ADVEEP_38C1600_CONFIG Default_38C1600_EEPROM_Config;

/*
 * DvcGetPhyAddr() flag arguments
 */
#define ADV_IS_SCSIQ_FLAG       0x01 /* 'addr' is ASC_SCSI_REQ_Q pointer */
#define ADV_ASCGETSGLIST_VADDR  0x02 /* 'addr' is AscGetSGList() virtual addr */
#define ADV_IS_SENSE_FLAG       0x04 /* 'addr' is sense virtual pointer */
#define ADV_IS_DATA_FLAG        0x08 /* 'addr' is data virtual pointer */
#define ADV_IS_SGLIST_FLAG      0x10 /* 'addr' is sglist virtual pointer */
#define ADV_IS_CARRIER_FLAG     0x20 /* 'addr' is ADV_CARR_T pointer */

/* Return the address that is aligned at the next doubleword >= to 'addr'. */
#define ADV_8BALIGN(addr)      (((ulong) (addr) + 0x7) & ~0x7)
#define ADV_16BALIGN(addr)     (((ulong) (addr) + 0xF) & ~0xF)
#define ADV_32BALIGN(addr)     (((ulong) (addr) + 0x1F) & ~0x1F)

/*
 * Total contiguous memory needed for driver SG blocks.
 *
 * ADV_MAX_SG_LIST must be defined by a driver. It is the maximum
 * number of scatter-gather elements the driver supports in a
 * single request.
 */

#define ADV_SG_LIST_MAX_BYTE_SIZE \
         (sizeof(ADV_SG_BLOCK) * \
          ((ADV_MAX_SG_LIST + (NO_OF_SG_PER_BLOCK - 1))/NO_OF_SG_PER_BLOCK))

/*
 * Inquiry data structure and bitfield macros
 *
 * Using bitfields to access the subchar data isn't portable across
 * endianness, so instead mask and shift. Only quantities of more
 * than 1 bit are shifted, since the others are just tested for true
 * or false.
 */

#define ADV_INQ_DVC_TYPE(inq)       ((inq)->periph & 0x1f)
#define ADV_INQ_QUALIFIER(inq)      (((inq)->periph & 0xe0) >> 5)
#define ADV_INQ_DVC_TYPE_MOD(inq)   ((inq)->devtype & 0x7f)
#define ADV_INQ_REMOVABLE(inq)      ((inq)->devtype & 0x80)
#define ADV_INQ_ANSI_VER(inq)       ((inq)->ver & 0x07)
#define ADV_INQ_ECMA_VER(inq)       (((inq)->ver & 0x38) >> 3)
#define ADV_INQ_ISO_VER(inq)        (((inq)->ver & 0xc0) >> 6)
#define ADV_INQ_RESPONSE_FMT(inq)   ((inq)->byte3 & 0x0f)
#define ADV_INQ_TERM_IO(inq)        ((inq)->byte3 & 0x40)
#define ADV_INQ_ASYNC_NOTIF(inq)    ((inq)->byte3 & 0x80)
#define ADV_INQ_SOFT_RESET(inq)     ((inq)->flags & 0x01)
#define ADV_INQ_CMD_QUEUE(inq)      ((inq)->flags & 0x02)
#define ADV_INQ_LINK_CMD(inq)       ((inq)->flags & 0x08)
#define ADV_INQ_SYNC(inq)           ((inq)->flags & 0x10)
#define ADV_INQ_WIDE16(inq)         ((inq)->flags & 0x20)
#define ADV_INQ_WIDE32(inq)         ((inq)->flags & 0x40)
#define ADV_INQ_REL_ADDR(inq)       ((inq)->flags & 0x80)
#define ADV_INQ_INFO_UNIT(inq)      ((inq)->info & 0x01)
#define ADV_INQ_QUICK_ARB(inq)      ((inq)->info & 0x02)
#define ADV_INQ_CLOCKING(inq)       (((inq)->info & 0x0c) >> 2)

typedef struct {
  uchar periph;                 /* peripheral device type [0:4] */
                                /* peripheral qualifier [5:7] */
  uchar devtype;                /* device type modifier (for SCSI I) [0:6] */
                                /* RMB - removable medium bit [7] */
  uchar ver;                    /* ANSI approved version [0:2] */
                                /* ECMA version [3:5] */
                                /* ISO version [6:7] */
  uchar byte3;                  /* response data format [0:3] */
                                /* 0 SCSI 1 */
                                /* 1 CCS */
                                /* 2 SCSI-2 */
                                /* 3-F reserved */
                                /* reserved [4:5] */
                                /* terminate I/O process bit (see 5.6.22) [6] */
                                /* asynch. event notification (processor) [7] */
  uchar add_len;                /* additional length */
  uchar res1;                   /* reserved */
  uchar res2;                   /* reserved */
  uchar flags;                  /* soft reset implemented [0] */
                                /* command queuing [1] */
                                /* reserved [2] */
                                /* linked command for this logical unit [3] */
                                /* synchronous data transfer [4] */
                                /* wide bus 16 bit data transfer [5] */
                                /* wide bus 32 bit data transfer [6] */
                                /* relative addressing mode [7] */
  uchar vendor_id[8];           /* vendor identification */
  uchar product_id[16];         /* product identification */
  uchar product_rev_level[4];   /* product revision level */
  uchar vendor_specific[20];    /* vendor specific */
  uchar info;                   /* information unit supported [0] */
                                /* quick arbitrate supported [1] */
                                /* clocking field [2:3] */
                                /* reserved [4:7] */
  uchar res3;                   /* reserved */
} ADV_SCSI_INQUIRY; /* 58 bytes */


/*
 * --- Driver Constants and Macros
 */

#define ASC_NUM_BOARD_SUPPORTED 16
#define ASC_NUM_IOPORT_PROBE    4
#define ASC_NUM_BUS             4

/* Reference Scsi_Host hostdata */
#define ASC_BOARDP(host) ((asc_board_t *) &((host)->hostdata))

/* asc_board_t flags */
#define ASC_HOST_IN_RESET       0x01
#define ASC_IS_WIDE_BOARD       0x04    /* AdvanSys Wide Board */
#define ASC_SELECT_QUEUE_DEPTHS 0x08

#define ASC_NARROW_BOARD(boardp) (((boardp)->flags & ASC_IS_WIDE_BOARD) == 0)
#define ASC_WIDE_BOARD(boardp)   ((boardp)->flags & ASC_IS_WIDE_BOARD)

#define NO_ISA_DMA              0xff        /* No ISA DMA Channel Used */

#define ASC_INFO_SIZE           128            /* advansys_info() line size */

#ifdef CONFIG_PROC_FS
/* /proc/scsi/advansys/[0...] related definitions */
#define ASC_PRTBUF_SIZE         2048
#define ASC_PRTLINE_SIZE        160

#define ASC_PRT_NEXT() \
    if (cp) { \
        totlen += len; \
        leftlen -= len; \
        if (leftlen == 0) { \
            return totlen; \
        } \
        cp += len; \
    }
#endif /* CONFIG_PROC_FS */

/* Asc Library return codes */
#define ASC_TRUE        1
#define ASC_FALSE       0
#define ASC_NOERROR     1
#define ASC_BUSY        0
#define ASC_ERROR       (-1)

/* struct scsi_cmnd function return codes */
#define STATUS_BYTE(byte)   (byte)
#define MSG_BYTE(byte)      ((byte) << 8)
#define HOST_BYTE(byte)     ((byte) << 16)
#define DRIVER_BYTE(byte)   ((byte) << 24)

/*
 * The following definitions and macros are OS independent interfaces to
 * the queue functions:
 *  REQ - SCSI request structure
 *  REQP - pointer to SCSI request structure
 *  REQPTID(reqp) - reqp's target id
 *  REQPNEXT(reqp) - reqp's next pointer
 *  REQPNEXTP(reqp) - pointer to reqp's next pointer
 *  REQPTIME(reqp) - reqp's time stamp value
 *  REQTIMESTAMP() - system time stamp value
 */
typedef struct scsi_cmnd     REQ, *REQP;
#define REQPNEXT(reqp)       ((REQP) ((reqp)->host_scribble))
#define REQPNEXTP(reqp)      ((REQP *) &((reqp)->host_scribble))
#define REQPTID(reqp)        ((reqp)->device->id)
#define REQPTIME(reqp)       ((reqp)->SCp.this_residual)
#define REQTIMESTAMP()       (jiffies)

#define REQTIMESTAT(function, ascq, reqp, tid) \
{ \
    /*
     * If the request time stamp is less than the system time stamp, then \
     * maybe the system time stamp wrapped. Set the request time to zero.\
     */ \
    if (REQPTIME(reqp) <= REQTIMESTAMP()) { \
        REQPTIME(reqp) = REQTIMESTAMP() - REQPTIME(reqp); \
    } else { \
        /* Indicate an error occurred with the assertion. */ \
        ASC_ASSERT(REQPTIME(reqp) <= REQTIMESTAMP()); \
        REQPTIME(reqp) = 0; \
    } \
    /* Handle first minimum time case without external initialization. */ \
    if (((ascq)->q_tot_cnt[tid] == 1) ||  \
        (REQPTIME(reqp) < (ascq)->q_min_tim[tid])) { \
            (ascq)->q_min_tim[tid] = REQPTIME(reqp); \
            ASC_DBG3(1, "%s: new q_min_tim[%d] %u\n", \
                (function), (tid), (ascq)->q_min_tim[tid]); \
        } \
    if (REQPTIME(reqp) > (ascq)->q_max_tim[tid]) { \
        (ascq)->q_max_tim[tid] = REQPTIME(reqp); \
        ASC_DBG3(1, "%s: new q_max_tim[%d] %u\n", \
            (function), tid, (ascq)->q_max_tim[tid]); \
    } \
    (ascq)->q_tot_tim[tid] += REQPTIME(reqp); \
    /* Reset the time stamp field. */ \
    REQPTIME(reqp) = 0; \
}

/* asc_enqueue() flags */
#define ASC_FRONT       1
#define ASC_BACK        2

/* asc_dequeue_list() argument */
#define ASC_TID_ALL        (-1)

/* Return non-zero, if the queue is empty. */
#define ASC_QUEUE_EMPTY(ascq)    ((ascq)->q_tidmask == 0)

#define PCI_MAX_SLOT            0x1F
#define PCI_MAX_BUS             0xFF
#define PCI_IOADDRESS_MASK      0xFFFE
#define ASC_PCI_DEVICE_ID_CNT   6       /* PCI Device ID count. */

#ifndef ADVANSYS_STATS
#define ASC_STATS(shp, counter)
#define ASC_STATS_ADD(shp, counter, count)
#else /* ADVANSYS_STATS */
#define ASC_STATS(shp, counter) \
    (ASC_BOARDP(shp)->asc_stats.counter++)

#define ASC_STATS_ADD(shp, counter, count) \
    (ASC_BOARDP(shp)->asc_stats.counter += (count))
#endif /* ADVANSYS_STATS */

#define ASC_CEILING(val, unit) (((val) + ((unit) - 1))/(unit))

/* If the result wraps when calculating tenths, return 0. */
#define ASC_TENTHS(num, den) \
    (((10 * ((num)/(den))) > (((num) * 10)/(den))) ? \
    0 : ((((num) * 10)/(den)) - (10 * ((num)/(den)))))

/*
 * Display a message to the console.
 */
#define ASC_PRINT(s) \
    { \
        printk("advansys: "); \
        printk(s); \
    }

#define ASC_PRINT1(s, a1) \
    { \
        printk("advansys: "); \
        printk((s), (a1)); \
    }

#define ASC_PRINT2(s, a1, a2) \
    { \
        printk("advansys: "); \
        printk((s), (a1), (a2)); \
    }

#define ASC_PRINT3(s, a1, a2, a3) \
    { \
        printk("advansys: "); \
        printk((s), (a1), (a2), (a3)); \
    }

#define ASC_PRINT4(s, a1, a2, a3, a4) \
    { \
        printk("advansys: "); \
        printk((s), (a1), (a2), (a3), (a4)); \
    }


#ifndef ADVANSYS_DEBUG

#define ASC_DBG(lvl, s)
#define ASC_DBG1(lvl, s, a1)
#define ASC_DBG2(lvl, s, a1, a2)
#define ASC_DBG3(lvl, s, a1, a2, a3)
#define ASC_DBG4(lvl, s, a1, a2, a3, a4)
#define ASC_DBG_PRT_SCSI_HOST(lvl, s)
#define ASC_DBG_PRT_SCSI_CMND(lvl, s)
#define ASC_DBG_PRT_ASC_SCSI_Q(lvl, scsiqp)
#define ASC_DBG_PRT_ADV_SCSI_REQ_Q(lvl, scsiqp)
#define ASC_DBG_PRT_ASC_QDONE_INFO(lvl, qdone)
#define ADV_DBG_PRT_ADV_SCSI_REQ_Q(lvl, scsiqp)
#define ASC_DBG_PRT_HEX(lvl, name, start, length)
#define ASC_DBG_PRT_CDB(lvl, cdb, len)
#define ASC_DBG_PRT_SENSE(lvl, sense, len)
#define ASC_DBG_PRT_INQUIRY(lvl, inq, len)

#else /* ADVANSYS_DEBUG */

/*
 * Debugging Message Levels:
 * 0: Errors Only
 * 1: High-Level Tracing
 * 2-N: Verbose Tracing
 */

#define ASC_DBG(lvl, s) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            printk(s); \
        } \
    }

#define ASC_DBG1(lvl, s, a1) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            printk((s), (a1)); \
        } \
    }

#define ASC_DBG2(lvl, s, a1, a2) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            printk((s), (a1), (a2)); \
        } \
    }

#define ASC_DBG3(lvl, s, a1, a2, a3) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            printk((s), (a1), (a2), (a3)); \
        } \
    }

#define ASC_DBG4(lvl, s, a1, a2, a3, a4) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            printk((s), (a1), (a2), (a3), (a4)); \
        } \
    }

#define ASC_DBG_PRT_SCSI_HOST(lvl, s) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            asc_prt_scsi_host(s); \
        } \
    }

#define ASC_DBG_PRT_SCSI_CMND(lvl, s) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            asc_prt_scsi_cmnd(s); \
        } \
    }

#define ASC_DBG_PRT_ASC_SCSI_Q(lvl, scsiqp) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            asc_prt_asc_scsi_q(scsiqp); \
        } \
    }

#define ASC_DBG_PRT_ASC_QDONE_INFO(lvl, qdone) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            asc_prt_asc_qdone_info(qdone); \
        } \
    }

#define ASC_DBG_PRT_ADV_SCSI_REQ_Q(lvl, scsiqp) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            asc_prt_adv_scsi_req_q(scsiqp); \
        } \
    }

#define ASC_DBG_PRT_HEX(lvl, name, start, length) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            asc_prt_hex((name), (start), (length)); \
        } \
    }

#define ASC_DBG_PRT_CDB(lvl, cdb, len) \
        ASC_DBG_PRT_HEX((lvl), "CDB", (uchar *) (cdb), (len));

#define ASC_DBG_PRT_SENSE(lvl, sense, len) \
        ASC_DBG_PRT_HEX((lvl), "SENSE", (uchar *) (sense), (len));

#define ASC_DBG_PRT_INQUIRY(lvl, inq, len) \
        ASC_DBG_PRT_HEX((lvl), "INQUIRY", (uchar *) (inq), (len));
#endif /* ADVANSYS_DEBUG */

#ifndef ADVANSYS_ASSERT
#define ASC_ASSERT(a)
#else /* ADVANSYS_ASSERT */

#define ASC_ASSERT(a) \
    { \
        if (!(a)) { \
            printk("ASC_ASSERT() Failure: file %s, line %d\n", \
                __FILE__, __LINE__); \
        } \
    }

#endif /* ADVANSYS_ASSERT */


/*
 * --- Driver Structures
 */

#ifdef ADVANSYS_STATS

/* Per board statistics structure */
struct asc_stats {
    /* Driver Entrypoint Statistics */
    ADV_DCNT queuecommand;    /* # calls to advansys_queuecommand() */
    ADV_DCNT reset;           /* # calls to advansys_eh_bus_reset() */
    ADV_DCNT biosparam;       /* # calls to advansys_biosparam() */
    ADV_DCNT interrupt;       /* # advansys_interrupt() calls */
    ADV_DCNT callback;        /* # calls to asc/adv_isr_callback() */
    ADV_DCNT done;            /* # calls to request's scsi_done function */
    ADV_DCNT build_error;     /* # asc/adv_build_req() ASC_ERROR returns. */
    ADV_DCNT adv_build_noreq; /* # adv_build_req() adv_req_t alloc. fail. */
    ADV_DCNT adv_build_nosg;  /* # adv_build_req() adv_sgblk_t alloc. fail. */
    /* AscExeScsiQueue()/AdvExeScsiQueue() Statistics */
    ADV_DCNT exe_noerror;     /* # ASC_NOERROR returns. */
    ADV_DCNT exe_busy;        /* # ASC_BUSY returns. */
    ADV_DCNT exe_error;       /* # ASC_ERROR returns. */
    ADV_DCNT exe_unknown;     /* # unknown returns. */
    /* Data Transfer Statistics */
    ADV_DCNT cont_cnt;        /* # non-scatter-gather I/O requests received */
    ADV_DCNT cont_xfer;       /* # contiguous transfer 512-bytes */
    ADV_DCNT sg_cnt;          /* # scatter-gather I/O requests received */
    ADV_DCNT sg_elem;         /* # scatter-gather elements */
    ADV_DCNT sg_xfer;         /* # scatter-gather transfer 512-bytes */
};
#endif /* ADVANSYS_STATS */

/*
 * Request queuing structure
 */
typedef struct asc_queue {
    ADV_SCSI_BIT_ID_TYPE  q_tidmask;                /* queue mask */
    REQP                  q_first[ADV_MAX_TID+1];   /* first queued request */
    REQP                  q_last[ADV_MAX_TID+1];    /* last queued request */
#ifdef ADVANSYS_STATS
    short                 q_cur_cnt[ADV_MAX_TID+1]; /* current queue count */
    short                 q_max_cnt[ADV_MAX_TID+1]; /* maximum queue count */
    ADV_DCNT              q_tot_cnt[ADV_MAX_TID+1]; /* total enqueue count */
    ADV_DCNT              q_tot_tim[ADV_MAX_TID+1]; /* total time queued */
    ushort                q_max_tim[ADV_MAX_TID+1]; /* maximum time queued */
    ushort                q_min_tim[ADV_MAX_TID+1]; /* minimum time queued */
#endif /* ADVANSYS_STATS */
} asc_queue_t;

/*
 * Adv Library Request Structures
 *
 * The following two structures are used to process Wide Board requests.
 *
 * The ADV_SCSI_REQ_Q structure in adv_req_t is passed to the Adv Library
 * and microcode with the ADV_SCSI_REQ_Q field 'srb_ptr' pointing to the
 * adv_req_t. The adv_req_t structure 'cmndp' field in turn points to the
 * Mid-Level SCSI request structure.
 *
 * Zero or more ADV_SG_BLOCK are used with each ADV_SCSI_REQ_Q. Each
 * ADV_SG_BLOCK structure holds 15 scatter-gather elements. Under Linux
 * up to 255 scatter-gather elements may be used per request or
 * ADV_SCSI_REQ_Q.
 *
 * Both structures must be 32 byte aligned.
 */
typedef struct adv_sgblk {
    ADV_SG_BLOCK        sg_block;     /* Sgblock structure. */
    uchar               align[32];    /* Sgblock structure padding. */
    struct adv_sgblk    *next_sgblkp; /* Next scatter-gather structure. */
} adv_sgblk_t;

typedef struct adv_req {
    ADV_SCSI_REQ_Q      scsi_req_q;   /* Adv Library request structure. */
    uchar               align[32];    /* Request structure padding. */
    struct scsi_cmnd	*cmndp;       /* Mid-Level SCSI command pointer. */
    adv_sgblk_t         *sgblkp;      /* Adv Library scatter-gather pointer. */
    struct adv_req      *next_reqp;   /* Next Request Structure. */
} adv_req_t;

/*
 * Structure allocated for each board.
 *
 * This structure is allocated by scsi_register() at the end
 * of the 'Scsi_Host' structure starting at the 'hostdata'
 * field. It is guaranteed to be allocated from DMA-able memory.
 */
typedef struct asc_board {
    int                  id;                    /* Board Id */
    uint                 flags;                 /* Board flags */
    union {
        ASC_DVC_VAR      asc_dvc_var;           /* Narrow board */
        ADV_DVC_VAR      adv_dvc_var;           /* Wide board */
    } dvc_var;
    union {
        ASC_DVC_CFG      asc_dvc_cfg;           /* Narrow board */
        ADV_DVC_CFG      adv_dvc_cfg;           /* Wide board */
    } dvc_cfg;
    ushort               asc_n_io_port;         /* Number I/O ports. */
    asc_queue_t          active;                /* Active command queue */
    asc_queue_t          waiting;               /* Waiting command queue */
    asc_queue_t          done;                  /* Done command queue */
    ADV_SCSI_BIT_ID_TYPE init_tidmask;          /* Target init./valid mask */
    struct scsi_device	*device[ADV_MAX_TID+1]; /* Mid-Level Scsi Device */
    ushort               reqcnt[ADV_MAX_TID+1]; /* Starvation request count */
    ADV_SCSI_BIT_ID_TYPE queue_full;            /* Queue full mask */
    ushort               queue_full_cnt[ADV_MAX_TID+1]; /* Queue full count */
    union {
        ASCEEP_CONFIG         asc_eep;          /* Narrow EEPROM config. */
        ADVEEP_3550_CONFIG    adv_3550_eep;     /* 3550 EEPROM config. */
        ADVEEP_38C0800_CONFIG adv_38C0800_eep;  /* 38C0800 EEPROM config. */
        ADVEEP_38C1600_CONFIG adv_38C1600_eep;  /* 38C1600 EEPROM config. */
    } eep_config;
    ulong                last_reset;            /* Saved last reset time */
    spinlock_t lock;                            /* Board spinlock */
#ifdef CONFIG_PROC_FS
    /* /proc/scsi/advansys/[0...] */
    char                 *prtbuf;               /* /proc print buffer */
#endif /* CONFIG_PROC_FS */
#ifdef ADVANSYS_STATS
    struct asc_stats     asc_stats;             /* Board statistics */
#endif /* ADVANSYS_STATS */
    /*
     * The following fields are used only for Narrow Boards.
     */
    /* The following three structures must be in DMA-able memory. */
    ASC_SCSI_REQ_Q       scsireqq;
    ASC_CAP_INFO         cap_info;
    ASC_SCSI_INQUIRY     inquiry;
    uchar                sdtr_data[ASC_MAX_TID+1]; /* SDTR information */
    /*
     * The following fields are used only for Wide Boards.
     */
    void                 __iomem *ioremap_addr; /* I/O Memory remap address. */
    ushort               ioport;                /* I/O Port address. */
    ADV_CARR_T           *orig_carrp;           /* ADV_CARR_T memory block. */
    adv_req_t            *orig_reqp;            /* adv_req_t memory block. */
    adv_req_t            *adv_reqp;             /* Request structures. */
    adv_sgblk_t          *adv_sgblkp;           /* Scatter-gather structures. */
    ushort               bios_signature;        /* BIOS Signature. */
    ushort               bios_version;          /* BIOS Version. */
    ushort               bios_codeseg;          /* BIOS Code Segment. */
    ushort               bios_codelen;          /* BIOS Code Segment Length. */
} asc_board_t;

/*
 * PCI configuration structures
 */
typedef struct _PCI_DATA_
{
    uchar    type;
    uchar    bus;
    uchar    slot;
    uchar    func;
    uchar    offset;
} PCI_DATA;

typedef struct _PCI_DEVICE_
{
    ushort   vendorID;
    ushort   deviceID;
    ushort   slotNumber;
    ushort   slotFound;
    uchar    busNumber;
    uchar    maxBusNumber;
    uchar    devFunc;
    ushort   startSlot;
    ushort   endSlot;
    uchar    bridge;
    uchar    type;
} PCI_DEVICE;

typedef struct _PCI_CONFIG_SPACE_
{
    ushort   vendorID;
    ushort   deviceID;
    ushort   command;
    ushort   status;
    uchar    revision;
    uchar    classCode[3];
    uchar    cacheSize;
    uchar    latencyTimer;
    uchar    headerType;
    uchar    bist;
    ADV_PADDR baseAddress[6];
    ushort   reserved[4];
    ADV_PADDR optionRomAddr;
    ushort   reserved2[4];
    uchar    irqLine;
    uchar    irqPin;
    uchar    minGnt;
    uchar    maxLatency;
} PCI_CONFIG_SPACE;


/*
 * --- Driver Data
 */

/* Note: All driver global data should be initialized. */

/* Number of boards detected in system. */
STATIC int asc_board_count = 0;
STATIC struct Scsi_Host    *asc_host[ASC_NUM_BOARD_SUPPORTED] = { NULL };

/* Overrun buffer used by all narrow boards. */
STATIC uchar overrun_buf[ASC_OVERRUN_BSIZE] = { 0 };

/*
 * Global structures required to issue a command.
 */
STATIC ASC_SCSI_Q asc_scsi_q = { { 0 } };
STATIC ASC_SG_HEAD asc_sg_head = { 0 };

/* List of supported bus types. */
STATIC ushort asc_bus[ASC_NUM_BUS] __initdata = {
    ASC_IS_ISA,
    ASC_IS_VL,
    ASC_IS_EISA,
    ASC_IS_PCI,
};

STATIC int asc_iopflag = ASC_FALSE;
STATIC int asc_ioport[ASC_NUM_IOPORT_PROBE] = { 0, 0, 0, 0 };

#ifdef ADVANSYS_DEBUG
STATIC char *
asc_bus_name[ASC_NUM_BUS] = {
    "ASC_IS_ISA",
    "ASC_IS_VL",
    "ASC_IS_EISA",
    "ASC_IS_PCI",
};

STATIC int          asc_dbglvl = 3;
#endif /* ADVANSYS_DEBUG */

/* Declaration for Asc Library internal data referenced by driver. */
STATIC PortAddr     _asc_def_iop_base[];


/*
 * --- Driver Function Prototypes
 *
 * advansys.h contains function prototypes for functions global to Linux.
 */

STATIC irqreturn_t advansys_interrupt(int, void *);
STATIC int	  advansys_slave_configure(struct scsi_device *);
STATIC void       asc_scsi_done_list(struct scsi_cmnd *);
STATIC int        asc_execute_scsi_cmnd(struct scsi_cmnd *);
STATIC int        asc_build_req(asc_board_t *, struct scsi_cmnd *);
STATIC int        adv_build_req(asc_board_t *, struct scsi_cmnd *, ADV_SCSI_REQ_Q **);
STATIC int        adv_get_sglist(asc_board_t *, adv_req_t *, struct scsi_cmnd *, int);
STATIC void       asc_isr_callback(ASC_DVC_VAR *, ASC_QDONE_INFO *);
STATIC void       adv_isr_callback(ADV_DVC_VAR *, ADV_SCSI_REQ_Q *);
STATIC void       adv_async_callback(ADV_DVC_VAR *, uchar);
STATIC void       asc_enqueue(asc_queue_t *, REQP, int);
STATIC REQP       asc_dequeue(asc_queue_t *, int);
STATIC REQP       asc_dequeue_list(asc_queue_t *, REQP *, int);
STATIC int        asc_rmqueue(asc_queue_t *, REQP);
STATIC void       asc_execute_queue(asc_queue_t *);
#ifdef CONFIG_PROC_FS
STATIC int        asc_proc_copy(off_t, off_t, char *, int , char *, int);
STATIC int        asc_prt_board_devices(struct Scsi_Host *, char *, int);
STATIC int        asc_prt_adv_bios(struct Scsi_Host *, char *, int);
STATIC int        asc_get_eeprom_string(ushort *serialnum, uchar *cp);
STATIC int        asc_prt_asc_board_eeprom(struct Scsi_Host *, char *, int);
STATIC int        asc_prt_adv_board_eeprom(struct Scsi_Host *, char *, int);
STATIC int        asc_prt_driver_conf(struct Scsi_Host *, char *, int);
STATIC int        asc_prt_asc_board_info(struct Scsi_Host *, char *, int);
STATIC int        asc_prt_adv_board_info(struct Scsi_Host *, char *, int);
STATIC int        asc_prt_line(char *, int, char *fmt, ...);
#endif /* CONFIG_PROC_FS */

/* Declaration for Asc Library internal functions referenced by driver. */
STATIC int          AscFindSignature(PortAddr);
STATIC ushort       AscGetEEPConfig(PortAddr, ASCEEP_CONFIG *, ushort);

/* Statistics function prototypes. */
#ifdef ADVANSYS_STATS
#ifdef CONFIG_PROC_FS
STATIC int          asc_prt_board_stats(struct Scsi_Host *, char *, int);
STATIC int          asc_prt_target_stats(struct Scsi_Host *, int, char *, int);
#endif /* CONFIG_PROC_FS */
#endif /* ADVANSYS_STATS */

/* Debug function prototypes. */
#ifdef ADVANSYS_DEBUG
STATIC void         asc_prt_scsi_host(struct Scsi_Host *);
STATIC void         asc_prt_scsi_cmnd(struct scsi_cmnd *);
STATIC void         asc_prt_asc_dvc_cfg(ASC_DVC_CFG *);
STATIC void         asc_prt_asc_dvc_var(ASC_DVC_VAR *);
STATIC void         asc_prt_asc_scsi_q(ASC_SCSI_Q *);
STATIC void         asc_prt_asc_qdone_info(ASC_QDONE_INFO *);
STATIC void         asc_prt_adv_dvc_cfg(ADV_DVC_CFG *);
STATIC void         asc_prt_adv_dvc_var(ADV_DVC_VAR *);
STATIC void         asc_prt_adv_scsi_req_q(ADV_SCSI_REQ_Q *);
STATIC void         asc_prt_adv_sgblock(int, ADV_SG_BLOCK *);
STATIC void         asc_prt_hex(char *f, uchar *, int);
#endif /* ADVANSYS_DEBUG */


#ifdef CONFIG_PROC_FS
/*
 * advansys_proc_info() - /proc/scsi/advansys/[0-(ASC_NUM_BOARD_SUPPORTED-1)]
 *
 * *buffer: I/O buffer
 * **start: if inout == FALSE pointer into buffer where user read should start
 * offset: current offset into a /proc/scsi/advansys/[0...] file
 * length: length of buffer
 * hostno: Scsi_Host host_no
 * inout: TRUE - user is writing; FALSE - user is reading
 *
 * Return the number of bytes read from or written to a
 * /proc/scsi/advansys/[0...] file.
 *
 * Note: This function uses the per board buffer 'prtbuf' which is
 * allocated when the board is initialized in advansys_detect(). The
 * buffer is ASC_PRTBUF_SIZE bytes. The function asc_proc_copy() is
 * used to write to the buffer. The way asc_proc_copy() is written
 * if 'prtbuf' is too small it will not be overwritten. Instead the
 * user just won't get all the available statistics.
 */
static int
advansys_proc_info(struct Scsi_Host *shost, char *buffer, char **start,
		off_t offset, int length, int inout)
{
    struct Scsi_Host    *shp;
    asc_board_t         *boardp;
    int                 i;
    char                *cp;
    int			cplen;
    int                 cnt;
    int                 totcnt;
    int                 leftlen;
    char                *curbuf;
    off_t               advoffset;
#ifdef ADVANSYS_STATS
    int                 tgt_id;
#endif /* ADVANSYS_STATS */

    ASC_DBG(1, "advansys_proc_info: begin\n");

    /*
     * User write not supported.
     */
    if (inout == TRUE) {
        return(-ENOSYS);
    }

    /*
     * User read of /proc/scsi/advansys/[0...] file.
     */

    /* Find the specified board. */
    for (i = 0; i < asc_board_count; i++) {
        if (asc_host[i]->host_no == shost->host_no) {
            break;
        }
    }
    if (i == asc_board_count) {
        return(-ENOENT);
    }

    shp = asc_host[i];
    boardp = ASC_BOARDP(shp);

    /* Copy read data starting at the beginning of the buffer. */
    *start = buffer;
    curbuf = buffer;
    advoffset = 0;
    totcnt = 0;
    leftlen = length;

    /*
     * Get board configuration information.
     *
     * advansys_info() returns the board string from its own static buffer.
     */
    cp = (char *) advansys_info(shp);
    strcat(cp, "\n");
    cplen = strlen(cp);
    /* Copy board information. */
    cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
    totcnt += cnt;
    leftlen -= cnt;
    if (leftlen == 0) {
        ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
        return totcnt;
    }
    advoffset += cplen;
    curbuf += cnt;

    /*
     * Display Wide Board BIOS Information.
     */
    if (ASC_WIDE_BOARD(boardp)) {
        cp = boardp->prtbuf;
        cplen = asc_prt_adv_bios(shp, cp, ASC_PRTBUF_SIZE);
        ASC_ASSERT(cplen < ASC_PRTBUF_SIZE);
        cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
        totcnt += cnt;
        leftlen -= cnt;
        if (leftlen == 0) {
            ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
            return totcnt;
        }
        advoffset += cplen;
        curbuf += cnt;
    }

    /*
     * Display driver information for each device attached to the board.
     */
    cp = boardp->prtbuf;
    cplen = asc_prt_board_devices(shp, cp, ASC_PRTBUF_SIZE);
    ASC_ASSERT(cplen < ASC_PRTBUF_SIZE);
    cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
    totcnt += cnt;
    leftlen -= cnt;
    if (leftlen == 0) {
        ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
        return totcnt;
    }
    advoffset += cplen;
    curbuf += cnt;

    /*
     * Display EEPROM configuration for the board.
     */
    cp = boardp->prtbuf;
    if (ASC_NARROW_BOARD(boardp)) {
        cplen = asc_prt_asc_board_eeprom(shp, cp, ASC_PRTBUF_SIZE);
    } else {
        cplen = asc_prt_adv_board_eeprom(shp, cp, ASC_PRTBUF_SIZE);
    }
    ASC_ASSERT(cplen < ASC_PRTBUF_SIZE);
    cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
    totcnt += cnt;
    leftlen -= cnt;
    if (leftlen == 0) {
        ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
        return totcnt;
    }
    advoffset += cplen;
    curbuf += cnt;

    /*
     * Display driver configuration and information for the board.
     */
    cp = boardp->prtbuf;
    cplen = asc_prt_driver_conf(shp, cp, ASC_PRTBUF_SIZE);
    ASC_ASSERT(cplen < ASC_PRTBUF_SIZE);
    cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
    totcnt += cnt;
    leftlen -= cnt;
    if (leftlen == 0) {
        ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
        return totcnt;
    }
    advoffset += cplen;
    curbuf += cnt;

#ifdef ADVANSYS_STATS
    /*
     * Display driver statistics for the board.
     */
    cp = boardp->prtbuf;
    cplen = asc_prt_board_stats(shp, cp, ASC_PRTBUF_SIZE);
    ASC_ASSERT(cplen <= ASC_PRTBUF_SIZE);
    cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
    totcnt += cnt;
    leftlen -= cnt;
    if (leftlen == 0) {
        ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
        return totcnt;
    }
    advoffset += cplen;
    curbuf += cnt;

    /*
     * Display driver statistics for each target.
     */
    for (tgt_id = 0; tgt_id <= ADV_MAX_TID; tgt_id++) {
      cp = boardp->prtbuf;
      cplen = asc_prt_target_stats(shp, tgt_id, cp, ASC_PRTBUF_SIZE);
      ASC_ASSERT(cplen <= ASC_PRTBUF_SIZE);
      cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
      totcnt += cnt;
      leftlen -= cnt;
      if (leftlen == 0) {
        ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
        return totcnt;
      }
      advoffset += cplen;
      curbuf += cnt;
    }
#endif /* ADVANSYS_STATS */

    /*
     * Display Asc Library dynamic configuration information
     * for the board.
     */
    cp = boardp->prtbuf;
    if (ASC_NARROW_BOARD(boardp)) {
        cplen = asc_prt_asc_board_info(shp, cp, ASC_PRTBUF_SIZE);
    } else {
        cplen = asc_prt_adv_board_info(shp, cp, ASC_PRTBUF_SIZE);
    }
    ASC_ASSERT(cplen < ASC_PRTBUF_SIZE);
    cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
    totcnt += cnt;
    leftlen -= cnt;
    if (leftlen == 0) {
        ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);
        return totcnt;
    }
    advoffset += cplen;
    curbuf += cnt;

    ASC_DBG1(1, "advansys_proc_info: totcnt %d\n", totcnt);

    return totcnt;
}
#endif /* CONFIG_PROC_FS */

/*
 * advansys_detect()
 *
 * Detect function for AdvanSys adapters.
 *
 * Argument is a pointer to the host driver's scsi_hosts entry.
 *
 * Return number of adapters found.
 *
 * Note: Because this function is called during system initialization
 * it must not call SCSI mid-level functions including scsi_malloc()
 * and scsi_free().
 */
static int __init
advansys_detect(struct scsi_host_template *tpnt)
{
    static int          detect_called = ASC_FALSE;
    int                 iop;
    int                 bus;
    struct Scsi_Host    *shp = NULL;
    asc_board_t         *boardp = NULL;
    ASC_DVC_VAR         *asc_dvc_varp = NULL;
    ADV_DVC_VAR         *adv_dvc_varp = NULL;
    adv_sgblk_t         *sgp = NULL;
    int                 ioport = 0;
    int                 share_irq = FALSE;
    int                 iolen = 0;
    struct device	*dev = NULL;
#ifdef CONFIG_PCI
    int                 pci_init_search = 0;
    struct pci_dev      *pci_devicep[ASC_NUM_BOARD_SUPPORTED];
    int                 pci_card_cnt_max = 0;
    int                 pci_card_cnt = 0;
    struct pci_dev      *pci_devp = NULL;
    int                 pci_device_id_cnt = 0;
    unsigned int        pci_device_id[ASC_PCI_DEVICE_ID_CNT] = {
                                    PCI_DEVICE_ID_ASP_1200A,
                                    PCI_DEVICE_ID_ASP_ABP940,
                                    PCI_DEVICE_ID_ASP_ABP940U,
                                    PCI_DEVICE_ID_ASP_ABP940UW,
                                    PCI_DEVICE_ID_38C0800_REV1,
                                    PCI_DEVICE_ID_38C1600_REV1
                        };
    ADV_PADDR           pci_memory_address;
#endif /* CONFIG_PCI */
    int                 warn_code, err_code;
    int                 ret;

    if (detect_called == ASC_FALSE) {
        detect_called = ASC_TRUE;
    } else {
        printk("AdvanSys SCSI: advansys_detect() multiple calls ignored\n");
        return 0;
    }

    ASC_DBG(1, "advansys_detect: begin\n");

    asc_board_count = 0;

    /*
     * If I/O port probing has been modified, then verify and
     * clean-up the 'asc_ioport' list.
     */
    if (asc_iopflag == ASC_TRUE) {
        for (ioport = 0; ioport < ASC_NUM_IOPORT_PROBE; ioport++) {
            ASC_DBG2(1, "advansys_detect: asc_ioport[%d] 0x%x\n",
                ioport, asc_ioport[ioport]);
            if (asc_ioport[ioport] != 0) {
                for (iop = 0; iop < ASC_IOADR_TABLE_MAX_IX; iop++) {
                    if (_asc_def_iop_base[iop] == asc_ioport[ioport]) {
                        break;
                    }
                }
                if (iop == ASC_IOADR_TABLE_MAX_IX) {
                    printk(
"AdvanSys SCSI: specified I/O Port 0x%X is invalid\n",
                        asc_ioport[ioport]);
                    asc_ioport[ioport] = 0;
                }
            }
        }
        ioport = 0;
    }

    for (bus = 0; bus < ASC_NUM_BUS; bus++) {

        ASC_DBG2(1, "advansys_detect: bus search type %d (%s)\n",
            bus, asc_bus_name[bus]);
        iop = 0;

        while (asc_board_count < ASC_NUM_BOARD_SUPPORTED) {

            ASC_DBG1(2, "advansys_detect: asc_board_count %d\n",
                asc_board_count);

            switch (asc_bus[bus]) {
            case ASC_IS_ISA:
            case ASC_IS_VL:
#ifdef CONFIG_ISA
                if (asc_iopflag == ASC_FALSE) {
                    iop = AscSearchIOPortAddr(iop, asc_bus[bus]);
                } else {
                    /*
                     * ISA and VL I/O port scanning has either been
                     * eliminated or limited to selected ports on
                     * the LILO command line, /etc/lilo.conf, or
                     * by setting variables when the module was loaded.
                     */
                    ASC_DBG(1, "advansys_detect: I/O port scanning modified\n");
                ioport_try_again:
                    iop = 0;
                    for (; ioport < ASC_NUM_IOPORT_PROBE; ioport++) {
                        if ((iop = asc_ioport[ioport]) != 0) {
                            break;
                        }
                    }
                    if (iop) {
                        ASC_DBG1(1,
                                "advansys_detect: probing I/O port 0x%x...\n",
                            iop);
			if (!request_region(iop, ASC_IOADR_GAP, "advansys")){
                            printk(
"AdvanSys SCSI: specified I/O Port 0x%X is busy\n", iop);
                            /* Don't try this I/O port twice. */
                            asc_ioport[ioport] = 0;
                            goto ioport_try_again;
                        } else if (AscFindSignature(iop) == ASC_FALSE) {
                            printk(
"AdvanSys SCSI: specified I/O Port 0x%X has no adapter\n", iop);
                            /* Don't try this I/O port twice. */
			    release_region(iop, ASC_IOADR_GAP);
                            asc_ioport[ioport] = 0;
                            goto ioport_try_again;
                        } else {
                            /*
                             * If this isn't an ISA board, then it must be
                             * a VL board. If currently looking an ISA
                             * board is being looked for then try for
                             * another ISA board in 'asc_ioport'.
                             */
                            if (asc_bus[bus] == ASC_IS_ISA &&
                                (AscGetChipVersion(iop, ASC_IS_ISA) &
                                 ASC_CHIP_VER_ISA_BIT) == 0) {
                                 /*
                                  * Don't clear 'asc_ioport[ioport]'. Try
                                  * this board again for VL. Increment
                                  * 'ioport' past this board.
                                  */
                                 ioport++;
				 release_region(iop, ASC_IOADR_GAP);
                                 goto ioport_try_again;
                            }
                        }
                        /*
                         * This board appears good, don't try the I/O port
                         * again by clearing its value. Increment 'ioport'
                         * for the next iteration.
                         */
                        asc_ioport[ioport++] = 0;
                    }
                }
#endif /* CONFIG_ISA */
                break;

            case ASC_IS_EISA:
#ifdef CONFIG_ISA
                iop = AscSearchIOPortAddr(iop, asc_bus[bus]);
#endif /* CONFIG_ISA */
                break;

            case ASC_IS_PCI:
#ifdef CONFIG_PCI
                if (pci_init_search == 0) {
                    int i, j;

                    pci_init_search = 1;

                    /* Find all PCI cards. */
                    while (pci_device_id_cnt < ASC_PCI_DEVICE_ID_CNT) {
                        if ((pci_devp = pci_find_device(PCI_VENDOR_ID_ASP,
                            pci_device_id[pci_device_id_cnt], pci_devp)) ==
                            NULL) {
                            pci_device_id_cnt++;
                        } else {
                            if (pci_enable_device(pci_devp) == 0) {
                                pci_devicep[pci_card_cnt_max++] = pci_devp;
                            }
                        }
                    }

                    /*
                     * Sort PCI cards in ascending order by PCI Bus, Slot,
                     * and Device Number.
                     */
                    for (i = 0; i < pci_card_cnt_max - 1; i++)
                    {
                        for (j = i + 1; j < pci_card_cnt_max; j++) {
                            if ((pci_devicep[j]->bus->number <
                                 pci_devicep[i]->bus->number) ||
                                ((pci_devicep[j]->bus->number ==
                                  pci_devicep[i]->bus->number) &&
                                  (pci_devicep[j]->devfn <
                                   pci_devicep[i]->devfn))) {
                                pci_devp = pci_devicep[i];
                                pci_devicep[i] = pci_devicep[j];
                                pci_devicep[j] = pci_devp;
                            }
                        }
                    }

                    pci_card_cnt = 0;
                } else {
                    pci_card_cnt++;
                }

                if (pci_card_cnt == pci_card_cnt_max) {
                    iop = 0;
                } else {
                    pci_devp = pci_devicep[pci_card_cnt];

                    ASC_DBG2(2,
                        "advansys_detect: devfn %d, bus number %d\n",
                        pci_devp->devfn, pci_devp->bus->number);
                    iop = pci_resource_start(pci_devp, 0);
                    ASC_DBG2(1,
                        "advansys_detect: vendorID %X, deviceID %X\n",
                        pci_devp->vendor, pci_devp->device);
                    ASC_DBG2(2, "advansys_detect: iop %X, irqLine %d\n",
                        iop, pci_devp->irq);
                }
		if(pci_devp)
		    dev = &pci_devp->dev;

#endif /* CONFIG_PCI */
                break;

            default:
                ASC_PRINT1("advansys_detect: unknown bus type: %d\n",
                    asc_bus[bus]);
                break;
            }
            ASC_DBG1(1, "advansys_detect: iop 0x%x\n", iop);

            /*
             * Adapter not found, try next bus type.
             */
            if (iop == 0) {
                break;
            }

            /*
             * Adapter found.
             *
             * Register the adapter, get its configuration, and
             * initialize it.
             */
            ASC_DBG(2, "advansys_detect: scsi_register()\n");
            shp = scsi_register(tpnt, sizeof(asc_board_t));

            if (shp == NULL) {
                continue;
            }

            /* Save a pointer to the Scsi_Host of each board found. */
            asc_host[asc_board_count++] = shp;

            /* Initialize private per board data */
            boardp = ASC_BOARDP(shp);
            memset(boardp, 0, sizeof(asc_board_t));
            boardp->id = asc_board_count - 1;

            /* Initialize spinlock. */
            spin_lock_init(&boardp->lock);

            /*
             * Handle both narrow and wide boards.
             *
             * If a Wide board was detected, set the board structure
             * wide board flag. Set-up the board structure based on
             * the board type.
             */
#ifdef CONFIG_PCI
            if (asc_bus[bus] == ASC_IS_PCI &&
                (pci_devp->device == PCI_DEVICE_ID_ASP_ABP940UW ||
                 pci_devp->device == PCI_DEVICE_ID_38C0800_REV1 ||
                 pci_devp->device == PCI_DEVICE_ID_38C1600_REV1))
            {
                boardp->flags |= ASC_IS_WIDE_BOARD;
            }
#endif /* CONFIG_PCI */

            if (ASC_NARROW_BOARD(boardp)) {
                ASC_DBG(1, "advansys_detect: narrow board\n");
                asc_dvc_varp = &boardp->dvc_var.asc_dvc_var;
                asc_dvc_varp->bus_type = asc_bus[bus];
                asc_dvc_varp->drv_ptr = boardp;
                asc_dvc_varp->cfg = &boardp->dvc_cfg.asc_dvc_cfg;
                asc_dvc_varp->cfg->overrun_buf = &overrun_buf[0];
                asc_dvc_varp->iop_base = iop;
                asc_dvc_varp->isr_callback = asc_isr_callback;
            } else {
                ASC_DBG(1, "advansys_detect: wide board\n");
                adv_dvc_varp = &boardp->dvc_var.adv_dvc_var;
                adv_dvc_varp->drv_ptr = boardp;
                adv_dvc_varp->cfg = &boardp->dvc_cfg.adv_dvc_cfg;
                adv_dvc_varp->isr_callback = adv_isr_callback;
                adv_dvc_varp->async_callback = adv_async_callback;
#ifdef CONFIG_PCI
                if (pci_devp->device == PCI_DEVICE_ID_ASP_ABP940UW)
                {
                    ASC_DBG(1, "advansys_detect: ASC-3550\n");
                    adv_dvc_varp->chip_type = ADV_CHIP_ASC3550;
                } else if (pci_devp->device == PCI_DEVICE_ID_38C0800_REV1)
                {
                    ASC_DBG(1, "advansys_detect: ASC-38C0800\n");
                    adv_dvc_varp->chip_type = ADV_CHIP_ASC38C0800;
                } else
                {
                    ASC_DBG(1, "advansys_detect: ASC-38C1600\n");
                    adv_dvc_varp->chip_type = ADV_CHIP_ASC38C1600;
                }
#endif /* CONFIG_PCI */

                /*
                 * Map the board's registers into virtual memory for
                 * PCI slave access. Only memory accesses are used to
                 * access the board's registers.
                 *
                 * Note: The PCI register base address is not always
                 * page aligned, but the address passed to ioremap()
                 * must be page aligned. It is guaranteed that the
                 * PCI register base address will not cross a page
                 * boundary.
                 */
                if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
                {
                    iolen = ADV_3550_IOLEN;
                } else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800)
                {
                    iolen = ADV_38C0800_IOLEN;
                } else
                {
                    iolen = ADV_38C1600_IOLEN;
                }
#ifdef CONFIG_PCI
                pci_memory_address = pci_resource_start(pci_devp, 1);
                ASC_DBG1(1, "advansys_detect: pci_memory_address: 0x%lx\n",
                    (ulong) pci_memory_address);
                if ((boardp->ioremap_addr =
                    ioremap(pci_memory_address & PAGE_MASK,
                         PAGE_SIZE)) == 0) {
                   ASC_PRINT3(
"advansys_detect: board %d: ioremap(%x, %d) returned NULL\n",
                       boardp->id, pci_memory_address, iolen);
                   scsi_unregister(shp);
                   asc_board_count--;
                   continue;
                }
                ASC_DBG1(1, "advansys_detect: ioremap_addr: 0x%lx\n",
                    (ulong) boardp->ioremap_addr);
                adv_dvc_varp->iop_base = (AdvPortAddr)
                    (boardp->ioremap_addr +
                     (pci_memory_address - (pci_memory_address & PAGE_MASK)));
                ASC_DBG1(1, "advansys_detect: iop_base: 0x%lx\n",
                    adv_dvc_varp->iop_base);
#endif /* CONFIG_PCI */

                /*
                 * Even though it isn't used to access wide boards, other
                 * than for the debug line below, save I/O Port address so
                 * that it can be reported.
                 */
                boardp->ioport = iop;

                ASC_DBG2(1,
"advansys_detect: iopb_chip_id_1 0x%x, iopw_chip_id_0 0x%x\n",
                    (ushort) inp(iop + 1), (ushort) inpw(iop));
            }

#ifdef CONFIG_PROC_FS
            /*
             * Allocate buffer for printing information from
             * /proc/scsi/advansys/[0...].
             */
            if ((boardp->prtbuf =
                kmalloc(ASC_PRTBUF_SIZE, GFP_ATOMIC)) == NULL) {
                ASC_PRINT3(
"advansys_detect: board %d: kmalloc(%d, %d) returned NULL\n",
                    boardp->id, ASC_PRTBUF_SIZE, GFP_ATOMIC);
                scsi_unregister(shp);
                asc_board_count--;
                continue;
            }
#endif /* CONFIG_PROC_FS */

            if (ASC_NARROW_BOARD(boardp)) {
		asc_dvc_varp->cfg->dev = dev;
		/*
                 * Set the board bus type and PCI IRQ before
                 * calling AscInitGetConfig().
                 */
                switch (asc_dvc_varp->bus_type) {
#ifdef CONFIG_ISA
                case ASC_IS_ISA:
                    shp->unchecked_isa_dma = TRUE;
                    share_irq = FALSE;
                    break;
                case ASC_IS_VL:
                    shp->unchecked_isa_dma = FALSE;
                    share_irq = FALSE;
                    break;
                case ASC_IS_EISA:
                    shp->unchecked_isa_dma = FALSE;
                    share_irq = TRUE;
                    break;
#endif /* CONFIG_ISA */
#ifdef CONFIG_PCI
                case ASC_IS_PCI:
                    shp->irq = asc_dvc_varp->irq_no = pci_devp->irq;
                    asc_dvc_varp->cfg->pci_slot_info =
                        ASC_PCI_MKID(pci_devp->bus->number,
                            PCI_SLOT(pci_devp->devfn),
                            PCI_FUNC(pci_devp->devfn));
                    shp->unchecked_isa_dma = FALSE;
                    share_irq = TRUE;
                    break;
#endif /* CONFIG_PCI */
                default:
                    ASC_PRINT2(
"advansys_detect: board %d: unknown adapter type: %d\n",
                        boardp->id, asc_dvc_varp->bus_type);
                    shp->unchecked_isa_dma = TRUE;
                    share_irq = FALSE;
                    break;
                }
            } else {
                adv_dvc_varp->cfg->dev = dev;
                /*
                 * For Wide boards set PCI information before calling
                 * AdvInitGetConfig().
                 */
#ifdef CONFIG_PCI
                shp->irq = adv_dvc_varp->irq_no = pci_devp->irq;
                adv_dvc_varp->cfg->pci_slot_info =
                    ASC_PCI_MKID(pci_devp->bus->number,
                        PCI_SLOT(pci_devp->devfn),
                        PCI_FUNC(pci_devp->devfn));
                shp->unchecked_isa_dma = FALSE;
                share_irq = TRUE;
#endif /* CONFIG_PCI */
            }

            /*
             * Read the board configuration.
             */
            if (ASC_NARROW_BOARD(boardp)) {
                 /*
                  * NOTE: AscInitGetConfig() may change the board's
                  * bus_type value. The asc_bus[bus] value should no
                  * longer be used. If the bus_type field must be
                  * referenced only use the bit-wise AND operator "&".
                  */
                ASC_DBG(2, "advansys_detect: AscInitGetConfig()\n");
                switch(ret = AscInitGetConfig(asc_dvc_varp)) {
                case 0:    /* No error */
                    break;
                case ASC_WARN_IO_PORT_ROTATE:
                    ASC_PRINT1(
"AscInitGetConfig: board %d: I/O port address modified\n",
                        boardp->id);
                    break;
                case ASC_WARN_AUTO_CONFIG:
                    ASC_PRINT1(
"AscInitGetConfig: board %d: I/O port increment switch enabled\n",
                        boardp->id);
                    break;
                case ASC_WARN_EEPROM_CHKSUM:
                    ASC_PRINT1(
"AscInitGetConfig: board %d: EEPROM checksum error\n",
                        boardp->id);
                    break;
                case ASC_WARN_IRQ_MODIFIED:
                    ASC_PRINT1(
"AscInitGetConfig: board %d: IRQ modified\n",
                        boardp->id);
                    break;
                case ASC_WARN_CMD_QNG_CONFLICT:
                    ASC_PRINT1(
"AscInitGetConfig: board %d: tag queuing enabled w/o disconnects\n",
                        boardp->id);
                    break;
                default:
                    ASC_PRINT2(
"AscInitGetConfig: board %d: unknown warning: 0x%x\n",
                        boardp->id, ret);
                    break;
                }
                if ((err_code = asc_dvc_varp->err_code) != 0) {
                    ASC_PRINT3(
"AscInitGetConfig: board %d error: init_state 0x%x, err_code 0x%x\n",
                        boardp->id, asc_dvc_varp->init_state,
                        asc_dvc_varp->err_code);
                }
            } else {
                ASC_DBG(2, "advansys_detect: AdvInitGetConfig()\n");
                if ((ret = AdvInitGetConfig(adv_dvc_varp)) != 0) {
                    ASC_PRINT2("AdvInitGetConfig: board %d: warning: 0x%x\n",
                        boardp->id, ret);
                }
                if ((err_code = adv_dvc_varp->err_code) != 0) {
                    ASC_PRINT2(
"AdvInitGetConfig: board %d error: err_code 0x%x\n",
                        boardp->id, adv_dvc_varp->err_code);
                }
            }

            if (err_code != 0) {
#ifdef CONFIG_PROC_FS
                kfree(boardp->prtbuf);
#endif /* CONFIG_PROC_FS */
                scsi_unregister(shp);
                asc_board_count--;
                continue;
            }

            /*
             * Save the EEPROM configuration so that it can be displayed
             * from /proc/scsi/advansys/[0...].
             */
            if (ASC_NARROW_BOARD(boardp)) {

                ASCEEP_CONFIG *ep;

                /*
                 * Set the adapter's target id bit in the 'init_tidmask' field.
                 */
                boardp->init_tidmask |=
                    ADV_TID_TO_TIDMASK(asc_dvc_varp->cfg->chip_scsi_id);

                /*
                 * Save EEPROM settings for the board.
                 */
                ep = &boardp->eep_config.asc_eep;

                ep->init_sdtr = asc_dvc_varp->cfg->sdtr_enable;
                ep->disc_enable = asc_dvc_varp->cfg->disc_enable;
                ep->use_cmd_qng = asc_dvc_varp->cfg->cmd_qng_enabled;
                ASC_EEP_SET_DMA_SPD(ep, asc_dvc_varp->cfg->isa_dma_speed);
                ep->start_motor = asc_dvc_varp->start_motor;
                ep->cntl = asc_dvc_varp->dvc_cntl;
                ep->no_scam = asc_dvc_varp->no_scam;
                ep->max_total_qng = asc_dvc_varp->max_total_qng;
                ASC_EEP_SET_CHIP_ID(ep, asc_dvc_varp->cfg->chip_scsi_id);
                /* 'max_tag_qng' is set to the same value for every device. */
                ep->max_tag_qng = asc_dvc_varp->cfg->max_tag_qng[0];
                ep->adapter_info[0] = asc_dvc_varp->cfg->adapter_info[0];
                ep->adapter_info[1] = asc_dvc_varp->cfg->adapter_info[1];
                ep->adapter_info[2] = asc_dvc_varp->cfg->adapter_info[2];
                ep->adapter_info[3] = asc_dvc_varp->cfg->adapter_info[3];
                ep->adapter_info[4] = asc_dvc_varp->cfg->adapter_info[4];
                ep->adapter_info[5] = asc_dvc_varp->cfg->adapter_info[5];

               /*
                * Modify board configuration.
                */
                ASC_DBG(2, "advansys_detect: AscInitSetConfig()\n");
                switch (ret = AscInitSetConfig(asc_dvc_varp)) {
                case 0:    /* No error. */
                    break;
                case ASC_WARN_IO_PORT_ROTATE:
                    ASC_PRINT1(
"AscInitSetConfig: board %d: I/O port address modified\n",
                        boardp->id);
                    break;
                case ASC_WARN_AUTO_CONFIG:
                    ASC_PRINT1(
"AscInitSetConfig: board %d: I/O port increment switch enabled\n",
                        boardp->id);
                    break;
                case ASC_WARN_EEPROM_CHKSUM:
                    ASC_PRINT1(
"AscInitSetConfig: board %d: EEPROM checksum error\n",
                        boardp->id);
                    break;
                case ASC_WARN_IRQ_MODIFIED:
                    ASC_PRINT1(
"AscInitSetConfig: board %d: IRQ modified\n",
                        boardp->id);
                    break;
                case ASC_WARN_CMD_QNG_CONFLICT:
                    ASC_PRINT1(
"AscInitSetConfig: board %d: tag queuing w/o disconnects\n",
                        boardp->id);
                    break;
                default:
                    ASC_PRINT2(
"AscInitSetConfig: board %d: unknown warning: 0x%x\n",
                        boardp->id, ret);
                    break;
                }
                if (asc_dvc_varp->err_code != 0) {
                    ASC_PRINT3(
"AscInitSetConfig: board %d error: init_state 0x%x, err_code 0x%x\n",
                        boardp->id, asc_dvc_varp->init_state,
                        asc_dvc_varp->err_code);
#ifdef CONFIG_PROC_FS
                    kfree(boardp->prtbuf);
#endif /* CONFIG_PROC_FS */
                    scsi_unregister(shp);
                    asc_board_count--;
                    continue;
                }

                /*
                 * Finish initializing the 'Scsi_Host' structure.
                 */
                /* AscInitSetConfig() will set the IRQ for non-PCI boards. */
                if ((asc_dvc_varp->bus_type & ASC_IS_PCI) == 0) {
                    shp->irq = asc_dvc_varp->irq_no;
                }
            } else {
                ADVEEP_3550_CONFIG      *ep_3550;
                ADVEEP_38C0800_CONFIG   *ep_38C0800;
                ADVEEP_38C1600_CONFIG   *ep_38C1600;

                /*
                 * Save Wide EEP Configuration Information.
                 */
                if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
                {
                    ep_3550 = &boardp->eep_config.adv_3550_eep;

                    ep_3550->adapter_scsi_id = adv_dvc_varp->chip_scsi_id;
                    ep_3550->max_host_qng = adv_dvc_varp->max_host_qng;
                    ep_3550->max_dvc_qng = adv_dvc_varp->max_dvc_qng;
                    ep_3550->termination = adv_dvc_varp->cfg->termination;
                    ep_3550->disc_enable = adv_dvc_varp->cfg->disc_enable;
                    ep_3550->bios_ctrl = adv_dvc_varp->bios_ctrl;
                    ep_3550->wdtr_able = adv_dvc_varp->wdtr_able;
                    ep_3550->sdtr_able = adv_dvc_varp->sdtr_able;
                    ep_3550->ultra_able = adv_dvc_varp->ultra_able;
                    ep_3550->tagqng_able = adv_dvc_varp->tagqng_able;
                    ep_3550->start_motor = adv_dvc_varp->start_motor;
                    ep_3550->scsi_reset_delay = adv_dvc_varp->scsi_reset_wait;
                    ep_3550->serial_number_word1 =
                        adv_dvc_varp->cfg->serial1;
                    ep_3550->serial_number_word2 =
                        adv_dvc_varp->cfg->serial2;
                    ep_3550->serial_number_word3 =
                        adv_dvc_varp->cfg->serial3;
                } else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800)
                {
                    ep_38C0800 = &boardp->eep_config.adv_38C0800_eep;

                    ep_38C0800->adapter_scsi_id = adv_dvc_varp->chip_scsi_id;
                    ep_38C0800->max_host_qng = adv_dvc_varp->max_host_qng;
                    ep_38C0800->max_dvc_qng = adv_dvc_varp->max_dvc_qng;
                    ep_38C0800->termination_lvd =
                        adv_dvc_varp->cfg->termination;
                    ep_38C0800->disc_enable = adv_dvc_varp->cfg->disc_enable;
                    ep_38C0800->bios_ctrl = adv_dvc_varp->bios_ctrl;
                    ep_38C0800->wdtr_able = adv_dvc_varp->wdtr_able;
                    ep_38C0800->tagqng_able = adv_dvc_varp->tagqng_able;
                    ep_38C0800->sdtr_speed1 = adv_dvc_varp->sdtr_speed1;
                    ep_38C0800->sdtr_speed2 = adv_dvc_varp->sdtr_speed2;
                    ep_38C0800->sdtr_speed3 = adv_dvc_varp->sdtr_speed3;
                    ep_38C0800->sdtr_speed4 = adv_dvc_varp->sdtr_speed4;
                    ep_38C0800->tagqng_able = adv_dvc_varp->tagqng_able;
                    ep_38C0800->start_motor = adv_dvc_varp->start_motor;
                    ep_38C0800->scsi_reset_delay =
                        adv_dvc_varp->scsi_reset_wait;
                    ep_38C0800->serial_number_word1 =
                        adv_dvc_varp->cfg->serial1;
                    ep_38C0800->serial_number_word2 =
                        adv_dvc_varp->cfg->serial2;
                    ep_38C0800->serial_number_word3 =
                        adv_dvc_varp->cfg->serial3;
                } else
                {
                    ep_38C1600 = &boardp->eep_config.adv_38C1600_eep;

                    ep_38C1600->adapter_scsi_id = adv_dvc_varp->chip_scsi_id;
                    ep_38C1600->max_host_qng = adv_dvc_varp->max_host_qng;
                    ep_38C1600->max_dvc_qng = adv_dvc_varp->max_dvc_qng;
                    ep_38C1600->termination_lvd =
                        adv_dvc_varp->cfg->termination;
                    ep_38C1600->disc_enable = adv_dvc_varp->cfg->disc_enable;
                    ep_38C1600->bios_ctrl = adv_dvc_varp->bios_ctrl;
                    ep_38C1600->wdtr_able = adv_dvc_varp->wdtr_able;
                    ep_38C1600->tagqng_able = adv_dvc_varp->tagqng_able;
                    ep_38C1600->sdtr_speed1 = adv_dvc_varp->sdtr_speed1;
                    ep_38C1600->sdtr_speed2 = adv_dvc_varp->sdtr_speed2;
                    ep_38C1600->sdtr_speed3 = adv_dvc_varp->sdtr_speed3;
                    ep_38C1600->sdtr_speed4 = adv_dvc_varp->sdtr_speed4;
                    ep_38C1600->tagqng_able = adv_dvc_varp->tagqng_able;
                    ep_38C1600->start_motor = adv_dvc_varp->start_motor;
                    ep_38C1600->scsi_reset_delay =
                        adv_dvc_varp->scsi_reset_wait;
                    ep_38C1600->serial_number_word1 =
                        adv_dvc_varp->cfg->serial1;
                    ep_38C1600->serial_number_word2 =
                        adv_dvc_varp->cfg->serial2;
                    ep_38C1600->serial_number_word3 =
                        adv_dvc_varp->cfg->serial3;
                }

                /*
                 * Set the adapter's target id bit in the 'init_tidmask' field.
                 */
                boardp->init_tidmask |=
                    ADV_TID_TO_TIDMASK(adv_dvc_varp->chip_scsi_id);

                /*
                 * Finish initializing the 'Scsi_Host' structure.
                 */
                shp->irq = adv_dvc_varp->irq_no;
            }

            /*
             * Channels are numbered beginning with 0. For AdvanSys one host
             * structure supports one channel. Multi-channel boards have a
             * separate host structure for each channel.
             */
            shp->max_channel = 0;
            if (ASC_NARROW_BOARD(boardp)) {
                shp->max_id = ASC_MAX_TID + 1;
                shp->max_lun = ASC_MAX_LUN + 1;

                shp->io_port = asc_dvc_varp->iop_base;
                boardp->asc_n_io_port = ASC_IOADR_GAP;
                shp->this_id = asc_dvc_varp->cfg->chip_scsi_id;

                /* Set maximum number of queues the adapter can handle. */
                shp->can_queue = asc_dvc_varp->max_total_qng;
            } else {
                shp->max_id = ADV_MAX_TID + 1;
                shp->max_lun = ADV_MAX_LUN + 1;

                /*
                 * Save the I/O Port address and length even though
                 * I/O ports are not used to access Wide boards.
                 * Instead the Wide boards are accessed with
                 * PCI Memory Mapped I/O.
                 */
                shp->io_port = iop;
                boardp->asc_n_io_port = iolen;

                shp->this_id = adv_dvc_varp->chip_scsi_id;

                /* Set maximum number of queues the adapter can handle. */
                shp->can_queue = adv_dvc_varp->max_host_qng;
            }

            /*
             * 'n_io_port' currently is one byte.
             *
             * Set a value to 'n_io_port', but never referenced it because
             * it may be truncated.
             */
            shp->n_io_port = boardp->asc_n_io_port <= 255 ?
                boardp->asc_n_io_port : 255;

            /*
             * Following v1.3.89, 'cmd_per_lun' is no longer needed
             * and should be set to zero.
             *
             * But because of a bug introduced in v1.3.89 if the driver is
             * compiled as a module and 'cmd_per_lun' is zero, the Mid-Level
             * SCSI function 'allocate_device' will panic. To allow the driver
             * to work as a module in these kernels set 'cmd_per_lun' to 1.
	     *
	     * Note: This is wrong.  cmd_per_lun should be set to the depth
	     * you want on untagged devices always.
#ifdef MODULE
             */
            shp->cmd_per_lun = 1;
/* #else
            shp->cmd_per_lun = 0;
#endif */

            /*
             * Set the maximum number of scatter-gather elements the
             * adapter can handle.
             */
            if (ASC_NARROW_BOARD(boardp)) {
                /*
                 * Allow two commands with 'sg_tablesize' scatter-gather
                 * elements to be executed simultaneously. This value is
                 * the theoretical hardware limit. It may be decreased
                 * below.
                 */
                shp->sg_tablesize =
                    (((asc_dvc_varp->max_total_qng - 2) / 2) *
                    ASC_SG_LIST_PER_Q) + 1;
            } else {
                shp->sg_tablesize = ADV_MAX_SG_LIST;
            }

            /*
             * The value of 'sg_tablesize' can not exceed the SCSI
             * mid-level driver definition of SG_ALL. SG_ALL also
             * must not be exceeded, because it is used to define the
             * size of the scatter-gather table in 'struct asc_sg_head'.
             */
            if (shp->sg_tablesize > SG_ALL) {
                shp->sg_tablesize = SG_ALL;
            }

            ASC_DBG1(1, "advansys_detect: sg_tablesize: %d\n",
                shp->sg_tablesize);

            /* BIOS start address. */
            if (ASC_NARROW_BOARD(boardp)) {
                shp->base =
                        ((ulong) AscGetChipBiosAddress(
                            asc_dvc_varp->iop_base,
                            asc_dvc_varp->bus_type));
            } else {
                /*
                 * Fill-in BIOS board variables. The Wide BIOS saves
                 * information in LRAM that is used by the driver.
                 */
                AdvReadWordLram(adv_dvc_varp->iop_base, BIOS_SIGNATURE,
                    boardp->bios_signature);
                AdvReadWordLram(adv_dvc_varp->iop_base, BIOS_VERSION,
                    boardp->bios_version);
                AdvReadWordLram(adv_dvc_varp->iop_base, BIOS_CODESEG,
                    boardp->bios_codeseg);
                AdvReadWordLram(adv_dvc_varp->iop_base, BIOS_CODELEN,
                    boardp->bios_codelen);

                ASC_DBG2(1,
                    "advansys_detect: bios_signature 0x%x, bios_version 0x%x\n",
                    boardp->bios_signature, boardp->bios_version);

                ASC_DBG2(1,
                    "advansys_detect: bios_codeseg 0x%x, bios_codelen 0x%x\n",
                    boardp->bios_codeseg, boardp->bios_codelen);

                /*
                 * If the BIOS saved a valid signature, then fill in
                 * the BIOS code segment base address.
                 */
                if (boardp->bios_signature == 0x55AA) {
                    /*
                     * Convert x86 realmode code segment to a linear
                     * address by shifting left 4.
                     */
                    shp->base = ((ulong) boardp->bios_codeseg << 4);
                } else {
                    shp->base = 0;
                }
            }

            /*
             * Register Board Resources - I/O Port, DMA, IRQ
             */

            /*
             * Register I/O port range.
             *
             * For Wide boards the I/O ports are not used to access
             * the board, but request the region anyway.
             *
             * 'shp->n_io_port' is not referenced, because it may be truncated.
             */
            ASC_DBG2(2,
                "advansys_detect: request_region port 0x%lx, len 0x%x\n",
                (ulong) shp->io_port, boardp->asc_n_io_port);
            if (request_region(shp->io_port, boardp->asc_n_io_port,
                               "advansys") == NULL) {
                ASC_PRINT3(
"advansys_detect: board %d: request_region() failed, port 0x%lx, len 0x%x\n",
                    boardp->id, (ulong) shp->io_port, boardp->asc_n_io_port);
#ifdef CONFIG_PROC_FS
                kfree(boardp->prtbuf);
#endif /* CONFIG_PROC_FS */
                scsi_unregister(shp);
                asc_board_count--;
                continue;
            }

            /* Register DMA Channel for Narrow boards. */
            shp->dma_channel = NO_ISA_DMA; /* Default to no ISA DMA. */
#ifdef CONFIG_ISA
            if (ASC_NARROW_BOARD(boardp)) {
                /* Register DMA channel for ISA bus. */
                if (asc_dvc_varp->bus_type & ASC_IS_ISA) {
                    shp->dma_channel = asc_dvc_varp->cfg->isa_dma_channel;
                    if ((ret =
                         request_dma(shp->dma_channel, "advansys")) != 0) {
                        ASC_PRINT3(
"advansys_detect: board %d: request_dma() %d failed %d\n",
                            boardp->id, shp->dma_channel, ret);
                        release_region(shp->io_port, boardp->asc_n_io_port);
#ifdef CONFIG_PROC_FS
                        kfree(boardp->prtbuf);
#endif /* CONFIG_PROC_FS */
                        scsi_unregister(shp);
                        asc_board_count--;
                        continue;
                    }
                    AscEnableIsaDma(shp->dma_channel);
                }
            }
#endif /* CONFIG_ISA */

            /* Register IRQ Number. */
            ASC_DBG1(2, "advansys_detect: request_irq() %d\n", shp->irq);
           /*
            * If request_irq() fails with the IRQF_DISABLED flag set,
            * then try again without the IRQF_DISABLED flag set. This
            * allows IRQ sharing to work even with other drivers that
            * do not set the IRQF_DISABLED flag.
            *
            * If IRQF_DISABLED is not set, then interrupts are enabled
            * before the driver interrupt function is called.
            */
            if (((ret = request_irq(shp->irq, advansys_interrupt,
                            IRQF_DISABLED | (share_irq == TRUE ? IRQF_SHARED : 0),
                            "advansys", boardp)) != 0) &&
                ((ret = request_irq(shp->irq, advansys_interrupt,
                            (share_irq == TRUE ? IRQF_SHARED : 0),
                            "advansys", boardp)) != 0))
            {
                if (ret == -EBUSY) {
                    ASC_PRINT2(
"advansys_detect: board %d: request_irq(): IRQ 0x%x already in use.\n",
                        boardp->id, shp->irq);
                } else if (ret == -EINVAL) {
                    ASC_PRINT2(
"advansys_detect: board %d: request_irq(): IRQ 0x%x not valid.\n",
                        boardp->id, shp->irq);
                } else {
                    ASC_PRINT3(
"advansys_detect: board %d: request_irq(): IRQ 0x%x failed with %d\n",
                        boardp->id, shp->irq, ret);
                }
                release_region(shp->io_port, boardp->asc_n_io_port);
                iounmap(boardp->ioremap_addr);
                if (shp->dma_channel != NO_ISA_DMA) {
                    free_dma(shp->dma_channel);
                }
#ifdef CONFIG_PROC_FS
                kfree(boardp->prtbuf);
#endif /* CONFIG_PROC_FS */
                scsi_unregister(shp);
                asc_board_count--;
                continue;
            }

            /*
             * Initialize board RISC chip and enable interrupts.
             */
            if (ASC_NARROW_BOARD(boardp)) {
                ASC_DBG(2, "advansys_detect: AscInitAsc1000Driver()\n");
                warn_code = AscInitAsc1000Driver(asc_dvc_varp);
                err_code = asc_dvc_varp->err_code;

                if (warn_code || err_code) {
                    ASC_PRINT4(
"advansys_detect: board %d error: init_state 0x%x, warn 0x%x, error 0x%x\n",
                        boardp->id, asc_dvc_varp->init_state,
                        warn_code, err_code);
                }
            } else {
                ADV_CARR_T      *carrp;
                int             req_cnt = 0;
                adv_req_t       *reqp = NULL;
                int             sg_cnt = 0;

                /*
                 * Allocate buffer carrier structures. The total size
                 * is about 4 KB, so allocate all at once.
                 */
                carrp =
                    (ADV_CARR_T *) kmalloc(ADV_CARRIER_BUFSIZE, GFP_ATOMIC);
                ASC_DBG1(1, "advansys_detect: carrp 0x%lx\n", (ulong) carrp);

                if (carrp == NULL) {
                    goto kmalloc_error;
                }

                /*
                 * Allocate up to 'max_host_qng' request structures for
                 * the Wide board. The total size is about 16 KB, so
                 * allocate all at once. If the allocation fails decrement
                 * and try again.
                 */
                for (req_cnt = adv_dvc_varp->max_host_qng;
                    req_cnt > 0; req_cnt--) {

                    reqp = (adv_req_t *)
                        kmalloc(sizeof(adv_req_t) * req_cnt, GFP_ATOMIC);

                    ASC_DBG3(1,
                        "advansys_detect: reqp 0x%lx, req_cnt %d, bytes %lu\n",
                        (ulong) reqp, req_cnt,
                        (ulong) sizeof(adv_req_t) * req_cnt);

                    if (reqp != NULL) {
                        break;
                    }
                }
                if (reqp == NULL)
                {
                    goto kmalloc_error;
                }

                /*
                 * Allocate up to ADV_TOT_SG_BLOCK request structures for
                 * the Wide board. Each structure is about 136 bytes.
                 */
                boardp->adv_sgblkp = NULL;
                for (sg_cnt = 0; sg_cnt < ADV_TOT_SG_BLOCK; sg_cnt++) {

                    sgp = (adv_sgblk_t *)
                        kmalloc(sizeof(adv_sgblk_t), GFP_ATOMIC);

                    if (sgp == NULL) {
                        break;
                    }

                    sgp->next_sgblkp = boardp->adv_sgblkp;
                    boardp->adv_sgblkp = sgp;

                }
                ASC_DBG3(1,
                    "advansys_detect: sg_cnt %d * %u = %u bytes\n",
                    sg_cnt, sizeof(adv_sgblk_t),
                    (unsigned) (sizeof(adv_sgblk_t) * sg_cnt));

                /*
                 * If no request structures or scatter-gather structures could
                 * be allocated, then return an error. Otherwise continue with
                 * initialization.
                 */
    kmalloc_error:
                if (carrp == NULL)
                {
                    ASC_PRINT1(
"advansys_detect: board %d error: failed to kmalloc() carrier buffer.\n",
                        boardp->id);
                    err_code = ADV_ERROR;
                } else if (reqp == NULL) {
                    kfree(carrp);
                    ASC_PRINT1(
"advansys_detect: board %d error: failed to kmalloc() adv_req_t buffer.\n",
                        boardp->id);
                    err_code = ADV_ERROR;
                } else if (boardp->adv_sgblkp == NULL) {
                    kfree(carrp);
                    kfree(reqp);
                    ASC_PRINT1(
"advansys_detect: board %d error: failed to kmalloc() adv_sgblk_t buffers.\n",
                        boardp->id);
                    err_code = ADV_ERROR;
                } else {

                    /* Save carrier buffer pointer. */
                    boardp->orig_carrp = carrp;

                    /*
                     * Save original pointer for kfree() in case the
                     * driver is built as a module and can be unloaded.
                     */
                    boardp->orig_reqp = reqp;

                    adv_dvc_varp->carrier_buf = carrp;

                    /*
                     * Point 'adv_reqp' to the request structures and
                     * link them together.
                     */
                    req_cnt--;
                    reqp[req_cnt].next_reqp = NULL;
                    for (; req_cnt > 0; req_cnt--) {
                        reqp[req_cnt - 1].next_reqp = &reqp[req_cnt];
                    }
                    boardp->adv_reqp = &reqp[0];

                    if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
                    {
                        ASC_DBG(2,
                            "advansys_detect: AdvInitAsc3550Driver()\n");
                        warn_code = AdvInitAsc3550Driver(adv_dvc_varp);
                    } else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800) {
                        ASC_DBG(2,
                            "advansys_detect: AdvInitAsc38C0800Driver()\n");
                        warn_code = AdvInitAsc38C0800Driver(adv_dvc_varp);
                    } else {
                        ASC_DBG(2,
                            "advansys_detect: AdvInitAsc38C1600Driver()\n");
                        warn_code = AdvInitAsc38C1600Driver(adv_dvc_varp);
                    }
                    err_code = adv_dvc_varp->err_code;

                    if (warn_code || err_code) {
                        ASC_PRINT3(
"advansys_detect: board %d error: warn 0x%x, error 0x%x\n",
                            boardp->id, warn_code, err_code);
                    }
                }
            }

            if (err_code != 0) {
                release_region(shp->io_port, boardp->asc_n_io_port);
                if (ASC_WIDE_BOARD(boardp)) {
                    iounmap(boardp->ioremap_addr);
                    kfree(boardp->orig_carrp);
                    boardp->orig_carrp = NULL;
                    if (boardp->orig_reqp) {
                        kfree(boardp->orig_reqp);
                        boardp->orig_reqp = boardp->adv_reqp = NULL;
                    }
                    while ((sgp = boardp->adv_sgblkp) != NULL)
                    {
                        boardp->adv_sgblkp = sgp->next_sgblkp;
                        kfree(sgp);
                    }
                }
                if (shp->dma_channel != NO_ISA_DMA) {
                    free_dma(shp->dma_channel);
                }
#ifdef CONFIG_PROC_FS
                kfree(boardp->prtbuf);
#endif /* CONFIG_PROC_FS */
                free_irq(shp->irq, boardp);
                scsi_unregister(shp);
                asc_board_count--;
                continue;
            }
            ASC_DBG_PRT_SCSI_HOST(2, shp);
        }
    }

    ASC_DBG1(1, "advansys_detect: done: asc_board_count %d\n", asc_board_count);
    return asc_board_count;
}

/*
 * advansys_release()
 *
 * Release resources allocated for a single AdvanSys adapter.
 */
static int
advansys_release(struct Scsi_Host *shp)
{
    asc_board_t    *boardp;

    ASC_DBG(1, "advansys_release: begin\n");
    boardp = ASC_BOARDP(shp);
    free_irq(shp->irq, boardp);
    if (shp->dma_channel != NO_ISA_DMA) {
        ASC_DBG(1, "advansys_release: free_dma()\n");
        free_dma(shp->dma_channel);
    }
    release_region(shp->io_port, boardp->asc_n_io_port);
    if (ASC_WIDE_BOARD(boardp)) {
        adv_sgblk_t    *sgp = NULL;

        iounmap(boardp->ioremap_addr);
        kfree(boardp->orig_carrp);
        boardp->orig_carrp = NULL;
        if (boardp->orig_reqp) {
            kfree(boardp->orig_reqp);
            boardp->orig_reqp = boardp->adv_reqp = NULL;
        }
        while ((sgp = boardp->adv_sgblkp) != NULL)
        {
            boardp->adv_sgblkp = sgp->next_sgblkp;
            kfree(sgp);
        }
    }
#ifdef CONFIG_PROC_FS
    ASC_ASSERT(boardp->prtbuf != NULL);
    kfree(boardp->prtbuf);
#endif /* CONFIG_PROC_FS */
    scsi_unregister(shp);
    ASC_DBG(1, "advansys_release: end\n");
    return 0;
}

/*
 * advansys_info()
 *
 * Return suitable for printing on the console with the argument
 * adapter's configuration information.
 *
 * Note: The information line should not exceed ASC_INFO_SIZE bytes,
 * otherwise the static 'info' array will be overrun.
 */
static const char *
advansys_info(struct Scsi_Host *shp)
{
    static char     info[ASC_INFO_SIZE];
    asc_board_t     *boardp;
    ASC_DVC_VAR     *asc_dvc_varp;
    ADV_DVC_VAR     *adv_dvc_varp;
    char            *busname;
    int             iolen;
    char            *widename = NULL;

    boardp = ASC_BOARDP(shp);
    if (ASC_NARROW_BOARD(boardp)) {
        asc_dvc_varp = &boardp->dvc_var.asc_dvc_var;
        ASC_DBG(1, "advansys_info: begin\n");
        if (asc_dvc_varp->bus_type & ASC_IS_ISA) {
            if ((asc_dvc_varp->bus_type & ASC_IS_ISAPNP) == ASC_IS_ISAPNP) {
                busname = "ISA PnP";
            } else {
                busname = "ISA";
            }
            /* Don't reference 'shp->n_io_port'; It may be truncated. */
            sprintf(info,
"AdvanSys SCSI %s: %s: IO 0x%lX-0x%lX, IRQ 0x%X, DMA 0x%X",
                ASC_VERSION, busname,
                (ulong) shp->io_port,
                (ulong) shp->io_port + boardp->asc_n_io_port - 1,
                shp->irq, shp->dma_channel);
        } else {
            if (asc_dvc_varp->bus_type & ASC_IS_VL) {
                busname = "VL";
            } else if (asc_dvc_varp->bus_type & ASC_IS_EISA) {
                busname = "EISA";
            } else if (asc_dvc_varp->bus_type & ASC_IS_PCI) {
                if ((asc_dvc_varp->bus_type & ASC_IS_PCI_ULTRA)
                    == ASC_IS_PCI_ULTRA) {
                    busname = "PCI Ultra";
                } else {
                    busname = "PCI";
                }
            } else {
                busname = "?";
                ASC_PRINT2( "advansys_info: board %d: unknown bus type %d\n",
                    boardp->id, asc_dvc_varp->bus_type);
            }
            /* Don't reference 'shp->n_io_port'; It may be truncated. */
            sprintf(info,
                "AdvanSys SCSI %s: %s: IO 0x%lX-0x%lX, IRQ 0x%X",
                ASC_VERSION, busname,
                (ulong) shp->io_port,
                (ulong) shp->io_port + boardp->asc_n_io_port - 1,
                shp->irq);
        }
    } else {
        /*
         * Wide Adapter Information
         *
         * Memory-mapped I/O is used instead of I/O space to access
         * the adapter, but display the I/O Port range. The Memory
         * I/O address is displayed through the driver /proc file.
         */
        adv_dvc_varp = &boardp->dvc_var.adv_dvc_var;
        if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
        {
            iolen = ADV_3550_IOLEN;
            widename = "Ultra-Wide";
        } else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800)
        {
            iolen = ADV_38C0800_IOLEN;
            widename = "Ultra2-Wide";
        } else
        {
            iolen = ADV_38C1600_IOLEN;
            widename = "Ultra3-Wide";
        }
        sprintf(info, "AdvanSys SCSI %s: PCI %s: PCIMEM 0x%lX-0x%lX, IRQ 0x%X",
            ASC_VERSION,
            widename,
            (ulong) adv_dvc_varp->iop_base,
            (ulong) adv_dvc_varp->iop_base + iolen - 1,
            shp->irq);
    }
    ASC_ASSERT(strlen(info) < ASC_INFO_SIZE);
    ASC_DBG(1, "advansys_info: end\n");
    return info;
}

/*
 * advansys_queuecommand() - interrupt-driven I/O entrypoint.
 *
 * This function always returns 0. Command return status is saved
 * in the 'scp' result field.
 */
static int
advansys_queuecommand(struct scsi_cmnd *scp, void (*done)(struct scsi_cmnd *))
{
    struct Scsi_Host    *shp;
    asc_board_t         *boardp;
    ulong               flags;
    struct scsi_cmnd           *done_scp;

    shp = scp->device->host;
    boardp = ASC_BOARDP(shp);
    ASC_STATS(shp, queuecommand);

    /* host_lock taken by mid-level prior to call but need to protect */
    /* against own ISR */
    spin_lock_irqsave(&boardp->lock, flags);

    /*
     * Block new commands while handling a reset or abort request.
     */
    if (boardp->flags & ASC_HOST_IN_RESET) {
        ASC_DBG1(1,
            "advansys_queuecommand: scp 0x%lx blocked for reset request\n",
            (ulong) scp);
        scp->result = HOST_BYTE(DID_RESET);

        /*
         * Add blocked requests to the board's 'done' queue. The queued
         * requests will be completed at the end of the abort or reset
         * handling.
         */
        asc_enqueue(&boardp->done, scp, ASC_BACK);
	spin_unlock_irqrestore(&boardp->lock, flags);
        return 0;
    }

    /*
     * Attempt to execute any waiting commands for the board.
     */
    if (!ASC_QUEUE_EMPTY(&boardp->waiting)) {
        ASC_DBG(1,
            "advansys_queuecommand: before asc_execute_queue() waiting\n");
        asc_execute_queue(&boardp->waiting);
    }

    /*
     * Save the function pointer to Linux mid-level 'done' function
     * and attempt to execute the command.
     *
     * If ASC_NOERROR is returned the request has been added to the
     * board's 'active' queue and will be completed by the interrupt
     * handler.
     *
     * If ASC_BUSY is returned add the request to the board's per
     * target waiting list. This is the first time the request has
     * been tried. Add it to the back of the waiting list. It will be
     * retried later.
     *
     * If an error occurred, the request will have been placed on the
     * board's 'done' queue and must be completed before returning.
     */
    scp->scsi_done = done;
    switch (asc_execute_scsi_cmnd(scp)) {
    case ASC_NOERROR:
        break;
    case ASC_BUSY:
        asc_enqueue(&boardp->waiting, scp, ASC_BACK);
        break;
    case ASC_ERROR:
    default:
        done_scp = asc_dequeue_list(&boardp->done, NULL, ASC_TID_ALL);
        /* Interrupts could be enabled here. */
        asc_scsi_done_list(done_scp);
        break;
    }
    spin_unlock_irqrestore(&boardp->lock, flags);

    return 0;
}

/*
 * advansys_reset()
 *
 * Reset the bus associated with the command 'scp'.
 *
 * This function runs its own thread. Interrupts must be blocked but
 * sleeping is allowed and no locking other than for host structures is
 * required. Returns SUCCESS or FAILED.
 */
static int
advansys_reset(struct scsi_cmnd *scp)
{
    struct Scsi_Host     *shp;
    asc_board_t          *boardp;
    ASC_DVC_VAR          *asc_dvc_varp;
    ADV_DVC_VAR          *adv_dvc_varp;
    ulong                flags;
    struct scsi_cmnd            *done_scp = NULL, *last_scp = NULL;
    struct scsi_cmnd            *tscp, *new_last_scp;
    int                  status;
    int                  ret = SUCCESS;

    ASC_DBG1(1, "advansys_reset: 0x%lx\n", (ulong) scp);

#ifdef ADVANSYS_STATS
    if (scp->device->host != NULL) {
        ASC_STATS(scp->device->host, reset);
    }
#endif /* ADVANSYS_STATS */

    if ((shp = scp->device->host) == NULL) {
        scp->result = HOST_BYTE(DID_ERROR);
        return FAILED;
    }

    boardp = ASC_BOARDP(shp);

    ASC_PRINT1("advansys_reset: board %d: SCSI bus reset started...\n",
        boardp->id);
    /*
     * Check for re-entrancy.
     */
    spin_lock_irqsave(&boardp->lock, flags);
    if (boardp->flags & ASC_HOST_IN_RESET) {
	spin_unlock_irqrestore(&boardp->lock, flags);
        return FAILED;
    }
    boardp->flags |= ASC_HOST_IN_RESET;
    spin_unlock_irqrestore(&boardp->lock, flags);

    if (ASC_NARROW_BOARD(boardp)) {
        /*
         * Narrow Board
         */
        asc_dvc_varp = &boardp->dvc_var.asc_dvc_var;

        /*
         * Reset the chip and SCSI bus.
         */
        ASC_DBG(1, "advansys_reset: before AscInitAsc1000Driver()\n");
        status = AscInitAsc1000Driver(asc_dvc_varp);

        /* Refer to ASC_IERR_* defintions for meaning of 'err_code'. */
        if (asc_dvc_varp->err_code) {
            ASC_PRINT2(
                "advansys_reset: board %d: SCSI bus reset error: 0x%x\n",
                boardp->id, asc_dvc_varp->err_code);
            ret = FAILED;
        } else if (status) {
            ASC_PRINT2(
                "advansys_reset: board %d: SCSI bus reset warning: 0x%x\n",
                boardp->id, status);
        } else {
            ASC_PRINT1(
                "advansys_reset: board %d: SCSI bus reset successful.\n",
                boardp->id);
        }

        ASC_DBG(1, "advansys_reset: after AscInitAsc1000Driver()\n");
	spin_lock_irqsave(&boardp->lock, flags);

    } else {
        /*
         * Wide Board
         *
         * If the suggest reset bus flags are set, then reset the bus.
         * Otherwise only reset the device.
         */
        adv_dvc_varp = &boardp->dvc_var.adv_dvc_var;

        /*
         * Reset the target's SCSI bus.
         */
        ASC_DBG(1, "advansys_reset: before AdvResetChipAndSB()\n");
        switch (AdvResetChipAndSB(adv_dvc_varp)) {
        case ASC_TRUE:
            ASC_PRINT1("advansys_reset: board %d: SCSI bus reset successful.\n",
                boardp->id);
            break;
        case ASC_FALSE:
        default:
            ASC_PRINT1("advansys_reset: board %d: SCSI bus reset error.\n",
                boardp->id);
            ret = FAILED;
            break;
        }
	spin_lock_irqsave(&boardp->lock, flags);
        (void) AdvISR(adv_dvc_varp);
    }
    /* Board lock is held. */

    /*
     * Dequeue all board 'done' requests. A pointer to the last request
     * is returned in 'last_scp'.
     */
    done_scp = asc_dequeue_list(&boardp->done, &last_scp, ASC_TID_ALL);

    /*
     * Dequeue all board 'active' requests for all devices and set
     * the request status to DID_RESET. A pointer to the last request
     * is returned in 'last_scp'.
     */
    if (done_scp == NULL) {
        done_scp = asc_dequeue_list(&boardp->active, &last_scp, ASC_TID_ALL);
        for (tscp = done_scp; tscp; tscp = REQPNEXT(tscp)) {
            tscp->result = HOST_BYTE(DID_RESET);
        }
    } else {
        /* Append to 'done_scp' at the end with 'last_scp'. */
        ASC_ASSERT(last_scp != NULL);
        last_scp->host_scribble = (unsigned char *)asc_dequeue_list(
			&boardp->active, &new_last_scp, ASC_TID_ALL);
        if (new_last_scp != NULL) {
            ASC_ASSERT(REQPNEXT(last_scp) != NULL);
            for (tscp = REQPNEXT(last_scp); tscp; tscp = REQPNEXT(tscp)) {
                tscp->result = HOST_BYTE(DID_RESET);
            }
            last_scp = new_last_scp;
        }
    }

    /*
     * Dequeue all 'waiting' requests and set the request status
     * to DID_RESET.
     */
    if (done_scp == NULL) {
        done_scp = asc_dequeue_list(&boardp->waiting, &last_scp, ASC_TID_ALL);
        for (tscp = done_scp; tscp; tscp = REQPNEXT(tscp)) {
            tscp->result = HOST_BYTE(DID_RESET);
        }
    } else {
        /* Append to 'done_scp' at the end with 'last_scp'. */
        ASC_ASSERT(last_scp != NULL);
        last_scp->host_scribble = (unsigned char *)asc_dequeue_list(
			&boardp->waiting, &new_last_scp, ASC_TID_ALL);
        if (new_last_scp != NULL) {
            ASC_ASSERT(REQPNEXT(last_scp) != NULL);
            for (tscp = REQPNEXT(last_scp); tscp; tscp = REQPNEXT(tscp)) {
                tscp->result = HOST_BYTE(DID_RESET);
            }
            last_scp = new_last_scp;
        }
    }

    /* Save the time of the most recently completed reset. */
    boardp->last_reset = jiffies;

    /* Clear reset flag. */
    boardp->flags &= ~ASC_HOST_IN_RESET;
    spin_unlock_irqrestore(&boardp->lock, flags);

    /*
     * Complete all the 'done_scp' requests.
     */
    if (done_scp != NULL) {
        asc_scsi_done_list(done_scp);
    }

    ASC_DBG1(1, "advansys_reset: ret %d\n", ret);

    return ret;
}

/*
 * advansys_biosparam()
 *
 * Translate disk drive geometry if the "BIOS greater than 1 GB"
 * support is enabled for a drive.
 *
 * ip (information pointer) is an int array with the following definition:
 * ip[0]: heads
 * ip[1]: sectors
 * ip[2]: cylinders
 */
static int
advansys_biosparam(struct scsi_device *sdev, struct block_device *bdev,
		sector_t capacity, int ip[])
{
    asc_board_t     *boardp;

    ASC_DBG(1, "advansys_biosparam: begin\n");
    ASC_STATS(sdev->host, biosparam);
    boardp = ASC_BOARDP(sdev->host);
    if (ASC_NARROW_BOARD(boardp)) {
        if ((boardp->dvc_var.asc_dvc_var.dvc_cntl &
             ASC_CNTL_BIOS_GT_1GB) && capacity > 0x200000) {
                ip[0] = 255;
                ip[1] = 63;
        } else {
                ip[0] = 64;
                ip[1] = 32;
        }
    } else {
        if ((boardp->dvc_var.adv_dvc_var.bios_ctrl &
             BIOS_CTRL_EXTENDED_XLAT) && capacity > 0x200000) {
                ip[0] = 255;
                ip[1] = 63;
        } else {
                ip[0] = 64;
                ip[1] = 32;
        }
    }
    ip[2] = (unsigned long)capacity / (ip[0] * ip[1]);
    ASC_DBG(1, "advansys_biosparam: end\n");
    return 0;
}

/*
 * --- Loadable Driver Support
 */

static struct scsi_host_template driver_template = {
    .proc_name                  = "advansys",
#ifdef CONFIG_PROC_FS
    .proc_info                  = advansys_proc_info,
#endif
    .name                       = "advansys",
    .detect                     = advansys_detect, 
    .release                    = advansys_release,
    .info                       = advansys_info,
    .queuecommand               = advansys_queuecommand,
    .eh_bus_reset_handler	= advansys_reset,
    .bios_param                 = advansys_biosparam,
    .slave_configure		= advansys_slave_configure,
    /*
     * Because the driver may control an ISA adapter 'unchecked_isa_dma'
     * must be set. The flag will be cleared in advansys_detect for non-ISA
     * adapters. Refer to the comment in scsi_module.c for more information.
     */
    .unchecked_isa_dma          = 1,
    /*
     * All adapters controlled by this driver are capable of large
     * scatter-gather lists. According to the mid-level SCSI documentation
     * this obviates any performance gain provided by setting
     * 'use_clustering'. But empirically while CPU utilization is increased
     * by enabling clustering, I/O throughput increases as well.
     */
    .use_clustering             = ENABLE_CLUSTERING,
};
#include "scsi_module.c"


/*
 * --- Miscellaneous Driver Functions
 */

/*
 * First-level interrupt handler.
 *
 * 'dev_id' is a pointer to the interrupting adapter's asc_board_t. Because
 * all boards are currently checked for interrupts on each interrupt, 'dev_id'
 * is not referenced. 'dev_id' could be used to identify an interrupt passed
 * to the AdvanSys driver which is for a device sharing an interrupt with
 * an AdvanSys adapter.
 */
STATIC irqreturn_t
advansys_interrupt(int irq, void *dev_id)
{
    ulong           flags;
    int             i;
    asc_board_t     *boardp;
    struct scsi_cmnd       *done_scp = NULL, *last_scp = NULL;
    struct scsi_cmnd       *new_last_scp;
    struct Scsi_Host *shp;

    ASC_DBG(1, "advansys_interrupt: begin\n");

    /*
     * Check for interrupts on all boards.
     * AscISR() will call asc_isr_callback().
     */
    for (i = 0; i < asc_board_count; i++) {
	shp = asc_host[i];
        boardp = ASC_BOARDP(shp);
        ASC_DBG2(2, "advansys_interrupt: i %d, boardp 0x%lx\n",
            i, (ulong) boardp);
        spin_lock_irqsave(&boardp->lock, flags);
        if (ASC_NARROW_BOARD(boardp)) {
            /*
             * Narrow Board
             */
            if (AscIsIntPending(shp->io_port)) {
                ASC_STATS(shp, interrupt);
                ASC_DBG(1, "advansys_interrupt: before AscISR()\n");
                AscISR(&boardp->dvc_var.asc_dvc_var);
            }
        } else {
            /*
             * Wide Board
             */
            ASC_DBG(1, "advansys_interrupt: before AdvISR()\n");
            if (AdvISR(&boardp->dvc_var.adv_dvc_var)) {
                ASC_STATS(shp, interrupt);
            }
        }

        /*
         * Start waiting requests and create a list of completed requests.
         *
         * If a reset request is being performed for the board, the reset
         * handler will complete pending requests after it has completed.
         */
        if ((boardp->flags & ASC_HOST_IN_RESET) == 0) {
            ASC_DBG2(1, "advansys_interrupt: done_scp 0x%lx, last_scp 0x%lx\n",
                (ulong) done_scp, (ulong) last_scp);

            /* Start any waiting commands for the board. */
            if (!ASC_QUEUE_EMPTY(&boardp->waiting)) {
                ASC_DBG(1, "advansys_interrupt: before asc_execute_queue()\n");
                asc_execute_queue(&boardp->waiting);
            }

             /*
              * Add to the list of requests that must be completed.
              *
              * 'done_scp' will always be NULL on the first iteration
              * of this loop. 'last_scp' is set at the same time as
              * 'done_scp'.
              */
            if (done_scp == NULL) {
                done_scp = asc_dequeue_list(&boardp->done, &last_scp,
                    ASC_TID_ALL);
            } else {
                ASC_ASSERT(last_scp != NULL);
                last_scp->host_scribble = (unsigned char *)asc_dequeue_list(
			&boardp->done, &new_last_scp, ASC_TID_ALL);
                if (new_last_scp != NULL) {
                    ASC_ASSERT(REQPNEXT(last_scp) != NULL);
                    last_scp = new_last_scp;
                }
            }
        }
        spin_unlock_irqrestore(&boardp->lock, flags);
    }

    /*
     * If interrupts were enabled on entry, then they
     * are now enabled here.
     *
     * Complete all requests on the done list.
     */

    asc_scsi_done_list(done_scp);

    ASC_DBG(1, "advansys_interrupt: end\n");
    return IRQ_HANDLED;
}

/*
 * Set the number of commands to queue per device for the
 * specified host adapter.
 */
STATIC int
advansys_slave_configure(struct scsi_device *device)
{
    asc_board_t        *boardp;

    boardp = ASC_BOARDP(device->host);
    boardp->flags |= ASC_SELECT_QUEUE_DEPTHS;
    /*
     * Save a pointer to the device and set its initial/maximum
     * queue depth.  Only save the pointer for a lun0 dev though.
     */
    if(device->lun == 0)
        boardp->device[device->id] = device;
    if(device->tagged_supported) {
        if (ASC_NARROW_BOARD(boardp)) {
	    scsi_adjust_queue_depth(device, MSG_ORDERED_TAG,
                boardp->dvc_var.asc_dvc_var.max_dvc_qng[device->id]);
        } else {
	    scsi_adjust_queue_depth(device, MSG_ORDERED_TAG,
                boardp->dvc_var.adv_dvc_var.max_dvc_qng);
        }
    } else {
	scsi_adjust_queue_depth(device, 0, device->host->cmd_per_lun);
    }
    ASC_DBG4(1, "advansys_slave_configure: device 0x%lx, boardp 0x%lx, id %d, depth %d\n",
            (ulong) device, (ulong) boardp, device->id, device->queue_depth);
    return 0;
}

/*
 * Complete all requests on the singly linked list pointed
 * to by 'scp'.
 *
 * Interrupts can be enabled on entry.
 */
STATIC void
asc_scsi_done_list(struct scsi_cmnd *scp)
{
    struct scsi_cmnd    *tscp;

    ASC_DBG(2, "asc_scsi_done_list: begin\n");
    while (scp != NULL) {
	asc_board_t *boardp;
	struct device *dev;

        ASC_DBG1(3, "asc_scsi_done_list: scp 0x%lx\n", (ulong) scp);
        tscp = REQPNEXT(scp);
        scp->host_scribble = NULL;

	boardp = ASC_BOARDP(scp->device->host);

	if (ASC_NARROW_BOARD(boardp))
	    dev = boardp->dvc_cfg.asc_dvc_cfg.dev;
	else
	    dev = boardp->dvc_cfg.adv_dvc_cfg.dev;

	if (scp->use_sg)
	    dma_unmap_sg(dev, (struct scatterlist *)scp->request_buffer,
			 scp->use_sg, scp->sc_data_direction);
	else if (scp->request_bufflen)
	    dma_unmap_single(dev, scp->SCp.dma_handle,
			     scp->request_bufflen, scp->sc_data_direction);

        ASC_STATS(scp->device->host, done);
        ASC_ASSERT(scp->scsi_done != NULL);

        scp->scsi_done(scp);

        scp = tscp;
    }
    ASC_DBG(2, "asc_scsi_done_list: done\n");
    return;
}

/*
 * Execute a single 'Scsi_Cmnd'.
 *
 * The function 'done' is called when the request has been completed.
 *
 * Scsi_Cmnd:
 *
 *  host - board controlling device
 *  device - device to send command
 *  target - target of device
 *  lun - lun of device
 *  cmd_len - length of SCSI CDB
 *  cmnd - buffer for SCSI 8, 10, or 12 byte CDB
 *  use_sg - if non-zero indicates scatter-gather request with use_sg elements
 *
 *  if (use_sg == 0) {
 *    request_buffer - buffer address for request
 *    request_bufflen - length of request buffer
 *  } else {
 *    request_buffer - pointer to scatterlist structure
 *  }
 *
 *  sense_buffer - sense command buffer
 *
 *  result (4 bytes of an int):
 *    Byte Meaning
 *    0 SCSI Status Byte Code
 *    1 SCSI One Byte Message Code
 *    2 Host Error Code
 *    3 Mid-Level Error Code
 *
 *  host driver fields:
 *    SCp - Scsi_Pointer used for command processing status
 *    scsi_done - used to save caller's done function
 *    host_scribble - used for pointer to another struct scsi_cmnd
 *
 * If this function returns ASC_NOERROR the request has been enqueued
 * on the board's 'active' queue and will be completed from the
 * interrupt handler.
 *
 * If this function returns ASC_NOERROR the request has been enqueued
 * on the board's 'done' queue and must be completed by the caller.
 *
 * If ASC_BUSY is returned the request will be enqueued by the
 * caller on the target's waiting queue and re-tried later.
 */
STATIC int
asc_execute_scsi_cmnd(struct scsi_cmnd *scp)
{
    asc_board_t        *boardp;
    ASC_DVC_VAR        *asc_dvc_varp;
    ADV_DVC_VAR        *adv_dvc_varp;
    ADV_SCSI_REQ_Q     *adv_scsiqp;
    struct scsi_device *device;
    int                ret;

    ASC_DBG2(1, "asc_execute_scsi_cmnd: scp 0x%lx, done 0x%lx\n",
        (ulong) scp, (ulong) scp->scsi_done);

    boardp = ASC_BOARDP(scp->device->host);
    device = boardp->device[scp->device->id];

    if (ASC_NARROW_BOARD(boardp)) {
        /*
         * Build and execute Narrow Board request.
         */

        asc_dvc_varp = &boardp->dvc_var.asc_dvc_var;

        /*
         * Build Asc Library request structure using the
         * global structures 'asc_scsi_req' and 'asc_sg_head'.
         *
         * If an error is returned, then the request has been
         * queued on the board done queue. It will be completed
         * by the caller.
         *
         * asc_build_req() can not return ASC_BUSY.
         */
        if (asc_build_req(boardp, scp) == ASC_ERROR) {
            ASC_STATS(scp->device->host, build_error);
            return ASC_ERROR;
        }

        /*
         * Execute the command. If there is no error, add the command
         * to the active queue.
         */
        switch (ret = AscExeScsiQueue(asc_dvc_varp, &asc_scsi_q)) {
        case ASC_NOERROR:
            ASC_STATS(scp->device->host, exe_noerror);
            /*
             * Increment monotonically increasing per device successful
             * request counter. Wrapping doesn't matter.
             */
            boardp->reqcnt[scp->device->id]++;
            asc_enqueue(&boardp->active, scp, ASC_BACK);
            ASC_DBG(1,
                "asc_execute_scsi_cmnd: AscExeScsiQueue(), ASC_NOERROR\n");
            break;
        case ASC_BUSY:
            /*
             * Caller will enqueue request on the target's waiting queue
             * and retry later.
             */
            ASC_STATS(scp->device->host, exe_busy);
            break;
        case ASC_ERROR:
            ASC_PRINT2(
"asc_execute_scsi_cmnd: board %d: AscExeScsiQueue() ASC_ERROR, err_code 0x%x\n",
                boardp->id, asc_dvc_varp->err_code);
            ASC_STATS(scp->device->host, exe_error);
            scp->result = HOST_BYTE(DID_ERROR);
            asc_enqueue(&boardp->done, scp, ASC_BACK);
            break;
        default:
            ASC_PRINT2(
"asc_execute_scsi_cmnd: board %d: AscExeScsiQueue() unknown, err_code 0x%x\n",
                boardp->id, asc_dvc_varp->err_code);
            ASC_STATS(scp->device->host, exe_unknown);
            scp->result = HOST_BYTE(DID_ERROR);
            asc_enqueue(&boardp->done, scp, ASC_BACK);
            break;
        }
    } else {
        /*
         * Build and execute Wide Board request.
         */
        adv_dvc_varp = &boardp->dvc_var.adv_dvc_var;

        /*
         * Build and get a pointer to an Adv Library request structure.
         *
         * If the request is successfully built then send it below,
         * otherwise return with an error.
         */
        switch (adv_build_req(boardp, scp, &adv_scsiqp)) {
        case ASC_NOERROR:
            ASC_DBG(3, "asc_execute_scsi_cmnd: adv_build_req ASC_NOERROR\n");
            break;
        case ASC_BUSY:
            ASC_DBG(1, "asc_execute_scsi_cmnd: adv_build_req ASC_BUSY\n");
            /*
             * If busy is returned the request has not been enqueued.
             * It will be enqueued by the caller on the target's waiting
             * queue and retried later.
             *
             * The asc_stats fields 'adv_build_noreq' and 'adv_build_nosg'
             * count wide board busy conditions. They are updated in
             * adv_build_req and adv_get_sglist, respectively.
             */
            return ASC_BUSY;
        case ASC_ERROR:
             /* 
              * If an error is returned, then the request has been
              * queued on the board done queue. It will be completed
              * by the caller.
              */
        default:
            ASC_DBG(1, "asc_execute_scsi_cmnd: adv_build_req ASC_ERROR\n");
            ASC_STATS(scp->device->host, build_error);
            return ASC_ERROR;
        }

        /*
         * Execute the command. If there is no error, add the command
         * to the active queue.
         */
        switch (ret = AdvExeScsiQueue(adv_dvc_varp, adv_scsiqp)) {
        case ASC_NOERROR:
            ASC_STATS(scp->device->host, exe_noerror);
            /*
             * Increment monotonically increasing per device successful
             * request counter. Wrapping doesn't matter.
             */
            boardp->reqcnt[scp->device->id]++;
            asc_enqueue(&boardp->active, scp, ASC_BACK);
            ASC_DBG(1,
                "asc_execute_scsi_cmnd: AdvExeScsiQueue(), ASC_NOERROR\n");
            break;
        case ASC_BUSY:
            /*
             * Caller will enqueue request on the target's waiting queue
             * and retry later.
             */
            ASC_STATS(scp->device->host, exe_busy);
            break;
        case ASC_ERROR:
            ASC_PRINT2(
"asc_execute_scsi_cmnd: board %d: AdvExeScsiQueue() ASC_ERROR, err_code 0x%x\n",
                boardp->id, adv_dvc_varp->err_code);
            ASC_STATS(scp->device->host, exe_error);
            scp->result = HOST_BYTE(DID_ERROR);
            asc_enqueue(&boardp->done, scp, ASC_BACK);
            break;
        default:
            ASC_PRINT2(
"asc_execute_scsi_cmnd: board %d: AdvExeScsiQueue() unknown, err_code 0x%x\n",
                boardp->id, adv_dvc_varp->err_code);
            ASC_STATS(scp->device->host, exe_unknown);
            scp->result = HOST_BYTE(DID_ERROR);
            asc_enqueue(&boardp->done, scp, ASC_BACK);
            break;
        }
    }

    ASC_DBG(1, "asc_execute_scsi_cmnd: end\n");
    return ret;
}

/*
 * Build a request structure for the Asc Library (Narrow Board).
 *
 * The global structures 'asc_scsi_q' and 'asc_sg_head' are
 * used to build the request.
 *
 * If an error occurs, then queue the request on the board done
 * queue and return ASC_ERROR.
 */
STATIC int
asc_build_req(asc_board_t *boardp, struct scsi_cmnd *scp)
{
    struct device *dev = boardp->dvc_cfg.asc_dvc_cfg.dev;

    /*
     * Mutually exclusive access is required to 'asc_scsi_q' and
     * 'asc_sg_head' until after the request is started.
     */
    memset(&asc_scsi_q, 0, sizeof(ASC_SCSI_Q));

    /*
     * Point the ASC_SCSI_Q to the 'struct scsi_cmnd'.
     */
    asc_scsi_q.q2.srb_ptr = ASC_VADDR_TO_U32(scp);

    /*
     * Build the ASC_SCSI_Q request.
     *
     * For narrow boards a CDB length maximum of 12 bytes
     * is supported.
     */
    if (scp->cmd_len > ASC_MAX_CDB_LEN) {
        ASC_PRINT3(
"asc_build_req: board %d: cmd_len %d > ASC_MAX_CDB_LEN  %d\n",
            boardp->id, scp->cmd_len, ASC_MAX_CDB_LEN);
        scp->result = HOST_BYTE(DID_ERROR);
        asc_enqueue(&boardp->done, scp, ASC_BACK);
        return ASC_ERROR;
    }
    asc_scsi_q.cdbptr = &scp->cmnd[0];
    asc_scsi_q.q2.cdb_len = scp->cmd_len;
    asc_scsi_q.q1.target_id = ASC_TID_TO_TARGET_ID(scp->device->id);
    asc_scsi_q.q1.target_lun = scp->device->lun;
    asc_scsi_q.q2.target_ix = ASC_TIDLUN_TO_IX(scp->device->id, scp->device->lun);
    asc_scsi_q.q1.sense_addr = cpu_to_le32(virt_to_bus(&scp->sense_buffer[0]));
    asc_scsi_q.q1.sense_len = sizeof(scp->sense_buffer);

    /*
     * If there are any outstanding requests for the current target,
     * then every 255th request send an ORDERED request. This heuristic
     * tries to retain the benefit of request sorting while preventing
     * request starvation. 255 is the max number of tags or pending commands
     * a device may have outstanding.
     *
     * The request count is incremented below for every successfully
     * started request.
     *
     */
    if ((boardp->dvc_var.asc_dvc_var.cur_dvc_qng[scp->device->id] > 0) &&
        (boardp->reqcnt[scp->device->id] % 255) == 0) {
        asc_scsi_q.q2.tag_code = MSG_ORDERED_TAG;
    } else {
        asc_scsi_q.q2.tag_code = MSG_SIMPLE_TAG;
    }

    /*
     * Build ASC_SCSI_Q for a contiguous buffer or a scatter-gather
     * buffer command.
     */
    if (scp->use_sg == 0) {
        /*
         * CDB request of single contiguous buffer.
         */
        ASC_STATS(scp->device->host, cont_cnt);
	scp->SCp.dma_handle = scp->request_bufflen ?
	    dma_map_single(dev, scp->request_buffer,
			   scp->request_bufflen, scp->sc_data_direction) : 0;
	asc_scsi_q.q1.data_addr = cpu_to_le32(scp->SCp.dma_handle);
        asc_scsi_q.q1.data_cnt = cpu_to_le32(scp->request_bufflen);
        ASC_STATS_ADD(scp->device->host, cont_xfer,
                      ASC_CEILING(scp->request_bufflen, 512));
        asc_scsi_q.q1.sg_queue_cnt = 0;
        asc_scsi_q.sg_head = NULL;
    } else {
        /*
         * CDB scatter-gather request list.
         */
        int                     sgcnt;
	int			use_sg;
        struct scatterlist      *slp;

	slp = (struct scatterlist *)scp->request_buffer;
	use_sg = dma_map_sg(dev, slp, scp->use_sg, scp->sc_data_direction);

	if (use_sg > scp->device->host->sg_tablesize) {
            ASC_PRINT3(
"asc_build_req: board %d: use_sg %d > sg_tablesize %d\n",
		boardp->id, use_sg, scp->device->host->sg_tablesize);
	    dma_unmap_sg(dev, slp, scp->use_sg, scp->sc_data_direction);
            scp->result = HOST_BYTE(DID_ERROR);
            asc_enqueue(&boardp->done, scp, ASC_BACK);
            return ASC_ERROR;
        }

        ASC_STATS(scp->device->host, sg_cnt);

        /*
         * Use global ASC_SG_HEAD structure and set the ASC_SCSI_Q
         * structure to point to it.
         */
        memset(&asc_sg_head, 0, sizeof(ASC_SG_HEAD));

        asc_scsi_q.q1.cntl |= QC_SG_HEAD;
        asc_scsi_q.sg_head = &asc_sg_head;
        asc_scsi_q.q1.data_cnt = 0;
        asc_scsi_q.q1.data_addr = 0;
        /* This is a byte value, otherwise it would need to be swapped. */
	asc_sg_head.entry_cnt = asc_scsi_q.q1.sg_queue_cnt = use_sg;
        ASC_STATS_ADD(scp->device->host, sg_elem, asc_sg_head.entry_cnt);

        /*
         * Convert scatter-gather list into ASC_SG_HEAD list.
         */
	for (sgcnt = 0; sgcnt < use_sg; sgcnt++, slp++) {
	    asc_sg_head.sg_list[sgcnt].addr = cpu_to_le32(sg_dma_address(slp));
	    asc_sg_head.sg_list[sgcnt].bytes = cpu_to_le32(sg_dma_len(slp));
	    ASC_STATS_ADD(scp->device->host, sg_xfer, ASC_CEILING(sg_dma_len(slp), 512));
        }
    }

    ASC_DBG_PRT_ASC_SCSI_Q(2, &asc_scsi_q);
    ASC_DBG_PRT_CDB(1, scp->cmnd, scp->cmd_len);

    return ASC_NOERROR;
}

/*
 * Build a request structure for the Adv Library (Wide Board).
 *
 * If an adv_req_t can not be allocated to issue the request,
 * then return ASC_BUSY. If an error occurs, then return ASC_ERROR.
 *
 * Multi-byte fields in the ASC_SCSI_REQ_Q that are used by the
 * microcode for DMA addresses or math operations are byte swapped
 * to little-endian order.
 */
STATIC int
adv_build_req(asc_board_t *boardp, struct scsi_cmnd *scp,
    ADV_SCSI_REQ_Q **adv_scsiqpp)
{
    adv_req_t           *reqp;
    ADV_SCSI_REQ_Q      *scsiqp;
    int                 i;
    int                 ret;
    struct device	*dev = boardp->dvc_cfg.adv_dvc_cfg.dev;

    /*
     * Allocate an adv_req_t structure from the board to execute
     * the command.
     */
    if (boardp->adv_reqp == NULL) {
        ASC_DBG(1, "adv_build_req: no free adv_req_t\n");
        ASC_STATS(scp->device->host, adv_build_noreq);
        return ASC_BUSY;
    } else {
        reqp = boardp->adv_reqp;
        boardp->adv_reqp = reqp->next_reqp;
        reqp->next_reqp = NULL;
    }

    /*
     * Get 32-byte aligned ADV_SCSI_REQ_Q and ADV_SG_BLOCK pointers.
     */
    scsiqp = (ADV_SCSI_REQ_Q *) ADV_32BALIGN(&reqp->scsi_req_q);

    /*
     * Initialize the structure.
     */
    scsiqp->cntl = scsiqp->scsi_cntl = scsiqp->done_status = 0;

    /*
     * Set the ADV_SCSI_REQ_Q 'srb_ptr' to point to the adv_req_t structure.
     */
    scsiqp->srb_ptr = ASC_VADDR_TO_U32(reqp);

    /*
     * Set the adv_req_t 'cmndp' to point to the struct scsi_cmnd structure.
     */
    reqp->cmndp = scp;

    /*
     * Build the ADV_SCSI_REQ_Q request.
     */

    /*
     * Set CDB length and copy it to the request structure.
     * For wide  boards a CDB length maximum of 16 bytes
     * is supported.
     */
    if (scp->cmd_len > ADV_MAX_CDB_LEN) {
        ASC_PRINT3(
"adv_build_req: board %d: cmd_len %d > ADV_MAX_CDB_LEN  %d\n",
            boardp->id, scp->cmd_len, ADV_MAX_CDB_LEN);
        scp->result = HOST_BYTE(DID_ERROR);
        asc_enqueue(&boardp->done, scp, ASC_BACK);
        return ASC_ERROR;
    }
    scsiqp->cdb_len = scp->cmd_len;
    /* Copy first 12 CDB bytes to cdb[]. */
    for (i = 0; i < scp->cmd_len && i < 12; i++) {
        scsiqp->cdb[i] = scp->cmnd[i];
    }
    /* Copy last 4 CDB bytes, if present, to cdb16[]. */
    for (; i < scp->cmd_len; i++) {
        scsiqp->cdb16[i - 12] = scp->cmnd[i];
    }

    scsiqp->target_id = scp->device->id;
    scsiqp->target_lun = scp->device->lun;

    scsiqp->sense_addr = cpu_to_le32(virt_to_bus(&scp->sense_buffer[0]));
    scsiqp->sense_len = sizeof(scp->sense_buffer);

    /*
     * Build ADV_SCSI_REQ_Q for a contiguous buffer or a scatter-gather
     * buffer command.
     */

    scsiqp->data_cnt = cpu_to_le32(scp->request_bufflen);
    scsiqp->vdata_addr = scp->request_buffer;
    scsiqp->data_addr = cpu_to_le32(virt_to_bus(scp->request_buffer));

    if (scp->use_sg == 0) {
        /*
         * CDB request of single contiguous buffer.
         */
        reqp->sgblkp = NULL;
	scsiqp->data_cnt = cpu_to_le32(scp->request_bufflen);
	if (scp->request_bufflen) {
	    scsiqp->vdata_addr = scp->request_buffer;
	    scp->SCp.dma_handle =
	        dma_map_single(dev, scp->request_buffer,
			       scp->request_bufflen, scp->sc_data_direction);
	} else {
	    scsiqp->vdata_addr = NULL;
	    scp->SCp.dma_handle = 0;
	}
	scsiqp->data_addr = cpu_to_le32(scp->SCp.dma_handle);
        scsiqp->sg_list_ptr = NULL;
        scsiqp->sg_real_addr = 0;
        ASC_STATS(scp->device->host, cont_cnt);
        ASC_STATS_ADD(scp->device->host, cont_xfer,
                      ASC_CEILING(scp->request_bufflen, 512));
    } else {
        /*
         * CDB scatter-gather request list.
         */
	struct scatterlist *slp;
	int use_sg;

	slp = (struct scatterlist *)scp->request_buffer;
	use_sg = dma_map_sg(dev, slp, scp->use_sg, scp->sc_data_direction);

	if (use_sg > ADV_MAX_SG_LIST) {
            ASC_PRINT3(
"adv_build_req: board %d: use_sg %d > ADV_MAX_SG_LIST %d\n",
		boardp->id, use_sg, scp->device->host->sg_tablesize);
	    dma_unmap_sg(dev, slp, scp->use_sg, scp->sc_data_direction);
            scp->result = HOST_BYTE(DID_ERROR);
            asc_enqueue(&boardp->done, scp, ASC_BACK);

            /*
             * Free the 'adv_req_t' structure by adding it back to the
             * board free list.
             */
            reqp->next_reqp = boardp->adv_reqp;
            boardp->adv_reqp = reqp;

            return ASC_ERROR;
        }

	if ((ret = adv_get_sglist(boardp, reqp, scp, use_sg)) != ADV_SUCCESS) {
            /*
             * Free the adv_req_t structure by adding it back to the
             * board free list.
             */
            reqp->next_reqp = boardp->adv_reqp;
            boardp->adv_reqp = reqp;

            return ret;
        }

        ASC_STATS(scp->device->host, sg_cnt);
	ASC_STATS_ADD(scp->device->host, sg_elem, use_sg);
    }

    ASC_DBG_PRT_ADV_SCSI_REQ_Q(2, scsiqp);
    ASC_DBG_PRT_CDB(1, scp->cmnd, scp->cmd_len);

    *adv_scsiqpp = scsiqp;

    return ASC_NOERROR;
}

/*
 * Build scatter-gather list for Adv Library (Wide Board).
 *
 * Additional ADV_SG_BLOCK structures will need to be allocated
 * if the total number of scatter-gather elements exceeds
 * NO_OF_SG_PER_BLOCK (15). The ADV_SG_BLOCK structures are
 * assumed to be physically contiguous.
 *
 * Return:
 *      ADV_SUCCESS(1) - SG List successfully created
 *      ADV_ERROR(-1) - SG List creation failed
 */
STATIC int
adv_get_sglist(asc_board_t *boardp, adv_req_t *reqp, struct scsi_cmnd *scp, int use_sg)
{
    adv_sgblk_t         *sgblkp;
    ADV_SCSI_REQ_Q      *scsiqp;
    struct scatterlist  *slp;
    int                 sg_elem_cnt;
    ADV_SG_BLOCK        *sg_block, *prev_sg_block;
    ADV_PADDR           sg_block_paddr;
    int                 i;

    scsiqp = (ADV_SCSI_REQ_Q *) ADV_32BALIGN(&reqp->scsi_req_q);
    slp = (struct scatterlist *) scp->request_buffer;
    sg_elem_cnt = use_sg;
    prev_sg_block = NULL;
    reqp->sgblkp = NULL;

    do
    {
        /*
         * Allocate a 'adv_sgblk_t' structure from the board free
         * list. One 'adv_sgblk_t' structure holds NO_OF_SG_PER_BLOCK
         * (15) scatter-gather elements.
         */
        if ((sgblkp = boardp->adv_sgblkp) == NULL) {
            ASC_DBG(1, "adv_get_sglist: no free adv_sgblk_t\n");
            ASC_STATS(scp->device->host, adv_build_nosg);

            /*
             * Allocation failed. Free 'adv_sgblk_t' structures already
             * allocated for the request.
             */
            while ((sgblkp = reqp->sgblkp) != NULL)
            {
                /* Remove 'sgblkp' from the request list. */
                reqp->sgblkp = sgblkp->next_sgblkp;

                /* Add 'sgblkp' to the board free list. */
                sgblkp->next_sgblkp = boardp->adv_sgblkp;
                boardp->adv_sgblkp = sgblkp;
            }
            return ASC_BUSY;
        } else {
            /* Complete 'adv_sgblk_t' board allocation. */
            boardp->adv_sgblkp = sgblkp->next_sgblkp;
            sgblkp->next_sgblkp = NULL;

            /*
             * Get 8 byte aligned virtual and physical addresses for
             * the allocated ADV_SG_BLOCK structure.
             */
            sg_block = (ADV_SG_BLOCK *) ADV_8BALIGN(&sgblkp->sg_block);
            sg_block_paddr = virt_to_bus(sg_block);

            /*
             * Check if this is the first 'adv_sgblk_t' for the request.
             */
            if (reqp->sgblkp == NULL)
            {
                /* Request's first scatter-gather block. */
                reqp->sgblkp = sgblkp;

                /*
                 * Set ADV_SCSI_REQ_T ADV_SG_BLOCK virtual and physical
                 * address pointers.
                 */
                scsiqp->sg_list_ptr = sg_block;
                scsiqp->sg_real_addr = cpu_to_le32(sg_block_paddr);
            } else
            {
                /* Request's second or later scatter-gather block. */
                sgblkp->next_sgblkp = reqp->sgblkp;
                reqp->sgblkp = sgblkp;

                /*
                 * Point the previous ADV_SG_BLOCK structure to
                 * the newly allocated ADV_SG_BLOCK structure.
                 */
                ASC_ASSERT(prev_sg_block != NULL);
                prev_sg_block->sg_ptr = cpu_to_le32(sg_block_paddr);
            }
        }

        for (i = 0; i < NO_OF_SG_PER_BLOCK; i++)
        {
	    sg_block->sg_list[i].sg_addr = cpu_to_le32(sg_dma_address(slp));
	    sg_block->sg_list[i].sg_count = cpu_to_le32(sg_dma_len(slp));
	    ASC_STATS_ADD(scp->device->host, sg_xfer, ASC_CEILING(sg_dma_len(slp), 512));

            if (--sg_elem_cnt == 0)
            {   /* Last ADV_SG_BLOCK and scatter-gather entry. */
                sg_block->sg_cnt = i + 1;
                sg_block->sg_ptr = 0L;    /* Last ADV_SG_BLOCK in list. */
                return ADV_SUCCESS;
            }
            slp++;
        }
        sg_block->sg_cnt = NO_OF_SG_PER_BLOCK;
        prev_sg_block = sg_block;
    }
    while (1);
    /* NOTREACHED */
}

/*
 * asc_isr_callback() - Second Level Interrupt Handler called by AscISR().
 *
 * Interrupt callback function for the Narrow SCSI Asc Library.
 */
STATIC void
asc_isr_callback(ASC_DVC_VAR *asc_dvc_varp, ASC_QDONE_INFO *qdonep)
{
    asc_board_t         *boardp;
    struct scsi_cmnd           *scp;
    struct Scsi_Host    *shp;
    int                 i;

    ASC_DBG2(1, "asc_isr_callback: asc_dvc_varp 0x%lx, qdonep 0x%lx\n",
        (ulong) asc_dvc_varp, (ulong) qdonep);
    ASC_DBG_PRT_ASC_QDONE_INFO(2, qdonep);

    /*
     * Get the struct scsi_cmnd structure and Scsi_Host structure for the
     * command that has been completed.
     */
    scp = (struct scsi_cmnd *) ASC_U32_TO_VADDR(qdonep->d2.srb_ptr);
    ASC_DBG1(1, "asc_isr_callback: scp 0x%lx\n", (ulong) scp);

    if (scp == NULL) {
        ASC_PRINT("asc_isr_callback: scp is NULL\n");
        return;
    }
    ASC_DBG_PRT_CDB(2, scp->cmnd, scp->cmd_len);

    /*
     * If the request's host pointer is not valid, display a
     * message and return.
     */
    shp = scp->device->host;
    for (i = 0; i < asc_board_count; i++) {
        if (asc_host[i] == shp) {
            break;
        }
    }
    if (i == asc_board_count) {
        ASC_PRINT2(
            "asc_isr_callback: scp 0x%lx has bad host pointer, host 0x%lx\n",
            (ulong) scp, (ulong) shp);
        return;
    }

    ASC_STATS(shp, callback);
    ASC_DBG1(1, "asc_isr_callback: shp 0x%lx\n", (ulong) shp);

    /*
     * If the request isn't found on the active queue, it may
     * have been removed to handle a reset request.
     * Display a message and return.
     */
    boardp = ASC_BOARDP(shp);
    ASC_ASSERT(asc_dvc_varp == &boardp->dvc_var.asc_dvc_var);
    if (asc_rmqueue(&boardp->active, scp) == ASC_FALSE) {
        ASC_PRINT2(
            "asc_isr_callback: board %d: scp 0x%lx not on active queue\n",
            boardp->id, (ulong) scp);
        return;
    }

    /*
     * 'qdonep' contains the command's ending status.
     */
    switch (qdonep->d3.done_stat) {
    case QD_NO_ERROR:
        ASC_DBG(2, "asc_isr_callback: QD_NO_ERROR\n");
        scp->result = 0;

        /*
         * If an INQUIRY command completed successfully, then call
         * the AscInquiryHandling() function to set-up the device.
         */
        if (scp->cmnd[0] == INQUIRY && scp->device->lun == 0 &&
            (scp->request_bufflen - qdonep->remain_bytes) >= 8)
        {
            AscInquiryHandling(asc_dvc_varp, scp->device->id & 0x7,
                (ASC_SCSI_INQUIRY *) scp->request_buffer);
        }

        /*
         * Check for an underrun condition.
         *
         * If there was no error and an underrun condition, then
         * then return the number of underrun bytes.
         */
        if (scp->request_bufflen != 0 && qdonep->remain_bytes != 0 &&
            qdonep->remain_bytes <= scp->request_bufflen) {
            ASC_DBG1(1, "asc_isr_callback: underrun condition %u bytes\n",
            (unsigned) qdonep->remain_bytes);
            scp->resid = qdonep->remain_bytes;
        }
        break;

    case QD_WITH_ERROR:
        ASC_DBG(2, "asc_isr_callback: QD_WITH_ERROR\n");
        switch (qdonep->d3.host_stat) {
        case QHSTA_NO_ERROR:
            if (qdonep->d3.scsi_stat == SAM_STAT_CHECK_CONDITION) {
                ASC_DBG(2, "asc_isr_callback: SAM_STAT_CHECK_CONDITION\n");
                ASC_DBG_PRT_SENSE(2, scp->sense_buffer,
                    sizeof(scp->sense_buffer));
                /*
                 * Note: The 'status_byte()' macro used by target drivers
                 * defined in scsi.h shifts the status byte returned by
                 * host drivers right by 1 bit. This is why target drivers
                 * also use right shifted status byte definitions. For
                 * instance target drivers use CHECK_CONDITION, defined to
                 * 0x1, instead of the SCSI defined check condition value
                 * of 0x2. Host drivers are supposed to return the status
                 * byte as it is defined by SCSI.
                 */
                scp->result = DRIVER_BYTE(DRIVER_SENSE) |
                    STATUS_BYTE(qdonep->d3.scsi_stat);
            } else {
                scp->result = STATUS_BYTE(qdonep->d3.scsi_stat);
            }
            break;

        default:
            /* QHSTA error occurred */
            ASC_DBG1(1, "asc_isr_callback: host_stat 0x%x\n",
                qdonep->d3.host_stat);
            scp->result = HOST_BYTE(DID_BAD_TARGET);
            break;
        }
        break;

    case QD_ABORTED_BY_HOST:
        ASC_DBG(1, "asc_isr_callback: QD_ABORTED_BY_HOST\n");
        scp->result = HOST_BYTE(DID_ABORT) | MSG_BYTE(qdonep->d3.scsi_msg) |
                STATUS_BYTE(qdonep->d3.scsi_stat);
        break;

    default:
        ASC_DBG1(1, "asc_isr_callback: done_stat 0x%x\n", qdonep->d3.done_stat);
        scp->result = HOST_BYTE(DID_ERROR) | MSG_BYTE(qdonep->d3.scsi_msg) |
                STATUS_BYTE(qdonep->d3.scsi_stat);
        break;
    }

    /*
     * If the 'init_tidmask' bit isn't already set for the target and the
     * current request finished normally, then set the bit for the target
     * to indicate that a device is present.
     */
    if ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(scp->device->id)) == 0 &&
        qdonep->d3.done_stat == QD_NO_ERROR &&
        qdonep->d3.host_stat == QHSTA_NO_ERROR) {
        boardp->init_tidmask |= ADV_TID_TO_TIDMASK(scp->device->id);
    }

    /*
     * Because interrupts may be enabled by the 'struct scsi_cmnd' done
     * function, add the command to the end of the board's done queue.
     * The done function for the command will be called from
     * advansys_interrupt().
     */
    asc_enqueue(&boardp->done, scp, ASC_BACK);

    return;
}

/*
 * adv_isr_callback() - Second Level Interrupt Handler called by AdvISR().
 *
 * Callback function for the Wide SCSI Adv Library.
 */
STATIC void
adv_isr_callback(ADV_DVC_VAR *adv_dvc_varp, ADV_SCSI_REQ_Q *scsiqp)
{
    asc_board_t         *boardp;
    adv_req_t           *reqp;
    adv_sgblk_t         *sgblkp;
    struct scsi_cmnd           *scp;
    struct Scsi_Host    *shp;
    int                 i;
    ADV_DCNT            resid_cnt;


    ASC_DBG2(1, "adv_isr_callback: adv_dvc_varp 0x%lx, scsiqp 0x%lx\n",
        (ulong) adv_dvc_varp, (ulong) scsiqp);
    ASC_DBG_PRT_ADV_SCSI_REQ_Q(2, scsiqp);

    /*
     * Get the adv_req_t structure for the command that has been
     * completed. The adv_req_t structure actually contains the
     * completed ADV_SCSI_REQ_Q structure.
     */
    reqp = (adv_req_t *) ADV_U32_TO_VADDR(scsiqp->srb_ptr);
    ASC_DBG1(1, "adv_isr_callback: reqp 0x%lx\n", (ulong) reqp);
    if (reqp == NULL) {
        ASC_PRINT("adv_isr_callback: reqp is NULL\n");
        return;
    }

    /*
     * Get the struct scsi_cmnd structure and Scsi_Host structure for the
     * command that has been completed.
     *
     * Note: The adv_req_t request structure and adv_sgblk_t structure,
     * if any, are dropped, because a board structure pointer can not be
     * determined.
     */
    scp = reqp->cmndp;
    ASC_DBG1(1, "adv_isr_callback: scp 0x%lx\n", (ulong) scp);
    if (scp == NULL) {
        ASC_PRINT("adv_isr_callback: scp is NULL; adv_req_t dropped.\n");
        return;
    }
    ASC_DBG_PRT_CDB(2, scp->cmnd, scp->cmd_len);

    /*
     * If the request's host pointer is not valid, display a message
     * and return.
     */
    shp = scp->device->host;
    for (i = 0; i < asc_board_count; i++) {
        if (asc_host[i] == shp) {
            break;
        }
    }
    /*
     * Note: If the host structure is not found, the adv_req_t request
     * structure and adv_sgblk_t structure, if any, is dropped.
     */
    if (i == asc_board_count) {
        ASC_PRINT2(
            "adv_isr_callback: scp 0x%lx has bad host pointer, host 0x%lx\n",
            (ulong) scp, (ulong) shp);
        return;
    }

    ASC_STATS(shp, callback);
    ASC_DBG1(1, "adv_isr_callback: shp 0x%lx\n", (ulong) shp);

    /*
     * If the request isn't found on the active queue, it may have been
     * removed to handle a reset request. Display a message and return.
     *
     * Note: Because the structure may still be in use don't attempt
     * to free the adv_req_t and adv_sgblk_t, if any, structures.
     */
    boardp = ASC_BOARDP(shp);
    ASC_ASSERT(adv_dvc_varp == &boardp->dvc_var.adv_dvc_var);
    if (asc_rmqueue(&boardp->active, scp) == ASC_FALSE) {
        ASC_PRINT2(
            "adv_isr_callback: board %d: scp 0x%lx not on active queue\n",
            boardp->id, (ulong) scp);
        return;
    }

    /*
     * 'done_status' contains the command's ending status.
     */
    switch (scsiqp->done_status) {
    case QD_NO_ERROR:
        ASC_DBG(2, "adv_isr_callback: QD_NO_ERROR\n");
        scp->result = 0;

        /*
         * Check for an underrun condition.
         *
         * If there was no error and an underrun condition, then
         * then return the number of underrun bytes.
         */
        resid_cnt = le32_to_cpu(scsiqp->data_cnt);
        if (scp->request_bufflen != 0 && resid_cnt != 0 &&
            resid_cnt <= scp->request_bufflen) {
            ASC_DBG1(1, "adv_isr_callback: underrun condition %lu bytes\n",
                (ulong) resid_cnt);
            scp->resid = resid_cnt;
        }
        break;

    case QD_WITH_ERROR:
        ASC_DBG(2, "adv_isr_callback: QD_WITH_ERROR\n");
        switch (scsiqp->host_status) {
        case QHSTA_NO_ERROR:
            if (scsiqp->scsi_status == SAM_STAT_CHECK_CONDITION) {
                ASC_DBG(2, "adv_isr_callback: SAM_STAT_CHECK_CONDITION\n");
                ASC_DBG_PRT_SENSE(2, scp->sense_buffer,
                    sizeof(scp->sense_buffer));
                /*
                 * Note: The 'status_byte()' macro used by target drivers
                 * defined in scsi.h shifts the status byte returned by
                 * host drivers right by 1 bit. This is why target drivers
                 * also use right shifted status byte definitions. For
                 * instance target drivers use CHECK_CONDITION, defined to
                 * 0x1, instead of the SCSI defined check condition value
                 * of 0x2. Host drivers are supposed to return the status
                 * byte as it is defined by SCSI.
                 */
                scp->result = DRIVER_BYTE(DRIVER_SENSE) |
                    STATUS_BYTE(scsiqp->scsi_status);
            } else {
                scp->result = STATUS_BYTE(scsiqp->scsi_status);
            }
            break;

        default:
            /* Some other QHSTA error occurred. */
            ASC_DBG1(1, "adv_isr_callback: host_status 0x%x\n",
                scsiqp->host_status);
            scp->result = HOST_BYTE(DID_BAD_TARGET);
            break;
        }
        break;

    case QD_ABORTED_BY_HOST:
        ASC_DBG(1, "adv_isr_callback: QD_ABORTED_BY_HOST\n");
        scp->result = HOST_BYTE(DID_ABORT) | STATUS_BYTE(scsiqp->scsi_status);
        break;

    default:
        ASC_DBG1(1, "adv_isr_callback: done_status 0x%x\n", scsiqp->done_status);
        scp->result = HOST_BYTE(DID_ERROR) | STATUS_BYTE(scsiqp->scsi_status);
        break;
    }

    /*
     * If the 'init_tidmask' bit isn't already set for the target and the
     * current request finished normally, then set the bit for the target
     * to indicate that a device is present.
     */
    if ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(scp->device->id)) == 0 &&
        scsiqp->done_status == QD_NO_ERROR &&
        scsiqp->host_status == QHSTA_NO_ERROR) {
        boardp->init_tidmask |= ADV_TID_TO_TIDMASK(scp->device->id);
    }

    /*
     * Because interrupts may be enabled by the 'struct scsi_cmnd' done
     * function, add the command to the end of the board's done queue.
     * The done function for the command will be called from
     * advansys_interrupt().
     */
    asc_enqueue(&boardp->done, scp, ASC_BACK);

    /*
     * Free all 'adv_sgblk_t' structures allocated for the request.
     */
    while ((sgblkp = reqp->sgblkp) != NULL)
    {
        /* Remove 'sgblkp' from the request list. */
        reqp->sgblkp = sgblkp->next_sgblkp;

        /* Add 'sgblkp' to the board free list. */
        sgblkp->next_sgblkp = boardp->adv_sgblkp;
        boardp->adv_sgblkp = sgblkp;
    }

    /*
     * Free the adv_req_t structure used with the command by adding
     * it back to the board free list.
     */
    reqp->next_reqp = boardp->adv_reqp;
    boardp->adv_reqp = reqp;

    ASC_DBG(1, "adv_isr_callback: done\n");

    return;
}

/*
 * adv_async_callback() - Adv Library asynchronous event callback function.
 */
STATIC void
adv_async_callback(ADV_DVC_VAR *adv_dvc_varp, uchar code)
{
    switch (code)
    {
    case ADV_ASYNC_SCSI_BUS_RESET_DET:
        /*
         * The firmware detected a SCSI Bus reset.
         */
        ASC_DBG(0, "adv_async_callback: ADV_ASYNC_SCSI_BUS_RESET_DET\n");
        break;

    case ADV_ASYNC_RDMA_FAILURE:
        /*
         * Handle RDMA failure by resetting the SCSI Bus and
         * possibly the chip if it is unresponsive. Log the error
         * with a unique code.
         */
        ASC_DBG(0, "adv_async_callback: ADV_ASYNC_RDMA_FAILURE\n");
        AdvResetChipAndSB(adv_dvc_varp);
        break;

    case ADV_HOST_SCSI_BUS_RESET:
        /*
         * Host generated SCSI bus reset occurred.
         */
        ASC_DBG(0, "adv_async_callback: ADV_HOST_SCSI_BUS_RESET\n");
        break;

    default:
        ASC_DBG1(0, "DvcAsyncCallBack: unknown code 0x%x\n", code);
        break;
    }
}

/*
 * Add a 'REQP' to the end of specified queue. Set 'tidmask'
 * to indicate a command is queued for the device.
 *
 * 'flag' may be either ASC_FRONT or ASC_BACK.
 *
 * 'REQPNEXT(reqp)' returns reqp's next pointer.
 */
STATIC void
asc_enqueue(asc_queue_t *ascq, REQP reqp, int flag)
{
    int        tid;

    ASC_DBG3(3, "asc_enqueue: ascq 0x%lx, reqp 0x%lx, flag %d\n",
        (ulong) ascq, (ulong) reqp, flag);
    ASC_ASSERT(reqp != NULL);
    ASC_ASSERT(flag == ASC_FRONT || flag == ASC_BACK);
    tid = REQPTID(reqp);
    ASC_ASSERT(tid >= 0 && tid <= ADV_MAX_TID);
    if (flag == ASC_FRONT) {
        reqp->host_scribble = (unsigned char *)ascq->q_first[tid];
        ascq->q_first[tid] = reqp;
        /* If the queue was empty, set the last pointer. */
        if (ascq->q_last[tid] == NULL) {
            ascq->q_last[tid] = reqp;
        }
    } else { /* ASC_BACK */
        if (ascq->q_last[tid] != NULL) {
            ascq->q_last[tid]->host_scribble = (unsigned char *)reqp;
        }
        ascq->q_last[tid] = reqp;
        reqp->host_scribble = NULL;
        /* If the queue was empty, set the first pointer. */
        if (ascq->q_first[tid] == NULL) {
            ascq->q_first[tid] = reqp;
        }
    }
    /* The queue has at least one entry, set its bit. */
    ascq->q_tidmask |= ADV_TID_TO_TIDMASK(tid);
#ifdef ADVANSYS_STATS
    /* Maintain request queue statistics. */
    ascq->q_tot_cnt[tid]++;
    ascq->q_cur_cnt[tid]++;
    if (ascq->q_cur_cnt[tid] > ascq->q_max_cnt[tid]) {
        ascq->q_max_cnt[tid] = ascq->q_cur_cnt[tid];
        ASC_DBG2(2, "asc_enqueue: new q_max_cnt[%d] %d\n",
            tid, ascq->q_max_cnt[tid]);
    }
    REQPTIME(reqp) = REQTIMESTAMP();
#endif /* ADVANSYS_STATS */
    ASC_DBG1(3, "asc_enqueue: reqp 0x%lx\n", (ulong) reqp);
    return;
}

/*
 * Return first queued 'REQP' on the specified queue for
 * the specified target device. Clear the 'tidmask' bit for
 * the device if no more commands are left queued for it.
 *
 * 'REQPNEXT(reqp)' returns reqp's next pointer.
 */
STATIC REQP
asc_dequeue(asc_queue_t *ascq, int tid)
{
    REQP    reqp;

    ASC_DBG2(3, "asc_dequeue: ascq 0x%lx, tid %d\n", (ulong) ascq, tid);
    ASC_ASSERT(tid >= 0 && tid <= ADV_MAX_TID);
    if ((reqp = ascq->q_first[tid]) != NULL) {
        ASC_ASSERT(ascq->q_tidmask & ADV_TID_TO_TIDMASK(tid));
        ascq->q_first[tid] = REQPNEXT(reqp);
        /* If the queue is empty, clear its bit and the last pointer. */
        if (ascq->q_first[tid] == NULL) {
            ascq->q_tidmask &= ~ADV_TID_TO_TIDMASK(tid);
            ASC_ASSERT(ascq->q_last[tid] == reqp);
            ascq->q_last[tid] = NULL;
        }
#ifdef ADVANSYS_STATS
        /* Maintain request queue statistics. */
        ascq->q_cur_cnt[tid]--;
        ASC_ASSERT(ascq->q_cur_cnt[tid] >= 0);
        REQTIMESTAT("asc_dequeue", ascq, reqp, tid);
#endif /* ADVANSYS_STATS */
    }
    ASC_DBG1(3, "asc_dequeue: reqp 0x%lx\n", (ulong) reqp);
    return reqp;
}

/*
 * Return a pointer to a singly linked list of all the requests queued
 * for 'tid' on the 'asc_queue_t' pointed to by 'ascq'.
 *
 * If 'lastpp' is not NULL, '*lastpp' will be set to point to the
 * the last request returned in the singly linked list.
 *
 * 'tid' should either be a valid target id or if it is ASC_TID_ALL,
 * then all queued requests are concatenated into one list and
 * returned.
 *
 * Note: If 'lastpp' is used to append a new list to the end of
 * an old list, only change the old list last pointer if '*lastpp'
 * (or the function return value) is not NULL, i.e. use a temporary
 * variable for 'lastpp' and check its value after the function return
 * before assigning it to the list last pointer.
 *
 * Unfortunately collecting queuing time statistics adds overhead to
 * the function that isn't inherent to the function's algorithm.
 */
STATIC REQP
asc_dequeue_list(asc_queue_t *ascq, REQP *lastpp, int tid)
{
    REQP    firstp, lastp;
    int     i;

    ASC_DBG2(3, "asc_dequeue_list: ascq 0x%lx, tid %d\n", (ulong) ascq, tid);
    ASC_ASSERT((tid == ASC_TID_ALL) || (tid >= 0 && tid <= ADV_MAX_TID));

    /*
     * If 'tid' is not ASC_TID_ALL, return requests only for
     * the specified 'tid'. If 'tid' is ASC_TID_ALL, return all
     * requests for all tids.
     */
    if (tid != ASC_TID_ALL) {
        /* Return all requests for the specified 'tid'. */
        if ((ascq->q_tidmask & ADV_TID_TO_TIDMASK(tid)) == 0) {
            /* List is empty; Set first and last return pointers to NULL. */
            firstp = lastp = NULL;
        } else {
            firstp = ascq->q_first[tid];
            lastp = ascq->q_last[tid];
            ascq->q_first[tid] = ascq->q_last[tid] = NULL;
            ascq->q_tidmask &= ~ADV_TID_TO_TIDMASK(tid);
#ifdef ADVANSYS_STATS
            {
                REQP reqp;
                ascq->q_cur_cnt[tid] = 0;
                for (reqp = firstp; reqp; reqp = REQPNEXT(reqp)) {
                    REQTIMESTAT("asc_dequeue_list", ascq, reqp, tid);
                }
            }
#endif /* ADVANSYS_STATS */
        }
    } else {
        /* Return all requests for all tids. */
        firstp = lastp = NULL;
        for (i = 0; i <= ADV_MAX_TID; i++) {
            if (ascq->q_tidmask & ADV_TID_TO_TIDMASK(i)) {
                if (firstp == NULL) {
                    firstp = ascq->q_first[i];
                    lastp = ascq->q_last[i];
                } else {
                    ASC_ASSERT(lastp != NULL);
                    lastp->host_scribble = (unsigned char *)ascq->q_first[i];
                    lastp = ascq->q_last[i];
                }
                ascq->q_first[i] = ascq->q_last[i] = NULL;
                ascq->q_tidmask &= ~ADV_TID_TO_TIDMASK(i);
#ifdef ADVANSYS_STATS
                ascq->q_cur_cnt[i] = 0;
#endif /* ADVANSYS_STATS */
            }
        }
#ifdef ADVANSYS_STATS
        {
            REQP reqp;
            for (reqp = firstp; reqp; reqp = REQPNEXT(reqp)) {
                REQTIMESTAT("asc_dequeue_list", ascq, reqp, reqp->device->id);
            }
        }
#endif /* ADVANSYS_STATS */
    }
    if (lastpp) {
        *lastpp = lastp;
    }
    ASC_DBG1(3, "asc_dequeue_list: firstp 0x%lx\n", (ulong) firstp);
    return firstp;
}

/*
 * Remove the specified 'REQP' from the specified queue for
 * the specified target device. Clear the 'tidmask' bit for the
 * device if no more commands are left queued for it.
 *
 * 'REQPNEXT(reqp)' returns reqp's the next pointer.
 *
 * Return ASC_TRUE if the command was found and removed,
 * otherwise return ASC_FALSE.
 */
STATIC int
asc_rmqueue(asc_queue_t *ascq, REQP reqp)
{
    REQP        currp, prevp;
    int         tid;
    int         ret = ASC_FALSE;

    ASC_DBG2(3, "asc_rmqueue: ascq 0x%lx, reqp 0x%lx\n",
        (ulong) ascq, (ulong) reqp);
    ASC_ASSERT(reqp != NULL);

    tid = REQPTID(reqp);
    ASC_ASSERT(tid >= 0 && tid <= ADV_MAX_TID);

    /*
     * Handle the common case of 'reqp' being the first
     * entry on the queue.
     */
    if (reqp == ascq->q_first[tid]) {
        ret = ASC_TRUE;
        ascq->q_first[tid] = REQPNEXT(reqp);
        /* If the queue is now empty, clear its bit and the last pointer. */
        if (ascq->q_first[tid] == NULL) {
            ascq->q_tidmask &= ~ADV_TID_TO_TIDMASK(tid);
            ASC_ASSERT(ascq->q_last[tid] == reqp);
            ascq->q_last[tid] = NULL;
        }
    } else if (ascq->q_first[tid] != NULL) {
        ASC_ASSERT(ascq->q_last[tid] != NULL);
        /*
         * Because the case of 'reqp' being the first entry has been
         * handled above and it is known the queue is not empty, if
         * 'reqp' is found on the queue it is guaranteed the queue will
         * not become empty and that 'q_first[tid]' will not be changed.
         *
         * Set 'prevp' to the first entry, 'currp' to the second entry,
         * and search for 'reqp'.
         */
        for (prevp = ascq->q_first[tid], currp = REQPNEXT(prevp);
             currp; prevp = currp, currp = REQPNEXT(currp)) {
            if (currp == reqp) {
                ret = ASC_TRUE;
                prevp->host_scribble = (unsigned char *)REQPNEXT(currp);
                reqp->host_scribble = NULL;
                if (ascq->q_last[tid] == reqp) {
                    ascq->q_last[tid] = prevp;
                }
                break;
            }
        }
    }
#ifdef ADVANSYS_STATS
    /* Maintain request queue statistics. */
    if (ret == ASC_TRUE) {
        ascq->q_cur_cnt[tid]--;
        REQTIMESTAT("asc_rmqueue", ascq, reqp, tid);
    }
    ASC_ASSERT(ascq->q_cur_cnt[tid] >= 0);
#endif /* ADVANSYS_STATS */
    ASC_DBG2(3, "asc_rmqueue: reqp 0x%lx, ret %d\n", (ulong) reqp, ret);
    return ret;
}

/*
 * Execute as many queued requests as possible for the specified queue.
 *
 * Calls asc_execute_scsi_cmnd() to execute a REQP/struct scsi_cmnd.
 */
STATIC void
asc_execute_queue(asc_queue_t *ascq)
{
    ADV_SCSI_BIT_ID_TYPE    scan_tidmask;
    REQP                    reqp;
    int                     i;

    ASC_DBG1(1, "asc_execute_queue: ascq 0x%lx\n", (ulong) ascq);
    /*
     * Execute queued commands for devices attached to
     * the current board in round-robin fashion.
     */
    scan_tidmask = ascq->q_tidmask;
    do {
        for (i = 0; i <= ADV_MAX_TID; i++) {
            if (scan_tidmask & ADV_TID_TO_TIDMASK(i)) {
                if ((reqp = asc_dequeue(ascq, i)) == NULL) {
                    scan_tidmask &= ~ADV_TID_TO_TIDMASK(i);
                } else if (asc_execute_scsi_cmnd((struct scsi_cmnd *) reqp)
                            == ASC_BUSY) {
                    scan_tidmask &= ~ADV_TID_TO_TIDMASK(i);
                    /*
                     * The request returned ASC_BUSY. Enqueue at the front of
                     * target's waiting list to maintain correct ordering.
                     */
                    asc_enqueue(ascq, reqp, ASC_FRONT);
                }
            }
        }
    } while (scan_tidmask);
    return;
}

#ifdef CONFIG_PROC_FS
/*
 * asc_prt_board_devices()
 *
 * Print driver information for devices attached to the board.
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
STATIC int
asc_prt_board_devices(struct Scsi_Host *shp, char *cp, int cplen)
{
    asc_board_t        *boardp;
    int                leftlen;
    int                totlen;
    int                len;
    int                chip_scsi_id;
    int                i;

    boardp = ASC_BOARDP(shp);
    leftlen = cplen;
    totlen = len = 0;

    len = asc_prt_line(cp, leftlen,
"\nDevice Information for AdvanSys SCSI Host %d:\n", shp->host_no);
    ASC_PRT_NEXT();

    if (ASC_NARROW_BOARD(boardp)) {
        chip_scsi_id = boardp->dvc_cfg.asc_dvc_cfg.chip_scsi_id;
    } else {
        chip_scsi_id = boardp->dvc_var.adv_dvc_var.chip_scsi_id;
    }

    len = asc_prt_line(cp, leftlen, "Target IDs Detected:");
    ASC_PRT_NEXT();
    for (i = 0; i <= ADV_MAX_TID; i++) {
        if (boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) {
            len = asc_prt_line(cp, leftlen, " %X,", i);
            ASC_PRT_NEXT();
        }
    }
    len = asc_prt_line(cp, leftlen, " (%X=Host Adapter)\n", chip_scsi_id);
    ASC_PRT_NEXT();

    return totlen;
}

/*
 * Display Wide Board BIOS Information.
 */
STATIC int
asc_prt_adv_bios(struct Scsi_Host *shp, char *cp, int cplen)
{
    asc_board_t        *boardp;
    int                leftlen;
    int                totlen;
    int                len;
    ushort             major, minor, letter;

    boardp = ASC_BOARDP(shp);
    leftlen = cplen;
    totlen = len = 0;

    len = asc_prt_line(cp, leftlen, "\nROM BIOS Version: ");
    ASC_PRT_NEXT();

    /*
     * If the BIOS saved a valid signature, then fill in
     * the BIOS code segment base address.
     */
    if (boardp->bios_signature != 0x55AA) {
        len = asc_prt_line(cp, leftlen, "Disabled or Pre-3.1\n");
        ASC_PRT_NEXT();
        len = asc_prt_line(cp, leftlen,
"BIOS either disabled or Pre-3.1. If it is pre-3.1, then a newer version\n");
        ASC_PRT_NEXT();
        len = asc_prt_line(cp, leftlen,
"can be found at the ConnectCom FTP site: ftp://ftp.connectcom.net/pub\n");
        ASC_PRT_NEXT();
    } else {
        major = (boardp->bios_version >> 12) & 0xF;
        minor = (boardp->bios_version >> 8) & 0xF;
        letter = (boardp->bios_version & 0xFF);

        len = asc_prt_line(cp, leftlen, "%d.%d%c\n",
            major, minor, letter >= 26 ? '?' : letter + 'A');
        ASC_PRT_NEXT();

        /*
         * Current available ROM BIOS release is 3.1I for UW
         * and 3.2I for U2W. This code doesn't differentiate
         * UW and U2W boards.
         */
        if (major < 3 || (major <= 3 && minor < 1) ||
            (major <= 3 && minor <= 1 && letter < ('I'- 'A'))) {
            len = asc_prt_line(cp, leftlen,
"Newer version of ROM BIOS is available at the ConnectCom FTP site:\n");
            ASC_PRT_NEXT();
            len = asc_prt_line(cp, leftlen,
"ftp://ftp.connectcom.net/pub\n");
            ASC_PRT_NEXT();
        }
    }

    return totlen;
}

/*
 * Add serial number to information bar if signature AAh
 * is found in at bit 15-9 (7 bits) of word 1.
 *
 * Serial Number consists fo 12 alpha-numeric digits.
 *
 *       1 - Product type (A,B,C,D..)  Word0: 15-13 (3 bits)
 *       2 - MFG Location (A,B,C,D..)  Word0: 12-10 (3 bits)
 *     3-4 - Product ID (0-99)         Word0: 9-0 (10 bits)
 *       5 - Product revision (A-J)    Word0:  "         "
 *
 *           Signature                 Word1: 15-9 (7 bits)
 *       6 - Year (0-9)                Word1: 8-6 (3 bits) & Word2: 15 (1 bit)
 *     7-8 - Week of the year (1-52)   Word1: 5-0 (6 bits)
 *
 *    9-12 - Serial Number (A001-Z999) Word2: 14-0 (15 bits)
 *
 * Note 1: Only production cards will have a serial number.
 *
 * Note 2: Signature is most significant 7 bits (0xFE).
 *
 * Returns ASC_TRUE if serial number found, otherwise returns ASC_FALSE.
 */
STATIC int
asc_get_eeprom_string(ushort *serialnum, uchar *cp)
{
    ushort      w, num;

    if ((serialnum[1] & 0xFE00) != ((ushort) 0xAA << 8)) {
        return ASC_FALSE;
    } else {
        /*
         * First word - 6 digits.
         */
        w = serialnum[0];

        /* Product type - 1st digit. */
        if ((*cp = 'A' + ((w & 0xE000) >> 13)) == 'H') {
            /* Product type is P=Prototype */
            *cp += 0x8;
        }
        cp++;

        /* Manufacturing location - 2nd digit. */
        *cp++ = 'A' + ((w & 0x1C00) >> 10);

        /* Product ID - 3rd, 4th digits. */
        num = w & 0x3FF;
        *cp++ = '0' + (num / 100);
        num %= 100;
        *cp++ = '0' + (num / 10);

        /* Product revision - 5th digit. */
        *cp++ = 'A' + (num % 10);

        /*
         * Second word
         */
        w = serialnum[1];

        /*
         * Year - 6th digit.
         *
         * If bit 15 of third word is set, then the
         * last digit of the year is greater than 7.
         */
        if (serialnum[2] & 0x8000) {
            *cp++ = '8' + ((w & 0x1C0) >> 6);
        } else {
            *cp++ = '0' + ((w & 0x1C0) >> 6);
        }

        /* Week of year - 7th, 8th digits. */
        num = w & 0x003F;
        *cp++ = '0' + num / 10;
        num %= 10;
        *cp++ = '0' + num;

        /*
         * Third word
         */
        w = serialnum[2] & 0x7FFF;

        /* Serial number - 9th digit. */
        *cp++ = 'A' + (w / 1000);

        /* 10th, 11th, 12th digits. */
        num = w % 1000;
        *cp++ = '0' + num / 100;
        num %= 100;
        *cp++ = '0' + num / 10;
        num %= 10;
        *cp++ = '0' + num;

        *cp = '\0';     /* Null Terminate the string. */
        return ASC_TRUE;
    }
}

/*
 * asc_prt_asc_board_eeprom()
 *
 * Print board EEPROM configuration.
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
STATIC int
asc_prt_asc_board_eeprom(struct Scsi_Host *shp, char *cp, int cplen)
{
    asc_board_t        *boardp;
    ASC_DVC_VAR        *asc_dvc_varp;
    int                leftlen;
    int                totlen;
    int                len;
    ASCEEP_CONFIG      *ep;
    int                i;
#ifdef CONFIG_ISA
    int                isa_dma_speed[] = { 10, 8, 7, 6, 5, 4, 3, 2 };
#endif /* CONFIG_ISA */
    uchar              serialstr[13];

    boardp = ASC_BOARDP(shp);
    asc_dvc_varp = &boardp->dvc_var.asc_dvc_var;
    ep = &boardp->eep_config.asc_eep;

    leftlen = cplen;
    totlen = len = 0;

    len = asc_prt_line(cp, leftlen,
"\nEEPROM Settings for AdvanSys SCSI Host %d:\n", shp->host_no);
    ASC_PRT_NEXT();

    if (asc_get_eeprom_string((ushort *) &ep->adapter_info[0], serialstr) ==
        ASC_TRUE) {
        len = asc_prt_line(cp, leftlen, " Serial Number: %s\n", serialstr);
        ASC_PRT_NEXT();
    } else {
        if (ep->adapter_info[5] == 0xBB) {
            len = asc_prt_line(cp, leftlen,
                " Default Settings Used for EEPROM-less Adapter.\n");
            ASC_PRT_NEXT();
        } else {
            len = asc_prt_line(cp, leftlen,
                " Serial Number Signature Not Present.\n");
            ASC_PRT_NEXT();
        }
    }

    len = asc_prt_line(cp, leftlen,
" Host SCSI ID: %u, Host Queue Size: %u, Device Queue Size: %u\n",
        ASC_EEP_GET_CHIP_ID(ep), ep->max_total_qng, ep->max_tag_qng);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" cntl 0x%x, no_scam 0x%x\n",
        ep->cntl, ep->no_scam);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" Target ID:           ");
    ASC_PRT_NEXT();
    for (i = 0; i <= ASC_MAX_TID; i++) {
        len = asc_prt_line(cp, leftlen, " %d", i);
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" Disconnects:         ");
    ASC_PRT_NEXT();
    for (i = 0; i <= ASC_MAX_TID; i++) {
        len = asc_prt_line(cp, leftlen, " %c",
            (ep->disc_enable & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" Command Queuing:     ");
    ASC_PRT_NEXT();
    for (i = 0; i <= ASC_MAX_TID; i++) {
        len = asc_prt_line(cp, leftlen, " %c",
            (ep->use_cmd_qng & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" Start Motor:         ");
    ASC_PRT_NEXT();
    for (i = 0; i <= ASC_MAX_TID; i++) {
        len = asc_prt_line(cp, leftlen, " %c",
            (ep->start_motor & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" Synchronous Transfer:");
    ASC_PRT_NEXT();
    for (i = 0; i <= ASC_MAX_TID; i++) {
        len = asc_prt_line(cp, leftlen, " %c",
            (ep->init_sdtr & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

#ifdef CONFIG_ISA
    if (asc_dvc_varp->bus_type & ASC_IS_ISA) {
        len = asc_prt_line(cp, leftlen,
" Host ISA DMA speed:   %d MB/S\n",
            isa_dma_speed[ASC_EEP_GET_DMA_SPD(ep)]);
        ASC_PRT_NEXT();
    }
#endif /* CONFIG_ISA */

     return totlen;
}

/*
 * asc_prt_adv_board_eeprom()
 *
 * Print board EEPROM configuration.
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
STATIC int
asc_prt_adv_board_eeprom(struct Scsi_Host *shp, char *cp, int cplen)
{
    asc_board_t                 *boardp;
    ADV_DVC_VAR                 *adv_dvc_varp;
    int                         leftlen;
    int                         totlen;
    int                         len;
    int                         i;
    char                        *termstr;
    uchar                       serialstr[13];
    ADVEEP_3550_CONFIG          *ep_3550 = NULL;
    ADVEEP_38C0800_CONFIG       *ep_38C0800 = NULL;
    ADVEEP_38C1600_CONFIG       *ep_38C1600 = NULL;
    ushort                      word;
    ushort                      *wordp;
    ushort                      sdtr_speed = 0;

    boardp = ASC_BOARDP(shp);
    adv_dvc_varp = &boardp->dvc_var.adv_dvc_var;
    if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
    {
        ep_3550 = &boardp->eep_config.adv_3550_eep;
    } else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800)
    {
        ep_38C0800 = &boardp->eep_config.adv_38C0800_eep;
    } else
    {
        ep_38C1600 = &boardp->eep_config.adv_38C1600_eep;
    }

    leftlen = cplen;
    totlen = len = 0;

    len = asc_prt_line(cp, leftlen,
"\nEEPROM Settings for AdvanSys SCSI Host %d:\n", shp->host_no);
    ASC_PRT_NEXT();

    if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
    {
        wordp = &ep_3550->serial_number_word1;
    } else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800)
    {
        wordp = &ep_38C0800->serial_number_word1;
    } else
    {
        wordp = &ep_38C1600->serial_number_word1;
    }

    if (asc_get_eeprom_string(wordp, serialstr) == ASC_TRUE) {
        len = asc_prt_line(cp, leftlen, " Serial Number: %s\n", serialstr);
        ASC_PRT_NEXT();
    } else {
        len = asc_prt_line(cp, leftlen,
            " Serial Number Signature Not Present.\n");
        ASC_PRT_NEXT();
    }

    if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
    {
        len = asc_prt_line(cp, leftlen,
" Host SCSI ID: %u, Host Queue Size: %u, Device Queue Size: %u\n",
            ep_3550->adapter_scsi_id, ep_3550->max_host_qng,
            ep_3550->max_dvc_qng);
        ASC_PRT_NEXT();
    } else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800)
    {
        len = asc_prt_line(cp, leftlen,
" Host SCSI ID: %u, Host Queue Size: %u, Device Queue Size: %u\n",
            ep_38C0800->adapter_scsi_id, ep_38C0800->max_host_qng,
            ep_38C0800->max_dvc_qng);
        ASC_PRT_NEXT();
    } else
    {
        len = asc_prt_line(cp, leftlen,
" Host SCSI ID: %u, Host Queue Size: %u, Device Queue Size: %u\n",
            ep_38C1600->adapter_scsi_id, ep_38C1600->max_host_qng,
            ep_38C1600->max_dvc_qng);
        ASC_PRT_NEXT();
    }
    if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
    {
        word = ep_3550->termination;
    } else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800)
    {
        word = ep_38C0800->termination_lvd;
    } else
    {
        word = ep_38C1600->termination_lvd;
    }
    switch (word) {
        case 1:
            termstr = "Low Off/High Off";
            break;
        case 2:
            termstr = "Low Off/High On";
            break;
        case 3:
            termstr = "Low On/High On";
            break;
        default:
        case 0:
            termstr = "Automatic";
            break;
    }

    if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
    {
        len = asc_prt_line(cp, leftlen,
" termination: %u (%s), bios_ctrl: 0x%x\n",
            ep_3550->termination, termstr, ep_3550->bios_ctrl);
        ASC_PRT_NEXT();
    } else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800)
    {
        len = asc_prt_line(cp, leftlen,
" termination: %u (%s), bios_ctrl: 0x%x\n",
            ep_38C0800->termination_lvd, termstr, ep_38C0800->bios_ctrl);
        ASC_PRT_NEXT();
    } else
    {
        len = asc_prt_line(cp, leftlen,
" termination: %u (%s), bios_ctrl: 0x%x\n",
            ep_38C1600->termination_lvd, termstr, ep_38C1600->bios_ctrl);
        ASC_PRT_NEXT();
    }

    len = asc_prt_line(cp, leftlen,
" Target ID:           ");
    ASC_PRT_NEXT();
    for (i = 0; i <= ADV_MAX_TID; i++) {
        len = asc_prt_line(cp, leftlen, " %X", i);
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
    {
        word = ep_3550->disc_enable;
    } else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800)
    {
        word = ep_38C0800->disc_enable;
    } else
    {
        word = ep_38C1600->disc_enable;
    }
    len = asc_prt_line(cp, leftlen,
" Disconnects:         ");
    ASC_PRT_NEXT();
    for (i = 0; i <= ADV_MAX_TID; i++) {
        len = asc_prt_line(cp, leftlen, " %c",
            (word & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
    {
        word = ep_3550->tagqng_able;
    } else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800)
    {
        word = ep_38C0800->tagqng_able;
    } else
    {
        word = ep_38C1600->tagqng_able;
    }
    len = asc_prt_line(cp, leftlen,
" Command Queuing:     ");
    ASC_PRT_NEXT();
    for (i = 0; i <= ADV_MAX_TID; i++) {
        len = asc_prt_line(cp, leftlen, " %c",
            (word & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
    {
        word = ep_3550->start_motor;
    } else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800)
    {
        word = ep_38C0800->start_motor;
    } else
    {
        word = ep_38C1600->start_motor;
    }
    len = asc_prt_line(cp, leftlen,
" Start Motor:         ");
    ASC_PRT_NEXT();
    for (i = 0; i <= ADV_MAX_TID; i++) {
        len = asc_prt_line(cp, leftlen, " %c",
            (word & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
    {
        len = asc_prt_line(cp, leftlen,
" Synchronous Transfer:");
        ASC_PRT_NEXT();
        for (i = 0; i <= ADV_MAX_TID; i++) {
            len = asc_prt_line(cp, leftlen, " %c",
                (ep_3550->sdtr_able & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
            ASC_PRT_NEXT();
        }
        len = asc_prt_line(cp, leftlen, "\n");
        ASC_PRT_NEXT();
    }

    if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
    {
        len = asc_prt_line(cp, leftlen,
" Ultra Transfer:      ");
    ASC_PRT_NEXT();
        for (i = 0; i <= ADV_MAX_TID; i++) {
            len = asc_prt_line(cp, leftlen, " %c",
                (ep_3550->ultra_able & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
            ASC_PRT_NEXT();
        }
        len = asc_prt_line(cp, leftlen, "\n");
        ASC_PRT_NEXT();
    }

    if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550)
    {
        word = ep_3550->wdtr_able;
    } else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800)
    {
        word = ep_38C0800->wdtr_able;
    } else
    {
        word = ep_38C1600->wdtr_able;
    }
    len = asc_prt_line(cp, leftlen,
" Wide Transfer:       ");
    ASC_PRT_NEXT();
    for (i = 0; i <= ADV_MAX_TID; i++) {
        len = asc_prt_line(cp, leftlen, " %c",
            (word & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800 ||
        adv_dvc_varp->chip_type == ADV_CHIP_ASC38C1600)
    {
        len = asc_prt_line(cp, leftlen,
" Synchronous Transfer Speed (Mhz):\n  ");
        ASC_PRT_NEXT();
        for (i = 0; i <= ADV_MAX_TID; i++) {
            char *speed_str;

            if (i == 0)
            {
                sdtr_speed = adv_dvc_varp->sdtr_speed1;
            } else if (i == 4)
            {
                sdtr_speed = adv_dvc_varp->sdtr_speed2;
            } else if (i == 8)
            {
                sdtr_speed = adv_dvc_varp->sdtr_speed3;
            } else if (i == 12)
            {
                sdtr_speed = adv_dvc_varp->sdtr_speed4;
            }
            switch (sdtr_speed & ADV_MAX_TID)
            {
                case 0:  speed_str = "Off"; break;
                case 1:  speed_str = "  5"; break;
                case 2:  speed_str = " 10"; break;
                case 3:  speed_str = " 20"; break;
                case 4:  speed_str = " 40"; break;
                case 5:  speed_str = " 80"; break;
                default: speed_str = "Unk"; break;
            }
            len = asc_prt_line(cp, leftlen, "%X:%s ", i, speed_str);
            ASC_PRT_NEXT();
            if (i == 7)
            {
                len = asc_prt_line(cp, leftlen, "\n  ");
                ASC_PRT_NEXT();
            }
            sdtr_speed >>= 4;
        }
        len = asc_prt_line(cp, leftlen, "\n");
        ASC_PRT_NEXT();
    }

    return totlen;
}

/*
 * asc_prt_driver_conf()
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
STATIC int
asc_prt_driver_conf(struct Scsi_Host *shp, char *cp, int cplen)
{
    asc_board_t            *boardp;
    int                    leftlen;
    int                    totlen;
    int                    len;
    int                    chip_scsi_id;

    boardp = ASC_BOARDP(shp);

    leftlen = cplen;
    totlen = len = 0;

    len = asc_prt_line(cp, leftlen,
"\nLinux Driver Configuration and Information for AdvanSys SCSI Host %d:\n",
        shp->host_no);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" host_busy %u, last_reset %u, max_id %u, max_lun %u, max_channel %u\n",
        shp->host_busy, shp->last_reset, shp->max_id, shp->max_lun,
        shp->max_channel);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" unique_id %d, can_queue %d, this_id %d, sg_tablesize %u, cmd_per_lun %u\n",
        shp->unique_id, shp->can_queue, shp->this_id, shp->sg_tablesize,
        shp->cmd_per_lun);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" unchecked_isa_dma %d, use_clustering %d\n",
        shp->unchecked_isa_dma, shp->use_clustering);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" flags 0x%x, last_reset 0x%x, jiffies 0x%x, asc_n_io_port 0x%x\n",
        boardp->flags, boardp->last_reset, jiffies, boardp->asc_n_io_port);
    ASC_PRT_NEXT();

     /* 'shp->n_io_port' may be truncated because it is only one byte. */
    len = asc_prt_line(cp, leftlen,
" io_port 0x%x, n_io_port 0x%x\n",
        shp->io_port, shp->n_io_port);
    ASC_PRT_NEXT();

    if (ASC_NARROW_BOARD(boardp)) {
        chip_scsi_id = boardp->dvc_cfg.asc_dvc_cfg.chip_scsi_id;
    } else {
        chip_scsi_id = boardp->dvc_var.adv_dvc_var.chip_scsi_id;
    }

    return totlen;
}

/*
 * asc_prt_asc_board_info()
 *
 * Print dynamic board configuration information.
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
STATIC int
asc_prt_asc_board_info(struct Scsi_Host *shp, char *cp, int cplen)
{
    asc_board_t            *boardp;
    int                    chip_scsi_id;
    int                    leftlen;
    int                    totlen;
    int                    len;
    ASC_DVC_VAR            *v;
    ASC_DVC_CFG            *c;
    int                    i;
    int                    renegotiate = 0;

    boardp = ASC_BOARDP(shp);
    v = &boardp->dvc_var.asc_dvc_var;
    c = &boardp->dvc_cfg.asc_dvc_cfg;
    chip_scsi_id = c->chip_scsi_id;

    leftlen = cplen;
    totlen = len = 0;

    len = asc_prt_line(cp, leftlen,
"\nAsc Library Configuration and Statistics for AdvanSys SCSI Host %d:\n",
    shp->host_no);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" chip_version %u, lib_version 0x%x, lib_serial_no %u, mcode_date 0x%x\n",
        c->chip_version, c->lib_version, c->lib_serial_no, c->mcode_date);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" mcode_version 0x%x, err_code %u\n",
         c->mcode_version, v->err_code);
    ASC_PRT_NEXT();

    /* Current number of commands waiting for the host. */
    len = asc_prt_line(cp, leftlen,
" Total Command Pending: %d\n", v->cur_total_qng);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" Command Queuing:");
    ASC_PRT_NEXT();
    for (i = 0; i <= ASC_MAX_TID; i++) {
        if ((chip_scsi_id == i) ||
            ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
            continue;
        }
        len = asc_prt_line(cp, leftlen, " %X:%c",
            i, (v->use_tagged_qng & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    /* Current number of commands waiting for a device. */
    len = asc_prt_line(cp, leftlen,
" Command Queue Pending:");
    ASC_PRT_NEXT();
    for (i = 0; i <= ASC_MAX_TID; i++) {
        if ((chip_scsi_id == i) ||
            ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
            continue;
        }
        len = asc_prt_line(cp, leftlen, " %X:%u", i, v->cur_dvc_qng[i]);
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    /* Current limit on number of commands that can be sent to a device. */
    len = asc_prt_line(cp, leftlen,
" Command Queue Limit:");
    ASC_PRT_NEXT();
    for (i = 0; i <= ASC_MAX_TID; i++) {
        if ((chip_scsi_id == i) ||
            ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
            continue;
        }
        len = asc_prt_line(cp, leftlen, " %X:%u", i, v->max_dvc_qng[i]);
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    /* Indicate whether the device has returned queue full status. */
    len = asc_prt_line(cp, leftlen,
" Command Queue Full:");
    ASC_PRT_NEXT();
    for (i = 0; i <= ASC_MAX_TID; i++) {
        if ((chip_scsi_id == i) ||
            ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
            continue;
        }
        if (boardp->queue_full & ADV_TID_TO_TIDMASK(i)) {
            len = asc_prt_line(cp, leftlen, " %X:Y-%d",
                i, boardp->queue_full_cnt[i]);
        } else {
            len = asc_prt_line(cp, leftlen, " %X:N", i);
        }
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" Synchronous Transfer:");
    ASC_PRT_NEXT();
    for (i = 0; i <= ASC_MAX_TID; i++) {
        if ((chip_scsi_id == i) ||
            ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
            continue;
        }
        len = asc_prt_line(cp, leftlen, " %X:%c",
            i, (v->sdtr_done & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    for (i = 0; i <= ASC_MAX_TID; i++) {
        uchar syn_period_ix;

        if ((chip_scsi_id == i) ||
            ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0) ||
            ((v->init_sdtr & ADV_TID_TO_TIDMASK(i)) == 0)) {
            continue;
        }

        len = asc_prt_line(cp, leftlen, "  %X:", i);
        ASC_PRT_NEXT();

        if ((boardp->sdtr_data[i] & ASC_SYN_MAX_OFFSET) == 0)
        {
            len = asc_prt_line(cp, leftlen, " Asynchronous");
            ASC_PRT_NEXT();
        } else
        {
            syn_period_ix =
                (boardp->sdtr_data[i] >> 4) & (v->max_sdtr_index - 1);

            len = asc_prt_line(cp, leftlen,
                " Transfer Period Factor: %d (%d.%d Mhz),",
                v->sdtr_period_tbl[syn_period_ix],
                250 / v->sdtr_period_tbl[syn_period_ix],
                ASC_TENTHS(250, v->sdtr_period_tbl[syn_period_ix]));
            ASC_PRT_NEXT();

            len = asc_prt_line(cp, leftlen, " REQ/ACK Offset: %d",
                boardp->sdtr_data[i] & ASC_SYN_MAX_OFFSET);
            ASC_PRT_NEXT();
        }

        if ((v->sdtr_done & ADV_TID_TO_TIDMASK(i)) == 0) {
            len = asc_prt_line(cp, leftlen, "*\n");
            renegotiate = 1;
        } else
        {
            len = asc_prt_line(cp, leftlen, "\n");
        }
        ASC_PRT_NEXT();
    }

    if (renegotiate)
    {
        len = asc_prt_line(cp, leftlen,
            " * = Re-negotiation pending before next command.\n");
        ASC_PRT_NEXT();
    }

    return totlen;
}

/*
 * asc_prt_adv_board_info()
 *
 * Print dynamic board configuration information.
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
STATIC int
asc_prt_adv_board_info(struct Scsi_Host *shp, char *cp, int cplen)
{
    asc_board_t            *boardp;
    int                    leftlen;
    int                    totlen;
    int                    len;
    int                    i;
    ADV_DVC_VAR            *v;
    ADV_DVC_CFG            *c;
    AdvPortAddr            iop_base;
    ushort                 chip_scsi_id;
    ushort                 lramword;
    uchar                  lrambyte;
    ushort                 tagqng_able;
    ushort                 sdtr_able, wdtr_able;
    ushort                 wdtr_done, sdtr_done;
    ushort                 period = 0;
    int                    renegotiate = 0;

    boardp = ASC_BOARDP(shp);
    v = &boardp->dvc_var.adv_dvc_var;
    c = &boardp->dvc_cfg.adv_dvc_cfg;
    iop_base = v->iop_base;
    chip_scsi_id = v->chip_scsi_id;

    leftlen = cplen;
    totlen = len = 0;

    len = asc_prt_line(cp, leftlen,
"\nAdv Library Configuration and Statistics for AdvanSys SCSI Host %d:\n",
    shp->host_no);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" iop_base 0x%lx, cable_detect: %X, err_code %u\n",
         v->iop_base,
         AdvReadWordRegister(iop_base, IOPW_SCSI_CFG1) & CABLE_DETECT,
         v->err_code);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" chip_version %u, lib_version 0x%x, mcode_date 0x%x, mcode_version 0x%x\n",
        c->chip_version, c->lib_version, c->mcode_date, c->mcode_version);
    ASC_PRT_NEXT();

    AdvReadWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
    len = asc_prt_line(cp, leftlen,
" Queuing Enabled:");
    ASC_PRT_NEXT();
    for (i = 0; i <= ADV_MAX_TID; i++) {
        if ((chip_scsi_id == i) ||
            ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
            continue;
        }

        len = asc_prt_line(cp, leftlen, " %X:%c",
            i, (tagqng_able & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" Queue Limit:");
    ASC_PRT_NEXT();
    for (i = 0; i <= ADV_MAX_TID; i++) {
        if ((chip_scsi_id == i) ||
            ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
            continue;
        }

        AdvReadByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + i, lrambyte);

        len = asc_prt_line(cp, leftlen, " %X:%d", i, lrambyte);
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" Command Pending:");
    ASC_PRT_NEXT();
    for (i = 0; i <= ADV_MAX_TID; i++) {
        if ((chip_scsi_id == i) ||
            ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
            continue;
        }

        AdvReadByteLram(iop_base, ASC_MC_NUMBER_OF_QUEUED_CMD + i, lrambyte);

        len = asc_prt_line(cp, leftlen, " %X:%d", i, lrambyte);
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    AdvReadWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
    len = asc_prt_line(cp, leftlen,
" Wide Enabled:");
    ASC_PRT_NEXT();
    for (i = 0; i <= ADV_MAX_TID; i++) {
        if ((chip_scsi_id == i) ||
            ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
            continue;
        }

        len = asc_prt_line(cp, leftlen, " %X:%c",
            i, (wdtr_able & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    AdvReadWordLram(iop_base, ASC_MC_WDTR_DONE, wdtr_done);
    len = asc_prt_line(cp, leftlen,
" Transfer Bit Width:");
    ASC_PRT_NEXT();
    for (i = 0; i <= ADV_MAX_TID; i++) {
        if ((chip_scsi_id == i) ||
            ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
            continue;
        }

        AdvReadWordLram(iop_base, ASC_MC_DEVICE_HSHK_CFG_TABLE + (2 * i),
            lramword);

        len = asc_prt_line(cp, leftlen, " %X:%d",
            i, (lramword & 0x8000) ? 16 : 8);
        ASC_PRT_NEXT();

        if ((wdtr_able & ADV_TID_TO_TIDMASK(i)) &&
            (wdtr_done & ADV_TID_TO_TIDMASK(i)) == 0) {
            len = asc_prt_line(cp, leftlen, "*");
            ASC_PRT_NEXT();
            renegotiate = 1;
        }
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    AdvReadWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
    len = asc_prt_line(cp, leftlen,
" Synchronous Enabled:");
    ASC_PRT_NEXT();
    for (i = 0; i <= ADV_MAX_TID; i++) {
        if ((chip_scsi_id == i) ||
            ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
            continue;
        }

        len = asc_prt_line(cp, leftlen, " %X:%c",
            i, (sdtr_able & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
        ASC_PRT_NEXT();
    }
    len = asc_prt_line(cp, leftlen, "\n");
    ASC_PRT_NEXT();

    AdvReadWordLram(iop_base, ASC_MC_SDTR_DONE, sdtr_done);
    for (i = 0; i <= ADV_MAX_TID; i++) {

        AdvReadWordLram(iop_base, ASC_MC_DEVICE_HSHK_CFG_TABLE + (2 * i),
            lramword);
        lramword &= ~0x8000;

        if ((chip_scsi_id == i) ||
            ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0) ||
            ((sdtr_able & ADV_TID_TO_TIDMASK(i)) == 0)) {
            continue;
        }

        len = asc_prt_line(cp, leftlen, "  %X:", i);
        ASC_PRT_NEXT();

        if ((lramword & 0x1F) == 0) /* Check for REQ/ACK Offset 0. */
        {
            len = asc_prt_line(cp, leftlen, " Asynchronous");
            ASC_PRT_NEXT();
        } else
        {
            len = asc_prt_line(cp, leftlen, " Transfer Period Factor: ");
            ASC_PRT_NEXT();

            if ((lramword & 0x1F00) == 0x1100) /* 80 Mhz */
            {
                len = asc_prt_line(cp, leftlen, "9 (80.0 Mhz),");
                ASC_PRT_NEXT();
            } else if ((lramword & 0x1F00) == 0x1000) /* 40 Mhz */
            {
                len = asc_prt_line(cp, leftlen, "10 (40.0 Mhz),");
                ASC_PRT_NEXT();
            } else /* 20 Mhz or below. */
            {
                period = (((lramword >> 8) * 25) + 50)/4;

                if (period == 0) /* Should never happen. */
                {
                    len = asc_prt_line(cp, leftlen, "%d (? Mhz), ");
                    ASC_PRT_NEXT();
                } else
                {
                    len = asc_prt_line(cp, leftlen,
                        "%d (%d.%d Mhz),",
                        period, 250/period, ASC_TENTHS(250, period));
                    ASC_PRT_NEXT();
                }
            }

            len = asc_prt_line(cp, leftlen, " REQ/ACK Offset: %d",
                lramword & 0x1F);
            ASC_PRT_NEXT();
        }

        if ((sdtr_done & ADV_TID_TO_TIDMASK(i)) == 0) {
            len = asc_prt_line(cp, leftlen, "*\n");
            renegotiate = 1;
        } else
        {
            len = asc_prt_line(cp, leftlen, "\n");
        }
        ASC_PRT_NEXT();
    }

    if (renegotiate)
    {
        len = asc_prt_line(cp, leftlen,
            " * = Re-negotiation pending before next command.\n");
        ASC_PRT_NEXT();
    }

    return totlen;
}

/*
 * asc_proc_copy()
 *
 * Copy proc information to a read buffer taking into account the current
 * read offset in the file and the remaining space in the read buffer.
 */
STATIC int
asc_proc_copy(off_t advoffset, off_t offset, char *curbuf, int leftlen,
              char *cp, int cplen)
{
    int cnt = 0;

    ASC_DBG3(2, "asc_proc_copy: offset %d, advoffset %d, cplen %d\n",
            (unsigned) offset, (unsigned) advoffset, cplen);
    if (offset <= advoffset) {
        /* Read offset below current offset, copy everything. */
        cnt = min(cplen, leftlen);
        ASC_DBG3(2, "asc_proc_copy: curbuf 0x%lx, cp 0x%lx, cnt %d\n",
                (ulong) curbuf, (ulong) cp, cnt);
        memcpy(curbuf, cp, cnt);
    } else if (offset < advoffset + cplen) {
        /* Read offset within current range, partial copy. */
        cnt = (advoffset + cplen) - offset;
        cp = (cp + cplen) - cnt;
        cnt = min(cnt, leftlen);
        ASC_DBG3(2, "asc_proc_copy: curbuf 0x%lx, cp 0x%lx, cnt %d\n",
                (ulong) curbuf, (ulong) cp, cnt);
        memcpy(curbuf, cp, cnt);
    }
    return cnt;
}

/*
 * asc_prt_line()
 *
 * If 'cp' is NULL print to the console, otherwise print to a buffer.
 *
 * Return 0 if printing to the console, otherwise return the number of
 * bytes written to the buffer.
 *
 * Note: If any single line is greater than ASC_PRTLINE_SIZE bytes the stack
 * will be corrupted. 's[]' is defined to be ASC_PRTLINE_SIZE bytes.
 */
STATIC int
asc_prt_line(char *buf, int buflen, char *fmt, ...)
{
    va_list        args;
    int            ret;
    char           s[ASC_PRTLINE_SIZE];

    va_start(args, fmt);
    ret = vsprintf(s, fmt, args);
    ASC_ASSERT(ret < ASC_PRTLINE_SIZE);
    if (buf == NULL) {
        (void) printk(s);
        ret = 0;
    } else {
        ret = min(buflen, ret);
        memcpy(buf, s, ret);
    }
    va_end(args);
    return ret;
}
#endif /* CONFIG_PROC_FS */


/*
 * --- Functions Required by the Asc Library
 */

/*
 * Delay for 'n' milliseconds. Don't use the 'jiffies'
 * global variable which is incremented once every 5 ms
 * from a timer interrupt, because this function may be
 * called when interrupts are disabled.
 */
STATIC void
DvcSleepMilliSecond(ADV_DCNT n)
{
    ASC_DBG1(4, "DvcSleepMilliSecond: %lu\n", (ulong) n);
    mdelay(n);
}

/*
 * Currently and inline noop but leave as a placeholder.
 * Leave DvcEnterCritical() as a noop placeholder.
 */
STATIC inline ulong
DvcEnterCritical(void)
{
    return 0;
}

/*
 * Critical sections are all protected by the board spinlock.
 * Leave DvcLeaveCritical() as a noop placeholder.
 */
STATIC inline void
DvcLeaveCritical(ulong flags)
{
    return;
}

/*
 * void
 * DvcPutScsiQ(PortAddr iop_base, ushort s_addr, uchar *outbuf, int words)
 *
 * Calling/Exit State:
 *    none
 *
 * Description:
 *     Output an ASC_SCSI_Q structure to the chip
 */
STATIC void
DvcPutScsiQ(PortAddr iop_base, ushort s_addr, uchar *outbuf, int words)
{
    int    i;

    ASC_DBG_PRT_HEX(2, "DvcPutScsiQ", outbuf, 2 * words);
    AscSetChipLramAddr(iop_base, s_addr);
    for (i = 0; i < 2 * words; i += 2) {
        if (i == 4 || i == 20) {
            continue;
        }
        outpw(iop_base + IOP_RAM_DATA,
            ((ushort) outbuf[i + 1] << 8) | outbuf[i]);
    }
}

/*
 * void
 * DvcGetQinfo(PortAddr iop_base, ushort s_addr, uchar *inbuf, int words)
 *
 * Calling/Exit State:
 *    none
 *
 * Description:
 *     Input an ASC_QDONE_INFO structure from the chip
 */
STATIC void
DvcGetQinfo(PortAddr iop_base, ushort s_addr, uchar *inbuf, int words)
{
    int    i;
    ushort word;

    AscSetChipLramAddr(iop_base, s_addr);
    for (i = 0; i < 2 * words; i += 2) {
        if (i == 10) {
            continue;
        }
        word = inpw(iop_base + IOP_RAM_DATA);
        inbuf[i] = word & 0xff;
        inbuf[i + 1] = (word >> 8) & 0xff;
    }
    ASC_DBG_PRT_HEX(2, "DvcGetQinfo", inbuf, 2 * words);
}

/*
 * Read a PCI configuration byte.
 */
STATIC uchar __init
DvcReadPCIConfigByte(
        ASC_DVC_VAR *asc_dvc,
        ushort offset)
{
#ifdef CONFIG_PCI
    uchar byte_data;
    pci_read_config_byte(to_pci_dev(asc_dvc->cfg->dev), offset, &byte_data);
    return byte_data;
#else /* !defined(CONFIG_PCI) */
    return 0;
#endif /* !defined(CONFIG_PCI) */
}

/*
 * Write a PCI configuration byte.
 */
STATIC void __init
DvcWritePCIConfigByte(
        ASC_DVC_VAR *asc_dvc,
        ushort offset,
        uchar  byte_data)
{
#ifdef CONFIG_PCI
    pci_write_config_byte(to_pci_dev(asc_dvc->cfg->dev), offset, byte_data);
#endif /* CONFIG_PCI */
}

/*
 * Return the BIOS address of the adapter at the specified
 * I/O port and with the specified bus type.
 */
STATIC ushort __init
AscGetChipBiosAddress(
        PortAddr iop_base,
        ushort bus_type)
{
    ushort  cfg_lsw;
    ushort  bios_addr;

    /*
     * The PCI BIOS is re-located by the motherboard BIOS. Because
     * of this the driver can not determine where a PCI BIOS is
     * loaded and executes.
     */
    if (bus_type & ASC_IS_PCI)
    {
        return(0);
    }

#ifdef CONFIG_ISA
    if((bus_type & ASC_IS_EISA) != 0)
    {
        cfg_lsw = AscGetEisaChipCfg(iop_base);
        cfg_lsw &= 0x000F;
        bios_addr = (ushort)(ASC_BIOS_MIN_ADDR  +
                                (cfg_lsw * ASC_BIOS_BANK_SIZE));
        return(bios_addr);
    }/* if */
#endif /* CONFIG_ISA */

    cfg_lsw = AscGetChipCfgLsw(iop_base);

    /*
    *  ISA PnP uses the top bit as the 32K BIOS flag
    */
    if (bus_type == ASC_IS_ISAPNP)
    {
        cfg_lsw &= 0x7FFF;
    }/* if */

    bios_addr = (ushort)(((cfg_lsw >> 12) * ASC_BIOS_BANK_SIZE) +
            ASC_BIOS_MIN_ADDR);
    return(bios_addr);
}


/*
 * --- Functions Required by the Adv Library
 */

/*
 * DvcGetPhyAddr()
 *
 * Return the physical address of 'vaddr' and set '*lenp' to the
 * number of physically contiguous bytes that follow 'vaddr'.
 * 'flag' indicates the type of structure whose physical address
 * is being translated.
 *
 * Note: Because Linux currently doesn't page the kernel and all
 * kernel buffers are physically contiguous, leave '*lenp' unchanged.
 */
ADV_PADDR
DvcGetPhyAddr(ADV_DVC_VAR *asc_dvc, ADV_SCSI_REQ_Q *scsiq,
        uchar *vaddr, ADV_SDCNT *lenp, int flag)
{
    ADV_PADDR           paddr;

    paddr = virt_to_bus(vaddr);

    ASC_DBG4(4,
        "DvcGetPhyAddr: vaddr 0x%lx, lenp 0x%lx *lenp %lu, paddr 0x%lx\n",
        (ulong) vaddr, (ulong) lenp, (ulong) *((ulong *) lenp), (ulong) paddr);

    return paddr;
}

/*
 * Read a PCI configuration byte.
 */
STATIC uchar __init
DvcAdvReadPCIConfigByte(
        ADV_DVC_VAR *asc_dvc,
        ushort offset)
{
#ifdef CONFIG_PCI
    uchar byte_data;
    pci_read_config_byte(to_pci_dev(asc_dvc->cfg->dev), offset, &byte_data);
    return byte_data;
#else /* CONFIG_PCI */
    return 0;
#endif /* CONFIG_PCI */
}

/*
 * Write a PCI configuration byte.
 */
STATIC void __init
DvcAdvWritePCIConfigByte(
        ADV_DVC_VAR *asc_dvc,
        ushort offset,
        uchar  byte_data)
{
#ifdef CONFIG_PCI
    pci_write_config_byte(to_pci_dev(asc_dvc->cfg->dev), offset, byte_data);
#else /* CONFIG_PCI */
    return;
#endif /* CONFIG_PCI */
}

/*
 * --- Tracing and Debugging Functions
 */

#ifdef ADVANSYS_STATS
#ifdef CONFIG_PROC_FS
/*
 * asc_prt_board_stats()
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
STATIC int
asc_prt_board_stats(struct Scsi_Host *shp, char *cp, int cplen)
{
    int                    leftlen;
    int                    totlen;
    int                    len;
    struct asc_stats       *s;
    asc_board_t            *boardp;

    leftlen = cplen;
    totlen = len = 0;

    boardp = ASC_BOARDP(shp);
    s = &boardp->asc_stats;

    len = asc_prt_line(cp, leftlen,
"\nLinux Driver Statistics for AdvanSys SCSI Host %d:\n", shp->host_no);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" queuecommand %lu, reset %lu, biosparam %lu, interrupt %lu\n",
        s->queuecommand, s->reset, s->biosparam, s->interrupt);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" callback %lu, done %lu, build_error %lu, build_noreq %lu, build_nosg %lu\n",
        s->callback, s->done, s->build_error, s->adv_build_noreq,
        s->adv_build_nosg);
    ASC_PRT_NEXT();

    len = asc_prt_line(cp, leftlen,
" exe_noerror %lu, exe_busy %lu, exe_error %lu, exe_unknown %lu\n",
        s->exe_noerror, s->exe_busy, s->exe_error, s->exe_unknown);
    ASC_PRT_NEXT();

    /*
     * Display data transfer statistics.
     */
    if (s->cont_cnt > 0) {
        len = asc_prt_line(cp, leftlen, " cont_cnt %lu, ", s->cont_cnt);
        ASC_PRT_NEXT();

        len = asc_prt_line(cp, leftlen, "cont_xfer %lu.%01lu kb ",
                    s->cont_xfer/2,
                    ASC_TENTHS(s->cont_xfer, 2));
        ASC_PRT_NEXT();

        /* Contiguous transfer average size */
        len = asc_prt_line(cp, leftlen, "avg_xfer %lu.%01lu kb\n",
                    (s->cont_xfer/2)/s->cont_cnt,
                    ASC_TENTHS((s->cont_xfer/2), s->cont_cnt));
        ASC_PRT_NEXT();
    }

    if (s->sg_cnt > 0) {

        len = asc_prt_line(cp, leftlen, " sg_cnt %lu, sg_elem %lu, ",
                    s->sg_cnt, s->sg_elem);
        ASC_PRT_NEXT();

        len = asc_prt_line(cp, leftlen, "sg_xfer %lu.%01lu kb\n",
                    s->sg_xfer/2,
                    ASC_TENTHS(s->sg_xfer, 2));
        ASC_PRT_NEXT();

        /* Scatter gather transfer statistics */
        len = asc_prt_line(cp, leftlen, " avg_num_elem %lu.%01lu, ",
                    s->sg_elem/s->sg_cnt,
                    ASC_TENTHS(s->sg_elem, s->sg_cnt));
        ASC_PRT_NEXT();

        len = asc_prt_line(cp, leftlen, "avg_elem_size %lu.%01lu kb, ",
                    (s->sg_xfer/2)/s->sg_elem,
                    ASC_TENTHS((s->sg_xfer/2), s->sg_elem));
        ASC_PRT_NEXT();

        len = asc_prt_line(cp, leftlen, "avg_xfer_size %lu.%01lu kb\n",
                    (s->sg_xfer/2)/s->sg_cnt,
                    ASC_TENTHS((s->sg_xfer/2), s->sg_cnt));
        ASC_PRT_NEXT();
    }

    /*
     * Display request queuing statistics.
     */
    len = asc_prt_line(cp, leftlen,
" Active and Waiting Request Queues (Time Unit: %d HZ):\n", HZ);
    ASC_PRT_NEXT();


     return totlen;
}

/*
 * asc_prt_target_stats()
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * This is separated from asc_prt_board_stats because a full set
 * of targets will overflow ASC_PRTBUF_SIZE.
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
STATIC int
asc_prt_target_stats(struct Scsi_Host *shp, int tgt_id, char *cp, int cplen)
{
    int                    leftlen;
    int                    totlen;
    int                    len;
    struct asc_stats       *s;
    ushort                 chip_scsi_id;
    asc_board_t            *boardp;
    asc_queue_t            *active;
    asc_queue_t            *waiting;

    leftlen = cplen;
    totlen = len = 0;

    boardp = ASC_BOARDP(shp);
    s = &boardp->asc_stats;

    active = &ASC_BOARDP(shp)->active;
    waiting = &ASC_BOARDP(shp)->waiting;

    if (ASC_NARROW_BOARD(boardp)) {
        chip_scsi_id = boardp->dvc_cfg.asc_dvc_cfg.chip_scsi_id;
    } else {
        chip_scsi_id = boardp->dvc_var.adv_dvc_var.chip_scsi_id;
    }

    if ((chip_scsi_id == tgt_id) ||
        ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(tgt_id)) == 0)) {
        return 0;
    }

    do {
        if (active->q_tot_cnt[tgt_id] > 0 || waiting->q_tot_cnt[tgt_id] > 0) {
            len = asc_prt_line(cp, leftlen, " target %d\n", tgt_id);
            ASC_PRT_NEXT();

            len = asc_prt_line(cp, leftlen,
"   active: cnt [cur %d, max %d, tot %u], time [min %d, max %d, avg %lu.%01lu]\n",
                active->q_cur_cnt[tgt_id], active->q_max_cnt[tgt_id],
                active->q_tot_cnt[tgt_id],
                active->q_min_tim[tgt_id], active->q_max_tim[tgt_id],
                (active->q_tot_cnt[tgt_id] == 0) ? 0 :
                (active->q_tot_tim[tgt_id]/active->q_tot_cnt[tgt_id]),
                (active->q_tot_cnt[tgt_id] == 0) ? 0 :
                ASC_TENTHS(active->q_tot_tim[tgt_id],
                active->q_tot_cnt[tgt_id]));
             ASC_PRT_NEXT();

             len = asc_prt_line(cp, leftlen,
"   waiting: cnt [cur %d, max %d, tot %u], time [min %u, max %u, avg %lu.%01lu]\n",
                waiting->q_cur_cnt[tgt_id], waiting->q_max_cnt[tgt_id],
                waiting->q_tot_cnt[tgt_id],
                waiting->q_min_tim[tgt_id], waiting->q_max_tim[tgt_id],
                (waiting->q_tot_cnt[tgt_id] == 0) ? 0 :
                (waiting->q_tot_tim[tgt_id]/waiting->q_tot_cnt[tgt_id]),
                (waiting->q_tot_cnt[tgt_id] == 0) ? 0 :
                ASC_TENTHS(waiting->q_tot_tim[tgt_id],
                waiting->q_tot_cnt[tgt_id]));
             ASC_PRT_NEXT();
        }
    } while (0);

     return totlen;
}
#endif /* CONFIG_PROC_FS */
#endif /* ADVANSYS_STATS */

#ifdef ADVANSYS_DEBUG
/*
 * asc_prt_scsi_host()
 */
STATIC void
asc_prt_scsi_host(struct Scsi_Host *s)
{
    asc_board_t         *boardp;

    boardp = ASC_BOARDP(s);

    printk("Scsi_Host at addr 0x%lx\n", (ulong) s);
    printk(
" host_busy %u, host_no %d, last_reset %d,\n",
        s->host_busy, s->host_no,
        (unsigned) s->last_reset);

    printk(
" base 0x%lx, io_port 0x%lx, n_io_port %u, irq 0x%x,\n",
        (ulong) s->base, (ulong) s->io_port, s->n_io_port, s->irq);

    printk(
" dma_channel %d, this_id %d, can_queue %d,\n",
        s->dma_channel, s->this_id, s->can_queue);

    printk(
" cmd_per_lun %d, sg_tablesize %d, unchecked_isa_dma %d\n",
        s->cmd_per_lun, s->sg_tablesize, s->unchecked_isa_dma);

    if (ASC_NARROW_BOARD(boardp)) {
        asc_prt_asc_dvc_var(&ASC_BOARDP(s)->dvc_var.asc_dvc_var);
        asc_prt_asc_dvc_cfg(&ASC_BOARDP(s)->dvc_cfg.asc_dvc_cfg);
    } else {
        asc_prt_adv_dvc_var(&ASC_BOARDP(s)->dvc_var.adv_dvc_var);
        asc_prt_adv_dvc_cfg(&ASC_BOARDP(s)->dvc_cfg.adv_dvc_cfg);
    }
}

/*
 * asc_prt_scsi_cmnd()
 */
STATIC void
asc_prt_scsi_cmnd(struct scsi_cmnd *s)
{
    printk("struct scsi_cmnd at addr 0x%lx\n", (ulong) s);

    printk(
" host 0x%lx, device 0x%lx, target %u, lun %u, channel %u,\n",
        (ulong) s->device->host, (ulong) s->device, s->device->id, s->device->lun,
        s->device->channel);

    asc_prt_hex(" CDB", s->cmnd, s->cmd_len);

    printk (
"sc_data_direction %u, resid %d\n",
        s->sc_data_direction, s->resid);

    printk(
" use_sg %u, sglist_len %u\n",
        s->use_sg, s->sglist_len);

    printk(
" serial_number 0x%x, retries %d, allowed %d\n",
        (unsigned) s->serial_number, s->retries, s->allowed);

    printk(
" timeout_per_command %d\n",
        s->timeout_per_command);

    printk(
" scsi_done 0x%lx, done 0x%lx, host_scribble 0x%lx, result 0x%x\n",
        (ulong) s->scsi_done, (ulong) s->done,
        (ulong) s->host_scribble, s->result);

    printk(
" tag %u, pid %u\n",
        (unsigned) s->tag, (unsigned) s->pid);
}

/*
 * asc_prt_asc_dvc_var()
 */
STATIC void
asc_prt_asc_dvc_var(ASC_DVC_VAR *h)
{
    printk("ASC_DVC_VAR at addr 0x%lx\n", (ulong) h);

    printk(
" iop_base 0x%x, err_code 0x%x, dvc_cntl 0x%x, bug_fix_cntl %d,\n",
        h->iop_base, h->err_code, h->dvc_cntl, h->bug_fix_cntl);

    printk(
" bus_type %d, isr_callback 0x%lx, exe_callback 0x%lx, init_sdtr 0x%x,\n",
        h->bus_type, (ulong) h->isr_callback, (ulong) h->exe_callback,
        (unsigned) h->init_sdtr);

    printk(
" sdtr_done 0x%x, use_tagged_qng 0x%x, unit_not_ready 0x%x, chip_no 0x%x,\n",
        (unsigned) h->sdtr_done, (unsigned) h->use_tagged_qng,
        (unsigned) h->unit_not_ready, (unsigned) h->chip_no);

    printk(
" queue_full_or_busy 0x%x, start_motor 0x%x, scsi_reset_wait %u,\n",
        (unsigned) h->queue_full_or_busy, (unsigned) h->start_motor,
        (unsigned) h->scsi_reset_wait);

    printk(
" is_in_int %u, max_total_qng %u, cur_total_qng %u, in_critical_cnt %u,\n",
        (unsigned) h->is_in_int, (unsigned) h->max_total_qng,
        (unsigned) h->cur_total_qng, (unsigned) h->in_critical_cnt);

    printk(
" last_q_shortage %u, init_state 0x%x, no_scam 0x%x, pci_fix_asyn_xfer 0x%x,\n",
        (unsigned) h->last_q_shortage, (unsigned) h->init_state,
        (unsigned) h->no_scam, (unsigned) h->pci_fix_asyn_xfer);

    printk(
" cfg 0x%lx, irq_no 0x%x\n",
        (ulong) h->cfg, (unsigned) h->irq_no);
}

/*
 * asc_prt_asc_dvc_cfg()
 */
STATIC void
asc_prt_asc_dvc_cfg(ASC_DVC_CFG *h)
{
    printk("ASC_DVC_CFG at addr 0x%lx\n", (ulong) h);

    printk(
" can_tagged_qng 0x%x, cmd_qng_enabled 0x%x,\n",
            h->can_tagged_qng, h->cmd_qng_enabled);
    printk(
" disc_enable 0x%x, sdtr_enable 0x%x,\n",
            h->disc_enable, h->sdtr_enable);

    printk(
" chip_scsi_id %d, isa_dma_speed %d, isa_dma_channel %d, chip_version %d,\n",
             h->chip_scsi_id, h->isa_dma_speed, h->isa_dma_channel,
             h->chip_version);

    printk(
" pci_device_id %d, lib_serial_no %u, lib_version %u, mcode_date 0x%x,\n",
	   to_pci_dev(h->dev)->device, h->lib_serial_no, h->lib_version,
	   h->mcode_date);

    printk(
" mcode_version %d, overrun_buf 0x%lx\n",
            h->mcode_version, (ulong) h->overrun_buf);
}

/*
 * asc_prt_asc_scsi_q()
 */
STATIC void
asc_prt_asc_scsi_q(ASC_SCSI_Q *q)
{
    ASC_SG_HEAD    *sgp;
    int i;

    printk("ASC_SCSI_Q at addr 0x%lx\n", (ulong) q);

    printk(
" target_ix 0x%x, target_lun %u, srb_ptr 0x%lx, tag_code 0x%x,\n",
            q->q2.target_ix, q->q1.target_lun,
            (ulong) q->q2.srb_ptr, q->q2.tag_code);

    printk(
" data_addr 0x%lx, data_cnt %lu, sense_addr 0x%lx, sense_len %u,\n",
            (ulong) le32_to_cpu(q->q1.data_addr),
            (ulong) le32_to_cpu(q->q1.data_cnt),
            (ulong) le32_to_cpu(q->q1.sense_addr), q->q1.sense_len);

    printk(
" cdbptr 0x%lx, cdb_len %u, sg_head 0x%lx, sg_queue_cnt %u\n",
            (ulong) q->cdbptr, q->q2.cdb_len,
            (ulong) q->sg_head, q->q1.sg_queue_cnt);

    if (q->sg_head) {
        sgp = q->sg_head;
        printk("ASC_SG_HEAD at addr 0x%lx\n", (ulong) sgp);
        printk(" entry_cnt %u, queue_cnt %u\n", sgp->entry_cnt, sgp->queue_cnt);
        for (i = 0; i < sgp->entry_cnt; i++) {
            printk(" [%u]: addr 0x%lx, bytes %lu\n",
                i, (ulong) le32_to_cpu(sgp->sg_list[i].addr),
                (ulong) le32_to_cpu(sgp->sg_list[i].bytes));
        }

    }
}

/*
 * asc_prt_asc_qdone_info()
 */
STATIC void
asc_prt_asc_qdone_info(ASC_QDONE_INFO *q)
{
    printk("ASC_QDONE_INFO at addr 0x%lx\n", (ulong) q);
    printk(
" srb_ptr 0x%lx, target_ix %u, cdb_len %u, tag_code %u,\n",
            (ulong) q->d2.srb_ptr, q->d2.target_ix, q->d2.cdb_len,
            q->d2.tag_code);
    printk(
" done_stat 0x%x, host_stat 0x%x, scsi_stat 0x%x, scsi_msg 0x%x\n",
            q->d3.done_stat, q->d3.host_stat, q->d3.scsi_stat, q->d3.scsi_msg);
}

/*
 * asc_prt_adv_dvc_var()
 *
 * Display an ADV_DVC_VAR structure.
 */
STATIC void
asc_prt_adv_dvc_var(ADV_DVC_VAR *h)
{
    printk(" ADV_DVC_VAR at addr 0x%lx\n", (ulong) h);

    printk(
"  iop_base 0x%lx, err_code 0x%x, ultra_able 0x%x\n",
        (ulong) h->iop_base, h->err_code, (unsigned) h->ultra_able);

    printk(
"  isr_callback 0x%lx, sdtr_able 0x%x, wdtr_able 0x%x\n",
        (ulong) h->isr_callback, (unsigned) h->sdtr_able,
        (unsigned) h->wdtr_able);

    printk(
"  start_motor 0x%x, scsi_reset_wait 0x%x, irq_no 0x%x,\n",
        (unsigned) h->start_motor,
        (unsigned) h->scsi_reset_wait, (unsigned) h->irq_no);

    printk(
"  max_host_qng %u, max_dvc_qng %u, carr_freelist 0x%lxn\n",
        (unsigned) h->max_host_qng, (unsigned) h->max_dvc_qng,
        (ulong) h->carr_freelist);

    printk(
"  icq_sp 0x%lx, irq_sp 0x%lx\n",
        (ulong) h->icq_sp, (ulong) h->irq_sp);

    printk(
"  no_scam 0x%x, tagqng_able 0x%x\n",
        (unsigned) h->no_scam, (unsigned) h->tagqng_able);

    printk(
"  chip_scsi_id 0x%x, cfg 0x%lx\n",
        (unsigned) h->chip_scsi_id, (ulong) h->cfg);
}

/*
 * asc_prt_adv_dvc_cfg()
 *
 * Display an ADV_DVC_CFG structure.
 */
STATIC void
asc_prt_adv_dvc_cfg(ADV_DVC_CFG *h)
{
    printk(" ADV_DVC_CFG at addr 0x%lx\n", (ulong) h);

    printk(
"  disc_enable 0x%x, termination 0x%x\n",
        h->disc_enable, h->termination);

    printk(
"  chip_version 0x%x, mcode_date 0x%x\n",
        h->chip_version, h->mcode_date);

    printk(
"  mcode_version 0x%x, pci_device_id 0x%x, lib_version %u\n",
       h->mcode_version, to_pci_dev(h->dev)->device, h->lib_version);

    printk(
"  control_flag 0x%x, pci_slot_info 0x%x\n",
       h->control_flag, h->pci_slot_info);
}

/*
 * asc_prt_adv_scsi_req_q()
 *
 * Display an ADV_SCSI_REQ_Q structure.
 */
STATIC void
asc_prt_adv_scsi_req_q(ADV_SCSI_REQ_Q *q)
{
    int                 sg_blk_cnt;
    struct asc_sg_block *sg_ptr;

    printk("ADV_SCSI_REQ_Q at addr 0x%lx\n", (ulong) q);

    printk(
"  target_id %u, target_lun %u, srb_ptr 0x%lx, a_flag 0x%x\n",
            q->target_id, q->target_lun, (ulong) q->srb_ptr, q->a_flag);

    printk("  cntl 0x%x, data_addr 0x%lx, vdata_addr 0x%lx\n",
            q->cntl, (ulong) le32_to_cpu(q->data_addr), (ulong) q->vdata_addr);

    printk(
"  data_cnt %lu, sense_addr 0x%lx, sense_len %u,\n",
            (ulong) le32_to_cpu(q->data_cnt),
            (ulong) le32_to_cpu(q->sense_addr), q->sense_len);

    printk(
"  cdb_len %u, done_status 0x%x, host_status 0x%x, scsi_status 0x%x\n",
            q->cdb_len, q->done_status, q->host_status, q->scsi_status);

    printk(
"  sg_working_ix 0x%x, target_cmd %u\n",
            q->sg_working_ix, q->target_cmd);

    printk(
"  scsiq_rptr 0x%lx, sg_real_addr 0x%lx, sg_list_ptr 0x%lx\n",
            (ulong) le32_to_cpu(q->scsiq_rptr),
            (ulong) le32_to_cpu(q->sg_real_addr), (ulong) q->sg_list_ptr);

    /* Display the request's ADV_SG_BLOCK structures. */
    if (q->sg_list_ptr != NULL)
    {
        sg_blk_cnt = 0;
        while (1) {
            /*
             * 'sg_ptr' is a physical address. Convert it to a virtual
             * address by indexing 'sg_blk_cnt' into the virtual address
             * array 'sg_list_ptr'.
             *
             * XXX - Assumes all SG physical blocks are virtually contiguous.
             */
            sg_ptr = &(((ADV_SG_BLOCK *) (q->sg_list_ptr))[sg_blk_cnt]);
            asc_prt_adv_sgblock(sg_blk_cnt, sg_ptr);
            if (sg_ptr->sg_ptr == 0)
            {
                break;
            }
            sg_blk_cnt++;
        }
    }
}

/*
 * asc_prt_adv_sgblock()
 *
 * Display an ADV_SG_BLOCK structure.
 */
STATIC void
asc_prt_adv_sgblock(int sgblockno, ADV_SG_BLOCK *b)
{
    int i;

    printk(" ASC_SG_BLOCK at addr 0x%lx (sgblockno %d)\n",
        (ulong) b, sgblockno);
    printk("  sg_cnt %u, sg_ptr 0x%lx\n",
        b->sg_cnt, (ulong) le32_to_cpu(b->sg_ptr));
    ASC_ASSERT(b->sg_cnt <= NO_OF_SG_PER_BLOCK);
    if (b->sg_ptr != 0)
    {
        ASC_ASSERT(b->sg_cnt == NO_OF_SG_PER_BLOCK);
    }
    for (i = 0; i < b->sg_cnt; i++) {
        printk("  [%u]: sg_addr 0x%lx, sg_count 0x%lx\n",
            i, (ulong) b->sg_list[i].sg_addr, (ulong) b->sg_list[i].sg_count);
    }
}

/*
 * asc_prt_hex()
 *
 * Print hexadecimal output in 4 byte groupings 32 bytes
 * or 8 double-words per line.
 */
STATIC void
asc_prt_hex(char *f, uchar *s, int l)
{
    int            i;
    int            j;
    int            k;
    int            m;

    printk("%s: (%d bytes)\n", f, l);

    for (i = 0; i < l; i += 32) {

        /* Display a maximum of 8 double-words per line. */
        if ((k = (l - i) / 4) >= 8) {
            k = 8;
            m = 0;
        } else {
            m = (l - i) % 4;
        }

        for (j = 0; j < k; j++) {
            printk(" %2.2X%2.2X%2.2X%2.2X",
                (unsigned) s[i+(j*4)], (unsigned) s[i+(j*4)+1],
                (unsigned) s[i+(j*4)+2], (unsigned) s[i+(j*4)+3]);
        }

        switch (m) {
        case 0:
        default:
            break;
        case 1:
            printk(" %2.2X",
                (unsigned) s[i+(j*4)]);
            break;
        case 2:
            printk(" %2.2X%2.2X",
                (unsigned) s[i+(j*4)],
                (unsigned) s[i+(j*4)+1]);
            break;
        case 3:
            printk(" %2.2X%2.2X%2.2X",
                (unsigned) s[i+(j*4)+1],
                (unsigned) s[i+(j*4)+2],
                (unsigned) s[i+(j*4)+3]);
            break;
        }

        printk("\n");
    }
}
#endif /* ADVANSYS_DEBUG */

/*
 * --- Asc Library Functions
 */

STATIC ushort __init
AscGetEisaChipCfg(
                     PortAddr iop_base)
{
    PortAddr            eisa_cfg_iop;

    eisa_cfg_iop = (PortAddr) ASC_GET_EISA_SLOT(iop_base) |
      (PortAddr) (ASC_EISA_CFG_IOP_MASK);
    return (inpw(eisa_cfg_iop));
}

STATIC uchar __init
AscSetChipScsiID(
                    PortAddr iop_base,
                    uchar new_host_id
)
{
    ushort              cfg_lsw;

    if (AscGetChipScsiID(iop_base) == new_host_id) {
        return (new_host_id);
    }
    cfg_lsw = AscGetChipCfgLsw(iop_base);
    cfg_lsw &= 0xF8FF;
    cfg_lsw |= (ushort) ((new_host_id & ASC_MAX_TID) << 8);
    AscSetChipCfgLsw(iop_base, cfg_lsw);
    return (AscGetChipScsiID(iop_base));
}

STATIC uchar __init
AscGetChipScsiCtrl(
		PortAddr iop_base)
{
    uchar               sc;

    AscSetBank(iop_base, 1);
    sc = inp(iop_base + IOP_REG_SC);
    AscSetBank(iop_base, 0);
    return (sc);
}

STATIC uchar __init
AscGetChipVersion(
                     PortAddr iop_base,
                     ushort bus_type
)
{
    if ((bus_type & ASC_IS_EISA) != 0) {
        PortAddr            eisa_iop;
        uchar               revision;
        eisa_iop = (PortAddr) ASC_GET_EISA_SLOT(iop_base) |
          (PortAddr) ASC_EISA_REV_IOP_MASK;
        revision = inp(eisa_iop);
        return ((uchar) ((ASC_CHIP_MIN_VER_EISA - 1) + revision));
    }
    return (AscGetChipVerNo(iop_base));
}

STATIC ushort __init
AscGetChipBusType(
                     PortAddr iop_base)
{
    ushort              chip_ver;

    chip_ver = AscGetChipVerNo(iop_base);
    if (
           (chip_ver >= ASC_CHIP_MIN_VER_VL)
           && (chip_ver <= ASC_CHIP_MAX_VER_VL)
) {
        if (
               ((iop_base & 0x0C30) == 0x0C30)
               || ((iop_base & 0x0C50) == 0x0C50)
) {
            return (ASC_IS_EISA);
        }
        return (ASC_IS_VL);
    }
    if ((chip_ver >= ASC_CHIP_MIN_VER_ISA) &&
        (chip_ver <= ASC_CHIP_MAX_VER_ISA)) {
        if (chip_ver >= ASC_CHIP_MIN_VER_ISA_PNP) {
            return (ASC_IS_ISAPNP);
        }
        return (ASC_IS_ISA);
    } else if ((chip_ver >= ASC_CHIP_MIN_VER_PCI) &&
               (chip_ver <= ASC_CHIP_MAX_VER_PCI)) {
        return (ASC_IS_PCI);
    }
    return (0);
}

STATIC ASC_DCNT
AscLoadMicroCode(
                    PortAddr iop_base,
                    ushort s_addr,
                    uchar *mcode_buf,
                    ushort mcode_size
)
{
    ASC_DCNT            chksum;
    ushort              mcode_word_size;
    ushort              mcode_chksum;

    /* Write the microcode buffer starting at LRAM address 0. */
    mcode_word_size = (ushort) (mcode_size >> 1);
    AscMemWordSetLram(iop_base, s_addr, 0, mcode_word_size);
    AscMemWordCopyPtrToLram(iop_base, s_addr, mcode_buf, mcode_word_size);

    chksum = AscMemSumLramWord(iop_base, s_addr, mcode_word_size);
    ASC_DBG1(1, "AscLoadMicroCode: chksum 0x%lx\n", (ulong) chksum);
    mcode_chksum = (ushort) AscMemSumLramWord(iop_base,
          (ushort) ASC_CODE_SEC_BEG,
          (ushort) ((mcode_size - s_addr - (ushort) ASC_CODE_SEC_BEG) / 2));
    ASC_DBG1(1, "AscLoadMicroCode: mcode_chksum 0x%lx\n",
        (ulong) mcode_chksum);
    AscWriteLramWord(iop_base, ASCV_MCODE_CHKSUM_W, mcode_chksum);
    AscWriteLramWord(iop_base, ASCV_MCODE_SIZE_W, mcode_size);
    return (chksum);
}

STATIC int
AscFindSignature(
                    PortAddr iop_base
)
{
    ushort              sig_word;

    ASC_DBG2(1, "AscFindSignature: AscGetChipSignatureByte(0x%x) 0x%x\n",
        iop_base, AscGetChipSignatureByte(iop_base));
    if (AscGetChipSignatureByte(iop_base) == (uchar) ASC_1000_ID1B) {
        ASC_DBG2(1, "AscFindSignature: AscGetChipSignatureWord(0x%x) 0x%x\n",
            iop_base, AscGetChipSignatureWord(iop_base));
        sig_word = AscGetChipSignatureWord(iop_base);
        if ((sig_word == (ushort) ASC_1000_ID0W) ||
            (sig_word == (ushort) ASC_1000_ID0W_FIX)) {
            return (1);
        }
    }
    return (0);
}

STATIC PortAddr _asc_def_iop_base[ASC_IOADR_TABLE_MAX_IX] __initdata =
{
    0x100, ASC_IOADR_1, 0x120, ASC_IOADR_2, 0x140, ASC_IOADR_3, ASC_IOADR_4,
    ASC_IOADR_5, ASC_IOADR_6, ASC_IOADR_7, ASC_IOADR_8
};

#ifdef CONFIG_ISA
STATIC uchar _isa_pnp_inited __initdata = 0;

STATIC PortAddr __init
AscSearchIOPortAddr(
                       PortAddr iop_beg,
                       ushort bus_type)
{
    if (bus_type & ASC_IS_VL) {
        while ((iop_beg = AscSearchIOPortAddr11(iop_beg)) != 0) {
            if (AscGetChipVersion(iop_beg, bus_type) <= ASC_CHIP_MAX_VER_VL) {
                return (iop_beg);
            }
        }
        return (0);
    }
    if (bus_type & ASC_IS_ISA) {
        if (_isa_pnp_inited == 0) {
            AscSetISAPNPWaitForKey();
            _isa_pnp_inited++;
        }
        while ((iop_beg = AscSearchIOPortAddr11(iop_beg)) != 0) {
            if ((AscGetChipVersion(iop_beg, bus_type) & ASC_CHIP_VER_ISA_BIT) != 0) {
                return (iop_beg);
            }
        }
        return (0);
    }
    if (bus_type & ASC_IS_EISA) {
        if ((iop_beg = AscSearchIOPortAddrEISA(iop_beg)) != 0) {
            return (iop_beg);
        }
        return (0);
    }
    return (0);
}

STATIC PortAddr __init
AscSearchIOPortAddr11(
                         PortAddr s_addr
)
{
    int                 i;
    PortAddr            iop_base;

    for (i = 0; i < ASC_IOADR_TABLE_MAX_IX; i++) {
        if (_asc_def_iop_base[i] > s_addr) {
            break;
        }
    }
    for (; i < ASC_IOADR_TABLE_MAX_IX; i++) {
        iop_base = _asc_def_iop_base[i];
	if (!request_region(iop_base, ASC_IOADR_GAP, "advansys")){
            ASC_DBG1(1,
               "AscSearchIOPortAddr11: check_region() failed I/O port 0x%x\n",
                     iop_base);
            continue;
        }
        ASC_DBG1(1, "AscSearchIOPortAddr11: probing I/O port 0x%x\n", iop_base);
	release_region(iop_base, ASC_IOADR_GAP);
        if (AscFindSignature(iop_base)) {
            return (iop_base);
        }
    }
    return (0);
}

STATIC void __init
AscSetISAPNPWaitForKey(void)
{
    outp(ASC_ISA_PNP_PORT_ADDR, 0x02);
    outp(ASC_ISA_PNP_PORT_WRITE, 0x02);
    return;
}
#endif /* CONFIG_ISA */

STATIC void __init
AscToggleIRQAct(
                   PortAddr iop_base
)
{
    AscSetChipStatus(iop_base, CIW_IRQ_ACT);
    AscSetChipStatus(iop_base, 0);
    return;
}

STATIC uchar __init
AscGetChipIRQ(
                 PortAddr iop_base,
                 ushort bus_type)
{
    ushort              cfg_lsw;
    uchar               chip_irq;

    if ((bus_type & ASC_IS_EISA) != 0) {
        cfg_lsw = AscGetEisaChipCfg(iop_base);
        chip_irq = (uchar) (((cfg_lsw >> 8) & 0x07) + 10);
        if ((chip_irq == 13) || (chip_irq > 15)) {
            return (0);
        }
        return (chip_irq);
    }
    if ((bus_type & ASC_IS_VL) != 0) {
        cfg_lsw = AscGetChipCfgLsw(iop_base);
        chip_irq = (uchar) (((cfg_lsw >> 2) & 0x07));
        if ((chip_irq == 0) ||
            (chip_irq == 4) ||
            (chip_irq == 7)) {
            return (0);
        }
        return ((uchar) (chip_irq + (ASC_MIN_IRQ_NO - 1)));
    }
    cfg_lsw = AscGetChipCfgLsw(iop_base);
    chip_irq = (uchar) (((cfg_lsw >> 2) & 0x03));
    if (chip_irq == 3)
        chip_irq += (uchar) 2;
    return ((uchar) (chip_irq + ASC_MIN_IRQ_NO));
}

STATIC uchar __init
AscSetChipIRQ(
                 PortAddr iop_base,
                 uchar irq_no,
                 ushort bus_type)
{
    ushort              cfg_lsw;

    if ((bus_type & ASC_IS_VL) != 0) {
        if (irq_no != 0) {
            if ((irq_no < ASC_MIN_IRQ_NO) || (irq_no > ASC_MAX_IRQ_NO)) {
                irq_no = 0;
            } else {
                irq_no -= (uchar) ((ASC_MIN_IRQ_NO - 1));
            }
        }
        cfg_lsw = (ushort) (AscGetChipCfgLsw(iop_base) & 0xFFE3);
        cfg_lsw |= (ushort) 0x0010;
        AscSetChipCfgLsw(iop_base, cfg_lsw);
        AscToggleIRQAct(iop_base);
        cfg_lsw = (ushort) (AscGetChipCfgLsw(iop_base) & 0xFFE0);
        cfg_lsw |= (ushort) ((irq_no & 0x07) << 2);
        AscSetChipCfgLsw(iop_base, cfg_lsw);
        AscToggleIRQAct(iop_base);
        return (AscGetChipIRQ(iop_base, bus_type));
    }
    if ((bus_type & (ASC_IS_ISA)) != 0) {
        if (irq_no == 15)
            irq_no -= (uchar) 2;
        irq_no -= (uchar) ASC_MIN_IRQ_NO;
        cfg_lsw = (ushort) (AscGetChipCfgLsw(iop_base) & 0xFFF3);
        cfg_lsw |= (ushort) ((irq_no & 0x03) << 2);
        AscSetChipCfgLsw(iop_base, cfg_lsw);
        return (AscGetChipIRQ(iop_base, bus_type));
    }
    return (0);
}

#ifdef CONFIG_ISA
STATIC void __init
AscEnableIsaDma(
                   uchar dma_channel)
{
    if (dma_channel < 4) {
        outp(0x000B, (ushort) (0xC0 | dma_channel));
        outp(0x000A, dma_channel);
    } else if (dma_channel < 8) {
        outp(0x00D6, (ushort) (0xC0 | (dma_channel - 4)));
        outp(0x00D4, (ushort) (dma_channel - 4));
    }
    return;
}
#endif /* CONFIG_ISA */

STATIC int
AscIsrChipHalted(
                    ASC_DVC_VAR *asc_dvc
)
{
    EXT_MSG             ext_msg;
    EXT_MSG             out_msg;
    ushort              halt_q_addr;
    int                 sdtr_accept;
    ushort              int_halt_code;
    ASC_SCSI_BIT_ID_TYPE scsi_busy;
    ASC_SCSI_BIT_ID_TYPE target_id;
    PortAddr            iop_base;
    uchar               tag_code;
    uchar               q_status;
    uchar               halt_qp;
    uchar               sdtr_data;
    uchar               target_ix;
    uchar               q_cntl, tid_no;
    uchar               cur_dvc_qng;
    uchar               asyn_sdtr;
    uchar               scsi_status;
    asc_board_t         *boardp;

    ASC_ASSERT(asc_dvc->drv_ptr != NULL);
    boardp = asc_dvc->drv_ptr;

    iop_base = asc_dvc->iop_base;
    int_halt_code = AscReadLramWord(iop_base, ASCV_HALTCODE_W);

    halt_qp = AscReadLramByte(iop_base, ASCV_CURCDB_B);
    halt_q_addr = ASC_QNO_TO_QADDR(halt_qp);
    target_ix = AscReadLramByte(iop_base,
                   (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_TARGET_IX));
    q_cntl = AscReadLramByte(iop_base,
                        (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_CNTL));
    tid_no = ASC_TIX_TO_TID(target_ix);
    target_id = (uchar) ASC_TID_TO_TARGET_ID(tid_no);
    if (asc_dvc->pci_fix_asyn_xfer & target_id) {
        asyn_sdtr = ASYN_SDTR_DATA_FIX_PCI_REV_AB;
    } else {
        asyn_sdtr = 0;
    }
    if (int_halt_code == ASC_HALT_DISABLE_ASYN_USE_SYN_FIX) {
        if (asc_dvc->pci_fix_asyn_xfer & target_id) {
            AscSetChipSDTR(iop_base, 0, tid_no);
            boardp->sdtr_data[tid_no] = 0;
        }
        AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
        return (0);
    } else if (int_halt_code == ASC_HALT_ENABLE_ASYN_USE_SYN_FIX) {
        if (asc_dvc->pci_fix_asyn_xfer & target_id) {
            AscSetChipSDTR(iop_base, asyn_sdtr, tid_no);
            boardp->sdtr_data[tid_no] = asyn_sdtr;
        }
        AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
        return (0);
    } else if (int_halt_code == ASC_HALT_EXTMSG_IN) {

        AscMemWordCopyPtrFromLram(iop_base,
                               ASCV_MSGIN_BEG,
                               (uchar *) &ext_msg,
                               sizeof(EXT_MSG) >> 1);

        if (ext_msg.msg_type == MS_EXTEND &&
            ext_msg.msg_req == MS_SDTR_CODE &&
            ext_msg.msg_len == MS_SDTR_LEN) {
            sdtr_accept = TRUE;
            if ((ext_msg.req_ack_offset > ASC_SYN_MAX_OFFSET)) {

                sdtr_accept = FALSE;
                ext_msg.req_ack_offset = ASC_SYN_MAX_OFFSET;
            }
            if ((ext_msg.xfer_period <
                 asc_dvc->sdtr_period_tbl[asc_dvc->host_init_sdtr_index]) ||
                (ext_msg.xfer_period >
                 asc_dvc->sdtr_period_tbl[asc_dvc->max_sdtr_index])) {
                sdtr_accept = FALSE;
                ext_msg.xfer_period =
                    asc_dvc->sdtr_period_tbl[asc_dvc->host_init_sdtr_index];
            }
            if (sdtr_accept) {
                sdtr_data = AscCalSDTRData(asc_dvc, ext_msg.xfer_period,
                                           ext_msg.req_ack_offset);
                if ((sdtr_data == 0xFF)) {

                    q_cntl |= QC_MSG_OUT;
                    asc_dvc->init_sdtr &= ~target_id;
                    asc_dvc->sdtr_done &= ~target_id;
                    AscSetChipSDTR(iop_base, asyn_sdtr, tid_no);
                    boardp->sdtr_data[tid_no] = asyn_sdtr;
                }
            }
            if (ext_msg.req_ack_offset == 0) {

                q_cntl &= ~QC_MSG_OUT;
                asc_dvc->init_sdtr &= ~target_id;
                asc_dvc->sdtr_done &= ~target_id;
                AscSetChipSDTR(iop_base, asyn_sdtr, tid_no);
            } else {
                if (sdtr_accept && (q_cntl & QC_MSG_OUT)) {

                    q_cntl &= ~QC_MSG_OUT;
                    asc_dvc->sdtr_done |= target_id;
                    asc_dvc->init_sdtr |= target_id;
                    asc_dvc->pci_fix_asyn_xfer &= ~target_id;
                    sdtr_data = AscCalSDTRData(asc_dvc, ext_msg.xfer_period,
                                               ext_msg.req_ack_offset);
                    AscSetChipSDTR(iop_base, sdtr_data, tid_no);
                    boardp->sdtr_data[tid_no] = sdtr_data;
                } else {

                    q_cntl |= QC_MSG_OUT;
                    AscMsgOutSDTR(asc_dvc,
                                  ext_msg.xfer_period,
                                  ext_msg.req_ack_offset);
                    asc_dvc->pci_fix_asyn_xfer &= ~target_id;
                    sdtr_data = AscCalSDTRData(asc_dvc, ext_msg.xfer_period,
                                               ext_msg.req_ack_offset);
                    AscSetChipSDTR(iop_base, sdtr_data, tid_no);
                    boardp->sdtr_data[tid_no] = sdtr_data;
                    asc_dvc->sdtr_done |= target_id;
                    asc_dvc->init_sdtr |= target_id;
                }
            }

            AscWriteLramByte(iop_base,
                         (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_CNTL),
                             q_cntl);
            AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
            return (0);
        } else if (ext_msg.msg_type == MS_EXTEND &&
                   ext_msg.msg_req == MS_WDTR_CODE &&
                   ext_msg.msg_len == MS_WDTR_LEN) {

            ext_msg.wdtr_width = 0;
            AscMemWordCopyPtrToLram(iop_base,
                                 ASCV_MSGOUT_BEG,
                                 (uchar *) &ext_msg,
                                 sizeof(EXT_MSG) >> 1);
            q_cntl |= QC_MSG_OUT;
            AscWriteLramByte(iop_base,
                         (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_CNTL),
                             q_cntl);
            AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
            return (0);
        } else {

            ext_msg.msg_type = MESSAGE_REJECT;
            AscMemWordCopyPtrToLram(iop_base,
                                 ASCV_MSGOUT_BEG,
                                 (uchar *) &ext_msg,
                                 sizeof(EXT_MSG) >> 1);
            q_cntl |= QC_MSG_OUT;
            AscWriteLramByte(iop_base,
                         (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_CNTL),
                             q_cntl);
            AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
            return (0);
        }
    } else if (int_halt_code == ASC_HALT_CHK_CONDITION) {

        q_cntl |= QC_REQ_SENSE;

        if ((asc_dvc->init_sdtr & target_id) != 0) {

            asc_dvc->sdtr_done &= ~target_id;

            sdtr_data = AscGetMCodeInitSDTRAtID(iop_base, tid_no);
            q_cntl |= QC_MSG_OUT;
            AscMsgOutSDTR(asc_dvc,
                          asc_dvc->sdtr_period_tbl[(sdtr_data >> 4) &
                           (uchar) (asc_dvc->max_sdtr_index - 1)],
                          (uchar) (sdtr_data & (uchar) ASC_SYN_MAX_OFFSET));
        }

        AscWriteLramByte(iop_base,
                         (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_CNTL),
                         q_cntl);

        tag_code = AscReadLramByte(iop_base,
                    (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_TAG_CODE));
        tag_code &= 0xDC;
        if (
               (asc_dvc->pci_fix_asyn_xfer & target_id)
               && !(asc_dvc->pci_fix_asyn_xfer_always & target_id)
) {

            tag_code |= (ASC_TAG_FLAG_DISABLE_DISCONNECT
                         | ASC_TAG_FLAG_DISABLE_ASYN_USE_SYN_FIX);

        }
        AscWriteLramByte(iop_base,
                     (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_TAG_CODE),
                         tag_code);

        q_status = AscReadLramByte(iop_base,
                      (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_STATUS));
        q_status |= (QS_READY | QS_BUSY);
        AscWriteLramByte(iop_base,
                       (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_STATUS),
                         q_status);

        scsi_busy = AscReadLramByte(iop_base,
                                    (ushort) ASCV_SCSIBUSY_B);
        scsi_busy &= ~target_id;
        AscWriteLramByte(iop_base, (ushort) ASCV_SCSIBUSY_B, scsi_busy);

        AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
        return (0);
    } else if (int_halt_code == ASC_HALT_SDTR_REJECTED) {

        AscMemWordCopyPtrFromLram(iop_base,
                               ASCV_MSGOUT_BEG,
                               (uchar *) &out_msg,
                               sizeof(EXT_MSG) >> 1);

        if ((out_msg.msg_type == MS_EXTEND) &&
            (out_msg.msg_len == MS_SDTR_LEN) &&
            (out_msg.msg_req == MS_SDTR_CODE)) {

            asc_dvc->init_sdtr &= ~target_id;
            asc_dvc->sdtr_done &= ~target_id;
            AscSetChipSDTR(iop_base, asyn_sdtr, tid_no);
            boardp->sdtr_data[tid_no] = asyn_sdtr;
        }
        q_cntl &= ~QC_MSG_OUT;
        AscWriteLramByte(iop_base,
                         (ushort) (halt_q_addr + (ushort) ASC_SCSIQ_B_CNTL),
                         q_cntl);
        AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
        return (0);
    } else if (int_halt_code == ASC_HALT_SS_QUEUE_FULL) {

        scsi_status = AscReadLramByte(iop_base,
          (ushort) ((ushort) halt_q_addr + (ushort) ASC_SCSIQ_SCSI_STATUS));
        cur_dvc_qng = AscReadLramByte(iop_base,
                     (ushort) ((ushort) ASC_QADR_BEG + (ushort) target_ix));
        if ((cur_dvc_qng > 0) &&
            (asc_dvc->cur_dvc_qng[tid_no] > 0)) {

            scsi_busy = AscReadLramByte(iop_base,
                                        (ushort) ASCV_SCSIBUSY_B);
            scsi_busy |= target_id;
            AscWriteLramByte(iop_base,
                             (ushort) ASCV_SCSIBUSY_B, scsi_busy);
            asc_dvc->queue_full_or_busy |= target_id;

            if (scsi_status == SAM_STAT_TASK_SET_FULL) {
                if (cur_dvc_qng > ASC_MIN_TAGGED_CMD) {
                    cur_dvc_qng -= 1;
                    asc_dvc->max_dvc_qng[tid_no] = cur_dvc_qng;

                    AscWriteLramByte(iop_base,
                          (ushort) ((ushort) ASCV_MAX_DVC_QNG_BEG +
                           (ushort) tid_no),
                          cur_dvc_qng);

                    /*
                     * Set the device queue depth to the number of
                     * active requests when the QUEUE FULL condition
                     * was encountered.
                     */
                    boardp->queue_full |= target_id;
                    boardp->queue_full_cnt[tid_no] = cur_dvc_qng;
                }
            }
        }
        AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
        return (0);
    }
#if CC_VERY_LONG_SG_LIST
    else if (int_halt_code == ASC_HALT_HOST_COPY_SG_LIST_TO_RISC)
    {
        uchar              q_no;
        ushort             q_addr;
        uchar              sg_wk_q_no;
        uchar              first_sg_wk_q_no;
        ASC_SCSI_Q         *scsiq; /* Ptr to driver request. */
        ASC_SG_HEAD        *sg_head; /* Ptr to driver SG request. */
        ASC_SG_LIST_Q      scsi_sg_q; /* Structure written to queue. */
        ushort             sg_list_dwords;
        ushort             sg_entry_cnt;
        uchar              next_qp;
        int                i;

        q_no = AscReadLramByte(iop_base, (ushort) ASCV_REQ_SG_LIST_QP);
        if (q_no == ASC_QLINK_END)
        {
            return(0);
        }

        q_addr = ASC_QNO_TO_QADDR(q_no);

        /*
         * Convert the request's SRB pointer to a host ASC_SCSI_REQ
         * structure pointer using a macro provided by the driver.
         * The ASC_SCSI_REQ pointer provides a pointer to the
         * host ASC_SG_HEAD structure.
         */
        /* Read request's SRB pointer. */
        scsiq = (ASC_SCSI_Q *)
           ASC_SRB2SCSIQ(
               ASC_U32_TO_VADDR(AscReadLramDWord(iop_base,
               (ushort) (q_addr + ASC_SCSIQ_D_SRBPTR))));

        /*
         * Get request's first and working SG queue.
         */
        sg_wk_q_no = AscReadLramByte(iop_base,
            (ushort) (q_addr + ASC_SCSIQ_B_SG_WK_QP));

        first_sg_wk_q_no = AscReadLramByte(iop_base,
            (ushort) (q_addr + ASC_SCSIQ_B_FIRST_SG_WK_QP));

        /*
         * Reset request's working SG queue back to the
         * first SG queue.
         */
        AscWriteLramByte(iop_base,
            (ushort) (q_addr + (ushort) ASC_SCSIQ_B_SG_WK_QP),
            first_sg_wk_q_no);

        sg_head = scsiq->sg_head;

        /*
         * Set sg_entry_cnt to the number of SG elements
         * that will be completed on this interrupt.
         *
         * Note: The allocated SG queues contain ASC_MAX_SG_LIST - 1
         * SG elements. The data_cnt and data_addr fields which
         * add 1 to the SG element capacity are not used when
         * restarting SG handling after a halt.
         */
        if (scsiq->remain_sg_entry_cnt > (ASC_MAX_SG_LIST - 1))
        {
             sg_entry_cnt = ASC_MAX_SG_LIST - 1;

             /*
              * Keep track of remaining number of SG elements that will
              * need to be handled on the next interrupt.
              */
             scsiq->remain_sg_entry_cnt -= (ASC_MAX_SG_LIST - 1);
        } else
        {
             sg_entry_cnt = scsiq->remain_sg_entry_cnt;
             scsiq->remain_sg_entry_cnt = 0;
        }

        /*
         * Copy SG elements into the list of allocated SG queues.
         *
         * Last index completed is saved in scsiq->next_sg_index.
         */
        next_qp = first_sg_wk_q_no;
        q_addr = ASC_QNO_TO_QADDR(next_qp);
        scsi_sg_q.sg_head_qp = q_no;
        scsi_sg_q.cntl = QCSG_SG_XFER_LIST;
        for( i = 0; i < sg_head->queue_cnt; i++)
        {
             scsi_sg_q.seq_no = i + 1;
             if (sg_entry_cnt > ASC_SG_LIST_PER_Q)
             {
                 sg_list_dwords = (uchar) (ASC_SG_LIST_PER_Q * 2);
                 sg_entry_cnt -= ASC_SG_LIST_PER_Q;
                 /*
                  * After very first SG queue RISC FW uses next
                  * SG queue first element then checks sg_list_cnt
                  * against zero and then decrements, so set
                  * sg_list_cnt 1 less than number of SG elements
                  * in each SG queue.
                  */
                 scsi_sg_q.sg_list_cnt = ASC_SG_LIST_PER_Q - 1;
                 scsi_sg_q.sg_cur_list_cnt = ASC_SG_LIST_PER_Q - 1;
             } else {
                 /*
                  * This is the last SG queue in the list of
                  * allocated SG queues. If there are more
                  * SG elements than will fit in the allocated
                  * queues, then set the QCSG_SG_XFER_MORE flag.
                  */
                 if (scsiq->remain_sg_entry_cnt != 0)
                 {
                     scsi_sg_q.cntl |= QCSG_SG_XFER_MORE;
                 } else
                 {
                     scsi_sg_q.cntl |= QCSG_SG_XFER_END;
                 }
                 /* equals sg_entry_cnt * 2 */
                 sg_list_dwords = sg_entry_cnt << 1;
                 scsi_sg_q.sg_list_cnt = sg_entry_cnt - 1;
                 scsi_sg_q.sg_cur_list_cnt = sg_entry_cnt - 1;
                 sg_entry_cnt = 0;
             }

             scsi_sg_q.q_no = next_qp;
             AscMemWordCopyPtrToLram(iop_base,
                          q_addr + ASC_SCSIQ_SGHD_CPY_BEG,
                          (uchar *) &scsi_sg_q,
                          sizeof(ASC_SG_LIST_Q) >> 1);

             AscMemDWordCopyPtrToLram(iop_base,
                          q_addr + ASC_SGQ_LIST_BEG,
                          (uchar *) &sg_head->sg_list[scsiq->next_sg_index],
                          sg_list_dwords);

             scsiq->next_sg_index += ASC_SG_LIST_PER_Q;

             /*
              * If the just completed SG queue contained the
              * last SG element, then no more SG queues need
              * to be written.
              */
             if (scsi_sg_q.cntl & QCSG_SG_XFER_END)
             {
                 break;
             }

             next_qp = AscReadLramByte( iop_base,
                          ( ushort )( q_addr+ASC_SCSIQ_B_FWD ) );
             q_addr = ASC_QNO_TO_QADDR( next_qp );
        }

        /*
         * Clear the halt condition so the RISC will be restarted
         * after the return.
         */
        AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
        return(0);
    }
#endif /* CC_VERY_LONG_SG_LIST */
    return (0);
}

STATIC uchar
_AscCopyLramScsiDoneQ(
                         PortAddr iop_base,
                         ushort q_addr,
                         ASC_QDONE_INFO * scsiq,
                         ASC_DCNT max_dma_count
)
{
    ushort              _val;
    uchar               sg_queue_cnt;

    DvcGetQinfo(iop_base,
                q_addr + ASC_SCSIQ_DONE_INFO_BEG,
                (uchar *) scsiq,
                (sizeof (ASC_SCSIQ_2) + sizeof (ASC_SCSIQ_3)) / 2);

    _val = AscReadLramWord(iop_base,
                           (ushort) (q_addr + (ushort) ASC_SCSIQ_B_STATUS));
    scsiq->q_status = (uchar) _val;
    scsiq->q_no = (uchar) (_val >> 8);
    _val = AscReadLramWord(iop_base,
                           (ushort) (q_addr + (ushort) ASC_SCSIQ_B_CNTL));
    scsiq->cntl = (uchar) _val;
    sg_queue_cnt = (uchar) (_val >> 8);
    _val = AscReadLramWord(iop_base,
                        (ushort) (q_addr + (ushort) ASC_SCSIQ_B_SENSE_LEN));
    scsiq->sense_len = (uchar) _val;
    scsiq->extra_bytes = (uchar) (_val >> 8);

    /*
     * Read high word of remain bytes from alternate location.
     */
    scsiq->remain_bytes = (((ADV_DCNT) AscReadLramWord( iop_base,
                      (ushort) (q_addr+ (ushort) ASC_SCSIQ_W_ALT_DC1))) << 16);
    /*
     * Read low word of remain bytes from original location.
     */
    scsiq->remain_bytes += AscReadLramWord(iop_base,
        (ushort) (q_addr+ (ushort) ASC_SCSIQ_DW_REMAIN_XFER_CNT));

    scsiq->remain_bytes &= max_dma_count;
    return (sg_queue_cnt);
}

STATIC int
AscIsrQDone(
               ASC_DVC_VAR *asc_dvc
)
{
    uchar               next_qp;
    uchar               n_q_used;
    uchar               sg_list_qp;
    uchar               sg_queue_cnt;
    uchar               q_cnt;
    uchar               done_q_tail;
    uchar               tid_no;
    ASC_SCSI_BIT_ID_TYPE scsi_busy;
    ASC_SCSI_BIT_ID_TYPE target_id;
    PortAddr            iop_base;
    ushort              q_addr;
    ushort              sg_q_addr;
    uchar               cur_target_qng;
    ASC_QDONE_INFO      scsiq_buf;
    ASC_QDONE_INFO *scsiq;
    int                 false_overrun;
    ASC_ISR_CALLBACK    asc_isr_callback;

    iop_base = asc_dvc->iop_base;
    asc_isr_callback = asc_dvc->isr_callback;
    n_q_used = 1;
    scsiq = (ASC_QDONE_INFO *) & scsiq_buf;
    done_q_tail = (uchar) AscGetVarDoneQTail(iop_base);
    q_addr = ASC_QNO_TO_QADDR(done_q_tail);
    next_qp = AscReadLramByte(iop_base,
                              (ushort) (q_addr + (ushort) ASC_SCSIQ_B_FWD));
    if (next_qp != ASC_QLINK_END) {
        AscPutVarDoneQTail(iop_base, next_qp);
        q_addr = ASC_QNO_TO_QADDR(next_qp);
        sg_queue_cnt = _AscCopyLramScsiDoneQ(iop_base, q_addr, scsiq,
            asc_dvc->max_dma_count);
        AscWriteLramByte(iop_base,
                         (ushort) (q_addr + (ushort) ASC_SCSIQ_B_STATUS),
             (uchar) (scsiq->q_status & (uchar) ~ (QS_READY | QS_ABORTED)));
        tid_no = ASC_TIX_TO_TID(scsiq->d2.target_ix);
        target_id = ASC_TIX_TO_TARGET_ID(scsiq->d2.target_ix);
        if ((scsiq->cntl & QC_SG_HEAD) != 0) {
            sg_q_addr = q_addr;
            sg_list_qp = next_qp;
            for (q_cnt = 0; q_cnt < sg_queue_cnt; q_cnt++) {
                sg_list_qp = AscReadLramByte(iop_base,
                           (ushort) (sg_q_addr + (ushort) ASC_SCSIQ_B_FWD));
                sg_q_addr = ASC_QNO_TO_QADDR(sg_list_qp);
                if (sg_list_qp == ASC_QLINK_END) {
                    AscSetLibErrorCode(asc_dvc, ASCQ_ERR_SG_Q_LINKS);
                    scsiq->d3.done_stat = QD_WITH_ERROR;
                    scsiq->d3.host_stat = QHSTA_D_QDONE_SG_LIST_CORRUPTED;
                    goto FATAL_ERR_QDONE;
                }
                AscWriteLramByte(iop_base,
                         (ushort) (sg_q_addr + (ushort) ASC_SCSIQ_B_STATUS),
                                 QS_FREE);
            }
            n_q_used = sg_queue_cnt + 1;
            AscPutVarDoneQTail(iop_base, sg_list_qp);
        }
        if (asc_dvc->queue_full_or_busy & target_id) {
            cur_target_qng = AscReadLramByte(iop_base,
            (ushort) ((ushort) ASC_QADR_BEG + (ushort) scsiq->d2.target_ix));
            if (cur_target_qng < asc_dvc->max_dvc_qng[tid_no]) {
                scsi_busy = AscReadLramByte(iop_base,
                                            (ushort) ASCV_SCSIBUSY_B);
                scsi_busy &= ~target_id;
                AscWriteLramByte(iop_base,
                                 (ushort) ASCV_SCSIBUSY_B, scsi_busy);
                asc_dvc->queue_full_or_busy &= ~target_id;
            }
        }
        if (asc_dvc->cur_total_qng >= n_q_used) {
            asc_dvc->cur_total_qng -= n_q_used;
            if (asc_dvc->cur_dvc_qng[tid_no] != 0) {
                asc_dvc->cur_dvc_qng[tid_no]--;
            }
        } else {
            AscSetLibErrorCode(asc_dvc, ASCQ_ERR_CUR_QNG);
            scsiq->d3.done_stat = QD_WITH_ERROR;
            goto FATAL_ERR_QDONE;
        }
        if ((scsiq->d2.srb_ptr == 0UL) ||
            ((scsiq->q_status & QS_ABORTED) != 0)) {
            return (0x11);
        } else if (scsiq->q_status == QS_DONE) {
            false_overrun = FALSE;
            if (scsiq->extra_bytes != 0) {
                scsiq->remain_bytes += (ADV_DCNT) scsiq->extra_bytes;
            }
            if (scsiq->d3.done_stat == QD_WITH_ERROR) {
                if (scsiq->d3.host_stat == QHSTA_M_DATA_OVER_RUN) {
                    if ((scsiq->cntl & (QC_DATA_IN | QC_DATA_OUT)) == 0) {
                        scsiq->d3.done_stat = QD_NO_ERROR;
                        scsiq->d3.host_stat = QHSTA_NO_ERROR;
                    } else if (false_overrun) {
                        scsiq->d3.done_stat = QD_NO_ERROR;
                        scsiq->d3.host_stat = QHSTA_NO_ERROR;
                    }
                } else if (scsiq->d3.host_stat ==
                           QHSTA_M_HUNG_REQ_SCSI_BUS_RESET) {
                    AscStopChip(iop_base);
                    AscSetChipControl(iop_base,
                        (uchar) (CC_SCSI_RESET | CC_HALT));
                    DvcDelayNanoSecond(asc_dvc, 60000);
                    AscSetChipControl(iop_base, CC_HALT);
                    AscSetChipStatus(iop_base, CIW_CLR_SCSI_RESET_INT);
                    AscSetChipStatus(iop_base, 0);
                    AscSetChipControl(iop_base, 0);
                }
            }
            if ((scsiq->cntl & QC_NO_CALLBACK) == 0) {
                (*asc_isr_callback) (asc_dvc, scsiq);
            } else {
                if ((AscReadLramByte(iop_base,
                          (ushort) (q_addr + (ushort) ASC_SCSIQ_CDB_BEG)) ==
                     START_STOP)) {
                    asc_dvc->unit_not_ready &= ~target_id;
                    if (scsiq->d3.done_stat != QD_NO_ERROR) {
                        asc_dvc->start_motor &= ~target_id;
                    }
                }
            }
            return (1);
        } else {
            AscSetLibErrorCode(asc_dvc, ASCQ_ERR_Q_STATUS);
          FATAL_ERR_QDONE:
            if ((scsiq->cntl & QC_NO_CALLBACK) == 0) {
                (*asc_isr_callback) (asc_dvc, scsiq);
            }
            return (0x80);
        }
    }
    return (0);
}

STATIC int
AscISR(
          ASC_DVC_VAR *asc_dvc
)
{
    ASC_CS_TYPE         chipstat;
    PortAddr            iop_base;
    ushort              saved_ram_addr;
    uchar               ctrl_reg;
    uchar               saved_ctrl_reg;
    int                 int_pending;
    int                 status;
    uchar               host_flag;

    iop_base = asc_dvc->iop_base;
    int_pending = FALSE;

    if (AscIsIntPending(iop_base) == 0)
    {
        return int_pending;
    }

    if (((asc_dvc->init_state & ASC_INIT_STATE_END_LOAD_MC) == 0)
        || (asc_dvc->isr_callback == 0)
) {
        return (ERR);
    }
    if (asc_dvc->in_critical_cnt != 0) {
        AscSetLibErrorCode(asc_dvc, ASCQ_ERR_ISR_ON_CRITICAL);
        return (ERR);
    }
    if (asc_dvc->is_in_int) {
        AscSetLibErrorCode(asc_dvc, ASCQ_ERR_ISR_RE_ENTRY);
        return (ERR);
    }
    asc_dvc->is_in_int = TRUE;
    ctrl_reg = AscGetChipControl(iop_base);
    saved_ctrl_reg = ctrl_reg & (~(CC_SCSI_RESET | CC_CHIP_RESET |
                                   CC_SINGLE_STEP | CC_DIAG | CC_TEST));
    chipstat = AscGetChipStatus(iop_base);
    if (chipstat & CSW_SCSI_RESET_LATCH) {
        if (!(asc_dvc->bus_type & (ASC_IS_VL | ASC_IS_EISA))) {
            int i = 10;
            int_pending = TRUE;
            asc_dvc->sdtr_done = 0;
            saved_ctrl_reg &= (uchar) (~CC_HALT);
            while ((AscGetChipStatus(iop_base) & CSW_SCSI_RESET_ACTIVE) &&
                   (i-- > 0))
            {
                  DvcSleepMilliSecond(100);
            }
            AscSetChipControl(iop_base, (CC_CHIP_RESET | CC_HALT));
            AscSetChipControl(iop_base, CC_HALT);
            AscSetChipStatus(iop_base, CIW_CLR_SCSI_RESET_INT);
            AscSetChipStatus(iop_base, 0);
            chipstat = AscGetChipStatus(iop_base);
        }
    }
    saved_ram_addr = AscGetChipLramAddr(iop_base);
    host_flag = AscReadLramByte(iop_base,
        ASCV_HOST_FLAG_B) & (uchar) (~ASC_HOST_FLAG_IN_ISR);
    AscWriteLramByte(iop_base, ASCV_HOST_FLAG_B,
                     (uchar) (host_flag | (uchar) ASC_HOST_FLAG_IN_ISR));
    if ((chipstat & CSW_INT_PENDING)
        || (int_pending)
) {
        AscAckInterrupt(iop_base);
        int_pending = TRUE;
        if ((chipstat & CSW_HALTED) &&
            (ctrl_reg & CC_SINGLE_STEP)) {
            if (AscIsrChipHalted(asc_dvc) == ERR) {
                goto ISR_REPORT_QDONE_FATAL_ERROR;
            } else {
                saved_ctrl_reg &= (uchar) (~CC_HALT);
            }
        } else {
          ISR_REPORT_QDONE_FATAL_ERROR:
            if ((asc_dvc->dvc_cntl & ASC_CNTL_INT_MULTI_Q) != 0) {
                while (((status = AscIsrQDone(asc_dvc)) & 0x01) != 0) {
                }
            } else {
                do {
                    if ((status = AscIsrQDone(asc_dvc)) == 1) {
                        break;
                    }
                } while (status == 0x11);
            }
            if ((status & 0x80) != 0)
                int_pending = ERR;
        }
    }
    AscWriteLramByte(iop_base, ASCV_HOST_FLAG_B, host_flag);
    AscSetChipLramAddr(iop_base, saved_ram_addr);
    AscSetChipControl(iop_base, saved_ctrl_reg);
    asc_dvc->is_in_int = FALSE;
    return (int_pending);
}

/* Microcode buffer is kept after initialization for error recovery. */
STATIC uchar _asc_mcode_buf[] =
{
  0x01,  0x03,  0x01,  0x19,  0x0F,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,
  0x0F,  0x0F,  0x0F,  0x0F,  0x0F,  0x0F,  0x0F,  0x0F,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,
  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,
  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,
  0x00,  0x00,  0x00,  0x00,  0xC3,  0x12,  0x0D,  0x05,  0x01,  0x00,  0x00,  0x00,  0x00,  0xFF,  0x00,  0x00,
  0x00,  0x00,  0x00,  0x00,  0xFF,  0x80,  0xFF,  0xFF,  0x01,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,
  0x00,  0x00,  0x00,  0x23,  0x00,  0x00,  0x00,  0x00,  0x00,  0x07,  0x00,  0xFF,  0x00,  0x00,  0x00,  0x00,
  0xFF,  0xFF,  0xFF,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0xE4,  0x88,  0x00,  0x00,  0x00,  0x00,
  0x80,  0x73,  0x48,  0x04,  0x36,  0x00,  0x00,  0xA2,  0xC2,  0x00,  0x80,  0x73,  0x03,  0x23,  0x36,  0x40,
  0xB6,  0x00,  0x36,  0x00,  0x05,  0xD6,  0x0C,  0xD2,  0x12,  0xDA,  0x00,  0xA2,  0xC2,  0x00,  0x92,  0x80,
  0x1E,  0x98,  0x50,  0x00,  0xF5,  0x00,  0x48,  0x98,  0xDF,  0x23,  0x36,  0x60,  0xB6,  0x00,  0x92,  0x80,
  0x4F,  0x00,  0xF5,  0x00,  0x48,  0x98,  0xEF,  0x23,  0x36,  0x60,  0xB6,  0x00,  0x92,  0x80,  0x80,  0x62,
  0x92,  0x80,  0x00,  0x46,  0x15,  0xEE,  0x13,  0xEA,  0x02,  0x01,  0x09,  0xD8,  0xCD,  0x04,  0x4D,  0x00,
  0x00,  0xA3,  0xD6,  0x00,  0xA6,  0x97,  0x7F,  0x23,  0x04,  0x61,  0x84,  0x01,  0xE6,  0x84,  0xD2,  0xC1,
  0x80,  0x73,  0xCD,  0x04,  0x4D,  0x00,  0x00,  0xA3,  0xDA,  0x01,  0xA6,  0x97,  0xC6,  0x81,  0xC2,  0x88,
  0x80,  0x73,  0x80,  0x77,  0x00,  0x01,  0x01,  0xA1,  0xFE,  0x00,  0x4F,  0x00,  0x84,  0x97,  0x07,  0xA6,
  0x08,  0x01,  0x00,  0x33,  0x03,  0x00,  0xC2,  0x88,  0x03,  0x03,  0x01,  0xDE,  0xC2,  0x88,  0xCE,  0x00,
  0x69,  0x60,  0xCE,  0x00,  0x02,  0x03,  0x4A,  0x60,  0x00,  0xA2,  0x78,  0x01,  0x80,  0x63,  0x07,  0xA6,
  0x24,  0x01,  0x78,  0x81,  0x03,  0x03,  0x80,  0x63,  0xE2,  0x00,  0x07,  0xA6,  0x34,  0x01,  0x00,  0x33,
  0x04,  0x00,  0xC2,  0x88,  0x03,  0x07,  0x02,  0x01,  0x04,  0xCA,  0x0D,  0x23,  0x68,  0x98,  0x4D,  0x04,
  0x04,  0x85,  0x05,  0xD8,  0x0D,  0x23,  0x68,  0x98,  0xCD,  0x04,  0x15,  0x23,  0xF8,  0x88,  0xFB,  0x23,
  0x02,  0x61,  0x82,  0x01,  0x80,  0x63,  0x02,  0x03,  0x06,  0xA3,  0x62,  0x01,  0x00,  0x33,  0x0A,  0x00,
  0xC2,  0x88,  0x4E,  0x00,  0x07,  0xA3,  0x6E,  0x01,  0x00,  0x33,  0x0B,  0x00,  0xC2,  0x88,  0xCD,  0x04,
  0x36,  0x2D,  0x00,  0x33,  0x1A,  0x00,  0xC2,  0x88,  0x50,  0x04,  0x88,  0x81,  0x06,  0xAB,  0x82,  0x01,
  0x88,  0x81,  0x4E,  0x00,  0x07,  0xA3,  0x92,  0x01,  0x50,  0x00,  0x00,  0xA3,  0x3C,  0x01,  0x00,  0x05,
  0x7C,  0x81,  0x46,  0x97,  0x02,  0x01,  0x05,  0xC6,  0x04,  0x23,  0xA0,  0x01,  0x15,  0x23,  0xA1,  0x01,
  0xBE,  0x81,  0xFD,  0x23,  0x02,  0x61,  0x82,  0x01,  0x0A,  0xDA,  0x4A,  0x00,  0x06,  0x61,  0x00,  0xA0,
  0xB4,  0x01,  0x80,  0x63,  0xCD,  0x04,  0x36,  0x2D,  0x00,  0x33,  0x1B,  0x00,  0xC2,  0x88,  0x06,  0x23,
  0x68,  0x98,  0xCD,  0x04,  0xE6,  0x84,  0x06,  0x01,  0x00,  0xA2,  0xD4,  0x01,  0x57,  0x60,  0x00,  0xA0,
  0xDA,  0x01,  0xE6,  0x84,  0x80,  0x23,  0xA0,  0x01,  0xE6,  0x84,  0x80,  0x73,  0x4B,  0x00,  0x06,  0x61,
  0x00,  0xA2,  0x00,  0x02,  0x04,  0x01,  0x0C,  0xDE,  0x02,  0x01,  0x03,  0xCC,  0x4F,  0x00,  0x84,  0x97,
  0xFC,  0x81,  0x08,  0x23,  0x02,  0x41,  0x82,  0x01,  0x4F,  0x00,  0x62,  0x97,  0x48,  0x04,  0x84,  0x80,
  0xF0,  0x97,  0x00,  0x46,  0x56,  0x00,  0x03,  0xC0,  0x01,  0x23,  0xE8,  0x00,  0x81,  0x73,  0x06,  0x29,
  0x03,  0x42,  0x06,  0xE2,  0x03,  0xEE,  0x6B,  0xEB,  0x11,  0x23,  0xF8,  0x88,  0x04,  0x98,  0xF0,  0x80,
  0x80,  0x73,  0x80,  0x77,  0x07,  0xA4,  0x2A,  0x02,  0x7C,  0x95,  0x06,  0xA6,  0x34,  0x02,  0x03,  0xA6,
  0x4C,  0x04,  0x46,  0x82,  0x04,  0x01,  0x03,  0xD8,  0xB4,  0x98,  0x6A,  0x96,  0x46,  0x82,  0xFE,  0x95,
  0x80,  0x67,  0x83,  0x03,  0x80,  0x63,  0xB6,  0x2D,  0x02,  0xA6,  0x6C,  0x02,  0x07,  0xA6,  0x5A,  0x02,
  0x06,  0xA6,  0x5E,  0x02,  0x03,  0xA6,  0x62,  0x02,  0xC2,  0x88,  0x7C,  0x95,  0x48,  0x82,  0x60,  0x96,
  0x48,  0x82,  0x04,  0x23,  0xA0,  0x01,  0x14,  0x23,  0xA1,  0x01,  0x3C,  0x84,  0x04,  0x01,  0x0C,  0xDC,
  0xE0,  0x23,  0x25,  0x61,  0xEF,  0x00,  0x14,  0x01,  0x4F,  0x04,  0xA8,  0x01,  0x6F,  0x00,  0xA5,  0x01,
  0x03,  0x23,  0xA4,  0x01,  0x06,  0x23,  0x9C,  0x01,  0x24,  0x2B,  0x1C,  0x01,  0x02,  0xA6,  0xAA,  0x02,
  0x07,  0xA6,  0x5A,  0x02,  0x06,  0xA6,  0x5E,  0x02,  0x03,  0xA6,  0x20,  0x04,  0x01,  0xA6,  0xB4,  0x02,
  0x00,  0xA6,  0xB4,  0x02,  0x00,  0x33,  0x12,  0x00,  0xC2,  0x88,  0x00,  0x0E,  0x80,  0x63,  0x00,  0x43,
  0x00,  0xA0,  0x8C,  0x02,  0x4D,  0x04,  0x04,  0x01,  0x0B,  0xDC,  0xE7,  0x23,  0x04,  0x61,  0x84,  0x01,
  0x10,  0x31,  0x12,  0x35,  0x14,  0x01,  0xEC,  0x00,  0x6C,  0x38,  0x00,  0x3F,  0x00,  0x00,  0xEA,  0x82,
  0x18,  0x23,  0x04,  0x61,  0x18,  0xA0,  0xE2,  0x02,  0x04,  0x01,  0xA2,  0xC8,  0x00,  0x33,  0x1F,  0x00,
  0xC2,  0x88,  0x08,  0x31,  0x0A,  0x35,  0x0C,  0x39,  0x0E,  0x3D,  0x7E,  0x98,  0xB6,  0x2D,  0x01,  0xA6,
  0x14,  0x03,  0x00,  0xA6,  0x14,  0x03,  0x07,  0xA6,  0x0C,  0x03,  0x06,  0xA6,  0x10,  0x03,  0x03,  0xA6,
  0x20,  0x04,  0x02,  0xA6,  0x6C,  0x02,  0x00,  0x33,  0x33,  0x00,  0xC2,  0x88,  0x7C,  0x95,  0xEE,  0x82,
  0x60,  0x96,  0xEE,  0x82,  0x82,  0x98,  0x80,  0x42,  0x7E,  0x98,  0x64,  0xE4,  0x04,  0x01,  0x2D,  0xC8,
  0x31,  0x05,  0x07,  0x01,  0x00,  0xA2,  0x54,  0x03,  0x00,  0x43,  0x87,  0x01,  0x05,  0x05,  0x86,  0x98,
  0x7E,  0x98,  0x00,  0xA6,  0x16,  0x03,  0x07,  0xA6,  0x4C,  0x03,  0x03,  0xA6,  0x3C,  0x04,  0x06,  0xA6,
  0x50,  0x03,  0x01,  0xA6,  0x16,  0x03,  0x00,  0x33,  0x25,  0x00,  0xC2,  0x88,  0x7C,  0x95,  0x32,  0x83,
  0x60,  0x96,  0x32,  0x83,  0x04,  0x01,  0x10,  0xCE,  0x07,  0xC8,  0x05,  0x05,  0xEB,  0x04,  0x00,  0x33,
  0x00,  0x20,  0xC0,  0x20,  0x81,  0x62,  0x72,  0x83,  0x00,  0x01,  0x05,  0x05,  0xFF,  0xA2,  0x7A,  0x03,
  0xB1,  0x01,  0x08,  0x23,  0xB2,  0x01,  0x2E,  0x83,  0x05,  0x05,  0x15,  0x01,  0x00,  0xA2,  0x9A,  0x03,
  0xEC,  0x00,  0x6E,  0x00,  0x95,  0x01,  0x6C,  0x38,  0x00,  0x3F,  0x00,  0x00,  0x01,  0xA6,  0x96,  0x03,
  0x00,  0xA6,  0x96,  0x03,  0x10,  0x84,  0x80,  0x42,  0x7E,  0x98,  0x01,  0xA6,  0xA4,  0x03,  0x00,  0xA6,
  0xBC,  0x03,  0x10,  0x84,  0xA8,  0x98,  0x80,  0x42,  0x01,  0xA6,  0xA4,  0x03,  0x07,  0xA6,  0xB2,  0x03,
  0xD4,  0x83,  0x7C,  0x95,  0xA8,  0x83,  0x00,  0x33,  0x2F,  0x00,  0xC2,  0x88,  0xA8,  0x98,  0x80,  0x42,
  0x00,  0xA6,  0xBC,  0x03,  0x07,  0xA6,  0xCA,  0x03,  0xD4,  0x83,  0x7C,  0x95,  0xC0,  0x83,  0x00,  0x33,
  0x26,  0x00,  0xC2,  0x88,  0x38,  0x2B,  0x80,  0x32,  0x80,  0x36,  0x04,  0x23,  0xA0,  0x01,  0x12,  0x23,
  0xA1,  0x01,  0x10,  0x84,  0x07,  0xF0,  0x06,  0xA4,  0xF4,  0x03,  0x80,  0x6B,  0x80,  0x67,  0x05,  0x23,
  0x83,  0x03,  0x80,  0x63,  0x03,  0xA6,  0x0E,  0x04,  0x07,  0xA6,  0x06,  0x04,  0x06,  0xA6,  0x0A,  0x04,
  0x00,  0x33,  0x17,  0x00,  0xC2,  0x88,  0x7C,  0x95,  0xF4,  0x83,  0x60,  0x96,  0xF4,  0x83,  0x20,  0x84,
  0x07,  0xF0,  0x06,  0xA4,  0x20,  0x04,  0x80,  0x6B,  0x80,  0x67,  0x05,  0x23,  0x83,  0x03,  0x80,  0x63,
  0xB6,  0x2D,  0x03,  0xA6,  0x3C,  0x04,  0x07,  0xA6,  0x34,  0x04,  0x06,  0xA6,  0x38,  0x04,  0x00,  0x33,
  0x30,  0x00,  0xC2,  0x88,  0x7C,  0x95,  0x20,  0x84,  0x60,  0x96,  0x20,  0x84,  0x1D,  0x01,  0x06,  0xCC,
  0x00,  0x33,  0x00,  0x84,  0xC0,  0x20,  0x00,  0x23,  0xEA,  0x00,  0x81,  0x62,  0xA2,  0x0D,  0x80,  0x63,
  0x07,  0xA6,  0x5A,  0x04,  0x00,  0x33,  0x18,  0x00,  0xC2,  0x88,  0x03,  0x03,  0x80,  0x63,  0xA3,  0x01,
  0x07,  0xA4,  0x64,  0x04,  0x23,  0x01,  0x00,  0xA2,  0x86,  0x04,  0x0A,  0xA0,  0x76,  0x04,  0xE0,  0x00,
  0x00,  0x33,  0x1D,  0x00,  0xC2,  0x88,  0x0B,  0xA0,  0x82,  0x04,  0xE0,  0x00,  0x00,  0x33,  0x1E,  0x00,
  0xC2,  0x88,  0x42,  0x23,  0xF8,  0x88,  0x00,  0x23,  0x22,  0xA3,  0xE6,  0x04,  0x08,  0x23,  0x22,  0xA3,
  0xA2,  0x04,  0x28,  0x23,  0x22,  0xA3,  0xAE,  0x04,  0x02,  0x23,  0x22,  0xA3,  0xC4,  0x04,  0x42,  0x23,
  0xF8,  0x88,  0x4A,  0x00,  0x06,  0x61,  0x00,  0xA0,  0xAE,  0x04,  0x45,  0x23,  0xF8,  0x88,  0x04,  0x98,
  0x00,  0xA2,  0xC0,  0x04,  0xB4,  0x98,  0x00,  0x33,  0x00,  0x82,  0xC0,  0x20,  0x81,  0x62,  0xE8,  0x81,
  0x47,  0x23,  0xF8,  0x88,  0x04,  0x01,  0x0B,  0xDE,  0x04,  0x98,  0xB4,  0x98,  0x00,  0x33,  0x00,  0x81,
  0xC0,  0x20,  0x81,  0x62,  0x14,  0x01,  0x00,  0xA0,  0x00,  0x02,  0x43,  0x23,  0xF8,  0x88,  0x04,  0x23,
  0xA0,  0x01,  0x44,  0x23,  0xA1,  0x01,  0x80,  0x73,  0x4D,  0x00,  0x03,  0xA3,  0xF4,  0x04,  0x00,  0x33,
  0x27,  0x00,  0xC2,  0x88,  0x04,  0x01,  0x04,  0xDC,  0x02,  0x23,  0xA2,  0x01,  0x04,  0x23,  0xA0,  0x01,
  0x04,  0x98,  0x26,  0x95,  0x4B,  0x00,  0xF6,  0x00,  0x4F,  0x04,  0x4F,  0x00,  0x00,  0xA3,  0x22,  0x05,
  0x00,  0x05,  0x76,  0x00,  0x06,  0x61,  0x00,  0xA2,  0x1C,  0x05,  0x0A,  0x85,  0x46,  0x97,  0xCD,  0x04,
  0x24,  0x85,  0x48,  0x04,  0x84,  0x80,  0x02,  0x01,  0x03,  0xDA,  0x80,  0x23,  0x82,  0x01,  0x34,  0x85,
  0x02,  0x23,  0xA0,  0x01,  0x4A,  0x00,  0x06,  0x61,  0x00,  0xA2,  0x40,  0x05,  0x1D,  0x01,  0x04,  0xD6,
  0xFF,  0x23,  0x86,  0x41,  0x4B,  0x60,  0xCB,  0x00,  0xFF,  0x23,  0x80,  0x01,  0x49,  0x00,  0x81,  0x01,
  0x04,  0x01,  0x02,  0xC8,  0x30,  0x01,  0x80,  0x01,  0xF7,  0x04,  0x03,  0x01,  0x49,  0x04,  0x80,  0x01,
  0xC9,  0x00,  0x00,  0x05,  0x00,  0x01,  0xFF,  0xA0,  0x60,  0x05,  0x77,  0x04,  0x01,  0x23,  0xEA,  0x00,
  0x5D,  0x00,  0xFE,  0xC7,  0x00,  0x62,  0x00,  0x23,  0xEA,  0x00,  0x00,  0x63,  0x07,  0xA4,  0xF8,  0x05,
  0x03,  0x03,  0x02,  0xA0,  0x8E,  0x05,  0xF4,  0x85,  0x00,  0x33,  0x2D,  0x00,  0xC2,  0x88,  0x04,  0xA0,
  0xB8,  0x05,  0x80,  0x63,  0x00,  0x23,  0xDF,  0x00,  0x4A,  0x00,  0x06,  0x61,  0x00,  0xA2,  0xA4,  0x05,
  0x1D,  0x01,  0x06,  0xD6,  0x02,  0x23,  0x02,  0x41,  0x82,  0x01,  0x50,  0x00,  0x62,  0x97,  0x04,  0x85,
  0x04,  0x23,  0x02,  0x41,  0x82,  0x01,  0x04,  0x85,  0x08,  0xA0,  0xBE,  0x05,  0xF4,  0x85,  0x03,  0xA0,
  0xC4,  0x05,  0xF4,  0x85,  0x01,  0xA0,  0xCE,  0x05,  0x88,  0x00,  0x80,  0x63,  0xCC,  0x86,  0x07,  0xA0,
  0xEE,  0x05,  0x5F,  0x00,  0x00,  0x2B,  0xDF,  0x08,  0x00,  0xA2,  0xE6,  0x05,  0x80,  0x67,  0x80,  0x63,
  0x01,  0xA2,  0x7A,  0x06,  0x7C,  0x85,  0x06,  0x23,  0x68,  0x98,  0x48,  0x23,  0xF8,  0x88,  0x07,  0x23,
  0x80,  0x00,  0x06,  0x87,  0x80,  0x63,  0x7C,  0x85,  0x00,  0x23,  0xDF,  0x00,  0x00,  0x63,  0x4A,  0x00,
  0x06,  0x61,  0x00,  0xA2,  0x36,  0x06,  0x1D,  0x01,  0x16,  0xD4,  0xC0,  0x23,  0x07,  0x41,  0x83,  0x03,
  0x80,  0x63,  0x06,  0xA6,  0x1C,  0x06,  0x00,  0x33,  0x37,  0x00,  0xC2,  0x88,  0x1D,  0x01,  0x01,  0xD6,
  0x20,  0x23,  0x63,  0x60,  0x83,  0x03,  0x80,  0x63,  0x02,  0x23,  0xDF,  0x00,  0x07,  0xA6,  0x7C,  0x05,
  0xEF,  0x04,  0x6F,  0x00,  0x00,  0x63,  0x4B,  0x00,  0x06,  0x41,  0xCB,  0x00,  0x52,  0x00,  0x06,  0x61,
  0x00,  0xA2,  0x4E,  0x06,  0x1D,  0x01,  0x03,  0xCA,  0xC0,  0x23,  0x07,  0x41,  0x00,  0x63,  0x1D,  0x01,
  0x04,  0xCC,  0x00,  0x33,  0x00,  0x83,  0xC0,  0x20,  0x81,  0x62,  0x80,  0x23,  0x07,  0x41,  0x00,  0x63,
  0x80,  0x67,  0x08,  0x23,  0x83,  0x03,  0x80,  0x63,  0x00,  0x63,  0x01,  0x23,  0xDF,  0x00,  0x06,  0xA6,
  0x84,  0x06,  0x07,  0xA6,  0x7C,  0x05,  0x80,  0x67,  0x80,  0x63,  0x00,  0x33,  0x00,  0x40,  0xC0,  0x20,
  0x81,  0x62,  0x00,  0x63,  0x00,  0x00,  0xFE,  0x95,  0x83,  0x03,  0x80,  0x63,  0x06,  0xA6,  0x94,  0x06,
  0x07,  0xA6,  0x7C,  0x05,  0x00,  0x00,  0x01,  0xA0,  0x14,  0x07,  0x00,  0x2B,  0x40,  0x0E,  0x80,  0x63,
  0x01,  0x00,  0x06,  0xA6,  0xAA,  0x06,  0x07,  0xA6,  0x7C,  0x05,  0x40,  0x0E,  0x80,  0x63,  0x00,  0x43,
  0x00,  0xA0,  0xA2,  0x06,  0x06,  0xA6,  0xBC,  0x06,  0x07,  0xA6,  0x7C,  0x05,  0x80,  0x67,  0x40,  0x0E,
  0x80,  0x63,  0x07,  0xA6,  0x7C,  0x05,  0x00,  0x23,  0xDF,  0x00,  0x00,  0x63,  0x07,  0xA6,  0xD6,  0x06,
  0x00,  0x33,  0x2A,  0x00,  0xC2,  0x88,  0x03,  0x03,  0x80,  0x63,  0x89,  0x00,  0x0A,  0x2B,  0x07,  0xA6,
  0xE8,  0x06,  0x00,  0x33,  0x29,  0x00,  0xC2,  0x88,  0x00,  0x43,  0x00,  0xA2,  0xF4,  0x06,  0xC0,  0x0E,
  0x80,  0x63,  0xDE,  0x86,  0xC0,  0x0E,  0x00,  0x33,  0x00,  0x80,  0xC0,  0x20,  0x81,  0x62,  0x04,  0x01,
  0x02,  0xDA,  0x80,  0x63,  0x7C,  0x85,  0x80,  0x7B,  0x80,  0x63,  0x06,  0xA6,  0x8C,  0x06,  0x00,  0x33,
  0x2C,  0x00,  0xC2,  0x88,  0x0C,  0xA2,  0x2E,  0x07,  0xFE,  0x95,  0x83,  0x03,  0x80,  0x63,  0x06,  0xA6,
  0x2C,  0x07,  0x07,  0xA6,  0x7C,  0x05,  0x00,  0x33,  0x3D,  0x00,  0xC2,  0x88,  0x00,  0x00,  0x80,  0x67,
  0x83,  0x03,  0x80,  0x63,  0x0C,  0xA0,  0x44,  0x07,  0x07,  0xA6,  0x7C,  0x05,  0xBF,  0x23,  0x04,  0x61,
  0x84,  0x01,  0xE6,  0x84,  0x00,  0x63,  0xF0,  0x04,  0x01,  0x01,  0xF1,  0x00,  0x00,  0x01,  0xF2,  0x00,
  0x01,  0x05,  0x80,  0x01,  0x72,  0x04,  0x71,  0x00,  0x81,  0x01,  0x70,  0x04,  0x80,  0x05,  0x81,  0x05,
  0x00,  0x63,  0xF0,  0x04,  0xF2,  0x00,  0x72,  0x04,  0x01,  0x01,  0xF1,  0x00,  0x70,  0x00,  0x81,  0x01,
  0x70,  0x04,  0x71,  0x00,  0x81,  0x01,  0x72,  0x00,  0x80,  0x01,  0x71,  0x04,  0x70,  0x00,  0x80,  0x01,
  0x70,  0x04,  0x00,  0x63,  0xF0,  0x04,  0xF2,  0x00,  0x72,  0x04,  0x00,  0x01,  0xF1,  0x00,  0x70,  0x00,
  0x80,  0x01,  0x70,  0x04,  0x71,  0x00,  0x80,  0x01,  0x72,  0x00,  0x81,  0x01,  0x71,  0x04,  0x70,  0x00,
  0x81,  0x01,  0x70,  0x04,  0x00,  0x63,  0x00,  0x23,  0xB3,  0x01,  0x83,  0x05,  0xA3,  0x01,  0xA2,  0x01,
  0xA1,  0x01,  0x01,  0x23,  0xA0,  0x01,  0x00,  0x01,  0xC8,  0x00,  0x03,  0xA1,  0xC4,  0x07,  0x00,  0x33,
  0x07,  0x00,  0xC2,  0x88,  0x80,  0x05,  0x81,  0x05,  0x04,  0x01,  0x11,  0xC8,  0x48,  0x00,  0xB0,  0x01,
  0xB1,  0x01,  0x08,  0x23,  0xB2,  0x01,  0x05,  0x01,  0x48,  0x04,  0x00,  0x43,  0x00,  0xA2,  0xE4,  0x07,
  0x00,  0x05,  0xDA,  0x87,  0x00,  0x01,  0xC8,  0x00,  0xFF,  0x23,  0x80,  0x01,  0x05,  0x05,  0x00,  0x63,
  0xF7,  0x04,  0x1A,  0x09,  0xF6,  0x08,  0x6E,  0x04,  0x00,  0x02,  0x80,  0x43,  0x76,  0x08,  0x80,  0x02,
  0x77,  0x04,  0x00,  0x63,  0xF7,  0x04,  0x1A,  0x09,  0xF6,  0x08,  0x6E,  0x04,  0x00,  0x02,  0x00,  0xA0,
  0x14,  0x08,  0x16,  0x88,  0x00,  0x43,  0x76,  0x08,  0x80,  0x02,  0x77,  0x04,  0x00,  0x63,  0xF3,  0x04,
  0x00,  0x23,  0xF4,  0x00,  0x74,  0x00,  0x80,  0x43,  0xF4,  0x00,  0xCF,  0x40,  0x00,  0xA2,  0x44,  0x08,
  0x74,  0x04,  0x02,  0x01,  0xF7,  0xC9,  0xF6,  0xD9,  0x00,  0x01,  0x01,  0xA1,  0x24,  0x08,  0x04,  0x98,
  0x26,  0x95,  0x24,  0x88,  0x73,  0x04,  0x00,  0x63,  0xF3,  0x04,  0x75,  0x04,  0x5A,  0x88,  0x02,  0x01,
  0x04,  0xD8,  0x46,  0x97,  0x04,  0x98,  0x26,  0x95,  0x4A,  0x88,  0x75,  0x00,  0x00,  0xA3,  0x64,  0x08,
  0x00,  0x05,  0x4E,  0x88,  0x73,  0x04,  0x00,  0x63,  0x80,  0x7B,  0x80,  0x63,  0x06,  0xA6,  0x76,  0x08,
  0x00,  0x33,  0x3E,  0x00,  0xC2,  0x88,  0x80,  0x67,  0x83,  0x03,  0x80,  0x63,  0x00,  0x63,  0x38,  0x2B,
  0x9C,  0x88,  0x38,  0x2B,  0x92,  0x88,  0x32,  0x09,  0x31,  0x05,  0x92,  0x98,  0x05,  0x05,  0xB2,  0x09,
  0x00,  0x63,  0x00,  0x32,  0x00,  0x36,  0x00,  0x3A,  0x00,  0x3E,  0x00,  0x63,  0x80,  0x32,  0x80,  0x36,
  0x80,  0x3A,  0x80,  0x3E,  0xB4,  0x3D,  0x00,  0x63,  0x38,  0x2B,  0x40,  0x32,  0x40,  0x36,  0x40,  0x3A,
  0x40,  0x3E,  0x00,  0x63,  0x5A,  0x20,  0xC9,  0x40,  0x00,  0xA0,  0xB4,  0x08,  0x5D,  0x00,  0xFE,  0xC3,
  0x00,  0x63,  0x80,  0x73,  0xE6,  0x20,  0x02,  0x23,  0xE8,  0x00,  0x82,  0x73,  0xFF,  0xFD,  0x80,  0x73,
  0x13,  0x23,  0xF8,  0x88,  0x66,  0x20,  0xC0,  0x20,  0x04,  0x23,  0xA0,  0x01,  0xA1,  0x23,  0xA1,  0x01,
  0x81,  0x62,  0xE2,  0x88,  0x80,  0x73,  0x80,  0x77,  0x68,  0x00,  0x00,  0xA2,  0x80,  0x00,  0x03,  0xC2,
  0xF1,  0xC7,  0x41,  0x23,  0xF8,  0x88,  0x11,  0x23,  0xA1,  0x01,  0x04,  0x23,  0xA0,  0x01,  0xE6,  0x84,
};

STATIC ushort _asc_mcode_size = sizeof(_asc_mcode_buf);
STATIC ADV_DCNT _asc_mcode_chksum = 0x012C453FUL;

#define ASC_SYN_OFFSET_ONE_DISABLE_LIST  16
STATIC uchar _syn_offset_one_disable_cmd[ASC_SYN_OFFSET_ONE_DISABLE_LIST] =
{
    INQUIRY,
    REQUEST_SENSE,
    READ_CAPACITY,
    READ_TOC,
    MODE_SELECT,
    MODE_SENSE,
    MODE_SELECT_10,
    MODE_SENSE_10,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF
};

STATIC int
AscExeScsiQueue(
                   ASC_DVC_VAR *asc_dvc,
                   ASC_SCSI_Q *scsiq
)
{
    PortAddr            iop_base;
    ulong               last_int_level;
    int                 sta;
    int                 n_q_required;
    int                 disable_syn_offset_one_fix;
    int                 i;
    ASC_PADDR           addr;
    ASC_EXE_CALLBACK    asc_exe_callback;
    ushort              sg_entry_cnt = 0;
    ushort              sg_entry_cnt_minus_one = 0;
    uchar               target_ix;
    uchar               tid_no;
    uchar               sdtr_data;
    uchar               extra_bytes;
    uchar               scsi_cmd;
    uchar               disable_cmd;
    ASC_SG_HEAD         *sg_head;
    ASC_DCNT            data_cnt;

    iop_base = asc_dvc->iop_base;
    sg_head = scsiq->sg_head;
    asc_exe_callback = asc_dvc->exe_callback;
    if (asc_dvc->err_code != 0)
        return (ERR);
    if (scsiq == (ASC_SCSI_Q *) 0L) {
        AscSetLibErrorCode(asc_dvc, ASCQ_ERR_SCSIQ_NULL_PTR);
        return (ERR);
    }
    scsiq->q1.q_no = 0;
    if ((scsiq->q2.tag_code & ASC_TAG_FLAG_EXTRA_BYTES) == 0) {
        scsiq->q1.extra_bytes = 0;
    }
    sta = 0;
    target_ix = scsiq->q2.target_ix;
    tid_no = ASC_TIX_TO_TID(target_ix);
    n_q_required = 1;
    if (scsiq->cdbptr[0] == REQUEST_SENSE) {
        if ((asc_dvc->init_sdtr & scsiq->q1.target_id) != 0) {
            asc_dvc->sdtr_done &= ~scsiq->q1.target_id;
            sdtr_data = AscGetMCodeInitSDTRAtID(iop_base, tid_no);
            AscMsgOutSDTR(asc_dvc,
                          asc_dvc->sdtr_period_tbl[(sdtr_data >> 4) &
                          (uchar) (asc_dvc->max_sdtr_index - 1)],
                          (uchar) (sdtr_data & (uchar) ASC_SYN_MAX_OFFSET));
            scsiq->q1.cntl |= (QC_MSG_OUT | QC_URGENT);
        }
    }
    last_int_level = DvcEnterCritical();
    if (asc_dvc->in_critical_cnt != 0) {
        DvcLeaveCritical(last_int_level);
        AscSetLibErrorCode(asc_dvc, ASCQ_ERR_CRITICAL_RE_ENTRY);
        return (ERR);
    }
    asc_dvc->in_critical_cnt++;
    if ((scsiq->q1.cntl & QC_SG_HEAD) != 0) {
        if ((sg_entry_cnt = sg_head->entry_cnt) == 0) {
            asc_dvc->in_critical_cnt--;
            DvcLeaveCritical(last_int_level);
            return (ERR);
        }
#if !CC_VERY_LONG_SG_LIST
        if (sg_entry_cnt > ASC_MAX_SG_LIST)
        {
            asc_dvc->in_critical_cnt--;
            DvcLeaveCritical(last_int_level);
            return(ERR);
        }
#endif /* !CC_VERY_LONG_SG_LIST */
        if (sg_entry_cnt == 1) {
            scsiq->q1.data_addr = (ADV_PADDR) sg_head->sg_list[0].addr;
            scsiq->q1.data_cnt = (ADV_DCNT) sg_head->sg_list[0].bytes;
            scsiq->q1.cntl &= ~(QC_SG_HEAD | QC_SG_SWAP_QUEUE);
        }
        sg_entry_cnt_minus_one = sg_entry_cnt - 1;
    }
    scsi_cmd = scsiq->cdbptr[0];
    disable_syn_offset_one_fix = FALSE;
    if ((asc_dvc->pci_fix_asyn_xfer & scsiq->q1.target_id) &&
        !(asc_dvc->pci_fix_asyn_xfer_always & scsiq->q1.target_id)) {
        if (scsiq->q1.cntl & QC_SG_HEAD) {
            data_cnt = 0;
            for (i = 0; i < sg_entry_cnt; i++) {
                data_cnt += (ADV_DCNT) le32_to_cpu(sg_head->sg_list[i].bytes);
            }
        } else {
            data_cnt = le32_to_cpu(scsiq->q1.data_cnt);
        }
        if (data_cnt != 0UL) {
            if (data_cnt < 512UL) {
                disable_syn_offset_one_fix = TRUE;
            } else {
                for (i = 0; i < ASC_SYN_OFFSET_ONE_DISABLE_LIST; i++) {
                    disable_cmd = _syn_offset_one_disable_cmd[i];
                    if (disable_cmd == 0xFF) {
                        break;
                    }
                    if (scsi_cmd == disable_cmd) {
                        disable_syn_offset_one_fix = TRUE;
                        break;
                    }
                }
            }
        }
    }
    if (disable_syn_offset_one_fix) {
        scsiq->q2.tag_code &= ~MSG_SIMPLE_TAG;
        scsiq->q2.tag_code |= (ASC_TAG_FLAG_DISABLE_ASYN_USE_SYN_FIX |
                               ASC_TAG_FLAG_DISABLE_DISCONNECT);
    } else {
        scsiq->q2.tag_code &= 0x27;
    }
    if ((scsiq->q1.cntl & QC_SG_HEAD) != 0) {
        if (asc_dvc->bug_fix_cntl) {
            if (asc_dvc->bug_fix_cntl & ASC_BUG_FIX_IF_NOT_DWB) {
                if ((scsi_cmd == READ_6) ||
                    (scsi_cmd == READ_10)) {
                    addr =
                        (ADV_PADDR) le32_to_cpu(
                            sg_head->sg_list[sg_entry_cnt_minus_one].addr) +
                        (ADV_DCNT) le32_to_cpu(
                            sg_head->sg_list[sg_entry_cnt_minus_one].bytes);
                    extra_bytes = (uchar) ((ushort) addr & 0x0003);
                    if ((extra_bytes != 0) &&
                        ((scsiq->q2.tag_code & ASC_TAG_FLAG_EXTRA_BYTES)
                         == 0)) {
                        scsiq->q2.tag_code |= ASC_TAG_FLAG_EXTRA_BYTES;
                        scsiq->q1.extra_bytes = extra_bytes;
                        data_cnt = le32_to_cpu(
                            sg_head->sg_list[sg_entry_cnt_minus_one].bytes);
                        data_cnt -= (ASC_DCNT) extra_bytes;
                        sg_head->sg_list[sg_entry_cnt_minus_one].bytes =
                            cpu_to_le32(data_cnt);
                    }
                }
            }
        }
        sg_head->entry_to_copy = sg_head->entry_cnt;
#if CC_VERY_LONG_SG_LIST
        /*
         * Set the sg_entry_cnt to the maximum possible. The rest of
         * the SG elements will be copied when the RISC completes the
         * SG elements that fit and halts.
         */
        if (sg_entry_cnt > ASC_MAX_SG_LIST)
        {
             sg_entry_cnt = ASC_MAX_SG_LIST;
        }
#endif /* CC_VERY_LONG_SG_LIST */
        n_q_required = AscSgListToQueue(sg_entry_cnt);
        if ((AscGetNumOfFreeQueue(asc_dvc, target_ix, n_q_required) >=
            (uint) n_q_required) || ((scsiq->q1.cntl & QC_URGENT) != 0)) {
            if ((sta = AscSendScsiQueue(asc_dvc, scsiq,
                                        n_q_required)) == 1) {
                asc_dvc->in_critical_cnt--;
                if (asc_exe_callback != 0) {
                    (*asc_exe_callback) (asc_dvc, scsiq);
                }
                DvcLeaveCritical(last_int_level);
                return (sta);
            }
        }
    } else {
        if (asc_dvc->bug_fix_cntl) {
            if (asc_dvc->bug_fix_cntl & ASC_BUG_FIX_IF_NOT_DWB) {
                if ((scsi_cmd == READ_6) ||
                    (scsi_cmd == READ_10)) {
                    addr = le32_to_cpu(scsiq->q1.data_addr) +
                        le32_to_cpu(scsiq->q1.data_cnt);
                    extra_bytes = (uchar) ((ushort) addr & 0x0003);
                    if ((extra_bytes != 0) &&
                        ((scsiq->q2.tag_code & ASC_TAG_FLAG_EXTRA_BYTES)
                          == 0)) {
                        data_cnt = le32_to_cpu(scsiq->q1.data_cnt);
                        if (((ushort) data_cnt & 0x01FF) == 0) {
                            scsiq->q2.tag_code |= ASC_TAG_FLAG_EXTRA_BYTES;
                            data_cnt -= (ASC_DCNT) extra_bytes;
                            scsiq->q1.data_cnt = cpu_to_le32(data_cnt);
                            scsiq->q1.extra_bytes = extra_bytes;
                        }
                    }
                }
            }
        }
        n_q_required = 1;
        if ((AscGetNumOfFreeQueue(asc_dvc, target_ix, 1) >= 1) ||
            ((scsiq->q1.cntl & QC_URGENT) != 0)) {
            if ((sta = AscSendScsiQueue(asc_dvc, scsiq,
                                        n_q_required)) == 1) {
                asc_dvc->in_critical_cnt--;
                if (asc_exe_callback != 0) {
                    (*asc_exe_callback) (asc_dvc, scsiq);
                }
                DvcLeaveCritical(last_int_level);
                return (sta);
            }
        }
    }
    asc_dvc->in_critical_cnt--;
    DvcLeaveCritical(last_int_level);
    return (sta);
}

STATIC int
AscSendScsiQueue(
                    ASC_DVC_VAR *asc_dvc,
                    ASC_SCSI_Q *scsiq,
                    uchar n_q_required
)
{
    PortAddr            iop_base;
    uchar               free_q_head;
    uchar               next_qp;
    uchar               tid_no;
    uchar               target_ix;
    int                 sta;

    iop_base = asc_dvc->iop_base;
    target_ix = scsiq->q2.target_ix;
    tid_no = ASC_TIX_TO_TID(target_ix);
    sta = 0;
    free_q_head = (uchar) AscGetVarFreeQHead(iop_base);
    if (n_q_required > 1) {
        if ((next_qp = AscAllocMultipleFreeQueue(iop_base,
                                       free_q_head, (uchar) (n_q_required)))
            != (uchar) ASC_QLINK_END) {
            asc_dvc->last_q_shortage = 0;
            scsiq->sg_head->queue_cnt = n_q_required - 1;
            scsiq->q1.q_no = free_q_head;
            if ((sta = AscPutReadySgListQueue(asc_dvc, scsiq,
                                              free_q_head)) == 1) {
                AscPutVarFreeQHead(iop_base, next_qp);
                asc_dvc->cur_total_qng += (uchar) (n_q_required);
                asc_dvc->cur_dvc_qng[tid_no]++;
            }
            return (sta);
        }
    } else if (n_q_required == 1) {
        if ((next_qp = AscAllocFreeQueue(iop_base,
                                         free_q_head)) != ASC_QLINK_END) {
            scsiq->q1.q_no = free_q_head;
            if ((sta = AscPutReadyQueue(asc_dvc, scsiq,
                                        free_q_head)) == 1) {
                AscPutVarFreeQHead(iop_base, next_qp);
                asc_dvc->cur_total_qng++;
                asc_dvc->cur_dvc_qng[tid_no]++;
            }
            return (sta);
        }
    }
    return (sta);
}

STATIC int
AscSgListToQueue(
                    int sg_list
)
{
    int                 n_sg_list_qs;

    n_sg_list_qs = ((sg_list - 1) / ASC_SG_LIST_PER_Q);
    if (((sg_list - 1) % ASC_SG_LIST_PER_Q) != 0)
        n_sg_list_qs++;
    return (n_sg_list_qs + 1);
}


STATIC uint
AscGetNumOfFreeQueue(
                        ASC_DVC_VAR *asc_dvc,
                        uchar target_ix,
                        uchar n_qs
)
{
    uint                cur_used_qs;
    uint                cur_free_qs;
    ASC_SCSI_BIT_ID_TYPE target_id;
    uchar               tid_no;

    target_id = ASC_TIX_TO_TARGET_ID(target_ix);
    tid_no = ASC_TIX_TO_TID(target_ix);
    if ((asc_dvc->unit_not_ready & target_id) ||
        (asc_dvc->queue_full_or_busy & target_id)) {
        return (0);
    }
    if (n_qs == 1) {
        cur_used_qs = (uint) asc_dvc->cur_total_qng +
          (uint) asc_dvc->last_q_shortage +
          (uint) ASC_MIN_FREE_Q;
    } else {
        cur_used_qs = (uint) asc_dvc->cur_total_qng +
          (uint) ASC_MIN_FREE_Q;
    }
    if ((uint) (cur_used_qs + n_qs) <= (uint) asc_dvc->max_total_qng) {
        cur_free_qs = (uint) asc_dvc->max_total_qng - cur_used_qs;
        if (asc_dvc->cur_dvc_qng[tid_no] >=
            asc_dvc->max_dvc_qng[tid_no]) {
            return (0);
        }
        return (cur_free_qs);
    }
    if (n_qs > 1) {
        if ((n_qs > asc_dvc->last_q_shortage) && (n_qs <= (asc_dvc->max_total_qng - ASC_MIN_FREE_Q))) {
            asc_dvc->last_q_shortage = n_qs;
        }
    }
    return (0);
}

STATIC int
AscPutReadyQueue(
                    ASC_DVC_VAR *asc_dvc,
                    ASC_SCSI_Q *scsiq,
                    uchar q_no
)
{
    ushort              q_addr;
    uchar               tid_no;
    uchar               sdtr_data;
    uchar               syn_period_ix;
    uchar               syn_offset;
    PortAddr            iop_base;

    iop_base = asc_dvc->iop_base;
    if (((asc_dvc->init_sdtr & scsiq->q1.target_id) != 0) &&
        ((asc_dvc->sdtr_done & scsiq->q1.target_id) == 0)) {
        tid_no = ASC_TIX_TO_TID(scsiq->q2.target_ix);
        sdtr_data = AscGetMCodeInitSDTRAtID(iop_base, tid_no);
        syn_period_ix = (sdtr_data >> 4) & (asc_dvc->max_sdtr_index - 1);
        syn_offset = sdtr_data & ASC_SYN_MAX_OFFSET;
        AscMsgOutSDTR(asc_dvc,
                      asc_dvc->sdtr_period_tbl[syn_period_ix],
                      syn_offset);
        scsiq->q1.cntl |= QC_MSG_OUT;
    }
    q_addr = ASC_QNO_TO_QADDR(q_no);
    if ((scsiq->q1.target_id & asc_dvc->use_tagged_qng) == 0) {
        scsiq->q2.tag_code &= ~MSG_SIMPLE_TAG ;
    }
    scsiq->q1.status = QS_FREE;
    AscMemWordCopyPtrToLram(iop_base,
                         q_addr + ASC_SCSIQ_CDB_BEG,
                         (uchar *) scsiq->cdbptr,
                         scsiq->q2.cdb_len >> 1);

    DvcPutScsiQ(iop_base,
                q_addr + ASC_SCSIQ_CPY_BEG,
                (uchar *) &scsiq->q1.cntl,
                ((sizeof(ASC_SCSIQ_1) + sizeof(ASC_SCSIQ_2)) / 2) - 1);
    AscWriteLramWord(iop_base,
                     (ushort) (q_addr + (ushort) ASC_SCSIQ_B_STATUS),
             (ushort) (((ushort) scsiq->q1.q_no << 8) | (ushort) QS_READY));
    return (1);
}

STATIC int
AscPutReadySgListQueue(
                          ASC_DVC_VAR *asc_dvc,
                          ASC_SCSI_Q *scsiq,
                          uchar q_no
)
{
    int                 sta;
    int                 i;
    ASC_SG_HEAD *sg_head;
    ASC_SG_LIST_Q       scsi_sg_q;
    ASC_DCNT            saved_data_addr;
    ASC_DCNT            saved_data_cnt;
    PortAddr            iop_base;
    ushort              sg_list_dwords;
    ushort              sg_index;
    ushort              sg_entry_cnt;
    ushort              q_addr;
    uchar               next_qp;

    iop_base = asc_dvc->iop_base;
    sg_head = scsiq->sg_head;
    saved_data_addr = scsiq->q1.data_addr;
    saved_data_cnt = scsiq->q1.data_cnt;
    scsiq->q1.data_addr = (ASC_PADDR) sg_head->sg_list[0].addr;
    scsiq->q1.data_cnt = (ASC_DCNT) sg_head->sg_list[0].bytes;
#if CC_VERY_LONG_SG_LIST
    /*
     * If sg_head->entry_cnt is greater than ASC_MAX_SG_LIST
     * then not all SG elements will fit in the allocated queues.
     * The rest of the SG elements will be copied when the RISC
     * completes the SG elements that fit and halts.
     */
    if (sg_head->entry_cnt > ASC_MAX_SG_LIST)
    {
         /*
          * Set sg_entry_cnt to be the number of SG elements that
          * will fit in the allocated SG queues. It is minus 1, because
          * the first SG element is handled above. ASC_MAX_SG_LIST is
          * already inflated by 1 to account for this. For example it
          * may be 50 which is 1 + 7 queues * 7 SG elements.
          */
         sg_entry_cnt = ASC_MAX_SG_LIST - 1;

         /*
          * Keep track of remaining number of SG elements that will
          * need to be handled from a_isr.c.
          */
         scsiq->remain_sg_entry_cnt = sg_head->entry_cnt - ASC_MAX_SG_LIST;
    } else
    {
#endif /* CC_VERY_LONG_SG_LIST */
         /*
          * Set sg_entry_cnt to be the number of SG elements that
          * will fit in the allocated SG queues. It is minus 1, because
          * the first SG element is handled above.
          */
         sg_entry_cnt = sg_head->entry_cnt - 1;
#if CC_VERY_LONG_SG_LIST
    }
#endif /* CC_VERY_LONG_SG_LIST */
    if (sg_entry_cnt != 0) {
        scsiq->q1.cntl |= QC_SG_HEAD;
        q_addr = ASC_QNO_TO_QADDR(q_no);
        sg_index = 1;
        scsiq->q1.sg_queue_cnt = sg_head->queue_cnt;
        scsi_sg_q.sg_head_qp = q_no;
        scsi_sg_q.cntl = QCSG_SG_XFER_LIST;
        for (i = 0; i < sg_head->queue_cnt; i++) {
            scsi_sg_q.seq_no = i + 1;
            if (sg_entry_cnt > ASC_SG_LIST_PER_Q) {
                sg_list_dwords = (uchar) (ASC_SG_LIST_PER_Q * 2);
                sg_entry_cnt -= ASC_SG_LIST_PER_Q;
                if (i == 0) {
                    scsi_sg_q.sg_list_cnt = ASC_SG_LIST_PER_Q;
                    scsi_sg_q.sg_cur_list_cnt = ASC_SG_LIST_PER_Q;
                } else {
                    scsi_sg_q.sg_list_cnt = ASC_SG_LIST_PER_Q - 1;
                    scsi_sg_q.sg_cur_list_cnt = ASC_SG_LIST_PER_Q - 1;
                }
            } else {
#if CC_VERY_LONG_SG_LIST
                /*
                 * This is the last SG queue in the list of
                 * allocated SG queues. If there are more
                 * SG elements than will fit in the allocated
                 * queues, then set the QCSG_SG_XFER_MORE flag.
                 */
                if (sg_head->entry_cnt > ASC_MAX_SG_LIST)
                {
                    scsi_sg_q.cntl |= QCSG_SG_XFER_MORE;
                } else
                {
#endif /* CC_VERY_LONG_SG_LIST */
                    scsi_sg_q.cntl |= QCSG_SG_XFER_END;
#if CC_VERY_LONG_SG_LIST
                }
#endif /* CC_VERY_LONG_SG_LIST */
                sg_list_dwords = sg_entry_cnt << 1;
                if (i == 0) {
                    scsi_sg_q.sg_list_cnt = sg_entry_cnt;
                    scsi_sg_q.sg_cur_list_cnt = sg_entry_cnt;
                } else {
                    scsi_sg_q.sg_list_cnt = sg_entry_cnt - 1;
                    scsi_sg_q.sg_cur_list_cnt = sg_entry_cnt - 1;
                }
                sg_entry_cnt = 0;
            }
            next_qp = AscReadLramByte(iop_base,
                                      (ushort) (q_addr + ASC_SCSIQ_B_FWD));
            scsi_sg_q.q_no = next_qp;
            q_addr = ASC_QNO_TO_QADDR(next_qp);
            AscMemWordCopyPtrToLram(iop_base,
                                q_addr + ASC_SCSIQ_SGHD_CPY_BEG,
                                (uchar *) &scsi_sg_q,
                                sizeof(ASC_SG_LIST_Q) >> 1);
            AscMemDWordCopyPtrToLram(iop_base,
                                q_addr + ASC_SGQ_LIST_BEG,
                                (uchar *) &sg_head->sg_list[sg_index],
                                sg_list_dwords);
            sg_index += ASC_SG_LIST_PER_Q;
            scsiq->next_sg_index = sg_index;
        }
    } else {
        scsiq->q1.cntl &= ~QC_SG_HEAD;
    }
    sta = AscPutReadyQueue(asc_dvc, scsiq, q_no);
    scsiq->q1.data_addr = saved_data_addr;
    scsiq->q1.data_cnt = saved_data_cnt;
    return (sta);
}

STATIC int
AscSetRunChipSynRegAtID(
                           PortAddr iop_base,
                           uchar tid_no,
                           uchar sdtr_data
)
{
    int                 sta = FALSE;

    if (AscHostReqRiscHalt(iop_base)) {
        sta = AscSetChipSynRegAtID(iop_base, tid_no, sdtr_data);
        AscStartChip(iop_base);
        return (sta);
    }
    return (sta);
}

STATIC int
AscSetChipSynRegAtID(
                        PortAddr iop_base,
                        uchar id,
                        uchar sdtr_data
)
{
    ASC_SCSI_BIT_ID_TYPE org_id;
    int                 i;
    int                 sta = TRUE;

    AscSetBank(iop_base, 1);
    org_id = AscReadChipDvcID(iop_base);
    for (i = 0; i <= ASC_MAX_TID; i++) {
        if (org_id == (0x01 << i))
            break;
    }
    org_id = (ASC_SCSI_BIT_ID_TYPE) i;
    AscWriteChipDvcID(iop_base, id);
    if (AscReadChipDvcID(iop_base) == (0x01 << id)) {
        AscSetBank(iop_base, 0);
        AscSetChipSyn(iop_base, sdtr_data);
        if (AscGetChipSyn(iop_base) != sdtr_data) {
            sta = FALSE;
        }
    } else {
        sta = FALSE;
    }
    AscSetBank(iop_base, 1);
    AscWriteChipDvcID(iop_base, org_id);
    AscSetBank(iop_base, 0);
    return (sta);
}

STATIC ushort
AscInitLram(
               ASC_DVC_VAR *asc_dvc
)
{
    uchar               i;
    ushort              s_addr;
    PortAddr            iop_base;
    ushort              warn_code;

    iop_base = asc_dvc->iop_base;
    warn_code = 0;
    AscMemWordSetLram(iop_base, ASC_QADR_BEG, 0,
               (ushort) (((int) (asc_dvc->max_total_qng + 2 + 1) * 64) >> 1)
);
    i = ASC_MIN_ACTIVE_QNO;
    s_addr = ASC_QADR_BEG + ASC_QBLK_SIZE;
    AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_FWD),
                     (uchar) (i + 1));
    AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_BWD),
                     (uchar) (asc_dvc->max_total_qng));
    AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_QNO),
                     (uchar) i);
    i++;
    s_addr += ASC_QBLK_SIZE;
    for (; i < asc_dvc->max_total_qng; i++, s_addr += ASC_QBLK_SIZE) {
        AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_FWD),
                         (uchar) (i + 1));
        AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_BWD),
                         (uchar) (i - 1));
        AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_QNO),
                         (uchar) i);
    }
    AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_FWD),
                     (uchar) ASC_QLINK_END);
    AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_BWD),
                     (uchar) (asc_dvc->max_total_qng - 1));
    AscWriteLramByte(iop_base, (ushort) (s_addr + ASC_SCSIQ_B_QNO),
                     (uchar) asc_dvc->max_total_qng);
    i++;
    s_addr += ASC_QBLK_SIZE;
    for (; i <= (uchar) (asc_dvc->max_total_qng + 3);
         i++, s_addr += ASC_QBLK_SIZE) {
        AscWriteLramByte(iop_base,
                         (ushort) (s_addr + (ushort) ASC_SCSIQ_B_FWD), i);
        AscWriteLramByte(iop_base,
                         (ushort) (s_addr + (ushort) ASC_SCSIQ_B_BWD), i);
        AscWriteLramByte(iop_base,
                         (ushort) (s_addr + (ushort) ASC_SCSIQ_B_QNO), i);
    }
    return (warn_code);
}

STATIC ushort
AscInitQLinkVar(
                   ASC_DVC_VAR *asc_dvc
)
{
    PortAddr            iop_base;
    int                 i;
    ushort              lram_addr;

    iop_base = asc_dvc->iop_base;
    AscPutRiscVarFreeQHead(iop_base, 1);
    AscPutRiscVarDoneQTail(iop_base, asc_dvc->max_total_qng);
    AscPutVarFreeQHead(iop_base, 1);
    AscPutVarDoneQTail(iop_base, asc_dvc->max_total_qng);
    AscWriteLramByte(iop_base, ASCV_BUSY_QHEAD_B,
                     (uchar) ((int) asc_dvc->max_total_qng + 1));
    AscWriteLramByte(iop_base, ASCV_DISC1_QHEAD_B,
                     (uchar) ((int) asc_dvc->max_total_qng + 2));
    AscWriteLramByte(iop_base, (ushort) ASCV_TOTAL_READY_Q_B,
                     asc_dvc->max_total_qng);
    AscWriteLramWord(iop_base, ASCV_ASCDVC_ERR_CODE_W, 0);
    AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
    AscWriteLramByte(iop_base, ASCV_STOP_CODE_B, 0);
    AscWriteLramByte(iop_base, ASCV_SCSIBUSY_B, 0);
    AscWriteLramByte(iop_base, ASCV_WTM_FLAG_B, 0);
    AscPutQDoneInProgress(iop_base, 0);
    lram_addr = ASC_QADR_BEG;
    for (i = 0; i < 32; i++, lram_addr += 2) {
        AscWriteLramWord(iop_base, lram_addr, 0);
    }
    return (0);
}

STATIC int
AscSetLibErrorCode(
                      ASC_DVC_VAR *asc_dvc,
                      ushort err_code
)
{
    if (asc_dvc->err_code == 0) {
        asc_dvc->err_code = err_code;
        AscWriteLramWord(asc_dvc->iop_base, ASCV_ASCDVC_ERR_CODE_W,
                         err_code);
    }
    return (err_code);
}


STATIC uchar
AscMsgOutSDTR(
                 ASC_DVC_VAR *asc_dvc,
                 uchar sdtr_period,
                 uchar sdtr_offset
)
{
    EXT_MSG             sdtr_buf;
    uchar               sdtr_period_index;
    PortAddr            iop_base;

    iop_base = asc_dvc->iop_base;
    sdtr_buf.msg_type = MS_EXTEND;
    sdtr_buf.msg_len = MS_SDTR_LEN;
    sdtr_buf.msg_req = MS_SDTR_CODE;
    sdtr_buf.xfer_period = sdtr_period;
    sdtr_offset &= ASC_SYN_MAX_OFFSET;
    sdtr_buf.req_ack_offset = sdtr_offset;
    if ((sdtr_period_index =
         AscGetSynPeriodIndex(asc_dvc, sdtr_period)) <=
        asc_dvc->max_sdtr_index) {
        AscMemWordCopyPtrToLram(iop_base,
                             ASCV_MSGOUT_BEG,
                             (uchar *) &sdtr_buf,
                             sizeof (EXT_MSG) >> 1);
        return ((sdtr_period_index << 4) | sdtr_offset);
    } else {

        sdtr_buf.req_ack_offset = 0;
        AscMemWordCopyPtrToLram(iop_base,
                             ASCV_MSGOUT_BEG,
                             (uchar *) &sdtr_buf,
                             sizeof (EXT_MSG) >> 1);
        return (0);
    }
}

STATIC uchar
AscCalSDTRData(
                  ASC_DVC_VAR *asc_dvc,
                  uchar sdtr_period,
                  uchar syn_offset
)
{
    uchar               byte;
    uchar               sdtr_period_ix;

    sdtr_period_ix = AscGetSynPeriodIndex(asc_dvc, sdtr_period);
    if (
           (sdtr_period_ix > asc_dvc->max_sdtr_index)
) {
        return (0xFF);
    }
    byte = (sdtr_period_ix << 4) | (syn_offset & ASC_SYN_MAX_OFFSET);
    return (byte);
}

STATIC void
AscSetChipSDTR(
                  PortAddr iop_base,
                  uchar sdtr_data,
                  uchar tid_no
)
{
    AscSetChipSynRegAtID(iop_base, tid_no, sdtr_data);
    AscPutMCodeSDTRDoneAtID(iop_base, tid_no, sdtr_data);
    return;
}

STATIC uchar
AscGetSynPeriodIndex(
                        ASC_DVC_VAR *asc_dvc,
                        uchar syn_time
)
{
    uchar             *period_table;
    int                 max_index;
    int                 min_index;
    int                 i;

    period_table = asc_dvc->sdtr_period_tbl;
    max_index = (int) asc_dvc->max_sdtr_index;
    min_index = (int)asc_dvc->host_init_sdtr_index;
    if ((syn_time <= period_table[max_index])) {
        for (i = min_index; i < (max_index - 1); i++) {
            if (syn_time <= period_table[i]) {
                return ((uchar) i);
            }
        }
        return ((uchar) max_index);
    } else {
        return ((uchar) (max_index + 1));
    }
}

STATIC uchar
AscAllocFreeQueue(
                     PortAddr iop_base,
                     uchar free_q_head
)
{
    ushort              q_addr;
    uchar               next_qp;
    uchar               q_status;

    q_addr = ASC_QNO_TO_QADDR(free_q_head);
    q_status = (uchar) AscReadLramByte(iop_base,
                                    (ushort) (q_addr + ASC_SCSIQ_B_STATUS));
    next_qp = AscReadLramByte(iop_base,
                              (ushort) (q_addr + ASC_SCSIQ_B_FWD));
    if (((q_status & QS_READY) == 0) && (next_qp != ASC_QLINK_END)) {
        return (next_qp);
    }
    return (ASC_QLINK_END);
}

STATIC uchar
AscAllocMultipleFreeQueue(
                             PortAddr iop_base,
                             uchar free_q_head,
                             uchar n_free_q
)
{
    uchar               i;

    for (i = 0; i < n_free_q; i++) {
        if ((free_q_head = AscAllocFreeQueue(iop_base, free_q_head))
            == ASC_QLINK_END) {
            return (ASC_QLINK_END);
        }
    }
    return (free_q_head);
}

STATIC int
AscHostReqRiscHalt(
                      PortAddr iop_base
)
{
    int                 count = 0;
    int                 sta = 0;
    uchar               saved_stop_code;

    if (AscIsChipHalted(iop_base))
        return (1);
    saved_stop_code = AscReadLramByte(iop_base, ASCV_STOP_CODE_B);
    AscWriteLramByte(iop_base, ASCV_STOP_CODE_B,
                     ASC_STOP_HOST_REQ_RISC_HALT | ASC_STOP_REQ_RISC_STOP
);
    do {
        if (AscIsChipHalted(iop_base)) {
            sta = 1;
            break;
        }
        DvcSleepMilliSecond(100);
    } while (count++ < 20);
    AscWriteLramByte(iop_base, ASCV_STOP_CODE_B, saved_stop_code);
    return (sta);
}

STATIC int
AscStopQueueExe(
                   PortAddr iop_base
)
{
    int                 count = 0;

    if (AscReadLramByte(iop_base, ASCV_STOP_CODE_B) == 0) {
        AscWriteLramByte(iop_base, ASCV_STOP_CODE_B,
                         ASC_STOP_REQ_RISC_STOP);
        do {
            if (
                   AscReadLramByte(iop_base, ASCV_STOP_CODE_B) &
                   ASC_STOP_ACK_RISC_STOP) {
                return (1);
            }
            DvcSleepMilliSecond(100);
        } while (count++ < 20);
    }
    return (0);
}

STATIC void
DvcDelayMicroSecond(ADV_DVC_VAR *asc_dvc, ushort micro_sec)
{
    udelay(micro_sec);
}

STATIC void
DvcDelayNanoSecond(ASC_DVC_VAR *asc_dvc, ASC_DCNT nano_sec)
{
    udelay((nano_sec + 999)/1000);
}

#ifdef CONFIG_ISA
STATIC ASC_DCNT __init
AscGetEisaProductID(
                       PortAddr iop_base)
{
    PortAddr            eisa_iop;
    ushort              product_id_high, product_id_low;
    ASC_DCNT            product_id;

    eisa_iop = ASC_GET_EISA_SLOT(iop_base) | ASC_EISA_PID_IOP_MASK;
    product_id_low = inpw(eisa_iop);
    product_id_high = inpw(eisa_iop + 2);
    product_id = ((ASC_DCNT) product_id_high << 16) |
        (ASC_DCNT) product_id_low;
    return (product_id);
}

STATIC PortAddr __init
AscSearchIOPortAddrEISA(
                           PortAddr iop_base)
{
    ASC_DCNT            eisa_product_id;

    if (iop_base == 0) {
        iop_base = ASC_EISA_MIN_IOP_ADDR;
    } else {
        if (iop_base == ASC_EISA_MAX_IOP_ADDR)
            return (0);
        if ((iop_base & 0x0050) == 0x0050) {
            iop_base += ASC_EISA_BIG_IOP_GAP;
        } else {
            iop_base += ASC_EISA_SMALL_IOP_GAP;
        }
    }
    while (iop_base <= ASC_EISA_MAX_IOP_ADDR) {
        eisa_product_id = AscGetEisaProductID(iop_base);
        if ((eisa_product_id == ASC_EISA_ID_740) ||
            (eisa_product_id == ASC_EISA_ID_750)) {
            if (AscFindSignature(iop_base)) {
                inpw(iop_base + 4);
                return (iop_base);
            }
        }
        if (iop_base == ASC_EISA_MAX_IOP_ADDR)
            return (0);
        if ((iop_base & 0x0050) == 0x0050) {
            iop_base += ASC_EISA_BIG_IOP_GAP;
        } else {
            iop_base += ASC_EISA_SMALL_IOP_GAP;
        }
    }
    return (0);
}
#endif /* CONFIG_ISA */

STATIC int
AscStartChip(
                PortAddr iop_base
)
{
    AscSetChipControl(iop_base, 0);
    if ((AscGetChipStatus(iop_base) & CSW_HALTED) != 0) {
        return (0);
    }
    return (1);
}

STATIC int
AscStopChip(
               PortAddr iop_base
)
{
    uchar               cc_val;

    cc_val = AscGetChipControl(iop_base) & (~(CC_SINGLE_STEP | CC_TEST | CC_DIAG));
    AscSetChipControl(iop_base, (uchar) (cc_val | CC_HALT));
    AscSetChipIH(iop_base, INS_HALT);
    AscSetChipIH(iop_base, INS_RFLAG_WTM);
    if ((AscGetChipStatus(iop_base) & CSW_HALTED) == 0) {
        return (0);
    }
    return (1);
}

STATIC int
AscIsChipHalted(
                   PortAddr iop_base
)
{
    if ((AscGetChipStatus(iop_base) & CSW_HALTED) != 0) {
        if ((AscGetChipControl(iop_base) & CC_HALT) != 0) {
            return (1);
        }
    }
    return (0);
}

STATIC void
AscSetChipIH(
                PortAddr iop_base,
                ushort ins_code
)
{
    AscSetBank(iop_base, 1);
    AscWriteChipIH(iop_base, ins_code);
    AscSetBank(iop_base, 0);
    return;
}

STATIC void
AscAckInterrupt(
                   PortAddr iop_base
)
{
    uchar               host_flag;
    uchar               risc_flag;
    ushort              loop;

    loop = 0;
    do {
        risc_flag = AscReadLramByte(iop_base, ASCV_RISC_FLAG_B);
        if (loop++ > 0x7FFF) {
            break;
        }
    } while ((risc_flag & ASC_RISC_FLAG_GEN_INT) != 0);
    host_flag = AscReadLramByte(iop_base, ASCV_HOST_FLAG_B) & (~ASC_HOST_FLAG_ACK_INT);
    AscWriteLramByte(iop_base, ASCV_HOST_FLAG_B,
                     (uchar) (host_flag | ASC_HOST_FLAG_ACK_INT));
    AscSetChipStatus(iop_base, CIW_INT_ACK);
    loop = 0;
    while (AscGetChipStatus(iop_base) & CSW_INT_PENDING) {
        AscSetChipStatus(iop_base, CIW_INT_ACK);
        if (loop++ > 3) {
            break;
        }
    }
    AscWriteLramByte(iop_base, ASCV_HOST_FLAG_B, host_flag);
    return;
}

STATIC void
AscDisableInterrupt(
                       PortAddr iop_base
)
{
    ushort              cfg;

    cfg = AscGetChipCfgLsw(iop_base);
    AscSetChipCfgLsw(iop_base, cfg & (~ASC_CFG0_HOST_INT_ON));
    return;
}

STATIC void
AscEnableInterrupt(
                      PortAddr iop_base
)
{
    ushort              cfg;

    cfg = AscGetChipCfgLsw(iop_base);
    AscSetChipCfgLsw(iop_base, cfg | ASC_CFG0_HOST_INT_ON);
    return;
}



STATIC void
AscSetBank(
              PortAddr iop_base,
              uchar bank
)
{
    uchar               val;

    val = AscGetChipControl(iop_base) &
      (~(CC_SINGLE_STEP | CC_TEST | CC_DIAG | CC_SCSI_RESET | CC_CHIP_RESET));
    if (bank == 1) {
        val |= CC_BANK_ONE;
    } else if (bank == 2) {
        val |= CC_DIAG | CC_BANK_ONE;
    } else {
        val &= ~CC_BANK_ONE;
    }
    AscSetChipControl(iop_base, val);
    return;
}

STATIC int
AscResetChipAndScsiBus(
                          ASC_DVC_VAR *asc_dvc
)
{
    PortAddr    iop_base;
    int         i = 10;

    iop_base = asc_dvc->iop_base;
    while ((AscGetChipStatus(iop_base) & CSW_SCSI_RESET_ACTIVE) && (i-- > 0))
    {
          DvcSleepMilliSecond(100);
    }
    AscStopChip(iop_base);
    AscSetChipControl(iop_base, CC_CHIP_RESET | CC_SCSI_RESET | CC_HALT);
    DvcDelayNanoSecond(asc_dvc, 60000);
    AscSetChipIH(iop_base, INS_RFLAG_WTM);
    AscSetChipIH(iop_base, INS_HALT);
    AscSetChipControl(iop_base, CC_CHIP_RESET | CC_HALT);
    AscSetChipControl(iop_base, CC_HALT);
    DvcSleepMilliSecond(200);
    AscSetChipStatus(iop_base, CIW_CLR_SCSI_RESET_INT);
    AscSetChipStatus(iop_base, 0);
    return (AscIsChipHalted(iop_base));
}

STATIC ASC_DCNT __init
AscGetMaxDmaCount(
                     ushort bus_type)
{
    if (bus_type & ASC_IS_ISA)
        return (ASC_MAX_ISA_DMA_COUNT);
    else if (bus_type & (ASC_IS_EISA | ASC_IS_VL))
        return (ASC_MAX_VL_DMA_COUNT);
    return (ASC_MAX_PCI_DMA_COUNT);
}

#ifdef CONFIG_ISA
STATIC ushort __init
AscGetIsaDmaChannel(
                       PortAddr iop_base)
{
    ushort              channel;

    channel = AscGetChipCfgLsw(iop_base) & 0x0003;
    if (channel == 0x03)
        return (0);
    else if (channel == 0x00)
        return (7);
    return (channel + 4);
}

STATIC ushort __init
AscSetIsaDmaChannel(
                       PortAddr iop_base,
                       ushort dma_channel)
{
    ushort              cfg_lsw;
    uchar               value;

    if ((dma_channel >= 5) && (dma_channel <= 7)) {
        if (dma_channel == 7)
            value = 0x00;
        else
            value = dma_channel - 4;
        cfg_lsw = AscGetChipCfgLsw(iop_base) & 0xFFFC;
        cfg_lsw |= value;
        AscSetChipCfgLsw(iop_base, cfg_lsw);
        return (AscGetIsaDmaChannel(iop_base));
    }
    return (0);
}

STATIC uchar __init
AscSetIsaDmaSpeed(
                     PortAddr iop_base,
                     uchar speed_value)
{
    speed_value &= 0x07;
    AscSetBank(iop_base, 1);
    AscWriteChipDmaSpeed(iop_base, speed_value);
    AscSetBank(iop_base, 0);
    return (AscGetIsaDmaSpeed(iop_base));
}

STATIC uchar __init
AscGetIsaDmaSpeed(
                     PortAddr iop_base
)
{
    uchar               speed_value;

    AscSetBank(iop_base, 1);
    speed_value = AscReadChipDmaSpeed(iop_base);
    speed_value &= 0x07;
    AscSetBank(iop_base, 0);
    return (speed_value);
}
#endif /* CONFIG_ISA */

STATIC ushort __init
AscReadPCIConfigWord(
    ASC_DVC_VAR *asc_dvc,
    ushort pci_config_offset)
{
    uchar       lsb, msb;

    lsb = DvcReadPCIConfigByte(asc_dvc, pci_config_offset);
    msb = DvcReadPCIConfigByte(asc_dvc, pci_config_offset + 1);
    return ((ushort) ((msb << 8) | lsb));
}

STATIC ushort __init
AscInitGetConfig(
        ASC_DVC_VAR *asc_dvc
)
{
    ushort              warn_code;
    PortAddr            iop_base;
    ushort              PCIDeviceID;
    ushort              PCIVendorID;
    uchar               PCIRevisionID;
    uchar               prevCmdRegBits;

    warn_code = 0;
    iop_base = asc_dvc->iop_base;
    asc_dvc->init_state = ASC_INIT_STATE_BEG_GET_CFG;
    if (asc_dvc->err_code != 0) {
        return (UW_ERR);
    }
    if (asc_dvc->bus_type == ASC_IS_PCI) {
        PCIVendorID = AscReadPCIConfigWord(asc_dvc,
                                    AscPCIConfigVendorIDRegister);

        PCIDeviceID = AscReadPCIConfigWord(asc_dvc,
                                    AscPCIConfigDeviceIDRegister);

        PCIRevisionID = DvcReadPCIConfigByte(asc_dvc,
                                    AscPCIConfigRevisionIDRegister);

        if (PCIVendorID != PCI_VENDOR_ID_ASP) {
            warn_code |= ASC_WARN_SET_PCI_CONFIG_SPACE;
        }
        prevCmdRegBits = DvcReadPCIConfigByte(asc_dvc,
                                    AscPCIConfigCommandRegister);

        if ((prevCmdRegBits & AscPCICmdRegBits_IOMemBusMaster) !=
            AscPCICmdRegBits_IOMemBusMaster) {
            DvcWritePCIConfigByte(asc_dvc,
                            AscPCIConfigCommandRegister,
                            (prevCmdRegBits |
                             AscPCICmdRegBits_IOMemBusMaster));

            if ((DvcReadPCIConfigByte(asc_dvc,
                                AscPCIConfigCommandRegister)
                 & AscPCICmdRegBits_IOMemBusMaster)
                != AscPCICmdRegBits_IOMemBusMaster) {
                warn_code |= ASC_WARN_SET_PCI_CONFIG_SPACE;
            }
        }
        if ((PCIDeviceID == PCI_DEVICE_ID_ASP_1200A) ||
            (PCIDeviceID == PCI_DEVICE_ID_ASP_ABP940)) {
            DvcWritePCIConfigByte(asc_dvc,
                            AscPCIConfigLatencyTimer, 0x00);
            if (DvcReadPCIConfigByte(asc_dvc, AscPCIConfigLatencyTimer)
                != 0x00) {
                warn_code |= ASC_WARN_SET_PCI_CONFIG_SPACE;
            }
        } else if (PCIDeviceID == PCI_DEVICE_ID_ASP_ABP940U) {
            if (DvcReadPCIConfigByte(asc_dvc,
                                AscPCIConfigLatencyTimer) < 0x20) {
                DvcWritePCIConfigByte(asc_dvc,
                                    AscPCIConfigLatencyTimer, 0x20);

                if (DvcReadPCIConfigByte(asc_dvc,
                                    AscPCIConfigLatencyTimer) < 0x20) {
                    warn_code |= ASC_WARN_SET_PCI_CONFIG_SPACE;
                }
            }
        }
    }

    if (AscFindSignature(iop_base)) {
        warn_code |= AscInitAscDvcVar(asc_dvc);
        warn_code |= AscInitFromEEP(asc_dvc);
        asc_dvc->init_state |= ASC_INIT_STATE_END_GET_CFG;
        if (asc_dvc->scsi_reset_wait > ASC_MAX_SCSI_RESET_WAIT) {
            asc_dvc->scsi_reset_wait = ASC_MAX_SCSI_RESET_WAIT;
        }
    } else {
        asc_dvc->err_code = ASC_IERR_BAD_SIGNATURE;
    }
    return(warn_code);
}

STATIC ushort __init
AscInitSetConfig(
                    ASC_DVC_VAR *asc_dvc
)
{
    ushort              warn_code = 0;

    asc_dvc->init_state |= ASC_INIT_STATE_BEG_SET_CFG;
    if (asc_dvc->err_code != 0)
        return (UW_ERR);
    if (AscFindSignature(asc_dvc->iop_base)) {
        warn_code |= AscInitFromAscDvcVar(asc_dvc);
        asc_dvc->init_state |= ASC_INIT_STATE_END_SET_CFG;
    } else {
        asc_dvc->err_code = ASC_IERR_BAD_SIGNATURE;
    }
    return (warn_code);
}

STATIC ushort __init
AscInitFromAscDvcVar(
                        ASC_DVC_VAR *asc_dvc
)
{
    PortAddr            iop_base;
    ushort              cfg_msw;
    ushort              warn_code;
    ushort              pci_device_id = 0;

    iop_base = asc_dvc->iop_base;
#ifdef CONFIG_PCI
    if (asc_dvc->cfg->dev)
        pci_device_id = to_pci_dev(asc_dvc->cfg->dev)->device;
#endif
    warn_code = 0;
    cfg_msw = AscGetChipCfgMsw(iop_base);
    if ((cfg_msw & ASC_CFG_MSW_CLR_MASK) != 0) {
        cfg_msw &= (~(ASC_CFG_MSW_CLR_MASK));
        warn_code |= ASC_WARN_CFG_MSW_RECOVER;
        AscSetChipCfgMsw(iop_base, cfg_msw);
    }
    if ((asc_dvc->cfg->cmd_qng_enabled & asc_dvc->cfg->disc_enable) !=
        asc_dvc->cfg->cmd_qng_enabled) {
        asc_dvc->cfg->disc_enable = asc_dvc->cfg->cmd_qng_enabled;
        warn_code |= ASC_WARN_CMD_QNG_CONFLICT;
    }
    if (AscGetChipStatus(iop_base) & CSW_AUTO_CONFIG) {
        warn_code |= ASC_WARN_AUTO_CONFIG;
    }
    if ((asc_dvc->bus_type & (ASC_IS_ISA | ASC_IS_VL)) != 0) {
        if (AscSetChipIRQ(iop_base, asc_dvc->irq_no, asc_dvc->bus_type)
            != asc_dvc->irq_no) {
            asc_dvc->err_code |= ASC_IERR_SET_IRQ_NO;
        }
    }
    if (asc_dvc->bus_type & ASC_IS_PCI) {
        cfg_msw &= 0xFFC0;
        AscSetChipCfgMsw(iop_base, cfg_msw);
        if ((asc_dvc->bus_type & ASC_IS_PCI_ULTRA) == ASC_IS_PCI_ULTRA) {
        } else {
            if ((pci_device_id == PCI_DEVICE_ID_ASP_1200A) ||
                (pci_device_id == PCI_DEVICE_ID_ASP_ABP940)) {
                asc_dvc->bug_fix_cntl |= ASC_BUG_FIX_IF_NOT_DWB;
                asc_dvc->bug_fix_cntl |= ASC_BUG_FIX_ASYN_USE_SYN;
            }
        }
    } else if (asc_dvc->bus_type == ASC_IS_ISAPNP) {
        if (AscGetChipVersion(iop_base, asc_dvc->bus_type)
            == ASC_CHIP_VER_ASYN_BUG) {
            asc_dvc->bug_fix_cntl |= ASC_BUG_FIX_ASYN_USE_SYN;
        }
    }
    if (AscSetChipScsiID(iop_base, asc_dvc->cfg->chip_scsi_id) !=
        asc_dvc->cfg->chip_scsi_id) {
        asc_dvc->err_code |= ASC_IERR_SET_SCSI_ID;
    }
#ifdef CONFIG_ISA
    if (asc_dvc->bus_type & ASC_IS_ISA) {
        AscSetIsaDmaChannel(iop_base, asc_dvc->cfg->isa_dma_channel);
        AscSetIsaDmaSpeed(iop_base, asc_dvc->cfg->isa_dma_speed);
    }
#endif /* CONFIG_ISA */
    return (warn_code);
}

STATIC ushort
AscInitAsc1000Driver(
                        ASC_DVC_VAR *asc_dvc
)
{
    ushort              warn_code;
    PortAddr            iop_base;

    iop_base = asc_dvc->iop_base;
    warn_code = 0;
    if ((asc_dvc->dvc_cntl & ASC_CNTL_RESET_SCSI) &&
        !(asc_dvc->init_state & ASC_INIT_RESET_SCSI_DONE)) {
        AscResetChipAndScsiBus(asc_dvc);
        DvcSleepMilliSecond((ASC_DCNT)
            ((ushort) asc_dvc->scsi_reset_wait * 1000));
    }
    asc_dvc->init_state |= ASC_INIT_STATE_BEG_LOAD_MC;
    if (asc_dvc->err_code != 0)
        return (UW_ERR);
    if (!AscFindSignature(asc_dvc->iop_base)) {
        asc_dvc->err_code = ASC_IERR_BAD_SIGNATURE;
        return (warn_code);
    }
    AscDisableInterrupt(iop_base);
    warn_code |= AscInitLram(asc_dvc);
    if (asc_dvc->err_code != 0)
        return (UW_ERR);
    ASC_DBG1(1, "AscInitAsc1000Driver: _asc_mcode_chksum 0x%lx\n",
        (ulong) _asc_mcode_chksum);
    if (AscLoadMicroCode(iop_base, 0, _asc_mcode_buf,
                         _asc_mcode_size) != _asc_mcode_chksum) {
        asc_dvc->err_code |= ASC_IERR_MCODE_CHKSUM;
        return (warn_code);
    }
    warn_code |= AscInitMicroCodeVar(asc_dvc);
    asc_dvc->init_state |= ASC_INIT_STATE_END_LOAD_MC;
    AscEnableInterrupt(iop_base);
    return (warn_code);
}

STATIC ushort __init
AscInitAscDvcVar(
                    ASC_DVC_VAR *asc_dvc)
{
    int                 i;
    PortAddr            iop_base;
    ushort              warn_code;
    uchar               chip_version;

    iop_base = asc_dvc->iop_base;
    warn_code = 0;
    asc_dvc->err_code = 0;
    if ((asc_dvc->bus_type &
         (ASC_IS_ISA | ASC_IS_PCI | ASC_IS_EISA | ASC_IS_VL)) == 0) {
        asc_dvc->err_code |= ASC_IERR_NO_BUS_TYPE;
    }
    AscSetChipControl(iop_base, CC_HALT);
    AscSetChipStatus(iop_base, 0);
    asc_dvc->bug_fix_cntl = 0;
    asc_dvc->pci_fix_asyn_xfer = 0;
    asc_dvc->pci_fix_asyn_xfer_always = 0;
    /* asc_dvc->init_state initalized in AscInitGetConfig(). */
    asc_dvc->sdtr_done = 0;
    asc_dvc->cur_total_qng = 0;
    asc_dvc->is_in_int = 0;
    asc_dvc->in_critical_cnt = 0;
    asc_dvc->last_q_shortage = 0;
    asc_dvc->use_tagged_qng = 0;
    asc_dvc->no_scam = 0;
    asc_dvc->unit_not_ready = 0;
    asc_dvc->queue_full_or_busy = 0;
    asc_dvc->redo_scam = 0;
    asc_dvc->res2 = 0;
    asc_dvc->host_init_sdtr_index = 0;
    asc_dvc->cfg->can_tagged_qng = 0;
    asc_dvc->cfg->cmd_qng_enabled = 0;
    asc_dvc->dvc_cntl = ASC_DEF_DVC_CNTL;
    asc_dvc->init_sdtr = 0;
    asc_dvc->max_total_qng = ASC_DEF_MAX_TOTAL_QNG;
    asc_dvc->scsi_reset_wait = 3;
    asc_dvc->start_motor = ASC_SCSI_WIDTH_BIT_SET;
    asc_dvc->max_dma_count = AscGetMaxDmaCount(asc_dvc->bus_type);
    asc_dvc->cfg->sdtr_enable = ASC_SCSI_WIDTH_BIT_SET;
    asc_dvc->cfg->disc_enable = ASC_SCSI_WIDTH_BIT_SET;
    asc_dvc->cfg->chip_scsi_id = ASC_DEF_CHIP_SCSI_ID;
    asc_dvc->cfg->lib_serial_no = ASC_LIB_SERIAL_NUMBER;
    asc_dvc->cfg->lib_version = (ASC_LIB_VERSION_MAJOR << 8) |
      ASC_LIB_VERSION_MINOR;
    chip_version = AscGetChipVersion(iop_base, asc_dvc->bus_type);
    asc_dvc->cfg->chip_version = chip_version;
    asc_dvc->sdtr_period_tbl[0] = SYN_XFER_NS_0;
    asc_dvc->sdtr_period_tbl[1] = SYN_XFER_NS_1;
    asc_dvc->sdtr_period_tbl[2] = SYN_XFER_NS_2;
    asc_dvc->sdtr_period_tbl[3] = SYN_XFER_NS_3;
    asc_dvc->sdtr_period_tbl[4] = SYN_XFER_NS_4;
    asc_dvc->sdtr_period_tbl[5] = SYN_XFER_NS_5;
    asc_dvc->sdtr_period_tbl[6] = SYN_XFER_NS_6;
    asc_dvc->sdtr_period_tbl[7] = SYN_XFER_NS_7;
    asc_dvc->max_sdtr_index = 7;
    if ((asc_dvc->bus_type & ASC_IS_PCI) &&
        (chip_version >= ASC_CHIP_VER_PCI_ULTRA_3150)) {
        asc_dvc->bus_type = ASC_IS_PCI_ULTRA;
        asc_dvc->sdtr_period_tbl[0] = SYN_ULTRA_XFER_NS_0;
        asc_dvc->sdtr_period_tbl[1] = SYN_ULTRA_XFER_NS_1;
        asc_dvc->sdtr_period_tbl[2] = SYN_ULTRA_XFER_NS_2;
        asc_dvc->sdtr_period_tbl[3] = SYN_ULTRA_XFER_NS_3;
        asc_dvc->sdtr_period_tbl[4] = SYN_ULTRA_XFER_NS_4;
        asc_dvc->sdtr_period_tbl[5] = SYN_ULTRA_XFER_NS_5;
        asc_dvc->sdtr_period_tbl[6] = SYN_ULTRA_XFER_NS_6;
        asc_dvc->sdtr_period_tbl[7] = SYN_ULTRA_XFER_NS_7;
        asc_dvc->sdtr_period_tbl[8] = SYN_ULTRA_XFER_NS_8;
        asc_dvc->sdtr_period_tbl[9] = SYN_ULTRA_XFER_NS_9;
        asc_dvc->sdtr_period_tbl[10] = SYN_ULTRA_XFER_NS_10;
        asc_dvc->sdtr_period_tbl[11] = SYN_ULTRA_XFER_NS_11;
        asc_dvc->sdtr_period_tbl[12] = SYN_ULTRA_XFER_NS_12;
        asc_dvc->sdtr_period_tbl[13] = SYN_ULTRA_XFER_NS_13;
        asc_dvc->sdtr_period_tbl[14] = SYN_ULTRA_XFER_NS_14;
        asc_dvc->sdtr_period_tbl[15] = SYN_ULTRA_XFER_NS_15;
        asc_dvc->max_sdtr_index = 15;
        if (chip_version == ASC_CHIP_VER_PCI_ULTRA_3150)
        {
            AscSetExtraControl(iop_base,
                (SEC_ACTIVE_NEGATE | SEC_SLEW_RATE));
        } else if (chip_version >= ASC_CHIP_VER_PCI_ULTRA_3050) {
            AscSetExtraControl(iop_base,
                (SEC_ACTIVE_NEGATE | SEC_ENABLE_FILTER));
        }
    }
    if (asc_dvc->bus_type == ASC_IS_PCI) {
           AscSetExtraControl(iop_base, (SEC_ACTIVE_NEGATE | SEC_SLEW_RATE));
    }

    asc_dvc->cfg->isa_dma_speed = ASC_DEF_ISA_DMA_SPEED;
    if (AscGetChipBusType(iop_base) == ASC_IS_ISAPNP) {
        AscSetChipIFC(iop_base, IFC_INIT_DEFAULT);
        asc_dvc->bus_type = ASC_IS_ISAPNP;
    }
#ifdef CONFIG_ISA
    if ((asc_dvc->bus_type & ASC_IS_ISA) != 0) {
        asc_dvc->cfg->isa_dma_channel = (uchar) AscGetIsaDmaChannel(iop_base);
    }
#endif /* CONFIG_ISA */
    for (i = 0; i <= ASC_MAX_TID; i++) {
        asc_dvc->cur_dvc_qng[i] = 0;
        asc_dvc->max_dvc_qng[i] = ASC_MAX_SCSI1_QNG;
        asc_dvc->scsiq_busy_head[i] = (ASC_SCSI_Q *) 0L;
        asc_dvc->scsiq_busy_tail[i] = (ASC_SCSI_Q *) 0L;
        asc_dvc->cfg->max_tag_qng[i] = ASC_MAX_INRAM_TAG_QNG;
    }
    return (warn_code);
}

STATIC ushort __init
AscInitFromEEP(ASC_DVC_VAR *asc_dvc)
{
    ASCEEP_CONFIG       eep_config_buf;
    ASCEEP_CONFIG       *eep_config;
    PortAddr            iop_base;
    ushort              chksum;
    ushort              warn_code;
    ushort              cfg_msw, cfg_lsw;
    int                 i;
    int                 write_eep = 0;

    iop_base = asc_dvc->iop_base;
    warn_code = 0;
    AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0x00FE);
    AscStopQueueExe(iop_base);
    if ((AscStopChip(iop_base) == FALSE) ||
        (AscGetChipScsiCtrl(iop_base) != 0)) {
        asc_dvc->init_state |= ASC_INIT_RESET_SCSI_DONE;
        AscResetChipAndScsiBus(asc_dvc);
        DvcSleepMilliSecond((ASC_DCNT)
            ((ushort) asc_dvc->scsi_reset_wait * 1000));
    }
    if (AscIsChipHalted(iop_base) == FALSE) {
        asc_dvc->err_code |= ASC_IERR_START_STOP_CHIP;
        return (warn_code);
    }
    AscSetPCAddr(iop_base, ASC_MCODE_START_ADDR);
    if (AscGetPCAddr(iop_base) != ASC_MCODE_START_ADDR) {
        asc_dvc->err_code |= ASC_IERR_SET_PC_ADDR;
        return (warn_code);
    }
    eep_config = (ASCEEP_CONFIG *) &eep_config_buf;
    cfg_msw = AscGetChipCfgMsw(iop_base);
    cfg_lsw = AscGetChipCfgLsw(iop_base);
    if ((cfg_msw & ASC_CFG_MSW_CLR_MASK) != 0) {
        cfg_msw &= (~(ASC_CFG_MSW_CLR_MASK));
        warn_code |= ASC_WARN_CFG_MSW_RECOVER;
        AscSetChipCfgMsw(iop_base, cfg_msw);
    }
    chksum = AscGetEEPConfig(iop_base, eep_config, asc_dvc->bus_type);
    ASC_DBG1(1, "AscInitFromEEP: chksum 0x%x\n", chksum);
    if (chksum == 0) {
        chksum = 0xaa55;
    }
    if (AscGetChipStatus(iop_base) & CSW_AUTO_CONFIG) {
        warn_code |= ASC_WARN_AUTO_CONFIG;
        if (asc_dvc->cfg->chip_version == 3) {
            if (eep_config->cfg_lsw != cfg_lsw) {
                warn_code |= ASC_WARN_EEPROM_RECOVER;
                eep_config->cfg_lsw = AscGetChipCfgLsw(iop_base);
            }
            if (eep_config->cfg_msw != cfg_msw) {
                warn_code |= ASC_WARN_EEPROM_RECOVER;
                eep_config->cfg_msw = AscGetChipCfgMsw(iop_base);
            }
        }
    }
    eep_config->cfg_msw &= ~ASC_CFG_MSW_CLR_MASK;
    eep_config->cfg_lsw |= ASC_CFG0_HOST_INT_ON;
    ASC_DBG1(1, "AscInitFromEEP: eep_config->chksum 0x%x\n",
        eep_config->chksum);
    if (chksum != eep_config->chksum) {
            if (AscGetChipVersion(iop_base, asc_dvc->bus_type) ==
                    ASC_CHIP_VER_PCI_ULTRA_3050 )
            {
                ASC_DBG(1,
"AscInitFromEEP: chksum error ignored; EEPROM-less board\n");
                eep_config->init_sdtr = 0xFF;
                eep_config->disc_enable = 0xFF;
                eep_config->start_motor = 0xFF;
                eep_config->use_cmd_qng = 0;
                eep_config->max_total_qng = 0xF0;
                eep_config->max_tag_qng = 0x20;
                eep_config->cntl = 0xBFFF;
                ASC_EEP_SET_CHIP_ID(eep_config, 7);
                eep_config->no_scam = 0;
                eep_config->adapter_info[0] = 0;
                eep_config->adapter_info[1] = 0;
                eep_config->adapter_info[2] = 0;
                eep_config->adapter_info[3] = 0;
                eep_config->adapter_info[4] = 0;
                /* Indicate EEPROM-less board. */
                eep_config->adapter_info[5] = 0xBB;
            } else {
                ASC_PRINT(
"AscInitFromEEP: EEPROM checksum error; Will try to re-write EEPROM.\n");
                write_eep = 1;
                warn_code |= ASC_WARN_EEPROM_CHKSUM;
            }
    }
    asc_dvc->cfg->sdtr_enable = eep_config->init_sdtr;
    asc_dvc->cfg->disc_enable = eep_config->disc_enable;
    asc_dvc->cfg->cmd_qng_enabled = eep_config->use_cmd_qng;
    asc_dvc->cfg->isa_dma_speed = ASC_EEP_GET_DMA_SPD(eep_config);
    asc_dvc->start_motor = eep_config->start_motor;
    asc_dvc->dvc_cntl = eep_config->cntl;
    asc_dvc->no_scam = eep_config->no_scam;
    asc_dvc->cfg->adapter_info[0] = eep_config->adapter_info[0];
    asc_dvc->cfg->adapter_info[1] = eep_config->adapter_info[1];
    asc_dvc->cfg->adapter_info[2] = eep_config->adapter_info[2];
    asc_dvc->cfg->adapter_info[3] = eep_config->adapter_info[3];
    asc_dvc->cfg->adapter_info[4] = eep_config->adapter_info[4];
    asc_dvc->cfg->adapter_info[5] = eep_config->adapter_info[5];
    if (!AscTestExternalLram(asc_dvc)) {
        if (((asc_dvc->bus_type & ASC_IS_PCI_ULTRA) == ASC_IS_PCI_ULTRA)) {
            eep_config->max_total_qng = ASC_MAX_PCI_ULTRA_INRAM_TOTAL_QNG;
            eep_config->max_tag_qng = ASC_MAX_PCI_ULTRA_INRAM_TAG_QNG;
        } else {
            eep_config->cfg_msw |= 0x0800;
            cfg_msw |= 0x0800;
            AscSetChipCfgMsw(iop_base, cfg_msw);
            eep_config->max_total_qng = ASC_MAX_PCI_INRAM_TOTAL_QNG;
            eep_config->max_tag_qng = ASC_MAX_INRAM_TAG_QNG;
        }
    } else {
    }
    if (eep_config->max_total_qng < ASC_MIN_TOTAL_QNG) {
        eep_config->max_total_qng = ASC_MIN_TOTAL_QNG;
    }
    if (eep_config->max_total_qng > ASC_MAX_TOTAL_QNG) {
        eep_config->max_total_qng = ASC_MAX_TOTAL_QNG;
    }
    if (eep_config->max_tag_qng > eep_config->max_total_qng) {
        eep_config->max_tag_qng = eep_config->max_total_qng;
    }
    if (eep_config->max_tag_qng < ASC_MIN_TAG_Q_PER_DVC) {
        eep_config->max_tag_qng = ASC_MIN_TAG_Q_PER_DVC;
    }
    asc_dvc->max_total_qng = eep_config->max_total_qng;
    if ((eep_config->use_cmd_qng & eep_config->disc_enable) !=
        eep_config->use_cmd_qng) {
        eep_config->disc_enable = eep_config->use_cmd_qng;
        warn_code |= ASC_WARN_CMD_QNG_CONFLICT;
    }
    if (asc_dvc->bus_type & (ASC_IS_ISA | ASC_IS_VL | ASC_IS_EISA)) {
        asc_dvc->irq_no = AscGetChipIRQ(iop_base, asc_dvc->bus_type);
    }
    ASC_EEP_SET_CHIP_ID(eep_config, ASC_EEP_GET_CHIP_ID(eep_config) & ASC_MAX_TID);
    asc_dvc->cfg->chip_scsi_id = ASC_EEP_GET_CHIP_ID(eep_config);
    if (((asc_dvc->bus_type & ASC_IS_PCI_ULTRA) == ASC_IS_PCI_ULTRA) &&
        !(asc_dvc->dvc_cntl & ASC_CNTL_SDTR_ENABLE_ULTRA)) {
        asc_dvc->host_init_sdtr_index = ASC_SDTR_ULTRA_PCI_10MB_INDEX;
    }

    for (i = 0; i <= ASC_MAX_TID; i++) {
        asc_dvc->dos_int13_table[i] = eep_config->dos_int13_table[i];
        asc_dvc->cfg->max_tag_qng[i] = eep_config->max_tag_qng;
        asc_dvc->cfg->sdtr_period_offset[i] =
            (uchar) (ASC_DEF_SDTR_OFFSET |
                     (asc_dvc->host_init_sdtr_index << 4));
    }
    eep_config->cfg_msw = AscGetChipCfgMsw(iop_base);
    if (write_eep) {
        if ((i = AscSetEEPConfig(iop_base, eep_config, asc_dvc->bus_type)) !=
             0) {
                ASC_PRINT1(
"AscInitFromEEP: Failed to re-write EEPROM with %d errors.\n", i);
        } else {
                ASC_PRINT("AscInitFromEEP: Successfully re-wrote EEPROM.\n");
        }
    }
    return (warn_code);
}

STATIC ushort
AscInitMicroCodeVar(
                       ASC_DVC_VAR *asc_dvc
)
{
    int                 i;
    ushort              warn_code;
    PortAddr            iop_base;
    ASC_PADDR           phy_addr;
    ASC_DCNT            phy_size;

    iop_base = asc_dvc->iop_base;
    warn_code = 0;
    for (i = 0; i <= ASC_MAX_TID; i++) {
        AscPutMCodeInitSDTRAtID(iop_base, i,
                                asc_dvc->cfg->sdtr_period_offset[i]
);
    }

    AscInitQLinkVar(asc_dvc);
    AscWriteLramByte(iop_base, ASCV_DISC_ENABLE_B,
                     asc_dvc->cfg->disc_enable);
    AscWriteLramByte(iop_base, ASCV_HOSTSCSI_ID_B,
                     ASC_TID_TO_TARGET_ID(asc_dvc->cfg->chip_scsi_id));

    /* Align overrun buffer on an 8 byte boundary. */
    phy_addr = virt_to_bus(asc_dvc->cfg->overrun_buf);
    phy_addr = cpu_to_le32((phy_addr + 7) & ~0x7);
    AscMemDWordCopyPtrToLram(iop_base, ASCV_OVERRUN_PADDR_D,
        (uchar *) &phy_addr, 1);
    phy_size = cpu_to_le32(ASC_OVERRUN_BSIZE - 8);
    AscMemDWordCopyPtrToLram(iop_base, ASCV_OVERRUN_BSIZE_D,
        (uchar *) &phy_size, 1);

    asc_dvc->cfg->mcode_date =
        AscReadLramWord(iop_base, (ushort) ASCV_MC_DATE_W);
    asc_dvc->cfg->mcode_version =
        AscReadLramWord(iop_base, (ushort) ASCV_MC_VER_W);

    AscSetPCAddr(iop_base, ASC_MCODE_START_ADDR);
    if (AscGetPCAddr(iop_base) != ASC_MCODE_START_ADDR) {
        asc_dvc->err_code |= ASC_IERR_SET_PC_ADDR;
        return (warn_code);
    }
    if (AscStartChip(iop_base) != 1) {
        asc_dvc->err_code |= ASC_IERR_START_STOP_CHIP;
        return (warn_code);
    }

    return (warn_code);
}

STATIC int __init
AscTestExternalLram(
                       ASC_DVC_VAR *asc_dvc)
{
    PortAddr            iop_base;
    ushort              q_addr;
    ushort              saved_word;
    int                 sta;

    iop_base = asc_dvc->iop_base;
    sta = 0;
    q_addr = ASC_QNO_TO_QADDR(241);
    saved_word = AscReadLramWord(iop_base, q_addr);
    AscSetChipLramAddr(iop_base, q_addr);
    AscSetChipLramData(iop_base, 0x55AA);
    DvcSleepMilliSecond(10);
    AscSetChipLramAddr(iop_base, q_addr);
    if (AscGetChipLramData(iop_base) == 0x55AA) {
        sta = 1;
        AscWriteLramWord(iop_base, q_addr, saved_word);
    }
    return (sta);
}

STATIC int __init
AscWriteEEPCmdReg(
                     PortAddr iop_base,
                     uchar cmd_reg
)
{
    uchar               read_back;
    int                 retry;

    retry = 0;
    while (TRUE) {
        AscSetChipEEPCmd(iop_base, cmd_reg);
        DvcSleepMilliSecond(1);
        read_back = AscGetChipEEPCmd(iop_base);
        if (read_back == cmd_reg) {
            return (1);
        }
        if (retry++ > ASC_EEP_MAX_RETRY) {
            return (0);
        }
    }
}

STATIC int __init
AscWriteEEPDataReg(
                      PortAddr iop_base,
                      ushort data_reg
)
{
    ushort              read_back;
    int                 retry;

    retry = 0;
    while (TRUE) {
        AscSetChipEEPData(iop_base, data_reg);
        DvcSleepMilliSecond(1);
        read_back = AscGetChipEEPData(iop_base);
        if (read_back == data_reg) {
            return (1);
        }
        if (retry++ > ASC_EEP_MAX_RETRY) {
            return (0);
        }
    }
}

STATIC void __init
AscWaitEEPRead(void)
{
    DvcSleepMilliSecond(1);
    return;
}

STATIC void __init
AscWaitEEPWrite(void)
{
    DvcSleepMilliSecond(20);
    return;
}

STATIC ushort __init
AscReadEEPWord(
                  PortAddr iop_base,
                  uchar addr)
{
    ushort              read_wval;
    uchar               cmd_reg;

    AscWriteEEPCmdReg(iop_base, ASC_EEP_CMD_WRITE_DISABLE);
    AscWaitEEPRead();
    cmd_reg = addr | ASC_EEP_CMD_READ;
    AscWriteEEPCmdReg(iop_base, cmd_reg);
    AscWaitEEPRead();
    read_wval = AscGetChipEEPData(iop_base);
    AscWaitEEPRead();
    return (read_wval);
}

STATIC ushort __init
AscWriteEEPWord(
                   PortAddr iop_base,
                   uchar addr,
                   ushort word_val)
{
    ushort              read_wval;

    read_wval = AscReadEEPWord(iop_base, addr);
    if (read_wval != word_val) {
        AscWriteEEPCmdReg(iop_base, ASC_EEP_CMD_WRITE_ABLE);
        AscWaitEEPRead();
        AscWriteEEPDataReg(iop_base, word_val);
        AscWaitEEPRead();
        AscWriteEEPCmdReg(iop_base,
                          (uchar) ((uchar) ASC_EEP_CMD_WRITE | addr));
        AscWaitEEPWrite();
        AscWriteEEPCmdReg(iop_base, ASC_EEP_CMD_WRITE_DISABLE);
        AscWaitEEPRead();
        return (AscReadEEPWord(iop_base, addr));
    }
    return (read_wval);
}

STATIC ushort __init
AscGetEEPConfig(
                   PortAddr iop_base,
                   ASCEEP_CONFIG * cfg_buf, ushort bus_type)
{
    ushort              wval;
    ushort              sum;
    ushort              *wbuf;
    int                 cfg_beg;
    int                 cfg_end;
    int                 uchar_end_in_config = ASC_EEP_MAX_DVC_ADDR - 2;
    int                 s_addr;

    wbuf = (ushort *) cfg_buf;
    sum = 0;
    /* Read two config words; Byte-swapping done by AscReadEEPWord(). */
    for (s_addr = 0; s_addr < 2; s_addr++, wbuf++) {
        *wbuf = AscReadEEPWord(iop_base, (uchar) s_addr);
        sum += *wbuf;
    }
    if (bus_type & ASC_IS_VL) {
        cfg_beg = ASC_EEP_DVC_CFG_BEG_VL;
        cfg_end = ASC_EEP_MAX_DVC_ADDR_VL;
    } else {
        cfg_beg = ASC_EEP_DVC_CFG_BEG;
        cfg_end = ASC_EEP_MAX_DVC_ADDR;
    }
    for (s_addr = cfg_beg; s_addr <= (cfg_end - 1); s_addr++, wbuf++) {
        wval = AscReadEEPWord( iop_base, ( uchar )s_addr ) ;
        if (s_addr <= uchar_end_in_config) {
            /*
             * Swap all char fields - must unswap bytes already swapped
             * by AscReadEEPWord().
             */
            *wbuf = le16_to_cpu(wval);
        } else {
            /* Don't swap word field at the end - cntl field. */
            *wbuf = wval;
        }
        sum += wval; /* Checksum treats all EEPROM data as words. */
    }
    /*
     * Read the checksum word which will be compared against 'sum'
     * by the caller. Word field already swapped.
     */
    *wbuf = AscReadEEPWord(iop_base, (uchar) s_addr);
    return (sum);
}

STATIC int __init
AscSetEEPConfigOnce(
                       PortAddr iop_base,
                       ASCEEP_CONFIG * cfg_buf, ushort bus_type)
{
    int                 n_error;
    ushort              *wbuf;
    ushort              word;
    ushort              sum;
    int                 s_addr;
    int                 cfg_beg;
    int                 cfg_end;
    int                 uchar_end_in_config = ASC_EEP_MAX_DVC_ADDR - 2;


    wbuf = (ushort *) cfg_buf;
    n_error = 0;
    sum = 0;
    /* Write two config words; AscWriteEEPWord() will swap bytes. */
    for (s_addr = 0; s_addr < 2; s_addr++, wbuf++) {
        sum += *wbuf;
        if (*wbuf != AscWriteEEPWord(iop_base, (uchar) s_addr, *wbuf)) {
            n_error++;
        }
    }
    if (bus_type & ASC_IS_VL) {
        cfg_beg = ASC_EEP_DVC_CFG_BEG_VL;
        cfg_end = ASC_EEP_MAX_DVC_ADDR_VL;
    } else {
        cfg_beg = ASC_EEP_DVC_CFG_BEG;
        cfg_end = ASC_EEP_MAX_DVC_ADDR;
    }
    for (s_addr = cfg_beg; s_addr <= (cfg_end - 1); s_addr++, wbuf++) {
        if (s_addr <= uchar_end_in_config) {
            /*
             * This is a char field. Swap char fields before they are
             * swapped again by AscWriteEEPWord().
             */
            word = cpu_to_le16(*wbuf);
            if (word != AscWriteEEPWord( iop_base, (uchar) s_addr, word)) {
                n_error++;
            }
        } else {
            /* Don't swap word field at the end - cntl field. */
            if (*wbuf != AscWriteEEPWord(iop_base, (uchar) s_addr, *wbuf)) {
                n_error++;
            }
        }
        sum += *wbuf; /* Checksum calculated from word values. */
    }
    /* Write checksum word. It will be swapped by AscWriteEEPWord(). */
    *wbuf = sum;
    if (sum != AscWriteEEPWord(iop_base, (uchar) s_addr, sum)) {
        n_error++;
    }

    /* Read EEPROM back again. */
    wbuf = (ushort *) cfg_buf;
    /*
     * Read two config words; Byte-swapping done by AscReadEEPWord().
     */
    for (s_addr = 0; s_addr < 2; s_addr++, wbuf++) {
        if (*wbuf != AscReadEEPWord(iop_base, (uchar) s_addr)) {
            n_error++;
        }
    }
    if (bus_type & ASC_IS_VL) {
        cfg_beg = ASC_EEP_DVC_CFG_BEG_VL;
        cfg_end = ASC_EEP_MAX_DVC_ADDR_VL;
    } else {
        cfg_beg = ASC_EEP_DVC_CFG_BEG;
        cfg_end = ASC_EEP_MAX_DVC_ADDR;
    }
    for (s_addr = cfg_beg; s_addr <= (cfg_end - 1); s_addr++, wbuf++) {
        if (s_addr <= uchar_end_in_config) {
            /*
             * Swap all char fields. Must unswap bytes already swapped
             * by AscReadEEPWord().
             */
            word = le16_to_cpu(AscReadEEPWord(iop_base, (uchar) s_addr));
        } else {
            /* Don't swap word field at the end - cntl field. */
            word = AscReadEEPWord(iop_base, (uchar) s_addr);
        }
        if (*wbuf != word) {
            n_error++;
        }
    }
    /* Read checksum; Byte swapping not needed. */
    if (AscReadEEPWord(iop_base, (uchar) s_addr) != sum) {
        n_error++;
    }
    return (n_error);
}

STATIC int __init
AscSetEEPConfig(
                   PortAddr iop_base,
                   ASCEEP_CONFIG * cfg_buf, ushort bus_type
)
{
    int            retry;
    int            n_error;

    retry = 0;
    while (TRUE) {
        if ((n_error = AscSetEEPConfigOnce(iop_base, cfg_buf,
                                           bus_type)) == 0) {
            break;
        }
        if (++retry > ASC_EEP_MAX_RETRY) {
            break;
        }
    }
    return (n_error);
}

STATIC void
AscAsyncFix(
               ASC_DVC_VAR *asc_dvc,
               uchar tid_no,
               ASC_SCSI_INQUIRY *inq)
{
    uchar                       dvc_type;
    ASC_SCSI_BIT_ID_TYPE        tid_bits;

    dvc_type = ASC_INQ_DVC_TYPE(inq);
    tid_bits = ASC_TIX_TO_TARGET_ID(tid_no);

    if (asc_dvc->bug_fix_cntl & ASC_BUG_FIX_ASYN_USE_SYN)
    {
        if (!(asc_dvc->init_sdtr & tid_bits))
        {
            if ((dvc_type == TYPE_ROM) &&
                (AscCompareString((uchar *) inq->vendor_id,
                    (uchar *) "HP ", 3) == 0))
            {
                asc_dvc->pci_fix_asyn_xfer_always |= tid_bits;
            }
            asc_dvc->pci_fix_asyn_xfer |= tid_bits;
            if ((dvc_type == TYPE_PROCESSOR) ||
                (dvc_type == TYPE_SCANNER) ||
                (dvc_type == TYPE_ROM) ||
                (dvc_type == TYPE_TAPE))
            {
                asc_dvc->pci_fix_asyn_xfer &= ~tid_bits;
            }

            if (asc_dvc->pci_fix_asyn_xfer & tid_bits)
            {
                AscSetRunChipSynRegAtID(asc_dvc->iop_base, tid_no,
                    ASYN_SDTR_DATA_FIX_PCI_REV_AB);
            }
        }
    }
    return;
}

STATIC int
AscTagQueuingSafe(ASC_SCSI_INQUIRY *inq)
{
    if ((inq->add_len >= 32) &&
        (AscCompareString((uchar *) inq->vendor_id,
            (uchar *) "QUANTUM XP34301", 15) == 0) &&
        (AscCompareString((uchar *) inq->product_rev_level,
            (uchar *) "1071", 4) == 0))
    {
        return 0;
    }
    return 1;
}

STATIC void
AscInquiryHandling(ASC_DVC_VAR *asc_dvc,
                   uchar tid_no, ASC_SCSI_INQUIRY *inq)
{
    ASC_SCSI_BIT_ID_TYPE tid_bit = ASC_TIX_TO_TARGET_ID(tid_no);
    ASC_SCSI_BIT_ID_TYPE orig_init_sdtr, orig_use_tagged_qng;

    orig_init_sdtr = asc_dvc->init_sdtr;
    orig_use_tagged_qng = asc_dvc->use_tagged_qng;

    asc_dvc->init_sdtr &= ~tid_bit;
    asc_dvc->cfg->can_tagged_qng &= ~tid_bit;
    asc_dvc->use_tagged_qng &= ~tid_bit;

    if (ASC_INQ_RESPONSE_FMT(inq) >= 2 || ASC_INQ_ANSI_VER(inq) >= 2) {
        if ((asc_dvc->cfg->sdtr_enable & tid_bit) && ASC_INQ_SYNC(inq)) {
            asc_dvc->init_sdtr |= tid_bit;
        }
        if ((asc_dvc->cfg->cmd_qng_enabled & tid_bit) &&
             ASC_INQ_CMD_QUEUE(inq)) {
            if (AscTagQueuingSafe(inq)) {
                asc_dvc->use_tagged_qng |= tid_bit;
                asc_dvc->cfg->can_tagged_qng |= tid_bit;
            }
        }
    }
    if (orig_use_tagged_qng != asc_dvc->use_tagged_qng) {
        AscWriteLramByte(asc_dvc->iop_base, ASCV_DISC_ENABLE_B,
                         asc_dvc->cfg->disc_enable);
        AscWriteLramByte(asc_dvc->iop_base, ASCV_USE_TAGGED_QNG_B,
                         asc_dvc->use_tagged_qng);
        AscWriteLramByte(asc_dvc->iop_base, ASCV_CAN_TAGGED_QNG_B,
                         asc_dvc->cfg->can_tagged_qng);

        asc_dvc->max_dvc_qng[tid_no] =
          asc_dvc->cfg->max_tag_qng[tid_no];
        AscWriteLramByte(asc_dvc->iop_base,
                         (ushort) (ASCV_MAX_DVC_QNG_BEG + tid_no),
                         asc_dvc->max_dvc_qng[tid_no]);
    }
    if (orig_init_sdtr != asc_dvc->init_sdtr) {
        AscAsyncFix(asc_dvc, tid_no, inq);
    }
    return;
}

STATIC int
AscCompareString(
                    uchar *str1,
                    uchar *str2,
                    int len
)
{
    int                 i;
    int                 diff;

    for (i = 0; i < len; i++) {
        diff = (int) (str1[i] - str2[i]);
        if (diff != 0)
            return (diff);
    }
    return (0);
}

STATIC uchar
AscReadLramByte(
                   PortAddr iop_base,
                   ushort addr
)
{
    uchar               byte_data;
    ushort              word_data;

    if (isodd_word(addr)) {
        AscSetChipLramAddr(iop_base, addr - 1);
        word_data = AscGetChipLramData(iop_base);
        byte_data = (uchar) ((word_data >> 8) & 0xFF);
    } else {
        AscSetChipLramAddr(iop_base, addr);
        word_data = AscGetChipLramData(iop_base);
        byte_data = (uchar) (word_data & 0xFF);
    }
    return (byte_data);
}
STATIC ushort
AscReadLramWord(
                   PortAddr iop_base,
                   ushort addr
)
{
    ushort              word_data;

    AscSetChipLramAddr(iop_base, addr);
    word_data = AscGetChipLramData(iop_base);
    return (word_data);
}

#if CC_VERY_LONG_SG_LIST
STATIC ASC_DCNT
AscReadLramDWord(
                    PortAddr iop_base,
                    ushort addr
)
{
    ushort              val_low, val_high;
    ASC_DCNT            dword_data;

    AscSetChipLramAddr(iop_base, addr);
    val_low = AscGetChipLramData(iop_base);
    val_high = AscGetChipLramData(iop_base);
    dword_data = ((ASC_DCNT) val_high << 16) | (ASC_DCNT) val_low;
    return (dword_data);
}
#endif /* CC_VERY_LONG_SG_LIST */

STATIC void
AscWriteLramWord(
                    PortAddr iop_base,
                    ushort addr,
                    ushort word_val
)
{
    AscSetChipLramAddr(iop_base, addr);
    AscSetChipLramData(iop_base, word_val);
    return;
}

STATIC void
AscWriteLramByte(
                    PortAddr iop_base,
                    ushort addr,
                    uchar byte_val
)
{
    ushort              word_data;

    if (isodd_word(addr)) {
        addr--;
        word_data = AscReadLramWord(iop_base, addr);
        word_data &= 0x00FF;
        word_data |= (((ushort) byte_val << 8) & 0xFF00);
    } else {
        word_data = AscReadLramWord(iop_base, addr);
        word_data &= 0xFF00;
        word_data |= ((ushort) byte_val & 0x00FF);
    }
    AscWriteLramWord(iop_base, addr, word_data);
    return;
}

/*
 * Copy 2 bytes to LRAM.
 *
 * The source data is assumed to be in little-endian order in memory
 * and is maintained in little-endian order when written to LRAM.
 */
STATIC void
AscMemWordCopyPtrToLram(
                        PortAddr iop_base,
                        ushort s_addr,
                        uchar *s_buffer,
                        int words
)
{
    int    i;

    AscSetChipLramAddr(iop_base, s_addr);
    for (i = 0; i < 2 * words; i += 2) {
        /*
         * On a little-endian system the second argument below
         * produces a little-endian ushort which is written to
         * LRAM in little-endian order. On a big-endian system
         * the second argument produces a big-endian ushort which
         * is "transparently" byte-swapped by outpw() and written
         * in little-endian order to LRAM.
         */
        outpw(iop_base + IOP_RAM_DATA,
            ((ushort) s_buffer[i + 1] << 8) | s_buffer[i]);
    }
    return;
}

/*
 * Copy 4 bytes to LRAM.
 *
 * The source data is assumed to be in little-endian order in memory
 * and is maintained in little-endian order when writen to LRAM.
 */
STATIC void
AscMemDWordCopyPtrToLram(
                         PortAddr iop_base,
                         ushort s_addr,
                         uchar *s_buffer,
                         int dwords
)
{
    int       i;

    AscSetChipLramAddr(iop_base, s_addr);
    for (i = 0; i < 4 * dwords; i += 4) {
        outpw(iop_base + IOP_RAM_DATA,
            ((ushort) s_buffer[i + 1] << 8) | s_buffer[i]); /* LSW */
        outpw(iop_base + IOP_RAM_DATA,
            ((ushort) s_buffer[i + 3] << 8) | s_buffer[i + 2]); /* MSW */
    }
    return;
}

/*
 * Copy 2 bytes from LRAM.
 *
 * The source data is assumed to be in little-endian order in LRAM
 * and is maintained in little-endian order when written to memory.
 */
STATIC void
AscMemWordCopyPtrFromLram(
                          PortAddr iop_base,
                          ushort s_addr,
                          uchar *d_buffer,
                          int words
)
{
    int i;
    ushort word;

    AscSetChipLramAddr(iop_base, s_addr);
    for (i = 0; i < 2 * words; i += 2) {
        word = inpw(iop_base + IOP_RAM_DATA);
        d_buffer[i] = word & 0xff;
        d_buffer[i + 1] = (word >> 8) & 0xff;
    }
    return;
}

STATIC ASC_DCNT
AscMemSumLramWord(
                     PortAddr iop_base,
                     ushort s_addr,
                     int words
)
{
    ASC_DCNT         sum;
    int              i;

    sum = 0L;
    for (i = 0; i < words; i++, s_addr += 2) {
        sum += AscReadLramWord(iop_base, s_addr);
    }
    return (sum);
}

STATIC void
AscMemWordSetLram(
                     PortAddr iop_base,
                     ushort s_addr,
                     ushort set_wval,
                     int words
)
{
    int             i;

    AscSetChipLramAddr(iop_base, s_addr);
    for (i = 0; i < words; i++) {
        AscSetChipLramData(iop_base, set_wval);
    }
    return;
}


/*
 * --- Adv Library Functions
 */

/* a_mcode.h */

/* Microcode buffer is kept after initialization for error recovery. */
STATIC unsigned char _adv_asc3550_buf[] = {
  0x00,  0x00,  0x00,  0xf2,  0x00,  0xf0,  0x00,  0x16,  0x18,  0xe4,  0x00,  0xfc,  0x01,  0x00,  0x48,  0xe4,
  0xbe,  0x18,  0x18,  0x80,  0x03,  0xf6,  0x02,  0x00,  0x00,  0xfa,  0xff,  0xff,  0x28,  0x0e,  0x9e,  0xe7,
  0xff,  0x00,  0x82,  0xe7,  0x00,  0xea,  0x00,  0xf6,  0x01,  0xe6,  0x09,  0xe7,  0x55,  0xf0,  0x01,  0xf6,
  0x01,  0xfa,  0x08,  0x00,  0x03,  0x00,  0x04,  0x00,  0x18,  0xf4,  0x10,  0x00,  0x00,  0xec,  0x85,  0xf0,
  0xbc,  0x00,  0xd5,  0xf0,  0x8e,  0x0c,  0x38,  0x54,  0x00,  0xe6,  0x1e,  0xf0,  0x86,  0xf0,  0xb4,  0x00,
  0x98,  0x57,  0xd0,  0x01,  0x0c,  0x1c,  0x3e,  0x1c,  0x0c,  0x00,  0xbb,  0x00,  0xaa,  0x18,  0x02,  0x80,
  0x32,  0xf0,  0x01,  0xfc,  0x88,  0x0c,  0xc6,  0x12,  0x02,  0x13,  0x18,  0x40,  0x00,  0x57,  0x01,  0xea,
  0x3c,  0x00,  0x6c,  0x01,  0x6e,  0x01,  0x04,  0x12,  0x3e,  0x57,  0x00,  0x80,  0x03,  0xe6,  0xb6,  0x00,
  0xc0,  0x00,  0x01,  0x01,  0x3e,  0x01,  0xda,  0x0f,  0x22,  0x10,  0x08,  0x12,  0x02,  0x4a,  0xb9,  0x54,
  0x03,  0x58,  0x1b,  0x80,  0x30,  0xe4,  0x4b,  0xe4,  0x20,  0x00,  0x32,  0x00,  0x3e,  0x00,  0x80,  0x00,
  0x24,  0x01,  0x3c,  0x01,  0x68,  0x01,  0x6a,  0x01,  0x70,  0x01,  0x72,  0x01,  0x74,  0x01,  0x76,  0x01,
  0x78,  0x01,  0x62,  0x0a,  0x92,  0x0c,  0x2c,  0x10,  0x2e,  0x10,  0x06,  0x13,  0x4c,  0x1c,  0xbb,  0x55,
  0x3c,  0x56,  0x04,  0x80,  0x4a,  0xe4,  0x02,  0xee,  0x5b,  0xf0,  0xb1,  0xf0,  0x03,  0xf7,  0x06,  0xf7,
  0x03,  0xfc,  0x0f,  0x00,  0x40,  0x00,  0xbe,  0x00,  0x00,  0x01,  0xb0,  0x08,  0x30,  0x13,  0x64,  0x15,
  0x32,  0x1c,  0x38,  0x1c,  0x4e,  0x1c,  0x10,  0x44,  0x02,  0x48,  0x00,  0x4c,  0x04,  0xea,  0x5d,  0xf0,
  0x04,  0xf6,  0x02,  0xfc,  0x05,  0x00,  0x34,  0x00,  0x36,  0x00,  0x98,  0x00,  0xcc,  0x00,  0x20,  0x01,
  0x4e,  0x01,  0x4e,  0x0b,  0x1e,  0x0e,  0x0c,  0x10,  0x0a,  0x12,  0x04,  0x13,  0x40,  0x13,  0x30,  0x1c,
  0x00,  0x4e,  0xbd,  0x56,  0x06,  0x83,  0x00,  0xdc,  0x05,  0xf0,  0x09,  0xf0,  0x59,  0xf0,  0xa7,  0xf0,
  0xb8,  0xf0,  0x0e,  0xf7,  0x06,  0x00,  0x19,  0x00,  0x33,  0x00,  0x9b,  0x00,  0xa4,  0x00,  0xb5,  0x00,
  0xba,  0x00,  0xd0,  0x00,  0xe1,  0x00,  0xe7,  0x00,  0xde,  0x03,  0x56,  0x0a,  0x14,  0x0e,  0x02,  0x10,
  0x04,  0x10,  0x0a,  0x10,  0x36,  0x10,  0x0a,  0x13,  0x12,  0x13,  0x52,  0x13,  0x10,  0x15,  0x14,  0x15,
  0xac,  0x16,  0x20,  0x1c,  0x34,  0x1c,  0x36,  0x1c,  0x08,  0x44,  0x38,  0x44,  0x91,  0x44,  0x0a,  0x45,
  0x48,  0x46,  0x01,  0x48,  0x68,  0x54,  0x83,  0x55,  0xb0,  0x57,  0x01,  0x58,  0x83,  0x59,  0x05,  0xe6,
  0x0b,  0xf0,  0x0c,  0xf0,  0x5c,  0xf0,  0x4b,  0xf4,  0x04,  0xf8,  0x05,  0xf8,  0x02,  0xfa,  0x03,  0xfa,
  0x04,  0xfc,  0x05,  0xfc,  0x07,  0x00,  0x0a,  0x00,  0x0d,  0x00,  0x1c,  0x00,  0x9e,  0x00,  0xa8,  0x00,
  0xaa,  0x00,  0xb9,  0x00,  0xe0,  0x00,  0x22,  0x01,  0x26,  0x01,  0x79,  0x01,  0x7a,  0x01,  0xc0,  0x01,
  0xc2,  0x01,  0x7c,  0x02,  0x5a,  0x03,  0xea,  0x04,  0xe8,  0x07,  0x68,  0x08,  0x69,  0x08,  0xba,  0x08,
  0xe9,  0x09,  0x06,  0x0b,  0x3a,  0x0e,  0x00,  0x10,  0x1a,  0x10,  0xed,  0x10,  0xf1,  0x10,  0x06,  0x12,
  0x0c,  0x13,  0x16,  0x13,  0x1e,  0x13,  0x82,  0x13,  0x42,  0x14,  0xd6,  0x14,  0x8a,  0x15,  0xc6,  0x17,
  0xd2,  0x17,  0x6b,  0x18,  0x12,  0x1c,  0x46,  0x1c,  0x9c,  0x32,  0x00,  0x40,  0x0e,  0x47,  0x48,  0x47,
  0x41,  0x48,  0x89,  0x48,  0x80,  0x4c,  0x00,  0x54,  0x44,  0x55,  0xe5,  0x55,  0x14,  0x56,  0x77,  0x57,
  0xbf,  0x57,  0x40,  0x5c,  0x06,  0x80,  0x08,  0x90,  0x03,  0xa1,  0xfe,  0x9c,  0xf0,  0x29,  0x02,  0xfe,
  0xb8,  0x0c,  0xff,  0x10,  0x00,  0x00,  0xd0,  0xfe,  0xcc,  0x18,  0x00,  0xcf,  0xfe,  0x80,  0x01,  0xff,
  0x03,  0x00,  0x00,  0xfe,  0x93,  0x15,  0xfe,  0x0f,  0x05,  0xff,  0x38,  0x00,  0x00,  0xfe,  0x57,  0x24,
  0x00,  0xfe,  0x48,  0x00,  0x4f,  0xff,  0x04,  0x00,  0x00,  0x10,  0xff,  0x09,  0x00,  0x00,  0xff,  0x08,
  0x01,  0x01,  0xff,  0x08,  0xff,  0xff,  0xff,  0x27,  0x00,  0x00,  0xff,  0x10,  0xff,  0xff,  0xff,  0x0f,
  0x00,  0x00,  0xfe,  0x78,  0x56,  0xfe,  0x34,  0x12,  0xff,  0x21,  0x00,  0x00,  0xfe,  0x04,  0xf7,  0xcf,
  0x2a,  0x67,  0x0b,  0x01,  0xfe,  0xce,  0x0e,  0xfe,  0x04,  0xf7,  0xcf,  0x67,  0x0b,  0x3c,  0x2a,  0xfe,
  0x3d,  0xf0,  0xfe,  0x02,  0x02,  0xfe,  0x20,  0xf0,  0x9c,  0xfe,  0x91,  0xf0,  0xfe,  0xf0,  0x01,  0xfe,
  0x90,  0xf0,  0xfe,  0xf0,  0x01,  0xfe,  0x8f,  0xf0,  0x9c,  0x05,  0x51,  0x3b,  0x02,  0xfe,  0xd4,  0x0c,
  0x01,  0xfe,  0x44,  0x0d,  0xfe,  0xdd,  0x12,  0xfe,  0xfc,  0x10,  0xfe,  0x28,  0x1c,  0x05,  0xfe,  0xa6,
  0x00,  0xfe,  0xd3,  0x12,  0x47,  0x18,  0xfe,  0xa6,  0x00,  0xb5,  0xfe,  0x48,  0xf0,  0xfe,  0x86,  0x02,
  0xfe,  0x49,  0xf0,  0xfe,  0xa0,  0x02,  0xfe,  0x4a,  0xf0,  0xfe,  0xbe,  0x02,  0xfe,  0x46,  0xf0,  0xfe,
  0x50,  0x02,  0xfe,  0x47,  0xf0,  0xfe,  0x56,  0x02,  0xfe,  0x43,  0xf0,  0xfe,  0x44,  0x02,  0xfe,  0x44,
  0xf0,  0xfe,  0x48,  0x02,  0xfe,  0x45,  0xf0,  0xfe,  0x4c,  0x02,  0x17,  0x0b,  0xa0,  0x17,  0x06,  0x18,
  0x96,  0x02,  0x29,  0xfe,  0x00,  0x1c,  0xde,  0xfe,  0x02,  0x1c,  0xdd,  0xfe,  0x1e,  0x1c,  0xfe,  0xe9,
  0x10,  0x01,  0xfe,  0x20,  0x17,  0xfe,  0xe7,  0x10,  0xfe,  0x06,  0xfc,  0xc7,  0x0a,  0x6b,  0x01,  0x9e,
  0x02,  0x29,  0x14,  0x4d,  0x37,  0x97,  0x01,  0xfe,  0x64,  0x0f,  0x0a,  0x6b,  0x01,  0x82,  0xfe,  0xbd,
  0x10,  0x0a,  0x6b,  0x01,  0x82,  0xfe,  0xad,  0x10,  0xfe,  0x16,  0x1c,  0xfe,  0x58,  0x1c,  0x17,  0x06,
  0x18,  0x96,  0x2a,  0x25,  0x29,  0xfe,  0x3d,  0xf0,  0xfe,  0x02,  0x02,  0x21,  0xfe,  0x94,  0x02,  0xfe,
  0x5a,  0x1c,  0xea,  0xfe,  0x14,  0x1c,  0x14,  0xfe,  0x30,  0x00,  0x37,  0x97,  0x01,  0xfe,  0x54,  0x0f,
  0x17,  0x06,  0x18,  0x96,  0x02,  0xd0,  0x1e,  0x20,  0x07,  0x10,  0x34,  0xfe,  0x69,  0x10,  0x17,  0x06,
  0x18,  0x96,  0xfe,  0x04,  0xec,  0x20,  0x46,  0x3d,  0x12,  0x20,  0xfe,  0x05,  0xf6,  0xc7,  0x01,  0xfe,
  0x52,  0x16,  0x09,  0x4a,  0x4c,  0x35,  0x11,  0x2d,  0x3c,  0x8a,  0x01,  0xe6,  0x02,  0x29,  0x0a,  0x40,
  0x01,  0x0e,  0x07,  0x00,  0x5d,  0x01,  0x6f,  0xfe,  0x18,  0x10,  0xfe,  0x41,  0x58,  0x0a,  0x99,  0x01,
  0x0e,  0xfe,  0xc8,  0x54,  0x64,  0xfe,  0x0c,  0x03,  0x01,  0xe6,  0x02,  0x29,  0x2a,  0x46,  0xfe,  0x02,
  0xe8,  0x27,  0xf8,  0xfe,  0x9e,  0x43,  0xf7,  0xfe,  0x27,  0xf0,  0xfe,  0xdc,  0x01,  0xfe,  0x07,  0x4b,
  0xfe,  0x20,  0xf0,  0x9c,  0xfe,  0x40,  0x1c,  0x25,  0xd2,  0xfe,  0x26,  0xf0,  0xfe,  0x56,  0x03,  0xfe,
  0xa0,  0xf0,  0xfe,  0x44,  0x03,  0xfe,  0x11,  0xf0,  0x9c,  0xfe,  0xef,  0x10,  0xfe,  0x9f,  0xf0,  0xfe,
  0x64,  0x03,  0xeb,  0x0f,  0xfe,  0x11,  0x00,  0x02,  0x5a,  0x2a,  0xfe,  0x48,  0x1c,  0xeb,  0x09,  0x04,
  0x1d,  0xfe,  0x18,  0x13,  0x23,  0x1e,  0x98,  0xac,  0x12,  0x98,  0x0a,  0x40,  0x01,  0x0e,  0xac,  0x75,
  0x01,  0xfe,  0xbc,  0x15,  0x11,  0xca,  0x25,  0xd2,  0xfe,  0x01,  0xf0,  0xd2,  0xfe,  0x82,  0xf0,  0xfe,
  0x92,  0x03,  0xec,  0x11,  0xfe,  0xe4,  0x00,  0x65,  0xfe,  0xa4,  0x03,  0x25,  0x32,  0x1f,  0xfe,  0xb4,
  0x03,  0x01,  0x43,  0xfe,  0x06,  0xf0,  0xfe,  0xc4,  0x03,  0x8d,  0x81,  0xfe,  0x0a,  0xf0,  0xfe,  0x7a,
  0x06,  0x02,  0x22,  0x05,  0x6b,  0x28,  0x16,  0xfe,  0xf6,  0x04,  0x14,  0x2c,  0x01,  0x33,  0x8f,  0xfe,
  0x66,  0x02,  0x02,  0xd1,  0xeb,  0x2a,  0x67,  0x1a,  0xfe,  0x67,  0x1b,  0xf8,  0xf7,  0xfe,  0x48,  0x1c,
  0x70,  0x01,  0x6e,  0x87,  0x0a,  0x40,  0x01,  0x0e,  0x07,  0x00,  0x16,  0xd3,  0x0a,  0xca,  0x01,  0x0e,
  0x74,  0x60,  0x59,  0x76,  0x27,  0x05,  0x6b,  0x28,  0xfe,  0x10,  0x12,  0x14,  0x2c,  0x01,  0x33,  0x8f,
  0xfe,  0x66,  0x02,  0x02,  0xd1,  0xbc,  0x7d,  0xbd,  0x7f,  0x25,  0x22,  0x65,  0xfe,  0x3c,  0x04,  0x1f,
  0xfe,  0x38,  0x04,  0x68,  0xfe,  0xa0,  0x00,  0xfe,  0x9b,  0x57,  0xfe,  0x4e,  0x12,  0x2b,  0xff,  0x02,
  0x00,  0x10,  0x01,  0x08,  0x1f,  0xfe,  0xe0,  0x04,  0x2b,  0x01,  0x08,  0x1f,  0x22,  0x30,  0x2e,  0xd5,
  0xfe,  0x4c,  0x44,  0xfe,  0x4c,  0x12,  0x60,  0xfe,  0x44,  0x48,  0x13,  0x2c,  0xfe,  0x4c,  0x54,  0x64,
  0xd3,  0x46,  0x76,  0x27,  0xfa,  0xef,  0xfe,  0x62,  0x13,  0x09,  0x04,  0x1d,  0xfe,  0x2a,  0x13,  0x2f,
  0x07,  0x7e,  0xa5,  0xfe,  0x20,  0x10,  0x13,  0x2c,  0xfe,  0x4c,  0x54,  0x64,  0xd3,  0xfa,  0xef,  0x86,
  0x09,  0x04,  0x1d,  0xfe,  0x08,  0x13,  0x2f,  0x07,  0x7e,  0x6e,  0x09,  0x04,  0x1d,  0xfe,  0x1c,  0x12,
  0x14,  0x92,  0x09,  0x04,  0x06,  0x3b,  0x14,  0xc4,  0x01,  0x33,  0x8f,  0xfe,  0x70,  0x0c,  0x02,  0x22,
  0x2b,  0x11,  0xfe,  0xe6,  0x00,  0xfe,  0x1c,  0x90,  0xf9,  0x03,  0x14,  0x92,  0x01,  0x33,  0x02,  0x29,
  0xfe,  0x42,  0x5b,  0x67,  0x1a,  0xfe,  0x46,  0x59,  0xf8,  0xf7,  0xfe,  0x87,  0x80,  0xfe,  0x31,  0xe4,
  0x4f,  0x09,  0x04,  0x0b,  0xfe,  0x78,  0x13,  0xfe,  0x20,  0x80,  0x07,  0x1a,  0xfe,  0x70,  0x12,  0x49,
  0x04,  0x06,  0xfe,  0x60,  0x13,  0x05,  0xfe,  0xa2,  0x00,  0x28,  0x16,  0xfe,  0x80,  0x05,  0xfe,  0x31,
  0xe4,  0x6a,  0x49,  0x04,  0x0b,  0xfe,  0x4a,  0x13,  0x05,  0xfe,  0xa0,  0x00,  0x28,  0xfe,  0x42,  0x12,
  0x5e,  0x01,  0x08,  0x25,  0x32,  0xf1,  0x01,  0x08,  0x26,  0xfe,  0x98,  0x05,  0x11,  0xfe,  0xe3,  0x00,
  0x23,  0x49,  0xfe,  0x4a,  0xf0,  0xfe,  0x6a,  0x05,  0xfe,  0x49,  0xf0,  0xfe,  0x64,  0x05,  0x83,  0x24,
  0xfe,  0x21,  0x00,  0xa1,  0x24,  0xfe,  0x22,  0x00,  0xa0,  0x24,  0x4c,  0xfe,  0x09,  0x48,  0x01,  0x08,
  0x26,  0xfe,  0x98,  0x05,  0xfe,  0xe2,  0x08,  0x49,  0x04,  0xc5,  0x3b,  0x01,  0x86,  0x24,  0x06,  0x12,
  0xcc,  0x37,  0xfe,  0x27,  0x01,  0x09,  0x04,  0x1d,  0xfe,  0x22,  0x12,  0x47,  0x01,  0xa7,  0x14,  0x92,
  0x09,  0x04,  0x06,  0x3b,  0x14,  0xc4,  0x01,  0x33,  0x8f,  0xfe,  0x70,  0x0c,  0x02,  0x22,  0x05,  0xfe,
  0x9c,  0x00,  0x28,  0xfe,  0x3e,  0x12,  0x05,  0x50,  0x28,  0xfe,  0x36,  0x13,  0x47,  0x01,  0xa7,  0x26,
  0xfe,  0x08,  0x06,  0x0a,  0x06,  0x49,  0x04,  0x19,  0xfe,  0x02,  0x12,  0x5f,  0x01,  0xfe,  0xaa,  0x14,
  0x1f,  0xfe,  0xfe,  0x05,  0x11,  0x9a,  0x01,  0x43,  0x11,  0xfe,  0xe5,  0x00,  0x05,  0x50,  0xb4,  0x0c,
  0x50,  0x05,  0xc6,  0x28,  0xfe,  0x62,  0x12,  0x05,  0x3f,  0x28,  0xfe,  0x5a,  0x13,  0x01,  0xfe,  0x14,
  0x18,  0x01,  0xfe,  0x66,  0x18,  0xfe,  0x43,  0x48,  0xb7,  0x19,  0x13,  0x6c,  0xff,  0x02,  0x00,  0x57,
  0x48,  0x8b,  0x1c,  0x3d,  0x85,  0xb7,  0x69,  0x47,  0x01,  0xa7,  0x26,  0xfe,  0x72,  0x06,  0x49,  0x04,
  0x1b,  0xdf,  0x89,  0x0a,  0x4d,  0x01,  0xfe,  0xd8,  0x14,  0x1f,  0xfe,  0x68,  0x06,  0x11,  0x9a,  0x01,
  0x43,  0x11,  0xfe,  0xe5,  0x00,  0x05,  0x3f,  0xb4,  0x0c,  0x3f,  0x17,  0x06,  0x01,  0xa7,  0xec,  0x72,
  0x70,  0x01,  0x6e,  0x87,  0x11,  0xfe,  0xe2,  0x00,  0x01,  0x08,  0x25,  0x32,  0xfe,  0x0a,  0xf0,  0xfe,
  0xa6,  0x06,  0x8c,  0xfe,  0x5c,  0x07,  0xfe,  0x06,  0xf0,  0xfe,  0x64,  0x07,  0x8d,  0x81,  0x02,  0x22,
  0x09,  0x04,  0x0b,  0xfe,  0x2e,  0x12,  0x15,  0x1a,  0x01,  0x08,  0x15,  0x00,  0x01,  0x08,  0x15,  0x00,
  0x01,  0x08,  0x15,  0x00,  0x01,  0x08,  0xfe,  0x99,  0xa4,  0x01,  0x08,  0x15,  0x00,  0x02,  0xfe,  0x32,
  0x08,  0x61,  0x04,  0x1b,  0xfe,  0x38,  0x12,  0x09,  0x04,  0x1b,  0x6e,  0x15,  0xfe,  0x1b,  0x00,  0x01,
  0x08,  0x15,  0x00,  0x01,  0x08,  0x15,  0x00,  0x01,  0x08,  0x15,  0x00,  0x01,  0x08,  0x15,  0x06,  0x01,
  0x08,  0x15,  0x00,  0x02,  0xd9,  0x66,  0x4c,  0xfe,  0x3a,  0x55,  0x5f,  0xfe,  0x9a,  0x81,  0x4b,  0x1d,
  0xba,  0xfe,  0x32,  0x07,  0x0a,  0x1d,  0xfe,  0x09,  0x6f,  0xaf,  0xfe,  0xca,  0x45,  0xfe,  0x32,  0x12,
  0x62,  0x2c,  0x85,  0x66,  0x7b,  0x01,  0x08,  0x25,  0x32,  0xfe,  0x0a,  0xf0,  0xfe,  0x32,  0x07,  0x8d,
  0x81,  0x8c,  0xfe,  0x5c,  0x07,  0x02,  0x22,  0x01,  0x43,  0x02,  0xfe,  0x8a,  0x06,  0x15,  0x19,  0x02,
  0xfe,  0x8a,  0x06,  0xfe,  0x9c,  0xf7,  0xd4,  0xfe,  0x2c,  0x90,  0xfe,  0xae,  0x90,  0x77,  0xfe,  0xca,
  0x07,  0x0c,  0x54,  0x18,  0x55,  0x09,  0x4a,  0x6a,  0x35,  0x1e,  0x20,  0x07,  0x10,  0xfe,  0x0e,  0x12,
  0x74,  0xfe,  0x80,  0x80,  0x37,  0x20,  0x63,  0x27,  0xfe,  0x06,  0x10,  0xfe,  0x83,  0xe7,  0xc4,  0xa1,
  0xfe,  0x03,  0x40,  0x09,  0x4a,  0x4f,  0x35,  0x01,  0xa8,  0xad,  0xfe,  0x1f,  0x40,  0x12,  0x58,  0x01,
  0xa5,  0xfe,  0x08,  0x50,  0xfe,  0x8a,  0x50,  0xfe,  0x44,  0x51,  0xfe,  0xc6,  0x51,  0x83,  0xfb,  0xfe,
  0x8a,  0x90,  0x0c,  0x52,  0x18,  0x53,  0xfe,  0x0c,  0x90,  0xfe,  0x8e,  0x90,  0xfe,  0x40,  0x50,  0xfe,
  0xc2,  0x50,  0x0c,  0x39,  0x18,  0x3a,  0xfe,  0x4a,  0x10,  0x09,  0x04,  0x6a,  0xfe,  0x2a,  0x12,  0xfe,
  0x2c,  0x90,  0xfe,  0xae,  0x90,  0x0c,  0x54,  0x18,  0x55,  0x09,  0x04,  0x4f,  0x85,  0x01,  0xa8,  0xfe,
  0x1f,  0x80,  0x12,  0x58,  0xfe,  0x44,  0x90,  0xfe,  0xc6,  0x90,  0x0c,  0x56,  0x18,  0x57,  0xfb,  0xfe,
  0x8a,  0x90,  0x0c,  0x52,  0x18,  0x53,  0xfe,  0x40,  0x90,  0xfe,  0xc2,  0x90,  0x0c,  0x39,  0x18,  0x3a,
  0x0c,  0x38,  0x18,  0x4e,  0x09,  0x4a,  0x19,  0x35,  0x2a,  0x13,  0xfe,  0x4e,  0x11,  0x65,  0xfe,  0x48,
  0x08,  0xfe,  0x9e,  0xf0,  0xfe,  0x5c,  0x08,  0xb1,  0x16,  0x32,  0x2a,  0x73,  0xdd,  0xb8,  0xfe,  0x80,
  0x08,  0xb9,  0xfe,  0x9e,  0x08,  0x8c,  0xfe,  0x74,  0x08,  0xfe,  0x06,  0xf0,  0xfe,  0x7a,  0x08,  0x8d,
  0x81,  0x02,  0x22,  0x01,  0x43,  0xfe,  0xc9,  0x10,  0x15,  0x19,  0xfe,  0xc9,  0x10,  0x61,  0x04,  0x06,
  0xfe,  0x10,  0x12,  0x61,  0x04,  0x0b,  0x45,  0x09,  0x04,  0x0b,  0xfe,  0x68,  0x12,  0xfe,  0x2e,  0x1c,
  0x02,  0xfe,  0x24,  0x0a,  0x61,  0x04,  0x06,  0x45,  0x61,  0x04,  0x0b,  0xfe,  0x52,  0x12,  0xfe,  0x2c,
  0x1c,  0xfe,  0xaa,  0xf0,  0xfe,  0x1e,  0x09,  0xfe,  0xac,  0xf0,  0xfe,  0xbe,  0x08,  0xfe,  0x8a,  0x10,
  0xaa,  0xfe,  0xf3,  0x10,  0xfe,  0xad,  0xf0,  0xfe,  0xca,  0x08,  0x02,  0xfe,  0x24,  0x0a,  0xab,  0xfe,
  0xe7,  0x10,  0xfe,  0x2b,  0xf0,  0x9d,  0xe9,  0x1c,  0xfe,  0x00,  0xfe,  0xfe,  0x1c,  0x12,  0xb5,  0xfe,
  0xd2,  0xf0,  0x9d,  0xfe,  0x76,  0x18,  0x1c,  0x1a,  0x16,  0x9d,  0x05,  0xcb,  0x1c,  0x06,  0x16,  0x9d,
  0xb8,  0x6d,  0xb9,  0x6d,  0xaa,  0xab,  0xfe,  0xb1,  0x10,  0x70,  0x5e,  0x2b,  0x14,  0x92,  0x01,  0x33,
  0x0f,  0xfe,  0x35,  0x00,  0xfe,  0x01,  0xf0,  0x5a,  0x0f,  0x7c,  0x02,  0x5a,  0xfe,  0x74,  0x18,  0x1c,
  0xfe,  0x00,  0xf8,  0x16,  0x6d,  0x67,  0x1b,  0x01,  0xfe,  0x44,  0x0d,  0x3b,  0x01,  0xe6,  0x1e,  0x27,
  0x74,  0x67,  0x1a,  0x02,  0x6d,  0x09,  0x04,  0x0b,  0x21,  0xfe,  0x06,  0x0a,  0x09,  0x04,  0x6a,  0xfe,
  0x82,  0x12,  0x09,  0x04,  0x19,  0xfe,  0x66,  0x13,  0x1e,  0x58,  0xac,  0xfc,  0xfe,  0x83,  0x80,  0xfe,
  0xc8,  0x44,  0xfe,  0x2e,  0x13,  0xfe,  0x04,  0x91,  0xfe,  0x86,  0x91,  0x63,  0x27,  0xfe,  0x40,  0x59,
  0xfe,  0xc1,  0x59,  0x77,  0xd7,  0x05,  0x54,  0x31,  0x55,  0x0c,  0x7b,  0x18,  0x7c,  0xbe,  0x54,  0xbf,
  0x55,  0x01,  0xa8,  0xad,  0x63,  0x27,  0x12,  0x58,  0xc0,  0x38,  0xc1,  0x4e,  0x79,  0x56,  0x68,  0x57,
  0xf4,  0xf5,  0xfe,  0x04,  0xfa,  0x38,  0xfe,  0x05,  0xfa,  0x4e,  0x01,  0xa5,  0xa2,  0x23,  0x0c,  0x7b,
  0x0c,  0x7c,  0x79,  0x56,  0x68,  0x57,  0xfe,  0x12,  0x10,  0x09,  0x04,  0x19,  0x16,  0xd7,  0x79,  0x39,
  0x68,  0x3a,  0x09,  0x04,  0xfe,  0xf7,  0x00,  0x35,  0x05,  0x52,  0x31,  0x53,  0xfe,  0x10,  0x58,  0xfe,
  0x91,  0x58,  0xfe,  0x14,  0x59,  0xfe,  0x95,  0x59,  0x02,  0x6d,  0x09,  0x04,  0x19,  0x16,  0xd7,  0x09,
  0x04,  0xfe,  0xf7,  0x00,  0x35,  0xfe,  0x3a,  0x55,  0xfe,  0x19,  0x81,  0x5f,  0xfe,  0x10,  0x90,  0xfe,
  0x92,  0x90,  0xfe,  0xd7,  0x10,  0x2f,  0x07,  0x9b,  0x16,  0xfe,  0xc6,  0x08,  0x11,  0x9b,  0x09,  0x04,
  0x0b,  0xfe,  0x14,  0x13,  0x05,  0x39,  0x31,  0x3a,  0x77,  0xfe,  0xc6,  0x08,  0xfe,  0x0c,  0x58,  0xfe,
  0x8d,  0x58,  0x02,  0x6d,  0x23,  0x47,  0xfe,  0x19,  0x80,  0xde,  0x09,  0x04,  0x0b,  0xfe,  0x1a,  0x12,
  0xfe,  0x6c,  0x19,  0xfe,  0x19,  0x41,  0xe9,  0xb5,  0xfe,  0xd1,  0xf0,  0xd9,  0x14,  0x7a,  0x01,  0x33,
  0x0f,  0xfe,  0x44,  0x00,  0xfe,  0x8e,  0x10,  0xfe,  0x6c,  0x19,  0xbe,  0x39,  0xfe,  0xed,  0x19,  0xbf,
  0x3a,  0xfe,  0x0c,  0x51,  0xfe,  0x8e,  0x51,  0xe9,  0x1c,  0xfe,  0x00,  0xff,  0x34,  0xfe,  0x74,  0x10,
  0xb5,  0xfe,  0xd2,  0xf0,  0xfe,  0xb2,  0x0a,  0xfe,  0x76,  0x18,  0x1c,  0x1a,  0x84,  0x05,  0xcb,  0x1c,
  0x06,  0xfe,  0x08,  0x13,  0x0f,  0xfe,  0x16,  0x00,  0x02,  0x5a,  0xfe,  0xd1,  0xf0,  0xfe,  0xc4,  0x0a,
  0x14,  0x7a,  0x01,  0x33,  0x0f,  0xfe,  0x17,  0x00,  0xfe,  0x42,  0x10,  0xfe,  0xce,  0xf0,  0xfe,  0xca,
  0x0a,  0xfe,  0x3c,  0x10,  0xfe,  0xcd,  0xf0,  0xfe,  0xd6,  0x0a,  0x0f,  0xfe,  0x22,  0x00,  0x02,  0x5a,
  0xfe,  0xcb,  0xf0,  0xfe,  0xe2,  0x0a,  0x0f,  0xfe,  0x24,  0x00,  0x02,  0x5a,  0xfe,  0xd0,  0xf0,  0xfe,
  0xec,  0x0a,  0x0f,  0x93,  0xdc,  0xfe,  0xcf,  0xf0,  0xfe,  0xf6,  0x0a,  0x0f,  0x4c,  0xfe,  0x10,  0x10,
  0xfe,  0xcc,  0xf0,  0xd9,  0x61,  0x04,  0x19,  0x3b,  0x0f,  0xfe,  0x12,  0x00,  0x2a,  0x13,  0xfe,  0x4e,
  0x11,  0x65,  0xfe,  0x0c,  0x0b,  0xfe,  0x9e,  0xf0,  0xfe,  0x20,  0x0b,  0xb1,  0x16,  0x32,  0x2a,  0x73,
  0xdd,  0xb8,  0x22,  0xb9,  0x22,  0x2a,  0xec,  0x65,  0xfe,  0x2c,  0x0b,  0x25,  0x32,  0x8c,  0xfe,  0x48,
  0x0b,  0x8d,  0x81,  0xb8,  0xd4,  0xb9,  0xd4,  0x02,  0x22,  0x01,  0x43,  0xfe,  0xdb,  0x10,  0x11,  0xfe,
  0xe8,  0x00,  0xaa,  0xab,  0x70,  0xbc,  0x7d,  0xbd,  0x7f,  0xfe,  0x89,  0xf0,  0x22,  0x30,  0x2e,  0xd8,
  0xbc,  0x7d,  0xbd,  0x7f,  0x01,  0x08,  0x1f,  0x22,  0x30,  0x2e,  0xd6,  0xb1,  0x45,  0x0f,  0xfe,  0x42,
  0x00,  0x02,  0x5a,  0x78,  0x06,  0xfe,  0x81,  0x49,  0x16,  0xfe,  0x38,  0x0c,  0x09,  0x04,  0x0b,  0xfe,
  0x44,  0x13,  0x0f,  0x00,  0x4b,  0x0b,  0xfe,  0x54,  0x12,  0x4b,  0xfe,  0x28,  0x00,  0x21,  0xfe,  0xa6,
  0x0c,  0x0a,  0x40,  0x01,  0x0e,  0x07,  0x00,  0x5d,  0x3e,  0xfe,  0x28,  0x00,  0xfe,  0xe2,  0x10,  0x01,
  0xe7,  0x01,  0xe8,  0x0a,  0x99,  0x01,  0xfe,  0x32,  0x0e,  0x59,  0x11,  0x2d,  0x01,  0x6f,  0x02,  0x29,
  0x0f,  0xfe,  0x44,  0x00,  0x4b,  0x0b,  0xdf,  0x3e,  0x0b,  0xfe,  0xb4,  0x10,  0x01,  0x86,  0x3e,  0x0b,
  0xfe,  0xaa,  0x10,  0x01,  0x86,  0xfe,  0x19,  0x82,  0xfe,  0x34,  0x46,  0xa3,  0x3e,  0x0b,  0x0f,  0xfe,
  0x43,  0x00,  0xfe,  0x96,  0x10,  0x09,  0x4a,  0x0b,  0x35,  0x01,  0xe7,  0x01,  0xe8,  0x59,  0x11,  0x2d,
  0x01,  0x6f,  0x67,  0x0b,  0x59,  0x3c,  0x8a,  0x02,  0xfe,  0x2a,  0x03,  0x09,  0x04,  0x0b,  0x84,  0x3e,
  0x0b,  0x0f,  0x00,  0xfe,  0x5c,  0x10,  0x61,  0x04,  0x1b,  0xfe,  0x58,  0x12,  0x09,  0x04,  0x1b,  0xfe,
  0x50,  0x13,  0xfe,  0x1c,  0x1c,  0xfe,  0x9d,  0xf0,  0xfe,  0x5c,  0x0c,  0xfe,  0x1c,  0x1c,  0xfe,  0x9d,
  0xf0,  0xfe,  0x62,  0x0c,  0x09,  0x4a,  0x1b,  0x35,  0xfe,  0xa9,  0x10,  0x0f,  0xfe,  0x15,  0x00,  0xfe,
  0x04,  0xe6,  0x0b,  0x5f,  0x5c,  0x0f,  0xfe,  0x13,  0x00,  0xfe,  0x10,  0x10,  0x0f,  0xfe,  0x47,  0x00,
  0xa1,  0x0f,  0xfe,  0x41,  0x00,  0xa0,  0x0f,  0xfe,  0x24,  0x00,  0x87,  0xaa,  0xab,  0x70,  0x05,  0x6b,
  0x28,  0x21,  0xd1,  0x5f,  0xfe,  0x04,  0xe6,  0x1b,  0xfe,  0x9d,  0x41,  0xfe,  0x1c,  0x42,  0x59,  0x01,
  0xda,  0x02,  0x29,  0xea,  0x14,  0x0b,  0x37,  0x95,  0xa9,  0x14,  0xfe,  0x31,  0x00,  0x37,  0x97,  0x01,
  0xfe,  0x54,  0x0f,  0x02,  0xd0,  0x3c,  0xfe,  0x06,  0xec,  0xc9,  0xee,  0x3e,  0x1d,  0xfe,  0xce,  0x45,
  0x34,  0x3c,  0xfe,  0x06,  0xea,  0xc9,  0xfe,  0x47,  0x4b,  0x89,  0xfe,  0x75,  0x57,  0x05,  0x51,  0xfe,
  0x98,  0x56,  0xfe,  0x38,  0x12,  0x0a,  0x42,  0x01,  0x0e,  0xfe,  0x44,  0x48,  0x46,  0x09,  0x04,  0x1d,
  0xfe,  0x1a,  0x13,  0x0a,  0x40,  0x01,  0x0e,  0x47,  0xfe,  0x41,  0x58,  0x0a,  0x99,  0x01,  0x0e,  0xfe,
  0x49,  0x54,  0x8e,  0xfe,  0x2a,  0x0d,  0x02,  0xfe,  0x2a,  0x03,  0x0a,  0x51,  0xfe,  0xee,  0x14,  0xee,
  0x3e,  0x1d,  0xfe,  0xce,  0x45,  0x34,  0x3c,  0xfe,  0xce,  0x47,  0xfe,  0xad,  0x13,  0x02,  0x29,  0x1e,
  0x20,  0x07,  0x10,  0xfe,  0x9e,  0x12,  0x23,  0x12,  0x4d,  0x12,  0x94,  0x12,  0xce,  0x1e,  0x2d,  0x47,
  0x37,  0x2d,  0xb1,  0xe0,  0xfe,  0xbc,  0xf0,  0xfe,  0xec,  0x0d,  0x13,  0x06,  0x12,  0x4d,  0x01,  0xfe,
  0xe2,  0x15,  0x05,  0xfe,  0x38,  0x01,  0x31,  0xfe,  0x3a,  0x01,  0x77,  0xfe,  0xf0,  0x0d,  0xfe,  0x02,
  0xec,  0xce,  0x62,  0x00,  0x5d,  0xfe,  0x04,  0xec,  0x20,  0x46,  0xfe,  0x05,  0xf6,  0xfe,  0x34,  0x01,
  0x01,  0xfe,  0x52,  0x16,  0xfb,  0xfe,  0x48,  0xf4,  0x0d,  0xfe,  0x18,  0x13,  0xaf,  0xfe,  0x02,  0xea,
  0xce,  0x62,  0x7a,  0xfe,  0xc5,  0x13,  0x14,  0x1b,  0x37,  0x95,  0xa9,  0x5c,  0x05,  0xfe,  0x38,  0x01,
  0x1c,  0xfe,  0xf0,  0xff,  0x0c,  0xfe,  0x60,  0x01,  0x05,  0xfe,  0x3a,  0x01,  0x0c,  0xfe,  0x62,  0x01,
  0x3d,  0x12,  0x20,  0x24,  0x06,  0x12,  0x2d,  0x11,  0x2d,  0x8a,  0x13,  0x06,  0x03,  0x23,  0x03,  0x1e,
  0x4d,  0xfe,  0xf7,  0x12,  0x1e,  0x94,  0xac,  0x12,  0x94,  0x07,  0x7a,  0xfe,  0x71,  0x13,  0xfe,  0x24,
  0x1c,  0x14,  0x1a,  0x37,  0x95,  0xa9,  0xfe,  0xd9,  0x10,  0xb6,  0xfe,  0x03,  0xdc,  0xfe,  0x73,  0x57,
  0xfe,  0x80,  0x5d,  0x03,  0xb6,  0xfe,  0x03,  0xdc,  0xfe,  0x5b,  0x57,  0xfe,  0x80,  0x5d,  0x03,  0xfe,
  0x03,  0x57,  0xb6,  0x23,  0xfe,  0x00,  0xcc,  0x03,  0xfe,  0x03,  0x57,  0xb6,  0x75,  0x03,  0x09,  0x04,
  0x4c,  0xfe,  0x22,  0x13,  0xfe,  0x1c,  0x80,  0x07,  0x06,  0xfe,  0x1a,  0x13,  0xfe,  0x1e,  0x80,  0xe1,
  0xfe,  0x1d,  0x80,  0xa4,  0xfe,  0x0c,  0x90,  0xfe,  0x0e,  0x13,  0xfe,  0x0e,  0x90,  0xa3,  0xfe,  0x3c,
  0x90,  0xfe,  0x30,  0xf4,  0x0b,  0xfe,  0x3c,  0x50,  0xa0,  0x01,  0xfe,  0x82,  0x16,  0x2f,  0x07,  0x2d,
  0xe0,  0x01,  0xfe,  0xbc,  0x15,  0x09,  0x04,  0x1d,  0x45,  0x01,  0xe7,  0x01,  0xe8,  0x11,  0xfe,  0xe9,
  0x00,  0x09,  0x04,  0x4c,  0xfe,  0x2c,  0x13,  0x01,  0xfe,  0x14,  0x16,  0xfe,  0x1e,  0x1c,  0xfe,  0x14,
  0x90,  0xfe,  0x96,  0x90,  0x0c,  0xfe,  0x64,  0x01,  0x18,  0xfe,  0x66,  0x01,  0x09,  0x04,  0x4f,  0xfe,
  0x12,  0x12,  0xfe,  0x03,  0x80,  0x74,  0xfe,  0x01,  0xec,  0x20,  0xfe,  0x80,  0x40,  0x12,  0x20,  0x63,
  0x27,  0x11,  0xc8,  0x59,  0x1e,  0x20,  0xed,  0x76,  0x20,  0x03,  0xfe,  0x08,  0x1c,  0x05,  0xfe,  0xac,
  0x00,  0xfe,  0x06,  0x58,  0x05,  0xfe,  0xae,  0x00,  0xfe,  0x07,  0x58,  0x05,  0xfe,  0xb0,  0x00,  0xfe,
  0x08,  0x58,  0x05,  0xfe,  0xb2,  0x00,  0xfe,  0x09,  0x58,  0xfe,  0x0a,  0x1c,  0x24,  0x69,  0x12,  0xc9,
  0x23,  0x0c,  0x50,  0x0c,  0x3f,  0x13,  0x40,  0x48,  0x5f,  0x17,  0x1d,  0xfe,  0x90,  0x4d,  0xfe,  0x91,
  0x54,  0x21,  0xfe,  0x08,  0x0f,  0x3e,  0x10,  0x13,  0x42,  0x48,  0x17,  0x4c,  0xfe,  0x90,  0x4d,  0xfe,
  0x91,  0x54,  0x21,  0xfe,  0x1e,  0x0f,  0x24,  0x10,  0x12,  0x20,  0x78,  0x2c,  0x46,  0x1e,  0x20,  0xed,
  0x76,  0x20,  0x11,  0xc8,  0xf6,  0xfe,  0xd6,  0xf0,  0xfe,  0x32,  0x0f,  0xea,  0x70,  0xfe,  0x14,  0x1c,
  0xfe,  0x10,  0x1c,  0xfe,  0x18,  0x1c,  0x03,  0x3c,  0xfe,  0x0c,  0x14,  0xee,  0xfe,  0x07,  0xe6,  0x1d,
  0xfe,  0xce,  0x47,  0xfe,  0xf5,  0x13,  0x03,  0x01,  0x86,  0x78,  0x2c,  0x46,  0xfa,  0xef,  0xfe,  0x42,
  0x13,  0x2f,  0x07,  0x2d,  0xfe,  0x34,  0x13,  0x0a,  0x42,  0x01,  0x0e,  0xb0,  0xfe,  0x36,  0x12,  0xf0,
  0xfe,  0x45,  0x48,  0x01,  0xe3,  0xfe,  0x00,  0xcc,  0xb0,  0xfe,  0xf3,  0x13,  0x3d,  0x75,  0x07,  0x10,
  0xa3,  0x0a,  0x80,  0x01,  0x0e,  0xfe,  0x80,  0x5c,  0x01,  0x6f,  0xfe,  0x0e,  0x10,  0x07,  0x7e,  0x45,
  0xf6,  0xfe,  0xd6,  0xf0,  0xfe,  0x6c,  0x0f,  0x03,  0xfe,  0x44,  0x58,  0x74,  0xfe,  0x01,  0xec,  0x97,
  0xfe,  0x9e,  0x40,  0xfe,  0x9d,  0xe7,  0x00,  0xfe,  0x9c,  0xe7,  0x1b,  0x76,  0x27,  0x01,  0xda,  0xfe,
  0xdd,  0x10,  0x2a,  0xbc,  0x7d,  0xbd,  0x7f,  0x30,  0x2e,  0xd5,  0x07,  0x1b,  0xfe,  0x48,  0x12,  0x07,
  0x0b,  0xfe,  0x56,  0x12,  0x07,  0x1a,  0xfe,  0x30,  0x12,  0x07,  0xc2,  0x16,  0xfe,  0x3e,  0x11,  0x07,
  0xfe,  0x23,  0x00,  0x16,  0xfe,  0x4a,  0x11,  0x07,  0x06,  0x16,  0xfe,  0xa8,  0x11,  0x07,  0x19,  0xfe,
  0x12,  0x12,  0x07,  0x00,  0x16,  0x22,  0x14,  0xc2,  0x01,  0x33,  0x9f,  0x2b,  0x01,  0x08,  0x8c,  0x43,
  0x03,  0x2b,  0xfe,  0x62,  0x08,  0x0a,  0xca,  0x01,  0xfe,  0x32,  0x0e,  0x11,  0x7e,  0x02,  0x29,  0x2b,
  0x2f,  0x07,  0x9b,  0xfe,  0xd9,  0x13,  0x79,  0x39,  0x68,  0x3a,  0x77,  0xfe,  0xfc,  0x10,  0x09,  0x04,
  0x6a,  0xfe,  0x72,  0x12,  0xc0,  0x38,  0xc1,  0x4e,  0xf4,  0xf5,  0x8e,  0xfe,  0xc6,  0x10,  0x1e,  0x58,
  0xfe,  0x26,  0x13,  0x05,  0x7b,  0x31,  0x7c,  0x77,  0xfe,  0x82,  0x0c,  0x0c,  0x54,  0x18,  0x55,  0x23,
  0x0c,  0x7b,  0x0c,  0x7c,  0x01,  0xa8,  0x24,  0x69,  0x73,  0x12,  0x58,  0x01,  0xa5,  0xc0,  0x38,  0xc1,
  0x4e,  0xfe,  0x04,  0x55,  0xfe,  0xa5,  0x55,  0xfe,  0x04,  0xfa,  0x38,  0xfe,  0x05,  0xfa,  0x4e,  0xfe,
  0x91,  0x10,  0x05,  0x56,  0x31,  0x57,  0xfe,  0x40,  0x56,  0xfe,  0xe1,  0x56,  0x0c,  0x56,  0x18,  0x57,
  0x83,  0xc0,  0x38,  0xc1,  0x4e,  0xf4,  0xf5,  0x05,  0x52,  0x31,  0x53,  0xfe,  0x00,  0x56,  0xfe,  0xa1,
  0x56,  0x0c,  0x52,  0x18,  0x53,  0x09,  0x04,  0x6a,  0xfe,  0x1e,  0x12,  0x1e,  0x58,  0xfe,  0x1f,  0x40,
  0x05,  0x54,  0x31,  0x55,  0xfe,  0x2c,  0x50,  0xfe,  0xae,  0x50,  0x05,  0x56,  0x31,  0x57,  0xfe,  0x44,
  0x50,  0xfe,  0xc6,  0x50,  0x05,  0x52,  0x31,  0x53,  0xfe,  0x08,  0x50,  0xfe,  0x8a,  0x50,  0x05,  0x39,
  0x31,  0x3a,  0xfe,  0x40,  0x50,  0xfe,  0xc2,  0x50,  0x02,  0x5c,  0x24,  0x06,  0x12,  0xcd,  0x02,  0x5b,
  0x2b,  0x01,  0x08,  0x1f,  0x44,  0x30,  0x2e,  0xd5,  0x07,  0x06,  0x21,  0x44,  0x2f,  0x07,  0x9b,  0x21,
  0x5b,  0x01,  0x6e,  0x1c,  0x3d,  0x16,  0x44,  0x09,  0x04,  0x0b,  0xe2,  0x79,  0x39,  0x68,  0x3a,  0xfe,
  0x0a,  0x55,  0x34,  0xfe,  0x8b,  0x55,  0xbe,  0x39,  0xbf,  0x3a,  0xfe,  0x0c,  0x51,  0xfe,  0x8e,  0x51,
  0x02,  0x5b,  0xfe,  0x19,  0x81,  0xaf,  0xfe,  0x19,  0x41,  0x02,  0x5b,  0x2b,  0x01,  0x08,  0x25,  0x32,
  0x1f,  0xa2,  0x30,  0x2e,  0xd8,  0x4b,  0x1a,  0xfe,  0xa6,  0x12,  0x4b,  0x0b,  0x3b,  0x02,  0x44,  0x01,
  0x08,  0x25,  0x32,  0x1f,  0xa2,  0x30,  0x2e,  0xd6,  0x07,  0x1a,  0x21,  0x44,  0x01,  0x08,  0x1f,  0xa2,
  0x30,  0x2e,  0xfe,  0xe8,  0x09,  0xfe,  0xc2,  0x49,  0x60,  0x05,  0xfe,  0x9c,  0x00,  0x28,  0x84,  0x49,
  0x04,  0x19,  0x34,  0x9f,  0xfe,  0xbb,  0x45,  0x4b,  0x00,  0x45,  0x3e,  0x06,  0x78,  0x3d,  0xfe,  0xda,
  0x14,  0x01,  0x6e,  0x87,  0xfe,  0x4b,  0x45,  0xe2,  0x2f,  0x07,  0x9a,  0xe1,  0x05,  0xc6,  0x28,  0x84,
  0x05,  0x3f,  0x28,  0x34,  0x5e,  0x02,  0x5b,  0xfe,  0xc0,  0x5d,  0xfe,  0xf8,  0x14,  0xfe,  0x03,  0x17,
  0x05,  0x50,  0xb4,  0x0c,  0x50,  0x5e,  0x2b,  0x01,  0x08,  0x26,  0x5c,  0x01,  0xfe,  0xaa,  0x14,  0x02,
  0x5c,  0x01,  0x08,  0x25,  0x32,  0x1f,  0x44,  0x30,  0x2e,  0xd6,  0x07,  0x06,  0x21,  0x44,  0x01,  0xfe,
  0x8e,  0x13,  0xfe,  0x42,  0x58,  0xfe,  0x82,  0x14,  0xfe,  0xa4,  0x14,  0x87,  0xfe,  0x4a,  0xf4,  0x0b,
  0x16,  0x44,  0xfe,  0x4a,  0xf4,  0x06,  0xfe,  0x0c,  0x12,  0x2f,  0x07,  0x9a,  0x85,  0x02,  0x5b,  0x05,
  0x3f,  0xb4,  0x0c,  0x3f,  0x5e,  0x2b,  0x01,  0x08,  0x26,  0x5c,  0x01,  0xfe,  0xd8,  0x14,  0x02,  0x5c,
  0x13,  0x06,  0x65,  0xfe,  0xca,  0x12,  0x26,  0xfe,  0xe0,  0x12,  0x72,  0xf1,  0x01,  0x08,  0x23,  0x72,
  0x03,  0x8f,  0xfe,  0xdc,  0x12,  0x25,  0xfe,  0xdc,  0x12,  0x1f,  0xfe,  0xca,  0x12,  0x5e,  0x2b,  0x01,
  0x08,  0xfe,  0xd5,  0x10,  0x13,  0x6c,  0xff,  0x02,  0x00,  0x57,  0x48,  0x8b,  0x1c,  0xfe,  0xff,  0x7f,
  0xfe,  0x30,  0x56,  0xfe,  0x00,  0x5c,  0x03,  0x13,  0x6c,  0xff,  0x02,  0x00,  0x57,  0x48,  0x8b,  0x1c,
  0x3d,  0xfe,  0x30,  0x56,  0xfe,  0x00,  0x5c,  0x03,  0x13,  0x6c,  0xff,  0x02,  0x00,  0x57,  0x48,  0x8b,
  0x03,  0x13,  0x6c,  0xff,  0x02,  0x00,  0x57,  0x48,  0x8b,  0xfe,  0x0b,  0x58,  0x03,  0x0a,  0x50,  0x01,
  0x82,  0x0a,  0x3f,  0x01,  0x82,  0x03,  0xfc,  0x1c,  0x10,  0xff,  0x03,  0x00,  0x54,  0xfe,  0x00,  0xf4,
  0x19,  0x48,  0xfe,  0x00,  0x7d,  0xfe,  0x01,  0x7d,  0xfe,  0x02,  0x7d,  0xfe,  0x03,  0x7c,  0x63,  0x27,
  0x0c,  0x52,  0x18,  0x53,  0xbe,  0x56,  0xbf,  0x57,  0x03,  0xfe,  0x62,  0x08,  0xfe,  0x82,  0x4a,  0xfe,
  0xe1,  0x1a,  0xfe,  0x83,  0x5a,  0x74,  0x03,  0x01,  0xfe,  0x14,  0x18,  0xfe,  0x42,  0x48,  0x5f,  0x60,
  0x89,  0x01,  0x08,  0x1f,  0xfe,  0xa2,  0x14,  0x30,  0x2e,  0xd8,  0x01,  0x08,  0x1f,  0xfe,  0xa2,  0x14,
  0x30,  0x2e,  0xfe,  0xe8,  0x0a,  0xfe,  0xc1,  0x59,  0x05,  0xc6,  0x28,  0xfe,  0xcc,  0x12,  0x49,  0x04,
  0x1b,  0xfe,  0xc4,  0x13,  0x23,  0x62,  0x1b,  0xe2,  0x4b,  0xc3,  0x64,  0xfe,  0xe8,  0x13,  0x3b,  0x13,
  0x06,  0x17,  0xc3,  0x78,  0xdb,  0xfe,  0x78,  0x10,  0xff,  0x02,  0x83,  0x55,  0xa1,  0xff,  0x02,  0x83,
  0x55,  0x62,  0x1a,  0xa4,  0xbb,  0xfe,  0x30,  0x00,  0x8e,  0xe4,  0x17,  0x2c,  0x13,  0x06,  0xfe,  0x56,
  0x10,  0x62,  0x0b,  0xe1,  0xbb,  0xfe,  0x64,  0x00,  0x8e,  0xe4,  0x0a,  0xfe,  0x64,  0x00,  0x17,  0x93,
  0x13,  0x06,  0xfe,  0x28,  0x10,  0x62,  0x06,  0xfe,  0x60,  0x13,  0xbb,  0xfe,  0xc8,  0x00,  0x8e,  0xe4,
  0x0a,  0xfe,  0xc8,  0x00,  0x17,  0x4d,  0x13,  0x06,  0x83,  0xbb,  0xfe,  0x90,  0x01,  0xba,  0xfe,  0x4e,
  0x14,  0x89,  0xfe,  0x12,  0x10,  0xfe,  0x43,  0xf4,  0x94,  0xfe,  0x56,  0xf0,  0xfe,  0x60,  0x14,  0xfe,
  0x04,  0xf4,  0x6c,  0xfe,  0x43,  0xf4,  0x93,  0xfe,  0xf3,  0x10,  0xf9,  0x01,  0xfe,  0x22,  0x13,  0x1c,
  0x3d,  0xfe,  0x10,  0x13,  0xfe,  0x00,  0x17,  0xfe,  0x4d,  0xe4,  0x69,  0xba,  0xfe,  0x9c,  0x14,  0xb7,
  0x69,  0xfe,  0x1c,  0x10,  0xfe,  0x00,  0x17,  0xfe,  0x4d,  0xe4,  0x19,  0xba,  0xfe,  0x9c,  0x14,  0xb7,
  0x19,  0x83,  0x60,  0x23,  0xfe,  0x4d,  0xf4,  0x00,  0xdf,  0x89,  0x13,  0x06,  0xfe,  0xb4,  0x56,  0xfe,
  0xc3,  0x58,  0x03,  0x60,  0x13,  0x0b,  0x03,  0x15,  0x06,  0x01,  0x08,  0x26,  0xe5,  0x15,  0x0b,  0x01,
  0x08,  0x26,  0xe5,  0x15,  0x1a,  0x01,  0x08,  0x26,  0xe5,  0x72,  0xfe,  0x89,  0x49,  0x01,  0x08,  0x03,
  0x15,  0x06,  0x01,  0x08,  0x26,  0xa6,  0x15,  0x1a,  0x01,  0x08,  0x26,  0xa6,  0x15,  0x06,  0x01,  0x08,
  0x26,  0xa6,  0xfe,  0x89,  0x49,  0x01,  0x08,  0x26,  0xa6,  0x72,  0xfe,  0x89,  0x4a,  0x01,  0x08,  0x03,
  0x60,  0x03,  0x1e,  0xcc,  0x07,  0x06,  0xfe,  0x44,  0x13,  0xad,  0x12,  0xcc,  0xfe,  0x49,  0xf4,  0x00,
  0x3b,  0x72,  0x9f,  0x5e,  0xfe,  0x01,  0xec,  0xfe,  0x27,  0x01,  0xf1,  0x01,  0x08,  0x2f,  0x07,  0xfe,
  0xe3,  0x00,  0xfe,  0x20,  0x13,  0x1f,  0xfe,  0x5a,  0x15,  0x23,  0x12,  0xcd,  0x01,  0x43,  0x1e,  0xcd,
  0x07,  0x06,  0x45,  0x09,  0x4a,  0x06,  0x35,  0x03,  0x0a,  0x42,  0x01,  0x0e,  0xed,  0x88,  0x07,  0x10,
  0xa4,  0x0a,  0x80,  0x01,  0x0e,  0x88,  0x0a,  0x51,  0x01,  0x9e,  0x03,  0x0a,  0x80,  0x01,  0x0e,  0x88,
  0xfe,  0x80,  0xe7,  0x10,  0x07,  0x10,  0x84,  0xfe,  0x45,  0x58,  0x01,  0xe3,  0x88,  0x03,  0x0a,  0x42,
  0x01,  0x0e,  0x88,  0x0a,  0x51,  0x01,  0x9e,  0x03,  0x0a,  0x42,  0x01,  0x0e,  0xfe,  0x80,  0x80,  0xf2,
  0xfe,  0x49,  0xe4,  0x10,  0xa4,  0x0a,  0x80,  0x01,  0x0e,  0xf2,  0x0a,  0x51,  0x01,  0x82,  0x03,  0x17,
  0x10,  0x71,  0x66,  0xfe,  0x60,  0x01,  0xfe,  0x18,  0xdf,  0xfe,  0x19,  0xde,  0xfe,  0x24,  0x1c,  0xfe,
  0x1d,  0xf7,  0x1d,  0x90,  0xfe,  0xf6,  0x15,  0x01,  0xfe,  0xfc,  0x16,  0xe0,  0x91,  0x1d,  0x66,  0xfe,
  0x2c,  0x01,  0xfe,  0x2f,  0x19,  0x03,  0xae,  0x21,  0xfe,  0xe6,  0x15,  0xfe,  0xda,  0x10,  0x17,  0x10,
  0x71,  0x05,  0xfe,  0x64,  0x01,  0xfe,  0x00,  0xf4,  0x19,  0xfe,  0x18,  0x58,  0x05,  0xfe,  0x66,  0x01,
  0xfe,  0x19,  0x58,  0x91,  0x19,  0xfe,  0x3c,  0x90,  0xfe,  0x30,  0xf4,  0x06,  0xfe,  0x3c,  0x50,  0x66,
  0xfe,  0x38,  0x00,  0xfe,  0x0f,  0x79,  0xfe,  0x1c,  0xf7,  0x19,  0x90,  0xfe,  0x40,  0x16,  0xfe,  0xb6,
  0x14,  0x34,  0x03,  0xae,  0x21,  0xfe,  0x18,  0x16,  0xfe,  0x9c,  0x10,  0x17,  0x10,  0x71,  0xfe,  0x83,
  0x5a,  0xfe,  0x18,  0xdf,  0xfe,  0x19,  0xde,  0xfe,  0x1d,  0xf7,  0x38,  0x90,  0xfe,  0x62,  0x16,  0xfe,
  0x94,  0x14,  0xfe,  0x10,  0x13,  0x91,  0x38,  0x66,  0x1b,  0xfe,  0xaf,  0x19,  0xfe,  0x98,  0xe7,  0x00,
  0x03,  0xae,  0x21,  0xfe,  0x56,  0x16,  0xfe,  0x6c,  0x10,  0x17,  0x10,  0x71,  0xfe,  0x30,  0xbc,  0xfe,
  0xb2,  0xbc,  0x91,  0xc5,  0x66,  0x1b,  0xfe,  0x0f,  0x79,  0xfe,  0x1c,  0xf7,  0xc5,  0x90,  0xfe,  0x9a,
  0x16,  0xfe,  0x5c,  0x14,  0x34,  0x03,  0xae,  0x21,  0xfe,  0x86,  0x16,  0xfe,  0x42,  0x10,  0xfe,  0x02,
  0xf6,  0x10,  0x71,  0xfe,  0x18,  0xfe,  0x54,  0xfe,  0x19,  0xfe,  0x55,  0xfc,  0xfe,  0x1d,  0xf7,  0x4f,
  0x90,  0xfe,  0xc0,  0x16,  0xfe,  0x36,  0x14,  0xfe,  0x1c,  0x13,  0x91,  0x4f,  0x47,  0xfe,  0x83,  0x58,
  0xfe,  0xaf,  0x19,  0xfe,  0x80,  0xe7,  0x10,  0xfe,  0x81,  0xe7,  0x10,  0x11,  0xfe,  0xdd,  0x00,  0x63,
  0x27,  0x03,  0x63,  0x27,  0xfe,  0x12,  0x45,  0x21,  0xfe,  0xb0,  0x16,  0x14,  0x06,  0x37,  0x95,  0xa9,
  0x02,  0x29,  0xfe,  0x39,  0xf0,  0xfe,  0x04,  0x17,  0x23,  0x03,  0xfe,  0x7e,  0x18,  0x1c,  0x1a,  0x5d,
  0x13,  0x0d,  0x03,  0x71,  0x05,  0xcb,  0x1c,  0x06,  0xfe,  0xef,  0x12,  0xfe,  0xe1,  0x10,  0x78,  0x2c,
  0x46,  0x2f,  0x07,  0x2d,  0xfe,  0x3c,  0x13,  0xfe,  0x82,  0x14,  0xfe,  0x42,  0x13,  0x3c,  0x8a,  0x0a,
  0x42,  0x01,  0x0e,  0xb0,  0xfe,  0x3e,  0x12,  0xf0,  0xfe,  0x45,  0x48,  0x01,  0xe3,  0xfe,  0x00,  0xcc,
  0xb0,  0xfe,  0xf3,  0x13,  0x3d,  0x75,  0x07,  0x10,  0xa3,  0x0a,  0x80,  0x01,  0x0e,  0xf2,  0x01,  0x6f,
  0xfe,  0x16,  0x10,  0x07,  0x7e,  0x85,  0xfe,  0x40,  0x14,  0xfe,  0x24,  0x12,  0xf6,  0xfe,  0xd6,  0xf0,
  0xfe,  0x24,  0x17,  0x17,  0x0b,  0x03,  0xfe,  0x9c,  0xe7,  0x0b,  0x0f,  0xfe,  0x15,  0x00,  0x59,  0x76,
  0x27,  0x01,  0xda,  0x17,  0x06,  0x03,  0x3c,  0x8a,  0x09,  0x4a,  0x1d,  0x35,  0x11,  0x2d,  0x01,  0x6f,
  0x17,  0x06,  0x03,  0xfe,  0x38,  0x90,  0xfe,  0xba,  0x90,  0x79,  0xc7,  0x68,  0xc8,  0xfe,  0x48,  0x55,
  0x34,  0xfe,  0xc9,  0x55,  0x03,  0x1e,  0x98,  0x73,  0x12,  0x98,  0x03,  0x0a,  0x99,  0x01,  0x0e,  0xf0,
  0x0a,  0x40,  0x01,  0x0e,  0xfe,  0x49,  0x44,  0x16,  0xfe,  0xf0,  0x17,  0x73,  0x75,  0x03,  0x0a,  0x42,
  0x01,  0x0e,  0x07,  0x10,  0x45,  0x0a,  0x51,  0x01,  0x9e,  0x0a,  0x40,  0x01,  0x0e,  0x73,  0x75,  0x03,
  0xfe,  0x4e,  0xe4,  0x1a,  0x64,  0xfe,  0x24,  0x18,  0x05,  0xfe,  0x90,  0x00,  0xfe,  0x3a,  0x45,  0x5b,
  0xfe,  0x4e,  0xe4,  0xc2,  0x64,  0xfe,  0x36,  0x18,  0x05,  0xfe,  0x92,  0x00,  0xfe,  0x02,  0xe6,  0x1b,
  0xdc,  0xfe,  0x4e,  0xe4,  0xfe,  0x0b,  0x00,  0x64,  0xfe,  0x48,  0x18,  0x05,  0xfe,  0x94,  0x00,  0xfe,
  0x02,  0xe6,  0x19,  0xfe,  0x08,  0x10,  0x05,  0xfe,  0x96,  0x00,  0xfe,  0x02,  0xe6,  0x2c,  0xfe,  0x4e,
  0x45,  0xfe,  0x0c,  0x12,  0xaf,  0xff,  0x04,  0x68,  0x54,  0xde,  0x1c,  0x69,  0x03,  0x07,  0x7a,  0xfe,
  0x5a,  0xf0,  0xfe,  0x74,  0x18,  0x24,  0xfe,  0x09,  0x00,  0xfe,  0x34,  0x10,  0x07,  0x1b,  0xfe,  0x5a,
  0xf0,  0xfe,  0x82,  0x18,  0x24,  0xc3,  0xfe,  0x26,  0x10,  0x07,  0x1a,  0x5d,  0x24,  0x2c,  0xdc,  0x07,
  0x0b,  0x5d,  0x24,  0x93,  0xfe,  0x0e,  0x10,  0x07,  0x06,  0x5d,  0x24,  0x4d,  0x9f,  0xad,  0x03,  0x14,
  0xfe,  0x09,  0x00,  0x01,  0x33,  0xfe,  0x04,  0xfe,  0x7d,  0x05,  0x7f,  0xf9,  0x03,  0x25,  0xfe,  0xca,
  0x18,  0xfe,  0x14,  0xf0,  0x08,  0x65,  0xfe,  0xc6,  0x18,  0x03,  0xff,  0x1a,  0x00,  0x00,
};

STATIC unsigned short _adv_asc3550_size =
        sizeof(_adv_asc3550_buf); /* 0x13AD */
STATIC ADV_DCNT _adv_asc3550_chksum =
        0x04D52DDDUL; /* Expanded little-endian checksum. */

/* Microcode buffer is kept after initialization for error recovery. */
STATIC unsigned char _adv_asc38C0800_buf[] = {
  0x00,  0x00,  0x00,  0xf2,  0x00,  0xf0,  0x00,  0xfc,  0x00,  0x16,  0x18,  0xe4,  0x01,  0x00,  0x48,  0xe4,
  0x18,  0x80,  0x03,  0xf6,  0x02,  0x00,  0xce,  0x19,  0x00,  0xfa,  0xff,  0xff,  0x1c,  0x0f,  0x00,  0xf6,
  0x9e,  0xe7,  0xff,  0x00,  0x82,  0xe7,  0x00,  0xea,  0x01,  0xfa,  0x01,  0xe6,  0x09,  0xe7,  0x55,  0xf0,
  0x01,  0xf6,  0x03,  0x00,  0x04,  0x00,  0x10,  0x00,  0x1e,  0xf0,  0x85,  0xf0,  0x18,  0xf4,  0x08,  0x00,
  0xbc,  0x00,  0x38,  0x54,  0x00,  0xec,  0xd5,  0xf0,  0x82,  0x0d,  0x00,  0xe6,  0x86,  0xf0,  0xb1,  0xf0,
  0x98,  0x57,  0x01,  0xfc,  0xb4,  0x00,  0xd4,  0x01,  0x0c,  0x1c,  0x3e,  0x1c,  0x3c,  0x00,  0xbb,  0x00,
  0x00,  0x10,  0xba,  0x19,  0x02,  0x80,  0x32,  0xf0,  0x7c,  0x0d,  0x02,  0x13,  0xba,  0x13,  0x18,  0x40,
  0x00,  0x57,  0x01,  0xea,  0x02,  0xfc,  0x03,  0xfc,  0x3e,  0x00,  0x6c,  0x01,  0x6e,  0x01,  0x74,  0x01,
  0x76,  0x01,  0xb9,  0x54,  0x3e,  0x57,  0x00,  0x80,  0x03,  0xe6,  0xb6,  0x00,  0xc0,  0x00,  0x01,  0x01,
  0x3e,  0x01,  0x7a,  0x01,  0xca,  0x08,  0xce,  0x10,  0x16,  0x11,  0x04,  0x12,  0x08,  0x12,  0x02,  0x4a,
  0xbb,  0x55,  0x3c,  0x56,  0x03,  0x58,  0x1b,  0x80,  0x30,  0xe4,  0x4b,  0xe4,  0x5d,  0xf0,  0x02,  0xfa,
  0x20,  0x00,  0x32,  0x00,  0x40,  0x00,  0x80,  0x00,  0x24,  0x01,  0x3c,  0x01,  0x68,  0x01,  0x6a,  0x01,
  0x70,  0x01,  0x72,  0x01,  0x78,  0x01,  0x7c,  0x01,  0x62,  0x0a,  0x86,  0x0d,  0x06,  0x13,  0x4c,  0x1c,
  0x04,  0x80,  0x4a,  0xe4,  0x02,  0xee,  0x5b,  0xf0,  0x03,  0xf7,  0x0c,  0x00,  0x0f,  0x00,  0x47,  0x00,
  0xbe,  0x00,  0x00,  0x01,  0x20,  0x11,  0x5c,  0x16,  0x32,  0x1c,  0x38,  0x1c,  0x4e,  0x1c,  0x10,  0x44,
  0x00,  0x4c,  0x04,  0xea,  0x5c,  0xf0,  0xa7,  0xf0,  0x04,  0xf6,  0x03,  0xfa,  0x05,  0x00,  0x34,  0x00,
  0x36,  0x00,  0x98,  0x00,  0xcc,  0x00,  0x20,  0x01,  0x4e,  0x01,  0x4a,  0x0b,  0x42,  0x0c,  0x12,  0x0f,
  0x0c,  0x10,  0x22,  0x11,  0x0a,  0x12,  0x04,  0x13,  0x30,  0x1c,  0x02,  0x48,  0x00,  0x4e,  0x42,  0x54,
  0x44,  0x55,  0xbd,  0x56,  0x06,  0x83,  0x00,  0xdc,  0x05,  0xf0,  0x09,  0xf0,  0x59,  0xf0,  0xb8,  0xf0,
  0x4b,  0xf4,  0x06,  0xf7,  0x0e,  0xf7,  0x04,  0xfc,  0x05,  0xfc,  0x06,  0x00,  0x19,  0x00,  0x33,  0x00,
  0x9b,  0x00,  0xa4,  0x00,  0xb5,  0x00,  0xba,  0x00,  0xd0,  0x00,  0xe1,  0x00,  0xe7,  0x00,  0xe2,  0x03,
  0x08,  0x0f,  0x02,  0x10,  0x04,  0x10,  0x0a,  0x10,  0x0a,  0x13,  0x0c,  0x13,  0x12,  0x13,  0x24,  0x14,
  0x34,  0x14,  0x04,  0x16,  0x08,  0x16,  0xa4,  0x17,  0x20,  0x1c,  0x34,  0x1c,  0x36,  0x1c,  0x08,  0x44,
  0x38,  0x44,  0x91,  0x44,  0x0a,  0x45,  0x48,  0x46,  0x01,  0x48,  0x68,  0x54,  0x3a,  0x55,  0x83,  0x55,
  0xe5,  0x55,  0xb0,  0x57,  0x01,  0x58,  0x83,  0x59,  0x05,  0xe6,  0x0b,  0xf0,  0x0c,  0xf0,  0x04,  0xf8,
  0x05,  0xf8,  0x07,  0x00,  0x0a,  0x00,  0x1c,  0x00,  0x1e,  0x00,  0x9e,  0x00,  0xa8,  0x00,  0xaa,  0x00,
  0xb9,  0x00,  0xe0,  0x00,  0x22,  0x01,  0x26,  0x01,  0x79,  0x01,  0x7e,  0x01,  0xc4,  0x01,  0xc6,  0x01,
  0x80,  0x02,  0x5e,  0x03,  0xee,  0x04,  0x9a,  0x06,  0xf8,  0x07,  0x62,  0x08,  0x68,  0x08,  0x69,  0x08,
  0xd6,  0x08,  0xe9,  0x09,  0xfa,  0x0b,  0x2e,  0x0f,  0x12,  0x10,  0x1a,  0x10,  0xed,  0x10,  0xf1,  0x10,
  0x2a,  0x11,  0x06,  0x12,  0x0c,  0x12,  0x3e,  0x12,  0x10,  0x13,  0x16,  0x13,  0x1e,  0x13,  0x46,  0x14,
  0x76,  0x14,  0x82,  0x14,  0x36,  0x15,  0xca,  0x15,  0x6b,  0x18,  0xbe,  0x18,  0xca,  0x18,  0xe6,  0x19,
  0x12,  0x1c,  0x46,  0x1c,  0x9c,  0x32,  0x00,  0x40,  0x0e,  0x47,  0xfe,  0x9c,  0xf0,  0x2b,  0x02,  0xfe,
  0xac,  0x0d,  0xff,  0x10,  0x00,  0x00,  0xd7,  0xfe,  0xe8,  0x19,  0x00,  0xd6,  0xfe,  0x84,  0x01,  0xff,
  0x03,  0x00,  0x00,  0xfe,  0x93,  0x15,  0xfe,  0x0f,  0x05,  0xff,  0x38,  0x00,  0x00,  0xfe,  0x57,  0x24,
  0x00,  0xfe,  0x4c,  0x00,  0x5b,  0xff,  0x04,  0x00,  0x00,  0x11,  0xff,  0x09,  0x00,  0x00,  0xff,  0x08,
  0x01,  0x01,  0xff,  0x08,  0xff,  0xff,  0xff,  0x27,  0x00,  0x00,  0xff,  0x10,  0xff,  0xff,  0xff,  0x11,
  0x00,  0x00,  0xfe,  0x78,  0x56,  0xfe,  0x34,  0x12,  0xff,  0x21,  0x00,  0x00,  0xfe,  0x04,  0xf7,  0xd6,
  0x2c,  0x99,  0x0a,  0x01,  0xfe,  0xc2,  0x0f,  0xfe,  0x04,  0xf7,  0xd6,  0x99,  0x0a,  0x42,  0x2c,  0xfe,
  0x3d,  0xf0,  0xfe,  0x06,  0x02,  0xfe,  0x20,  0xf0,  0xa7,  0xfe,  0x91,  0xf0,  0xfe,  0xf4,  0x01,  0xfe,
  0x90,  0xf0,  0xfe,  0xf4,  0x01,  0xfe,  0x8f,  0xf0,  0xa7,  0x03,  0x5d,  0x4d,  0x02,  0xfe,  0xc8,  0x0d,
  0x01,  0xfe,  0x38,  0x0e,  0xfe,  0xdd,  0x12,  0xfe,  0xfc,  0x10,  0xfe,  0x28,  0x1c,  0x03,  0xfe,  0xa6,
  0x00,  0xfe,  0xd3,  0x12,  0x41,  0x14,  0xfe,  0xa6,  0x00,  0xc2,  0xfe,  0x48,  0xf0,  0xfe,  0x8a,  0x02,
  0xfe,  0x49,  0xf0,  0xfe,  0xa4,  0x02,  0xfe,  0x4a,  0xf0,  0xfe,  0xc2,  0x02,  0xfe,  0x46,  0xf0,  0xfe,
  0x54,  0x02,  0xfe,  0x47,  0xf0,  0xfe,  0x5a,  0x02,  0xfe,  0x43,  0xf0,  0xfe,  0x48,  0x02,  0xfe,  0x44,
  0xf0,  0xfe,  0x4c,  0x02,  0xfe,  0x45,  0xf0,  0xfe,  0x50,  0x02,  0x18,  0x0a,  0xaa,  0x18,  0x06,  0x14,
  0xa1,  0x02,  0x2b,  0xfe,  0x00,  0x1c,  0xe7,  0xfe,  0x02,  0x1c,  0xe6,  0xfe,  0x1e,  0x1c,  0xfe,  0xe9,
  0x10,  0x01,  0xfe,  0x18,  0x18,  0xfe,  0xe7,  0x10,  0xfe,  0x06,  0xfc,  0xce,  0x09,  0x70,  0x01,  0xa8,
  0x02,  0x2b,  0x15,  0x59,  0x39,  0xa2,  0x01,  0xfe,  0x58,  0x10,  0x09,  0x70,  0x01,  0x87,  0xfe,  0xbd,
  0x10,  0x09,  0x70,  0x01,  0x87,  0xfe,  0xad,  0x10,  0xfe,  0x16,  0x1c,  0xfe,  0x58,  0x1c,  0x18,  0x06,
  0x14,  0xa1,  0x2c,  0x1c,  0x2b,  0xfe,  0x3d,  0xf0,  0xfe,  0x06,  0x02,  0x23,  0xfe,  0x98,  0x02,  0xfe,
  0x5a,  0x1c,  0xf8,  0xfe,  0x14,  0x1c,  0x15,  0xfe,  0x30,  0x00,  0x39,  0xa2,  0x01,  0xfe,  0x48,  0x10,
  0x18,  0x06,  0x14,  0xa1,  0x02,  0xd7,  0x22,  0x20,  0x07,  0x11,  0x35,  0xfe,  0x69,  0x10,  0x18,  0x06,
  0x14,  0xa1,  0xfe,  0x04,  0xec,  0x20,  0x4f,  0x43,  0x13,  0x20,  0xfe,  0x05,  0xf6,  0xce,  0x01,  0xfe,
  0x4a,  0x17,  0x08,  0x54,  0x58,  0x37,  0x12,  0x2f,  0x42,  0x92,  0x01,  0xfe,  0x82,  0x16,  0x02,  0x2b,
  0x09,  0x46,  0x01,  0x0e,  0x07,  0x00,  0x66,  0x01,  0x73,  0xfe,  0x18,  0x10,  0xfe,  0x41,  0x58,  0x09,
  0xa4,  0x01,  0x0e,  0xfe,  0xc8,  0x54,  0x6b,  0xfe,  0x10,  0x03,  0x01,  0xfe,  0x82,  0x16,  0x02,  0x2b,
  0x2c,  0x4f,  0xfe,  0x02,  0xe8,  0x2a,  0xfe,  0xbf,  0x57,  0xfe,  0x9e,  0x43,  0xfe,  0x77,  0x57,  0xfe,
  0x27,  0xf0,  0xfe,  0xe0,  0x01,  0xfe,  0x07,  0x4b,  0xfe,  0x20,  0xf0,  0xa7,  0xfe,  0x40,  0x1c,  0x1c,
  0xd9,  0xfe,  0x26,  0xf0,  0xfe,  0x5a,  0x03,  0xfe,  0xa0,  0xf0,  0xfe,  0x48,  0x03,  0xfe,  0x11,  0xf0,
  0xa7,  0xfe,  0xef,  0x10,  0xfe,  0x9f,  0xf0,  0xfe,  0x68,  0x03,  0xf9,  0x10,  0xfe,  0x11,  0x00,  0x02,
  0x65,  0x2c,  0xfe,  0x48,  0x1c,  0xf9,  0x08,  0x05,  0x1b,  0xfe,  0x18,  0x13,  0x21,  0x22,  0xa3,  0xb7,
  0x13,  0xa3,  0x09,  0x46,  0x01,  0x0e,  0xb7,  0x78,  0x01,  0xfe,  0xb4,  0x16,  0x12,  0xd1,  0x1c,  0xd9,
  0xfe,  0x01,  0xf0,  0xd9,  0xfe,  0x82,  0xf0,  0xfe,  0x96,  0x03,  0xfa,  0x12,  0xfe,  0xe4,  0x00,  0x27,
  0xfe,  0xa8,  0x03,  0x1c,  0x34,  0x1d,  0xfe,  0xb8,  0x03,  0x01,  0x4b,  0xfe,  0x06,  0xf0,  0xfe,  0xc8,
  0x03,  0x95,  0x86,  0xfe,  0x0a,  0xf0,  0xfe,  0x8a,  0x06,  0x02,  0x24,  0x03,  0x70,  0x28,  0x17,  0xfe,
  0xfa,  0x04,  0x15,  0x6d,  0x01,  0x36,  0x7b,  0xfe,  0x6a,  0x02,  0x02,  0xd8,  0xf9,  0x2c,  0x99,  0x19,
  0xfe,  0x67,  0x1b,  0xfe,  0xbf,  0x57,  0xfe,  0x77,  0x57,  0xfe,  0x48,  0x1c,  0x74,  0x01,  0xaf,  0x8c,
  0x09,  0x46,  0x01,  0x0e,  0x07,  0x00,  0x17,  0xda,  0x09,  0xd1,  0x01,  0x0e,  0x8d,  0x51,  0x64,  0x79,
  0x2a,  0x03,  0x70,  0x28,  0xfe,  0x10,  0x12,  0x15,  0x6d,  0x01,  0x36,  0x7b,  0xfe,  0x6a,  0x02,  0x02,
  0xd8,  0xc7,  0x81,  0xc8,  0x83,  0x1c,  0x24,  0x27,  0xfe,  0x40,  0x04,  0x1d,  0xfe,  0x3c,  0x04,  0x3b,
  0xfe,  0xa0,  0x00,  0xfe,  0x9b,  0x57,  0xfe,  0x4e,  0x12,  0x2d,  0xff,  0x02,  0x00,  0x10,  0x01,  0x0b,
  0x1d,  0xfe,  0xe4,  0x04,  0x2d,  0x01,  0x0b,  0x1d,  0x24,  0x33,  0x31,  0xde,  0xfe,  0x4c,  0x44,  0xfe,
  0x4c,  0x12,  0x51,  0xfe,  0x44,  0x48,  0x0f,  0x6f,  0xfe,  0x4c,  0x54,  0x6b,  0xda,  0x4f,  0x79,  0x2a,
  0xfe,  0x06,  0x80,  0xfe,  0x48,  0x47,  0xfe,  0x62,  0x13,  0x08,  0x05,  0x1b,  0xfe,  0x2a,  0x13,  0x32,
  0x07,  0x82,  0xfe,  0x52,  0x13,  0xfe,  0x20,  0x10,  0x0f,  0x6f,  0xfe,  0x4c,  0x54,  0x6b,  0xda,  0xfe,
  0x06,  0x80,  0xfe,  0x48,  0x47,  0xfe,  0x40,  0x13,  0x08,  0x05,  0x1b,  0xfe,  0x08,  0x13,  0x32,  0x07,
  0x82,  0xfe,  0x30,  0x13,  0x08,  0x05,  0x1b,  0xfe,  0x1c,  0x12,  0x15,  0x9d,  0x08,  0x05,  0x06,  0x4d,
  0x15,  0xfe,  0x0d,  0x00,  0x01,  0x36,  0x7b,  0xfe,  0x64,  0x0d,  0x02,  0x24,  0x2d,  0x12,  0xfe,  0xe6,
  0x00,  0xfe,  0x1c,  0x90,  0xfe,  0x40,  0x5c,  0x04,  0x15,  0x9d,  0x01,  0x36,  0x02,  0x2b,  0xfe,  0x42,
  0x5b,  0x99,  0x19,  0xfe,  0x46,  0x59,  0xfe,  0xbf,  0x57,  0xfe,  0x77,  0x57,  0xfe,  0x87,  0x80,  0xfe,
  0x31,  0xe4,  0x5b,  0x08,  0x05,  0x0a,  0xfe,  0x84,  0x13,  0xfe,  0x20,  0x80,  0x07,  0x19,  0xfe,  0x7c,
  0x12,  0x53,  0x05,  0x06,  0xfe,  0x6c,  0x13,  0x03,  0xfe,  0xa2,  0x00,  0x28,  0x17,  0xfe,  0x90,  0x05,
  0xfe,  0x31,  0xe4,  0x5a,  0x53,  0x05,  0x0a,  0xfe,  0x56,  0x13,  0x03,  0xfe,  0xa0,  0x00,  0x28,  0xfe,
  0x4e,  0x12,  0x67,  0xff,  0x02,  0x00,  0x10,  0x27,  0xfe,  0x48,  0x05,  0x1c,  0x34,  0xfe,  0x89,  0x48,
  0xff,  0x02,  0x00,  0x10,  0x27,  0xfe,  0x56,  0x05,  0x26,  0xfe,  0xa8,  0x05,  0x12,  0xfe,  0xe3,  0x00,
  0x21,  0x53,  0xfe,  0x4a,  0xf0,  0xfe,  0x76,  0x05,  0xfe,  0x49,  0xf0,  0xfe,  0x70,  0x05,  0x88,  0x25,
  0xfe,  0x21,  0x00,  0xab,  0x25,  0xfe,  0x22,  0x00,  0xaa,  0x25,  0x58,  0xfe,  0x09,  0x48,  0xff,  0x02,
  0x00,  0x10,  0x27,  0xfe,  0x86,  0x05,  0x26,  0xfe,  0xa8,  0x05,  0xfe,  0xe2,  0x08,  0x53,  0x05,  0xcb,
  0x4d,  0x01,  0xb0,  0x25,  0x06,  0x13,  0xd3,  0x39,  0xfe,  0x27,  0x01,  0x08,  0x05,  0x1b,  0xfe,  0x22,
  0x12,  0x41,  0x01,  0xb2,  0x15,  0x9d,  0x08,  0x05,  0x06,  0x4d,  0x15,  0xfe,  0x0d,  0x00,  0x01,  0x36,
  0x7b,  0xfe,  0x64,  0x0d,  0x02,  0x24,  0x03,  0xfe,  0x9c,  0x00,  0x28,  0xeb,  0x03,  0x5c,  0x28,  0xfe,
  0x36,  0x13,  0x41,  0x01,  0xb2,  0x26,  0xfe,  0x18,  0x06,  0x09,  0x06,  0x53,  0x05,  0x1f,  0xfe,  0x02,
  0x12,  0x50,  0x01,  0xfe,  0x9e,  0x15,  0x1d,  0xfe,  0x0e,  0x06,  0x12,  0xa5,  0x01,  0x4b,  0x12,  0xfe,
  0xe5,  0x00,  0x03,  0x5c,  0xc1,  0x0c,  0x5c,  0x03,  0xcd,  0x28,  0xfe,  0x62,  0x12,  0x03,  0x45,  0x28,
  0xfe,  0x5a,  0x13,  0x01,  0xfe,  0x0c,  0x19,  0x01,  0xfe,  0x76,  0x19,  0xfe,  0x43,  0x48,  0xc4,  0xcc,
  0x0f,  0x71,  0xff,  0x02,  0x00,  0x57,  0x52,  0x93,  0x1e,  0x43,  0x8b,  0xc4,  0x6e,  0x41,  0x01,  0xb2,
  0x26,  0xfe,  0x82,  0x06,  0x53,  0x05,  0x1a,  0xe9,  0x91,  0x09,  0x59,  0x01,  0xfe,  0xcc,  0x15,  0x1d,
  0xfe,  0x78,  0x06,  0x12,  0xa5,  0x01,  0x4b,  0x12,  0xfe,  0xe5,  0x00,  0x03,  0x45,  0xc1,  0x0c,  0x45,
  0x18,  0x06,  0x01,  0xb2,  0xfa,  0x76,  0x74,  0x01,  0xaf,  0x8c,  0x12,  0xfe,  0xe2,  0x00,  0x27,  0xdb,
  0x1c,  0x34,  0xfe,  0x0a,  0xf0,  0xfe,  0xb6,  0x06,  0x94,  0xfe,  0x6c,  0x07,  0xfe,  0x06,  0xf0,  0xfe,
  0x74,  0x07,  0x95,  0x86,  0x02,  0x24,  0x08,  0x05,  0x0a,  0xfe,  0x2e,  0x12,  0x16,  0x19,  0x01,  0x0b,
  0x16,  0x00,  0x01,  0x0b,  0x16,  0x00,  0x01,  0x0b,  0x16,  0x00,  0x01,  0x0b,  0xfe,  0x99,  0xa4,  0x01,
  0x0b,  0x16,  0x00,  0x02,  0xfe,  0x42,  0x08,  0x68,  0x05,  0x1a,  0xfe,  0x38,  0x12,  0x08,  0x05,  0x1a,
  0xfe,  0x30,  0x13,  0x16,  0xfe,  0x1b,  0x00,  0x01,  0x0b,  0x16,  0x00,  0x01,  0x0b,  0x16,  0x00,  0x01,
  0x0b,  0x16,  0x00,  0x01,  0x0b,  0x16,  0x06,  0x01,  0x0b,  0x16,  0x00,  0x02,  0xe2,  0x6c,  0x58,  0xbe,
  0x50,  0xfe,  0x9a,  0x81,  0x55,  0x1b,  0x7a,  0xfe,  0x42,  0x07,  0x09,  0x1b,  0xfe,  0x09,  0x6f,  0xba,
  0xfe,  0xca,  0x45,  0xfe,  0x32,  0x12,  0x69,  0x6d,  0x8b,  0x6c,  0x7f,  0x27,  0xfe,  0x54,  0x07,  0x1c,
  0x34,  0xfe,  0x0a,  0xf0,  0xfe,  0x42,  0x07,  0x95,  0x86,  0x94,  0xfe,  0x6c,  0x07,  0x02,  0x24,  0x01,
  0x4b,  0x02,  0xdb,  0x16,  0x1f,  0x02,  0xdb,  0xfe,  0x9c,  0xf7,  0xdc,  0xfe,  0x2c,  0x90,  0xfe,  0xae,
  0x90,  0x56,  0xfe,  0xda,  0x07,  0x0c,  0x60,  0x14,  0x61,  0x08,  0x54,  0x5a,  0x37,  0x22,  0x20,  0x07,
  0x11,  0xfe,  0x0e,  0x12,  0x8d,  0xfe,  0x80,  0x80,  0x39,  0x20,  0x6a,  0x2a,  0xfe,  0x06,  0x10,  0xfe,
  0x83,  0xe7,  0xfe,  0x48,  0x00,  0xab,  0xfe,  0x03,  0x40,  0x08,  0x54,  0x5b,  0x37,  0x01,  0xb3,  0xb8,
  0xfe,  0x1f,  0x40,  0x13,  0x62,  0x01,  0xef,  0xfe,  0x08,  0x50,  0xfe,  0x8a,  0x50,  0xfe,  0x44,  0x51,
  0xfe,  0xc6,  0x51,  0x88,  0xfe,  0x08,  0x90,  0xfe,  0x8a,  0x90,  0x0c,  0x5e,  0x14,  0x5f,  0xfe,  0x0c,
  0x90,  0xfe,  0x8e,  0x90,  0xfe,  0x40,  0x50,  0xfe,  0xc2,  0x50,  0x0c,  0x3d,  0x14,  0x3e,  0xfe,  0x4a,
  0x10,  0x08,  0x05,  0x5a,  0xfe,  0x2a,  0x12,  0xfe,  0x2c,  0x90,  0xfe,  0xae,  0x90,  0x0c,  0x60,  0x14,
  0x61,  0x08,  0x05,  0x5b,  0x8b,  0x01,  0xb3,  0xfe,  0x1f,  0x80,  0x13,  0x62,  0xfe,  0x44,  0x90,  0xfe,
  0xc6,  0x90,  0x0c,  0x3f,  0x14,  0x40,  0xfe,  0x08,  0x90,  0xfe,  0x8a,  0x90,  0x0c,  0x5e,  0x14,  0x5f,
  0xfe,  0x40,  0x90,  0xfe,  0xc2,  0x90,  0x0c,  0x3d,  0x14,  0x3e,  0x0c,  0x2e,  0x14,  0x3c,  0x21,  0x0c,
  0x49,  0x0c,  0x63,  0x08,  0x54,  0x1f,  0x37,  0x2c,  0x0f,  0xfe,  0x4e,  0x11,  0x27,  0xdd,  0xfe,  0x9e,
  0xf0,  0xfe,  0x76,  0x08,  0xbc,  0x17,  0x34,  0x2c,  0x77,  0xe6,  0xc5,  0xfe,  0x9a,  0x08,  0xc6,  0xfe,
  0xb8,  0x08,  0x94,  0xfe,  0x8e,  0x08,  0xfe,  0x06,  0xf0,  0xfe,  0x94,  0x08,  0x95,  0x86,  0x02,  0x24,
  0x01,  0x4b,  0xfe,  0xc9,  0x10,  0x16,  0x1f,  0xfe,  0xc9,  0x10,  0x68,  0x05,  0x06,  0xfe,  0x10,  0x12,
  0x68,  0x05,  0x0a,  0x4e,  0x08,  0x05,  0x0a,  0xfe,  0x90,  0x12,  0xfe,  0x2e,  0x1c,  0x02,  0xfe,  0x18,
  0x0b,  0x68,  0x05,  0x06,  0x4e,  0x68,  0x05,  0x0a,  0xfe,  0x7a,  0x12,  0xfe,  0x2c,  0x1c,  0xfe,  0xaa,
  0xf0,  0xfe,  0xd2,  0x09,  0xfe,  0xac,  0xf0,  0xfe,  0x00,  0x09,  0x02,  0xfe,  0xde,  0x09,  0xfe,  0xb7,
  0xf0,  0xfe,  0xfc,  0x08,  0xfe,  0x02,  0xf6,  0x1a,  0x50,  0xfe,  0x70,  0x18,  0xfe,  0xf1,  0x18,  0xfe,
  0x40,  0x55,  0xfe,  0xe1,  0x55,  0xfe,  0x10,  0x58,  0xfe,  0x91,  0x58,  0xfe,  0x14,  0x59,  0xfe,  0x95,
  0x59,  0x1c,  0x85,  0xfe,  0x8c,  0xf0,  0xfe,  0xfc,  0x08,  0xfe,  0xac,  0xf0,  0xfe,  0xf0,  0x08,  0xb5,
  0xfe,  0xcb,  0x10,  0xfe,  0xad,  0xf0,  0xfe,  0x0c,  0x09,  0x02,  0xfe,  0x18,  0x0b,  0xb6,  0xfe,  0xbf,
  0x10,  0xfe,  0x2b,  0xf0,  0x85,  0xf4,  0x1e,  0xfe,  0x00,  0xfe,  0xfe,  0x1c,  0x12,  0xc2,  0xfe,  0xd2,
  0xf0,  0x85,  0xfe,  0x76,  0x18,  0x1e,  0x19,  0x17,  0x85,  0x03,  0xd2,  0x1e,  0x06,  0x17,  0x85,  0xc5,
  0x4a,  0xc6,  0x4a,  0xb5,  0xb6,  0xfe,  0x89,  0x10,  0x74,  0x67,  0x2d,  0x15,  0x9d,  0x01,  0x36,  0x10,
  0xfe,  0x35,  0x00,  0xfe,  0x01,  0xf0,  0x65,  0x10,  0x80,  0x02,  0x65,  0xfe,  0x98,  0x80,  0xfe,  0x19,
  0xe4,  0x0a,  0xfe,  0x1a,  0x12,  0x51,  0xfe,  0x19,  0x82,  0xfe,  0x6c,  0x18,  0xfe,  0x44,  0x54,  0xbe,
  0xfe,  0x19,  0x81,  0xfe,  0x74,  0x18,  0x8f,  0x90,  0x17,  0xfe,  0xce,  0x08,  0x02,  0x4a,  0x08,  0x05,
  0x5a,  0xec,  0x03,  0x2e,  0x29,  0x3c,  0x0c,  0x3f,  0x14,  0x40,  0x9b,  0x2e,  0x9c,  0x3c,  0xfe,  0x6c,
  0x18,  0xfe,  0xed,  0x18,  0xfe,  0x44,  0x54,  0xfe,  0xe5,  0x54,  0x3a,  0x3f,  0x3b,  0x40,  0x03,  0x49,
  0x29,  0x63,  0x8f,  0xfe,  0xe3,  0x54,  0xfe,  0x74,  0x18,  0xfe,  0xf5,  0x18,  0x8f,  0xfe,  0xe3,  0x54,
  0x90,  0xc0,  0x56,  0xfe,  0xce,  0x08,  0x02,  0x4a,  0xfe,  0x37,  0xf0,  0xfe,  0xda,  0x09,  0xfe,  0x8b,
  0xf0,  0xfe,  0x60,  0x09,  0x02,  0x4a,  0x08,  0x05,  0x0a,  0x23,  0xfe,  0xfa,  0x0a,  0x3a,  0x49,  0x3b,
  0x63,  0x56,  0xfe,  0x3e,  0x0a,  0x0f,  0xfe,  0xc0,  0x07,  0x41,  0x98,  0x00,  0xad,  0xfe,  0x01,  0x59,
  0xfe,  0x52,  0xf0,  0xfe,  0x0c,  0x0a,  0x8f,  0x7a,  0xfe,  0x24,  0x0a,  0x3a,  0x49,  0x8f,  0xfe,  0xe3,
  0x54,  0x57,  0x49,  0x7d,  0x63,  0xfe,  0x14,  0x58,  0xfe,  0x95,  0x58,  0x02,  0x4a,  0x3a,  0x49,  0x3b,
  0x63,  0xfe,  0x14,  0x59,  0xfe,  0x95,  0x59,  0xbe,  0x57,  0x49,  0x57,  0x63,  0x02,  0x4a,  0x08,  0x05,
  0x5a,  0xfe,  0x82,  0x12,  0x08,  0x05,  0x1f,  0xfe,  0x66,  0x13,  0x22,  0x62,  0xb7,  0xfe,  0x03,  0xa1,
  0xfe,  0x83,  0x80,  0xfe,  0xc8,  0x44,  0xfe,  0x2e,  0x13,  0xfe,  0x04,  0x91,  0xfe,  0x86,  0x91,  0x6a,
  0x2a,  0xfe,  0x40,  0x59,  0xfe,  0xc1,  0x59,  0x56,  0xe0,  0x03,  0x60,  0x29,  0x61,  0x0c,  0x7f,  0x14,
  0x80,  0x57,  0x60,  0x7d,  0x61,  0x01,  0xb3,  0xb8,  0x6a,  0x2a,  0x13,  0x62,  0x9b,  0x2e,  0x9c,  0x3c,
  0x3a,  0x3f,  0x3b,  0x40,  0x90,  0xc0,  0xfe,  0x04,  0xfa,  0x2e,  0xfe,  0x05,  0xfa,  0x3c,  0x01,  0xef,
  0xfe,  0x36,  0x10,  0x21,  0x0c,  0x7f,  0x0c,  0x80,  0x3a,  0x3f,  0x3b,  0x40,  0xe4,  0x08,  0x05,  0x1f,
  0x17,  0xe0,  0x3a,  0x3d,  0x3b,  0x3e,  0x08,  0x05,  0xfe,  0xf7,  0x00,  0x37,  0x03,  0x5e,  0x29,  0x5f,
  0xfe,  0x10,  0x58,  0xfe,  0x91,  0x58,  0x57,  0x49,  0x7d,  0x63,  0x02,  0xfe,  0xf4,  0x09,  0x08,  0x05,
  0x1f,  0x17,  0xe0,  0x08,  0x05,  0xfe,  0xf7,  0x00,  0x37,  0xbe,  0xfe,  0x19,  0x81,  0x50,  0xfe,  0x10,
  0x90,  0xfe,  0x92,  0x90,  0xfe,  0xd3,  0x10,  0x32,  0x07,  0xa6,  0x17,  0xfe,  0x08,  0x09,  0x12,  0xa6,
  0x08,  0x05,  0x0a,  0xfe,  0x14,  0x13,  0x03,  0x3d,  0x29,  0x3e,  0x56,  0xfe,  0x08,  0x09,  0xfe,  0x0c,
  0x58,  0xfe,  0x8d,  0x58,  0x02,  0x4a,  0x21,  0x41,  0xfe,  0x19,  0x80,  0xe7,  0x08,  0x05,  0x0a,  0xfe,
  0x1a,  0x12,  0xfe,  0x6c,  0x19,  0xfe,  0x19,  0x41,  0xf4,  0xc2,  0xfe,  0xd1,  0xf0,  0xe2,  0x15,  0x7e,
  0x01,  0x36,  0x10,  0xfe,  0x44,  0x00,  0xfe,  0x8e,  0x10,  0xfe,  0x6c,  0x19,  0x57,  0x3d,  0xfe,  0xed,
  0x19,  0x7d,  0x3e,  0xfe,  0x0c,  0x51,  0xfe,  0x8e,  0x51,  0xf4,  0x1e,  0xfe,  0x00,  0xff,  0x35,  0xfe,
  0x74,  0x10,  0xc2,  0xfe,  0xd2,  0xf0,  0xfe,  0xa6,  0x0b,  0xfe,  0x76,  0x18,  0x1e,  0x19,  0x8a,  0x03,
  0xd2,  0x1e,  0x06,  0xfe,  0x08,  0x13,  0x10,  0xfe,  0x16,  0x00,  0x02,  0x65,  0xfe,  0xd1,  0xf0,  0xfe,
  0xb8,  0x0b,  0x15,  0x7e,  0x01,  0x36,  0x10,  0xfe,  0x17,  0x00,  0xfe,  0x42,  0x10,  0xfe,  0xce,  0xf0,
  0xfe,  0xbe,  0x0b,  0xfe,  0x3c,  0x10,  0xfe,  0xcd,  0xf0,  0xfe,  0xca,  0x0b,  0x10,  0xfe,  0x22,  0x00,
  0x02,  0x65,  0xfe,  0xcb,  0xf0,  0xfe,  0xd6,  0x0b,  0x10,  0xfe,  0x24,  0x00,  0x02,  0x65,  0xfe,  0xd0,
  0xf0,  0xfe,  0xe0,  0x0b,  0x10,  0x9e,  0xe5,  0xfe,  0xcf,  0xf0,  0xfe,  0xea,  0x0b,  0x10,  0x58,  0xfe,
  0x10,  0x10,  0xfe,  0xcc,  0xf0,  0xe2,  0x68,  0x05,  0x1f,  0x4d,  0x10,  0xfe,  0x12,  0x00,  0x2c,  0x0f,
  0xfe,  0x4e,  0x11,  0x27,  0xfe,  0x00,  0x0c,  0xfe,  0x9e,  0xf0,  0xfe,  0x14,  0x0c,  0xbc,  0x17,  0x34,
  0x2c,  0x77,  0xe6,  0xc5,  0x24,  0xc6,  0x24,  0x2c,  0xfa,  0x27,  0xfe,  0x20,  0x0c,  0x1c,  0x34,  0x94,
  0xfe,  0x3c,  0x0c,  0x95,  0x86,  0xc5,  0xdc,  0xc6,  0xdc,  0x02,  0x24,  0x01,  0x4b,  0xfe,  0xdb,  0x10,
  0x12,  0xfe,  0xe8,  0x00,  0xb5,  0xb6,  0x74,  0xc7,  0x81,  0xc8,  0x83,  0xfe,  0x89,  0xf0,  0x24,  0x33,
  0x31,  0xe1,  0xc7,  0x81,  0xc8,  0x83,  0x27,  0xfe,  0x66,  0x0c,  0x1d,  0x24,  0x33,  0x31,  0xdf,  0xbc,
  0x4e,  0x10,  0xfe,  0x42,  0x00,  0x02,  0x65,  0x7c,  0x06,  0xfe,  0x81,  0x49,  0x17,  0xfe,  0x2c,  0x0d,
  0x08,  0x05,  0x0a,  0xfe,  0x44,  0x13,  0x10,  0x00,  0x55,  0x0a,  0xfe,  0x54,  0x12,  0x55,  0xfe,  0x28,
  0x00,  0x23,  0xfe,  0x9a,  0x0d,  0x09,  0x46,  0x01,  0x0e,  0x07,  0x00,  0x66,  0x44,  0xfe,  0x28,  0x00,
  0xfe,  0xe2,  0x10,  0x01,  0xf5,  0x01,  0xf6,  0x09,  0xa4,  0x01,  0xfe,  0x26,  0x0f,  0x64,  0x12,  0x2f,
  0x01,  0x73,  0x02,  0x2b,  0x10,  0xfe,  0x44,  0x00,  0x55,  0x0a,  0xe9,  0x44,  0x0a,  0xfe,  0xb4,  0x10,
  0x01,  0xb0,  0x44,  0x0a,  0xfe,  0xaa,  0x10,  0x01,  0xb0,  0xfe,  0x19,  0x82,  0xfe,  0x34,  0x46,  0xac,
  0x44,  0x0a,  0x10,  0xfe,  0x43,  0x00,  0xfe,  0x96,  0x10,  0x08,  0x54,  0x0a,  0x37,  0x01,  0xf5,  0x01,
  0xf6,  0x64,  0x12,  0x2f,  0x01,  0x73,  0x99,  0x0a,  0x64,  0x42,  0x92,  0x02,  0xfe,  0x2e,  0x03,  0x08,
  0x05,  0x0a,  0x8a,  0x44,  0x0a,  0x10,  0x00,  0xfe,  0x5c,  0x10,  0x68,  0x05,  0x1a,  0xfe,  0x58,  0x12,
  0x08,  0x05,  0x1a,  0xfe,  0x50,  0x13,  0xfe,  0x1c,  0x1c,  0xfe,  0x9d,  0xf0,  0xfe,  0x50,  0x0d,  0xfe,
  0x1c,  0x1c,  0xfe,  0x9d,  0xf0,  0xfe,  0x56,  0x0d,  0x08,  0x54,  0x1a,  0x37,  0xfe,  0xa9,  0x10,  0x10,
  0xfe,  0x15,  0x00,  0xfe,  0x04,  0xe6,  0x0a,  0x50,  0xfe,  0x2e,  0x10,  0x10,  0xfe,  0x13,  0x00,  0xfe,
  0x10,  0x10,  0x10,  0x6f,  0xab,  0x10,  0xfe,  0x41,  0x00,  0xaa,  0x10,  0xfe,  0x24,  0x00,  0x8c,  0xb5,
  0xb6,  0x74,  0x03,  0x70,  0x28,  0x23,  0xd8,  0x50,  0xfe,  0x04,  0xe6,  0x1a,  0xfe,  0x9d,  0x41,  0xfe,
  0x1c,  0x42,  0x64,  0x01,  0xe3,  0x02,  0x2b,  0xf8,  0x15,  0x0a,  0x39,  0xa0,  0xb4,  0x15,  0xfe,  0x31,
  0x00,  0x39,  0xa2,  0x01,  0xfe,  0x48,  0x10,  0x02,  0xd7,  0x42,  0xfe,  0x06,  0xec,  0xd0,  0xfc,  0x44,
  0x1b,  0xfe,  0xce,  0x45,  0x35,  0x42,  0xfe,  0x06,  0xea,  0xd0,  0xfe,  0x47,  0x4b,  0x91,  0xfe,  0x75,
  0x57,  0x03,  0x5d,  0xfe,  0x98,  0x56,  0xfe,  0x38,  0x12,  0x09,  0x48,  0x01,  0x0e,  0xfe,  0x44,  0x48,
  0x4f,  0x08,  0x05,  0x1b,  0xfe,  0x1a,  0x13,  0x09,  0x46,  0x01,  0x0e,  0x41,  0xfe,  0x41,  0x58,  0x09,
  0xa4,  0x01,  0x0e,  0xfe,  0x49,  0x54,  0x96,  0xfe,  0x1e,  0x0e,  0x02,  0xfe,  0x2e,  0x03,  0x09,  0x5d,
  0xfe,  0xee,  0x14,  0xfc,  0x44,  0x1b,  0xfe,  0xce,  0x45,  0x35,  0x42,  0xfe,  0xce,  0x47,  0xfe,  0xad,
  0x13,  0x02,  0x2b,  0x22,  0x20,  0x07,  0x11,  0xfe,  0x9e,  0x12,  0x21,  0x13,  0x59,  0x13,  0x9f,  0x13,
  0xd5,  0x22,  0x2f,  0x41,  0x39,  0x2f,  0xbc,  0xad,  0xfe,  0xbc,  0xf0,  0xfe,  0xe0,  0x0e,  0x0f,  0x06,
  0x13,  0x59,  0x01,  0xfe,  0xda,  0x16,  0x03,  0xfe,  0x38,  0x01,  0x29,  0xfe,  0x3a,  0x01,  0x56,  0xfe,
  0xe4,  0x0e,  0xfe,  0x02,  0xec,  0xd5,  0x69,  0x00,  0x66,  0xfe,  0x04,  0xec,  0x20,  0x4f,  0xfe,  0x05,
  0xf6,  0xfe,  0x34,  0x01,  0x01,  0xfe,  0x4a,  0x17,  0xfe,  0x08,  0x90,  0xfe,  0x48,  0xf4,  0x0d,  0xfe,
  0x18,  0x13,  0xba,  0xfe,  0x02,  0xea,  0xd5,  0x69,  0x7e,  0xfe,  0xc5,  0x13,  0x15,  0x1a,  0x39,  0xa0,
  0xb4,  0xfe,  0x2e,  0x10,  0x03,  0xfe,  0x38,  0x01,  0x1e,  0xfe,  0xf0,  0xff,  0x0c,  0xfe,  0x60,  0x01,
  0x03,  0xfe,  0x3a,  0x01,  0x0c,  0xfe,  0x62,  0x01,  0x43,  0x13,  0x20,  0x25,  0x06,  0x13,  0x2f,  0x12,
  0x2f,  0x92,  0x0f,  0x06,  0x04,  0x21,  0x04,  0x22,  0x59,  0xfe,  0xf7,  0x12,  0x22,  0x9f,  0xb7,  0x13,
  0x9f,  0x07,  0x7e,  0xfe,  0x71,  0x13,  0xfe,  0x24,  0x1c,  0x15,  0x19,  0x39,  0xa0,  0xb4,  0xfe,  0xd9,
  0x10,  0xc3,  0xfe,  0x03,  0xdc,  0xfe,  0x73,  0x57,  0xfe,  0x80,  0x5d,  0x04,  0xc3,  0xfe,  0x03,  0xdc,
  0xfe,  0x5b,  0x57,  0xfe,  0x80,  0x5d,  0x04,  0xfe,  0x03,  0x57,  0xc3,  0x21,  0xfe,  0x00,  0xcc,  0x04,
  0xfe,  0x03,  0x57,  0xc3,  0x78,  0x04,  0x08,  0x05,  0x58,  0xfe,  0x22,  0x13,  0xfe,  0x1c,  0x80,  0x07,
  0x06,  0xfe,  0x1a,  0x13,  0xfe,  0x1e,  0x80,  0xed,  0xfe,  0x1d,  0x80,  0xae,  0xfe,  0x0c,  0x90,  0xfe,
  0x0e,  0x13,  0xfe,  0x0e,  0x90,  0xac,  0xfe,  0x3c,  0x90,  0xfe,  0x30,  0xf4,  0x0a,  0xfe,  0x3c,  0x50,
  0xaa,  0x01,  0xfe,  0x7a,  0x17,  0x32,  0x07,  0x2f,  0xad,  0x01,  0xfe,  0xb4,  0x16,  0x08,  0x05,  0x1b,
  0x4e,  0x01,  0xf5,  0x01,  0xf6,  0x12,  0xfe,  0xe9,  0x00,  0x08,  0x05,  0x58,  0xfe,  0x2c,  0x13,  0x01,
  0xfe,  0x0c,  0x17,  0xfe,  0x1e,  0x1c,  0xfe,  0x14,  0x90,  0xfe,  0x96,  0x90,  0x0c,  0xfe,  0x64,  0x01,
  0x14,  0xfe,  0x66,  0x01,  0x08,  0x05,  0x5b,  0xfe,  0x12,  0x12,  0xfe,  0x03,  0x80,  0x8d,  0xfe,  0x01,
  0xec,  0x20,  0xfe,  0x80,  0x40,  0x13,  0x20,  0x6a,  0x2a,  0x12,  0xcf,  0x64,  0x22,  0x20,  0xfb,  0x79,
  0x20,  0x04,  0xfe,  0x08,  0x1c,  0x03,  0xfe,  0xac,  0x00,  0xfe,  0x06,  0x58,  0x03,  0xfe,  0xae,  0x00,

  0xfe,  0x07,  0x58,  0x03,  0xfe,  0xb0,  0x00,  0xfe,  0x08,  0x58,  0x03,  0xfe,  0xb2,  0x00,  0xfe,  0x09,
  0x58,  0xfe,  0x0a,  0x1c,  0x25,  0x6e,  0x13,  0xd0,  0x21,  0x0c,  0x5c,  0x0c,  0x45,  0x0f,  0x46,  0x52,
  0x50,  0x18,  0x1b,  0xfe,  0x90,  0x4d,  0xfe,  0x91,  0x54,  0x23,  0xfe,  0xfc,  0x0f,  0x44,  0x11,  0x0f,
  0x48,  0x52,  0x18,  0x58,  0xfe,  0x90,  0x4d,  0xfe,  0x91,  0x54,  0x23,  0xe4,  0x25,  0x11,  0x13,  0x20,
  0x7c,  0x6f,  0x4f,  0x22,  0x20,  0xfb,  0x79,  0x20,  0x12,  0xcf,  0xfe,  0x14,  0x56,  0xfe,  0xd6,  0xf0,
  0xfe,  0x26,  0x10,  0xf8,  0x74,  0xfe,  0x14,  0x1c,  0xfe,  0x10,  0x1c,  0xfe,  0x18,  0x1c,  0x04,  0x42,
  0xfe,  0x0c,  0x14,  0xfc,  0xfe,  0x07,  0xe6,  0x1b,  0xfe,  0xce,  0x47,  0xfe,  0xf5,  0x13,  0x04,  0x01,
  0xb0,  0x7c,  0x6f,  0x4f,  0xfe,  0x06,  0x80,  0xfe,  0x48,  0x47,  0xfe,  0x42,  0x13,  0x32,  0x07,  0x2f,
  0xfe,  0x34,  0x13,  0x09,  0x48,  0x01,  0x0e,  0xbb,  0xfe,  0x36,  0x12,  0xfe,  0x41,  0x48,  0xfe,  0x45,
  0x48,  0x01,  0xf0,  0xfe,  0x00,  0xcc,  0xbb,  0xfe,  0xf3,  0x13,  0x43,  0x78,  0x07,  0x11,  0xac,  0x09,
  0x84,  0x01,  0x0e,  0xfe,  0x80,  0x5c,  0x01,  0x73,  0xfe,  0x0e,  0x10,  0x07,  0x82,  0x4e,  0xfe,  0x14,
  0x56,  0xfe,  0xd6,  0xf0,  0xfe,  0x60,  0x10,  0x04,  0xfe,  0x44,  0x58,  0x8d,  0xfe,  0x01,  0xec,  0xa2,
  0xfe,  0x9e,  0x40,  0xfe,  0x9d,  0xe7,  0x00,  0xfe,  0x9c,  0xe7,  0x1a,  0x79,  0x2a,  0x01,  0xe3,  0xfe,
  0xdd,  0x10,  0x2c,  0xc7,  0x81,  0xc8,  0x83,  0x33,  0x31,  0xde,  0x07,  0x1a,  0xfe,  0x48,  0x12,  0x07,
  0x0a,  0xfe,  0x56,  0x12,  0x07,  0x19,  0xfe,  0x30,  0x12,  0x07,  0xc9,  0x17,  0xfe,  0x32,  0x12,  0x07,
  0xfe,  0x23,  0x00,  0x17,  0xeb,  0x07,  0x06,  0x17,  0xfe,  0x9c,  0x12,  0x07,  0x1f,  0xfe,  0x12,  0x12,
  0x07,  0x00,  0x17,  0x24,  0x15,  0xc9,  0x01,  0x36,  0xa9,  0x2d,  0x01,  0x0b,  0x94,  0x4b,  0x04,  0x2d,
  0xdd,  0x09,  0xd1,  0x01,  0xfe,  0x26,  0x0f,  0x12,  0x82,  0x02,  0x2b,  0x2d,  0x32,  0x07,  0xa6,  0xfe,
  0xd9,  0x13,  0x3a,  0x3d,  0x3b,  0x3e,  0x56,  0xfe,  0xf0,  0x11,  0x08,  0x05,  0x5a,  0xfe,  0x72,  0x12,
  0x9b,  0x2e,  0x9c,  0x3c,  0x90,  0xc0,  0x96,  0xfe,  0xba,  0x11,  0x22,  0x62,  0xfe,  0x26,  0x13,  0x03,
  0x7f,  0x29,  0x80,  0x56,  0xfe,  0x76,  0x0d,  0x0c,  0x60,  0x14,  0x61,  0x21,  0x0c,  0x7f,  0x0c,  0x80,
  0x01,  0xb3,  0x25,  0x6e,  0x77,  0x13,  0x62,  0x01,  0xef,  0x9b,  0x2e,  0x9c,  0x3c,  0xfe,  0x04,  0x55,
  0xfe,  0xa5,  0x55,  0xfe,  0x04,  0xfa,  0x2e,  0xfe,  0x05,  0xfa,  0x3c,  0xfe,  0x91,  0x10,  0x03,  0x3f,
  0x29,  0x40,  0xfe,  0x40,  0x56,  0xfe,  0xe1,  0x56,  0x0c,  0x3f,  0x14,  0x40,  0x88,  0x9b,  0x2e,  0x9c,
  0x3c,  0x90,  0xc0,  0x03,  0x5e,  0x29,  0x5f,  0xfe,  0x00,  0x56,  0xfe,  0xa1,  0x56,  0x0c,  0x5e,  0x14,
  0x5f,  0x08,  0x05,  0x5a,  0xfe,  0x1e,  0x12,  0x22,  0x62,  0xfe,  0x1f,  0x40,  0x03,  0x60,  0x29,  0x61,
  0xfe,  0x2c,  0x50,  0xfe,  0xae,  0x50,  0x03,  0x3f,  0x29,  0x40,  0xfe,  0x44,  0x50,  0xfe,  0xc6,  0x50,
  0x03,  0x5e,  0x29,  0x5f,  0xfe,  0x08,  0x50,  0xfe,  0x8a,  0x50,  0x03,  0x3d,  0x29,  0x3e,  0xfe,  0x40,
  0x50,  0xfe,  0xc2,  0x50,  0x02,  0x89,  0x25,  0x06,  0x13,  0xd4,  0x02,  0x72,  0x2d,  0x01,  0x0b,  0x1d,
  0x4c,  0x33,  0x31,  0xde,  0x07,  0x06,  0x23,  0x4c,  0x32,  0x07,  0xa6,  0x23,  0x72,  0x01,  0xaf,  0x1e,
  0x43,  0x17,  0x4c,  0x08,  0x05,  0x0a,  0xee,  0x3a,  0x3d,  0x3b,  0x3e,  0xfe,  0x0a,  0x55,  0x35,  0xfe,
  0x8b,  0x55,  0x57,  0x3d,  0x7d,  0x3e,  0xfe,  0x0c,  0x51,  0xfe,  0x8e,  0x51,  0x02,  0x72,  0xfe,  0x19,
  0x81,  0xba,  0xfe,  0x19,  0x41,  0x02,  0x72,  0x2d,  0x01,  0x0b,  0x1c,  0x34,  0x1d,  0xe8,  0x33,  0x31,
  0xe1,  0x55,  0x19,  0xfe,  0xa6,  0x12,  0x55,  0x0a,  0x4d,  0x02,  0x4c,  0x01,  0x0b,  0x1c,  0x34,  0x1d,
  0xe8,  0x33,  0x31,  0xdf,  0x07,  0x19,  0x23,  0x4c,  0x01,  0x0b,  0x1d,  0xe8,  0x33,  0x31,  0xfe,  0xe8,
  0x09,  0xfe,  0xc2,  0x49,  0x51,  0x03,  0xfe,  0x9c,  0x00,  0x28,  0x8a,  0x53,  0x05,  0x1f,  0x35,  0xa9,
  0xfe,  0xbb,  0x45,  0x55,  0x00,  0x4e,  0x44,  0x06,  0x7c,  0x43,  0xfe,  0xda,  0x14,  0x01,  0xaf,  0x8c,
  0xfe,  0x4b,  0x45,  0xee,  0x32,  0x07,  0xa5,  0xed,  0x03,  0xcd,  0x28,  0x8a,  0x03,  0x45,  0x28,  0x35,
  0x67,  0x02,  0x72,  0xfe,  0xc0,  0x5d,  0xfe,  0xf8,  0x14,  0xfe,  0x03,  0x17,  0x03,  0x5c,  0xc1,  0x0c,
  0x5c,  0x67,  0x2d,  0x01,  0x0b,  0x26,  0x89,  0x01,  0xfe,  0x9e,  0x15,  0x02,  0x89,  0x01,  0x0b,  0x1c,
  0x34,  0x1d,  0x4c,  0x33,  0x31,  0xdf,  0x07,  0x06,  0x23,  0x4c,  0x01,  0xf1,  0xfe,  0x42,  0x58,  0xf1,
  0xfe,  0xa4,  0x14,  0x8c,  0xfe,  0x4a,  0xf4,  0x0a,  0x17,  0x4c,  0xfe,  0x4a,  0xf4,  0x06,  0xea,  0x32,
  0x07,  0xa5,  0x8b,  0x02,  0x72,  0x03,  0x45,  0xc1,  0x0c,  0x45,  0x67,  0x2d,  0x01,  0x0b,  0x26,  0x89,
  0x01,  0xfe,  0xcc,  0x15,  0x02,  0x89,  0x0f,  0x06,  0x27,  0xfe,  0xbe,  0x13,  0x26,  0xfe,  0xd4,  0x13,
  0x76,  0xfe,  0x89,  0x48,  0x01,  0x0b,  0x21,  0x76,  0x04,  0x7b,  0xfe,  0xd0,  0x13,  0x1c,  0xfe,  0xd0,
  0x13,  0x1d,  0xfe,  0xbe,  0x13,  0x67,  0x2d,  0x01,  0x0b,  0xfe,  0xd5,  0x10,  0x0f,  0x71,  0xff,  0x02,
  0x00,  0x57,  0x52,  0x93,  0x1e,  0xfe,  0xff,  0x7f,  0xfe,  0x30,  0x56,  0xfe,  0x00,  0x5c,  0x04,  0x0f,
  0x71,  0xff,  0x02,  0x00,  0x57,  0x52,  0x93,  0x1e,  0x43,  0xfe,  0x30,  0x56,  0xfe,  0x00,  0x5c,  0x04,
  0x0f,  0x71,  0xff,  0x02,  0x00,  0x57,  0x52,  0x93,  0x04,  0x0f,  0x71,  0xff,  0x02,  0x00,  0x57,  0x52,
  0x93,  0xfe,  0x0b,  0x58,  0x04,  0x09,  0x5c,  0x01,  0x87,  0x09,  0x45,  0x01,  0x87,  0x04,  0xfe,  0x03,
  0xa1,  0x1e,  0x11,  0xff,  0x03,  0x00,  0x54,  0xfe,  0x00,  0xf4,  0x1f,  0x52,  0xfe,  0x00,  0x7d,  0xfe,
  0x01,  0x7d,  0xfe,  0x02,  0x7d,  0xfe,  0x03,  0x7c,  0x6a,  0x2a,  0x0c,  0x5e,  0x14,  0x5f,  0x57,  0x3f,
  0x7d,  0x40,  0x04,  0xdd,  0xfe,  0x82,  0x4a,  0xfe,  0xe1,  0x1a,  0xfe,  0x83,  0x5a,  0x8d,  0x04,  0x01,
  0xfe,  0x0c,  0x19,  0xfe,  0x42,  0x48,  0x50,  0x51,  0x91,  0x01,  0x0b,  0x1d,  0xfe,  0x96,  0x15,  0x33,
  0x31,  0xe1,  0x01,  0x0b,  0x1d,  0xfe,  0x96,  0x15,  0x33,  0x31,  0xfe,  0xe8,  0x0a,  0xfe,  0xc1,  0x59,
  0x03,  0xcd,  0x28,  0xfe,  0xcc,  0x12,  0x53,  0x05,  0x1a,  0xfe,  0xc4,  0x13,  0x21,  0x69,  0x1a,  0xee,
  0x55,  0xca,  0x6b,  0xfe,  0xdc,  0x14,  0x4d,  0x0f,  0x06,  0x18,  0xca,  0x7c,  0x30,  0xfe,  0x78,  0x10,
  0xff,  0x02,  0x83,  0x55,  0xab,  0xff,  0x02,  0x83,  0x55,  0x69,  0x19,  0xae,  0x98,  0xfe,  0x30,  0x00,
  0x96,  0xf2,  0x18,  0x6d,  0x0f,  0x06,  0xfe,  0x56,  0x10,  0x69,  0x0a,  0xed,  0x98,  0xfe,  0x64,  0x00,
  0x96,  0xf2,  0x09,  0xfe,  0x64,  0x00,  0x18,  0x9e,  0x0f,  0x06,  0xfe,  0x28,  0x10,  0x69,  0x06,  0xfe,
  0x60,  0x13,  0x98,  0xfe,  0xc8,  0x00,  0x96,  0xf2,  0x09,  0xfe,  0xc8,  0x00,  0x18,  0x59,  0x0f,  0x06,
  0x88,  0x98,  0xfe,  0x90,  0x01,  0x7a,  0xfe,  0x42,  0x15,  0x91,  0xe4,  0xfe,  0x43,  0xf4,  0x9f,  0xfe,
  0x56,  0xf0,  0xfe,  0x54,  0x15,  0xfe,  0x04,  0xf4,  0x71,  0xfe,  0x43,  0xf4,  0x9e,  0xfe,  0xf3,  0x10,
  0xfe,  0x40,  0x5c,  0x01,  0xfe,  0x16,  0x14,  0x1e,  0x43,  0xec,  0xfe,  0x00,  0x17,  0xfe,  0x4d,  0xe4,
  0x6e,  0x7a,  0xfe,  0x90,  0x15,  0xc4,  0x6e,  0xfe,  0x1c,  0x10,  0xfe,  0x00,  0x17,  0xfe,  0x4d,  0xe4,
  0xcc,  0x7a,  0xfe,  0x90,  0x15,  0xc4,  0xcc,  0x88,  0x51,  0x21,  0xfe,  0x4d,  0xf4,  0x00,  0xe9,  0x91,
  0x0f,  0x06,  0xfe,  0xb4,  0x56,  0xfe,  0xc3,  0x58,  0x04,  0x51,  0x0f,  0x0a,  0x04,  0x16,  0x06,  0x01,
  0x0b,  0x26,  0xf3,  0x16,  0x0a,  0x01,  0x0b,  0x26,  0xf3,  0x16,  0x19,  0x01,  0x0b,  0x26,  0xf3,  0x76,
  0xfe,  0x89,  0x49,  0x01,  0x0b,  0x04,  0x16,  0x06,  0x01,  0x0b,  0x26,  0xb1,  0x16,  0x19,  0x01,  0x0b,
  0x26,  0xb1,  0x16,  0x06,  0x01,  0x0b,  0x26,  0xb1,  0xfe,  0x89,  0x49,  0x01,  0x0b,  0x26,  0xb1,  0x76,
  0xfe,  0x89,  0x4a,  0x01,  0x0b,  0x04,  0x51,  0x04,  0x22,  0xd3,  0x07,  0x06,  0xfe,  0x48,  0x13,  0xb8,
  0x13,  0xd3,  0xfe,  0x49,  0xf4,  0x00,  0x4d,  0x76,  0xa9,  0x67,  0xfe,  0x01,  0xec,  0xfe,  0x27,  0x01,
  0xfe,  0x89,  0x48,  0xff,  0x02,  0x00,  0x10,  0x27,  0xfe,  0x2e,  0x16,  0x32,  0x07,  0xfe,  0xe3,  0x00,
  0xfe,  0x20,  0x13,  0x1d,  0xfe,  0x52,  0x16,  0x21,  0x13,  0xd4,  0x01,  0x4b,  0x22,  0xd4,  0x07,  0x06,
  0x4e,  0x08,  0x54,  0x06,  0x37,  0x04,  0x09,  0x48,  0x01,  0x0e,  0xfb,  0x8e,  0x07,  0x11,  0xae,  0x09,
  0x84,  0x01,  0x0e,  0x8e,  0x09,  0x5d,  0x01,  0xa8,  0x04,  0x09,  0x84,  0x01,  0x0e,  0x8e,  0xfe,  0x80,
  0xe7,  0x11,  0x07,  0x11,  0x8a,  0xfe,  0x45,  0x58,  0x01,  0xf0,  0x8e,  0x04,  0x09,  0x48,  0x01,  0x0e,
  0x8e,  0x09,  0x5d,  0x01,  0xa8,  0x04,  0x09,  0x48,  0x01,  0x0e,  0xfe,  0x80,  0x80,  0xfe,  0x80,  0x4c,
  0xfe,  0x49,  0xe4,  0x11,  0xae,  0x09,  0x84,  0x01,  0x0e,  0xfe,  0x80,  0x4c,  0x09,  0x5d,  0x01,  0x87,
  0x04,  0x18,  0x11,  0x75,  0x6c,  0xfe,  0x60,  0x01,  0xfe,  0x18,  0xdf,  0xfe,  0x19,  0xde,  0xfe,  0x24,
  0x1c,  0xfe,  0x1d,  0xf7,  0x1b,  0x97,  0xfe,  0xee,  0x16,  0x01,  0xfe,  0xf4,  0x17,  0xad,  0x9a,  0x1b,
  0x6c,  0xfe,  0x2c,  0x01,  0xfe,  0x2f,  0x19,  0x04,  0xb9,  0x23,  0xfe,  0xde,  0x16,  0xfe,  0xda,  0x10,
  0x18,  0x11,  0x75,  0x03,  0xfe,  0x64,  0x01,  0xfe,  0x00,  0xf4,  0x1f,  0xfe,  0x18,  0x58,  0x03,  0xfe,
  0x66,  0x01,  0xfe,  0x19,  0x58,  0x9a,  0x1f,  0xfe,  0x3c,  0x90,  0xfe,  0x30,  0xf4,  0x06,  0xfe,  0x3c,
  0x50,  0x6c,  0xfe,  0x38,  0x00,  0xfe,  0x0f,  0x79,  0xfe,  0x1c,  0xf7,  0x1f,  0x97,  0xfe,  0x38,  0x17,
  0xfe,  0xb6,  0x14,  0x35,  0x04,  0xb9,  0x23,  0xfe,  0x10,  0x17,  0xfe,  0x9c,  0x10,  0x18,  0x11,  0x75,
  0xfe,  0x83,  0x5a,  0xfe,  0x18,  0xdf,  0xfe,  0x19,  0xde,  0xfe,  0x1d,  0xf7,  0x2e,  0x97,  0xfe,  0x5a,
  0x17,  0xfe,  0x94,  0x14,  0xec,  0x9a,  0x2e,  0x6c,  0x1a,  0xfe,  0xaf,  0x19,  0xfe,  0x98,  0xe7,  0x00,
  0x04,  0xb9,  0x23,  0xfe,  0x4e,  0x17,  0xfe,  0x6c,  0x10,  0x18,  0x11,  0x75,  0xfe,  0x30,  0xbc,  0xfe,
  0xb2,  0xbc,  0x9a,  0xcb,  0x6c,  0x1a,  0xfe,  0x0f,  0x79,  0xfe,  0x1c,  0xf7,  0xcb,  0x97,  0xfe,  0x92,
  0x17,  0xfe,  0x5c,  0x14,  0x35,  0x04,  0xb9,  0x23,  0xfe,  0x7e,  0x17,  0xfe,  0x42,  0x10,  0xfe,  0x02,
  0xf6,  0x11,  0x75,  0xfe,  0x18,  0xfe,  0x60,  0xfe,  0x19,  0xfe,  0x61,  0xfe,  0x03,  0xa1,  0xfe,  0x1d,
  0xf7,  0x5b,  0x97,  0xfe,  0xb8,  0x17,  0xfe,  0x36,  0x14,  0xfe,  0x1c,  0x13,  0x9a,  0x5b,  0x41,  0xfe,
  0x83,  0x58,  0xfe,  0xaf,  0x19,  0xfe,  0x80,  0xe7,  0x11,  0xfe,  0x81,  0xe7,  0x11,  0x12,  0xfe,  0xdd,
  0x00,  0x6a,  0x2a,  0x04,  0x6a,  0x2a,  0xfe,  0x12,  0x45,  0x23,  0xfe,  0xa8,  0x17,  0x15,  0x06,  0x39,
  0xa0,  0xb4,  0x02,  0x2b,  0xfe,  0x39,  0xf0,  0xfe,  0xfc,  0x17,  0x21,  0x04,  0xfe,  0x7e,  0x18,  0x1e,
  0x19,  0x66,  0x0f,  0x0d,  0x04,  0x75,  0x03,  0xd2,  0x1e,  0x06,  0xfe,  0xef,  0x12,  0xfe,  0xe1,  0x10,
  0x7c,  0x6f,  0x4f,  0x32,  0x07,  0x2f,  0xfe,  0x3c,  0x13,  0xf1,  0xfe,  0x42,  0x13,  0x42,  0x92,  0x09,
  0x48,  0x01,  0x0e,  0xbb,  0xeb,  0xfe,  0x41,  0x48,  0xfe,  0x45,  0x48,  0x01,  0xf0,  0xfe,  0x00,  0xcc,
  0xbb,  0xfe,  0xf3,  0x13,  0x43,  0x78,  0x07,  0x11,  0xac,  0x09,  0x84,  0x01,  0x0e,  0xfe,  0x80,  0x4c,
  0x01,  0x73,  0xfe,  0x16,  0x10,  0x07,  0x82,  0x8b,  0xfe,  0x40,  0x14,  0xfe,  0x24,  0x12,  0xfe,  0x14,
  0x56,  0xfe,  0xd6,  0xf0,  0xfe,  0x1c,  0x18,  0x18,  0x0a,  0x04,  0xfe,  0x9c,  0xe7,  0x0a,  0x10,  0xfe,
  0x15,  0x00,  0x64,  0x79,  0x2a,  0x01,  0xe3,  0x18,  0x06,  0x04,  0x42,  0x92,  0x08,  0x54,  0x1b,  0x37,
  0x12,  0x2f,  0x01,  0x73,  0x18,  0x06,  0x04,  0xfe,  0x38,  0x90,  0xfe,  0xba,  0x90,  0x3a,  0xce,  0x3b,
  0xcf,  0xfe,  0x48,  0x55,  0x35,  0xfe,  0xc9,  0x55,  0x04,  0x22,  0xa3,  0x77,  0x13,  0xa3,  0x04,  0x09,
  0xa4,  0x01,  0x0e,  0xfe,  0x41,  0x48,  0x09,  0x46,  0x01,  0x0e,  0xfe,  0x49,  0x44,  0x17,  0xfe,  0xe8,
  0x18,  0x77,  0x78,  0x04,  0x09,  0x48,  0x01,  0x0e,  0x07,  0x11,  0x4e,  0x09,  0x5d,  0x01,  0xa8,  0x09,
  0x46,  0x01,  0x0e,  0x77,  0x78,  0x04,  0xfe,  0x4e,  0xe4,  0x19,  0x6b,  0xfe,  0x1c,  0x19,  0x03,  0xfe,
  0x90,  0x00,  0xfe,  0x3a,  0x45,  0xfe,  0x2c,  0x10,  0xfe,  0x4e,  0xe4,  0xc9,  0x6b,  0xfe,  0x2e,  0x19,
  0x03,  0xfe,  0x92,  0x00,  0xfe,  0x02,  0xe6,  0x1a,  0xe5,  0xfe,  0x4e,  0xe4,  0xfe,  0x0b,  0x00,  0x6b,
  0xfe,  0x40,  0x19,  0x03,  0xfe,  0x94,  0x00,  0xfe,  0x02,  0xe6,  0x1f,  0xfe,  0x08,  0x10,  0x03,  0xfe,
  0x96,  0x00,  0xfe,  0x02,  0xe6,  0x6d,  0xfe,  0x4e,  0x45,  0xea,  0xba,  0xff,  0x04,  0x68,  0x54,  0xe7,
  0x1e,  0x6e,  0xfe,  0x08,  0x1c,  0xfe,  0x67,  0x19,  0xfe,  0x0a,  0x1c,  0xfe,  0x1a,  0xf4,  0xfe,  0x00,
  0x04,  0xea,  0xfe,  0x48,  0xf4,  0x19,  0x7a,  0xfe,  0x74,  0x19,  0x0f,  0x19,  0x04,  0x07,  0x7e,  0xfe,
  0x5a,  0xf0,  0xfe,  0x84,  0x19,  0x25,  0xfe,  0x09,  0x00,  0xfe,  0x34,  0x10,  0x07,  0x1a,  0xfe,  0x5a,
  0xf0,  0xfe,  0x92,  0x19,  0x25,  0xca,  0xfe,  0x26,  0x10,  0x07,  0x19,  0x66,  0x25,  0x6d,  0xe5,  0x07,
  0x0a,  0x66,  0x25,  0x9e,  0xfe,  0x0e,  0x10,  0x07,  0x06,  0x66,  0x25,  0x59,  0xa9,  0xb8,  0x04,  0x15,
  0xfe,  0x09,  0x00,  0x01,  0x36,  0xfe,  0x04,  0xfe,  0x81,  0x03,  0x83,  0xfe,  0x40,  0x5c,  0x04,  0x1c,
  0xf7,  0xfe,  0x14,  0xf0,  0x0b,  0x27,  0xfe,  0xd6,  0x19,  0x1c,  0xf7,  0x7b,  0xf7,  0xfe,  0x82,  0xf0,
  0xfe,  0xda,  0x19,  0x04,  0xff,  0xcc,  0x00,  0x00,
};

STATIC unsigned short _adv_asc38C0800_size =
        sizeof(_adv_asc38C0800_buf); /* 0x14E1 */
STATIC ADV_DCNT _adv_asc38C0800_chksum =
        0x050D3FD8UL; /* Expanded little-endian checksum. */

/* Microcode buffer is kept after initialization for error recovery. */
STATIC unsigned char _adv_asc38C1600_buf[] = {
  0x00,  0x00,  0x00,  0xf2,  0x00,  0x16,  0x00,  0xfc,  0x00,  0x10,  0x00,  0xf0,  0x18,  0xe4,  0x01,  0x00,
  0x04,  0x1e,  0x48,  0xe4,  0x03,  0xf6,  0xf7,  0x13,  0x2e,  0x1e,  0x02,  0x00,  0x07,  0x17,  0xc0,  0x5f,
  0x00,  0xfa,  0xff,  0xff,  0x04,  0x00,  0x00,  0xf6,  0x09,  0xe7,  0x82,  0xe7,  0x85,  0xf0,  0x86,  0xf0,
  0x4e,  0x10,  0x9e,  0xe7,  0xff,  0x00,  0x55,  0xf0,  0x01,  0xf6,  0x03,  0x00,  0x98,  0x57,  0x01,  0xe6,
  0x00,  0xea,  0x00,  0xec,  0x01,  0xfa,  0x18,  0xf4,  0x08,  0x00,  0xf0,  0x1d,  0x38,  0x54,  0x32,  0xf0,
  0x10,  0x00,  0xc2,  0x0e,  0x1e,  0xf0,  0xd5,  0xf0,  0xbc,  0x00,  0x4b,  0xe4,  0x00,  0xe6,  0xb1,  0xf0,
  0xb4,  0x00,  0x02,  0x13,  0x3e,  0x1c,  0xc8,  0x47,  0x3e,  0x00,  0xd8,  0x01,  0x06,  0x13,  0x0c,  0x1c,
  0x5e,  0x1e,  0x00,  0x57,  0xc8,  0x57,  0x01,  0xfc,  0xbc,  0x0e,  0xa2,  0x12,  0xb9,  0x54,  0x00,  0x80,
  0x62,  0x0a,  0x5a,  0x12,  0xc8,  0x15,  0x3e,  0x1e,  0x18,  0x40,  0xbd,  0x56,  0x03,  0xe6,  0x01,  0xea,
  0x5c,  0xf0,  0x0f,  0x00,  0x20,  0x00,  0x6c,  0x01,  0x6e,  0x01,  0x04,  0x12,  0x04,  0x13,  0xbb,  0x55,
  0x3c,  0x56,  0x3e,  0x57,  0x03,  0x58,  0x4a,  0xe4,  0x40,  0x00,  0xb6,  0x00,  0xbb,  0x00,  0xc0,  0x00,
  0x00,  0x01,  0x01,  0x01,  0x3e,  0x01,  0x58,  0x0a,  0x44,  0x10,  0x0a,  0x12,  0x4c,  0x1c,  0x4e,  0x1c,
  0x02,  0x4a,  0x30,  0xe4,  0x05,  0xe6,  0x0c,  0x00,  0x3c,  0x00,  0x80,  0x00,  0x24,  0x01,  0x3c,  0x01,
  0x68,  0x01,  0x6a,  0x01,  0x70,  0x01,  0x72,  0x01,  0x74,  0x01,  0x76,  0x01,  0x78,  0x01,  0x7c,  0x01,
  0xc6,  0x0e,  0x0c,  0x10,  0xac,  0x12,  0xae,  0x12,  0x16,  0x1a,  0x32,  0x1c,  0x6e,  0x1e,  0x02,  0x48,
  0x3a,  0x55,  0xc9,  0x57,  0x02,  0xee,  0x5b,  0xf0,  0x03,  0xf7,  0x06,  0xf7,  0x03,  0xfc,  0x06,  0x00,
  0x1e,  0x00,  0xbe,  0x00,  0xe1,  0x00,  0x0c,  0x12,  0x18,  0x1a,  0x70,  0x1a,  0x30,  0x1c,  0x38,  0x1c,
  0x10,  0x44,  0x00,  0x4c,  0xb0,  0x57,  0x40,  0x5c,  0x4d,  0xe4,  0x04,  0xea,  0x5d,  0xf0,  0xa7,  0xf0,
  0x04,  0xf6,  0x02,  0xfc,  0x05,  0x00,  0x09,  0x00,  0x19,  0x00,  0x32,  0x00,  0x33,  0x00,  0x34,  0x00,
  0x36,  0x00,  0x98,  0x00,  0x9e,  0x00,  0xcc,  0x00,  0x20,  0x01,  0x4e,  0x01,  0x79,  0x01,  0x3c,  0x09,
  0x68,  0x0d,  0x02,  0x10,  0x04,  0x10,  0x3a,  0x10,  0x08,  0x12,  0x0a,  0x13,  0x40,  0x16,  0x50,  0x16,
  0x00,  0x17,  0x4a,  0x19,  0x00,  0x4e,  0x00,  0x54,  0x01,  0x58,  0x00,  0xdc,  0x05,  0xf0,  0x09,  0xf0,
  0x59,  0xf0,  0xb8,  0xf0,  0x48,  0xf4,  0x0e,  0xf7,  0x0a,  0x00,  0x9b,  0x00,  0x9c,  0x00,  0xa4,  0x00,
  0xb5,  0x00,  0xba,  0x00,  0xd0,  0x00,  0xe7,  0x00,  0xf0,  0x03,  0x69,  0x08,  0xe9,  0x09,  0x5c,  0x0c,
  0xb6,  0x12,  0xbc,  0x19,  0xd8,  0x1b,  0x20,  0x1c,  0x34,  0x1c,  0x36,  0x1c,  0x42,  0x1d,  0x08,  0x44,
  0x38,  0x44,  0x91,  0x44,  0x0a,  0x45,  0x48,  0x46,  0x89,  0x48,  0x68,  0x54,  0x83,  0x55,  0x83,  0x59,
  0x31,  0xe4,  0x02,  0xe6,  0x07,  0xf0,  0x08,  0xf0,  0x0b,  0xf0,  0x0c,  0xf0,  0x4b,  0xf4,  0x04,  0xf8,
  0x05,  0xf8,  0x02,  0xfa,  0x03,  0xfa,  0x04,  0xfc,  0x05,  0xfc,  0x07,  0x00,  0xa8,  0x00,  0xaa,  0x00,
  0xb9,  0x00,  0xe0,  0x00,  0xe5,  0x00,  0x22,  0x01,  0x26,  0x01,  0x60,  0x01,  0x7a,  0x01,  0x82,  0x01,
  0xc8,  0x01,  0xca,  0x01,  0x86,  0x02,  0x6a,  0x03,  0x18,  0x05,  0xb2,  0x07,  0x68,  0x08,  0x10,  0x0d,
  0x06,  0x10,  0x0a,  0x10,  0x0e,  0x10,  0x12,  0x10,  0x60,  0x10,  0xed,  0x10,  0xf3,  0x10,  0x06,  0x12,
  0x10,  0x12,  0x1e,  0x12,  0x0c,  0x13,  0x0e,  0x13,  0x10,  0x13,  0xfe,  0x9c,  0xf0,  0x35,  0x05,  0xfe,
  0xec,  0x0e,  0xff,  0x10,  0x00,  0x00,  0xe9,  0xfe,  0x34,  0x1f,  0x00,  0xe8,  0xfe,  0x88,  0x01,  0xff,
  0x03,  0x00,  0x00,  0xfe,  0x93,  0x15,  0xfe,  0x0f,  0x05,  0xff,  0x38,  0x00,  0x00,  0xfe,  0x57,  0x24,
  0x00,  0xfe,  0x4c,  0x00,  0x65,  0xff,  0x04,  0x00,  0x00,  0x1a,  0xff,  0x09,  0x00,  0x00,  0xff,  0x08,
  0x01,  0x01,  0xff,  0x08,  0xff,  0xff,  0xff,  0x27,  0x00,  0x00,  0xff,  0x10,  0xff,  0xff,  0xff,  0x13,
  0x00,  0x00,  0xfe,  0x78,  0x56,  0xfe,  0x34,  0x12,  0xff,  0x21,  0x00,  0x00,  0xfe,  0x04,  0xf7,  0xe8,
  0x37,  0x7d,  0x0d,  0x01,  0xfe,  0x4a,  0x11,  0xfe,  0x04,  0xf7,  0xe8,  0x7d,  0x0d,  0x51,  0x37,  0xfe,
  0x3d,  0xf0,  0xfe,  0x0c,  0x02,  0xfe,  0x20,  0xf0,  0xbc,  0xfe,  0x91,  0xf0,  0xfe,  0xf8,  0x01,  0xfe,
  0x90,  0xf0,  0xfe,  0xf8,  0x01,  0xfe,  0x8f,  0xf0,  0xbc,  0x03,  0x67,  0x4d,  0x05,  0xfe,  0x08,  0x0f,
  0x01,  0xfe,  0x78,  0x0f,  0xfe,  0xdd,  0x12,  0x05,  0xfe,  0x0e,  0x03,  0xfe,  0x28,  0x1c,  0x03,  0xfe,
  0xa6,  0x00,  0xfe,  0xd1,  0x12,  0x3e,  0x22,  0xfe,  0xa6,  0x00,  0xac,  0xfe,  0x48,  0xf0,  0xfe,  0x90,
  0x02,  0xfe,  0x49,  0xf0,  0xfe,  0xaa,  0x02,  0xfe,  0x4a,  0xf0,  0xfe,  0xc8,  0x02,  0xfe,  0x46,  0xf0,
  0xfe,  0x5a,  0x02,  0xfe,  0x47,  0xf0,  0xfe,  0x60,  0x02,  0xfe,  0x43,  0xf0,  0xfe,  0x4e,  0x02,  0xfe,
  0x44,  0xf0,  0xfe,  0x52,  0x02,  0xfe,  0x45,  0xf0,  0xfe,  0x56,  0x02,  0x1c,  0x0d,  0xa2,  0x1c,  0x07,
  0x22,  0xb7,  0x05,  0x35,  0xfe,  0x00,  0x1c,  0xfe,  0xf1,  0x10,  0xfe,  0x02,  0x1c,  0xf5,  0xfe,  0x1e,
  0x1c,  0xfe,  0xe9,  0x10,  0x01,  0x5f,  0xfe,  0xe7,  0x10,  0xfe,  0x06,  0xfc,  0xde,  0x0a,  0x81,  0x01,
  0xa3,  0x05,  0x35,  0x1f,  0x95,  0x47,  0xb8,  0x01,  0xfe,  0xe4,  0x11,  0x0a,  0x81,  0x01,  0x5c,  0xfe,
  0xbd,  0x10,  0x0a,  0x81,  0x01,  0x5c,  0xfe,  0xad,  0x10,  0xfe,  0x16,  0x1c,  0xfe,  0x58,  0x1c,  0x1c,
  0x07,  0x22,  0xb7,  0x37,  0x2a,  0x35,  0xfe,  0x3d,  0xf0,  0xfe,  0x0c,  0x02,  0x2b,  0xfe,  0x9e,  0x02,
  0xfe,  0x5a,  0x1c,  0xfe,  0x12,  0x1c,  0xfe,  0x14,  0x1c,  0x1f,  0xfe,  0x30,  0x00,  0x47,  0xb8,  0x01,
  0xfe,  0xd4,  0x11,  0x1c,  0x07,  0x22,  0xb7,  0x05,  0xe9,  0x21,  0x2c,  0x09,  0x1a,  0x31,  0xfe,  0x69,
  0x10,  0x1c,  0x07,  0x22,  0xb7,  0xfe,  0x04,  0xec,  0x2c,  0x60,  0x01,  0xfe,  0x1e,  0x1e,  0x20,  0x2c,
  0xfe,  0x05,  0xf6,  0xde,  0x01,  0xfe,  0x62,  0x1b,  0x01,  0x0c,  0x61,  0x4a,  0x44,  0x15,  0x56,  0x51,
  0x01,  0xfe,  0x9e,  0x1e,  0x01,  0xfe,  0x96,  0x1a,  0x05,  0x35,  0x0a,  0x57,  0x01,  0x18,  0x09,  0x00,
  0x36,  0x01,  0x85,  0xfe,  0x18,  0x10,  0xfe,  0x41,  0x58,  0x0a,  0xba,  0x01,  0x18,  0xfe,  0xc8,  0x54,
  0x7b,  0xfe,  0x1c,  0x03,  0x01,  0xfe,  0x96,  0x1a,  0x05,  0x35,  0x37,  0x60,  0xfe,  0x02,  0xe8,  0x30,
  0xfe,  0xbf,  0x57,  0xfe,  0x9e,  0x43,  0xfe,  0x77,  0x57,  0xfe,  0x27,  0xf0,  0xfe,  0xe4,  0x01,  0xfe,
  0x07,  0x4b,  0xfe,  0x20,  0xf0,  0xbc,  0xfe,  0x40,  0x1c,  0x2a,  0xeb,  0xfe,  0x26,  0xf0,  0xfe,  0x66,
  0x03,  0xfe,  0xa0,  0xf0,  0xfe,  0x54,  0x03,  0xfe,  0x11,  0xf0,  0xbc,  0xfe,  0xef,  0x10,  0xfe,  0x9f,
  0xf0,  0xfe,  0x74,  0x03,  0xfe,  0x46,  0x1c,  0x19,  0xfe,  0x11,  0x00,  0x05,  0x70,  0x37,  0xfe,  0x48,
  0x1c,  0xfe,  0x46,  0x1c,  0x01,  0x0c,  0x06,  0x28,  0xfe,  0x18,  0x13,  0x26,  0x21,  0xb9,  0xc7,  0x20,
  0xb9,  0x0a,  0x57,  0x01,  0x18,  0xc7,  0x89,  0x01,  0xfe,  0xc8,  0x1a,  0x15,  0xe1,  0x2a,  0xeb,  0xfe,
  0x01,  0xf0,  0xeb,  0xfe,  0x82,  0xf0,  0xfe,  0xa4,  0x03,  0xfe,  0x9c,  0x32,  0x15,  0xfe,  0xe4,  0x00,
  0x2f,  0xfe,  0xb6,  0x03,  0x2a,  0x3c,  0x16,  0xfe,  0xc6,  0x03,  0x01,  0x41,  0xfe,  0x06,  0xf0,  0xfe,
  0xd6,  0x03,  0xaf,  0xa0,  0xfe,  0x0a,  0xf0,  0xfe,  0xa2,  0x07,  0x05,  0x29,  0x03,  0x81,  0x1e,  0x1b,
  0xfe,  0x24,  0x05,  0x1f,  0x63,  0x01,  0x42,  0x8f,  0xfe,  0x70,  0x02,  0x05,  0xea,  0xfe,  0x46,  0x1c,
  0x37,  0x7d,  0x1d,  0xfe,  0x67,  0x1b,  0xfe,  0xbf,  0x57,  0xfe,  0x77,  0x57,  0xfe,  0x48,  0x1c,  0x75,
  0x01,  0xa6,  0x86,  0x0a,  0x57,  0x01,  0x18,  0x09,  0x00,  0x1b,  0xec,  0x0a,  0xe1,  0x01,  0x18,  0x77,
  0x50,  0x40,  0x8d,  0x30,  0x03,  0x81,  0x1e,  0xf8,  0x1f,  0x63,  0x01,  0x42,  0x8f,  0xfe,  0x70,  0x02,
  0x05,  0xea,  0xd7,  0x99,  0xd8,  0x9c,  0x2a,  0x29,  0x2f,  0xfe,  0x4e,  0x04,  0x16,  0xfe,  0x4a,  0x04,
  0x7e,  0xfe,  0xa0,  0x00,  0xfe,  0x9b,  0x57,  0xfe,  0x54,  0x12,  0x32,  0xff,  0x02,  0x00,  0x10,  0x01,
  0x08,  0x16,  0xfe,  0x02,  0x05,  0x32,  0x01,  0x08,  0x16,  0x29,  0x27,  0x25,  0xee,  0xfe,  0x4c,  0x44,
  0xfe,  0x58,  0x12,  0x50,  0xfe,  0x44,  0x48,  0x13,  0x34,  0xfe,  0x4c,  0x54,  0x7b,  0xec,  0x60,  0x8d,
  0x30,  0x01,  0xfe,  0x4e,  0x1e,  0xfe,  0x48,  0x47,  0xfe,  0x7c,  0x13,  0x01,  0x0c,  0x06,  0x28,  0xfe,
  0x32,  0x13,  0x01,  0x43,  0x09,  0x9b,  0xfe,  0x68,  0x13,  0xfe,  0x26,  0x10,  0x13,  0x34,  0xfe,  0x4c,
  0x54,  0x7b,  0xec,  0x01,  0xfe,  0x4e,  0x1e,  0xfe,  0x48,  0x47,  0xfe,  0x54,  0x13,  0x01,  0x0c,  0x06,
  0x28,  0xa5,  0x01,  0x43,  0x09,  0x9b,  0xfe,  0x40,  0x13,  0x01,  0x0c,  0x06,  0x28,  0xf9,  0x1f,  0x7f,
  0x01,  0x0c,  0x06,  0x07,  0x4d,  0x1f,  0xfe,  0x0d,  0x00,  0x01,  0x42,  0x8f,  0xfe,  0xa4,  0x0e,  0x05,
  0x29,  0x32,  0x15,  0xfe,  0xe6,  0x00,  0x0f,  0xfe,  0x1c,  0x90,  0x04,  0xfe,  0x9c,  0x93,  0x3a,  0x0b,
  0x0e,  0x8b,  0x02,  0x1f,  0x7f,  0x01,  0x42,  0x05,  0x35,  0xfe,  0x42,  0x5b,  0x7d,  0x1d,  0xfe,  0x46,
  0x59,  0xfe,  0xbf,  0x57,  0xfe,  0x77,  0x57,  0x0f,  0xfe,  0x87,  0x80,  0x04,  0xfe,  0x87,  0x83,  0xfe,
  0xc9,  0x47,  0x0b,  0x0e,  0xd0,  0x65,  0x01,  0x0c,  0x06,  0x0d,  0xfe,  0x98,  0x13,  0x0f,  0xfe,  0x20,
  0x80,  0x04,  0xfe,  0xa0,  0x83,  0x33,  0x0b,  0x0e,  0x09,  0x1d,  0xfe,  0x84,  0x12,  0x01,  0x38,  0x06,
  0x07,  0xfe,  0x70,  0x13,  0x03,  0xfe,  0xa2,  0x00,  0x1e,  0x1b,  0xfe,  0xda,  0x05,  0xd0,  0x54,  0x01,
  0x38,  0x06,  0x0d,  0xfe,  0x58,  0x13,  0x03,  0xfe,  0xa0,  0x00,  0x1e,  0xfe,  0x50,  0x12,  0x5e,  0xff,
  0x02,  0x00,  0x10,  0x2f,  0xfe,  0x90,  0x05,  0x2a,  0x3c,  0xcc,  0xff,  0x02,  0x00,  0x10,  0x2f,  0xfe,
  0x9e,  0x05,  0x17,  0xfe,  0xf4,  0x05,  0x15,  0xfe,  0xe3,  0x00,  0x26,  0x01,  0x38,  0xfe,  0x4a,  0xf0,
  0xfe,  0xc0,  0x05,  0xfe,  0x49,  0xf0,  0xfe,  0xba,  0x05,  0x71,  0x2e,  0xfe,  0x21,  0x00,  0xf1,  0x2e,
  0xfe,  0x22,  0x00,  0xa2,  0x2e,  0x4a,  0xfe,  0x09,  0x48,  0xff,  0x02,  0x00,  0x10,  0x2f,  0xfe,  0xd0,
  0x05,  0x17,  0xfe,  0xf4,  0x05,  0xfe,  0xe2,  0x08,  0x01,  0x38,  0x06,  0xfe,  0x1c,  0x00,  0x4d,  0x01,
  0xa7,  0x2e,  0x07,  0x20,  0xe4,  0x47,  0xfe,  0x27,  0x01,  0x01,  0x0c,  0x06,  0x28,  0xfe,  0x24,  0x12,
  0x3e,  0x01,  0x84,  0x1f,  0x7f,  0x01,  0x0c,  0x06,  0x07,  0x4d,  0x1f,  0xfe,  0x0d,  0x00,  0x01,  0x42,
  0x8f,  0xfe,  0xa4,  0x0e,  0x05,  0x29,  0x03,  0xe6,  0x1e,  0xfe,  0xca,  0x13,  0x03,  0xb6,  0x1e,  0xfe,
  0x40,  0x12,  0x03,  0x66,  0x1e,  0xfe,  0x38,  0x13,  0x3e,  0x01,  0x84,  0x17,  0xfe,  0x72,  0x06,  0x0a,
  0x07,  0x01,  0x38,  0x06,  0x24,  0xfe,  0x02,  0x12,  0x4f,  0x01,  0xfe,  0x56,  0x19,  0x16,  0xfe,  0x68,
  0x06,  0x15,  0x82,  0x01,  0x41,  0x15,  0xe2,  0x03,  0x66,  0x8a,  0x10,  0x66,  0x03,  0x9a,  0x1e,  0xfe,
  0x70,  0x12,  0x03,  0x55,  0x1e,  0xfe,  0x68,  0x13,  0x01,  0xc6,  0x09,  0x12,  0x48,  0xfe,  0x92,  0x06,
  0x2e,  0x12,  0x01,  0xfe,  0xac,  0x1d,  0xfe,  0x43,  0x48,  0x62,  0x80,  0x13,  0x58,  0xff,  0x02,  0x00,
  0x57,  0x52,  0xad,  0x23,  0x3f,  0x4e,  0x62,  0x49,  0x3e,  0x01,  0x84,  0x17,  0xfe,  0xea,  0x06,  0x01,
  0x38,  0x06,  0x12,  0xf7,  0x45,  0x0a,  0x95,  0x01,  0xfe,  0x84,  0x19,  0x16,  0xfe,  0xe0,  0x06,  0x15,
  0x82,  0x01,  0x41,  0x15,  0xe2,  0x03,  0x55,  0x8a,  0x10,  0x55,  0x1c,  0x07,  0x01,  0x84,  0xfe,  0xae,
  0x10,  0x03,  0x6f,  0x1e,  0xfe,  0x9e,  0x13,  0x3e,  0x01,  0x84,  0x03,  0x9a,  0x1e,  0xfe,  0x1a,  0x12,
  0x01,  0x38,  0x06,  0x12,  0xfc,  0x01,  0xc6,  0x01,  0xfe,  0xac,  0x1d,  0xfe,  0x43,  0x48,  0x62,  0x80,
  0xf0,  0x45,  0x0a,  0x95,  0x03,  0xb6,  0x1e,  0xf8,  0x01,  0x38,  0x06,  0x24,  0x36,  0xfe,  0x02,  0xf6,
  0x07,  0x71,  0x78,  0x8c,  0x00,  0x4d,  0x62,  0x49,  0x3e,  0x2d,  0x93,  0x4e,  0xd0,  0x0d,  0x17,  0xfe,
  0x9a,  0x07,  0x01,  0xfe,  0xc0,  0x19,  0x16,  0xfe,  0x90,  0x07,  0x26,  0x20,  0x9e,  0x15,  0x82,  0x01,
  0x41,  0x15,  0xe2,  0x21,  0x9e,  0x09,  0x07,  0xfb,  0x03,  0xe6,  0xfe,  0x58,  0x57,  0x10,  0xe6,  0x05,
  0xfe,  0x2a,  0x06,  0x03,  0x6f,  0x8a,  0x10,  0x6f,  0x1c,  0x07,  0x01,  0x84,  0xfe,  0x9c,  0x32,  0x5f,
  0x75,  0x01,  0xa6,  0x86,  0x15,  0xfe,  0xe2,  0x00,  0x2f,  0xed,  0x2a,  0x3c,  0xfe,  0x0a,  0xf0,  0xfe,
  0xce,  0x07,  0xae,  0xfe,  0x96,  0x08,  0xfe,  0x06,  0xf0,  0xfe,  0x9e,  0x08,  0xaf,  0xa0,  0x05,  0x29,
  0x01,  0x0c,  0x06,  0x0d,  0xfe,  0x2e,  0x12,  0x14,  0x1d,  0x01,  0x08,  0x14,  0x00,  0x01,  0x08,  0x14,
  0x00,  0x01,  0x08,  0x14,  0x00,  0x01,  0x08,  0xfe,  0x99,  0xa4,  0x01,  0x08,  0x14,  0x00,  0x05,  0xfe,
  0xc6,  0x09,  0x01,  0x76,  0x06,  0x12,  0xfe,  0x3a,  0x12,  0x01,  0x0c,  0x06,  0x12,  0xfe,  0x30,  0x13,
  0x14,  0xfe,  0x1b,  0x00,  0x01,  0x08,  0x14,  0x00,  0x01,  0x08,  0x14,  0x00,  0x01,  0x08,  0x14,  0x00,
  0x01,  0x08,  0x14,  0x07,  0x01,  0x08,  0x14,  0x00,  0x05,  0xef,  0x7c,  0x4a,  0x78,  0x4f,  0x0f,  0xfe,
  0x9a,  0x81,  0x04,  0xfe,  0x9a,  0x83,  0xfe,  0xcb,  0x47,  0x0b,  0x0e,  0x2d,  0x28,  0x48,  0xfe,  0x6c,
  0x08,  0x0a,  0x28,  0xfe,  0x09,  0x6f,  0xca,  0xfe,  0xca,  0x45,  0xfe,  0x32,  0x12,  0x53,  0x63,  0x4e,
  0x7c,  0x97,  0x2f,  0xfe,  0x7e,  0x08,  0x2a,  0x3c,  0xfe,  0x0a,  0xf0,  0xfe,  0x6c,  0x08,  0xaf,  0xa0,
  0xae,  0xfe,  0x96,  0x08,  0x05,  0x29,  0x01,  0x41,  0x05,  0xed,  0x14,  0x24,  0x05,  0xed,  0xfe,  0x9c,
  0xf7,  0x9f,  0x01,  0xfe,  0xae,  0x1e,  0xfe,  0x18,  0x58,  0x01,  0xfe,  0xbe,  0x1e,  0xfe,  0x99,  0x58,
  0xfe,  0x78,  0x18,  0xfe,  0xf9,  0x18,  0x8e,  0xfe,  0x16,  0x09,  0x10,  0x6a,  0x22,  0x6b,  0x01,  0x0c,
  0x61,  0x54,  0x44,  0x21,  0x2c,  0x09,  0x1a,  0xf8,  0x77,  0x01,  0xfe,  0x7e,  0x1e,  0x47,  0x2c,  0x7a,
  0x30,  0xf0,  0xfe,  0x83,  0xe7,  0xfe,  0x3f,  0x00,  0x71,  0xfe,  0x03,  0x40,  0x01,  0x0c,  0x61,  0x65,
  0x44,  0x01,  0xc2,  0xc8,  0xfe,  0x1f,  0x40,  0x20,  0x6e,  0x01,  0xfe,  0x6a,  0x16,  0xfe,  0x08,  0x50,
  0xfe,  0x8a,  0x50,  0xfe,  0x44,  0x51,  0xfe,  0xc6,  0x51,  0xfe,  0x10,  0x10,  0x01,  0xfe,  0xce,  0x1e,
  0x01,  0xfe,  0xde,  0x1e,  0x10,  0x68,  0x22,  0x69,  0x01,  0xfe,  0xee,  0x1e,  0x01,  0xfe,  0xfe,  0x1e,
  0xfe,  0x40,  0x50,  0xfe,  0xc2,  0x50,  0x10,  0x4b,  0x22,  0x4c,  0xfe,  0x8a,  0x10,  0x01,  0x0c,  0x06,
  0x54,  0xfe,  0x50,  0x12,  0x01,  0xfe,  0xae,  0x1e,  0x01,  0xfe,  0xbe,  0x1e,  0x10,  0x6a,  0x22,  0x6b,
  0x01,  0x0c,  0x06,  0x65,  0x4e,  0x01,  0xc2,  0x0f,  0xfe,  0x1f,  0x80,  0x04,  0xfe,  0x9f,  0x83,  0x33,
  0x0b,  0x0e,  0x20,  0x6e,  0x0f,  0xfe,  0x44,  0x90,  0x04,  0xfe,  0xc4,  0x93,  0x3a,  0x0b,  0xfe,  0xc6,
  0x90,  0x04,  0xfe,  0xc6,  0x93,  0x79,  0x0b,  0x0e,  0x10,  0x6c,  0x22,  0x6d,  0x01,  0xfe,  0xce,  0x1e,
  0x01,  0xfe,  0xde,  0x1e,  0x10,  0x68,  0x22,  0x69,  0x0f,  0xfe,  0x40,  0x90,  0x04,  0xfe,  0xc0,  0x93,
  0x3a,  0x0b,  0xfe,  0xc2,  0x90,  0x04,  0xfe,  0xc2,  0x93,  0x79,  0x0b,  0x0e,  0x10,  0x4b,  0x22,  0x4c,
  0x10,  0x64,  0x22,  0x34,  0x01,  0x0c,  0x61,  0x24,  0x44,  0x37,  0x13,  0xfe,  0x4e,  0x11,  0x2f,  0xfe,
  0xde,  0x09,  0xfe,  0x9e,  0xf0,  0xfe,  0xf2,  0x09,  0xfe,  0x01,  0x48,  0x1b,  0x3c,  0x37,  0x88,  0xf5,
  0xd4,  0xfe,  0x1e,  0x0a,  0xd5,  0xfe,  0x42,  0x0a,  0xd2,  0xfe,  0x1e,  0x0a,  0xd3,  0xfe,  0x42,  0x0a,
  0xae,  0xfe,  0x12,  0x0a,  0xfe,  0x06,  0xf0,  0xfe,  0x18,  0x0a,  0xaf,  0xa0,  0x05,  0x29,  0x01,  0x41,
  0xfe,  0xc1,  0x10,  0x14,  0x24,  0xfe,  0xc1,  0x10,  0x01,  0x76,  0x06,  0x07,  0xfe,  0x14,  0x12,  0x01,
  0x76,  0x06,  0x0d,  0x5d,  0x01,  0x0c,  0x06,  0x0d,  0xfe,  0x74,  0x12,  0xfe,  0x2e,  0x1c,  0x05,  0xfe,
  0x1a,  0x0c,  0x01,  0x76,  0x06,  0x07,  0x5d,  0x01,  0x76,  0x06,  0x0d,  0x41,  0xfe,  0x2c,  0x1c,  0xfe,
  0xaa,  0xf0,  0xfe,  0xce,  0x0a,  0xfe,  0xac,  0xf0,  0xfe,  0x66,  0x0a,  0xfe,  0x92,  0x10,  0xc4,  0xf6,
  0xfe,  0xad,  0xf0,  0xfe,  0x72,  0x0a,  0x05,  0xfe,  0x1a,  0x0c,  0xc5,  0xfe,  0xe7,  0x10,  0xfe,  0x2b,
  0xf0,  0xbf,  0xfe,  0x6b,  0x18,  0x23,  0xfe,  0x00,  0xfe,  0xfe,  0x1c,  0x12,  0xac,  0xfe,  0xd2,  0xf0,
  0xbf,  0xfe,  0x76,  0x18,  0x23,  0x1d,  0x1b,  0xbf,  0x03,  0xe3,  0x23,  0x07,  0x1b,  0xbf,  0xd4,  0x5b,
  0xd5,  0x5b,  0xd2,  0x5b,  0xd3,  0x5b,  0xc4,  0xc5,  0xfe,  0xa9,  0x10,  0x75,  0x5e,  0x32,  0x1f,  0x7f,
  0x01,  0x42,  0x19,  0xfe,  0x35,  0x00,  0xfe,  0x01,  0xf0,  0x70,  0x19,  0x98,  0x05,  0x70,  0xfe,  0x74,
  0x18,  0x23,  0xfe,  0x00,  0xf8,  0x1b,  0x5b,  0x7d,  0x12,  0x01,  0xfe,  0x78,  0x0f,  0x4d,  0x01,  0xfe,
  0x96,  0x1a,  0x21,  0x30,  0x77,  0x7d,  0x1d,  0x05,  0x5b,  0x01,  0x0c,  0x06,  0x0d,  0x2b,  0xfe,  0xe2,
  0x0b,  0x01,  0x0c,  0x06,  0x54,  0xfe,  0xa6,  0x12,  0x01,  0x0c,  0x06,  0x24,  0xfe,  0x88,  0x13,  0x21,
  0x6e,  0xc7,  0x01,  0xfe,  0x1e,  0x1f,  0x0f,  0xfe,  0x83,  0x80,  0x04,  0xfe,  0x83,  0x83,  0xfe,  0xc9,
  0x47,  0x0b,  0x0e,  0xfe,  0xc8,  0x44,  0xfe,  0x42,  0x13,  0x0f,  0xfe,  0x04,  0x91,  0x04,  0xfe,  0x84,
  0x93,  0xfe,  0xca,  0x57,  0x0b,  0xfe,  0x86,  0x91,  0x04,  0xfe,  0x86,  0x93,  0xfe,  0xcb,  0x57,  0x0b,
  0x0e,  0x7a,  0x30,  0xfe,  0x40,  0x59,  0xfe,  0xc1,  0x59,  0x8e,  0x40,  0x03,  0x6a,  0x3b,  0x6b,  0x10,
  0x97,  0x22,  0x98,  0xd9,  0x6a,  0xda,  0x6b,  0x01,  0xc2,  0xc8,  0x7a,  0x30,  0x20,  0x6e,  0xdb,  0x64,
  0xdc,  0x34,  0x91,  0x6c,  0x7e,  0x6d,  0xfe,  0x44,  0x55,  0xfe,  0xe5,  0x55,  0xfe,  0x04,  0xfa,  0x64,
  0xfe,  0x05,  0xfa,  0x34,  0x01,  0xfe,  0x6a,  0x16,  0xa3,  0x26,  0x10,  0x97,  0x10,  0x98,  0x91,  0x6c,
  0x7e,  0x6d,  0xfe,  0x14,  0x10,  0x01,  0x0c,  0x06,  0x24,  0x1b,  0x40,  0x91,  0x4b,  0x7e,  0x4c,  0x01,
  0x0c,  0x06,  0xfe,  0xf7,  0x00,  0x44,  0x03,  0x68,  0x3b,  0x69,  0xfe,  0x10,  0x58,  0xfe,  0x91,  0x58,
  0xfe,  0x14,  0x59,  0xfe,  0x95,  0x59,  0x05,  0x5b,  0x01,  0x0c,  0x06,  0x24,  0x1b,  0x40,  0x01,  0x0c,
  0x06,  0xfe,  0xf7,  0x00,  0x44,  0x78,  0x01,  0xfe,  0x8e,  0x1e,  0x4f,  0x0f,  0xfe,  0x10,  0x90,  0x04,
  0xfe,  0x90,  0x93,  0x3a,  0x0b,  0xfe,  0x92,  0x90,  0x04,  0xfe,  0x92,  0x93,  0x79,  0x0b,  0x0e,  0xfe,
  0xbd,  0x10,  0x01,  0x43,  0x09,  0xbb,  0x1b,  0xfe,  0x6e,  0x0a,  0x15,  0xbb,  0x01,  0x0c,  0x06,  0x0d,
  0xfe,  0x14,  0x13,  0x03,  0x4b,  0x3b,  0x4c,  0x8e,  0xfe,  0x6e,  0x0a,  0xfe,  0x0c,  0x58,  0xfe,  0x8d,
  0x58,  0x05,  0x5b,  0x26,  0x3e,  0x0f,  0xfe,  0x19,  0x80,  0x04,  0xfe,  0x99,  0x83,  0x33,  0x0b,  0x0e,
  0xfe,  0xe5,  0x10,  0x01,  0x0c,  0x06,  0x0d,  0xfe,  0x1a,  0x12,  0xfe,  0x6c,  0x19,  0xfe,  0x19,  0x41,
  0xfe,  0x6b,  0x18,  0xac,  0xfe,  0xd1,  0xf0,  0xef,  0x1f,  0x92,  0x01,  0x42,  0x19,  0xfe,  0x44,  0x00,
  0xfe,  0x90,  0x10,  0xfe,  0x6c,  0x19,  0xd9,  0x4b,  0xfe,  0xed,  0x19,  0xda,  0x4c,  0xfe,  0x0c,  0x51,
  0xfe,  0x8e,  0x51,  0xfe,  0x6b,  0x18,  0x23,  0xfe,  0x00,  0xff,  0x31,  0xfe,  0x76,  0x10,  0xac,  0xfe,
  0xd2,  0xf0,  0xfe,  0xba,  0x0c,  0xfe,  0x76,  0x18,  0x23,  0x1d,  0x5d,  0x03,  0xe3,  0x23,  0x07,  0xfe,
  0x08,  0x13,  0x19,  0xfe,  0x16,  0x00,  0x05,  0x70,  0xfe,  0xd1,  0xf0,  0xfe,  0xcc,  0x0c,  0x1f,  0x92,
  0x01,  0x42,  0x19,  0xfe,  0x17,  0x00,  0x5c,  0xfe,  0xce,  0xf0,  0xfe,  0xd2,  0x0c,  0xfe,  0x3e,  0x10,
  0xfe,  0xcd,  0xf0,  0xfe,  0xde,  0x0c,  0x19,  0xfe,  0x22,  0x00,  0x05,  0x70,  0xfe,  0xcb,  0xf0,  0xfe,
  0xea,  0x0c,  0x19,  0xfe,  0x24,  0x00,  0x05,  0x70,  0xfe,  0xd0,  0xf0,  0xfe,  0xf4,  0x0c,  0x19,  0x94,
  0xfe,  0x1c,  0x10,  0xfe,  0xcf,  0xf0,  0xfe,  0xfe,  0x0c,  0x19,  0x4a,  0xf3,  0xfe,  0xcc,  0xf0,  0xef,
  0x01,  0x76,  0x06,  0x24,  0x4d,  0x19,  0xfe,  0x12,  0x00,  0x37,  0x13,  0xfe,  0x4e,  0x11,  0x2f,  0xfe,
  0x16,  0x0d,  0xfe,  0x9e,  0xf0,  0xfe,  0x2a,  0x0d,  0xfe,  0x01,  0x48,  0x1b,  0x3c,  0x37,  0x88,  0xf5,
  0xd4,  0x29,  0xd5,  0x29,  0xd2,  0x29,  0xd3,  0x29,  0x37,  0xfe,  0x9c,  0x32,  0x2f,  0xfe,  0x3e,  0x0d,
  0x2a,  0x3c,  0xae,  0xfe,  0x62,  0x0d,  0xaf,  0xa0,  0xd4,  0x9f,  0xd5,  0x9f,  0xd2,  0x9f,  0xd3,  0x9f,
  0x05,  0x29,  0x01,  0x41,  0xfe,  0xd3,  0x10,  0x15,  0xfe,  0xe8,  0x00,  0xc4,  0xc5,  0x75,  0xd7,  0x99,
  0xd8,  0x9c,  0xfe,  0x89,  0xf0,  0x29,  0x27,  0x25,  0xbe,  0xd7,  0x99,  0xd8,  0x9c,  0x2f,  0xfe,  0x8c,
  0x0d,  0x16,  0x29,  0x27,  0x25,  0xbd,  0xfe,  0x01,  0x48,  0xa4,  0x19,  0xfe,  0x42,  0x00,  0x05,  0x70,
  0x90,  0x07,  0xfe,  0x81,  0x49,  0x1b,  0xfe,  0x64,  0x0e,  0x01,  0x0c,  0x06,  0x0d,  0xfe,  0x44,  0x13,
  0x19,  0x00,  0x2d,  0x0d,  0xfe,  0x54,  0x12,  0x2d,  0xfe,  0x28,  0x00,  0x2b,  0xfe,  0xda,  0x0e,  0x0a,
  0x57,  0x01,  0x18,  0x09,  0x00,  0x36,  0x46,  0xfe,  0x28,  0x00,  0xfe,  0xfa,  0x10,  0x01,  0xfe,  0xf4,
  0x1c,  0x01,  0xfe,  0x00,  0x1d,  0x0a,  0xba,  0x01,  0xfe,  0x58,  0x10,  0x40,  0x15,  0x56,  0x01,  0x85,
  0x05,  0x35,  0x19,  0xfe,  0x44,  0x00,  0x2d,  0x0d,  0xf7,  0x46,  0x0d,  0xfe,  0xcc,  0x10,  0x01,  0xa7,
  0x46,  0x0d,  0xfe,  0xc2,  0x10,  0x01,  0xa7,  0x0f,  0xfe,  0x19,  0x82,  0x04,  0xfe,  0x99,  0x83,  0xfe,
  0xcc,  0x47,  0x0b,  0x0e,  0xfe,  0x34,  0x46,  0xa5,  0x46,  0x0d,  0x19,  0xfe,  0x43,  0x00,  0xfe,  0xa2,
  0x10,  0x01,  0x0c,  0x61,  0x0d,  0x44,  0x01,  0xfe,  0xf4,  0x1c,  0x01,  0xfe,  0x00,  0x1d,  0x40,  0x15,
  0x56,  0x01,  0x85,  0x7d,  0x0d,  0x40,  0x51,  0x01,  0xfe,  0x9e,  0x1e,  0x05,  0xfe,  0x3a,  0x03,  0x01,
  0x0c,  0x06,  0x0d,  0x5d,  0x46,  0x0d,  0x19,  0x00,  0xfe,  0x62,  0x10,  0x01,  0x76,  0x06,  0x12,  0xfe,
  0x5c,  0x12,  0x01,  0x0c,  0x06,  0x12,  0xfe,  0x52,  0x13,  0xfe,  0x1c,  0x1c,  0xfe,  0x9d,  0xf0,  0xfe,
  0x8e,  0x0e,  0xfe,  0x1c,  0x1c,  0xfe,  0x9d,  0xf0,  0xfe,  0x94,  0x0e,  0x01,  0x0c,  0x61,  0x12,  0x44,
  0xfe,  0x9f,  0x10,  0x19,  0xfe,  0x15,  0x00,  0xfe,  0x04,  0xe6,  0x0d,  0x4f,  0xfe,  0x2e,  0x10,  0x19,
  0xfe,  0x13,  0x00,  0xfe,  0x10,  0x10,  0x19,  0xfe,  0x47,  0x00,  0xf1,  0x19,  0xfe,  0x41,  0x00,  0xa2,
  0x19,  0xfe,  0x24,  0x00,  0x86,  0xc4,  0xc5,  0x75,  0x03,  0x81,  0x1e,  0x2b,  0xea,  0x4f,  0xfe,  0x04,
  0xe6,  0x12,  0xfe,  0x9d,  0x41,  0xfe,  0x1c,  0x42,  0x40,  0x01,  0xf4,  0x05,  0x35,  0xfe,  0x12,  0x1c,
  0x1f,  0x0d,  0x47,  0xb5,  0xc3,  0x1f,  0xfe,  0x31,  0x00,  0x47,  0xb8,  0x01,  0xfe,  0xd4,  0x11,  0x05,
  0xe9,  0x51,  0xfe,  0x06,  0xec,  0xe0,  0xfe,  0x0e,  0x47,  0x46,  0x28,  0xfe,  0xce,  0x45,  0x31,  0x51,
  0xfe,  0x06,  0xea,  0xe0,  0xfe,  0x47,  0x4b,  0x45,  0xfe,  0x75,  0x57,  0x03,  0x67,  0xfe,  0x98,  0x56,
  0xfe,  0x38,  0x12,  0x0a,  0x5a,  0x01,  0x18,  0xfe,  0x44,  0x48,  0x60,  0x01,  0x0c,  0x06,  0x28,  0xfe,
  0x18,  0x13,  0x0a,  0x57,  0x01,  0x18,  0x3e,  0xfe,  0x41,  0x58,  0x0a,  0xba,  0xfe,  0xfa,  0x14,  0xfe,
  0x49,  0x54,  0xb0,  0xfe,  0x5e,  0x0f,  0x05,  0xfe,  0x3a,  0x03,  0x0a,  0x67,  0xfe,  0xe0,  0x14,  0xfe,
  0x0e,  0x47,  0x46,  0x28,  0xfe,  0xce,  0x45,  0x31,  0x51,  0xfe,  0xce,  0x47,  0xfe,  0xad,  0x13,  0x05,
  0x35,  0x21,  0x2c,  0x09,  0x1a,  0xfe,  0x98,  0x12,  0x26,  0x20,  0x96,  0x20,  0xe7,  0xfe,  0x08,  0x1c,
  0xfe,  0x7c,  0x19,  0xfe,  0xfd,  0x19,  0xfe,  0x0a,  0x1c,  0x03,  0xe5,  0xfe,  0x48,  0x55,  0xa5,  0x3b,
  0xfe,  0x62,  0x01,  0xfe,  0xc9,  0x55,  0x31,  0xfe,  0x74,  0x10,  0x01,  0xfe,  0xf0,  0x1a,  0x03,  0xfe,
  0x38,  0x01,  0x3b,  0xfe,  0x3a,  0x01,  0x8e,  0xfe,  0x1e,  0x10,  0xfe,  0x02,  0xec,  0xe7,  0x53,  0x00,
  0x36,  0xfe,  0x04,  0xec,  0x2c,  0x60,  0xfe,  0x05,  0xf6,  0xfe,  0x34,  0x01,  0x01,  0xfe,  0x62,  0x1b,
  0x01,  0xfe,  0xce,  0x1e,  0xb2,  0x11,  0xfe,  0x18,  0x13,  0xca,  0xfe,  0x02,  0xea,  0xe7,  0x53,  0x92,
  0xfe,  0xc3,  0x13,  0x1f,  0x12,  0x47,  0xb5,  0xc3,  0xfe,  0x2a,  0x10,  0x03,  0xfe,  0x38,  0x01,  0x23,
  0xfe,  0xf0,  0xff,  0x10,  0xe5,  0x03,  0xfe,  0x3a,  0x01,  0x10,  0xfe,  0x62,  0x01,  0x01,  0xfe,  0x1e,
  0x1e,  0x20,  0x2c,  0x15,  0x56,  0x01,  0xfe,  0x9e,  0x1e,  0x13,  0x07,  0x02,  0x26,  0x02,  0x21,  0x96,
  0xc7,  0x20,  0x96,  0x09,  0x92,  0xfe,  0x79,  0x13,  0x1f,  0x1d,  0x47,  0xb5,  0xc3,  0xfe,  0xe1,  0x10,
  0xcf,  0xfe,  0x03,  0xdc,  0xfe,  0x73,  0x57,  0xfe,  0x80,  0x5d,  0x02,  0xcf,  0xfe,  0x03,  0xdc,  0xfe,
  0x5b,  0x57,  0xfe,  0x80,  0x5d,  0x02,  0xfe,  0x03,  0x57,  0xcf,  0x26,  0xfe,  0x00,  0xcc,  0x02,  0xfe,
  0x03,  0x57,  0xcf,  0x89,  0x02,  0x01,  0x0c,  0x06,  0x4a,  0xfe,  0x4e,  0x13,  0x0f,  0xfe,  0x1c,  0x80,
  0x04,  0xfe,  0x9c,  0x83,  0x33,  0x0b,  0x0e,  0x09,  0x07,  0xfe,  0x3a,  0x13,  0x0f,  0xfe,  0x1e,  0x80,
  0x04,  0xfe,  0x9e,  0x83,  0x33,  0x0b,  0x0e,  0xfe,  0x2a,  0x13,  0x0f,  0xfe,  0x1d,  0x80,  0x04,  0xfe,
  0x9d,  0x83,  0xfe,  0xf9,  0x13,  0x0e,  0xfe,  0x1c,  0x13,  0x01,  0xfe,  0xee,  0x1e,  0xac,  0xfe,  0x14,
  0x13,  0x01,  0xfe,  0xfe,  0x1e,  0xfe,  0x81,  0x58,  0xfa,  0x01,  0xfe,  0x0e,  0x1f,  0xfe,  0x30,  0xf4,
  0x0d,  0xfe,  0x3c,  0x50,  0xa2,  0x01,  0xfe,  0x92,  0x1b,  0x01,  0x43,  0x09,  0x56,  0xfb,  0x01,  0xfe,
  0xc8,  0x1a,  0x01,  0x0c,  0x06,  0x28,  0xa4,  0x01,  0xfe,  0xf4,  0x1c,  0x01,  0xfe,  0x00,  0x1d,  0x15,
  0xfe,  0xe9,  0x00,  0x01,  0x0c,  0x06,  0x4a,  0xfe,  0x4e,  0x13,  0x01,  0xfe,  0x22,  0x1b,  0xfe,  0x1e,
  0x1c,  0x0f,  0xfe,  0x14,  0x90,  0x04,  0xfe,  0x94,  0x93,  0x3a,  0x0b,  0xfe,  0x96,  0x90,  0x04,  0xfe,
  0x96,  0x93,  0x79,  0x0b,  0x0e,  0x10,  0xfe,  0x64,  0x01,  0x22,  0xfe,  0x66,  0x01,  0x01,  0x0c,  0x06,
  0x65,  0xf9,  0x0f,  0xfe,  0x03,  0x80,  0x04,  0xfe,  0x83,  0x83,  0x33,  0x0b,  0x0e,  0x77,  0xfe,  0x01,
  0xec,  0x2c,  0xfe,  0x80,  0x40,  0x20,  0x2c,  0x7a,  0x30,  0x15,  0xdf,  0x40,  0x21,  0x2c,  0xfe,  0x00,
  0x40,  0x8d,  0x2c,  0x02,  0xfe,  0x08,  0x1c,  0x03,  0xfe,  0xac,  0x00,  0xfe,  0x06,  0x58,  0x03,  0xfe,
  0xae,  0x00,  0xfe,  0x07,  0x58,  0x03,  0xfe,  0xb0,  0x00,  0xfe,  0x08,  0x58,  0x03,  0xfe,  0xb2,  0x00,
  0xfe,  0x09,  0x58,  0xfe,  0x0a,  0x1c,  0x2e,  0x49,  0x20,  0xe0,  0x26,  0x10,  0x66,  0x10,  0x55,  0x10,
  0x6f,  0x13,  0x57,  0x52,  0x4f,  0x1c,  0x28,  0xfe,  0x90,  0x4d,  0xfe,  0x91,  0x54,  0x2b,  0xfe,  0x88,
  0x11,  0x46,  0x1a,  0x13,  0x5a,  0x52,  0x1c,  0x4a,  0xfe,  0x90,  0x4d,  0xfe,  0x91,  0x54,  0x2b,  0xfe,
  0x9e,  0x11,  0x2e,  0x1a,  0x20,  0x2c,  0x90,  0x34,  0x60,  0x21,  0x2c,  0xfe,  0x00,  0x40,  0x8d,  0x2c,
  0x15,  0xdf,  0xfe,  0x14,  0x56,  0xfe,  0xd6,  0xf0,  0xfe,  0xb2,  0x11,  0xfe,  0x12,  0x1c,  0x75,  0xfe,
  0x14,  0x1c,  0xfe,  0x10,  0x1c,  0xfe,  0x18,  0x1c,  0x02,  0x51,  0xfe,  0x0c,  0x14,  0xfe,  0x0e,  0x47,
  0xfe,  0x07,  0xe6,  0x28,  0xfe,  0xce,  0x47,  0xfe,  0xf5,  0x13,  0x02,  0x01,  0xa7,  0x90,  0x34,  0x60,
  0xfe,  0x06,  0x80,  0xfe,  0x48,  0x47,  0xfe,  0x42,  0x13,  0xfe,  0x02,  0x80,  0x09,  0x56,  0xfe,  0x34,
  0x13,  0x0a,  0x5a,  0x01,  0x18,  0xcb,  0xfe,  0x36,  0x12,  0xfe,  0x41,  0x48,  0xfe,  0x45,  0x48,  0x01,
  0xfe,  0xb2,  0x16,  0xfe,  0x00,  0xcc,  0xcb,  0xfe,  0xf3,  0x13,  0x3f,  0x89,  0x09,  0x1a,  0xa5,  0x0a,
  0x9d,  0x01,  0x18,  0xfe,  0x80,  0x5c,  0x01,  0x85,  0xf2,  0x09,  0x9b,  0xa4,  0xfe,  0x14,  0x56,  0xfe,
  0xd6,  0xf0,  0xfe,  0xec,  0x11,  0x02,  0xfe,  0x44,  0x58,  0x77,  0xfe,  0x01,  0xec,  0xb8,  0xfe,  0x9e,
  0x40,  0xfe,  0x9d,  0xe7,  0x00,  0xfe,  0x9c,  0xe7,  0x12,  0x8d,  0x30,  0x01,  0xf4,  0xfe,  0xdd,  0x10,
  0x37,  0xd7,  0x99,  0xd8,  0x9c,  0x27,  0x25,  0xee,  0x09,  0x12,  0xfe,  0x48,  0x12,  0x09,  0x0d,  0xfe,
  0x56,  0x12,  0x09,  0x1d,  0xfe,  0x30,  0x12,  0x09,  0xdd,  0x1b,  0xfe,  0xc4,  0x13,  0x09,  0xfe,  0x23,
  0x00,  0x1b,  0xfe,  0xd0,  0x13,  0x09,  0x07,  0x1b,  0xfe,  0x34,  0x14,  0x09,  0x24,  0xfe,  0x12,  0x12,
  0x09,  0x00,  0x1b,  0x29,  0x1f,  0xdd,  0x01,  0x42,  0xa1,  0x32,  0x01,  0x08,  0xae,  0x41,  0x02,  0x32,
  0xfe,  0x62,  0x08,  0x0a,  0xe1,  0x01,  0xfe,  0x58,  0x10,  0x15,  0x9b,  0x05,  0x35,  0x32,  0x01,  0x43,
  0x09,  0xbb,  0xfe,  0xd7,  0x13,  0x91,  0x4b,  0x7e,  0x4c,  0x8e,  0xfe,  0x80,  0x13,  0x01,  0x0c,  0x06,
  0x54,  0xfe,  0x72,  0x12,  0xdb,  0x64,  0xdc,  0x34,  0xfe,  0x44,  0x55,  0xfe,  0xe5,  0x55,  0xb0,  0xfe,
  0x4a,  0x13,  0x21,  0x6e,  0xfe,  0x26,  0x13,  0x03,  0x97,  0x3b,  0x98,  0x8e,  0xfe,  0xb6,  0x0e,  0x10,
  0x6a,  0x22,  0x6b,  0x26,  0x10,  0x97,  0x10,  0x98,  0x01,  0xc2,  0x2e,  0x49,  0x88,  0x20,  0x6e,  0x01,
  0xfe,  0x6a,  0x16,  0xdb,  0x64,  0xdc,  0x34,  0xfe,  0x04,  0x55,  0xfe,  0xa5,  0x55,  0xfe,  0x04,  0xfa,
  0x64,  0xfe,  0x05,  0xfa,  0x34,  0xfe,  0x8f,  0x10,  0x03,  0x6c,  0x3b,  0x6d,  0xfe,  0x40,  0x56,  0xfe,
  0xe1,  0x56,  0x10,  0x6c,  0x22,  0x6d,  0x71,  0xdb,  0x64,  0xdc,  0x34,  0xfe,  0x44,  0x55,  0xfe,  0xe5,
  0x55,  0x03,  0x68,  0x3b,  0x69,  0xfe,  0x00,  0x56,  0xfe,  0xa1,  0x56,  0x10,  0x68,  0x22,  0x69,  0x01,
  0x0c,  0x06,  0x54,  0xf9,  0x21,  0x6e,  0xfe,  0x1f,  0x40,  0x03,  0x6a,  0x3b,  0x6b,  0xfe,  0x2c,  0x50,
  0xfe,  0xae,  0x50,  0x03,  0x6c,  0x3b,  0x6d,  0xfe,  0x44,  0x50,  0xfe,  0xc6,  0x50,  0x03,  0x68,  0x3b,
  0x69,  0xfe,  0x08,  0x50,  0xfe,  0x8a,  0x50,  0x03,  0x4b,  0x3b,  0x4c,  0xfe,  0x40,  0x50,  0xfe,  0xc2,
  0x50,  0x05,  0x73,  0x2e,  0x07,  0x20,  0x9e,  0x05,  0x72,  0x32,  0x01,  0x08,  0x16,  0x3d,  0x27,  0x25,
  0xee,  0x09,  0x07,  0x2b,  0x3d,  0x01,  0x43,  0x09,  0xbb,  0x2b,  0x72,  0x01,  0xa6,  0x23,  0x3f,  0x1b,
  0x3d,  0x01,  0x0c,  0x06,  0x0d,  0xfe,  0x1e,  0x13,  0x91,  0x4b,  0x7e,  0x4c,  0xfe,  0x0a,  0x55,  0x31,
  0xfe,  0x8b,  0x55,  0xd9,  0x4b,  0xda,  0x4c,  0xfe,  0x0c,  0x51,  0xfe,  0x8e,  0x51,  0x05,  0x72,  0x01,
  0xfe,  0x8e,  0x1e,  0xca,  0xfe,  0x19,  0x41,  0x05,  0x72,  0x32,  0x01,  0x08,  0x2a,  0x3c,  0x16,  0xc0,
  0x27,  0x25,  0xbe,  0x2d,  0x1d,  0xc0,  0x2d,  0x0d,  0x83,  0x2d,  0x7f,  0x1b,  0xfe,  0x66,  0x15,  0x05,
  0x3d,  0x01,  0x08,  0x2a,  0x3c,  0x16,  0xc0,  0x27,  0x25,  0xbd,  0x09,  0x1d,  0x2b,  0x3d,  0x01,  0x08,
  0x16,  0xc0,  0x27,  0x25,  0xfe,  0xe8,  0x09,  0xfe,  0xc2,  0x49,  0x50,  0x03,  0xb6,  0x1e,  0x83,  0x01,
  0x38,  0x06,  0x24,  0x31,  0xa1,  0xfe,  0xbb,  0x45,  0x2d,  0x00,  0xa4,  0x46,  0x07,  0x90,  0x3f,  0x01,
  0xfe,  0xf8,  0x15,  0x01,  0xa6,  0x86,  0xfe,  0x4b,  0x45,  0xfe,  0x20,  0x13,  0x01,  0x43,  0x09,  0x82,
  0xfe,  0x16,  0x13,  0x03,  0x9a,  0x1e,  0x5d,  0x03,  0x55,  0x1e,  0x31,  0x5e,  0x05,  0x72,  0xfe,  0xc0,
  0x5d,  0x01,  0xa7,  0xfe,  0x03,  0x17,  0x03,  0x66,  0x8a,  0x10,  0x66,  0x5e,  0x32,  0x01,  0x08,  0x17,
  0x73,  0x01,  0xfe,  0x56,  0x19,  0x05,  0x73,  0x01,  0x08,  0x2a,  0x3c,  0x16,  0x3d,  0x27,  0x25,  0xbd,
  0x09,  0x07,  0x2b,  0x3d,  0x01,  0xfe,  0xbe,  0x16,  0xfe,  0x42,  0x58,  0xfe,  0xe8,  0x14,  0x01,  0xa6,
  0x86,  0xfe,  0x4a,  0xf4,  0x0d,  0x1b,  0x3d,  0xfe,  0x4a,  0xf4,  0x07,  0xfe,  0x0e,  0x12,  0x01,  0x43,
  0x09,  0x82,  0x4e,  0x05,  0x72,  0x03,  0x55,  0x8a,  0x10,  0x55,  0x5e,  0x32,  0x01,  0x08,  0x17,  0x73,
  0x01,  0xfe,  0x84,  0x19,  0x05,  0x73,  0x01,  0x08,  0x2a,  0x3c,  0x16,  0x3d,  0x27,  0x25,  0xbd,  0x09,
  0x12,  0x2b,  0x3d,  0x01,  0xfe,  0xe8,  0x17,  0x8b,  0xfe,  0xaa,  0x14,  0xfe,  0xb6,  0x14,  0x86,  0xa8,
  0xb2,  0x0d,  0x1b,  0x3d,  0xb2,  0x07,  0xfe,  0x0e,  0x12,  0x01,  0x43,  0x09,  0x82,  0x4e,  0x05,  0x72,
  0x03,  0x6f,  0x8a,  0x10,  0x6f,  0x5e,  0x32,  0x01,  0x08,  0x17,  0x73,  0x01,  0xfe,  0xc0,  0x19,  0x05,
  0x73,  0x13,  0x07,  0x2f,  0xfe,  0xcc,  0x15,  0x17,  0xfe,  0xe2,  0x15,  0x5f,  0xcc,  0x01,  0x08,  0x26,
  0x5f,  0x02,  0x8f,  0xfe,  0xde,  0x15,  0x2a,  0xfe,  0xde,  0x15,  0x16,  0xfe,  0xcc,  0x15,  0x5e,  0x32,
  0x01,  0x08,  0xfe,  0xd5,  0x10,  0x13,  0x58,  0xff,  0x02,  0x00,  0x57,  0x52,  0xad,  0x23,  0xfe,  0xff,
  0x7f,  0xfe,  0x30,  0x56,  0xfe,  0x00,  0x5c,  0x02,  0x13,  0x58,  0xff,  0x02,  0x00,  0x57,  0x52,  0xad,
  0x23,  0x3f,  0xfe,  0x30,  0x56,  0xfe,  0x00,  0x5c,  0x02,  0x13,  0x58,  0xff,  0x02,  0x00,  0x57,  0x52,
  0xad,  0x02,  0x13,  0x58,  0xff,  0x02,  0x00,  0x57,  0x52,  0xfe,  0x00,  0x5e,  0x02,  0x13,  0x58,  0xff,
  0x02,  0x00,  0x57,  0x52,  0xad,  0xfe,  0x0b,  0x58,  0x02,  0x0a,  0x66,  0x01,  0x5c,  0x0a,  0x55,  0x01,
  0x5c,  0x0a,  0x6f,  0x01,  0x5c,  0x02,  0x01,  0xfe,  0x1e,  0x1f,  0x23,  0x1a,  0xff,  0x03,  0x00,  0x54,
  0xfe,  0x00,  0xf4,  0x24,  0x52,  0x0f,  0xfe,  0x00,  0x7c,  0x04,  0xfe,  0x07,  0x7c,  0x3a,  0x0b,  0x0e,
  0xfe,  0x00,  0x71,  0xfe,  0xf9,  0x18,  0xfe,  0x7a,  0x19,  0xfe,  0xfb,  0x19,  0xfe,  0x1a,  0xf7,  0x00,
  0xfe,  0x1b,  0xf7,  0x00,  0x7a,  0x30,  0x10,  0x68,  0x22,  0x69,  0xd9,  0x6c,  0xda,  0x6d,  0x02,  0xfe,
  0x62,  0x08,  0xfe,  0x82,  0x4a,  0xfe,  0xe1,  0x1a,  0xfe,  0x83,  0x5a,  0x77,  0x02,  0x01,  0xc6,  0xfe,
  0x42,  0x48,  0x4f,  0x50,  0x45,  0x01,  0x08,  0x16,  0xfe,  0xe0,  0x17,  0x27,  0x25,  0xbe,  0x01,  0x08,
  0x16,  0xfe,  0xe0,  0x17,  0x27,  0x25,  0xfe,  0xe8,  0x0a,  0xfe,  0xc1,  0x59,  0x03,  0x9a,  0x1e,  0xfe,
  0xda,  0x12,  0x01,  0x38,  0x06,  0x12,  0xfe,  0xd0,  0x13,  0x26,  0x53,  0x12,  0x48,  0xfe,  0x08,  0x17,
  0xd1,  0x12,  0x53,  0x12,  0xfe,  0x1e,  0x13,  0x2d,  0xb4,  0x7b,  0xfe,  0x26,  0x17,  0x4d,  0x13,  0x07,
  0x1c,  0xb4,  0x90,  0x04,  0xfe,  0x78,  0x10,  0xff,  0x02,  0x83,  0x55,  0xf1,  0xff,  0x02,  0x83,  0x55,
  0x53,  0x1d,  0xfe,  0x12,  0x13,  0xd6,  0xfe,  0x30,  0x00,  0xb0,  0xfe,  0x80,  0x17,  0x1c,  0x63,  0x13,
  0x07,  0xfe,  0x56,  0x10,  0x53,  0x0d,  0xfe,  0x16,  0x13,  0xd6,  0xfe,  0x64,  0x00,  0xb0,  0xfe,  0x80,
  0x17,  0x0a,  0xfe,  0x64,  0x00,  0x1c,  0x94,  0x13,  0x07,  0xfe,  0x28,  0x10,  0x53,  0x07,  0xfe,  0x60,
  0x13,  0xd6,  0xfe,  0xc8,  0x00,  0xb0,  0xfe,  0x80,  0x17,  0x0a,  0xfe,  0xc8,  0x00,  0x1c,  0x95,  0x13,
  0x07,  0x71,  0xd6,  0xfe,  0x90,  0x01,  0x48,  0xfe,  0x8c,  0x17,  0x45,  0xf3,  0xfe,  0x43,  0xf4,  0x96,
  0xfe,  0x56,  0xf0,  0xfe,  0x9e,  0x17,  0xfe,  0x04,  0xf4,  0x58,  0xfe,  0x43,  0xf4,  0x94,  0xf6,  0x8b,
  0x01,  0xfe,  0x24,  0x16,  0x23,  0x3f,  0xfc,  0xa8,  0x8c,  0x49,  0x48,  0xfe,  0xda,  0x17,  0x62,  0x49,
  0xfe,  0x1c,  0x10,  0xa8,  0x8c,  0x80,  0x48,  0xfe,  0xda,  0x17,  0x62,  0x80,  0x71,  0x50,  0x26,  0xfe,
  0x4d,  0xf4,  0x00,  0xf7,  0x45,  0x13,  0x07,  0xfe,  0xb4,  0x56,  0xfe,  0xc3,  0x58,  0x02,  0x50,  0x13,
  0x0d,  0x02,  0x50,  0x3e,  0x78,  0x4f,  0x45,  0x01,  0x08,  0x16,  0xa9,  0x27,  0x25,  0xbe,  0xfe,  0x03,
  0xea,  0xfe,  0x7e,  0x01,  0x01,  0x08,  0x16,  0xa9,  0x27,  0x25,  0xfe,  0xe9,  0x0a,  0x01,  0x08,  0x16,
  0xa9,  0x27,  0x25,  0xfe,  0xe9,  0x0a,  0xfe,  0x05,  0xea,  0xfe,  0x7f,  0x01,  0x01,  0x08,  0x16,  0xa9,
  0x27,  0x25,  0xfe,  0x69,  0x09,  0xfe,  0x02,  0xea,  0xfe,  0x80,  0x01,  0x01,  0x08,  0x16,  0xa9,  0x27,
  0x25,  0xfe,  0xe8,  0x08,  0x47,  0xfe,  0x81,  0x01,  0x03,  0xb6,  0x1e,  0x83,  0x01,  0x38,  0x06,  0x24,
  0x31,  0xa2,  0x78,  0xf2,  0x53,  0x07,  0x36,  0xfe,  0x34,  0xf4,  0x3f,  0xa1,  0x78,  0x03,  0x9a,  0x1e,
  0x83,  0x01,  0x38,  0x06,  0x12,  0x31,  0xf0,  0x4f,  0x45,  0xfe,  0x90,  0x10,  0xfe,  0x40,  0x5a,  0x23,
  0x3f,  0xfb,  0x8c,  0x49,  0x48,  0xfe,  0xaa,  0x18,  0x62,  0x49,  0x71,  0x8c,  0x80,  0x48,  0xfe,  0xaa,
  0x18,  0x62,  0x80,  0xfe,  0xb4,  0x56,  0xfe,  0x40,  0x5d,  0x01,  0xc6,  0x01,  0xfe,  0xac,  0x1d,  0xfe,
  0x02,  0x17,  0xfe,  0xc8,  0x45,  0xfe,  0x5a,  0xf0,  0xfe,  0xc0,  0x18,  0xfe,  0x43,  0x48,  0x2d,  0x93,
  0x36,  0xfe,  0x34,  0xf4,  0xfe,  0x00,  0x11,  0xfe,  0x40,  0x10,  0x2d,  0xb4,  0x36,  0xfe,  0x34,  0xf4,
  0x04,  0xfe,  0x34,  0x10,  0x2d,  0xfe,  0x0b,  0x00,  0x36,  0x46,  0x63,  0xfe,  0x28,  0x10,  0xfe,  0xc0,
  0x49,  0xff,  0x02,  0x00,  0x54,  0xb2,  0xfe,  0x90,  0x01,  0x48,  0xfe,  0xfa,  0x18,  0x45,  0xfe,  0x1c,
  0xf4,  0x3f,  0xf3,  0xfe,  0x40,  0xf4,  0x96,  0xfe,  0x56,  0xf0,  0xfe,  0x0c,  0x19,  0xfe,  0x04,  0xf4,
  0x58,  0xfe,  0x40,  0xf4,  0x94,  0xf6,  0x3e,  0x2d,  0x93,  0x4e,  0xd0,  0x0d,  0x21,  0xfe,  0x7f,  0x01,
  0xfe,  0xc8,  0x46,  0xfe,  0x24,  0x13,  0x8c,  0x00,  0x5d,  0x26,  0x21,  0xfe,  0x7e,  0x01,  0xfe,  0xc8,
  0x45,  0xfe,  0x14,  0x13,  0x21,  0xfe,  0x80,  0x01,  0xfe,  0x48,  0x45,  0xfa,  0x21,  0xfe,  0x81,  0x01,
  0xfe,  0xc8,  0x44,  0x4e,  0x26,  0x02,  0x13,  0x07,  0x02,  0x78,  0x45,  0x50,  0x13,  0x0d,  0x02,  0x14,
  0x07,  0x01,  0x08,  0x17,  0xfe,  0x82,  0x19,  0x14,  0x0d,  0x01,  0x08,  0x17,  0xfe,  0x82,  0x19,  0x14,
  0x1d,  0x01,  0x08,  0x17,  0xfe,  0x82,  0x19,  0x5f,  0xfe,  0x89,  0x49,  0x01,  0x08,  0x02,  0x14,  0x07,
  0x01,  0x08,  0x17,  0xc1,  0x14,  0x1d,  0x01,  0x08,  0x17,  0xc1,  0x14,  0x07,  0x01,  0x08,  0x17,  0xc1,
  0xfe,  0x89,  0x49,  0x01,  0x08,  0x17,  0xc1,  0x5f,  0xfe,  0x89,  0x4a,  0x01,  0x08,  0x02,  0x50,  0x02,
  0x14,  0x07,  0x01,  0x08,  0x17,  0x74,  0x14,  0x7f,  0x01,  0x08,  0x17,  0x74,  0x14,  0x12,  0x01,  0x08,
  0x17,  0x74,  0xfe,  0x89,  0x49,  0x01,  0x08,  0x17,  0x74,  0x14,  0x00,  0x01,  0x08,  0x17,  0x74,  0xfe,
  0x89,  0x4a,  0x01,  0x08,  0x17,  0x74,  0xfe,  0x09,  0x49,  0x01,  0x08,  0x17,  0x74,  0x5f,  0xcc,  0x01,
  0x08,  0x02,  0x21,  0xe4,  0x09,  0x07,  0xfe,  0x4c,  0x13,  0xc8,  0x20,  0xe4,  0xfe,  0x49,  0xf4,  0x00,
  0x4d,  0x5f,  0xa1,  0x5e,  0xfe,  0x01,  0xec,  0xfe,  0x27,  0x01,  0xcc,  0xff,  0x02,  0x00,  0x10,  0x2f,
  0xfe,  0x3e,  0x1a,  0x01,  0x43,  0x09,  0xfe,  0xe3,  0x00,  0xfe,  0x22,  0x13,  0x16,  0xfe,  0x64,  0x1a,
  0x26,  0x20,  0x9e,  0x01,  0x41,  0x21,  0x9e,  0x09,  0x07,  0x5d,  0x01,  0x0c,  0x61,  0x07,  0x44,  0x02,
  0x0a,  0x5a,  0x01,  0x18,  0xfe,  0x00,  0x40,  0xaa,  0x09,  0x1a,  0xfe,  0x12,  0x13,  0x0a,  0x9d,  0x01,
  0x18,  0xaa,  0x0a,  0x67,  0x01,  0xa3,  0x02,  0x0a,  0x9d,  0x01,  0x18,  0xaa,  0xfe,  0x80,  0xe7,  0x1a,
  0x09,  0x1a,  0x5d,  0xfe,  0x45,  0x58,  0x01,  0xfe,  0xb2,  0x16,  0xaa,  0x02,  0x0a,  0x5a,  0x01,  0x18,
  0xaa,  0x0a,  0x67,  0x01,  0xa3,  0x02,  0x0a,  0x5a,  0x01,  0x18,  0x01,  0xfe,  0x7e,  0x1e,  0xfe,  0x80,
  0x4c,  0xfe,  0x49,  0xe4,  0x1a,  0xfe,  0x12,  0x13,  0x0a,  0x9d,  0x01,  0x18,  0xfe,  0x80,  0x4c,  0x0a,
  0x67,  0x01,  0x5c,  0x02,  0x1c,  0x1a,  0x87,  0x7c,  0xe5,  0xfe,  0x18,  0xdf,  0xfe,  0x19,  0xde,  0xfe,
  0x24,  0x1c,  0xfe,  0x1d,  0xf7,  0x28,  0xb1,  0xfe,  0x04,  0x1b,  0x01,  0xfe,  0x2a,  0x1c,  0xfa,  0xb3,
  0x28,  0x7c,  0xfe,  0x2c,  0x01,  0xfe,  0x2f,  0x19,  0x02,  0xc9,  0x2b,  0xfe,  0xf4,  0x1a,  0xfe,  0xfa,
  0x10,  0x1c,  0x1a,  0x87,  0x03,  0xfe,  0x64,  0x01,  0xfe,  0x00,  0xf4,  0x24,  0xfe,  0x18,  0x58,  0x03,
  0xfe,  0x66,  0x01,  0xfe,  0x19,  0x58,  0xb3,  0x24,  0x01,  0xfe,  0x0e,  0x1f,  0xfe,  0x30,  0xf4,  0x07,
  0xfe,  0x3c,  0x50,  0x7c,  0xfe,  0x38,  0x00,  0xfe,  0x0f,  0x79,  0xfe,  0x1c,  0xf7,  0x24,  0xb1,  0xfe,
  0x50,  0x1b,  0xfe,  0xd4,  0x14,  0x31,  0x02,  0xc9,  0x2b,  0xfe,  0x26,  0x1b,  0xfe,  0xba,  0x10,  0x1c,
  0x1a,  0x87,  0xfe,  0x83,  0x5a,  0xfe,  0x18,  0xdf,  0xfe,  0x19,  0xde,  0xfe,  0x1d,  0xf7,  0x54,  0xb1,
  0xfe,  0x72,  0x1b,  0xfe,  0xb2,  0x14,  0xfc,  0xb3,  0x54,  0x7c,  0x12,  0xfe,  0xaf,  0x19,  0xfe,  0x98,
  0xe7,  0x00,  0x02,  0xc9,  0x2b,  0xfe,  0x66,  0x1b,  0xfe,  0x8a,  0x10,  0x1c,  0x1a,  0x87,  0x8b,  0x0f,
  0xfe,  0x30,  0x90,  0x04,  0xfe,  0xb0,  0x93,  0x3a,  0x0b,  0xfe,  0x18,  0x58,  0xfe,  0x32,  0x90,  0x04,
  0xfe,  0xb2,  0x93,  0x3a,  0x0b,  0xfe,  0x19,  0x58,  0x0e,  0xa8,  0xb3,  0x4a,  0x7c,  0x12,  0xfe,  0x0f,
  0x79,  0xfe,  0x1c,  0xf7,  0x4a,  0xb1,  0xfe,  0xc6,  0x1b,  0xfe,  0x5e,  0x14,  0x31,  0x02,  0xc9,  0x2b,
  0xfe,  0x96,  0x1b,  0x5c,  0xfe,  0x02,  0xf6,  0x1a,  0x87,  0xfe,  0x18,  0xfe,  0x6a,  0xfe,  0x19,  0xfe,
  0x6b,  0x01,  0xfe,  0x1e,  0x1f,  0xfe,  0x1d,  0xf7,  0x65,  0xb1,  0xfe,  0xee,  0x1b,  0xfe,  0x36,  0x14,
  0xfe,  0x1c,  0x13,  0xb3,  0x65,  0x3e,  0xfe,  0x83,  0x58,  0xfe,  0xaf,  0x19,  0xfe,  0x80,  0xe7,  0x1a,
  0xfe,  0x81,  0xe7,  0x1a,  0x15,  0xfe,  0xdd,  0x00,  0x7a,  0x30,  0x02,  0x7a,  0x30,  0xfe,  0x12,  0x45,
  0x2b,  0xfe,  0xdc,  0x1b,  0x1f,  0x07,  0x47,  0xb5,  0xc3,  0x05,  0x35,  0xfe,  0x39,  0xf0,  0x75,  0x26,
  0x02,  0xfe,  0x7e,  0x18,  0x23,  0x1d,  0x36,  0x13,  0x11,  0x02,  0x87,  0x03,  0xe3,  0x23,  0x07,  0xfe,
  0xef,  0x12,  0xfe,  0xe1,  0x10,  0x90,  0x34,  0x60,  0xfe,  0x02,  0x80,  0x09,  0x56,  0xfe,  0x3c,  0x13,
  0xfe,  0x82,  0x14,  0xfe,  0x42,  0x13,  0x51,  0xfe,  0x06,  0x83,  0x0a,  0x5a,  0x01,  0x18,  0xcb,  0xfe,
  0x3e,  0x12,  0xfe,  0x41,  0x48,  0xfe,  0x45,  0x48,  0x01,  0xfe,  0xb2,  0x16,  0xfe,  0x00,  0xcc,  0xcb,
  0xfe,  0xf3,  0x13,  0x3f,  0x89,  0x09,  0x1a,  0xa5,  0x0a,  0x9d,  0x01,  0x18,  0xfe,  0x80,  0x4c,  0x01,
  0x85,  0xfe,  0x16,  0x10,  0x09,  0x9b,  0x4e,  0xfe,  0x40,  0x14,  0xfe,  0x24,  0x12,  0xfe,  0x14,  0x56,
  0xfe,  0xd6,  0xf0,  0xfe,  0x52,  0x1c,  0x1c,  0x0d,  0x02,  0xfe,  0x9c,  0xe7,  0x0d,  0x19,  0xfe,  0x15,
  0x00,  0x40,  0x8d,  0x30,  0x01,  0xf4,  0x1c,  0x07,  0x02,  0x51,  0xfe,  0x06,  0x83,  0xfe,  0x18,  0x80,
  0x61,  0x28,  0x44,  0x15,  0x56,  0x01,  0x85,  0x1c,  0x07,  0x02,  0xfe,  0x38,  0x90,  0xfe,  0xba,  0x90,
  0x91,  0xde,  0x7e,  0xdf,  0xfe,  0x48,  0x55,  0x31,  0xfe,  0xc9,  0x55,  0x02,  0x21,  0xb9,  0x88,  0x20,
  0xb9,  0x02,  0x0a,  0xba,  0x01,  0x18,  0xfe,  0x41,  0x48,  0x0a,  0x57,  0x01,  0x18,  0xfe,  0x49,  0x44,
  0x1b,  0xfe,  0x1e,  0x1d,  0x88,  0x89,  0x02,  0x0a,  0x5a,  0x01,  0x18,  0x09,  0x1a,  0xa4,  0x0a,  0x67,
  0x01,  0xa3,  0x0a,  0x57,  0x01,  0x18,  0x88,  0x89,  0x02,  0xfe,  0x4e,  0xe4,  0x1d,  0x7b,  0xfe,  0x52,
  0x1d,  0x03,  0xfe,  0x90,  0x00,  0xfe,  0x3a,  0x45,  0xfe,  0x2c,  0x10,  0xfe,  0x4e,  0xe4,  0xdd,  0x7b,
  0xfe,  0x64,  0x1d,  0x03,  0xfe,  0x92,  0x00,  0xd1,  0x12,  0xfe,  0x1a,  0x10,  0xfe,  0x4e,  0xe4,  0xfe,
  0x0b,  0x00,  0x7b,  0xfe,  0x76,  0x1d,  0x03,  0xfe,  0x94,  0x00,  0xd1,  0x24,  0xfe,  0x08,  0x10,  0x03,
  0xfe,  0x96,  0x00,  0xd1,  0x63,  0xfe,  0x4e,  0x45,  0x83,  0xca,  0xff,  0x04,  0x68,  0x54,  0xfe,  0xf1,
  0x10,  0x23,  0x49,  0xfe,  0x08,  0x1c,  0xfe,  0x67,  0x19,  0xfe,  0x0a,  0x1c,  0xfe,  0x1a,  0xf4,  0xfe,
  0x00,  0x04,  0x83,  0xb2,  0x1d,  0x48,  0xfe,  0xaa,  0x1d,  0x13,  0x1d,  0x02,  0x09,  0x92,  0xfe,  0x5a,
  0xf0,  0xfe,  0xba,  0x1d,  0x2e,  0x93,  0xfe,  0x34,  0x10,  0x09,  0x12,  0xfe,  0x5a,  0xf0,  0xfe,  0xc8,
  0x1d,  0x2e,  0xb4,  0xfe,  0x26,  0x10,  0x09,  0x1d,  0x36,  0x2e,  0x63,  0xfe,  0x1a,  0x10,  0x09,  0x0d,
  0x36,  0x2e,  0x94,  0xf2,  0x09,  0x07,  0x36,  0x2e,  0x95,  0xa1,  0xc8,  0x02,  0x1f,  0x93,  0x01,  0x42,
  0xfe,  0x04,  0xfe,  0x99,  0x03,  0x9c,  0x8b,  0x02,  0x2a,  0xfe,  0x1c,  0x1e,  0xfe,  0x14,  0xf0,  0x08,
  0x2f,  0xfe,  0x0c,  0x1e,  0x2a,  0xfe,  0x1c,  0x1e,  0x8f,  0xfe,  0x1c,  0x1e,  0xfe,  0x82,  0xf0,  0xfe,
  0x10,  0x1e,  0x02,  0x0f,  0x3f,  0x04,  0xfe,  0x80,  0x83,  0x33,  0x0b,  0x0e,  0x02,  0x0f,  0xfe,  0x18,
  0x80,  0x04,  0xfe,  0x98,  0x83,  0x33,  0x0b,  0x0e,  0x02,  0x0f,  0xfe,  0x02,  0x80,  0x04,  0xfe,  0x82,
  0x83,  0x33,  0x0b,  0x0e,  0x02,  0x0f,  0xfe,  0x06,  0x80,  0x04,  0xfe,  0x86,  0x83,  0x33,  0x0b,  0x0e,
  0x02,  0x0f,  0xfe,  0x1b,  0x80,  0x04,  0xfe,  0x9b,  0x83,  0x33,  0x0b,  0x0e,  0x02,  0x0f,  0xfe,  0x04,
  0x80,  0x04,  0xfe,  0x84,  0x83,  0x33,  0x0b,  0x0e,  0x02,  0x0f,  0xfe,  0x80,  0x80,  0x04,  0xfe,  0x80,
  0x83,  0xfe,  0xc9,  0x47,  0x0b,  0x0e,  0x02,  0x0f,  0xfe,  0x19,  0x81,  0x04,  0xfe,  0x99,  0x83,  0xfe,
  0xca,  0x47,  0x0b,  0x0e,  0x02,  0x0f,  0xfe,  0x06,  0x83,  0x04,  0xfe,  0x86,  0x83,  0xfe,  0xce,  0x47,
  0x0b,  0x0e,  0x02,  0x0f,  0xfe,  0x2c,  0x90,  0x04,  0xfe,  0xac,  0x93,  0x3a,  0x0b,  0x0e,  0x02,  0x0f,
  0xfe,  0xae,  0x90,  0x04,  0xfe,  0xae,  0x93,  0x79,  0x0b,  0x0e,  0x02,  0x0f,  0xfe,  0x08,  0x90,  0x04,
  0xfe,  0x88,  0x93,  0x3a,  0x0b,  0x0e,  0x02,  0x0f,  0xfe,  0x8a,  0x90,  0x04,  0xfe,  0x8a,  0x93,  0x79,
  0x0b,  0x0e,  0x02,  0x0f,  0xfe,  0x0c,  0x90,  0x04,  0xfe,  0x8c,  0x93,  0x3a,  0x0b,  0x0e,  0x02,  0x0f,
  0xfe,  0x8e,  0x90,  0x04,  0xfe,  0x8e,  0x93,  0x79,  0x0b,  0x0e,  0x02,  0x0f,  0xfe,  0x3c,  0x90,  0x04,
  0xfe,  0xbc,  0x93,  0x3a,  0x0b,  0x0e,  0x02,  0x8b,  0x0f,  0xfe,  0x03,  0x80,  0x04,  0xfe,  0x83,  0x83,
  0x33,  0x0b,  0x77,  0x0e,  0xa8,  0x02,  0xff,  0x66,  0x00,  0x00,
};

STATIC unsigned short _adv_asc38C1600_size =
        sizeof(_adv_asc38C1600_buf); /* 0x1673 */
STATIC ADV_DCNT _adv_asc38C1600_chksum =
        0x0604EF77UL; /* Expanded little-endian checksum. */

/* a_init.c */
/*
 * EEPROM Configuration.
 *
 * All drivers should use this structure to set the default EEPROM
 * configuration. The BIOS now uses this structure when it is built.
 * Additional structure information can be found in a_condor.h where
 * the structure is defined.
 *
 * The *_Field_IsChar structs are needed to correct for endianness.
 * These values are read from the board 16 bits at a time directly
 * into the structs. Because some fields are char, the values will be
 * in the wrong order. The *_Field_IsChar tells when to flip the
 * bytes. Data read and written to PCI memory is automatically swapped
 * on big-endian platforms so char fields read as words are actually being
 * unswapped on big-endian platforms.
 */
STATIC ADVEEP_3550_CONFIG
Default_3550_EEPROM_Config __initdata = {
    ADV_EEPROM_BIOS_ENABLE,     /* cfg_lsw */
    0x0000,                     /* cfg_msw */
    0xFFFF,                     /* disc_enable */
    0xFFFF,                     /* wdtr_able */
    0xFFFF,                     /* sdtr_able */
    0xFFFF,                     /* start_motor */
    0xFFFF,                     /* tagqng_able */
    0xFFFF,                     /* bios_scan */
    0,                          /* scam_tolerant */
    7,                          /* adapter_scsi_id */
    0,                          /* bios_boot_delay */
    3,                          /* scsi_reset_delay */
    0,                          /* bios_id_lun */
    0,                          /* termination */
    0,                          /* reserved1 */
    0xFFE7,                     /* bios_ctrl */
    0xFFFF,                     /* ultra_able */
    0,                          /* reserved2 */
    ASC_DEF_MAX_HOST_QNG,       /* max_host_qng */
    ASC_DEF_MAX_DVC_QNG,        /* max_dvc_qng */
    0,                          /* dvc_cntl */
    0,                          /* bug_fix */
    0,                          /* serial_number_word1 */
    0,                          /* serial_number_word2 */
    0,                          /* serial_number_word3 */
    0,                          /* check_sum */
    { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, /* oem_name[16] */
    0,                          /* dvc_err_code */
    0,                          /* adv_err_code */
    0,                          /* adv_err_addr */
    0,                          /* saved_dvc_err_code */
    0,                          /* saved_adv_err_code */
    0,                          /* saved_adv_err_addr */
    0                           /* num_of_err */
};

STATIC ADVEEP_3550_CONFIG
ADVEEP_3550_Config_Field_IsChar __initdata = {
    0,                          /* cfg_lsw */
    0,                          /* cfg_msw */
    0,                          /* -disc_enable */
    0,                          /* wdtr_able */
    0,                          /* sdtr_able */
    0,                          /* start_motor */
    0,                          /* tagqng_able */
    0,                          /* bios_scan */
    0,                          /* scam_tolerant */
    1,                          /* adapter_scsi_id */
    1,                          /* bios_boot_delay */
    1,                          /* scsi_reset_delay */
    1,                          /* bios_id_lun */
    1,                          /* termination */
    1,                          /* reserved1 */
    0,                          /* bios_ctrl */
    0,                          /* ultra_able */
    0,                          /* reserved2 */
    1,                          /* max_host_qng */
    1,                          /* max_dvc_qng */
    0,                          /* dvc_cntl */
    0,                          /* bug_fix */
    0,                          /* serial_number_word1 */
    0,                          /* serial_number_word2 */
    0,                          /* serial_number_word3 */
    0,                          /* check_sum */
    { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 }, /* oem_name[16] */
    0,                          /* dvc_err_code */
    0,                          /* adv_err_code */
    0,                          /* adv_err_addr */
    0,                          /* saved_dvc_err_code */
    0,                          /* saved_adv_err_code */
    0,                          /* saved_adv_err_addr */
    0                           /* num_of_err */
};

STATIC ADVEEP_38C0800_CONFIG
Default_38C0800_EEPROM_Config __initdata = {
    ADV_EEPROM_BIOS_ENABLE,     /* 00 cfg_lsw */
    0x0000,                     /* 01 cfg_msw */
    0xFFFF,                     /* 02 disc_enable */
    0xFFFF,                     /* 03 wdtr_able */
    0x4444,                     /* 04 sdtr_speed1 */
    0xFFFF,                     /* 05 start_motor */
    0xFFFF,                     /* 06 tagqng_able */
    0xFFFF,                     /* 07 bios_scan */
    0,                          /* 08 scam_tolerant */
    7,                          /* 09 adapter_scsi_id */
    0,                          /*    bios_boot_delay */
    3,                          /* 10 scsi_reset_delay */
    0,                          /*    bios_id_lun */
    0,                          /* 11 termination_se */
    0,                          /*    termination_lvd */
    0xFFE7,                     /* 12 bios_ctrl */
    0x4444,                     /* 13 sdtr_speed2 */
    0x4444,                     /* 14 sdtr_speed3 */
    ASC_DEF_MAX_HOST_QNG,       /* 15 max_host_qng */
    ASC_DEF_MAX_DVC_QNG,        /*    max_dvc_qng */
    0,                          /* 16 dvc_cntl */
    0x4444,                     /* 17 sdtr_speed4 */
    0,                          /* 18 serial_number_word1 */
    0,                          /* 19 serial_number_word2 */
    0,                          /* 20 serial_number_word3 */
    0,                          /* 21 check_sum */
    { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, /* 22-29 oem_name[16] */
    0,                          /* 30 dvc_err_code */
    0,                          /* 31 adv_err_code */
    0,                          /* 32 adv_err_addr */
    0,                          /* 33 saved_dvc_err_code */
    0,                          /* 34 saved_adv_err_code */
    0,                          /* 35 saved_adv_err_addr */
    0,                          /* 36 reserved */
    0,                          /* 37 reserved */
    0,                          /* 38 reserved */
    0,                          /* 39 reserved */
    0,                          /* 40 reserved */
    0,                          /* 41 reserved */
    0,                          /* 42 reserved */
    0,                          /* 43 reserved */
    0,                          /* 44 reserved */
    0,                          /* 45 reserved */
    0,                          /* 46 reserved */
    0,                          /* 47 reserved */
    0,                          /* 48 reserved */
    0,                          /* 49 reserved */
    0,                          /* 50 reserved */
    0,                          /* 51 reserved */
    0,                          /* 52 reserved */
    0,                          /* 53 reserved */
    0,                          /* 54 reserved */
    0,                          /* 55 reserved */
    0,                          /* 56 cisptr_lsw */
    0,                          /* 57 cisprt_msw */
    PCI_VENDOR_ID_ASP,          /* 58 subsysvid */
    PCI_DEVICE_ID_38C0800_REV1, /* 59 subsysid */
    0,                          /* 60 reserved */
    0,                          /* 61 reserved */
    0,                          /* 62 reserved */
    0                           /* 63 reserved */
};

STATIC ADVEEP_38C0800_CONFIG
ADVEEP_38C0800_Config_Field_IsChar __initdata = {
    0,                          /* 00 cfg_lsw */
    0,                          /* 01 cfg_msw */
    0,                          /* 02 disc_enable */
    0,                          /* 03 wdtr_able */
    0,                          /* 04 sdtr_speed1 */
    0,                          /* 05 start_motor */
    0,                          /* 06 tagqng_able */
    0,                          /* 07 bios_scan */
    0,                          /* 08 scam_tolerant */
    1,                          /* 09 adapter_scsi_id */
    1,                          /*    bios_boot_delay */
    1,                          /* 10 scsi_reset_delay */
    1,                          /*    bios_id_lun */
    1,                          /* 11 termination_se */
    1,                          /*    termination_lvd */
    0,                          /* 12 bios_ctrl */
    0,                          /* 13 sdtr_speed2 */
    0,                          /* 14 sdtr_speed3 */
    1,                          /* 15 max_host_qng */
    1,                          /*    max_dvc_qng */
    0,                          /* 16 dvc_cntl */
    0,                          /* 17 sdtr_speed4 */
    0,                          /* 18 serial_number_word1 */
    0,                          /* 19 serial_number_word2 */
    0,                          /* 20 serial_number_word3 */
    0,                          /* 21 check_sum */
    { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 }, /* 22-29 oem_name[16] */
    0,                          /* 30 dvc_err_code */
    0,                          /* 31 adv_err_code */
    0,                          /* 32 adv_err_addr */
    0,                          /* 33 saved_dvc_err_code */
    0,                          /* 34 saved_adv_err_code */
    0,                          /* 35 saved_adv_err_addr */
    0,                          /* 36 reserved */
    0,                          /* 37 reserved */
    0,                          /* 38 reserved */
    0,                          /* 39 reserved */
    0,                          /* 40 reserved */
    0,                          /* 41 reserved */
    0,                          /* 42 reserved */
    0,                          /* 43 reserved */
    0,                          /* 44 reserved */
    0,                          /* 45 reserved */
    0,                          /* 46 reserved */
    0,                          /* 47 reserved */
    0,                          /* 48 reserved */
    0,                          /* 49 reserved */
    0,                          /* 50 reserved */
    0,                          /* 51 reserved */
    0,                          /* 52 reserved */
    0,                          /* 53 reserved */
    0,                          /* 54 reserved */
    0,                          /* 55 reserved */
    0,                          /* 56 cisptr_lsw */
    0,                          /* 57 cisprt_msw */
    0,                          /* 58 subsysvid */
    0,                          /* 59 subsysid */
    0,                          /* 60 reserved */
    0,                          /* 61 reserved */
    0,                          /* 62 reserved */
    0                           /* 63 reserved */
};

STATIC ADVEEP_38C1600_CONFIG
Default_38C1600_EEPROM_Config __initdata = {
    ADV_EEPROM_BIOS_ENABLE,     /* 00 cfg_lsw */
    0x0000,                     /* 01 cfg_msw */
    0xFFFF,                     /* 02 disc_enable */
    0xFFFF,                     /* 03 wdtr_able */
    0x5555,                     /* 04 sdtr_speed1 */
    0xFFFF,                     /* 05 start_motor */
    0xFFFF,                     /* 06 tagqng_able */
    0xFFFF,                     /* 07 bios_scan */
    0,                          /* 08 scam_tolerant */
    7,                          /* 09 adapter_scsi_id */
    0,                          /*    bios_boot_delay */
    3,                          /* 10 scsi_reset_delay */
    0,                          /*    bios_id_lun */
    0,                          /* 11 termination_se */
    0,                          /*    termination_lvd */
    0xFFE7,                     /* 12 bios_ctrl */
    0x5555,                     /* 13 sdtr_speed2 */
    0x5555,                     /* 14 sdtr_speed3 */
    ASC_DEF_MAX_HOST_QNG,       /* 15 max_host_qng */
    ASC_DEF_MAX_DVC_QNG,        /*    max_dvc_qng */
    0,                          /* 16 dvc_cntl */
    0x5555,                     /* 17 sdtr_speed4 */
    0,                          /* 18 serial_number_word1 */
    0,                          /* 19 serial_number_word2 */
    0,                          /* 20 serial_number_word3 */
    0,                          /* 21 check_sum */
    { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, /* 22-29 oem_name[16] */
    0,                          /* 30 dvc_err_code */
    0,                          /* 31 adv_err_code */
    0,                          /* 32 adv_err_addr */
    0,                          /* 33 saved_dvc_err_code */
    0,                          /* 34 saved_adv_err_code */
    0,                          /* 35 saved_adv_err_addr */
    0,                          /* 36 reserved */
    0,                          /* 37 reserved */
    0,                          /* 38 reserved */
    0,                          /* 39 reserved */
    0,                          /* 40 reserved */
    0,                          /* 41 reserved */
    0,                          /* 42 reserved */
    0,                          /* 43 reserved */
    0,                          /* 44 reserved */
    0,                          /* 45 reserved */
    0,                          /* 46 reserved */
    0,                          /* 47 reserved */
    0,                          /* 48 reserved */
    0,                          /* 49 reserved */
    0,                          /* 50 reserved */
    0,                          /* 51 reserved */
    0,                          /* 52 reserved */
    0,                          /* 53 reserved */
    0,                          /* 54 reserved */
    0,                          /* 55 reserved */
    0,                          /* 56 cisptr_lsw */
    0,                          /* 57 cisprt_msw */
    PCI_VENDOR_ID_ASP,          /* 58 subsysvid */
    PCI_DEVICE_ID_38C1600_REV1, /* 59 subsysid */
    0,                          /* 60 reserved */
    0,                          /* 61 reserved */
    0,                          /* 62 reserved */
    0                           /* 63 reserved */
};

STATIC ADVEEP_38C1600_CONFIG
ADVEEP_38C1600_Config_Field_IsChar __initdata = {
    0,                          /* 00 cfg_lsw */
    0,                          /* 01 cfg_msw */
    0,                          /* 02 disc_enable */
    0,                          /* 03 wdtr_able */
    0,                          /* 04 sdtr_speed1 */
    0,                          /* 05 start_motor */
    0,                          /* 06 tagqng_able */
    0,                          /* 07 bios_scan */
    0,                          /* 08 scam_tolerant */
    1,                          /* 09 adapter_scsi_id */
    1,                          /*    bios_boot_delay */
    1,                          /* 10 scsi_reset_delay */
    1,                          /*    bios_id_lun */
    1,                          /* 11 termination_se */
    1,                          /*    termination_lvd */
    0,                          /* 12 bios_ctrl */
    0,                          /* 13 sdtr_speed2 */
    0,                          /* 14 sdtr_speed3 */
    1,                          /* 15 max_host_qng */
    1,                          /*    max_dvc_qng */
    0,                          /* 16 dvc_cntl */
    0,                          /* 17 sdtr_speed4 */
    0,                          /* 18 serial_number_word1 */
    0,                          /* 19 serial_number_word2 */
    0,                          /* 20 serial_number_word3 */
    0,                          /* 21 check_sum */
    { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 }, /* 22-29 oem_name[16] */
    0,                          /* 30 dvc_err_code */
    0,                          /* 31 adv_err_code */
    0,                          /* 32 adv_err_addr */
    0,                          /* 33 saved_dvc_err_code */
    0,                          /* 34 saved_adv_err_code */
    0,                          /* 35 saved_adv_err_addr */
    0,                          /* 36 reserved */
    0,                          /* 37 reserved */
    0,                          /* 38 reserved */
    0,                          /* 39 reserved */
    0,                          /* 40 reserved */
    0,                          /* 41 reserved */
    0,                          /* 42 reserved */
    0,                          /* 43 reserved */
    0,                          /* 44 reserved */
    0,                          /* 45 reserved */
    0,                          /* 46 reserved */
    0,                          /* 47 reserved */
    0,                          /* 48 reserved */
    0,                          /* 49 reserved */
    0,                          /* 50 reserved */
    0,                          /* 51 reserved */
    0,                          /* 52 reserved */
    0,                          /* 53 reserved */
    0,                          /* 54 reserved */
    0,                          /* 55 reserved */
    0,                          /* 56 cisptr_lsw */
    0,                          /* 57 cisprt_msw */
    0,                          /* 58 subsysvid */
    0,                          /* 59 subsysid */
    0,                          /* 60 reserved */
    0,                          /* 61 reserved */
    0,                          /* 62 reserved */
    0                           /* 63 reserved */
};

/*
 * Initialize the ADV_DVC_VAR structure.
 *
 * On failure set the ADV_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 */
STATIC int __init
AdvInitGetConfig(ADV_DVC_VAR *asc_dvc)
{
    ushort      warn_code;
    AdvPortAddr iop_base;
    uchar       pci_cmd_reg;
    int         status;

    warn_code = 0;
    asc_dvc->err_code = 0;
    iop_base = asc_dvc->iop_base;

    /*
     * PCI Command Register
     *
     * Note: AscPCICmdRegBits_BusMastering definition (0x0007) includes
     * I/O Space Control, Memory Space Control and Bus Master Control bits.
     */

    if (((pci_cmd_reg = DvcAdvReadPCIConfigByte(asc_dvc,
                            AscPCIConfigCommandRegister))
         & AscPCICmdRegBits_BusMastering)
        != AscPCICmdRegBits_BusMastering)
    {
        pci_cmd_reg |= AscPCICmdRegBits_BusMastering;

        DvcAdvWritePCIConfigByte(asc_dvc,
                AscPCIConfigCommandRegister, pci_cmd_reg);

        if (((DvcAdvReadPCIConfigByte(asc_dvc, AscPCIConfigCommandRegister))
             & AscPCICmdRegBits_BusMastering)
            != AscPCICmdRegBits_BusMastering)
        {
            warn_code |= ASC_WARN_SET_PCI_CONFIG_SPACE;
        }
    }

    /*
     * PCI Latency Timer
     *
     * If the "latency timer" register is 0x20 or above, then we don't need
     * to change it.  Otherwise, set it to 0x20 (i.e. set it to 0x20 if it
     * comes up less than 0x20).
     */
    if (DvcAdvReadPCIConfigByte(asc_dvc, AscPCIConfigLatencyTimer) < 0x20) {
        DvcAdvWritePCIConfigByte(asc_dvc, AscPCIConfigLatencyTimer, 0x20);
        if (DvcAdvReadPCIConfigByte(asc_dvc, AscPCIConfigLatencyTimer) < 0x20)
        {
            warn_code |= ASC_WARN_SET_PCI_CONFIG_SPACE;
        }
    }

    /*
     * Save the state of the PCI Configuration Command Register
     * "Parity Error Response Control" Bit. If the bit is clear (0),
     * in AdvInitAsc3550/38C0800Driver() tell the microcode to ignore
     * DMA parity errors.
     */
    asc_dvc->cfg->control_flag = 0;
    if (((DvcAdvReadPCIConfigByte(asc_dvc, AscPCIConfigCommandRegister)
         & AscPCICmdRegBits_ParErrRespCtrl)) == 0)
    {
        asc_dvc->cfg->control_flag |= CONTROL_FLAG_IGNORE_PERR;
    }

    asc_dvc->cfg->lib_version = (ADV_LIB_VERSION_MAJOR << 8) |
      ADV_LIB_VERSION_MINOR;
    asc_dvc->cfg->chip_version =
      AdvGetChipVersion(iop_base, asc_dvc->bus_type);

    ASC_DBG2(1, "AdvInitGetConfig: iopb_chip_id_1: 0x%x 0x%x\n",
        (ushort) AdvReadByteRegister(iop_base, IOPB_CHIP_ID_1),
        (ushort) ADV_CHIP_ID_BYTE);

    ASC_DBG2(1, "AdvInitGetConfig: iopw_chip_id_0: 0x%x 0x%x\n",
        (ushort) AdvReadWordRegister(iop_base, IOPW_CHIP_ID_0),
        (ushort) ADV_CHIP_ID_WORD);

    /*
     * Reset the chip to start and allow register writes.
     */
    if (AdvFindSignature(iop_base) == 0)
    {
        asc_dvc->err_code = ASC_IERR_BAD_SIGNATURE;
        return ADV_ERROR;
    }
    else {
        /*
         * The caller must set 'chip_type' to a valid setting.
         */
        if (asc_dvc->chip_type != ADV_CHIP_ASC3550 &&
            asc_dvc->chip_type != ADV_CHIP_ASC38C0800 &&
            asc_dvc->chip_type != ADV_CHIP_ASC38C1600)
        {
            asc_dvc->err_code |= ASC_IERR_BAD_CHIPTYPE;
            return ADV_ERROR;
        }

        /*
         * Reset Chip.
         */
        AdvWriteWordRegister(iop_base, IOPW_CTRL_REG,
            ADV_CTRL_REG_CMD_RESET);
        DvcSleepMilliSecond(100);
        AdvWriteWordRegister(iop_base, IOPW_CTRL_REG,
            ADV_CTRL_REG_CMD_WR_IO_REG);

        if (asc_dvc->chip_type == ADV_CHIP_ASC38C1600)
        {
            if ((status = AdvInitFrom38C1600EEP(asc_dvc)) == ADV_ERROR)
            {
                return ADV_ERROR;
            }
        } else if (asc_dvc->chip_type == ADV_CHIP_ASC38C0800)
        {
            if ((status = AdvInitFrom38C0800EEP(asc_dvc)) == ADV_ERROR)
            {
                return ADV_ERROR;
            }
        } else
        {
            if ((status = AdvInitFrom3550EEP(asc_dvc)) == ADV_ERROR)
            {
                return ADV_ERROR;
            }
        }
        warn_code |= status;
    }

    return warn_code;
}

/*
 * Initialize the ASC-3550.
 *
 * On failure set the ADV_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 *
 * Needed after initialization for error recovery.
 */
STATIC int
AdvInitAsc3550Driver(ADV_DVC_VAR *asc_dvc)
{
    AdvPortAddr iop_base;
    ushort      warn_code;
    ADV_DCNT    sum;
    int         begin_addr;
    int         end_addr;
    ushort      code_sum;
    int         word;
    int         j;
    int         adv_asc3550_expanded_size;
    ADV_CARR_T  *carrp;
    ADV_DCNT    contig_len;
    ADV_SDCNT   buf_size;
    ADV_PADDR   carr_paddr;
    int         i;
    ushort      scsi_cfg1;
    uchar       tid;
    ushort      bios_mem[ASC_MC_BIOSLEN/2]; /* BIOS RISC Memory 0x40-0x8F. */
    ushort      wdtr_able = 0, sdtr_able, tagqng_able;
    uchar       max_cmd[ADV_MAX_TID + 1];

    /* If there is already an error, don't continue. */
    if (asc_dvc->err_code != 0)
    {
        return ADV_ERROR;
    }

    /*
     * The caller must set 'chip_type' to ADV_CHIP_ASC3550.
     */
    if (asc_dvc->chip_type != ADV_CHIP_ASC3550)
    {
        asc_dvc->err_code |= ASC_IERR_BAD_CHIPTYPE;
        return ADV_ERROR;
    }

    warn_code = 0;
    iop_base = asc_dvc->iop_base;

    /*
     * Save the RISC memory BIOS region before writing the microcode.
     * The BIOS may already be loaded and using its RISC LRAM region
     * so its region must be saved and restored.
     *
     * Note: This code makes the assumption, which is currently true,
     * that a chip reset does not clear RISC LRAM.
     */
    for (i = 0; i < ASC_MC_BIOSLEN/2; i++)
    {
        AdvReadWordLram(iop_base, ASC_MC_BIOSMEM + (2 * i), bios_mem[i]);
    }

    /*
     * Save current per TID negotiated values.
     */
    if (bios_mem[(ASC_MC_BIOS_SIGNATURE - ASC_MC_BIOSMEM)/2] == 0x55AA)
    {
        ushort  bios_version, major, minor;

        bios_version = bios_mem[(ASC_MC_BIOS_VERSION - ASC_MC_BIOSMEM)/2];
        major = (bios_version  >> 12) & 0xF;
        minor = (bios_version  >> 8) & 0xF;
        if (major < 3 || (major == 3 && minor == 1))
        {
            /* BIOS 3.1 and earlier location of 'wdtr_able' variable. */
            AdvReadWordLram(iop_base, 0x120, wdtr_able);
        } else
        {
            AdvReadWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
        }
    }
    AdvReadWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
    AdvReadWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
    for (tid = 0; tid <= ADV_MAX_TID; tid++)
    {
        AdvReadByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
            max_cmd[tid]);
    }

    /*
     * Load the Microcode
     *
     * Write the microcode image to RISC memory starting at address 0.
     */
    AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, 0);
    /* Assume the following compressed format of the microcode buffer:
     *
     *  254 word (508 byte) table indexed by byte code followed
     *  by the following byte codes:
     *
     *    1-Byte Code:
     *      00: Emit word 0 in table.
     *      01: Emit word 1 in table.
     *      .
     *      FD: Emit word 253 in table.
     *
     *    Multi-Byte Code:
     *      FE WW WW: (3 byte code) Word to emit is the next word WW WW.
     *      FF BB WW WW: (4 byte code) Emit BB count times next word WW WW.
     */
    word = 0;
    for (i = 253 * 2; i < _adv_asc3550_size; i++)
    {
        if (_adv_asc3550_buf[i] == 0xff)
        {
            for (j = 0; j < _adv_asc3550_buf[i + 1]; j++)
            {
                AdvWriteWordAutoIncLram(iop_base, (((ushort)
                    _adv_asc3550_buf[i + 3] << 8) |
                _adv_asc3550_buf[i + 2]));
                word++;
            }
            i += 3;
        } else if (_adv_asc3550_buf[i] == 0xfe)
        {
            AdvWriteWordAutoIncLram(iop_base, (((ushort)
                _adv_asc3550_buf[i + 2] << 8) |
                _adv_asc3550_buf[i + 1]));
            i += 2;
            word++;
        } else
        {
            AdvWriteWordAutoIncLram(iop_base, (((ushort)
                _adv_asc3550_buf[(_adv_asc3550_buf[i] * 2) + 1] << 8) |
                _adv_asc3550_buf[_adv_asc3550_buf[i] * 2]));
            word++;
        }
    }

    /*
     * Set 'word' for later use to clear the rest of memory and save
     * the expanded mcode size.
     */
    word *= 2;
    adv_asc3550_expanded_size = word;

    /*
     * Clear the rest of ASC-3550 Internal RAM (8KB).
     */
    for (; word < ADV_3550_MEMSIZE; word += 2)
    {
        AdvWriteWordAutoIncLram(iop_base, 0);
    }

    /*
     * Verify the microcode checksum.
     */
    sum = 0;
    AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, 0);

    for (word = 0; word < adv_asc3550_expanded_size; word += 2)
    {
        sum += AdvReadWordAutoIncLram(iop_base);
    }

    if (sum != _adv_asc3550_chksum)
    {
        asc_dvc->err_code |= ASC_IERR_MCODE_CHKSUM;
        return ADV_ERROR;
    }

    /*
     * Restore the RISC memory BIOS region.
     */
    for (i = 0; i < ASC_MC_BIOSLEN/2; i++)
    {
        AdvWriteWordLram(iop_base, ASC_MC_BIOSMEM + (2 * i), bios_mem[i]);
    }

    /*
     * Calculate and write the microcode code checksum to the microcode
     * code checksum location ASC_MC_CODE_CHK_SUM (0x2C).
     */
    AdvReadWordLram(iop_base, ASC_MC_CODE_BEGIN_ADDR, begin_addr);
    AdvReadWordLram(iop_base, ASC_MC_CODE_END_ADDR, end_addr);
    code_sum = 0;
    AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, begin_addr);
    for (word = begin_addr; word < end_addr; word += 2)
    {
        code_sum += AdvReadWordAutoIncLram(iop_base);
    }
    AdvWriteWordLram(iop_base, ASC_MC_CODE_CHK_SUM, code_sum);

    /*
     * Read and save microcode version and date.
     */
    AdvReadWordLram(iop_base, ASC_MC_VERSION_DATE, asc_dvc->cfg->mcode_date);
    AdvReadWordLram(iop_base, ASC_MC_VERSION_NUM, asc_dvc->cfg->mcode_version);

    /*
     * Set the chip type to indicate the ASC3550.
     */
    AdvWriteWordLram(iop_base, ASC_MC_CHIP_TYPE, ADV_CHIP_ASC3550);

    /*
     * If the PCI Configuration Command Register "Parity Error Response
     * Control" Bit was clear (0), then set the microcode variable
     * 'control_flag' CONTROL_FLAG_IGNORE_PERR flag to tell the microcode
     * to ignore DMA parity errors.
     */
    if (asc_dvc->cfg->control_flag & CONTROL_FLAG_IGNORE_PERR)
    {
        AdvReadWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
        word |= CONTROL_FLAG_IGNORE_PERR;
        AdvWriteWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
    }

    /*
     * For ASC-3550, setting the START_CTL_EMFU [3:2] bits sets a FIFO
     * threshold of 128 bytes. This register is only accessible to the host.
     */
    AdvWriteByteRegister(iop_base, IOPB_DMA_CFG0,
        START_CTL_EMFU | READ_CMD_MRM);

    /*
     * Microcode operating variables for WDTR, SDTR, and command tag
     * queuing will be set in AdvInquiryHandling() based on what a
     * device reports it is capable of in Inquiry byte 7.
     *
     * If SCSI Bus Resets have been disabled, then directly set
     * SDTR and WDTR from the EEPROM configuration. This will allow
     * the BIOS and warm boot to work without a SCSI bus hang on
     * the Inquiry caused by host and target mismatched DTR values.
     * Without the SCSI Bus Reset, before an Inquiry a device can't
     * be assumed to be in Asynchronous, Narrow mode.
     */
    if ((asc_dvc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS) == 0)
    {
        AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE, asc_dvc->wdtr_able);
        AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE, asc_dvc->sdtr_able);
    }

    /*
     * Set microcode operating variables for SDTR_SPEED1, SDTR_SPEED2,
     * SDTR_SPEED3, and SDTR_SPEED4 based on the ULTRA EEPROM per TID
     * bitmask. These values determine the maximum SDTR speed negotiated
     * with a device.
     *
     * The SDTR per TID bitmask overrides the SDTR_SPEED1, SDTR_SPEED2,
     * SDTR_SPEED3, and SDTR_SPEED4 values so it is safe to set them
     * without determining here whether the device supports SDTR.
     *
     * 4-bit speed  SDTR speed name
     * ===========  ===============
     * 0000b (0x0)  SDTR disabled
     * 0001b (0x1)  5 Mhz
     * 0010b (0x2)  10 Mhz
     * 0011b (0x3)  20 Mhz (Ultra)
     * 0100b (0x4)  40 Mhz (LVD/Ultra2)
     * 0101b (0x5)  80 Mhz (LVD2/Ultra3)
     * 0110b (0x6)  Undefined
     * .
     * 1111b (0xF)  Undefined
     */
    word = 0;
    for (tid = 0; tid <= ADV_MAX_TID; tid++)
    {
        if (ADV_TID_TO_TIDMASK(tid) & asc_dvc->ultra_able)
        {
            /* Set Ultra speed for TID 'tid'. */
            word |= (0x3 << (4 * (tid % 4)));
        } else
        {
            /* Set Fast speed for TID 'tid'. */
            word |= (0x2 << (4 * (tid % 4)));
        }
        if (tid == 3) /* Check if done with sdtr_speed1. */
        {
            AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED1, word);
            word = 0;
        } else if (tid == 7) /* Check if done with sdtr_speed2. */
        {
            AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED2, word);
            word = 0;
        } else if (tid == 11) /* Check if done with sdtr_speed3. */
        {
            AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED3, word);
            word = 0;
        } else if (tid == 15) /* Check if done with sdtr_speed4. */
        {
            AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED4, word);
            /* End of loop. */
        }
    }

    /*
     * Set microcode operating variable for the disconnect per TID bitmask.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DISC_ENABLE, asc_dvc->cfg->disc_enable);

    /*
     * Set SCSI_CFG0 Microcode Default Value.
     *
     * The microcode will set the SCSI_CFG0 register using this value
     * after it is started below.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SCSI_CFG0,
        PARITY_EN | QUEUE_128 | SEL_TMO_LONG | OUR_ID_EN |
        asc_dvc->chip_scsi_id);

    /*
     * Determine SCSI_CFG1 Microcode Default Value.
     *
     * The microcode will set the SCSI_CFG1 register using this value
     * after it is started below.
     */

    /* Read current SCSI_CFG1 Register value. */
    scsi_cfg1 = AdvReadWordRegister(iop_base, IOPW_SCSI_CFG1);

    /*
     * If all three connectors are in use, return an error.
     */
    if ((scsi_cfg1 & CABLE_ILLEGAL_A) == 0 ||
        (scsi_cfg1 & CABLE_ILLEGAL_B) == 0)
    {
            asc_dvc->err_code |= ASC_IERR_ILLEGAL_CONNECTION;
            return ADV_ERROR;
    }

    /*
     * If the internal narrow cable is reversed all of the SCSI_CTRL
     * register signals will be set. Check for and return an error if
     * this condition is found.
     */
    if ((AdvReadWordRegister(iop_base, IOPW_SCSI_CTRL) & 0x3F07) == 0x3F07)
    {
        asc_dvc->err_code |= ASC_IERR_REVERSED_CABLE;
        return ADV_ERROR;
    }

    /*
     * If this is a differential board and a single-ended device
     * is attached to one of the connectors, return an error.
     */
    if ((scsi_cfg1 & DIFF_MODE) && (scsi_cfg1 & DIFF_SENSE) == 0)
    {
        asc_dvc->err_code |= ASC_IERR_SINGLE_END_DEVICE;
        return ADV_ERROR;
    }

    /*
     * If automatic termination control is enabled, then set the
     * termination value based on a table listed in a_condor.h.
     *
     * If manual termination was specified with an EEPROM setting
     * then 'termination' was set-up in AdvInitFrom3550EEPROM() and
     * is ready to be 'ored' into SCSI_CFG1.
     */
    if (asc_dvc->cfg->termination == 0)
    {
        /*
         * The software always controls termination by setting TERM_CTL_SEL.
         * If TERM_CTL_SEL were set to 0, the hardware would set termination.
         */
        asc_dvc->cfg->termination |= TERM_CTL_SEL;

        switch(scsi_cfg1 & CABLE_DETECT)
        {
            /* TERM_CTL_H: on, TERM_CTL_L: on */
            case 0x3: case 0x7: case 0xB: case 0xD: case 0xE: case 0xF:
                asc_dvc->cfg->termination |= (TERM_CTL_H | TERM_CTL_L);
                break;

            /* TERM_CTL_H: on, TERM_CTL_L: off */
            case 0x1: case 0x5: case 0x9: case 0xA: case 0xC:
                asc_dvc->cfg->termination |= TERM_CTL_H;
                break;

            /* TERM_CTL_H: off, TERM_CTL_L: off */
            case 0x2: case 0x6:
                break;
        }
    }

    /*
     * Clear any set TERM_CTL_H and TERM_CTL_L bits.
     */
    scsi_cfg1 &= ~TERM_CTL;

    /*
     * Invert the TERM_CTL_H and TERM_CTL_L bits and then
     * set 'scsi_cfg1'. The TERM_POL bit does not need to be
     * referenced, because the hardware internally inverts
     * the Termination High and Low bits if TERM_POL is set.
     */
    scsi_cfg1 |= (TERM_CTL_SEL | (~asc_dvc->cfg->termination & TERM_CTL));

    /*
     * Set SCSI_CFG1 Microcode Default Value
     *
     * Set filter value and possibly modified termination control
     * bits in the Microcode SCSI_CFG1 Register Value.
     *
     * The microcode will set the SCSI_CFG1 register using this value
     * after it is started below.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SCSI_CFG1,
        FLTR_DISABLE | scsi_cfg1);

    /*
     * Set MEM_CFG Microcode Default Value
     *
     * The microcode will set the MEM_CFG register using this value
     * after it is started below.
     *
     * MEM_CFG may be accessed as a word or byte, but only bits 0-7
     * are defined.
     *
     * ASC-3550 has 8KB internal memory.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_MEM_CFG,
        BIOS_EN | RAM_SZ_8KB);

    /*
     * Set SEL_MASK Microcode Default Value
     *
     * The microcode will set the SEL_MASK register using this value
     * after it is started below.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SEL_MASK,
        ADV_TID_TO_TIDMASK(asc_dvc->chip_scsi_id));

    /*
     * Build carrier freelist.
     *
     * Driver must have already allocated memory and set 'carrier_buf'.
     */
    ASC_ASSERT(asc_dvc->carrier_buf != NULL);

    carrp = (ADV_CARR_T *) ADV_16BALIGN(asc_dvc->carrier_buf);
    asc_dvc->carr_freelist = NULL;
    if (carrp == (ADV_CARR_T *) asc_dvc->carrier_buf)
    {
        buf_size = ADV_CARRIER_BUFSIZE;
    } else
    {
        buf_size = ADV_CARRIER_BUFSIZE - sizeof(ADV_CARR_T);
    }

    do {
        /*
         * Get physical address of the carrier 'carrp'.
         */
        contig_len = sizeof(ADV_CARR_T);
        carr_paddr = cpu_to_le32(DvcGetPhyAddr(asc_dvc, NULL, (uchar *) carrp,
            (ADV_SDCNT *) &contig_len, ADV_IS_CARRIER_FLAG));

        buf_size -= sizeof(ADV_CARR_T);

        /*
         * If the current carrier is not physically contiguous, then
         * maybe there was a page crossing. Try the next carrier aligned
         * start address.
         */
        if (contig_len < sizeof(ADV_CARR_T))
        {
            carrp++;
            continue;
        }

        carrp->carr_pa = carr_paddr;
        carrp->carr_va = cpu_to_le32(ADV_VADDR_TO_U32(carrp));

        /*
         * Insert the carrier at the beginning of the freelist.
         */
        carrp->next_vpa = cpu_to_le32(ADV_VADDR_TO_U32(asc_dvc->carr_freelist));
        asc_dvc->carr_freelist = carrp;

        carrp++;
    }
    while (buf_size > 0);

    /*
     * Set-up the Host->RISC Initiator Command Queue (ICQ).
     */

    if ((asc_dvc->icq_sp = asc_dvc->carr_freelist) == NULL)
    {
        asc_dvc->err_code |= ASC_IERR_NO_CARRIER;
        return ADV_ERROR;
    }
    asc_dvc->carr_freelist = (ADV_CARR_T *)
        ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->icq_sp->next_vpa));

    /*
     * The first command issued will be placed in the stopper carrier.
     */
    asc_dvc->icq_sp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

    /*
     * Set RISC ICQ physical address start value.
     */
    AdvWriteDWordLramNoSwap(iop_base, ASC_MC_ICQ, asc_dvc->icq_sp->carr_pa);

    /*
     * Set-up the RISC->Host Initiator Response Queue (IRQ).
     */
    if ((asc_dvc->irq_sp = asc_dvc->carr_freelist) == NULL)
    {
        asc_dvc->err_code |= ASC_IERR_NO_CARRIER;
        return ADV_ERROR;
    }
    asc_dvc->carr_freelist = (ADV_CARR_T *)
         ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->irq_sp->next_vpa));

    /*
     * The first command completed by the RISC will be placed in
     * the stopper.
     *
     * Note: Set 'next_vpa' to ASC_CQ_STOPPER. When the request is
     * completed the RISC will set the ASC_RQ_STOPPER bit.
     */
    asc_dvc->irq_sp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

    /*
     * Set RISC IRQ physical address start value.
     */
    AdvWriteDWordLramNoSwap(iop_base, ASC_MC_IRQ, asc_dvc->irq_sp->carr_pa);
    asc_dvc->carr_pending_cnt = 0;

    AdvWriteByteRegister(iop_base, IOPB_INTR_ENABLES,
        (ADV_INTR_ENABLE_HOST_INTR | ADV_INTR_ENABLE_GLOBAL_INTR));

    AdvReadWordLram(iop_base, ASC_MC_CODE_BEGIN_ADDR, word);
    AdvWriteWordRegister(iop_base, IOPW_PC, word);

    /* finally, finally, gentlemen, start your engine */
    AdvWriteWordRegister(iop_base, IOPW_RISC_CSR, ADV_RISC_CSR_RUN);

    /*
     * Reset the SCSI Bus if the EEPROM indicates that SCSI Bus
     * Resets should be performed. The RISC has to be running
     * to issue a SCSI Bus Reset.
     */
    if (asc_dvc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS)
    {
        /*
         * If the BIOS Signature is present in memory, restore the
         * BIOS Handshake Configuration Table and do not perform
         * a SCSI Bus Reset.
         */
        if (bios_mem[(ASC_MC_BIOS_SIGNATURE - ASC_MC_BIOSMEM)/2] == 0x55AA)
        {
            /*
             * Restore per TID negotiated values.
             */
            AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
            AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
            AdvWriteWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
            for (tid = 0; tid <= ADV_MAX_TID; tid++)
            {
                AdvWriteByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
                    max_cmd[tid]);
            }
        } else
        {
            if (AdvResetSB(asc_dvc) != ADV_TRUE)
            {
                warn_code = ASC_WARN_BUSRESET_ERROR;
            }
        }
    }

    return warn_code;
}

/*
 * Initialize the ASC-38C0800.
 *
 * On failure set the ADV_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 *
 * Needed after initialization for error recovery.
 */
STATIC int
AdvInitAsc38C0800Driver(ADV_DVC_VAR *asc_dvc)
{
    AdvPortAddr iop_base;
    ushort      warn_code;
    ADV_DCNT    sum;
    int         begin_addr;
    int         end_addr;
    ushort      code_sum;
    int         word;
    int         j;
    int         adv_asc38C0800_expanded_size;
    ADV_CARR_T  *carrp;
    ADV_DCNT    contig_len;
    ADV_SDCNT   buf_size;
    ADV_PADDR   carr_paddr;
    int         i;
    ushort      scsi_cfg1;
    uchar       byte;
    uchar       tid;
    ushort      bios_mem[ASC_MC_BIOSLEN/2]; /* BIOS RISC Memory 0x40-0x8F. */
    ushort      wdtr_able, sdtr_able, tagqng_able;
    uchar       max_cmd[ADV_MAX_TID + 1];

    /* If there is already an error, don't continue. */
    if (asc_dvc->err_code != 0)
    {
        return ADV_ERROR;
    }

    /*
     * The caller must set 'chip_type' to ADV_CHIP_ASC38C0800.
     */
    if (asc_dvc->chip_type != ADV_CHIP_ASC38C0800)
    {
        asc_dvc->err_code = ASC_IERR_BAD_CHIPTYPE;
        return ADV_ERROR;
    }

    warn_code = 0;
    iop_base = asc_dvc->iop_base;

    /*
     * Save the RISC memory BIOS region before writing the microcode.
     * The BIOS may already be loaded and using its RISC LRAM region
     * so its region must be saved and restored.
     *
     * Note: This code makes the assumption, which is currently true,
     * that a chip reset does not clear RISC LRAM.
     */
    for (i = 0; i < ASC_MC_BIOSLEN/2; i++)
    {
        AdvReadWordLram(iop_base, ASC_MC_BIOSMEM + (2 * i), bios_mem[i]);
    }

    /*
     * Save current per TID negotiated values.
     */
    AdvReadWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
    AdvReadWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
    AdvReadWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
    for (tid = 0; tid <= ADV_MAX_TID; tid++)
    {
        AdvReadByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
            max_cmd[tid]);
    }

    /*
     * RAM BIST (RAM Built-In Self Test)
     *
     * Address : I/O base + offset 0x38h register (byte).
     * Function: Bit 7-6(RW) : RAM mode
     *                          Normal Mode   : 0x00
     *                          Pre-test Mode : 0x40
     *                          RAM Test Mode : 0x80
     *           Bit 5       : unused
     *           Bit 4(RO)   : Done bit
     *           Bit 3-0(RO) : Status
     *                          Host Error    : 0x08
     *                          Int_RAM Error : 0x04
     *                          RISC Error    : 0x02
     *                          SCSI Error    : 0x01
     *                          No Error      : 0x00
     *
     * Note: RAM BIST code should be put right here, before loading the
     * microcode and after saving the RISC memory BIOS region.
     */

    /*
     * LRAM Pre-test
     *
     * Write PRE_TEST_MODE (0x40) to register and wait for 10 milliseconds.
     * If Done bit not set or low nibble not PRE_TEST_VALUE (0x05), return
     * an error. Reset to NORMAL_MODE (0x00) and do again. If cannot reset
     * to NORMAL_MODE, return an error too.
     */
    for (i = 0; i < 2; i++)
    {
        AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, PRE_TEST_MODE);
        DvcSleepMilliSecond(10);  /* Wait for 10ms before reading back. */
        byte = AdvReadByteRegister(iop_base, IOPB_RAM_BIST);
        if ((byte & RAM_TEST_DONE) == 0 || (byte & 0x0F) != PRE_TEST_VALUE)
        {
            asc_dvc->err_code |= ASC_IERR_BIST_PRE_TEST;
            return ADV_ERROR;
        }

        AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, NORMAL_MODE);
        DvcSleepMilliSecond(10);  /* Wait for 10ms before reading back. */
        if (AdvReadByteRegister(iop_base, IOPB_RAM_BIST)
            != NORMAL_VALUE)
        {
            asc_dvc->err_code |= ASC_IERR_BIST_PRE_TEST;
            return ADV_ERROR;
        }
    }

    /*
     * LRAM Test - It takes about 1.5 ms to run through the test.
     *
     * Write RAM_TEST_MODE (0x80) to register and wait for 10 milliseconds.
     * If Done bit not set or Status not 0, save register byte, set the
     * err_code, and return an error.
     */
    AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, RAM_TEST_MODE);
    DvcSleepMilliSecond(10);  /* Wait for 10ms before checking status. */

    byte = AdvReadByteRegister(iop_base, IOPB_RAM_BIST);
    if ((byte & RAM_TEST_DONE) == 0 || (byte & RAM_TEST_STATUS) != 0)
    {
        /* Get here if Done bit not set or Status not 0. */
        asc_dvc->bist_err_code = byte;  /* for BIOS display message */
        asc_dvc->err_code |= ASC_IERR_BIST_RAM_TEST;
        return ADV_ERROR;
    }

    /* We need to reset back to normal mode after LRAM test passes. */
    AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, NORMAL_MODE);

    /*
     * Load the Microcode
     *
     * Write the microcode image to RISC memory starting at address 0.
     *
     */
    AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, 0);

    /* Assume the following compressed format of the microcode buffer:
     *
     *  254 word (508 byte) table indexed by byte code followed
     *  by the following byte codes:
     *
     *    1-Byte Code:
     *      00: Emit word 0 in table.
     *      01: Emit word 1 in table.
     *      .
     *      FD: Emit word 253 in table.
     *
     *    Multi-Byte Code:
     *      FE WW WW: (3 byte code) Word to emit is the next word WW WW.
     *      FF BB WW WW: (4 byte code) Emit BB count times next word WW WW.
     */
    word = 0;
    for (i = 253 * 2; i < _adv_asc38C0800_size; i++)
    {
        if (_adv_asc38C0800_buf[i] == 0xff)
        {
            for (j = 0; j < _adv_asc38C0800_buf[i + 1]; j++)
            {
                AdvWriteWordAutoIncLram(iop_base, (((ushort)
                    _adv_asc38C0800_buf[i + 3] << 8) |
                    _adv_asc38C0800_buf[i + 2]));
                word++;
            }
            i += 3;
        } else if (_adv_asc38C0800_buf[i] == 0xfe)
        {
            AdvWriteWordAutoIncLram(iop_base, (((ushort)
                _adv_asc38C0800_buf[i + 2] << 8) |
                _adv_asc38C0800_buf[i + 1]));
            i += 2;
            word++;
        } else
        {
            AdvWriteWordAutoIncLram(iop_base, (((ushort)
                _adv_asc38C0800_buf[(_adv_asc38C0800_buf[i] * 2) + 1] << 8) |
                _adv_asc38C0800_buf[_adv_asc38C0800_buf[i] * 2]));
            word++;
        }
    }

    /*
     * Set 'word' for later use to clear the rest of memory and save
     * the expanded mcode size.
     */
    word *= 2;
    adv_asc38C0800_expanded_size = word;

    /*
     * Clear the rest of ASC-38C0800 Internal RAM (16KB).
     */
    for (; word < ADV_38C0800_MEMSIZE; word += 2)
    {
        AdvWriteWordAutoIncLram(iop_base, 0);
    }

    /*
     * Verify the microcode checksum.
     */
    sum = 0;
    AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, 0);

    for (word = 0; word < adv_asc38C0800_expanded_size; word += 2)
    {
        sum += AdvReadWordAutoIncLram(iop_base);
    }
    ASC_DBG2(1, "AdvInitAsc38C0800Driver: word %d, i %d\n", word, i);

    ASC_DBG2(1,
        "AdvInitAsc38C0800Driver: sum 0x%lx, _adv_asc38C0800_chksum 0x%lx\n",
        (ulong) sum, (ulong) _adv_asc38C0800_chksum);

    if (sum != _adv_asc38C0800_chksum)
    {
        asc_dvc->err_code |= ASC_IERR_MCODE_CHKSUM;
        return ADV_ERROR;
    }

    /*
     * Restore the RISC memory BIOS region.
     */
    for (i = 0; i < ASC_MC_BIOSLEN/2; i++)
    {
        AdvWriteWordLram(iop_base, ASC_MC_BIOSMEM + (2 * i), bios_mem[i]);
    }

    /*
     * Calculate and write the microcode code checksum to the microcode
     * code checksum location ASC_MC_CODE_CHK_SUM (0x2C).
     */
    AdvReadWordLram(iop_base, ASC_MC_CODE_BEGIN_ADDR, begin_addr);
    AdvReadWordLram(iop_base, ASC_MC_CODE_END_ADDR, end_addr);
    code_sum = 0;
    AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, begin_addr);
    for (word = begin_addr; word < end_addr; word += 2)
    {
        code_sum += AdvReadWordAutoIncLram(iop_base);
    }
    AdvWriteWordLram(iop_base, ASC_MC_CODE_CHK_SUM, code_sum);

    /*
     * Read microcode version and date.
     */
    AdvReadWordLram(iop_base, ASC_MC_VERSION_DATE, asc_dvc->cfg->mcode_date);
    AdvReadWordLram(iop_base, ASC_MC_VERSION_NUM, asc_dvc->cfg->mcode_version);

    /*
     * Set the chip type to indicate the ASC38C0800.
     */
    AdvWriteWordLram(iop_base, ASC_MC_CHIP_TYPE, ADV_CHIP_ASC38C0800);

    /*
     * Write 1 to bit 14 'DIS_TERM_DRV' in the SCSI_CFG1 register.
     * When DIS_TERM_DRV set to 1, C_DET[3:0] will reflect current
     * cable detection and then we are able to read C_DET[3:0].
     *
     * Note: We will reset DIS_TERM_DRV to 0 in the 'Set SCSI_CFG1
     * Microcode Default Value' section below.
     */
    scsi_cfg1 = AdvReadWordRegister(iop_base, IOPW_SCSI_CFG1);
    AdvWriteWordRegister(iop_base, IOPW_SCSI_CFG1, scsi_cfg1 | DIS_TERM_DRV);

    /*
     * If the PCI Configuration Command Register "Parity Error Response
     * Control" Bit was clear (0), then set the microcode variable
     * 'control_flag' CONTROL_FLAG_IGNORE_PERR flag to tell the microcode
     * to ignore DMA parity errors.
     */
    if (asc_dvc->cfg->control_flag & CONTROL_FLAG_IGNORE_PERR)
    {
        AdvReadWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
        word |= CONTROL_FLAG_IGNORE_PERR;
        AdvWriteWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
    }

    /*
     * For ASC-38C0800, set FIFO_THRESH_80B [6:4] bits and START_CTL_TH [3:2]
     * bits for the default FIFO threshold.
     *
     * Note: ASC-38C0800 FIFO threshold has been changed to 256 bytes.
     *
     * For DMA Errata #4 set the BC_THRESH_ENB bit.
     */
    AdvWriteByteRegister(iop_base, IOPB_DMA_CFG0,
        BC_THRESH_ENB | FIFO_THRESH_80B | START_CTL_TH | READ_CMD_MRM);

    /*
     * Microcode operating variables for WDTR, SDTR, and command tag
     * queuing will be set in AdvInquiryHandling() based on what a
     * device reports it is capable of in Inquiry byte 7.
     *
     * If SCSI Bus Resets have been disabled, then directly set
     * SDTR and WDTR from the EEPROM configuration. This will allow
     * the BIOS and warm boot to work without a SCSI bus hang on
     * the Inquiry caused by host and target mismatched DTR values.
     * Without the SCSI Bus Reset, before an Inquiry a device can't
     * be assumed to be in Asynchronous, Narrow mode.
     */
    if ((asc_dvc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS) == 0)
    {
        AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE, asc_dvc->wdtr_able);
        AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE, asc_dvc->sdtr_able);
    }

    /*
     * Set microcode operating variables for DISC and SDTR_SPEED1,
     * SDTR_SPEED2, SDTR_SPEED3, and SDTR_SPEED4 based on the EEPROM
     * configuration values.
     *
     * The SDTR per TID bitmask overrides the SDTR_SPEED1, SDTR_SPEED2,
     * SDTR_SPEED3, and SDTR_SPEED4 values so it is safe to set them
     * without determining here whether the device supports SDTR.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DISC_ENABLE, asc_dvc->cfg->disc_enable);
    AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED1, asc_dvc->sdtr_speed1);
    AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED2, asc_dvc->sdtr_speed2);
    AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED3, asc_dvc->sdtr_speed3);
    AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED4, asc_dvc->sdtr_speed4);

    /*
     * Set SCSI_CFG0 Microcode Default Value.
     *
     * The microcode will set the SCSI_CFG0 register using this value
     * after it is started below.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SCSI_CFG0,
        PARITY_EN | QUEUE_128 | SEL_TMO_LONG | OUR_ID_EN |
        asc_dvc->chip_scsi_id);

    /*
     * Determine SCSI_CFG1 Microcode Default Value.
     *
     * The microcode will set the SCSI_CFG1 register using this value
     * after it is started below.
     */

    /* Read current SCSI_CFG1 Register value. */
    scsi_cfg1 = AdvReadWordRegister(iop_base, IOPW_SCSI_CFG1);

    /*
     * If the internal narrow cable is reversed all of the SCSI_CTRL
     * register signals will be set. Check for and return an error if
     * this condition is found.
     */
    if ((AdvReadWordRegister(iop_base, IOPW_SCSI_CTRL) & 0x3F07) == 0x3F07)
    {
        asc_dvc->err_code |= ASC_IERR_REVERSED_CABLE;
        return ADV_ERROR;
    }

    /*
     * All kind of combinations of devices attached to one of four connectors
     * are acceptable except HVD device attached. For example, LVD device can
     * be attached to SE connector while SE device attached to LVD connector.
     * If LVD device attached to SE connector, it only runs up to Ultra speed.
     *
     * If an HVD device is attached to one of LVD connectors, return an error.
     * However, there is no way to detect HVD device attached to SE connectors.
     */
    if (scsi_cfg1 & HVD)
    {
        asc_dvc->err_code |= ASC_IERR_HVD_DEVICE;
        return ADV_ERROR;
    }

    /*
     * If either SE or LVD automatic termination control is enabled, then
     * set the termination value based on a table listed in a_condor.h.
     *
     * If manual termination was specified with an EEPROM setting then
     * 'termination' was set-up in AdvInitFrom38C0800EEPROM() and is ready to
     * be 'ored' into SCSI_CFG1.
     */
    if ((asc_dvc->cfg->termination & TERM_SE) == 0)
    {
        /* SE automatic termination control is enabled. */
        switch(scsi_cfg1 & C_DET_SE)
        {
            /* TERM_SE_HI: on, TERM_SE_LO: on */
            case 0x1: case 0x2: case 0x3:
                asc_dvc->cfg->termination |= TERM_SE;
                break;

            /* TERM_SE_HI: on, TERM_SE_LO: off */
            case 0x0:
                asc_dvc->cfg->termination |= TERM_SE_HI;
                break;
        }
    }

    if ((asc_dvc->cfg->termination & TERM_LVD) == 0)
    {
        /* LVD automatic termination control is enabled. */
        switch(scsi_cfg1 & C_DET_LVD)
        {
            /* TERM_LVD_HI: on, TERM_LVD_LO: on */
            case 0x4: case 0x8: case 0xC:
                asc_dvc->cfg->termination |= TERM_LVD;
                break;

            /* TERM_LVD_HI: off, TERM_LVD_LO: off */
            case 0x0:
                break;
        }
    }

    /*
     * Clear any set TERM_SE and TERM_LVD bits.
     */
    scsi_cfg1 &= (~TERM_SE & ~TERM_LVD);

    /*
     * Invert the TERM_SE and TERM_LVD bits and then set 'scsi_cfg1'.
     */
    scsi_cfg1 |= (~asc_dvc->cfg->termination & 0xF0);

    /*
     * Clear BIG_ENDIAN, DIS_TERM_DRV, Terminator Polarity and HVD/LVD/SE bits
     * and set possibly modified termination control bits in the Microcode
     * SCSI_CFG1 Register Value.
     */
    scsi_cfg1 &= (~BIG_ENDIAN & ~DIS_TERM_DRV & ~TERM_POL & ~HVD_LVD_SE);

    /*
     * Set SCSI_CFG1 Microcode Default Value
     *
     * Set possibly modified termination control and reset DIS_TERM_DRV
     * bits in the Microcode SCSI_CFG1 Register Value.
     *
     * The microcode will set the SCSI_CFG1 register using this value
     * after it is started below.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SCSI_CFG1, scsi_cfg1);

    /*
     * Set MEM_CFG Microcode Default Value
     *
     * The microcode will set the MEM_CFG register using this value
     * after it is started below.
     *
     * MEM_CFG may be accessed as a word or byte, but only bits 0-7
     * are defined.
     *
     * ASC-38C0800 has 16KB internal memory.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_MEM_CFG,
        BIOS_EN | RAM_SZ_16KB);

    /*
     * Set SEL_MASK Microcode Default Value
     *
     * The microcode will set the SEL_MASK register using this value
     * after it is started below.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SEL_MASK,
        ADV_TID_TO_TIDMASK(asc_dvc->chip_scsi_id));

    /*
     * Build the carrier freelist.
     *
     * Driver must have already allocated memory and set 'carrier_buf'.
     */
    ASC_ASSERT(asc_dvc->carrier_buf != NULL);

    carrp = (ADV_CARR_T *) ADV_16BALIGN(asc_dvc->carrier_buf);
    asc_dvc->carr_freelist = NULL;
    if (carrp == (ADV_CARR_T *) asc_dvc->carrier_buf)
    {
        buf_size = ADV_CARRIER_BUFSIZE;
    } else
    {
        buf_size = ADV_CARRIER_BUFSIZE - sizeof(ADV_CARR_T);
    }

    do {
        /*
         * Get physical address for the carrier 'carrp'.
         */
        contig_len = sizeof(ADV_CARR_T);
        carr_paddr = cpu_to_le32(DvcGetPhyAddr(asc_dvc, NULL, (uchar *) carrp,
            (ADV_SDCNT *) &contig_len, ADV_IS_CARRIER_FLAG));

        buf_size -= sizeof(ADV_CARR_T);

        /*
         * If the current carrier is not physically contiguous, then
         * maybe there was a page crossing. Try the next carrier aligned
         * start address.
         */
        if (contig_len < sizeof(ADV_CARR_T))
        {
            carrp++;
            continue;
        }

        carrp->carr_pa = carr_paddr;
        carrp->carr_va = cpu_to_le32(ADV_VADDR_TO_U32(carrp));

        /*
         * Insert the carrier at the beginning of the freelist.
         */
        carrp->next_vpa = cpu_to_le32(ADV_VADDR_TO_U32(asc_dvc->carr_freelist));
        asc_dvc->carr_freelist = carrp;

        carrp++;
    }
    while (buf_size > 0);

    /*
     * Set-up the Host->RISC Initiator Command Queue (ICQ).
     */

    if ((asc_dvc->icq_sp = asc_dvc->carr_freelist) == NULL)
    {
        asc_dvc->err_code |= ASC_IERR_NO_CARRIER;
        return ADV_ERROR;
    }
    asc_dvc->carr_freelist = (ADV_CARR_T *)
        ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->icq_sp->next_vpa));

    /*
     * The first command issued will be placed in the stopper carrier.
     */
    asc_dvc->icq_sp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

    /*
     * Set RISC ICQ physical address start value.
     * carr_pa is LE, must be native before write
     */
    AdvWriteDWordLramNoSwap(iop_base, ASC_MC_ICQ, asc_dvc->icq_sp->carr_pa);

    /*
     * Set-up the RISC->Host Initiator Response Queue (IRQ).
     */
    if ((asc_dvc->irq_sp = asc_dvc->carr_freelist) == NULL)
    {
        asc_dvc->err_code |= ASC_IERR_NO_CARRIER;
        return ADV_ERROR;
    }
    asc_dvc->carr_freelist = (ADV_CARR_T *)
        ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->irq_sp->next_vpa));

    /*
     * The first command completed by the RISC will be placed in
     * the stopper.
     *
     * Note: Set 'next_vpa' to ASC_CQ_STOPPER. When the request is
     * completed the RISC will set the ASC_RQ_STOPPER bit.
     */
    asc_dvc->irq_sp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

    /*
     * Set RISC IRQ physical address start value.
     *
     * carr_pa is LE, must be native before write *
     */
    AdvWriteDWordLramNoSwap(iop_base, ASC_MC_IRQ, asc_dvc->irq_sp->carr_pa);
    asc_dvc->carr_pending_cnt = 0;

    AdvWriteByteRegister(iop_base, IOPB_INTR_ENABLES,
        (ADV_INTR_ENABLE_HOST_INTR | ADV_INTR_ENABLE_GLOBAL_INTR));

    AdvReadWordLram(iop_base, ASC_MC_CODE_BEGIN_ADDR, word);
    AdvWriteWordRegister(iop_base, IOPW_PC, word);

    /* finally, finally, gentlemen, start your engine */
    AdvWriteWordRegister(iop_base, IOPW_RISC_CSR, ADV_RISC_CSR_RUN);

    /*
     * Reset the SCSI Bus if the EEPROM indicates that SCSI Bus
     * Resets should be performed. The RISC has to be running
     * to issue a SCSI Bus Reset.
     */
    if (asc_dvc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS)
    {
        /*
         * If the BIOS Signature is present in memory, restore the
         * BIOS Handshake Configuration Table and do not perform
         * a SCSI Bus Reset.
         */
        if (bios_mem[(ASC_MC_BIOS_SIGNATURE - ASC_MC_BIOSMEM)/2] == 0x55AA)
        {
            /*
             * Restore per TID negotiated values.
             */
            AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
            AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
            AdvWriteWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
            for (tid = 0; tid <= ADV_MAX_TID; tid++)
            {
                AdvWriteByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
                    max_cmd[tid]);
            }
        } else
        {
            if (AdvResetSB(asc_dvc) != ADV_TRUE)
            {
                warn_code = ASC_WARN_BUSRESET_ERROR;
            }
        }
    }

    return warn_code;
}

/*
 * Initialize the ASC-38C1600.
 *
 * On failure set the ASC_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 *
 * Needed after initialization for error recovery.
 */
STATIC int
AdvInitAsc38C1600Driver(ADV_DVC_VAR *asc_dvc)
{
    AdvPortAddr iop_base;
    ushort      warn_code;
    ADV_DCNT    sum;
    int         begin_addr;
    int         end_addr;
    ushort      code_sum;
    long        word;
    int         j;
    int         adv_asc38C1600_expanded_size;
    ADV_CARR_T  *carrp;
    ADV_DCNT    contig_len;
    ADV_SDCNT   buf_size;
    ADV_PADDR   carr_paddr;
    int         i;
    ushort      scsi_cfg1;
    uchar       byte;
    uchar       tid;
    ushort      bios_mem[ASC_MC_BIOSLEN/2]; /* BIOS RISC Memory 0x40-0x8F. */
    ushort      wdtr_able, sdtr_able, ppr_able, tagqng_able;
    uchar       max_cmd[ASC_MAX_TID + 1];

    /* If there is already an error, don't continue. */
    if (asc_dvc->err_code != 0)
    {
        return ADV_ERROR;
    }

    /*
     * The caller must set 'chip_type' to ADV_CHIP_ASC38C1600.
     */
    if (asc_dvc->chip_type != ADV_CHIP_ASC38C1600)
    {
        asc_dvc->err_code = ASC_IERR_BAD_CHIPTYPE;
        return ADV_ERROR;
    }

    warn_code = 0;
    iop_base = asc_dvc->iop_base;

    /*
     * Save the RISC memory BIOS region before writing the microcode.
     * The BIOS may already be loaded and using its RISC LRAM region
     * so its region must be saved and restored.
     *
     * Note: This code makes the assumption, which is currently true,
     * that a chip reset does not clear RISC LRAM.
     */
    for (i = 0; i < ASC_MC_BIOSLEN/2; i++)
    {
        AdvReadWordLram(iop_base, ASC_MC_BIOSMEM + (2 * i), bios_mem[i]);
    }

    /*
     * Save current per TID negotiated values.
     */
    AdvReadWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
    AdvReadWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
    AdvReadWordLram(iop_base, ASC_MC_PPR_ABLE, ppr_able);
    AdvReadWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
    for (tid = 0; tid <= ASC_MAX_TID; tid++)
    {
        AdvReadByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
            max_cmd[tid]);
    }

    /*
     * RAM BIST (Built-In Self Test)
     *
     * Address : I/O base + offset 0x38h register (byte).
     * Function: Bit 7-6(RW) : RAM mode
     *                          Normal Mode   : 0x00
     *                          Pre-test Mode : 0x40
     *                          RAM Test Mode : 0x80
     *           Bit 5       : unused
     *           Bit 4(RO)   : Done bit
     *           Bit 3-0(RO) : Status
     *                          Host Error    : 0x08
     *                          Int_RAM Error : 0x04
     *                          RISC Error    : 0x02
     *                          SCSI Error    : 0x01
     *                          No Error      : 0x00
     *
     * Note: RAM BIST code should be put right here, before loading the
     * microcode and after saving the RISC memory BIOS region.
     */

    /*
     * LRAM Pre-test
     *
     * Write PRE_TEST_MODE (0x40) to register and wait for 10 milliseconds.
     * If Done bit not set or low nibble not PRE_TEST_VALUE (0x05), return
     * an error. Reset to NORMAL_MODE (0x00) and do again. If cannot reset
     * to NORMAL_MODE, return an error too.
     */
    for (i = 0; i < 2; i++)
    {
        AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, PRE_TEST_MODE);
        DvcSleepMilliSecond(10);  /* Wait for 10ms before reading back. */
        byte = AdvReadByteRegister(iop_base, IOPB_RAM_BIST);
        if ((byte & RAM_TEST_DONE) == 0 || (byte & 0x0F) != PRE_TEST_VALUE)
        {
            asc_dvc->err_code |= ASC_IERR_BIST_PRE_TEST;
            return ADV_ERROR;
        }

        AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, NORMAL_MODE);
        DvcSleepMilliSecond(10);  /* Wait for 10ms before reading back. */
        if (AdvReadByteRegister(iop_base, IOPB_RAM_BIST)
            != NORMAL_VALUE)
        {
            asc_dvc->err_code |= ASC_IERR_BIST_PRE_TEST;
            return ADV_ERROR;
        }
    }

    /*
     * LRAM Test - It takes about 1.5 ms to run through the test.
     *
     * Write RAM_TEST_MODE (0x80) to register and wait for 10 milliseconds.
     * If Done bit not set or Status not 0, save register byte, set the
     * err_code, and return an error.
     */
    AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, RAM_TEST_MODE);
    DvcSleepMilliSecond(10);  /* Wait for 10ms before checking status. */

    byte = AdvReadByteRegister(iop_base, IOPB_RAM_BIST);
    if ((byte & RAM_TEST_DONE) == 0 || (byte & RAM_TEST_STATUS) != 0)
    {
        /* Get here if Done bit not set or Status not 0. */
        asc_dvc->bist_err_code = byte;  /* for BIOS display message */
        asc_dvc->err_code |= ASC_IERR_BIST_RAM_TEST;
        return ADV_ERROR;
    }

    /* We need to reset back to normal mode after LRAM test passes. */
    AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, NORMAL_MODE);

    /*
     * Load the Microcode
     *
     * Write the microcode image to RISC memory starting at address 0.
     *
     */
    AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, 0);

    /*
     * Assume the following compressed format of the microcode buffer:
     *
     *  254 word (508 byte) table indexed by byte code followed
     *  by the following byte codes:
     *
     *    1-Byte Code:
     *      00: Emit word 0 in table.
     *      01: Emit word 1 in table.
     *      .
     *      FD: Emit word 253 in table.
     *
     *    Multi-Byte Code:
     *      FE WW WW: (3 byte code) Word to emit is the next word WW WW.
     *      FF BB WW WW: (4 byte code) Emit BB count times next word WW WW.
     */
    word = 0;
    for (i = 253 * 2; i < _adv_asc38C1600_size; i++)
    {
        if (_adv_asc38C1600_buf[i] == 0xff)
        {
            for (j = 0; j < _adv_asc38C1600_buf[i + 1]; j++)
            {
                AdvWriteWordAutoIncLram(iop_base, (((ushort)
                     _adv_asc38C1600_buf[i + 3] << 8) |
                     _adv_asc38C1600_buf[i + 2]));
                word++;
            }
           i += 3;
        } else if (_adv_asc38C1600_buf[i] == 0xfe)
        {
                AdvWriteWordAutoIncLram(iop_base, (((ushort)
                     _adv_asc38C1600_buf[i + 2] << 8) |
                     _adv_asc38C1600_buf[i + 1]));
            i += 2;
            word++;
        } else
        {
            AdvWriteWordAutoIncLram(iop_base, (((ushort)
                 _adv_asc38C1600_buf[(_adv_asc38C1600_buf[i] * 2) + 1] << 8) |
                 _adv_asc38C1600_buf[_adv_asc38C1600_buf[i] * 2]));
            word++;
        }
    }

    /*
     * Set 'word' for later use to clear the rest of memory and save
     * the expanded mcode size.
     */
    word *= 2;
    adv_asc38C1600_expanded_size = word;

    /*
     * Clear the rest of ASC-38C1600 Internal RAM (32KB).
     */
    for (; word < ADV_38C1600_MEMSIZE; word += 2)
    {
        AdvWriteWordAutoIncLram(iop_base, 0);
    }

    /*
     * Verify the microcode checksum.
     */
    sum = 0;
    AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, 0);

    for (word = 0; word < adv_asc38C1600_expanded_size; word += 2)
    {
        sum += AdvReadWordAutoIncLram(iop_base);
    }

    if (sum != _adv_asc38C1600_chksum)
    {
        asc_dvc->err_code |= ASC_IERR_MCODE_CHKSUM;
        return ADV_ERROR;
    }

    /*
     * Restore the RISC memory BIOS region.
     */
    for (i = 0; i < ASC_MC_BIOSLEN/2; i++)
    {
        AdvWriteWordLram(iop_base, ASC_MC_BIOSMEM + (2 * i), bios_mem[i]);
    }

    /*
     * Calculate and write the microcode code checksum to the microcode
     * code checksum location ASC_MC_CODE_CHK_SUM (0x2C).
     */
    AdvReadWordLram(iop_base, ASC_MC_CODE_BEGIN_ADDR, begin_addr);
    AdvReadWordLram(iop_base, ASC_MC_CODE_END_ADDR, end_addr);
    code_sum = 0;
    AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, begin_addr);
    for (word = begin_addr; word < end_addr; word += 2)
    {
        code_sum += AdvReadWordAutoIncLram(iop_base);
    }
    AdvWriteWordLram(iop_base, ASC_MC_CODE_CHK_SUM, code_sum);

    /*
     * Read microcode version and date.
     */
    AdvReadWordLram(iop_base, ASC_MC_VERSION_DATE, asc_dvc->cfg->mcode_date);
    AdvReadWordLram(iop_base, ASC_MC_VERSION_NUM, asc_dvc->cfg->mcode_version);

    /*
     * Set the chip type to indicate the ASC38C1600.
     */
    AdvWriteWordLram(iop_base, ASC_MC_CHIP_TYPE, ADV_CHIP_ASC38C1600);

    /*
     * Write 1 to bit 14 'DIS_TERM_DRV' in the SCSI_CFG1 register.
     * When DIS_TERM_DRV set to 1, C_DET[3:0] will reflect current
     * cable detection and then we are able to read C_DET[3:0].
     *
     * Note: We will reset DIS_TERM_DRV to 0 in the 'Set SCSI_CFG1
     * Microcode Default Value' section below.
     */
    scsi_cfg1 = AdvReadWordRegister(iop_base, IOPW_SCSI_CFG1);
    AdvWriteWordRegister(iop_base, IOPW_SCSI_CFG1, scsi_cfg1 | DIS_TERM_DRV);

    /*
     * If the PCI Configuration Command Register "Parity Error Response
     * Control" Bit was clear (0), then set the microcode variable
     * 'control_flag' CONTROL_FLAG_IGNORE_PERR flag to tell the microcode
     * to ignore DMA parity errors.
     */
    if (asc_dvc->cfg->control_flag & CONTROL_FLAG_IGNORE_PERR)
    {
        AdvReadWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
        word |= CONTROL_FLAG_IGNORE_PERR;
        AdvWriteWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
    }

    /*
     * If the BIOS control flag AIPP (Asynchronous Information
     * Phase Protection) disable bit is not set, then set the firmware
     * 'control_flag' CONTROL_FLAG_ENABLE_AIPP bit to enable
     * AIPP checking and encoding.
     */
    if ((asc_dvc->bios_ctrl & BIOS_CTRL_AIPP_DIS) == 0)
    {
        AdvReadWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
        word |= CONTROL_FLAG_ENABLE_AIPP;
        AdvWriteWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
    }

    /*
     * For ASC-38C1600 use DMA_CFG0 default values: FIFO_THRESH_80B [6:4],
     * and START_CTL_TH [3:2].
     */
    AdvWriteByteRegister(iop_base, IOPB_DMA_CFG0,
        FIFO_THRESH_80B | START_CTL_TH | READ_CMD_MRM);

    /*
     * Microcode operating variables for WDTR, SDTR, and command tag
     * queuing will be set in AdvInquiryHandling() based on what a
     * device reports it is capable of in Inquiry byte 7.
     *
     * If SCSI Bus Resets have been disabled, then directly set
     * SDTR and WDTR from the EEPROM configuration. This will allow
     * the BIOS and warm boot to work without a SCSI bus hang on
     * the Inquiry caused by host and target mismatched DTR values.
     * Without the SCSI Bus Reset, before an Inquiry a device can't
     * be assumed to be in Asynchronous, Narrow mode.
     */
    if ((asc_dvc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS) == 0)
    {
        AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE, asc_dvc->wdtr_able);
        AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE, asc_dvc->sdtr_able);
    }

    /*
     * Set microcode operating variables for DISC and SDTR_SPEED1,
     * SDTR_SPEED2, SDTR_SPEED3, and SDTR_SPEED4 based on the EEPROM
     * configuration values.
     *
     * The SDTR per TID bitmask overrides the SDTR_SPEED1, SDTR_SPEED2,
     * SDTR_SPEED3, and SDTR_SPEED4 values so it is safe to set them
     * without determining here whether the device supports SDTR.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DISC_ENABLE, asc_dvc->cfg->disc_enable);
    AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED1, asc_dvc->sdtr_speed1);
    AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED2, asc_dvc->sdtr_speed2);
    AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED3, asc_dvc->sdtr_speed3);
    AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED4, asc_dvc->sdtr_speed4);

    /*
     * Set SCSI_CFG0 Microcode Default Value.
     *
     * The microcode will set the SCSI_CFG0 register using this value
     * after it is started below.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SCSI_CFG0,
        PARITY_EN | QUEUE_128 | SEL_TMO_LONG | OUR_ID_EN |
        asc_dvc->chip_scsi_id);

    /*
     * Calculate SCSI_CFG1 Microcode Default Value.
     *
     * The microcode will set the SCSI_CFG1 register using this value
     * after it is started below.
     *
     * Each ASC-38C1600 function has only two cable detect bits.
     * The bus mode override bits are in IOPB_SOFT_OVER_WR.
     */
    scsi_cfg1 = AdvReadWordRegister(iop_base, IOPW_SCSI_CFG1);

    /*
     * If the cable is reversed all of the SCSI_CTRL register signals
     * will be set. Check for and return an error if this condition is
     * found.
     */
    if ((AdvReadWordRegister(iop_base, IOPW_SCSI_CTRL) & 0x3F07) == 0x3F07)
    {
        asc_dvc->err_code |= ASC_IERR_REVERSED_CABLE;
        return ADV_ERROR;
    }

    /*
     * Each ASC-38C1600 function has two connectors. Only an HVD device
     * can not be connected to either connector. An LVD device or SE device
     * may be connected to either connecor. If an SE device is connected,
     * then at most Ultra speed (20 Mhz) can be used on both connectors.
     *
     * If an HVD device is attached, return an error.
     */
    if (scsi_cfg1 & HVD)
    {
        asc_dvc->err_code |= ASC_IERR_HVD_DEVICE;
        return ADV_ERROR;
    }

    /*
     * Each function in the ASC-38C1600 uses only the SE cable detect and
     * termination because there are two connectors for each function. Each
     * function may use either LVD or SE mode. Corresponding the SE automatic
     * termination control EEPROM bits are used for each function. Each
     * function has its own EEPROM. If SE automatic control is enabled for
     * the function, then set the termination value based on a table listed
     * in a_condor.h.
     *
     * If manual termination is specified in the EEPROM for the function,
     * then 'termination' was set-up in AscInitFrom38C1600EEPROM() and is
     * ready to be 'ored' into SCSI_CFG1.
     */
    if ((asc_dvc->cfg->termination & TERM_SE) == 0)
    {
        /* SE automatic termination control is enabled. */
        switch(scsi_cfg1 & C_DET_SE)
        {
            /* TERM_SE_HI: on, TERM_SE_LO: on */
            case 0x1: case 0x2: case 0x3:
                asc_dvc->cfg->termination |= TERM_SE;
                break;

            case 0x0:
                if (ASC_PCI_ID2FUNC(asc_dvc->cfg->pci_slot_info) == 0)
                {
                    /* Function 0 - TERM_SE_HI: off, TERM_SE_LO: off */
                }
                else
                {
                    /* Function 1 - TERM_SE_HI: on, TERM_SE_LO: off */
                    asc_dvc->cfg->termination |= TERM_SE_HI;
                }
                break;
        }
    }

    /*
     * Clear any set TERM_SE bits.
     */
    scsi_cfg1 &= ~TERM_SE;

    /*
     * Invert the TERM_SE bits and then set 'scsi_cfg1'.
     */
    scsi_cfg1 |= (~asc_dvc->cfg->termination & TERM_SE);

    /*
     * Clear Big Endian and Terminator Polarity bits and set possibly
     * modified termination control bits in the Microcode SCSI_CFG1
     * Register Value.
     *
     * Big Endian bit is not used even on big endian machines.
     */
    scsi_cfg1 &= (~BIG_ENDIAN & ~DIS_TERM_DRV & ~TERM_POL);

    /*
     * Set SCSI_CFG1 Microcode Default Value
     *
     * Set possibly modified termination control bits in the Microcode
     * SCSI_CFG1 Register Value.
     *
     * The microcode will set the SCSI_CFG1 register using this value
     * after it is started below.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SCSI_CFG1, scsi_cfg1);

    /*
     * Set MEM_CFG Microcode Default Value
     *
     * The microcode will set the MEM_CFG register using this value
     * after it is started below.
     *
     * MEM_CFG may be accessed as a word or byte, but only bits 0-7
     * are defined.
     *
     * ASC-38C1600 has 32KB internal memory.
     *
     * XXX - Since ASC38C1600 Rev.3 has a Local RAM failure issue, we come
     * out a special 16K Adv Library and Microcode version. After the issue
     * resolved, we should turn back to the 32K support. Both a_condor.h and
     * mcode.sas files also need to be updated.
     *
     * AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_MEM_CFG,
     *  BIOS_EN | RAM_SZ_32KB);
     */
     AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_MEM_CFG, BIOS_EN | RAM_SZ_16KB);

    /*
     * Set SEL_MASK Microcode Default Value
     *
     * The microcode will set the SEL_MASK register using this value
     * after it is started below.
     */
    AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SEL_MASK,
        ADV_TID_TO_TIDMASK(asc_dvc->chip_scsi_id));

    /*
     * Build the carrier freelist.
     *
     * Driver must have already allocated memory and set 'carrier_buf'.
     */

    ASC_ASSERT(asc_dvc->carrier_buf != NULL);

    carrp = (ADV_CARR_T *) ADV_16BALIGN(asc_dvc->carrier_buf);
    asc_dvc->carr_freelist = NULL;
    if (carrp == (ADV_CARR_T *) asc_dvc->carrier_buf)
    {
        buf_size = ADV_CARRIER_BUFSIZE;
    } else
    {
        buf_size = ADV_CARRIER_BUFSIZE - sizeof(ADV_CARR_T);
    }

    do {
        /*
         * Get physical address for the carrier 'carrp'.
         */
        contig_len = sizeof(ADV_CARR_T);
        carr_paddr = cpu_to_le32(DvcGetPhyAddr(asc_dvc, NULL, (uchar *) carrp,
            (ADV_SDCNT *) &contig_len, ADV_IS_CARRIER_FLAG));

        buf_size -= sizeof(ADV_CARR_T);

        /*
         * If the current carrier is not physically contiguous, then
         * maybe there was a page crossing. Try the next carrier aligned
         * start address.
         */
        if (contig_len < sizeof(ADV_CARR_T))
        {
            carrp++;
            continue;
        }

        carrp->carr_pa = carr_paddr;
        carrp->carr_va = cpu_to_le32(ADV_VADDR_TO_U32(carrp));

        /*
         * Insert the carrier at the beginning of the freelist.
         */
        carrp->next_vpa = cpu_to_le32(ADV_VADDR_TO_U32(asc_dvc->carr_freelist));
        asc_dvc->carr_freelist = carrp;

        carrp++;
    }
    while (buf_size > 0);

    /*
     * Set-up the Host->RISC Initiator Command Queue (ICQ).
     */
    if ((asc_dvc->icq_sp = asc_dvc->carr_freelist) == NULL)
    {
        asc_dvc->err_code |= ASC_IERR_NO_CARRIER;
        return ADV_ERROR;
    }
    asc_dvc->carr_freelist = (ADV_CARR_T *)
        ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->icq_sp->next_vpa));

    /*
     * The first command issued will be placed in the stopper carrier.
     */
    asc_dvc->icq_sp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

    /*
     * Set RISC ICQ physical address start value. Initialize the
     * COMMA register to the same value otherwise the RISC will
     * prematurely detect a command is available.
     */
    AdvWriteDWordLramNoSwap(iop_base, ASC_MC_ICQ, asc_dvc->icq_sp->carr_pa);
    AdvWriteDWordRegister(iop_base, IOPDW_COMMA,
        le32_to_cpu(asc_dvc->icq_sp->carr_pa));

    /*
     * Set-up the RISC->Host Initiator Response Queue (IRQ).
     */
    if ((asc_dvc->irq_sp = asc_dvc->carr_freelist) == NULL)
    {
        asc_dvc->err_code |= ASC_IERR_NO_CARRIER;
        return ADV_ERROR;
    }
    asc_dvc->carr_freelist = (ADV_CARR_T *)
        ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->irq_sp->next_vpa));

    /*
     * The first command completed by the RISC will be placed in
     * the stopper.
     *
     * Note: Set 'next_vpa' to ASC_CQ_STOPPER. When the request is
     * completed the RISC will set the ASC_RQ_STOPPER bit.
     */
    asc_dvc->irq_sp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

    /*
     * Set RISC IRQ physical address start value.
     */
    AdvWriteDWordLramNoSwap(iop_base, ASC_MC_IRQ, asc_dvc->irq_sp->carr_pa);
    asc_dvc->carr_pending_cnt = 0;

    AdvWriteByteRegister(iop_base, IOPB_INTR_ENABLES,
        (ADV_INTR_ENABLE_HOST_INTR | ADV_INTR_ENABLE_GLOBAL_INTR));
    AdvReadWordLram(iop_base, ASC_MC_CODE_BEGIN_ADDR, word);
    AdvWriteWordRegister(iop_base, IOPW_PC, word);

    /* finally, finally, gentlemen, start your engine */
    AdvWriteWordRegister(iop_base, IOPW_RISC_CSR, ADV_RISC_CSR_RUN);

    /*
     * Reset the SCSI Bus if the EEPROM indicates that SCSI Bus
     * Resets should be performed. The RISC has to be running
     * to issue a SCSI Bus Reset.
     */
    if (asc_dvc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS)
    {
        /*
         * If the BIOS Signature is present in memory, restore the
         * per TID microcode operating variables.
         */
        if (bios_mem[(ASC_MC_BIOS_SIGNATURE - ASC_MC_BIOSMEM)/2] == 0x55AA)
        {
            /*
             * Restore per TID negotiated values.
             */
            AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
            AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
            AdvWriteWordLram(iop_base, ASC_MC_PPR_ABLE, ppr_able);
            AdvWriteWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
            for (tid = 0; tid <= ASC_MAX_TID; tid++)
            {
                AdvWriteByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
                    max_cmd[tid]);
            }
        } else
        {
            if (AdvResetSB(asc_dvc) != ADV_TRUE)
            {
                warn_code = ASC_WARN_BUSRESET_ERROR;
            }
        }
    }

    return warn_code;
}

/*
 * Read the board's EEPROM configuration. Set fields in ADV_DVC_VAR and
 * ADV_DVC_CFG based on the EEPROM settings. The chip is stopped while
 * all of this is done.
 *
 * On failure set the ADV_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 *
 * Note: Chip is stopped on entry.
 */
STATIC int __init
AdvInitFrom3550EEP(ADV_DVC_VAR *asc_dvc)
{
    AdvPortAddr         iop_base;
    ushort              warn_code;
    ADVEEP_3550_CONFIG  eep_config;
    int                 i;

    iop_base = asc_dvc->iop_base;

    warn_code = 0;

    /*
     * Read the board's EEPROM configuration.
     *
     * Set default values if a bad checksum is found.
     */
    if (AdvGet3550EEPConfig(iop_base, &eep_config) != eep_config.check_sum)
    {
        warn_code |= ASC_WARN_EEPROM_CHKSUM;

        /*
         * Set EEPROM default values.
         */
        for (i = 0; i < sizeof(ADVEEP_3550_CONFIG); i++)
        {
            *((uchar *) &eep_config + i) =
                *((uchar *) &Default_3550_EEPROM_Config + i);
        }

        /*
         * Assume the 6 byte board serial number that was read
         * from EEPROM is correct even if the EEPROM checksum
         * failed.
         */
        eep_config.serial_number_word3 =
            AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 1);

        eep_config.serial_number_word2 =
            AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 2);

        eep_config.serial_number_word1 =
            AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 3);

        AdvSet3550EEPConfig(iop_base, &eep_config);
    }
    /*
     * Set ASC_DVC_VAR and ASC_DVC_CFG variables from the
     * EEPROM configuration that was read.
     *
     * This is the mapping of EEPROM fields to Adv Library fields.
     */
    asc_dvc->wdtr_able = eep_config.wdtr_able;
    asc_dvc->sdtr_able = eep_config.sdtr_able;
    asc_dvc->ultra_able = eep_config.ultra_able;
    asc_dvc->tagqng_able = eep_config.tagqng_able;
    asc_dvc->cfg->disc_enable = eep_config.disc_enable;
    asc_dvc->max_host_qng = eep_config.max_host_qng;
    asc_dvc->max_dvc_qng = eep_config.max_dvc_qng;
    asc_dvc->chip_scsi_id = (eep_config.adapter_scsi_id & ADV_MAX_TID);
    asc_dvc->start_motor = eep_config.start_motor;
    asc_dvc->scsi_reset_wait = eep_config.scsi_reset_delay;
    asc_dvc->bios_ctrl = eep_config.bios_ctrl;
    asc_dvc->no_scam = eep_config.scam_tolerant;
    asc_dvc->cfg->serial1 = eep_config.serial_number_word1;
    asc_dvc->cfg->serial2 = eep_config.serial_number_word2;
    asc_dvc->cfg->serial3 = eep_config.serial_number_word3;

    /*
     * Set the host maximum queuing (max. 253, min. 16) and the per device
     * maximum queuing (max. 63, min. 4).
     */
    if (eep_config.max_host_qng > ASC_DEF_MAX_HOST_QNG)
    {
        eep_config.max_host_qng = ASC_DEF_MAX_HOST_QNG;
    } else if (eep_config.max_host_qng < ASC_DEF_MIN_HOST_QNG)
    {
        /* If the value is zero, assume it is uninitialized. */
        if (eep_config.max_host_qng == 0)
        {
            eep_config.max_host_qng = ASC_DEF_MAX_HOST_QNG;
        } else
        {
            eep_config.max_host_qng = ASC_DEF_MIN_HOST_QNG;
        }
    }

    if (eep_config.max_dvc_qng > ASC_DEF_MAX_DVC_QNG)
    {
        eep_config.max_dvc_qng = ASC_DEF_MAX_DVC_QNG;
    } else if (eep_config.max_dvc_qng < ASC_DEF_MIN_DVC_QNG)
    {
        /* If the value is zero, assume it is uninitialized. */
        if (eep_config.max_dvc_qng == 0)
        {
            eep_config.max_dvc_qng = ASC_DEF_MAX_DVC_QNG;
        } else
        {
            eep_config.max_dvc_qng = ASC_DEF_MIN_DVC_QNG;
        }
    }

    /*
     * If 'max_dvc_qng' is greater than 'max_host_qng', then
     * set 'max_dvc_qng' to 'max_host_qng'.
     */
    if (eep_config.max_dvc_qng > eep_config.max_host_qng)
    {
        eep_config.max_dvc_qng = eep_config.max_host_qng;
    }

    /*
     * Set ADV_DVC_VAR 'max_host_qng' and ADV_DVC_VAR 'max_dvc_qng'
     * values based on possibly adjusted EEPROM values.
     */
    asc_dvc->max_host_qng = eep_config.max_host_qng;
    asc_dvc->max_dvc_qng = eep_config.max_dvc_qng;


    /*
     * If the EEPROM 'termination' field is set to automatic (0), then set
     * the ADV_DVC_CFG 'termination' field to automatic also.
     *
     * If the termination is specified with a non-zero 'termination'
     * value check that a legal value is set and set the ADV_DVC_CFG
     * 'termination' field appropriately.
     */
    if (eep_config.termination == 0)
    {
        asc_dvc->cfg->termination = 0;    /* auto termination */
    } else
    {
        /* Enable manual control with low off / high off. */
        if (eep_config.termination == 1)
        {
            asc_dvc->cfg->termination = TERM_CTL_SEL;

        /* Enable manual control with low off / high on. */
        } else if (eep_config.termination == 2)
        {
            asc_dvc->cfg->termination = TERM_CTL_SEL | TERM_CTL_H;

        /* Enable manual control with low on / high on. */
        } else if (eep_config.termination == 3)
        {
            asc_dvc->cfg->termination = TERM_CTL_SEL | TERM_CTL_H | TERM_CTL_L;
        } else
        {
            /*
             * The EEPROM 'termination' field contains a bad value. Use
             * automatic termination instead.
             */
            asc_dvc->cfg->termination = 0;
            warn_code |= ASC_WARN_EEPROM_TERMINATION;
        }
    }

    return warn_code;
}

/*
 * Read the board's EEPROM configuration. Set fields in ADV_DVC_VAR and
 * ADV_DVC_CFG based on the EEPROM settings. The chip is stopped while
 * all of this is done.
 *
 * On failure set the ADV_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 *
 * Note: Chip is stopped on entry.
 */
STATIC int __init
AdvInitFrom38C0800EEP(ADV_DVC_VAR *asc_dvc)
{
    AdvPortAddr              iop_base;
    ushort                   warn_code;
    ADVEEP_38C0800_CONFIG    eep_config;
    int                      i;
    uchar                    tid, termination;
    ushort                   sdtr_speed = 0;

    iop_base = asc_dvc->iop_base;

    warn_code = 0;

    /*
     * Read the board's EEPROM configuration.
     *
     * Set default values if a bad checksum is found.
     */
    if (AdvGet38C0800EEPConfig(iop_base, &eep_config) != eep_config.check_sum)
    {
        warn_code |= ASC_WARN_EEPROM_CHKSUM;

        /*
         * Set EEPROM default values.
         */
        for (i = 0; i < sizeof(ADVEEP_38C0800_CONFIG); i++)
        {
            *((uchar *) &eep_config + i) =
                *((uchar *) &Default_38C0800_EEPROM_Config + i);
        }

        /*
         * Assume the 6 byte board serial number that was read
         * from EEPROM is correct even if the EEPROM checksum
         * failed.
         */
        eep_config.serial_number_word3 =
            AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 1);

        eep_config.serial_number_word2 =
            AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 2);

        eep_config.serial_number_word1 =
            AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 3);

        AdvSet38C0800EEPConfig(iop_base, &eep_config);
    }
    /*
     * Set ADV_DVC_VAR and ADV_DVC_CFG variables from the
     * EEPROM configuration that was read.
     *
     * This is the mapping of EEPROM fields to Adv Library fields.
     */
    asc_dvc->wdtr_able = eep_config.wdtr_able;
    asc_dvc->sdtr_speed1 = eep_config.sdtr_speed1;
    asc_dvc->sdtr_speed2 = eep_config.sdtr_speed2;
    asc_dvc->sdtr_speed3 = eep_config.sdtr_speed3;
    asc_dvc->sdtr_speed4 = eep_config.sdtr_speed4;
    asc_dvc->tagqng_able = eep_config.tagqng_able;
    asc_dvc->cfg->disc_enable = eep_config.disc_enable;
    asc_dvc->max_host_qng = eep_config.max_host_qng;
    asc_dvc->max_dvc_qng = eep_config.max_dvc_qng;
    asc_dvc->chip_scsi_id = (eep_config.adapter_scsi_id & ADV_MAX_TID);
    asc_dvc->start_motor = eep_config.start_motor;
    asc_dvc->scsi_reset_wait = eep_config.scsi_reset_delay;
    asc_dvc->bios_ctrl = eep_config.bios_ctrl;
    asc_dvc->no_scam = eep_config.scam_tolerant;
    asc_dvc->cfg->serial1 = eep_config.serial_number_word1;
    asc_dvc->cfg->serial2 = eep_config.serial_number_word2;
    asc_dvc->cfg->serial3 = eep_config.serial_number_word3;

    /*
     * For every Target ID if any of its 'sdtr_speed[1234]' bits
     * are set, then set an 'sdtr_able' bit for it.
     */
    asc_dvc->sdtr_able = 0;
    for (tid = 0; tid <= ADV_MAX_TID; tid++)
    {
        if (tid == 0)
        {
            sdtr_speed = asc_dvc->sdtr_speed1;
        } else if (tid == 4)
        {
            sdtr_speed = asc_dvc->sdtr_speed2;
        } else if (tid == 8)
        {
            sdtr_speed = asc_dvc->sdtr_speed3;
        } else if (tid == 12)
        {
            sdtr_speed = asc_dvc->sdtr_speed4;
        }
        if (sdtr_speed & ADV_MAX_TID)
        {
            asc_dvc->sdtr_able |= (1 << tid);
        }
        sdtr_speed >>= 4;
    }

    /*
     * Set the host maximum queuing (max. 253, min. 16) and the per device
     * maximum queuing (max. 63, min. 4).
     */
    if (eep_config.max_host_qng > ASC_DEF_MAX_HOST_QNG)
    {
        eep_config.max_host_qng = ASC_DEF_MAX_HOST_QNG;
    } else if (eep_config.max_host_qng < ASC_DEF_MIN_HOST_QNG)
    {
        /* If the value is zero, assume it is uninitialized. */
        if (eep_config.max_host_qng == 0)
        {
            eep_config.max_host_qng = ASC_DEF_MAX_HOST_QNG;
        } else
        {
            eep_config.max_host_qng = ASC_DEF_MIN_HOST_QNG;
        }
    }

    if (eep_config.max_dvc_qng > ASC_DEF_MAX_DVC_QNG)
    {
        eep_config.max_dvc_qng = ASC_DEF_MAX_DVC_QNG;
    } else if (eep_config.max_dvc_qng < ASC_DEF_MIN_DVC_QNG)
    {
        /* If the value is zero, assume it is uninitialized. */
        if (eep_config.max_dvc_qng == 0)
        {
            eep_config.max_dvc_qng = ASC_DEF_MAX_DVC_QNG;
        } else
        {
            eep_config.max_dvc_qng = ASC_DEF_MIN_DVC_QNG;
        }
    }

    /*
     * If 'max_dvc_qng' is greater than 'max_host_qng', then
     * set 'max_dvc_qng' to 'max_host_qng'.
     */
    if (eep_config.max_dvc_qng > eep_config.max_host_qng)
    {
        eep_config.max_dvc_qng = eep_config.max_host_qng;
    }

    /*
     * Set ADV_DVC_VAR 'max_host_qng' and ADV_DVC_VAR 'max_dvc_qng'
     * values based on possibly adjusted EEPROM values.
     */
    asc_dvc->max_host_qng = eep_config.max_host_qng;
    asc_dvc->max_dvc_qng = eep_config.max_dvc_qng;

    /*
     * If the EEPROM 'termination' field is set to automatic (0), then set
     * the ADV_DVC_CFG 'termination' field to automatic also.
     *
     * If the termination is specified with a non-zero 'termination'
     * value check that a legal value is set and set the ADV_DVC_CFG
     * 'termination' field appropriately.
     */
    if (eep_config.termination_se == 0)
    {
        termination = 0;                         /* auto termination for SE */
    } else
    {
        /* Enable manual control with low off / high off. */
        if (eep_config.termination_se == 1)
        {
            termination = 0;

        /* Enable manual control with low off / high on. */
        } else if (eep_config.termination_se == 2)
        {
            termination = TERM_SE_HI;

        /* Enable manual control with low on / high on. */
        } else if (eep_config.termination_se == 3)
        {
            termination = TERM_SE;
        } else
        {
            /*
             * The EEPROM 'termination_se' field contains a bad value.
             * Use automatic termination instead.
             */
            termination = 0;
            warn_code |= ASC_WARN_EEPROM_TERMINATION;
        }
    }

    if (eep_config.termination_lvd == 0)
    {
        asc_dvc->cfg->termination = termination; /* auto termination for LVD */
    } else
    {
        /* Enable manual control with low off / high off. */
        if (eep_config.termination_lvd == 1)
        {
            asc_dvc->cfg->termination = termination;

        /* Enable manual control with low off / high on. */
        } else if (eep_config.termination_lvd == 2)
        {
            asc_dvc->cfg->termination = termination | TERM_LVD_HI;

        /* Enable manual control with low on / high on. */
        } else if (eep_config.termination_lvd == 3)
        {
            asc_dvc->cfg->termination =
                termination | TERM_LVD;
        } else
        {
            /*
             * The EEPROM 'termination_lvd' field contains a bad value.
             * Use automatic termination instead.
             */
            asc_dvc->cfg->termination = termination;
            warn_code |= ASC_WARN_EEPROM_TERMINATION;
        }
    }

    return warn_code;
}

/*
 * Read the board's EEPROM configuration. Set fields in ASC_DVC_VAR and
 * ASC_DVC_CFG based on the EEPROM settings. The chip is stopped while
 * all of this is done.
 *
 * On failure set the ASC_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 *
 * Note: Chip is stopped on entry.
 */
STATIC int __init
AdvInitFrom38C1600EEP(ADV_DVC_VAR *asc_dvc)
{
    AdvPortAddr              iop_base;
    ushort                   warn_code;
    ADVEEP_38C1600_CONFIG    eep_config;
    int                      i;
    uchar                    tid, termination;
    ushort                   sdtr_speed = 0;

    iop_base = asc_dvc->iop_base;

    warn_code = 0;

    /*
     * Read the board's EEPROM configuration.
     *
     * Set default values if a bad checksum is found.
     */
    if (AdvGet38C1600EEPConfig(iop_base, &eep_config) != eep_config.check_sum)
    {
        warn_code |= ASC_WARN_EEPROM_CHKSUM;

        /*
         * Set EEPROM default values.
         */
        for (i = 0; i < sizeof(ADVEEP_38C1600_CONFIG); i++)
        {
            if (i == 1 && ASC_PCI_ID2FUNC(asc_dvc->cfg->pci_slot_info) != 0)
            {
                /*
                 * Set Function 1 EEPROM Word 0 MSB
                 *
                 * Clear the BIOS_ENABLE (bit 14) and INTAB (bit 11)
                 * EEPROM bits.
                 *
                 * Disable Bit 14 (BIOS_ENABLE) to fix SPARC Ultra 60 and
                 * old Mac system booting problem. The Expansion ROM must
                 * be disabled in Function 1 for these systems.
                 *
                 */
                *((uchar *) &eep_config + i) =
                ((*((uchar *) &Default_38C1600_EEPROM_Config + i)) &
                    (~(((ADV_EEPROM_BIOS_ENABLE | ADV_EEPROM_INTAB) >> 8) &
                     0xFF)));

                /*
                 * Set the INTAB (bit 11) if the GPIO 0 input indicates
                 * the Function 1 interrupt line is wired to INTA.
                 *
                 * Set/Clear Bit 11 (INTAB) from the GPIO bit 0 input:
                 *   1 - Function 1 interrupt line wired to INT A.
                 *   0 - Function 1 interrupt line wired to INT B.
                 *
                 * Note: Adapter boards always have Function 0 wired to INTA.
                 * Put all 5 GPIO bits in input mode and then read
                 * their input values.
                 */
                AdvWriteByteRegister(iop_base, IOPB_GPIO_CNTL, 0);
                if (AdvReadByteRegister(iop_base, IOPB_GPIO_DATA) & 0x01)
                {
                    /* Function 1 interrupt wired to INTA; Set EEPROM bit. */
                *((uchar *) &eep_config + i) |=
                    ((ADV_EEPROM_INTAB >> 8) & 0xFF);
                }
            }
            else
            {
                *((uchar *) &eep_config + i) =
                *((uchar *) &Default_38C1600_EEPROM_Config + i);
            }
        }

        /*
         * Assume the 6 byte board serial number that was read
         * from EEPROM is correct even if the EEPROM checksum
         * failed.
         */
        eep_config.serial_number_word3 =
            AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 1);

        eep_config.serial_number_word2 =
            AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 2);

        eep_config.serial_number_word1 =
            AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 3);

        AdvSet38C1600EEPConfig(iop_base, &eep_config);
    }

    /*
     * Set ASC_DVC_VAR and ASC_DVC_CFG variables from the
     * EEPROM configuration that was read.
     *
     * This is the mapping of EEPROM fields to Adv Library fields.
     */
    asc_dvc->wdtr_able = eep_config.wdtr_able;
    asc_dvc->sdtr_speed1 = eep_config.sdtr_speed1;
    asc_dvc->sdtr_speed2 = eep_config.sdtr_speed2;
    asc_dvc->sdtr_speed3 = eep_config.sdtr_speed3;
    asc_dvc->sdtr_speed4 = eep_config.sdtr_speed4;
    asc_dvc->ppr_able = 0;
    asc_dvc->tagqng_able = eep_config.tagqng_able;
    asc_dvc->cfg->disc_enable = eep_config.disc_enable;
    asc_dvc->max_host_qng = eep_config.max_host_qng;
    asc_dvc->max_dvc_qng = eep_config.max_dvc_qng;
    asc_dvc->chip_scsi_id = (eep_config.adapter_scsi_id & ASC_MAX_TID);
    asc_dvc->start_motor = eep_config.start_motor;
    asc_dvc->scsi_reset_wait = eep_config.scsi_reset_delay;
    asc_dvc->bios_ctrl = eep_config.bios_ctrl;
    asc_dvc->no_scam = eep_config.scam_tolerant;

    /*
     * For every Target ID if any of its 'sdtr_speed[1234]' bits
     * are set, then set an 'sdtr_able' bit for it.
     */
    asc_dvc->sdtr_able = 0;
    for (tid = 0; tid <= ASC_MAX_TID; tid++)
    {
        if (tid == 0)
        {
            sdtr_speed = asc_dvc->sdtr_speed1;
        } else if (tid == 4)
        {
            sdtr_speed = asc_dvc->sdtr_speed2;
        } else if (tid == 8)
        {
            sdtr_speed = asc_dvc->sdtr_speed3;
        } else if (tid == 12)
        {
            sdtr_speed = asc_dvc->sdtr_speed4;
        }
        if (sdtr_speed & ASC_MAX_TID)
        {
            asc_dvc->sdtr_able |= (1 << tid);
        }
        sdtr_speed >>= 4;
    }

    /*
     * Set the host maximum queuing (max. 253, min. 16) and the per device
     * maximum queuing (max. 63, min. 4).
     */
    if (eep_config.max_host_qng > ASC_DEF_MAX_HOST_QNG)
    {
        eep_config.max_host_qng = ASC_DEF_MAX_HOST_QNG;
    } else if (eep_config.max_host_qng < ASC_DEF_MIN_HOST_QNG)
    {
        /* If the value is zero, assume it is uninitialized. */
        if (eep_config.max_host_qng == 0)
        {
            eep_config.max_host_qng = ASC_DEF_MAX_HOST_QNG;
        } else
        {
            eep_config.max_host_qng = ASC_DEF_MIN_HOST_QNG;
        }
    }

    if (eep_config.max_dvc_qng > ASC_DEF_MAX_DVC_QNG)
    {
        eep_config.max_dvc_qng = ASC_DEF_MAX_DVC_QNG;
    } else if (eep_config.max_dvc_qng < ASC_DEF_MIN_DVC_QNG)
    {
        /* If the value is zero, assume it is uninitialized. */
        if (eep_config.max_dvc_qng == 0)
        {
            eep_config.max_dvc_qng = ASC_DEF_MAX_DVC_QNG;
        } else
        {
            eep_config.max_dvc_qng = ASC_DEF_MIN_DVC_QNG;
        }
    }

    /*
     * If 'max_dvc_qng' is greater than 'max_host_qng', then
     * set 'max_dvc_qng' to 'max_host_qng'.
     */
    if (eep_config.max_dvc_qng > eep_config.max_host_qng)
    {
        eep_config.max_dvc_qng = eep_config.max_host_qng;
    }

    /*
     * Set ASC_DVC_VAR 'max_host_qng' and ASC_DVC_VAR 'max_dvc_qng'
     * values based on possibly adjusted EEPROM values.
     */
    asc_dvc->max_host_qng = eep_config.max_host_qng;
    asc_dvc->max_dvc_qng = eep_config.max_dvc_qng;

    /*
     * If the EEPROM 'termination' field is set to automatic (0), then set
     * the ASC_DVC_CFG 'termination' field to automatic also.
     *
     * If the termination is specified with a non-zero 'termination'
     * value check that a legal value is set and set the ASC_DVC_CFG
     * 'termination' field appropriately.
     */
    if (eep_config.termination_se == 0)
    {
        termination = 0;                         /* auto termination for SE */
    } else
    {
        /* Enable manual control with low off / high off. */
        if (eep_config.termination_se == 1)
        {
            termination = 0;

        /* Enable manual control with low off / high on. */
        } else if (eep_config.termination_se == 2)
        {
            termination = TERM_SE_HI;

        /* Enable manual control with low on / high on. */
        } else if (eep_config.termination_se == 3)
        {
            termination = TERM_SE;
        } else
        {
            /*
             * The EEPROM 'termination_se' field contains a bad value.
             * Use automatic termination instead.
             */
            termination = 0;
            warn_code |= ASC_WARN_EEPROM_TERMINATION;
        }
    }

    if (eep_config.termination_lvd == 0)
    {
        asc_dvc->cfg->termination = termination; /* auto termination for LVD */
    } else
    {
        /* Enable manual control with low off / high off. */
        if (eep_config.termination_lvd == 1)
        {
            asc_dvc->cfg->termination = termination;

        /* Enable manual control with low off / high on. */
        } else if (eep_config.termination_lvd == 2)
        {
            asc_dvc->cfg->termination = termination | TERM_LVD_HI;

        /* Enable manual control with low on / high on. */
        } else if (eep_config.termination_lvd == 3)
        {
            asc_dvc->cfg->termination =
                termination | TERM_LVD;
        } else
        {
            /*
             * The EEPROM 'termination_lvd' field contains a bad value.
             * Use automatic termination instead.
             */
            asc_dvc->cfg->termination = termination;
            warn_code |= ASC_WARN_EEPROM_TERMINATION;
        }
    }

    return warn_code;
}

/*
 * Read EEPROM configuration into the specified buffer.
 *
 * Return a checksum based on the EEPROM configuration read.
 */
STATIC ushort __init
AdvGet3550EEPConfig(AdvPortAddr iop_base, ADVEEP_3550_CONFIG *cfg_buf)
{
    ushort              wval, chksum;
    ushort              *wbuf;
    int                 eep_addr;
    ushort              *charfields;

    charfields = (ushort *) &ADVEEP_3550_Config_Field_IsChar;
    wbuf = (ushort *) cfg_buf;
    chksum = 0;

    for (eep_addr = ADV_EEP_DVC_CFG_BEGIN;
         eep_addr < ADV_EEP_DVC_CFG_END;
         eep_addr++, wbuf++)
    {
        wval = AdvReadEEPWord(iop_base, eep_addr);
        chksum += wval; /* Checksum is calculated from word values. */
        if (*charfields++) {
            *wbuf = le16_to_cpu(wval);
        } else {
            *wbuf = wval;
        }
    }
    /* Read checksum word. */
    *wbuf = AdvReadEEPWord(iop_base, eep_addr);
    wbuf++; charfields++;

    /* Read rest of EEPROM not covered by the checksum. */
    for (eep_addr = ADV_EEP_DVC_CTL_BEGIN;
         eep_addr < ADV_EEP_MAX_WORD_ADDR;
         eep_addr++, wbuf++)
    {
        *wbuf = AdvReadEEPWord(iop_base, eep_addr);
        if (*charfields++) {
            *wbuf = le16_to_cpu(*wbuf);
        }
    }
    return chksum;
}

/*
 * Read EEPROM configuration into the specified buffer.
 *
 * Return a checksum based on the EEPROM configuration read.
 */
STATIC ushort __init
AdvGet38C0800EEPConfig(AdvPortAddr iop_base,
                       ADVEEP_38C0800_CONFIG *cfg_buf)
{
    ushort              wval, chksum;
    ushort              *wbuf;
    int                 eep_addr;
    ushort              *charfields;

    charfields = (ushort *) &ADVEEP_38C0800_Config_Field_IsChar;
    wbuf = (ushort *) cfg_buf;
    chksum = 0;

    for (eep_addr = ADV_EEP_DVC_CFG_BEGIN;
         eep_addr < ADV_EEP_DVC_CFG_END;
         eep_addr++, wbuf++)
    {
        wval = AdvReadEEPWord(iop_base, eep_addr);
        chksum += wval; /* Checksum is calculated from word values. */
        if (*charfields++) {
            *wbuf = le16_to_cpu(wval);
        } else {
            *wbuf = wval;
        }
    }
    /* Read checksum word. */
    *wbuf = AdvReadEEPWord(iop_base, eep_addr);
    wbuf++; charfields++;

    /* Read rest of EEPROM not covered by the checksum. */
    for (eep_addr = ADV_EEP_DVC_CTL_BEGIN;
         eep_addr < ADV_EEP_MAX_WORD_ADDR;
         eep_addr++, wbuf++)
    {
        *wbuf = AdvReadEEPWord(iop_base, eep_addr);
        if (*charfields++) {
            *wbuf = le16_to_cpu(*wbuf);
        }
    }
    return chksum;
}

/*
 * Read EEPROM configuration into the specified buffer.
 *
 * Return a checksum based on the EEPROM configuration read.
 */
STATIC ushort __init
AdvGet38C1600EEPConfig(AdvPortAddr iop_base,
                       ADVEEP_38C1600_CONFIG *cfg_buf)
{
    ushort              wval, chksum;
    ushort              *wbuf;
    int                 eep_addr;
    ushort              *charfields;

    charfields = (ushort*) &ADVEEP_38C1600_Config_Field_IsChar;
    wbuf = (ushort *) cfg_buf;
    chksum = 0;

    for (eep_addr = ADV_EEP_DVC_CFG_BEGIN;
         eep_addr < ADV_EEP_DVC_CFG_END;
         eep_addr++, wbuf++)
    {
        wval = AdvReadEEPWord(iop_base, eep_addr);
        chksum += wval; /* Checksum is calculated from word values. */
        if (*charfields++) {
            *wbuf = le16_to_cpu(wval);
        } else {
            *wbuf = wval;
        }
    }
    /* Read checksum word. */
    *wbuf = AdvReadEEPWord(iop_base, eep_addr);
    wbuf++; charfields++;

    /* Read rest of EEPROM not covered by the checksum. */
    for (eep_addr = ADV_EEP_DVC_CTL_BEGIN;
         eep_addr < ADV_EEP_MAX_WORD_ADDR;
         eep_addr++, wbuf++)
    {
        *wbuf = AdvReadEEPWord(iop_base, eep_addr);
        if (*charfields++) {
            *wbuf = le16_to_cpu(*wbuf);
        }
    }
    return chksum;
}

/*
 * Read the EEPROM from specified location
 */
STATIC ushort __init
AdvReadEEPWord(AdvPortAddr iop_base, int eep_word_addr)
{
    AdvWriteWordRegister(iop_base, IOPW_EE_CMD,
        ASC_EEP_CMD_READ | eep_word_addr);
    AdvWaitEEPCmd(iop_base);
    return AdvReadWordRegister(iop_base, IOPW_EE_DATA);
}

/*
 * Wait for EEPROM command to complete
 */
STATIC void __init
AdvWaitEEPCmd(AdvPortAddr iop_base)
{
    int eep_delay_ms;

    for (eep_delay_ms = 0; eep_delay_ms < ADV_EEP_DELAY_MS; eep_delay_ms++)
    {
        if (AdvReadWordRegister(iop_base, IOPW_EE_CMD) & ASC_EEP_CMD_DONE)
        {
            break;
        }
        DvcSleepMilliSecond(1);
    }
    if ((AdvReadWordRegister(iop_base, IOPW_EE_CMD) & ASC_EEP_CMD_DONE) == 0)
    {
        ASC_ASSERT(0);
    }
    return;
}

/*
 * Write the EEPROM from 'cfg_buf'.
 */
void __init
AdvSet3550EEPConfig(AdvPortAddr iop_base, ADVEEP_3550_CONFIG *cfg_buf)
{
    ushort *wbuf;
    ushort addr, chksum;
    ushort *charfields;

    wbuf = (ushort *) cfg_buf;
    charfields = (ushort *) &ADVEEP_3550_Config_Field_IsChar;
    chksum = 0;

    AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE_ABLE);
    AdvWaitEEPCmd(iop_base);

    /*
     * Write EEPROM from word 0 to word 20.
     */
    for (addr = ADV_EEP_DVC_CFG_BEGIN;
         addr < ADV_EEP_DVC_CFG_END; addr++, wbuf++)
    {
        ushort word;

        if (*charfields++) {
            word = cpu_to_le16(*wbuf);
        } else {
            word = *wbuf;
        }
        chksum += *wbuf; /* Checksum is calculated from word values. */
        AdvWriteWordRegister(iop_base, IOPW_EE_DATA, word);
        AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE | addr);
        AdvWaitEEPCmd(iop_base);
        DvcSleepMilliSecond(ADV_EEP_DELAY_MS);
    }

    /*
     * Write EEPROM checksum at word 21.
     */
    AdvWriteWordRegister(iop_base, IOPW_EE_DATA, chksum);
    AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE | addr);
    AdvWaitEEPCmd(iop_base);
    wbuf++; charfields++;

    /*
     * Write EEPROM OEM name at words 22 to 29.
     */
    for (addr = ADV_EEP_DVC_CTL_BEGIN;
         addr < ADV_EEP_MAX_WORD_ADDR; addr++, wbuf++)
    {
        ushort word;

        if (*charfields++) {
            word = cpu_to_le16(*wbuf);
        } else {
            word = *wbuf;
        }
        AdvWriteWordRegister(iop_base, IOPW_EE_DATA, word);
        AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE | addr);
        AdvWaitEEPCmd(iop_base);
    }
    AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE_DISABLE);
    AdvWaitEEPCmd(iop_base);
    return;
}

/*
 * Write the EEPROM from 'cfg_buf'.
 */
void __init
AdvSet38C0800EEPConfig(AdvPortAddr iop_base,
                       ADVEEP_38C0800_CONFIG *cfg_buf)
{
    ushort *wbuf;
    ushort *charfields;
    ushort addr, chksum;

    wbuf = (ushort *) cfg_buf;
    charfields = (ushort *) &ADVEEP_38C0800_Config_Field_IsChar;
    chksum = 0;

    AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE_ABLE);
    AdvWaitEEPCmd(iop_base);

    /*
     * Write EEPROM from word 0 to word 20.
     */
    for (addr = ADV_EEP_DVC_CFG_BEGIN;
         addr < ADV_EEP_DVC_CFG_END; addr++, wbuf++)
    {
        ushort word;

        if (*charfields++) {
            word = cpu_to_le16(*wbuf);
        } else {
            word = *wbuf;
        }
        chksum += *wbuf; /* Checksum is calculated from word values. */
        AdvWriteWordRegister(iop_base, IOPW_EE_DATA, word);
        AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE | addr);
        AdvWaitEEPCmd(iop_base);
        DvcSleepMilliSecond(ADV_EEP_DELAY_MS);
    }

    /*
     * Write EEPROM checksum at word 21.
     */
    AdvWriteWordRegister(iop_base, IOPW_EE_DATA, chksum);
    AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE | addr);
    AdvWaitEEPCmd(iop_base);
    wbuf++; charfields++;

    /*
     * Write EEPROM OEM name at words 22 to 29.
     */
    for (addr = ADV_EEP_DVC_CTL_BEGIN;
         addr < ADV_EEP_MAX_WORD_ADDR; addr++, wbuf++)
    {
        ushort word;

        if (*charfields++) {
            word = cpu_to_le16(*wbuf);
        } else {
            word = *wbuf;
        }
        AdvWriteWordRegister(iop_base, IOPW_EE_DATA, word);
        AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE | addr);
        AdvWaitEEPCmd(iop_base);
    }
    AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE_DISABLE);
    AdvWaitEEPCmd(iop_base);
    return;
}

/*
 * Write the EEPROM from 'cfg_buf'.
 */
void __init
AdvSet38C1600EEPConfig(AdvPortAddr iop_base,
                       ADVEEP_38C1600_CONFIG *cfg_buf)
{
    ushort              *wbuf;
    ushort              *charfields;
    ushort              addr, chksum;

    wbuf = (ushort *) cfg_buf;
    charfields = (ushort *) &ADVEEP_38C1600_Config_Field_IsChar;
    chksum = 0;

    AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE_ABLE);
    AdvWaitEEPCmd(iop_base);

    /*
     * Write EEPROM from word 0 to word 20.
     */
    for (addr = ADV_EEP_DVC_CFG_BEGIN;
         addr < ADV_EEP_DVC_CFG_END; addr++, wbuf++)
    {
        ushort word;

        if (*charfields++) {
            word = cpu_to_le16(*wbuf);
        } else {
            word = *wbuf;
        }
        chksum += *wbuf; /* Checksum is calculated from word values. */
        AdvWriteWordRegister(iop_base, IOPW_EE_DATA, word);
        AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE | addr);
        AdvWaitEEPCmd(iop_base);
        DvcSleepMilliSecond(ADV_EEP_DELAY_MS);
    }

    /*
     * Write EEPROM checksum at word 21.
     */
    AdvWriteWordRegister(iop_base, IOPW_EE_DATA, chksum);
    AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE | addr);
    AdvWaitEEPCmd(iop_base);
    wbuf++; charfields++;

    /*
     * Write EEPROM OEM name at words 22 to 29.
     */
    for (addr = ADV_EEP_DVC_CTL_BEGIN;
         addr < ADV_EEP_MAX_WORD_ADDR; addr++, wbuf++)
    {
        ushort word;

        if (*charfields++) {
            word = cpu_to_le16(*wbuf);
        } else {
            word = *wbuf;
        }
        AdvWriteWordRegister(iop_base, IOPW_EE_DATA, word);
        AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE | addr);
        AdvWaitEEPCmd(iop_base);
    }
    AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE_DISABLE);
    AdvWaitEEPCmd(iop_base);
    return;
}

/* a_advlib.c */
/*
 * AdvExeScsiQueue() - Send a request to the RISC microcode program.
 *
 *   Allocate a carrier structure, point the carrier to the ADV_SCSI_REQ_Q,
 *   add the carrier to the ICQ (Initiator Command Queue), and tickle the
 *   RISC to notify it a new command is ready to be executed.
 *
 * If 'done_status' is not set to QD_DO_RETRY, then 'error_retry' will be
 * set to SCSI_MAX_RETRY.
 *
 * Multi-byte fields in the ASC_SCSI_REQ_Q that are used by the microcode
 * for DMA addresses or math operations are byte swapped to little-endian
 * order.
 *
 * Return:
 *      ADV_SUCCESS(1) - The request was successfully queued.
 *      ADV_BUSY(0) -    Resource unavailable; Retry again after pending
 *                       request completes.
 *      ADV_ERROR(-1) -  Invalid ADV_SCSI_REQ_Q request structure
 *                       host IC error.
 */
STATIC int
AdvExeScsiQueue(ADV_DVC_VAR *asc_dvc,
                ADV_SCSI_REQ_Q *scsiq)
{
    ulong                  last_int_level;
    AdvPortAddr            iop_base;
    ADV_DCNT               req_size;
    ADV_PADDR              req_paddr;
    ADV_CARR_T             *new_carrp;

    ASC_ASSERT(scsiq != NULL); /* 'scsiq' should never be NULL. */

    /*
     * The ADV_SCSI_REQ_Q 'target_id' field should never exceed ADV_MAX_TID.
     */
    if (scsiq->target_id > ADV_MAX_TID)
    {
        scsiq->host_status = QHSTA_M_INVALID_DEVICE;
        scsiq->done_status = QD_WITH_ERROR;
        return ADV_ERROR;
    }

    iop_base = asc_dvc->iop_base;

    last_int_level = DvcEnterCritical();

    /*
     * Allocate a carrier ensuring at least one carrier always
     * remains on the freelist and initialize fields.
     */
    if ((new_carrp = asc_dvc->carr_freelist) == NULL)
    {
       DvcLeaveCritical(last_int_level);
       return ADV_BUSY;
    }
    asc_dvc->carr_freelist = (ADV_CARR_T *)
        ADV_U32_TO_VADDR(le32_to_cpu(new_carrp->next_vpa));
    asc_dvc->carr_pending_cnt++;

    /*
     * Set the carrier to be a stopper by setting 'next_vpa'
     * to the stopper value. The current stopper will be changed
     * below to point to the new stopper.
     */
    new_carrp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

    /*
     * Clear the ADV_SCSI_REQ_Q done flag.
     */
    scsiq->a_flag &= ~ADV_SCSIQ_DONE;

    req_size = sizeof(ADV_SCSI_REQ_Q);
    req_paddr = DvcGetPhyAddr(asc_dvc, scsiq, (uchar *) scsiq,
        (ADV_SDCNT *) &req_size, ADV_IS_SCSIQ_FLAG);

    ASC_ASSERT(ADV_32BALIGN(req_paddr) == req_paddr);
    ASC_ASSERT(req_size >= sizeof(ADV_SCSI_REQ_Q));

    /* Wait for assertion before making little-endian */
    req_paddr = cpu_to_le32(req_paddr);

    /* Save virtual and physical address of ADV_SCSI_REQ_Q and carrier. */
    scsiq->scsiq_ptr = cpu_to_le32(ADV_VADDR_TO_U32(scsiq));
    scsiq->scsiq_rptr = req_paddr;

    scsiq->carr_va = cpu_to_le32(ADV_VADDR_TO_U32(asc_dvc->icq_sp));
    /*
     * Every ADV_CARR_T.carr_pa is byte swapped to little-endian
     * order during initialization.
     */
    scsiq->carr_pa = asc_dvc->icq_sp->carr_pa;

   /*
    * Use the current stopper to send the ADV_SCSI_REQ_Q command to
    * the microcode. The newly allocated stopper will become the new
    * stopper.
    */
    asc_dvc->icq_sp->areq_vpa = req_paddr;

    /*
     * Set the 'next_vpa' pointer for the old stopper to be the
     * physical address of the new stopper. The RISC can only
     * follow physical addresses.
     */
    asc_dvc->icq_sp->next_vpa = new_carrp->carr_pa;

    /*
     * Set the host adapter stopper pointer to point to the new carrier.
     */
    asc_dvc->icq_sp = new_carrp;

    if (asc_dvc->chip_type == ADV_CHIP_ASC3550 ||
        asc_dvc->chip_type == ADV_CHIP_ASC38C0800)
    {
        /*
         * Tickle the RISC to tell it to read its Command Queue Head pointer.
         */
        AdvWriteByteRegister(iop_base, IOPB_TICKLE, ADV_TICKLE_A);
        if (asc_dvc->chip_type == ADV_CHIP_ASC3550)
        {
            /*
             * Clear the tickle value. In the ASC-3550 the RISC flag
             * command 'clr_tickle_a' does not work unless the host
             * value is cleared.
             */
            AdvWriteByteRegister(iop_base, IOPB_TICKLE, ADV_TICKLE_NOP);
        }
    } else if (asc_dvc->chip_type == ADV_CHIP_ASC38C1600)
    {
        /*
         * Notify the RISC a carrier is ready by writing the physical
         * address of the new carrier stopper to the COMMA register.
         */
        AdvWriteDWordRegister(iop_base, IOPDW_COMMA,
                le32_to_cpu(new_carrp->carr_pa));
    }

    DvcLeaveCritical(last_int_level);

    return ADV_SUCCESS;
}

/*
 * Reset SCSI Bus and purge all outstanding requests.
 *
 * Return Value:
 *      ADV_TRUE(1) -   All requests are purged and SCSI Bus is reset.
 *      ADV_FALSE(0) -  Microcode command failed.
 *      ADV_ERROR(-1) - Microcode command timed-out. Microcode or IC
 *                      may be hung which requires driver recovery.
 */
STATIC int
AdvResetSB(ADV_DVC_VAR *asc_dvc)
{
    int         status;

    /*
     * Send the SCSI Bus Reset idle start idle command which asserts
     * the SCSI Bus Reset signal.
     */
    status = AdvSendIdleCmd(asc_dvc, (ushort) IDLE_CMD_SCSI_RESET_START, 0L);
    if (status != ADV_TRUE)
    {
        return status;
    }

    /*
     * Delay for the specified SCSI Bus Reset hold time.
     *
     * The hold time delay is done on the host because the RISC has no
     * microsecond accurate timer.
     */
    DvcDelayMicroSecond(asc_dvc, (ushort) ASC_SCSI_RESET_HOLD_TIME_US);

    /*
     * Send the SCSI Bus Reset end idle command which de-asserts
     * the SCSI Bus Reset signal and purges any pending requests.
     */
    status = AdvSendIdleCmd(asc_dvc, (ushort) IDLE_CMD_SCSI_RESET_END, 0L);
    if (status != ADV_TRUE)
    {
        return status;
    }

    DvcSleepMilliSecond((ADV_DCNT) asc_dvc->scsi_reset_wait * 1000);

    return status;
}

/*
 * Reset chip and SCSI Bus.
 *
 * Return Value:
 *      ADV_TRUE(1) -   Chip re-initialization and SCSI Bus Reset successful.
 *      ADV_FALSE(0) -  Chip re-initialization and SCSI Bus Reset failure.
 */
STATIC int
AdvResetChipAndSB(ADV_DVC_VAR *asc_dvc)
{
    int         status;
    ushort      wdtr_able, sdtr_able, tagqng_able;
    ushort      ppr_able = 0;
    uchar       tid, max_cmd[ADV_MAX_TID + 1];
    AdvPortAddr iop_base;
    ushort      bios_sig;

    iop_base = asc_dvc->iop_base;

    /*
     * Save current per TID negotiated values.
     */
    AdvReadWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
    AdvReadWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
    if (asc_dvc->chip_type == ADV_CHIP_ASC38C1600)
    {
        AdvReadWordLram(iop_base, ASC_MC_PPR_ABLE, ppr_able);
    }
    AdvReadWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
    for (tid = 0; tid <= ADV_MAX_TID; tid++)
    {
        AdvReadByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
            max_cmd[tid]);
    }

    /*
     * Force the AdvInitAsc3550/38C0800Driver() function to
     * perform a SCSI Bus Reset by clearing the BIOS signature word.
     * The initialization functions assumes a SCSI Bus Reset is not
     * needed if the BIOS signature word is present.
     */
    AdvReadWordLram(iop_base, ASC_MC_BIOS_SIGNATURE, bios_sig);
    AdvWriteWordLram(iop_base, ASC_MC_BIOS_SIGNATURE, 0);

    /*
     * Stop chip and reset it.
     */
    AdvWriteWordRegister(iop_base, IOPW_RISC_CSR, ADV_RISC_CSR_STOP);
    AdvWriteWordRegister(iop_base, IOPW_CTRL_REG, ADV_CTRL_REG_CMD_RESET);
    DvcSleepMilliSecond(100);
    AdvWriteWordRegister(iop_base, IOPW_CTRL_REG, ADV_CTRL_REG_CMD_WR_IO_REG);

    /*
     * Reset Adv Library error code, if any, and try
     * re-initializing the chip.
     */
    asc_dvc->err_code = 0;
    if (asc_dvc->chip_type == ADV_CHIP_ASC38C1600)
    {
        status = AdvInitAsc38C1600Driver(asc_dvc);
    }
    else if (asc_dvc->chip_type == ADV_CHIP_ASC38C0800)
    {
        status = AdvInitAsc38C0800Driver(asc_dvc);
    } else
    {
        status = AdvInitAsc3550Driver(asc_dvc);
    }

    /* Translate initialization return value to status value. */
    if (status == 0)
    {
        status = ADV_TRUE;
    } else
    {
        status = ADV_FALSE;
    }

    /*
     * Restore the BIOS signature word.
     */
    AdvWriteWordLram(iop_base, ASC_MC_BIOS_SIGNATURE, bios_sig);

    /*
     * Restore per TID negotiated values.
     */
    AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
    AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
    if (asc_dvc->chip_type == ADV_CHIP_ASC38C1600)
    {
        AdvWriteWordLram(iop_base, ASC_MC_PPR_ABLE, ppr_able);
    }
    AdvWriteWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
    for (tid = 0; tid <= ADV_MAX_TID; tid++)
    {
        AdvWriteByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
            max_cmd[tid]);
    }

    return status;
}

/*
 * Adv Library Interrupt Service Routine
 *
 *  This function is called by a driver's interrupt service routine.
 *  The function disables and re-enables interrupts.
 *
 *  When a microcode idle command is completed, the ADV_DVC_VAR
 *  'idle_cmd_done' field is set to ADV_TRUE.
 *
 *  Note: AdvISR() can be called when interrupts are disabled or even
 *  when there is no hardware interrupt condition present. It will
 *  always check for completed idle commands and microcode requests.
 *  This is an important feature that shouldn't be changed because it
 *  allows commands to be completed from polling mode loops.
 *
 * Return:
 *   ADV_TRUE(1) - interrupt was pending
 *   ADV_FALSE(0) - no interrupt was pending
 */
STATIC int
AdvISR(ADV_DVC_VAR *asc_dvc)
{
    AdvPortAddr                 iop_base;
    uchar                       int_stat;
    ushort                      target_bit;
    ADV_CARR_T                  *free_carrp;
    ADV_VADDR                   irq_next_vpa;
    int                         flags;
    ADV_SCSI_REQ_Q              *scsiq;

    flags = DvcEnterCritical();

    iop_base = asc_dvc->iop_base;

    /* Reading the register clears the interrupt. */
    int_stat = AdvReadByteRegister(iop_base, IOPB_INTR_STATUS_REG);

    if ((int_stat & (ADV_INTR_STATUS_INTRA | ADV_INTR_STATUS_INTRB |
         ADV_INTR_STATUS_INTRC)) == 0)
    {
        DvcLeaveCritical(flags);
        return ADV_FALSE;
    }

    /*
     * Notify the driver of an asynchronous microcode condition by
     * calling the ADV_DVC_VAR.async_callback function. The function
     * is passed the microcode ASC_MC_INTRB_CODE byte value.
     */
    if (int_stat & ADV_INTR_STATUS_INTRB)
    {
        uchar intrb_code;

        AdvReadByteLram(iop_base, ASC_MC_INTRB_CODE, intrb_code);

        if (asc_dvc->chip_type == ADV_CHIP_ASC3550 ||
            asc_dvc->chip_type == ADV_CHIP_ASC38C0800)
        {
            if (intrb_code == ADV_ASYNC_CARRIER_READY_FAILURE &&
                asc_dvc->carr_pending_cnt != 0)
            {
                AdvWriteByteRegister(iop_base, IOPB_TICKLE, ADV_TICKLE_A);
                if (asc_dvc->chip_type == ADV_CHIP_ASC3550)
                {
                    AdvWriteByteRegister(iop_base, IOPB_TICKLE, ADV_TICKLE_NOP);
                }
            }
        }

        if (asc_dvc->async_callback != 0)
        {
            (*asc_dvc->async_callback)(asc_dvc, intrb_code);
        }
    }

    /*
     * Check if the IRQ stopper carrier contains a completed request.
     */
    while (((irq_next_vpa =
             le32_to_cpu(asc_dvc->irq_sp->next_vpa)) & ASC_RQ_DONE) != 0)
    {
        /*
         * Get a pointer to the newly completed ADV_SCSI_REQ_Q structure.
         * The RISC will have set 'areq_vpa' to a virtual address.
         *
         * The firmware will have copied the ASC_SCSI_REQ_Q.scsiq_ptr
         * field to the carrier ADV_CARR_T.areq_vpa field. The conversion
         * below complements the conversion of ASC_SCSI_REQ_Q.scsiq_ptr'
         * in AdvExeScsiQueue().
         */
        scsiq = (ADV_SCSI_REQ_Q *)
            ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->irq_sp->areq_vpa));

        /*
         * Request finished with good status and the queue was not
         * DMAed to host memory by the firmware. Set all status fields
         * to indicate good status.
         */
        if ((irq_next_vpa & ASC_RQ_GOOD) != 0)
        {
            scsiq->done_status = QD_NO_ERROR;
            scsiq->host_status = scsiq->scsi_status = 0;
            scsiq->data_cnt = 0L;
        }

        /*
         * Advance the stopper pointer to the next carrier
         * ignoring the lower four bits. Free the previous
         * stopper carrier.
         */
        free_carrp = asc_dvc->irq_sp;
        asc_dvc->irq_sp = (ADV_CARR_T *)
            ADV_U32_TO_VADDR(ASC_GET_CARRP(irq_next_vpa));

        free_carrp->next_vpa =
                cpu_to_le32(ADV_VADDR_TO_U32(asc_dvc->carr_freelist));
        asc_dvc->carr_freelist = free_carrp;
        asc_dvc->carr_pending_cnt--;

        ASC_ASSERT(scsiq != NULL);
        target_bit = ADV_TID_TO_TIDMASK(scsiq->target_id);

        /*
         * Clear request microcode control flag.
         */
        scsiq->cntl = 0;

        /*
         * If the command that completed was a SCSI INQUIRY and
         * LUN 0 was sent the command, then process the INQUIRY
         * command information for the device.
         *
         * Note: If data returned were either VPD or CmdDt data,
         * don't process the INQUIRY command information for
         * the device, otherwise may erroneously set *_able bits.
         */
        if (scsiq->done_status == QD_NO_ERROR &&
            scsiq->cdb[0] == INQUIRY &&
            scsiq->target_lun == 0 &&
            (scsiq->cdb[1] & ADV_INQ_RTN_VPD_AND_CMDDT)
                == ADV_INQ_RTN_STD_INQUIRY_DATA)
        {
            AdvInquiryHandling(asc_dvc, scsiq);
        }

        /*
         * Notify the driver of the completed request by passing
         * the ADV_SCSI_REQ_Q pointer to its callback function.
         */
        scsiq->a_flag |= ADV_SCSIQ_DONE;
        (*asc_dvc->isr_callback)(asc_dvc, scsiq);
        /*
         * Note: After the driver callback function is called, 'scsiq'
         * can no longer be referenced.
         *
         * Fall through and continue processing other completed
         * requests...
         */

        /*
         * Disable interrupts again in case the driver inadvertently
         * enabled interrupts in its callback function.
         *
         * The DvcEnterCritical() return value is ignored, because
         * the 'flags' saved when AdvISR() was first entered will be
         * used to restore the interrupt flag on exit.
         */
        (void) DvcEnterCritical();
    }
    DvcLeaveCritical(flags);
    return ADV_TRUE;
}

/*
 * Send an idle command to the chip and wait for completion.
 *
 * Command completion is polled for once per microsecond.
 *
 * The function can be called from anywhere including an interrupt handler.
 * But the function is not re-entrant, so it uses the DvcEnter/LeaveCritical()
 * functions to prevent reentrancy.
 *
 * Return Values:
 *   ADV_TRUE - command completed successfully
 *   ADV_FALSE - command failed
 *   ADV_ERROR - command timed out
 */
STATIC int
AdvSendIdleCmd(ADV_DVC_VAR *asc_dvc,
               ushort idle_cmd,
               ADV_DCNT idle_cmd_parameter)
{
    ulong       last_int_level;
    int         result;
    ADV_DCNT    i, j;
    AdvPortAddr iop_base;

    last_int_level = DvcEnterCritical();

    iop_base = asc_dvc->iop_base;

    /*
     * Clear the idle command status which is set by the microcode
     * to a non-zero value to indicate when the command is completed.
     * The non-zero result is one of the IDLE_CMD_STATUS_* values
     * defined in a_advlib.h.
     */
    AdvWriteWordLram(iop_base, ASC_MC_IDLE_CMD_STATUS, (ushort) 0);

    /*
     * Write the idle command value after the idle command parameter
     * has been written to avoid a race condition. If the order is not
     * followed, the microcode may process the idle command before the
     * parameters have been written to LRAM.
     */
    AdvWriteDWordLramNoSwap(iop_base, ASC_MC_IDLE_CMD_PARAMETER,
        cpu_to_le32(idle_cmd_parameter));
    AdvWriteWordLram(iop_base, ASC_MC_IDLE_CMD, idle_cmd);

    /*
     * Tickle the RISC to tell it to process the idle command.
     */
    AdvWriteByteRegister(iop_base, IOPB_TICKLE, ADV_TICKLE_B);
    if (asc_dvc->chip_type == ADV_CHIP_ASC3550)
    {
        /*
         * Clear the tickle value. In the ASC-3550 the RISC flag
         * command 'clr_tickle_b' does not work unless the host
         * value is cleared.
         */
        AdvWriteByteRegister(iop_base, IOPB_TICKLE, ADV_TICKLE_NOP);
    }

    /* Wait for up to 100 millisecond for the idle command to timeout. */
    for (i = 0; i < SCSI_WAIT_100_MSEC; i++)
    {
        /* Poll once each microsecond for command completion. */
        for (j = 0; j < SCSI_US_PER_MSEC; j++)
        {
            AdvReadWordLram(iop_base, ASC_MC_IDLE_CMD_STATUS, result);
            if (result != 0)
            {
                DvcLeaveCritical(last_int_level);
                return result;
            }
            DvcDelayMicroSecond(asc_dvc, (ushort) 1);
        }
    }

    ASC_ASSERT(0); /* The idle command should never timeout. */
    DvcLeaveCritical(last_int_level);
    return ADV_ERROR;
}

/*
 * Inquiry Information Byte 7 Handling
 *
 * Handle SCSI Inquiry Command information for a device by setting
 * microcode operating variables that affect WDTR, SDTR, and Tag
 * Queuing.
 */
STATIC void
AdvInquiryHandling(
    ADV_DVC_VAR                 *asc_dvc,
    ADV_SCSI_REQ_Q              *scsiq)
{
    AdvPortAddr                 iop_base;
    uchar                       tid;
    ADV_SCSI_INQUIRY            *inq;
    ushort                      tidmask;
    ushort                      cfg_word;

    /*
     * AdvInquiryHandling() requires up to INQUIRY information Byte 7
     * to be available.
     *
     * If less than 8 bytes of INQUIRY information were requested or less
     * than 8 bytes were transferred, then return. cdb[4] is the request
     * length and the ADV_SCSI_REQ_Q 'data_cnt' field is set by the
     * microcode to the transfer residual count.
     */

    if (scsiq->cdb[4] < 8 ||
        (scsiq->cdb[4] - le32_to_cpu(scsiq->data_cnt)) < 8)
    {
        return;
    }

    iop_base = asc_dvc->iop_base;
    tid = scsiq->target_id;

    inq = (ADV_SCSI_INQUIRY *) scsiq->vdata_addr;

    /*
     * WDTR, SDTR, and Tag Queuing cannot be enabled for old devices.
     */
    if (ADV_INQ_RESPONSE_FMT(inq) < 2 && ADV_INQ_ANSI_VER(inq) < 2)
    {
        return;
    } else
    {
        /*
         * INQUIRY Byte 7 Handling
         *
         * Use a device's INQUIRY byte 7 to determine whether it
         * supports WDTR, SDTR, and Tag Queuing. If the feature
         * is enabled in the EEPROM and the device supports the
         * feature, then enable it in the microcode.
         */

        tidmask = ADV_TID_TO_TIDMASK(tid);

        /*
         * Wide Transfers
         *
         * If the EEPROM enabled WDTR for the device and the device
         * supports wide bus (16 bit) transfers, then turn on the
         * device's 'wdtr_able' bit and write the new value to the
         * microcode.
         */
        if ((asc_dvc->wdtr_able & tidmask) && ADV_INQ_WIDE16(inq))
        {
            AdvReadWordLram(iop_base, ASC_MC_WDTR_ABLE, cfg_word);
            if ((cfg_word & tidmask) == 0)
            {
                cfg_word |= tidmask;
                AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE, cfg_word);

                /*
                 * Clear the microcode "SDTR negotiation" and "WDTR
                 * negotiation" done indicators for the target to cause
                 * it to negotiate with the new setting set above.
                 * WDTR when accepted causes the target to enter
                 * asynchronous mode, so SDTR must be negotiated.
                 */
                AdvReadWordLram(iop_base, ASC_MC_SDTR_DONE, cfg_word);
                cfg_word &= ~tidmask;
                AdvWriteWordLram(iop_base, ASC_MC_SDTR_DONE, cfg_word);
                AdvReadWordLram(iop_base, ASC_MC_WDTR_DONE, cfg_word);
                cfg_word &= ~tidmask;
                AdvWriteWordLram(iop_base, ASC_MC_WDTR_DONE, cfg_word);
            }
        }

        /*
         * Synchronous Transfers
         *
         * If the EEPROM enabled SDTR for the device and the device
         * supports synchronous transfers, then turn on the device's
         * 'sdtr_able' bit. Write the new value to the microcode.
         */
        if ((asc_dvc->sdtr_able & tidmask) && ADV_INQ_SYNC(inq))
        {
            AdvReadWordLram(iop_base, ASC_MC_SDTR_ABLE, cfg_word);
            if ((cfg_word & tidmask) == 0)
            {
                cfg_word |= tidmask;
                AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE, cfg_word);

                /*
                 * Clear the microcode "SDTR negotiation" done indicator
                 * for the target to cause it to negotiate with the new
                 * setting set above.
                 */
                AdvReadWordLram(iop_base, ASC_MC_SDTR_DONE, cfg_word);
                cfg_word &= ~tidmask;
                AdvWriteWordLram(iop_base, ASC_MC_SDTR_DONE, cfg_word);
            }
        }
        /*
         * If the Inquiry data included enough space for the SPI-3
         * Clocking field, then check if DT mode is supported.
         */
        if (asc_dvc->chip_type == ADV_CHIP_ASC38C1600 &&
            (scsiq->cdb[4] >= 57 ||
            (scsiq->cdb[4] - le32_to_cpu(scsiq->data_cnt)) >= 57))
        {
            /*
             * PPR (Parallel Protocol Request) Capable
             *
             * If the device supports DT mode, then it must be PPR capable.
             * The PPR message will be used in place of the SDTR and WDTR
             * messages to negotiate synchronous speed and offset, transfer
             * width, and protocol options.
             */
            if (ADV_INQ_CLOCKING(inq) & ADV_INQ_CLOCKING_DT_ONLY)
            {
                AdvReadWordLram(iop_base, ASC_MC_PPR_ABLE, asc_dvc->ppr_able);
                asc_dvc->ppr_able |= tidmask;
                AdvWriteWordLram(iop_base, ASC_MC_PPR_ABLE, asc_dvc->ppr_able);
            }
        }

        /*
         * If the EEPROM enabled Tag Queuing for the device and the
         * device supports Tag Queueing, then turn on the device's
         * 'tagqng_enable' bit in the microcode and set the microcode
         * maximum command count to the ADV_DVC_VAR 'max_dvc_qng'
         * value.
         *
         * Tag Queuing is disabled for the BIOS which runs in polled
         * mode and would see no benefit from Tag Queuing. Also by
         * disabling Tag Queuing in the BIOS devices with Tag Queuing
         * bugs will at least work with the BIOS.
         */
        if ((asc_dvc->tagqng_able & tidmask) && ADV_INQ_CMD_QUEUE(inq))
        {
            AdvReadWordLram(iop_base, ASC_MC_TAGQNG_ABLE, cfg_word);
            cfg_word |= tidmask;
            AdvWriteWordLram(iop_base, ASC_MC_TAGQNG_ABLE, cfg_word);

            AdvWriteByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
                asc_dvc->max_dvc_qng);
        }
    }
}
MODULE_LICENSE("Dual BSD/GPL");

#ifdef CONFIG_PCI
/* PCI Devices supported by this driver */
static struct pci_device_id advansys_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_ASP, PCI_DEVICE_ID_ASP_1200A,
	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_ASP, PCI_DEVICE_ID_ASP_ABP940,
	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_ASP, PCI_DEVICE_ID_ASP_ABP940U,
	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_ASP, PCI_DEVICE_ID_ASP_ABP940UW,
	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_ASP, PCI_DEVICE_ID_38C0800_REV1,
	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_ASP, PCI_DEVICE_ID_38C1600_REV1,
	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ }
};
MODULE_DEVICE_TABLE(pci, advansys_pci_tbl);
#endif /* CONFIG_PCI */
