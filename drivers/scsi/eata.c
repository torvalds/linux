/*
 *      eata.c - Low-level driver for EATA/DMA SCSI host adapters.
 *
 *      03 Jun 2003 Rev. 8.10 for linux-2.5.70
 *        + Update for new IRQ API.
 *        + Use "goto" when appropriate.
 *        + Drop eata.h.
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
 *             rs:[y|n]       rev_scan=[1|0]
 *             ip:[y|n]       isa_probe=[1|0]
 *             ep:[y|n]       eisa_probe=[1|0]
 *             pp:[y|n]       pci_probe=[1|0]
 *
 *          A valid example using the new parameter format is:
 *          modprobe eata "eata=0x7410,0x230,lc:y,tm:0,mq:4,ep:n"
 *
 *          which is equivalent to the old format:
 *          modprobe eata io_port=0x7410,0x230 linked_comm=1 tag_mode=0 \
 *                        max_queue_depth=4 eisa_probe=0
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
 *        + The tagged_comm module parameter has been removed, use tag_mode
 *          instead, equivalent to the "tm:" boot option.
 *
 *      10 Oct 2002 Rev. 7.70 for linux 2.5.42
 *        + Foreport from revision 6.70.
 *
 *      25 Jun 2002 Rev. 6.70 for linux 2.4.19
 *        + This release is the first one tested on a Big Endian platform:
 *          fixed endian-ness problem due to bitfields;
 *          fixed endian-ness problem in read_pio.
 *        + Added new options for selectively probing ISA, EISA and PCI bus:
 *
 *          Boot option   Parameter name    Default according to
 *
 *          ip:[y|n]      isa_probe=[1|0]   CONFIG_ISA  defined
 *          ep:[y|n]      eisa_probe=[1|0]  CONFIG_EISA defined
 *          pp:[y|n]      pci_probe=[1|0]   CONFIG_PCI  defined
 *
 *          The default action is to perform probing if the corrisponding
 *          bus is configured and to skip probing otherwise.
 *
 *        + If pci_probe is in effect and a list of I/O  ports is specified
 *          as parameter or boot option, pci_enable_device() is performed
 *          on all pci devices matching PCI_CLASS_STORAGE_SCSI.
 *
 *      21 Feb 2002 Rev. 6.52 for linux 2.4.18
 *        + Backport from rev. 7.22 (use io_request_lock).
 *
 *      20 Feb 2002 Rev. 7.22 for linux 2.5.5
 *        + Remove any reference to virt_to_bus().
 *        + Fix pio hang while detecting multiple HBAs.
 *        + Fixed a board detection bug: in a system with
 *          multiple ISA/EISA boards, all but the first one
 *          were erroneously detected as PCI.
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
 *        + Clean up all pci related routines.
 *        + Fix data transfer direction for opcode SEND_CUE_SHEET (0x5d)
 *
 *      30 Jan 2001 Rev. 6.04 for linux 2.4.1
 *        + Call pci_resource_start after pci_enable_device.
 *
 *      25 Jan 2001 Rev. 6.03 for linux 2.4.0
 *        + "check_region" call replaced by "request_region".
 *
 *      22 Nov 2000 Rev. 6.02 for linux 2.4.0-test11
 *        + Return code checked when calling pci_enable_device.
 *        + Removed old scsi error handling support.
 *        + The obsolete boot option flag eh:n is silently ignored.
 *        + Removed error messages while a disk drive is powered up at
 *          boot time.
 *        + Improved boot messages: all tagged capable device are
 *          indicated as "tagged" or "soft-tagged" :
 *          - "soft-tagged"  means that the driver is trying to do its
 *            own tagging (i.e. the tc:y option is in effect);
 *          - "tagged" means that the device supports tagged commands,
 *            but the driver lets the HBA be responsible for tagging
 *            support.
 *
 *      16 Sep 1999 Rev. 5.11 for linux 2.2.12 and 2.3.18
 *        + Updated to the new __setup interface for boot command line options.
 *        + When loaded as a module, accepts the new parameter boot_options
 *          which value is a string with the same format of the kernel boot
 *          command line options. A valid example is:
 *          modprobe eata 'boot_options="0x7410,0x230,lc:y,tc:n,mq:4"'
 *
 *       9 Sep 1999 Rev. 5.10 for linux 2.2.12 and 2.3.17
 *        + 64bit cleanup for Linux/Alpha platform support
 *          (contribution from H.J. Lu).
 *
 *      22 Jul 1999 Rev. 5.00 for linux 2.2.10 and 2.3.11
 *        + Removed pre-2.2 source code compatibility.
 *        + Added call to pci_set_master.
 *
 *      26 Jul 1998 Rev. 4.33 for linux 2.0.35 and 2.1.111
 *        + Added command line option (rs:[y|n]) to reverse the scan order
 *          of PCI boards. The default is rs:y, which reverses the BIOS order
 *          while registering PCI boards. The default value rs:y generates
 *          the same order of all previous revisions of this driver.
 *          Pls. note that "BIOS order" might have been reversed itself
 *          after the 2.1.9x PCI modifications in the linux kernel.
 *          The rs value is ignored when the explicit list of addresses
 *          is used by the "eata=port0,port1,..." command line option.
 *        + Added command line option (et:[y|n]) to force use of extended
 *          translation (255 heads, 63 sectors) as disk geometry.
 *          The default is et:n, which uses the disk geometry returned
 *          by scsicam_bios_param. The default value et:n is compatible with
 *          all previous revisions of this driver.
 *
 *      28 May 1998 Rev. 4.32 for linux 2.0.33 and 2.1.104
 *          Increased busy timeout from 10 msec. to 200 msec. while
 *          processing interrupts.
 *
 *      16 May 1998 Rev. 4.31 for linux 2.0.33 and 2.1.102
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
 *          Added support for new PCI code and IO-APIC remapping of irqs.
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
 *          Increased controller busy timeout in order to better support
 *          slow SCSI devices.
 *
 *      24 Feb 1997 rev. 3.00 for linux 2.0.29 and 2.1.26
 *          When loading as a module, parameter passing is now supported
 *          both in 2.0 and in 2.1 style.
 *          Fixed data transfer direction for some SCSI opcodes.
 *          Immediate acknowledge to request sense commands.
 *          Linked commands to each disk device are now reordered by elevator
 *          sorting. Rare cases in which reordering of write requests could
 *          cause wrong results are managed.
 *          Fixed spurious timeouts caused by long simple queue tag sequences.
 *          New command line option (tm:[0-3]) to choose the type of tags:
 *          0 -> mixed (default); 1 -> simple; 2 -> head; 3 -> ordered.
 *
 *      18 Jan 1997 rev. 2.60 for linux 2.1.21 and 2.0.28
 *          Added command line options to enable/disable linked commands
 *          (lc:[y|n]), tagged commands (tc:[y|n]) and to set the max queue
 *          depth (mq:xx). Default is "eata=lc:n,tc:n,mq:16".
 *          Improved command linking.
 *          Documented how to setup RAID-0 with DPT SmartRAID boards.
 *
 *       8 Jan 1997 rev. 2.50 for linux 2.1.20 and 2.0.27
 *          Added linked command support.
 *          Improved detection of PCI boards using ISA base addresses.
 *
 *       3 Dec 1996 rev. 2.40 for linux 2.1.14 and 2.0.27
 *          Added support for tagged commands and queue depth adjustment.
 *
 *      22 Nov 1996 rev. 2.30 for linux 2.1.12 and 2.0.26
 *          When CONFIG_PCI is defined, BIOS32 is used to include in the
 *          list of i/o ports to be probed all the PCI SCSI controllers.
 *          The list of i/o ports to be probed can be overwritten by the
 *          "eata=port0,port1,...." boot command line option.
 *          Scatter/gather lists are now allocated by a number of kmalloc
 *          calls, in order to avoid the previous size limit of 64Kb.
 *
 *      16 Nov 1996 rev. 2.20 for linux 2.1.10 and 2.0.25
 *          Added support for EATA 2.0C, PCI, multichannel and wide SCSI.
 *
 *      27 Sep 1996 rev. 2.12 for linux 2.1.0
 *          Portability cleanups (virtual/bus addressing, little/big endian
 *          support).
 *
 *      09 Jul 1996 rev. 2.11 for linux 2.0.4
 *          Number of internal retries is now limited.
 *
 *      16 Apr 1996 rev. 2.10 for linux 1.3.90
 *          New argument "reset_flags" to the reset routine.
 *
 *       6 Jul 1995 rev. 2.01 for linux 1.3.7
 *          Update required by the new /proc/scsi support.
 *
 *      11 Mar 1995 rev. 2.00 for linux 1.2.0
 *          Fixed a bug which prevented media change detection for removable
 *          disk drives.
 *
 *      23 Feb 1995 rev. 1.18 for linux 1.1.94
 *          Added a check for scsi_register returning NULL.
 *
 *      11 Feb 1995 rev. 1.17 for linux 1.1.91
 *          Now DEBUG_RESET is disabled by default.
 *          Register a board even if it does not assert DMA protocol support
 *          (DPT SK2011B does not report correctly the dmasup bit).
 *
 *       9 Feb 1995 rev. 1.16 for linux 1.1.90
 *          Use host->wish_block instead of host->block.
 *          New list of Data Out SCSI commands.
 *
 *       8 Feb 1995 rev. 1.15 for linux 1.1.89
 *          Cleared target_time_out counter while performing a reset.
 *          All external symbols renamed to avoid possible name conflicts.
 *
 *      28 Jan 1995 rev. 1.14 for linux 1.1.86
 *          Added module support.
 *          Log and do a retry when a disk drive returns a target status
 *          different from zero on a recovered error.
 *
 *      24 Jan 1995 rev. 1.13 for linux 1.1.85
 *          Use optimized board configuration, with a measured performance
 *          increase in the range 10%-20% on i/o throughput.
 *
 *      16 Jan 1995 rev. 1.12 for linux 1.1.81
 *          Fix mscp structure comments (no functional change).
 *          Display a message if check_region detects a port address
 *          already in use.
 *
 *      17 Dec 1994 rev. 1.11 for linux 1.1.74
 *          Use the scsicam_bios_param routine. This allows an easy
 *          migration path from disk partition tables created using
 *          different SCSI drivers and non optimal disk geometry.
 *
 *      15 Dec 1994 rev. 1.10 for linux 1.1.74
 *          Added support for ISA EATA boards (DPT PM2011, DPT PM2021).
 *          The host->block flag is set for all the detected ISA boards.
 *          The detect routine no longer enforces LEVEL triggering
 *          for EISA boards, it just prints a warning message.
 *
 *      30 Nov 1994 rev. 1.09 for linux 1.1.68
 *          Redo i/o on target status CHECK_CONDITION for TYPE_DISK only.
 *          Added optional support for using a single board at a time.
 *
 *      18 Nov 1994 rev. 1.08 for linux 1.1.64
 *          Forces sg_tablesize = 64 and can_queue = 64 if these
 *          values are not correctly detected (DPT PM2012).
 *
 *      14 Nov 1994 rev. 1.07 for linux 1.1.63  Final BETA release.
 *      04 Aug 1994 rev. 1.00 for linux 1.1.39  First BETA release.
 *
 *
 *          This driver is based on the CAM (Common Access Method Committee)
 *          EATA (Enhanced AT Bus Attachment) rev. 2.0A, using DMA protocol.
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
 */

