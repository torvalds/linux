/* Device driver for Meilhaus ME-4000 board family.
 * ================================================
 *
 *  Copyright (C) 2003 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 *  This file is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Author:	Guenter Gebhardt	<g.gebhardt@meilhaus.de>
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/unistd.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <asm/pgtable.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <asm/system.h>

/* Include-File for the Meilhaus ME-4000 I/O board */
#include "me4000.h"
#include "me4000_firmware.h"
#include "me4610_firmware.h"

/* Administrative stuff for modinfo */
MODULE_AUTHOR("Guenter Gebhardt <g.gebhardt@meilhaus.de>");
MODULE_DESCRIPTION
    ("Device Driver Module for Meilhaus ME-4000 boards version 1.0.5");
MODULE_SUPPORTED_DEVICE("Meilhaus ME-4000 Multi I/O boards");
MODULE_LICENSE("GPL");

/* Board specific data are kept in a global list */
static LIST_HEAD(me4000_board_info_list);

/* Major Device Numbers. 0 means to get it automatically from the System */
static int me4000_ao_major_driver_no;
static int me4000_ai_major_driver_no;
static int me4000_dio_major_driver_no;
static int me4000_cnt_major_driver_no;
static int me4000_ext_int_major_driver_no;

/* Let the user specify a custom major driver number */
module_param(me4000_ao_major_driver_no, int, 0);
MODULE_PARM_DESC(me4000_ao_major_driver_no,
		 "Major driver number for analog output (default 0)");

module_param(me4000_ai_major_driver_no, int, 0);
MODULE_PARM_DESC(me4000_ai_major_driver_no,
		 "Major driver number for analog input (default 0)");

module_param(me4000_dio_major_driver_no, int, 0);
MODULE_PARM_DESC(me4000_dio_major_driver_no,
		 "Major driver number digital I/O (default 0)");

module_param(me4000_cnt_major_driver_no, int, 0);
MODULE_PARM_DESC(me4000_cnt_major_driver_no,
		 "Major driver number for counter (default 0)");

module_param(me4000_ext_int_major_driver_no, int, 0);
MODULE_PARM_DESC(me4000_ext_int_major_driver_no,
		 "Major driver number for external interrupt (default 0)");

/*-----------------------------------------------------------------------------
  Board detection and initialization
  ---------------------------------------------------------------------------*/
static int me4000_probe(struct pci_dev *dev, const struct pci_device_id *id);
static int me4000_xilinx_download(struct me4000_info *);
static int me4000_reset_board(struct me4000_info *);

static void clear_board_info_list(void);
static void release_ao_contexts(struct me4000_info *board_info);
/*-----------------------------------------------------------------------------
  Stuff used by all device parts
  ---------------------------------------------------------------------------*/
static int me4000_open(struct inode *, struct file *);
static int me4000_release(struct inode *, struct file *);

static int me4000_get_user_info(struct me4000_user_info *,
				struct me4000_info *board_info);
static int me4000_read_procmem(char *, char **, off_t, int, int *, void *);

/*-----------------------------------------------------------------------------
  Analog output stuff
  ---------------------------------------------------------------------------*/
static ssize_t me4000_ao_write_sing(struct file *, const char *, size_t,
				    loff_t *);
static ssize_t me4000_ao_write_wrap(struct file *, const char *, size_t,
				    loff_t *);
static ssize_t me4000_ao_write_cont(struct file *, const char *, size_t,
				    loff_t *);

static int me4000_ao_ioctl_sing(struct inode *, struct file *, unsigned int,
				unsigned long);
static int me4000_ao_ioctl_wrap(struct inode *, struct file *, unsigned int,
				unsigned long);
static int me4000_ao_ioctl_cont(struct inode *, struct file *, unsigned int,
				unsigned long);

static unsigned int me4000_ao_poll_cont(struct file *, poll_table *);
static int me4000_ao_fsync_cont(struct file *, struct dentry *, int);

static int me4000_ao_start(unsigned long *, struct me4000_ao_context *);
static int me4000_ao_stop(struct me4000_ao_context *);
static int me4000_ao_immediate_stop(struct me4000_ao_context *);
static int me4000_ao_timer_set_divisor(u32 *, struct me4000_ao_context *);
static int me4000_ao_preload(struct me4000_ao_context *);
static int me4000_ao_preload_update(struct me4000_ao_context *);
static int me4000_ao_ex_trig_set_edge(int *, struct me4000_ao_context *);
static int me4000_ao_ex_trig_enable(struct me4000_ao_context *);
static int me4000_ao_ex_trig_disable(struct me4000_ao_context *);
static int me4000_ao_prepare(struct me4000_ao_context *ao_info);
static int me4000_ao_reset(struct me4000_ao_context *ao_info);
static int me4000_ao_enable_do(struct me4000_ao_context *);
static int me4000_ao_disable_do(struct me4000_ao_context *);
static int me4000_ao_fsm_state(int *, struct me4000_ao_context *);

static int me4000_ao_simultaneous_ex_trig(struct me4000_ao_context *ao_context);
static int me4000_ao_simultaneous_sw(struct me4000_ao_context *ao_context);
static int me4000_ao_simultaneous_disable(struct me4000_ao_context *ao_context);
static int me4000_ao_simultaneous_update(
					struct me4000_ao_channel_list *channels,
					struct me4000_ao_context *ao_context);

static int me4000_ao_synchronous_ex_trig(struct me4000_ao_context *ao_context);
static int me4000_ao_synchronous_sw(struct me4000_ao_context *ao_context);
static int me4000_ao_synchronous_disable(struct me4000_ao_context *ao_context);

static int me4000_ao_ex_trig_timeout(unsigned long *arg,
				     struct me4000_ao_context *ao_context);
static int me4000_ao_get_free_buffer(unsigned long *arg,
				     struct me4000_ao_context *ao_context);

/*-----------------------------------------------------------------------------
  Analog input stuff
  ---------------------------------------------------------------------------*/
static int me4000_ai_single(struct me4000_ai_single *,
				struct me4000_ai_context *);
static int me4000_ai_ioctl_sing(struct inode *, struct file *, unsigned int,
				unsigned long);

static ssize_t me4000_ai_read(struct file *, char *, size_t, loff_t *);
static int me4000_ai_ioctl_sw(struct inode *, struct file *, unsigned int,
			      unsigned long);
static unsigned int me4000_ai_poll(struct file *, poll_table *);
static int me4000_ai_fasync(int fd, struct file *file_p, int mode);

static int me4000_ai_ioctl_ext(struct inode *, struct file *, unsigned int,
			       unsigned long);

static int me4000_ai_prepare(struct me4000_ai_context *ai_context);
static int me4000_ai_reset(struct me4000_ai_context *ai_context);
static int me4000_ai_config(struct me4000_ai_config *,
				struct me4000_ai_context *);
static int me4000_ai_start(struct me4000_ai_context *);
static int me4000_ai_start_ex(unsigned long *, struct me4000_ai_context *);
static int me4000_ai_stop(struct me4000_ai_context *);
static int me4000_ai_immediate_stop(struct me4000_ai_context *);
static int me4000_ai_ex_trig_enable(struct me4000_ai_context *);
static int me4000_ai_ex_trig_disable(struct me4000_ai_context *);
static int me4000_ai_ex_trig_setup(struct me4000_ai_trigger *,
				   struct me4000_ai_context *);
static int me4000_ai_sc_setup(struct me4000_ai_sc *arg,
			      struct me4000_ai_context *ai_context);
static int me4000_ai_offset_enable(struct me4000_ai_context *ai_context);
static int me4000_ai_offset_disable(struct me4000_ai_context *ai_context);
static int me4000_ai_fullscale_enable(struct me4000_ai_context *ai_context);
static int me4000_ai_fullscale_disable(struct me4000_ai_context *ai_context);
static int me4000_ai_fsm_state(int *arg, struct me4000_ai_context *ai_context);
static int me4000_ai_get_count_buffer(unsigned long *arg,
				      struct me4000_ai_context *ai_context);

/*-----------------------------------------------------------------------------
  EEPROM stuff
  ---------------------------------------------------------------------------*/
static int me4000_eeprom_read(struct me4000_eeprom *arg,
			      struct me4000_ai_context *ai_context);
static int me4000_eeprom_write(struct me4000_eeprom *arg,
			       struct me4000_ai_context *ai_context);

/*-----------------------------------------------------------------------------
  Digital I/O stuff
  ---------------------------------------------------------------------------*/
static int me4000_dio_ioctl(struct inode *, struct file *, unsigned int,
			    unsigned long);
static int me4000_dio_config(struct me4000_dio_config *,
				struct me4000_dio_context *);
static int me4000_dio_get_byte(struct me4000_dio_byte *,
				struct me4000_dio_context *);
static int me4000_dio_set_byte(struct me4000_dio_byte *,
				struct me4000_dio_context *);
static int me4000_dio_reset(struct me4000_dio_context *);

/*-----------------------------------------------------------------------------
  Counter stuff
  ---------------------------------------------------------------------------*/
static int me4000_cnt_ioctl(struct inode *, struct file *, unsigned int,
			    unsigned long);
static int me4000_cnt_config(struct me4000_cnt_config *,
				struct me4000_cnt_context *);
static int me4000_cnt_read(struct me4000_cnt *, struct me4000_cnt_context *);
static int me4000_cnt_write(struct me4000_cnt *, struct me4000_cnt_context *);
static int me4000_cnt_reset(struct me4000_cnt_context *);

/*-----------------------------------------------------------------------------
  External interrupt routines
  ---------------------------------------------------------------------------*/
static int me4000_ext_int_ioctl(struct inode *, struct file *, unsigned int,
				unsigned long);
static int me4000_ext_int_enable(struct me4000_ext_int_context *);
static int me4000_ext_int_disable(struct me4000_ext_int_context *);
static int me4000_ext_int_count(unsigned long *arg,
				struct me4000_ext_int_context *ext_int_context);
static int me4000_ext_int_fasync(int fd, struct file *file_ptr, int mode);

/*-----------------------------------------------------------------------------
  The interrupt service routines
  ---------------------------------------------------------------------------*/
static irqreturn_t me4000_ao_isr(int, void *);
static irqreturn_t me4000_ai_isr(int, void *);
static irqreturn_t me4000_ext_int_isr(int, void *);

/*-----------------------------------------------------------------------------
  Inline functions
  ---------------------------------------------------------------------------*/

static inline int me4000_buf_count(struct me4000_circ_buf buf, int size)
{
	return (buf.head - buf.tail) & (size - 1);
}

static inline int me4000_buf_space(struct me4000_circ_buf buf, int size)
{
	return (buf.tail - (buf.head + 1)) & (size - 1);
}

static inline int me4000_values_to_end(struct me4000_circ_buf buf, int size)
{
	int end;
	int n;
	end = size - buf.tail;
	n = (buf.head + end) & (size - 1);
	return (n < end) ? n : end;
}

static inline int me4000_space_to_end(struct me4000_circ_buf buf, int size)
{
	int end;
	int n;

	end = size - 1 - buf.head;
	n = (end + buf.tail) & (size - 1);
	return (n <= end) ? n : (end + 1);
}

static inline void me4000_outb(unsigned char value, unsigned long port)
{
	PORT_PDEBUG("--> 0x%02X port 0x%04lX\n", value, port);
	outb(value, port);
}

static inline void me4000_outl(unsigned long value, unsigned long port)
{
	PORT_PDEBUG("--> 0x%08lX port 0x%04lX\n", value, port);
	outl(value, port);
}

static inline unsigned long me4000_inl(unsigned long port)
{
	unsigned long value;
	value = inl(port);
	PORT_PDEBUG("<-- 0x%08lX port 0x%04lX\n", value, port);
	return value;
}

static inline unsigned char me4000_inb(unsigned long port)
{
	unsigned char value;
	value = inb(port);
	PORT_PDEBUG("<-- 0x%08X port 0x%04lX\n", value, port);
	return value;
}

static struct pci_driver me4000_driver = {
	.name = ME4000_NAME,
	.id_table = me4000_pci_table,
	.probe = me4000_probe
};

static const struct file_operations me4000_ao_fops_sing = {
      .owner = THIS_MODULE,
      .write = me4000_ao_write_sing,
      .ioctl = me4000_ao_ioctl_sing,
      .open = me4000_open,
      .release = me4000_release,
};

static const struct file_operations me4000_ao_fops_wrap = {
      .owner = THIS_MODULE,
      .write = me4000_ao_write_wrap,
      .ioctl = me4000_ao_ioctl_wrap,
      .open = me4000_open,
      .release = me4000_release,
};

static const struct file_operations me4000_ao_fops_cont = {
      .owner = THIS_MODULE,
      .write = me4000_ao_write_cont,
      .poll = me4000_ao_poll_cont,
      .ioctl = me4000_ao_ioctl_cont,
      .open = me4000_open,
      .release = me4000_release,
      .fsync = me4000_ao_fsync_cont,
};

static const struct file_operations me4000_ai_fops_sing = {
      .owner = THIS_MODULE,
      .ioctl = me4000_ai_ioctl_sing,
      .open = me4000_open,
      .release = me4000_release,
};

static const struct file_operations me4000_ai_fops_cont_sw = {
      .owner = THIS_MODULE,
      .read = me4000_ai_read,
      .poll = me4000_ai_poll,
      .ioctl = me4000_ai_ioctl_sw,
      .open = me4000_open,
      .release = me4000_release,
      .fasync = me4000_ai_fasync,
};

static const struct file_operations me4000_ai_fops_cont_et = {
      .owner = THIS_MODULE,
      .read = me4000_ai_read,
      .poll = me4000_ai_poll,
      .ioctl = me4000_ai_ioctl_ext,
      .open = me4000_open,
      .release = me4000_release,
};

static const struct file_operations me4000_ai_fops_cont_et_value = {
      .owner = THIS_MODULE,
      .read = me4000_ai_read,
      .poll = me4000_ai_poll,
      .ioctl = me4000_ai_ioctl_ext,
      .open = me4000_open,
      .release = me4000_release,
};

static const struct file_operations me4000_ai_fops_cont_et_chanlist = {
      .owner = THIS_MODULE,
      .read = me4000_ai_read,
      .poll = me4000_ai_poll,
      .ioctl = me4000_ai_ioctl_ext,
      .open = me4000_open,
      .release = me4000_release,
};

static const struct file_operations me4000_dio_fops = {
      .owner = THIS_MODULE,
      .ioctl = me4000_dio_ioctl,
      .open = me4000_open,
      .release = me4000_release,
};

static const struct file_operations me4000_cnt_fops = {
      .owner = THIS_MODULE,
      .ioctl = me4000_cnt_ioctl,
      .open = me4000_open,
      .release = me4000_release,
};

static const struct file_operations me4000_ext_int_fops = {
      .owner = THIS_MODULE,
      .ioctl = me4000_ext_int_ioctl,
      .open = me4000_open,
      .release = me4000_release,
      .fasync = me4000_ext_int_fasync,
};

static const struct file_operations *me4000_ao_fops_array[] = {
	/* single operations */
	&me4000_ao_fops_sing,
	/* wraparound operations */
	&me4000_ao_fops_wrap,
	/* continuous operations */
	&me4000_ao_fops_cont,
};

static const struct file_operations *me4000_ai_fops_array[] = {
	/* single operations */
	&me4000_ai_fops_sing,
	/* continuous operations with software start */
	&me4000_ai_fops_cont_sw,
	/* continuous operations with external trigger */
	&me4000_ai_fops_cont_et,
	/* sample values by external trigger */
	&me4000_ai_fops_cont_et_value,
	/* work through one channel list by external trigger */
	&me4000_ai_fops_cont_et_chanlist,
};

static int __init me4000_init_module(void)
{
	int result;

	CALL_PDEBUG("init_module() is executed\n");

	/* Register driver capabilities */
	result = pci_register_driver(&me4000_driver);
	PDEBUG("init_module():%d devices detected\n", result);
	if (result < 0) {
		printk(KERN_ERR "ME4000:init_module():Can't register driver\n");
		goto INIT_ERROR_1;
	}

	/* Allocate major number for analog output */
	result =
	    register_chrdev(me4000_ao_major_driver_no, ME4000_AO_NAME,
			    &me4000_ao_fops_sing);
	if (result < 0) {
		printk(KERN_ERR "ME4000:init_module():Can't get AO major no\n");
		goto INIT_ERROR_2;
	} else {
		me4000_ao_major_driver_no = result;
	}
	PDEBUG("init_module():Major driver number for AO = %ld\n",
	       me4000_ao_major_driver_no);

	/* Allocate major number for analog input  */
	result =
	    register_chrdev(me4000_ai_major_driver_no, ME4000_AI_NAME,
			    &me4000_ai_fops_sing);
	if (result < 0) {
		printk(KERN_ERR "ME4000:init_module():Can't get AI major no\n");
		goto INIT_ERROR_3;
	} else {
		me4000_ai_major_driver_no = result;
	}
	PDEBUG("init_module():Major driver number for AI = %ld\n",
	       me4000_ai_major_driver_no);

	/* Allocate major number for digital I/O */
	result =
	    register_chrdev(me4000_dio_major_driver_no, ME4000_DIO_NAME,
			    &me4000_dio_fops);
	if (result < 0) {
		printk(KERN_ERR
		       "ME4000:init_module():Can't get DIO major no\n");
		goto INIT_ERROR_4;
	} else {
		me4000_dio_major_driver_no = result;
	}
	PDEBUG("init_module():Major driver number for DIO = %ld\n",
	       me4000_dio_major_driver_no);

	/* Allocate major number for counter */
	result =
	    register_chrdev(me4000_cnt_major_driver_no, ME4000_CNT_NAME,
			    &me4000_cnt_fops);
	if (result < 0) {
		printk(KERN_ERR
		       "ME4000:init_module():Can't get CNT major no\n");
		goto INIT_ERROR_5;
	} else {
		me4000_cnt_major_driver_no = result;
	}
	PDEBUG("init_module():Major driver number for CNT = %ld\n",
	       me4000_cnt_major_driver_no);

	/* Allocate major number for external interrupt */
	result =
	    register_chrdev(me4000_ext_int_major_driver_no, ME4000_EXT_INT_NAME,
			    &me4000_ext_int_fops);
	if (result < 0) {
		printk(KERN_ERR
		       "ME4000:init_module():Can't get major no for external interrupt\n");
		goto INIT_ERROR_6;
	} else {
		me4000_ext_int_major_driver_no = result;
	}
	PDEBUG
	    ("init_module():Major driver number for external interrupt = %ld\n",
	     me4000_ext_int_major_driver_no);

	/* Create the /proc/me4000 entry */
	if (!create_proc_read_entry
	    ("me4000", 0, NULL, me4000_read_procmem, NULL)) {
		result = -ENODEV;
		printk(KERN_ERR
		       "ME4000:init_module():Can't create proc entry\n");
		goto INIT_ERROR_7;
	}

	return 0;

INIT_ERROR_7:
	unregister_chrdev(me4000_ext_int_major_driver_no, ME4000_EXT_INT_NAME);

INIT_ERROR_6:
	unregister_chrdev(me4000_cnt_major_driver_no, ME4000_CNT_NAME);

INIT_ERROR_5:
	unregister_chrdev(me4000_dio_major_driver_no, ME4000_DIO_NAME);

INIT_ERROR_4:
	unregister_chrdev(me4000_ai_major_driver_no, ME4000_AI_NAME);

INIT_ERROR_3:
	unregister_chrdev(me4000_ao_major_driver_no, ME4000_AO_NAME);

INIT_ERROR_2:
	pci_unregister_driver(&me4000_driver);
	clear_board_info_list();

INIT_ERROR_1:
	return result;
}

module_init(me4000_init_module);

static void clear_board_info_list(void)
{
	struct me4000_info *board_info, *board_info_safe;
	struct me4000_ao_context *ao_context, *ao_context_safe;

	/* Clear context lists */
	list_for_each_entry(board_info, &me4000_board_info_list, list) {
		/* Clear analog output context list */
		list_for_each_entry_safe(ao_context, ao_context_safe,
				&board_info->ao_context_list, list) {
			me4000_ao_reset(ao_context);
			free_irq(ao_context->irq, ao_context);
			kfree(ao_context->circ_buf.buf);
			list_del(&ao_context->list);
			kfree(ao_context);
		}

		/* Clear analog input context */
		kfree(board_info->ai_context->circ_buf.buf);
		kfree(board_info->ai_context);

		/* Clear digital I/O context */
		kfree(board_info->dio_context);

		/* Clear counter context */
		kfree(board_info->cnt_context);

		/* Clear external interrupt context */
		kfree(board_info->ext_int_context);
	}

	/* Clear the board info list */
	list_for_each_entry_safe(board_info, board_info_safe,
			&me4000_board_info_list, list) {
		pci_release_regions(board_info->pci_dev_p);
		list_del(&board_info->list);
		kfree(board_info);
	}
}

static int get_registers(struct pci_dev *dev, struct me4000_info *board_info)
{

	/*--------------------------- plx regbase ---------------------------------*/

	board_info->plx_regbase = pci_resource_start(dev, 1);
	if (board_info->plx_regbase == 0) {
		printk(KERN_ERR
		       "ME4000:get_registers():PCI base address 1 is not available\n");
		return -ENODEV;
	}
	board_info->plx_regbase_size = pci_resource_len(dev, 1);

	PDEBUG
	    ("get_registers():PLX configuration registers at address 0x%4lX [0x%4lX]\n",
	     board_info->plx_regbase, board_info->plx_regbase_size);

	/*--------------------------- me4000 regbase ------------------------------*/

	board_info->me4000_regbase = pci_resource_start(dev, 2);
	if (board_info->me4000_regbase == 0) {
		printk(KERN_ERR
		       "ME4000:get_registers():PCI base address 2 is not available\n");
		return -ENODEV;
	}
	board_info->me4000_regbase_size = pci_resource_len(dev, 2);

	PDEBUG("get_registers():ME4000 registers at address 0x%4lX [0x%4lX]\n",
	       board_info->me4000_regbase, board_info->me4000_regbase_size);

	/*--------------------------- timer regbase ------------------------------*/

	board_info->timer_regbase = pci_resource_start(dev, 3);
	if (board_info->timer_regbase == 0) {
		printk(KERN_ERR
		       "ME4000:get_registers():PCI base address 3 is not available\n");
		return -ENODEV;
	}
	board_info->timer_regbase_size = pci_resource_len(dev, 3);

	PDEBUG("get_registers():Timer registers at address 0x%4lX [0x%4lX]\n",
	       board_info->timer_regbase, board_info->timer_regbase_size);

	/*--------------------------- program regbase ------------------------------*/

	board_info->program_regbase = pci_resource_start(dev, 5);
	if (board_info->program_regbase == 0) {
		printk(KERN_ERR
		       "get_registers():ME4000:PCI base address 5 is not available\n");
		return -ENODEV;
	}
	board_info->program_regbase_size = pci_resource_len(dev, 5);

	PDEBUG("get_registers():Program registers at address 0x%4lX [0x%4lX]\n",
	       board_info->program_regbase, board_info->program_regbase_size);

	return 0;
}

static int init_board_info(struct pci_dev *pci_dev_p,
			   struct me4000_info *board_info)
{
	int i;
	int result;
	struct list_head *board_p;
	board_info->pci_dev_p = pci_dev_p;

