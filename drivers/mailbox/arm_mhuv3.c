// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Message Handling Unit Version 3 (MHUv3) driver.
 *
 * Copyright (C) 2024 ARM Ltd.
 *
 * Based on ARM MHUv2 driver.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/types.h>

/* ====== MHUv3 Registers ====== */

/* Maximum number of Doorbell channel windows */
#define MHUV3_DBCW_MAX			128
/* Number of DBCH combined interrupt status registers */
#define MHUV3_DBCH_CMB_INT_ST_REG_CNT	4

/* Number of FFCH combined interrupt status registers */
#define MHUV3_FFCH_CMB_INT_ST_REG_CNT	2

#define MHUV3_FLAG_BITS			32

/* Not a typo ... */
#define MHUV3_MAJOR_VERSION		2

enum {
	MHUV3_MBOX_CELL_TYPE,
	MHUV3_MBOX_CELL_CHWN,
	MHUV3_MBOX_CELL_PARAM,
	MHUV3_MBOX_CELLS
};

/* Padding bitfields/fields represents hole in the regs MMIO */

/* CTRL_Page */
struct blk_id {
#define id		GENMASK(3, 0)
	u32 val;
} __packed;

struct feat_spt0 {
#define	dbe_spt		GENMASK(3, 0)
#define	fe_spt		GENMASK(7, 4)
#define	fce_spt		GENMASK(11, 8)
	u32 val;
} __packed;

struct feat_spt1 {
#define	auto_op_spt	GENMASK(3, 0)
	u32 val;
} __packed;

struct dbch_cfg0 {
#define num_dbch	GENMASK(7, 0)
	u32 val;
} __packed;

struct ffch_cfg0 {
#define num_ffch	GENMASK(7, 0)
#define x8ba_spt	BIT(8)
#define x16ba_spt	BIT(9)
#define x32ba_spt	BIT(10)
#define x64ba_spt	BIT(11)
#define ffch_depth	GENMASK(25, 16)
	u32 val;
} __packed;

struct fch_cfg0 {
#define num_fch		GENMASK(9, 0)
#define fcgi_spt	BIT(10)		// MBX-only
#define num_fcg		GENMASK(15, 11)
#define num_fch_per_grp	GENMASK(20, 16)
#define fch_ws		GENMASK(28, 21)
	u32 val;
} __packed;

struct ctrl {
#define op_req		BIT(0)
#define	ch_op_mask	BIT(1)
	u32 val;
} __packed;

struct fch_ctrl {
#define _int_en		BIT(2)
	u32 val;
} __packed;

struct iidr {
#define implementer	GENMASK(11, 0)
#define revision	GENMASK(15, 12)
#define variant		GENMASK(19, 16)
#define product_id	GENMASK(31, 20)
	u32 val;
} __packed;

struct aidr {
#define arch_minor_rev	GENMASK(3, 0)
#define arch_major_rev	GENMASK(7, 4)
	u32 val;
} __packed;

struct ctrl_page {
	struct blk_id blk_id;
	u8 pad[12];
	struct feat_spt0 feat_spt0;
	struct feat_spt1 feat_spt1;
	u8 pad1[8];
	struct dbch_cfg0 dbch_cfg0;
	u8 pad2[12];
	struct ffch_cfg0 ffch_cfg0;
	u8 pad3[12];
	struct fch_cfg0 fch_cfg0;
	u8 pad4[188];
	struct ctrl x_ctrl;
	/*-- MBX-only registers --*/
	u8 pad5[60];
	struct fch_ctrl fch_ctrl;
	u32 fcg_int_en;
	u8 pad6[696];
	/*-- End of MBX-only ---- */
	u32 dbch_int_st[MHUV3_DBCH_CMB_INT_ST_REG_CNT];
	u32 ffch_int_st[MHUV3_FFCH_CMB_INT_ST_REG_CNT];
	/*-- MBX-only registers --*/
	u8 pad7[88];
	u32 fcg_int_st;
	u8 pad8[12];
	u32 fcg_grp_int_st[32];
	u8 pad9[2760];
	/*-- End of MBX-only ---- */
	struct iidr iidr;
	struct aidr aidr;
	u32 imp_def_id[12];
} __packed;

/* DBCW_Page */

struct xbcw_ctrl {
#define comb_en		BIT(0)
	u32 val;
} __packed;

struct pdbcw_int {
#define tfr_ack		BIT(0)
	u32 val;
} __packed;

struct pdbcw_page {
	u32 st;
	u8 pad[8];
	u32 set;
	struct pdbcw_int int_st;
	struct pdbcw_int int_clr;
	struct pdbcw_int int_en;
	struct xbcw_ctrl ctrl;
} __packed;

struct mdbcw_page {
	u32 st;
	u32 st_msk;
	u32 clr;
	u8 pad[4];
	u32 msk_st;
	u32 msk_set;
	u32 msk_clr;
	struct xbcw_ctrl ctrl;
} __packed;