/*
 *
 *  Here is a brief description of the DPT SCSI host adapters.
 *  All these boards provide an EATA/DMA compatible programming interface
 *  and are fully supported by this driver in any configuration, including
 *  multiple SCSI channels:
 *
 *  PM2011B/9X -  Entry Level ISA
 *  PM2021A/9X -  High Performance ISA
 *  PM2012A       Old EISA
 *  PM2012B       Old EISA
 *  PM2022A/9X -  Entry Level EISA
 *  PM2122A/9X -  High Performance EISA
 *  PM2322A/9X -  Extra High Performance EISA
 *  PM3021     -  SmartRAID Adapter for ISA
 *  PM3222     -  SmartRAID Adapter for EISA (PM3222W is 16-bit wide SCSI)
 *  PM3224     -  SmartRAID Adapter for PCI  (PM3224W is 16-bit wide SCSI)
 *  PM33340UW  -  SmartRAID Adapter for PCI  ultra wide multichannel
 *
 *  The above list is just an indication: as a matter of fact all DPT
 *  boards using the EATA/DMA protocol are supported by this driver,
 *  since they use exactely the same programming interface.
 *
 *  The DPT PM2001 provides only the EATA/PIO interface and hence is not
 *  supported by this driver.
 *
 *  This code has been tested with up to 3 Distributed Processing Technology
 *  PM2122A/9X (DPT SCSI BIOS v002.D1, firmware v05E.0) EISA controllers,
 *  in any combination of private and shared IRQ.
 *  PCI support has been tested using up to 2 DPT PM3224W (DPT SCSI BIOS
 *  v003.D0, firmware v07G.0).
 *
 *  DPT SmartRAID boards support "Hardware Array" - a group of disk drives
 *  which are all members of the same RAID-0, RAID-1 or RAID-5 array implemented
 *  in host adapter hardware. Hardware Arrays are fully compatible with this
 *  driver, since they look to it as a single disk drive.
 *
 *  WARNING: to create a RAID-0 "Hardware Array" you must select "Other Unix"
 *  as the current OS in the DPTMGR "Initial System Installation" menu.
 *  Otherwise RAID-0 is generated as an "Array Group" (i.e. software RAID-0),
 *  which is not supported by the actual SCSI subsystem.
 *  To get the "Array Group" functionality, the Linux MD driver must be used
 *  instead of the DPT "Array Group" feature.
 *
 *  Multiple ISA, EISA and PCI boards can be configured in the same system.
 *  It is suggested to put all the EISA boards on the same IRQ level, all
 *  the PCI  boards on another IRQ level, while ISA boards cannot share
 *  interrupts.
 *
 *  If you configure multiple boards on the same IRQ, the interrupt must
 *  be _level_ triggered (not _edge_ triggered).
 *
 *  This driver detects EATA boards by probes at fixed port addresses,
 *  so no BIOS32 or PCI BIOS support is required.
 *  The suggested way to detect a generic EATA PCI board is to force on it
 *  any unused EISA address, even if there are other controllers on the EISA
 *  bus, or even if you system has no EISA bus at all.
 *  Do not force any ISA address on EATA PCI boards.
 *
 *  If PCI bios support is configured into the kernel, BIOS32 is used to
 *  include in the list of i/o ports to be probed all the PCI SCSI controllers.
 *
 *  Due to a DPT BIOS "feature", it might not be possible to force an EISA
 *  address on more than a single DPT PCI board, so in this case you have to
 *  let the PCI BIOS assign the addresses.
 *
 *  The sequence of detection probes is:
 *
 *  - ISA 0x1F0;
 *  - PCI SCSI controllers (only if BIOS32 is available);
 *  - EISA/PCI 0x1C88 through 0xFC88 (corresponding to EISA slots 1 to 15);
 *  - ISA  0x170, 0x230, 0x330.
 *
 *  The above list of detection probes can be totally replaced by the
 *  boot command line option: "eata=port0,port1,port2,...", where the
 *  port0, port1... arguments are ISA/EISA/PCI addresses to be probed.
 *  For example using "eata=0x7410,0x7450,0x230", the driver probes
 *  only the two PCI addresses 0x7410 and 0x7450 and the ISA address 0x230,
 *  in this order; "eata=0" totally disables this driver.
 *
 *  After the optional list of detection probes, other possible command line
 *  options are:
 *
 *  et:y  force use of extended translation (255 heads, 63 sectors);
 *  et:n  use disk geometry detected by scsicam_bios_param;
 *  rs:y  reverse scan order while detecting PCI boards;
 *  rs:n  use BIOS order while detecting PCI boards;
 *  lc:y  enables linked commands;
 *  lc:n  disables linked commands;
 *  tm:0  disables tagged commands (same as tc:n);
 *  tm:1  use simple queue tags (same as tc:y);
 *  tm:2  use ordered queue tags (same as tc:2);
 *  mq:xx set the max queue depth to the value xx (2 <= xx <= 32).
 *
 *  The default value is: "eata=lc:n,mq:16,tm:0,et:n,rs:n".
 *  An example using the list of detection probes could be:
 *  "eata=0x7410,0x230,lc:y,tm:2,mq:4,et:n".
 *
 *  When loading as a module, parameters can be specified as well.
 *  The above example would be (use 1 in place of y and 0 in place of n):
 *
 *  modprobe eata io_port=0x7410,0x230 linked_comm=1 \
 *                max_queue_depth=4 ext_tran=0 tag_mode=2 \
 *                rev_scan=1
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
 *  The extended translation option (et:y) is useful when using large physical
 *  disks/arrays. It could also be useful when switching between Adaptec boards
 *  and DPT boards without reformatting the disk.
 *  When a boot disk is partitioned with extended translation, in order to
 *  be able to boot it with a DPT board is could be necessary to add to
 *  lilo.conf additional commands as in the following example:
 *
 *  fix-table
 *  disk=/dev/sda bios=0x80 sectors=63 heads=128 cylindres=546
 *
 *  where the above geometry should be replaced with the one reported at
 *  power up by the DPT controller.
 *  ----------------------------------------------------------------------------
 *
 *  The boards are named EATA0, EATA1,... according to the detection order.
 *
 *  In order to support multiple ISA boards in a reliable way,
 *  the driver sets host->wish_block = 1 for all ISA boards.
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/stat.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <asm/byteorder.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>

static int eata2x_detect(struct scsi_host_template *);
static int eata2x_release(struct Scsi_Host *);
static int eata2x_queuecommand(struct scsi_cmnd *,
			       void (*done) (struct scsi_cmnd *));
static int eata2x_eh_abort(struct scsi_cmnd *);
static int eata2x_eh_host_reset(struct scsi_cmnd *);
static int eata2x_bios_param(struct scsi_device *, struct block_device *,
			     sector_t, int *);
static int eata2x_slave_configure(struct scsi_device *);

static struct scsi_host_template driver_template = {
	.name = "EATA/DMA 2.0x rev. 8.10.00 ",
	.detect = eata2x_detect,
	.release = eata2x_release,
	.queuecommand = eata2x_queuecommand,
	.eh_abort_handler = eata2x_eh_abort,
	.eh_host_reset_handler = eata2x_eh_host_reset,
	.bios_param = eata2x_bios_param,
	.slave_configure = eata2x_slave_configure,
	.this_id = 7,
	.unchecked_isa_dma = 1,
	.use_clustering = ENABLE_CLUSTERING,
};

#if !defined(__BIG_ENDIAN_BITFIELD) && !defined(__LITTLE_ENDIAN_BITFIELD)
#error "Adjust your <asm/byteorder.h> defines"
#endif

/* Subversion values */
#define ISA  0
#define ESA 1

#undef  FORCE_CONFIG

#undef  DEBUG_LINKED_COMMANDS
#undef  DEBUG_DETECT
#undef  DEBUG_PCI_DETECT
#undef  DEBUG_INTERRUPT
#undef  DEBUG_RESET
#undef  DEBUG_GENERATE_ERRORS
#undef  DEBUG_GENERATE_ABORTS
#undef  DEBUG_GEOMETRY

#define MAX_ISA 4
#define MAX_VESA 0
#define MAX_EISA 15
#define MAX_PCI 16
#define MAX_BOARDS (MAX_ISA + MAX_VESA + MAX_EISA + MAX_PCI)
#define MAX_CHANNEL 4
#define MAX_LUN 32
#define MAX_TARGET 32
#define MAX_MAILBOXES 64
#define MAX_SGLIST 64
#define MAX_LARGE_SGLIST 122
#define MAX_INTERNAL_RETRIES 64
#define MAX_CMD_PER_LUN 2
#define MAX_TAGGED_CMD_PER_LUN (MAX_MAILBOXES - MAX_CMD_PER_LUN)

#define SKIP ULONG_MAX
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

#define REG_CMD         7
#define REG_STATUS      7
#define REG_AUX_STATUS  8
#define REG_DATA        0
#define REG_DATA2       1
#define REG_SEE         6
#define REG_LOW         2
#define REG_LM          3
#define REG_MID         4
#define REG_MSB         5
#define REGION_SIZE     9UL
#define MAX_ISA_ADDR    0x03ff
#define MIN_EISA_ADDR   0x1c88
#define MAX_EISA_ADDR   0xfc88
#define BSY_ASSERTED      0x80
#define DRQ_ASSERTED      0x08
#define ABSY_ASSERTED     0x01
#define IRQ_ASSERTED      0x02
#define READ_CONFIG_PIO   0xf0
#define SET_CONFIG_PIO    0xf1
#define SEND_CP_PIO       0xf2
#define RECEIVE_SP_PIO    0xf3
#define TRUNCATE_XFR_PIO  0xf4
#define RESET_PIO         0xf9
#define READ_CONFIG_DMA   0xfd
#define SET_CONFIG_DMA    0xfe
#define SEND_CP_DMA       0xff
#define ASOK              0x00
#define ASST              0x01

#define YESNO(a) ((a) ? 'y' : 'n')
#define TLDEV(type) ((type) == TYPE_DISK || (type) == TYPE_ROM)

/* "EATA", in Big Endian format */
#define EATA_SIG_BE 0x45415441

/* Number of valid bytes in the board config structure for EATA 2.0x */
#define EATA_2_0A_SIZE 28
#define EATA_2_0B_SIZE 30
#define EATA_2_0C_SIZE 34

/* Board info structure */
struct eata_info {
	u_int32_t data_len;	/* Number of valid bytes after this field */
	u_int32_t sign;		/* ASCII "EATA" signature */

#if defined(__BIG_ENDIAN_BITFIELD)
	unchar version	: 4,
	       		: 4;
	unchar haaval	: 1,
	       ata	: 1,
	       drqvld	: 1,
	       dmasup	: 1,
	       morsup	: 1,
	       trnxfr	: 1,
	       tarsup	: 1,
	       ocsena	: 1;
#else
	unchar		: 4,	/* unused low nibble */
	 	version	: 4;	/* EATA version, should be 0x1 */
	unchar ocsena	: 1,	/* Overlap Command Support Enabled */
	       tarsup	: 1,	/* Target Mode Supported */
	       trnxfr	: 1,	/* Truncate Transfer Cmd NOT Necessary */
	       morsup	: 1,	/* More Supported */
	       dmasup	: 1,	/* DMA Supported */
	       drqvld	: 1,	/* DRQ Index (DRQX) is valid */
	       ata	: 1,	/* This is an ATA device */
	       haaval	: 1;	/* Host Adapter Address Valid */
#endif

