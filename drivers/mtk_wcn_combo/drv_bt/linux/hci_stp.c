/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein is
 * confidential and proprietary to MediaTek Inc. and/or its licensors. Without
 * the prior written permission of MediaTek inc. and/or its licensors, any
 * reproduction, modification, use or disclosure of MediaTek Software, and
 * information contained herein, in whole or in part, shall be strictly
 * prohibited.
 * 
 * MediaTek Inc. (C) 2010. All rights reserved.
 * 
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER
 * ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL
 * WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR
 * NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH
 * RESPECT TO THE SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY,
 * INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES
 * TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO.
 * RECEIVER EXPRESSLY ACKNOWLEDGES THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO
 * OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES CONTAINED IN MEDIATEK
 * SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE
 * RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S
 * ENTIRE AND CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE
 * RELEASED HEREUNDER WILL BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE
 * MEDIATEK SOFTWARE AT ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE
 * CHARGE PAID BY RECEIVER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek
 * Software") have been modified by MediaTek Inc. All revisions are subject to
 * any receiver's applicable license agreements with MediaTek Inc.
 */


#include "hci_stp.h"
#include "bt_conf.h"
#include "stp_exp.h"
#include "wmt_exp.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/* Debugging Purpose */
#define PFX "[HCI-STP]"
#define BT_LOG_LOUD (4)
#define BT_LOG_DBG (3)
#define BT_LOG_INFO (2)
#define BT_LOG_WARN (1)
#define BT_LOG_ERR (0)

#define VERSION "2.0"

/* H4 receiver States */
#define H4_W4_PACKET_TYPE (0)
#define H4_W4_EVENT_HDR (1)
#define H4_W4_ACL_HDR (2)
#define H4_W4_SCO_HDR (3)
#define H4_W4_DATA (4)

#define HCI_STP_TXQ_IN_BLZ (0)
/* access txq in BlueZ tx tasklet context */
#define HCI_STP_TXQ_IN_HCISTP (1)
/* access txq in HCI-STP context,  defined by compile flag: HCI_STP_TX */

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/
unsigned int gDbgLevel = BT_LOG_INFO;

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/
/* Allow one BT driver */
static struct hci_dev *hdev = NULL;
static int reset = 0;

/* maybe struct hci_stp is a better place to put these data */
#if (HCI_STP_TX == HCI_STP_TX_TASKLET)
static struct tasklet_struct hci_tx_tasklet;

#if (HCI_STP_TX_TASKLET_LOCK == HCI_STP_TX_TASKLET_RWLOCK)
static DEFINE_RWLOCK(hci_stp_txqlock);

#elif (HCI_STP_TX_TASKLET_LOCK == HCI_STP_TX_TASKLET_SPINLOCK)
static spinlock_t hci_stp_txqlock;

#endif
#endif

#if (HCI_STP_TX == HCI_STP_TX_THRD)
static spinlock_t hci_stp_txqlock;
struct task_struct * hci_stp_tx_thrd = NULL;
wait_queue_head_t hci_stp_tx_thrd_wq;
#endif


#define CUSTOM_BT_CFG_FILE          "/data/BT.cfg"
#define INTERNAL_BT_CFG_FILE        "/data/bluetooth/BT.cfg"

static bool fgetEFUSE = false;

static unsigned char bt_get_bd_addr[4] =
    {0x01, 0x09, 0x10, 0x00};
static unsigned char bt_get_bd_addr_evt[] =
    {0x04, 0x0E, 0x0A, 0x01, 0x09, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static unsigned char bt_set_bd_addr[10] =
    {0x01, 0x1A, 0xFC, 0x06, 0x01, 0x20, 0x66, 0x46, 0x00, 0x00};
static unsigned char bt_set_bd_addr_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x1A, 0xFC, 0x00};
static unsigned char bt_set_link_key_type[5]=
    {0x01, 0x1B, 0xFC, 0x01, 0x01};
static unsigned char bt_set_link_key_type_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x1B, 0xFC, 0x00};
static unsigned char bt_set_unit_key[20] =
    {0x01, 0x75, 0xFC, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static unsigned char bt_set_unit_key_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x75, 0xFC, 0x00};
static unsigned char bt_set_encrypt[7] =
    {0x01, 0x76, 0xFC, 0x03, 0x00, 0x02, 0x10};
static unsigned char bt_set_encrypt_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x76, 0xFC, 0x00};
static unsigned char bt_set_pin_code_type[5] =
    {0x01, 0x0A, 0x0C, 0x01, 0x00};
static unsigned char bt_set_pin_code_type_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x0A, 0x0C, 0x00};
static unsigned char bt_set_voice[6] =
    {0x01, 0x26, 0x0C, 0x02, 0x60, 0x00};
static unsigned char bt_set_voice_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x26, 0x0C, 0x00};
static unsigned char bt_set_codec[8] =
    {0x01, 0x72, 0xFC, 0x04, 0x23, 0x10, 0x00, 0x00};
static unsigned char bt_set_codec_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x72, 0xFC, 0x00};
static unsigned char bt_set_radio[10] =
    {0x01, 0x79, 0xFC, 0x06, 0x06, 0x80, 0x00, 0x06, 0x03, 0x06};
static unsigned char bt_set_radio_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x79, 0xFC, 0x00};
static unsigned char bt_set_tx_pwr_offset[7] =
    {0x01, 0x93, 0xFC, 0x03, 0xFF, 0xFF, 0xFF};
static unsigned char bt_set_tx_pwr_offset_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x93, 0xFC, 0x00};
static unsigned char bt_set_sleep[11] =
    {0x01, 0x7A, 0xFC, 0x07, 0x03, 0x40, 0x1F, 0x40, 0x1F, 0x00, 0x04};
static unsigned char bt_set_sleep_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x7A, 0xFC, 0x00};
static unsigned char bt_set_feature[6] =
    {0x01, 0x7D, 0xFC, 0x02, 0x80, 0x0};
static unsigned char bt_set_feature_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x7D, 0xFC, 0x00};
static unsigned char bt_set_OSC[9] =
    {0x01, 0x7B, 0xFC, 0x05, 0x01, 0x01, 0x14, 0x0A, 0x05};
static unsigned char bt_set_OSC_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x7B, 0xFC, 0x00};
static unsigned char bt_set_LPO[14] =
    {0x01, 0x7C, 0xFC, 0x0A, 0x01, 0xFA, 0x0A, 0x02, 0x00, 0xA6, 0x0E, 0x00, 0x40, 0x00};
static unsigned char bt_set_LPO_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x7C, 0xFC, 0x00};
static unsigned char bt_set_legacy_PTA[14] =
    {0x01, 0x74, 0xFC, 0x0A, 0xC9, 0x8B, 0xBF, 0x00, 0x00, 0x52, 0x0E, 0x0E, 0x1F, 0x1B};
static unsigned char bt_set_legacy_PTA_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x74, 0xFC, 0x00};
static unsigned char bt_set_BLE_PTA[9] =
    {0x01, 0xFC, 0xFC, 0x05, 0x16, 0x0E, 0x0E, 0x00, 0x07};