struct dummy_page {
	u8 pad[SZ_4K];
} __packed;

struct mhu3_pbx_frame_reg {
	struct ctrl_page ctrl;
	struct pdbcw_page dbcw[MHUV3_DBCW_MAX];
	struct dummy_page ffcw;
	struct dummy_page fcw;
	u8 pad[SZ_4K * 11];
	struct dummy_page impdef;
} __packed;

struct mhu3_mbx_frame_reg {
	struct ctrl_page ctrl;
	struct mdbcw_page dbcw[MHUV3_DBCW_MAX];
	struct dummy_page ffcw;
	struct dummy_page fcw;
	u8 pad[SZ_4K * 11];
	struct dummy_page impdef;
} __packed;

/* Macro for reading a bitmask within a physically mapped packed struct */
#define readl_relaxed_bitmask(_regptr, _bitmask)			\
	({								\
		unsigned long _rval;					\
		_rval = readl_relaxed(_regptr);				\
		FIELD_GET(_bitmask, _rval);				\
	})

/* Macro for writing a bitmask within a physically mapped packed struct */
#define writel_relaxed_bitmask(_value, _regptr, _bitmask)		\
	({								\
		unsigned long _rval;					\
		typeof(_regptr) _rptr = _regptr;			\
		typeof(_bitmask) _bmask = _bitmask;			\
		_rval = readl_relaxed(_rptr);				\
		_rval &= ~(_bmask);					\
		_rval |= FIELD_PREP((unsigned long long)_bmask, _value);\
		writel_relaxed(_rval, _rptr);				\
	})

/* ====== MHUv3 data structures ====== */

enum mhuv3_frame {
	PBX_FRAME,
	MBX_FRAME,
};

static char *mhuv3_str[] = {
	"PBX",
	"MBX"
};

enum mhuv3_extension_type {
	DBE_EXT,
	FCE_EXT,
	FE_EXT,
	NUM_EXT
};

static char *mhuv3_ext_str[] = {
	"DBE",
	"FCE",
	"FE"
};

struct mhuv3;

/**
 * struct mhuv3_protocol_ops - MHUv3 operations
 *
 * @rx_startup: Receiver startup callback.
 * @rx_shutdown: Receiver shutdown callback.
 * @read_data: Read available Sender in-band LE data (if any).
 * @rx_complete: Acknowledge data reception to the Sender. Any out-of-band data
 *		 has to have been already retrieved before calling this.
 * @tx_startup: Sender startup callback.
 * @tx_shutdown: Sender shutdown callback.
 * @last_tx_done: Report back to the Sender if the last transfer has completed.
 * @send_data: Send data to the receiver.
 *
 * Each supported transport protocol provides its own implementation of
 * these operations.
 */
struct mhuv3_protocol_ops {
	int (*rx_startup)(struct mhuv3 *mhu, struct mbox_chan *chan);
	void (*rx_shutdown)(struct mhuv3 *mhu, struct mbox_chan *chan);
	void *(*read_data)(struct mhuv3 *mhu, struct mbox_chan *chan);
	void (*rx_complete)(struct mhuv3 *mhu, struct mbox_chan *chan);
	void (*tx_startup)(struct mhuv3 *mhu, struct mbox_chan *chan);
	void (*tx_shutdown)(struct mhuv3 *mhu, struct mbox_chan *chan);
	int (*last_tx_done)(struct mhuv3 *mhu, struct mbox_chan *chan);
	int (*send_data)(struct mhuv3 *mhu, struct mbox_chan *chan, void *arg);
};

/**
 * struct mhuv3_mbox_chan_priv - MHUv3 channel private information
 *
 * @ch_idx: Channel window index associated to this mailbox channel.
 * @doorbell: Doorbell bit number within the @ch_idx window.
 *	      Only relevant to Doorbell transport.
 * @ops: Transport protocol specific operations for this channel.
 *
 * Transport specific data attached to mmailbox channel priv data.
 */
struct mhuv3_mbox_chan_priv {
	u32 ch_idx;
	u32 doorbell;
	const struct mhuv3_protocol_ops *ops;
};

/**
 * struct mhuv3_extension - MHUv3 extension descriptor
 *
 * @type: Type of extension
 * @num_chans: Max number of channels found for this extension.
 * @base_ch_idx: First channel number assigned to this extension, picked from
 *		 the set of all mailbox channels descriptors created.
 * @mbox_of_xlate: Extension specific helper to parse DT and lookup associated
 *		   channel from the related 'mboxes' property.
 * @combined_irq_setup: Extension specific helper to setup the combined irq.
 * @channels_init: Extension specific helper to initialize channels.
 * @chan_from_comb_irq_get: Extension specific helper to lookup which channel
 *			    triggered the combined irq.
 * @pending_db: Array of per-channel pending doorbells.
 * @pending_lock: Protect access to pending_db.
 */