	for (i = 0; i < ARRAY_SIZE(me4000_boards); i++) {
		if (me4000_boards[i].device_id == pci_dev_p->device) {
			board_info->board_p = &me4000_boards[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(me4000_boards)) {
		printk(KERN_ERR
		       "ME4000:init_board_info():Device ID not valid\n");
		return -ENODEV;
	}

	/* Get the index of the board in the global list */
	i = 0;
	list_for_each(board_p, &me4000_board_info_list) {
		if (board_p == &board_info->list) {
			board_info->board_count = i;
			break;
		}
		i++;
	}
	if (board_p == &me4000_board_info_list) {
		printk(KERN_ERR
		       "ME4000:init_board_info():Cannot get index of board\n");
		return -ENODEV;
	}

	/* Init list head for analog output contexts */
	INIT_LIST_HEAD(&board_info->ao_context_list);

	/* Init spin locks */
	spin_lock_init(&board_info->preload_lock);
	spin_lock_init(&board_info->ai_ctrl_lock);

	/* Get the serial number */
	result = pci_read_config_dword(pci_dev_p, 0x2C, &board_info->serial_no);
	if (result != PCIBIOS_SUCCESSFUL) {
		printk(KERN_WARNING
		       "ME4000:init_board_info: Can't get serial_no\n");
		return result;
	}
	PDEBUG("init_board_info():serial_no = 0x%x\n", board_info->serial_no);

	/* Get the hardware revision */
	result =
	    pci_read_config_byte(pci_dev_p, 0x08, &board_info->hw_revision);
	if (result != PCIBIOS_SUCCESSFUL) {
		printk(KERN_WARNING
		       "ME4000:init_board_info():Can't get hw_revision\n");
		return result;
	}
	PDEBUG("init_board_info():hw_revision = 0x%x\n",
	       board_info->hw_revision);

	/* Get the vendor id */
	board_info->vendor_id = pci_dev_p->vendor;
	PDEBUG("init_board_info():vendor_id = 0x%x\n", board_info->vendor_id);

	/* Get the device id */
	board_info->device_id = pci_dev_p->device;
	PDEBUG("init_board_info():device_id = 0x%x\n", board_info->device_id);

	/* Get the pci device number */
	board_info->pci_dev_no = PCI_FUNC(pci_dev_p->devfn);
	PDEBUG("init_board_info():pci_func_no = 0x%x\n",
	       board_info->pci_func_no);

	/* Get the pci slot number */
	board_info->pci_dev_no = PCI_SLOT(pci_dev_p->devfn);
	PDEBUG("init_board_info():pci_dev_no = 0x%x\n", board_info->pci_dev_no);

	/* Get the pci bus number */
	board_info->pci_bus_no = pci_dev_p->bus->number;
	PDEBUG("init_board_info():pci_bus_no = 0x%x\n", board_info->pci_bus_no);

	/* Get the irq assigned to the board */
	board_info->irq = pci_dev_p->irq;
	PDEBUG("init_board_info():irq = %d\n", board_info->irq);

	return 0;
}

static int alloc_ao_contexts(struct me4000_info *info)
{
	int i;
	int err;
	struct me4000_ao_context *ao_context;

	for (i = 0; i < info->board_p->ao.count; i++) {
		ao_context = kzalloc(sizeof(struct me4000_ao_context),
								GFP_KERNEL);
		if (!ao_context) {
			printk(KERN_ERR
			       "alloc_ao_contexts():Can't get memory for ao context\n");
			release_ao_contexts(info);
			return -ENOMEM;
		}

		spin_lock_init(&ao_context->use_lock);
		spin_lock_init(&ao_context->int_lock);
		ao_context->irq = info->irq;
		init_waitqueue_head(&ao_context->wait_queue);
		ao_context->board_info = info;

		if (info->board_p->ao.fifo_count) {
			/* Allocate circular buffer */
			ao_context->circ_buf.buf =
			    kzalloc(ME4000_AO_BUFFER_SIZE, GFP_KERNEL);
			if (!ao_context->circ_buf.buf) {
				printk(KERN_ERR
				       "alloc_ao_contexts():Can't get circular buffer\n");
				release_ao_contexts(info);
				return -ENOMEM;
			}

			/* Clear the circular buffer */
			ao_context->circ_buf.head = 0;
			ao_context->circ_buf.tail = 0;
		}

		switch (i) {
		case 0:
			ao_context->ctrl_reg =
			    info->me4000_regbase + ME4000_AO_00_CTRL_REG;
			ao_context->status_reg =
			    info->me4000_regbase + ME4000_AO_00_STATUS_REG;
			ao_context->fifo_reg =
			    info->me4000_regbase + ME4000_AO_00_FIFO_REG;
			ao_context->single_reg =
			    info->me4000_regbase + ME4000_AO_00_SINGLE_REG;
			ao_context->timer_reg =
			    info->me4000_regbase + ME4000_AO_00_TIMER_REG;
			ao_context->irq_status_reg =
			    info->me4000_regbase + ME4000_IRQ_STATUS_REG;
			ao_context->preload_reg =
			    info->me4000_regbase + ME4000_AO_LOADSETREG_XX;
			break;
		case 1:
			ao_context->ctrl_reg =
			    info->me4000_regbase + ME4000_AO_01_CTRL_REG;
			ao_context->status_reg =
			    info->me4000_regbase + ME4000_AO_01_STATUS_REG;
			ao_context->fifo_reg =
			    info->me4000_regbase + ME4000_AO_01_FIFO_REG;
			ao_context->single_reg =
			    info->me4000_regbase + ME4000_AO_01_SINGLE_REG;
			ao_context->timer_reg =
			    info->me4000_regbase + ME4000_AO_01_TIMER_REG;
			ao_context->irq_status_reg =
			    info->me4000_regbase + ME4000_IRQ_STATUS_REG;
			ao_context->preload_reg =
			    info->me4000_regbase + ME4000_AO_LOADSETREG_XX;
			break;
		case 2:
			ao_context->ctrl_reg =
			    info->me4000_regbase + ME4000_AO_02_CTRL_REG;
			ao_context->status_reg =
			    info->me4000_regbase + ME4000_AO_02_STATUS_REG;
			ao_context->fifo_reg =
			    info->me4000_regbase + ME4000_AO_02_FIFO_REG;
			ao_context->single_reg =
			    info->me4000_regbase + ME4000_AO_02_SINGLE_REG;
			ao_context->timer_reg =
			    info->me4000_regbase + ME4000_AO_02_TIMER_REG;
			ao_context->irq_status_reg =
			    info->me4000_regbase + ME4000_IRQ_STATUS_REG;
			ao_context->preload_reg =
			    info->me4000_regbase + ME4000_AO_LOADSETREG_XX;
			break;
		case 3:
			ao_context->ctrl_reg =
			    info->me4000_regbase + ME4000_AO_03_CTRL_REG;
			ao_context->status_reg =
			    info->me4000_regbase + ME4000_AO_03_STATUS_REG;
			ao_context->fifo_reg =
			    info->me4000_regbase + ME4000_AO_03_FIFO_REG;
			ao_context->single_reg =
			    info->me4000_regbase + ME4000_AO_03_SINGLE_REG;
			ao_context->timer_reg =
			    info->me4000_regbase + ME4000_AO_03_TIMER_REG;
			ao_context->irq_status_reg =
			    info->me4000_regbase + ME4000_IRQ_STATUS_REG;
			ao_context->preload_reg =
			    info->me4000_regbase + ME4000_AO_LOADSETREG_XX;
			break;
		default:
			break;
		}

		if (info->board_p->ao.fifo_count) {
			/* Request the interrupt line */
			err =
			    request_irq(ao_context->irq, me4000_ao_isr,
					IRQF_DISABLED | IRQF_SHARED,
					ME4000_NAME, ao_context);
			if (err) {
				printk(KERN_ERR
				       "%s:Can't get interrupt line", __func__);
				kfree(ao_context->circ_buf.buf);
				kfree(ao_context);
				release_ao_contexts(info);
				return -ENODEV;
			}
		}

		list_add_tail(&ao_context->list, &info->ao_context_list);
		ao_context->index = i;
	}

	return 0;
}

static void release_ao_contexts(struct me4000_info *board_info)
{
	struct me4000_ao_context *ao_context, *ao_context_safe;

	/* Clear analog output context list */
	list_for_each_entry_safe(ao_context, ao_context_safe,
			&board_info->ao_context_list, list) {
		free_irq(ao_context->irq, ao_context);
		kfree(ao_context->circ_buf.buf);
		list_del(&ao_context->list);
		kfree(ao_context);
	}
}

static int alloc_ai_context(struct me4000_info *info)
{
	struct me4000_ai_context *ai_context;

	if (info->board_p->ai.count) {
		ai_context = kzalloc(sizeof(struct me4000_ai_context),
								GFP_KERNEL);
		if (!ai_context) {
			printk(KERN_ERR
			       "ME4000:alloc_ai_context():Can't get memory for ai context\n");
			return -ENOMEM;
		}

		info->ai_context = ai_context;

		spin_lock_init(&ai_context->use_lock);
		spin_lock_init(&ai_context->int_lock);
		ai_context->number = 0;
		ai_context->irq = info->irq;
		init_waitqueue_head(&ai_context->wait_queue);
		ai_context->board_info = info;

		ai_context->ctrl_reg =
		    info->me4000_regbase + ME4000_AI_CTRL_REG;
		ai_context->status_reg =
		    info->me4000_regbase + ME4000_AI_STATUS_REG;
		ai_context->channel_list_reg =
		    info->me4000_regbase + ME4000_AI_CHANNEL_LIST_REG;
		ai_context->data_reg =
		    info->me4000_regbase + ME4000_AI_DATA_REG;
		ai_context->chan_timer_reg =
		    info->me4000_regbase + ME4000_AI_CHAN_TIMER_REG;
		ai_context->chan_pre_timer_reg =
		    info->me4000_regbase + ME4000_AI_CHAN_PRE_TIMER_REG;
		ai_context->scan_timer_low_reg =
		    info->me4000_regbase + ME4000_AI_SCAN_TIMER_LOW_REG;
		ai_context->scan_timer_high_reg =
		    info->me4000_regbase + ME4000_AI_SCAN_TIMER_HIGH_REG;
		ai_context->scan_pre_timer_low_reg =
		    info->me4000_regbase + ME4000_AI_SCAN_PRE_TIMER_LOW_REG;
		ai_context->scan_pre_timer_high_reg =
		    info->me4000_regbase + ME4000_AI_SCAN_PRE_TIMER_HIGH_REG;
		ai_context->start_reg =
		    info->me4000_regbase + ME4000_AI_START_REG;
		ai_context->irq_status_reg =
		    info->me4000_regbase + ME4000_IRQ_STATUS_REG;
		ai_context->sample_counter_reg =
		    info->me4000_regbase + ME4000_AI_SAMPLE_COUNTER_REG;
	}

	return 0;
}

static int alloc_dio_context(struct me4000_info *info)
{
	struct me4000_dio_context *dio_context;

	if (info->board_p->dio.count) {
		dio_context = kzalloc(sizeof(struct me4000_dio_context),
								GFP_KERNEL);
		if (!dio_context) {
			printk(KERN_ERR
			       "ME4000:alloc_dio_context():Can't get memory for dio context\n");
			return -ENOMEM;
		}

		info->dio_context = dio_context;

		spin_lock_init(&dio_context->use_lock);
		dio_context->board_info = info;

		dio_context->dio_count = info->board_p->dio.count;

		dio_context->dir_reg =
		    info->me4000_regbase + ME4000_DIO_DIR_REG;
		dio_context->ctrl_reg =
		    info->me4000_regbase + ME4000_DIO_CTRL_REG;
		dio_context->port_0_reg =
		    info->me4000_regbase + ME4000_DIO_PORT_0_REG;
		dio_context->port_1_reg =
		    info->me4000_regbase + ME4000_DIO_PORT_1_REG;
		dio_context->port_2_reg =
		    info->me4000_regbase + ME4000_DIO_PORT_2_REG;
		dio_context->port_3_reg =
		    info->me4000_regbase + ME4000_DIO_PORT_3_REG;
	}

	return 0;
}

static int alloc_cnt_context(struct me4000_info *info)
{
	struct me4000_cnt_context *cnt_context;

	if (info->board_p->cnt.count) {
		cnt_context = kzalloc(sizeof(struct me4000_cnt_context),
								GFP_KERNEL);
		if (!cnt_context) {
			printk(KERN_ERR
			       "ME4000:alloc_cnt_context():Can't get memory for cnt context\n");
			return -ENOMEM;
		}

		info->cnt_context = cnt_context;

		spin_lock_init(&cnt_context->use_lock);
		cnt_context->board_info = info;

		cnt_context->ctrl_reg =
		    info->timer_regbase + ME4000_CNT_CTRL_REG;
		cnt_context->counter_0_reg =
		    info->timer_regbase + ME4000_CNT_COUNTER_0_REG;
		cnt_context->counter_1_reg =
		    info->timer_regbase + ME4000_CNT_COUNTER_1_REG;
		cnt_context->counter_2_reg =
		    info->timer_regbase + ME4000_CNT_COUNTER_2_REG;
	}

	return 0;
}

static int alloc_ext_int_context(struct me4000_info *info)
{
	struct me4000_ext_int_context *ext_int_context;

	if (info->board_p->cnt.count) {
		ext_int_context =
		    kzalloc(sizeof(struct me4000_ext_int_context), GFP_KERNEL);
		if (!ext_int_context) {
			printk(KERN_ERR
			       "ME4000:alloc_ext_int_context():Can't get memory for cnt context\n");
			return -ENOMEM;
		}

		info->ext_int_context = ext_int_context;

		spin_lock_init(&ext_int_context->use_lock);
		ext_int_context->board_info = info;

		ext_int_context->fasync_ptr = NULL;
		ext_int_context->irq = info->irq;

		ext_int_context->ctrl_reg =
		    info->me4000_regbase + ME4000_AI_CTRL_REG;
		ext_int_context->irq_status_reg =
		    info->me4000_regbase + ME4000_IRQ_STATUS_REG;
	}

	return 0;
}

static int me4000_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int result = 0;
	struct me4000_info *board_info;

	CALL_PDEBUG("me4000_probe() is executed\n");

	/* Allocate structure for board context */
	board_info = kzalloc(sizeof(struct me4000_info), GFP_KERNEL);
	if (!board_info) {
		printk(KERN_ERR
		       "ME4000:Can't get memory for board info structure\n");
		result = -ENOMEM;
		goto PROBE_ERROR_1;
	}

	/* Add to global linked list */
	list_add_tail(&board_info->list, &me4000_board_info_list);

	/* Get the PCI base registers */
	result = get_registers(dev, board_info);
	if (result) {
		printk(KERN_ERR "%s:Cannot get registers\n", __func__);
		goto PROBE_ERROR_2;
	}

	/* Enable the device */
	result = pci_enable_device(dev);
	if (result < 0) {
		printk(KERN_ERR "%s:Cannot enable PCI device\n", __func__);
		goto PROBE_ERROR_2;
	}

	/* Request the PCI register regions */
	result = pci_request_regions(dev, ME4000_NAME);
	if (result < 0) {
		printk(KERN_ERR "%s:Cannot request I/O regions\n", __func__);
		goto PROBE_ERROR_2;
	}

	/* Initialize board info */
	result = init_board_info(dev, board_info);
	if (result) {
		printk(KERN_ERR "%s:Cannot init baord info\n", __func__);
		goto PROBE_ERROR_3;
	}

	/* Download the xilinx firmware */
	result = me4000_xilinx_download(board_info);
	if (result) {
		printk(KERN_ERR "%s:Can't download firmware\n", __func__);
		goto PROBE_ERROR_3;
	}

	/* Make a hardware reset */
	result = me4000_reset_board(board_info);
	if (result) {
		printk(KERN_ERR "%s :Can't reset board\n", __func__);
		goto PROBE_ERROR_3;
	}

	/* Allocate analog output context structures */
	result = alloc_ao_contexts(board_info);
	if (result) {
		printk(KERN_ERR "%s:Cannot allocate ao contexts\n", __func__);
		goto PROBE_ERROR_3;
	}

	/* Allocate analog input context */
	result = alloc_ai_context(board_info);
	if (result) {
		printk(KERN_ERR "%s:Cannot allocate ai context\n", __func__);
		goto PROBE_ERROR_4;
	}

	/* Allocate digital I/O context */
	result = alloc_dio_context(board_info);
	if (result) {
		printk(KERN_ERR "%s:Cannot allocate dio context\n", __func__);
		goto PROBE_ERROR_5;
	}

	/* Allocate counter context */
	result = alloc_cnt_context(board_info);
	if (result) {
		printk(KERN_ERR "%s:Cannot allocate cnt context\n", __func__);
		goto PROBE_ERROR_6;
	}

	/* Allocate external interrupt context */
	result = alloc_ext_int_context(board_info);
	if (result) {
		printk(KERN_ERR
		       "%s:Cannot allocate ext_int context\n", __func__);
		goto PROBE_ERROR_7;
	}

	return 0;

PROBE_ERROR_7:
	kfree(board_info->cnt_context);

PROBE_ERROR_6:
	kfree(board_info->dio_context);

PROBE_ERROR_5:
	kfree(board_info->ai_context);

PROBE_ERROR_4:
	release_ao_contexts(board_info);

PROBE_ERROR_3:
	pci_release_regions(dev);

PROBE_ERROR_2:
	list_del(&board_info->list);
	kfree(board_info);

PROBE_ERROR_1:
	return result;
}

static int me4000_xilinx_download(struct me4000_info *info)
{
	int size = 0;
	u32 value = 0;
	int idx = 0;
	unsigned char *firm;
	wait_queue_head_t queue;

	CALL_PDEBUG("me4000_xilinx_download() is executed\n");

	init_waitqueue_head(&queue);

	firm = (info->device_id == 0x4610) ? xilinx_firm_4610 : xilinx_firm;

	/*
	 * Set PLX local interrupt 2 polarity to high.
	 * Interrupt is thrown by init pin of xilinx.
	 */
	outl(0x10, info->plx_regbase + PLX_INTCSR);

	/* Set /CS and /WRITE of the Xilinx */
	value = inl(info->plx_regbase + PLX_ICR);
	value |= 0x100;
	outl(value, info->plx_regbase + PLX_ICR);

	/* Init Xilinx with CS1 */
	inb(info->program_regbase + 0xC8);

	/* Wait until /INIT pin is set */
	udelay(20);
	if (!(inl(info->plx_regbase + PLX_INTCSR) & 0x20)) {
		printk(KERN_ERR "%s:Can't init Xilinx\n", __func__);
		return -EIO;
	}

	/* Reset /CS and /WRITE of the Xilinx */
	value = inl(info->plx_regbase + PLX_ICR);
	value &= ~0x100;
	outl(value, info->plx_regbase + PLX_ICR);

	/* Download Xilinx firmware */
	size = (firm[0] << 24) + (firm[1] << 16) + (firm[2] << 8) + firm[3];
	udelay(10);

	for (idx = 0; idx < size; idx++) {
		outb(firm[16 + idx], info->program_regbase);

		udelay(10);

		/* Check if BUSY flag is low */
		if (inl(info->plx_regbase + PLX_ICR) & 0x20) {
			printk(KERN_ERR
			       "%s:Xilinx is still busy (idx = %d)\n", __func__,
			       idx);
			return -EIO;
		}
	}

	PDEBUG("me4000_xilinx_download():%d bytes written\n", idx);

	/* If done flag is high download was successful */
	if (inl(info->plx_regbase + PLX_ICR) & 0x4) {
		PDEBUG("me4000_xilinx_download():Done flag is set\n");
		PDEBUG("me4000_xilinx_download():Download was successful\n");
	} else {
		printk(KERN_ERR
		       "ME4000:%s:DONE flag is not set\n", __func__);
		printk(KERN_ERR
		       "ME4000:%s:Download not succesful\n", __func__);
		return -EIO;
	}

	/* Set /CS and /WRITE */
	value = inl(info->plx_regbase + PLX_ICR);
	value |= 0x100;
	outl(value, info->plx_regbase + PLX_ICR);

	return 0;
}

static int me4000_reset_board(struct me4000_info *info)
{
	unsigned long icr;

	CALL_PDEBUG("me4000_reset_board() is executed\n");

	/* Make a hardware reset */
	icr = me4000_inl(info->plx_regbase + PLX_ICR);
	icr |= 0x40000000;
	me4000_outl(icr, info->plx_regbase + PLX_ICR);
	icr &= ~0x40000000;
	me4000_outl(icr, info->plx_regbase + PLX_ICR);

	/* Set both stop bits in the analog input control register */
	me4000_outl(ME4000_AI_CTRL_BIT_IMMEDIATE_STOP | ME4000_AI_CTRL_BIT_STOP,
		    info->me4000_regbase + ME4000_AI_CTRL_REG);

	/* Set both stop bits in the analog output control register */
	me4000_outl(ME4000_AO_CTRL_BIT_IMMEDIATE_STOP | ME4000_AO_CTRL_BIT_STOP,
		    info->me4000_regbase + ME4000_AO_00_CTRL_REG);
	me4000_outl(ME4000_AO_CTRL_BIT_IMMEDIATE_STOP | ME4000_AO_CTRL_BIT_STOP,
		    info->me4000_regbase + ME4000_AO_01_CTRL_REG);
	me4000_outl(ME4000_AO_CTRL_BIT_IMMEDIATE_STOP | ME4000_AO_CTRL_BIT_STOP,
		    info->me4000_regbase + ME4000_AO_02_CTRL_REG);
	me4000_outl(ME4000_AO_CTRL_BIT_IMMEDIATE_STOP | ME4000_AO_CTRL_BIT_STOP,
		    info->me4000_regbase + ME4000_AO_03_CTRL_REG);

	/* 0x8000 to the DACs means an output voltage of 0V */
	me4000_outl(0x8000, info->me4000_regbase + ME4000_AO_00_SINGLE_REG);
	me4000_outl(0x8000, info->me4000_regbase + ME4000_AO_01_SINGLE_REG);
	me4000_outl(0x8000, info->me4000_regbase + ME4000_AO_02_SINGLE_REG);
	me4000_outl(0x8000, info->me4000_regbase + ME4000_AO_03_SINGLE_REG);

	/* Enable interrupts on the PLX */
	me4000_outl(0x43, info->plx_regbase + PLX_INTCSR);

	/* Set the adustment register for AO demux */
	me4000_outl(ME4000_AO_DEMUX_ADJUST_VALUE,
		    info->me4000_regbase + ME4000_AO_DEMUX_ADJUST_REG);

	/* Set digital I/O direction for port 0 to output on isolated versions */
	if (!(me4000_inl(info->me4000_regbase + ME4000_DIO_DIR_REG) & 0x1))
		me4000_outl(0x1, info->me4000_regbase + ME4000_DIO_CTRL_REG);

	return 0;
}

static int me4000_open(struct inode *inode_p, struct file *file_p)
{
	int board, dev, mode;
	int err = 0;
	int i;
	struct list_head *ptr;
	struct me4000_info *board_info = NULL;
	struct me4000_ao_context *ao_context = NULL;
	struct me4000_ai_context *ai_context = NULL;
	struct me4000_dio_context *dio_context = NULL;
	struct me4000_cnt_context *cnt_context = NULL;
	struct me4000_ext_int_context *ext_int_context = NULL;

	CALL_PDEBUG("me4000_open() is executed\n");

	/* Analog output */
	if (MAJOR(inode_p->i_rdev) == me4000_ao_major_driver_no) {
		board = AO_BOARD(inode_p->i_rdev);
		dev = AO_PORT(inode_p->i_rdev);
		mode = AO_MODE(inode_p->i_rdev);

		PDEBUG("me4000_open():board = %d ao = %d mode = %d\n", board,
		       dev, mode);

		/* Search for the board context */
		i = 0;
		list_for_each(ptr, &me4000_board_info_list) {
			if (i == board)
				break;
			i++;
		}
		board_info = list_entry(ptr, struct me4000_info, list);

		if (ptr == &me4000_board_info_list) {
			printk(KERN_ERR
			       "ME4000:me4000_open():Board %d not in device list\n",
			       board);
			return -ENODEV;
		}

		/* Search for the dac context */
		i = 0;
		list_for_each(ptr, &board_info->ao_context_list) {
			if (i == dev)
				break;
			i++;
		}
		ao_context = list_entry(ptr, struct me4000_ao_context, list);

		if (ptr == &board_info->ao_context_list) {
			printk(KERN_ERR
			       "ME4000:me4000_open():Device %d not in device list\n",
			       dev);
			return -ENODEV;
		}

		/* Check if mode is valid */
		if (mode > 2) {
			printk(KERN_ERR
			       "ME4000:me4000_open():Mode is not valid\n");
			return -ENODEV;
		}

		/* Check if mode is valid for this AO */
		if ((mode != ME4000_AO_CONV_MODE_SINGLE)
		    && (dev >= board_info->board_p->ao.fifo_count)) {
			printk(KERN_ERR
			       "ME4000:me4000_open():AO %d only in single mode available\n",
			       dev);
			return -ENODEV;
		}

		/* Check if already opened */
		spin_lock(&ao_context->use_lock);
		if (ao_context->dac_in_use) {
			printk(KERN_ERR
			       "ME4000:me4000_open():AO %d already in use\n",
			       dev);
			spin_unlock(&ao_context->use_lock);
			return -EBUSY;
		}
		ao_context->dac_in_use = 1;
		spin_unlock(&ao_context->use_lock);

		ao_context->mode = mode;

		/* Hold the context in private data */
		file_p->private_data = ao_context;

		/* Set file operations pointer */
		file_p->f_op = me4000_ao_fops_array[mode];

		err = me4000_ao_prepare(ao_context);
		if (err) {
			ao_context->dac_in_use = 0;
			return 1;
		}
	}
	/* Analog input */
	else if (MAJOR(inode_p->i_rdev) == me4000_ai_major_driver_no) {
		board = AI_BOARD(inode_p->i_rdev);
		mode = AI_MODE(inode_p->i_rdev);

		PDEBUG("me4000_open():ai board = %d mode = %d\n", board, mode);

		/* Search for the board context */
		i = 0;
		list_for_each(ptr, &me4000_board_info_list) {
			if (i == board)
				break;
			i++;
		}
		board_info = list_entry(ptr, struct me4000_info, list);

		if (ptr == &me4000_board_info_list) {
			printk(KERN_ERR
			       "ME4000:me4000_open():Board %d not in device list\n",
			       board);
			return -ENODEV;
		}

		ai_context = board_info->ai_context;

		/* Check if mode is valid */
		if (mode > 5) {
			printk(KERN_ERR
			       "ME4000:me4000_open():Mode is not valid\n");
			return -EINVAL;
		}

		/* Check if already opened */
		spin_lock(&ai_context->use_lock);
		if (ai_context->in_use) {
			printk(KERN_ERR
			       "ME4000:me4000_open():AI already in use\n");
			spin_unlock(&ai_context->use_lock);
			return -EBUSY;
		}
		ai_context->in_use = 1;
		spin_unlock(&ai_context->use_lock);

		ai_context->mode = mode;

		/* Hold the context in private data */
		file_p->private_data = ai_context;

		/* Set file operations pointer */
		file_p->f_op = me4000_ai_fops_array[mode];

		/* Prepare analog input */
		me4000_ai_prepare(ai_context);
	}
	/* Digital I/O */
	else if (MAJOR(inode_p->i_rdev) == me4000_dio_major_driver_no) {
		board = DIO_BOARD(inode_p->i_rdev);
		dev = 0;
		mode = 0;

		PDEBUG("me4000_open():board = %d\n", board);

		/* Search for the board context */
		list_for_each_entry(board_info, &me4000_board_info_list, list) {
			if (board_info->board_count == board)
				break;
		}

		if (&board_info->list == &me4000_board_info_list) {
			printk(KERN_ERR
			       "ME4000:me4000_open():Board %d not in device list\n",
			       board);
			return -ENODEV;
		}

		/* Search for the dio context */
		dio_context = board_info->dio_context;

		/* Check if already opened */
		spin_lock(&dio_context->use_lock);
		if (dio_context->in_use) {
			printk(KERN_ERR
			       "ME4000:me4000_open():DIO already in use\n");
			spin_unlock(&dio_context->use_lock);
			return -EBUSY;
		}
		dio_context->in_use = 1;
		spin_unlock(&dio_context->use_lock);

		/* Hold the context in private data */
		file_p->private_data = dio_context;

		/* Set file operations pointer to single functions */
		file_p->f_op = &me4000_dio_fops;

		/* me4000_dio_reset(dio_context); */
	}
	/* Counters */
	else if (MAJOR(inode_p->i_rdev) == me4000_cnt_major_driver_no) {
		board = CNT_BOARD(inode_p->i_rdev);
		dev = 0;
		mode = 0;

		PDEBUG("me4000_open():board = %d\n", board);

		/* Search for the board context */
		list_for_each_entry(board_info, &me4000_board_info_list, list) {
			if (board_info->board_count == board)
				break;
		}

		if (&board_info->list == &me4000_board_info_list) {
			printk(KERN_ERR
			       "ME4000:me4000_open():Board %d not in device list\n",
			       board);
			return -ENODEV;
		}

		/* Get the cnt context */
		cnt_context = board_info->cnt_context;

		/* Check if already opened */
		spin_lock(&cnt_context->use_lock);
		if (cnt_context->in_use) {
			printk(KERN_ERR
			       "ME4000:me4000_open():CNT already in use\n");
			spin_unlock(&cnt_context->use_lock);
			return -EBUSY;
		}
		cnt_context->in_use = 1;
		spin_unlock(&cnt_context->use_lock);

		/* Hold the context in private data */
		file_p->private_data = cnt_context;

		/* Set file operations pointer to single functions */
		file_p->f_op = &me4000_cnt_fops;
	}
	/* External Interrupt */
	else if (MAJOR(inode_p->i_rdev) == me4000_ext_int_major_driver_no) {
		board = EXT_INT_BOARD(inode_p->i_rdev);
		dev = 0;
		mode = 0;

		PDEBUG("me4000_open():board = %d\n", board);

		/* Search for the board context */
		list_for_each_entry(board_info, &me4000_board_info_list, list) {
			if (board_info->board_count == board)
				break;
		}

		if (&board_info->list == &me4000_board_info_list) {
			printk(KERN_ERR
			       "ME4000:me4000_open():Board %d not in device list\n",
			       board);
			return -ENODEV;
		}

		/* Get the external interrupt context */
		ext_int_context = board_info->ext_int_context;

		/* Check if already opened */
		spin_lock(&cnt_context->use_lock);
		if (ext_int_context->in_use) {
			printk(KERN_ERR
			       "ME4000:me4000_open():External interrupt already in use\n");
			spin_unlock(&ext_int_context->use_lock);
			return -EBUSY;
		}
		ext_int_context->in_use = 1;
		spin_unlock(&ext_int_context->use_lock);

		/* Hold the context in private data */
		file_p->private_data = ext_int_context;

		/* Set file operations pointer to single functions */
		file_p->f_op = &me4000_ext_int_fops;

		/* Request the interrupt line */
		err =
		    request_irq(ext_int_context->irq, me4000_ext_int_isr,
				IRQF_DISABLED | IRQF_SHARED, ME4000_NAME,
				ext_int_context);
		if (err) {
			printk(KERN_ERR
			       "ME4000:me4000_open():Can't get interrupt line");
			ext_int_context->in_use = 0;
			return -ENODEV;
		}

		/* Reset the counter */
		me4000_ext_int_disable(ext_int_context);
	} else {
		printk(KERN_ERR "ME4000:me4000_open():Major number unknown\n");
		return -EINVAL;
	}

	return 0;
}

static int me4000_release(struct inode *inode_p, struct file *file_p)
{
	struct me4000_ao_context *ao_context;
	struct me4000_ai_context *ai_context;
	struct me4000_dio_context *dio_context;
	struct me4000_cnt_context *cnt_context;
	struct me4000_ext_int_context *ext_int_context;

	CALL_PDEBUG("me4000_release() is executed\n");

	if (MAJOR(inode_p->i_rdev) == me4000_ao_major_driver_no) {
		ao_context = file_p->private_data;

		/* Mark DAC as unused */
		ao_context->dac_in_use = 0;
	} else if (MAJOR(inode_p->i_rdev) == me4000_ai_major_driver_no) {
		ai_context = file_p->private_data;

		/* Reset the analog input */
		me4000_ai_reset(ai_context);

		/* Free the interrupt and the circular buffer */
		if (ai_context->mode) {
			free_irq(ai_context->irq, ai_context);
			kfree(ai_context->circ_buf.buf);
			ai_context->circ_buf.buf = NULL;
			ai_context->circ_buf.head = 0;
			ai_context->circ_buf.tail = 0;
		}

		/* Mark AI as unused */
		ai_context->in_use = 0;
	} else if (MAJOR(inode_p->i_rdev) == me4000_dio_major_driver_no) {
		dio_context = file_p->private_data;

		/* Mark digital I/O as unused */
		dio_context->in_use = 0;
	} else if (MAJOR(inode_p->i_rdev) == me4000_cnt_major_driver_no) {
		cnt_context = file_p->private_data;

		/* Mark counters as unused */
		cnt_context->in_use = 0;
	} else if (MAJOR(inode_p->i_rdev) == me4000_ext_int_major_driver_no) {
		ext_int_context = file_p->private_data;

		/* Disable the externel interrupt */
		me4000_ext_int_disable(ext_int_context);

		free_irq(ext_int_context->irq, ext_int_context);

		/* Mark as unused */
		ext_int_context->in_use = 0;
	} else {
		printk(KERN_ERR
		       "ME4000:me4000_release():Major number unknown\n");
		return -EINVAL;
	}

	return 0;
}

/*------------------------------- Analog output stuff --------------------------------------*/

static int me4000_ao_prepare(struct me4000_ao_context *ao_context)
{
	unsigned long flags;

	CALL_PDEBUG("me4000_ao_prepare() is executed\n");

	if (ao_context->mode == ME4000_AO_CONV_MODE_CONTINUOUS) {
		/* Only do anything if not already in the correct mode */
		unsigned long mode = me4000_inl(ao_context->ctrl_reg);
		if ((mode & ME4000_AO_CONV_MODE_CONTINUOUS)
		    && (mode & ME4000_AO_CTRL_BIT_ENABLE_FIFO)) {
			return 0;
		}

		/* Stop any conversion */
		me4000_ao_immediate_stop(ao_context);

		/* Set the control register to default state  */
		spin_lock_irqsave(&ao_context->int_lock, flags);
		me4000_outl(ME4000_AO_CONV_MODE_CONTINUOUS |
			    ME4000_AO_CTRL_BIT_ENABLE_FIFO |
			    ME4000_AO_CTRL_BIT_STOP |
			    ME4000_AO_CTRL_BIT_IMMEDIATE_STOP,
			    ao_context->ctrl_reg);
		spin_unlock_irqrestore(&ao_context->int_lock, flags);

		/* Set to fastest sample rate */
		me4000_outl(65, ao_context->timer_reg);
	} else if (ao_context->mode == ME4000_AO_CONV_MODE_WRAPAROUND) {
		/* Only do anything if not already in the correct mode */
		unsigned long mode = me4000_inl(ao_context->ctrl_reg);
		if ((mode & ME4000_AO_CONV_MODE_WRAPAROUND)
		    && (mode & ME4000_AO_CTRL_BIT_ENABLE_FIFO)) {
			return 0;
		}

		/* Stop any conversion */
		me4000_ao_immediate_stop(ao_context);

		/* Set the control register to default state  */
		spin_lock_irqsave(&ao_context->int_lock, flags);
		me4000_outl(ME4000_AO_CONV_MODE_WRAPAROUND |
			    ME4000_AO_CTRL_BIT_ENABLE_FIFO |
			    ME4000_AO_CTRL_BIT_STOP |
			    ME4000_AO_CTRL_BIT_IMMEDIATE_STOP,
			    ao_context->ctrl_reg);
		spin_unlock_irqrestore(&ao_context->int_lock, flags);

		/* Set to fastest sample rate */
		me4000_outl(65, ao_context->timer_reg);
	} else if (ao_context->mode == ME4000_AO_CONV_MODE_SINGLE) {
		/* Only do anything if not already in the correct mode */
		unsigned long mode = me4000_inl(ao_context->ctrl_reg);
		if (!
		    (mode &
		     (ME4000_AO_CONV_MODE_WRAPAROUND |
		      ME4000_AO_CONV_MODE_CONTINUOUS))) {
			return 0;
		}

		/* Stop any conversion */
		me4000_ao_immediate_stop(ao_context);

		/* Clear the control register */
		spin_lock_irqsave(&ao_context->int_lock, flags);
		me4000_outl(0x0, ao_context->ctrl_reg);
		spin_unlock_irqrestore(&ao_context->int_lock, flags);

		/* Set voltage to 0V */
		me4000_outl(0x8000, ao_context->single_reg);
	} else {
		printk(KERN_ERR
		       "ME4000:me4000_ao_prepare():Invalid mode specified\n");
		return -EINVAL;
	}

	return 0;
}

static int me4000_ao_reset(struct me4000_ao_context *ao_context)
{
	u32 tmp;
	wait_queue_head_t queue;
	unsigned long flags;

	CALL_PDEBUG("me4000_ao_reset() is executed\n");

	init_waitqueue_head(&queue);

	if (ao_context->mode == ME4000_AO_CONV_MODE_WRAPAROUND) {
		/*
		 * First stop conversion of the DAC before reconfigure.
		 * This is essantial, cause of the state machine.
		 * If not stopped before configuring mode, it could
		 * walk in a undefined state.
		 */
		tmp = me4000_inl(ao_context->ctrl_reg);
		tmp |= ME4000_AO_CTRL_BIT_IMMEDIATE_STOP;
		me4000_outl(tmp, ao_context->ctrl_reg);

		wait_event_timeout(queue,
			(inl(ao_context->status_reg) &
				ME4000_AO_STATUS_BIT_FSM) == 0,
			1);

		/* Set to transparent mode */
		me4000_ao_simultaneous_disable(ao_context);

		/* Set to single mode in order to set default voltage */
		me4000_outl(0x0, ao_context->ctrl_reg);

		/* Set voltage to 0V */
		me4000_outl(0x8000, ao_context->single_reg);

		/* Set to fastest sample rate */
		me4000_outl(65, ao_context->timer_reg);

		/* Set the original mode and enable FIFO */
		me4000_outl(ME4000_AO_CONV_MODE_WRAPAROUND |
			    ME4000_AO_CTRL_BIT_ENABLE_FIFO |
			    ME4000_AO_CTRL_BIT_STOP |
			    ME4000_AO_CTRL_BIT_IMMEDIATE_STOP,
			    ao_context->ctrl_reg);
	} else if (ao_context->mode == ME4000_AO_CONV_MODE_CONTINUOUS) {
		/*
		 * First stop conversion of the DAC before reconfigure.
		 * This is essantial, cause of the state machine.
		 * If not stopped before configuring mode, it could
		 * walk in a undefined state.
		 */
		spin_lock_irqsave(&ao_context->int_lock, flags);
		tmp = me4000_inl(ao_context->ctrl_reg);
		tmp |= ME4000_AO_CTRL_BIT_STOP;
		me4000_outl(tmp, ao_context->ctrl_reg);
		spin_unlock_irqrestore(&ao_context->int_lock, flags);

		wait_event_timeout(queue,
			(inl(ao_context->status_reg) &
				ME4000_AO_STATUS_BIT_FSM) == 0,
			1);

		/* Clear the circular buffer */
		ao_context->circ_buf.head = 0;
		ao_context->circ_buf.tail = 0;

		/* Set to transparent mode */
		me4000_ao_simultaneous_disable(ao_context);

		/* Set to single mode in order to set default voltage */
		spin_lock_irqsave(&ao_context->int_lock, flags);
		tmp = me4000_inl(ao_context->ctrl_reg);
		me4000_outl(0x0, ao_context->ctrl_reg);

		/* Set voltage to 0V */
		me4000_outl(0x8000, ao_context->single_reg);

		/* Set to fastest sample rate */
		me4000_outl(65, ao_context->timer_reg);

		/* Set the original mode and enable FIFO */
		me4000_outl(ME4000_AO_CONV_MODE_CONTINUOUS |
			    ME4000_AO_CTRL_BIT_ENABLE_FIFO |
			    ME4000_AO_CTRL_BIT_STOP |
			    ME4000_AO_CTRL_BIT_IMMEDIATE_STOP,
			    ao_context->ctrl_reg);
		spin_unlock_irqrestore(&ao_context->int_lock, flags);
	} else {
		/* Set to transparent mode */
		me4000_ao_simultaneous_disable(ao_context);

		/* Set voltage to 0V */
		me4000_outl(0x8000, ao_context->single_reg);
	}

	return 0;
}

static ssize_t me4000_ao_write_sing(struct file *filep, const char *buff,
				    size_t cnt, loff_t *offp)
{
	struct me4000_ao_context *ao_context = filep->private_data;
	u32 value;
	const u16 *buffer = (const u16 *)buff;

	CALL_PDEBUG("me4000_ao_write_sing() is executed\n");

	if (cnt != 2) {
		printk(KERN_ERR
		       "%s:Write count is not 2\n", __func__);
		return -EINVAL;
	}

	if (get_user(value, buffer)) {
		printk(KERN_ERR
		       "%s:Cannot copy data from user\n", __func__);
		return -EFAULT;
	}

	me4000_outl(value, ao_context->single_reg);

	return 2;
}

static ssize_t me4000_ao_write_wrap(struct file *filep, const char *buff,
				    size_t cnt, loff_t *offp)
{
	struct me4000_ao_context *ao_context = filep->private_data;
	size_t i;
	u32 value;
	u32 tmp;
	const u16 *buffer = (const u16 *)buff;
	size_t count = cnt / 2;

	CALL_PDEBUG("me4000_ao_write_wrap() is executed\n");

	/* Check if a conversion is already running */
	if (inl(ao_context->status_reg) & ME4000_AO_STATUS_BIT_FSM) {
		printk(KERN_ERR
		       "%s:There is already a conversion running\n", __func__);
		return -EBUSY;
	}

	if (count > ME4000_AO_FIFO_COUNT) {
		printk(KERN_ERR
		       "%s:Can't load more than %d values\n", __func__,
		       ME4000_AO_FIFO_COUNT);
		return -ENOSPC;
	}

	/* Reset the FIFO */
	tmp = inl(ao_context->ctrl_reg);
	tmp &= ~ME4000_AO_CTRL_BIT_ENABLE_FIFO;
	outl(tmp, ao_context->ctrl_reg);
	tmp |= ME4000_AO_CTRL_BIT_ENABLE_FIFO;
	outl(tmp, ao_context->ctrl_reg);

	for (i = 0; i < count; i++) {
		if (get_user(value, buffer + i)) {
			printk(KERN_ERR
			       "%s:Cannot copy data from user\n", __func__);
			return -EFAULT;
		}
		if (((ao_context->fifo_reg & 0xFF) == ME4000_AO_01_FIFO_REG)
		    || ((ao_context->fifo_reg & 0xFF) == ME4000_AO_03_FIFO_REG))
			value = value << 16;
		outl(value, ao_context->fifo_reg);
	}
	CALL_PDEBUG("me4000_ao_write_wrap() is leaved with %d\n", i * 2);

	return i * 2;
}

static ssize_t me4000_ao_write_cont(struct file *filep, const char *buff,
				    size_t cnt, loff_t *offp)
{
	struct me4000_ao_context *ao_context = filep->private_data;
	const u16 *buffer = (const u16 *)buff;
	size_t count = cnt / 2;
	unsigned long flags;
	u32 tmp;
	int c = 0;
	int k = 0;
	int ret = 0;
	u16 svalue;
	u32 lvalue;
	int i;
	wait_queue_head_t queue;

	CALL_PDEBUG("me4000_ao_write_cont() is executed\n");

	init_waitqueue_head(&queue);

	/* Check count */
	if (count <= 0) {
		PDEBUG("me4000_ao_write_cont():Count is 0\n");
		return 0;
	}

	if (filep->f_flags & O_APPEND) {
		PDEBUG("me4000_ao_write_cont():Append data to data stream\n");
		while (count > 0) {
			if (filep->f_flags & O_NONBLOCK) {
				if (ao_context->pipe_flag) {
					printk(KERN_ERR
					       "ME4000:me4000_ao_write_cont():Broken pipe in nonblocking write\n");
					return -EPIPE;
				}
				c = me4000_space_to_end(ao_context->circ_buf,
							ME4000_AO_BUFFER_COUNT);
				if (!c) {
					PDEBUG
					    ("me4000_ao_write_cont():Returning from nonblocking write\n");
					break;
				}
			} else {
				wait_event_interruptible(ao_context->wait_queue,
							 (c =
							  me4000_space_to_end
							  (ao_context->circ_buf,
							   ME4000_AO_BUFFER_COUNT)));
				if (ao_context->pipe_flag) {
					printk(KERN_ERR
					       "me4000_ao_write_cont():Broken pipe in blocking write\n");
					return -EPIPE;
				}
				if (signal_pending(current)) {
					printk(KERN_ERR
					       "me4000_ao_write_cont():Wait for free buffer interrupted from signal\n");
					return -EINTR;
				}
			}

			PDEBUG("me4000_ao_write_cont():Space to end = %d\n", c);

			/* Only able to write size of free buffer or size of count */
			if (count < c)
				c = count;

			k = 2 * c;
			k -= copy_from_user(ao_context->circ_buf.buf +
					    ao_context->circ_buf.head, buffer,
					    k);
			c = k / 2;
			PDEBUG
			    ("me4000_ao_write_cont():Copy %d values from user space\n",
			     c);

			if (!c)
				return -EFAULT;

			ao_context->circ_buf.head =
			    (ao_context->circ_buf.head +
			     c) & (ME4000_AO_BUFFER_COUNT - 1);
			buffer += c;
			count -= c;
			ret += c;

			/* Values are now available so enable interrupts */
			spin_lock_irqsave(&ao_context->int_lock, flags);
			if (me4000_buf_count
			    (ao_context->circ_buf, ME4000_AO_BUFFER_COUNT)) {
				tmp = me4000_inl(ao_context->ctrl_reg);
				tmp |= ME4000_AO_CTRL_BIT_ENABLE_IRQ;
				me4000_outl(tmp, ao_context->ctrl_reg);
			}
			spin_unlock_irqrestore(&ao_context->int_lock, flags);
		}

		/* Wait until the state machine is stopped if O_SYNC is set */
		if (filep->f_flags & O_SYNC) {
			while (inl(ao_context->status_reg) &
			       ME4000_AO_STATUS_BIT_FSM) {
				interruptible_sleep_on_timeout(&queue, 1);
				if (ao_context->pipe_flag) {
					PDEBUG
					    ("me4000_ao_write_cont():Broken pipe detected after sync\n");
					return -EPIPE;
				}
				if (signal_pending(current)) {
					printk(KERN_ERR
					       "me4000_ao_write_cont():Wait on state machine after sync interrupted\n");
					return -EINTR;
				}
			}
		}
	} else {
		PDEBUG("me4000_ao_write_cont():Preload DAC FIFO\n");
		if ((me4000_inl(ao_context->status_reg) &
		     ME4000_AO_STATUS_BIT_FSM)) {
			printk(KERN_ERR
			       "me4000_ao_write_cont():Can't Preload DAC FIFO while conversion is running\n");
			return -EBUSY;
		}

		/* Clear the FIFO */
		spin_lock_irqsave(&ao_context->int_lock, flags);
		tmp = me4000_inl(ao_context->ctrl_reg);
		tmp &=
		    ~(ME4000_AO_CTRL_BIT_ENABLE_FIFO |
		      ME4000_AO_CTRL_BIT_ENABLE_IRQ);
		me4000_outl(tmp, ao_context->ctrl_reg);
		tmp |= ME4000_AO_CTRL_BIT_ENABLE_FIFO;
		me4000_outl(tmp, ao_context->ctrl_reg);
		spin_unlock_irqrestore(&ao_context->int_lock, flags);

		/* Clear the circular buffer */
		ao_context->circ_buf.head = 0;
		ao_context->circ_buf.tail = 0;

		/* Reset the broken pipe flag */
		ao_context->pipe_flag = 0;

		/* Only able to write size of fifo or count */
		c = ME4000_AO_FIFO_COUNT;
		if (count < c)
			c = count;

		PDEBUG
		    ("me4000_ao_write_cont():Write %d values to DAC on 0x%lX\n",
		     c, ao_context->fifo_reg);

		/* Write values to the fifo */
		for (i = 0; i < c; i++) {
			if (get_user(svalue, buffer))
				return -EFAULT;

			if (((ao_context->fifo_reg & 0xFF) ==
			     ME4000_AO_01_FIFO_REG)
			    || ((ao_context->fifo_reg & 0xFF) ==
				ME4000_AO_03_FIFO_REG)) {
				lvalue = ((u32) svalue) << 16;
			} else
				lvalue = (u32) svalue;

			outl(lvalue, ao_context->fifo_reg);
			buffer++;
		}
		count -= c;
		ret += c;

		while (1) {
			/* Get free buffer */
			c = me4000_space_to_end(ao_context->circ_buf,
						ME4000_AO_BUFFER_COUNT);

			if (c == 0)
				return 2 * ret;

			/* Only able to write size of free buffer or size of count */
			if (count < c)
				c = count;

			/* If count = 0 return to user */
			if (c <= 0) {
				PDEBUG
				    ("me4000_ao_write_cont():Count reached 0\n");
				break;
			}

			k = 2 * c;
			k -= copy_from_user(ao_context->circ_buf.buf +
					    ao_context->circ_buf.head, buffer,
					    k);
			c = k / 2;
			PDEBUG
			    ("me4000_ao_write_cont():Wrote %d values to buffer\n",
			     c);

			if (!c)
				return -EFAULT;

			ao_context->circ_buf.head =
			    (ao_context->circ_buf.head +
			     c) & (ME4000_AO_BUFFER_COUNT - 1);
			buffer += c;
			count -= c;
			ret += c;

			/* If values in the buffer are available so enable interrupts */
			spin_lock_irqsave(&ao_context->int_lock, flags);
			if (me4000_buf_count
			    (ao_context->circ_buf, ME4000_AO_BUFFER_COUNT)) {
				PDEBUG
				    ("me4000_ao_write_cont():Enable Interrupts\n");
				tmp = me4000_inl(ao_context->ctrl_reg);
				tmp |= ME4000_AO_CTRL_BIT_ENABLE_IRQ;
				me4000_outl(tmp, ao_context->ctrl_reg);
			}
			spin_unlock_irqrestore(&ao_context->int_lock, flags);
		}
	}

	if (filep->f_flags & O_NONBLOCK)
		return (ret == 0) ? -EAGAIN : 2 * ret;

	return 2 * ret;
}

static unsigned int me4000_ao_poll_cont(struct file *file_p, poll_table *wait)
{
	struct me4000_ao_context *ao_context;
	unsigned long mask = 0;

	CALL_PDEBUG("me4000_ao_poll_cont() is executed\n");

	ao_context = file_p->private_data;

	poll_wait(file_p, &ao_context->wait_queue, wait);

	/* Get free buffer */
	if (me4000_space_to_end(ao_context->circ_buf, ME4000_AO_BUFFER_COUNT))
		mask |= POLLOUT | POLLWRNORM;

	CALL_PDEBUG("me4000_ao_poll_cont():Return mask %lX\n", mask);

	return mask;
}

static int me4000_ao_fsync_cont(struct file *file_p, struct dentry *dentry_p,
				int datasync)
{
	struct me4000_ao_context *ao_context;
	wait_queue_head_t queue;

	CALL_PDEBUG("me4000_ao_fsync_cont() is executed\n");

	ao_context = file_p->private_data;
	init_waitqueue_head(&queue);

	while (inl(ao_context->status_reg) & ME4000_AO_STATUS_BIT_FSM) {
		interruptible_sleep_on_timeout(&queue, 1);
			wait_event_interruptible_timeout(queue,
			!(inl(ao_context->status_reg) & ME4000_AO_STATUS_BIT_FSM),
			1);
		if (ao_context->pipe_flag) {
			printk(KERN_ERR
			       "%s:Broken pipe detected\n", __func__);
			return -EPIPE;
		}

		if (signal_pending(current)) {
			printk(KERN_ERR
			       "%s:Wait on state machine interrupted\n",
			       __func__);
			return -EINTR;
		}
	}

	return 0;
}

static int me4000_ao_ioctl_sing(struct inode *inode_p, struct file *file_p,
				unsigned int service, unsigned long arg)
{
	struct me4000_ao_context *ao_context;

	CALL_PDEBUG("me4000_ao_ioctl_sing() is executed\n");

	ao_context = file_p->private_data;

	if (_IOC_TYPE(service) != ME4000_MAGIC) {
		return -ENOTTY;
		PDEBUG("me4000_ao_ioctl_sing():Wrong magic number\n");
	}

	switch (service) {
	case ME4000_AO_EX_TRIG_SETUP:
		return me4000_ao_ex_trig_set_edge((int *)arg, ao_context);
	case ME4000_AO_EX_TRIG_ENABLE:
		return me4000_ao_ex_trig_enable(ao_context);
	case ME4000_AO_EX_TRIG_DISABLE:
		return me4000_ao_ex_trig_disable(ao_context);
	case ME4000_AO_PRELOAD:
		return me4000_ao_preload(ao_context);
	case ME4000_AO_PRELOAD_UPDATE:
		return me4000_ao_preload_update(ao_context);
	case ME4000_GET_USER_INFO:
		return me4000_get_user_info((struct me4000_user_info *)arg,
					    ao_context->board_info);
	case ME4000_AO_SIMULTANEOUS_EX_TRIG:
		return me4000_ao_simultaneous_ex_trig(ao_context);
	case ME4000_AO_SIMULTANEOUS_SW:
		return me4000_ao_simultaneous_sw(ao_context);
	case ME4000_AO_SIMULTANEOUS_DISABLE:
		return me4000_ao_simultaneous_disable(ao_context);
	case ME4000_AO_SIMULTANEOUS_UPDATE:
		return
		    me4000_ao_simultaneous_update(
				(struct me4000_ao_channel_list *)arg,
				ao_context);
	case ME4000_AO_EX_TRIG_TIMEOUT:
		return me4000_ao_ex_trig_timeout((unsigned long *)arg,
						 ao_context);
	case ME4000_AO_DISABLE_DO:
		return me4000_ao_disable_do(ao_context);
	default:
		printk(KERN_ERR
		       "me4000_ao_ioctl_sing():Service number invalid\n");
		return -ENOTTY;
	}

	return 0;
}

static int me4000_ao_ioctl_wrap(struct inode *inode_p, struct file *file_p,
				unsigned int service, unsigned long arg)
{
	struct me4000_ao_context *ao_context;

	CALL_PDEBUG("me4000_ao_ioctl_wrap() is executed\n");

	ao_context = file_p->private_data;

	if (_IOC_TYPE(service) != ME4000_MAGIC) {
		return -ENOTTY;
		PDEBUG("me4000_ao_ioctl_wrap():Wrong magic number\n");
	}

	switch (service) {
	case ME4000_AO_START:
		return me4000_ao_start((unsigned long *)arg, ao_context);
	case ME4000_AO_STOP:
		return me4000_ao_stop(ao_context);
	case ME4000_AO_IMMEDIATE_STOP:
		return me4000_ao_immediate_stop(ao_context);
	case ME4000_AO_RESET:
		return me4000_ao_reset(ao_context);
	case ME4000_AO_TIMER_SET_DIVISOR:
		return me4000_ao_timer_set_divisor((u32 *) arg, ao_context);
	case ME4000_AO_EX_TRIG_SETUP:
		return me4000_ao_ex_trig_set_edge((int *)arg, ao_context);
	case ME4000_AO_EX_TRIG_ENABLE:
		return me4000_ao_ex_trig_enable(ao_context);
	case ME4000_AO_EX_TRIG_DISABLE:
		return me4000_ao_ex_trig_disable(ao_context);
	case ME4000_GET_USER_INFO:
		return me4000_get_user_info((struct me4000_user_info *)arg,
					    ao_context->board_info);
	case ME4000_AO_FSM_STATE:
		return me4000_ao_fsm_state((int *)arg, ao_context);
	case ME4000_AO_ENABLE_DO:
		return me4000_ao_enable_do(ao_context);
	case ME4000_AO_DISABLE_DO:
		return me4000_ao_disable_do(ao_context);
	case ME4000_AO_SYNCHRONOUS_EX_TRIG:
		return me4000_ao_synchronous_ex_trig(ao_context);
	case ME4000_AO_SYNCHRONOUS_SW:
		return me4000_ao_synchronous_sw(ao_context);
	case ME4000_AO_SYNCHRONOUS_DISABLE:
		return me4000_ao_synchronous_disable(ao_context);
	default:
		return -ENOTTY;
	}
	return 0;
}

static int me4000_ao_ioctl_cont(struct inode *inode_p, struct file *file_p,
				unsigned int service, unsigned long arg)
{
	struct me4000_ao_context *ao_context;

	CALL_PDEBUG("me4000_ao_ioctl_cont() is executed\n");

	ao_context = file_p->private_data;

	if (_IOC_TYPE(service) != ME4000_MAGIC) {
		return -ENOTTY;
		PDEBUG("me4000_ao_ioctl_cont():Wrong magic number\n");
	}

	switch (service) {
	case ME4000_AO_START:
		return me4000_ao_start((unsigned long *)arg, ao_context);
	case ME4000_AO_STOP:
		return me4000_ao_stop(ao_context);
	case ME4000_AO_IMMEDIATE_STOP:
		return me4000_ao_immediate_stop(ao_context);
	case ME4000_AO_RESET:
		return me4000_ao_reset(ao_context);
	case ME4000_AO_TIMER_SET_DIVISOR:
		return me4000_ao_timer_set_divisor((u32 *) arg, ao_context);
	case ME4000_AO_EX_TRIG_SETUP:
		return me4000_ao_ex_trig_set_edge((int *)arg, ao_context);
	case ME4000_AO_EX_TRIG_ENABLE:
		return me4000_ao_ex_trig_enable(ao_context);
	case ME4000_AO_EX_TRIG_DISABLE:
		return me4000_ao_ex_trig_disable(ao_context);
	case ME4000_AO_ENABLE_DO:
		return me4000_ao_enable_do(ao_context);
	case ME4000_AO_DISABLE_DO:
		return me4000_ao_disable_do(ao_context);
	case ME4000_AO_FSM_STATE:
		return me4000_ao_fsm_state((int *)arg, ao_context);
	case ME4000_GET_USER_INFO:
		return me4000_get_user_info((struct me4000_user_info *)arg,
					    ao_context->board_info);
	case ME4000_AO_SYNCHRONOUS_EX_TRIG:
		return me4000_ao_synchronous_ex_trig(ao_context);
	case ME4000_AO_SYNCHRONOUS_SW:
		return me4000_ao_synchronous_sw(ao_context);
	case ME4000_AO_SYNCHRONOUS_DISABLE:
		return me4000_ao_synchronous_disable(ao_context);
	case ME4000_AO_GET_FREE_BUFFER:
		return me4000_ao_get_free_buffer((unsigned long *)arg,
						 ao_context);
	default:
		return -ENOTTY;
	}
	return 0;
}

static int me4000_ao_start(unsigned long *arg,
			   struct me4000_ao_context *ao_context)
{
	u32 tmp;
	wait_queue_head_t queue;
	unsigned long ref;
	unsigned long timeout;
	unsigned long flags;

