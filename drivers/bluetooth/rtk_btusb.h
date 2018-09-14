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
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/usb.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/poll.h>

#include <linux/version.h>
#include <linux/pm_runtime.h>
#include <linux/firmware.h>
#include <linux/suspend.h>

#define CONFIG_BLUEDROID        1 /* bleuz 0, bluedroid 1 */
//#define CONFIG_SCO_OVER_HCI

#ifdef CONFIG_SCO_OVER_HCI
#include <linux/usb/audio.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#define RTK_SCO_ID "snd_sco_rtk"
enum {
	USB_CAPTURE_RUNNING,
	USB_PLAYBACK_RUNNING,
	ALSA_CAPTURE_OPEN,
	ALSA_PLAYBACK_OPEN,
	ALSA_CAPTURE_RUNNING,
	ALSA_PLAYBACK_RUNNING,
	CAPTURE_URB_COMPLETED,
	PLAYBACK_URB_COMPLETED,
	DISCONNECTED,
};

// RTK sound card
typedef struct RTK_sco_card {
    struct snd_card *card;
    struct snd_pcm *pcm;
    struct usb_device *dev;
    struct btusb_data *usb_data;
    unsigned long states;
    struct rtk_sco_stream {
		    struct snd_pcm_substream *substream;
		    unsigned int sco_packet_bytes;
		    snd_pcm_uframes_t buffer_pos;
	  } capture, playback;
    spinlock_t capture_lock;
    spinlock_t playback_lock;
    struct work_struct send_sco_work;
} RTK_sco_card_t;
#endif
/* Some Android system may use standard Linux kernel, while
 * standard Linux may also implement early suspend feature.
 * So exclude earysuspend.h from CONFIG_BLUEDROID.
 */
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#if CONFIG_BLUEDROID
#else
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci.h>
#endif


/***********************************
** Realtek - For rtk_btusb driver **
***********************************/
#define URB_CANCELING_DELAY_MS          10
/* when OS suspended, module is still powered,usb is not powered,
 * this may set to 1, and must comply with special patch code.
 */
#define CONFIG_RESET_RESUME     1
#define PRINT_CMD_EVENT         0
#define PRINT_ACL_DATA          0
#define PRINT_SCO_DATA          0

#define RTKBT_DBG_FLAG          0

#if RTKBT_DBG_FLAG
#define RTKBT_DBG(fmt, arg...) printk(KERN_INFO "rtk_btusb: " fmt "\n" , ## arg)
#else
#define RTKBT_DBG(fmt, arg...)
#endif
#define RTKBT_INFO(fmt, arg...) printk(KERN_INFO "rtk_btusb: " fmt "\n" , ## arg)
#define RTKBT_WARN(fmt, arg...) printk(KERN_WARNING "rtk_btusb: " fmt "\n" , ## arg)
#define RTKBT_ERR(fmt, arg...) printk(KERN_ERR "rtk_btusb: " fmt "\n" , ## arg)


#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 33)
#define HDEV_BUS        hdev->bus
#define USB_RPM            1
#else
#define HDEV_BUS        hdev->type
#define USB_RPM            0
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 38)
#define NUM_REASSEMBLY 3
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 4, 0)
#define GET_DRV_DATA(x)        hci_get_drvdata(x)
#else
#define GET_DRV_DATA(x)        x->driver_data
#endif

#define BTUSB_RPM        (0 * USB_RPM) /* 1 SS enable; 0 SS disable */
#define BTUSB_WAKEUP_HOST        0    /* 1  enable; 0  disable */
#define BTUSB_MAX_ISOC_FRAMES    48
#define BTUSB_INTR_RUNNING        0
#define BTUSB_BULK_RUNNING        1
#define BTUSB_ISOC_RUNNING        2
#define BTUSB_SUSPENDING        3
#define BTUSB_DID_ISO_RESUME    4

#define HCI_CMD_READ_BD_ADDR 0x1009
#define HCI_VENDOR_CHANGE_BDRATE 0xfc17
#define HCI_VENDOR_READ_RTK_ROM_VERISION 0xfc6d
#define HCI_VENDOR_READ_LMP_VERISION 0x1001
#define HCI_VENDOR_FORCE_RESET_AND_PATCHABLE 0xfc66
#define HCI_VENDOR_RESET                       0x0C03
#define HCI_VENDOR_ADD_WAKE_UP_DEVICE       0xfc7b
#define HCI_VENDOR_REMOVE_WAKE_UP_DEVICE    0xfc7c
#define HCI_VENDOR_CLEAR_POWERON_LIST       0xfc7d

