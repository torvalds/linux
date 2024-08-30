// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved. */

/* ========================================================================== */
/*                          INCLUDE FILES                                     */
/* ========================================================================== */
#include <linux/compiler.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/virtio.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/phy.h>
#include <soc/qcom/boot_stats.h>
#include <linux/virtio_config.h>
#include <linux/semaphore.h>
#include "stmmac.h"
/* ========================================================================== */
/*                         MACRO DEFINITIONS                                  */
/* ========================================================================== */
#define EMAC_MDIO_FE_ERR(fmt, args...)                            \
	pr_err("[ ERR %s: %d] " fmt, __func__,  __LINE__, ##args)
#define EMAC_MDIO_FE_WARN(fmt, args...)                           \
	pr_warn("[ WARN: %s: %d] " fmt, __func__,  __LINE__, ##args)
#define EMAC_MDIO_FE_INFO(fmt, args...)                           \
	pr_info("[ INFO: %s: %d] " fmt, __func__,  __LINE__, ##args)
#define EMAC_MDIO_FE_DBG(fmt, args...)                           \
	pr_debug("[ DBG: %s: %d] " fmt, __func__,  __LINE__, ##args)

/*Virtio ID of EMAC*/
#define VIRTIO_DT_QCOM_BASE         (49152)
#define VIRTIO_DT_EMAC_MDIO         (VIRTIO_DT_QCOM_BASE + 12)
#define WAIT_PHY_REPLY_MAX_TIMEOUT  (100)
/* ========================================================================== */
/*                         TYPE DECLARATIONS                                  */
/* ========================================================================== */
enum mdio_interface_type_e {
	MDIO_CLAUSE_22,
	MDIO_CLAUSE_45_INDIRECT,
	MDIO_CLAUSE_45_DIRECT,
	MDIO_MAC2MAC_WO_MDIO,
	MDIO_TYPE_TOTAL
};

enum mdio_remote_op_type_e {
	MDIO_REMOTE_OP_TYPE_NULL,
	MDIO_REMOTE_OP_TYPE_READ,
	MDIO_REMOTE_OP_TYPE_WRITE,
	MDIO_REMOTE_OP_TYPE_TOTAL
};

struct phy_remote_access_t {
	enum mdio_interface_type_e mdio_type;
	enum mdio_remote_op_type_e mdio_op_remote_type;
	unsigned char  phyaddr;
	unsigned short phydev;
	unsigned short phyreg;
	unsigned short phydata;
};

enum emac_mdio_fe_state_type {
	EMAC_MDIO_FE_DOWN = 0,
	EMAC_MDIO_FE_UP,
};

struct be_to_fe_msg {
	u16 msgid;        /* unique message id */
	u16 len;          /* command total length */
	u32 cmd;          /* command */
	s32 result;       /* command result */
} __packed;

struct fe_to_be_msg {
	u8  type;         /* unique message id       1byte  */
	u16 len;          /* command total length    2byte  */
	u8  result;       /* command result          1byte  */
	struct phy_remote_access_t request_data;
} __packed;

struct emac_mdio_dev {
	/*device driver properties*/
	char                         *name;

	/*Virtio device*/
	struct virtio_device          *vdev;

	struct virtqueue              *emac_mdio_fe_txq;
	struct virtqueue              *emac_mdio_fe_rxq;

	spinlock_t                     txq_lock; /* Used for transmitting */
	spinlock_t                     rxq_lock; /* Used for receiving */

	struct fe_to_be_msg tx_msg;
	struct be_to_fe_msg rx_msg[10];

	enum emac_mdio_fe_state_type   emac_mdio_fe_state;
	struct semaphore               emac_mdio_fe_sem;
	struct mutex                   emac_mdio_fe_lock; /* Used for mdio ready/write */
	s32                            phy_reply;
};

enum EMAC_MDIO_FE_VIRTQ {
	EMAC_MDIO_FE_TX_VQ     = 0,
	EMAC_MDIO_FE_RX_VQ     = 1,
	EMAC_MDIO_FE_VIRTQ_NUM = 2,
};

enum emac_mdio_fe_to_be_cmds {
	VIRTIO_EMAC_MDIO_FE_DOWN = 0,
	VIRTIO_EMAC_MDIO_FE_UP   = 1,
	VIRTIO_EMAC_MDIO_FE_REQ  = 2,
};

enum emac_mdio_be_to_fe_cmds {
	VIRTIO_EMAC_MDIO_HW_DOWN  = 0,
	VIRTIO_EMAC_MDIO_HW_UP    = 1,
	VIRTIO_EMAC_MDIO_HW_REPLY = 2,
};

/* ========================================================================== */
/*                         FUNCTION DECLARATIONS                              */
/* ========================================================================== */
static int emac_mdio_fe_probe(struct virtio_device *vdev);

/* ========================================================================== */
/*                          VARIABLE DEFINITIONS                              */
/* ========================================================================== */
static struct emac_mdio_dev *emac_mdio_fe_ctx;
static struct emac_mdio_dev *emac_mdio_fe_pdev;

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_DT_EMAC_MDIO, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	/*none*/
};

static struct virtio_driver emac_mdio_fe_virtio_drv = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
	},
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.id_table = id_table,
	.probe = emac_mdio_fe_probe,
};

/* ========================================================================== */
/*                         FUNCTION DEFINITIONS                               */
/* ========================================================================== */
static int __maybe_unused emac_mdio_fe_xmit(struct emac_mdio_dev *pdev)
{
	unsigned long flags;
	struct scatterlist sg[1];
	struct fe_to_be_msg *msg = NULL;
	int retval = 0;

	msg = &pdev->tx_msg;

	/*lock*/
	EMAC_MDIO_FE_DBG("Entry msg len =%d", msg->len);
	sg_init_one(sg, msg, sizeof(*msg));

	spin_lock_irqsave(&pdev->txq_lock, flags);
	/*expose output buffers to other end*/
	retval = virtqueue_add_outbuf(pdev->emac_mdio_fe_txq, sg, 1, msg, GFP_ATOMIC);
	if (retval) {
		spin_unlock_irqrestore(&pdev->txq_lock, flags);
		EMAC_MDIO_FE_ERR("fail to add output buffer\n");
		/* who will free buffer*/
		goto out;
	}
	/*update other side after add_buf*/
	virtqueue_kick(pdev->emac_mdio_fe_txq);
	EMAC_MDIO_FE_DBG("Kicked Host receive Q\n");
	/*unlock*/
	spin_unlock_irqrestore(&pdev->txq_lock, flags);
out:
	return retval;
}

static void emac_mdio_fe_replenish_rxbuf(struct emac_mdio_dev *pdev, struct be_to_fe_msg *msg)
{
	struct scatterlist sg[1];

	EMAC_MDIO_FE_DBG("Entry");
	memset(msg, 0x0, sizeof(*msg));
	sg_init_one(sg, msg, sizeof(*msg));

	/*expose input buffers to other end*/
	virtqueue_add_inbuf(pdev->emac_mdio_fe_rxq, sg, 1, msg, GFP_ATOMIC);
}

static void emac_mdio_fe_update(struct emac_mdio_dev *pdev, struct be_to_fe_msg *msg)
{
	EMAC_MDIO_FE_DBG("Receive msg->cmd= %d", msg->cmd);

	switch (msg->cmd) {
	case VIRTIO_EMAC_MDIO_HW_DOWN:
		EMAC_MDIO_FE_INFO("Notify VIRTIO_EMAC_MDIO_HW_DOWN");
		pdev->emac_mdio_fe_state = EMAC_MDIO_FE_DOWN;
		break;

	case VIRTIO_EMAC_MDIO_HW_UP:
		EMAC_MDIO_FE_INFO("Notify VIRTIO_EMAC_MDIO_HW_UP");
		pdev->emac_mdio_fe_state = EMAC_MDIO_FE_UP;
		break;

	case VIRTIO_EMAC_MDIO_HW_REPLY:
		EMAC_MDIO_FE_DBG("Notify VIRTIO_EMAC_MDIO_HW_REPLY");
		pdev->phy_reply = msg->result;
		up(&pdev->emac_mdio_fe_sem);
		break;

	default:
		EMAC_MDIO_FE_WARN("Received cmd %d not recognized ",  msg->cmd);
		break;
	}
}

/* This is similar to RX complete interrupt. */
/* It seems like single kick but needs to treat as if_start. */
static void emac_mdio_fe_recv_done(struct virtqueue *rvq)
{
	struct emac_mdio_dev *pdev = rvq->vdev->priv;
	struct be_to_fe_msg *msg;
	unsigned long flags;
	unsigned int len;

	EMAC_MDIO_FE_DBG("Entry");
	while (1) {
		spin_lock_irqsave(&pdev->rxq_lock, flags);
		EMAC_MDIO_FE_DBG("Call Virtqueue_get_buff");
		msg = virtqueue_get_buf(pdev->emac_mdio_fe_rxq, &len);
		if (!msg) {
			spin_unlock_irqrestore(&pdev->rxq_lock, flags);
			EMAC_MDIO_FE_DBG("incoming signal, but no used buffer\n");
			break;
		}
		EMAC_MDIO_FE_DBG("Got Buffer len %d ", len);
		spin_unlock_irqrestore(&pdev->rxq_lock, flags);
		/*Process received message, can be stubbed out*/
		emac_mdio_fe_update(pdev, msg);

		/*Reclaim RX buffer*/
		emac_mdio_fe_replenish_rxbuf(pdev, msg);
	} /*while*/

	spin_lock_irqsave(&pdev->rxq_lock, flags);
	virtqueue_kick(pdev->emac_mdio_fe_rxq);
	spin_unlock_irqrestore(&pdev->rxq_lock, flags);
}

static void emac_mdio_fe_xmit_done(struct virtqueue *txq)
{
	struct emac_mdio_dev         *pdev = txq->vdev->priv;
	struct fe_to_be_msg   *msg = NULL;
	unsigned long                           flags = 0;
	unsigned int                            len = 0;

	EMAC_MDIO_FE_DBG("-->");
	while (1) {
		spin_lock_irqsave(&pdev->txq_lock, flags);
		EMAC_MDIO_FE_DBG("Call virtqueue_get_buf");
		msg = virtqueue_get_buf(pdev->emac_mdio_fe_txq, &len);
		spin_unlock_irqrestore(&pdev->txq_lock, flags);
		if (!msg)
			break;
	} /*while*/
	EMAC_MDIO_FE_DBG("<--");
}

static void emac_mdio_fe_allocate_rxbufs(struct emac_mdio_dev *pdev)
{
	unsigned long flags;
	int i, size;

	spin_lock_irqsave(&pdev->rxq_lock, flags);
	size = virtqueue_get_vring_size(pdev->emac_mdio_fe_rxq);
	if (size > ARRAY_SIZE(pdev->rx_msg))
		size = ARRAY_SIZE(pdev->rx_msg);
	for (i = 0; i < size; i++)
		emac_mdio_fe_replenish_rxbuf(pdev, &pdev->rx_msg[i]);

	spin_unlock_irqrestore(&pdev->rxq_lock, flags);
}

static int emac_mdio_fe_init_vqs(struct emac_mdio_dev *pdev)
{
	struct virtqueue *vqs[EMAC_MDIO_FE_VIRTQ_NUM];
	static const char *const names[] = { "emac_mdio_tx", "emac_mdio_rx" };
	vq_callback_t *cbs[] = {emac_mdio_fe_xmit_done, emac_mdio_fe_recv_done};
	int ret;

	/* Find VirtQueues and Register callback*/
	ret = virtio_find_vqs(pdev->vdev, EMAC_MDIO_FE_VIRTQ_NUM, vqs, cbs, names, NULL);
	if (ret) {
		EMAC_MDIO_FE_ERR("virtio_find_vqs failed\n");
		return ret;
	}

	EMAC_MDIO_FE_INFO("VirtQ Callback Reg Complete\n");

	/*Initialize TX VQ*/
	spin_lock_init(&pdev->txq_lock);
	pdev->emac_mdio_fe_txq = vqs[EMAC_MDIO_FE_TX_VQ];

	/*Initialized RX VQ*/
	spin_lock_init(&pdev->rxq_lock);
	pdev->emac_mdio_fe_rxq = vqs[EMAC_MDIO_FE_RX_VQ];

	EMAC_MDIO_FE_INFO("VirtQ Init Complete\n");
	return 0;
}

int virtio_mdio_read(struct mii_bus *bus, int addr, int regnum)
{
	struct phy_remote_access_t *phy_request = NULL;
	unsigned long tmp;
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);

	mutex_lock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
	phy_request = &emac_mdio_fe_ctx->tx_msg.request_data;
	memset(phy_request, 0, sizeof(*phy_request));
	phy_request->mdio_type = MDIO_CLAUSE_22;
	phy_request->mdio_op_remote_type = MDIO_REMOTE_OP_TYPE_READ;
	phy_request->phyaddr = addr;
	phy_request->phyreg = regnum;

	emac_mdio_fe_ctx->tx_msg.type = VIRTIO_EMAC_MDIO_FE_REQ;
	emac_mdio_fe_ctx->tx_msg.len = sizeof(struct fe_to_be_msg);

	if (atomic_read(&priv->plat->phy_clks_suspended))
		return -EBUSY;

	priv->plat->mdio_op_busy = true;
	reinit_completion(&priv->plat->mdio_op);

	emac_mdio_fe_xmit(emac_mdio_fe_ctx);
	EMAC_MDIO_FE_DBG("Sent VIRTIO_EMAC_MDIO_FE_REQ Event Cmd\n");

	emac_mdio_fe_ctx->phy_reply = -1;
	tmp = msecs_to_jiffies(WAIT_PHY_REPLY_MAX_TIMEOUT);
	if (down_timeout(&emac_mdio_fe_ctx->emac_mdio_fe_sem, tmp) == -ETIME) {
		EMAC_MDIO_FE_DBG("Wait for phy reply timeout\n");
		mutex_unlock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
		priv->plat->mdio_op_busy = false;
		complete_all(&priv->plat->mdio_op);
		return -1;
	}

	mutex_unlock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
	priv->plat->mdio_op_busy = false;
	complete_all(&priv->plat->mdio_op);
	return (int)emac_mdio_fe_ctx->phy_reply;
}
EXPORT_SYMBOL_GPL(virtio_mdio_read);

