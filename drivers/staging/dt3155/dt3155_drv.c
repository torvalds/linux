/*

Copyright 1996,2002,2005 Gregory D. Hager, Alfred A. Rizzi, Noah J. Cowan,
                         Jason Lapenta, Scott Smedley, Greg Sharp

This file is part of the DT3155 Device Driver.

The DT3155 Device Driver is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The DT3155 Device Driver is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty
of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the DT3155 Device Driver; if not, write to the Free
Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
MA 02111-1307 USA

-- Changes --

  Date     Programmer	Description of changes made
  -------------------------------------------------------------------
  03-Jul-2000 JML       n/a
  10-Oct-2001 SS        port to 2.4 kernel
  02-Apr-2002 SS        Mods to use allocator as a standalone module;
                        Merged John Roll's changes (john@cfa.harvard.edu)
                        to make work with multiple boards.
  02-Jul-2002 SS        Merged James Rose's chages (rosejr@purdue.edu) to:
                         * fix successive interrupt-driven captures
                         * add select/poll support.
  10-Jul-2002 GCS       Add error check when ndevices > MAXBOARDS.
  02-Aug-2002 GCS       Fix field mode so that odd (lower) field is stored
                        in lower half of buffer.
  05-Aug-2005 SS        port to 2.6 kernel.
  26-Oct-2009 SS	port to 2.6.30 kernel.

-- Notes --

** appended "mem=124" in lilo.conf to allow for 4megs free on my 128meg system.
 * using allocator.c and allocator.h from o'reilly book (alessandro rubini)
    ftp://ftp.systemy.it/pub/develop (see README.allocator)

 + might want to get rid of MAXboards for allocating initial buffer.
    confusing and not necessary

 + in cleanup_module the MOD_IN_USE looks like it is check after it should

 * GFP_DMA should not be set with a PCI system (pg 291)

 - NJC why are only two buffers allowed? (see isr, approx line 358)

*/

extern void printques(int);

#ifdef MODULE
#include <linux/module.h>
#include <linux/interrupt.h>


MODULE_LICENSE("GPL");

#endif

#ifndef CONFIG_PCI
#error  "DT3155 :  Kernel PCI support not enabled (DT3155 drive requires PCI)"
#endif

#include <linux/pci.h>
#include <linux/types.h>
#include <linux/poll.h>
#include <linux/sched.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include "dt3155.h"
#include "dt3155_drv.h"
#include "dt3155_isr.h"
#include "dt3155_io.h"
#include "allocator.h"

/* Error variable.  Zero means no error. */
int dt3155_errno = 0;

#ifndef PCI_DEVICE_ID_INTEL_7116
#define PCI_DEVICE_ID_INTEL_7116 0x1223
#endif

#define DT3155_VENDORID    PCI_VENDOR_ID_INTEL
#define DT3155_DEVICEID    PCI_DEVICE_ID_INTEL_7116
#define MAXPCI    16

#ifdef DT_DEBUG
#define DT_3155_DEBUG_MSG(x,y) printk(x,y)
#else
#define DT_3155_DEBUG_MSG(x,y)
#endif

/* wait queue for interrupts */
wait_queue_head_t dt3155_read_wait_queue[ MAXBOARDS ];

#define DT_3155_SUCCESS 0
#define DT_3155_FAILURE -EIO

/* set to dynamicaly allocate, but it is tunable: */
/* insmod DT_3155 dt3155 dt3155_major=XX */
int dt3155_major = 0;

/* The minor numbers are 0 and 1 ... they are not tunable.
 * They are used as the indices for the structure vectors,
 * and register address vectors
 */

/* Global structures and variables */

/* Status of each device */
struct dt3155_status_s dt3155_status[ MAXBOARDS ];

/* kernel logical address of the board */
u8 *dt3155_lbase[ MAXBOARDS ] = { NULL
#if MAXBOARDS == 2
				      , NULL
#endif
};
/* DT3155 registers              */
u8 *dt3155_bbase = NULL;		  /* kernel logical address of the *
					   * buffer region                 */
u_int  dt3155_dev_open[ MAXBOARDS ] = {0
#if MAXBOARDS == 2
				       , 0
#endif
};

u_int  ndevices = 0;
u_long unique_tag = 0;;


/*
 * Stops interrupt generation right away and resets the status
 * to idle.  I don't know why this works and the other way doesn't.
 * (James Rose)
 */