	CALL_PDEBUG("me4000_ao_start() is executed\n");

	if (get_user(timeout, arg)) {
		printk(KERN_ERR
		       "me4000_ao_start():Cannot copy data from user\n");
		return -EFAULT;
	}

	init_waitqueue_head(&queue);

	spin_lock_irqsave(&ao_context->int_lock, flags);
	tmp = inl(ao_context->ctrl_reg);
	tmp &= ~(ME4000_AO_CTRL_BIT_STOP | ME4000_AO_CTRL_BIT_IMMEDIATE_STOP);
	me4000_outl(tmp, ao_context->ctrl_reg);
	spin_unlock_irqrestore(&ao_context->int_lock, flags);

	if ((tmp & ME4000_AO_CTRL_BIT_ENABLE_EX_TRIG)) {
		if (timeout) {
			ref = jiffies;
			while (!
			       (inl(ao_context->status_reg) &
				ME4000_AO_STATUS_BIT_FSM)) {
				interruptible_sleep_on_timeout(&queue, 1);
				if (signal_pending(current)) {
					printk(KERN_ERR
					       "ME4000:me4000_ao_start():Wait on start of state machine interrupted\n");
					return -EINTR;
				}
				/* kernel 2.6 has different definitions for HZ
				 * in user and kernel space */
				if ((jiffies - ref) > (timeout * HZ / USER_HZ)) {
					printk(KERN_ERR
					       "ME4000:me4000_ao_start():Timeout reached\n");
					return -EIO;
				}
			}
		}
	} else {
		me4000_outl(0x8000, ao_context->single_reg);
	}

