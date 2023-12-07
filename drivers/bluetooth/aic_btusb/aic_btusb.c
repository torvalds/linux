/*
 *
 *  AicSemi Bluetooth USB driver
 *
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
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
#include <linux/init.h>/
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

#include "aic_btusb.h"

#ifdef CONFIG_USE_FW_REQUEST
#include <linux/firmware.h>
#endif

#define AICBT_RELEASE_NAME "202012_ANDROID"
#define VERSION "2.1.0"

#define SUSPNED_DW_FW 0


static spinlock_t queue_lock;
static spinlock_t dlfw_lock;
static volatile uint16_t    dlfw_dis_state = 0;

/* USB Device ID */
#define USB_VENDOR_ID_AIC                0xA69C
#define USB_PRODUCT_ID_AIC8801				0x8801
#define USB_PRODUCT_ID_AIC8800DC			0x88dc
#define USB_PRODUCT_ID_AIC8800D80			0x8d81

enum AICWF_IC{
	PRODUCT_ID_AIC8801	=	0,
	PRODUCT_ID_AIC8800DC,
	PRODUCT_ID_AIC8800DW,
	PRODUCT_ID_AIC8800D80
};

u16 g_chipid = PRODUCT_ID_AIC8801;
u8 chip_id = 0;
u8 sub_chip_id = 0;

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

#if (CONFIG_BLUEDROID == 0)
#if HCI_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
		spinlock_t rxlock;
		struct sk_buff *evt_skb;
		struct sk_buff *acl_skb;
		struct sk_buff *sco_skb;
#endif
#endif

    struct usb_endpoint_descriptor *intr_ep;
    struct usb_endpoint_descriptor *bulk_tx_ep;
    struct usb_endpoint_descriptor *bulk_rx_ep;
    struct usb_endpoint_descriptor *isoc_tx_ep;
    struct usb_endpoint_descriptor *isoc_rx_ep;

    __u8 cmdreq_type;

    unsigned int sco_num;
    int isoc_altsetting;
    int suspend_count;
    uint16_t sco_handle;

#if (CONFIG_BLUEDROID == 0)
#if HCI_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
    int (*recv_bulk) (struct btusb_data * data, void *buffer, int count);
#endif
#endif

//#ifdef CONFIG_HAS_EARLYSUSPEND
#if 0
    struct early_suspend early_suspend;
#else
    struct notifier_block pm_notifier;
    struct notifier_block reboot_notifier;
#endif
    firmware_info *fw_info;

#ifdef CONFIG_SCO_OVER_HCI
    AIC_sco_card_t  *pSCOSnd;
#endif
};
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 1)
static bool reset_on_close = 0;
#endif

#ifdef CONFIG_SCO_OVER_HCI
struct snd_sco_cap_timer {
	struct timer_list cap_timer;
	struct timer_list play_timer;
	struct btusb_data snd_usb_data;
	int snd_sco_length;
};
static struct snd_sco_cap_timer snd_cap_timer;
#endif


int bt_support = 0;
module_param(bt_support, int, 0660);

#ifdef CONFIG_SUPPORT_VENDOR_APCF
int vendor_apcf_sent_done = 0;
#endif

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




static void aic_free( struct btusb_data *data)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 1)
    kfree(data);
#endif
    return;
}

static struct btusb_data *aic_alloc(struct usb_interface *intf)
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
    //uint wlength = skb->len;
    u16 *handle = (u16 *)(skb->data);
    u16 len = *(handle+1);
    //u8 *acl_data = (u8 *)(skb->data);

    AICBT_INFO("aic %s: direction %d, handle %04x, len %d",
            __func__, direction, *handle, len);
#endif
}

static void print_sco(struct sk_buff *skb, int direction)
{
#if PRINT_SCO_DATA
    uint wlength = skb->len;
    u16 *handle = (u16 *)(skb->data);
    u8 len = *(u8 *)(handle+1);
    //u8 *sco_data =(u8 *)(skb->data);

    AICBT_INFO("aic %s: direction %d, handle %04x, len %d,wlength %d",
            __func__, direction, *handle, len,wlength);
#endif
}

static void print_error_command(struct sk_buff *skb)
{
    u16 *opcode = (u16*)(skb->data);
    u8 *cmd_data = (u8*)(skb->data);
    u8 len = *(cmd_data+2);

	printk(" 0x%04x,len:%d,", *opcode, len);
#if CONFIG_BLUEDROID
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
	case HCI_OP_Write_Simple_Pairing_Mode:
		printk("HCI_OP_Write_Simple_Pairing_Mode");
		break;
	case HCI_OP_Read_Buffer_Size:
		printk("HCI_OP_Read_Buffer_Size");
		break;
	case HCI_OP_Host_Buffer_Size:
		printk("HCI_OP_Host_Buffer_Size");
		break;
	case HCI_OP_Read_Local_Version_Information:
		printk("HCI_OP_Read_Local_Version_Information");
		break;
	case HCI_OP_Read_BD_ADDR:
		printk("HCI_OP_Read_BD_ADDR");
		break;
	case HCI_OP_Read_Local_Supported_Commands:
		printk("HCI_OP_Read_Local_Supported_Commands");
		break;
	case HCI_OP_Write_Scan_Enable:
		printk("HCI_OP_Write_Scan_Enable");
		break;
	case HCI_OP_Write_Current_IAC_LAP:
		printk("HCI_OP_Write_Current_IAC_LAP");
		break;
	case HCI_OP_Write_Inquiry_Scan_Activity:
		printk("HCI_OP_Write_Inquiry_Scan_Activity");
		break;
	case HCI_OP_Write_Class_of_Device:
		printk("HCI_OP_Write_Class_of_Device");
		break;
	case HCI_OP_LE_Rand:
		printk("HCI_OP_LE_Rand");
		break;
	case HCI_OP_LE_Set_Random_Address:
		printk("HCI_OP_LE_Set_Random_Address");
		break;
	case HCI_OP_LE_Set_Extended_Scan_Enable:
		printk("HCI_OP_LE_Set_Extended_Scan_Enable");
		break;
	case HCI_OP_LE_Set_Extended_Scan_Parameters:
		printk("HCI_OP_LE_Set_Extended_Scan_Parameters");
		break;	
	case HCI_OP_Set_Event_Filter:
		printk("HCI_OP_Set_Event_Filter");
		break;
	case HCI_OP_Write_Voice_Setting:
		printk("HCI_OP_Write_Voice_Setting");
		break;
	case HCI_OP_Change_Local_Name:
		printk("HCI_OP_Change_Local_Name");
		break;
	case HCI_OP_Read_Local_Name:
		printk("HCI_OP_Read_Local_Name");
		break;
	case HCI_OP_Wirte_Page_Timeout:
		printk("HCI_OP_Wirte_Page_Timeout");
		break;
	case HCI_OP_LE_Clear_Resolving_List:
		printk("HCI_OP_LE_Clear_Resolving_List");
		break;
	case HCI_OP_LE_Set_Addres_Resolution_Enable_Command:
		printk("HCI_OP_LE_Set_Addres_Resolution_Enable_Command");
		break;
	case HCI_OP_Write_Inquiry_mode:
		printk("HCI_OP_Write_Inquiry_mode");
		break;
	case HCI_OP_Write_Page_Scan_Type:
		printk("HCI_OP_Write_Page_Scan_Type");
		break;
	case HCI_OP_Write_Inquiry_Scan_Type:
		printk("HCI_OP_Write_Inquiry_Scan_Type");
		break;
	case HCI_OP_Delete_Stored_Link_Key:
		printk("HCI_OP_Delete_Stored_Link_Key");
		break;
	case HCI_OP_LE_Read_Local_Resolvable_Address:
		printk("HCI_OP_LE_Read_Local_Resolvable_Address");
		break;
	case HCI_OP_LE_Extended_Create_Connection:
		printk("HCI_OP_LE_Extended_Create_Connection");
		break;
	case HCI_OP_Read_Remote_Version_Information:
		printk("HCI_OP_Read_Remote_Version_Information");
		break;
	case HCI_OP_LE_Start_Encryption:
		printk("HCI_OP_LE_Start_Encryption");
		break;
	case HCI_OP_LE_Add_Device_to_Resolving_List:
		printk("HCI_OP_LE_Add_Device_to_Resolving_List");
		break;
	case HCI_OP_LE_Set_Privacy_Mode:
		printk("HCI_OP_LE_Set_Privacy_Mode");
		break;
	case HCI_OP_LE_Connection_Update:
		printk("HCI_OP_LE_Connection_Update");
		break;
    default:
        printk("UNKNOW_HCI_COMMAND");
        break;
    }
#endif //CONFIG_BLUEDROID
}

static void print_command(struct sk_buff *skb)
{
#if PRINT_CMD_EVENT
    print_error_command(skb);
#endif
}


enum CODEC_TYPE{
    CODEC_CVSD,
    CODEC_MSBC,
};

static enum CODEC_TYPE codec_type = CODEC_CVSD;
static void set_select_msbc(enum CODEC_TYPE type);
static enum CODEC_TYPE check_select_msbc(void);


#if CONFIG_BLUEDROID

/* Global parameters for bt usb char driver */
#define BT_CHAR_DEVICE_NAME "aicbt_dev"
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

#ifdef CONFIG_SUPPORT_VENDOR_APCF
static int bypass_event(struct sk_buff *skb)
{
	int ret = 0;
	u8 *opcode = (u8*)(skb->data);
	//u8 len = *(opcode+1);
	u16 sub_opcpde;

	switch(*opcode) {
		case HCI_EV_CMD_COMPLETE:
			sub_opcpde = ((u16)opcode[3]|(u16)(opcode[4])<<8);
			if(sub_opcpde == 0xfd57){
				if(vendor_apcf_sent_done){
					vendor_apcf_sent_done--;
					printk("apcf bypass\r\n");
					ret = 1;
				}
			}
			break;
		default:
			break;
	}
	return ret;
}
#endif//CONFIG_SUPPORT_VENDOR_APCF
static void print_event(struct sk_buff *skb)
{
#if PRINT_CMD_EVENT
    //uint wlength = skb->len;
    //uint icount = 0;
    u8 *opcode = (u8*)(skb->data);
    //u8 len = *(opcode+1);

    printk("aic %s ", __func__);
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
        printk("unknow event");
        break;
    }
	printk("\n");
#if 0
    printk("%02x,len:%d,", *opcode,len);
    for (icount = 2; (icount < wlength) && (icount < 24); icount++)
        printk("%02x ", *(opcode+icount));
    printk("\n");
#endif
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

static struct sk_buff *aic_skb_queue[QUEUE_SIZE];
static int aic_skb_queue_front = 0;
static int aic_skb_queue_rear = 0;

static void aic_enqueue(struct sk_buff *skb)
{
    spin_lock(&queue_lock);
    if (aic_skb_queue_front == (aic_skb_queue_rear + 1) % QUEUE_SIZE) {
        /*
         * If queue is full, current solution is to drop
         * the following entries.
         */
        AICBT_WARN("%s: Queue is full, entry will be dropped", __func__);
    } else {
        aic_skb_queue[aic_skb_queue_rear] = skb;

        aic_skb_queue_rear++;
        aic_skb_queue_rear %= QUEUE_SIZE;

    }
    spin_unlock(&queue_lock);
}

static struct sk_buff *aic_dequeue_try(unsigned int deq_len)
{
    struct sk_buff *skb;
    struct sk_buff *skb_copy;

    if (aic_skb_queue_front == aic_skb_queue_rear) {
        AICBT_WARN("%s: Queue is empty", __func__);
        return NULL;
    }

    skb = aic_skb_queue[aic_skb_queue_front];
    if (deq_len >= skb->len) {

        aic_skb_queue_front++;
        aic_skb_queue_front %= QUEUE_SIZE;

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
    return (aic_skb_queue_front == aic_skb_queue_rear) ? 1 : 0;
}

static void aic_clear_queue(void)
{
    struct sk_buff *skb;
    spin_lock(&queue_lock);
    while(!is_queue_empty()) {
        skb = aic_skb_queue[aic_skb_queue_front];
        aic_skb_queue[aic_skb_queue_front] = NULL;
        aic_skb_queue_front++;
        aic_skb_queue_front %= QUEUE_SIZE;
        if (skb) {
            kfree_skb(skb);
        }
    }
    spin_unlock(&queue_lock);
}

/*
 * AicSemi - Integrate from hci_core.c
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

    AICBT_DBG("%s: dev %d", __func__, dev);

    hdev = hci_dev_get(dev);
    if (!hdev) {
        AICBT_ERR("%s: Failed to get hci dev[Null]", __func__);
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
        AICBT_ERR("%s: failed to get hci dev[Null]", __func__);
        return -ENODEV;
    }

    err = hci_dev_do_close(hdev);

    return err;
}

#ifdef CONFIG_SCO_OVER_HCI
/* copy data from the URB buffer into the ALSA ring buffer */
static bool aic_copy_capture_data_to_alsa(struct btusb_data *data, uint8_t* p_data, unsigned int frames)
{
 	struct snd_pcm_runtime *runtime;
	unsigned int frame_bytes, frames1;
	u8 *dest;
    AIC_sco_card_t  *pSCOSnd = data->pSCOSnd;

    runtime = pSCOSnd->capture.substream->runtime;
    frame_bytes = 2;

        dest = runtime->dma_area + pSCOSnd->capture.buffer_pos * frame_bytes;
    if (pSCOSnd->capture.buffer_pos + frames <= runtime->buffer_size) {
  		memcpy(dest, p_data, frames * frame_bytes);
  	} else {
  		/* wrap around at end of ring buffer */
        frames1 = runtime->buffer_size - pSCOSnd->capture.buffer_pos;
  		memcpy(dest, p_data, frames1 * frame_bytes);
  		memcpy(runtime->dma_area,
  		       p_data + frames1 * frame_bytes,
  		       (frames - frames1) * frame_bytes);
  	}

    pSCOSnd->capture.buffer_pos += frames;
    if (pSCOSnd->capture.buffer_pos >= runtime->buffer_size) {
        pSCOSnd->capture.buffer_pos -= runtime->buffer_size;
    }

    if((pSCOSnd->capture.buffer_pos%runtime->period_size) == 0) {
        snd_pcm_period_elapsed(pSCOSnd->capture.substream);
    }

    return false;
}


static void hci_send_to_alsa_ringbuffer(struct hci_dev *hdev, struct sk_buff *skb)
{
    struct btusb_data *data = GET_DRV_DATA(hdev);
    AIC_sco_card_t  *pSCOSnd = data->pSCOSnd;
    uint8_t* p_data;
    int sco_length = skb->len - HCI_SCO_HDR_SIZE;
    u16 *handle = (u16 *) (skb->data);
    //u8 errflg = (u8)((*handle & 0x3000) >> 12);

    pSCOSnd->usb_data->sco_handle = (*handle & 0x0fff);

    AICBT_DBG("%s, %x, %x %x\n", __func__,pSCOSnd->usb_data->sco_handle, *handle, errflg);

    if (!hdev) {
        AICBT_INFO("%s: Frame for unknown HCI device", __func__);
        return;
    }

    if (!test_bit(ALSA_CAPTURE_RUNNING, &pSCOSnd->states)) {
        AICBT_INFO("%s: ALSA is not running", __func__);
        return;
    }
	snd_cap_timer.snd_sco_length = sco_length;
    p_data = (uint8_t *)skb->data + HCI_SCO_HDR_SIZE;
    aic_copy_capture_data_to_alsa(data, p_data, sco_length/2);
}

#endif

#if CONFIG_BLUEDROID
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

    AICBT_DBG("%s: %p name %s bus %d", __func__, hdev, hdev->name, hdev->bus);
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