static void quick_stop (int minor)
{
  // TODO: scott was here
#if 1
  ReadMReg((dt3155_lbase[ minor ] + INT_CSR), int_csr_r.reg);
  /* disable interrupts */
  int_csr_r.fld.FLD_END_EVE_EN = 0;
  int_csr_r.fld.FLD_END_ODD_EN = 0;
  WriteMReg((dt3155_lbase[ minor ] + INT_CSR), int_csr_r.reg );

  dt3155_status[ minor ].state &= ~(DT3155_STATE_STOP|0xff);
  /* mark the system stopped: */
  dt3155_status[ minor ].state |= DT3155_STATE_IDLE;
  dt3155_fbuffer[ minor ]->stop_acquire = 0;
  dt3155_fbuffer[ minor ]->even_stopped = 0;
#else
  dt3155_status[minor].state |= DT3155_STATE_STOP;
  dt3155_status[minor].fbuffer.stop_acquire = 1;
#endif

}


/*****************************************************
 *  dt3155_isr() Interrupt service routien
 *
 * - looks like this isr supports IRQ sharing (or could) JML
 * - Assumes irq's are disabled, via SA_INTERRUPT flag
 * being set in request_irq() call from init_module()
 *****************************************************/
static inline void dt3155_isr( int irq, void *dev_id, struct pt_regs *regs )
{
  int    minor = -1;
  int    index;
  unsigned long flags;
  u_long buffer_addr;

  /* find out who issued the interrupt */
  for ( index = 0; index < ndevices; index++ ) {
    if( dev_id == (void*) &dt3155_status[ index ])
      {
	minor = index;
	break;
      }
  }

  /* hopefully we should not get here */
  if ( minor < 0 || minor >= MAXBOARDS ) {
    printk(KERN_ERR "dt3155_isr called with invalid dev_id\n");
    return;
  }

  /* Check for corruption and set a flag if so */
  ReadMReg( (dt3155_lbase[ minor ] + CSR1), csr1_r.reg );

  if ( (csr1_r.fld.FLD_CRPT_EVE) || (csr1_r.fld.FLD_CRPT_ODD) )
    {
      /* TODO: this should probably stop acquisition */
      /* and set some flags so that dt3155_read      */
      /* returns an error next time it is called     */
      dt3155_errno = DT_ERR_CORRUPT;
      printk("dt3155:  corrupt field\n");
      return;
    }

  ReadMReg((dt3155_lbase[ minor ] + INT_CSR), int_csr_r.reg);

  /* Handle the even field ... */
  if (int_csr_r.fld.FLD_END_EVE)
    {
      if ( (dt3155_status[ minor ].state & DT3155_STATE_MODE) ==
	   DT3155_STATE_FLD )
	{
	  dt3155_fbuffer[ minor ]->frame_count++;
	}

      ReadI2C(dt3155_lbase[ minor ], EVEN_CSR, &i2c_even_csr.reg);

      /* Clear the interrupt? */
      int_csr_r.fld.FLD_END_EVE = 1;

      /* disable the interrupt if last field */
      if (dt3155_fbuffer[ minor ]->stop_acquire)
	{
	  printk("dt3155:  even stopped.\n");
	  dt3155_fbuffer[ minor ]->even_stopped = 1;
	  if (i2c_even_csr.fld.SNGL_EVE)
	    {
	      int_csr_r.fld.FLD_END_EVE_EN = 0;
	    }
	  else
	    {
	      i2c_even_csr.fld.SNGL_EVE  = 1;
	    }
	}

      WriteMReg( (dt3155_lbase[ minor ] + INT_CSR), int_csr_r.reg );

      /* Set up next DMA if we are doing FIELDS */
      if ( (dt3155_status[ minor ].state & DT3155_STATE_MODE ) ==
	   DT3155_STATE_FLD)
	{
	  /* GCS (Aug 2, 2002) -- In field mode, dma the odd field
	     into the lower half of the buffer */
	  const u_long stride =  dt3155_status[ minor ].config.cols;
	  buffer_addr = dt3155_fbuffer[ minor ]->
	    frame_info[ dt3155_fbuffer[ minor ]->active_buf ].addr
	    + (DT3155_MAX_ROWS / 2) * stride;
	  local_save_flags(flags);
	  local_irq_disable();
	  wake_up_interruptible( &dt3155_read_wait_queue[ minor ] );

	  /* Set up the DMA address for the next field */
	  local_irq_restore(flags);
	  WriteMReg((dt3155_lbase[ minor ] + ODD_DMA_START), buffer_addr);
	}

      /* Check for errors. */
      i2c_even_csr.fld.DONE_EVE = 1;
      if ( i2c_even_csr.fld.ERROR_EVE )
	dt3155_errno = DT_ERR_OVERRUN;

      WriteI2C( dt3155_lbase[ minor ], EVEN_CSR, i2c_even_csr.reg );

      /* Note that we actually saw an even field meaning  */
      /* that subsequent odd field complete the frame     */
      dt3155_fbuffer[ minor ]->even_happened = 1;

      /* recording the time that the even field finished, this should be */
      /* about time in the middle of the frame */
      do_gettimeofday( &(dt3155_fbuffer[ minor ]->
			 frame_info[ dt3155_fbuffer[ minor ]->
				     active_buf ].time) );
      return;
    }

  /* ... now handle the odd field */
  if ( int_csr_r.fld.FLD_END_ODD )
    {
      ReadI2C( dt3155_lbase[ minor ], ODD_CSR, &i2c_odd_csr.reg );

      /* Clear the interrupt? */
      int_csr_r.fld.FLD_END_ODD = 1;

      if (dt3155_fbuffer[ minor ]->even_happened ||
	  (dt3155_status[ minor ].state & DT3155_STATE_MODE) ==
	  DT3155_STATE_FLD)
	{
	  dt3155_fbuffer[ minor ]->frame_count++;
	}

      if ( dt3155_fbuffer[ minor ]->stop_acquire &&
	   dt3155_fbuffer[ minor ]->even_stopped )
	{
	  printk(KERN_DEBUG "dt3155:  stopping odd..\n");
	  if ( i2c_odd_csr.fld.SNGL_ODD )
	    {
	      /* disable interrupts */
	      int_csr_r.fld.FLD_END_ODD_EN = 0;
	      dt3155_status[ minor ].state &= ~(DT3155_STATE_STOP|0xff);

	      /* mark the system stopped: */
	      dt3155_status[ minor ].state |= DT3155_STATE_IDLE;
	      dt3155_fbuffer[ minor ]->stop_acquire = 0;
	      dt3155_fbuffer[ minor ]->even_stopped = 0;

	      printk(KERN_DEBUG "dt3155:  state is now %lx\n",
		     dt3155_status[minor].state);
	    }
	  else
	    {
	      i2c_odd_csr.fld.SNGL_ODD  = 1;
	    }
	}

      WriteMReg( (dt3155_lbase[ minor ] + INT_CSR), int_csr_r.reg );

      /* if the odd field has been acquired, then     */
      /* change the next dma location for both fields */
      /* and wake up the process if sleeping          */
      if ( dt3155_fbuffer[ minor ]->even_happened ||
	   (dt3155_status[ minor ].state & DT3155_STATE_MODE) ==
	   DT3155_STATE_FLD )
	{

	  local_save_flags(flags);
	  local_irq_disable();

#ifdef DEBUG_QUES_B
	  printques( minor );
#endif
	  if ( dt3155_fbuffer[ minor ]->nbuffers > 2 )
	    {
	      if ( !are_empty_buffers( minor ) )
		{
		  /* The number of active + locked buffers is
		   * at most 2, and since there are none empty, there
		   * must be at least nbuffers-2 ready buffers.
		   * This is where we 'drop frames', oldest first. */
		  push_empty( pop_ready( minor ),  minor );
		}

	      /* The ready_que can't be full, since we know
	       * there is one active buffer right now, so it's safe
	       * to push the active buf on the ready_que. */
	      push_ready( minor, dt3155_fbuffer[ minor ]->active_buf );
	      /* There's at least 1 empty -- make it active */
	      dt3155_fbuffer[ minor ]->active_buf = pop_empty( minor );
	      dt3155_fbuffer[ minor ]->
		frame_info[ dt3155_fbuffer[ minor ]->
			    active_buf ].tag = ++unique_tag;
	    }
	  else /* nbuffers == 2, special case */
	    { /* There is 1 active buffer.
	       * If there is a locked buffer, keep the active buffer
	       * the same -- that means we drop a frame.
	       */
	      if ( dt3155_fbuffer[ minor ]->locked_buf < 0 )
		{
		  push_ready( minor,
			      dt3155_fbuffer[ minor ]->active_buf );
		  if (are_empty_buffers( minor ) )
		    {
		      dt3155_fbuffer[ minor ]->active_buf =
			pop_empty( minor );
		    }
		  else
		    { /* no empty or locked buffers, so use a readybuf */
		      dt3155_fbuffer[ minor ]->active_buf =
			pop_ready( minor );
		    }
		}
	    }

#ifdef DEBUG_QUES_B
	  printques( minor );
#endif

	  dt3155_fbuffer[ minor ]->even_happened = 0;

	  wake_up_interruptible( &dt3155_read_wait_queue[ minor ] );

	  local_irq_restore(flags);
	}


      /* Set up the DMA address for the next frame/field */
      buffer_addr = dt3155_fbuffer[ minor ]->
	frame_info[ dt3155_fbuffer[ minor ]->active_buf ].addr;
      if ( (dt3155_status[ minor ].state & DT3155_STATE_MODE) ==
	   DT3155_STATE_FLD )
	{
	  WriteMReg((dt3155_lbase[ minor ] + EVEN_DMA_START), buffer_addr);
	}
      else
	{
	  WriteMReg((dt3155_lbase[ minor ] + EVEN_DMA_START), buffer_addr);

	  WriteMReg((dt3155_lbase[ minor ] + ODD_DMA_START), buffer_addr
		    + dt3155_status[ minor ].config.cols);
	}

      /* Do error checking */
      i2c_odd_csr.fld.DONE_ODD = 1;
      if ( i2c_odd_csr.fld.ERROR_ODD )
	dt3155_errno = DT_ERR_OVERRUN;

      WriteI2C(dt3155_lbase[ minor ], ODD_CSR, i2c_odd_csr.reg );

      return;
    }
  /* If we get here, the Odd Field wasn't it either... */
  printk( "neither even nor odd.  shared perhaps?\n");
}

