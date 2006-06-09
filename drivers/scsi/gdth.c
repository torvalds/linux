/************************************************************************
 * Linux driver for                                                     *  
 * ICP vortex GmbH:    GDT ISA/EISA/PCI Disk Array Controllers          *
 * Intel Corporation:  Storage RAID Controllers                         *
 *                                                                      *
 * gdth.c                                                               *
 * Copyright (C) 1995-06 ICP vortex GmbH, Achim Leubner                 *
 * Copyright (C) 2002-04 Intel Corporation                              *
 * Copyright (C) 2003-06 Adaptec Inc.                                   *
 * <achim_leubner@adaptec.com>                                          *
 *                                                                      *
 * Additions/Fixes:                                                     *
 * Boji Tony Kannanthanam <boji.t.kannanthanam@intel.com>               *
 * Johannes Dinner <johannes_dinner@adaptec.com>                        *
 *                                                                      *
 * This program is free software; you can redistribute it and/or modify *
 * it under the terms of the GNU General Public License as published    *
 * by the Free Software Foundation; either version 2 of the License,    *
 * or (at your option) any later version.                               *
 *                                                                      *
 * This program is distributed in the hope that it will be useful,      *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of       *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the         *
 * GNU General Public License for more details.                         *
 *                                                                      *
 * You should have received a copy of the GNU General Public License    *
 * along with this kernel; if not, write to the Free Software           *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.            *
 *                                                                      *
 * Linux kernel 2.4.x, 2.6.x supported                                  *
 *                                                                      *
 * $Log: gdth.c,v $
 * Revision 1.74  2006/04/10 13:44:47  achim
 * Community changes for 2.6.x
 * Kernel 2.2.x no longer supported
 * scsi_request interface removed, thanks to Christoph Hellwig
 *
 * Revision 1.73  2004/03/31 13:33:03  achim
 * Special command 0xfd implemented to detect 64-bit DMA support
 *
 * Revision 1.72  2004/03/17 08:56:04  achim
 * 64-bit DMA only enabled if FW >= x.43
 *
 * Revision 1.71  2004/03/05 15:51:29  achim
 * Screen service: separate message buffer, bugfixes
 *
 * Revision 1.70  2004/02/27 12:19:07  achim
 * Bugfix: Reset bit in config (0xfe) call removed
 *
 * Revision 1.69  2004/02/20 09:50:24  achim
 * Compatibility changes for kernels < 2.4.20
 * Bugfix screen service command size
 * pci_set_dma_mask() error handling added
 *
 * Revision 1.68  2004/02/19 15:46:54  achim
 * 64-bit DMA bugfixes
 * Drive size bugfix for drives > 1TB
 *
 * Revision 1.67  2004/01/14 13:11:57  achim
 * Tool access over /proc no longer supported
 * Bugfixes IOCTLs
 *
 * Revision 1.66  2003/12/19 15:04:06  achim
 * Bugfixes support for drives > 2TB
 *
 * Revision 1.65  2003/12/15 11:21:56  achim
 * 64-bit DMA support added
 * Support for drives > 2 TB implemented
 * Kernels 2.2.x, 2.4.x, 2.6.x supported
 *
 * Revision 1.64  2003/09/17 08:30:26  achim
 * EISA/ISA controller scan disabled
 * Command line switch probe_eisa_isa added
 *
 * Revision 1.63  2003/07/12 14:01:00  Daniele Bellucci <bellucda@tiscali.it>
 * Minor cleanups in gdth_ioctl.
 *
 * Revision 1.62  2003/02/27 15:01:59  achim
 * Dynamic DMA mapping implemented
 * New (character device) IOCTL interface added
 * Other controller related changes made
 *
 * Revision 1.61  2002/11/08 13:09:52  boji
 * Added support for XSCALE based RAID Controllers
 * Fixed SCREENSERVICE initialization in SMP cases
 * Added checks for gdth_polling before GDTH_HA_LOCK
 *
 * Revision 1.60  2002/02/05 09:35:22  achim
 * MODULE_LICENSE only if kernel >= 2.4.11
 *
 * Revision 1.59  2002/01/30 09:46:33  achim
 * Small changes
 *
 * Revision 1.58  2002/01/29 15:30:02  achim
 * Set default value of shared_access to Y
 * New status S_CACHE_RESERV for clustering added
 *
 * Revision 1.57  2001/08/21 11:16:35  achim
 * Bugfix free_irq()
 *
 * Revision 1.56  2001/08/09 11:19:39  achim
 * Scsi_Host_Template changes
 *
 * Revision 1.55  2001/08/09 10:11:28  achim
 * Command HOST_UNFREEZE_IO before cache service init.
 *
 * Revision 1.54  2001/07/20 13:48:12  achim
 * Expand: gdth_analyse_hdrive() removed
 *
 * Revision 1.53  2001/07/17 09:52:49  achim
 * Small OEM related change
 *
 * Revision 1.52  2001/06/19 15:06:20  achim
 * New host command GDT_UNFREEZE_IO added
 *
 * Revision 1.51  2001/05/22 06:42:37  achim
 * PCI: Subdevice ID added
 *
 * Revision 1.50  2001/05/17 13:42:16  achim
 * Support for Intel Storage RAID Controllers added
 *
 * Revision 1.50  2001/05/17 12:12:34  achim
 * Support for Intel Storage RAID Controllers added
 *
 * Revision 1.49  2001/03/15 15:07:17  achim
 * New __setup interface for boot command line options added
 *
 * Revision 1.48  2001/02/06 12:36:28  achim
 * Bugfix Cluster protocol
 *
 * Revision 1.47  2001/01/10 14:42:06  achim
 * New switch shared_access added
 *
 * Revision 1.46  2001/01/09 08:11:35  achim
 * gdth_command() removed
 * meaning of Scsi_Pointer members changed
 *
 * Revision 1.45  2000/11/16 12:02:24  achim
 * Changes for kernel 2.4
 *
 * Revision 1.44  2000/10/11 08:44:10  achim
 * Clustering changes: New flag media_changed added
 *
 * Revision 1.43  2000/09/20 12:59:01  achim
 * DPMEM remap functions for all PCI controller types implemented
 * Small changes for ia64 platform
 *
 * Revision 1.42  2000/07/20 09:04:50  achim
 * Small changes for kernel 2.4
 *
 * Revision 1.41  2000/07/04 14:11:11  achim
 * gdth_analyse_hdrive() added to rescan drives after online expansion
 *
 * Revision 1.40  2000/06/27 11:24:16  achim
 * Changes Clustering, Screenservice
 *
 * Revision 1.39  2000/06/15 13:09:04  achim
 * Changes for gdth_do_cmd()
 *
 * Revision 1.38  2000/06/15 12:08:43  achim
 * Bugfix gdth_sync_event(), service SCREENSERVICE
 * Data direction for command 0xc2 changed to DOU
 *
 * Revision 1.37  2000/05/25 13:50:10  achim
 * New driver parameter virt_ctr added
 *
 * Revision 1.36  2000/05/04 08:50:46  achim
 * Event buffer now in gdth_ha_str
 *
 * Revision 1.35  2000/03/03 10:44:08  achim
 * New event_string only valid for the RP controller family
 *
 * Revision 1.34  2000/03/02 14:55:29  achim
 * New mechanism for async. event handling implemented
 *
 * Revision 1.33  2000/02/21 15:37:37  achim
 * Bugfix Alpha platform + DPMEM above 4GB
 *
 * Revision 1.32  2000/02/14 16:17:37  achim
 * Bugfix sense_buffer[] + raw devices
 *
 * Revision 1.31  2000/02/10 10:29:00  achim
 * Delete sense_buffer[0], if command OK
 *
 * Revision 1.30  1999/11/02 13:42:39  achim
 * ARRAY_DRV_LIST2 implemented
 * Now 255 log. and 100 host drives supported
 *
 * Revision 1.29  1999/10/05 13:28:47  achim
 * GDT_CLUST_RESET added
 *
 * Revision 1.28  1999/08/12 13:44:54  achim
 * MOUNTALL removed
 * Cluster drives -> removeable drives
 *
 * Revision 1.27  1999/06/22 07:22:38  achim
 * Small changes
 *
 * Revision 1.26  1999/06/10 16:09:12  achim
 * Cluster Host Drive support: Bugfixes
 *
 * Revision 1.25  1999/06/01 16:03:56  achim
 * gdth_init_pci(): Manipulate config. space to start RP controller
 *
 * Revision 1.24  1999/05/26 11:53:06  achim
 * Cluster Host Drive support added
 *
 * Revision 1.23  1999/03/26 09:12:31  achim
 * Default value for hdr_channel set to 0
 *
 * Revision 1.22  1999/03/22 16:27:16  achim
 * Bugfix: gdth_store_event() must not be locked with GDTH_LOCK_HA()
 *
 * Revision 1.21  1999/03/16 13:40:34  achim
 * Problems with reserved drives solved
 * gdth_eh_bus_reset() implemented
 *
 * Revision 1.20  1999/03/10 09:08:13  achim
 * Bugfix: Corrections in gdth_direction_tab[] made
 * Bugfix: Increase command timeout (gdth_update_timeout()) NOT in gdth_putq()
 *
 * Revision 1.19  1999/03/05 14:38:16  achim
 * Bugfix: Heads/Sectors mapping for reserved devices possibly wrong
 * -> gdth_eval_mapping() implemented, changes in gdth_bios_param()
 * INIT_RETRIES set to 100s to avoid DEINIT-Timeout for controllers
 * with BIOS disabled and memory test set to Intensive
 * Enhanced /proc support
 *
 * Revision 1.18  1999/02/24 09:54:33  achim
 * Command line parameter hdr_channel implemented
 * Bugfix for EISA controllers + Linux 2.2.x
 *
 * Revision 1.17  1998/12/17 15:58:11  achim
 * Command line parameters implemented
 * Changes for Alpha platforms
 * PCI controller scan changed
 * SMP support improved (spin_lock_irqsave(),...)
 * New async. events, new scan/reserve commands included
 *
 * Revision 1.16  1998/09/28 16:08:46  achim
 * GDT_PCIMPR: DPMEM remapping, if required
 * mdelay() added
 *
 * Revision 1.15  1998/06/03 14:54:06  achim
 * gdth_delay(), gdth_flush() implemented
 * Bugfix: gdth_release() changed
 *
 * Revision 1.14  1998/05/22 10:01:17  achim
 * mj: pcibios_strerror() removed
 * Improved SMP support (if version >= 2.1.95)
 * gdth_halt(): halt_called flag added (if version < 2.1)
 *
 * Revision 1.13  1998/04/16 09:14:57  achim
 * Reserve drives (for raw service) implemented
 * New error handling code enabled
 * Get controller name from board_info() IOCTL
 * Final round of PCI device driver patches by Martin Mares
 *
 * Revision 1.12  1998/03/03 09:32:37  achim
 * Fibre channel controller support added
 *
 * Revision 1.11  1998/01/27 16:19:14  achim
 * SA_SHIRQ added
 * add_timer()/del_timer() instead of GDTH_TIMER
 * scsi_add_timer()/scsi_del_timer() instead of SCSI_TIMER
 * New error handling included
 *
 * Revision 1.10  1997/10/31 12:29:57  achim
 * Read heads/sectors from host drive
 *
 * Revision 1.9  1997/09/04 10:07:25  achim
 * IO-mapping with virt_to_bus(), gdth_readb(), gdth_writeb(), ...
 * register_reboot_notifier() to get a notify on shutown used
 *
 * Revision 1.8  1997/04/02 12:14:30  achim
 * Version 1.00 (see gdth.h), tested with kernel 2.0.29
 *
 * Revision 1.7  1997/03/12 13:33:37  achim
 * gdth_reset() changed, new async. events
 *
 * Revision 1.6  1997/03/04 14:01:11  achim
 * Shutdown routine gdth_halt() implemented
 *
 * Revision 1.5  1997/02/21 09:08:36  achim
 * New controller included (RP, RP1, RP2 series)
 * IOCTL interface implemented
 *
 * Revision 1.4  1996/07/05 12:48:55  achim
 * Function gdth_bios_param() implemented
 * New constant GDTH_MAXC_P_L inserted
 * GDT_WRITE_THR, GDT_EXT_INFO implemented
 * Function gdth_reset() changed
 *
 * Revision 1.3  1996/05/10 09:04:41  achim
 * Small changes for Linux 1.2.13
 *
 * Revision 1.2  1996/05/09 12:45:27  achim
 * Loadable module support implemented
 * /proc support corrections made
 *
 * Revision 1.1  1996/04/11 07:35:57  achim
 * Initial revision
 *
 ************************************************************************/

/* All GDT Disk Array Controllers are fully supported by this driver.
 * This includes the PCI/EISA/ISA SCSI Disk Array Controllers and the
 * PCI Fibre Channel Disk Array Controllers. See gdth.h for a complete
 * list of all controller types.
 * 
 * If you have one or more GDT3000/3020 EISA controllers with 
 * controller BIOS disabled, you have to set the IRQ values with the 
 * command line option "gdth=irq1,irq2,...", where the irq1,irq2,... are
 * the IRQ values for the EISA controllers.
 * 
 * After the optional list of IRQ values, other possible 
 * command line options are:
 * disable:Y                    disable driver
 * disable:N                    enable driver
 * reserve_mode:0               reserve no drives for the raw service
 * reserve_mode:1               reserve all not init., removable drives
 * reserve_mode:2               reserve all not init. drives
 * reserve_list:h,b,t,l,h,b,t,l,...     reserve particular drive(s) with 
 *                              h- controller no., b- channel no., 
 *                              t- target ID, l- LUN
 * reverse_scan:Y               reverse scan order for PCI controllers         
 * reverse_scan:N               scan PCI controllers like BIOS
 * max_ids:x                    x - target ID count per channel (1..MAXID)
 * rescan:Y                     rescan all channels/IDs 
 * rescan:N                     use all devices found until now
 * virt_ctr:Y                   map every channel to a virtual controller 
 * virt_ctr:N                   use multi channel support 
 * hdr_channel:x                x - number of virtual bus for host drives
 * shared_access:Y              disable driver reserve/release protocol to 
 *                              access a shared resource from several nodes, 
 *                              appropriate controller firmware required
 * shared_access:N              enable driver reserve/release protocol
 * probe_eisa_isa:Y             scan for EISA/ISA controllers
 * probe_eisa_isa:N             do not scan for EISA/ISA controllers
 * force_dma32:Y                use only 32 bit DMA mode
 * force_dma32:N                use 64 bit DMA mode, if supported
 *
 * The default values are: "gdth=disable:N,reserve_mode:1,reverse_scan:N,
 *                          max_ids:127,rescan:N,virt_ctr:N,hdr_channel:0,
 *                          shared_access:Y,probe_eisa_isa:N,force_dma32:N".
 * Here is another example: "gdth=reserve_list:0,1,2,0,0,1,3,0,rescan:Y".
 * 
 * When loading the gdth driver as a module, the same options are available. 
 * You can set the IRQs with "IRQ=...". However, the syntax to specify the
 * options changes slightly. You must replace all ',' between options 
 * with ' ' and all ':' with '=' and you must use 
 * '1' in place of 'Y' and '0' in place of 'N'.
 * 
 * Default: "modprobe gdth disable=0 reserve_mode=1 reverse_scan=0
 *           max_ids=127 rescan=0 virt_ctr=0 hdr_channel=0 shared_access=0 
 *           probe_eisa_isa=0 force_dma32=0"
 * The other example: "modprobe gdth reserve_list=0,1,2,0,0,1,3,0 rescan=1".
 */

/* The meaning of the Scsi_Pointer members in this driver is as follows:
 * ptr:                     Chaining
 * this_residual:           Command priority
 * buffer:                  phys. DMA sense buffer 
 * dma_handle:              phys. DMA buffer (kernel >= 2.4.0)
 * buffers_residual:        Timeout value
 * Status:                  Command status (gdth_do_cmd()), DMA mem. mappings
 * Message:                 Additional info (gdth_do_cmd()), DMA direction
 * have_data_in:            Flag for gdth_wait_completion()
 * sent_command:            Opcode special command
 * phase:                   Service/parameter/return code special command
 */


/* interrupt coalescing */
/* #define INT_COAL */

/* statistics */
#define GDTH_STATISTICS

#include <linux/module.h>

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/timer.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,6)
#include <linux/dma-mapping.h>
#else
#define DMA_32BIT_MASK	0x00000000ffffffffULL
#define DMA_64BIT_MASK	0xffffffffffffffffULL
#endif

#ifdef GDTH_RTC
#include <linux/mc146818rtc.h>
#endif
#include <linux/reboot.h>

#include <asm/dma.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/spinlock.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <linux/blkdev.h>
#else
#include <linux/blk.h>
#include "sd.h"
#endif

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "gdth_kcompat.h"
#include "gdth.h"

static void gdth_delay(int milliseconds);
static void gdth_eval_mapping(ulong32 size, ulong32 *cyls, int *heads, int *secs);
static irqreturn_t gdth_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int gdth_sync_event(int hanum,int service,unchar index,Scsi_Cmnd *scp);
static int gdth_async_event(int hanum);
static void gdth_log_event(gdth_evt_data *dvr, char *buffer);

static void gdth_putq(int hanum,Scsi_Cmnd *scp,unchar priority);
static void gdth_next(int hanum);
static int gdth_fill_raw_cmd(int hanum,Scsi_Cmnd *scp,unchar b);
static int gdth_special_cmd(int hanum,Scsi_Cmnd *scp);
static gdth_evt_str *gdth_store_event(gdth_ha_str *ha, ushort source,
                                      ushort idx, gdth_evt_data *evt);
static int gdth_read_event(gdth_ha_str *ha, int handle, gdth_evt_str *estr);
static void gdth_readapp_event(gdth_ha_str *ha, unchar application, 
                               gdth_evt_str *estr);
static void gdth_clear_events(void);

static void gdth_copy_internal_data(int hanum,Scsi_Cmnd *scp,
                                    char *buffer,ushort count);
static int gdth_internal_cache_cmd(int hanum,Scsi_Cmnd *scp);
static int gdth_fill_cache_cmd(int hanum,Scsi_Cmnd *scp,ushort hdrive);

static int gdth_search_eisa(ushort eisa_adr);
static int gdth_search_isa(ulong32 bios_adr);
static int gdth_search_pci(gdth_pci_str *pcistr);
static void gdth_search_dev(gdth_pci_str *pcistr, ushort *cnt, 
                            ushort vendor, ushort dev);
static void gdth_sort_pci(gdth_pci_str *pcistr, int cnt);
static int gdth_init_eisa(ushort eisa_adr,gdth_ha_str *ha);
static int gdth_init_isa(ulong32 bios_adr,gdth_ha_str *ha);
static int gdth_init_pci(gdth_pci_str *pcistr,gdth_ha_str *ha);

static void gdth_enable_int(int hanum);
static int gdth_get_status(unchar *pIStatus,int irq);
static int gdth_test_busy(int hanum);
static int gdth_get_cmd_index(int hanum);
static void gdth_release_event(int hanum);
static int gdth_wait(int hanum,int index,ulong32 time);
static int gdth_internal_cmd(int hanum,unchar service,ushort opcode,ulong32 p1,
                             ulong64 p2,ulong64 p3);
static int gdth_search_drives(int hanum);
static int gdth_analyse_hdrive(int hanum, ushort hdrive);

static const char *gdth_ctr_name(int hanum);

static int gdth_open(struct inode *inode, struct file *filep);
static int gdth_close(struct inode *inode, struct file *filep);
static int gdth_ioctl(struct inode *inode, struct file *filep,
                      unsigned int cmd, unsigned long arg);

static void gdth_flush(int hanum);
static int gdth_halt(struct notifier_block *nb, ulong event, void *buf);
static int gdth_queuecommand(Scsi_Cmnd *scp,void (*done)(Scsi_Cmnd *));
static void gdth_scsi_done(struct scsi_cmnd *scp);

#ifdef DEBUG_GDTH
static unchar   DebugState = DEBUG_GDTH;

#ifdef __SERIAL__
#define MAX_SERBUF 160
static void ser_init(void);
static void ser_puts(char *str);
static void ser_putc(char c);
static int  ser_printk(const char *fmt, ...);
static char strbuf[MAX_SERBUF+1];
#ifdef __COM2__
#define COM_BASE 0x2f8
#else
#define COM_BASE 0x3f8
#endif
static void ser_init()
{
    unsigned port=COM_BASE;

    outb(0x80,port+3);
    outb(0,port+1);
    /* 19200 Baud, if 9600: outb(12,port) */
    outb(6, port);
    outb(3,port+3);
    outb(0,port+1);
    /*
    ser_putc('I');
    ser_putc(' ');
    */
}

static void ser_puts(char *str)
{
    char *ptr;

    ser_init();
    for (ptr=str;*ptr;++ptr)
        ser_putc(*ptr);
}

static void ser_putc(char c)
{
    unsigned port=COM_BASE;

    while ((inb(port+5) & 0x20)==0);
    outb(c,port);
    if (c==0x0a)
    {
        while ((inb(port+5) & 0x20)==0);
        outb(0x0d,port);
    }
}

static int ser_printk(const char *fmt, ...)
{
    va_list args;
    int i;

    va_start(args,fmt);
    i = vsprintf(strbuf,fmt,args);
    ser_puts(strbuf);
    va_end(args);
    return i;
}

#define TRACE(a)    {if (DebugState==1) {ser_printk a;}}
#define TRACE2(a)   {if (DebugState==1 || DebugState==2) {ser_printk a;}}
#define TRACE3(a)   {if (DebugState!=0) {ser_printk a;}}

#else /* !__SERIAL__ */
#define TRACE(a)    {if (DebugState==1) {printk a;}}
#define TRACE2(a)   {if (DebugState==1 || DebugState==2) {printk a;}}
#define TRACE3(a)   {if (DebugState!=0) {printk a;}}
#endif

#else /* !DEBUG */
#define TRACE(a)
#define TRACE2(a)
#define TRACE3(a)
#endif

#ifdef GDTH_STATISTICS
static ulong32 max_rq=0, max_index=0, max_sg=0;
#ifdef INT_COAL
static ulong32 max_int_coal=0;
#endif
static ulong32 act_ints=0, act_ios=0, act_stats=0, act_rq=0;
static struct timer_list gdth_timer;
#endif

#define PTR2USHORT(a)   (ushort)(ulong)(a)
#define GDTOFFSOF(a,b)  (size_t)&(((a*)0)->b)
#define INDEX_OK(i,t)   ((i)<ARRAY_SIZE(t))

#define NUMDATA(a)      ( (gdth_num_str  *)((a)->hostdata))
#define HADATA(a)       (&((gdth_ext_str *)((a)->hostdata))->haext)
#define CMDDATA(a)      (&((gdth_ext_str *)((a)->hostdata))->cmdext)

#define BUS_L2P(a,b)    ((b)>(a)->virt_bus ? (b-1):(b))

#define gdth_readb(addr)        readb(addr)
#define gdth_readw(addr)        readw(addr)
#define gdth_readl(addr)        readl(addr)
#define gdth_writeb(b,addr)     writeb((b),(addr))
#define gdth_writew(b,addr)     writew((b),(addr))
#define gdth_writel(b,addr)     writel((b),(addr))

static unchar   gdth_drq_tab[4] = {5,6,7,7};            /* DRQ table */
static unchar   gdth_irq_tab[6] = {0,10,11,12,14,0};    /* IRQ table */
static unchar   gdth_polling;                           /* polling if TRUE */
static unchar   gdth_from_wait  = FALSE;                /* gdth_wait() */
static int      wait_index,wait_hanum;                  /* gdth_wait() */
static int      gdth_ctr_count  = 0;                    /* controller count */
static int      gdth_ctr_vcount = 0;                    /* virt. ctr. count */
static int      gdth_ctr_released = 0;                  /* gdth_release() */
static struct Scsi_Host *gdth_ctr_tab[MAXHA];           /* controller table */
static struct Scsi_Host *gdth_ctr_vtab[MAXHA*MAXBUS];   /* virt. ctr. table */
static unchar   gdth_write_through = FALSE;             /* write through */
static gdth_evt_str ebuffer[MAX_EVENTS];                /* event buffer */
static int elastidx;
static int eoldidx;
static int major;

#define DIN     1                               /* IN data direction */
#define DOU     2                               /* OUT data direction */
#define DNO     DIN                             /* no data transfer */
#define DUN     DIN                             /* unknown data direction */
static unchar gdth_direction_tab[0x100] = {
    DNO,DNO,DIN,DIN,DOU,DIN,DIN,DOU,DIN,DUN,DOU,DOU,DUN,DUN,DUN,DIN,
    DNO,DIN,DIN,DOU,DIN,DOU,DNO,DNO,DOU,DNO,DIN,DNO,DIN,DOU,DNO,DUN,
    DIN,DUN,DIN,DUN,DOU,DIN,DUN,DUN,DIN,DIN,DOU,DNO,DUN,DIN,DOU,DOU,
    DOU,DOU,DOU,DNO,DIN,DNO,DNO,DIN,DOU,DOU,DOU,DOU,DIN,DOU,DIN,DOU,
    DOU,DOU,DIN,DIN,DIN,DNO,DUN,DNO,DNO,DNO,DUN,DNO,DOU,DIN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DOU,DUN,DUN,DUN,DUN,DIN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DIN,DUN,DOU,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DIN,DUN,
    DUN,DUN,DUN,DUN,DUN,DNO,DNO,DUN,DIN,DNO,DOU,DUN,DNO,DUN,DOU,DOU,
    DOU,DOU,DOU,DNO,DUN,DIN,DOU,DIN,DIN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DOU,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DOU,DUN,DUN,DUN,DUN,DUN,
    DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN,DUN
};

