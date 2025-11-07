/* SPDX-License-Identifier: GPL-2.0 */
/**
 * xhci-dbgcap.h - xHCI debug capability support
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */
#ifndef __LINUX_XHCI_DBGCAP_H
#define __LINUX_XHCI_DBGCAP_H

#include <linux/tty.h>
#include <linux/kfifo.h>

struct dbc_regs {
	__le32	capability;
	__le32	doorbell;
	__le32	ersts;		/* Event Ring Segment Table Size*/
	__le32	__reserved_0;	/* 0c~0f reserved bits */
	__le64	erstba;		/* Event Ring Segment Table Base Address */
	__le64	erdp;		/* Event Ring Dequeue Pointer */
	__le32	control;
	__le32	status;
	__le32	portsc;		/* Port status and control */
	__le32	__reserved_1;	/* 2b~28 reserved bits */
	__le64	dccp;		/* Debug Capability Context Pointer */
	__le32	devinfo1;	/* Device Descriptor Info Register 1 */
	__le32	devinfo2;	/* Device Descriptor Info Register 2 */
};

struct dbc_info_context {
	__le64	string0;
	__le64	manufacturer;
	__le64	product;
	__le64	serial;
	__le32	length;
	__le32	__reserved_0[7];
};

#define DBC_CTRL_DBC_RUN		BIT(0)
#define DBC_CTRL_PORT_ENABLE		BIT(1)
#define DBC_CTRL_HALT_OUT_TR		BIT(2)
#define DBC_CTRL_HALT_IN_TR		BIT(3)
#define DBC_CTRL_DBC_RUN_CHANGE		BIT(4)
#define DBC_CTRL_DBC_ENABLE		BIT(31)
#define DBC_CTRL_MAXBURST(p)		(((p) >> 16) & 0xff)
#define DBC_DOOR_BELL_TARGET(p)		(((p) & 0xff) << 8)

#define DBC_MAX_PACKET			1024
#define DBC_MAX_STRING_LENGTH		64
#define DBC_STRING_MANUFACTURER		"Linux Foundation"
#define DBC_STRING_PRODUCT		"Linux USB Debug Target"
#define DBC_STRING_SERIAL		"0001"
#define	DBC_CONTEXT_SIZE		64

/*
 * Port status:
 */
#define DBC_PORTSC_CONN_STATUS		BIT(0)
#define DBC_PORTSC_PORT_ENABLED		BIT(1)
#define DBC_PORTSC_CONN_CHANGE		BIT(17)
#define DBC_PORTSC_RESET_CHANGE		BIT(21)
#define DBC_PORTSC_LINK_CHANGE		BIT(22)
#define DBC_PORTSC_CONFIG_CHANGE	BIT(23)

struct dbc_str_descs {
	char	string0[DBC_MAX_STRING_LENGTH];
	char	manufacturer[DBC_MAX_STRING_LENGTH];
	char	product[DBC_MAX_STRING_LENGTH];
	char	serial[DBC_MAX_STRING_LENGTH];
};

#define DBC_PROTOCOL			1	/* GNU Remote Debug Command */
#define DBC_VENDOR_ID			0x1d6b	/* Linux Foundation 0x1d6b */
#define DBC_PRODUCT_ID			0x0010	/* device 0010 */
#define DBC_DEVICE_REV			0x0010	/* 0.10 */

enum dbc_state {
	DS_DISABLED = 0,
	DS_INITIALIZED,
	DS_ENABLED,
	DS_CONNECTED,
	DS_CONFIGURED,
	DS_MAX
};

struct dbc_ep {
	struct xhci_dbc			*dbc;
	struct list_head		list_pending;
	struct xhci_ring		*ring;
	unsigned int			direction:1;
	unsigned int			halted:1;
};

#define DBC_QUEUE_SIZE			16
#define DBC_WRITE_BUF_SIZE		8192
#define DBC_POLL_INTERVAL_DEFAULT	64	/* milliseconds */
#define DBC_POLL_INTERVAL_MAX		5000	/* milliseconds */
#define DBC_XFER_INACTIVITY_TIMEOUT	10	/* milliseconds */
/*
 * Private structure for DbC hardware state:
 */
struct dbc_port {
	struct tty_port			port;
	spinlock_t			port_lock;	/* port access */
	int				minor;

	struct list_head		read_pool;
	struct list_head		read_queue;
	unsigned int			n_read;
	struct tasklet_struct		push;

	struct list_head		write_pool;
	unsigned int			tx_boundary;

	bool				registered;
	bool				tx_running;
};

struct dbc_driver {
	int (*configure)(struct xhci_dbc *dbc);
	void (*disconnect)(struct xhci_dbc *dbc);
};