/*****************************************************
 * init_isr(int minor)
 *   turns on interupt generation for the card
 *   designated by "minor".
 *   It is called *only* from inside ioctl().
 *****************************************************/
static void dt3155_init_isr(int minor)
{
  const u_long stride =  dt3155_status[ minor ].config.cols;

  switch (dt3155_status[ minor ].state & DT3155_STATE_MODE)
    {
    case DT3155_STATE_FLD:
      {
	even_dma_start_r  = dt3155_status[ minor ].
	  fbuffer.frame_info[ dt3155_status[ minor ].fbuffer.active_buf ].addr;
	even_dma_stride_r = 0;
	odd_dma_stride_r  = 0;

	WriteMReg((dt3155_lbase[ minor ] + EVEN_DMA_START),
		  even_dma_start_r);
	WriteMReg((dt3155_lbase[ minor ] + EVEN_DMA_STRIDE),
		  even_dma_stride_r);
	WriteMReg((dt3155_lbase[ minor ] + ODD_DMA_STRIDE),
		  odd_dma_stride_r);
	break;
      }

    case DT3155_STATE_FRAME:
    default:
      {
	even_dma_start_r  = dt3155_status[ minor ].
	  fbuffer.frame_info[ dt3155_status[ minor ].fbuffer.active_buf ].addr;
	odd_dma_start_r   =  even_dma_start_r + stride;
	even_dma_stride_r =  stride;
	odd_dma_stride_r  =  stride;

	WriteMReg((dt3155_lbase[ minor ] + EVEN_DMA_START),
		  even_dma_start_r);
	WriteMReg((dt3155_lbase[ minor ] + ODD_DMA_START),
		  odd_dma_start_r);
	WriteMReg((dt3155_lbase[ minor ] + EVEN_DMA_STRIDE),
		  even_dma_stride_r);
	WriteMReg((dt3155_lbase[ minor ] + ODD_DMA_STRIDE),
		  odd_dma_stride_r);
	break;
      }
    }

  /* 50/60 Hz should be set before this point but let's make sure it is */
  /* right anyway */

  ReadI2C(dt3155_lbase[ minor ], CONFIG, &i2c_csr2.reg);
  i2c_csr2.fld.HZ50 = FORMAT50HZ;
  WriteI2C(dt3155_lbase[ minor ], CONFIG, i2c_config.reg);

  /* enable busmaster chip, clear flags */

  /*
   * TODO:
   * shouldn't we be concered with continuous values of
   * DT3155_SNAP & DT3155_ACQ here? (SS)
   */

  csr1_r.reg                = 0;
  csr1_r.fld.CAP_CONT_EVE   = 1; /* use continuous capture bits to */
  csr1_r.fld.CAP_CONT_ODD   = 1; /* enable */
  csr1_r.fld.FLD_DN_EVE     = 1; /* writing a 1 clears flags */
  csr1_r.fld.FLD_DN_ODD     = 1;
  csr1_r.fld.SRST           = 1; /* reset        - must be 1 */
  csr1_r.fld.FIFO_EN        = 1; /* fifo control - must be 1 */
  csr1_r.fld.FLD_CRPT_EVE   = 1; /* writing a 1 clears flags */
  csr1_r.fld.FLD_CRPT_ODD   = 1;

  WriteMReg((dt3155_lbase[ minor ] + CSR1),csr1_r.reg);

  /* Enable interrupts at the end of each field */

  int_csr_r.reg = 0;
  int_csr_r.fld.FLD_END_EVE_EN = 1;
  int_csr_r.fld.FLD_END_ODD_EN = 1;
  int_csr_r.fld.FLD_START_EN = 0;

  WriteMReg((dt3155_lbase[ minor ] + INT_CSR), int_csr_r.reg);

  /* start internal BUSY bits */

  ReadI2C(dt3155_lbase[ minor ], CSR2, &i2c_csr2.reg);
  i2c_csr2.fld.BUSY_ODD  = 1;
  i2c_csr2.fld.BUSY_EVE  = 1;
  WriteI2C(dt3155_lbase[ minor ], CSR2, i2c_csr2.reg);

  /* Now its up to the interrupt routine!! */

  return;
}