#define HCI_VENDOR_USB_DISC_HARDWARE_ERROR   0xFF

#define SET_WAKEUP_DEVICE_CONF      "/data/misc/bluedroid/rtkbt_wakeup_ble.conf"

#define DRV_NORMAL_MODE 0
#define DRV_MP_MODE 1
int mp_drv_mode = 0; /* 1 Mptool Fw; 0 Normal Fw */

#define ROM_LMP_NONE                0x0000
#define ROM_LMP_8723a               0x1200
#define ROM_LMP_8723b               0x8723
#define ROM_LMP_8821a               0X8821
#define ROM_LMP_8761a               0X8761
#define ROM_LMP_8703a               0x8723
#define ROM_LMP_8763a               0x8763
#define ROM_LMP_8703b               0x8703
#define ROM_LMP_8723c               0x8703
#define ROM_LMP_8822b               0x8822
#define ROM_LMP_8723d               0x8723
#define ROM_LMP_8821c               0x8821

/* signature: Realtek */
const uint8_t RTK_EPATCH_SIGNATURE[8] = {0x52,0x65,0x61,0x6C,0x74,0x65,0x63,0x68};
/* Extension Section IGNATURE:0x77FD0451 */
const uint8_t EXTENSION_SECTION_SIGNATURE[4] = {0x51,0x04,0xFD,0x77};

uint16_t project_id[] = {
    ROM_LMP_8723a,
    ROM_LMP_8723b,
    ROM_LMP_8821a,
    ROM_LMP_8761a,
    ROM_LMP_8703a,
    ROM_LMP_8763a,
    ROM_LMP_8703b,
    ROM_LMP_8723c,
    ROM_LMP_8822b,
    ROM_LMP_8723d,
    ROM_LMP_8821c,
    ROM_LMP_NONE
};
struct rtk_eversion_evt {
    uint8_t status;
    uint8_t version;
} __attribute__ ((packed));

/*modified by lamparten 1020*/
struct rtk_reset_evt {
    uint8_t status;
} __attribute__ ((packed));
/*modified by lamparten 1020*/

struct rtk_localversion_evt {
    uint8_t status;
    uint8_t hci_version;
    uint16_t hci_revision;
    uint8_t lmp_version;
    uint16_t lmp_manufacture;
    uint16_t lmp_subversion;
} __attribute__ ((packed));

struct rtk_epatch_entry {
    uint16_t chip_id;
    uint16_t patch_length;
    uint32_t start_offset;
    uint32_t coex_version;
    uint32_t svn_version;
    uint32_t fw_version;
} __attribute__ ((packed));

struct rtk_epatch {
    uint8_t signature[8];
    uint32_t fw_version;
    uint16_t number_of_total_patch;
    struct rtk_epatch_entry entry[0];
} __attribute__ ((packed));

struct rtk_extension_entry {
    uint8_t opcode;
    uint8_t length;
    uint8_t *data;
} __attribute__ ((packed));

struct rtk_bt_vendor_config_entry{
    uint16_t offset;
    uint8_t entry_len;
    uint8_t entry_data[0];
} __attribute__ ((packed));

struct rtk_bt_vendor_config{
    uint32_t signature;
    uint16_t data_len;
    struct rtk_bt_vendor_config_entry entry[0];
} __attribute__ ((packed));

/* Realtek - For rtk_btusb driver end */

#if CONFIG_BLUEDROID
#define QUEUE_SIZE 500

/***************************************
** Realtek - Integrate from bluetooth.h **
*****************************************/
/* Reserv for core and drivers use */
#define BT_SKB_RESERVE    8

/* BD Address */
typedef struct {
    __u8 b[6];
} __packed bdaddr_t;

/* Skb helpers */
struct bt_skb_cb {
    __u8 pkt_type;
    __u8 incoming;
    __u16 expect;
    __u16 tx_seq;
    __u8 retries;
    __u8 sar;
    __u8 force_active;
};

#define bt_cb(skb) ((struct bt_skb_cb *)((skb)->cb))

