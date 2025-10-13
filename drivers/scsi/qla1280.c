// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
*                  QLOGIC LINUX SOFTWARE
*
* QLogic  QLA1280 (Ultra2)  and  QLA12160 (Ultra3) SCSI driver
* Copyright (C) 2000 Qlogic Corporation (www.qlogic.com)
* Copyright (C) 2001-2004 Jes Sorensen, Wild Open Source Inc.
* Copyright (C) 2003-2004 Christoph Hellwig
*
******************************************************************************/
#define QLA1280_VERSION      "3.27.1"
/*****************************************************************************
    Revision History:
    Rev  3.27.1, February 8, 2010, Michael Reed
	- Retain firmware image for error recovery.
    Rev  3.27, February 10, 2009, Michael Reed
	- General code cleanup.
	- Improve error recovery.
    Rev  3.26, January 16, 2006 Jes Sorensen
	- Ditch all < 2.6 support
    Rev  3.25.1, February 10, 2005 Christoph Hellwig
	- use pci_map_single to map non-S/G requests
	- remove qla1280_proc_info
    Rev  3.25, September 28, 2004, Christoph Hellwig
	- add support for ISP1020/1040
	- don't include "scsi.h" anymore for 2.6.x
    Rev  3.24.4 June 7, 2004 Christoph Hellwig
	- restructure firmware loading, cleanup initialization code
	- prepare support for ISP1020/1040 chips
    Rev  3.24.3 January 19, 2004, Jes Sorensen
	- Handle PCI DMA mask settings correctly
	- Correct order of error handling in probe_one, free_irq should not
	  be called if request_irq failed
    Rev  3.24.2 January 19, 2004, James Bottomley & Andrew Vasquez
	- Big endian fixes (James)
	- Remove bogus IOCB content on zero data transfer commands (Andrew)
    Rev  3.24.1 January 5, 2004, Jes Sorensen
	- Initialize completion queue to avoid OOPS on probe
	- Handle interrupts during mailbox testing
    Rev  3.24 November 17, 2003, Christoph Hellwig
    	- use struct list_head for completion queue
	- avoid old Scsi_FOO typedefs
	- cleanup 2.4 compat glue a bit
	- use <scsi/scsi_*.h> headers on 2.6 instead of "scsi.h"
	- make initialization for memory mapped vs port I/O more similar
	- remove broken pci config space manipulation
	- kill more cruft
	- this is an almost perfect 2.6 scsi driver now! ;)
    Rev  3.23.39 December 17, 2003, Jes Sorensen
	- Delete completion queue from srb if mailbox command failed to
	  to avoid qla1280_done completeting qla1280_error_action's
	  obsolete context
	- Reduce arguments for qla1280_done
    Rev  3.23.38 October 18, 2003, Christoph Hellwig
	- Convert to new-style hotplugable driver for 2.6
	- Fix missing scsi_unregister/scsi_host_put on HBA removal
	- Kill some more cruft
    Rev  3.23.37 October 1, 2003, Jes Sorensen
	- Make MMIO depend on CONFIG_X86_VISWS instead of yet another
	  random CONFIG option
	- Clean up locking in probe path
    Rev  3.23.36 October 1, 2003, Christoph Hellwig
	- queuecommand only ever receives new commands - clear flags
	- Reintegrate lost fixes from Linux 2.5
    Rev  3.23.35 August 14, 2003, Jes Sorensen
	- Build against 2.6
    Rev  3.23.34 July 23, 2003, Jes Sorensen
	- Remove pointless TRUE/FALSE macros
	- Clean up vchan handling
    Rev  3.23.33 July 3, 2003, Jes Sorensen
	- Don't define register access macros before define determining MMIO.
	  This just happened to work out on ia64 but not elsewhere.
	- Don't try and read from the card while it is in reset as
	  it won't respond and causes an MCA
    Rev  3.23.32 June 23, 2003, Jes Sorensen
	- Basic support for boot time arguments
    Rev  3.23.31 June 8, 2003, Jes Sorensen
	- Reduce boot time messages
    Rev  3.23.30 June 6, 2003, Jes Sorensen
	- Do not enable sync/wide/ppr before it has been determined
	  that the target device actually supports it
	- Enable DMA arbitration for multi channel controllers
    Rev  3.23.29 June 3, 2003, Jes Sorensen
	- Port to 2.5.69
    Rev  3.23.28 June 3, 2003, Jes Sorensen
	- Eliminate duplicate marker commands on bus resets
	- Handle outstanding commands appropriately on bus/device resets
    Rev  3.23.27 May 28, 2003, Jes Sorensen
	- Remove bogus input queue code, let the Linux SCSI layer do the work
	- Clean up NVRAM handling, only read it once from the card
	- Add a number of missing default nvram parameters
    Rev  3.23.26 Beta May 28, 2003, Jes Sorensen
	- Use completion queue for mailbox commands instead of busy wait
    Rev  3.23.25 Beta May 27, 2003, James Bottomley
	- Migrate to use new error handling code
    Rev  3.23.24 Beta May 21, 2003, James Bottomley
	- Big endian support
	- Cleanup data direction code
    Rev  3.23.23 Beta May 12, 2003, Jes Sorensen
	- Switch to using MMIO instead of PIO
    Rev  3.23.22 Beta April 15, 2003, Jes Sorensen
	- Fix PCI parity problem with 12160 during reset.
    Rev  3.23.21 Beta April 14, 2003, Jes Sorensen
	- Use pci_map_page()/pci_unmap_page() instead of map_single version.
    Rev  3.23.20 Beta April 9, 2003, Jes Sorensen
	- Remove < 2.4.x support
	- Introduce HOST_LOCK to make the spin lock changes portable.
	- Remove a bunch of idiotic and unnecessary typedef's
	- Kill all leftovers of target-mode support which never worked anyway
    Rev  3.23.19 Beta April 11, 2002, Linus Torvalds
	- Do qla1280_pci_config() before calling request_irq() and
	  request_region()
	- Use pci_dma_hi32() to handle upper word of DMA addresses instead
	  of large shifts
	- Hand correct arguments to free_irq() in case of failure
    Rev  3.23.18 Beta April 11, 2002, Jes Sorensen
	- Run source through Lindent and clean up the output
    Rev  3.23.17 Beta April 11, 2002, Jes Sorensen
	- Update SCSI firmware to qla1280 v8.15.00 and qla12160 v10.04.32
    Rev  3.23.16 Beta March 19, 2002, Jes Sorensen
	- Rely on mailbox commands generating interrupts - do not
	  run qla1280_isr() from ql1280_mailbox_command()
	- Remove device_reg_t
	- Integrate ql12160_set_target_parameters() with 1280 version
	- Make qla1280_setup() non static
	- Do not call qla1280_check_for_dead_scsi_bus() on every I/O request
	  sent to the card - this command pauses the firmware!!!
    Rev  3.23.15 Beta March 19, 2002, Jes Sorensen
	- Clean up qla1280.h - remove obsolete QL_DEBUG_LEVEL_x definitions
	- Remove a pile of pointless and confusing (srb_t **) and
	  (scsi_lu_t *) typecasts
	- Explicit mark that we do not use the new error handling (for now)
	- Remove scsi_qla_host_t and use 'struct' instead
	- Remove in_abort, watchdog_enabled, dpc, dpc_sched, bios_enabled,
	  pci_64bit_slot flags which weren't used for anything anyway
	- Grab host->host_lock while calling qla1280_isr() from abort()
	- Use spin_lock()/spin_unlock() in qla1280_intr_handler() - we
	  do not need to save/restore flags in the interrupt handler
	- Enable interrupts early (before any mailbox access) in preparation
	  for cleaning up the mailbox handling
    Rev  3.23.14 Beta March 14, 2002, Jes Sorensen
	- Further cleanups. Remove all trace of QL_DEBUG_LEVEL_x and replace
	  it with proper use of dprintk().
	- Make qla1280_print_scsi_cmd() and qla1280_dump_buffer() both take
	  a debug level argument to determine if data is to be printed
	- Add KERN_* info to printk()
    Rev  3.23.13 Beta March 14, 2002, Jes Sorensen
	- Significant cosmetic cleanups
	- Change debug code to use dprintk() and remove #if mess
    Rev  3.23.12 Beta March 13, 2002, Jes Sorensen
	- More cosmetic cleanups, fix places treating return as function
	- use cpu_relax() in qla1280_debounce_register()
    Rev  3.23.11 Beta March 13, 2002, Jes Sorensen
	- Make it compile under 2.5.5
    Rev  3.23.10 Beta October 1, 2001, Jes Sorensen
	- Do no typecast short * to long * in QL1280BoardTbl, this
	  broke miserably on big endian boxes
    Rev  3.23.9 Beta September 30, 2001, Jes Sorensen
	- Remove pre 2.2 hack for checking for reentrance in interrupt handler
	- Make data types used to receive from SCSI_{BUS,TCN,LUN}_32
	  unsigned int to match the types from struct scsi_cmnd
    Rev  3.23.8 Beta September 29, 2001, Jes Sorensen
	- Remove bogus timer_t typedef from qla1280.h
	- Remove obsolete pre 2.2 PCI setup code, use proper #define's
	  for PCI_ values, call pci_set_master()
	- Fix memleak of qla1280_buffer on module unload
	- Only compile module parsing code #ifdef MODULE - should be
	  changed to use individual MODULE_PARM's later
	- Remove dummy_buffer that was never modified nor printed
	- ENTER()/LEAVE() are noops unless QL_DEBUG_LEVEL_3, hence remove
	  #ifdef QL_DEBUG_LEVEL_3/#endif around ENTER()/LEAVE() calls
	- Remove \r from print statements, this is Linux, not DOS
	- Remove obsolete QLA1280_{SCSILU,INTR,RING}_{LOCK,UNLOCK}
	  dummy macros
	- Remove C++ compile hack in header file as Linux driver are not
	  supposed to be compiled as C++
	- Kill MS_64BITS macro as it makes the code more readable
	- Remove unnecessary flags.in_interrupts bit
    Rev  3.23.7 Beta August 20, 2001, Jes Sorensen
	- Dont' check for set flags on q->q_flag one by one in qla1280_next()
        - Check whether the interrupt was generated by the QLA1280 before
          doing any processing
	- qla1280_status_entry(): Only zero out part of sense_buffer that
	  is not being copied into
	- Remove more superflouous typecasts
	- qla1280_32bit_start_scsi() replace home-brew memcpy() with memcpy()
    Rev  3.23.6 Beta August 20, 2001, Tony Luck, Intel
        - Don't walk the entire list in qla1280_putq_t() just to directly
	  grab the pointer to the last element afterwards
    Rev  3.23.5 Beta August 9, 2001, Jes Sorensen
	- Don't use IRQF_DISABLED, it's use is deprecated for this kinda driver
    Rev  3.23.4 Beta August 8, 2001, Jes Sorensen
	- Set dev->max_sectors to 1024
    Rev  3.23.3 Beta August 6, 2001, Jes Sorensen
	- Provide compat macros for pci_enable_device(), pci_find_subsys()
	  and scsi_set_pci_device()
	- Call scsi_set_pci_device() for all devices
	- Reduce size of kernel version dependent device probe code
	- Move duplicate probe/init code to separate function
	- Handle error if qla1280_mem_alloc() fails
	- Kill OFFSET() macro and use Linux's PCI definitions instead
        - Kill private structure defining PCI config space (struct config_reg)
	- Only allocate I/O port region if not in MMIO mode
	- Remove duplicate (unused) sanity check of sife of srb_t
    Rev  3.23.2 Beta August 6, 2001, Jes Sorensen
	- Change home-brew memset() implementations to use memset()
        - Remove all references to COMTRACE() - accessing a PC's COM2 serial
          port directly is not legal under Linux.
    Rev  3.23.1 Beta April 24, 2001, Jes Sorensen
        - Remove pre 2.2 kernel support
        - clean up 64 bit DMA setting to use 2.4 API (provide backwards compat)
        - Fix MMIO access to use readl/writel instead of directly
          dereferencing pointers
        - Nuke MSDOS debugging code
        - Change true/false data types to int from uint8_t
        - Use int for counters instead of uint8_t etc.
        - Clean up size & byte order conversion macro usage
    Rev  3.23 Beta January 11, 2001 BN Qlogic
        - Added check of device_id when handling non
          QLA12160s during detect().
    Rev  3.22 Beta January 5, 2001 BN Qlogic
        - Changed queue_task() to schedule_task()
          for kernels 2.4.0 and higher.
          Note: 2.4.0-testxx kernels released prior to
                the actual 2.4.0 kernel release on January 2001
                will get compile/link errors with schedule_task().
                Please update your kernel to released 2.4.0 level,
                or comment lines in this file flagged with  3.22
                to resolve compile/link error of schedule_task().
        - Added -DCONFIG_SMP in addition to -D__SMP__
          in Makefile for 2.4.0 builds of driver as module.
    Rev  3.21 Beta January 4, 2001 BN Qlogic
        - Changed criteria of 64/32 Bit mode of HBA
          operation according to BITS_PER_LONG rather
          than HBA's NVRAM setting of >4Gig memory bit;
          so that the HBA auto-configures without the need
          to setup each system individually.
    Rev  3.20 Beta December 5, 2000 BN Qlogic
        - Added priority handling to IA-64  onboard SCSI
          ISP12160 chip for kernels greater than 2.3.18.
        - Added irqrestore for qla1280_intr_handler.
        - Enabled /proc/scsi/qla1280 interface.
        - Clear /proc/scsi/qla1280 counters in detect().
    Rev  3.19 Beta October 13, 2000 BN Qlogic
        - Declare driver_template for new kernel
          (2.4.0 and greater) scsi initialization scheme.
        - Update /proc/scsi entry for 2.3.18 kernels and
          above as qla1280
    Rev  3.18 Beta October 10, 2000 BN Qlogic
        - Changed scan order of adapters to map
          the QLA12160 followed by the QLA1280.
    Rev  3.17 Beta September 18, 2000 BN Qlogic
        - Removed warnings for 32 bit 2.4.x compiles
        - Corrected declared size for request and response
          DMA addresses that are kept in each ha
    Rev. 3.16 Beta  August 25, 2000   BN  Qlogic
        - Corrected 64 bit addressing issue on IA-64
          where the upper 32 bits were not properly
          passed to the RISC engine.
    Rev. 3.15 Beta  August 22, 2000   BN  Qlogic
        - Modified qla1280_setup_chip to properly load
          ISP firmware for greater that 4 Gig memory on IA-64
    Rev. 3.14 Beta  August 16, 2000   BN  Qlogic
        - Added setting of dma_mask to full 64 bit
          if flags.enable_64bit_addressing is set in NVRAM
    Rev. 3.13 Beta  August 16, 2000   BN  Qlogic
        - Use new PCI DMA mapping APIs for 2.4.x kernel
    Rev. 3.12       July 18, 2000    Redhat & BN Qlogic
        - Added check of pci_enable_device to detect() for 2.3.x
        - Use pci_resource_start() instead of
          pdev->resource[0].start in detect() for 2.3.x
        - Updated driver version
    Rev. 3.11       July 14, 2000    BN  Qlogic
	- Updated SCSI Firmware to following versions:
	  qla1x80:   8.13.08
	  qla1x160:  10.04.08
	- Updated driver version to 3.11
    Rev. 3.10    June 23, 2000   BN Qlogic
        - Added filtering of AMI SubSys Vendor ID devices
    Rev. 3.9
        - DEBUG_QLA1280 undefined and  new version  BN Qlogic
    Rev. 3.08b      May 9, 2000    MD Dell
        - Added logic to check against AMI subsystem vendor ID
	Rev. 3.08       May 4, 2000    DG  Qlogic
        - Added logic to check for PCI subsystem ID.
	Rev. 3.07       Apr 24, 2000    DG & BN  Qlogic
	   - Updated SCSI Firmware to following versions:
	     qla12160:   10.01.19
		 qla1280:     8.09.00
	Rev. 3.06       Apr 12, 2000    DG & BN  Qlogic
	   - Internal revision; not released
    Rev. 3.05       Mar 28, 2000    DG & BN  Qlogic
       - Edit correction for virt_to_bus and PROC.
    Rev. 3.04       Mar 28, 2000    DG & BN  Qlogic
       - Merge changes from ia64 port.
    Rev. 3.03       Mar 28, 2000    BN  Qlogic
       - Increase version to reflect new code drop with compile fix
         of issue with inclusion of linux/spinlock for 2.3 kernels
    Rev. 3.02       Mar 15, 2000    BN  Qlogic
       - Merge qla1280_proc_info from 2.10 code base
    Rev. 3.01       Feb 10, 2000    BN  Qlogic
       - Corrected code to compile on a 2.2.x kernel.
    Rev. 3.00       Jan 17, 2000    DG  Qlogic
	   - Added 64-bit support.
    Rev. 2.07       Nov 9, 1999     DG  Qlogic
	   - Added new routine to set target parameters for ISP12160.
    Rev. 2.06       Sept 10, 1999     DG  Qlogic
       - Added support for ISP12160 Ultra 3 chip.
    Rev. 2.03       August 3, 1999    Fred Lewis, Intel DuPont
	- Modified code to remove errors generated when compiling with
	  Cygnus IA64 Compiler.
        - Changed conversion of pointers to unsigned longs instead of integers.
        - Changed type of I/O port variables from uint32_t to unsigned long.
        - Modified OFFSET macro to work with 64-bit as well as 32-bit.
        - Changed sprintf and printk format specifiers for pointers to %p.
        - Changed some int to long type casts where needed in sprintf & printk.
        - Added l modifiers to sprintf and printk format specifiers for longs.
        - Removed unused local variables.
    Rev. 1.20       June 8, 1999      DG,  Qlogic
         Changes to support RedHat release 6.0 (kernel 2.2.5).
       - Added SCSI exclusive access lock (io_request_lock) when accessing
         the adapter.
       - Added changes for the new LINUX interface template. Some new error
         handling routines have been added to the template, but for now we
         will use the old ones.
    -   Initial Beta Release.
*****************************************************************************/


#include <linux/module.h>

#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/pci_ids.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include <asm/processor.h>
#include <asm/types.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>


/*
 * Compile time Options:
 *            0 - Disable and 1 - Enable
 */
#define  DEBUG_QLA1280_INTR	0
#define  DEBUG_PRINT_NVRAM	0
#define  DEBUG_QLA1280		0

#define	MEMORY_MAPPED_IO	1

#include "qla1280.h"

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
#define QLA_64BIT_PTR	1
#endif

#define NVRAM_DELAY()			udelay(500)	/* 2 microseconds */

#define IS_ISP1040(ha) (ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP1020)
#define IS_ISP1x40(ha) (ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP1020 || \
			ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP1240)
#define IS_ISP1x160(ha)        (ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP10160 || \
				ha->pdev->device == PCI_DEVICE_ID_QLOGIC_ISP12160)


static int qla1280_probe_one(struct pci_dev *, const struct pci_device_id *);
static void qla1280_remove_one(struct pci_dev *);

/*
 *  QLogic Driver Support Function Prototypes.
 */
static void qla1280_done(struct scsi_qla_host *);
static int qla1280_get_token(char *);
static int qla1280_setup(char *s) __init;

/*
 *  QLogic ISP1280 Hardware Support Function Prototypes.
 */