	ushort cp_pad_len;	/* Number of pad bytes after cp_len */
	unchar host_addr[4];	/* Host Adapter SCSI ID for channels 3, 2, 1, 0 */
	u_int32_t cp_len;	/* Number of valid bytes in cp */
	u_int32_t sp_len;	/* Number of valid bytes in sp */
	ushort queue_size;	/* Max number of cp that can be queued */
	ushort unused;
	ushort scatt_size;	/* Max number of entries in scatter/gather table */

#if defined(__BIG_ENDIAN_BITFIELD)
	unchar drqx	: 2,
	       second	: 1,
	       irq_tr	: 1,
	       irq	: 4;
	unchar sync;
	unchar		: 4,
	       res1	: 1,
	       large_sg	: 1,
	       forcaddr	: 1,
	       isaena	: 1;
	unchar max_chan	: 3,
	       max_id	: 5;
	unchar max_lun;
	unchar eisa	: 1,
	       pci	: 1,
	       idquest	: 1,
	       m1	: 1,
	       		: 4;
#else
	unchar irq	: 4,	/* Interrupt Request assigned to this controller */
	       irq_tr	: 1,	/* 0 for edge triggered, 1 for level triggered */
	       second	: 1,	/* 1 if this is a secondary (not primary) controller */
	       drqx	: 2;	/* DRQ Index (0=DMA0, 1=DMA7, 2=DMA6, 3=DMA5) */
	unchar sync;		/* 1 if scsi target id 7...0 is running sync scsi */

	/* Structure extension defined in EATA 2.0B */
	unchar isaena	: 1,	/* ISA i/o addressing is disabled/enabled */
	       forcaddr	: 1,	/* Port address has been forced */
	       large_sg	: 1,	/* 1 if large SG lists are supported */
	       res1	: 1,
	       		: 4;
	unchar max_id	: 5,	/* Max SCSI target ID number */
	       max_chan	: 3;	/* Max SCSI channel number on this board */

	/* Structure extension defined in EATA 2.0C */
	unchar max_lun;		/* Max SCSI LUN number */
	unchar
			: 4,
	       m1	: 1,	/* This is a PCI with an M1 chip installed */
	       idquest	: 1,	/* RAIDNUM returned is questionable */
	       pci	: 1,	/* This board is PCI */
	       eisa	: 1;	/* This board is EISA */
#endif

	unchar raidnum;		/* Uniquely identifies this HBA in a system */
	unchar notused;

	ushort ipad[247];
};

/* Board config structure */
struct eata_config {
	ushort len;		/* Number of bytes following this field */

#if defined(__BIG_ENDIAN_BITFIELD)
	unchar		: 4,
	       tarena	: 1,
	       mdpena	: 1,
	       ocena	: 1,
	       edis	: 1;
#else
	unchar edis	: 1,	/* Disable EATA interface after config command */
	       ocena	: 1,	/* Overlapped Commands Enabled */
	       mdpena	: 1,	/* Transfer all Modified Data Pointer Messages */
	       tarena	: 1,	/* Target Mode Enabled for this controller */
	       		: 4;
#endif
	unchar cpad[511];
};

/* Returned status packet structure */
struct mssp {
#if defined(__BIG_ENDIAN_BITFIELD)
	unchar eoc	: 1,
	       adapter_status : 7;
#else
	unchar adapter_status : 7,	/* State related to current command */
	       eoc	: 1;		/* End Of Command (1 = command completed) */
#endif
	unchar target_status;	/* SCSI status received after data transfer */
	unchar unused[2];
	u_int32_t inv_res_len;	/* Number of bytes not transferred */
	u_int32_t cpp_index;	/* Index of address set in cp */
	char mess[12];
};

struct sg_list {
	unsigned int address;	/* Segment Address */
	unsigned int num_bytes;	/* Segment Length */
};

/* MailBox SCSI Command Packet */
struct mscp {
#if defined(__BIG_ENDIAN_BITFIELD)
	unchar din	: 1,
	       dout	: 1,
	       interp	: 1,
	       		: 1,
		sg	: 1,
		reqsen	:1,
		init	: 1,
		sreset	: 1;
	unchar sense_len;
	unchar unused[3];
	unchar		: 7,
	       fwnest	: 1;
	unchar		: 5,
	       hbaci	: 1,
	       iat	: 1,
	       phsunit	: 1;
	unchar channel	: 3,
	       target	: 5;
	unchar one	: 1,
	       dispri	: 1,
	       luntar	: 1,
	       lun	: 5;
#else
	unchar sreset	:1,	/* SCSI Bus Reset Signal should be asserted */
	       init	:1,	/* Re-initialize controller and self test */
	       reqsen	:1,	/* Transfer Request Sense Data to addr using DMA */
	       sg	:1,	/* Use Scatter/Gather */
	       		:1,
	       interp	:1,	/* The controller interprets cp, not the target */
	       dout	:1,	/* Direction of Transfer is Out (Host to Target) */
	       din	:1;	/* Direction of Transfer is In (Target to Host) */
	unchar sense_len;	/* Request Sense Length */
	unchar unused[3];
	unchar fwnest	: 1,	/* Send command to a component of an Array Group */
			: 7;
	unchar phsunit	: 1,	/* Send to Target Physical Unit (bypass RAID) */
	       iat	: 1,	/* Inhibit Address Translation */
	       hbaci	: 1,	/* Inhibit HBA Caching for this command */
	       		: 5;
	unchar target	: 5,	/* SCSI target ID */
	       channel	: 3;	/* SCSI channel number */
	unchar lun	: 5,	/* SCSI logical unit number */
	       luntar	: 1,	/* This cp is for Target (not LUN) */
	       dispri	: 1,	/* Disconnect Privilege granted */
	       one	: 1;	/* 1 */
#endif

	unchar mess[3];		/* Massage to/from Target */
	unchar cdb[12];		/* Command Descriptor Block */
	u_int32_t data_len;	/* If sg=0 Data Length, if sg=1 sglist length */
	u_int32_t cpp_index;	/* Index of address to be returned in sp */
	u_int32_t data_address;	/* If sg=0 Data Address, if sg=1 sglist address */
	u_int32_t sp_dma_addr;	/* Address where sp is DMA'ed when cp completes */
	u_int32_t sense_addr;	/* Address where Sense Data is DMA'ed on error */

	/* Additional fields begin here. */
	struct scsi_cmnd *SCpnt;

	/* All the cp structure is zero filled by queuecommand except the
	   following CP_TAIL_SIZE bytes, initialized by detect */
	dma_addr_t cp_dma_addr;	/* dma handle for this cp structure */
	struct sg_list *sglist;	/* pointer to the allocated SG list */
};

#define CP_TAIL_SIZE (sizeof(struct sglist *) + sizeof(dma_addr_t))

struct hostdata {
	struct mscp cp[MAX_MAILBOXES];	/* Mailboxes for this board */
	unsigned int cp_stat[MAX_MAILBOXES];	/* FREE, IN_USE, LOCKED, IN_RESET */
	unsigned int last_cp_used;	/* Index of last mailbox used */
	unsigned int iocount;	/* Total i/o done for this board */
	int board_number;	/* Number of this board */
	char board_name[16];	/* Name of this board */
	int in_reset;		/* True if board is doing a reset */
	int target_to[MAX_TARGET][MAX_CHANNEL];	/* N. of timeout errors on target */
	int target_redo[MAX_TARGET][MAX_CHANNEL];	/* If 1 redo i/o on target */
	unsigned int retries;	/* Number of internal retries */
	unsigned long last_retried_pid;	/* Pid of last retried command */
	unsigned char subversion;	/* Bus type, either ISA or EISA/PCI */
	unsigned char protocol_rev;	/* EATA 2.0 rev., 'A' or 'B' or 'C' */
	unsigned char is_pci;	/* 1 is bus type is PCI */
	struct pci_dev *pdev;	/* pdev for PCI bus, NULL otherwise */
	struct mssp *sp_cpu_addr;	/* cpu addr for DMA buffer sp */
	dma_addr_t sp_dma_addr;	/* dma handle for DMA buffer sp */
	struct mssp sp;		/* Local copy of sp buffer */
};

static struct Scsi_Host *sh[MAX_BOARDS];
static const char *driver_name = "EATA";
static char sha[MAX_BOARDS];
static DEFINE_SPINLOCK(driver_lock);

/* Initialize num_boards so that ihdlr can work while detect is in progress */
static unsigned int num_boards = MAX_BOARDS;

static unsigned long io_port[] = {

	/* Space for MAX_INT_PARAM ports usable while loading as a module */
	SKIP, SKIP, SKIP, SKIP, SKIP, SKIP, SKIP, SKIP,
	SKIP, SKIP,

	/* First ISA */
	0x1f0,

	/* Space for MAX_PCI ports possibly reported by PCI_BIOS */
	SKIP, SKIP, SKIP, SKIP, SKIP, SKIP, SKIP, SKIP,
	SKIP, SKIP, SKIP, SKIP, SKIP, SKIP, SKIP, SKIP,

	/* MAX_EISA ports */
	0x1c88, 0x2c88, 0x3c88, 0x4c88, 0x5c88, 0x6c88, 0x7c88, 0x8c88,
	0x9c88, 0xac88, 0xbc88, 0xcc88, 0xdc88, 0xec88, 0xfc88,

	/* Other (MAX_ISA - 1) ports */
	0x170, 0x230, 0x330,

	/* End of list */
	0x0
};

/* Device is Big Endian */
#define H2DEV(x)   cpu_to_be32(x)
#define DEV2H(x)   be32_to_cpu(x)
#define H2DEV16(x) cpu_to_be16(x)
#define DEV2H16(x) be16_to_cpu(x)

/* But transfer orientation from the 16 bit data register is Little Endian */
#define REG2H(x)   le16_to_cpu(x)

static irqreturn_t do_interrupt_handler(int, void *);
static void flush_dev(struct scsi_device *, unsigned long, struct hostdata *,
		      unsigned int);
static int do_trace = 0;
static int setup_done = 0;
static int link_statistics;
static int ext_tran = 0;
static int rev_scan = 1;

#if defined(CONFIG_SCSI_EATA_TAGGED_QUEUE)
static int tag_mode = TAG_SIMPLE;
#else
static int tag_mode = TAG_DISABLED;
#endif

#if defined(CONFIG_SCSI_EATA_LINKED_COMMANDS)
static int linked_comm = 1;
#else
static int linked_comm = 0;
#endif

#if defined(CONFIG_SCSI_EATA_MAX_TAGS)
static int max_queue_depth = CONFIG_SCSI_EATA_MAX_TAGS;
#else
static int max_queue_depth = MAX_CMD_PER_LUN;
#endif

#if defined(CONFIG_ISA)
static int isa_probe = 1;
#else
static int isa_probe = 0;
#endif

#if defined(CONFIG_EISA)
static int eisa_probe = 1;
#else
static int eisa_probe = 0;
#endif

#if defined(CONFIG_PCI)
static int pci_probe = 1;
#else
static int pci_probe = 0;
#endif

#define MAX_INT_PARAM 10
#define MAX_BOOT_OPTIONS_SIZE 256
static char boot_options[MAX_BOOT_OPTIONS_SIZE];