int virtio_mdio_write(struct mii_bus *bus, int addr, int regnum, u16 val)
{
	struct phy_remote_access_t *phy_request = NULL;
	unsigned long tmp;
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);

	if (atomic_read(&priv->plat->phy_clks_suspended))
		return -EBUSY;

	priv->plat->mdio_op_busy = true;
	reinit_completion(&priv->plat->mdio_op);

	mutex_lock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
	phy_request = &emac_mdio_fe_ctx->tx_msg.request_data;
	memset(phy_request, 0, sizeof(*phy_request));
	phy_request->mdio_type = MDIO_CLAUSE_22;
	phy_request->mdio_op_remote_type = MDIO_REMOTE_OP_TYPE_WRITE;
	phy_request->phyaddr = addr;
	phy_request->phyreg = regnum;
	phy_request->phydata = val;

	emac_mdio_fe_ctx->tx_msg.type = VIRTIO_EMAC_MDIO_FE_REQ;
	emac_mdio_fe_ctx->tx_msg.len = sizeof(struct fe_to_be_msg);

	emac_mdio_fe_xmit(emac_mdio_fe_ctx);
	EMAC_MDIO_FE_DBG("Sent VIRTIO_EMAC_MDIO_FE_REQ Event Cmd\n");

	emac_mdio_fe_ctx->phy_reply = -1;
	tmp = msecs_to_jiffies(WAIT_PHY_REPLY_MAX_TIMEOUT);
	if (down_timeout(&emac_mdio_fe_ctx->emac_mdio_fe_sem, tmp) == -ETIME) {
		EMAC_MDIO_FE_WARN("Wait for phy reply timeout\n");
		mutex_unlock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
		priv->plat->mdio_op_busy = false;
		complete_all(&priv->plat->mdio_op);
		return -1;
	}
		priv->plat->mdio_op_busy = false;
		complete_all(&priv->plat->mdio_op);
	mutex_unlock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
	return (int)emac_mdio_fe_ctx->phy_reply;
}
EXPORT_SYMBOL_GPL(virtio_mdio_write);