/* LILO and modprobe/insmod parameters */
/* IRQ list for GDT3000/3020 EISA controllers */
static int irq[MAXHA] __initdata = 
{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
/* disable driver flag */
static int disable __initdata = 0;
/* reserve flag */
static int reserve_mode = 1;                  
/* reserve list */
static int reserve_list[MAX_RES_ARGS] = 
{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
/* scan order for PCI controllers */
static int reverse_scan = 0;
/* virtual channel for the host drives */
static int hdr_channel = 0;
/* max. IDs per channel */
static int max_ids = MAXID;
/* rescan all IDs */
static int rescan = 0;
/* map channels to virtual controllers */
static int virt_ctr = 0;
/* shared access */
static int shared_access = 1;
/* enable support for EISA and ISA controllers */
static int probe_eisa_isa = 0;
/* 64 bit DMA mode, support for drives > 2 TB, if force_dma32 = 0 */
static int force_dma32 = 0;

/* parameters for modprobe/insmod */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
module_param_array(irq, int, NULL, 0);
module_param(disable, int, 0);
module_param(reserve_mode, int, 0);
module_param_array(reserve_list, int, NULL, 0);
module_param(reverse_scan, int, 0);
module_param(hdr_channel, int, 0);
module_param(max_ids, int, 0);
module_param(rescan, int, 0);
module_param(virt_ctr, int, 0);
module_param(shared_access, int, 0);
module_param(probe_eisa_isa, int, 0);
module_param(force_dma32, int, 0);
#else
MODULE_PARM(irq, "i");
MODULE_PARM(disable, "i");
MODULE_PARM(reserve_mode, "i");
MODULE_PARM(reserve_list, "4-" __MODULE_STRING(MAX_RES_ARGS) "i");
MODULE_PARM(reverse_scan, "i");
MODULE_PARM(hdr_channel, "i");
MODULE_PARM(max_ids, "i");
MODULE_PARM(rescan, "i");
MODULE_PARM(virt_ctr, "i");
MODULE_PARM(shared_access, "i");
MODULE_PARM(probe_eisa_isa, "i");
MODULE_PARM(force_dma32, "i");
#endif
MODULE_AUTHOR("Achim Leubner");
MODULE_LICENSE("GPL");

/* ioctl interface */
static struct file_operations gdth_fops = {
    .ioctl   = gdth_ioctl,
    .open    = gdth_open,
    .release = gdth_close,
};

#include "gdth_proc.h"
#include "gdth_proc.c"

/* notifier block to get a notify on system shutdown/halt/reboot */
static struct notifier_block gdth_notifier = {
    gdth_halt, NULL, 0
};
static int notifier_disabled = 0;

static void gdth_delay(int milliseconds)
{
    if (milliseconds == 0) {
        udelay(1);
    } else {
        mdelay(milliseconds);
    }
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static void gdth_scsi_done(struct scsi_cmnd *scp)
{
    TRACE2(("gdth_scsi_done()\n"));

    if (scp->sc_request)
        complete((struct completion *)scp->sc_request);
}

int __gdth_execute(struct scsi_device *sdev, gdth_cmd_str *gdtcmd, char *cmnd,
                   int timeout, u32 *info)
{
    Scsi_Cmnd *scp;
    DECLARE_COMPLETION(wait);
    int rval;

    scp = kmalloc(sizeof(*scp), GFP_KERNEL);
    if (!scp)
        return -ENOMEM;
    memset(scp, 0, sizeof(*scp));
    scp->device = sdev;
    /* use sc_request field to save the ptr. to completion struct. */
    scp->sc_request = (struct scsi_request *)&wait;
    scp->timeout_per_command = timeout*HZ;
    scp->request_buffer = gdtcmd;
    scp->cmd_len = 12;
    memcpy(scp->cmnd, cmnd, 12);
    scp->SCp.this_residual = IOCTL_PRI;   /* priority */
    scp->done = gdth_scsi_done; /* some fn. test this */
    gdth_queuecommand(scp, gdth_scsi_done);
    wait_for_completion(&wait);

    rval = scp->SCp.Status;
    if (info)
        *info = scp->SCp.Message;
    kfree(scp);
    return rval;
}
#else
static void gdth_scsi_done(Scsi_Cmnd *scp)
{
    TRACE2(("gdth_scsi_done()\n"));

    scp->request.rq_status = RQ_SCSI_DONE;
    if (scp->request.waiting)
        complete(scp->request.waiting);
}

int __gdth_execute(struct scsi_device *sdev, gdth_cmd_str *gdtcmd, char *cmnd,
                   int timeout, u32 *info)
{
    Scsi_Cmnd *scp = scsi_allocate_device(sdev, 1, FALSE);
    unsigned bufflen = gdtcmd ? sizeof(gdth_cmd_str) : 0;
    DECLARE_COMPLETION(wait);
    int rval;

    if (!scp)
        return -ENOMEM;
    scp->cmd_len = 12;
    scp->use_sg = 0;
    scp->SCp.this_residual = IOCTL_PRI;   /* priority */
    scp->request.rq_status = RQ_SCSI_BUSY;
    scp->request.waiting = &wait;
    scsi_do_cmd(scp, cmnd, gdtcmd, bufflen, gdth_scsi_done, timeout*HZ, 1);
    wait_for_completion(&wait);

    rval = scp->SCp.Status;
    if (info)
        *info = scp->SCp.Message;

    scsi_release_command(scp);
    return rval;
}
#endif

int gdth_execute(struct Scsi_Host *shost, gdth_cmd_str *gdtcmd, char *cmnd,
                 int timeout, u32 *info)
{
    struct scsi_device *sdev = scsi_get_host_dev(shost);
    int rval = __gdth_execute(sdev, gdtcmd, cmnd, timeout, info);

    scsi_free_host_dev(sdev);
    return rval;
}

static void gdth_eval_mapping(ulong32 size, ulong32 *cyls, int *heads, int *secs)
{
    *cyls = size /HEADS/SECS;
    if (*cyls <= MAXCYLS) {
        *heads = HEADS;
        *secs = SECS;
    } else {                                        /* too high for 64*32 */
        *cyls = size /MEDHEADS/MEDSECS;
        if (*cyls <= MAXCYLS) {
            *heads = MEDHEADS;
            *secs = MEDSECS;
        } else {                                    /* too high for 127*63 */
            *cyls = size /BIGHEADS/BIGSECS;
            *heads = BIGHEADS;
            *secs = BIGSECS;
        }
    }
}

/* controller search and initialization functions */

static int __init gdth_search_eisa(ushort eisa_adr)
{
    ulong32 id;
    
    TRACE(("gdth_search_eisa() adr. %x\n",eisa_adr));
    id = inl(eisa_adr+ID0REG);
    if (id == GDT3A_ID || id == GDT3B_ID) {     /* GDT3000A or GDT3000B */
        if ((inb(eisa_adr+EISAREG) & 8) == 0)   
            return 0;                           /* not EISA configured */
        return 1;
    }
    if (id == GDT3_ID)                          /* GDT3000 */
        return 1;

    return 0;                                   
}


static int __init gdth_search_isa(ulong32 bios_adr)
{
    void __iomem *addr;
    ulong32 id;

    TRACE(("gdth_search_isa() bios adr. %x\n",bios_adr));
    if ((addr = ioremap(bios_adr+BIOS_ID_OFFS, sizeof(ulong32))) != NULL) {
        id = gdth_readl(addr);
        iounmap(addr);
        if (id == GDT2_ID)                          /* GDT2000 */
            return 1;
    }
    return 0;
}


static int __init gdth_search_pci(gdth_pci_str *pcistr)
{
    ushort device, cnt;
    
    TRACE(("gdth_search_pci()\n"));

    cnt = 0;
    for (device = 0; device <= PCI_DEVICE_ID_VORTEX_GDT6555; ++device)
        gdth_search_dev(pcistr, &cnt, PCI_VENDOR_ID_VORTEX, device);
    for (device = PCI_DEVICE_ID_VORTEX_GDT6x17RP; 
         device <= PCI_DEVICE_ID_VORTEX_GDTMAXRP; ++device)
        gdth_search_dev(pcistr, &cnt, PCI_VENDOR_ID_VORTEX, device);
    gdth_search_dev(pcistr, &cnt, PCI_VENDOR_ID_VORTEX, 
                    PCI_DEVICE_ID_VORTEX_GDTNEWRX);
    gdth_search_dev(pcistr, &cnt, PCI_VENDOR_ID_VORTEX, 
                    PCI_DEVICE_ID_VORTEX_GDTNEWRX2);
    gdth_search_dev(pcistr, &cnt, PCI_VENDOR_ID_INTEL,
                    PCI_DEVICE_ID_INTEL_SRC);
    gdth_search_dev(pcistr, &cnt, PCI_VENDOR_ID_INTEL,
                    PCI_DEVICE_ID_INTEL_SRC_XSCALE);
    return cnt;
}

/* Vortex only makes RAID controllers.
 * We do not really want to specify all 550 ids here, so wildcard match.
 */
static struct pci_device_id gdthtable[] __attribute_used__ = {
    {PCI_VENDOR_ID_VORTEX,PCI_ANY_ID,PCI_ANY_ID, PCI_ANY_ID},
    {PCI_VENDOR_ID_INTEL,PCI_DEVICE_ID_INTEL_SRC,PCI_ANY_ID,PCI_ANY_ID}, 
    {PCI_VENDOR_ID_INTEL,PCI_DEVICE_ID_INTEL_SRC_XSCALE,PCI_ANY_ID,PCI_ANY_ID}, 
    {0}
};
MODULE_DEVICE_TABLE(pci,gdthtable);

static void __init gdth_search_dev(gdth_pci_str *pcistr, ushort *cnt,
                                   ushort vendor, ushort device)
{
    ulong base0, base1, base2;
    struct pci_dev *pdev;
    
    TRACE(("gdth_search_dev() cnt %d vendor %x device %x\n",
          *cnt, vendor, device));

    pdev = NULL;
    while ((pdev = pci_find_device(vendor, device, pdev)) 
           != NULL) {
        if (pci_enable_device(pdev))
            continue;
        if (*cnt >= MAXHA)
            return;
        /* GDT PCI controller found, resources are already in pdev */
        pcistr[*cnt].pdev = pdev;
        pcistr[*cnt].vendor_id = vendor;
        pcistr[*cnt].device_id = device;
        pcistr[*cnt].subdevice_id = pdev->subsystem_device;
        pcistr[*cnt].bus = pdev->bus->number;
        pcistr[*cnt].device_fn = pdev->devfn;
        pcistr[*cnt].irq = pdev->irq;
        base0 = pci_resource_flags(pdev, 0);
        base1 = pci_resource_flags(pdev, 1);
        base2 = pci_resource_flags(pdev, 2);
        if (device <= PCI_DEVICE_ID_VORTEX_GDT6000B ||   /* GDT6000/B */
            device >= PCI_DEVICE_ID_VORTEX_GDT6x17RP) {  /* MPR */
            if (!(base0 & IORESOURCE_MEM)) 
                continue;
            pcistr[*cnt].dpmem = pci_resource_start(pdev, 0);
        } else {                                  /* GDT6110, GDT6120, .. */
            if (!(base0 & IORESOURCE_MEM) ||
                !(base2 & IORESOURCE_MEM) ||
                !(base1 & IORESOURCE_IO)) 
                continue;
            pcistr[*cnt].dpmem = pci_resource_start(pdev, 2);
            pcistr[*cnt].io_mm = pci_resource_start(pdev, 0);
            pcistr[*cnt].io    = pci_resource_start(pdev, 1);
        }
        TRACE2(("Controller found at %d/%d, irq %d, dpmem 0x%lx\n",
                pcistr[*cnt].bus, PCI_SLOT(pcistr[*cnt].device_fn), 
                pcistr[*cnt].irq, pcistr[*cnt].dpmem));
        (*cnt)++;
    }       
}   


static void __init gdth_sort_pci(gdth_pci_str *pcistr, int cnt)
{    
    gdth_pci_str temp;
    int i, changed;
    
    TRACE(("gdth_sort_pci() cnt %d\n",cnt));
    if (cnt == 0)
        return;

    do {
        changed = FALSE;
        for (i = 0; i < cnt-1; ++i) {
            if (!reverse_scan) {
                if ((pcistr[i].bus > pcistr[i+1].bus) ||
                    (pcistr[i].bus == pcistr[i+1].bus &&
                     PCI_SLOT(pcistr[i].device_fn) > 
                     PCI_SLOT(pcistr[i+1].device_fn))) {
                    temp = pcistr[i];
                    pcistr[i] = pcistr[i+1];
                    pcistr[i+1] = temp;
                    changed = TRUE;
                }
            } else {
                if ((pcistr[i].bus < pcistr[i+1].bus) ||
                    (pcistr[i].bus == pcistr[i+1].bus &&
                     PCI_SLOT(pcistr[i].device_fn) < 
                     PCI_SLOT(pcistr[i+1].device_fn))) {
                    temp = pcistr[i];
                    pcistr[i] = pcistr[i+1];
                    pcistr[i+1] = temp;
                    changed = TRUE;
                }
            }
        }
    } while (changed);
}


static int __init gdth_init_eisa(ushort eisa_adr,gdth_ha_str *ha)
{
    ulong32 retries,id;
    unchar prot_ver,eisacf,i,irq_found;

    TRACE(("gdth_init_eisa() adr. %x\n",eisa_adr));
    
    /* disable board interrupts, deinitialize services */
    outb(0xff,eisa_adr+EDOORREG);
    outb(0x00,eisa_adr+EDENABREG);
    outb(0x00,eisa_adr+EINTENABREG);
    
    outb(0xff,eisa_adr+LDOORREG);
    retries = INIT_RETRIES;
    gdth_delay(20);
    while (inb(eisa_adr+EDOORREG) != 0xff) {
        if (--retries == 0) {
            printk("GDT-EISA: Initialization error (DEINIT failed)\n");
            return 0;
        }
        gdth_delay(1);
        TRACE2(("wait for DEINIT: retries=%d\n",retries));
    }
    prot_ver = inb(eisa_adr+MAILBOXREG);
    outb(0xff,eisa_adr+EDOORREG);
    if (prot_ver != PROTOCOL_VERSION) {
        printk("GDT-EISA: Illegal protocol version\n");
        return 0;
    }
    ha->bmic = eisa_adr;
    ha->brd_phys = (ulong32)eisa_adr >> 12;

    outl(0,eisa_adr+MAILBOXREG);
    outl(0,eisa_adr+MAILBOXREG+4);
    outl(0,eisa_adr+MAILBOXREG+8);
    outl(0,eisa_adr+MAILBOXREG+12);

    /* detect IRQ */ 
    if ((id = inl(eisa_adr+ID0REG)) == GDT3_ID) {
        ha->oem_id = OEM_ID_ICP;
        ha->type = GDT_EISA;
        ha->stype = id;
        outl(1,eisa_adr+MAILBOXREG+8);
        outb(0xfe,eisa_adr+LDOORREG);
        retries = INIT_RETRIES;
        gdth_delay(20);
        while (inb(eisa_adr+EDOORREG) != 0xfe) {
            if (--retries == 0) {
                printk("GDT-EISA: Initialization error (get IRQ failed)\n");
                return 0;
            }
            gdth_delay(1);
        }
        ha->irq = inb(eisa_adr+MAILBOXREG);
        outb(0xff,eisa_adr+EDOORREG);
        TRACE2(("GDT3000/3020: IRQ=%d\n",ha->irq));
        /* check the result */
        if (ha->irq == 0) {
                TRACE2(("Unknown IRQ, use IRQ table from cmd line !\n"));
                for (i = 0, irq_found = FALSE; 
                     i < MAXHA && irq[i] != 0xff; ++i) {
                if (irq[i]==10 || irq[i]==11 || irq[i]==12 || irq[i]==14) {
                    irq_found = TRUE;
                    break;
                }
                }
            if (irq_found) {
                ha->irq = irq[i];
                irq[i] = 0;
                printk("GDT-EISA: Can not detect controller IRQ,\n");
                printk("Use IRQ setting from command line (IRQ = %d)\n",
                       ha->irq);
            } else {
                printk("GDT-EISA: Initialization error (unknown IRQ), Enable\n");
                printk("the controller BIOS or use command line parameters\n");
                return 0;
            }
        }
    } else {
        eisacf = inb(eisa_adr+EISAREG) & 7;
        if (eisacf > 4)                         /* level triggered */
            eisacf -= 4;
        ha->irq = gdth_irq_tab[eisacf];
        ha->oem_id = OEM_ID_ICP;
        ha->type = GDT_EISA;
        ha->stype = id;
    }

    ha->dma64_support = 0;
    return 1;
}

       
static int __init gdth_init_isa(ulong32 bios_adr,gdth_ha_str *ha)
{
    register gdt2_dpram_str __iomem *dp2_ptr;
    int i;
    unchar irq_drq,prot_ver;
    ulong32 retries;

    TRACE(("gdth_init_isa() bios adr. %x\n",bios_adr));

    ha->brd = ioremap(bios_adr, sizeof(gdt2_dpram_str));
    if (ha->brd == NULL) {
        printk("GDT-ISA: Initialization error (DPMEM remap error)\n");
        return 0;
    }
    dp2_ptr = ha->brd;
    gdth_writeb(1, &dp2_ptr->io.memlock); /* switch off write protection */
    /* reset interface area */
    memset_io(&dp2_ptr->u, 0, sizeof(dp2_ptr->u));
    if (gdth_readl(&dp2_ptr->u) != 0) {
        printk("GDT-ISA: Initialization error (DPMEM write error)\n");
        iounmap(ha->brd);
        return 0;
    }

    /* disable board interrupts, read DRQ and IRQ */
    gdth_writeb(0xff, &dp2_ptr->io.irqdel);
    gdth_writeb(0x00, &dp2_ptr->io.irqen);
    gdth_writeb(0x00, &dp2_ptr->u.ic.S_Status);
    gdth_writeb(0x00, &dp2_ptr->u.ic.Cmd_Index);

    irq_drq = gdth_readb(&dp2_ptr->io.rq);
    for (i=0; i<3; ++i) {
        if ((irq_drq & 1)==0)
            break;
        irq_drq >>= 1;
    }
    ha->drq = gdth_drq_tab[i];

    irq_drq = gdth_readb(&dp2_ptr->io.rq) >> 3;
    for (i=1; i<5; ++i) {
        if ((irq_drq & 1)==0)
            break;
        irq_drq >>= 1;
    }
    ha->irq = gdth_irq_tab[i];

    /* deinitialize services */
    gdth_writel(bios_adr, &dp2_ptr->u.ic.S_Info[0]);
    gdth_writeb(0xff, &dp2_ptr->u.ic.S_Cmd_Indx);
    gdth_writeb(0, &dp2_ptr->io.event);
    retries = INIT_RETRIES;
    gdth_delay(20);
    while (gdth_readb(&dp2_ptr->u.ic.S_Status) != 0xff) {
        if (--retries == 0) {
            printk("GDT-ISA: Initialization error (DEINIT failed)\n");
            iounmap(ha->brd);
            return 0;
        }
        gdth_delay(1);
    }
    prot_ver = (unchar)gdth_readl(&dp2_ptr->u.ic.S_Info[0]);
    gdth_writeb(0, &dp2_ptr->u.ic.Status);
    gdth_writeb(0xff, &dp2_ptr->io.irqdel);
    if (prot_ver != PROTOCOL_VERSION) {
        printk("GDT-ISA: Illegal protocol version\n");
        iounmap(ha->brd);
        return 0;
    }

    ha->oem_id = OEM_ID_ICP;
    ha->type = GDT_ISA;
    ha->ic_all_size = sizeof(dp2_ptr->u);
    ha->stype= GDT2_ID;
    ha->brd_phys = bios_adr >> 4;

    /* special request to controller BIOS */
    gdth_writel(0x00, &dp2_ptr->u.ic.S_Info[0]);
    gdth_writel(0x00, &dp2_ptr->u.ic.S_Info[1]);
    gdth_writel(0x01, &dp2_ptr->u.ic.S_Info[2]);
    gdth_writel(0x00, &dp2_ptr->u.ic.S_Info[3]);
    gdth_writeb(0xfe, &dp2_ptr->u.ic.S_Cmd_Indx);
    gdth_writeb(0, &dp2_ptr->io.event);
    retries = INIT_RETRIES;
    gdth_delay(20);
    while (gdth_readb(&dp2_ptr->u.ic.S_Status) != 0xfe) {
        if (--retries == 0) {
            printk("GDT-ISA: Initialization error\n");
            iounmap(ha->brd);
            return 0;
        }
        gdth_delay(1);
    }
    gdth_writeb(0, &dp2_ptr->u.ic.Status);
    gdth_writeb(0xff, &dp2_ptr->io.irqdel);

    ha->dma64_support = 0;
    return 1;
}


static int __init gdth_init_pci(gdth_pci_str *pcistr,gdth_ha_str *ha)
{
    register gdt6_dpram_str __iomem *dp6_ptr;
    register gdt6c_dpram_str __iomem *dp6c_ptr;
    register gdt6m_dpram_str __iomem *dp6m_ptr;
    ulong32 retries;
    unchar prot_ver;
    ushort command;
    int i, found = FALSE;

    TRACE(("gdth_init_pci()\n"));

    if (pcistr->vendor_id == PCI_VENDOR_ID_INTEL)
        ha->oem_id = OEM_ID_INTEL;
    else
        ha->oem_id = OEM_ID_ICP;
    ha->brd_phys = (pcistr->bus << 8) | (pcistr->device_fn & 0xf8);
    ha->stype = (ulong32)pcistr->device_id;
    ha->subdevice_id = pcistr->subdevice_id;
    ha->irq = pcistr->irq;
    ha->pdev = pcistr->pdev;
    
    if (ha->stype <= PCI_DEVICE_ID_VORTEX_GDT6000B) {  /* GDT6000/B */
        TRACE2(("init_pci() dpmem %lx irq %d\n",pcistr->dpmem,ha->irq));
        ha->brd = ioremap(pcistr->dpmem, sizeof(gdt6_dpram_str));
        if (ha->brd == NULL) {
            printk("GDT-PCI: Initialization error (DPMEM remap error)\n");
            return 0;
        }
        /* check and reset interface area */
        dp6_ptr = ha->brd;
        gdth_writel(DPMEM_MAGIC, &dp6_ptr->u);
        if (gdth_readl(&dp6_ptr->u) != DPMEM_MAGIC) {
            printk("GDT-PCI: Cannot access DPMEM at 0x%lx (shadowed?)\n", 
                   pcistr->dpmem);
            found = FALSE;
            for (i = 0xC8000; i < 0xE8000; i += 0x4000) {
                iounmap(ha->brd);
                ha->brd = ioremap(i, sizeof(ushort)); 
                if (ha->brd == NULL) {
                    printk("GDT-PCI: Initialization error (DPMEM remap error)\n");
                    return 0;
                }
                if (gdth_readw(ha->brd) != 0xffff) {
                    TRACE2(("init_pci_old() address 0x%x busy\n", i));
                    continue;
                }
                iounmap(ha->brd);
                pci_write_config_dword(pcistr->pdev, 
                                       PCI_BASE_ADDRESS_0, i);
                ha->brd = ioremap(i, sizeof(gdt6_dpram_str)); 
                if (ha->brd == NULL) {
                    printk("GDT-PCI: Initialization error (DPMEM remap error)\n");
                    return 0;
                }
                dp6_ptr = ha->brd;
                gdth_writel(DPMEM_MAGIC, &dp6_ptr->u);
                if (gdth_readl(&dp6_ptr->u) == DPMEM_MAGIC) {
                    printk("GDT-PCI: Use free address at 0x%x\n", i);
                    found = TRUE;
                    break;
                }
            }   
            if (!found) {
                printk("GDT-PCI: No free address found!\n");
                iounmap(ha->brd);
                return 0;
            }
        }
        memset_io(&dp6_ptr->u, 0, sizeof(dp6_ptr->u));
        if (gdth_readl(&dp6_ptr->u) != 0) {
            printk("GDT-PCI: Initialization error (DPMEM write error)\n");
            iounmap(ha->brd);
            return 0;
        }
        
        /* disable board interrupts, deinit services */
        gdth_writeb(0xff, &dp6_ptr->io.irqdel);
        gdth_writeb(0x00, &dp6_ptr->io.irqen);
        gdth_writeb(0x00, &dp6_ptr->u.ic.S_Status);
        gdth_writeb(0x00, &dp6_ptr->u.ic.Cmd_Index);

        gdth_writel(pcistr->dpmem, &dp6_ptr->u.ic.S_Info[0]);
        gdth_writeb(0xff, &dp6_ptr->u.ic.S_Cmd_Indx);
        gdth_writeb(0, &dp6_ptr->io.event);
        retries = INIT_RETRIES;
        gdth_delay(20);
        while (gdth_readb(&dp6_ptr->u.ic.S_Status) != 0xff) {
            if (--retries == 0) {
                printk("GDT-PCI: Initialization error (DEINIT failed)\n");
                iounmap(ha->brd);
                return 0;
            }
            gdth_delay(1);
        }
        prot_ver = (unchar)gdth_readl(&dp6_ptr->u.ic.S_Info[0]);
        gdth_writeb(0, &dp6_ptr->u.ic.S_Status);
        gdth_writeb(0xff, &dp6_ptr->io.irqdel);
        if (prot_ver != PROTOCOL_VERSION) {
            printk("GDT-PCI: Illegal protocol version\n");
            iounmap(ha->brd);
            return 0;
        }

        ha->type = GDT_PCI;
        ha->ic_all_size = sizeof(dp6_ptr->u);
        
        /* special command to controller BIOS */
        gdth_writel(0x00, &dp6_ptr->u.ic.S_Info[0]);
        gdth_writel(0x00, &dp6_ptr->u.ic.S_Info[1]);
        gdth_writel(0x00, &dp6_ptr->u.ic.S_Info[2]);
        gdth_writel(0x00, &dp6_ptr->u.ic.S_Info[3]);
        gdth_writeb(0xfe, &dp6_ptr->u.ic.S_Cmd_Indx);
        gdth_writeb(0, &dp6_ptr->io.event);
        retries = INIT_RETRIES;
        gdth_delay(20);
        while (gdth_readb(&dp6_ptr->u.ic.S_Status) != 0xfe) {
            if (--retries == 0) {
                printk("GDT-PCI: Initialization error\n");
                iounmap(ha->brd);
                return 0;
            }
            gdth_delay(1);
        }
        gdth_writeb(0, &dp6_ptr->u.ic.S_Status);
        gdth_writeb(0xff, &dp6_ptr->io.irqdel);

        ha->dma64_support = 0;

    } else if (ha->stype <= PCI_DEVICE_ID_VORTEX_GDT6555) { /* GDT6110, ... */
        ha->plx = (gdt6c_plx_regs *)pcistr->io;
        TRACE2(("init_pci_new() dpmem %lx irq %d\n",
            pcistr->dpmem,ha->irq));
        ha->brd = ioremap(pcistr->dpmem, sizeof(gdt6c_dpram_str));
        if (ha->brd == NULL) {
            printk("GDT-PCI: Initialization error (DPMEM remap error)\n");
            iounmap(ha->brd);
            return 0;
        }
        /* check and reset interface area */
        dp6c_ptr = ha->brd;
        gdth_writel(DPMEM_MAGIC, &dp6c_ptr->u);
        if (gdth_readl(&dp6c_ptr->u) != DPMEM_MAGIC) {
            printk("GDT-PCI: Cannot access DPMEM at 0x%lx (shadowed?)\n", 
                   pcistr->dpmem);
            found = FALSE;
            for (i = 0xC8000; i < 0xE8000; i += 0x4000) {
                iounmap(ha->brd);
                ha->brd = ioremap(i, sizeof(ushort)); 
                if (ha->brd == NULL) {
                    printk("GDT-PCI: Initialization error (DPMEM remap error)\n");
                    return 0;
                }
                if (gdth_readw(ha->brd) != 0xffff) {
                    TRACE2(("init_pci_plx() address 0x%x busy\n", i));
                    continue;
                }
                iounmap(ha->brd);
                pci_write_config_dword(pcistr->pdev, 
                                       PCI_BASE_ADDRESS_2, i);
                ha->brd = ioremap(i, sizeof(gdt6c_dpram_str)); 
                if (ha->brd == NULL) {
                    printk("GDT-PCI: Initialization error (DPMEM remap error)\n");
                    return 0;
                }
                dp6c_ptr = ha->brd;
                gdth_writel(DPMEM_MAGIC, &dp6c_ptr->u);
                if (gdth_readl(&dp6c_ptr->u) == DPMEM_MAGIC) {
                    printk("GDT-PCI: Use free address at 0x%x\n", i);
                    found = TRUE;
                    break;
                }
            }   
            if (!found) {
                printk("GDT-PCI: No free address found!\n");
                iounmap(ha->brd);
                return 0;
            }
        }
        memset_io(&dp6c_ptr->u, 0, sizeof(dp6c_ptr->u));
        if (gdth_readl(&dp6c_ptr->u) != 0) {
            printk("GDT-PCI: Initialization error (DPMEM write error)\n");
            iounmap(ha->brd);
            return 0;
        }
        
        /* disable board interrupts, deinit services */
        outb(0x00,PTR2USHORT(&ha->plx->control1));
        outb(0xff,PTR2USHORT(&ha->plx->edoor_reg));
        
        gdth_writeb(0x00, &dp6c_ptr->u.ic.S_Status);
        gdth_writeb(0x00, &dp6c_ptr->u.ic.Cmd_Index);

        gdth_writel(pcistr->dpmem, &dp6c_ptr->u.ic.S_Info[0]);
        gdth_writeb(0xff, &dp6c_ptr->u.ic.S_Cmd_Indx);

        outb(1,PTR2USHORT(&ha->plx->ldoor_reg));

        retries = INIT_RETRIES;
        gdth_delay(20);
        while (gdth_readb(&dp6c_ptr->u.ic.S_Status) != 0xff) {
            if (--retries == 0) {
                printk("GDT-PCI: Initialization error (DEINIT failed)\n");
                iounmap(ha->brd);
                return 0;
            }
            gdth_delay(1);
        }
        prot_ver = (unchar)gdth_readl(&dp6c_ptr->u.ic.S_Info[0]);
        gdth_writeb(0, &dp6c_ptr->u.ic.Status);
        if (prot_ver != PROTOCOL_VERSION) {
            printk("GDT-PCI: Illegal protocol version\n");
            iounmap(ha->brd);
            return 0;
        }

        ha->type = GDT_PCINEW;
        ha->ic_all_size = sizeof(dp6c_ptr->u);

        /* special command to controller BIOS */
        gdth_writel(0x00, &dp6c_ptr->u.ic.S_Info[0]);
        gdth_writel(0x00, &dp6c_ptr->u.ic.S_Info[1]);
        gdth_writel(0x00, &dp6c_ptr->u.ic.S_Info[2]);
        gdth_writel(0x00, &dp6c_ptr->u.ic.S_Info[3]);
        gdth_writeb(0xfe, &dp6c_ptr->u.ic.S_Cmd_Indx);
        
        outb(1,PTR2USHORT(&ha->plx->ldoor_reg));

        retries = INIT_RETRIES;
        gdth_delay(20);
        while (gdth_readb(&dp6c_ptr->u.ic.S_Status) != 0xfe) {
            if (--retries == 0) {
                printk("GDT-PCI: Initialization error\n");
                iounmap(ha->brd);
                return 0;
            }
            gdth_delay(1);
        }
        gdth_writeb(0, &dp6c_ptr->u.ic.S_Status);

        ha->dma64_support = 0;

    } else {                                            /* MPR */
        TRACE2(("init_pci_mpr() dpmem %lx irq %d\n",pcistr->dpmem,ha->irq));
        ha->brd = ioremap(pcistr->dpmem, sizeof(gdt6m_dpram_str));
        if (ha->brd == NULL) {
            printk("GDT-PCI: Initialization error (DPMEM remap error)\n");
            return 0;
        }

        /* manipulate config. space to enable DPMEM, start RP controller */
        pci_read_config_word(pcistr->pdev, PCI_COMMAND, &command);
        command |= 6;
        pci_write_config_word(pcistr->pdev, PCI_COMMAND, command);
        if (pci_resource_start(pcistr->pdev, 8) == 1UL)
            pci_resource_start(pcistr->pdev, 8) = 0UL;
        i = 0xFEFF0001UL;
        pci_write_config_dword(pcistr->pdev, PCI_ROM_ADDRESS, i);
        gdth_delay(1);
        pci_write_config_dword(pcistr->pdev, PCI_ROM_ADDRESS,
                               pci_resource_start(pcistr->pdev, 8));
        
        dp6m_ptr = ha->brd;

        /* Ensure that it is safe to access the non HW portions of DPMEM.
         * Aditional check needed for Xscale based RAID controllers */
        while( ((int)gdth_readb(&dp6m_ptr->i960r.sema0_reg) ) & 3 )
            gdth_delay(1);
        
        /* check and reset interface area */
        gdth_writel(DPMEM_MAGIC, &dp6m_ptr->u);
        if (gdth_readl(&dp6m_ptr->u) != DPMEM_MAGIC) {
            printk("GDT-PCI: Cannot access DPMEM at 0x%lx (shadowed?)\n", 
                   pcistr->dpmem);
            found = FALSE;
            for (i = 0xC8000; i < 0xE8000; i += 0x4000) {
                iounmap(ha->brd);
                ha->brd = ioremap(i, sizeof(ushort)); 
                if (ha->brd == NULL) {
                    printk("GDT-PCI: Initialization error (DPMEM remap error)\n");
                    return 0;
                }
                if (gdth_readw(ha->brd) != 0xffff) {
                    TRACE2(("init_pci_mpr() address 0x%x busy\n", i));
                    continue;
                }
                iounmap(ha->brd);
                pci_write_config_dword(pcistr->pdev, 
                                       PCI_BASE_ADDRESS_0, i);
                ha->brd = ioremap(i, sizeof(gdt6m_dpram_str)); 
                if (ha->brd == NULL) {
                    printk("GDT-PCI: Initialization error (DPMEM remap error)\n");
                    return 0;
                }
                dp6m_ptr = ha->brd;
                gdth_writel(DPMEM_MAGIC, &dp6m_ptr->u);
                if (gdth_readl(&dp6m_ptr->u) == DPMEM_MAGIC) {
                    printk("GDT-PCI: Use free address at 0x%x\n", i);
                    found = TRUE;
                    break;
                }
            }   
            if (!found) {
                printk("GDT-PCI: No free address found!\n");
                iounmap(ha->brd);
                return 0;
            }
        }
        memset_io(&dp6m_ptr->u, 0, sizeof(dp6m_ptr->u));
        
        /* disable board interrupts, deinit services */
        gdth_writeb(gdth_readb(&dp6m_ptr->i960r.edoor_en_reg) | 4,
                    &dp6m_ptr->i960r.edoor_en_reg);
        gdth_writeb(0xff, &dp6m_ptr->i960r.edoor_reg);
        gdth_writeb(0x00, &dp6m_ptr->u.ic.S_Status);
        gdth_writeb(0x00, &dp6m_ptr->u.ic.Cmd_Index);

        gdth_writel(pcistr->dpmem, &dp6m_ptr->u.ic.S_Info[0]);
        gdth_writeb(0xff, &dp6m_ptr->u.ic.S_Cmd_Indx);
        gdth_writeb(1, &dp6m_ptr->i960r.ldoor_reg);
        retries = INIT_RETRIES;
        gdth_delay(20);
        while (gdth_readb(&dp6m_ptr->u.ic.S_Status) != 0xff) {
            if (--retries == 0) {
                printk("GDT-PCI: Initialization error (DEINIT failed)\n");
                iounmap(ha->brd);
                return 0;
            }
            gdth_delay(1);
        }
        prot_ver = (unchar)gdth_readl(&dp6m_ptr->u.ic.S_Info[0]);
        gdth_writeb(0, &dp6m_ptr->u.ic.S_Status);
        if (prot_ver != PROTOCOL_VERSION) {
            printk("GDT-PCI: Illegal protocol version\n");
            iounmap(ha->brd);
            return 0;
        }

        ha->type = GDT_PCIMPR;
        ha->ic_all_size = sizeof(dp6m_ptr->u);
        
        /* special command to controller BIOS */
        gdth_writel(0x00, &dp6m_ptr->u.ic.S_Info[0]);
        gdth_writel(0x00, &dp6m_ptr->u.ic.S_Info[1]);
        gdth_writel(0x00, &dp6m_ptr->u.ic.S_Info[2]);
        gdth_writel(0x00, &dp6m_ptr->u.ic.S_Info[3]);
        gdth_writeb(0xfe, &dp6m_ptr->u.ic.S_Cmd_Indx);
        gdth_writeb(1, &dp6m_ptr->i960r.ldoor_reg);
        retries = INIT_RETRIES;
        gdth_delay(20);
        while (gdth_readb(&dp6m_ptr->u.ic.S_Status) != 0xfe) {
            if (--retries == 0) {
                printk("GDT-PCI: Initialization error\n");
                iounmap(ha->brd);
                return 0;
            }
            gdth_delay(1);
        }
        gdth_writeb(0, &dp6m_ptr->u.ic.S_Status);

        /* read FW version to detect 64-bit DMA support */
        gdth_writeb(0xfd, &dp6m_ptr->u.ic.S_Cmd_Indx);
        gdth_writeb(1, &dp6m_ptr->i960r.ldoor_reg);
        retries = INIT_RETRIES;
        gdth_delay(20);
        while (gdth_readb(&dp6m_ptr->u.ic.S_Status) != 0xfd) {
            if (--retries == 0) {
                printk("GDT-PCI: Initialization error (DEINIT failed)\n");
                iounmap(ha->brd);
                return 0;
            }
            gdth_delay(1);
        }
        prot_ver = (unchar)(gdth_readl(&dp6m_ptr->u.ic.S_Info[0]) >> 16);
        gdth_writeb(0, &dp6m_ptr->u.ic.S_Status);
        if (prot_ver < 0x2b)      /* FW < x.43: no 64-bit DMA support */
            ha->dma64_support = 0;
        else 
            ha->dma64_support = 1;
    }

    return 1;
}


/* controller protocol functions */

static void __init gdth_enable_int(int hanum)
{
    gdth_ha_str *ha;
    ulong flags;
    gdt2_dpram_str __iomem *dp2_ptr;
    gdt6_dpram_str __iomem *dp6_ptr;
    gdt6m_dpram_str __iomem *dp6m_ptr;

    TRACE(("gdth_enable_int() hanum %d\n",hanum));
    ha = HADATA(gdth_ctr_tab[hanum]);
    spin_lock_irqsave(&ha->smp_lock, flags);

    if (ha->type == GDT_EISA) {
        outb(0xff, ha->bmic + EDOORREG);
        outb(0xff, ha->bmic + EDENABREG);
        outb(0x01, ha->bmic + EINTENABREG);
    } else if (ha->type == GDT_ISA) {
        dp2_ptr = ha->brd;
        gdth_writeb(1, &dp2_ptr->io.irqdel);
        gdth_writeb(0, &dp2_ptr->u.ic.Cmd_Index);
        gdth_writeb(1, &dp2_ptr->io.irqen);
    } else if (ha->type == GDT_PCI) {
        dp6_ptr = ha->brd;
        gdth_writeb(1, &dp6_ptr->io.irqdel);
        gdth_writeb(0, &dp6_ptr->u.ic.Cmd_Index);
        gdth_writeb(1, &dp6_ptr->io.irqen);
    } else if (ha->type == GDT_PCINEW) {
        outb(0xff, PTR2USHORT(&ha->plx->edoor_reg));
        outb(0x03, PTR2USHORT(&ha->plx->control1));
    } else if (ha->type == GDT_PCIMPR) {
        dp6m_ptr = ha->brd;
        gdth_writeb(0xff, &dp6m_ptr->i960r.edoor_reg);
        gdth_writeb(gdth_readb(&dp6m_ptr->i960r.edoor_en_reg) & ~4,
                    &dp6m_ptr->i960r.edoor_en_reg);
    }
    spin_unlock_irqrestore(&ha->smp_lock, flags);
}


static int gdth_get_status(unchar *pIStatus,int irq)
{
    register gdth_ha_str *ha;
    int i;

    TRACE(("gdth_get_status() irq %d ctr_count %d\n",
           irq,gdth_ctr_count));
    
    *pIStatus = 0;
    for (i=0; i<gdth_ctr_count; ++i) {
        ha = HADATA(gdth_ctr_tab[i]);
        if (ha->irq != (unchar)irq)             /* check IRQ */
            continue;
        if (ha->type == GDT_EISA)
            *pIStatus = inb((ushort)ha->bmic + EDOORREG);
        else if (ha->type == GDT_ISA)
            *pIStatus =
                gdth_readb(&((gdt2_dpram_str __iomem *)ha->brd)->u.ic.Cmd_Index);
        else if (ha->type == GDT_PCI)
            *pIStatus =
                gdth_readb(&((gdt6_dpram_str __iomem *)ha->brd)->u.ic.Cmd_Index);
        else if (ha->type == GDT_PCINEW) 
            *pIStatus = inb(PTR2USHORT(&ha->plx->edoor_reg));
        else if (ha->type == GDT_PCIMPR)
            *pIStatus =
                gdth_readb(&((gdt6m_dpram_str __iomem *)ha->brd)->i960r.edoor_reg);
   
        if (*pIStatus)                                  
            return i;                           /* board found */
    }
    return -1;
}
                 
    
static int gdth_test_busy(int hanum)
{
    register gdth_ha_str *ha;
    register int gdtsema0 = 0;

    TRACE(("gdth_test_busy() hanum %d\n",hanum));
    
    ha = HADATA(gdth_ctr_tab[hanum]);
    if (ha->type == GDT_EISA)
        gdtsema0 = (int)inb(ha->bmic + SEMA0REG);
    else if (ha->type == GDT_ISA)
        gdtsema0 = (int)gdth_readb(&((gdt2_dpram_str __iomem *)ha->brd)->u.ic.Sema0);
    else if (ha->type == GDT_PCI)
        gdtsema0 = (int)gdth_readb(&((gdt6_dpram_str __iomem *)ha->brd)->u.ic.Sema0);
    else if (ha->type == GDT_PCINEW) 
        gdtsema0 = (int)inb(PTR2USHORT(&ha->plx->sema0_reg));
    else if (ha->type == GDT_PCIMPR)
        gdtsema0 = 
            (int)gdth_readb(&((gdt6m_dpram_str __iomem *)ha->brd)->i960r.sema0_reg);

    return (gdtsema0 & 1);
}


static int gdth_get_cmd_index(int hanum)
{
    register gdth_ha_str *ha;
    int i;

    TRACE(("gdth_get_cmd_index() hanum %d\n",hanum));

    ha = HADATA(gdth_ctr_tab[hanum]);
    for (i=0; i<GDTH_MAXCMDS; ++i) {
        if (ha->cmd_tab[i].cmnd == UNUSED_CMND) {
            ha->cmd_tab[i].cmnd = ha->pccb->RequestBuffer;
            ha->cmd_tab[i].service = ha->pccb->Service;
            ha->pccb->CommandIndex = (ulong32)i+2;
            return (i+2);
        }
    }
    return 0;
}


static void gdth_set_sema0(int hanum)
{
    register gdth_ha_str *ha;

    TRACE(("gdth_set_sema0() hanum %d\n",hanum));

    ha = HADATA(gdth_ctr_tab[hanum]);
    if (ha->type == GDT_EISA) {
        outb(1, ha->bmic + SEMA0REG);
    } else if (ha->type == GDT_ISA) {
        gdth_writeb(1, &((gdt2_dpram_str __iomem *)ha->brd)->u.ic.Sema0);
    } else if (ha->type == GDT_PCI) {
        gdth_writeb(1, &((gdt6_dpram_str __iomem *)ha->brd)->u.ic.Sema0);
    } else if (ha->type == GDT_PCINEW) { 
        outb(1, PTR2USHORT(&ha->plx->sema0_reg));
    } else if (ha->type == GDT_PCIMPR) {
        gdth_writeb(1, &((gdt6m_dpram_str __iomem *)ha->brd)->i960r.sema0_reg);
    }
}


static void gdth_copy_command(int hanum)
{
    register gdth_ha_str *ha;
    register gdth_cmd_str *cmd_ptr;
    register gdt6m_dpram_str __iomem *dp6m_ptr;
    register gdt6c_dpram_str __iomem *dp6c_ptr;
    gdt6_dpram_str __iomem *dp6_ptr;
    gdt2_dpram_str __iomem *dp2_ptr;
    ushort cp_count,dp_offset,cmd_no;
    
    TRACE(("gdth_copy_command() hanum %d\n",hanum));

    ha = HADATA(gdth_ctr_tab[hanum]);
    cp_count = ha->cmd_len;
    dp_offset= ha->cmd_offs_dpmem;
    cmd_no   = ha->cmd_cnt;
    cmd_ptr  = ha->pccb;

    ++ha->cmd_cnt;                                                      
    if (ha->type == GDT_EISA)
        return;                                 /* no DPMEM, no copy */

    /* set cpcount dword aligned */
    if (cp_count & 3)
        cp_count += (4 - (cp_count & 3));

    ha->cmd_offs_dpmem += cp_count;
    
    /* set offset and service, copy command to DPMEM */
    if (ha->type == GDT_ISA) {
        dp2_ptr = ha->brd;
        gdth_writew(dp_offset + DPMEM_COMMAND_OFFSET, 
                    &dp2_ptr->u.ic.comm_queue[cmd_no].offset);
        gdth_writew((ushort)cmd_ptr->Service, 
                    &dp2_ptr->u.ic.comm_queue[cmd_no].serv_id);
        memcpy_toio(&dp2_ptr->u.ic.gdt_dpr_cmd[dp_offset],cmd_ptr,cp_count);
    } else if (ha->type == GDT_PCI) {
        dp6_ptr = ha->brd;
        gdth_writew(dp_offset + DPMEM_COMMAND_OFFSET, 
                    &dp6_ptr->u.ic.comm_queue[cmd_no].offset);
        gdth_writew((ushort)cmd_ptr->Service, 
                    &dp6_ptr->u.ic.comm_queue[cmd_no].serv_id);
        memcpy_toio(&dp6_ptr->u.ic.gdt_dpr_cmd[dp_offset],cmd_ptr,cp_count);
    } else if (ha->type == GDT_PCINEW) {
        dp6c_ptr = ha->brd;
        gdth_writew(dp_offset + DPMEM_COMMAND_OFFSET, 
                    &dp6c_ptr->u.ic.comm_queue[cmd_no].offset);
        gdth_writew((ushort)cmd_ptr->Service, 
                    &dp6c_ptr->u.ic.comm_queue[cmd_no].serv_id);
        memcpy_toio(&dp6c_ptr->u.ic.gdt_dpr_cmd[dp_offset],cmd_ptr,cp_count);
    } else if (ha->type == GDT_PCIMPR) {
        dp6m_ptr = ha->brd;
        gdth_writew(dp_offset + DPMEM_COMMAND_OFFSET, 
                    &dp6m_ptr->u.ic.comm_queue[cmd_no].offset);
        gdth_writew((ushort)cmd_ptr->Service, 
                    &dp6m_ptr->u.ic.comm_queue[cmd_no].serv_id);
        memcpy_toio(&dp6m_ptr->u.ic.gdt_dpr_cmd[dp_offset],cmd_ptr,cp_count);
    }
}


static void gdth_release_event(int hanum)
{
    register gdth_ha_str *ha;

    TRACE(("gdth_release_event() hanum %d\n",hanum));
    ha = HADATA(gdth_ctr_tab[hanum]);

#ifdef GDTH_STATISTICS
    {
        ulong32 i,j;
        for (i=0,j=0; j<GDTH_MAXCMDS; ++j) {
            if (ha->cmd_tab[j].cmnd != UNUSED_CMND)
                ++i;
        }
        if (max_index < i) {
            max_index = i;
            TRACE3(("GDT: max_index = %d\n",(ushort)i));
        }
    }
#endif

    if (ha->pccb->OpCode == GDT_INIT)
        ha->pccb->Service |= 0x80;

    if (ha->type == GDT_EISA) {
        if (ha->pccb->OpCode == GDT_INIT)               /* store DMA buffer */
            outl(ha->ccb_phys, ha->bmic + MAILBOXREG);
        outb(ha->pccb->Service, ha->bmic + LDOORREG);
    } else if (ha->type == GDT_ISA) {
        gdth_writeb(0, &((gdt2_dpram_str __iomem *)ha->brd)->io.event);
    } else if (ha->type == GDT_PCI) {
        gdth_writeb(0, &((gdt6_dpram_str __iomem *)ha->brd)->io.event);
    } else if (ha->type == GDT_PCINEW) { 
        outb(1, PTR2USHORT(&ha->plx->ldoor_reg));
    } else if (ha->type == GDT_PCIMPR) {
        gdth_writeb(1, &((gdt6m_dpram_str __iomem *)ha->brd)->i960r.ldoor_reg);
    }
}

    
static int gdth_wait(int hanum,int index,ulong32 time)
{
    gdth_ha_str *ha;
    int answer_found = FALSE;

    TRACE(("gdth_wait() hanum %d index %d time %d\n",hanum,index,time));

    ha = HADATA(gdth_ctr_tab[hanum]);
    if (index == 0)
        return 1;                               /* no wait required */

    gdth_from_wait = TRUE;
    do {
        gdth_interrupt((int)ha->irq,ha,NULL);
        if (wait_hanum==hanum && wait_index==index) {
            answer_found = TRUE;
            break;
        }
        gdth_delay(1);
    } while (--time);
    gdth_from_wait = FALSE;
    
    while (gdth_test_busy(hanum))
        gdth_delay(0);

    return (answer_found);
}


static int gdth_internal_cmd(int hanum,unchar service,ushort opcode,ulong32 p1,
                             ulong64 p2,ulong64 p3)
{
    register gdth_ha_str *ha;
    register gdth_cmd_str *cmd_ptr;
    int retries,index;

    TRACE2(("gdth_internal_cmd() service %d opcode %d\n",service,opcode));

    ha = HADATA(gdth_ctr_tab[hanum]);
    cmd_ptr = ha->pccb;
    memset((char*)cmd_ptr,0,sizeof(gdth_cmd_str));

    /* make command  */
    for (retries = INIT_RETRIES;;) {
        cmd_ptr->Service          = service;
        cmd_ptr->RequestBuffer    = INTERNAL_CMND;
        if (!(index=gdth_get_cmd_index(hanum))) {
            TRACE(("GDT: No free command index found\n"));
            return 0;
        }
        gdth_set_sema0(hanum);
        cmd_ptr->OpCode           = opcode;
        cmd_ptr->BoardNode        = LOCALBOARD;
        if (service == CACHESERVICE) {
            if (opcode == GDT_IOCTL) {
                cmd_ptr->u.ioctl.subfunc = p1;
                cmd_ptr->u.ioctl.channel = (ulong32)p2;
                cmd_ptr->u.ioctl.param_size = (ushort)p3;
                cmd_ptr->u.ioctl.p_param = ha->scratch_phys;
            } else {
                if (ha->cache_feat & GDT_64BIT) {
                    cmd_ptr->u.cache64.DeviceNo = (ushort)p1;
                    cmd_ptr->u.cache64.BlockNo  = p2;
                } else {
                    cmd_ptr->u.cache.DeviceNo = (ushort)p1;
                    cmd_ptr->u.cache.BlockNo  = (ulong32)p2;
                }
            }
        } else if (service == SCSIRAWSERVICE) {
            if (ha->raw_feat & GDT_64BIT) {
                cmd_ptr->u.raw64.direction  = p1;
                cmd_ptr->u.raw64.bus        = (unchar)p2;
                cmd_ptr->u.raw64.target     = (unchar)p3;
                cmd_ptr->u.raw64.lun        = (unchar)(p3 >> 8);
            } else {
                cmd_ptr->u.raw.direction  = p1;
                cmd_ptr->u.raw.bus        = (unchar)p2;
                cmd_ptr->u.raw.target     = (unchar)p3;
                cmd_ptr->u.raw.lun        = (unchar)(p3 >> 8);
            }
        } else if (service == SCREENSERVICE) {
            if (opcode == GDT_REALTIME) {
                *(ulong32 *)&cmd_ptr->u.screen.su.data[0] = p1;
                *(ulong32 *)&cmd_ptr->u.screen.su.data[4] = (ulong32)p2;
                *(ulong32 *)&cmd_ptr->u.screen.su.data[8] = (ulong32)p3;
            }
        }
        ha->cmd_len          = sizeof(gdth_cmd_str);
        ha->cmd_offs_dpmem   = 0;
        ha->cmd_cnt          = 0;
        gdth_copy_command(hanum);
        gdth_release_event(hanum);
        gdth_delay(20);
        if (!gdth_wait(hanum,index,INIT_TIMEOUT)) {
            printk("GDT: Initialization error (timeout service %d)\n",service);
            return 0;
        }
        if (ha->status != S_BSY || --retries == 0)
            break;
        gdth_delay(1);   
    }   
    
    return (ha->status != S_OK ? 0:1);
}
    

/* search for devices */

static int __init gdth_search_drives(int hanum)
{
    register gdth_ha_str *ha;
    ushort cdev_cnt, i;
    int ok;
    ulong32 bus_no, drv_cnt, drv_no, j;
    gdth_getch_str *chn;
    gdth_drlist_str *drl;
    gdth_iochan_str *ioc;
    gdth_raw_iochan_str *iocr;
    gdth_arcdl_str *alst;
    gdth_alist_str *alst2;
    gdth_oem_str_ioctl *oemstr;
#ifdef INT_COAL
    gdth_perf_modes *pmod;
#endif

#ifdef GDTH_RTC
    unchar rtc[12];
    ulong flags;
#endif     
   
    TRACE(("gdth_search_drives() hanum %d\n",hanum));
    ha = HADATA(gdth_ctr_tab[hanum]);
    ok = 0;

    /* initialize controller services, at first: screen service */
    ha->screen_feat = 0;
    if (!force_dma32) {
        ok = gdth_internal_cmd(hanum,SCREENSERVICE,GDT_X_INIT_SCR,0,0,0);
        if (ok)
            ha->screen_feat = GDT_64BIT;
    }
    if (force_dma32 || (!ok && ha->status == (ushort)S_NOFUNC))
        ok = gdth_internal_cmd(hanum,SCREENSERVICE,GDT_INIT,0,0,0);
    if (!ok) {
        printk("GDT-HA %d: Initialization error screen service (code %d)\n",
               hanum, ha->status);
        return 0;
    }
    TRACE2(("gdth_search_drives(): SCREENSERVICE initialized\n"));

#ifdef GDTH_RTC
    /* read realtime clock info, send to controller */
    /* 1. wait for the falling edge of update flag */
    spin_lock_irqsave(&rtc_lock, flags);
    for (j = 0; j < 1000000; ++j)
        if (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP)
            break;
    for (j = 0; j < 1000000; ++j)
        if (!(CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP))
            break;
    /* 2. read info */
    do {
        for (j = 0; j < 12; ++j) 
            rtc[j] = CMOS_READ(j);
    } while (rtc[0] != CMOS_READ(0));
    spin_lock_irqrestore(&rtc_lock, flags);
    TRACE2(("gdth_search_drives(): RTC: %x/%x/%x\n",*(ulong32 *)&rtc[0],
            *(ulong32 *)&rtc[4], *(ulong32 *)&rtc[8]));
    /* 3. send to controller firmware */
    gdth_internal_cmd(hanum,SCREENSERVICE,GDT_REALTIME, *(ulong32 *)&rtc[0],
                      *(ulong32 *)&rtc[4], *(ulong32 *)&rtc[8]);
#endif  
 
    /* unfreeze all IOs */
    gdth_internal_cmd(hanum,CACHESERVICE,GDT_UNFREEZE_IO,0,0,0);
 
    /* initialize cache service */
    ha->cache_feat = 0;
    if (!force_dma32) {
        ok = gdth_internal_cmd(hanum,CACHESERVICE,GDT_X_INIT_HOST,LINUX_OS,0,0);
        if (ok)
            ha->cache_feat = GDT_64BIT;
    }
    if (force_dma32 || (!ok && ha->status == (ushort)S_NOFUNC))
        ok = gdth_internal_cmd(hanum,CACHESERVICE,GDT_INIT,LINUX_OS,0,0);
    if (!ok) {
        printk("GDT-HA %d: Initialization error cache service (code %d)\n",
               hanum, ha->status);
        return 0;
    }
    TRACE2(("gdth_search_drives(): CACHESERVICE initialized\n"));
    cdev_cnt = (ushort)ha->info;
    ha->fw_vers = ha->service;

#ifdef INT_COAL
    if (ha->type == GDT_PCIMPR) {
        /* set perf. modes */
        pmod = (gdth_perf_modes *)ha->pscratch;
        pmod->version          = 1;
        pmod->st_mode          = 1;    /* enable one status buffer */
        *((ulong64 *)&pmod->st_buff_addr1) = ha->coal_stat_phys;
        pmod->st_buff_indx1    = COALINDEX;
        pmod->st_buff_addr2    = 0;
        pmod->st_buff_u_addr2  = 0;
        pmod->st_buff_indx2    = 0;
        pmod->st_buff_size     = sizeof(gdth_coal_status) * MAXOFFSETS;
        pmod->cmd_mode         = 0;    // disable all cmd buffers
        pmod->cmd_buff_addr1   = 0;
        pmod->cmd_buff_u_addr1 = 0;
        pmod->cmd_buff_indx1   = 0;
        pmod->cmd_buff_addr2   = 0;
        pmod->cmd_buff_u_addr2 = 0;
        pmod->cmd_buff_indx2   = 0;
        pmod->cmd_buff_size    = 0;
        pmod->reserved1        = 0;            
        pmod->reserved2        = 0;            
        if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,SET_PERF_MODES,
                              INVALID_CHANNEL,sizeof(gdth_perf_modes))) {
            printk("GDT-HA %d: Interrupt coalescing activated\n", hanum);
        }
    }
#endif

    /* detect number of buses - try new IOCTL */
    iocr = (gdth_raw_iochan_str *)ha->pscratch;
    iocr->hdr.version        = 0xffffffff;
    iocr->hdr.list_entries   = MAXBUS;
    iocr->hdr.first_chan     = 0;
    iocr->hdr.last_chan      = MAXBUS-1;
    iocr->hdr.list_offset    = GDTOFFSOF(gdth_raw_iochan_str, list[0]);
    if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,IOCHAN_RAW_DESC,
                          INVALID_CHANNEL,sizeof(gdth_raw_iochan_str))) {
        TRACE2(("IOCHAN_RAW_DESC supported!\n"));
        ha->bus_cnt = iocr->hdr.chan_count;
        for (bus_no = 0; bus_no < ha->bus_cnt; ++bus_no) {
            if (iocr->list[bus_no].proc_id < MAXID)
                ha->bus_id[bus_no] = iocr->list[bus_no].proc_id;
            else
                ha->bus_id[bus_no] = 0xff;
        }
    } else {
        /* old method */
        chn = (gdth_getch_str *)ha->pscratch;
        for (bus_no = 0; bus_no < MAXBUS; ++bus_no) {
            chn->channel_no = bus_no;
            if (!gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,
                                   SCSI_CHAN_CNT | L_CTRL_PATTERN,
                                   IO_CHANNEL | INVALID_CHANNEL,
                                   sizeof(gdth_getch_str))) {
                if (bus_no == 0) {
                    printk("GDT-HA %d: Error detecting channel count (0x%x)\n",
                           hanum, ha->status);
                    return 0;
                }
                break;
            }
            if (chn->siop_id < MAXID)
                ha->bus_id[bus_no] = chn->siop_id;
            else
                ha->bus_id[bus_no] = 0xff;
        }       
        ha->bus_cnt = (unchar)bus_no;
    }
    TRACE2(("gdth_search_drives() %d channels\n",ha->bus_cnt));

    /* read cache configuration */
    if (!gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,CACHE_INFO,
                           INVALID_CHANNEL,sizeof(gdth_cinfo_str))) {
        printk("GDT-HA %d: Initialization error cache service (code %d)\n",
               hanum, ha->status);
        return 0;
    }
    ha->cpar = ((gdth_cinfo_str *)ha->pscratch)->cpar;
    TRACE2(("gdth_search_drives() cinfo: vs %x sta %d str %d dw %d b %d\n",
            ha->cpar.version,ha->cpar.state,ha->cpar.strategy,
            ha->cpar.write_back,ha->cpar.block_size));

    /* read board info and features */
    ha->more_proc = FALSE;
    if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,BOARD_INFO,
                          INVALID_CHANNEL,sizeof(gdth_binfo_str))) {
        memcpy(&ha->binfo, (gdth_binfo_str *)ha->pscratch,
               sizeof(gdth_binfo_str));
        if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,BOARD_FEATURES,
                              INVALID_CHANNEL,sizeof(gdth_bfeat_str))) {
            TRACE2(("BOARD_INFO/BOARD_FEATURES supported\n"));
            ha->bfeat = *(gdth_bfeat_str *)ha->pscratch;
            ha->more_proc = TRUE;
        }
    } else {
        TRACE2(("BOARD_INFO requires firmware >= 1.10/2.08\n"));
        strcpy(ha->binfo.type_string, gdth_ctr_name(hanum));
    }
    TRACE2(("Controller name: %s\n",ha->binfo.type_string));

    /* read more informations */
    if (ha->more_proc) {
        /* physical drives, channel addresses */
        ioc = (gdth_iochan_str *)ha->pscratch;
        ioc->hdr.version        = 0xffffffff;
        ioc->hdr.list_entries   = MAXBUS;
        ioc->hdr.first_chan     = 0;
        ioc->hdr.last_chan      = MAXBUS-1;
        ioc->hdr.list_offset    = GDTOFFSOF(gdth_iochan_str, list[0]);
        if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,IOCHAN_DESC,
                              INVALID_CHANNEL,sizeof(gdth_iochan_str))) {
            for (bus_no = 0; bus_no < ha->bus_cnt; ++bus_no) {
                ha->raw[bus_no].address = ioc->list[bus_no].address;
                ha->raw[bus_no].local_no = ioc->list[bus_no].local_no;
            }
        } else {
            for (bus_no = 0; bus_no < ha->bus_cnt; ++bus_no) {
                ha->raw[bus_no].address = IO_CHANNEL;
                ha->raw[bus_no].local_no = bus_no;
            }
        }
        for (bus_no = 0; bus_no < ha->bus_cnt; ++bus_no) {
            chn = (gdth_getch_str *)ha->pscratch;
            chn->channel_no = ha->raw[bus_no].local_no;
            if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,
                                  SCSI_CHAN_CNT | L_CTRL_PATTERN,
                                  ha->raw[bus_no].address | INVALID_CHANNEL,
                                  sizeof(gdth_getch_str))) {
                ha->raw[bus_no].pdev_cnt = chn->drive_cnt;
                TRACE2(("Channel %d: %d phys. drives\n",
                        bus_no,chn->drive_cnt));
            }
            if (ha->raw[bus_no].pdev_cnt > 0) {
                drl = (gdth_drlist_str *)ha->pscratch;
                drl->sc_no = ha->raw[bus_no].local_no;
                drl->sc_cnt = ha->raw[bus_no].pdev_cnt;
                if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,
                                      SCSI_DR_LIST | L_CTRL_PATTERN,
                                      ha->raw[bus_no].address | INVALID_CHANNEL,
                                      sizeof(gdth_drlist_str))) {
                    for (j = 0; j < ha->raw[bus_no].pdev_cnt; ++j) 
                        ha->raw[bus_no].id_list[j] = drl->sc_list[j];
                } else {
                    ha->raw[bus_no].pdev_cnt = 0;
                }
            }
        }

        /* logical drives */
        if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,CACHE_DRV_CNT,
                              INVALID_CHANNEL,sizeof(ulong32))) {
            drv_cnt = *(ulong32 *)ha->pscratch;
            if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,CACHE_DRV_LIST,
                                  INVALID_CHANNEL,drv_cnt * sizeof(ulong32))) {
                for (j = 0; j < drv_cnt; ++j) {
                    drv_no = ((ulong32 *)ha->pscratch)[j];
                    if (drv_no < MAX_LDRIVES) {
                        ha->hdr[drv_no].is_logdrv = TRUE;
                        TRACE2(("Drive %d is log. drive\n",drv_no));
                    }
                }
            }
            alst = (gdth_arcdl_str *)ha->pscratch;
            alst->entries_avail = MAX_LDRIVES;
            alst->first_entry = 0;
            alst->list_offset = GDTOFFSOF(gdth_arcdl_str, list[0]);
            if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,
                                  ARRAY_DRV_LIST2 | LA_CTRL_PATTERN, 
                                  INVALID_CHANNEL, sizeof(gdth_arcdl_str) +
                                  (alst->entries_avail-1) * sizeof(gdth_alist_str))) { 
                for (j = 0; j < alst->entries_init; ++j) {
                    ha->hdr[j].is_arraydrv = alst->list[j].is_arrayd;
                    ha->hdr[j].is_master = alst->list[j].is_master;
                    ha->hdr[j].is_parity = alst->list[j].is_parity;
                    ha->hdr[j].is_hotfix = alst->list[j].is_hotfix;
                    ha->hdr[j].master_no = alst->list[j].cd_handle;
                }
            } else if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,
                                         ARRAY_DRV_LIST | LA_CTRL_PATTERN,
                                         0, 35 * sizeof(gdth_alist_str))) {
                for (j = 0; j < 35; ++j) {
                    alst2 = &((gdth_alist_str *)ha->pscratch)[j];
                    ha->hdr[j].is_arraydrv = alst2->is_arrayd;
                    ha->hdr[j].is_master = alst2->is_master;
                    ha->hdr[j].is_parity = alst2->is_parity;
                    ha->hdr[j].is_hotfix = alst2->is_hotfix;
                    ha->hdr[j].master_no = alst2->cd_handle;
                }
            }
        }
    }       
                                  
    /* initialize raw service */
    ha->raw_feat = 0;
    if (!force_dma32) {
        ok = gdth_internal_cmd(hanum,SCSIRAWSERVICE,GDT_X_INIT_RAW,0,0,0);
        if (ok)
            ha->raw_feat = GDT_64BIT;
    }
    if (force_dma32 || (!ok && ha->status == (ushort)S_NOFUNC))
        ok = gdth_internal_cmd(hanum,SCSIRAWSERVICE,GDT_INIT,0,0,0);
    if (!ok) {
        printk("GDT-HA %d: Initialization error raw service (code %d)\n",
               hanum, ha->status);
        return 0;
    }
    TRACE2(("gdth_search_drives(): RAWSERVICE initialized\n"));

    /* set/get features raw service (scatter/gather) */
    if (gdth_internal_cmd(hanum,SCSIRAWSERVICE,GDT_SET_FEAT,SCATTER_GATHER,
                          0,0)) {
        TRACE2(("gdth_search_drives(): set features RAWSERVICE OK\n"));
        if (gdth_internal_cmd(hanum,SCSIRAWSERVICE,GDT_GET_FEAT,0,0,0)) {
            TRACE2(("gdth_search_dr(): get feat RAWSERVICE %d\n",
                    ha->info));
            ha->raw_feat |= (ushort)ha->info;
        }
    } 

    /* set/get features cache service (equal to raw service) */
    if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_SET_FEAT,0,
                          SCATTER_GATHER,0)) {
        TRACE2(("gdth_search_drives(): set features CACHESERVICE OK\n"));
        if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_GET_FEAT,0,0,0)) {
            TRACE2(("gdth_search_dr(): get feat CACHESERV. %d\n",
                    ha->info));
            ha->cache_feat |= (ushort)ha->info;
        }
    }

    /* reserve drives for raw service */
    if (reserve_mode != 0) {
        gdth_internal_cmd(hanum,SCSIRAWSERVICE,GDT_RESERVE_ALL,
                          reserve_mode == 1 ? 1 : 3, 0, 0);
        TRACE2(("gdth_search_drives(): RESERVE_ALL code %d\n", 
                ha->status));
    }
    for (i = 0; i < MAX_RES_ARGS; i += 4) {
        if (reserve_list[i] == hanum && reserve_list[i+1] < ha->bus_cnt && 
            reserve_list[i+2] < ha->tid_cnt && reserve_list[i+3] < MAXLUN) {
            TRACE2(("gdth_search_drives(): reserve ha %d bus %d id %d lun %d\n",
                    reserve_list[i], reserve_list[i+1],
                    reserve_list[i+2], reserve_list[i+3]));
            if (!gdth_internal_cmd(hanum,SCSIRAWSERVICE,GDT_RESERVE,0,
                                   reserve_list[i+1], reserve_list[i+2] | 
                                   (reserve_list[i+3] << 8))) {
                printk("GDT-HA %d: Error raw service (RESERVE, code %d)\n",
                       hanum, ha->status);
             }
        }
    }

    /* Determine OEM string using IOCTL */
    oemstr = (gdth_oem_str_ioctl *)ha->pscratch;
    oemstr->params.ctl_version = 0x01;
    oemstr->params.buffer_size = sizeof(oemstr->text);
    if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_IOCTL,
                          CACHE_READ_OEM_STRING_RECORD,INVALID_CHANNEL,
                          sizeof(gdth_oem_str_ioctl))) {
        TRACE2(("gdth_search_drives(): CACHE_READ_OEM_STRING_RECORD OK\n"));
        printk("GDT-HA %d: Vendor: %s Name: %s\n",
               hanum,oemstr->text.oem_company_name,ha->binfo.type_string);
        /* Save the Host Drive inquiry data */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        strlcpy(ha->oem_name,oemstr->text.scsi_host_drive_inquiry_vendor_id,
                sizeof(ha->oem_name));
#else
        strncpy(ha->oem_name,oemstr->text.scsi_host_drive_inquiry_vendor_id,7);
        ha->oem_name[7] = '\0';
#endif
    } else {
        /* Old method, based on PCI ID */
        TRACE2(("gdth_search_drives(): CACHE_READ_OEM_STRING_RECORD failed\n"));
        printk("GDT-HA %d: Name: %s\n",
               hanum,ha->binfo.type_string);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        if (ha->oem_id == OEM_ID_INTEL)
            strlcpy(ha->oem_name,"Intel  ", sizeof(ha->oem_name));
        else
            strlcpy(ha->oem_name,"ICP    ", sizeof(ha->oem_name));
#else 
        if (ha->oem_id == OEM_ID_INTEL)
            strcpy(ha->oem_name,"Intel  ");
        else
            strcpy(ha->oem_name,"ICP    ");
#endif
    }

    /* scanning for host drives */
    for (i = 0; i < cdev_cnt; ++i) 
        gdth_analyse_hdrive(hanum,i);
    
    TRACE(("gdth_search_drives() OK\n"));
    return 1;
}

