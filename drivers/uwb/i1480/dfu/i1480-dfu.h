/*
 * i1480 Device Firmware Upload
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * This driver is the firmware uploader for the Intel Wireless UWB
 * Link 1480 device (both in the USB and PCI incarnations).
 *
 * The process is quite simple: we stop the device, write the firmware
 * to its memory and then restart it. Wait for the device to let us
 * know it is done booting firmware. Ready.
 *
 * We might have to upload before or after a phy firmware (which might
 * be done in two methods, using a normal firmware image or through
 * the MPI port).
 *
 * Because USB and PCI use common methods, we just make ops out of the
 * common operations (read, write, wait_init_done and cmd) and
 * implement them in usb.c and pci.c.
 *
 * The flow is (some parts omitted):
 *
 * i1480_{usb,pci}_probe()	  On enumerate/discovery
 *   i1480_fw_upload()
 *     i1480_pre_fw_upload()
 *       __mac_fw_upload()
 *         fw_hdrs_load()
 *         mac_fw_hdrs_push()
 *           i1480->write()       [i1480_{usb,pci}_write()]
 *           i1480_fw_cmp()
 *             i1480->read()      [i1480_{usb,pci}_read()]
 *     i1480_mac_fw_upload()
 *       __mac_fw_upload()
 *       i1480->setup(()
 *       i1480->wait_init_done()
 *       i1480_cmd_reset()
 *         i1480->cmd()           [i1480_{usb,pci}_cmd()]
 *         ...
 *     i1480_phy_fw_upload()
 *       request_firmware()
 *       i1480_mpi_write()
 *         i1480->cmd()           [i1480_{usb,pci}_cmd()]
 *
 * Once the probe function enumerates the device and uploads the
 * firmware, we just exit with -ENODEV, as we don't really want to
 * attach to the device.
 */
#ifndef __i1480_DFU_H__
#define __i1480_DFU_H__

#include <linux/uwb/spec.h>
#include <linux/types.h>
#include <linux/completion.h>

#define i1480_FW_UPLOAD_MODE_MASK (cpu_to_le32(0x00000018))

#if i1480_FW > 0x00000302
#define i1480_RCEB_EXTENDED
#endif

struct uwb_rccb;
struct uwb_rceb;

/*
 * Common firmware upload handlers
 *
 * Normally you embed this struct in another one specific to your hw.
 *
 * @write	Write to device's memory from buffer.
 * @read	Read from device's memory to i1480->evt_buf.
 * @setup	Setup device after basic firmware is uploaded
 * @wait_init_done
 *              Wait for the device to send a notification saying init
 *              is done.
 * @cmd         FOP for issuing the command to the hardware. The
 *              command data is contained in i1480->cmd_buf and the size
 *              is supplied as an argument. The command replied is put
 *              in i1480->evt_buf and the size in i1480->evt_result (or if
 *              an error, a < 0 errno code).
 *
 * @cmd_buf	Memory buffer used to send commands to the device.
 *              Allocated by the upper layers i1480_fw_upload().
 *              Size has to be @buf_size.
 * @evt_buf	Memory buffer used to place the async notifications
 *              received by the hw. Allocated by the upper layers
 *              i1480_fw_upload().
 *              Size has to be @buf_size.
 * @cmd_complete
 *              Low level driver uses this to notify code waiting afor
 *              an event that the event has arrived and data is in
 *              i1480->evt_buf (and size/result in i1480->evt_result).
 * @hw_rev
 *              Use this value to activate dfu code to support new revisions
 *              of hardware.  i1480_init() sets this to a default value.
 *              It should be updated by the USB and PCI code.
 */
struct i1480 {
	struct device *dev;

	int (*write)(struct i1480 *, u32 addr, const void *, size_t);
	int (*read)(struct i1480 *, u32 addr, size_t);
	int (*rc_setup)(struct i1480 *);
	void (*rc_release)(struct i1480 *);
	int (*wait_init_done)(struct i1480 *);
	int (*cmd)(struct i1480 *, const char *cmd_name, size_t cmd_size);
	const char *pre_fw_name;
	const char *mac_fw_name;
	const char *mac_fw_name_deprecate;	/* FIXME: Will go away */
	const char *phy_fw_name;
	u8 hw_rev;

	size_t buf_size;	/* size of both evt_buf and cmd_buf */
	void *evt_buf, *cmd_buf;
	ssize_t evt_result;
	struct completion evt_complete;
};