/*****************************************************
 * ioctl()
 *
 *****************************************************/
static int dt3155_ioctl (
			 struct inode	*inode,
			 struct file		*file,
			 u_int			cmd,
			 u_long			arg)
{
  int minor = MINOR(inode->i_rdev); /* What device are we ioctl()'ing? */

  if ( minor >= MAXBOARDS || minor < 0 )
    return -ENODEV;

  /* make sure it is valid command */
  if (_IOC_NR(cmd) > DT3155_IOC_MAXNR)
    {
      printk("DT3155: invalid IOCTL(0x%x)\n",cmd);
      printk("DT3155: Valid commands (0x%x), (0x%x), (0x%x), (0x%x), (0x%x)\n",
	     (unsigned int)DT3155_GET_CONFIG,
	     (unsigned int)DT3155_SET_CONFIG,
	     (unsigned int)DT3155_START,
	     (unsigned int)DT3155_STOP,
	     (unsigned int)DT3155_FLUSH);
      return -EINVAL;
    }

  switch (cmd)
    {
    case DT3155_SET_CONFIG:
      {
	if (dt3155_status[minor].state != DT3155_STATE_IDLE)
	  return -EBUSY;

	{
	  struct dt3155_config_s tmp;
	  if (copy_from_user((void *)&tmp, (void *) arg, sizeof(tmp)))
	      return -EFAULT;
	  /* check for valid settings */
	  if (tmp.rows > DT3155_MAX_ROWS ||
	      tmp.cols > DT3155_MAX_COLS ||
	      (tmp.acq_mode != DT3155_MODE_FRAME &&
	       tmp.acq_mode != DT3155_MODE_FIELD) ||
	      (tmp.continuous != DT3155_SNAP &&
	       tmp.continuous != DT3155_ACQ))
	    {
	      return -EINVAL;
	    }
	  dt3155_status[minor].config = tmp;
	}
	return 0;
      }
    case DT3155_GET_CONFIG:
      {
	if (copy_to_user((void *) arg, (void *) &dt3155_status[minor],
		     sizeof(dt3155_status_t) ))
	    return -EFAULT;
	return 0;
      }
    case DT3155_FLUSH: /* Flushes the buffers -- ensures fresh data */
      {
	if (dt3155_status[minor].state != DT3155_STATE_IDLE)
	  return -EBUSY;
	return dt3155_flush(minor);
      }
    case DT3155_STOP:
      {
	if (dt3155_status[minor].state & DT3155_STATE_STOP ||
	    dt3155_status[minor].fbuffer.stop_acquire)
	  return -EBUSY;

	if (dt3155_status[minor].state == DT3155_STATE_IDLE)
	  return 0;

	quick_stop(minor);
	if (copy_to_user((void *) arg, (void *) &dt3155_status[minor],
		     sizeof(dt3155_status_t)))
	    return -EFAULT;
	return 0;
      }
    case DT3155_START:
      {
	if (dt3155_status[minor].state != DT3155_STATE_IDLE)
	  return -EBUSY;

	dt3155_status[minor].fbuffer.stop_acquire = 0;
	dt3155_status[minor].fbuffer.frame_count = 0;

	/* Set the MODE in the status -- we default to FRAME */
	if (dt3155_status[minor].config.acq_mode == DT3155_MODE_FIELD)
	  {
	    dt3155_status[minor].state = DT3155_STATE_FLD;
	  }
	else
	  {
	    dt3155_status[minor].state = DT3155_STATE_FRAME;
	  }

	dt3155_init_isr(minor);
	if (copy_to_user( (void *) arg, (void *) &dt3155_status[minor],
		      sizeof(dt3155_status_t)))
	    return -EFAULT;
	return 0;
      }
    default:
      {
	printk("DT3155: invalid IOCTL(0x%x)\n",cmd);
      printk("DT3155: Valid commands (0x%x), (0x%x), (0x%x), (0x%x), (0x%x)\n",
	     (unsigned int)DT3155_GET_CONFIG,
	     (unsigned int)DT3155_SET_CONFIG,
	     DT3155_START, DT3155_STOP, DT3155_FLUSH);
	return -ENOSYS;
      }
    }
  return -ENOSYS;
}