	return 0;
}

static int me4000_ao_stop(struct me4000_ao_context *ao_context)
{
	u32 tmp;
	wait_queue_head_t queue;
	unsigned long flags;

	init_waitqueue_head(&queue);

	CALL_PDEBUG("me4000_ao_stop() is executed\n");

	/* Set the stop bit */
	spin_lock_irqsave(&ao_context->int_lock, flags);
	tmp = inl(ao_context->ctrl_reg);
	tmp |= ME4000_AO_CTRL_BIT_STOP;
	me4000_outl(tmp, ao_context->ctrl_reg);
	spin_unlock_irqrestore(&ao_context->int_lock, flags);

	while (inl(ao_context->status_reg) & ME4000_AO_STATUS_BIT_FSM) {
		interruptible_sleep_on_timeout(&queue, 1);
		if (signal_pending(current)) {
			printk(KERN_ERR
			       "me4000_ao_stop():Wait on state machine after stop interrupted\n");
			return -EINTR;
		}
	}

	/* Clear the stop bit */
	/* tmp &= ~ME4000_AO_CTRL_BIT_STOP; */
	/* me4000_outl(tmp, ao_context->ctrl_reg); */

	return 0;
}

static int me4000_ao_immediate_stop(struct me4000_ao_context *ao_context)
{
	u32 tmp;
	wait_queue_head_t queue;
	unsigned long flags;

	init_waitqueue_head(&queue);

	CALL_PDEBUG("me4000_ao_immediate_stop() is executed\n");

	spin_lock_irqsave(&ao_context->int_lock, flags);
	tmp = inl(ao_context->ctrl_reg);
	tmp |= ME4000_AO_CTRL_BIT_STOP | ME4000_AO_CTRL_BIT_IMMEDIATE_STOP;
	me4000_outl(tmp, ao_context->ctrl_reg);
	spin_unlock_irqrestore(&ao_context->int_lock, flags);

	while (inl(ao_context->status_reg) & ME4000_AO_STATUS_BIT_FSM) {
		interruptible_sleep_on_timeout(&queue, 1);
		if (signal_pending(current)) {
			printk(KERN_ERR
			       "me4000_ao_immediate_stop():Wait on state machine after stop interrupted\n");
			return -EINTR;
		}
	}

	/* Clear the stop bits */
	/* tmp &= ~(ME4000_AO_CTRL_BIT_STOP | ME4000_AO_CTRL_BIT_IMMEDIATE_STOP); */
	/* me4000_outl(tmp, ao_context->ctrl_reg); */

	return 0;
}

static int me4000_ao_timer_set_divisor(u32 *arg,
				       struct me4000_ao_context *ao_context)
{
	u32 divisor;
	u32 tmp;

	CALL_PDEBUG("me4000_ao_timer set_divisor() is executed\n");

	if (get_user(divisor, arg))
		return -EFAULT;

	/* Check if the state machine is stopped */
	tmp = me4000_inl(ao_context->status_reg);
	if (tmp & ME4000_AO_STATUS_BIT_FSM) {
		printk(KERN_ERR
		       "me4000_ao_timer_set_divisor():Can't set timer while DAC is running\n");
		return -EBUSY;
	}

	PDEBUG("me4000_ao_timer set_divisor():Divisor from user = %d\n",
	       divisor);

	/* Check if the divisor is right. ME4000_AO_MIN_TICKS is the lowest */
	if (divisor < ME4000_AO_MIN_TICKS) {
		printk(KERN_ERR
		       "ME4000:me4000_ao_timer set_divisor():Divisor to low\n");
		return -EINVAL;
	}

	/* Fix bug in Firmware */
	divisor -= 2;

	PDEBUG("me4000_ao_timer set_divisor():Divisor to HW = %d\n", divisor);

	/* Write the divisor */
	me4000_outl(divisor, ao_context->timer_reg);

	return 0;
}

static int me4000_ao_ex_trig_set_edge(int *arg,
				      struct me4000_ao_context *ao_context)
{
	int mode;
	u32 tmp;
	unsigned long flags;

	CALL_PDEBUG("me4000_ao_ex_trig_set_edge() is executed\n");

	if (get_user(mode, arg))
		return -EFAULT;

	/* Check if the state machine is stopped */
	tmp = me4000_inl(ao_context->status_reg);
	if (tmp & ME4000_AO_STATUS_BIT_FSM) {
		printk(KERN_ERR
		       "me4000_ao_ex_trig_set_edge():Can't set trigger while DAC is running\n");
		return -EBUSY;
	}

	if (mode == ME4000_AO_TRIGGER_EXT_EDGE_RISING) {
		spin_lock_irqsave(&ao_context->int_lock, flags);
		tmp = me4000_inl(ao_context->ctrl_reg);
		tmp &=
		    ~(ME4000_AO_CTRL_BIT_EX_TRIG_EDGE |
		      ME4000_AO_CTRL_BIT_EX_TRIG_BOTH);
		me4000_outl(tmp, ao_context->ctrl_reg);
		spin_unlock_irqrestore(&ao_context->int_lock, flags);
	} else if (mode == ME4000_AO_TRIGGER_EXT_EDGE_FALLING) {
		spin_lock_irqsave(&ao_context->int_lock, flags);
		tmp = me4000_inl(ao_context->ctrl_reg);
		tmp &= ~ME4000_AO_CTRL_BIT_EX_TRIG_BOTH;
		tmp |= ME4000_AO_CTRL_BIT_EX_TRIG_EDGE;
		me4000_outl(tmp, ao_context->ctrl_reg);
		spin_unlock_irqrestore(&ao_context->int_lock, flags);
	} else if (mode == ME4000_AO_TRIGGER_EXT_EDGE_BOTH) {
		spin_lock_irqsave(&ao_context->int_lock, flags);
		tmp = me4000_inl(ao_context->ctrl_reg);
		tmp |=
		    ME4000_AO_CTRL_BIT_EX_TRIG_EDGE |
		    ME4000_AO_CTRL_BIT_EX_TRIG_BOTH;
		me4000_outl(tmp, ao_context->ctrl_reg);
		spin_unlock_irqrestore(&ao_context->int_lock, flags);
	} else {
		printk(KERN_ERR
		       "me4000_ao_ex_trig_set_edge():Invalid trigger mode\n");
		return -EINVAL;
	}

	return 0;
}

static int me4000_ao_ex_trig_enable(struct me4000_ao_context *ao_context)
{
	u32 tmp;
	unsigned long flags;

	CALL_PDEBUG("me4000_ao_ex_trig_enable() is executed\n");

	/* Check if the state machine is stopped */
	tmp = me4000_inl(ao_context->status_reg);
	if (tmp & ME4000_AO_STATUS_BIT_FSM) {
		printk(KERN_ERR
		       "me4000_ao_ex_trig_enable():Can't enable trigger while DAC is running\n");
		return -EBUSY;
	}

	spin_lock_irqsave(&ao_context->int_lock, flags);
	tmp = me4000_inl(ao_context->ctrl_reg);
	tmp |= ME4000_AO_CTRL_BIT_ENABLE_EX_TRIG;
	me4000_outl(tmp, ao_context->ctrl_reg);
	spin_unlock_irqrestore(&ao_context->int_lock, flags);

	return 0;
}

static int me4000_ao_ex_trig_disable(struct me4000_ao_context *ao_context)
{
	u32 tmp;
	unsigned long flags;

	CALL_PDEBUG("me4000_ao_ex_trig_disable() is executed\n");

	/* Check if the state machine is stopped */
	tmp = me4000_inl(ao_context->status_reg);
	if (tmp & ME4000_AO_STATUS_BIT_FSM) {
		printk(KERN_ERR
		       "me4000_ao_ex_trig_disable():Can't disable trigger while DAC is running\n");
		return -EBUSY;
	}

	spin_lock_irqsave(&ao_context->int_lock, flags);
	tmp = me4000_inl(ao_context->ctrl_reg);
	tmp &= ~ME4000_AO_CTRL_BIT_ENABLE_EX_TRIG;
	me4000_outl(tmp, ao_context->ctrl_reg);
	spin_unlock_irqrestore(&ao_context->int_lock, flags);

	return 0;
}

static int me4000_ao_simultaneous_disable(struct me4000_ao_context *ao_context)
{
	u32 tmp;

	CALL_PDEBUG("me4000_ao_simultaneous_disable() is executed\n");

	/* Check if the state machine is stopped */
	/* Be careful here because this function is called from
	   me4000_ao_synchronous disable */
	tmp = me4000_inl(ao_context->status_reg);
	if (tmp & ME4000_AO_STATUS_BIT_FSM) {
		printk(KERN_ERR
		       "me4000_ao_simultaneous_disable():Can't disable while DAC is running\n");
		return -EBUSY;
	}

	spin_lock(&ao_context->board_info->preload_lock);
	tmp = me4000_inl(ao_context->preload_reg);
	/* Disable preload bit */
	tmp &= ~(0x1 << ao_context->index);
	/* Disable hw simultaneous bit */
	tmp &= ~(0x1 << (ao_context->index + 16));
	me4000_outl(tmp, ao_context->preload_reg);
	spin_unlock(&ao_context->board_info->preload_lock);

	return 0;
}

static int me4000_ao_simultaneous_ex_trig(struct me4000_ao_context *ao_context)
{
	u32 tmp;

	CALL_PDEBUG("me4000_ao_simultaneous_ex_trig() is executed\n");

	spin_lock(&ao_context->board_info->preload_lock);
	tmp = me4000_inl(ao_context->preload_reg);
	/* Enable preload bit */
	tmp |= (0x1 << ao_context->index);
	/* Enable hw simulatenous bit */
	tmp |= (0x1 << (ao_context->index + 16));
	me4000_outl(tmp, ao_context->preload_reg);
	spin_unlock(&ao_context->board_info->preload_lock);

	return 0;
}

static int me4000_ao_simultaneous_sw(struct me4000_ao_context *ao_context)
{
	u32 tmp;

	CALL_PDEBUG("me4000_ao_simultaneous_sw() is executed\n");

	spin_lock(&ao_context->board_info->preload_lock);
	tmp = me4000_inl(ao_context->preload_reg);
	/* Enable preload bit */
	tmp |= (0x1 << ao_context->index);
	/* Enable hw simulatenous bit */
	tmp &= ~(0x1 << (ao_context->index + 16));
	me4000_outl(tmp, ao_context->preload_reg);
	spin_unlock(&ao_context->board_info->preload_lock);

	return 0;
}

static int me4000_ao_preload(struct me4000_ao_context *ao_context)
{
	CALL_PDEBUG("me4000_ao_preload() is executed\n");
	return me4000_ao_simultaneous_sw(ao_context);
}

static int me4000_ao_preload_update(struct me4000_ao_context *ao_context)
{
	u32 tmp;
	u32 ctrl;
	struct list_head *entry;

	CALL_PDEBUG("me4000_ao_preload_update() is executed\n");

	spin_lock(&ao_context->board_info->preload_lock);
	tmp = me4000_inl(ao_context->preload_reg);
	list_for_each(entry, &ao_context->board_info->ao_context_list) {
		/* The channels we update must be in the following state :
		   - Mode A
		   - Hardware trigger is disabled
		   - Corresponding simultaneous bit is reset
		 */
		ctrl = me4000_inl(ao_context->ctrl_reg);
		if (!
		    (ctrl &
		     (ME4000_AO_CTRL_BIT_MODE_0 | ME4000_AO_CTRL_BIT_MODE_1 |
		      ME4000_AO_CTRL_BIT_ENABLE_EX_TRIG))) {
			if (!
			    (tmp &
			     (0x1 <<
			      (((struct me4000_ao_context *)entry)->index
								      + 16)))) {
				tmp &=
				    ~(0x1 <<
				      (((struct me4000_ao_context *)entry)->
									index));
			}
		}
	}
	me4000_outl(tmp, ao_context->preload_reg);
	spin_unlock(&ao_context->board_info->preload_lock);

	return 0;
}

static int me4000_ao_simultaneous_update(struct me4000_ao_channel_list *arg,
					 struct me4000_ao_context *ao_context)
{
	int err;
	int i;
	u32 tmp;
	struct me4000_ao_channel_list channels;

	CALL_PDEBUG("me4000_ao_simultaneous_update() is executed\n");

	/* Copy data from user */
	err = copy_from_user(&channels, arg,
			sizeof(struct me4000_ao_channel_list));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_ao_simultaneous_update():Can't copy command\n");
		return -EFAULT;
	}

	channels.list =
	    kzalloc(sizeof(unsigned long) * channels.count, GFP_KERNEL);
	if (!channels.list) {
		printk(KERN_ERR
		       "ME4000:me4000_ao_simultaneous_update():Can't get buffer\n");
		return -ENOMEM;
	}

	/* Copy channel list from user */
	err =
	    copy_from_user(channels.list, arg->list,
			   sizeof(unsigned long) * channels.count);
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_ao_simultaneous_update():Can't copy list\n");
		kfree(channels.list);
		return -EFAULT;
	}

	spin_lock(&ao_context->board_info->preload_lock);
	tmp = me4000_inl(ao_context->preload_reg);
	for (i = 0; i < channels.count; i++) {
		if (channels.list[i] >
		    ao_context->board_info->board_p->ao.count) {
			spin_unlock(&ao_context->board_info->preload_lock);
			kfree(channels.list);
			printk(KERN_ERR
			       "ME4000:me4000_ao_simultaneous_update():Invalid board number specified\n");
			return -EFAULT;
		}
		/* Clear the preload bit */
		tmp &= ~(0x1 << channels.list[i]);
		/* Clear the hw simultaneous bit */
		tmp &= ~(0x1 << (channels.list[i] + 16));
	}
	me4000_outl(tmp, ao_context->preload_reg);
	spin_unlock(&ao_context->board_info->preload_lock);
	kfree(channels.list);

	return 0;
}

static int me4000_ao_synchronous_ex_trig(struct me4000_ao_context *ao_context)
{
	u32 tmp;
	unsigned long flags;

	CALL_PDEBUG("me4000_ao_synchronous_ex_trig() is executed\n");

	/* Check if the state machine is stopped */
	tmp = me4000_inl(ao_context->status_reg);
	if (tmp & ME4000_AO_STATUS_BIT_FSM) {
		printk(KERN_ERR
		       "me4000_ao_synchronous_ex_trig(): DAC is running\n");
		return -EBUSY;
	}

	spin_lock(&ao_context->board_info->preload_lock);
	tmp = me4000_inl(ao_context->preload_reg);
	/* Disable synchronous sw bit */
	tmp &= ~(0x1 << ao_context->index);
	/* Enable synchronous hw bit */
	tmp |= 0x1 << (ao_context->index + 16);
	me4000_outl(tmp, ao_context->preload_reg);
	spin_unlock(&ao_context->board_info->preload_lock);

	/* Make runnable */
	spin_lock_irqsave(&ao_context->int_lock, flags);
	tmp = me4000_inl(ao_context->ctrl_reg);
	if (tmp & (ME4000_AO_CTRL_BIT_MODE_0 | ME4000_AO_CTRL_BIT_MODE_1)) {
		tmp &=
		    ~(ME4000_AO_CTRL_BIT_STOP |
		      ME4000_AO_CTRL_BIT_IMMEDIATE_STOP);
		me4000_outl(tmp, ao_context->ctrl_reg);
	}
	spin_unlock_irqrestore(&ao_context->int_lock, flags);

	return 0;
}

static int me4000_ao_synchronous_sw(struct me4000_ao_context *ao_context)
{
	u32 tmp;
	unsigned long flags;

	CALL_PDEBUG("me4000_ao_synchronous_sw() is executed\n");

	/* Check if the state machine is stopped */
	tmp = me4000_inl(ao_context->status_reg);
	if (tmp & ME4000_AO_STATUS_BIT_FSM) {
		printk(KERN_ERR "me4000_ao_synchronous_sw(): DAC is running\n");
		return -EBUSY;
	}

	spin_lock(&ao_context->board_info->preload_lock);
	tmp = me4000_inl(ao_context->preload_reg);
	/* Enable synchronous sw bit */
	tmp |= 0x1 << ao_context->index;
	/* Disable synchronous hw bit */
	tmp &= ~(0x1 << (ao_context->index + 16));
	me4000_outl(tmp, ao_context->preload_reg);
	spin_unlock(&ao_context->board_info->preload_lock);

	/* Make runnable */
	spin_lock_irqsave(&ao_context->int_lock, flags);
	tmp = me4000_inl(ao_context->ctrl_reg);
	if (tmp & (ME4000_AO_CTRL_BIT_MODE_0 | ME4000_AO_CTRL_BIT_MODE_1)) {
		tmp &=
		    ~(ME4000_AO_CTRL_BIT_STOP |
		      ME4000_AO_CTRL_BIT_IMMEDIATE_STOP);
		me4000_outl(tmp, ao_context->ctrl_reg);
	}
	spin_unlock_irqrestore(&ao_context->int_lock, flags);

	return 0;
}

static int me4000_ao_synchronous_disable(struct me4000_ao_context *ao_context)
{
	return me4000_ao_simultaneous_disable(ao_context);
}

static int me4000_ao_get_free_buffer(unsigned long *arg,
				     struct me4000_ao_context *ao_context)
{
	unsigned long c;
	int err;

	c = me4000_buf_space(ao_context->circ_buf, ME4000_AO_BUFFER_COUNT);

	err = copy_to_user(arg, &c, sizeof(unsigned long));
	if (err) {
		printk(KERN_ERR
		       "%s:Can't copy to user space\n", __func__);
		return -EFAULT;
	}

	return 0;
}

static int me4000_ao_ex_trig_timeout(unsigned long *arg,
				     struct me4000_ao_context *ao_context)
{
	u32 tmp;
	wait_queue_head_t queue;
	unsigned long ref;
	unsigned long timeout;

	CALL_PDEBUG("me4000_ao_ex_trig_timeout() is executed\n");

	if (get_user(timeout, arg)) {
		printk(KERN_ERR
		       "me4000_ao_ex_trig_timeout():Cannot copy data from user\n");
		return -EFAULT;
	}

	init_waitqueue_head(&queue);

	tmp = inl(ao_context->ctrl_reg);

	if ((tmp & ME4000_AO_CTRL_BIT_ENABLE_EX_TRIG)) {
		if (timeout) {
			ref = jiffies;
			while ((inl(ao_context->status_reg) &
				ME4000_AO_STATUS_BIT_FSM)) {
				interruptible_sleep_on_timeout(&queue, 1);
				if (signal_pending(current)) {
					printk(KERN_ERR
					       "ME4000:me4000_ao_ex_trig_timeout():Wait on start of state machine interrupted\n");
					return -EINTR;
				}
				/* kernel 2.6 has different definitions for HZ
				 * in user and kernel space */
				if ((jiffies - ref) > (timeout * HZ / USER_HZ)) {
					printk(KERN_ERR
					       "ME4000:me4000_ao_ex_trig_timeout():Timeout reached\n");
					return -EIO;
				}
			}
		} else {
			while ((inl(ao_context->status_reg) &
				ME4000_AO_STATUS_BIT_FSM)) {
				interruptible_sleep_on_timeout(&queue, 1);
				if (signal_pending(current)) {
					printk(KERN_ERR
					       "ME4000:me4000_ao_ex_trig_timeout():Wait on start of state machine interrupted\n");
					return -EINTR;
				}
			}
		}
	} else {
		printk(KERN_ERR
		       "ME4000:me4000_ao_ex_trig_timeout():External Trigger is not enabled\n");
		return -EINVAL;
	}

	return 0;
}

static int me4000_ao_enable_do(struct me4000_ao_context *ao_context)
{
	u32 tmp;
	unsigned long flags;

	CALL_PDEBUG("me4000_ao_enable_do() is executed\n");

	/* Only available for analog output 3 */
	if (ao_context->index != 3) {
		printk(KERN_ERR
		       "me4000_ao_enable_do():Only available for analog output 3\n");
		return -ENOTTY;
	}

	/* Check if the state machine is stopped */
	tmp = me4000_inl(ao_context->status_reg);
	if (tmp & ME4000_AO_STATUS_BIT_FSM) {
		printk(KERN_ERR "me4000_ao_enable_do(): DAC is running\n");
		return -EBUSY;
	}

	/* Set the stop bit */
	spin_lock_irqsave(&ao_context->int_lock, flags);
	tmp = inl(ao_context->ctrl_reg);
	tmp |= ME4000_AO_CTRL_BIT_ENABLE_DO;
	me4000_outl(tmp, ao_context->ctrl_reg);
	spin_unlock_irqrestore(&ao_context->int_lock, flags);

	return 0;
}

static int me4000_ao_disable_do(struct me4000_ao_context *ao_context)
{
	u32 tmp;
	unsigned long flags;

	CALL_PDEBUG("me4000_ao_disable_do() is executed\n");

	/* Only available for analog output 3 */
	if (ao_context->index != 3) {
		printk(KERN_ERR
		       "me4000_ao_disable():Only available for analog output 3\n");
		return -ENOTTY;
	}

	/* Check if the state machine is stopped */
	tmp = me4000_inl(ao_context->status_reg);
	if (tmp & ME4000_AO_STATUS_BIT_FSM) {
		printk(KERN_ERR "me4000_ao_disable_do(): DAC is running\n");
		return -EBUSY;
	}

	spin_lock_irqsave(&ao_context->int_lock, flags);
	tmp = inl(ao_context->ctrl_reg);
	tmp &= ~(ME4000_AO_CTRL_BIT_ENABLE_DO);
	me4000_outl(tmp, ao_context->ctrl_reg);
	spin_unlock_irqrestore(&ao_context->int_lock, flags);

	return 0;
}

static int me4000_ao_fsm_state(int *arg, struct me4000_ao_context *ao_context)
{
	unsigned long tmp;

	CALL_PDEBUG("me4000_ao_fsm_state() is executed\n");

	tmp =
	    (me4000_inl(ao_context->status_reg) & ME4000_AO_STATUS_BIT_FSM) ? 1
	    : 0;

	if (ao_context->pipe_flag) {
		printk(KERN_ERR "me4000_ao_fsm_state():Broken pipe detected\n");
		return -EPIPE;
	}

	if (put_user(tmp, arg)) {
		printk(KERN_ERR "me4000_ao_fsm_state():Cannot copy to user\n");
		return -EFAULT;
	}

	return 0;
}

/*------------------------- Analog input stuff -------------------------------*/

static int me4000_ai_prepare(struct me4000_ai_context *ai_context)
{
	wait_queue_head_t queue;
	int err;

	CALL_PDEBUG("me4000_ai_prepare() is executed\n");

	init_waitqueue_head(&queue);

	/* Set the new mode and stop bits */
	me4000_outl(ai_context->
		    mode | ME4000_AI_CTRL_BIT_STOP |
		    ME4000_AI_CTRL_BIT_IMMEDIATE_STOP, ai_context->ctrl_reg);

	/* Set the timer registers */
	ai_context->chan_timer = 66;
	ai_context->chan_pre_timer = 66;
	ai_context->scan_timer_low = 0;
	ai_context->scan_timer_high = 0;

	me4000_outl(65, ai_context->chan_timer_reg);
	me4000_outl(65, ai_context->chan_pre_timer_reg);
	me4000_outl(0, ai_context->scan_timer_low_reg);
	me4000_outl(0, ai_context->scan_timer_high_reg);
	me4000_outl(0, ai_context->scan_pre_timer_low_reg);
	me4000_outl(0, ai_context->scan_pre_timer_high_reg);

	ai_context->channel_list_count = 0;

	if (ai_context->mode) {
		/* Request the interrupt line */
		err =
		    request_irq(ai_context->irq, me4000_ai_isr,
				IRQF_DISABLED | IRQF_SHARED, ME4000_NAME,
				ai_context);
		if (err) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_prepare():Can't get interrupt line");
			return -ENODEV;
		}

		/* Allocate circular buffer */
		ai_context->circ_buf.buf =
		    kzalloc(ME4000_AI_BUFFER_SIZE, GFP_KERNEL);
		if (!ai_context->circ_buf.buf) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_prepare():Can't get circular buffer\n");
			free_irq(ai_context->irq, ai_context);
			return -ENOMEM;
		}

		/* Clear the circular buffer */
		ai_context->circ_buf.head = 0;
		ai_context->circ_buf.tail = 0;
	}

	return 0;
}