struct mhuv3_extension {
	enum mhuv3_extension_type type;
	unsigned int num_chans;
	unsigned int base_ch_idx;
	struct mbox_chan *(*mbox_of_xlate)(struct mhuv3 *mhu,
					   unsigned int channel,
					   unsigned int param);
	void (*combined_irq_setup)(struct mhuv3 *mhu);
	int (*channels_init)(struct mhuv3 *mhu);
	struct mbox_chan *(*chan_from_comb_irq_get)(struct mhuv3 *mhu);
	u32 pending_db[MHUV3_DBCW_MAX];
	/* Protect access to pending_db */
	spinlock_t pending_lock;
};

/**
 * struct mhuv3 - MHUv3 mailbox controller data
 *
 * @frame:	Frame type: MBX_FRAME or PBX_FRAME.
 * @auto_op_full: Flag to indicate if the MHU supports AutoOp full mode.
 * @major: MHUv3 controller architectural major version.
 * @minor: MHUv3 controller architectural minor version.
 * @implem: MHUv3 controller IIDR implementer.
 * @rev: MHUv3 controller IIDR revision.
 * @var: MHUv3 controller IIDR variant.
 * @prod_id: MHUv3 controller IIDR product_id.
 * @num_chans: The total number of channnels discovered across all extensions.
 * @cmb_irq: Combined IRQ number if any found defined.
 * @ctrl: A reference to the MHUv3 control page for this block.
 * @pbx: Base address of the PBX register mapping region.
 * @mbx: Base address of the MBX register mapping region.
 * @ext: Array holding descriptors for any found implemented extension.
 * @mbox: Mailbox controller belonging to the MHU frame.
 */
struct mhuv3 {
	enum mhuv3_frame frame;
	bool auto_op_full;
	unsigned int major;
	unsigned int minor;
	unsigned int implem;
	unsigned int rev;
	unsigned int var;
	unsigned int prod_id;
	unsigned int num_chans;
	int cmb_irq;
	struct ctrl_page __iomem *ctrl;
	union {
		struct mhu3_pbx_frame_reg __iomem *pbx;
		struct mhu3_mbx_frame_reg __iomem *mbx;
	};
	struct mhuv3_extension *ext[NUM_EXT];
	struct mbox_controller mbox;
};

#define mhu_from_mbox(_mbox) container_of(_mbox, struct mhuv3, mbox)

typedef int (*mhuv3_extension_initializer)(struct mhuv3 *mhu);

/* =================== Doorbell transport protocol operations =============== */

static void mhuv3_doorbell_tx_startup(struct mhuv3 *mhu, struct mbox_chan *chan)
{
	struct mhuv3_mbox_chan_priv *priv = chan->con_priv;

	/* Enable Transfer Acknowledgment events */
	writel_relaxed_bitmask(0x1, &mhu->pbx->dbcw[priv->ch_idx].int_en, tfr_ack);
}

static void mhuv3_doorbell_tx_shutdown(struct mhuv3 *mhu, struct mbox_chan *chan)
{
	struct mhuv3_mbox_chan_priv *priv = chan->con_priv;
	struct mhuv3_extension *e = mhu->ext[DBE_EXT];
	unsigned long flags;

	/* Disable Channel Transfer Ack events */
	writel_relaxed_bitmask(0x0, &mhu->pbx->dbcw[priv->ch_idx].int_en, tfr_ack);

	/* Clear Channel Transfer Ack and pending doorbells */
	writel_relaxed_bitmask(0x1, &mhu->pbx->dbcw[priv->ch_idx].int_clr, tfr_ack);
	spin_lock_irqsave(&e->pending_lock, flags);
	e->pending_db[priv->ch_idx] = 0;
	spin_unlock_irqrestore(&e->pending_lock, flags);
}

static int mhuv3_doorbell_rx_startup(struct mhuv3 *mhu, struct mbox_chan *chan)
{
	struct mhuv3_mbox_chan_priv *priv = chan->con_priv;

	/* Unmask Channel Transfer events */
	writel_relaxed(BIT(priv->doorbell), &mhu->mbx->dbcw[priv->ch_idx].msk_clr);

	return 0;
}

static void mhuv3_doorbell_rx_shutdown(struct mhuv3 *mhu,
				       struct mbox_chan *chan)
{
	struct mhuv3_mbox_chan_priv *priv = chan->con_priv;

	/* Mask Channel Transfer events */
	writel_relaxed(BIT(priv->doorbell), &mhu->mbx->dbcw[priv->ch_idx].msk_set);
}

