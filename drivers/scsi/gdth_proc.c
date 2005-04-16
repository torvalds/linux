/* gdth_proc.c 
 * $Id: gdth_proc.c,v 1.42 2004/03/05 15:50:20 achim Exp $
 */

#include <linux/completion.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
int gdth_proc_info(struct Scsi_Host *host, char *buffer,char **start,off_t offset,int length,   
                   int inout)
{
    int hanum,busnum;

    TRACE2(("gdth_proc_info() length %d offs %d inout %d\n",
            length,(int)offset,inout));

    hanum = NUMDATA(host)->hanum;
    busnum= NUMDATA(host)->busnum;

    if (inout)
        return(gdth_set_info(buffer,length,host,hanum,busnum));
    else
        return(gdth_get_info(buffer,start,offset,length,host,hanum,busnum));
}
#else
int gdth_proc_info(char *buffer,char **start,off_t offset,int length,int hostno,   
                   int inout)
{
    int hanum,busnum,i;

    TRACE2(("gdth_proc_info() length %d offs %d inout %d\n",
            length,(int)offset,inout));

    for (i = 0; i < gdth_ctr_vcount; ++i) {
        if (gdth_ctr_vtab[i]->host_no == hostno)
            break;
    }
    if (i == gdth_ctr_vcount)
        return(-EINVAL);

    hanum = NUMDATA(gdth_ctr_vtab[i])->hanum;
    busnum= NUMDATA(gdth_ctr_vtab[i])->busnum;

    if (inout)
        return(gdth_set_info(buffer,length,gdth_ctr_vtab[i],hanum,busnum));
    else
        return(gdth_get_info(buffer,start,offset,length,
                             gdth_ctr_vtab[i],hanum,busnum));
}
#endif

static int gdth_set_info(char *buffer,int length,struct Scsi_Host *host,
                         int hanum,int busnum)
{
    int             ret_val = -EINVAL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    Scsi_Request    *scp;
    Scsi_Device     *sdev;
#else
    Scsi_Cmnd       *scp;
    Scsi_Device     *sdev;
#endif
    TRACE2(("gdth_set_info() ha %d bus %d\n",hanum,busnum));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    sdev = scsi_get_host_dev(host);
    scp  = scsi_allocate_request(sdev, GFP_KERNEL);
    if (!scp)
        return -ENOMEM;
    scp->sr_cmd_len = 12;
    scp->sr_use_sg = 0;
#else
    sdev = scsi_get_host_dev(host);
    scp  = scsi_allocate_device(sdev, 1, FALSE);
    if (!scp)
        return -ENOMEM;
    scp->cmd_len = 12;
    scp->use_sg = 0;
#endif

    if (length >= 4) {
        if (strncmp(buffer,"gdth",4) == 0) {
            buffer += 5;
            length -= 5;
            ret_val = gdth_set_asc_info( buffer, length, hanum, scp );
        }
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    scsi_release_request(scp);
    scsi_free_host_dev(sdev);
#else
    scsi_release_command(scp);
    scsi_free_host_dev(sdev);
#endif
    return ret_val;
}
         
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int gdth_set_asc_info(char *buffer,int length,int hanum,Scsi_Request *scp)
#else
static int gdth_set_asc_info(char *buffer,int length,int hanum,Scsi_Cmnd *scp)
#endif
{
    int             orig_length, drive, wb_mode;
    int             i, found;
    gdth_ha_str     *ha;
    gdth_cmd_str    gdtcmd;
    gdth_cpar_str   *pcpar;
    ulong64         paddr;

    char            cmnd[MAX_COMMAND_SIZE];
    memset(cmnd, 0xff, 12);
    memset(&gdtcmd, 0, sizeof(gdth_cmd_str));

    TRACE2(("gdth_set_asc_info() ha %d\n",hanum));
    ha = HADATA(gdth_ctr_tab[hanum]);
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
                gdth_do_req(scp, &gdtcmd, cmnd, 30);
#else
                gdth_do_cmd(scp, &gdtcmd, cmnd, 30);
#endif
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
        if (!gdth_ioctl_alloc(hanum, sizeof(gdth_cpar_str), TRUE, &paddr))
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        gdth_do_req(scp, &gdtcmd, cmnd, 30);
#else
        gdth_do_cmd(scp, &gdtcmd, cmnd, 30);
#endif
        gdth_ioctl_free(hanum, GDTH_SCRATCH, ha->pscratch, paddr);
        printk("Done.\n");
        return(orig_length);
    }

    printk("GDT: Unknown command: %s  Length: %d\n",buffer,length);
    return(-EINVAL);
}