static inline struct sk_buff *bt_skb_alloc(unsigned int len, gfp_t how)
{
    struct sk_buff *skb;

    if ((skb = alloc_skb(len + BT_SKB_RESERVE, how))) {
        skb_reserve(skb, BT_SKB_RESERVE);
        bt_cb(skb)->incoming  = 0;
    }
    return skb;
}
/* Realtek - Integrate from bluetooth.h end */

/***********************************
** Realtek - Integrate from hci.h **
***********************************/
#define HCI_MAX_ACL_SIZE    1024
#define HCI_MAX_SCO_SIZE    255
#define HCI_MAX_EVENT_SIZE    260
#define HCI_MAX_FRAME_SIZE    (HCI_MAX_ACL_SIZE + 4)

/* HCI bus types */
#define HCI_VIRTUAL    0
#define HCI_USB        1
#define HCI_PCCARD    2
#define HCI_UART    3
#define HCI_RS232    4
#define HCI_PCI        5
#define HCI_SDIO    6

/* HCI controller types */
#define HCI_BREDR    0x00
#define HCI_AMP        0x01

/* HCI device flags */
enum {
    HCI_UP,
    HCI_INIT,
    HCI_RUNNING,

    HCI_PSCAN,
    HCI_ISCAN,
    HCI_AUTH,
    HCI_ENCRYPT,
    HCI_INQUIRY,

    HCI_RAW,

    HCI_RESET,
};

/*
 * BR/EDR and/or LE controller flags: the flags defined here should represent
 * states from the controller.
 */
enum {
    HCI_SETUP,
    HCI_AUTO_OFF,
    HCI_MGMT,
    HCI_PAIRABLE,
    HCI_SERVICE_CACHE,
    HCI_LINK_KEYS,
    HCI_DEBUG_KEYS,
    HCI_UNREGISTER,

    HCI_LE_SCAN,
    HCI_SSP_ENABLED,
    HCI_HS_ENABLED,
    HCI_LE_ENABLED,
    HCI_CONNECTABLE,
    HCI_DISCOVERABLE,
    HCI_LINK_SECURITY,
    HCI_PENDING_CLASS,
};

/* HCI data types */
#define HCI_COMMAND_PKT        0x01
#define HCI_ACLDATA_PKT        0x02
#define HCI_SCODATA_PKT        0x03
#define HCI_EVENT_PKT        0x04
#define HCI_VENDOR_PKT        0xff

#define HCI_MAX_NAME_LENGTH        248
#define HCI_MAX_EIR_LENGTH        240

#define HCI_OP_READ_LOCAL_VERSION    0x1001
struct hci_rp_read_local_version {
    __u8     status;
    __u8     hci_ver;
    __le16   hci_rev;
    __u8     lmp_ver;
    __le16   manufacturer;
    __le16   lmp_subver;
} __packed;

#define HCI_EV_CMD_COMPLETE        0x0e
struct hci_ev_cmd_complete {
    __u8     ncmd;
    __le16   opcode;
} __packed;

/* ---- HCI Packet structures ---- */
#define HCI_COMMAND_HDR_SIZE 3
#define HCI_EVENT_HDR_SIZE   2
#define HCI_ACL_HDR_SIZE     4
#define HCI_SCO_HDR_SIZE     3

struct hci_command_hdr {
    __le16    opcode;        /* OCF & OGF */
    __u8    plen;
} __packed;

struct hci_event_hdr {
    __u8    evt;
    __u8    plen;
} __packed;

struct hci_acl_hdr {
    __le16    handle;        /* Handle & Flags(PB, BC) */
    __le16    dlen;
} __packed;

struct hci_sco_hdr {
    __le16    handle;
    __u8    dlen;
} __packed;

static inline struct hci_event_hdr *hci_event_hdr(const struct sk_buff *skb)
{
    return (struct hci_event_hdr *) skb->data;
}

static inline struct hci_acl_hdr *hci_acl_hdr(const struct sk_buff *skb)
{
    return (struct hci_acl_hdr *) skb->data;
}

static inline struct hci_sco_hdr *hci_sco_hdr(const struct sk_buff *skb)
{
    return (struct hci_sco_hdr *) skb->data;
}

/* ---- HCI Ioctl requests structures ---- */
struct hci_dev_stats {
    __u32 err_rx;
    __u32 err_tx;
    __u32 cmd_tx;
    __u32 evt_rx;
    __u32 acl_tx;
    __u32 acl_rx;
    __u32 sco_tx;
    __u32 sco_rx;
    __u32 byte_rx;
    __u32 byte_tx;
};
/* Realtek - Integrate from hci.h end */

