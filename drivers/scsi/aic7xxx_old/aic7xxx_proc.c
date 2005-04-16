/*+M*************************************************************************
 * Adaptec AIC7xxx device driver proc support for Linux.
 *
 * Copyright (c) 1995, 1996 Dean W. Gehnert
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * ----------------------------------------------------------------
 *  o Modified from the EATA-DMA /proc support.
 *  o Additional support for device block statistics provided by
 *    Matthew Jacob.
 *  o Correction of overflow by Heinz Mauelshagen
 *  o Adittional corrections by Doug Ledford
 *
 *  Dean W. Gehnert, deang@teleport.com, 05/01/96
 *
 *  $Id: aic7xxx_proc.c,v 4.1 1997/06/97 08:23:42 deang Exp $
 *-M*************************************************************************/

#include <linux/config.h>

#define	BLS	(&aic7xxx_buffer[size])
#define HDRB \
"               0 - 4K   4 - 16K   16 - 64K  64 - 256K  256K - 1M        1M+"

#ifdef PROC_DEBUG
extern int vsprintf(char *, const char *, va_list);

static void
proc_debug(const char *fmt, ...)
{
  va_list ap;
  char buf[256];

  va_start(ap, fmt);
  vsprintf(buf, fmt, ap);
  printk(buf);
  va_end(ap);
}
#else /* PROC_DEBUG */
#  define proc_debug(fmt, args...)
#endif /* PROC_DEBUG */

static int aic7xxx_buffer_size = 0;
static char *aic7xxx_buffer = NULL;


/*+F*************************************************************************
 * Function:
 *   aic7xxx_set_info
 *
 * Description:
 *   Set parameters for the driver from the /proc filesystem.
 *-F*************************************************************************/