static void mhuv3_doorbell_rx_complete(struct mhuv3 *mhu, struct mbox_chan *chan)
{
	struct mhuv3_mbox_chan_priv *priv = chan->con_priv;

	/* Clearing the pending transfer generates the Channel Transfer Ack */
	writel_relaxed(BIT(priv->doorbell), &mhu->mbx->dbcw[priv->ch_idx].clr);
}

static int mhuv3_doorbell_last_tx_done(struct mhuv3 *mhu,
				       struct mbox_chan *chan)
{
	struct mhuv3_mbox_chan_priv *priv = chan->con_priv;
	int done;

	done = !(readl_relaxed(&mhu->pbx->dbcw[priv->ch_idx].st) &
		 BIT(priv->doorbell));
	if (done) {
		struct mhuv3_extension *e = mhu->ext[DBE_EXT];
		unsigned long flags;

		/* Take care to clear the pending doorbell also when polling */
		spin_lock_irqsave(&e->pending_lock, flags);
		e->pending_db[priv->ch_idx] &= ~BIT(priv->doorbell);
		spin_unlock_irqrestore(&e->pending_lock, flags);
	}

	return done;
}

static int mhuv3_doorbell_send_data(struct mhuv3 *mhu, struct mbox_chan *chan,
				    void *arg)
{
	struct mhuv3_mbox_chan_priv *priv = chan->con_priv;
	struct mhuv3_extension *e = mhu->ext[DBE_EXT];

	scoped_guard(spinlock_irqsave, &e->pending_lock) {
		/* Only one in-flight Transfer is allowed per-doorbell */
		if (e->pending_db[priv->ch_idx] & BIT(priv->doorbell))
			return -EBUSY;

		e->pending_db[priv->ch_idx] |= BIT(priv->doorbell);
	}

	writel_relaxed(BIT(priv->doorbell), &mhu->pbx->dbcw[priv->ch_idx].set);

	return 0;
}

static const struct mhuv3_protocol_ops mhuv3_doorbell_ops = {
	.tx_startup = mhuv3_doorbell_tx_startup,
	.tx_shutdown = mhuv3_doorbell_tx_shutdown,
	.rx_startup = mhuv3_doorbell_rx_startup,
	.rx_shutdown = mhuv3_doorbell_rx_shutdown,
	.rx_complete = mhuv3_doorbell_rx_complete,
	.last_tx_done = mhuv3_doorbell_last_tx_done,
	.send_data = mhuv3_doorbell_send_data,
};

/* Sender and receiver mailbox ops */
static bool mhuv3_sender_last_tx_done(struct mbox_chan *chan)
{
	struct mhuv3_mbox_chan_priv *priv = chan->con_priv;
	struct mhuv3 *mhu = mhu_from_mbox(chan->mbox);

	return priv->ops->last_tx_done(mhu, chan);
}

static int mhuv3_sender_send_data(struct mbox_chan *chan, void *data)
{
	struct mhuv3_mbox_chan_priv *priv = chan->con_priv;
	struct mhuv3 *mhu = mhu_from_mbox(chan->mbox);

	if (!priv->ops->last_tx_done(mhu, chan))
		return -EBUSY;

	return priv->ops->send_data(mhu, chan, data);
}

static int mhuv3_sender_startup(struct mbox_chan *chan)
{
	struct mhuv3_mbox_chan_priv *priv = chan->con_priv;
	struct mhuv3 *mhu = mhu_from_mbox(chan->mbox);

	if (priv->ops->tx_startup)
		priv->ops->tx_startup(mhu, chan);

	return 0;
}

static void mhuv3_sender_shutdown(struct mbox_chan *chan)
{
	struct mhuv3_mbox_chan_priv *priv = chan->con_priv;
	struct mhuv3 *mhu = mhu_from_mbox(chan->mbox);

	if (priv->ops->tx_shutdown)
		priv->ops->tx_shutdown(mhu, chan);
}

static const struct mbox_chan_ops mhuv3_sender_ops = {
	.send_data = mhuv3_sender_send_data,
	.startup = mhuv3_sender_startup,
	.shutdown = mhuv3_sender_shutdown,
	.last_tx_done = mhuv3_sender_last_tx_done,
};

static int mhuv3_receiver_startup(struct mbox_chan *chan)
{
	struct mhuv3_mbox_chan_priv *priv = chan->con_priv;
	struct mhuv3 *mhu = mhu_from_mbox(chan->mbox);

	return priv->ops->rx_startup(mhu, chan);
}

static void mhuv3_receiver_shutdown(struct mbox_chan *chan)
{
	struct mhuv3_mbox_chan_priv *priv = chan->con_priv;
	struct mhuv3 *mhu = mhu_from_mbox(chan->mbox);

	priv->ops->rx_shutdown(mhu, chan);
}

static int mhuv3_receiver_send_data(struct mbox_chan *chan, void *data)
{
	dev_err(chan->mbox->dev,
		"Trying to transmit on a MBX MHUv3 frame\n");
	return -EIO;
}

