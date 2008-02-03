/* 
   3w-xxxx.c -- 3ware Storage Controller device driver for Linux.

   Written By: Adam Radford <linuxraid@amcc.com>
   Modifications By: Joel Jacobson <linux@3ware.com>
   		     Arnaldo Carvalho de Melo <acme@conectiva.com.br>
                     Brad Strand <linux@3ware.com>

   Copyright (C) 1999-2007 3ware Inc.

   Kernel compatiblity By: 	Andre Hedrick <andre@suse.com>
   Non-Copyright (C) 2000	Andre Hedrick <andre@suse.com>
   
   Further tiny build fixes and trivial hoovering    Alan Cox

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,           
   but WITHOUT ANY WARRANTY; without even the implied warranty of            
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             
   GNU General Public License for more details.                              

   NO WARRANTY                                                               
   THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR        
   CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT      
   LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,      
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is    
   solely responsible for determining the appropriateness of using and       
   distributing the Program and assumes all risks associated with its        
   exercise of rights under this Agreement, including but not limited to     
   the risks and costs of program errors, damage to or loss of data,         
   programs or equipment, and unavailability or interruption of operations.  

   DISCLAIMER OF LIABILITY                                                   
   NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY   
   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL        
   DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND   
   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR     
   TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE    
   USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED  
   HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES             

   You should have received a copy of the GNU General Public License         
   along with this program; if not, write to the Free Software               
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 

   Bugs/Comments/Suggestions should be mailed to:                            
   linuxraid@amcc.com

   For more information, goto:
   http://www.amcc.com

   History
   -------
   0.1.000 -     Initial release.
   0.4.000 -     Added support for Asynchronous Event Notification through
                 ioctls for 3DM.
   1.0.000 -     Added DPO & FUA bit support for WRITE_10 & WRITE_6 cdb
                 to disable drive write-cache before writes.
   1.1.000 -     Fixed performance bug with DPO & FUA not existing for WRITE_6.
   1.2.000 -     Added support for clean shutdown notification/feature table.
   1.02.00.001 - Added support for full command packet posts through ioctls
                 for 3DM.
                 Bug fix so hot spare drives don't show up.
   1.02.00.002 - Fix bug with tw_setfeature() call that caused oops on some
                 systems.
   08/21/00    - release previously allocated resources on failure at
                 tw_allocate_memory (acme)
   1.02.00.003 - Fix tw_interrupt() to report error to scsi layer when
                 controller status is non-zero.
                 Added handling of request_sense opcode.
                 Fix possible null pointer dereference in 
                 tw_reset_device_extension()
   1.02.00.004 - Add support for device id of 3ware 7000 series controllers.
                 Make tw_setfeature() call with interrupts disabled.
                 Register interrupt handler before enabling interrupts.
                 Clear attention interrupt before draining aen queue.
   1.02.00.005 - Allocate bounce buffers and custom queue depth for raid5 for
                 6000 and 5000 series controllers.
                 Reduce polling mdelays causing problems on some systems.
                 Fix use_sg = 1 calculation bug.
                 Check for scsi_register returning NULL.
                 Add aen count to /proc/scsi/3w-xxxx.
                 Remove aen code unit masking in tw_aen_complete().
   1.02.00.006 - Remove unit from printk in tw_scsi_eh_abort(), causing
                 possible oops.
                 Fix possible null pointer dereference in tw_scsi_queue()
                 if done function pointer was invalid.
   1.02.00.007 - Fix possible null pointer dereferences in tw_ioctl().
                 Remove check for invalid done function pointer from
                 tw_scsi_queue().
   1.02.00.008 - Set max sectors per io to TW_MAX_SECTORS in tw_findcards().
                 Add tw_decode_error() for printing readable error messages.
                 Print some useful information on certain aen codes.
                 Add tw_decode_bits() for interpreting status register output.
                 Make scsi_set_pci_device() for kernels >= 2.4.4
                 Fix bug where aen's could be lost before a reset.
                 Re-add spinlocks in tw_scsi_detect().
                 Fix possible null pointer dereference in tw_aen_drain_queue()
                 during initialization.
                 Clear pci parity errors during initialization and during io.
   1.02.00.009 - Remove redundant increment in tw_state_request_start().
                 Add ioctl support for direct ATA command passthru.
                 Add entire aen code string list.
   1.02.00.010 - Cleanup queueing code, fix jbod thoughput.
                 Fix get_param for specific units.
   1.02.00.011 - Fix bug in tw_aen_complete() where aen's could be lost.
                 Fix tw_aen_drain_queue() to display useful info at init.
                 Set tw_host->max_id for 12 port cards.
                 Add ioctl support for raw command packet post from userspace
                 with sglist fragments (parameter and io).
   1.02.00.012 - Fix read capacity to under report by 1 sector to fix get
                 last sector ioctl.
   1.02.00.013 - Fix bug where more AEN codes weren't coming out during
                 driver initialization.
                 Improved handling of PCI aborts.
   1.02.00.014 - Fix bug in tw_findcards() where AEN code could be lost.
                 Increase timeout in tw_aen_drain_queue() to 30 seconds.
   1.02.00.015 - Re-write raw command post with data ioctl method.
                 Remove raid5 bounce buffers for raid5 for 6XXX for kernel 2.5
                 Add tw_map/unmap_scsi_sg/single_data() for kernel 2.5
                 Replace io_request_lock with host_lock for kernel 2.5
                 Set max_cmd_len to 16 for 3dm for kernel 2.5
   1.02.00.016 - Set host->max_sectors back up to 256.
   1.02.00.017 - Modified pci parity error handling/clearing from config space
                 during initialization.
   1.02.00.018 - Better handling of request sense opcode and sense information
                 for failed commands.  Add tw_decode_sense().
                 Replace all mdelay()'s with scsi_sleep().
   1.02.00.019 - Revert mdelay's and scsi_sleep's, this caused problems on
                 some SMP systems.
   1.02.00.020 - Add pci_set_dma_mask(), rewrite kmalloc()/virt_to_bus() to
                 pci_alloc/free_consistent().
                 Better alignment checking in tw_allocate_memory().
                 Cleanup tw_initialize_device_extension().
   1.02.00.021 - Bump cmd_per_lun in SHT to 255 for better jbod performance.
                 Improve handling of errors in tw_interrupt().
                 Add handling/clearing of controller queue error.
                 Empty stale responses before draining aen queue.
                 Fix tw_scsi_eh_abort() to not reset on every io abort.
                 Set can_queue in SHT to 255 to prevent hang from AEN.
   1.02.00.022 - Fix possible null pointer dereference in tw_scsi_release().
   1.02.00.023 - Fix bug in tw_aen_drain_queue() where unit # was always zero.
   1.02.00.024 - Add severity levels to AEN strings.
   1.02.00.025 - Fix command interrupt spurious error messages.
                 Fix bug in raw command post with data ioctl method.
                 Fix bug where rollcall sometimes failed with cable errors.
                 Print unit # on all command timeouts.
   1.02.00.026 - Fix possible infinite retry bug with power glitch induced
                 drive timeouts.
                 Cleanup some AEN severity levels.
   1.02.00.027 - Add drive not supported AEN code for SATA controllers.
                 Remove spurious unknown ioctl error message.
   1.02.00.028 - Fix bug where multiple controllers with no units were the
                 same card number.
                 Fix bug where cards were being shut down more than once.
   1.02.00.029 - Add missing pci_free_consistent() in tw_allocate_memory().
                 Replace pci_map_single() with pci_map_page() for highmem.
                 Check for tw_setfeature() failure.
   1.02.00.030 - Make driver 64-bit clean.
   1.02.00.031 - Cleanup polling timeouts/routines in several places.
                 Add support for mode sense opcode.
                 Add support for cache mode page.
                 Add support for synchronize cache opcode.
   1.02.00.032 - Fix small multicard rollcall bug.
                 Make driver stay loaded with no units for hot add/swap.
                 Add support for "twe" character device for ioctls.
                 Clean up request_id queueing code.
                 Fix tw_scsi_queue() spinlocks.
   1.02.00.033 - Fix tw_aen_complete() to not queue 'queue empty' AEN's.
                 Initialize queues correctly when loading with no valid units.
   1.02.00.034 - Fix tw_decode_bits() to handle multiple errors.
                 Add support for user configurable cmd_per_lun.
                 Add support for sht->slave_configure().
   1.02.00.035 - Improve tw_allocate_memory() memory allocation.
                 Fix tw_chrdev_ioctl() to sleep correctly.
   1.02.00.036 - Increase character ioctl timeout to 60 seconds.
   1.02.00.037 - Fix tw_ioctl() to handle all non-data ATA passthru cmds
                 for 'smartmontools' support.
   1.26.00.038 - Roll driver minor version to 26 to denote kernel 2.6.
                 Add support for cmds_per_lun module parameter.
   1.26.00.039 - Fix bug in tw_chrdev_ioctl() polling code.
                 Fix data_buffer_length usage in tw_chrdev_ioctl().
                 Update contact information.
   1.26.02.000 - Convert driver to pci_driver format.
   1.26.02.001 - Increase max ioctl buffer size to 512 sectors.
                 Make tw_scsi_queue() return 0 for 'Unknown scsi opcode'.
                 Fix tw_remove() to free irq handler/unregister_chrdev()
                 before shutting down card.
                 Change to new 'change_queue_depth' api.
                 Fix 'handled=1' ISR usage, remove bogus IRQ check.
   1.26.02.002 - Free irq handler in __tw_shutdown().
                 Turn on RCD bit for caching mode page.
                 Serialize reset code.
*/

#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_cmnd.h>
#include "3w-xxxx.h"

/* Globals */
#define TW_DRIVER_VERSION "1.26.02.002"
static TW_Device_Extension *tw_device_extension_list[TW_MAX_SLOT];
static int tw_device_extension_count = 0;
static int twe_major = -1;

