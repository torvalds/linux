/*
 *      u14-34f.c - Low-level driver for UltraStor 14F/34F SCSI host adapters.
 *
 *      03 Jun 2003 Rev. 8.10 for linux-2.5.70
 *        + Update for new IRQ API.
 *        + Use "goto" when appropriate.
 *        + Drop u14-34f.h.
 *        + Update for new module_param API.
 *        + Module parameters  can now be specified only in the
 *          same format as the kernel boot options.
 *
 *             boot option    old module param 
 *             -----------    ------------------
 *             addr,...       io_port=addr,...
 *             lc:[y|n]       linked_comm=[1|0]
 *             mq:xx          max_queue_depth=xx
 *             tm:[0|1|2]     tag_mode=[0|1|2]
 *             et:[y|n]       ext_tran=[1|0]
 *             of:[y|n]       have_old_firmware=[1|0]
 *
 *          A valid example using the new parameter format is:
 *          modprobe u14-34f "u14-34f=0x340,0x330,lc:y,tm:0,mq:4"
 *
 *          which is equivalent to the old format:
 *          modprobe u14-34f io_port=0x340,0x330 linked_comm=1 tag_mode=0 \
 *                        max_queue_depth=4
 *
 *          With actual module code, u14-34f and u14_34f are equivalent
 *          as module parameter names.
 *
 *      12 Feb 2003 Rev. 8.04 for linux 2.5.60
 *        + Release irq before calling scsi_register.
 *
 *      12 Nov 2002 Rev. 8.02 for linux 2.5.47
 *        + Release driver_lock before calling scsi_register.
 *
 *      11 Nov 2002 Rev. 8.01 for linux 2.5.47
 *        + Fixed bios_param and scsicam_bios_param calling parameters.
 *
 *      28 Oct 2002 Rev. 8.00 for linux 2.5.44-ac4
 *        + Use new tcq and adjust_queue_depth api.
 *        + New command line option (tm:[0-2]) to choose the type of tags:
 *          0 -> disable tagging ; 1 -> simple tags  ; 2 -> ordered tags.
 *          Default is tm:0 (tagged commands disabled).
 *          For compatibility the "tc:" option is an alias of the "tm:"
 *          option; tc:n is equivalent to tm:0 and tc:y is equivalent to
 *          tm:1.
 *
 *      10 Oct 2002 Rev. 7.70 for linux 2.5.42
 *        + Foreport from revision 6.70.
 *
 *      25 Jun 2002 Rev. 6.70 for linux 2.4.19
 *        + Fixed endian-ness problem due to bitfields.
 *
 *      21 Feb 2002 Rev. 6.52 for linux 2.4.18
 *        + Backport from rev. 7.22 (use io_request_lock).
 *
 *      20 Feb 2002 Rev. 7.22 for linux 2.5.5
 *        + Remove any reference to virt_to_bus().
 *        + Fix pio hang while detecting multiple HBAs.
 *
 *      01 Jan 2002 Rev. 7.20 for linux 2.5.1
 *        + Use the dynamic DMA mapping API.
 *
 *      19 Dec 2001 Rev. 7.02 for linux 2.5.1
 *        + Use SCpnt->sc_data_direction if set.
 *        + Use sglist.page instead of sglist.address.
 *
 *      11 Dec 2001 Rev. 7.00 for linux 2.5.1
 *        + Use host->host_lock instead of io_request_lock.
 *
 *       1 May 2001 Rev. 6.05 for linux 2.4.4
 *        + Fix data transfer direction for opcode SEND_CUE_SHEET (0x5d)
 *
 *      25 Jan 2001 Rev. 6.03 for linux 2.4.0
 *        + "check_region" call replaced by "request_region".
 *
 *      22 Nov 2000 Rev. 6.02 for linux 2.4.0-test11
 *        + Removed old scsi error handling support.
 *        + The obsolete boot option flag eh:n is silently ignored.
 *        + Removed error messages while a disk drive is powered up at
 *          boot time.
 *        + Improved boot messages: all tagged capable device are
 *          indicated as "tagged".
 *
 *      16 Sep 1999 Rev. 5.11 for linux 2.2.12 and 2.3.18
 *        + Updated to the new __setup interface for boot command line options.
 *        + When loaded as a module, accepts the new parameter boot_options
 *          which value is a string with the same format of the kernel boot
 *          command line options. A valid example is:
 *          modprobe u14-34f 'boot_options="0x230,0x340,lc:y,mq:4"'
 *
 *      22 Jul 1999 Rev. 5.00 for linux 2.2.10 and 2.3.11
 *        + Removed pre-2.2 source code compatibility.
 *
 *      26 Jul 1998 Rev. 4.33 for linux 2.0.35 and 2.1.111
 *          Added command line option (et:[y|n]) to use the existing
 *          translation (returned by scsicam_bios_param) as disk geometry.
 *          The default is et:n, which uses the disk geometry jumpered
 *          on the board.
 *          The default value et:n is compatible with all previous revisions
 *          of this driver.
 *
 *      28 May 1998 Rev. 4.32 for linux 2.0.33 and 2.1.104
 *          Increased busy timeout from 10 msec. to 200 msec. while
 *          processing interrupts.
 *
 *      18 May 1998 Rev. 4.31 for linux 2.0.33 and 2.1.102
 *          Improved abort handling during the eh recovery process.
 *
 *      13 May 1998 Rev. 4.30 for linux 2.0.33 and 2.1.101
 *          The driver is now fully SMP safe, including the
 *          abort and reset routines.
 *          Added command line options (eh:[y|n]) to choose between
 *          new_eh_code and the old scsi code.
 *          If linux version >= 2.1.101 the default is eh:y, while the eh
 *          option is ignored for previous releases and the old scsi code
 *          is used.
 *
 *      18 Apr 1998 Rev. 4.20 for linux 2.0.33 and 2.1.97
 *          Reworked interrupt handler.
 *
 *      11 Apr 1998 rev. 4.05 for linux 2.0.33 and 2.1.95
 *          Major reliability improvement: when a batch with overlapping
 *          requests is detected, requests are queued one at a time
 *          eliminating any possible board or drive reordering.
 *
 *      10 Apr 1998 rev. 4.04 for linux 2.0.33 and 2.1.95
 *          Improved SMP support (if linux version >= 2.1.95).
 *
 *       9 Apr 1998 rev. 4.03 for linux 2.0.33 and 2.1.94
 *          Performance improvement: when sequential i/o is detected,
 *          always use direct sort instead of reverse sort.
 *
 *       4 Apr 1998 rev. 4.02 for linux 2.0.33 and 2.1.92
 *          io_port is now unsigned long.
 *
 *      17 Mar 1998 rev. 4.01 for linux 2.0.33 and 2.1.88
 *          Use new scsi error handling code (if linux version >= 2.1.88).
 *          Use new interrupt code.
 *
 *      12 Sep 1997 rev. 3.11 for linux 2.0.30 and 2.1.55
 *          Use of udelay inside the wait loops to avoid timeout
 *          problems with fast cpus.
 *          Removed check about useless calls to the interrupt service
 *          routine (reported on SMP systems only).
 *          At initialization time "sorted/unsorted" is displayed instead
 *          of "linked/unlinked" to reinforce the fact that "linking" is
 *          nothing but "elevator sorting" in the actual implementation.
 *
 *      17 May 1997 rev. 3.10 for linux 2.0.30 and 2.1.38
 *          Use of serial_number_at_timeout in abort and reset processing.
 *          Use of the __initfunc and __initdata macro in setup code.
 *          Minor cleanups in the list_statistics code.
 *
 *      24 Feb 1997 rev. 3.00 for linux 2.0.29 and 2.1.26
 *          When loading as a module, parameter passing is now supported
 *          both in 2.0 and in 2.1 style.
 *          Fixed data transfer direction for some SCSI opcodes.
 *          Immediate acknowledge to request sense commands.
 *          Linked commands to each disk device are now reordered by elevator
 *          sorting. Rare cases in which reordering of write requests could
 *          cause wrong results are managed.
 *
 *      18 Jan 1997 rev. 2.60 for linux 2.1.21 and 2.0.28
 *          Added command line options to enable/disable linked commands
 *          (lc:[y|n]), old firmware support (of:[y|n]) and to set the max
 *          queue depth (mq:xx). Default is "u14-34f=lc:n,of:n,mq:8".
 *          Improved command linking.
 *
 *       8 Jan 1997 rev. 2.50 for linux 2.1.20 and 2.0.27
 *          Added linked command support.
 *
 *       3 Dec 1996 rev. 2.40 for linux 2.1.14 and 2.0.27
 *          Added queue depth adjustment.
 *
 *      22 Nov 1996 rev. 2.30 for linux 2.1.12 and 2.0.26
 *          The list of i/o ports to be probed can be overwritten by the
 *          "u14-34f=port0,port1,...." boot command line option.
 *          Scatter/gather lists are now allocated by a number of kmalloc
 *          calls, in order to avoid the previous size limit of 64Kb.
 *
 *      16 Nov 1996 rev. 2.20 for linux 2.1.10 and 2.0.25
 *          Added multichannel support.
 *
 *      27 Sep 1996 rev. 2.12 for linux 2.1.0
 *          Portability cleanups (virtual/bus addressing, little/big endian
 *          support).
 *
 *      09 Jul 1996 rev. 2.11 for linux 2.0.4
 *          "Data over/under-run" no longer implies a redo on all targets.
 *          Number of internal retries is now limited.
 *
 *      16 Apr 1996 rev. 2.10 for linux 1.3.90
 *          New argument "reset_flags" to the reset routine.
 *
 *      21 Jul 1995 rev. 2.02 for linux 1.3.11
 *          Fixed Data Transfer Direction for some SCSI commands.
 *
 *      13 Jun 1995 rev. 2.01 for linux 1.2.10
 *          HAVE_OLD_UX4F_FIRMWARE should be defined for U34F boards when
 *          the firmware prom is not the latest one (28008-006).
 *
 *      11 Mar 1995 rev. 2.00 for linux 1.2.0
 *          Fixed a bug which prevented media change detection for removable
 *          disk drives.
 *
 *      23 Feb 1995 rev. 1.18 for linux 1.1.94
 *          Added a check for scsi_register returning NULL.
 *
 *      11 Feb 1995 rev. 1.17 for linux 1.1.91
 *          U14F qualified to run with 32 sglists.
 *          Now DEBUG_RESET is disabled by default.
 *
 *       9 Feb 1995 rev. 1.16 for linux 1.1.90
 *          Use host->wish_block instead of host->block.
 *
 *       8 Feb 1995 rev. 1.15 for linux 1.1.89
 *          Cleared target_time_out counter while performing a reset.
 *
 *      28 Jan 1995 rev. 1.14 for linux 1.1.86
 *          Added module support.
 *          Log and do a retry when a disk drive returns a target status
 *          different from zero on a recovered error.
 *          Auto detects if U14F boards have an old firmware revision.
 *          Max number of scatter/gather lists set to 16 for all boards
 *          (most installation run fine using 33 sglists, while other
 *          has problems when using more than 16).
 *
 *      16 Jan 1995 rev. 1.13 for linux 1.1.81
 *          Display a message if check_region detects a port address
 *          already in use.
 *
 *      15 Dec 1994 rev. 1.12 for linux 1.1.74
 *          The host->block flag is set for all the detected ISA boards.
 *
 *      30 Nov 1994 rev. 1.11 for linux 1.1.68
 *          Redo i/o on target status CHECK_CONDITION for TYPE_DISK only.
 *          Added optional support for using a single board at a time.
 *
 *      14 Nov 1994 rev. 1.10 for linux 1.1.63
 *
 *      28 Oct 1994 rev. 1.09 for linux 1.1.58  Final BETA release.
 *      16 Jul 1994 rev. 1.00 for linux 1.1.29  Initial ALPHA release.
 *
 *          This driver is a total replacement of the original UltraStor
 *          scsi driver, but it supports ONLY the 14F and 34F boards.
 *          It can be configured in the same kernel in which the original
 *          ultrastor driver is configured to allow the original U24F
 *          support.
 *
 *          Multiple U14F and/or U34F host adapters are supported.
 *
 *  Copyright (C) 1994-2003 Dario Ballabio (ballabio_dario@emc.com)
 *
 *  Alternate email: dario.ballabio@inwind.it, dario.ballabio@tiscalinet.it
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that redistributions of source
 *  code retain the above copyright notice and this comment without
 *  modification.
 *
 *      WARNING: if your 14/34F board has an old firmware revision (see below)
 *               you must change "#undef" into "#define" in the following
 *               statement.
 */