static int gdth_get_info(char *buffer,char **start,off_t offset,int length,
                         struct Scsi_Host *host,int hanum,int busnum)
{
    int size = 0,len = 0;
    off_t begin = 0,pos = 0;
    gdth_ha_str *ha;
    int id, i, j, k, sec, flag;
    int no_mdrv = 0, drv_no, is_mirr;
    ulong32 cnt;
    ulong64 paddr;
    int rc = -ENOMEM;

    gdth_cmd_str *gdtcmd;
    gdth_evt_str *estr;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    Scsi_Request *scp;
    Scsi_Device *sdev; 
#else
    Scsi_Cmnd *scp;
    Scsi_Device *sdev;
#endif
    char hrec[161];
    struct timeval tv;

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

    TRACE2(("gdth_get_info() ha %d bus %d\n",hanum,busnum));
    ha = HADATA(gdth_ctr_tab[hanum]);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    sdev = scsi_get_host_dev(host);
    scp  = scsi_allocate_request(sdev, GFP_KERNEL);
    if (!scp)
        goto free_fail;
    scp->sr_cmd_len = 12;
    scp->sr_use_sg = 0;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    sdev = scsi_get_host_dev(host);
    scp  = scsi_allocate_device(sdev, 1, FALSE);
    if (!scp)
        goto free_fail;
    scp->cmd_len = 12;
    scp->use_sg = 0;
#else
    memset(&sdev,0,sizeof(Scsi_Device));
    memset(&scp, 0,sizeof(Scsi_Cmnd));
    sdev.host = scp.host = host;
    sdev.id = scp.target = sdev.host->this_id;
    scp.device = &sdev;
#endif
    
    
    /* request is i.e. "cat /proc/scsi/gdth/0" */ 
    /* format: %-15s\t%-10s\t%-15s\t%s */
    /* driver parameters */
    size = sprintf(buffer+len,"Driver Parameters:\n");
    len += size;  pos = begin + len;
    if (reserve_list[0] == 0xff)
        strcpy(hrec, "--");
    else {
        sprintf(hrec, "%d", reserve_list[0]);
        for (i = 1;  i < MAX_RES_ARGS; i++) {
            if (reserve_list[i] == 0xff) 
                break;
            sprintf(hrec,"%s,%d", hrec, reserve_list[i]);
        }
    }
    size = sprintf(buffer+len,
                   " reserve_mode: \t%d         \treserve_list:  \t%s\n",
                   reserve_mode, hrec);
    len += size;  pos = begin + len;
    size = sprintf(buffer+len,
                   " max_ids:      \t%-3d       \thdr_channel:   \t%d\n",
                   max_ids, hdr_channel);
    len += size;  pos = begin + len;

    /* controller information */
    size = sprintf(buffer+len,"\nDisk Array Controller Information:\n");
    len += size;  pos = begin + len;
    if (virt_ctr)
        sprintf(hrec, "%s (Bus %d)", ha->binfo.type_string, busnum);
    else
        strcpy(hrec, ha->binfo.type_string);
    size = sprintf(buffer+len,
                   " Number:       \t%d         \tName:          \t%s\n",
                   hanum, hrec);
    len += size;  pos = begin + len;

    if (ha->more_proc)
        sprintf(hrec, "%d.%02d.%02d-%c%03X", 
                (unchar)(ha->binfo.upd_fw_ver>>24),
                (unchar)(ha->binfo.upd_fw_ver>>16),
                (unchar)(ha->binfo.upd_fw_ver),
                ha->bfeat.raid ? 'R':'N',
                ha->binfo.upd_revision);
    else
        sprintf(hrec, "%d.%02d", (unchar)(ha->cpar.version>>8),
                (unchar)(ha->cpar.version));

    size = sprintf(buffer+len,
                   " Driver Ver.:  \t%-10s\tFirmware Ver.: \t%s\n",
                   GDTH_VERSION_STR, hrec);
    len += size;  pos = begin + len;
 
    if (ha->more_proc) {
        /* more information: 1. about controller */
        size = sprintf(buffer+len,
                       " Serial No.:   \t0x%8X\tCache RAM size:\t%d KB\n",
                       ha->binfo.ser_no, ha->binfo.memsize / 1024);
        len += size;  pos = begin + len;
    }

#ifdef GDTH_DMA_STATISTICS
    /* controller statistics */
    size = sprintf(buffer+len,"\nController Statistics:\n");
    len += size;  pos = begin + len;
    size = sprintf(buffer+len,
                   " 32-bit DMA buffer:\t%lu\t64-bit DMA buffer:\t%lu\n",
                   ha->dma32_cnt, ha->dma64_cnt);
    len += size;  pos = begin + len;
#endif