/* Module parameters */
MODULE_AUTHOR("AMCC");
MODULE_DESCRIPTION("3ware Storage Controller Linux Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(TW_DRIVER_VERSION);

/* Function prototypes */
static int tw_reset_device_extension(TW_Device_Extension *tw_dev);

/* Functions */

/* This function will check the status register for unexpected bits */
static int tw_check_bits(u32 status_reg_value)
{
	if ((status_reg_value & TW_STATUS_EXPECTED_BITS) != TW_STATUS_EXPECTED_BITS) {  
		dprintk(KERN_WARNING "3w-xxxx: tw_check_bits(): No expected bits (0x%x).\n", status_reg_value);
		return 1;
	}
	if ((status_reg_value & TW_STATUS_UNEXPECTED_BITS) != 0) {
		dprintk(KERN_WARNING "3w-xxxx: tw_check_bits(): Found unexpected bits (0x%x).\n", status_reg_value);
		return 1;
	}

	return 0;
} /* End tw_check_bits() */

/* This function will print readable messages from status register errors */
static int tw_decode_bits(TW_Device_Extension *tw_dev, u32 status_reg_value, int print_host)
{
	char host[16];

	dprintk(KERN_WARNING "3w-xxxx: tw_decode_bits()\n");

	if (print_host)
		sprintf(host, " scsi%d:", tw_dev->host->host_no);
	else
		host[0] = '\0';

	if (status_reg_value & TW_STATUS_PCI_PARITY_ERROR) {
		printk(KERN_WARNING "3w-xxxx:%s PCI Parity Error: clearing.\n", host);
		outl(TW_CONTROL_CLEAR_PARITY_ERROR, TW_CONTROL_REG_ADDR(tw_dev));
	}

	if (status_reg_value & TW_STATUS_PCI_ABORT) {
		printk(KERN_WARNING "3w-xxxx:%s PCI Abort: clearing.\n", host);
		outl(TW_CONTROL_CLEAR_PCI_ABORT, TW_CONTROL_REG_ADDR(tw_dev));
		pci_write_config_word(tw_dev->tw_pci_dev, PCI_STATUS, TW_PCI_CLEAR_PCI_ABORT);
	}

	if (status_reg_value & TW_STATUS_QUEUE_ERROR) {
		printk(KERN_WARNING "3w-xxxx:%s Controller Queue Error: clearing.\n", host);
		outl(TW_CONTROL_CLEAR_QUEUE_ERROR, TW_CONTROL_REG_ADDR(tw_dev));
	}

	if (status_reg_value & TW_STATUS_SBUF_WRITE_ERROR) {
		printk(KERN_WARNING "3w-xxxx:%s SBUF Write Error: clearing.\n", host);
		outl(TW_CONTROL_CLEAR_SBUF_WRITE_ERROR, TW_CONTROL_REG_ADDR(tw_dev));
	}

	if (status_reg_value & TW_STATUS_MICROCONTROLLER_ERROR) {
		if (tw_dev->reset_print == 0) {
			printk(KERN_WARNING "3w-xxxx:%s Microcontroller Error: clearing.\n", host);
			tw_dev->reset_print = 1;
		}
		return 1;
	}
	
	return 0;
} /* End tw_decode_bits() */

/* This function will poll the status register for a flag */
static int tw_poll_status(TW_Device_Extension *tw_dev, u32 flag, int seconds)
{
	u32 status_reg_value;
	unsigned long before;
	int retval = 1;

	status_reg_value = inl(TW_STATUS_REG_ADDR(tw_dev));
	before = jiffies;

	if (tw_check_bits(status_reg_value))
		tw_decode_bits(tw_dev, status_reg_value, 0);

	while ((status_reg_value & flag) != flag) {
		status_reg_value = inl(TW_STATUS_REG_ADDR(tw_dev));

		if (tw_check_bits(status_reg_value))
			tw_decode_bits(tw_dev, status_reg_value, 0);

		if (time_after(jiffies, before + HZ * seconds))
			goto out;

		msleep(50);
	}
	retval = 0;
out:
	return retval;
} /* End tw_poll_status() */

/* This function will poll the status register for disappearance of a flag */
static int tw_poll_status_gone(TW_Device_Extension *tw_dev, u32 flag, int seconds)
{
	u32 status_reg_value;
	unsigned long before;
	int retval = 1;

	status_reg_value = inl(TW_STATUS_REG_ADDR(tw_dev));
	before = jiffies;

	if (tw_check_bits(status_reg_value))
		tw_decode_bits(tw_dev, status_reg_value, 0);

	while ((status_reg_value & flag) != 0) {
		status_reg_value = inl(TW_STATUS_REG_ADDR(tw_dev));

		if (tw_check_bits(status_reg_value))
			tw_decode_bits(tw_dev, status_reg_value, 0);

		if (time_after(jiffies, before + HZ * seconds))
			goto out;

		msleep(50);
	}
	retval = 0;
out:
	return retval;
} /* End tw_poll_status_gone() */

/* This function will attempt to post a command packet to the board */
static int tw_post_command_packet(TW_Device_Extension *tw_dev, int request_id)
{
	u32 status_reg_value;
	unsigned long command_que_value;

	dprintk(KERN_NOTICE "3w-xxxx: tw_post_command_packet()\n");
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	status_reg_value = inl(TW_STATUS_REG_ADDR(tw_dev));

	if (tw_check_bits(status_reg_value)) {
		dprintk(KERN_WARNING "3w-xxxx: tw_post_command_packet(): Unexpected bits.\n");
		tw_decode_bits(tw_dev, status_reg_value, 1);
	}

	if ((status_reg_value & TW_STATUS_COMMAND_QUEUE_FULL) == 0) {
		/* We successfully posted the command packet */
		outl(command_que_value, TW_COMMAND_QUEUE_REG_ADDR(tw_dev));
		tw_dev->state[request_id] = TW_S_POSTED;
		tw_dev->posted_request_count++;
		if (tw_dev->posted_request_count > tw_dev->max_posted_request_count) {
			tw_dev->max_posted_request_count = tw_dev->posted_request_count;
		}
	} else {
		/* Couldn't post the command packet, so we do it in the isr */
		if (tw_dev->state[request_id] != TW_S_PENDING) {
			tw_dev->state[request_id] = TW_S_PENDING;
			tw_dev->pending_request_count++;
			if (tw_dev->pending_request_count > tw_dev->max_pending_request_count) {
				tw_dev->max_pending_request_count = tw_dev->pending_request_count;
			}
			tw_dev->pending_queue[tw_dev->pending_tail] = request_id;
			if (tw_dev->pending_tail == TW_Q_LENGTH-1) {
				tw_dev->pending_tail = TW_Q_START;
			} else {
				tw_dev->pending_tail = tw_dev->pending_tail + 1;
			}
		} 
		TW_UNMASK_COMMAND_INTERRUPT(tw_dev);
		return 1;
	}
	return 0;
} /* End tw_post_command_packet() */

/* This function will return valid sense buffer information for failed cmds */
static int tw_decode_sense(TW_Device_Extension *tw_dev, int request_id, int fill_sense)
{
	int i;
	TW_Command *command;

        dprintk(KERN_WARNING "3w-xxxx: tw_decode_sense()\n");
	command = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];

	printk(KERN_WARNING "3w-xxxx: scsi%d: Command failed: status = 0x%x, flags = 0x%x, unit #%d.\n", tw_dev->host->host_no, command->status, command->flags, TW_UNIT_OUT(command->unit__hostid));

	/* Attempt to return intelligent sense information */
	if (fill_sense) {
		if ((command->status == 0xc7) || (command->status == 0xcb)) {
			for (i = 0; i < ARRAY_SIZE(tw_sense_table); i++) {
				if (command->flags == tw_sense_table[i][0]) {

					/* Valid bit and 'current errors' */
					tw_dev->srb[request_id]->sense_buffer[0] = (0x1 << 7 | 0x70);

					/* Sense key */
					tw_dev->srb[request_id]->sense_buffer[2] = tw_sense_table[i][1];

					/* Additional sense length */
					tw_dev->srb[request_id]->sense_buffer[7] = 0xa; /* 10 bytes */

					/* Additional sense code */
					tw_dev->srb[request_id]->sense_buffer[12] = tw_sense_table[i][2];

					/* Additional sense code qualifier */
					tw_dev->srb[request_id]->sense_buffer[13] = tw_sense_table[i][3];

					tw_dev->srb[request_id]->result = (DID_OK << 16) | (CHECK_CONDITION << 1);
					return TW_ISR_DONT_RESULT; /* Special case for isr to not over-write result */
				}
			}
		}

		/* If no table match, error so we get a reset */
		return 1;
	}

	return 0;
} /* End tw_decode_sense() */

/* This function will report controller error status */
static int tw_check_errors(TW_Device_Extension *tw_dev) 
{
	u32 status_reg_value;
  
	status_reg_value = inl(TW_STATUS_REG_ADDR(tw_dev));

	if (TW_STATUS_ERRORS(status_reg_value) || tw_check_bits(status_reg_value)) {
		tw_decode_bits(tw_dev, status_reg_value, 0);
		return 1;
	}

	return 0;
} /* End tw_check_errors() */

/* This function will empty the response que */
static void tw_empty_response_que(TW_Device_Extension *tw_dev) 
{
	u32 status_reg_value, response_que_value;

	status_reg_value = inl(TW_STATUS_REG_ADDR(tw_dev));

	while ((status_reg_value & TW_STATUS_RESPONSE_QUEUE_EMPTY) == 0) {
		response_que_value = inl(TW_RESPONSE_QUEUE_REG_ADDR(tw_dev));
		status_reg_value = inl(TW_STATUS_REG_ADDR(tw_dev));
	}
} /* End tw_empty_response_que() */

/* This function will free a request_id */
static void tw_state_request_finish(TW_Device_Extension *tw_dev, int request_id)
{
	tw_dev->free_queue[tw_dev->free_tail] = request_id;
	tw_dev->state[request_id] = TW_S_FINISHED;
	tw_dev->free_tail = (tw_dev->free_tail + 1) % TW_Q_LENGTH;
} /* End tw_state_request_finish() */

/* This function will assign an available request_id */
static void tw_state_request_start(TW_Device_Extension *tw_dev, int *request_id)
{
	*request_id = tw_dev->free_queue[tw_dev->free_head];
	tw_dev->free_head = (tw_dev->free_head + 1) % TW_Q_LENGTH;
	tw_dev->state[*request_id] = TW_S_STARTED;
} /* End tw_state_request_start() */

/* Show some statistics about the card */
static ssize_t tw_show_stats(struct class_device *class_dev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(class_dev);
	TW_Device_Extension *tw_dev = (TW_Device_Extension *)host->hostdata;
	unsigned long flags = 0;
	ssize_t len;

	spin_lock_irqsave(tw_dev->host->host_lock, flags);
	len = snprintf(buf, PAGE_SIZE, "3w-xxxx Driver version: %s\n"
		       "Current commands posted:   %4d\n"
		       "Max commands posted:       %4d\n"
		       "Current pending commands:  %4d\n"
		       "Max pending commands:      %4d\n"
		       "Last sgl length:           %4d\n"
		       "Max sgl length:            %4d\n"
		       "Last sector count:         %4d\n"
		       "Max sector count:          %4d\n"
		       "SCSI Host Resets:          %4d\n"
		       "AEN's:                     %4d\n", 
		       TW_DRIVER_VERSION,
		       tw_dev->posted_request_count,
		       tw_dev->max_posted_request_count,
		       tw_dev->pending_request_count,
		       tw_dev->max_pending_request_count,
		       tw_dev->sgl_entries,
		       tw_dev->max_sgl_entries,
		       tw_dev->sector_count,
		       tw_dev->max_sector_count,
		       tw_dev->num_resets,
		       tw_dev->aen_count);
	spin_unlock_irqrestore(tw_dev->host->host_lock, flags);
	return len;
} /* End tw_show_stats() */

/* This function will set a devices queue depth */
static int tw_change_queue_depth(struct scsi_device *sdev, int queue_depth)
{
	if (queue_depth > TW_Q_LENGTH-2)
		queue_depth = TW_Q_LENGTH-2;
	scsi_adjust_queue_depth(sdev, MSG_ORDERED_TAG, queue_depth);
	return queue_depth;
} /* End tw_change_queue_depth() */

/* Create sysfs 'stats' entry */
static struct class_device_attribute tw_host_stats_attr = {
	.attr = {
		.name = 	"stats",
		.mode =		S_IRUGO,
	},
	.show = tw_show_stats
};

/* Host attributes initializer */
static struct class_device_attribute *tw_host_attrs[] = {
	&tw_host_stats_attr,
	NULL,
};