/*****************************************
** Realtek - Integrate from hci_core.h  **
*****************************************/
struct hci_conn_hash {
    struct list_head list;
    unsigned int     acl_num;
    unsigned int     sco_num;
    unsigned int     le_num;
};

#define HCI_MAX_SHORT_NAME_LENGTH    10

#define NUM_REASSEMBLY 4
struct hci_dev {
    struct mutex    lock;

    char        name[8];
    unsigned long    flags;
    __u16        id;
    __u8        bus;
    __u8        dev_type;

    struct sk_buff        *reassembly[NUM_REASSEMBLY];

    struct hci_conn_hash    conn_hash;

    struct hci_dev_stats    stat;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 4, 0)
    atomic_t        refcnt;
    struct module           *owner;
    void                    *driver_data;
#endif

    atomic_t        promisc;

    struct device        *parent;
    struct device        dev;

    unsigned long        dev_flags;

    int (*open)(struct hci_dev *hdev);
    int (*close)(struct hci_dev *hdev);
    int (*flush)(struct hci_dev *hdev);
    int (*send)(struct sk_buff *skb);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 4, 0)
    void (*destruct)(struct hci_dev *hdev);
#endif
    __u16               voice_setting;
    void (*notify)(struct hci_dev *hdev, unsigned int evt);
    int (*ioctl)(struct hci_dev *hdev, unsigned int cmd, unsigned long arg);
};

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 4, 0)
static inline struct hci_dev *__hci_dev_hold(struct hci_dev *d)
{
    atomic_inc(&d->refcnt);
    return d;
}

static inline void __hci_dev_put(struct hci_dev *d)
{
    if (atomic_dec_and_test(&d->refcnt))
        d->destruct(d);
}
#endif

static inline void *hci_get_drvdata(struct hci_dev *hdev)
{
    return dev_get_drvdata(&hdev->dev);
}

static inline void hci_set_drvdata(struct hci_dev *hdev, void *data)
{
    dev_set_drvdata(&hdev->dev, data);
}

#define SET_HCIDEV_DEV(hdev, pdev) ((hdev)->parent = (pdev))
/* Realtek - Integrate from hci_core.h end */

/* -----  HCI Commands ---- */
#define HCI_OP_INQUIRY            0x0401
#define HCI_OP_INQUIRY_CANCEL        0x0402
#define HCI_OP_EXIT_PERIODIC_INQ    0x0404
#define HCI_OP_CREATE_CONN        0x0405
#define HCI_OP_DISCONNECT                0x0406
#define HCI_OP_ADD_SCO            0x0407
#define HCI_OP_CREATE_CONN_CANCEL    0x0408
#define HCI_OP_ACCEPT_CONN_REQ        0x0409
#define HCI_OP_REJECT_CONN_REQ        0x040a
#define HCI_OP_LINK_KEY_REPLY        0x040b
#define HCI_OP_LINK_KEY_NEG_REPLY    0x040c
#define HCI_OP_PIN_CODE_REPLY        0x040d
#define HCI_OP_PIN_CODE_NEG_REPLY    0x040e
#define HCI_OP_CHANGE_CONN_PTYPE    0x040f
#define HCI_OP_AUTH_REQUESTED        0x0411
#define HCI_OP_SET_CONN_ENCRYPT        0x0413
#define HCI_OP_CHANGE_CONN_LINK_KEY    0x0415
#define HCI_OP_REMOTE_NAME_REQ        0x0419
#define HCI_OP_REMOTE_NAME_REQ_CANCEL    0x041a
#define HCI_OP_READ_REMOTE_FEATURES    0x041b
#define HCI_OP_READ_REMOTE_EXT_FEATURES    0x041c
#define HCI_OP_READ_REMOTE_VERSION    0x041d
#define HCI_OP_SETUP_SYNC_CONN        0x0428
#define HCI_OP_ACCEPT_SYNC_CONN_REQ    0x0429
#define HCI_OP_REJECT_SYNC_CONN_REQ    0x042a
#define HCI_OP_SNIFF_MODE        0x0803
#define HCI_OP_EXIT_SNIFF_MODE        0x0804
#define HCI_OP_ROLE_DISCOVERY        0x0809
#define HCI_OP_SWITCH_ROLE        0x080b
#define HCI_OP_READ_LINK_POLICY        0x080c
#define HCI_OP_WRITE_LINK_POLICY    0x080d
#define HCI_OP_READ_DEF_LINK_POLICY    0x080e
#define HCI_OP_WRITE_DEF_LINK_POLICY    0x080f
#define HCI_OP_SNIFF_SUBRATE        0x0811
#define HCI_OP_SET_EVENT_MASK        0x0c01
#define HCI_OP_RESET            0x0c03
#define HCI_OP_SET_EVENT_FLT        0x0c05
#define HCI_OP_Write_Extended_Inquiry_Response        0x0c52

