// SPDX-License-Identifier: GPL-2.0-only
/*
 * Device handling thread implementation for mac80211 ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * Based on:
 * ST-Ericsson UMAC CW1200 driver, which is
 * Copyright (c) 2010, ST-Ericsson
 * Author: Ajitpal Singh <ajitpal.singh@stericsson.com>
 */

#include <linux/module.h>
#include <net/mac80211.h>
#include <linux/kthread.h>
#include <linux/timer.h>

#include "cw1200.h"
#include "bh.h"
#include "hwio.h"
#include "wsm.h"
#include "hwbus.h"
#include "debug.h"
#include "fwio.h"

static int cw1200_bh(void *arg);

#define DOWNLOAD_BLOCK_SIZE_WR	(0x1000 - 4)
/* an SPI message cannot be bigger than (2"12-1)*2 bytes
 * "*2" to cvt to bytes
 */
#define MAX_SZ_RD_WR_BUFFERS	(DOWNLOAD_BLOCK_SIZE_WR*2)
#define PIGGYBACK_CTRL_REG	(2)
#define EFFECTIVE_BUF_SIZE	(MAX_SZ_RD_WR_BUFFERS - PIGGYBACK_CTRL_REG)

/* Suspend state privates */
enum cw1200_bh_pm_state {
	CW1200_BH_RESUMED = 0,
	CW1200_BH_SUSPEND,
	CW1200_BH_SUSPENDED,
	CW1200_BH_RESUME,
};

static void cw1200_bh_work(struct work_struct *work)
{
	struct cw1200_common *priv =
	container_of(work, struct cw1200_common, bh_work);
	cw1200_bh(priv);
}

int cw1200_register_bh(struct cw1200_common *priv)
{
	int err = 0;
	/* Realtime workqueue */
	priv->bh_workqueue = alloc_workqueue("cw1200_bh",
				WQ_MEM_RECLAIM | WQ_HIGHPRI
				| WQ_CPU_INTENSIVE, 1);

	if (!priv->bh_workqueue)
		return -ENOMEM;

	INIT_WORK(&priv->bh_work, cw1200_bh_work);

	pr_debug("[BH] register.\n");

	atomic_set(&priv->bh_rx, 0);
	atomic_set(&priv->bh_tx, 0);
	atomic_set(&priv->bh_term, 0);
	atomic_set(&priv->bh_suspend, CW1200_BH_RESUMED);
	priv->bh_error = 0;
	priv->hw_bufs_used = 0;
	priv->buf_id_tx = 0;
	priv->buf_id_rx = 0;
	init_waitqueue_head(&priv->bh_wq);
	init_waitqueue_head(&priv->bh_evt_wq);

	err = !queue_work(priv->bh_workqueue, &priv->bh_work);
	WARN_ON(err);
	return err;
}

void cw1200_unregister_bh(struct cw1200_common *priv)
{
	atomic_inc(&priv->bh_term);
	wake_up(&priv->bh_wq);

	destroy_workqueue(priv->bh_workqueue);
	priv->bh_workqueue = NULL;

	pr_debug("[BH] unregistered.\n");
}

void cw1200_irq_handler(struct cw1200_common *priv)
{
	pr_debug("[BH] irq.\n");

	/* Disable Interrupts! */
	/* NOTE:  hwbus_ops->lock already held */
	__cw1200_irq_enable(priv, 0);

	if (/* WARN_ON */(priv->bh_error))
		return;

	if (atomic_inc_return(&priv->bh_rx) == 1)
		wake_up(&priv->bh_wq);
}
EXPORT_SYMBOL_GPL(cw1200_irq_handler);

void cw1200_bh_wakeup(struct cw1200_common *priv)
{
	pr_debug("[BH] wakeup.\n");
	if (priv->bh_error) {
		pr_err("[BH] wakeup failed (BH error)\n");
		return;
	}

	if (atomic_inc_return(&priv->bh_tx) == 1)
		wake_up(&priv->bh_wq);
}

