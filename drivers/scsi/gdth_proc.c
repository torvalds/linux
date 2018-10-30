// SPDX-License-Identifier: GPL-2.0
/* gdth_proc.c 
 * $Id: gdth_proc.c,v 1.43 2006/01/11 16:15:00 achim Exp $
 */

#include <linux/completion.h>
#include <linux/slab.h>

int gdth_set_info(struct Scsi_Host *host, char *buffer, int length)
{
    gdth_ha_str *ha = shost_priv(host);
    int ret_val = -EINVAL;

    TRACE2(("gdth_set_info() ha %d\n",ha->hanum,));

    if (length >= 4) {
        if (strncmp(buffer,"gdth",4) == 0) {
            buffer += 5;
            length -= 5;
            ret_val = gdth_set_asc_info(host, buffer, length, ha);
        }
    }

    return ret_val;
}
         
static int gdth_set_asc_info(struct Scsi_Host *host, char *buffer,
                        int length, gdth_ha_str *ha)
{
    int orig_length, drive, wb_mode;
    int i, found;
    gdth_cmd_str    gdtcmd;
    gdth_cpar_str   *pcpar;
    u64         paddr;

    char            cmnd[MAX_COMMAND_SIZE];
    memset(cmnd, 0xff, 12);
    memset(&gdtcmd, 0, sizeof(gdth_cmd_str));

    TRACE2(("gdth_set_asc_info() ha %d\n",ha->hanum));
    orig_length = length + 5;
    drive = -1;
    wb_mode = 0;
    found = FALSE;

    if (length >= 5 && strncmp(buffer,"flush",5)==0) {
        buffer += 6;
        length -= 6;
        if (length && *buffer>='0' && *buffer<='9') {
            drive = (int)(*buffer-'0');
            ++buffer; --length;
            if (length && *buffer>='0' && *buffer<='9') {
                drive = drive*10 + (int)(*buffer-'0');
                ++buffer; --length;
            }
            printk("GDT: Flushing host drive %d .. ",drive);
        } else {
            printk("GDT: Flushing all host drives .. ");
        }
        for (i = 0; i < MAX_HDRIVES; ++i) {
            if (ha->hdr[i].present) {
                if (drive != -1 && i != drive)
                    continue;
                found = TRUE;
                gdtcmd.Service = CACHESERVICE;
                gdtcmd.OpCode = GDT_FLUSH;
                if (ha->cache_feat & GDT_64BIT) {
                    gdtcmd.u.cache64.DeviceNo = i;
                    gdtcmd.u.cache64.BlockNo = 1;
                } else {
                    gdtcmd.u.cache.DeviceNo = i;
                    gdtcmd.u.cache.BlockNo = 1;
                }

                gdth_execute(host, &gdtcmd, cmnd, 30, NULL);
            }
        }
        if (!found)
            printk("\nNo host drive found !\n");
        else
            printk("Done.\n");
        return(orig_length);
    }

    if (length >= 7 && strncmp(buffer,"wbp_off",7)==0) {
        buffer += 8;
        length -= 8;
        printk("GDT: Disabling write back permanently .. ");
        wb_mode = 1;
    } else if (length >= 6 && strncmp(buffer,"wbp_on",6)==0) {
        buffer += 7;
        length -= 7;
        printk("GDT: Enabling write back permanently .. ");
        wb_mode = 2;
    } else if (length >= 6 && strncmp(buffer,"wb_off",6)==0) {
        buffer += 7;
        length -= 7;
        printk("GDT: Disabling write back commands .. ");
        if (ha->cache_feat & GDT_WR_THROUGH) {
            gdth_write_through = TRUE;
            printk("Done.\n");
        } else {
            printk("Not supported !\n");
        }
        return(orig_length);
    } else if (length >= 5 && strncmp(buffer,"wb_on",5)==0) {
        buffer += 6;
        length -= 6;
        printk("GDT: Enabling write back commands .. ");
        gdth_write_through = FALSE;
        printk("Done.\n");
        return(orig_length);
    }

    if (wb_mode) {
        if (!gdth_ioctl_alloc(ha, sizeof(gdth_cpar_str), TRUE, &paddr))
            return(-EBUSY);
        pcpar = (gdth_cpar_str *)ha->pscratch;
        memcpy( pcpar, &ha->cpar, sizeof(gdth_cpar_str) );
        gdtcmd.Service = CACHESERVICE;
        gdtcmd.OpCode = GDT_IOCTL;
        gdtcmd.u.ioctl.p_param = paddr;
        gdtcmd.u.ioctl.param_size = sizeof(gdth_cpar_str);
        gdtcmd.u.ioctl.subfunc = CACHE_CONFIG;
        gdtcmd.u.ioctl.channel = INVALID_CHANNEL;
        pcpar->write_back = wb_mode==1 ? 0:1;

        gdth_execute(host, &gdtcmd, cmnd, 30, NULL);

        gdth_ioctl_free(ha, GDTH_SCRATCH, ha->pscratch, paddr);
        printk("Done.\n");
        return(orig_length);
    }

    printk("GDT: Unknown command: %s  Length: %d\n",buffer,length);
    return(-EINVAL);
}

