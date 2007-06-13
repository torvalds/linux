#ifndef _LIBERTAS_IF_USB_H
#define _LIBERTAS_IF_USB_H

#include <linux/list.h>

/**
  * This file contains definition for USB interface.
  */
#define CMD_TYPE_REQUEST                0xF00DFACE
#define CMD_TYPE_DATA                   0xBEADC0DE
#define CMD_TYPE_INDICATION             0xBEEFFACE

#define IPFIELD_ALIGN_OFFSET	2

#define BOOT_CMD_FW_BY_USB     0x01
#define BOOT_CMD_FW_IN_EEPROM  0x02
#define BOOT_CMD_UPDATE_BOOT2  0x03
#define BOOT_CMD_UPDATE_FW     0x04
#define BOOT_CMD_MAGIC_NUMBER  0x4C56524D   /* M=>0x4D,R=>0x52,V=>0x56,L=>0x4C */

struct bootcmdstr
{
	__le32 u32magicnumber;
	u8  u8cmd_tag;
	u8  au8dumy[11];
};

#define BOOT_CMD_RESP_OK     0x0001
#define BOOT_CMD_RESP_FAIL   0x0000

struct bootcmdrespStr
{
	__le32 u32magicnumber;
	u8  u8cmd_tag;
	u8  u8result;
	u8  au8dumy[2];
};

/* read callback private data */
struct read_cb_info {
        wlan_private *priv;
        struct sk_buff *skb;
};

/** USB card description structure*/
struct usb_card_rec {
	struct list_head list;
	struct net_device *eth_dev;
	struct usb_device *udev;
	struct urb *rx_urb, *tx_urb;
	void *priv;
	struct read_cb_info rinfo;

	int bulk_in_size;
	u8 bulk_in_endpointAddr;

	u8 *bulk_out_buffer;
	int bulk_out_size;
	u8 bulk_out_endpointAddr;

	u8 CRC_OK;
	u32 fwseqnum;
	u32 lastseqnum;
	u32 totalbytes;
	u32 fwlastblksent;
	u8 fwdnldover;
	u8 fwfinalblk;

	u32 usb_event_cause;
	u8 usb_int_cause;

	u8 rx_urb_recall;

	u8 bootcmdresp;
};

/** fwheader */
struct fwheader {
	__le32 dnldcmd;
	__le32 baseaddr;
	__le32 datalength;
	__le32 CRC;
};

#define FW_MAX_DATA_BLK_SIZE	600
/** FWData */
struct FWData {
	struct fwheader fwheader;
	__le32 seqnum;
	u8 data[FW_MAX_DATA_BLK_SIZE];
};

/** fwsyncheader */
struct fwsyncheader {
	__le32 cmd;
	__le32 seqnum;
};

#define FW_HAS_DATA_TO_RECV		0x00000001
#define FW_HAS_LAST_BLOCK		0x00000004

#define FW_DATA_XMIT_SIZE \
	sizeof(struct fwheader) + le32_to_cpu(fwdata->fwheader.datalength) + sizeof(u32)

int usb_tx_block(wlan_private *priv, u8 *payload, u16 nb);
void if_usb_free(struct usb_card_rec *cardp);
int if_usb_issue_boot_command(wlan_private *priv, int ivalue);

#endif
