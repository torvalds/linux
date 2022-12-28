// SPDX-License-Identifier: GPL-2.0-or-later
/* hfcsusb.c
 * mISDN driver for Colognechip HFC-S USB chip
 *
 * Copyright 2001 by Peter Sprenger (sprenger@moving-bytes.de)
 * Copyright 2008 by Martin Bachem (info@bachem-it.com)
 *
 * module params
 *   debug=<n>, default=0, with n=0xHHHHGGGG
 *      H - l1 driver flags described in hfcsusb.h
 *      G - common mISDN debug flags described at mISDNhw.h
 *
 *   poll=<n>, default 128
 *     n : burst size of PH_DATA_IND at transparent rx data
 *
 * Revision: 0.3.3 (socket), 2008-11-05
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/usb.h>
#include <linux/mISDNhw.h>
#include <linux/slab.h>
#include "hfcsusb.h"

static unsigned int debug;
static int poll = DEFAULT_TRANSP_BURST_SZ;

static LIST_HEAD(HFClist);
static DEFINE_RWLOCK(HFClock);


MODULE_AUTHOR("Martin Bachem");
MODULE_LICENSE("GPL");
module_param(debug, uint, S_IRUGO | S_IWUSR);
module_param(poll, int, 0);

static int hfcsusb_cnt;

/* some function prototypes */
static void hfcsusb_ph_command(struct hfcsusb *hw, u_char command);
static void release_hw(struct hfcsusb *hw);
static void reset_hfcsusb(struct hfcsusb *hw);
static void setPortMode(struct hfcsusb *hw);
static void hfcsusb_start_endpoint(struct hfcsusb *hw, int channel);
static void hfcsusb_stop_endpoint(struct hfcsusb *hw, int channel);
static int  hfcsusb_setup_bch(struct bchannel *bch, int protocol);
static void deactivate_bchannel(struct bchannel *bch);
static int  hfcsusb_ph_info(struct hfcsusb *hw);

/* start next background transfer for control channel */
static void
ctrl_start_transfer(struct hfcsusb *hw)
{
	if (debug & DBG_HFC_CALL_TRACE)
		printk(KERN_DEBUG "%s: %s\n", hw->name, __func__);

	if (hw->ctrl_cnt) {
		hw->ctrl_urb->pipe = hw->ctrl_out_pipe;
		hw->ctrl_urb->setup_packet = (u_char *)&hw->ctrl_write;
		hw->ctrl_urb->transfer_buffer = NULL;
		hw->ctrl_urb->transfer_buffer_length = 0;
		hw->ctrl_write.wIndex =
			cpu_to_le16(hw->ctrl_buff[hw->ctrl_out_idx].hfcs_reg);
		hw->ctrl_write.wValue =
			cpu_to_le16(hw->ctrl_buff[hw->ctrl_out_idx].reg_val);

		usb_submit_urb(hw->ctrl_urb, GFP_ATOMIC);
	}
}

/*
 * queue a control transfer request to write HFC-S USB
 * chip register using CTRL resuest queue
 */
static int write_reg(struct hfcsusb *hw, __u8 reg, __u8 val)
{
	struct ctrl_buf *buf;

	if (debug & DBG_HFC_CALL_TRACE)
		printk(KERN_DEBUG "%s: %s reg(0x%02x) val(0x%02x)\n",
		       hw->name, __func__, reg, val);

	spin_lock(&hw->ctrl_lock);
	if (hw->ctrl_cnt >= HFC_CTRL_BUFSIZE) {
		spin_unlock(&hw->ctrl_lock);
		return 1;
	}
	buf = &hw->ctrl_buff[hw->ctrl_in_idx];
	buf->hfcs_reg = reg;
	buf->reg_val = val;
	if (++hw->ctrl_in_idx >= HFC_CTRL_BUFSIZE)
		hw->ctrl_in_idx = 0;
	if (++hw->ctrl_cnt == 1)
		ctrl_start_transfer(hw);
	spin_unlock(&hw->ctrl_lock);

	return 0;
}

/* control completion routine handling background control cmds */
static void
ctrl_complete(struct urb *urb)
{
	struct hfcsusb *hw = (struct hfcsusb *) urb->context;

	if (debug & DBG_HFC_CALL_TRACE)
		printk(KERN_DEBUG "%s: %s\n", hw->name, __func__);

	urb->dev = hw->dev;
	if (hw->ctrl_cnt) {
		hw->ctrl_cnt--;	/* decrement actual count */
		if (++hw->ctrl_out_idx >= HFC_CTRL_BUFSIZE)
			hw->ctrl_out_idx = 0;	/* pointer wrap */

		ctrl_start_transfer(hw); /* start next transfer */
	}
}

/* handle LED bits   */
static void
set_led_bit(struct hfcsusb *hw, signed short led_bits, int set_on)
{
	if (set_on) {
		if (led_bits < 0)
			hw->led_state &= ~abs(led_bits);
		else
			hw->led_state |= led_bits;
	} else {
		if (led_bits < 0)
			hw->led_state |= abs(led_bits);
		else
			hw->led_state &= ~led_bits;
	}
}

/* handle LED requests  */
static void
handle_led(struct hfcsusb *hw, int event)
{
	struct hfcsusb_vdata *driver_info = (struct hfcsusb_vdata *)
		hfcsusb_idtab[hw->vend_idx].driver_info;
	__u8 tmpled;

	if (driver_info->led_scheme == LED_OFF)
		return;
	tmpled = hw->led_state;

	switch (event) {
	case LED_POWER_ON:
		set_led_bit(hw, driver_info->led_bits[0], 1);
		set_led_bit(hw, driver_info->led_bits[1], 0);
		set_led_bit(hw, driver_info->led_bits[2], 0);
		set_led_bit(hw, driver_info->led_bits[3], 0);
		break;
	case LED_POWER_OFF:
		set_led_bit(hw, driver_info->led_bits[0], 0);
		set_led_bit(hw, driver_info->led_bits[1], 0);
		set_led_bit(hw, driver_info->led_bits[2], 0);
		set_led_bit(hw, driver_info->led_bits[3], 0);
		break;
	case LED_S0_ON:
		set_led_bit(hw, driver_info->led_bits[1], 1);
		break;
	case LED_S0_OFF:
		set_led_bit(hw, driver_info->led_bits[1], 0);
		break;
	case LED_B1_ON:
		set_led_bit(hw, driver_info->led_bits[2], 1);
		break;
	case LED_B1_OFF:
		set_led_bit(hw, driver_info->led_bits[2], 0);
		break;
	case LED_B2_ON:
		set_led_bit(hw, driver_info->led_bits[3], 1);
		break;
	case LED_B2_OFF:
		set_led_bit(hw, driver_info->led_bits[3], 0);
		break;
	}

	if (hw->led_state != tmpled) {
		if (debug & DBG_HFC_CALL_TRACE)
			printk(KERN_DEBUG "%s: %s reg(0x%02x) val(x%02x)\n",
			       hw->name, __func__,
			       HFCUSB_P_DATA, hw->led_state);

		write_reg(hw, HFCUSB_P_DATA, hw->led_state);
	}
}

/*
 * Layer2 -> Layer 1 Bchannel data
 */
static int
hfcusb_l2l1B(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct bchannel		*bch = container_of(ch, struct bchannel, ch);
	struct hfcsusb		*hw = bch->hw;
	int			ret = -EINVAL;
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);
	u_long			flags;

	if (debug & DBG_HFC_CALL_TRACE)
		printk(KERN_DEBUG "%s: %s\n", hw->name, __func__);

	switch (hh->prim) {
	case PH_DATA_REQ:
		spin_lock_irqsave(&hw->lock, flags);
		ret = bchannel_senddata(bch, skb);
		spin_unlock_irqrestore(&hw->lock, flags);
		if (debug & DBG_HFC_CALL_TRACE)
			printk(KERN_DEBUG "%s: %s PH_DATA_REQ ret(%i)\n",
			       hw->name, __func__, ret);
		if (ret > 0)
			ret = 0;
		return ret;
	case PH_ACTIVATE_REQ:
		if (!test_and_set_bit(FLG_ACTIVE, &bch->Flags)) {
			hfcsusb_start_endpoint(hw, bch->nr - 1);
			ret = hfcsusb_setup_bch(bch, ch->protocol);
		} else
			ret = 0;
		if (!ret)
			_queue_data(ch, PH_ACTIVATE_IND, MISDN_ID_ANY,
				    0, NULL, GFP_KERNEL);
		break;
	case PH_DEACTIVATE_REQ:
		deactivate_bchannel(bch);
		_queue_data(ch, PH_DEACTIVATE_IND, MISDN_ID_ANY,
			    0, NULL, GFP_KERNEL);
		ret = 0;
		break;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return ret;
}

/*
 * send full D/B channel status information
 * as MPH_INFORMATION_IND
 */
static int
hfcsusb_ph_info(struct hfcsusb *hw)
{
	struct ph_info *phi;
	struct dchannel *dch = &hw->dch;
	int i;

	phi = kzalloc(struct_size(phi, bch, dch->dev.nrbchan), GFP_ATOMIC);
	if (!phi)
		return -ENOMEM;

	phi->dch.ch.protocol = hw->protocol;
	phi->dch.ch.Flags = dch->Flags;
	phi->dch.state = dch->state;
	phi->dch.num_bch = dch->dev.nrbchan;
	for (i = 0; i < dch->dev.nrbchan; i++) {
		phi->bch[i].protocol = hw->bch[i].ch.protocol;
		phi->bch[i].Flags = hw->bch[i].Flags;
	}
	_queue_data(&dch->dev.D, MPH_INFORMATION_IND, MISDN_ID_ANY,
		    struct_size(phi, bch, dch->dev.nrbchan), phi, GFP_ATOMIC);
	kfree(phi);

	return 0;
}