#if defined(MODULE)
#include <linux/module.h>
#include <linux/moduleparam.h>

module_param_string(eata, boot_options, MAX_BOOT_OPTIONS_SIZE, 0);
MODULE_PARM_DESC(eata, " equivalent to the \"eata=...\" kernel boot option."
		 "            Example: modprobe eata \"eata=0x7410,0x230,lc:y,tm:0,mq:4,ep:n\"");
MODULE_AUTHOR("Dario Ballabio");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EATA/DMA SCSI Driver");

#endif

static int eata2x_slave_configure(struct scsi_device *dev)
{
	int tqd, utqd;
	char *tag_suffix, *link_suffix;

	utqd = MAX_CMD_PER_LUN;
	tqd = max_queue_depth;

	if (TLDEV(dev->type) && dev->tagged_supported) {
		if (tag_mode == TAG_SIMPLE) {
			scsi_adjust_queue_depth(dev, MSG_SIMPLE_TAG, tqd);
			tag_suffix = ", simple tags";
		} else if (tag_mode == TAG_ORDERED) {
			scsi_adjust_queue_depth(dev, MSG_ORDERED_TAG, tqd);
			tag_suffix = ", ordered tags";
		} else {
			scsi_adjust_queue_depth(dev, 0, tqd);
			tag_suffix = ", no tags";
		}
	} else if (TLDEV(dev->type) && linked_comm) {
		scsi_adjust_queue_depth(dev, 0, tqd);
		tag_suffix = ", untagged";
	} else {
		scsi_adjust_queue_depth(dev, 0, utqd);
		tag_suffix = "";
	}

	if (TLDEV(dev->type) && linked_comm && dev->queue_depth > 2)
		link_suffix = ", sorted";
	else if (TLDEV(dev->type))
		link_suffix = ", unsorted";
	else
		link_suffix = "";

	sdev_printk(KERN_INFO, dev,
		"cmds/lun %d%s%s.\n",
	       dev->queue_depth, link_suffix, tag_suffix);

	return 0;
}

static int wait_on_busy(unsigned long iobase, unsigned int loop)
{
	while (inb(iobase + REG_AUX_STATUS) & ABSY_ASSERTED) {
		udelay(1L);
		if (--loop == 0)
			return 1;
	}
	return 0;
}

static int do_dma(unsigned long iobase, unsigned long addr, unchar cmd)
{
	unsigned char *byaddr;
	unsigned long devaddr;

	if (wait_on_busy(iobase, (addr ? MAXLOOP * 100 : MAXLOOP)))
		return 1;

	if (addr) {
		devaddr = H2DEV(addr);
		byaddr = (unsigned char *)&devaddr;
		outb(byaddr[3], iobase + REG_LOW);
		outb(byaddr[2], iobase + REG_LM);
		outb(byaddr[1], iobase + REG_MID);
		outb(byaddr[0], iobase + REG_MSB);
	}

	outb(cmd, iobase + REG_CMD);
	return 0;
}

static int read_pio(unsigned long iobase, ushort * start, ushort * end)
{
	unsigned int loop = MAXLOOP;
	ushort *p;

	for (p = start; p <= end; p++) {
		while (!(inb(iobase + REG_STATUS) & DRQ_ASSERTED)) {
			udelay(1L);
			if (--loop == 0)
				return 1;
		}
		loop = MAXLOOP;
		*p = REG2H(inw(iobase));
	}

	return 0;
}

static struct pci_dev *get_pci_dev(unsigned long port_base)
{
#if defined(CONFIG_PCI)
	unsigned int addr;
	struct pci_dev *dev = NULL;

	while ((dev = pci_get_class(PCI_CLASS_STORAGE_SCSI << 8, dev))) {
		addr = pci_resource_start(dev, 0);

#if defined(DEBUG_PCI_DETECT)
		printk("%s: get_pci_dev, bus %d, devfn 0x%x, addr 0x%x.\n",
		       driver_name, dev->bus->number, dev->devfn, addr);
#endif

		/* we are in so much trouble for a pci hotplug system with this driver
		 * anyway, so doing this at least lets people unload the driver and not
		 * cause memory problems, but in general this is a bad thing to do (this
		 * driver needs to be converted to the proper PCI api someday... */
		pci_dev_put(dev);
		if (addr + PCI_BASE_ADDRESS_0 == port_base)
			return dev;
	}
#endif				/* end CONFIG_PCI */
	return NULL;
}

static void enable_pci_ports(void)
{
#if defined(CONFIG_PCI)
	struct pci_dev *dev = NULL;

	while ((dev = pci_get_class(PCI_CLASS_STORAGE_SCSI << 8, dev))) {
#if defined(DEBUG_PCI_DETECT)
		printk("%s: enable_pci_ports, bus %d, devfn 0x%x.\n",
		       driver_name, dev->bus->number, dev->devfn);
#endif

		if (pci_enable_device(dev))
			printk
			    ("%s: warning, pci_enable_device failed, bus %d devfn 0x%x.\n",
			     driver_name, dev->bus->number, dev->devfn);
	}

#endif				/* end CONFIG_PCI */
}

static int port_detect(unsigned long port_base, unsigned int j,
		struct scsi_host_template *tpnt)
{
	unsigned char irq, dma_channel, subversion, i, is_pci = 0;
	unsigned char protocol_rev;
	struct eata_info info;
	char *bus_type, dma_name[16];
	struct pci_dev *pdev;
	/* Allowed DMA channels for ISA (0 indicates reserved) */
	unsigned char dma_channel_table[4] = { 5, 6, 7, 0 };
	struct Scsi_Host *shost;
	struct hostdata *ha;
	char name[16];

	sprintf(name, "%s%d", driver_name, j);

	if (!request_region(port_base, REGION_SIZE, driver_name)) {
#if defined(DEBUG_DETECT)
		printk("%s: address 0x%03lx in use, skipping probe.\n", name,
		       port_base);
#endif
		goto fail;
	}

	spin_lock_irq(&driver_lock);

	if (do_dma(port_base, 0, READ_CONFIG_PIO)) {
#if defined(DEBUG_DETECT)
		printk("%s: detect, do_dma failed at 0x%03lx.\n", name,
		       port_base);
#endif
		goto freelock;
	}

	/* Read the info structure */
	if (read_pio(port_base, (ushort *) & info, (ushort *) & info.ipad[0])) {
#if defined(DEBUG_DETECT)
		printk("%s: detect, read_pio failed at 0x%03lx.\n", name,
		       port_base);
#endif
		goto freelock;
	}

	info.data_len = DEV2H(info.data_len);
	info.sign = DEV2H(info.sign);
	info.cp_pad_len = DEV2H16(info.cp_pad_len);
	info.cp_len = DEV2H(info.cp_len);
	info.sp_len = DEV2H(info.sp_len);
	info.scatt_size = DEV2H16(info.scatt_size);
	info.queue_size = DEV2H16(info.queue_size);

	/* Check the controller "EATA" signature */
	if (info.sign != EATA_SIG_BE) {
#if defined(DEBUG_DETECT)
		printk("%s: signature 0x%04x discarded.\n", name, info.sign);
#endif
		goto freelock;
	}

	if (info.data_len < EATA_2_0A_SIZE) {
		printk
		    ("%s: config structure size (%d bytes) too short, detaching.\n",
		     name, info.data_len);
		goto freelock;
	} else if (info.data_len == EATA_2_0A_SIZE)
		protocol_rev = 'A';
	else if (info.data_len == EATA_2_0B_SIZE)
		protocol_rev = 'B';
	else
		protocol_rev = 'C';

	if (protocol_rev != 'A' && info.forcaddr) {
		printk("%s: warning, port address has been forced.\n", name);
		bus_type = "PCI";
		is_pci = 1;
		subversion = ESA;
	} else if (port_base > MAX_EISA_ADDR
		   || (protocol_rev == 'C' && info.pci)) {
		bus_type = "PCI";
		is_pci = 1;
		subversion = ESA;
	} else if (port_base >= MIN_EISA_ADDR
		   || (protocol_rev == 'C' && info.eisa)) {
		bus_type = "EISA";
		subversion = ESA;
	} else if (protocol_rev == 'C' && !info.eisa && !info.pci) {
		bus_type = "ISA";
		subversion = ISA;
	} else if (port_base > MAX_ISA_ADDR) {
		bus_type = "PCI";
		is_pci = 1;
		subversion = ESA;
	} else {
		bus_type = "ISA";
		subversion = ISA;
	}

	if (!info.haaval || info.ata) {
		printk
		    ("%s: address 0x%03lx, unusable %s board (%d%d), detaching.\n",
		     name, port_base, bus_type, info.haaval, info.ata);
		goto freelock;
	}

	if (info.drqvld) {
		if (subversion == ESA)
			printk("%s: warning, weird %s board using DMA.\n", name,
			       bus_type);

		subversion = ISA;
		dma_channel = dma_channel_table[3 - info.drqx];
	} else {
		if (subversion == ISA)
			printk("%s: warning, weird %s board not using DMA.\n",
			       name, bus_type);

		subversion = ESA;
		dma_channel = NO_DMA;
	}

	if (!info.dmasup)
		printk("%s: warning, DMA protocol support not asserted.\n",
		       name);

	irq = info.irq;

	if (subversion == ESA && !info.irq_tr)
		printk
		    ("%s: warning, LEVEL triggering is suggested for IRQ %u.\n",
		     name, irq);

	if (is_pci) {
		pdev = get_pci_dev(port_base);
		if (!pdev)
			printk
			    ("%s: warning, failed to get pci_dev structure.\n",
			     name);
	} else
		pdev = NULL;

	if (pdev && (irq != pdev->irq)) {
		printk("%s: IRQ %u mapped to IO-APIC IRQ %u.\n", name, irq,
		       pdev->irq);
		irq = pdev->irq;
	}

	/* Board detected, allocate its IRQ */
	if (request_irq(irq, do_interrupt_handler,
			IRQF_DISABLED | ((subversion == ESA) ? IRQF_SHARED : 0),
			driver_name, (void *)&sha[j])) {
		printk("%s: unable to allocate IRQ %u, detaching.\n", name,
		       irq);
		goto freelock;
	}

	if (subversion == ISA && request_dma(dma_channel, driver_name)) {
		printk("%s: unable to allocate DMA channel %u, detaching.\n",
		       name, dma_channel);
		goto freeirq;
	}
#if defined(FORCE_CONFIG)
	{
		struct eata_config *cf;
		dma_addr_t cf_dma_addr;

		cf = pci_alloc_consistent(pdev, sizeof(struct eata_config),
					  &cf_dma_addr);

		if (!cf) {
			printk
			    ("%s: config, pci_alloc_consistent failed, detaching.\n",
			     name);
			goto freedma;
		}

		/* Set board configuration */
		memset((char *)cf, 0, sizeof(struct eata_config));
		cf->len = (ushort) H2DEV16((ushort) 510);
		cf->ocena = 1;

		if (do_dma(port_base, cf_dma_addr, SET_CONFIG_DMA)) {
			printk
			    ("%s: busy timeout sending configuration, detaching.\n",
			     name);
			pci_free_consistent(pdev, sizeof(struct eata_config),
					    cf, cf_dma_addr);
			goto freedma;
		}

	}
#endif