static unsigned char bt_set_BLE_PTA_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0xFC, 0xFC, 0x00};
static unsigned char bt_set_RF_desence[10] =
    {0x01, 0x20, 0xFC, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
static unsigned char bt_set_RF_desence_evt[] =
    {0x04, 0x0e, 0x04, 0x01, 0x20, 0xFC, 0x00};
static unsigned char bt_reset[4] =
    {0x01, 0x03, 0x0C, 0x0};
static unsigned char bt_reset_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00};
static unsigned char bt_set_intern_PTA_1[19] =
    {0x01, 0xFB, 0xFC, 0x0F, 0x00, 0x01, 0x0F, 0x0F, 0x01, 0x0F, 0x0F, 0x01, 0x0F, 0x0F, 0x01, 0x0F, 0x0F, 0x02, 0x01};
static unsigned char bt_set_intern_PTA_1_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0xFB, 0xFC, 0x00};
static unsigned char bt_set_intern_PTA_2[11] =
    {0x01, 0xFB, 0xFC, 0x07, 0x01, 0x19, 0x19, 0x07, 0xD0, 0x00, 0x01};
static unsigned char bt_set_intern_PTA_2_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0xFB, 0xFC, 0x00};
static unsigned char bt_set_SLP_control_reg[12] =
    {0x01, 0xD0, 0xFC, 0x08, 0x74, 0x00, 0x01, 0x81, 0xE2, 0x29, 0x0, 0x0};
static unsigned char bt_set_SLP_control_reg_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0xD0, 0xFC, 0x00};
static unsigned char bt_set_SLP_LDOD_reg[12] =
    {0x01, 0xD0, 0xFC, 0x08, 0x1C, 0x00, 0x02, 0x81, 0x79, 0x08, 0x0, 0x0};
static unsigned char bt_set_SLP_LDOD_reg_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0xD0, 0xFC, 0x00};
static unsigned char bt_set_RF_reg_100[10] =
    {0x01, 0xB0, 0xFC, 0x06, 0x64, 0x01, 0x02, 0x00, 0x00, 0x00};
static unsigned char bt_set_RF_reg_100_evt[] =
    {0x04, 0x0E, 0x04, 0x01, 0xB0, 0xFC, 0x00};

/* Do init commands in sequence, cmd and cmd##_evt */
static struct hci_stp_init_cmd init_table[] =
{
    hci_stp_init_entry(bt_get_bd_addr),
    hci_stp_init_entry(bt_set_bd_addr),
    hci_stp_init_entry(bt_set_link_key_type),
    hci_stp_init_entry(bt_set_unit_key),
    hci_stp_init_entry(bt_set_encrypt),
    hci_stp_init_entry(bt_set_pin_code_type),
    hci_stp_init_entry(bt_set_voice),
    hci_stp_init_entry(bt_set_codec),
    hci_stp_init_entry(bt_set_radio),
    hci_stp_init_entry(bt_set_tx_pwr_offset),
    hci_stp_init_entry(bt_set_sleep),
    hci_stp_init_entry(bt_set_feature),
    hci_stp_init_entry(bt_set_OSC),
    hci_stp_init_entry(bt_set_LPO),
    hci_stp_init_entry(bt_set_legacy_PTA),
    hci_stp_init_entry(bt_set_BLE_PTA),
    hci_stp_init_entry(bt_set_RF_desence),
    hci_stp_init_entry(bt_reset),
    hci_stp_init_entry(bt_set_intern_PTA_1),
    hci_stp_init_entry(bt_set_intern_PTA_2),
    hci_stp_init_entry(bt_set_SLP_control_reg),
    hci_stp_init_entry(bt_set_SLP_LDOD_reg),
    hci_stp_init_entry(bt_set_RF_reg_100),
};

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#define BT_LOUD_FUNC(fmt, arg...)   if(gDbgLevel >= BT_LOG_LOUD){printk(PFX "[L]%s:"  fmt, __FUNCTION__ ,##arg);}
#define BT_DBG_FUNC(fmt, arg...)    if(gDbgLevel >= BT_LOG_DBG){printk(PFX "[D]%s:"  fmt, __FUNCTION__ ,##arg);}
#define BT_INFO_FUNC(fmt, arg...)   if(gDbgLevel >= BT_LOG_INFO){printk(PFX "[I]%s:"  fmt, __FUNCTION__ ,##arg);}
#define BT_WARN_FUNC(fmt, arg...)   if(gDbgLevel >= BT_LOG_WARN){printk(PFX "[W]%s:"  fmt, __FUNCTION__ ,##arg);}
#define BT_ERR_FUNC(fmt, arg...)    if(gDbgLevel >= BT_LOG_ERR){printk(PFX "[E]%s:"   fmt, __FUNCTION__ ,##arg);}
#define BT_TRC_FUNC(f)              if(gDbgLevel >= BT_LOG_LOUD){printk(PFX "[T]%s:%d\n", __FUNCTION__, __LINE__);}

#if HCI_STP_SAFE_RESET
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
/* HCI_RESET bit definition in linux/include/net/bluetooth/hci.h since 2.6.39:
    http://lxr.free-electrons.com/source/include/net/bluetooth/hci.h?v=2.6.39;a=arm
*/
#define BT_GET_HDEV_RST_FG(hdev) (test_bit(HCI_RESET, &hdev->flags))
#else
#define BT_GET_HDEV_RST_FG(hdev) (0) /* no HCI_RESET bit available */
#endif
#endif

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
/* Functions to be implemted by all HCI_STP_TX_* methods */
void hci_stp_tx_init (struct hci_stp *hu);
void hci_stp_tx_deinit (struct hci_stp *hu);
void hci_stp_txq_lock (unsigned int ctx);
void hci_stp_txq_unlock (unsigned int ctx);
void hci_stp_tx_kick (void);

/* Functions to be implemented by all HCI_STP_INIT_* methods*/
static int hci_stp_dev_init (struct hci_stp *phu);

#if (HCI_STP_TX == HCI_STP_TX_TASKLET)
static int hci_stp_tx_wakeup (struct hci_stp *hu);
static void hci_stp_tx_tasklet_func (unsigned long data);
#endif

#if (HCI_STP_TX == HCI_STP_TX_THRD)
static int hci_stp_tx_thrd_func (void *pdata);
#endif

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

static ssize_t file_read(char *filename, char *buf, size_t len, loff_t *offset)
{
    struct file *fp;
    mm_segment_t old_fs;
    ssize_t retLen;

    fp = filp_open(filename, O_RDONLY, 0);
    if (IS_ERR(fp)) {
        BT_WARN_FUNC("Failed to open %s!\n", filename);
        return -1;
    }

    old_fs = get_fs();
    set_fs(KERNEL_DS);

    if ((fp->f_op == NULL) || (fp->f_op->read == NULL)){
        BT_WARN_FUNC("File can not be read!\n");
        set_fs(old_fs);
        filp_close(fp, NULL);
        return -1;
    }

    retLen = fp->f_op->read(fp, buf, len, offset);

    set_fs(old_fs);
    filp_close(fp, NULL);

    return retLen;
}

static ssize_t file_write(char *filename, char *buf, size_t len, loff_t *offset)
{
    struct file *fp;
    mm_segment_t old_fs;
    ssize_t retLen;

    fp = filp_open(filename, O_WRONLY | O_CREAT, 0644);
    if (IS_ERR(fp)) {
        BT_WARN_FUNC("Failed to open %s!\n", filename);
        return -1;
    }

    old_fs = get_fs();
    set_fs(KERNEL_DS);

    if ((fp->f_op == NULL) || (fp->f_op->write == NULL)){
        BT_WARN_FUNC("File can not be write!\n");
        set_fs(old_fs);
        filp_close(fp, NULL);
        return -1;
    }

    retLen = fp->f_op->write(fp, buf, len, offset);

    set_fs(old_fs);
    filp_close(fp, NULL);

    return retLen;
}

