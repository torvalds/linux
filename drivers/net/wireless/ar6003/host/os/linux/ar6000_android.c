//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------
#include "ar6000_drv.h"
#include "htc.h"
#include <linux/vmalloc.h>
#include <linux/fs.h>

#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#ifdef CONFIG_MMC_MSM
A_BOOL enable_mmc_host_detect_change = 1;
#else
A_BOOL enable_mmc_host_detect_change = 0;
#endif
static void ar6000_enable_mmchost_detect_change(int enable);


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#ifdef RK29
#ifdef TCHIP
char softmac_path[256] = "/data/misc/wifi/ar6302";
#endif
char fwpath[256] = "/system/etc/firmware";
//char fwpath[256] = "/data/ar6302";
#else
char fwpath[256] = "/system/wifi";
#endif
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) */
int wowledon;
unsigned int enablelogcat;

extern int bmienable;
extern struct net_device *ar6000_devices[];
extern char ifname[];

#ifdef CONFIG_HAS_WAKELOCK
extern struct wake_lock ar6k_wow_wake_lock;
struct wake_lock ar6k_init_wake_lock;
#endif
extern int num_device;

const char def_ifname[] = "wlan0";
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
module_param_string(fwpath, fwpath, sizeof(fwpath), 0644);
module_param(enablelogcat, uint, 0644);
module_param(wowledon, int, 0644);
#else
#define __user
/* for linux 2.4 and lower */
MODULE_PARAM(wowledon,"i");
#endif 

#ifdef CONFIG_HAS_EARLYSUSPEND
static int screen_is_off;
static struct early_suspend ar6k_early_suspend;
#endif

static A_STATUS (*ar6000_avail_ev_p)(void *, void *);

#if defined(CONFIG_ANDROID_LOGGER) && (!defined(CONFIG_MMC_MSM) || LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
int logger_write(const enum logidx index,
                const unsigned char prio,
                const char __kernel * const tag,
                const char __kernel * const fmt,
                ...)
{
    int ret = 0;
    va_list vargs;
    struct file *filp = (struct file *)-ENOENT;
    mm_segment_t oldfs;
    struct iovec vec[3];
    int tag_bytes = strlen(tag) + 1, msg_bytes;
    char *msg;      
    va_start(vargs, fmt);
    msg = kvasprintf(GFP_ATOMIC, fmt, vargs);
    va_end(vargs);
    if (!msg)
        return -ENOMEM;
    if (in_interrupt()) {
        /* we have no choice since aio_write may be blocked */
        printk(KERN_ALERT "%s", msg);
        goto out_free_message;
    }
    msg_bytes = strlen(msg) + 1;
    if (msg_bytes <= 1) /* empty message? */
        goto out_free_message; /* don't bother, then */
    if ((msg_bytes + tag_bytes + 1) > 2048) {
        ret = -E2BIG;
        goto out_free_message;
    }
            
    vec[0].iov_base  = (unsigned char *) &prio;
    vec[0].iov_len    = 1;
    vec[1].iov_base   = (void *) tag;
    vec[1].iov_len    = strlen(tag) + 1;
    vec[2].iov_base   = (void *) msg;
    vec[2].iov_len    = strlen(msg) + 1; 

    oldfs = get_fs();
    set_fs(KERNEL_DS);
    do {
        filp = filp_open("/dev/log/main", O_WRONLY, S_IRUSR);
        if (IS_ERR(filp) || !filp->f_op) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: filp_open /dev/log/main error\n", __FUNCTION__));
            ret = -ENOENT;
            break;
        }

        if (filp->f_op->aio_write) {
            int nr_segs = sizeof(vec) / sizeof(vec[0]);
            int len = vec[0].iov_len + vec[1].iov_len + vec[2].iov_len;
            struct kiocb kiocb;
            init_sync_kiocb(&kiocb, filp);
            kiocb.ki_pos = 0;
            kiocb.ki_left = len;
            kiocb.ki_nbytes = len;
            ret = filp->f_op->aio_write(&kiocb, vec, nr_segs, kiocb.ki_pos);
        }
        
    } while (0);

    if (!IS_ERR(filp)) {
        filp_close(filp, NULL);
    }
    set_fs(oldfs);
out_free_message:
    if (msg) {
        kfree(msg);
    }
    return ret;
}
#endif