int cw1200_bh_suspend(struct cw1200_common *priv)
{
	pr_debug("[BH] suspend.\n");
	if (priv->bh_error) {
		wiphy_warn(priv->hw->wiphy, "BH error -- can't suspend\n");
		return -EINVAL;
	}

	atomic_set(&priv->bh_suspend, CW1200_BH_SUSPEND);
	wake_up(&priv->bh_wq);
	return wait_event_timeout(priv->bh_evt_wq, priv->bh_error ||
		(CW1200_BH_SUSPENDED == atomic_read(&priv->bh_suspend)),
		 1 * HZ) ? 0 : -ETIMEDOUT;
}

int cw1200_bh_resume(struct cw1200_common *priv)
{
	pr_debug("[BH] resume.\n");
	if (priv->bh_error) {
		wiphy_warn(priv->hw->wiphy, "BH error -- can't resume\n");
		return -EINVAL;
	}

	atomic_set(&priv->bh_suspend, CW1200_BH_RESUME);
	wake_up(&priv->bh_wq);
	return wait_event_timeout(priv->bh_evt_wq, priv->bh_error ||
		(CW1200_BH_RESUMED == atomic_read(&priv->bh_suspend)),
		1 * HZ) ? 0 : -ETIMEDOUT;
}

static inline void wsm_alloc_tx_buffer(struct cw1200_common *priv)
{
	++priv->hw_bufs_used;
}

int wsm_release_tx_buffer(struct cw1200_common *priv, int count)
{
	int ret = 0;
	int hw_bufs_used = priv->hw_bufs_used;

	priv->hw_bufs_used -= count;
	if (WARN_ON(priv->hw_bufs_used < 0))
		ret = -1;
	else if (hw_bufs_used >= priv->wsm_caps.input_buffers)
		ret = 1;
	if (!priv->hw_bufs_used)
		wake_up(&priv->bh_evt_wq);
	return ret;
}

static int cw1200_bh_read_ctrl_reg(struct cw1200_common *priv,
					  u16 *ctrl_reg)
{
	int ret;

	ret = cw1200_reg_read_16(priv,
			ST90TDS_CONTROL_REG_ID, ctrl_reg);
	if (ret) {
		ret = cw1200_reg_read_16(priv,
				ST90TDS_CONTROL_REG_ID, ctrl_reg);
		if (ret)
			pr_err("[BH] Failed to read control register.\n");
	}

	return ret;
}

static int cw1200_device_wakeup(struct cw1200_common *priv)
{
	u16 ctrl_reg;
	int ret;

	pr_debug("[BH] Device wakeup.\n");

	/* First, set the dpll register */
	ret = cw1200_reg_write_32(priv, ST90TDS_TSET_GEN_R_W_REG_ID,
				  cw1200_dpll_from_clk(priv->hw_refclk));
	if (WARN_ON(ret))
		return ret;

	/* To force the device to be always-on, the host sets WLAN_UP to 1 */
	ret = cw1200_reg_write_16(priv, ST90TDS_CONTROL_REG_ID,
			ST90TDS_CONT_WUP_BIT);
	if (WARN_ON(ret))
		return ret;

	ret = cw1200_bh_read_ctrl_reg(priv, &ctrl_reg);
	if (WARN_ON(ret))
		return ret;

	/* If the device returns WLAN_RDY as 1, the device is active and will
	 * remain active.
	 */
	if (ctrl_reg & ST90TDS_CONT_RDY_BIT) {
		pr_debug("[BH] Device awake.\n");
		return 1;
	}

	return 0;
}

/* Must be called from BH thraed. */
void cw1200_enable_powersave(struct cw1200_common *priv,
			     bool enable)
{
	pr_debug("[BH] Powerave is %s.\n",
		 enable ? "enabled" : "disabled");
	priv->powersave_enabled = enable;
}