struct xhci_dbc {
	spinlock_t			lock;		/* device access */
	struct device			*dev;
	struct xhci_hcd			*xhci;
	struct dbc_regs __iomem		*regs;
	struct xhci_ring		*ring_evt;
	struct xhci_ring		*ring_in;
	struct xhci_ring		*ring_out;
	struct xhci_erst		erst;
	struct xhci_container_ctx	*ctx;

	struct dbc_str_descs		*string;
	dma_addr_t			string_dma;
	size_t				string_size;
	u16				idVendor;
	u16				idProduct;
	u16				bcdDevice;
	u8				bInterfaceProtocol;

	enum dbc_state			state;
	struct delayed_work		event_work;
	unsigned int			poll_interval;	/* ms */
	unsigned long			xfer_timestamp;
	unsigned			resume_required:1;
	struct dbc_ep			eps[2];

	const struct dbc_driver		*driver;
	void				*priv;
};

struct dbc_request {
	void				*buf;
	unsigned int			length;
	dma_addr_t			dma;
	void				(*complete)(struct xhci_dbc *dbc,
						    struct dbc_request *req);
	struct list_head		list_pool;
	int				status;
	unsigned int			actual;

	struct xhci_dbc			*dbc;
	struct list_head		list_pending;
	dma_addr_t			trb_dma;
	union xhci_trb			*trb;
	unsigned			direction:1;
};

#define dbc_bulkout_ctx(d)		\
	((struct xhci_ep_ctx *)((d)->ctx->bytes + DBC_CONTEXT_SIZE))
#define dbc_bulkin_ctx(d)		\
	((struct xhci_ep_ctx *)((d)->ctx->bytes + DBC_CONTEXT_SIZE * 2))
#define dbc_bulkout_enq(d)		\
	xhci_trb_virt_to_dma((d)->ring_out->enq_seg, (d)->ring_out->enqueue)
#define dbc_bulkin_enq(d)		\
	xhci_trb_virt_to_dma((d)->ring_in->enq_seg, (d)->ring_in->enqueue)
#define dbc_epctx_info2(t, p, b)	\
	cpu_to_le32(EP_TYPE(t) | MAX_PACKET(p) | MAX_BURST(b))
#define dbc_ep_dma_direction(d)		\
	((d)->direction ? DMA_FROM_DEVICE : DMA_TO_DEVICE)

#define BULK_OUT			0
#define BULK_IN				1
#define EPID_OUT			2
#define EPID_IN				3

enum evtreturn {
	EVT_ERR	= -1,
	EVT_DONE,
	EVT_XFER_DONE,
	EVT_GSER,
	EVT_DISC,
};

static inline struct dbc_ep *get_in_ep(struct xhci_dbc *dbc)
{
	return &dbc->eps[BULK_IN];
}

static inline struct dbc_ep *get_out_ep(struct xhci_dbc *dbc)
{
	return &dbc->eps[BULK_OUT];
}

#ifdef CONFIG_USB_XHCI_DBGCAP
int xhci_create_dbc_dev(struct xhci_hcd *xhci);
void xhci_remove_dbc_dev(struct xhci_hcd *xhci);
int xhci_dbc_init(void);
void xhci_dbc_exit(void);
int dbc_tty_init(void);
void dbc_tty_exit(void);
int xhci_dbc_tty_probe(struct device *dev, void __iomem *res, struct xhci_hcd *xhci);
void xhci_dbc_tty_remove(struct xhci_dbc *dbc);
struct xhci_dbc *xhci_alloc_dbc(struct device *dev, void __iomem *res,
				 const struct dbc_driver *driver);
void xhci_dbc_remove(struct xhci_dbc *dbc);
struct dbc_request *dbc_alloc_request(struct xhci_dbc *dbc,
				      unsigned int direction,
				      gfp_t flags);
void dbc_free_request(struct dbc_request *req);
int dbc_ep_queue(struct dbc_request *req);
#ifdef CONFIG_PM
int xhci_dbc_suspend(struct xhci_hcd *xhci);
int xhci_dbc_resume(struct xhci_hcd *xhci);
#endif /* CONFIG_PM */
#else
static inline int xhci_create_dbc_dev(struct xhci_hcd *xhci)
{
	return 0;
}

static inline void xhci_remove_dbc_dev(struct xhci_hcd *xhci)
{
}
static inline int xhci_dbc_init(void)
{
	return 0;
}
static inline void xhci_dbc_exit(void)
{
}
static inline int xhci_dbc_suspend(struct xhci_hcd *xhci)
{
	return 0;
}

static inline int xhci_dbc_resume(struct xhci_hcd *xhci)
{
	return 0;
}
#endif /* CONFIG_USB_XHCI_DBGCAP */
#endif /* __LINUX_XHCI_DBGCAP_H */