/*
 * Layer2 -> Layer 1 Dchannel data
 */
static int
hfcusb_l2l1D(struct mISDNchannel *ch, struct sk_buff *skb)
{
	struct mISDNdevice	*dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel		*dch = container_of(dev, struct dchannel, dev);
	struct mISDNhead	*hh = mISDN_HEAD_P(skb);
	struct hfcsusb		*hw = dch->hw;
	int			ret = -EINVAL;
	u_long			flags;

	switch (hh->prim) {
	case PH_DATA_REQ:
		if (debug & DBG_HFC_CALL_TRACE)
			printk(KERN_DEBUG "%s: %s: PH_DATA_REQ\n",
			       hw->name, __func__);

		spin_lock_irqsave(&hw->lock, flags);
		ret = dchannel_senddata(dch, skb);
		spin_unlock_irqrestore(&hw->lock, flags);
		if (ret > 0) {
			ret = 0;
			queue_ch_frame(ch, PH_DATA_CNF, hh->id, NULL);
		}
		break;

	case PH_ACTIVATE_REQ:
		if (debug & DBG_HFC_CALL_TRACE)
			printk(KERN_DEBUG "%s: %s: PH_ACTIVATE_REQ %s\n",
			       hw->name, __func__,
			       (hw->protocol == ISDN_P_NT_S0) ? "NT" : "TE");

		if (hw->protocol == ISDN_P_NT_S0) {
			ret = 0;
			if (test_bit(FLG_ACTIVE, &dch->Flags)) {
				_queue_data(&dch->dev.D,
					    PH_ACTIVATE_IND, MISDN_ID_ANY, 0,
					    NULL, GFP_ATOMIC);
			} else {
				hfcsusb_ph_command(hw,
						   HFC_L1_ACTIVATE_NT);
				test_and_set_bit(FLG_L2_ACTIVATED,
						 &dch->Flags);
			}
		} else {
			hfcsusb_ph_command(hw, HFC_L1_ACTIVATE_TE);
			ret = l1_event(dch->l1, hh->prim);
		}
		break;

	case PH_DEACTIVATE_REQ:
		if (debug & DBG_HFC_CALL_TRACE)
			printk(KERN_DEBUG "%s: %s: PH_DEACTIVATE_REQ\n",
			       hw->name, __func__);
		test_and_clear_bit(FLG_L2_ACTIVATED, &dch->Flags);

		if (hw->protocol == ISDN_P_NT_S0) {
			struct sk_buff_head free_queue;

			__skb_queue_head_init(&free_queue);
			hfcsusb_ph_command(hw, HFC_L1_DEACTIVATE_NT);
			spin_lock_irqsave(&hw->lock, flags);
			skb_queue_splice_init(&dch->squeue, &free_queue);
			if (dch->tx_skb) {
				__skb_queue_tail(&free_queue, dch->tx_skb);
				dch->tx_skb = NULL;
			}
			dch->tx_idx = 0;
			if (dch->rx_skb) {
				__skb_queue_tail(&free_queue, dch->rx_skb);
				dch->rx_skb = NULL;
			}
			test_and_clear_bit(FLG_TX_BUSY, &dch->Flags);
			spin_unlock_irqrestore(&hw->lock, flags);
			__skb_queue_purge(&free_queue);
#ifdef FIXME
			if (test_and_clear_bit(FLG_L1_BUSY, &dch->Flags))
				dchannel_sched_event(&hc->dch, D_CLEARBUSY);
#endif
			ret = 0;
		} else
			ret = l1_event(dch->l1, hh->prim);
		break;
	case MPH_INFORMATION_REQ:
		ret = hfcsusb_ph_info(hw);
		break;
	}

	return ret;
}

/*
 * Layer 1 callback function
 */
static int
hfc_l1callback(struct dchannel *dch, u_int cmd)
{
	struct hfcsusb *hw = dch->hw;

	if (debug & DBG_HFC_CALL_TRACE)
		printk(KERN_DEBUG "%s: %s cmd 0x%x\n",
		       hw->name, __func__, cmd);

	switch (cmd) {
	case INFO3_P8:
	case INFO3_P10:
	case HW_RESET_REQ:
	case HW_POWERUP_REQ:
		break;

	case HW_DEACT_REQ:
		skb_queue_purge(&dch->squeue);
		if (dch->tx_skb) {
			dev_kfree_skb(dch->tx_skb);
			dch->tx_skb = NULL;
		}
		dch->tx_idx = 0;
		if (dch->rx_skb) {
			dev_kfree_skb(dch->rx_skb);
			dch->rx_skb = NULL;
		}
		test_and_clear_bit(FLG_TX_BUSY, &dch->Flags);
		break;
	case PH_ACTIVATE_IND:
		test_and_set_bit(FLG_ACTIVE, &dch->Flags);
		_queue_data(&dch->dev.D, cmd, MISDN_ID_ANY, 0, NULL,
			    GFP_ATOMIC);
		break;
	case PH_DEACTIVATE_IND:
		test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
		_queue_data(&dch->dev.D, cmd, MISDN_ID_ANY, 0, NULL,
			    GFP_ATOMIC);
		break;
	default:
		if (dch->debug & DEBUG_HW)
			printk(KERN_DEBUG "%s: %s: unknown cmd %x\n",
			       hw->name, __func__, cmd);
		return -1;
	}
	return hfcsusb_ph_info(hw);
}

static int
open_dchannel(struct hfcsusb *hw, struct mISDNchannel *ch,
	      struct channel_req *rq)
{
	int err = 0;

	if (debug & DEBUG_HW_OPEN)
		printk(KERN_DEBUG "%s: %s: dev(%d) open addr(%i) from %p\n",
		       hw->name, __func__, hw->dch.dev.id, rq->adr.channel,
		       __builtin_return_address(0));
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;

	test_and_clear_bit(FLG_ACTIVE, &hw->dch.Flags);
	test_and_clear_bit(FLG_ACTIVE, &hw->ech.Flags);
	hfcsusb_start_endpoint(hw, HFC_CHAN_D);

	/* E-Channel logging */
	if (rq->adr.channel == 1) {
		if (hw->fifos[HFCUSB_PCM_RX].pipe) {
			hfcsusb_start_endpoint(hw, HFC_CHAN_E);
			set_bit(FLG_ACTIVE, &hw->ech.Flags);
			_queue_data(&hw->ech.dev.D, PH_ACTIVATE_IND,
				    MISDN_ID_ANY, 0, NULL, GFP_ATOMIC);
		} else
			return -EINVAL;
	}

	if (!hw->initdone) {
		hw->protocol = rq->protocol;
		if (rq->protocol == ISDN_P_TE_S0) {
			err = create_l1(&hw->dch, hfc_l1callback);
			if (err)
				return err;
		}
		setPortMode(hw);
		ch->protocol = rq->protocol;
		hw->initdone = 1;
	} else {
		if (rq->protocol != ch->protocol)
			return -EPROTONOSUPPORT;
	}

	if (((ch->protocol == ISDN_P_NT_S0) && (hw->dch.state == 3)) ||
	    ((ch->protocol == ISDN_P_TE_S0) && (hw->dch.state == 7)))
		_queue_data(ch, PH_ACTIVATE_IND, MISDN_ID_ANY,
			    0, NULL, GFP_KERNEL);
	rq->ch = ch;
	if (!try_module_get(THIS_MODULE))
		printk(KERN_WARNING "%s: %s: cannot get module\n",
		       hw->name, __func__);
	return 0;
}

static int
open_bchannel(struct hfcsusb *hw, struct channel_req *rq)
{
	struct bchannel		*bch;

	if (rq->adr.channel == 0 || rq->adr.channel > 2)
		return -EINVAL;
	if (rq->protocol == ISDN_P_NONE)
		return -EINVAL;

	if (debug & DBG_HFC_CALL_TRACE)
		printk(KERN_DEBUG "%s: %s B%i\n",
		       hw->name, __func__, rq->adr.channel);

	bch = &hw->bch[rq->adr.channel - 1];
	if (test_and_set_bit(FLG_OPEN, &bch->Flags))
		return -EBUSY; /* b-channel can be only open once */
	bch->ch.protocol = rq->protocol;
	rq->ch = &bch->ch;

	if (!try_module_get(THIS_MODULE))
		printk(KERN_WARNING "%s: %s:cannot get module\n",
		       hw->name, __func__);
	return 0;
}

static int
channel_ctrl(struct hfcsusb *hw, struct mISDN_ctrl_req *cq)
{
	int ret = 0;

	if (debug & DBG_HFC_CALL_TRACE)
		printk(KERN_DEBUG "%s: %s op(0x%x) channel(0x%x)\n",
		       hw->name, __func__, (cq->op), (cq->channel));

	switch (cq->op) {
	case MISDN_CTRL_GETOP:
		cq->op = MISDN_CTRL_LOOP | MISDN_CTRL_CONNECT |
			MISDN_CTRL_DISCONNECT;
		break;
	default:
		printk(KERN_WARNING "%s: %s: unknown Op %x\n",
		       hw->name, __func__, cq->op);
		ret = -EINVAL;
		break;
	}
	return ret;
}

/*
 * device control function
 */
