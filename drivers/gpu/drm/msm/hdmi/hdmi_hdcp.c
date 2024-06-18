// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2010-2015, The Linux Foundation. All rights reserved.
 */

#include "hdmi.h"
#include <linux/firmware/qcom/qcom_scm.h>

#define HDCP_REG_ENABLE 0x01
#define HDCP_REG_DISABLE 0x00
#define HDCP_PORT_ADDR 0x74

#define HDCP_INT_STATUS_MASK ( \
		HDMI_HDCP_INT_CTRL_AUTH_SUCCESS_INT | \
		HDMI_HDCP_INT_CTRL_AUTH_FAIL_INT | \
		HDMI_HDCP_INT_CTRL_AUTH_XFER_REQ_INT | \
		HDMI_HDCP_INT_CTRL_AUTH_XFER_DONE_INT)

#define AUTH_WORK_RETRIES_TIME 100
#define AUTH_RETRIES_TIME 30

/* QFPROM Registers for HDMI/HDCP */
#define QFPROM_RAW_FEAT_CONFIG_ROW0_LSB  0x000000F8
#define QFPROM_RAW_FEAT_CONFIG_ROW0_MSB  0x000000FC
#define HDCP_KSV_LSB                     0x000060D8
#define HDCP_KSV_MSB                     0x000060DC

enum DS_TYPE {  /* type of downstream device */
	DS_UNKNOWN,
	DS_RECEIVER,
	DS_REPEATER,
};

enum hdmi_hdcp_state {
	HDCP_STATE_NO_AKSV,
	HDCP_STATE_INACTIVE,
	HDCP_STATE_AUTHENTICATING,
	HDCP_STATE_AUTHENTICATED,
	HDCP_STATE_AUTH_FAILED
};

struct hdmi_hdcp_reg_data {
	u32 reg_id;
	u32 off;
	char *name;
	u32 reg_val;
};

struct hdmi_hdcp_ctrl {
	struct hdmi *hdmi;
	u32 auth_retries;
	bool tz_hdcp;
	enum hdmi_hdcp_state hdcp_state;
	struct work_struct hdcp_auth_work;
	struct work_struct hdcp_reauth_work;

#define AUTH_ABORT_EV 1
#define AUTH_RESULT_RDY_EV 2
	unsigned long auth_event;
	wait_queue_head_t auth_event_queue;

	u32 ksv_fifo_w_index;
	/*
	 * store aksv from qfprom
	 */
	u32 aksv_lsb;
	u32 aksv_msb;
	bool aksv_valid;
	u32 ds_type;
	u32 bksv_lsb;
	u32 bksv_msb;
	u8 dev_count;
	u8 depth;
	u8 ksv_list[5 * 127];
	bool max_cascade_exceeded;
	bool max_dev_exceeded;
};

static int msm_hdmi_ddc_read(struct hdmi *hdmi, u16 addr, u8 offset,
	u8 *data, u16 data_len)
{
	int rc;
	int retry = 5;
	struct i2c_msg msgs[] = {
		{
			.addr	= addr >> 1,
			.flags	= 0,
			.len	= 1,
			.buf	= &offset,
		}, {
			.addr	= addr >> 1,
			.flags	= I2C_M_RD,
			.len	= data_len,
			.buf	= data,
		}
	};

	DBG("Start DDC read");
retry:
	rc = i2c_transfer(hdmi->i2c, msgs, 2);

	retry--;
	if (rc == 2)
		rc = 0;
	else if (retry > 0)
		goto retry;
	else
		rc = -EIO;

	DBG("End DDC read %d", rc);

	return rc;
}

#define HDCP_DDC_WRITE_MAX_BYTE_NUM 32

static int msm_hdmi_ddc_write(struct hdmi *hdmi, u16 addr, u8 offset,
	u8 *data, u16 data_len)
{
	int rc;
	int retry = 10;
	u8 buf[HDCP_DDC_WRITE_MAX_BYTE_NUM];
	struct i2c_msg msgs[] = {
		{
			.addr	= addr >> 1,
			.flags	= 0,
			.len	= 1,
		}
	};

	DBG("Start DDC write");
	if (data_len > (HDCP_DDC_WRITE_MAX_BYTE_NUM - 1)) {
		pr_err("%s: write size too big\n", __func__);
		return -ERANGE;
	}

	buf[0] = offset;
	memcpy(&buf[1], data, data_len);
	msgs[0].buf = buf;
	msgs[0].len = data_len + 1;
retry:
	rc = i2c_transfer(hdmi->i2c, msgs, 1);

	retry--;
	if (rc == 1)
		rc = 0;
	else if (retry > 0)
		goto retry;
	else
		rc = -EIO;

	DBG("End DDC write %d", rc);

	return rc;
}

static int msm_hdmi_hdcp_scm_wr(struct hdmi_hdcp_ctrl *hdcp_ctrl, u32 *preg,
	u32 *pdata, u32 count)
{
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	struct qcom_scm_hdcp_req scm_buf[QCOM_SCM_HDCP_MAX_REQ_CNT];
	u32 resp, phy_addr, idx = 0;
	int i, ret = 0;

	WARN_ON(!pdata || !preg || (count == 0));

	if (hdcp_ctrl->tz_hdcp) {
		phy_addr = (u32)hdmi->mmio_phy_addr;

		while (count) {
			memset(scm_buf, 0, sizeof(scm_buf));
			for (i = 0; i < count && i < QCOM_SCM_HDCP_MAX_REQ_CNT;
				i++) {
				scm_buf[i].addr = phy_addr + preg[idx];
				scm_buf[i].val  = pdata[idx];
				idx++;
			}
			ret = qcom_scm_hdcp_req(scm_buf, i, &resp);

			if (ret || resp) {
				pr_err("%s: error: scm_call ret=%d resp=%u\n",
					__func__, ret, resp);
				ret = -EINVAL;
				break;
			}

			count -= i;
		}
	} else {
		for (i = 0; i < count; i++)
			hdmi_write(hdmi, preg[i], pdata[i]);
	}

	return ret;
}