/*****************************************************
 * mmap()
 *
 * only allow the user to mmap the registers and buffer
 * It is quite possible that this is broken, since the
 * addition of of the capacity for two cards!!!!!!!!
 * It *looks* like it should work but since I'm not
 * sure how to use it, I'm not actually sure. (NJC? ditto by SS)
 *****************************************************/
static int dt3155_mmap (struct file * file, struct vm_area_struct * vma)
{
  /* which device are we mmapping? */
  int				minor = MINOR(file->f_dentry->d_inode->i_rdev);
  unsigned long	offset;
  offset = vma->vm_pgoff << PAGE_SHIFT;

  if (offset >= __pa(high_memory) || (file->f_flags & O_SYNC))
    vma->vm_flags |= VM_IO;

  /* Don't try to swap out physical pages.. */
  vma->vm_flags |= VM_RESERVED;

  /* they are mapping the registers or the buffer */
  if ((offset == dt3155_status[minor].reg_addr &&
       vma->vm_end - vma->vm_start == PCI_PAGE_SIZE) ||
      (offset == dt3155_status[minor].mem_addr &&
       vma->vm_end - vma->vm_start == dt3155_status[minor].mem_size))
    {
      if (remap_pfn_range(vma,
			vma->vm_start,
			offset >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start,
			vma->vm_page_prot)) {
	  printk("DT3155: remap_page_range() failed.\n");
	  return -EAGAIN;
	}
    }
  else
    {
      printk("DT3155: dt3155_mmap() bad call.\n");
      return -ENXIO;
    }

  return 0;
}