int load_custom_bt_conf(struct btradio_conf_data *cfg)
{
   /*
    This method depends on customer's platform configuration data
    store machenism.
    Customer may use NVRAM, data file, or other patterns.
    Here RECOMMEND and GIVE AN EXAMPLE to push configuration data
    under /data/BT.cfg
    */

    struct btradio_conf_data temp;
    loff_t pos = 0;
    ssize_t retLen;

    retLen = file_read(CUSTOM_BT_CFG_FILE,
                       (char*)&temp,
                       sizeof(temp),
                       &pos);

    if (retLen < 0)
        return -1;

    if(retLen < sizeof(temp)){
        BT_ERR_FUNC("File read error len: %d\n", retLen);
        return -1;
    }
    else{
        memcpy(cfg, &temp, retLen);
        return 0;
    }
}

int load_internal_bt_conf(struct btradio_conf_data *cfg)
{
    struct btradio_conf_data temp;
    loff_t pos = 0;
    ssize_t retLen;
    ssize_t written;

    retLen = file_read(INTERNAL_BT_CFG_FILE,
                       (char*)&temp,
                       sizeof(temp),
                       &pos);

    if (retLen < 0){
        BT_INFO_FUNC("No internal BT config, generate from default value\n");
        memcpy(&temp, &sDefaultCfg, sizeof(struct btradio_conf_data));

        // Generate internal BT config file
        pos = 0;
        written = file_write(INTERNAL_BT_CFG_FILE,
                             (char*)&temp,
                             sizeof(temp),
                             &pos);
        if (written < 0){
            BT_ERR_FUNC("Try to create internal BT config, error\n");
            return -1;
        }
        else if(written < sizeof(temp)){
            BT_ERR_FUNC("File write error len: %d\n", written);
        }
        else{
            BT_INFO_FUNC("Internal BT config generated\n");
        }

        memcpy(cfg, &temp, sizeof(temp));
        return 0;
    }
    else if(retLen < sizeof(temp)){
        BT_ERR_FUNC("File read error len: %d\n", retLen);
    }

    memcpy(cfg, &temp, retLen);
    return 0;
}


static inline void hci_stp_tx_skb_comp (struct hci_stp *hu, struct sk_buff *skb)
{
    struct hci_dev *hdev;
    int pkt_type;


    hdev = hu->hdev;
    hdev->stat.byte_tx += skb->len;

    pkt_type = bt_cb(skb)->pkt_type;
    /* Update HCI stat counters */
    switch (pkt_type) {
       case HCI_COMMAND_PKT:
            hdev->stat.cmd_tx++;
            break;

       case HCI_ACLDATA_PKT:
            hdev->stat.acl_tx++;
            break;

       case HCI_SCODATA_PKT:
            hdev->stat.cmd_tx++;
            break;
    }
}

#if (HCI_STP_TX == HCI_STP_TX_TASKLET)
void hci_stp_tx_init (struct hci_stp *hu)
{
    tasklet_init(&hci_tx_tasklet, hci_stp_tx_tasklet_func, (unsigned long)hu);

    #if (HCI_STP_TX_TASKLET_LOCK == HCI_STP_TX_TASKLET_RWLOCK)
    rwlock_init(&hci_stp_txqlock);
    #elif (HCI_STP_TX_TASKLET_LOCK == HCI_STP_TX_TASKLET_SPINLOCK)
    spin_lock_init(&hci_stp_txqlock);
    #endif
}

void hci_stp_tx_deinit (struct hci_stp *hu)
{
    tasklet_kill(&hci_tx_tasklet);

    return;
}

void hci_stp_txq_lock (unsigned int ctx)
{
    if (ctx == HCI_STP_TXQ_IN_BLZ) {
        /* lock txq in BlueZ tx tasklet context */
    #if (HCI_STP_TX_TASKLET_LOCK == HCI_STP_TX_TASKLET_RWLOCK)
        write_lock_bh(&hci_stp_txqlock);
    #elif (HCI_STP_TX_TASKLET_LOCK == HCI_STP_TX_TASKLET_SPINLOCK)
        spin_lock_bh(&hci_stp_txqlock);
    #else
    #error "HCI_STP_TX_TASKLET_LOCK"
    #endif
    }
    else {
        /* lock txq in HCI-STP context(defined by compile flag: HCI_STP_TX) */
    #if (HCI_STP_TX_TASKLET_LOCK == HCI_STP_TX_TASKLET_RWLOCK)
        write_lock_bh(&hci_stp_txqlock);
    #elif (HCI_STP_TX_TASKLET_LOCK == HCI_STP_TX_TASKLET_SPINLOCK)
        spin_lock_bh(&hci_stp_txqlock);
    #else
    #error "HCI_STP_TX_TASKLET_LOCK"
    #endif
    }
}

void hci_stp_txq_unlock (unsigned int ctx)
{
    if (ctx == HCI_STP_TXQ_IN_BLZ) {
        /* lock txq in BlueZ tx tasklet context with hci_stp_tx */
    #if (HCI_STP_TX_TASKLET_LOCK == HCI_STP_TX_TASKLET_RWLOCK)
        write_unlock_bh(&hci_stp_txqlock);
    #elif (HCI_STP_TX_TASKLET_LOCK == HCI_STP_TX_TASKLET_SPINLOCK)
        spin_unlock_bh(&hci_stp_txqlock);
    #else
    #error "HCI_STP_TX_TASKLET_LOCK"
    #endif
    }
    else {
        /* lock txq in HCI-STP context(defined by compile flag: HCI_STP_TX) */
    #if (HCI_STP_TX_TASKLET_LOCK == HCI_STP_TX_TASKLET_RWLOCK)
        write_unlock_bh(&hci_stp_txqlock);
    #elif (HCI_STP_TX_TASKLET_LOCK == HCI_STP_TX_TASKLET_SPINLOCK)
        spin_unlock_bh(&hci_stp_txqlock);
    #else
    #error "HCI_STP_TX_TASKLET_LOCK"
    #endif
    }
}

void hci_stp_tx_kick (void)
{
    tasklet_schedule(&hci_tx_tasklet);
}

static void hci_stp_tx_tasklet_func(unsigned long data) {

    struct hci_stp *hu = (struct hci_stp *)data;

    /* sanity check to see if status is still correct? */
    if (unlikely(hdev == NULL)) {
        BT_ERR_FUNC("Null hdev!\n");
        BUG_ON(hdev == NULL);
        return;
    }

    if (unlikely(hu != hdev->driver_data)) {
        BT_ERR_FUNC("hu(0x%p) != hdev->driver_data(0x%p)\n",
            hu, hdev->driver_data);
        BUG_ON(hu != hdev->driver_data);
        return;
    }

    //read_lock(&hci_stp_txq_lock);
    hci_stp_txq_lock(HCI_STP_TXQ_IN_HCISTP);

    hci_stp_tx_wakeup(hu);

    //read_unlock(&hci_stp_txq_lock);
    hci_stp_txq_unlock(HCI_STP_TXQ_IN_HCISTP);
}