static int
hfc_dctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct mISDNdevice	*dev = container_of(ch, struct mISDNdevice, D);
	struct dchannel		*dch = container_of(dev, struct dchannel, dev);
	struct hfcsusb		*hw = dch->hw;
	struct channel_req	*rq;
	int			err = 0;

	if (dch->debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: %s: cmd:%x %p\n",
		       hw->name, __func__, cmd, arg);
	switch (cmd) {
	case OPEN_CHANNEL:
		rq = arg;
		if ((rq->protocol == ISDN_P_TE_S0) ||
		    (rq->protocol == ISDN_P_NT_S0))
			err = open_dchannel(hw, ch, rq);
		else
			err = open_bchannel(hw, rq);
		if (!err)
			hw->open++;
		break;
	case CLOSE_CHANNEL:
		hw->open--;
		if (debug & DEBUG_HW_OPEN)
			printk(KERN_DEBUG
			       "%s: %s: dev(%d) close from %p (open %d)\n",
			       hw->name, __func__, hw->dch.dev.id,
			       __builtin_return_address(0), hw->open);
		if (!hw->open) {
			hfcsusb_stop_endpoint(hw, HFC_CHAN_D);
			if (hw->fifos[HFCUSB_PCM_RX].pipe)
				hfcsusb_stop_endpoint(hw, HFC_CHAN_E);
			handle_led(hw, LED_POWER_ON);
		}
		module_put(THIS_MODULE);
		break;
	case CONTROL_CHANNEL:
		err = channel_ctrl(hw, arg);
		break;
	default:
		if (dch->debug & DEBUG_HW)
			printk(KERN_DEBUG "%s: %s: unknown command %x\n",
			       hw->name, __func__, cmd);
		return -EINVAL;
	}
	return err;
}

/*
 * S0 TE state change event handler
 */
static void
ph_state_te(struct dchannel *dch)
{
	struct hfcsusb *hw = dch->hw;

	if (debug & DEBUG_HW) {
		if (dch->state <= HFC_MAX_TE_LAYER1_STATE)
			printk(KERN_DEBUG "%s: %s: %s\n", hw->name, __func__,
			       HFC_TE_LAYER1_STATES[dch->state]);
		else
			printk(KERN_DEBUG "%s: %s: TE F%d\n",
			       hw->name, __func__, dch->state);
	}

	switch (dch->state) {
	case 0:
		l1_event(dch->l1, HW_RESET_IND);
		break;
	case 3:
		l1_event(dch->l1, HW_DEACT_IND);
		break;
	case 5:
	case 8:
		l1_event(dch->l1, ANYSIGNAL);
		break;
	case 6:
		l1_event(dch->l1, INFO2);
		break;
	case 7:
		l1_event(dch->l1, INFO4_P8);
		break;
	}
	if (dch->state == 7)
		handle_led(hw, LED_S0_ON);
	else
		handle_led(hw, LED_S0_OFF);
}

/*
 * S0 NT state change event handler
 */
static void
ph_state_nt(struct dchannel *dch)
{
	struct hfcsusb *hw = dch->hw;

	if (debug & DEBUG_HW) {
		if (dch->state <= HFC_MAX_NT_LAYER1_STATE)
			printk(KERN_DEBUG "%s: %s: %s\n",
			       hw->name, __func__,
			       HFC_NT_LAYER1_STATES[dch->state]);

		else
			printk(KERN_INFO DRIVER_NAME "%s: %s: NT G%d\n",
			       hw->name, __func__, dch->state);
	}

	switch (dch->state) {
	case (1):
		test_and_clear_bit(FLG_ACTIVE, &dch->Flags);
		test_and_clear_bit(FLG_L2_ACTIVATED, &dch->Flags);
		hw->nt_timer = 0;
		hw->timers &= ~NT_ACTIVATION_TIMER;
		handle_led(hw, LED_S0_OFF);
		break;

	case (2):
		if (hw->nt_timer < 0) {
			hw->nt_timer = 0;
			hw->timers &= ~NT_ACTIVATION_TIMER;
			hfcsusb_ph_command(dch->hw, HFC_L1_DEACTIVATE_NT);
		} else {
			hw->timers |= NT_ACTIVATION_TIMER;
			hw->nt_timer = NT_T1_COUNT;
			/* allow G2 -> G3 transition */
			write_reg(hw, HFCUSB_STATES, 2 | HFCUSB_NT_G2_G3);
		}
		break;
	case (3):
		hw->nt_timer = 0;
		hw->timers &= ~NT_ACTIVATION_TIMER;
		test_and_set_bit(FLG_ACTIVE, &dch->Flags);
		_queue_data(&dch->dev.D, PH_ACTIVATE_IND,
			    MISDN_ID_ANY, 0, NULL, GFP_ATOMIC);
		handle_led(hw, LED_S0_ON);
		break;
	case (4):
		hw->nt_timer = 0;
		hw->timers &= ~NT_ACTIVATION_TIMER;
		break;
	default:
		break;
	}
	hfcsusb_ph_info(hw);
}

static void
ph_state(struct dchannel *dch)
{
	struct hfcsusb *hw = dch->hw;

	if (hw->protocol == ISDN_P_NT_S0)
		ph_state_nt(dch);
	else if (hw->protocol == ISDN_P_TE_S0)
		ph_state_te(dch);
}

/*
 * disable/enable BChannel for desired protocoll
 */
static int
hfcsusb_setup_bch(struct bchannel *bch, int protocol)
{
	struct hfcsusb *hw = bch->hw;
	__u8 conhdlc, sctrl, sctrl_r;

	if (debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: %s: protocol %x-->%x B%d\n",
		       hw->name, __func__, bch->state, protocol,
		       bch->nr);

	/* setup val for CON_HDLC */
	conhdlc = 0;
	if (protocol > ISDN_P_NONE)
		conhdlc = 8;	/* enable FIFO */

	switch (protocol) {
	case (-1):	/* used for init */
		bch->state = -1;
		fallthrough;
	case (ISDN_P_NONE):
		if (bch->state == ISDN_P_NONE)
			return 0; /* already in idle state */
		bch->state = ISDN_P_NONE;
		clear_bit(FLG_HDLC, &bch->Flags);
		clear_bit(FLG_TRANSPARENT, &bch->Flags);
		break;
	case (ISDN_P_B_RAW):
		conhdlc |= 2;
		bch->state = protocol;
		set_bit(FLG_TRANSPARENT, &bch->Flags);
		break;
	case (ISDN_P_B_HDLC):
		bch->state = protocol;
		set_bit(FLG_HDLC, &bch->Flags);
		break;
	default:
		if (debug & DEBUG_HW)
			printk(KERN_DEBUG "%s: %s: prot not known %x\n",
			       hw->name, __func__, protocol);
		return -ENOPROTOOPT;
	}

	if (protocol >= ISDN_P_NONE) {
		write_reg(hw, HFCUSB_FIFO, (bch->nr == 1) ? 0 : 2);
		write_reg(hw, HFCUSB_CON_HDLC, conhdlc);
		write_reg(hw, HFCUSB_INC_RES_F, 2);
		write_reg(hw, HFCUSB_FIFO, (bch->nr == 1) ? 1 : 3);
		write_reg(hw, HFCUSB_CON_HDLC, conhdlc);
		write_reg(hw, HFCUSB_INC_RES_F, 2);

		sctrl = 0x40 + ((hw->protocol == ISDN_P_TE_S0) ? 0x00 : 0x04);
		sctrl_r = 0x0;
		if (test_bit(FLG_ACTIVE, &hw->bch[0].Flags)) {
			sctrl |= 1;
			sctrl_r |= 1;
		}
		if (test_bit(FLG_ACTIVE, &hw->bch[1].Flags)) {
			sctrl |= 2;
			sctrl_r |= 2;
		}
		write_reg(hw, HFCUSB_SCTRL, sctrl);
		write_reg(hw, HFCUSB_SCTRL_R, sctrl_r);

		if (protocol > ISDN_P_NONE)
			handle_led(hw, (bch->nr == 1) ? LED_B1_ON : LED_B2_ON);
		else
			handle_led(hw, (bch->nr == 1) ? LED_B1_OFF :
				   LED_B2_OFF);
	}
	return hfcsusb_ph_info(hw);
}

static void
hfcsusb_ph_command(struct hfcsusb *hw, u_char command)
{
	if (debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: %s: %x\n",
		       hw->name, __func__, command);

	switch (command) {
	case HFC_L1_ACTIVATE_TE:
		/* force sending sending INFO1 */
		write_reg(hw, HFCUSB_STATES, 0x14);
		/* start l1 activation */
		write_reg(hw, HFCUSB_STATES, 0x04);
		break;

	case HFC_L1_FORCE_DEACTIVATE_TE:
		write_reg(hw, HFCUSB_STATES, 0x10);
		write_reg(hw, HFCUSB_STATES, 0x03);
		break;

	case HFC_L1_ACTIVATE_NT:
		if (hw->dch.state == 3)
			_queue_data(&hw->dch.dev.D, PH_ACTIVATE_IND,
				    MISDN_ID_ANY, 0, NULL, GFP_ATOMIC);
		else
			write_reg(hw, HFCUSB_STATES, HFCUSB_ACTIVATE |
				  HFCUSB_DO_ACTION | HFCUSB_NT_G2_G3);
		break;

	case HFC_L1_DEACTIVATE_NT:
		write_reg(hw, HFCUSB_STATES,
			  HFCUSB_DO_ACTION);
		break;
	}
}

/*
 * Layer 1 B-channel hardware access
 */
static int
channel_bctrl(struct bchannel *bch, struct mISDN_ctrl_req *cq)
{
	return mISDN_ctrl_bchannel(bch, cq);
}