static int me4000_ai_reset(struct me4000_ai_context *ai_context)
{
	wait_queue_head_t queue;
	u32 tmp;
	unsigned long flags;

	CALL_PDEBUG("me4000_ai_reset() is executed\n");

	init_waitqueue_head(&queue);

	/*
	 * First stop conversion of the state machine before reconfigure.
	 * If not stopped before configuring mode, it could
	 * walk in a undefined state.
	 */
	spin_lock_irqsave(&ai_context->int_lock, flags);
	tmp = me4000_inl(ai_context->ctrl_reg);
	tmp |= ME4000_AI_CTRL_BIT_IMMEDIATE_STOP;
	me4000_outl(tmp, ai_context->ctrl_reg);
	spin_unlock_irqrestore(&ai_context->int_lock, flags);

	while (inl(ai_context->status_reg) & ME4000_AI_STATUS_BIT_FSM) {
		interruptible_sleep_on_timeout(&queue, 1);
		if (signal_pending(current)) {
			printk(KERN_ERR
			       "me4000_ai_reset():Wait on state machine after stop interrupted\n");
			return -EINTR;
		}
	}

	/* Clear the control register and set the stop bits */
	spin_lock_irqsave(&ai_context->int_lock, flags);
	tmp = me4000_inl(ai_context->ctrl_reg);
	me4000_outl(ME4000_AI_CTRL_BIT_IMMEDIATE_STOP | ME4000_AI_CTRL_BIT_STOP,
		    ai_context->ctrl_reg);
	spin_unlock_irqrestore(&ai_context->int_lock, flags);

	/* Reset timer registers */
	ai_context->chan_timer = 66;
	ai_context->chan_pre_timer = 66;
	ai_context->scan_timer_low = 0;
	ai_context->scan_timer_high = 0;
	ai_context->sample_counter = 0;
	ai_context->sample_counter_reload = 0;

	me4000_outl(65, ai_context->chan_timer_reg);
	me4000_outl(65, ai_context->chan_pre_timer_reg);
	me4000_outl(0, ai_context->scan_timer_low_reg);
	me4000_outl(0, ai_context->scan_timer_high_reg);
	me4000_outl(0, ai_context->scan_pre_timer_low_reg);
	me4000_outl(0, ai_context->scan_pre_timer_high_reg);
	me4000_outl(0, ai_context->sample_counter_reg);

	ai_context->channel_list_count = 0;

	/* Clear the circular buffer */
	ai_context->circ_buf.head = 0;
	ai_context->circ_buf.tail = 0;

	return 0;
}

static int me4000_ai_ioctl_sing(struct inode *inode_p, struct file *file_p,
				unsigned int service, unsigned long arg)
{
	struct me4000_ai_context *ai_context;

	CALL_PDEBUG("me4000_ai_ioctl_sing() is executed\n");

	ai_context = file_p->private_data;

	if (_IOC_TYPE(service) != ME4000_MAGIC) {
		printk(KERN_ERR "me4000_ai_ioctl_sing():Wrong magic number\n");
		return -ENOTTY;
	}
	if (_IOC_NR(service) > ME4000_IOCTL_MAXNR) {
		printk(KERN_ERR
		       "me4000_ai_ioctl_sing():Service number to high\n");
		return -ENOTTY;
	}

	switch (service) {
	case ME4000_AI_SINGLE:
		return me4000_ai_single((struct me4000_ai_single *)arg,
								ai_context);
	case ME4000_AI_EX_TRIG_ENABLE:
		return me4000_ai_ex_trig_enable(ai_context);
	case ME4000_AI_EX_TRIG_DISABLE:
		return me4000_ai_ex_trig_disable(ai_context);
	case ME4000_AI_EX_TRIG_SETUP:
		return me4000_ai_ex_trig_setup((struct me4000_ai_trigger *)arg,
					       ai_context);
	case ME4000_GET_USER_INFO:
		return me4000_get_user_info((struct me4000_user_info *)arg,
					    ai_context->board_info);
	case ME4000_AI_OFFSET_ENABLE:
		return me4000_ai_offset_enable(ai_context);
	case ME4000_AI_OFFSET_DISABLE:
		return me4000_ai_offset_disable(ai_context);
	case ME4000_AI_FULLSCALE_ENABLE:
		return me4000_ai_fullscale_enable(ai_context);
	case ME4000_AI_FULLSCALE_DISABLE:
		return me4000_ai_fullscale_disable(ai_context);
	case ME4000_AI_EEPROM_READ:
		return me4000_eeprom_read((struct me4000_eeprom *)arg,
								ai_context);
	case ME4000_AI_EEPROM_WRITE:
		return me4000_eeprom_write((struct me4000_eeprom *)arg,
								ai_context);
	default:
		printk(KERN_ERR
		       "me4000_ai_ioctl_sing():Invalid service number\n");
		return -ENOTTY;
	}
	return 0;
}

static int me4000_ai_single(struct me4000_ai_single *arg,
			    struct me4000_ai_context *ai_context)
{
	struct me4000_ai_single cmd;
	int err;
	u32 tmp;
	wait_queue_head_t queue;
	unsigned long jiffy;

	CALL_PDEBUG("me4000_ai_single() is executed\n");

	init_waitqueue_head(&queue);

	/* Copy data from user */
	err = copy_from_user(&cmd, arg, sizeof(struct me4000_ai_single));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_ai_single():Can't copy from user space\n");
		return -EFAULT;
	}

	/* Check range parameter */
	switch (cmd.range) {
	case ME4000_AI_LIST_RANGE_BIPOLAR_10:
	case ME4000_AI_LIST_RANGE_BIPOLAR_2_5:
	case ME4000_AI_LIST_RANGE_UNIPOLAR_10:
	case ME4000_AI_LIST_RANGE_UNIPOLAR_2_5:
		break;
	default:
		printk(KERN_ERR
		       "ME4000:me4000_ai_single():Invalid range specified\n");
		return -EINVAL;
	}

	/* Check mode and channel number */
	switch (cmd.mode) {
	case ME4000_AI_LIST_INPUT_SINGLE_ENDED:
		if (cmd.channel >= ai_context->board_info->board_p->ai.count) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_single():Analog input is not available\n");
			return -EINVAL;
		}
		break;
	case ME4000_AI_LIST_INPUT_DIFFERENTIAL:
		if (cmd.channel >=
		    ai_context->board_info->board_p->ai.diff_count) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_single():Analog input is not available in differential mode\n");
			return -EINVAL;
		}
		break;
	default:
		printk(KERN_ERR
		       "ME4000:me4000_ai_single():Invalid mode specified\n");
		return -EINVAL;
	}

	/* Clear channel list, data fifo and both stop bits */
	tmp = me4000_inl(ai_context->ctrl_reg);
	tmp &=
	    ~(ME4000_AI_CTRL_BIT_CHANNEL_FIFO | ME4000_AI_CTRL_BIT_DATA_FIFO |
	      ME4000_AI_CTRL_BIT_STOP | ME4000_AI_CTRL_BIT_IMMEDIATE_STOP);
	me4000_outl(tmp, ai_context->ctrl_reg);

	/* Enable channel list and data fifo */
	tmp |= ME4000_AI_CTRL_BIT_CHANNEL_FIFO | ME4000_AI_CTRL_BIT_DATA_FIFO;
	me4000_outl(tmp, ai_context->ctrl_reg);

	/* Generate channel list entry */
	me4000_outl(cmd.channel | cmd.range | cmd.
		    mode | ME4000_AI_LIST_LAST_ENTRY,
		    ai_context->channel_list_reg);

	/* Set the timer to maximum */
	me4000_outl(66, ai_context->chan_timer_reg);
	me4000_outl(66, ai_context->chan_pre_timer_reg);

	if (tmp & ME4000_AI_CTRL_BIT_EX_TRIG) {
		jiffy = jiffies;
		while (!
		       (me4000_inl(ai_context->status_reg) &
			ME4000_AI_STATUS_BIT_EF_DATA)) {
			interruptible_sleep_on_timeout(&queue, 1);
			if (signal_pending(current)) {
				printk(KERN_ERR
				       "ME4000:me4000_ai_single():Wait on start of state machine interrupted\n");
				return -EINTR;
			}
			/* 2.6 has different definitions for HZ in user and kernel space */
			if (((jiffies - jiffy) > (cmd.timeout * HZ / USER_HZ)) && cmd.timeout) {
				printk(KERN_ERR
				       "ME4000:me4000_ai_single():Timeout reached\n");
				return -EIO;
			}
		}
	} else {
		/* Start conversion */
		me4000_inl(ai_context->start_reg);

		/* Wait until ready */
		udelay(10);
		if (!
		    (me4000_inl(ai_context->status_reg) &
		     ME4000_AI_STATUS_BIT_EF_DATA)) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_single():Value not available after wait\n");
			return -EIO;
		}
	}

	/* Read value from data fifo */
	cmd.value = me4000_inl(ai_context->data_reg) & 0xFFFF;

	/* Copy result back to user */
	err = copy_to_user(arg, &cmd, sizeof(struct me4000_ai_single));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_ai_single():Can't copy to user space\n");
		return -EFAULT;
	}

	return 0;
}

static int me4000_ai_ioctl_sw(struct inode *inode_p, struct file *file_p,
			      unsigned int service, unsigned long arg)
{
	struct me4000_ai_context *ai_context;

	CALL_PDEBUG("me4000_ai_ioctl_sw() is executed\n");

	ai_context = file_p->private_data;

	if (_IOC_TYPE(service) != ME4000_MAGIC) {
		printk(KERN_ERR "me4000_ai_ioctl_sw():Wrong magic number\n");
		return -ENOTTY;
	}
	if (_IOC_NR(service) > ME4000_IOCTL_MAXNR) {
		printk(KERN_ERR
		       "me4000_ai_ioctl_sw():Service number to high\n");
		return -ENOTTY;
	}

	switch (service) {
	case ME4000_AI_SC_SETUP:
		return me4000_ai_sc_setup((struct me4000_ai_sc *)arg,
								ai_context);
	case ME4000_AI_CONFIG:
		return me4000_ai_config((struct me4000_ai_config *)arg,
								ai_context);
	case ME4000_AI_START:
		return me4000_ai_start(ai_context);
	case ME4000_AI_STOP:
		return me4000_ai_stop(ai_context);
	case ME4000_AI_IMMEDIATE_STOP:
		return me4000_ai_immediate_stop(ai_context);
	case ME4000_AI_FSM_STATE:
		return me4000_ai_fsm_state((int *)arg, ai_context);
	case ME4000_GET_USER_INFO:
		return me4000_get_user_info((struct me4000_user_info *)arg,
					    ai_context->board_info);
	case ME4000_AI_EEPROM_READ:
		return me4000_eeprom_read((struct me4000_eeprom *)arg,
								ai_context);
	case ME4000_AI_EEPROM_WRITE:
		return me4000_eeprom_write((struct me4000_eeprom *)arg,
								ai_context);
	case ME4000_AI_GET_COUNT_BUFFER:
		return me4000_ai_get_count_buffer((unsigned long *)arg,
						  ai_context);
	default:
		printk(KERN_ERR
		       "%s:Invalid service number %d\n", __func__, service);
		return -ENOTTY;
	}
	return 0;
}

static int me4000_ai_ioctl_ext(struct inode *inode_p, struct file *file_p,
			       unsigned int service, unsigned long arg)
{
	struct me4000_ai_context *ai_context;

	CALL_PDEBUG("me4000_ai_ioctl_ext() is executed\n");

	ai_context = file_p->private_data;

	if (_IOC_TYPE(service) != ME4000_MAGIC) {
		printk(KERN_ERR "me4000_ai_ioctl_ext():Wrong magic number\n");
		return -ENOTTY;
	}
	if (_IOC_NR(service) > ME4000_IOCTL_MAXNR) {
		printk(KERN_ERR
		       "me4000_ai_ioctl_ext():Service number to high\n");
		return -ENOTTY;
	}

	switch (service) {
	case ME4000_AI_SC_SETUP:
		return me4000_ai_sc_setup((struct me4000_ai_sc *)arg,
								ai_context);
	case ME4000_AI_CONFIG:
		return me4000_ai_config((struct me4000_ai_config *)arg,
								ai_context);
	case ME4000_AI_START:
		return me4000_ai_start_ex((unsigned long *)arg, ai_context);
	case ME4000_AI_STOP:
		return me4000_ai_stop(ai_context);
	case ME4000_AI_IMMEDIATE_STOP:
		return me4000_ai_immediate_stop(ai_context);
	case ME4000_AI_EX_TRIG_ENABLE:
		return me4000_ai_ex_trig_enable(ai_context);
	case ME4000_AI_EX_TRIG_DISABLE:
		return me4000_ai_ex_trig_disable(ai_context);
	case ME4000_AI_EX_TRIG_SETUP:
		return me4000_ai_ex_trig_setup((struct me4000_ai_trigger *)arg,
					       ai_context);
	case ME4000_AI_FSM_STATE:
		return me4000_ai_fsm_state((int *)arg, ai_context);
	case ME4000_GET_USER_INFO:
		return me4000_get_user_info((struct me4000_user_info *)arg,
					    ai_context->board_info);
	case ME4000_AI_GET_COUNT_BUFFER:
		return me4000_ai_get_count_buffer((unsigned long *)arg,
						  ai_context);
	default:
		printk(KERN_ERR
		       "%s:Invalid service number %d\n", __func__ , service);
		return -ENOTTY;
	}
	return 0;
}

static int me4000_ai_fasync(int fd, struct file *file_p, int mode)
{
	struct me4000_ai_context *ai_context;

	CALL_PDEBUG("me4000_ao_fasync_cont() is executed\n");

	ai_context = file_p->private_data;
	return fasync_helper(fd, file_p, mode, &ai_context->fasync_p);
}

static int me4000_ai_config(struct me4000_ai_config *arg,
			    struct me4000_ai_context *ai_context)
{
	struct me4000_ai_config cmd;
	u32 *list = NULL;
	u32 mode;
	int i;
	int err;
	wait_queue_head_t queue;
	u64 scan;
	u32 tmp;

	CALL_PDEBUG("me4000_ai_config() is executed\n");

	init_waitqueue_head(&queue);

	/* Check if conversion is stopped */
	if (inl(ai_context->ctrl_reg) & ME4000_AI_STATUS_BIT_FSM) {
		printk(KERN_ERR
		       "ME4000:me4000_ai_config():Conversion is not stopped\n");
		err = -EBUSY;
		goto AI_CONFIG_ERR;
	}

	/* Copy data from user */
	err = copy_from_user(&cmd, arg, sizeof(struct me4000_ai_config));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_ai_config():Can't copy from user space\n");
		err = -EFAULT;
		goto AI_CONFIG_ERR;
	}

	PDEBUG
	    ("me4000_ai_config():chan = %ld, pre_chan = %ld, scan_low = %ld, scan_high = %ld, count = %ld\n",
	     cmd.timer.chan, cmd.timer.pre_chan, cmd.timer.scan_low,
	     cmd.timer.scan_high, cmd.channel_list.count);

	/* Check whether sample and hold is available for this board */
	if (cmd.sh) {
		if (!ai_context->board_info->board_p->ai.sh_count) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_config():Sample and Hold is not available for this board\n");
			err = -ENODEV;
			goto AI_CONFIG_ERR;
		}
	}

	/* Check the channel list size */
	if (cmd.channel_list.count > ME4000_AI_CHANNEL_LIST_COUNT) {
		printk(KERN_ERR
		       "me4000_ai_config():Channel list is to large\n");
		err = -EINVAL;
		goto AI_CONFIG_ERR;
	}

	/* Copy channel list from user */
	list = kmalloc(sizeof(u32) * cmd.channel_list.count, GFP_KERNEL);
	if (!list) {
		printk(KERN_ERR
		       "ME4000:me4000_ai_config():Can't get memory for channel list\n");
		err = -ENOMEM;
		goto AI_CONFIG_ERR;
	}
	err =
	    copy_from_user(list, cmd.channel_list.list,
			   sizeof(u32) * cmd.channel_list.count);
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_ai_config():Can't copy from user space\n");
		err = -EFAULT;
		goto AI_CONFIG_ERR;
	}

	/* Check if last entry bit is set */
	if (!(list[cmd.channel_list.count - 1] & ME4000_AI_LIST_LAST_ENTRY)) {
		printk(KERN_WARNING
		       "me4000_ai_config():Last entry bit is not set\n");
		list[cmd.channel_list.count - 1] |= ME4000_AI_LIST_LAST_ENTRY;
	}

	/* Check whether mode is equal for all entries */
	mode = list[0] & 0x20;
	for (i = 0; i < cmd.channel_list.count; i++) {
		if ((list[i] & 0x20) != mode) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_config():Mode is not equal for all entries\n");
			err = -EINVAL;
			goto AI_CONFIG_ERR;
		}
	}

	/* Check whether channels are available for this mode */
	if (mode == ME4000_AI_LIST_INPUT_SINGLE_ENDED) {
		for (i = 0; i < cmd.channel_list.count; i++) {
			if ((list[i] & 0x1F) >=
			    ai_context->board_info->board_p->ai.count) {
				printk(KERN_ERR
				       "ME4000:me4000_ai_config():Channel is not available for single ended\n");
				err = -EINVAL;
				goto AI_CONFIG_ERR;
			}
		}
	} else if (mode == ME4000_AI_LIST_INPUT_DIFFERENTIAL) {
		for (i = 0; i < cmd.channel_list.count; i++) {
			if ((list[i] & 0x1F) >=
			    ai_context->board_info->board_p->ai.diff_count) {
				printk(KERN_ERR
				       "ME4000:me4000_ai_config():Channel is not available for differential\n");
				err = -EINVAL;
				goto AI_CONFIG_ERR;
			}
		}
	}

	/* Check if bipolar is set for all entries when in differential mode */
	if (mode == ME4000_AI_LIST_INPUT_DIFFERENTIAL) {
		for (i = 0; i < cmd.channel_list.count; i++) {
			if ((list[i] & 0xC0) != ME4000_AI_LIST_RANGE_BIPOLAR_10
			    && (list[i] & 0xC0) !=
			    ME4000_AI_LIST_RANGE_BIPOLAR_2_5) {
				printk(KERN_ERR
				       "ME4000:me4000_ai_config():Bipolar is not selected in differential mode\n");
				err = -EINVAL;
				goto AI_CONFIG_ERR;
			}
		}
	}

	if (ai_context->mode != ME4000_AI_ACQ_MODE_EXT_SINGLE_VALUE) {
		/* Check for minimum channel divisor */
		if (cmd.timer.chan < ME4000_AI_MIN_TICKS) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_config():Channel timer divisor is to low\n");
			err = -EINVAL;
			goto AI_CONFIG_ERR;
		}

		/* Check if minimum channel divisor is adjusted when sample and hold is activated */
		if ((cmd.sh) && (cmd.timer.chan != ME4000_AI_MIN_TICKS)) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_config():Channel timer divisor must be at minimum when sample and hold is activated\n");
			err = -EINVAL;
			goto AI_CONFIG_ERR;
		}

		/* Check for minimum channel pre divisor */
		if (cmd.timer.pre_chan < ME4000_AI_MIN_TICKS) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_config():Channel pre timer divisor is to low\n");
			err = -EINVAL;
			goto AI_CONFIG_ERR;
		}

		/* Write the channel timers */
		me4000_outl(cmd.timer.chan - 1, ai_context->chan_timer_reg);
		me4000_outl(cmd.timer.pre_chan - 1,
			    ai_context->chan_pre_timer_reg);

		/* Save the timer values in the board context */
		ai_context->chan_timer = cmd.timer.chan;
		ai_context->chan_pre_timer = cmd.timer.pre_chan;

		if (ai_context->mode != ME4000_AI_ACQ_MODE_EXT_SINGLE_CHANLIST) {
			/* Check for scan timer divisor */
			scan =
			    (u64) cmd.timer.scan_low | ((u64) cmd.timer.
							scan_high << 32);
			if (scan != 0) {
				if (scan <
				    cmd.channel_list.count * cmd.timer.chan +
				    1) {
					printk(KERN_ERR
					       "ME4000:me4000_ai_config():Scan timer divisor is to low\n");
					err = -EINVAL;
					goto AI_CONFIG_ERR;
				}
			}

			/* Write the scan timers */
			if (scan != 0) {
				scan--;
				tmp = (u32) (scan & 0xFFFFFFFF);
				me4000_outl(tmp,
					    ai_context->scan_timer_low_reg);
				tmp = (u32) ((scan >> 32) & 0xFFFFFFFF);
				me4000_outl(tmp,
					    ai_context->scan_timer_high_reg);

				scan =
				    scan - (cmd.timer.chan - 1) +
				    (cmd.timer.pre_chan - 1);
				tmp = (u32) (scan & 0xFFFFFFFF);
				me4000_outl(tmp,
					    ai_context->scan_pre_timer_low_reg);
				tmp = (u32) ((scan >> 32) & 0xFFFFFFFF);
				me4000_outl(tmp,
					    ai_context->
					    scan_pre_timer_high_reg);
			} else {
				me4000_outl(0x0,
					    ai_context->scan_timer_low_reg);
				me4000_outl(0x0,
					    ai_context->scan_timer_high_reg);

				me4000_outl(0x0,
					    ai_context->scan_pre_timer_low_reg);
				me4000_outl(0x0,
					    ai_context->
					    scan_pre_timer_high_reg);
			}

			ai_context->scan_timer_low = cmd.timer.scan_low;
			ai_context->scan_timer_high = cmd.timer.scan_high;
		}
	}

	/* Clear the channel list */
	tmp = me4000_inl(ai_context->ctrl_reg);
	tmp &= ~ME4000_AI_CTRL_BIT_CHANNEL_FIFO;
	me4000_outl(tmp, ai_context->ctrl_reg);
	tmp |= ME4000_AI_CTRL_BIT_CHANNEL_FIFO;
	me4000_outl(tmp, ai_context->ctrl_reg);

	/* Write the channel list */
	for (i = 0; i < cmd.channel_list.count; i++)
		me4000_outl(list[i], ai_context->channel_list_reg);

	/* Setup sample and hold */
	if (cmd.sh) {
		tmp |= ME4000_AI_CTRL_BIT_SAMPLE_HOLD;
		me4000_outl(tmp, ai_context->ctrl_reg);
	} else {
		tmp &= ~ME4000_AI_CTRL_BIT_SAMPLE_HOLD;
		me4000_outl(tmp, ai_context->ctrl_reg);
	}

	/* Save the channel list size in the board context */
	ai_context->channel_list_count = cmd.channel_list.count;

	kfree(list);

	return 0;

AI_CONFIG_ERR:

	/* Reset the timers */
	ai_context->chan_timer = 66;
	ai_context->chan_pre_timer = 66;
	ai_context->scan_timer_low = 0;
	ai_context->scan_timer_high = 0;

	me4000_outl(65, ai_context->chan_timer_reg);
	me4000_outl(65, ai_context->chan_pre_timer_reg);
	me4000_outl(0, ai_context->scan_timer_high_reg);
	me4000_outl(0, ai_context->scan_timer_low_reg);
	me4000_outl(0, ai_context->scan_pre_timer_high_reg);
	me4000_outl(0, ai_context->scan_pre_timer_low_reg);

	ai_context->channel_list_count = 0;

	tmp = me4000_inl(ai_context->ctrl_reg);
	tmp &=
	    ~(ME4000_AI_CTRL_BIT_CHANNEL_FIFO | ME4000_AI_CTRL_BIT_SAMPLE_HOLD);

	kfree(list);

	return err;

}

static int ai_common_start(struct me4000_ai_context *ai_context)
{
	u32 tmp;
	CALL_PDEBUG("ai_common_start() is executed\n");

	tmp = me4000_inl(ai_context->ctrl_reg);

	/* Check if conversion is stopped */
	if (tmp & ME4000_AI_STATUS_BIT_FSM) {
		printk(KERN_ERR
		       "ME4000:ai_common_start():Conversion is not stopped\n");
		return -EBUSY;
	}

	/* Clear data fifo, disable all interrupts, clear sample counter reload */
	tmp &= ~(ME4000_AI_CTRL_BIT_DATA_FIFO | ME4000_AI_CTRL_BIT_LE_IRQ |
		 ME4000_AI_CTRL_BIT_HF_IRQ | ME4000_AI_CTRL_BIT_SC_IRQ |
		 ME4000_AI_CTRL_BIT_SC_RELOAD);

	me4000_outl(tmp, ai_context->ctrl_reg);

	/* Clear circular buffer */
	ai_context->circ_buf.head = 0;
	ai_context->circ_buf.tail = 0;

	/* Enable data fifo */
	tmp |= ME4000_AI_CTRL_BIT_DATA_FIFO;

	/* Determine interrupt setup */
	if (ai_context->sample_counter && !ai_context->sample_counter_reload) {
		/* Enable Half Full Interrupt and Sample Counter Interrupt */
		tmp |= ME4000_AI_CTRL_BIT_SC_IRQ | ME4000_AI_CTRL_BIT_HF_IRQ;
	} else if (ai_context->sample_counter
		   && ai_context->sample_counter_reload) {
		if (ai_context->sample_counter <= ME4000_AI_FIFO_COUNT / 2) {
			/* Enable only Sample Counter Interrupt */
			tmp |=
			    ME4000_AI_CTRL_BIT_SC_IRQ |
			    ME4000_AI_CTRL_BIT_SC_RELOAD;
		} else {
			/* Enable Half Full Interrupt and Sample Counter Interrupt */
			tmp |=
			    ME4000_AI_CTRL_BIT_SC_IRQ |
			    ME4000_AI_CTRL_BIT_HF_IRQ |
			    ME4000_AI_CTRL_BIT_SC_RELOAD;
		}
	} else {
		/* Enable only Half Full Interrupt */
		tmp |= ME4000_AI_CTRL_BIT_HF_IRQ;
	}

	/* Clear the stop bits */
	tmp &= ~(ME4000_AI_CTRL_BIT_STOP | ME4000_AI_CTRL_BIT_IMMEDIATE_STOP);

	/* Write setup to hardware */
	me4000_outl(tmp, ai_context->ctrl_reg);

	/* Write sample counter */
	me4000_outl(ai_context->sample_counter, ai_context->sample_counter_reg);

	return 0;
}

static int me4000_ai_start(struct me4000_ai_context *ai_context)
{
	int err;
	CALL_PDEBUG("me4000_ai_start() is executed\n");

	/* Prepare Hardware */
	err = ai_common_start(ai_context);
	if (err)
		return err;

	/* Start conversion by dummy read */
	me4000_inl(ai_context->start_reg);

	return 0;
}