static inline
void i1480_init(struct i1480 *i1480)
{
	i1480->hw_rev = 1;
	init_completion(&i1480->evt_complete);
}

extern int i1480_fw_upload(struct i1480 *);
extern int i1480_pre_fw_upload(struct i1480 *);
extern int i1480_mac_fw_upload(struct i1480 *);
extern int i1480_phy_fw_upload(struct i1480 *);
extern ssize_t i1480_cmd(struct i1480 *, const char *, size_t, size_t);
extern int i1480_rceb_check(const struct i1480 *,
			    const struct uwb_rceb *, const char *, u8,
			    unsigned, unsigned);

enum {
	/* Vendor specific command type */
	i1480_CET_VS1 = 		0xfd,
	/* i1480 commands */
	i1480_CMD_SET_IP_MAS = 		0x000e,
	i1480_CMD_GET_MAC_PHY_INFO = 	0x0003,
	i1480_CMD_MPI_WRITE =		0x000f,
	i1480_CMD_MPI_READ = 		0x0010,
	/* i1480 events */
#if i1480_FW > 0x00000302
	i1480_EVT_CONFIRM = 		0x0002,
	i1480_EVT_RM_INIT_DONE = 	0x0101,
	i1480_EVT_DEV_ADD = 		0x0103,
	i1480_EVT_DEV_RM = 		0x0104,
	i1480_EVT_DEV_ID_CHANGE = 	0x0105,
	i1480_EVT_GET_MAC_PHY_INFO =	i1480_CMD_GET_MAC_PHY_INFO,
#else
	i1480_EVT_CONFIRM = 		0x0002,
	i1480_EVT_RM_INIT_DONE = 	0x0101,
	i1480_EVT_DEV_ADD = 		0x0103,
	i1480_EVT_DEV_RM = 		0x0104,
	i1480_EVT_DEV_ID_CHANGE = 	0x0105,
	i1480_EVT_GET_MAC_PHY_INFO =	i1480_EVT_CONFIRM,
#endif
};


struct i1480_evt_confirm {
	struct uwb_rceb rceb;
#ifdef i1480_RCEB_EXTENDED
	__le16 wParamLength;
#endif
	u8 bResultCode;
} __attribute__((packed));


struct i1480_rceb {
	struct uwb_rceb rceb;
#ifdef i1480_RCEB_EXTENDED
	__le16 wParamLength;
#endif
} __attribute__((packed));


/**
 * Get MAC & PHY Information confirm event structure
 *
 * Confirm event returned by the command.
 */
struct i1480_evt_confirm_GMPI {
#if i1480_FW > 0x00000302
	struct uwb_rceb rceb;
	__le16 wParamLength;
	__le16 status;
	u8 mac_addr[6];		/* EUI-64 bit IEEE address [still 8 bytes?] */
	u8 dev_addr[2];
	__le16 mac_fw_rev;	/* major = v >> 8; minor = v & 0xff */
	u8 hw_rev;
	u8 phy_vendor;
	u8 phy_rev;		/* major v = >> 8; minor = v & 0xff */
	__le16 mac_caps;
	u8 phy_caps[3];
	u8 key_stores;
	__le16 mcast_addr_stores;
	u8 sec_mode_supported;
#else
	struct uwb_rceb rceb;
	u8 status;
	u8 mac_addr[8];         /* EUI-64 bit IEEE address [still 8 bytes?] */
	u8 dev_addr[2];
	__le16 mac_fw_rev;      /* major = v >> 8; minor = v & 0xff */
	__le16 phy_fw_rev;      /* major v = >> 8; minor = v & 0xff */
	__le16 mac_caps;
	u8 phy_caps;
	u8 key_stores;
	__le16 mcast_addr_stores;
	u8 sec_mode_supported;
#endif
} __attribute__((packed));


struct i1480_cmd_mpi_write {
	struct uwb_rccb rccb;
	__le16 size;
	u8 data[];
};


struct i1480_cmd_mpi_read {
	struct uwb_rccb rccb;
	__le16 size;
	struct {
		u8 page, offset;
	} __attribute__((packed)) data[];
} __attribute__((packed));


struct i1480_evt_mpi_read {
	struct uwb_rceb rceb;
#ifdef i1480_RCEB_EXTENDED
	__le16 wParamLength;
#endif
	u8 bResultCode;
	__le16 size;
	struct {
		u8 page, offset, value;
	} __attribute__((packed)) data[];
} __attribute__((packed));


#endif /* #ifndef __i1480_DFU_H__ */