int android_logger_lv(void *module, int mask)
{
    switch (mask) {
    case ATH_DEBUG_ERR:
        return 6;
    case ATH_DEBUG_INFO:
        return 4;
    case ATH_DEBUG_WARN:
        return 5; 
    case ATH_DEBUG_TRC:        
        return 3; 
    default:
#ifdef DEBUG
        if (!module) {
            return 3;
        } else if (module == &GET_ATH_MODULE_DEBUG_VAR_NAME(driver)) {
            return (mask <=ATH_DEBUG_MAKE_MODULE_MASK(3)) ? 3 : 2;
        } else if (module == &GET_ATH_MODULE_DEBUG_VAR_NAME(htc)) {
            return 2;
        } else {
            return 3;
        }
#else
        return 3; /* DEBUG */
#endif
    }
}

#ifdef RK29
int
#else
static int
#endif
android_readwrite_file(const A_CHAR *filename, A_CHAR *rbuf, const A_CHAR *wbuf, size_t length)
{
    int ret = 0;
    struct file *filp = (struct file *)-ENOENT;
    mm_segment_t oldfs;
    oldfs = get_fs();
    set_fs(KERNEL_DS);
    do {
        int mode = (wbuf) ? O_RDWR : O_RDONLY;
#ifdef TCHIP
	if (strstr(filename, "softmac") && wbuf) {
		mode = O_CREAT | O_RDWR;
	}
#endif
        filp = filp_open(filename, mode, S_IRUSR);
        if (IS_ERR(filp) || !filp->f_op) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: file %s filp_open error\n", __FUNCTION__, filename));
            ret = -ENOENT;
            break;
        }
    
        if (length==0) {
            /* Read the length of the file only */
            struct inode    *inode;

            inode = GET_INODE_FROM_FILEP(filp);
            if (!inode) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Get inode from %s failed\n", __FUNCTION__, filename));
                ret = -ENOENT;
                break;
            }
            ret = i_size_read(inode->i_mapping->host);
            break;
        }

        if (wbuf) {
            if ( (ret=filp->f_op->write(filp, wbuf, length, &filp->f_pos)) < 0) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Write %u bytes to file %s error %d\n", __FUNCTION__, 
                                length, filename, ret));
                break;
            }
        } else {
            if ( (ret=filp->f_op->read(filp, rbuf, length, &filp->f_pos)) < 0) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Read %u bytes from file %s error %d\n", __FUNCTION__,
                                length, filename, ret));
                break;
            }
        }
    } while (0);

    if (!IS_ERR(filp)) {
        filp_close(filp, NULL);
    }
    set_fs(oldfs);

    return ret;
}

#ifdef TCHIP
void android_wifi_softmac_update(char *mac_buff, int len)
{
    char filename[256];
	sprintf(filename, "%s/%s", softmac_path, "softmac");
	
	android_readwrite_file(filename, NULL, mac_buff, len);
}
#endif

int android_request_firmware(const struct firmware **firmware_p, const char *name,
                     struct device *device)
{
    int ret = 0;
    struct firmware *firmware;
    char filename[256];
    const char *raw_filename = name;
#ifdef TCHIP
    int sm = 0;
    char mac[] = "00:00:00:00:00:00";
#endif

    *firmware_p = firmware = A_MALLOC(sizeof(*firmware));
    if (!firmware) 
		return -ENOMEM;
    A_MEMZERO(firmware, sizeof(*firmware));

#ifdef TCHIP
	if(!strcmp(name,"softmac")) {
		sprintf(filename, "%s/%s", softmac_path, raw_filename);
		sm = 1;
	}
	else
#endif
	sprintf(filename, "%s/%s", fwpath, raw_filename);
    do {
        size_t length, bufsize, bmisize;

        if ( (ret=android_readwrite_file(filename, NULL, NULL, 0)) < 0) {
#ifdef TCHIP
		if (sm) {
			android_wifi_softmac_update(mac, sizeof(mac));
			length = sizeof(mac);
		}
		else
#endif
            break;
        } else {
            length = ret;
        }
    
        bufsize = ALIGN(length, PAGE_SIZE);
        bmisize = A_ROUND_UP(length, 4);
        bufsize = max(bmisize, bufsize);
        firmware->data = vmalloc(bufsize);
        firmware->size = length;
        if (!firmware->data) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s: Cannot allocate buffer for firmware\n", __FUNCTION__));
            ret = -ENOMEM;
            break;
        }
    