/* George: HCI_STP_SENDING and HCI_STP_TX_WAKEUP flags in this function seem
* to be redundant.
*/
static int hci_stp_tx_wakeup(struct hci_stp *hu)
{
//    struct hci_dev *hdev = hu->hdev;
    struct sk_buff *skb;
    int j = 0;

    BT_TRC_FUNC();

    if (test_and_set_bit(HCI_STP_SENDING, &hu->tx_state)) {
        set_bit(HCI_STP_TX_WAKEUP, &hu->tx_state);
        printk("[BT] enqueue and return\n");
        return 0;
    }

    BT_DBG_FUNC("hci_stp_tx_wakeup %d\n", __LINE__);

restart:
    clear_bit(HCI_STP_TX_WAKEUP, &hu->tx_state);

    while ((skb = skb_dequeue(&hu->txq))) {
        int len;
        BT_DBG_FUNC("dqueue times = %d\n", ++j);

        /* hci reset cmd check */
#if HCI_STP_SAFE_RESET
        if (unlikely(skb->len == ARRAY_SIZE(bt_reset))) {
            if (unlikely(!memcmp(bt_reset, skb->data, ARRAY_SIZE(bt_reset)))) {
                atomic_inc(&hu->reset_count);
                BT_DBG_FUNC("hci reset cmd,f(%d),c(%d)\n",
                    BT_GET_HDEV_RST_FG(hdev),
                    atomic_read(&hu->reset_count));
            }
        }
#endif

        if ((len = mtk_wcn_stp_send_data(skb->data, skb->len, BT_TASK_INDX)) == 0 ) {
            /* can not send */
            BT_ERR_FUNC("mtk_wcn_stp_send_data can not send\n");
            BT_ERR_FUNC("Error %s %d\n", __FUNCTION__, __LINE__);

            skb_queue_head(&hu->txq, skb);//Put back to queue head
            goto END;
        }

        //hdev->stat.byte_tx += len; // moved into hci_stp_tx_skb_comp()
        //hci_stp_tx_skb_comp(hu, bt_cb(skb)->pkt_type);
        hci_stp_tx_skb_comp(hu, skb);
        kfree_skb(skb);
    }

END:
    if (test_bit(HCI_STP_TX_WAKEUP, &hu->tx_state))
          goto restart;

    clear_bit(HCI_STP_SENDING, &hu->tx_state);

    return 0;
}


#elif (HCI_STP_TX == HCI_STP_TX_THRD)

void hci_stp_tx_init (struct hci_stp *hu)
{
    spin_lock_init(&hci_stp_txqlock);
    init_waitqueue_head(&hci_stp_tx_thrd_wq);

    hci_stp_tx_thrd = kthread_create(hci_stp_tx_thrd_func, (void *)hu, "hci_stpd");
    if (NULL == hci_stp_tx_thrd) {
        BT_ERR_FUNC("kthread_create hci_stpd fail!\n");
    }
    wake_up_process(hci_stp_tx_thrd);

    return;
}

void hci_stp_tx_deinit (struct hci_stp *hu)
{
    kthread_stop(hci_stp_tx_thrd);
    hci_stp_tx_thrd = NULL;

    return;
}

void hci_stp_txq_lock (unsigned int ctx)
{
    if (ctx == HCI_STP_TXQ_IN_BLZ) {
        /* lock txq in BlueZ tx tasklet context */
        spin_lock(&hci_stp_txqlock);
    }
    else {
        /* lock txq in HCI-STP context(defined by compile flag: HCI_STP_TX) */
        spin_lock_bh(&hci_stp_txqlock);
    }
}

void hci_stp_txq_unlock (unsigned int ctx)
{
    if (ctx == HCI_STP_TXQ_IN_BLZ) {
        spin_unlock(&hci_stp_txqlock);
    }
    else {
        /* lock txq in HCI-STP context(defined by compile flag: HCI_STP_TX) */
        spin_unlock_bh(&hci_stp_txqlock);
    }
}

static int
hci_stp_tx_thrd_func (void *pdata)
{
    struct hci_stp *hu;
    struct hci_dev *hdev;
    struct sk_buff *skb;
    int len;

    hu = (struct hci_stp *)pdata;
    hdev = hu->hdev;

    /* sanity check to see if status is still correct? */
    if (unlikely(hdev == NULL)) {
        BT_ERR_FUNC("Null hdev!\n");
        BUG_ON(hdev == NULL);
        return -1;
    }

    if (unlikely(hu != hdev->driver_data)) {
        BT_ERR_FUNC("hu(0x%p) != hdev->driver_data(0x%p)\n",
            hu, hdev->driver_data);
        BUG_ON(hu != hdev->driver_data);
        return -1;
    }

    for (;;) {
        smp_rmb(); /* sync shared data */

        wait_event_interruptible(hci_stp_tx_thrd_wq,
            (!skb_queue_empty(&hu->txq) || kthread_should_stop()));

        if (unlikely(kthread_should_stop())) {
            BT_DBG_FUNC("hci_stpd thread should stop now... \n");
            break;
        }

        hci_stp_txq_lock(HCI_STP_TXQ_IN_HCISTP);
        while ((skb = skb_dequeue(&hu->txq))) {
            /* protect txq only */
            hci_stp_txq_unlock(HCI_STP_TXQ_IN_HCISTP);

            /* hci reset cmd check */
    #if HCI_STP_SAFE_RESET
            if (unlikely(skb->len == ARRAY_SIZE(bt_reset))) {
                if (unlikely(!memcmp(bt_reset, skb->data, ARRAY_SIZE(bt_reset)))) {
                    atomic_inc(&hu->reset_count);
                    BT_DBG_FUNC("hci reset cmd,f(%d),c(%d)\n",
                        BT_GET_HDEV_RST_FG(hdev),
                        atomic_read(&hu->reset_count));
                }
            }
    #endif

            len = mtk_wcn_stp_send_data(skb->data, skb->len, BT_TASK_INDX);
            if (unlikely(len != skb->len)) {
                /* can not send */
                BT_ERR_FUNC("mtk_wcn_stp_send_data fail, enqueue again!(%d, %d)\n",
                    len, skb->len);

                hci_stp_txq_lock(HCI_STP_TXQ_IN_HCISTP);
                skb_queue_head(&hu->txq, skb);//Put back to queue head
                /* do hci_stp_txq_unlock outside while loop */
                break;
            }
            //hdev->stat.byte_tx += len; // moved into hci_stp_tx_skb_comp()
            hci_stp_tx_skb_comp(hu, skb);
            kfree_skb(skb);

            hci_stp_txq_lock(HCI_STP_TXQ_IN_HCISTP);
        }
        hci_stp_txq_unlock(HCI_STP_TXQ_IN_HCISTP);

        /* back to wait */
    }

    BT_INFO_FUNC("tx thread stop!\n");
    return 0;
}

void hci_stp_tx_kick (void)
{
    smp_wmb();
    wake_up_interruptible(&hci_stp_tx_thrd_wq);
}

#else
#error "Not implemented HCI_STP_TX"
#endif