    AICBT_DBG("%s: id %d, name %s", __func__, hdev->id, hdev->name);


    for (i = 0; i < NUM_REASSEMBLY; i++)
        hdev->reassembly[i] = NULL;

    memset(&hdev->stat, 0, sizeof(struct hci_dev_stats));
    atomic_set(&hdev->promisc, 0);

    if (ghdev) {
        write_unlock(&hci_dev_lock);
        AICBT_ERR("%s: Hci device has been registered already", __func__);
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

    AICBT_DBG("%s: hdev %p name %s bus %d", __func__, hdev, hdev->name, hdev->bus);
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
    struct sk_buff *aic_skb_copy = NULL;

    //AICBT_DBG("%s", __func__);

    if (!hdev) {
        AICBT_ERR("%s: Frame for unknown HCI device", __func__);
        return;
    }

    if (!test_bit(HCI_RUNNING, &hdev->flags)) {
        AICBT_ERR("%s: HCI not running", __func__);
        return;
    }

    aic_skb_copy = pskb_copy(skb, GFP_ATOMIC);
    if (!aic_skb_copy) {
        AICBT_ERR("%s: Copy skb error", __func__);
        return;
    }

    memcpy(skb_push(aic_skb_copy, 1), &bt_cb(skb)->pkt_type, 1);
    aic_enqueue(aic_skb_copy);

    /* Make sure bt char device existing before wakeup read queue */
    hdev = hci_dev_get(0);
    if (hdev) {
        //AICBT_DBG("%s: Try to wakeup read queue", __func__);
        AICBT_DBG("%s", __func__);
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
#ifdef CONFIG_SCO_OVER_HCI
        if(bt_cb(skb)->pkt_type == HCI_SCODATA_PKT){
            hci_send_to_alsa_ringbuffer(hdev, skb);
        }else{
#ifdef CONFIG_SUPPORT_VENDOR_APCF
        	if(bt_cb(skb)->pkt_type == HCI_EVENT_PKT){
				if(bypass_event(skb)){
					kfree_skb(skb);
					return 0;
				}
			}
#endif //CONFIG_SUPPORT_VENDOR_APCF
			hci_send_to_stack(hdev, skb);
		}
#else
#ifdef CONFIG_SUPPORT_VENDOR_APCF
		if(bt_cb(skb)->pkt_type == HCI_EVENT_PKT){
			if(bypass_event(skb)){
				kfree_skb(skb);
				return 0;
			}
		}
#endif //CONFIG_SUPPORT_VENDOR_APCF
		/* Send copy to the sockets */
		hci_send_to_stack(hdev, skb);
#endif

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

    //AICBT_DBG("%s", __func__);

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
#endif //CONFIG_BLUEDROID

void hci_hardware_error(void)
{
    struct sk_buff *aic_skb_copy = NULL;
    int len = 3;
    uint8_t hardware_err_pkt[4] = {HCI_EVENT_PKT, 0x10, 0x01, HCI_VENDOR_USB_DISC_HARDWARE_ERROR};

    aic_skb_copy = alloc_skb(len, GFP_ATOMIC);
    if (!aic_skb_copy) {
        AICBT_ERR("%s: Failed to allocate mem", __func__);
        return;
    }

    memcpy(skb_put(aic_skb_copy, len), hardware_err_pkt, len);
    aic_enqueue(aic_skb_copy);

    wake_up_interruptible(&btchr_read_wait);
}

static int btchr_open(struct inode *inode_p, struct file  *file_p)
{
    struct btusb_data *data;
    struct hci_dev *hdev;

    AICBT_DBG("%s: BT usb char device is opening", __func__);
    /* Not open unless wanna tracing log */
    /* trace_printk("%s: open....\n", __func__); */

    hdev = hci_dev_get(0);
    if (!hdev) {
        AICBT_DBG("%s: Failed to get hci dev[NULL]", __func__);
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

    aic_clear_queue();
    return nonseekable_open(inode_p, file_p);
}

static int btchr_close(struct inode  *inode_p, struct file   *file_p)
{
    struct btusb_data *data;
    struct hci_dev *hdev;

    AICBT_INFO("%s: BT usb char device is closing", __func__);
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

    while (count) {
        hdev = hci_dev_get(0);
        if (!hdev) {
            /*
             * Note: Only when BT device hotplugged out, we wil get
             * into such situation. In order to keep the upper layer
             * stack alive (blocking the read), we should never return
             * EFAULT or break the loop.
             */
            AICBT_ERR("%s: Failed to get hci dev[Null]", __func__);
        }

        ret = wait_event_interruptible(btchr_read_wait, !is_queue_empty());
        if (ret < 0) {
            AICBT_ERR("%s: wait event is signaled %d", __func__, (int)ret);
            break;
        }

        skb = aic_dequeue_try(count);
        if (skb) {
            ret = usb_put_user(skb, buf_p, count);
            if (ret < 0)
                AICBT_ERR("%s: Failed to put data to user space", __func__);
            kfree_skb(skb);
            break;
        }
    }

    return ret;
}

#ifdef CONFIG_SUPPORT_VENDOR_APCF
void btchr_external_write(char* buff, int len){
	struct hci_dev *hdev;
	struct sk_buff *skb;
	int i;
	struct btusb_data *data;

	AICBT_INFO("%s \r\n", __func__);
	for(i=0;i<len;i++){
		printk("0x%x ",(u8)buff[i]);
	}
	printk("\r\n");
	hdev = hci_dev_get(0);
	if (!hdev) {
		AICBT_WARN("%s: Failed to get hci dev[Null]", __func__);
		return;
	}
    /* Never trust on btusb_data, as bt device may be hotplugged out */
    data = GET_DRV_DATA(hdev);
    if (!data) {
        AICBT_WARN("%s: Failed to get bt usb driver data[Null]", __func__);
        return;
    }
    vendor_apcf_sent_done++;

	skb = bt_skb_alloc(len, GFP_ATOMIC);
    if (!skb)
        return;
    skb_reserve(skb, -1); // Add this line
    skb->dev = (void *)hdev;
	memcpy((__u8 *)skb->data,(__u8 *)buff,len);
	skb_put(skb, len);
    bt_cb(skb)->pkt_type = *((__u8 *)skb->data);
    skb_pull(skb, 1);
    data->hdev->send(skb);
}

EXPORT_SYMBOL(btchr_external_write);
#endif //CONFIG_SUPPORT_VENDOR_APCF

static ssize_t btchr_write(struct file *file_p,
        const char __user *buf_p,
        size_t count,
        loff_t *pos_p)
{
    struct btusb_data *data = file_p->private_data;
    struct hci_dev *hdev;
    struct sk_buff *skb;

    //AICBT_DBG("%s: BT usb char device is writing", __func__);
    AICBT_DBG("%s", __func__);

    hdev = hci_dev_get(0);
    if (!hdev) {
        AICBT_WARN("%s: Failed to get hci dev[Null]", __func__);
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
        AICBT_WARN("%s: Failed to get bt usb driver data[Null]", __func__);
        return count;
    }

    if (count > HCI_MAX_FRAME_SIZE)
        return -EINVAL;

    skb = bt_skb_alloc(count, GFP_ATOMIC);
    if (!skb)
        return -ENOMEM;
    skb_reserve(skb, -1); // Add this line

    if (copy_from_user(skb_put(skb, count), buf_p, count)) {
        AICBT_ERR("%s: Failed to get data from user space", __func__);
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

    //AICBT_DBG("%s: BT usb char device is polling", __func__);

    if(!bt_char_dev_registered) {
        AICBT_ERR("%s: char device has not registered!", __func__);
        return POLLERR | POLLHUP;
    }

    poll_wait(file_p, &btchr_read_wait, wait);

    hdev = hci_dev_get(0);
    if (!hdev) {
        AICBT_ERR("%s: Failed to get hci dev[Null]", __func__);
        mdelay(URB_CANCELING_DELAY_MS);
        return POLLERR | POLLHUP;
        return POLLOUT | POLLWRNORM;
    }

    /* Never trust on btusb_data, as bt device may be hotplugged out */
    data = GET_DRV_DATA(hdev);
    if (!data) {
        /*
         * When bt device is hotplugged out, btusb_data will
         * be freed in disconnect.
         */
        AICBT_ERR("%s: Failed to get bt usb driver data[Null]", __func__);
        mdelay(URB_CANCELING_DELAY_MS);
        return POLLOUT | POLLWRNORM;
    }

    if (!is_queue_empty())
        return POLLIN | POLLRDNORM;

    return POLLOUT | POLLWRNORM;
}
static long btchr_ioctl(struct file *file_p,unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    struct hci_dev *hdev;
    struct btusb_data *data;
    firmware_info *fw_info;

    if(!bt_char_dev_registered) {
        return -ENODEV;
    }

    if(check_set_dlfw_state_value(1) != 1) {
        AICBT_ERR("%s bt controller is disconnecting!", __func__);
        return 0;
    }

    hdev = hci_dev_get(0);
    if(!hdev) {
        AICBT_ERR("%s device is NULL!", __func__);
        set_dlfw_state_value(0);
        return 0;
    }
    data = GET_DRV_DATA(hdev);
    fw_info = data->fw_info;

    AICBT_INFO(" btchr_ioctl DOWN_FW_CFG with Cmd:%d",cmd);
    switch (cmd) {
        case DOWN_FW_CFG:
            AICBT_INFO(" btchr_ioctl DOWN_FW_CFG");
            ret = usb_autopm_get_interface(data->intf);
            if (ret < 0){
                goto failed;
            }

            //ret = download_patch(fw_info,1);
            usb_autopm_put_interface(data->intf);
            if(ret < 0){
                AICBT_ERR("%s:Failed in download_patch with ret:%d",__func__,ret);
                goto failed;
            }

            ret = hdev->open(hdev);
            if(ret < 0){
                AICBT_ERR("%s:Failed in hdev->open(hdev):%d",__func__,ret);
                goto failed;
            }
            set_bit(HCI_UP, &hdev->flags);
            set_dlfw_state_value(0);
            wake_up_interruptible(&bt_dlfw_wait);
            return 1;
        case DWFW_CMPLT:
            AICBT_INFO(" btchr_ioctl DWFW_CMPLT");
#if 1
	case SET_ISO_CFG:
            AICBT_INFO("btchr_ioctl SET_ISO_CFG");
		if(copy_from_user(&(hdev->voice_setting), (__u16*)arg, sizeof(__u16))){
			AICBT_INFO(" voice settings err");
		}
		//hdev->voice_setting = *(uint16_t*)arg;
		AICBT_INFO(" voice settings = %d", hdev->voice_setting);
		//return 1;
#endif
        case GET_USB_INFO:
			//ret = download_patch(fw_info,1);
            AICBT_INFO(" btchr_ioctl GET_USB_INFO");
            ret = hdev->open(hdev);
            if(ret < 0){
                AICBT_ERR("%s:Failed in hdev->open(hdev):%d",__func__,ret);
                //goto done;
            }
            set_bit(HCI_UP, &hdev->flags);
            set_dlfw_state_value(0);
            wake_up_interruptible(&bt_dlfw_wait);
            return 1;
        case RESET_CONTROLLER:
            AICBT_INFO(" btchr_ioctl RESET_CONTROLLER");
            //reset_controller(fw_info);
            return 1;
        default:
            AICBT_ERR("%s:Failed with wrong Cmd:%d",__func__,cmd);
            goto failed;
        }
    failed:
        set_dlfw_state_value(0);
        wake_up_interruptible(&bt_dlfw_wait);
        return ret;

}

#ifdef CONFIG_PLATFORM_UBUNTU//AIDEN
typedef u32		compat_uptr_t;
static inline void __user *compat_ptr(compat_uptr_t uptr)
{
	return (void __user *)(unsigned long)uptr;
}
#endif

#ifdef CONFIG_COMPAT
static long compat_btchr_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
    AICBT_DBG("%s: enter",__func__);
    return btchr_ioctl(filp, cmd, (unsigned long) compat_ptr(arg));
}
#endif
static struct file_operations bt_chrdev_ops  = {
    open    :    btchr_open,
    release    :    btchr_close,
    read    :    btchr_read,
    write    :    btchr_write,
    poll    :    btchr_poll,
    unlocked_ioctl   :   btchr_ioctl,
#ifdef CONFIG_COMPAT
    compat_ioctl :  compat_btchr_ioctl,
#endif
};

static int btchr_init(void)
{
    int res = 0;
    struct device *dev;

    AICBT_INFO("Register usb char device interface for BT driver");
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
        AICBT_ERR("Failed to create bt char class");
        return PTR_ERR(bt_char_class);
    }

    res = alloc_chrdev_region(&bt_devid, 0, 1, BT_CHAR_DEVICE_NAME);
    if (res < 0) {
        AICBT_ERR("Failed to allocate bt char device");
        goto err_alloc;
    }

    dev = device_create(bt_char_class, NULL, bt_devid, NULL, BT_CHAR_DEVICE_NAME);
    if (IS_ERR(dev)) {
        AICBT_ERR("Failed to create bt char device");
        res = PTR_ERR(dev);
        goto err_create;
    }

    cdev_init(&bt_char_dev, &bt_chrdev_ops);
    res = cdev_add(&bt_char_dev, bt_devid, 1);
    if (res < 0) {
        AICBT_ERR("Failed to add bt char device");
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
    AICBT_INFO("Unregister usb char device interface for BT driver");

    device_destroy(bt_char_class, bt_devid);
    cdev_del(&bt_char_dev);
    unregister_chrdev_region(bt_devid, 1);
    class_destroy(bt_char_class);

    return;
}
#endif

int send_hci_cmd(firmware_info *fw_info)
{

    int len = 0;
    int ret_val = -1;
	int i = 0;

	if(g_chipid == PRODUCT_ID_AIC8801 || g_chipid == PRODUCT_ID_AIC8800D80){
	    ret_val = usb_bulk_msg(fw_info->udev, fw_info->pipe_out, fw_info->send_pkt, fw_info->pkt_len,
	            &len, 3000);
	    if (ret_val || (len != fw_info->pkt_len)) {
	        AICBT_INFO("Error in send hci cmd = %d,"
	            "len = %d, size = %d", ret_val, len, fw_info->pkt_len);
	    }
	}else if(g_chipid == PRODUCT_ID_AIC8800DC){
		while((ret_val<0)&&(i++<3))
		{
			ret_val = usb_control_msg(
			   fw_info->udev, fw_info->pipe_out,
			   0, USB_TYPE_CLASS, 0, 0,
			   (void *)(fw_info->send_pkt),
			   fw_info->pkt_len, MSG_TO);
		}

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
            (void *)(fw_info->rcv_pkt), RCV_PKT_LEN,
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
    int ret_val;

    AICBT_INFO("%s: %s", __func__, onoff != 0 ? "on" : "off");

    fw_info->cmd_hdr->opcode = cpu_to_le16(BTOFF_OPCODE);
    fw_info->cmd_hdr->plen = 1;
    fw_info->pkt_len = CMD_HDR_LEN + 1;
    fw_info->send_pkt[CMD_HDR_LEN] = onoff;

    ret_val = send_hci_cmd(fw_info);
    if (ret_val < 0) {
        AICBT_ERR("%s: Failed to send bt %s cmd, errno %d",
                __func__, onoff != 0 ? "on" : "off", ret_val);
        return ret_val;
    }

    ret_val = rcv_hci_evt(fw_info);
    if (ret_val < 0) {
        AICBT_ERR("%s: Failed to receive bt %s event, errno %d",
                __func__, onoff != 0 ? "on" : "off", ret_val);
        return ret_val;
    }

    return ret_val;
}

//for 8800DC start
u32 fwcfg_tbl[][2] = {
    {0x40200028, 0x0021047e},
    {0x40200024, 0x0000011d},
};

int fw_config(firmware_info* fw_info)
{
    int ret_val = -1;
    struct hci_dbg_rd_mem_cmd *rd_cmd;
    struct hci_dbg_rd_mem_cmd_evt *evt_para;
    int len = 0, i = 0;
    struct fw_status *evt_status;

    rd_cmd = (struct hci_dbg_rd_mem_cmd *)(fw_info->req_para);
    if (!rd_cmd)
        return -ENOMEM;

    rd_cmd->start_addr = 0x40200024;
    rd_cmd->type = 32;
    rd_cmd->length = 4;
    fw_info->cmd_hdr->opcode = cpu_to_le16(HCI_VSC_DBG_RD_MEM_CMD);
    fw_info->cmd_hdr->plen = sizeof(struct hci_dbg_rd_mem_cmd);
    fw_info->pkt_len = CMD_HDR_LEN + sizeof(struct hci_dbg_rd_mem_cmd);

    ret_val = send_hci_cmd(fw_info);
    if (ret_val < 0) {
        printk("%s: Failed to send hci cmd 0x%04x, errno %d",
                __func__, fw_info->cmd_hdr->opcode, ret_val);
        return ret_val;
    }

    ret_val = rcv_hci_evt(fw_info);
    if (ret_val < 0) {
        printk("%s: Failed to receive hci event, errno %d",
                __func__, ret_val);
        return ret_val;
    }

    evt_para = (struct hci_dbg_rd_mem_cmd_evt *)(fw_info->rsp_para);

    printk("%s: fw status = 0x%04x, length %d, %x %x %x %x",
            __func__, evt_para->status, evt_para->length,
            evt_para->data[0],
            evt_para->data[1],
            evt_para->data[2],
            evt_para->data[3]);

    ret_val = evt_para->status;
    if (evt_para->status == 0) {
        uint16_t rd_data = (evt_para->data[0] | (evt_para->data[1] << 8));
        printk("%s rd_data is %x\n", __func__, rd_data);
        if (rd_data == 0x119) {
            struct aicbt_patch_table_cmd *patch_table_cmd = (struct aicbt_patch_table_cmd *)(fw_info->req_para);
            len = sizeof(fwcfg_tbl) / sizeof(u32) / 2;
            patch_table_cmd->patch_num = len;
            for (i = 0; i < len; i++) {
                memcpy(&patch_table_cmd->patch_table_addr[i], &fwcfg_tbl[i][0], sizeof(uint32_t));
                memcpy(&patch_table_cmd->patch_table_data[i], &fwcfg_tbl[i][1], sizeof(uint32_t));
                printk("%s [%d] data: %08x %08x\n", __func__, i, patch_table_cmd->patch_table_addr[i],patch_table_cmd->patch_table_data[i]);
            }
            fw_info->cmd_hdr->opcode = cpu_to_le16(HCI_VSC_UPDATE_PT_CMD);
            fw_info->cmd_hdr->plen = HCI_VSC_UPDATE_PT_SIZE;
            fw_info->pkt_len = fw_info->cmd_hdr->plen + 3;
            ret_val = send_hci_cmd(fw_info);
            if (ret_val < 0) {
                AICBT_ERR("%s: rcv_hci_evt err %d", __func__, ret_val);
                return ret_val;
            }
            ret_val = rcv_hci_evt(fw_info);
            if (ret_val < 0) {
                printk("%s: Failed to receive hci event, errno %d",
                        __func__, ret_val);
                return ret_val;
            }
            evt_status = (struct fw_status *)fw_info->rsp_para;
            ret_val = evt_status->status;
            if (0 != evt_status->status) {
                ret_val = -1;
            } else {
                ret_val = 0;
            }

        }
    }
    return ret_val;
}

int system_config(firmware_info *fw_info)
{
    int ret_val = -1;
    struct hci_dbg_rd_mem_cmd *rd_cmd;
    struct hci_dbg_rd_mem_cmd_evt *evt_para;
    //int len = 0, i = 0;
    //struct fw_status *evt_status;

    rd_cmd = (struct hci_dbg_rd_mem_cmd *)(fw_info->req_para);
    if (!rd_cmd)
        return -ENOMEM;

    rd_cmd->start_addr = 0x40500000;
    rd_cmd->type = 32;
    rd_cmd->length = 4;
    fw_info->cmd_hdr->opcode = cpu_to_le16(HCI_VSC_DBG_RD_MEM_CMD);
    fw_info->cmd_hdr->plen = sizeof(struct hci_dbg_rd_mem_cmd);
    fw_info->pkt_len = CMD_HDR_LEN + sizeof(struct hci_dbg_rd_mem_cmd);

    ret_val = send_hci_cmd(fw_info);
    if (ret_val < 0)
    {
        printk("%s: Failed to send hci cmd 0x%04x, errno %d",
               __func__, fw_info->cmd_hdr->opcode, ret_val);
        return ret_val;
    }

    ret_val = rcv_hci_evt(fw_info);
    if (ret_val < 0)
    {
        printk("%s: Failed to receive hci event, errno %d",
               __func__, ret_val);
        return ret_val;
    }

    evt_para = (struct hci_dbg_rd_mem_cmd_evt *)(fw_info->rsp_para);

    printk("%s: fw status = 0x%04x, length %d, %x %x %x %x",
           __func__, evt_para->status, evt_para->length,
           evt_para->data[0],
           evt_para->data[1],
           evt_para->data[2],
           evt_para->data[3]);

    ret_val = evt_para->status;
    if (evt_para->status == 0)
    {
        uint32_t rd_data = (evt_para->data[0] | (evt_para->data[1] << 8) | (evt_para->data[2] << 16) | (evt_para->data[3] << 24));
        //printk("%s 0x40500000 rd_data is %x\n", __func__, rd_data);
        chip_id = (u8) (rd_data >> 16);
    }

    rd_cmd->start_addr = 0x20;
    rd_cmd->type = 32;
    rd_cmd->length = 4;
    fw_info->cmd_hdr->opcode = cpu_to_le16(HCI_VSC_DBG_RD_MEM_CMD);
    fw_info->cmd_hdr->plen = sizeof(struct hci_dbg_rd_mem_cmd);
    fw_info->pkt_len = CMD_HDR_LEN + sizeof(struct hci_dbg_rd_mem_cmd);

    ret_val = send_hci_cmd(fw_info);
    if (ret_val < 0)
    {
        printk("%s: Failed to send hci cmd 0x%04x, errno %d",
               __func__, fw_info->cmd_hdr->opcode, ret_val);
        return ret_val;
    }

    ret_val = rcv_hci_evt(fw_info);
    if (ret_val < 0)
    {
        printk("%s: Failed to receive hci event, errno %d",
               __func__, ret_val);
        return ret_val;
    }

    evt_para = (struct hci_dbg_rd_mem_cmd_evt *)(fw_info->rsp_para);

    printk("%s: fw status = 0x%04x, length %d, %x %x %x %x",
           __func__, evt_para->status, evt_para->length,
           evt_para->data[0],
           evt_para->data[1],
           evt_para->data[2],
           evt_para->data[3]);

    ret_val = evt_para->status;
    if (evt_para->status == 0)
    {
        uint32_t rd_data = (evt_para->data[0] | (evt_para->data[1] << 8) | (evt_para->data[2] << 16) | (evt_para->data[3] << 24));
        //printk("%s 0x02 rd_data is %x\n", __func__, rd_data);
        sub_chip_id = (u8) (rd_data);
    }
    printk("chip_id = %x, sub_chip_id = %x\n", chip_id, sub_chip_id);
    return ret_val;
}

int check_fw_status(firmware_info* fw_info)
{
    struct fw_status *read_ver_rsp;
    int ret_val = -1;

    fw_info->cmd_hdr->opcode = cpu_to_le16(HCI_VSC_FW_STATUS_GET_CMD);
    fw_info->cmd_hdr->plen = 0;
    fw_info->pkt_len = CMD_HDR_LEN;

    ret_val = send_hci_cmd(fw_info);
    if (ret_val < 0) {
        printk("%s: Failed to send hci cmd 0x%04x, errno %d",
                __func__, fw_info->cmd_hdr->opcode, ret_val);
        return ret_val;
    }

    ret_val = rcv_hci_evt(fw_info);
    if (ret_val < 0) {
        printk("%s: Failed to receive hci event, errno %d",
                __func__, ret_val);
        return ret_val;
    }

    read_ver_rsp = (struct fw_status *)(fw_info->rsp_para);

    printk("%s: fw status = 0x%04x",
            __func__, read_ver_rsp->status);
    return read_ver_rsp->status;
}

int download_data(firmware_info *fw_info, u32 fw_addr, char *filename)
{
    unsigned int i=0;
    int size;
    u8 *dst=NULL;
    int err=0;
    struct hci_dbg_wr_mem_cmd *dl_cmd;
    int hdr_len = sizeof(__le32) + sizeof(__u8) + sizeof(__u8);
    int data_len = HCI_VSC_MEM_WR_SIZE;
    int frag_len = data_len + hdr_len;
    int ret_val;
    int ncmd = 1;
    struct fw_status *evt_para;

    /* load aic firmware */
    size = aic_load_firmware(&dst, filename, NULL);
    if(size <= 0){
            printk("wrong size of firmware file\n");
            vfree(dst);
            dst = NULL;
            return -1;
    }

    dl_cmd = (struct hci_dbg_wr_mem_cmd *)(fw_info->req_para);
    if (!dl_cmd)
        return -ENOMEM;
    evt_para = (struct fw_status *)fw_info->rsp_para;

    /* Copy the file on the Embedded side */
    printk("### Upload %s firmware, @ = %x  size=%d\n", filename, fw_addr, size);

    if (size > HCI_VSC_MEM_WR_SIZE) {// > 1KB data
        for (i = 0; i < (size - HCI_VSC_MEM_WR_SIZE); i += HCI_VSC_MEM_WR_SIZE) {//each time write 240 bytes
            data_len = HCI_VSC_MEM_WR_SIZE;
            frag_len = data_len + hdr_len;
            memcpy(dl_cmd->data, dst + i, data_len);
            dl_cmd->length = data_len;
            dl_cmd->type = 32;
            dl_cmd->start_addr = fw_addr + i;
            fw_info->cmd_hdr->opcode = cpu_to_le16(DOWNLOAD_OPCODE);
            fw_info->cmd_hdr->plen = frag_len;
            fw_info->pkt_len = frag_len + 3;
            #if 0
            printk("[%d] data_len %d, src %x, dst %x\n", i, data_len, dst + i, fw_addr + i);
            printk("%p , %d\n", dl_cmd, fw_info->pkt_len);
            print_hex_dump(KERN_ERR,"payload:",DUMP_PREFIX_NONE,16,1,dl_cmd->data,32,false);
            /* Send download command */
            print_hex_dump(KERN_ERR,"data:",DUMP_PREFIX_NONE,16,1,fw_info->send_pkt,32,false);
            #endif
            ret_val = send_hci_cmd(fw_info);

            while (ncmd > 0) {
                ret_val = rcv_hci_evt(fw_info);
                printk("rcv_hci_evt %d\n", ret_val);
                if (ret_val < 0) {
                    AICBT_ERR("%s: rcv_hci_evt err %d", __func__, ret_val);
                    goto out;
                } else {
                    AICBT_DBG("%s: Receive acked frag num %d", __func__, evt_para->status);
                    ncmd--;
                }
                if (0 != evt_para->status) {
                    AICBT_ERR("%s: Receive acked frag num %d, err status %d",
                            __func__, ret_val, evt_para->status);
                    ret_val = -1;
                    goto out;
                } else {
                    ret_val = 0;
                }
            }
            ncmd = 1;
        }
    }

    if (!err && (i < size)) {// <1KB data
        data_len = size - i;
        frag_len = data_len + hdr_len;
        memcpy(dl_cmd->data, dst + i, data_len);
        dl_cmd->length = data_len;
        dl_cmd->type = 32;
        dl_cmd->start_addr = fw_addr + i;
        fw_info->cmd_hdr->opcode = cpu_to_le16(DOWNLOAD_OPCODE);
        fw_info->cmd_hdr->plen = frag_len;
        fw_info->pkt_len = frag_len + 3;
        ret_val = send_hci_cmd(fw_info);
        //printk("(%d) data_len %d, src %x, dst %x\n", i, data_len, (dst + i), fw_addr + i);
        //printk("%p , %d\n", dl_cmd, fw_info->pkt_len);
        while (ncmd > 0) {
            ret_val = rcv_hci_evt(fw_info);
            if (ret_val < 0) {
                AICBT_ERR("%s: rcv_hci_evt err %d", __func__, ret_val);
                goto out;
            } else {
                AICBT_DBG("%s: Receive acked frag num %d", __func__, evt_para->status);
                ncmd--;
            }
            if (0 != evt_para->status) {
                AICBT_ERR("%s: Receive acked frag num %d, err status %d",
                        __func__, ret_val, evt_para->status);
                ret_val = -1;
                goto out;
            } else {
                ret_val = 0;
            }
        }
        ncmd = 0;
    }

out:
    if (dst) {
        vfree(dst);
        dst = NULL;
    }

    printk("fw download complete\n\n");
    return ret_val;

}


struct aicbt_info_t {
    uint32_t btmode;
    uint32_t btport;
    uint32_t uart_baud;
    uint32_t uart_flowctrl;
    uint32_t lpm_enable;
    uint32_t txpwr_lvl;
};

struct aicbsp_info_t {
    int hwinfo;
    uint32_t cpmode;
};

enum aicbsp_cpmode_type {
    AICBSP_CPMODE_WORK,
    AICBSP_CPMODE_TEST,
};

/*  btmode
 * used for force bt mode,if not AICBSP_MODE_NULL
 * efuse valid and vendor_info will be invalid, even has beed set valid
*/
enum aicbt_btmode_type {
    AICBT_BTMODE_BT_ONLY_SW = 0x0,    // bt only mode with switch
    AICBT_BTMODE_BT_WIFI_COMBO,       // wifi/bt combo mode
    AICBT_BTMODE_BT_ONLY,             // bt only mode without switch
    AICBT_BTMODE_BT_ONLY_TEST,        // bt only test mode
    AICBT_BTMODE_BT_WIFI_COMBO_TEST,  // wifi/bt combo test mode
    AICBT_MODE_NULL = 0xFF,           // invalid value
};

enum aicbt_btport_type {
    AICBT_BTPORT_NULL,
    AICBT_BTPORT_MB,
    AICBT_BTPORT_UART,
};

enum aicbt_uart_baud_type {
    AICBT_UART_BAUD_115200     = 115200,
    AICBT_UART_BAUD_921600     = 921600,
    AICBT_UART_BAUD_1_5M       = 1500000,
    AICBT_UART_BAUD_3_25M      = 3250000,
};

enum aicbt_uart_flowctrl_type {
    AICBT_UART_FLOWCTRL_DISABLE = 0x0,    // uart without flow ctrl
    AICBT_UART_FLOWCTRL_ENABLE,           // uart with flow ctrl
};

#define AICBSP_HWINFO_DEFAULT       (-1)
#define AICBSP_CPMODE_DEFAULT       AICBSP_CPMODE_WORK
#define AICBT_TXPWR_DFT                0x6F2F


#define AICBT_BTMODE_DEFAULT        AICBT_BTMODE_BT_WIFI_COMBO
#define AICBT_BTPORT_DEFAULT        AICBT_BTPORT_MB
#define AICBT_UART_BAUD_DEFAULT     AICBT_UART_BAUD_1_5M
#define AICBT_UART_FC_DEFAULT       AICBT_UART_FLOWCTRL_ENABLE
#define AICBT_LPM_ENABLE_DEFAULT    0
#define AICBT_TXPWR_LVL_DEFAULT     AICBT_TXPWR_DFT

struct aicbsp_info_t aicbsp_info = {
    .hwinfo   = AICBSP_HWINFO_DEFAULT,
    .cpmode   = AICBSP_CPMODE_DEFAULT,
};

#ifndef CONFIG_USE_FW_REQUEST
#define FW_PATH_MAX 200

char aic_fw_path[FW_PATH_MAX];
#if (CONFIG_BLUEDROID == 0)
static const char* aic_default_fw_path = "/lib/firmware/aic8800DC";
#else
static const char* aic_default_fw_path = "/vendor/etc/firmware";
#endif
#endif //CONFIG_USE_FW_REQUEST

static struct aicbt_info_t aicbt_info = {
    .btmode        = AICBT_BTMODE_DEFAULT,
    .btport        = AICBT_BTPORT_DEFAULT,
    .uart_baud     = AICBT_UART_BAUD_DEFAULT,
    .uart_flowctrl = AICBT_UART_FC_DEFAULT,
    .lpm_enable    = AICBT_LPM_ENABLE_DEFAULT,
    .txpwr_lvl     = AICBT_TXPWR_LVL_DEFAULT,
};

int patch_table_load(firmware_info *fw_info, struct aicbt_patch_table *_head)
{
    struct aicbt_patch_table *head, *p;
    int i;
    uint32_t *data = NULL;
    struct aicbt_patch_table_cmd *patch_table_cmd = (struct aicbt_patch_table_cmd *)(fw_info->req_para);
    struct fw_status *evt_para;
    int ret_val = 0;
    int ncmd = 1;
    uint32_t len = 0;
    uint32_t tot_len = 0;
    head = _head;
    for (p = head; p != NULL; p = p->next) {
        data = p->data;
        if(AICBT_PT_BTMODE == p->type){
            *(data + 1)  = aicbsp_info.hwinfo < 0;
            *(data + 3) = aicbsp_info.hwinfo;
            *(data + 5)  = aicbsp_info.cpmode;

            *(data + 7) = aicbt_info.btmode;
            *(data + 9) = aicbt_info.btport;
            *(data + 11) = aicbt_info.uart_baud;
            *(data + 13) = aicbt_info.uart_flowctrl;
            *(data + 15) = aicbt_info.lpm_enable;
            *(data + 17) = aicbt_info.txpwr_lvl;

        }
        if (p->type == AICBT_PT_NULL || p->type == AICBT_PT_PWRON) {
            continue;
        }
        if (p->type == AICBT_PT_VER) {
            char *data_s = (char *)p->data;
            printk("patch version %s\n", data_s);
            continue;
        }
        if (p->len == 0) {
            printk("len is 0\n");
            continue;
        }
        tot_len = p->len;
        while (tot_len) {
            if (tot_len > HCI_PT_MAX_LEN) {
                len = HCI_PT_MAX_LEN;
            } else {
                len = tot_len;
            }
            for (i = 0; i < len; i++) {
                patch_table_cmd->patch_num = len;
                memcpy(&patch_table_cmd->patch_table_addr[i], data, sizeof(uint32_t));
                memcpy(&patch_table_cmd->patch_table_data[i], data + 1, sizeof(uint32_t));
                printk("[%d] data: %08x %08x\n", i, patch_table_cmd->patch_table_addr[i],patch_table_cmd->patch_table_data[i]);
                data += 2;
            }
            tot_len -= len;
            evt_para = (struct fw_status *)fw_info->rsp_para;
            //print_hex_dump(KERN_ERR,"data0:",DUMP_PREFIX_NONE,16,1,patch_table_cmd,sizeof(struct aicbt_patch_table_cmd),false);

            //printk("patch num %x %d\n", patch_table_cmd->patch_num, sizeof(struct aicbt_patch_table_cmd));
            fw_info->cmd_hdr->opcode = cpu_to_le16(HCI_VSC_UPDATE_PT_CMD);
            fw_info->cmd_hdr->plen = HCI_VSC_UPDATE_PT_SIZE;
            fw_info->pkt_len = fw_info->cmd_hdr->plen + 3;
            AICBT_DBG("patch num 0x%x, plen 0x%x\n", patch_table_cmd->patch_num, fw_info->cmd_hdr->plen );
            //print_hex_dump(KERN_ERR,"patch table:",DUMP_PREFIX_NONE,16,1,fw_info->send_pkt,32,false);
            ret_val = send_hci_cmd(fw_info);
            while (ncmd > 0) {
                ret_val = rcv_hci_evt(fw_info);
                if (ret_val < 0) {
                    AICBT_ERR("%s: rcv_hci_evt err %d", __func__, ret_val);
                    goto out;
                } else {
                    AICBT_DBG("%s: Receive acked frag num %d", __func__, evt_para->status);
                    ncmd--;
                }
                if (0 != evt_para->status) {
                    AICBT_ERR("%s: Receive acked frag num %d, err status %d",
                            __func__, ret_val, evt_para->status);
                    ret_val = -1;
                    goto out;
                }
            }
            ncmd = 1;
        }
    }
out:
    aicbt_patch_table_free(&head);
    return ret_val;
}

int aic_load_firmware(u8 ** fw_buf, const char *name, struct device *device)
{

#ifdef CONFIG_USE_FW_REQUEST
	const struct firmware *fw = NULL;
	u32 *dst = NULL;
	void *buffer=NULL;
	int size = 0;
	int ret = 0;

	printk("%s: request firmware = %s \n", __func__ ,name);


	ret = request_firmware(&fw, name, NULL);

	if (ret < 0) {
		printk("Load %s fail\n", name);
		release_firmware(fw);
		return -1;
	}

	size = fw->size;
	dst = (u32 *)fw->data;

	if (size <= 0) {
		printk("wrong size of firmware file\n");
		release_firmware(fw);
		return -1;
	}


	buffer = vmalloc(size);
	memset(buffer, 0, size);
	memcpy(buffer, dst, size);

	*fw_buf = buffer;

	release_firmware(fw);

	return size;

#else
    u8 *buffer=NULL;
    char *path=NULL;
    struct file *fp=NULL;
    int size = 0, len=0;
    ssize_t rdlen=0;

    /* get the firmware path */
    path = __getname();
    if (!path){
            *fw_buf=NULL;
            return -1;
    }

    if (strlen(aic_fw_path) > 0) {
        printk("%s: use customer define fw_path\n", __func__);
        len = snprintf(path, FW_PATH_MAX, "%s/%s", aic_fw_path, name);
    } else {
        len = snprintf(path, FW_PATH_MAX, "%s/%s",aic_default_fw_path, name);
    }

    if (len >= FW_PATH_MAX) {
        printk("%s: %s file's path too long\n", __func__, name);
        *fw_buf=NULL;
        __putname(path);
        return -1;
    }

    printk("%s :firmware path = %s  \n", __func__ ,path);


    /* open the firmware file */
    fp=filp_open(path, O_RDONLY, 0);
    if(IS_ERR(fp) || (!fp)){
            printk("%s: %s file failed to open\n", __func__, name);
            if(IS_ERR(fp))
        printk("is_Err\n");
    if((!fp))
        printk("null\n");
    *fw_buf=NULL;
            __putname(path);
            fp=NULL;
            return -1;
    }

    size = i_size_read(file_inode(fp));
    if(size<=0){
            printk("%s: %s file size invalid %d\n", __func__, name, size);
            *fw_buf=NULL;
            __putname(path);
            filp_close(fp,NULL);
            fp=NULL;
            return -1;
}

    /* start to read from firmware file */
    buffer = vmalloc(size);
    memset(buffer, 0, size);
    if(!buffer){
            *fw_buf=NULL;
            __putname(path);
            filp_close(fp,NULL);
            fp=NULL;
            return -1;
    }


    #if LINUX_VERSION_CODE > KERNEL_VERSION(4, 13, 16)
    rdlen = kernel_read(fp, buffer, size, &fp->f_pos);
    #else
    rdlen = kernel_read(fp, fp->f_pos, buffer, size);
    #endif

    if(size != rdlen){
            printk("%s: %s file rdlen invalid %d %d\n", __func__, name, (int)rdlen, size);
            *fw_buf=NULL;
            __putname(path);
            filp_close(fp,NULL);
            fp=NULL;
            vfree(buffer);
            buffer=NULL;
            return -1;
    }
    if(rdlen > 0){
            fp->f_pos += rdlen;
            //printk("f_pos=%d\n", (int)fp->f_pos);
    }
    *fw_buf = buffer;

#if 0
    MD5Init(&md5);
    MD5Update(&md5, (unsigned char *)dst, size);
    MD5Final(&md5, decrypt);

    printk(MD5PINRT, MD5(decrypt));

#endif
    return size;
#endif
}

int aicbt_patch_table_free(struct aicbt_patch_table **head)
{
    struct aicbt_patch_table *p = *head, *n = NULL;
    while (p) {
        n = p->next;
        kfree(p->name);
        kfree(p->data);
        kfree(p);
        p = n;
    }
    *head = NULL;
    return 0;
}

int get_patch_addr_from_patch_table(firmware_info *fw_info, char *filename, uint32_t *fw_patch_base_addr)
{
    int size;
    int ret = 0;
    uint8_t *rawdata=NULL;
    uint8_t *p = NULL;
    uint32_t *data = NULL;
    uint32_t type = 0, len = 0;
    int j;

    /* load aic firmware */
    size = aic_load_firmware((u8 **)&rawdata, filename, NULL);

    /* Copy the file on the Embedded side */
    printk("### Upload %s fw_patch_table, size=%d\n", filename, size);

    if (size <= 0) {
        printk("wrong size of firmware file\n");
        ret = -1;
        goto err;
    }

    p = rawdata;

    if (memcmp(p, AICBT_PT_TAG, sizeof(AICBT_PT_TAG) < 16 ? sizeof(AICBT_PT_TAG) : 16)) {
        printk("TAG err\n");
        ret = -1;
        goto err;
    }
    p += 16;

    while (p - rawdata < size) {
        printk("size = %d  p - rawdata = 0x%0lx \r\n", size, p - rawdata);
        p += 16;

        type = *(uint32_t *)p;
        p += 4;

        len = *(uint32_t *)p;
        p += 4;
        printk("cur->type %x, len %d\n", type, len);

        if(type >= 1000 ) {//Temp Workaround
            len = 0;
        }else{
            data = (uint32_t *)p;
            if (type == AICBT_PT_NULL) {
                *(fw_patch_base_addr) = *(data + 3);
                printk("addr found %x\n", *(fw_patch_base_addr));
                for (j = 0; j < len; j++) {
                    printk("addr %x\n", *(data+j));
                }
                break;
            }
            p += len * 8;
        }
    }

    vfree(rawdata);
    return ret;
err:
    //aicbt_patch_table_free(&head);

    if (rawdata){
        vfree(rawdata);
    }
    return ret;
}



int patch_table_download(firmware_info *fw_info, char *filename)
{
    struct aicbt_patch_table *head = NULL;
    struct aicbt_patch_table *new = NULL;
    struct aicbt_patch_table *cur = NULL;
        int size;
    int ret = 0;
       uint8_t *rawdata=NULL;
    uint8_t *p = NULL;

    /* load aic firmware */
    size = aic_load_firmware((u8 **)&rawdata, filename, NULL);

    /* Copy the file on the Embedded side */
    printk("### Upload %s fw_patch_table, size=%d\n", filename, size);

    if (size <= 0) {
        printk("wrong size of firmware file\n");
        ret = -1;
        goto err;
    }

    p = rawdata;

    if (memcmp(p, AICBT_PT_TAG, sizeof(AICBT_PT_TAG) < 16 ? sizeof(AICBT_PT_TAG) : 16)) {
        printk("TAG err\n");
        ret = -1;
        goto err;
    }
    p += 16;

    while (p - rawdata < size) {
        printk("size = %d  p - rawdata = 0x%0lx \r\n", size, p - rawdata);
        new = (struct aicbt_patch_table *)kmalloc(sizeof(struct aicbt_patch_table), GFP_KERNEL);
        memset(new, 0, sizeof(struct aicbt_patch_table));
        if (head == NULL) {
            head = new;
            cur  = new;
        } else {
            cur->next = new;
            cur = cur->next;
        }

        cur->name = (char *)kmalloc(sizeof(char) * 16, GFP_KERNEL);
        memset(cur->name, 0, sizeof(char) * 16);
        memcpy(cur->name, p, 16);
        p += 16;

        cur->type = *(uint32_t *)p;
        p += 4;

        cur->len = *(uint32_t *)p;
        p += 4;
        printk("cur->type %x, len %d\n", cur->type, cur->len);

        if((cur->type )  >= 1000 ) {//Temp Workaround
            cur->len = 0;
        }else{
            cur->data = (uint32_t *)kmalloc(sizeof(uint8_t) * cur->len * 8, GFP_KERNEL);
            memset(cur->data, 0, sizeof(uint8_t) * cur->len * 8);
            memcpy(cur->data, p, cur->len * 8);
            p += cur->len * 8;
        }
    }

    vfree(rawdata);
    patch_table_load(fw_info, head);
    printk("fw_patch_table download complete\n\n");

    return ret;
err:
    //aicbt_patch_table_free(&head);

    if (rawdata){
        vfree(rawdata);
    }
    return ret;
}


int download_patch(firmware_info *fw_info, int cached)
{
    int ret_val = 0;

    printk("%s: Download fw patch start, cached %d", __func__, cached);

    if (!fw_info) {
        printk("%s: No patch entry exists(fw_info %p)", __func__, fw_info);
        ret_val = -1;
        goto end;
    }

    ret_val = fw_config(fw_info);
    if (ret_val) {
        printk("%s: fw config failed %d", __func__, ret_val);
        goto free;
    }

    ret_val = system_config(fw_info);
    if (ret_val)
    {
        printk("%s: system config failed %d", __func__, ret_val);
        goto free;
    }

    /*
     * step1: check firmware statis
     * step2: download firmware if updated
     */


    ret_val = check_fw_status(fw_info);


    if (ret_val) {
        #if 0
        ret_val = download_data(fw_info, FW_RAM_ADID_BASE_ADDR, FW_ADID_BASE_NAME);
        if (ret_val) {
            printk("aic load adid fail %d\n", ret_val);
            goto free;
        }
        #endif
        if (sub_chip_id == 0) {
            ret_val= download_data(fw_info, FW_RAM_PATCH_BASE_ADDR, FW_PATCH_BASE_NAME);
            if (ret_val) {
                printk("aic load patch fail %d\n", ret_val);
                goto free;
            }

            ret_val= patch_table_download(fw_info, FW_PATCH_TABLE_NAME);
            if (ret_val) {
                printk("aic load patch ftable ail %d\n", ret_val);
                goto free;
            }
        } else if (sub_chip_id == 1) {
            uint32_t fw_ram_patch_base_addr = FW_RAM_PATCH_BASE_ADDR;

            ret_val = get_patch_addr_from_patch_table(fw_info, FW_PATCH_TABLE_NAME_U02, &fw_ram_patch_base_addr);
            if (ret_val)
            {
                printk("aic get patch addr fail %d\n", ret_val);
                goto free;
            }
            printk("%s %x\n", __func__, fw_ram_patch_base_addr);
            ret_val = download_data(fw_info, fw_ram_patch_base_addr, FW_PATCH_BASE_NAME_U02);
            if (ret_val)
            {
                printk("aic load patch fail %d\n", ret_val);
                goto free;
            }

            ret_val = patch_table_download(fw_info, FW_PATCH_TABLE_NAME_U02);
            if (ret_val)
            {
                printk("aic load patch ftable ail %d\n", ret_val);
                goto free;
            }
        } else if (sub_chip_id == 2) {
            uint32_t fw_ram_patch_base_addr = FW_RAM_PATCH_BASE_ADDR;

            ret_val = get_patch_addr_from_patch_table(fw_info, FW_PATCH_TABLE_NAME_U02H, &fw_ram_patch_base_addr);
            if (ret_val)
            {
                printk("aic get patch addr fail %d\n", ret_val);
                goto free;
            }
            printk("U02H %s %x\n", __func__, fw_ram_patch_base_addr);
            ret_val = download_data(fw_info, fw_ram_patch_base_addr, FW_PATCH_BASE_NAME_U02H);
            if (ret_val)
            {
                printk("aic load patch fail %d\n", ret_val);
                goto free;
            }

            ret_val = patch_table_download(fw_info, FW_PATCH_TABLE_NAME_U02H);
            if (ret_val)
            {
                printk("aic load patch ftable ail %d\n", ret_val);
                goto free;
            }
	} else {
            printk("%s unsupported sub_chip_id %x\n", __func__, sub_chip_id);
        }
    }

free:
    /* Free fw data after download finished */
    kfree(fw_info->fw_data);
    fw_info->fw_data = NULL;

end:
    return ret_val;
}

//for 8800dc end

firmware_info *firmware_info_init(struct usb_interface *intf)
{
    struct usb_device *udev = interface_to_usbdev(intf);
    firmware_info *fw_info;

    AICBT_DBG("%s: start", __func__);

    fw_info = kzalloc(sizeof(*fw_info), GFP_KERNEL);
    if (!fw_info)
        return NULL;

    fw_info->send_pkt = kzalloc(SEND_PKT_LEN, GFP_KERNEL);
    if (!fw_info->send_pkt) {
        kfree(fw_info);
        return NULL;
    }

    fw_info->rcv_pkt = kzalloc(RCV_PKT_LEN, GFP_KERNEL);
    if (!fw_info->rcv_pkt) {
        kfree(fw_info->send_pkt);
        kfree(fw_info);
        return NULL;
    }

    fw_info->intf = intf;
    fw_info->udev = udev;
if(g_chipid == PRODUCT_ID_AIC8801 || g_chipid == PRODUCT_ID_AIC8800D80){
    fw_info->pipe_in = usb_rcvbulkpipe(fw_info->udev, BULK_EP);
	fw_info->pipe_out = usb_rcvbulkpipe(fw_info->udev, CTRL_EP);
}else if(g_chipid == PRODUCT_ID_AIC8800DC){
    fw_info->pipe_in = usb_rcvintpipe(fw_info->udev, INTR_EP);
    fw_info->pipe_out = usb_sndctrlpipe(fw_info->udev, CTRL_EP);
}
    fw_info->cmd_hdr = (struct hci_command_hdr *)(fw_info->send_pkt);
    fw_info->evt_hdr = (struct hci_event_hdr *)(fw_info->rcv_pkt);
    fw_info->cmd_cmp = (struct hci_ev_cmd_complete *)(fw_info->rcv_pkt + EVT_HDR_LEN);
    fw_info->req_para = fw_info->send_pkt + CMD_HDR_LEN;
    fw_info->rsp_para = fw_info->rcv_pkt + EVT_HDR_LEN + CMD_CMP_LEN;

#if BTUSB_RPM
    AICBT_INFO("%s: Auto suspend is enabled", __func__);
    usb_enable_autosuspend(udev);
    pm_runtime_set_autosuspend_delay(&(udev->dev), 2000);
#else
    AICBT_INFO("%s: Auto suspend is disabled", __func__);
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


}

static struct usb_driver btusb_driver;

static struct usb_device_id btusb_table[] = {
    #if 0
    { .match_flags = USB_DEVICE_ID_MATCH_VENDOR |
                     USB_DEVICE_ID_MATCH_INT_INFO,
      .idVendor = 0xa69d,
      .bInterfaceClass = 0xe0,
      .bInterfaceSubClass = 0x01,
      .bInterfaceProtocol = 0x01 },
    #endif
    {USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_AIC, USB_PRODUCT_ID_AIC8801, 0xe0, 0x01,0x01)},
    {USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_AIC, USB_PRODUCT_ID_AIC8800D80, 0xe0, 0x01,0x01)},
    {USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_AIC, USB_PRODUCT_ID_AIC8800DC, 0xe0, 0x01,0x01)},
    {}
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
    u8 air_mode = 0;
    struct hci_dev *hdev = urb->context;
#ifdef CONFIG_SCO_OVER_HCI
    struct btusb_data *data = GET_DRV_DATA(hdev);
    AIC_sco_card_t  *pSCOSnd = data->pSCOSnd;
#endif

    switch (*opcode) {
    case HCI_EV_SYNC_CONN_COMPLETE:
        AICBT_INFO("%s: HCI_EV_SYNC_CONN_COMPLETE(0x%02x)", __func__, *opcode);
        status = *(opcode + 2);
        sco_handle = *(opcode + 3) | *(opcode + 4) << 8;
        air_mode = *(opcode + 18);
		printk("%s status:%d,air_mode:%d \r\n", __func__, status,air_mode);
        if (status == 0) {
            hdev->conn_hash.sco_num++;
			hdev->notify(hdev, 0);
            //schedule_work(&data->work);
            if (air_mode == 0x03) {
                set_select_msbc(CODEC_MSBC);
            }
        }
        break;
    case HCI_EV_DISCONN_COMPLETE:
        AICBT_INFO("%s: HCI_EV_DISCONN_COMPLETE(0x%02x)", __func__, *opcode);
        status = *(opcode + 2);
        handle = *(opcode + 3) | *(opcode + 4) << 8;
        if (status == 0 && sco_handle == handle) {
            hdev->conn_hash.sco_num--;
			hdev->notify(hdev, 0);
            set_select_msbc(CODEC_CVSD);
            //schedule_work(&data->work);
#ifdef CONFIG_SCO_OVER_HCI
			if (test_bit(ALSA_CAPTURE_RUNNING, &pSCOSnd->states)) {
				mod_timer(&snd_cap_timer.cap_timer,jiffies + msecs_to_jiffies(3));
			}
#endif
        }
        break;
    default:
        AICBT_DBG("%s: event 0x%02x", __func__, *opcode);
        break;
    }
}

#if (CONFIG_BLUEDROID == 0)
#if HCI_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
static inline void btusb_free_frags(struct btusb_data *data)
{
    unsigned long flags;

    spin_lock_irqsave(&data->rxlock, flags);

    kfree_skb(data->evt_skb);
    data->evt_skb = NULL;

    kfree_skb(data->acl_skb);
    data->acl_skb = NULL;

    kfree_skb(data->sco_skb);
    data->sco_skb = NULL;

    spin_unlock_irqrestore(&data->rxlock, flags);
}

static int btusb_recv_intr(struct btusb_data *data, void *buffer, int count)
{
    struct sk_buff *skb;
    int err = 0;

    spin_lock(&data->rxlock);
    skb = data->evt_skb;
    //printk("%s count %d\n", __func__, count);

#if 1
    while (count) {
        int len;

        if (!skb) {
            skb = bt_skb_alloc(HCI_MAX_EVENT_SIZE, GFP_ATOMIC);
            if (!skb) {
                err = -ENOMEM;
                break;
            }

            bt_cb(skb)->pkt_type = HCI_EVENT_PKT;
            bt_cb(skb)->expect = HCI_EVENT_HDR_SIZE;
        }

        len = min_t(uint, bt_cb(skb)->expect, count);
        memcpy(skb_put(skb, len), buffer, len);

        count -= len;
        buffer += len;
        bt_cb(skb)->expect -= len;

        if (skb->len == HCI_EVENT_HDR_SIZE) {
            /* Complete event header */
            bt_cb(skb)->expect = hci_event_hdr(skb)->plen;

            if (skb_tailroom(skb) < bt_cb(skb)->expect) {
                kfree_skb(skb);
                skb = NULL;

                err = -EILSEQ;
                break;
            }
        }

        if (bt_cb(skb)->expect == 0) {
            /* Complete frame */
            hci_recv_frame(data->hdev, skb);
            skb = NULL;
        }
    }
#endif

    data->evt_skb = skb;
    spin_unlock(&data->rxlock);

    return err;
}

static int btusb_recv_bulk(struct btusb_data *data, void *buffer, int count)
{
    struct sk_buff *skb;
    int err = 0;

    spin_lock(&data->rxlock);
    skb = data->acl_skb;

    while (count) {
        int len;

        if (!skb) {
            skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC);
            if (!skb) {
                err = -ENOMEM;
                break;
            }

            bt_cb(skb)->pkt_type = HCI_ACLDATA_PKT;
            bt_cb(skb)->expect = HCI_ACL_HDR_SIZE;
        }

        len = min_t(uint, bt_cb(skb)->expect, count);
        memcpy(skb_put(skb, len), buffer, len);

        count -= len;
        buffer += len;
        bt_cb(skb)->expect -= len;

        if (skb->len == HCI_ACL_HDR_SIZE) {
            __le16 dlen = hci_acl_hdr(skb)->dlen;

            /* Complete ACL header */
            bt_cb(skb)->expect = __le16_to_cpu(dlen);

            if (skb_tailroom(skb) < bt_cb(skb)->expect) {
                kfree_skb(skb);
                skb = NULL;

                err = -EILSEQ;
                break;
            }
        }

        if (bt_cb(skb)->expect == 0) {
            /* Complete frame */
            hci_recv_frame(data->hdev, skb);
            skb = NULL;
        }
    }

    data->acl_skb = skb;
    spin_unlock(&data->rxlock);

    return err;
}

static int btusb_recv_isoc(struct btusb_data *data, void *buffer, int count)
{
    struct sk_buff *skb;
    int err = 0;

    spin_lock(&data->rxlock);
    skb = data->sco_skb;

    while (count) {
        int len;

        if (!skb) {
            skb = bt_skb_alloc(HCI_MAX_SCO_SIZE, GFP_ATOMIC);
            if (!skb) {
                err = -ENOMEM;
                break;
            }

            bt_cb(skb)->pkt_type = HCI_SCODATA_PKT;
            bt_cb(skb)->expect = HCI_SCO_HDR_SIZE;
        }

        len = min_t(uint, bt_cb(skb)->expect, count);
        memcpy(skb_put(skb, len), buffer, len);

        count -= len;
        buffer += len;
        bt_cb(skb)->expect -= len;

        if (skb->len == HCI_SCO_HDR_SIZE) {
            /* Complete SCO header */
            bt_cb(skb)->expect = hci_sco_hdr(skb)->dlen;

            if (skb_tailroom(skb) < bt_cb(skb)->expect) {
                kfree_skb(skb);
                skb = NULL;

                err = -EILSEQ;
                break;
            }
        }

        if (bt_cb(skb)->expect == 0) {
            /* Complete frame */
            hci_recv_frame(data->hdev, skb);
            skb = NULL;
        }
    }

    data->sco_skb = skb;
    spin_unlock(&data->rxlock);

    return err;
}
#endif
#endif // (CONFIG_BLUEDROID == 0)


static void btusb_intr_complete(struct urb *urb)
{
    struct hci_dev *hdev = urb->context;
    struct btusb_data *data = GET_DRV_DATA(hdev);
    int err;

    AICBT_DBG("%s: urb %p status %d count %d ", __func__,
            urb, urb->status, urb->actual_length);

    if (!test_bit(HCI_RUNNING, &hdev->flags)) {
        printk("%s return \n", __func__);
        return;
    }
    if (urb->status == 0) {
        hdev->stat.byte_rx += urb->actual_length;

#if (CONFIG_BLUEDROID) || (HCI_VERSION_CODE < KERNEL_VERSION(3, 18, 0))
		if (hci_recv_fragment(hdev, HCI_EVENT_PKT,
						urb->transfer_buffer,
						urb->actual_length) < 0) {
			AICBT_ERR("%s: Corrupted event packet", __func__);
			hdev->stat.err_rx++;
		}
#else
		if (btusb_recv_intr(data, urb->transfer_buffer,
					urb->actual_length) < 0) {
			AICBT_ERR("%s corrupted event packet", hdev->name);
			hdev->stat.err_rx++;
		}
#endif

#ifdef CONFIG_SCO_OVER_HCI
		check_sco_event(urb);
#endif
#ifdef CONFIG_USB_AIC_UART_SCO_DRIVER
		check_sco_event(urb);
#endif

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
        if (err != -EPERM && err != -ENODEV)
            AICBT_ERR("%s: Failed to re-submit urb %p, err %d",
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

    AICBT_DBG("%s: mMaxPacketSize %d, bEndpointAddress 0x%02x",
            __func__, size, data->intr_ep->bEndpointAddress);

    pipe = usb_rcvintpipe(data->udev, data->intr_ep->bEndpointAddress);

    usb_fill_int_urb(urb, data->udev, pipe, buf, size,
                        btusb_intr_complete, hdev,
                        data->intr_ep->bInterval);

    urb->transfer_flags |= URB_FREE_BUFFER;

    usb_anchor_urb(urb, &data->intr_anchor);

    err = usb_submit_urb(urb, mem_flags);
    if (err < 0) {
        AICBT_ERR("%s: Failed to submit urb %p, err %d",
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

    AICBT_DBG("%s: urb %p status %d count %d",
            __func__, urb, urb->status, urb->actual_length);

    if (!test_bit(HCI_RUNNING, &hdev->flags)) {
        printk("%s HCI_RUNNING\n", __func__);
        return;
    }
    if (urb->status == 0) {
        hdev->stat.byte_rx += urb->actual_length;

#if (CONFIG_BLUEDROID) || (HCI_VERSION_CODE < KERNEL_VERSION(3, 18, 0))
		if (hci_recv_fragment(hdev, HCI_ACLDATA_PKT,
			  urb->transfer_buffer,
			  urb->actual_length) < 0) {
				AICBT_ERR("%s: Corrupted ACL packet", __func__);
				hdev->stat.err_rx++;
			}
#else
		if (data->recv_bulk(data, urb->transfer_buffer,
				urb->actual_length) < 0) {
				AICBT_ERR("%s Corrupted ACL packet", hdev->name);
				hdev->stat.err_rx++;
			}
#endif

    }
    /* Avoid suspend failed when usb_kill_urb */
    else if(urb->status == -ENOENT)    {
        printk("%s ENOENT\n", __func__);
        return;
    }
    AICBT_DBG("%s: OUT", __func__);

    if (!test_bit(BTUSB_BULK_RUNNING, &data->flags)) {
        printk("%s BTUSB_BULK_RUNNING\n", __func__);
        return;
    }
    usb_anchor_urb(urb, &data->bulk_anchor);
    usb_mark_last_busy(data->udev);

    //printk("LIULI bulk submit\n");
    err = usb_submit_urb(urb, GFP_ATOMIC);
    if (err < 0) {
        /* -EPERM: urb is being killed;
         * -ENODEV: device got disconnected */
        if (err != -EPERM && err != -ENODEV)
            AICBT_ERR("btusb_bulk_complete %s urb %p failed to resubmit (%d)",
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

    AICBT_DBG("%s: hdev name %s", __func__, hdev->name);
    AICBT_DBG("%s: mMaxPacketSize %d, bEndpointAddress 0x%02x",
            __func__, size, data->bulk_rx_ep->bEndpointAddress);

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
        AICBT_ERR("%s: Failed to submit urb %p, err %d", __func__, urb, err);
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
	unsigned int total_length = 0;

    AICBT_DBG("%s: urb %p status %d count %d",
            __func__, urb, urb->status, urb->actual_length);

    if (!test_bit(HCI_RUNNING, &hdev->flags))
        return;

    if (urb->status == 0) {
        for (i = 0; i < urb->number_of_packets; i++) {
            unsigned int offset = urb->iso_frame_desc[i].offset;
            unsigned int length = urb->iso_frame_desc[i].actual_length;
            //u8 *data = (u8 *)(urb->transfer_buffer + offset);
            //AICBT_DBG("%d,%d ,%x,%x,%x  s %d.",
            //offset, length, data[0], data[1],data[2],urb->iso_frame_desc[i].status);

            if(total_length >= urb->actual_length){
                AICBT_ERR("total_len >= actual_length ,return");
                break;
            }
            total_length += length;

            if (urb->iso_frame_desc[i].status)
                continue;

            hdev->stat.byte_rx += length;
            if(length){
#if (CONFIG_BLUEDROID) || (HCI_VERSION_CODE < KERNEL_VERSION(3, 18, 0))
				if (hci_recv_fragment(hdev, HCI_SCODATA_PKT,
					  urb->transfer_buffer + offset,
					  length) < 0) {
						AICBT_ERR("%s: Corrupted SCO packet", __func__);
							hdev->stat.err_rx++;
					}
#else
				if (btusb_recv_isoc(data, urb->transfer_buffer + offset,
					length) < 0) {
						AICBT_ERR("%s corrupted SCO packet",
							  hdev->name);
						hdev->stat.err_rx++;
				}
#endif

            }
        }
    }
    /* Avoid suspend failed when usb_kill_urb */
    else if(urb->status == -ENOENT) {
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
            AICBT_ERR("%s: Failed to re-sumbit urb %p, retry %d, err %d",
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

    AICBT_DBG("%s: len %d mtu %d", __func__, len, mtu);

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
	int interval;

    if (!data->isoc_rx_ep)
        return -ENODEV;
    AICBT_DBG("%s: mMaxPacketSize %d, bEndpointAddress 0x%02x",
            __func__, size, data->isoc_rx_ep->bEndpointAddress);

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
	if (urb->dev->speed == USB_SPEED_HIGH || urb->dev->speed >= USB_SPEED_SUPER) {  
		/* make sure interval is within allowed range */  
		interval = clamp((int)data->isoc_rx_ep->bInterval, 1, 16);  
		urb->interval = 1 << (interval - 1); 
	} else {  
		urb->interval = data->isoc_rx_ep->bInterval; 
	}

	AICBT_INFO("urb->interval %d \r\n", urb->interval);

    urb->transfer_flags  = URB_FREE_BUFFER | URB_ISO_ASAP;
    urb->transfer_buffer = buf;
    urb->transfer_buffer_length = size;

    fill_isoc_descriptor(urb, size,
            le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize));

    usb_anchor_urb(urb, &data->isoc_anchor);

    err = usb_submit_urb(urb, mem_flags);
    if (err < 0) {
        AICBT_ERR("%s: Failed to submit urb %p, err %d", __func__, urb, err);
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

    AICBT_DBG("%s: urb %p status %d count %d",
            __func__, urb, urb->status, urb->actual_length);

    if (skb && hdev) {
        if (!test_bit(HCI_RUNNING, &hdev->flags))
            goto done;

        if (!urb->status)
            hdev->stat.byte_tx += urb->transfer_buffer_length;
        else
            hdev->stat.err_tx++;
    } else
        AICBT_ERR("%s: skb 0x%p hdev 0x%p", __func__, skb, hdev);

done:
    kfree(urb->setup_packet);

    kfree_skb(skb);
}

#if (CONFIG_BLUEDROID == 0)
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 0, 9)
static int btusb_shutdown(struct hci_dev *hdev)
{
	struct sk_buff *skb;
    printk("aic %s\n", __func__);

	skb = __hci_cmd_sync(hdev, HCI_OP_RESET, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		printk("HCI reset during shutdown failed\n");
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

    return 0;
}
#endif
#endif //(CONFIG_BLUEDROID == 0)

static int btusb_open(struct hci_dev *hdev)
{
    struct btusb_data *data = GET_DRV_DATA(hdev);
    int err = 0;

    AICBT_INFO("%s: Start", __func__);

    err = usb_autopm_get_interface(data->intf);
    if (err < 0)
        return err;

    data->intf->needs_remote_wakeup = 1;

#if (CONFIG_BLUEDROID == 0)
		//err = download_patch(data->fw_info,1);
		printk(" download_patch %d", err);
		if (err < 0) {
			goto failed;
		}
#endif


    if (test_and_set_bit(HCI_RUNNING, &hdev->flags)){
        goto done;
    }

    if (test_and_set_bit(BTUSB_INTR_RUNNING, &data->flags)){
        goto done;
    }

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
    AICBT_INFO("%s: End", __func__);
    return 0;

failed:
    clear_bit(BTUSB_INTR_RUNNING, &data->flags);
    clear_bit(HCI_RUNNING, &hdev->flags);
    usb_autopm_put_interface(data->intf);
    AICBT_ERR("%s: Failed", __func__);
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
#if (CONFIG_BLUEDROID) || (HCI_VERSION_CODE < KERNEL_VERSION(4, 1, 0))
    int i;
#endif
	int err;

    AICBT_INFO("%s: hci running %lu", __func__, hdev->flags & HCI_RUNNING);

    if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags)){
        return 0;
    }
	
	if (!test_and_clear_bit(BTUSB_INTR_RUNNING, &data->flags)){
        return 0;
	}

#if (CONFIG_BLUEDROID) || (HCI_VERSION_CODE < KERNEL_VERSION(4, 1, 0))
	for (i = 0; i < NUM_REASSEMBLY; i++) {
		if (hdev->reassembly[i]) {
			AICBT_DBG("%s: free ressembly[%d]", __func__, i);
			kfree_skb(hdev->reassembly[i]);
			hdev->reassembly[i] = NULL;
		}
	}
#endif

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

    AICBT_DBG("%s", __func__);

    mdelay(URB_CANCELING_DELAY_MS);
    usb_kill_anchored_urbs(&data->tx_anchor);

    return 0;
}

#ifdef CONFIG_SCO_OVER_HCI
static void btusb_isoc_snd_tx_complete(struct urb *urb);

static int snd_send_sco_frame(struct sk_buff *skb)
{
    struct hci_dev *hdev = (struct hci_dev *) skb->dev;

    struct btusb_data *data = GET_DRV_DATA(hdev);
    //struct usb_ctrlrequest *dr;
    struct urb *urb;
    unsigned int pipe;
    int err;

    AICBT_DBG("%s:pkt type %d, packet_len : %d",
            __func__,bt_cb(skb)->pkt_type, skb->len);

    if (!hdev && !test_bit(HCI_RUNNING, &hdev->flags))
        return -EBUSY;

    if (!data->isoc_tx_ep || hdev->conn_hash.sco_num < 1) {
        kfree(skb);
        return -ENODEV;
    }

    urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, GFP_ATOMIC);
    if (!urb) {
        AICBT_ERR("%s: Failed to allocate mem for sco pkts", __func__);
        kfree(skb);
        return -ENOMEM;
    }

    pipe = usb_sndisocpipe(data->udev, data->isoc_tx_ep->bEndpointAddress);

    usb_fill_int_urb(urb, data->udev, pipe,
            skb->data, skb->len, btusb_isoc_snd_tx_complete,
            skb, data->isoc_tx_ep->bInterval);

    urb->transfer_flags  = URB_ISO_ASAP;

    fill_isoc_descriptor(urb, skb->len,
            le16_to_cpu(data->isoc_tx_ep->wMaxPacketSize));

    hdev->stat.sco_tx++;

    usb_anchor_urb(urb, &data->tx_anchor);

    err = usb_submit_urb(urb, GFP_ATOMIC);
    if (err < 0) {
        AICBT_ERR("%s: Failed to submit urb %p, pkt type %d, err %d",
                __func__, urb, bt_cb(skb)->pkt_type, err);
        kfree(urb->setup_packet);
        usb_unanchor_urb(urb);
    } else
        usb_mark_last_busy(data->udev);
    usb_free_urb(urb);

    return err;

}

static bool snd_copy_send_sco_data( AIC_sco_card_t *pSCOSnd)
{
    struct snd_pcm_runtime *runtime = pSCOSnd->playback.substream->runtime;
  	unsigned int frame_bytes = 2, frames1;
    const u8 *source;

    snd_pcm_uframes_t period_size = runtime->period_size;
    int i, count;
    u8 buffer[period_size * 3];
    int sco_packet_bytes = pSCOSnd->playback.sco_packet_bytes;
    struct sk_buff *skb;

    count = frames_to_bytes(runtime, period_size)/sco_packet_bytes;
    skb = bt_skb_alloc(((sco_packet_bytes + HCI_SCO_HDR_SIZE) * count), GFP_ATOMIC);
    skb->dev = (void *)hci_dev_get(0);
    bt_cb(skb)->pkt_type = HCI_SCODATA_PKT;
    skb_put(skb, ((sco_packet_bytes + HCI_SCO_HDR_SIZE) * count));
    if(!skb)
        return false;

    AICBT_DBG("%s, buffer_pos:%d sco_handle:%d sco_packet_bytes:%d count:%d", __FUNCTION__, pSCOSnd->playback.buffer_pos, pSCOSnd->usb_data->sco_handle,
    sco_packet_bytes, count);

    source = runtime->dma_area + pSCOSnd->playback.buffer_pos * frame_bytes;

    if (pSCOSnd->playback.buffer_pos + period_size <= runtime->buffer_size) {
      memcpy(buffer, source, period_size * frame_bytes);
    } else {
      /* wrap around at end of ring buffer */
      frames1 = runtime->buffer_size - pSCOSnd->playback.buffer_pos;
      memcpy(buffer, source, frames1 * frame_bytes);
      memcpy(&buffer[frames1 * frame_bytes],
             runtime->dma_area, (period_size - frames1) * frame_bytes);
    }

    pSCOSnd->playback.buffer_pos += period_size;
    if ( pSCOSnd->playback.buffer_pos >= runtime->buffer_size)
       pSCOSnd->playback.buffer_pos -= runtime->buffer_size;

    for(i = 0; i < count; i++) {
        *((__u16 *)(skb->data + i * (sco_packet_bytes + HCI_SCO_HDR_SIZE))) = pSCOSnd->usb_data->sco_handle;
        *((__u8 *)(skb->data + i*(sco_packet_bytes + HCI_SCO_HDR_SIZE) + 2)) = sco_packet_bytes;
        memcpy((skb->data + i * (sco_packet_bytes + HCI_SCO_HDR_SIZE) + HCI_SCO_HDR_SIZE),
          &buffer[sco_packet_bytes * i], sco_packet_bytes);
    }

    if(test_bit(ALSA_PLAYBACK_RUNNING, &pSCOSnd->states)) {
        snd_pcm_period_elapsed(pSCOSnd->playback.substream);
    }
    snd_send_sco_frame(skb);
    return true;
}

static void btusb_isoc_snd_tx_complete(struct urb *urb)
{
    struct sk_buff *skb = urb->context;
    struct hci_dev *hdev = (struct hci_dev *) skb->dev;
    struct btusb_data *data = GET_DRV_DATA(hdev);
    AIC_sco_card_t  *pSCOSnd = data->pSCOSnd;

    AICBT_DBG("%s: status %d count %d",
            __func__,urb->status, urb->actual_length);

    if (skb && hdev) {
        if (!test_bit(HCI_RUNNING, &hdev->flags))
            goto done;

        if (!urb->status)
            hdev->stat.byte_tx += urb->transfer_buffer_length;
        else
            hdev->stat.err_tx++;
    } else
        AICBT_ERR("%s: skb 0x%p hdev 0x%p", __func__, skb, hdev);

done:
    kfree(urb->setup_packet);
    kfree_skb(skb);
    if(test_bit(ALSA_PLAYBACK_RUNNING, &pSCOSnd->states)){
        snd_copy_send_sco_data(pSCOSnd);
        //schedule_work(&pSCOSnd->send_sco_work);
    }
}

static void playback_work(struct work_struct *work)
{
    AIC_sco_card_t *pSCOSnd = container_of(work, AIC_sco_card_t, send_sco_work);

    snd_copy_send_sco_data(pSCOSnd);
}

#endif

#if (CONFIG_BLUEDROID) || (HCI_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
int btusb_send_frame(struct sk_buff *skb)
{
    struct hci_dev *hdev = (struct hci_dev *) skb->dev;
#else
int btusb_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
#endif
    //struct hci_dev *hdev = (struct hci_dev *) skb->dev;

    struct btusb_data *data = GET_DRV_DATA(hdev);
    struct usb_ctrlrequest *dr;
    struct urb *urb;
    unsigned int pipe;
    int err = 0;
    int retries = 0;
    u16 *opcode = NULL;

    AICBT_DBG("%s: hdev %p, btusb data %p, pkt type %d",
            __func__, hdev, data, bt_cb(skb)->pkt_type);

    //printk("aic %d %d\r\n", bt_cb(skb)->pkt_type, skb->len);
    if (!test_bit(HCI_RUNNING, &hdev->flags))
        return -EBUSY;

#if (CONFIG_BLUEDROID == 0)
#if HCI_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	skb->dev = (void *)hdev;
#endif
#endif

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
        if (bt_cb(skb)->pkt_type == HCI_COMMAND_PKT) {
            print_command(skb);
            opcode = (u16*)(skb->data);
            printk("aic cmd:0x%04x", *opcode);
        } else {
            print_acl(skb, 1);
        }
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
            AICBT_ERR("%s: Failed to allocate mem for sco pkts", __func__);
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
        AICBT_ERR("%s: Failed to submit urb %p, pkt type %d, err %d, retries %d",
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

    AICBT_DBG("%s: name %s", __func__, hdev->name);

    kfree(data);
}
#endif

static void btusb_notify(struct hci_dev *hdev, unsigned int evt)
{
    struct btusb_data *data = GET_DRV_DATA(hdev);

    AICBT_DBG("%s: name %s, evt %d", __func__, hdev->name, evt);

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
        AICBT_ERR("%s: Failed to set interface, altsetting %d, err %d",
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
        AICBT_ERR("%s: Invalid SCO descriptors", __func__);
        return -ENODEV;
    }

	AICBT_ERR("%s: hdev->reassembly implemant\r\n",
			__func__);

#if CONFIG_BLUEDROID
    if(hdev->reassembly[HCI_SCODATA_PKT - 1]) {
        kfree_skb(hdev->reassembly[HCI_SCODATA_PKT - 1]);
        hdev->reassembly[HCI_SCODATA_PKT - 1] = NULL;
    }
#endif
    return 0;
}

static void set_select_msbc(enum CODEC_TYPE type)
{
    printk("%s codec type = %d", __func__, (int)type);
    codec_type = type;
}

static enum CODEC_TYPE check_select_msbc(void)
{
    return codec_type;
}

#ifdef CONFIG_SCO_OVER_HCI
static int check_controller_support_msbc( struct usb_device *udev)
{
    //fix this in the future,when new card support msbc decode and encode
    AICBT_INFO("%s:pid = 0x%02x, vid = 0x%02x",__func__,udev->descriptor.idProduct, udev->descriptor.idVendor);
    switch (udev->descriptor.idProduct) {

        default:
          return 0;
    }
    return 0;
}
#endif
static void btusb_work(struct work_struct *work)
{
    struct btusb_data *data = container_of(work, struct btusb_data, work);
    struct hci_dev *hdev = data->hdev;
    int err;
    int new_alts;
#ifdef CONFIG_SCO_OVER_HCI
    AIC_sco_card_t  *pSCOSnd = data->pSCOSnd;
#endif
	printk("%s data->sco_num:%d \r\n", __func__, data->sco_num);
	
    if (data->sco_num > 0) {
        if (!test_bit(BTUSB_DID_ISO_RESUME, &data->flags)) {
            err = usb_autopm_get_interface(data->isoc ? data->isoc : data->intf);
            if (err < 0) {
                clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
                mdelay(URB_CANCELING_DELAY_MS);
                usb_kill_anchored_urbs(&data->isoc_anchor);
				printk("%s usb_kill_anchored_urbs after \r\n", __func__);
                return;
            }

            set_bit(BTUSB_DID_ISO_RESUME, &data->flags);
        }

	hdev->voice_setting = 93;
        AICBT_INFO("%s voice settings = 0x%04x", __func__, hdev->voice_setting);
        if (!(hdev->voice_setting & 0x0003)) {
            if(data->sco_num == 1)
                if(check_select_msbc()) {
                    new_alts = 1;
                } else {
                    new_alts = 2;
                }
            else {
              AICBT_INFO("%s: we don't support mutiple sco link for cvsd", __func__);
              return;
            }
        } else{
            if(check_select_msbc()) {
                if(data->sco_num == 1)
                    new_alts = 1;
                else {
                    AICBT_INFO("%s: we don't support mutiple sco link for msbc", __func__);
                    return;
                }
            } else {
                new_alts = 2;
            }
        }
        if (data->isoc_altsetting != new_alts) {

            clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
            mdelay(URB_CANCELING_DELAY_MS);
            usb_kill_anchored_urbs(&data->isoc_anchor);

			printk("%s set_isoc_interface in \r\n", __func__);
            if (set_isoc_interface(hdev, new_alts) < 0)
                return;
			
        }
		
		printk("%s set_isoc_interface out \r\n", __func__);

        if (!test_and_set_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
			printk("%s btusb_submit_isoc_urb\r\n", __func__);
            if (btusb_submit_isoc_urb(hdev, GFP_KERNEL) < 0)
                clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
            else
                btusb_submit_isoc_urb(hdev, GFP_KERNEL);
        }
#ifdef CONFIG_SCO_OVER_HCI
        if(test_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
            set_bit(USB_CAPTURE_RUNNING, &data->pSCOSnd->states);
            set_bit(USB_PLAYBACK_RUNNING, &data->pSCOSnd->states);
        }
        if (test_bit(ALSA_PLAYBACK_RUNNING, &pSCOSnd->states)) {
            schedule_work(&pSCOSnd->send_sco_work);
            AICBT_INFO("%s: play_timer restart", __func__);
        }
#endif
    } else {
        clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
#ifdef CONFIG_SCO_OVER_HCI
        clear_bit(USB_CAPTURE_RUNNING, &data->pSCOSnd->states);
        clear_bit(USB_PLAYBACK_RUNNING, &data->pSCOSnd->states);
		//AIC_sco_card_t	*pSCOSnd = data->pSCOSnd;
		if (test_bit(ALSA_PLAYBACK_RUNNING, &pSCOSnd->states)) {
			mod_timer(&snd_cap_timer.play_timer,jiffies + msecs_to_jiffies(30));
			AICBT_INFO("%s: play_timer start", __func__);
		}
#endif
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

    AICBT_DBG("%s", __func__);

    err = usb_autopm_get_interface(data->intf);
    if (err < 0)
        return;

    usb_autopm_put_interface(data->intf);
}

int bt_pm_notify(struct notifier_block *notifier, ulong pm_event, void *unused)
{
    struct btusb_data *data;
    firmware_info *fw_info;
    struct usb_device *udev;

    AICBT_INFO("%s: pm event %ld", __func__, pm_event);

    data = container_of(notifier, struct btusb_data, pm_notifier);
    fw_info = data->fw_info;
    udev = fw_info->udev;

    switch (pm_event) {
    case PM_SUSPEND_PREPARE:
    case PM_HIBERNATION_PREPARE:
#if 0
        patch_entry->fw_len = load_firmware(fw_info, &patch_entry->fw_cache);
        if (patch_entry->fw_len <= 0) {
        /* We may encount failure in loading firmware, just give a warning */
            AICBT_WARN("%s: Failed to load firmware", __func__);
        }
#endif
        if (!device_may_wakeup(&udev->dev)) {
#if (CONFIG_RESET_RESUME || CONFIG_BLUEDROID)
            AICBT_INFO("%s:remote wakeup not supported, reset resume supported", __func__);
#else
            fw_info->intf->needs_binding = 1;
            AICBT_INFO("%s:remote wakeup not supported, binding needed", __func__);
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
    struct usb_device *udev;

    AICBT_INFO("%s: pm event %ld", __func__, pm_event);

    data = container_of(notifier, struct btusb_data, reboot_notifier);
    fw_info = data->fw_info;
    udev = fw_info->udev;

    switch (pm_event) {
    case SYS_DOWN:
        AICBT_DBG("%s:system down or restart", __func__);
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
		    AICBT_ERR("%s: Failed to download suspend fw", __func__);
#endif

#ifdef SET_WAKEUP_DEVICE
        set_wakeup_device_from_conf(fw_info_4_suspend);
#endif
        AICBT_DBG("%s:system halt or power off", __func__);
    break;

    default:
        break;
    }

    return NOTIFY_DONE;
}


#ifdef CONFIG_SCO_OVER_HCI
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
void aic_snd_capture_timeout(ulong data)
#else
void aic_snd_capture_timeout(struct timer_list *t)
#endif
{
	uint8_t null_data[255];
	struct btusb_data *usb_data;
	
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
    usb_data = (struct btusb_data *)data;
#else
    usb_data = &snd_cap_timer.snd_usb_data;
#endif
    aic_copy_capture_data_to_alsa(usb_data, null_data, snd_cap_timer.snd_sco_length/2);
	//printk("%s enter\r\n", __func__);
    mod_timer(&snd_cap_timer.cap_timer,jiffies + msecs_to_jiffies(3));
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
void aic_snd_play_timeout(ulong data)
#else
void aic_snd_play_timeout(struct timer_list *t)
#endif
{
	AIC_sco_card_t *pSCOSnd;
	struct snd_pcm_runtime *runtime;
	snd_pcm_uframes_t period_size;
    int count;
	struct btusb_data *usb_data;
	int sco_packet_bytes;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
    usb_data = (struct btusb_data *)data;
#else
    usb_data = &snd_cap_timer.snd_usb_data;
#endif
	pSCOSnd = usb_data->pSCOSnd;

	if(test_bit(USB_PLAYBACK_RUNNING, &pSCOSnd->states)) {
		return;
	}

	if(!test_bit(ALSA_PLAYBACK_RUNNING, &pSCOSnd->states)) {
		return;
	}

	runtime = pSCOSnd->playback.substream->runtime;
	period_size = runtime->period_size;
    sco_packet_bytes = pSCOSnd->playback.sco_packet_bytes;
    count = frames_to_bytes(runtime, period_size)/sco_packet_bytes;

    pSCOSnd->playback.buffer_pos += period_size;
    if ( pSCOSnd->playback.buffer_pos >= runtime->buffer_size)
       pSCOSnd->playback.buffer_pos -= runtime->buffer_size;

    if(test_bit(ALSA_PLAYBACK_RUNNING, &pSCOSnd->states)) {
        snd_pcm_period_elapsed(pSCOSnd->playback.substream);
    }
    //AICBT_DBG("%s,play_timer restart buffer_pos:%d sco_handle:%d sco_packet_bytes:%d count:%d", __FUNCTION__, pSCOSnd->playback.buffer_pos, pSCOSnd->usb_data->sco_handle,
    //sco_packet_bytes, count);
    mod_timer(&snd_cap_timer.play_timer,jiffies + msecs_to_jiffies(3*count));
}

static const struct snd_pcm_hardware snd_card_sco_capture_default =
{
    .info               = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_NONINTERLEAVED |
                            SNDRV_PCM_ACCESS_RW_INTERLEAVED | SNDRV_PCM_INFO_FIFO_IN_FRAMES),
    .formats            = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8,
    .rates              = (SNDRV_PCM_RATE_8000),
    .rate_min           = 8000,
    .rate_max           = 8000,
    .channels_min       = 1,
    .channels_max       = 1,
    .buffer_bytes_max   = 8 * 768,
    .period_bytes_min   = 48,
    .period_bytes_max   = 768,
    .periods_min        = 1,
    .periods_max        = 8,
    .fifo_size          = 8,

};

static int snd_sco_capture_pcm_open(struct snd_pcm_substream * substream)
{
    AIC_sco_card_t  *pSCOSnd = substream->private_data;

    AICBT_INFO("%s", __FUNCTION__);
    pSCOSnd->capture.substream = substream;

    memcpy(&substream->runtime->hw, &snd_card_sco_capture_default, sizeof(struct snd_pcm_hardware));
	pSCOSnd->capture.buffer_pos = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
	init_timer(&snd_cap_timer.cap_timer);
	snd_cap_timer.cap_timer.data = (unsigned long)pSCOSnd->usb_data;
	snd_cap_timer.cap_timer.function = aic_snd_capture_timeout;
#else
	timer_setup(&snd_cap_timer.cap_timer, aic_snd_capture_timeout, 0);
	snd_cap_timer.snd_usb_data = *(pSCOSnd->usb_data);
#endif

    if(check_controller_support_msbc(pSCOSnd->dev)) {
        substream->runtime->hw.rates |= SNDRV_PCM_RATE_16000;
        substream->runtime->hw.rate_max = 16000;
        substream->runtime->hw.period_bytes_min = 96;
        substream->runtime->hw.period_bytes_max = 16 * 96;
        substream->runtime->hw.buffer_bytes_max = 8 * 16 * 96;
    }
    set_bit(ALSA_CAPTURE_OPEN, &pSCOSnd->states);
    return 0;
}

static int snd_sco_capture_pcm_close(struct snd_pcm_substream *substream)
{
	AIC_sco_card_t *pSCOSnd = substream->private_data;

	del_timer(&snd_cap_timer.cap_timer);
	clear_bit(ALSA_CAPTURE_OPEN, &pSCOSnd->states);
	return 0;
}

static int snd_sco_capture_ioctl(struct snd_pcm_substream *substream,  unsigned int cmd, void *arg)
{
    AICBT_DBG("%s, cmd = %d", __FUNCTION__, cmd);
    switch (cmd)
    {
        default:
            return snd_pcm_lib_ioctl(substream, cmd, arg);
    }
    return 0;
}

static int snd_sco_capture_pcm_hw_params(struct snd_pcm_substream * substream, struct snd_pcm_hw_params * hw_params)
{

    int err;
    struct snd_pcm_runtime *runtime = substream->runtime;
    err = snd_pcm_lib_alloc_vmalloc_buffer(substream, params_buffer_bytes(hw_params));
    AICBT_INFO("%s,err : %d,  runtime state : %d", __FUNCTION__, err, runtime->status->state);
    return err;
}

static int snd_sco_capture_pcm_hw_free(struct snd_pcm_substream * substream)
{
    AICBT_DBG("%s", __FUNCTION__);
    return snd_pcm_lib_free_vmalloc_buffer(substream);;
}

static int snd_sco_capture_pcm_prepare(struct snd_pcm_substream *substream)
{
    AIC_sco_card_t *pSCOSnd = substream->private_data;
    struct snd_pcm_runtime *runtime = substream->runtime;

    AICBT_INFO("%s %d\n", __FUNCTION__, (int)runtime->period_size);
    if (test_bit(DISCONNECTED, &pSCOSnd->states))
		    return -ENODEV;
	  if (!test_bit(USB_CAPTURE_RUNNING, &pSCOSnd->states))
		    return -EIO;

    if(runtime->rate == 8000) {
        if(pSCOSnd->usb_data->isoc_altsetting != 2)
            return -ENOEXEC;
        pSCOSnd->capture.sco_packet_bytes = 48;
    }
    else if(runtime->rate == 16000 && check_controller_support_msbc(pSCOSnd->dev)) {
        if(pSCOSnd->usb_data->isoc_altsetting != 4)
            return -ENOEXEC;
        pSCOSnd->capture.sco_packet_bytes = 96;
    }
    else if(pSCOSnd->usb_data->isoc_altsetting == 2) {
        pSCOSnd->capture.sco_packet_bytes = 48;
    }
    else if(pSCOSnd->usb_data->isoc_altsetting == 1) {
        pSCOSnd->capture.sco_packet_bytes = 24;
    }
    return 0;
}

static int snd_sco_capture_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	  AIC_sco_card_t *pSCOSnd = substream->private_data;
    AICBT_INFO("%s, cmd : %d", __FUNCTION__, cmd);

	  switch (cmd) {
	    case SNDRV_PCM_TRIGGER_START:
		      if (!test_bit(USB_CAPTURE_RUNNING, &pSCOSnd->states))
			      return -EIO;
		      set_bit(ALSA_CAPTURE_RUNNING, &pSCOSnd->states);
		      return 0;
	    case SNDRV_PCM_TRIGGER_STOP:
		      clear_bit(ALSA_CAPTURE_RUNNING, &pSCOSnd->states);
		      return 0;
	    default:
		      return -EINVAL;
	  }
}

static snd_pcm_uframes_t snd_sco_capture_pcm_pointer(struct snd_pcm_substream *substream)
{
	  AIC_sco_card_t *pSCOSnd = substream->private_data;

	  return pSCOSnd->capture.buffer_pos;
}


static struct snd_pcm_ops snd_sco_capture_pcm_ops = {
	.open =         snd_sco_capture_pcm_open,
	.close =        snd_sco_capture_pcm_close,
	.ioctl =        snd_sco_capture_ioctl,
	.hw_params =    snd_sco_capture_pcm_hw_params,
	.hw_free =      snd_sco_capture_pcm_hw_free,
	.prepare =      snd_sco_capture_pcm_prepare,
	.trigger =      snd_sco_capture_pcm_trigger,
	.pointer =      snd_sco_capture_pcm_pointer,
};


static const struct snd_pcm_hardware snd_card_sco_playback_default =
{
    .info               = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_NONINTERLEAVED |
                            SNDRV_PCM_ACCESS_RW_INTERLEAVED | SNDRV_PCM_INFO_FIFO_IN_FRAMES),
    .formats            = SNDRV_PCM_FMTBIT_S16_LE,
    .rates              = (SNDRV_PCM_RATE_8000),
    .rate_min           = 8000,
    .rate_max           = 8000,
    .channels_min       = 1,
    .channels_max       = 1,
    .buffer_bytes_max   = 8 * 768,
    .period_bytes_min   = 48,
    .period_bytes_max   = 768,
    .periods_min        = 1,
    .periods_max        = 8,
    .fifo_size          = 8,
};

static int snd_sco_playback_pcm_open(struct snd_pcm_substream * substream)
{
    AIC_sco_card_t *pSCOSnd = substream->private_data;
    int err = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
	init_timer(&snd_cap_timer.play_timer);
	snd_cap_timer.play_timer.data = (unsigned long)pSCOSnd->usb_data;
	snd_cap_timer.play_timer.function = aic_snd_play_timeout;
#else
	timer_setup(&snd_cap_timer.play_timer, aic_snd_play_timeout, 0);
	snd_cap_timer.snd_usb_data = *(pSCOSnd->usb_data);
#endif
	pSCOSnd->playback.buffer_pos = 0;

    AICBT_INFO("%s, rate : %d", __FUNCTION__, substream->runtime->rate);
    memcpy(&substream->runtime->hw, &snd_card_sco_playback_default, sizeof(struct snd_pcm_hardware));
    if(check_controller_support_msbc(pSCOSnd->dev)) {
        substream->runtime->hw.rates |= SNDRV_PCM_RATE_16000;
        substream->runtime->hw.rate_max = 16000;
        substream->runtime->hw.period_bytes_min = 96;
        substream->runtime->hw.period_bytes_max = 16 * 96;
        substream->runtime->hw.buffer_bytes_max = 8 * 16 * 96;
    }
    pSCOSnd->playback.substream = substream;
    set_bit(ALSA_PLAYBACK_OPEN, &pSCOSnd->states);

    return err;
}

static int snd_sco_playback_pcm_close(struct snd_pcm_substream *substream)
{
    AIC_sco_card_t *pSCOSnd = substream->private_data;

	del_timer(&snd_cap_timer.play_timer);
	AICBT_INFO("%s: play_timer delete", __func__);
	clear_bit(ALSA_PLAYBACK_OPEN, &pSCOSnd->states);
    cancel_work_sync(&pSCOSnd->send_sco_work);
	  return 0;
}

static int snd_sco_playback_ioctl(struct snd_pcm_substream *substream,  unsigned int cmd, void *arg)
{
    AICBT_DBG("%s, cmd : %d", __FUNCTION__, cmd);
    switch (cmd)
    {
        default:
            return snd_pcm_lib_ioctl(substream, cmd, arg);
            break;
    }
    return 0;
}

static int snd_sco_playback_pcm_hw_params(struct snd_pcm_substream * substream, struct snd_pcm_hw_params * hw_params)
{
    int err;
    err = snd_pcm_lib_alloc_vmalloc_buffer(substream, params_buffer_bytes(hw_params));
    return err;
}

static int snd_sco_palyback_pcm_hw_free(struct snd_pcm_substream * substream)
{
    AICBT_DBG("%s", __FUNCTION__);
    return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int snd_sco_playback_pcm_prepare(struct snd_pcm_substream *substream)
{
	  AIC_sco_card_t *pSCOSnd = substream->private_data;
    struct snd_pcm_runtime *runtime = substream->runtime;

    AICBT_INFO("%s, bound_rate = %d", __FUNCTION__, runtime->rate);

	  if (test_bit(DISCONNECTED, &pSCOSnd->states))
		    return -ENODEV;
	  if (!test_bit(USB_PLAYBACK_RUNNING, &pSCOSnd->states))
		    return -EIO;

    if(runtime->rate == 8000) {
        if(pSCOSnd->usb_data->isoc_altsetting != 2)
            return -ENOEXEC;
        pSCOSnd->playback.sco_packet_bytes = 48;
    }
    else if(runtime->rate == 16000) {
        if(pSCOSnd->usb_data->isoc_altsetting != 4)
            return -ENOEXEC;
        pSCOSnd->playback.sco_packet_bytes = 96;
    }

  	return 0;
}

static int snd_sco_playback_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
  	AIC_sco_card_t *pSCOSnd = substream->private_data;

    AICBT_INFO("%s, cmd = %d", __FUNCTION__, cmd);
  	switch (cmd) {
      	case SNDRV_PCM_TRIGGER_START:
      		if (!test_bit(USB_PLAYBACK_RUNNING, &pSCOSnd->states))
      			return -EIO;
      		set_bit(ALSA_PLAYBACK_RUNNING, &pSCOSnd->states);
          schedule_work(&pSCOSnd->send_sco_work);
#ifdef CONFIG_SCO_OVER_HCI
		  if (!test_bit(USB_PLAYBACK_RUNNING, &pSCOSnd->states)) {
			  AICBT_INFO("%s: play_timer cmd 1 start ", __func__);
			  mod_timer(&snd_cap_timer.play_timer,jiffies + msecs_to_jiffies(3));
		  }
#endif
      		return 0;
      	case SNDRV_PCM_TRIGGER_STOP:
      		clear_bit(ALSA_PLAYBACK_RUNNING, &pSCOSnd->states);
      		return 0;
      	default:
      		return -EINVAL;
  	}
}

static snd_pcm_uframes_t snd_sco_playback_pcm_pointer(struct snd_pcm_substream *substream)
{
  	AIC_sco_card_t *pSCOSnd = substream->private_data;

  	return pSCOSnd->playback.buffer_pos;
}


static struct snd_pcm_ops snd_sco_playback_pcm_ops = {
	.open =         snd_sco_playback_pcm_open,
	.close =        snd_sco_playback_pcm_close,
	.ioctl =        snd_sco_playback_ioctl,
	.hw_params =    snd_sco_playback_pcm_hw_params,
	.hw_free =      snd_sco_palyback_pcm_hw_free,
	.prepare =      snd_sco_playback_pcm_prepare,
	.trigger =      snd_sco_playback_pcm_trigger,
	.pointer =      snd_sco_playback_pcm_pointer,
};


static AIC_sco_card_t* btusb_snd_init(struct usb_interface *intf, const struct usb_device_id *id, struct btusb_data *data)
{
    struct snd_card *card;
    AIC_sco_card_t  *pSCOSnd;
    int err=0;
    AICBT_INFO("%s", __func__);
    err = snd_card_new(&intf->dev,
     -1, AIC_SCO_ID, THIS_MODULE,
     sizeof(AIC_sco_card_t), &card);
    if (err < 0) {
        AICBT_ERR("%s: sco snd card create fail", __func__);
        return NULL;
    }
    // private data
    pSCOSnd = (AIC_sco_card_t *)card->private_data;
    pSCOSnd->card = card;
    pSCOSnd->dev = interface_to_usbdev(intf);
    pSCOSnd->usb_data = data;

    strcpy(card->driver, AIC_SCO_ID);
    strcpy(card->shortname, "Aicsemi sco snd");
    sprintf(card->longname, "Aicsemi sco over hci: VID:0x%04x, PID:0x%04x",
        id->idVendor, pSCOSnd->dev->descriptor.idProduct);

    err = snd_pcm_new(card, AIC_SCO_ID, 0, 1, 1, &pSCOSnd->pcm);
    if (err < 0) {
        AICBT_ERR("%s: sco snd card new pcm fail", __func__);
        return NULL;
    }
    pSCOSnd->pcm->private_data = pSCOSnd;
    sprintf(pSCOSnd->pcm->name, "sco_pcm:VID:0x%04x, PID:0x%04x",
      id->idVendor, pSCOSnd->dev->descriptor.idProduct);

    snd_pcm_set_ops(pSCOSnd->pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_sco_playback_pcm_ops);
    snd_pcm_set_ops(pSCOSnd->pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_sco_capture_pcm_ops);

    err = snd_card_register(card);
    if (err < 0) {
        AICBT_ERR("%s: sco snd card register card fail", __func__);
        return NULL;
    }

    spin_lock_init(&pSCOSnd->capture_lock);
    spin_lock_init(&pSCOSnd->playback_lock);
    INIT_WORK(&pSCOSnd->send_sco_work, playback_work);
    return pSCOSnd;
}
#endif

static int aicwf_usb_chipmatch(u16 vid, u16 pid){

	if(pid == USB_PRODUCT_ID_AIC8801){
		g_chipid = PRODUCT_ID_AIC8801;
		printk("%s USE AIC8801\r\n", __func__);
		return 0;
	}else if(pid == USB_PRODUCT_ID_AIC8800DC){
		g_chipid = PRODUCT_ID_AIC8800DC;
		printk("%s USE AIC8800DC\r\n", __func__);
		return 0;
	}else if(pid == USB_PRODUCT_ID_AIC8800D80){
                g_chipid = PRODUCT_ID_AIC8800D80;
                printk("%s USE AIC8800D80\r\n", __func__);
                return 0;
	}else{
		return -1;
	}
}


static int btusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usb_device *udev = interface_to_usbdev(intf);
    struct usb_endpoint_descriptor *ep_desc;
    u8 endpoint_num;
    struct btusb_data *data;
    struct hci_dev *hdev;
    firmware_info *fw_info;
    int i, err=0;

    bt_support = 1;
    
    AICBT_INFO("%s: usb_interface %p, bInterfaceNumber %d, idVendor 0x%04x, "
            "idProduct 0x%04x", __func__, intf,
            intf->cur_altsetting->desc.bInterfaceNumber,
            id->idVendor, id->idProduct);

	aicwf_usb_chipmatch(id->idVendor, id->idProduct);

    /* interface numbers are hardcoded in the spec */
    if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
        return -ENODEV;

    AICBT_DBG("%s: can wakeup = %x, may wakeup = %x", __func__,
            device_can_wakeup(&udev->dev), device_may_wakeup(&udev->dev));

    data = aic_alloc(intf);
    if (!data)
        return -ENOMEM;

    for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
        ep_desc = &intf->cur_altsetting->endpoint[i].desc;

        endpoint_num = usb_endpoint_num(ep_desc);
        printk("endpoint num %d\n", endpoint_num);

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
        aic_free(data);
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

#if (CONFIG_BLUEDROID == 0)
#if HCI_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
		spin_lock_init(&data->rxlock);
		data->recv_bulk = btusb_recv_bulk;
#endif
#endif


    fw_info = firmware_info_init(intf);
    if (fw_info)
        data->fw_info = fw_info;
    else {
        AICBT_WARN("%s: Failed to initialize fw info", __func__);
        /* Skip download patch */
        goto end;
    }

    AICBT_INFO("%s: download begining...", __func__);

#if CONFIG_BLUEDROID
    mutex_lock(&btchr_mutex);
#endif
	if(g_chipid == PRODUCT_ID_AIC8800DC){
		err = download_patch(data->fw_info,1);
	}

#if CONFIG_BLUEDROID
    mutex_unlock(&btchr_mutex);
#endif

    AICBT_INFO("%s: download ending...", __func__);
	if (err < 0) {
		return err;
	}


    hdev = hci_alloc_dev();
    if (!hdev) {
        aic_free(data);
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
#if (CONFIG_BLUEDROID == 0)
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 0, 9)
    hdev->shutdown = btusb_shutdown;
#endif
#endif //(CONFIG_BLUEDROIF == 0)

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
        AICBT_DBG("%s: Set HCI_QUIRK_RESET_ON_CLOSE", __func__);
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
            aic_free(data);
            data = NULL;
            return err;
        }
#ifdef CONFIG_SCO_OVER_HCI
        data->pSCOSnd = btusb_snd_init(intf, id, data);
#endif
    }

    err = hci_register_dev(hdev);
    if (err < 0) {
        hci_free_dev(hdev);
        hdev = NULL;
        aic_free(data);
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
    AICBT_INFO("%s: Check bt reset flag %d", __func__, bt_reset);
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
#if CONFIG_BLUEDROID
    wait_event_interruptible(bt_dlfw_wait, (check_set_dlfw_state_value(2) == 2));
#endif

    bt_support = 0;

    AICBT_INFO("%s: usb_interface %p, bInterfaceNumber %d",
            __func__, intf, intf->cur_altsetting->desc.bInterfaceNumber);

    data = usb_get_intfdata(intf);

    if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
        return;

    if (data)
        hdev = data->hdev;
    else {
        AICBT_WARN("%s: Failed to get bt usb data[Null]", __func__);
        return;
    }

#ifdef CONFIG_SCO_OVER_HCI
    if (intf->cur_altsetting->desc.bInterfaceNumber == 0) {
        AIC_sco_card_t *pSCOSnd = data->pSCOSnd;
        if(!pSCOSnd) {
            AICBT_ERR("%s: sco private data is null", __func__);
            return;
        }
        set_bit(DISCONNECTED, &pSCOSnd->states);
        snd_card_disconnect(pSCOSnd->card);
        snd_card_free_when_closed(pSCOSnd->card);
    }
#endif

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
        AICBT_INFO("%s: Set BT reset flag", __func__);
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
    aic_free(data);
    data = NULL;
    set_dlfw_state_value(0);
}

#ifdef CONFIG_PM
static int btusb_suspend(struct usb_interface *intf, pm_message_t message)
{
    struct btusb_data *data = usb_get_intfdata(intf);
    //firmware_info *fw_info = data->fw_info;

    AICBT_INFO("%s: event 0x%x, suspend count %d", __func__,
            message.event, data->suspend_count);

    if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
        return 0;
#if 0
    if (!test_bit(HCI_RUNNING, &data->hdev->flags))
        set_bt_onoff(fw_info, 1);
#endif
    if (data->suspend_count++)
        return 0;

    spin_lock_irq(&data->txlock);
    if (!((message.event & PM_EVENT_AUTO) && data->tx_in_flight)) {
        set_bit(BTUSB_SUSPENDING, &data->flags);
        spin_unlock_irq(&data->txlock);
    } else {
        spin_unlock_irq(&data->txlock);
        data->suspend_count--;
        AICBT_ERR("%s: Failed to enter suspend", __func__);
        return -EBUSY;
    }

    cancel_work_sync(&data->work);

    btusb_stop_traffic(data);
    mdelay(URB_CANCELING_DELAY_MS);
    usb_kill_anchored_urbs(&data->tx_anchor);

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
            AICBT_ERR("%s: Failed to submit urb %p, err %d",
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
    int err = 0;

    AICBT_INFO("%s: Suspend count %d", __func__, data->suspend_count);

    if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
        return 0;

    if (--data->suspend_count)
        return 0;

    #if 0
    /*check_fw_version to check the status of the BT Controller after USB Resume*/
    err = check_fw_version(fw_info);
    if (err !=0)
    {
        AICBT_INFO("%s: BT Controller Power OFF And Return hci_hardware_error:%d", __func__, err);
        hci_hardware_error();
    }
    #endif

    AICBT_INFO("%s g_chipid %x\n", __func__, g_chipid);
    if(g_chipid == PRODUCT_ID_AIC8800DC){
        if(data->fw_info){
            err = download_patch(data->fw_info,1);
        }else{
            AICBT_WARN("%s: Failed to initialize fw info", __func__);
        }
    }

    #if 1
    if (test_bit(BTUSB_INTR_RUNNING, &data->flags)) {
        err = btusb_submit_intr_urb(hdev, GFP_NOIO);
        if (err < 0) {
            clear_bit(BTUSB_INTR_RUNNING, &data->flags);
            goto failed;
        }
    }
    #endif

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
    .name        = "aic_btusb",
    .probe        = btusb_probe,
    .disconnect    = btusb_disconnect,
#ifdef CONFIG_PM
    .suspend    = btusb_suspend,
    .resume        = btusb_resume,
#if CONFIG_RESET_RESUME
    .reset_resume    = btusb_resume,
#endif
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

    AICBT_INFO("AICBT_RELEASE_NAME: %s",AICBT_RELEASE_NAME);
    AICBT_INFO("AicSemi Bluetooth USB driver module init, version %s", VERSION);
	AICBT_INFO("RELEASE DATE: 2023_0506_1635 \r\n");
#if CONFIG_BLUEDROID
    err = btchr_init();
    if (err < 0) {
        /* usb register will go on, even bt char register failed */
        AICBT_ERR("Failed to register usb char device interfaces");
    } else
        bt_char_dev_registered = 1;
#endif
    err = usb_register(&btusb_driver);
    if (err < 0)
        AICBT_ERR("Failed to register aic bluetooth USB driver");
    return err;
}

static void __exit btusb_exit(void)
{
    AICBT_INFO("AicSemi Bluetooth USB driver module exit");
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif

MODULE_AUTHOR("AicSemi Corporation");
MODULE_DESCRIPTION("AicSemi Bluetooth USB driver version");
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