/* collect data from incoming interrupt or isochron USB data */
static void
hfcsusb_rx_frame(struct usb_fifo *fifo, __u8 *data, unsigned int len,
		 int finish)
{
	struct hfcsusb	*hw = fifo->hw;
	struct sk_buff	*rx_skb = NULL;
	int		maxlen = 0;
	int		fifon = fifo->fifonum;
	int		i;
	int		hdlc = 0;
	unsigned long	flags;

	if (debug & DBG_HFC_CALL_TRACE)
		printk(KERN_DEBUG "%s: %s: fifo(%i) len(%i) "
		       "dch(%p) bch(%p) ech(%p)\n",
		       hw->name, __func__, fifon, len,
		       fifo->dch, fifo->bch, fifo->ech);

	if (!len)
		return;

	if ((!!fifo->dch + !!fifo->bch + !!fifo->ech) != 1) {
		printk(KERN_DEBUG "%s: %s: undefined channel\n",
		       hw->name, __func__);
		return;
	}

	spin_lock_irqsave(&hw->lock, flags);
	if (fifo->dch) {
		rx_skb = fifo->dch->rx_skb;
		maxlen = fifo->dch->maxlen;
		hdlc = 1;
	}
	if (fifo->bch) {
		if (test_bit(FLG_RX_OFF, &fifo->bch->Flags)) {
			fifo->bch->dropcnt += len;
			spin_unlock_irqrestore(&hw->lock, flags);
			return;
		}
		maxlen = bchannel_get_rxbuf(fifo->bch, len);
		rx_skb = fifo->bch->rx_skb;
		if (maxlen < 0) {
			if (rx_skb)
				skb_trim(rx_skb, 0);
			pr_warn("%s.B%d: No bufferspace for %d bytes\n",
				hw->name, fifo->bch->nr, len);
			spin_unlock_irqrestore(&hw->lock, flags);
			return;
		}
		maxlen = fifo->bch->maxlen;
		hdlc = test_bit(FLG_HDLC, &fifo->bch->Flags);
	}
	if (fifo->ech) {
		rx_skb = fifo->ech->rx_skb;
		maxlen = fifo->ech->maxlen;
		hdlc = 1;
	}

	if (fifo->dch || fifo->ech) {
		if (!rx_skb) {
			rx_skb = mI_alloc_skb(maxlen, GFP_ATOMIC);
			if (rx_skb) {
				if (fifo->dch)
					fifo->dch->rx_skb = rx_skb;
				if (fifo->ech)
					fifo->ech->rx_skb = rx_skb;
				skb_trim(rx_skb, 0);
			} else {
				printk(KERN_DEBUG "%s: %s: No mem for rx_skb\n",
				       hw->name, __func__);
				spin_unlock_irqrestore(&hw->lock, flags);
				return;
			}
		}
		/* D/E-Channel SKB range check */
		if ((rx_skb->len + len) >= MAX_DFRAME_LEN_L1) {
			printk(KERN_DEBUG "%s: %s: sbk mem exceeded "
			       "for fifo(%d) HFCUSB_D_RX\n",
			       hw->name, __func__, fifon);
			skb_trim(rx_skb, 0);
			spin_unlock_irqrestore(&hw->lock, flags);
			return;
		}
	}

	skb_put_data(rx_skb, data, len);

	if (hdlc) {
		/* we have a complete hdlc packet */
		if (finish) {
			if ((rx_skb->len > 3) &&
			    (!(rx_skb->data[rx_skb->len - 1]))) {
				if (debug & DBG_HFC_FIFO_VERBOSE) {
					printk(KERN_DEBUG "%s: %s: fifon(%i)"
					       " new RX len(%i): ",
					       hw->name, __func__, fifon,
					       rx_skb->len);
					i = 0;
					while (i < rx_skb->len)
						printk("%02x ",
						       rx_skb->data[i++]);
					printk("\n");
				}

				/* remove CRC & status */
				skb_trim(rx_skb, rx_skb->len - 3);

				if (fifo->dch)
					recv_Dchannel(fifo->dch);
				if (fifo->bch)
					recv_Bchannel(fifo->bch, MISDN_ID_ANY,
						      0);
				if (fifo->ech)
					recv_Echannel(fifo->ech,
						      &hw->dch);
			} else {
				if (debug & DBG_HFC_FIFO_VERBOSE) {
					printk(KERN_DEBUG
					       "%s: CRC or minlen ERROR fifon(%i) "
					       "RX len(%i): ",
					       hw->name, fifon, rx_skb->len);
					i = 0;
					while (i < rx_skb->len)
						printk("%02x ",
						       rx_skb->data[i++]);
					printk("\n");
				}
				skb_trim(rx_skb, 0);
			}
		}
	} else {
		/* deliver transparent data to layer2 */
		recv_Bchannel(fifo->bch, MISDN_ID_ANY, false);
	}
	spin_unlock_irqrestore(&hw->lock, flags);
}

static void
fill_isoc_urb(struct urb *urb, struct usb_device *dev, unsigned int pipe,
	      void *buf, int num_packets, int packet_size, int interval,
	      usb_complete_t complete, void *context)
{
	int k;

	usb_fill_bulk_urb(urb, dev, pipe, buf, packet_size * num_packets,
			  complete, context);

	urb->number_of_packets = num_packets;
	urb->transfer_flags = URB_ISO_ASAP;
	urb->actual_length = 0;
	urb->interval = interval;

	for (k = 0; k < num_packets; k++) {
		urb->iso_frame_desc[k].offset = packet_size * k;
		urb->iso_frame_desc[k].length = packet_size;
		urb->iso_frame_desc[k].actual_length = 0;
	}
}