static bool mhuv3_receiver_last_tx_done(struct mbox_chan *chan)
{
	dev_err(chan->mbox->dev, "Trying to Tx poll on a MBX MHUv3 frame\n");
	return true;
}

static const struct mbox_chan_ops mhuv3_receiver_ops = {
	.send_data = mhuv3_receiver_send_data,
	.startup = mhuv3_receiver_startup,
	.shutdown = mhuv3_receiver_shutdown,
	.last_tx_done = mhuv3_receiver_last_tx_done,
};

static struct mbox_chan *mhuv3_dbe_mbox_of_xlate(struct mhuv3 *mhu,
						 unsigned int channel,
						 unsigned int doorbell)
{
	struct mhuv3_extension *e = mhu->ext[DBE_EXT];
	struct mbox_controller *mbox = &mhu->mbox;
	struct mbox_chan *chans = mbox->chans;

	if (channel >= e->num_chans || doorbell >= MHUV3_FLAG_BITS) {
		dev_err(mbox->dev, "Couldn't xlate to a valid channel (%d: %d)\n",
			channel, doorbell);
		return ERR_PTR(-ENODEV);
	}

	return &chans[e->base_ch_idx + channel * MHUV3_FLAG_BITS + doorbell];
}

static void mhuv3_dbe_combined_irq_setup(struct mhuv3 *mhu)
{
	struct mhuv3_extension *e = mhu->ext[DBE_EXT];
	int i;

	if (mhu->frame == PBX_FRAME) {
		struct pdbcw_page __iomem *dbcw = mhu->pbx->dbcw;

		for (i = 0; i < e->num_chans; i++) {
			writel_relaxed_bitmask(0x1, &dbcw[i].int_clr, tfr_ack);
			writel_relaxed_bitmask(0x0, &dbcw[i].int_en, tfr_ack);
			writel_relaxed_bitmask(0x1, &dbcw[i].ctrl, comb_en);
		}
	} else {
		struct mdbcw_page __iomem *dbcw = mhu->mbx->dbcw;

		for (i = 0; i < e->num_chans; i++) {
			writel_relaxed(0xFFFFFFFF, &dbcw[i].clr);
			writel_relaxed(0xFFFFFFFF, &dbcw[i].msk_set);
			writel_relaxed_bitmask(0x1, &dbcw[i].ctrl, comb_en);
		}
	}
}

static int mhuv3_dbe_channels_init(struct mhuv3 *mhu)
{
	struct mhuv3_extension *e = mhu->ext[DBE_EXT];
	struct mbox_controller *mbox = &mhu->mbox;
	struct mbox_chan *chans;
	int i;

	chans = mbox->chans + mbox->num_chans;
	e->base_ch_idx = mbox->num_chans;
	for (i = 0; i < e->num_chans; i++) {
		struct mhuv3_mbox_chan_priv *priv;
		int k;

		for (k = 0; k < MHUV3_FLAG_BITS; k++) {
			priv = devm_kmalloc(mbox->dev, sizeof(*priv), GFP_KERNEL);
			if (!priv)
				return -ENOMEM;

			priv->ch_idx = i;
			priv->ops = &mhuv3_doorbell_ops;
			priv->doorbell = k;
			chans++->con_priv = priv;
			mbox->num_chans++;
		}
	}

	spin_lock_init(&e->pending_lock);

	return 0;
}

static bool mhuv3_dbe_doorbell_lookup(struct mhuv3 *mhu, unsigned int channel,
				      unsigned int *db)
{
	struct mhuv3_extension *e = mhu->ext[DBE_EXT];
	struct device *dev = mhu->mbox.dev;
	u32 st;

	if (mhu->frame == PBX_FRAME) {
		u32 active_dbs, fired_dbs;

		st = readl_relaxed_bitmask(&mhu->pbx->dbcw[channel].int_st,
					   tfr_ack);
		if (!st)
			goto err_spurious;

		active_dbs = readl_relaxed(&mhu->pbx->dbcw[channel].st);
		scoped_guard(spinlock_irqsave, &e->pending_lock) {
			fired_dbs = e->pending_db[channel] & ~active_dbs;
			if (!fired_dbs)
				goto err_spurious;

			*db = __ffs(fired_dbs);
			e->pending_db[channel] &= ~BIT(*db);
		}
		fired_dbs &= ~BIT(*db);
		/* Clear TFR Ack if no more doorbells pending */
		if (!fired_dbs)
			writel_relaxed_bitmask(0x1,
					       &mhu->pbx->dbcw[channel].int_clr,
					       tfr_ack);
	} else {
		st = readl_relaxed(&mhu->mbx->dbcw[channel].st_msk);
		if (!st)
			goto err_spurious;

		*db = __ffs(st);
	}

	return true;

err_spurious:
	dev_warn(dev, "Spurious IRQ on %s channel:%d\n",
		 mhuv3_str[mhu->frame], channel);

	return false;
}