void hci_stp_dev_init_rx_cb (const UINT8 *data, INT32 count)
{
    struct hci_stp *hu;
    unsigned int idx;

    if (unlikely(!hdev)) {
        BT_ERR_FUNC("null hdev!\n");
        return;
    }
    if (unlikely(!hdev->driver_data)) {
        BT_ERR_FUNC("null hdev->driver_data!\n");
        return;
    }

    /* get hci_stp from global variable */
    hu = (struct hci_stp *)hdev->driver_data;
    idx = hu->init_cmd_idx;

    if (unlikely(count != init_table[idx].evtSz)){
        hu->init_evt_rx_flag = -1; /* size mismatch */
    }
    else if (unlikely(memcmp(data, init_table[idx].hci_evt, 7))){
        hu->init_evt_rx_flag = -2; /* content mismatch */
    }
    else{
        hu->init_evt_rx_flag = 1; /* ok */
        BT_DBG_FUNC("EVT(%d) len(%d) ok\n", idx, count);
        if (idx == 0) {
            /* store the returned eFUSE address */
            memcpy(&bt_get_bd_addr_evt[7], &data[7], 6);
        }
    }

    if (unlikely(1 != hu->init_evt_rx_flag)) {
        int i;
        BT_WARN_FUNC("EVT(%d) len(%d) buf:[", idx, count);
        for (i = 0; i < count; ++i) {
            printk("0x%02x ", data[i]);
        }
        printk("]\n");
        BT_WARN_FUNC("EVT(%d) exp(%d) buf:[", idx, init_table[idx].evtSz);
        for (i = 0; i < count; ++i) {
            printk("0x%02x ", init_table[idx].hci_evt[i]);
        }
        printk("]\n");

    }

    smp_wmb(); /* sync shared data */

    spin_lock(&hu->init_lock);
    if (likely(hu->p_init_evt_wq)) {
        wake_up(hu->p_init_evt_wq); /* wake up dev_init_work */
    }
    else {
        int i;
        BT_WARN_FUNC("late EVT(%d) len(%d) buf:[", idx, count);
        for (i = 0; i < count; ++i) {
            printk("0x%02x ", data[i]);
        }
        printk("]\n");
        BT_WARN_FUNC("Please check if uart rx data is returned or processed in time for BT init!\n");
        BT_WARN_FUNC("Possibly caused by a very busy system, or stp_uart rx priority too low...\n");
        BT_WARN_FUNC("Check which one is the real case and try to raise stp_uart rx priority.\n");
    }
    spin_unlock(&hu->init_lock);
}

static void hci_stp_dev_init_work (struct work_struct *work)
{
    struct hci_stp *phu;
    unsigned int idx;
    long ret, to;

    struct btradio_conf_data cfg = {
        {0x00, 0x00, 0x46, 0x66, 0x20, 0x01},
        {0x60, 0x00},
        {0x23, 0x10, 0x00, 0x00},
        {0x06, 0x80, 0x00, 0x06, 0x03, 0x06},
        {0x03, 0x40, 0x1F, 0x40, 0x1F, 0x00, 0x04},
        {0x80, 0x00},
        {0xFF, 0xFF, 0xFF}};

    /* get client's information */
    phu = container_of(work, struct hci_stp, init_work);

    if (load_custom_bt_conf(&cfg) < 0){
        BT_INFO_FUNC("No custom BT config\n");

        if (load_internal_bt_conf(&cfg) < 0){
            BT_ERR_FUNC("Load internal BT config failed!\n");
        }
        else{
            BT_INFO_FUNC("Load internal BT config success\n");

            if (0 == memcmp(cfg.addr, sDefaultCfg.addr, 6)){
                /* BD address default value, want to retrieve module eFUSE */
                fgetEFUSE = true;
                /* retrieve eFUSE address in init command loop */
            }
        }
    }
    else{
        BT_INFO_FUNC("Load custom BT config success\n");
    }

    BT_DBG_FUNC("Read BT config data:\n");
    BT_DBG_FUNC("[BD address %02x-%02x-%02x-%02x-%02x-%02x]\n",
        cfg.addr[0], cfg.addr[1], cfg.addr[2], cfg.addr[3], cfg.addr[4], cfg.addr[5]);
    BT_DBG_FUNC("[voice %02x %02x][codec %02x %02x %02x %02x]\n",
        cfg.voice[0], cfg.voice[1], cfg.codec[0], cfg.codec[1], cfg.codec[2], cfg.codec[3]);
    BT_DBG_FUNC("[radio %02x %02x %02x %02x %02x %02x]\n",
        cfg.radio[0], cfg.radio[1], cfg.radio[2], cfg.radio[3], cfg.radio[4], cfg.radio[5]);
    BT_DBG_FUNC("[sleep %02x %02x %02x %02x %02x %02x %02x]\n",
        cfg.sleep[0], cfg.sleep[1], cfg.sleep[2], cfg.sleep[3], cfg.sleep[4], cfg.sleep[5], cfg.sleep[6]);
    BT_DBG_FUNC("[feature %02x %02x]\n",
        cfg.feature[0], cfg.feature[1]);
    BT_DBG_FUNC("[tx power offset %02x %02x %02x]\n",
        cfg.tx_pwr_offset[0], cfg.tx_pwr_offset[1], cfg.tx_pwr_offset[2]);

    bt_set_bd_addr[4] = cfg.addr[5];
    bt_set_bd_addr[5] = cfg.addr[4];
    bt_set_bd_addr[6] = cfg.addr[3];
    bt_set_bd_addr[7] = cfg.addr[2];
    bt_set_bd_addr[8] = cfg.addr[1];
    bt_set_bd_addr[9] = cfg.addr[0];

    memcpy(&bt_set_voice[4], cfg.voice, 2);
    memcpy(&bt_set_codec[4], cfg.codec, 4);
    memcpy(&bt_set_radio[4], cfg.radio, 6);
    memcpy(&bt_set_tx_pwr_offset[4], cfg.tx_pwr_offset, 3);
    memcpy(&bt_set_sleep[4], cfg.sleep, 7);
    memcpy(&bt_set_feature[4], cfg.feature, 2);

    /*
     * INIT command loop starts
     */
    if (fgetEFUSE == true)
        idx = 0;
    else // skip bt_get_bd_addr
        idx = 1;

    for (; idx < ARRAY_SIZE(init_table); ++idx) {
        phu->init_cmd_idx = idx;
        phu->init_evt_rx_flag = 0;
        to = (init_table[idx].hci_cmd == bt_reset) ? BT_CMD_DELAY_MS_RESET : BT_CMD_DELAY_MS_COMM;
        /* safe waiting time in case running on a busy system */
        to = msecs_to_jiffies(to * BT_CMD_DELAY_SAFE_GUARD);

        BT_DBG_FUNC("CMD(%d) (%s) t/o(%ld))\n", idx, init_table[idx].str, to);
        smp_wmb(); /* sync shared data */

        /* Send hci command */
        mtk_wcn_stp_send_data(init_table[idx].hci_cmd, init_table[idx].cmdSz, BT_TASK_INDX);
        /* Wait rx hci event */
        /* no need to lock init_lock here for wq, for that it will be freed
         * only after we call complete(phu->p_init_comp); in this function.
         */
        ret = wait_event_timeout((*phu->p_init_evt_wq), phu->init_evt_rx_flag != 0, to);

        /* Check result */
        if (likely(1 == phu->init_evt_rx_flag)) {
            if (idx == 0) 
            { // bt_get_bd_addr event handler
                unsigned long randNum;
                loff_t pos = 0;
                ssize_t written;

                BT_DBG_FUNC("Retrieve eFUSE address %02x-%02x-%02x-%02x-%02x-%02x\n",
                    bt_get_bd_addr_evt[12], bt_get_bd_addr_evt[11], bt_get_bd_addr_evt[10], bt_get_bd_addr_evt[9], bt_get_bd_addr_evt[8], bt_get_bd_addr_evt[7]);

                cfg.addr[0] = bt_get_bd_addr_evt[12];
                cfg.addr[1] = bt_get_bd_addr_evt[11];
                cfg.addr[2] = bt_get_bd_addr_evt[10];
                cfg.addr[3] = bt_get_bd_addr_evt[9];
                cfg.addr[4] = bt_get_bd_addr_evt[8];
                cfg.addr[5] = bt_get_bd_addr_evt[7];

                if (0 == memcmp(cfg.addr, sDefaultCfg.addr, 6)){
                #if BD_ADDR_AUTOGEN
                    /* eFUSE address default value, enable auto-gen */
                    BT_DBG_FUNC("eFUSE address default value, enable auto-gen!\n");
                    get_random_bytes(&randNum, sizeof(unsigned long));
                    BT_DBG_FUNC("Get random number: %lu\n", randNum);

                    bt_get_bd_addr_evt[12] = (((randNum>>24 | randNum>>16) & (0xFE)) | (0x02));
                    bt_get_bd_addr_evt[11] = ((randNum>>8) & 0xFF);
                    bt_get_bd_addr_evt[7] = (randNum & 0xFF);
                    
                    cfg.addr[0] = bt_get_bd_addr_evt[12];
                    cfg.addr[1] = bt_get_bd_addr_evt[11];
                    cfg.addr[5] = bt_get_bd_addr_evt[7];
                #endif
                }
                else {
                    /* eFUSE address has valid value */
                }

                memcpy(&bt_set_bd_addr[4], &bt_get_bd_addr_evt[7], 6);

                /* Update BD address in internal BT config file */
                pos = 0;
                written = file_write(INTERNAL_BT_CFG_FILE,
                                     (char*)&cfg,
                                     6,
                                     &pos);

                /* Clear flag */
                fgetEFUSE = false;
            }
            /* Process next cmd */
            continue;
        }
        else {
            BT_ERR_FUNC("EVT(%d) ret(%u) rx_flag(%d)<=\n",
                idx, jiffies_to_msecs(ret), phu->init_evt_rx_flag);
            /* Stop processing and skip next cmd */
            break;
        }
    }

    if (phu->p_init_comp) {
        complete(phu->p_init_comp);
    }
}

