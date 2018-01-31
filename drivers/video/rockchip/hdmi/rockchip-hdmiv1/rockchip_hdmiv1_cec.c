/* SPDX-License-Identifier: GPL-2.0 */
#include "rockchip_hdmiv1.h"
#include "rockchip_hdmiv1_hw.h"
#include "../rockchip-hdmi-cec.h"

struct cec_t {
	wait_queue_head_t wait;
	int busfree;
	int tx_done;
};

static int init = 1;
static struct cec_t cec;

static int rockchip_hdmiv1_cec_read_frame(struct hdmi *hdmi,
					  struct cec_framedata *frame)
{
	int i, length, val;
	char *data = (char *)frame;
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	if (!frame)
		return -1;

	hdmi_readl(hdmi_dev, CEC_RX_LENGTH, &length);
	hdmi_writel(hdmi_dev, CEC_RX_OFFSET, 0);

	HDMIDBG(1, "CEC: %s length is %d\n", __func__, length);
	for (i = 0; i < length; i++) {
		hdmi_readl(hdmi_dev, CEC_DATA, &val);
		data[i] = val;
		pr_info("%02x\n", data[i]);
	}
	return 0;
}

static int rockchip_hdmiv1_cec_send_frame(struct hdmi *hdmi,
					  struct cec_framedata *frame)
{
	int i;
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG(1, "CEC: TX srcdestaddr %x opcode %x ",
		frame->srcdestaddr, frame->opcode);
	if (frame->argcount) {
		HDMIDBG(1, "args:");
		for (i = 0; i < frame->argcount; i++)
			HDMIDBG(1, "%02x ", frame->args[i]);
	}
	HDMIDBG(1, "\n");

	hdmi_writel(hdmi_dev, CEC_TX_OFFSET, 0);
	hdmi_writel(hdmi_dev, CEC_DATA, frame->srcdestaddr);
	hdmi_writel(hdmi_dev, CEC_DATA, frame->opcode);

	for (i = 0; i < frame->argcount; i++)
		hdmi_writel(hdmi_dev, CEC_DATA, frame->args[i]);

	hdmi_writel(hdmi_dev, CEC_TX_LENGTH, frame->argcount + 2);

	/*Wait for bus free*/
	cec.busfree = 1;
	hdmi_writel(hdmi_dev, CEC_CTRL, m_BUSFREETIME_ENABLE);
	HDMIDBG(1, "start wait bus free\n");
	if (wait_event_interruptible_timeout(cec.wait,
					     cec.busfree == 0,
					     msecs_to_jiffies(17)))
		return CEC_SEND_BUSY;

	HDMIDBG(1, "end wait bus free,start tx,busfree=%d\n", cec.busfree);
	/*Start TX*/
	cec.tx_done = 0;
	hdmi_writel(hdmi_dev, CEC_CTRL, m_BUSFREETIME_ENABLE | m_START_TX);
	if (wait_event_interruptible_timeout(cec.wait,
					     cec.tx_done != 0,
					     msecs_to_jiffies(100)))
		hdmi_writel(hdmi_dev, CEC_CTRL, 0);
	HDMIDBG(1, "end tx,tx_done=%d\n", cec.tx_done);

	if (cec.tx_done == 1) {
		cec.tx_done = 0;
		return CEC_SEND_SUCCESS;
	} else {
		return CEC_SEND_NACK;
	}
}

void rockchip_hdmiv1_cec_setcecla(struct hdmi *hdmi, int ceclgaddr)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	HDMIDBG(1, "CEC: %s\n", __func__);
	hdmi_writel(hdmi_dev, CEC_LOGICADDR, ceclgaddr);
}

void rockchip_hdmiv1_cec_isr(struct hdmi_dev *hdmi_dev)
{
	int tx_isr = 0, rx_isr = 0;

	hdmi_readl(hdmi_dev, CEC_TX_INT, &tx_isr);
	hdmi_readl(hdmi_dev, CEC_RX_INT, &rx_isr);

	HDMIDBG(1, "CEC: rockchip_hdmiv1_cec_isr:tx_isr %02x  rx_isr %02x\n\n",
		tx_isr, rx_isr);

	hdmi_writel(hdmi_dev, CEC_TX_INT, tx_isr);
	hdmi_writel(hdmi_dev, CEC_RX_INT, rx_isr);

	if (tx_isr & m_TX_BUSNOTFREE) {
		cec.busfree = 0;
		HDMIDBG(1, "CEC: m_TX_BUSNOTFREE,busfree=%d\n", cec.busfree);
	} else if (tx_isr & m_TX_DONE) {
		cec.tx_done = 1;
		HDMIDBG(1, "CEC: m_TX_DONE,busfree=%d\n", cec.tx_done);
	} else {
		cec.tx_done = -1;
		HDMIDBG(1, "CEC: else:busfree=%d\n", cec.tx_done);
	}

	wake_up_interruptible_all(&cec.wait);
	if (rx_isr & m_RX_DONE)
		rockchip_hdmi_cec_submit_work(EVENT_RX_FRAME, 0, NULL);
}

void rockchip_hdmiv1_cec_init(struct hdmi *hdmi)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;

	if (init) {
		/*Fref = Fsys / ((register 0xd4 + 1)*(register 0xd5 + 1))*/
		/*Fref = 0.5M, Fsys = 74.25M*/
		hdmi_writel(hdmi_dev, CEC_CLK_H, 11);
		hdmi_writel(hdmi_dev, CEC_CLK_L, 11);

		/*Set bus free time to 16.8ms*/
		hdmi_writel(hdmi_dev, CEC_BUSFREETIME_L, 0xd0);
		hdmi_writel(hdmi_dev, CEC_BUSFREETIME_H, 0x20);

		/*Enable TX/RX INT*/
		hdmi_writel(hdmi_dev, CEC_TX_INT, 0xFF);
		hdmi_writel(hdmi_dev, CEC_RX_INT, 0xFF);

		HDMIDBG(1, "CEC: rockchip_hdmiv1_cec_init sucess\n");
		rockchip_hdmi_cec_init(hdmi,
				       rockchip_hdmiv1_cec_send_frame,
				       rockchip_hdmiv1_cec_read_frame,
				       rockchip_hdmiv1_cec_setcecla);
		init = 0;
		init_waitqueue_head(&cec.wait);
	}
	HDMIDBG(1, "%s", __func__);
}