/* This function will read the aen queue from the isr */
static int tw_aen_read_queue(TW_Device_Extension *tw_dev, int request_id) 
{
	TW_Command *command_packet;
	TW_Param *param;
	unsigned long command_que_value;
	u32 status_reg_value;
	unsigned long param_value = 0;

	dprintk(KERN_NOTICE "3w-xxxx: tw_aen_read_queue()\n");

	status_reg_value = inl(TW_STATUS_REG_ADDR(tw_dev));
	if (tw_check_bits(status_reg_value)) {
		dprintk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Unexpected bits.\n");
		tw_decode_bits(tw_dev, status_reg_value, 1);
		return 1;
	}
	if (tw_dev->command_packet_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Bad command packet virtual address.\n");
		return 1;
	}
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->opcode__sgloffset = TW_OPSGL_IN(2, TW_OP_GET_PARAM);
	command_packet->size = 4;
	command_packet->request_id = request_id;
	command_packet->status = 0;
	command_packet->flags = 0;
	command_packet->byte6.parameter_count = 1;
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Bad command packet physical address.\n");
		return 1;
	}
	/* Now setup the param */
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Bad alignment virtual address.\n");
		return 1;
	}
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	memset(param, 0, sizeof(TW_Sector));
	param->table_id = 0x401; /* AEN table */
	param->parameter_id = 2; /* Unit code */
	param->parameter_size_bytes = 2;
	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Bad alignment physical address.\n");
		return 1;
	}
	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);

	/* Now post the command packet */
	if ((status_reg_value & TW_STATUS_COMMAND_QUEUE_FULL) == 0) {
		dprintk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Post succeeded.\n");
		tw_dev->srb[request_id] = NULL; /* Flag internal command */
		tw_dev->state[request_id] = TW_S_POSTED;
		outl(command_que_value, TW_COMMAND_QUEUE_REG_ADDR(tw_dev));
	} else {
		printk(KERN_WARNING "3w-xxxx: tw_aen_read_queue(): Post failed, will retry.\n");
		return 1;
	}

	return 0;
} /* End tw_aen_read_queue() */

/* This function will complete an aen request from the isr */
static int tw_aen_complete(TW_Device_Extension *tw_dev, int request_id) 
{
	TW_Param *param;
	unsigned short aen;
	int error = 0, table_max = 0;

	dprintk(KERN_WARNING "3w-xxxx: tw_aen_complete()\n");
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_complete(): Bad alignment virtual address.\n");
		return 1;
	}
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	aen = *(unsigned short *)(param->data);
	dprintk(KERN_NOTICE "3w-xxxx: tw_aen_complete(): Queue'd code 0x%x\n", aen);

	/* Print some useful info when certain aen codes come out */
	if (aen == 0x0ff) {
		printk(KERN_WARNING "3w-xxxx: scsi%d: AEN: INFO: AEN queue overflow.\n", tw_dev->host->host_no);
	} else {
		table_max = ARRAY_SIZE(tw_aen_string);
		if ((aen & 0x0ff) < table_max) {
			if ((tw_aen_string[aen & 0xff][strlen(tw_aen_string[aen & 0xff])-1]) == '#') {
				printk(KERN_WARNING "3w-xxxx: scsi%d: AEN: %s%d.\n", tw_dev->host->host_no, tw_aen_string[aen & 0xff], aen >> 8);
			} else {
				if (aen != 0x0) 
					printk(KERN_WARNING "3w-xxxx: scsi%d: AEN: %s.\n", tw_dev->host->host_no, tw_aen_string[aen & 0xff]);
			}
		} else {
			printk(KERN_WARNING "3w-xxxx: scsi%d: Received AEN %d.\n", tw_dev->host->host_no, aen);
		}
	}
	if (aen != TW_AEN_QUEUE_EMPTY) {
		tw_dev->aen_count++;

		/* Now queue the code */
		tw_dev->aen_queue[tw_dev->aen_tail] = aen;
		if (tw_dev->aen_tail == TW_Q_LENGTH - 1) {
			tw_dev->aen_tail = TW_Q_START;
		} else {
			tw_dev->aen_tail = tw_dev->aen_tail + 1;
		}
		if (tw_dev->aen_head == tw_dev->aen_tail) {
			if (tw_dev->aen_head == TW_Q_LENGTH - 1) {
				tw_dev->aen_head = TW_Q_START;
			} else {
				tw_dev->aen_head = tw_dev->aen_head + 1;
			}
		}

		error = tw_aen_read_queue(tw_dev, request_id);
		if (error) {
			printk(KERN_WARNING "3w-xxxx: scsi%d: Error completing AEN.\n", tw_dev->host->host_no);
			tw_dev->state[request_id] = TW_S_COMPLETED;
			tw_state_request_finish(tw_dev, request_id);
		}
	} else {
		tw_dev->state[request_id] = TW_S_COMPLETED;
		tw_state_request_finish(tw_dev, request_id);
	}

	return 0;
} /* End tw_aen_complete() */

/* This function will drain the aen queue after a soft reset */
static int tw_aen_drain_queue(TW_Device_Extension *tw_dev)
{
	TW_Command *command_packet;
	TW_Param *param;
	int request_id = 0;
	unsigned long command_que_value;
	unsigned long param_value;
	TW_Response_Queue response_queue;
	unsigned short aen;
	unsigned short aen_code;
	int finished = 0;
	int first_reset = 0;
	int queue = 0;
	int found = 0, table_max = 0;

	dprintk(KERN_NOTICE "3w-xxxx: tw_aen_drain_queue()\n");

	if (tw_poll_status(tw_dev, TW_STATUS_ATTENTION_INTERRUPT | TW_STATUS_MICROCONTROLLER_READY, 30)) {
		dprintk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): No attention interrupt for card %d.\n", tw_device_extension_count);
		return 1;
	}
	TW_CLEAR_ATTENTION_INTERRUPT(tw_dev);

	/* Empty response queue */
	tw_empty_response_que(tw_dev);

	/* Initialize command packet */
	if (tw_dev->command_packet_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Bad command packet virtual address.\n");
		return 1;
	}
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->opcode__sgloffset = TW_OPSGL_IN(2, TW_OP_GET_PARAM);
	command_packet->size = 4;
	command_packet->request_id = request_id;
	command_packet->status = 0;
	command_packet->flags = 0;
	command_packet->byte6.parameter_count = 1;
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Bad command packet physical address.\n");
		return 1;
	}

	/* Now setup the param */
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Bad alignment virtual address.\n");
		return 1;
	}
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	memset(param, 0, sizeof(TW_Sector));
	param->table_id = 0x401; /* AEN table */
	param->parameter_id = 2; /* Unit code */
	param->parameter_size_bytes = 2;
	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Bad alignment physical address.\n");
		return 1;
	}
	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);

	/* Now drain the controller's aen queue */
	do {
		/* Post command packet */
		outl(command_que_value, TW_COMMAND_QUEUE_REG_ADDR(tw_dev));

		/* Now poll for completion */
		if (tw_poll_status_gone(tw_dev, TW_STATUS_RESPONSE_QUEUE_EMPTY, 30) == 0) {
			response_queue.value = inl(TW_RESPONSE_QUEUE_REG_ADDR(tw_dev));
			request_id = TW_RESID_OUT(response_queue.response_id);

			if (request_id != 0) {
				/* Unexpected request id */
				printk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Unexpected request id.\n");
				return 1;
			}
			
			if (command_packet->status != 0) {
				if (command_packet->flags != TW_AEN_TABLE_UNDEFINED) {
					/* Bad response */
					tw_decode_sense(tw_dev, request_id, 0);
					return 1;
				} else {
					/* We know this is a 3w-1x00, and doesn't support aen's */
					return 0;
				}
			}

			/* Now check the aen */
			aen = *(unsigned short *)(param->data);
			aen_code = (aen & 0x0ff);
			queue = 0;
			switch (aen_code) {
				case TW_AEN_QUEUE_EMPTY:
					dprintk(KERN_WARNING "3w-xxxx: AEN: %s.\n", tw_aen_string[aen & 0xff]);
					if (first_reset != 1) {
						return 1;
					} else {
						finished = 1;
					}
					break;
				case TW_AEN_SOFT_RESET:
					if (first_reset == 0) {
						first_reset = 1;
					} else {
						printk(KERN_WARNING "3w-xxxx: AEN: %s.\n", tw_aen_string[aen & 0xff]);
						tw_dev->aen_count++;
						queue = 1;
					}
					break;
				default:
					if (aen == 0x0ff) {
						printk(KERN_WARNING "3w-xxxx: AEN: INFO: AEN queue overflow.\n");
					} else {
						table_max = ARRAY_SIZE(tw_aen_string);
						if ((aen & 0x0ff) < table_max) {
							if ((tw_aen_string[aen & 0xff][strlen(tw_aen_string[aen & 0xff])-1]) == '#') {
								printk(KERN_WARNING "3w-xxxx: AEN: %s%d.\n", tw_aen_string[aen & 0xff], aen >> 8);
							} else {
								printk(KERN_WARNING "3w-xxxx: AEN: %s.\n", tw_aen_string[aen & 0xff]);
							}
						} else
							printk(KERN_WARNING "3w-xxxx: Received AEN %d.\n", aen);
					}
					tw_dev->aen_count++;
					queue = 1;
			}

			/* Now put the aen on the aen_queue */
			if (queue == 1) {
				tw_dev->aen_queue[tw_dev->aen_tail] = aen;
				if (tw_dev->aen_tail == TW_Q_LENGTH - 1) {
					tw_dev->aen_tail = TW_Q_START;
				} else {
					tw_dev->aen_tail = tw_dev->aen_tail + 1;
				}
				if (tw_dev->aen_head == tw_dev->aen_tail) {
					if (tw_dev->aen_head == TW_Q_LENGTH - 1) {
						tw_dev->aen_head = TW_Q_START;
					} else {
						tw_dev->aen_head = tw_dev->aen_head + 1;
					}
				}
			}
			found = 1;
		}
		if (found == 0) {
			printk(KERN_WARNING "3w-xxxx: tw_aen_drain_queue(): Response never received.\n");
			return 1;
		}
	} while (finished == 0);

	return 0;
} /* End tw_aen_drain_queue() */

/* This function will allocate memory */
static int tw_allocate_memory(TW_Device_Extension *tw_dev, int size, int which)
{
	int i;
	dma_addr_t dma_handle;
	unsigned long *cpu_addr = NULL;

	dprintk(KERN_NOTICE "3w-xxxx: tw_allocate_memory()\n");

	cpu_addr = pci_alloc_consistent(tw_dev->tw_pci_dev, size*TW_Q_LENGTH, &dma_handle);
	if (cpu_addr == NULL) {
		printk(KERN_WARNING "3w-xxxx: pci_alloc_consistent() failed.\n");
		return 1;
	}

	if ((unsigned long)cpu_addr % (tw_dev->tw_pci_dev->device == TW_DEVICE_ID ? TW_ALIGNMENT_6000 : TW_ALIGNMENT_7000)) {
		printk(KERN_WARNING "3w-xxxx: Couldn't allocate correctly aligned memory.\n");
		pci_free_consistent(tw_dev->tw_pci_dev, size*TW_Q_LENGTH, cpu_addr, dma_handle);
		return 1;
	}

	memset(cpu_addr, 0, size*TW_Q_LENGTH);

	for (i=0;i<TW_Q_LENGTH;i++) {
		switch(which) {
		case 0:
			tw_dev->command_packet_physical_address[i] = dma_handle+(i*size);
			tw_dev->command_packet_virtual_address[i] = (unsigned long *)((unsigned char *)cpu_addr + (i*size));
			break;
		case 1:
			tw_dev->alignment_physical_address[i] = dma_handle+(i*size);
			tw_dev->alignment_virtual_address[i] = (unsigned long *)((unsigned char *)cpu_addr + (i*size));
			break;
		default:
			printk(KERN_WARNING "3w-xxxx: tw_allocate_memory(): case slip in tw_allocate_memory()\n");
			return 1;
		}
	}

	return 0;
} /* End tw_allocate_memory() */

