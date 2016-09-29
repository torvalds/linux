#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include "rockchip_hdmiv1.h"
#include "rockchip_hdmiv1_hdcp.h"
#include "rockchip_hdmiv1_hw.h"

static struct hdcp *hdcp;

static void hdcp_work_queue(struct work_struct *work);

#define AUTH_TIMEOUT (2 * HZ)
static struct timer_list auth_timer;
static int timer_state;

static int is_1b_03_test(struct hdmi_dev *hdmi_dev)
{
	int reg_value;
	int reg_val_1;

	hdmi_readl(hdmi_dev, 0x58, &reg_value);
	hdmi_readl(hdmi_dev, 0xc3, &reg_val_1);

	if (reg_value != 0) {
		if ((reg_val_1 & 0x40) == 0)
			return 1;
	}
	return 0;
}

static void rockchip_hdmiv1_set_colorbar(struct hdmi_dev *hdmi_dev,
					 int enable)
{
	static int display_mask;
	int reg_value;
	int tmds_clk;

	tmds_clk = hdmi_dev->tmdsclk;
	if (enable) {
		if (!display_mask) {
			if (tmds_clk <= (HDMI_SYS_FREG_CLK << 2)) {
				hdmi_readl(hdmi_dev, SYS_CTRL, &reg_value);
				hdmi_msk_reg(hdmi_dev, SYS_CTRL,
					     m_REG_CLK_SOURCE,
					     v_REG_CLK_SOURCE_SYS);
				hdmi_writel(hdmi_dev, HDMI_COLORBAR, 0x00);
				hdmi_writel(hdmi_dev, SYS_CTRL, reg_value);
			} else {
				hdmi_writel(hdmi_dev, HDMI_COLORBAR, 0x00);
			}
			display_mask = 1;
		}
	} else {
		if (display_mask) {
			if (tmds_clk <= (HDMI_SYS_FREG_CLK << 2)) {
				hdmi_readl(hdmi_dev, SYS_CTRL, &reg_value);
				hdmi_msk_reg(hdmi_dev, SYS_CTRL,
					     m_REG_CLK_SOURCE,
					     v_REG_CLK_SOURCE_SYS);
				hdmi_writel(hdmi_dev, HDMI_COLORBAR, 0x10);
				hdmi_writel(hdmi_dev, SYS_CTRL, reg_value);
			} else {
				hdmi_writel(hdmi_dev, HDMI_COLORBAR, 0x10);
			}
			display_mask = 0;
		}
	}
}

static void rockchip_hdmiv1_hdcp_disable(struct hdmi_dev *hdmi_dev)
{
	int reg_value;
	int tmds_clk;

	tmds_clk = hdmi_dev->tmdsclk;
	if (tmds_clk <= (HDMI_SYS_FREG_CLK << 2)) {
		hdmi_readl(hdmi_dev, SYS_CTRL, &reg_value);
		hdmi_msk_reg(hdmi_dev, SYS_CTRL,
			     m_REG_CLK_SOURCE, v_REG_CLK_SOURCE_SYS);
	}

	/* Disable HDCP Interrupt */
	hdmi_writel(hdmi_dev, HDCP_INT_MASK1, 0x00);
	/* Stop and Reset HDCP*/
	hdmi_msk_reg(hdmi_dev, HDCP_CTRL1,
		     m_AUTH_START | m_AUTH_STOP | m_HDCP_RESET,
		     v_AUTH_START(0) | v_AUTH_STOP(1) | v_HDCP_RESET(1));

	if (tmds_clk <= (HDMI_SYS_FREG_CLK << 2))
		hdmi_writel(hdmi_dev, SYS_CTRL, reg_value);
}