    if (pos < offset) {
        len = 0;
        begin = pos;
    }
    if (pos > offset + length)
        goto stop_output;

    if (ha->more_proc) {
        /* more information: 2. about physical devices */
        size = sprintf(buffer+len,"\nPhysical Devices:");
        len += size;  pos = begin + len;
        flag = FALSE;
            
        buf = gdth_ioctl_alloc(hanum, GDTH_SCRATCH, FALSE, &paddr);
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
            cnt = (3*GDTH_SCRATCH/4 - 5 * sizeof(ulong32)) /
                sizeof(pds->list[0]);
            if (pds->entries > cnt)
                pds->entries = cnt;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
            gdth_do_req(scp, gdtcmd, cmnd, 30);
            if (scp->sr_command->SCp.Status != S_OK) 
#else
            gdth_do_cmd(scp, gdtcmd, cmnd, 30);
            if (scp->SCp.Status != S_OK) 
#endif
            { 
                pds->count = 0;
            }

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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
                gdth_do_req(scp, gdtcmd, cmnd, 30);
                if (scp->sr_command->SCp.Status == S_OK) 
#else
                gdth_do_cmd(scp, gdtcmd, cmnd, 30);
                if (scp->SCp.Status == S_OK) 
#endif
                {
                    strncpy(hrec,pdi->vendor,8);
                    strncpy(hrec+8,pdi->product,16);
                    strncpy(hrec+24,pdi->revision,4);
                    hrec[28] = 0;
                    size = sprintf(buffer+len,
                                   "\n Chn/ID/LUN:   \t%c/%02d/%d    \tName:          \t%s\n",
                                   'A'+i,pdi->target_id,pdi->lun,hrec);
                    len += size;  pos = begin + len;
                    flag = TRUE;
                    pdi->no_ldrive &= 0xffff;
                    if (pdi->no_ldrive == 0xffff)
                        strcpy(hrec,"--");
                    else
                        sprintf(hrec,"%d",pdi->no_ldrive);
                    size = sprintf(buffer+len,
                                   " Capacity [MB]:\t%-6d    \tTo Log. Drive: \t%s\n",
                                   pdi->blkcnt/(1024*1024/pdi->blksize),
                                   hrec);
                    len += size;  pos = begin + len;
                } else {
                    pdi->devtype = 0xff;
                }
                    
                if (pdi->devtype == 0) {
                    /* search retries/reassigns */
                    for (k = 0; k < pds->count; ++k) {
                        if (pds->list[k].tid == pdi->target_id &&
                            pds->list[k].lun == pdi->lun) {
                            size = sprintf(buffer+len,
                                           " Retries:      \t%-6d    \tReassigns:     \t%d\n",
                                           pds->list[k].retries,
                                           pds->list[k].reassigns);
                            len += size;  pos = begin + len;
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
                    gdth_do_req(scp, gdtcmd, cmnd, 30);
                    if (scp->sr_command->SCp.Status == S_OK) 
#else
                    gdth_do_cmd(scp, gdtcmd, cmnd, 30);
                    if (scp->SCp.Status == S_OK) 
#endif
                    {
                        size = sprintf(buffer+len,
                                       " Grown Defects:\t%d\n",
                                       pdef->sddc_cnt);
                        len += size;  pos = begin + len;
                    }
                }
                if (pos < offset) {
                    len = 0;
                    begin = pos;
                }
                if (pos > offset + length)
                    goto stop_output;
            }
        }
        gdth_ioctl_free(hanum, GDTH_SCRATCH, buf, paddr);

        if (!flag) {
            size = sprintf(buffer+len, "\n --\n");
            len += size;  pos = begin + len;
        }

        /* 3. about logical drives */
        size = sprintf(buffer+len,"\nLogical Drives:");
        len += size;  pos = begin + len;
        flag = FALSE;

        buf = gdth_ioctl_alloc(hanum, GDTH_SCRATCH, FALSE, &paddr);
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
                gdth_do_req(scp, gdtcmd, cmnd, 30);
                if (scp->sr_command->SCp.Status != S_OK) 
#else
                gdth_do_cmd(scp, gdtcmd, cmnd, 30);
                if (scp->SCp.Status != S_OK)
#endif
                {
                    break;
                }
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
                    size = sprintf(buffer+len,
                                   "\n Number:       \t%-2d        \tStatus:        \t%s\n",
                                   drv_no, hrec);
                    len += size;  pos = begin + len;
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
                    size = sprintf(buffer+len,
                                   " Capacity [MB]:\t%-6d    \tType:          \t%s\n",
                                   pcdi->ld_blkcnt/(1024*1024/pcdi->ld_blksize),
                                   hrec);
                    len += size;  pos = begin + len;
                } else {
                    size = sprintf(buffer+len,
                                   " Slave Number: \t%-2d        \tStatus:        \t%s\n",
                                   drv_no & 0x7fff, hrec);
                    len += size;  pos = begin + len;
                }
                drv_no = pcdi->ld_slave;
                if (pos < offset) {
                    len = 0;
                    begin = pos;
                }
                if (pos > offset + length)
                    goto stop_output;
            } while (drv_no != -1);
             
            if (is_mirr) {
                size = sprintf(buffer+len,
                               " Missing Drv.: \t%-2d        \tInvalid Drv.:  \t%d\n",
                               no_mdrv - j - k, k);
                len += size;  pos = begin + len;
            }
              
            if (!ha->hdr[i].is_arraydrv)
                strcpy(hrec, "--");
            else
                sprintf(hrec, "%d", ha->hdr[i].master_no);
            size = sprintf(buffer+len,
                           " To Array Drv.:\t%s\n", hrec);
            len += size;  pos = begin + len;
            if (pos < offset) {
                len = 0;
                begin = pos;
            }
            if (pos > offset + length)
                goto stop_output;
        }       
        gdth_ioctl_free(hanum, GDTH_SCRATCH, buf, paddr);
        
        if (!flag) {
            size = sprintf(buffer+len, "\n --\n");
            len += size;  pos = begin + len;
        }   

        /* 4. about array drives */
        size = sprintf(buffer+len,"\nArray Drives:");
        len += size;  pos = begin + len;
        flag = FALSE;

        buf = gdth_ioctl_alloc(hanum, GDTH_SCRATCH, FALSE, &paddr);
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
            gdth_do_req(scp, gdtcmd, cmnd, 30);
            if (scp->sr_command->SCp.Status == S_OK) 