/* This function handles ioctl for the character device */
static int tw_chrdev_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int request_id;
	dma_addr_t dma_handle;
	unsigned short tw_aen_code;
	unsigned long flags;
	unsigned int data_buffer_length = 0;
	unsigned long data_buffer_length_adjusted = 0;
	unsigned long *cpu_addr;
	long timeout;
	TW_New_Ioctl *tw_ioctl;
	TW_Passthru *passthru;
	TW_Device_Extension *tw_dev = tw_device_extension_list[iminor(inode)];
	int retval = -EFAULT;
	void __user *argp = (void __user *)arg;

	dprintk(KERN_WARNING "3w-xxxx: tw_chrdev_ioctl()\n");

	/* Only let one of these through at a time */
	if (mutex_lock_interruptible(&tw_dev->ioctl_lock))
		return -EINTR;

	/* First copy down the buffer length */
	if (copy_from_user(&data_buffer_length, argp, sizeof(unsigned int)))
		goto out;

	/* Check size */
	if (data_buffer_length > TW_MAX_IOCTL_SECTORS * 512) {
		retval = -EINVAL;
		goto out;
	}

	/* Hardware can only do multiple of 512 byte transfers */
	data_buffer_length_adjusted = (data_buffer_length + 511) & ~511;
	
	/* Now allocate ioctl buf memory */
	cpu_addr = dma_alloc_coherent(&tw_dev->tw_pci_dev->dev, data_buffer_length_adjusted+sizeof(TW_New_Ioctl) - 1, &dma_handle, GFP_KERNEL);
	if (cpu_addr == NULL) {
		retval = -ENOMEM;
		goto out;
	}

	tw_ioctl = (TW_New_Ioctl *)cpu_addr;

	/* Now copy down the entire ioctl */
	if (copy_from_user(tw_ioctl, argp, data_buffer_length + sizeof(TW_New_Ioctl) - 1))
		goto out2;

	passthru = (TW_Passthru *)&tw_ioctl->firmware_command;

	/* See which ioctl we are doing */
	switch (cmd) {
		case TW_OP_NOP:
			dprintk(KERN_WARNING "3w-xxxx: tw_chrdev_ioctl(): caught TW_OP_NOP.\n");
			break;
		case TW_OP_AEN_LISTEN:
			dprintk(KERN_WARNING "3w-xxxx: tw_chrdev_ioctl(): caught TW_AEN_LISTEN.\n");
			memset(tw_ioctl->data_buffer, 0, data_buffer_length);

			spin_lock_irqsave(tw_dev->host->host_lock, flags);
			if (tw_dev->aen_head == tw_dev->aen_tail) {
				tw_aen_code = TW_AEN_QUEUE_EMPTY;
			} else {
				tw_aen_code = tw_dev->aen_queue[tw_dev->aen_head];
				if (tw_dev->aen_head == TW_Q_LENGTH - 1) {
					tw_dev->aen_head = TW_Q_START;
				} else {
					tw_dev->aen_head = tw_dev->aen_head + 1;
				}
			}
			spin_unlock_irqrestore(tw_dev->host->host_lock, flags);
			memcpy(tw_ioctl->data_buffer, &tw_aen_code, sizeof(tw_aen_code));
			break;
		case TW_CMD_PACKET_WITH_DATA:
			dprintk(KERN_WARNING "3w-xxxx: tw_chrdev_ioctl(): caught TW_CMD_PACKET_WITH_DATA.\n");
			spin_lock_irqsave(tw_dev->host->host_lock, flags);

			tw_state_request_start(tw_dev, &request_id);

			/* Flag internal command */
			tw_dev->srb[request_id] = NULL;

			/* Flag chrdev ioctl */
			tw_dev->chrdev_request_id = request_id;

			tw_ioctl->firmware_command.request_id = request_id;

			/* Load the sg list */
			switch (TW_SGL_OUT(tw_ioctl->firmware_command.opcode__sgloffset)) {
			case 2:
				tw_ioctl->firmware_command.byte8.param.sgl[0].address = dma_handle + sizeof(TW_New_Ioctl) - 1;
				tw_ioctl->firmware_command.byte8.param.sgl[0].length = data_buffer_length_adjusted;
				break;
			case 3:
				tw_ioctl->firmware_command.byte8.io.sgl[0].address = dma_handle + sizeof(TW_New_Ioctl) - 1;
				tw_ioctl->firmware_command.byte8.io.sgl[0].length = data_buffer_length_adjusted;
				break;
			case 5:
				passthru->sg_list[0].address = dma_handle + sizeof(TW_New_Ioctl) - 1;
				passthru->sg_list[0].length = data_buffer_length_adjusted;
				break;
			}

			memcpy(tw_dev->command_packet_virtual_address[request_id], &(tw_ioctl->firmware_command), sizeof(TW_Command));

			/* Now post the command packet to the controller */
			tw_post_command_packet(tw_dev, request_id);
			spin_unlock_irqrestore(tw_dev->host->host_lock, flags);

			timeout = TW_IOCTL_CHRDEV_TIMEOUT*HZ;

			/* Now wait for the command to complete */
			timeout = wait_event_timeout(tw_dev->ioctl_wqueue, tw_dev->chrdev_request_id == TW_IOCTL_CHRDEV_FREE, timeout);

			/* We timed out, and didn't get an interrupt */
			if (tw_dev->chrdev_request_id != TW_IOCTL_CHRDEV_FREE) {
				/* Now we need to reset the board */
				printk(KERN_WARNING "3w-xxxx: scsi%d: Character ioctl (0x%x) timed out, resetting card.\n", tw_dev->host->host_no, cmd);
				retval = -EIO;
				if (tw_reset_device_extension(tw_dev)) {
					printk(KERN_WARNING "3w-xxxx: tw_chrdev_ioctl(): Reset failed for card %d.\n", tw_dev->host->host_no);
				}
				goto out2;
			}

			/* Now copy in the command packet response */
			memcpy(&(tw_ioctl->firmware_command), tw_dev->command_packet_virtual_address[request_id], sizeof(TW_Command));

			/* Now complete the io */
			spin_lock_irqsave(tw_dev->host->host_lock, flags);
			tw_dev->posted_request_count--;
			tw_dev->state[request_id] = TW_S_COMPLETED;
			tw_state_request_finish(tw_dev, request_id);
			spin_unlock_irqrestore(tw_dev->host->host_lock, flags);
			break;
		default:
			retval = -ENOTTY;
			goto out2;
	}

	/* Now copy the response to userspace */
	if (copy_to_user(argp, tw_ioctl, sizeof(TW_New_Ioctl) + data_buffer_length - 1))
		goto out2;
	retval = 0;
out2:
	/* Now free ioctl buf memory */
	dma_free_coherent(&tw_dev->tw_pci_dev->dev, data_buffer_length_adjusted+sizeof(TW_New_Ioctl) - 1, cpu_addr, dma_handle);
out:
	mutex_unlock(&tw_dev->ioctl_lock);
	return retval;
} /* End tw_chrdev_ioctl() */

/* This function handles open for the character device */
static int tw_chrdev_open(struct inode *inode, struct file *file)
{
	unsigned int minor_number;

	dprintk(KERN_WARNING "3w-xxxx: tw_ioctl_open()\n");

	minor_number = iminor(inode);
	if (minor_number >= tw_device_extension_count)
		return -ENODEV;

	return 0;
} /* End tw_chrdev_open() */

/* File operations struct for character device */
static const struct file_operations tw_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= tw_chrdev_ioctl,
	.open		= tw_chrdev_open,
	.release	= NULL
};

/* This function will free up device extension resources */
static void tw_free_device_extension(TW_Device_Extension *tw_dev)
{
	dprintk(KERN_NOTICE "3w-xxxx: tw_free_device_extension()\n");

	/* Free command packet and generic buffer memory */
	if (tw_dev->command_packet_virtual_address[0])
		pci_free_consistent(tw_dev->tw_pci_dev, sizeof(TW_Command)*TW_Q_LENGTH, tw_dev->command_packet_virtual_address[0], tw_dev->command_packet_physical_address[0]);

	if (tw_dev->alignment_virtual_address[0])
		pci_free_consistent(tw_dev->tw_pci_dev, sizeof(TW_Sector)*TW_Q_LENGTH, tw_dev->alignment_virtual_address[0], tw_dev->alignment_physical_address[0]);
} /* End tw_free_device_extension() */

/* This function will send an initconnection command to controller */
static int tw_initconnection(TW_Device_Extension *tw_dev, int message_credits) 
{
	unsigned long command_que_value;
	TW_Command  *command_packet;
	TW_Response_Queue response_queue;
	int request_id = 0;

	dprintk(KERN_NOTICE "3w-xxxx: tw_initconnection()\n");

	/* Initialize InitConnection command packet */
	if (tw_dev->command_packet_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_initconnection(): Bad command packet virtual address.\n");
		return 1;
	}

	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->opcode__sgloffset = TW_OPSGL_IN(0, TW_OP_INIT_CONNECTION);
	command_packet->size = TW_INIT_COMMAND_PACKET_SIZE;
	command_packet->request_id = request_id;
	command_packet->status = 0x0;
	command_packet->flags = 0x0;
	command_packet->byte6.message_credits = message_credits; 
	command_packet->byte8.init_connection.response_queue_pointer = 0x0;
	command_que_value = tw_dev->command_packet_physical_address[request_id];

	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_initconnection(): Bad command packet physical address.\n");
		return 1;
	}
  
	/* Send command packet to the board */
	outl(command_que_value, TW_COMMAND_QUEUE_REG_ADDR(tw_dev));
    
	/* Poll for completion */
	if (tw_poll_status_gone(tw_dev, TW_STATUS_RESPONSE_QUEUE_EMPTY, 30) == 0) {
		response_queue.value = inl(TW_RESPONSE_QUEUE_REG_ADDR(tw_dev));
		request_id = TW_RESID_OUT(response_queue.response_id);

		if (request_id != 0) {
			/* unexpected request id */
			printk(KERN_WARNING "3w-xxxx: tw_initconnection(): Unexpected request id.\n");
			return 1;
		}
		if (command_packet->status != 0) {
			/* bad response */
			tw_decode_sense(tw_dev, request_id, 0);
			return 1;
		}
	}
	return 0;
} /* End tw_initconnection() */

/* Set a value in the features table */
static int tw_setfeature(TW_Device_Extension *tw_dev, int parm, int param_size,
                  unsigned char *val)
{
	TW_Param *param;
	TW_Command  *command_packet;
	TW_Response_Queue response_queue;
	int request_id = 0;
	unsigned long command_que_value;
	unsigned long param_value;

  	/* Initialize SetParam command packet */
	if (tw_dev->command_packet_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_setfeature(): Bad command packet virtual address.\n");
		return 1;
	}
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	memset(command_packet, 0, sizeof(TW_Sector));
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];

	command_packet->opcode__sgloffset = TW_OPSGL_IN(2, TW_OP_SET_PARAM);
	param->table_id = 0x404;  /* Features table */
	param->parameter_id = parm;
	param->parameter_size_bytes = param_size;
	memcpy(param->data, val, param_size);

	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_setfeature(): Bad alignment physical address.\n");
		tw_dev->state[request_id] = TW_S_COMPLETED;
		tw_state_request_finish(tw_dev, request_id);
		tw_dev->srb[request_id]->result = (DID_OK << 16);
		tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);
	}
	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);

	command_packet->size = 4;
	command_packet->request_id = request_id;
	command_packet->byte6.parameter_count = 1;

  	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_setfeature(): Bad command packet physical address.\n");
	return 1;
	}

	/* Send command packet to the board */
	outl(command_que_value, TW_COMMAND_QUEUE_REG_ADDR(tw_dev));

	/* Poll for completion */
	if (tw_poll_status_gone(tw_dev, TW_STATUS_RESPONSE_QUEUE_EMPTY, 30) == 0) {
		response_queue.value = inl(TW_RESPONSE_QUEUE_REG_ADDR(tw_dev));
		request_id = TW_RESID_OUT(response_queue.response_id);

		if (request_id != 0) {
			/* unexpected request id */
			printk(KERN_WARNING "3w-xxxx: tw_setfeature(): Unexpected request id.\n");
			return 1;
		}
		if (command_packet->status != 0) {
			/* bad response */
			tw_decode_sense(tw_dev, request_id, 0);
			return 1;
		}
	}

	return 0;
} /* End tw_setfeature() */