static int cw1200_bh_rx_helper(struct cw1200_common *priv,
			       uint16_t *ctrl_reg,
			       int *tx)
{
	size_t read_len = 0;
	struct sk_buff *skb_rx = NULL;
	struct wsm_hdr *wsm;
	size_t wsm_len;
	u16 wsm_id;
	u8 wsm_seq;
	int rx_resync = 1;

	size_t alloc_len;
	u8 *data;

	read_len = (*ctrl_reg & ST90TDS_CONT_NEXT_LEN_MASK) * 2;
	if (!read_len)
		return 0; /* No more work */

	if (WARN_ON((read_len < sizeof(struct wsm_hdr)) ||
		    (read_len > EFFECTIVE_BUF_SIZE))) {
		pr_debug("Invalid read len: %zu (%04x)",
			 read_len, *ctrl_reg);
		goto err;
	}

	/* Add SIZE of PIGGYBACK reg (CONTROL Reg)
	 * to the NEXT Message length + 2 Bytes for SKB
	 */
	read_len = read_len + 2;

	alloc_len = priv->hwbus_ops->align_size(
		priv->hwbus_priv, read_len);

	/* Check if not exceeding CW1200 capabilities */
	if (WARN_ON_ONCE(alloc_len > EFFECTIVE_BUF_SIZE)) {
		pr_debug("Read aligned len: %zu\n",
			 alloc_len);
	}

	skb_rx = dev_alloc_skb(alloc_len);
	if (WARN_ON(!skb_rx))
		goto err;

	skb_trim(skb_rx, 0);
	skb_put(skb_rx, read_len);
	data = skb_rx->data;
	if (WARN_ON(!data))
		goto err;

	if (WARN_ON(cw1200_data_read(priv, data, alloc_len))) {
		pr_err("rx blew up, len %zu\n", alloc_len);
		goto err;
	}

	/* Piggyback */
	*ctrl_reg = __le16_to_cpu(
		((__le16 *)data)[alloc_len / 2 - 1]);

	wsm = (struct wsm_hdr *)data;
	wsm_len = __le16_to_cpu(wsm->len);
	if (WARN_ON(wsm_len > read_len))
		goto err;

	if (priv->wsm_enable_wsm_dumps)
		print_hex_dump_bytes("<-- ",
				     DUMP_PREFIX_NONE,
				     data, wsm_len);

	wsm_id  = __le16_to_cpu(wsm->id) & 0xFFF;
	wsm_seq = (__le16_to_cpu(wsm->id) >> 13) & 7;

	skb_trim(skb_rx, wsm_len);

	if (wsm_id == 0x0800) {
		wsm_handle_exception(priv,
				     &data[sizeof(*wsm)],
				     wsm_len - sizeof(*wsm));
		goto err;
	} else if (!rx_resync) {
		if (WARN_ON(wsm_seq != priv->wsm_rx_seq))
			goto err;
	}
	priv->wsm_rx_seq = (wsm_seq + 1) & 7;
	rx_resync = 0;

	if (wsm_id & 0x0400) {
		int rc = wsm_release_tx_buffer(priv, 1);
		if (WARN_ON(rc < 0))
			return rc;
		else if (rc > 0)
			*tx = 1;
	}

	/* cw1200_wsm_rx takes care on SKB livetime */
	if (WARN_ON(wsm_handle_rx(priv, wsm_id, wsm, &skb_rx)))
		goto err;

	dev_kfree_skb(skb_rx);

	return 0;

err:
	dev_kfree_skb(skb_rx);
	return -1;
}