void msm_hdmi_hdcp_irq(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 reg_val, hdcp_int_status;
	unsigned long flags;

	spin_lock_irqsave(&hdmi->reg_lock, flags);
	reg_val = hdmi_read(hdmi, REG_HDMI_HDCP_INT_CTRL);
	hdcp_int_status = reg_val & HDCP_INT_STATUS_MASK;
	if (!hdcp_int_status) {
		spin_unlock_irqrestore(&hdmi->reg_lock, flags);
		return;
	}
	/* Clear Interrupts */
	reg_val |= hdcp_int_status << 1;
	/* Clear AUTH_FAIL_INFO as well */
	if (hdcp_int_status & HDMI_HDCP_INT_CTRL_AUTH_FAIL_INT)
		reg_val |= HDMI_HDCP_INT_CTRL_AUTH_FAIL_INFO_ACK;
	hdmi_write(hdmi, REG_HDMI_HDCP_INT_CTRL, reg_val);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);

	DBG("hdcp irq %x", hdcp_int_status);

	if (hdcp_int_status & HDMI_HDCP_INT_CTRL_AUTH_SUCCESS_INT) {
		pr_info("%s:AUTH_SUCCESS_INT received\n", __func__);
		if (HDCP_STATE_AUTHENTICATING == hdcp_ctrl->hdcp_state) {
			set_bit(AUTH_RESULT_RDY_EV, &hdcp_ctrl->auth_event);
			wake_up_all(&hdcp_ctrl->auth_event_queue);
		}
	}

	if (hdcp_int_status & HDMI_HDCP_INT_CTRL_AUTH_FAIL_INT) {
		reg_val = hdmi_read(hdmi, REG_HDMI_HDCP_LINK0_STATUS);
		pr_info("%s: AUTH_FAIL_INT rcvd, LINK0_STATUS=0x%08x\n",
			__func__, reg_val);
		if (HDCP_STATE_AUTHENTICATED == hdcp_ctrl->hdcp_state)
			queue_work(hdmi->workq, &hdcp_ctrl->hdcp_reauth_work);
		else if (HDCP_STATE_AUTHENTICATING ==
				hdcp_ctrl->hdcp_state) {
			set_bit(AUTH_RESULT_RDY_EV, &hdcp_ctrl->auth_event);
			wake_up_all(&hdcp_ctrl->auth_event_queue);
		}
	}
}

static int msm_hdmi_hdcp_msleep(struct hdmi_hdcp_ctrl *hdcp_ctrl, u32 ms, u32 ev)
{
	int rc;

	rc = wait_event_timeout(hdcp_ctrl->auth_event_queue,
		!!test_bit(ev, &hdcp_ctrl->auth_event),
		msecs_to_jiffies(ms));
	if (rc) {
		pr_info("%s: msleep is canceled by event %d\n",
				__func__, ev);
		clear_bit(ev, &hdcp_ctrl->auth_event);
		return -ECANCELED;
	}

	return 0;
}

static int msm_hdmi_hdcp_read_validate_aksv(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	struct hdmi *hdmi = hdcp_ctrl->hdmi;

	/* Fetch aksv from QFPROM, this info should be public. */
	hdcp_ctrl->aksv_lsb = hdmi_qfprom_read(hdmi, HDCP_KSV_LSB);
	hdcp_ctrl->aksv_msb = hdmi_qfprom_read(hdmi, HDCP_KSV_MSB);

	/* check there are 20 ones in AKSV */
	if ((hweight32(hdcp_ctrl->aksv_lsb) + hweight32(hdcp_ctrl->aksv_msb))
			!= 20) {
		pr_err("%s: AKSV QFPROM doesn't have 20 1's, 20 0's\n",
			__func__);
		pr_err("%s: QFPROM AKSV chk failed (AKSV=%02x%08x)\n",
			__func__, hdcp_ctrl->aksv_msb,
			hdcp_ctrl->aksv_lsb);
		return -EINVAL;
	}
	DBG("AKSV=%02x%08x", hdcp_ctrl->aksv_msb, hdcp_ctrl->aksv_lsb);

	return 0;
}

static int msm_reset_hdcp_ddc_failures(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 reg_val, failure, nack0;
	int rc = 0;

	/* Check for any DDC transfer failures */
	reg_val = hdmi_read(hdmi, REG_HDMI_HDCP_DDC_STATUS);
	failure = reg_val & HDMI_HDCP_DDC_STATUS_FAILED;
	nack0 = reg_val & HDMI_HDCP_DDC_STATUS_NACK0;
	DBG("HDCP_DDC_STATUS=0x%x, FAIL=%d, NACK0=%d",
		reg_val, failure, nack0);

	if (failure) {
		/*
		 * Indicates that the last HDCP HW DDC transfer failed.
		 * This occurs when a transfer is attempted with HDCP DDC
		 * disabled (HDCP_DDC_DISABLE=1) or the number of retries
		 * matches HDCP_DDC_RETRY_CNT.
		 * Failure occurred,  let's clear it.
		 */
		DBG("DDC failure detected");

		/* First, Disable DDC */
		hdmi_write(hdmi, REG_HDMI_HDCP_DDC_CTRL_0,
			HDMI_HDCP_DDC_CTRL_0_DISABLE);

		/* ACK the Failure to Clear it */
		reg_val = hdmi_read(hdmi, REG_HDMI_HDCP_DDC_CTRL_1);
		reg_val |= HDMI_HDCP_DDC_CTRL_1_FAILED_ACK;
		hdmi_write(hdmi, REG_HDMI_HDCP_DDC_CTRL_1, reg_val);

		/* Check if the FAILURE got Cleared */
		reg_val = hdmi_read(hdmi, REG_HDMI_HDCP_DDC_STATUS);
		if (reg_val & HDMI_HDCP_DDC_STATUS_FAILED)
			pr_info("%s: Unable to clear HDCP DDC Failure\n",
				__func__);

		/* Re-Enable HDCP DDC */
		hdmi_write(hdmi, REG_HDMI_HDCP_DDC_CTRL_0, 0);
	}

	if (nack0) {
		DBG("Before: HDMI_DDC_SW_STATUS=0x%08x",
			hdmi_read(hdmi, REG_HDMI_DDC_SW_STATUS));
		/* Reset HDMI DDC software status */
		reg_val = hdmi_read(hdmi, REG_HDMI_DDC_CTRL);
		reg_val |= HDMI_DDC_CTRL_SW_STATUS_RESET;
		hdmi_write(hdmi, REG_HDMI_DDC_CTRL, reg_val);

		rc = msm_hdmi_hdcp_msleep(hdcp_ctrl, 20, AUTH_ABORT_EV);

		reg_val = hdmi_read(hdmi, REG_HDMI_DDC_CTRL);
		reg_val &= ~HDMI_DDC_CTRL_SW_STATUS_RESET;
		hdmi_write(hdmi, REG_HDMI_DDC_CTRL, reg_val);

		/* Reset HDMI DDC Controller */
		reg_val = hdmi_read(hdmi, REG_HDMI_DDC_CTRL);
		reg_val |= HDMI_DDC_CTRL_SOFT_RESET;
		hdmi_write(hdmi, REG_HDMI_DDC_CTRL, reg_val);

		/* If previous msleep is aborted, skip this msleep */
		if (!rc)
			rc = msm_hdmi_hdcp_msleep(hdcp_ctrl, 20, AUTH_ABORT_EV);

		reg_val = hdmi_read(hdmi, REG_HDMI_DDC_CTRL);
		reg_val &= ~HDMI_DDC_CTRL_SOFT_RESET;
		hdmi_write(hdmi, REG_HDMI_DDC_CTRL, reg_val);
		DBG("After: HDMI_DDC_SW_STATUS=0x%08x",
			hdmi_read(hdmi, REG_HDMI_DDC_SW_STATUS));
	}

	return rc;
}