#else
            gdth_do_cmd(scp, gdtcmd, cmnd, 30);
            if (scp->SCp.Status == S_OK) 
#endif
            {
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
                size = sprintf(buffer+len,
                               "\n Number:       \t%-2d        \tStatus:        \t%s\n",
                               i,hrec);
                len += size;  pos = begin + len;
                flag = TRUE;

                if (pai->ai_type == 0)
                    strcpy(hrec, "RAID-0");
                else if (pai->ai_type == 4)
                    strcpy(hrec, "RAID-4");
                else if (pai->ai_type == 5)
                    strcpy(hrec, "RAID-5");
                else 
                    strcpy(hrec, "RAID-10");
                size = sprintf(buffer+len,
                               " Capacity [MB]:\t%-6d    \tType:          \t%s\n",
                               pai->ai_size/(1024*1024/pai->ai_secsize),
                               hrec);
                len += size;  pos = begin + len;
                if (pos < offset) {
                    len = 0;
                    begin = pos;
                }
                if (pos > offset + length)
                    goto stop_output;
            }
        }
        gdth_ioctl_free(hanum, GDTH_SCRATCH, buf, paddr);
        
        if (!flag) {
            size = sprintf(buffer+len, "\n --\n");
            len += size;  pos = begin + len;
        }

        /* 5. about host drives */
        size = sprintf(buffer+len,"\nHost Drives:");
        len += size;  pos = begin + len;
        flag = FALSE;

        buf = gdth_ioctl_alloc(hanum, sizeof(gdth_hget_str), FALSE, &paddr);
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
            gdth_do_req(scp, gdtcmd, cmnd, 30);
            if (scp->sr_command->SCp.Status != S_OK) 
#else
            gdth_do_cmd(scp, gdtcmd, cmnd, 30);
            if (scp->SCp.Status != S_OK) 
