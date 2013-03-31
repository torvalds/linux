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


#define HDRB \
"               0 - 4K   4 - 16K   16 - 64K  64 - 256K  256K - 1M        1M+"


/*+F*************************************************************************
 * Function:
 *   aic7xxx_show_info
 *
 * Description:
 *   Return information to handle /proc support for the driver.
 *-F*************************************************************************/
int
aic7xxx_show_info(struct seq_file *m, struct Scsi_Host *HBAptr)
{
  struct aic7xxx_host *p;
  struct aic_dev_data *aic_dev;
  struct scsi_device *sdptr;
  unsigned char i;
  unsigned char tindex;

  for(p=first_aic7xxx; p && p->host != HBAptr; p=p->next)
    ;

  if (!p)
  {
    seq_printf(m, "Can't find adapter for host number %d\n", HBAptr->host_no);
    return 0;
  }

  p = (struct aic7xxx_host *) HBAptr->hostdata;

  seq_printf(m, "Adaptec AIC7xxx driver version: ");
  seq_printf(m, "%s/", AIC7XXX_C_VERSION);
  seq_printf(m, "%s", AIC7XXX_H_VERSION);
  seq_printf(m, "\n");
  seq_printf(m, "Adapter Configuration:\n");
  seq_printf(m, "           SCSI Adapter: %s\n",
      board_names[p->board_name_index]);
  if (p->flags & AHC_TWIN)
    seq_printf(m, "                         Twin Channel Controller ");
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
    seq_printf(m, "                           %s%sController%s ",
      ultra, wide, channel);
  }
  switch(p->chip & ~AHC_CHIPID_MASK)
  {
    case AHC_VL:
      seq_printf(m, "at VLB slot %d\n", p->pci_device_fn);
      break;
    case AHC_EISA:
      seq_printf(m, "at EISA slot %d\n", p->pci_device_fn);
      break;
    default:
      seq_printf(m, "at PCI %d/%d/%d\n", p->pci_bus,
        PCI_SLOT(p->pci_device_fn), PCI_FUNC(p->pci_device_fn));
      break;
  }
  if( !(p->maddr) )
  {
    seq_printf(m, "    Programmed I/O Base: %lx\n", p->base);
  }
  else
  {
    seq_printf(m, "    PCI MMAPed I/O Base: 0x%lx\n", p->mbase);
  }
  if( (p->chip & (AHC_VL | AHC_EISA)) )
  {
    seq_printf(m, "    BIOS Memory Address: 0x%08x\n", p->bios_address);
  }
  seq_printf(m, " Adapter SEEPROM Config: %s\n",
          (p->flags & AHC_SEEPROM_FOUND) ? "SEEPROM found and used." :
         ((p->flags & AHC_USEDEFAULTS) ? "SEEPROM not found, using defaults." :
           "SEEPROM not found, using leftover BIOS values.") );
  seq_printf(m, "      Adaptec SCSI BIOS: %s\n",
          (p->flags & AHC_BIOS_ENABLED) ? "Enabled" : "Disabled");
  seq_printf(m, "                    IRQ: %d\n", HBAptr->irq);
  seq_printf(m, "                   SCBs: Active %d, Max Active %d,\n",
            p->activescbs, p->max_activescbs);
  seq_printf(m, "                         Allocated %d, HW %d, "
            "Page %d\n", p->scb_data->numscbs, p->scb_data->maxhscbs,
            p->scb_data->maxscbs);
  if (p->flags & AHC_EXTERNAL_SRAM)
    seq_printf(m, "                         Using External SCB SRAM\n");
  seq_printf(m, "             Interrupts: %ld", p->isr_count);
  if (p->chip & AHC_EISA)
  {
    seq_printf(m, " %s\n",
        (p->pause & IRQMS) ? "(Level Sensitive)" : "(Edge Triggered)");
  }
  else
  {
    seq_printf(m, "\n");
  }
  seq_printf(m, "      BIOS Control Word: 0x%04x\n",
            p->bios_control);
  seq_printf(m, "   Adapter Control Word: 0x%04x\n",
            p->adapter_control);
  seq_printf(m, "   Extended Translation: %sabled\n",
      (p->flags & AHC_EXTEND_TRANS_A) ? "En" : "Dis");
  seq_printf(m, "Disconnect Enable Flags: 0x%04x\n", p->discenable);
  if (p->features & (AHC_ULTRA | AHC_ULTRA2))
  {
    seq_printf(m, "     Ultra Enable Flags: 0x%04x\n", p->ultraenb);
  }
  seq_printf(m, "Default Tag Queue Depth: %d\n", aic7xxx_default_queue_depth);
  seq_printf(m, "    Tagged Queue By Device array for aic7xxx host "
                       "instance %d:\n", p->instance);
  seq_printf(m, "      {");
  for(i=0; i < (MAX_TARGETS - 1); i++)
    seq_printf(m, "%d,",aic7xxx_tag_info[p->instance].tag_commands[i]);
  seq_printf(m, "%d}\n",aic7xxx_tag_info[p->instance].tag_commands[i]);

  seq_printf(m, "\n");
  seq_printf(m, "Statistics:\n\n");
  list_for_each_entry(aic_dev, &p->aic_devs, list)
  {
    sdptr = aic_dev->SDptr;
    tindex = sdptr->channel << 3 | sdptr->id;
    seq_printf(m, "(scsi%d:%d:%d:%d)\n",
        p->host_no, sdptr->channel, sdptr->id, sdptr->lun);
    seq_printf(m, "  Device using %s/%s",
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
        seq_printf(m, "%s MByte/sec, offset %d\n",
                        sync_rate->rate[rate],
                        aic_dev->cur.offset );
      }
      else
      {
        seq_printf(m, "3.3 MByte/sec, offset %d\n",
                        aic_dev->cur.offset );
      }
    }
    seq_printf(m, "  Transinfo settings: ");
    seq_printf(m, "current(%d/%d/%d/%d), ",
                    aic_dev->cur.period,
                    aic_dev->cur.offset,
                    aic_dev->cur.width,
                    aic_dev->cur.options);
    seq_printf(m, "goal(%d/%d/%d/%d), ",
                    aic_dev->goal.period,
                    aic_dev->goal.offset,
                    aic_dev->goal.width,
                    aic_dev->goal.options);
    seq_printf(m, "user(%d/%d/%d/%d)\n",
                    p->user[tindex].period,
                    p->user[tindex].offset,
                    p->user[tindex].width,
                    p->user[tindex].options);
    if(sdptr->simple_tags)
    {
      seq_printf(m, "  Tagged Command Queueing Enabled, Ordered Tags %s, Depth %d/%d\n", sdptr->ordered_tags ? "Enabled" : "Disabled", sdptr->queue_depth, aic_dev->max_q_depth);
    }
    if(aic_dev->barrier_total)
      seq_printf(m, "  Total transfers %ld:\n    (%ld/%ld/%ld/%ld reads/writes/REQ_BARRIER/Ordered Tags)\n",
        aic_dev->r_total+aic_dev->w_total, aic_dev->r_total, aic_dev->w_total,
        aic_dev->barrier_total, aic_dev->ordered_total);
    else
      seq_printf(m, "  Total transfers %ld:\n    (%ld/%ld reads/writes)\n",
        aic_dev->r_total+aic_dev->w_total, aic_dev->r_total, aic_dev->w_total);
    seq_printf(m, "%s\n", HDRB);
    seq_printf(m, "   Reads:");
    for (i = 0; i < ARRAY_SIZE(aic_dev->r_bins); i++)
    {
      seq_printf(m, " %10ld", aic_dev->r_bins[i]);
    }
    seq_printf(m, "\n");
    seq_printf(m, "  Writes:");
    for (i = 0; i < ARRAY_SIZE(aic_dev->w_bins); i++)
    {
      seq_printf(m, " %10ld", aic_dev->w_bins[i]);
    }
    seq_printf(m, "\n");
    seq_printf(m, "\n\n");
  }
  return 0;
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