static int msm_hdmi_hdcp_hw_ddc_clean(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	int rc;
	u32 hdcp_ddc_status, ddc_hw_status;
	u32 xfer_done, xfer_req, hw_done;
	bool hw_not_ready;
	u32 timeout_count;
	struct hdmi *hdmi = hdcp_ctrl->hdmi;

	if (hdmi_read(hdmi, REG_HDMI_DDC_HW_STATUS) == 0)
		return 0;

	/* Wait to be clean on DDC HW engine */
	timeout_count = 100;
	do {
		hdcp_ddc_status = hdmi_read(hdmi, REG_HDMI_HDCP_DDC_STATUS);
		ddc_hw_status = hdmi_read(hdmi, REG_HDMI_DDC_HW_STATUS);

		xfer_done = hdcp_ddc_status & HDMI_HDCP_DDC_STATUS_XFER_DONE;
		xfer_req = hdcp_ddc_status & HDMI_HDCP_DDC_STATUS_XFER_REQ;
		hw_done = ddc_hw_status & HDMI_DDC_HW_STATUS_DONE;
		hw_not_ready = !xfer_done || xfer_req || !hw_done;

		if (hw_not_ready)
			break;

		timeout_count--;
		if (!timeout_count) {
			pr_warn("%s: hw_ddc_clean failed\n", __func__);
			return -ETIMEDOUT;
		}

		rc = msm_hdmi_hdcp_msleep(hdcp_ctrl, 20, AUTH_ABORT_EV);
		if (rc)
			return rc;
	} while (1);

	return 0;
}

static void msm_hdmi_hdcp_reauth_work(struct work_struct *work)
{
	struct hdmi_hdcp_ctrl *hdcp_ctrl = container_of(work,
		struct hdmi_hdcp_ctrl, hdcp_reauth_work);
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	unsigned long flags;
	u32 reg_val;

	DBG("HDCP REAUTH WORK");
	/*
	 * Disable HPD circuitry.
	 * This is needed to reset the HDCP cipher engine so that when we
	 * attempt a re-authentication, HW would clear the AN0_READY and
	 * AN1_READY bits in HDMI_HDCP_LINK0_STATUS register
	 */
	spin_lock_irqsave(&hdmi->reg_lock, flags);
	reg_val = hdmi_read(hdmi, REG_HDMI_HPD_CTRL);
	reg_val &= ~HDMI_HPD_CTRL_ENABLE;
	hdmi_write(hdmi, REG_HDMI_HPD_CTRL, reg_val);

	/* Disable HDCP interrupts */
	hdmi_write(hdmi, REG_HDMI_HDCP_INT_CTRL, 0);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);

	hdmi_write(hdmi, REG_HDMI_HDCP_RESET,
		HDMI_HDCP_RESET_LINK0_DEAUTHENTICATE);

	/* Wait to be clean on DDC HW engine */
	if (msm_hdmi_hdcp_hw_ddc_clean(hdcp_ctrl)) {
		pr_info("%s: reauth work aborted\n", __func__);
		return;
	}

	/* Disable encryption and disable the HDCP block */
	hdmi_write(hdmi, REG_HDMI_HDCP_CTRL, 0);

	/* Enable HPD circuitry */
	spin_lock_irqsave(&hdmi->reg_lock, flags);
	reg_val = hdmi_read(hdmi, REG_HDMI_HPD_CTRL);
	reg_val |= HDMI_HPD_CTRL_ENABLE;
	hdmi_write(hdmi, REG_HDMI_HPD_CTRL, reg_val);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);

	/*
	 * Only retry defined times then abort current authenticating process
	 */
	if (++hdcp_ctrl->auth_retries == AUTH_RETRIES_TIME) {
		hdcp_ctrl->hdcp_state = HDCP_STATE_INACTIVE;
		hdcp_ctrl->auth_retries = 0;
		pr_info("%s: abort reauthentication!\n", __func__);

		return;
	}

	DBG("Queue AUTH WORK");
	hdcp_ctrl->hdcp_state = HDCP_STATE_AUTHENTICATING;
	queue_work(hdmi->workq, &hdcp_ctrl->hdcp_auth_work);
}

static int msm_hdmi_hdcp_auth_prepare(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 link0_status;
	u32 reg_val;
	unsigned long flags;
	int rc;

	if (!hdcp_ctrl->aksv_valid) {
		rc = msm_hdmi_hdcp_read_validate_aksv(hdcp_ctrl);
		if (rc) {
			pr_err("%s: ASKV validation failed\n", __func__);
			hdcp_ctrl->hdcp_state = HDCP_STATE_NO_AKSV;
			return -ENOTSUPP;
		}
		hdcp_ctrl->aksv_valid = true;
	}

	spin_lock_irqsave(&hdmi->reg_lock, flags);
	/* disable HDMI Encrypt */
	reg_val = hdmi_read(hdmi, REG_HDMI_CTRL);
	reg_val &= ~HDMI_CTRL_ENCRYPTED;
	hdmi_write(hdmi, REG_HDMI_CTRL, reg_val);

	/* Enabling Software DDC */
	reg_val = hdmi_read(hdmi, REG_HDMI_DDC_ARBITRATION);
	reg_val &= ~HDMI_DDC_ARBITRATION_HW_ARBITRATION;
	hdmi_write(hdmi, REG_HDMI_DDC_ARBITRATION, reg_val);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);

	/*
	 * Write AKSV read from QFPROM to the HDCP registers.
	 * This step is needed for HDCP authentication and must be
	 * written before enabling HDCP.
	 */
	hdmi_write(hdmi, REG_HDMI_HDCP_SW_LOWER_AKSV, hdcp_ctrl->aksv_lsb);
	hdmi_write(hdmi, REG_HDMI_HDCP_SW_UPPER_AKSV, hdcp_ctrl->aksv_msb);

	/*
	 * HDCP setup prior to enabling HDCP_CTRL.
	 * Setup seed values for random number An.
	 */
	hdmi_write(hdmi, REG_HDMI_HDCP_ENTROPY_CTRL0, 0xB1FFB0FF);
	hdmi_write(hdmi, REG_HDMI_HDCP_ENTROPY_CTRL1, 0xF00DFACE);

	/* Disable the RngCipher state */
	reg_val = hdmi_read(hdmi, REG_HDMI_HDCP_DEBUG_CTRL);
	reg_val &= ~HDMI_HDCP_DEBUG_CTRL_RNG_CIPHER;
	hdmi_write(hdmi, REG_HDMI_HDCP_DEBUG_CTRL, reg_val);
	DBG("HDCP_DEBUG_CTRL=0x%08x",
		hdmi_read(hdmi, REG_HDMI_HDCP_DEBUG_CTRL));

	/*
	 * Ensure that all register writes are completed before
	 * enabling HDCP cipher
	 */
	wmb();

	/*
	 * Enable HDCP
	 * This needs to be done as early as possible in order for the
	 * hardware to make An available to read
	 */
	hdmi_write(hdmi, REG_HDMI_HDCP_CTRL, HDMI_HDCP_CTRL_ENABLE);

	/*
	 * If we had stale values for the An ready bit, it should most
	 * likely be cleared now after enabling HDCP cipher
	 */
	link0_status = hdmi_read(hdmi, REG_HDMI_HDCP_LINK0_STATUS);
	DBG("After enabling HDCP Link0_Status=0x%08x", link0_status);
	if (!(link0_status &
		(HDMI_HDCP_LINK0_STATUS_AN_0_READY |
		HDMI_HDCP_LINK0_STATUS_AN_1_READY)))
		DBG("An not ready after enabling HDCP");

	/* Clear any DDC failures from previous tries before enable HDCP*/
	rc = msm_reset_hdcp_ddc_failures(hdcp_ctrl);

	return rc;
}

