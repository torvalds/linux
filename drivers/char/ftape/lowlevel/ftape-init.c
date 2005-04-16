/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
 *                (C) 1996-1997 Claus-Justus Heine.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 *      This file contains the code that interfaces the kernel
 *      for the QIC-40/80/3010/3020 floppy-tape driver for Linux.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/major.h>

#include <linux/ftape.h>
#include <linux/init.h>
#include <linux/qic117.h>
#ifdef CONFIG_ZFTAPE
#include <linux/zftape.h>
#endif

#include "../lowlevel/ftape-init.h"
#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-read.h"
#include "../lowlevel/ftape-write.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/fdc-io.h"
#include "../lowlevel/ftape-buffer.h"
#include "../lowlevel/ftape-proc.h"
#include "../lowlevel/ftape-tracing.h"


#if defined(MODULE) && !defined(CONFIG_FT_NO_TRACE_AT_ALL)
static int ft_tracing = -1;
#endif


/*  Called by modules package when installing the driver
 *  or by kernel during the initialization phase
 */
static int __init ftape_init(void)
{
	TRACE_FUN(ft_t_flow);

#ifdef MODULE
#ifndef CONFIG_FT_NO_TRACE_AT_ALL
	if (ft_tracing != -1) {
		ftape_tracing = ft_tracing;
	}
#endif
	printk(KERN_INFO FTAPE_VERSION "\n");
        if (TRACE_LEVEL >= ft_t_info) {
		printk(
KERN_INFO "(c) 1993-1996 Bas Laarhoven (bas@vimec.nl)\n"
KERN_INFO "(c) 1995-1996 Kai Harrekilde-Petersen (khp@dolphinics.no)\n"
KERN_INFO "(c) 1996-1997 Claus-Justus Heine (claus@momo.math.rwth-aachen.de)\n"
KERN_INFO "QIC-117 driver for QIC-40/80/3010/3020 floppy tape drives\n");
        }
#else /* !MODULE */
	/* print a short no-nonsense boot message */
	printk(KERN_INFO FTAPE_VERSION "\n");
#endif /* MODULE */
	TRACE(ft_t_info, "installing QIC-117 floppy tape hardware drive ... ");
	TRACE(ft_t_info, "ftape_init @ 0x%p", ftape_init);
	/*  Allocate the DMA buffers. They are deallocated at cleanup() time.
	 */
#ifdef TESTING
#ifdef MODULE
	while (ftape_set_nr_buffers(CONFIG_FT_NR_BUFFERS) < 0) {
		ftape_sleep(FT_SECOND/20);
		if (signal_pending(current)) {
			(void)ftape_set_nr_buffers(0);
			TRACE(ft_t_bug,
			      "Killed by signal while allocating buffers.");
			TRACE_ABORT(-EINTR, 
				    ft_t_bug, "Free up memory and retry");
		}
	}
#else
	TRACE_CATCH(ftape_set_nr_buffers(CONFIG_FT_NR_BUFFERS),
		    (void)ftape_set_nr_buffers(0));
#endif
#else
	TRACE_CATCH(ftape_set_nr_buffers(CONFIG_FT_NR_BUFFERS),
		    (void)ftape_set_nr_buffers(0));
#endif
	ft_drive_sel = -1;
	ft_failure   = 1;         /* inhibit any operation but open */
	ftape_udelay_calibrate(); /* must be before fdc_wait_calibrate ! */
	fdc_wait_calibrate();
#if defined(CONFIG_PROC_FS) && defined(CONFIG_FT_PROC_FS)
	(void)ftape_proc_init();
#endif
#ifdef CONFIG_ZFTAPE
	(void)zft_init();
#endif
	TRACE_EXIT 0;
}

module_param(ft_fdc_base,       uint, 0);
MODULE_PARM_DESC(ft_fdc_base,  "Base address of FDC controller.");
module_param(ft_fdc_irq,        uint, 0);
MODULE_PARM_DESC(ft_fdc_irq,   "IRQ (interrupt channel) to use.");
module_param(ft_fdc_dma,        uint, 0);
MODULE_PARM_DESC(ft_fdc_dma,   "DMA channel to use.");
module_param(ft_fdc_threshold,  uint, 0);
MODULE_PARM_DESC(ft_fdc_threshold,  "Threshold of the FDC Fifo.");
module_param(ft_fdc_rate_limit, uint, 0);
MODULE_PARM_DESC(ft_fdc_rate_limit, "Maximal data rate for FDC.");
module_param(ft_probe_fc10,     bool, 0);
MODULE_PARM_DESC(ft_probe_fc10,
	    "If non-zero, probe for a Colorado FC-10/FC-20 controller.");
module_param(ft_mach2,          bool, 0);
MODULE_PARM_DESC(ft_mach2,
	    "If non-zero, probe for a Mountain MACH-2 controller.");
#if defined(MODULE) && !defined(CONFIG_FT_NO_TRACE_AT_ALL)
module_param(ft_tracing,        int, 0644);
MODULE_PARM_DESC(ft_tracing,
	    "Amount of debugging output, 0 <= tracing <= 8, default 3.");
#endif

MODULE_AUTHOR(
	"(c) 1993-1996 Bas Laarhoven (bas@vimec.nl), "
	"(c) 1995-1996 Kai Harrekilde-Petersen (khp@dolphinics.no), "
	"(c) 1996, 1997 Claus-Justus Heine (claus@momo.math.rwth-aachen.de)");
MODULE_DESCRIPTION(
	"QIC-117 driver for QIC-40/80/3010/3020 floppy tape drives.");
MODULE_LICENSE("GPL");

static void __exit ftape_exit(void)
{
	TRACE_FUN(ft_t_flow);

#if defined(CONFIG_PROC_FS) && defined(CONFIG_FT_PROC_FS)
	ftape_proc_destroy();
#endif
	(void)ftape_set_nr_buffers(0);
        printk(KERN_INFO "ftape: unloaded.\n");
	TRACE_EXIT;
}

module_init(ftape_init);
module_exit(ftape_exit);