/* This function will reset a controller */
static int tw_reset_sequence(TW_Device_Extension *tw_dev) 
{
	int error = 0;
	int tries = 0;
	unsigned char c = 1;

	/* Reset the board */
	while (tries < TW_MAX_RESET_TRIES) {
		TW_SOFT_RESET(tw_dev);

		error = tw_aen_drain_queue(tw_dev);
		if (error) {
			printk(KERN_WARNING "3w-xxxx: scsi%d: AEN drain failed, retrying.\n", tw_dev->host->host_no);
			tries++;
			continue;
		}

		/* Check for controller errors */
		if (tw_check_errors(tw_dev)) {
			printk(KERN_WARNING "3w-xxxx: scsi%d: Controller errors found, retrying.\n", tw_dev->host->host_no);
			tries++;
			continue;
		}

		/* Now the controller is in a good state */
		break;
	}

	if (tries >= TW_MAX_RESET_TRIES) {
		printk(KERN_WARNING "3w-xxxx: scsi%d: Controller errors, card not responding, check all cabling.\n", tw_dev->host->host_no);
		return 1;
	}

	error = tw_initconnection(tw_dev, TW_INIT_MESSAGE_CREDITS);
	if (error) {
		printk(KERN_WARNING "3w-xxxx: scsi%d: Connection initialization failed.\n", tw_dev->host->host_no);
		return 1;
	}

	error = tw_setfeature(tw_dev, 2, 1, &c);
	if (error) {
		printk(KERN_WARNING "3w-xxxx: Unable to set features for card, probable old firmware or card.\n");
	}

	return 0;
} /* End tw_reset_sequence() */

/* This function will initialize the fields of a device extension */
static int tw_initialize_device_extension(TW_Device_Extension *tw_dev)
{
	int i, error=0;

	dprintk(KERN_NOTICE "3w-xxxx: tw_initialize_device_extension()\n");

	/* Initialize command packet buffers */
	error = tw_allocate_memory(tw_dev, sizeof(TW_Command), 0);
	if (error) {
		printk(KERN_WARNING "3w-xxxx: Command packet memory allocation failed.\n");
		return 1;
	}

	/* Initialize generic buffer */
	error = tw_allocate_memory(tw_dev, sizeof(TW_Sector), 1);
	if (error) {
		printk(KERN_WARNING "3w-xxxx: Generic memory allocation failed.\n");
		return 1;
	}

	for (i=0;i<TW_Q_LENGTH;i++) {
		tw_dev->free_queue[i] = i;
		tw_dev->state[i] = TW_S_INITIAL;
	}

	tw_dev->pending_head = TW_Q_START;
	tw_dev->pending_tail = TW_Q_START;
	tw_dev->chrdev_request_id = TW_IOCTL_CHRDEV_FREE;

	mutex_init(&tw_dev->ioctl_lock);
	init_waitqueue_head(&tw_dev->ioctl_wqueue);

	return 0;
} /* End tw_initialize_device_extension() */

static int tw_map_scsi_sg_data(struct pci_dev *pdev, struct scsi_cmnd *cmd)
{
	int use_sg;

	dprintk(KERN_WARNING "3w-xxxx: tw_map_scsi_sg_data()\n");

	use_sg = scsi_dma_map(cmd);
	if (use_sg < 0) {
		printk(KERN_WARNING "3w-xxxx: tw_map_scsi_sg_data(): pci_map_sg() failed.\n");
		return 0;
	}

	cmd->SCp.phase = TW_PHASE_SGLIST;
	cmd->SCp.have_data_in = use_sg;

	return use_sg;
} /* End tw_map_scsi_sg_data() */

static void tw_unmap_scsi_data(struct pci_dev *pdev, struct scsi_cmnd *cmd)
{
	dprintk(KERN_WARNING "3w-xxxx: tw_unmap_scsi_data()\n");

	scsi_dma_unmap(cmd);
} /* End tw_unmap_scsi_data() */

/* This function will reset a device extension */
static int tw_reset_device_extension(TW_Device_Extension *tw_dev)
{
	int i = 0;
	struct scsi_cmnd *srb;
	unsigned long flags = 0;

	dprintk(KERN_NOTICE "3w-xxxx: tw_reset_device_extension()\n");

	set_bit(TW_IN_RESET, &tw_dev->flags);
	TW_DISABLE_INTERRUPTS(tw_dev);
	TW_MASK_COMMAND_INTERRUPT(tw_dev);
	spin_lock_irqsave(tw_dev->host->host_lock, flags);

	/* Abort all requests that are in progress */
	for (i=0;i<TW_Q_LENGTH;i++) {
		if ((tw_dev->state[i] != TW_S_FINISHED) && 
		    (tw_dev->state[i] != TW_S_INITIAL) &&
		    (tw_dev->state[i] != TW_S_COMPLETED)) {
			srb = tw_dev->srb[i];
			if (srb != NULL) {
				srb->result = (DID_RESET << 16);
				tw_dev->srb[i]->scsi_done(tw_dev->srb[i]);
				tw_unmap_scsi_data(tw_dev->tw_pci_dev, tw_dev->srb[i]);
			}
		}
	}

	/* Reset queues and counts */
	for (i=0;i<TW_Q_LENGTH;i++) {
		tw_dev->free_queue[i] = i;
		tw_dev->state[i] = TW_S_INITIAL;
	}
	tw_dev->free_head = TW_Q_START;
	tw_dev->free_tail = TW_Q_START;
	tw_dev->posted_request_count = 0;
	tw_dev->pending_request_count = 0;
	tw_dev->pending_head = TW_Q_START;
	tw_dev->pending_tail = TW_Q_START;
	tw_dev->reset_print = 0;

	spin_unlock_irqrestore(tw_dev->host->host_lock, flags);

	if (tw_reset_sequence(tw_dev)) {
		printk(KERN_WARNING "3w-xxxx: scsi%d: Reset sequence failed.\n", tw_dev->host->host_no);
		return 1;
	}

	TW_ENABLE_AND_CLEAR_INTERRUPTS(tw_dev);
	clear_bit(TW_IN_RESET, &tw_dev->flags);
	tw_dev->chrdev_request_id = TW_IOCTL_CHRDEV_FREE;

	return 0;
} /* End tw_reset_device_extension() */

/* This funciton returns unit geometry in cylinders/heads/sectors */
static int tw_scsi_biosparam(struct scsi_device *sdev, struct block_device *bdev,
		sector_t capacity, int geom[]) 
{
	int heads, sectors, cylinders;
	TW_Device_Extension *tw_dev;
	
	dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_biosparam()\n");
	tw_dev = (TW_Device_Extension *)sdev->host->hostdata;

	heads = 64;
	sectors = 32;
	cylinders = sector_div(capacity, heads * sectors);

	if (capacity >= 0x200000) {
		heads = 255;
		sectors = 63;
		cylinders = sector_div(capacity, heads * sectors);
	}

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_biosparam(): heads = %d, sectors = %d, cylinders = %d\n", heads, sectors, cylinders);
	geom[0] = heads;			 
	geom[1] = sectors;
	geom[2] = cylinders;

	return 0;
} /* End tw_scsi_biosparam() */

/* This is the new scsi eh reset function */
static int tw_scsi_eh_reset(struct scsi_cmnd *SCpnt) 
{
	TW_Device_Extension *tw_dev=NULL;
	int retval = FAILED;

	tw_dev = (TW_Device_Extension *)SCpnt->device->host->hostdata;

	tw_dev->num_resets++;

	sdev_printk(KERN_WARNING, SCpnt->device,
		"WARNING: Command (0x%x) timed out, resetting card.\n",
		SCpnt->cmnd[0]);

	/* Make sure we are not issuing an ioctl or resetting from ioctl */
	mutex_lock(&tw_dev->ioctl_lock);

	/* Now reset the card and some of the device extension data */
	if (tw_reset_device_extension(tw_dev)) {
		printk(KERN_WARNING "3w-xxxx: scsi%d: Reset failed.\n", tw_dev->host->host_no);
		goto out;
	}

	retval = SUCCESS;
out:
	mutex_unlock(&tw_dev->ioctl_lock);
	return retval;
} /* End tw_scsi_eh_reset() */

/* This function handles scsi inquiry commands */
static int tw_scsiop_inquiry(TW_Device_Extension *tw_dev, int request_id)
{
	TW_Param *param;
	TW_Command *command_packet;
	unsigned long command_que_value;
	unsigned long param_value;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_inquiry()\n");

	/* Initialize command packet */
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	if (command_packet == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_inquiry(): Bad command packet virtual address.\n");
		return 1;
	}
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->opcode__sgloffset = TW_OPSGL_IN(2, TW_OP_GET_PARAM);
	command_packet->size = 4;
	command_packet->request_id = request_id;
	command_packet->status = 0;
	command_packet->flags = 0;
	command_packet->byte6.parameter_count = 1;

	/* Now setup the param */
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_inquiry(): Bad alignment virtual address.\n");
		return 1;
	}
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	memset(param, 0, sizeof(TW_Sector));
	param->table_id = 3;	 /* unit summary table */
	param->parameter_id = 3; /* unitsstatus parameter */
	param->parameter_size_bytes = TW_MAX_UNITS;
	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_inquiry(): Bad alignment physical address.\n");
		return 1;
	}

	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_inquiry(): Bad command packet physical address.\n");
		return 1;
	}

	/* Now try to post the command packet */
	tw_post_command_packet(tw_dev, request_id);

	return 0;
} /* End tw_scsiop_inquiry() */

static void tw_transfer_internal(TW_Device_Extension *tw_dev, int request_id,
				 void *data, unsigned int len)
{
	struct scsi_cmnd *cmd = tw_dev->srb[request_id];
	void *buf;
	unsigned int transfer_len;
	unsigned long flags = 0;
	struct scatterlist *sg = scsi_sglist(cmd);

	local_irq_save(flags);
	buf = kmap_atomic(sg_page(sg), KM_IRQ0) + sg->offset;
	transfer_len = min(sg->length, len);

	memcpy(buf, data, transfer_len);

	kunmap_atomic(buf - sg->offset, KM_IRQ0);
	local_irq_restore(flags);
}