int virtio_mdio_read_c45(struct mii_bus *bus, int addr, int devnum, int regnum)
{
	struct phy_remote_access_t *phy_request = NULL;
	unsigned long tmp;
		struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	mutex_lock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
	phy_request = &emac_mdio_fe_ctx->tx_msg.request_data;
	memset(phy_request, 0, sizeof(*phy_request));
	phy_request->mdio_type = MDIO_CLAUSE_45_DIRECT;
	phy_request->mdio_op_remote_type = MDIO_REMOTE_OP_TYPE_READ;
	phy_request->phyaddr = addr;
	phy_request->phydev = devnum;
	phy_request->phyreg = regnum;

	emac_mdio_fe_ctx->tx_msg.type = VIRTIO_EMAC_MDIO_FE_REQ;
	emac_mdio_fe_ctx->tx_msg.len = sizeof(struct fe_to_be_msg);
	if (atomic_read(&priv->plat->phy_clks_suspended))
		return -EBUSY;

	priv->plat->mdio_op_busy = true;
	reinit_completion(&priv->plat->mdio_op);

	emac_mdio_fe_xmit(emac_mdio_fe_ctx);
	EMAC_MDIO_FE_DBG("Sent VIRTIO_EMAC_MDIO_FE_REQ Event Cmd\n");

	emac_mdio_fe_ctx->phy_reply = -1;
	tmp = msecs_to_jiffies(WAIT_PHY_REPLY_MAX_TIMEOUT);
	if (down_timeout(&emac_mdio_fe_ctx->emac_mdio_fe_sem, tmp) == -ETIME) {
		EMAC_MDIO_FE_WARN("Wait for phy reply timeout\n");
		mutex_unlock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
				priv->plat->mdio_op_busy = false;
		complete_all(&priv->plat->mdio_op);
		return -1;
	}
		priv->plat->mdio_op_busy = false;
		complete_all(&priv->plat->mdio_op);
	mutex_unlock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
	return (int)emac_mdio_fe_ctx->phy_reply;
}
EXPORT_SYMBOL_GPL(virtio_mdio_read_c45);