int gdth_show_info(struct seq_file *m, struct Scsi_Host *host)
{
    gdth_ha_str *ha = shost_priv(host);
    int hlen;
    int id, i, j, k, sec, flag;
    int no_mdrv = 0, drv_no, is_mirr;
    u32 cnt;
    u64 paddr;
    int rc = -ENOMEM;

    gdth_cmd_str *gdtcmd;
    gdth_evt_str *estr;
    char hrec[277];

    char *buf;
    gdth_dskstat_str *pds;
    gdth_diskinfo_str *pdi;
    gdth_arrayinf_str *pai;
    gdth_defcnt_str *pdef;
    gdth_cdrinfo_str *pcdi;
    gdth_hget_str *phg;
    char cmnd[MAX_COMMAND_SIZE];

    gdtcmd = kmalloc(sizeof(*gdtcmd), GFP_KERNEL);
    estr = kmalloc(sizeof(*estr), GFP_KERNEL);
    if (!gdtcmd || !estr)
        goto free_fail;

    memset(cmnd, 0xff, 12);
    memset(gdtcmd, 0, sizeof(gdth_cmd_str));

    TRACE2(("gdth_get_info() ha %d\n",ha->hanum));

    
    /* request is i.e. "cat /proc/scsi/gdth/0" */ 
    /* format: %-15s\t%-10s\t%-15s\t%s */
    /* driver parameters */
    seq_puts(m, "Driver Parameters:\n");
    if (reserve_list[0] == 0xff)
        strcpy(hrec, "--");
    else {
        hlen = sprintf(hrec, "%d", reserve_list[0]);
        for (i = 1;  i < MAX_RES_ARGS; i++) {
            if (reserve_list[i] == 0xff) 
                break;
            hlen += snprintf(hrec + hlen , 161 - hlen, ",%d", reserve_list[i]);
        }
    }
    seq_printf(m,
                   " reserve_mode: \t%d         \treserve_list:  \t%s\n",
                   reserve_mode, hrec);
    seq_printf(m,
                   " max_ids:      \t%-3d       \thdr_channel:   \t%d\n",
                   max_ids, hdr_channel);

    /* controller information */
    seq_puts(m, "\nDisk Array Controller Information:\n");
    seq_printf(m,
                   " Number:       \t%d         \tName:          \t%s\n",
                   ha->hanum, ha->binfo.type_string);

    seq_printf(m,
                   " Driver Ver.:  \t%-10s\tFirmware Ver.: \t",
                   GDTH_VERSION_STR);
    if (ha->more_proc)
        seq_printf(m, "%d.%02d.%02d-%c%03X\n", 
                (u8)(ha->binfo.upd_fw_ver>>24),
                (u8)(ha->binfo.upd_fw_ver>>16),
                (u8)(ha->binfo.upd_fw_ver),
                ha->bfeat.raid ? 'R':'N',
                ha->binfo.upd_revision);
    else
        seq_printf(m, "%d.%02d\n", (u8)(ha->cpar.version>>8),
                (u8)(ha->cpar.version));
 
    if (ha->more_proc)
        /* more information: 1. about controller */
        seq_printf(m,
                       " Serial No.:   \t0x%8X\tCache RAM size:\t%d KB\n",
                       ha->binfo.ser_no, ha->binfo.memsize / 1024);

#ifdef GDTH_DMA_STATISTICS
    /* controller statistics */
    seq_puts(m, "\nController Statistics:\n");
    seq_printf(m,
                   " 32-bit DMA buffer:\t%lu\t64-bit DMA buffer:\t%lu\n",
                   ha->dma32_cnt, ha->dma64_cnt);
#endif

    if (ha->more_proc) {
        /* more information: 2. about physical devices */
        seq_puts(m, "\nPhysical Devices:");
        flag = FALSE;
            
        buf = gdth_ioctl_alloc(ha, GDTH_SCRATCH, FALSE, &paddr);
        if (!buf) 
            goto stop_output;
        for (i = 0; i < ha->bus_cnt; ++i) {
            /* 2.a statistics (and retries/reassigns) */
            TRACE2(("pdr_statistics() chn %d\n",i));                
            pds = (gdth_dskstat_str *)(buf + GDTH_SCRATCH/4);
            gdtcmd->Service = CACHESERVICE;
            gdtcmd->OpCode = GDT_IOCTL;
            gdtcmd->u.ioctl.p_param = paddr + GDTH_SCRATCH/4;
            gdtcmd->u.ioctl.param_size = 3*GDTH_SCRATCH/4;
            gdtcmd->u.ioctl.subfunc = DSK_STATISTICS | L_CTRL_PATTERN;
            gdtcmd->u.ioctl.channel = ha->raw[i].address | INVALID_CHANNEL;
            pds->bid = ha->raw[i].local_no;
            pds->first = 0;
            pds->entries = ha->raw[i].pdev_cnt;
            cnt = (3*GDTH_SCRATCH/4 - 5 * sizeof(u32)) /
                sizeof(pds->list[0]);
            if (pds->entries > cnt)
                pds->entries = cnt;

            if (gdth_execute(host, gdtcmd, cmnd, 30, NULL) != S_OK)
                pds->count = 0;

            /* other IOCTLs must fit into area GDTH_SCRATCH/4 */
            for (j = 0; j < ha->raw[i].pdev_cnt; ++j) {
                /* 2.b drive info */
                TRACE2(("scsi_drv_info() chn %d dev %d\n",
                    i, ha->raw[i].id_list[j]));             
                pdi = (gdth_diskinfo_str *)buf;
                gdtcmd->Service = CACHESERVICE;
                gdtcmd->OpCode = GDT_IOCTL;
                gdtcmd->u.ioctl.p_param = paddr;
                gdtcmd->u.ioctl.param_size = sizeof(gdth_diskinfo_str);
                gdtcmd->u.ioctl.subfunc = SCSI_DR_INFO | L_CTRL_PATTERN;
                gdtcmd->u.ioctl.channel = 
                    ha->raw[i].address | ha->raw[i].id_list[j];

                if (gdth_execute(host, gdtcmd, cmnd, 30, NULL) == S_OK) {
                    strncpy(hrec,pdi->vendor,8);
                    strncpy(hrec+8,pdi->product,16);
                    strncpy(hrec+24,pdi->revision,4);
                    hrec[28] = 0;
                    seq_printf(m,
                                   "\n Chn/ID/LUN:   \t%c/%02d/%d    \tName:          \t%s\n",
                                   'A'+i,pdi->target_id,pdi->lun,hrec);
                    flag = TRUE;
                    pdi->no_ldrive &= 0xffff;
                    if (pdi->no_ldrive == 0xffff)
                        strcpy(hrec,"--");
                    else
                        sprintf(hrec,"%d",pdi->no_ldrive);
                    seq_printf(m,
                                   " Capacity [MB]:\t%-6d    \tTo Log. Drive: \t%s\n",
                                   pdi->blkcnt/(1024*1024/pdi->blksize),
                                   hrec);
                } else {
                    pdi->devtype = 0xff;
                }
                    
                if (pdi->devtype == 0) {
                    /* search retries/reassigns */
                    for (k = 0; k < pds->count; ++k) {
                        if (pds->list[k].tid == pdi->target_id &&
                            pds->list[k].lun == pdi->lun) {
                            seq_printf(m,
                                           " Retries:      \t%-6d    \tReassigns:     \t%d\n",
                                           pds->list[k].retries,
                                           pds->list[k].reassigns);
                            break;
                        }
                    }
                    /* 2.c grown defects */
                    TRACE2(("scsi_drv_defcnt() chn %d dev %d\n",
                            i, ha->raw[i].id_list[j]));             
                    pdef = (gdth_defcnt_str *)buf;
                    gdtcmd->Service = CACHESERVICE;
                    gdtcmd->OpCode = GDT_IOCTL;
                    gdtcmd->u.ioctl.p_param = paddr;
                    gdtcmd->u.ioctl.param_size = sizeof(gdth_defcnt_str);
                    gdtcmd->u.ioctl.subfunc = SCSI_DEF_CNT | L_CTRL_PATTERN;
                    gdtcmd->u.ioctl.channel = 
                        ha->raw[i].address | ha->raw[i].id_list[j];
                    pdef->sddc_type = 0x08;

                    if (gdth_execute(host, gdtcmd, cmnd, 30, NULL) == S_OK) {
                        seq_printf(m,
                                       " Grown Defects:\t%d\n",
                                       pdef->sddc_cnt);
                    }
                }
            }
        }
        gdth_ioctl_free(ha, GDTH_SCRATCH, buf, paddr);

        if (!flag)
            seq_puts(m, "\n --\n");

        /* 3. about logical drives */
        seq_puts(m, "\nLogical Drives:");
        flag = FALSE;

        buf = gdth_ioctl_alloc(ha, GDTH_SCRATCH, FALSE, &paddr);
        if (!buf) 
            goto stop_output;
        for (i = 0; i < MAX_LDRIVES; ++i) {
            if (!ha->hdr[i].is_logdrv)
                continue;
            drv_no = i;
            j = k = 0;
            is_mirr = FALSE;
            do {
                /* 3.a log. drive info */
                TRACE2(("cache_drv_info() drive no %d\n",drv_no));
                pcdi = (gdth_cdrinfo_str *)buf;
                gdtcmd->Service = CACHESERVICE;
                gdtcmd->OpCode = GDT_IOCTL;
                gdtcmd->u.ioctl.p_param = paddr;
                gdtcmd->u.ioctl.param_size = sizeof(gdth_cdrinfo_str);
                gdtcmd->u.ioctl.subfunc = CACHE_DRV_INFO;
                gdtcmd->u.ioctl.channel = drv_no;
                if (gdth_execute(host, gdtcmd, cmnd, 30, NULL) != S_OK)
                    break;
                pcdi->ld_dtype >>= 16;
                j++;
                if (pcdi->ld_dtype > 2) {
                    strcpy(hrec, "missing");
                } else if (pcdi->ld_error & 1) {
                    strcpy(hrec, "fault");
                } else if (pcdi->ld_error & 2) {
                    strcpy(hrec, "invalid");
                    k++; j--;
                } else {
                    strcpy(hrec, "ok");
                }
                    
                if (drv_no == i) {
                    seq_printf(m,
                                   "\n Number:       \t%-2d        \tStatus:        \t%s\n",
                                   drv_no, hrec);
                    flag = TRUE;
                    no_mdrv = pcdi->cd_ldcnt;
                    if (no_mdrv > 1 || pcdi->ld_slave != -1) {
                        is_mirr = TRUE;
                        strcpy(hrec, "RAID-1");
                    } else if (pcdi->ld_dtype == 0) {
                        strcpy(hrec, "Disk");
                    } else if (pcdi->ld_dtype == 1) {
                        strcpy(hrec, "RAID-0");
                    } else if (pcdi->ld_dtype == 2) {
                        strcpy(hrec, "Chain");
                    } else {
                        strcpy(hrec, "???");
                    }
                    seq_printf(m,
                                   " Capacity [MB]:\t%-6d    \tType:          \t%s\n",
                                   pcdi->ld_blkcnt/(1024*1024/pcdi->ld_blksize),
                                   hrec);
                } else {
                    seq_printf(m,
                                   " Slave Number: \t%-2d        \tStatus:        \t%s\n",
                                   drv_no & 0x7fff, hrec);
                }
                drv_no = pcdi->ld_slave;
            } while (drv_no != -1);
             
            if (is_mirr)
                seq_printf(m,
                               " Missing Drv.: \t%-2d        \tInvalid Drv.:  \t%d\n",
                               no_mdrv - j - k, k);

            if (!ha->hdr[i].is_arraydrv)
                strcpy(hrec, "--");
            else
                sprintf(hrec, "%d", ha->hdr[i].master_no);
            seq_printf(m,
                           " To Array Drv.:\t%s\n", hrec);
        }       
        gdth_ioctl_free(ha, GDTH_SCRATCH, buf, paddr);
        
        if (!flag)
            seq_puts(m, "\n --\n");

        /* 4. about array drives */
        seq_puts(m, "\nArray Drives:");
        flag = FALSE;

        buf = gdth_ioctl_alloc(ha, GDTH_SCRATCH, FALSE, &paddr);
        if (!buf) 
            goto stop_output;
        for (i = 0; i < MAX_LDRIVES; ++i) {
            if (!(ha->hdr[i].is_arraydrv && ha->hdr[i].is_master))
                continue;
            /* 4.a array drive info */
            TRACE2(("array_info() drive no %d\n",i));
            pai = (gdth_arrayinf_str *)buf;
            gdtcmd->Service = CACHESERVICE;
            gdtcmd->OpCode = GDT_IOCTL;
            gdtcmd->u.ioctl.p_param = paddr;
            gdtcmd->u.ioctl.param_size = sizeof(gdth_arrayinf_str);
            gdtcmd->u.ioctl.subfunc = ARRAY_INFO | LA_CTRL_PATTERN;
            gdtcmd->u.ioctl.channel = i;
            if (gdth_execute(host, gdtcmd, cmnd, 30, NULL) == S_OK) {
                if (pai->ai_state == 0)
                    strcpy(hrec, "idle");
                else if (pai->ai_state == 2)
                    strcpy(hrec, "build");
                else if (pai->ai_state == 4)
                    strcpy(hrec, "ready");
                else if (pai->ai_state == 6)
                    strcpy(hrec, "fail");
                else if (pai->ai_state == 8 || pai->ai_state == 10)
                    strcpy(hrec, "rebuild");
                else
                    strcpy(hrec, "error");
                if (pai->ai_ext_state & 0x10)
                    strcat(hrec, "/expand");
                else if (pai->ai_ext_state & 0x1)
                    strcat(hrec, "/patch");
                seq_printf(m,
                               "\n Number:       \t%-2d        \tStatus:        \t%s\n",
                               i,hrec);
                flag = TRUE;

                if (pai->ai_type == 0)
                    strcpy(hrec, "RAID-0");
                else if (pai->ai_type == 4)
                    strcpy(hrec, "RAID-4");
                else if (pai->ai_type == 5)
                    strcpy(hrec, "RAID-5");
                else 
                    strcpy(hrec, "RAID-10");
                seq_printf(m,
                               " Capacity [MB]:\t%-6d    \tType:          \t%s\n",
                               pai->ai_size/(1024*1024/pai->ai_secsize),
                               hrec);
            }
        }
        gdth_ioctl_free(ha, GDTH_SCRATCH, buf, paddr);
        
        if (!flag)
            seq_puts(m, "\n --\n");

        /* 5. about host drives */
        seq_puts(m, "\nHost Drives:");
        flag = FALSE;

        buf = gdth_ioctl_alloc(ha, sizeof(gdth_hget_str), FALSE, &paddr);
        if (!buf) 
            goto stop_output;
        for (i = 0; i < MAX_LDRIVES; ++i) {
            if (!ha->hdr[i].is_logdrv || 
                (ha->hdr[i].is_arraydrv && !ha->hdr[i].is_master))
                continue;
            /* 5.a get host drive list */
            TRACE2(("host_get() drv_no %d\n",i));           
            phg = (gdth_hget_str *)buf;
            gdtcmd->Service = CACHESERVICE;
            gdtcmd->OpCode = GDT_IOCTL;
            gdtcmd->u.ioctl.p_param = paddr;
            gdtcmd->u.ioctl.param_size = sizeof(gdth_hget_str);
            gdtcmd->u.ioctl.subfunc = HOST_GET | LA_CTRL_PATTERN;
            gdtcmd->u.ioctl.channel = i;
            phg->entries = MAX_HDRIVES;
            phg->offset = GDTOFFSOF(gdth_hget_str, entry[0]); 
            if (gdth_execute(host, gdtcmd, cmnd, 30, NULL) == S_OK) {
                ha->hdr[i].ldr_no = i;
                ha->hdr[i].rw_attribs = 0;
                ha->hdr[i].start_sec = 0;
            } else {
                for (j = 0; j < phg->entries; ++j) {
                    k = phg->entry[j].host_drive;
                    if (k >= MAX_LDRIVES)
                        continue;
                    ha->hdr[k].ldr_no = phg->entry[j].log_drive;
                    ha->hdr[k].rw_attribs = phg->entry[j].rw_attribs;
                    ha->hdr[k].start_sec = phg->entry[j].start_sec;
                }
            }
        }
        gdth_ioctl_free(ha, sizeof(gdth_hget_str), buf, paddr);

        for (i = 0; i < MAX_HDRIVES; ++i) {
            if (!(ha->hdr[i].present))
                continue;
              
            seq_printf(m,
                           "\n Number:       \t%-2d        \tArr/Log. Drive:\t%d\n",
                           i, ha->hdr[i].ldr_no);
            flag = TRUE;

            seq_printf(m,
                           " Capacity [MB]:\t%-6d    \tStart Sector:  \t%d\n",
                           (u32)(ha->hdr[i].size/2048), ha->hdr[i].start_sec);
        }
        
        if (!flag)
            seq_puts(m, "\n --\n");
    }

    /* controller events */
    seq_puts(m, "\nController Events:\n");

    for (id = -1;;) {
        id = gdth_read_event(ha, id, estr);
        if (estr->event_source == 0)
            break;
        if (estr->event_data.eu.driver.ionode == ha->hanum &&
            estr->event_source == ES_ASYNC) { 
            gdth_log_event(&estr->event_data, hrec);

	    /*
	     * Elapsed seconds subtraction with unsigned operands is
	     * safe from wrap around in year 2106.  Executes as:
	     * operand a + (2's complement operand b) + 1
	     */

	    sec = (int)((u32)ktime_get_real_seconds() - estr->first_stamp);
            if (sec < 0) sec = 0;
            seq_printf(m," date- %02d:%02d:%02d\t%s\n",
                           sec/3600, sec%3600/60, sec%60, hrec);
        }
        if (id == -1)
            break;
    }
stop_output:
    rc = 0;
free_fail:
    kfree(gdtcmd);
    kfree(estr);
    return rc;
}