static int gdth_analyse_hdrive(int hanum,ushort hdrive)
{
    register gdth_ha_str *ha;
    ulong32 drv_cyls;
    int drv_hds, drv_secs;

    TRACE(("gdth_analyse_hdrive() hanum %d drive %d\n",hanum,hdrive));
    if (hdrive >= MAX_HDRIVES)
        return 0;
    ha = HADATA(gdth_ctr_tab[hanum]);

    if (!gdth_internal_cmd(hanum,CACHESERVICE,GDT_INFO,hdrive,0,0)) 
        return 0;
    ha->hdr[hdrive].present = TRUE;
    ha->hdr[hdrive].size = ha->info;
   
    /* evaluate mapping (sectors per head, heads per cylinder) */
    ha->hdr[hdrive].size &= ~SECS32;
    if (ha->info2 == 0) {
        gdth_eval_mapping(ha->hdr[hdrive].size,&drv_cyls,&drv_hds,&drv_secs);
    } else {
        drv_hds = ha->info2 & 0xff;
        drv_secs = (ha->info2 >> 8) & 0xff;
        drv_cyls = (ulong32)ha->hdr[hdrive].size / drv_hds / drv_secs;
    }
    ha->hdr[hdrive].heads = (unchar)drv_hds;
    ha->hdr[hdrive].secs  = (unchar)drv_secs;
    /* round size */
    ha->hdr[hdrive].size  = drv_cyls * drv_hds * drv_secs;
    
    if (ha->cache_feat & GDT_64BIT) {
        if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_X_INFO,hdrive,0,0)
            && ha->info2 != 0) {
            ha->hdr[hdrive].size = ((ulong64)ha->info2 << 32) | ha->info;
        }
    }
    TRACE2(("gdth_search_dr() cdr. %d size %d hds %d scs %d\n",
            hdrive,ha->hdr[hdrive].size,drv_hds,drv_secs));

    /* get informations about device */
    if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_DEVTYPE,hdrive,0,0)) {
        TRACE2(("gdth_search_dr() cache drive %d devtype %d\n",
                hdrive,ha->info));
        ha->hdr[hdrive].devtype = (ushort)ha->info;
    }

    /* cluster info */
    if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_CLUST_INFO,hdrive,0,0)) {
        TRACE2(("gdth_search_dr() cache drive %d cluster info %d\n",
                hdrive,ha->info));
        if (!shared_access)
            ha->hdr[hdrive].cluster_type = (unchar)ha->info;
    }

    /* R/W attributes */
    if (gdth_internal_cmd(hanum,CACHESERVICE,GDT_RW_ATTRIBS,hdrive,0,0)) {
        TRACE2(("gdth_search_dr() cache drive %d r/w attrib. %d\n",
                hdrive,ha->info));
        ha->hdr[hdrive].rw_attribs = (unchar)ha->info;
    }

    return 1;
}