static int me4000_ai_start_ex(unsigned long *arg,
			      struct me4000_ai_context *ai_context)
{
	int err;
	wait_queue_head_t queue;
	unsigned long ref;
	unsigned long timeout;

	CALL_PDEBUG("me4000_ai_start_ex() is executed\n");

	if (get_user(timeout, arg)) {
		printk(KERN_ERR
		       "me4000_ai_start_ex():Cannot copy data from user\n");
		return -EFAULT;
	}

	init_waitqueue_head(&queue);

	/* Prepare Hardware */
	err = ai_common_start(ai_context);
	if (err)
		return err;

	if (timeout) {
		ref = jiffies;
		while (!(inl(ai_context->status_reg) & ME4000_AI_STATUS_BIT_FSM)) {
			interruptible_sleep_on_timeout(&queue, 1);
			if (signal_pending(current)) {
				printk(KERN_ERR
				       "ME4000:me4000_ai_start_ex():Wait on start of state machine interrupted\n");
				return -EINTR;
			}
			/* 2.6 has different definitions for HZ in user and kernel space */
			if ((jiffies - ref) > (timeout * HZ / USER_HZ)) {
				printk(KERN_ERR
				       "ME4000:me4000_ai_start_ex():Timeout reached\n");
				return -EIO;
			}
		}
	} else {
		while (!(inl(ai_context->status_reg) & ME4000_AI_STATUS_BIT_FSM)) {
			interruptible_sleep_on_timeout(&queue, 1);
			if (signal_pending(current)) {
				printk(KERN_ERR
				       "ME4000:me4000_ai_start_ex():Wait on start of state machine interrupted\n");
				return -EINTR;
			}
		}
	}

	return 0;
}

static int me4000_ai_stop(struct me4000_ai_context *ai_context)
{
	wait_queue_head_t queue;
	u32 tmp;
	unsigned long flags;

	CALL_PDEBUG("me4000_ai_stop() is executed\n");

	init_waitqueue_head(&queue);

	/* Disable irqs and clear data fifo */
	spin_lock_irqsave(&ai_context->int_lock, flags);
	tmp = me4000_inl(ai_context->ctrl_reg);
	tmp &=
	    ~(ME4000_AI_CTRL_BIT_HF_IRQ | ME4000_AI_CTRL_BIT_SC_IRQ |
	      ME4000_AI_CTRL_BIT_DATA_FIFO);
	/* Stop conversion of the state machine */
	tmp |= ME4000_AI_CTRL_BIT_STOP;
	me4000_outl(tmp, ai_context->ctrl_reg);
	spin_unlock_irqrestore(&ai_context->int_lock, flags);

	/* Clear circular buffer */
	ai_context->circ_buf.head = 0;
	ai_context->circ_buf.tail = 0;

	while (inl(ai_context->status_reg) & ME4000_AI_STATUS_BIT_FSM) {
		interruptible_sleep_on_timeout(&queue, 1);
		if (signal_pending(current)) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_stop():Wait on state machine after stop interrupted\n");
			return -EINTR;
		}
	}

	return 0;
}

static int me4000_ai_immediate_stop(struct me4000_ai_context *ai_context)
{
	wait_queue_head_t queue;
	u32 tmp;
	unsigned long flags;

	CALL_PDEBUG("me4000_ai_stop() is executed\n");

	init_waitqueue_head(&queue);

	/* Disable irqs and clear data fifo */
	spin_lock_irqsave(&ai_context->int_lock, flags);
	tmp = me4000_inl(ai_context->ctrl_reg);
	tmp &=
	    ~(ME4000_AI_CTRL_BIT_HF_IRQ | ME4000_AI_CTRL_BIT_SC_IRQ |
	      ME4000_AI_CTRL_BIT_DATA_FIFO);
	/* Stop conversion of the state machine */
	tmp |= ME4000_AI_CTRL_BIT_IMMEDIATE_STOP;
	me4000_outl(tmp, ai_context->ctrl_reg);
	spin_unlock_irqrestore(&ai_context->int_lock, flags);

	/* Clear circular buffer */
	ai_context->circ_buf.head = 0;
	ai_context->circ_buf.tail = 0;

	while (inl(ai_context->status_reg) & ME4000_AI_STATUS_BIT_FSM) {
		interruptible_sleep_on_timeout(&queue, 1);
		if (signal_pending(current)) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_stop():Wait on state machine after stop interrupted\n");
			return -EINTR;
		}
	}

	return 0;
}

static int me4000_ai_ex_trig_enable(struct me4000_ai_context *ai_context)
{
	u32 tmp;
	unsigned long flags;

	CALL_PDEBUG("me4000_ai_ex_trig_enable() is executed\n");

	spin_lock_irqsave(&ai_context->int_lock, flags);
	tmp = me4000_inl(ai_context->ctrl_reg);
	tmp |= ME4000_AI_CTRL_BIT_EX_TRIG;
	me4000_outl(tmp, ai_context->ctrl_reg);
	spin_unlock_irqrestore(&ai_context->int_lock, flags);

	return 0;
}

static int me4000_ai_ex_trig_disable(struct me4000_ai_context *ai_context)
{
	u32 tmp;
	unsigned long flags;

	CALL_PDEBUG("me4000_ai_ex_trig_disable() is executed\n");

	spin_lock_irqsave(&ai_context->int_lock, flags);
	tmp = me4000_inl(ai_context->ctrl_reg);
	tmp &= ~ME4000_AI_CTRL_BIT_EX_TRIG;
	me4000_outl(tmp, ai_context->ctrl_reg);
	spin_unlock_irqrestore(&ai_context->int_lock, flags);

	return 0;
}

static int me4000_ai_ex_trig_setup(struct me4000_ai_trigger *arg,
				   struct me4000_ai_context *ai_context)
{
	struct me4000_ai_trigger cmd;
	int err;
	u32 tmp;
	unsigned long flags;

	CALL_PDEBUG("me4000_ai_ex_trig_setup() is executed\n");

	/* Copy data from user */
	err = copy_from_user(&cmd, arg, sizeof(struct me4000_ai_trigger));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_ai_ex_trig_setup():Can't copy from user space\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&ai_context->int_lock, flags);
	tmp = me4000_inl(ai_context->ctrl_reg);

	if (cmd.mode == ME4000_AI_TRIGGER_EXT_DIGITAL) {
		tmp &= ~ME4000_AI_CTRL_BIT_EX_TRIG_ANALOG;
	} else if (cmd.mode == ME4000_AI_TRIGGER_EXT_ANALOG) {
		if (!ai_context->board_info->board_p->ai.ex_trig_analog) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_ex_trig_setup():No analog trigger available\n");
			return -EINVAL;
		}
		tmp |= ME4000_AI_CTRL_BIT_EX_TRIG_ANALOG;
	} else {
		spin_unlock_irqrestore(&ai_context->int_lock, flags);
		printk(KERN_ERR
		       "ME4000:me4000_ai_ex_trig_setup():Invalid trigger mode specified\n");
		return -EINVAL;
	}

	if (cmd.edge == ME4000_AI_TRIGGER_EXT_EDGE_RISING) {
		tmp &=
		    ~(ME4000_AI_CTRL_BIT_EX_TRIG_BOTH |
		      ME4000_AI_CTRL_BIT_EX_TRIG_FALLING);
	} else if (cmd.edge == ME4000_AI_TRIGGER_EXT_EDGE_FALLING) {
		tmp |= ME4000_AI_CTRL_BIT_EX_TRIG_FALLING;
		tmp &= ~ME4000_AI_CTRL_BIT_EX_TRIG_BOTH;
	} else if (cmd.edge == ME4000_AI_TRIGGER_EXT_EDGE_BOTH) {
		tmp |=
		    ME4000_AI_CTRL_BIT_EX_TRIG_BOTH |
		    ME4000_AI_CTRL_BIT_EX_TRIG_FALLING;
	} else {
		spin_unlock_irqrestore(&ai_context->int_lock, flags);
		printk(KERN_ERR
		       "ME4000:me4000_ai_ex_trig_setup():Invalid trigger edge specified\n");
		return -EINVAL;
	}

	me4000_outl(tmp, ai_context->ctrl_reg);
	spin_unlock_irqrestore(&ai_context->int_lock, flags);
	return 0;
}

static int me4000_ai_sc_setup(struct me4000_ai_sc *arg,
			      struct me4000_ai_context *ai_context)
{
	struct me4000_ai_sc cmd;
	int err;

	CALL_PDEBUG("me4000_ai_sc_setup() is executed\n");

	/* Copy data from user */
	err = copy_from_user(&cmd, arg, sizeof(struct me4000_ai_sc));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_ai_sc_setup():Can't copy from user space\n");
		return -EFAULT;
	}

	ai_context->sample_counter = cmd.value;
	ai_context->sample_counter_reload = cmd.reload;

	return 0;
}

static ssize_t me4000_ai_read(struct file *filep, char *buff, size_t cnt,
			      loff_t *offp)
{
	struct me4000_ai_context *ai_context = filep->private_data;
	s16 *buffer = (s16 *) buff;
	size_t count = cnt / 2;
	unsigned long flags;
	int tmp;
	int c = 0;
	int k = 0;
	int ret = 0;
	wait_queue_t wait;

	CALL_PDEBUG("me4000_ai_read() is executed\n");

	init_waitqueue_entry(&wait, current);

	/* Check count */
	if (count <= 0) {
		PDEBUG("me4000_ai_read():Count is 0\n");
		return 0;
	}

	while (count > 0) {
		if (filep->f_flags & O_NONBLOCK) {
			c = me4000_values_to_end(ai_context->circ_buf,
						 ME4000_AI_BUFFER_COUNT);
			if (!c) {
				PDEBUG
				    ("me4000_ai_read():Returning from nonblocking read\n");
				break;
			}
		} else {
			/* Check if conversion is still running */
			if (!
			    (me4000_inl(ai_context->status_reg) &
			     ME4000_AI_STATUS_BIT_FSM)) {
				printk(KERN_ERR
				       "ME4000:me4000_ai_read():Conversion interrupted\n");
				return -EPIPE;
			}

			wait_event_interruptible(ai_context->wait_queue,
						 (me4000_values_to_end
						  (ai_context->circ_buf,
						   ME4000_AI_BUFFER_COUNT)));
			if (signal_pending(current)) {
				printk(KERN_ERR
				       "ME4000:me4000_ai_read():Wait on values interrupted from signal\n");
				return -EINTR;
			}
		}

		/* Only read count values or as much as available */
		c = me4000_values_to_end(ai_context->circ_buf,
					 ME4000_AI_BUFFER_COUNT);
		PDEBUG("me4000_ai_read():%d values to end\n", c);
		if (count < c)
			c = count;

		PDEBUG("me4000_ai_read():Copy %d values to user space\n", c);
		k = 2 * c;
		k -= copy_to_user(buffer,
				  ai_context->circ_buf.buf +
				  ai_context->circ_buf.tail, k);
		c = k / 2;
		if (!c) {
			printk(KERN_ERR
			       "ME4000:me4000_ai_read():Cannot copy new values to user\n");
			return -EFAULT;
		}

		ai_context->circ_buf.tail =
		    (ai_context->circ_buf.tail + c) & (ME4000_AI_BUFFER_COUNT -
						       1);
		buffer += c;
		count -= c;
		ret += c;

		spin_lock_irqsave(&ai_context->int_lock, flags);
		if (me4000_buf_space
		    (ai_context->circ_buf, ME4000_AI_BUFFER_COUNT)) {
			tmp = me4000_inl(ai_context->ctrl_reg);

			/* Determine interrupt setup */
			if (ai_context->sample_counter
			    && !ai_context->sample_counter_reload) {
				/* Enable Half Full Interrupt and Sample Counter Interrupt */
				tmp |=
				    ME4000_AI_CTRL_BIT_SC_IRQ |
				    ME4000_AI_CTRL_BIT_HF_IRQ;
			} else if (ai_context->sample_counter
				   && ai_context->sample_counter_reload) {
				if (ai_context->sample_counter <
				    ME4000_AI_FIFO_COUNT / 2) {
					/* Enable only Sample Counter Interrupt */
					tmp |= ME4000_AI_CTRL_BIT_SC_IRQ;
				} else {
					/* Enable Half Full Interrupt and Sample Counter Interrupt */
					tmp |=
					    ME4000_AI_CTRL_BIT_SC_IRQ |
					    ME4000_AI_CTRL_BIT_HF_IRQ;
				}
			} else {
				/* Enable only Half Full Interrupt */
				tmp |= ME4000_AI_CTRL_BIT_HF_IRQ;
			}

			me4000_outl(tmp, ai_context->ctrl_reg);
		}
		spin_unlock_irqrestore(&ai_context->int_lock, flags);
	}

	/* Check if conversion is still running */
	if (!(me4000_inl(ai_context->status_reg) & ME4000_AI_STATUS_BIT_FSM)) {
		printk(KERN_ERR
		       "ME4000:me4000_ai_read():Conversion not running after complete read\n");
		return -EPIPE;
	}

	if (filep->f_flags & O_NONBLOCK)
		return (k == 0) ? -EAGAIN : 2 * ret;

	CALL_PDEBUG("me4000_ai_read() is leaved\n");
	return ret * 2;
}

static unsigned int me4000_ai_poll(struct file *file_p, poll_table *wait)
{
	struct me4000_ai_context *ai_context;
	unsigned long mask = 0;

	CALL_PDEBUG("me4000_ai_poll() is executed\n");

	ai_context = file_p->private_data;

	/* Register wait queue */
	poll_wait(file_p, &ai_context->wait_queue, wait);

	/* Get available values */
	if (me4000_values_to_end(ai_context->circ_buf, ME4000_AI_BUFFER_COUNT))
		mask |= POLLIN | POLLRDNORM;

	PDEBUG("me4000_ai_poll():Return mask %lX\n", mask);

	return mask;
}

static int me4000_ai_offset_enable(struct me4000_ai_context *ai_context)
{
	unsigned long tmp;

	CALL_PDEBUG("me4000_ai_offset_enable() is executed\n");

	tmp = me4000_inl(ai_context->ctrl_reg);
	tmp |= ME4000_AI_CTRL_BIT_OFFSET;
	me4000_outl(tmp, ai_context->ctrl_reg);

	return 0;
}

static int me4000_ai_offset_disable(struct me4000_ai_context *ai_context)
{
	unsigned long tmp;

	CALL_PDEBUG("me4000_ai_offset_disable() is executed\n");

	tmp = me4000_inl(ai_context->ctrl_reg);
	tmp &= ~ME4000_AI_CTRL_BIT_OFFSET;
	me4000_outl(tmp, ai_context->ctrl_reg);

	return 0;
}

static int me4000_ai_fullscale_enable(struct me4000_ai_context *ai_context)
{
	unsigned long tmp;

	CALL_PDEBUG("me4000_ai_fullscale_enable() is executed\n");

	tmp = me4000_inl(ai_context->ctrl_reg);
	tmp |= ME4000_AI_CTRL_BIT_FULLSCALE;
	me4000_outl(tmp, ai_context->ctrl_reg);

	return 0;
}

static int me4000_ai_fullscale_disable(struct me4000_ai_context *ai_context)
{
	unsigned long tmp;

	CALL_PDEBUG("me4000_ai_fullscale_disable() is executed\n");

	tmp = me4000_inl(ai_context->ctrl_reg);
	tmp &= ~ME4000_AI_CTRL_BIT_FULLSCALE;
	me4000_outl(tmp, ai_context->ctrl_reg);

	return 0;
}

static int me4000_ai_fsm_state(int *arg, struct me4000_ai_context *ai_context)
{
	unsigned long tmp;

	CALL_PDEBUG("me4000_ai_fsm_state() is executed\n");

	tmp =
	    (me4000_inl(ai_context->status_reg) & ME4000_AI_STATUS_BIT_FSM) ? 1
	    : 0;

	if (put_user(tmp, arg)) {
		printk(KERN_ERR "me4000_ai_fsm_state():Cannot copy to user\n");
		return -EFAULT;
	}

	return 0;
}

static int me4000_ai_get_count_buffer(unsigned long *arg,
				      struct me4000_ai_context *ai_context)
{
	unsigned long c;
	int err;

	c = me4000_buf_count(ai_context->circ_buf, ME4000_AI_BUFFER_COUNT);

	err = copy_to_user(arg, &c, sizeof(unsigned long));
	if (err) {
		printk(KERN_ERR
		       "%s:Can't copy to user space\n", __func__);
		return -EFAULT;
	}

	return 0;
}

/*---------------------------------- EEPROM stuff ---------------------------*/

static int eeprom_write_cmd(struct me4000_ai_context *ai_context, unsigned long cmd,
			    int length)
{
	int i;
	unsigned long value;

	CALL_PDEBUG("eeprom_write_cmd() is executed\n");

	PDEBUG("eeprom_write_cmd():Write command 0x%08lX with length = %d\n",
	       cmd, length);

	/* Get the ICR register and clear the related bits */
	value = me4000_inl(ai_context->board_info->plx_regbase + PLX_ICR);
	value &= ~(PLX_ICR_MASK_EEPROM);
	me4000_outl(value, ai_context->board_info->plx_regbase + PLX_ICR);

	/* Raise the chip select */
	value |= PLX_ICR_BIT_EEPROM_CHIP_SELECT;
	me4000_outl(value, ai_context->board_info->plx_regbase + PLX_ICR);
	udelay(EEPROM_DELAY);

	for (i = 0; i < length; i++) {
		if (cmd & ((0x1 << (length - 1)) >> i))
			value |= PLX_ICR_BIT_EEPROM_WRITE;
		else
			value &= ~PLX_ICR_BIT_EEPROM_WRITE;

		/* Write to EEPROM */
		me4000_outl(value,
			    ai_context->board_info->plx_regbase + PLX_ICR);
		udelay(EEPROM_DELAY);

		/* Raising edge of the clock */
		value |= PLX_ICR_BIT_EEPROM_CLOCK_SET;
		me4000_outl(value,
			    ai_context->board_info->plx_regbase + PLX_ICR);
		udelay(EEPROM_DELAY);

		/* Falling edge of the clock */
		value &= ~PLX_ICR_BIT_EEPROM_CLOCK_SET;
		me4000_outl(value,
			    ai_context->board_info->plx_regbase + PLX_ICR);
		udelay(EEPROM_DELAY);
	}

	/* Clear the chip select */
	value &= ~PLX_ICR_BIT_EEPROM_CHIP_SELECT;
	me4000_outl(value, ai_context->board_info->plx_regbase + PLX_ICR);
	udelay(EEPROM_DELAY);

	/* Wait until hardware is ready for sure */
	mdelay(10);

	return 0;
}

static unsigned short eeprom_read_cmd(struct me4000_ai_context *ai_context,
				      unsigned long cmd, int length)
{
	int i;
	unsigned long value;
	unsigned short id = 0;

	CALL_PDEBUG("eeprom_read_cmd() is executed\n");

	PDEBUG("eeprom_read_cmd():Read command 0x%08lX with length = %d\n", cmd,
	       length);

	/* Get the ICR register and clear the related bits */
	value = me4000_inl(ai_context->board_info->plx_regbase + PLX_ICR);
	value &= ~(PLX_ICR_MASK_EEPROM);

	me4000_outl(value, ai_context->board_info->plx_regbase + PLX_ICR);

	/* Raise the chip select */
	value |= PLX_ICR_BIT_EEPROM_CHIP_SELECT;
	me4000_outl(value, ai_context->board_info->plx_regbase + PLX_ICR);
	udelay(EEPROM_DELAY);

	/* Write the read command to the eeprom */
	for (i = 0; i < length; i++) {
		if (cmd & ((0x1 << (length - 1)) >> i))
			value |= PLX_ICR_BIT_EEPROM_WRITE;
		else
			value &= ~PLX_ICR_BIT_EEPROM_WRITE;

		me4000_outl(value,
			    ai_context->board_info->plx_regbase + PLX_ICR);
		udelay(EEPROM_DELAY);

		/* Raising edge of the clock */
		value |= PLX_ICR_BIT_EEPROM_CLOCK_SET;
		me4000_outl(value,
			    ai_context->board_info->plx_regbase + PLX_ICR);
		udelay(EEPROM_DELAY);

		/* Falling edge of the clock */
		value &= ~PLX_ICR_BIT_EEPROM_CLOCK_SET;
		me4000_outl(value,
			    ai_context->board_info->plx_regbase + PLX_ICR);
		udelay(EEPROM_DELAY);
	}

	/* Read the value from the eeprom */
	for (i = 0; i < 16; i++) {
		/* Raising edge of the clock */
		value |= PLX_ICR_BIT_EEPROM_CLOCK_SET;
		me4000_outl(value,
			    ai_context->board_info->plx_regbase + PLX_ICR);
		udelay(EEPROM_DELAY);

		if (me4000_inl(ai_context->board_info->plx_regbase + PLX_ICR) &
		    PLX_ICR_BIT_EEPROM_READ) {
			id |= (0x8000 >> i);
			PDEBUG("eeprom_read_cmd():OR with 0x%04X\n",
			       (0x8000 >> i));
		} else {
			PDEBUG("eeprom_read_cmd():Dont't OR\n");
		}

		/* Falling edge of the clock */
		value &= ~PLX_ICR_BIT_EEPROM_CLOCK_SET;
		me4000_outl(value,
			    ai_context->board_info->plx_regbase + PLX_ICR);
		udelay(EEPROM_DELAY);
	}

	/* Clear the chip select */
	value &= ~PLX_ICR_BIT_EEPROM_CHIP_SELECT;
	me4000_outl(value, ai_context->board_info->plx_regbase + PLX_ICR);
	udelay(EEPROM_DELAY);

	return id;
}

static int me4000_eeprom_write(struct me4000_eeprom *arg,
			       struct me4000_ai_context *ai_context)
{
	int err;
	struct me4000_eeprom setup;
	unsigned long cmd;
	unsigned long date_high;
	unsigned long date_low;

	CALL_PDEBUG("me4000_eeprom_write() is executed\n");

	err = copy_from_user(&setup, arg, sizeof(setup));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_eeprom_write():Cannot copy from user\n");
		return err;
	}

	/* Enable writing */
	eeprom_write_cmd(ai_context, ME4000_EEPROM_CMD_WRITE_ENABLE,
			 ME4000_EEPROM_CMD_LENGTH_WRITE_ENABLE);

	/* Command for date */
	date_high = (setup.date & 0xFFFF0000) >> 16;
	date_low = (setup.date & 0x0000FFFF);

	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_DATE_HIGH <<
				       ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
								     (unsigned
								      long)
								     date_high);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_DATE_LOW <<
				       ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
								     (unsigned
								      long)
								     date_low);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	/* Command for unipolar 10V offset */
	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_GAIN_1_UNI_OFFSET <<
				       ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
								     (unsigned
								      long)
								     setup.
								     uni_10_offset);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	/* Command for unipolar 10V fullscale */
	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_GAIN_1_UNI_FULLSCALE <<
				       ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
								     (unsigned
								      long)
								     setup.
								     uni_10_fullscale);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	/* Command for unipolar 2,5V offset */
	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_GAIN_4_UNI_OFFSET <<
				       ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
								     (unsigned
								      long)
								     setup.
								     uni_2_5_offset);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	/* Command for unipolar 2,5V fullscale */
	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_GAIN_4_UNI_FULLSCALE <<
				       ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
								     (unsigned
								      long)
								     setup.
								     uni_2_5_fullscale);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	/* Command for bipolar 10V offset */
	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_GAIN_1_BI_OFFSET <<
				       ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
								     (unsigned
								      long)
								     setup.
								     bi_10_offset);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	/* Command for bipolar 10V fullscale */
	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_GAIN_1_BI_FULLSCALE <<
				       ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
								     (unsigned
								      long)
								     setup.
								     bi_10_fullscale);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	/* Command for bipolar 2,5V offset */
	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_GAIN_4_BI_OFFSET <<
				       ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
								     (unsigned
								      long)
								     setup.
								     bi_2_5_offset);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	/* Command for bipolar 2,5V fullscale */
	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_GAIN_4_BI_FULLSCALE <<
				       ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
								     (unsigned
								      long)
								     setup.
								     bi_2_5_fullscale);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	/* Command for differential 10V offset */
	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_GAIN_1_DIFF_OFFSET <<
				       ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
								     (unsigned
								      long)
								     setup.
								     diff_10_offset);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	/* Command for differential 10V fullscale */
	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_GAIN_1_DIFF_FULLSCALE
				       << ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
									(unsigned
									 long)
									setup.
									diff_10_fullscale);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	/* Command for differential 2,5V offset */
	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_GAIN_4_DIFF_OFFSET <<
				       ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
								     (unsigned
								      long)
								     setup.
								     diff_2_5_offset);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	/* Command for differential 2,5V fullscale */
	cmd =
	    ME4000_EEPROM_CMD_WRITE | (ME4000_EEPROM_ADR_GAIN_4_DIFF_FULLSCALE
				       << ME4000_EEPROM_DATA_LENGTH) | (0xFFFF &
									(unsigned
									 long)
									setup.
									diff_2_5_fullscale);
	err = eeprom_write_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_WRITE);
	if (err)
		return err;

	/* Disable writing */
	eeprom_write_cmd(ai_context, ME4000_EEPROM_CMD_WRITE_DISABLE,
			 ME4000_EEPROM_CMD_LENGTH_WRITE_DISABLE);

	return 0;
}

static int me4000_eeprom_read(struct me4000_eeprom *arg,
			      struct me4000_ai_context *ai_context)
{
	int err;
	unsigned long cmd;
	struct me4000_eeprom setup;

	CALL_PDEBUG("me4000_eeprom_read() is executed\n");

	/* Command for date */
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_DATE_HIGH;
	setup.date =
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);
	setup.date <<= 16;
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_DATE_LOW;
	setup.date |=
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);

	/* Command for unipolar 10V offset */
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_GAIN_1_UNI_OFFSET;
	setup.uni_10_offset =
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);

	/* Command for unipolar 10V fullscale */
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_GAIN_1_UNI_FULLSCALE;
	setup.uni_10_fullscale =
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);

	/* Command for unipolar 2,5V offset */
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_GAIN_4_UNI_OFFSET;
	setup.uni_2_5_offset =
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);

	/* Command for unipolar 2,5V fullscale */
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_GAIN_4_UNI_FULLSCALE;
	setup.uni_2_5_fullscale =
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);

	/* Command for bipolar 10V offset */
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_GAIN_1_BI_OFFSET;
	setup.bi_10_offset =
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);

	/* Command for bipolar 10V fullscale */
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_GAIN_1_BI_FULLSCALE;
	setup.bi_10_fullscale =
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);

	/* Command for bipolar 2,5V offset */
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_GAIN_4_BI_OFFSET;
	setup.bi_2_5_offset =
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);

	/* Command for bipolar 2,5V fullscale */
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_GAIN_4_BI_FULLSCALE;
	setup.bi_2_5_fullscale =
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);

	/* Command for differntial 10V offset */
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_GAIN_1_DIFF_OFFSET;
	setup.diff_10_offset =
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);

	/* Command for differential 10V fullscale */
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_GAIN_1_DIFF_FULLSCALE;
	setup.diff_10_fullscale =
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);

	/* Command for differntial 2,5V offset */
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_GAIN_4_DIFF_OFFSET;
	setup.diff_2_5_offset =
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);

	/* Command for differential 2,5V fullscale */
	cmd = ME4000_EEPROM_CMD_READ | ME4000_EEPROM_ADR_GAIN_4_DIFF_FULLSCALE;
	setup.diff_2_5_fullscale =
	    eeprom_read_cmd(ai_context, cmd, ME4000_EEPROM_CMD_LENGTH_READ);

	err = copy_to_user(arg, &setup, sizeof(setup));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_eeprom_read():Cannot copy to user\n");
		return err;
	}

	return 0;
}