static struct mbox_chan *mhuv3_dbe_chan_from_comb_irq_get(struct mhuv3 *mhu)
{
	struct mhuv3_extension *e = mhu->ext[DBE_EXT];
	struct device *dev = mhu->mbox.dev;
	int i;

	for (i = 0; i < MHUV3_DBCH_CMB_INT_ST_REG_CNT; i++) {
		unsigned int channel, db;
		u32 cmb_st;

		cmb_st = readl_relaxed(&mhu->ctrl->dbch_int_st[i]);
		if (!cmb_st)
			continue;

		channel = i * MHUV3_FLAG_BITS + __ffs(cmb_st);
		if (channel >= e->num_chans) {
			dev_err(dev, "Invalid %s channel:%d\n",
				mhuv3_str[mhu->frame], channel);
			return ERR_PTR(-EIO);
		}

		if (!mhuv3_dbe_doorbell_lookup(mhu, channel, &db))
			continue;

		dev_dbg(dev, "Found %s ch[%d]/db[%d]\n",
			mhuv3_str[mhu->frame], channel, db);

		return &mhu->mbox.chans[channel * MHUV3_FLAG_BITS + db];
	}

	return ERR_PTR(-EIO);
}

static int mhuv3_dbe_init(struct mhuv3 *mhu)
{
	struct device *dev = mhu->mbox.dev;
	struct mhuv3_extension *e;

	if (!readl_relaxed_bitmask(&mhu->ctrl->feat_spt0, dbe_spt))
		return 0;

	dev_dbg(dev, "%s: Initializing DBE Extension.\n", mhuv3_str[mhu->frame]);

	e = devm_kzalloc(dev, sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	e->type = DBE_EXT;
	/* Note that, by the spec, the number of channels is (num_dbch + 1) */
	e->num_chans =
		readl_relaxed_bitmask(&mhu->ctrl->dbch_cfg0, num_dbch) + 1;
	e->mbox_of_xlate = mhuv3_dbe_mbox_of_xlate;
	e->combined_irq_setup = mhuv3_dbe_combined_irq_setup;
	e->channels_init = mhuv3_dbe_channels_init;
	e->chan_from_comb_irq_get = mhuv3_dbe_chan_from_comb_irq_get;

	mhu->num_chans += e->num_chans * MHUV3_FLAG_BITS;
	mhu->ext[DBE_EXT] = e;

	dev_dbg(dev, "%s: found %d DBE channels.\n",
		mhuv3_str[mhu->frame], e->num_chans);

	return 0;
}

static int mhuv3_fce_init(struct mhuv3 *mhu)
{
	struct device *dev = mhu->mbox.dev;

	if (!readl_relaxed_bitmask(&mhu->ctrl->feat_spt0, fce_spt))
		return 0;

	dev_dbg(dev, "%s: FCE Extension not supported by driver.\n",
		mhuv3_str[mhu->frame]);

	return 0;
}

static int mhuv3_fe_init(struct mhuv3 *mhu)
{
	struct device *dev = mhu->mbox.dev;

	if (!readl_relaxed_bitmask(&mhu->ctrl->feat_spt0, fe_spt))
		return 0;

	dev_dbg(dev, "%s: FE Extension not supported by driver.\n",
		mhuv3_str[mhu->frame]);

	return 0;
}

static mhuv3_extension_initializer mhuv3_extension_init[NUM_EXT] = {
	mhuv3_dbe_init,
	mhuv3_fce_init,
	mhuv3_fe_init,
};

static int mhuv3_initialize_channels(struct device *dev, struct mhuv3 *mhu)
{
	struct mbox_controller *mbox = &mhu->mbox;
	int i, ret = 0;

	mbox->chans = devm_kcalloc(dev, mhu->num_chans,
				   sizeof(*mbox->chans), GFP_KERNEL);
	if (!mbox->chans)
		return dev_err_probe(dev, -ENOMEM,
				     "Failed to initialize channels\n");

	for (i = 0; i < NUM_EXT && !ret; i++)
		if (mhu->ext[i])
			ret = mhu->ext[i]->channels_init(mhu);

	return ret;
}

static struct mbox_chan *mhuv3_mbox_of_xlate(struct mbox_controller *mbox,
					     const struct of_phandle_args *pa)
{
	struct mhuv3 *mhu = mhu_from_mbox(mbox);
	unsigned int type, channel, param;

	if (pa->args_count != MHUV3_MBOX_CELLS)
		return ERR_PTR(-EINVAL);

	type = pa->args[MHUV3_MBOX_CELL_TYPE];
	if (type >= NUM_EXT)
		return ERR_PTR(-EINVAL);

	channel = pa->args[MHUV3_MBOX_CELL_CHWN];
	param = pa->args[MHUV3_MBOX_CELL_PARAM];

	return mhu->ext[type]->mbox_of_xlate(mhu, channel, param);
}

static void mhu_frame_cleanup_actions(void *data)
{
	struct mhuv3 *mhu = data;

	writel_relaxed_bitmask(0x0, &mhu->ctrl->x_ctrl, op_req);
}

static int mhuv3_frame_init(struct mhuv3 *mhu, void __iomem *regs)
{
	struct device *dev = mhu->mbox.dev;
	int i;

	mhu->ctrl = regs;
	mhu->frame = readl_relaxed_bitmask(&mhu->ctrl->blk_id, id);
	if (mhu->frame > MBX_FRAME)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid Frame type- %d\n", mhu->frame);

	mhu->major = readl_relaxed_bitmask(&mhu->ctrl->aidr, arch_major_rev);
	mhu->minor = readl_relaxed_bitmask(&mhu->ctrl->aidr, arch_minor_rev);
	mhu->implem = readl_relaxed_bitmask(&mhu->ctrl->iidr, implementer);
	mhu->rev = readl_relaxed_bitmask(&mhu->ctrl->iidr, revision);
	mhu->var = readl_relaxed_bitmask(&mhu->ctrl->iidr, variant);
	mhu->prod_id = readl_relaxed_bitmask(&mhu->ctrl->iidr, product_id);
	if (mhu->major != MHUV3_MAJOR_VERSION)
		return dev_err_probe(dev, -EINVAL,
				     "Unsupported MHU %s block - major:%d  minor:%d\n",
				     mhuv3_str[mhu->frame], mhu->major,
				     mhu->minor);

	mhu->auto_op_full =
		!!readl_relaxed_bitmask(&mhu->ctrl->feat_spt1, auto_op_spt);
	/* Request the PBX/MBX to remain operational */
	if (mhu->auto_op_full) {
		writel_relaxed_bitmask(0x1, &mhu->ctrl->x_ctrl, op_req);
		devm_add_action_or_reset(dev, mhu_frame_cleanup_actions, mhu);
	}

	dev_dbg(dev,
		"Found MHU %s block - major:%d  minor:%d\n  implem:0x%X  rev:0x%X  var:0x%X  prod_id:0x%X",
		mhuv3_str[mhu->frame], mhu->major, mhu->minor,
		mhu->implem, mhu->rev, mhu->var, mhu->prod_id);

	if (mhu->frame == PBX_FRAME)
		mhu->pbx = regs;
	else
		mhu->mbx = regs;

	for (i = 0; i < NUM_EXT; i++) {
		int ret;

		/*
		 * Note that extensions initialization fails only when such
		 * extension initialization routine fails and the extensions
		 * was found to be supported in hardware and in software.
		 */
		ret = mhuv3_extension_init[i](mhu);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to initialize %s %s\n",
					     mhuv3_str[mhu->frame],
					     mhuv3_ext_str[i]);
	}

	return 0;
}