#undef HAVE_OLD_UX4F_FIRMWARE
/*
 *  The UltraStor 14F, 24F, and 34F are a family of intelligent, high
 *  performance SCSI-2 host adapters.
 *  Here is the scoop on the various models:
 *
 *  14F - ISA first-party DMA HA with floppy support and WD1003 emulation.
 *  24F - EISA Bus Master HA with floppy support and WD1003 emulation.
 *  34F - VESA Local-Bus Bus Master HA (no WD1003 emulation).
 *
 *  This code has been tested with up to two U14F boards, using both
 *  firmware 28004-005/38004-004 (BIOS rev. 2.00) and the latest firmware
 *  28004-006/38004-005 (BIOS rev. 2.01).
 *
 *  The latest firmware is required in order to get reliable operations when
 *  clustering is enabled. ENABLE_CLUSTERING provides a performance increase
 *  up to 50% on sequential access.
 *
 *  Since the struct scsi_host_template structure is shared among all 14F and 34F,
 *  the last setting of use_clustering is in effect for all of these boards.
 *
 *  Here a sample configuration using two U14F boards:
 *
 U14F0: ISA 0x330, BIOS 0xc8000, IRQ 11, DMA 5, SG 32, MB 16, of:n, lc:y, mq:8.
 U14F1: ISA 0x340, BIOS 0x00000, IRQ 10, DMA 6, SG 32, MB 16, of:n, lc:y, mq:8.
 *
 *  The boot controller must have its BIOS enabled, while other boards can
 *  have their BIOS disabled, or enabled to an higher address.
 *  Boards are named Ux4F0, Ux4F1..., according to the port address order in
 *  the io_port[] array.
 *
 *  The following facts are based on real testing results (not on
 *  documentation) on the above U14F board.
 *
 *  - The U14F board should be jumpered for bus on time less or equal to 7
 *    microseconds, while the default is 11 microseconds. This is order to
 *    get acceptable performance while using floppy drive and hard disk
 *    together. The jumpering for 7 microseconds is: JP13 pin 15-16,
 *    JP14 pin 7-8 and pin 9-10.
 *    The reduction has a little impact on scsi performance.
 *
 *  - If scsi bus length exceeds 3m., the scsi bus speed needs to be reduced
 *    from 10Mhz to 5Mhz (do this by inserting a jumper on JP13 pin 7-8).
 *
 *  - If U14F on board firmware is older than 28004-006/38004-005,
 *    the U14F board is unable to provide reliable operations if the scsi
 *    request length exceeds 16Kbyte. When this length is exceeded the
 *    behavior is:
 *    - adapter_status equal 0x96 or 0xa3 or 0x93 or 0x94;
 *    - adapter_status equal 0 and target_status equal 2 on for all targets
 *      in the next operation following the reset.
 *    This sequence takes a long time (>3 seconds), so in the meantime
 *    the SD_TIMEOUT in sd.c could expire giving rise to scsi aborts
 *    (SD_TIMEOUT has been increased from 3 to 6 seconds in 1.1.31).
 *    Because of this I had to DISABLE_CLUSTERING and to work around the
 *    bus reset in the interrupt service routine, returning DID_BUS_BUSY
 *    so that the operations are retried without complains from the scsi.c
 *    code.
 *    Any reset of the scsi bus is going to kill tape operations, since
 *    no retry is allowed for tapes. Bus resets are more likely when the
 *    scsi bus is under heavy load.
 *    Requests using scatter/gather have a maximum length of 16 x 1024 bytes
 *    when DISABLE_CLUSTERING is in effect, but unscattered requests could be
 *    larger than 16Kbyte.
 *
 *    The new firmware has fixed all the above problems.
 *
 *  For U34F boards the latest bios prom is 38008-002 (BIOS rev. 2.01),
 *  the latest firmware prom is 28008-006. Older firmware 28008-005 has
 *  problems when using more than 16 scatter/gather lists.
 *
 *  The list of i/o ports to be probed can be totally replaced by the
 *  boot command line option: "u14-34f=port0,port1,port2,...", where the
 *  port0, port1... arguments are ISA/VESA addresses to be probed.
 *  For example using "u14-34f=0x230,0x340", the driver probes only the two
 *  addresses 0x230 and 0x340 in this order; "u14-34f=0" totally disables
 *  this driver.
 *
 *  After the optional list of detection probes, other possible command line
 *  options are:
 *
 *  et:y  use disk geometry returned by scsicam_bios_param;
 *  et:n  use disk geometry jumpered on the board;
 *  lc:y  enables linked commands;
 *  lc:n  disables linked commands;
 *  tm:0  disables tagged commands (same as tc:n);
 *  tm:1  use simple queue tags (same as tc:y);
 *  tm:2  use ordered queue tags (same as tc:2);
 *  of:y  enables old firmware support;
 *  of:n  disables old firmware support;
 *  mq:xx set the max queue depth to the value xx (2 <= xx <= 8).
 *
 *  The default value is: "u14-34f=lc:n,of:n,mq:8,tm:0,et:n".
 *  An example using the list of detection probes could be:
 *  "u14-34f=0x230,0x340,lc:y,tm:2,of:n,mq:4,et:n".
 *
 *  When loading as a module, parameters can be specified as well.
 *  The above example would be (use 1 in place of y and 0 in place of n):
 *
 *  modprobe u14-34f io_port=0x230,0x340 linked_comm=1 have_old_firmware=0 \
 *                max_queue_depth=4 ext_tran=0 tag_mode=2
 *
 *  ----------------------------------------------------------------------------
 *  In this implementation, linked commands are designed to work with any DISK
 *  or CD-ROM, since this linking has only the intent of clustering (time-wise)
 *  and reordering by elevator sorting commands directed to each device,
 *  without any relation with the actual SCSI protocol between the controller
 *  and the device.
 *  If Q is the queue depth reported at boot time for each device (also named
 *  cmds/lun) and Q > 2, whenever there is already an active command to the
 *  device all other commands to the same device  (up to Q-1) are kept waiting
 *  in the elevator sorting queue. When the active command completes, the
 *  commands in this queue are sorted by sector address. The sort is chosen
 *  between increasing or decreasing by minimizing the seek distance between
 *  the sector of the commands just completed and the sector of the first
 *  command in the list to be sorted.
 *  Trivial math assures that the unsorted average seek distance when doing
 *  random seeks over S sectors is S/3.
 *  When (Q-1) requests are uniformly distributed over S sectors, the average
 *  distance between two adjacent requests is S/((Q-1) + 1), so the sorted
 *  average seek distance for (Q-1) random requests over S sectors is S/Q.
 *  The elevator sorting hence divides the seek distance by a factor Q/3.
 *  The above pure geometric remarks are valid in all cases and the
 *  driver effectively reduces the seek distance by the predicted factor
 *  when there are Q concurrent read i/o operations on the device, but this
 *  does not necessarily results in a noticeable performance improvement:
 *  your mileage may vary....
 *
 *  Note: command reordering inside a batch of queued commands could cause
 *        wrong results only if there is at least one write request and the
 *        intersection (sector-wise) of all requests is not empty.
 *        When the driver detects a batch including overlapping requests
 *        (a really rare event) strict serial (pid) order is enforced.
 *  ----------------------------------------------------------------------------
 *
 *  The boards are named Ux4F0, Ux4F1,... according to the detection order.
 *
 *  In order to support multiple ISA boards in a reliable way,
 *  the driver sets host->wish_block = TRUE for all ISA boards.
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/byteorder.h>
#include <linux/proc_fs.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/stat.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/spinlock.h>
#include <asm/dma.h>
#include <asm/irq.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>

static int u14_34f_detect(struct scsi_host_template *);
static int u14_34f_release(struct Scsi_Host *);
static int u14_34f_queuecommand(struct scsi_cmnd *, void (*done)(struct scsi_cmnd *));
static int u14_34f_eh_abort(struct scsi_cmnd *);
static int u14_34f_eh_host_reset(struct scsi_cmnd *);
static int u14_34f_bios_param(struct scsi_device *, struct block_device *,
                              sector_t, int *);
static int u14_34f_slave_configure(struct scsi_device *);

static struct scsi_host_template driver_template = {
                .name                    = "UltraStor 14F/34F rev. 8.10.00 ",
                .detect                  = u14_34f_detect,
                .release                 = u14_34f_release,
                .queuecommand            = u14_34f_queuecommand,
                .eh_abort_handler        = u14_34f_eh_abort,
                .eh_host_reset_handler   = u14_34f_eh_host_reset,
                .bios_param              = u14_34f_bios_param,
                .slave_configure         = u14_34f_slave_configure,
                .this_id                 = 7,
                .unchecked_isa_dma       = 1,
                .use_clustering          = ENABLE_CLUSTERING
                };

#if !defined(__BIG_ENDIAN_BITFIELD) && !defined(__LITTLE_ENDIAN_BITFIELD)
#error "Adjust your <asm/byteorder.h> defines"
#endif

/* Values for the PRODUCT_ID ports for the 14/34F */
#define PRODUCT_ID1  0x56
#define PRODUCT_ID2  0x40        /* NOTE: Only upper nibble is used */