/* receive completion routine for all ISO tx fifos   */
static void
rx_iso_complete(struct urb *urb)
{
	struct iso_urb *context_iso_urb = (struct iso_urb *) urb->context;
	struct usb_fifo *fifo = context_iso_urb->owner_fifo;
	struct hfcsusb *hw = fifo->hw;
	int k, len, errcode, offset, num_isoc_packets, fifon, maxlen,
		status, iso_status, i;
	__u8 *buf;
	static __u8 eof[8];
	__u8 s0_state;
	unsigned long flags;

	fifon = fifo->fifonum;
	status = urb->status;

	spin_lock_irqsave(&hw->lock, flags);
	if (fifo->stop_gracefull) {
		fifo->stop_gracefull = 0;
		fifo->active = 0;
		spin_unlock_irqrestore(&hw->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&hw->lock, flags);

	/*
	 * ISO transfer only partially completed,
	 * look at individual frame status for details
	 */
	if (status == -EXDEV) {
		if (debug & DEBUG_HW)
			printk(KERN_DEBUG "%s: %s: with -EXDEV "
			       "urb->status %d, fifonum %d\n",
			       hw->name, __func__,  status, fifon);

		/* clear status, so go on with ISO transfers */
		status = 0;
	}

	s0_state = 0;
	if (fifo->active && !status) {
		num_isoc_packets = iso_packets[fifon];
		maxlen = fifo->usb_packet_maxlen;

		for (k = 0; k < num_isoc_packets; ++k) {
			len = urb->iso_frame_desc[k].actual_length;
			offset = urb->iso_frame_desc[k].offset;
			buf = context_iso_urb->buffer + offset;
			iso_status = urb->iso_frame_desc[k].status;

			if (iso_status && (debug & DBG_HFC_FIFO_VERBOSE)) {
				printk(KERN_DEBUG "%s: %s: "
				       "ISO packet %i, status: %i\n",
				       hw->name, __func__, k, iso_status);
			}

			/* USB data log for every D ISO in */
			if ((fifon == HFCUSB_D_RX) &&
			    (debug & DBG_HFC_USB_VERBOSE)) {
				printk(KERN_DEBUG
				       "%s: %s: %d (%d/%d) len(%d) ",
				       hw->name, __func__, urb->start_frame,
				       k, num_isoc_packets - 1,
				       len);
				for (i = 0; i < len; i++)
					printk("%x ", buf[i]);
				printk("\n");
			}

			if (!iso_status) {
				if (fifo->last_urblen != maxlen) {
					/*
					 * save fifo fill-level threshold bits
					 * to use them later in TX ISO URB
					 * completions
					 */
					hw->threshold_mask = buf[1];

					if (fifon == HFCUSB_D_RX)
						s0_state = (buf[0] >> 4);

					eof[fifon] = buf[0] & 1;
					if (len > 2)
						hfcsusb_rx_frame(fifo, buf + 2,
								 len - 2, (len < maxlen)
								 ? eof[fifon] : 0);
				} else
					hfcsusb_rx_frame(fifo, buf, len,
							 (len < maxlen) ?
							 eof[fifon] : 0);
				fifo->last_urblen = len;
			}
		}

		/* signal S0 layer1 state change */
		if ((s0_state) && (hw->initdone) &&
		    (s0_state != hw->dch.state)) {
			hw->dch.state = s0_state;
			schedule_event(&hw->dch, FLG_PHCHANGE);
		}

		fill_isoc_urb(urb, fifo->hw->dev, fifo->pipe,
			      context_iso_urb->buffer, num_isoc_packets,
			      fifo->usb_packet_maxlen, fifo->intervall,
			      (usb_complete_t)rx_iso_complete, urb->context);
		errcode = usb_submit_urb(urb, GFP_ATOMIC);
		if (errcode < 0) {
			if (debug & DEBUG_HW)
				printk(KERN_DEBUG "%s: %s: error submitting "
				       "ISO URB: %d\n",
				       hw->name, __func__, errcode);
		}
	} else {
		if (status && (debug & DBG_HFC_URB_INFO))
			printk(KERN_DEBUG "%s: %s: rx_iso_complete : "
			       "urb->status %d, fifonum %d\n",
			       hw->name, __func__, status, fifon);
	}
}

/* receive completion routine for all interrupt rx fifos */
static void
rx_int_complete(struct urb *urb)
{
	int len, status, i;
	__u8 *buf, maxlen, fifon;
	struct usb_fifo *fifo = (struct usb_fifo *) urb->context;
	struct hfcsusb *hw = fifo->hw;
	static __u8 eof[8];
	unsigned long flags;

	spin_lock_irqsave(&hw->lock, flags);
	if (fifo->stop_gracefull) {
		fifo->stop_gracefull = 0;
		fifo->active = 0;
		spin_unlock_irqrestore(&hw->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&hw->lock, flags);

	fifon = fifo->fifonum;
	if ((!fifo->active) || (urb->status)) {
		if (debug & DBG_HFC_URB_ERROR)
			printk(KERN_DEBUG
			       "%s: %s: RX-Fifo %i is going down (%i)\n",
			       hw->name, __func__, fifon, urb->status);

		fifo->urb->interval = 0; /* cancel automatic rescheduling */
		return;
	}
	len = urb->actual_length;
	buf = fifo->buffer;
	maxlen = fifo->usb_packet_maxlen;

	/* USB data log for every D INT in */
	if ((fifon == HFCUSB_D_RX) && (debug & DBG_HFC_USB_VERBOSE)) {
		printk(KERN_DEBUG "%s: %s: D RX INT len(%d) ",
		       hw->name, __func__, len);
		for (i = 0; i < len; i++)
			printk("%02x ", buf[i]);
		printk("\n");
	}

	if (fifo->last_urblen != fifo->usb_packet_maxlen) {
		/* the threshold mask is in the 2nd status byte */
		hw->threshold_mask = buf[1];

		/* signal S0 layer1 state change */
		if (hw->initdone && ((buf[0] >> 4) != hw->dch.state)) {
			hw->dch.state = (buf[0] >> 4);
			schedule_event(&hw->dch, FLG_PHCHANGE);
		}

		eof[fifon] = buf[0] & 1;
		/* if we have more than the 2 status bytes -> collect data */
		if (len > 2)
			hfcsusb_rx_frame(fifo, buf + 2,
					 urb->actual_length - 2,
					 (len < maxlen) ? eof[fifon] : 0);
	} else {
		hfcsusb_rx_frame(fifo, buf, urb->actual_length,
				 (len < maxlen) ? eof[fifon] : 0);
	}
	fifo->last_urblen = urb->actual_length;

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		if (debug & DEBUG_HW)
			printk(KERN_DEBUG "%s: %s: error resubmitting USB\n",
			       hw->name, __func__);
	}
}

/* transmit completion routine for all ISO tx fifos */
static void
tx_iso_complete(struct urb *urb)
{
	struct iso_urb *context_iso_urb = (struct iso_urb *) urb->context;
	struct usb_fifo *fifo = context_iso_urb->owner_fifo;
	struct hfcsusb *hw = fifo->hw;
	struct sk_buff *tx_skb;
	int k, tx_offset, num_isoc_packets, sink, remain, current_len,
		errcode, hdlc, i;
	int *tx_idx;
	int frame_complete, fifon, status, fillempty = 0;
	__u8 threshbit, *p;
	unsigned long flags;

	spin_lock_irqsave(&hw->lock, flags);
	if (fifo->stop_gracefull) {
		fifo->stop_gracefull = 0;
		fifo->active = 0;
		spin_unlock_irqrestore(&hw->lock, flags);
		return;
	}

	if (fifo->dch) {
		tx_skb = fifo->dch->tx_skb;
		tx_idx = &fifo->dch->tx_idx;
		hdlc = 1;
	} else if (fifo->bch) {
		tx_skb = fifo->bch->tx_skb;
		tx_idx = &fifo->bch->tx_idx;
		hdlc = test_bit(FLG_HDLC, &fifo->bch->Flags);
		if (!tx_skb && !hdlc &&
		    test_bit(FLG_FILLEMPTY, &fifo->bch->Flags))
			fillempty = 1;
	} else {
		printk(KERN_DEBUG "%s: %s: neither BCH nor DCH\n",
		       hw->name, __func__);
		spin_unlock_irqrestore(&hw->lock, flags);
		return;
	}

	fifon = fifo->fifonum;
	status = urb->status;

	tx_offset = 0;

	/*
	 * ISO transfer only partially completed,
	 * look at individual frame status for details
	 */
	if (status == -EXDEV) {
		if (debug & DBG_HFC_URB_ERROR)
			printk(KERN_DEBUG "%s: %s: "
			       "-EXDEV (%i) fifon (%d)\n",
			       hw->name, __func__, status, fifon);

		/* clear status, so go on with ISO transfers */
		status = 0;
	}

	if (fifo->active && !status) {
		/* is FifoFull-threshold set for our channel? */
		threshbit = (hw->threshold_mask & (1 << fifon));
		num_isoc_packets = iso_packets[fifon];

		/* predict dataflow to avoid fifo overflow */
		if (fifon >= HFCUSB_D_TX)
			sink = (threshbit) ? SINK_DMIN : SINK_DMAX;
		else
			sink = (threshbit) ? SINK_MIN : SINK_MAX;
		fill_isoc_urb(urb, fifo->hw->dev, fifo->pipe,
			      context_iso_urb->buffer, num_isoc_packets,
			      fifo->usb_packet_maxlen, fifo->intervall,
			      (usb_complete_t)tx_iso_complete, urb->context);
		memset(context_iso_urb->buffer, 0,
		       sizeof(context_iso_urb->buffer));
		frame_complete = 0;

		for (k = 0; k < num_isoc_packets; ++k) {
			/* analyze tx success of previous ISO packets */
			if (debug & DBG_HFC_URB_ERROR) {
				errcode = urb->iso_frame_desc[k].status;
				if (errcode) {
					printk(KERN_DEBUG "%s: %s: "
					       "ISO packet %i, status: %i\n",
					       hw->name, __func__, k, errcode);
				}
			}

			/* Generate next ISO Packets */
			if (tx_skb)
				remain = tx_skb->len - *tx_idx;
			else if (fillempty)
				remain = 15; /* > not complete */
			else
				remain = 0;

			if (remain > 0) {
				fifo->bit_line -= sink;
				current_len = (0 - fifo->bit_line) / 8;
				if (current_len > 14)
					current_len = 14;
				if (current_len < 0)
					current_len = 0;
				if (remain < current_len)
					current_len = remain;

				/* how much bit do we put on the line? */
				fifo->bit_line += current_len * 8;

				context_iso_urb->buffer[tx_offset] = 0;
				if (current_len == remain) {
					if (hdlc) {
						/* signal frame completion */
						context_iso_urb->
							buffer[tx_offset] = 1;
						/* add 2 byte flags and 16bit
						 * CRC at end of ISDN frame */
						fifo->bit_line += 32;
					}
					frame_complete = 1;
				}

				/* copy tx data to iso-urb buffer */
				p = context_iso_urb->buffer + tx_offset + 1;
				if (fillempty) {
					memset(p, fifo->bch->fill[0],
					       current_len);
				} else {
					memcpy(p, (tx_skb->data + *tx_idx),
					       current_len);
					*tx_idx += current_len;
				}
				urb->iso_frame_desc[k].offset = tx_offset;
				urb->iso_frame_desc[k].length = current_len + 1;

				/* USB data log for every D ISO out */
				if ((fifon == HFCUSB_D_RX) && !fillempty &&
				    (debug & DBG_HFC_USB_VERBOSE)) {
					printk(KERN_DEBUG
					       "%s: %s (%d/%d) offs(%d) len(%d) ",
					       hw->name, __func__,
					       k, num_isoc_packets - 1,
					       urb->iso_frame_desc[k].offset,
					       urb->iso_frame_desc[k].length);

					for (i = urb->iso_frame_desc[k].offset;
					     i < (urb->iso_frame_desc[k].offset
						  + urb->iso_frame_desc[k].length);
					     i++)
						printk("%x ",
						       context_iso_urb->buffer[i]);

					printk(" skb->len(%i) tx-idx(%d)\n",
					       tx_skb->len, *tx_idx);
				}

				tx_offset += (current_len + 1);
			} else {
				urb->iso_frame_desc[k].offset = tx_offset++;
				urb->iso_frame_desc[k].length = 1;
				/* we lower data margin every msec */
				fifo->bit_line -= sink;
				if (fifo->bit_line < BITLINE_INF)
					fifo->bit_line = BITLINE_INF;
			}

			if (frame_complete) {
				frame_complete = 0;

				if (debug & DBG_HFC_FIFO_VERBOSE) {
					printk(KERN_DEBUG  "%s: %s: "
					       "fifon(%i) new TX len(%i): ",
					       hw->name, __func__,
					       fifon, tx_skb->len);
					i = 0;
					while (i < tx_skb->len)
						printk("%02x ",
						       tx_skb->data[i++]);
					printk("\n");
				}

				dev_consume_skb_irq(tx_skb);
				tx_skb = NULL;
				if (fifo->dch && get_next_dframe(fifo->dch))
					tx_skb = fifo->dch->tx_skb;
				else if (fifo->bch &&
					 get_next_bframe(fifo->bch))
					tx_skb = fifo->bch->tx_skb;
			}
		}
		errcode = usb_submit_urb(urb, GFP_ATOMIC);
		if (errcode < 0) {
			if (debug & DEBUG_HW)
				printk(KERN_DEBUG
				       "%s: %s: error submitting ISO URB: %d \n",
				       hw->name, __func__, errcode);
		}

		/*
		 * abuse DChannel tx iso completion to trigger NT mode state
		 * changes tx_iso_complete is assumed to be called every
		 * fifo->intervall (ms)
		 */
		if ((fifon == HFCUSB_D_TX) && (hw->protocol == ISDN_P_NT_S0)
		    && (hw->timers & NT_ACTIVATION_TIMER)) {
			if ((--hw->nt_timer) < 0)
				schedule_event(&hw->dch, FLG_PHCHANGE);
		}

	} else {
		if (status && (debug & DBG_HFC_URB_ERROR))
			printk(KERN_DEBUG  "%s: %s: urb->status %s (%i)"
			       "fifonum=%d\n",
			       hw->name, __func__,
			       symbolic(urb_errlist, status), status, fifon);
	}
	spin_unlock_irqrestore(&hw->lock, flags);
}

/*
 * allocs urbs and start isoc transfer with two pending urbs to avoid
 * gaps in the transfer chain
 */
static int
start_isoc_chain(struct usb_fifo *fifo, int num_packets_per_urb,
		 usb_complete_t complete, int packet_size)
{
	struct hfcsusb *hw = fifo->hw;
	int i, k, errcode;

	if (debug)
		printk(KERN_DEBUG "%s: %s: fifo %i\n",
		       hw->name, __func__, fifo->fifonum);

	/* allocate Memory for Iso out Urbs */
	for (i = 0; i < 2; i++) {
		if (!(fifo->iso[i].urb)) {
			fifo->iso[i].urb =
				usb_alloc_urb(num_packets_per_urb, GFP_KERNEL);
			if (!(fifo->iso[i].urb)) {
				printk(KERN_DEBUG
				       "%s: %s: alloc urb for fifo %i failed",
				       hw->name, __func__, fifo->fifonum);
				continue;
			}
			fifo->iso[i].owner_fifo = (struct usb_fifo *) fifo;
			fifo->iso[i].indx = i;

			/* Init the first iso */
			if (ISO_BUFFER_SIZE >=
			    (fifo->usb_packet_maxlen *
			     num_packets_per_urb)) {
				fill_isoc_urb(fifo->iso[i].urb,
					      fifo->hw->dev, fifo->pipe,
					      fifo->iso[i].buffer,
					      num_packets_per_urb,
					      fifo->usb_packet_maxlen,
					      fifo->intervall, complete,
					      &fifo->iso[i]);
				memset(fifo->iso[i].buffer, 0,
				       sizeof(fifo->iso[i].buffer));

				for (k = 0; k < num_packets_per_urb; k++) {
					fifo->iso[i].urb->
						iso_frame_desc[k].offset =
						k * packet_size;
					fifo->iso[i].urb->
						iso_frame_desc[k].length =
						packet_size;
				}
			} else {
				printk(KERN_DEBUG
				       "%s: %s: ISO Buffer size to small!\n",
				       hw->name, __func__);
			}
		}
		fifo->bit_line = BITLINE_INF;

		errcode = usb_submit_urb(fifo->iso[i].urb, GFP_KERNEL);
		fifo->active = (errcode >= 0) ? 1 : 0;
		fifo->stop_gracefull = 0;
		if (errcode < 0) {
			printk(KERN_DEBUG "%s: %s: %s URB nr:%d\n",
			       hw->name, __func__,
			       symbolic(urb_errlist, errcode), i);
		}
	}
	return fifo->active;
}

static void
stop_iso_gracefull(struct usb_fifo *fifo)
{
	struct hfcsusb *hw = fifo->hw;
	int i, timeout;
	u_long flags;

	for (i = 0; i < 2; i++) {
		spin_lock_irqsave(&hw->lock, flags);
		if (debug)
			printk(KERN_DEBUG "%s: %s for fifo %i.%i\n",
			       hw->name, __func__, fifo->fifonum, i);
		fifo->stop_gracefull = 1;
		spin_unlock_irqrestore(&hw->lock, flags);
	}

	for (i = 0; i < 2; i++) {
		timeout = 3;
		while (fifo->stop_gracefull && timeout--)
			schedule_timeout_interruptible((HZ / 1000) * 16);
		if (debug && fifo->stop_gracefull)
			printk(KERN_DEBUG "%s: ERROR %s for fifo %i.%i\n",
			       hw->name, __func__, fifo->fifonum, i);
	}
}

static void
stop_int_gracefull(struct usb_fifo *fifo)
{
	struct hfcsusb *hw = fifo->hw;
	int timeout;
	u_long flags;

	spin_lock_irqsave(&hw->lock, flags);
	if (debug)
		printk(KERN_DEBUG "%s: %s for fifo %i\n",
		       hw->name, __func__, fifo->fifonum);
	fifo->stop_gracefull = 1;
	spin_unlock_irqrestore(&hw->lock, flags);

	timeout = 3;
	while (fifo->stop_gracefull && timeout--)
		schedule_timeout_interruptible((HZ / 1000) * 3);
	if (debug && fifo->stop_gracefull)
		printk(KERN_DEBUG "%s: ERROR %s for fifo %i\n",
		       hw->name, __func__, fifo->fifonum);
}

/* start the interrupt transfer for the given fifo */
static void
start_int_fifo(struct usb_fifo *fifo)
{
	struct hfcsusb *hw = fifo->hw;
	int errcode;

	if (debug)
		printk(KERN_DEBUG "%s: %s: INT IN fifo:%d\n",
		       hw->name, __func__, fifo->fifonum);

	if (!fifo->urb) {
		fifo->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!fifo->urb)
			return;
	}
	usb_fill_int_urb(fifo->urb, fifo->hw->dev, fifo->pipe,
			 fifo->buffer, fifo->usb_packet_maxlen,
			 (usb_complete_t)rx_int_complete, fifo, fifo->intervall);
	fifo->active = 1;
	fifo->stop_gracefull = 0;
	errcode = usb_submit_urb(fifo->urb, GFP_KERNEL);
	if (errcode) {
		printk(KERN_DEBUG "%s: %s: submit URB: status:%i\n",
		       hw->name, __func__, errcode);
		fifo->active = 0;
	}
}