	spin_unlock_irq(&driver_lock);
	sh[j] = shost = scsi_register(tpnt, sizeof(struct hostdata));
	spin_lock_irq(&driver_lock);

	if (shost == NULL) {
		printk("%s: unable to register host, detaching.\n", name);
		goto freedma;
	}

	shost->io_port = port_base;
	shost->unique_id = port_base;
	shost->n_io_port = REGION_SIZE;
	shost->dma_channel = dma_channel;
	shost->irq = irq;
	shost->sg_tablesize = (ushort) info.scatt_size;
	shost->this_id = (ushort) info.host_addr[3];
	shost->can_queue = (ushort) info.queue_size;
	shost->cmd_per_lun = MAX_CMD_PER_LUN;

	ha = (struct hostdata *)shost->hostdata;
	
	memset(ha, 0, sizeof(struct hostdata));
	ha->subversion = subversion;
	ha->protocol_rev = protocol_rev;
	ha->is_pci = is_pci;
	ha->pdev = pdev;
	ha->board_number = j;

	if (ha->subversion == ESA)
		shost->unchecked_isa_dma = 0;
	else {
		unsigned long flags;
		shost->unchecked_isa_dma = 1;

		flags = claim_dma_lock();
		disable_dma(dma_channel);
		clear_dma_ff(dma_channel);
		set_dma_mode(dma_channel, DMA_MODE_CASCADE);
		enable_dma(dma_channel);
		release_dma_lock(flags);

	}

	strcpy(ha->board_name, name);

	/* DPT PM2012 does not allow to detect sg_tablesize correctly */
	if (shost->sg_tablesize > MAX_SGLIST || shost->sg_tablesize < 2) {
		printk("%s: detect, wrong n. of SG lists %d, fixed.\n",
		       ha->board_name, shost->sg_tablesize);
		shost->sg_tablesize = MAX_SGLIST;
	}

	/* DPT PM2012 does not allow to detect can_queue correctly */
	if (shost->can_queue > MAX_MAILBOXES || shost->can_queue < 2) {
		printk("%s: detect, wrong n. of mbox %d, fixed.\n",
		       ha->board_name, shost->can_queue);
		shost->can_queue = MAX_MAILBOXES;
	}

	if (protocol_rev != 'A') {
		if (info.max_chan > 0 && info.max_chan < MAX_CHANNEL)
			shost->max_channel = info.max_chan;

		if (info.max_id > 7 && info.max_id < MAX_TARGET)
			shost->max_id = info.max_id + 1;

		if (info.large_sg && shost->sg_tablesize == MAX_SGLIST)
			shost->sg_tablesize = MAX_LARGE_SGLIST;
	}

	if (protocol_rev == 'C') {
		if (info.max_lun > 7 && info.max_lun < MAX_LUN)
			shost->max_lun = info.max_lun + 1;
	}

	if (dma_channel == NO_DMA)
		sprintf(dma_name, "%s", "BMST");
	else
		sprintf(dma_name, "DMA %u", dma_channel);

	spin_unlock_irq(&driver_lock);

	for (i = 0; i < shost->can_queue; i++)
		ha->cp[i].cp_dma_addr = pci_map_single(ha->pdev,
							  &ha->cp[i],
							  sizeof(struct mscp),
							  PCI_DMA_BIDIRECTIONAL);

	for (i = 0; i < shost->can_queue; i++) {
		size_t sz = shost->sg_tablesize *sizeof(struct sg_list);
		gfp_t gfp_mask = (shost->unchecked_isa_dma ? GFP_DMA : 0) | GFP_ATOMIC;
		ha->cp[i].sglist = kmalloc(sz, gfp_mask);
		if (!ha->cp[i].sglist) {
			printk
			    ("%s: kmalloc SGlist failed, mbox %d, detaching.\n",
			     ha->board_name, i);
			goto release;
		}
	}

	if (!(ha->sp_cpu_addr = pci_alloc_consistent(ha->pdev,
							sizeof(struct mssp),
							&ha->sp_dma_addr))) {
		printk("%s: pci_alloc_consistent failed, detaching.\n", ha->board_name);
		goto release;
	}

	if (max_queue_depth > MAX_TAGGED_CMD_PER_LUN)
		max_queue_depth = MAX_TAGGED_CMD_PER_LUN;

	if (max_queue_depth < MAX_CMD_PER_LUN)
		max_queue_depth = MAX_CMD_PER_LUN;

	if (tag_mode != TAG_DISABLED && tag_mode != TAG_SIMPLE)
		tag_mode = TAG_ORDERED;

	if (j == 0) {
		printk
		    ("EATA/DMA 2.0x: Copyright (C) 1994-2003 Dario Ballabio.\n");
		printk
		    ("%s config options -> tm:%d, lc:%c, mq:%d, rs:%c, et:%c, "
		     "ip:%c, ep:%c, pp:%c.\n", driver_name, tag_mode,
		     YESNO(linked_comm), max_queue_depth, YESNO(rev_scan),
		     YESNO(ext_tran), YESNO(isa_probe), YESNO(eisa_probe),
		     YESNO(pci_probe));
	}

	printk("%s: 2.0%c, %s 0x%03lx, IRQ %u, %s, SG %d, MB %d.\n",
	       ha->board_name, ha->protocol_rev, bus_type,
	       (unsigned long)shost->io_port, shost->irq, dma_name,
	       shost->sg_tablesize, shost->can_queue);

	if (shost->max_id > 8 || shost->max_lun > 8)
		printk
		    ("%s: wide SCSI support enabled, max_id %u, max_lun %u.\n",
		     ha->board_name, shost->max_id, shost->max_lun);

	for (i = 0; i <= shost->max_channel; i++)
		printk("%s: SCSI channel %u enabled, host target ID %d.\n",
		       ha->board_name, i, info.host_addr[3 - i]);

#if defined(DEBUG_DETECT)
	printk("%s: Vers. 0x%x, ocs %u, tar %u, trnxfr %u, more %u, SYNC 0x%x, "
	       "sec. %u, infol %d, cpl %d spl %d.\n", name, info.version,
	       info.ocsena, info.tarsup, info.trnxfr, info.morsup, info.sync,
	       info.second, info.data_len, info.cp_len, info.sp_len);

	if (protocol_rev == 'B' || protocol_rev == 'C')
		printk("%s: isaena %u, forcaddr %u, max_id %u, max_chan %u, "
		       "large_sg %u, res1 %u.\n", name, info.isaena,
		       info.forcaddr, info.max_id, info.max_chan, info.large_sg,
		       info.res1);

	if (protocol_rev == 'C')
		printk("%s: max_lun %u, m1 %u, idquest %u, pci %u, eisa %u, "
		       "raidnum %u.\n", name, info.max_lun, info.m1,
		       info.idquest, info.pci, info.eisa, info.raidnum);
#endif

	if (ha->pdev) {
		pci_set_master(ha->pdev);
		if (pci_set_dma_mask(ha->pdev, DMA_32BIT_MASK))
			printk("%s: warning, pci_set_dma_mask failed.\n",
			       ha->board_name);
	}

	return 1;

      freedma:
	if (subversion == ISA)
		free_dma(dma_channel);
      freeirq:
	free_irq(irq, &sha[j]);
      freelock:
	spin_unlock_irq(&driver_lock);
	release_region(port_base, REGION_SIZE);
      fail:
	return 0;

      release:
	eata2x_release(shost);
	return 0;
}

static void internal_setup(char *str, int *ints)
{
	int i, argc = ints[0];
	char *cur = str, *pc;

	if (argc > 0) {
		if (argc > MAX_INT_PARAM)
			argc = MAX_INT_PARAM;

		for (i = 0; i < argc; i++)
			io_port[i] = ints[i + 1];

		io_port[i] = 0;
		setup_done = 1;
	}

	while (cur && (pc = strchr(cur, ':'))) {
		int val = 0, c = *++pc;

		if (c == 'n' || c == 'N')
			val = 0;
		else if (c == 'y' || c == 'Y')
			val = 1;
		else
			val = (int)simple_strtoul(pc, NULL, 0);

		if (!strncmp(cur, "lc:", 3))
			linked_comm = val;
		else if (!strncmp(cur, "tm:", 3))
			tag_mode = val;
		else if (!strncmp(cur, "tc:", 3))
			tag_mode = val;
		else if (!strncmp(cur, "mq:", 3))
			max_queue_depth = val;
		else if (!strncmp(cur, "ls:", 3))
			link_statistics = val;
		else if (!strncmp(cur, "et:", 3))
			ext_tran = val;
		else if (!strncmp(cur, "rs:", 3))
			rev_scan = val;
		else if (!strncmp(cur, "ip:", 3))
			isa_probe = val;
		else if (!strncmp(cur, "ep:", 3))
			eisa_probe = val;
		else if (!strncmp(cur, "pp:", 3))
			pci_probe = val;

		if ((cur = strchr(cur, ',')))
			++cur;
	}

	return;
}

static int option_setup(char *str)
{
	int ints[MAX_INT_PARAM];
	char *cur = str;
	int i = 1;

	while (cur && isdigit(*cur) && i <= MAX_INT_PARAM) {
		ints[i++] = simple_strtoul(cur, NULL, 0);

		if ((cur = strchr(cur, ',')) != NULL)
			cur++;
	}

	ints[0] = i - 1;
	internal_setup(cur, ints);
	return 1;
}

static void add_pci_ports(void)
{
#if defined(CONFIG_PCI)
	unsigned int addr, k;
	struct pci_dev *dev = NULL;

	for (k = 0; k < MAX_PCI; k++) {

		if (!(dev = pci_get_class(PCI_CLASS_STORAGE_SCSI << 8, dev)))
			break;

		if (pci_enable_device(dev)) {
#if defined(DEBUG_PCI_DETECT)
			printk
			    ("%s: detect, bus %d, devfn 0x%x, pci_enable_device failed.\n",
			     driver_name, dev->bus->number, dev->devfn);
#endif

			continue;
		}

		addr = pci_resource_start(dev, 0);

#if defined(DEBUG_PCI_DETECT)
		printk("%s: detect, seq. %d, bus %d, devfn 0x%x, addr 0x%x.\n",
		       driver_name, k, dev->bus->number, dev->devfn, addr);
#endif

		/* Order addresses according to rev_scan value */
		io_port[MAX_INT_PARAM + (rev_scan ? (MAX_PCI - k) : (1 + k))] =
		    addr + PCI_BASE_ADDRESS_0;
	}

	pci_dev_put(dev);
#endif				/* end CONFIG_PCI */
}

static int eata2x_detect(struct scsi_host_template *tpnt)
{
	unsigned int j = 0, k;

	tpnt->proc_name = "eata2x";

	if (strlen(boot_options))
		option_setup(boot_options);

#if defined(MODULE)
	/* io_port could have been modified when loading as a module */
	if (io_port[0] != SKIP) {
		setup_done = 1;
		io_port[MAX_INT_PARAM] = 0;
	}
#endif

	for (k = MAX_INT_PARAM; io_port[k]; k++)
		if (io_port[k] == SKIP)
			continue;
		else if (io_port[k] <= MAX_ISA_ADDR) {
			if (!isa_probe)
				io_port[k] = SKIP;
		} else if (io_port[k] >= MIN_EISA_ADDR
			   && io_port[k] <= MAX_EISA_ADDR) {
			if (!eisa_probe)
				io_port[k] = SKIP;
		}

	if (pci_probe) {
		if (!setup_done)
			add_pci_ports();
		else
			enable_pci_ports();
	}

	for (k = 0; io_port[k]; k++) {

		if (io_port[k] == SKIP)
			continue;

		if (j < MAX_BOARDS && port_detect(io_port[k], j, tpnt))
			j++;
	}

	num_boards = j;
	return j;
}