/* -----  HCI events---- */
#define HCI_OP_DISCONNECT        0x0406
#define HCI_EV_INQUIRY_COMPLETE        0x01
#define HCI_EV_INQUIRY_RESULT        0x02
#define HCI_EV_CONN_COMPLETE        0x03
#define HCI_EV_CONN_REQUEST            0x04
#define HCI_EV_DISCONN_COMPLETE        0x05
#define HCI_EV_AUTH_COMPLETE        0x06
#define HCI_EV_REMOTE_NAME            0x07
#define HCI_EV_ENCRYPT_CHANGE        0x08
#define HCI_EV_CHANGE_LINK_KEY_COMPLETE    0x09

#define HCI_EV_REMOTE_FEATURES        0x0b
#define HCI_EV_REMOTE_VERSION        0x0c
#define HCI_EV_QOS_SETUP_COMPLETE    0x0d
#define HCI_EV_CMD_COMPLETE            0x0e
#define HCI_EV_CMD_STATUS            0x0f

#define HCI_EV_ROLE_CHANGE            0x12
#define HCI_EV_NUM_COMP_PKTS        0x13
#define HCI_EV_MODE_CHANGE            0x14
#define HCI_EV_PIN_CODE_REQ            0x16
#define HCI_EV_LINK_KEY_REQ            0x17
#define HCI_EV_LINK_KEY_NOTIFY        0x18
#define HCI_EV_CLOCK_OFFSET            0x1c
#define HCI_EV_PKT_TYPE_CHANGE        0x1d
#define HCI_EV_PSCAN_REP_MODE        0x20

#define HCI_EV_INQUIRY_RESULT_WITH_RSSI    0x22
#define HCI_EV_REMOTE_EXT_FEATURES    0x23
#define HCI_EV_SYNC_CONN_COMPLETE    0x2c
#define HCI_EV_SYNC_CONN_CHANGED    0x2d
#define HCI_EV_SNIFF_SUBRATE            0x2e
#define HCI_EV_EXTENDED_INQUIRY_RESULT    0x2f
#define HCI_EV_IO_CAPA_REQUEST        0x31
#define HCI_EV_SIMPLE_PAIR_COMPLETE    0x36
#define HCI_EV_REMOTE_HOST_FEATURES    0x3d

#define CONFIG_MAC_OFFSET_GEN_1_2       (0x3C)      //MAC's OFFSET in config/efuse for realtek generation 1~2 bluetooth chip
#define CONFIG_MAC_OFFSET_GEN_3PLUS     (0x44)      //MAC's OFFSET in config/efuse for rtk generation 3+ bluetooth chip

/*******************************
**    Reasil patch code
********************************/
#define CMD_CMP_EVT        0x0e
#define PKT_LEN            300
#define MSG_TO            1000
#define PATCH_SEG_MAX    252
#define DATA_END        0x80
#define DOWNLOAD_OPCODE    0xfc20
#define BTOFF_OPCODE    0xfc28
#define TRUE            1
#define FALSE            0
#define CMD_HDR_LEN        sizeof(struct hci_command_hdr)
#define EVT_HDR_LEN        sizeof(struct hci_event_hdr)
#define CMD_CMP_LEN        sizeof(struct hci_ev_cmd_complete)
#define MAX_PATCH_SIZE_24K (1024*24)
#define MAX_PATCH_SIZE_40K (1024*40)

enum rtk_endpoit {
    CTRL_EP = 0,
    INTR_EP = 1,
    BULK_EP = 2,
    ISOC_EP = 3
};

typedef struct {
    uint16_t    vid;
    uint16_t    pid;
    uint16_t    lmp_sub_default;
    uint16_t    lmp_sub;
    uint16_t    eversion;
    char        *mp_patch_name;
    char        *patch_name;
    char        *config_name;
    uint8_t     *fw_cache;
    int         fw_len;
    uint16_t    mac_offset;
    uint32_t    max_patch_size;
} patch_info;

