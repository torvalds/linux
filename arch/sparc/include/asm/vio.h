#ifndef _SPARC64_VIO_H
#define _SPARC64_VIO_H

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/log2.h>

#include <asm/ldc.h>
#include <asm/mdesc.h>

struct vio_msg_tag {
	u8			type;
#define VIO_TYPE_CTRL		0x01
#define VIO_TYPE_DATA		0x02
#define VIO_TYPE_ERR		0x04

	u8			stype;
#define VIO_SUBTYPE_INFO	0x01
#define VIO_SUBTYPE_ACK		0x02
#define VIO_SUBTYPE_NACK	0x04

	u16			stype_env;
#define VIO_VER_INFO		0x0001
#define VIO_ATTR_INFO		0x0002
#define VIO_DRING_REG		0x0003
#define VIO_DRING_UNREG		0x0004
#define VIO_RDX			0x0005
#define VIO_PKT_DATA		0x0040
#define VIO_DESC_DATA		0x0041
#define VIO_DRING_DATA		0x0042
#define VNET_MCAST_INFO		0x0101

	u32		sid;
};

struct vio_rdx {
	struct vio_msg_tag	tag;
	u64			resv[6];
};

struct vio_ver_info {
	struct vio_msg_tag	tag;
	u16			major;
	u16			minor;
	u8			dev_class;
#define VDEV_NETWORK		0x01
#define VDEV_NETWORK_SWITCH	0x02
#define VDEV_DISK		0x03
#define VDEV_DISK_SERVER	0x04

	u8			resv1[3];
	u64			resv2[5];
};

struct vio_dring_register {
	struct vio_msg_tag	tag;
	u64			dring_ident;
	u32			num_descr;
	u32			descr_size;
	u16			options;
#define VIO_TX_DRING		0x0001
#define VIO_RX_DRING		0x0002
	u16			resv;
	u32			num_cookies;
	struct ldc_trans_cookie	cookies[0];
};

struct vio_dring_unregister {
	struct vio_msg_tag	tag;
	u64			dring_ident;
	u64			resv[5];
};

/* Data transfer modes */
#define VIO_PKT_MODE		0x01 /* Packet based transfer	*/
#define VIO_DESC_MODE		0x02 /* In-band descriptors	*/
#define VIO_DRING_MODE		0x03 /* Descriptor rings	*/

struct vio_dring_data {
	struct vio_msg_tag	tag;
	u64			seq;
	u64			dring_ident;
	u32			start_idx;
	u32			end_idx;
	u8			state;
#define VIO_DRING_ACTIVE	0x01
#define VIO_DRING_STOPPED	0x02

	u8			__pad1;
	u16			__pad2;
	u32			__pad3;
	u64			__par4[2];
};

struct vio_dring_hdr {
	u8			state;
#define VIO_DESC_FREE		0x01
#define VIO_DESC_READY		0x02
#define VIO_DESC_ACCEPTED	0x03
#define VIO_DESC_DONE		0x04
	u8			ack;
#define VIO_ACK_ENABLE		0x01
#define VIO_ACK_DISABLE		0x00

	u16			__pad1;
	u32			__pad2;
};

/* VIO disk specific structures and defines */
struct vio_disk_attr_info {
	struct vio_msg_tag	tag;
	u8			xfer_mode;
	u8			vdisk_type;
#define VD_DISK_TYPE_SLICE	0x01 /* Slice in block device	*/
#define VD_DISK_TYPE_DISK	0x02 /* Entire block device	*/
	u16			resv1;
	u32			vdisk_block_size;
	u64			operations;
	u64			vdisk_size;
	u64			max_xfer_size;
	u64			resv2[2];
};