static void
setPortMode(struct hfcsusb *hw)
{
	if (debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: %s %s\n", hw->name, __func__,
		       (hw->protocol == ISDN_P_TE_S0) ? "TE" : "NT");

	if (hw->protocol == ISDN_P_TE_S0) {
		write_reg(hw, HFCUSB_SCTRL, 0x40);
		write_reg(hw, HFCUSB_SCTRL_E, 0x00);
		write_reg(hw, HFCUSB_CLKDEL, CLKDEL_TE);
		write_reg(hw, HFCUSB_STATES, 3 | 0x10);
		write_reg(hw, HFCUSB_STATES, 3);
	} else {
		write_reg(hw, HFCUSB_SCTRL, 0x44);
		write_reg(hw, HFCUSB_SCTRL_E, 0x09);
		write_reg(hw, HFCUSB_CLKDEL, CLKDEL_NT);
		write_reg(hw, HFCUSB_STATES, 1 | 0x10);
		write_reg(hw, HFCUSB_STATES, 1);
	}
}

static void
reset_hfcsusb(struct hfcsusb *hw)
{
	struct usb_fifo *fifo;
	int i;

	if (debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: %s\n", hw->name, __func__);

	/* do Chip reset */
	write_reg(hw, HFCUSB_CIRM, 8);

	/* aux = output, reset off */
	write_reg(hw, HFCUSB_CIRM, 0x10);

	/* set USB_SIZE to match the wMaxPacketSize for INT or BULK transfers */
	write_reg(hw, HFCUSB_USB_SIZE, (hw->packet_size / 8) |
		  ((hw->packet_size / 8) << 4));

	/* set USB_SIZE_I to match the wMaxPacketSize for ISO transfers */
	write_reg(hw, HFCUSB_USB_SIZE_I, hw->iso_packet_size);

	/* enable PCM/GCI master mode */
	write_reg(hw, HFCUSB_MST_MODE1, 0);	/* set default values */
	write_reg(hw, HFCUSB_MST_MODE0, 1);	/* enable master mode */

	/* init the fifos */
	write_reg(hw, HFCUSB_F_THRES,
		  (HFCUSB_TX_THRESHOLD / 8) | ((HFCUSB_RX_THRESHOLD / 8) << 4));

	fifo = hw->fifos;
	for (i = 0; i < HFCUSB_NUM_FIFOS; i++) {
		write_reg(hw, HFCUSB_FIFO, i);	/* select the desired fifo */
		fifo[i].max_size =
			(i <= HFCUSB_B2_RX) ? MAX_BCH_SIZE : MAX_DFRAME_LEN;
		fifo[i].last_urblen = 0;

		/* set 2 bit for D- & E-channel */
		write_reg(hw, HFCUSB_HDLC_PAR, ((i <= HFCUSB_B2_RX) ? 0 : 2));

		/* enable all fifos */
		if (i == HFCUSB_D_TX)
			write_reg(hw, HFCUSB_CON_HDLC,
				  (hw->protocol == ISDN_P_NT_S0) ? 0x08 : 0x09);
		else
			write_reg(hw, HFCUSB_CON_HDLC, 0x08);
		write_reg(hw, HFCUSB_INC_RES_F, 2); /* reset the fifo */
	}

	write_reg(hw, HFCUSB_SCTRL_R, 0); /* disable both B receivers */
	handle_led(hw, LED_POWER_ON);
}

/* start USB data pipes dependand on device's endpoint configuration */
static void
hfcsusb_start_endpoint(struct hfcsusb *hw, int channel)
{
	/* quick check if endpoint already running */
	if ((channel == HFC_CHAN_D) && (hw->fifos[HFCUSB_D_RX].active))
		return;
	if ((channel == HFC_CHAN_B1) && (hw->fifos[HFCUSB_B1_RX].active))
		return;
	if ((channel == HFC_CHAN_B2) && (hw->fifos[HFCUSB_B2_RX].active))
		return;
	if ((channel == HFC_CHAN_E) && (hw->fifos[HFCUSB_PCM_RX].active))
		return;

	/* start rx endpoints using USB INT IN method */
	if (hw->cfg_used == CNF_3INT3ISO || hw->cfg_used == CNF_4INT3ISO)
		start_int_fifo(hw->fifos + channel * 2 + 1);

	/* start rx endpoints using USB ISO IN method */
	if (hw->cfg_used == CNF_3ISO3ISO || hw->cfg_used == CNF_4ISO3ISO) {
		switch (channel) {
		case HFC_CHAN_D:
			start_isoc_chain(hw->fifos + HFCUSB_D_RX,
					 ISOC_PACKETS_D,
					 (usb_complete_t)rx_iso_complete,
					 16);
			break;
		case HFC_CHAN_E:
			start_isoc_chain(hw->fifos + HFCUSB_PCM_RX,
					 ISOC_PACKETS_D,
					 (usb_complete_t)rx_iso_complete,
					 16);
			break;
		case HFC_CHAN_B1:
			start_isoc_chain(hw->fifos + HFCUSB_B1_RX,
					 ISOC_PACKETS_B,
					 (usb_complete_t)rx_iso_complete,
					 16);
			break;
		case HFC_CHAN_B2:
			start_isoc_chain(hw->fifos + HFCUSB_B2_RX,
					 ISOC_PACKETS_B,
					 (usb_complete_t)rx_iso_complete,
					 16);
			break;
		}
	}

	/* start tx endpoints using USB ISO OUT method */
	switch (channel) {
	case HFC_CHAN_D:
		start_isoc_chain(hw->fifos + HFCUSB_D_TX,
				 ISOC_PACKETS_B,
				 (usb_complete_t)tx_iso_complete, 1);
		break;
	case HFC_CHAN_B1:
		start_isoc_chain(hw->fifos + HFCUSB_B1_TX,
				 ISOC_PACKETS_D,
				 (usb_complete_t)tx_iso_complete, 1);
		break;
	case HFC_CHAN_B2:
		start_isoc_chain(hw->fifos + HFCUSB_B2_TX,
				 ISOC_PACKETS_B,
				 (usb_complete_t)tx_iso_complete, 1);
		break;
	}
}

/* stop USB data pipes dependand on device's endpoint configuration */
static void
hfcsusb_stop_endpoint(struct hfcsusb *hw, int channel)
{
	/* quick check if endpoint currently running */
	if ((channel == HFC_CHAN_D) && (!hw->fifos[HFCUSB_D_RX].active))
		return;
	if ((channel == HFC_CHAN_B1) && (!hw->fifos[HFCUSB_B1_RX].active))
		return;
	if ((channel == HFC_CHAN_B2) && (!hw->fifos[HFCUSB_B2_RX].active))
		return;
	if ((channel == HFC_CHAN_E) && (!hw->fifos[HFCUSB_PCM_RX].active))
		return;

	/* rx endpoints using USB INT IN method */
	if (hw->cfg_used == CNF_3INT3ISO || hw->cfg_used == CNF_4INT3ISO)
		stop_int_gracefull(hw->fifos + channel * 2 + 1);

	/* rx endpoints using USB ISO IN method */
	if (hw->cfg_used == CNF_3ISO3ISO || hw->cfg_used == CNF_4ISO3ISO)
		stop_iso_gracefull(hw->fifos + channel * 2 + 1);

	/* tx endpoints using USB ISO OUT method */
	if (channel != HFC_CHAN_E)
		stop_iso_gracefull(hw->fifos + channel * 2);
}


/* Hardware Initialization */
static int
setup_hfcsusb(struct hfcsusb *hw)
{
	void *dmabuf = kmalloc(sizeof(u_char), GFP_KERNEL);
	u_char b;
	int ret;

	if (debug & DBG_HFC_CALL_TRACE)
		printk(KERN_DEBUG "%s: %s\n", hw->name, __func__);

	if (!dmabuf)
		return -ENOMEM;

	ret = read_reg_atomic(hw, HFCUSB_CHIP_ID, dmabuf);

	memcpy(&b, dmabuf, sizeof(u_char));
	kfree(dmabuf);

	/* check the chip id */
	if (ret != 1) {
		printk(KERN_DEBUG "%s: %s: cannot read chip id\n",
		       hw->name, __func__);
		return 1;
	}
	if (b != HFCUSB_CHIPID) {
		printk(KERN_DEBUG "%s: %s: Invalid chip id 0x%02x\n",
		       hw->name, __func__, b);
		return 1;
	}

	/* first set the needed config, interface and alternate */
	(void) usb_set_interface(hw->dev, hw->if_used, hw->alt_used);

	hw->led_state = 0;

	/* init the background machinery for control requests */
	hw->ctrl_read.bRequestType = 0xc0;
	hw->ctrl_read.bRequest = 1;
	hw->ctrl_read.wLength = cpu_to_le16(1);
	hw->ctrl_write.bRequestType = 0x40;
	hw->ctrl_write.bRequest = 0;
	hw->ctrl_write.wLength = 0;
	usb_fill_control_urb(hw->ctrl_urb, hw->dev, hw->ctrl_out_pipe,
			     (u_char *)&hw->ctrl_write, NULL, 0,
			     (usb_complete_t)ctrl_complete, hw);

	reset_hfcsusb(hw);
	return 0;
}

static void
release_hw(struct hfcsusb *hw)
{
	if (debug & DBG_HFC_CALL_TRACE)
		printk(KERN_DEBUG "%s: %s\n", hw->name, __func__);

	/*
	 * stop all endpoints gracefully
	 * TODO: mISDN_core should generate CLOSE_CHANNEL
	 *       signals after calling mISDN_unregister_device()
	 */
	hfcsusb_stop_endpoint(hw, HFC_CHAN_D);
	hfcsusb_stop_endpoint(hw, HFC_CHAN_B1);
	hfcsusb_stop_endpoint(hw, HFC_CHAN_B2);
	if (hw->fifos[HFCUSB_PCM_RX].pipe)
		hfcsusb_stop_endpoint(hw, HFC_CHAN_E);
	if (hw->protocol == ISDN_P_TE_S0)
		l1_event(hw->dch.l1, CLOSE_CHANNEL);

	mISDN_unregister_device(&hw->dch.dev);
	mISDN_freebchannel(&hw->bch[1]);
	mISDN_freebchannel(&hw->bch[0]);
	mISDN_freedchannel(&hw->dch);

	if (hw->ctrl_urb) {
		usb_kill_urb(hw->ctrl_urb);
		usb_free_urb(hw->ctrl_urb);
		hw->ctrl_urb = NULL;
	}

	if (hw->intf)
		usb_set_intfdata(hw->intf, NULL);
	list_del(&hw->list);
	kfree(hw);
	hw = NULL;
}

static void
deactivate_bchannel(struct bchannel *bch)
{
	struct hfcsusb *hw = bch->hw;
	u_long flags;

	if (bch->debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: %s: bch->nr(%i)\n",
		       hw->name, __func__, bch->nr);

	spin_lock_irqsave(&hw->lock, flags);
	mISDN_clear_bchannel(bch);
	spin_unlock_irqrestore(&hw->lock, flags);
	hfcsusb_setup_bch(bch, ISDN_P_NONE);
	hfcsusb_stop_endpoint(hw, bch->nr - 1);
}

/*
 * Layer 1 B-channel hardware access
 */
static int
hfc_bctrl(struct mISDNchannel *ch, u_int cmd, void *arg)
{
	struct bchannel	*bch = container_of(ch, struct bchannel, ch);
	int		ret = -EINVAL;

	if (bch->debug & DEBUG_HW)
		printk(KERN_DEBUG "%s: cmd:%x %p\n", __func__, cmd, arg);

	switch (cmd) {
	case HW_TESTRX_RAW:
	case HW_TESTRX_HDLC:
	case HW_TESTRX_OFF:
		ret = -EINVAL;
		break;

	case CLOSE_CHANNEL:
		test_and_clear_bit(FLG_OPEN, &bch->Flags);
		deactivate_bchannel(bch);
		ch->protocol = ISDN_P_NONE;
		ch->peer = NULL;
		module_put(THIS_MODULE);
		ret = 0;
		break;
	case CONTROL_CHANNEL:
		ret = channel_bctrl(bch, arg);
		break;
	default:
		printk(KERN_WARNING "%s: unknown prim(%x)\n",
		       __func__, cmd);
	}
	return ret;
}

static int
setup_instance(struct hfcsusb *hw, struct device *parent)
{
	u_long	flags;
	int	err, i;

	if (debug & DBG_HFC_CALL_TRACE)
		printk(KERN_DEBUG "%s: %s\n", hw->name, __func__);

	spin_lock_init(&hw->ctrl_lock);
	spin_lock_init(&hw->lock);

	mISDN_initdchannel(&hw->dch, MAX_DFRAME_LEN_L1, ph_state);
	hw->dch.debug = debug & 0xFFFF;
	hw->dch.hw = hw;
	hw->dch.dev.Dprotocols = (1 << ISDN_P_TE_S0) | (1 << ISDN_P_NT_S0);
	hw->dch.dev.D.send = hfcusb_l2l1D;
	hw->dch.dev.D.ctrl = hfc_dctrl;

	/* enable E-Channel logging */
	if (hw->fifos[HFCUSB_PCM_RX].pipe)
		mISDN_initdchannel(&hw->ech, MAX_DFRAME_LEN_L1, NULL);

	hw->dch.dev.Bprotocols = (1 << (ISDN_P_B_RAW & ISDN_P_B_MASK)) |
		(1 << (ISDN_P_B_HDLC & ISDN_P_B_MASK));
	hw->dch.dev.nrbchan = 2;
	for (i = 0; i < 2; i++) {
		hw->bch[i].nr = i + 1;
		set_channelmap(i + 1, hw->dch.dev.channelmap);
		hw->bch[i].debug = debug;
		mISDN_initbchannel(&hw->bch[i], MAX_DATA_MEM, poll >> 1);
		hw->bch[i].hw = hw;
		hw->bch[i].ch.send = hfcusb_l2l1B;
		hw->bch[i].ch.ctrl = hfc_bctrl;
		hw->bch[i].ch.nr = i + 1;
		list_add(&hw->bch[i].ch.list, &hw->dch.dev.bchannels);
	}

	hw->fifos[HFCUSB_B1_TX].bch = &hw->bch[0];
	hw->fifos[HFCUSB_B1_RX].bch = &hw->bch[0];
	hw->fifos[HFCUSB_B2_TX].bch = &hw->bch[1];
	hw->fifos[HFCUSB_B2_RX].bch = &hw->bch[1];
	hw->fifos[HFCUSB_D_TX].dch = &hw->dch;
	hw->fifos[HFCUSB_D_RX].dch = &hw->dch;
	hw->fifos[HFCUSB_PCM_RX].ech = &hw->ech;
	hw->fifos[HFCUSB_PCM_TX].ech = &hw->ech;

	err = setup_hfcsusb(hw);
	if (err)
		goto out;

	snprintf(hw->name, MISDN_MAX_IDLEN - 1, "%s.%d", DRIVER_NAME,
		 hfcsusb_cnt + 1);
	printk(KERN_INFO "%s: registered as '%s'\n",
	       DRIVER_NAME, hw->name);

	err = mISDN_register_device(&hw->dch.dev, parent, hw->name);
	if (err)
		goto out;

	hfcsusb_cnt++;
	write_lock_irqsave(&HFClock, flags);
	list_add_tail(&hw->list, &HFClist);
	write_unlock_irqrestore(&HFClock, flags);
	return 0;

out:
	mISDN_freebchannel(&hw->bch[1]);
	mISDN_freebchannel(&hw->bch[0]);
	mISDN_freedchannel(&hw->dch);
	kfree(hw);
	return err;
}

static int
hfcsusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct hfcsusb			*hw;
	struct usb_device		*dev = interface_to_usbdev(intf);
	struct usb_host_interface	*iface = intf->cur_altsetting;
	struct usb_host_interface	*iface_used = NULL;
	struct usb_host_endpoint	*ep;
	struct hfcsusb_vdata		*driver_info;
	int ifnum = iface->desc.bInterfaceNumber, i, idx, alt_idx,
		probe_alt_setting, vend_idx, cfg_used, *vcf, attr, cfg_found,
		ep_addr, cmptbl[16], small_match, iso_packet_size, packet_size,
		alt_used = 0;

	vend_idx = 0xffff;
	for (i = 0; hfcsusb_idtab[i].idVendor; i++) {
		if ((le16_to_cpu(dev->descriptor.idVendor)
		     == hfcsusb_idtab[i].idVendor) &&
		    (le16_to_cpu(dev->descriptor.idProduct)
		     == hfcsusb_idtab[i].idProduct)) {
			vend_idx = i;
			continue;
		}
	}

	printk(KERN_DEBUG
	       "%s: interface(%d) actalt(%d) minor(%d) vend_idx(%d)\n",
	       __func__, ifnum, iface->desc.bAlternateSetting,
	       intf->minor, vend_idx);

	if (vend_idx == 0xffff) {
		printk(KERN_WARNING
		       "%s: no valid vendor found in USB descriptor\n",
		       __func__);
		return -EIO;
	}
	/* if vendor and product ID is OK, start probing alternate settings */
	alt_idx = 0;
	small_match = -1;

	/* default settings */
	iso_packet_size = 16;
	packet_size = 64;

	while (alt_idx < intf->num_altsetting) {
		iface = intf->altsetting + alt_idx;
		probe_alt_setting = iface->desc.bAlternateSetting;
		cfg_used = 0;

		while (validconf[cfg_used][0]) {
			cfg_found = 1;
			vcf = validconf[cfg_used];
			ep = iface->endpoint;
			memcpy(cmptbl, vcf, 16 * sizeof(int));

			/* check for all endpoints in this alternate setting */
			for (i = 0; i < iface->desc.bNumEndpoints; i++) {
				ep_addr = ep->desc.bEndpointAddress;

				/* get endpoint base */
				idx = ((ep_addr & 0x7f) - 1) * 2;
				if (idx > 15)
					return -EIO;

				if (ep_addr & 0x80)
					idx++;
				attr = ep->desc.bmAttributes;

				if (cmptbl[idx] != EP_NOP) {
					if (cmptbl[idx] == EP_NUL)
						cfg_found = 0;
					if (attr == USB_ENDPOINT_XFER_INT
					    && cmptbl[idx] == EP_INT)
						cmptbl[idx] = EP_NUL;
					if (attr == USB_ENDPOINT_XFER_BULK
					    && cmptbl[idx] == EP_BLK)
						cmptbl[idx] = EP_NUL;
					if (attr == USB_ENDPOINT_XFER_ISOC
					    && cmptbl[idx] == EP_ISO)
						cmptbl[idx] = EP_NUL;

					if (attr == USB_ENDPOINT_XFER_INT &&
					    ep->desc.bInterval < vcf[17]) {
						cfg_found = 0;
					}
				}
				ep++;
			}

			for (i = 0; i < 16; i++)
				if (cmptbl[i] != EP_NOP && cmptbl[i] != EP_NUL)
					cfg_found = 0;

			if (cfg_found) {
				if (small_match < cfg_used) {
					small_match = cfg_used;
					alt_used = probe_alt_setting;
					iface_used = iface;
				}
			}
			cfg_used++;
		}
		alt_idx++;
	}	/* (alt_idx < intf->num_altsetting) */

	/* not found a valid USB Ta Endpoint config */
	if (small_match == -1)
		return -EIO;

	iface = iface_used;
	hw = kzalloc(sizeof(struct hfcsusb), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;	/* got no mem */
	snprintf(hw->name, MISDN_MAX_IDLEN - 1, "%s", DRIVER_NAME);

	ep = iface->endpoint;
	vcf = validconf[small_match];

	for (i = 0; i < iface->desc.bNumEndpoints; i++) {
		struct usb_fifo *f;

		ep_addr = ep->desc.bEndpointAddress;
		/* get endpoint base */
		idx = ((ep_addr & 0x7f) - 1) * 2;
		if (ep_addr & 0x80)
			idx++;
		f = &hw->fifos[idx & 7];

		/* init Endpoints */
		if (vcf[idx] == EP_NOP || vcf[idx] == EP_NUL) {
			ep++;
			continue;
		}
		switch (ep->desc.bmAttributes) {
		case USB_ENDPOINT_XFER_INT:
			f->pipe = usb_rcvintpipe(dev,
						 ep->desc.bEndpointAddress);
			f->usb_transfer_mode = USB_INT;
			packet_size = le16_to_cpu(ep->desc.wMaxPacketSize);
			break;
		case USB_ENDPOINT_XFER_BULK:
			if (ep_addr & 0x80)
				f->pipe = usb_rcvbulkpipe(dev,
							  ep->desc.bEndpointAddress);
			else
				f->pipe = usb_sndbulkpipe(dev,
							  ep->desc.bEndpointAddress);
			f->usb_transfer_mode = USB_BULK;
			packet_size = le16_to_cpu(ep->desc.wMaxPacketSize);
			break;
		case USB_ENDPOINT_XFER_ISOC:
			if (ep_addr & 0x80)
				f->pipe = usb_rcvisocpipe(dev,
							  ep->desc.bEndpointAddress);
			else
				f->pipe = usb_sndisocpipe(dev,
							  ep->desc.bEndpointAddress);
			f->usb_transfer_mode = USB_ISOC;
			iso_packet_size = le16_to_cpu(ep->desc.wMaxPacketSize);
			break;
		default:
			f->pipe = 0;
		}

		if (f->pipe) {
			f->fifonum = idx & 7;
			f->hw = hw;
			f->usb_packet_maxlen =
				le16_to_cpu(ep->desc.wMaxPacketSize);
			f->intervall = ep->desc.bInterval;
		}
		ep++;
	}
	hw->dev = dev; /* save device */
	hw->if_used = ifnum; /* save used interface */
	hw->alt_used = alt_used; /* and alternate config */
	hw->ctrl_paksize = dev->descriptor.bMaxPacketSize0; /* control size */
	hw->cfg_used = vcf[16];	/* store used config */
	hw->vend_idx = vend_idx; /* store found vendor */
	hw->packet_size = packet_size;
	hw->iso_packet_size = iso_packet_size;

	/* create the control pipes needed for register access */
	hw->ctrl_in_pipe = usb_rcvctrlpipe(hw->dev, 0);
	hw->ctrl_out_pipe = usb_sndctrlpipe(hw->dev, 0);

	driver_info = (struct hfcsusb_vdata *)
		      hfcsusb_idtab[vend_idx].driver_info;

	hw->ctrl_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!hw->ctrl_urb) {
		pr_warn("%s: No memory for control urb\n",
			driver_info->vend_name);
		kfree(hw);
		return -ENOMEM;
	}

	pr_info("%s: %s: detected \"%s\" (%s, if=%d alt=%d)\n",
		hw->name, __func__, driver_info->vend_name,
		conf_str[small_match], ifnum, alt_used);

	if (setup_instance(hw, dev->dev.parent))
		return -EIO;

	hw->intf = intf;
	usb_set_intfdata(hw->intf, hw);
	return 0;
}

/* function called when an active device is removed */
static void
hfcsusb_disconnect(struct usb_interface *intf)
{
	struct hfcsusb *hw = usb_get_intfdata(intf);
	struct hfcsusb *next;
	int cnt = 0;

	printk(KERN_INFO "%s: device disconnected\n", hw->name);

	handle_led(hw, LED_POWER_OFF);
	release_hw(hw);

	list_for_each_entry_safe(hw, next, &HFClist, list)
		cnt++;
	if (!cnt)
		hfcsusb_cnt = 0;

	usb_set_intfdata(intf, NULL);
}

static struct usb_driver hfcsusb_drv = {
	.name = DRIVER_NAME,
	.id_table = hfcsusb_idtab,
	.probe = hfcsusb_probe,
	.disconnect = hfcsusb_disconnect,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(hfcsusb_drv);