int virtio_mdio_write_c45(struct mii_bus *bus, int addr, int devnum, int regnum, u16 val)
{
	struct phy_remote_access_t *phy_request = NULL;
	unsigned long tmp;
		struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);

	if (atomic_read(&priv->plat->phy_clks_suspended))
		return -EBUSY;

	priv->plat->mdio_op_busy = true;
	reinit_completion(&priv->plat->mdio_op);

	mutex_lock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
	phy_request = &emac_mdio_fe_ctx->tx_msg.request_data;
	memset(phy_request, 0, sizeof(*phy_request));
	phy_request->mdio_type = MDIO_CLAUSE_45_DIRECT;
	phy_request->mdio_op_remote_type = MDIO_REMOTE_OP_TYPE_WRITE;
	phy_request->phyaddr = addr;
	phy_request->phydev = devnum;
	phy_request->phyreg = regnum;
	phy_request->phydata = val;

	emac_mdio_fe_ctx->tx_msg.type = VIRTIO_EMAC_MDIO_FE_REQ;
	emac_mdio_fe_ctx->tx_msg.len = sizeof(struct fe_to_be_msg);

	emac_mdio_fe_xmit(emac_mdio_fe_ctx);
	EMAC_MDIO_FE_DBG("Sent VIRTIO_EMAC_MDIO_FE_REQ Event Cmd\n");

	emac_mdio_fe_ctx->phy_reply = -1;
	tmp = msecs_to_jiffies(WAIT_PHY_REPLY_MAX_TIMEOUT);
	if (down_timeout(&emac_mdio_fe_ctx->emac_mdio_fe_sem, tmp) == -ETIME) {
		EMAC_MDIO_FE_WARN("Wait for phy reply timeout\n");
				priv->plat->mdio_op_busy = false;
		complete_all(&priv->plat->mdio_op);
		mutex_unlock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
		return -1;
	}
		priv->plat->mdio_op_busy = false;
		complete_all(&priv->plat->mdio_op);
	mutex_unlock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
	return (int)emac_mdio_fe_ctx->phy_reply;
}
EXPORT_SYMBOL_GPL(virtio_mdio_write_c45);