/* command queueing/sending functions */

static void gdth_putq(int hanum,Scsi_Cmnd *scp,unchar priority)
{
    register gdth_ha_str *ha;
    register Scsi_Cmnd *pscp;
    register Scsi_Cmnd *nscp;
    ulong flags;
    unchar b, t;

    TRACE(("gdth_putq() priority %d\n",priority));
    ha = HADATA(gdth_ctr_tab[hanum]);
    spin_lock_irqsave(&ha->smp_lock, flags);

    if (scp->done != gdth_scsi_done) {
        scp->SCp.this_residual = (int)priority;
        b = virt_ctr ? NUMDATA(scp->device->host)->busnum:scp->device->channel;
        t = scp->device->id;
        if (priority >= DEFAULT_PRI) {
            if ((b != ha->virt_bus && ha->raw[BUS_L2P(ha,b)].lock) ||
                (b==ha->virt_bus && t<MAX_HDRIVES && ha->hdr[t].lock)) {
                TRACE2(("gdth_putq(): locked IO ->update_timeout()\n"));
                scp->SCp.buffers_residual = gdth_update_timeout(hanum, scp, 0);
            }
        }
    }

    if (ha->req_first==NULL) {
        ha->req_first = scp;                    /* queue was empty */
        scp->SCp.ptr = NULL;
    } else {                                    /* queue not empty */
        pscp = ha->req_first;
        nscp = (Scsi_Cmnd *)pscp->SCp.ptr;
        /* priority: 0-highest,..,0xff-lowest */
        while (nscp && (unchar)nscp->SCp.this_residual <= priority) {
            pscp = nscp;
            nscp = (Scsi_Cmnd *)pscp->SCp.ptr;
        }
        pscp->SCp.ptr = (char *)scp;
        scp->SCp.ptr  = (char *)nscp;
    }
    spin_unlock_irqrestore(&ha->smp_lock, flags);

#ifdef GDTH_STATISTICS
    flags = 0;
    for (nscp=ha->req_first; nscp; nscp=(Scsi_Cmnd*)nscp->SCp.ptr)
        ++flags;
    if (max_rq < flags) {
        max_rq = flags;
        TRACE3(("GDT: max_rq = %d\n",(ushort)max_rq));
    }
#endif
}

static void gdth_next(int hanum)
{
    register gdth_ha_str *ha;
    register Scsi_Cmnd *pscp;
    register Scsi_Cmnd *nscp;
    unchar b, t, l, firsttime;
    unchar this_cmd, next_cmd;
    ulong flags = 0;
    int cmd_index;

    TRACE(("gdth_next() hanum %d\n",hanum));
    ha = HADATA(gdth_ctr_tab[hanum]);
    if (!gdth_polling) 
        spin_lock_irqsave(&ha->smp_lock, flags);

    ha->cmd_cnt = ha->cmd_offs_dpmem = 0;
    this_cmd = firsttime = TRUE;
    next_cmd = gdth_polling ? FALSE:TRUE;
    cmd_index = 0;

    for (nscp = pscp = ha->req_first; nscp; nscp = (Scsi_Cmnd *)nscp->SCp.ptr) {
        if (nscp != pscp && nscp != (Scsi_Cmnd *)pscp->SCp.ptr)
            pscp = (Scsi_Cmnd *)pscp->SCp.ptr;
        if (nscp->done != gdth_scsi_done) {
            b = virt_ctr ?
                NUMDATA(nscp->device->host)->busnum : nscp->device->channel;
            t = nscp->device->id;
            l = nscp->device->lun;
            if (nscp->SCp.this_residual >= DEFAULT_PRI) {
                if ((b != ha->virt_bus && ha->raw[BUS_L2P(ha,b)].lock) ||
                    (b == ha->virt_bus && t < MAX_HDRIVES && ha->hdr[t].lock))
                    continue;
            }
        } else
            b = t = l = 0;

        if (firsttime) {
            if (gdth_test_busy(hanum)) {        /* controller busy ? */
                TRACE(("gdth_next() controller %d busy !\n",hanum));
                if (!gdth_polling) {
                    spin_unlock_irqrestore(&ha->smp_lock, flags);
                    return;
                }
                while (gdth_test_busy(hanum))
                    gdth_delay(1);
            }   
            firsttime = FALSE;
        }

        if (nscp->done != gdth_scsi_done) {
        if (nscp->SCp.phase == -1) {
            nscp->SCp.phase = CACHESERVICE;           /* default: cache svc. */ 
            if (nscp->cmnd[0] == TEST_UNIT_READY) {
                TRACE2(("TEST_UNIT_READY Bus %d Id %d LUN %d\n", 
                        b, t, l));
                /* TEST_UNIT_READY -> set scan mode */
                if ((ha->scan_mode & 0x0f) == 0) {
                    if (b == 0 && t == 0 && l == 0) {
                        ha->scan_mode |= 1;
                        TRACE2(("Scan mode: 0x%x\n", ha->scan_mode));
                    }
                } else if ((ha->scan_mode & 0x0f) == 1) {
                    if (b == 0 && ((t == 0 && l == 1) ||
                         (t == 1 && l == 0))) {
                        nscp->SCp.sent_command = GDT_SCAN_START;
                        nscp->SCp.phase = ((ha->scan_mode & 0x10 ? 1:0) << 8) 
                            | SCSIRAWSERVICE;
                        ha->scan_mode = 0x12;
                        TRACE2(("Scan mode: 0x%x (SCAN_START)\n", 
                                ha->scan_mode));
                    } else {
                        ha->scan_mode &= 0x10;
                        TRACE2(("Scan mode: 0x%x\n", ha->scan_mode));
                    }                   
                } else if (ha->scan_mode == 0x12) {
                    if (b == ha->bus_cnt && t == ha->tid_cnt-1) {
                        nscp->SCp.phase = SCSIRAWSERVICE;
                        nscp->SCp.sent_command = GDT_SCAN_END;
                        ha->scan_mode &= 0x10;
                        TRACE2(("Scan mode: 0x%x (SCAN_END)\n", 
                                ha->scan_mode));
                    }
                }
            }
            if (b == ha->virt_bus && nscp->cmnd[0] != INQUIRY &&
                nscp->cmnd[0] != READ_CAPACITY && nscp->cmnd[0] != MODE_SENSE &&
                (ha->hdr[t].cluster_type & CLUSTER_DRIVE)) {
                /* always GDT_CLUST_INFO! */
                nscp->SCp.sent_command = GDT_CLUST_INFO;
            }
        }
        }

        if (nscp->SCp.sent_command != -1) {
            if ((nscp->SCp.phase & 0xff) == CACHESERVICE) {
                if (!(cmd_index=gdth_fill_cache_cmd(hanum,nscp,t)))
                    this_cmd = FALSE;
                next_cmd = FALSE;
            } else if ((nscp->SCp.phase & 0xff) == SCSIRAWSERVICE) {
                if (!(cmd_index=gdth_fill_raw_cmd(hanum,nscp,BUS_L2P(ha,b))))
                    this_cmd = FALSE;
                next_cmd = FALSE;
            } else {
                memset((char*)nscp->sense_buffer,0,16);
                nscp->sense_buffer[0] = 0x70;
                nscp->sense_buffer[2] = NOT_READY;
                nscp->result = (DID_OK << 16) | (CHECK_CONDITION << 1);
                if (!nscp->SCp.have_data_in)
                    nscp->SCp.have_data_in++;
                else
                    nscp->scsi_done(nscp);
            }
        } else if (nscp->done == gdth_scsi_done) {
            if (!(cmd_index=gdth_special_cmd(hanum,nscp)))
                this_cmd = FALSE;
            next_cmd = FALSE;
        } else if (b != ha->virt_bus) {
            if (ha->raw[BUS_L2P(ha,b)].io_cnt[t] >= GDTH_MAX_RAW ||
                !(cmd_index=gdth_fill_raw_cmd(hanum,nscp,BUS_L2P(ha,b)))) 
                this_cmd = FALSE;
            else 
                ha->raw[BUS_L2P(ha,b)].io_cnt[t]++;
        } else if (t >= MAX_HDRIVES || !ha->hdr[t].present || l != 0) {
            TRACE2(("Command 0x%x to bus %d id %d lun %d -> IGNORE\n",
                    nscp->cmnd[0], b, t, l));
            nscp->result = DID_BAD_TARGET << 16;
            if (!nscp->SCp.have_data_in)
                nscp->SCp.have_data_in++;
            else
                nscp->scsi_done(nscp);
        } else {
            switch (nscp->cmnd[0]) {
              case TEST_UNIT_READY:
              case INQUIRY:
              case REQUEST_SENSE:
              case READ_CAPACITY:
              case VERIFY:
              case START_STOP:
              case MODE_SENSE:
              case SERVICE_ACTION_IN:
                TRACE(("cache cmd %x/%x/%x/%x/%x/%x\n",nscp->cmnd[0],
                       nscp->cmnd[1],nscp->cmnd[2],nscp->cmnd[3],
                       nscp->cmnd[4],nscp->cmnd[5]));
                if (ha->hdr[t].media_changed && nscp->cmnd[0] != INQUIRY) {
                    /* return UNIT_ATTENTION */
                    TRACE2(("cmd 0x%x target %d: UNIT_ATTENTION\n",
                             nscp->cmnd[0], t));
                    ha->hdr[t].media_changed = FALSE;
                    memset((char*)nscp->sense_buffer,0,16);
                    nscp->sense_buffer[0] = 0x70;
                    nscp->sense_buffer[2] = UNIT_ATTENTION;
                    nscp->result = (DID_OK << 16) | (CHECK_CONDITION << 1);
                    if (!nscp->SCp.have_data_in)
                        nscp->SCp.have_data_in++;
                    else
                        nscp->scsi_done(nscp);
                } else if (gdth_internal_cache_cmd(hanum,nscp))
                    nscp->scsi_done(nscp);
                break;

              case ALLOW_MEDIUM_REMOVAL:
                TRACE(("cache cmd %x/%x/%x/%x/%x/%x\n",nscp->cmnd[0],
                       nscp->cmnd[1],nscp->cmnd[2],nscp->cmnd[3],
                       nscp->cmnd[4],nscp->cmnd[5]));
                if ( (nscp->cmnd[4]&1) && !(ha->hdr[t].devtype&1) ) {
                    TRACE(("Prevent r. nonremov. drive->do nothing\n"));
                    nscp->result = DID_OK << 16;
                    nscp->sense_buffer[0] = 0;
                    if (!nscp->SCp.have_data_in)
                        nscp->SCp.have_data_in++;
                    else
                        nscp->scsi_done(nscp);
                } else {
                    nscp->cmnd[3] = (ha->hdr[t].devtype&1) ? 1:0;
                    TRACE(("Prevent/allow r. %d rem. drive %d\n",
                           nscp->cmnd[4],nscp->cmnd[3]));
                    if (!(cmd_index=gdth_fill_cache_cmd(hanum,nscp,t)))
                        this_cmd = FALSE;
                }
                break;
                
              case RESERVE:
              case RELEASE:
                TRACE2(("cache cmd %s\n",nscp->cmnd[0] == RESERVE ?
                        "RESERVE" : "RELEASE"));
                if (!(cmd_index=gdth_fill_cache_cmd(hanum,nscp,t)))
                    this_cmd = FALSE;
                break;
                
              case READ_6:
              case WRITE_6:
              case READ_10:
              case WRITE_10:
              case READ_16:
              case WRITE_16:
                if (ha->hdr[t].media_changed) {
                    /* return UNIT_ATTENTION */
                    TRACE2(("cmd 0x%x target %d: UNIT_ATTENTION\n",
                             nscp->cmnd[0], t));
                    ha->hdr[t].media_changed = FALSE;
                    memset((char*)nscp->sense_buffer,0,16);
                    nscp->sense_buffer[0] = 0x70;
                    nscp->sense_buffer[2] = UNIT_ATTENTION;
                    nscp->result = (DID_OK << 16) | (CHECK_CONDITION << 1);
                    if (!nscp->SCp.have_data_in)
                        nscp->SCp.have_data_in++;
                    else
                        nscp->scsi_done(nscp);
                } else if (!(cmd_index=gdth_fill_cache_cmd(hanum,nscp,t)))
                    this_cmd = FALSE;
                break;

              default:
                TRACE2(("cache cmd %x/%x/%x/%x/%x/%x unknown\n",nscp->cmnd[0],
                        nscp->cmnd[1],nscp->cmnd[2],nscp->cmnd[3],
                        nscp->cmnd[4],nscp->cmnd[5]));
                printk("GDT-HA %d: Unknown SCSI command 0x%x to cache service !\n",
                       hanum, nscp->cmnd[0]);
                nscp->result = DID_ABORT << 16;
                if (!nscp->SCp.have_data_in)
                    nscp->SCp.have_data_in++;
                else
                    nscp->scsi_done(nscp);
                break;
            }
        }

        if (!this_cmd)
            break;
        if (nscp == ha->req_first)
            ha->req_first = pscp = (Scsi_Cmnd *)nscp->SCp.ptr;
        else
            pscp->SCp.ptr = nscp->SCp.ptr;
        if (!next_cmd)
            break;
    }

    if (ha->cmd_cnt > 0) {
        gdth_release_event(hanum);
    }

    if (!gdth_polling) 
        spin_unlock_irqrestore(&ha->smp_lock, flags);

    if (gdth_polling && ha->cmd_cnt > 0) {
        if (!gdth_wait(hanum,cmd_index,POLL_TIMEOUT))
            printk("GDT-HA %d: Command %d timed out !\n",
                   hanum,cmd_index);
    }
}
   