/* Subversion values */
#define ISA  0
#define ESA 1

#define OP_HOST_ADAPTER   0x1
#define OP_SCSI           0x2
#define OP_RESET          0x4
#define DTD_SCSI          0x0
#define DTD_IN            0x1
#define DTD_OUT           0x2
#define DTD_NONE          0x3
#define HA_CMD_INQUIRY    0x1
#define HA_CMD_SELF_DIAG  0x2
#define HA_CMD_READ_BUFF  0x3
#define HA_CMD_WRITE_BUFF 0x4

#undef  DEBUG_LINKED_COMMANDS
#undef  DEBUG_DETECT
#undef  DEBUG_INTERRUPT
#undef  DEBUG_RESET
#undef  DEBUG_GENERATE_ERRORS
#undef  DEBUG_GENERATE_ABORTS
#undef  DEBUG_GEOMETRY

#define MAX_ISA 3
#define MAX_VESA 1
#define MAX_EISA 0
#define MAX_PCI 0
#define MAX_BOARDS (MAX_ISA + MAX_VESA + MAX_EISA + MAX_PCI)
#define MAX_CHANNEL 1
#define MAX_LUN 8
#define MAX_TARGET 8
#define MAX_MAILBOXES 16
#define MAX_SGLIST 32
#define MAX_SAFE_SGLIST 16
#define MAX_INTERNAL_RETRIES 64
#define MAX_CMD_PER_LUN 2
#define MAX_TAGGED_CMD_PER_LUN (MAX_MAILBOXES - MAX_CMD_PER_LUN)

#define SKIP ULONG_MAX
#define FALSE 0
#define TRUE 1
#define FREE 0
#define IN_USE   1
#define LOCKED   2
#define IN_RESET 3
#define IGNORE   4
#define READY    5
#define ABORTING 6
#define NO_DMA  0xff
#define MAXLOOP  10000
#define TAG_DISABLED 0
#define TAG_SIMPLE   1
#define TAG_ORDERED  2

#define REG_LCL_MASK      0
#define REG_LCL_INTR      1
#define REG_SYS_MASK      2
#define REG_SYS_INTR      3
#define REG_PRODUCT_ID1   4
#define REG_PRODUCT_ID2   5
#define REG_CONFIG1       6
#define REG_CONFIG2       7
#define REG_OGM           8
#define REG_ICM           12
#define REGION_SIZE       13UL
#define BSY_ASSERTED      0x01
#define IRQ_ASSERTED      0x01
#define CMD_RESET         0xc0
#define CMD_OGM_INTR      0x01
#define CMD_CLR_INTR      0x01
#define CMD_ENA_INTR      0x81
#define ASOK              0x00
#define ASST              0x91

#define YESNO(a) ((a) ? 'y' : 'n')
#define TLDEV(type) ((type) == TYPE_DISK || (type) == TYPE_ROM)

#define PACKED          __attribute__((packed))

struct sg_list {
   unsigned int address;                /* Segment Address */
   unsigned int num_bytes;              /* Segment Length */
   };

/* MailBox SCSI Command Packet */
struct mscp {

#if defined(__BIG_ENDIAN_BITFIELD)
   unsigned char sg:1, ca:1, dcn:1, xdir:2, opcode:3;
   unsigned char lun: 3, channel:2, target:3;
#else
   unsigned char opcode: 3,             /* type of command */
                 xdir: 2,               /* data transfer direction */
                 dcn: 1,                /* disable disconnect */
                 ca: 1,                 /* use cache (if available) */
                 sg: 1;                 /* scatter/gather operation */
   unsigned char target: 3,             /* SCSI target id */
                 channel: 2,            /* SCSI channel number */
                 lun: 3;                /* SCSI logical unit number */
#endif

   unsigned int data_address PACKED;    /* transfer data pointer */
   unsigned int data_len PACKED;        /* length in bytes */
   unsigned int link_address PACKED;    /* for linking command chains */
   unsigned char clink_id;              /* identifies command in chain */
   unsigned char use_sg;                /* (if sg is set) 8 bytes per list */
   unsigned char sense_len;
   unsigned char cdb_len;               /* 6, 10, or 12 */
   unsigned char cdb[12];               /* SCSI Command Descriptor Block */
   unsigned char adapter_status;        /* non-zero indicates HA error */
   unsigned char target_status;         /* non-zero indicates target error */
   unsigned int sense_addr PACKED;

   /* Additional fields begin here. */
   struct scsi_cmnd *SCpnt;
   unsigned int cpp_index;              /* cp index */

   /* All the cp structure is zero filled by queuecommand except the
      following CP_TAIL_SIZE bytes, initialized by detect */
   dma_addr_t cp_dma_addr; /* dma handle for this cp structure */
   struct sg_list *sglist; /* pointer to the allocated SG list */
   };

#define CP_TAIL_SIZE (sizeof(struct sglist *) + sizeof(dma_addr_t))

struct hostdata {
   struct mscp cp[MAX_MAILBOXES];       /* Mailboxes for this board */
   unsigned int cp_stat[MAX_MAILBOXES]; /* FREE, IN_USE, LOCKED, IN_RESET */
   unsigned int last_cp_used;           /* Index of last mailbox used */
   unsigned int iocount;                /* Total i/o done for this board */
   int board_number;                    /* Number of this board */
   char board_name[16];                 /* Name of this board */
   int in_reset;                        /* True if board is doing a reset */
   int target_to[MAX_TARGET][MAX_CHANNEL]; /* N. of timeout errors on target */
   int target_redo[MAX_TARGET][MAX_CHANNEL]; /* If TRUE redo i/o on target */
   unsigned int retries;                /* Number of internal retries */
   unsigned long last_retried_pid;      /* Pid of last retried command */
   unsigned char subversion;            /* Bus type, either ISA or ESA */
   struct pci_dev *pdev;                /* Always NULL */
   unsigned char heads;
   unsigned char sectors;
   char board_id[256];                  /* data from INQUIRY on this board */
   };

static struct Scsi_Host *sh[MAX_BOARDS + 1];
static const char *driver_name = "Ux4F";
static char sha[MAX_BOARDS];
static DEFINE_SPINLOCK(driver_lock);

/* Initialize num_boards so that ihdlr can work while detect is in progress */
static unsigned int num_boards = MAX_BOARDS;

static unsigned long io_port[] = {

   /* Space for MAX_INT_PARAM ports usable while loading as a module */
   SKIP,    SKIP,   SKIP,   SKIP,   SKIP,   SKIP,   SKIP,   SKIP,
   SKIP,    SKIP,

   /* Possible ISA/VESA ports */
   0x330, 0x340, 0x230, 0x240, 0x210, 0x130, 0x140,

   /* End of list */
   0x0
   };

#define HD(board) ((struct hostdata *) &sh[board]->hostdata)
#define BN(board) (HD(board)->board_name)

/* Device is Little Endian */
#define H2DEV(x) cpu_to_le32(x)
#define DEV2H(x) le32_to_cpu(x)

static irqreturn_t do_interrupt_handler(int, void *, struct pt_regs *);
static void flush_dev(struct scsi_device *, unsigned long, unsigned int, unsigned int);
static int do_trace = FALSE;
static int setup_done = FALSE;
static int link_statistics;
static int ext_tran = FALSE;

#if defined(HAVE_OLD_UX4F_FIRMWARE)
static int have_old_firmware = TRUE;
#else
static int have_old_firmware = FALSE;
#endif

#if defined(CONFIG_SCSI_U14_34F_TAGGED_QUEUE)
static int tag_mode = TAG_SIMPLE;
#else
static int tag_mode = TAG_DISABLED;
#endif

#if defined(CONFIG_SCSI_U14_34F_LINKED_COMMANDS)
static int linked_comm = TRUE;
#else
static int linked_comm = FALSE;
#endif

#if defined(CONFIG_SCSI_U14_34F_MAX_TAGS)
static int max_queue_depth = CONFIG_SCSI_U14_34F_MAX_TAGS;
#else
static int max_queue_depth = MAX_CMD_PER_LUN;
#endif

#define MAX_INT_PARAM 10
#define MAX_BOOT_OPTIONS_SIZE 256
static char boot_options[MAX_BOOT_OPTIONS_SIZE];

#if defined(MODULE)
#include <linux/module.h>
#include <linux/moduleparam.h>

module_param_string(u14_34f, boot_options, MAX_BOOT_OPTIONS_SIZE, 0);
MODULE_PARM_DESC(u14_34f, " equivalent to the \"u14-34f=...\" kernel boot " \
"option." \
"      Example: modprobe u14-34f \"u14_34f=0x340,0x330,lc:y,tm:0,mq:4\"");
MODULE_AUTHOR("Dario Ballabio");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("UltraStor 14F/34F SCSI Driver");

#endif