int virtio_mdio_read_c45_indirect(struct mii_bus *bus, int addr, int regnum)
{
	struct phy_remote_access_t *phy_request = NULL;
	unsigned long tmp;

	mutex_lock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
	phy_request = &emac_mdio_fe_ctx->tx_msg.request_data;
	memset(phy_request, 0, sizeof(*phy_request));
	phy_request->mdio_type = MDIO_CLAUSE_45_DIRECT;
	phy_request->mdio_op_remote_type = MDIO_REMOTE_OP_TYPE_READ;
	phy_request->phyaddr = addr;
	phy_request->phydev =  mdiobus_c45_devad(regnum);
	phy_request->phyreg = mdiobus_c45_regad(regnum);

	emac_mdio_fe_ctx->tx_msg.type = VIRTIO_EMAC_MDIO_FE_REQ;
	emac_mdio_fe_ctx->tx_msg.len = sizeof(struct fe_to_be_msg);

	emac_mdio_fe_xmit(emac_mdio_fe_ctx);
	EMAC_MDIO_FE_DBG("Sent VIRTIO_EMAC_MDIO_FE_REQ Event Cmd\n");

	emac_mdio_fe_ctx->phy_reply = -1;
	tmp = msecs_to_jiffies(WAIT_PHY_REPLY_MAX_TIMEOUT);
	if (down_timeout(&emac_mdio_fe_ctx->emac_mdio_fe_sem, tmp) == -ETIME) {
		EMAC_MDIO_FE_WARN("Wait for phy reply timeout\n");
		mutex_unlock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
		return -1;
	}

	mutex_unlock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
	return (int)emac_mdio_fe_ctx->phy_reply;
}
EXPORT_SYMBOL_GPL(virtio_mdio_read_c45_indirect);

