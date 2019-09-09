/*****************************************************************************/
/* ips.c -- driver for the Adaptec / IBM ServeRAID controller                */
/*                                                                           */
/* Written By: Keith Mitchell, IBM Corporation                               */
/*             Jack Hammer, Adaptec, Inc.                                    */
/*             David Jeffery, Adaptec, Inc.                                  */
/*                                                                           */
/* Copyright (C) 2000 IBM Corporation                                        */
/* Copyright (C) 2002,2003 Adaptec, Inc.                                     */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation; either version 2 of the License, or         */
/* (at your option) any later version.                                       */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             */
/* GNU General Public License for more details.                              */
/*                                                                           */
/* NO WARRANTY                                                               */
/* THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR        */
/* CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT      */
/* LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,      */
/* MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is    */
/* solely responsible for determining the appropriateness of using and       */
/* distributing the Program and assumes all risks associated with its        */
/* exercise of rights under this Agreement, including but not limited to     */
/* the risks and costs of program errors, damage to or loss of data,         */
/* programs or equipment, and unavailability or interruption of operations.  */
/*                                                                           */
/* DISCLAIMER OF LIABILITY                                                   */
/* NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY   */
/* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL        */
/* DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND   */
/* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR     */
/* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE    */
/* USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED  */
/* HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES             */
/*                                                                           */
/* You should have received a copy of the GNU General Public License         */
/* along with this program; if not, write to the Free Software               */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
/*                                                                           */
/* Bugs/Comments/Suggestions about this driver should be mailed to:          */
/*      ipslinux@adaptec.com        	                                     */
/*                                                                           */
/* For system support issues, contact your local IBM Customer support.       */
/* Directions to find IBM Customer Support for each country can be found at: */
/*      http://www.ibm.com/planetwide/                                       */
/*                                                                           */
/*****************************************************************************/

/*****************************************************************************/
/* Change Log                                                                */
/*                                                                           */
/* 0.99.02  - Breakup commands that are bigger than 8 * the stripe size      */
/* 0.99.03  - Make interrupt routine handle all completed request on the     */
/*            adapter not just the first one                                 */
/*          - Make sure passthru commands get woken up if we run out of      */
/*            SCBs                                                           */
/*          - Send all of the commands on the queue at once rather than      */
/*            one at a time since the card will support it.                  */
/* 0.99.04  - Fix race condition in the passthru mechanism -- this required  */
/*            the interface to the utilities to change                       */
/*          - Fix error recovery code                                        */
/* 0.99.05  - Fix an oops when we get certain passthru commands              */
/* 1.00.00  - Initial Public Release                                         */
/*            Functionally equivalent to 0.99.05                             */
/* 3.60.00  - Bump max commands to 128 for use with firmware 3.60            */
/*          - Change version to 3.60 to coincide with release numbering.     */
/* 3.60.01  - Remove bogus error check in passthru routine                   */
/* 3.60.02  - Make DCDB direction based on lookup table                      */
/*          - Only allow one DCDB command to a SCSI ID at a time             */
/* 4.00.00  - Add support for ServeRAID 4                                    */
/* 4.00.01  - Add support for First Failure Data Capture                     */
/* 4.00.02  - Fix problem with PT DCDB with no buffer                        */
/* 4.00.03  - Add alternative passthru interface                             */
/*          - Add ability to flash BIOS                                      */
/* 4.00.04  - Rename structures/constants to be prefixed with IPS_           */
/* 4.00.05  - Remove wish_block from init routine                            */
/*          - Use linux/spinlock.h instead of asm/spinlock.h for kernels     */
/*            2.3.18 and later                                               */
/*          - Sync with other changes from the 2.3 kernels                   */
/* 4.00.06  - Fix timeout with initial FFDC command                          */
/* 4.00.06a - Port to 2.4 (trivial) -- Christoph Hellwig <hch@infradead.org> */
/* 4.10.00  - Add support for ServeRAID 4M/4L                                */
/* 4.10.13  - Fix for dynamic unload and proc file system                    */
/* 4.20.03  - Rename version to coincide with new release schedules          */
/*            Performance fixes                                              */
/*            Fix truncation of /proc files with cat                         */
/*            Merge in changes through kernel 2.4.0test1ac21                 */
/* 4.20.13  - Fix some failure cases / reset code                            */
/*          - Hook into the reboot_notifier to flush the controller cache    */
/* 4.50.01  - Fix problem when there is a hole in logical drive numbering    */
/* 4.70.09  - Use a Common ( Large Buffer ) for Flashing from the JCRM CD    */
/*          - Add IPSSEND Flash Support                                      */
/*          - Set Sense Data for Unknown SCSI Command                        */
/*          - Use Slot Number from NVRAM Page 5                              */
/*          - Restore caller's DCDB Structure                                */
/* 4.70.12  - Corrective actions for bad controller ( during initialization )*/
/* 4.70.13  - Don't Send CDB's if we already know the device is not present  */
/*          - Don't release HA Lock in ips_next() until SC taken off queue   */
/*          - Unregister SCSI device in ips_release()                        */
/* 4.70.15  - Fix Breakup for very large ( non-SG ) requests in ips_done()   */
/* 4.71.00  - Change all memory allocations to not use GFP_DMA flag          */
/*            Code Clean-Up for 2.4.x kernel                                 */
/* 4.72.00  - Allow for a Scatter-Gather Element to exceed MAX_XFER Size     */
/* 4.72.01  - I/O Mapped Memory release ( so "insmod ips" does not Fail )    */
/*          - Don't Issue Internal FFDC Command if there are Active Commands */
/*          - Close Window for getting too many IOCTL's active               */
/* 4.80.00  - Make ia64 Safe                                                 */
/* 4.80.04  - Eliminate calls to strtok() if 2.4.x or greater                */
/*          - Adjustments to Device Queue Depth                              */
/* 4.80.14  - Take all semaphores off stack                                  */
/*          - Clean Up New_IOCTL path                                        */
/* 4.80.20  - Set max_sectors in Scsi_Host structure ( if >= 2.4.7 kernel )  */
/*          - 5 second delay needed after resetting an i960 adapter          */
/* 4.80.26  - Clean up potential code problems ( Arjan's recommendations )   */
/* 4.90.01  - Version Matching for FirmWare, BIOS, and Driver                */
/* 4.90.05  - Use New PCI Architecture to facilitate Hot Plug Development    */
/* 4.90.08  - Increase Delays in Flashing ( Trombone Only - 4H )             */
/* 4.90.08  - Data Corruption if First Scatter Gather Element is > 64K       */
/* 4.90.11  - Don't actually RESET unless it's physically required           */
/*          - Remove unused compile options                                  */
/* 5.00.01  - Sarasota ( 5i ) adapters must always be scanned first          */
/*          - Get rid on IOCTL_NEW_COMMAND code                              */
/*          - Add Extended DCDB Commands for Tape Support in 5I              */
/* 5.10.12  - use pci_dma interfaces, update for 2.5 kernel changes          */
/* 5.10.15  - remove unused code (sem, macros, etc.)                         */
/* 5.30.00  - use __devexit_p()                                              */
/* 6.00.00  - Add 6x Adapters and Battery Flash                              */
/* 6.10.00  - Remove 1G Addressing Limitations                               */
/* 6.11.xx  - Get VersionInfo buffer off the stack !              DDTS 60401 */
/* 6.11.xx  - Make Logical Drive Info structure safe for DMA      DDTS 60639 */
/* 7.10.18  - Add highmem_io flag in SCSI Templete for 2.4 kernels           */
/*          - Fix path/name for scsi_hosts.h include for 2.6 kernels         */
/*          - Fix sort order of 7k                                           */
/*          - Remove 3 unused "inline" functions                             */
/* 7.12.xx  - Use STATIC functions wherever possible                        */
/*          - Clean up deprecated MODULE_PARM calls                          */
/* 7.12.05  - Remove Version Matching per IBM request                        */
/*****************************************************************************/

/*
 * Conditional Compilation directives for this driver:
 *
 * IPS_DEBUG            - Turn on debugging info
 *
 * Parameters:
 *
 * debug:<number>       - Set debug level to <number>
 *                        NOTE: only works when IPS_DEBUG compile directive is used.
 *       1              - Normal debug messages
 *       2              - Verbose debug messages
 *       11             - Method trace (non interrupt)
 *       12             - Method trace (includes interrupt)
 *
 * noi2o                - Don't use I2O Queues (ServeRAID 4 only)
 * nommap               - Don't use memory mapped I/O
 * ioctlsize            - Initial size of the IOCTL buffer
 */

#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/page.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/reboot.h>
#include <linux/interrupt.h>

#include <linux/blkdev.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>

#include <scsi/sg.h>
#include "scsi.h"
#include <scsi/scsi_host.h>

#include "ips.h"

#include <linux/module.h>

#include <linux/stat.h>

#include <linux/spinlock.h>
#include <linux/init.h>

#include <linux/smp.h>

#ifdef MODULE
static char *ips = NULL;
module_param(ips, charp, 0);
#endif

/*
 * DRIVER_VER
 */
#define IPS_VERSION_HIGH        IPS_VER_MAJOR_STRING "." IPS_VER_MINOR_STRING
#define IPS_VERSION_LOW         "." IPS_VER_BUILD_STRING " "

#define IPS_DMA_DIR(scb) ((!scb->scsi_cmd || ips_is_passthru(scb->scsi_cmd) || \
                         DMA_NONE == scb->scsi_cmd->sc_data_direction) ? \
                         DMA_BIDIRECTIONAL : \
                         scb->scsi_cmd->sc_data_direction)

#ifdef IPS_DEBUG
#define METHOD_TRACE(s, i)    if (ips_debug >= (i+10)) printk(KERN_NOTICE s "\n");
#define DEBUG(i, s)           if (ips_debug >= i) printk(KERN_NOTICE s "\n");
#define DEBUG_VAR(i, s, v...) if (ips_debug >= i) printk(KERN_NOTICE s "\n", v);
#else
#define METHOD_TRACE(s, i)
#define DEBUG(i, s)
#define DEBUG_VAR(i, s, v...)
#endif

/*
 * Function prototypes
 */
static int ips_eh_abort(struct scsi_cmnd *);
static int ips_eh_reset(struct scsi_cmnd *);
static int ips_queue(struct Scsi_Host *, struct scsi_cmnd *);
static const char *ips_info(struct Scsi_Host *);
static irqreturn_t do_ipsintr(int, void *);
static int ips_hainit(ips_ha_t *);
static int ips_map_status(ips_ha_t *, ips_scb_t *, ips_stat_t *);
static int ips_send_wait(ips_ha_t *, ips_scb_t *, int, int);
static int ips_send_cmd(ips_ha_t *, ips_scb_t *);
static int ips_online(ips_ha_t *, ips_scb_t *);
static int ips_inquiry(ips_ha_t *, ips_scb_t *);
static int ips_rdcap(ips_ha_t *, ips_scb_t *);
static int ips_msense(ips_ha_t *, ips_scb_t *);
static int ips_reqsen(ips_ha_t *, ips_scb_t *);
static int ips_deallocatescbs(ips_ha_t *, int);
static int ips_allocatescbs(ips_ha_t *);
static int ips_reset_copperhead(ips_ha_t *);
static int ips_reset_copperhead_memio(ips_ha_t *);
static int ips_reset_morpheus(ips_ha_t *);
static int ips_issue_copperhead(ips_ha_t *, ips_scb_t *);
static int ips_issue_copperhead_memio(ips_ha_t *, ips_scb_t *);
static int ips_issue_i2o(ips_ha_t *, ips_scb_t *);
static int ips_issue_i2o_memio(ips_ha_t *, ips_scb_t *);
static int ips_isintr_copperhead(ips_ha_t *);
static int ips_isintr_copperhead_memio(ips_ha_t *);
static int ips_isintr_morpheus(ips_ha_t *);
static int ips_wait(ips_ha_t *, int, int);
static int ips_write_driver_status(ips_ha_t *, int);
static int ips_read_adapter_status(ips_ha_t *, int);
static int ips_read_subsystem_parameters(ips_ha_t *, int);
static int ips_read_config(ips_ha_t *, int);
static int ips_clear_adapter(ips_ha_t *, int);
static int ips_readwrite_page5(ips_ha_t *, int, int);
static int ips_init_copperhead(ips_ha_t *);
static int ips_init_copperhead_memio(ips_ha_t *);
static int ips_init_morpheus(ips_ha_t *);
static int ips_isinit_copperhead(ips_ha_t *);
static int ips_isinit_copperhead_memio(ips_ha_t *);
static int ips_isinit_morpheus(ips_ha_t *);
static int ips_erase_bios(ips_ha_t *);
static int ips_program_bios(ips_ha_t *, char *, uint32_t, uint32_t);
static int ips_verify_bios(ips_ha_t *, char *, uint32_t, uint32_t);
static int ips_erase_bios_memio(ips_ha_t *);
static int ips_program_bios_memio(ips_ha_t *, char *, uint32_t, uint32_t);
static int ips_verify_bios_memio(ips_ha_t *, char *, uint32_t, uint32_t);
static int ips_flash_copperhead(ips_ha_t *, ips_passthru_t *, ips_scb_t *);
static int ips_flash_bios(ips_ha_t *, ips_passthru_t *, ips_scb_t *);
static int ips_flash_firmware(ips_ha_t *, ips_passthru_t *, ips_scb_t *);
static void ips_free_flash_copperhead(ips_ha_t * ha);
static void ips_get_bios_version(ips_ha_t *, int);
static void ips_identify_controller(ips_ha_t *);
static void ips_chkstatus(ips_ha_t *, IPS_STATUS *);
static void ips_enable_int_copperhead(ips_ha_t *);
static void ips_enable_int_copperhead_memio(ips_ha_t *);
static void ips_enable_int_morpheus(ips_ha_t *);
static int ips_intr_copperhead(ips_ha_t *);
static int ips_intr_morpheus(ips_ha_t *);
static void ips_next(ips_ha_t *, int);
static void ipsintr_blocking(ips_ha_t *, struct ips_scb *);
static void ipsintr_done(ips_ha_t *, struct ips_scb *);
static void ips_done(ips_ha_t *, ips_scb_t *);
static void ips_free(ips_ha_t *);
static void ips_init_scb(ips_ha_t *, ips_scb_t *);
static void ips_freescb(ips_ha_t *, ips_scb_t *);
static void ips_setup_funclist(ips_ha_t *);
static void ips_statinit(ips_ha_t *);
static void ips_statinit_memio(ips_ha_t *);
static void ips_fix_ffdc_time(ips_ha_t *, ips_scb_t *, time64_t);
static void ips_ffdc_reset(ips_ha_t *, int);
static void ips_ffdc_time(ips_ha_t *);
static uint32_t ips_statupd_copperhead(ips_ha_t *);
static uint32_t ips_statupd_copperhead_memio(ips_ha_t *);
static uint32_t ips_statupd_morpheus(ips_ha_t *);
static ips_scb_t *ips_getscb(ips_ha_t *);
static void ips_putq_scb_head(ips_scb_queue_t *, ips_scb_t *);
static void ips_putq_wait_tail(ips_wait_queue_entry_t *, struct scsi_cmnd *);
static void ips_putq_copp_tail(ips_copp_queue_t *,
				      ips_copp_wait_item_t *);
static ips_scb_t *ips_removeq_scb_head(ips_scb_queue_t *);
static ips_scb_t *ips_removeq_scb(ips_scb_queue_t *, ips_scb_t *);
static struct scsi_cmnd *ips_removeq_wait_head(ips_wait_queue_entry_t *);
static struct scsi_cmnd *ips_removeq_wait(ips_wait_queue_entry_t *,
					  struct scsi_cmnd *);
static ips_copp_wait_item_t *ips_removeq_copp(ips_copp_queue_t *,
						     ips_copp_wait_item_t *);
static ips_copp_wait_item_t *ips_removeq_copp_head(ips_copp_queue_t *);

static int ips_is_passthru(struct scsi_cmnd *);
static int ips_make_passthru(ips_ha_t *, struct scsi_cmnd *, ips_scb_t *, int);
static int ips_usrcmd(ips_ha_t *, ips_passthru_t *, ips_scb_t *);
static void ips_cleanup_passthru(ips_ha_t *, ips_scb_t *);
static void ips_scmd_buf_write(struct scsi_cmnd * scmd, void *data,
			       unsigned int count);
static void ips_scmd_buf_read(struct scsi_cmnd * scmd, void *data,
			      unsigned int count);

static int ips_write_info(struct Scsi_Host *, char *, int);
static int ips_show_info(struct seq_file *, struct Scsi_Host *);
static int ips_host_info(ips_ha_t *, struct seq_file *);
static int ips_abort_init(ips_ha_t * ha, int index);
static int ips_init_phase2(int index);

static int ips_init_phase1(struct pci_dev *pci_dev, int *indexPtr);
static int ips_register_scsi(int index);

static int  ips_poll_for_flush_complete(ips_ha_t * ha);
static void ips_flush_and_reset(ips_ha_t *ha);

/*
 * global variables
 */
static const char ips_name[] = "ips";
static struct Scsi_Host *ips_sh[IPS_MAX_ADAPTERS];	/* Array of host controller structures */
static ips_ha_t *ips_ha[IPS_MAX_ADAPTERS];	/* Array of HA structures */
static unsigned int ips_next_controller;
static unsigned int ips_num_controllers;
static unsigned int ips_released_controllers;
static int ips_hotplug;
static int ips_cmd_timeout = 60;
static int ips_reset_timeout = 60 * 5;
static int ips_force_memio = 1;		/* Always use Memory Mapped I/O    */
static int ips_force_i2o = 1;	/* Always use I2O command delivery */
static int ips_ioctlsize = IPS_IOCTL_SIZE;	/* Size of the ioctl buffer        */
static int ips_cd_boot;			/* Booting from Manager CD         */
static char *ips_FlashData = NULL;	/* CD Boot - Flash Data Buffer      */
static dma_addr_t ips_flashbusaddr;
static long ips_FlashDataInUse;		/* CD Boot - Flash Data In Use Flag */
static uint32_t MaxLiteCmds = 32;	/* Max Active Cmds for a Lite Adapter */
static struct scsi_host_template ips_driver_template = {
	.info			= ips_info,
	.queuecommand		= ips_queue,
	.eh_abort_handler	= ips_eh_abort,
	.eh_host_reset_handler	= ips_eh_reset,
	.proc_name		= "ips",
	.show_info		= ips_show_info,
	.write_info		= ips_write_info,
	.slave_configure	= ips_slave_configure,
	.bios_param		= ips_biosparam,
	.this_id		= -1,
	.sg_tablesize		= IPS_MAX_SG,
	.cmd_per_lun		= 3,
	.no_write_same		= 1,
};


/* This table describes all ServeRAID Adapters */
static struct  pci_device_id  ips_pci_table[] = {
	{ 0x1014, 0x002E, PCI_ANY_ID, PCI_ANY_ID, 0, 0 },
	{ 0x1014, 0x01BD, PCI_ANY_ID, PCI_ANY_ID, 0, 0 },
	{ 0x9005, 0x0250, PCI_ANY_ID, PCI_ANY_ID, 0, 0 },
	{ 0, }
};

MODULE_DEVICE_TABLE( pci, ips_pci_table );

static char ips_hot_plug_name[] = "ips";

static int  ips_insert_device(struct pci_dev *pci_dev, const struct pci_device_id *ent);
static void ips_remove_device(struct pci_dev *pci_dev);

static struct pci_driver ips_pci_driver = {
	.name		= ips_hot_plug_name,
	.id_table	= ips_pci_table,
	.probe		= ips_insert_device,
	.remove		= ips_remove_device,
};


/*
 * Necessary forward function protoypes
 */
static int ips_halt(struct notifier_block *nb, ulong event, void *buf);

#define MAX_ADAPTER_NAME 15

static char ips_adapter_name[][30] = {
	"ServeRAID",
	"ServeRAID II",
	"ServeRAID on motherboard",
	"ServeRAID on motherboard",
	"ServeRAID 3H",
	"ServeRAID 3L",
	"ServeRAID 4H",
	"ServeRAID 4M",
	"ServeRAID 4L",
	"ServeRAID 4Mx",
	"ServeRAID 4Lx",
	"ServeRAID 5i",
	"ServeRAID 5i",
	"ServeRAID 6M",
	"ServeRAID 6i",
	"ServeRAID 7t",
	"ServeRAID 7k",
	"ServeRAID 7M"
};

static struct notifier_block ips_notifier = {
	ips_halt, NULL, 0
};

/*
 * Direction table
 */
static char ips_command_direction[] = {
	IPS_DATA_NONE, IPS_DATA_NONE, IPS_DATA_IN, IPS_DATA_IN, IPS_DATA_OUT,
	IPS_DATA_IN, IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_IN, IPS_DATA_UNK,
	IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_IN, IPS_DATA_NONE, IPS_DATA_NONE, IPS_DATA_IN, IPS_DATA_OUT,
	IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_NONE, IPS_DATA_NONE, IPS_DATA_OUT,
	IPS_DATA_NONE, IPS_DATA_IN, IPS_DATA_NONE, IPS_DATA_IN, IPS_DATA_OUT,
	IPS_DATA_NONE, IPS_DATA_UNK, IPS_DATA_IN, IPS_DATA_UNK, IPS_DATA_IN,
	IPS_DATA_UNK, IPS_DATA_OUT, IPS_DATA_IN, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_IN, IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_NONE, IPS_DATA_UNK,
	IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_OUT,
	IPS_DATA_OUT, IPS_DATA_NONE, IPS_DATA_IN, IPS_DATA_NONE, IPS_DATA_NONE,
	IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_OUT,
	IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_OUT,
	IPS_DATA_OUT, IPS_DATA_IN, IPS_DATA_IN, IPS_DATA_IN, IPS_DATA_NONE,
	IPS_DATA_UNK, IPS_DATA_NONE, IPS_DATA_NONE, IPS_DATA_NONE, IPS_DATA_UNK,
	IPS_DATA_NONE, IPS_DATA_OUT, IPS_DATA_IN, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_OUT, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_IN, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_NONE, IPS_DATA_NONE, IPS_DATA_UNK, IPS_DATA_IN, IPS_DATA_NONE,
	IPS_DATA_OUT, IPS_DATA_UNK, IPS_DATA_NONE, IPS_DATA_UNK, IPS_DATA_OUT,
	IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_OUT, IPS_DATA_NONE,
	IPS_DATA_UNK, IPS_DATA_IN, IPS_DATA_OUT, IPS_DATA_IN, IPS_DATA_IN,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_OUT,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK,
	IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK, IPS_DATA_UNK
};


/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_setup                                                  */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   setup parameters to the driver                                         */
/*                                                                          */
/****************************************************************************/
static int
ips_setup(char *ips_str)
{

	int i;
	char *key;
	char *value;
	IPS_OPTION options[] = {
		{"noi2o", &ips_force_i2o, 0},
		{"nommap", &ips_force_memio, 0},
		{"ioctlsize", &ips_ioctlsize, IPS_IOCTL_SIZE},
		{"cdboot", &ips_cd_boot, 0},
		{"maxcmds", &MaxLiteCmds, 32},
	};

	/* Don't use strtok() anymore ( if 2.4 Kernel or beyond ) */
	/* Search for value */
	while ((key = strsep(&ips_str, ",."))) {
		if (!*key)
			continue;
		value = strchr(key, ':');
		if (value)
			*value++ = '\0';
		/*
		 * We now have key/value pairs.
		 * Update the variables
		 */
		for (i = 0; i < ARRAY_SIZE(options); i++) {
			if (strncasecmp
			    (key, options[i].option_name,
			     strlen(options[i].option_name)) == 0) {
				if (value)
					*options[i].option_flag =
					    simple_strtoul(value, NULL, 0);
				else
					*options[i].option_flag =
					    options[i].option_value;
				break;
			}
		}
	}

	return (1);
}

__setup("ips=", ips_setup);

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_detect                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Detect and initialize the driver                                       */
/*                                                                          */
/* NOTE: this routine is called under the io_request_lock spinlock          */
/*                                                                          */
/****************************************************************************/
static int
ips_detect(struct scsi_host_template * SHT)
{
	int i;

	METHOD_TRACE("ips_detect", 1);

#ifdef MODULE
	if (ips)
		ips_setup(ips);
#endif

	for (i = 0; i < ips_num_controllers; i++) {
		if (ips_register_scsi(i))
			ips_free(ips_ha[i]);
		ips_released_controllers++;
	}
	ips_hotplug = 1;
	return (ips_num_controllers);
}