static int qla1280_load_firmware(struct scsi_qla_host *);
static int qla1280_init_rings(struct scsi_qla_host *);
static int qla1280_nvram_config(struct scsi_qla_host *);
static int qla1280_mailbox_command(struct scsi_qla_host *,
				   uint8_t, uint16_t *);
static int qla1280_bus_reset(struct scsi_qla_host *, int);
static int qla1280_device_reset(struct scsi_qla_host *, int, int);
static int qla1280_abort_command(struct scsi_qla_host *, struct srb *, int);
static int qla1280_abort_isp(struct scsi_qla_host *);
#ifdef QLA_64BIT_PTR
static int qla1280_64bit_start_scsi(struct scsi_qla_host *, struct srb *);
#else
static int qla1280_32bit_start_scsi(struct scsi_qla_host *, struct srb *);
#endif
static void qla1280_nv_write(struct scsi_qla_host *, uint16_t);
static void qla1280_poll(struct scsi_qla_host *);
static void qla1280_reset_adapter(struct scsi_qla_host *);
static void qla1280_marker(struct scsi_qla_host *, int, int, int, u8);
static void qla1280_isp_cmd(struct scsi_qla_host *);
static void qla1280_isr(struct scsi_qla_host *, struct list_head *);
static void qla1280_rst_aen(struct scsi_qla_host *);
static void qla1280_status_entry(struct scsi_qla_host *, struct response *,
				 struct list_head *);
static void qla1280_error_entry(struct scsi_qla_host *, struct response *,
				struct list_head *);
static uint16_t qla1280_get_nvram_word(struct scsi_qla_host *, uint32_t);
static uint16_t qla1280_nvram_request(struct scsi_qla_host *, uint32_t);
static uint16_t qla1280_debounce_register(volatile uint16_t __iomem *);
static request_t *qla1280_req_pkt(struct scsi_qla_host *);
static int qla1280_check_for_dead_scsi_bus(struct scsi_qla_host *,
					   unsigned int);
static void qla1280_get_target_parameters(struct scsi_qla_host *,
					   struct scsi_device *);
static int qla1280_set_target_parameters(struct scsi_qla_host *, int, int);


static struct qla_driver_setup driver_setup;

/*
 * convert scsi data direction to request_t control flags
 */
static inline uint16_t
qla1280_data_direction(struct scsi_cmnd *cmnd)
{
	switch(cmnd->sc_data_direction) {
	case DMA_FROM_DEVICE:
		return BIT_5;
	case DMA_TO_DEVICE:
		return BIT_6;
	case DMA_BIDIRECTIONAL:
		return BIT_5 | BIT_6;
	/*
	 * We could BUG() on default here if one of the four cases aren't
	 * met, but then again if we receive something like that from the
	 * SCSI layer we have more serious problems. This shuts up GCC.
	 */
	case DMA_NONE:
	default:
		return 0;
	}
}
		
#if DEBUG_QLA1280
static void __qla1280_print_scsi_cmd(struct scsi_cmnd * cmd);
static void __qla1280_dump_buffer(char *, int);
#endif


/*
 * insmod needs to find the variable and make it point to something
 */
#ifdef MODULE
static char *qla1280;

/* insmod qla1280 options=verbose" */
module_param(qla1280, charp, 0);
#else
__setup("qla1280=", qla1280_setup);
#endif


#define	CMD_CDBLEN(Cmnd)	Cmnd->cmd_len
#define	CMD_CDBP(Cmnd)		Cmnd->cmnd
#define	CMD_SNSP(Cmnd)		Cmnd->sense_buffer
#define	CMD_SNSLEN(Cmnd)	SCSI_SENSE_BUFFERSIZE
#define	CMD_RESULT(Cmnd)	Cmnd->result
#define	CMD_HANDLE(Cmnd)	Cmnd->host_scribble

#define CMD_HOST(Cmnd)		Cmnd->device->host
#define SCSI_BUS_32(Cmnd)	Cmnd->device->channel
#define SCSI_TCN_32(Cmnd)	Cmnd->device->id
#define SCSI_LUN_32(Cmnd)	Cmnd->device->lun


/*****************************************/
/*   ISP Boards supported by this driver */
/*****************************************/

struct qla_boards {
	char *name;		/* Board ID String */
	int numPorts;		/* Number of SCSI ports */
	int fw_index;		/* index into qla1280_fw_tbl for firmware */
};