struct vio_disk_desc {
	struct vio_dring_hdr	hdr;
	u64			req_id;
	u8			operation;
#define VD_OP_BREAD		0x01 /* Block read			*/
#define VD_OP_BWRITE		0x02 /* Block write			*/
#define VD_OP_FLUSH		0x03 /* Flush disk contents		*/
#define VD_OP_GET_WCE		0x04 /* Get write-cache status		*/
#define VD_OP_SET_WCE		0x05 /* Enable/disable write-cache	*/
#define VD_OP_GET_VTOC		0x06 /* Get VTOC			*/
#define VD_OP_SET_VTOC		0x07 /* Set VTOC			*/
#define VD_OP_GET_DISKGEOM	0x08 /* Get disk geometry		*/
#define VD_OP_SET_DISKGEOM	0x09 /* Set disk geometry		*/
#define VD_OP_SCSICMD		0x0a /* SCSI control command		*/
#define VD_OP_GET_DEVID		0x0b /* Get device ID			*/
#define VD_OP_GET_EFI		0x0c /* Get EFI				*/
#define VD_OP_SET_EFI		0x0d /* Set EFI				*/
	u8			slice;
	u16			resv1;
	u32			status;
	u64			offset;
	u64			size;
	u32			ncookies;
	u32			resv2;
	struct ldc_trans_cookie	cookies[0];
};

#define VIO_DISK_VNAME_LEN	8
#define VIO_DISK_ALABEL_LEN	128
#define VIO_DISK_NUM_PART	8

struct vio_disk_vtoc {
	u8			volume_name[VIO_DISK_VNAME_LEN];
	u16			sector_size;
	u16			num_partitions;
	u8			ascii_label[VIO_DISK_ALABEL_LEN];
	struct {
		u16		id;
		u16		perm_flags;
		u32		resv;
		u64		start_block;
		u64		num_blocks;
	} partitions[VIO_DISK_NUM_PART];
};

struct vio_disk_geom {
	u16			num_cyl; /* Num data cylinders		*/
	u16			alt_cyl; /* Num alternate cylinders	*/
	u16			beg_cyl; /* Cyl off of fixed head area	*/
	u16			num_hd;  /* Num heads			*/
	u16			num_sec; /* Num sectors			*/
	u16			ifact;   /* Interleave factor		*/
	u16			apc;     /* Alts per cylinder (SCSI)	*/
	u16			rpm;	 /* Revolutions per minute	*/
	u16			phy_cyl; /* Num physical cylinders	*/
	u16			wr_skip; /* Num sects to skip, writes	*/
	u16			rd_skip; /* Num sects to skip, writes	*/
};

struct vio_disk_devid {
	u16			resv;
	u16			type;
	u32			len;
	char			id[0];
};

struct vio_disk_efi {
	u64			lba;
	u64			len;
	char			data[0];
};

/* VIO net specific structures and defines */
struct vio_net_attr_info {
	struct vio_msg_tag	tag;
	u8			xfer_mode;
	u8			addr_type;
#define VNET_ADDR_ETHERMAC	0x01
	u16			ack_freq;
	u32			resv1;
	u64			addr;
	u64			mtu;
	u64			resv2[3];
};

#define VNET_NUM_MCAST		7

struct vio_net_mcast_info {
	struct vio_msg_tag	tag;
	u8			set;
	u8			count;
	u8			mcast_addr[VNET_NUM_MCAST * 6];
	u32			resv;
};

struct vio_net_desc {
	struct vio_dring_hdr	hdr;
	u32			size;
	u32			ncookies;
	struct ldc_trans_cookie	cookies[0];
};

#define VIO_MAX_RING_COOKIES	24

struct vio_dring_state {
	u64			ident;
	void			*base;
	u64			snd_nxt;
	u64			rcv_nxt;
	u32			entry_size;
	u32			num_entries;
	u32			prod;
	u32			cons;
	u32			pending;
	int			ncookies;
	struct ldc_trans_cookie	cookies[VIO_MAX_RING_COOKIES];
};

static inline void *vio_dring_cur(struct vio_dring_state *dr)
{
	return dr->base + (dr->entry_size * dr->prod);
}

static inline void *vio_dring_entry(struct vio_dring_state *dr,
				    unsigned int index)
{
	return dr->base + (dr->entry_size * index);
}

static inline u32 vio_dring_avail(struct vio_dring_state *dr,
				  unsigned int ring_size)
{
	return (dr->pending -
		((dr->prod - dr->cons) & (ring_size - 1)));
}

#define VIO_MAX_TYPE_LEN	32
#define VIO_MAX_COMPAT_LEN	64

struct vio_dev {
	u64			mp;
	struct device_node	*dp;

	char			type[VIO_MAX_TYPE_LEN];
	char			compat[VIO_MAX_COMPAT_LEN];
	int			compat_len;

	u64			dev_no;