static void map_dma(unsigned int i, struct hostdata *ha)
{
	unsigned int k, pci_dir;
	int count;
	struct scatterlist *sg;
	struct mscp *cpp;
	struct scsi_cmnd *SCpnt;

	cpp = &ha->cp[i];
	SCpnt = cpp->SCpnt;
	pci_dir = SCpnt->sc_data_direction;

	if (SCpnt->sense_buffer)
		cpp->sense_addr =
		    H2DEV(pci_map_single(ha->pdev, SCpnt->sense_buffer,
			   SCSI_SENSE_BUFFERSIZE, PCI_DMA_FROMDEVICE));

	cpp->sense_len = SCSI_SENSE_BUFFERSIZE;

	count = scsi_dma_map(SCpnt);
	BUG_ON(count < 0);
	scsi_for_each_sg(SCpnt, sg, count, k) {
		cpp->sglist[k].address = H2DEV(sg_dma_address(sg));
		cpp->sglist[k].num_bytes = H2DEV(sg_dma_len(sg));
	}

	cpp->sg = 1;
	cpp->data_address = H2DEV(pci_map_single(ha->pdev, cpp->sglist,
						 scsi_sg_count(SCpnt) *
						 sizeof(struct sg_list),
						 pci_dir));
	cpp->data_len = H2DEV((scsi_sg_count(SCpnt) * sizeof(struct sg_list)));
}

static void unmap_dma(unsigned int i, struct hostdata *ha)
{
	unsigned int pci_dir;
	struct mscp *cpp;
	struct scsi_cmnd *SCpnt;

	cpp = &ha->cp[i];
	SCpnt = cpp->SCpnt;
	pci_dir = SCpnt->sc_data_direction;

	if (DEV2H(cpp->sense_addr))
		pci_unmap_single(ha->pdev, DEV2H(cpp->sense_addr),
				 DEV2H(cpp->sense_len), PCI_DMA_FROMDEVICE);

	scsi_dma_unmap(SCpnt);

	if (!DEV2H(cpp->data_len))
		pci_dir = PCI_DMA_BIDIRECTIONAL;

	if (DEV2H(cpp->data_address))
		pci_unmap_single(ha->pdev, DEV2H(cpp->data_address),
				 DEV2H(cpp->data_len), pci_dir);
}

static void sync_dma(unsigned int i, struct hostdata *ha)
{
	unsigned int pci_dir;
	struct mscp *cpp;
	struct scsi_cmnd *SCpnt;

	cpp = &ha->cp[i];
	SCpnt = cpp->SCpnt;
	pci_dir = SCpnt->sc_data_direction;

	if (DEV2H(cpp->sense_addr))
		pci_dma_sync_single_for_cpu(ha->pdev, DEV2H(cpp->sense_addr),
					    DEV2H(cpp->sense_len),
					    PCI_DMA_FROMDEVICE);

	if (scsi_sg_count(SCpnt))
		pci_dma_sync_sg_for_cpu(ha->pdev, scsi_sglist(SCpnt),
					scsi_sg_count(SCpnt), pci_dir);

	if (!DEV2H(cpp->data_len))
		pci_dir = PCI_DMA_BIDIRECTIONAL;

	if (DEV2H(cpp->data_address))
		pci_dma_sync_single_for_cpu(ha->pdev,
					    DEV2H(cpp->data_address),
					    DEV2H(cpp->data_len), pci_dir);
}

static void scsi_to_dev_dir(unsigned int i, struct hostdata *ha)
{
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

	cpp = &ha->cp[i];
	SCpnt = cpp->SCpnt;

	if (SCpnt->sc_data_direction == DMA_FROM_DEVICE) {
		cpp->din = 1;
		cpp->dout = 0;
		return;
	} else if (SCpnt->sc_data_direction == DMA_TO_DEVICE) {
		cpp->din = 0;
		cpp->dout = 1;
		return;
	} else if (SCpnt->sc_data_direction == DMA_NONE) {
		cpp->din = 0;
		cpp->dout = 0;
		return;
	}

	if (SCpnt->sc_data_direction != DMA_BIDIRECTIONAL)
		panic("%s: qcomm, invalid SCpnt->sc_data_direction.\n",
				ha->board_name);

	for (k = 0; k < ARRAY_SIZE(data_out_cmds); k++)
		if (SCpnt->cmnd[0] == data_out_cmds[k]) {
			cpp->dout = 1;
			break;
		}

	if ((cpp->din = !cpp->dout))
		for (k = 0; k < ARRAY_SIZE(data_none_cmds); k++)
			if (SCpnt->cmnd[0] == data_none_cmds[k]) {
				cpp->din = 0;
				break;
			}

}

static int eata2x_queuecommand(struct scsi_cmnd *SCpnt,
			       void (*done) (struct scsi_cmnd *))
{
	struct Scsi_Host *shost = SCpnt->device->host;
	struct hostdata *ha = (struct hostdata *)shost->hostdata;
	unsigned int i, k;
	struct mscp *cpp;

	if (SCpnt->host_scribble)
		panic("%s: qcomm, pid %ld, SCpnt %p already active.\n",
		      ha->board_name, SCpnt->serial_number, SCpnt);

	/* i is the mailbox number, look for the first free mailbox
	   starting from last_cp_used */
	i = ha->last_cp_used + 1;

	for (k = 0; k < shost->can_queue; k++, i++) {
		if (i >= shost->can_queue)
			i = 0;
		if (ha->cp_stat[i] == FREE) {
			ha->last_cp_used = i;
			break;
		}
	}

	if (k == shost->can_queue) {
		printk("%s: qcomm, no free mailbox.\n", ha->board_name);
		return 1;
	}

	/* Set pointer to control packet structure */
	cpp = &ha->cp[i];

	memset(cpp, 0, sizeof(struct mscp) - CP_TAIL_SIZE);

	/* Set pointer to status packet structure, Big Endian format */
	cpp->sp_dma_addr = H2DEV(ha->sp_dma_addr);

	SCpnt->scsi_done = done;
	cpp->cpp_index = i;
	SCpnt->host_scribble = (unsigned char *)&cpp->cpp_index;

	if (do_trace)
		scmd_printk(KERN_INFO, SCpnt,
			"qcomm, mbox %d, pid %ld.\n", i, SCpnt->serial_number);

	cpp->reqsen = 1;
	cpp->dispri = 1;
#if 0
	if (SCpnt->device->type == TYPE_TAPE)
		cpp->hbaci = 1;
#endif
	cpp->one = 1;
	cpp->channel = SCpnt->device->channel;
	cpp->target = SCpnt->device->id;
	cpp->lun = SCpnt->device->lun;
	cpp->SCpnt = SCpnt;
	memcpy(cpp->cdb, SCpnt->cmnd, SCpnt->cmd_len);

	/* Use data transfer direction SCpnt->sc_data_direction */
	scsi_to_dev_dir(i, ha);

	/* Map DMA buffers and SG list */
	map_dma(i, ha);

	if (linked_comm && SCpnt->device->queue_depth > 2
	    && TLDEV(SCpnt->device->type)) {
		ha->cp_stat[i] = READY;
		flush_dev(SCpnt->device, SCpnt->request->sector, ha, 0);
		return 0;
	}

	/* Send control packet to the board */
	if (do_dma(shost->io_port, cpp->cp_dma_addr, SEND_CP_DMA)) {
		unmap_dma(i, ha);
		SCpnt->host_scribble = NULL;
		scmd_printk(KERN_INFO, SCpnt,
			"qcomm, pid %ld, adapter busy.\n", SCpnt->serial_number);
		return 1;
	}

	ha->cp_stat[i] = IN_USE;
	return 0;
}

static int eata2x_eh_abort(struct scsi_cmnd *SCarg)
{
	struct Scsi_Host *shost = SCarg->device->host;
	struct hostdata *ha = (struct hostdata *)shost->hostdata;
	unsigned int i;

	if (SCarg->host_scribble == NULL) {
		scmd_printk(KERN_INFO, SCarg,
			"abort, pid %ld inactive.\n", SCarg->serial_number);
		return SUCCESS;
	}

	i = *(unsigned int *)SCarg->host_scribble;
	scmd_printk(KERN_WARNING, SCarg,
		"abort, mbox %d, pid %ld.\n", i, SCarg->serial_number);

	if (i >= shost->can_queue)
		panic("%s: abort, invalid SCarg->host_scribble.\n", ha->board_name);

	if (wait_on_busy(shost->io_port, MAXLOOP)) {
		printk("%s: abort, timeout error.\n", ha->board_name);
		return FAILED;
	}

	if (ha->cp_stat[i] == FREE) {
		printk("%s: abort, mbox %d is free.\n", ha->board_name, i);
		return SUCCESS;
	}

	if (ha->cp_stat[i] == IN_USE) {
		printk("%s: abort, mbox %d is in use.\n", ha->board_name, i);

		if (SCarg != ha->cp[i].SCpnt)
			panic("%s: abort, mbox %d, SCarg %p, cp SCpnt %p.\n",
			      ha->board_name, i, SCarg, ha->cp[i].SCpnt);

		if (inb(shost->io_port + REG_AUX_STATUS) & IRQ_ASSERTED)
			printk("%s: abort, mbox %d, interrupt pending.\n",
			       ha->board_name, i);

		return FAILED;
	}

	if (ha->cp_stat[i] == IN_RESET) {
		printk("%s: abort, mbox %d is in reset.\n", ha->board_name, i);
		return FAILED;
	}

	if (ha->cp_stat[i] == LOCKED) {
		printk("%s: abort, mbox %d is locked.\n", ha->board_name, i);
		return SUCCESS;
	}

	if (ha->cp_stat[i] == READY || ha->cp_stat[i] == ABORTING) {
		unmap_dma(i, ha);
		SCarg->result = DID_ABORT << 16;
		SCarg->host_scribble = NULL;
		ha->cp_stat[i] = FREE;
		printk("%s, abort, mbox %d ready, DID_ABORT, pid %ld done.\n",
		       ha->board_name, i, SCarg->serial_number);
		SCarg->scsi_done(SCarg);
		return SUCCESS;
	}

	panic("%s: abort, mbox %d, invalid cp_stat.\n", ha->board_name, i);
}