/* NOTE: the last argument in each entry is used to index ql1280_board_tbl */
static const struct pci_device_id qla1280_pci_tbl[] = {
	{PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP12160,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP1020,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP1080,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
	{PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP1240,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3},
	{PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP1280,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4},
	{PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP10160,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 5},
	{0,}
};
MODULE_DEVICE_TABLE(pci, qla1280_pci_tbl);

static DEFINE_MUTEX(qla1280_firmware_mutex);

struct qla_fw {
	char *fwname;
	const struct firmware *fw;
};

#define QL_NUM_FW_IMAGES 3

static struct qla_fw qla1280_fw_tbl[QL_NUM_FW_IMAGES] = {
	{"qlogic/1040.bin",  NULL},	/* image 0 */
	{"qlogic/1280.bin",  NULL},	/* image 1 */
	{"qlogic/12160.bin", NULL},	/* image 2 */
};

/* NOTE: Order of boards in this table must match order in qla1280_pci_tbl */
static struct qla_boards ql1280_board_tbl[] = {
	{.name = "QLA12160", .numPorts = 2, .fw_index = 2},
	{.name = "QLA1040" , .numPorts = 1, .fw_index = 0},
	{.name = "QLA1080" , .numPorts = 1, .fw_index = 1},
	{.name = "QLA1240" , .numPorts = 2, .fw_index = 1},
	{.name = "QLA1280" , .numPorts = 2, .fw_index = 1},
	{.name = "QLA10160", .numPorts = 1, .fw_index = 2},
	{.name = "        ", .numPorts = 0, .fw_index = -1},
};

static int qla1280_verbose = 1;

#if DEBUG_QLA1280
static int ql_debug_level = 1;
#define dprintk(level, format, a...)	\
	do { if (ql_debug_level >= level) printk(KERN_ERR format, ##a); } while(0)
#define qla1280_dump_buffer(level, buf, size)	\
	if (ql_debug_level >= level) __qla1280_dump_buffer(buf, size)
#define qla1280_print_scsi_cmd(level, cmd)	\
	if (ql_debug_level >= level) __qla1280_print_scsi_cmd(cmd)
#else
#define ql_debug_level			0
#define dprintk(level, format, a...)	do{}while(0)
#define qla1280_dump_buffer(a, b, c)	do{}while(0)
#define qla1280_print_scsi_cmd(a, b)	do{}while(0)
#endif

#define ENTER(x)		dprintk(3, "qla1280 : Entering %s()\n", x);
#define LEAVE(x)		dprintk(3, "qla1280 : Leaving %s()\n", x);
#define ENTER_INTR(x)		dprintk(4, "qla1280 : Entering %s()\n", x);
#define LEAVE_INTR(x)		dprintk(4, "qla1280 : Leaving %s()\n", x);


static int qla1280_read_nvram(struct scsi_qla_host *ha)
{
	uint16_t *wptr;
	uint8_t chksum;
	int cnt, i;
	struct nvram *nv;

	ENTER("qla1280_read_nvram");

	if (driver_setup.no_nvram)
		return 1;

	printk(KERN_INFO "scsi(%ld): Reading NVRAM\n", ha->host_no);

	wptr = (uint16_t *)&ha->nvram;
	nv = &ha->nvram;
	chksum = 0;
	for (cnt = 0; cnt < 3; cnt++) {
		*wptr = qla1280_get_nvram_word(ha, cnt);
		chksum += *wptr & 0xff;
		chksum += (*wptr >> 8) & 0xff;
		wptr++;
	}

	if (nv->id0 != 'I' || nv->id1 != 'S' ||
	    nv->id2 != 'P' || nv->id3 != ' ' || nv->version < 1) {
		dprintk(2, "Invalid nvram ID or version!\n");
		chksum = 1;
	} else {
		for (; cnt < sizeof(struct nvram); cnt++) {
			*wptr = qla1280_get_nvram_word(ha, cnt);
			chksum += *wptr & 0xff;
			chksum += (*wptr >> 8) & 0xff;
			wptr++;
		}
	}

	dprintk(3, "qla1280_read_nvram: NVRAM Magic ID= %c %c %c %02x"
	       " version %i\n", nv->id0, nv->id1, nv->id2, nv->id3,
	       nv->version);


	if (chksum) {
		if (!driver_setup.no_nvram)
			printk(KERN_WARNING "scsi(%ld): Unable to identify or "
			       "validate NVRAM checksum, using default "
			       "settings\n", ha->host_no);
		ha->nvram_valid = 0;
	} else
		ha->nvram_valid = 1;

	/* The firmware interface is, um, interesting, in that the
	 * actual firmware image on the chip is little endian, thus,
	 * the process of taking that image to the CPU would end up
	 * little endian.  However, the firmware interface requires it
	 * to be read a word (two bytes) at a time.
	 *
	 * The net result of this would be that the word (and
	 * doubleword) quantities in the firmware would be correct, but
	 * the bytes would be pairwise reversed.  Since most of the
	 * firmware quantities are, in fact, bytes, we do an extra
	 * le16_to_cpu() in the firmware read routine.
	 *
	 * The upshot of all this is that the bytes in the firmware
	 * are in the correct places, but the 16 and 32 bit quantities
	 * are still in little endian format.  We fix that up below by
	 * doing extra reverses on them */
	nv->isp_parameter = cpu_to_le16(nv->isp_parameter);
	nv->firmware_feature.w = cpu_to_le16(nv->firmware_feature.w);
	for(i = 0; i < MAX_BUSES; i++) {
		nv->bus[i].selection_timeout = cpu_to_le16(nv->bus[i].selection_timeout);
		nv->bus[i].max_queue_depth = cpu_to_le16(nv->bus[i].max_queue_depth);
	}
	dprintk(1, "qla1280_read_nvram: Completed Reading NVRAM\n");
	LEAVE("qla1280_read_nvram");

	return chksum;
}

/**************************************************************************
 *   qla1280_info
 *     Return a string describing the driver.
 **************************************************************************/
static const char *
qla1280_info(struct Scsi_Host *host)
{
	static char qla1280_scsi_name_buffer[125];
	char *bp;
	struct scsi_qla_host *ha;
	struct qla_boards *bdp;

	bp = &qla1280_scsi_name_buffer[0];
	ha = (struct scsi_qla_host *)host->hostdata;
	bdp = &ql1280_board_tbl[ha->devnum];
	memset(bp, 0, sizeof(qla1280_scsi_name_buffer));

	sprintf (bp,
		 "QLogic %s PCI to SCSI Host Adapter\n"
		 "       Firmware version: %2d.%02d.%02d, Driver version %s",
		 &bdp->name[0], ha->fwver1, ha->fwver2, ha->fwver3,
		 QLA1280_VERSION);
	return bp;
}

/**************************************************************************
 *   qla1280_queuecommand
 *     Queue a command to the controller.
 *
 * Note:
 * The mid-level driver tries to ensures that queuecommand never gets invoked
 * concurrently with itself or the interrupt handler (although the
 * interrupt handler may call this routine as part of request-completion
 * handling).   Unfortunately, it sometimes calls the scheduler in interrupt
 * context which is a big NO! NO!.
 **************************************************************************/
static int qla1280_queuecommand_lck(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	struct scsi_qla_host *ha = (struct scsi_qla_host *)host->hostdata;
	struct srb *sp = scsi_cmd_priv(cmd);
	int status;

	sp->cmd = cmd;
	sp->flags = 0;
	sp->wait = NULL;
	CMD_HANDLE(cmd) = (unsigned char *)NULL;

	qla1280_print_scsi_cmd(5, cmd);

#ifdef QLA_64BIT_PTR
	/*
	 * Using 64 bit commands if the PCI bridge doesn't support it is a
	 * bit wasteful, however this should really only happen if one's
	 * PCI controller is completely broken, like the BCM1250. For
	 * sane hardware this is not an issue.
	 */
	status = qla1280_64bit_start_scsi(ha, sp);
#else
	status = qla1280_32bit_start_scsi(ha, sp);
#endif
	return status;
}

static DEF_SCSI_QCMD(qla1280_queuecommand)

enum action {
	ABORT_COMMAND,
	DEVICE_RESET,
	BUS_RESET,
};


static void qla1280_mailbox_timeout(struct timer_list *t)
{
	struct scsi_qla_host *ha = timer_container_of(ha, t, mailbox_timer);
	struct device_reg __iomem *reg;
	reg = ha->iobase;

	ha->mailbox_out[0] = RD_REG_WORD(&reg->mailbox0);
	printk(KERN_ERR "scsi(%ld): mailbox timed out, mailbox0 %04x, "
	       "ictrl %04x, istatus %04x\n", ha->host_no, ha->mailbox_out[0],
	       RD_REG_WORD(&reg->ictrl), RD_REG_WORD(&reg->istatus));
	complete(ha->mailbox_wait);
}

static int
_qla1280_wait_for_single_command(struct scsi_qla_host *ha, struct srb *sp,
				 struct completion *wait)
{
	int	status = FAILED;
	struct scsi_cmnd *cmd = sp->cmd;

	spin_unlock_irq(ha->host->host_lock);
	wait_for_completion_timeout(wait, 4*HZ);
	spin_lock_irq(ha->host->host_lock);
	sp->wait = NULL;
	if(CMD_HANDLE(cmd) == COMPLETED_HANDLE) {
		status = SUCCESS;
		scsi_done(cmd);
	}
	return status;
}

static int
qla1280_wait_for_single_command(struct scsi_qla_host *ha, struct srb *sp)
{
	DECLARE_COMPLETION_ONSTACK(wait);

	sp->wait = &wait;
	return _qla1280_wait_for_single_command(ha, sp, &wait);
}

static int
qla1280_wait_for_pending_commands(struct scsi_qla_host *ha, int bus, int target)
{
	int		cnt;
	int		status;
	struct srb	*sp;
	struct scsi_cmnd *cmd;

	status = SUCCESS;

	/*
	 * Wait for all commands with the designated bus/target
	 * to be completed by the firmware
	 */
	for (cnt = 0; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
		sp = ha->outstanding_cmds[cnt];
		if (sp) {
			cmd = sp->cmd;

			if (bus >= 0 && SCSI_BUS_32(cmd) != bus)
				continue;
			if (target >= 0 && SCSI_TCN_32(cmd) != target)
				continue;

			status = qla1280_wait_for_single_command(ha, sp);
			if (status == FAILED)
				break;
		}
	}
	return status;
}

/**************************************************************************
 * qla1280_error_action
 *    The function will attempt to perform a specified error action and
 *    wait for the results (or time out).
 *
 * Input:
 *      cmd = Linux SCSI command packet of the command that cause the
 *            bus reset.
 *      action = error action to take (see action_t)
 *
 * Returns:
 *      SUCCESS or FAILED
 *
 **************************************************************************/
static int
qla1280_error_action(struct scsi_cmnd *cmd, enum action action)
{
	struct scsi_qla_host *ha;
	int bus, target, lun;
	struct srb *sp;
	int i, found;
	int result=FAILED;
	int wait_for_bus=-1;
	int wait_for_target = -1;
	DECLARE_COMPLETION_ONSTACK(wait);

	ENTER("qla1280_error_action");

	ha = (struct scsi_qla_host *)(CMD_HOST(cmd)->hostdata);
	sp = scsi_cmd_priv(cmd);
	bus = SCSI_BUS_32(cmd);
	target = SCSI_TCN_32(cmd);
	lun = SCSI_LUN_32(cmd);

	dprintk(4, "error_action %i, istatus 0x%04x\n", action,
		RD_REG_WORD(&ha->iobase->istatus));

	dprintk(4, "host_cmd 0x%04x, ictrl 0x%04x, jiffies %li\n",
		RD_REG_WORD(&ha->iobase->host_cmd),
		RD_REG_WORD(&ha->iobase->ictrl), jiffies);

	if (qla1280_verbose)
		printk(KERN_INFO "scsi(%li): Resetting Cmnd=0x%p, "
		       "Handle=0x%p, action=0x%x\n",
		       ha->host_no, cmd, CMD_HANDLE(cmd), action);

	/*
	 * Check to see if we have the command in the outstanding_cmds[]
	 * array.  If not then it must have completed before this error
	 * action was initiated.  If the error_action isn't ABORT_COMMAND
	 * then the driver must proceed with the requested action.
	 */
	found = -1;
	for (i = 0; i < MAX_OUTSTANDING_COMMANDS; i++) {
		if (sp == ha->outstanding_cmds[i]) {
			found = i;
			sp->wait = &wait; /* we'll wait for it to complete */
			break;
		}
	}

	if (found < 0) {	/* driver doesn't have command */
		result = SUCCESS;
		if (qla1280_verbose) {
			printk(KERN_INFO
			       "scsi(%ld:%d:%d:%d): specified command has "
			       "already completed.\n", ha->host_no, bus,
				target, lun);
		}
	}

	switch (action) {

	case ABORT_COMMAND:
		dprintk(1, "qla1280: RISC aborting command\n");
		/*
		 * The abort might fail due to race when the host_lock
		 * is released to issue the abort.  As such, we
		 * don't bother to check the return status.
		 */
		if (found >= 0)
			qla1280_abort_command(ha, sp, found);
		break;

	case DEVICE_RESET:
		if (qla1280_verbose)
			printk(KERN_INFO
			       "scsi(%ld:%d:%d:%d): Queueing device reset "
			       "command.\n", ha->host_no, bus, target, lun);
		if (qla1280_device_reset(ha, bus, target) == 0) {
			/* issued device reset, set wait conditions */
			wait_for_bus = bus;
			wait_for_target = target;
		}
		break;

	case BUS_RESET:
		if (qla1280_verbose)
			printk(KERN_INFO "qla1280(%ld:%d): Issued bus "
			       "reset.\n", ha->host_no, bus);
		if (qla1280_bus_reset(ha, bus) == 0) {
			/* issued bus reset, set wait conditions */
			wait_for_bus = bus;
		}
		break;

	default:
		dprintk(1, "RESET invalid action %d\n", action);
		return FAILED;
	}

	/*
	 * At this point, the host_lock has been released and retaken
	 * by the issuance of the mailbox command.
	 * Wait for the command passed in by the mid-layer if it
	 * was found by the driver.  It might have been returned
	 * between eh recovery steps, hence the check of the "found"
	 * variable.
	 */

	if (found >= 0)
		result = _qla1280_wait_for_single_command(ha, sp, &wait);

	if (action == ABORT_COMMAND && result != SUCCESS) {
		printk(KERN_WARNING
		       "scsi(%li:%i:%i:%i): "
		       "Unable to abort command!\n",
		       ha->host_no, bus, target, lun);
	}

	/*
	 * If the command passed in by the mid-layer has been
	 * returned by the board, then wait for any additional
	 * commands which are supposed to complete based upon
	 * the error action.
	 *
	 * All commands are unconditionally returned during a
	 * call to qla1280_abort_isp(), ADAPTER_RESET.  No need
	 * to wait for them.
	 */
	if (result == SUCCESS && wait_for_bus >= 0) {
		result = qla1280_wait_for_pending_commands(ha,
					wait_for_bus, wait_for_target);
	}

	dprintk(1, "RESET returning %d\n", result);

	LEAVE("qla1280_error_action");
	return result;
}

/**************************************************************************
 *   qla1280_abort
 *     Abort the specified SCSI command(s).
 **************************************************************************/
static int
qla1280_eh_abort(struct scsi_cmnd * cmd)
{
	int rc;

	spin_lock_irq(cmd->device->host->host_lock);
	rc = qla1280_error_action(cmd, ABORT_COMMAND);
	spin_unlock_irq(cmd->device->host->host_lock);

	return rc;
}

/**************************************************************************
 *   qla1280_device_reset
 *     Reset the specified SCSI device
 **************************************************************************/
static int
qla1280_eh_device_reset(struct scsi_cmnd *cmd)
{
	int rc;

	spin_lock_irq(cmd->device->host->host_lock);
	rc = qla1280_error_action(cmd, DEVICE_RESET);
	spin_unlock_irq(cmd->device->host->host_lock);

	return rc;
}

/**************************************************************************
 *   qla1280_bus_reset
 *     Reset the specified bus.
 **************************************************************************/
static int
qla1280_eh_bus_reset(struct scsi_cmnd *cmd)
{
	int rc;

	spin_lock_irq(cmd->device->host->host_lock);
	rc = qla1280_error_action(cmd, BUS_RESET);
	spin_unlock_irq(cmd->device->host->host_lock);

	return rc;
}

/**************************************************************************
 *   qla1280_adapter_reset
 *     Reset the specified adapter (both channels)
 **************************************************************************/
static int
qla1280_eh_adapter_reset(struct scsi_cmnd *cmd)
{
	int rc = SUCCESS;
	struct Scsi_Host *shost = cmd->device->host;
	struct scsi_qla_host *ha = (struct scsi_qla_host *)shost->hostdata;

	spin_lock_irq(shost->host_lock);
	if (qla1280_verbose) {
		printk(KERN_INFO
		       "scsi(%ld): Issued ADAPTER RESET\n",
		       ha->host_no);
		printk(KERN_INFO "scsi(%ld): I/O processing will "
		       "continue automatically\n", ha->host_no);
	}
	ha->flags.reset_active = 1;

	if (qla1280_abort_isp(ha) != 0) {	/* it's dead */
		rc = FAILED;
	}

	ha->flags.reset_active = 0;

	spin_unlock_irq(shost->host_lock);

	return rc;
}

static int
qla1280_biosparam(struct scsi_device *sdev, struct gendisk *unused,
		  sector_t capacity, int geom[])
{
	int heads, sectors, cylinders;

	heads = 64;
	sectors = 32;
	cylinders = (unsigned long)capacity / (heads * sectors);
	if (cylinders > 1024) {
		heads = 255;
		sectors = 63;
		cylinders = (unsigned long)capacity / (heads * sectors);
		/* if (cylinders > 1023)
		   cylinders = 1023; */
	}

	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;

	return 0;
}

 
/* disable risc and host interrupts */
static inline void
qla1280_disable_intrs(struct scsi_qla_host *ha)
{
	WRT_REG_WORD(&ha->iobase->ictrl, 0);
	RD_REG_WORD(&ha->iobase->ictrl);	/* PCI Posted Write flush */
}

/* enable risc and host interrupts */
static inline void
qla1280_enable_intrs(struct scsi_qla_host *ha)
{
	WRT_REG_WORD(&ha->iobase->ictrl, (ISP_EN_INT | ISP_EN_RISC));
	RD_REG_WORD(&ha->iobase->ictrl);	/* PCI Posted Write flush */
}

/**************************************************************************
 * qla1280_intr_handler
 *   Handles the H/W interrupt
 **************************************************************************/
static irqreturn_t
qla1280_intr_handler(int irq, void *dev_id)
{
	struct scsi_qla_host *ha;
	struct device_reg __iomem *reg;
	u16 data;
	int handled = 0;

	ENTER_INTR ("qla1280_intr_handler");
	ha = (struct scsi_qla_host *)dev_id;

	spin_lock(ha->host->host_lock);

	ha->isr_count++;
	reg = ha->iobase;

	qla1280_disable_intrs(ha);

	data = qla1280_debounce_register(&reg->istatus);
	/* Check for pending interrupts. */
	if (data & RISC_INT) {	
		qla1280_isr(ha, &ha->done_q);
		handled = 1;
	}
	if (!list_empty(&ha->done_q))
		qla1280_done(ha);

	spin_unlock(ha->host->host_lock);

	qla1280_enable_intrs(ha);

	LEAVE_INTR("qla1280_intr_handler");
	return IRQ_RETVAL(handled);
}


static int
qla1280_set_target_parameters(struct scsi_qla_host *ha, int bus, int target)
{
	uint8_t mr;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	struct nvram *nv;
	int status, lun;

	nv = &ha->nvram;

	mr = BIT_3 | BIT_2 | BIT_1 | BIT_0;

	/* Set Target Parameters. */
	mb[0] = MBC_SET_TARGET_PARAMETERS;
	mb[1] = (uint16_t)((bus ? target | BIT_7 : target) << 8);
	mb[2] = nv->bus[bus].target[target].parameter.renegotiate_on_error << 8;
	mb[2] |= nv->bus[bus].target[target].parameter.stop_queue_on_check << 9;
	mb[2] |= nv->bus[bus].target[target].parameter.auto_request_sense << 10;
	mb[2] |= nv->bus[bus].target[target].parameter.tag_queuing << 11;
	mb[2] |= nv->bus[bus].target[target].parameter.enable_sync << 12;
	mb[2] |= nv->bus[bus].target[target].parameter.enable_wide << 13;
	mb[2] |= nv->bus[bus].target[target].parameter.parity_checking << 14;
	mb[2] |= nv->bus[bus].target[target].parameter.disconnect_allowed << 15;

	if (IS_ISP1x160(ha)) {
		mb[2] |= nv->bus[bus].target[target].ppr_1x160.flags.enable_ppr << 5;
		mb[3] =	(nv->bus[bus].target[target].flags.flags1x160.sync_offset << 8);
		mb[6] =	(nv->bus[bus].target[target].ppr_1x160.flags.ppr_options << 8) |
			 nv->bus[bus].target[target].ppr_1x160.flags.ppr_bus_width;
		mr |= BIT_6;
	} else {
		mb[3] =	(nv->bus[bus].target[target].flags.flags1x80.sync_offset << 8);
	}
	mb[3] |= nv->bus[bus].target[target].sync_period;

	status = qla1280_mailbox_command(ha, mr, mb);

	/* Set Device Queue Parameters. */
	for (lun = 0; lun < MAX_LUNS; lun++) {
		mb[0] = MBC_SET_DEVICE_QUEUE;
		mb[1] = (uint16_t)((bus ? target | BIT_7 : target) << 8);
		mb[1] |= lun;
		mb[2] = nv->bus[bus].max_queue_depth;
		mb[3] = nv->bus[bus].target[target].execution_throttle;
		status |= qla1280_mailbox_command(ha, 0x0f, mb);
	}

	if (status)
		printk(KERN_WARNING "scsi(%ld:%i:%i): "
		       "qla1280_set_target_parameters() failed\n",
		       ha->host_no, bus, target);
	return status;
}


/**************************************************************************
 *   qla1280_sdev_configure
 *
 * Description:
 *   Determines the queue depth for a given device.  There are two ways
 *   a queue depth can be obtained for a tagged queueing device.  One
 *   way is the default queue depth which is determined by whether
 *   If it is defined, then it is used
 *   as the default queue depth.  Otherwise, we use either 4 or 8 as the
 *   default queue depth (dependent on the number of hardware SCBs).
 **************************************************************************/
static int
qla1280_sdev_configure(struct scsi_device *device, struct queue_limits *lim)
{
	struct scsi_qla_host *ha;
	int default_depth = 3;
	int bus = device->channel;
	int target = device->id;
	int status = 0;
	struct nvram *nv;
	unsigned long flags;

	ha = (struct scsi_qla_host *)device->host->hostdata;
	nv = &ha->nvram;

	if (qla1280_check_for_dead_scsi_bus(ha, bus))
		return 1;

	if (device->tagged_supported &&
	    (ha->bus_settings[bus].qtag_enables & (BIT_0 << target))) {
		scsi_change_queue_depth(device, ha->bus_settings[bus].hiwat);
	} else {
		scsi_change_queue_depth(device, default_depth);
	}

	nv->bus[bus].target[target].parameter.enable_sync = device->sdtr;
	nv->bus[bus].target[target].parameter.enable_wide = device->wdtr;
	nv->bus[bus].target[target].ppr_1x160.flags.enable_ppr = device->ppr;

	if (driver_setup.no_sync ||
	    (driver_setup.sync_mask &&
	     (~driver_setup.sync_mask & (1 << target))))
		nv->bus[bus].target[target].parameter.enable_sync = 0;
	if (driver_setup.no_wide ||
	    (driver_setup.wide_mask &&
	     (~driver_setup.wide_mask & (1 << target))))
		nv->bus[bus].target[target].parameter.enable_wide = 0;
	if (IS_ISP1x160(ha)) {
		if (driver_setup.no_ppr ||
		    (driver_setup.ppr_mask &&
		     (~driver_setup.ppr_mask & (1 << target))))
			nv->bus[bus].target[target].ppr_1x160.flags.enable_ppr = 0;
	}

	spin_lock_irqsave(ha->host->host_lock, flags);
	if (nv->bus[bus].target[target].parameter.enable_sync)
		status = qla1280_set_target_parameters(ha, bus, target);
	qla1280_get_target_parameters(ha, device);
	spin_unlock_irqrestore(ha->host->host_lock, flags);
	return status;
}


/*
 * qla1280_done
 *      Process completed commands.
 *
 * Input:
 *      ha           = adapter block pointer.
 */
static void
qla1280_done(struct scsi_qla_host *ha)
{
	struct srb *sp;
	struct list_head *done_q;
	int bus, target;
	struct scsi_cmnd *cmd;

	ENTER("qla1280_done");

	done_q = &ha->done_q;

	while (!list_empty(done_q)) {
		sp = list_entry(done_q->next, struct srb, list);

		list_del(&sp->list);
	
		cmd = sp->cmd;
		bus = SCSI_BUS_32(cmd);
		target = SCSI_TCN_32(cmd);

		switch ((CMD_RESULT(cmd) >> 16)) {
		case DID_RESET:
			/* Issue marker command. */
			if (!ha->flags.abort_isp_active)
				qla1280_marker(ha, bus, target, 0, MK_SYNC_ID);
			break;
		case DID_ABORT:
			sp->flags &= ~SRB_ABORT_PENDING;
			sp->flags |= SRB_ABORTED;
			break;
		default:
			break;
		}

		/* Release memory used for this I/O */
		scsi_dma_unmap(cmd);

		/* Call the mid-level driver interrupt handler */
		ha->actthreads--;

		if (sp->wait == NULL)
			scsi_done(cmd);
		else
			complete(sp->wait);
	}
	LEAVE("qla1280_done");
}

/*
 * Translates a ISP error to a Linux SCSI error
 */
static int
qla1280_return_status(struct response * sts, struct scsi_cmnd *cp)
{
	int host_status = DID_ERROR;
	uint16_t comp_status = le16_to_cpu(sts->comp_status);
	uint16_t state_flags = le16_to_cpu(sts->state_flags);
	uint32_t residual_length = le32_to_cpu(sts->residual_length);
	uint16_t scsi_status = le16_to_cpu(sts->scsi_status);
#if DEBUG_QLA1280_INTR
	static char *reason[] = {
		"DID_OK",
		"DID_NO_CONNECT",
		"DID_BUS_BUSY",
		"DID_TIME_OUT",
		"DID_BAD_TARGET",
		"DID_ABORT",
		"DID_PARITY",
		"DID_ERROR",
		"DID_RESET",
		"DID_BAD_INTR"
	};
#endif				/* DEBUG_QLA1280_INTR */

	ENTER("qla1280_return_status");

#if DEBUG_QLA1280_INTR
	/*
	  dprintk(1, "qla1280_return_status: compl status = 0x%04x\n",
	  comp_status);
	*/
#endif

	switch (comp_status) {
	case CS_COMPLETE:
		host_status = DID_OK;
		break;

	case CS_INCOMPLETE:
		if (!(state_flags & SF_GOT_BUS))
			host_status = DID_NO_CONNECT;
		else if (!(state_flags & SF_GOT_TARGET))
			host_status = DID_BAD_TARGET;
		else if (!(state_flags & SF_SENT_CDB))
			host_status = DID_ERROR;
		else if (!(state_flags & SF_TRANSFERRED_DATA))
			host_status = DID_ERROR;
		else if (!(state_flags & SF_GOT_STATUS))
			host_status = DID_ERROR;
		else if (!(state_flags & SF_GOT_SENSE))
			host_status = DID_ERROR;
		break;

	case CS_RESET:
		host_status = DID_RESET;
		break;

	case CS_ABORTED:
		host_status = DID_ABORT;
		break;

	case CS_TIMEOUT:
		host_status = DID_TIME_OUT;
		break;

	case CS_DATA_OVERRUN:
		dprintk(2, "Data overrun 0x%x\n", residual_length);
		dprintk(2, "qla1280_return_status: response packet data\n");
		qla1280_dump_buffer(2, (char *)sts, RESPONSE_ENTRY_SIZE);
		host_status = DID_ERROR;
		break;

	case CS_DATA_UNDERRUN:
		if ((scsi_bufflen(cp) - residual_length) <
		    cp->underflow) {
			printk(KERN_WARNING
			       "scsi: Underflow detected - retrying "
			       "command.\n");
			host_status = DID_ERROR;
		} else {
			scsi_set_resid(cp, residual_length);
			host_status = DID_OK;
		}
		break;

	default:
		host_status = DID_ERROR;
		break;
	}

#if DEBUG_QLA1280_INTR
	dprintk(1, "qla1280 ISP status: host status (%s) scsi status %x\n",
		reason[host_status], scsi_status);
#endif

	LEAVE("qla1280_return_status");

	return (scsi_status & 0xff) | (host_status << 16);
}

/****************************************************************************/
/*                QLogic ISP1280 Hardware Support Functions.                */
/****************************************************************************/

/*
 * qla1280_initialize_adapter
 *      Initialize board.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = success
 */
static int
qla1280_initialize_adapter(struct scsi_qla_host *ha)
{
	struct device_reg __iomem *reg;
	int status;
	int bus;
	unsigned long flags;

	ENTER("qla1280_initialize_adapter");

	/* Clear adapter flags. */
	ha->flags.online = 0;
	ha->flags.disable_host_adapter = 0;
	ha->flags.reset_active = 0;
	ha->flags.abort_isp_active = 0;

	/* TODO: implement support for the 1040 nvram format */
	if (IS_ISP1040(ha))
		driver_setup.no_nvram = 1;

	dprintk(1, "Configure PCI space for adapter...\n");

	reg = ha->iobase;

	/* Insure mailbox registers are free. */
	WRT_REG_WORD(&reg->semaphore, 0);
	WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
	WRT_REG_WORD(&reg->host_cmd, HC_CLR_HOST_INT);
	RD_REG_WORD(&reg->host_cmd);

	if (qla1280_read_nvram(ha)) {
		dprintk(2, "qla1280_initialize_adapter: failed to read "
			"NVRAM\n");
	}

	/*
	 * It's necessary to grab the spin here as qla1280_mailbox_command
	 * needs to be able to drop the lock unconditionally to wait
	 * for completion.
	 */
	spin_lock_irqsave(ha->host->host_lock, flags);

	status = qla1280_load_firmware(ha);
	if (status) {
		printk(KERN_ERR "scsi(%li): initialize: pci probe failed!\n",
		       ha->host_no);
		goto out;
	}

	/* Setup adapter based on NVRAM parameters. */
	dprintk(1, "scsi(%ld): Configure NVRAM parameters\n", ha->host_no);
	qla1280_nvram_config(ha);

	if (ha->flags.disable_host_adapter) {
		status = 1;
		goto out;
	}

	status = qla1280_init_rings(ha);
	if (status)
		goto out;

	/* Issue SCSI reset, if we can't reset twice then bus is dead */
	for (bus = 0; bus < ha->ports; bus++) {
		if (!ha->bus_settings[bus].disable_scsi_reset &&
		    qla1280_bus_reset(ha, bus) &&
		    qla1280_bus_reset(ha, bus))
			ha->bus_settings[bus].scsi_bus_dead = 1;
	}

	ha->flags.online = 1;
 out:
	spin_unlock_irqrestore(ha->host->host_lock, flags);

	if (status)
		dprintk(2, "qla1280_initialize_adapter: **** FAILED ****\n");

	LEAVE("qla1280_initialize_adapter");
	return status;
}

/*
 * qla1280_request_firmware
 *      Acquire firmware for chip.  Retain in memory
 *      for error recovery.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      Pointer to firmware image or an error code
 *      cast to pointer via ERR_PTR().
 */
static const struct firmware *
qla1280_request_firmware(struct scsi_qla_host *ha)
{
	const struct firmware *fw;
	int err;
	int index;
	char *fwname;

	spin_unlock_irq(ha->host->host_lock);
	mutex_lock(&qla1280_firmware_mutex);

	index = ql1280_board_tbl[ha->devnum].fw_index;
	fw = qla1280_fw_tbl[index].fw;
	if (fw)
		goto out;

	fwname = qla1280_fw_tbl[index].fwname;
	err = request_firmware(&fw, fwname, &ha->pdev->dev);

	if (err) {
		printk(KERN_ERR "Failed to load image \"%s\" err %d\n",
		       fwname, err);
		fw = ERR_PTR(err);
		goto unlock;
	}
	if ((fw->size % 2) || (fw->size < 6)) {
		printk(KERN_ERR "Invalid firmware length %zu in image \"%s\"\n",
		       fw->size, fwname);
		release_firmware(fw);
		fw = ERR_PTR(-EINVAL);
		goto unlock;
	}

	qla1280_fw_tbl[index].fw = fw;

 out:
	ha->fwver1 = fw->data[0];
	ha->fwver2 = fw->data[1];
	ha->fwver3 = fw->data[2];
 unlock:
	mutex_unlock(&qla1280_firmware_mutex);
	spin_lock_irq(ha->host->host_lock);
	return fw;
}

/*
 * Chip diagnostics
 *      Test chip for proper operation.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = success.
 */
static int
qla1280_chip_diag(struct scsi_qla_host *ha)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	struct device_reg __iomem *reg = ha->iobase;
	int status = 0;
	int cnt;
	uint16_t data;
	dprintk(3, "qla1280_chip_diag: testing device at 0x%p \n", &reg->id_l);

	dprintk(1, "scsi(%ld): Verifying chip\n", ha->host_no);

	/* Soft reset chip and wait for it to finish. */
	WRT_REG_WORD(&reg->ictrl, ISP_RESET);

	/*
	 * We can't do a traditional PCI write flush here by reading
	 * back the register. The card will not respond once the reset
	 * is in action and we end up with a machine check exception
	 * instead. Nothing to do but wait and hope for the best.
	 * A portable pci_write_flush(pdev) call would be very useful here.
	 */
	udelay(20);
	data = qla1280_debounce_register(&reg->ictrl);
	/*
	 * Yet another QLogic gem ;-(
	 */
	for (cnt = 1000000; cnt && data & ISP_RESET; cnt--) {
		udelay(5);
		data = RD_REG_WORD(&reg->ictrl);
	}

	if (!cnt)
		goto fail;

	/* Reset register cleared by chip reset. */
	dprintk(3, "qla1280_chip_diag: reset register cleared by chip reset\n");

	WRT_REG_WORD(&reg->cfg_1, 0);

	/* Reset RISC and disable BIOS which
	   allows RISC to execute out of RAM. */
	WRT_REG_WORD(&reg->host_cmd, HC_RESET_RISC |
		     HC_RELEASE_RISC | HC_DISABLE_BIOS);

	RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
	data = qla1280_debounce_register(&reg->mailbox0);

	/*
	 * I *LOVE* this code!
	 */
	for (cnt = 1000000; cnt && data == MBS_BUSY; cnt--) {
		udelay(5);
		data = RD_REG_WORD(&reg->mailbox0);
	}

	if (!cnt)
		goto fail;

	/* Check product ID of chip */
	dprintk(3, "qla1280_chip_diag: Checking product ID of chip\n");

	if (RD_REG_WORD(&reg->mailbox1) != PROD_ID_1 ||
	    (RD_REG_WORD(&reg->mailbox2) != PROD_ID_2 &&
	     RD_REG_WORD(&reg->mailbox2) != PROD_ID_2a) ||
	    RD_REG_WORD(&reg->mailbox3) != PROD_ID_3 ||
	    RD_REG_WORD(&reg->mailbox4) != PROD_ID_4) {
		printk(KERN_INFO "qla1280: Wrong product ID = "
		       "0x%x,0x%x,0x%x,0x%x\n",
		       RD_REG_WORD(&reg->mailbox1),
		       RD_REG_WORD(&reg->mailbox2),
		       RD_REG_WORD(&reg->mailbox3),
		       RD_REG_WORD(&reg->mailbox4));
		goto fail;
	}

	/*
	 * Enable ints early!!!
	 */
	qla1280_enable_intrs(ha);

	dprintk(1, "qla1280_chip_diag: Checking mailboxes of chip\n");
	/* Wrap Incoming Mailboxes Test. */
	mb[0] = MBC_MAILBOX_REGISTER_TEST;
	mb[1] = 0xAAAA;
	mb[2] = 0x5555;
	mb[3] = 0xAA55;
	mb[4] = 0x55AA;
	mb[5] = 0xA5A5;
	mb[6] = 0x5A5A;
	mb[7] = 0x2525;

	status = qla1280_mailbox_command(ha, 0xff, mb);
	if (status)
		goto fail;

	if (mb[1] != 0xAAAA || mb[2] != 0x5555 || mb[3] != 0xAA55 ||
	    mb[4] != 0x55AA || mb[5] != 0xA5A5 || mb[6] != 0x5A5A ||
	    mb[7] != 0x2525) {
		printk(KERN_INFO "qla1280: Failed mbox check\n");
		goto fail;
	}

	dprintk(3, "qla1280_chip_diag: exiting normally\n");
	return 0;
 fail:
	dprintk(2, "qla1280_chip_diag: **** FAILED ****\n");
	return status;
}

static int
qla1280_load_firmware_pio(struct scsi_qla_host *ha)
{
	/* enter with host_lock acquired */

	const struct firmware *fw;
	const __le16 *fw_data;
	uint16_t risc_address, risc_code_size;
	uint16_t mb[MAILBOX_REGISTER_COUNT], i;
	int err = 0;

	fw = qla1280_request_firmware(ha);
	if (IS_ERR(fw))
		return PTR_ERR(fw);

	fw_data = (const __le16 *)&fw->data[0];
	ha->fwstart = __le16_to_cpu(fw_data[2]);

	/* Load RISC code. */
	risc_address = ha->fwstart;
	fw_data = (const __le16 *)&fw->data[6];
	risc_code_size = (fw->size - 6) / 2;

	for (i = 0; i < risc_code_size; i++) {
		mb[0] = MBC_WRITE_RAM_WORD;
		mb[1] = risc_address + i;
		mb[2] = __le16_to_cpu(fw_data[i]);

		err = qla1280_mailbox_command(ha, BIT_0 | BIT_1 | BIT_2, mb);
		if (err) {
			printk(KERN_ERR "scsi(%li): Failed to load firmware\n",
					ha->host_no);
			break;
		}
	}

	return err;
}

#ifdef QLA_64BIT_PTR
#define LOAD_CMD	MBC_LOAD_RAM_A64_ROM
#define DUMP_CMD	MBC_DUMP_RAM_A64_ROM
#define CMD_ARGS	(BIT_7 | BIT_6 | BIT_4 | BIT_3 | BIT_2 | BIT_1 | BIT_0)
#else
#define LOAD_CMD	MBC_LOAD_RAM
#define DUMP_CMD	MBC_DUMP_RAM
#define CMD_ARGS	(BIT_4 | BIT_3 | BIT_2 | BIT_1 | BIT_0)
#endif

#define DUMP_IT_BACK 0		/* for debug of RISC loading */
static int
qla1280_load_firmware_dma(struct scsi_qla_host *ha)
{
	/* enter with host_lock acquired */
	const struct firmware *fw;
	const __le16 *fw_data;
	uint16_t risc_address, risc_code_size;
	uint16_t mb[MAILBOX_REGISTER_COUNT], cnt;
	int err = 0, num, i;
#if DUMP_IT_BACK
	uint8_t *sp, *tbuf;
	dma_addr_t p_tbuf;

	tbuf = dma_alloc_coherent(&ha->pdev->dev, 8000, &p_tbuf, GFP_KERNEL);
	if (!tbuf)
		return -ENOMEM;
#endif

	fw = qla1280_request_firmware(ha);
	if (IS_ERR(fw))
		return PTR_ERR(fw);

	fw_data = (const __le16 *)&fw->data[0];
	ha->fwstart = __le16_to_cpu(fw_data[2]);

	/* Load RISC code. */
	risc_address = ha->fwstart;
	fw_data = (const __le16 *)&fw->data[6];
	risc_code_size = (fw->size - 6) / 2;

	dprintk(1, "%s: DMA RISC code (%i) words\n",
			__func__, risc_code_size);

	num = 0;
	while (risc_code_size > 0) {
		int warn __attribute__((unused)) = 0;

		cnt = 2000 >> 1;

		if (cnt > risc_code_size)
			cnt = risc_code_size;

		dprintk(2, "qla1280_setup_chip:  loading risc @ =(0x%p),"
			"%d,%d(0x%x)\n",
			fw_data, cnt, num, risc_address);
		for(i = 0; i < cnt; i++)
			((__le16 *)ha->request_ring)[i] = fw_data[i];

		mb[0] = LOAD_CMD;
		mb[1] = risc_address;
		mb[4] = cnt;
		mb[3] = ha->request_dma & 0xffff;
		mb[2] = (ha->request_dma >> 16) & 0xffff;
		mb[7] = upper_32_bits(ha->request_dma) & 0xffff;
		mb[6] = upper_32_bits(ha->request_dma) >> 16;
		dprintk(2, "%s: op=%d  0x%p = 0x%4x,0x%4x,0x%4x,0x%4x\n",
				__func__, mb[0],
				(void *)(long)ha->request_dma,
				mb[6], mb[7], mb[2], mb[3]);
		err = qla1280_mailbox_command(ha, CMD_ARGS, mb);
		if (err) {
			printk(KERN_ERR "scsi(%li): Failed to load partial "
			       "segment of f\n", ha->host_no);
			goto out;
		}

#if DUMP_IT_BACK
		mb[0] = DUMP_CMD;
		mb[1] = risc_address;
		mb[4] = cnt;
		mb[3] = p_tbuf & 0xffff;
		mb[2] = (p_tbuf >> 16) & 0xffff;
		mb[7] = upper_32_bits(p_tbuf) & 0xffff;
		mb[6] = upper_32_bits(p_tbuf) >> 16;

		err = qla1280_mailbox_command(ha, CMD_ARGS, mb);
		if (err) {
			printk(KERN_ERR
			       "Failed to dump partial segment of f/w\n");
			goto out;
		}
		sp = (uint8_t *)ha->request_ring;
		for (i = 0; i < (cnt << 1); i++) {
			if (tbuf[i] != sp[i] && warn++ < 10) {
				printk(KERN_ERR "%s: FW compare error @ "
						"byte(0x%x) loop#=%x\n",
						__func__, i, num);
				printk(KERN_ERR "%s: FWbyte=%x  "
						"FWfromChip=%x\n",
						__func__, sp[i], tbuf[i]);
				/*break; */
			}
		}
#endif
		risc_address += cnt;
		risc_code_size = risc_code_size - cnt;
		fw_data = fw_data + cnt;
		num++;
	}

 out:
#if DUMP_IT_BACK
	dma_free_coherent(&ha->pdev->dev, 8000, tbuf, p_tbuf);
#endif
	return err;
}

static int
qla1280_start_firmware(struct scsi_qla_host *ha)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	int err;

	dprintk(1, "%s: Verifying checksum of loaded RISC code.\n",
			__func__);

	/* Verify checksum of loaded RISC code. */
	mb[0] = MBC_VERIFY_CHECKSUM;
	/* mb[1] = ql12_risc_code_addr01; */
	mb[1] = ha->fwstart;
	err = qla1280_mailbox_command(ha, BIT_1 | BIT_0, mb);
	if (err) {
		printk(KERN_ERR "scsi(%li): RISC checksum failed.\n", ha->host_no);
		return err;
	}

	/* Start firmware execution. */
	dprintk(1, "%s: start firmware running.\n", __func__);
	mb[0] = MBC_EXECUTE_FIRMWARE;
	mb[1] = ha->fwstart;
	err = qla1280_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);
	if (err) {
		printk(KERN_ERR "scsi(%li): Failed to start firmware\n",
				ha->host_no);
	}

	return err;
}