static int u14_34f_slave_configure(struct scsi_device *dev) {
   int j, tqd, utqd;
   char *tag_suffix, *link_suffix;
   struct Scsi_Host *host = dev->host;

   j = ((struct hostdata *) host->hostdata)->board_number;

   utqd = MAX_CMD_PER_LUN;
   tqd = max_queue_depth;

   if (TLDEV(dev->type) && dev->tagged_supported)

      if (tag_mode == TAG_SIMPLE) {
         scsi_adjust_queue_depth(dev, MSG_SIMPLE_TAG, tqd);
         tag_suffix = ", simple tags";
         }
      else if (tag_mode == TAG_ORDERED) {
         scsi_adjust_queue_depth(dev, MSG_ORDERED_TAG, tqd);
         tag_suffix = ", ordered tags";
         }
      else {
         scsi_adjust_queue_depth(dev, 0, tqd);
         tag_suffix = ", no tags";
         }

   else if (TLDEV(dev->type) && linked_comm) {
      scsi_adjust_queue_depth(dev, 0, tqd);
      tag_suffix = ", untagged";
      }

   else {
      scsi_adjust_queue_depth(dev, 0, utqd);
      tag_suffix = "";
      }

   if (TLDEV(dev->type) && linked_comm && dev->queue_depth > 2)
      link_suffix = ", sorted";
   else if (TLDEV(dev->type))
      link_suffix = ", unsorted";
   else
      link_suffix = "";

   sdev_printk(KERN_INFO, dev, "cmds/lun %d%s%s.\n",
          dev->queue_depth, link_suffix, tag_suffix);

   return FALSE;
}

static int wait_on_busy(unsigned long iobase, unsigned int loop) {

   while (inb(iobase + REG_LCL_INTR) & BSY_ASSERTED) {
      udelay(1L);
      if (--loop == 0) return TRUE;
      }

   return FALSE;
}

static int board_inquiry(unsigned int j) {
   struct mscp *cpp;
   dma_addr_t id_dma_addr;
   unsigned int time, limit = 0;

   id_dma_addr = pci_map_single(HD(j)->pdev, HD(j)->board_id,
                    sizeof(HD(j)->board_id), PCI_DMA_BIDIRECTIONAL);
   cpp = &HD(j)->cp[0];
   cpp->cp_dma_addr = pci_map_single(HD(j)->pdev, cpp, sizeof(struct mscp),
                                     PCI_DMA_BIDIRECTIONAL);
   memset(cpp, 0, sizeof(struct mscp) - CP_TAIL_SIZE);
   cpp->opcode = OP_HOST_ADAPTER;
   cpp->xdir = DTD_IN;
   cpp->data_address = H2DEV(id_dma_addr);
   cpp->data_len = H2DEV(sizeof(HD(j)->board_id));
   cpp->cdb_len = 6;
   cpp->cdb[0] = HA_CMD_INQUIRY;

   if (wait_on_busy(sh[j]->io_port, MAXLOOP)) {
      printk("%s: board_inquiry, adapter busy.\n", BN(j));
      return TRUE;
      }

   HD(j)->cp_stat[0] = IGNORE;

   /* Clear the interrupt indication */
   outb(CMD_CLR_INTR, sh[j]->io_port + REG_SYS_INTR);

   /* Store pointer in OGM address bytes */
   outl(H2DEV(cpp->cp_dma_addr), sh[j]->io_port + REG_OGM);

   /* Issue OGM interrupt */
   outb(CMD_OGM_INTR, sh[j]->io_port + REG_LCL_INTR);

   spin_unlock_irq(&driver_lock);
   time = jiffies;
   while ((jiffies - time) < HZ && limit++ < 20000) udelay(100L);
   spin_lock_irq(&driver_lock);

   if (cpp->adapter_status || HD(j)->cp_stat[0] != FREE) {
      HD(j)->cp_stat[0] = FREE;
      printk("%s: board_inquiry, err 0x%x.\n", BN(j), cpp->adapter_status);
      return TRUE;
      }

   pci_unmap_single(HD(j)->pdev, cpp->cp_dma_addr, sizeof(struct mscp),
                    PCI_DMA_BIDIRECTIONAL);
   pci_unmap_single(HD(j)->pdev, id_dma_addr, sizeof(HD(j)->board_id),
                    PCI_DMA_BIDIRECTIONAL);
   return FALSE;
}

static int port_detect \
      (unsigned long port_base, unsigned int j, struct scsi_host_template *tpnt) {
   unsigned char irq, dma_channel, subversion, i;
   unsigned char in_byte;
   char *bus_type, dma_name[16];

   /* Allowed BIOS base addresses (NULL indicates reserved) */
   unsigned long bios_segment_table[8] = {
      0,
      0xc4000, 0xc8000, 0xcc000, 0xd0000,
      0xd4000, 0xd8000, 0xdc000
      };

   /* Allowed IRQs */
   unsigned char interrupt_table[4] = { 15, 14, 11, 10 };

   /* Allowed DMA channels for ISA (0 indicates reserved) */
   unsigned char dma_channel_table[4] = { 5, 6, 7, 0 };

   /* Head/sector mappings */
   struct {
      unsigned char heads;
      unsigned char sectors;
      } mapping_table[4] = {
           { 16, 63 }, { 64, 32 }, { 64, 63 }, { 64, 32 }
           };

   struct config_1 {

#if defined(__BIG_ENDIAN_BITFIELD)
      unsigned char dma_channel: 2, interrupt:2,
                    removable_disks_as_fixed:1, bios_segment: 3;
#else
      unsigned char bios_segment: 3, removable_disks_as_fixed: 1,
                    interrupt: 2, dma_channel: 2;
#endif

      } config_1;

   struct config_2 {

#if defined(__BIG_ENDIAN_BITFIELD)
      unsigned char tfr_port: 2, bios_drive_number: 1,
                    mapping_mode: 2, ha_scsi_id: 3;
#else
      unsigned char ha_scsi_id: 3, mapping_mode: 2,
                    bios_drive_number: 1, tfr_port: 2;
#endif

      } config_2;

   char name[16];

   sprintf(name, "%s%d", driver_name, j);

   if (!request_region(port_base, REGION_SIZE, driver_name)) {
#if defined(DEBUG_DETECT)
      printk("%s: address 0x%03lx in use, skipping probe.\n", name, port_base);
#endif
      goto fail;
      }

   spin_lock_irq(&driver_lock);

   if (inb(port_base + REG_PRODUCT_ID1) != PRODUCT_ID1) goto freelock;

   in_byte = inb(port_base + REG_PRODUCT_ID2);

   if ((in_byte & 0xf0) != PRODUCT_ID2) goto freelock;

   *(char *)&config_1 = inb(port_base + REG_CONFIG1);
   *(char *)&config_2 = inb(port_base + REG_CONFIG2);

   irq = interrupt_table[config_1.interrupt];
   dma_channel = dma_channel_table[config_1.dma_channel];
   subversion = (in_byte & 0x0f);

   /* Board detected, allocate its IRQ */
   if (request_irq(irq, do_interrupt_handler,
             SA_INTERRUPT | ((subversion == ESA) ? SA_SHIRQ : 0),
             driver_name, (void *) &sha[j])) {
      printk("%s: unable to allocate IRQ %u, detaching.\n", name, irq);
      goto freelock;
      }

   if (subversion == ISA && request_dma(dma_channel, driver_name)) {
      printk("%s: unable to allocate DMA channel %u, detaching.\n",
             name, dma_channel);
      goto freeirq;
      }

   if (have_old_firmware) tpnt->use_clustering = DISABLE_CLUSTERING;

   spin_unlock_irq(&driver_lock);
   sh[j] = scsi_register(tpnt, sizeof(struct hostdata));
   spin_lock_irq(&driver_lock);

   if (sh[j] == NULL) {
      printk("%s: unable to register host, detaching.\n", name);
      goto freedma;
      }

   sh[j]->io_port = port_base;
   sh[j]->unique_id = port_base;
   sh[j]->n_io_port = REGION_SIZE;
   sh[j]->base = bios_segment_table[config_1.bios_segment];
   sh[j]->irq = irq;
   sh[j]->sg_tablesize = MAX_SGLIST;
   sh[j]->this_id = config_2.ha_scsi_id;
   sh[j]->can_queue = MAX_MAILBOXES;
   sh[j]->cmd_per_lun = MAX_CMD_PER_LUN;

#if defined(DEBUG_DETECT)
   {
   unsigned char sys_mask, lcl_mask;

   sys_mask = inb(sh[j]->io_port + REG_SYS_MASK);
   lcl_mask = inb(sh[j]->io_port + REG_LCL_MASK);
   printk("SYS_MASK 0x%x, LCL_MASK 0x%x.\n", sys_mask, lcl_mask);
   }
#endif

   /* Probably a bogus host scsi id, set it to the dummy value */
   if (sh[j]->this_id == 0) sh[j]->this_id = -1;

   /* If BIOS is disabled, force enable interrupts */
   if (sh[j]->base == 0) outb(CMD_ENA_INTR, sh[j]->io_port + REG_SYS_MASK);

   memset(HD(j), 0, sizeof(struct hostdata));
   HD(j)->heads = mapping_table[config_2.mapping_mode].heads;
   HD(j)->sectors = mapping_table[config_2.mapping_mode].sectors;
   HD(j)->subversion = subversion;
   HD(j)->pdev = NULL;
   HD(j)->board_number = j;

   if (have_old_firmware) sh[j]->sg_tablesize = MAX_SAFE_SGLIST;

   if (HD(j)->subversion == ESA) {
      sh[j]->unchecked_isa_dma = FALSE;
      sh[j]->dma_channel = NO_DMA;
      sprintf(BN(j), "U34F%d", j);
      bus_type = "VESA";
      }
   else {
      unsigned long flags;
      sh[j]->unchecked_isa_dma = TRUE;

      flags=claim_dma_lock();
      disable_dma(dma_channel);
      clear_dma_ff(dma_channel);
      set_dma_mode(dma_channel, DMA_MODE_CASCADE);
      enable_dma(dma_channel);
      release_dma_lock(flags);

      sh[j]->dma_channel = dma_channel;
      sprintf(BN(j), "U14F%d", j);
      bus_type = "ISA";
      }

   sh[j]->max_channel = MAX_CHANNEL - 1;
   sh[j]->max_id = MAX_TARGET;
   sh[j]->max_lun = MAX_LUN;

   if (HD(j)->subversion == ISA && !board_inquiry(j)) {
      HD(j)->board_id[40] = 0;

      if (strcmp(&HD(j)->board_id[32], "06000600")) {
         printk("%s: %s.\n", BN(j), &HD(j)->board_id[8]);
         printk("%s: firmware %s is outdated, FW PROM should be 28004-006.\n",
                BN(j), &HD(j)->board_id[32]);
         sh[j]->hostt->use_clustering = DISABLE_CLUSTERING;
         sh[j]->sg_tablesize = MAX_SAFE_SGLIST;
         }
      }

   if (dma_channel == NO_DMA) sprintf(dma_name, "%s", "BMST");
   else                       sprintf(dma_name, "DMA %u", dma_channel);

   spin_unlock_irq(&driver_lock);

   for (i = 0; i < sh[j]->can_queue; i++)
      HD(j)->cp[i].cp_dma_addr = pci_map_single(HD(j)->pdev,
            &HD(j)->cp[i], sizeof(struct mscp), PCI_DMA_BIDIRECTIONAL);

   for (i = 0; i < sh[j]->can_queue; i++)
      if (! ((&HD(j)->cp[i])->sglist = kmalloc(
            sh[j]->sg_tablesize * sizeof(struct sg_list),
            (sh[j]->unchecked_isa_dma ? GFP_DMA : 0) | GFP_ATOMIC))) {
         printk("%s: kmalloc SGlist failed, mbox %d, detaching.\n", BN(j), i);
         goto release;
         }

   if (max_queue_depth > MAX_TAGGED_CMD_PER_LUN)
       max_queue_depth = MAX_TAGGED_CMD_PER_LUN;

   if (max_queue_depth < MAX_CMD_PER_LUN) max_queue_depth = MAX_CMD_PER_LUN;

   if (tag_mode != TAG_DISABLED && tag_mode != TAG_SIMPLE)
      tag_mode = TAG_ORDERED;

   if (j == 0) {
      printk("UltraStor 14F/34F: Copyright (C) 1994-2003 Dario Ballabio.\n");
      printk("%s config options -> of:%c, tm:%d, lc:%c, mq:%d, et:%c.\n",
             driver_name, YESNO(have_old_firmware), tag_mode,
             YESNO(linked_comm), max_queue_depth, YESNO(ext_tran));
      }

   printk("%s: %s 0x%03lx, BIOS 0x%05x, IRQ %u, %s, SG %d, MB %d.\n",
          BN(j), bus_type, (unsigned long)sh[j]->io_port, (int)sh[j]->base,
          sh[j]->irq, dma_name, sh[j]->sg_tablesize, sh[j]->can_queue);

   if (sh[j]->max_id > 8 || sh[j]->max_lun > 8)
      printk("%s: wide SCSI support enabled, max_id %u, max_lun %u.\n",
             BN(j), sh[j]->max_id, sh[j]->max_lun);

   for (i = 0; i <= sh[j]->max_channel; i++)
      printk("%s: SCSI channel %u enabled, host target ID %d.\n",
             BN(j), i, sh[j]->this_id);

   return TRUE;

freedma:
   if (subversion == ISA) free_dma(dma_channel);
freeirq:
   free_irq(irq, &sha[j]);
freelock:
   spin_unlock_irq(&driver_lock);
   release_region(port_base, REGION_SIZE);
fail:
   return FALSE;

release:
   u14_34f_release(sh[j]);
   return FALSE;
}