/****************************************************************************/
/*   configure the function pointers to use the functions that will work    */
/*   with the found version of the adapter                                  */
/****************************************************************************/
static void
ips_setup_funclist(ips_ha_t * ha)
{

	/*
	 * Setup Functions
	 */
	if (IPS_IS_MORPHEUS(ha) || IPS_IS_MARCO(ha)) {
		/* morpheus / marco / sebring */
		ha->func.isintr = ips_isintr_morpheus;
		ha->func.isinit = ips_isinit_morpheus;
		ha->func.issue = ips_issue_i2o_memio;
		ha->func.init = ips_init_morpheus;
		ha->func.statupd = ips_statupd_morpheus;
		ha->func.reset = ips_reset_morpheus;
		ha->func.intr = ips_intr_morpheus;
		ha->func.enableint = ips_enable_int_morpheus;
	} else if (IPS_USE_MEMIO(ha)) {
		/* copperhead w/MEMIO */
		ha->func.isintr = ips_isintr_copperhead_memio;
		ha->func.isinit = ips_isinit_copperhead_memio;
		ha->func.init = ips_init_copperhead_memio;
		ha->func.statupd = ips_statupd_copperhead_memio;
		ha->func.statinit = ips_statinit_memio;
		ha->func.reset = ips_reset_copperhead_memio;
		ha->func.intr = ips_intr_copperhead;
		ha->func.erasebios = ips_erase_bios_memio;
		ha->func.programbios = ips_program_bios_memio;
		ha->func.verifybios = ips_verify_bios_memio;
		ha->func.enableint = ips_enable_int_copperhead_memio;
		if (IPS_USE_I2O_DELIVER(ha))
			ha->func.issue = ips_issue_i2o_memio;
		else
			ha->func.issue = ips_issue_copperhead_memio;
	} else {
		/* copperhead */
		ha->func.isintr = ips_isintr_copperhead;
		ha->func.isinit = ips_isinit_copperhead;
		ha->func.init = ips_init_copperhead;
		ha->func.statupd = ips_statupd_copperhead;
		ha->func.statinit = ips_statinit;
		ha->func.reset = ips_reset_copperhead;
		ha->func.intr = ips_intr_copperhead;
		ha->func.erasebios = ips_erase_bios;
		ha->func.programbios = ips_program_bios;
		ha->func.verifybios = ips_verify_bios;
		ha->func.enableint = ips_enable_int_copperhead;

		if (IPS_USE_I2O_DELIVER(ha))
			ha->func.issue = ips_issue_i2o;
		else
			ha->func.issue = ips_issue_copperhead;
	}
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_release                                                */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove a driver                                                        */
/*                                                                          */
/****************************************************************************/
static int
ips_release(struct Scsi_Host *sh)
{
	ips_scb_t *scb;
	ips_ha_t *ha;
	int i;

	METHOD_TRACE("ips_release", 1);

	scsi_remove_host(sh);

	for (i = 0; i < IPS_MAX_ADAPTERS && ips_sh[i] != sh; i++) ;

	if (i == IPS_MAX_ADAPTERS) {
		printk(KERN_WARNING
		       "(%s) release, invalid Scsi_Host pointer.\n", ips_name);
		BUG();
		return (FALSE);
	}

	ha = IPS_HA(sh);

	if (!ha)
		return (FALSE);

	/* flush the cache on the controller */
	scb = &ha->scbs[ha->max_cmds - 1];

	ips_init_scb(ha, scb);

	scb->timeout = ips_cmd_timeout;
	scb->cdb[0] = IPS_CMD_FLUSH;

	scb->cmd.flush_cache.op_code = IPS_CMD_FLUSH;
	scb->cmd.flush_cache.command_id = IPS_COMMAND_ID(ha, scb);
	scb->cmd.flush_cache.state = IPS_NORM_STATE;
	scb->cmd.flush_cache.reserved = 0;
	scb->cmd.flush_cache.reserved2 = 0;
	scb->cmd.flush_cache.reserved3 = 0;
	scb->cmd.flush_cache.reserved4 = 0;

	IPS_PRINTK(KERN_WARNING, ha->pcidev, "Flushing Cache.\n");

	/* send command */
	if (ips_send_wait(ha, scb, ips_cmd_timeout, IPS_INTR_ON) == IPS_FAILURE)
		IPS_PRINTK(KERN_WARNING, ha->pcidev, "Incomplete Flush.\n");

	IPS_PRINTK(KERN_WARNING, ha->pcidev, "Flushing Complete.\n");

	ips_sh[i] = NULL;
	ips_ha[i] = NULL;

	/* free extra memory */
	ips_free(ha);

	/* free IRQ */
	free_irq(ha->pcidev->irq, ha);

	scsi_host_put(sh);

	ips_released_controllers++;

	return (FALSE);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_halt                                                   */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Perform cleanup when the system reboots                                */
/*                                                                          */
/****************************************************************************/
static int
ips_halt(struct notifier_block *nb, ulong event, void *buf)
{
	ips_scb_t *scb;
	ips_ha_t *ha;
	int i;

	if ((event != SYS_RESTART) && (event != SYS_HALT) &&
	    (event != SYS_POWER_OFF))
		return (NOTIFY_DONE);

	for (i = 0; i < ips_next_controller; i++) {
		ha = (ips_ha_t *) ips_ha[i];

		if (!ha)
			continue;

		if (!ha->active)
			continue;

		/* flush the cache on the controller */
		scb = &ha->scbs[ha->max_cmds - 1];

		ips_init_scb(ha, scb);

		scb->timeout = ips_cmd_timeout;
		scb->cdb[0] = IPS_CMD_FLUSH;

		scb->cmd.flush_cache.op_code = IPS_CMD_FLUSH;
		scb->cmd.flush_cache.command_id = IPS_COMMAND_ID(ha, scb);
		scb->cmd.flush_cache.state = IPS_NORM_STATE;
		scb->cmd.flush_cache.reserved = 0;
		scb->cmd.flush_cache.reserved2 = 0;
		scb->cmd.flush_cache.reserved3 = 0;
		scb->cmd.flush_cache.reserved4 = 0;

		IPS_PRINTK(KERN_WARNING, ha->pcidev, "Flushing Cache.\n");

		/* send command */
		if (ips_send_wait(ha, scb, ips_cmd_timeout, IPS_INTR_ON) ==
		    IPS_FAILURE)
			IPS_PRINTK(KERN_WARNING, ha->pcidev,
				   "Incomplete Flush.\n");
		else
			IPS_PRINTK(KERN_WARNING, ha->pcidev,
				   "Flushing Complete.\n");
	}

	return (NOTIFY_OK);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_eh_abort                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Abort a command (using the new error code stuff)                       */
/* Note: this routine is called under the io_request_lock                   */
/****************************************************************************/
int ips_eh_abort(struct scsi_cmnd *SC)
{
	ips_ha_t *ha;
	ips_copp_wait_item_t *item;
	int ret;
	struct Scsi_Host *host;

	METHOD_TRACE("ips_eh_abort", 1);

	if (!SC)
		return (FAILED);

	host = SC->device->host;
	ha = (ips_ha_t *) SC->device->host->hostdata;

	if (!ha)
		return (FAILED);

	if (!ha->active)
		return (FAILED);

	spin_lock(host->host_lock);

	/* See if the command is on the copp queue */
	item = ha->copp_waitlist.head;
	while ((item) && (item->scsi_cmd != SC))
		item = item->next;

	if (item) {
		/* Found it */
		ips_removeq_copp(&ha->copp_waitlist, item);
		ret = (SUCCESS);

		/* See if the command is on the wait queue */
	} else if (ips_removeq_wait(&ha->scb_waitlist, SC)) {
		/* command not sent yet */
		ret = (SUCCESS);
	} else {
		/* command must have already been sent */
		ret = (FAILED);
	}

	spin_unlock(host->host_lock);
	return ret;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_eh_reset                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Reset the controller (with new eh error code)                          */
/*                                                                          */
/* NOTE: this routine is called under the io_request_lock spinlock          */
/*                                                                          */
/****************************************************************************/
static int __ips_eh_reset(struct scsi_cmnd *SC)
{
	int ret;
	int i;
	ips_ha_t *ha;
	ips_scb_t *scb;
	ips_copp_wait_item_t *item;

	METHOD_TRACE("ips_eh_reset", 1);

#ifdef NO_IPS_RESET
	return (FAILED);
#else

	if (!SC) {
		DEBUG(1, "Reset called with NULL scsi command");

		return (FAILED);
	}

	ha = (ips_ha_t *) SC->device->host->hostdata;

	if (!ha) {
		DEBUG(1, "Reset called with NULL ha struct");

		return (FAILED);
	}

	if (!ha->active)
		return (FAILED);

	/* See if the command is on the copp queue */
	item = ha->copp_waitlist.head;
	while ((item) && (item->scsi_cmd != SC))
		item = item->next;

	if (item) {
		/* Found it */
		ips_removeq_copp(&ha->copp_waitlist, item);
		return (SUCCESS);
	}

	/* See if the command is on the wait queue */
	if (ips_removeq_wait(&ha->scb_waitlist, SC)) {
		/* command not sent yet */
		return (SUCCESS);
	}

	/* An explanation for the casual observer:                              */
	/* Part of the function of a RAID controller is automatic error         */
	/* detection and recovery.  As such, the only problem that physically   */
	/* resetting an adapter will ever fix is when, for some reason,         */
	/* the driver is not successfully communicating with the adapter.       */
	/* Therefore, we will attempt to flush this adapter.  If that succeeds, */
	/* then there's no real purpose in a physical reset. This will complete */
	/* much faster and avoids any problems that might be caused by a        */
	/* physical reset ( such as having to fail all the outstanding I/O's ). */

	if (ha->ioctl_reset == 0) {	/* IF Not an IOCTL Requested Reset */
		scb = &ha->scbs[ha->max_cmds - 1];

		ips_init_scb(ha, scb);

		scb->timeout = ips_cmd_timeout;
		scb->cdb[0] = IPS_CMD_FLUSH;

		scb->cmd.flush_cache.op_code = IPS_CMD_FLUSH;
		scb->cmd.flush_cache.command_id = IPS_COMMAND_ID(ha, scb);
		scb->cmd.flush_cache.state = IPS_NORM_STATE;
		scb->cmd.flush_cache.reserved = 0;
		scb->cmd.flush_cache.reserved2 = 0;
		scb->cmd.flush_cache.reserved3 = 0;
		scb->cmd.flush_cache.reserved4 = 0;

		/* Attempt the flush command */
		ret = ips_send_wait(ha, scb, ips_cmd_timeout, IPS_INTR_IORL);
		if (ret == IPS_SUCCESS) {
			IPS_PRINTK(KERN_NOTICE, ha->pcidev,
				   "Reset Request - Flushed Cache\n");
			return (SUCCESS);
		}
	}

	/* Either we can't communicate with the adapter or it's an IOCTL request */
	/* from a utility.  A physical reset is needed at this point.            */

	ha->ioctl_reset = 0;	/* Reset the IOCTL Requested Reset Flag */

	/*
	 * command must have already been sent
	 * reset the controller
	 */
	IPS_PRINTK(KERN_NOTICE, ha->pcidev, "Resetting controller.\n");
	ret = (*ha->func.reset) (ha);

	if (!ret) {
		struct scsi_cmnd *scsi_cmd;

		IPS_PRINTK(KERN_NOTICE, ha->pcidev,
			   "Controller reset failed - controller now offline.\n");

		/* Now fail all of the active commands */
		DEBUG_VAR(1, "(%s%d) Failing active commands",
			  ips_name, ha->host_num);

		while ((scb = ips_removeq_scb_head(&ha->scb_activelist))) {
			scb->scsi_cmd->result = DID_ERROR << 16;
			scb->scsi_cmd->scsi_done(scb->scsi_cmd);
			ips_freescb(ha, scb);
		}

		/* Now fail all of the pending commands */
		DEBUG_VAR(1, "(%s%d) Failing pending commands",
			  ips_name, ha->host_num);

		while ((scsi_cmd = ips_removeq_wait_head(&ha->scb_waitlist))) {
			scsi_cmd->result = DID_ERROR;
			scsi_cmd->scsi_done(scsi_cmd);
		}

		ha->active = FALSE;
		return (FAILED);
	}

	if (!ips_clear_adapter(ha, IPS_INTR_IORL)) {
		struct scsi_cmnd *scsi_cmd;

		IPS_PRINTK(KERN_NOTICE, ha->pcidev,
			   "Controller reset failed - controller now offline.\n");

		/* Now fail all of the active commands */
		DEBUG_VAR(1, "(%s%d) Failing active commands",
			  ips_name, ha->host_num);

		while ((scb = ips_removeq_scb_head(&ha->scb_activelist))) {
			scb->scsi_cmd->result = DID_ERROR << 16;
			scb->scsi_cmd->scsi_done(scb->scsi_cmd);
			ips_freescb(ha, scb);
		}

		/* Now fail all of the pending commands */
		DEBUG_VAR(1, "(%s%d) Failing pending commands",
			  ips_name, ha->host_num);

		while ((scsi_cmd = ips_removeq_wait_head(&ha->scb_waitlist))) {
			scsi_cmd->result = DID_ERROR << 16;
			scsi_cmd->scsi_done(scsi_cmd);
		}

		ha->active = FALSE;
		return (FAILED);
	}

	/* FFDC */
	if (le32_to_cpu(ha->subsys->param[3]) & 0x300000) {
		ha->last_ffdc = ktime_get_real_seconds();
		ha->reset_count++;
		ips_ffdc_reset(ha, IPS_INTR_IORL);
	}

	/* Now fail all of the active commands */
	DEBUG_VAR(1, "(%s%d) Failing active commands", ips_name, ha->host_num);

	while ((scb = ips_removeq_scb_head(&ha->scb_activelist))) {
		scb->scsi_cmd->result = DID_RESET << 16;
		scb->scsi_cmd->scsi_done(scb->scsi_cmd);
		ips_freescb(ha, scb);
	}

	/* Reset DCDB active command bits */
	for (i = 1; i < ha->nbus; i++)
		ha->dcdb_active[i - 1] = 0;

	/* Reset the number of active IOCTLs */
	ha->num_ioctl = 0;

	ips_next(ha, IPS_INTR_IORL);

	return (SUCCESS);
#endif				/* NO_IPS_RESET */

}

static int ips_eh_reset(struct scsi_cmnd *SC)
{
	int rc;

	spin_lock_irq(SC->device->host->host_lock);
	rc = __ips_eh_reset(SC);
	spin_unlock_irq(SC->device->host->host_lock);

	return rc;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_queue                                                  */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Send a command to the controller                                       */
/*                                                                          */
/* NOTE:                                                                    */
/*    Linux obtains io_request_lock before calling this function            */
/*                                                                          */
/****************************************************************************/
static int ips_queue_lck(struct scsi_cmnd *SC, void (*done) (struct scsi_cmnd *))
{
	ips_ha_t *ha;
	ips_passthru_t *pt;

	METHOD_TRACE("ips_queue", 1);

	ha = (ips_ha_t *) SC->device->host->hostdata;

	if (!ha)
		return (1);

	if (!ha->active)
		return (DID_ERROR);

	if (ips_is_passthru(SC)) {
		if (ha->copp_waitlist.count == IPS_MAX_IOCTL_QUEUE) {
			SC->result = DID_BUS_BUSY << 16;
			done(SC);

			return (0);
		}
	} else if (ha->scb_waitlist.count == IPS_MAX_QUEUE) {
		SC->result = DID_BUS_BUSY << 16;
		done(SC);

		return (0);
	}

	SC->scsi_done = done;

	DEBUG_VAR(2, "(%s%d): ips_queue: cmd 0x%X (%d %d %d)",
		  ips_name,
		  ha->host_num,
		  SC->cmnd[0],
		  SC->device->channel, SC->device->id, SC->device->lun);

	/* Check for command to initiator IDs */
	if ((scmd_channel(SC) > 0)
	    && (scmd_id(SC) == ha->ha_id[scmd_channel(SC)])) {
		SC->result = DID_NO_CONNECT << 16;
		done(SC);

		return (0);
	}

	if (ips_is_passthru(SC)) {

		ips_copp_wait_item_t *scratch;

		/* A Reset IOCTL is only sent by the boot CD in extreme cases.           */
		/* There can never be any system activity ( network or disk ), but check */
		/* anyway just as a good practice.                                       */
		pt = (ips_passthru_t *) scsi_sglist(SC);
		if ((pt->CoppCP.cmd.reset.op_code == IPS_CMD_RESET_CHANNEL) &&
		    (pt->CoppCP.cmd.reset.adapter_flag == 1)) {
			if (ha->scb_activelist.count != 0) {
				SC->result = DID_BUS_BUSY << 16;
				done(SC);
				return (0);
			}
			ha->ioctl_reset = 1;	/* This reset request is from an IOCTL */
			__ips_eh_reset(SC);
			SC->result = DID_OK << 16;
			SC->scsi_done(SC);
			return (0);
		}

		/* allocate space for the scribble */
		scratch = kmalloc(sizeof (ips_copp_wait_item_t), GFP_ATOMIC);

		if (!scratch) {
			SC->result = DID_ERROR << 16;
			done(SC);

			return (0);
		}

		scratch->scsi_cmd = SC;
		scratch->next = NULL;

		ips_putq_copp_tail(&ha->copp_waitlist, scratch);
	} else {
		ips_putq_wait_tail(&ha->scb_waitlist, SC);
	}

	ips_next(ha, IPS_INTR_IORL);

	return (0);
}

static DEF_SCSI_QCMD(ips_queue)

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_biosparam                                              */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Set bios geometry for the controller                                   */
/*                                                                          */
/****************************************************************************/
static int ips_biosparam(struct scsi_device *sdev, struct block_device *bdev,
			 sector_t capacity, int geom[])
{
	ips_ha_t *ha = (ips_ha_t *) sdev->host->hostdata;
	int heads;
	int sectors;
	int cylinders;

	METHOD_TRACE("ips_biosparam", 1);

	if (!ha)
		/* ?!?! host adater info invalid */
		return (0);

	if (!ha->active)
		return (0);

	if (!ips_read_adapter_status(ha, IPS_INTR_ON))
		/* ?!?! Enquiry command failed */
		return (0);

	if ((capacity > 0x400000) && ((ha->enq->ucMiscFlag & 0x8) == 0)) {
		heads = IPS_NORM_HEADS;
		sectors = IPS_NORM_SECTORS;
	} else {
		heads = IPS_COMP_HEADS;
		sectors = IPS_COMP_SECTORS;
	}

	cylinders = (unsigned long) capacity / (heads * sectors);

	DEBUG_VAR(2, "Geometry: heads: %d, sectors: %d, cylinders: %d",
		  heads, sectors, cylinders);

	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;

	return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_slave_configure                                        */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Set queue depths on devices once scan is complete                      */
/*                                                                          */
/****************************************************************************/
static int
ips_slave_configure(struct scsi_device * SDptr)
{
	ips_ha_t *ha;
	int min;

	ha = IPS_HA(SDptr->host);
	if (SDptr->tagged_supported && SDptr->type == TYPE_DISK) {
		min = ha->max_cmds / 2;
		if (ha->enq->ucLogDriveCount <= 2)
			min = ha->max_cmds - 1;
		scsi_change_queue_depth(SDptr, min);
	}

	SDptr->skip_ms_page_8 = 1;
	SDptr->skip_ms_page_3f = 1;
	return 0;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: do_ipsintr                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Wrapper for the interrupt handler                                      */
/*                                                                          */
/****************************************************************************/
static irqreturn_t
do_ipsintr(int irq, void *dev_id)
{
	ips_ha_t *ha;
	struct Scsi_Host *host;
	int irqstatus;

	METHOD_TRACE("do_ipsintr", 2);

	ha = (ips_ha_t *) dev_id;
	if (!ha)
		return IRQ_NONE;
	host = ips_sh[ha->host_num];
	/* interrupt during initialization */
	if (!host) {
		(*ha->func.intr) (ha);
		return IRQ_HANDLED;
	}

	spin_lock(host->host_lock);

	if (!ha->active) {
		spin_unlock(host->host_lock);
		return IRQ_HANDLED;
	}

	irqstatus = (*ha->func.intr) (ha);

	spin_unlock(host->host_lock);

	/* start the next command */
	ips_next(ha, IPS_INTR_ON);
	return IRQ_RETVAL(irqstatus);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_intr_copperhead                                        */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Polling interrupt handler                                              */
/*                                                                          */
/*   ASSUMES interrupts are disabled                                        */
/*                                                                          */
/****************************************************************************/
int
ips_intr_copperhead(ips_ha_t * ha)
{
	ips_stat_t *sp;
	ips_scb_t *scb;
	IPS_STATUS cstatus;
	int intrstatus;

	METHOD_TRACE("ips_intr", 2);

	if (!ha)
		return 0;

	if (!ha->active)
		return 0;

	intrstatus = (*ha->func.isintr) (ha);

	if (!intrstatus) {
		/*
		 * Unexpected/Shared interrupt
		 */

		return 0;
	}

	while (TRUE) {
		sp = &ha->sp;

		intrstatus = (*ha->func.isintr) (ha);

		if (!intrstatus)
			break;
		else
			cstatus.value = (*ha->func.statupd) (ha);

		if (cstatus.fields.command_id > (IPS_MAX_CMDS - 1)) {
			/* Spurious Interrupt ? */
			continue;
		}

		ips_chkstatus(ha, &cstatus);
		scb = (ips_scb_t *) sp->scb_addr;

		/*
		 * use the callback function to finish things up
		 * NOTE: interrupts are OFF for this
		 */
		(*scb->callback) (ha, scb);
	}			/* end while */
	return 1;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_intr_morpheus                                          */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Polling interrupt handler                                              */
/*                                                                          */
/*   ASSUMES interrupts are disabled                                        */
/*                                                                          */
/****************************************************************************/
int
ips_intr_morpheus(ips_ha_t * ha)
{
	ips_stat_t *sp;
	ips_scb_t *scb;
	IPS_STATUS cstatus;
	int intrstatus;

	METHOD_TRACE("ips_intr_morpheus", 2);

	if (!ha)
		return 0;

	if (!ha->active)
		return 0;

	intrstatus = (*ha->func.isintr) (ha);

	if (!intrstatus) {
		/*
		 * Unexpected/Shared interrupt
		 */

		return 0;
	}

	while (TRUE) {
		sp = &ha->sp;

		intrstatus = (*ha->func.isintr) (ha);

		if (!intrstatus)
			break;
		else
			cstatus.value = (*ha->func.statupd) (ha);

		if (cstatus.value == 0xffffffff)
			/* No more to process */
			break;

		if (cstatus.fields.command_id > (IPS_MAX_CMDS - 1)) {
			IPS_PRINTK(KERN_WARNING, ha->pcidev,
				   "Spurious interrupt; no ccb.\n");

			continue;
		}

		ips_chkstatus(ha, &cstatus);
		scb = (ips_scb_t *) sp->scb_addr;

		/*
		 * use the callback function to finish things up
		 * NOTE: interrupts are OFF for this
		 */
		(*scb->callback) (ha, scb);
	}			/* end while */
	return 1;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_info                                                   */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Return info about the driver                                           */
/*                                                                          */
/****************************************************************************/
static const char *
ips_info(struct Scsi_Host *SH)
{
	static char buffer[256];
	char *bp;
	ips_ha_t *ha;

	METHOD_TRACE("ips_info", 1);

	ha = IPS_HA(SH);

	if (!ha)
		return (NULL);

	bp = &buffer[0];
	memset(bp, 0, sizeof (buffer));

	sprintf(bp, "%s%s%s Build %d", "IBM PCI ServeRAID ",
		IPS_VERSION_HIGH, IPS_VERSION_LOW, IPS_BUILD_IDENT);

	if (ha->ad_type > 0 && ha->ad_type <= MAX_ADAPTER_NAME) {
		strcat(bp, " <");
		strcat(bp, ips_adapter_name[ha->ad_type - 1]);
		strcat(bp, ">");
	}

	return (bp);
}

static int
ips_write_info(struct Scsi_Host *host, char *buffer, int length)
{
	int i;
	ips_ha_t *ha = NULL;

	/* Find our host structure */
	for (i = 0; i < ips_next_controller; i++) {
		if (ips_sh[i]) {
			if (ips_sh[i] == host) {
				ha = (ips_ha_t *) ips_sh[i]->hostdata;
				break;
			}
		}
	}

	if (!ha)
		return (-EINVAL);

	return 0;
}

static int
ips_show_info(struct seq_file *m, struct Scsi_Host *host)
{
	int i;
	ips_ha_t *ha = NULL;

	/* Find our host structure */
	for (i = 0; i < ips_next_controller; i++) {
		if (ips_sh[i]) {
			if (ips_sh[i] == host) {
				ha = (ips_ha_t *) ips_sh[i]->hostdata;
				break;
			}
		}
	}

	if (!ha)
		return (-EINVAL);

	return ips_host_info(ha, m);
}

/*--------------------------------------------------------------------------*/
/* Helper Functions                                                         */
/*--------------------------------------------------------------------------*/

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_is_passthru                                            */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Determine if the specified SCSI command is really a passthru command   */
/*                                                                          */
/****************************************************************************/
static int ips_is_passthru(struct scsi_cmnd *SC)
{
	unsigned long flags;

	METHOD_TRACE("ips_is_passthru", 1);

	if (!SC)
		return (0);

	if ((SC->cmnd[0] == IPS_IOCTL_COMMAND) &&
	    (SC->device->channel == 0) &&
	    (SC->device->id == IPS_ADAPTER_ID) &&
	    (SC->device->lun == 0) && scsi_sglist(SC)) {
                struct scatterlist *sg = scsi_sglist(SC);
                char  *buffer;

                /* kmap_atomic() ensures addressability of the user buffer.*/
                /* local_irq_save() protects the KM_IRQ0 address slot.     */
                local_irq_save(flags);
                buffer = kmap_atomic(sg_page(sg)) + sg->offset;
                if (buffer && buffer[0] == 'C' && buffer[1] == 'O' &&
                    buffer[2] == 'P' && buffer[3] == 'P') {
                        kunmap_atomic(buffer - sg->offset);
                        local_irq_restore(flags);
                        return 1;
                }
                kunmap_atomic(buffer - sg->offset);
                local_irq_restore(flags);
	}
	return 0;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_alloc_passthru_buffer                                  */
/*                                                                          */
/* Routine Description:                                                     */
/*   allocate a buffer large enough for the ioctl data if the ioctl buffer  */
/*   is too small or doesn't exist                                          */
/****************************************************************************/
static int
ips_alloc_passthru_buffer(ips_ha_t * ha, int length)
{
	void *bigger_buf;
	dma_addr_t dma_busaddr;

	if (ha->ioctl_data && length <= ha->ioctl_len)
		return 0;
	/* there is no buffer or it's not big enough, allocate a new one */
	bigger_buf = dma_alloc_coherent(&ha->pcidev->dev, length, &dma_busaddr,
			GFP_KERNEL);
	if (bigger_buf) {
		/* free the old memory */
		dma_free_coherent(&ha->pcidev->dev, ha->ioctl_len,
				  ha->ioctl_data, ha->ioctl_busaddr);
		/* use the new memory */
		ha->ioctl_data = (char *) bigger_buf;
		ha->ioctl_len = length;
		ha->ioctl_busaddr = dma_busaddr;
	} else {
		return -1;
	}
	return 0;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_make_passthru                                          */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Make a passthru command out of the info in the Scsi block              */
/*                                                                          */
/****************************************************************************/
static int
ips_make_passthru(ips_ha_t *ha, struct scsi_cmnd *SC, ips_scb_t *scb, int intr)
{
	ips_passthru_t *pt;
	int length = 0;
	int i, ret;
        struct scatterlist *sg = scsi_sglist(SC);

	METHOD_TRACE("ips_make_passthru", 1);

        scsi_for_each_sg(SC, sg, scsi_sg_count(SC), i)
		length += sg->length;

	if (length < sizeof (ips_passthru_t)) {
		/* wrong size */
		DEBUG_VAR(1, "(%s%d) Passthru structure wrong size",
			  ips_name, ha->host_num);
		return (IPS_FAILURE);
	}
	if (ips_alloc_passthru_buffer(ha, length)) {
		/* allocation failure!  If ha->ioctl_data exists, use it to return
		   some error codes.  Return a failed command to the scsi layer. */
		if (ha->ioctl_data) {
			pt = (ips_passthru_t *) ha->ioctl_data;
			ips_scmd_buf_read(SC, pt, sizeof (ips_passthru_t));
			pt->BasicStatus = 0x0B;
			pt->ExtendedStatus = 0x00;
			ips_scmd_buf_write(SC, pt, sizeof (ips_passthru_t));
		}
		return IPS_FAILURE;
	}
	ha->ioctl_datasize = length;

	ips_scmd_buf_read(SC, ha->ioctl_data, ha->ioctl_datasize);
	pt = (ips_passthru_t *) ha->ioctl_data;

	/*
	 * Some notes about the passthru interface used
	 *
	 * IF the scsi op_code == 0x0d then we assume
	 * that the data came along with/goes with the
	 * packet we received from the sg driver. In this
	 * case the CmdBSize field of the pt structure is
	 * used for the size of the buffer.
	 */

	switch (pt->CoppCmd) {
	case IPS_NUMCTRLS:
		memcpy(ha->ioctl_data + sizeof (ips_passthru_t),
		       &ips_num_controllers, sizeof (int));
		ips_scmd_buf_write(SC, ha->ioctl_data,
				   sizeof (ips_passthru_t) + sizeof (int));
		SC->result = DID_OK << 16;

		return (IPS_SUCCESS_IMM);

	case IPS_COPPUSRCMD:
	case IPS_COPPIOCCMD:
		if (SC->cmnd[0] == IPS_IOCTL_COMMAND) {
			if (length < (sizeof (ips_passthru_t) + pt->CmdBSize)) {
				/* wrong size */
				DEBUG_VAR(1,
					  "(%s%d) Passthru structure wrong size",
					  ips_name, ha->host_num);

				return (IPS_FAILURE);
			}

			if (ha->pcidev->device == IPS_DEVICEID_COPPERHEAD &&
			    pt->CoppCP.cmd.flashfw.op_code ==
			    IPS_CMD_RW_BIOSFW) {
				ret = ips_flash_copperhead(ha, pt, scb);
				ips_scmd_buf_write(SC, ha->ioctl_data,
						   sizeof (ips_passthru_t));
				return ret;
			}
			if (ips_usrcmd(ha, pt, scb))
				return (IPS_SUCCESS);
			else
				return (IPS_FAILURE);
		}

		break;

	}			/* end switch */

	return (IPS_FAILURE);
}

/****************************************************************************/
/* Routine Name: ips_flash_copperhead                                       */
/* Routine Description:                                                     */
/*   Flash the BIOS/FW on a Copperhead style controller                     */
/****************************************************************************/
static int
ips_flash_copperhead(ips_ha_t * ha, ips_passthru_t * pt, ips_scb_t * scb)
{
	int datasize;

	/* Trombone is the only copperhead that can do packet flash, but only
	 * for firmware. No one said it had to make sense. */
	if (IPS_IS_TROMBONE(ha) && pt->CoppCP.cmd.flashfw.type == IPS_FW_IMAGE) {
		if (ips_usrcmd(ha, pt, scb))
			return IPS_SUCCESS;
		else
			return IPS_FAILURE;
	}
	pt->BasicStatus = 0x0B;
	pt->ExtendedStatus = 0;
	scb->scsi_cmd->result = DID_OK << 16;
	/* IF it's OK to Use the "CD BOOT" Flash Buffer, then you can     */
	/* avoid allocating a huge buffer per adapter ( which can fail ). */
	if (pt->CoppCP.cmd.flashfw.type == IPS_BIOS_IMAGE &&
	    pt->CoppCP.cmd.flashfw.direction == IPS_ERASE_BIOS) {
		pt->BasicStatus = 0;
		return ips_flash_bios(ha, pt, scb);
	} else if (pt->CoppCP.cmd.flashfw.packet_num == 0) {
		if (ips_FlashData && !test_and_set_bit(0, &ips_FlashDataInUse)){
			ha->flash_data = ips_FlashData;
			ha->flash_busaddr = ips_flashbusaddr;
			ha->flash_len = PAGE_SIZE << 7;
			ha->flash_datasize = 0;
		} else if (!ha->flash_data) {
			datasize = pt->CoppCP.cmd.flashfw.total_packets *
			    pt->CoppCP.cmd.flashfw.count;
			ha->flash_data = dma_alloc_coherent(&ha->pcidev->dev,
					datasize, &ha->flash_busaddr, GFP_KERNEL);
			if (!ha->flash_data){
				printk(KERN_WARNING "Unable to allocate a flash buffer\n");
				return IPS_FAILURE;
			}
			ha->flash_datasize = 0;
			ha->flash_len = datasize;
		} else
			return IPS_FAILURE;
	} else {
		if (pt->CoppCP.cmd.flashfw.count + ha->flash_datasize >
		    ha->flash_len) {
			ips_free_flash_copperhead(ha);
			IPS_PRINTK(KERN_WARNING, ha->pcidev,
				   "failed size sanity check\n");
			return IPS_FAILURE;
		}
	}
	if (!ha->flash_data)
		return IPS_FAILURE;
	pt->BasicStatus = 0;
	memcpy(&ha->flash_data[ha->flash_datasize], pt + 1,
	       pt->CoppCP.cmd.flashfw.count);
	ha->flash_datasize += pt->CoppCP.cmd.flashfw.count;
	if (pt->CoppCP.cmd.flashfw.packet_num ==
	    pt->CoppCP.cmd.flashfw.total_packets - 1) {
		if (pt->CoppCP.cmd.flashfw.type == IPS_BIOS_IMAGE)
			return ips_flash_bios(ha, pt, scb);
		else if (pt->CoppCP.cmd.flashfw.type == IPS_FW_IMAGE)
			return ips_flash_firmware(ha, pt, scb);
	}
	return IPS_SUCCESS_IMM;
}

/****************************************************************************/
/* Routine Name: ips_flash_bios                                             */
/* Routine Description:                                                     */
/*   flashes the bios of a copperhead adapter                               */
/****************************************************************************/
static int
ips_flash_bios(ips_ha_t * ha, ips_passthru_t * pt, ips_scb_t * scb)
{

	if (pt->CoppCP.cmd.flashfw.type == IPS_BIOS_IMAGE &&
	    pt->CoppCP.cmd.flashfw.direction == IPS_WRITE_BIOS) {
		if ((!ha->func.programbios) || (!ha->func.erasebios) ||
		    (!ha->func.verifybios))
			goto error;
		if ((*ha->func.erasebios) (ha)) {
			DEBUG_VAR(1,
				  "(%s%d) flash bios failed - unable to erase flash",
				  ips_name, ha->host_num);
			goto error;
		} else
		    if ((*ha->func.programbios) (ha,
						 ha->flash_data +
						 IPS_BIOS_HEADER,
						 ha->flash_datasize -
						 IPS_BIOS_HEADER, 0)) {
			DEBUG_VAR(1,
				  "(%s%d) flash bios failed - unable to flash",
				  ips_name, ha->host_num);
			goto error;
		} else
		    if ((*ha->func.verifybios) (ha,
						ha->flash_data +
						IPS_BIOS_HEADER,
						ha->flash_datasize -
						IPS_BIOS_HEADER, 0)) {
			DEBUG_VAR(1,
				  "(%s%d) flash bios failed - unable to verify flash",
				  ips_name, ha->host_num);
			goto error;
		}
		ips_free_flash_copperhead(ha);
		return IPS_SUCCESS_IMM;
	} else if (pt->CoppCP.cmd.flashfw.type == IPS_BIOS_IMAGE &&
		   pt->CoppCP.cmd.flashfw.direction == IPS_ERASE_BIOS) {
		if (!ha->func.erasebios)
			goto error;
		if ((*ha->func.erasebios) (ha)) {
			DEBUG_VAR(1,
				  "(%s%d) flash bios failed - unable to erase flash",
				  ips_name, ha->host_num);
			goto error;
		}
		return IPS_SUCCESS_IMM;
	}
      error:
	pt->BasicStatus = 0x0B;
	pt->ExtendedStatus = 0x00;
	ips_free_flash_copperhead(ha);
	return IPS_FAILURE;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_fill_scb_sg_single                                     */
/*                                                                          */
/* Routine Description:                                                     */
/*   Fill in a single scb sg_list element from an address                   */
/*   return a -1 if a breakup occurred                                      */
/****************************************************************************/
static int
ips_fill_scb_sg_single(ips_ha_t * ha, dma_addr_t busaddr,
		       ips_scb_t * scb, int indx, unsigned int e_len)
{

	int ret_val = 0;

	if ((scb->data_len + e_len) > ha->max_xfer) {
		e_len = ha->max_xfer - scb->data_len;
		scb->breakup = indx;
		++scb->sg_break;
		ret_val = -1;
	} else {
		scb->breakup = 0;
		scb->sg_break = 0;
	}
	if (IPS_USE_ENH_SGLIST(ha)) {
		scb->sg_list.enh_list[indx].address_lo =
		    cpu_to_le32(lower_32_bits(busaddr));
		scb->sg_list.enh_list[indx].address_hi =
		    cpu_to_le32(upper_32_bits(busaddr));
		scb->sg_list.enh_list[indx].length = cpu_to_le32(e_len);
	} else {
		scb->sg_list.std_list[indx].address =
		    cpu_to_le32(lower_32_bits(busaddr));
		scb->sg_list.std_list[indx].length = cpu_to_le32(e_len);
	}

	++scb->sg_len;
	scb->data_len += e_len;
	return ret_val;
}

/****************************************************************************/
/* Routine Name: ips_flash_firmware                                         */
/* Routine Description:                                                     */
/*   flashes the firmware of a copperhead adapter                           */
/****************************************************************************/
static int
ips_flash_firmware(ips_ha_t * ha, ips_passthru_t * pt, ips_scb_t * scb)
{
	IPS_SG_LIST sg_list;
	uint32_t cmd_busaddr;

	if (pt->CoppCP.cmd.flashfw.type == IPS_FW_IMAGE &&
	    pt->CoppCP.cmd.flashfw.direction == IPS_WRITE_FW) {
		memset(&pt->CoppCP.cmd, 0, sizeof (IPS_HOST_COMMAND));
		pt->CoppCP.cmd.flashfw.op_code = IPS_CMD_DOWNLOAD;
		pt->CoppCP.cmd.flashfw.count = cpu_to_le32(ha->flash_datasize);
	} else {
		pt->BasicStatus = 0x0B;
		pt->ExtendedStatus = 0x00;
		ips_free_flash_copperhead(ha);
		return IPS_FAILURE;
	}
	/* Save the S/G list pointer so it doesn't get clobbered */
	sg_list.list = scb->sg_list.list;
	cmd_busaddr = scb->scb_busaddr;
	/* copy in the CP */
	memcpy(&scb->cmd, &pt->CoppCP.cmd, sizeof (IPS_IOCTL_CMD));
	/* FIX stuff that might be wrong */
	scb->sg_list.list = sg_list.list;
	scb->scb_busaddr = cmd_busaddr;
	scb->bus = scb->scsi_cmd->device->channel;
	scb->target_id = scb->scsi_cmd->device->id;
	scb->lun = scb->scsi_cmd->device->lun;
	scb->sg_len = 0;
	scb->data_len = 0;
	scb->flags = 0;
	scb->op_code = 0;
	scb->callback = ipsintr_done;
	scb->timeout = ips_cmd_timeout;

	scb->data_len = ha->flash_datasize;
	scb->data_busaddr =
	    dma_map_single(&ha->pcidev->dev, ha->flash_data, scb->data_len,
			   IPS_DMA_DIR(scb));
	scb->flags |= IPS_SCB_MAP_SINGLE;
	scb->cmd.flashfw.command_id = IPS_COMMAND_ID(ha, scb);
	scb->cmd.flashfw.buffer_addr = cpu_to_le32(scb->data_busaddr);
	if (pt->TimeOut)
		scb->timeout = pt->TimeOut;
	scb->scsi_cmd->result = DID_OK << 16;
	return IPS_SUCCESS;
}

/****************************************************************************/
/* Routine Name: ips_free_flash_copperhead                                  */
/* Routine Description:                                                     */
/*   release the memory resources used to hold the flash image              */
/****************************************************************************/
static void
ips_free_flash_copperhead(ips_ha_t * ha)
{
	if (ha->flash_data == ips_FlashData)
		test_and_clear_bit(0, &ips_FlashDataInUse);
	else if (ha->flash_data)
		dma_free_coherent(&ha->pcidev->dev, ha->flash_len,
				  ha->flash_data, ha->flash_busaddr);
	ha->flash_data = NULL;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_usrcmd                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Process a user command and make it ready to send                       */
/*                                                                          */
/****************************************************************************/
static int
ips_usrcmd(ips_ha_t * ha, ips_passthru_t * pt, ips_scb_t * scb)
{
	IPS_SG_LIST sg_list;
	uint32_t cmd_busaddr;

	METHOD_TRACE("ips_usrcmd", 1);

	if ((!scb) || (!pt) || (!ha))
		return (0);

	/* Save the S/G list pointer so it doesn't get clobbered */
	sg_list.list = scb->sg_list.list;
	cmd_busaddr = scb->scb_busaddr;
	/* copy in the CP */
	memcpy(&scb->cmd, &pt->CoppCP.cmd, sizeof (IPS_IOCTL_CMD));
	memcpy(&scb->dcdb, &pt->CoppCP.dcdb, sizeof (IPS_DCDB_TABLE));

	/* FIX stuff that might be wrong */
	scb->sg_list.list = sg_list.list;
	scb->scb_busaddr = cmd_busaddr;
	scb->bus = scb->scsi_cmd->device->channel;
	scb->target_id = scb->scsi_cmd->device->id;
	scb->lun = scb->scsi_cmd->device->lun;
	scb->sg_len = 0;
	scb->data_len = 0;
	scb->flags = 0;
	scb->op_code = 0;
	scb->callback = ipsintr_done;
	scb->timeout = ips_cmd_timeout;
	scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);

	/* we don't support DCDB/READ/WRITE Scatter Gather */
	if ((scb->cmd.basic_io.op_code == IPS_CMD_READ_SG) ||
	    (scb->cmd.basic_io.op_code == IPS_CMD_WRITE_SG) ||
	    (scb->cmd.basic_io.op_code == IPS_CMD_DCDB_SG))
		return (0);

	if (pt->CmdBSize) {
		scb->data_len = pt->CmdBSize;
		scb->data_busaddr = ha->ioctl_busaddr + sizeof (ips_passthru_t);
	} else {
		scb->data_busaddr = 0L;
	}

	if (scb->cmd.dcdb.op_code == IPS_CMD_DCDB)
		scb->cmd.dcdb.dcdb_address = cpu_to_le32(scb->scb_busaddr +
							 (unsigned long) &scb->
							 dcdb -
							 (unsigned long) scb);

	if (pt->CmdBSize) {
		if (scb->cmd.dcdb.op_code == IPS_CMD_DCDB)
			scb->dcdb.buffer_pointer =
			    cpu_to_le32(scb->data_busaddr);
		else
			scb->cmd.basic_io.sg_addr =
			    cpu_to_le32(scb->data_busaddr);
	}

	/* set timeouts */
	if (pt->TimeOut) {
		scb->timeout = pt->TimeOut;

		if (pt->TimeOut <= 10)
			scb->dcdb.cmd_attribute |= IPS_TIMEOUT10;
		else if (pt->TimeOut <= 60)
			scb->dcdb.cmd_attribute |= IPS_TIMEOUT60;
		else
			scb->dcdb.cmd_attribute |= IPS_TIMEOUT20M;
	}

	/* assume success */
	scb->scsi_cmd->result = DID_OK << 16;

	/* success */
	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_cleanup_passthru                                       */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Cleanup after a passthru command                                       */
/*                                                                          */
/****************************************************************************/
static void
ips_cleanup_passthru(ips_ha_t * ha, ips_scb_t * scb)
{
	ips_passthru_t *pt;

	METHOD_TRACE("ips_cleanup_passthru", 1);

	if ((!scb) || (!scb->scsi_cmd) || (!scsi_sglist(scb->scsi_cmd))) {
		DEBUG_VAR(1, "(%s%d) couldn't cleanup after passthru",
			  ips_name, ha->host_num);

		return;
	}
	pt = (ips_passthru_t *) ha->ioctl_data;

	/* Copy data back to the user */
	if (scb->cmd.dcdb.op_code == IPS_CMD_DCDB)	/* Copy DCDB Back to Caller's Area */
		memcpy(&pt->CoppCP.dcdb, &scb->dcdb, sizeof (IPS_DCDB_TABLE));

	pt->BasicStatus = scb->basic_status;
	pt->ExtendedStatus = scb->extended_status;
	pt->AdapterType = ha->ad_type;

	if (ha->pcidev->device == IPS_DEVICEID_COPPERHEAD &&
	    (scb->cmd.flashfw.op_code == IPS_CMD_DOWNLOAD ||
	     scb->cmd.flashfw.op_code == IPS_CMD_RW_BIOSFW))
		ips_free_flash_copperhead(ha);

	ips_scmd_buf_write(scb->scsi_cmd, ha->ioctl_data, ha->ioctl_datasize);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_host_info                                              */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   The passthru interface for the driver                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_host_info(ips_ha_t *ha, struct seq_file *m)
{
	METHOD_TRACE("ips_host_info", 1);

	seq_puts(m, "\nIBM ServeRAID General Information:\n\n");

	if ((le32_to_cpu(ha->nvram->signature) == IPS_NVRAM_P5_SIG) &&
	    (le16_to_cpu(ha->nvram->adapter_type) != 0))
		seq_printf(m, "\tController Type                   : %s\n",
			  ips_adapter_name[ha->ad_type - 1]);
	else
		seq_puts(m, "\tController Type                   : Unknown\n");

	if (ha->io_addr)
		seq_printf(m,
			  "\tIO region                         : 0x%x (%d bytes)\n",
			  ha->io_addr, ha->io_len);

	if (ha->mem_addr) {
		seq_printf(m,
			  "\tMemory region                     : 0x%x (%d bytes)\n",
			  ha->mem_addr, ha->mem_len);
		seq_printf(m,
			  "\tShared memory address             : 0x%lx\n",
			  (unsigned long)ha->mem_ptr);
	}

	seq_printf(m, "\tIRQ number                        : %d\n", ha->pcidev->irq);

    /* For the Next 3 lines Check for Binary 0 at the end and don't include it if it's there. */
    /* That keeps everything happy for "text" operations on the proc file.                    */

	if (le32_to_cpu(ha->nvram->signature) == IPS_NVRAM_P5_SIG) {
	if (ha->nvram->bios_low[3] == 0) {
		seq_printf(m,
			  "\tBIOS Version                      : %c%c%c%c%c%c%c\n",
			  ha->nvram->bios_high[0], ha->nvram->bios_high[1],
			  ha->nvram->bios_high[2], ha->nvram->bios_high[3],
			  ha->nvram->bios_low[0], ha->nvram->bios_low[1],
			  ha->nvram->bios_low[2]);

        } else {
		seq_printf(m,
			  "\tBIOS Version                      : %c%c%c%c%c%c%c%c\n",
			  ha->nvram->bios_high[0], ha->nvram->bios_high[1],
			  ha->nvram->bios_high[2], ha->nvram->bios_high[3],
			  ha->nvram->bios_low[0], ha->nvram->bios_low[1],
			  ha->nvram->bios_low[2], ha->nvram->bios_low[3]);
        }

    }

    if (ha->enq->CodeBlkVersion[7] == 0) {
        seq_printf(m,
		  "\tFirmware Version                  : %c%c%c%c%c%c%c\n",
		  ha->enq->CodeBlkVersion[0], ha->enq->CodeBlkVersion[1],
		  ha->enq->CodeBlkVersion[2], ha->enq->CodeBlkVersion[3],
		  ha->enq->CodeBlkVersion[4], ha->enq->CodeBlkVersion[5],
		  ha->enq->CodeBlkVersion[6]);
    } else {
	seq_printf(m,
		  "\tFirmware Version                  : %c%c%c%c%c%c%c%c\n",
		  ha->enq->CodeBlkVersion[0], ha->enq->CodeBlkVersion[1],
		  ha->enq->CodeBlkVersion[2], ha->enq->CodeBlkVersion[3],
		  ha->enq->CodeBlkVersion[4], ha->enq->CodeBlkVersion[5],
		  ha->enq->CodeBlkVersion[6], ha->enq->CodeBlkVersion[7]);
    }

    if (ha->enq->BootBlkVersion[7] == 0) {
        seq_printf(m,
		  "\tBoot Block Version                : %c%c%c%c%c%c%c\n",
		  ha->enq->BootBlkVersion[0], ha->enq->BootBlkVersion[1],
		  ha->enq->BootBlkVersion[2], ha->enq->BootBlkVersion[3],
		  ha->enq->BootBlkVersion[4], ha->enq->BootBlkVersion[5],
		  ha->enq->BootBlkVersion[6]);
    } else {
        seq_printf(m,
		  "\tBoot Block Version                : %c%c%c%c%c%c%c%c\n",
		  ha->enq->BootBlkVersion[0], ha->enq->BootBlkVersion[1],
		  ha->enq->BootBlkVersion[2], ha->enq->BootBlkVersion[3],
		  ha->enq->BootBlkVersion[4], ha->enq->BootBlkVersion[5],
		  ha->enq->BootBlkVersion[6], ha->enq->BootBlkVersion[7]);
    }

	seq_printf(m, "\tDriver Version                    : %s%s\n",
		  IPS_VERSION_HIGH, IPS_VERSION_LOW);

	seq_printf(m, "\tDriver Build                      : %d\n",
		  IPS_BUILD_IDENT);

	seq_printf(m, "\tMax Physical Devices              : %d\n",
		  ha->enq->ucMaxPhysicalDevices);
	seq_printf(m, "\tMax Active Commands               : %d\n",
		  ha->max_cmds);
	seq_printf(m, "\tCurrent Queued Commands           : %d\n",
		  ha->scb_waitlist.count);
	seq_printf(m, "\tCurrent Active Commands           : %d\n",
		  ha->scb_activelist.count - ha->num_ioctl);
	seq_printf(m, "\tCurrent Queued PT Commands        : %d\n",
		  ha->copp_waitlist.count);
	seq_printf(m, "\tCurrent Active PT Commands        : %d\n",
		  ha->num_ioctl);

	seq_putc(m, '\n');

	return 0;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_identify_controller                                    */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Identify this controller                                               */
/*                                                                          */
/****************************************************************************/
static void
ips_identify_controller(ips_ha_t * ha)
{
	METHOD_TRACE("ips_identify_controller", 1);

	switch (ha->pcidev->device) {
	case IPS_DEVICEID_COPPERHEAD:
		if (ha->pcidev->revision <= IPS_REVID_SERVERAID) {
			ha->ad_type = IPS_ADTYPE_SERVERAID;
		} else if (ha->pcidev->revision == IPS_REVID_SERVERAID2) {
			ha->ad_type = IPS_ADTYPE_SERVERAID2;
		} else if (ha->pcidev->revision == IPS_REVID_NAVAJO) {
			ha->ad_type = IPS_ADTYPE_NAVAJO;
		} else if ((ha->pcidev->revision == IPS_REVID_SERVERAID2)
			   && (ha->slot_num == 0)) {
			ha->ad_type = IPS_ADTYPE_KIOWA;
		} else if ((ha->pcidev->revision >= IPS_REVID_CLARINETP1) &&
			   (ha->pcidev->revision <= IPS_REVID_CLARINETP3)) {
			if (ha->enq->ucMaxPhysicalDevices == 15)
				ha->ad_type = IPS_ADTYPE_SERVERAID3L;
			else
				ha->ad_type = IPS_ADTYPE_SERVERAID3;
		} else if ((ha->pcidev->revision >= IPS_REVID_TROMBONE32) &&
			   (ha->pcidev->revision <= IPS_REVID_TROMBONE64)) {
			ha->ad_type = IPS_ADTYPE_SERVERAID4H;
		}
		break;

	case IPS_DEVICEID_MORPHEUS:
		switch (ha->pcidev->subsystem_device) {
		case IPS_SUBDEVICEID_4L:
			ha->ad_type = IPS_ADTYPE_SERVERAID4L;
			break;

		case IPS_SUBDEVICEID_4M:
			ha->ad_type = IPS_ADTYPE_SERVERAID4M;
			break;

		case IPS_SUBDEVICEID_4MX:
			ha->ad_type = IPS_ADTYPE_SERVERAID4MX;
			break;

		case IPS_SUBDEVICEID_4LX:
			ha->ad_type = IPS_ADTYPE_SERVERAID4LX;
			break;

		case IPS_SUBDEVICEID_5I2:
			ha->ad_type = IPS_ADTYPE_SERVERAID5I2;
			break;

		case IPS_SUBDEVICEID_5I1:
			ha->ad_type = IPS_ADTYPE_SERVERAID5I1;
			break;
		}

		break;

	case IPS_DEVICEID_MARCO:
		switch (ha->pcidev->subsystem_device) {
		case IPS_SUBDEVICEID_6M:
			ha->ad_type = IPS_ADTYPE_SERVERAID6M;
			break;
		case IPS_SUBDEVICEID_6I:
			ha->ad_type = IPS_ADTYPE_SERVERAID6I;
			break;
		case IPS_SUBDEVICEID_7k:
			ha->ad_type = IPS_ADTYPE_SERVERAID7k;
			break;
		case IPS_SUBDEVICEID_7M:
			ha->ad_type = IPS_ADTYPE_SERVERAID7M;
			break;
		}
		break;
	}
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_get_bios_version                                       */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Get the BIOS revision number                                           */
/*                                                                          */
/****************************************************************************/
static void
ips_get_bios_version(ips_ha_t * ha, int intr)
{
	ips_scb_t *scb;
	int ret;
	uint8_t major;
	uint8_t minor;
	uint8_t subminor;
	uint8_t *buffer;

	METHOD_TRACE("ips_get_bios_version", 1);

	major = 0;
	minor = 0;

	strncpy(ha->bios_version, "       ?", 8);

	if (ha->pcidev->device == IPS_DEVICEID_COPPERHEAD) {
		if (IPS_USE_MEMIO(ha)) {
			/* Memory Mapped I/O */

			/* test 1st byte */
			writel(0, ha->mem_ptr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			if (readb(ha->mem_ptr + IPS_REG_FLDP) != 0x55)
				return;

			writel(1, ha->mem_ptr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			if (readb(ha->mem_ptr + IPS_REG_FLDP) != 0xAA)
				return;

			/* Get Major version */
			writel(0x1FF, ha->mem_ptr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			major = readb(ha->mem_ptr + IPS_REG_FLDP);

			/* Get Minor version */
			writel(0x1FE, ha->mem_ptr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */
			minor = readb(ha->mem_ptr + IPS_REG_FLDP);

			/* Get SubMinor version */
			writel(0x1FD, ha->mem_ptr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */
			subminor = readb(ha->mem_ptr + IPS_REG_FLDP);

		} else {
			/* Programmed I/O */

			/* test 1st byte */
			outl(0, ha->io_addr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			if (inb(ha->io_addr + IPS_REG_FLDP) != 0x55)
				return;

			outl(1, ha->io_addr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			if (inb(ha->io_addr + IPS_REG_FLDP) != 0xAA)
				return;

			/* Get Major version */
			outl(0x1FF, ha->io_addr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			major = inb(ha->io_addr + IPS_REG_FLDP);

			/* Get Minor version */
			outl(0x1FE, ha->io_addr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			minor = inb(ha->io_addr + IPS_REG_FLDP);

			/* Get SubMinor version */
			outl(0x1FD, ha->io_addr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			subminor = inb(ha->io_addr + IPS_REG_FLDP);

		}
	} else {
		/* Morpheus Family - Send Command to the card */

		buffer = ha->ioctl_data;

		memset(buffer, 0, 0x1000);

		scb = &ha->scbs[ha->max_cmds - 1];

		ips_init_scb(ha, scb);

		scb->timeout = ips_cmd_timeout;
		scb->cdb[0] = IPS_CMD_RW_BIOSFW;

		scb->cmd.flashfw.op_code = IPS_CMD_RW_BIOSFW;
		scb->cmd.flashfw.command_id = IPS_COMMAND_ID(ha, scb);
		scb->cmd.flashfw.type = 1;
		scb->cmd.flashfw.direction = 0;
		scb->cmd.flashfw.count = cpu_to_le32(0x800);
		scb->cmd.flashfw.total_packets = 1;
		scb->cmd.flashfw.packet_num = 0;
		scb->data_len = 0x1000;
		scb->cmd.flashfw.buffer_addr = ha->ioctl_busaddr;

		/* issue the command */
		if (((ret =
		      ips_send_wait(ha, scb, ips_cmd_timeout,
				    intr)) == IPS_FAILURE)
		    || (ret == IPS_SUCCESS_IMM)
		    || ((scb->basic_status & IPS_GSC_STATUS_MASK) > 1)) {
			/* Error occurred */

			return;
		}

		if ((buffer[0xC0] == 0x55) && (buffer[0xC1] == 0xAA)) {
			major = buffer[0x1ff + 0xC0];	/* Offset 0x1ff after the header (0xc0) */
			minor = buffer[0x1fe + 0xC0];	/* Offset 0x1fe after the header (0xc0) */
			subminor = buffer[0x1fd + 0xC0];	/* Offset 0x1fd after the header (0xc0) */
		} else {
			return;
		}
	}

	ha->bios_version[0] = hex_asc_upper_hi(major);
	ha->bios_version[1] = '.';
	ha->bios_version[2] = hex_asc_upper_lo(major);
	ha->bios_version[3] = hex_asc_upper_lo(subminor);
	ha->bios_version[4] = '.';
	ha->bios_version[5] = hex_asc_upper_hi(minor);
	ha->bios_version[6] = hex_asc_upper_lo(minor);
	ha->bios_version[7] = 0;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_hainit                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Initialize the controller                                              */
/*                                                                          */
/* NOTE: Assumes to be called from with a lock                              */
/*                                                                          */
/****************************************************************************/
static int
ips_hainit(ips_ha_t * ha)
{
	int i;

	METHOD_TRACE("ips_hainit", 1);

	if (!ha)
		return (0);

	if (ha->func.statinit)
		(*ha->func.statinit) (ha);

	if (ha->func.enableint)
		(*ha->func.enableint) (ha);

	/* Send FFDC */
	ha->reset_count = 1;
	ha->last_ffdc = ktime_get_real_seconds();
	ips_ffdc_reset(ha, IPS_INTR_IORL);

	if (!ips_read_config(ha, IPS_INTR_IORL)) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "unable to read config from controller.\n");

		return (0);
	}
	/* end if */
	if (!ips_read_adapter_status(ha, IPS_INTR_IORL)) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "unable to read controller status.\n");

		return (0);
	}

	/* Identify this controller */
	ips_identify_controller(ha);

	if (!ips_read_subsystem_parameters(ha, IPS_INTR_IORL)) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "unable to read subsystem parameters.\n");

		return (0);
	}

	/* write nvram user page 5 */
	if (!ips_write_driver_status(ha, IPS_INTR_IORL)) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "unable to write driver info to controller.\n");

		return (0);
	}

	/* If there are Logical Drives and a Reset Occurred, then an EraseStripeLock is Needed */
	if ((ha->conf->ucLogDriveCount > 0) && (ha->requires_esl == 1))
		ips_clear_adapter(ha, IPS_INTR_IORL);

	/* set limits on SID, LUN, BUS */
	ha->ntargets = IPS_MAX_TARGETS + 1;
	ha->nlun = 1;
	ha->nbus = (ha->enq->ucMaxPhysicalDevices / IPS_MAX_TARGETS) + 1;

	switch (ha->conf->logical_drive[0].ucStripeSize) {
	case 4:
		ha->max_xfer = 0x10000;
		break;

	case 5:
		ha->max_xfer = 0x20000;
		break;

	case 6:
		ha->max_xfer = 0x40000;
		break;

	case 7:
	default:
		ha->max_xfer = 0x80000;
		break;
	}

	/* setup max concurrent commands */
	if (le32_to_cpu(ha->subsys->param[4]) & 0x1) {
		/* Use the new method */
		ha->max_cmds = ha->enq->ucConcurrentCmdCount;
	} else {
		/* use the old method */
		switch (ha->conf->logical_drive[0].ucStripeSize) {
		case 4:
			ha->max_cmds = 32;
			break;

		case 5:
			ha->max_cmds = 16;
			break;

		case 6:
			ha->max_cmds = 8;
			break;

		case 7:
		default:
			ha->max_cmds = 4;
			break;
		}
	}

	/* Limit the Active Commands on a Lite Adapter */
	if ((ha->ad_type == IPS_ADTYPE_SERVERAID3L) ||
	    (ha->ad_type == IPS_ADTYPE_SERVERAID4L) ||
	    (ha->ad_type == IPS_ADTYPE_SERVERAID4LX)) {
		if ((ha->max_cmds > MaxLiteCmds) && (MaxLiteCmds))
			ha->max_cmds = MaxLiteCmds;
	}

	/* set controller IDs */
	ha->ha_id[0] = IPS_ADAPTER_ID;
	for (i = 1; i < ha->nbus; i++) {
		ha->ha_id[i] = ha->conf->init_id[i - 1] & 0x1f;
		ha->dcdb_active[i - 1] = 0;
	}

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_next                                                   */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Take the next command off the queue and send it to the controller      */
/*                                                                          */
/****************************************************************************/
static void
ips_next(ips_ha_t * ha, int intr)
{
	ips_scb_t *scb;
	struct scsi_cmnd *SC;
	struct scsi_cmnd *p;
	struct scsi_cmnd *q;
	ips_copp_wait_item_t *item;
	int ret;
	struct Scsi_Host *host;
	METHOD_TRACE("ips_next", 1);

	if (!ha)
		return;
	host = ips_sh[ha->host_num];
	/*
	 * Block access to the queue function so
	 * this command won't time out
	 */
	if (intr == IPS_INTR_ON)
		spin_lock(host->host_lock);

	if ((ha->subsys->param[3] & 0x300000)
	    && (ha->scb_activelist.count == 0)) {
		time64_t now = ktime_get_real_seconds();
		if (now - ha->last_ffdc > IPS_SECS_8HOURS) {
			ha->last_ffdc = now;
			ips_ffdc_time(ha);
		}
	}

	/*
	 * Send passthru commands
	 * These have priority over normal I/O
	 * but shouldn't affect performance too much
	 * since we limit the number that can be active
	 * on the card at any one time
	 */
	while ((ha->num_ioctl < IPS_MAX_IOCTL) &&
	       (ha->copp_waitlist.head) && (scb = ips_getscb(ha))) {

		item = ips_removeq_copp_head(&ha->copp_waitlist);
		ha->num_ioctl++;
		if (intr == IPS_INTR_ON)
			spin_unlock(host->host_lock);
		scb->scsi_cmd = item->scsi_cmd;
		kfree(item);

		ret = ips_make_passthru(ha, scb->scsi_cmd, scb, intr);

		if (intr == IPS_INTR_ON)
			spin_lock(host->host_lock);
		switch (ret) {
		case IPS_FAILURE:
			if (scb->scsi_cmd) {
				scb->scsi_cmd->result = DID_ERROR << 16;
				scb->scsi_cmd->scsi_done(scb->scsi_cmd);
			}

			ips_freescb(ha, scb);
			break;
		case IPS_SUCCESS_IMM:
			if (scb->scsi_cmd) {
				scb->scsi_cmd->result = DID_OK << 16;
				scb->scsi_cmd->scsi_done(scb->scsi_cmd);
			}

			ips_freescb(ha, scb);
			break;
		default:
			break;
		}		/* end case */

		if (ret != IPS_SUCCESS) {
			ha->num_ioctl--;
			continue;
		}

		ret = ips_send_cmd(ha, scb);

		if (ret == IPS_SUCCESS)
			ips_putq_scb_head(&ha->scb_activelist, scb);
		else
			ha->num_ioctl--;

		switch (ret) {
		case IPS_FAILURE:
			if (scb->scsi_cmd) {
				scb->scsi_cmd->result = DID_ERROR << 16;
			}

			ips_freescb(ha, scb);
			break;
		case IPS_SUCCESS_IMM:
			ips_freescb(ha, scb);
			break;
		default:
			break;
		}		/* end case */

	}

	/*
	 * Send "Normal" I/O commands
	 */

	p = ha->scb_waitlist.head;
	while ((p) && (scb = ips_getscb(ha))) {
		if ((scmd_channel(p) > 0)
		    && (ha->
			dcdb_active[scmd_channel(p) -
				    1] & (1 << scmd_id(p)))) {
			ips_freescb(ha, scb);
			p = (struct scsi_cmnd *) p->host_scribble;
			continue;
		}

		q = p;
		SC = ips_removeq_wait(&ha->scb_waitlist, q);

		if (intr == IPS_INTR_ON)
			spin_unlock(host->host_lock);	/* Unlock HA after command is taken off queue */

		SC->result = DID_OK;
		SC->host_scribble = NULL;

		scb->target_id = SC->device->id;
		scb->lun = SC->device->lun;
		scb->bus = SC->device->channel;
		scb->scsi_cmd = SC;
		scb->breakup = 0;
		scb->data_len = 0;
		scb->callback = ipsintr_done;
		scb->timeout = ips_cmd_timeout;
		memset(&scb->cmd, 0, 16);

		/* copy in the CDB */
		memcpy(scb->cdb, SC->cmnd, SC->cmd_len);

                scb->sg_count = scsi_dma_map(SC);
                BUG_ON(scb->sg_count < 0);
		if (scb->sg_count) {
			struct scatterlist *sg;
			int i;

			scb->flags |= IPS_SCB_MAP_SG;

                        scsi_for_each_sg(SC, sg, scb->sg_count, i) {
				if (ips_fill_scb_sg_single
				    (ha, sg_dma_address(sg), scb, i,
				     sg_dma_len(sg)) < 0)
					break;
			}
			scb->dcdb.transfer_length = scb->data_len;
		} else {
                        scb->data_busaddr = 0L;
                        scb->sg_len = 0;
                        scb->data_len = 0;
                        scb->dcdb.transfer_length = 0;
		}

		scb->dcdb.cmd_attribute =
		    ips_command_direction[scb->scsi_cmd->cmnd[0]];

		/* Allow a WRITE BUFFER Command to Have no Data */
		/* This is Used by Tape Flash Utilites          */
		if ((scb->scsi_cmd->cmnd[0] == WRITE_BUFFER) &&
				(scb->data_len == 0))
			scb->dcdb.cmd_attribute = 0;

		if (!(scb->dcdb.cmd_attribute & 0x3))
			scb->dcdb.transfer_length = 0;

		if (scb->data_len >= IPS_MAX_XFER) {
			scb->dcdb.cmd_attribute |= IPS_TRANSFER64K;
			scb->dcdb.transfer_length = 0;
		}
		if (intr == IPS_INTR_ON)
			spin_lock(host->host_lock);

		ret = ips_send_cmd(ha, scb);

		switch (ret) {
		case IPS_SUCCESS:
			ips_putq_scb_head(&ha->scb_activelist, scb);
			break;
		case IPS_FAILURE:
			if (scb->scsi_cmd) {
				scb->scsi_cmd->result = DID_ERROR << 16;
				scb->scsi_cmd->scsi_done(scb->scsi_cmd);
			}

			if (scb->bus)
				ha->dcdb_active[scb->bus - 1] &=
				    ~(1 << scb->target_id);

			ips_freescb(ha, scb);
			break;
		case IPS_SUCCESS_IMM:
			if (scb->scsi_cmd)
				scb->scsi_cmd->scsi_done(scb->scsi_cmd);

			if (scb->bus)
				ha->dcdb_active[scb->bus - 1] &=
				    ~(1 << scb->target_id);

			ips_freescb(ha, scb);
			break;
		default:
			break;
		}		/* end case */

		p = (struct scsi_cmnd *) p->host_scribble;

	}			/* end while */

	if (intr == IPS_INTR_ON)
		spin_unlock(host->host_lock);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_putq_scb_head                                          */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Add an item to the head of the queue                                   */
/*                                                                          */
/* ASSUMED to be called from within the HA lock                             */
/*                                                                          */
/****************************************************************************/
static void
ips_putq_scb_head(ips_scb_queue_t * queue, ips_scb_t * item)
{
	METHOD_TRACE("ips_putq_scb_head", 1);

	if (!item)
		return;

	item->q_next = queue->head;
	queue->head = item;

	if (!queue->tail)
		queue->tail = item;

	queue->count++;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_removeq_scb_head                                       */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove the head of the queue                                           */
/*                                                                          */
/* ASSUMED to be called from within the HA lock                             */
/*                                                                          */
/****************************************************************************/
static ips_scb_t *
ips_removeq_scb_head(ips_scb_queue_t * queue)
{
	ips_scb_t *item;

	METHOD_TRACE("ips_removeq_scb_head", 1);

	item = queue->head;

	if (!item) {
		return (NULL);
	}

	queue->head = item->q_next;
	item->q_next = NULL;

	if (queue->tail == item)
		queue->tail = NULL;

	queue->count--;

	return (item);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_removeq_scb                                            */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove an item from a queue                                            */
/*                                                                          */
/* ASSUMED to be called from within the HA lock                             */
/*                                                                          */
/****************************************************************************/
static ips_scb_t *
ips_removeq_scb(ips_scb_queue_t * queue, ips_scb_t * item)
{
	ips_scb_t *p;

	METHOD_TRACE("ips_removeq_scb", 1);

	if (!item)
		return (NULL);

	if (item == queue->head) {
		return (ips_removeq_scb_head(queue));
	}

	p = queue->head;

	while ((p) && (item != p->q_next))
		p = p->q_next;

	if (p) {
		/* found a match */
		p->q_next = item->q_next;

		if (!item->q_next)
			queue->tail = p;

		item->q_next = NULL;
		queue->count--;

		return (item);
	}

	return (NULL);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_putq_wait_tail                                         */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Add an item to the tail of the queue                                   */
/*                                                                          */
/* ASSUMED to be called from within the HA lock                             */
/*                                                                          */
/****************************************************************************/
static void ips_putq_wait_tail(ips_wait_queue_entry_t *queue, struct scsi_cmnd *item)
{
	METHOD_TRACE("ips_putq_wait_tail", 1);

	if (!item)
		return;

	item->host_scribble = NULL;

	if (queue->tail)
		queue->tail->host_scribble = (char *) item;

	queue->tail = item;

	if (!queue->head)
		queue->head = item;

	queue->count++;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_removeq_wait_head                                      */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove the head of the queue                                           */
/*                                                                          */
/* ASSUMED to be called from within the HA lock                             */
/*                                                                          */
/****************************************************************************/
static struct scsi_cmnd *ips_removeq_wait_head(ips_wait_queue_entry_t *queue)
{
	struct scsi_cmnd *item;

	METHOD_TRACE("ips_removeq_wait_head", 1);

	item = queue->head;

	if (!item) {
		return (NULL);
	}

	queue->head = (struct scsi_cmnd *) item->host_scribble;
	item->host_scribble = NULL;

	if (queue->tail == item)
		queue->tail = NULL;

	queue->count--;

	return (item);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_removeq_wait                                           */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove an item from a queue                                            */
/*                                                                          */
/* ASSUMED to be called from within the HA lock                             */
/*                                                                          */
/****************************************************************************/
static struct scsi_cmnd *ips_removeq_wait(ips_wait_queue_entry_t *queue,
					  struct scsi_cmnd *item)
{
	struct scsi_cmnd *p;

	METHOD_TRACE("ips_removeq_wait", 1);

	if (!item)
		return (NULL);

	if (item == queue->head) {
		return (ips_removeq_wait_head(queue));
	}

	p = queue->head;

	while ((p) && (item != (struct scsi_cmnd *) p->host_scribble))
		p = (struct scsi_cmnd *) p->host_scribble;

	if (p) {
		/* found a match */
		p->host_scribble = item->host_scribble;

		if (!item->host_scribble)
			queue->tail = p;

		item->host_scribble = NULL;
		queue->count--;

		return (item);
	}

	return (NULL);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_putq_copp_tail                                         */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Add an item to the tail of the queue                                   */
/*                                                                          */
/* ASSUMED to be called from within the HA lock                             */
/*                                                                          */
/****************************************************************************/
static void
ips_putq_copp_tail(ips_copp_queue_t * queue, ips_copp_wait_item_t * item)
{
	METHOD_TRACE("ips_putq_copp_tail", 1);

	if (!item)
		return;

	item->next = NULL;

	if (queue->tail)
		queue->tail->next = item;

	queue->tail = item;

	if (!queue->head)
		queue->head = item;

	queue->count++;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_removeq_copp_head                                      */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove the head of the queue                                           */
/*                                                                          */
/* ASSUMED to be called from within the HA lock                             */
/*                                                                          */
/****************************************************************************/
static ips_copp_wait_item_t *
ips_removeq_copp_head(ips_copp_queue_t * queue)
{
	ips_copp_wait_item_t *item;

	METHOD_TRACE("ips_removeq_copp_head", 1);

	item = queue->head;

	if (!item) {
		return (NULL);
	}

	queue->head = item->next;
	item->next = NULL;

	if (queue->tail == item)
		queue->tail = NULL;

	queue->count--;

	return (item);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_removeq_copp                                           */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove an item from a queue                                            */
/*                                                                          */
/* ASSUMED to be called from within the HA lock                             */
/*                                                                          */
/****************************************************************************/
static ips_copp_wait_item_t *
ips_removeq_copp(ips_copp_queue_t * queue, ips_copp_wait_item_t * item)
{
	ips_copp_wait_item_t *p;

	METHOD_TRACE("ips_removeq_copp", 1);

	if (!item)
		return (NULL);

	if (item == queue->head) {
		return (ips_removeq_copp_head(queue));
	}

	p = queue->head;

	while ((p) && (item != p->next))
		p = p->next;

	if (p) {
		/* found a match */
		p->next = item->next;

		if (!item->next)
			queue->tail = p;

		item->next = NULL;
		queue->count--;

		return (item);
	}

	return (NULL);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ipsintr_blocking                                           */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Finalize an interrupt for internal commands                            */
/*                                                                          */
/****************************************************************************/
static void
ipsintr_blocking(ips_ha_t * ha, ips_scb_t * scb)
{
	METHOD_TRACE("ipsintr_blocking", 2);

	ips_freescb(ha, scb);
	if ((ha->waitflag == TRUE) && (ha->cmd_in_progress == scb->cdb[0])) {
		ha->waitflag = FALSE;

		return;
	}
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ipsintr_done                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Finalize an interrupt for non-internal commands                        */
/*                                                                          */
/****************************************************************************/
static void
ipsintr_done(ips_ha_t * ha, ips_scb_t * scb)
{
	METHOD_TRACE("ipsintr_done", 2);

	if (!scb) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "Spurious interrupt; scb NULL.\n");

		return;
	}

	if (scb->scsi_cmd == NULL) {
		/* unexpected interrupt */
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "Spurious interrupt; scsi_cmd not set.\n");

		return;
	}

	ips_done(ha, scb);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_done                                                   */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Do housekeeping on completed commands                                  */
/*  ASSUMED to be called form within the request lock                       */
/****************************************************************************/
static void
ips_done(ips_ha_t * ha, ips_scb_t * scb)
{
	int ret;

	METHOD_TRACE("ips_done", 1);

	if (!scb)
		return;

	if ((scb->scsi_cmd) && (ips_is_passthru(scb->scsi_cmd))) {
		ips_cleanup_passthru(ha, scb);
		ha->num_ioctl--;
	} else {
		/*
		 * Check to see if this command had too much
		 * data and had to be broke up.  If so, queue
		 * the rest of the data and continue.
		 */
		if ((scb->breakup) || (scb->sg_break)) {
                        struct scatterlist *sg;
                        int i, sg_dma_index, ips_sg_index = 0;

			/* we had a data breakup */
			scb->data_len = 0;

                        sg = scsi_sglist(scb->scsi_cmd);

                        /* Spin forward to last dma chunk */
                        sg_dma_index = scb->breakup;
                        for (i = 0; i < scb->breakup; i++)
                                sg = sg_next(sg);

			/* Take care of possible partial on last chunk */
                        ips_fill_scb_sg_single(ha,
                                               sg_dma_address(sg),
                                               scb, ips_sg_index++,
                                               sg_dma_len(sg));

                        for (; sg_dma_index < scsi_sg_count(scb->scsi_cmd);
                             sg_dma_index++, sg = sg_next(sg)) {
                                if (ips_fill_scb_sg_single
                                    (ha,
                                     sg_dma_address(sg),
                                     scb, ips_sg_index++,
                                     sg_dma_len(sg)) < 0)
                                        break;
                        }

			scb->dcdb.transfer_length = scb->data_len;
			scb->dcdb.cmd_attribute |=
			    ips_command_direction[scb->scsi_cmd->cmnd[0]];

			if (!(scb->dcdb.cmd_attribute & 0x3))
				scb->dcdb.transfer_length = 0;

			if (scb->data_len >= IPS_MAX_XFER) {
				scb->dcdb.cmd_attribute |= IPS_TRANSFER64K;
				scb->dcdb.transfer_length = 0;
			}

			ret = ips_send_cmd(ha, scb);

			switch (ret) {
			case IPS_FAILURE:
				if (scb->scsi_cmd) {
					scb->scsi_cmd->result = DID_ERROR << 16;
					scb->scsi_cmd->scsi_done(scb->scsi_cmd);
				}

				ips_freescb(ha, scb);
				break;
			case IPS_SUCCESS_IMM:
				if (scb->scsi_cmd) {
					scb->scsi_cmd->result = DID_ERROR << 16;
					scb->scsi_cmd->scsi_done(scb->scsi_cmd);
				}

				ips_freescb(ha, scb);
				break;
			default:
				break;
			}	/* end case */

			return;
		}
	}			/* end if passthru */

	if (scb->bus) {
		ha->dcdb_active[scb->bus - 1] &= ~(1 << scb->target_id);
	}

	scb->scsi_cmd->scsi_done(scb->scsi_cmd);

	ips_freescb(ha, scb);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_map_status                                             */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Map Controller Error codes to Linux Error Codes                        */
/*                                                                          */
/****************************************************************************/
static int
ips_map_status(ips_ha_t * ha, ips_scb_t * scb, ips_stat_t * sp)
{
	int errcode;
	int device_error;
	uint32_t transfer_len;
	IPS_DCDB_TABLE_TAPE *tapeDCDB;
	IPS_SCSI_INQ_DATA inquiryData;

	METHOD_TRACE("ips_map_status", 1);

	if (scb->bus) {
		DEBUG_VAR(2,
			  "(%s%d) Physical device error (%d %d %d): %x %x, Sense Key: %x, ASC: %x, ASCQ: %x",
			  ips_name, ha->host_num,
			  scb->scsi_cmd->device->channel,
			  scb->scsi_cmd->device->id, scb->scsi_cmd->device->lun,
			  scb->basic_status, scb->extended_status,
			  scb->extended_status ==
			  IPS_ERR_CKCOND ? scb->dcdb.sense_info[2] & 0xf : 0,
			  scb->extended_status ==
			  IPS_ERR_CKCOND ? scb->dcdb.sense_info[12] : 0,
			  scb->extended_status ==
			  IPS_ERR_CKCOND ? scb->dcdb.sense_info[13] : 0);
	}

	/* default driver error */
	errcode = DID_ERROR;
	device_error = 0;

	switch (scb->basic_status & IPS_GSC_STATUS_MASK) {
	case IPS_CMD_TIMEOUT:
		errcode = DID_TIME_OUT;
		break;

	case IPS_INVAL_OPCO:
	case IPS_INVAL_CMD_BLK:
	case IPS_INVAL_PARM_BLK:
	case IPS_LD_ERROR:
	case IPS_CMD_CMPLT_WERROR:
		break;

	case IPS_PHYS_DRV_ERROR:
		switch (scb->extended_status) {
		case IPS_ERR_SEL_TO:
			if (scb->bus)
				errcode = DID_NO_CONNECT;

			break;

		case IPS_ERR_OU_RUN:
			if ((scb->cmd.dcdb.op_code == IPS_CMD_EXTENDED_DCDB) ||
			    (scb->cmd.dcdb.op_code ==
			     IPS_CMD_EXTENDED_DCDB_SG)) {
				tapeDCDB = (IPS_DCDB_TABLE_TAPE *) & scb->dcdb;
				transfer_len = tapeDCDB->transfer_length;
			} else {
				transfer_len =
				    (uint32_t) scb->dcdb.transfer_length;
			}

			if ((scb->bus) && (transfer_len < scb->data_len)) {
				/* Underrun - set default to no error */
				errcode = DID_OK;

				/* Restrict access to physical DASD */
				if (scb->scsi_cmd->cmnd[0] == INQUIRY) {
				    ips_scmd_buf_read(scb->scsi_cmd,
                                      &inquiryData, sizeof (inquiryData));
 				    if ((inquiryData.DeviceType & 0x1f) == TYPE_DISK) {
				        errcode = DID_TIME_OUT;
				        break;
				    }
				}
			} else
				errcode = DID_ERROR;

			break;

		case IPS_ERR_RECOVERY:
			/* don't fail recovered errors */
			if (scb->bus)
				errcode = DID_OK;

			break;

		case IPS_ERR_HOST_RESET:
		case IPS_ERR_DEV_RESET:
			errcode = DID_RESET;
			break;

		case IPS_ERR_CKCOND:
			if (scb->bus) {
				if ((scb->cmd.dcdb.op_code ==
				     IPS_CMD_EXTENDED_DCDB)
				    || (scb->cmd.dcdb.op_code ==
					IPS_CMD_EXTENDED_DCDB_SG)) {
					tapeDCDB =
					    (IPS_DCDB_TABLE_TAPE *) & scb->dcdb;
					memcpy(scb->scsi_cmd->sense_buffer,
					       tapeDCDB->sense_info,
					       SCSI_SENSE_BUFFERSIZE);
				} else {
					memcpy(scb->scsi_cmd->sense_buffer,
					       scb->dcdb.sense_info,
					       SCSI_SENSE_BUFFERSIZE);
				}
				device_error = 2;	/* check condition */
			}

			errcode = DID_OK;

			break;

		default:
			errcode = DID_ERROR;
			break;

		}		/* end switch */
	}			/* end switch */

	scb->scsi_cmd->result = device_error | (errcode << 16);

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_send_wait                                              */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Send a command to the controller and wait for it to return             */
/*                                                                          */
/*   The FFDC Time Stamp use this function for the callback, but doesn't    */
/*   actually need to wait.                                                 */
/****************************************************************************/
static int
ips_send_wait(ips_ha_t * ha, ips_scb_t * scb, int timeout, int intr)
{
	int ret;

	METHOD_TRACE("ips_send_wait", 1);

	if (intr != IPS_FFDC) {	/* Won't be Waiting if this is a Time Stamp */
		ha->waitflag = TRUE;
		ha->cmd_in_progress = scb->cdb[0];
	}
	scb->callback = ipsintr_blocking;
	ret = ips_send_cmd(ha, scb);

	if ((ret == IPS_FAILURE) || (ret == IPS_SUCCESS_IMM))
		return (ret);

	if (intr != IPS_FFDC)	/* Don't Wait around if this is a Time Stamp */
		ret = ips_wait(ha, timeout, intr);

	return (ret);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_scmd_buf_write                                         */
/*                                                                          */
/* Routine Description:                                                     */
/*  Write data to struct scsi_cmnd request_buffer at proper offsets	    */
/****************************************************************************/
static void
ips_scmd_buf_write(struct scsi_cmnd *scmd, void *data, unsigned int count)
{
	unsigned long flags;

	local_irq_save(flags);
	scsi_sg_copy_from_buffer(scmd, data, count);
	local_irq_restore(flags);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_scmd_buf_read                                          */
/*                                                                          */
/* Routine Description:                                                     */
/*  Copy data from a struct scsi_cmnd to a new, linear buffer		    */
/****************************************************************************/
static void
ips_scmd_buf_read(struct scsi_cmnd *scmd, void *data, unsigned int count)
{
	unsigned long flags;

	local_irq_save(flags);
	scsi_sg_copy_to_buffer(scmd, data, count);
	local_irq_restore(flags);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_send_cmd                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Map SCSI commands to ServeRAID commands for logical drives             */
/*                                                                          */
/****************************************************************************/
static int
ips_send_cmd(ips_ha_t * ha, ips_scb_t * scb)
{
	int ret;
	char *sp;
	int device_error;
	IPS_DCDB_TABLE_TAPE *tapeDCDB;
	int TimeOut;

	METHOD_TRACE("ips_send_cmd", 1);

	ret = IPS_SUCCESS;

	if (!scb->scsi_cmd) {
		/* internal command */

		if (scb->bus > 0) {
			/* Controller commands can't be issued */
			/* to real devices -- fail them        */
			if ((ha->waitflag == TRUE) &&
			    (ha->cmd_in_progress == scb->cdb[0])) {
				ha->waitflag = FALSE;
			}

			return (1);
		}
	} else if ((scb->bus == 0) && (!ips_is_passthru(scb->scsi_cmd))) {
		/* command to logical bus -- interpret */
		ret = IPS_SUCCESS_IMM;

		switch (scb->scsi_cmd->cmnd[0]) {
		case ALLOW_MEDIUM_REMOVAL:
		case REZERO_UNIT:
		case ERASE:
		case WRITE_FILEMARKS:
		case SPACE:
			scb->scsi_cmd->result = DID_ERROR << 16;
			break;

		case START_STOP:
			scb->scsi_cmd->result = DID_OK << 16;
			break;

		case TEST_UNIT_READY:
		case INQUIRY:
			if (scb->target_id == IPS_ADAPTER_ID) {
				/*
				 * Either we have a TUR
				 * or we have a SCSI inquiry
				 */
				if (scb->scsi_cmd->cmnd[0] == TEST_UNIT_READY)
					scb->scsi_cmd->result = DID_OK << 16;

				if (scb->scsi_cmd->cmnd[0] == INQUIRY) {
					IPS_SCSI_INQ_DATA inquiry;

					memset(&inquiry, 0,
					       sizeof (IPS_SCSI_INQ_DATA));

					inquiry.DeviceType =
					    IPS_SCSI_INQ_TYPE_PROCESSOR;
					inquiry.DeviceTypeQualifier =
					    IPS_SCSI_INQ_LU_CONNECTED;
					inquiry.Version = IPS_SCSI_INQ_REV2;
					inquiry.ResponseDataFormat =
					    IPS_SCSI_INQ_RD_REV2;
					inquiry.AdditionalLength = 31;
					inquiry.Flags[0] =
					    IPS_SCSI_INQ_Address16;
					inquiry.Flags[1] =
					    IPS_SCSI_INQ_WBus16 |
					    IPS_SCSI_INQ_Sync;
					strncpy(inquiry.VendorId, "IBM     ",
						8);
					strncpy(inquiry.ProductId,
						"SERVERAID       ", 16);
					strncpy(inquiry.ProductRevisionLevel,
						"1.00", 4);

					ips_scmd_buf_write(scb->scsi_cmd,
							   &inquiry,
							   sizeof (inquiry));

					scb->scsi_cmd->result = DID_OK << 16;
				}
			} else {
				scb->cmd.logical_info.op_code = IPS_CMD_GET_LD_INFO;
				scb->cmd.logical_info.command_id = IPS_COMMAND_ID(ha, scb);
				scb->cmd.logical_info.reserved = 0;
				scb->cmd.logical_info.reserved2 = 0;
				scb->data_len = sizeof (IPS_LD_INFO);
				scb->data_busaddr = ha->logical_drive_info_dma_addr;
				scb->flags = 0;
				scb->cmd.logical_info.buffer_addr = scb->data_busaddr;
				ret = IPS_SUCCESS;
			}

			break;

		case REQUEST_SENSE:
			ips_reqsen(ha, scb);
			scb->scsi_cmd->result = DID_OK << 16;
			break;

		case READ_6:
		case WRITE_6:
			if (!scb->sg_len) {
				scb->cmd.basic_io.op_code =
				    (scb->scsi_cmd->cmnd[0] ==
				     READ_6) ? IPS_CMD_READ : IPS_CMD_WRITE;
				scb->cmd.basic_io.enhanced_sg = 0;
				scb->cmd.basic_io.sg_addr =
				    cpu_to_le32(scb->data_busaddr);
			} else {
				scb->cmd.basic_io.op_code =
				    (scb->scsi_cmd->cmnd[0] ==
				     READ_6) ? IPS_CMD_READ_SG :
				    IPS_CMD_WRITE_SG;
				scb->cmd.basic_io.enhanced_sg =
				    IPS_USE_ENH_SGLIST(ha) ? 0xFF : 0;
				scb->cmd.basic_io.sg_addr =
				    cpu_to_le32(scb->sg_busaddr);
			}

			scb->cmd.basic_io.segment_4G = 0;
			scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);
			scb->cmd.basic_io.log_drv = scb->target_id;
			scb->cmd.basic_io.sg_count = scb->sg_len;

			if (scb->cmd.basic_io.lba)
				le32_add_cpu(&scb->cmd.basic_io.lba,
						le16_to_cpu(scb->cmd.basic_io.
							    sector_count));
			else
				scb->cmd.basic_io.lba =
				    (((scb->scsi_cmd->
				       cmnd[1] & 0x1f) << 16) | (scb->scsi_cmd->
								 cmnd[2] << 8) |
				     (scb->scsi_cmd->cmnd[3]));

			scb->cmd.basic_io.sector_count =
			    cpu_to_le16(scb->data_len / IPS_BLKSIZE);

			if (le16_to_cpu(scb->cmd.basic_io.sector_count) == 0)
				scb->cmd.basic_io.sector_count =
				    cpu_to_le16(256);

			ret = IPS_SUCCESS;
			break;

		case READ_10:
		case WRITE_10:
			if (!scb->sg_len) {
				scb->cmd.basic_io.op_code =
				    (scb->scsi_cmd->cmnd[0] ==
				     READ_10) ? IPS_CMD_READ : IPS_CMD_WRITE;
				scb->cmd.basic_io.enhanced_sg = 0;
				scb->cmd.basic_io.sg_addr =
				    cpu_to_le32(scb->data_busaddr);
			} else {
				scb->cmd.basic_io.op_code =
				    (scb->scsi_cmd->cmnd[0] ==
				     READ_10) ? IPS_CMD_READ_SG :
				    IPS_CMD_WRITE_SG;
				scb->cmd.basic_io.enhanced_sg =
				    IPS_USE_ENH_SGLIST(ha) ? 0xFF : 0;
				scb->cmd.basic_io.sg_addr =
				    cpu_to_le32(scb->sg_busaddr);
			}

			scb->cmd.basic_io.segment_4G = 0;
			scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);
			scb->cmd.basic_io.log_drv = scb->target_id;
			scb->cmd.basic_io.sg_count = scb->sg_len;

			if (scb->cmd.basic_io.lba)
				le32_add_cpu(&scb->cmd.basic_io.lba,
						le16_to_cpu(scb->cmd.basic_io.
							    sector_count));
			else
				scb->cmd.basic_io.lba =
				    ((scb->scsi_cmd->cmnd[2] << 24) | (scb->
								       scsi_cmd->
								       cmnd[3]
								       << 16) |
				     (scb->scsi_cmd->cmnd[4] << 8) | scb->
				     scsi_cmd->cmnd[5]);

			scb->cmd.basic_io.sector_count =
			    cpu_to_le16(scb->data_len / IPS_BLKSIZE);

			if (cpu_to_le16(scb->cmd.basic_io.sector_count) == 0) {
				/*
				 * This is a null condition
				 * we don't have to do anything
				 * so just return
				 */
				scb->scsi_cmd->result = DID_OK << 16;
			} else
				ret = IPS_SUCCESS;

			break;

		case RESERVE:
		case RELEASE:
			scb->scsi_cmd->result = DID_OK << 16;
			break;

		case MODE_SENSE:
			scb->cmd.basic_io.op_code = IPS_CMD_ENQUIRY;
			scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);
			scb->cmd.basic_io.segment_4G = 0;
			scb->cmd.basic_io.enhanced_sg = 0;
			scb->data_len = sizeof (*ha->enq);
			scb->cmd.basic_io.sg_addr = ha->enq_busaddr;
			ret = IPS_SUCCESS;
			break;

		case READ_CAPACITY:
			scb->cmd.logical_info.op_code = IPS_CMD_GET_LD_INFO;
			scb->cmd.logical_info.command_id = IPS_COMMAND_ID(ha, scb);
			scb->cmd.logical_info.reserved = 0;
			scb->cmd.logical_info.reserved2 = 0;
			scb->cmd.logical_info.reserved3 = 0;
			scb->data_len = sizeof (IPS_LD_INFO);
			scb->data_busaddr = ha->logical_drive_info_dma_addr;
			scb->flags = 0;
			scb->cmd.logical_info.buffer_addr = scb->data_busaddr;
			ret = IPS_SUCCESS;
			break;

		case SEND_DIAGNOSTIC:
		case REASSIGN_BLOCKS:
		case FORMAT_UNIT:
		case SEEK_10:
		case VERIFY:
		case READ_DEFECT_DATA:
		case READ_BUFFER:
		case WRITE_BUFFER:
			scb->scsi_cmd->result = DID_OK << 16;
			break;

		default:
			/* Set the Return Info to appear like the Command was */
			/* attempted, a Check Condition occurred, and Sense   */
			/* Data indicating an Invalid CDB OpCode is returned. */
			sp = (char *) scb->scsi_cmd->sense_buffer;

			sp[0] = 0x70;	/* Error Code               */
			sp[2] = ILLEGAL_REQUEST;	/* Sense Key 5 Illegal Req. */
			sp[7] = 0x0A;	/* Additional Sense Length  */
			sp[12] = 0x20;	/* ASC = Invalid OpCode     */
			sp[13] = 0x00;	/* ASCQ                     */

			device_error = 2;	/* Indicate Check Condition */
			scb->scsi_cmd->result = device_error | (DID_OK << 16);
			break;
		}		/* end switch */
	}
	/* end if */
	if (ret == IPS_SUCCESS_IMM)
		return (ret);

	/* setup DCDB */
	if (scb->bus > 0) {

		/* If we already know the Device is Not there, no need to attempt a Command   */
		/* This also protects an NT FailOver Controller from getting CDB's sent to it */
		if (ha->conf->dev[scb->bus - 1][scb->target_id].ucState == 0) {
			scb->scsi_cmd->result = DID_NO_CONNECT << 16;
			return (IPS_SUCCESS_IMM);
		}

		ha->dcdb_active[scb->bus - 1] |= (1 << scb->target_id);
		scb->cmd.dcdb.command_id = IPS_COMMAND_ID(ha, scb);
		scb->cmd.dcdb.dcdb_address = cpu_to_le32(scb->scb_busaddr +
							 (unsigned long) &scb->
							 dcdb -
							 (unsigned long) scb);
		scb->cmd.dcdb.reserved = 0;
		scb->cmd.dcdb.reserved2 = 0;
		scb->cmd.dcdb.reserved3 = 0;
		scb->cmd.dcdb.segment_4G = 0;
		scb->cmd.dcdb.enhanced_sg = 0;

		TimeOut = scb->scsi_cmd->request->timeout;

		if (ha->subsys->param[4] & 0x00100000) {	/* If NEW Tape DCDB is Supported */
			if (!scb->sg_len) {
				scb->cmd.dcdb.op_code = IPS_CMD_EXTENDED_DCDB;
			} else {
				scb->cmd.dcdb.op_code =
				    IPS_CMD_EXTENDED_DCDB_SG;
				scb->cmd.dcdb.enhanced_sg =
				    IPS_USE_ENH_SGLIST(ha) ? 0xFF : 0;
			}

			tapeDCDB = (IPS_DCDB_TABLE_TAPE *) & scb->dcdb;	/* Use Same Data Area as Old DCDB Struct */
			tapeDCDB->device_address =
			    ((scb->bus - 1) << 4) | scb->target_id;
			tapeDCDB->cmd_attribute |= IPS_DISCONNECT_ALLOWED;
			tapeDCDB->cmd_attribute &= ~IPS_TRANSFER64K;	/* Always Turn OFF 64K Size Flag */

			if (TimeOut) {
				if (TimeOut < (10 * HZ))
					tapeDCDB->cmd_attribute |= IPS_TIMEOUT10;	/* TimeOut is 10 Seconds */
				else if (TimeOut < (60 * HZ))
					tapeDCDB->cmd_attribute |= IPS_TIMEOUT60;	/* TimeOut is 60 Seconds */
				else if (TimeOut < (1200 * HZ))
					tapeDCDB->cmd_attribute |= IPS_TIMEOUT20M;	/* TimeOut is 20 Minutes */
			}

			tapeDCDB->cdb_length = scb->scsi_cmd->cmd_len;
			tapeDCDB->reserved_for_LUN = 0;
			tapeDCDB->transfer_length = scb->data_len;
			if (scb->cmd.dcdb.op_code == IPS_CMD_EXTENDED_DCDB_SG)
				tapeDCDB->buffer_pointer =
				    cpu_to_le32(scb->sg_busaddr);
			else
				tapeDCDB->buffer_pointer =
				    cpu_to_le32(scb->data_busaddr);
			tapeDCDB->sg_count = scb->sg_len;
			tapeDCDB->sense_length = sizeof (tapeDCDB->sense_info);
			tapeDCDB->scsi_status = 0;
			tapeDCDB->reserved = 0;
			memcpy(tapeDCDB->scsi_cdb, scb->scsi_cmd->cmnd,
			       scb->scsi_cmd->cmd_len);
		} else {
			if (!scb->sg_len) {
				scb->cmd.dcdb.op_code = IPS_CMD_DCDB;
			} else {
				scb->cmd.dcdb.op_code = IPS_CMD_DCDB_SG;
				scb->cmd.dcdb.enhanced_sg =
				    IPS_USE_ENH_SGLIST(ha) ? 0xFF : 0;
			}

			scb->dcdb.device_address =
			    ((scb->bus - 1) << 4) | scb->target_id;
			scb->dcdb.cmd_attribute |= IPS_DISCONNECT_ALLOWED;

			if (TimeOut) {
				if (TimeOut < (10 * HZ))
					scb->dcdb.cmd_attribute |= IPS_TIMEOUT10;	/* TimeOut is 10 Seconds */
				else if (TimeOut < (60 * HZ))
					scb->dcdb.cmd_attribute |= IPS_TIMEOUT60;	/* TimeOut is 60 Seconds */
				else if (TimeOut < (1200 * HZ))
					scb->dcdb.cmd_attribute |= IPS_TIMEOUT20M;	/* TimeOut is 20 Minutes */
			}

			scb->dcdb.transfer_length = scb->data_len;
			if (scb->dcdb.cmd_attribute & IPS_TRANSFER64K)
				scb->dcdb.transfer_length = 0;
			if (scb->cmd.dcdb.op_code == IPS_CMD_DCDB_SG)
				scb->dcdb.buffer_pointer =
				    cpu_to_le32(scb->sg_busaddr);
			else
				scb->dcdb.buffer_pointer =
				    cpu_to_le32(scb->data_busaddr);
			scb->dcdb.cdb_length = scb->scsi_cmd->cmd_len;
			scb->dcdb.sense_length = sizeof (scb->dcdb.sense_info);
			scb->dcdb.sg_count = scb->sg_len;
			scb->dcdb.reserved = 0;
			memcpy(scb->dcdb.scsi_cdb, scb->scsi_cmd->cmnd,
			       scb->scsi_cmd->cmd_len);
			scb->dcdb.scsi_status = 0;
			scb->dcdb.reserved2[0] = 0;
			scb->dcdb.reserved2[1] = 0;
			scb->dcdb.reserved2[2] = 0;
		}
	}

	return ((*ha->func.issue) (ha, scb));
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_chk_status                                             */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Check the status of commands to logical drives                         */
/*   Assumed to be called with the HA lock                                  */
/****************************************************************************/
static void
ips_chkstatus(ips_ha_t * ha, IPS_STATUS * pstatus)
{
	ips_scb_t *scb;
	ips_stat_t *sp;
	uint8_t basic_status;
	uint8_t ext_status;
	int errcode;
	IPS_SCSI_INQ_DATA inquiryData;

	METHOD_TRACE("ips_chkstatus", 1);

	scb = &ha->scbs[pstatus->fields.command_id];
	scb->basic_status = basic_status =
	    pstatus->fields.basic_status & IPS_BASIC_STATUS_MASK;
	scb->extended_status = ext_status = pstatus->fields.extended_status;

	sp = &ha->sp;
	sp->residue_len = 0;
	sp->scb_addr = (void *) scb;

	/* Remove the item from the active queue */
	ips_removeq_scb(&ha->scb_activelist, scb);

	if (!scb->scsi_cmd)
		/* internal commands are handled in do_ipsintr */
		return;

	DEBUG_VAR(2, "(%s%d) ips_chkstatus: cmd 0x%X id %d (%d %d %d)",
		  ips_name,
		  ha->host_num,
		  scb->cdb[0],
		  scb->cmd.basic_io.command_id,
		  scb->bus, scb->target_id, scb->lun);

	if ((scb->scsi_cmd) && (ips_is_passthru(scb->scsi_cmd)))
		/* passthru - just returns the raw result */
		return;

	errcode = DID_OK;

	if (((basic_status & IPS_GSC_STATUS_MASK) == IPS_CMD_SUCCESS) ||
	    ((basic_status & IPS_GSC_STATUS_MASK) == IPS_CMD_RECOVERED_ERROR)) {

		if (scb->bus == 0) {
			if ((basic_status & IPS_GSC_STATUS_MASK) ==
			    IPS_CMD_RECOVERED_ERROR) {
				DEBUG_VAR(1,
					  "(%s%d) Recovered Logical Drive Error OpCode: %x, BSB: %x, ESB: %x",
					  ips_name, ha->host_num,
					  scb->cmd.basic_io.op_code,
					  basic_status, ext_status);
			}

			switch (scb->scsi_cmd->cmnd[0]) {
			case ALLOW_MEDIUM_REMOVAL:
			case REZERO_UNIT:
			case ERASE:
			case WRITE_FILEMARKS:
			case SPACE:
				errcode = DID_ERROR;
				break;

			case START_STOP:
				break;

			case TEST_UNIT_READY:
				if (!ips_online(ha, scb)) {
					errcode = DID_TIME_OUT;
				}
				break;

			case INQUIRY:
				if (ips_online(ha, scb)) {
					ips_inquiry(ha, scb);
				} else {
					errcode = DID_TIME_OUT;
				}
				break;

			case REQUEST_SENSE:
				ips_reqsen(ha, scb);
				break;

			case READ_6:
			case WRITE_6:
			case READ_10:
			case WRITE_10:
			case RESERVE:
			case RELEASE:
				break;

			case MODE_SENSE:
				if (!ips_online(ha, scb)
				    || !ips_msense(ha, scb)) {
					errcode = DID_ERROR;
				}
				break;

			case READ_CAPACITY:
				if (ips_online(ha, scb))
					ips_rdcap(ha, scb);
				else {
					errcode = DID_TIME_OUT;
				}
				break;

			case SEND_DIAGNOSTIC:
			case REASSIGN_BLOCKS:
				break;

			case FORMAT_UNIT:
				errcode = DID_ERROR;
				break;

			case SEEK_10:
			case VERIFY:
			case READ_DEFECT_DATA:
			case READ_BUFFER:
			case WRITE_BUFFER:
				break;

			default:
				errcode = DID_ERROR;
			}	/* end switch */

			scb->scsi_cmd->result = errcode << 16;
		} else {	/* bus == 0 */
			/* restrict access to physical drives */
			if (scb->scsi_cmd->cmnd[0] == INQUIRY) {
			    ips_scmd_buf_read(scb->scsi_cmd,
                                  &inquiryData, sizeof (inquiryData));
			    if ((inquiryData.DeviceType & 0x1f) == TYPE_DISK)
			        scb->scsi_cmd->result = DID_TIME_OUT << 16;
			}
		}		/* else */
	} else {		/* recovered error / success */
		if (scb->bus == 0) {
			DEBUG_VAR(1,
				  "(%s%d) Unrecovered Logical Drive Error OpCode: %x, BSB: %x, ESB: %x",
				  ips_name, ha->host_num,
				  scb->cmd.basic_io.op_code, basic_status,
				  ext_status);
		}

		ips_map_status(ha, scb, sp);
	}			/* else */
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_online                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Determine if a logical drive is online                                 */
/*                                                                          */
/****************************************************************************/
static int
ips_online(ips_ha_t * ha, ips_scb_t * scb)
{
	METHOD_TRACE("ips_online", 1);

	if (scb->target_id >= IPS_MAX_LD)
		return (0);

	if ((scb->basic_status & IPS_GSC_STATUS_MASK) > 1) {
		memset(ha->logical_drive_info, 0, sizeof (IPS_LD_INFO));
		return (0);
	}

	if (ha->logical_drive_info->drive_info[scb->target_id].state !=
	    IPS_LD_OFFLINE
	    && ha->logical_drive_info->drive_info[scb->target_id].state !=
	    IPS_LD_FREE
	    && ha->logical_drive_info->drive_info[scb->target_id].state !=
	    IPS_LD_CRS
	    && ha->logical_drive_info->drive_info[scb->target_id].state !=
	    IPS_LD_SYS)
		return (1);
	else
		return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_inquiry                                                */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Simulate an inquiry command to a logical drive                         */
/*                                                                          */
/****************************************************************************/
static int
ips_inquiry(ips_ha_t * ha, ips_scb_t * scb)
{
	IPS_SCSI_INQ_DATA inquiry;

	METHOD_TRACE("ips_inquiry", 1);

	memset(&inquiry, 0, sizeof (IPS_SCSI_INQ_DATA));

	inquiry.DeviceType = IPS_SCSI_INQ_TYPE_DASD;
	inquiry.DeviceTypeQualifier = IPS_SCSI_INQ_LU_CONNECTED;
	inquiry.Version = IPS_SCSI_INQ_REV2;
	inquiry.ResponseDataFormat = IPS_SCSI_INQ_RD_REV2;
	inquiry.AdditionalLength = 31;
	inquiry.Flags[0] = IPS_SCSI_INQ_Address16;
	inquiry.Flags[1] =
	    IPS_SCSI_INQ_WBus16 | IPS_SCSI_INQ_Sync | IPS_SCSI_INQ_CmdQue;
	strncpy(inquiry.VendorId, "IBM     ", 8);
	strncpy(inquiry.ProductId, "SERVERAID       ", 16);
	strncpy(inquiry.ProductRevisionLevel, "1.00", 4);

	ips_scmd_buf_write(scb->scsi_cmd, &inquiry, sizeof (inquiry));

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_rdcap                                                  */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Simulate a read capacity command to a logical drive                    */
/*                                                                          */
/****************************************************************************/
static int
ips_rdcap(ips_ha_t * ha, ips_scb_t * scb)
{
	IPS_SCSI_CAPACITY cap;

	METHOD_TRACE("ips_rdcap", 1);

	if (scsi_bufflen(scb->scsi_cmd) < 8)
		return (0);

	cap.lba =
	    cpu_to_be32(le32_to_cpu
			(ha->logical_drive_info->
			 drive_info[scb->target_id].sector_count) - 1);
	cap.len = cpu_to_be32((uint32_t) IPS_BLKSIZE);

	ips_scmd_buf_write(scb->scsi_cmd, &cap, sizeof (cap));

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_msense                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Simulate a mode sense command to a logical drive                       */
/*                                                                          */
/****************************************************************************/
static int
ips_msense(ips_ha_t * ha, ips_scb_t * scb)
{
	uint16_t heads;
	uint16_t sectors;
	uint32_t cylinders;
	IPS_SCSI_MODE_PAGE_DATA mdata;

	METHOD_TRACE("ips_msense", 1);

	if (le32_to_cpu(ha->enq->ulDriveSize[scb->target_id]) > 0x400000 &&
	    (ha->enq->ucMiscFlag & 0x8) == 0) {
		heads = IPS_NORM_HEADS;
		sectors = IPS_NORM_SECTORS;
	} else {
		heads = IPS_COMP_HEADS;
		sectors = IPS_COMP_SECTORS;
	}

	cylinders =
	    (le32_to_cpu(ha->enq->ulDriveSize[scb->target_id]) -
	     1) / (heads * sectors);

	memset(&mdata, 0, sizeof (IPS_SCSI_MODE_PAGE_DATA));

	mdata.hdr.BlockDescLength = 8;

	switch (scb->scsi_cmd->cmnd[2] & 0x3f) {
	case 0x03:		/* page 3 */
		mdata.pdata.pg3.PageCode = 3;
		mdata.pdata.pg3.PageLength = sizeof (IPS_SCSI_MODE_PAGE3);
		mdata.hdr.DataLength =
		    3 + mdata.hdr.BlockDescLength + mdata.pdata.pg3.PageLength;
		mdata.pdata.pg3.TracksPerZone = 0;
		mdata.pdata.pg3.AltSectorsPerZone = 0;
		mdata.pdata.pg3.AltTracksPerZone = 0;
		mdata.pdata.pg3.AltTracksPerVolume = 0;
		mdata.pdata.pg3.SectorsPerTrack = cpu_to_be16(sectors);
		mdata.pdata.pg3.BytesPerSector = cpu_to_be16(IPS_BLKSIZE);
		mdata.pdata.pg3.Interleave = cpu_to_be16(1);
		mdata.pdata.pg3.TrackSkew = 0;
		mdata.pdata.pg3.CylinderSkew = 0;
		mdata.pdata.pg3.flags = IPS_SCSI_MP3_SoftSector;
		break;

	case 0x4:
		mdata.pdata.pg4.PageCode = 4;
		mdata.pdata.pg4.PageLength = sizeof (IPS_SCSI_MODE_PAGE4);
		mdata.hdr.DataLength =
		    3 + mdata.hdr.BlockDescLength + mdata.pdata.pg4.PageLength;
		mdata.pdata.pg4.CylindersHigh =
		    cpu_to_be16((cylinders >> 8) & 0xFFFF);
		mdata.pdata.pg4.CylindersLow = (cylinders & 0xFF);
		mdata.pdata.pg4.Heads = heads;
		mdata.pdata.pg4.WritePrecompHigh = 0;
		mdata.pdata.pg4.WritePrecompLow = 0;
		mdata.pdata.pg4.ReducedWriteCurrentHigh = 0;
		mdata.pdata.pg4.ReducedWriteCurrentLow = 0;
		mdata.pdata.pg4.StepRate = cpu_to_be16(1);
		mdata.pdata.pg4.LandingZoneHigh = 0;
		mdata.pdata.pg4.LandingZoneLow = 0;
		mdata.pdata.pg4.flags = 0;
		mdata.pdata.pg4.RotationalOffset = 0;
		mdata.pdata.pg4.MediumRotationRate = 0;
		break;
	case 0x8:
		mdata.pdata.pg8.PageCode = 8;
		mdata.pdata.pg8.PageLength = sizeof (IPS_SCSI_MODE_PAGE8);
		mdata.hdr.DataLength =
		    3 + mdata.hdr.BlockDescLength + mdata.pdata.pg8.PageLength;
		/* everything else is left set to 0 */
		break;

	default:
		return (0);
	}			/* end switch */

	ips_scmd_buf_write(scb->scsi_cmd, &mdata, sizeof (mdata));

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_reqsen                                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Simulate a request sense command to a logical drive                    */
/*                                                                          */
/****************************************************************************/
static int
ips_reqsen(ips_ha_t * ha, ips_scb_t * scb)
{
	IPS_SCSI_REQSEN reqsen;

	METHOD_TRACE("ips_reqsen", 1);

	memset(&reqsen, 0, sizeof (IPS_SCSI_REQSEN));

	reqsen.ResponseCode =
	    IPS_SCSI_REQSEN_VALID | IPS_SCSI_REQSEN_CURRENT_ERR;
	reqsen.AdditionalLength = 10;
	reqsen.AdditionalSenseCode = IPS_SCSI_REQSEN_NO_SENSE;
	reqsen.AdditionalSenseCodeQual = IPS_SCSI_REQSEN_NO_SENSE;

	ips_scmd_buf_write(scb->scsi_cmd, &reqsen, sizeof (reqsen));

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_free                                                   */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Free any allocated space for this controller                           */
/*                                                                          */
/****************************************************************************/
static void
ips_free(ips_ha_t * ha)
{

	METHOD_TRACE("ips_free", 1);

	if (ha) {
		if (ha->enq) {
			dma_free_coherent(&ha->pcidev->dev, sizeof(IPS_ENQ),
					    ha->enq, ha->enq_busaddr);
			ha->enq = NULL;
		}

		kfree(ha->conf);
		ha->conf = NULL;

		if (ha->adapt) {
			dma_free_coherent(&ha->pcidev->dev,
					    sizeof (IPS_ADAPTER) +
					    sizeof (IPS_IO_CMD), ha->adapt,
					    ha->adapt->hw_status_start);
			ha->adapt = NULL;
		}

		if (ha->logical_drive_info) {
			dma_free_coherent(&ha->pcidev->dev,
					    sizeof (IPS_LD_INFO),
					    ha->logical_drive_info,
					    ha->logical_drive_info_dma_addr);
			ha->logical_drive_info = NULL;
		}

		kfree(ha->nvram);
		ha->nvram = NULL;

		kfree(ha->subsys);
		ha->subsys = NULL;

		if (ha->ioctl_data) {
			dma_free_coherent(&ha->pcidev->dev, ha->ioctl_len,
					    ha->ioctl_data, ha->ioctl_busaddr);
			ha->ioctl_data = NULL;
			ha->ioctl_datasize = 0;
			ha->ioctl_len = 0;
		}
		ips_deallocatescbs(ha, ha->max_cmds);

		/* free memory mapped (if applicable) */
		if (ha->mem_ptr) {
			iounmap(ha->ioremap_ptr);
			ha->ioremap_ptr = NULL;
			ha->mem_ptr = NULL;
		}

		ha->mem_addr = 0;

	}
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_deallocatescbs                                         */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Free the command blocks                                                */
/*                                                                          */
/****************************************************************************/
static int
ips_deallocatescbs(ips_ha_t * ha, int cmds)
{
	if (ha->scbs) {
		dma_free_coherent(&ha->pcidev->dev,
				    IPS_SGLIST_SIZE(ha) * IPS_MAX_SG * cmds,
				    ha->scbs->sg_list.list,
				    ha->scbs->sg_busaddr);
		dma_free_coherent(&ha->pcidev->dev, sizeof (ips_scb_t) * cmds,
				    ha->scbs, ha->scbs->scb_busaddr);
		ha->scbs = NULL;
	}			/* end if */
	return 1;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_allocatescbs                                           */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Allocate the command blocks                                            */
/*                                                                          */
/****************************************************************************/
static int
ips_allocatescbs(ips_ha_t * ha)
{
	ips_scb_t *scb_p;
	IPS_SG_LIST ips_sg;
	int i;
	dma_addr_t command_dma, sg_dma;

	METHOD_TRACE("ips_allocatescbs", 1);

	/* Allocate memory for the SCBs */
	ha->scbs = dma_alloc_coherent(&ha->pcidev->dev,
			ha->max_cmds * sizeof (ips_scb_t),
			&command_dma, GFP_KERNEL);
	if (ha->scbs == NULL)
		return 0;
	ips_sg.list = dma_alloc_coherent(&ha->pcidev->dev,
			IPS_SGLIST_SIZE(ha) * IPS_MAX_SG * ha->max_cmds,
			&sg_dma, GFP_KERNEL);
	if (ips_sg.list == NULL) {
		dma_free_coherent(&ha->pcidev->dev,
				    ha->max_cmds * sizeof (ips_scb_t), ha->scbs,
				    command_dma);
		return 0;
	}

	memset(ha->scbs, 0, ha->max_cmds * sizeof (ips_scb_t));

	for (i = 0; i < ha->max_cmds; i++) {
		scb_p = &ha->scbs[i];
		scb_p->scb_busaddr = command_dma + sizeof (ips_scb_t) * i;
		/* set up S/G list */
		if (IPS_USE_ENH_SGLIST(ha)) {
			scb_p->sg_list.enh_list =
			    ips_sg.enh_list + i * IPS_MAX_SG;
			scb_p->sg_busaddr =
			    sg_dma + IPS_SGLIST_SIZE(ha) * IPS_MAX_SG * i;
		} else {
			scb_p->sg_list.std_list =
			    ips_sg.std_list + i * IPS_MAX_SG;
			scb_p->sg_busaddr =
			    sg_dma + IPS_SGLIST_SIZE(ha) * IPS_MAX_SG * i;
		}

		/* add to the free list */
		if (i < ha->max_cmds - 1) {
			scb_p->q_next = ha->scb_freelist;
			ha->scb_freelist = scb_p;
		}
	}

	/* success */
	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_init_scb                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Initialize a CCB to default values                                     */
/*                                                                          */
/****************************************************************************/
static void
ips_init_scb(ips_ha_t * ha, ips_scb_t * scb)
{
	IPS_SG_LIST sg_list;
	uint32_t cmd_busaddr, sg_busaddr;
	METHOD_TRACE("ips_init_scb", 1);

	if (scb == NULL)
		return;

	sg_list.list = scb->sg_list.list;
	cmd_busaddr = scb->scb_busaddr;
	sg_busaddr = scb->sg_busaddr;
	/* zero fill */
	memset(scb, 0, sizeof (ips_scb_t));
	memset(ha->dummy, 0, sizeof (IPS_IO_CMD));

	/* Initialize dummy command bucket */
	ha->dummy->op_code = 0xFF;
	ha->dummy->ccsar = cpu_to_le32(ha->adapt->hw_status_start
				       + sizeof (IPS_ADAPTER));
	ha->dummy->command_id = IPS_MAX_CMDS;

	/* set bus address of scb */
	scb->scb_busaddr = cmd_busaddr;
	scb->sg_busaddr = sg_busaddr;
	scb->sg_list.list = sg_list.list;

	/* Neptune Fix */
	scb->cmd.basic_io.cccr = cpu_to_le32((uint32_t) IPS_BIT_ILE);
	scb->cmd.basic_io.ccsar = cpu_to_le32(ha->adapt->hw_status_start
					      + sizeof (IPS_ADAPTER));
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_get_scb                                                */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Initialize a CCB to default values                                     */
/*                                                                          */
/* ASSUMED to be called from within a lock                                 */
/*                                                                          */
/****************************************************************************/
static ips_scb_t *
ips_getscb(ips_ha_t * ha)
{
	ips_scb_t *scb;

	METHOD_TRACE("ips_getscb", 1);

	if ((scb = ha->scb_freelist) == NULL) {

		return (NULL);
	}

	ha->scb_freelist = scb->q_next;
	scb->flags = 0;
	scb->q_next = NULL;

	ips_init_scb(ha, scb);

	return (scb);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_free_scb                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Return an unused CCB back to the free list                             */
/*                                                                          */
/* ASSUMED to be called from within a lock                                  */
/*                                                                          */
/****************************************************************************/
static void
ips_freescb(ips_ha_t * ha, ips_scb_t * scb)
{

	METHOD_TRACE("ips_freescb", 1);
	if (scb->flags & IPS_SCB_MAP_SG)
                scsi_dma_unmap(scb->scsi_cmd);
	else if (scb->flags & IPS_SCB_MAP_SINGLE)
		dma_unmap_single(&ha->pcidev->dev, scb->data_busaddr,
				 scb->data_len, IPS_DMA_DIR(scb));

	/* check to make sure this is not our "special" scb */
	if (IPS_COMMAND_ID(ha, scb) < (ha->max_cmds - 1)) {
		scb->q_next = ha->scb_freelist;
		ha->scb_freelist = scb;
	}
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_isinit_copperhead                                      */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Is controller initialized ?                                            */
/*                                                                          */
/****************************************************************************/
static int
ips_isinit_copperhead(ips_ha_t * ha)
{
	uint8_t scpr;
	uint8_t isr;

	METHOD_TRACE("ips_isinit_copperhead", 1);

	isr = inb(ha->io_addr + IPS_REG_HISR);
	scpr = inb(ha->io_addr + IPS_REG_SCPR);

	if (((isr & IPS_BIT_EI) == 0) && ((scpr & IPS_BIT_EBM) == 0))
		return (0);
	else
		return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_isinit_copperhead_memio                                */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Is controller initialized ?                                            */
/*                                                                          */
/****************************************************************************/
static int
ips_isinit_copperhead_memio(ips_ha_t * ha)
{
	uint8_t isr = 0;
	uint8_t scpr;

	METHOD_TRACE("ips_is_init_copperhead_memio", 1);

	isr = readb(ha->mem_ptr + IPS_REG_HISR);
	scpr = readb(ha->mem_ptr + IPS_REG_SCPR);

	if (((isr & IPS_BIT_EI) == 0) && ((scpr & IPS_BIT_EBM) == 0))
		return (0);
	else
		return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_isinit_morpheus                                        */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Is controller initialized ?                                            */
/*                                                                          */
/****************************************************************************/
static int
ips_isinit_morpheus(ips_ha_t * ha)
{
	uint32_t post;
	uint32_t bits;

	METHOD_TRACE("ips_is_init_morpheus", 1);

	if (ips_isintr_morpheus(ha))
	    ips_flush_and_reset(ha);

	post = readl(ha->mem_ptr + IPS_REG_I960_MSG0);
	bits = readl(ha->mem_ptr + IPS_REG_I2O_HIR);

	if (post == 0)
		return (0);
	else if (bits & 0x3)
		return (0);
	else
		return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_flush_and_reset                                        */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Perform cleanup ( FLUSH and RESET ) when the adapter is in an unknown  */
/*   state ( was trying to INIT and an interrupt was already pending ) ...  */
/*                                                                          */
/****************************************************************************/
static void
ips_flush_and_reset(ips_ha_t *ha)
{
	ips_scb_t *scb;
	int  ret;
 	int  time;
	int  done;
	dma_addr_t command_dma;

	/* Create a usuable SCB */
	scb = dma_alloc_coherent(&ha->pcidev->dev, sizeof(ips_scb_t),
			&command_dma, GFP_KERNEL);
	if (scb) {
	    memset(scb, 0, sizeof(ips_scb_t));
	    ips_init_scb(ha, scb);
	    scb->scb_busaddr = command_dma;

	    scb->timeout = ips_cmd_timeout;
	    scb->cdb[0] = IPS_CMD_FLUSH;

	    scb->cmd.flush_cache.op_code = IPS_CMD_FLUSH;
	    scb->cmd.flush_cache.command_id = IPS_MAX_CMDS;   /* Use an ID that would otherwise not exist */
	    scb->cmd.flush_cache.state = IPS_NORM_STATE;
	    scb->cmd.flush_cache.reserved = 0;
	    scb->cmd.flush_cache.reserved2 = 0;
	    scb->cmd.flush_cache.reserved3 = 0;
	    scb->cmd.flush_cache.reserved4 = 0;

	    ret = ips_send_cmd(ha, scb);                      /* Send the Flush Command */

	    if (ret == IPS_SUCCESS) {
	        time = 60 * IPS_ONE_SEC;	              /* Max Wait time is 60 seconds */
	        done = 0;

	        while ((time > 0) && (!done)) {
		   done = ips_poll_for_flush_complete(ha);
	           /* This may look evil, but it's only done during extremely rare start-up conditions ! */
	           udelay(1000);
	           time--;
	        }
        }
	}

	/* Now RESET and INIT the adapter */
	(*ha->func.reset) (ha);

	dma_free_coherent(&ha->pcidev->dev, sizeof(ips_scb_t), scb, command_dma);
	return;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_poll_for_flush_complete                                */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Poll for the Flush Command issued by ips_flush_and_reset() to complete */
/*   All other responses are just taken off the queue and ignored           */
/*                                                                          */
/****************************************************************************/
static int
ips_poll_for_flush_complete(ips_ha_t * ha)
{
	IPS_STATUS cstatus;

	while (TRUE) {
	    cstatus.value = (*ha->func.statupd) (ha);

	    if (cstatus.value == 0xffffffff)      /* If No Interrupt to process */
			break;

	    /* Success is when we see the Flush Command ID */
	    if (cstatus.fields.command_id == IPS_MAX_CMDS)
	        return 1;
	 }

	return 0;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_enable_int_copperhead                                  */
/*                                                                          */
/* Routine Description:                                                     */
/*   Turn on interrupts                                                     */
/*                                                                          */
/****************************************************************************/
static void
ips_enable_int_copperhead(ips_ha_t * ha)
{
	METHOD_TRACE("ips_enable_int_copperhead", 1);

	outb(ha->io_addr + IPS_REG_HISR, IPS_BIT_EI);
	inb(ha->io_addr + IPS_REG_HISR);	/*Ensure PCI Posting Completes*/
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_enable_int_copperhead_memio                            */
/*                                                                          */
/* Routine Description:                                                     */
/*   Turn on interrupts                                                     */
/*                                                                          */
/****************************************************************************/
static void
ips_enable_int_copperhead_memio(ips_ha_t * ha)
{
	METHOD_TRACE("ips_enable_int_copperhead_memio", 1);

	writeb(IPS_BIT_EI, ha->mem_ptr + IPS_REG_HISR);
	readb(ha->mem_ptr + IPS_REG_HISR);	/*Ensure PCI Posting Completes*/
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_enable_int_morpheus                                    */
/*                                                                          */
/* Routine Description:                                                     */
/*   Turn on interrupts                                                     */
/*                                                                          */
/****************************************************************************/
static void
ips_enable_int_morpheus(ips_ha_t * ha)
{
	uint32_t Oimr;

	METHOD_TRACE("ips_enable_int_morpheus", 1);

	Oimr = readl(ha->mem_ptr + IPS_REG_I960_OIMR);
	Oimr &= ~0x08;
	writel(Oimr, ha->mem_ptr + IPS_REG_I960_OIMR);
	readl(ha->mem_ptr + IPS_REG_I960_OIMR);	/*Ensure PCI Posting Completes*/
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_init_copperhead                                        */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Initialize a copperhead controller                                     */
/*                                                                          */
/****************************************************************************/
static int
ips_init_copperhead(ips_ha_t * ha)
{
	uint8_t Isr;
	uint8_t Cbsp;
	uint8_t PostByte[IPS_MAX_POST_BYTES];
	uint8_t ConfigByte[IPS_MAX_CONFIG_BYTES];
	int i, j;

	METHOD_TRACE("ips_init_copperhead", 1);

	for (i = 0; i < IPS_MAX_POST_BYTES; i++) {
		for (j = 0; j < 45; j++) {
			Isr = inb(ha->io_addr + IPS_REG_HISR);
			if (Isr & IPS_BIT_GHI)
				break;

			/* Delay for 1 Second */
			MDELAY(IPS_ONE_SEC);
		}

		if (j >= 45)
			/* error occurred */
			return (0);

		PostByte[i] = inb(ha->io_addr + IPS_REG_ISPR);
		outb(Isr, ha->io_addr + IPS_REG_HISR);
	}

	if (PostByte[0] < IPS_GOOD_POST_STATUS) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "reset controller fails (post status %x %x).\n",
			   PostByte[0], PostByte[1]);

		return (0);
	}

	for (i = 0; i < IPS_MAX_CONFIG_BYTES; i++) {
		for (j = 0; j < 240; j++) {
			Isr = inb(ha->io_addr + IPS_REG_HISR);
			if (Isr & IPS_BIT_GHI)
				break;

			/* Delay for 1 Second */
			MDELAY(IPS_ONE_SEC);
		}

		if (j >= 240)
			/* error occurred */
			return (0);

		ConfigByte[i] = inb(ha->io_addr + IPS_REG_ISPR);
		outb(Isr, ha->io_addr + IPS_REG_HISR);
	}

	for (i = 0; i < 240; i++) {
		Cbsp = inb(ha->io_addr + IPS_REG_CBSP);

		if ((Cbsp & IPS_BIT_OP) == 0)
			break;

		/* Delay for 1 Second */
		MDELAY(IPS_ONE_SEC);
	}

	if (i >= 240)
		/* reset failed */
		return (0);

	/* setup CCCR */
	outl(0x1010, ha->io_addr + IPS_REG_CCCR);

	/* Enable busmastering */
	outb(IPS_BIT_EBM, ha->io_addr + IPS_REG_SCPR);

	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		/* fix for anaconda64 */
		outl(0, ha->io_addr + IPS_REG_NDAE);

	/* Enable interrupts */
	outb(IPS_BIT_EI, ha->io_addr + IPS_REG_HISR);

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_init_copperhead_memio                                  */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Initialize a copperhead controller with memory mapped I/O              */
/*                                                                          */
/****************************************************************************/
static int
ips_init_copperhead_memio(ips_ha_t * ha)
{
	uint8_t Isr = 0;
	uint8_t Cbsp;
	uint8_t PostByte[IPS_MAX_POST_BYTES];
	uint8_t ConfigByte[IPS_MAX_CONFIG_BYTES];
	int i, j;

	METHOD_TRACE("ips_init_copperhead_memio", 1);

	for (i = 0; i < IPS_MAX_POST_BYTES; i++) {
		for (j = 0; j < 45; j++) {
			Isr = readb(ha->mem_ptr + IPS_REG_HISR);
			if (Isr & IPS_BIT_GHI)
				break;

			/* Delay for 1 Second */
			MDELAY(IPS_ONE_SEC);
		}

		if (j >= 45)
			/* error occurred */
			return (0);

		PostByte[i] = readb(ha->mem_ptr + IPS_REG_ISPR);
		writeb(Isr, ha->mem_ptr + IPS_REG_HISR);
	}

	if (PostByte[0] < IPS_GOOD_POST_STATUS) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "reset controller fails (post status %x %x).\n",
			   PostByte[0], PostByte[1]);

		return (0);
	}

	for (i = 0; i < IPS_MAX_CONFIG_BYTES; i++) {
		for (j = 0; j < 240; j++) {
			Isr = readb(ha->mem_ptr + IPS_REG_HISR);
			if (Isr & IPS_BIT_GHI)
				break;

			/* Delay for 1 Second */
			MDELAY(IPS_ONE_SEC);
		}

		if (j >= 240)
			/* error occurred */
			return (0);

		ConfigByte[i] = readb(ha->mem_ptr + IPS_REG_ISPR);
		writeb(Isr, ha->mem_ptr + IPS_REG_HISR);
	}

	for (i = 0; i < 240; i++) {
		Cbsp = readb(ha->mem_ptr + IPS_REG_CBSP);

		if ((Cbsp & IPS_BIT_OP) == 0)
			break;

		/* Delay for 1 Second */
		MDELAY(IPS_ONE_SEC);
	}

	if (i >= 240)
		/* error occurred */
		return (0);

	/* setup CCCR */
	writel(0x1010, ha->mem_ptr + IPS_REG_CCCR);

	/* Enable busmastering */
	writeb(IPS_BIT_EBM, ha->mem_ptr + IPS_REG_SCPR);

	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		/* fix for anaconda64 */
		writel(0, ha->mem_ptr + IPS_REG_NDAE);

	/* Enable interrupts */
	writeb(IPS_BIT_EI, ha->mem_ptr + IPS_REG_HISR);

	/* if we get here then everything went OK */
	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_init_morpheus                                          */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Initialize a morpheus controller                                       */
/*                                                                          */
/****************************************************************************/
static int
ips_init_morpheus(ips_ha_t * ha)
{
	uint32_t Post;
	uint32_t Config;
	uint32_t Isr;
	uint32_t Oimr;
	int i;

	METHOD_TRACE("ips_init_morpheus", 1);

	/* Wait up to 45 secs for Post */
	for (i = 0; i < 45; i++) {
		Isr = readl(ha->mem_ptr + IPS_REG_I2O_HIR);

		if (Isr & IPS_BIT_I960_MSG0I)
			break;

		/* Delay for 1 Second */
		MDELAY(IPS_ONE_SEC);
	}

	if (i >= 45) {
		/* error occurred */
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "timeout waiting for post.\n");

		return (0);
	}

	Post = readl(ha->mem_ptr + IPS_REG_I960_MSG0);

	if (Post == 0x4F00) {	/* If Flashing the Battery PIC         */
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "Flashing Battery PIC, Please wait ...\n");

		/* Clear the interrupt bit */
		Isr = (uint32_t) IPS_BIT_I960_MSG0I;
		writel(Isr, ha->mem_ptr + IPS_REG_I2O_HIR);

		for (i = 0; i < 120; i++) {	/*    Wait Up to 2 Min. for Completion */
			Post = readl(ha->mem_ptr + IPS_REG_I960_MSG0);
			if (Post != 0x4F00)
				break;
			/* Delay for 1 Second */
			MDELAY(IPS_ONE_SEC);
		}

		if (i >= 120) {
			IPS_PRINTK(KERN_WARNING, ha->pcidev,
				   "timeout waiting for Battery PIC Flash\n");
			return (0);
		}

	}

	/* Clear the interrupt bit */
	Isr = (uint32_t) IPS_BIT_I960_MSG0I;
	writel(Isr, ha->mem_ptr + IPS_REG_I2O_HIR);

	if (Post < (IPS_GOOD_POST_STATUS << 8)) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "reset controller fails (post status %x).\n", Post);

		return (0);
	}

	/* Wait up to 240 secs for config bytes */
	for (i = 0; i < 240; i++) {
		Isr = readl(ha->mem_ptr + IPS_REG_I2O_HIR);

		if (Isr & IPS_BIT_I960_MSG1I)
			break;

		/* Delay for 1 Second */
		MDELAY(IPS_ONE_SEC);
	}

	if (i >= 240) {
		/* error occurred */
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "timeout waiting for config.\n");

		return (0);
	}

	Config = readl(ha->mem_ptr + IPS_REG_I960_MSG1);

	/* Clear interrupt bit */
	Isr = (uint32_t) IPS_BIT_I960_MSG1I;
	writel(Isr, ha->mem_ptr + IPS_REG_I2O_HIR);

	/* Turn on the interrupts */
	Oimr = readl(ha->mem_ptr + IPS_REG_I960_OIMR);
	Oimr &= ~0x8;
	writel(Oimr, ha->mem_ptr + IPS_REG_I960_OIMR);

	/* if we get here then everything went OK */

	/* Since we did a RESET, an EraseStripeLock may be needed */
	if (Post == 0xEF10) {
		if ((Config == 0x000F) || (Config == 0x0009))
			ha->requires_esl = 1;
	}

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_reset_copperhead                                       */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Reset the controller                                                   */
/*                                                                          */
/****************************************************************************/
static int
ips_reset_copperhead(ips_ha_t * ha)
{
	int reset_counter;

	METHOD_TRACE("ips_reset_copperhead", 1);

	DEBUG_VAR(1, "(%s%d) ips_reset_copperhead: io addr: %x, irq: %d",
		  ips_name, ha->host_num, ha->io_addr, ha->pcidev->irq);

	reset_counter = 0;

	while (reset_counter < 2) {
		reset_counter++;

		outb(IPS_BIT_RST, ha->io_addr + IPS_REG_SCPR);

		/* Delay for 1 Second */
		MDELAY(IPS_ONE_SEC);

		outb(0, ha->io_addr + IPS_REG_SCPR);

		/* Delay for 1 Second */
		MDELAY(IPS_ONE_SEC);

		if ((*ha->func.init) (ha))
			break;
		else if (reset_counter >= 2) {

			return (0);
		}
	}

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_reset_copperhead_memio                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Reset the controller                                                   */
/*                                                                          */
/****************************************************************************/
static int
ips_reset_copperhead_memio(ips_ha_t * ha)
{
	int reset_counter;

	METHOD_TRACE("ips_reset_copperhead_memio", 1);

	DEBUG_VAR(1, "(%s%d) ips_reset_copperhead_memio: mem addr: %x, irq: %d",
		  ips_name, ha->host_num, ha->mem_addr, ha->pcidev->irq);

	reset_counter = 0;

	while (reset_counter < 2) {
		reset_counter++;

		writeb(IPS_BIT_RST, ha->mem_ptr + IPS_REG_SCPR);

		/* Delay for 1 Second */
		MDELAY(IPS_ONE_SEC);

		writeb(0, ha->mem_ptr + IPS_REG_SCPR);

		/* Delay for 1 Second */
		MDELAY(IPS_ONE_SEC);

		if ((*ha->func.init) (ha))
			break;
		else if (reset_counter >= 2) {

			return (0);
		}
	}

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_reset_morpheus                                         */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Reset the controller                                                   */
/*                                                                          */
/****************************************************************************/
static int
ips_reset_morpheus(ips_ha_t * ha)
{
	int reset_counter;
	uint8_t junk;

	METHOD_TRACE("ips_reset_morpheus", 1);

	DEBUG_VAR(1, "(%s%d) ips_reset_morpheus: mem addr: %x, irq: %d",
		  ips_name, ha->host_num, ha->mem_addr, ha->pcidev->irq);

	reset_counter = 0;

	while (reset_counter < 2) {
		reset_counter++;

		writel(0x80000000, ha->mem_ptr + IPS_REG_I960_IDR);

		/* Delay for 5 Seconds */
		MDELAY(5 * IPS_ONE_SEC);

		/* Do a PCI config read to wait for adapter */
		pci_read_config_byte(ha->pcidev, 4, &junk);

		if ((*ha->func.init) (ha))
			break;
		else if (reset_counter >= 2) {

			return (0);
		}
	}

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_statinit                                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Initialize the status queues on the controller                         */
/*                                                                          */
/****************************************************************************/
static void
ips_statinit(ips_ha_t * ha)
{
	uint32_t phys_status_start;

	METHOD_TRACE("ips_statinit", 1);

	ha->adapt->p_status_start = ha->adapt->status;
	ha->adapt->p_status_end = ha->adapt->status + IPS_MAX_CMDS;
	ha->adapt->p_status_tail = ha->adapt->status;

	phys_status_start = ha->adapt->hw_status_start;
	outl(phys_status_start, ha->io_addr + IPS_REG_SQSR);
	outl(phys_status_start + IPS_STATUS_Q_SIZE,
	     ha->io_addr + IPS_REG_SQER);
	outl(phys_status_start + IPS_STATUS_SIZE,
	     ha->io_addr + IPS_REG_SQHR);
	outl(phys_status_start, ha->io_addr + IPS_REG_SQTR);

	ha->adapt->hw_status_tail = phys_status_start;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_statinit_memio                                         */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Initialize the status queues on the controller                         */
/*                                                                          */
/****************************************************************************/
static void
ips_statinit_memio(ips_ha_t * ha)
{
	uint32_t phys_status_start;

	METHOD_TRACE("ips_statinit_memio", 1);

	ha->adapt->p_status_start = ha->adapt->status;
	ha->adapt->p_status_end = ha->adapt->status + IPS_MAX_CMDS;
	ha->adapt->p_status_tail = ha->adapt->status;

	phys_status_start = ha->adapt->hw_status_start;
	writel(phys_status_start, ha->mem_ptr + IPS_REG_SQSR);
	writel(phys_status_start + IPS_STATUS_Q_SIZE,
	       ha->mem_ptr + IPS_REG_SQER);
	writel(phys_status_start + IPS_STATUS_SIZE, ha->mem_ptr + IPS_REG_SQHR);
	writel(phys_status_start, ha->mem_ptr + IPS_REG_SQTR);

	ha->adapt->hw_status_tail = phys_status_start;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_statupd_copperhead                                     */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove an element from the status queue                                */
/*                                                                          */
/****************************************************************************/
static uint32_t
ips_statupd_copperhead(ips_ha_t * ha)
{
	METHOD_TRACE("ips_statupd_copperhead", 1);

	if (ha->adapt->p_status_tail != ha->adapt->p_status_end) {
		ha->adapt->p_status_tail++;
		ha->adapt->hw_status_tail += sizeof (IPS_STATUS);
	} else {
		ha->adapt->p_status_tail = ha->adapt->p_status_start;
		ha->adapt->hw_status_tail = ha->adapt->hw_status_start;
	}

	outl(ha->adapt->hw_status_tail,
	     ha->io_addr + IPS_REG_SQTR);

	return (ha->adapt->p_status_tail->value);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_statupd_copperhead_memio                               */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove an element from the status queue                                */
/*                                                                          */
/****************************************************************************/
static uint32_t
ips_statupd_copperhead_memio(ips_ha_t * ha)
{
	METHOD_TRACE("ips_statupd_copperhead_memio", 1);

	if (ha->adapt->p_status_tail != ha->adapt->p_status_end) {
		ha->adapt->p_status_tail++;
		ha->adapt->hw_status_tail += sizeof (IPS_STATUS);
	} else {
		ha->adapt->p_status_tail = ha->adapt->p_status_start;
		ha->adapt->hw_status_tail = ha->adapt->hw_status_start;
	}

	writel(ha->adapt->hw_status_tail, ha->mem_ptr + IPS_REG_SQTR);

	return (ha->adapt->p_status_tail->value);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_statupd_morpheus                                       */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Remove an element from the status queue                                */
/*                                                                          */
/****************************************************************************/
static uint32_t
ips_statupd_morpheus(ips_ha_t * ha)
{
	uint32_t val;

	METHOD_TRACE("ips_statupd_morpheus", 1);

	val = readl(ha->mem_ptr + IPS_REG_I2O_OUTMSGQ);

	return (val);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_issue_copperhead                                       */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Send a command down to the controller                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_issue_copperhead(ips_ha_t * ha, ips_scb_t * scb)
{
	uint32_t TimeOut;
	uint32_t val;

	METHOD_TRACE("ips_issue_copperhead", 1);

	if (scb->scsi_cmd) {
		DEBUG_VAR(2, "(%s%d) ips_issue: cmd 0x%X id %d (%d %d %d)",
			  ips_name,
			  ha->host_num,
			  scb->cdb[0],
			  scb->cmd.basic_io.command_id,
			  scb->bus, scb->target_id, scb->lun);
	} else {
		DEBUG_VAR(2, KERN_NOTICE "(%s%d) ips_issue: logical cmd id %d",
			  ips_name, ha->host_num, scb->cmd.basic_io.command_id);
	}

	TimeOut = 0;

	while ((val =
		le32_to_cpu(inl(ha->io_addr + IPS_REG_CCCR))) & IPS_BIT_SEM) {
		udelay(1000);

		if (++TimeOut >= IPS_SEM_TIMEOUT) {
			if (!(val & IPS_BIT_START_STOP))
				break;

			IPS_PRINTK(KERN_WARNING, ha->pcidev,
				   "ips_issue val [0x%x].\n", val);
			IPS_PRINTK(KERN_WARNING, ha->pcidev,
				   "ips_issue semaphore chk timeout.\n");

			return (IPS_FAILURE);
		}		/* end if */
	}			/* end while */

	outl(scb->scb_busaddr, ha->io_addr + IPS_REG_CCSAR);
	outw(IPS_BIT_START_CMD, ha->io_addr + IPS_REG_CCCR);

	return (IPS_SUCCESS);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_issue_copperhead_memio                                 */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Send a command down to the controller                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_issue_copperhead_memio(ips_ha_t * ha, ips_scb_t * scb)
{
	uint32_t TimeOut;
	uint32_t val;

	METHOD_TRACE("ips_issue_copperhead_memio", 1);

	if (scb->scsi_cmd) {
		DEBUG_VAR(2, "(%s%d) ips_issue: cmd 0x%X id %d (%d %d %d)",
			  ips_name,
			  ha->host_num,
			  scb->cdb[0],
			  scb->cmd.basic_io.command_id,
			  scb->bus, scb->target_id, scb->lun);
	} else {
		DEBUG_VAR(2, "(%s%d) ips_issue: logical cmd id %d",
			  ips_name, ha->host_num, scb->cmd.basic_io.command_id);
	}

	TimeOut = 0;

	while ((val = readl(ha->mem_ptr + IPS_REG_CCCR)) & IPS_BIT_SEM) {
		udelay(1000);

		if (++TimeOut >= IPS_SEM_TIMEOUT) {
			if (!(val & IPS_BIT_START_STOP))
				break;

			IPS_PRINTK(KERN_WARNING, ha->pcidev,
				   "ips_issue val [0x%x].\n", val);
			IPS_PRINTK(KERN_WARNING, ha->pcidev,
				   "ips_issue semaphore chk timeout.\n");

			return (IPS_FAILURE);
		}		/* end if */
	}			/* end while */

	writel(scb->scb_busaddr, ha->mem_ptr + IPS_REG_CCSAR);
	writel(IPS_BIT_START_CMD, ha->mem_ptr + IPS_REG_CCCR);

	return (IPS_SUCCESS);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_issue_i2o                                              */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Send a command down to the controller                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_issue_i2o(ips_ha_t * ha, ips_scb_t * scb)
{

	METHOD_TRACE("ips_issue_i2o", 1);

	if (scb->scsi_cmd) {
		DEBUG_VAR(2, "(%s%d) ips_issue: cmd 0x%X id %d (%d %d %d)",
			  ips_name,
			  ha->host_num,
			  scb->cdb[0],
			  scb->cmd.basic_io.command_id,
			  scb->bus, scb->target_id, scb->lun);
	} else {
		DEBUG_VAR(2, "(%s%d) ips_issue: logical cmd id %d",
			  ips_name, ha->host_num, scb->cmd.basic_io.command_id);
	}

	outl(scb->scb_busaddr, ha->io_addr + IPS_REG_I2O_INMSGQ);

	return (IPS_SUCCESS);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_issue_i2o_memio                                        */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Send a command down to the controller                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_issue_i2o_memio(ips_ha_t * ha, ips_scb_t * scb)
{

	METHOD_TRACE("ips_issue_i2o_memio", 1);

	if (scb->scsi_cmd) {
		DEBUG_VAR(2, "(%s%d) ips_issue: cmd 0x%X id %d (%d %d %d)",
			  ips_name,
			  ha->host_num,
			  scb->cdb[0],
			  scb->cmd.basic_io.command_id,
			  scb->bus, scb->target_id, scb->lun);
	} else {
		DEBUG_VAR(2, "(%s%d) ips_issue: logical cmd id %d",
			  ips_name, ha->host_num, scb->cmd.basic_io.command_id);
	}

	writel(scb->scb_busaddr, ha->mem_ptr + IPS_REG_I2O_INMSGQ);

	return (IPS_SUCCESS);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_isintr_copperhead                                      */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Test to see if an interrupt is for us                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_isintr_copperhead(ips_ha_t * ha)
{
	uint8_t Isr;

	METHOD_TRACE("ips_isintr_copperhead", 2);

	Isr = inb(ha->io_addr + IPS_REG_HISR);

	if (Isr == 0xFF)
		/* ?!?! Nothing really there */
		return (0);

	if (Isr & IPS_BIT_SCE)
		return (1);
	else if (Isr & (IPS_BIT_SQO | IPS_BIT_GHI)) {
		/* status queue overflow or GHI */
		/* just clear the interrupt */
		outb(Isr, ha->io_addr + IPS_REG_HISR);
	}

	return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_isintr_copperhead_memio                                */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Test to see if an interrupt is for us                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_isintr_copperhead_memio(ips_ha_t * ha)
{
	uint8_t Isr;

	METHOD_TRACE("ips_isintr_memio", 2);

	Isr = readb(ha->mem_ptr + IPS_REG_HISR);

	if (Isr == 0xFF)
		/* ?!?! Nothing really there */
		return (0);

	if (Isr & IPS_BIT_SCE)
		return (1);
	else if (Isr & (IPS_BIT_SQO | IPS_BIT_GHI)) {
		/* status queue overflow or GHI */
		/* just clear the interrupt */
		writeb(Isr, ha->mem_ptr + IPS_REG_HISR);
	}

	return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_isintr_morpheus                                        */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Test to see if an interrupt is for us                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_isintr_morpheus(ips_ha_t * ha)
{
	uint32_t Isr;

	METHOD_TRACE("ips_isintr_morpheus", 2);

	Isr = readl(ha->mem_ptr + IPS_REG_I2O_HIR);

	if (Isr & IPS_BIT_I2O_OPQI)
		return (1);
	else
		return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_wait                                                   */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Wait for a command to complete                                         */
/*                                                                          */
/****************************************************************************/
static int
ips_wait(ips_ha_t * ha, int time, int intr)
{
	int ret;
	int done;

	METHOD_TRACE("ips_wait", 1);

	ret = IPS_FAILURE;
	done = FALSE;

	time *= IPS_ONE_SEC;	/* convert seconds */

	while ((time > 0) && (!done)) {
		if (intr == IPS_INTR_ON) {
			if (ha->waitflag == FALSE) {
				ret = IPS_SUCCESS;
				done = TRUE;
				break;
			}
		} else if (intr == IPS_INTR_IORL) {
			if (ha->waitflag == FALSE) {
				/*
				 * controller generated an interrupt to
				 * acknowledge completion of the command
				 * and ips_intr() has serviced the interrupt.
				 */
				ret = IPS_SUCCESS;
				done = TRUE;
				break;
			}

			/*
			 * NOTE: we already have the io_request_lock so
			 * even if we get an interrupt it won't get serviced
			 * until after we finish.
			 */

			(*ha->func.intr) (ha);
		}

		/* This looks like a very evil loop, but it only does this during start-up */
		udelay(1000);
		time--;
	}

	return (ret);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_write_driver_status                                    */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Write OS/Driver version to Page 5 of the nvram on the controller       */
/*                                                                          */
/****************************************************************************/
static int
ips_write_driver_status(ips_ha_t * ha, int intr)
{
	METHOD_TRACE("ips_write_driver_status", 1);

	if (!ips_readwrite_page5(ha, FALSE, intr)) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "unable to read NVRAM page 5.\n");

		return (0);
	}

	/* check to make sure the page has a valid */
	/* signature */
	if (le32_to_cpu(ha->nvram->signature) != IPS_NVRAM_P5_SIG) {
		DEBUG_VAR(1,
			  "(%s%d) NVRAM page 5 has an invalid signature: %X.",
			  ips_name, ha->host_num, ha->nvram->signature);
		ha->nvram->signature = IPS_NVRAM_P5_SIG;
	}

	DEBUG_VAR(2,
		  "(%s%d) Ad Type: %d, Ad Slot: %d, BIOS: %c%c%c%c %c%c%c%c.",
		  ips_name, ha->host_num, le16_to_cpu(ha->nvram->adapter_type),
		  ha->nvram->adapter_slot, ha->nvram->bios_high[0],
		  ha->nvram->bios_high[1], ha->nvram->bios_high[2],
		  ha->nvram->bios_high[3], ha->nvram->bios_low[0],
		  ha->nvram->bios_low[1], ha->nvram->bios_low[2],
		  ha->nvram->bios_low[3]);

	ips_get_bios_version(ha, intr);

	/* change values (as needed) */
	ha->nvram->operating_system = IPS_OS_LINUX;
	ha->nvram->adapter_type = ha->ad_type;
	strncpy((char *) ha->nvram->driver_high, IPS_VERSION_HIGH, 4);
	strncpy((char *) ha->nvram->driver_low, IPS_VERSION_LOW, 4);
	strncpy((char *) ha->nvram->bios_high, ha->bios_version, 4);
	strncpy((char *) ha->nvram->bios_low, ha->bios_version + 4, 4);

	ha->nvram->versioning = 0;	/* Indicate the Driver Does Not Support Versioning */

	/* now update the page */
	if (!ips_readwrite_page5(ha, TRUE, intr)) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "unable to write NVRAM page 5.\n");

		return (0);
	}

	/* IF NVRAM Page 5 is OK, Use it for Slot Number Info Because Linux Doesn't Do Slots */
	ha->slot_num = ha->nvram->adapter_slot;

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_read_adapter_status                                    */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Do an Inquiry command to the adapter                                   */
/*                                                                          */
/****************************************************************************/
static int
ips_read_adapter_status(ips_ha_t * ha, int intr)
{
	ips_scb_t *scb;
	int ret;

	METHOD_TRACE("ips_read_adapter_status", 1);

	scb = &ha->scbs[ha->max_cmds - 1];

	ips_init_scb(ha, scb);

	scb->timeout = ips_cmd_timeout;
	scb->cdb[0] = IPS_CMD_ENQUIRY;

	scb->cmd.basic_io.op_code = IPS_CMD_ENQUIRY;
	scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);
	scb->cmd.basic_io.sg_count = 0;
	scb->cmd.basic_io.lba = 0;
	scb->cmd.basic_io.sector_count = 0;
	scb->cmd.basic_io.log_drv = 0;
	scb->data_len = sizeof (*ha->enq);
	scb->cmd.basic_io.sg_addr = ha->enq_busaddr;

	/* send command */
	if (((ret =
	      ips_send_wait(ha, scb, ips_cmd_timeout, intr)) == IPS_FAILURE)
	    || (ret == IPS_SUCCESS_IMM)
	    || ((scb->basic_status & IPS_GSC_STATUS_MASK) > 1))
		return (0);

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_read_subsystem_parameters                              */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Read subsystem parameters from the adapter                             */
/*                                                                          */
/****************************************************************************/
static int
ips_read_subsystem_parameters(ips_ha_t * ha, int intr)
{
	ips_scb_t *scb;
	int ret;

	METHOD_TRACE("ips_read_subsystem_parameters", 1);

	scb = &ha->scbs[ha->max_cmds - 1];

	ips_init_scb(ha, scb);

	scb->timeout = ips_cmd_timeout;
	scb->cdb[0] = IPS_CMD_GET_SUBSYS;

	scb->cmd.basic_io.op_code = IPS_CMD_GET_SUBSYS;
	scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);
	scb->cmd.basic_io.sg_count = 0;
	scb->cmd.basic_io.lba = 0;
	scb->cmd.basic_io.sector_count = 0;
	scb->cmd.basic_io.log_drv = 0;
	scb->data_len = sizeof (*ha->subsys);
	scb->cmd.basic_io.sg_addr = ha->ioctl_busaddr;

	/* send command */
	if (((ret =
	      ips_send_wait(ha, scb, ips_cmd_timeout, intr)) == IPS_FAILURE)
	    || (ret == IPS_SUCCESS_IMM)
	    || ((scb->basic_status & IPS_GSC_STATUS_MASK) > 1))
		return (0);

	memcpy(ha->subsys, ha->ioctl_data, sizeof(*ha->subsys));
	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_read_config                                            */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Read the configuration on the adapter                                  */
/*                                                                          */
/****************************************************************************/
static int
ips_read_config(ips_ha_t * ha, int intr)
{
	ips_scb_t *scb;
	int i;
	int ret;

	METHOD_TRACE("ips_read_config", 1);

	/* set defaults for initiator IDs */
	for (i = 0; i < 4; i++)
		ha->conf->init_id[i] = 7;

	scb = &ha->scbs[ha->max_cmds - 1];

	ips_init_scb(ha, scb);

	scb->timeout = ips_cmd_timeout;
	scb->cdb[0] = IPS_CMD_READ_CONF;

	scb->cmd.basic_io.op_code = IPS_CMD_READ_CONF;
	scb->cmd.basic_io.command_id = IPS_COMMAND_ID(ha, scb);
	scb->data_len = sizeof (*ha->conf);
	scb->cmd.basic_io.sg_addr = ha->ioctl_busaddr;

	/* send command */
	if (((ret =
	      ips_send_wait(ha, scb, ips_cmd_timeout, intr)) == IPS_FAILURE)
	    || (ret == IPS_SUCCESS_IMM)
	    || ((scb->basic_status & IPS_GSC_STATUS_MASK) > 1)) {

		memset(ha->conf, 0, sizeof (IPS_CONF));

		/* reset initiator IDs */
		for (i = 0; i < 4; i++)
			ha->conf->init_id[i] = 7;

		/* Allow Completed with Errors, so JCRM can access the Adapter to fix the problems */
		if ((scb->basic_status & IPS_GSC_STATUS_MASK) ==
		    IPS_CMD_CMPLT_WERROR)
			return (1);

		return (0);
	}

	memcpy(ha->conf, ha->ioctl_data, sizeof(*ha->conf));
	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_readwrite_page5                                        */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Read nvram page 5 from the adapter                                     */
/*                                                                          */
/****************************************************************************/
static int
ips_readwrite_page5(ips_ha_t * ha, int write, int intr)
{
	ips_scb_t *scb;
	int ret;

	METHOD_TRACE("ips_readwrite_page5", 1);

	scb = &ha->scbs[ha->max_cmds - 1];

	ips_init_scb(ha, scb);

	scb->timeout = ips_cmd_timeout;
	scb->cdb[0] = IPS_CMD_RW_NVRAM_PAGE;

	scb->cmd.nvram.op_code = IPS_CMD_RW_NVRAM_PAGE;
	scb->cmd.nvram.command_id = IPS_COMMAND_ID(ha, scb);
	scb->cmd.nvram.page = 5;
	scb->cmd.nvram.write = write;
	scb->cmd.nvram.reserved = 0;
	scb->cmd.nvram.reserved2 = 0;
	scb->data_len = sizeof (*ha->nvram);
	scb->cmd.nvram.buffer_addr = ha->ioctl_busaddr;
	if (write)
		memcpy(ha->ioctl_data, ha->nvram, sizeof(*ha->nvram));

	/* issue the command */
	if (((ret =
	      ips_send_wait(ha, scb, ips_cmd_timeout, intr)) == IPS_FAILURE)
	    || (ret == IPS_SUCCESS_IMM)
	    || ((scb->basic_status & IPS_GSC_STATUS_MASK) > 1)) {

		memset(ha->nvram, 0, sizeof (IPS_NVRAM_P5));

		return (0);
	}
	if (!write)
		memcpy(ha->nvram, ha->ioctl_data, sizeof(*ha->nvram));
	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_clear_adapter                                          */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   Clear the stripe lock tables                                           */
/*                                                                          */
/****************************************************************************/
static int
ips_clear_adapter(ips_ha_t * ha, int intr)
{
	ips_scb_t *scb;
	int ret;

	METHOD_TRACE("ips_clear_adapter", 1);

	scb = &ha->scbs[ha->max_cmds - 1];

	ips_init_scb(ha, scb);

	scb->timeout = ips_reset_timeout;
	scb->cdb[0] = IPS_CMD_CONFIG_SYNC;

	scb->cmd.config_sync.op_code = IPS_CMD_CONFIG_SYNC;
	scb->cmd.config_sync.command_id = IPS_COMMAND_ID(ha, scb);
	scb->cmd.config_sync.channel = 0;
	scb->cmd.config_sync.source_target = IPS_POCL;
	scb->cmd.config_sync.reserved = 0;
	scb->cmd.config_sync.reserved2 = 0;
	scb->cmd.config_sync.reserved3 = 0;

	/* issue command */
	if (((ret =
	      ips_send_wait(ha, scb, ips_reset_timeout, intr)) == IPS_FAILURE)
	    || (ret == IPS_SUCCESS_IMM)
	    || ((scb->basic_status & IPS_GSC_STATUS_MASK) > 1))
		return (0);

	/* send unlock stripe command */
	ips_init_scb(ha, scb);

	scb->cdb[0] = IPS_CMD_ERROR_TABLE;
	scb->timeout = ips_reset_timeout;

	scb->cmd.unlock_stripe.op_code = IPS_CMD_ERROR_TABLE;
	scb->cmd.unlock_stripe.command_id = IPS_COMMAND_ID(ha, scb);
	scb->cmd.unlock_stripe.log_drv = 0;
	scb->cmd.unlock_stripe.control = IPS_CSL;
	scb->cmd.unlock_stripe.reserved = 0;
	scb->cmd.unlock_stripe.reserved2 = 0;
	scb->cmd.unlock_stripe.reserved3 = 0;

	/* issue command */
	if (((ret =
	      ips_send_wait(ha, scb, ips_cmd_timeout, intr)) == IPS_FAILURE)
	    || (ret == IPS_SUCCESS_IMM)
	    || ((scb->basic_status & IPS_GSC_STATUS_MASK) > 1))
		return (0);

	return (1);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_ffdc_reset                                             */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   FFDC: write reset info                                                 */
/*                                                                          */
/****************************************************************************/
static void
ips_ffdc_reset(ips_ha_t * ha, int intr)
{
	ips_scb_t *scb;

	METHOD_TRACE("ips_ffdc_reset", 1);

	scb = &ha->scbs[ha->max_cmds - 1];

	ips_init_scb(ha, scb);

	scb->timeout = ips_cmd_timeout;
	scb->cdb[0] = IPS_CMD_FFDC;
	scb->cmd.ffdc.op_code = IPS_CMD_FFDC;
	scb->cmd.ffdc.command_id = IPS_COMMAND_ID(ha, scb);
	scb->cmd.ffdc.reset_count = ha->reset_count;
	scb->cmd.ffdc.reset_type = 0x80;

	/* convert time to what the card wants */
	ips_fix_ffdc_time(ha, scb, ha->last_ffdc);

	/* issue command */
	ips_send_wait(ha, scb, ips_cmd_timeout, intr);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_ffdc_time                                              */
/*                                                                          */
/* Routine Description:                                                     */
/*                                                                          */
/*   FFDC: write time info                                                  */
/*                                                                          */
/****************************************************************************/
static void
ips_ffdc_time(ips_ha_t * ha)
{
	ips_scb_t *scb;

	METHOD_TRACE("ips_ffdc_time", 1);

	DEBUG_VAR(1, "(%s%d) Sending time update.", ips_name, ha->host_num);

	scb = &ha->scbs[ha->max_cmds - 1];

	ips_init_scb(ha, scb);

	scb->timeout = ips_cmd_timeout;
	scb->cdb[0] = IPS_CMD_FFDC;
	scb->cmd.ffdc.op_code = IPS_CMD_FFDC;
	scb->cmd.ffdc.command_id = IPS_COMMAND_ID(ha, scb);
	scb->cmd.ffdc.reset_count = 0;
	scb->cmd.ffdc.reset_type = 0;

	/* convert time to what the card wants */
	ips_fix_ffdc_time(ha, scb, ha->last_ffdc);

	/* issue command */
	ips_send_wait(ha, scb, ips_cmd_timeout, IPS_FFDC);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_fix_ffdc_time                                          */
/*                                                                          */
/* Routine Description:                                                     */
/*   Adjust time_t to what the card wants                                   */
/*                                                                          */
/****************************************************************************/
static void
ips_fix_ffdc_time(ips_ha_t * ha, ips_scb_t * scb, time64_t current_time)
{
	struct tm tm;

	METHOD_TRACE("ips_fix_ffdc_time", 1);

	time64_to_tm(current_time, 0, &tm);

	scb->cmd.ffdc.hour   = tm.tm_hour;
	scb->cmd.ffdc.minute = tm.tm_min;
	scb->cmd.ffdc.second = tm.tm_sec;
	scb->cmd.ffdc.yearH  = (tm.tm_year + 1900) / 100;
	scb->cmd.ffdc.yearL  = tm.tm_year % 100;
	scb->cmd.ffdc.month  = tm.tm_mon + 1;
	scb->cmd.ffdc.day    = tm.tm_mday;
}

/****************************************************************************
 * BIOS Flash Routines                                                      *
 ****************************************************************************/

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_erase_bios                                             */
/*                                                                          */
/* Routine Description:                                                     */
/*   Erase the BIOS on the adapter                                          */
/*                                                                          */
/****************************************************************************/
static int
ips_erase_bios(ips_ha_t * ha)
{
	int timeout;
	uint8_t status = 0;

	METHOD_TRACE("ips_erase_bios", 1);

	status = 0;

	/* Clear the status register */
	outl(0, ha->io_addr + IPS_REG_FLAP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	outb(0x50, ha->io_addr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	/* Erase Setup */
	outb(0x20, ha->io_addr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	/* Erase Confirm */
	outb(0xD0, ha->io_addr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	/* Erase Status */
	outb(0x70, ha->io_addr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	timeout = 80000;	/* 80 seconds */

	while (timeout > 0) {
		if (ha->pcidev->revision == IPS_REVID_TROMBONE64) {
			outl(0, ha->io_addr + IPS_REG_FLAP);
			udelay(25);	/* 25 us */
		}

		status = inb(ha->io_addr + IPS_REG_FLDP);

		if (status & 0x80)
			break;

		MDELAY(1);
		timeout--;
	}

	/* check for timeout */
	if (timeout <= 0) {
		/* timeout */

		/* try to suspend the erase */
		outb(0xB0, ha->io_addr + IPS_REG_FLDP);
		if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
			udelay(25);	/* 25 us */

		/* wait for 10 seconds */
		timeout = 10000;
		while (timeout > 0) {
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64) {
				outl(0, ha->io_addr + IPS_REG_FLAP);
				udelay(25);	/* 25 us */
			}

			status = inb(ha->io_addr + IPS_REG_FLDP);

			if (status & 0xC0)
				break;

			MDELAY(1);
			timeout--;
		}

		return (1);
	}

	/* check for valid VPP */
	if (status & 0x08)
		/* VPP failure */
		return (1);

	/* check for successful flash */
	if (status & 0x30)
		/* sequence error */
		return (1);

	/* Otherwise, we were successful */
	/* clear status */
	outb(0x50, ha->io_addr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	/* enable reads */
	outb(0xFF, ha->io_addr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_erase_bios_memio                                       */
/*                                                                          */
/* Routine Description:                                                     */
/*   Erase the BIOS on the adapter                                          */
/*                                                                          */
/****************************************************************************/
static int
ips_erase_bios_memio(ips_ha_t * ha)
{
	int timeout;
	uint8_t status;

	METHOD_TRACE("ips_erase_bios_memio", 1);

	status = 0;

	/* Clear the status register */
	writel(0, ha->mem_ptr + IPS_REG_FLAP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	writeb(0x50, ha->mem_ptr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	/* Erase Setup */
	writeb(0x20, ha->mem_ptr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	/* Erase Confirm */
	writeb(0xD0, ha->mem_ptr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	/* Erase Status */
	writeb(0x70, ha->mem_ptr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	timeout = 80000;	/* 80 seconds */

	while (timeout > 0) {
		if (ha->pcidev->revision == IPS_REVID_TROMBONE64) {
			writel(0, ha->mem_ptr + IPS_REG_FLAP);
			udelay(25);	/* 25 us */
		}

		status = readb(ha->mem_ptr + IPS_REG_FLDP);

		if (status & 0x80)
			break;

		MDELAY(1);
		timeout--;
	}

	/* check for timeout */
	if (timeout <= 0) {
		/* timeout */

		/* try to suspend the erase */
		writeb(0xB0, ha->mem_ptr + IPS_REG_FLDP);
		if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
			udelay(25);	/* 25 us */

		/* wait for 10 seconds */
		timeout = 10000;
		while (timeout > 0) {
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64) {
				writel(0, ha->mem_ptr + IPS_REG_FLAP);
				udelay(25);	/* 25 us */
			}

			status = readb(ha->mem_ptr + IPS_REG_FLDP);

			if (status & 0xC0)
				break;

			MDELAY(1);
			timeout--;
		}

		return (1);
	}

	/* check for valid VPP */
	if (status & 0x08)
		/* VPP failure */
		return (1);

	/* check for successful flash */
	if (status & 0x30)
		/* sequence error */
		return (1);

	/* Otherwise, we were successful */
	/* clear status */
	writeb(0x50, ha->mem_ptr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	/* enable reads */
	writeb(0xFF, ha->mem_ptr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_program_bios                                           */
/*                                                                          */
/* Routine Description:                                                     */
/*   Program the BIOS on the adapter                                        */
/*                                                                          */
/****************************************************************************/
static int
ips_program_bios(ips_ha_t * ha, char *buffer, uint32_t buffersize,
		 uint32_t offset)
{
	int i;
	int timeout;
	uint8_t status = 0;

	METHOD_TRACE("ips_program_bios", 1);

	status = 0;

	for (i = 0; i < buffersize; i++) {
		/* write a byte */
		outl(i + offset, ha->io_addr + IPS_REG_FLAP);
		if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
			udelay(25);	/* 25 us */

		outb(0x40, ha->io_addr + IPS_REG_FLDP);
		if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
			udelay(25);	/* 25 us */

		outb(buffer[i], ha->io_addr + IPS_REG_FLDP);
		if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
			udelay(25);	/* 25 us */

		/* wait up to one second */
		timeout = 1000;
		while (timeout > 0) {
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64) {
				outl(0, ha->io_addr + IPS_REG_FLAP);
				udelay(25);	/* 25 us */
			}

			status = inb(ha->io_addr + IPS_REG_FLDP);

			if (status & 0x80)
				break;

			MDELAY(1);
			timeout--;
		}

		if (timeout == 0) {
			/* timeout error */
			outl(0, ha->io_addr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			outb(0xFF, ha->io_addr + IPS_REG_FLDP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			return (1);
		}

		/* check the status */
		if (status & 0x18) {
			/* programming error */
			outl(0, ha->io_addr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			outb(0xFF, ha->io_addr + IPS_REG_FLDP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			return (1);
		}
	}			/* end for */

	/* Enable reading */
	outl(0, ha->io_addr + IPS_REG_FLAP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	outb(0xFF, ha->io_addr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_program_bios_memio                                     */
/*                                                                          */
/* Routine Description:                                                     */
/*   Program the BIOS on the adapter                                        */
/*                                                                          */
/****************************************************************************/
static int
ips_program_bios_memio(ips_ha_t * ha, char *buffer, uint32_t buffersize,
		       uint32_t offset)
{
	int i;
	int timeout;
	uint8_t status = 0;

	METHOD_TRACE("ips_program_bios_memio", 1);

	status = 0;

	for (i = 0; i < buffersize; i++) {
		/* write a byte */
		writel(i + offset, ha->mem_ptr + IPS_REG_FLAP);
		if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
			udelay(25);	/* 25 us */

		writeb(0x40, ha->mem_ptr + IPS_REG_FLDP);
		if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
			udelay(25);	/* 25 us */

		writeb(buffer[i], ha->mem_ptr + IPS_REG_FLDP);
		if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
			udelay(25);	/* 25 us */

		/* wait up to one second */
		timeout = 1000;
		while (timeout > 0) {
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64) {
				writel(0, ha->mem_ptr + IPS_REG_FLAP);
				udelay(25);	/* 25 us */
			}

			status = readb(ha->mem_ptr + IPS_REG_FLDP);

			if (status & 0x80)
				break;

			MDELAY(1);
			timeout--;
		}

		if (timeout == 0) {
			/* timeout error */
			writel(0, ha->mem_ptr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			writeb(0xFF, ha->mem_ptr + IPS_REG_FLDP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			return (1);
		}

		/* check the status */
		if (status & 0x18) {
			/* programming error */
			writel(0, ha->mem_ptr + IPS_REG_FLAP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			writeb(0xFF, ha->mem_ptr + IPS_REG_FLDP);
			if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
				udelay(25);	/* 25 us */

			return (1);
		}
	}			/* end for */

	/* Enable reading */
	writel(0, ha->mem_ptr + IPS_REG_FLAP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	writeb(0xFF, ha->mem_ptr + IPS_REG_FLDP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_verify_bios                                            */
/*                                                                          */
/* Routine Description:                                                     */
/*   Verify the BIOS on the adapter                                         */
/*                                                                          */
/****************************************************************************/
static int
ips_verify_bios(ips_ha_t * ha, char *buffer, uint32_t buffersize,
		uint32_t offset)
{
	uint8_t checksum;
	int i;

	METHOD_TRACE("ips_verify_bios", 1);

	/* test 1st byte */
	outl(0, ha->io_addr + IPS_REG_FLAP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	if (inb(ha->io_addr + IPS_REG_FLDP) != 0x55)
		return (1);

	outl(1, ha->io_addr + IPS_REG_FLAP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */
	if (inb(ha->io_addr + IPS_REG_FLDP) != 0xAA)
		return (1);

	checksum = 0xff;
	for (i = 2; i < buffersize; i++) {

		outl(i + offset, ha->io_addr + IPS_REG_FLAP);
		if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
			udelay(25);	/* 25 us */

		checksum = (uint8_t) checksum + inb(ha->io_addr + IPS_REG_FLDP);
	}

	if (checksum != 0)
		/* failure */
		return (1);
	else
		/* success */
		return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_verify_bios_memio                                      */
/*                                                                          */
/* Routine Description:                                                     */
/*   Verify the BIOS on the adapter                                         */
/*                                                                          */
/****************************************************************************/
static int
ips_verify_bios_memio(ips_ha_t * ha, char *buffer, uint32_t buffersize,
		      uint32_t offset)
{
	uint8_t checksum;
	int i;

	METHOD_TRACE("ips_verify_bios_memio", 1);

	/* test 1st byte */
	writel(0, ha->mem_ptr + IPS_REG_FLAP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */

	if (readb(ha->mem_ptr + IPS_REG_FLDP) != 0x55)
		return (1);

	writel(1, ha->mem_ptr + IPS_REG_FLAP);
	if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
		udelay(25);	/* 25 us */
	if (readb(ha->mem_ptr + IPS_REG_FLDP) != 0xAA)
		return (1);

	checksum = 0xff;
	for (i = 2; i < buffersize; i++) {

		writel(i + offset, ha->mem_ptr + IPS_REG_FLAP);
		if (ha->pcidev->revision == IPS_REVID_TROMBONE64)
			udelay(25);	/* 25 us */

		checksum =
		    (uint8_t) checksum + readb(ha->mem_ptr + IPS_REG_FLDP);
	}

	if (checksum != 0)
		/* failure */
		return (1);
	else
		/* success */
		return (0);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_abort_init                                             */
/*                                                                          */
/* Routine Description:                                                     */
/*   cleanup routine for a failed adapter initialization                    */
/****************************************************************************/
static int
ips_abort_init(ips_ha_t * ha, int index)
{
	ha->active = 0;
	ips_free(ha);
	ips_ha[index] = NULL;
	ips_sh[index] = NULL;
	return -1;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_shift_controllers                                      */
/*                                                                          */
/* Routine Description:                                                     */
/*   helper function for ordering adapters                                  */
/****************************************************************************/
static void
ips_shift_controllers(int lowindex, int highindex)
{
	ips_ha_t *ha_sav = ips_ha[highindex];
	struct Scsi_Host *sh_sav = ips_sh[highindex];
	int i;

	for (i = highindex; i > lowindex; i--) {
		ips_ha[i] = ips_ha[i - 1];
		ips_sh[i] = ips_sh[i - 1];
		ips_ha[i]->host_num = i;
	}
	ha_sav->host_num = lowindex;
	ips_ha[lowindex] = ha_sav;
	ips_sh[lowindex] = sh_sav;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_order_controllers                                      */
/*                                                                          */
/* Routine Description:                                                     */
/*   place controllers is the "proper" boot order                           */
/****************************************************************************/
static void
ips_order_controllers(void)
{
	int i, j, tmp, position = 0;
	IPS_NVRAM_P5 *nvram;
	if (!ips_ha[0])
		return;
	nvram = ips_ha[0]->nvram;

	if (nvram->adapter_order[0]) {
		for (i = 1; i <= nvram->adapter_order[0]; i++) {
			for (j = position; j < ips_num_controllers; j++) {
				switch (ips_ha[j]->ad_type) {
				case IPS_ADTYPE_SERVERAID6M:
				case IPS_ADTYPE_SERVERAID7M:
					if (nvram->adapter_order[i] == 'M') {
						ips_shift_controllers(position,
								      j);
						position++;
					}
					break;
				case IPS_ADTYPE_SERVERAID4L:
				case IPS_ADTYPE_SERVERAID4M:
				case IPS_ADTYPE_SERVERAID4MX:
				case IPS_ADTYPE_SERVERAID4LX:
					if (nvram->adapter_order[i] == 'N') {
						ips_shift_controllers(position,
								      j);
						position++;
					}
					break;
				case IPS_ADTYPE_SERVERAID6I:
				case IPS_ADTYPE_SERVERAID5I2:
				case IPS_ADTYPE_SERVERAID5I1:
				case IPS_ADTYPE_SERVERAID7k:
					if (nvram->adapter_order[i] == 'S') {
						ips_shift_controllers(position,
								      j);
						position++;
					}
					break;
				case IPS_ADTYPE_SERVERAID:
				case IPS_ADTYPE_SERVERAID2:
				case IPS_ADTYPE_NAVAJO:
				case IPS_ADTYPE_KIOWA:
				case IPS_ADTYPE_SERVERAID3L:
				case IPS_ADTYPE_SERVERAID3:
				case IPS_ADTYPE_SERVERAID4H:
					if (nvram->adapter_order[i] == 'A') {
						ips_shift_controllers(position,
								      j);
						position++;
					}
					break;
				default:
					break;
				}
			}
		}
		/* if adapter_order[0], then ordering is complete */
		return;
	}
	/* old bios, use older ordering */
	tmp = 0;
	for (i = position; i < ips_num_controllers; i++) {
		if (ips_ha[i]->ad_type == IPS_ADTYPE_SERVERAID5I2 ||
		    ips_ha[i]->ad_type == IPS_ADTYPE_SERVERAID5I1) {
			ips_shift_controllers(position, i);
			position++;
			tmp = 1;
		}
	}
	/* if there were no 5I cards, then don't do any extra ordering */
	if (!tmp)
		return;
	for (i = position; i < ips_num_controllers; i++) {
		if (ips_ha[i]->ad_type == IPS_ADTYPE_SERVERAID4L ||
		    ips_ha[i]->ad_type == IPS_ADTYPE_SERVERAID4M ||
		    ips_ha[i]->ad_type == IPS_ADTYPE_SERVERAID4LX ||
		    ips_ha[i]->ad_type == IPS_ADTYPE_SERVERAID4MX) {
			ips_shift_controllers(position, i);
			position++;
		}
	}

	return;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_register_scsi                                          */
/*                                                                          */
/* Routine Description:                                                     */
/*   perform any registration and setup with the scsi layer                 */
/****************************************************************************/
static int
ips_register_scsi(int index)
{
	struct Scsi_Host *sh;
	ips_ha_t *ha, *oldha = ips_ha[index];
	sh = scsi_host_alloc(&ips_driver_template, sizeof (ips_ha_t));
	if (!sh) {
		IPS_PRINTK(KERN_WARNING, oldha->pcidev,
			   "Unable to register controller with SCSI subsystem\n");
		return -1;
	}
	ha = IPS_HA(sh);
	memcpy(ha, oldha, sizeof (ips_ha_t));
	free_irq(oldha->pcidev->irq, oldha);
	/* Install the interrupt handler with the new ha */
	if (request_irq(ha->pcidev->irq, do_ipsintr, IRQF_SHARED, ips_name, ha)) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "Unable to install interrupt handler\n");
		goto err_out_sh;
	}

	kfree(oldha);

	/* Store away needed values for later use */
	sh->unique_id = (ha->io_addr) ? ha->io_addr : ha->mem_addr;
	sh->sg_tablesize = sh->hostt->sg_tablesize;
	sh->can_queue = sh->hostt->can_queue;
	sh->cmd_per_lun = sh->hostt->cmd_per_lun;
	sh->max_sectors = 128;

	sh->max_id = ha->ntargets;
	sh->max_lun = ha->nlun;
	sh->max_channel = ha->nbus - 1;
	sh->can_queue = ha->max_cmds - 1;

	if (scsi_add_host(sh, &ha->pcidev->dev))
		goto err_out;

	ips_sh[index] = sh;
	ips_ha[index] = ha;

	scsi_scan_host(sh);

	return 0;

err_out:
	free_irq(ha->pcidev->irq, ha);
err_out_sh:
	scsi_host_put(sh);
	return -1;
}

/*---------------------------------------------------------------------------*/
/*   Routine Name: ips_remove_device                                         */
/*                                                                           */
/*   Routine Description:                                                    */
/*     Remove one Adapter ( Hot Plugging )                                   */
/*---------------------------------------------------------------------------*/
static void
ips_remove_device(struct pci_dev *pci_dev)
{
	struct Scsi_Host *sh = pci_get_drvdata(pci_dev);

	pci_set_drvdata(pci_dev, NULL);

	ips_release(sh);

	pci_release_regions(pci_dev);
	pci_disable_device(pci_dev);
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_module_init                                            */
/*                                                                          */
/* Routine Description:                                                     */
/*   function called on module load                                         */
/****************************************************************************/
static int __init
ips_module_init(void)
{
#if !defined(__i386__) && !defined(__ia64__) && !defined(__x86_64__)
	printk(KERN_ERR "ips: This driver has only been tested on the x86/ia64/x86_64 platforms\n");
	add_taint(TAINT_CPU_OUT_OF_SPEC, LOCKDEP_STILL_OK);
#endif

	if (pci_register_driver(&ips_pci_driver) < 0)
		return -ENODEV;
	ips_driver_template.module = THIS_MODULE;
	ips_order_controllers();
	if (!ips_detect(&ips_driver_template)) {
		pci_unregister_driver(&ips_pci_driver);
		return -ENODEV;
	}
	register_reboot_notifier(&ips_notifier);
	return 0;
}

/****************************************************************************/
/*                                                                          */
/* Routine Name: ips_module_exit                                            */
/*                                                                          */
/* Routine Description:                                                     */
/*   function called on module unload                                       */
/****************************************************************************/
static void __exit
ips_module_exit(void)
{
	pci_unregister_driver(&ips_pci_driver);
	unregister_reboot_notifier(&ips_notifier);
}

module_init(ips_module_init);
module_exit(ips_module_exit);

/*---------------------------------------------------------------------------*/
/*   Routine Name: ips_insert_device                                         */
/*                                                                           */
/*   Routine Description:                                                    */
/*     Add One Adapter ( Hot Plug )                                          */
/*                                                                           */
/*   Return Value:                                                           */
/*     0 if Successful, else non-zero                                        */
/*---------------------------------------------------------------------------*/
static int
ips_insert_device(struct pci_dev *pci_dev, const struct pci_device_id *ent)
{
	int index = -1;
	int rc;

	METHOD_TRACE("ips_insert_device", 1);
	rc = pci_enable_device(pci_dev);
	if (rc)
		return rc;

	rc = pci_request_regions(pci_dev, "ips");
	if (rc)
		goto err_out;

	rc = ips_init_phase1(pci_dev, &index);
	if (rc == SUCCESS)
		rc = ips_init_phase2(index);

	if (ips_hotplug)
		if (ips_register_scsi(index)) {
			ips_free(ips_ha[index]);
			rc = -1;
		}

	if (rc == SUCCESS)
		ips_num_controllers++;

	ips_next_controller = ips_num_controllers;

	if (rc < 0) {
		rc = -ENODEV;
		goto err_out_regions;
	}

	pci_set_drvdata(pci_dev, ips_sh[index]);
	return 0;

err_out_regions:
	pci_release_regions(pci_dev);
err_out:
	pci_disable_device(pci_dev);
	return rc;
}

/*---------------------------------------------------------------------------*/
/*   Routine Name: ips_init_phase1                                           */
/*                                                                           */
/*   Routine Description:                                                    */
/*     Adapter Initialization                                                */
/*                                                                           */
/*   Return Value:                                                           */
/*     0 if Successful, else non-zero                                        */
/*---------------------------------------------------------------------------*/
static int
ips_init_phase1(struct pci_dev *pci_dev, int *indexPtr)
{
	ips_ha_t *ha;
	uint32_t io_addr;
	uint32_t mem_addr;
	uint32_t io_len;
	uint32_t mem_len;
	uint8_t bus;
	uint8_t func;
	int j;
	int index;
	dma_addr_t dma_address;
	char __iomem *ioremap_ptr;
	char __iomem *mem_ptr;
	uint32_t IsDead;

	METHOD_TRACE("ips_init_phase1", 1);
	index = IPS_MAX_ADAPTERS;
	for (j = 0; j < IPS_MAX_ADAPTERS; j++) {
		if (ips_ha[j] == NULL) {
			index = j;
			break;
		}
	}

	if (index >= IPS_MAX_ADAPTERS)
		return -1;

	/* stuff that we get in dev */
	bus = pci_dev->bus->number;
	func = pci_dev->devfn;

	/* Init MEM/IO addresses to 0 */
	mem_addr = 0;
	io_addr = 0;
	mem_len = 0;
	io_len = 0;

	for (j = 0; j < 2; j++) {
		if (!pci_resource_start(pci_dev, j))
			break;

		if (pci_resource_flags(pci_dev, j) & IORESOURCE_IO) {
			io_addr = pci_resource_start(pci_dev, j);
			io_len = pci_resource_len(pci_dev, j);
		} else {
			mem_addr = pci_resource_start(pci_dev, j);
			mem_len = pci_resource_len(pci_dev, j);
		}
	}

	/* setup memory mapped area (if applicable) */
	if (mem_addr) {
		uint32_t base;
		uint32_t offs;

		base = mem_addr & PAGE_MASK;
		offs = mem_addr - base;
		ioremap_ptr = ioremap(base, PAGE_SIZE);
		if (!ioremap_ptr)
			return -1;
		mem_ptr = ioremap_ptr + offs;
	} else {
		ioremap_ptr = NULL;
		mem_ptr = NULL;
	}

	/* found a controller */
	ha = kzalloc(sizeof (ips_ha_t), GFP_KERNEL);
	if (ha == NULL) {
		IPS_PRINTK(KERN_WARNING, pci_dev,
			   "Unable to allocate temporary ha struct\n");
		return -1;
	}

	ips_sh[index] = NULL;
	ips_ha[index] = ha;
	ha->active = 1;

	/* Store info in HA structure */
	ha->io_addr = io_addr;
	ha->io_len = io_len;
	ha->mem_addr = mem_addr;
	ha->mem_len = mem_len;
	ha->mem_ptr = mem_ptr;
	ha->ioremap_ptr = ioremap_ptr;
	ha->host_num = (uint32_t) index;
	ha->slot_num = PCI_SLOT(pci_dev->devfn);
	ha->pcidev = pci_dev;

	/*
	 * Set the pci_dev's dma_mask.  Not all adapters support 64bit
	 * addressing so don't enable it if the adapter can't support
	 * it!  Also, don't use 64bit addressing if dma addresses
	 * are guaranteed to be < 4G.
	 */
	if (sizeof(dma_addr_t) > 4 && IPS_HAS_ENH_SGLIST(ha) &&
	    !dma_set_mask(&ha->pcidev->dev, DMA_BIT_MASK(64))) {
		(ha)->flags |= IPS_HA_ENH_SG;
	} else {
		if (dma_set_mask(&ha->pcidev->dev, DMA_BIT_MASK(32)) != 0) {
			printk(KERN_WARNING "Unable to set DMA Mask\n");
			return ips_abort_init(ha, index);
		}
	}
	if(ips_cd_boot && !ips_FlashData){
		ips_FlashData = dma_alloc_coherent(&pci_dev->dev,
				PAGE_SIZE << 7, &ips_flashbusaddr, GFP_KERNEL);
	}

	ha->enq = dma_alloc_coherent(&pci_dev->dev, sizeof (IPS_ENQ),
			&ha->enq_busaddr, GFP_KERNEL);
	if (!ha->enq) {
		IPS_PRINTK(KERN_WARNING, pci_dev,
			   "Unable to allocate host inquiry structure\n");
		return ips_abort_init(ha, index);
	}

	ha->adapt = dma_alloc_coherent(&pci_dev->dev,
			sizeof (IPS_ADAPTER) + sizeof (IPS_IO_CMD),
			&dma_address, GFP_KERNEL);
	if (!ha->adapt) {
		IPS_PRINTK(KERN_WARNING, pci_dev,
			   "Unable to allocate host adapt & dummy structures\n");
		return ips_abort_init(ha, index);
	}
	ha->adapt->hw_status_start = dma_address;
	ha->dummy = (void *) (ha->adapt + 1);



	ha->logical_drive_info = dma_alloc_coherent(&pci_dev->dev,
			sizeof (IPS_LD_INFO), &dma_address, GFP_KERNEL);
	if (!ha->logical_drive_info) {
		IPS_PRINTK(KERN_WARNING, pci_dev,
			   "Unable to allocate logical drive info structure\n");
		return ips_abort_init(ha, index);
	}
	ha->logical_drive_info_dma_addr = dma_address;


	ha->conf = kmalloc(sizeof (IPS_CONF), GFP_KERNEL);

	if (!ha->conf) {
		IPS_PRINTK(KERN_WARNING, pci_dev,
			   "Unable to allocate host conf structure\n");
		return ips_abort_init(ha, index);
	}

	ha->nvram = kmalloc(sizeof (IPS_NVRAM_P5), GFP_KERNEL);

	if (!ha->nvram) {
		IPS_PRINTK(KERN_WARNING, pci_dev,
			   "Unable to allocate host NVRAM structure\n");
		return ips_abort_init(ha, index);
	}

	ha->subsys = kmalloc(sizeof (IPS_SUBSYS), GFP_KERNEL);

	if (!ha->subsys) {
		IPS_PRINTK(KERN_WARNING, pci_dev,
			   "Unable to allocate host subsystem structure\n");
		return ips_abort_init(ha, index);
	}

	/* the ioctl buffer is now used during adapter initialization, so its
	 * successful allocation is now required */
	if (ips_ioctlsize < PAGE_SIZE)
		ips_ioctlsize = PAGE_SIZE;

	ha->ioctl_data = dma_alloc_coherent(&pci_dev->dev, ips_ioctlsize,
			&ha->ioctl_busaddr, GFP_KERNEL);
	ha->ioctl_len = ips_ioctlsize;
	if (!ha->ioctl_data) {
		IPS_PRINTK(KERN_WARNING, pci_dev,
			   "Unable to allocate IOCTL data\n");
		return ips_abort_init(ha, index);
	}

	/*
	 * Setup Functions
	 */
	ips_setup_funclist(ha);

	if ((IPS_IS_MORPHEUS(ha)) || (IPS_IS_MARCO(ha))) {
		/* If Morpheus appears dead, reset it */
		IsDead = readl(ha->mem_ptr + IPS_REG_I960_MSG1);
		if (IsDead == 0xDEADBEEF) {
			ips_reset_morpheus(ha);
		}
	}

	/*
	 * Initialize the card if it isn't already
	 */

	if (!(*ha->func.isinit) (ha)) {
		if (!(*ha->func.init) (ha)) {
			/*
			 * Initialization failed
			 */
			IPS_PRINTK(KERN_WARNING, pci_dev,
				   "Unable to initialize controller\n");
			return ips_abort_init(ha, index);
		}
	}

	*indexPtr = index;
	return SUCCESS;
}

/*---------------------------------------------------------------------------*/
/*   Routine Name: ips_init_phase2                                           */
/*                                                                           */
/*   Routine Description:                                                    */
/*     Adapter Initialization Phase 2                                        */
/*                                                                           */
/*   Return Value:                                                           */
/*     0 if Successful, else non-zero                                        */
/*---------------------------------------------------------------------------*/
static int
ips_init_phase2(int index)
{
	ips_ha_t *ha;

	ha = ips_ha[index];

	METHOD_TRACE("ips_init_phase2", 1);
	if (!ha->active) {
		ips_ha[index] = NULL;
		return -1;
	}

	/* Install the interrupt handler */
	if (request_irq(ha->pcidev->irq, do_ipsintr, IRQF_SHARED, ips_name, ha)) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "Unable to install interrupt handler\n");
		return ips_abort_init(ha, index);
	}

	/*
	 * Allocate a temporary SCB for initialization
	 */
	ha->max_cmds = 1;
	if (!ips_allocatescbs(ha)) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "Unable to allocate a CCB\n");
		free_irq(ha->pcidev->irq, ha);
		return ips_abort_init(ha, index);
	}

	if (!ips_hainit(ha)) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "Unable to initialize controller\n");
		free_irq(ha->pcidev->irq, ha);
		return ips_abort_init(ha, index);
	}
	/* Free the temporary SCB */
	ips_deallocatescbs(ha, 1);

	/* allocate CCBs */
	if (!ips_allocatescbs(ha)) {
		IPS_PRINTK(KERN_WARNING, ha->pcidev,
			   "Unable to allocate CCBs\n");
		free_irq(ha->pcidev->irq, ha);
		return ips_abort_init(ha, index);
	}

	return SUCCESS;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IBM ServeRAID Adapter Driver " IPS_VER_STRING);
MODULE_VERSION(IPS_VER_STRING);


/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