static int
qla1280_load_firmware(struct scsi_qla_host *ha)
{
	/* enter with host_lock taken */
	int err;

	err = qla1280_chip_diag(ha);
	if (err)
		goto out;
	if (IS_ISP1040(ha))
		err = qla1280_load_firmware_pio(ha);
	else
		err = qla1280_load_firmware_dma(ha);
	if (err)
		goto out;
	err = qla1280_start_firmware(ha);
 out:
	return err;
}

/*
 * Initialize rings
 *
 * Input:
 *      ha                = adapter block pointer.
 *      ha->request_ring  = request ring virtual address
 *      ha->response_ring = response ring virtual address
 *      ha->request_dma   = request ring physical address
 *      ha->response_dma  = response ring physical address
 *
 * Returns:
 *      0 = success.
 */
static int
qla1280_init_rings(struct scsi_qla_host *ha)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	int status = 0;

	ENTER("qla1280_init_rings");

	/* Clear outstanding commands array. */
	memset(ha->outstanding_cmds, 0,
	       sizeof(struct srb *) * MAX_OUTSTANDING_COMMANDS);

	/* Initialize request queue. */
	ha->request_ring_ptr = ha->request_ring;
	ha->req_ring_index = 0;
	ha->req_q_cnt = REQUEST_ENTRY_CNT;
	/* mb[0] = MBC_INIT_REQUEST_QUEUE; */
	mb[0] = MBC_INIT_REQUEST_QUEUE_A64;
	mb[1] = REQUEST_ENTRY_CNT;
	mb[3] = ha->request_dma & 0xffff;
	mb[2] = (ha->request_dma >> 16) & 0xffff;
	mb[4] = 0;
	mb[7] = upper_32_bits(ha->request_dma) & 0xffff;
	mb[6] = upper_32_bits(ha->request_dma) >> 16;
	if (!(status = qla1280_mailbox_command(ha, BIT_7 | BIT_6 | BIT_4 |
					       BIT_3 | BIT_2 | BIT_1 | BIT_0,
					       &mb[0]))) {
		/* Initialize response queue. */
		ha->response_ring_ptr = ha->response_ring;
		ha->rsp_ring_index = 0;
		/* mb[0] = MBC_INIT_RESPONSE_QUEUE; */
		mb[0] = MBC_INIT_RESPONSE_QUEUE_A64;
		mb[1] = RESPONSE_ENTRY_CNT;
		mb[3] = ha->response_dma & 0xffff;
		mb[2] = (ha->response_dma >> 16) & 0xffff;
		mb[5] = 0;
		mb[7] = upper_32_bits(ha->response_dma) & 0xffff;
		mb[6] = upper_32_bits(ha->response_dma) >> 16;
		status = qla1280_mailbox_command(ha, BIT_7 | BIT_6 | BIT_5 |
						 BIT_3 | BIT_2 | BIT_1 | BIT_0,
						 &mb[0]);
	}

	if (status)
		dprintk(2, "qla1280_init_rings: **** FAILED ****\n");

	LEAVE("qla1280_init_rings");
	return status;
}

static void
qla1280_print_settings(struct nvram *nv)
{
	dprintk(1, "qla1280 : initiator scsi id bus[0]=%d\n",
		nv->bus[0].config_1.initiator_id);
	dprintk(1, "qla1280 : initiator scsi id bus[1]=%d\n",
		nv->bus[1].config_1.initiator_id);

	dprintk(1, "qla1280 : bus reset delay[0]=%d\n",
		nv->bus[0].bus_reset_delay);
	dprintk(1, "qla1280 : bus reset delay[1]=%d\n",
		nv->bus[1].bus_reset_delay);

	dprintk(1, "qla1280 : retry count[0]=%d\n", nv->bus[0].retry_count);
	dprintk(1, "qla1280 : retry delay[0]=%d\n", nv->bus[0].retry_delay);
	dprintk(1, "qla1280 : retry count[1]=%d\n", nv->bus[1].retry_count);
	dprintk(1, "qla1280 : retry delay[1]=%d\n", nv->bus[1].retry_delay);

	dprintk(1, "qla1280 : async data setup time[0]=%d\n",
		nv->bus[0].config_2.async_data_setup_time);
	dprintk(1, "qla1280 : async data setup time[1]=%d\n",
		nv->bus[1].config_2.async_data_setup_time);

	dprintk(1, "qla1280 : req/ack active negation[0]=%d\n",
		nv->bus[0].config_2.req_ack_active_negation);
	dprintk(1, "qla1280 : req/ack active negation[1]=%d\n",
		nv->bus[1].config_2.req_ack_active_negation);

	dprintk(1, "qla1280 : data line active negation[0]=%d\n",
		nv->bus[0].config_2.data_line_active_negation);
	dprintk(1, "qla1280 : data line active negation[1]=%d\n",
		nv->bus[1].config_2.data_line_active_negation);

	dprintk(1, "qla1280 : disable loading risc code=%d\n",
		nv->cntr_flags_1.disable_loading_risc_code);

	dprintk(1, "qla1280 : enable 64bit addressing=%d\n",
		nv->cntr_flags_1.enable_64bit_addressing);

	dprintk(1, "qla1280 : selection timeout limit[0]=%d\n",
		nv->bus[0].selection_timeout);
	dprintk(1, "qla1280 : selection timeout limit[1]=%d\n",
		nv->bus[1].selection_timeout);

	dprintk(1, "qla1280 : max queue depth[0]=%d\n",
		nv->bus[0].max_queue_depth);
	dprintk(1, "qla1280 : max queue depth[1]=%d\n",
		nv->bus[1].max_queue_depth);
}

static void
qla1280_set_target_defaults(struct scsi_qla_host *ha, int bus, int target)
{
	struct nvram *nv = &ha->nvram;

	nv->bus[bus].target[target].parameter.renegotiate_on_error = 1;
	nv->bus[bus].target[target].parameter.auto_request_sense = 1;
	nv->bus[bus].target[target].parameter.tag_queuing = 1;
	nv->bus[bus].target[target].parameter.enable_sync = 1;
#if 1	/* Some SCSI Processors do not seem to like this */
	nv->bus[bus].target[target].parameter.enable_wide = 1;
#endif
	nv->bus[bus].target[target].execution_throttle =
		nv->bus[bus].max_queue_depth - 1;
	nv->bus[bus].target[target].parameter.parity_checking = 1;
	nv->bus[bus].target[target].parameter.disconnect_allowed = 1;

	if (IS_ISP1x160(ha)) {
		nv->bus[bus].target[target].flags.flags1x160.device_enable = 1;
		nv->bus[bus].target[target].flags.flags1x160.sync_offset = 0x0e;
		nv->bus[bus].target[target].sync_period = 9;
		nv->bus[bus].target[target].ppr_1x160.flags.enable_ppr = 1;
		nv->bus[bus].target[target].ppr_1x160.flags.ppr_options = 2;
		nv->bus[bus].target[target].ppr_1x160.flags.ppr_bus_width = 1;
	} else {
		nv->bus[bus].target[target].flags.flags1x80.device_enable = 1;
		nv->bus[bus].target[target].flags.flags1x80.sync_offset = 12;
		nv->bus[bus].target[target].sync_period = 10;
	}
}

static void
qla1280_set_defaults(struct scsi_qla_host *ha)
{
	struct nvram *nv = &ha->nvram;
	int bus, target;

	dprintk(1, "Using defaults for NVRAM: \n");
	memset(nv, 0, sizeof(struct nvram));

	/* nv->cntr_flags_1.disable_loading_risc_code = 1; */
	nv->firmware_feature.f.enable_fast_posting = 1;
	nv->firmware_feature.f.disable_synchronous_backoff = 1;
	nv->termination.scsi_bus_0_control = 3;
	nv->termination.scsi_bus_1_control = 3;
	nv->termination.auto_term_support = 1;

	/*
	 * Set default FIFO magic - What appropriate values would be here
	 * is unknown. This is what I have found testing with 12160s.
	 *
	 * Now, I would love the magic decoder ring for this one, the
	 * header file provided by QLogic seems to be bogus or incomplete
	 * at best.
	 */
	nv->isp_config.burst_enable = 1;
	if (IS_ISP1040(ha))
		nv->isp_config.fifo_threshold |= 3;
	else
		nv->isp_config.fifo_threshold |= 4;

	if (IS_ISP1x160(ha))
		nv->isp_parameter = 0x01; /* fast memory enable */

	for (bus = 0; bus < MAX_BUSES; bus++) {
		nv->bus[bus].config_1.initiator_id = 7;
		nv->bus[bus].config_2.req_ack_active_negation = 1;
		nv->bus[bus].config_2.data_line_active_negation = 1;
		nv->bus[bus].selection_timeout = 250;
		nv->bus[bus].max_queue_depth = 32;

		if (IS_ISP1040(ha)) {
			nv->bus[bus].bus_reset_delay = 3;
			nv->bus[bus].config_2.async_data_setup_time = 6;
			nv->bus[bus].retry_delay = 1;
		} else {
			nv->bus[bus].bus_reset_delay = 5;
			nv->bus[bus].config_2.async_data_setup_time = 8;
		}

		for (target = 0; target < MAX_TARGETS; target++)
			qla1280_set_target_defaults(ha, bus, target);
	}
}