int virtio_mdio_write_c45_indirect(struct mii_bus *bus, int addr, int regnum, u16 val)
{
	struct phy_remote_access_t *phy_request = NULL;
	unsigned long tmp;

	mutex_lock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
	phy_request = &emac_mdio_fe_ctx->tx_msg.request_data;
	memset(phy_request, 0, sizeof(*phy_request));
	phy_request->mdio_type = MDIO_CLAUSE_45_DIRECT;
	phy_request->mdio_op_remote_type = MDIO_REMOTE_OP_TYPE_WRITE;
	phy_request->phyaddr = addr;
	phy_request->phydev = mdiobus_c45_devad(regnum);
	phy_request->phyreg = mdiobus_c45_regad(regnum);
	phy_request->phydata = val;

	emac_mdio_fe_ctx->tx_msg.type = VIRTIO_EMAC_MDIO_FE_REQ;
	emac_mdio_fe_ctx->tx_msg.len = sizeof(struct fe_to_be_msg);

	emac_mdio_fe_xmit(emac_mdio_fe_ctx);
	EMAC_MDIO_FE_DBG("Sent VIRTIO_EMAC_MDIO_FE_REQ Event Cmd\n");

	emac_mdio_fe_ctx->phy_reply = -1;
	tmp = msecs_to_jiffies(WAIT_PHY_REPLY_MAX_TIMEOUT);
	if (down_timeout(&emac_mdio_fe_ctx->emac_mdio_fe_sem, tmp) == -ETIME) {
		EMAC_MDIO_FE_WARN("Wait for phy reply timeout\n");
		mutex_unlock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
		return -1;
	}

	mutex_unlock(&emac_mdio_fe_pdev->emac_mdio_fe_lock);
	return (int)emac_mdio_fe_ctx->phy_reply;
}
EXPORT_SYMBOL_GPL(virtio_mdio_write_c45_indirect);