static void msm_hdmi_hdcp_auth_fail(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 reg_val;
	unsigned long flags;

	DBG("hdcp auth failed, queue reauth work");
	/* clear HDMI Encrypt */
	spin_lock_irqsave(&hdmi->reg_lock, flags);
	reg_val = hdmi_read(hdmi, REG_HDMI_CTRL);
	reg_val &= ~HDMI_CTRL_ENCRYPTED;
	hdmi_write(hdmi, REG_HDMI_CTRL, reg_val);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);

	hdcp_ctrl->hdcp_state = HDCP_STATE_AUTH_FAILED;
	queue_work(hdmi->workq, &hdcp_ctrl->hdcp_reauth_work);
}

static void msm_hdmi_hdcp_auth_done(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 reg_val;
	unsigned long flags;

	/*
	 * Disable software DDC before going into part3 to make sure
	 * there is no Arbitration between software and hardware for DDC
	 */
	spin_lock_irqsave(&hdmi->reg_lock, flags);
	reg_val = hdmi_read(hdmi, REG_HDMI_DDC_ARBITRATION);
	reg_val |= HDMI_DDC_ARBITRATION_HW_ARBITRATION;
	hdmi_write(hdmi, REG_HDMI_DDC_ARBITRATION, reg_val);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);

	/* enable HDMI Encrypt */
	spin_lock_irqsave(&hdmi->reg_lock, flags);
	reg_val = hdmi_read(hdmi, REG_HDMI_CTRL);
	reg_val |= HDMI_CTRL_ENCRYPTED;
	hdmi_write(hdmi, REG_HDMI_CTRL, reg_val);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);

	hdcp_ctrl->hdcp_state = HDCP_STATE_AUTHENTICATED;
	hdcp_ctrl->auth_retries = 0;
}

/*
 * hdcp authenticating part 1
 * Wait Key/An ready
 * Read BCAPS from sink
 * Write BCAPS and AKSV into HDCP engine
 * Write An and AKSV to sink
 * Read BKSV from sink and write into HDCP engine
 */
static int msm_hdmi_hdcp_wait_key_an_ready(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	int rc;
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 link0_status, keys_state;
	u32 timeout_count;
	bool an_ready;

	/* Wait for HDCP keys to be checked and validated */
	timeout_count = 100;
	do {
		link0_status = hdmi_read(hdmi, REG_HDMI_HDCP_LINK0_STATUS);
		keys_state = (link0_status >> 28) & 0x7;
		if (keys_state == HDCP_KEYS_STATE_VALID)
			break;

		DBG("Keys not ready(%d). s=%d, l0=%0x08x",
			timeout_count, keys_state, link0_status);

		timeout_count--;
		if (!timeout_count) {
			pr_err("%s: Wait key state timedout", __func__);
			return -ETIMEDOUT;
		}

		rc = msm_hdmi_hdcp_msleep(hdcp_ctrl, 20, AUTH_ABORT_EV);
		if (rc)
			return rc;
	} while (1);

	timeout_count = 100;
	do {
		link0_status = hdmi_read(hdmi, REG_HDMI_HDCP_LINK0_STATUS);
		an_ready = (link0_status & HDMI_HDCP_LINK0_STATUS_AN_0_READY)
			&& (link0_status & HDMI_HDCP_LINK0_STATUS_AN_1_READY);
		if (an_ready)
			break;

		DBG("An not ready(%d). l0_status=0x%08x",
			timeout_count, link0_status);

		timeout_count--;
		if (!timeout_count) {
			pr_err("%s: Wait An timedout", __func__);
			return -ETIMEDOUT;
		}

		rc = msm_hdmi_hdcp_msleep(hdcp_ctrl, 20, AUTH_ABORT_EV);
		if (rc)
			return rc;
	} while (1);

	return 0;
}

static int msm_hdmi_hdcp_send_aksv_an(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	int rc = 0;
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 link0_aksv_0, link0_aksv_1;
	u32 link0_an[2];
	u8 aksv[5];

	/* Read An0 and An1 */
	link0_an[0] = hdmi_read(hdmi, REG_HDMI_HDCP_RCVPORT_DATA5);
	link0_an[1] = hdmi_read(hdmi, REG_HDMI_HDCP_RCVPORT_DATA6);

	/* Read AKSV */
	link0_aksv_0 = hdmi_read(hdmi, REG_HDMI_HDCP_RCVPORT_DATA3);
	link0_aksv_1 = hdmi_read(hdmi, REG_HDMI_HDCP_RCVPORT_DATA4);

	DBG("Link ASKV=%08x%08x", link0_aksv_0, link0_aksv_1);
	/* Copy An and AKSV to byte arrays for transmission */
	aksv[0] =  link0_aksv_0        & 0xFF;
	aksv[1] = (link0_aksv_0 >> 8)  & 0xFF;
	aksv[2] = (link0_aksv_0 >> 16) & 0xFF;
	aksv[3] = (link0_aksv_0 >> 24) & 0xFF;
	aksv[4] =  link0_aksv_1        & 0xFF;

	/* Write An to offset 0x18 */
	rc = msm_hdmi_ddc_write(hdmi, HDCP_PORT_ADDR, 0x18, (u8 *)link0_an,
		(u16)sizeof(link0_an));
	if (rc) {
		pr_err("%s:An write failed\n", __func__);
		return rc;
	}
	DBG("Link0-An=%08x%08x", link0_an[0], link0_an[1]);

	/* Write AKSV to offset 0x10 */
	rc = msm_hdmi_ddc_write(hdmi, HDCP_PORT_ADDR, 0x10, aksv, 5);
	if (rc) {
		pr_err("%s:AKSV write failed\n", __func__);
		return rc;
	}
	DBG("Link0-AKSV=%02x%08x", link0_aksv_1 & 0xFF, link0_aksv_0);

	return 0;
}