static void gdth_copy_internal_data(int hanum,Scsi_Cmnd *scp,
                                    char *buffer,ushort count)
{
    ushort cpcount,i;
    ushort cpsum,cpnow;
    struct scatterlist *sl;
    gdth_ha_str *ha;
    char *address;

    cpcount = count<=(ushort)scp->request_bufflen ? count:(ushort)scp->request_bufflen;
    ha = HADATA(gdth_ctr_tab[hanum]);

    if (scp->use_sg) {
        sl = (struct scatterlist *)scp->request_buffer;
        for (i=0,cpsum=0; i<scp->use_sg; ++i,++sl) {
            unsigned long flags;
            cpnow = (ushort)sl->length;
            TRACE(("copy_internal() now %d sum %d count %d %d\n",
                          cpnow,cpsum,cpcount,(ushort)scp->bufflen));
            if (cpsum+cpnow > cpcount) 
                cpnow = cpcount - cpsum;
            cpsum += cpnow;
            if (!sl->page) {
                printk("GDT-HA %d: invalid sc/gt element in gdth_copy_internal_data()\n",
                       hanum);
                return;
            }
            local_irq_save(flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
            address = kmap_atomic(sl->page, KM_BIO_SRC_IRQ) + sl->offset;
            memcpy(address,buffer,cpnow);
            flush_dcache_page(sl->page);
            kunmap_atomic(address, KM_BIO_SRC_IRQ);
#else
            address = kmap_atomic(sl->page, KM_BH_IRQ) + sl->offset;
            memcpy(address,buffer,cpnow);
            flush_dcache_page(sl->page);
            kunmap_atomic(address, KM_BH_IRQ);
#endif
            local_irq_restore(flags);
            if (cpsum == cpcount)
                break;
            buffer += cpnow;
        }
    } else {
        TRACE(("copy_internal() count %d\n",cpcount));
        memcpy((char*)scp->request_buffer,buffer,cpcount);
    }
}

static int gdth_internal_cache_cmd(int hanum,Scsi_Cmnd *scp)
{
    register gdth_ha_str *ha;
    unchar t;
    gdth_inq_data inq;
    gdth_rdcap_data rdc;
    gdth_sense_data sd;
    gdth_modep_data mpd;

    ha = HADATA(gdth_ctr_tab[hanum]);
    t  = scp->device->id;
    TRACE(("gdth_internal_cache_cmd() cmd 0x%x hdrive %d\n",
           scp->cmnd[0],t));

    scp->result = DID_OK << 16;
    scp->sense_buffer[0] = 0;

    switch (scp->cmnd[0]) {
      case TEST_UNIT_READY:
      case VERIFY:
      case START_STOP:
        TRACE2(("Test/Verify/Start hdrive %d\n",t));
        break;

      case INQUIRY:
        TRACE2(("Inquiry hdrive %d devtype %d\n",
                t,ha->hdr[t].devtype));
        inq.type_qual = (ha->hdr[t].devtype&4) ? TYPE_ROM:TYPE_DISK;
        /* you can here set all disks to removable, if you want to do
           a flush using the ALLOW_MEDIUM_REMOVAL command */
        inq.modif_rmb = 0x00;
        if ((ha->hdr[t].devtype & 1) ||
            (ha->hdr[t].cluster_type & CLUSTER_DRIVE))
            inq.modif_rmb = 0x80;
        inq.version   = 2;
        inq.resp_aenc = 2;
        inq.add_length= 32;
        strcpy(inq.vendor,ha->oem_name);
        sprintf(inq.product,"Host Drive  #%02d",t);
        strcpy(inq.revision,"   ");
        gdth_copy_internal_data(hanum,scp,(char*)&inq,sizeof(gdth_inq_data));
        break;

      case REQUEST_SENSE:
        TRACE2(("Request sense hdrive %d\n",t));
        sd.errorcode = 0x70;
        sd.segno     = 0x00;
        sd.key       = NO_SENSE;
        sd.info      = 0;
        sd.add_length= 0;
        gdth_copy_internal_data(hanum,scp,(char*)&sd,sizeof(gdth_sense_data));
        break;

      case MODE_SENSE:
        TRACE2(("Mode sense hdrive %d\n",t));
        memset((char*)&mpd,0,sizeof(gdth_modep_data));
        mpd.hd.data_length = sizeof(gdth_modep_data);
        mpd.hd.dev_par     = (ha->hdr[t].devtype&2) ? 0x80:0;
        mpd.hd.bd_length   = sizeof(mpd.bd);
        mpd.bd.block_length[0] = (SECTOR_SIZE & 0x00ff0000) >> 16;
        mpd.bd.block_length[1] = (SECTOR_SIZE & 0x0000ff00) >> 8;
        mpd.bd.block_length[2] = (SECTOR_SIZE & 0x000000ff);
        gdth_copy_internal_data(hanum,scp,(char*)&mpd,sizeof(gdth_modep_data));
        break;

      case READ_CAPACITY:
        TRACE2(("Read capacity hdrive %d\n",t));
        if (ha->hdr[t].size > (ulong64)0xffffffff)
            rdc.last_block_no = 0xffffffff;
        else
            rdc.last_block_no = cpu_to_be32(ha->hdr[t].size-1);
        rdc.block_length  = cpu_to_be32(SECTOR_SIZE);
        gdth_copy_internal_data(hanum,scp,(char*)&rdc,sizeof(gdth_rdcap_data));
        break;

      case SERVICE_ACTION_IN:
        if ((scp->cmnd[1] & 0x1f) == SAI_READ_CAPACITY_16 &&
            (ha->cache_feat & GDT_64BIT)) {
            gdth_rdcap16_data rdc16;

            TRACE2(("Read capacity (16) hdrive %d\n",t));
            rdc16.last_block_no = cpu_to_be64(ha->hdr[t].size-1);
            rdc16.block_length  = cpu_to_be32(SECTOR_SIZE);
            gdth_copy_internal_data(hanum,scp,(char*)&rdc16,sizeof(gdth_rdcap16_data));
        } else { 
            scp->result = DID_ABORT << 16;
        }
        break;

      default:
        TRACE2(("Internal cache cmd 0x%x unknown\n",scp->cmnd[0]));
        break;
    }

    if (!scp->SCp.have_data_in)
        scp->SCp.have_data_in++;
    else 
        return 1;

    return 0;
}
    
static int gdth_fill_cache_cmd(int hanum,Scsi_Cmnd *scp,ushort hdrive)
{
    register gdth_ha_str *ha;
    register gdth_cmd_str *cmdp;
    struct scatterlist *sl;
    ulong32 cnt, blockcnt;
    ulong64 no, blockno;
    dma_addr_t phys_addr;
    int i, cmd_index, read_write, sgcnt, mode64;
    struct page *page;
    ulong offset;

    ha = HADATA(gdth_ctr_tab[hanum]);
    cmdp = ha->pccb;
    TRACE(("gdth_fill_cache_cmd() cmd 0x%x cmdsize %d hdrive %d\n",
                 scp->cmnd[0],scp->cmd_len,hdrive));

    if (ha->type==GDT_EISA && ha->cmd_cnt>0) 
        return 0;

    mode64 = (ha->cache_feat & GDT_64BIT) ? TRUE : FALSE;
    /* test for READ_16, WRITE_16 if !mode64 ? ---
       not required, should not occur due to error return on 
       READ_CAPACITY_16 */

    cmdp->Service = CACHESERVICE;
    cmdp->RequestBuffer = scp;
    /* search free command index */
    if (!(cmd_index=gdth_get_cmd_index(hanum))) {
        TRACE(("GDT: No free command index found\n"));
        return 0;
    }
    /* if it's the first command, set command semaphore */
    if (ha->cmd_cnt == 0)
        gdth_set_sema0(hanum);

    /* fill command */
    read_write = 0;
    if (scp->SCp.sent_command != -1) 
        cmdp->OpCode = scp->SCp.sent_command;   /* special cache cmd. */
    else if (scp->cmnd[0] == RESERVE) 
        cmdp->OpCode = GDT_RESERVE_DRV;
    else if (scp->cmnd[0] == RELEASE)
        cmdp->OpCode = GDT_RELEASE_DRV;
    else if (scp->cmnd[0] == ALLOW_MEDIUM_REMOVAL) {
        if (scp->cmnd[4] & 1)                   /* prevent ? */
            cmdp->OpCode = GDT_MOUNT;
        else if (scp->cmnd[3] & 1)              /* removable drive ? */
            cmdp->OpCode = GDT_UNMOUNT;
        else
            cmdp->OpCode = GDT_FLUSH;
    } else if (scp->cmnd[0] == WRITE_6 || scp->cmnd[0] == WRITE_10 ||
               scp->cmnd[0] == WRITE_12 || scp->cmnd[0] == WRITE_16
    ) {
        read_write = 1;
        if (gdth_write_through || ((ha->hdr[hdrive].rw_attribs & 1) && 
                                   (ha->cache_feat & GDT_WR_THROUGH)))
            cmdp->OpCode = GDT_WRITE_THR;
        else
            cmdp->OpCode = GDT_WRITE;
    } else {
        read_write = 2;
        cmdp->OpCode = GDT_READ;
    }

    cmdp->BoardNode = LOCALBOARD;
    if (mode64) {
        cmdp->u.cache64.DeviceNo = hdrive;
        cmdp->u.cache64.BlockNo  = 1;
        cmdp->u.cache64.sg_canz  = 0;
    } else {
        cmdp->u.cache.DeviceNo = hdrive;
        cmdp->u.cache.BlockNo  = 1;
        cmdp->u.cache.sg_canz  = 0;
    }

    if (read_write) {
        if (scp->cmd_len == 16) {
            memcpy(&no, &scp->cmnd[2], sizeof(ulong64));
            blockno = be64_to_cpu(no);
            memcpy(&cnt, &scp->cmnd[10], sizeof(ulong32));
            blockcnt = be32_to_cpu(cnt);
        } else if (scp->cmd_len == 10) {
            memcpy(&no, &scp->cmnd[2], sizeof(ulong32));
            blockno = be32_to_cpu(no);
            memcpy(&cnt, &scp->cmnd[7], sizeof(ushort));
            blockcnt = be16_to_cpu(cnt);
        } else {
            memcpy(&no, &scp->cmnd[0], sizeof(ulong32));
            blockno = be32_to_cpu(no) & 0x001fffffUL;
            blockcnt= scp->cmnd[4]==0 ? 0x100 : scp->cmnd[4];
        }
        if (mode64) {
            cmdp->u.cache64.BlockNo = blockno;
            cmdp->u.cache64.BlockCnt = blockcnt;
        } else {
            cmdp->u.cache.BlockNo = (ulong32)blockno;
            cmdp->u.cache.BlockCnt = blockcnt;
        }

        if (scp->use_sg) {
            sl = (struct scatterlist *)scp->request_buffer;
            sgcnt = scp->use_sg;
            scp->SCp.Status = GDTH_MAP_SG;
            scp->SCp.Message = (read_write == 1 ? 
                PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);   
            sgcnt = pci_map_sg(ha->pdev,sl,scp->use_sg,scp->SCp.Message);
            if (mode64) {
                cmdp->u.cache64.DestAddr= (ulong64)-1;
                cmdp->u.cache64.sg_canz = sgcnt;
                for (i=0; i<sgcnt; ++i,++sl) {
                    cmdp->u.cache64.sg_lst[i].sg_ptr = sg_dma_address(sl);
#ifdef GDTH_DMA_STATISTICS
                    if (cmdp->u.cache64.sg_lst[i].sg_ptr > (ulong64)0xffffffff)
                        ha->dma64_cnt++;
                    else
                        ha->dma32_cnt++;
#endif
                    cmdp->u.cache64.sg_lst[i].sg_len = sg_dma_len(sl);
                }
            } else {
                cmdp->u.cache.DestAddr= 0xffffffff;
                cmdp->u.cache.sg_canz = sgcnt;
                for (i=0; i<sgcnt; ++i,++sl) {
                    cmdp->u.cache.sg_lst[i].sg_ptr = sg_dma_address(sl);
#ifdef GDTH_DMA_STATISTICS
                    ha->dma32_cnt++;
#endif
                    cmdp->u.cache.sg_lst[i].sg_len = sg_dma_len(sl);
                }
            }

#ifdef GDTH_STATISTICS
            if (max_sg < (ulong32)sgcnt) {
                max_sg = (ulong32)sgcnt;
                TRACE3(("GDT: max_sg = %d\n",max_sg));
            }
#endif

        } else if (scp->request_bufflen) {
            scp->SCp.Status = GDTH_MAP_SINGLE;
            scp->SCp.Message = (read_write == 1 ? 
                PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
            page = virt_to_page(scp->request_buffer);
            offset = (ulong)scp->request_buffer & ~PAGE_MASK;
            phys_addr = pci_map_page(ha->pdev,page,offset,
                                     scp->request_bufflen,scp->SCp.Message);
            scp->SCp.dma_handle = phys_addr;
            if (mode64) {
                if (ha->cache_feat & SCATTER_GATHER) {
                    cmdp->u.cache64.DestAddr = (ulong64)-1;
                    cmdp->u.cache64.sg_canz = 1;
                    cmdp->u.cache64.sg_lst[0].sg_ptr = phys_addr;
                    cmdp->u.cache64.sg_lst[0].sg_len = scp->request_bufflen;
                    cmdp->u.cache64.sg_lst[1].sg_len = 0;
                } else {
                    cmdp->u.cache64.DestAddr  = phys_addr;
                    cmdp->u.cache64.sg_canz= 0;
                }
            } else {
                if (ha->cache_feat & SCATTER_GATHER) {
                    cmdp->u.cache.DestAddr = 0xffffffff;
                    cmdp->u.cache.sg_canz = 1;
                    cmdp->u.cache.sg_lst[0].sg_ptr = phys_addr;
                    cmdp->u.cache.sg_lst[0].sg_len = scp->request_bufflen;
                    cmdp->u.cache.sg_lst[1].sg_len = 0;
                } else {
                    cmdp->u.cache.DestAddr  = phys_addr;
                    cmdp->u.cache.sg_canz= 0;
                }
            }
        }
    }
    /* evaluate command size, check space */
    if (mode64) {
        TRACE(("cache cmd: addr. %x sganz %x sgptr0 %x sglen0 %x\n",
               cmdp->u.cache64.DestAddr,cmdp->u.cache64.sg_canz,
               cmdp->u.cache64.sg_lst[0].sg_ptr,
               cmdp->u.cache64.sg_lst[0].sg_len));
        TRACE(("cache cmd: cmd %d blockno. %d, blockcnt %d\n",
               cmdp->OpCode,cmdp->u.cache64.BlockNo,cmdp->u.cache64.BlockCnt));
        ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.cache64.sg_lst) +
            (ushort)cmdp->u.cache64.sg_canz * sizeof(gdth_sg64_str);
    } else {
        TRACE(("cache cmd: addr. %x sganz %x sgptr0 %x sglen0 %x\n",
               cmdp->u.cache.DestAddr,cmdp->u.cache.sg_canz,
               cmdp->u.cache.sg_lst[0].sg_ptr,
               cmdp->u.cache.sg_lst[0].sg_len));
        TRACE(("cache cmd: cmd %d blockno. %d, blockcnt %d\n",
               cmdp->OpCode,cmdp->u.cache.BlockNo,cmdp->u.cache.BlockCnt));
        ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.cache.sg_lst) +
            (ushort)cmdp->u.cache.sg_canz * sizeof(gdth_sg_str);
    }
    if (ha->cmd_len & 3)
        ha->cmd_len += (4 - (ha->cmd_len & 3));

    if (ha->cmd_cnt > 0) {
        if ((ha->cmd_offs_dpmem + ha->cmd_len + DPMEM_COMMAND_OFFSET) >
            ha->ic_all_size) {
            TRACE2(("gdth_fill_cache() DPMEM overflow\n"));
            ha->cmd_tab[cmd_index-2].cmnd = UNUSED_CMND;
            return 0;
        }
    }

    /* copy command */
    gdth_copy_command(hanum);
    return cmd_index;
}

static int gdth_fill_raw_cmd(int hanum,Scsi_Cmnd *scp,unchar b)
{
    register gdth_ha_str *ha;
    register gdth_cmd_str *cmdp;
    struct scatterlist *sl;
    ushort i;
    dma_addr_t phys_addr, sense_paddr;
    int cmd_index, sgcnt, mode64;
    unchar t,l;
    struct page *page;
    ulong offset;

    ha = HADATA(gdth_ctr_tab[hanum]);
    t = scp->device->id;
    l = scp->device->lun;
    cmdp = ha->pccb;
    TRACE(("gdth_fill_raw_cmd() cmd 0x%x bus %d ID %d LUN %d\n",
           scp->cmnd[0],b,t,l));

    if (ha->type==GDT_EISA && ha->cmd_cnt>0) 
        return 0;

    mode64 = (ha->raw_feat & GDT_64BIT) ? TRUE : FALSE;

    cmdp->Service = SCSIRAWSERVICE;
    cmdp->RequestBuffer = scp;
    /* search free command index */
    if (!(cmd_index=gdth_get_cmd_index(hanum))) {
        TRACE(("GDT: No free command index found\n"));
        return 0;
    }
    /* if it's the first command, set command semaphore */
    if (ha->cmd_cnt == 0)
        gdth_set_sema0(hanum);

    /* fill command */  
    if (scp->SCp.sent_command != -1) {
        cmdp->OpCode           = scp->SCp.sent_command; /* special raw cmd. */
        cmdp->BoardNode        = LOCALBOARD;
        if (mode64) {
            cmdp->u.raw64.direction = (scp->SCp.phase >> 8);
            TRACE2(("special raw cmd 0x%x param 0x%x\n", 
                    cmdp->OpCode, cmdp->u.raw64.direction));
            /* evaluate command size */
            ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.raw64.sg_lst);
        } else {
            cmdp->u.raw.direction  = (scp->SCp.phase >> 8);
            TRACE2(("special raw cmd 0x%x param 0x%x\n", 
                    cmdp->OpCode, cmdp->u.raw.direction));
            /* evaluate command size */
            ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.raw.sg_lst);
        }

    } else {
        page = virt_to_page(scp->sense_buffer);
        offset = (ulong)scp->sense_buffer & ~PAGE_MASK;
        sense_paddr = pci_map_page(ha->pdev,page,offset,
                                   16,PCI_DMA_FROMDEVICE);
        *(ulong32 *)&scp->SCp.buffer = (ulong32)sense_paddr;
        /* high part, if 64bit */
        *(ulong32 *)&scp->host_scribble = (ulong32)((ulong64)sense_paddr >> 32);
        cmdp->OpCode           = GDT_WRITE;             /* always */
        cmdp->BoardNode        = LOCALBOARD;
        if (mode64) { 
            cmdp->u.raw64.reserved   = 0;
            cmdp->u.raw64.mdisc_time = 0;
            cmdp->u.raw64.mcon_time  = 0;
            cmdp->u.raw64.clen       = scp->cmd_len;
            cmdp->u.raw64.target     = t;
            cmdp->u.raw64.lun        = l;
            cmdp->u.raw64.bus        = b;
            cmdp->u.raw64.priority   = 0;
            cmdp->u.raw64.sdlen      = scp->request_bufflen;
            cmdp->u.raw64.sense_len  = 16;
            cmdp->u.raw64.sense_data = sense_paddr;
            cmdp->u.raw64.direction  = 
                gdth_direction_tab[scp->cmnd[0]]==DOU ? GDTH_DATA_OUT:GDTH_DATA_IN;
            memcpy(cmdp->u.raw64.cmd,scp->cmnd,16);
        } else {
            cmdp->u.raw.reserved   = 0;
            cmdp->u.raw.mdisc_time = 0;
            cmdp->u.raw.mcon_time  = 0;
            cmdp->u.raw.clen       = scp->cmd_len;
            cmdp->u.raw.target     = t;
            cmdp->u.raw.lun        = l;
            cmdp->u.raw.bus        = b;
            cmdp->u.raw.priority   = 0;
            cmdp->u.raw.link_p     = 0;
            cmdp->u.raw.sdlen      = scp->request_bufflen;
            cmdp->u.raw.sense_len  = 16;
            cmdp->u.raw.sense_data = sense_paddr;
            cmdp->u.raw.direction  = 
                gdth_direction_tab[scp->cmnd[0]]==DOU ? GDTH_DATA_OUT:GDTH_DATA_IN;
            memcpy(cmdp->u.raw.cmd,scp->cmnd,12);
        }

        if (scp->use_sg) {
            sl = (struct scatterlist *)scp->request_buffer;
            sgcnt = scp->use_sg;
            scp->SCp.Status = GDTH_MAP_SG;
            scp->SCp.Message = PCI_DMA_BIDIRECTIONAL; 
            sgcnt = pci_map_sg(ha->pdev,sl,scp->use_sg,scp->SCp.Message);
            if (mode64) {
                cmdp->u.raw64.sdata = (ulong64)-1;
                cmdp->u.raw64.sg_ranz = sgcnt;
                for (i=0; i<sgcnt; ++i,++sl) {
                    cmdp->u.raw64.sg_lst[i].sg_ptr = sg_dma_address(sl);
#ifdef GDTH_DMA_STATISTICS
                    if (cmdp->u.raw64.sg_lst[i].sg_ptr > (ulong64)0xffffffff)
                        ha->dma64_cnt++;
                    else
                        ha->dma32_cnt++;
#endif
                    cmdp->u.raw64.sg_lst[i].sg_len = sg_dma_len(sl);
                }
            } else {
                cmdp->u.raw.sdata = 0xffffffff;
                cmdp->u.raw.sg_ranz = sgcnt;
                for (i=0; i<sgcnt; ++i,++sl) {
                    cmdp->u.raw.sg_lst[i].sg_ptr = sg_dma_address(sl);
#ifdef GDTH_DMA_STATISTICS
                    ha->dma32_cnt++;
#endif
                    cmdp->u.raw.sg_lst[i].sg_len = sg_dma_len(sl);
                }
            }

#ifdef GDTH_STATISTICS
            if (max_sg < sgcnt) {
                max_sg = sgcnt;
                TRACE3(("GDT: max_sg = %d\n",sgcnt));
            }
#endif

        } else if (scp->request_bufflen) {
            scp->SCp.Status = GDTH_MAP_SINGLE;
            scp->SCp.Message = PCI_DMA_BIDIRECTIONAL; 
            page = virt_to_page(scp->request_buffer);
            offset = (ulong)scp->request_buffer & ~PAGE_MASK;
            phys_addr = pci_map_page(ha->pdev,page,offset,
                                     scp->request_bufflen,scp->SCp.Message);
            scp->SCp.dma_handle = phys_addr;

            if (mode64) {
                if (ha->raw_feat & SCATTER_GATHER) {
                    cmdp->u.raw64.sdata  = (ulong64)-1;
                    cmdp->u.raw64.sg_ranz= 1;
                    cmdp->u.raw64.sg_lst[0].sg_ptr = phys_addr;
                    cmdp->u.raw64.sg_lst[0].sg_len = scp->request_bufflen;
                    cmdp->u.raw64.sg_lst[1].sg_len = 0;
                } else {
                    cmdp->u.raw64.sdata  = phys_addr;
                    cmdp->u.raw64.sg_ranz= 0;
                }
            } else {
                if (ha->raw_feat & SCATTER_GATHER) {
                    cmdp->u.raw.sdata  = 0xffffffff;
                    cmdp->u.raw.sg_ranz= 1;
                    cmdp->u.raw.sg_lst[0].sg_ptr = phys_addr;
                    cmdp->u.raw.sg_lst[0].sg_len = scp->request_bufflen;
                    cmdp->u.raw.sg_lst[1].sg_len = 0;
                } else {
                    cmdp->u.raw.sdata  = phys_addr;
                    cmdp->u.raw.sg_ranz= 0;
                }
            }
        }
        if (mode64) {
            TRACE(("raw cmd: addr. %x sganz %x sgptr0 %x sglen0 %x\n",
                   cmdp->u.raw64.sdata,cmdp->u.raw64.sg_ranz,
                   cmdp->u.raw64.sg_lst[0].sg_ptr,
                   cmdp->u.raw64.sg_lst[0].sg_len));
            /* evaluate command size */
            ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.raw64.sg_lst) +
                (ushort)cmdp->u.raw64.sg_ranz * sizeof(gdth_sg64_str);
        } else {
            TRACE(("raw cmd: addr. %x sganz %x sgptr0 %x sglen0 %x\n",
                   cmdp->u.raw.sdata,cmdp->u.raw.sg_ranz,
                   cmdp->u.raw.sg_lst[0].sg_ptr,
                   cmdp->u.raw.sg_lst[0].sg_len));
            /* evaluate command size */
            ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.raw.sg_lst) +
                (ushort)cmdp->u.raw.sg_ranz * sizeof(gdth_sg_str);
        }
    }
    /* check space */
    if (ha->cmd_len & 3)
        ha->cmd_len += (4 - (ha->cmd_len & 3));

    if (ha->cmd_cnt > 0) {
        if ((ha->cmd_offs_dpmem + ha->cmd_len + DPMEM_COMMAND_OFFSET) >
            ha->ic_all_size) {
            TRACE2(("gdth_fill_raw() DPMEM overflow\n"));
            ha->cmd_tab[cmd_index-2].cmnd = UNUSED_CMND;
            return 0;
        }
    }

    /* copy command */
    gdth_copy_command(hanum);
    return cmd_index;
}

static int gdth_special_cmd(int hanum,Scsi_Cmnd *scp)
{
    register gdth_ha_str *ha;
    register gdth_cmd_str *cmdp;
    int cmd_index;

    ha  = HADATA(gdth_ctr_tab[hanum]);
    cmdp= ha->pccb;
    TRACE2(("gdth_special_cmd(): "));

    if (ha->type==GDT_EISA && ha->cmd_cnt>0) 
        return 0;

    memcpy( cmdp, scp->request_buffer, sizeof(gdth_cmd_str));
    cmdp->RequestBuffer = scp;

    /* search free command index */
    if (!(cmd_index=gdth_get_cmd_index(hanum))) {
        TRACE(("GDT: No free command index found\n"));
        return 0;
    }

    /* if it's the first command, set command semaphore */
    if (ha->cmd_cnt == 0)
       gdth_set_sema0(hanum);

    /* evaluate command size, check space */
    if (cmdp->OpCode == GDT_IOCTL) {
        TRACE2(("IOCTL\n"));
        ha->cmd_len = 
            GDTOFFSOF(gdth_cmd_str,u.ioctl.p_param) + sizeof(ulong64);
    } else if (cmdp->Service == CACHESERVICE) {
        TRACE2(("cache command %d\n",cmdp->OpCode));
        if (ha->cache_feat & GDT_64BIT)
            ha->cmd_len = 
                GDTOFFSOF(gdth_cmd_str,u.cache64.sg_lst) + sizeof(gdth_sg64_str);
        else
            ha->cmd_len = 
                GDTOFFSOF(gdth_cmd_str,u.cache.sg_lst) + sizeof(gdth_sg_str);
    } else if (cmdp->Service == SCSIRAWSERVICE) {
        TRACE2(("raw command %d\n",cmdp->OpCode));
        if (ha->raw_feat & GDT_64BIT)
            ha->cmd_len = 
                GDTOFFSOF(gdth_cmd_str,u.raw64.sg_lst) + sizeof(gdth_sg64_str);
        else
            ha->cmd_len = 
                GDTOFFSOF(gdth_cmd_str,u.raw.sg_lst) + sizeof(gdth_sg_str);
    }

    if (ha->cmd_len & 3)
        ha->cmd_len += (4 - (ha->cmd_len & 3));

    if (ha->cmd_cnt > 0) {
        if ((ha->cmd_offs_dpmem + ha->cmd_len + DPMEM_COMMAND_OFFSET) >
            ha->ic_all_size) {
            TRACE2(("gdth_special_cmd() DPMEM overflow\n"));
            ha->cmd_tab[cmd_index-2].cmnd = UNUSED_CMND;
            return 0;
        }
    }

    /* copy command */
    gdth_copy_command(hanum);
    return cmd_index;
}    


/* Controller event handling functions */
static gdth_evt_str *gdth_store_event(gdth_ha_str *ha, ushort source, 
                                      ushort idx, gdth_evt_data *evt)
{
    gdth_evt_str *e;
    struct timeval tv;

    /* no GDTH_LOCK_HA() ! */
    TRACE2(("gdth_store_event() source %d idx %d\n", source, idx));
    if (source == 0)                        /* no source -> no event */
        return NULL;

    if (ebuffer[elastidx].event_source == source &&
        ebuffer[elastidx].event_idx == idx &&
        ((evt->size != 0 && ebuffer[elastidx].event_data.size != 0 &&
            !memcmp((char *)&ebuffer[elastidx].event_data.eu,
            (char *)&evt->eu, evt->size)) ||
        (evt->size == 0 && ebuffer[elastidx].event_data.size == 0 &&
            !strcmp((char *)&ebuffer[elastidx].event_data.event_string,
            (char *)&evt->event_string)))) { 
        e = &ebuffer[elastidx];
        do_gettimeofday(&tv);
        e->last_stamp = tv.tv_sec;
        ++e->same_count;
    } else {
        if (ebuffer[elastidx].event_source != 0) {  /* entry not free ? */
            ++elastidx;
            if (elastidx == MAX_EVENTS)
                elastidx = 0;
            if (elastidx == eoldidx) {              /* reached mark ? */
                ++eoldidx;
                if (eoldidx == MAX_EVENTS)
                    eoldidx = 0;
            }
        }
        e = &ebuffer[elastidx];
        e->event_source = source;
        e->event_idx = idx;
        do_gettimeofday(&tv);
        e->first_stamp = e->last_stamp = tv.tv_sec;
        e->same_count = 1;
        e->event_data = *evt;
        e->application = 0;
    }
    return e;
}

static int gdth_read_event(gdth_ha_str *ha, int handle, gdth_evt_str *estr)
{
    gdth_evt_str *e;
    int eindex;
    ulong flags;

    TRACE2(("gdth_read_event() handle %d\n", handle));
    spin_lock_irqsave(&ha->smp_lock, flags);
    if (handle == -1)
        eindex = eoldidx;
    else
        eindex = handle;
    estr->event_source = 0;

    if (eindex >= MAX_EVENTS) {
        spin_unlock_irqrestore(&ha->smp_lock, flags);
        return eindex;
    }
    e = &ebuffer[eindex];
    if (e->event_source != 0) {
        if (eindex != elastidx) {
            if (++eindex == MAX_EVENTS)
                eindex = 0;
        } else {
            eindex = -1;
        }
        memcpy(estr, e, sizeof(gdth_evt_str));
    }
    spin_unlock_irqrestore(&ha->smp_lock, flags);
    return eindex;
}

static void gdth_readapp_event(gdth_ha_str *ha,
                               unchar application, gdth_evt_str *estr)
{
    gdth_evt_str *e;
    int eindex;
    ulong flags;
    unchar found = FALSE;

    TRACE2(("gdth_readapp_event() app. %d\n", application));
    spin_lock_irqsave(&ha->smp_lock, flags);
    eindex = eoldidx;
    for (;;) {
        e = &ebuffer[eindex];
        if (e->event_source == 0)
            break;
        if ((e->application & application) == 0) {
            e->application |= application;
            found = TRUE;
            break;
        }
        if (eindex == elastidx)
            break;
        if (++eindex == MAX_EVENTS)
            eindex = 0;
    }
    if (found)
        memcpy(estr, e, sizeof(gdth_evt_str));
    else
        estr->event_source = 0;
    spin_unlock_irqrestore(&ha->smp_lock, flags);
}

static void gdth_clear_events(void)
{
    TRACE(("gdth_clear_events()"));

    eoldidx = elastidx = 0;
    ebuffer[0].event_source = 0;
}


/* SCSI interface functions */