static int rockchip_hdmiv1_hdcp_key_check(struct hdcp_keys *key)
{
	int i = 0;

	HDMIDBG(3, "HDCP: check hdcp key\n");
	/*check 40 private key */
	for (i = 0; i < HDCP_PRIVATE_KEY_SIZE; i++) {
		if (key->devicekey[i] != 0x00)
			return HDCP_KEY_VALID;
	}
	/*check aksv*/
	for (i = 0; i < 5; i++) {
		if (key->ksv[i] != 0x00)
			return HDCP_KEY_VALID;
	}

	return HDCP_KEY_INVALID;
}

static int rockchip_hdmiv1_hdcp_load_key2mem(void)
{
	int i;
	struct hdmi_dev *hdmi_dev;
	struct hdcp_keys *key;

	if (!hdcp)
		return -1;
	hdmi_dev = hdcp->hdmi_dev;
	key = hdcp->keys;
	HDMIDBG(3, "HDCP: rockchip_hdmiv1_hdcp_load_key2mem start\n");
	/* Write 40 private key*/
	for (i = 0; i < HDCP_PRIVATE_KEY_SIZE; i++)
		hdmi_writel(hdmi_dev, HDCP_KEY_FIFO, key->devicekey[i]);
	/* Write 1st aksv*/
	for (i = 0; i < 5; i++)
		hdmi_writel(hdmi_dev, HDCP_KEY_FIFO, key->ksv[i]);
	/* Write 2nd aksv*/
	for (i = 0; i < 5; i++)
		hdmi_writel(hdmi_dev, HDCP_KEY_FIFO, key->ksv[i]);
	HDMIDBG(3, "HDCP: rockchip_hdmiv1_hdcp_load_key2mem end\n");
	return HDCP_OK;
}

static int rockchip_hdmiv1_hdcp_start_authentication(struct hdmi_dev *hdmi_dev)
{
	int temp;
	int retry = 0;
	int tmds_clk;

	tmds_clk = hdmi_dev->tmdsclk;
	if (!hdcp->keys) {
		HDCP_WARN("HDCP: key is not loaded\n");
		return HDCP_KEY_ERR;
	}
	if (rockchip_hdmiv1_hdcp_key_check(hdcp->keys) == HDCP_KEY_INVALID) {
		HDCP_WARN("loaded HDCP key is incorrect\n");
		return HDCP_KEY_ERR;
	}
	if (tmds_clk > (HDMI_SYS_FREG_CLK << 2)) {
		/*Select TMDS CLK to configure regs*/
		hdmi_msk_reg(hdmi_dev, SYS_CTRL,
			     m_REG_CLK_SOURCE, v_REG_CLK_SOURCE_TMDS);
	} else {
		hdmi_msk_reg(hdmi_dev, SYS_CTRL,
			     m_REG_CLK_SOURCE, v_REG_CLK_SOURCE_SYS);
	}
	hdmi_writel(hdmi_dev, HDCP_TIMER_100MS, 0x28);
	hdmi_readl(hdmi_dev, HDCP_KEY_STATUS, &temp);
	while ((temp & m_KEY_READY) == 0) {
		if (retry > 1000) {
			HDCP_WARN("HDCP: loaded key error\n");
			return HDCP_KEY_ERR;
		}
		rockchip_hdmiv1_hdcp_load_key2mem();
		usleep_range(900, 1000);
		hdmi_readl(hdmi_dev, HDCP_KEY_STATUS, &temp);
		retry++;
	}
	/*Config DDC bus clock: ddc_clk = reg_clk/4*(reg 0x4c 0x4b)*/
	retry = hdmi_dev->hclk_rate / (HDCP_DDC_CLK << 2);
	hdmi_writel(hdmi_dev, DDC_CLK_L, retry & 0xFF);
	hdmi_writel(hdmi_dev, DDC_CLK_H, (retry >> 8) & 0xFF);
	hdmi_writel(hdmi_dev, HDCP_CTRL2, 0x67);
	/*Enable interrupt*/
	hdmi_writel(hdmi_dev, HDCP_INT_MASK1,
		    m_INT_HDCP_ERR | m_INT_BKSV_READY | m_INT_BKSV_UPDATE |
		    m_INT_AUTH_SUCCESS | m_INT_AUTH_READY);
	hdmi_writel(hdmi_dev, HDCP_INT_MASK2, 0x00);
	/*Start authentication*/
	hdmi_msk_reg(hdmi_dev, HDCP_CTRL1,
		     m_AUTH_START | m_ENCRYPT_ENABLE | m_ADVANED_ENABLE |
		     m_AUTH_STOP | m_HDCP_RESET,
		     v_AUTH_START(1) | v_ENCRYPT_ENABLE(1) |
		     v_ADVANED_ENABLE(0) | v_AUTH_STOP(0) | v_HDCP_RESET(0));

	if (tmds_clk <= (HDMI_SYS_FREG_CLK << 2)) {
		hdmi_msk_reg(hdmi_dev, SYS_CTRL, m_REG_CLK_SOURCE,
			     v_REG_CLK_SOURCE_TMDS);
	}
	return HDCP_OK;
}