static int
qla1280_config_target(struct scsi_qla_host *ha, int bus, int target)
{
	struct nvram *nv = &ha->nvram;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	int status, lun;
	uint16_t flag;

	/* Set Target Parameters. */
	mb[0] = MBC_SET_TARGET_PARAMETERS;
	mb[1] = (uint16_t)((bus ? target | BIT_7 : target) << 8);

	/*
	 * Do not enable sync and ppr for the initial INQUIRY run. We
	 * enable this later if we determine the target actually
	 * supports it.
	 */
	mb[2] = (TP_RENEGOTIATE | TP_AUTO_REQUEST_SENSE | TP_TAGGED_QUEUE
		 | TP_WIDE | TP_PARITY | TP_DISCONNECT);

	if (IS_ISP1x160(ha))
		mb[3] =	nv->bus[bus].target[target].flags.flags1x160.sync_offset << 8;
	else
		mb[3] =	nv->bus[bus].target[target].flags.flags1x80.sync_offset << 8;
	mb[3] |= nv->bus[bus].target[target].sync_period;
	status = qla1280_mailbox_command(ha, 0x0f, mb);

	/* Save Tag queuing enable flag. */
	flag = (BIT_0 << target);
	if (nv->bus[bus].target[target].parameter.tag_queuing)
		ha->bus_settings[bus].qtag_enables |= flag;

	/* Save Device enable flag. */
	if (IS_ISP1x160(ha)) {
		if (nv->bus[bus].target[target].flags.flags1x160.device_enable)
			ha->bus_settings[bus].device_enables |= flag;
		ha->bus_settings[bus].lun_disables |= 0;
	} else {
		if (nv->bus[bus].target[target].flags.flags1x80.device_enable)
			ha->bus_settings[bus].device_enables |= flag;
		/* Save LUN disable flag. */
		if (nv->bus[bus].target[target].flags.flags1x80.lun_disable)
			ha->bus_settings[bus].lun_disables |= flag;
	}

	/* Set Device Queue Parameters. */
	for (lun = 0; lun < MAX_LUNS; lun++) {
		mb[0] = MBC_SET_DEVICE_QUEUE;
		mb[1] = (uint16_t)((bus ? target | BIT_7 : target) << 8);
		mb[1] |= lun;
		mb[2] = nv->bus[bus].max_queue_depth;
		mb[3] = nv->bus[bus].target[target].execution_throttle;
		status |= qla1280_mailbox_command(ha, 0x0f, mb);
	}

	return status;
}

static int
qla1280_config_bus(struct scsi_qla_host *ha, int bus)
{
	struct nvram *nv = &ha->nvram;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	int target, status;

	/* SCSI Reset Disable. */
	ha->bus_settings[bus].disable_scsi_reset =
		nv->bus[bus].config_1.scsi_reset_disable;

	/* Initiator ID. */
	ha->bus_settings[bus].id = nv->bus[bus].config_1.initiator_id;
	mb[0] = MBC_SET_INITIATOR_ID;
	mb[1] = bus ? ha->bus_settings[bus].id | BIT_7 :
		ha->bus_settings[bus].id;
	status = qla1280_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);

	/* Reset Delay. */
	ha->bus_settings[bus].bus_reset_delay =
		nv->bus[bus].bus_reset_delay;

	/* Command queue depth per device. */
	ha->bus_settings[bus].hiwat = nv->bus[bus].max_queue_depth - 1;

	/* Set target parameters. */
	for (target = 0; target < MAX_TARGETS; target++)
		status |= qla1280_config_target(ha, bus, target);

	return status;
}

static int
qla1280_nvram_config(struct scsi_qla_host *ha)
{
	struct device_reg __iomem *reg = ha->iobase;
	struct nvram *nv = &ha->nvram;
	int bus, target, status = 0;
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	ENTER("qla1280_nvram_config");

	if (ha->nvram_valid) {
		/* Always force AUTO sense for LINUX SCSI */
		for (bus = 0; bus < MAX_BUSES; bus++)
			for (target = 0; target < MAX_TARGETS; target++) {
				nv->bus[bus].target[target].parameter.
					auto_request_sense = 1;
			}
	} else {
		qla1280_set_defaults(ha);
	}

	qla1280_print_settings(nv);

	/* Disable RISC load of firmware. */
	ha->flags.disable_risc_code_load =
		nv->cntr_flags_1.disable_loading_risc_code;

	if (IS_ISP1040(ha)) {
		uint16_t hwrev, cfg1, cdma_conf;

		hwrev = RD_REG_WORD(&reg->cfg_0) & ISP_CFG0_HWMSK;

		cfg1 = RD_REG_WORD(&reg->cfg_1) & ~(BIT_4 | BIT_5 | BIT_6);
		cdma_conf = RD_REG_WORD(&reg->cdma_cfg);

		/* Busted fifo, says mjacob. */
		if (hwrev != ISP_CFG0_1040A)
			cfg1 |= nv->isp_config.fifo_threshold << 4;

		cfg1 |= nv->isp_config.burst_enable << 2;
		WRT_REG_WORD(&reg->cfg_1, cfg1);

		WRT_REG_WORD(&reg->cdma_cfg, cdma_conf | CDMA_CONF_BENAB);
		WRT_REG_WORD(&reg->ddma_cfg, cdma_conf | DDMA_CONF_BENAB);
	} else {
		uint16_t cfg1, term;

		/* Set ISP hardware DMA burst */
		cfg1 = nv->isp_config.fifo_threshold << 4;
		cfg1 |= nv->isp_config.burst_enable << 2;
		/* Enable DMA arbitration on dual channel controllers */
		if (ha->ports > 1)
			cfg1 |= BIT_13;
		WRT_REG_WORD(&reg->cfg_1, cfg1);

		/* Set SCSI termination. */
		WRT_REG_WORD(&reg->gpio_enable,
			     BIT_7 | BIT_3 | BIT_2 | BIT_1 | BIT_0);
		term = nv->termination.scsi_bus_1_control;
		term |= nv->termination.scsi_bus_0_control << 2;
		term |= nv->termination.auto_term_support << 7;
		RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
		WRT_REG_WORD(&reg->gpio_data, term);
	}
	RD_REG_WORD(&reg->id_l);	/* Flush PCI write */

	/* ISP parameter word. */
	mb[0] = MBC_SET_SYSTEM_PARAMETER;
	mb[1] = nv->isp_parameter;
	status |= qla1280_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);

	if (IS_ISP1x40(ha)) {
		/* clock rate - for qla1240 and older, only */
		mb[0] = MBC_SET_CLOCK_RATE;
		mb[1] = 40;
	 	status |= qla1280_mailbox_command(ha, BIT_1 | BIT_0, mb);
	}

	/* Firmware feature word. */
	mb[0] = MBC_SET_FIRMWARE_FEATURES;
	mb[1] = nv->firmware_feature.f.enable_fast_posting;
	mb[1] |= nv->firmware_feature.f.report_lvd_bus_transition << 1;
	mb[1] |= nv->firmware_feature.f.disable_synchronous_backoff << 5;
	status |= qla1280_mailbox_command(ha, BIT_1 | BIT_0, mb);

	/* Retry count and delay. */
	mb[0] = MBC_SET_RETRY_COUNT;
	mb[1] = nv->bus[0].retry_count;
	mb[2] = nv->bus[0].retry_delay;
	mb[6] = nv->bus[1].retry_count;
	mb[7] = nv->bus[1].retry_delay;
	status |= qla1280_mailbox_command(ha, BIT_7 | BIT_6 | BIT_2 |
					  BIT_1 | BIT_0, &mb[0]);

	/* ASYNC data setup time. */
	mb[0] = MBC_SET_ASYNC_DATA_SETUP;
	mb[1] = nv->bus[0].config_2.async_data_setup_time;
	mb[2] = nv->bus[1].config_2.async_data_setup_time;
	status |= qla1280_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);

	/* Active negation states. */
	mb[0] = MBC_SET_ACTIVE_NEGATION;
	mb[1] = 0;
	if (nv->bus[0].config_2.req_ack_active_negation)
		mb[1] |= BIT_5;
	if (nv->bus[0].config_2.data_line_active_negation)
		mb[1] |= BIT_4;
	mb[2] = 0;
	if (nv->bus[1].config_2.req_ack_active_negation)
		mb[2] |= BIT_5;
	if (nv->bus[1].config_2.data_line_active_negation)
		mb[2] |= BIT_4;
	status |= qla1280_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, mb);

	mb[0] = MBC_SET_DATA_OVERRUN_RECOVERY;
	mb[1] = 2;	/* Reset SCSI bus and return all outstanding IO */
	status |= qla1280_mailbox_command(ha, BIT_1 | BIT_0, mb);

	/* thingy */
	mb[0] = MBC_SET_PCI_CONTROL;
	mb[1] = BIT_1;	/* Data DMA Channel Burst Enable */
	mb[2] = BIT_1;	/* Command DMA Channel Burst Enable */
	status |= qla1280_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, mb);

	mb[0] = MBC_SET_TAG_AGE_LIMIT;
	mb[1] = 8;
	status |= qla1280_mailbox_command(ha, BIT_1 | BIT_0, mb);

	/* Selection timeout. */
	mb[0] = MBC_SET_SELECTION_TIMEOUT;
	mb[1] = nv->bus[0].selection_timeout;
	mb[2] = nv->bus[1].selection_timeout;
	status |= qla1280_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, mb);

	for (bus = 0; bus < ha->ports; bus++)
		status |= qla1280_config_bus(ha, bus);

	if (status)
		dprintk(2, "qla1280_nvram_config: **** FAILED ****\n");

	LEAVE("qla1280_nvram_config");
	return status;
}

/*
 * Get NVRAM data word
 *      Calculates word position in NVRAM and calls request routine to
 *      get the word from NVRAM.
 *
 * Input:
 *      ha      = adapter block pointer.
 *      address = NVRAM word address.
 *
 * Returns:
 *      data word.
 */
static uint16_t
qla1280_get_nvram_word(struct scsi_qla_host *ha, uint32_t address)
{
	uint32_t nv_cmd;
	uint16_t data;

	nv_cmd = address << 16;
	nv_cmd |= NV_READ_OP;

	data = le16_to_cpu(qla1280_nvram_request(ha, nv_cmd));

	dprintk(8, "qla1280_get_nvram_word: exiting normally NVRAM data = "
		"0x%x", data);

	return data;
}

/*
 * NVRAM request
 *      Sends read command to NVRAM and gets data from NVRAM.
 *
 * Input:
 *      ha     = adapter block pointer.
 *      nv_cmd = Bit 26     = start bit
 *               Bit 25, 24 = opcode
 *               Bit 23-16  = address
 *               Bit 15-0   = write data
 *
 * Returns:
 *      data word.
 */
static uint16_t
qla1280_nvram_request(struct scsi_qla_host *ha, uint32_t nv_cmd)
{
	struct device_reg __iomem *reg = ha->iobase;
	int cnt;
	uint16_t data = 0;
	uint16_t reg_data;

	/* Send command to NVRAM. */

	nv_cmd <<= 5;
	for (cnt = 0; cnt < 11; cnt++) {
		if (nv_cmd & BIT_31)
			qla1280_nv_write(ha, NV_DATA_OUT);
		else
			qla1280_nv_write(ha, 0);
		nv_cmd <<= 1;
	}

	/* Read data from NVRAM. */

	for (cnt = 0; cnt < 16; cnt++) {
		WRT_REG_WORD(&reg->nvram, (NV_SELECT | NV_CLOCK));
		RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
		NVRAM_DELAY();
		data <<= 1;
		reg_data = RD_REG_WORD(&reg->nvram);
		if (reg_data & NV_DATA_IN)
			data |= BIT_0;
		WRT_REG_WORD(&reg->nvram, NV_SELECT);
		RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
		NVRAM_DELAY();
	}

	/* Deselect chip. */

	WRT_REG_WORD(&reg->nvram, NV_DESELECT);
	RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
	NVRAM_DELAY();

	return data;
}

static void
qla1280_nv_write(struct scsi_qla_host *ha, uint16_t data)
{
	struct device_reg __iomem *reg = ha->iobase;

	WRT_REG_WORD(&reg->nvram, data | NV_SELECT);
	RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
	NVRAM_DELAY();
	WRT_REG_WORD(&reg->nvram, data | NV_SELECT | NV_CLOCK);
	RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
	NVRAM_DELAY();
	WRT_REG_WORD(&reg->nvram, data | NV_SELECT);
	RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
	NVRAM_DELAY();
}

/*
 * Mailbox Command
 *      Issue mailbox command and waits for completion.
 *
 * Input:
 *      ha = adapter block pointer.
 *      mr = mailbox registers to load.
 *      mb = data pointer for mailbox registers.
 *
 * Output:
 *      mb[MAILBOX_REGISTER_COUNT] = returned mailbox data.
 *
 * Returns:
 *      0 = success
 */
static int
qla1280_mailbox_command(struct scsi_qla_host *ha, uint8_t mr, uint16_t *mb)
{
	struct device_reg __iomem *reg = ha->iobase;
	int status = 0;
	int cnt;
	uint16_t *optr, *iptr;
	uint16_t __iomem *mptr;
	DECLARE_COMPLETION_ONSTACK(wait);

	ENTER("qla1280_mailbox_command");

	if (ha->mailbox_wait) {
		printk(KERN_ERR "Warning mailbox wait already in use!\n");
	}
	ha->mailbox_wait = &wait;

	/*
	 * We really should start out by verifying that the mailbox is
	 * available before starting sending the command data
	 */
	/* Load mailbox registers. */
	mptr = (uint16_t __iomem *) &reg->mailbox0;
	iptr = mb;
	for (cnt = 0; cnt < MAILBOX_REGISTER_COUNT; cnt++) {
		if (mr & BIT_0) {
			WRT_REG_WORD(mptr, (*iptr));
		}

		mr >>= 1;
		mptr++;
		iptr++;
	}

	/* Issue set host interrupt command. */

	/* set up a timer just in case we're really jammed */
	timer_setup(&ha->mailbox_timer, qla1280_mailbox_timeout, 0);
	mod_timer(&ha->mailbox_timer, jiffies + 20 * HZ);

	spin_unlock_irq(ha->host->host_lock);
	WRT_REG_WORD(&reg->host_cmd, HC_SET_HOST_INT);
	qla1280_debounce_register(&reg->istatus);

	wait_for_completion(&wait);
	timer_delete_sync(&ha->mailbox_timer);

	spin_lock_irq(ha->host->host_lock);

	ha->mailbox_wait = NULL;

	/* Check for mailbox command timeout. */
	if (ha->mailbox_out[0] != MBS_CMD_CMP) {
		printk(KERN_WARNING "qla1280_mailbox_command: Command failed, "
		       "mailbox0 = 0x%04x, mailbox_out0 = 0x%04x, istatus = "
		       "0x%04x\n", 
		       mb[0], ha->mailbox_out[0], RD_REG_WORD(&reg->istatus));
		printk(KERN_WARNING "m0 %04x, m1 %04x, m2 %04x, m3 %04x\n",
		       RD_REG_WORD(&reg->mailbox0), RD_REG_WORD(&reg->mailbox1),
		       RD_REG_WORD(&reg->mailbox2), RD_REG_WORD(&reg->mailbox3));
		printk(KERN_WARNING "m4 %04x, m5 %04x, m6 %04x, m7 %04x\n",
		       RD_REG_WORD(&reg->mailbox4), RD_REG_WORD(&reg->mailbox5),
		       RD_REG_WORD(&reg->mailbox6), RD_REG_WORD(&reg->mailbox7));
		status = 1;
	}

	/* Load return mailbox registers. */
	optr = mb;
	iptr = (uint16_t *) &ha->mailbox_out[0];
	memcpy(optr, iptr, MAILBOX_REGISTER_COUNT * sizeof(uint16_t));

	if (ha->flags.reset_marker)
		qla1280_rst_aen(ha);

	if (status)
		dprintk(2, "qla1280_mailbox_command: **** FAILED, mailbox0 = "
			"0x%x ****\n", mb[0]);

	LEAVE("qla1280_mailbox_command");
	return status;
}

/*
 * qla1280_poll
 *      Polls ISP for interrupts.
 *
 * Input:
 *      ha = adapter block pointer.
 */
static void
qla1280_poll(struct scsi_qla_host *ha)
{
	struct device_reg __iomem *reg = ha->iobase;
	uint16_t data;
	LIST_HEAD(done_q);

	/* ENTER("qla1280_poll"); */

	/* Check for pending interrupts. */
	data = RD_REG_WORD(&reg->istatus);
	if (data & RISC_INT)
		qla1280_isr(ha, &done_q);

	if (!ha->mailbox_wait) {
		if (ha->flags.reset_marker)
			qla1280_rst_aen(ha);
	}

	if (!list_empty(&done_q))
		qla1280_done(ha);

	/* LEAVE("qla1280_poll"); */
}

/*
 * qla1280_bus_reset
 *      Issue SCSI bus reset.
 *
 * Input:
 *      ha  = adapter block pointer.
 *      bus = SCSI bus number.
 *
 * Returns:
 *      0 = success
 */
static int
qla1280_bus_reset(struct scsi_qla_host *ha, int bus)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	uint16_t reset_delay;
	int status;

	dprintk(3, "qla1280_bus_reset: entered\n");

	if (qla1280_verbose)
		printk(KERN_INFO "scsi(%li:%i): Resetting SCSI BUS\n",
		       ha->host_no, bus);

	reset_delay = ha->bus_settings[bus].bus_reset_delay;
	mb[0] = MBC_BUS_RESET;
	mb[1] = reset_delay;
	mb[2] = (uint16_t) bus;
	status = qla1280_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);

	if (status) {
		if (ha->bus_settings[bus].failed_reset_count > 2)
			ha->bus_settings[bus].scsi_bus_dead = 1;
		ha->bus_settings[bus].failed_reset_count++;
	} else {
		spin_unlock_irq(ha->host->host_lock);
		ssleep(reset_delay);
		spin_lock_irq(ha->host->host_lock);

		ha->bus_settings[bus].scsi_bus_dead = 0;
		ha->bus_settings[bus].failed_reset_count = 0;
		ha->bus_settings[bus].reset_marker = 0;
		/* Issue marker command. */
		qla1280_marker(ha, bus, 0, 0, MK_SYNC_ALL);
	}

	/*
	 * We should probably call qla1280_set_target_parameters()
	 * here as well for all devices on the bus.
	 */

	if (status)
		dprintk(2, "qla1280_bus_reset: **** FAILED ****\n");
	else
		dprintk(3, "qla1280_bus_reset: exiting normally\n");

	return status;
}

/*
 * qla1280_device_reset
 *      Issue bus device reset message to the target.
 *
 * Input:
 *      ha      = adapter block pointer.
 *      bus     = SCSI BUS number.
 *      target  = SCSI ID.
 *
 * Returns:
 *      0 = success
 */
static int
qla1280_device_reset(struct scsi_qla_host *ha, int bus, int target)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	int status;

	ENTER("qla1280_device_reset");

	mb[0] = MBC_ABORT_TARGET;
	mb[1] = (bus ? (target | BIT_7) : target) << 8;
	mb[2] = 1;
	status = qla1280_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);

	/* Issue marker command. */
	qla1280_marker(ha, bus, target, 0, MK_SYNC_ID);

	if (status)
		dprintk(2, "qla1280_device_reset: **** FAILED ****\n");

	LEAVE("qla1280_device_reset");
	return status;
}

/*
 * qla1280_abort_command
 *      Abort command aborts a specified IOCB.
 *
 * Input:
 *      ha = adapter block pointer.
 *      sp = SB structure pointer.
 *
 * Returns:
 *      0 = success
 */