static irqreturn_t gdth_interrupt(int irq,void *dev_id,struct pt_regs *regs)
{
    gdth_ha_str *ha2 = (gdth_ha_str *)dev_id;
    register gdth_ha_str *ha;
    gdt6m_dpram_str __iomem *dp6m_ptr = NULL;
    gdt6_dpram_str __iomem *dp6_ptr;
    gdt2_dpram_str __iomem *dp2_ptr;
    Scsi_Cmnd *scp;
    int hanum, rval, i;
    unchar IStatus;
    ushort Service;
    ulong flags = 0;
#ifdef INT_COAL
    int coalesced = FALSE;
    int next = FALSE;
    gdth_coal_status *pcs = NULL;
    int act_int_coal = 0;       
#endif

    TRACE(("gdth_interrupt() IRQ %d\n",irq));

    /* if polling and not from gdth_wait() -> return */
    if (gdth_polling) {
        if (!gdth_from_wait) {
            return IRQ_HANDLED;
        }
    }

    if (!gdth_polling)
        spin_lock_irqsave(&ha2->smp_lock, flags);
    wait_index = 0;

    /* search controller */
    if ((hanum = gdth_get_status(&IStatus,irq)) == -1) {
        /* spurious interrupt */
        if (!gdth_polling)
            spin_unlock_irqrestore(&ha2->smp_lock, flags);
            return IRQ_HANDLED;
    }
    ha = HADATA(gdth_ctr_tab[hanum]);

#ifdef GDTH_STATISTICS
    ++act_ints;
#endif

#ifdef INT_COAL
    /* See if the fw is returning coalesced status */
    if (IStatus == COALINDEX) {
        /* Coalesced status.  Setup the initial status 
           buffer pointer and flags */
        pcs = ha->coal_stat;
        coalesced = TRUE;        
        next = TRUE;
    }

    do {
        if (coalesced) {
            /* For coalesced requests all status
               information is found in the status buffer */
            IStatus = (unchar)(pcs->status & 0xff);
        }
#endif
    
        if (ha->type == GDT_EISA) {
            if (IStatus & 0x80) {                       /* error flag */
                IStatus &= ~0x80;
                ha->status = inw(ha->bmic + MAILBOXREG+8);
                TRACE2(("gdth_interrupt() error %d/%d\n",IStatus,ha->status));
            } else                                      /* no error */
                ha->status = S_OK;
            ha->info = inl(ha->bmic + MAILBOXREG+12);
            ha->service = inw(ha->bmic + MAILBOXREG+10);
            ha->info2 = inl(ha->bmic + MAILBOXREG+4);

            outb(0xff, ha->bmic + EDOORREG);    /* acknowledge interrupt */
            outb(0x00, ha->bmic + SEMA1REG);    /* reset status semaphore */
        } else if (ha->type == GDT_ISA) {
            dp2_ptr = ha->brd;
            if (IStatus & 0x80) {                       /* error flag */
                IStatus &= ~0x80;
                ha->status = gdth_readw(&dp2_ptr->u.ic.Status);
                TRACE2(("gdth_interrupt() error %d/%d\n",IStatus,ha->status));
            } else                                      /* no error */
                ha->status = S_OK;
            ha->info = gdth_readl(&dp2_ptr->u.ic.Info[0]);
            ha->service = gdth_readw(&dp2_ptr->u.ic.Service);
            ha->info2 = gdth_readl(&dp2_ptr->u.ic.Info[1]);

            gdth_writeb(0xff, &dp2_ptr->io.irqdel); /* acknowledge interrupt */
            gdth_writeb(0, &dp2_ptr->u.ic.Cmd_Index);/* reset command index */
            gdth_writeb(0, &dp2_ptr->io.Sema1);     /* reset status semaphore */
        } else if (ha->type == GDT_PCI) {
            dp6_ptr = ha->brd;
            if (IStatus & 0x80) {                       /* error flag */
                IStatus &= ~0x80;
                ha->status = gdth_readw(&dp6_ptr->u.ic.Status);
                TRACE2(("gdth_interrupt() error %d/%d\n",IStatus,ha->status));
            } else                                      /* no error */
                ha->status = S_OK;
            ha->info = gdth_readl(&dp6_ptr->u.ic.Info[0]);
            ha->service = gdth_readw(&dp6_ptr->u.ic.Service);
            ha->info2 = gdth_readl(&dp6_ptr->u.ic.Info[1]);

            gdth_writeb(0xff, &dp6_ptr->io.irqdel); /* acknowledge interrupt */
            gdth_writeb(0, &dp6_ptr->u.ic.Cmd_Index);/* reset command index */
            gdth_writeb(0, &dp6_ptr->io.Sema1);     /* reset status semaphore */
        } else if (ha->type == GDT_PCINEW) {
            if (IStatus & 0x80) {                       /* error flag */
                IStatus &= ~0x80;
                ha->status = inw(PTR2USHORT(&ha->plx->status));
                TRACE2(("gdth_interrupt() error %d/%d\n",IStatus,ha->status));
            } else
                ha->status = S_OK;
            ha->info = inl(PTR2USHORT(&ha->plx->info[0]));
            ha->service = inw(PTR2USHORT(&ha->plx->service));
            ha->info2 = inl(PTR2USHORT(&ha->plx->info[1]));

            outb(0xff, PTR2USHORT(&ha->plx->edoor_reg)); 
            outb(0x00, PTR2USHORT(&ha->plx->sema1_reg)); 
        } else if (ha->type == GDT_PCIMPR) {
            dp6m_ptr = ha->brd;
            if (IStatus & 0x80) {                       /* error flag */
                IStatus &= ~0x80;
#ifdef INT_COAL
                if (coalesced)
                    ha->status = pcs->ext_status && 0xffff;
                else 
#endif
                    ha->status = gdth_readw(&dp6m_ptr->i960r.status);
                TRACE2(("gdth_interrupt() error %d/%d\n",IStatus,ha->status));
            } else                                      /* no error */
                ha->status = S_OK;
#ifdef INT_COAL
            /* get information */
            if (coalesced) {    
                ha->info = pcs->info0;
                ha->info2 = pcs->info1;
                ha->service = (pcs->ext_status >> 16) && 0xffff;
            } else
#endif
            {
                ha->info = gdth_readl(&dp6m_ptr->i960r.info[0]);
                ha->service = gdth_readw(&dp6m_ptr->i960r.service);
                ha->info2 = gdth_readl(&dp6m_ptr->i960r.info[1]);
            }
            /* event string */
            if (IStatus == ASYNCINDEX) {
                if (ha->service != SCREENSERVICE &&
                    (ha->fw_vers & 0xff) >= 0x1a) {
                    ha->dvr.severity = gdth_readb
                        (&((gdt6m_dpram_str __iomem *)ha->brd)->i960r.severity);
                    for (i = 0; i < 256; ++i) {
                        ha->dvr.event_string[i] = gdth_readb
                            (&((gdt6m_dpram_str __iomem *)ha->brd)->i960r.evt_str[i]);
                        if (ha->dvr.event_string[i] == 0)
                            break;
                    }
                }
            }
#ifdef INT_COAL
            /* Make sure that non coalesced interrupts get cleared
               before being handled by gdth_async_event/gdth_sync_event */
            if (!coalesced)
#endif                          
            {
                gdth_writeb(0xff, &dp6m_ptr->i960r.edoor_reg);
                gdth_writeb(0, &dp6m_ptr->i960r.sema1_reg);
            }
        } else {
            TRACE2(("gdth_interrupt() unknown controller type\n"));
            if (!gdth_polling)
                spin_unlock_irqrestore(&ha2->smp_lock, flags);
            return IRQ_HANDLED;
        }

        TRACE(("gdth_interrupt() index %d stat %d info %d\n",
               IStatus,ha->status,ha->info));

        if (gdth_from_wait) {
            wait_hanum = hanum;
            wait_index = (int)IStatus;
        }

        if (IStatus == ASYNCINDEX) {
            TRACE2(("gdth_interrupt() async. event\n"));
            gdth_async_event(hanum);
            if (!gdth_polling)
                spin_unlock_irqrestore(&ha2->smp_lock, flags);
            gdth_next(hanum);
            return IRQ_HANDLED;
        } 

        if (IStatus == SPEZINDEX) {
            TRACE2(("Service unknown or not initialized !\n"));
            ha->dvr.size = sizeof(ha->dvr.eu.driver);
            ha->dvr.eu.driver.ionode = hanum;
            gdth_store_event(ha, ES_DRIVER, 4, &ha->dvr);
            if (!gdth_polling)
                spin_unlock_irqrestore(&ha2->smp_lock, flags);
            return IRQ_HANDLED;
        }
        scp     = ha->cmd_tab[IStatus-2].cmnd;
        Service = ha->cmd_tab[IStatus-2].service;
        ha->cmd_tab[IStatus-2].cmnd = UNUSED_CMND;
        if (scp == UNUSED_CMND) {
            TRACE2(("gdth_interrupt() index to unused command (%d)\n",IStatus));
            ha->dvr.size = sizeof(ha->dvr.eu.driver);
            ha->dvr.eu.driver.ionode = hanum;
            ha->dvr.eu.driver.index = IStatus;
            gdth_store_event(ha, ES_DRIVER, 1, &ha->dvr);
            if (!gdth_polling)
                spin_unlock_irqrestore(&ha2->smp_lock, flags);
            return IRQ_HANDLED;
        }
        if (scp == INTERNAL_CMND) {
            TRACE(("gdth_interrupt() answer to internal command\n"));
            if (!gdth_polling)
                spin_unlock_irqrestore(&ha2->smp_lock, flags);
            return IRQ_HANDLED;
        }

        TRACE(("gdth_interrupt() sync. status\n"));
        rval = gdth_sync_event(hanum,Service,IStatus,scp);
        if (!gdth_polling)
            spin_unlock_irqrestore(&ha2->smp_lock, flags);
        if (rval == 2) {
            gdth_putq(hanum,scp,scp->SCp.this_residual);
        } else if (rval == 1) {
            scp->scsi_done(scp);
        }

#ifdef INT_COAL
        if (coalesced) {
            /* go to the next status in the status buffer */
            ++pcs;
#ifdef GDTH_STATISTICS
            ++act_int_coal;
            if (act_int_coal > max_int_coal) {
                max_int_coal = act_int_coal;
                printk("GDT: max_int_coal = %d\n",(ushort)max_int_coal);
            }
#endif      
            /* see if there is another status */
            if (pcs->status == 0)    
                /* Stop the coalesce loop */
                next = FALSE;
        }
    } while (next);

    /* coalescing only for new GDT_PCIMPR controllers available */      
    if (ha->type == GDT_PCIMPR && coalesced) {
        gdth_writeb(0xff, &dp6m_ptr->i960r.edoor_reg);
        gdth_writeb(0, &dp6m_ptr->i960r.sema1_reg);
    }
#endif

    gdth_next(hanum);
    return IRQ_HANDLED;
}

static int gdth_sync_event(int hanum,int service,unchar index,Scsi_Cmnd *scp)
{
    register gdth_ha_str *ha;
    gdth_msg_str *msg;
    gdth_cmd_str *cmdp;
    unchar b, t;

    ha   = HADATA(gdth_ctr_tab[hanum]);
    cmdp = ha->pccb;
    TRACE(("gdth_sync_event() serv %d status %d\n",
           service,ha->status));

    if (service == SCREENSERVICE) {
        msg  = ha->pmsg;
        TRACE(("len: %d, answer: %d, ext: %d, alen: %d\n",
               msg->msg_len,msg->msg_answer,msg->msg_ext,msg->msg_alen));
        if (msg->msg_len > MSGLEN+1)
            msg->msg_len = MSGLEN+1;
        if (msg->msg_len)
            if (!(msg->msg_answer && msg->msg_ext)) {
                msg->msg_text[msg->msg_len] = '\0';
                printk("%s",msg->msg_text);
            }

        if (msg->msg_ext && !msg->msg_answer) {
            while (gdth_test_busy(hanum))
                gdth_delay(0);
            cmdp->Service       = SCREENSERVICE;
            cmdp->RequestBuffer = SCREEN_CMND;
            gdth_get_cmd_index(hanum);
            gdth_set_sema0(hanum);
            cmdp->OpCode        = GDT_READ;
            cmdp->BoardNode     = LOCALBOARD;
            cmdp->u.screen.reserved  = 0;
            cmdp->u.screen.su.msg.msg_handle= msg->msg_handle;
            cmdp->u.screen.su.msg.msg_addr  = ha->msg_phys;
            ha->cmd_offs_dpmem = 0;
            ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.screen.su.msg.msg_addr) 
                + sizeof(ulong64);
            ha->cmd_cnt = 0;
            gdth_copy_command(hanum);
            gdth_release_event(hanum);
            return 0;
        }

        if (msg->msg_answer && msg->msg_alen) {
            /* default answers (getchar() not possible) */
            if (msg->msg_alen == 1) {
                msg->msg_alen = 0;
                msg->msg_len = 1;
                msg->msg_text[0] = 0;
            } else {
                msg->msg_alen -= 2;
                msg->msg_len = 2;
                msg->msg_text[0] = 1;
                msg->msg_text[1] = 0;
            }
            msg->msg_ext    = 0;
            msg->msg_answer = 0;
            while (gdth_test_busy(hanum))
                gdth_delay(0);
            cmdp->Service       = SCREENSERVICE;
            cmdp->RequestBuffer = SCREEN_CMND;
            gdth_get_cmd_index(hanum);
            gdth_set_sema0(hanum);
            cmdp->OpCode        = GDT_WRITE;
            cmdp->BoardNode     = LOCALBOARD;
            cmdp->u.screen.reserved  = 0;
            cmdp->u.screen.su.msg.msg_handle= msg->msg_handle;
            cmdp->u.screen.su.msg.msg_addr  = ha->msg_phys;
            ha->cmd_offs_dpmem = 0;
            ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.screen.su.msg.msg_addr) 
                + sizeof(ulong64);
            ha->cmd_cnt = 0;
            gdth_copy_command(hanum);
            gdth_release_event(hanum);
            return 0;
        }
        printk("\n");

    } else {
        b = virt_ctr ? NUMDATA(scp->device->host)->busnum : scp->device->channel;
        t = scp->device->id;
        if (scp->SCp.sent_command == -1 && b != ha->virt_bus) {
            ha->raw[BUS_L2P(ha,b)].io_cnt[t]--;
        }
        /* cache or raw service */
        if (ha->status == S_BSY) {
            TRACE2(("Controller busy -> retry !\n"));
            if (scp->SCp.sent_command == GDT_MOUNT)
                scp->SCp.sent_command = GDT_CLUST_INFO;
            /* retry */
            return 2;
        }
        if (scp->SCp.Status == GDTH_MAP_SG) 
            pci_unmap_sg(ha->pdev,scp->request_buffer,
                         scp->use_sg,scp->SCp.Message);
        else if (scp->SCp.Status == GDTH_MAP_SINGLE) 
            pci_unmap_page(ha->pdev,scp->SCp.dma_handle,
                           scp->request_bufflen,scp->SCp.Message);
        if (scp->SCp.buffer) {
            dma_addr_t addr;
            addr = (dma_addr_t)*(ulong32 *)&scp->SCp.buffer;
            if (scp->host_scribble)
                addr += (dma_addr_t)
                    ((ulong64)(*(ulong32 *)&scp->host_scribble) << 32);
            pci_unmap_page(ha->pdev,addr,16,PCI_DMA_FROMDEVICE);
        }

        if (ha->status == S_OK) {
            scp->SCp.Status = S_OK;
            scp->SCp.Message = ha->info;
            if (scp->SCp.sent_command != -1) {
                TRACE2(("gdth_sync_event(): special cmd 0x%x OK\n",
                        scp->SCp.sent_command));
                /* special commands GDT_CLUST_INFO/GDT_MOUNT ? */
                if (scp->SCp.sent_command == GDT_CLUST_INFO) {
                    ha->hdr[t].cluster_type = (unchar)ha->info;
                    if (!(ha->hdr[t].cluster_type & 
                        CLUSTER_MOUNTED)) {
                        /* NOT MOUNTED -> MOUNT */
                        scp->SCp.sent_command = GDT_MOUNT;
                        if (ha->hdr[t].cluster_type & 
                            CLUSTER_RESERVED) {
                            /* cluster drive RESERVED (on the other node) */
                            scp->SCp.phase = -2;      /* reservation conflict */
                        }
                    } else {
                        scp->SCp.sent_command = -1;
                    }
                } else {
                    if (scp->SCp.sent_command == GDT_MOUNT) {
                        ha->hdr[t].cluster_type |= CLUSTER_MOUNTED;
                        ha->hdr[t].media_changed = TRUE;
                    } else if (scp->SCp.sent_command == GDT_UNMOUNT) {
                        ha->hdr[t].cluster_type &= ~CLUSTER_MOUNTED;
                        ha->hdr[t].media_changed = TRUE;
                    } 
                    scp->SCp.sent_command = -1;
                }
                /* retry */
                scp->SCp.this_residual = HIGH_PRI;
                return 2;
            } else {
                /* RESERVE/RELEASE ? */
                if (scp->cmnd[0] == RESERVE) {
                    ha->hdr[t].cluster_type |= CLUSTER_RESERVED;
                } else if (scp->cmnd[0] == RELEASE) {
                    ha->hdr[t].cluster_type &= ~CLUSTER_RESERVED;
                }           
                scp->result = DID_OK << 16;
                scp->sense_buffer[0] = 0;
            }
        } else {
            scp->SCp.Status = ha->status;
            scp->SCp.Message = ha->info;

            if (scp->SCp.sent_command != -1) {
                TRACE2(("gdth_sync_event(): special cmd 0x%x error 0x%x\n",
                        scp->SCp.sent_command, ha->status));
                if (scp->SCp.sent_command == GDT_SCAN_START ||
                    scp->SCp.sent_command == GDT_SCAN_END) {
                    scp->SCp.sent_command = -1;
                    /* retry */
                    scp->SCp.this_residual = HIGH_PRI;
                    return 2;
                }
                memset((char*)scp->sense_buffer,0,16);
                scp->sense_buffer[0] = 0x70;
                scp->sense_buffer[2] = NOT_READY;
                scp->result = (DID_OK << 16) | (CHECK_CONDITION << 1);
            } else if (service == CACHESERVICE) {
                if (ha->status == S_CACHE_UNKNOWN &&
                    (ha->hdr[t].cluster_type & 
                     CLUSTER_RESERVE_STATE) == CLUSTER_RESERVE_STATE) {
                    /* bus reset -> force GDT_CLUST_INFO */
                    ha->hdr[t].cluster_type &= ~CLUSTER_RESERVED;
                }
                memset((char*)scp->sense_buffer,0,16);
                if (ha->status == (ushort)S_CACHE_RESERV) {
                    scp->result = (DID_OK << 16) | (RESERVATION_CONFLICT << 1);
                } else {
                    scp->sense_buffer[0] = 0x70;
                    scp->sense_buffer[2] = NOT_READY;
                    scp->result = (DID_OK << 16) | (CHECK_CONDITION << 1);
                }
                if (scp->done != gdth_scsi_done) {
                    ha->dvr.size = sizeof(ha->dvr.eu.sync);
                    ha->dvr.eu.sync.ionode  = hanum;
                    ha->dvr.eu.sync.service = service;
                    ha->dvr.eu.sync.status  = ha->status;
                    ha->dvr.eu.sync.info    = ha->info;
                    ha->dvr.eu.sync.hostdrive = t;
                    if (ha->status >= 0x8000)
                        gdth_store_event(ha, ES_SYNC, 0, &ha->dvr);
                    else
                        gdth_store_event(ha, ES_SYNC, service, &ha->dvr);
                }
            } else {
                /* sense buffer filled from controller firmware (DMA) */
                if (ha->status != S_RAW_SCSI || ha->info >= 0x100) {
                    scp->result = DID_BAD_TARGET << 16;
                } else {
                    scp->result = (DID_OK << 16) | ha->info;
                }
            }
        }
        if (!scp->SCp.have_data_in)
            scp->SCp.have_data_in++;
        else 
            return 1;
    }

    return 0;
}

static char *async_cache_tab[] = {
/* 0*/  "\011\000\002\002\002\004\002\006\004"
        "GDT HA %u, service %u, async. status %u/%lu unknown",
/* 1*/  "\011\000\002\002\002\004\002\006\004"
        "GDT HA %u, service %u, async. status %u/%lu unknown",
/* 2*/  "\005\000\002\006\004"
        "GDT HA %u, Host Drive %lu not ready",
/* 3*/  "\005\000\002\006\004"
        "GDT HA %u, Host Drive %lu: REASSIGN not successful and/or data error on reassigned blocks. Drive may crash in the future and should be replaced",
/* 4*/  "\005\000\002\006\004"
        "GDT HA %u, mirror update on Host Drive %lu failed",
/* 5*/  "\005\000\002\006\004"
        "GDT HA %u, Mirror Drive %lu failed",
/* 6*/  "\005\000\002\006\004"
        "GDT HA %u, Mirror Drive %lu: REASSIGN not successful and/or data error on reassigned blocks. Drive may crash in the future and should be replaced",
/* 7*/  "\005\000\002\006\004"
        "GDT HA %u, Host Drive %lu write protected",
/* 8*/  "\005\000\002\006\004"
        "GDT HA %u, media changed in Host Drive %lu",
/* 9*/  "\005\000\002\006\004"
        "GDT HA %u, Host Drive %lu is offline",
/*10*/  "\005\000\002\006\004"
        "GDT HA %u, media change of Mirror Drive %lu",
/*11*/  "\005\000\002\006\004"
        "GDT HA %u, Mirror Drive %lu is write protected",
/*12*/  "\005\000\002\006\004"
        "GDT HA %u, general error on Host Drive %lu. Please check the devices of this drive!",
/*13*/  "\007\000\002\006\002\010\002"
        "GDT HA %u, Array Drive %u: Cache Drive %u failed",
/*14*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: FAIL state entered",
/*15*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: error",
/*16*/  "\007\000\002\006\002\010\002"
        "GDT HA %u, Array Drive %u: failed drive replaced by Cache Drive %u",
/*17*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: parity build failed",
/*18*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: drive rebuild failed",
/*19*/  "\005\000\002\010\002"
        "GDT HA %u, Test of Hot Fix %u failed",
/*20*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: drive build finished successfully",
/*21*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: drive rebuild finished successfully",
/*22*/  "\007\000\002\006\002\010\002"
        "GDT HA %u, Array Drive %u: Hot Fix %u activated",
/*23*/  "\005\000\002\006\002"
        "GDT HA %u, Host Drive %u: processing of i/o aborted due to serious drive error",
/*24*/  "\005\000\002\010\002"
        "GDT HA %u, mirror update on Cache Drive %u completed",
/*25*/  "\005\000\002\010\002"
        "GDT HA %u, mirror update on Cache Drive %lu failed",
/*26*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: drive rebuild started",
/*27*/  "\005\000\002\012\001"
        "GDT HA %u, Fault bus %u: SHELF OK detected",
/*28*/  "\005\000\002\012\001"
        "GDT HA %u, Fault bus %u: SHELF not OK detected",
/*29*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: Auto Hot Plug started",
/*30*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: new disk detected",
/*31*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: old disk detected",
/*32*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: plugging an active disk is invalid",
/*33*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: invalid device detected",
/*34*/  "\011\000\002\012\001\013\001\006\004"
        "GDT HA %u, Fault bus %u, ID %u: insufficient disk capacity (%lu MB required)",
/*35*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: disk write protected",
/*36*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: disk not available",
/*37*/  "\007\000\002\012\001\006\004"
        "GDT HA %u, Fault bus %u: swap detected (%lu)",
/*38*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: Auto Hot Plug finished successfully",
/*39*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: Auto Hot Plug aborted due to user Hot Plug",
/*40*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: Auto Hot Plug aborted",
/*41*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, Fault bus %u, ID %u: Auto Hot Plug for Hot Fix started",
/*42*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: drive build started",
/*43*/  "\003\000\002"
        "GDT HA %u, DRAM parity error detected",
/*44*/  "\005\000\002\006\002"
        "GDT HA %u, Mirror Drive %u: update started",
/*45*/  "\007\000\002\006\002\010\002"
        "GDT HA %u, Mirror Drive %u: Hot Fix %u activated",
/*46*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: no matching Pool Hot Fix Drive available",
/*47*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: Pool Hot Fix Drive available",
/*48*/  "\005\000\002\006\002"
        "GDT HA %u, Mirror Drive %u: no matching Pool Hot Fix Drive available",
/*49*/  "\005\000\002\006\002"
        "GDT HA %u, Mirror Drive %u: Pool Hot Fix Drive available",
/*50*/  "\007\000\002\012\001\013\001"
        "GDT HA %u, SCSI bus %u, ID %u: IGNORE_WIDE_RESIDUE message received",
/*51*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: expand started",
/*52*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: expand finished successfully",
/*53*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: expand failed",
/*54*/  "\003\000\002"
        "GDT HA %u, CPU temperature critical",
/*55*/  "\003\000\002"
        "GDT HA %u, CPU temperature OK",
/*56*/  "\005\000\002\006\004"
        "GDT HA %u, Host drive %lu created",
/*57*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: expand restarted",
/*58*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: expand stopped",
/*59*/  "\005\000\002\010\002"
        "GDT HA %u, Mirror Drive %u: drive build quited",
/*60*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: parity build quited",
/*61*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: drive rebuild quited",
/*62*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: parity verify started",
/*63*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: parity verify done",
/*64*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: parity verify failed",
/*65*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: parity error detected",
/*66*/  "\005\000\002\006\002"
        "GDT HA %u, Array Drive %u: parity verify quited",
/*67*/  "\005\000\002\006\002"
        "GDT HA %u, Host Drive %u reserved",
/*68*/  "\005\000\002\006\002"
        "GDT HA %u, Host Drive %u mounted and released",
/*69*/  "\005\000\002\006\002"
        "GDT HA %u, Host Drive %u released",
/*70*/  "\003\000\002"
        "GDT HA %u, DRAM error detected and corrected with ECC",
/*71*/  "\003\000\002"
        "GDT HA %u, Uncorrectable DRAM error detected with ECC",
/*72*/  "\011\000\002\012\001\013\001\014\001"
        "GDT HA %u, SCSI bus %u, ID %u, LUN %u: reassigning block",
/*73*/  "\005\000\002\006\002"
        "GDT HA %u, Host drive %u resetted locally",
/*74*/  "\005\000\002\006\002"
        "GDT HA %u, Host drive %u resetted remotely",
/*75*/  "\003\000\002"
        "GDT HA %u, async. status 75 unknown",
};


static int gdth_async_event(int hanum)
{
    gdth_ha_str *ha;
    gdth_cmd_str *cmdp;
    int cmd_index;

    ha  = HADATA(gdth_ctr_tab[hanum]);
    cmdp= ha->pccb;
    TRACE2(("gdth_async_event() ha %d serv %d\n",
            hanum,ha->service));

    if (ha->service == SCREENSERVICE) {
        if (ha->status == MSG_REQUEST) {
            while (gdth_test_busy(hanum))
                gdth_delay(0);
            cmdp->Service       = SCREENSERVICE;
            cmdp->RequestBuffer = SCREEN_CMND;
            cmd_index = gdth_get_cmd_index(hanum);
            gdth_set_sema0(hanum);
            cmdp->OpCode        = GDT_READ;
            cmdp->BoardNode     = LOCALBOARD;
            cmdp->u.screen.reserved  = 0;
            cmdp->u.screen.su.msg.msg_handle= MSG_INV_HANDLE;
            cmdp->u.screen.su.msg.msg_addr  = ha->msg_phys;
            ha->cmd_offs_dpmem = 0;
            ha->cmd_len = GDTOFFSOF(gdth_cmd_str,u.screen.su.msg.msg_addr) 
                + sizeof(ulong64);
            ha->cmd_cnt = 0;
            gdth_copy_command(hanum);
            if (ha->type == GDT_EISA)
                printk("[EISA slot %d] ",(ushort)ha->brd_phys);
            else if (ha->type == GDT_ISA)
                printk("[DPMEM 0x%4X] ",(ushort)ha->brd_phys);
            else 
                printk("[PCI %d/%d] ",(ushort)(ha->brd_phys>>8),
                       (ushort)((ha->brd_phys>>3)&0x1f));
            gdth_release_event(hanum);
        }

    } else {
        if (ha->type == GDT_PCIMPR && 
            (ha->fw_vers & 0xff) >= 0x1a) {
            ha->dvr.size = 0;
            ha->dvr.eu.async.ionode = hanum;
            ha->dvr.eu.async.status  = ha->status;
            /* severity and event_string already set! */
        } else {        
            ha->dvr.size = sizeof(ha->dvr.eu.async);
            ha->dvr.eu.async.ionode   = hanum;
            ha->dvr.eu.async.service = ha->service;
            ha->dvr.eu.async.status  = ha->status;
            ha->dvr.eu.async.info    = ha->info;
            *(ulong32 *)ha->dvr.eu.async.scsi_coord  = ha->info2;
        }
        gdth_store_event( ha, ES_ASYNC, ha->service, &ha->dvr );
        gdth_log_event( &ha->dvr, NULL );
    
        /* new host drive from expand? */
        if (ha->service == CACHESERVICE && ha->status == 56) {
            TRACE2(("gdth_async_event(): new host drive %d created\n",
                    (ushort)ha->info));
            /* gdth_analyse_hdrive(hanum, (ushort)ha->info); */
        }   
    }
    return 1;
}

static void gdth_log_event(gdth_evt_data *dvr, char *buffer)
{
    gdth_stackframe stack;
    char *f = NULL;
    int i,j;

    TRACE2(("gdth_log_event()\n"));
    if (dvr->size == 0) {
        if (buffer == NULL) {
            printk("Adapter %d: %s\n",dvr->eu.async.ionode,dvr->event_string); 
        } else {
            sprintf(buffer,"Adapter %d: %s\n",
                dvr->eu.async.ionode,dvr->event_string); 
        }
    } else if (dvr->eu.async.service == CACHESERVICE && 
        INDEX_OK(dvr->eu.async.status, async_cache_tab)) {
        TRACE2(("GDT: Async. event cache service, event no.: %d\n",
                dvr->eu.async.status));
        
        f = async_cache_tab[dvr->eu.async.status];
        
        /* i: parameter to push, j: stack element to fill */
        for (j=0,i=1; i < f[0]; i+=2) {
            switch (f[i+1]) {
              case 4:
                stack.b[j++] = *(ulong32*)&dvr->eu.stream[(int)f[i]];
                break;
              case 2:
                stack.b[j++] = *(ushort*)&dvr->eu.stream[(int)f[i]];
                break;
              case 1:
                stack.b[j++] = *(unchar*)&dvr->eu.stream[(int)f[i]];
                break;
              default:
                break;
            }
        }
        
        if (buffer == NULL) {
            printk(&f[(int)f[0]],stack); 
            printk("\n");
        } else {
            sprintf(buffer,&f[(int)f[0]],stack); 
        }

    } else {
        if (buffer == NULL) {
            printk("GDT HA %u, Unknown async. event service %d event no. %d\n",
                   dvr->eu.async.ionode,dvr->eu.async.service,dvr->eu.async.status);
        } else {
            sprintf(buffer,"GDT HA %u, Unknown async. event service %d event no. %d",
                    dvr->eu.async.ionode,dvr->eu.async.service,dvr->eu.async.status);
        }
    }
}