static int rockchip_hdmiv1_hdcp_stop_authentication(struct hdmi_dev *hdmi_dev)
{
	hdmi_msk_reg(hdmi_dev, SYS_CTRL,
		     m_REG_CLK_SOURCE, v_REG_CLK_SOURCE_SYS);
	hdmi_writel(hdmi_dev, DDC_CLK_L, 0x1c);
	hdmi_writel(hdmi_dev, DDC_CLK_H, 0x00);
	hdmi_writel(hdmi_dev, HDCP_CTRL2, 0x08);
	hdmi_writel(hdmi_dev, HDCP_INT_MASK2, 0x06);
	hdmi_writel(hdmi_dev, HDCP_CTRL1, 0x02);
	return 0;
	/*hdmi_writel(HDCP_CTRL1, 0x0a);*/
}

static int rockchip_hdmiv1_hdcp_error(int value)
{
	if (value & 0x80)
		HDCP_WARN("Timed out waiting for downstream repeater\n");
	else if (value & 0x40)
		HDCP_WARN("Too many devices connected to repeater tree\n");
	else if (value & 0x20)
		HDCP_WARN("SHA-1 hash check of BKSV list failed\n");
	else if (value & 0x10)
		HDCP_WARN("SHA-1 hash check of BKSV list failed\n");
	else if (value & 0x08)
		HDCP_WARN("DDC channels no acknowledge\n");
	else if (value & 0x04)
		HDCP_WARN("Pj mismatch\n");
	else if (value & 0x02)
		HDCP_WARN("Ri mismatch\n");
	else if (value & 0x01)
		HDCP_WARN("Bksv is wrong\n");
	else
		return 0;
	return 1;
}