static int hci_stp_dev_init (struct hci_stp *phu)
{
    DECLARE_COMPLETION_ONSTACK(hci_stp_dev_init_comp);
    DECLARE_WAIT_QUEUE_HEAD_ONSTACK(hci_stp_dev_init_wq);

    spin_lock(&phu->init_lock);
    phu->p_init_comp = &hci_stp_dev_init_comp;
    phu->p_init_evt_wq = &hci_stp_dev_init_wq;
    spin_unlock(&phu->init_lock);

    /* unregister rx event callback */
    mtk_wcn_stp_register_event_cb(BT_TASK_INDX, NULL);
    /* register direct rx callback for init only */
    mtk_wcn_stp_register_if_rx(hci_stp_dev_init_rx_cb);
    /* use bluez mode */
    mtk_wcn_stp_set_bluez(1);

    /* Schedule to call hci_stp_dev_init_work(). init_work is initialized in
     * hci_stp_init().
     */
    schedule_work(&phu->init_work);

    wait_for_completion(&hci_stp_dev_init_comp);

    spin_lock(&phu->init_lock);
    /* clear references to stack variables */
    phu->p_init_comp = NULL;
    phu->p_init_evt_wq = NULL;
    spin_unlock(&phu->init_lock);

    /* check result */
    /* flag: (to be replaced by a constant value)
        1 rx event correctly,
        0 no response in time,
        -1 unequal rx event length,
        -2 unequal rx event content.
    */
    if (likely(1 == phu->init_evt_rx_flag)) {
        return 0;
    }
    else {
        /* return non-zero value for error */
        return (phu->init_evt_rx_flag + 256);
    }
}

/* Invoked when there is ONE received BT packet */
void stp_tx_event_cb(void)
{
#if 0
    struct hci_stp *hu;
    if (unlikely(hdev == NULL)) {
        BT_ERR_FUNC("Null hdev!\n");
        BUG_ON(hdev == NULL);
        return;
    }
    hu = (struct hci_stp *) hdev->driver_data;
#endif
    /* George: [FixMe] can we call hci_stp_tx_wakeup() directly in STP-CORE's
     * context? It seems to be dangerous! Replace it with suitable one according
     * to HCI_STP_TX compile flag.
     */
    hci_stp_tx_kick(); /* George: adapt different tx_kick function */
}