static int msm_hdmi_hdcp_recv_bksv(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	int rc = 0;
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u8 bksv[5];
	u32 reg[2], data[2];

	/* Read BKSV at offset 0x00 */
	rc = msm_hdmi_ddc_read(hdmi, HDCP_PORT_ADDR, 0x00, bksv, 5);
	if (rc) {
		pr_err("%s:BKSV read failed\n", __func__);
		return rc;
	}

	hdcp_ctrl->bksv_lsb = bksv[0] | (bksv[1] << 8) |
		(bksv[2] << 16) | (bksv[3] << 24);
	hdcp_ctrl->bksv_msb = bksv[4];
	DBG(":BKSV=%02x%08x", hdcp_ctrl->bksv_msb, hdcp_ctrl->bksv_lsb);

	/* check there are 20 ones in BKSV */
	if ((hweight32(hdcp_ctrl->bksv_lsb) + hweight32(hdcp_ctrl->bksv_msb))
			!= 20) {
		pr_err(": BKSV doesn't have 20 1's and 20 0's\n");
		pr_err(": BKSV chk fail. BKSV=%02x%02x%02x%02x%02x\n",
			bksv[4], bksv[3], bksv[2], bksv[1], bksv[0]);
		return -EINVAL;
	}

	/* Write BKSV read from sink to HDCP registers */
	reg[0] = REG_HDMI_HDCP_RCVPORT_DATA0;
	data[0] = hdcp_ctrl->bksv_lsb;
	reg[1] = REG_HDMI_HDCP_RCVPORT_DATA1;
	data[1] = hdcp_ctrl->bksv_msb;
	rc = msm_hdmi_hdcp_scm_wr(hdcp_ctrl, reg, data, 2);

	return rc;
}

static int msm_hdmi_hdcp_recv_bcaps(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	int rc = 0;
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 reg, data;
	u8 bcaps;

	rc = msm_hdmi_ddc_read(hdmi, HDCP_PORT_ADDR, 0x40, &bcaps, 1);
	if (rc) {
		pr_err("%s:BCAPS read failed\n", __func__);
		return rc;
	}
	DBG("BCAPS=%02x", bcaps);

	/* receiver (0), repeater (1) */
	hdcp_ctrl->ds_type = (bcaps & BIT(6)) ? DS_REPEATER : DS_RECEIVER;

	/* Write BCAPS to the hardware */
	reg = REG_HDMI_HDCP_RCVPORT_DATA12;
	data = (u32)bcaps;
	rc = msm_hdmi_hdcp_scm_wr(hdcp_ctrl, &reg, &data, 1);

	return rc;
}

static int msm_hdmi_hdcp_auth_part1_key_exchange(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	unsigned long flags;
	int rc;

	/* Wait for AKSV key and An ready */
	rc = msm_hdmi_hdcp_wait_key_an_ready(hdcp_ctrl);
	if (rc) {
		pr_err("%s: wait key and an ready failed\n", __func__);
		return rc;
	}

	/* Read BCAPS and send to HDCP engine */
	rc = msm_hdmi_hdcp_recv_bcaps(hdcp_ctrl);
	if (rc) {
		pr_err("%s: read bcaps error, abort\n", __func__);
		return rc;
	}

	/*
	 * 1.1_Features turned off by default.
	 * No need to write AInfo since 1.1_Features is disabled.
	 */
	hdmi_write(hdmi, REG_HDMI_HDCP_RCVPORT_DATA4, 0);

	/* Send AKSV and An to sink */
	rc = msm_hdmi_hdcp_send_aksv_an(hdcp_ctrl);
	if (rc) {
		pr_err("%s:An/Aksv write failed\n", __func__);
		return rc;
	}

	/* Read BKSV and send to HDCP engine*/
	rc = msm_hdmi_hdcp_recv_bksv(hdcp_ctrl);
	if (rc) {
		pr_err("%s:BKSV Process failed\n", __func__);
		return rc;
	}

	/* Enable HDCP interrupts and ack/clear any stale interrupts */
	spin_lock_irqsave(&hdmi->reg_lock, flags);
	hdmi_write(hdmi, REG_HDMI_HDCP_INT_CTRL,
		HDMI_HDCP_INT_CTRL_AUTH_SUCCESS_ACK |
		HDMI_HDCP_INT_CTRL_AUTH_SUCCESS_MASK |
		HDMI_HDCP_INT_CTRL_AUTH_FAIL_ACK |
		HDMI_HDCP_INT_CTRL_AUTH_FAIL_MASK |
		HDMI_HDCP_INT_CTRL_AUTH_FAIL_INFO_ACK);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);

	return 0;
}

/* read R0' from sink and pass it to HDCP engine */
static int msm_hdmi_hdcp_auth_part1_recv_r0(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	int rc = 0;
	u8 buf[2];

	/*
	 * HDCP Compliance Test case 1A-01:
	 * Wait here at least 100ms before reading R0'
	 */
	rc = msm_hdmi_hdcp_msleep(hdcp_ctrl, 125, AUTH_ABORT_EV);
	if (rc)
		return rc;

	/* Read R0' at offset 0x08 */
	rc = msm_hdmi_ddc_read(hdmi, HDCP_PORT_ADDR, 0x08, buf, 2);
	if (rc) {
		pr_err("%s:R0' read failed\n", __func__);
		return rc;
	}
	DBG("R0'=%02x%02x", buf[1], buf[0]);

	/* Write R0' to HDCP registers and check to see if it is a match */
	hdmi_write(hdmi, REG_HDMI_HDCP_RCVPORT_DATA2_0,
		(((u32)buf[1]) << 8) | buf[0]);

	return 0;
}

/* Wait for authenticating result: R0/R0' are matched or not */
static int msm_hdmi_hdcp_auth_part1_verify_r0(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 link0_status;
	int rc;

	/* wait for hdcp irq, 10 sec should be long enough */
	rc = msm_hdmi_hdcp_msleep(hdcp_ctrl, 10000, AUTH_RESULT_RDY_EV);
	if (!rc) {
		pr_err("%s: Wait Auth IRQ timeout\n", __func__);
		return -ETIMEDOUT;
	}

	link0_status = hdmi_read(hdmi, REG_HDMI_HDCP_LINK0_STATUS);
	if (!(link0_status & HDMI_HDCP_LINK0_STATUS_RI_MATCHES)) {
		pr_err("%s: Authentication Part I failed\n", __func__);
		return -EINVAL;
	}

	/* Enable HDCP Encryption */
	hdmi_write(hdmi, REG_HDMI_HDCP_CTRL,
		HDMI_HDCP_CTRL_ENABLE |
		HDMI_HDCP_CTRL_ENCRYPTION_ENABLE);

	return 0;
}