#ifdef GDTH_STATISTICS
static void gdth_timeout(ulong data)
{
    ulong32 i;
    Scsi_Cmnd *nscp;
    gdth_ha_str *ha;
    ulong flags;
    int hanum = 0;

    ha = HADATA(gdth_ctr_tab[hanum]);
    spin_lock_irqsave(&ha->smp_lock, flags);

    for (act_stats=0,i=0; i<GDTH_MAXCMDS; ++i) 
        if (ha->cmd_tab[i].cmnd != UNUSED_CMND)
            ++act_stats;

    for (act_rq=0,nscp=ha->req_first; nscp; nscp=(Scsi_Cmnd*)nscp->SCp.ptr)
        ++act_rq;

    TRACE2(("gdth_to(): ints %d, ios %d, act_stats %d, act_rq %d\n",
            act_ints, act_ios, act_stats, act_rq));
    act_ints = act_ios = 0;

    gdth_timer.expires = jiffies + 30 * HZ;
    add_timer(&gdth_timer);
    spin_unlock_irqrestore(&ha->smp_lock, flags);
}
#endif

static void __init internal_setup(char *str,int *ints)
{
    int i, argc;
    char *cur_str, *argv;

    TRACE2(("internal_setup() str %s ints[0] %d\n", 
            str ? str:"NULL", ints ? ints[0]:0));

    /* read irq[] from ints[] */
    if (ints) {
        argc = ints[0];
        if (argc > 0) {
            if (argc > MAXHA)
                argc = MAXHA;
            for (i = 0; i < argc; ++i)
                irq[i] = ints[i+1];
        }
    }

    /* analyse string */
    argv = str;
    while (argv && (cur_str = strchr(argv, ':'))) {
        int val = 0, c = *++cur_str;
        
        if (c == 'n' || c == 'N')
            val = 0;
        else if (c == 'y' || c == 'Y')
            val = 1;
        else
            val = (int)simple_strtoul(cur_str, NULL, 0);

        if (!strncmp(argv, "disable:", 8))
            disable = val;
        else if (!strncmp(argv, "reserve_mode:", 13))
            reserve_mode = val;
        else if (!strncmp(argv, "reverse_scan:", 13))
            reverse_scan = val;
        else if (!strncmp(argv, "hdr_channel:", 12))
            hdr_channel = val;
        else if (!strncmp(argv, "max_ids:", 8))
            max_ids = val;
        else if (!strncmp(argv, "rescan:", 7))
            rescan = val;
        else if (!strncmp(argv, "virt_ctr:", 9))
            virt_ctr = val;
        else if (!strncmp(argv, "shared_access:", 14))
            shared_access = val;
        else if (!strncmp(argv, "probe_eisa_isa:", 15))
            probe_eisa_isa = val;
        else if (!strncmp(argv, "reserve_list:", 13)) {
            reserve_list[0] = val;
            for (i = 1; i < MAX_RES_ARGS; i++) {
                cur_str = strchr(cur_str, ',');
                if (!cur_str)
                    break;
                if (!isdigit((int)*++cur_str)) {
                    --cur_str;          
                    break;
                }
                reserve_list[i] = 
                    (int)simple_strtoul(cur_str, NULL, 0);
            }
            if (!cur_str)
                break;
            argv = ++cur_str;
            continue;
        }

        if ((argv = strchr(argv, ',')))
            ++argv;
    }
}

int __init option_setup(char *str)
{
    int ints[MAXHA];
    char *cur = str;
    int i = 1;

    TRACE2(("option_setup() str %s\n", str ? str:"NULL")); 

    while (cur && isdigit(*cur) && i <= MAXHA) {
        ints[i++] = simple_strtoul(cur, NULL, 0);
        if ((cur = strchr(cur, ',')) != NULL) cur++;
    }

    ints[0] = i - 1;
    internal_setup(cur, ints);
    return 1;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int __init gdth_detect(struct scsi_host_template *shtp)
#else
static int __init gdth_detect(Scsi_Host_Template *shtp)
#endif
{
    struct Scsi_Host *shp;
    gdth_pci_str pcistr[MAXHA];
    gdth_ha_str *ha;
    ulong32 isa_bios;
    ushort eisa_slot;
    int i,hanum,cnt,ctr,err;
    unchar b;
    
 
#ifdef DEBUG_GDTH
    printk("GDT: This driver contains debugging information !! Trace level = %d\n",
        DebugState);
    printk("     Destination of debugging information: ");
#ifdef __SERIAL__
#ifdef __COM2__
    printk("Serial port COM2\n");
#else
    printk("Serial port COM1\n");
#endif
#else
    printk("Console\n");
#endif
    gdth_delay(3000);
#endif

    TRACE(("gdth_detect()\n"));

    if (disable) {
        printk("GDT-HA: Controller driver disabled from command line !\n");
        return 0;
    }

    printk("GDT-HA: Storage RAID Controller Driver. Version: %s\n",GDTH_VERSION_STR);
    /* initializations */
    gdth_polling = TRUE; b = 0;
    gdth_clear_events();

    /* As default we do not probe for EISA or ISA controllers */
    if (probe_eisa_isa) {    
        /* scanning for controllers, at first: ISA controller */
        for (isa_bios=0xc8000UL; isa_bios<=0xd8000UL; isa_bios+=0x8000UL) {
            dma_addr_t scratch_dma_handle;
            scratch_dma_handle = 0;

            if (gdth_ctr_count >= MAXHA) 
                break;
            if (gdth_search_isa(isa_bios)) {        /* controller found */
                shp = scsi_register(shtp,sizeof(gdth_ext_str));
                if (shp == NULL)
                    continue;  

                ha = HADATA(shp);
                if (!gdth_init_isa(isa_bios,ha)) {
                    scsi_unregister(shp);
                    continue;
                }
#ifdef __ia64__
                break;
#else
                /* controller found and initialized */
                printk("Configuring GDT-ISA HA at BIOS 0x%05X IRQ %u DRQ %u\n",
                       isa_bios,ha->irq,ha->drq);

                if (request_irq(ha->irq,gdth_interrupt,SA_INTERRUPT,"gdth",ha)) {
                    printk("GDT-ISA: Unable to allocate IRQ\n");
                    scsi_unregister(shp);
                    continue;
                }
                if (request_dma(ha->drq,"gdth")) {
                    printk("GDT-ISA: Unable to allocate DMA channel\n");
                    free_irq(ha->irq,ha);
                    scsi_unregister(shp);
                    continue;
                }
                set_dma_mode(ha->drq,DMA_MODE_CASCADE);
                enable_dma(ha->drq);
                shp->unchecked_isa_dma = 1;
                shp->irq = ha->irq;
                shp->dma_channel = ha->drq;
                hanum = gdth_ctr_count;         
                gdth_ctr_tab[gdth_ctr_count++] = shp;
                gdth_ctr_vtab[gdth_ctr_vcount++] = shp;

                NUMDATA(shp)->hanum = (ushort)hanum;
                NUMDATA(shp)->busnum= 0;

                ha->pccb = CMDDATA(shp);
                ha->ccb_phys = 0L;
                ha->pdev = NULL;
                ha->pscratch = pci_alloc_consistent(ha->pdev, GDTH_SCRATCH, 
                                                    &scratch_dma_handle);
                ha->scratch_phys = scratch_dma_handle;
                ha->pmsg = pci_alloc_consistent(ha->pdev, sizeof(gdth_msg_str), 
                                                &scratch_dma_handle);
                ha->msg_phys = scratch_dma_handle;
#ifdef INT_COAL
                ha->coal_stat = (gdth_coal_status *)
                    pci_alloc_consistent(ha->pdev, sizeof(gdth_coal_status) *
                        MAXOFFSETS, &scratch_dma_handle);
                ha->coal_stat_phys = scratch_dma_handle;
#endif

                ha->scratch_busy = FALSE;
                ha->req_first = NULL;
                ha->tid_cnt = MAX_HDRIVES;
                if (max_ids > 0 && max_ids < ha->tid_cnt)
                    ha->tid_cnt = max_ids;
                for (i=0; i<GDTH_MAXCMDS; ++i)
                    ha->cmd_tab[i].cmnd = UNUSED_CMND;
                ha->scan_mode = rescan ? 0x10 : 0;

                if (ha->pscratch == NULL || ha->pmsg == NULL || 
                    !gdth_search_drives(hanum)) {
                    printk("GDT-ISA: Error during device scan\n");
                    --gdth_ctr_count;
                    --gdth_ctr_vcount;

#ifdef INT_COAL
                    if (ha->coal_stat)
                        pci_free_consistent(ha->pdev, sizeof(gdth_coal_status) *
                                            MAXOFFSETS, ha->coal_stat,
                                            ha->coal_stat_phys);
#endif
                    if (ha->pscratch)
                        pci_free_consistent(ha->pdev, GDTH_SCRATCH, 
                                            ha->pscratch, ha->scratch_phys);
                    if (ha->pmsg)
                        pci_free_consistent(ha->pdev, sizeof(gdth_msg_str), 
                                            ha->pmsg, ha->msg_phys);

                    free_irq(ha->irq,ha);
                    scsi_unregister(shp);
                    continue;
                }
                if (hdr_channel < 0 || hdr_channel > ha->bus_cnt)
                    hdr_channel = ha->bus_cnt;
                ha->virt_bus = hdr_channel;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,20) && \
    LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
                shp->highmem_io  = 0;
#endif
                if (ha->cache_feat & ha->raw_feat & ha->screen_feat & GDT_64BIT) 
                    shp->max_cmd_len = 16;

                shp->max_id      = ha->tid_cnt;
                shp->max_lun     = MAXLUN;
                shp->max_channel = virt_ctr ? 0 : ha->bus_cnt;
                if (virt_ctr) {
                    virt_ctr = 1;
                    /* register addit. SCSI channels as virtual controllers */
                    for (b = 1; b < ha->bus_cnt + 1; ++b) {
                        shp = scsi_register(shtp,sizeof(gdth_num_str));
                        shp->unchecked_isa_dma = 1;
                        shp->irq = ha->irq;
                        shp->dma_channel = ha->drq;
                        gdth_ctr_vtab[gdth_ctr_vcount++] = shp;
                        NUMDATA(shp)->hanum = (ushort)hanum;
                        NUMDATA(shp)->busnum = b;
                    }
                }  

                spin_lock_init(&ha->smp_lock);
                gdth_enable_int(hanum);
#endif /* !__ia64__ */
            }
        }

        /* scanning for EISA controllers */
        for (eisa_slot=0x1000; eisa_slot<=0x8000; eisa_slot+=0x1000) {
            dma_addr_t scratch_dma_handle;
            scratch_dma_handle = 0;

            if (gdth_ctr_count >= MAXHA) 
                break;
            if (gdth_search_eisa(eisa_slot)) {      /* controller found */
                shp = scsi_register(shtp,sizeof(gdth_ext_str));
                if (shp == NULL)
                    continue;  

                ha = HADATA(shp);
                if (!gdth_init_eisa(eisa_slot,ha)) {
                    scsi_unregister(shp);
                    continue;
                }
                /* controller found and initialized */
                printk("Configuring GDT-EISA HA at Slot %d IRQ %u\n",
                       eisa_slot>>12,ha->irq);

                if (request_irq(ha->irq,gdth_interrupt,SA_INTERRUPT,"gdth",ha)) {
                    printk("GDT-EISA: Unable to allocate IRQ\n");
                    scsi_unregister(shp);
                    continue;
                }
                shp->unchecked_isa_dma = 0;
                shp->irq = ha->irq;
                shp->dma_channel = 0xff;
                hanum = gdth_ctr_count;
                gdth_ctr_tab[gdth_ctr_count++] = shp;
                gdth_ctr_vtab[gdth_ctr_vcount++] = shp;

                NUMDATA(shp)->hanum = (ushort)hanum;
                NUMDATA(shp)->busnum= 0;
                TRACE2(("EISA detect Bus 0: hanum %d\n",
                        NUMDATA(shp)->hanum));

                ha->pccb = CMDDATA(shp);
                ha->ccb_phys = 0L; 

                ha->pdev = NULL;
                ha->pscratch = pci_alloc_consistent(ha->pdev, GDTH_SCRATCH, 
                                                    &scratch_dma_handle);
                ha->scratch_phys = scratch_dma_handle;
                ha->pmsg = pci_alloc_consistent(ha->pdev, sizeof(gdth_msg_str), 
                                                &scratch_dma_handle);
                ha->msg_phys = scratch_dma_handle;
#ifdef INT_COAL
                ha->coal_stat = (gdth_coal_status *)
                    pci_alloc_consistent(ha->pdev, sizeof(gdth_coal_status) *
                                         MAXOFFSETS, &scratch_dma_handle);
                ha->coal_stat_phys = scratch_dma_handle;
#endif
                ha->ccb_phys = 
                    pci_map_single(ha->pdev,ha->pccb,
                                   sizeof(gdth_cmd_str),PCI_DMA_BIDIRECTIONAL);
                ha->scratch_busy = FALSE;
                ha->req_first = NULL;
                ha->tid_cnt = MAX_HDRIVES;
                if (max_ids > 0 && max_ids < ha->tid_cnt)
                    ha->tid_cnt = max_ids;
                for (i=0; i<GDTH_MAXCMDS; ++i)
                    ha->cmd_tab[i].cmnd = UNUSED_CMND;
                ha->scan_mode = rescan ? 0x10 : 0;

                if (ha->pscratch == NULL || ha->pmsg == NULL || 
                    !gdth_search_drives(hanum)) {
                    printk("GDT-EISA: Error during device scan\n");
                    --gdth_ctr_count;
                    --gdth_ctr_vcount;
#ifdef INT_COAL
                    if (ha->coal_stat)
                        pci_free_consistent(ha->pdev, sizeof(gdth_coal_status) *
                                            MAXOFFSETS, ha->coal_stat,
                                            ha->coal_stat_phys);
#endif
                    if (ha->pscratch)
                        pci_free_consistent(ha->pdev, GDTH_SCRATCH, 
                                            ha->pscratch, ha->scratch_phys);
                    if (ha->pmsg)
                        pci_free_consistent(ha->pdev, sizeof(gdth_msg_str), 
                                            ha->pmsg, ha->msg_phys);
                    if (ha->ccb_phys)
                        pci_unmap_single(ha->pdev,ha->ccb_phys,
                                        sizeof(gdth_cmd_str),PCI_DMA_BIDIRECTIONAL);
                    free_irq(ha->irq,ha);
                    scsi_unregister(shp);
                    continue;
                }
                if (hdr_channel < 0 || hdr_channel > ha->bus_cnt)
                    hdr_channel = ha->bus_cnt;
                ha->virt_bus = hdr_channel;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,20) && \
    LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
                shp->highmem_io  = 0;
#endif
                if (ha->cache_feat & ha->raw_feat & ha->screen_feat & GDT_64BIT) 
                    shp->max_cmd_len = 16;

                shp->max_id      = ha->tid_cnt;
                shp->max_lun     = MAXLUN;
                shp->max_channel = virt_ctr ? 0 : ha->bus_cnt;
                if (virt_ctr) {
                    virt_ctr = 1;
                    /* register addit. SCSI channels as virtual controllers */
                    for (b = 1; b < ha->bus_cnt + 1; ++b) {
                        shp = scsi_register(shtp,sizeof(gdth_num_str));
                        shp->unchecked_isa_dma = 0;
                        shp->irq = ha->irq;
                        shp->dma_channel = 0xff;
                        gdth_ctr_vtab[gdth_ctr_vcount++] = shp;
                        NUMDATA(shp)->hanum = (ushort)hanum;
                        NUMDATA(shp)->busnum = b;
                    }
                }  

                spin_lock_init(&ha->smp_lock);
                gdth_enable_int(hanum);
            }
        }
    }

    /* scanning for PCI controllers */
    cnt = gdth_search_pci(pcistr);
    printk("GDT-HA: Found %d PCI Storage RAID Controllers\n",cnt);
    gdth_sort_pci(pcistr,cnt);
    for (ctr = 0; ctr < cnt; ++ctr) {
        dma_addr_t scratch_dma_handle;
        scratch_dma_handle = 0;

        if (gdth_ctr_count >= MAXHA)
            break;
        shp = scsi_register(shtp,sizeof(gdth_ext_str));
        if (shp == NULL)
            continue;  

        ha = HADATA(shp);
        if (!gdth_init_pci(&pcistr[ctr],ha)) {
            scsi_unregister(shp);
            continue;
        }
        /* controller found and initialized */
        printk("Configuring GDT-PCI HA at %d/%d IRQ %u\n",
               pcistr[ctr].bus,PCI_SLOT(pcistr[ctr].device_fn),ha->irq);

        if (request_irq(ha->irq, gdth_interrupt,
                        SA_INTERRUPT|SA_SHIRQ, "gdth", ha))
        {
            printk("GDT-PCI: Unable to allocate IRQ\n");
            scsi_unregister(shp);
            continue;
        }
        shp->unchecked_isa_dma = 0;
        shp->irq = ha->irq;
        shp->dma_channel = 0xff;
        hanum = gdth_ctr_count;
        gdth_ctr_tab[gdth_ctr_count++] = shp;
        gdth_ctr_vtab[gdth_ctr_vcount++] = shp;

        NUMDATA(shp)->hanum = (ushort)hanum;
        NUMDATA(shp)->busnum= 0;

        ha->pccb = CMDDATA(shp);
        ha->ccb_phys = 0L;

        ha->pscratch = pci_alloc_consistent(ha->pdev, GDTH_SCRATCH, 
                                            &scratch_dma_handle);
        ha->scratch_phys = scratch_dma_handle;
        ha->pmsg = pci_alloc_consistent(ha->pdev, sizeof(gdth_msg_str), 
                                        &scratch_dma_handle);
        ha->msg_phys = scratch_dma_handle;
#ifdef INT_COAL
        ha->coal_stat = (gdth_coal_status *)
            pci_alloc_consistent(ha->pdev, sizeof(gdth_coal_status) *
                                 MAXOFFSETS, &scratch_dma_handle);
        ha->coal_stat_phys = scratch_dma_handle;
#endif
        ha->scratch_busy = FALSE;
        ha->req_first = NULL;
        ha->tid_cnt = pcistr[ctr].device_id >= 0x200 ? MAXID : MAX_HDRIVES;
        if (max_ids > 0 && max_ids < ha->tid_cnt)
            ha->tid_cnt = max_ids;
        for (i=0; i<GDTH_MAXCMDS; ++i)
            ha->cmd_tab[i].cmnd = UNUSED_CMND;
        ha->scan_mode = rescan ? 0x10 : 0;

        err = FALSE;
        if (ha->pscratch == NULL || ha->pmsg == NULL || 
            !gdth_search_drives(hanum)) {
            err = TRUE;
        } else {
            if (hdr_channel < 0 || hdr_channel > ha->bus_cnt)
                hdr_channel = ha->bus_cnt;
            ha->virt_bus = hdr_channel;


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
            scsi_set_pci_device(shp, pcistr[ctr].pdev);
#endif
            if (!(ha->cache_feat & ha->raw_feat & ha->screen_feat &GDT_64BIT)||
                /* 64-bit DMA only supported from FW >= x.43 */
                (!ha->dma64_support)) {
                if (pci_set_dma_mask(pcistr[ctr].pdev, DMA_32BIT_MASK)) {
                    printk(KERN_WARNING "GDT-PCI %d: Unable to set 32-bit DMA\n", hanum);
                    err = TRUE;
                }
            } else {
                shp->max_cmd_len = 16;
                if (!pci_set_dma_mask(pcistr[ctr].pdev, DMA_64BIT_MASK)) {
                    printk("GDT-PCI %d: 64-bit DMA enabled\n", hanum);
                } else if (pci_set_dma_mask(pcistr[ctr].pdev, DMA_32BIT_MASK)) {
                    printk(KERN_WARNING "GDT-PCI %d: Unable to set 64/32-bit DMA\n", hanum);
                    err = TRUE;
                }
            }
        }

        if (err) {
            printk("GDT-PCI %d: Error during device scan\n", hanum);
            --gdth_ctr_count;
            --gdth_ctr_vcount;
#ifdef INT_COAL
            if (ha->coal_stat)
                pci_free_consistent(ha->pdev, sizeof(gdth_coal_status) *
                                    MAXOFFSETS, ha->coal_stat,
                                    ha->coal_stat_phys);
#endif
            if (ha->pscratch)
                pci_free_consistent(ha->pdev, GDTH_SCRATCH, 
                                    ha->pscratch, ha->scratch_phys);
            if (ha->pmsg)
                pci_free_consistent(ha->pdev, sizeof(gdth_msg_str), 
                                    ha->pmsg, ha->msg_phys);
            free_irq(ha->irq,ha);
            scsi_unregister(shp);
            continue;
        }

        shp->max_id      = ha->tid_cnt;
        shp->max_lun     = MAXLUN;
        shp->max_channel = virt_ctr ? 0 : ha->bus_cnt;
        if (virt_ctr) {
            virt_ctr = 1;
            /* register addit. SCSI channels as virtual controllers */
            for (b = 1; b < ha->bus_cnt + 1; ++b) {
                shp = scsi_register(shtp,sizeof(gdth_num_str));
                shp->unchecked_isa_dma = 0;
                shp->irq = ha->irq;
                shp->dma_channel = 0xff;
                gdth_ctr_vtab[gdth_ctr_vcount++] = shp;
                NUMDATA(shp)->hanum = (ushort)hanum;
                NUMDATA(shp)->busnum = b;
            }
        }  

        spin_lock_init(&ha->smp_lock);
        gdth_enable_int(hanum);
    }
    
    TRACE2(("gdth_detect() %d controller detected\n",gdth_ctr_count));
    if (gdth_ctr_count > 0) {
#ifdef GDTH_STATISTICS
        TRACE2(("gdth_detect(): Initializing timer !\n"));
        init_timer(&gdth_timer);
        gdth_timer.expires = jiffies + HZ;
        gdth_timer.data = 0L;
        gdth_timer.function = gdth_timeout;
        add_timer(&gdth_timer);
#endif
        major = register_chrdev(0,"gdth",&gdth_fops);
        notifier_disabled = 0;
        register_reboot_notifier(&gdth_notifier);
    }
    gdth_polling = FALSE;
    return gdth_ctr_vcount;
}

static int gdth_release(struct Scsi_Host *shp)
{
    int hanum;
    gdth_ha_str *ha;

    TRACE2(("gdth_release()\n"));
    if (NUMDATA(shp)->busnum == 0) {
        hanum = NUMDATA(shp)->hanum;
        ha    = HADATA(gdth_ctr_tab[hanum]);
        if (ha->sdev) {
            scsi_free_host_dev(ha->sdev);
            ha->sdev = NULL;
        }
        gdth_flush(hanum);

        if (shp->irq) {
            free_irq(shp->irq,ha);
        }
#ifndef __ia64__
        if (shp->dma_channel != 0xff) {
            free_dma(shp->dma_channel);
        }
#endif
#ifdef INT_COAL
        if (ha->coal_stat)
            pci_free_consistent(ha->pdev, sizeof(gdth_coal_status) *
                                MAXOFFSETS, ha->coal_stat, ha->coal_stat_phys);
#endif
        if (ha->pscratch)
            pci_free_consistent(ha->pdev, GDTH_SCRATCH, 
                                ha->pscratch, ha->scratch_phys);
        if (ha->pmsg)
            pci_free_consistent(ha->pdev, sizeof(gdth_msg_str), 
                                ha->pmsg, ha->msg_phys);
        if (ha->ccb_phys)
            pci_unmap_single(ha->pdev,ha->ccb_phys,
                             sizeof(gdth_cmd_str),PCI_DMA_BIDIRECTIONAL);
        gdth_ctr_released++;
        TRACE2(("gdth_release(): HA %d of %d\n", 
                gdth_ctr_released, gdth_ctr_count));

        if (gdth_ctr_released == gdth_ctr_count) {
#ifdef GDTH_STATISTICS
            del_timer(&gdth_timer);
#endif
            unregister_chrdev(major,"gdth");
            unregister_reboot_notifier(&gdth_notifier);
        }
    }

    scsi_unregister(shp);
    return 0;
}
            

static const char *gdth_ctr_name(int hanum)
{
    gdth_ha_str *ha;

    TRACE2(("gdth_ctr_name()\n"));

    ha    = HADATA(gdth_ctr_tab[hanum]);

    if (ha->type == GDT_EISA) {
        switch (ha->stype) {
          case GDT3_ID:
            return("GDT3000/3020");
          case GDT3A_ID:
            return("GDT3000A/3020A/3050A");
          case GDT3B_ID:
            return("GDT3000B/3010A");
        }
    } else if (ha->type == GDT_ISA) {
        return("GDT2000/2020");
    } else if (ha->type == GDT_PCI) {
        switch (ha->stype) {
          case PCI_DEVICE_ID_VORTEX_GDT60x0:
            return("GDT6000/6020/6050");
          case PCI_DEVICE_ID_VORTEX_GDT6000B:
            return("GDT6000B/6010");
        }
    } 
    /* new controllers (GDT_PCINEW, GDT_PCIMPR, ..) use board_info IOCTL! */

    return("");
}

static const char *gdth_info(struct Scsi_Host *shp)
{
    int hanum;
    gdth_ha_str *ha;

    TRACE2(("gdth_info()\n"));
    hanum = NUMDATA(shp)->hanum;
    ha    = HADATA(gdth_ctr_tab[hanum]);

    return ((const char *)ha->binfo.type_string);
}