static int
qla1280_abort_command(struct scsi_qla_host *ha, struct srb * sp, int handle)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	unsigned int bus, target, lun;
	int status;

	ENTER("qla1280_abort_command");

	bus = SCSI_BUS_32(sp->cmd);
	target = SCSI_TCN_32(sp->cmd);
	lun = SCSI_LUN_32(sp->cmd);

	sp->flags |= SRB_ABORT_PENDING;

	mb[0] = MBC_ABORT_COMMAND;
	mb[1] = (bus ? target | BIT_7 : target) << 8 | lun;
	mb[2] = handle >> 16;
	mb[3] = handle & 0xffff;
	status = qla1280_mailbox_command(ha, 0x0f, &mb[0]);

	if (status) {
		dprintk(2, "qla1280_abort_command: **** FAILED ****\n");
		sp->flags &= ~SRB_ABORT_PENDING;
	}


	LEAVE("qla1280_abort_command");
	return status;
}

/*
 * qla1280_reset_adapter
 *      Reset adapter.
 *
 * Input:
 *      ha = adapter block pointer.
 */
static void
qla1280_reset_adapter(struct scsi_qla_host *ha)
{
	struct device_reg __iomem *reg = ha->iobase;

	ENTER("qla1280_reset_adapter");

	/* Disable ISP chip */
	ha->flags.online = 0;
	WRT_REG_WORD(&reg->ictrl, ISP_RESET);
	WRT_REG_WORD(&reg->host_cmd,
		     HC_RESET_RISC | HC_RELEASE_RISC | HC_DISABLE_BIOS);
	RD_REG_WORD(&reg->id_l);	/* Flush PCI write */

	LEAVE("qla1280_reset_adapter");
}

/*
 *  Issue marker command.
 *      Function issues marker IOCB.
 *
 * Input:
 *      ha   = adapter block pointer.
 *      bus  = SCSI BUS number
 *      id   = SCSI ID
 *      lun  = SCSI LUN
 *      type = marker modifier
 */
static void
qla1280_marker(struct scsi_qla_host *ha, int bus, int id, int lun, u8 type)
{
	struct mrk_entry *pkt;

	ENTER("qla1280_marker");

	/* Get request packet. */
	if ((pkt = (struct mrk_entry *) qla1280_req_pkt(ha))) {
		pkt->entry_type = MARKER_TYPE;
		pkt->lun = (uint8_t) lun;
		pkt->target = (uint8_t) (bus ? (id | BIT_7) : id);
		pkt->modifier = type;
		pkt->entry_status = 0;

		/* Issue command to ISP */
		qla1280_isp_cmd(ha);
	}

	LEAVE("qla1280_marker");
}


/*
 * qla1280_64bit_start_scsi
 *      The start SCSI is responsible for building request packets on
 *      request ring and modifying ISP input pointer.
 *
 * Input:
 *      ha = adapter block pointer.
 *      sp = SB structure pointer.
 *
 * Returns:
 *      0 = success, was able to issue command.
 */
#ifdef QLA_64BIT_PTR
static int
qla1280_64bit_start_scsi(struct scsi_qla_host *ha, struct srb * sp)
{
	struct device_reg __iomem *reg = ha->iobase;
	struct scsi_cmnd *cmd = sp->cmd;
	cmd_a64_entry_t *pkt;
	__le32 *dword_ptr;
	dma_addr_t dma_handle;
	int status = 0;
	int cnt;
	int req_cnt;
	int seg_cnt;
	u8 dir;

	ENTER("qla1280_64bit_start_scsi:");

	/* Calculate number of entries and segments required. */
	req_cnt = 1;
	seg_cnt = scsi_dma_map(cmd);
	if (seg_cnt > 0) {
		if (seg_cnt > 2) {
			req_cnt += (seg_cnt - 2) / 5;
			if ((seg_cnt - 2) % 5)
				req_cnt++;
		}
	} else if (seg_cnt < 0) {
		status = 1;
		goto out;
	}

	if ((req_cnt + 2) >= ha->req_q_cnt) {
		/* Calculate number of free request entries. */
		cnt = RD_REG_WORD(&reg->mailbox4);
		if (ha->req_ring_index < cnt)
			ha->req_q_cnt = cnt - ha->req_ring_index;
		else
			ha->req_q_cnt =
				REQUEST_ENTRY_CNT - (ha->req_ring_index - cnt);
	}

	dprintk(3, "Number of free entries=(%d) seg_cnt=0x%x\n",
		ha->req_q_cnt, seg_cnt);

	/* If room for request in request ring. */
	if ((req_cnt + 2) >= ha->req_q_cnt) {
		status = SCSI_MLQUEUE_HOST_BUSY;
		dprintk(2, "qla1280_start_scsi: in-ptr=0x%x  req_q_cnt="
			"0x%xreq_cnt=0x%x", ha->req_ring_index, ha->req_q_cnt,
			req_cnt);
		goto out;
	}

	/* Check for room in outstanding command list. */
	for (cnt = 0; cnt < MAX_OUTSTANDING_COMMANDS &&
		     ha->outstanding_cmds[cnt] != NULL; cnt++);

	if (cnt >= MAX_OUTSTANDING_COMMANDS) {
		status = SCSI_MLQUEUE_HOST_BUSY;
		dprintk(2, "qla1280_start_scsi: NO ROOM IN "
			"OUTSTANDING ARRAY, req_q_cnt=0x%x", ha->req_q_cnt);
		goto out;
	}

	ha->outstanding_cmds[cnt] = sp;
	ha->req_q_cnt -= req_cnt;
	CMD_HANDLE(sp->cmd) = (unsigned char *)(unsigned long)(cnt + 1);

	dprintk(2, "start: cmd=%p sp=%p CDB=%xm, handle %lx\n", cmd, sp,
		cmd->cmnd[0], (long)CMD_HANDLE(sp->cmd));
	dprintk(2, "             bus %i, target %i, lun %i\n",
		SCSI_BUS_32(cmd), SCSI_TCN_32(cmd), SCSI_LUN_32(cmd));
	qla1280_dump_buffer(2, cmd->cmnd, MAX_COMMAND_SIZE);

	/*
	 * Build command packet.
	 */
	pkt = (cmd_a64_entry_t *) ha->request_ring_ptr;

	pkt->entry_type = COMMAND_A64_TYPE;
	pkt->entry_count = (uint8_t) req_cnt;
	pkt->sys_define = (uint8_t) ha->req_ring_index;
	pkt->entry_status = 0;
	pkt->handle = cpu_to_le32(cnt);

	/* Zero out remaining portion of packet. */
	memset(((char *)pkt + 8), 0, (REQUEST_ENTRY_SIZE - 8));

	/* Set ISP command timeout. */
	pkt->timeout = cpu_to_le16(scsi_cmd_to_rq(cmd)->timeout / HZ);

	/* Set device target ID and LUN */
	pkt->lun = SCSI_LUN_32(cmd);
	pkt->target = SCSI_BUS_32(cmd) ?
		(SCSI_TCN_32(cmd) | BIT_7) : SCSI_TCN_32(cmd);

	/* Enable simple tag queuing if device supports it. */
	if (cmd->device->simple_tags)
		pkt->control_flags |= cpu_to_le16(BIT_3);

	/* Load SCSI command packet. */
	pkt->cdb_len = cpu_to_le16(CMD_CDBLEN(cmd));
	memcpy(pkt->scsi_cdb, CMD_CDBP(cmd), CMD_CDBLEN(cmd));
	/* dprintk(1, "Build packet for command[0]=0x%x\n",pkt->scsi_cdb[0]); */

	/* Set transfer direction. */
	dir = qla1280_data_direction(cmd);
	pkt->control_flags |= cpu_to_le16(dir);

	/* Set total data segment count. */
	pkt->dseg_count = cpu_to_le16(seg_cnt);

	/*
	 * Load data segments.
	 */
	if (seg_cnt) {	/* If data transfer. */
		struct scatterlist *sg, *s;
		int remseg = seg_cnt;

		sg = scsi_sglist(cmd);

		/* Setup packet address segment pointer. */
		dword_ptr = (u32 *)&pkt->dseg_0_address;

		/* Load command entry data segments. */
		for_each_sg(sg, s, seg_cnt, cnt) {
			if (cnt == 2)
				break;

			dma_handle = sg_dma_address(s);
			*dword_ptr++ =
				cpu_to_le32(lower_32_bits(dma_handle));
			*dword_ptr++ =
				cpu_to_le32(upper_32_bits(dma_handle));
			*dword_ptr++ = cpu_to_le32(sg_dma_len(s));
			dprintk(3, "S/G Segment phys_addr=%x %x, len=0x%x\n",
				cpu_to_le32(upper_32_bits(dma_handle)),
				cpu_to_le32(lower_32_bits(dma_handle)),
				cpu_to_le32(sg_dma_len(s)));
			remseg--;
		}
		dprintk(5, "qla1280_64bit_start_scsi: Scatter/gather "
			"command packet data - b %i, t %i, l %i \n",
			SCSI_BUS_32(cmd), SCSI_TCN_32(cmd),
			SCSI_LUN_32(cmd));
		qla1280_dump_buffer(5, (char *)pkt,
				    REQUEST_ENTRY_SIZE);

		/*
		 * Build continuation packets.
		 */
		dprintk(3, "S/G Building Continuation...seg_cnt=0x%x "
			"remains\n", seg_cnt);

		while (remseg > 0) {
			/* Update sg start */
			sg = s;
			/* Adjust ring index. */
			ha->req_ring_index++;
			if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
				ha->req_ring_index = 0;
				ha->request_ring_ptr =
					ha->request_ring;
			} else
				ha->request_ring_ptr++;

			pkt = (cmd_a64_entry_t *)ha->request_ring_ptr;

			/* Zero out packet. */
			memset(pkt, 0, REQUEST_ENTRY_SIZE);

			/* Load packet defaults. */
			((struct cont_a64_entry *) pkt)->entry_type =
				CONTINUE_A64_TYPE;
			((struct cont_a64_entry *) pkt)->entry_count = 1;
			((struct cont_a64_entry *) pkt)->sys_define =
				(uint8_t)ha->req_ring_index;
			/* Setup packet address segment pointer. */
			dword_ptr =
				(u32 *)&((struct cont_a64_entry *) pkt)->dseg_0_address;

			/* Load continuation entry data segments. */
			for_each_sg(sg, s, remseg, cnt) {
				if (cnt == 5)
					break;
				dma_handle = sg_dma_address(s);
				*dword_ptr++ =
					cpu_to_le32(lower_32_bits(dma_handle));
				*dword_ptr++ =
					cpu_to_le32(upper_32_bits(dma_handle));
				*dword_ptr++ =
					cpu_to_le32(sg_dma_len(s));
				dprintk(3, "S/G Segment Cont. phys_addr=%x %x, len=0x%x\n",
					cpu_to_le32(upper_32_bits(dma_handle)),
					cpu_to_le32(lower_32_bits(dma_handle)),
					cpu_to_le32(sg_dma_len(s)));
			}
			remseg -= cnt;
			dprintk(5, "qla1280_64bit_start_scsi: "
				"continuation packet data - b %i, t "
				"%i, l %i \n", SCSI_BUS_32(cmd),
				SCSI_TCN_32(cmd), SCSI_LUN_32(cmd));
			qla1280_dump_buffer(5, (char *)pkt,
					    REQUEST_ENTRY_SIZE);
		}
	} else {	/* No data transfer */
		dprintk(5, "qla1280_64bit_start_scsi: No data, command "
			"packet data - b %i, t %i, l %i \n",
			SCSI_BUS_32(cmd), SCSI_TCN_32(cmd), SCSI_LUN_32(cmd));
		qla1280_dump_buffer(5, (char *)pkt, REQUEST_ENTRY_SIZE);
	}
	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	/* Set chip new ring index. */
	dprintk(2,
		"qla1280_64bit_start_scsi: Wakeup RISC for pending command\n");
	sp->flags |= SRB_SENT;
	ha->actthreads++;
	WRT_REG_WORD(&reg->mailbox4, ha->req_ring_index);

 out:
	if (status)
		dprintk(2, "qla1280_64bit_start_scsi: **** FAILED ****\n");
	else
		dprintk(3, "qla1280_64bit_start_scsi: exiting normally\n");

	return status;
}
#else /* !QLA_64BIT_PTR */

/*
 * qla1280_32bit_start_scsi
 *      The start SCSI is responsible for building request packets on
 *      request ring and modifying ISP input pointer.
 *
 *      The Qlogic firmware interface allows every queue slot to have a SCSI
 *      command and up to 4 scatter/gather (SG) entries.  If we need more
 *      than 4 SG entries, then continuation entries are used that can
 *      hold another 7 entries each.  The start routine determines if there
 *      is eought empty slots then build the combination of requests to
 *      fulfill the OS request.
 *
 * Input:
 *      ha = adapter block pointer.
 *      sp = SCSI Request Block structure pointer.
 *
 * Returns:
 *      0 = success, was able to issue command.
 */
static int
qla1280_32bit_start_scsi(struct scsi_qla_host *ha, struct srb * sp)
{
	struct device_reg __iomem *reg = ha->iobase;
	struct scsi_cmnd *cmd = sp->cmd;
	struct cmd_entry *pkt;
	__le32 *dword_ptr;
	int status = 0;
	int cnt;
	int req_cnt;
	int seg_cnt;
	u8 dir;

	ENTER("qla1280_32bit_start_scsi");

	dprintk(1, "32bit_start: cmd=%p sp=%p CDB=%x\n", cmd, sp,
		cmd->cmnd[0]);

	/* Calculate number of entries and segments required. */
	req_cnt = 1;
	seg_cnt = scsi_dma_map(cmd);
	if (seg_cnt) {
		/*
		 * if greater than four sg entries then we need to allocate
		 * continuation entries
		 */
		if (seg_cnt > 4) {
			req_cnt += (seg_cnt - 4) / 7;
			if ((seg_cnt - 4) % 7)
				req_cnt++;
		}
		dprintk(3, "S/G Transfer cmd=%p seg_cnt=0x%x, req_cnt=%x\n",
			cmd, seg_cnt, req_cnt);
	} else if (seg_cnt < 0) {
		status = 1;
		goto out;
	}

	if ((req_cnt + 2) >= ha->req_q_cnt) {
		/* Calculate number of free request entries. */
		cnt = RD_REG_WORD(&reg->mailbox4);
		if (ha->req_ring_index < cnt)
			ha->req_q_cnt = cnt - ha->req_ring_index;
		else
			ha->req_q_cnt =
				REQUEST_ENTRY_CNT - (ha->req_ring_index - cnt);
	}

	dprintk(3, "Number of free entries=(%d) seg_cnt=0x%x\n",
		ha->req_q_cnt, seg_cnt);
	/* If room for request in request ring. */
	if ((req_cnt + 2) >= ha->req_q_cnt) {
		status = SCSI_MLQUEUE_HOST_BUSY;
		dprintk(2, "qla1280_32bit_start_scsi: in-ptr=0x%x, "
			"req_q_cnt=0x%x, req_cnt=0x%x", ha->req_ring_index,
			ha->req_q_cnt, req_cnt);
		goto out;
	}

	/* Check for empty slot in outstanding command list. */
	for (cnt = 0; cnt < MAX_OUTSTANDING_COMMANDS &&
	     ha->outstanding_cmds[cnt]; cnt++);

	if (cnt >= MAX_OUTSTANDING_COMMANDS) {
		status = SCSI_MLQUEUE_HOST_BUSY;
		dprintk(2, "qla1280_32bit_start_scsi: NO ROOM IN OUTSTANDING "
			"ARRAY, req_q_cnt=0x%x\n", ha->req_q_cnt);
		goto out;
	}

	CMD_HANDLE(sp->cmd) = (unsigned char *) (unsigned long)(cnt + 1);
	ha->outstanding_cmds[cnt] = sp;
	ha->req_q_cnt -= req_cnt;

	/*
	 * Build command packet.
	 */
	pkt = (struct cmd_entry *) ha->request_ring_ptr;

	pkt->entry_type = COMMAND_TYPE;
	pkt->entry_count = (uint8_t) req_cnt;
	pkt->sys_define = (uint8_t) ha->req_ring_index;
	pkt->entry_status = 0;
	pkt->handle = cpu_to_le32(cnt);

	/* Zero out remaining portion of packet. */
	memset(((char *)pkt + 8), 0, (REQUEST_ENTRY_SIZE - 8));

	/* Set ISP command timeout. */
	pkt->timeout = cpu_to_le16(scsi_cmd_to_rq(cmd)->timeout / HZ);

	/* Set device target ID and LUN */
	pkt->lun = SCSI_LUN_32(cmd);
	pkt->target = SCSI_BUS_32(cmd) ?
		(SCSI_TCN_32(cmd) | BIT_7) : SCSI_TCN_32(cmd);

	/* Enable simple tag queuing if device supports it. */
	if (cmd->device->simple_tags)
		pkt->control_flags |= cpu_to_le16(BIT_3);

	/* Load SCSI command packet. */
	pkt->cdb_len = cpu_to_le16(CMD_CDBLEN(cmd));
	memcpy(pkt->scsi_cdb, CMD_CDBP(cmd), CMD_CDBLEN(cmd));

	/*dprintk(1, "Build packet for command[0]=0x%x\n",pkt->scsi_cdb[0]); */
	/* Set transfer direction. */
	dir = qla1280_data_direction(cmd);
	pkt->control_flags |= cpu_to_le16(dir);

	/* Set total data segment count. */
	pkt->dseg_count = cpu_to_le16(seg_cnt);

	/*
	 * Load data segments.
	 */
	if (seg_cnt) {
		struct scatterlist *sg, *s;
		int remseg = seg_cnt;

		sg = scsi_sglist(cmd);

		/* Setup packet address segment pointer. */
		dword_ptr = &pkt->dseg_0_address;

		dprintk(3, "Building S/G data segments..\n");
		qla1280_dump_buffer(1, (char *)sg, 4 * 16);

		/* Load command entry data segments. */
		for_each_sg(sg, s, seg_cnt, cnt) {
			if (cnt == 4)
				break;
			*dword_ptr++ =
				cpu_to_le32(lower_32_bits(sg_dma_address(s)));
			*dword_ptr++ = cpu_to_le32(sg_dma_len(s));
			dprintk(3, "S/G Segment phys_addr=0x%lx, len=0x%x\n",
				(lower_32_bits(sg_dma_address(s))),
				(sg_dma_len(s)));
			remseg--;
		}
		/*
		 * Build continuation packets.
		 */
		dprintk(3, "S/G Building Continuation"
			"...seg_cnt=0x%x remains\n", seg_cnt);
		while (remseg > 0) {
			/* Continue from end point */
			sg = s;
			/* Adjust ring index. */
			ha->req_ring_index++;
			if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
				ha->req_ring_index = 0;
				ha->request_ring_ptr =
					ha->request_ring;
			} else
				ha->request_ring_ptr++;

			pkt = (struct cmd_entry *)ha->request_ring_ptr;

			/* Zero out packet. */
			memset(pkt, 0, REQUEST_ENTRY_SIZE);

			/* Load packet defaults. */
			((struct cont_entry *) pkt)->
				entry_type = CONTINUE_TYPE;
			((struct cont_entry *) pkt)->entry_count = 1;

			((struct cont_entry *) pkt)->sys_define =
				(uint8_t) ha->req_ring_index;

			/* Setup packet address segment pointer. */
			dword_ptr =
				&((struct cont_entry *) pkt)->dseg_0_address;

			/* Load continuation entry data segments. */
			for_each_sg(sg, s, remseg, cnt) {
				if (cnt == 7)
					break;
				*dword_ptr++ =
					cpu_to_le32(lower_32_bits(sg_dma_address(s)));
				*dword_ptr++ =
					cpu_to_le32(sg_dma_len(s));
				dprintk(1,
					"S/G Segment Cont. phys_addr=0x%x, "
					"len=0x%x\n",
					cpu_to_le32(lower_32_bits(sg_dma_address(s))),
					cpu_to_le32(sg_dma_len(s)));
			}
			remseg -= cnt;
			dprintk(5, "qla1280_32bit_start_scsi: "
				"continuation packet data - "
				"scsi(%i:%i:%i)\n", SCSI_BUS_32(cmd),
				SCSI_TCN_32(cmd), SCSI_LUN_32(cmd));
			qla1280_dump_buffer(5, (char *)pkt,
					    REQUEST_ENTRY_SIZE);
		}
	} else {	/* No data transfer at all */
		dprintk(5, "qla1280_32bit_start_scsi: No data, command "
			"packet data - \n");
		qla1280_dump_buffer(5, (char *)pkt, REQUEST_ENTRY_SIZE);
	}
	dprintk(5, "qla1280_32bit_start_scsi: First IOCB block:\n");
	qla1280_dump_buffer(5, (char *)ha->request_ring_ptr,
			    REQUEST_ENTRY_SIZE);

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	/* Set chip new ring index. */
	dprintk(2, "qla1280_32bit_start_scsi: Wakeup RISC "
		"for pending command\n");
	sp->flags |= SRB_SENT;
	ha->actthreads++;
	WRT_REG_WORD(&reg->mailbox4, ha->req_ring_index);