#endif
            {
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
        gdth_ioctl_free(hanum, sizeof(gdth_hget_str), buf, paddr);

        for (i = 0; i < MAX_HDRIVES; ++i) {
            if (!(ha->hdr[i].present))
                continue;
              
            size = sprintf(buffer+len,
                           "\n Number:       \t%-2d        \tArr/Log. Drive:\t%d\n",
                           i, ha->hdr[i].ldr_no);
            len += size;  pos = begin + len;
            flag = TRUE;

            size = sprintf(buffer+len,
                           " Capacity [MB]:\t%-6d    \tStart Sector:  \t%d\n",
                           (ulong32)(ha->hdr[i].size/2048), ha->hdr[i].start_sec);
            len += size;  pos = begin + len;
            if (pos < offset) {
                len = 0;
                begin = pos;
            }
            if (pos > offset + length)
                goto stop_output;
        }
        
        if (!flag) {
            size = sprintf(buffer+len, "\n --\n");
            len += size;  pos = begin + len;
        }
    }

    /* controller events */
    size = sprintf(buffer+len,"\nController Events:\n");
    len += size;  pos = begin + len;

    for (id = -1;;) {
        id = gdth_read_event(ha, id, estr);
        if (estr->event_source == 0)
            break;
        if (estr->event_data.eu.driver.ionode == hanum &&
            estr->event_source == ES_ASYNC) { 
            gdth_log_event(&estr->event_data, hrec);
            do_gettimeofday(&tv);
            sec = (int)(tv.tv_sec - estr->first_stamp);
            if (sec < 0) sec = 0;
            size = sprintf(buffer+len," date- %02d:%02d:%02d\t%s\n",
                           sec/3600, sec%3600/60, sec%60, hrec);
            len += size;  pos = begin + len;
            if (pos < offset) {
                len = 0;
                begin = pos;
            }
            if (pos > offset + length)
                goto stop_output;
        }
        if (id == -1)
            break;
    }

stop_output:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    scsi_release_request(scp);
    scsi_free_host_dev(sdev);
#else
    scsi_release_command(scp);
    scsi_free_host_dev(sdev);
#endif
    *start = buffer +(offset-begin);
    len -= (offset-begin);
    if (len > length)
        len = length;
    TRACE2(("get_info() len %d pos %d begin %d offset %d length %d size %d\n",
            len,(int)pos,(int)begin,(int)offset,length,size));
    rc = len;

free_fail:
    kfree(gdtcmd);
    kfree(estr);
    return rc;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static void gdth_do_req(Scsi_Request *scp, gdth_cmd_str *gdtcmd, 
                        char *cmnd, int timeout)
{
    unsigned bufflen;
    DECLARE_COMPLETION(wait);

    TRACE2(("gdth_do_req()\n"));
    if (gdtcmd != NULL) { 
        bufflen = sizeof(gdth_cmd_str);
    } else {
        bufflen = 0;
    }
    scp->sr_request->rq_status = RQ_SCSI_BUSY;
    scp->sr_request->waiting = &wait;
    scsi_do_req(scp, cmnd, gdtcmd, bufflen, gdth_scsi_done, timeout*HZ, 1);
    wait_for_completion(&wait);
}

#else
static void gdth_do_cmd(Scsi_Cmnd *scp, gdth_cmd_str *gdtcmd, 
                        char *cmnd, int timeout)
{
    unsigned bufflen;
    DECLARE_COMPLETION(wait);

    TRACE2(("gdth_do_cmd()\n"));
    if (gdtcmd != NULL) { 
        scp->SCp.this_residual = IOCTL_PRI;
        bufflen = sizeof(gdth_cmd_str);
    } else {
        scp->SCp.this_residual = DEFAULT_PRI;
        bufflen = 0;
    }

    scp->request.rq_status = RQ_SCSI_BUSY;
    scp->request.waiting = &wait;
    scsi_do_cmd(scp, cmnd, gdtcmd, bufflen, gdth_scsi_done, timeout*HZ, 1);
    wait_for_completion(&wait);
}
#endif

void gdth_scsi_done(Scsi_Cmnd *scp)
{
    TRACE2(("gdth_scsi_done()\n"));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    scp->request->rq_status = RQ_SCSI_DONE;
    if (scp->request->waiting != NULL)
        complete(scp->request->waiting);
#else
    scp->request.rq_status = RQ_SCSI_DONE;
    if (scp->request.waiting != NULL)
        complete(scp->request.waiting);
#endif
}