static int emac_mdio_fe_probe(struct virtio_device *vdev)
{
	int ret;
	struct emac_mdio_dev *pdev;

	EMAC_MDIO_FE_INFO("Start Probe allocate devm\n");

	/* Resource Managed kzalloc and mem allocated with the fun is auto freed on driver detach */
	/* Allocations are cleaned up automatically shall the probe itself fail */
	pdev = devm_kzalloc(&vdev->dev, sizeof(*pdev), GFP_KERNEL);
	if (!pdev) {
		ret = -ENOMEM;
		return ret;
	}
	emac_mdio_fe_pdev = pdev;

	sema_init(&pdev->emac_mdio_fe_sem, (0));
	mutex_init(&pdev->emac_mdio_fe_lock);

	emac_mdio_fe_ctx = pdev;
	vdev->priv = pdev;
	pdev->vdev = vdev;
	pdev->name = "emac_mdio_fe";

	/*Initialize States*/
	pdev->emac_mdio_fe_state = EMAC_MDIO_FE_DOWN;

	EMAC_MDIO_FE_INFO("Init VQS\n");
	/* Allocate and Initialize RX and TX VirtQueues */
	ret = emac_mdio_fe_init_vqs(pdev);

	/* enable vq use in probe function */
	virtio_device_ready(vdev);

	EMAC_MDIO_FE_INFO("Allocate RXBufs\n");
	emac_mdio_fe_allocate_rxbufs(pdev);

	/* Enable TX Complete ISR */
	virtqueue_enable_cb(pdev->emac_mdio_fe_txq);

	/*Enable Rx Complete ISR*/
	virtqueue_enable_cb(pdev->emac_mdio_fe_rxq);

	/* Kick Host */
	virtqueue_kick(pdev->emac_mdio_fe_rxq);

	EMAC_MDIO_FE_INFO("Kicked Host VirtQ\n");
	pdev->emac_mdio_fe_state = EMAC_MDIO_FE_DOWN;

	emac_mdio_fe_ctx->tx_msg.type = VIRTIO_EMAC_MDIO_FE_UP;
	emac_mdio_fe_ctx->tx_msg.len = sizeof(struct fe_to_be_msg);
	emac_mdio_fe_xmit(emac_mdio_fe_ctx);
	EMAC_MDIO_FE_INFO("Sent Register Event Cmd\n");

	return 0;
}

static int __init emac_mdio_fe_init(void)
{
	EMAC_MDIO_FE_INFO("%s: Module Entry\n", __func__);
	return register_virtio_driver(&emac_mdio_fe_virtio_drv);
}

static void __exit emac_mdio_fe_exit(void)
{
	unregister_virtio_driver(&emac_mdio_fe_virtio_drv);
}

module_init(emac_mdio_fe_init);
module_exit(emac_mdio_fe_exit);

MODULE_SOFTDEP("post: stmmac");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EMAC Virt MDIO FE Driver");