        if ( (ret=android_readwrite_file(filename, (char*)firmware->data, NULL, length)) != length) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s: file read error, ret %d request %d\n", __FUNCTION__, ret, length));
            ret = -1;
            break;
        }
    
    } while (0);

    if (ret<0) {
        if (firmware) {
            if (firmware->data)
                vfree(firmware->data);
            A_FREE(firmware);
        }
        *firmware_p = NULL;
    } else {
        ret = 0;
    }
    return ret;    
}

void android_release_firmware(const struct firmware *firmware)
{
	if (firmware) {
        if (firmware->data)
            vfree(firmware->data);
        A_FREE((struct firmware *)firmware);
    }
}

static A_STATUS ar6000_android_avail_ev(void *context, void *hif_handle)
{
    A_STATUS ret;    
#ifdef CONFIG_HAS_WAKELOCK
    wake_lock(&ar6k_init_wake_lock);
#endif
    ar6000_enable_mmchost_detect_change(0);
    ret = ar6000_avail_ev_p(context, hif_handle);
#ifdef CONFIG_HAS_WAKELOCK
    wake_unlock(&ar6k_init_wake_lock);
#endif
    return ret;
}

static int android_do_ioctl_direct(struct net_device *dev, int cmd, struct ifreq *ifr, void *data)
{
    int ret = -EIO;
    int  (*do_ioctl)(struct net_device *, struct ifreq *, int);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)    
    do_ioctl =  dev->do_ioctl;
#else   
    do_ioctl = dev->netdev_ops->ndo_do_ioctl;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29) */

    ifr->ifr_ifru.ifru_data = (__force void __user *)data;
    
    if (do_ioctl) {
        mm_segment_t oldfs = get_fs();
        set_fs(KERNEL_DS);
        ret = do_ioctl(dev, ifr, cmd);
        set_fs(oldfs);
    }
    return ret;
}

int android_ioctl_siwpriv(struct net_device *dev,
              struct iw_request_info *__info,
              struct iw_point *data, char *__extra)
{
    char cmd[384]; /* assume that android command will not excess 384 */
    char buf[512];
    int len = sizeof(cmd)-1;
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_STA_T *arSta = &arPriv->arSta;

    if (!data->pointer) {
        return -EOPNOTSUPP;
    }
    if (data->length < len) {
        len = data->length;
    }
    if (copy_from_user(cmd, data->pointer, len)) {
        return -EIO;
    }
    cmd[len] = 0;