static int
aic7xxx_set_info(char *buffer, int length, struct Scsi_Host *HBAptr)
{
  proc_debug("aic7xxx_set_info(): %s\n", buffer);
  return (-ENOSYS);  /* Currently this is a no-op */
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_proc_info
 *
 * Description:
 *   Return information to handle /proc support for the driver.
 *-F*************************************************************************/
int
aic7xxx_proc_info ( struct Scsi_Host *HBAptr, char *buffer, char **start, off_t offset, int length, 
                    int inout)
{
  struct aic7xxx_host *p;
  struct aic_dev_data *aic_dev;
  struct scsi_device *sdptr;
  int    size = 0;
  unsigned char i;
  unsigned char tindex;

  for(p=first_aic7xxx; p && p->host != HBAptr; p=p->next)
    ;

  if (!p)
  {
    size += sprintf(buffer, "Can't find adapter for host number %d\n", HBAptr->host_no);
    if (size > length)
    {
      return (size);
    }
    else
    {
      return (length);
    }
  }

  if (inout == TRUE) /* Has data been written to the file? */ 
  {
    return (aic7xxx_set_info(buffer, length, HBAptr));
  }

  p = (struct aic7xxx_host *) HBAptr->hostdata;

  /*
   * It takes roughly 1K of space to hold all relevant card info, not
   * counting any proc stats, so we start out with a 1.5k buffer size and
   * if proc_stats is defined, then we sweep the stats structure to see
   * how many drives we will be printing out for and add 384 bytes per
   * device with active stats.
   *
   * Hmmmm...that 1.5k seems to keep growing as items get added so they
   * can be easily viewed for debugging purposes.  So, we bumped that
   * 1.5k to 4k so we can quit having to bump it all the time.
   */

  size = 4096;
  list_for_each_entry(aic_dev, &p->aic_devs, list)
    size += 512;
  if (aic7xxx_buffer_size != size)
  {
    if (aic7xxx_buffer != NULL) 
    {
      kfree(aic7xxx_buffer);
      aic7xxx_buffer_size = 0;
    }
    aic7xxx_buffer = kmalloc(size, GFP_KERNEL);
  }
  if (aic7xxx_buffer == NULL)
  {
    size = sprintf(buffer, "AIC7xxx - kmalloc error at line %d\n",
        __LINE__);
    return size;
  }
  aic7xxx_buffer_size = size;

  size = 0;
  size += sprintf(BLS, "Adaptec AIC7xxx driver version: ");
  size += sprintf(BLS, "%s/", AIC7XXX_C_VERSION);
  size += sprintf(BLS, "%s", AIC7XXX_H_VERSION);
  size += sprintf(BLS, "\n");
  size += sprintf(BLS, "Adapter Configuration:\n");
  size += sprintf(BLS, "           SCSI Adapter: %s\n",
      board_names[p->board_name_index]);
  if (p->flags & AHC_TWIN)
    size += sprintf(BLS, "                         Twin Channel Controller ");
  else
  {
    char *channel = "";
    char *ultra = "";
    char *wide = "Narrow ";
    if (p->flags & AHC_MULTI_CHANNEL)
    {
      channel = " Channel A";
      if (p->flags & (AHC_CHNLB|AHC_CHNLC))
        channel = (p->flags & AHC_CHNLB) ? " Channel B" : " Channel C";
    }
    if (p->features & AHC_WIDE)
      wide = "Wide ";
    if (p->features & AHC_ULTRA3)
    {
      switch(p->chip & AHC_CHIPID_MASK)
      {
        case AHC_AIC7892:
        case AHC_AIC7899:
          ultra = "Ultra-160/m LVD/SE ";
          break;
        default:
          ultra = "Ultra-3 LVD/SE ";
          break;
      }
    }
    else if (p->features & AHC_ULTRA2)
      ultra = "Ultra-2 LVD/SE ";
    else if (p->features & AHC_ULTRA)
      ultra = "Ultra ";
    size += sprintf(BLS, "                           %s%sController%s ",
      ultra, wide, channel);
  }
  switch(p->chip & ~AHC_CHIPID_MASK)
  {
    case AHC_VL:
      size += sprintf(BLS, "at VLB slot %d\n", p->pci_device_fn);
      break;
    case AHC_EISA:
      size += sprintf(BLS, "at EISA slot %d\n", p->pci_device_fn);
      break;
    default:
      size += sprintf(BLS, "at PCI %d/%d/%d\n", p->pci_bus,
        PCI_SLOT(p->pci_device_fn), PCI_FUNC(p->pci_device_fn));
      break;
  }
  if( !(p->maddr) )
  {
    size += sprintf(BLS, "    Programmed I/O Base: %lx\n", p->base);
  }
  else
  {
    size += sprintf(BLS, "    PCI MMAPed I/O Base: 0x%lx\n", p->mbase);
  }
  if( (p->chip & (AHC_VL | AHC_EISA)) )
  {
    size += sprintf(BLS, "    BIOS Memory Address: 0x%08x\n", p->bios_address);
  }
  size += sprintf(BLS, " Adapter SEEPROM Config: %s\n",
          (p->flags & AHC_SEEPROM_FOUND) ? "SEEPROM found and used." :
         ((p->flags & AHC_USEDEFAULTS) ? "SEEPROM not found, using defaults." :
           "SEEPROM not found, using leftover BIOS values.") );
  size += sprintf(BLS, "      Adaptec SCSI BIOS: %s\n",
          (p->flags & AHC_BIOS_ENABLED) ? "Enabled" : "Disabled");
  size += sprintf(BLS, "                    IRQ: %d\n", HBAptr->irq);
  size += sprintf(BLS, "                   SCBs: Active %d, Max Active %d,\n",
            p->activescbs, p->max_activescbs);
  size += sprintf(BLS, "                         Allocated %d, HW %d, "
            "Page %d\n", p->scb_data->numscbs, p->scb_data->maxhscbs,
            p->scb_data->maxscbs);
  if (p->flags & AHC_EXTERNAL_SRAM)
    size += sprintf(BLS, "                         Using External SCB SRAM\n");
  size += sprintf(BLS, "             Interrupts: %ld", p->isr_count);
  if (p->chip & AHC_EISA)
  {
    size += sprintf(BLS, " %s\n",
        (p->pause & IRQMS) ? "(Level Sensitive)" : "(Edge Triggered)");
  }
  else
  {
    size += sprintf(BLS, "\n");
  }
  size += sprintf(BLS, "      BIOS Control Word: 0x%04x\n",
            p->bios_control);
  size += sprintf(BLS, "   Adapter Control Word: 0x%04x\n",
            p->adapter_control);
  size += sprintf(BLS, "   Extended Translation: %sabled\n",
      (p->flags & AHC_EXTEND_TRANS_A) ? "En" : "Dis");
  size += sprintf(BLS, "Disconnect Enable Flags: 0x%04x\n", p->discenable);
  if (p->features & (AHC_ULTRA | AHC_ULTRA2))
  {
    size += sprintf(BLS, "     Ultra Enable Flags: 0x%04x\n", p->ultraenb);
  }
  size += sprintf(BLS, "Default Tag Queue Depth: %d\n", aic7xxx_default_queue_depth);
  size += sprintf(BLS, "    Tagged Queue By Device array for aic7xxx host "
                       "instance %d:\n", p->instance);
  size += sprintf(BLS, "      {");
  for(i=0; i < (MAX_TARGETS - 1); i++)
    size += sprintf(BLS, "%d,",aic7xxx_tag_info[p->instance].tag_commands[i]);
  size += sprintf(BLS, "%d}\n",aic7xxx_tag_info[p->instance].tag_commands[i]);

  size += sprintf(BLS, "\n");
  size += sprintf(BLS, "Statistics:\n\n");
  list_for_each_entry(aic_dev, &p->aic_devs, list)
  {
    sdptr = aic_dev->SDptr;
    tindex = sdptr->channel << 3 | sdptr->id;
    size += sprintf(BLS, "(scsi%d:%d:%d:%d)\n",
        p->host_no, sdptr->channel, sdptr->id, sdptr->lun);
    size += sprintf(BLS, "  Device using %s/%s",
          (aic_dev->cur.width == MSG_EXT_WDTR_BUS_16_BIT) ?
          "Wide" : "Narrow",
          (aic_dev->cur.offset != 0) ?
          "Sync transfers at " : "Async transfers.\n" );
    if (aic_dev->cur.offset != 0)
    {
      struct aic7xxx_syncrate *sync_rate;
      unsigned char options = aic_dev->cur.options;
      int period = aic_dev->cur.period;
      int rate = (aic_dev->cur.width ==
                  MSG_EXT_WDTR_BUS_16_BIT) ? 1 : 0;

      sync_rate = aic7xxx_find_syncrate(p, &period, 0, &options);
      if (sync_rate != NULL)
      {
        size += sprintf(BLS, "%s MByte/sec, offset %d\n",
                        sync_rate->rate[rate],
                        aic_dev->cur.offset );
      }
      else
      {
        size += sprintf(BLS, "3.3 MByte/sec, offset %d\n",
                        aic_dev->cur.offset );
      }
    }
    size += sprintf(BLS, "  Transinfo settings: ");
    size += sprintf(BLS, "current(%d/%d/%d/%d), ",
                    aic_dev->cur.period,
                    aic_dev->cur.offset,
                    aic_dev->cur.width,
                    aic_dev->cur.options);
    size += sprintf(BLS, "goal(%d/%d/%d/%d), ",
                    aic_dev->goal.period,
                    aic_dev->goal.offset,
                    aic_dev->goal.width,
                    aic_dev->goal.options);
    size += sprintf(BLS, "user(%d/%d/%d/%d)\n",
                    p->user[tindex].period,
                    p->user[tindex].offset,
                    p->user[tindex].width,
                    p->user[tindex].options);
    if(sdptr->simple_tags)
    {
      size += sprintf(BLS, "  Tagged Command Queueing Enabled, Ordered Tags %s, Depth %d/%d\n", sdptr->ordered_tags ? "Enabled" : "Disabled", sdptr->queue_depth, aic_dev->max_q_depth);
    }
    if(aic_dev->barrier_total)
      size += sprintf(BLS, "  Total transfers %ld:\n    (%ld/%ld/%ld/%ld reads/writes/REQ_BARRIER/Ordered Tags)\n",
        aic_dev->r_total+aic_dev->w_total, aic_dev->r_total, aic_dev->w_total,
        aic_dev->barrier_total, aic_dev->ordered_total);
    else
      size += sprintf(BLS, "  Total transfers %ld:\n    (%ld/%ld reads/writes)\n",
        aic_dev->r_total+aic_dev->w_total, aic_dev->r_total, aic_dev->w_total);
    size += sprintf(BLS, "%s\n", HDRB);
    size += sprintf(BLS, "   Reads:");
    for (i = 0; i < ARRAY_SIZE(aic_dev->r_bins); i++)
    {
      size += sprintf(BLS, " %10ld", aic_dev->r_bins[i]);
    }
    size += sprintf(BLS, "\n");
    size += sprintf(BLS, "  Writes:");
    for (i = 0; i < ARRAY_SIZE(aic_dev->w_bins); i++)
    {
      size += sprintf(BLS, " %10ld", aic_dev->w_bins[i]);
    }
    size += sprintf(BLS, "\n");
    size += sprintf(BLS, "\n\n");
  }
  if (size >= aic7xxx_buffer_size)
  {
    printk(KERN_WARNING "aic7xxx: Overflow in aic7xxx_proc.c\n");
  }

  if (offset > size - 1)
  {
    kfree(aic7xxx_buffer);
    aic7xxx_buffer = NULL;
    aic7xxx_buffer_size = length = 0;
    *start = NULL;
  }
  else
  {
    *start = buffer;
    length = min_t(int, length, size - offset);
    memcpy(buffer, &aic7xxx_buffer[offset], length);
  }

  return (length);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
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