static void internal_setup(char *str, int *ints) {
   int i, argc = ints[0];
   char *cur = str, *pc;

   if (argc > 0) {

      if (argc > MAX_INT_PARAM) argc = MAX_INT_PARAM;

      for (i = 0; i < argc; i++) io_port[i] = ints[i + 1];

      io_port[i] = 0;
      setup_done = TRUE;
      }

   while (cur && (pc = strchr(cur, ':'))) {
      int val = 0, c = *++pc;

      if (c == 'n' || c == 'N') val = FALSE;
      else if (c == 'y' || c == 'Y') val = TRUE;
      else val = (int) simple_strtoul(pc, NULL, 0);

      if (!strncmp(cur, "lc:", 3)) linked_comm = val;
      else if (!strncmp(cur, "of:", 3)) have_old_firmware = val;
      else if (!strncmp(cur, "tm:", 3)) tag_mode = val;
      else if (!strncmp(cur, "tc:", 3)) tag_mode = val;
      else if (!strncmp(cur, "mq:", 3))  max_queue_depth = val;
      else if (!strncmp(cur, "ls:", 3))  link_statistics = val;
      else if (!strncmp(cur, "et:", 3))  ext_tran = val;

      if ((cur = strchr(cur, ','))) ++cur;
      }

   return;
}

static int option_setup(char *str) {
   int ints[MAX_INT_PARAM];
   char *cur = str;
   int i = 1;

   while (cur && isdigit(*cur) && i <= MAX_INT_PARAM) {
      ints[i++] = simple_strtoul(cur, NULL, 0);

      if ((cur = strchr(cur, ',')) != NULL) cur++;
   }

   ints[0] = i - 1;
   internal_setup(cur, ints);
   return 1;
}

static int u14_34f_detect(struct scsi_host_template *tpnt) {
   unsigned int j = 0, k;

   tpnt->proc_name = "u14-34f";

   if(strlen(boot_options)) option_setup(boot_options);

#if defined(MODULE)
   /* io_port could have been modified when loading as a module */
   if(io_port[0] != SKIP) {
      setup_done = TRUE;
      io_port[MAX_INT_PARAM] = 0;
      }
#endif

   for (k = 0; k < MAX_BOARDS + 1; k++) sh[k] = NULL;

   for (k = 0; io_port[k]; k++) {

      if (io_port[k] == SKIP) continue;

      if (j < MAX_BOARDS && port_detect(io_port[k], j, tpnt)) j++;
      }

   num_boards = j;
   return j;
}

static void map_dma(unsigned int i, unsigned int j) {
   unsigned int data_len = 0;
   unsigned int k, count, pci_dir;
   struct scatterlist *sgpnt;
   struct mscp *cpp;
   struct scsi_cmnd *SCpnt;

   cpp = &HD(j)->cp[i]; SCpnt = cpp->SCpnt;
   pci_dir = SCpnt->sc_data_direction;

   if (SCpnt->sense_buffer)
      cpp->sense_addr = H2DEV(pci_map_single(HD(j)->pdev, SCpnt->sense_buffer,
                           sizeof SCpnt->sense_buffer, PCI_DMA_FROMDEVICE));

   cpp->sense_len = sizeof SCpnt->sense_buffer;

   if (!SCpnt->use_sg) {

      /* If we get here with PCI_DMA_NONE, pci_map_single triggers a BUG() */
      if (!SCpnt->request_bufflen) pci_dir = PCI_DMA_BIDIRECTIONAL;

      if (SCpnt->request_buffer)
         cpp->data_address = H2DEV(pci_map_single(HD(j)->pdev,
                  SCpnt->request_buffer, SCpnt->request_bufflen, pci_dir));

      cpp->data_len = H2DEV(SCpnt->request_bufflen);
      return;
      }

   sgpnt = (struct scatterlist *) SCpnt->request_buffer;
   count = pci_map_sg(HD(j)->pdev, sgpnt, SCpnt->use_sg, pci_dir);

   for (k = 0; k < count; k++) {
      cpp->sglist[k].address = H2DEV(sg_dma_address(&sgpnt[k]));
      cpp->sglist[k].num_bytes = H2DEV(sg_dma_len(&sgpnt[k]));
      data_len += sgpnt[k].length;
      }

   cpp->sg = TRUE;
   cpp->use_sg = SCpnt->use_sg;
   cpp->data_address = H2DEV(pci_map_single(HD(j)->pdev, cpp->sglist,
                             SCpnt->use_sg * sizeof(struct sg_list), pci_dir));
   cpp->data_len = H2DEV(data_len);
}

static void unmap_dma(unsigned int i, unsigned int j) {
   unsigned int pci_dir;
   struct mscp *cpp;
   struct scsi_cmnd *SCpnt;

   cpp = &HD(j)->cp[i]; SCpnt = cpp->SCpnt;
   pci_dir = SCpnt->sc_data_direction;

   if (DEV2H(cpp->sense_addr))
      pci_unmap_single(HD(j)->pdev, DEV2H(cpp->sense_addr),
                       DEV2H(cpp->sense_len), PCI_DMA_FROMDEVICE);

   if (SCpnt->use_sg)
      pci_unmap_sg(HD(j)->pdev, SCpnt->request_buffer, SCpnt->use_sg, pci_dir);

   if (!DEV2H(cpp->data_len)) pci_dir = PCI_DMA_BIDIRECTIONAL;

   if (DEV2H(cpp->data_address))
      pci_unmap_single(HD(j)->pdev, DEV2H(cpp->data_address),
                       DEV2H(cpp->data_len), pci_dir);
}

static void sync_dma(unsigned int i, unsigned int j) {
   unsigned int pci_dir;
   struct mscp *cpp;
   struct scsi_cmnd *SCpnt;

   cpp = &HD(j)->cp[i]; SCpnt = cpp->SCpnt;
   pci_dir = SCpnt->sc_data_direction;

   if (DEV2H(cpp->sense_addr))
      pci_dma_sync_single_for_cpu(HD(j)->pdev, DEV2H(cpp->sense_addr),
                          DEV2H(cpp->sense_len), PCI_DMA_FROMDEVICE);

   if (SCpnt->use_sg)
      pci_dma_sync_sg_for_cpu(HD(j)->pdev, SCpnt->request_buffer,
                         SCpnt->use_sg, pci_dir);

   if (!DEV2H(cpp->data_len)) pci_dir = PCI_DMA_BIDIRECTIONAL;

   if (DEV2H(cpp->data_address))
      pci_dma_sync_single_for_cpu(HD(j)->pdev, DEV2H(cpp->data_address),
                       DEV2H(cpp->data_len), pci_dir);
}