/*------------------------------------ DIO stuff ----------------------------------------------*/

static int me4000_dio_ioctl(struct inode *inode_p, struct file *file_p,
			    unsigned int service, unsigned long arg)
{
	struct me4000_dio_context *dio_context;

	CALL_PDEBUG("me4000_dio_ioctl() is executed\n");

	dio_context = file_p->private_data;

	if (_IOC_TYPE(service) != ME4000_MAGIC) {
		printk(KERN_ERR "me4000_dio_ioctl():Wrong magic number\n");
		return -ENOTTY;
	}
	if (_IOC_NR(service) > ME4000_IOCTL_MAXNR) {
		printk(KERN_ERR "me4000_dio_ioctl():Service number to high\n");
		return -ENOTTY;
	}

	switch (service) {
	case ME4000_DIO_CONFIG:
		return me4000_dio_config((struct me4000_dio_config *)arg,
					 dio_context);
	case ME4000_DIO_SET_BYTE:
		return me4000_dio_set_byte((struct me4000_dio_byte *)arg,
					   dio_context);
	case ME4000_DIO_GET_BYTE:
		return me4000_dio_get_byte((struct me4000_dio_byte *)arg,
					   dio_context);
	case ME4000_DIO_RESET:
		return me4000_dio_reset(dio_context);
	default:
		printk(KERN_ERR
		       "ME4000:me4000_dio_ioctl():Invalid service number %d\n",
		       service);
		return -ENOTTY;
	}
	return 0;
}

static int me4000_dio_config(struct me4000_dio_config *arg,
			     struct me4000_dio_context *dio_context)
{
	struct me4000_dio_config cmd;
	u32 tmp;
	int err;

	CALL_PDEBUG("me4000_dio_config() is executed\n");