    if (strcasecmp(cmd, "RSSI")==0 || strcasecmp(cmd, "RSSI-APPROX") == 0) {
        int rssi = -200;
        struct iw_statistics *iwStats;
        struct iw_statistics* (*get_iwstats)(struct net_device *);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
        get_iwstats = dev->get_wireless_stats;
#else
        get_iwstats = dev->wireless_handlers->get_wireless_stats;
#endif
        if (get_iwstats && arPriv->arConnected) {
            iwStats = get_iwstats(dev);
            if (iwStats) {
                rssi = iwStats->qual.qual;          
                if (rssi == 255)
                    rssi = -200;
                else
                    rssi += (161 - 256);                
            }
        }
        len = snprintf(buf, data->length, "SSID rssi %d\n", rssi) + 1;
        return (copy_to_user(data->pointer, buf, len)==0) ? len : -1;
    } else if (strcasecmp(cmd, "LINKSPEED")==0) {
        /* We skip to use SIOCGIWRATE since Android always asked LINKSPEED just after RSSI*/
        unsigned int speed_mbps;
        if (arPriv->arConnected) {
            speed_mbps = arPriv->arTargetStats.tx_unicast_rate / 1000;
        } else {
            speed_mbps = 1;
        }
        len = snprintf(buf, data->length, "LinkSpeed %u\n", speed_mbps) + 1;
        return (copy_to_user(data->pointer, buf, len)==0) ? len : -1;
    } else if (memcmp(cmd, "CSCAN S\x01\x00\x00S\x00", 12)==0) {
        int iocmd = SIOCSIWSCAN - SIOCSIWCOMMIT;
        const iw_handler setScan = dev->wireless_handlers->standard[iocmd];
        A_INT32 home_dwell=0, pas_dwell=0, act_dwell=0;
        A_UCHAR ssid[IW_ESSID_MAX_SIZE+1] = { 0 };       
        A_INT32 ssid_len = 0, ie_len;
        A_UINT8 index = 1; /* reserve index 0 for wext */
        A_INT32 ch = 0;
        A_CHAR nprobe, scantype;
        struct iw_freq chList[IW_MAX_FREQUENCIES];
        A_UCHAR *scanBuf = (A_UCHAR*)(cmd + 12);
        A_UCHAR *scanEnd = (A_UCHAR*)(cmd + len);
        A_BOOL broadcastSsid = FALSE;

        while ( scanBuf < scanEnd ) {
            A_UCHAR *sp = scanBuf;
            switch (*scanBuf) {
            case 'S': /* SSID section */
                if (ssid_len > 0 && index < MAX_PROBED_SSID_INDEX) {
                    /* setup the last parsed ssid, reserve index 0 for wext */
                    if (wmi_probedSsid_cmd(arPriv->arWmi, index,
                                           SPECIFIC_SSID_FLAG, ssid_len, ssid) == A_OK) {
                        ++index;
                        if (arSta->scanSpecificSsid<index) {
                            arSta->scanSpecificSsid = index;
                        }
                    }
                }
                ie_len = ((scanBuf + 1) < scanEnd) ? ((A_INT32)*(scanBuf+1) + 1) : 0;
                if ((scanBuf+ie_len) < scanEnd ) {
                    ssid_len = *(scanBuf+1);
                    if (ssid_len == 0) {
                        broadcastSsid = TRUE;
                    } else {
                        A_MEMCPY(ssid, scanBuf+2, ssid_len);
                        ssid[ssid_len] = '\0';
                    }
                }
                scanBuf += 1 + ie_len;
                break;
            case 'C': /* Channel section */
                if (scanBuf+1 < scanEnd) {
                    int value = *(scanBuf+1);
                    if (value == 0) {
                        ch = 0; /* scan for all channels */
                    } else if (ch < IW_MAX_FREQUENCIES) {
                        if (value>1000) {
                            chList[ch].e = 1;
                            chList[ch].m = value * 100000;
                        } else {
                            chList[ch].e = 0;
                            chList[ch].m = value;
                        }
                        ++ch;
                    }
                }
                scanBuf += 2;
                break;
            case 'P': /* Passive dwell section */
                if (scanBuf+2 < scanEnd) {
                    pas_dwell = *(scanBuf+1) + (*(scanBuf+2) << 8);
                }
                scanBuf += 3;
                break;
            case 'H': /* Home dwell section */
                if (scanBuf+2 < scanEnd) {
                    home_dwell = *(scanBuf+1) + (*(scanBuf+2) << 8);
                }
                scanBuf += 3;
                break;
            case 'N': /* Number of probe section */
                if (scanBuf+1 < scanEnd) {
                    nprobe = *(scanBuf+1);
                }
                scanBuf += 2;
                break;
            case 'A': /* Active dwell section */
                if (scanBuf+2 < scanEnd) {
                    act_dwell = *(scanBuf+1) + (*(scanBuf+2) << 8);
                }
                scanBuf += 3;
                break;
            case 'T': /* Scan active type section */
                if (scanBuf+1 < scanEnd) {
                    scantype = *(scanBuf+1);
                }
                scanBuf += 2;
                break;
            default:
                break;
            }
            if (sp == scanBuf) {
                return -1; /* parsing error */
            }
        }

        if (ssid_len>0) {
            A_UINT8 idx; /* Clean up the last specific scan items */
            for (idx=index; idx<arSta->scanSpecificSsid; ++idx) {
                wmi_probedSsid_cmd(arPriv->arWmi, idx, DISABLE_SSID_FLAG, 0, NULL);
            }
            arSta->scanSpecificSsid = index;
            /* 
             * There is no way to know when we need to send broadcast probe in current Android wpa_supplicant_6 
             * combo scan implemenation. Always force to sent it here uniti future Android version will set
             * the broadcast flags for combo scan.
             */
#if 0
            if (broadcastSsid)
#endif
            {
                /* setup the last index as broadcast SSID for combo scan */
                ++arSta->scanSpecificSsid;
                wmi_probedSsid_cmd(arPriv->arWmi, index, ANY_SSID_FLAG, 0, NULL);
            }
        }

        if (pas_dwell>0) {
            /* TODO: Should we change our passive dwell? There may be some impact for bt-coex */
        }

        if (home_dwell>0) {
            /* TODO: Should we adjust home_dwell? How to pass it to wext handler? */
        }

        if (setScan) {
            union iwreq_data miwr;
            struct iw_request_info minfo;
            struct iw_scan_req scanreq, *pScanReq = NULL;
            A_MEMZERO(&minfo, sizeof(minfo));
            A_MEMZERO(&miwr, sizeof(miwr));            
            A_MEMZERO(&scanreq, sizeof(scanreq));
            if (ssid_len > 0) {
                pScanReq = &scanreq;
                memcpy(scanreq.essid, ssid, ssid_len);
                scanreq.essid_len = ssid_len;
                miwr.data.flags |= IW_SCAN_THIS_ESSID;
            }
            if (ch > 0) {
                pScanReq = &scanreq;
                scanreq.num_channels = ch;
                memcpy(scanreq.channel_list, chList, ch * sizeof(chList[0]));
                miwr.data.flags |= IW_SCAN_THIS_FREQ;
            }
            if (pScanReq) {
                miwr.data.pointer = (__force void __user *)&scanreq;
                miwr.data.length = sizeof(scanreq);
            }
            minfo.cmd = SIOCSIWSCAN;
            return setScan(dev, &minfo, &miwr, (char*)pScanReq);
        }
        return -1;
    } else if (strcasecmp(cmd, "MACADDR")==0) {
        /* reply comes back in the form "Macaddr = XX:XX:XX:XX:XX:XX" where XX */
        A_UCHAR *mac = dev->dev_addr;
        len = snprintf(buf, data->length, "Macaddr = %02X:%02X:%02X:%02X:%02X:%02X\n",
                        mac[0], mac[1], mac[2],
                        mac[3], mac[4], mac[5]) + 1;
        return (copy_to_user(data->pointer, buf, len)==0) ? len : -1;
    } else if (strcasecmp(cmd, "SCAN-ACTIVE")==0) {
        return 0; /* unsupport function. Suppress the error */
    } else if (strcasecmp(cmd, "SCAN-PASSIVE")==0) {
        return 0; /* unsupport function. Suppress the error */
    } else if (strcasecmp(cmd, "START")==0 || strcasecmp(cmd, "STOP")==0) {
        struct ifreq ifr;
        char userBuf[16];
        int ex_arg = (strcasecmp(cmd, "START")==0) ? WLAN_ENABLED : WLAN_DISABLED;
        int ret;
        A_MEMZERO(userBuf, sizeof(userBuf));
        ((int *)userBuf)[0] = AR6000_XIOCTRL_WMI_SET_WLAN_STATE;
        ((int *)userBuf)[1] = ex_arg;
        ret = android_do_ioctl_direct(dev, AR6000_IOCTL_EXTENDED, &ifr, userBuf);
        if (ret==0) {
            /* Send wireless event which need by android supplicant */
            union iwreq_data wrqu;
            A_MEMZERO(&wrqu, sizeof(wrqu));
            wrqu.data.length = strlen(cmd);
            wireless_send_event(dev, IWEVCUSTOM, &wrqu, cmd);
        }
        return ret;
    } else if (strncasecmp(cmd, "POWERMODE ", 10)==0) {
        int mode;
        if (sscanf(cmd, "%*s %d", &mode) == 1) {
            int iocmd = SIOCSIWPOWER - SIOCSIWCOMMIT;
            iw_handler setPower = dev->wireless_handlers->standard[iocmd];
            if (setPower) {
                union iwreq_data miwr;
                struct iw_request_info minfo;
                A_MEMZERO(&minfo, sizeof(minfo));
                A_MEMZERO(&miwr, sizeof(miwr));
                minfo.cmd = SIOCSIWPOWER;
                if (mode == 0 /* auto */)
                    miwr.power.disabled = 0;
                else if (mode == 1 /* active */)
                    miwr.power.disabled = 1;
                else
                    return -1;
                return setPower(dev, &minfo, &miwr, NULL);
            }
        }
        return -1;
    } else if (strcasecmp(cmd, "GETPOWER")==0) {
        struct ifreq ifr;
        int userBuf[2];
        A_MEMZERO(userBuf, sizeof(userBuf));
        ((int *)userBuf)[0] = AR6000_XIOCTRL_WMI_GET_POWER_MODE;
        if (android_do_ioctl_direct(dev, AR6000_IOCTL_EXTENDED, &ifr, userBuf)>=0) {
            WMI_POWER_MODE_CMD *getPowerMode = (WMI_POWER_MODE_CMD *)userBuf;
            len = snprintf(buf, data->length, "powermode = %u\n", 
                           (getPowerMode->powerMode==MAX_PERF_POWER) ? 1/*active*/ : 0/*auto*/) + 1;
            return (copy_to_user(data->pointer, buf, len)==0) ? len : -1;        
        }
        return -1;
    } else if (strncasecmp(cmd, "SETSUSPENDOPT ", 14)==0) {
        int enable;
        if (sscanf(cmd, "%*s %d", &enable)==1) {
            /* 
             * We set our suspend mode by wlan_config.h now. 
             * Should we follow Android command?? TODO
             */
            return 0;
        }
        return -1;
    } else if (strcasecmp(cmd, "SCAN-CHANNELS")==0) {
        // reply comes back in the form "Scan-Channels = X" where X is the number of channels        
        int iocmd = SIOCGIWRANGE - SIOCSIWCOMMIT;
        iw_handler getRange = dev->wireless_handlers->standard[iocmd];            
        if (getRange) {
            union iwreq_data miwr;
            struct iw_request_info minfo;
            struct iw_range range;
            A_MEMZERO(&minfo, sizeof(minfo));
            A_MEMZERO(&miwr, sizeof(miwr));
            A_MEMZERO(&range, sizeof(range));
            minfo.cmd = SIOCGIWRANGE;
            miwr.data.pointer = (__force void __user *) &range;
            miwr.data.length = sizeof(range);
            getRange(dev, &minfo, &miwr, (char*)&range);
        }
        if (arSta->arNumChannels!=-1) {
            len = snprintf(buf, data->length, "Scan-Channels = %d\n", arSta->arNumChannels) + 1;
            return (copy_to_user(data->pointer, buf, len)==0) ? len : -1;
        }
        return -1;
    } else if (strncasecmp(cmd, "SCAN-CHANNELS ", 14)==0 || 
               strncasecmp(cmd, "COUNTRY ", 8)==0) {
        /* 
         * Set the available channels with WMI_SET_CHANNELPARAMS cmd
         * However, the channels will be limited by the eeprom regulator domain
         * Try to use a regulator domain which will not limited the channels range.
         */
        int i;
        int chan = 0;
        A_UINT16 *clist;
        struct ifreq ifr; 
        char ioBuf[256];
        WMI_CHANNEL_PARAMS_CMD *chParamCmd = (WMI_CHANNEL_PARAMS_CMD *)ioBuf;
        if (strncasecmp(cmd, "COUNTRY ", 8)==0) {
            char *country = cmd + 8;
            if (strcasecmp(country, "US")==0) {
                chan = 11;
            } else if (strcasecmp(country, "JP")==0) {
                chan = 14;
            } else if (strcasecmp(country, "EU")==0) {
                chan = 13;
            }
        } else if (sscanf(cmd, "%*s %d", &chan) != 1) {
            return -1;
        }
        if ( (chan != 11) && (chan != 13) && (chan != 14)) {
            return -1;
        }
        if (arPriv->arNextMode == AP_NETWORK) {
            return -1;
        }
        A_MEMZERO(&ifr, sizeof(ifr));
        A_MEMZERO(ioBuf, sizeof(ioBuf));

        chParamCmd->phyMode = WMI_11G_MODE;
        clist = chParamCmd->channelList;
        chParamCmd->numChannels = chan;
        chParamCmd->scanParam = 1;        
        for (i = 0; i < chan; i++) {
            clist[i] = wlan_ieee2freq(i + 1);
        }
        
        return android_do_ioctl_direct(dev, AR6000_IOCTL_WMI_SET_CHANNELPARAMS, &ifr, ioBuf);
    } else if (strncasecmp(cmd, "BTCOEXMODE ", 11)==0) {
        int mode;
        if (sscanf(cmd, "%*s %d", &mode)==1) {
            /* 
            * Android disable BT-COEX when obtaining dhcp packet except there is headset is connected 
             * It enable the BT-COEX after dhcp process is finished
             * We ignore since we have our way to do bt-coex during dhcp obtaining.
             */
            switch (mode) {
            case 1: /* Disable*/
                break;
            case 0: /* Enable */
                /* fall through */
            case 2: /* Sense*/
                /* fall through */
            default:
                break;
            }
            return 0; /* ignore it */
        }
        return -1;
    } else if (strcasecmp(cmd, "BTCOEXSCAN-START")==0) {
        /* Android enable or disable Bluetooth coexistence scan mode. When this mode is on,
         * some of the low-level scan parameters used by the driver are changed to
         * reduce interference with A2DP streaming.
         */
        return 0; /* ignore it since we have btfilter  */
    } else if (strcasecmp(cmd, "BTCOEXSCAN-STOP")==0) {
        return 0; /* ignore it since we have btfilter  */
    } else if (strncasecmp(cmd, "RXFILTER-ADD ", 13)==0) {
        return 0; /* ignore it */
    } else if (strncasecmp(cmd, "RXFILTER-REMOVE ", 16)==0) {
        return 0; /* ignoret it */
    } else if (strcasecmp(cmd, "RXFILTER-START")==0 || strcasecmp(cmd, "RXFILTER-STOP")==0) {
        unsigned int flags = dev->flags;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34)
        int mc_count = dev->mc_count;   
#else
        int mc_count = netdev_mc_count(dev);
#endif
        if (strcasecmp(cmd, "RXFILTER-START")==0) {
            if (mc_count > 0 || (flags & IFF_MULTICAST) ) {
                flags &= ~IFF_MULTICAST;
            }
        } else {
            flags |= IFF_MULTICAST;
        }
        if (flags != dev->flags) {
            dev_change_flags(dev, flags);
        }
        return 0;
    }

    return -EOPNOTSUPP;
}