static int msm_hdmi_hdcp_recv_check_bstatus(struct hdmi_hdcp_ctrl *hdcp_ctrl,
	u16 *pbstatus)
{
	int rc;
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	bool max_devs_exceeded = false, max_cascade_exceeded = false;
	u32 repeater_cascade_depth = 0, down_stream_devices = 0;
	u16 bstatus;
	u8 buf[2];

	/* Read BSTATUS at offset 0x41 */
	rc = msm_hdmi_ddc_read(hdmi, HDCP_PORT_ADDR, 0x41, buf, 2);
	if (rc) {
		pr_err("%s: BSTATUS read failed\n", __func__);
		goto error;
	}
	*pbstatus = bstatus = (buf[1] << 8) | buf[0];


	down_stream_devices = bstatus & 0x7F;
	repeater_cascade_depth = (bstatus >> 8) & 0x7;
	max_devs_exceeded = (bstatus & BIT(7)) ? true : false;
	max_cascade_exceeded = (bstatus & BIT(11)) ? true : false;

	if (down_stream_devices == 0) {
		/*
		 * If no downstream devices are attached to the repeater
		 * then part II fails.
		 * todo: The other approach would be to continue PART II.
		 */
		pr_err("%s: No downstream devices\n", __func__);
		rc = -EINVAL;
		goto error;
	}

	/*
	 * HDCP Compliance 1B-05:
	 * Check if no. of devices connected to repeater
	 * exceed max_devices_connected from bit 7 of Bstatus.
	 */
	if (max_devs_exceeded) {
		pr_err("%s: no. of devs connected exceeds max allowed",
			__func__);
		rc = -EINVAL;
		goto error;
	}

	/*
	 * HDCP Compliance 1B-06:
	 * Check if no. of cascade connected to repeater
	 * exceed max_cascade_connected from bit 11 of Bstatus.
	 */
	if (max_cascade_exceeded) {
		pr_err("%s: no. of cascade conn exceeds max allowed",
			__func__);
		rc = -EINVAL;
		goto error;
	}

error:
	hdcp_ctrl->dev_count = down_stream_devices;
	hdcp_ctrl->max_cascade_exceeded = max_cascade_exceeded;
	hdcp_ctrl->max_dev_exceeded = max_devs_exceeded;
	hdcp_ctrl->depth = repeater_cascade_depth;
	return rc;
}

static int msm_hdmi_hdcp_auth_part2_wait_ksv_fifo_ready(
	struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	int rc;
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 reg, data;
	u32 timeout_count;
	u16 bstatus;
	u8 bcaps;

	/*
	 * Wait until READY bit is set in BCAPS, as per HDCP specifications
	 * maximum permitted time to check for READY bit is five seconds.
	 */
	timeout_count = 100;
	do {
		/* Read BCAPS at offset 0x40 */
		rc = msm_hdmi_ddc_read(hdmi, HDCP_PORT_ADDR, 0x40, &bcaps, 1);
		if (rc) {
			pr_err("%s: BCAPS read failed\n", __func__);
			return rc;
		}

		if (bcaps & BIT(5))
			break;

		timeout_count--;
		if (!timeout_count) {
			pr_err("%s: Wait KSV fifo ready timedout", __func__);
			return -ETIMEDOUT;
		}

		rc = msm_hdmi_hdcp_msleep(hdcp_ctrl, 20, AUTH_ABORT_EV);
		if (rc)
			return rc;
	} while (1);

	rc = msm_hdmi_hdcp_recv_check_bstatus(hdcp_ctrl, &bstatus);
	if (rc) {
		pr_err("%s: bstatus error\n", __func__);
		return rc;
	}

	/* Write BSTATUS and BCAPS to HDCP registers */
	reg = REG_HDMI_HDCP_RCVPORT_DATA12;
	data = bcaps | (bstatus << 8);
	rc = msm_hdmi_hdcp_scm_wr(hdcp_ctrl, &reg, &data, 1);
	if (rc) {
		pr_err("%s: BSTATUS write failed\n", __func__);
		return rc;
	}

	return 0;
}

/*
 * hdcp authenticating part 2: 2nd
 * read ksv fifo from sink
 * transfer V' from sink to HDCP engine
 * reset SHA engine
 */
static int msm_hdmi_hdcp_transfer_v_h(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	int rc = 0;
	struct hdmi_hdcp_reg_data reg_data[]  = {
		{REG_HDMI_HDCP_RCVPORT_DATA7,  0x20, "V' H0"},
		{REG_HDMI_HDCP_RCVPORT_DATA8,  0x24, "V' H1"},
		{REG_HDMI_HDCP_RCVPORT_DATA9,  0x28, "V' H2"},
		{REG_HDMI_HDCP_RCVPORT_DATA10, 0x2C, "V' H3"},
		{REG_HDMI_HDCP_RCVPORT_DATA11, 0x30, "V' H4"},
	};
	struct hdmi_hdcp_reg_data *rd;
	u32 size = ARRAY_SIZE(reg_data);
	u32 reg[ARRAY_SIZE(reg_data)];
	u32 data[ARRAY_SIZE(reg_data)];
	int i;

	for (i = 0; i < size; i++) {
		rd = &reg_data[i];
		rc = msm_hdmi_ddc_read(hdmi, HDCP_PORT_ADDR,
			rd->off, (u8 *)&data[i], (u16)sizeof(data[i]));
		if (rc) {
			pr_err("%s: Read %s failed\n", __func__, rd->name);
			goto error;
		}

		DBG("%s =%x", rd->name, data[i]);
		reg[i] = reg_data[i].reg_id;
	}

	rc = msm_hdmi_hdcp_scm_wr(hdcp_ctrl, reg, data, size);

error:
	return rc;
}

static int msm_hdmi_hdcp_recv_ksv_fifo(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	int rc;
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 ksv_bytes;

	ksv_bytes = 5 * hdcp_ctrl->dev_count;

	rc = msm_hdmi_ddc_read(hdmi, HDCP_PORT_ADDR, 0x43,
		hdcp_ctrl->ksv_list, ksv_bytes);
	if (rc)
		pr_err("%s: KSV FIFO read failed\n", __func__);

	return rc;
}

static int msm_hdmi_hdcp_reset_sha_engine(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	u32 reg[2], data[2];
	u32 rc  = 0;

	reg[0] = REG_HDMI_HDCP_SHA_CTRL;
	data[0] = HDCP_REG_ENABLE;
	reg[1] = REG_HDMI_HDCP_SHA_CTRL;
	data[1] = HDCP_REG_DISABLE;

	rc = msm_hdmi_hdcp_scm_wr(hdcp_ctrl, reg, data, 2);

	return rc;
}