/*****************************************************
 * open()
 *
 * Our special open code.
 * MOD_INC_USE_COUNT make sure that the driver memory is not freed
 * while the device is in use.
 *****************************************************/
static int dt3155_open( struct inode* inode, struct file* filep)
{
  int minor = MINOR(inode->i_rdev); /* what device are we opening? */
  if (dt3155_dev_open[ minor ]) {
    printk ("DT3155:  Already opened by another process.\n");
    return -EBUSY;
  }

  if (dt3155_status[ minor ].device_installed==0)
    {
      printk("DT3155 Open Error: No such device dt3155 minor number %d\n",
	     minor);
      return -EIO;
    }

  if (dt3155_status[ minor ].state != DT3155_STATE_IDLE) {
    printk ("DT3155:  Not in idle state (state = %lx)\n",
	    dt3155_status[ minor ].state);
    return -EBUSY;
  }

  printk("DT3155: Device opened.\n");

  dt3155_dev_open[ minor ] = 1 ;

  dt3155_flush( minor );

  /* Disable ALL interrupts */
  int_csr_r.reg = 0;
  WriteMReg( (dt3155_lbase[ minor ] + INT_CSR), int_csr_r.reg );

  init_waitqueue_head(&(dt3155_read_wait_queue[minor]));

  return 0;
}


/*****************************************************
 * close()
 *
 * Now decrement the use count.
 *
 *****************************************************/
static int dt3155_close( struct inode *inode, struct file *filep)
{
  int minor;

  minor = MINOR(inode->i_rdev); /* which device are we closing */
  if (!dt3155_dev_open[ minor ])
    {
      printk("DT3155: attempt to CLOSE a not OPEN device\n");
    }
  else
    {
      dt3155_dev_open[ minor ] = 0;

      if (dt3155_status[ minor ].state != DT3155_STATE_IDLE)
	{
	  quick_stop(minor);
	}
    }
  return 0;
}

/*****************************************************
 * read()
 *
 *****************************************************/
static ssize_t dt3155_read(struct file *filep, char __user *buf,
			   size_t count, loff_t *ppos)
{
  /* which device are we reading from? */
  int		minor = MINOR(filep->f_dentry->d_inode->i_rdev);
  u_long		offset;
  int		frame_index;
  frame_info_t	*frame_info_p;

  /* TODO: this should check the error flag and */
  /*   return an error on hardware failures */
  if (count != sizeof(dt3155_read_t))
    {
      printk("DT3155 ERROR (NJC): count is not right\n");
      return -EINVAL;
    }


  /* Hack here -- I'm going to allow reading even when idle.
   * this is so that the frames can be read after STOP has
   * been called.  Leaving it here, commented out, as a reminder
   * for a short while to make sure there are no problems.
   * Note that if the driver is not opened in non_blocking mode,
   * and the device is idle, then it could sit here forever! */

  /*  if (dt3155_status[minor].state == DT3155_STATE_IDLE)*/
  /*    return -EBUSY;*/

  /* non-blocking reads should return if no data */
  if (filep->f_flags & O_NDELAY)
    {
      if ((frame_index = dt3155_get_ready_buffer(minor)) < 0) {
	/*printk( "dt3155:  no buffers available (?)\n");*/
	/* 		printques(minor); */
	return -EAGAIN;
      }
    }
  else
    {
      /*
       * sleep till data arrives , or we get interrupted.
       * Note that wait_event_interruptible() does not actually
       * sleep/wait if it's condition evaluates to true upon entry.
       */
      wait_event_interruptible(dt3155_read_wait_queue[minor],
			       (frame_index = dt3155_get_ready_buffer(minor))
			       >= 0);

      if (frame_index < 0)
	{
	  printk ("DT3155: read: interrupted\n");
	  quick_stop (minor);
	  printques(minor);
	  return -EINTR;
	}
    }

  frame_info_p = &dt3155_status[minor].fbuffer.frame_info[frame_index];

  /* make this an offset */
  offset = frame_info_p->addr - dt3155_status[minor].mem_addr;

  put_user(offset, (unsigned int *) buf);
  buf += sizeof(u_long);
  put_user( dt3155_status[minor].fbuffer.frame_count, (unsigned int *) buf);
  buf += sizeof(u_long);
  put_user(dt3155_status[minor].state, (unsigned int *) buf);
  buf += sizeof(u_long);
  if (copy_to_user(buf, frame_info_p, sizeof(frame_info_t)))
      return -EFAULT;

  return sizeof(dt3155_read_t);
}