/* This function is called by the isr to complete an inquiry command */
static int tw_scsiop_inquiry_complete(TW_Device_Extension *tw_dev, int request_id)
{
	unsigned char *is_unit_present;
	unsigned char request_buffer[36];
	TW_Param *param;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_inquiry_complete()\n");

	memset(request_buffer, 0, sizeof(request_buffer));
	request_buffer[0] = TYPE_DISK; /* Peripheral device type */
	request_buffer[1] = 0;	       /* Device type modifier */
	request_buffer[2] = 0;	       /* No ansi/iso compliance */
	request_buffer[4] = 31;	       /* Additional length */
	memcpy(&request_buffer[8], "3ware   ", 8);	 /* Vendor ID */
	sprintf(&request_buffer[16], "Logical Disk %-2d ", tw_dev->srb[request_id]->device->id);
	memcpy(&request_buffer[32], TW_DRIVER_VERSION, 3);
	tw_transfer_internal(tw_dev, request_id, request_buffer,
			     sizeof(request_buffer));

	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	if (param == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_inquiry_complete(): Bad alignment virtual address.\n");
		return 1;
	}
	is_unit_present = &(param->data[0]);

	if (is_unit_present[tw_dev->srb[request_id]->device->id] & TW_UNIT_ONLINE) {
		tw_dev->is_unit_present[tw_dev->srb[request_id]->device->id] = 1;
	} else {
		tw_dev->is_unit_present[tw_dev->srb[request_id]->device->id] = 0;
		tw_dev->srb[request_id]->result = (DID_BAD_TARGET << 16);
		return TW_ISR_DONT_RESULT;
	}

	return 0;
} /* End tw_scsiop_inquiry_complete() */

/* This function handles scsi mode_sense commands */
static int tw_scsiop_mode_sense(TW_Device_Extension *tw_dev, int request_id)
{
	TW_Param *param;
	TW_Command *command_packet;
	unsigned long command_que_value;
	unsigned long param_value;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_mode_sense()\n");

	/* Only page control = 0, page code = 0x8 (cache page) supported */
	if (tw_dev->srb[request_id]->cmnd[2] != 0x8) {
		tw_dev->state[request_id] = TW_S_COMPLETED;
		tw_state_request_finish(tw_dev, request_id);
		tw_dev->srb[request_id]->result = (DID_OK << 16);
		tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);
		return 0;
	}

	/* Now read firmware cache setting for this unit */
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	if (command_packet == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_mode_sense(): Bad command packet virtual address.\n");
		return 1;
	}

	/* Setup the command packet */
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->opcode__sgloffset = TW_OPSGL_IN(2, TW_OP_GET_PARAM);
	command_packet->size = 4;
	command_packet->request_id = request_id;
	command_packet->status = 0;
	command_packet->flags = 0;
	command_packet->byte6.parameter_count = 1;

	/* Setup the param */
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_mode_sense(): Bad alignment virtual address.\n");
		return 1;
	}

	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	memset(param, 0, sizeof(TW_Sector));
	param->table_id = TW_UNIT_INFORMATION_TABLE_BASE + tw_dev->srb[request_id]->device->id;
	param->parameter_id = 7; /* unit flags */
	param->parameter_size_bytes = 1;
	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_mode_sense(): Bad alignment physical address.\n");
		return 1;
	}

	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_mode_sense(): Bad command packet physical address.\n");
		return 1;
	}

	/* Now try to post the command packet */
	tw_post_command_packet(tw_dev, request_id);
	
	return 0;
} /* End tw_scsiop_mode_sense() */

/* This function is called by the isr to complete a mode sense command */
static int tw_scsiop_mode_sense_complete(TW_Device_Extension *tw_dev, int request_id)
{
	TW_Param *param;
	unsigned char *flags;
	unsigned char request_buffer[8];

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_mode_sense_complete()\n");

	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	if (param == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_mode_sense_complete(): Bad alignment virtual address.\n");
		return 1;
	}
	flags = (char *)&(param->data[0]);
	memset(request_buffer, 0, sizeof(request_buffer));

	request_buffer[0] = 0xf;        /* mode data length */
	request_buffer[1] = 0;          /* default medium type */
	request_buffer[2] = 0x10;       /* dpo/fua support on */
	request_buffer[3] = 0;          /* no block descriptors */
	request_buffer[4] = 0x8;        /* caching page */
	request_buffer[5] = 0xa;        /* page length */
	if (*flags & 0x1)
		request_buffer[6] = 0x5;        /* WCE on, RCD on */
	else
		request_buffer[6] = 0x1;        /* WCE off, RCD on */
	tw_transfer_internal(tw_dev, request_id, request_buffer,
			     sizeof(request_buffer));

	return 0;
} /* End tw_scsiop_mode_sense_complete() */

/* This function handles scsi read_capacity commands */
static int tw_scsiop_read_capacity(TW_Device_Extension *tw_dev, int request_id) 
{
	TW_Param *param;
	TW_Command *command_packet;
	unsigned long command_que_value;
	unsigned long param_value;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity()\n");

	/* Initialize command packet */
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];

	if (command_packet == NULL) {
		dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity(): Bad command packet virtual address.\n");
		return 1;
	}
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->opcode__sgloffset = TW_OPSGL_IN(2, TW_OP_GET_PARAM);
	command_packet->size = 4;
	command_packet->request_id = request_id;
	command_packet->unit__hostid = TW_UNITHOST_IN(0, tw_dev->srb[request_id]->device->id);
	command_packet->status = 0;
	command_packet->flags = 0;
	command_packet->byte6.block_count = 1;

	/* Now setup the param */
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity(): Bad alignment virtual address.\n");
		return 1;
	}
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	memset(param, 0, sizeof(TW_Sector));
	param->table_id = TW_UNIT_INFORMATION_TABLE_BASE + 
	tw_dev->srb[request_id]->device->id;
	param->parameter_id = 4;	/* unitcapacity parameter */
	param->parameter_size_bytes = 4;
	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity(): Bad alignment physical address.\n");
		return 1;
	}
  
	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity(): Bad command packet physical address.\n");
		return 1;
	}

	/* Now try to post the command to the board */
	tw_post_command_packet(tw_dev, request_id);
  
	return 0;
} /* End tw_scsiop_read_capacity() */

/* This function is called by the isr to complete a readcapacity command */
static int tw_scsiop_read_capacity_complete(TW_Device_Extension *tw_dev, int request_id)
{
	unsigned char *param_data;
	u32 capacity;
	char buff[8];
	TW_Param *param;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity_complete()\n");

	memset(buff, 0, sizeof(buff));
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	if (param == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_read_capacity_complete(): Bad alignment virtual address.\n");
		return 1;
	}
	param_data = &(param->data[0]);

	capacity = (param_data[3] << 24) | (param_data[2] << 16) | 
		   (param_data[1] << 8) | param_data[0];

	/* Subtract one sector to fix get last sector ioctl */
	capacity -= 1;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_capacity_complete(): Capacity = 0x%x.\n", capacity);

	/* Number of LBA's */
	buff[0] = (capacity >> 24);
	buff[1] = (capacity >> 16) & 0xff;
	buff[2] = (capacity >> 8) & 0xff;
	buff[3] = capacity & 0xff;

	/* Block size in bytes (512) */
	buff[4] = (TW_BLOCK_SIZE >> 24);
	buff[5] = (TW_BLOCK_SIZE >> 16) & 0xff;
	buff[6] = (TW_BLOCK_SIZE >> 8) & 0xff;
	buff[7] = TW_BLOCK_SIZE & 0xff;

	tw_transfer_internal(tw_dev, request_id, buff, sizeof(buff));

	return 0;
} /* End tw_scsiop_read_capacity_complete() */

/* This function handles scsi read or write commands */
static int tw_scsiop_read_write(TW_Device_Extension *tw_dev, int request_id) 
{
	TW_Command *command_packet;
	unsigned long command_que_value;
	u32 lba = 0x0, num_sectors = 0x0;
	int i, use_sg;
	struct scsi_cmnd *srb;
	struct scatterlist *sglist, *sg;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_write()\n");

	srb = tw_dev->srb[request_id];

	sglist = scsi_sglist(srb);
	if (!sglist) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_read_write(): Request buffer NULL.\n");
		return 1;
	}

	/* Initialize command packet */
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	if (command_packet == NULL) {
		dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_write(): Bad command packet virtual address.\n");
		return 1;
	}

	if (srb->cmnd[0] == READ_6 || srb->cmnd[0] == READ_10) {
		command_packet->opcode__sgloffset = TW_OPSGL_IN(3, TW_OP_READ);
	} else {
		command_packet->opcode__sgloffset = TW_OPSGL_IN(3, TW_OP_WRITE);
	}

	command_packet->size = 3;
	command_packet->request_id = request_id;
	command_packet->unit__hostid = TW_UNITHOST_IN(0, srb->device->id);
	command_packet->status = 0;
	command_packet->flags = 0;

	if (srb->cmnd[0] == WRITE_10) {
		if ((srb->cmnd[1] & 0x8) || (srb->cmnd[1] & 0x10))
			command_packet->flags = 1;
	}

	if (srb->cmnd[0] == READ_6 || srb->cmnd[0] == WRITE_6) {
		lba = ((u32)srb->cmnd[1] << 16) | ((u32)srb->cmnd[2] << 8) | (u32)srb->cmnd[3];
		num_sectors = (u32)srb->cmnd[4];
	} else {
		lba = ((u32)srb->cmnd[2] << 24) | ((u32)srb->cmnd[3] << 16) | ((u32)srb->cmnd[4] << 8) | (u32)srb->cmnd[5];
		num_sectors = (u32)srb->cmnd[8] | ((u32)srb->cmnd[7] << 8);
	}
  
	/* Update sector statistic */
	tw_dev->sector_count = num_sectors;
	if (tw_dev->sector_count > tw_dev->max_sector_count)
		tw_dev->max_sector_count = tw_dev->sector_count;
  
	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_read_write(): lba = 0x%x num_sectors = 0x%x\n", lba, num_sectors);
	command_packet->byte8.io.lba = lba;
	command_packet->byte6.block_count = num_sectors;

	use_sg = tw_map_scsi_sg_data(tw_dev->tw_pci_dev, tw_dev->srb[request_id]);
	if (!use_sg)
		return 1;

	scsi_for_each_sg(tw_dev->srb[request_id], sg, use_sg, i) {
		command_packet->byte8.io.sgl[i].address = sg_dma_address(sg);
		command_packet->byte8.io.sgl[i].length = sg_dma_len(sg);
		command_packet->size+=2;
	}

	/* Update SG statistics */
	tw_dev->sgl_entries = scsi_sg_count(tw_dev->srb[request_id]);
	if (tw_dev->sgl_entries > tw_dev->max_sgl_entries)
		tw_dev->max_sgl_entries = tw_dev->sgl_entries;

	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		dprintk(KERN_WARNING "3w-xxxx: tw_scsiop_read_write(): Bad command packet physical address.\n");
		return 1;
	}
      
	/* Now try to post the command to the board */
	tw_post_command_packet(tw_dev, request_id);

	return 0;
} /* End tw_scsiop_read_write() */

/* This function will handle the request sense scsi command */
static int tw_scsiop_request_sense(TW_Device_Extension *tw_dev, int request_id)
{
	char request_buffer[18];

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_request_sense()\n");

	memset(request_buffer, 0, sizeof(request_buffer));
	request_buffer[0] = 0x70; /* Immediate fixed format */
	request_buffer[7] = 10;	/* minimum size per SPC: 18 bytes */
	/* leave all other fields zero, giving effectively NO_SENSE return */
	tw_transfer_internal(tw_dev, request_id, request_buffer,
			     sizeof(request_buffer));

	tw_dev->state[request_id] = TW_S_COMPLETED;
	tw_state_request_finish(tw_dev, request_id);

	/* If we got a request_sense, we probably want a reset, return error */
	tw_dev->srb[request_id]->result = (DID_ERROR << 16);
	tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);

	return 0;
} /* End tw_scsiop_request_sense() */