static void scsi_to_dev_dir(unsigned int i, unsigned int j) {
   unsigned int k;

   static const unsigned char data_out_cmds[] = {
      0x0a, 0x2a, 0x15, 0x55, 0x04, 0x07, 0x18, 0x1d, 0x24, 0x2e,
      0x30, 0x31, 0x32, 0x38, 0x39, 0x3a, 0x3b, 0x3d, 0x3f, 0x40,
      0x41, 0x4c, 0xaa, 0xae, 0xb0, 0xb1, 0xb2, 0xb6, 0xea, 0x1b, 0x5d
      };

   static const unsigned char data_none_cmds[] = {
      0x01, 0x0b, 0x10, 0x11, 0x13, 0x16, 0x17, 0x19, 0x2b, 0x1e,
      0x2c, 0xac, 0x2f, 0xaf, 0x33, 0xb3, 0x35, 0x36, 0x45, 0x47,
      0x48, 0x49, 0xa9, 0x4b, 0xa5, 0xa6, 0xb5, 0x00
      };

   struct mscp *cpp;
   struct scsi_cmnd *SCpnt;

   cpp = &HD(j)->cp[i]; SCpnt = cpp->SCpnt;

   if (SCpnt->sc_data_direction == DMA_FROM_DEVICE) {
      cpp->xdir = DTD_IN;
      return;
      }
   else if (SCpnt->sc_data_direction == DMA_FROM_DEVICE) {
      cpp->xdir = DTD_OUT;
      return;
      }
   else if (SCpnt->sc_data_direction == DMA_NONE) {
      cpp->xdir = DTD_NONE;
      return;
      }

   if (SCpnt->sc_data_direction != DMA_BIDIRECTIONAL)
      panic("%s: qcomm, invalid SCpnt->sc_data_direction.\n", BN(j));

   cpp->xdir = DTD_IN;

   for (k = 0; k < ARRAY_SIZE(data_out_cmds); k++)
      if (SCpnt->cmnd[0] == data_out_cmds[k]) {
         cpp->xdir = DTD_OUT;
         break;
         }

   if (cpp->xdir == DTD_IN)
      for (k = 0; k < ARRAY_SIZE(data_none_cmds); k++)
         if (SCpnt->cmnd[0] == data_none_cmds[k]) {
            cpp->xdir = DTD_NONE;
            break;
            }

}

static int u14_34f_queuecommand(struct scsi_cmnd *SCpnt, void (*done)(struct scsi_cmnd *)) {
   unsigned int i, j, k;
   struct mscp *cpp;

   /* j is the board number */
   j = ((struct hostdata *) SCpnt->device->host->hostdata)->board_number;

   if (SCpnt->host_scribble)
      panic("%s: qcomm, pid %ld, SCpnt %p already active.\n",
            BN(j), SCpnt->pid, SCpnt);

   /* i is the mailbox number, look for the first free mailbox
      starting from last_cp_used */
   i = HD(j)->last_cp_used + 1;

   for (k = 0; k < sh[j]->can_queue; k++, i++) {

      if (i >= sh[j]->can_queue) i = 0;

      if (HD(j)->cp_stat[i] == FREE) {
         HD(j)->last_cp_used = i;
         break;
         }
      }

   if (k == sh[j]->can_queue) {
      printk("%s: qcomm, no free mailbox.\n", BN(j));
      return 1;
      }

   /* Set pointer to control packet structure */
   cpp = &HD(j)->cp[i];

   memset(cpp, 0, sizeof(struct mscp) - CP_TAIL_SIZE);
   SCpnt->scsi_done = done;
   cpp->cpp_index = i;
   SCpnt->host_scribble = (unsigned char *) &cpp->cpp_index;

   if (do_trace) printk("%s: qcomm, mbox %d, target %d.%d:%d, pid %ld.\n",
                        BN(j), i, SCpnt->device->channel, SCpnt->device->id,
                        SCpnt->device->lun, SCpnt->pid);

   cpp->opcode = OP_SCSI;
   cpp->channel = SCpnt->device->channel;
   cpp->target = SCpnt->device->id;
   cpp->lun = SCpnt->device->lun;
   cpp->SCpnt = SCpnt;
   cpp->cdb_len = SCpnt->cmd_len;
   memcpy(cpp->cdb, SCpnt->cmnd, SCpnt->cmd_len);

   /* Use data transfer direction SCpnt->sc_data_direction */
   scsi_to_dev_dir(i, j);

   /* Map DMA buffers and SG list */
   map_dma(i, j);

   if (linked_comm && SCpnt->device->queue_depth > 2
                                     && TLDEV(SCpnt->device->type)) {
      HD(j)->cp_stat[i] = READY;
      flush_dev(SCpnt->device, SCpnt->request->sector, j, FALSE);
      return 0;
      }

   if (wait_on_busy(sh[j]->io_port, MAXLOOP)) {
      unmap_dma(i, j);
      SCpnt->host_scribble = NULL;
      scmd_printk(KERN_INFO, SCpnt,
      		"qcomm, pid %ld, adapter busy.\n", SCpnt->pid);
      return 1;
      }

   /* Store pointer in OGM address bytes */
   outl(H2DEV(cpp->cp_dma_addr), sh[j]->io_port + REG_OGM);

   /* Issue OGM interrupt */
   outb(CMD_OGM_INTR, sh[j]->io_port + REG_LCL_INTR);

   HD(j)->cp_stat[i] = IN_USE;
   return 0;
}

static int u14_34f_eh_abort(struct scsi_cmnd *SCarg) {
   unsigned int i, j;

   j = ((struct hostdata *) SCarg->device->host->hostdata)->board_number;

   if (SCarg->host_scribble == NULL) {
      scmd_printk(KERN_INFO, SCarg, "abort, pid %ld inactive.\n",
             SCarg->pid);
      return SUCCESS;
      }

   i = *(unsigned int *)SCarg->host_scribble;
   scmd_printk(KERN_INFO, SCarg, "abort, mbox %d, pid %ld.\n",
	       i, SCarg->pid);

   if (i >= sh[j]->can_queue)
      panic("%s: abort, invalid SCarg->host_scribble.\n", BN(j));

   if (wait_on_busy(sh[j]->io_port, MAXLOOP)) {
      printk("%s: abort, timeout error.\n", BN(j));
      return FAILED;
      }

   if (HD(j)->cp_stat[i] == FREE) {
      printk("%s: abort, mbox %d is free.\n", BN(j), i);
      return SUCCESS;
      }

   if (HD(j)->cp_stat[i] == IN_USE) {
      printk("%s: abort, mbox %d is in use.\n", BN(j), i);

      if (SCarg != HD(j)->cp[i].SCpnt)
         panic("%s: abort, mbox %d, SCarg %p, cp SCpnt %p.\n",
               BN(j), i, SCarg, HD(j)->cp[i].SCpnt);

      if (inb(sh[j]->io_port + REG_SYS_INTR) & IRQ_ASSERTED)
         printk("%s: abort, mbox %d, interrupt pending.\n", BN(j), i);

      return FAILED;
      }

   if (HD(j)->cp_stat[i] == IN_RESET) {
      printk("%s: abort, mbox %d is in reset.\n", BN(j), i);
      return FAILED;
      }

   if (HD(j)->cp_stat[i] == LOCKED) {
      printk("%s: abort, mbox %d is locked.\n", BN(j), i);
      return SUCCESS;
      }

   if (HD(j)->cp_stat[i] == READY || HD(j)->cp_stat[i] == ABORTING) {
      unmap_dma(i, j);
      SCarg->result = DID_ABORT << 16;
      SCarg->host_scribble = NULL;
      HD(j)->cp_stat[i] = FREE;
      printk("%s, abort, mbox %d ready, DID_ABORT, pid %ld done.\n",
             BN(j), i, SCarg->pid);
      SCarg->scsi_done(SCarg);
      return SUCCESS;
      }

   panic("%s: abort, mbox %d, invalid cp_stat.\n", BN(j), i);
}

static int u14_34f_eh_host_reset(struct scsi_cmnd *SCarg) {
   unsigned int i, j, time, k, c, limit = 0;
   int arg_done = FALSE;
   struct scsi_cmnd *SCpnt;

   j = ((struct hostdata *) SCarg->device->host->hostdata)->board_number;
   scmd_printk(KERN_INFO, SCarg, "reset, enter, pid %ld.\n", SCarg->pid);

   spin_lock_irq(sh[j]->host_lock);

   if (SCarg->host_scribble == NULL)
      printk("%s: reset, pid %ld inactive.\n", BN(j), SCarg->pid);

   if (HD(j)->in_reset) {
      printk("%s: reset, exit, already in reset.\n", BN(j));
      spin_unlock_irq(sh[j]->host_lock);
      return FAILED;
      }

   if (wait_on_busy(sh[j]->io_port, MAXLOOP)) {
      printk("%s: reset, exit, timeout error.\n", BN(j));
      spin_unlock_irq(sh[j]->host_lock);
      return FAILED;
      }

   HD(j)->retries = 0;

   for (c = 0; c <= sh[j]->max_channel; c++)
      for (k = 0; k < sh[j]->max_id; k++) {
         HD(j)->target_redo[k][c] = TRUE;
         HD(j)->target_to[k][c] = 0;
         }

   for (i = 0; i < sh[j]->can_queue; i++) {

      if (HD(j)->cp_stat[i] == FREE) continue;

      if (HD(j)->cp_stat[i] == LOCKED) {
         HD(j)->cp_stat[i] = FREE;
         printk("%s: reset, locked mbox %d forced free.\n", BN(j), i);
         continue;
         }

      if (!(SCpnt = HD(j)->cp[i].SCpnt))
         panic("%s: reset, mbox %d, SCpnt == NULL.\n", BN(j), i);

      if (HD(j)->cp_stat[i] == READY || HD(j)->cp_stat[i] == ABORTING) {
         HD(j)->cp_stat[i] = ABORTING;
         printk("%s: reset, mbox %d aborting, pid %ld.\n",
                BN(j), i, SCpnt->pid);
         }

      else {
         HD(j)->cp_stat[i] = IN_RESET;
         printk("%s: reset, mbox %d in reset, pid %ld.\n",
                BN(j), i, SCpnt->pid);
         }

      if (SCpnt->host_scribble == NULL)
         panic("%s: reset, mbox %d, garbled SCpnt.\n", BN(j), i);

      if (*(unsigned int *)SCpnt->host_scribble != i)
         panic("%s: reset, mbox %d, index mismatch.\n", BN(j), i);

      if (SCpnt->scsi_done == NULL)
         panic("%s: reset, mbox %d, SCpnt->scsi_done == NULL.\n", BN(j), i);

      if (SCpnt == SCarg) arg_done = TRUE;
      }

   if (wait_on_busy(sh[j]->io_port, MAXLOOP)) {
      printk("%s: reset, cannot reset, timeout error.\n", BN(j));
      spin_unlock_irq(sh[j]->host_lock);
      return FAILED;
      }

   outb(CMD_RESET, sh[j]->io_port + REG_LCL_INTR);
   printk("%s: reset, board reset done, enabling interrupts.\n", BN(j));

#if defined(DEBUG_RESET)
   do_trace = TRUE;
#endif

   HD(j)->in_reset = TRUE;

   spin_unlock_irq(sh[j]->host_lock);
   time = jiffies;
   while ((jiffies - time) < (10 * HZ) && limit++ < 200000) udelay(100L);
   spin_lock_irq(sh[j]->host_lock);

   printk("%s: reset, interrupts disabled, loops %d.\n", BN(j), limit);

   for (i = 0; i < sh[j]->can_queue; i++) {

      if (HD(j)->cp_stat[i] == IN_RESET) {
         SCpnt = HD(j)->cp[i].SCpnt;
         unmap_dma(i, j);
         SCpnt->result = DID_RESET << 16;
         SCpnt->host_scribble = NULL;

         /* This mailbox is still waiting for its interrupt */
         HD(j)->cp_stat[i] = LOCKED;

         printk("%s, reset, mbox %d locked, DID_RESET, pid %ld done.\n",
                BN(j), i, SCpnt->pid);
         }

      else if (HD(j)->cp_stat[i] == ABORTING) {
         SCpnt = HD(j)->cp[i].SCpnt;
         unmap_dma(i, j);
         SCpnt->result = DID_RESET << 16;
         SCpnt->host_scribble = NULL;

         /* This mailbox was never queued to the adapter */
         HD(j)->cp_stat[i] = FREE;

         printk("%s, reset, mbox %d aborting, DID_RESET, pid %ld done.\n",
                BN(j), i, SCpnt->pid);
         }

      else

         /* Any other mailbox has already been set free by interrupt */
         continue;

      SCpnt->scsi_done(SCpnt);
      }

   HD(j)->in_reset = FALSE;
   do_trace = FALSE;

   if (arg_done) printk("%s: reset, exit, pid %ld done.\n", BN(j), SCarg->pid);
   else          printk("%s: reset, exit.\n", BN(j));

   spin_unlock_irq(sh[j]->host_lock);
   return SUCCESS;
}

