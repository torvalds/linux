/*
 *
 *  Realtek Bluetooth USB driver
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/usb.h>

#include <linux/ioctl.h>
#include <linux/io.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/reboot.h>

#include "rtk_btusb.h"

#define RTKBT_RELEASE_NAME "20170109_TV_ANDROID_6.x"
#define VERSION "4.1.2"

#define SUSPNED_DW_FW 0
#define SET_WAKEUP_DEVICE 0


static spinlock_t queue_lock;
static spinlock_t dlfw_lock;
static volatile uint16_t    dlfw_dis_state = 0;

#if SUSPNED_DW_FW
static firmware_info *fw_info_4_suspend = NULL;
#endif

static patch_info fw_patch_table[] = {
/* { vid, pid, lmp_sub_default, lmp_sub, everion, mp_fw_name, fw_name, config_name, fw_cache, fw_len, mac_offset } */
{ 0x0BDA, 0x1724, 0x1200, 0, 0, "mp_rtl8723a_fw", "rtl8723a_fw", "rtl8723a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723A */
{ 0x0BDA, 0x8723, 0x1200, 0, 0, "mp_rtl8723a_fw", "rtl8723a_fw", "rtl8723a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* 8723AE */
{ 0x0BDA, 0xA723, 0x1200, 0, 0, "mp_rtl8723a_fw", "rtl8723a_fw", "rtl8723a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* 8723AE for LI */
{ 0x0BDA, 0x0723, 0x1200, 0, 0, "mp_rtl8723a_fw", "rtl8723a_fw", "rtl8723a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* 8723AE */
{ 0x13D3, 0x3394, 0x1200, 0, 0, "mp_rtl8723a_fw", "rtl8723a_fw", "rtl8723a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* 8723AE for Azurewave*/

{ 0x0BDA, 0x0724, 0x1200, 0, 0, "mp_rtl8723a_fw", "rtl8723a_fw", "rtl8723a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* 8723AU */
{ 0x0BDA, 0x8725, 0x1200, 0, 0, "mp_rtl8723a_fw", "rtl8723a_fw", "rtl8723a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* 8723AU */
{ 0x0BDA, 0x872A, 0x1200, 0, 0, "mp_rtl8723a_fw", "rtl8723a_fw", "rtl8723a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* 8723AU */
{ 0x0BDA, 0x872B, 0x1200, 0, 0, "mp_rtl8723a_fw", "rtl8723a_fw", "rtl8723a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* 8723AU */

{ 0x0BDA, 0xb720, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723bu_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BU */
{ 0x0BDA, 0xb72A, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723bu_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BU */
{ 0x0BDA, 0xb728, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BE for LC */
{ 0x0BDA, 0xb723, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BE */
{ 0x0BDA, 0xb72B, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BE */
{ 0x0BDA, 0xb001, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BE for HP */
{ 0x0BDA, 0xb002, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BE */
{ 0x0BDA, 0xb003, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BE */
{ 0x0BDA, 0xb004, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BE */
{ 0x0BDA, 0xb005, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BE */

{ 0x13D3, 0x3410, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BE for Azurewave */
{ 0x13D3, 0x3416, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BE for Azurewave */
{ 0x13D3, 0x3459, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BE for Azurewave */
{ 0x0489, 0xE085, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BE for Foxconn */
{ 0x0489, 0xE08B, 0x8723, 0, 0, "mp_rtl8723b_fw", "rtl8723b_fw", "rtl8723b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8723BE for Foxconn */

{ 0x0BDA, 0x2850, 0x8761, 0, 0, "mp_rtl8761a_fw", "rtl8761au_fw", "rtl8761a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8761AU */
{ 0x0BDA, 0xA761, 0x8761, 0, 0, "mp_rtl8761a_fw", "rtl8761au_fw", "rtl8761a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8761AU only */
{ 0x0BDA, 0x818B, 0x8761, 0, 0, "mp_rtl8761a_fw", "rtl8761aw8192eu_fw", "rtl8761aw8192eu_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8761AW + 8192EU */
{ 0x0BDA, 0x818C, 0x8761, 0, 0, "mp_rtl8761a_fw", "rtl8761aw8192eu_fw", "rtl8761aw8192eu_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8761AW + 8192EU */
{ 0x0BDA, 0x8760, 0x8761, 0, 0, "mp_rtl8761a_fw", "rtl8761au8192ee_fw", "rtl8761a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8761AU + 8192EE */
{ 0x0BDA, 0xB761, 0x8761, 0, 0, "mp_rtl8761a_fw", "rtl8761au_fw", "rtl8761a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8761AUV only */
{ 0x0BDA, 0x8761, 0x8761, 0, 0, "mp_rtl8761a_fw", "rtl8761au8192ee_fw", "rtl8761a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8761AU + 8192EE for LI */
{ 0x0BDA, 0x8A60, 0x8761, 0, 0, "mp_rtl8761a_fw", "rtl8761au8812ae_fw", "rtl8761a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8761AU + 8812AE */

{ 0x0BDA, 0x8821, 0x8821, 0, 0, "mp_rtl8821a_fw", "rtl8821a_fw", "rtl8821a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8821AE */
{ 0x0BDA, 0x0821, 0x8821, 0, 0, "mp_rtl8821a_fw", "rtl8821a_fw", "rtl8821a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8821AE */
{ 0x0BDA, 0x0823, 0x8821, 0, 0, "mp_rtl8821a_fw", "rtl8821a_fw", "rtl8821a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8821AU */
{ 0x13D3, 0x3414, 0x8821, 0, 0, "mp_rtl8821a_fw", "rtl8821a_fw", "rtl8821a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8821AE */
{ 0x13D3, 0x3458, 0x8821, 0, 0, "mp_rtl8821a_fw", "rtl8821a_fw", "rtl8821a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8821AE */
{ 0x13D3, 0x3461, 0x8821, 0, 0, "mp_rtl8821a_fw", "rtl8821a_fw", "rtl8821a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8821AE */
{ 0x13D3, 0x3462, 0x8821, 0, 0, "mp_rtl8821a_fw", "rtl8821a_fw", "rtl8821a_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_1_2, MAX_PATCH_SIZE_24K}, /* RTL8821AE */

{ 0x0BDA, 0xB822, 0x8822, 0, 0, "mp_rtl8822b_fw", "rtl8822b_fw", "rtl8822b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_3PLUS, MAX_PATCH_SIZE_24K}, /* RTL8822BU */
{ 0x0BDA, 0xB82C, 0x8822, 0, 0, "mp_rtl8822b_fw", "rtl8822b_fw", "rtl8822b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_3PLUS, MAX_PATCH_SIZE_24K}, /* RTL8822BU */
{ 0x0BDA, 0xb023, 0x8822, 0, 0, "mp_rtl8822b_fw", "rtl8822b_fw", "rtl8822b_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_3PLUS, MAX_PATCH_SIZE_24K}, /* RTL8822BE */
{ 0x0BDA, 0xB703, 0x8703, 0, 0, "mp_rtl8723c_fw", "rtl8723c_fw", "rtl8723c_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_3PLUS, MAX_PATCH_SIZE_24K}, /* RTL8723CU */
/* todo: RTL8703BU */

{ 0x0BDA, 0xD723, 0x8723, 0, 0, "mp_rtl8723d_fw", "rtl8723d_fw", "rtl8723d_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_3PLUS, MAX_PATCH_SIZE_40K}, /* RTL8723DU */
{ 0x0BDA, 0xB820, 0x8821, 0, 0, "mp_rtl8821c_fw", "rtl8821c_fw", "rtl8821c_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_3PLUS, MAX_PATCH_SIZE_40K}, /* RTL8821CU */
{ 0x0BDA, 0xC820, 0x8821, 0, 0, "mp_rtl8821c_fw", "rtl8821c_fw", "rtl8821c_config", NULL, 0 ,CONFIG_MAC_OFFSET_GEN_3PLUS, MAX_PATCH_SIZE_40K}, /* RTL8821CU */
/* todo: RTL8703CU */

/* NOTE: must append patch entries above the null entry */
{ 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0 }
};

struct btusb_data {
    struct hci_dev       *hdev;
    struct usb_device    *udev;
    struct usb_interface *intf;
    struct usb_interface *isoc;

    spinlock_t lock;

    unsigned long flags;

    struct work_struct work;
    struct work_struct waker;

    struct usb_anchor tx_anchor;
    struct usb_anchor intr_anchor;
    struct usb_anchor bulk_anchor;
    struct usb_anchor isoc_anchor;
    struct usb_anchor deferred;
    int tx_in_flight;
    spinlock_t txlock;

    struct usb_endpoint_descriptor *intr_ep;
    struct usb_endpoint_descriptor *bulk_tx_ep;
    struct usb_endpoint_descriptor *bulk_rx_ep;
    struct usb_endpoint_descriptor *isoc_tx_ep;
    struct usb_endpoint_descriptor *isoc_rx_ep;

    __u8 cmdreq_type;

    unsigned int sco_num;
    int isoc_altsetting;
    int suspend_count;
//#ifdef CONFIG_HAS_EARLYSUSPEND
#if 0
    struct early_suspend early_suspend;
#else
    struct notifier_block pm_notifier;
    struct notifier_block reboot_notifier;
#endif
    firmware_info *fw_info;
};
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 1)
static bool reset_on_close = 0;
#endif

int download_patch(firmware_info *fw_info, int cached);

static inline int check_set_dlfw_state_value(uint16_t change_value)
{
    spin_lock(&dlfw_lock);
    if(!dlfw_dis_state) {
        dlfw_dis_state = change_value;
    }
    spin_unlock(&dlfw_lock);
    return dlfw_dis_state;
}

static inline void set_dlfw_state_value(uint16_t change_value)
{
    spin_lock(&dlfw_lock);
    dlfw_dis_state = change_value;
    spin_unlock(&dlfw_lock);
}

#if SUSPNED_DW_FW
static int download_suspend_patch(firmware_info *fw_info, int cached);
#endif
#if SET_WAKEUP_DEVICE
static void set_wakeup_device_from_conf(firmware_info *fw_info);
int set_wakeup_device(firmware_info* fw_info, uint8_t* wakeup_bdaddr);
#endif

static void rtk_free( struct btusb_data *data)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 1)
    kfree(data);
#endif
    return;
}

static struct btusb_data *rtk_alloc(struct usb_interface *intf)
{
    struct btusb_data *data;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 1)
    data = kzalloc(sizeof(*data), GFP_KERNEL);
#else
    data = devm_kzalloc(&intf->dev, sizeof(*data), GFP_KERNEL);
#endif
    return data;
}

static void print_acl(struct sk_buff *skb, int direction)
{
#if PRINT_ACL_DATA
    uint wlength = skb->len;
    u16 *handle = (u16 *)(skb->data);
    u16 len = *(handle+1);
    u8 *acl_data = (u8 *)(skb->data);

    RTK_INFO("%s: direction %d, handle %04x, len %d",
            __func__, direction, *handle, len);
#endif
}

static void print_sco(struct sk_buff *skb, int direction)
{
#if PRINT_SCO_DATA
    uint wlength = skb->len;
    u16 *handle = (u16 *)(skb->data);
    u8 len = *(u8 *)(handle+1);
    u8 *sco_data =(u8 *)(skb->data);

    RTKBT_INFO("%s: direction %d, handle %04x, len %d",
            __func__, direction, *handle, len);
#endif
}

static void print_error_command(struct sk_buff *skb)
{
    uint wlength = skb->len;
    uint icount = 0;
    u16 *opcode = (u16*)(skb->data);
    u8 *cmd_data = (u8*)(skb->data);
    u8 len = *(cmd_data+2);

    switch (*opcode) {
    case HCI_OP_INQUIRY:
        printk("HCI_OP_INQUIRY");
        break;
    case HCI_OP_INQUIRY_CANCEL:
        printk("HCI_OP_INQUIRY_CANCEL");
        break;
    case HCI_OP_EXIT_PERIODIC_INQ:
        printk("HCI_OP_EXIT_PERIODIC_INQ");
        break;
    case HCI_OP_CREATE_CONN:
        printk("HCI_OP_CREATE_CONN");
        break;
    case HCI_OP_DISCONNECT:
        printk("HCI_OP_DISCONNECT");
        break;
    case HCI_OP_CREATE_CONN_CANCEL:
        printk("HCI_OP_CREATE_CONN_CANCEL");
        break;
    case HCI_OP_ACCEPT_CONN_REQ:
        printk("HCI_OP_ACCEPT_CONN_REQ");
        break;
    case HCI_OP_REJECT_CONN_REQ:
        printk("HCI_OP_REJECT_CONN_REQ");
        break;
    case HCI_OP_AUTH_REQUESTED:
        printk("HCI_OP_AUTH_REQUESTED");
        break;
    case HCI_OP_SET_CONN_ENCRYPT:
        printk("HCI_OP_SET_CONN_ENCRYPT");
        break;
    case HCI_OP_REMOTE_NAME_REQ:
        printk("HCI_OP_REMOTE_NAME_REQ");
        break;
    case HCI_OP_READ_REMOTE_FEATURES:
        printk("HCI_OP_READ_REMOTE_FEATURES");
        break;
    case HCI_OP_SNIFF_MODE:
        printk("HCI_OP_SNIFF_MODE");
        break;
    case HCI_OP_EXIT_SNIFF_MODE:
        printk("HCI_OP_EXIT_SNIFF_MODE");
        break;
    case HCI_OP_SWITCH_ROLE:
        printk("HCI_OP_SWITCH_ROLE");
        break;
    case HCI_OP_SNIFF_SUBRATE:
        printk("HCI_OP_SNIFF_SUBRATE");
        break;
    case HCI_OP_RESET:
        printk("HCI_OP_RESET");
        break;
    case HCI_OP_Write_Extended_Inquiry_Response:
        printk("HCI_Write_Extended_Inquiry_Response");
        break;

    default:
        printk("CMD");
        break;
    }
    printk(":%04x,len:%d,", *opcode,len);
    for (icount = 3; (icount < wlength) && (icount < 24); icount++)
        printk("%02x ", *(cmd_data+icount));
    printk("\n");
}

static void print_command(struct sk_buff *skb)
{
#if PRINT_CMD_EVENT
    print_error_command(skb);
#endif
}

#if CONFIG_BLUEDROID
/* Global parameters for bt usb char driver */
#define BT_CHAR_DEVICE_NAME "rtk_btusb"
struct mutex btchr_mutex;
static struct sk_buff_head btchr_readq;
static wait_queue_head_t btchr_read_wait;
static wait_queue_head_t bt_dlfw_wait;
static int bt_char_dev_registered;
static dev_t bt_devid; /* bt char device number */
static struct cdev bt_char_dev; /* bt character device structure */
static struct class *bt_char_class; /* device class for usb char driver */
static int bt_reset = 0;
/* HCI device & lock */
DEFINE_RWLOCK(hci_dev_lock);
struct hci_dev *ghdev = NULL;

static void print_event(struct sk_buff *skb)
{
#if PRINT_CMD_EVENT
    uint wlength = skb->len;
    uint icount = 0;
    u8 *opcode = (u8*)(skb->data);
    u8 len = *(opcode+1);

    switch (*opcode) {
    case HCI_EV_INQUIRY_COMPLETE:
        printk("HCI_EV_INQUIRY_COMPLETE");
        break;
    case HCI_EV_INQUIRY_RESULT:
        printk("HCI_EV_INQUIRY_RESULT");
        break;
    case HCI_EV_CONN_COMPLETE:
        printk("HCI_EV_CONN_COMPLETE");
        break;
    case HCI_EV_CONN_REQUEST:
        printk("HCI_EV_CONN_REQUEST");
        break;
    case HCI_EV_DISCONN_COMPLETE:
        printk("HCI_EV_DISCONN_COMPLETE");
        break;
    case HCI_EV_AUTH_COMPLETE:
        printk("HCI_EV_AUTH_COMPLETE");
        break;
    case HCI_EV_REMOTE_NAME:
        printk("HCI_EV_REMOTE_NAME");
        break;
    case HCI_EV_ENCRYPT_CHANGE:
        printk("HCI_EV_ENCRYPT_CHANGE");
        break;
    case HCI_EV_CHANGE_LINK_KEY_COMPLETE:
        printk("HCI_EV_CHANGE_LINK_KEY_COMPLETE");
        break;
    case HCI_EV_REMOTE_FEATURES:
        printk("HCI_EV_REMOTE_FEATURES");
        break;
    case HCI_EV_REMOTE_VERSION:
        printk("HCI_EV_REMOTE_VERSION");
        break;
    case HCI_EV_QOS_SETUP_COMPLETE:
        printk("HCI_EV_QOS_SETUP_COMPLETE");
        break;
    case HCI_EV_CMD_COMPLETE:
        printk("HCI_EV_CMD_COMPLETE");
        break;
    case HCI_EV_CMD_STATUS:
        printk("HCI_EV_CMD_STATUS");
        break;
    case HCI_EV_ROLE_CHANGE:
        printk("HCI_EV_ROLE_CHANGE");
        break;
    case HCI_EV_NUM_COMP_PKTS:
        printk("HCI_EV_NUM_COMP_PKTS");
        break;
    case HCI_EV_MODE_CHANGE:
        printk("HCI_EV_MODE_CHANGE");
        break;
    case HCI_EV_PIN_CODE_REQ:
        printk("HCI_EV_PIN_CODE_REQ");
        break;
    case HCI_EV_LINK_KEY_REQ:
        printk("HCI_EV_LINK_KEY_REQ");
        break;
    case HCI_EV_LINK_KEY_NOTIFY:
        printk("HCI_EV_LINK_KEY_NOTIFY");
        break;
    case HCI_EV_CLOCK_OFFSET:
        printk("HCI_EV_CLOCK_OFFSET");
        break;
    case HCI_EV_PKT_TYPE_CHANGE:
        printk("HCI_EV_PKT_TYPE_CHANGE");
        break;
    case HCI_EV_PSCAN_REP_MODE:
        printk("HCI_EV_PSCAN_REP_MODE");
        break;
    case HCI_EV_INQUIRY_RESULT_WITH_RSSI:
        printk("HCI_EV_INQUIRY_RESULT_WITH_RSSI");
        break;
    case HCI_EV_REMOTE_EXT_FEATURES:
        printk("HCI_EV_REMOTE_EXT_FEATURES");
        break;
    case HCI_EV_SYNC_CONN_COMPLETE:
        printk("HCI_EV_SYNC_CONN_COMPLETE");
        break;
    case HCI_EV_SYNC_CONN_CHANGED:
        printk("HCI_EV_SYNC_CONN_CHANGED");
        break;
    case HCI_EV_SNIFF_SUBRATE:
        printk("HCI_EV_SNIFF_SUBRATE");
        break;
    case HCI_EV_EXTENDED_INQUIRY_RESULT:
        printk("HCI_EV_EXTENDED_INQUIRY_RESULT");
        break;
    case HCI_EV_IO_CAPA_REQUEST:
        printk("HCI_EV_IO_CAPA_REQUEST");
        break;
    case HCI_EV_SIMPLE_PAIR_COMPLETE:
        printk("HCI_EV_SIMPLE_PAIR_COMPLETE");
        break;
    case HCI_EV_REMOTE_HOST_FEATURES:
        printk("HCI_EV_REMOTE_HOST_FEATURES");
        break;
    default:
        printk("event");
        break;
    }
    printk(":%02x,len:%d,", *opcode,len);
    for (icount = 2; (icount < wlength) && (icount < 24); icount++)
        printk("%02x ", *(opcode+icount));
    printk("\n");
#endif
}

static inline ssize_t usb_put_user(struct sk_buff *skb,
        char __user *buf, int count)
{
    char __user *ptr = buf;
    int len = min_t(unsigned int, skb->len, count);

    if (copy_to_user(ptr, skb->data, len))
        return -EFAULT;

    return len;
}

static struct sk_buff *rtk_skb_queue[QUEUE_SIZE];
static int rtk_skb_queue_front = 0;
static int rtk_skb_queue_rear = 0;

static void rtk_enqueue(struct sk_buff *skb)
{
    spin_lock(&queue_lock);
    if (rtk_skb_queue_front == (rtk_skb_queue_rear + 1) % QUEUE_SIZE) {
        /*
         * If queue is full, current solution is to drop
         * the following entries.
         */
        RTKBT_WARN("%s: Queue is full, entry will be dropped", __func__);
    } else {
        rtk_skb_queue[rtk_skb_queue_rear] = skb;

        rtk_skb_queue_rear++;
        rtk_skb_queue_rear %= QUEUE_SIZE;

    }
    spin_unlock(&queue_lock);
}

static struct sk_buff *rtk_dequeue_try(unsigned int deq_len)
{
    struct sk_buff *skb;
    struct sk_buff *skb_copy;

    if (rtk_skb_queue_front == rtk_skb_queue_rear) {
        RTKBT_WARN("%s: Queue is empty", __func__);
        return NULL;
    }

    skb = rtk_skb_queue[rtk_skb_queue_front];
    if (deq_len >= skb->len) {

        rtk_skb_queue_front++;
        rtk_skb_queue_front %= QUEUE_SIZE;

        /*
         * Return skb addr to be dequeued, and the caller
         * should free the skb eventually.
         */
        return skb;
    } else {
        skb_copy = pskb_copy(skb, GFP_ATOMIC);
        skb_pull(skb, deq_len);
        /* Return its copy to be freed */
        return skb_copy;
    }
}

static inline int is_queue_empty(void)
{
    return (rtk_skb_queue_front == rtk_skb_queue_rear) ? 1 : 0;
}

/*
 * Realtek - Integrate from hci_core.c
 */

/* Get HCI device by index.
 * Device is held on return. */
static struct hci_dev *hci_dev_get(int index)
{
    if (index != 0)
        return NULL;

    return ghdev;
}

/* ---- HCI ioctl helpers ---- */
static int hci_dev_open(__u16 dev)
{
    struct hci_dev *hdev;
    int ret = 0;

    RTKBT_DBG("%s: dev %d", __func__, dev);

    hdev = hci_dev_get(dev);
    if (!hdev) {
        RTKBT_ERR("%s: Failed to get hci dev[Null]", __func__);
        return -ENODEV;
    }

    if (test_bit(HCI_UNREGISTER, &hdev->dev_flags)) {
        ret = -ENODEV;
        goto done;
    }

    if (test_bit(HCI_UP, &hdev->flags)) {
        ret = -EALREADY;
        goto done;
    }

done:
    return ret;
}

static int hci_dev_do_close(struct hci_dev *hdev)
{
    if (hdev->flush)
        hdev->flush(hdev);
    /* After this point our queues are empty
     * and no tasks are scheduled. */
    hdev->close(hdev);
    /* Clear flags */
    hdev->flags = 0;
    return 0;
}

static int hci_dev_close(__u16 dev)
{
    struct hci_dev *hdev;
    int err;
    hdev = hci_dev_get(dev);
    if (!hdev) {
        RTKBT_ERR("%s: failed to get hci dev[Null]", __func__);
        return -ENODEV;
    }

    err = hci_dev_do_close(hdev);

    return err;
}

static struct hci_dev *hci_alloc_dev(void)
{
    struct hci_dev *hdev;

    hdev = kzalloc(sizeof(struct hci_dev), GFP_KERNEL);
    if (!hdev)
        return NULL;

    return hdev;
}

/* Free HCI device */
static void hci_free_dev(struct hci_dev *hdev)
{
    kfree(hdev);
}

/* Register HCI device */
static int hci_register_dev(struct hci_dev *hdev)
{
    int i, id;

    RTKBT_DBG("%s: %p name %s bus %d", __func__, hdev, hdev->name, hdev->bus);
    /* Do not allow HCI_AMP devices to register at index 0,
     * so the index can be used as the AMP controller ID.
     */
    id = (hdev->dev_type == HCI_BREDR) ? 0 : 1;

    write_lock(&hci_dev_lock);

    sprintf(hdev->name, "hci%d", id);
    hdev->id = id;
    hdev->flags = 0;
    hdev->dev_flags = 0;
    mutex_init(&hdev->lock);

    RTKBT_DBG("%s: id %d, name %s", __func__, hdev->id, hdev->name);


    for (i = 0; i < NUM_REASSEMBLY; i++)
        hdev->reassembly[i] = NULL;

    memset(&hdev->stat, 0, sizeof(struct hci_dev_stats));
    atomic_set(&hdev->promisc, 0);

    if (ghdev) {
        write_unlock(&hci_dev_lock);
        RTKBT_ERR("%s: Hci device has been registered already", __func__);
        return -1;
    } else
        ghdev = hdev;

    write_unlock(&hci_dev_lock);

    return id;
}

/* Unregister HCI device */
static void hci_unregister_dev(struct hci_dev *hdev)
{
    int i;

    RTKBT_DBG("%s: hdev %p name %s bus %d", __func__, hdev, hdev->name, hdev->bus);
    set_bit(HCI_UNREGISTER, &hdev->dev_flags);

    write_lock(&hci_dev_lock);
    ghdev = NULL;
    write_unlock(&hci_dev_lock);

    hci_dev_do_close(hdev);
    for (i = 0; i < NUM_REASSEMBLY; i++)
        kfree_skb(hdev->reassembly[i]);
}

static void hci_send_to_stack(struct hci_dev *hdev, struct sk_buff *skb)
{
    struct sk_buff *rtk_skb_copy = NULL;

    RTKBT_DBG("%s", __func__);

    if (!hdev) {
        RTKBT_ERR("%s: Frame for unknown HCI device", __func__);
        return;
    }

    if (!test_bit(HCI_RUNNING, &hdev->flags)) {
        RTKBT_ERR("%s: HCI not running", __func__);
        return;
    }

    rtk_skb_copy = pskb_copy(skb, GFP_ATOMIC);
    if (!rtk_skb_copy) {
        RTKBT_ERR("%s: Copy skb error", __func__);
        return;
    }

    memcpy(skb_push(rtk_skb_copy, 1), &bt_cb(skb)->pkt_type, 1);
    rtk_enqueue(rtk_skb_copy);

    /* Make sure bt char device existing before wakeup read queue */
    hdev = hci_dev_get(0);
    if (hdev) {
        RTKBT_DBG("%s: Try to wakeup read queue", __func__);
        wake_up_interruptible(&btchr_read_wait);
    }

    return;
}

/* Receive frame from HCI drivers */
static int hci_recv_frame(struct sk_buff *skb)
{
    struct hci_dev *hdev = (struct hci_dev *) skb->dev;

    if (!hdev ||
        (!test_bit(HCI_UP, &hdev->flags) && !test_bit(HCI_INIT, &hdev->flags))) {
        kfree_skb(skb);
        return -ENXIO;
    }

    /* Incomming skb */
    bt_cb(skb)->incoming = 1;

    /* Time stamp */
    __net_timestamp(skb);

    if (atomic_read(&hdev->promisc)) {
        /* Send copy to the sockets */
        hci_send_to_stack(hdev, skb);
    }
    kfree_skb(skb);
    return 0;
}

static int hci_reassembly(struct hci_dev *hdev, int type, void *data,
                          int count, __u8 index)
{
    int len = 0;
    int hlen = 0;
    int remain = count;
    struct sk_buff *skb;
    struct bt_skb_cb *scb;

    RTKBT_DBG("%s", __func__);

    if ((type < HCI_ACLDATA_PKT || type > HCI_EVENT_PKT) ||
            index >= NUM_REASSEMBLY)
        return -EILSEQ;

    skb = hdev->reassembly[index];

    if (!skb) {
        switch (type) {
        case HCI_ACLDATA_PKT:
            len = HCI_MAX_FRAME_SIZE;
            hlen = HCI_ACL_HDR_SIZE;
            break;
        case HCI_EVENT_PKT:
            len = HCI_MAX_EVENT_SIZE;
            hlen = HCI_EVENT_HDR_SIZE;
            break;
        case HCI_SCODATA_PKT:
            len = HCI_MAX_SCO_SIZE;
            hlen = HCI_SCO_HDR_SIZE;
            break;
        }

        skb = bt_skb_alloc(len, GFP_ATOMIC);
        if (!skb)
            return -ENOMEM;

        scb = (void *) skb->cb;
        scb->expect = hlen;
        scb->pkt_type = type;

        skb->dev = (void *) hdev;
        hdev->reassembly[index] = skb;
    }

    while (count) {
        scb = (void *) skb->cb;
        len = min_t(uint, scb->expect, count);

        memcpy(skb_put(skb, len), data, len);

        count -= len;
        data += len;
        scb->expect -= len;
        remain = count;

        switch (type) {
        case HCI_EVENT_PKT:
            if (skb->len == HCI_EVENT_HDR_SIZE) {
                struct hci_event_hdr *h = hci_event_hdr(skb);
                scb->expect = h->plen;

                if (skb_tailroom(skb) < scb->expect) {
                    kfree_skb(skb);
                    hdev->reassembly[index] = NULL;
                    return -ENOMEM;
                }
            }
            break;

        case HCI_ACLDATA_PKT:
            if (skb->len  == HCI_ACL_HDR_SIZE) {
                struct hci_acl_hdr *h = hci_acl_hdr(skb);
                scb->expect = __le16_to_cpu(h->dlen);

                if (skb_tailroom(skb) < scb->expect) {
                    kfree_skb(skb);
                    hdev->reassembly[index] = NULL;
                    return -ENOMEM;
                }
            }
            break;

        case HCI_SCODATA_PKT:
            if (skb->len == HCI_SCO_HDR_SIZE) {
                struct hci_sco_hdr *h = hci_sco_hdr(skb);
                scb->expect = h->dlen;

                if (skb_tailroom(skb) < scb->expect) {
                    kfree_skb(skb);
                    hdev->reassembly[index] = NULL;
                    return -ENOMEM;
                }
            }
            break;
        }

        if (scb->expect == 0) {
            /* Complete frame */
            if(HCI_ACLDATA_PKT == type)
                print_acl(skb,0);
            if(HCI_SCODATA_PKT == type)
                print_sco(skb,0);
            if(HCI_EVENT_PKT == type)
                print_event(skb);

            bt_cb(skb)->pkt_type = type;
            hci_recv_frame(skb);

            hdev->reassembly[index] = NULL;
            return remain;
        }
    }

    return remain;
}

static int hci_recv_fragment(struct hci_dev *hdev, int type, void *data, int count)
{
    int rem = 0;

    if (type < HCI_ACLDATA_PKT || type > HCI_EVENT_PKT)
        return -EILSEQ;

    while (count) {
        rem = hci_reassembly(hdev, type, data, count, type - 1);
        if (rem < 0)
            return rem;

        data += (count - rem);
        count = rem;
    }

    return rem;
}

void hci_hardware_error(void)
{
    struct sk_buff *rtk_skb_copy = NULL;
    int len = 3;
    uint8_t hardware_err_pkt[4] = {HCI_EVENT_PKT, 0x10, 0x01, HCI_VENDOR_USB_DISC_HARDWARE_ERROR};

    rtk_skb_copy = alloc_skb(len, GFP_ATOMIC);
    if (!rtk_skb_copy) {
        RTKBT_ERR("%s: Failed to allocate mem", __func__);
        return;
    }

    memcpy(skb_put(rtk_skb_copy, len), hardware_err_pkt, len);
    rtk_enqueue(rtk_skb_copy);

    wake_up_interruptible(&btchr_read_wait);
}

static int btchr_open(struct inode *inode_p, struct file  *file_p)
{
    struct btusb_data *data;
    struct hci_dev *hdev;

    RTKBT_INFO("%s: BT usb char device is opening", __func__);
    /* Not open unless wanna tracing log */
    /* trace_printk("%s: open....\n", __func__); */

    hdev = hci_dev_get(0);
    if (!hdev) {
        RTKBT_ERR("%s: Failed to get hci dev[NULL]", __func__);
        return -ENODEV;
    }
    data = GET_DRV_DATA(hdev);

    atomic_inc(&hdev->promisc);
    /*
     * As bt device is not re-opened when hotplugged out, we cannot
     * trust on file's private data(may be null) when other file ops
     * are invoked.
     */
    file_p->private_data = data;

    mutex_lock(&btchr_mutex);
    hci_dev_open(0);
    mutex_unlock(&btchr_mutex);

    return nonseekable_open(inode_p, file_p);
}

static int btchr_close(struct inode  *inode_p, struct file   *file_p)
{
    struct btusb_data *data;
    struct hci_dev *hdev;

    RTKBT_INFO("%s: BT usb char device is closing", __func__);
    /* Not open unless wanna tracing log */
    /* trace_printk("%s: close....\n", __func__); */

    data = file_p->private_data;
    file_p->private_data = NULL;

#if CONFIG_BLUEDROID
    /*
     * If the upper layer closes bt char interfaces, no reset
     * action required even bt device hotplugged out.
     */
    bt_reset = 0;
#endif

    hdev = hci_dev_get(0);
    if (hdev) {
        atomic_set(&hdev->promisc, 0);
        mutex_lock(&btchr_mutex);
        hci_dev_close(0);
        mutex_unlock(&btchr_mutex);
    }

    return 0;
}

static ssize_t btchr_read(struct file *file_p,
        char __user *buf_p,
        size_t count,
        loff_t *pos_p)
{
    struct hci_dev *hdev;
    struct sk_buff *skb;
    ssize_t ret = 0;

    RTKBT_DBG("%s: BT usb char device is reading", __func__);

    while (count) {
        hdev = hci_dev_get(0);
        if (!hdev) {
            /*
             * Note: Only when BT device hotplugged out, we wil get
             * into such situation. In order to keep the upper layer
             * stack alive (blocking the read), we should never return
             * EFAULT or break the loop.
             */
            RTKBT_ERR("%s: Failed to get hci dev[Null]", __func__);
        }

        ret = wait_event_interruptible(btchr_read_wait, !is_queue_empty());
        if (ret < 0) {
            RTKBT_ERR("%s: wait event is signaled %d", __func__, (int)ret);
            break;
        }

        skb = rtk_dequeue_try(count);
        if (skb) {
            ret = usb_put_user(skb, buf_p, count);
            if (ret < 0)
                RTKBT_ERR("%s: Failed to put data to user space", __func__);
            kfree_skb(skb);
            break;
        }
    }

    return ret;
}

static ssize_t btchr_write(struct file *file_p,
        const char __user *buf_p,
        size_t count,
        loff_t *pos_p)
{
    struct btusb_data *data = file_p->private_data;
    struct hci_dev *hdev;
    struct sk_buff *skb;

    RTKBT_DBG("%s: BT usb char device is writing", __func__);

    hdev = hci_dev_get(0);
    if (!hdev) {
        RTKBT_WARN("%s: Failed to get hci dev[Null]", __func__);
        /*
         * Note: we bypass the data from the upper layer if bt device
         * is hotplugged out. Fortunatelly, H4 or H5 HCI stack does
         * NOT check btchr_write's return value. However, returning
         * count instead of EFAULT is preferable.
         */
        /* return -EFAULT; */
        return count;
    }

    /* Never trust on btusb_data, as bt device may be hotplugged out */
    data = GET_DRV_DATA(hdev);
    if (!data) {
        RTKBT_WARN("%s: Failed to get bt usb driver data[Null]", __func__);
        return count;
    }

    if (count > HCI_MAX_FRAME_SIZE)
        return -EINVAL;

    skb = bt_skb_alloc(count, GFP_ATOMIC);
    if (!skb)
        return -ENOMEM;
    skb_reserve(skb, -1); // Add this line

    if (copy_from_user(skb_put(skb, count), buf_p, count)) {
        RTKBT_ERR("%s: Failed to get data from user space", __func__);
        kfree_skb(skb);
        return -EFAULT;
    }

    skb->dev = (void *)hdev;
    bt_cb(skb)->pkt_type = *((__u8 *)skb->data);
    skb_pull(skb, 1);
    data->hdev->send(skb);

    return count;
}

static unsigned int btchr_poll(struct file *file_p, poll_table *wait)
{
    struct btusb_data *data = file_p->private_data;
    struct hci_dev *hdev;

    RTKBT_DBG("%s: BT usb char device is polling", __func__);

    poll_wait(file_p, &btchr_read_wait, wait);

    hdev = hci_dev_get(0);
    if (!hdev) {
        RTKBT_ERR("%s: Failed to get hci dev[Null]", __func__);
        mdelay(URB_CANCELING_DELAY_MS);
        return POLLOUT | POLLWRNORM;
    }

    /* Never trust on btusb_data, as bt device may be hotplugged out */
    data = GET_DRV_DATA(hdev);
    if (!data) {
        /*
         * When bt device is hotplugged out, btusb_data will
         * be freed in disconnect.
         */
        RTKBT_ERR("%s: Failed to get bt usb driver data[Null]", __func__);
        mdelay(URB_CANCELING_DELAY_MS);
        return POLLOUT | POLLWRNORM;
    }

    if (!is_queue_empty())
        return POLLIN | POLLRDNORM;

    return POLLOUT | POLLWRNORM;
}
static long btchr_ioctl(struct file *file_p,unsigned int cmd, unsigned long arg){
    int ret = 0;
    struct hci_dev *hdev;
    struct btusb_data *data;
    firmware_info *fw_info;

    if(check_set_dlfw_state_value(1) != 1) {
        RTKBT_ERR("%s bt controller is disconnecting!", __func__);
        return 0;
    }

    hdev = hci_dev_get(0);
    if(!hdev) {
        RTKBT_ERR("%s device is NULL!", __func__);
        set_dlfw_state_value(0);
        return 0;
    }
    data = GET_DRV_DATA(hdev);
    fw_info = data->fw_info;

    RTKBT_INFO(" btchr_ioctl DOWN_FW_CFG with Cmd:%d",cmd);
    switch (cmd) {
        case DOWN_FW_CFG:
            ret = usb_autopm_get_interface(data->intf);
            if (ret < 0){
                goto failed;
            }

            ret = download_patch(fw_info,1);
            usb_autopm_put_interface(data->intf);
            if(ret < 0){
                RTKBT_ERR("%s:Failed in download_patch with ret:%d",__func__,ret);
                goto failed;
            }

            ret = hdev->open(hdev);
            if(ret < 0){
                RTKBT_ERR("%s:Failed in hdev->open(hdev):%d",__func__,ret);
                goto failed;
            }
            set_bit(HCI_UP, &hdev->flags);
            set_dlfw_state_value(0);
            wake_up_interruptible(&bt_dlfw_wait);
            return 1;
        default:
            RTKBT_ERR("%s:Failed with wrong Cmd:%d",__func__,cmd);
            goto failed;
        }
    failed:
        set_dlfw_state_value(0);
        wake_up_interruptible(&bt_dlfw_wait);
        return ret;

}



static struct file_operations bt_chrdev_ops  = {
    open    :    btchr_open,
    release    :    btchr_close,
    read    :    btchr_read,
    write    :    btchr_write,
    poll    :    btchr_poll,
    unlocked_ioctl   :   btchr_ioctl,
};

static int btchr_init(void)
{
    int res = 0;
    struct device *dev;

    RTKBT_INFO("Register usb char device interface for BT driver");
    /*
     * btchr mutex is used to sync between
     * 1) downloading patch and opening bt char driver
     * 2) the file operations of bt char driver
     */
    mutex_init(&btchr_mutex);

    skb_queue_head_init(&btchr_readq);
    init_waitqueue_head(&btchr_read_wait);
    init_waitqueue_head(&bt_dlfw_wait);

    bt_char_class = class_create(THIS_MODULE, BT_CHAR_DEVICE_NAME);
    if (IS_ERR(bt_char_class)) {
        RTKBT_ERR("Failed to create bt char class");
        return PTR_ERR(bt_char_class);
    }

    res = alloc_chrdev_region(&bt_devid, 0, 1, BT_CHAR_DEVICE_NAME);
    if (res < 0) {
        RTKBT_ERR("Failed to allocate bt char device");
        goto err_alloc;
    }

    dev = device_create(bt_char_class, NULL, bt_devid, NULL, BT_CHAR_DEVICE_NAME);
    if (IS_ERR(dev)) {
        RTKBT_ERR("Failed to create bt char device");
        res = PTR_ERR(dev);
        goto err_create;
    }

    cdev_init(&bt_char_dev, &bt_chrdev_ops);
    res = cdev_add(&bt_char_dev, bt_devid, 1);
    if (res < 0) {
        RTKBT_ERR("Failed to add bt char device");
        goto err_add;
    }

    return 0;

err_add:
    device_destroy(bt_char_class, bt_devid);
err_create:
    unregister_chrdev_region(bt_devid, 1);
err_alloc:
    class_destroy(bt_char_class);
    return res;
}

static void btchr_exit(void)
{
    RTKBT_INFO("Unregister usb char device interface for BT driver");

    device_destroy(bt_char_class, bt_devid);
    cdev_del(&bt_char_dev);
    unregister_chrdev_region(bt_devid, 1);
    class_destroy(bt_char_class);

    return;
}
#endif

int send_hci_cmd(firmware_info *fw_info)
{
   int i = 0;
   int ret_val = -1;
   while((ret_val<0)&&(i++<10))
   {
       ret_val = usb_control_msg(
          fw_info->udev, fw_info->pipe_out,
          0, USB_TYPE_CLASS, 0, 0,
          (void *)(fw_info->send_pkt),
          fw_info->pkt_len, MSG_TO);
   }
   return ret_val;
}

int rcv_hci_evt(firmware_info *fw_info)
{
    int ret_len = 0, ret_val = 0;
    int i;

    while (1) {
        for(i = 0; i < 5; i++) {
        ret_val = usb_interrupt_msg(
            fw_info->udev, fw_info->pipe_in,
            (void *)(fw_info->rcv_pkt), PKT_LEN,
            &ret_len, MSG_TO);
            if (ret_val >= 0)
                break;
        }

        if (ret_val < 0)
            return ret_val;

        if (CMD_CMP_EVT == fw_info->evt_hdr->evt) {
            if (fw_info->cmd_hdr->opcode == fw_info->cmd_cmp->opcode)
                return ret_len;
        }
    }
}

int set_bt_onoff(firmware_info *fw_info, uint8_t onoff)
{
    patch_info *patch_entry;
    int ret_val;

    RTKBT_INFO("%s: %s", __func__, onoff != 0 ? "on" : "off");

    patch_entry = fw_info->patch_entry;
    if (!patch_entry)
        return -1;

    fw_info->cmd_hdr->opcode = cpu_to_le16(BTOFF_OPCODE);
    fw_info->cmd_hdr->plen = 1;
    fw_info->pkt_len = CMD_HDR_LEN + 1;
    fw_info->send_pkt[CMD_HDR_LEN] = onoff;

    ret_val = send_hci_cmd(fw_info);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to send bt %s cmd, errno %d",
                __func__, onoff != 0 ? "on" : "off", ret_val);
        return ret_val;
    }

    ret_val = rcv_hci_evt(fw_info);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to receive bt %s event, errno %d",
                __func__, onoff != 0 ? "on" : "off", ret_val);
        return ret_val;
    }

    return ret_val;
}

static patch_info *get_fw_table_entry(struct usb_device* udev)
{
    patch_info *patch_entry = fw_patch_table;
    uint16_t vid = le16_to_cpu(udev->descriptor.idVendor);
    uint16_t pid = le16_to_cpu(udev->descriptor.idProduct);
    uint32_t entry_size = sizeof(fw_patch_table) / sizeof(fw_patch_table[0]);
    uint32_t i;

    RTKBT_INFO("%s: Product id = 0x%04x, fw table entry size %d", __func__, pid, entry_size);

    for (i = 0; i < entry_size; i++, patch_entry++) {
        if ((vid == patch_entry->vid)&&(pid == patch_entry->pid))
            break;
    }

    if (i == entry_size) {
        RTKBT_ERR("%s: No fw table entry found", __func__);
        return NULL;
    }

    return patch_entry;
}

#if SUSPNED_DW_FW
static patch_info *get_suspend_fw_table_entry(struct usb_device* udev)
{
    patch_info *patch_entry = fw_patch_table;
    uint16_t vid = le16_to_cpu(udev->descriptor.idVendor);
    uint16_t pid = le16_to_cpu(udev->descriptor.idProduct);
    uint32_t entry_size = sizeof(fw_patch_table) / sizeof(fw_patch_table[0]);
    uint32_t i;

    RTKBT_INFO("%s: Product id = 0x%04x, fw table entry size %d", __func__, pid, entry_size);

    for (i = 0; i < entry_size; i++, patch_entry++) {
        if ((vid == patch_entry->vid)&&(pid == patch_entry->pid))
            break;
    }

    if (i == entry_size) {
        RTKBT_ERR("%s: No fw table entry found", __func__);
        return NULL;
    }

    return patch_entry;
}
#endif

static struct rtk_epatch_entry *get_fw_patch_entry(struct rtk_epatch *epatch_info, uint16_t eco_ver)
{
    int patch_num = epatch_info->number_of_total_patch;
    uint8_t *epatch_buf = (uint8_t *)epatch_info;
    struct rtk_epatch_entry *p_entry = NULL;
    int coex_date;
    int coex_ver;
    int i;

    for (i = 0; i < patch_num; i++) {
        if (*(uint16_t *)(epatch_buf + 14 + 2*i) == eco_ver + 1) {
            p_entry = kzalloc(sizeof(*p_entry), GFP_KERNEL);
            if (!p_entry) {
                RTKBT_ERR("%s: Failed to allocate mem for patch entry", __func__);
                return NULL;
            }
            p_entry->chip_id = eco_ver + 1;
            p_entry->patch_length = *(uint16_t*)(epatch_buf + 14 + 2*patch_num + 2*i);
            p_entry->start_offset = *(uint32_t*)(epatch_buf + 14 + 4*patch_num + 4*i);
            p_entry->coex_version = *(uint32_t*)(epatch_buf + p_entry->start_offset + p_entry->patch_length - 12);
            p_entry->svn_version = *(uint32_t*)(epatch_buf + p_entry->start_offset + p_entry->patch_length - 8);
            p_entry->fw_version = *(uint32_t*)(epatch_buf + p_entry->start_offset + p_entry->patch_length - 4);

            coex_date = ((p_entry->coex_version >> 16) & 0x7ff) + ((p_entry->coex_version >> 27) * 10000);
            coex_ver = p_entry->coex_version & 0xffff;

            RTKBT_INFO("BTCOEX:20%06d-0x%04x svn version:0x%08x fw version:0x%08x rtk_btusb version:%s Cut:%d, patch length:0x%04x, patch offset:0x%08x\n", \
                    coex_date, coex_ver, p_entry->svn_version, p_entry->fw_version, VERSION, p_entry->chip_id, p_entry->patch_length, p_entry->start_offset);
            break;
        }
    }

    return p_entry;
}

/*reset_controller is aimed to reset_bt_fw before updata Fw patch*/
int reset_controller(firmware_info* fw_info)
{
    int ret_val;
    RTKBT_ERR("reset_controller");

    if (!fw_info)
        return -ENODEV;

    fw_info->cmd_hdr->opcode = cpu_to_le16(HCI_VENDOR_FORCE_RESET_AND_PATCHABLE);
    fw_info->cmd_hdr->plen = 0;
    fw_info->pkt_len = CMD_HDR_LEN;
    ret_val = send_hci_cmd(fw_info);

    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to send hci cmd 0x%04x, errno %d",
                __func__, fw_info->cmd_hdr->opcode, ret_val);
        return ret_val;
    }

    //sleep 1s for firmware reset.
    msleep(1000);
    RTKBT_INFO("%s: Wait fw reset for 1ms",__func__);

    return ret_val;
}
/*reset_controller is aimed to reset_bt_fw before updata Fw patch*/

/*
 * check the return value
 * 1: need to download fw patch
 * 0: no need to download fw patch
 * <0: failed to check lmp version
 */
int check_fw_version(firmware_info* fw_info)
{
    struct hci_rp_read_local_version *read_ver_rsp;
    patch_info *patch_entry = NULL;
    int ret_val = -1;

    fw_info->cmd_hdr->opcode = cpu_to_le16(HCI_OP_READ_LOCAL_VERSION);
    fw_info->cmd_hdr->plen = 0;
    fw_info->pkt_len = CMD_HDR_LEN;

    ret_val = send_hci_cmd(fw_info);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to send hci cmd 0x%04x, errno %d",
                __func__, fw_info->cmd_hdr->opcode, ret_val);
        return ret_val;
    }

    ret_val = rcv_hci_evt(fw_info);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to receive hci event, errno %d",
                __func__, ret_val);
        return ret_val;
    }

    patch_entry = fw_info->patch_entry;
    read_ver_rsp = (struct hci_rp_read_local_version *)(fw_info->rsp_para);

    RTKBT_INFO("%s: Controller lmp = 0x%04x, patch lmp = 0x%04x, default patch lmp = 0x%04x",
            __func__, read_ver_rsp->lmp_subver, patch_entry->lmp_sub, patch_entry->lmp_sub_default);

    if (read_ver_rsp->lmp_subver == patch_entry->lmp_sub_default) {
        RTKBT_INFO("%s: Cold BT controller startup", __func__);

        return 2;

    } else if (read_ver_rsp->lmp_subver != patch_entry->lmp_sub) {
        RTKBT_INFO("%s: Warm BT controller startup with updated lmp", __func__);
        return 1;
    } else {
        RTKBT_INFO("%s: Warm BT controller startup with same lmp", __func__);
        return 0;
    }
}

#if SET_WAKEUP_DEVICE
int set_wakeup_device(firmware_info* fw_info, uint8_t* wakeup_bdaddr)
{
    struct rtk_eversion_evt *ever_evt;
    int ret_val;

    if (!fw_info)
        return -ENODEV;

    fw_info->cmd_hdr->opcode = cpu_to_le16(HCI_VENDOR_ADD_WAKE_UP_DEVICE);
    fw_info->cmd_hdr->plen = 7;
    memcpy(fw_info->req_para, wakeup_bdaddr, 7);
    fw_info->pkt_len = CMD_HDR_LEN + 7;

    ret_val = send_hci_cmd(fw_info);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to send hci cmd 0x%04x, errno %d\n",
            __func__, fw_info->cmd_hdr->opcode, ret_val);
        return ret_val;
    }

    ret_val = rcv_hci_evt(fw_info);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to receive hci event, errno %d\n",__func__, ret_val);
        return ret_val;
    }

    ever_evt = (struct rtk_eversion_evt *)(fw_info->rsp_para);

    RTKBT_DBG("%s: status %d, eversion %d", __func__, ever_evt->status, ever_evt->version);
    return ret_val;
}
#endif

/*reset_channel to recover the communication between wifi 8192eu with 8761 bt controller in case of geteversion error*/

int reset_channel(firmware_info* fw_info)
{
    struct rtk_reset_evt *ever_evt;
    int ret_val;

    if (!fw_info)
        return -ENODEV;

    fw_info->cmd_hdr->opcode = cpu_to_le16(HCI_VENDOR_RESET);
    fw_info->cmd_hdr->plen = 0;
    fw_info->pkt_len = CMD_HDR_LEN;

    ret_val = send_hci_cmd(fw_info);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to send  hci cmd 0x%04x, errno %d",
                __func__, fw_info->cmd_hdr->opcode, ret_val);
        return ret_val;
    }

    ret_val = rcv_hci_evt(fw_info);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to receive  hci event, errno %d",
                __func__, ret_val);
        return ret_val;
    }

    ever_evt = (struct rtk_reset_evt *)(fw_info->rsp_para);

    RTKBT_INFO("%s: status %d ", __func__, ever_evt->status);

    //sleep 300ms for channel reset.
    msleep(300);
    RTKBT_INFO("%s: Wait channel reset for 300ms",__func__);

    return ret_val;
}

int read_localversion(firmware_info* fw_info)
{
    struct rtk_localversion_evt *ever_evt;
    int ret_val;

    if (!fw_info)
        return -ENODEV;

    fw_info->cmd_hdr->opcode = cpu_to_le16(HCI_VENDOR_READ_LMP_VERISION);
    fw_info->cmd_hdr->plen = 0;
    fw_info->pkt_len = CMD_HDR_LEN;

    ret_val = send_hci_cmd(fw_info);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to send  hci cmd 0x%04x, errno %d",
                __func__, fw_info->cmd_hdr->opcode, ret_val);
        return ret_val;
    }

    ret_val = rcv_hci_evt(fw_info);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to receive  hci event, errno %d",
                __func__, ret_val);
        return ret_val;
    }

    ever_evt = (struct rtk_localversion_evt *)(fw_info->rsp_para);

    RTKBT_INFO("%s: status %d ", __func__, ever_evt->status);
    RTKBT_INFO("%s: hci_version %d ", __func__, ever_evt->hci_version);
    RTKBT_INFO("%s: hci_revision %d ", __func__, ever_evt->hci_revision);
    RTKBT_INFO("%s: lmp_version %d ", __func__, ever_evt->lmp_version);
    RTKBT_INFO("%s: lmp_subversion %d ", __func__, ever_evt->lmp_subversion);
    RTKBT_INFO("%s: lmp_manufacture %d ", __func__, ever_evt->lmp_manufacture);
    //sleep 300ms for channel reset.
    msleep(300);
    RTKBT_INFO("%s: Wait channel reset for 300ms",__func__);

    return ret_val;
}

int get_eversion(firmware_info* fw_info)
{
    struct rtk_eversion_evt *ever_evt;
    int ret_val;

    if (!fw_info)
        return -ENODEV;

    fw_info->cmd_hdr->opcode = cpu_to_le16(HCI_VENDOR_READ_RTK_ROM_VERISION);
    fw_info->cmd_hdr->plen = 0;
    fw_info->pkt_len = CMD_HDR_LEN;

    ret_val = send_hci_cmd(fw_info);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to send hci cmd 0x%04x, errno %d",
                __func__, fw_info->cmd_hdr->opcode, ret_val);
        return ret_val;
    }

    ret_val = rcv_hci_evt(fw_info);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to receive hci event, errno %d",
                __func__, ret_val);
        return ret_val;
    }

    ever_evt = (struct rtk_eversion_evt *)(fw_info->rsp_para);

    RTKBT_INFO("%s: status %d, eversion %d", __func__, ever_evt->status, ever_evt->version);

    if (ever_evt->status)
        fw_info->patch_entry->eversion = 0;
    else
        fw_info->patch_entry->eversion = ever_evt->version;

    return ret_val;
}

void rtk_update_altsettings(patch_info *patch_entry, const unsigned char* org_config_buf, int org_config_len, unsigned char ** new_config_buf_ptr, int *new_config_len_ptr)
{
    static unsigned char config_buf[1024];
    unsigned short offset[256];
    unsigned char val[256];

    struct rtk_bt_vendor_config* config = (struct rtk_bt_vendor_config*) config_buf;
    struct rtk_bt_vendor_config_entry* entry = config->entry;

    int count = 0,temp = 0, i = 0, j;

    memset(config_buf, 0, sizeof(config_buf));
    memset(offset, 0, sizeof(offset));
    memset(val, 0, sizeof(val));

    memcpy(config_buf, org_config_buf, org_config_len);
    *new_config_buf_ptr = config_buf;
    *new_config_len_ptr = org_config_len;

    count = getAltSettings(patch_entry, offset, sizeof(offset)/sizeof(unsigned short));
    if(count <= 0){
        RTKBT_INFO("rtk_update_altsettings: No AltSettings");
        return;
    }else{
        RTKBT_INFO("rtk_update_altsettings: %d AltSettings", count);
    }

    RTKBT_INFO("ORG Config len=%08x:\n", org_config_len);
    for(i=0;i<=org_config_len;i+=0x10)
    {
        RTKBT_INFO("%08x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n", i, \
            config_buf[i], config_buf[i+1], config_buf[i+2], config_buf[i+3], config_buf[i+4], config_buf[i+5], config_buf[i+6], config_buf[i+7], \
            config_buf[i+8], config_buf[i+9], config_buf[i+10], config_buf[i+11], config_buf[i+12], config_buf[i+13], config_buf[i+14], config_buf[i+15]);
    }

    if (config->data_len != org_config_len - sizeof(struct rtk_bt_vendor_config))
    {
        RTKBT_ERR("rtk_update_altsettings: config len(%x) is not right(%x)", config->data_len, (unsigned int)(org_config_len-sizeof(struct rtk_bt_vendor_config)));
        return;
    }

    for (i=0; i<config->data_len;)
    {
        for(j = 0; j < count;j++)
        {
            if(entry->offset == offset[j])
                offset[j] = 0;
        }
        if(getAltSettingVal(patch_entry, entry->offset, val) == entry->entry_len){
            RTKBT_INFO("rtk_update_altsettings: replace %04x[%02x]", entry->offset, entry->entry_len);
            memcpy(entry->entry_data, val, entry->entry_len);
        }
        temp = entry->entry_len + sizeof(struct rtk_bt_vendor_config_entry);
        i += temp;
        entry = (struct rtk_bt_vendor_config_entry*)((uint8_t*)entry + temp);
    }
    for(j = 0; j < count;j++){
        if(offset[j] == 0)
            continue;
        entry->entry_len = getAltSettingVal(patch_entry, offset[j], val);
        if(entry->entry_len <= 0)
            continue;
        entry->offset = offset[j];
        memcpy(entry->entry_data, val, entry->entry_len);
        RTKBT_INFO("rtk_update_altsettings: add %04x[%02x]", entry->offset, entry->entry_len);
        temp = entry->entry_len + sizeof(struct rtk_bt_vendor_config_entry);
        i += temp;
        entry = (struct rtk_bt_vendor_config_entry*)((uint8_t*)entry + temp);
    }
    config->data_len = i;
    *new_config_buf_ptr = config_buf;
    *new_config_len_ptr = config->data_len+sizeof(struct rtk_bt_vendor_config);

    return;
}

int load_firmware(firmware_info *fw_info, uint8_t **buff)
{
    const struct firmware *fw, *cfg;
    struct usb_device *udev;
    patch_info *patch_entry;
    char *config_name, *fw_name;
    int fw_len = 0;
    int ret_val;

    int config_len = 0, buf_len = -1;
    uint8_t *buf = *buff, *config_file_buf = NULL;
    uint8_t *epatch_buf = NULL;

    struct rtk_epatch *epatch_info = NULL;
    uint8_t need_download_fw = 1;
    struct rtk_extension_entry patch_lmp = {0};
    struct rtk_epatch_entry *p_epatch_entry = NULL;
    uint16_t lmp_version;
    //uint8_t use_mp_fw = 0;
    RTKBT_DBG("%s: start", __func__);

    udev = fw_info->udev;
    patch_entry = fw_info->patch_entry;
    lmp_version = patch_entry->lmp_sub_default;
    config_name = patch_entry->config_name;
/* 1 Mptool Fw; 0 Normal Fw */
    if(DRV_MP_MODE == mp_drv_mode){
        fw_name = patch_entry->mp_patch_name;
    }else{
        fw_name = patch_entry->patch_name;
    }

    RTKBT_INFO("%s: Default lmp version = 0x%04x, config file name[%s], "
            "fw file name[%s]", __func__, lmp_version,config_name, fw_name);

    ret_val = request_firmware(&cfg, config_name, &udev->dev);
    if (ret_val < 0)
        config_len = 0;
    else {
        int i;
        rtk_update_altsettings(patch_entry, cfg->data, cfg->size, &config_file_buf, &config_len);

        RTKBT_INFO("Final Config len=%08x:\n", config_len);
        for(i=0;i<=config_len;i+=0x10)
        {
            RTKBT_INFO("%08x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n", i, \
                config_file_buf[i], config_file_buf[i+1], config_file_buf[i+2], config_file_buf[i+3], config_file_buf[i+4], config_file_buf[i+5], config_file_buf[i+6], config_file_buf[i+7], \
                config_file_buf[i+8], config_file_buf[i+9], config_file_buf[i+10], config_file_buf[i+11], config_file_buf[i+12], config_file_buf[i+13], config_file_buf[i+14], config_file_buf[i+15]);
        }

        release_firmware(cfg);
    }

    ret_val = request_firmware(&fw, fw_name, &udev->dev);
    if (ret_val < 0)
        goto fw_fail;
    else {
        epatch_buf = vmalloc(fw->size);
        RTKBT_INFO("%s: epatch_buf = vmalloc(fw->size, GFP_KERNEL)", __func__);
        if (!epatch_buf) {
            release_firmware(fw);
            goto fw_fail;
        }
        memcpy(epatch_buf, fw->data, fw->size);
        fw_len = fw->size;
        buf_len = fw_len + config_len;
        release_firmware(fw);
    }

    if (lmp_version == ROM_LMP_8723a) {
        RTKBT_DBG("%s: 8723a -> use old style patch", __func__);
        if (!memcmp(epatch_buf, RTK_EPATCH_SIGNATURE, 8)) {
            RTKBT_ERR("%s: 8723a check signature error", __func__);
            need_download_fw = 0;
        } else {
            if (!(buf = kzalloc(buf_len, GFP_KERNEL))) {
                RTKBT_ERR("%s: Failed to allocate mem for fw&config", __func__);
                buf_len = -1;
            } else {
                RTKBT_DBG("%s: 8723a -> fw copy directly", __func__);
                memcpy(buf, epatch_buf, buf_len);
                patch_entry->lmp_sub = *(uint16_t *)(buf + buf_len - config_len - 4);
                RTKBT_DBG("%s: Config lmp version = 0x%04x", __func__,
                        patch_entry->lmp_sub);
                vfree(epatch_buf);
                RTKBT_INFO("%s:ROM_LMP_8723a vfree(epatch_buf)", __func__);
                epatch_buf = NULL;
                if (config_len)
                    memcpy(buf + buf_len - config_len, config_file_buf, config_len);
            }
        }
    } else {
        RTKBT_DBG("%s: Not 8723a -> use new style patch", __func__);

        RTKBT_DBG("%s: reset_channel before get_eversion from bt controller", __func__);
        ret_val = reset_channel(fw_info);
        if (ret_val < 0) {
            RTKBT_ERR("%s: Failed to reset_channel, errno %d", __func__, ret_val);
            goto fw_fail;
        }
//        read_localversion(fw_info);
        RTKBT_DBG("%s: get_eversion from bt controller", __func__);

        ret_val = get_eversion(fw_info);
        if (ret_val < 0) {
            RTKBT_ERR("%s: Failed to get eversion, errno %d", __func__, ret_val);
            goto fw_fail;
        }
        RTKBT_DBG("%s: Get eversion =%d", __func__, patch_entry->eversion);
        if (memcmp(epatch_buf + buf_len - config_len - 4 , EXTENSION_SECTION_SIGNATURE, 4)) {
            RTKBT_ERR("%s: Failed to check extension section signature", __func__);
            need_download_fw = 0;
        } else {
            uint8_t *temp;
            temp = epatch_buf+buf_len-config_len - 5;
            do {
                if (*temp == 0x00) {
                    patch_lmp.opcode = *temp;
                    patch_lmp.length = *(temp-1);
                    if ((patch_lmp.data = kzalloc(patch_lmp.length, GFP_KERNEL))) {
                        int k;
                        for (k = 0; k < patch_lmp.length; k++) {
                            *(patch_lmp.data+k) = *(temp-2-k);
                            RTKBT_DBG("data = 0x%x", *(patch_lmp.data+k));
                        }
                    }
                    RTKBT_DBG("%s: opcode = 0x%x, length = 0x%x, data = 0x%x", __func__,
                            patch_lmp.opcode, patch_lmp.length, *(patch_lmp.data));
                    break;
                }
                temp -= *(temp-1) + 2;
            } while (*temp != 0xFF);

            if (lmp_version != project_id[*(patch_lmp.data)]) {
                RTKBT_ERR("%s: Default lmp_version 0x%04x, project_id[%d] 0x%04x "
                        "-> not match", __func__, lmp_version, *(patch_lmp.data),project_id[*(patch_lmp.data)]);
                if (patch_lmp.data)
                    kfree(patch_lmp.data);
                need_download_fw = 0;
            } else {
                RTKBT_INFO("%s: Default lmp_version 0x%04x, project_id[%d] 0x%04x "
                        "-> match", __func__, lmp_version, *(patch_lmp.data), project_id[*(patch_lmp.data)]);
                if (patch_lmp.data)
                    kfree(patch_lmp.data);
                if (memcmp(epatch_buf, RTK_EPATCH_SIGNATURE, 8)) {
                    RTKBT_ERR("%s: Check signature error", __func__);
                    need_download_fw = 0;
                } else {
                    epatch_info = (struct rtk_epatch*)epatch_buf;
                    patch_entry->lmp_sub = (uint16_t)epatch_info->fw_version;

                    RTKBT_DBG("%s: lmp version 0x%04x, fw_version 0x%x, "
                            "number_of_total_patch %d", __func__,
                            patch_entry->lmp_sub, epatch_info->fw_version,
                            epatch_info->number_of_total_patch);

                    /* Get right epatch entry */
                    p_epatch_entry = get_fw_patch_entry(epatch_info, patch_entry->eversion);
                    if (p_epatch_entry == NULL) {
                        RTKBT_WARN("%s: Failed to get fw patch entry", __func__);
                        ret_val = -1;
                        goto fw_fail ;
                    }

                    buf_len = p_epatch_entry->patch_length + config_len;
                    RTKBT_DBG("buf_len = 0x%x", buf_len);

                    if (!(buf = kzalloc(buf_len, GFP_KERNEL))) {
                        RTKBT_ERR("%s: Can't alloc memory for  fw&config", __func__);
                        buf_len = -1;
                    } else {
                        memcpy(buf, &epatch_buf[p_epatch_entry->start_offset], p_epatch_entry->patch_length);
                        memcpy(&buf[p_epatch_entry->patch_length-4], &epatch_info->fw_version, 4);
                        kfree(p_epatch_entry);
                    }
                    vfree(epatch_buf);
                    RTKBT_INFO("%s: vfree(epatch_buf)", __func__);
                    epatch_buf = NULL;

                    if (config_len)
                        memcpy(&buf[buf_len - config_len], config_file_buf, config_len);
                }
            }
        }
    }

    RTKBT_INFO("%s: fw%s exists, config file%s exists", __func__,
            (buf_len > 0) ? "" : " not", (config_len > 0) ? "":" not");

    if (buf && buf_len > 0 && need_download_fw)
        *buff = buf;

    RTKBT_DBG("%s: done", __func__);

    return buf_len;

fw_fail:
    return ret_val;
}

#if SUSPNED_DW_FW
static int load_suspend_firmware(firmware_info *fw_info, uint8_t **buff)
{
    const struct firmware *fw, *cfg;
    struct usb_device *udev;
    patch_info *patch_entry;
    char config_name[100] = {0};
    char fw_name[100] = {0};
    int fw_len = 0;
    int ret_val;

    int config_len = 0, buf_len = -1;
    uint8_t *buf = *buff, *config_file_buf = NULL;
    uint8_t *epatch_buf = NULL;

    struct rtk_epatch *epatch_info = NULL;
    uint8_t need_download_fw = 1;
    struct rtk_extension_entry patch_lmp = {0};
    struct rtk_epatch_entry *p_epatch_entry = NULL;
    uint16_t lmp_version;
    RTKBT_DBG("%s: start", __func__);

    udev = fw_info->udev;
    patch_entry = fw_info->patch_entry;
    lmp_version = patch_entry->lmp_sub_default;
    sprintf(config_name, "%s_suspend", patch_entry->config_name);
    sprintf(fw_name, "%s_suspend", patch_entry->patch_name);

    RTKBT_INFO("%s: Default lmp version = 0x%04x, config file name[%s], "
            "fw file name[%s]", __func__, lmp_version,config_name, fw_name);

    ret_val = request_firmware(&cfg, config_name, &udev->dev);
    if (ret_val < 0)
        config_len = 0;
    else {
        config_file_buf = vmalloc(cfg->size);
        RTKBT_INFO("%s: epatch_buf = vmalloc(cfg->size)", __func__);
        if (!config_file_buf)
            return -ENOMEM;
        memcpy(config_file_buf, cfg->data, cfg->size);
        config_len = cfg->size;
        release_firmware(cfg);
    }

    ret_val = request_firmware(&fw, fw_name, &udev->dev);
    if (ret_val < 0)
        goto fw_fail;
    else {
        epatch_buf = vmalloc(fw->size);
        RTKBT_INFO("%s: epatch_buf = vmalloc(fw->size, GFP_KERNEL)", __func__);
        if (!epatch_buf) {
            release_firmware(fw);
            goto fw_fail;
        }
        memcpy(epatch_buf, fw->data, fw->size);
        fw_len = fw->size;
        buf_len = fw_len + config_len;
        release_firmware(fw);
    }

    RTKBT_DBG("%s: Not 8723a -> use new style patch", __func__);

    RTKBT_DBG("%s: get_eversion from bt controller", __func__);

    ret_val = get_eversion(fw_info);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to get eversion, errno %d", __func__, ret_val);
        goto fw_fail;
    }
    RTKBT_DBG("%s: Get eversion =%d", __func__, patch_entry->eversion);
    if (memcmp(epatch_buf + buf_len - config_len - 4 , EXTENSION_SECTION_SIGNATURE, 4)) {
        RTKBT_ERR("%s: Failed to check extension section signature", __func__);
        need_download_fw = 0;
    } else {
        uint8_t *temp;
        temp = epatch_buf+buf_len-config_len - 5;
        do {
            if (*temp == 0x00) {
                patch_lmp.opcode = *temp;
                patch_lmp.length = *(temp-1);
                if ((patch_lmp.data = kzalloc(patch_lmp.length, GFP_KERNEL))) {
                    int k;
                    for (k = 0; k < patch_lmp.length; k++) {
                        *(patch_lmp.data+k) = *(temp-2-k);
                        RTKBT_DBG("data = 0x%x", *(patch_lmp.data+k));
                    }
                }
                RTKBT_DBG("%s: opcode = 0x%x, length = 0x%x, data = 0x%x", __func__,
                    patch_lmp.opcode, patch_lmp.length, *(patch_lmp.data));
                break;
            }
            temp -= *(temp-1) + 2;
        } while (*temp != 0xFF);

        if (lmp_version != project_id[*(patch_lmp.data)]) {
            RTKBT_ERR("%s: Default lmp_version 0x%04x, project_id[%d] 0x%04x "
                "-> not match", __func__, lmp_version, *(patch_lmp.data),project_id[*(patch_lmp.data)]);
            if (patch_lmp.data)
                kfree(patch_lmp.data);
            need_download_fw = 0;
        } else {
            RTKBT_INFO("%s: Default lmp_version 0x%04x, project_id[%d] 0x%04x "
                "-> match", __func__, lmp_version, *(patch_lmp.data), project_id[*(patch_lmp.data)]);
            if (patch_lmp.data)
                kfree(patch_lmp.data);
            if (memcmp(epatch_buf, RTK_EPATCH_SIGNATURE, 8)) {
                RTKBT_ERR("%s: Check signature error", __func__);
                need_download_fw = 0;
            } else {
                epatch_info = (struct rtk_epatch*)epatch_buf;
                patch_entry->lmp_sub = (uint16_t)epatch_info->fw_version;

                RTKBT_DBG("%s: lmp version 0x%04x, fw_version 0x%x, "
                    "number_of_total_patch %d", __func__,
                    patch_entry->lmp_sub, epatch_info->fw_version,
                    epatch_info->number_of_total_patch);

             /* Get right epatch entry */
                p_epatch_entry = get_fw_patch_entry(epatch_info, patch_entry->eversion);
                if (p_epatch_entry == NULL) {
                    RTKBT_WARN("%s: Failed to get fw patch entry", __func__);
                    ret_val = -1;
                    goto fw_fail ;
                }

                buf_len = p_epatch_entry->patch_length + config_len;
                RTKBT_DBG("buf_len = 0x%x", buf_len);

                if (!(buf = kzalloc(buf_len, GFP_KERNEL))) {
                    RTKBT_ERR("%s: Can't alloc memory for  fw&config", __func__);
                    buf_len = -1;
                } else {
                    memcpy(buf, &epatch_buf[p_epatch_entry->start_offset], p_epatch_entry->patch_length);
                    memcpy(&buf[p_epatch_entry->patch_length-4], &epatch_info->fw_version, 4);
                    kfree(p_epatch_entry);
                }
                vfree(epatch_buf);
                RTKBT_INFO("%s: vfree(epatch_buf)", __func__);
                epatch_buf = NULL;

                if (config_len)
                    memcpy(&buf[buf_len - config_len], config_file_buf, config_len);
            }
        }
    }

    if (config_file_buf){
        vfree(config_file_buf);
        config_file_buf = NULL;
        RTKBT_INFO("%s: vfree(config_file_buf)", __func__);
        }

    RTKBT_INFO("%s: fw%s exists, config file%s exists", __func__,
            (buf_len > 0) ? "" : " not", (config_len > 0) ? "":" not");

    if (buf && buf_len > 0 && need_download_fw)
        *buff = buf;

    RTKBT_DBG("%s: done", __func__);

    return buf_len;

fw_fail:
    if (config_file_buf){
        vfree(config_file_buf);
        config_file_buf = NULL;
        }
    RTKBT_INFO("%s: fw_fail vfree(config_file_buf)", __func__);
    return ret_val;
}
#endif

int get_firmware(firmware_info *fw_info, int cached)
{
    patch_info *patch_entry = fw_info->patch_entry;

    RTKBT_INFO("%s: start, cached %d,patch_entry->fw_len= %d", __func__, cached,patch_entry->fw_len);

    if (cached > 0) {
        if (patch_entry->fw_len > 0) {
            fw_info->fw_data = kzalloc(patch_entry->fw_len, GFP_KERNEL);
            if (!fw_info->fw_data)
                return -ENOMEM;
            memcpy(fw_info->fw_data, patch_entry->fw_cache, patch_entry->fw_len);
            fw_info->fw_len = patch_entry->fw_len;
        } else {
            fw_info->fw_len = load_firmware(fw_info, &fw_info->fw_data);
            if (fw_info->fw_len <= 0)
                return -1;
        }
    } else {
        fw_info->fw_len = load_firmware(fw_info, &fw_info->fw_data);
        if (fw_info->fw_len <= 0)
            return -1;
    }

    return 0;
}

#if SUSPNED_DW_FW
static int get_suspend_firmware(firmware_info *fw_info, int cached)
{
    patch_info *patch_entry = fw_info->patch_entry;

    RTKBT_INFO("%s: start, cached %d,patch_entry->fw_len= %d", __func__, cached,patch_entry->fw_len);

    if (cached > 0) {
        if (patch_entry->fw_len > 0) {
            fw_info->fw_data = kzalloc(patch_entry->fw_len, GFP_KERNEL);
            if (!fw_info->fw_data)
                return -ENOMEM;
            memcpy(fw_info->fw_data, patch_entry->fw_cache, patch_entry->fw_len);
            fw_info->fw_len = patch_entry->fw_len;
        } else {
            fw_info->fw_len = load_suspend_firmware(fw_info, &fw_info->fw_data);
            if (fw_info->fw_len <= 0)
                return -1;
        }
    } else {
        fw_info->fw_len = load_suspend_firmware(fw_info, &fw_info->fw_data);
        if (fw_info->fw_len <= 0)
            return -1;
    }

    return 0;
}
#endif

/*
 * Open the log message only if in debugging,
 * or it will decelerate download procedure.
 */
int download_data(firmware_info *fw_info)
{
    download_cp *cmd_para;
    download_rp *evt_para;
    uint8_t *pcur;
    int pkt_len, frag_num, frag_len;
    int i, ret_val;
    int ncmd = 1, step = 1;

    RTKBT_DBG("%s: start", __func__);

    cmd_para = (download_cp *)fw_info->req_para;
    evt_para = (download_rp *)fw_info->rsp_para;
    pcur = fw_info->fw_data;
    pkt_len = CMD_HDR_LEN + sizeof(download_cp);
    frag_num = fw_info->fw_len / PATCH_SEG_MAX + 1;
    frag_len = PATCH_SEG_MAX;

    for (i = 0; i < frag_num; i++) {
        cmd_para->index = i?((i-1)%0x7f+1):0;
        if (i == (frag_num - 1)) {
            cmd_para->index |= DATA_END;
            frag_len = fw_info->fw_len % PATCH_SEG_MAX;
            pkt_len -= (PATCH_SEG_MAX - frag_len);
        }
        fw_info->cmd_hdr->opcode = cpu_to_le16(DOWNLOAD_OPCODE);
        fw_info->cmd_hdr->plen = sizeof(uint8_t) + frag_len;
        fw_info->pkt_len = pkt_len;
        memcpy(cmd_para->data, pcur, frag_len);

        if (step > 0) {
            ret_val = send_hci_cmd(fw_info);
            if (ret_val < 0) {
                RTKBT_DBG("%s: Failed to send frag num %d", __func__, cmd_para->index);
                return ret_val;
            } else
                RTKBT_DBG("%s: Send frag num %d", __func__, cmd_para->index);

            if (--step > 0 && i < frag_num - 1) {
                RTKBT_DBG("%s: Continue to send frag num %d", __func__, cmd_para->index + 1);
                pcur += PATCH_SEG_MAX;
                continue;
            }
        }

        while (ncmd > 0) {
            ret_val = rcv_hci_evt(fw_info);
            if (ret_val < 0) {
                RTKBT_ERR("%s: rcv_hci_evt err %d", __func__, ret_val);
                return ret_val;
            } else {
                RTKBT_DBG("%s: Receive acked frag num %d", __func__, evt_para->index);
                ncmd--;
            }

            if (0 != evt_para->status) {
                RTKBT_ERR("%s: Receive acked frag num %d, err status %d",
                        __func__, ret_val, evt_para->status);
                return -1;
            }

            if ((evt_para->index & DATA_END) || (evt_para->index == frag_num - 1)) {
                RTKBT_DBG("%s: Receive last acked index %d", __func__, evt_para->index);
                goto end;
            }
        }

        ncmd = step = fw_info->cmd_cmp->ncmd;
        pcur += PATCH_SEG_MAX;
        RTKBT_DBG("%s: HCI command packet num %d", __func__, ncmd);
    }

    /*
     * It is tricky that Host cannot receive DATA_END index from BT
     * controller, at least for 8723au. We are doomed if failed.
     */
#if 0
    /* Continue to receive the responsed events until last index occurs */
    if (i == frag_num) {
        RTKBT_DBG("%s: total frag count %d", __func__, frag_num);
        while (!(evt_para->index & DATA_END)) {
            ret_val = rcv_hci_evt(fw_info);
            if (ret_val < 0) {
                RTKBT_ERR("%s: rcv_hci_evt err %d", __func__, ret_val);
                return ret_val;
            }
            if (0 != evt_para->status)
                return -1;
            RTKBT_DBG("%s: continue to receive acked frag num %d", __func__, evt_para->index);
        }
    }
#endif
end:
    RTKBT_INFO("%s: done, sent %d frag pkts, received %d frag events",
            __func__, cmd_para->index, evt_para->index);
    return fw_info->fw_len;
}

int download_patch(firmware_info *fw_info, int cached)
{
    int ret_val = 0;

    RTKBT_DBG("%s: Download fw patch start, cached %d", __func__, cached);

    if (!fw_info || !fw_info->patch_entry) {
        RTKBT_ERR("%s: No patch entry exists(fw_info %p)", __func__, fw_info);
        ret_val = -1;
        goto end;
    }

    /*
     * step1: get local firmware if existed
     * step2: check firmware version
     * step3: download firmware if updated
     */
    ret_val = get_firmware(fw_info, cached);
    if (ret_val < 0) {
        RTKBT_ERR("%s: Failed to get firmware", __func__);
        goto end;
    }

#if SUSPNED_DW_FW
    if(fw_info_4_suspend) {
        RTKBT_DBG("%s: get suspend fw first cached %d", __func__, cached);
        ret_val = get_suspend_firmware(fw_info_4_suspend, cached);
        if (ret_val < 0) {
            RTKBT_ERR("%s: Failed to get suspend firmware", __func__);
            goto end;
        }
    }
#endif

    /*check the length of fw to be download*/
    RTKBT_DBG("%s: Check fw_info->fw_len:%d max_patch_size %d", __func__, fw_info->fw_len, fw_info->patch_entry->max_patch_size);
    if (fw_info->fw_len > fw_info->patch_entry->max_patch_size) {
        RTKBT_ERR("%s: Total length of fw&config(%08x) larger than max_patch_size 0x%08x", __func__, fw_info->fw_len, fw_info->patch_entry->max_patch_size);
        ret_val = -1;
        goto free;
    }

    ret_val = check_fw_version(fw_info);

    if (2 == ret_val) {
        RTKBT_ERR("%s: Cold reset bt chip only download", __func__);
        ret_val = download_data(fw_info);
        if (ret_val > 0)
            RTKBT_ERR("%s: Download fw patch done, fw len %d", __func__, ret_val);
    } else if(1 == ret_val){
        //   reset bt chip to update Fw patch
        ret_val = reset_controller(fw_info);
        RTKBT_ERR("%s: reset bt chip to update Fw patch, fw len %d", __func__, ret_val);
        ret_val = download_data(fw_info);
        if (ret_val > 0)
                RTKBT_ERR("%s: Download fw patch done, fw len %d", __func__, ret_val);
    }


free:
    /* Free fw data after download finished */
    kfree(fw_info->fw_data);
    fw_info->fw_data = NULL;

end:
    return ret_val;
}

#if SUSPNED_DW_FW
static int download_suspend_patch(firmware_info *fw_info, int cached)
{
    int ret_val = 0;

    RTKBT_DBG("%s: Download fw patch start, cached %d", __func__, cached);

    if (!fw_info || !fw_info->patch_entry) {
        RTKBT_ERR("%s: No patch entry exists(fw_info %p)", __func__, fw_info);
        ret_val = -1;
        goto end;
    }

    /*check the length of fw to be download*/
    RTKBT_DBG("%s:Check RTK_PATCH_LENGTH fw_info->fw_len:%d", __func__,fw_info->fw_len);
    if (fw_info->fw_len > RTK_PATCH_LENGTH_MAX || fw_info->fw_len == 0) {
        RTKBT_ERR("%s: Total length of fw&config larger than allowed 24K or no fw len:%d", __func__, fw_info->fw_len);
        ret_val = -1;
        goto free;
    }

    ret_val = check_fw_version(fw_info);

    if (2 == ret_val) {
        RTKBT_ERR("%s: Cold reset bt chip only download", __func__);
        ret_val = download_data(fw_info);
        if (ret_val > 0)
            RTKBT_ERR("%s: Download fw patch done, fw len %d", __func__, ret_val);
    } else if(1 == ret_val){
        //   reset bt chip to update Fw patch
        ret_val = reset_controller(fw_info);
        RTKBT_ERR("%s: reset bt chip to update Fw patch, fw len %d", __func__, ret_val);
        ret_val = download_data(fw_info);
        if (ret_val > 0)
                RTKBT_ERR("%s: Download fw patch done, fw len %d", __func__, ret_val);
    }


free:
    /* Free fw data after download finished */
    kfree(fw_info->fw_data);
    fw_info->fw_data = NULL;

end:
    return ret_val;
}

static void suspend_firmware_info_init(firmware_info *fw_info)
{
    RTKBT_DBG("%s: start", __func__);
    if(!fw_info)
        return;

    fw_info_4_suspend= kzalloc(sizeof(*fw_info), GFP_KERNEL);
    if (!fw_info_4_suspend)
        goto error;

    fw_info_4_suspend->send_pkt = kzalloc(PKT_LEN, GFP_KERNEL);
    if (!fw_info_4_suspend->send_pkt) {
        kfree(fw_info_4_suspend);
        goto error;
    }

    fw_info_4_suspend->rcv_pkt = kzalloc(PKT_LEN, GFP_KERNEL);
    if (!fw_info_4_suspend->rcv_pkt) {
        kfree(fw_info_4_suspend->send_pkt);
        kfree(fw_info_4_suspend);
        goto error;
    }

    fw_info_4_suspend->patch_entry = get_suspend_fw_table_entry(fw_info->udev);
    if (!fw_info_4_suspend->patch_entry) {
        kfree(fw_info_4_suspend->rcv_pkt);
        kfree(fw_info_4_suspend->send_pkt);
        kfree(fw_info_4_suspend);
        goto error;
    }

    fw_info_4_suspend->intf = fw_info->intf;
    fw_info_4_suspend->udev = fw_info->udev;
    fw_info_4_suspend->cmd_hdr = (struct hci_command_hdr *)(fw_info_4_suspend->send_pkt);
    fw_info_4_suspend->evt_hdr = (struct hci_event_hdr *)(fw_info_4_suspend->rcv_pkt);
    fw_info_4_suspend->cmd_cmp = (struct hci_ev_cmd_complete *)(fw_info_4_suspend->rcv_pkt + EVT_HDR_LEN);
    fw_info_4_suspend->req_para = fw_info_4_suspend->send_pkt + CMD_HDR_LEN;
    fw_info_4_suspend->rsp_para = fw_info_4_suspend->rcv_pkt + EVT_HDR_LEN + CMD_CMP_LEN;
    fw_info_4_suspend->pipe_in = fw_info->pipe_in;
    fw_info_4_suspend->pipe_out = fw_info->pipe_out;

    return;
error:
    RTKBT_DBG("%s: fail !", __func__);
    fw_info_4_suspend = NULL;
    return;
}
#endif

#if SET_WAKEUP_DEVICE
static void set_wakeup_device_from_conf(firmware_info *fw_info)
{
    uint8_t paired_wakeup_bdaddr[7];
    uint8_t num = 0;
    int i;
    struct file *fp;
    mm_segment_t fs;
    loff_t pos;

    memset(paired_wakeup_bdaddr, 0, 7);
    fp = filp_open(SET_WAKEUP_DEVICE_CONF, O_RDWR, 0);
    if (!IS_ERR(fp)) {
        fs = get_fs();
        set_fs(KERNEL_DS);
        pos = 0;
        //read number
        vfs_read(fp, &num, 1, &pos);
        RTKBT_DBG("read number = %d", num);
        if(num) {
            for(i = 0; i < num; i++) {
            vfs_read(fp, paired_wakeup_bdaddr, 7, &pos);
            RTKBT_DBG("paired_wakeup_bdaddr: 0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x",
                paired_wakeup_bdaddr[1],paired_wakeup_bdaddr[2],paired_wakeup_bdaddr[3],
                paired_wakeup_bdaddr[4],paired_wakeup_bdaddr[5],paired_wakeup_bdaddr[6]);
            set_wakeup_device(fw_info, paired_wakeup_bdaddr);
            }
        }
        filp_close(fp, NULL);
        set_fs(fs);
    }
    else {
        RTKBT_ERR("open wakeup config file fail! errno = %ld", PTR_ERR(fp));
    }
}
#endif

firmware_info *firmware_info_init(struct usb_interface *intf)
{
    struct usb_device *udev = interface_to_usbdev(intf);
    firmware_info *fw_info;

    RTKBT_DBG("%s: start", __func__);

    fw_info = kzalloc(sizeof(*fw_info), GFP_KERNEL);
    if (!fw_info)
        return NULL;

    fw_info->send_pkt = kzalloc(PKT_LEN, GFP_KERNEL);
    if (!fw_info->send_pkt) {
        kfree(fw_info);
        return NULL;
    }

    fw_info->rcv_pkt = kzalloc(PKT_LEN, GFP_KERNEL);
    if (!fw_info->rcv_pkt) {
        kfree(fw_info->send_pkt);
        kfree(fw_info);
        return NULL;
    }

    fw_info->patch_entry = get_fw_table_entry(udev);
    if (!fw_info->patch_entry) {
        kfree(fw_info->rcv_pkt);
        kfree(fw_info->send_pkt);
        kfree(fw_info);
        return NULL;
    }

    fw_info->intf = intf;
    fw_info->udev = udev;
    fw_info->pipe_in = usb_rcvintpipe(fw_info->udev, INTR_EP);
    fw_info->pipe_out = usb_sndctrlpipe(fw_info->udev, CTRL_EP);
    fw_info->cmd_hdr = (struct hci_command_hdr *)(fw_info->send_pkt);
    fw_info->evt_hdr = (struct hci_event_hdr *)(fw_info->rcv_pkt);
    fw_info->cmd_cmp = (struct hci_ev_cmd_complete *)(fw_info->rcv_pkt + EVT_HDR_LEN);
    fw_info->req_para = fw_info->send_pkt + CMD_HDR_LEN;
    fw_info->rsp_para = fw_info->rcv_pkt + EVT_HDR_LEN + CMD_CMP_LEN;

#if SUSPNED_DW_FW
    suspend_firmware_info_init(fw_info);
#endif

#if BTUSB_RPM
    RTKBT_INFO("%s: Auto suspend is enabled", __func__);
    usb_enable_autosuspend(udev);
    pm_runtime_set_autosuspend_delay(&(udev->dev), 2000);
#else
    RTKBT_INFO("%s: Auto suspend is disabled", __func__);
    usb_disable_autosuspend(udev);
#endif

#if BTUSB_WAKEUP_HOST
    device_wakeup_enable(&udev->dev);
#endif

    return fw_info;
}

void firmware_info_destroy(struct usb_interface *intf)
{
    firmware_info *fw_info;
    struct usb_device *udev;
    struct btusb_data *data;

    udev = interface_to_usbdev(intf);
    data = usb_get_intfdata(intf);

    fw_info = data->fw_info;
    if (!fw_info)
        return;

#if BTUSB_RPM
    usb_disable_autosuspend(udev);
#endif

    /*
     * In order to reclaim fw data mem, we free fw_data immediately
     * after download patch finished instead of here.
     */
    kfree(fw_info->rcv_pkt);
    kfree(fw_info->send_pkt);
    kfree(fw_info);

#if SUSPNED_DW_FW
    if (!fw_info_4_suspend)
        return;

    kfree(fw_info_4_suspend->rcv_pkt);
    kfree(fw_info_4_suspend->send_pkt);
    kfree(fw_info_4_suspend);
	fw_info_4_suspend = NULL;
#endif
}

static struct usb_driver btusb_driver;

static struct usb_device_id btusb_table[] = {
    { .match_flags = USB_DEVICE_ID_MATCH_VENDOR |
                     USB_DEVICE_ID_MATCH_INT_INFO,
      .idVendor = 0x0bda,
      .bInterfaceClass = 0xe0,
      .bInterfaceSubClass = 0x01,
      .bInterfaceProtocol = 0x01 },

    { .match_flags = USB_DEVICE_ID_MATCH_VENDOR |
                     USB_DEVICE_ID_MATCH_INT_INFO,
      .idVendor = 0x13d3,
      .bInterfaceClass = 0xe0,
      .bInterfaceSubClass = 0x01,
      .bInterfaceProtocol = 0x01 },

    { }
};

MODULE_DEVICE_TABLE(usb, btusb_table);

static int inc_tx(struct btusb_data *data)
{
    unsigned long flags;
    int rv;

    spin_lock_irqsave(&data->txlock, flags);
    rv = test_bit(BTUSB_SUSPENDING, &data->flags);
    if (!rv)
        data->tx_in_flight++;
    spin_unlock_irqrestore(&data->txlock, flags);

    return rv;
}

void check_sco_event(struct urb *urb)
{
    u8* opcode = (u8*)(urb->transfer_buffer);
    u8 status;
    static uint16_t sco_handle = 0;
    uint16_t handle;
    struct hci_dev *hdev = urb->context;
    struct btusb_data *data = GET_DRV_DATA(hdev);

    switch (*opcode) {
    case HCI_EV_SYNC_CONN_COMPLETE:
        RTKBT_INFO("%s: HCI_EV_SYNC_CONN_COMPLETE(0x%02x)", __func__, *opcode);
        status = *(opcode + 2);
        sco_handle = *(opcode + 3) | *(opcode + 4) << 8;
        if (status == 0) {
            hdev->conn_hash.sco_num++;
            schedule_work(&data->work);
        }
        break;
    case HCI_EV_DISCONN_COMPLETE:
        RTKBT_INFO("%s: HCI_EV_DISCONN_COMPLETE(0x%02x)", __func__, *opcode);
        status = *(opcode + 2);
        handle = *(opcode + 3) | *(opcode + 4) << 8;
        if (status == 0 && sco_handle == handle) {
            hdev->conn_hash.sco_num--;
            schedule_work(&data->work);
        }
        break;
    default:
        RTKBT_DBG("%s: event 0x%02x", __func__, *opcode);
        break;
    }
}

static void btusb_intr_complete(struct urb *urb)
{
    struct hci_dev *hdev = urb->context;
    struct btusb_data *data = GET_DRV_DATA(hdev);
    int err;

    RTKBT_DBG("%s: urb %p status %d count %d ", __func__,
            urb, urb->status, urb->actual_length);

//    check_sco_event(urb);

    if (!test_bit(HCI_RUNNING, &hdev->flags))
        return;


    if (urb->status == 0) {
        hdev->stat.byte_rx += urb->actual_length;

        if (hci_recv_fragment(hdev, HCI_EVENT_PKT,
                        urb->transfer_buffer,
                        urb->actual_length) < 0) {
            RTKBT_ERR("%s: Corrupted event packet", __func__);
            hdev->stat.err_rx++;
        }
    }
    /* Avoid suspend failed when usb_kill_urb */
    else if(urb->status == -ENOENT)    {
        return;
    }


    if (!test_bit(BTUSB_INTR_RUNNING, &data->flags))
        return;

    usb_mark_last_busy(data->udev);
    usb_anchor_urb(urb, &data->intr_anchor);

    err = usb_submit_urb(urb, GFP_ATOMIC);
    if (err < 0) {
        /* EPERM: urb is being killed;
         * ENODEV: device got disconnected */
        if (err != -EPERM && err != -ENODEV)
            RTKBT_ERR("%s: Failed to re-submit urb %p, err %d",
                    __func__, urb, err);
        usb_unanchor_urb(urb);
    }
}

static int btusb_submit_intr_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
    struct btusb_data *data = GET_DRV_DATA(hdev);
    struct urb *urb;
    unsigned char *buf;
    unsigned int pipe;
    int err, size;

    if (!data->intr_ep)
        return -ENODEV;

    urb = usb_alloc_urb(0, mem_flags);
    if (!urb)
        return -ENOMEM;

    size = le16_to_cpu(data->intr_ep->wMaxPacketSize);

    buf = kmalloc(size, mem_flags);
    if (!buf) {
        usb_free_urb(urb);
        return -ENOMEM;
    }

    RTKBT_DBG("%s: mMaxPacketSize %d, bEndpointAddress 0x%02x",
            __func__, size, data->intr_ep->bEndpointAddress);

    pipe = usb_rcvintpipe(data->udev, data->intr_ep->bEndpointAddress);

    usb_fill_int_urb(urb, data->udev, pipe, buf, size,
                        btusb_intr_complete, hdev,
                        data->intr_ep->bInterval);

    urb->transfer_flags |= URB_FREE_BUFFER;

    usb_anchor_urb(urb, &data->intr_anchor);

    err = usb_submit_urb(urb, mem_flags);
    if (err < 0) {
        RTKBT_ERR("%s: Failed to submit urb %p, err %d",
                __func__, urb, err);
        usb_unanchor_urb(urb);
    }

    usb_free_urb(urb);

    return err;
}

static void btusb_bulk_complete(struct urb *urb)
{
    struct hci_dev *hdev = urb->context;
    struct btusb_data *data = GET_DRV_DATA(hdev);
    int err;

    RTKBT_DBG("%s: urb %p status %d count %d",
            __func__, urb, urb->status, urb->actual_length);

    if (!test_bit(HCI_RUNNING, &hdev->flags))
        return;

    if (urb->status == 0) {
        hdev->stat.byte_rx += urb->actual_length;

        if (hci_recv_fragment(hdev, HCI_ACLDATA_PKT,
                        urb->transfer_buffer,
                        urb->actual_length) < 0) {
            RTKBT_ERR("%s: Corrupted ACL packet", __func__);
            hdev->stat.err_rx++;
        }
    }
    /* Avoid suspend failed when usb_kill_urb */
    else if(urb->status == -ENOENT)    {
        return;
    }


    if (!test_bit(BTUSB_BULK_RUNNING, &data->flags))
        return;

    usb_anchor_urb(urb, &data->bulk_anchor);
    usb_mark_last_busy(data->udev);

    err = usb_submit_urb(urb, GFP_ATOMIC);
    if (err < 0) {
        /* -EPERM: urb is being killed;
         * -ENODEV: device got disconnected */
        if (err != -EPERM && err != -ENODEV)
            RTKBT_ERR("btusb_bulk_complete %s urb %p failed to resubmit (%d)",
                        hdev->name, urb, -err);
        usb_unanchor_urb(urb);
    }
}

static int btusb_submit_bulk_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
    struct btusb_data *data = GET_DRV_DATA(hdev);
    struct urb *urb;
    unsigned char *buf;
    unsigned int pipe;
    int err, size = HCI_MAX_FRAME_SIZE;

    RTKBT_DBG("%s: hdev name %s", __func__, hdev->name);

    if (!data->bulk_rx_ep)
        return -ENODEV;

    urb = usb_alloc_urb(0, mem_flags);
    if (!urb)
        return -ENOMEM;

    buf = kmalloc(size, mem_flags);
    if (!buf) {
        usb_free_urb(urb);
        return -ENOMEM;
    }

    pipe = usb_rcvbulkpipe(data->udev, data->bulk_rx_ep->bEndpointAddress);

    usb_fill_bulk_urb(urb, data->udev, pipe,
                    buf, size, btusb_bulk_complete, hdev);

    urb->transfer_flags |= URB_FREE_BUFFER;

    usb_mark_last_busy(data->udev);
    usb_anchor_urb(urb, &data->bulk_anchor);

    err = usb_submit_urb(urb, mem_flags);
    if (err < 0) {
        RTKBT_ERR("%s: Failed to submit urb %p, err %d", __func__, urb, err);
        usb_unanchor_urb(urb);
    }

    usb_free_urb(urb);

    return err;
}

static void btusb_isoc_complete(struct urb *urb)
{
    struct hci_dev *hdev = urb->context;
    struct btusb_data *data = GET_DRV_DATA(hdev);
    int i, err;


    RTKBT_DBG("%s: urb %p status %d count %d",
            __func__, urb, urb->status, urb->actual_length);

    if (!test_bit(HCI_RUNNING, &hdev->flags))
        return;

    if (urb->status == 0) {
        for (i = 0; i < urb->number_of_packets; i++) {
            unsigned int offset = urb->iso_frame_desc[i].offset;
            unsigned int length = urb->iso_frame_desc[i].actual_length;

            if (urb->iso_frame_desc[i].status)
                continue;

            hdev->stat.byte_rx += length;

            if (hci_recv_fragment(hdev, HCI_SCODATA_PKT,
                        urb->transfer_buffer + offset,
                                length) < 0) {
                RTKBT_ERR("%s: Corrupted SCO packet", __func__);
                hdev->stat.err_rx++;
            }
        }
    }
    /* Avoid suspend failed when usb_kill_urb */
    else if(urb->status == -ENOENT)    {
        return;
    }


    if (!test_bit(BTUSB_ISOC_RUNNING, &data->flags))
        return;

    usb_anchor_urb(urb, &data->isoc_anchor);
    i = 0;
retry:
    err = usb_submit_urb(urb, GFP_ATOMIC);
    if (err < 0) {
        /* -EPERM: urb is being killed;
         * -ENODEV: device got disconnected */
        if (err != -EPERM && err != -ENODEV)
            RTKBT_ERR("%s: Failed to re-sumbit urb %p, retry %d, err %d",
                    __func__, urb, i, err);
        if (i < 10) {
            i++;
            mdelay(1);
            goto retry;
        }

        usb_unanchor_urb(urb);
    }
}

static inline void fill_isoc_descriptor(struct urb *urb, int len, int mtu)
{
    int i, offset = 0;

    RTKBT_DBG("%s: len %d mtu %d", __func__, len, mtu);

    for (i = 0; i < BTUSB_MAX_ISOC_FRAMES && len >= mtu;
                    i++, offset += mtu, len -= mtu) {
        urb->iso_frame_desc[i].offset = offset;
        urb->iso_frame_desc[i].length = mtu;
    }

    if (len && i < BTUSB_MAX_ISOC_FRAMES) {
        urb->iso_frame_desc[i].offset = offset;
        urb->iso_frame_desc[i].length = len;
        i++;
    }

    urb->number_of_packets = i;
}

static int btusb_submit_isoc_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
    struct btusb_data *data = GET_DRV_DATA(hdev);
    struct urb *urb;
    unsigned char *buf;
    unsigned int pipe;
    int err, size;

    if (!data->isoc_rx_ep)
        return -ENODEV;

    urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, mem_flags);
    if (!urb)
        return -ENOMEM;

    size = le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize) *
                        BTUSB_MAX_ISOC_FRAMES;

    buf = kmalloc(size, mem_flags);
    if (!buf) {
        usb_free_urb(urb);
        return -ENOMEM;
    }

    pipe = usb_rcvisocpipe(data->udev, data->isoc_rx_ep->bEndpointAddress);

    urb->dev      = data->udev;
    urb->pipe     = pipe;
    urb->context  = hdev;
    urb->complete = btusb_isoc_complete;
    urb->interval = data->isoc_rx_ep->bInterval;

    urb->transfer_flags  = URB_FREE_BUFFER | URB_ISO_ASAP;
    urb->transfer_buffer = buf;
    urb->transfer_buffer_length = size;

    fill_isoc_descriptor(urb, size,
            le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize));

    usb_anchor_urb(urb, &data->isoc_anchor);

    err = usb_submit_urb(urb, mem_flags);
    if (err < 0) {
        RTKBT_ERR("%s: Failed to submit urb %p, err %d", __func__, urb, err);
        usb_unanchor_urb(urb);
    }

    usb_free_urb(urb);

    return err;
}

static void btusb_tx_complete(struct urb *urb)
{
    struct sk_buff *skb = urb->context;
    struct hci_dev *hdev = (struct hci_dev *) skb->dev;
    struct btusb_data *data = GET_DRV_DATA(hdev);

    if (!test_bit(HCI_RUNNING, &hdev->flags))
        goto done;

    if (!urb->status)
        hdev->stat.byte_tx += urb->transfer_buffer_length;
    else
        hdev->stat.err_tx++;

done:
    spin_lock(&data->txlock);
    data->tx_in_flight--;
    spin_unlock(&data->txlock);

    kfree(urb->setup_packet);

    kfree_skb(skb);
}

static void btusb_isoc_tx_complete(struct urb *urb)
{
    struct sk_buff *skb = urb->context;
    struct hci_dev *hdev = (struct hci_dev *) skb->dev;

    RTKBT_DBG("%s: urb %p status %d count %d",
            __func__, urb, urb->status, urb->actual_length);

    if (skb && hdev) {
        if (!test_bit(HCI_RUNNING, &hdev->flags))
            goto done;

        if (!urb->status)
            hdev->stat.byte_tx += urb->transfer_buffer_length;
        else
            hdev->stat.err_tx++;
    } else
        RTKBT_ERR("%s: skb 0x%p hdev 0x%p", __func__, skb, hdev);

done:
    kfree(urb->setup_packet);

    kfree_skb(skb);
}

static int btusb_open(struct hci_dev *hdev)
{
    struct btusb_data *data = GET_DRV_DATA(hdev);
    int err = 0;

    RTKBT_INFO("%s: Start, PM usage count %d", __func__,
            atomic_read(&(data->intf->pm_usage_cnt)));

    err = usb_autopm_get_interface(data->intf);
    if (err < 0)
        return err;

    data->intf->needs_remote_wakeup = 1;

    if (test_and_set_bit(HCI_RUNNING, &hdev->flags))
        goto done;

    if (test_and_set_bit(BTUSB_INTR_RUNNING, &data->flags))
        goto done;

    err = btusb_submit_intr_urb(hdev, GFP_KERNEL);
    if (err < 0)
        goto failed;

    err = btusb_submit_bulk_urb(hdev, GFP_KERNEL);
    if (err < 0) {
        mdelay(URB_CANCELING_DELAY_MS);
        usb_kill_anchored_urbs(&data->intr_anchor);
        goto failed;
    }

    set_bit(BTUSB_BULK_RUNNING, &data->flags);
    btusb_submit_bulk_urb(hdev, GFP_KERNEL);

done:
    usb_autopm_put_interface(data->intf);
    RTKBT_INFO("%s: End, PM usage count %d", __func__,
            atomic_read(&(data->intf->pm_usage_cnt)));
    return 0;

failed:
    clear_bit(BTUSB_INTR_RUNNING, &data->flags);
    clear_bit(HCI_RUNNING, &hdev->flags);
    usb_autopm_put_interface(data->intf);
    RTKBT_ERR("%s: Failed, PM usage count %d", __func__,
            atomic_read(&(data->intf->pm_usage_cnt)));
    return err;
}

static void btusb_stop_traffic(struct btusb_data *data)
{
    mdelay(URB_CANCELING_DELAY_MS);
    usb_kill_anchored_urbs(&data->intr_anchor);
    usb_kill_anchored_urbs(&data->bulk_anchor);
    usb_kill_anchored_urbs(&data->isoc_anchor);
}

static int btusb_close(struct hci_dev *hdev)
{
    struct btusb_data *data = GET_DRV_DATA(hdev);
    int i, err;

    RTKBT_INFO("%s: hci running %lu", __func__, hdev->flags & HCI_RUNNING);

    if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
        return 0;

    for (i = 0; i < NUM_REASSEMBLY; i++) {
        if (hdev->reassembly[i]) {
            RTKBT_DBG("%s: free ressembly[%d]", __func__, i);
            kfree_skb(hdev->reassembly[i]);
            hdev->reassembly[i] = NULL;
        }
    }

    cancel_work_sync(&data->work);
    cancel_work_sync(&data->waker);

    clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
    clear_bit(BTUSB_BULK_RUNNING, &data->flags);
    clear_bit(BTUSB_INTR_RUNNING, &data->flags);

    btusb_stop_traffic(data);
    err = usb_autopm_get_interface(data->intf);
    if (err < 0)
        goto failed;

    data->intf->needs_remote_wakeup = 0;
    usb_autopm_put_interface(data->intf);

failed:
    mdelay(URB_CANCELING_DELAY_MS);
    usb_scuttle_anchored_urbs(&data->deferred);
    return 0;
}

static int btusb_flush(struct hci_dev *hdev)
{
    struct btusb_data *data = GET_DRV_DATA(hdev);

    RTKBT_DBG("%s", __func__);

    mdelay(URB_CANCELING_DELAY_MS);
    usb_kill_anchored_urbs(&data->tx_anchor);

    return 0;
}


static int btusb_send_frame(struct sk_buff *skb)
{
    struct hci_dev *hdev = (struct hci_dev *) skb->dev;

    struct btusb_data *data = GET_DRV_DATA(hdev);
    struct usb_ctrlrequest *dr;
    struct urb *urb;
    unsigned int pipe;
    int err;
    int retries = 0;

    RTKBT_DBG("%s: hdev %p, btusb data %p, pkt type %d",
            __func__, hdev, data, bt_cb(skb)->pkt_type);

    if (!test_bit(HCI_RUNNING, &hdev->flags))
        return -EBUSY;



    switch (bt_cb(skb)->pkt_type) {
    case HCI_COMMAND_PKT:
        print_command(skb);
        urb = usb_alloc_urb(0, GFP_ATOMIC);
        if (!urb)
            return -ENOMEM;

        dr = kmalloc(sizeof(*dr), GFP_ATOMIC);
        if (!dr) {
            usb_free_urb(urb);
            return -ENOMEM;
        }

        dr->bRequestType = data->cmdreq_type;
        dr->bRequest     = 0;
        dr->wIndex       = 0;
        dr->wValue       = 0;
        dr->wLength      = __cpu_to_le16(skb->len);

        pipe = usb_sndctrlpipe(data->udev, 0x00);

        usb_fill_control_urb(urb, data->udev, pipe, (void *) dr,
                skb->data, skb->len, btusb_tx_complete, skb);

        hdev->stat.cmd_tx++;
        break;

    case HCI_ACLDATA_PKT:
        print_acl(skb, 1);
        if (!data->bulk_tx_ep)
            return -ENODEV;

        urb = usb_alloc_urb(0, GFP_ATOMIC);
        if (!urb)
            return -ENOMEM;

        pipe = usb_sndbulkpipe(data->udev,
                    data->bulk_tx_ep->bEndpointAddress);

        usb_fill_bulk_urb(urb, data->udev, pipe,
                skb->data, skb->len, btusb_tx_complete, skb);

        hdev->stat.acl_tx++;
        break;

    case HCI_SCODATA_PKT:
        print_sco(skb, 1);
        if (!data->isoc_tx_ep || SCO_NUM < 1) {
            kfree(skb);
            return -ENODEV;
        }

        urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, GFP_ATOMIC);
        if (!urb) {
            RTKBT_ERR("%s: Failed to allocate mem for sco pkts", __func__);
            kfree(skb);
            return -ENOMEM;
        }

        pipe = usb_sndisocpipe(data->udev, data->isoc_tx_ep->bEndpointAddress);

        usb_fill_int_urb(urb, data->udev, pipe,
                skb->data, skb->len, btusb_isoc_tx_complete,
                skb, data->isoc_tx_ep->bInterval);

        urb->transfer_flags  = URB_ISO_ASAP;

        fill_isoc_descriptor(urb, skb->len,
                le16_to_cpu(data->isoc_tx_ep->wMaxPacketSize));

        hdev->stat.sco_tx++;
        goto skip_waking;

    default:
        return -EILSEQ;
    }

    err = inc_tx(data);
    if (err) {
        usb_anchor_urb(urb, &data->deferred);
        schedule_work(&data->waker);
        err = 0;
        goto done;
    }

skip_waking:
    usb_anchor_urb(urb, &data->tx_anchor);
retry:
    err = usb_submit_urb(urb, GFP_ATOMIC);
    if (err < 0) {
        RTKBT_ERR("%s: Failed to submit urb %p, pkt type %d, err %d, retries %d",
                __func__, urb, bt_cb(skb)->pkt_type, err, retries);
        if ((bt_cb(skb)->pkt_type != HCI_SCODATA_PKT) && (retries < 10)) {
            mdelay(1);

            if (bt_cb(skb)->pkt_type == HCI_COMMAND_PKT)
                print_error_command(skb);
            retries++;
            goto retry;
        }
        kfree(urb->setup_packet);
        usb_unanchor_urb(urb);
    } else
        usb_mark_last_busy(data->udev);
    usb_free_urb(urb);

done:
    return err;
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 4, 0)
static void btusb_destruct(struct hci_dev *hdev)
{
    struct btusb_data *data = GET_DRV_DATA(hdev);

    RTKBT_DBG("%s: name %s", __func__, hdev->name);

    kfree(data);
}
#endif

static void btusb_notify(struct hci_dev *hdev, unsigned int evt)
{
    struct btusb_data *data = GET_DRV_DATA(hdev);

    RTKBT_DBG("%s: name %s, evt %d", __func__, hdev->name, evt);

    if (SCO_NUM != data->sco_num) {
        data->sco_num = SCO_NUM;
        schedule_work(&data->work);
    }
}

static inline int set_isoc_interface(struct hci_dev *hdev, int altsetting)
{
    struct btusb_data *data = GET_DRV_DATA(hdev);
    struct usb_interface *intf = data->isoc;
    struct usb_endpoint_descriptor *ep_desc;
    int i, err;

    if (!data->isoc)
        return -ENODEV;

    err = usb_set_interface(data->udev, 1, altsetting);
    if (err < 0) {
        RTKBT_ERR("%s: Failed to set interface, altsetting %d, err %d",
                __func__, altsetting, err);
        return err;
    }

    data->isoc_altsetting = altsetting;

    data->isoc_tx_ep = NULL;
    data->isoc_rx_ep = NULL;

    for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
        ep_desc = &intf->cur_altsetting->endpoint[i].desc;

        if (!data->isoc_tx_ep && usb_endpoint_is_isoc_out(ep_desc)) {
            data->isoc_tx_ep = ep_desc;
            continue;
        }

        if (!data->isoc_rx_ep && usb_endpoint_is_isoc_in(ep_desc)) {
            data->isoc_rx_ep = ep_desc;
            continue;
        }
    }

    if (!data->isoc_tx_ep || !data->isoc_rx_ep) {
        RTKBT_ERR("%s: Invalid SCO descriptors", __func__);
        return -ENODEV;
    }

    return 0;
}

static void btusb_work(struct work_struct *work)
{
    struct btusb_data *data = container_of(work, struct btusb_data, work);
    struct hci_dev *hdev = data->hdev;
    int err;
    int new_alts;
    if (data->sco_num > 0) {
        if (!test_bit(BTUSB_DID_ISO_RESUME, &data->flags)) {
            err = usb_autopm_get_interface(data->isoc ? data->isoc : data->intf);
            if (err < 0) {
                clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
                mdelay(URB_CANCELING_DELAY_MS);
                usb_kill_anchored_urbs(&data->isoc_anchor);
                return;
            }

            set_bit(BTUSB_DID_ISO_RESUME, &data->flags);
        }
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 7, 1)
        if (hdev->voice_setting & 0x0020) {
            static const int alts[3] = { 2, 4, 5 };
            new_alts = alts[data->sco_num - 1];
        } else{
            new_alts = data->sco_num;
        }
        if (data->isoc_altsetting != new_alts) {
#else
        if (data->isoc_altsetting != 2) {
            new_alts = 2;
#endif

            clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
            mdelay(URB_CANCELING_DELAY_MS);
            usb_kill_anchored_urbs(&data->isoc_anchor);

            if (set_isoc_interface(hdev, new_alts) < 0)
                return;
        }

        if (!test_and_set_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
            if (btusb_submit_isoc_urb(hdev, GFP_KERNEL) < 0)
                clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
            else
                btusb_submit_isoc_urb(hdev, GFP_KERNEL);
        }
    } else {
        clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
        mdelay(URB_CANCELING_DELAY_MS);
        usb_kill_anchored_urbs(&data->isoc_anchor);

        set_isoc_interface(hdev, 0);
        if (test_and_clear_bit(BTUSB_DID_ISO_RESUME, &data->flags))
            usb_autopm_put_interface(data->isoc ? data->isoc : data->intf);
    }
}

static void btusb_waker(struct work_struct *work)
{
    struct btusb_data *data = container_of(work, struct btusb_data, waker);
    int err;

    RTKBT_DBG("%s: PM usage count %d", __func__,
            atomic_read(&data->intf->pm_usage_cnt));

    err = usb_autopm_get_interface(data->intf);
    if (err < 0)
        return;

    usb_autopm_put_interface(data->intf);
}


//#ifdef CONFIG_HAS_EARLYSUSPEND
#if 0
static void btusb_early_suspend(struct early_suspend *h)
{
    struct btusb_data *data;
    firmware_info *fw_info;
    patch_info *patch_entry;

    RTKBT_INFO("%s", __func__);

    data = container_of(h, struct btusb_data, early_suspend);
    fw_info = data->fw_info;
    patch_entry = fw_info->patch_entry;

    patch_entry->fw_len = load_firmware(fw_info, &patch_entry->fw_cache);
    if (patch_entry->fw_len <= 0) {
        /* We may encount failure in loading firmware, just give a warning */
        RTKBT_WARN("%s: Failed to load firmware", __func__);
    }
}

static void btusb_late_resume(struct early_suspend *h)
{
    struct btusb_data *data;
    firmware_info *fw_info;
    patch_info *patch_entry;

    RTKBT_INFO("%s", __func__);

    data = container_of(h, struct btusb_data, early_suspend);
    fw_info = data->fw_info;
    patch_entry = fw_info->patch_entry;

    /* Reclaim fw buffer when bt usb resumed */
    if (patch_entry->fw_len > 0) {
        kfree(patch_entry->fw_cache);
        patch_entry->fw_cache = NULL;
        patch_entry->fw_len = 0;
    }
}
#else
int bt_pm_notify(struct notifier_block *notifier, ulong pm_event, void *unused)
{
    struct btusb_data *data;
    firmware_info *fw_info;
    patch_info *patch_entry;
    struct usb_device *udev;

    RTKBT_INFO("%s: pm event %ld", __func__, pm_event);

    data = container_of(notifier, struct btusb_data, pm_notifier);
    fw_info = data->fw_info;
    patch_entry = fw_info->patch_entry;
    udev = fw_info->udev;

    switch (pm_event) {
    case PM_SUSPEND_PREPARE:
    case PM_HIBERNATION_PREPARE:
#if 0
        patch_entry->fw_len = load_firmware(fw_info, &patch_entry->fw_cache);
        if (patch_entry->fw_len <= 0) {
        /* We may encount failure in loading firmware, just give a warning */
            RTKBT_WARN("%s: Failed to load firmware", __func__);
        }
#endif
        if (!device_may_wakeup(&udev->dev)) {
#if (CONFIG_RESET_RESUME || CONFIG_BLUEDROID)
            RTKBT_INFO("%s:remote wakeup not supported, reset resume supported", __func__);
#else
            fw_info->intf->needs_binding = 1;
            RTKBT_INFO("%s:remote wakeup not supported, binding needed", __func__);
#endif
        }
        break;

    case PM_POST_SUSPEND:
    case PM_POST_HIBERNATION:
    case PM_POST_RESTORE:
#if 0
        /* Reclaim fw buffer when bt usb resumed */
        if (patch_entry->fw_len > 0) {
            kfree(patch_entry->fw_cache);
            patch_entry->fw_cache = NULL;
            patch_entry->fw_len = 0;
        }
#endif

#if BTUSB_RPM
        usb_disable_autosuspend(udev);
        usb_enable_autosuspend(udev);
        pm_runtime_set_autosuspend_delay(&(udev->dev), 2000);
#endif
        break;

    default:
        break;
    }

    return NOTIFY_DONE;
}

int bt_reboot_notify(struct notifier_block *notifier, ulong pm_event, void *unused)
{
    struct btusb_data *data;
    firmware_info *fw_info;
    patch_info *patch_entry;
    struct usb_device *udev;

    RTKBT_INFO("%s: pm event %ld", __func__, pm_event);

    data = container_of(notifier, struct btusb_data, reboot_notifier);
    fw_info = data->fw_info;
    patch_entry = fw_info->patch_entry;
    udev = fw_info->udev;

    switch (pm_event) {
    case SYS_DOWN:
        RTKBT_DBG("%s:system down or restart", __func__);
    break;

    case SYS_HALT:
    case SYS_POWER_OFF:
#if SUSPNED_DW_FW
        cancel_work_sync(&data->work);

        btusb_stop_traffic(data);
        mdelay(URB_CANCELING_DELAY_MS);
        usb_kill_anchored_urbs(&data->tx_anchor);


        if(fw_info_4_suspend) {
            download_suspend_patch(fw_info_4_suspend,1);
        }
	    else
		    RTKBT_ERR("%s: Failed to download suspend fw", __func__);
#endif

#if SET_WAKEUP_DEVICE
        set_wakeup_device_from_conf(fw_info_4_suspend);
#endif
        RTKBT_DBG("%s:system halt or power off", __func__);
    break;

    default:
        break;
    }

    return NOTIFY_DONE;
}

#endif

static int btusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usb_device *udev = interface_to_usbdev(intf);
    struct usb_endpoint_descriptor *ep_desc;
    struct btusb_data *data;
    struct hci_dev *hdev;
    firmware_info *fw_info;
    int i, err=0;

    RTKBT_INFO("%s: usb_interface %p, bInterfaceNumber %d, idVendor 0x%04x, "
            "idProduct 0x%04x", __func__, intf,
            intf->cur_altsetting->desc.bInterfaceNumber,
            id->idVendor, id->idProduct);

    /* interface numbers are hardcoded in the spec */
    if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
        return -ENODEV;

    RTKBT_DBG("%s: can wakeup = %x, may wakeup = %x", __func__,
            device_can_wakeup(&udev->dev), device_may_wakeup(&udev->dev));

    data = rtk_alloc(intf);
    if (!data)
        return -ENOMEM;

    for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
        ep_desc = &intf->cur_altsetting->endpoint[i].desc;

        if (!data->intr_ep && usb_endpoint_is_int_in(ep_desc)) {
            data->intr_ep = ep_desc;
            continue;
        }

        if (!data->bulk_tx_ep && usb_endpoint_is_bulk_out(ep_desc)) {
            data->bulk_tx_ep = ep_desc;
            continue;
        }

        if (!data->bulk_rx_ep && usb_endpoint_is_bulk_in(ep_desc)) {
            data->bulk_rx_ep = ep_desc;
            continue;
        }
    }

    if (!data->intr_ep || !data->bulk_tx_ep || !data->bulk_rx_ep) {
        rtk_free(data);
        return -ENODEV;
    }

    data->cmdreq_type = USB_TYPE_CLASS;

    data->udev = udev;
    data->intf = intf;

    dlfw_dis_state = 0;
    spin_lock_init(&queue_lock);
    spin_lock_init(&dlfw_lock);
    spin_lock_init(&data->lock);

    INIT_WORK(&data->work, btusb_work);
    INIT_WORK(&data->waker, btusb_waker);
    spin_lock_init(&data->txlock);

    init_usb_anchor(&data->tx_anchor);
    init_usb_anchor(&data->intr_anchor);
    init_usb_anchor(&data->bulk_anchor);
    init_usb_anchor(&data->isoc_anchor);
    init_usb_anchor(&data->deferred);

    fw_info = firmware_info_init(intf);
    if (fw_info)
        data->fw_info = fw_info;
    else {
        RTKBT_WARN("%s: Failed to initialize fw info", __func__);
        /* Skip download patch */
        goto end;
    }

    RTKBT_INFO("%s: download begining...", __func__);

#if CONFIG_BLUEDROID
    mutex_lock(&btchr_mutex);
#endif



#if CONFIG_BLUEDROID
    mutex_unlock(&btchr_mutex);
#endif

    RTKBT_INFO("%s: download ending...", __func__);

    hdev = hci_alloc_dev();
    if (!hdev) {
        rtk_free(data);
        data = NULL;
        return -ENOMEM;
    }

    HDEV_BUS = HCI_USB;

    data->hdev = hdev;

    SET_HCIDEV_DEV(hdev, &intf->dev);

    hdev->open     = btusb_open;
    hdev->close    = btusb_close;
    hdev->flush    = btusb_flush;
    hdev->send     = btusb_send_frame;
    hdev->notify   = btusb_notify;

#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 4, 0)
    hci_set_drvdata(hdev, data);
#else
    hdev->driver_data = data;
    hdev->destruct = btusb_destruct;
    hdev->owner = THIS_MODULE;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 1)
    if (!reset_on_close){
        /* set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks); */
        RTKBT_DBG("%s: Set HCI_QUIRK_RESET_ON_CLOSE", __func__);
    }
#endif

    /* Interface numbers are hardcoded in the specification */
    data->isoc = usb_ifnum_to_if(data->udev, 1);
    if (data->isoc) {
        err = usb_driver_claim_interface(&btusb_driver,
                            data->isoc, data);
        if (err < 0) {
            hci_free_dev(hdev);
            hdev = NULL;
            rtk_free(data);
            data = NULL;
            return err;
        }
    }

    err = hci_register_dev(hdev);
    if (err < 0) {
        hci_free_dev(hdev);
        hdev = NULL;
        rtk_free(data);
        data = NULL;
        return err;
    }

    usb_set_intfdata(intf, data);

//#ifdef CONFIG_HAS_EARLYSUSPEND
#if 0
    data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
    data->early_suspend.suspend = btusb_early_suspend;
    data->early_suspend.resume = btusb_late_resume;
    register_early_suspend(&data->early_suspend);
#else
    data->pm_notifier.notifier_call = bt_pm_notify;
    data->reboot_notifier.notifier_call = bt_reboot_notify;
    register_pm_notifier(&data->pm_notifier);
    register_reboot_notifier(&data->reboot_notifier);
#endif

#if CONFIG_BLUEDROID
    RTKBT_INFO("%s: Check bt reset flag %d", __func__, bt_reset);
    /* Report hci hardware error after everthing is ready,
     * especially hci register is completed. Or, btchr_poll
     * will get null hci dev when hotplug in.
     */
    if (bt_reset == 1) {
        hci_hardware_error();
        bt_reset = 0;
    } else
        bt_reset = 0; /* Clear and reset it anyway */
#endif

end:
    return 0;
}

static void btusb_disconnect(struct usb_interface *intf)
{
    struct btusb_data *data;
    struct hci_dev *hdev = NULL;

    wait_event_interruptible(bt_dlfw_wait, (check_set_dlfw_state_value(2) == 2));

    RTKBT_INFO("%s: usb_interface %p, bInterfaceNumber %d",
            __func__, intf, intf->cur_altsetting->desc.bInterfaceNumber);

    data = usb_get_intfdata(intf);

    if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
        return;

    if (data)
        hdev = data->hdev;
    else {
        RTKBT_WARN("%s: Failed to get bt usb data[Null]", __func__);
        return;
    }

//#ifdef CONFIG_HAS_EARLYSUSPEND
#if 0
    unregister_early_suspend(&data->early_suspend);
#else
    unregister_pm_notifier(&data->pm_notifier);
    unregister_reboot_notifier(&data->reboot_notifier);
#endif

    firmware_info_destroy(intf);

#if CONFIG_BLUEDROID
    if (test_bit(HCI_RUNNING, &hdev->flags)) {
        RTKBT_INFO("%s: Set BT reset flag", __func__);
        bt_reset = 1;
    }
#endif

    usb_set_intfdata(data->intf, NULL);

    if (data->isoc)
        usb_set_intfdata(data->isoc, NULL);

    hci_unregister_dev(hdev);

    if (intf == data->isoc)
        usb_driver_release_interface(&btusb_driver, data->intf);
    else if (data->isoc)
        usb_driver_release_interface(&btusb_driver, data->isoc);

#if !CONFIG_BLUEDROID
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 4, 0)
    __hci_dev_put(hdev);
#endif
#endif

    hci_free_dev(hdev);
    rtk_free(data);
    data = NULL;
    set_dlfw_state_value(0);
}

#ifdef CONFIG_PM
static int btusb_suspend(struct usb_interface *intf, pm_message_t message)
{
    struct btusb_data *data = usb_get_intfdata(intf);
    firmware_info *fw_info = data->fw_info;

    RTKBT_INFO("%s: event 0x%x, suspend count %d", __func__,
            message.event, data->suspend_count);

    if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
        return 0;

    if (!test_bit(HCI_RUNNING, &data->hdev->flags))
        set_bt_onoff(fw_info, 1);

    if (data->suspend_count++)
        return 0;

    spin_lock_irq(&data->txlock);
    if (!((message.event & PM_EVENT_AUTO) && data->tx_in_flight)) {
        set_bit(BTUSB_SUSPENDING, &data->flags);
        spin_unlock_irq(&data->txlock);
    } else {
        spin_unlock_irq(&data->txlock);
        data->suspend_count--;
        RTKBT_ERR("%s: Failed to enter suspend", __func__);
        return -EBUSY;
    }

    cancel_work_sync(&data->work);

    btusb_stop_traffic(data);
    mdelay(URB_CANCELING_DELAY_MS);
    usb_kill_anchored_urbs(&data->tx_anchor);

#if SUSPNED_DW_FW
    if(fw_info_4_suspend) {
        download_suspend_patch(fw_info_4_suspend,1);
    }
    else
        RTKBT_ERR("%s: Failed to download suspend fw", __func__);
#endif

#if SET_WAKEUP_DEVICE
    set_wakeup_device_from_conf(fw_info_4_suspend);
#endif

    return 0;
}

static void play_deferred(struct btusb_data *data)
{
    struct urb *urb;
    int err;

    while ((urb = usb_get_from_anchor(&data->deferred))) {
        usb_anchor_urb(urb, &data->tx_anchor);
        err = usb_submit_urb(urb, GFP_ATOMIC);
        if (err < 0) {
            RTKBT_ERR("%s: Failed to submit urb %p, err %d",
                    __func__, urb, err);
            kfree(urb->setup_packet);
            usb_unanchor_urb(urb);
        } else {
            usb_mark_last_busy(data->udev);
        }
        usb_free_urb(urb);

        data->tx_in_flight++;
    }
    mdelay(URB_CANCELING_DELAY_MS);
    usb_scuttle_anchored_urbs(&data->deferred);
}

static int btusb_resume(struct usb_interface *intf)
{
    struct btusb_data *data = usb_get_intfdata(intf);
    struct hci_dev *hdev = data->hdev;
    firmware_info *fw_info = data->fw_info;
    int err = 0;

    RTKBT_INFO("%s: Suspend count %d", __func__, data->suspend_count);

    if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
        return 0;

    if (--data->suspend_count)
        return 0;

    /*check_fw_version to check the status of the BT Controller after USB Resume*/
    err = check_fw_version(fw_info);
    if (err !=0)
    {
        RTKBT_INFO("%s: BT Controller Power OFF And Return hci_hardware_error:%d", __func__, err);
        hci_hardware_error();
    }


    if (test_bit(BTUSB_INTR_RUNNING, &data->flags)) {
        err = btusb_submit_intr_urb(hdev, GFP_NOIO);
        if (err < 0) {
            clear_bit(BTUSB_INTR_RUNNING, &data->flags);
            goto failed;
        }
    }

    if (test_bit(BTUSB_BULK_RUNNING, &data->flags)) {
        err = btusb_submit_bulk_urb(hdev, GFP_NOIO);
        if (err < 0) {
            clear_bit(BTUSB_BULK_RUNNING, &data->flags);
            goto failed;
        }

        btusb_submit_bulk_urb(hdev, GFP_NOIO);
    }

    if (test_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
        if (btusb_submit_isoc_urb(hdev, GFP_NOIO) < 0)
            clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
        else
            btusb_submit_isoc_urb(hdev, GFP_NOIO);
    }

    spin_lock_irq(&data->txlock);
    play_deferred(data);
    clear_bit(BTUSB_SUSPENDING, &data->flags);
    spin_unlock_irq(&data->txlock);
    schedule_work(&data->work);

    return 0;

failed:
    mdelay(URB_CANCELING_DELAY_MS);
    usb_scuttle_anchored_urbs(&data->deferred);
    spin_lock_irq(&data->txlock);
    clear_bit(BTUSB_SUSPENDING, &data->flags);
    spin_unlock_irq(&data->txlock);

    return err;
}
#endif

static struct usb_driver btusb_driver = {
    .name        = "rtk_btusb",
    .probe        = btusb_probe,
    .disconnect    = btusb_disconnect,
#ifdef CONFIG_PM
    .suspend    = btusb_suspend,
    .resume        = btusb_resume,
#endif
#if CONFIG_RESET_RESUME
    .reset_resume    = btusb_resume,
#endif
    .id_table    = btusb_table,
    .supports_autosuspend = 1,
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 7, 1)
    .disable_hub_initiated_lpm = 1,
#endif
};

static int __init btusb_init(void)
{
    int err;

    RTKBT_INFO("RTKBT_RELEASE_NAME: %s",RTKBT_RELEASE_NAME);
    RTKBT_INFO("Realtek Bluetooth USB driver module init, version %s", VERSION);
#if CONFIG_BLUEDROID
    err = btchr_init();
    if (err < 0) {
        /* usb register will go on, even bt char register failed */
        RTKBT_ERR("Failed to register usb char device interfaces");
    } else
        bt_char_dev_registered = 1;
#endif
    err = usb_register(&btusb_driver);
    if (err < 0)
        RTKBT_ERR("Failed to register RTK bluetooth USB driver");
    return err;
}

static void __exit btusb_exit(void)
{
    RTKBT_INFO("Realtek Bluetooth USB driver module exit");
#if CONFIG_BLUEDROID
    if (bt_char_dev_registered > 0)
        btchr_exit();
#endif
    usb_deregister(&btusb_driver);
}

module_init(btusb_init);
module_exit(btusb_exit);


module_param(mp_drv_mode, int, 0644);
MODULE_PARM_DESC(mp_drv_mode, "0: NORMAL; 1: MP MODE");


MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek Bluetooth USB driver version");
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