/* This function will handle synchronize cache scsi command */
static int tw_scsiop_synchronize_cache(TW_Device_Extension *tw_dev, int request_id)
{
	TW_Command *command_packet;
	unsigned long command_que_value;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_synchronize_cache()\n");

	/* Send firmware flush command for this unit */
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	if (command_packet == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_synchronize_cache(): Bad command packet virtual address.\n");
		return 1;
	}

	/* Setup the command packet */
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->opcode__sgloffset = TW_OPSGL_IN(0, TW_OP_FLUSH_CACHE);
	command_packet->size = 2;
	command_packet->request_id = request_id;
	command_packet->unit__hostid = TW_UNITHOST_IN(0, tw_dev->srb[request_id]->device->id);
	command_packet->status = 0;
	command_packet->flags = 0;
	command_packet->byte6.parameter_count = 1;
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_synchronize_cache(): Bad command packet physical address.\n");
		return 1;
	}

	/* Now try to post the command packet */
	tw_post_command_packet(tw_dev, request_id);

	return 0;
} /* End tw_scsiop_synchronize_cache() */

/* This function will handle test unit ready scsi command */
static int tw_scsiop_test_unit_ready(TW_Device_Extension *tw_dev, int request_id)
{
	TW_Param *param;
	TW_Command *command_packet;
	unsigned long command_que_value;
	unsigned long param_value;

	dprintk(KERN_NOTICE "3w-xxxx: tw_scsiop_test_unit_ready()\n");

	/* Initialize command packet */
	command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
	if (command_packet == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_test_unit_ready(): Bad command packet virtual address.\n");
		return 1;
	}
	memset(command_packet, 0, sizeof(TW_Sector));
	command_packet->opcode__sgloffset = TW_OPSGL_IN(2, TW_OP_GET_PARAM);
	command_packet->size = 4;
	command_packet->request_id = request_id;
	command_packet->status = 0;
	command_packet->flags = 0;
	command_packet->byte6.parameter_count = 1;

	/* Now setup the param */
	if (tw_dev->alignment_virtual_address[request_id] == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_test_unit_ready(): Bad alignment virtual address.\n");
		return 1;
	}
	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	memset(param, 0, sizeof(TW_Sector));
	param->table_id = 3;	 /* unit summary table */
	param->parameter_id = 3; /* unitsstatus parameter */
	param->parameter_size_bytes = TW_MAX_UNITS;
	param_value = tw_dev->alignment_physical_address[request_id];
	if (param_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_test_unit_ready(): Bad alignment physical address.\n");
		return 1;
	}

	command_packet->byte8.param.sgl[0].address = param_value;
	command_packet->byte8.param.sgl[0].length = sizeof(TW_Sector);
	command_que_value = tw_dev->command_packet_physical_address[request_id];
	if (command_que_value == 0) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_test_unit_ready(): Bad command packet physical address.\n");
		return 1;
	}

	/* Now try to post the command packet */
	tw_post_command_packet(tw_dev, request_id);

	return 0;
} /* End tw_scsiop_test_unit_ready() */

/* This function is called by the isr to complete a testunitready command */
static int tw_scsiop_test_unit_ready_complete(TW_Device_Extension *tw_dev, int request_id)
{
	unsigned char *is_unit_present;
	TW_Param *param;

	dprintk(KERN_WARNING "3w-xxxx: tw_scsiop_test_unit_ready_complete()\n");

	param = (TW_Param *)tw_dev->alignment_virtual_address[request_id];
	if (param == NULL) {
		printk(KERN_WARNING "3w-xxxx: tw_scsiop_test_unit_ready_complete(): Bad alignment virtual address.\n");
		return 1;
	}
	is_unit_present = &(param->data[0]);

	if (is_unit_present[tw_dev->srb[request_id]->device->id] & TW_UNIT_ONLINE) {
		tw_dev->is_unit_present[tw_dev->srb[request_id]->device->id] = 1;
	} else {
		tw_dev->is_unit_present[tw_dev->srb[request_id]->device->id] = 0;
		tw_dev->srb[request_id]->result = (DID_BAD_TARGET << 16);
		return TW_ISR_DONT_RESULT;
	}

	return 0;
} /* End tw_scsiop_test_unit_ready_complete() */

/* This is the main scsi queue function to handle scsi opcodes */
static int tw_scsi_queue(struct scsi_cmnd *SCpnt, void (*done)(struct scsi_cmnd *)) 
{
	unsigned char *command = SCpnt->cmnd;
	int request_id = 0;
	int retval = 1;
	TW_Device_Extension *tw_dev = (TW_Device_Extension *)SCpnt->device->host->hostdata;

	/* If we are resetting due to timed out ioctl, report as busy */
	if (test_bit(TW_IN_RESET, &tw_dev->flags))
		return SCSI_MLQUEUE_HOST_BUSY;

	/* Save done function into Scsi_Cmnd struct */
	SCpnt->scsi_done = done;
		 
	/* Queue the command and get a request id */
	tw_state_request_start(tw_dev, &request_id);

	/* Save the scsi command for use by the ISR */
	tw_dev->srb[request_id] = SCpnt;

	/* Initialize phase to zero */
	SCpnt->SCp.phase = TW_PHASE_INITIAL;

	switch (*command) {
		case READ_10:
		case READ_6:
		case WRITE_10:
		case WRITE_6:
			dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue(): caught READ/WRITE.\n");
			retval = tw_scsiop_read_write(tw_dev, request_id);
			break;
		case TEST_UNIT_READY:
			dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue(): caught TEST_UNIT_READY.\n");
			retval = tw_scsiop_test_unit_ready(tw_dev, request_id);
			break;
		case INQUIRY:
			dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue(): caught INQUIRY.\n");
			retval = tw_scsiop_inquiry(tw_dev, request_id);
			break;
		case READ_CAPACITY:
			dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue(): caught READ_CAPACITY.\n");
			retval = tw_scsiop_read_capacity(tw_dev, request_id);
			break;
	        case REQUEST_SENSE:
		        dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue(): caught REQUEST_SENSE.\n");
		        retval = tw_scsiop_request_sense(tw_dev, request_id);
		        break;
		case MODE_SENSE:
			dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue(): caught MODE_SENSE.\n");
			retval = tw_scsiop_mode_sense(tw_dev, request_id);
			break;
		case SYNCHRONIZE_CACHE:
			dprintk(KERN_NOTICE "3w-xxxx: tw_scsi_queue(): caught SYNCHRONIZE_CACHE.\n");
			retval = tw_scsiop_synchronize_cache(tw_dev, request_id);
			break;
		case TW_IOCTL:
			printk(KERN_WARNING "3w-xxxx: SCSI_IOCTL_SEND_COMMAND deprecated, please update your 3ware tools.\n");
			break;
		default:
			printk(KERN_NOTICE "3w-xxxx: scsi%d: Unknown scsi opcode: 0x%x\n", tw_dev->host->host_no, *command);
			tw_dev->state[request_id] = TW_S_COMPLETED;
			tw_state_request_finish(tw_dev, request_id);
			SCpnt->result = (DID_BAD_TARGET << 16);
			done(SCpnt);
			retval = 0;
	}
	if (retval) {
		tw_dev->state[request_id] = TW_S_COMPLETED;
		tw_state_request_finish(tw_dev, request_id);
		SCpnt->result = (DID_ERROR << 16);
		done(SCpnt);
		retval = 0;
	}
	return retval;
} /* End tw_scsi_queue() */

/* This function is the interrupt service routine */
static irqreturn_t tw_interrupt(int irq, void *dev_instance) 
{
	int request_id;
	u32 status_reg_value;
	TW_Device_Extension *tw_dev = (TW_Device_Extension *)dev_instance;
	TW_Response_Queue response_que;
	int error = 0, retval = 0;
	TW_Command *command_packet;
	int handled = 0;

	/* Get the host lock for io completions */
	spin_lock(tw_dev->host->host_lock);

	/* Read the registers */
	status_reg_value = inl(TW_STATUS_REG_ADDR(tw_dev));

	/* Check if this is our interrupt, otherwise bail */
	if (!(status_reg_value & TW_STATUS_VALID_INTERRUPT))
		goto tw_interrupt_bail;

	handled = 1;

	/* If we are resetting, bail */
	if (test_bit(TW_IN_RESET, &tw_dev->flags))
		goto tw_interrupt_bail;

	/* Check controller for errors */
	if (tw_check_bits(status_reg_value)) {
		dprintk(KERN_WARNING "3w-xxxx: tw_interrupt(): Unexpected bits.\n");
		if (tw_decode_bits(tw_dev, status_reg_value, 1)) {
			TW_CLEAR_ALL_INTERRUPTS(tw_dev);
			goto tw_interrupt_bail;
		}
	}

	/* Handle host interrupt */
	if (status_reg_value & TW_STATUS_HOST_INTERRUPT) {
		dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): Received host interrupt.\n");
		TW_CLEAR_HOST_INTERRUPT(tw_dev);
	}

	/* Handle attention interrupt */
	if (status_reg_value & TW_STATUS_ATTENTION_INTERRUPT) {
		dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): Received attention interrupt.\n");
		TW_CLEAR_ATTENTION_INTERRUPT(tw_dev);
		tw_state_request_start(tw_dev, &request_id);
		error = tw_aen_read_queue(tw_dev, request_id);
		if (error) {
			printk(KERN_WARNING "3w-xxxx: scsi%d: Error reading aen queue.\n", tw_dev->host->host_no);
			tw_dev->state[request_id] = TW_S_COMPLETED;
			tw_state_request_finish(tw_dev, request_id);
		}
	}

	/* Handle command interrupt */
	if (status_reg_value & TW_STATUS_COMMAND_INTERRUPT) {
		/* Drain as many pending commands as we can */
		while (tw_dev->pending_request_count > 0) {
			request_id = tw_dev->pending_queue[tw_dev->pending_head];
			if (tw_dev->state[request_id] != TW_S_PENDING) {
				printk(KERN_WARNING "3w-xxxx: scsi%d: Found request id that wasn't pending.\n", tw_dev->host->host_no);
				break;
			}
			if (tw_post_command_packet(tw_dev, request_id)==0) {
				if (tw_dev->pending_head == TW_Q_LENGTH-1) {
					tw_dev->pending_head = TW_Q_START;
				} else {
					tw_dev->pending_head = tw_dev->pending_head + 1;
				}
				tw_dev->pending_request_count--;
			} else {
				/* If we get here, we will continue re-posting on the next command interrupt */
				break;
			}
		}
		/* If there are no more pending requests, we mask command interrupt */
		if (tw_dev->pending_request_count == 0) 
			TW_MASK_COMMAND_INTERRUPT(tw_dev);
	}

	/* Handle response interrupt */
	if (status_reg_value & TW_STATUS_RESPONSE_INTERRUPT) {
		/* Drain the response queue from the board */
		while ((status_reg_value & TW_STATUS_RESPONSE_QUEUE_EMPTY) == 0) {
			/* Read response queue register */
			response_que.value = inl(TW_RESPONSE_QUEUE_REG_ADDR(tw_dev));
			request_id = TW_RESID_OUT(response_que.response_id);
			command_packet = (TW_Command *)tw_dev->command_packet_virtual_address[request_id];
			error = 0;

			/* Check for bad response */
			if (command_packet->status != 0) {
				/* If internal command, don't error, don't fill sense */
				if (tw_dev->srb[request_id] == NULL) {
					tw_decode_sense(tw_dev, request_id, 0);
				} else {
					error = tw_decode_sense(tw_dev, request_id, 1);
				}
			}

			/* Check for correct state */
			if (tw_dev->state[request_id] != TW_S_POSTED) {
				if (tw_dev->srb[request_id] != NULL) {
					printk(KERN_WARNING "3w-xxxx: scsi%d: Received a request id that wasn't posted.\n", tw_dev->host->host_no);
					error = 1;
				}
			}

			dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): Response queue request id: %d.\n", request_id);

			/* Check for internal command completion */
			if (tw_dev->srb[request_id] == NULL) {
				dprintk(KERN_WARNING "3w-xxxx: tw_interrupt(): Found internally posted command.\n");
				/* Check for chrdev ioctl completion */
				if (request_id != tw_dev->chrdev_request_id) {
					retval = tw_aen_complete(tw_dev, request_id);
					if (retval) {
						printk(KERN_WARNING "3w-xxxx: scsi%d: Error completing aen.\n", tw_dev->host->host_no);
					}
				} else {
					tw_dev->chrdev_request_id = TW_IOCTL_CHRDEV_FREE;
					wake_up(&tw_dev->ioctl_wqueue);
				}
			} else {
				switch (tw_dev->srb[request_id]->cmnd[0]) {
				case READ_10:
				case READ_6:
					dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): caught READ_10/READ_6\n");
					break;
				case WRITE_10:
				case WRITE_6:
					dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): caught WRITE_10/WRITE_6\n");
					break;
				case TEST_UNIT_READY:
					dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): caught TEST_UNIT_READY\n");
					error = tw_scsiop_test_unit_ready_complete(tw_dev, request_id);
					break;
				case INQUIRY:
					dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): caught INQUIRY\n");
					error = tw_scsiop_inquiry_complete(tw_dev, request_id);
					break;
				case READ_CAPACITY:
					dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): caught READ_CAPACITY\n");
					error = tw_scsiop_read_capacity_complete(tw_dev, request_id);
					break;
				case MODE_SENSE:
					dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): caught MODE_SENSE\n");
					error = tw_scsiop_mode_sense_complete(tw_dev, request_id);
					break;
				case SYNCHRONIZE_CACHE:
					dprintk(KERN_NOTICE "3w-xxxx: tw_interrupt(): caught SYNCHRONIZE_CACHE\n");
					break;
				default:
					printk(KERN_WARNING "3w-xxxx: case slip in tw_interrupt()\n");
					error = 1;
				}

				/* If no error command was a success */
				if (error == 0) {
					tw_dev->srb[request_id]->result = (DID_OK << 16);
				}

				/* If error, command failed */
				if (error == 1) {
					/* Ask for a host reset */
					tw_dev->srb[request_id]->result = (DID_OK << 16) | (CHECK_CONDITION << 1);
				}

				/* Now complete the io */
				if ((error != TW_ISR_DONT_COMPLETE)) {
					tw_dev->state[request_id] = TW_S_COMPLETED;
					tw_state_request_finish(tw_dev, request_id);
					tw_dev->posted_request_count--;
					tw_dev->srb[request_id]->scsi_done(tw_dev->srb[request_id]);
					
					tw_unmap_scsi_data(tw_dev->tw_pci_dev, tw_dev->srb[request_id]);
				}
			}
				
			/* Check for valid status after each drain */
			status_reg_value = inl(TW_STATUS_REG_ADDR(tw_dev));
			if (tw_check_bits(status_reg_value)) {
				dprintk(KERN_WARNING "3w-xxxx: tw_interrupt(): Unexpected bits.\n");
				if (tw_decode_bits(tw_dev, status_reg_value, 1)) {
					TW_CLEAR_ALL_INTERRUPTS(tw_dev);
					goto tw_interrupt_bail;
				}
			}
		}
	}