static char *gdth_ioctl_alloc(gdth_ha_str *ha, int size, int scratch,
                              u64 *paddr)
{
    unsigned long flags;
    char *ret_val;

    if (size == 0)
        return NULL;

    spin_lock_irqsave(&ha->smp_lock, flags);

    if (!ha->scratch_busy && size <= GDTH_SCRATCH) {
        ha->scratch_busy = TRUE;
        ret_val = ha->pscratch;
        *paddr = ha->scratch_phys;
    } else if (scratch) {
        ret_val = NULL;
    } else {
        dma_addr_t dma_addr;

        ret_val = pci_alloc_consistent(ha->pdev, size, &dma_addr);
        *paddr = dma_addr;
    }

    spin_unlock_irqrestore(&ha->smp_lock, flags);
    return ret_val;
}

static void gdth_ioctl_free(gdth_ha_str *ha, int size, char *buf, u64 paddr)
{
    unsigned long flags;

    if (buf == ha->pscratch) {
	spin_lock_irqsave(&ha->smp_lock, flags);
        ha->scratch_busy = FALSE;
	spin_unlock_irqrestore(&ha->smp_lock, flags);
    } else {
        pci_free_consistent(ha->pdev, size, buf, paddr);
    }
}

#ifdef GDTH_IOCTL_PROC
static int gdth_ioctl_check_bin(gdth_ha_str *ha, u16 size)
{
    unsigned long flags;
    int ret_val;

    spin_lock_irqsave(&ha->smp_lock, flags);

    ret_val = FALSE;
    if (ha->scratch_busy) {
        if (((gdth_iord_str *)ha->pscratch)->size == (u32)size)
            ret_val = TRUE;
    }
    spin_unlock_irqrestore(&ha->smp_lock, flags);
    return ret_val;
}
#endif

static void gdth_wait_completion(gdth_ha_str *ha, int busnum, int id)
{
    unsigned long flags;
    int i;
    struct scsi_cmnd *scp;
    struct gdth_cmndinfo *cmndinfo;
    u8 b, t;

    spin_lock_irqsave(&ha->smp_lock, flags);

    for (i = 0; i < GDTH_MAXCMDS; ++i) {
        scp = ha->cmd_tab[i].cmnd;
        cmndinfo = gdth_cmnd_priv(scp);

        b = scp->device->channel;
        t = scp->device->id;
        if (!SPECIAL_SCP(scp) && t == (u8)id && 
            b == (u8)busnum) {
            cmndinfo->wait_for_completion = 0;
            spin_unlock_irqrestore(&ha->smp_lock, flags);
            while (!cmndinfo->wait_for_completion)
                barrier();
            spin_lock_irqsave(&ha->smp_lock, flags);
        }
    }
    spin_unlock_irqrestore(&ha->smp_lock, flags);
}