static irqreturn_t mhuv3_pbx_comb_interrupt(int irq, void *arg)
{
	unsigned int i, found = 0;
	struct mhuv3 *mhu = arg;
	struct mbox_chan *chan;
	struct device *dev;
	int ret = IRQ_NONE;

	dev = mhu->mbox.dev;
	for (i = 0; i < NUM_EXT; i++) {
		struct mhuv3_mbox_chan_priv *priv;

		/* FCE does not participate to the PBX combined */
		if (i == FCE_EXT || !mhu->ext[i])
			continue;

		chan = mhu->ext[i]->chan_from_comb_irq_get(mhu);
		if (IS_ERR(chan))
			continue;

		found++;
		priv = chan->con_priv;
		if (!chan->cl) {
			dev_warn(dev, "TX Ack on UNBOUND channel (%u)\n",
				 priv->ch_idx);
			continue;
		}

		mbox_chan_txdone(chan, 0);
		ret = IRQ_HANDLED;
	}

	if (found == 0)
		dev_warn_once(dev, "Failed to find channel for the TX interrupt\n");

	return ret;
}

static irqreturn_t mhuv3_mbx_comb_interrupt(int irq, void *arg)
{
	unsigned int i, found = 0;
	struct mhuv3 *mhu = arg;
	struct mbox_chan *chan;
	struct device *dev;
	int ret = IRQ_NONE;

	dev = mhu->mbox.dev;
	for (i = 0; i < NUM_EXT; i++) {
		struct mhuv3_mbox_chan_priv *priv;
		void *data __free(kfree) = NULL;

		if (!mhu->ext[i])
			continue;

		/* Process any extension which could be source of the IRQ */
		chan = mhu->ext[i]->chan_from_comb_irq_get(mhu);
		if (IS_ERR(chan))
			continue;

		found++;
		/* From here on we need to call rx_complete even on error */
		priv = chan->con_priv;
		if (!chan->cl) {
			dev_warn(dev, "RX Data on UNBOUND channel (%u)\n",
				 priv->ch_idx);
			goto rx_ack;
		}

		/* Read optional in-band LE data first. */
		if (priv->ops->read_data) {
			data = priv->ops->read_data(mhu, chan);
			if (IS_ERR(data)) {
				dev_err(dev,
					"Failed to read in-band data. err:%ld\n",
					PTR_ERR(data));
				goto rx_ack;
			}
		}

		mbox_chan_received_data(chan, data);
		ret = IRQ_HANDLED;

		/*
		 * Acknowledge transfer after any possible optional
		 * out-of-band data has also been retrieved via
		 * mbox_chan_received_data().
		 */
rx_ack:
		if (priv->ops->rx_complete)
			priv->ops->rx_complete(mhu, chan);
	}

	if (found == 0)
		dev_warn_once(dev, "Failed to find channel for the RX interrupt\n");

	return ret;
}