tw_interrupt_bail:
	spin_unlock(tw_dev->host->host_lock);
	return IRQ_RETVAL(handled);
} /* End tw_interrupt() */

/* This function tells the controller to shut down */
static void __tw_shutdown(TW_Device_Extension *tw_dev)
{
	/* Disable interrupts */
	TW_DISABLE_INTERRUPTS(tw_dev);

	/* Free up the IRQ */
	free_irq(tw_dev->tw_pci_dev->irq, tw_dev);

	printk(KERN_WARNING "3w-xxxx: Shutting down host %d.\n", tw_dev->host->host_no);

	/* Tell the card we are shutting down */
	if (tw_initconnection(tw_dev, 1)) {
		printk(KERN_WARNING "3w-xxxx: Connection shutdown failed.\n");
	} else {
		printk(KERN_WARNING "3w-xxxx: Shutdown complete.\n");
	}

	/* Clear all interrupts just before exit */
	TW_ENABLE_AND_CLEAR_INTERRUPTS(tw_dev);
} /* End __tw_shutdown() */

/* Wrapper for __tw_shutdown */
static void tw_shutdown(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	TW_Device_Extension *tw_dev = (TW_Device_Extension *)host->hostdata;

	__tw_shutdown(tw_dev);
} /* End tw_shutdown() */

static struct scsi_host_template driver_template = {
	.module			= THIS_MODULE,
	.name			= "3ware Storage Controller",
	.queuecommand		= tw_scsi_queue,
	.eh_host_reset_handler	= tw_scsi_eh_reset,
	.bios_param		= tw_scsi_biosparam,
	.change_queue_depth	= tw_change_queue_depth,
	.can_queue		= TW_Q_LENGTH-2,
	.this_id		= -1,
	.sg_tablesize		= TW_MAX_SGL_LENGTH,
	.max_sectors		= TW_MAX_SECTORS,
	.cmd_per_lun		= TW_MAX_CMDS_PER_LUN,	
	.use_clustering		= ENABLE_CLUSTERING,
	.shost_attrs		= tw_host_attrs,
	.emulated		= 1
};

/* This function will probe and initialize a card */
static int __devinit tw_probe(struct pci_dev *pdev, const struct pci_device_id *dev_id)
{
	struct Scsi_Host *host = NULL;
	TW_Device_Extension *tw_dev;
	int retval = -ENODEV;

	retval = pci_enable_device(pdev);
	if (retval) {
		printk(KERN_WARNING "3w-xxxx: Failed to enable pci device.");
		goto out_disable_device;
	}

	pci_set_master(pdev);

	retval = pci_set_dma_mask(pdev, TW_DMA_MASK);
	if (retval) {
		printk(KERN_WARNING "3w-xxxx: Failed to set dma mask.");
		goto out_disable_device;
	}

	host = scsi_host_alloc(&driver_template, sizeof(TW_Device_Extension));
	if (!host) {
		printk(KERN_WARNING "3w-xxxx: Failed to allocate memory for device extension.");
		retval = -ENOMEM;
		goto out_disable_device;
	}
	tw_dev = (TW_Device_Extension *)host->hostdata;

	memset(tw_dev, 0, sizeof(TW_Device_Extension));

	/* Save values to device extension */
	tw_dev->host = host;
	tw_dev->tw_pci_dev = pdev;

	if (tw_initialize_device_extension(tw_dev)) {
		printk(KERN_WARNING "3w-xxxx: Failed to initialize device extension.");
		goto out_free_device_extension;
	}

	/* Request IO regions */
	retval = pci_request_regions(pdev, "3w-xxxx");
	if (retval) {
		printk(KERN_WARNING "3w-xxxx: Failed to get mem region.");
		goto out_free_device_extension;
	}

	/* Save base address */
	tw_dev->base_addr = pci_resource_start(pdev, 0);
	if (!tw_dev->base_addr) {
		printk(KERN_WARNING "3w-xxxx: Failed to get io address.");
		goto out_release_mem_region;
	}

	/* Disable interrupts on the card */
	TW_DISABLE_INTERRUPTS(tw_dev);

	/* Initialize the card */
	if (tw_reset_sequence(tw_dev))
		goto out_release_mem_region;

	/* Set host specific parameters */
	host->max_id = TW_MAX_UNITS;
	host->max_cmd_len = TW_MAX_CDB_LEN;

	/* Luns and channels aren't supported by adapter */
	host->max_lun = 0;
	host->max_channel = 0;

	/* Register the card with the kernel SCSI layer */
	retval = scsi_add_host(host, &pdev->dev);
	if (retval) {
		printk(KERN_WARNING "3w-xxxx: scsi add host failed");
		goto out_release_mem_region;
	}

	pci_set_drvdata(pdev, host);

	printk(KERN_WARNING "3w-xxxx: scsi%d: Found a 3ware Storage Controller at 0x%x, IRQ: %d.\n", host->host_no, tw_dev->base_addr, pdev->irq);

	/* Now setup the interrupt handler */
	retval = request_irq(pdev->irq, tw_interrupt, IRQF_SHARED, "3w-xxxx", tw_dev);
	if (retval) {
		printk(KERN_WARNING "3w-xxxx: Error requesting IRQ.");
		goto out_remove_host;
	}

	tw_device_extension_list[tw_device_extension_count] = tw_dev;
	tw_device_extension_count++;

	/* Re-enable interrupts on the card */
	TW_ENABLE_AND_CLEAR_INTERRUPTS(tw_dev);

	/* Finally, scan the host */
	scsi_scan_host(host);

	if (twe_major == -1) {
		if ((twe_major = register_chrdev (0, "twe", &tw_fops)) < 0)
			printk(KERN_WARNING "3w-xxxx: Failed to register character device.");
	}
	return 0;

out_remove_host:
	scsi_remove_host(host);
out_release_mem_region:
	pci_release_regions(pdev);
out_free_device_extension:
	tw_free_device_extension(tw_dev);
	scsi_host_put(host);
out_disable_device:
	pci_disable_device(pdev);

	return retval;
} /* End tw_probe() */

/* This function is called to remove a device */
static void tw_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	TW_Device_Extension *tw_dev = (TW_Device_Extension *)host->hostdata;

	scsi_remove_host(tw_dev->host);

	/* Unregister character device */
	if (twe_major >= 0) {
		unregister_chrdev(twe_major, "twe");
		twe_major = -1;
	}

	/* Shutdown the card */
	__tw_shutdown(tw_dev);

	/* Free up the mem region */
	pci_release_regions(pdev);

	/* Free up device extension resources */
	tw_free_device_extension(tw_dev);

	scsi_host_put(tw_dev->host);
	pci_disable_device(pdev);
	tw_device_extension_count--;
} /* End tw_remove() */

/* PCI Devices supported by this driver */
static struct pci_device_id tw_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_3WARE, PCI_DEVICE_ID_3WARE_1000,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_3WARE, PCI_DEVICE_ID_3WARE_7000,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ }
};
MODULE_DEVICE_TABLE(pci, tw_pci_tbl);

/* pci_driver initializer */
static struct pci_driver tw_driver = {
	.name		= "3w-xxxx",
	.id_table	= tw_pci_tbl,
	.probe		= tw_probe,
	.remove		= tw_remove,
	.shutdown	= tw_shutdown,
};

/* This function is called on driver initialization */
static int __init tw_init(void)
{
	printk(KERN_WARNING "3ware Storage Controller device driver for Linux v%s.\n", TW_DRIVER_VERSION);

	return pci_register_driver(&tw_driver);
} /* End tw_init() */

/* This function is called on driver exit */
static void __exit tw_exit(void)
{
	pci_unregister_driver(&tw_driver);
} /* End tw_exit() */

module_init(tw_init);
module_exit(tw_exit);

