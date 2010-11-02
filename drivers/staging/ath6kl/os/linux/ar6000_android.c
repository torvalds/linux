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

A_BOOL enable_mmc_host_detect_change = 0;
static void ar6000_enable_mmchost_detect_change(int enable);


char fwpath[256] = "/system/wifi";
int wowledon;
unsigned int enablelogcat;

extern int bmienable;
extern struct net_device *ar6000_devices[];
extern char ifname[];

#ifdef CONFIG_HAS_WAKELOCK
extern struct wake_lock ar6k_wow_wake_lock;
struct wake_lock ar6k_init_wake_lock;
#endif

const char def_ifname[] = "wlan0";
module_param_string(fwpath, fwpath, sizeof(fwpath), 0644);
module_param(enablelogcat, uint, 0644);
module_param(wowledon, int, 0644);

#ifdef CONFIG_HAS_EARLYSUSPEND
static int screen_is_off;
static struct early_suspend ar6k_early_suspend;
#endif

static A_STATUS (*ar6000_avail_ev_p)(void *, void *);

#if defined(CONFIG_ANDROID_LOGGER) && (!defined(CONFIG_MMC_MSM))
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

static int android_readwrite_file(const A_CHAR *filename, A_CHAR *rbuf, const A_CHAR *wbuf, size_t length)
{
    int ret = 0;
    struct file *filp = (struct file *)-ENOENT;
    mm_segment_t oldfs;
    oldfs = get_fs();
    set_fs(KERNEL_DS);
    do {
        int mode = (wbuf) ? O_RDWR : O_RDONLY;
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

int android_request_firmware(const struct firmware **firmware_p, const char *name,
                     struct device *device)
{
    int ret = 0;
    struct firmware *firmware;
    char filename[256];
    const char *raw_filename = name;
	*firmware_p = firmware = kzalloc(sizeof(*firmware), GFP_KERNEL);
    if (!firmware) 
		return -ENOMEM;
	sprintf(filename, "%s/%s", fwpath, raw_filename);
    do {
        size_t length, bufsize, bmisize;

        if ( (ret=android_readwrite_file(filename, NULL, NULL, 0)) < 0) {
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
            kfree(firmware);
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
        kfree(firmware);
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
    if (ifname[0] == '\0')
        strcpy(ifname, def_ifname);
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
}

#ifdef CONFIG_PM
void android_ar6k_check_wow_status(AR_SOFTC_T *ar, struct sk_buff *skb, A_BOOL isEvent)
{
    if (
#ifdef CONFIG_HAS_EARLYSUSPEND
        screen_is_off && 
#endif 
            skb && ar->arConnected) {
        A_BOOL needWake = FALSE;
        if (isEvent) {
            if (A_NETBUF_LEN(skb) >= sizeof(A_UINT16)) {
                A_UINT16 cmd = *(const A_UINT16 *)A_NETBUF_DATA(skb);
                switch (cmd) {
                case WMI_CONNECT_EVENTID:
                case WMI_DISCONNECT_EVENTID:
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
                case 0x888e: /* EAPOL */
                case 0x88c7: /* RSN_PREAUTH */
                case 0x88b4: /* WAPI */
                     needWake = TRUE;
                     break;
                case 0x0806: /* ARP is not important to hold wake lock */
                default:
                    break;
                }
            }
        }
        if (needWake) {
            /* keep host wake up if there is any event and packate comming in*/
#ifdef CONFIG_HAS_WAKELOCK
            wake_lock_timeout(&ar6k_wow_wake_lock, 3*HZ);
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
}
#endif /* CONFIG_PM */