static int msm_hdmi_hdcp_auth_part2_recv_ksv_fifo(
	struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	int rc;
	u32 timeout_count;

	/*
	 * Read KSV FIFO over DDC
	 * Key Selection vector FIFO Used to pull downstream KSVs
	 * from HDCP Repeaters.
	 * All bytes (DEVICE_COUNT * 5) must be read in a single,
	 * auto incrementing access.
	 * All bytes read as 0x00 for HDCP Receivers that are not
	 * HDCP Repeaters (REPEATER == 0).
	 */
	timeout_count = 100;
	do {
		rc = msm_hdmi_hdcp_recv_ksv_fifo(hdcp_ctrl);
		if (!rc)
			break;

		timeout_count--;
		if (!timeout_count) {
			pr_err("%s: Recv ksv fifo timedout", __func__);
			return -ETIMEDOUT;
		}

		rc = msm_hdmi_hdcp_msleep(hdcp_ctrl, 25, AUTH_ABORT_EV);
		if (rc)
			return rc;
	} while (1);

	rc = msm_hdmi_hdcp_transfer_v_h(hdcp_ctrl);
	if (rc) {
		pr_err("%s: transfer V failed\n", __func__);
		return rc;
	}

	/* reset SHA engine before write ksv fifo */
	rc = msm_hdmi_hdcp_reset_sha_engine(hdcp_ctrl);
	if (rc) {
		pr_err("%s: fail to reset sha engine\n", __func__);
		return rc;
	}

	return 0;
}

/*
 * Write KSV FIFO to HDCP_SHA_DATA.
 * This is done 1 byte at time starting with the LSB.
 * Once 64 bytes have been written, we need to poll for
 * HDCP_SHA_BLOCK_DONE before writing any further
 * If the last byte is written, we need to poll for
 * HDCP_SHA_COMP_DONE to wait until HW finish
 */
static int msm_hdmi_hdcp_write_ksv_fifo(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	int i;
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 ksv_bytes, last_byte = 0;
	u8 *ksv_fifo = NULL;
	u32 reg_val, data, reg;
	u32 rc  = 0;

	ksv_bytes  = 5 * hdcp_ctrl->dev_count;

	/* Check if need to wait for HW completion */
	if (hdcp_ctrl->ksv_fifo_w_index) {
		reg_val = hdmi_read(hdmi, REG_HDMI_HDCP_SHA_STATUS);
		DBG("HDCP_SHA_STATUS=%08x", reg_val);
		if (hdcp_ctrl->ksv_fifo_w_index == ksv_bytes) {
			/* check COMP_DONE if last write */
			if (reg_val & HDMI_HDCP_SHA_STATUS_COMP_DONE) {
				DBG("COMP_DONE");
				return 0;
			} else {
				return -EAGAIN;
			}
		} else {
			/* check BLOCK_DONE if not last write */
			if (!(reg_val & HDMI_HDCP_SHA_STATUS_BLOCK_DONE))
				return -EAGAIN;

			DBG("BLOCK_DONE");
		}
	}

	ksv_bytes  -= hdcp_ctrl->ksv_fifo_w_index;
	if (ksv_bytes <= 64)
		last_byte = 1;
	else
		ksv_bytes = 64;

	ksv_fifo = hdcp_ctrl->ksv_list;
	ksv_fifo += hdcp_ctrl->ksv_fifo_w_index;

	for (i = 0; i < ksv_bytes; i++) {
		/* Write KSV byte and set DONE bit[0] for last byte*/
		reg_val = ksv_fifo[i] << 16;
		if ((i == (ksv_bytes - 1)) && last_byte)
			reg_val |= HDMI_HDCP_SHA_DATA_DONE;

		reg = REG_HDMI_HDCP_SHA_DATA;
		data = reg_val;
		rc = msm_hdmi_hdcp_scm_wr(hdcp_ctrl, &reg, &data, 1);

		if (rc)
			return rc;
	}

	hdcp_ctrl->ksv_fifo_w_index += ksv_bytes;

	/*
	 *return -EAGAIN to notify caller to wait for COMP_DONE or BLOCK_DONE
	 */
	return -EAGAIN;
}

/* write ksv fifo into HDCP engine */
static int msm_hdmi_hdcp_auth_part2_write_ksv_fifo(
	struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	int rc;
	u32 timeout_count;

	hdcp_ctrl->ksv_fifo_w_index = 0;
	timeout_count = 100;
	do {
		rc = msm_hdmi_hdcp_write_ksv_fifo(hdcp_ctrl);
		if (!rc)
			break;

		if (rc != -EAGAIN)
			return rc;

		timeout_count--;
		if (!timeout_count) {
			pr_err("%s: Write KSV fifo timedout", __func__);
			return -ETIMEDOUT;
		}

		rc = msm_hdmi_hdcp_msleep(hdcp_ctrl, 20, AUTH_ABORT_EV);
		if (rc)
			return rc;
	} while (1);

	return 0;
}

static int msm_hdmi_hdcp_auth_part2_check_v_match(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	int rc = 0;
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 link0_status;
	u32 timeout_count = 100;

	do {
		link0_status = hdmi_read(hdmi, REG_HDMI_HDCP_LINK0_STATUS);
		if (link0_status & HDMI_HDCP_LINK0_STATUS_V_MATCHES)
			break;

		timeout_count--;
		if (!timeout_count) {
				pr_err("%s: HDCP V Match timedout", __func__);
				return -ETIMEDOUT;
		}

		rc = msm_hdmi_hdcp_msleep(hdcp_ctrl, 20, AUTH_ABORT_EV);
		if (rc)
			return rc;
	} while (1);

	return 0;
}