static int gdth_eh_bus_reset(Scsi_Cmnd *scp)
{
    int i, hanum;
    gdth_ha_str *ha;
    ulong flags;
    Scsi_Cmnd *cmnd;
    unchar b;

    TRACE2(("gdth_eh_bus_reset()\n"));

    hanum = NUMDATA(scp->device->host)->hanum;
    b = virt_ctr ? NUMDATA(scp->device->host)->busnum : scp->device->channel;
    ha    = HADATA(gdth_ctr_tab[hanum]);

    /* clear command tab */
    spin_lock_irqsave(&ha->smp_lock, flags);
    for (i = 0; i < GDTH_MAXCMDS; ++i) {
        cmnd = ha->cmd_tab[i].cmnd;
        if (!SPECIAL_SCP(cmnd) && cmnd->device->channel == b)
            ha->cmd_tab[i].cmnd = UNUSED_CMND;
    }
    spin_unlock_irqrestore(&ha->smp_lock, flags);

    if (b == ha->virt_bus) {
        /* host drives */
        for (i = 0; i < MAX_HDRIVES; ++i) {
            if (ha->hdr[i].present) {
                spin_lock_irqsave(&ha->smp_lock, flags);
                gdth_polling = TRUE;
                while (gdth_test_busy(hanum))
                    gdth_delay(0);
                if (gdth_internal_cmd(hanum, CACHESERVICE, 
                                      GDT_CLUST_RESET, i, 0, 0))
                    ha->hdr[i].cluster_type &= ~CLUSTER_RESERVED;
                gdth_polling = FALSE;
                spin_unlock_irqrestore(&ha->smp_lock, flags);
            }
        }
    } else {
        /* raw devices */
        spin_lock_irqsave(&ha->smp_lock, flags);
        for (i = 0; i < MAXID; ++i)
            ha->raw[BUS_L2P(ha,b)].io_cnt[i] = 0;
        gdth_polling = TRUE;
        while (gdth_test_busy(hanum))
            gdth_delay(0);
        gdth_internal_cmd(hanum, SCSIRAWSERVICE, GDT_RESET_BUS,
                          BUS_L2P(ha,b), 0, 0);
        gdth_polling = FALSE;
        spin_unlock_irqrestore(&ha->smp_lock, flags);
    }
    return SUCCESS;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int gdth_bios_param(struct scsi_device *sdev,struct block_device *bdev,sector_t cap,int *ip)
#else
static int gdth_bios_param(Disk *disk,kdev_t dev,int *ip)
#endif
{
    unchar b, t;
    int hanum;
    gdth_ha_str *ha;
    struct scsi_device *sd;
    unsigned capacity;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    sd = sdev;
    capacity = cap;
#else
    sd = disk->device;
    capacity = disk->capacity;
#endif
    hanum = NUMDATA(sd->host)->hanum;
    b = virt_ctr ? NUMDATA(sd->host)->busnum : sd->channel;
    t = sd->id;
    TRACE2(("gdth_bios_param() ha %d bus %d target %d\n", hanum, b, t)); 
    ha = HADATA(gdth_ctr_tab[hanum]);

    if (b != ha->virt_bus || ha->hdr[t].heads == 0) {
        /* raw device or host drive without mapping information */
        TRACE2(("Evaluate mapping\n"));
        gdth_eval_mapping(capacity,&ip[2],&ip[0],&ip[1]);
    } else {
        ip[0] = ha->hdr[t].heads;
        ip[1] = ha->hdr[t].secs;
        ip[2] = capacity / ip[0] / ip[1];
    }

    TRACE2(("gdth_bios_param(): %d heads, %d secs, %d cyls\n",
            ip[0],ip[1],ip[2]));
    return 0;
}


static int gdth_queuecommand(Scsi_Cmnd *scp,void (*done)(Scsi_Cmnd *))
{
    int hanum;
    int priority;

    TRACE(("gdth_queuecommand() cmd 0x%x\n", scp->cmnd[0]));
    
    scp->scsi_done = (void *)done;
    scp->SCp.have_data_in = 1;
    scp->SCp.phase = -1;
    scp->SCp.sent_command = -1;
    scp->SCp.Status = GDTH_MAP_NONE;
    scp->SCp.buffer = (struct scatterlist *)NULL;

    hanum = NUMDATA(scp->device->host)->hanum;
#ifdef GDTH_STATISTICS
    ++act_ios;
#endif

    priority = DEFAULT_PRI;
    if (scp->done == gdth_scsi_done)
        priority = scp->SCp.this_residual;
    else
        gdth_update_timeout(hanum, scp, scp->timeout_per_command * 6);

    gdth_putq( hanum, scp, priority );
    gdth_next( hanum );
    return 0;
}


static int gdth_open(struct inode *inode, struct file *filep)
{
    gdth_ha_str *ha;
    int i;

    for (i = 0; i < gdth_ctr_count; i++) {
        ha = HADATA(gdth_ctr_tab[i]);
        if (!ha->sdev)
            ha->sdev = scsi_get_host_dev(gdth_ctr_tab[i]);
    }

    TRACE(("gdth_open()\n"));
    return 0;
}

static int gdth_close(struct inode *inode, struct file *filep)
{
    TRACE(("gdth_close()\n"));
    return 0;
}

static int ioc_event(void __user *arg)
{
    gdth_ioctl_event evt;
    gdth_ha_str *ha;
    ulong flags;

    if (copy_from_user(&evt, arg, sizeof(gdth_ioctl_event)) ||
        evt.ionode >= gdth_ctr_count)
        return -EFAULT;
    ha = HADATA(gdth_ctr_tab[evt.ionode]);

    if (evt.erase == 0xff) {
        if (evt.event.event_source == ES_TEST)
            evt.event.event_data.size=sizeof(evt.event.event_data.eu.test); 
        else if (evt.event.event_source == ES_DRIVER)
            evt.event.event_data.size=sizeof(evt.event.event_data.eu.driver); 
        else if (evt.event.event_source == ES_SYNC)
            evt.event.event_data.size=sizeof(evt.event.event_data.eu.sync); 
        else
            evt.event.event_data.size=sizeof(evt.event.event_data.eu.async);
        spin_lock_irqsave(&ha->smp_lock, flags);
        gdth_store_event(ha, evt.event.event_source, evt.event.event_idx,
                         &evt.event.event_data);
        spin_unlock_irqrestore(&ha->smp_lock, flags);
    } else if (evt.erase == 0xfe) {
        gdth_clear_events();
    } else if (evt.erase == 0) {
        evt.handle = gdth_read_event(ha, evt.handle, &evt.event);
    } else {
        gdth_readapp_event(ha, evt.erase, &evt.event);
    }     
    if (copy_to_user(arg, &evt, sizeof(gdth_ioctl_event)))
        return -EFAULT;
    return 0;
}

static int ioc_lockdrv(void __user *arg)
{
    gdth_ioctl_lockdrv ldrv;
    unchar i, j;
    ulong flags;
    gdth_ha_str *ha;

    if (copy_from_user(&ldrv, arg, sizeof(gdth_ioctl_lockdrv)) ||
        ldrv.ionode >= gdth_ctr_count)
        return -EFAULT;
    ha = HADATA(gdth_ctr_tab[ldrv.ionode]);
 
    for (i = 0; i < ldrv.drive_cnt && i < MAX_HDRIVES; ++i) {
        j = ldrv.drives[i];
        if (j >= MAX_HDRIVES || !ha->hdr[j].present)
            continue;
        if (ldrv.lock) {
            spin_lock_irqsave(&ha->smp_lock, flags);
            ha->hdr[j].lock = 1;
            spin_unlock_irqrestore(&ha->smp_lock, flags);
            gdth_wait_completion(ldrv.ionode, ha->bus_cnt, j); 
            gdth_stop_timeout(ldrv.ionode, ha->bus_cnt, j); 
        } else {
            spin_lock_irqsave(&ha->smp_lock, flags);
            ha->hdr[j].lock = 0;
            spin_unlock_irqrestore(&ha->smp_lock, flags);
            gdth_start_timeout(ldrv.ionode, ha->bus_cnt, j); 
            gdth_next(ldrv.ionode); 
        }
    } 
    return 0;
}

static int ioc_resetdrv(void __user *arg, char *cmnd)
{
    gdth_ioctl_reset res;
    gdth_cmd_str cmd;
    int hanum;
    gdth_ha_str *ha;
    int rval;

    if (copy_from_user(&res, arg, sizeof(gdth_ioctl_reset)) ||
        res.ionode >= gdth_ctr_count || res.number >= MAX_HDRIVES)
        return -EFAULT;
    hanum = res.ionode;
    ha = HADATA(gdth_ctr_tab[hanum]);
 
    if (!ha->hdr[res.number].present)
        return 0;
    memset(&cmd, 0, sizeof(gdth_cmd_str));
    cmd.Service = CACHESERVICE;
    cmd.OpCode = GDT_CLUST_RESET;
    if (ha->cache_feat & GDT_64BIT)
        cmd.u.cache64.DeviceNo = res.number;
    else
        cmd.u.cache.DeviceNo = res.number;

    rval = __gdth_execute(ha->sdev, &cmd, cmnd, 30, NULL);
    if (rval < 0)
        return rval;
    res.status = rval;

    if (copy_to_user(arg, &res, sizeof(gdth_ioctl_reset)))
        return -EFAULT;
    return 0;
}

static int ioc_general(void __user *arg, char *cmnd)
{
    gdth_ioctl_general gen;
    char *buf = NULL;
    ulong64 paddr; 
    int hanum;
    gdth_ha_str *ha;
    int rval;
        
    if (copy_from_user(&gen, arg, sizeof(gdth_ioctl_general)) ||
        gen.ionode >= gdth_ctr_count)
        return -EFAULT;
    hanum = gen.ionode; 
    ha = HADATA(gdth_ctr_tab[hanum]);
    if (gen.data_len + gen.sense_len != 0) {
        if (!(buf = gdth_ioctl_alloc(hanum, gen.data_len + gen.sense_len, 
                                     FALSE, &paddr)))
            return -EFAULT;
        if (copy_from_user(buf, arg + sizeof(gdth_ioctl_general),  
                           gen.data_len + gen.sense_len)) {
            gdth_ioctl_free(hanum, gen.data_len+gen.sense_len, buf, paddr);
            return -EFAULT;
        }

        if (gen.command.OpCode == GDT_IOCTL) {
            gen.command.u.ioctl.p_param = paddr;
        } else if (gen.command.Service == CACHESERVICE) {
            if (ha->cache_feat & GDT_64BIT) {
                /* copy elements from 32-bit IOCTL structure */
                gen.command.u.cache64.BlockCnt = gen.command.u.cache.BlockCnt;
                gen.command.u.cache64.BlockNo = gen.command.u.cache.BlockNo;
                gen.command.u.cache64.DeviceNo = gen.command.u.cache.DeviceNo;
                /* addresses */
                if (ha->cache_feat & SCATTER_GATHER) {
                    gen.command.u.cache64.DestAddr = (ulong64)-1;
                    gen.command.u.cache64.sg_canz = 1;
                    gen.command.u.cache64.sg_lst[0].sg_ptr = paddr;
                    gen.command.u.cache64.sg_lst[0].sg_len = gen.data_len;
                    gen.command.u.cache64.sg_lst[1].sg_len = 0;
                } else {
                    gen.command.u.cache64.DestAddr = paddr;
                    gen.command.u.cache64.sg_canz = 0;
                }
            } else {
                if (ha->cache_feat & SCATTER_GATHER) {
                    gen.command.u.cache.DestAddr = 0xffffffff;
                    gen.command.u.cache.sg_canz = 1;
                    gen.command.u.cache.sg_lst[0].sg_ptr = (ulong32)paddr;
                    gen.command.u.cache.sg_lst[0].sg_len = gen.data_len;
                    gen.command.u.cache.sg_lst[1].sg_len = 0;
                } else {
                    gen.command.u.cache.DestAddr = paddr;
                    gen.command.u.cache.sg_canz = 0;
                }
            }
        } else if (gen.command.Service == SCSIRAWSERVICE) {
            if (ha->raw_feat & GDT_64BIT) {
                /* copy elements from 32-bit IOCTL structure */
                char cmd[16];
                gen.command.u.raw64.sense_len = gen.command.u.raw.sense_len;
                gen.command.u.raw64.bus = gen.command.u.raw.bus;
                gen.command.u.raw64.lun = gen.command.u.raw.lun;
                gen.command.u.raw64.target = gen.command.u.raw.target;
                memcpy(cmd, gen.command.u.raw.cmd, 16);
                memcpy(gen.command.u.raw64.cmd, cmd, 16);
                gen.command.u.raw64.clen = gen.command.u.raw.clen;
                gen.command.u.raw64.sdlen = gen.command.u.raw.sdlen;
                gen.command.u.raw64.direction = gen.command.u.raw.direction;
                /* addresses */
                if (ha->raw_feat & SCATTER_GATHER) {
                    gen.command.u.raw64.sdata = (ulong64)-1;
                    gen.command.u.raw64.sg_ranz = 1;
                    gen.command.u.raw64.sg_lst[0].sg_ptr = paddr;
                    gen.command.u.raw64.sg_lst[0].sg_len = gen.data_len;
                    gen.command.u.raw64.sg_lst[1].sg_len = 0;
                } else {
                    gen.command.u.raw64.sdata = paddr;
                    gen.command.u.raw64.sg_ranz = 0;
                }
                gen.command.u.raw64.sense_data = paddr + gen.data_len;
            } else {
                if (ha->raw_feat & SCATTER_GATHER) {
                    gen.command.u.raw.sdata = 0xffffffff;
                    gen.command.u.raw.sg_ranz = 1;
                    gen.command.u.raw.sg_lst[0].sg_ptr = (ulong32)paddr;
                    gen.command.u.raw.sg_lst[0].sg_len = gen.data_len;
                    gen.command.u.raw.sg_lst[1].sg_len = 0;
                } else {
                    gen.command.u.raw.sdata = paddr;
                    gen.command.u.raw.sg_ranz = 0;
                }
                gen.command.u.raw.sense_data = (ulong32)paddr + gen.data_len;
            }
        } else {
            gdth_ioctl_free(hanum, gen.data_len+gen.sense_len, buf, paddr);
            return -EFAULT;
        }
    }

    rval = __gdth_execute(ha->sdev, &gen.command, cmnd, gen.timeout, &gen.info);
    if (rval < 0)
        return rval;
    gen.status = rval;

    if (copy_to_user(arg + sizeof(gdth_ioctl_general), buf, 
                     gen.data_len + gen.sense_len)) {
        gdth_ioctl_free(hanum, gen.data_len+gen.sense_len, buf, paddr);
        return -EFAULT; 
    } 
    if (copy_to_user(arg, &gen, 
        sizeof(gdth_ioctl_general) - sizeof(gdth_cmd_str))) {
        gdth_ioctl_free(hanum, gen.data_len+gen.sense_len, buf, paddr);
        return -EFAULT;
    }
    gdth_ioctl_free(hanum, gen.data_len+gen.sense_len, buf, paddr);
    return 0;
}
 
static int ioc_hdrlist(void __user *arg, char *cmnd)
{
    gdth_ioctl_rescan *rsc;
    gdth_cmd_str *cmd;
    gdth_ha_str *ha;
    unchar i;
    int hanum, rc = -ENOMEM;
    u32 cluster_type = 0;

    rsc = kmalloc(sizeof(*rsc), GFP_KERNEL);
    cmd = kmalloc(sizeof(*cmd), GFP_KERNEL);
    if (!rsc || !cmd)
        goto free_fail;

    if (copy_from_user(rsc, arg, sizeof(gdth_ioctl_rescan)) ||
        rsc->ionode >= gdth_ctr_count) {
        rc = -EFAULT;
        goto free_fail;
    }
    hanum = rsc->ionode;
    ha = HADATA(gdth_ctr_tab[hanum]);
    memset(cmd, 0, sizeof(gdth_cmd_str));
   
    for (i = 0; i < MAX_HDRIVES; ++i) { 
        if (!ha->hdr[i].present) {
            rsc->hdr_list[i].bus = 0xff; 
            continue;
        } 
        rsc->hdr_list[i].bus = ha->virt_bus;
        rsc->hdr_list[i].target = i;
        rsc->hdr_list[i].lun = 0;
        rsc->hdr_list[i].cluster_type = ha->hdr[i].cluster_type;
        if (ha->hdr[i].cluster_type & CLUSTER_DRIVE) { 
            cmd->Service = CACHESERVICE;
            cmd->OpCode = GDT_CLUST_INFO;
            if (ha->cache_feat & GDT_64BIT)
                cmd->u.cache64.DeviceNo = i;
            else
                cmd->u.cache.DeviceNo = i;
            if (__gdth_execute(ha->sdev, cmd, cmnd, 30, &cluster_type) == S_OK)
                rsc->hdr_list[i].cluster_type = cluster_type;
        }
    } 

    if (copy_to_user(arg, rsc, sizeof(gdth_ioctl_rescan)))
        rc = -EFAULT;
    else
        rc = 0;

free_fail:
    kfree(rsc);
    kfree(cmd);
    return rc;
}

static int ioc_rescan(void __user *arg, char *cmnd)
{
    gdth_ioctl_rescan *rsc;
    gdth_cmd_str *cmd;
    ushort i, status, hdr_cnt;
    ulong32 info;
    int hanum, cyls, hds, secs;
    int rc = -ENOMEM;
    ulong flags;
    gdth_ha_str *ha; 

    rsc = kmalloc(sizeof(*rsc), GFP_KERNEL);
    cmd = kmalloc(sizeof(*cmd), GFP_KERNEL);
    if (!cmd || !rsc)
        goto free_fail;

    if (copy_from_user(rsc, arg, sizeof(gdth_ioctl_rescan)) ||
        rsc->ionode >= gdth_ctr_count) {
        rc = -EFAULT;
        goto free_fail;
    }
    hanum = rsc->ionode;
    ha = HADATA(gdth_ctr_tab[hanum]);
    memset(cmd, 0, sizeof(gdth_cmd_str));

    if (rsc->flag == 0) {
        /* old method: re-init. cache service */
        cmd->Service = CACHESERVICE;
        if (ha->cache_feat & GDT_64BIT) {
            cmd->OpCode = GDT_X_INIT_HOST;
            cmd->u.cache64.DeviceNo = LINUX_OS;
        } else {
            cmd->OpCode = GDT_INIT;
            cmd->u.cache.DeviceNo = LINUX_OS;
        }

        status = __gdth_execute(ha->sdev, cmd, cmnd, 30, &info);
        i = 0;
        hdr_cnt = (status == S_OK ? (ushort)info : 0);
    } else {
        i = rsc->hdr_no;
        hdr_cnt = i + 1;
    }

    for (; i < hdr_cnt && i < MAX_HDRIVES; ++i) {
        cmd->Service = CACHESERVICE;
        cmd->OpCode = GDT_INFO;
        if (ha->cache_feat & GDT_64BIT) 
            cmd->u.cache64.DeviceNo = i;
        else 
            cmd->u.cache.DeviceNo = i;

        status = __gdth_execute(ha->sdev, cmd, cmnd, 30, &info);

        spin_lock_irqsave(&ha->smp_lock, flags);
        rsc->hdr_list[i].bus = ha->virt_bus;
        rsc->hdr_list[i].target = i;
        rsc->hdr_list[i].lun = 0;
        if (status != S_OK) {
            ha->hdr[i].present = FALSE;
        } else {
            ha->hdr[i].present = TRUE;
            ha->hdr[i].size = info;
            /* evaluate mapping */
            ha->hdr[i].size &= ~SECS32;
            gdth_eval_mapping(ha->hdr[i].size,&cyls,&hds,&secs); 
            ha->hdr[i].heads = hds;
            ha->hdr[i].secs = secs;
            /* round size */
            ha->hdr[i].size = cyls * hds * secs;
        }
        spin_unlock_irqrestore(&ha->smp_lock, flags);
        if (status != S_OK)
            continue; 
        
        /* extended info, if GDT_64BIT, for drives > 2 TB */
        /* but we need ha->info2, not yet stored in scp->SCp */

        /* devtype, cluster info, R/W attribs */
        cmd->Service = CACHESERVICE;
        cmd->OpCode = GDT_DEVTYPE;
        if (ha->cache_feat & GDT_64BIT) 
            cmd->u.cache64.DeviceNo = i;
        else
            cmd->u.cache.DeviceNo = i;

        status = __gdth_execute(ha->sdev, cmd, cmnd, 30, &info);

        spin_lock_irqsave(&ha->smp_lock, flags);
        ha->hdr[i].devtype = (status == S_OK ? (ushort)info : 0);
        spin_unlock_irqrestore(&ha->smp_lock, flags);

        cmd->Service = CACHESERVICE;
        cmd->OpCode = GDT_CLUST_INFO;
        if (ha->cache_feat & GDT_64BIT) 
            cmd->u.cache64.DeviceNo = i;
        else
            cmd->u.cache.DeviceNo = i;

        status = __gdth_execute(ha->sdev, cmd, cmnd, 30, &info);

        spin_lock_irqsave(&ha->smp_lock, flags);
        ha->hdr[i].cluster_type = 
            ((status == S_OK && !shared_access) ? (ushort)info : 0);
        spin_unlock_irqrestore(&ha->smp_lock, flags);
        rsc->hdr_list[i].cluster_type = ha->hdr[i].cluster_type;

        cmd->Service = CACHESERVICE;
        cmd->OpCode = GDT_RW_ATTRIBS;
        if (ha->cache_feat & GDT_64BIT) 
            cmd->u.cache64.DeviceNo = i;
        else
            cmd->u.cache.DeviceNo = i;

        status = __gdth_execute(ha->sdev, cmd, cmnd, 30, &info);

        spin_lock_irqsave(&ha->smp_lock, flags);
        ha->hdr[i].rw_attribs = (status == S_OK ? (ushort)info : 0);
        spin_unlock_irqrestore(&ha->smp_lock, flags);
    }
 
    if (copy_to_user(arg, rsc, sizeof(gdth_ioctl_rescan)))
        rc = -EFAULT;
    else
        rc = 0;

free_fail:
    kfree(rsc);
    kfree(cmd);
    return rc;
}
  
static int gdth_ioctl(struct inode *inode, struct file *filep,
                      unsigned int cmd, unsigned long arg)
{
    gdth_ha_str *ha; 
    Scsi_Cmnd *scp;
    ulong flags;
    char cmnd[MAX_COMMAND_SIZE];   
    void __user *argp = (void __user *)arg;

    memset(cmnd, 0xff, 12);
    
    TRACE(("gdth_ioctl() cmd 0x%x\n", cmd));
 
    switch (cmd) {
      case GDTIOCTL_CTRCNT:
      { 
        int cnt = gdth_ctr_count;
        if (put_user(cnt, (int __user *)argp))
                return -EFAULT;
        break;
      }

      case GDTIOCTL_DRVERS:
      { 
        int ver = (GDTH_VERSION<<8) | GDTH_SUBVERSION;
        if (put_user(ver, (int __user *)argp))
                return -EFAULT;
        break;
      }
      
      case GDTIOCTL_OSVERS:
      { 
        gdth_ioctl_osvers osv; 

        osv.version = (unchar)(LINUX_VERSION_CODE >> 16);
        osv.subversion = (unchar)(LINUX_VERSION_CODE >> 8);
        osv.revision = (ushort)(LINUX_VERSION_CODE & 0xff);
        if (copy_to_user(argp, &osv, sizeof(gdth_ioctl_osvers)))
                return -EFAULT;
        break;
      }

      case GDTIOCTL_CTRTYPE:
      { 
        gdth_ioctl_ctrtype ctrt;
        
        if (copy_from_user(&ctrt, argp, sizeof(gdth_ioctl_ctrtype)) ||
            ctrt.ionode >= gdth_ctr_count)
            return -EFAULT;
        ha = HADATA(gdth_ctr_tab[ctrt.ionode]);
        if (ha->type == GDT_ISA || ha->type == GDT_EISA) {
            ctrt.type = (unchar)((ha->stype>>20) - 0x10);
        } else {
            if (ha->type != GDT_PCIMPR) {
                ctrt.type = (unchar)((ha->stype<<4) + 6);
            } else {
                ctrt.type = 
                    (ha->oem_id == OEM_ID_INTEL ? 0xfd : 0xfe);
                if (ha->stype >= 0x300)
                    ctrt.ext_type = 0x6000 | ha->subdevice_id;
                else 
                    ctrt.ext_type = 0x6000 | ha->stype;
            }
            ctrt.device_id = ha->stype;
            ctrt.sub_device_id = ha->subdevice_id;
        }
        ctrt.info = ha->brd_phys;
        ctrt.oem_id = ha->oem_id;
        if (copy_to_user(argp, &ctrt, sizeof(gdth_ioctl_ctrtype)))
            return -EFAULT;
        break;
      }
        
      case GDTIOCTL_GENERAL:
        return ioc_general(argp, cmnd);

      case GDTIOCTL_EVENT:
        return ioc_event(argp);

      case GDTIOCTL_LOCKDRV:
        return ioc_lockdrv(argp);

      case GDTIOCTL_LOCKCHN:
      {
        gdth_ioctl_lockchn lchn;
        unchar i, j;

        if (copy_from_user(&lchn, argp, sizeof(gdth_ioctl_lockchn)) ||
            lchn.ionode >= gdth_ctr_count)
            return -EFAULT;
        ha = HADATA(gdth_ctr_tab[lchn.ionode]);
        
        i = lchn.channel;
        if (i < ha->bus_cnt) {
            if (lchn.lock) {
                spin_lock_irqsave(&ha->smp_lock, flags);
                ha->raw[i].lock = 1;
                spin_unlock_irqrestore(&ha->smp_lock, flags);
                for (j = 0; j < ha->tid_cnt; ++j) {
                    gdth_wait_completion(lchn.ionode, i, j); 
                    gdth_stop_timeout(lchn.ionode, i, j); 
                }
            } else {
                spin_lock_irqsave(&ha->smp_lock, flags);
                ha->raw[i].lock = 0;
                spin_unlock_irqrestore(&ha->smp_lock, flags);
                for (j = 0; j < ha->tid_cnt; ++j) {
                    gdth_start_timeout(lchn.ionode, i, j); 
                    gdth_next(lchn.ionode); 
                }
            }
        } 
        break;
      }

      case GDTIOCTL_RESCAN:
        return ioc_rescan(argp, cmnd);

      case GDTIOCTL_HDRLIST:
        return ioc_hdrlist(argp, cmnd);

      case GDTIOCTL_RESET_BUS:
      {
        gdth_ioctl_reset res;
        int hanum, rval;

        if (copy_from_user(&res, argp, sizeof(gdth_ioctl_reset)) ||
            res.ionode >= gdth_ctr_count)
            return -EFAULT;
        hanum = res.ionode; 
        ha = HADATA(gdth_ctr_tab[hanum]);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        scp  = kmalloc(sizeof(*scp), GFP_KERNEL);
        if (!scp)
            return -ENOMEM;
        memset(scp, 0, sizeof(*scp));
        scp->device = ha->sdev;
        scp->cmd_len = 12;
        scp->use_sg = 0;
        scp->device->channel = virt_ctr ? 0 : res.number;
        rval = gdth_eh_bus_reset(scp);
        res.status = (rval == SUCCESS ? S_OK : S_GENERR);
        kfree(scp);
#else
        scp  = scsi_allocate_device(ha->sdev, 1, FALSE);
        if (!scp)
            return -ENOMEM;
        scp->cmd_len = 12;
        scp->use_sg = 0;
        scp->channel = virt_ctr ? 0 : res.number;
        rval = gdth_eh_bus_reset(scp);
        res.status = (rval == SUCCESS ? S_OK : S_GENERR);
        scsi_release_command(scp);
#endif
        if (copy_to_user(argp, &res, sizeof(gdth_ioctl_reset)))
            return -EFAULT;
        break;
      }

      case GDTIOCTL_RESET_DRV:
        return ioc_resetdrv(argp, cmnd);

      default:
        break; 
    }
    return 0;
}


/* flush routine */
static void gdth_flush(int hanum)
{
    int             i;
    gdth_ha_str     *ha;
    gdth_cmd_str    gdtcmd;
    char            cmnd[MAX_COMMAND_SIZE];   
    memset(cmnd, 0xff, MAX_COMMAND_SIZE);

    TRACE2(("gdth_flush() hanum %d\n",hanum));
    ha = HADATA(gdth_ctr_tab[hanum]);

    for (i = 0; i < MAX_HDRIVES; ++i) {
        if (ha->hdr[i].present) {
            gdtcmd.BoardNode = LOCALBOARD;
            gdtcmd.Service = CACHESERVICE;
            gdtcmd.OpCode = GDT_FLUSH;
            if (ha->cache_feat & GDT_64BIT) { 
                gdtcmd.u.cache64.DeviceNo = i;
                gdtcmd.u.cache64.BlockNo = 1;
                gdtcmd.u.cache64.sg_canz = 0;
            } else {
                gdtcmd.u.cache.DeviceNo = i;
                gdtcmd.u.cache.BlockNo = 1;
                gdtcmd.u.cache.sg_canz = 0;
            }
            TRACE2(("gdth_flush(): flush ha %d drive %d\n", hanum, i));

            gdth_execute(gdth_ctr_tab[hanum], &gdtcmd, cmnd, 30, NULL);
        }
    }
}

/* shutdown routine */
static int gdth_halt(struct notifier_block *nb, ulong event, void *buf)
{
    int             hanum;
#ifndef __alpha__
    gdth_cmd_str    gdtcmd;
    char            cmnd[MAX_COMMAND_SIZE];   
#endif

    if (notifier_disabled)
        return NOTIFY_OK;

    TRACE2(("gdth_halt() event %d\n",(int)event));
    if (event != SYS_RESTART && event != SYS_HALT && event != SYS_POWER_OFF)
        return NOTIFY_DONE;

    notifier_disabled = 1;
    printk("GDT-HA: Flushing all host drives .. ");
    for (hanum = 0; hanum < gdth_ctr_count; ++hanum) {
        gdth_flush(hanum);

#ifndef __alpha__
        /* controller reset */
        memset(cmnd, 0xff, MAX_COMMAND_SIZE);
        gdtcmd.BoardNode = LOCALBOARD;
        gdtcmd.Service = CACHESERVICE;
        gdtcmd.OpCode = GDT_RESET;
        TRACE2(("gdth_halt(): reset controller %d\n", hanum));
        gdth_execute(gdth_ctr_tab[hanum], &gdtcmd, cmnd, 10, NULL);
#endif
    }
    printk("Done.\n");

#ifdef GDTH_STATISTICS
    del_timer(&gdth_timer);
#endif
    return NOTIFY_OK;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
/* configure lun */
static int gdth_slave_configure(struct scsi_device *sdev)
{
    scsi_adjust_queue_depth(sdev, 0, sdev->host->cmd_per_lun);
    sdev->skip_ms_page_3f = 1;
    sdev->skip_ms_page_8 = 1;
    return 0;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static struct scsi_host_template driver_template = {
#else
static Scsi_Host_Template driver_template = {
#endif
        .proc_name              = "gdth", 
        .proc_info              = gdth_proc_info,
        .name                   = "GDT SCSI Disk Array Controller",
        .detect                 = gdth_detect, 
        .release                = gdth_release,
        .info                   = gdth_info, 
        .queuecommand           = gdth_queuecommand,
        .eh_bus_reset_handler   = gdth_eh_bus_reset,
        .bios_param             = gdth_bios_param,
        .can_queue              = GDTH_MAXCMDS,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        .slave_configure        = gdth_slave_configure,
#endif
        .this_id                = -1,
        .sg_tablesize           = GDTH_MAXSG,
        .cmd_per_lun            = GDTH_MAXC_P_L,
        .unchecked_isa_dma      = 1,
        .use_clustering         = ENABLE_CLUSTERING,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
        .use_new_eh_code        = 1,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,20)
        .highmem_io             = 1,
#endif
#endif
};

#include "scsi_module.c"
#ifndef MODULE
__setup("gdth=", option_setup);
#endif