/* Useful for qualcom platform to detect our wlan card for mmc stack */
static void ar6000_enable_mmchost_detect_change(int enable)
{
#ifdef CONFIG_MMC_MSM
#define MMC_MSM_DEV "msm_sdcc.1"
    char buf[3];
    int length;

    if (!enable_mmc_host_detect_change) {
        return;
    }
    length = snprintf(buf, sizeof(buf), "%d\n", enable ? 1 : 0);
    if (android_readwrite_file("/sys/devices/platform/" MMC_MSM_DEV "/detect_change", 
                               NULL, buf, length) < 0) {
        /* fall back to polling */
        android_readwrite_file("/sys/devices/platform/" MMC_MSM_DEV "/polling", NULL, buf, length);
        A_MDELAY(50);
    }
#endif
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void android_early_suspend(struct early_suspend *h)
{
    screen_is_off = 1;
}

static void android_late_resume(struct early_suspend *h)
{
    screen_is_off = 0;
}
#endif

void android_module_init(OSDRV_CALLBACKS *osdrvCallbacks)
{
    bmienable = 1;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    if (ifname[0] == '\0')
        strcpy(ifname, def_ifname);
#endif 
#ifdef CONFIG_HAS_WAKELOCK
    wake_lock_init(&ar6k_init_wake_lock, WAKE_LOCK_SUSPEND, "ar6k_init");
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
    ar6k_early_suspend.suspend = android_early_suspend;
    ar6k_early_suspend.resume  = android_late_resume;
    ar6k_early_suspend.level   = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
    register_early_suspend(&ar6k_early_suspend);
#endif

    ar6000_avail_ev_p = osdrvCallbacks->deviceInsertedHandler;
    osdrvCallbacks->deviceInsertedHandler = ar6000_android_avail_ev;

    ar6000_enable_mmchost_detect_change(1);
}

void android_module_exit(void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&ar6k_early_suspend);
#endif
#ifdef CONFIG_HAS_WAKELOCK
    wake_lock_destroy(&ar6k_init_wake_lock);
#endif
    ar6000_enable_mmchost_detect_change(1);
    /* disable polling again after we remove our wlan card */
    ar6000_enable_mmchost_detect_change(0);
}