static char *gdth_ioctl_alloc(int hanum, int size, int scratch, 
                              ulong64 *paddr)
{
    gdth_ha_str *ha;
    ulong flags;
    char *ret_val;

    if (size == 0)
        return NULL;

    ha = HADATA(gdth_ctr_tab[hanum]);
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

static void gdth_ioctl_free(int hanum, int size, char *buf, ulong64 paddr)
{
    gdth_ha_str *ha;
    ulong flags;

    ha = HADATA(gdth_ctr_tab[hanum]);
    spin_lock_irqsave(&ha->smp_lock, flags);

    if (buf == ha->pscratch) {
        ha->scratch_busy = FALSE;
    } else {
        pci_free_consistent(ha->pdev, size, buf, paddr);
    }

    spin_unlock_irqrestore(&ha->smp_lock, flags);
}

#ifdef GDTH_IOCTL_PROC
static int gdth_ioctl_check_bin(int hanum, ushort size)
{
    gdth_ha_str *ha;
    ulong flags;
    int ret_val;

    ha = HADATA(gdth_ctr_tab[hanum]);
    spin_lock_irqsave(&ha->smp_lock, flags);

    ret_val = FALSE;
    if (ha->scratch_busy) {
        if (((gdth_iord_str *)ha->pscratch)->size == (ulong32)size)
            ret_val = TRUE;
    }
    spin_unlock_irqrestore(&ha->smp_lock, flags);
    return ret_val;
}
#endif

static void gdth_wait_completion(int hanum, int busnum, int id)
{
    gdth_ha_str *ha;
    ulong flags;
    int i;
    Scsi_Cmnd *scp;
    unchar b, t;

    ha = HADATA(gdth_ctr_tab[hanum]);
    spin_lock_irqsave(&ha->smp_lock, flags);

    for (i = 0; i < GDTH_MAXCMDS; ++i) {
        scp = ha->cmd_tab[i].cmnd;

        b = virt_ctr ? NUMDATA(scp->device->host)->busnum : scp->device->channel;
        t = scp->device->id;
        if (!SPECIAL_SCP(scp) && t == (unchar)id && 
            b == (unchar)busnum) {
            scp->SCp.have_data_in = 0;
            spin_unlock_irqrestore(&ha->smp_lock, flags);
            while (!scp->SCp.have_data_in)
                barrier();
            spin_lock_irqsave(&ha->smp_lock, flags);
        }
    }
    spin_unlock_irqrestore(&ha->smp_lock, flags);
}

static void gdth_stop_timeout(int hanum, int busnum, int id)
{
    gdth_ha_str *ha;
    ulong flags;
    Scsi_Cmnd *scp;
    unchar b, t;

    ha = HADATA(gdth_ctr_tab[hanum]);
    spin_lock_irqsave(&ha->smp_lock, flags);

    for (scp = ha->req_first; scp; scp = (Scsi_Cmnd *)scp->SCp.ptr) {
        b = virt_ctr ? NUMDATA(scp->device->host)->busnum : scp->device->channel;
        t = scp->device->id;
        if (t == (unchar)id && b == (unchar)busnum) {
            TRACE2(("gdth_stop_timeout(): update_timeout()\n"));
            scp->SCp.buffers_residual = gdth_update_timeout(hanum, scp, 0);
        }
    }
    spin_unlock_irqrestore(&ha->smp_lock, flags);
}

static void gdth_start_timeout(int hanum, int busnum, int id)
{
    gdth_ha_str *ha;
    ulong flags;
    Scsi_Cmnd *scp;
    unchar b, t;

    ha = HADATA(gdth_ctr_tab[hanum]);
    spin_lock_irqsave(&ha->smp_lock, flags);

    for (scp = ha->req_first; scp; scp = (Scsi_Cmnd *)scp->SCp.ptr) {
        b = virt_ctr ? NUMDATA(scp->device->host)->busnum : scp->device->channel;
        t = scp->device->id;
        if (t == (unchar)id && b == (unchar)busnum) {
            TRACE2(("gdth_start_timeout(): update_timeout()\n"));
            gdth_update_timeout(hanum, scp, scp->SCp.buffers_residual);
        }
    }
    spin_unlock_irqrestore(&ha->smp_lock, flags);
}

static int gdth_update_timeout(int hanum, Scsi_Cmnd *scp, int timeout)
{
    int oldto;

    oldto = scp->timeout_per_command;
    scp->timeout_per_command = timeout;

    if (timeout == 0) {
        del_timer(&scp->eh_timeout);
        scp->eh_timeout.data = (unsigned long) NULL;
        scp->eh_timeout.expires = 0;
    } else {
        if (scp->eh_timeout.data != (unsigned long) NULL) 
            del_timer(&scp->eh_timeout);
        scp->eh_timeout.data = (unsigned long) scp;
        scp->eh_timeout.expires = jiffies + timeout;
        add_timer(&scp->eh_timeout);
    }

    return oldto;
}