/*
  Direct delivery of bluez not changed hands through the stp buffer
*/
void stp_rx_event_cb_directly(const UINT8 *data, INT32 count)
{
    register const UINT8 *ptr;
    struct hci_event_hdr *eh;
    struct hci_acl_hdr   *ah;
    struct hci_sco_hdr   *sh;
    register int len, type, dlen;
    int while_count; /* = 0; Is while_count redundant? */
    //static unsigned long rx_count; /* Is it ok w/o an initial value??? */
    static unsigned int rx_count = 0;
    //static unsigned long rx_state; /* Is it ok w/o an initial value??? */
    static unsigned int rx_state = H4_W4_PACKET_TYPE;
    struct  sk_buff *rx_skb = NULL; /* Is it ok to use non-static skb??? */
    register int room;
#if HCI_STP_SAFE_RESET
    struct hci_stp *hu;
#endif

    BT_LOUD_FUNC("count(%d)rx_state(%d)rx_count(%d)\n",
        count, rx_state, rx_count);

    if (data == NULL) {
        BT_ERR_FUNC("Data is Null\n");
        return;
    }

    if (count > 5000) {
        BT_WARN_FUNC("abnormal count(%d)\n", count);
    }

    ptr = data;
    /*Add statistic*/
    hdev->stat.byte_rx += count;

#if HCI_STP_SAFE_RESET
    hu = (struct hci_stp *)hdev->driver_data;
    /* is waiting hci reset event? */
    if (unlikely(atomic_read(&hu->reset_count))) {
        if (count == ARRAY_SIZE(bt_reset_evt)) {
            if (!memcmp(bt_reset_evt, data, ARRAY_SIZE(bt_reset_evt))) {
                BT_DBG_FUNC("hci reset evt,f(%d),c(%d)\n",
                    BT_GET_HDEV_RST_FG(hdev),
                    atomic_read(&hu->reset_count));

                atomic_dec(&hu->reset_count);
                wake_up(&hu->reset_wq);
            }
        }
    }
#endif

    while_count = 0;
    while (count > 0) { /* while (count) */
        /* Is while_count redundant? */
        //while_count++;
        if (++while_count > 5000) {
            BT_WARN_FUNC("abnormal while_count(%d)\n", while_count);
        }

        if (rx_count) {
            len = min_t(unsigned int, rx_count, count);
            memcpy(skb_put(rx_skb, len), ptr, len);
            rx_count -= len;
            count -= len;
            ptr += len;

            if (rx_count)
                continue;
            /* rx_count==0, ready to indicate to hci_core */

            switch (rx_state) {
            case H4_W4_DATA:
                BT_LOUD_FUNC("Complete data\n");
                hci_recv_frame(rx_skb);
                rx_state = H4_W4_PACKET_TYPE;
                rx_skb = NULL;
                continue;

            case H4_W4_EVENT_HDR:
                eh = hci_event_hdr(rx_skb);
                //BT_DBG_FUNC("Event header:evt(0x%2.2x)plen(%d)\n", eh->evt, eh->plen);
                room = skb_tailroom(rx_skb);
                //BT_DBG_FUNC("len(%d)room(%d)\n", eh->plen, room);
                BT_LOUD_FUNC("Event header:evt(0x%2.2x)plen(%d)room(%d)\n",
                    eh->evt, eh->plen, room);

                if (!eh->plen) {
                    hci_recv_frame(rx_skb);
                    rx_state = H4_W4_PACKET_TYPE;
                    rx_skb   = NULL;
                    rx_count = 0; /* redundant? here rx_count is 0 already */
                }
                else if (eh->plen > room) {
                    //BT_ERR("Data length is too large\n");
                    BT_ERR_FUNC("too large data length:eh->plen(%d)>room(%d)\n",
                        eh->plen, room);
                    kfree_skb(rx_skb);
                    rx_state = H4_W4_PACKET_TYPE;
                    rx_skb = NULL;
                    rx_count = 0; /* redundant? here rx_count is 0 already */
                }
                else {
                    rx_state = H4_W4_DATA;
                    rx_count = eh->plen;
                }
                continue;

            case H4_W4_ACL_HDR:
                ah = hci_acl_hdr(rx_skb);
                dlen = __le16_to_cpu(ah->dlen);

                room = skb_tailroom(rx_skb);
                BT_LOUD_FUNC("ACL header:dlen(%d)room(%d)\n", dlen, room);
                if (!dlen) {
                    hci_recv_frame(rx_skb);
                    rx_state = H4_W4_PACKET_TYPE;
                    rx_skb = NULL;
                    rx_count = 0;
                }
                else if (dlen > room) {
                    //BT_ERR_FUNC("Data length is too large\n");
                    BT_ERR_FUNC("too large data length:dlen(%d)>room(%d)\n",
                        dlen, room);
                    kfree_skb(rx_skb);
                    rx_state = H4_W4_PACKET_TYPE;
                    rx_skb = NULL;
                    rx_count = 0;
                }
                else {
                    rx_state = H4_W4_DATA;
                    rx_count = dlen;
                }
                continue;

            case H4_W4_SCO_HDR:
                sh = hci_sco_hdr(rx_skb);
                room = skb_tailroom(rx_skb);
                BT_LOUD_FUNC("SCO header:dlen(%d)room(%d)\n", sh->dlen, room);

                if (!sh->dlen) {
                    hci_recv_frame(rx_skb);
                    rx_state = H4_W4_PACKET_TYPE;
                    rx_skb = NULL;
                    rx_count = 0;
                }
                else if (sh->dlen > room) {
                    BT_ERR_FUNC("Data length is too large\n");
                    BT_ERR_FUNC("too large data length:sh->dlen(%d)>room(%d)\n",
                        sh->dlen , room);
                    kfree_skb(rx_skb);
                    rx_state = H4_W4_PACKET_TYPE;
                    rx_skb = NULL;
                    rx_count = 0;
                }
                else {
                    rx_state = H4_W4_DATA;
                    rx_count = sh->dlen;
                }
                continue;
            }
        }

        /* H4_W4_PACKET_TYPE */
        switch (*ptr) {
            case HCI_EVENT_PKT:
                BT_LOUD_FUNC("Event packet\n");
                rx_state = H4_W4_EVENT_HDR;
                rx_count = HCI_EVENT_HDR_SIZE;
                type = HCI_EVENT_PKT;
                break;

            case HCI_ACLDATA_PKT:
                BT_LOUD_FUNC("ACL packet\n");
                rx_state = H4_W4_ACL_HDR;
                rx_count = HCI_ACL_HDR_SIZE;
                type = HCI_ACLDATA_PKT;
                break;

            case HCI_SCODATA_PKT:
                BT_LOUD_FUNC("SCO packet\n");
                rx_state = H4_W4_SCO_HDR;
                rx_count = HCI_SCO_HDR_SIZE;
                type = HCI_SCODATA_PKT;
                break;

            default:
                BT_ERR_FUNC("Unknown HCI packet type %2.2x\n", (__u8)*ptr);
                ++(hdev->stat.err_rx);
                ++ptr;
                --count;
                continue;
        };

        ++ptr;
        --count;

        /* Allocate packet */
        rx_skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC);
        if (!rx_skb) {
           BT_ERR_FUNC("bt_skb_alloc(%d, GFP_ATOMIC) fail!\n", HCI_MAX_FRAME_SIZE);
           rx_state = H4_W4_PACKET_TYPE;
           rx_count = 0;
           return;
        }

        rx_skb->dev = (void *) hdev;
        bt_cb(rx_skb)->pkt_type = type;
    }

    return;
}

/* ------- Interface to HCI layer ------ */
/* Initialize device */
static int hci_stp_open(struct hci_dev *hdev)
{
    struct hci_stp *hu;
    int ret;

    if (unlikely(!hdev)) {
        BT_ERR_FUNC("null hdev\n");
        return -ENODEV;
    }

    if (unlikely(!hdev->driver_data)) {
        BT_ERR_FUNC("null hdev\n");
        return -ENODEV;
    }

    hu = (struct hci_stp *)hdev->driver_data;
    BT_INFO_FUNC("%s(0x%p)\n", hdev->name, hdev);

    /* clear txq and free all skb in it */
    hci_stp_txq_lock(HCI_STP_TXQ_IN_BLZ);
    skb_queue_purge(&hu->txq);
    hci_stp_txq_unlock(HCI_STP_TXQ_IN_BLZ);

    /* turn on BT */
    if (unlikely(MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_BT))) {
        BT_WARN_FUNC("WMT turn on BT fail!\n");
        return -ENODEV;
    }

    BT_INFO_FUNC("WMT turn on BT OK!\n");

    if (likely(mtk_wcn_stp_is_ready())) {
        BT_DBG_FUNC("STP is ready!\n");

        ret = hci_stp_dev_init(hu);
        /* error handling: turn off BT */
        if (unlikely(ret)) {
            BT_WARN_FUNC("hci_stp_dev_init fail(%d)!\n", ret);
            if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_off(WMTDRV_TYPE_BT)) {
                BT_WARN_FUNC("WMT turn off BT fail!\n");
            }
            else {
                BT_INFO_FUNC("WMT turn off BT ok!\n");
            }
            return -ENODEV;
        }

        BT_INFO_FUNC("hci_stp_dev_init ok\n");

        set_bit(HCI_RUNNING, &hdev->flags);

        /* registered tx/rx path */
        mtk_wcn_stp_register_if_rx(stp_rx_event_cb_directly);

        mtk_wcn_stp_register_event_cb(BT_TASK_INDX, NULL);
        mtk_wcn_stp_register_tx_event_cb(BT_TASK_INDX, stp_tx_event_cb);

        /* use bluez */
        mtk_wcn_stp_set_bluez(1);

        return 0;
    }
    else {
        BT_WARN_FUNC("STP is not ready!\n");
        return -ENODEV;
    }
}

/* Reset device */
static int hci_stp_flush(struct hci_dev *hdev)
{
    struct hci_stp *hu;

    BT_DBG_FUNC("start\n");
    if (!hdev || !hdev->driver_data) {
        BT_WARN_FUNC("invalid hdev(0x%p) or drv data(0x%p)\n",
            hdev, (hdev) ? hdev->driver_data : hdev);
        return -EFAULT;
    }

    hu = (struct hci_stp *)hdev->driver_data;

    /* clear txq and free all skb in it */
    hci_stp_txq_lock(HCI_STP_TXQ_IN_BLZ);
    skb_queue_purge(&hu->txq);
    hci_stp_txq_unlock(HCI_STP_TXQ_IN_BLZ);

    BT_INFO_FUNC("done\n");
    return 0;
}

/* Close device */
static int hci_stp_close(struct hci_dev *hdev)
{
#if HCI_STP_SAFE_RESET
    struct hci_stp *hu;
    long ret;
#endif

    BT_INFO_FUNC("hdev(0x%p)\n", hdev);

    if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags)) {
          return 0;
    }

    hci_stp_flush(hdev);
    hdev->flush = NULL;