static int u14_34f_bios_param(struct scsi_device *disk,
                 struct block_device *bdev, sector_t capacity, int *dkinfo) {
   unsigned int j = 0;
   unsigned int size = capacity;

   dkinfo[0] = HD(j)->heads;
   dkinfo[1] = HD(j)->sectors;
   dkinfo[2] = size / (HD(j)->heads * HD(j)->sectors);

   if (ext_tran && (scsicam_bios_param(bdev, capacity, dkinfo) < 0)) {
      dkinfo[0] = 255;
      dkinfo[1] = 63;
      dkinfo[2] = size / (dkinfo[0] * dkinfo[1]);
      }

#if defined (DEBUG_GEOMETRY)
   printk ("%s: bios_param, head=%d, sec=%d, cyl=%d.\n", driver_name,
           dkinfo[0], dkinfo[1], dkinfo[2]);
#endif

   return FALSE;
}

static void sort(unsigned long sk[], unsigned int da[], unsigned int n,
                 unsigned int rev) {
   unsigned int i, j, k, y;
   unsigned long x;

   for (i = 0; i < n - 1; i++) {
      k = i;

      for (j = k + 1; j < n; j++)
         if (rev) {
            if (sk[j] > sk[k]) k = j;
            }
         else {
            if (sk[j] < sk[k]) k = j;
            }

      if (k != i) {
         x = sk[k]; sk[k] = sk[i]; sk[i] = x;
         y = da[k]; da[k] = da[i]; da[i] = y;
         }
      }

   return;
   }

static int reorder(unsigned int j, unsigned long cursec,
                 unsigned int ihdlr, unsigned int il[], unsigned int n_ready) {
   struct scsi_cmnd *SCpnt;
   struct mscp *cpp;
   unsigned int k, n;
   unsigned int rev = FALSE, s = TRUE, r = TRUE;
   unsigned int input_only = TRUE, overlap = FALSE;
   unsigned long sl[n_ready], pl[n_ready], ll[n_ready];
   unsigned long maxsec = 0, minsec = ULONG_MAX, seek = 0, iseek = 0;
   unsigned long ioseek = 0;

   static unsigned int flushcount = 0, batchcount = 0, sortcount = 0;
   static unsigned int readycount = 0, ovlcount = 0, inputcount = 0;
   static unsigned int readysorted = 0, revcount = 0;
   static unsigned long seeksorted = 0, seeknosort = 0;

   if (link_statistics && !(++flushcount % link_statistics))
      printk("fc %d bc %d ic %d oc %d rc %d rs %d sc %d re %d"\
             " av %ldK as %ldK.\n", flushcount, batchcount, inputcount,
             ovlcount, readycount, readysorted, sortcount, revcount,
             seeknosort / (readycount + 1),
             seeksorted / (readycount + 1));

   if (n_ready <= 1) return FALSE;

   for (n = 0; n < n_ready; n++) {
      k = il[n]; cpp = &HD(j)->cp[k]; SCpnt = cpp->SCpnt;

      if (!(cpp->xdir == DTD_IN)) input_only = FALSE;

      if (SCpnt->request->sector < minsec) minsec = SCpnt->request->sector;
      if (SCpnt->request->sector > maxsec) maxsec = SCpnt->request->sector;

      sl[n] = SCpnt->request->sector;
      ioseek += SCpnt->request->nr_sectors;

      if (!n) continue;

      if (sl[n] < sl[n - 1]) s = FALSE;
      if (sl[n] > sl[n - 1]) r = FALSE;

      if (link_statistics) {
         if (sl[n] > sl[n - 1])
            seek += sl[n] - sl[n - 1];
         else
            seek += sl[n - 1] - sl[n];
         }

      }

   if (link_statistics) {
      if (cursec > sl[0]) seek += cursec - sl[0]; else seek += sl[0] - cursec;
      }

   if (cursec > ((maxsec + minsec) / 2)) rev = TRUE;

   if (ioseek > ((maxsec - minsec) / 2)) rev = FALSE;

   if (!((rev && r) || (!rev && s))) sort(sl, il, n_ready, rev);

   if (!input_only) for (n = 0; n < n_ready; n++) {
      k = il[n]; cpp = &HD(j)->cp[k]; SCpnt = cpp->SCpnt;
      ll[n] = SCpnt->request->nr_sectors; pl[n] = SCpnt->pid;

      if (!n) continue;

      if ((sl[n] == sl[n - 1]) || (!rev && ((sl[n - 1] + ll[n - 1]) > sl[n]))
          || (rev && ((sl[n] + ll[n]) > sl[n - 1]))) overlap = TRUE;
      }

   if (overlap) sort(pl, il, n_ready, FALSE);

   if (link_statistics) {
      if (cursec > sl[0]) iseek = cursec - sl[0]; else iseek = sl[0] - cursec;
      batchcount++; readycount += n_ready; seeknosort += seek / 1024;
      if (input_only) inputcount++;
      if (overlap) { ovlcount++; seeksorted += iseek / 1024; }
      else seeksorted += (iseek + maxsec - minsec) / 1024;
      if (rev && !r)     {  revcount++; readysorted += n_ready; }
      if (!rev && !s)    { sortcount++; readysorted += n_ready; }
      }

#if defined(DEBUG_LINKED_COMMANDS)
   if (link_statistics && (overlap || !(flushcount % link_statistics)))
      for (n = 0; n < n_ready; n++) {
         k = il[n]; cpp = &HD(j)->cp[k]; SCpnt = cpp->SCpnt;
         printk("%s %d.%d:%d pid %ld mb %d fc %d nr %d sec %ld ns %ld"\
                " cur %ld s:%c r:%c rev:%c in:%c ov:%c xd %d.\n",
                (ihdlr ? "ihdlr" : "qcomm"), SCpnt->channel, SCpnt->target,
                SCpnt->lun, SCpnt->pid, k, flushcount, n_ready,
                SCpnt->request->sector, SCpnt->request->nr_sectors, cursec,
                YESNO(s), YESNO(r), YESNO(rev), YESNO(input_only),
                YESNO(overlap), cpp->xdir);
         }
#endif
   return overlap;
}

static void flush_dev(struct scsi_device *dev, unsigned long cursec, unsigned int j,
                      unsigned int ihdlr) {
   struct scsi_cmnd *SCpnt;
   struct mscp *cpp;
   unsigned int k, n, n_ready = 0, il[MAX_MAILBOXES];

   for (k = 0; k < sh[j]->can_queue; k++) {

      if (HD(j)->cp_stat[k] != READY && HD(j)->cp_stat[k] != IN_USE) continue;

      cpp = &HD(j)->cp[k]; SCpnt = cpp->SCpnt;

      if (SCpnt->device != dev) continue;

      if (HD(j)->cp_stat[k] == IN_USE) return;

      il[n_ready++] = k;
      }

   if (reorder(j, cursec, ihdlr, il, n_ready)) n_ready = 1;

   for (n = 0; n < n_ready; n++) {
      k = il[n]; cpp = &HD(j)->cp[k]; SCpnt = cpp->SCpnt;

      if (wait_on_busy(sh[j]->io_port, MAXLOOP)) {
         scmd_printk(KERN_INFO, SCpnt,
	 	"%s, pid %ld, mbox %d, adapter"
                " busy, will abort.\n", (ihdlr ? "ihdlr" : "qcomm"),
                SCpnt->pid, k);
         HD(j)->cp_stat[k] = ABORTING;
         continue;
         }

      outl(H2DEV(cpp->cp_dma_addr), sh[j]->io_port + REG_OGM);
      outb(CMD_OGM_INTR, sh[j]->io_port + REG_LCL_INTR);
      HD(j)->cp_stat[k] = IN_USE;
      }

}