static int eata2x_eh_host_reset(struct scsi_cmnd *SCarg)
{
	unsigned int i, time, k, c, limit = 0;
	int arg_done = 0;
	struct scsi_cmnd *SCpnt;
	struct Scsi_Host *shost = SCarg->device->host;
	struct hostdata *ha = (struct hostdata *)shost->hostdata;

	scmd_printk(KERN_INFO, SCarg,
		"reset, enter, pid %ld.\n", SCarg->serial_number);

	spin_lock_irq(shost->host_lock);

	if (SCarg->host_scribble == NULL)
		printk("%s: reset, pid %ld inactive.\n", ha->board_name, SCarg->serial_number);

	if (ha->in_reset) {
		printk("%s: reset, exit, already in reset.\n", ha->board_name);
		spin_unlock_irq(shost->host_lock);
		return FAILED;
	}

	if (wait_on_busy(shost->io_port, MAXLOOP)) {
		printk("%s: reset, exit, timeout error.\n", ha->board_name);
		spin_unlock_irq(shost->host_lock);
		return FAILED;
	}

	ha->retries = 0;

	for (c = 0; c <= shost->max_channel; c++)
		for (k = 0; k < shost->max_id; k++) {
			ha->target_redo[k][c] = 1;
			ha->target_to[k][c] = 0;
		}

	for (i = 0; i < shost->can_queue; i++) {

		if (ha->cp_stat[i] == FREE)
			continue;

		if (ha->cp_stat[i] == LOCKED) {
			ha->cp_stat[i] = FREE;
			printk("%s: reset, locked mbox %d forced free.\n",
			       ha->board_name, i);
			continue;
		}

		if (!(SCpnt = ha->cp[i].SCpnt))
			panic("%s: reset, mbox %d, SCpnt == NULL.\n", ha->board_name, i);

		if (ha->cp_stat[i] == READY || ha->cp_stat[i] == ABORTING) {
			ha->cp_stat[i] = ABORTING;
			printk("%s: reset, mbox %d aborting, pid %ld.\n",
			       ha->board_name, i, SCpnt->serial_number);
		}

		else {
			ha->cp_stat[i] = IN_RESET;
			printk("%s: reset, mbox %d in reset, pid %ld.\n",
			       ha->board_name, i, SCpnt->serial_number);
		}

		if (SCpnt->host_scribble == NULL)
			panic("%s: reset, mbox %d, garbled SCpnt.\n", ha->board_name, i);

		if (*(unsigned int *)SCpnt->host_scribble != i)
			panic("%s: reset, mbox %d, index mismatch.\n", ha->board_name, i);

		if (SCpnt->scsi_done == NULL)
			panic("%s: reset, mbox %d, SCpnt->scsi_done == NULL.\n",
			      ha->board_name, i);

		if (SCpnt == SCarg)
			arg_done = 1;
	}

	if (do_dma(shost->io_port, 0, RESET_PIO)) {
		printk("%s: reset, cannot reset, timeout error.\n", ha->board_name);
		spin_unlock_irq(shost->host_lock);
		return FAILED;
	}

	printk("%s: reset, board reset done, enabling interrupts.\n", ha->board_name);

#if defined(DEBUG_RESET)
	do_trace = 1;
#endif

	ha->in_reset = 1;

	spin_unlock_irq(shost->host_lock);

	/* FIXME: use a sleep instead */
	time = jiffies;
	while ((jiffies - time) < (10 * HZ) && limit++ < 200000)
		udelay(100L);

	spin_lock_irq(shost->host_lock);

	printk("%s: reset, interrupts disabled, loops %d.\n", ha->board_name, limit);

	for (i = 0; i < shost->can_queue; i++) {

		if (ha->cp_stat[i] == IN_RESET) {
			SCpnt = ha->cp[i].SCpnt;
			unmap_dma(i, ha);
			SCpnt->result = DID_RESET << 16;
			SCpnt->host_scribble = NULL;

			/* This mailbox is still waiting for its interrupt */
			ha->cp_stat[i] = LOCKED;

			printk
			    ("%s, reset, mbox %d locked, DID_RESET, pid %ld done.\n",
			     ha->board_name, i, SCpnt->serial_number);
		}

		else if (ha->cp_stat[i] == ABORTING) {
			SCpnt = ha->cp[i].SCpnt;
			unmap_dma(i, ha);
			SCpnt->result = DID_RESET << 16;
			SCpnt->host_scribble = NULL;

			/* This mailbox was never queued to the adapter */
			ha->cp_stat[i] = FREE;

			printk
			    ("%s, reset, mbox %d aborting, DID_RESET, pid %ld done.\n",
			     ha->board_name, i, SCpnt->serial_number);
		}

		else
			/* Any other mailbox has already been set free by interrupt */
			continue;

		SCpnt->scsi_done(SCpnt);
	}

	ha->in_reset = 0;
	do_trace = 0;

	if (arg_done)
		printk("%s: reset, exit, pid %ld done.\n", ha->board_name, SCarg->serial_number);
	else
		printk("%s: reset, exit.\n", ha->board_name);

	spin_unlock_irq(shost->host_lock);
	return SUCCESS;
}

int eata2x_bios_param(struct scsi_device *sdev, struct block_device *bdev,
		      sector_t capacity, int *dkinfo)
{
	unsigned int size = capacity;

	if (ext_tran || (scsicam_bios_param(bdev, capacity, dkinfo) < 0)) {
		dkinfo[0] = 255;
		dkinfo[1] = 63;
		dkinfo[2] = size / (dkinfo[0] * dkinfo[1]);
	}
#if defined (DEBUG_GEOMETRY)
	printk("%s: bios_param, head=%d, sec=%d, cyl=%d.\n", driver_name,
	       dkinfo[0], dkinfo[1], dkinfo[2]);
#endif

	return 0;
}

static void sort(unsigned long sk[], unsigned int da[], unsigned int n,
		 unsigned int rev)
{
	unsigned int i, j, k, y;
	unsigned long x;

	for (i = 0; i < n - 1; i++) {
		k = i;

		for (j = k + 1; j < n; j++)
			if (rev) {
				if (sk[j] > sk[k])
					k = j;
			} else {
				if (sk[j] < sk[k])
					k = j;
			}

		if (k != i) {
			x = sk[k];
			sk[k] = sk[i];
			sk[i] = x;
			y = da[k];
			da[k] = da[i];
			da[i] = y;
		}
	}

	return;
}

static int reorder(struct hostdata *ha, unsigned long cursec,
		   unsigned int ihdlr, unsigned int il[], unsigned int n_ready)
{
	struct scsi_cmnd *SCpnt;
	struct mscp *cpp;
	unsigned int k, n;
	unsigned int rev = 0, s = 1, r = 1;
	unsigned int input_only = 1, overlap = 0;
	unsigned long sl[n_ready], pl[n_ready], ll[n_ready];
	unsigned long maxsec = 0, minsec = ULONG_MAX, seek = 0, iseek = 0;
	unsigned long ioseek = 0;

	static unsigned int flushcount = 0, batchcount = 0, sortcount = 0;
	static unsigned int readycount = 0, ovlcount = 0, inputcount = 0;
	static unsigned int readysorted = 0, revcount = 0;
	static unsigned long seeksorted = 0, seeknosort = 0;

	if (link_statistics && !(++flushcount % link_statistics))
		printk("fc %d bc %d ic %d oc %d rc %d rs %d sc %d re %d"
		       " av %ldK as %ldK.\n", flushcount, batchcount,
		       inputcount, ovlcount, readycount, readysorted, sortcount,
		       revcount, seeknosort / (readycount + 1),
		       seeksorted / (readycount + 1));

	if (n_ready <= 1)
		return 0;

	for (n = 0; n < n_ready; n++) {
		k = il[n];
		cpp = &ha->cp[k];
		SCpnt = cpp->SCpnt;

		if (!cpp->din)
			input_only = 0;

		if (SCpnt->request->sector < minsec)
			minsec = SCpnt->request->sector;
		if (SCpnt->request->sector > maxsec)
			maxsec = SCpnt->request->sector;

		sl[n] = SCpnt->request->sector;
		ioseek += SCpnt->request->nr_sectors;

		if (!n)
			continue;

		if (sl[n] < sl[n - 1])
			s = 0;
		if (sl[n] > sl[n - 1])
			r = 0;

		if (link_statistics) {
			if (sl[n] > sl[n - 1])
				seek += sl[n] - sl[n - 1];
			else
				seek += sl[n - 1] - sl[n];
		}

	}

	if (link_statistics) {
		if (cursec > sl[0])
			seek += cursec - sl[0];
		else
			seek += sl[0] - cursec;
	}

	if (cursec > ((maxsec + minsec) / 2))
		rev = 1;

	if (ioseek > ((maxsec - minsec) / 2))
		rev = 0;

	if (!((rev && r) || (!rev && s)))
		sort(sl, il, n_ready, rev);

	if (!input_only)
		for (n = 0; n < n_ready; n++) {
			k = il[n];
			cpp = &ha->cp[k];
			SCpnt = cpp->SCpnt;
			ll[n] = SCpnt->request->nr_sectors;
			pl[n] = SCpnt->serial_number;

			if (!n)
				continue;

			if ((sl[n] == sl[n - 1])
			    || (!rev && ((sl[n - 1] + ll[n - 1]) > sl[n]))
			    || (rev && ((sl[n] + ll[n]) > sl[n - 1])))
				overlap = 1;
		}

	if (overlap)
		sort(pl, il, n_ready, 0);

	if (link_statistics) {
		if (cursec > sl[0])
			iseek = cursec - sl[0];
		else
			iseek = sl[0] - cursec;
		batchcount++;
		readycount += n_ready;
		seeknosort += seek / 1024;
		if (input_only)
			inputcount++;
		if (overlap) {
			ovlcount++;
			seeksorted += iseek / 1024;
		} else
			seeksorted += (iseek + maxsec - minsec) / 1024;
		if (rev && !r) {
			revcount++;
			readysorted += n_ready;
		}
		if (!rev && !s) {
			sortcount++;
			readysorted += n_ready;
		}
	}
#if defined(DEBUG_LINKED_COMMANDS)
	if (link_statistics && (overlap || !(flushcount % link_statistics)))
		for (n = 0; n < n_ready; n++) {
			k = il[n];
			cpp = &ha->cp[k];
			SCpnt = cpp->SCpnt;
			scmd_printk(KERN_INFO, SCpnt,
			    "%s pid %ld mb %d fc %d nr %d sec %ld ns %ld"
			     " cur %ld s:%c r:%c rev:%c in:%c ov:%c xd %d.\n",
			     (ihdlr ? "ihdlr" : "qcomm"),
			     SCpnt->serial_number, k, flushcount,
			     n_ready, SCpnt->request->sector,
			     SCpnt->request->nr_sectors, cursec, YESNO(s),
			     YESNO(r), YESNO(rev), YESNO(input_only),
			     YESNO(overlap), cpp->din);
		}
#endif
	return overlap;
}

static void flush_dev(struct scsi_device *dev, unsigned long cursec,
		      struct hostdata *ha, unsigned int ihdlr)
{
	struct scsi_cmnd *SCpnt;
	struct mscp *cpp;
	unsigned int k, n, n_ready = 0, il[MAX_MAILBOXES];

	for (k = 0; k < dev->host->can_queue; k++) {

		if (ha->cp_stat[k] != READY && ha->cp_stat[k] != IN_USE)
			continue;

		cpp = &ha->cp[k];
		SCpnt = cpp->SCpnt;

		if (SCpnt->device != dev)
			continue;

		if (ha->cp_stat[k] == IN_USE)
			return;

		il[n_ready++] = k;
	}

	if (reorder(ha, cursec, ihdlr, il, n_ready))
		n_ready = 1;

	for (n = 0; n < n_ready; n++) {
		k = il[n];
		cpp = &ha->cp[k];
		SCpnt = cpp->SCpnt;

		if (do_dma(dev->host->io_port, cpp->cp_dma_addr, SEND_CP_DMA)) {
			scmd_printk(KERN_INFO, SCpnt,
			    "%s, pid %ld, mbox %d, adapter"
			     " busy, will abort.\n",
			     (ihdlr ? "ihdlr" : "qcomm"),
			     SCpnt->serial_number, k);
			ha->cp_stat[k] = ABORTING;
			continue;
		}

		ha->cp_stat[k] = IN_USE;
	}
}