typedef struct {
    struct usb_interface    *intf;
    struct usb_device        *udev;
    patch_info *patch_entry;
    int            pipe_in, pipe_out;
    uint8_t        *send_pkt;
    uint8_t        *rcv_pkt;
    struct hci_command_hdr        *cmd_hdr;
    struct hci_event_hdr        *evt_hdr;
    struct hci_ev_cmd_complete    *cmd_cmp;
    uint8_t        *req_para,    *rsp_para;
    uint8_t        *fw_data;
    int            pkt_len;
    int            fw_len;
} firmware_info;

typedef struct {
    uint8_t index;
    uint8_t data[PATCH_SEG_MAX];
} __attribute__((packed)) download_cp;

typedef struct {
    uint8_t status;
    uint8_t index;
} __attribute__((packed)) download_rp;



//Define ioctl cmd the same as HCIDEVUP in the kernel
#define DOWN_FW_CFG  _IOW('H', 201, int)
#ifdef CONFIG_SCO_OVER_HCI
#define SET_ISO_CFG  _IOW('H', 202, int)
#endif
#define GET_USB_INFO            _IOW('H', 203, int)
#define RESET_CONTROLLER        _IOW('H', 204, int)

/*  for altsettings*/
#include <linux/fs.h>
#define BDADDR_FILE "/data/misc/bluetooth/bdaddr"
#define FACTORY_BT_BDADDR_STORAGE_LEN 17

static inline int getmacaddr(uint8_t * vnd_local_bd_addr)
{
    struct file  *bdaddr_file;
    mm_segment_t oldfs;
    char buf[FACTORY_BT_BDADDR_STORAGE_LEN];
    int32_t i = 0;
    int ret = -1;
    memset(buf, 0, FACTORY_BT_BDADDR_STORAGE_LEN);
    bdaddr_file = filp_open(BDADDR_FILE, O_RDONLY, 0);
    if (IS_ERR(bdaddr_file)){
        RTKBT_INFO("No Mac Config for BT\n");
        return -1;
    }
    oldfs = get_fs(); set_fs(KERNEL_DS);
    bdaddr_file->f_op->llseek(bdaddr_file, 0, 0);
    ret = vfs_read(bdaddr_file, buf, FACTORY_BT_BDADDR_STORAGE_LEN, &bdaddr_file->f_pos);
    set_fs(oldfs);
    filp_close(bdaddr_file, NULL);
    if(ret == FACTORY_BT_BDADDR_STORAGE_LEN)
    {
        for (i = 0; i < 6; i++) {
            if(buf[3*i]>'9')
            {
                if(buf[3*i]>'Z')
                    buf[3*i] -=('a'-'A'); //change  a to A
                buf[3*i] -= ('A'-'9'-1);
            }
            if(buf[3*i+1]>'9')
            {
                if(buf[3*i+1]>'Z')
                    buf[3*i+1] -=('a'-'A'); //change  a to A
                buf[3*i+1] -= ('A'-'9'-1);
            }
            vnd_local_bd_addr[5-i] = ((uint8_t)buf[3*i]-'0')*16 + ((uint8_t)buf[3*i+1]-'0');
        }
        return 0;
    }
    return -1;
}

static inline int getAltSettings(patch_info *patch_entry, unsigned short *offset, int max_group_cnt)
{
    int n = 0;
    if(patch_entry)
        offset[n++] = patch_entry->mac_offset;
/*
//sample code, add special settings

    offset[n++] = 0x15B;
*/
    return n;
}
static inline int getAltSettingVal(patch_info *patch_entry, unsigned short offset, unsigned char * val)
{
    int res = 0;

    switch(offset)
    {
/*
//sample code, add special settings
        case 0x15B:
            val[0] = 0x0B;
            val[1] = 0x0B;
            val[2] = 0x0B;
            val[3] = 0x0B;
            res = 4;
            break;
*/
        default:
            res = 0;
            break;
    }

    if((patch_entry)&&(offset == patch_entry->mac_offset)&&(res == 0))
    {
        if(getmacaddr(val) == 0){
            RTKBT_INFO("MAC: %02x:%02x:%02x:%02x:%02x:%02x", val[5], val[4], val[3], val[2], val[1], val[0]);
            res = 6;
        }
    }
    return res;
}

#endif /* CONFIG_BLUEDROID */