static int mhuv3_setup_pbx(struct mhuv3 *mhu)
{
	struct device *dev = mhu->mbox.dev;

	mhu->mbox.ops = &mhuv3_sender_ops;

	if (mhu->cmb_irq > 0) {
		int ret, i;

		ret = devm_request_threaded_irq(dev, mhu->cmb_irq, NULL,
						mhuv3_pbx_comb_interrupt,
						IRQF_ONESHOT, "mhuv3-pbx", mhu);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to request PBX IRQ\n");

		mhu->mbox.txdone_irq = true;
		mhu->mbox.txdone_poll = false;

		for (i = 0; i < NUM_EXT; i++)
			if (mhu->ext[i])
				mhu->ext[i]->combined_irq_setup(mhu);

		dev_dbg(dev, "MHUv3 PBX IRQs initialized.\n");

		return 0;
	}

	dev_info(dev, "Using PBX in Tx polling mode.\n");
	mhu->mbox.txdone_irq = false;
	mhu->mbox.txdone_poll = true;
	mhu->mbox.txpoll_period = 1;

	return 0;
}

static int mhuv3_setup_mbx(struct mhuv3 *mhu)
{
	struct device *dev = mhu->mbox.dev;
	int ret, i;

	mhu->mbox.ops = &mhuv3_receiver_ops;

	if (mhu->cmb_irq <= 0)
		return dev_err_probe(dev, -EINVAL,
				     "MBX combined IRQ is missing !\n");

	ret = devm_request_threaded_irq(dev, mhu->cmb_irq, NULL,
					mhuv3_mbx_comb_interrupt, IRQF_ONESHOT,
					"mhuv3-mbx", mhu);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request MBX IRQ\n");

	for (i = 0; i < NUM_EXT; i++)
		if (mhu->ext[i])
			mhu->ext[i]->combined_irq_setup(mhu);

	dev_dbg(dev, "MHUv3 MBX IRQs initialized.\n");

	return ret;
}

static int mhuv3_irqs_init(struct mhuv3 *mhu, struct platform_device *pdev)
{
	dev_dbg(mhu->mbox.dev, "Initializing %s block.\n",
		mhuv3_str[mhu->frame]);

	if (mhu->frame == PBX_FRAME) {
		mhu->cmb_irq =
			platform_get_irq_byname_optional(pdev, "combined");
		return mhuv3_setup_pbx(mhu);
	}

	mhu->cmb_irq = platform_get_irq_byname(pdev, "combined");
	return mhuv3_setup_mbx(mhu);
}

static int mhuv3_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *regs;
	struct mhuv3 *mhu;
	int ret;

	mhu = devm_kzalloc(dev, sizeof(*mhu), GFP_KERNEL);
	if (!mhu)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	mhu->mbox.dev = dev;
	ret = mhuv3_frame_init(mhu, regs);
	if (ret)
		return ret;

	ret = mhuv3_irqs_init(mhu, pdev);
	if (ret)
		return ret;

	mhu->mbox.of_xlate = mhuv3_mbox_of_xlate;
	ret = mhuv3_initialize_channels(dev, mhu);
	if (ret)
		return ret;

	ret = devm_mbox_controller_register(dev, &mhu->mbox);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register ARM MHUv3 driver\n");

	return ret;
}

static const struct of_device_id mhuv3_of_match[] = {
	{ .compatible = "arm,mhuv3", .data = NULL },
	{}
};
MODULE_DEVICE_TABLE(of, mhuv3_of_match);

static struct platform_driver mhuv3_driver = {
	.driver = {
		.name = "arm-mhuv3-mailbox",
		.of_match_table = mhuv3_of_match,
	},
	.probe = mhuv3_probe,
};
module_platform_driver(mhuv3_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ARM MHUv3 Driver");
MODULE_AUTHOR("Cristian Marussi <cristian.marussi@arm.com>");