	/* Copy data from user */
	err = copy_from_user(&cmd, arg, sizeof(struct me4000_dio_config));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_dio_config():Can't copy from user space\n");
		return -EFAULT;
	}

	/* Check port parameter */
	if (cmd.port >= dio_context->dio_count) {
		printk(KERN_ERR
		       "ME4000:me4000_dio_config():Port %d is not available\n",
		       cmd.port);
		return -EINVAL;
	}

	PDEBUG("me4000_dio_config(): port %d, mode %d, function %d\n", cmd.port,
	       cmd.mode, cmd.function);

	if (cmd.port == ME4000_DIO_PORT_A) {
		if (cmd.mode == ME4000_DIO_PORT_INPUT) {
			/* Check if opto isolated version */
			if (!(me4000_inl(dio_context->dir_reg) & 0x1)) {
				printk(KERN_ERR
				       "ME4000:me4000_dio_config():Cannot set to input on opto isolated versions\n");
				return -EIO;
			}

			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp &=
			    ~(ME4000_DIO_CTRL_BIT_MODE_0 |
			      ME4000_DIO_CTRL_BIT_MODE_1);
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else if (cmd.mode == ME4000_DIO_PORT_OUTPUT) {
			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp &=
			    ~(ME4000_DIO_CTRL_BIT_MODE_0 |
			      ME4000_DIO_CTRL_BIT_MODE_1);
			tmp |= ME4000_DIO_CTRL_BIT_MODE_0;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else if (cmd.mode == ME4000_DIO_FIFO_LOW) {
			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp &=
			    ~(ME4000_DIO_CTRL_BIT_MODE_0 |
			      ME4000_DIO_CTRL_BIT_MODE_1 |
			      ME4000_DIO_CTRL_BIT_FIFO_HIGH_0);
			tmp |=
			    ME4000_DIO_CTRL_BIT_MODE_0 |
			    ME4000_DIO_CTRL_BIT_MODE_1;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else if (cmd.mode == ME4000_DIO_FIFO_HIGH) {
			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp |=
			    ME4000_DIO_CTRL_BIT_MODE_0 |
			    ME4000_DIO_CTRL_BIT_MODE_1 |
			    ME4000_DIO_CTRL_BIT_FIFO_HIGH_0;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else {
			printk(KERN_ERR
			       "ME4000:me4000_dio_config():Mode %d is not available\n",
			       cmd.mode);
			return -EINVAL;
		}
	} else if (cmd.port == ME4000_DIO_PORT_B) {
		if (cmd.mode == ME4000_DIO_PORT_INPUT) {
			/* Only do anything when TTL version is installed */
			if ((me4000_inl(dio_context->dir_reg) & 0x1)) {
				tmp = me4000_inl(dio_context->ctrl_reg);
				tmp &=
				    ~(ME4000_DIO_CTRL_BIT_MODE_2 |
				      ME4000_DIO_CTRL_BIT_MODE_3);
				me4000_outl(tmp, dio_context->ctrl_reg);
			}
		} else if (cmd.mode == ME4000_DIO_PORT_OUTPUT) {
			/* Check if opto isolated version */
			if (!(me4000_inl(dio_context->dir_reg) & 0x1)) {
				printk(KERN_ERR
				       "ME4000:me4000_dio_config():Cannot set to output on opto isolated versions\n");
				return -EIO;
			}

			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp &=
			    ~(ME4000_DIO_CTRL_BIT_MODE_2 |
			      ME4000_DIO_CTRL_BIT_MODE_3);
			tmp |= ME4000_DIO_CTRL_BIT_MODE_2;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else if (cmd.mode == ME4000_DIO_FIFO_LOW) {
			/* Check if opto isolated version */
			if (!(me4000_inl(dio_context->dir_reg) & 0x1)) {
				printk(KERN_ERR
				       "ME4000:me4000_dio_config():Cannot set to FIFO low output on opto isolated versions\n");
				return -EIO;
			}

			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp &=
			    ~(ME4000_DIO_CTRL_BIT_MODE_2 |
			      ME4000_DIO_CTRL_BIT_MODE_3 |
			      ME4000_DIO_CTRL_BIT_FIFO_HIGH_1);
			tmp |=
			    ME4000_DIO_CTRL_BIT_MODE_2 |
			    ME4000_DIO_CTRL_BIT_MODE_3;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else if (cmd.mode == ME4000_DIO_FIFO_HIGH) {
			/* Check if opto isolated version */
			if (!(me4000_inl(dio_context->dir_reg) & 0x1)) {
				printk(KERN_ERR
				       "ME4000:me4000_dio_config():Cannot set to FIFO high output on opto isolated versions\n");
				return -EIO;
			}

			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp |=
			    ME4000_DIO_CTRL_BIT_MODE_2 |
			    ME4000_DIO_CTRL_BIT_MODE_3 |
			    ME4000_DIO_CTRL_BIT_FIFO_HIGH_1;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else {
			printk(KERN_ERR
			       "ME4000:me4000_dio_config():Mode %d is not available\n",
			       cmd.mode);
			return -EINVAL;
		}
	} else if (cmd.port == ME4000_DIO_PORT_C) {
		if (cmd.mode == ME4000_DIO_PORT_INPUT) {
			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp &=
			    ~(ME4000_DIO_CTRL_BIT_MODE_4 |
			      ME4000_DIO_CTRL_BIT_MODE_5);
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else if (cmd.mode == ME4000_DIO_PORT_OUTPUT) {
			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp &=
			    ~(ME4000_DIO_CTRL_BIT_MODE_4 |
			      ME4000_DIO_CTRL_BIT_MODE_5);
			tmp |= ME4000_DIO_CTRL_BIT_MODE_4;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else if (cmd.mode == ME4000_DIO_FIFO_LOW) {
			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp &=
			    ~(ME4000_DIO_CTRL_BIT_MODE_4 |
			      ME4000_DIO_CTRL_BIT_MODE_5 |
			      ME4000_DIO_CTRL_BIT_FIFO_HIGH_2);
			tmp |=
			    ME4000_DIO_CTRL_BIT_MODE_4 |
			    ME4000_DIO_CTRL_BIT_MODE_5;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else if (cmd.mode == ME4000_DIO_FIFO_HIGH) {
			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp |=
			    ME4000_DIO_CTRL_BIT_MODE_4 |
			    ME4000_DIO_CTRL_BIT_MODE_5 |
			    ME4000_DIO_CTRL_BIT_FIFO_HIGH_2;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else {
			printk(KERN_ERR
			       "ME4000:me4000_dio_config():Mode %d is not available\n",
			       cmd.mode);
			return -EINVAL;
		}
	} else if (cmd.port == ME4000_DIO_PORT_D) {
		if (cmd.mode == ME4000_DIO_PORT_INPUT) {
			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp &=
			    ~(ME4000_DIO_CTRL_BIT_MODE_6 |
			      ME4000_DIO_CTRL_BIT_MODE_7);
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else if (cmd.mode == ME4000_DIO_PORT_OUTPUT) {
			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp &=
			    ~(ME4000_DIO_CTRL_BIT_MODE_6 |
			      ME4000_DIO_CTRL_BIT_MODE_7);
			tmp |= ME4000_DIO_CTRL_BIT_MODE_6;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else if (cmd.mode == ME4000_DIO_FIFO_LOW) {
			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp &=
			    ~(ME4000_DIO_CTRL_BIT_MODE_6 |
			      ME4000_DIO_CTRL_BIT_MODE_7 |
			      ME4000_DIO_CTRL_BIT_FIFO_HIGH_3);
			tmp |=
			    ME4000_DIO_CTRL_BIT_MODE_6 |
			    ME4000_DIO_CTRL_BIT_MODE_7;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else if (cmd.mode == ME4000_DIO_FIFO_HIGH) {
			tmp = me4000_inl(dio_context->ctrl_reg);
			tmp |=
			    ME4000_DIO_CTRL_BIT_MODE_6 |
			    ME4000_DIO_CTRL_BIT_MODE_7 |
			    ME4000_DIO_CTRL_BIT_FIFO_HIGH_3;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else {
			printk(KERN_ERR
			       "ME4000:me4000_dio_config():Mode %d is not available\n",
			       cmd.mode);
			return -EINVAL;
		}
	} else {
		printk(KERN_ERR
		       "ME4000:me4000_dio_config():Port %d is not available\n",
		       cmd.port);
		return -EINVAL;
	}

	PDEBUG("me4000_dio_config(): port %d, mode %d, function %d\n", cmd.port,
	       cmd.mode, cmd.function);

	if ((cmd.mode == ME4000_DIO_FIFO_HIGH)
	    || (cmd.mode == ME4000_DIO_FIFO_LOW)) {
		tmp = me4000_inl(dio_context->ctrl_reg);
		tmp &=
		    ~(ME4000_DIO_CTRL_BIT_FUNCTION_0 |
		      ME4000_DIO_CTRL_BIT_FUNCTION_1);
		if (cmd.function == ME4000_DIO_FUNCTION_PATTERN) {
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else if (cmd.function == ME4000_DIO_FUNCTION_DEMUX) {
			tmp |= ME4000_DIO_CTRL_BIT_FUNCTION_0;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else if (cmd.function == ME4000_DIO_FUNCTION_MUX) {
			tmp |= ME4000_DIO_CTRL_BIT_FUNCTION_1;
			me4000_outl(tmp, dio_context->ctrl_reg);
		} else {
			printk(KERN_ERR
			       "ME4000:me4000_dio_config():Invalid port function specified\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int me4000_dio_set_byte(struct me4000_dio_byte *arg,
			       struct me4000_dio_context *dio_context)
{
	struct me4000_dio_byte cmd;
	int err;

	CALL_PDEBUG("me4000_dio_set_byte() is executed\n");

	/* Copy data from user */
	err = copy_from_user(&cmd, arg, sizeof(struct me4000_dio_byte));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_dio_set_byte():Can't copy from user space\n");
		return -EFAULT;
	}

	/* Check port parameter */
	if (cmd.port >= dio_context->dio_count) {
		printk(KERN_ERR
		       "ME4000:me4000_dio_set_byte():Port %d is not available\n",
		       cmd.port);
		return -EINVAL;
	}

	if (cmd.port == ME4000_DIO_PORT_A) {
		if ((me4000_inl(dio_context->ctrl_reg) & 0x3) != 0x1) {
			printk(KERN_ERR
			       "ME4000:me4000_dio_set_byte():Port %d is not in output mode\n",
			       cmd.port);
			return -EIO;
		}
		me4000_outl(cmd.byte, dio_context->port_0_reg);
	} else if (cmd.port == ME4000_DIO_PORT_B) {
		if ((me4000_inl(dio_context->ctrl_reg) & 0xC) != 0x4) {
			printk(KERN_ERR
			       "ME4000:me4000_dio_set_byte():Port %d is not in output mode\n",
			       cmd.port);
			return -EIO;
		}
		me4000_outl(cmd.byte, dio_context->port_1_reg);
	} else if (cmd.port == ME4000_DIO_PORT_C) {
		if ((me4000_inl(dio_context->ctrl_reg) & 0x30) != 0x10) {
			printk(KERN_ERR
			       "ME4000:me4000_dio_set_byte():Port %d is not in output mode\n",
			       cmd.port);
			return -EIO;
		}
		me4000_outl(cmd.byte, dio_context->port_2_reg);
	} else if (cmd.port == ME4000_DIO_PORT_D) {
		if ((me4000_inl(dio_context->ctrl_reg) & 0xC0) != 0x40) {
			printk(KERN_ERR
			       "ME4000:me4000_dio_set_byte():Port %d is not in output mode\n",
			       cmd.port);
			return -EIO;
		}
		me4000_outl(cmd.byte, dio_context->port_3_reg);
	} else {
		printk(KERN_ERR
		       "ME4000:me4000_dio_set_byte():Port %d is not available\n",
		       cmd.port);
		return -EINVAL;
	}

	return 0;
}

static int me4000_dio_get_byte(struct me4000_dio_byte *arg,
			       struct me4000_dio_context *dio_context)
{
	struct me4000_dio_byte cmd;
	int err;

	CALL_PDEBUG("me4000_dio_get_byte() is executed\n");

	/* Copy data from user */
	err = copy_from_user(&cmd, arg, sizeof(struct me4000_dio_byte));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_dio_get_byte():Can't copy from user space\n");
		return -EFAULT;
	}

	/* Check port parameter */
	if (cmd.port >= dio_context->dio_count) {
		printk(KERN_ERR
		       "ME4000:me4000_dio_get_byte():Port %d is not available\n",
		       cmd.port);
		return -EINVAL;
	}

	if (cmd.port == ME4000_DIO_PORT_A) {
		cmd.byte = me4000_inl(dio_context->port_0_reg) & 0xFF;
	} else if (cmd.port == ME4000_DIO_PORT_B) {
		cmd.byte = me4000_inl(dio_context->port_1_reg) & 0xFF;
	} else if (cmd.port == ME4000_DIO_PORT_C) {
		cmd.byte = me4000_inl(dio_context->port_2_reg) & 0xFF;
	} else if (cmd.port == ME4000_DIO_PORT_D) {
		cmd.byte = me4000_inl(dio_context->port_3_reg) & 0xFF;
	} else {
		printk(KERN_ERR
		       "ME4000:me4000_dio_get_byte():Port %d is not available\n",
		       cmd.port);
		return -EINVAL;
	}

	/* Copy result back to user */
	err = copy_to_user(arg, &cmd, sizeof(struct me4000_dio_byte));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_dio_get_byte():Can't copy to user space\n");
		return -EFAULT;
	}

	return 0;
}

static int me4000_dio_reset(struct me4000_dio_context *dio_context)
{
	CALL_PDEBUG("me4000_dio_reset() is executed\n");

	/* Clear the control register */
	me4000_outl(0, dio_context->ctrl_reg);

	/* Check for opto isolated version */
	if (!(me4000_inl(dio_context->dir_reg) & 0x1)) {
		me4000_outl(0x1, dio_context->ctrl_reg);
		me4000_outl(0x0, dio_context->port_0_reg);
	}

	return 0;
}

/*------------------------------------ COUNTER STUFF ------------------------------------*/

static int me4000_cnt_ioctl(struct inode *inode_p, struct file *file_p,
			    unsigned int service, unsigned long arg)
{
	struct me4000_cnt_context *cnt_context;

	CALL_PDEBUG("me4000_cnt_ioctl() is executed\n");

	cnt_context = file_p->private_data;

	if (_IOC_TYPE(service) != ME4000_MAGIC) {
		printk(KERN_ERR "me4000_dio_ioctl():Wrong magic number\n");
		return -ENOTTY;
	}
	if (_IOC_NR(service) > ME4000_IOCTL_MAXNR) {
		printk(KERN_ERR "me4000_dio_ioctl():Service number to high\n");
		return -ENOTTY;
	}

	switch (service) {
	case ME4000_CNT_READ:
		return me4000_cnt_read((struct me4000_cnt *)arg, cnt_context);
	case ME4000_CNT_WRITE:
		return me4000_cnt_write((struct me4000_cnt *)arg, cnt_context);
	case ME4000_CNT_CONFIG:
		return me4000_cnt_config((struct me4000_cnt_config *)arg,
					 cnt_context);
	case ME4000_CNT_RESET:
		return me4000_cnt_reset(cnt_context);
	default:
		printk(KERN_ERR
		       "ME4000:me4000_dio_ioctl():Invalid service number %d\n",
		       service);
		return -ENOTTY;
	}
	return 0;
}

static int me4000_cnt_config(struct me4000_cnt_config *arg,
			     struct me4000_cnt_context *cnt_context)
{
	struct me4000_cnt_config cmd;
	u8 counter;
	u8 mode;
	int err;

	CALL_PDEBUG("me4000_cnt_config() is executed\n");

	/* Copy data from user */
	err = copy_from_user(&cmd, arg, sizeof(struct me4000_cnt_config));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_cnt_config():Can't copy from user space\n");
		return -EFAULT;
	}

	/* Check counter parameter */
	switch (cmd.counter) {
	case ME4000_CNT_COUNTER_0:
		counter = ME4000_CNT_CTRL_BIT_COUNTER_0;
		break;
	case ME4000_CNT_COUNTER_1:
		counter = ME4000_CNT_CTRL_BIT_COUNTER_1;
		break;
	case ME4000_CNT_COUNTER_2:
		counter = ME4000_CNT_CTRL_BIT_COUNTER_2;
		break;
	default:
		printk(KERN_ERR
		       "ME4000:me4000_cnt_config():Counter %d is not available\n",
		       cmd.counter);
		return -EINVAL;
	}

	/* Check mode parameter */
	switch (cmd.mode) {
	case ME4000_CNT_MODE_0:
		mode = ME4000_CNT_CTRL_BIT_MODE_0;
		break;
	case ME4000_CNT_MODE_1:
		mode = ME4000_CNT_CTRL_BIT_MODE_1;
		break;
	case ME4000_CNT_MODE_2:
		mode = ME4000_CNT_CTRL_BIT_MODE_2;
		break;
	case ME4000_CNT_MODE_3:
		mode = ME4000_CNT_CTRL_BIT_MODE_3;
		break;
	case ME4000_CNT_MODE_4:
		mode = ME4000_CNT_CTRL_BIT_MODE_4;
		break;
	case ME4000_CNT_MODE_5:
		mode = ME4000_CNT_CTRL_BIT_MODE_5;
		break;
	default:
		printk(KERN_ERR
		       "ME4000:me4000_cnt_config():Mode %d is not available\n",
		       cmd.mode);
		return -EINVAL;
	}

	/* Write the control word */
	me4000_outb((counter | mode | 0x30), cnt_context->ctrl_reg);

	return 0;
}

static int me4000_cnt_read(struct me4000_cnt *arg,
			   struct me4000_cnt_context *cnt_context)
{
	struct me4000_cnt cmd;
	u8 tmp;
	int err;

	CALL_PDEBUG("me4000_cnt_read() is executed\n");

	/* Copy data from user */
	err = copy_from_user(&cmd, arg, sizeof(struct me4000_cnt));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_cnt_read():Can't copy from user space\n");
		return -EFAULT;
	}

	/* Read counter */
	switch (cmd.counter) {
	case ME4000_CNT_COUNTER_0:
		tmp = me4000_inb(cnt_context->counter_0_reg);
		cmd.value = tmp;
		tmp = me4000_inb(cnt_context->counter_0_reg);
		cmd.value |= ((u16) tmp) << 8;
		break;
	case ME4000_CNT_COUNTER_1:
		tmp = me4000_inb(cnt_context->counter_1_reg);
		cmd.value = tmp;
		tmp = me4000_inb(cnt_context->counter_1_reg);
		cmd.value |= ((u16) tmp) << 8;
		break;
	case ME4000_CNT_COUNTER_2:
		tmp = me4000_inb(cnt_context->counter_2_reg);
		cmd.value = tmp;
		tmp = me4000_inb(cnt_context->counter_2_reg);
		cmd.value |= ((u16) tmp) << 8;
		break;
	default:
		printk(KERN_ERR
		       "ME4000:me4000_cnt_read():Counter %d is not available\n",
		       cmd.counter);
		return -EINVAL;
	}

	/* Copy result back to user */
	err = copy_to_user(arg, &cmd, sizeof(struct me4000_cnt));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_cnt_read():Can't copy to user space\n");
		return -EFAULT;
	}

	return 0;
}

static int me4000_cnt_write(struct me4000_cnt *arg,
			    struct me4000_cnt_context *cnt_context)
{
	struct me4000_cnt cmd;
	u8 tmp;
	int err;

	CALL_PDEBUG("me4000_cnt_write() is executed\n");

	/* Copy data from user */
	err = copy_from_user(&cmd, arg, sizeof(struct me4000_cnt));
	if (err) {
		printk(KERN_ERR
		       "ME4000:me4000_cnt_write():Can't copy from user space\n");
		return -EFAULT;
	}

	/* Write counter */
	switch (cmd.counter) {
	case ME4000_CNT_COUNTER_0:
		tmp = cmd.value & 0xFF;
		me4000_outb(tmp, cnt_context->counter_0_reg);
		tmp = (cmd.value >> 8) & 0xFF;
		me4000_outb(tmp, cnt_context->counter_0_reg);
		break;
	case ME4000_CNT_COUNTER_1:
		tmp = cmd.value & 0xFF;
		me4000_outb(tmp, cnt_context->counter_1_reg);
		tmp = (cmd.value >> 8) & 0xFF;
		me4000_outb(tmp, cnt_context->counter_1_reg);
		break;
	case ME4000_CNT_COUNTER_2:
		tmp = cmd.value & 0xFF;
		me4000_outb(tmp, cnt_context->counter_2_reg);
		tmp = (cmd.value >> 8) & 0xFF;
		me4000_outb(tmp, cnt_context->counter_2_reg);
		break;
	default:
		printk(KERN_ERR
		       "ME4000:me4000_cnt_write():Counter %d is not available\n",
		       cmd.counter);
		return -EINVAL;
	}

	return 0;
}

static int me4000_cnt_reset(struct me4000_cnt_context *cnt_context)
{
	CALL_PDEBUG("me4000_cnt_reset() is executed\n");

	/* Set the mode and value for counter 0 */
	me4000_outb(0x30, cnt_context->ctrl_reg);
	me4000_outb(0x00, cnt_context->counter_0_reg);
	me4000_outb(0x00, cnt_context->counter_0_reg);

	/* Set the mode and value for counter 1 */
	me4000_outb(0x70, cnt_context->ctrl_reg);
	me4000_outb(0x00, cnt_context->counter_1_reg);
	me4000_outb(0x00, cnt_context->counter_1_reg);

	/* Set the mode and value for counter 2 */
	me4000_outb(0xB0, cnt_context->ctrl_reg);
	me4000_outb(0x00, cnt_context->counter_2_reg);
	me4000_outb(0x00, cnt_context->counter_2_reg);

	return 0;
}

/*------------------------------------ External Interrupt stuff ------------------------------------*/

static int me4000_ext_int_ioctl(struct inode *inode_p, struct file *file_p,
				unsigned int service, unsigned long arg)
{
	struct me4000_ext_int_context *ext_int_context;

	CALL_PDEBUG("me4000_ext_int_ioctl() is executed\n");

	ext_int_context = file_p->private_data;

	if (_IOC_TYPE(service) != ME4000_MAGIC) {
		printk(KERN_ERR "me4000_ext_int_ioctl():Wrong magic number\n");
		return -ENOTTY;
	}
	if (_IOC_NR(service) > ME4000_IOCTL_MAXNR) {
		printk(KERN_ERR
		       "me4000_ext_int_ioctl():Service number to high\n");
		return -ENOTTY;
	}

	switch (service) {
	case ME4000_EXT_INT_ENABLE:
		return me4000_ext_int_enable(ext_int_context);
	case ME4000_EXT_INT_DISABLE:
		return me4000_ext_int_disable(ext_int_context);
	case ME4000_EXT_INT_COUNT:
		return me4000_ext_int_count((unsigned long *)arg,
					    ext_int_context);
	default:
		printk(KERN_ERR
		       "ME4000:me4000_ext_int_ioctl():Invalid service number %d\n",
		       service);
		return -ENOTTY;
	}
	return 0;
}

static int me4000_ext_int_enable(struct me4000_ext_int_context *ext_int_context)
{
	unsigned long tmp;

	CALL_PDEBUG("me4000_ext_int_enable() is executed\n");

	tmp = me4000_inl(ext_int_context->ctrl_reg);
	tmp |= ME4000_AI_CTRL_BIT_EX_IRQ;
	me4000_outl(tmp, ext_int_context->ctrl_reg);

	return 0;
}

static int me4000_ext_int_disable(struct me4000_ext_int_context *ext_int_context)
{
	unsigned long tmp;

	CALL_PDEBUG("me4000_ext_int_disable() is executed\n");

	tmp = me4000_inl(ext_int_context->ctrl_reg);
	tmp &= ~ME4000_AI_CTRL_BIT_EX_IRQ;
	me4000_outl(tmp, ext_int_context->ctrl_reg);

	return 0;
}

static int me4000_ext_int_count(unsigned long *arg,
				struct me4000_ext_int_context *ext_int_context)
{

	CALL_PDEBUG("me4000_ext_int_count() is executed\n");

	put_user(ext_int_context->int_count, arg);
	return 0;
}

/*------------------------------------ General stuff ------------------------------------*/

static int me4000_get_user_info(struct me4000_user_info *arg,
				struct me4000_info *board_info)
{
	struct me4000_user_info user_info;

	CALL_PDEBUG("me4000_get_user_info() is executed\n");

	user_info.board_count = board_info->board_count;
	user_info.plx_regbase = board_info->plx_regbase;
	user_info.plx_regbase_size = board_info->plx_regbase_size;
	user_info.me4000_regbase = board_info->me4000_regbase;
	user_info.me4000_regbase_size = board_info->me4000_regbase_size;
	user_info.serial_no = board_info->serial_no;
	user_info.hw_revision = board_info->hw_revision;
	user_info.vendor_id = board_info->vendor_id;
	user_info.device_id = board_info->device_id;
	user_info.pci_bus_no = board_info->pci_bus_no;
	user_info.pci_dev_no = board_info->pci_dev_no;
	user_info.pci_func_no = board_info->pci_func_no;
	user_info.irq = board_info->irq;
	user_info.irq_count = board_info->irq_count;
	user_info.driver_version = ME4000_DRIVER_VERSION;
	user_info.ao_count = board_info->board_p->ao.count;
	user_info.ao_fifo_count = board_info->board_p->ao.fifo_count;

	user_info.ai_count = board_info->board_p->ai.count;
	user_info.ai_sh_count = board_info->board_p->ai.sh_count;
	user_info.ai_ex_trig_analog = board_info->board_p->ai.ex_trig_analog;

	user_info.dio_count = board_info->board_p->dio.count;

	user_info.cnt_count = board_info->board_p->cnt.count;

	if (copy_to_user(arg, &user_info, sizeof(struct me4000_user_info)))
		return -EFAULT;

	return 0;
}

/*------------------------------------ ISR STUFF ------------------------------------*/

static int me4000_ext_int_fasync(int fd, struct file *file_ptr, int mode)
{
	int result = 0;
	struct me4000_ext_int_context *ext_int_context;

	CALL_PDEBUG("me4000_ext_int_fasync() is executed\n");

	ext_int_context = file_ptr->private_data;

	result =
	    fasync_helper(fd, file_ptr, mode, &ext_int_context->fasync_ptr);

	CALL_PDEBUG("me4000_ext_int_fasync() is leaved\n");
	return result;
}

static irqreturn_t me4000_ao_isr(int irq, void *dev_id)
{
	u32 tmp;
	u32 value;
	struct me4000_ao_context *ao_context;
	int i;
	int c = 0;
	int c1 = 0;

	ISR_PDEBUG("me4000_ao_isr() is executed\n");

	ao_context = dev_id;

	/* Check if irq number is right */
	if (irq != ao_context->irq) {
		ISR_PDEBUG("me4000_ao_isr():incorrect interrupt num: %d\n",
			   irq);
		return IRQ_NONE;
	}

	/* Check if this DAC rised an interrupt */
	if (!
	    ((0x1 << (ao_context->index + 3)) &
	     me4000_inl(ao_context->irq_status_reg))) {
		ISR_PDEBUG("me4000_ao_isr():Not this DAC\n");
		return IRQ_NONE;
	}

	/* Read status register to find out what happened */
	tmp = me4000_inl(ao_context->status_reg);

	if (!(tmp & ME4000_AO_STATUS_BIT_EF) && (tmp & ME4000_AO_STATUS_BIT_HF)
	    && (tmp & ME4000_AO_STATUS_BIT_HF)) {
		c = ME4000_AO_FIFO_COUNT;
		ISR_PDEBUG("me4000_ao_isr():Fifo empty\n");
	} else if ((tmp & ME4000_AO_STATUS_BIT_EF)
		   && (tmp & ME4000_AO_STATUS_BIT_HF)
		   && (tmp & ME4000_AO_STATUS_BIT_HF)) {
		c = ME4000_AO_FIFO_COUNT / 2;
		ISR_PDEBUG("me4000_ao_isr():Fifo under half full\n");
	} else {
		c = 0;
		ISR_PDEBUG("me4000_ao_isr():Fifo full\n");
	}

	ISR_PDEBUG("me4000_ao_isr():Try to write 0x%04X values\n", c);

	while (1) {
		c1 = me4000_values_to_end(ao_context->circ_buf,
					  ME4000_AO_BUFFER_COUNT);
		ISR_PDEBUG("me4000_ao_isr():Values to end = %d\n", c1);
		if (c1 > c)
			c1 = c;

		if (c1 <= 0) {
			ISR_PDEBUG
			    ("me4000_ao_isr():Work done or buffer empty\n");
			break;
		}
		if (((ao_context->fifo_reg & 0xFF) == ME4000_AO_01_FIFO_REG) ||
		    ((ao_context->fifo_reg & 0xFF) == ME4000_AO_03_FIFO_REG)) {
			for (i = 0; i < c1; i++) {
				value =
				    ((u32)
				     (*
				      (ao_context->circ_buf.buf +
				       ao_context->circ_buf.tail + i))) << 16;
				outl(value, ao_context->fifo_reg);
			}
		} else
			outsw(ao_context->fifo_reg,
			      ao_context->circ_buf.buf +
			      ao_context->circ_buf.tail, c1);


		ao_context->circ_buf.tail =
		    (ao_context->circ_buf.tail + c1) & (ME4000_AO_BUFFER_COUNT -
							1);
		ISR_PDEBUG("me4000_ao_isr():%d values wrote to port 0x%04X\n",
			   c1, ao_context->fifo_reg);
		c -= c1;
	}

	/* If there are no values left in the buffer, disable interrupts */
	spin_lock(&ao_context->int_lock);
	if (!me4000_buf_count(ao_context->circ_buf, ME4000_AO_BUFFER_COUNT)) {
		ISR_PDEBUG
		    ("me4000_ao_isr():Disable Interrupt because no values left in buffer\n");
		tmp = me4000_inl(ao_context->ctrl_reg);
		tmp &= ~ME4000_AO_CTRL_BIT_ENABLE_IRQ;
		me4000_outl(tmp, ao_context->ctrl_reg);
	}
	spin_unlock(&ao_context->int_lock);

	/* Reset the interrupt */
	spin_lock(&ao_context->int_lock);
	tmp = me4000_inl(ao_context->ctrl_reg);
	tmp |= ME4000_AO_CTRL_BIT_RESET_IRQ;
	me4000_outl(tmp, ao_context->ctrl_reg);
	tmp &= ~ME4000_AO_CTRL_BIT_RESET_IRQ;
	me4000_outl(tmp, ao_context->ctrl_reg);

	/* If state machine is stopped, flow was interrupted */
	if (!(me4000_inl(ao_context->status_reg) & ME4000_AO_STATUS_BIT_FSM)) {
		printk(KERN_ERR "ME4000:me4000_ao_isr():Broken pipe\n");
		/* Set flag in order to inform write routine */
		ao_context->pipe_flag = 1;
		/* Disable interrupt */
		tmp &= ~ME4000_AO_CTRL_BIT_ENABLE_IRQ;
	}
	me4000_outl(tmp, ao_context->ctrl_reg);
	spin_unlock(&ao_context->int_lock);

	/* Wake up waiting process */
	wake_up_interruptible(&(ao_context->wait_queue));

	/* Count the interrupt */
	ao_context->board_info->irq_count++;

	return IRQ_HANDLED;
}

static irqreturn_t me4000_ai_isr(int irq, void *dev_id)
{
	u32 tmp;
	struct me4000_ai_context *ai_context;
	int i;
	int c = 0;
	int c1 = 0;
#ifdef ME4000_ISR_DEBUG
	unsigned long before;
	unsigned long after;
#endif

	ISR_PDEBUG("me4000_ai_isr() is executed\n");

#ifdef ME4000_ISR_DEBUG
	rdtscl(before);
#endif

	ai_context = dev_id;

	/* Check if irq number is right */
	if (irq != ai_context->irq) {
		ISR_PDEBUG("me4000_ai_isr():incorrect interrupt num: %d\n",
			   irq);
		return IRQ_NONE;
	}

	if (me4000_inl(ai_context->irq_status_reg) &
	    ME4000_IRQ_STATUS_BIT_AI_HF) {
		ISR_PDEBUG
		    ("me4000_ai_isr():Fifo half full interrupt occured\n");

		/* Read status register to find out what happened */
		tmp = me4000_inl(ai_context->ctrl_reg);

		if (!(tmp & ME4000_AI_STATUS_BIT_FF_DATA) &&
		    !(tmp & ME4000_AI_STATUS_BIT_HF_DATA)
		    && (tmp & ME4000_AI_STATUS_BIT_EF_DATA)) {
			ISR_PDEBUG("me4000_ai_isr():Fifo full\n");
			c = ME4000_AI_FIFO_COUNT;

			/* FIFO overflow, so stop conversion and disable all interrupts */
			spin_lock(&ai_context->int_lock);
			tmp = me4000_inl(ai_context->ctrl_reg);
			tmp |= ME4000_AI_CTRL_BIT_IMMEDIATE_STOP;
			tmp &=
			    ~(ME4000_AI_CTRL_BIT_HF_IRQ |
			      ME4000_AI_CTRL_BIT_SC_IRQ);
			outl(tmp, ai_context->ctrl_reg);
			spin_unlock(&ai_context->int_lock);
		} else if ((tmp & ME4000_AI_STATUS_BIT_FF_DATA) &&
			   !(tmp & ME4000_AI_STATUS_BIT_HF_DATA)
			   && (tmp & ME4000_AI_STATUS_BIT_EF_DATA)) {
			ISR_PDEBUG("me4000_ai_isr():Fifo half full\n");
			c = ME4000_AI_FIFO_COUNT / 2;
		} else {
			c = 0;
			ISR_PDEBUG
			    ("me4000_ai_isr():Can't determine state of fifo\n");
		}

		ISR_PDEBUG("me4000_ai_isr():Try to read %d values\n", c);

		while (1) {
			c1 = me4000_space_to_end(ai_context->circ_buf,
						 ME4000_AI_BUFFER_COUNT);
			ISR_PDEBUG("me4000_ai_isr():Space to end = %d\n", c1);
			if (c1 > c)
				c1 = c;

			if (c1 <= 0) {
				ISR_PDEBUG
				    ("me4000_ai_isr():Work done or buffer full\n");
				break;
			}

			insw(ai_context->data_reg,
			     ai_context->circ_buf.buf +
			     ai_context->circ_buf.head, c1);
			ai_context->circ_buf.head =
			    (ai_context->circ_buf.head +
			     c1) & (ME4000_AI_BUFFER_COUNT - 1);
			c -= c1;
		}

		/* Work is done, so reset the interrupt */
		ISR_PDEBUG
		    ("me4000_ai_isr():reset interrupt fifo half full interrupt\n");
		spin_lock(&ai_context->int_lock);
		tmp = me4000_inl(ai_context->ctrl_reg);
		tmp |= ME4000_AI_CTRL_BIT_HF_IRQ_RESET;
		me4000_outl(tmp, ai_context->ctrl_reg);
		tmp &= ~ME4000_AI_CTRL_BIT_HF_IRQ_RESET;
		me4000_outl(tmp, ai_context->ctrl_reg);
		spin_unlock(&ai_context->int_lock);
	}

	if (me4000_inl(ai_context->irq_status_reg) & ME4000_IRQ_STATUS_BIT_SC) {
		ISR_PDEBUG
		    ("me4000_ai_isr():Sample counter interrupt occured\n");

		if (!ai_context->sample_counter_reload) {
			ISR_PDEBUG
			    ("me4000_ai_isr():Single data block available\n");

			/* Poll data until fifo empty */
			for (i = 0;
			     (i < ME4000_AI_FIFO_COUNT / 2)
			     && (inl(ai_context->ctrl_reg) &
				 ME4000_AI_STATUS_BIT_EF_DATA); i++) {
				if (me4000_space_to_end
				    (ai_context->circ_buf,
				     ME4000_AI_BUFFER_COUNT)) {
					*(ai_context->circ_buf.buf +
					  ai_context->circ_buf.head) =
		 inw(ai_context->data_reg);
					ai_context->circ_buf.head =
					    (ai_context->circ_buf.head +
					     1) & (ME4000_AI_BUFFER_COUNT - 1);
				} else
					break;
			}
			ISR_PDEBUG("me4000_ai_isr():%d values read\n", i);
		} else {
			if (ai_context->sample_counter <=
			    ME4000_AI_FIFO_COUNT / 2) {
				ISR_PDEBUG
				    ("me4000_ai_isr():Interrupt from adjustable half full threshold\n");

				/* Read status register to find out what happened */
				tmp = me4000_inl(ai_context->ctrl_reg);

				if (!(tmp & ME4000_AI_STATUS_BIT_FF_DATA) &&
				    !(tmp & ME4000_AI_STATUS_BIT_HF_DATA)
				    && (tmp & ME4000_AI_STATUS_BIT_EF_DATA)) {
					ISR_PDEBUG
					    ("me4000_ai_isr():Fifo full\n");
					c = ME4000_AI_FIFO_COUNT;

					/* FIFO overflow, so stop conversion */
					spin_lock(&ai_context->int_lock);
					tmp = me4000_inl(ai_context->ctrl_reg);
					tmp |=
					    ME4000_AI_CTRL_BIT_IMMEDIATE_STOP;
					outl(tmp, ai_context->ctrl_reg);
					spin_unlock(&ai_context->int_lock);
				} else if ((tmp & ME4000_AI_STATUS_BIT_FF_DATA)
					   && !(tmp &
						ME4000_AI_STATUS_BIT_HF_DATA)
					   && (tmp &
					       ME4000_AI_STATUS_BIT_EF_DATA)) {
					ISR_PDEBUG
					    ("me4000_ai_isr():Fifo half full\n");
					c = ME4000_AI_FIFO_COUNT / 2;
				} else {
					c = ai_context->sample_counter;
					ISR_PDEBUG
					    ("me4000_ai_isr():Sample count values\n");
				}

				ISR_PDEBUG
				    ("me4000_ai_isr():Try to read %d values\n",
				     c);

				while (1) {
					c1 = me4000_space_to_end(ai_context->
								 circ_buf,
								 ME4000_AI_BUFFER_COUNT);
					ISR_PDEBUG
					    ("me4000_ai_isr():Space to end = %d\n",
					     c1);
					if (c1 > c)
						c1 = c;

					if (c1 <= 0) {
						ISR_PDEBUG
						    ("me4000_ai_isr():Work done or buffer full\n");
						break;
					}

					insw(ai_context->data_reg,
					     ai_context->circ_buf.buf +
					     ai_context->circ_buf.head, c1);
					ai_context->circ_buf.head =
					    (ai_context->circ_buf.head +
					     c1) & (ME4000_AI_BUFFER_COUNT - 1);
					c -= c1;
				}
			} else {
				ISR_PDEBUG
				    ("me4000_ai_isr():Multiple data block available\n");

				/* Read status register to find out what happened */
				tmp = me4000_inl(ai_context->ctrl_reg);

				if (!(tmp & ME4000_AI_STATUS_BIT_FF_DATA) &&
				    !(tmp & ME4000_AI_STATUS_BIT_HF_DATA)
				    && (tmp & ME4000_AI_STATUS_BIT_EF_DATA)) {
					ISR_PDEBUG
					    ("me4000_ai_isr():Fifo full\n");
					c = ME4000_AI_FIFO_COUNT;

					/* FIFO overflow, so stop conversion */
					spin_lock(&ai_context->int_lock);
					tmp = me4000_inl(ai_context->ctrl_reg);
					tmp |=
					    ME4000_AI_CTRL_BIT_IMMEDIATE_STOP;
					outl(tmp, ai_context->ctrl_reg);
					spin_unlock(&ai_context->int_lock);

					while (1) {
						c1 = me4000_space_to_end
						    (ai_context->circ_buf,
						     ME4000_AI_BUFFER_COUNT);
						ISR_PDEBUG
						    ("me4000_ai_isr():Space to end = %d\n",
						     c1);
						if (c1 > c)
							c1 = c;

						if (c1 <= 0) {
							ISR_PDEBUG
							    ("me4000_ai_isr():Work done or buffer full\n");
							break;
						}

						insw(ai_context->data_reg,
						     ai_context->circ_buf.buf +
						     ai_context->circ_buf.head,
						     c1);
						ai_context->circ_buf.head =
						    (ai_context->circ_buf.head +
						     c1) &
						    (ME4000_AI_BUFFER_COUNT -
						     1);
						c -= c1;
					}
				} else if ((tmp & ME4000_AI_STATUS_BIT_FF_DATA)
					   && !(tmp &
						ME4000_AI_STATUS_BIT_HF_DATA)
					   && (tmp &
					       ME4000_AI_STATUS_BIT_EF_DATA)) {
					ISR_PDEBUG
					    ("me4000_ai_isr():Fifo half full\n");
					c = ME4000_AI_FIFO_COUNT / 2;

					while (1) {
						c1 = me4000_space_to_end
						    (ai_context->circ_buf,
						     ME4000_AI_BUFFER_COUNT);
						ISR_PDEBUG
						    ("me4000_ai_isr():Space to end = %d\n",
						     c1);
						if (c1 > c)
							c1 = c;

						if (c1 <= 0) {
							ISR_PDEBUG
							    ("me4000_ai_isr():Work done or buffer full\n");
							break;
						}

						insw(ai_context->data_reg,
						     ai_context->circ_buf.buf +
						     ai_context->circ_buf.head,
						     c1);
						ai_context->circ_buf.head =
						    (ai_context->circ_buf.head +
						     c1) &
						    (ME4000_AI_BUFFER_COUNT -
						     1);
						c -= c1;
					}
				} else {
					/* Poll data until fifo empty */
					for (i = 0;
					     (i < ME4000_AI_FIFO_COUNT / 2)
					     && (inl(ai_context->ctrl_reg) &
						 ME4000_AI_STATUS_BIT_EF_DATA);
					     i++) {
						if (me4000_space_to_end
						    (ai_context->circ_buf,
						     ME4000_AI_BUFFER_COUNT)) {
							*(ai_context->circ_buf.
							  buf +
							  ai_context->circ_buf.
							  head) =
				       inw(ai_context->data_reg);
							ai_context->circ_buf.
							    head =
							    (ai_context->
							     circ_buf.head +
							     1) &
							    (ME4000_AI_BUFFER_COUNT
							     - 1);
						} else
							break;
					}
					ISR_PDEBUG
					    ("me4000_ai_isr():%d values read\n",
					     i);
				}
			}
		}

		/* Work is done, so reset the interrupt */
		ISR_PDEBUG
		    ("me4000_ai_isr():reset interrupt from sample counter\n");
		spin_lock(&ai_context->int_lock);
		tmp = me4000_inl(ai_context->ctrl_reg);
		tmp |= ME4000_AI_CTRL_BIT_SC_IRQ_RESET;
		me4000_outl(tmp, ai_context->ctrl_reg);
		tmp &= ~ME4000_AI_CTRL_BIT_SC_IRQ_RESET;
		me4000_outl(tmp, ai_context->ctrl_reg);
		spin_unlock(&ai_context->int_lock);
	}

	/* Values are now available, so wake up waiting process */
	if (me4000_buf_count(ai_context->circ_buf, ME4000_AI_BUFFER_COUNT)) {
		ISR_PDEBUG("me4000_ai_isr():Wake up waiting process\n");
		wake_up_interruptible(&(ai_context->wait_queue));
	}

	/* If there is no space left in the buffer, disable interrupts */
	spin_lock(&ai_context->int_lock);
	if (!me4000_buf_space(ai_context->circ_buf, ME4000_AI_BUFFER_COUNT)) {
		ISR_PDEBUG
		    ("me4000_ai_isr():Disable Interrupt because no space left in buffer\n");
		tmp = me4000_inl(ai_context->ctrl_reg);
		tmp &=
		    ~(ME4000_AI_CTRL_BIT_SC_IRQ | ME4000_AI_CTRL_BIT_HF_IRQ |
		      ME4000_AI_CTRL_BIT_LE_IRQ);
		me4000_outl(tmp, ai_context->ctrl_reg);
	}
	spin_unlock(&ai_context->int_lock);

#ifdef ME4000_ISR_DEBUG
	rdtscl(after);
	printk(KERN_ERR "ME4000:me4000_ai_isr():Time lapse = %lu\n",
	       after - before);
#endif

	return IRQ_HANDLED;
}

static irqreturn_t me4000_ext_int_isr(int irq, void *dev_id)
{
	struct me4000_ext_int_context *ext_int_context;
	unsigned long tmp;

	ISR_PDEBUG("me4000_ext_int_isr() is executed\n");

	ext_int_context = dev_id;

	/* Check if irq number is right */
	if (irq != ext_int_context->irq) {
		ISR_PDEBUG("me4000_ext_int_isr():incorrect interrupt num: %d\n",
			   irq);
		return IRQ_NONE;
	}

	if (me4000_inl(ext_int_context->irq_status_reg) &
	    ME4000_IRQ_STATUS_BIT_EX) {
		ISR_PDEBUG("me4000_ext_int_isr():External interrupt occured\n");
		tmp = me4000_inl(ext_int_context->ctrl_reg);
		tmp |= ME4000_AI_CTRL_BIT_EX_IRQ_RESET;
		me4000_outl(tmp, ext_int_context->ctrl_reg);
		tmp &= ~ME4000_AI_CTRL_BIT_EX_IRQ_RESET;
		me4000_outl(tmp, ext_int_context->ctrl_reg);

		ext_int_context->int_count++;

		if (ext_int_context->fasync_ptr) {
			ISR_PDEBUG
			    ("me2600_ext_int_isr():Send signal to process\n");
			kill_fasync(&ext_int_context->fasync_ptr, SIGIO,
				    POLL_IN);
		}
	}

	return IRQ_HANDLED;
}

static void __exit me4000_module_exit(void)
{
	struct me4000_info *board_info;

	CALL_PDEBUG("cleanup_module() is executed\n");

	unregister_chrdev(me4000_ext_int_major_driver_no, ME4000_EXT_INT_NAME);

	unregister_chrdev(me4000_cnt_major_driver_no, ME4000_CNT_NAME);

	unregister_chrdev(me4000_dio_major_driver_no, ME4000_DIO_NAME);

	unregister_chrdev(me4000_ai_major_driver_no, ME4000_AI_NAME);

	unregister_chrdev(me4000_ao_major_driver_no, ME4000_AO_NAME);

	remove_proc_entry("me4000", NULL);

	pci_unregister_driver(&me4000_driver);

	/* Reset the boards */
	list_for_each_entry(board_info, &me4000_board_info_list, list) {
		me4000_reset_board(board_info);
	}

	clear_board_info_list();
}

module_exit(me4000_module_exit);

static int me4000_read_procmem(char *buf, char **start, off_t offset, int count,
			       int *eof, void *data)
{
	int len = 0;
	int limit = count - 1000;
	struct me4000_info *board_info;

	len += sprintf(buf + len, "\nME4000 DRIVER VERSION %X.%X.%X\n\n",
		       (ME4000_DRIVER_VERSION & 0xFF0000) >> 16,
		       (ME4000_DRIVER_VERSION & 0xFF00) >> 8,
		       (ME4000_DRIVER_VERSION & 0xFF));

	/* Search for the board context */
	list_for_each_entry(board_info, &me4000_board_info_list, list) {
		len +=
		    sprintf(buf + len, "Board number %d:\n",
			    board_info->board_count);
		len += sprintf(buf + len, "---------------\n");
		len +=
		    sprintf(buf + len, "PLX base register = 0x%lX\n",
			    board_info->plx_regbase);
		len +=
		    sprintf(buf + len, "PLX base register size = 0x%X\n",
			    (unsigned int)board_info->plx_regbase_size);
		len +=
		    sprintf(buf + len, "ME4000 base register = 0x%X\n",
			    (unsigned int)board_info->me4000_regbase);
		len +=
		    sprintf(buf + len, "ME4000 base register size = 0x%X\n",
			    (unsigned int)board_info->me4000_regbase_size);
		len +=
		    sprintf(buf + len, "Serial number = 0x%X\n",
			    board_info->serial_no);
		len +=
		    sprintf(buf + len, "Hardware revision = 0x%X\n",
			    board_info->hw_revision);
		len +=
		    sprintf(buf + len, "Vendor id = 0x%X\n",
			    board_info->vendor_id);
		len +=
		    sprintf(buf + len, "Device id = 0x%X\n",
			    board_info->device_id);
		len +=
		    sprintf(buf + len, "PCI bus number = %d\n",
			    board_info->pci_bus_no);
		len +=
		    sprintf(buf + len, "PCI device number = %d\n",
			    board_info->pci_dev_no);
		len +=
		    sprintf(buf + len, "PCI function number = %d\n",
			    board_info->pci_func_no);
		len += sprintf(buf + len, "IRQ = %u\n", board_info->irq);
		len +=
		    sprintf(buf + len,
			    "Count of interrupts since module was loaded = %d\n",
			    board_info->irq_count);

		len +=
		    sprintf(buf + len, "Count of analog outputs = %d\n",
			    board_info->board_p->ao.count);
		len +=
		    sprintf(buf + len, "Count of analog output fifos = %d\n",
			    board_info->board_p->ao.fifo_count);

		len +=
		    sprintf(buf + len, "Count of analog inputs = %d\n",
			    board_info->board_p->ai.count);
		len +=
		    sprintf(buf + len,
			    "Count of sample and hold devices for analog input = %d\n",
			    board_info->board_p->ai.sh_count);
		len +=
		    sprintf(buf + len,
			    "Analog external trigger available for analog input = %d\n",
			    board_info->board_p->ai.ex_trig_analog);

		len +=
		    sprintf(buf + len, "Count of digital ports = %d\n",
			    board_info->board_p->dio.count);

		len +=
		    sprintf(buf + len, "Count of counter devices = %d\n",
			    board_info->board_p->cnt.count);
		len +=
		    sprintf(buf + len, "AI control register = 0x%08X\n",
			    inl(board_info->me4000_regbase +
				ME4000_AI_CTRL_REG));

		len += sprintf(buf + len, "AO 0 control register = 0x%08X\n",
			       inl(board_info->me4000_regbase +
				   ME4000_AO_00_CTRL_REG));
		len +=
		    sprintf(buf + len, "AO 0 status register = 0x%08X\n",
			    inl(board_info->me4000_regbase +
				ME4000_AO_00_STATUS_REG));
		len +=
		    sprintf(buf + len, "AO 1 control register = 0x%08X\n",
			    inl(board_info->me4000_regbase +
				ME4000_AO_01_CTRL_REG));
		len +=
		    sprintf(buf + len, "AO 1 status register = 0x%08X\n",
			    inl(board_info->me4000_regbase +
				ME4000_AO_01_STATUS_REG));
		len +=
		    sprintf(buf + len, "AO 2 control register = 0x%08X\n",
			    inl(board_info->me4000_regbase +
				ME4000_AO_02_CTRL_REG));
		len +=
		    sprintf(buf + len, "AO 2 status register = 0x%08X\n",
			    inl(board_info->me4000_regbase +
				ME4000_AO_02_STATUS_REG));
		len +=
		    sprintf(buf + len, "AO 3 control register = 0x%08X\n",
			    inl(board_info->me4000_regbase +
				ME4000_AO_03_CTRL_REG));
		len +=
		    sprintf(buf + len, "AO 3 status register = 0x%08X\n",
			    inl(board_info->me4000_regbase +
				ME4000_AO_03_STATUS_REG));
		if (len >= limit)
			break;
	}

	*eof = 1;
	return len;
}