static irqreturn_t ihdlr(int irq, unsigned int j) {
   struct scsi_cmnd *SCpnt;
   unsigned int i, k, c, status, tstatus, reg, ret;
   struct mscp *spp, *cpp;

   if (sh[j]->irq != irq)
       panic("%s: ihdlr, irq %d, sh[j]->irq %d.\n", BN(j), irq, sh[j]->irq);

   /* Check if this board need to be serviced */
   if (!((reg = inb(sh[j]->io_port + REG_SYS_INTR)) & IRQ_ASSERTED)) goto none;

   HD(j)->iocount++;

   if (do_trace) printk("%s: ihdlr, enter, irq %d, count %d.\n", BN(j), irq,
                        HD(j)->iocount);

   /* Check if this board is still busy */
   if (wait_on_busy(sh[j]->io_port, 20 * MAXLOOP)) {
      outb(CMD_CLR_INTR, sh[j]->io_port + REG_SYS_INTR);
      printk("%s: ihdlr, busy timeout error,  irq %d, reg 0x%x, count %d.\n",
             BN(j), irq, reg, HD(j)->iocount);
      goto none;
      }

   ret = inl(sh[j]->io_port + REG_ICM);

   /* Clear interrupt pending flag */
   outb(CMD_CLR_INTR, sh[j]->io_port + REG_SYS_INTR);

   /* Find the mailbox to be serviced on this board */
   for (i = 0; i < sh[j]->can_queue; i++)
      if (H2DEV(HD(j)->cp[i].cp_dma_addr) == ret) break;

   if (i >= sh[j]->can_queue)
      panic("%s: ihdlr, invalid mscp bus address %p, cp0 %p.\n", BN(j),
            (void *)ret, (void *)H2DEV(HD(j)->cp[0].cp_dma_addr));

   cpp = &(HD(j)->cp[i]);
   spp = cpp;

#if defined(DEBUG_GENERATE_ABORTS)
   if ((HD(j)->iocount > 500) && ((HD(j)->iocount % 500) < 3)) goto handled;
#endif

   if (HD(j)->cp_stat[i] == IGNORE) {
      HD(j)->cp_stat[i] = FREE;
      goto handled;
      }
   else if (HD(j)->cp_stat[i] == LOCKED) {
      HD(j)->cp_stat[i] = FREE;
      printk("%s: ihdlr, mbox %d unlocked, count %d.\n", BN(j), i,
             HD(j)->iocount);
      goto handled;
      }
   else if (HD(j)->cp_stat[i] == FREE) {
      printk("%s: ihdlr, mbox %d is free, count %d.\n", BN(j), i,
             HD(j)->iocount);
      goto handled;
      }
   else if (HD(j)->cp_stat[i] == IN_RESET)
      printk("%s: ihdlr, mbox %d is in reset.\n", BN(j), i);
   else if (HD(j)->cp_stat[i] != IN_USE)
      panic("%s: ihdlr, mbox %d, invalid cp_stat: %d.\n",
            BN(j), i, HD(j)->cp_stat[i]);

   HD(j)->cp_stat[i] = FREE;
   SCpnt = cpp->SCpnt;

   if (SCpnt == NULL) panic("%s: ihdlr, mbox %d, SCpnt == NULL.\n", BN(j), i);

   if (SCpnt->host_scribble == NULL)
      panic("%s: ihdlr, mbox %d, pid %ld, SCpnt %p garbled.\n", BN(j), i,
            SCpnt->pid, SCpnt);

   if (*(unsigned int *)SCpnt->host_scribble != i)
      panic("%s: ihdlr, mbox %d, pid %ld, index mismatch %d.\n",
            BN(j), i, SCpnt->pid, *(unsigned int *)SCpnt->host_scribble);

   sync_dma(i, j);

   if (linked_comm && SCpnt->device->queue_depth > 2
                                     && TLDEV(SCpnt->device->type))
      flush_dev(SCpnt->device, SCpnt->request->sector, j, TRUE);

   tstatus = status_byte(spp->target_status);

#if defined(DEBUG_GENERATE_ERRORS)
   if ((HD(j)->iocount > 500) && ((HD(j)->iocount % 200) < 2))
                                           spp->adapter_status = 0x01;
#endif

   switch (spp->adapter_status) {
      case ASOK:     /* status OK */

         /* Forces a reset if a disk drive keeps returning BUSY */
         if (tstatus == BUSY && SCpnt->device->type != TYPE_TAPE)
            status = DID_ERROR << 16;

         /* If there was a bus reset, redo operation on each target */
         else if (tstatus != GOOD && SCpnt->device->type == TYPE_DISK
                  && HD(j)->target_redo[scmd_id(SCpnt)][scmd_channel(SCpnt)])
            status = DID_BUS_BUSY << 16;

         /* Works around a flaw in scsi.c */
         else if (tstatus == CHECK_CONDITION
                  && SCpnt->device->type == TYPE_DISK
                  && (SCpnt->sense_buffer[2] & 0xf) == RECOVERED_ERROR)
            status = DID_BUS_BUSY << 16;

         else
            status = DID_OK << 16;

         if (tstatus == GOOD)
            HD(j)->target_redo[scmd_id(SCpnt)][scmd_channel(SCpnt)] = FALSE;

         if (spp->target_status && SCpnt->device->type == TYPE_DISK &&
             (!(tstatus == CHECK_CONDITION && HD(j)->iocount <= 1000 &&
               (SCpnt->sense_buffer[2] & 0xf) == NOT_READY)))
            scmd_printk(KERN_INFO, SCpnt,
	    	"ihdlr, pid %ld, target_status 0x%x, sense key 0x%x.\n",
                   SCpnt->pid, spp->target_status,
                   SCpnt->sense_buffer[2]);

         HD(j)->target_to[scmd_id(SCpnt)][scmd_channel(SCpnt)] = 0;

         if (HD(j)->last_retried_pid == SCpnt->pid) HD(j)->retries = 0;

         break;
      case ASST:     /* Selection Time Out */

         if (HD(j)->target_to[scmd_id(SCpnt)][scmd_channel(SCpnt)] > 1)
            status = DID_ERROR << 16;
         else {
            status = DID_TIME_OUT << 16;
            HD(j)->target_to[scmd_id(SCpnt)][scmd_channel(SCpnt)]++;
            }

         break;

      /* Perform a limited number of internal retries */
      case 0x93:     /* Unexpected bus free */
      case 0x94:     /* Target bus phase sequence failure */
      case 0x96:     /* Illegal SCSI command */
      case 0xa3:     /* SCSI bus reset error */

         for (c = 0; c <= sh[j]->max_channel; c++)
            for (k = 0; k < sh[j]->max_id; k++)
               HD(j)->target_redo[k][c] = TRUE;


      case 0x92:     /* Data over/under-run */

         if (SCpnt->device->type != TYPE_TAPE
             && HD(j)->retries < MAX_INTERNAL_RETRIES) {

#if defined(DID_SOFT_ERROR)
            status = DID_SOFT_ERROR << 16;
#else
            status = DID_BUS_BUSY << 16;
#endif

            HD(j)->retries++;
            HD(j)->last_retried_pid = SCpnt->pid;
            }
         else
            status = DID_ERROR << 16;

         break;
      case 0x01:     /* Invalid command */
      case 0x02:     /* Invalid parameters */
      case 0x03:     /* Invalid data list */
      case 0x84:     /* SCSI bus abort error */
      case 0x9b:     /* Auto request sense error */
      case 0x9f:     /* Unexpected command complete message error */
      case 0xff:     /* Invalid parameter in the S/G list */
      default:
         status = DID_ERROR << 16;
         break;
      }

   SCpnt->result = status | spp->target_status;

#if defined(DEBUG_INTERRUPT)
   if (SCpnt->result || do_trace)
#else
   if ((spp->adapter_status != ASOK && HD(j)->iocount >  1000) ||
       (spp->adapter_status != ASOK &&
        spp->adapter_status != ASST && HD(j)->iocount <= 1000) ||
        do_trace || msg_byte(spp->target_status))
#endif
      scmd_printk(KERN_INFO, SCpnt, "ihdlr, mbox %2d, err 0x%x:%x,"\
             " pid %ld, reg 0x%x, count %d.\n",
             i, spp->adapter_status, spp->target_status, SCpnt->pid,
             reg, HD(j)->iocount);

   unmap_dma(i, j);

   /* Set the command state to inactive */
   SCpnt->host_scribble = NULL;

   SCpnt->scsi_done(SCpnt);

   if (do_trace) printk("%s: ihdlr, exit, irq %d, count %d.\n", BN(j), irq,
                        HD(j)->iocount);

handled:
   return IRQ_HANDLED;
none:
   return IRQ_NONE;
}

static irqreturn_t do_interrupt_handler(int irq, void *shap,
                                        struct pt_regs *regs) {
   unsigned int j;
   unsigned long spin_flags;
   irqreturn_t ret;

   /* Check if the interrupt must be processed by this handler */
   if ((j = (unsigned int)((char *)shap - sha)) >= num_boards) return IRQ_NONE;

   spin_lock_irqsave(sh[j]->host_lock, spin_flags);
   ret = ihdlr(irq, j);
   spin_unlock_irqrestore(sh[j]->host_lock, spin_flags);
   return ret;
}

static int u14_34f_release(struct Scsi_Host *shpnt) {
   unsigned int i, j;

   for (j = 0; sh[j] != NULL && sh[j] != shpnt; j++);

   if (sh[j] == NULL)
      panic("%s: release, invalid Scsi_Host pointer.\n", driver_name);

   for (i = 0; i < sh[j]->can_queue; i++)
      kfree((&HD(j)->cp[i])->sglist);

   for (i = 0; i < sh[j]->can_queue; i++)
      pci_unmap_single(HD(j)->pdev, HD(j)->cp[i].cp_dma_addr,
                     sizeof(struct mscp), PCI_DMA_BIDIRECTIONAL);

   free_irq(sh[j]->irq, &sha[j]);

   if (sh[j]->dma_channel != NO_DMA)
      free_dma(sh[j]->dma_channel);

   release_region(sh[j]->io_port, sh[j]->n_io_port);
   scsi_unregister(sh[j]);
   return FALSE;
}

#include "scsi_module.c"

#ifndef MODULE
__setup("u14-34f=", option_setup);
#endif /* end MODULE */