#ifdef CONFIG_PM
void android_ar6k_check_wow_status(AR_SOFTC_T *ar, struct sk_buff *skb, A_BOOL isEvent)
{
#ifdef CONFIG_HAS_WAKELOCK
    unsigned long wake_timeout = HZ; /* 1 second for normal window's ping test */
#endif
    AR_SOFTC_DEV_T *arPriv;
    A_UINT8  i; 
    A_BOOL needWake = FALSE;
    for(i = 0; i < num_device; i++) 
    {
        arPriv = ar->arDev[i];
        if (
#ifdef CONFIG_HAS_EARLYSUSPEND
            screen_is_off && 
#endif
                skb && arPriv->arConnected) {
            if (isEvent) {
                if (A_NETBUF_LEN(skb) >= sizeof(A_UINT16)) {
                    A_UINT16 cmd = *(const A_UINT16 *)A_NETBUF_DATA(skb);
                    switch (cmd) {
                    case WMI_CONNECT_EVENTID:
#ifdef CONFIG_HAS_WAKELOCK
                         wake_timeout = 3*HZ;
#endif
                         needWake = TRUE;
                         break;
                    default:
                        /* dont wake lock the system for other event */
                         break;
                    }
                }
            } else if (A_NETBUF_LEN(skb) >= sizeof(ATH_MAC_HDR)) {
                ATH_MAC_HDR *datap = (ATH_MAC_HDR *)A_NETBUF_DATA(skb);
                if (!IEEE80211_IS_MULTICAST(datap->dstMac)) {
                    switch (A_BE2CPU16(datap->typeOrLen)) {
                    case 0x0800: /* IP */
                        if (A_NETBUF_LEN(skb)>=24 &&
                            *((A_UCHAR*)A_NETBUF_DATA(skb)+23)==0x11) {
                            A_UCHAR *udpPkt = (A_UCHAR*)A_NETBUF_DATA(skb)+14;
                            A_UINT8 ihl = (*udpPkt & 0x0f) * sizeof(A_UINT32);
                            const A_UCHAR ipsec_keepalive[] = { 
                                0x11, 0x94, 0x11, 0x94, 0x00, 0x09, 0x00, 0x00, 0xff
                            };
                            udpPkt += ihl;
                            if (A_NETBUF_LEN(skb)>=14+ihl+sizeof(ipsec_keepalive) &&
                                    !memcmp(udpPkt, ipsec_keepalive, sizeof(ipsec_keepalive)-3) &&
                                    udpPkt[8]==0xff) {
                                /* 
                                 * RFC 3948 UDP Encapsulation of IPsec ESP Packets
                                 * Source and Destination port must be 4500
                                 * Receivers MUST NOT depend upon the UDP checksum being zero 
                                 * Sender must use 1 byte payload with 0xff
                                 * Receiver SHOULD ignore a received NAT-keepalive packet
                                 *
                                 * IPSec over UDP NAT keepalive packet. Just ignore
                                 */
                                break;
                            }
                        }
                    case 0x888e: /* EAPOL */
                    case 0x88c7: /* RSN_PREAUTH */
                    case 0x88b4: /* WAPI */
                         needWake = TRUE;
                         break;
                    case 0x0806: /* ARP is not important to hold wake lock */
                        needWake = (arPriv->arNetworkType==AP_NETWORK);
                        break;
                    default:
                         break;
                    }
                } else if ( !IEEE80211_IS_BROADCAST(datap->dstMac) ) {
                    if (A_NETBUF_LEN(skb)>=14+20 ) {
					    /* check if it is mDNS packets */
                        A_UINT8 *dstIpAddr = (A_UINT8*)(A_NETBUF_DATA(skb)+14+20-4);                    
                        struct net_device *ndev = arPriv->arNetDev;
                        needWake = ((dstIpAddr[3] & 0xf8) == 0xf8) &&
                                (arPriv->arNetworkType==AP_NETWORK || 
                                (ndev->flags & IFF_ALLMULTI || ndev->flags & IFF_MULTICAST));
                    }
                }else if (arPriv->arNetworkType==AP_NETWORK) {
                    switch (A_BE2CPU16(datap->typeOrLen)) {
                    case 0x0800: /* IP */
                        if (A_NETBUF_LEN(skb)>=14+20+2) {
                            A_UINT16 dstPort = *(A_UINT16*)(A_NETBUF_DATA(skb)+14+20);
                            dstPort = A_BE2CPU16(dstPort);
                            needWake = (dstPort == 0x43); /* dhcp request */
                        }
                        break;
                    case 0x0806: 
                        needWake = TRUE;
                    default:
                        break;
                    }
                }
             }
         }
    }
    if (needWake) {
#ifdef CONFIG_HAS_WAKELOCK
        /* keep host wake up if there is any event and packate comming in*/
        wake_lock_timeout(&ar6k_wow_wake_lock, wake_timeout);
#endif
        if (wowledon) {
            char buf[32];
            int len = sprintf(buf, "on");
            android_readwrite_file("/sys/power/state", NULL, buf, len);

            len = sprintf(buf, "%d", 127);
            android_readwrite_file("/sys/class/leds/lcd-backlight/brightness",
                                   NULL, buf,len);
        }
    }
}
#endif /* CONFIG_PM */

void android_send_reload_event(AR_SOFTC_DEV_T *arPriv)
{
    struct net_device *ndev = arPriv->arNetDev;
    union iwreq_data wrqu;
    const char reloadEvt[] = "HANG";
    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = strlen(reloadEvt);
    wireless_send_event(ndev, IWEVCUSTOM, &wrqu, reloadEvt);
}