static int cw1200_bh_tx_helper(struct cw1200_common *priv,
			       int *pending_tx,
			       int *tx_burst)
{
	size_t tx_len;
	u8 *data;
	int ret;
	struct wsm_hdr *wsm;

	if (priv->device_can_sleep) {
		ret = cw1200_device_wakeup(priv);
		if (WARN_ON(ret < 0)) { /* Error in wakeup */
			*pending_tx = 1;
			return 0;
		} else if (ret) { /* Woke up */
			priv->device_can_sleep = false;
		} else { /* Did not awake */
			*pending_tx = 1;
			return 0;
		}
	}

	wsm_alloc_tx_buffer(priv);
	ret = wsm_get_tx(priv, &data, &tx_len, tx_burst);
	if (ret <= 0) {
		wsm_release_tx_buffer(priv, 1);
		if (WARN_ON(ret < 0))
			return ret; /* Error */
		return 0; /* No work */
	}

	wsm = (struct wsm_hdr *)data;
	BUG_ON(tx_len < sizeof(*wsm));
	BUG_ON(__le16_to_cpu(wsm->len) != tx_len);

	atomic_inc(&priv->bh_tx);

	tx_len = priv->hwbus_ops->align_size(
		priv->hwbus_priv, tx_len);

	/* Check if not exceeding CW1200 capabilities */
	if (WARN_ON_ONCE(tx_len > EFFECTIVE_BUF_SIZE))
		pr_debug("Write aligned len: %zu\n", tx_len);

	wsm->id &= __cpu_to_le16(0xffff ^ WSM_TX_SEQ(WSM_TX_SEQ_MAX));
	wsm->id |= __cpu_to_le16(WSM_TX_SEQ(priv->wsm_tx_seq));

	if (WARN_ON(cw1200_data_write(priv, data, tx_len))) {
		pr_err("tx blew up, len %zu\n", tx_len);
		wsm_release_tx_buffer(priv, 1);
		return -1; /* Error */
	}

	if (priv->wsm_enable_wsm_dumps)
		print_hex_dump_bytes("--> ",
				     DUMP_PREFIX_NONE,
				     data,
				     __le16_to_cpu(wsm->len));

	wsm_txed(priv, data);
	priv->wsm_tx_seq = (priv->wsm_tx_seq + 1) & WSM_TX_SEQ_MAX;

	if (*tx_burst > 1) {
		cw1200_debug_tx_burst(priv);
		return 1; /* Work remains */
	}

	return 0;
}