out:
	if (status)
		dprintk(2, "qla1280_32bit_start_scsi: **** FAILED ****\n");

	LEAVE("qla1280_32bit_start_scsi");

	return status;
}
#endif

/*
 * qla1280_req_pkt
 *      Function is responsible for locking ring and
 *      getting a zeroed out request packet.
 *
 * Input:
 *      ha  = adapter block pointer.
 *
 * Returns:
 *      0 = failed to get slot.
 */
static request_t *
qla1280_req_pkt(struct scsi_qla_host *ha)
{
	struct device_reg __iomem *reg = ha->iobase;
	request_t *pkt = NULL;
	int cnt;
	uint32_t timer;

	ENTER("qla1280_req_pkt");

	/*
	 * This can be called from interrupt context, damn it!!!
	 */
	/* Wait for 30 seconds for slot. */
	for (timer = 15000000; timer; timer--) {
		if (ha->req_q_cnt > 0) {
			/* Calculate number of free request entries. */
			cnt = RD_REG_WORD(&reg->mailbox4);
			if (ha->req_ring_index < cnt)
				ha->req_q_cnt = cnt - ha->req_ring_index;
			else
				ha->req_q_cnt =
					REQUEST_ENTRY_CNT - (ha->req_ring_index - cnt);
		}

		/* Found empty request ring slot? */
		if (ha->req_q_cnt > 0) {
			ha->req_q_cnt--;
			pkt = ha->request_ring_ptr;

			/* Zero out packet. */
			memset(pkt, 0, REQUEST_ENTRY_SIZE);

			/*
			 * How can this be right when we have a ring
			 * size of 512???
			 */
			/* Set system defined field. */
			pkt->sys_define = (uint8_t) ha->req_ring_index;

			/* Set entry count. */
			pkt->entry_count = 1;

			break;
		}

		udelay(2);	/* 10 */

		/* Check for pending interrupts. */
		qla1280_poll(ha);
	}

	if (!pkt)
		dprintk(2, "qla1280_req_pkt: **** FAILED ****\n");
	else
		dprintk(3, "qla1280_req_pkt: exiting normally\n");

	return pkt;
}

/*
 * qla1280_isp_cmd
 *      Function is responsible for modifying ISP input pointer.
 *      Releases ring lock.
 *
 * Input:
 *      ha  = adapter block pointer.
 */
static void
qla1280_isp_cmd(struct scsi_qla_host *ha)
{
	struct device_reg __iomem *reg = ha->iobase;

	ENTER("qla1280_isp_cmd");

	dprintk(5, "qla1280_isp_cmd: IOCB data:\n");
	qla1280_dump_buffer(5, (char *)ha->request_ring_ptr,
			    REQUEST_ENTRY_SIZE);

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	/*
	 * Update request index to mailbox4 (Request Queue In).
	 */
	WRT_REG_WORD(&reg->mailbox4, ha->req_ring_index);

	LEAVE("qla1280_isp_cmd");
}

/****************************************************************************/
/*                        Interrupt Service Routine.                        */
/****************************************************************************/

/****************************************************************************
 *  qla1280_isr
 *      Calls I/O done on command completion.
 *
 * Input:
 *      ha           = adapter block pointer.
 *      done_q       = done queue.
 ****************************************************************************/
static void
qla1280_isr(struct scsi_qla_host *ha, struct list_head *done_q)
{
	struct device_reg __iomem *reg = ha->iobase;
	struct response *pkt;
	struct srb *sp = NULL;
	uint16_t mailbox[MAILBOX_REGISTER_COUNT];
	uint16_t *wptr;
	uint32_t index;
	u16 istatus;

	ENTER("qla1280_isr");

	istatus = RD_REG_WORD(&reg->istatus);
	if (!(istatus & (RISC_INT | PCI_INT)))
		return;

	/* Save mailbox register 5 */
	mailbox[5] = RD_REG_WORD(&reg->mailbox5);

	/* Check for mailbox interrupt. */

	mailbox[0] = RD_REG_WORD_dmasync(&reg->semaphore);

	if (mailbox[0] & BIT_0) {
		/* Get mailbox data. */
		/* dprintk(1, "qla1280_isr: In Get mailbox data \n"); */

		wptr = &mailbox[0];
		*wptr++ = RD_REG_WORD(&reg->mailbox0);
		*wptr++ = RD_REG_WORD(&reg->mailbox1);
		*wptr = RD_REG_WORD(&reg->mailbox2);
		if (mailbox[0] != MBA_SCSI_COMPLETION) {
			wptr++;
			*wptr++ = RD_REG_WORD(&reg->mailbox3);
			*wptr++ = RD_REG_WORD(&reg->mailbox4);
			wptr++;
			*wptr++ = RD_REG_WORD(&reg->mailbox6);
			*wptr = RD_REG_WORD(&reg->mailbox7);
		}

		/* Release mailbox registers. */

		WRT_REG_WORD(&reg->semaphore, 0);
		WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);

		dprintk(5, "qla1280_isr: mailbox interrupt mailbox[0] = 0x%x",
			mailbox[0]);

		/* Handle asynchronous event */
		switch (mailbox[0]) {
		case MBA_SCSI_COMPLETION:	/* Response completion */
			dprintk(5, "qla1280_isr: mailbox SCSI response "
				"completion\n");

			if (ha->flags.online) {
				/* Get outstanding command index. */
				index = mailbox[2] << 16 | mailbox[1];

				/* Validate handle. */
				if (index < MAX_OUTSTANDING_COMMANDS)
					sp = ha->outstanding_cmds[index];
				else
					sp = NULL;

				if (sp) {
					/* Free outstanding command slot. */
					ha->outstanding_cmds[index] = NULL;

					/* Save ISP completion status */
					CMD_RESULT(sp->cmd) = 0;
					CMD_HANDLE(sp->cmd) = COMPLETED_HANDLE;

					/* Place block on done queue */
					list_add_tail(&sp->list, done_q);
				} else {
					/*
					 * If we get here we have a real problem!
					 */
					printk(KERN_WARNING
					       "qla1280: ISP invalid handle\n");
				}
			}
			break;

		case MBA_BUS_RESET:	/* SCSI Bus Reset */
			ha->flags.reset_marker = 1;
			index = mailbox[6] & BIT_0;
			ha->bus_settings[index].reset_marker = 1;

			printk(KERN_DEBUG "qla1280_isr(): index %i "
			       "asynchronous BUS_RESET\n", index);
			break;

		case MBA_SYSTEM_ERR:	/* System Error */
			printk(KERN_WARNING
			       "qla1280: ISP System Error - mbx1=%xh, mbx2="
			       "%xh, mbx3=%xh\n", mailbox[1], mailbox[2],
			       mailbox[3]);
			break;

		case MBA_REQ_TRANSFER_ERR:	/* Request Transfer Error */
			printk(KERN_WARNING
			       "qla1280: ISP Request Transfer Error\n");
			break;

		case MBA_RSP_TRANSFER_ERR:	/* Response Transfer Error */
			printk(KERN_WARNING
			       "qla1280: ISP Response Transfer Error\n");
			break;

		case MBA_WAKEUP_THRES:	/* Request Queue Wake-up */
			dprintk(2, "qla1280_isr: asynchronous WAKEUP_THRES\n");
			break;

		case MBA_TIMEOUT_RESET:	/* Execution Timeout Reset */
			dprintk(2,
				"qla1280_isr: asynchronous TIMEOUT_RESET\n");
			break;

		case MBA_DEVICE_RESET:	/* Bus Device Reset */
			printk(KERN_INFO "qla1280_isr(): asynchronous "
			       "BUS_DEVICE_RESET\n");

			ha->flags.reset_marker = 1;
			index = mailbox[6] & BIT_0;
			ha->bus_settings[index].reset_marker = 1;
			break;

		case MBA_BUS_MODE_CHANGE:
			dprintk(2,
				"qla1280_isr: asynchronous BUS_MODE_CHANGE\n");
			break;

		default:
			/* dprintk(1, "qla1280_isr: default case of switch MB \n"); */
			if (mailbox[0] < MBA_ASYNC_EVENT) {
				wptr = &mailbox[0];
				memcpy((uint16_t *) ha->mailbox_out, wptr,
				       MAILBOX_REGISTER_COUNT *
				       sizeof(uint16_t));

				if(ha->mailbox_wait != NULL)
					complete(ha->mailbox_wait);
			}
			break;
		}
	} else {
		WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
	}

	/*
	 * We will receive interrupts during mailbox testing prior to
	 * the card being marked online, hence the double check.
	 */
	if (!(ha->flags.online && !ha->mailbox_wait)) {
		dprintk(2, "qla1280_isr: Response pointer Error\n");
		goto out;
	}

	if (mailbox[5] >= RESPONSE_ENTRY_CNT)
		goto out;

	while (ha->rsp_ring_index != mailbox[5]) {
		pkt = ha->response_ring_ptr;

		dprintk(5, "qla1280_isr: ha->rsp_ring_index = 0x%x, mailbox[5]"
			" = 0x%x\n", ha->rsp_ring_index, mailbox[5]);
		dprintk(5,"qla1280_isr: response packet data\n");
		qla1280_dump_buffer(5, (char *)pkt, RESPONSE_ENTRY_SIZE);

		if (pkt->entry_type == STATUS_TYPE) {
			if ((le16_to_cpu(pkt->scsi_status) & 0xff)
			    || pkt->comp_status || pkt->entry_status) {
				dprintk(2, "qla1280_isr: ha->rsp_ring_index = "
					"0x%x mailbox[5] = 0x%x, comp_status "
					"= 0x%x, scsi_status = 0x%x\n",
					ha->rsp_ring_index, mailbox[5],
					le16_to_cpu(pkt->comp_status),
					le16_to_cpu(pkt->scsi_status));
			}
		} else {
			dprintk(2, "qla1280_isr: ha->rsp_ring_index = "
				"0x%x, mailbox[5] = 0x%x\n",
				ha->rsp_ring_index, mailbox[5]);
			dprintk(2, "qla1280_isr: response packet data\n");
			qla1280_dump_buffer(2, (char *)pkt,
					    RESPONSE_ENTRY_SIZE);
		}

		if (pkt->entry_type == STATUS_TYPE || pkt->entry_status) {
			dprintk(2, "status: Cmd %p, handle %i\n",
				ha->outstanding_cmds[pkt->handle]->cmd,
				pkt->handle);
			if (pkt->entry_type == STATUS_TYPE)
				qla1280_status_entry(ha, pkt, done_q);
			else
				qla1280_error_entry(ha, pkt, done_q);
			/* Adjust ring index. */
			ha->rsp_ring_index++;
			if (ha->rsp_ring_index == RESPONSE_ENTRY_CNT) {
				ha->rsp_ring_index = 0;
				ha->response_ring_ptr =	ha->response_ring;
			} else
				ha->response_ring_ptr++;
			WRT_REG_WORD(&reg->mailbox5, ha->rsp_ring_index);
		}
	}
	
 out:
	LEAVE("qla1280_isr");
}

/*
 *  qla1280_rst_aen
 *      Processes asynchronous reset.
 *
 * Input:
 *      ha  = adapter block pointer.
 */
static void
qla1280_rst_aen(struct scsi_qla_host *ha)
{
	uint8_t bus;

	ENTER("qla1280_rst_aen");

	if (ha->flags.online && !ha->flags.reset_active &&
	    !ha->flags.abort_isp_active) {
		ha->flags.reset_active = 1;
		while (ha->flags.reset_marker) {
			/* Issue marker command. */
			ha->flags.reset_marker = 0;
			for (bus = 0; bus < ha->ports &&
				     !ha->flags.reset_marker; bus++) {
				if (ha->bus_settings[bus].reset_marker) {
					ha->bus_settings[bus].reset_marker = 0;
					qla1280_marker(ha, bus, 0, 0,
						       MK_SYNC_ALL);
				}
			}
		}
	}

	LEAVE("qla1280_rst_aen");
}


/*
 *  qla1280_status_entry
 *      Processes received ISP status entry.
 *
 * Input:
 *      ha           = adapter block pointer.
 *      pkt          = entry pointer.
 *      done_q       = done queue.
 */
static void
qla1280_status_entry(struct scsi_qla_host *ha, struct response *pkt,
		     struct list_head *done_q)
{
	int sense_sz;
	struct srb *sp;
	struct scsi_cmnd *cmd;
	uint32_t handle = le32_to_cpu(pkt->handle);
	uint16_t scsi_status = le16_to_cpu(pkt->scsi_status);
	uint16_t comp_status = le16_to_cpu(pkt->comp_status);

	ENTER("qla1280_status_entry");

	/* Validate handle. */
	if (handle < MAX_OUTSTANDING_COMMANDS)
		sp = ha->outstanding_cmds[handle];
	else
		sp = NULL;

	if (!sp) {
		printk(KERN_WARNING "qla1280: Status Entry invalid handle\n");
		goto out;
	}

	/* Free outstanding command slot. */
	ha->outstanding_cmds[handle] = NULL;

	cmd = sp->cmd;

	if (comp_status || scsi_status) {
		dprintk(3, "scsi: comp_status = 0x%x, scsi_status = "
			"0x%x, handle = 0x%x\n", comp_status,
			scsi_status, handle);
	}

	/* Target busy or queue full */
	if ((scsi_status & 0xFF) == SAM_STAT_TASK_SET_FULL ||
	    (scsi_status & 0xFF) == SAM_STAT_BUSY) {
		CMD_RESULT(cmd) = scsi_status & 0xff;
	} else {

		/* Save ISP completion status */
		CMD_RESULT(cmd) = qla1280_return_status(pkt, cmd);

		if (scsi_status & SAM_STAT_CHECK_CONDITION) {
			if (comp_status != CS_ARS_FAILED) {
				uint16_t req_sense_length =
					le16_to_cpu(pkt->req_sense_length);
				if (req_sense_length < CMD_SNSLEN(cmd))
					sense_sz = req_sense_length;
				else
					/*
					 * scsi_cmnd->sense_buffer is
					 * 64 bytes, why only copy 63?
					 * This looks wrong! /Jes
					 */
					sense_sz = CMD_SNSLEN(cmd) - 1;

				memcpy(cmd->sense_buffer,
				       &pkt->req_sense_data, sense_sz);
			} else
				sense_sz = 0;
			memset(cmd->sense_buffer + sense_sz, 0,
			       SCSI_SENSE_BUFFERSIZE - sense_sz);

			dprintk(2, "qla1280_status_entry: Check "
				"condition Sense data, b %i, t %i, "
				"l %i\n", SCSI_BUS_32(cmd), SCSI_TCN_32(cmd),
				SCSI_LUN_32(cmd));
			if (sense_sz)
				qla1280_dump_buffer(2,
						    (char *)cmd->sense_buffer,
						    sense_sz);
		}
	}

	CMD_HANDLE(sp->cmd) = COMPLETED_HANDLE;

	/* Place command on done queue. */
	list_add_tail(&sp->list, done_q);
 out:
	LEAVE("qla1280_status_entry");
}

/*
 *  qla1280_error_entry
 *      Processes error entry.
 *
 * Input:
 *      ha           = adapter block pointer.
 *      pkt          = entry pointer.
 *      done_q       = done queue.
 */
static void
qla1280_error_entry(struct scsi_qla_host *ha, struct response *pkt,
		    struct list_head *done_q)
{
	struct srb *sp;
	uint32_t handle = le32_to_cpu(pkt->handle);

	ENTER("qla1280_error_entry");

	if (pkt->entry_status & BIT_3)
		dprintk(2, "qla1280_error_entry: BAD PAYLOAD flag error\n");
	else if (pkt->entry_status & BIT_2)
		dprintk(2, "qla1280_error_entry: BAD HEADER flag error\n");
	else if (pkt->entry_status & BIT_1)
		dprintk(2, "qla1280_error_entry: FULL flag error\n");
	else
		dprintk(2, "qla1280_error_entry: UNKNOWN flag error\n");

	/* Validate handle. */
	if (handle < MAX_OUTSTANDING_COMMANDS)
		sp = ha->outstanding_cmds[handle];
	else
		sp = NULL;

	if (sp) {
		/* Free outstanding command slot. */
		ha->outstanding_cmds[handle] = NULL;

		/* Bad payload or header */
		if (pkt->entry_status & (BIT_3 + BIT_2)) {
			/* Bad payload or header, set error status. */
			/* CMD_RESULT(sp->cmd) = CS_BAD_PAYLOAD; */
			CMD_RESULT(sp->cmd) = DID_ERROR << 16;
		} else if (pkt->entry_status & BIT_1) {	/* FULL flag */
			CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
		} else {
			/* Set error status. */
			CMD_RESULT(sp->cmd) = DID_ERROR << 16;
		}

		CMD_HANDLE(sp->cmd) = COMPLETED_HANDLE;

		/* Place command on done queue. */
		list_add_tail(&sp->list, done_q);
	}
#ifdef QLA_64BIT_PTR
	else if (pkt->entry_type == COMMAND_A64_TYPE) {
		printk(KERN_WARNING "!qla1280: Error Entry invalid handle");
	}
#endif

	LEAVE("qla1280_error_entry");
}

/*
 *  qla1280_abort_isp
 *      Resets ISP and aborts all outstanding commands.
 *
 * Input:
 *      ha           = adapter block pointer.
 *
 * Returns:
 *      0 = success
 */