#if HCI_STP_SAFE_RESET
    hu = (struct hci_stp *)hdev->driver_data;
    if (hu) {
        /* double waiting time in case of busy system */
        ret = wait_event_timeout((hu->reset_wq),
            (atomic_read(&hu->reset_count) == 0),
            msecs_to_jiffies(BT_CMD_DELAY_MS_RESET*2));
        if ( (!ret) || (atomic_read(&hu->reset_count) != 0) ) {
            BT_WARN_FUNC("wait on-going reset finish fail,f(%d),c(%d),ret(%u)\n",
                BT_GET_HDEV_RST_FG(hdev),
                atomic_read(&hu->reset_count),
                jiffies_to_msecs(ret));
        }
        else {
            BT_DBG_FUNC("check reset log,f(%d),c(%d),ret(%u)\n",
                BT_GET_HDEV_RST_FG(hdev),
                atomic_read(&hu->reset_count),
                jiffies_to_msecs(ret));
        }
    }
#endif

    /* clear txq and free all skb in it */
    hci_stp_txq_lock(HCI_STP_TXQ_IN_BLZ);
    skb_queue_purge(&hu->txq);
    hci_stp_txq_unlock(HCI_STP_TXQ_IN_BLZ);

    /* unregistered tx/rx path */
    mtk_wcn_stp_register_if_rx(NULL);

    mtk_wcn_stp_register_event_cb(BT_TASK_INDX, NULL);
    mtk_wcn_stp_register_tx_event_cb(BT_TASK_INDX, NULL);

    /* not use bluez */
    mtk_wcn_stp_set_bluez(0);

    if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_off(WMTDRV_TYPE_BT)) {
        BT_WARN_FUNC("WMT turn off BT fail!\n");
    }
    else {
        BT_INFO_FUNC("WMT turn off BT OK!\n");
    }

    return 0;
}

/* Send frames from HCI layer */
static int hci_stp_send_frame(struct sk_buff *skb)
{
    struct hci_dev* hdev = (struct hci_dev *) skb->dev;
    struct hci_stp *hu;

    if (!hdev) {
        BT_ERR_FUNC("Null hdev in skb\n");
        return -ENODEV;
    }

    if (!test_bit(HCI_RUNNING, &hdev->flags)) {
        BT_ERR_FUNC("no HCI_RUNNING in hdev->flags(0x%lx)\n", hdev->flags);
        return -EBUSY;
    }

    hu = (struct hci_stp *) hdev->driver_data;

    BT_LOUD_FUNC("%s: type(%d) len(%d)\n",
        hdev->name, bt_cb(skb)->pkt_type, skb->len);

#if 0 /* just timestamp?? */
    if (gDbgLevel >= BT_LOG_DBG)
    {
        struct timeval now;
        do_gettimeofday(&now);
        printk("%s: sec = %ld, --> usec --> %ld\n",
             __FUNCTION__, now.tv_sec, now.tv_usec);
    }
#endif

    /* Prepend skb with frame type. Is it safe to do skb_push? */
    memcpy(skb_push(skb, 1), &bt_cb(skb)->pkt_type, 1);

    /* George: is no lock ok?? Add hci_stp_txq_lock and unlock! */
    hci_stp_txq_lock(HCI_STP_TXQ_IN_BLZ);
    /*Queue a buffer at the end of a list. This function takes no locks
    *  and you must therefore hold required locks before calling it.
    */
    skb_queue_tail(&hu->txq, skb);
    hci_stp_txq_unlock(HCI_STP_TXQ_IN_BLZ);

    /* George: adapt different tx_kick function */
    hci_stp_tx_kick();

    return 0;
}

static void hci_stp_destruct(struct hci_dev *hdev)
{
    BT_TRC_FUNC();

    if (!hdev) {
        return;
    }

    BT_DBG_FUNC("%s\n", hdev->name);
    hci_stp_tx_deinit((struct hci_stp *)hdev->driver_data);
    kfree(hdev->driver_data);
}

static int __init hci_stp_init(void)
{
    struct hci_stp *hu = NULL;

    if (!(hu = kzalloc(sizeof(struct hci_stp), GFP_ATOMIC))) {
        BT_ERR_FUNC("Can't allocate control structure\n");
        return -ENOMEM;
    }

    BT_DBG_FUNC("hu 0x%08x\n", (int)hu);

    /*
       used to stored pending skb
     */
    skb_queue_head_init(&hu->txq);

    /*
       Initialize and register HCI device
     */
    hdev = hci_alloc_dev();
    if (!hdev) {
        if (hu){
            kfree(hu);
            hu = NULL;
        }

        BT_ERR_FUNC("Can't allocate HCI device\n");
        return -ENOMEM;
    }

    hu->hdev = hdev;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34))
    hdev->type = HCI_UART;
#else
    hdev->bus = HCI_UART;
    hdev->dev_type = HCI_BREDR;
#endif
    hdev->driver_data = hu;

    BT_DBG_FUNC("hdev->driver_data 0x%08x\n", (int)hdev->driver_data);
    hdev->open  = hci_stp_open;
    hdev->close = hci_stp_close;
    hdev->flush = hci_stp_flush;
    hdev->send  = hci_stp_send_frame;
    hdev->destruct = hci_stp_destruct;

    hdev->owner = THIS_MODULE;

    BT_DBG_FUNC("HCI_QUIRK_NO_RESET\n");

    set_bit(HCI_QUIRK_NO_RESET, &hdev->quirks);

    if (hci_register_dev(hdev) < 0) {
        BT_ERR_FUNC("Can't register HCI device\n");
        kfree(hu);
        hu = NULL;
        hci_free_dev(hdev);
        hdev = NULL;
        return -ENODEV;
     }

#if HCI_STP_SAFE_RESET
    atomic_set(&hu->reset_count, 0);
    init_waitqueue_head(&hu->reset_wq);
#endif

    /* George: adapt different tx_init function */
    hci_stp_tx_init(hu);

    /* init_work in heap */
    INIT_WORK(&hu->init_work, hci_stp_dev_init_work);
    spin_lock_init(&hu->init_lock);

    mtk_wcn_stp_register_if_rx(NULL);
    mtk_wcn_stp_register_event_cb(BT_TASK_INDX, NULL);
    mtk_wcn_stp_register_tx_event_cb(BT_TASK_INDX, NULL);

    BT_INFO_FUNC("HCI STP driver ver %s, hdev(0x%p), init done\n", VERSION, hdev);

    return 0;
}

static void __exit hci_stp_exit(void)
{
    struct hci_stp *hu = (struct hci_stp *)hdev->driver_data;

    BT_TRC_FUNC();

    mtk_wcn_stp_register_event_cb(BT_TASK_INDX, NULL);
    mtk_wcn_stp_register_tx_event_cb(BT_TASK_INDX, NULL);

    hci_unregister_dev(hdev);

    /* George: adapt different tx_deinit function */
    //tasklet_kill(&hci_tx_tasklet);

    skb_queue_purge(&hu->txq);

    /* hci_stp_destruct does this */
    /*kfree(hdev->driver_data);*/
    hci_free_dev(hdev);

    hdev = NULL;
}

module_init(hci_stp_init);
module_exit(hci_stp_exit);

module_param(reset, bool, 0644);
MODULE_PARM_DESC(reset, "Send HCI reset command on initialization");

MODULE_AUTHOR("Mediatek Inc.");
MODULE_DESCRIPTION("Bluetooth HCI STP driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");