static unsigned int dt3155_poll (struct file * filp, poll_table *wait)
{
  int minor = MINOR(filp->f_dentry->d_inode->i_rdev);

  if (!is_ready_buf_empty(minor))
    return POLLIN | POLLRDNORM;

  poll_wait (filp, &dt3155_read_wait_queue[minor], wait);

  return 0;
}


/*****************************************************
 * file operations supported by DT3155 driver
 *  needed by init_module
 *  register_chrdev
 *****************************************************/
static struct file_operations dt3155_fops = {
  read:		dt3155_read,
  ioctl:		dt3155_ioctl,
  mmap:		dt3155_mmap,
  poll:           dt3155_poll,
  open:		dt3155_open,
  release:	dt3155_close
};


/*****************************************************
 * find_PCI();
 *
 * PCI has been totally reworked in 2.1..
 *****************************************************/
static int find_PCI (void)
{
  struct pci_dev *pci_dev = NULL;
  int error, pci_index = 0;
  unsigned short rev_device;
  unsigned long base;
  unsigned char irq;

  while ((pci_dev = pci_get_device
	  (DT3155_VENDORID, DT3155_DEVICEID, pci_dev)) != NULL)
    {
      pci_index ++;

      /* Is it really there? */
      if ((error =
	   pci_read_config_word(pci_dev, PCI_CLASS_DEVICE, &rev_device)))
	continue;

      /* Found a board */
      DT_3155_DEBUG_MSG("DT3155: Device number %d \n", pci_index);

      /* Make sure the driver was compiled with enough buffers to handle
	 this many boards */
      if (pci_index > MAXBOARDS) {
	printk("DT3155: ERROR - found %d devices, but driver only configured "
	       "for %d devices\n"
	       "DT3155: Please change MAXBOARDS in dt3155.h\n",
	       pci_index, MAXBOARDS);
	goto err;
      }

      /* Now, just go out and make sure that this/these device(s) is/are
	 actually mapped into the kernel address space */
      if ((error = pci_read_config_dword( pci_dev, PCI_BASE_ADDRESS_0,
					  (u_int *) &base)))
	{
	  printk("DT3155: Was not able to find device \n");
	  goto err;
	}

      DT_3155_DEBUG_MSG("DT3155: Base address 0 for device is %lx \n", base);
      dt3155_status[pci_index-1].reg_addr = base;

      /* Remap the base address to a logical address through which we
       * can access it. */
      dt3155_lbase[ pci_index - 1 ] = ioremap(base,PCI_PAGE_SIZE);
      dt3155_status[ pci_index - 1 ].reg_addr = base;
      DT_3155_DEBUG_MSG("DT3155: New logical address is %p \n",
			dt3155_lbase[pci_index-1]);
      if ( !dt3155_lbase[pci_index-1] )
	{
	  printk("DT3155: Unable to remap control registers\n");
	  goto err;
	}

      if ( (error = pci_read_config_byte( pci_dev, PCI_INTERRUPT_LINE, &irq)) )
	{
	  printk("DT3155: Was not able to find device \n");
	  goto err;
	}

      DT_3155_DEBUG_MSG("DT3155: IRQ is %d \n",irq);
      dt3155_status[ pci_index-1 ].irq = irq;
      /* Set flag: kth device found! */
      dt3155_status[ pci_index-1 ].device_installed = 1;
      printk("DT3155: Installing device %d w/irq %d and address %p\n",
	     pci_index,
	     (u_int)dt3155_status[pci_index-1].irq,
	     dt3155_lbase[pci_index-1]);

    }
  ndevices = pci_index;

  return DT_3155_SUCCESS;

err:
  pci_dev_put(pci_dev);
  return DT_3155_FAILURE;
}

u_long allocatorAddr = 0;

/*****************************************************
 * init_module()
 *****************************************************/