static int cw1200_bh(void *arg)
{
	struct cw1200_common *priv = arg;
	int rx, tx, term, suspend;
	u16 ctrl_reg = 0;
	int tx_allowed;
	int pending_tx = 0;
	int tx_burst;
	long status;
	u32 dummy;
	int ret;

	for (;;) {
		if (!priv->hw_bufs_used &&
		    priv->powersave_enabled &&
		    !priv->device_can_sleep &&
		    !atomic_read(&priv->recent_scan)) {
			status = 1 * HZ;
			pr_debug("[BH] Device wakedown. No data.\n");
			cw1200_reg_write_16(priv, ST90TDS_CONTROL_REG_ID, 0);
			priv->device_can_sleep = true;
		} else if (priv->hw_bufs_used) {
			/* Interrupt loss detection */
			status = 1 * HZ;
		} else {
			status = MAX_SCHEDULE_TIMEOUT;
		}

		/* Dummy Read for SDIO retry mechanism*/
		if ((priv->hw_type != -1) &&
		    (atomic_read(&priv->bh_rx) == 0) &&
		    (atomic_read(&priv->bh_tx) == 0))
			cw1200_reg_read(priv, ST90TDS_CONFIG_REG_ID,
					&dummy, sizeof(dummy));

		pr_debug("[BH] waiting ...\n");
		status = wait_event_interruptible_timeout(priv->bh_wq, ({
				rx = atomic_xchg(&priv->bh_rx, 0);
				tx = atomic_xchg(&priv->bh_tx, 0);
				term = atomic_xchg(&priv->bh_term, 0);
				suspend = pending_tx ?
					0 : atomic_read(&priv->bh_suspend);
				(rx || tx || term || suspend || priv->bh_error);
			}), status);

		pr_debug("[BH] - rx: %d, tx: %d, term: %d, bh_err: %d, suspend: %d, status: %ld\n",
			 rx, tx, term, suspend, priv->bh_error, status);

		/* Did an error occur? */
		if ((status < 0 && status != -ERESTARTSYS) ||
		    term || priv->bh_error) {
			break;
		}
		if (!status) {  /* wait_event timed out */
			unsigned long timestamp = jiffies;
			long timeout;
			int pending = 0;
			int i;

			/* Check to see if we have any outstanding frames */
			if (priv->hw_bufs_used && (!rx || !tx)) {
				wiphy_warn(priv->hw->wiphy,
					   "Missed interrupt? (%d frames outstanding)\n",
					   priv->hw_bufs_used);
				rx = 1;

				/* Get a timestamp of "oldest" frame */
				for (i = 0; i < 4; ++i)
					pending += cw1200_queue_get_xmit_timestamp(
						&priv->tx_queue[i],
						&timestamp,
						priv->pending_frame_id);

				/* Check if frame transmission is timed out.
				 * Add an extra second with respect to possible
				 * interrupt loss.
				 */
				timeout = timestamp +
					WSM_CMD_LAST_CHANCE_TIMEOUT +
					1 * HZ  -
					jiffies;

				/* And terminate BH thread if the frame is "stuck" */
				if (pending && timeout < 0) {
					wiphy_warn(priv->hw->wiphy,
						   "Timeout waiting for TX confirm (%d/%d pending, %ld vs %lu).\n",
						   priv->hw_bufs_used, pending,
						   timestamp, jiffies);
					break;
				}
			} else if (!priv->device_can_sleep &&
				   !atomic_read(&priv->recent_scan)) {
				pr_debug("[BH] Device wakedown. Timeout.\n");
				cw1200_reg_write_16(priv,
						    ST90TDS_CONTROL_REG_ID, 0);
				priv->device_can_sleep = true;
			}
			goto done;
		} else if (suspend) {
			pr_debug("[BH] Device suspend.\n");
			if (priv->powersave_enabled) {
				pr_debug("[BH] Device wakedown. Suspend.\n");
				cw1200_reg_write_16(priv,
						    ST90TDS_CONTROL_REG_ID, 0);
				priv->device_can_sleep = true;
			}

			atomic_set(&priv->bh_suspend, CW1200_BH_SUSPENDED);
			wake_up(&priv->bh_evt_wq);
			status = wait_event_interruptible(priv->bh_wq,
							  CW1200_BH_RESUME == atomic_read(&priv->bh_suspend));
			if (status < 0) {
				wiphy_err(priv->hw->wiphy,
					  "Failed to wait for resume: %ld.\n",
					  status);
				break;
			}
			pr_debug("[BH] Device resume.\n");
			atomic_set(&priv->bh_suspend, CW1200_BH_RESUMED);
			wake_up(&priv->bh_evt_wq);
			atomic_inc(&priv->bh_rx);
			goto done;
		}

	rx:
		tx += pending_tx;
		pending_tx = 0;

		if (cw1200_bh_read_ctrl_reg(priv, &ctrl_reg))
			break;

		/* Don't bother trying to rx unless we have data to read */
		if (ctrl_reg & ST90TDS_CONT_NEXT_LEN_MASK) {
			ret = cw1200_bh_rx_helper(priv, &ctrl_reg, &tx);
			if (ret < 0)
				break;
			/* Double up here if there's more data.. */
			if (ctrl_reg & ST90TDS_CONT_NEXT_LEN_MASK) {
				ret = cw1200_bh_rx_helper(priv, &ctrl_reg, &tx);
				if (ret < 0)
					break;
			}
		}

	tx:
		if (tx) {
			tx = 0;

			BUG_ON(priv->hw_bufs_used > priv->wsm_caps.input_buffers);
			tx_burst = priv->wsm_caps.input_buffers - priv->hw_bufs_used;
			tx_allowed = tx_burst > 0;

			if (!tx_allowed) {
				/* Buffers full.  Ensure we process tx
				 * after we handle rx..
				 */
				pending_tx = tx;
				goto done_rx;
			}
			ret = cw1200_bh_tx_helper(priv, &pending_tx, &tx_burst);
			if (ret < 0)
				break;
			if (ret > 0) /* More to transmit */
				tx = ret;

			/* Re-read ctrl reg */
			if (cw1200_bh_read_ctrl_reg(priv, &ctrl_reg))
				break;
		}

	done_rx:
		if (priv->bh_error)
			break;
		if (ctrl_reg & ST90TDS_CONT_NEXT_LEN_MASK)
			goto rx;
		if (tx)
			goto tx;

	done:
		/* Re-enable device interrupts */
		priv->hwbus_ops->lock(priv->hwbus_priv);
		__cw1200_irq_enable(priv, 1);
		priv->hwbus_ops->unlock(priv->hwbus_priv);
	}

	/* Explicitly disable device interrupts */
	priv->hwbus_ops->lock(priv->hwbus_priv);
	__cw1200_irq_enable(priv, 0);
	priv->hwbus_ops->unlock(priv->hwbus_priv);

	if (!term) {
		pr_err("[BH] Fatal error, exiting.\n");
		priv->bh_error = 1;
		/* TODO: schedule_work(recovery) */
	}
	return 0;
}