static irqreturn_t ihdlr(struct Scsi_Host *shost)
{
	struct scsi_cmnd *SCpnt;
	unsigned int i, k, c, status, tstatus, reg;
	struct mssp *spp;
	struct mscp *cpp;
	struct hostdata *ha = (struct hostdata *)shost->hostdata;
	int irq = shost->irq;

	/* Check if this board need to be serviced */
	if (!(inb(shost->io_port + REG_AUX_STATUS) & IRQ_ASSERTED))
		goto none;

	ha->iocount++;

	if (do_trace)
		printk("%s: ihdlr, enter, irq %d, count %d.\n", ha->board_name, irq,
		       ha->iocount);

	/* Check if this board is still busy */
	if (wait_on_busy(shost->io_port, 20 * MAXLOOP)) {
		reg = inb(shost->io_port + REG_STATUS);
		printk
		    ("%s: ihdlr, busy timeout error,  irq %d, reg 0x%x, count %d.\n",
		     ha->board_name, irq, reg, ha->iocount);
		goto none;
	}

	spp = &ha->sp;

	/* Make a local copy just before clearing the interrupt indication */
	memcpy(spp, ha->sp_cpu_addr, sizeof(struct mssp));

	/* Clear the completion flag and cp pointer on the dynamic copy of sp */
	memset(ha->sp_cpu_addr, 0, sizeof(struct mssp));

	/* Read the status register to clear the interrupt indication */
	reg = inb(shost->io_port + REG_STATUS);

#if defined (DEBUG_INTERRUPT)
	{
		unsigned char *bytesp;
		int cnt;
		bytesp = (unsigned char *)spp;
		if (ha->iocount < 200) {
			printk("sp[] =");
			for (cnt = 0; cnt < 15; cnt++)
				printk(" 0x%x", bytesp[cnt]);
			printk("\n");
		}
	}
#endif

	/* Reject any sp with supspect data */
	if (spp->eoc == 0 && ha->iocount > 1)
		printk
		    ("%s: ihdlr, spp->eoc == 0, irq %d, reg 0x%x, count %d.\n",
		     ha->board_name, irq, reg, ha->iocount);
	if (spp->cpp_index < 0 || spp->cpp_index >= shost->can_queue)
		printk
		    ("%s: ihdlr, bad spp->cpp_index %d, irq %d, reg 0x%x, count %d.\n",
		     ha->board_name, spp->cpp_index, irq, reg, ha->iocount);
	if (spp->eoc == 0 || spp->cpp_index < 0
	    || spp->cpp_index >= shost->can_queue)
		goto handled;

	/* Find the mailbox to be serviced on this board */
	i = spp->cpp_index;

	cpp = &(ha->cp[i]);

#if defined(DEBUG_GENERATE_ABORTS)
	if ((ha->iocount > 500) && ((ha->iocount % 500) < 3))
		goto handled;
#endif

	if (ha->cp_stat[i] == IGNORE) {
		ha->cp_stat[i] = FREE;
		goto handled;
	} else if (ha->cp_stat[i] == LOCKED) {
		ha->cp_stat[i] = FREE;
		printk("%s: ihdlr, mbox %d unlocked, count %d.\n", ha->board_name, i,
		       ha->iocount);
		goto handled;
	} else if (ha->cp_stat[i] == FREE) {
		printk("%s: ihdlr, mbox %d is free, count %d.\n", ha->board_name, i,
		       ha->iocount);
		goto handled;
	} else if (ha->cp_stat[i] == IN_RESET)
		printk("%s: ihdlr, mbox %d is in reset.\n", ha->board_name, i);
	else if (ha->cp_stat[i] != IN_USE)
		panic("%s: ihdlr, mbox %d, invalid cp_stat: %d.\n",
		      ha->board_name, i, ha->cp_stat[i]);

	ha->cp_stat[i] = FREE;
	SCpnt = cpp->SCpnt;

	if (SCpnt == NULL)
		panic("%s: ihdlr, mbox %d, SCpnt == NULL.\n", ha->board_name, i);

	if (SCpnt->host_scribble == NULL)
		panic("%s: ihdlr, mbox %d, pid %ld, SCpnt %p garbled.\n", ha->board_name,
		      i, SCpnt->serial_number, SCpnt);

	if (*(unsigned int *)SCpnt->host_scribble != i)
		panic("%s: ihdlr, mbox %d, pid %ld, index mismatch %d.\n",
		      ha->board_name, i, SCpnt->serial_number,
		      *(unsigned int *)SCpnt->host_scribble);

	sync_dma(i, ha);

	if (linked_comm && SCpnt->device->queue_depth > 2
	    && TLDEV(SCpnt->device->type))
		flush_dev(SCpnt->device, SCpnt->request->sector, ha, 1);

	tstatus = status_byte(spp->target_status);

#if defined(DEBUG_GENERATE_ERRORS)
	if ((ha->iocount > 500) && ((ha->iocount % 200) < 2))
		spp->adapter_status = 0x01;
#endif

	switch (spp->adapter_status) {
	case ASOK:		/* status OK */

		/* Forces a reset if a disk drive keeps returning BUSY */
		if (tstatus == BUSY && SCpnt->device->type != TYPE_TAPE)
			status = DID_ERROR << 16;

		/* If there was a bus reset, redo operation on each target */
		else if (tstatus != GOOD && SCpnt->device->type == TYPE_DISK
			 && ha->target_redo[SCpnt->device->id][SCpnt->
								  device->
								  channel])
			status = DID_BUS_BUSY << 16;

		/* Works around a flaw in scsi.c */
		else if (tstatus == CHECK_CONDITION
			 && SCpnt->device->type == TYPE_DISK
			 && (SCpnt->sense_buffer[2] & 0xf) == RECOVERED_ERROR)
			status = DID_BUS_BUSY << 16;

		else
			status = DID_OK << 16;

		if (tstatus == GOOD)
			ha->target_redo[SCpnt->device->id][SCpnt->device->
							      channel] = 0;

		if (spp->target_status && SCpnt->device->type == TYPE_DISK &&
		    (!(tstatus == CHECK_CONDITION && ha->iocount <= 1000 &&
		       (SCpnt->sense_buffer[2] & 0xf) == NOT_READY)))
			printk("%s: ihdlr, target %d.%d:%d, pid %ld, "
			       "target_status 0x%x, sense key 0x%x.\n",
			       ha->board_name,
			       SCpnt->device->channel, SCpnt->device->id,
			       SCpnt->device->lun, SCpnt->serial_number,
			       spp->target_status, SCpnt->sense_buffer[2]);

		ha->target_to[SCpnt->device->id][SCpnt->device->channel] = 0;

		if (ha->last_retried_pid == SCpnt->serial_number)
			ha->retries = 0;

		break;
	case ASST:		/* Selection Time Out */
	case 0x02:		/* Command Time Out   */

		if (ha->target_to[SCpnt->device->id][SCpnt->device->channel] > 1)
			status = DID_ERROR << 16;
		else {
			status = DID_TIME_OUT << 16;
			ha->target_to[SCpnt->device->id][SCpnt->device->
							    channel]++;
		}

		break;

		/* Perform a limited number of internal retries */
	case 0x03:		/* SCSI Bus Reset Received */
	case 0x04:		/* Initial Controller Power-up */

		for (c = 0; c <= shost->max_channel; c++)
			for (k = 0; k < shost->max_id; k++)
				ha->target_redo[k][c] = 1;

		if (SCpnt->device->type != TYPE_TAPE
		    && ha->retries < MAX_INTERNAL_RETRIES) {

#if defined(DID_SOFT_ERROR)
			status = DID_SOFT_ERROR << 16;
#else
			status = DID_BUS_BUSY << 16;
#endif

			ha->retries++;
			ha->last_retried_pid = SCpnt->serial_number;
		} else
			status = DID_ERROR << 16;

		break;
	case 0x05:		/* Unexpected Bus Phase */
	case 0x06:		/* Unexpected Bus Free */
	case 0x07:		/* Bus Parity Error */
	case 0x08:		/* SCSI Hung */
	case 0x09:		/* Unexpected Message Reject */
	case 0x0a:		/* SCSI Bus Reset Stuck */
	case 0x0b:		/* Auto Request-Sense Failed */
	case 0x0c:		/* Controller Ram Parity Error */
	default:
		status = DID_ERROR << 16;
		break;
	}

	SCpnt->result = status | spp->target_status;

#if defined(DEBUG_INTERRUPT)
	if (SCpnt->result || do_trace)
#else
	if ((spp->adapter_status != ASOK && ha->iocount > 1000) ||
	    (spp->adapter_status != ASOK &&
	     spp->adapter_status != ASST && ha->iocount <= 1000) ||
	    do_trace || msg_byte(spp->target_status))
#endif
		scmd_printk(KERN_INFO, SCpnt, "ihdlr, mbox %2d, err 0x%x:%x,"
		       " pid %ld, reg 0x%x, count %d.\n",
		       i, spp->adapter_status, spp->target_status,
		       SCpnt->serial_number, reg, ha->iocount);

	unmap_dma(i, ha);

	/* Set the command state to inactive */
	SCpnt->host_scribble = NULL;

	SCpnt->scsi_done(SCpnt);

	if (do_trace)
		printk("%s: ihdlr, exit, irq %d, count %d.\n", ha->board_name,
				irq, ha->iocount);

      handled:
	return IRQ_HANDLED;
      none:
	return IRQ_NONE;
}

static irqreturn_t do_interrupt_handler(int dummy, void *shap)
{
	struct Scsi_Host *shost;
	unsigned int j;
	unsigned long spin_flags;
	irqreturn_t ret;

	/* Check if the interrupt must be processed by this handler */
	if ((j = (unsigned int)((char *)shap - sha)) >= num_boards)
		return IRQ_NONE;
	shost = sh[j];

	spin_lock_irqsave(shost->host_lock, spin_flags);
	ret = ihdlr(shost);
	spin_unlock_irqrestore(shost->host_lock, spin_flags);
	return ret;
}

static int eata2x_release(struct Scsi_Host *shost)
{
	struct hostdata *ha = (struct hostdata *)shost->hostdata;
	unsigned int i;

	for (i = 0; i < shost->can_queue; i++)
		kfree((&ha->cp[i])->sglist);

	for (i = 0; i < shost->can_queue; i++)
		pci_unmap_single(ha->pdev, ha->cp[i].cp_dma_addr,
				 sizeof(struct mscp), PCI_DMA_BIDIRECTIONAL);

	if (ha->sp_cpu_addr)
		pci_free_consistent(ha->pdev, sizeof(struct mssp),
				    ha->sp_cpu_addr, ha->sp_dma_addr);

	free_irq(shost->irq, &sha[ha->board_number]);

	if (shost->dma_channel != NO_DMA)
		free_dma(shost->dma_channel);

	release_region(shost->io_port, shost->n_io_port);
	scsi_unregister(shost);
	return 0;
}

#include "scsi_module.c"

#ifndef MODULE
__setup("eata=", option_setup);
#endif				/* end MODULE */
