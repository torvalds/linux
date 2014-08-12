#include <linux/delay.h>
#include "rk3036_hdmi.h"
#include "rk3036_hdmi_hw.h"
#include "rk3036_hdcp.h"
int is_1b_03_test(void)
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

void rk3036_set_colorbar(int enable)
{
	static int display_mask;
	int reg_value;
	int tmds_clk;

	tmds_clk = hdmi_dev->driver.tmdsclk;
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

void rk3036_hdcp_disable(void)
{
	int reg_value;
	int tmds_clk;

	tmds_clk = hdmi_dev->driver.tmdsclk;
	if (tmds_clk <= (HDMI_SYS_FREG_CLK << 2)) {
		hdmi_readl(hdmi_dev, SYS_CTRL, &reg_value);
		hdmi_msk_reg(hdmi_dev, SYS_CTRL,
			     m_REG_CLK_SOURCE, v_REG_CLK_SOURCE_SYS);
	}

	/* Diable HDCP Interrupt*/
	hdmi_writel(hdmi_dev, HDCP_INT_MASK1, 0x00);
	/* Stop and Reset HDCP*/
	hdmi_msk_reg(hdmi_dev, HDCP_CTRL1,
		     m_AUTH_START | m_AUTH_STOP | m_HDCP_RESET,
		     v_AUTH_START(0) | v_AUTH_STOP(1) | v_HDCP_RESET(1));

	if (tmds_clk <= (HDMI_SYS_FREG_CLK << 2))
		hdmi_writel(hdmi_dev, SYS_CTRL, reg_value);
}

int rk3036_hdcp_key_check(struct hdcp_keys *key)
{
	int i = 0;

	DBG("HDCP: check hdcp key\n");
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
int rk3036_hdcp_load_key2mem(struct hdcp_keys *key)
{
	int i;

	DBG("HDCP: rk3036_hdcp_load_key2mem start\n");
	/* Write 40 private key*/
	for (i = 0; i < HDCP_PRIVATE_KEY_SIZE; i++)
		hdmi_writel(hdmi_dev, HDCP_KEY_FIFO, key->devicekey[i]);
	/* Write 1st aksv*/
	for (i = 0; i < 5; i++)
		hdmi_writel(hdmi_dev, HDCP_KEY_FIFO, key->ksv[i]);
	/* Write 2nd aksv*/
	for (i = 0; i < 5; i++)
		hdmi_writel(hdmi_dev, HDCP_KEY_FIFO, key->ksv[i]);
	DBG("HDCP: rk3036_hdcp_load_key2mem end\n");
	return HDCP_OK;
}

int rk3036_hdcp_start_authentication(void)
{
	int temp;
	int retry = 0;
	int tmds_clk;

	tmds_clk = hdmi_dev->driver.tmdsclk;
	if (hdcp->keys == NULL) {
		HDCP_WARN("HDCP: key is not loaded\n");
		return HDCP_KEY_ERR;
	}
	if (rk3036_hdcp_key_check(hdcp->keys) == HDCP_KEY_INVALID) {
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
		rk3036_hdcp_load_key2mem(hdcp->keys);
		msleep(1);
		hdmi_readl(hdmi_dev, HDCP_KEY_STATUS, &temp);
		retry++;
	}
	/*Config DDC bus clock: ddc_clk = reg_clk/4*(reg 0x4c 0x4b)*/
	retry = hdmi_dev->hclk_rate/(HDCP_DDC_CLK << 2);
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

int rk3036_hdcp_stop_authentication(void)
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
#if 0
int	rk3036_hdcp_check_bksv(void)
{
	int i, j;
	char temp = 0, bksv[5];
	char *invalidkey;

	for (i = 0; i < 5; i++) {
		hdmi_readl(HDCP_KSV_BYTE0 + (4 - i), &temp);
		bksv[i] = temp & 0xFF;
	}
	DBG("bksv is 0x%02x%02x%02x%02x%02x",
	    bksv[0], bksv[1], bksv[2], bksv[3], bksv[4]);

	temp = 0;
	for (i = 0; i < 5; i++) {
		for (j = 0; j < 8; j++) {
			if (bksv[i] & 0x01)
				temp++;
			bksv[i] >>= 1;
		}
	}
	if (temp != 20)
		return HDCP_KSV_ERR;
	for (i = 0; i < hdcp->invalidkey; i++) {
		invalidkey = hdcp->invalidkeys + i*5;
		if (memcmp(bksv, invalidkey, 5) == 0) {
			HDCP_WARN("HDCP:BKSV was revocated!\n");
			hdmi_msk_reg(HDCP_CTRL1, m_BKSV_INVALID | m_ENCRYPT_ENABLE,
				     v_BKSV_INVALID(1) | v_ENCRYPT_ENABLE(1));
			return HDCP_KSV_ERR;
		}
	}
	hdmi_msk_reg(HDCP_CTRL1, m_BKSV_VALID | m_ENCRYPT_ENABLE,
		     v_BKSV_VALID(1) | v_ENCRYPT_ENABLE(1));
	return HDCP_OK;
}
#endif

int rk3036_hdcp_error(int value)
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
void rk3036_hdcp_interrupt(char *status1, char *status2)
{
	int interrupt1 = 0;
	int interrupt2 = 0;
	int temp = 0;
	int tmds_clk;

	tmds_clk = hdmi_dev->driver.tmdsclk;
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
			rk3036_hdcp_error(temp);
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
/*
	hdmi_readl(HDCP_ERROR, &temp);
	DBG("HDCP: Error reg 0x65 = 0x%02x\n", temp);
*/
}