static void msm_hdmi_hdcp_auth_work(struct work_struct *work)
{
	struct hdmi_hdcp_ctrl *hdcp_ctrl = container_of(work,
		struct hdmi_hdcp_ctrl, hdcp_auth_work);
	int rc;

	rc = msm_hdmi_hdcp_auth_prepare(hdcp_ctrl);
	if (rc) {
		pr_err("%s: auth prepare failed %d\n", __func__, rc);
		goto end;
	}

	/* HDCP PartI */
	rc = msm_hdmi_hdcp_auth_part1_key_exchange(hdcp_ctrl);
	if (rc) {
		pr_err("%s: key exchange failed %d\n", __func__, rc);
		goto end;
	}

	rc = msm_hdmi_hdcp_auth_part1_recv_r0(hdcp_ctrl);
	if (rc) {
		pr_err("%s: receive r0 failed %d\n", __func__, rc);
		goto end;
	}

	rc = msm_hdmi_hdcp_auth_part1_verify_r0(hdcp_ctrl);
	if (rc) {
		pr_err("%s: verify r0 failed %d\n", __func__, rc);
		goto end;
	}
	pr_info("%s: Authentication Part I successful\n", __func__);
	if (hdcp_ctrl->ds_type == DS_RECEIVER)
		goto end;

	/* HDCP PartII */
	rc = msm_hdmi_hdcp_auth_part2_wait_ksv_fifo_ready(hdcp_ctrl);
	if (rc) {
		pr_err("%s: wait ksv fifo ready failed %d\n", __func__, rc);
		goto end;
	}

	rc = msm_hdmi_hdcp_auth_part2_recv_ksv_fifo(hdcp_ctrl);
	if (rc) {
		pr_err("%s: recv ksv fifo failed %d\n", __func__, rc);
		goto end;
	}

	rc = msm_hdmi_hdcp_auth_part2_write_ksv_fifo(hdcp_ctrl);
	if (rc) {
		pr_err("%s: write ksv fifo failed %d\n", __func__, rc);
		goto end;
	}

	rc = msm_hdmi_hdcp_auth_part2_check_v_match(hdcp_ctrl);
	if (rc)
		pr_err("%s: check v match failed %d\n", __func__, rc);

end:
	if (rc == -ECANCELED) {
		pr_info("%s: hdcp authentication canceled\n", __func__);
	} else if (rc == -ENOTSUPP) {
		pr_info("%s: hdcp is not supported\n", __func__);
	} else if (rc) {
		pr_err("%s: hdcp authentication failed\n", __func__);
		msm_hdmi_hdcp_auth_fail(hdcp_ctrl);
	} else {
		msm_hdmi_hdcp_auth_done(hdcp_ctrl);
	}
}

void msm_hdmi_hdcp_on(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	u32 reg_val;
	unsigned long flags;

	if ((HDCP_STATE_INACTIVE != hdcp_ctrl->hdcp_state) ||
		(HDCP_STATE_NO_AKSV == hdcp_ctrl->hdcp_state)) {
		DBG("still active or activating or no askv. returning");
		return;
	}

	/* clear HDMI Encrypt */
	spin_lock_irqsave(&hdmi->reg_lock, flags);
	reg_val = hdmi_read(hdmi, REG_HDMI_CTRL);
	reg_val &= ~HDMI_CTRL_ENCRYPTED;
	hdmi_write(hdmi, REG_HDMI_CTRL, reg_val);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);

	hdcp_ctrl->auth_event = 0;
	hdcp_ctrl->hdcp_state = HDCP_STATE_AUTHENTICATING;
	hdcp_ctrl->auth_retries = 0;
	queue_work(hdmi->workq, &hdcp_ctrl->hdcp_auth_work);
}

void msm_hdmi_hdcp_off(struct hdmi_hdcp_ctrl *hdcp_ctrl)
{
	struct hdmi *hdmi = hdcp_ctrl->hdmi;
	unsigned long flags;
	u32 reg_val;

	if ((HDCP_STATE_INACTIVE == hdcp_ctrl->hdcp_state) ||
		(HDCP_STATE_NO_AKSV == hdcp_ctrl->hdcp_state)) {
		DBG("hdcp inactive or no aksv. returning");
		return;
	}

	/*
	 * Disable HPD circuitry.
	 * This is needed to reset the HDCP cipher engine so that when we
	 * attempt a re-authentication, HW would clear the AN0_READY and
	 * AN1_READY bits in HDMI_HDCP_LINK0_STATUS register
	 */
	spin_lock_irqsave(&hdmi->reg_lock, flags);
	reg_val = hdmi_read(hdmi, REG_HDMI_HPD_CTRL);
	reg_val &= ~HDMI_HPD_CTRL_ENABLE;
	hdmi_write(hdmi, REG_HDMI_HPD_CTRL, reg_val);

	/*
	 * Disable HDCP interrupts.
	 * Also, need to set the state to inactive here so that any ongoing
	 * reauth works will know that the HDCP session has been turned off.
	 */
	hdmi_write(hdmi, REG_HDMI_HDCP_INT_CTRL, 0);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);

	/*
	 * Cancel any pending auth/reauth attempts.
	 * If one is ongoing, this will wait for it to finish.
	 * No more reauthentication attempts will be scheduled since we
	 * set the current state to inactive.
	 */
	set_bit(AUTH_ABORT_EV, &hdcp_ctrl->auth_event);
	wake_up_all(&hdcp_ctrl->auth_event_queue);
	cancel_work_sync(&hdcp_ctrl->hdcp_auth_work);
	cancel_work_sync(&hdcp_ctrl->hdcp_reauth_work);

	hdmi_write(hdmi, REG_HDMI_HDCP_RESET,
		HDMI_HDCP_RESET_LINK0_DEAUTHENTICATE);

	/* Disable encryption and disable the HDCP block */
	hdmi_write(hdmi, REG_HDMI_HDCP_CTRL, 0);

	spin_lock_irqsave(&hdmi->reg_lock, flags);
	reg_val = hdmi_read(hdmi, REG_HDMI_CTRL);
	reg_val &= ~HDMI_CTRL_ENCRYPTED;
	hdmi_write(hdmi, REG_HDMI_CTRL, reg_val);

	/* Enable HPD circuitry */
	reg_val = hdmi_read(hdmi, REG_HDMI_HPD_CTRL);
	reg_val |= HDMI_HPD_CTRL_ENABLE;
	hdmi_write(hdmi, REG_HDMI_HPD_CTRL, reg_val);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);

	hdcp_ctrl->hdcp_state = HDCP_STATE_INACTIVE;

	DBG("HDCP: Off");
}

struct hdmi_hdcp_ctrl *msm_hdmi_hdcp_init(struct hdmi *hdmi)
{
	struct hdmi_hdcp_ctrl *hdcp_ctrl = NULL;

	if (!hdmi->qfprom_mmio) {
		pr_err("%s: HDCP is not supported without qfprom\n",
			__func__);
		return ERR_PTR(-EINVAL);
	}

	hdcp_ctrl = kzalloc(sizeof(*hdcp_ctrl), GFP_KERNEL);
	if (!hdcp_ctrl)
		return ERR_PTR(-ENOMEM);

	INIT_WORK(&hdcp_ctrl->hdcp_auth_work, msm_hdmi_hdcp_auth_work);
	INIT_WORK(&hdcp_ctrl->hdcp_reauth_work, msm_hdmi_hdcp_reauth_work);
	init_waitqueue_head(&hdcp_ctrl->auth_event_queue);
	hdcp_ctrl->hdmi = hdmi;
	hdcp_ctrl->hdcp_state = HDCP_STATE_INACTIVE;
	hdcp_ctrl->aksv_valid = false;

	if (qcom_scm_hdcp_available())
		hdcp_ctrl->tz_hdcp = true;
	else
		hdcp_ctrl->tz_hdcp = false;

	return hdcp_ctrl;
}

void msm_hdmi_hdcp_destroy(struct hdmi *hdmi)
{
	if (hdmi) {
		kfree(hdmi->hdcp_ctrl);
		hdmi->hdcp_ctrl = NULL;
	}
}