int init_module(void)
{
  int index;
  int rcode = 0;
  char *devname[ MAXBOARDS ];

  devname[ 0 ] = "dt3155a";
#if MAXBOARDS == 2
  devname[ 1 ] = "dt3155b";
#endif

  printk("DT3155: Loading module...\n");

  /* Register the device driver */
  rcode = register_chrdev( dt3155_major, "dt3155", &dt3155_fops );
  if( rcode < 0 )
    {
      printk( KERN_INFO "DT3155: register_chrdev failed \n");
      return rcode;
    }

  if( dt3155_major == 0 )
    dt3155_major = rcode; /* dynamic */


  /* init the status variables.                     */
  /* DMA memory is taken care of in setup_buffers() */
  for ( index = 0; index < MAXBOARDS; index++ )
    {
      dt3155_status[ index ].config.acq_mode   = DT3155_MODE_FRAME;
      dt3155_status[ index ].config.continuous = DT3155_ACQ;
      dt3155_status[ index ].config.cols       = DT3155_MAX_COLS;
      dt3155_status[ index ].config.rows       = DT3155_MAX_ROWS;
      dt3155_status[ index ].state = DT3155_STATE_IDLE;

      /* find_PCI() will check if devices are installed; */
      /* first assume they're not:                       */
      dt3155_status[ index ].mem_addr          = 0;
      dt3155_status[ index ].mem_size          = 0;
      dt3155_status[ index ].state             = DT3155_STATE_IDLE;
      dt3155_status[ index ].device_installed  = 0;
    }

  /* Now let's find the hardware.  find_PCI() will set ndevices to the
   * number of cards found in this machine. */
    {
      if ( (rcode = find_PCI()) !=  DT_3155_SUCCESS )
	{
	  printk("DT3155 error: find_PCI() failed to find dt3155 board(s)\n");
	  unregister_chrdev( dt3155_major, "dt3155" );
	  return rcode;
	}
    }

  /* Ok, time to setup the frame buffers */
  if( (rcode = dt3155_setup_buffers(&allocatorAddr)) < 0 )
    {
      printk("DT3155: Error: setting up buffer not large enough.");
      unregister_chrdev( dt3155_major, "dt3155" );
      return rcode;
    }

  /* If we are this far, then there is enough RAM */
  /* for the buffers: Print the configuration.    */
  for(  index = 0;  index < ndevices;  index++ )
    {
      printk("DT3155: Device = %d; acq_mode = %d; "
	     "continuous = %d; cols = %d; rows = %d;\n",
	     index ,
	     dt3155_status[ index ].config.acq_mode,
	     dt3155_status[ index ].config.continuous,
	     dt3155_status[ index ].config.cols,
	     dt3155_status[ index ].config.rows);
      printk("DT3155: m_addr = 0x%x; m_size = %ld; "
	     "state = %ld; device_installed = %d\n",
	     (u_int)dt3155_status[ index ].mem_addr,
	     dt3155_status[ index ].mem_size,
	     dt3155_status[ index ].state,
	     dt3155_status[ index ].device_installed);
    }

  /* Disable ALL interrupts */
  int_csr_r.reg = 0;
  for(  index = 0;  index < ndevices;  index++ )
    {
      WriteMReg( (dt3155_lbase[ index ] + INT_CSR), int_csr_r.reg );
      if( dt3155_status[ index ].device_installed )
	{
	  /*
	   * This driver *looks* like it can handle sharing interrupts,
	   * but I can't actually test myself. I've had reports that it
	   * DOES work so I'll enable it for now. This comment will remain
	   * as a reminder in case any problems arise. (SS)
	   */
	  /* in older kernels flags are: SA_SHIRQ | SA_INTERRUPT */
	  rcode = request_irq( dt3155_status[ index ].irq, (void *)dt3155_isr,
			       IRQF_SHARED | IRQF_DISABLED, devname[ index ],
			       (void*) &dt3155_status[index]);
	  if( rcode < 0 )
	    {
	      printk("DT3155: minor %d request_irq failed for IRQ %d\n",
		     index, dt3155_status[index].irq);
	      unregister_chrdev( dt3155_major, "dt3155" );
	      return rcode;
	    }
	}
    }

  printk("DT3155: finished loading\n");

  return 0;
}

/*****************************************************
 * cleanup_module(void)
 *
 *****************************************************/
void cleanup_module(void)
{
  int index;

  printk("DT3155:  cleanup_module called\n");

  /* removed DMA allocated with the allocator */
#ifdef STANDALONE_ALLOCATOR
  if (allocatorAddr != 0)
    allocator_free_dma(allocatorAddr);
#else
  allocator_cleanup();
#endif

  unregister_chrdev( dt3155_major, "dt3155" );

  for( index = 0; index < ndevices; index++ )
    {
      if( dt3155_status[ index ].device_installed == 1 )
	{
	  printk( "DT3155: Freeing irq %d for device %d\n",
		  dt3155_status[ index ].irq, index );
	  free_irq( dt3155_status[ index ].irq, (void*)&dt3155_status[index] );
	}
    }
}