static int
qla1280_abort_isp(struct scsi_qla_host *ha)
{
	struct device_reg __iomem *reg = ha->iobase;
	struct srb *sp;
	int status = 0;
	int cnt;
	int bus;

	ENTER("qla1280_abort_isp");

	if (ha->flags.abort_isp_active || !ha->flags.online)
		goto out;
	
	ha->flags.abort_isp_active = 1;

	/* Disable ISP interrupts. */
	qla1280_disable_intrs(ha);
	WRT_REG_WORD(&reg->host_cmd, HC_PAUSE_RISC);
	RD_REG_WORD(&reg->id_l);

	printk(KERN_INFO "scsi(%li): dequeuing outstanding commands\n",
	       ha->host_no);
	/* Dequeue all commands in outstanding command list. */
	for (cnt = 0; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
		struct scsi_cmnd *cmd;
		sp = ha->outstanding_cmds[cnt];
		if (sp) {
			cmd = sp->cmd;
			CMD_RESULT(cmd) = DID_RESET << 16;
			CMD_HANDLE(cmd) = COMPLETED_HANDLE;
			ha->outstanding_cmds[cnt] = NULL;
			list_add_tail(&sp->list, &ha->done_q);
		}
	}

	qla1280_done(ha);

	status = qla1280_load_firmware(ha);
	if (status)
		goto out;

	/* Setup adapter based on NVRAM parameters. */
	qla1280_nvram_config (ha);

	status = qla1280_init_rings(ha);
	if (status)
		goto out;
		
	/* Issue SCSI reset. */
	for (bus = 0; bus < ha->ports; bus++)
		qla1280_bus_reset(ha, bus);
		
	ha->flags.abort_isp_active = 0;
 out:
	if (status) {
		printk(KERN_WARNING
		       "qla1280: ISP error recovery failed, board disabled");
		qla1280_reset_adapter(ha);
		dprintk(2, "qla1280_abort_isp: **** FAILED ****\n");
	}

	LEAVE("qla1280_abort_isp");
	return status;
}


/*
 * qla1280_debounce_register
 *      Debounce register.
 *
 * Input:
 *      port = register address.
 *
 * Returns:
 *      register value.
 */
static u16
qla1280_debounce_register(volatile u16 __iomem * addr)
{
	volatile u16 ret;
	volatile u16 ret2;

	ret = RD_REG_WORD(addr);
	ret2 = RD_REG_WORD(addr);

	if (ret == ret2)
		return ret;

	do {
		cpu_relax();
		ret = RD_REG_WORD(addr);
		ret2 = RD_REG_WORD(addr);
	} while (ret != ret2);

	return ret;
}


/************************************************************************
 * qla1280_check_for_dead_scsi_bus                                      *
 *                                                                      *
 *    This routine checks for a dead SCSI bus                           *
 ************************************************************************/
#define SET_SXP_BANK            0x0100
#define SCSI_PHASE_INVALID      0x87FF
static int
qla1280_check_for_dead_scsi_bus(struct scsi_qla_host *ha, unsigned int bus)
{
	uint16_t config_reg, scsi_control;
	struct device_reg __iomem *reg = ha->iobase;

	if (ha->bus_settings[bus].scsi_bus_dead) {
		WRT_REG_WORD(&reg->host_cmd, HC_PAUSE_RISC);
		config_reg = RD_REG_WORD(&reg->cfg_1);
		WRT_REG_WORD(&reg->cfg_1, SET_SXP_BANK);
		scsi_control = RD_REG_WORD(&reg->scsiControlPins);
		WRT_REG_WORD(&reg->cfg_1, config_reg);
		WRT_REG_WORD(&reg->host_cmd, HC_RELEASE_RISC);

		if (scsi_control == SCSI_PHASE_INVALID) {
			ha->bus_settings[bus].scsi_bus_dead = 1;
			return 1;	/* bus is dead */
		} else {
			ha->bus_settings[bus].scsi_bus_dead = 0;
			ha->bus_settings[bus].failed_reset_count = 0;
		}
	}
	return 0;		/* bus is not dead */
}

static void
qla1280_get_target_parameters(struct scsi_qla_host *ha,
			      struct scsi_device *device)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	int bus, target, lun;

	bus = device->channel;
	target = device->id;
	lun = device->lun;


	mb[0] = MBC_GET_TARGET_PARAMETERS;
	mb[1] = (uint16_t) (bus ? target | BIT_7 : target);
	mb[1] <<= 8;
	qla1280_mailbox_command(ha, BIT_6 | BIT_3 | BIT_2 | BIT_1 | BIT_0,
				&mb[0]);

	printk(KERN_INFO "scsi(%li:%d:%d:%d):", ha->host_no, bus, target, lun);

	if (mb[3] != 0) {
		printk(KERN_CONT " Sync: period %d, offset %d",
		       (mb[3] & 0xff), (mb[3] >> 8));
		if (mb[2] & BIT_13)
			printk(KERN_CONT ", Wide");
		if ((mb[2] & BIT_5) && ((mb[6] >> 8) & 0xff) >= 2)
			printk(KERN_CONT ", DT");
	} else
		printk(KERN_CONT " Async");

	if (device->simple_tags)
		printk(KERN_CONT ", Tagged queuing: depth %d", device->queue_depth);
	printk(KERN_CONT "\n");
}


#if DEBUG_QLA1280
static void
__qla1280_dump_buffer(char *b, int size)
{
	int cnt;
	u8 c;

	printk(KERN_DEBUG " 0   1   2   3   4   5   6   7   8   9   Ah  "
	       "Bh  Ch  Dh  Eh  Fh\n");
	printk(KERN_DEBUG "---------------------------------------------"
	       "------------------\n");

	for (cnt = 0; cnt < size;) {
		c = *b++;

		printk("0x%02x", c);
		cnt++;
		if (!(cnt % 16))
			printk("\n");
		else
			printk(" ");
	}
	if (cnt % 16)
		printk("\n");
}

/**************************************************************************
 *   ql1280_print_scsi_cmd
 *
 **************************************************************************/
static void
__qla1280_print_scsi_cmd(struct scsi_cmnd *cmd)
{
	struct scsi_qla_host *ha;
	struct Scsi_Host *host = CMD_HOST(cmd);
	struct srb *sp;
	/* struct scatterlist *sg; */

	int i;
	ha = (struct scsi_qla_host *)host->hostdata;

	sp = scsi_cmd_priv(cmd);
	printk("SCSI Command @= 0x%p, Handle=0x%p\n", cmd, CMD_HANDLE(cmd));
	printk("  chan=%d, target = 0x%02x, lun = 0x%02x, cmd_len = 0x%02x\n",
	       SCSI_BUS_32(cmd), SCSI_TCN_32(cmd), SCSI_LUN_32(cmd),
	       CMD_CDBLEN(cmd));
	printk(" CDB = ");
	for (i = 0; i < cmd->cmd_len; i++) {
		printk("0x%02x ", cmd->cmnd[i]);
	}
	printk("  seg_cnt =%d\n", scsi_sg_count(cmd));
	printk("  request buffer=0x%p, request buffer len=0x%x\n",
	       scsi_sglist(cmd), scsi_bufflen(cmd));
	/* if (cmd->use_sg)
	   {
	   sg = (struct scatterlist *) cmd->request_buffer;
	   printk("  SG buffer: \n");
	   qla1280_dump_buffer(1, (char *)sg, (cmd->use_sg*sizeof(struct scatterlist)));
	   } */
	printk("  tag=%d, transfersize=0x%x \n",
	       scsi_cmd_to_rq(cmd)->tag, cmd->transfersize);
	printk(" underflow size = 0x%x, direction=0x%x\n",
	       cmd->underflow, cmd->sc_data_direction);
}

/**************************************************************************
 *   ql1280_dump_device
 *
 **************************************************************************/
static void
ql1280_dump_device(struct scsi_qla_host *ha)
{

	struct scsi_cmnd *cp;
	struct srb *sp;
	int i;

	printk(KERN_DEBUG "Outstanding Commands on controller:\n");

	for (i = 0; i < MAX_OUTSTANDING_COMMANDS; i++) {
		if ((sp = ha->outstanding_cmds[i]) == NULL)
			continue;
		if ((cp = sp->cmd) == NULL)
			continue;
		qla1280_print_scsi_cmd(1, cp);
	}
}
#endif


enum tokens {
	TOKEN_NVRAM,
	TOKEN_SYNC,
	TOKEN_WIDE,
	TOKEN_PPR,
	TOKEN_VERBOSE,
	TOKEN_DEBUG,
};

struct setup_tokens {
	char *token;
	int val;
};

static struct setup_tokens setup_token[] __initdata = 
{
	{ "nvram", TOKEN_NVRAM },
	{ "sync", TOKEN_SYNC },
	{ "wide", TOKEN_WIDE },
	{ "ppr", TOKEN_PPR },
	{ "verbose", TOKEN_VERBOSE },
	{ "debug", TOKEN_DEBUG },
};


/**************************************************************************
 *   qla1280_setup
 *
 *   Handle boot parameters. This really needs to be changed so one
 *   can specify per adapter parameters.
 **************************************************************************/
static int __init
qla1280_setup(char *s)
{
	char *cp, *ptr;
	unsigned long val;

	cp = s;

	while (cp && (ptr = strchr(cp, ':'))) {
		ptr++;
		if (!strcmp(ptr, "yes")) {
			val = 0x10000;
			ptr += 3;
		} else if (!strcmp(ptr, "no")) {
 			val = 0;
			ptr += 2;
		} else
			val = simple_strtoul(ptr, &ptr, 0);

		switch (qla1280_get_token(cp)) {
		case TOKEN_NVRAM:
			if (!val)
				driver_setup.no_nvram = 1;
			break;
		case TOKEN_SYNC:
			if (!val)
				driver_setup.no_sync = 1;
			else if (val != 0x10000)
				driver_setup.sync_mask = val;
			break;
		case TOKEN_WIDE:
			if (!val)
				driver_setup.no_wide = 1;
			else if (val != 0x10000)
				driver_setup.wide_mask = val;
			break;
		case TOKEN_PPR:
			if (!val)
				driver_setup.no_ppr = 1;
			else if (val != 0x10000)
				driver_setup.ppr_mask = val;
			break;
		case TOKEN_VERBOSE:
			qla1280_verbose = val;
			break;
		default:
			printk(KERN_INFO "qla1280: unknown boot option %s\n",
			       cp);
		}

		cp = strchr(ptr, ';');
		if (cp)
			cp++;
		else {
			break;
		}
	}
	return 1;
}


static int __init
qla1280_get_token(char *str)
{
	char *sep;
	long ret = -1;
	int i;

	sep = strchr(str, ':');

	if (sep) {
		for (i = 0; i < ARRAY_SIZE(setup_token); i++) {
			if (!strncmp(setup_token[i].token, str, (sep - str))) {
				ret =  setup_token[i].val;
				break;
			}
		}
	}

	return ret;
}


static const struct scsi_host_template qla1280_driver_template = {
	.module			= THIS_MODULE,
	.proc_name		= "qla1280",
	.name			= "Qlogic ISP 1280/12160",
	.info			= qla1280_info,
	.sdev_configure		= qla1280_sdev_configure,
	.queuecommand		= qla1280_queuecommand,
	.eh_abort_handler	= qla1280_eh_abort,
	.eh_device_reset_handler= qla1280_eh_device_reset,
	.eh_bus_reset_handler	= qla1280_eh_bus_reset,
	.eh_host_reset_handler	= qla1280_eh_adapter_reset,
	.bios_param		= qla1280_biosparam,
	.can_queue		= MAX_OUTSTANDING_COMMANDS,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.cmd_size		= sizeof(struct srb),
};


static int
qla1280_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int devnum = id->driver_data;
	struct qla_boards *bdp = &ql1280_board_tbl[devnum];
	struct Scsi_Host *host;
	struct scsi_qla_host *ha;
	int error = -ENODEV;

	/* Bypass all AMI SUBSYS VENDOR IDs */
	if (pdev->subsystem_vendor == PCI_VENDOR_ID_AMI) {
		printk(KERN_INFO
		       "qla1280: Skipping AMI SubSys Vendor ID Chip\n");
		goto error;
	}

	printk(KERN_INFO "qla1280: %s found on PCI bus %i, dev %i\n",
	       bdp->name, pdev->bus->number, PCI_SLOT(pdev->devfn));
	
	if (pci_enable_device(pdev)) {
		printk(KERN_WARNING
		       "qla1280: Failed to enabled pci device, aborting.\n");
		goto error;
	}

	pci_set_master(pdev);

	error = -ENOMEM;
	host = scsi_host_alloc(&qla1280_driver_template, sizeof(*ha));
	if (!host) {
		printk(KERN_WARNING
		       "qla1280: Failed to register host, aborting.\n");
		goto error_disable_device;
	}

	ha = (struct scsi_qla_host *)host->hostdata;
	memset(ha, 0, sizeof(struct scsi_qla_host));

	ha->pdev = pdev;
	ha->devnum = devnum;	/* specifies microcode load address */

#ifdef QLA_64BIT_PTR
	if (dma_set_mask_and_coherent(&ha->pdev->dev, DMA_BIT_MASK(64))) {
		if (dma_set_mask(&ha->pdev->dev, DMA_BIT_MASK(32))) {
			printk(KERN_WARNING "scsi(%li): Unable to set a "
			       "suitable DMA mask - aborting\n", ha->host_no);
			error = -ENODEV;
			goto error_put_host;
		}
	} else
		dprintk(2, "scsi(%li): 64 Bit PCI Addressing Enabled\n",
			ha->host_no);
#else
	if (dma_set_mask(&ha->pdev->dev, DMA_BIT_MASK(32))) {
		printk(KERN_WARNING "scsi(%li): Unable to set a "
		       "suitable DMA mask - aborting\n", ha->host_no);
		error = -ENODEV;
		goto error_put_host;
	}
#endif

	ha->request_ring = dma_alloc_coherent(&ha->pdev->dev,
			((REQUEST_ENTRY_CNT + 1) * sizeof(request_t)),
			&ha->request_dma, GFP_KERNEL);
	if (!ha->request_ring) {
		printk(KERN_INFO "qla1280: Failed to get request memory\n");
		goto error_put_host;
	}

	ha->response_ring = dma_alloc_coherent(&ha->pdev->dev,
			((RESPONSE_ENTRY_CNT + 1) * sizeof(struct response)),
			&ha->response_dma, GFP_KERNEL);
	if (!ha->response_ring) {
		printk(KERN_INFO "qla1280: Failed to get response memory\n");
		goto error_free_request_ring;
	}

	ha->ports = bdp->numPorts;

	ha->host = host;
	ha->host_no = host->host_no;

	host->irq = pdev->irq;
	host->max_channel = bdp->numPorts - 1;
	host->max_lun = MAX_LUNS - 1;
	host->max_id = MAX_TARGETS;
	host->max_sectors = 1024;
	host->unique_id = host->host_no;

	error = -ENODEV;

#if MEMORY_MAPPED_IO
	ha->mmpbase = pci_ioremap_bar(ha->pdev, 1);
	if (!ha->mmpbase) {
		printk(KERN_INFO "qla1280: Unable to map I/O memory\n");
		goto error_free_response_ring;
	}

	host->base = (unsigned long)ha->mmpbase;
	ha->iobase = (struct device_reg __iomem *)ha->mmpbase;
#else
	host->io_port = pci_resource_start(ha->pdev, 0);
	if (!request_region(host->io_port, 0xff, "qla1280")) {
		printk(KERN_INFO "qla1280: Failed to reserve i/o region "
				 "0x%04lx-0x%04lx - already in use\n",
		       host->io_port, host->io_port + 0xff);
		goto error_free_response_ring;
	}

	ha->iobase = (struct device_reg *)host->io_port;
#endif

	INIT_LIST_HEAD(&ha->done_q);

	/* Disable ISP interrupts. */
	qla1280_disable_intrs(ha);

	if (request_irq(pdev->irq, qla1280_intr_handler, IRQF_SHARED,
				"qla1280", ha)) {
		printk("qla1280 : Failed to reserve interrupt %d already "
		       "in use\n", pdev->irq);
		goto error_release_region;
	}

	/* load the F/W, read paramaters, and init the H/W */
	if (qla1280_initialize_adapter(ha)) {
		printk(KERN_INFO "qla1x160: Failed to initialize adapter\n");
		goto error_free_irq;
	}

	/* set our host ID  (need to do something about our two IDs) */
	host->this_id = ha->bus_settings[0].id;

	pci_set_drvdata(pdev, host);

	error = scsi_add_host(host, &pdev->dev);
	if (error)
		goto error_disable_adapter;
	scsi_scan_host(host);

	return 0;

 error_disable_adapter:
	qla1280_disable_intrs(ha);
 error_free_irq:
	free_irq(pdev->irq, ha);
 error_release_region:
#if MEMORY_MAPPED_IO
	iounmap(ha->mmpbase);
#else
	release_region(host->io_port, 0xff);
#endif
 error_free_response_ring:
	dma_free_coherent(&ha->pdev->dev,
			((RESPONSE_ENTRY_CNT + 1) * sizeof(struct response)),
			ha->response_ring, ha->response_dma);
 error_free_request_ring:
	dma_free_coherent(&ha->pdev->dev,
			((REQUEST_ENTRY_CNT + 1) * sizeof(request_t)),
			ha->request_ring, ha->request_dma);
 error_put_host:
	scsi_host_put(host);
 error_disable_device:
	pci_disable_device(pdev);
 error:
	return error;
}


static void
qla1280_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	struct scsi_qla_host *ha = (struct scsi_qla_host *)host->hostdata;

	scsi_remove_host(host);

	qla1280_disable_intrs(ha);

	free_irq(pdev->irq, ha);

#if MEMORY_MAPPED_IO
	iounmap(ha->mmpbase);
#else
	release_region(host->io_port, 0xff);
#endif

	dma_free_coherent(&ha->pdev->dev,
			((REQUEST_ENTRY_CNT + 1) * (sizeof(request_t))),
			ha->request_ring, ha->request_dma);
	dma_free_coherent(&ha->pdev->dev,
			((RESPONSE_ENTRY_CNT + 1) * (sizeof(struct response))),
			ha->response_ring, ha->response_dma);

	pci_disable_device(pdev);

	scsi_host_put(host);
}

static struct pci_driver qla1280_pci_driver = {
	.name		= "qla1280",
	.id_table	= qla1280_pci_tbl,
	.probe		= qla1280_probe_one,
	.remove		= qla1280_remove_one,
};

static int __init
qla1280_init(void)
{
#ifdef MODULE
	/*
	 * If we are called as a module, the qla1280 pointer may not be null
	 * and it would point to our bootup string, just like on the lilo
	 * command line.  IF not NULL, then process this config string with
	 * qla1280_setup
	 *
	 * Boot time Options
	 * To add options at boot time add a line to your lilo.conf file like:
	 * append="qla1280=verbose,max_tags:{{255,255,255,255},{255,255,255,255}}"
	 * which will result in the first four devices on the first two
	 * controllers being set to a tagged queue depth of 32.
	 */
	if (qla1280)
		qla1280_setup(qla1280);
#endif

	return pci_register_driver(&qla1280_pci_driver);
}

static void __exit
qla1280_exit(void)
{
	int i;

	pci_unregister_driver(&qla1280_pci_driver);
	/* release any allocated firmware images */
	for (i = 0; i < QL_NUM_FW_IMAGES; i++) {
		release_firmware(qla1280_fw_tbl[i].fw);
		qla1280_fw_tbl[i].fw = NULL;
	}
}

module_init(qla1280_init);
module_exit(qla1280_exit);

MODULE_AUTHOR("Qlogic & Jes Sorensen");
MODULE_DESCRIPTION("Qlogic ISP SCSI (qla1x80/qla1x160) driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("qlogic/1040.bin");
MODULE_FIRMWARE("qlogic/1280.bin");
MODULE_FIRMWARE("qlogic/12160.bin");
MODULE_VERSION(QLA1280_VERSION);