	unsigned long		channel_id;

	unsigned int		tx_irq;
	unsigned int		rx_irq;

	struct device		dev;
};

struct vio_driver {
	struct list_head		node;
	const struct vio_device_id	*id_table;
	int (*probe)(struct vio_dev *dev, const struct vio_device_id *id);
	int (*remove)(struct vio_dev *dev);
	void (*shutdown)(struct vio_dev *dev);
	unsigned long			driver_data;
	struct device_driver		driver;
};

struct vio_version {
	u16		major;
	u16		minor;
};

struct vio_driver_state;
struct vio_driver_ops {
	int	(*send_attr)(struct vio_driver_state *vio);
	int	(*handle_attr)(struct vio_driver_state *vio, void *pkt);
	void	(*handshake_complete)(struct vio_driver_state *vio);
};

struct vio_completion {
	struct completion	com;
	int			err;
	int			waiting_for;
};

struct vio_driver_state {
	/* Protects VIO handshake and, optionally, driver private state.  */
	spinlock_t		lock;

	struct ldc_channel	*lp;

	u32			_peer_sid;
	u32			_local_sid;
	struct vio_dring_state	drings[2];
#define VIO_DRIVER_TX_RING	0
#define VIO_DRIVER_RX_RING	1

	u8			hs_state;
#define VIO_HS_INVALID		0x00
#define VIO_HS_GOTVERS		0x01
#define VIO_HS_GOT_ATTR		0x04
#define VIO_HS_SENT_DREG	0x08
#define VIO_HS_SENT_RDX		0x10
#define VIO_HS_GOT_RDX_ACK	0x20
#define VIO_HS_GOT_RDX		0x40
#define VIO_HS_SENT_RDX_ACK	0x80
#define VIO_HS_COMPLETE		(VIO_HS_GOT_RDX_ACK | VIO_HS_SENT_RDX_ACK)

	u8			dev_class;

	u8			dr_state;
#define VIO_DR_STATE_TXREG	0x01
#define VIO_DR_STATE_RXREG	0x02
#define VIO_DR_STATE_TXREQ	0x10
#define VIO_DR_STATE_RXREQ	0x20

	u8			debug;
#define VIO_DEBUG_HS		0x01
#define VIO_DEBUG_DATA		0x02

	void			*desc_buf;
	unsigned int		desc_buf_len;

	struct vio_completion	*cmp;

	struct vio_dev		*vdev;

	struct timer_list	timer;

	struct vio_version	ver;

	struct vio_version	*ver_table;
	int			ver_table_entries;

	char			*name;

	struct vio_driver_ops	*ops;
};

#define viodbg(TYPE, f, a...) \
do {	if (vio->debug & VIO_DEBUG_##TYPE) \
		printk(KERN_INFO "vio: ID[%lu] " f, \
		       vio->vdev->channel_id, ## a); \
} while (0)

extern int vio_register_driver(struct vio_driver *drv);
extern void vio_unregister_driver(struct vio_driver *drv);

static inline struct vio_driver *to_vio_driver(struct device_driver *drv)
{
	return container_of(drv, struct vio_driver, driver);
}

static inline struct vio_dev *to_vio_dev(struct device *dev)
{
	return container_of(dev, struct vio_dev, dev);
}

extern int vio_ldc_send(struct vio_driver_state *vio, void *data, int len);
extern void vio_link_state_change(struct vio_driver_state *vio, int event);
extern void vio_conn_reset(struct vio_driver_state *vio);
extern int vio_control_pkt_engine(struct vio_driver_state *vio, void *pkt);
extern int vio_validate_sid(struct vio_driver_state *vio,
			    struct vio_msg_tag *tp);
extern u32 vio_send_sid(struct vio_driver_state *vio);
extern int vio_ldc_alloc(struct vio_driver_state *vio,
			 struct ldc_channel_config *base_cfg, void *event_arg);
extern void vio_ldc_free(struct vio_driver_state *vio);
extern int vio_driver_init(struct vio_driver_state *vio, struct vio_dev *vdev,
			   u8 dev_class, struct vio_version *ver_table,
			   int ver_table_size, struct vio_driver_ops *ops,
			   char *name);

extern void vio_port_up(struct vio_driver_state *vio);

#endif /* _SPARC64_VIO_H */