static void rockchip_hdmiv1_hdcp_interrupt(struct hdmi_dev *hdmi_dev,
					   char *status1, char *status2)
{
	int interrupt1 = 0;
	int interrupt2 = 0;
	int temp = 0;
	int tmds_clk;

	tmds_clk = hdmi_dev->tmdsclk;
	hdmi_readl(hdmi_dev, HDCP_INT_STATUS1, &interrupt1);
	hdmi_readl(hdmi_dev, HDCP_INT_STATUS2, &interrupt2);

	if (tmds_clk <= (HDMI_SYS_FREG_CLK << 2))
		hdmi_msk_reg(hdmi_dev, SYS_CTRL,
			     m_REG_CLK_SOURCE, v_REG_CLK_SOURCE_SYS);

	if (interrupt1) {
		hdmi_writel(hdmi_dev, HDCP_INT_STATUS1, interrupt1);
		if (interrupt1 & m_INT_HDCP_ERR) {
			hdmi_readl(hdmi_dev, HDCP_ERROR, &temp);
			HDCP_WARN("HDCP: Error reg 0x65 = 0x%02x\n", temp);
			rockchip_hdmiv1_hdcp_error(temp);
			hdmi_writel(hdmi_dev, HDCP_ERROR, temp);
		}
	}
	if (interrupt2)
		hdmi_writel(hdmi_dev, HDCP_INT_STATUS2, interrupt2);

	*status1 = interrupt1;
	*status2 = interrupt2;

	if (tmds_clk <= (HDMI_SYS_FREG_CLK << 2))
		hdmi_msk_reg(hdmi_dev, SYS_CTRL, m_REG_CLK_SOURCE,
			     v_REG_CLK_SOURCE_TMDS);
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_submit_work
 *-----------------------------------------------------------------------------
 */
static struct delayed_work *hdcp_submit_work(int event, int delay)
{
	struct hdcp_delayed_work *work;

	HDMIDBG(3, "%s event %04x delay %d\n", __func__, event, delay);
	work = kmalloc(sizeof(*work), GFP_ATOMIC);

	if (work) {
		INIT_DELAYED_WORK(&work->work, hdcp_work_queue);
		work->event = event;
		queue_delayed_work(hdcp->workqueue,
				   &work->work,
				   msecs_to_jiffies(delay));
	} else {
		HDCP_WARN("HDCP:Cannot allocate memory to create work\n");
		return 0;
	}

	return &work->work;
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_cancel_work
 *-----------------------------------------------------------------------------
 */
static void hdcp_cancel_work(struct delayed_work **work)
{
	int ret = 0;

	if (*work) {
		ret = cancel_delayed_work(*work);
		if (ret != 1) {
			ret = cancel_work_sync(&((*work)->work));
			HDCP_WARN("Canceling sync work failed %d\n", ret);
		}
		kfree(*work);
		*work = 0;
	}
}

/*-----------------------------------------------------------------------------
 * Function: auth_timer_func
 *-----------------------------------------------------------------------------
 */
static void auth_timer_func(unsigned long data)
{
	HDCP_WARN("hdcp auth 2 second timeout\n");
	if (hdcp->auth_state == 0) {
		mod_timer(&auth_timer, jiffies + AUTH_TIMEOUT);
		if ((hdcp->hdcp_state != HDCP_DISABLED) &&
		    (hdcp->hdcp_state != HDCP_ENABLE_PENDING)) {
			if (is_1b_03_test(hdcp->hdmi_dev))
				return;
			hdcp_submit_work(HDCP_FAIL_EVENT, 0);
		}
	}
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_authentication_failure
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_authentication_failure(void)
{
	if (hdcp->hdmi_state == HDMI_STOPPED)
		return;

	rockchip_hdmiv1_hdcp_disable(hdcp->hdmi_dev);

	/* rockchip_hdmiv1_hdmi_control_output(false); */

	rockchip_hdmiv1_set_colorbar(hdcp->hdmi_dev, 1);
	hdcp_cancel_work(&hdcp->pending_wq_event);
	if (hdcp->retry_cnt && (hdcp->hdmi_state != HDMI_STOPPED)) {
		if (hdcp->retry_cnt <= HDCP_INFINITE_REAUTH) {
			hdcp->retry_cnt--;
			HDCP_WARN("authentication failed attempts=%d\n",
				  hdcp->retry_cnt);
		} else {
			HDCP_WARN("authentication failed retrying\n");
		}
		hdcp->hdcp_state = HDCP_AUTHENTICATION_START;

		if (hdcp->auth_state == 1 && timer_state == 0) {
			HDMIDBG(3, "add auth timer\n");
			hdcp->auth_state = 0;
			hdcp->retry_cnt = HDCP_INFINITE_REAUTH;
			auth_timer.expires = jiffies + AUTH_TIMEOUT;
			add_timer(&auth_timer);
			timer_state = 1;
		}

		hdcp->pending_wq_event = hdcp_submit_work(HDCP_AUTH_REATT_EVENT,
							 HDCP_REAUTH_DELAY);
	} else {
		HDCP_WARN("authentication failed HDCP disabled\n");
		hdcp->hdcp_state = HDCP_ENABLE_PENDING;

		if (timer_state == 1) {
			HDMIDBG(3, "delete auth timer\n");
			del_timer_sync(&auth_timer);
			timer_state = 0;
		}
	}
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_start_authentication
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_start_authentication(void)
{
	int status = HDCP_OK;

	hdcp->hdcp_state = HDCP_AUTHENTICATION_START;
	HDMIDBG(3, "HDCP: authentication start\n");
	status = rockchip_hdmiv1_hdcp_start_authentication(hdcp->hdmi_dev);
	if (status != HDCP_OK) {
		HDMIDBG(3, "HDCP: authentication failed\n");
		hdcp_wq_authentication_failure();
	} else {
		/*hdcp->hdcp_state = HDCP_WAIT_KSV_LIST;*/
		hdcp->hdcp_state = HDCP_LINK_INTEGRITY_CHECK;
	}
}

#if 0
/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_check_bksv
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_check_bksv(void)
{
	int status = HDCP_OK;

	DBG("Check BKSV start");
	status = rockchip_hdmiv1_hdcp_check_bksv();
	if (status != HDCP_OK) {
		HDCP_WARN("HDCP: Check BKSV failed");
		hdcp->retry_cnt = 0;
		hdcp_wq_authentication_failure();
	} else {
		DBG("HDCP: Check BKSV successful");
		hdcp->hdcp_state = HDCP_LINK_INTEGRITY_CHECK;
		/* Restore retry counter */
		if (hdcp->retry_times == 0)
			hdcp->retry_cnt = HDCP_INFINITE_REAUTH;
		else
			hdcp->retry_cnt = hdcp->retry_times;
	}
}
#endif
/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_authentication_success
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_authentication_success(void)
{
	hdcp->auth_state = 1;
	if (timer_state == 1) {
		HDMIDBG(3, "delete auth timer\n");
		timer_state = 0;
		del_timer_sync(&auth_timer);
	}

	rockchip_hdmiv1_set_colorbar(hdcp->hdmi_dev, 0);
	HDCP_WARN("HDCP: authentication pass\n");
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_wq_disable
 *-----------------------------------------------------------------------------
 */
static void hdcp_wq_disable(int event)
{
	HDCP_WARN("HDCP: disabled\n");

	hdcp_cancel_work(&hdcp->pending_wq_event);
	rockchip_hdmiv1_hdcp_disable(hdcp->hdmi_dev);
	if (event == HDCP_DISABLE_CTL) {
		hdcp->hdcp_state = HDCP_DISABLED;
		if (hdcp->hdmi_state == HDMI_STARTED)
			rockchip_hdmiv1_set_colorbar(hdcp->hdmi_dev, 0);
	} else if (event == HDCP_STOP_FRAME_EVENT) {
		hdcp->hdcp_state = HDCP_ENABLE_PENDING;
	}
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_work_queue
 *-----------------------------------------------------------------------------
 */
static void hdcp_work_queue(struct work_struct *work)
{
	struct hdcp_delayed_work *hdcp_w =
		container_of(work, struct hdcp_delayed_work, work.work);
	int event = hdcp_w->event;

	mutex_lock(&hdcp->lock);
	HDMIDBG(3, "%s - START - %u hdmi=%d hdcp=%d evt= %x %d\n",
		__func__,
		jiffies_to_msecs(jiffies),
		hdcp->hdmi_state,
		hdcp->hdcp_state,
		(event & 0xFF00) >> 8,
		event & 0xFF);

	if (event == HDCP_STOP_FRAME_EVENT)
		hdcp->hdmi_state = HDMI_STOPPED;
	if (event == HDCP_DISABLE_CTL || event == HDCP_STOP_FRAME_EVENT)
		hdcp_wq_disable(event);
	if (event & HDCP_WORKQUEUE_SRC)
		hdcp->pending_wq_event = 0;
	/* First handle HDMI state */
	if (event == HDCP_START_FRAME_EVENT) {
		hdcp->pending_start = 0;
		hdcp->hdmi_state = HDMI_STARTED;
	}

	/**********************/
	/* HDCP state machine */
	/**********************/
	switch (hdcp->hdcp_state) {
	case HDCP_DISABLED:
		/* HDCP enable control or re-authentication event */
		if (event == HDCP_ENABLE_CTL) {
			#if 0
			if (hdcp->retry_times == 0)
				hdcp->retry_cnt = HDCP_INFINITE_REAUTH;
			else
				hdcp->retry_cnt = hdcp->retry_times;
			#endif
			hdcp->retry_cnt = HDCP_INFINITE_REAUTH;
			if (hdcp->hdmi_state == HDMI_STARTED)
				hdcp_wq_start_authentication();
			else
				hdcp->hdcp_state = HDCP_ENABLE_PENDING;
		}
		break;
	case HDCP_ENABLE_PENDING:
		/* HDMI start frame event */
		if (event == HDCP_START_FRAME_EVENT)
			hdcp_wq_start_authentication();
		break;
	case HDCP_AUTHENTICATION_START:
		/* Re-authentication */
		if (event == HDCP_AUTH_REATT_EVENT)
			hdcp_wq_start_authentication();
		break;
#if 0
	case HDCP_WAIT_KSV_LIST:
		/* KSV failure */
		if (event == HDCP_FAIL_EVENT) {
			HDCP_WARN("HDCP: KSV switch failure\n");
			hdcp_wq_authentication_failure();
		}
		/* KSV list ready event */
		else if (event == HDCP_KSV_LIST_RDY_EVENT)
			hdcp_wq_check_bksv();
		break;
#endif
	case HDCP_LINK_INTEGRITY_CHECK:
		/* authentication failure */
		if (event == HDCP_FAIL_EVENT) {
			HDCP_WARN("HDCP: Ri check failure\n");
			hdcp_wq_authentication_failure();
		} else if (event == HDCP_AUTH_PASS_EVENT) {
			hdcp_wq_authentication_success();
		}
		break;
	default:
		HDCP_WARN("HDCP: error - unknown HDCP state\n");
		break;
	}
	kfree(hdcp_w);
	if (event == HDCP_STOP_FRAME_EVENT)
		complete(&hdcp->complete);
	mutex_unlock(&hdcp->lock);
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_start_frame_cb
 *-----------------------------------------------------------------------------
 */
static void hdcp_start_frame_cb(struct hdmi *hdmi)
{
	HDMIDBG(3, "hdcp_start_frame_cb()\n");

	/* Cancel any pending work */
	if (hdcp->pending_start)
		hdcp_cancel_work(&hdcp->pending_start);
	if (hdcp->pending_wq_event)
		hdcp_cancel_work(&hdcp->pending_wq_event);

	if (timer_state == 0) {
		HDMIDBG(3, "add auth timer\n");
		auth_timer.expires = jiffies + AUTH_TIMEOUT;
		add_timer(&auth_timer);
		timer_state = 1;
	}

	hdcp->retry_cnt = HDCP_INFINITE_REAUTH;
	hdcp->pending_start = hdcp_submit_work(HDCP_START_FRAME_EVENT,
					       HDCP_ENABLE_DELAY);
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_irq_cb
 *-----------------------------------------------------------------------------
 */
static void hdcp_irq_cb(int status)
{
	char interrupt1;
	char interrupt2;

	rockchip_hdmiv1_hdcp_interrupt(hdcp->hdmi_dev,
				       &interrupt1,
				       &interrupt2);
	HDMIDBG(3, "%s 0x%02x 0x%02x\n", __func__, interrupt1, interrupt2);
	if (interrupt1 & m_INT_HDCP_ERR) {
		if ((hdcp->hdcp_state != HDCP_DISABLED) &&
		    (hdcp->hdcp_state != HDCP_ENABLE_PENDING))
			hdcp_submit_work(HDCP_FAIL_EVENT, 0);
	}
	#if 0
	else if (interrupt1 & (m_INT_BKSV_READY | m_INT_BKSV_UPDATE))
		hdcp_submit_work(HDCP_KSV_LIST_RDY_EVENT, 0);
	#endif
	else if (interrupt1 & m_INT_AUTH_SUCCESS)
		hdcp_submit_work(HDCP_AUTH_PASS_EVENT, 0);
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_power_on_cb
 *-----------------------------------------------------------------------------
 */
static int hdcp_power_on_cb(void)
{
	HDMIDBG(3, "%s", __func__);
	return rockchip_hdmiv1_hdcp_load_key2mem();
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_power_off_cb
 *-----------------------------------------------------------------------------
 */
static void hdcp_power_off_cb(struct hdmi *hdmi)
{
	unsigned int time;

	HDMIDBG(3, "%s\n", __func__);
	if (timer_state == 1) {
		HDMIDBG(3, "delete auth timer\n");
		timer_state = 0;
		del_timer_sync(&auth_timer);
	}
	hdcp->auth_state = 0;

	if (!hdcp->enable)
		return;
	rockchip_hdmiv1_hdcp_stop_authentication(hdcp->hdmi_dev);
	hdcp_cancel_work(&hdcp->pending_start);
	hdcp_cancel_work(&hdcp->pending_wq_event);
	init_completion(&hdcp->complete);
	/* Post event to workqueue */
	time = msecs_to_jiffies(5000);
	if (hdcp_submit_work(HDCP_STOP_FRAME_EVENT, 0))
		wait_for_completion_interruptible_timeout(&hdcp->complete,
							  time);
}

/*
 * Load HDCP key to external HDCP memory
 */
static void hdcp_load_keys_cb(const struct firmware *fw, void *context)
{
	if (!fw) {
		pr_err("HDCP: failed to load keys\n");
		return;
	}
	if (fw->size < HDCP_KEY_SIZE) {
		pr_err("HDCP: firmware wrong size %d\n", (int)fw->size);
		return;
	}
	hdcp->keys =  kmalloc(HDCP_KEY_SIZE, GFP_KERNEL);
	if (!hdcp->keys)
		return;
	memcpy(hdcp->keys, fw->data, HDCP_KEY_SIZE);
	HDCP_WARN("HDCP: load hdcp key success\n");

	if (fw->size > HDCP_KEY_SIZE) {
		HDMIDBG(3, "%s invalid key size %d\n", __func__,
			(int)fw->size - HDCP_KEY_SIZE);
		if ((fw->size - HDCP_KEY_SIZE) % 5) {
			pr_err("HDCP: failed to load invalid keys\n");
			return;
		}
		hdcp->invalidkeys =
			kmalloc(fw->size - HDCP_KEY_SIZE, GFP_KERNEL);
		if (!hdcp->invalidkeys) {
			pr_err("HDCP: can't allocated space for invalid keys\n");
			return;
		}
		memcpy(hdcp->invalidkeys, fw->data +
		       HDCP_KEY_SIZE, fw->size - HDCP_KEY_SIZE);
		hdcp->invalidkey = (fw->size - HDCP_KEY_SIZE) / 5;
		HDCP_WARN("HDCP: loaded hdcp invalid key success\n");
	}
}

static ssize_t hdcp_enable_read(struct device *device,
				struct device_attribute *attr,
				char *buf)
{
	int enable = 0;

	if (hdcp)
		enable = hdcp->enable;
	return snprintf(buf, PAGE_SIZE, "%d\n", enable);
}

static ssize_t hdcp_enable_write(struct device *device,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	int enable;

	if (!hdcp)
		return -EINVAL;
	if (kstrtoint(buf, 0, &enable))
		return -EINVAL;
	if (hdcp->enable != enable) {
		/* Post event to workqueue */
		if (enable) {
			if (hdcp_submit_work(HDCP_ENABLE_CTL, 0) == 0)
				return -EFAULT;
		} else {
			hdcp_cancel_work(&hdcp->pending_start);
			hdcp_cancel_work(&hdcp->pending_wq_event);

			/* Post event to workqueue */
			if (hdcp_submit_work(HDCP_DISABLE_CTL, 0) == 0)
				return -EFAULT;
		}
		hdcp->enable = enable;
	}
	return count;
}

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR,
			 hdcp_enable_read, hdcp_enable_write);

static ssize_t hdcp_trytimes_read(struct device *device,
				  struct device_attribute *attr,
				  char *buf)
{
	int trytimes = 0;

	if (hdcp)
		trytimes = hdcp->retry_times;
	return snprintf(buf, PAGE_SIZE, "%d\n", trytimes);
}

static ssize_t hdcp_trytimes_wrtie(struct device *device,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int trytimes;

	if (!hdcp)
		return -EINVAL;
	if (kstrtoint(buf, 0, &trytimes))
		return -EINVAL;
	if (hdcp->retry_times != trytimes)
		hdcp->retry_times = trytimes;
	return count;
}

static DEVICE_ATTR(trytimes, S_IRUGO | S_IWUSR,
			 hdcp_trytimes_read, hdcp_trytimes_wrtie);
static struct miscdevice mdev;

int rockchip_hdmiv1_hdcp_init(struct hdmi *hdmi)
{
	int ret;

	HDMIDBG(3, "[%s]\n", __func__);
	if (hdcp)
		return 0;

	hdcp = kmalloc(sizeof(*hdcp), GFP_KERNEL);
	if (!hdcp) {
		HDCP_WARN(">>HDCP: kmalloc fail!\n");
		ret = -ENOMEM;
		goto error0;
	}
	memset(hdcp, 0, sizeof(struct hdcp));
	mutex_init(&hdcp->lock);
	mdev.minor = MISC_DYNAMIC_MINOR;
	mdev.name = "hdcp";
	mdev.mode = 0666;
	if (misc_register(&mdev)) {
		HDCP_WARN("HDCP: Could not add character driver\n");
		ret = HDMI_ERROR_FALSE;
		goto error1;
	}
	ret = device_create_file(mdev.this_device, &dev_attr_enable);
	if (ret) {
		HDCP_WARN("HDCP: Could not add sys file enable\n");
		ret = -EINVAL;
		goto error2;
	}
	ret = device_create_file(mdev.this_device, &dev_attr_trytimes);
	if (ret) {
		HDCP_WARN("HDCP: Could not add sys file trytimes\n");
		ret = -EINVAL;
			goto error3;
	}
	hdcp->workqueue = create_singlethread_workqueue("hdcp");
	if (!hdcp->workqueue) {
		HDCP_WARN("HDCP,: create workqueue failed.\n");
		goto error4;
	}
	ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_NOHOTPLUG,
				      "hdcp", mdev.this_device,
				      GFP_KERNEL, hdcp,
				      hdcp_load_keys_cb);
	if (ret < 0) {
		HDCP_WARN("HDCP: request_firmware_nowait failed: %d\n", ret);
		goto error5;
	}
	hdcp->hdmi_dev = hdmi->property->priv;
	hdmi->ops->hdcp_cb = hdcp_start_frame_cb;
	hdmi->ops->hdcp_irq_cb = hdcp_irq_cb;
	hdmi->ops->hdcp_power_on_cb = hdcp_power_on_cb;
	hdmi->ops->hdcp_power_off_cb = hdcp_power_off_cb;

	init_timer(&auth_timer);
	auth_timer.data = 0;
	auth_timer.function = auth_timer_func;
	HDMIDBG(3, "%s success\n", __func__);
	return 0;
error5:
	destroy_workqueue(hdcp->workqueue);
error4:
	device_remove_file(mdev.this_device, &dev_attr_trytimes);
error3:
	device_remove_file(mdev.this_device, &dev_attr_enable);
error2:
	misc_deregister(&mdev);
error1:
	kfree(hdcp->keys);
	kfree(hdcp->invalidkeys);
	kfree(hdcp);
error0:
	return ret;
}

