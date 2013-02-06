/*
 * Copyright (C) 2011 Samsung Electronics
 *
 * Authors: Adam Hampson <ahampson@sta.samsung.com>
 *          Erik Gilling <konkers@android.com>
 *
 * Additional contributions by : Shankar Bandal <shankar.b@samsung.com>
 *                               Dharam Kumar <dharam.kr@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/sii9234.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/wakelock.h>
#include <linux/earlysuspend.h>
#ifdef CONFIG_EXTCON
#include <linux/extcon.h>
#endif

/*////////////////////////////////////////////////////////////////////////////*/
/*/////////////////////////	definition area		//////////////////////*/
/*////////////////////////////////////////////////////////////////////////////*/

#define __CONFIG_USE_TIMER__
#define	__CONFIG_RSEN_LOST_PATCH__
/* #define __CONFIG_MHL_SWING_LEVEL__ */
#define	__CONFIG_SS_FACTORY__
#define	__CONFIG_MHL_DEBUG__
#if defined(CONFIG_MACH_T0) || defined(CONFIG_MACH_M3) \
	|| defined(CONFIG_MACH_M0_DUOSCTC)
#	define __CONFIG_MHL_VER_1_2__
#else
#	define __CONFIG_MHL_VER_1_1__
#endif
/* #define	__SII9234_MUTEX_DEBUG__ */
/*////////////////////////////////////////////////////////////////////////////*/
/*//////////////////     dependence hader file area     //////////////////////*/
/*////////////////////////////////////////////////////////////////////////////*/

#ifdef __CONFIG_MHL_SWING_LEVEL__
#include <linux/ctype.h>
#endif
#include "sii9234_driver.h"

/*////////////////////////////////////////////////////////////////////////////*/
/*//////////////////////////     mecro area     //////////////////////////////*/
/*////////////////////////////////////////////////////////////////////////////*/

#undef pr_debug
#ifdef __CONFIG_MHL_DEBUG__
int mhl_dbg_flag;
#	define pr_debug(fmt, ...) \
	do { \
		if (unlikely(mhl_dbg_flag == 1)) { \
			printk(KERN_INFO fmt, ##__VA_ARGS__); \
		} \
	} while (0)
#else
#	define pr_debug(fmt, ...)
#endif

#ifdef __SII9234_MUTEX_DEBUG__
	int g_mutex_cnt;
	int g_cbus_mutex_cnt;
#	define	sii9234_mutex_lock(prm)	\
	do { \
		printk(KERN_INFO"%s(%d) mutex++:%d\n", __func__,\
				__LINE__, ++g_mutex_cnt); \
		mutex_lock(prm); \
		printk(KERN_INFO"%s(%d) mutex--:%d\n", __func__,\
				__LINE__, g_mutex_cnt); \
	} while (0)
#	define	sii9234_mutex_unlock(prm) \
	do { \
		printk(KERN_INFO"%s(%d) mutex_unlock:%d\n", __func__, \
				__LINE__, --g_mutex_cnt); \
		mutex_unlock(prm); \
	} while (0)
#	define	sii9234_cbus_mutex_lock(prm) \
	do { \
		printk(KERN_INFO"%s(%d) cbus mutex++:%d\n", __func__, \
				__LINE__, ++g_cbus_mutex_cnt); \
		mutex_lock(prm); \
		printk(KERN_INFO"%s(%d) cbus mutex--:%d\n", __func__, \
				__LINE__, g_cbus_mutex_cnt); \
	} while (0)
#	define	sii9234_cbus_mutex_unlock(prm)	\
	do { \
		printk(KERN_INFO"%s(%d) cbus mutex_unlock:%d\n", __func__, \
				__LINE__, --g_cbus_mutex_cnt); \
		mutex_unlock(prm); \
	} while (0)
#else
#	define	sii9234_mutex_lock(prm)	mutex_lock(prm);
#	define	sii9234_mutex_unlock(prm)	mutex_unlock(prm);
#	define	sii9234_cbus_mutex_lock(prm)	mutex_lock(prm);
#	define	sii9234_cbus_mutex_unlock(prm)	mutex_unlock(prm);
#endif /*__SII9234_MUTEX_DEBUG__*/

static struct mutex sii9234_irq_lock;
#define	__SII9234_IRQ_DEBUG__
#ifdef __SII9234_IRQ_DEBUG__
int en_irq;
#	define sii9234_enable_irq() \
	do { \
		mutex_lock(&sii9234_irq_lock); \
		if (atomic_read(&sii9234->is_irq_enabled) == false) { \
			atomic_set(&sii9234->is_irq_enabled, true); \
			enable_irq(sii9234->pdata->mhl_tx_client->irq); \
			printk(KERN_INFO"%s() : enable_irq(%d)\n", __func__, \
					++en_irq); \
		} else { \
			printk(KERN_INFO"%s() : irq is already enabled(%d)\n" \
					, __func__, en_irq); \
		} \
		mutex_unlock(&sii9234_irq_lock); \
	} while (0)

#	define sii9234_disable_irq() \
	do { \
		mutex_lock(&sii9234_irq_lock); \
		if (atomic_read(&sii9234->is_irq_enabled) == true) { \
			atomic_set(&sii9234->is_irq_enabled, false); \
			disable_irq_nosync(sii9234->pdata->mhl_tx_client->irq);\
			printk(KERN_INFO"%s() : disable_irq(%d)\n", \
					__func__, --en_irq); \
		} else { \
			printk(KERN_INFO"%s() : irq is already disabled(%d)\n"\
					, __func__, en_irq); \
		} \
		mutex_unlock(&sii9234_irq_lock); \
	} while (0)
#else
#	define sii9234_enable_irq() \
	do { \
		mutex_lock(&sii9234_irq_lock); \
		if (atomic_read(&sii9234->is_irq_enabled) == false) { \
			atomic_set(&sii9234->is_irq_enabled, true); \
			enable_irq(sii9234->pdata->mhl_tx_client->irq); \
		} \
		mutex_unlock(&sii9234_irq_lock); \
	} while (0)

#	define sii9234_disable_irq() \
	do { \
		mutex_lock(&sii9234_irq_lock); \
		if (atomic_read(&sii9234->is_irq_enabled) == true) { \
			atomic_set(&sii9234->is_irq_enabled, false); \
			disable_irq_nosync(sii9234->pdata->mhl_tx_client->irq);\
		} \
		mutex_unlock(&sii9234_irq_lock); \
	} while (0)
#endif /*__SII9234_IRQ_DEBUG__*/

/*////////////////////////////////////////////////////////////////////////////*/
/*////////////////////     global value area     /////////////////////////////*/
/*////////////////////////////////////////////////////////////////////////////*/

#ifdef __MHL_NEW_CBUS_MSC_CMD__
LIST_HEAD(g_msc_packet_list);
static int g_list_cnt;
static struct workqueue_struct *sii9234_msc_wq;
#endif

static struct cbus_packet cbus_pkt_buf[CBUS_PKT_BUF_COUNT];
#ifdef __CONFIG_USE_TIMER__
static int cbus_command_abort_state;
#endif

#ifdef __CONFIG_TMDS_OFFON_WORKAROUND__
static struct workqueue_struct *sii9234_tmds_offon_wq;
#endif

/*////////////////////////////////////////////////////////////////////////////*/
/*/////////////////     function declaration  area     ///////////////////////*/
/*////////////////////////////////////////////////////////////////////////////*/

static u8 sii9234_tmds_control(struct sii9234_data *sii9234, bool enable);
#ifdef __CONFIG_TMDS_OFFON_WORKAROUND__
static u8 sii9234_tmds_control2(struct sii9234_data *sii9234, bool enable);
#endif
static bool cbus_command_request(struct sii9234_data *sii9234,
				 enum cbus_command command, u8 offset, u8 data);
static void cbus_command_response(struct sii9234_data *sii9234);
static irqreturn_t sii9234_irq_thread(int irq, void *data);

static void goto_d3(void);

/*////////////////////////////////////////////////////////////////////////////*/
/*////////////////     function description  area     ////////////////////////*/
/*////////////////////////////////////////////////////////////////////////////*/

#ifdef CONFIG_MACH_MIDAS
void sii9234_wake_lock(void)
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);
	if (!sii9234 || !sii9234->pdata) {
		pr_err("[ERROR] %s() MHL driver has not initailized.\n",
			 __func__);
		return;
	}
	wake_lock(&sii9234->mhl_wake_lock);
	pr_debug("%s()\n", __func__);
}
EXPORT_SYMBOL(sii9234_wake_lock);

void sii9234_wake_unlock(void)
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);
	if (!sii9234 || !sii9234->pdata) {
		pr_err("[ERROR] %s() MHL driver has not initailized.\n",
			 __func__);
		return;
	}
	wake_unlock(&sii9234->mhl_wake_lock);
	pr_debug("%s()\n", __func__);
}
EXPORT_SYMBOL(sii9234_wake_unlock);
#endif

#ifdef __CONFIG_MHL_SWING_LEVEL__
static ssize_t sii9234_swing_test_show(struct class *class,
				       struct class_attribute *attr,
				       char *buf)
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);
	return sprintf(buf, "mhl_show_value : 0%o(0x%x)\n",
	       sii9234->pdata->swing_level, sii9234->pdata->swing_level);

}

static ssize_t sii9234_swing_test_store(struct class *class,
					struct class_attribute *attr,
					const char *buf, size_t size)
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);
	const char *p = buf;
	int data, clk, ret;
	unsigned int base, value;

	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		base = 16;
	else
		base = 8;

	ret = kstrtouint(p, base, &value);
	printk(KERN_INFO "\n%s(): ret = %d, value = %d(=0%o=0x%x)\n", __func__,
		 ret, value, value, value);
	if (ret != 0 || value < 0 || value > 0xff) {
		printk(KERN_INFO "[ERROR] %s(): The value is invald!",
				__func__);
		return size;
	}
	data = value & 070;
	clk = value & 07;
	sii9234->pdata->swing_level = 0300 | data | clk;
	printk(KERN_INFO "%s(): mhl_store_value : 0%o(0x%x)\n", __func__,
		 sii9234->pdata->swing_level, sii9234->pdata->swing_level);
	return size;
}

static CLASS_ATTR(swing, 0666,
		sii9234_swing_test_show, sii9234_swing_test_store);
#endif

#if defined(CONFIG_SAMSUNG_USE_11PIN_CONNECTOR) && !defined(CONFIG_MACH_P4NOTE)
static int is_mhl_cable_connected(void)
{
#	ifdef CONFIG_SAMSUNG_SMARTDOCK
		if (max77693_muic_get_status1_adc_value() == ADC_SMARTDOCK)
			return 1;
		else
			return max77693_muic_get_status1_adc1k_value();
#	else
		return max77693_muic_get_status1_adc1k_value();
#	endif
}
#endif
#if defined(CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE) &&\
	!defined(CONFIG_SAMSUNG_MHL_9290)
/*
 *  This workaournd is for prevent of HDMI_HPD pin glitch.
 *  HDMI_HPD pin is from MHL IC's HPD pin to AP's HDMI HPD pin.
 *  When connect/disconnect the MHL Dongle, or turn on/off MHL IC,
 * or other cases, the glitch can be generated.
 *  - Cases
 *  1) 2012 Sept
 *    a. When HDMI is connected.
 *    b. Remove TA/HDMI cable.
 *    c. Remove MHL dongle about 0.5 sec(shorter then 1 sec) after b.
 *    d. Glitch is occured with HDMI connected/disconnected pop-up.
 *
 *  void mhl_hpd_handler(bool onoff) must be defined on HDMI driver.
 *  That fuction will control the HDMI HPD high/low intterpt on/off.
 *
 *
 *  int (*hpd_intr_state)(void);
 *  This function pointer shows whether HDMI HPD low interrrupt was
 * handled on HDMI driver or not. It must be assigned in HDMI driver's probe
 * function.
 */
int (*hpd_intr_state)(void);
/*
 *  Sched_hpd_handler_false means that mhl_hpd_handler(false) must be called.
 *  It is set on sii9234_mhl_hpd_handler_false(). It is read and cleared on
 * call_sched_mhl_hpd_handler().
 */
atomic_t sched_hpd_handler_false;
/*
 *  void call_sched_mhl_hpd_handler(void);
 *  This function must be called by HDMI HPD LOW IRQ handler.
 *  If the sched_hpd_handler_false is 1, then it clears the
 * sched_hpd_handler_false to 0 and call mhl_hpd_handler(false).
 */
void call_sched_mhl_hpd_handler(void)
{
	if (atomic_read(&sched_hpd_handler_false) == 1) {
		atomic_set(&sched_hpd_handler_false, 0);
		mhl_hpd_handler(false);
		pr_info("%s : called mhl_hpd_handler(false)\n", __func__);
	}
}
/*
 *  void sii9234_mhl_hpd_handler_false(void);
 *  This function checks whether HDMI HPD LOW IRQ handler was handled
 * by hpd_intr_state().
 *  If IRQ handler was handled, mhl_hpd_handler(false) is called directly,
 * if not, mhl_hpd_handler(false) is scheduled by setting the
 * sched_hpd_handler_false to 1.
 */
void sii9234_mhl_hpd_handler_false(void)
{
	if (hpd_intr_state != NULL) {
		if (hpd_intr_state() == LOW) {
			mhl_hpd_handler(false);
		} else {
			atomic_set(&sched_hpd_handler_false, 1);
			pr_info("%s : mhl_hpd_handler(false)"
					" is scheduled\n", __func__);
		}
	} else {
		mhl_hpd_handler(false);
	}
}
#endif

u8 mhl_onoff_ex(bool onoff)
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);
	int ret;

	pr_info("sii9234: %s(%s)\n", __func__, onoff ? "on" : "off");

	if (!sii9234 || !sii9234->pdata) {
		pr_info("sii9234: mhl_onoff_ex: getting resource is failed\n");
		return 2;
	}

	if (sii9234->pdata->power_state == onoff) {
		pr_info("sii9234: mhl_onoff_ex: mhl already %s\n",
			onoff ? "on" : "off");
		return 2;
	}

	sii9234->pdata->power_state = onoff;	/*save power state */

	if (sii9234->pdata->mhl_sel)
		sii9234->pdata->mhl_sel(onoff);

	if (onoff) {
		if (sii9234->pdata->hw_onoff)
			sii9234->pdata->hw_onoff(1);

		if (sii9234->pdata->hw_reset)
			sii9234->pdata->hw_reset();

		goto_d3();
		return 2;
	} else {
		sii9234_cancel_callback();

		if (sii9234->pdata->hw_onoff)
			sii9234->pdata->hw_onoff(0);

#if defined(CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE) &&\
	!defined(CONFIG_SAMSUNG_MHL_9290)
		sii9234_mhl_hpd_handler_false();
#endif

#ifdef CONFIG_SAMSUNG_USE_11PIN_CONNECTOR
#if !defined(CONFIG_MACH_P4NOTE)
		ret = is_mhl_cable_connected();
#endif
		if (ret == 1) {
			pr_info("sii9234: %s() mhl still inserted, "
				 "retry discovery\n", __func__);
			schedule_work(&sii9234->mhl_restart_work);
		} else if (ret == 0) {
			pr_info("sii9234: %s() mhl cable is removed\n",
				__func__);
		} else {
			pr_err
			    ("[ERROR] %s() is_mhl_cable_connected error : %d\n",
			     __func__, ret);
		}
#endif
	}
	return sii9234->rgnd;
}
EXPORT_SYMBOL(mhl_onoff_ex);

static int mhl_tx_write_reg(struct sii9234_data *sii9234, unsigned int offset,
			    u8 value)
{
	int ret;
	ret = i2c_smbus_write_byte_data(sii9234->pdata->mhl_tx_client, offset,
					value);
	if (ret < 0)
		pr_err("[ERROR] sii9234 : %s(0x%02x, 0x%02x)\n", __func__,
		       offset, value);
	return ret;
}

static int mhl_tx_read_reg(struct sii9234_data *sii9234, unsigned int offset,
			   u8 *value)
{
	int ret;

	if (!value)
		return -EINVAL;

	ret = i2c_smbus_write_byte(sii9234->pdata->mhl_tx_client, offset);
	if (ret < 0) {
		pr_err("[ERROR] sii9234 : %s(0x%02x)\n", __func__, offset);
		return ret;
	}

	ret = i2c_smbus_read_byte(sii9234->pdata->mhl_tx_client);
	if (ret < 0) {
		pr_err("[ERROR] sii9234 : %s(0x%02x)\n", __func__, offset);
		return ret;
	}

	*value = ret & 0x000000FF;

	return 0;
}

static int mhl_tx_set_reg(struct sii9234_data *sii9234, unsigned int offset,
			  u8 mask)
{
	int ret;
	u8 value;

	ret = mhl_tx_read_reg(sii9234, offset, &value);
	if (ret < 0) {
		pr_err("[ERROR] sii9234 : %s(0x%02x, 0x%02x)\n", __func__,
		       offset, mask);
		return ret;
	}

	value |= mask;

	return mhl_tx_write_reg(sii9234, offset, value);
}

static int mhl_tx_clear_reg(struct sii9234_data *sii9234, unsigned int offset,
			    u8 mask)
{
	int ret;
	u8 value;

	ret = mhl_tx_read_reg(sii9234, offset, &value);
	if (ret < 0) {
		pr_err("[ERROR] sii9234 : %s(0x%02x, 0x%02x)\n", __func__,
		       offset, mask);
		return ret;
	}

	value &= ~mask;

	ret = mhl_tx_write_reg(sii9234, offset, value);
	if (ret < 0)
		pr_err("[ERROR] sii9234 : %s(0x%02x, 0x%02x)\n", __func__,
		       offset, mask);
	return ret;
}

static int tpi_write_reg(struct sii9234_data *sii9234, unsigned int offset,
			 u8 value)
{
	int ret = 0;
	ret = i2c_smbus_write_byte_data(sii9234->pdata->tpi_client, offset,
					value);
	if (ret < 0)
		pr_err("[ERROR] sii9234 : %s(0x%02x, 0x%02x)\n", __func__,
		       offset, value);
	return ret;
}

static int tpi_read_reg(struct sii9234_data *sii9234, unsigned int offset,
			u8 *value)
{
	int ret;

	if (!value)
		return -EINVAL;

	ret = i2c_smbus_write_byte(sii9234->pdata->tpi_client, offset);
	if (ret < 0) {
		pr_err("[ERROR] sii9234 : %s(0x%02x)\n", __func__, offset);
		return ret;
	}

	ret = i2c_smbus_read_byte(sii9234->pdata->tpi_client);
	if (ret < 0) {
		pr_err("[ERROR] sii9234 : %s(0x%02x)\n", __func__, offset);
		return ret;
	}

	*value = ret & 0x000000FF;

	return 0;
}

static int hdmi_rx_read_reg(struct sii9234_data *sii9234, unsigned int offset,
			    u8 *value)
{
	int ret;

	if (!value)
		return -EINVAL;

	ret = i2c_smbus_write_byte(sii9234->pdata->hdmi_rx_client, offset);
	if (ret < 0) {
		pr_err("[ERROR] sii9234 : %s(0x%02x)\n", __func__, offset);
		return ret;
	}

	ret = i2c_smbus_read_byte(sii9234->pdata->hdmi_rx_client);
	if (ret < 0) {
		pr_err("[ERROR] sii9234 : %s(0x%02x)\n", __func__, offset);
		return ret;
	}

	*value = ret & 0x000000FF;

	return 0;
}

static int hdmi_rx_write_reg(struct sii9234_data *sii9234, unsigned int offset,
			     u8 value)
{
	int ret;
	ret = i2c_smbus_write_byte_data(sii9234->pdata->hdmi_rx_client, offset,
					value);
	if (ret < 0)
		pr_err("[ERROR] sii9234 : %s(0x%02x, 0x%02x)\n", __func__,
		       offset, value);
	return ret;
}

static int cbus_write_reg(struct sii9234_data *sii9234, unsigned int offset,
			  u8 value)
{
	return i2c_smbus_write_byte_data(sii9234->pdata->cbus_client, offset,
					 value);
}

static int cbus_read_reg(struct sii9234_data *sii9234, unsigned int offset,
			 u8 *value)
{
	int ret;

	if (!value)
		return -EINVAL;

	ret = i2c_smbus_write_byte(sii9234->pdata->cbus_client, offset);
	if (ret < 0) {
		pr_err("[ERROR] sii9234 : %s(0x%02x)\n", __func__, offset);
		return ret;
	}

	ret = i2c_smbus_read_byte(sii9234->pdata->cbus_client);
	if (ret < 0) {
		pr_err("[ERROR] sii9234 : %s(0x%02x)\n", __func__, offset);
		return ret;
	}

	*value = ret & 0x000000FF;

	return 0;
}

static int cbus_set_reg(struct sii9234_data *sii9234, unsigned int offset,
			u8 mask)
{
	int ret;
	u8 value;

	ret = cbus_read_reg(sii9234, offset, &value);
	if (ret < 0) {
		pr_err("[ERROR] sii9234 : %s(0x%02x, 0x%02x)\n", __func__,
		       offset, mask);
		return ret;
	}

	value |= mask;

	return cbus_write_reg(sii9234, offset, value);
}

#ifdef __CONFIG_TMDS_OFFON_WORKAROUND__
void sii9234_tmds_offon_work(struct work_struct *work)
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);

	pr_debug("%s()\n", __func__);
	sii9234_tmds_control2(sii9234, false);
	sii9234_tmds_control2(sii9234, true);
}
#endif

static int mhl_wake_toggle(struct sii9234_data *sii9234,
			   unsigned long high_period, unsigned long low_period)
{
	int ret;

	/* These bits are not documented. */
	ret =
	    mhl_tx_set_reg(sii9234, MHL_TX_DISC_CTRL7_REG, (1 << 7) | (1 << 6));
	if (ret < 0)
		return ret;

	usleep_range(high_period * USEC_PER_MSEC, high_period * USEC_PER_MSEC);

	ret =
	    mhl_tx_clear_reg(sii9234, MHL_TX_DISC_CTRL7_REG,
			     (1 << 7) | (1 << 6));
	if (ret < 0)
		return ret;

	usleep_range(low_period * USEC_PER_MSEC, low_period * USEC_PER_MSEC);

	return 0;
}

static int mhl_send_wake_pulses(struct sii9234_data *sii9234)
{
	int ret;

	ret = mhl_wake_toggle(sii9234, T_SRC_WAKE_PULSE_WIDTH_1,
			      T_SRC_WAKE_PULSE_WIDTH_1);
	if (ret < 0)
		return ret;

	ret = mhl_wake_toggle(sii9234, T_SRC_WAKE_PULSE_WIDTH_1,
			      T_SRC_WAKE_PULSE_WIDTH_2);
	if (ret < 0)
		return ret;

	ret = mhl_wake_toggle(sii9234, T_SRC_WAKE_PULSE_WIDTH_1,
			      T_SRC_WAKE_PULSE_WIDTH_1);
	if (ret < 0)
		return ret;

	ret = mhl_wake_toggle(sii9234, T_SRC_WAKE_PULSE_WIDTH_1,
			      T_SRC_WAKE_TO_DISCOVER);
	if (ret < 0)
		return ret;

	return 0;
}

static int sii9234_cbus_reset(struct sii9234_data *sii9234)
{
	int ret;
	u8 idx;
	/* Reset CBUS */
	ret = mhl_tx_set_reg(sii9234, MHL_TX_SRST, (1 << 3));
	if (ret < 0)
		return ret;

	msleep(T_SRC_CBUS_DEGLITCH);

	ret = mhl_tx_clear_reg(sii9234, MHL_TX_SRST, (1 << 3));
	if (ret < 0)
		return ret;

	for (idx = 0; idx < 4; idx++) {
		/* Enable WRITE_STAT interrupt for writes to all
		   4 MSC Status registers. */
		ret = cbus_write_reg(sii9234, 0xE0 + idx, 0xF2);
		if (ret < 0)
			return ret;

		/*Enable SET_INT interrupt for writes to all
		   4 MSC Interrupt registers. */
		ret = cbus_write_reg(sii9234, 0xF0 + idx, 0xF2);
		if (ret < 0)
			return ret;
	}

	return 0;

}

/* require to chek mhl imformation of samsung in cbus_init_register*/
static int sii9234_cbus_init(struct sii9234_data *sii9234)
{
	u8 value;
	int ret;

	ret = cbus_write_reg(sii9234, 0x07, 0xF2);
	if (ret < 0)
		goto i2c_error_exit;

	ret = cbus_write_reg(sii9234, 0x40, 0x03);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x42, 0x06);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x36, 0x0C);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x3D, 0xFD);
	if (ret < 0)
		goto i2c_error_exit;

	ret = cbus_write_reg(sii9234, 0x1C, 0x01);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x1D, 0x0F);
	if (ret < 0)
		goto i2c_error_exit;

	ret = cbus_write_reg(sii9234, 0x44, 0x02);
	if (ret < 0)
		goto i2c_error_exit;

	/* Setup our devcap */
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_DEV_STATE, 0x00);
				/*To meet cts 6.3.10.1 spec */
	if (ret < 0)
		goto i2c_error_exit;
#if defined(__CONFIG_MHL_VER_1_1__)
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_MHL_VERSION, 0x11);
				/*mhl version 1.1 */
#elif defined(__CONFIG_MHL_VER_1_2__)
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_MHL_VERSION, 0x12);
				/*mhl version 1.2 */
#endif
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_DEV_CAT, 0x02);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_ADOPTER_ID_H, 0x01);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_ADOPTER_ID_L, 0x41);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_VID_LINK_MODE, 0x03);
				/* YCbCr444, RGB444 */
	if (ret < 0)
		goto i2c_error_exit;
#ifdef CONFIG_VIDEO_TVOUT_5_1CH_AUDIO
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_AUD_LINK_MODE, 0x03);
				/* 8ch, 2ch */
#else
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_AUD_LINK_MODE, 0x01);
				/* 2ch */
#endif
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_VIDEO_TYPE, 0);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_LOG_DEV_MAP, (1 << 7));
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_BANDWIDTH, 0x0F);
	if (ret < 0)
		goto i2c_error_exit;
	ret =
	    cbus_write_reg(sii9234, 0x80 + DEVCAP_DEV_FEATURE_FLAG,
			   (1 << 0) | (1 << 1) | (1 << 2));
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_DEVICE_ID_H, 0x0);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_DEVICE_ID_L, 0x0);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_SCRATCHPAD_SIZE, 0x10);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_INT_STAT_SIZE, 0x33);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x80 + DEVCAP_RESERVED, 0);
	if (ret < 0)
		goto i2c_error_exit;

	ret = cbus_read_reg(sii9234, 0x31, &value);
	if (ret < 0)
		goto i2c_error_exit;
	value |= 0x0C;
	ret = cbus_write_reg(sii9234, 0x31, value);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, 0x30, 0x01);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_read_reg(sii9234, 0x3C, &value);
	if (ret < 0)
		goto i2c_error_exit;
	value &= ~0x38;
	value |= 0x30;
	ret = cbus_write_reg(sii9234, 0x3C, value);
	if (ret < 0)
		goto i2c_error_exit;

	ret = cbus_read_reg(sii9234, 0x22, &value);
	if (ret < 0)
		goto i2c_error_exit;
	value &= ~0x0F;
	value |= 0x0D;
	ret = cbus_write_reg(sii9234, 0x22, value);
	if (ret < 0)
		goto i2c_error_exit;

	ret = cbus_read_reg(sii9234, 0x2E, &value);
	if (ret < 0)
		goto i2c_error_exit;
	value |= 0x15;
	ret = cbus_write_reg(sii9234, 0x2E, value);
	if (ret < 0)
		goto i2c_error_exit;

	ret = cbus_write_reg(sii9234, CBUS_INTR1_ENABLE_REG, 0);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, CBUS_INTR2_ENABLE_REG, 0);
	if (ret < 0)
		goto i2c_error_exit;

	return 0;
 i2c_error_exit:
	pr_err("[ERROR] %s()\n", __func__);
	return ret;
}

static void cbus_req_abort_error(struct sii9234_data *sii9234)
{
	u8 abort_reason = 0;

	pr_debug("sii9234: MSC Request Aborted:");

	cbus_read_reg(sii9234, MSC_REQ_ABORT_REASON_REG, &abort_reason);

	if (abort_reason) {
		if (abort_reason & BIT_MSC_XFR_ABORT) {
			cbus_read_reg(sii9234, MSC_REQ_ABORT_REASON_REG,
				      &abort_reason);
			pr_cont("ABORT_REASON_REG = %d\n", abort_reason);
			cbus_write_reg(sii9234, MSC_REQ_ABORT_REASON_REG, 0xff);
		}
		if (abort_reason & BIT_MSC_ABORT) {
			cbus_read_reg(sii9234, BIT_MSC_ABORT, &abort_reason);
			pr_cont("BIT_MSC_ABORT = %d\n", abort_reason);
			cbus_write_reg(sii9234, BIT_MSC_ABORT, 0xff);
		}
		if (abort_reason & ABORT_BY_PEER)
			pr_cont(" Peer Sent an ABORT:");
		if (abort_reason & UNDEF_CMD)
			pr_cont(" Undefined Opcode:");
		if (abort_reason & TIMEOUT)
			pr_cont(" Requestor Translation layer Timeout:");
		if (abort_reason & PROTO_ERROR)
			pr_cont(" Protocol Error:");
		if (abort_reason & MAX_FAIL) {
			u8 msc_retry_thr_val = 0;
			pr_cont(" Retry Threshold exceeded:");
			cbus_read_reg(sii9234,
				      MSC_RETRY_FAIL_LIM_REG,
				      &msc_retry_thr_val);
			pr_cont("Retry Threshold value is:%d",
				msc_retry_thr_val);
		}
	}
	pr_cont("\n");
}

static void cbus_resp_abort_error(struct sii9234_data *sii9234)
{
	u8 abort_reason = 0;

	pr_debug("sii9234: MSC Response Aborted:");
	cbus_read_reg(sii9234, MSC_RESP_ABORT_REASON_REG, &abort_reason);
	cbus_write_reg(sii9234, MSC_RESP_ABORT_REASON_REG, 0xff);
	if (abort_reason) {
		if (abort_reason & ABORT_BY_PEER)
			pr_cont(" Peer Sent an ABORT");
		if (abort_reason & UNDEF_CMD)
			pr_cont(" Undefined Opcode");
		if (abort_reason & TIMEOUT)
			pr_cont(" Requestor Translation layer Timeout");
	}
	pr_cont("\n");
}

static void force_usb_id_switch_open(struct sii9234_data *sii9234)
{
	/*Disable CBUS discovery */
	mhl_tx_clear_reg(sii9234, MHL_TX_DISC_CTRL1_REG, (1 << 0));
	/*Force USB ID switch to open */
	mhl_tx_set_reg(sii9234, MHL_TX_DISC_CTRL6_REG, USB_ID_OVR);

	mhl_tx_set_reg(sii9234, MHL_TX_DISC_CTRL3_REG, 0x86);
	/*Force upstream HPD to 0 when not in MHL mode. */
	mhl_tx_clear_reg(sii9234, MHL_TX_INT_CTRL_REG, (1 << 4) | (1 << 5));
}

static void release_usb_id_switch_open(struct sii9234_data *sii9234)
{
	msleep(T_SRC_CBUS_FLOAT);
	/* clear USB ID switch to open */
	mhl_tx_clear_reg(sii9234, MHL_TX_DISC_CTRL6_REG, USB_ID_OVR);
	/* Enable CBUS discovery */
	mhl_tx_set_reg(sii9234, MHL_TX_DISC_CTRL1_REG, (1 << 0));
}

static bool cbus_ddc_abort_error(struct sii9234_data *sii9234)
{
	u8 val1, val2;

	/* clear the ddc abort counter */
	cbus_write_reg(sii9234, 0x29, 0xFF);
	cbus_read_reg(sii9234, 0x29, &val1);
	usleep_range(3000, 4000);
	cbus_read_reg(sii9234, 0x29, &val2);
	if (val2 > (val1 + 50)) {
		pr_debug("Applying DDC Abort Safety(SWA 18958)\n)");
		mhl_tx_set_reg(sii9234, MHL_TX_SRST, (1 << 3));
		mhl_tx_clear_reg(sii9234, MHL_TX_SRST, (1 << 3));
		force_usb_id_switch_open(sii9234);
		release_usb_id_switch_open(sii9234);
		mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL1_REG, 0xD0);
		sii9234_tmds_control(sii9234, false);
		/* Disconnect and notify to OTG */
		return true;
	}
	pr_debug("sii9234: DDC abort interrupt\n");

	return false;
}

#ifdef CONFIG_SII9234_RCP
static void rcp_uevent_report(struct sii9234_data *sii9234, u8 key)
{
	if (!sii9234->input_dev) {
		pr_err("%s: sii9234->input_dev is NULL & "
		       "skip rcp_report\n", __func__);
		return;
	}

	pr_info("sii9234: rcp_uevent_report key: %d\n", key);
	input_report_key(sii9234->input_dev, (unsigned int)key + 1, 1);
	input_report_key(sii9234->input_dev, (unsigned int)key + 1, 0);
	input_sync(sii9234->input_dev);
}

/*
 * is_rcp_code_valid: Validdates the recevied RCP key,
 * valid key is 1 to 1  map to fwk keylayer file sii9234_rcp.kl
 * located at (/system/usr/keylayout/sii9234_rcp.kl).
 *
 * New key support needs to be update is_rcp_key_code_valid at
 * driver side and /system/usr/keylayout/sii9234_rcp.kl at fwk side.
 */

static int is_rcp_key_code_valid(u8 key)
{
	switch (key + 1) {
		/*should resemble /system/usr/keylayout/sii9234_rcp.kl */
	case 1:		/* ENTER                WAKE_DROPPED */
	case 2:		/* DPAD_UP              WAKE_DROPPED */
	case 3:		/* DPAD_DOWN            WAKE_DROPPED */
	case 4:		/* DPAD_LEFT            WAKE_DROPPED */
	case 5:		/* DPAD_RIGHT           WAKE_DROPPED */
	case 10:		/* MENU                 WAKE_DROPPED */
	case 14:		/* BACK                 WAKE_DROPPED */
	case 33:		/* 0    */
	case 34:		/* 1    */
	case 35:		/* 2    */
	case 36:		/* 3    */
	case 37:		/* 4    */
	case 38:		/* 5    */
	case 39:		/* 6    */
	case 40:		/* 7    */
	case 41:		/* 8    */
	case 42:		/* 9    */
	case 43:		/* ENTER */
	case 45:		/* DEL  */
	case 69:		/* MEDIA_PLAY_PAUSE         WAKE */
	case 70:		/* MEDIA_STOP               WAKE */
	case 71:		/* MEDIA_PAUSE              WAKE */
	case 73:		/* MEDIA_REWIND             WAKE */
	case 74:		/* MEDIA_FAST_FORWARD       WAKE */
	case 76:		/* MEDIA_NEXT               WAKE */
	case 77:		/* MEDIA_PREVIOUS           WAKE */
		return 1;
	default:
		return 0;
	}

}

static void cbus_process_rcp_key(struct sii9234_data *sii9234, u8 key)
{

	if (is_rcp_key_code_valid(key)) {
		/* Report the key */
		rcp_uevent_report(sii9234, key);
		/* Send the RCP ack */
#ifndef __MHL_NEW_CBUS_MSC_CMD__
		cbus_command_request(sii9234, CBUS_MSC_MSG, MSG_RCPK, key);
#else
		sii9234_enqueue_msc_work(sii9234, CBUS_MSC_MSG,
					 MSG_RCPK, key, 0x0);
#endif
	} else {
		sii9234->error_key = key;
		/*
		 * Send a RCPE(RCP Error Message) to Peer followed by
		 * RCPK with old key-code so that initiator(TV) can
		 * recognize failed key code.error code = 0x01 means
		 * Ineffective key code was received.
		 * See Table 21.(PRM)for details.
		 */
#ifndef __MHL_NEW_CBUS_MSC_CMD__
		cbus_command_request(sii9234, CBUS_MSC_MSG, MSG_RCPE, 0x01);
#else
		sii9234_enqueue_msc_work(sii9234, CBUS_MSC_MSG, MSG_RCPE, 0x01,
					 0x0);
#endif
	}
}
#endif

static void cbus_process_rap_key(struct sii9234_data *sii9234, u8 key)
{
	if (CBUS_MSC_RAP_CONTENT_ON == key)
		sii9234_tmds_control(sii9234, true);
	else if (CBUS_MSC_RAP_CONTENT_OFF == key)
		sii9234_tmds_control(sii9234, false);

#ifndef __MHL_NEW_CBUS_MSC_CMD__
	cbus_command_request(sii9234, CBUS_MSC_MSG, MSG_RAPK, 0x00);
#else
	sii9234_enqueue_msc_work(sii9234, CBUS_MSC_MSG, MSG_RAPK, 0x00, 0x0);
#endif
}

/*
 * Incoming MSC_MSG : RCP/RAP/RCPK/RCPE/RAPK commands
 *
 * Process RCP key codes and the send supported keys to userspace.
 * If a key is not supported then an error ack is sent to the peer.  Note
 * that by default all key codes are supported.
 *
 * An alternate method might be to decide the validity of the key in the
 * driver itself.  However, the driver does not have any criteria to which
 * to make this decision.
 */
static void cbus_handle_msc_msg(struct sii9234_data *sii9234)
{
	u8 cmd_code, key;

	sii9234_cbus_mutex_lock(&sii9234->cbus_lock);
	if (sii9234->state != STATE_ESTABLISHED) {
		pr_debug("sii9234: invalid MHL state\n");
		sii9234_cbus_mutex_unlock(&sii9234->cbus_lock);
		return;
	}

	cbus_read_reg(sii9234, CBUS_MSC_MSG_CMD_IN_REG, &cmd_code);
	cbus_read_reg(sii9234, CBUS_MSC_MSG_DATA_IN_REG, &key);

	pr_info("sii9234: cmd_code:%d, key:%d\n", cmd_code, key);

	switch (cmd_code) {
	case MSG_RCP:
		pr_debug("sii9234: RCP Arrived. KEY CODE:%d\n", key);
		sii9234_cbus_mutex_unlock(&sii9234->cbus_lock);
		cbus_process_rcp_key(sii9234, key);
		return;
	case MSG_RAP:
		pr_debug("sii9234: RAP Arrived\n");
		sii9234_cbus_mutex_unlock(&sii9234->cbus_lock);
		cbus_process_rap_key(sii9234, key);
		return;
	case MSG_RCPK:
		pr_debug("sii9234: RCPK Arrived\n");
		break;
	case MSG_RCPE:
		pr_debug("sii9234: RCPE Arrived\n");
		break;
	case MSG_RAPK:
		pr_debug("sii9234: RAPK Arrived\n");
		break;
	default:
		pr_debug("sii9234: MAC error\n");
		sii9234_cbus_mutex_unlock(&sii9234->cbus_lock);
#ifndef __MHL_NEW_CBUS_MSC_CMD__
		cbus_command_request(sii9234, CBUS_GET_MSC_ERR_CODE, 0, 0);
#else
		sii9234_enqueue_msc_work(sii9234, CBUS_GET_MSC_ERR_CODE, 0, 0,
					 0x0);
#endif
		return;
	}
	sii9234_cbus_mutex_unlock(&sii9234->cbus_lock);
}

void mhl_path_enable(struct sii9234_data *sii9234, bool path_en)
{
	pr_debug("sii9234: mhl_path_enable MHL_STATUS_PATH_ENABLED,"
		 " path_en=%d !!!\n", path_en);

	if (path_en)
		sii9234->mhl_status_value.linkmode |= MHL_STATUS_PATH_ENABLED;
	else
		sii9234->mhl_status_value.linkmode &= ~MHL_STATUS_PATH_ENABLED;
#ifndef __MHL_NEW_CBUS_MSC_CMD__
	cbus_command_request(sii9234, CBUS_WRITE_STAT, CBUS_LINK_CONTROL_2_REG,
			     sii9234->mhl_status_value.linkmode);
#else
	sii9234_enqueue_msc_work(sii9234, CBUS_WRITE_STAT,
				 CBUS_LINK_CONTROL_2_REG,
				 sii9234->mhl_status_value.linkmode, 0x0);
#endif
}

static void cbus_handle_wrt_burst_recd(struct sii9234_data *sii9234)
{
	pr_debug("sii9234: CBUS WRT_BURST_RECD\n");
}

static void cbus_handle_wrt_stat_recd(struct sii9234_data *sii9234)
{
	u8 status_reg0, status_reg1, value;

	pr_debug("sii9234: CBUS WRT_STAT_RECD\n");

	/*
	 * The two MHL status registers need to read to ensure that the MSC is
	 * ready to receive the READ_DEVCAP command.
	 * The READ_DEVCAP command is need to determine the dongle power state
	 * and whether RCP, RCPE, RCPK, RAP, and RAPE are supported.
	 *
	 * Note that this is not documented properly in the PRM.
	 */

	cbus_read_reg(sii9234, CBUS_MHL_STATUS_REG_0, &status_reg0);
	cbus_write_reg(sii9234, CBUS_MHL_STATUS_REG_0, 0xFF);
	cbus_read_reg(sii9234, CBUS_MHL_STATUS_REG_1, &status_reg1);
	cbus_write_reg(sii9234, CBUS_MHL_STATUS_REG_1, 0xFF);

	pr_debug("sii9234: STATUS_REG0 : [%d];STATUS_REG1 : [%d]\n",
		 status_reg0, status_reg1);

	/* clear WRT_STAT_RECD intr */
	cbus_read_reg(sii9234, CBUS_MHL_STATUS_REG_0, &value);
	cbus_write_reg(sii9234, CBUS_MHL_STATUS_REG_0, value);

	cbus_read_reg(sii9234, CBUS_MHL_STATUS_REG_1, &value);
	cbus_write_reg(sii9234, CBUS_MHL_STATUS_REG_1, value);

	cbus_read_reg(sii9234, CBUS_MHL_STATUS_REG_2, &value);
	cbus_write_reg(sii9234, CBUS_MHL_STATUS_REG_2, value);

	cbus_read_reg(sii9234, CBUS_MHL_STATUS_REG_3, &value);
	cbus_write_reg(sii9234, CBUS_MHL_STATUS_REG_3, value);

	if (!(sii9234->mhl_status_value.linkmode & MHL_STATUS_PATH_ENABLED) &&
	    (MHL_STATUS_PATH_ENABLED & status_reg1)) {
		mhl_path_enable(sii9234, true);
	} else if ((sii9234->mhl_status_value.linkmode
		    & MHL_STATUS_PATH_ENABLED) &&
		   !(MHL_STATUS_PATH_ENABLED & status_reg1)) {
		mhl_path_enable(sii9234, false);
	}

	if (status_reg0 & MHL_STATUS_DCAP_READY) {
		pr_debug("sii9234: DEV CAP READY\n");
#ifndef __MHL_NEW_CBUS_MSC_CMD__
		cbus_command_request(sii9234, CBUS_READ_DEVCAP,
				     DEVCAP_DEV_CAT, 0x00);
		cbus_command_request(sii9234, CBUS_READ_DEVCAP,
				     DEVCAP_DEV_FEATURE_FLAG, 0x00);
#else
		sii9234_enqueue_msc_work(sii9234, CBUS_READ_DEVCAP,
					 DEVCAP_DEV_CAT, 0x00, 0x0);
		sii9234_enqueue_msc_work(sii9234, CBUS_READ_DEVCAP,
					 DEVCAP_DEV_FEATURE_FLAG, 0x00, 0x0);
		sii9234_enqueue_msc_work(sii9234, CBUS_READ_DEVCAP,
					 DEVCAP_DEVICE_ID_H, 0x0, 0x0);
		sii9234_enqueue_msc_work(sii9234, CBUS_READ_DEVCAP,
					 DEVCAP_DEVICE_ID_L, 0x0, 0x0);
		sii9234_enqueue_msc_work(sii9234, CBUS_READ_DEVCAP,
					 DEVCAP_RESERVED, 0x0, 0x0);
#endif
	}
}

static void cbus_handle_set_int_recd(struct sii9234_data *sii9234)
{
	u8 intr_reg0, intr_reg1, value;

	/* read and clear interrupt */
	cbus_read_reg(sii9234, CBUS_MHL_INTR_REG_0, &intr_reg0);
	cbus_write_reg(sii9234, CBUS_MHL_INTR_REG_0, intr_reg0);

	cbus_read_reg(sii9234, CBUS_MHL_INTR_REG_1, &intr_reg1);
	cbus_write_reg(sii9234, CBUS_MHL_INTR_REG_1, intr_reg1);

	pr_debug("sii9234: INTR_REG0 : [%d]; INTR_REG1 : [%d]\n",
		 intr_reg0, intr_reg1);

	if (intr_reg0 & MHL_INT_DCAP_CHG) {
#ifndef __MHL_NEW_CBUS_MSC_CMD__
		pr_debug("sii9234: MHL_INT_DCAP_CHG\n");
		cbus_command_request(sii9234, CBUS_READ_DEVCAP,
				     DEVCAP_DEV_CAT, 0x00);
		cbus_command_request(sii9234, CBUS_READ_DEVCAP,
				     DEVCAP_DEV_FEATURE_FLAG, 0x00);
#endif
	}

	if (intr_reg0 & MHL_INT_DSCR_CHG)
		pr_debug("sii9234:  MHL_INT_DSCR_CHG\n");

	if (intr_reg0 & MHL_INT_REQ_WRT) {
		pr_debug("sii9234:  MHL_INT_REQ_WRT\n");
#ifndef __MHL_NEW_CBUS_MSC_CMD__
		cbus_command_request(sii9234, CBUS_SET_INT,
				     MHL_RCHANGE_INT, MHL_INT_GRT_WRT);
#else
		sii9234_enqueue_msc_work(sii9234, CBUS_SET_INT,
					 MHL_RCHANGE_INT, MHL_INT_GRT_WRT, 0x0);
#endif
	}

	if (intr_reg0 & MHL_INT_GRT_WRT)
		pr_debug("sii9234:  MHL_INT_GRT_WRT\n");

	if (intr_reg1 & MHL_INT_EDID_CHG) {
		pr_debug("sii9234:  MHL_INT_EDID_CHG\n");
		/* Enable Overriding HPD OUT */
		mhl_tx_set_reg(sii9234, MHL_TX_INT_CTRL_REG, (1 << 4));

		/*
		 * As per HDMI specification to indicate EDID change
		 * in TV (or sink), we need to toggle HPD line.
		 */

		/* HPD OUT = Low */
		mhl_tx_clear_reg(sii9234, MHL_TX_INT_CTRL_REG, (1 << 5));

		/* A SET_HPD command shall not follow a CLR_HPD command
		 * within less than THPD_WIDTH(50ms).
		 */
		msleep(T_HPD_WIDTH);

		/* HPD OUT = High */
		mhl_tx_set_reg(sii9234, MHL_TX_INT_CTRL_REG, (1 << 5));

		/* Disable Overriding of HPD OUT */
		mhl_tx_clear_reg(sii9234, MHL_TX_INT_CTRL_REG, (1 << 4));
	}

	/* clear SET_INT_RECD interrupt */
	cbus_read_reg(sii9234, CBUS_MHL_INTR_REG_2, &value);
	cbus_write_reg(sii9234, CBUS_MHL_INTR_REG_2, value);

	cbus_read_reg(sii9234, CBUS_MHL_INTR_REG_3, &value);
	cbus_write_reg(sii9234, CBUS_MHL_INTR_REG_3, value);
}

static int sii9234_power_init(struct sii9234_data *sii9234)
{
	int ret;

	/* Force the SiI9234 into the D0 state. */
	ret = tpi_write_reg(sii9234, TPI_DPD_REG, 0x3F);
	if (ret < 0)
		return ret;

	/* Enable TxPLL Clock */
	ret = hdmi_rx_write_reg(sii9234, HDMI_RX_TMDS_CLK_EN_REG, 0x01);
	if (ret < 0)
		return ret;

	/* Enable Tx Clock Path & Equalizer */
	ret = hdmi_rx_write_reg(sii9234, HDMI_RX_TMDS_CH_EN_REG, 0x15);
	if (ret < 0)
		return ret;

	/* Power Up TMDS */
	ret = mhl_tx_write_reg(sii9234, 0x08, 0x35);
	if (ret < 0)
		return ret;

	return ret;
}

static int sii9234_hdmi_init(struct sii9234_data *sii9234)
{
	int ret = 0;
	/* Analog PLL Control
	 * bits 5:4 = 2b00 as per characterization team.
	 */
	ret = hdmi_rx_write_reg(sii9234, HDMI_RX_TMDS0_CCTRL1_REG, 0xC1);
	if (ret < 0)
		goto i2c_error_exit;

	/* PLL Calrefsel */
	ret = hdmi_rx_write_reg(sii9234, HDMI_RX_PLL_CALREFSEL_REG, 0x03);
	if (ret < 0)
		goto i2c_error_exit;

	/* VCO Cal */
	ret = hdmi_rx_write_reg(sii9234, HDMI_RX_PLL_VCOCAL_REG, 0x20);
	if (ret < 0)
		goto i2c_error_exit;

	/* Auto EQ */
	ret = hdmi_rx_write_reg(sii9234, HDMI_RX_EQ_DATA0_REG, 0x8A);
	if (ret < 0)
		goto i2c_error_exit;

	/* Auto EQ */
	ret = hdmi_rx_write_reg(sii9234, HDMI_RX_EQ_DATA1_REG, 0x6A);
	if (ret < 0)
		goto i2c_error_exit;

	/* Auto EQ */
	ret = hdmi_rx_write_reg(sii9234, HDMI_RX_EQ_DATA2_REG, 0xAA);
	if (ret < 0)
		goto i2c_error_exit;

	/* Auto EQ */
	ret = hdmi_rx_write_reg(sii9234, HDMI_RX_EQ_DATA3_REG, 0xCA);
	if (ret < 0)
		goto i2c_error_exit;

	/* Auto EQ */
	ret = hdmi_rx_write_reg(sii9234, HDMI_RX_EQ_DATA4_REG, 0xEA);
	if (ret < 0)
		goto i2c_error_exit;

	/* Manual zone */
	ret = hdmi_rx_write_reg(sii9234, HDMI_RX_TMDS_ZONE_CTRL_REG, 0xA0);
	if (ret < 0)
		goto i2c_error_exit;

	/* PLL Mode Value */
	ret = hdmi_rx_write_reg(sii9234, HDMI_RX_TMDS_MODE_CTRL_REG, 0x00);
	if (ret < 0)
		goto i2c_error_exit;

	ret = mhl_tx_write_reg(sii9234, MHL_TX_TMDS_CCTRL, 0x34);
	if (ret < 0)
		goto i2c_error_exit;

	ret = hdmi_rx_write_reg(sii9234, 0x45, 0x44);
	if (ret < 0)
		goto i2c_error_exit;

	/* Rx PLL BW ~ 4MHz */
	ret = hdmi_rx_write_reg(sii9234, 0x31, 0x0A);
	if (ret < 0)
		goto i2c_error_exit;

	/* Analog PLL Control
	 * bits 5:4 = 2b00 as per characterization team.
	 */
	ret = hdmi_rx_write_reg(sii9234, HDMI_RX_TMDS0_CCTRL1_REG, 0xC1);
	if (ret < 0)
		goto i2c_error_exit;

	return ret;

 i2c_error_exit:
	pr_err("[ERROR] %s()\n", __func__);
	return ret;
}

static int sii9234_mhl_tx_ctl_int(struct sii9234_data *sii9234)
{
	int ret = 0;

	ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL1_REG, 0xD0);
	if (ret < 0)
		goto i2c_error_exit;
#ifdef	__CONFIG_RSEN_LOST_PATCH__
	ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL2_REG, 0xC0);
	if (ret < 0)
		goto i2c_error_exit;
#else
	ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL2_REG, 0xFC);
	if (ret < 0)
		goto i2c_error_exit;
#endif
	ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL4_REG,
			       sii9234->pdata->swing_level);
	if (ret < 0)
		goto i2c_error_exit;
	ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL7_REG, 0x0C);
	if (ret < 0)
		goto i2c_error_exit;

	return ret;

 i2c_error_exit:
	pr_err("[ERROR] %s()\n", __func__);
	return ret;
}

static void sii9234_power_down(struct sii9234_data *sii9234)
{
	sii9234_disable_irq();

#ifdef __MHL_NEW_CBUS_MSC_CMD__
	if (sii9234->claimed) {
		if (sii9234->pdata->vbus_present)
			sii9234->pdata->vbus_present(false,
						     sii9234->vbus_owner);
	}
#endif

	sii9234->state = STATE_DISCONNECTED;
	sii9234->claimed = false;

	tpi_write_reg(sii9234, TPI_DPD_REG, 0);
	/*turn on&off hpd festure for only QCT HDMI */
}

int rsen_state_timer_out(struct sii9234_data *sii9234)
{
	int ret = 0;
	u8 value;

	ret = mhl_tx_read_reg(sii9234, MHL_TX_SYSSTAT_REG, &value);
	if (ret < 0)
		goto err_exit;
	sii9234->rsen = value & RSEN_STATUS;

	if (value & RSEN_STATUS) {
		pr_info("sii9234: MHL cable connected.. RSEN High\n");
	} else {
		pr_info("sii9234: RSEN lost\n");
		msleep(T_SRC_RXSENSE_DEGLITCH);
		ret = mhl_tx_read_reg(sii9234, MHL_TX_SYSSTAT_REG, &value);
		if (ret < 0)
			goto err_exit;

		pr_info("sii9234: sys_stat: %x ~\n", value);
		if ((value & RSEN_STATUS) == 0) {
			pr_info("sii9234: RSEN Really LOW ~\n");
			/*To meet CTS 3.3.22.2 spec */
			sii9234_tmds_control(sii9234, false);
			force_usb_id_switch_open(sii9234);
			release_usb_id_switch_open(sii9234);
			ret = -1;
			goto err_exit;
		} else
			pr_info("sii9234: RSEN recovery\n");

	}
	return ret;

 err_exit:
	/*turn off mhl and change usb_sel to usb */
	pr_info("sii9234: %s() call mhl_onoff_ex(off)\n", __func__);
	schedule_work(&sii9234->mhl_end_work);
	return ret;
}

int sii9234_callback_sched;
static void goto_d3(void)
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);
	int ret;
	u8 value;

	pr_debug("sii9234: detection started d3\n");
	sii9234_callback_sched = 0;

	sii9234->mhl_status_value.linkmode = MHL_STATUS_CLK_MODE_NORMAL;
	sii9234->rgnd = RGND_UNKNOWN;

	sii9234->state = NO_MHL_STATUS;

	sii9234->rsen = false;

#if defined(CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE) &&\
	!defined(CONFIG_SAMSUNG_MHL_9290)
		sii9234_mhl_hpd_handler_false();
#endif
	memset(cbus_pkt_buf, 0x00, sizeof(cbus_pkt_buf));

	ret = sii9234_power_init(sii9234);
	if (ret < 0)
		goto exit;
	ret = sii9234_hdmi_init(sii9234);
	if (ret < 0)
		goto exit;
	ret = sii9234_mhl_tx_ctl_int(sii9234);
	if (ret < 0)
		goto exit;

	/* Enable HDCP Compliance safety */
	ret = mhl_tx_write_reg(sii9234, 0x2B, 0x01);
	if (ret < 0)
		goto exit;

	/* CBUS discovery cycle time for each drive and float = 150us */
	ret = mhl_tx_read_reg(sii9234, MHL_TX_DISC_CTRL1_REG, &value);
	if (ret < 0)
		goto exit;

	value &= ~(1 << 3);
	value |= (1 << 2);
	ret = mhl_tx_write_reg(sii9234, MHL_TX_DISC_CTRL1_REG, value);
	if (ret < 0)
		goto exit;

	/* Clear bit 6 (reg_skip_rgnd) */
	ret = mhl_tx_write_reg(sii9234, MHL_TX_DISC_CTRL2_REG,
			(1 << 7) /* Reserved Bit */ |
			2 << ATT_THRESH_SHIFT | DEGLITCH_TIME_50MS);
	if (ret < 0)
		goto exit;

	/* Changed from 66 to 65 for 94[1:0] = 01 = 5k reg_cbusmhl_pup_sel */
	/* 1.8V CBUS VTH & GND threshold */
	/*To meet CTS 3.3.7.2 spec */
	ret = mhl_tx_write_reg(sii9234, MHL_TX_DISC_CTRL5_REG, 0x77);
	if (ret < 0)
		goto exit;

	/* set bit 2 and 3, which is Initiator Timeout */
	ret = cbus_read_reg(sii9234, CBUS_LINK_CONTROL_2_REG, &value);
	if (ret < 0)
		goto exit;

	value |= 0x0C;

	ret = cbus_write_reg(sii9234, CBUS_LINK_CONTROL_2_REG, value);
	if (ret < 0)
		goto exit;

	ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL6_REG, 0xA0);
	if (ret < 0)
		goto exit;

	/* RGND & single discovery attempt (RGND blocking) */
	ret = mhl_tx_write_reg(sii9234, MHL_TX_DISC_CTRL6_REG, BLOCK_RGND_INT |
			       DVRFLT_SEL | SINGLE_ATT);
	if (ret < 0)
		goto exit;

	/* Use VBUS path of discovery state machine */
	ret = mhl_tx_write_reg(sii9234, MHL_TX_DISC_CTRL8_REG, 0);
	if (ret < 0)
		goto exit;
	ret = mhl_tx_set_reg(sii9234, MHL_TX_DISC_CTRL6_REG, USB_ID_OVR);
	if (ret < 0)
		goto exit;

	/* To allow RGND engine to operate correctly.
	 * When moving the chip from D2 to D0 (power up, init regs)
	 * the values should be
	 * 94[1:0] = 01  reg_cbusmhl_pup_sel[1:0] should be set for 5k
	 * 93[7:6] = 10  reg_cbusdisc_pup_sel[1:0] should be
	 * set for 10k (default)
	 * 93[5:4] = 00  reg_cbusidle_pup_sel[1:0] = open (default)
	 */
	ret = mhl_tx_set_reg(sii9234, MHL_TX_DISC_CTRL3_REG, 0x86);
	if (ret < 0)
		goto exit;
	/* change from CC to 8C to match 5K */
	/*To meet CTS 3.3.72 spec */
	ret = mhl_tx_set_reg(sii9234, MHL_TX_DISC_CTRL4_REG, 0x8C);
	if (ret < 0)
		goto exit;

	/* Configure the interrupt as active high */
	ret =
	    mhl_tx_clear_reg(sii9234, MHL_TX_INT_CTRL_REG, (1 << 2) | (1 << 1));
	if (ret < 0)
		goto exit;

	msleep(25);

	/* release usb_id switch */
	ret = mhl_tx_clear_reg(sii9234, MHL_TX_DISC_CTRL6_REG, USB_ID_OVR);
	if (ret < 0)
		goto exit;

	ret = mhl_tx_write_reg(sii9234, MHL_TX_DISC_CTRL1_REG, 0x27);
	if (ret < 0)
		goto exit;

	ret = sii9234_cbus_reset(sii9234);
	if (ret < 0)
		goto exit;
	ret = sii9234_cbus_init(sii9234);
	if (ret < 0)
		goto exit;

	/* Enable Auto soft reset on SCDT = 0 */
	ret = mhl_tx_write_reg(sii9234, 0x05, 0x04);
	if (ret < 0)
		goto exit;

	/* HDMI Transcode mode enable */
	ret = mhl_tx_write_reg(sii9234, 0x0D, 0x1C);
	if (ret < 0)
		goto exit;

	ret = mhl_tx_write_reg(sii9234, MHL_TX_INTR4_ENABLE_REG,
			       RGND_READY_MASK | CBUS_LKOUT_MASK |
			       MHL_DISC_FAIL_MASK | MHL_EST_MASK);
	if (ret < 0)
		goto exit;

	ret = mhl_tx_write_reg(sii9234, MHL_TX_INTR1_ENABLE_REG,
			       (1 << 5) | (1 << 6));
	if (ret < 0)
		goto exit;

	/* this point is very importand before megsure RGND impedance */
	force_usb_id_switch_open(sii9234);
	ret = mhl_tx_clear_reg(sii9234, MHL_TX_DISC_CTRL4_REG,
			       (1 << 7) | (1 << 6) | (1 << 5) | (1 << 4));
	if (ret < 0)
		goto exit;
	ret =
	    mhl_tx_clear_reg(sii9234, MHL_TX_DISC_CTRL5_REG,
			     (1 << 1) | (1 << 0));
	if (ret < 0)
		goto exit;
	release_usb_id_switch_open(sii9234);
	/*end of this */
	/* Force upstream HPD to 0 when not in MHL mode */
	ret = mhl_tx_clear_reg(sii9234, MHL_TX_INT_CTRL_REG, (1 << 5));
	if (ret < 0)
		goto exit;
	ret = mhl_tx_set_reg(sii9234, MHL_TX_INT_CTRL_REG, (1 << 4));
	if (ret < 0)
		goto exit;

	ret = hdmi_rx_write_reg(sii9234, 0x01, 0x03);
	if (ret < 0)
		goto exit;
	ret = tpi_read_reg(sii9234, 0x3D, &value);
	if (ret < 0)
		goto exit;
	value &= ~BIT0;
	ret = tpi_write_reg(sii9234, 0x3D, value);
	if (ret < 0)
		goto exit;
	pr_info("sii9234 : go_to d3 mode!!!\n");

	sii9234_enable_irq();

	return;
 exit:
	pr_err("[ERROR] %s() error terminated!\n", __func__);
	schedule_work(&sii9234->mhl_end_work);
	return;
}

#ifdef __MHL_NEW_CBUS_MSC_CMD__
void sii9234_process_msc_work(struct work_struct *work)
{
	u8 value;
	int ret;
	struct msc_packet *p_msc_pkt, *scratch;
	struct sii9234_data *sii9234 = container_of(work,
						    struct sii9234_data,
						    msc_work);

	sii9234_cbus_mutex_lock(&sii9234->cbus_lock);
	sii9234_mutex_lock(&sii9234->lock);

	pr_debug("%s() - start\n", __func__);

	list_for_each_entry_safe(p_msc_pkt, scratch,
				 &g_msc_packet_list, p_msc_packet_list) {

		pr_debug("[MSC] %s() command(0x%x), offset(0x%x), "
				"data_1(0x%x), data_2(0x%x)\n",
				__func__, p_msc_pkt->command, p_msc_pkt->offset,
				p_msc_pkt->data_1, p_msc_pkt->data_2);

		/* msc request */
		ret = sii9234_msc_req_locked(sii9234, p_msc_pkt);
		if (ret < 0) {
			pr_info("%s(): msc_req_locked error %d\n",
				__func__, ret);
			goto exit;
		}

		/* MSC_REQ_DONE received */
		switch (p_msc_pkt->command) {
		case CBUS_MSC_MSG:
			if ((p_msc_pkt->offset == MSG_RCPE) &&
			    (p_msc_pkt->data_2 == 0x01)) {
				sii9234_enqueue_msc_work(sii9234, CBUS_MSC_MSG,
							 MSG_RCPK, MSG_RCPK,
							 0x0);
			}
			break;
		case CBUS_WRITE_STAT:
			pr_debug("sii9234: cbus_command_response"
					"CBUS_WRITE_STAT\n");
			cbus_read_reg(sii9234, CBUS_MSC_FIRST_DATA_IN_REG,
					&p_msc_pkt->data_1);
			break;
		case CBUS_SET_INT:
			if ((p_msc_pkt->offset == MHL_RCHANGE_INT) &&
				(p_msc_pkt->data_1 == MHL_INT_DSCR_CHG)) {
				/* Write burst final step...
				   Req->GRT->Write->DSCR */
				pr_debug("sii9234: MHL_RCHANGE_INT &"
						"MHL_INT_DSCR_CHG\n");
			} else if (p_msc_pkt->offset == MHL_RCHANGE_INT &&
				p_msc_pkt->data_1 == MHL_INT_DCAP_CHG) {
				sii9234_enqueue_msc_work(sii9234,
						CBUS_WRITE_STAT,
						MHL_STATUS_REG_CONNECTED_RDY,
						MHL_STATUS_DCAP_READY, 0x0);
			}
			break;
		case CBUS_WRITE_BURST:
			pr_debug("sii9234: cbus_command_response"
					"MHL_WRITE_BURST\n");
			p_msc_pkt->command = CBUS_IDLE;
			sii9234_enqueue_msc_work(sii9234, CBUS_SET_INT,
					MHL_RCHANGE_INT, MHL_INT_DSCR_CHG, 0x0);
			break;
		case CBUS_READ_DEVCAP:
			ret = cbus_read_reg(sii9234,
					    CBUS_MSC_FIRST_DATA_IN_REG, &value);
			if (ret < 0)
				break;
			switch (p_msc_pkt->offset) {
			case DEVCAP_DEV_STATE:
				pr_debug("sii9234: DEVCAP_DEV_STATE\n");
				break;
			case DEVCAP_MHL_VERSION:
				sii9234->devcap.mhl_ver = value;
				pr_debug("sii9234: MHL_VERSION: %X\n", value);
				break;
			case DEVCAP_DEV_CAT:
				if (value & MHL_DEV_CATEGORY_POW_BIT)
					pr_debug("sii9234: CAT=POWERED");
				else
					pr_debug("sii9234: CAT=UNPOWERED");
				break;
			case DEVCAP_ADOPTER_ID_H:
				sii9234->devcap.adopter_id =
				    (value & 0xFF) << 0x8;
				pr_debug("sii9234: DEVCAP_ADOPTER_ID_H = %X\n",
					 value);
				break;
			case DEVCAP_ADOPTER_ID_L:
				sii9234->devcap.adopter_id |= value & 0xFF;
				pr_debug("sii9234: DEVCAP_ADOPTER_ID_L = %X\n",
					 value);
				break;
			case DEVCAP_VID_LINK_MODE:
				sii9234->devcap.vid_link_mode = 0x3F & value;
				pr_debug
				    ("sii9234: MHL_CAP_VID_LINK_MODE = %d\n",
				     sii9234->devcap.vid_link_mode);
				break;
			case DEVCAP_AUD_LINK_MODE:
				sii9234->devcap.aud_link_mode = 0x03 & value;
				pr_debug("sii9234: DEVCAP_AUD_LINK_MODE =%d\n",
					 sii9234->devcap.aud_link_mode);
				break;
			case DEVCAP_VIDEO_TYPE:
				sii9234->devcap.video_type = 0x8F & value;
				pr_debug("sii9234: DEVCAP_VIDEO_TYPE =%d\n",
					 sii9234->devcap.video_type);
				break;
			case DEVCAP_LOG_DEV_MAP:
				sii9234->devcap.log_dev_map = value;
				pr_debug("sii9234: DEVCAP_LOG_DEV_MAP =%d\n",
					 sii9234->devcap.log_dev_map);
				break;
			case DEVCAP_BANDWIDTH:
				sii9234->devcap.bandwidth = value;
				pr_debug("sii9234: DEVCAP_BANDWIDTH =%d\n",
					 sii9234->devcap.bandwidth);
				break;
			case DEVCAP_DEV_FEATURE_FLAG:
				if ((value & MHL_FEATURE_RCP_SUPPORT) == 0)
					pr_debug("sii9234: FEATURE_FLAG=RCP");

				if ((value & MHL_FEATURE_RAP_SUPPORT) == 0)
					pr_debug("sii9234: FEATURE_FLAG=RAP\n");

				if ((value & MHL_FEATURE_SP_SUPPORT) == 0)
					pr_debug("sii9234: FEATURE_FLAG=SP\n");
				break;
			case DEVCAP_DEVICE_ID_H:
				sii9234->devcap.device_id =
				    (value & 0xFF) << 0x8;
				pr_info("sii9234: DEVICE_ID_H=0x%x\n", value);
				break;
			case DEVCAP_DEVICE_ID_L:
				sii9234->devcap.device_id |= value & 0xFF;
				pr_info("sii9234: DEVICE_ID_L=0x%x\n", value);
				break;
			case DEVCAP_SCRATCHPAD_SIZE:
				sii9234->devcap.scratchpad_size = value;
				pr_debug
				    ("sii9234: DEVCAP_SCRATCHPAD_SIZE =%d\n",
				     sii9234->devcap.scratchpad_size);
				break;
			case DEVCAP_INT_STAT_SIZE:
				sii9234->devcap.int_stat_size = value;
				pr_debug("sii9234: DEVCAP_INT_STAT_SIZE =%d\n",
					 sii9234->devcap.int_stat_size);
				break;
			case DEVCAP_RESERVED:
				sii9234->dcap_ready_status = 1;
				sii9234->devcap.reserved_data = value;
				pr_info("sii9234: DEVCAP_RESERVED : %d\n",
					value);
				wake_up(&sii9234->wq);
				break;
			default:
				pr_debug("sii9234: DEVCAP DEFAULT\n");
				break;
			}
			break;
		default:
			break;
		}

		list_del(&p_msc_pkt->p_msc_packet_list);
		pr_debug("[MSC] %s() free item , addr = 0x%x, cnt=%d\n",
			 __func__, (unsigned int)p_msc_pkt, --g_list_cnt);
		kfree(p_msc_pkt);
	}
 exit:
	sii9234_mutex_unlock(&sii9234->lock);
	sii9234_cbus_mutex_unlock(&sii9234->cbus_lock);
}

static int sii9234_enqueue_msc_work(struct sii9234_data *sii9234, u8 command,
				    u8 offset, u8 data_1, u8 data_2)
{
	struct msc_packet *packet_item;

	packet_item = kmalloc(sizeof(struct msc_packet), GFP_KERNEL);
	if (!packet_item) {
		pr_err("[ERROR] %s() kmalloc error\n", __func__);
		return -ENOMEM;
	} else
		pr_debug("[MSC] %s() add item, addr = 0x%x, cnt=%d\n",
			 __func__, (unsigned int)packet_item, ++g_list_cnt);

	packet_item->command = command;
	packet_item->offset = offset;
	packet_item->data_1 = data_1;
	packet_item->data_2 = data_2;

	pr_debug("[MSC] %s() command(0x%x), offset(0x%x), data_1(0x%x), "
		"data_2(0x%x)\n", __func__, command, offset, data_1, data_2);
	list_add_tail(&packet_item->p_msc_packet_list, &g_msc_packet_list);

	pr_debug("[MSC] %s() msc work schedule\n", __func__);
	queue_work(sii9234_msc_wq, &(sii9234->msc_work));

	return 0;
}

/* Must call with sii9234->lock held */
static int sii9234_msc_req_locked(struct sii9234_data *sii9234,
				  struct msc_packet *msc_pkt)
{
	int ret;
	u8 start_command;

	if (sii9234->state != STATE_ESTABLISHED)
		return -ENOENT;

	init_completion(&sii9234->msc_complete);

	cbus_write_reg(sii9234, CBUS_MSC_OFFSET_REG, msc_pkt->offset);
	if (msc_pkt->command == CBUS_MSC_MSG)
		msc_pkt->data_1 = msc_pkt->offset;
	cbus_write_reg(sii9234, CBUS_MSC_FIRST_DATA_OUT_REG, msc_pkt->data_1);

	switch (msc_pkt->command) {
	case CBUS_SET_INT:
	case CBUS_WRITE_STAT:
		start_command = START_BIT_WRITE_STAT_INT;
		break;
	case CBUS_MSC_MSG:
		cbus_write_reg(sii9234, CBUS_MSC_SECOND_DATA_OUT_REG,
			       msc_pkt->data_2);
		cbus_write_reg(sii9234, CBUS_MSC_OFFSET_REG, msc_pkt->command);

		start_command = START_BIT_MSC_MSG;
		break;
	case CBUS_READ_DEVCAP:
		start_command = START_BIT_READ_DEVCAP;
		break;
	case CBUS_WRITE_BURST:
		start_command = START_BIT_WRITE_BURST;
		break;
	case CBUS_GET_STATE:
	case CBUS_GET_VENDOR_ID:
	case CBUS_SET_HPD:
	case CBUS_CLR_HPD:
	case CBUS_GET_MSC_ERR_CODE:
	case CBUS_GET_SC3_ERR_CODE:
	case CBUS_GET_SC1_ERR_CODE:
	case CBUS_GET_DDC_ERR_CODE:
		cbus_write_reg(sii9234, CBUS_MSC_OFFSET_REG, msc_pkt->command);
		start_command = START_BIT_MSC_RESERVED;
		break;
	default:
		pr_err("[ERROR] %s() invalid msc command(%d)\n",
		       __func__, msc_pkt->command);
		return -EINVAL;
	}

	cbus_write_reg(sii9234, CBUS_MSC_COMMAND_START_REG, start_command);

	sii9234_mutex_unlock(&sii9234->lock);
	ret = wait_for_completion_timeout(&sii9234->msc_complete,
					  msecs_to_jiffies(300));
	if (ret == 0)
		printk(KERN_ERR "[ERROR] %s() MSC_REQ_DONE timeout\n",
		       __func__);

	sii9234_mutex_lock(&sii9234->lock);

	return ret ? 0 : -EIO;
}
#endif				/* __MHL_NEW_CBUS_MSC_CMD__ end */

static void sii9234_detection_callback_worker(struct work_struct *p)
{
	pr_debug("%s()\n", __func__);
	sii9234_detection_callback();
	return;
}

static void mhl_start_worker(struct work_struct *p)
{
	pr_debug("%s()\n", __func__);
	mhl_onoff_ex(1);
	return;
}

static void mhl_end_worker(struct work_struct *p)
{
	pr_debug("%s()\n", __func__);
	mhl_onoff_ex(0);
	return;
}

static void mhl_goto_d3_worker(struct work_struct *p)
{
	pr_debug("%s()\n", __func__);
	goto_d3();
	return;
}

static void mhl_cbus_write_stat_worker(struct work_struct *p)
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);
#ifndef __MHL_NEW_CBUS_MSC_CMD__
	cbus_command_request(sii9234, CBUS_WRITE_STAT, CBUS_LINK_CONTROL_2_REG,
			     sii9234->mhl_status_value.linkmode);
#else
	sii9234_enqueue_msc_work(sii9234, CBUS_WRITE_STAT,
				 CBUS_LINK_CONTROL_2_REG,
				 sii9234->mhl_status_value.linkmode, 0x0);
#endif
	return;
}

static int sii9234_detection_callback(void)
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);
	int ret;
	u8 value;
	int handled = 0;

	pr_debug("%s() - start\n", __func__);
	sii9234_callback_sched = 1;

	sii9234_mutex_lock(&sii9234->lock);
	sii9234->mhl_status_value.linkmode = MHL_STATUS_CLK_MODE_NORMAL;
	sii9234->rgnd = RGND_UNKNOWN;
	sii9234->state = STATE_DISCONNECTED;
	sii9234->rsen = false;
#ifdef __MHL_NEW_CBUS_MSC_CMD__
	sii9234->dcap_ready_status = 0;
#endif
	memset(cbus_pkt_buf, 0x00, sizeof(cbus_pkt_buf));
	ret = sii9234_power_init(sii9234);
	if (ret < 0) {
		pr_err("[ERROR] %s() - sii9234_power_init\n", __func__);
		goto unhandled;
	}

	ret = sii9234_cbus_reset(sii9234);
	if (ret < 0) {
		pr_err("[ERROR] %s() - sii9234_cbus_reset\n", __func__);
		goto unhandled;
	}

	ret = sii9234_hdmi_init(sii9234);
	if (ret < 0) {
		pr_err("[ERROR] %s() - sii9234_hdmi_init\n", __func__);
		goto unhandled;
	}

	ret = sii9234_mhl_tx_ctl_int(sii9234);
	if (ret < 0) {
		pr_err("[ERROR] %s() - sii9234_mhl_tx_ctl_int\n", __func__);
		goto unhandled;
	}

	/* Enable HDCP Compliance safety */
	ret = mhl_tx_write_reg(sii9234, 0x2B, 0x01);
	if (ret < 0) {
		pr_err("[ERROR] %s() - mhl_tx_write_reg 0x2B\n", __func__);
		goto unhandled;
	}

	/* CBUS discovery cycle time for each drive and float = 150us */
	ret = mhl_tx_read_reg(sii9234, MHL_TX_DISC_CTRL1_REG, &value);
	if (ret < 0) {
		pr_err("[ERROR] %s() - MHL_TX_DISC_CTRL1_REG\n", __func__);
		goto unhandled;
	}

	value &= ~(1 << 3);
	value |= (1 << 2);
	ret = mhl_tx_write_reg(sii9234, MHL_TX_DISC_CTRL1_REG, value);
	if (ret < 0) {
		pr_err("[ERROR] %s() - MHL_TX_DISC_CTRL1_REG\n", __func__);
		goto unhandled;
	}

	/* Clear bit 6 (reg_skip_rgnd) */
	ret = mhl_tx_write_reg(sii9234,
			MHL_TX_DISC_CTRL2_REG, (1 << 7) /* Reserved Bit */ |
			2 << ATT_THRESH_SHIFT | DEGLITCH_TIME_50MS);
	if (ret < 0) {
		pr_err("[ERROR] %s() - Clear bit 6\n", __func__);
		goto unhandled;
	}

	/* Changed from 66 to 65 for 94[1:0] = 01 = 5k reg_cbusmhl_pup_sel */
	/* 1.8V CBUS VTH & GND threshold */
	/*To meet CTS 3.3.7.2 spec */
	ret = mhl_tx_write_reg(sii9234, MHL_TX_DISC_CTRL5_REG, 0x77);
	if (ret < 0) {
		pr_err("[ERROR] %s() - MHL_TX_DISC_CTRL5_REG\n", __func__);
		goto unhandled;
	}

	/* set bit 2 and 3, which is Initiator Timeout */
	ret = cbus_read_reg(sii9234, CBUS_LINK_CONTROL_2_REG, &value);
	if (ret < 0) {
		pr_err("[ERROR] %s() - read CBUS_LINK_CONTROL_2_REG\n",
		       __func__);
		goto unhandled;
	}

	value |= 0x0C;

	ret = cbus_write_reg(sii9234, CBUS_LINK_CONTROL_2_REG, value);
	if (ret < 0) {
		pr_err("[ERROR] %s() - write CBUS_LINK_CONTROL_2_REG\n",
		       __func__);
		goto unhandled;
	}

	ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL6_REG, 0xA0);
	if (ret < 0) {
		pr_err("[ERROR] %s() - write MHL_TX_MHLTX_CTL6_REG\n",
		       __func__);
		goto unhandled;
	}

	/* RGND & single discovery attempt (RGND blocking) */
	ret = mhl_tx_write_reg(sii9234, MHL_TX_DISC_CTRL6_REG, BLOCK_RGND_INT |
			       DVRFLT_SEL | SINGLE_ATT);
	if (ret < 0) {
		pr_err("[ERROR] %s() - write MHL_TX_DISC_CTRL6_REG\n",
		       __func__);
		goto unhandled;
	}

	/* Use VBUS path of discovery state machine */
	ret = mhl_tx_write_reg(sii9234, MHL_TX_DISC_CTRL8_REG, 0);
	if (ret < 0) {
		pr_err("[ERROR] %s() - write MHL_TX_DISC_CTRL8_REG\n",
		       __func__);
		goto unhandled;
	}

	/* 0x92[3] sets the CBUS / ID switch */
	ret = mhl_tx_set_reg(sii9234, MHL_TX_DISC_CTRL6_REG, USB_ID_OVR);
	if (ret < 0) {
		pr_err("[ERROR] %s() - set MHL_TX_DISC_CTRL6_REG\n", __func__);
		goto unhandled;
	}

	/* To allow RGND engine to operate correctly.
	 * When moving the chip from D2 to D0 (power up, init regs)
	 * the values should be
	 * 94[1:0] = 01  reg_cbusmhl_pup_sel[1:0] should be set for 5k
	 * 93[7:6] = 10  reg_cbusdisc_pup_sel[1:0] should be
	 * set for 10k (default)
	 * 93[5:4] = 00  reg_cbusidle_pup_sel[1:0] = open (default)
	 */
	ret = mhl_tx_set_reg(sii9234, MHL_TX_DISC_CTRL3_REG, 0x86);
	if (ret < 0) {
		pr_err("[ERROR] %s() - set MHL_TX_DISC_CTRL3_REG\n", __func__);
		goto unhandled;
	}

	/* change from CC to 8C to match 5K */
	/*To meet CTS 3.3.72 spec */
	ret = mhl_tx_set_reg(sii9234, MHL_TX_DISC_CTRL4_REG, 0x8C);
	if (ret < 0) {
		pr_err("[ERROR] %s() - set MHL_TX_DISC_CTRL4_REG\n", __func__);
		goto unhandled;
	}

	/* Force upstream HPD to 0 when not in MHL mode */
	ret = mhl_tx_clear_reg(sii9234, MHL_TX_INT_CTRL_REG, (1 << 5));
	if (ret < 0) {
		pr_err("[ERROR] %s() - clear MHL_TX_INT_CTRL_REG\n", __func__);
		goto unhandled;
	}
	ret = mhl_tx_set_reg(sii9234, MHL_TX_INT_CTRL_REG, (1 << 4));
	if (ret < 0) {
		pr_err("[ERROR] %s() - set MHL_TX_INT_CTRL_REG\n", __func__);
		goto unhandled;
	}

	/* Configure the interrupt as active high */
	ret =
	    mhl_tx_clear_reg(sii9234, MHL_TX_INT_CTRL_REG, (1 << 2) | (1 << 1));
	if (ret < 0) {
		pr_err("[ERROR] %s() - clear MHL_TX_INT_CTRL_REG\n", __func__);
		goto unhandled;
	}

	msleep(25);

	/* release usb_id switch : HW decides the switch setting */
	ret = mhl_tx_clear_reg(sii9234, MHL_TX_DISC_CTRL6_REG, USB_ID_OVR);
	if (ret < 0) {
		pr_err("[ERROR] %s() - clear MHL_TX_DISC_CTRL6_REG\n",
		       __func__);
		goto unhandled;
	}

	ret = sii9234_cbus_init(sii9234);
	if (ret < 0)
		goto unhandled;
	ret = mhl_tx_write_reg(sii9234, MHL_TX_DISC_CTRL1_REG, 0x27);
	if (ret < 0)
		goto unhandled;

	/* Enable Auto soft reset on SCDT = 0 */
	ret = mhl_tx_write_reg(sii9234, 0x05, 0x04);
	if (ret < 0)
		goto unhandled;

	/* HDMI Transcode mode enable */
	ret = mhl_tx_write_reg(sii9234, 0x0D, 0x1C);
	if (ret < 0)
		goto unhandled;

#ifdef __CONFIG_TMDS_OFFON_WORKAROUND__
	ret = mhl_tx_write_reg(sii9234, MHL_TX_INTR4_ENABLE_REG,
			       RGND_READY_MASK | CBUS_LKOUT_MASK |
			       MHL_DISC_FAIL_MASK | MHL_EST_MASK | (1 << 0));
#else
	ret = mhl_tx_write_reg(sii9234, MHL_TX_INTR4_ENABLE_REG,
			       RGND_READY_MASK | CBUS_LKOUT_MASK |
			       MHL_DISC_FAIL_MASK | MHL_EST_MASK);
#endif
	if (ret < 0)
		goto unhandled;

	ret = mhl_tx_write_reg(sii9234, MHL_TX_INTR1_ENABLE_REG,
			       (1 << 5) | (1 << 6));
	if (ret < 0)
		goto unhandled;

/* this point is very importand before megsure RGND impedance*/
	force_usb_id_switch_open(sii9234);
	ret = mhl_tx_clear_reg(sii9234, MHL_TX_DISC_CTRL4_REG,
			       (1 << 7) | (1 << 6) | (1 << 5) | (1 << 4));
	if (ret < 0)
		goto unhandled;
	ret =
	    mhl_tx_clear_reg(sii9234, MHL_TX_DISC_CTRL5_REG,
			     (1 << 1) | (1 << 0));
	if (ret < 0)
		goto unhandled;
	release_usb_id_switch_open(sii9234);
/*end of this*/
	pr_debug("sii9234: waiting for RGND measurement\n");
	sii9234_enable_irq();

	/* SiI9244 Programmer's Reference Section 2.4.3
	 * State : RGND Ready
	 */
	sii9234_mutex_unlock(&sii9234->lock);

	pr_debug("sii9234: waiting for detection\n");
	ret = wait_event_timeout(sii9234->wq,
				 sii9234->state != STATE_DISCONNECTED,
				 msecs_to_jiffies(T_WAIT_TIMEOUT_DISC_INT * 2));
	sii9234_mutex_lock(&sii9234->lock);
	if (ret == 0) {
		pr_err("[ERROR] %s() - wait detection\n", __func__);
		goto unhandled;
	}

	if (sii9234->state == STATE_DISCOVERY_FAILED) {
		handled = -1;
		pr_err("[ERROR] %s() - state == STATE_DISCOVERY_FAILED\n",
		       __func__);
		goto unhandled;
	}

	if (sii9234->state == NO_MHL_STATUS) {
		handled = -1;
		pr_err("[ERROR] %s() - state == NO_MHL_STATUS\n", __func__);
		goto unhandled;
	}

	if (mhl_state_is_error(sii9234->state)) {
		pr_err("[ERROR] %s() -mhl_state_is_error\n", __func__);
		goto unhandled;
	}

	sii9234_mutex_unlock(&sii9234->lock);
	pr_info("sii9234: Established & start to moniter RSEN\n");
	/*CTS 3.3.14.3 Discovery;Sink Never Drives MHL+/- HIGH */
	/*MHL SPEC 8.2.1.1.1;Transition SRC4-SRC7 */
	if (rsen_state_timer_out(sii9234) < 0)
		return handled;

	sii9234->claimed = true;

	ret = cbus_write_reg(sii9234,
			     CBUS_INTR1_ENABLE_REG,
			     MSC_RESP_ABORT_MASK |
			     MSC_REQ_ABORT_MASK |
			     MSC_REQ_DONE_MASK |
			     MSC_MSG_RECD_MASK | CBUS_DDC_ABORT_MASK);
	if (ret < 0)
		goto unhandled_nolock;

	ret = cbus_write_reg(sii9234,
			     CBUS_INTR2_ENABLE_REG,
			     WRT_STAT_RECD_MASK | SET_INT_RECD_MASK);
	if (ret < 0)
		goto unhandled_nolock;

#ifdef __MHL_NEW_CBUS_MSC_CMD__
	ret = wait_event_timeout(sii9234->wq,
				 sii9234->dcap_ready_status,
				 msecs_to_jiffies(500));
	if (ret == 0) {
		sii9234->vbus_owner = 0; /*UNKNOWN*/
		pr_debug("dcap_timeout err, dcap_staus:%d\n",
			 sii9234->dcap_ready_status);
	} else {
		/*SAMSUNG DEVICE_ID 0x1134:dongle, 0x1234:dock */
		if (sii9234->devcap.device_id == SS_MHL_DONGLE_DEV_ID ||
		    sii9234->devcap.device_id == SS_MHL_DOCK_DEV_ID)
			sii9234->vbus_owner = sii9234->devcap.reserved_data;
		else
			sii9234->vbus_owner = 0;
	}
	pr_debug("device_id:0x%4x, vbus_owner:%d\n",
		 sii9234->devcap.device_id, sii9234->vbus_owner);
	/*send some data for VBUS SRC such a TA or USB or UNKNOWN */
	if (sii9234->pdata->vbus_present)
		sii9234->pdata->vbus_present(true, sii9234->vbus_owner);
#endif

	return handled;

 unhandled:
	pr_info("sii9234: Detection failed");
	if (sii9234->state == STATE_DISCONNECTED) {
		pr_cont(" (timeout)");
		schedule_work(&sii9234->mhl_end_work);
	} else if (sii9234->state == STATE_DISCOVERY_FAILED)
		pr_cont(" (discovery failed)");
	else if (sii9234->state == NO_MHL_STATUS)
		pr_cont(" (already went to D3)");
	else if (sii9234->state == STATE_CBUS_LOCKOUT)
		pr_cont(" (cbus_lockout)");
	pr_cont("\n");

	/*mhl spec: 8.3.3, if discovery failed, must retry discovering */
#ifdef	CONFIG_SAMSUNG_USE_11PIN_CONNECTOR
	if ((sii9234->state == STATE_DISCOVERY_FAILED) &&
	    (sii9234->rgnd == RGND_1K)) {
#else
	if ((sii9234->state == STATE_DISCOVERY_FAILED)) {
#endif
		pr_cont("Discovery failed but RGND_1K impedence"
			" restart detection_callback");

		schedule_work(&sii9234->mhl_end_work);
	}

	sii9234_mutex_unlock(&sii9234->lock);

	return handled;

 unhandled_nolock:
	pr_info("sii9234: Detection failed");
	if (sii9234->state == STATE_DISCONNECTED) {
		pr_cont(" (timeout)");
		schedule_work(&sii9234->mhl_end_work);
	} else if (sii9234->state == STATE_DISCOVERY_FAILED)
		pr_cont(" (discovery failed)");
	else if (sii9234->state == NO_MHL_STATUS)
		pr_cont(" (already went to D3)");
	else if (sii9234->state == STATE_CBUS_LOCKOUT)
		pr_cont(" (cbus_lockout)");
	pr_cont("\n");

	/*mhl spec: 8.3.3, if discovery failed, must retry discovering */
#ifdef	CONFIG_SAMSUNG_USE_11PIN_CONNECTOR
	if ((sii9234->state == STATE_DISCOVERY_FAILED) &&
	    (sii9234->rgnd == RGND_1K)) {
#else
	if ((sii9234->state == STATE_DISCOVERY_FAILED)) {
#endif
		pr_cont("Discovery failed but RGND_1K impedence"
			" restart detection_callback");

		schedule_work(&sii9234->mhl_end_work);
	}

	return handled;
}

static void sii9234_cancel_callback(void)
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);

	sii9234_mutex_lock(&sii9234->lock);
	sii9234_power_down(sii9234);
	sii9234_mutex_unlock(&sii9234->lock);
}

#ifdef CONFIG_SAMSUNG_MHL_9290
static int sii9234_30pin_callback(struct notifier_block *this,
				  unsigned long event, void *ptr)
{
	int ret = false;
	struct sii9234_data *sii9234 = container_of(this, struct sii9234_data,
						    acc_con_nb);

	pr_debug("%s\n", __func__);
	/* if connection event, start detection */
	if (event) {
		ret = sii9234_30pin_init_for_9290(sii9234);
#ifdef	CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE
		mhl_hpd_handler(true);
#endif
	} else {
		pr_info("sii9234: power down for 9290\n");
		sii9234->state = STATE_DISCONNECTED;
		sii9234->pdata->hw_onoff(0);
#ifdef	CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE
		mhl_hpd_handler(false);
#endif
		ret = false;
	}
	return ret;
}

static int sii9234_cbus_init_for_9290(struct sii9234_data *sii9234)
{
	int ret = 0;

	ret = cbus_write_reg(sii9234, 0x1F, 0x02);
	if (ret < 0)
		return ret;
	ret = cbus_write_reg(sii9234, 0x07, 0x30 | 0x06);
	if (ret < 0)
		return ret;
	ret = cbus_write_reg(sii9234, 0x40, 0x03);
	if (ret < 0)
		return ret;
	ret = cbus_write_reg(sii9234, 0x42, 0x06);
	if (ret < 0)
		return ret;
	ret = cbus_write_reg(sii9234, 0x36, 0x0C);
	if (ret < 0)
		return ret;
	ret = cbus_write_reg(sii9234, 0x3D, 0xFD);
	if (ret < 0)
		return ret;
	ret = cbus_write_reg(sii9234, 0x1C, 0x00);
	if (ret < 0)
		return ret;
	ret = cbus_write_reg(sii9234, 0x44, 0x00);

	return ret;
}

static int sii9234_30pin_reg_init_for_9290(struct sii9234_data *sii9234)
{
	int ret = 0;
	u8 value;

	ret = tpi_write_reg(sii9234, 0x3D, 0x3F);
	if (ret < 0)
		return ret;

	ret = hdmi_rx_write_reg(sii9234, 0x11, 0x01);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x12, 0x15);
	if (ret < 0)
		return ret;
	ret = mhl_tx_write_reg(sii9234, 0x08, 0x35);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x00, 0x00);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x13, 0x60);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x14, 0xF0);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x4B, 0x06);
	if (ret < 0)
		return ret;

	/* Analog PLL Control */
	ret = hdmi_rx_write_reg(sii9234, 0x17, 0x07);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x1A, 0x20);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x22, 0xE0);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x23, 0xC0);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x24, 0xA0);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x25, 0x80);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x26, 0x60);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x27, 0x40);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x28, 0x20);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x29, 0x00);
	if (ret < 0)
		return ret;

	ret = hdmi_rx_write_reg(sii9234, 0x4D, 0x02);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x4C, 0xA0);
	if (ret < 0)
		return ret;

	ret = mhl_tx_write_reg(sii9234, 0x80, 0x34);
	if (ret < 0)
		return ret;

	ret = hdmi_rx_write_reg(sii9234, 0x31, 0x0B);
	if (ret < 0)
		return ret;
	ret = hdmi_rx_write_reg(sii9234, 0x45, 0x06);
	if (ret < 0)
		return ret;
	ret = mhl_tx_write_reg(sii9234, 0xA0, 0xD0);
	if (ret < 0)
		return ret;
	ret = mhl_tx_write_reg(sii9234, 0xA1, 0xFC);
	if (ret < 0)
		return ret;
#ifdef	CONFIG_MACH_P4NOTE
	ret = mhl_tx_write_reg(sii9234, 0xA3, 0xC0);	/*output swing level*/
	if (ret < 0)
		return ret;
#else
	ret = mhl_tx_write_reg(sii9234, 0xA3, 0xEB);	/*output swing level */
	if (ret < 0)
		return ret;
#endif
	ret = mhl_tx_write_reg(sii9234, 0xA6, 0x00);
	if (ret < 0)
		return ret;

	ret = mhl_tx_write_reg(sii9234, 0x2B, 0x01);
	if (ret < 0)
		return ret;

	/* CBUS & Discovery */
	ret = mhl_tx_read_reg(sii9234, MHL_TX_DISC_CTRL1_REG, &value);
	if (ret < 0)
		return ret;
	value &= ~(1 << 2);
	value |= (1 << 3);
	ret = mhl_tx_write_reg(sii9234, MHL_TX_DISC_CTRL1_REG, value);
	if (ret < 0)
		return ret;

	ret = mhl_tx_write_reg(sii9234, 0x91, 0xE5);
	if (ret < 0)
		return ret;
	ret = mhl_tx_write_reg(sii9234, 0x94, 0x66);
	if (ret < 0)
		return ret;

	ret = cbus_read_reg(sii9234, 0x31, &value);
	if (ret < 0)
		return ret;
	value |= 0x0C;
	if (ret < 0)
		return ret;
	ret = cbus_write_reg(sii9234, 0x31, value);
	if (ret < 0)
		return ret;

	ret = mhl_tx_write_reg(sii9234, 0xA5, 0x80);
	if (ret < 0)
		return ret;
	ret = mhl_tx_write_reg(sii9234, 0x95, 0x31);
	if (ret < 0)
		return ret;
	ret = mhl_tx_write_reg(sii9234, 0x96, 0x22);
	if (ret < 0)
		return ret;

	ret = mhl_tx_read_reg(sii9234, MHL_TX_DISC_CTRL6_REG, &value);
	if (ret < 0)
		return ret;
	value |= (1 << 6);
	ret = mhl_tx_write_reg(sii9234, MHL_TX_DISC_CTRL6_REG, value);
	if (ret < 0)
		return ret;

	ret = mhl_tx_write_reg(sii9234, 0x92, 0x46);
	if (ret < 0)
		return ret;
	ret = mhl_tx_write_reg(sii9234, 0x93, 0xDC);
	if (ret < 0)
		return ret;

	ret =
	    mhl_tx_clear_reg(sii9234, MHL_TX_INT_CTRL_REG, (1 << 2) | (1 << 1));
	if (ret < 0)
		return ret;

	mdelay(25);

	ret = mhl_tx_clear_reg(sii9234, MHL_TX_DISC_CTRL6_REG, USB_ID_OVR);
	if (ret < 0)
		return ret;

	ret = mhl_tx_write_reg(sii9234, 0x90, 0x27);
	if (ret < 0)
		return ret;

	ret = sii9234_cbus_init_for_9290(sii9234);
	if (ret < 0)
		return ret;

	ret = mhl_tx_write_reg(sii9234, 0x05, 0x4);
	if (ret < 0)
		return ret;
	ret = mhl_tx_write_reg(sii9234, 0x0D, 0x1C);

	return ret;
}

static int sii9234_30pin_init_for_9290(struct sii9234_data *sii9234)
{
	u8 value;
	int ret = 0;

	sii9234_mutex_lock(&sii9234->lock);
	sii9234->pdata->hw_onoff(1);
	sii9234->pdata->hw_reset();

	/*sii9234->state = STATE_9290_CONNECTED; */

	/* init registers */
	ret = sii9234_30pin_reg_init_for_9290(sii9234);
	if (ret < 0)
		goto unhandled;

	/* start tpi */
	ret = mhl_tx_write_reg(sii9234, 0xC7, 0x00);
	if (ret < 0)
		goto unhandled;

	/* enable interrupts */
	ret = mhl_tx_write_reg(sii9234, 0xBC, 0x01);
	if (ret < 0)
		goto unhandled;
	ret = mhl_tx_write_reg(sii9234, 0xBD, 0x78);
	if (ret < 0)
		goto unhandled;
	ret = mhl_tx_write_reg(sii9234, 0xBE, 0x01);
	if (ret < 0)
		goto unhandled;

	/* mhd rx connected */
	ret = mhl_tx_write_reg(sii9234, 0xBC, 0x01);
	if (ret < 0)
		goto unhandled;
	ret = mhl_tx_write_reg(sii9234, 0xBD, 0xA0);
	if (ret < 0)
		goto unhandled;
	ret = mhl_tx_write_reg(sii9234, 0xBE, 0x10);
	if (ret < 0)
		goto unhandled;
	ret = cbus_write_reg(sii9234, 0x07, 0x30 | 0x0E);
	if (ret < 0)
		goto unhandled;
	ret = cbus_write_reg(sii9234, 0x47, 0x03);
	if (ret < 0)
		goto unhandled;
	ret = cbus_write_reg(sii9234, 0x21, 0x01);
	if (ret < 0)
		goto unhandled;

	/* enable mhd tx */
	ret = mhl_tx_clear_reg(sii9234, 0x1A, 1 << 4);
	if (ret < 0)
		goto unhandled;

	/* set mhd power active mode */
	ret = mhl_tx_clear_reg(sii9234, 0x1E, 1 << 1 | 1 << 0);
	if (ret < 0)
		goto unhandled;

	ret = mhl_tx_write_reg(sii9234, 0xBC, 0x01);
	if (ret < 0)
		goto unhandled;
	ret = mhl_tx_write_reg(sii9234, 0xBD, 0xA0);
	if (ret < 0)
		goto unhandled;

	ret = mhl_tx_read_reg(sii9234, 0xBE, &value);
	if (ret < 0)
		goto unhandled;
	if ((value & (1 << 7 | 1 << 6)) != 0x00) {
		/* Assert Mobile HD FIFO Reset */
		ret = mhl_tx_write_reg(sii9234, 0xBC, 0x01);
		if (ret < 0)
			goto unhandled;
		ret = mhl_tx_write_reg(sii9234, 0xBD, 0x05);
		if (ret < 0)
			goto unhandled;
		ret = mhl_tx_write_reg(sii9234, 0xBE, (1 << 4 | 0x04));
		if (ret < 0)
			goto unhandled;
		mdelay(1);
		/* Deassert Mobile HD FIFO Reset */
		ret = mhl_tx_write_reg(sii9234, 0xBC, 0x01);
		if (ret < 0)
			goto unhandled;
		ret = mhl_tx_write_reg(sii9234, 0xBD, 0x05);
		if (ret < 0)
			goto unhandled;
		ret = mhl_tx_write_reg(sii9234, 0xBE, 0x04);
		if (ret < 0)
			goto unhandled;
	}

	sii9234_mutex_unlock(&sii9234->lock);
	return true;

 unhandled:
	sii9234->pdata->hw_onoff(0);
	sii9234->state = STATE_DISCONNECTED;
	sii9234_mutex_unlock(&sii9234->lock);
	return false;
}
static struct workqueue_struct *sii9234_tmds_reset_wq;

void sii9234_tmds_reset()
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);
	queue_work(sii9234_tmds_reset_wq, &(sii9234->tmds_reset_work));
}
void sii9234_tmds_reset_work(struct work_struct *work)
{
	/*this function is a workaround for LSI AP*/
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);

	msleep(80);
	mhl_tx_write_reg(sii9234, 0x1A, 1 << 4);
	mhl_tx_clear_reg(sii9234, 0x1A, 1 << 4);
	pr_info("sii9234: tmds reset\n");

}
EXPORT_SYMBOL(sii9234_tmds_reset);

#endif				/* CONFIG_SAMSUNG_MHL_9290 */

static void save_cbus_pkt_to_buffer(struct sii9234_data *sii9234)
{
	int index;

	for (index = 0; index < CBUS_PKT_BUF_COUNT; index++)
		if (sii9234->cbus_pkt_buf[index].status == false)
			break;

	if (index == CBUS_PKT_BUF_COUNT) {
		pr_debug("sii9234: Error save_cbus_pkt Buffer Full\n");
		index -= 1;	/*adjust index */
	}

	pr_debug("sii9234: save_cbus_pkt_to_buffer index = %d\n", index);
	memcpy(&sii9234->cbus_pkt_buf[index], &sii9234->cbus_pkt,
	       sizeof(struct cbus_packet));
	sii9234->cbus_pkt_buf[index].status = true;
}

static void cbus_command_response(struct sii9234_data *sii9234)
{
	u8 value, offset = 0;

	sii9234_cbus_mutex_lock(&sii9234->cbus_lock);
	pr_debug("sii9234: cbus_command_response\n");

	switch (sii9234->cbus_pkt.command) {
	case CBUS_MSC_MSG:
		pr_debug("sii9234: cbus_command_response Received"
			 " ACK for CBUS_MSC_MSG\n");
#ifdef CONFIG_SII9234_RCP
		if (sii9234->cbus_pkt.data[0] == MSG_RCPE &&
		    sii9234->cbus_pkt.data[1] == 0x01) {
			sii9234->cbus_pkt.command = CBUS_IDLE;
			sii9234_cbus_mutex_unlock(&sii9234->cbus_lock);
			cbus_command_request(sii9234, CBUS_MSC_MSG, MSG_RCPK,
					     sii9234->error_key);
			return;
		}
#endif
		break;
	case CBUS_WRITE_STAT:
		pr_debug("sii9234: cbus_command_response" "CBUS_WRITE_STAT\n");
		cbus_read_reg(sii9234, CBUS_MSC_FIRST_DATA_IN_REG,
			      &sii9234->cbus_pkt.data[0]);
		break;
	case CBUS_SET_INT:
		pr_debug("sii9234: cbus_command_response CBUS_SET_INT\n");
		if (sii9234->cbus_pkt.offset == MHL_RCHANGE_INT &&
		    sii9234->cbus_pkt.data[0] == MHL_INT_DSCR_CHG) {
			/*Write burst final step... Req->GRT->Write->DSCR */
			pr_debug("sii9234: MHL_RCHANGE_INT &"
				 "MHL_INT_DSCR_CHG\n");
		} else if (sii9234->cbus_pkt.offset == MHL_RCHANGE_INT &&
			   sii9234->cbus_pkt.data[0] == MHL_INT_DCAP_CHG) {
			pr_debug("sii9234: MHL_RCHANGE_INT &"
				 "MHL_INT_DCAP_CHG\n");
			sii9234->cbus_pkt.command = CBUS_IDLE;
			sii9234_cbus_mutex_unlock(&sii9234->cbus_lock);
			cbus_command_request(sii9234, CBUS_WRITE_STAT,
					     MHL_STATUS_REG_CONNECTED_RDY,
					     MHL_STATUS_DCAP_READY);
			return;
		}
		break;
	case CBUS_WRITE_BURST:
		pr_debug("sii9234: cbus_command_response" "MHL_WRITE_BURST\n");
		sii9234->cbus_pkt.command = CBUS_IDLE;
		sii9234_cbus_mutex_unlock(&sii9234->cbus_lock);
		cbus_command_request(sii9234, CBUS_SET_INT,
				     MHL_RCHANGE_INT, MHL_INT_DSCR_CHG);
		return;
	case CBUS_READ_DEVCAP:
		pr_debug("sii9234: cbus_command_response"
			 " CBUS_READ_DEVCAP\n");
		cbus_read_reg(sii9234, CBUS_MSC_FIRST_DATA_IN_REG, &value);
		switch (sii9234->cbus_pkt.offset) {
		case DEVCAP_MHL_VERSION:
			sii9234->devcap.mhl_ver = value;
			pr_debug("sii9234: MHL_VERSION: %X\n", value);
			break;
		case DEVCAP_DEV_CAT:
			if (value & MHL_DEV_CATEGORY_POW_BIT)
				pr_debug("sii9234: CAT=POWERED");
			else
				pr_debug("sii9234: CAT=UNPOWERED");
			break;
		case DEVCAP_ADOPTER_ID_H:
			sii9234->devcap.adopter_id = (value & 0xFF) << 0x8;
			pr_debug("sii9234: DEVCAP_ADOPTER_ID_H = %X\n", value);
			break;
		case DEVCAP_ADOPTER_ID_L:
			sii9234->devcap.adopter_id |= value & 0xFF;
			pr_debug("sii9234: DEVCAP_ADOPTER_ID_L = %X\n", value);
			break;
		case DEVCAP_VID_LINK_MODE:
			sii9234->devcap.vid_link_mode = 0x3F & value;
			pr_debug("sii9234: MHL_CAP_VID_LINK_MODE = %d\n",
				 sii9234->devcap.vid_link_mode);
			break;
		case DEVCAP_AUD_LINK_MODE:
			sii9234->devcap.aud_link_mode = 0x03 & value;
			pr_debug("sii9234: DEVCAP_AUD_LINK_MODE =%d\n",
				 sii9234->devcap.aud_link_mode);
			break;
		case DEVCAP_VIDEO_TYPE:
			sii9234->devcap.video_type = 0x8F & value;
			pr_debug("sii9234: DEVCAP_VIDEO_TYPE =%d\n",
				 sii9234->devcap.video_type);
			break;
		case DEVCAP_LOG_DEV_MAP:
			sii9234->devcap.log_dev_map = value;
			pr_debug("sii9234: DEVCAP_LOG_DEV_MAP =%d\n",
				 sii9234->devcap.log_dev_map);
			break;
		case DEVCAP_BANDWIDTH:
			sii9234->devcap.bandwidth = value;
			pr_debug("sii9234: DEVCAP_BANDWIDTH =%d\n",
				 sii9234->devcap.bandwidth);
			break;
		case DEVCAP_DEV_FEATURE_FLAG:
			if ((value & MHL_FEATURE_RCP_SUPPORT) == 0)
				pr_debug("sii9234: FEATURE_FLAG=RCP");

			if ((value & MHL_FEATURE_RAP_SUPPORT) == 0)
				pr_debug("sii9234: FEATURE_FLAG=RAP\n");

			if ((value & MHL_FEATURE_SP_SUPPORT) == 0)
				pr_debug("sii9234: FEATURE_FLAG=SP\n");
			break;
		case DEVCAP_DEVICE_ID_H:
			sii9234->devcap.device_id = (value & 0xFF) << 0x8;
			pr_info("sii9234: DEVICE_ID_H=0x%x\n", value);
			offset = DEVCAP_DEVICE_ID_L;
			break;
		case DEVCAP_DEVICE_ID_L:
			sii9234->devcap.device_id |= value & 0xFF;
			pr_info("sii9234: DEVICE_ID_L=0x%x\n", value);
			break;
		case DEVCAP_SCRATCHPAD_SIZE:
			sii9234->devcap.scratchpad_size = value;
			pr_debug("sii9234: DEVCAP_SCRATCHPAD_SIZE =%d\n",
				 sii9234->devcap.scratchpad_size);
			break;
		case DEVCAP_INT_STAT_SIZE:
			sii9234->devcap.int_stat_size = value;
			pr_debug("sii9234: DEVCAP_INT_STAT_SIZE =%d\n",
				 sii9234->devcap.int_stat_size);
			break;
		case DEVCAP_RESERVED:
			pr_info("sii9234: DEVCAP_RESERVED : %d\n", value);
			break;
		case DEVCAP_DEV_STATE:
			pr_debug("sii9234: DEVCAP_DEV_STATE\n");
			break;
		default:
			pr_debug("sii9234: DEVCAP DEFAULT\n");
			break;
		}

		break;
	default:
		pr_debug("sii9234: error: cbus_command_response"
			 "cannot handle...\n");
	}

	sii9234->cbus_pkt.command = CBUS_IDLE;
	sii9234_cbus_mutex_unlock(&sii9234->cbus_lock);

	if (offset)
		cbus_command_request(sii9234, CBUS_READ_DEVCAP, offset, 0x00);
}

#ifdef DEBUG_MHL
static void cbus_command_response_dbg_msg(struct sii9234_data *sii9234,
					  u8 index)
{
	/*Added to debugcbus_pkt_buf */
	pr_info("sii9234: cbus_pkt_buf[index].command = %d,"
		"sii9234->cbus_pkt.command = %d\n",
		sii9234->cbus_pkt_buf[index].command,
		sii9234->cbus_pkt.command);
	pr_info("sii9234: cbus_pkt_buf[index].data[0] = %d,"
		"sii9234->cbus_pkt.data[0] =  %d\n",
		sii9234->cbus_pkt_buf[index].data[0],
		sii9234->cbus_pkt.data[0]);

	pr_info("sii9234: cbus_pkt_buf[index].data[1] = %d,"
		"sii9234->cbus_pkt.data[1] = %d\n",
		sii9234->cbus_pkt_buf[index].data[1],
		sii9234->cbus_pkt.data[1]);
	pr_info("sii9234: cbus_pkt_buf[index].offset = %d,"
		"sii9234->cbus_pkt.offset = %d\n",
		sii9234->cbus_pkt_buf[index].offset, sii9234->cbus_pkt.offset);
}
#endif

static void cbus_command_response_all(struct sii9234_data *sii9234)
{
	u8 index;
	struct cbus_packet cbus_pkt_process_buf[CBUS_PKT_BUF_COUNT];

	/*take bkp of cbus_pkt_buf */
	memcpy(cbus_pkt_process_buf, sii9234->cbus_pkt_buf,
	       sizeof(cbus_pkt_process_buf));

	/*clear cbus_pkt_buf  to hold next request */
	memset(sii9234->cbus_pkt_buf, 0x00, sizeof(sii9234->cbus_pkt_buf));

	/*process all previous requests */
	for (index = 0; index < CBUS_PKT_BUF_COUNT; index++) {
		if (cbus_pkt_process_buf[index].status == true) {
			memcpy(&sii9234->cbus_pkt, &cbus_pkt_process_buf[index],
			       sizeof(struct cbus_packet));
			cbus_command_response(sii9234);
#ifdef DEBUG_MHL
			/*print cbus_cmd messg */
			cbus_command_response_dbg_msg(sii9234, index);
#endif
		}
	}
}

static bool cbus_command_request(struct sii9234_data *sii9234,
				 enum cbus_command command, u8 offset, u8 data)
{
	u8 start_bit = 0;

	sii9234_cbus_mutex_lock(&sii9234->cbus_lock);
	if (sii9234->state != STATE_ESTABLISHED) {
		pr_debug("sii9234: cbus_command_request without establish\n");
		pr_debug("sii9234: ==> command:0x%X, offset:0x%X",
			 command, offset);
		sii9234_cbus_mutex_unlock(&sii9234->cbus_lock);
		return -EINVAL;
	}

	sii9234->cbus_pkt.command = command;
	sii9234->cbus_pkt.offset = offset;

	if (command == CBUS_MSC_MSG)
		sii9234->cbus_pkt.data[0] = offset;
	else
		sii9234->cbus_pkt.data[0] = data;

	pr_debug("sii9234: cbus_command_request Sending MSC_MSG SubCommand=%d"
		 ",key-code=%d\n", sii9234->cbus_pkt.offset,
		 sii9234->cbus_pkt.data[0]);

	cbus_write_reg(sii9234, CBUS_MSC_OFFSET_REG, sii9234->cbus_pkt.offset);
	cbus_write_reg(sii9234, CBUS_MSC_FIRST_DATA_OUT_REG,
		       sii9234->cbus_pkt.data[0]);

	switch (sii9234->cbus_pkt.command) {
	case CBUS_SET_INT:
		pr_debug("sii9234: cbus_command_request" "CBUS_SET_INT\n");
		start_bit = START_BIT_WRITE_STAT_INT;
		break;
	case CBUS_WRITE_STAT:
		pr_debug("sii9234: cbus_command_request" "CBUS_WRITE_STAT\n");
		start_bit = START_BIT_WRITE_STAT_INT;
		break;
	case CBUS_MSC_MSG:
		/*treat offset as data[0] in case of CBUS_MSC_MSG */
		sii9234->cbus_pkt.data[0] = offset;
		sii9234->cbus_pkt.data[1] = data;
		pr_debug("sii9234: cbus_command_request CBUS_MSC_MSG"
			 "SubCommand=%d,key-code=%d\n",
			 sii9234->cbus_pkt.data[0], sii9234->cbus_pkt.data[1]);

		cbus_write_reg(sii9234, CBUS_MSC_SECOND_DATA_OUT_REG,
			       sii9234->cbus_pkt.data[1]);
		cbus_write_reg(sii9234, CBUS_MSC_OFFSET_REG,
			       sii9234->cbus_pkt.command);
		start_bit = START_BIT_MSC_MSG;
		break;
	case CBUS_READ_DEVCAP:
		pr_debug("sii9234: cbus_command_request CBUS_READ_DEVCAP\n");
		start_bit = START_BIT_READ_DEVCAP;
		break;
	case CBUS_WRITE_BURST:
		pr_debug("sii9234: cbus_command_request CBUS_WRITE_BURST\n");
		start_bit = START_BIT_WRITE_BURST;
		break;
	case CBUS_GET_STATE:
	case CBUS_GET_VENDOR_ID:
	case CBUS_SET_HPD:
	case CBUS_CLR_HPD:
	case CBUS_GET_MSC_ERR_CODE:
	case CBUS_GET_SC3_ERR_CODE:
	case CBUS_GET_SC1_ERR_CODE:
	case CBUS_GET_DDC_ERR_CODE:
		cbus_write_reg(sii9234, CBUS_MSC_OFFSET_REG,
			       sii9234->cbus_pkt.command);
		start_bit = START_BIT_MSC_RESERVED;
		break;
	default:
		pr_debug("sii9234: error send cbus command fail\n");
		sii9234_cbus_mutex_unlock(&sii9234->cbus_lock);
		return false;
	}

	pr_debug("sii9234: startbit = %d\n", start_bit);
	cbus_write_reg(sii9234, CBUS_MSC_COMMAND_START_REG, start_bit);
	save_cbus_pkt_to_buffer(sii9234);
	sii9234_cbus_mutex_unlock(&sii9234->cbus_lock);

	return true;
}

#ifdef __CONFIG_TMDS_OFFON_WORKAROUND__
static u8 sii9234_tmds_control(struct sii9234_data *sii9234, bool enable)
{
	int ret;

	if (sii9234->tmds_state == enable) {
		if (enable) {
			pr_debug("sii9234: MHL HPD High, already enabled TMDS\n");
			ret = mhl_tx_set_reg(sii9234, MHL_TX_INT_CTRL_REG,
					(1 << 4) | (1 << 5));
			if (ret < 0)
				return ret;
		} else {
			pr_debug("sii9234 MHL HPD low, already disabled TMDS\n");
			ret = mhl_tx_clear_reg(sii9234, MHL_TX_INT_CTRL_REG,
					(1 << 4) | (1 << 5));
			if (ret < 0)
				return ret;
		}
		return ret;
	} else {
		sii9234->tmds_state = enable;
	}

	if (enable) {
#ifdef	__CONFIG_RSEN_LOST_PATCH__
		ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL2_REG, 0xFC);
		if (ret < 0)
			return ret;
#endif
		ret = mhl_tx_set_reg(sii9234, MHL_TX_TMDS_CCTRL, (1 << 4));
		if (ret < 0)
			return ret;
		pr_debug("sii9234: MHL HPD High, enabled TMDS\n");
		ret = mhl_tx_set_reg(sii9234, MHL_TX_INT_CTRL_REG,
				     (1 << 4) | (1 << 5));
		if (ret < 0)
			return ret;
	} else {
		ret = mhl_tx_clear_reg(sii9234, MHL_TX_TMDS_CCTRL, (1 << 4));
		if (ret < 0)
			return ret;
		pr_debug("sii9234 MHL HPD low, disabled TMDS\n");
		ret = mhl_tx_clear_reg(sii9234, MHL_TX_INT_CTRL_REG,
				       (1 << 4) | (1 << 5));
		if (ret < 0)
			return ret;
#ifdef	__CONFIG_RSEN_LOST_PATCH__
		ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL2_REG, 0xC0);
		if (ret < 0)
			return ret;
#endif
	}

	return ret;
}

static u8 sii9234_tmds_control2(struct sii9234_data *sii9234, bool enable)
{
	int ret;

	if (sii9234->tmds_state == enable) {
		pr_debug("%s(): already %s TMDS!!\n", __func__,
				enable ? "enabled" : "disabled");
		return 0;
	} else {
		sii9234->tmds_state = enable;
	}

	if (enable) {
#ifdef	__CONFIG_RSEN_LOST_PATCH__
		ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL2_REG, 0xFC);
		if (ret < 0)
			return ret;
#endif
		ret = mhl_tx_set_reg(sii9234, MHL_TX_TMDS_CCTRL, (1 << 4));
		if (ret < 0)
			return ret;
		pr_debug("sii9234: enabled TMDS\n");
	} else {
		ret = mhl_tx_clear_reg(sii9234, MHL_TX_TMDS_CCTRL, (1 << 4));
		if (ret < 0)
			return ret;
		pr_debug("sii9234: disabled TMDS\n");
#ifdef	__CONFIG_RSEN_LOST_PATCH__
		ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL2_REG, 0xC0);
		if (ret < 0)
			return ret;
#endif
	}

	return ret;
}
#else
static u8 sii9234_tmds_control(struct sii9234_data *sii9234, bool enable)
{
	int ret;

	if (enable) {
#ifdef	__CONFIG_RSEN_LOST_PATCH__
		ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL2_REG, 0xFC);
		if (ret < 0)
			return ret;
#endif
		ret = mhl_tx_set_reg(sii9234, MHL_TX_TMDS_CCTRL, (1 << 4));
		if (ret < 0)
			return ret;
		pr_debug("sii9234: MHL HPD High, enabled TMDS\n");
		ret = mhl_tx_set_reg(sii9234, MHL_TX_INT_CTRL_REG,
				     (1 << 4) | (1 << 5));
		if (ret < 0)
			return ret;
	} else {
		ret = mhl_tx_clear_reg(sii9234, MHL_TX_TMDS_CCTRL, (1 << 4));
		if (ret < 0)
			return ret;
		pr_debug("sii9234 MHL HPD low, disabled TMDS\n");
		ret = mhl_tx_clear_reg(sii9234, MHL_TX_INT_CTRL_REG,
				       (1 << 4) | (1 << 5));
		if (ret < 0)
			return ret;
#ifdef	__CONFIG_RSEN_LOST_PATCH__
		ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL2_REG, 0xC0);
		if (ret < 0)
			return ret;
#endif
	}

	return ret;
}
#endif

static irqreturn_t sii9234_irq_thread(int irq, void *data)
{
	struct sii9234_data *sii9234 = data;
	int ret;
	u8 intr1, intr4, value;
	u8 intr1_en, intr4_en;
	u8 cbus_intr1, cbus_intr2;
	u8 mhl_poweroff = 0;
	void (*cbus_resp_callback) (struct sii9234_data *) = NULL;

	if (!sii9234) {
		pr_err("[ERROR] %s: sii9234 is NULL & skipp this rutine\n",
		       __func__);
		return IRQ_HANDLED;
	}

	msleep(30);

	sii9234_mutex_lock(&sii9234->lock);

	ret = mhl_tx_read_reg(sii9234, MHL_TX_INTR1_REG, &intr1);
	if (ret < 0) {
		printk(KERN_ERR
		       "[ERROR] %s():%d read MHL_TX_INTR1_REG failed !\n",
		       __func__, __LINE__);
		goto i2c_error_exit;
	}
	ret = mhl_tx_read_reg(sii9234, MHL_TX_INTR4_REG, &intr4);
	if (ret < 0) {
		printk(KERN_ERR
		       "[ERROR] %s():%d read MHL_TX_INTR4_REG failed !\n",
		       __func__, __LINE__);
		goto i2c_error_exit;
	}

	ret = mhl_tx_read_reg(sii9234, MHL_TX_INTR1_ENABLE_REG, &intr1_en);
	if (ret < 0) {
		printk(KERN_ERR
		       "[ERROR] %s():%d read MHL_TX_INTR1_ENABLE_REG failed !\n",
		       __func__, __LINE__);
		goto i2c_error_exit;
	}
	ret = mhl_tx_read_reg(sii9234, MHL_TX_INTR4_ENABLE_REG, &intr4_en);
	if (ret < 0) {
		printk(KERN_ERR
		       "[ERROR] %s():%d read MHL_TX_INTR4_ENABLE_REG failed !\n",
		       __func__, __LINE__);
		goto i2c_error_exit;
	}

	ret = cbus_read_reg(sii9234, CBUS_INT_STATUS_1_REG, &cbus_intr1);
	if (ret < 0) {
		printk(KERN_ERR
		       "[ERROR] %s():%d read CBUS_INT_STATUS_1_REG failed !\n",
		       __func__, __LINE__);
		goto i2c_error_exit;
	}
	ret = cbus_read_reg(sii9234, CBUS_INT_STATUS_2_REG, &cbus_intr2);
	if (ret < 0) {
		printk(KERN_ERR
		       "[ERROR] %s():%d read CBUS_INT_STATUS_2_REG failed !\n",
		       __func__, __LINE__);
		goto i2c_error_exit;
	}

	pr_debug("sii9234: irq %02x/%02x %02x/%02x %02x/%02x\n",
		 intr1, intr1_en, intr4, intr4_en, cbus_intr1, cbus_intr2);

#ifdef __CONFIG_TMDS_OFFON_WORKAROUND__
	if (intr4 & (1 << 0)) {
		ret = mhl_tx_read_reg(sii9234, 0x81, &value);
		if (ret < 0) {
			pr_err("[ERROR] %s() read 0x81\n", __func__);
			goto i2c_error_exit;
		} else
			pr_debug("%s() 0x81 = 0x%x\n", __func__, value);

		queue_work(sii9234_tmds_offon_wq, &(sii9234->tmds_offon_work));
	}
#endif

	if (intr4 & RGND_READY_INT) {
		if (sii9234_callback_sched == 0) {
			pr_debug("%s() RGND_READY_INT\n", __func__);

			sii9234_disable_irq();

			if (sii9234->pdata->hw_reset)
				sii9234->pdata->hw_reset();

			schedule_work(&sii9234->rgnd_work);
			goto err_exit;
		}
		ret = mhl_tx_read_reg(sii9234, MHL_TX_STAT2_REG, &value);
		if (ret < 0) {
			dev_err(&sii9234->pdata->mhl_tx_client->dev,
				"STAT2 reg, err %d\n", ret);
			goto err_exit;
		}

		switch (value & RGND_INTP_MASK) {
		case RGND_INTP_OPEN:
			pr_debug("sii9234: RGND Open\n");
			sii9234->rgnd = RGND_OPEN;
			break;
		case RGND_INTP_1K:
			pr_debug("sii9234: RGND 1K!!\n");
			/* After applying RGND patch, there is some issue
			   about discovry failure
			   This point is add to fix that problem */
			ret = mhl_tx_set_reg(sii9234, MHL_TX_DISC_CTRL4_REG,
					     0x8C);
			if (ret < 0) {
				printk(KERN_ERR "[ERROR] %s():%d write "
					"MHL_TX_DISC_CTRL4_REG failed !\n",
					__func__, __LINE__);
				goto i2c_error_exit;
			}
			ret = mhl_tx_write_reg(sii9234,
					       MHL_TX_DISC_CTRL5_REG, 0x77);
			if (ret < 0) {
				printk(KERN_ERR "[ERROR] %s():%d write"
					" MHL_TX_DISC_CTRL5_REG failed !\n",
					__func__, __LINE__);
				goto i2c_error_exit;
			}

			ret = mhl_tx_set_reg(sii9234,
					     MHL_TX_DISC_CTRL6_REG, 0x05);
			if (ret < 0) {
				printk(KERN_ERR "[ERROR] %s():%d write "
					"MHL_TX_DISC_CTRL6_REG failed !\n",
					__func__, __LINE__);
				goto i2c_error_exit;
			}
			usleep_range(T_SRC_VBUS_CBUS_TO_STABLE * USEC_PER_MSEC,
				     T_SRC_VBUS_CBUS_TO_STABLE * USEC_PER_MSEC);
			/* end of this */
			pr_debug("sii9234: %s() send wake up pulse\n",
				 __func__);
			ret = mhl_send_wake_pulses(sii9234);
			if (ret < 0) {
				pr_err("[ERROR] sii9234: "
						"sending wake pulses error\n");
				goto err_exit;
			}
			sii9234->rgnd = RGND_1K;
			break;

		case RGND_INTP_2K:
			pr_debug("sii9234: RGND 2K\n");
			sii9234->rgnd = RGND_2K;
			break;
		case RGND_INTP_SHORT:
			pr_debug("sii9234: RGND Short\n");
			sii9234->rgnd = RGND_SHORT;
			break;
		};

		if (sii9234->rgnd != RGND_1K) {
			mhl_poweroff = 1;	/*Power down mhl chip */
			pr_debug("sii9234: RGND is not 1k\n");
			sii9234->state = STATE_DISCOVERY_FAILED;
			goto err_exit;
		}
	}

	if (intr4 & CBUS_LKOUT_INT) {	/* 1<<4 */
		pr_debug("%s(): CBUS Lockout Interrupt\n", __func__);
		sii9234->state = STATE_CBUS_LOCKOUT;
	}

	if (intr4 & MHL_DISC_FAIL_INT) {	/* 1<<3 */
		printk(KERN_ERR "[ERROR] %s(): MHL_DISC_FAIL_INT\n", __func__);
		sii9234->state = STATE_DISCOVERY_FAILED;
		goto err_exit;
	}

	if (intr4 & MHL_EST_INT) {	/* 1<<2 */
		pr_debug("%s(): mhl est interrupt\n", __func__);

		/* discovery override */
		ret = mhl_tx_write_reg(sii9234, MHL_TX_MHLTX_CTL1_REG, 0x10);
		if (ret < 0) {
			printk(KERN_ERR
			       "[ERROR] %s():%d write MHL_TX_MHLTX_CTRL1_REG failed !\n",
			       __func__, __LINE__);
			goto i2c_error_exit;
		}

		/* increase DDC translation layer timer (byte mode) */
		ret = cbus_write_reg(sii9234, 0x07, 0x32);
		if (ret < 0) {
			printk(KERN_ERR
			       "[ERROR] %s():%d cbus_write_reg failed !\n",
			       __func__, __LINE__);
			goto i2c_error_exit;
		}
		ret = cbus_set_reg(sii9234, 0x44, 1 << 1);
		if (ret < 0) {
			printk(KERN_ERR
			       "[ERROR] %s():%d cbus_set_reg failed !\n",
			       __func__, __LINE__);
			goto i2c_error_exit;
		}

		/* Keep the discovery enabled. Need RGND interrupt */
		ret = mhl_tx_set_reg(sii9234, MHL_TX_DISC_CTRL1_REG, (1 << 0));
		if (ret < 0) {
			printk(KERN_ERR
			       "[ERROR] %s():%d mhl_tx_set_reg failed !\n",
			       __func__, __LINE__);
			goto i2c_error_exit;
		}

		sii9234->state = STATE_ESTABLISHED;

#ifndef __MHL_NEW_CBUS_MSC_CMD__
		cbus_command_request(sii9234, CBUS_SET_INT,
				     MHL_RCHANGE_INT, MHL_INT_DCAP_CHG);
#else
		sii9234_enqueue_msc_work(sii9234, CBUS_SET_INT,
					 MHL_RCHANGE_INT, MHL_INT_DCAP_CHG,
					 0x0);
#endif

		ret = mhl_tx_write_reg(sii9234, MHL_TX_INTR1_ENABLE_REG,
				       RSEN_CHANGE_INT_MASK |
				       HPD_CHANGE_INT_MASK);
	}

	if (intr1 & HPD_CHANGE_INT) {	/* 1<<6 */
		ret = cbus_read_reg(sii9234, MSC_REQ_ABORT_REASON_REG, &value);
		if (ret < 0) {
			printk(KERN_ERR
			       "[ERROR] %s():%d cbus_read_reg failed !\n",
			       __func__, __LINE__);
			goto i2c_error_exit;
		}

		if (value & SET_HPD_DOWNSTREAM) {
			pr_info("%s() hpd high\n", __func__);
			/* Downstream HPD High */

			/* Do we need to send HPD upstream using
			 * Register 0x79(page0)? Is HPD need to be overriden??
			 *      TODO: See if we need code for overriding HPD OUT
			 *      as per Page 0,0x79 Register
			 */
			sii9234->mhl_status_value.sink_hpd = true;
#ifdef __CONFIG_USE_TIMER__
			if (cbus_command_abort_state == 1) {
				pr_debug("cbus_command_mod_timer\n");
				mod_timer(&sii9234->cbus_command_timer,
					  jiffies + 2 * HZ);
				cbus_command_abort_state = 0;
			} else
#endif
#ifndef __MHL_NEW_CBUS_MSC_CMD__
				cbus_command_request(sii9234, CBUS_WRITE_STAT,
						CBUS_LINK_CONTROL_2_REG,
						sii9234->mhl_status_value.
						linkmode);
#else
				sii9234_enqueue_msc_work(sii9234,
						CBUS_WRITE_STAT,
						CBUS_LINK_CONTROL_2_REG,
						sii9234->
						mhl_status_value.
						linkmode, 0x0);
#endif
			/* Enable TMDS */
			sii9234_tmds_control(sii9234, true);
			/*turn on&off hpd festure for only QCT HDMI */
#ifdef CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE
			mhl_hpd_handler(true);
#endif

		} else {
			pr_info("sii9234: hpd low\n");
			/*Downstream HPD Low */

			/* Similar to above comments.
			 * TODO:Do we need to override HPD OUT value
			 * and do we need to disable TMDS here?
			 */
			sii9234->mhl_status_value.sink_hpd = false;
			/* Disable TMDS */
			sii9234_tmds_control(sii9234, false);
		}
	}

	if (intr1 & RSEN_CHANGE_INT) {
		/* work_around code to handle worng interrupt */
		if (sii9234->rgnd != RGND_1K) {
			pr_err("[ERROR] sii9234: Err RSEN_HIGH"
			       " without RGND_1K rgnd=%d\n", sii9234->rgnd);
			goto err_exit2;
		}
		ret = mhl_tx_read_reg(sii9234, MHL_TX_SYSSTAT_REG, &value);
		if (ret < 0) {
			pr_err("[ERROR] sii9234: "
					"MHL_TX_SYSSTAT_REG read error\n");
			goto err_exit2;
		}
		sii9234->rsen = value & RSEN_STATUS;

		if (value & RSEN_STATUS) {
			pr_info("%s(): MHL cable connected.. RSEN High\n",
				__func__);
		} else {
			pr_info("%s(): RSEN lost -\n", __func__);
			/* Once RSEN loss is confirmed,we need to check
			 * based on cable status and chip power status,whether
			 * it is SINK Loss(HDMI cable not connected, TV Off)
			 * or MHL cable disconnection
			 * TODO: Define the below mhl_disconnection()
			 */
#ifdef __CONFIG_USE_TIMER__
			del_timer(&sii9234->cbus_command_timer);
#endif
			msleep(T_SRC_RXSENSE_DEGLITCH);
			ret = mhl_tx_read_reg(sii9234, MHL_TX_SYSSTAT_REG,
					      &value);
			if (ret < 0) {
				printk(KERN_ERR
				       "[ERROR] %s() read MHL_TX_SYSSTAT_REG\n",
				       __func__);
				goto i2c_error_exit;
			}
			pr_info("sii9234:  sys_stat: %x\n", value);

			if ((value & RSEN_STATUS) == 0) {
				printk(KERN_INFO
				       "%s() RSEN Really LOW ~\n", __func__);
				/*To meet CTS 3.3.22.2 spec */
				sii9234_tmds_control(sii9234, false);
				force_usb_id_switch_open(sii9234);
				release_usb_id_switch_open(sii9234);
				mhl_poweroff = 1;	/*Power down mhl chip */
				goto err_exit;
			} else
				pr_info("sii9234: RSEN recovery ~\n");
		}
	}

	/*
	 * Process CBUS interrupts only when MHL connection has been
	 * established
	 */
	if (sii9234->state == STATE_ESTABLISHED) {

		if (cbus_intr1 & MSC_RESP_ABORT)
			cbus_resp_abort_error(sii9234);

		if (cbus_intr1 & MSC_REQ_ABORT) {
#ifdef __CONFIG_USE_TIMER__
			cbus_write_reg(sii9234, CBUS_INTR1_ENABLE_REG, 0);
			cbus_req_abort_error(sii9234);
			cbus_write_reg(sii9234, CBUS_INTR1_ENABLE_REG, 0xFF);
			cbus_command_abort_state = 1;
#else
			cbus_req_abort_error(sii9234);
#endif
		}
		if ((cbus_intr1 & CBUS_DDC_ABORT) ||
		    (cbus_intr1 & MSC_RESP_ABORT)) {
			if (cbus_intr1 & CBUS_DDC_ABORT)
				pr_debug("sii9234: CBUS DDC abort\n");
			if (cbus_ddc_abort_error(sii9234))
				schedule_work(&sii9234->mhl_end_work);
		}

		if (cbus_intr1 & MSC_REQ_DONE) {
			pr_debug("sii9234: CBUS cmd ACK Received\n");
#ifndef __MHL_NEW_CBUS_MSC_CMD__
			cbus_resp_callback = cbus_command_response_all;
#else
			complete(&sii9234->msc_complete);
#endif
		}
#ifdef CONFIG_SII9234_RCP
		if (cbus_intr1 & MSC_MSG_RECD) {
			pr_debug("sii9234: MSC MSG Received\n");
			cbus_handle_msc_msg(sii9234);
		}
#endif

		/* ignore WRT_STAT_RECD interrupt when we get HPD CHANGE */
		if (cbus_intr2 & WRT_STAT_RECD && intr1 == 0)
			cbus_handle_wrt_stat_recd(sii9234);

		if (cbus_intr2 & SET_INT_RECD)
			cbus_handle_set_int_recd(sii9234);
	}

 err_exit:
	pr_debug("sii9234: wake_up\n");
	wake_up(&sii9234->wq);
 err_exit2:
	ret = mhl_tx_write_reg(sii9234, MHL_TX_INTR1_REG, intr1);
	if (ret < 0)
		goto i2c_error_exit;
	ret = mhl_tx_write_reg(sii9234, MHL_TX_INTR4_REG, intr4);
	if (ret < 0)
		goto i2c_error_exit;

	ret = cbus_write_reg(sii9234, CBUS_INT_STATUS_1_REG, cbus_intr1);
	if (ret < 0)
		goto i2c_error_exit;
	ret = cbus_write_reg(sii9234, CBUS_INT_STATUS_2_REG, cbus_intr2);
	if (ret < 0)
		goto i2c_error_exit;

	sii9234_mutex_unlock(&sii9234->lock);

	if (cbus_resp_callback)
		cbus_resp_callback(sii9234);

	if (mhl_poweroff) {
		if (sii9234_callback_sched != 0) {
			sii9234_disable_irq();
			if (sii9234->pdata->factory_test == 0) {
				schedule_work(&sii9234->mhl_d3_work);
				pr_info("%s() normal goto_d3\n", __func__);
			} else {
				pr_info("%s() skip goto_d3\n", __func__);
				mhl_onoff_ex(0);
			}
		}
	}
	return IRQ_HANDLED;

 i2c_error_exit:
	sii9234_mutex_unlock(&sii9234->lock);
	pr_info("%s(): i2c error exit\n", __func__);
	pr_debug("sii9234: wake_up\n");
	wake_up(&sii9234->wq);
	return IRQ_HANDLED;
}

static void mhl_cbus_command_timer(unsigned long data)
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);
	schedule_work(&sii9234->mhl_cbus_write_stat_work);
}

#ifdef __CONFIG_SS_FACTORY__
#define SII_ID 0x92
static ssize_t sysfs_check_mhl_command(struct class *class,
				       struct class_attribute *attr, char *buf)
{
	int size;
	u8 sii_id = 0;
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);

	if (sii9234->pdata->hw_onoff)
		sii9234->pdata->hw_onoff(1);

	if (sii9234->pdata->hw_reset)
		sii9234->pdata->hw_reset();

	mhl_tx_read_reg(sii9234, MHL_TX_IDH_REG, &sii_id);
	pr_info("sii9234 : sel_show sii_id: %X\n", sii_id);

	if (sii9234->pdata->hw_onoff)
		sii9234->pdata->hw_onoff(0);

	size = snprintf(buf, 10, "%d\n", sii_id == SII_ID ? 1 : 0);

	return size;
}

static ssize_t sysfs_check_factory_store(struct class *class,
		struct class_attribute *attr, const char *buf, size_t size)
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);
	const char *p = buf;
	u8 value = 0;

	if (p[0] == '1')
		value = 1;
	else
		value = 0;

	sii9234->pdata->factory_test = value;

	if (sii9234->pdata->factory_test == 1)
		pr_info("sii9234: in factory mode\n");
	else
		pr_info("sii9234: not factory mode\n");

	return size;

}

static CLASS_ATTR(test_result, 0664, sysfs_check_mhl_command,
		sysfs_check_factory_store);
#endif /*__CONFIG_SS_FACTORY__*/

static ssize_t sysfs_mhl_read_reg_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	pr_info("sii9234: %s()\n", __func__);
	return sprintf(buf, "sysfs_mhl_read_reg_show\n");
}

static ssize_t sysfs_mhl_read_reg_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct sii9234_data *sii9234 = dev_get_drvdata(sii9244_mhldev);
	enum page_num pn;
	unsigned int offset;
	int ret;
	u8 value = 0;
	char dest[10];
	char *buffer = (char *)buf;
	char *token;

	if (size > 10) {
		pr_info("sii9234: Error : Unsupported format\n");
		return size;
	}

	if (strnicmp(buf, "mhl", 3) == 0) {
		printk(KERN_INFO "sii9234: %s() - mhl start\n", __func__);
		schedule_work(&sii9234->mhl_restart_work);
	}

	token = strsep(&buffer, ":");
	if (token != NULL)
		strcpy(dest, token);
	else {
		pr_info("sii9234: Error: command parsing error\n");
		return size;
	}

	ret = kstrtouint(dest, 0, &offset);
	if (ret != 0) {
		pr_info("sii9234: Error : Page number\n");
		return size;
	}
	pn = (enum page_num)offset;

	strcpy(dest, buffer);
	ret = kstrtouint(dest, 0, &offset);
	if (ret || offset > 0xff) {
		pr_info("sii9234: Error : Offset number\n");
		return size;
	}

	switch (pn) {
	case PAGE0:
		mhl_tx_read_reg(sii9234, offset, &value);
		break;
	case PAGE1:
		tpi_read_reg(sii9234, offset, &value);
		break;
	case PAGE2:
		hdmi_rx_read_reg(sii9234, offset, &value);
		break;
	case PAGE3:
		cbus_read_reg(sii9234, offset, &value);
		break;
	default:
		pr_info("\nsii9234: Error : Out of the page number range\n");
		return size;
	}
	pr_info("sii9234: MHL register Page%d:0x%02x = 0x%02x\n", pn, offset,
		value);

	return size;
}

static DEVICE_ATTR(mhl_read_reg, S_IRUGO | S_IWUSR,
		   sysfs_mhl_read_reg_show, sysfs_mhl_read_reg_store);

#ifdef __CONFIG_MHL_DEBUG__
module_param_named(mhl_dbg_flag, mhl_dbg_flag, uint, 0644);
#endif

#ifdef CONFIG_SLP
#ifdef CONFIG_PM_SLEEP
static int sii9234_mhl_tx_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sii9234_data *sii9234 = i2c_get_clientdata(client);

	if (!sii9234 || !sii9234->pdata)
		return -EFAULT;

	sii9234_mutex_lock(&sii9234->lock);

	if (!sii9234->pdata->power_state)
		goto out;

	sii9234->pdata->power_state = 0;

	if (sii9234->pdata->mhl_sel)
		sii9234->pdata->mhl_sel(0);

	sii9234_power_down(sii9234);

	if (sii9234->pdata->hw_onoff)
		sii9234->pdata->hw_onoff(0);
 out:
	sii9234_mutex_unlock(&sii9234->lock);
	return 0;
}

static int sii9234_mhl_tx_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sii9234_data *sii9234 = i2c_get_clientdata(client);

	if (!sii9234 || !sii9234->pdata)
		return -EFAULT;

	sii9234_mutex_lock(&sii9234->lock);

#ifdef	CONFIG_SAMSUNG_USE_11PIN_CONNECTOR
	if (!is_mhl_cable_connected()) {
		sii9234_mutex_unlock(&sii9234->lock);
		return 0;
	}
#endif

	schedule_work(&sii9234->mhl_restart_work);
	sii9234_mutex_unlock(&sii9234->lock);

	return 0;
}

static const struct dev_pm_ops sii9234_pm_ops = {
	.suspend = sii9234_mhl_tx_suspend,
	.resume = sii9234_mhl_tx_resume,
};
#endif
#endif

#ifdef CONFIG_EXTCON
static void sii9234_extcon_work(struct work_struct *work)
{
	struct sii9234_data *sii9234 =
		container_of(work, struct sii9234_data, extcon_wq);

	pr_info("%s: MHL is %s\n", __func__,
		sii9234->extcon_attached ? "attached" : "detached");

	if (sii9234->extcon_attached) {
#ifdef CONFIG_JACK_MON
		jack_event_handler("hdmi", 1);
#endif
#ifdef CONFIG_SAMSUNG_MHL
#ifdef CONFIG_MACH_MIDAS
		sii9234_wake_lock();
#endif
		mhl_onoff_ex(1);
#endif

	} else {
#ifdef CONFIG_JACK_MON
		jack_event_handler("hdmi", 0);
#endif
#ifdef CONFIG_SAMSUNG_MHL
		mhl_onoff_ex(false);
#ifdef CONFIG_MACH_MIDAS
		sii9234_wake_unlock();
#endif
#endif
	}
}

static int sii9234_extcon_notifier(struct notifier_block *self,
			unsigned long event, void *ptr)
{
	struct sii9234_data *sii9234 =
		container_of(self, struct sii9234_data, extcon_nb);

	sii9234->extcon_attached = event;
	schedule_work(&sii9234->extcon_wq);

	return NOTIFY_DONE;
}
#endif


#ifdef CONFIG_HAS_EARLYSUSPEND
static void sii9234_early_suspend(struct early_suspend *early_sus)
{
	struct sii9234_data *sii9234 = container_of(early_sus,
			struct sii9234_data, early_suspend);

	pr_debug("%s()\n", __func__);
	if (!sii9234 || !sii9234->pdata)
		return;

	sii9234_mutex_lock(&sii9234->lock);
	sii9234->suspend_state = true;
	sii9234_mutex_unlock(&sii9234->lock);
}

static void sii9234_late_resume(struct early_suspend *early_sus)
{
	struct sii9234_data *sii9234 = container_of(early_sus,
			struct sii9234_data, early_suspend);

	pr_debug("%s()\n", __func__);
	if (!sii9234 || !sii9234->pdata)
		return;

	sii9234_mutex_lock(&sii9234->lock);
	sii9234->suspend_state = false;
	sii9234_mutex_unlock(&sii9234->lock);
}
#endif

static int __devinit sii9234_mhl_tx_i2c_probe(struct i2c_client *client,
					      const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct sii9234_data *sii9234;
#ifdef CONFIG_SII9234_RCP
	struct input_dev *input;
#endif
	int ret;
#if defined(__CONFIG_SS_FACTORY__) || defined(__CONFIG_MHL_SWING_LEVEL__)
	struct class *sec_mhl;
#endif
#ifdef __CONFIG_MHL_DEBUG__
	mhl_dbg_flag = 1;
#endif

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	sii9234 = kzalloc(sizeof(struct sii9234_data), GFP_KERNEL);
	if (!sii9234) {
		dev_err(&client->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}
#ifdef CONFIG_SII9234_RCP
	input = input_allocate_device();
	if (!input) {
		dev_err(&client->dev, "failed to allocate input device.\n");
		ret = -ENOMEM;
		goto err_exit0;
	}
#endif

	sii9234->pdata = client->dev.platform_data;
	if (!sii9234->pdata) {
		ret = -EINVAL;
		goto err_exit1;
	}
	sii9234->pdata->mhl_tx_client = client;
	sii9234->pdata->factory_test = 0;

	init_waitqueue_head(&sii9234->wq);
	mutex_init(&sii9234->lock);
	mutex_init(&sii9234_irq_lock);
	mutex_init(&sii9234->cbus_lock);

#ifdef __SII9234_MUTEX_DEBUG__
	g_mutex_cnt = 0;
	g_cbus_mutex_cnt = 0;
#endif
	INIT_WORK(&sii9234->mhl_restart_work, mhl_start_worker);

	INIT_WORK(&sii9234->mhl_end_work, mhl_end_worker);

	INIT_WORK(&sii9234->rgnd_work, sii9234_detection_callback_worker);

	INIT_WORK(&sii9234->mhl_d3_work, mhl_goto_d3_worker);

	INIT_WORK(&sii9234->mhl_cbus_write_stat_work,
		  mhl_cbus_write_stat_worker);

	sii9234->pdata->init();

	i2c_set_clientdata(client, sii9234);
	client->irq = gpio_to_irq(GPIO_MHL_INT);
	sii9244_mhldev = &client->dev;

#ifdef CONFIG_MACH_MIDAS
	wake_lock_init(&sii9234->mhl_wake_lock, WAKE_LOCK_SUSPEND,
		       "mhl_wake_lock");
	pr_debug("%s(): wake lock is initialized.\n", __func__);
#endif

#ifdef __SII9234_IRQ_DEBUG__
	en_irq = 0;
#endif
	if (sii9234->pdata->swing_level == 0)
		sii9234->pdata->swing_level = 0xEB;
#ifdef __MHL_NEW_CBUS_MSC_CMD__
	sii9234_msc_wq = create_singlethread_workqueue("sii9234_msc_wq");
	if (!sii9234_msc_wq) {
		printk(KERN_ERR
		       "[ERROR] %s() workqueue create fail\n", __func__);
		ret = -ENOMEM;
		goto	err_msc_wq;
	}
	INIT_WORK(&sii9234->msc_work, sii9234_process_msc_work);
#endif

#ifdef __CONFIG_TMDS_OFFON_WORKAROUND__
	sii9234_tmds_offon_wq =
		create_singlethread_workqueue("sii9234_tmds_offon_wq");
	if (!sii9234_tmds_offon_wq) {
		printk(KERN_ERR	"[ERROR] %s() tmds_offon"
				" workqueue create fail\n", __func__);
		ret = -ENOMEM;
		goto	err_tmds_offon_wq;
	}
	INIT_WORK(&sii9234->tmds_offon_work, sii9234_tmds_offon_work);
#endif

	ret = request_threaded_irq(client->irq, NULL, sii9234_irq_thread,
				   IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				   "sii9234", sii9234);
	if (ret < 0)
		goto err_exit_after_irq;

	atomic_set(&sii9234->is_irq_enabled, false);
	disable_irq(client->irq);

#if defined(__CONFIG_SS_FACTORY__) || defined(__CONFIG_MHL_SWING_LEVEL__)
	pr_info("sii9234 : create mhl sysfile\n");

	sec_mhl = class_create(THIS_MODULE, "mhl");
	if (IS_ERR(sec_mhl)) {
		pr_err("[ERROR] Failed to create class(sec_mhl)!\n");
		goto err_exit_after_irq;
	}
#endif

#ifdef __CONFIG_SS_FACTORY__
	ret = class_create_file(sec_mhl, &class_attr_test_result);
	if (ret) {
		pr_err("[ERROR] Failed to create "
				"device file in sysfs entries!\n");
		goto err_exit2a;
	}
#endif

#ifdef __CONFIG_MHL_SWING_LEVEL__
	pr_info("sii9234 : create mhl sysfile\n");

	ret = class_create_file(sec_mhl, &class_attr_swing);
	if (ret) {
		pr_err("[ERROR] failed to create swing sysfs file\n");
		goto err_exit2b;
	}
#endif

	sii9234->cbus_pkt.command = CBUS_IDLE;
	sii9234->cbus_pkt.offset = DEVCAP_DEV_STATE;
#ifdef __CONFIG_USE_TIMER__
	init_timer(&sii9234->cbus_command_timer);
	sii9234->cbus_command_timer.function = mhl_cbus_command_timer;
	sii9234->cbus_command_timer.data = (unsigned int)NULL;

	sii9234->cbus_command_timer.expires = 0xffffffffL;
	add_timer(&sii9234->cbus_command_timer);
#endif
#ifdef CONFIG_SII9234_RCP
	/* indicate that we generate key events */
	set_bit(EV_KEY, input->evbit);
	bitmap_fill(input->keybit, KEY_MAX);

	sii9234->input_dev = input;
	input_set_drvdata(input, sii9234);
	input->name = "sii9234_rcp";
	input->id.bustype = BUS_I2C;

	ret = input_register_device(input);
	if (ret < 0) {
		dev_err(&client->dev, "fail to register input device\n");
		goto err_exit2c;
	}
#endif
#ifdef CONFIG_SAMSUNG_MHL_9290
	sii9234->acc_con_nb.notifier_call = sii9234_30pin_callback;
	acc_register_notifier(&sii9234->acc_con_nb);

	sii9234_tmds_reset_wq =
		create_singlethread_workqueue("sii9234_tmds_reset_wq");
	if (!sii9234_tmds_reset_wq) {
		printk(KERN_ERR	"[ERROR] %s() tmds_reset"
				" workqueue create fail\n", __func__);
	}
	INIT_WORK(&sii9234->tmds_reset_work, sii9234_tmds_reset_work);
#endif

#ifdef CONFIG_EXTCON
	/* Extcon */
	INIT_WORK(&sii9234->extcon_wq, sii9234_extcon_work);
	sii9234->extcon_nb.notifier_call = sii9234_extcon_notifier;
	ret = extcon_register_interest(&sii9234->extcon_dev,
					sii9234->pdata->extcon_name,
					"MHL",
					&sii9234->extcon_nb);
	if (ret < 0) {
		pr_info("Cannot register extcon_dev for %s(cable: MHL).\n",
						sii9234->pdata->extcon_name);
		ret = -EINVAL;
		goto err_extcon;
	}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	sii9234->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 5;
	sii9234->early_suspend.suspend = sii9234_early_suspend;
	sii9234->early_suspend.resume = sii9234_late_resume;
	register_early_suspend(&sii9234->early_suspend);
	sii9234->suspend_state = false;
#endif
#ifdef __CONFIG_TMDS_OFFON_WORKAROUND__
	sii9234->tmds_state = 0;
#endif
#if defined(CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE) &&\
	!defined(CONFIG_SAMSUNG_MHL_9290)
	atomic_set(&sched_hpd_handler_false, 0);
#endif
	return 0;

#ifdef CONFIG_EXTCON
err_extcon:
	extcon_unregister_interest(&sii9234->extcon_dev);
#endif
 err_exit2c:
#ifdef __CONFIG_USE_TIMER__
	del_timer(&sii9234->cbus_command_timer);
#endif
#ifdef __CONFIG_MHL_SWING_LEVEL__
	class_remove_file(sec_mhl, &class_attr_swing);
#endif
 err_exit2b:
#ifdef __CONFIG_SS_FACTORY__
	class_remove_file(sec_mhl, &class_attr_test_result);
#endif
 err_exit2a:
#if defined(__CONFIG_SS_FACTORY__) || defined(__CONFIG_MHL_SWING_LEVEL__)
	class_destroy(sec_mhl);
#endif
err_exit_after_irq:
	free_irq(client->irq, sii9234);
#ifdef __CONFIG_TMDS_OFFON_WORKAROUND__
err_tmds_offon_wq:
	destroy_workqueue(sii9234_tmds_offon_wq);
#endif

#ifdef __MHL_NEW_CBUS_MSC_CMD__
err_msc_wq:
	destroy_workqueue(sii9234_msc_wq);
#endif

err_exit1:
#ifdef CONFIG_SII9234_RCP
	input_free_device(input);
#endif
err_exit0:
	kfree(sii9234);
	sii9234 = NULL;
	return ret;
}

static int __devinit sii9234_tpi_i2c_probe(struct i2c_client *client,
					   const struct i2c_device_id *id)
{
	struct sii9234_platform_data *pdata = client->dev.platform_data;
	if (!pdata)
		return -EINVAL;
	pdata->tpi_client = client;
	return 0;
}

static int __devinit sii9234_hdmi_rx_i2c_probe(struct i2c_client *client,
					       const struct i2c_device_id *id)
{
	struct sii9234_platform_data *pdata = client->dev.platform_data;
	if (!pdata)
		return -EINVAL;

	pdata->hdmi_rx_client = client;
	return 0;
}

static int __devinit sii9234_cbus_i2c_probe(struct i2c_client *client,
					    const struct i2c_device_id *id)
{
	struct sii9234_platform_data *pdata = client->dev.platform_data;
	if (!pdata)
		return -EINVAL;

	pdata->cbus_client = client;
	return 0;
}

static int __devexit sii9234_mhl_tx_remove(struct i2c_client *client)
{
	return 0;
}

static int __devexit sii9234_tpi_remove(struct i2c_client *client)
{
	return 0;
}

static int __devexit sii9234_hdmi_rx_remove(struct i2c_client *client)
{
	return 0;
}

static int __devexit sii9234_cbus_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id sii9234_mhl_tx_id[] = {
	{"sii9234_mhl_tx", 0},
	{}
};

static const struct i2c_device_id sii9234_tpi_id[] = {
	{"sii9234_tpi", 0},
	{}
};

static const struct i2c_device_id sii9234_hdmi_rx_id[] = {
	{"sii9234_hdmi_rx", 0},
	{}
};

static const struct i2c_device_id sii9234_cbus_id[] = {
	{"sii9234_cbus", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sii9234_mhl_tx_id);
MODULE_DEVICE_TABLE(i2c, sii9234_tpi_id);
MODULE_DEVICE_TABLE(i2c, sii9234_hdmi_rx_id);
MODULE_DEVICE_TABLE(i2c, sii9234_cbus_id);

static struct i2c_driver sii9234_mhl_tx_i2c_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "sii9234_mhl_tx",
#ifdef CONFIG_SLP
		   .pm = &sii9234_pm_ops,
#endif
		   },
	.id_table = sii9234_mhl_tx_id,
	.probe = sii9234_mhl_tx_i2c_probe,
	.remove = __devexit_p(sii9234_mhl_tx_remove),
	.command = NULL,
};

static struct i2c_driver sii9234_tpi_i2c_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "sii9234_tpi",
		   },
	.id_table = sii9234_tpi_id,
	.probe = sii9234_tpi_i2c_probe,
	.remove = __devexit_p(sii9234_tpi_remove),
};

static struct i2c_driver sii9234_hdmi_rx_i2c_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "sii9234_hdmi_rx",
		   },
	.id_table = sii9234_hdmi_rx_id,
	.probe = sii9234_hdmi_rx_i2c_probe,
	.remove = __devexit_p(sii9234_hdmi_rx_remove),
};

static struct i2c_driver sii9234_cbus_i2c_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "sii9234_cbus",
		   },
	.id_table = sii9234_cbus_id,
	.probe = sii9234_cbus_i2c_probe,
	.remove = __devexit_p(sii9234_cbus_remove),
};

static int __init sii9234_init(void)
{
	int ret;

	ret = i2c_add_driver(&sii9234_mhl_tx_i2c_driver);
	if (ret < 0)
		return ret;

	ret = i2c_add_driver(&sii9234_tpi_i2c_driver);
	if (ret < 0)
		goto err_exit1;

	ret = i2c_add_driver(&sii9234_hdmi_rx_i2c_driver);
	if (ret < 0)
		goto err_exit2;

	ret = i2c_add_driver(&sii9234_cbus_i2c_driver);
	if (ret < 0)
		goto err_exit3;

	return 0;

 err_exit3:
	i2c_del_driver(&sii9234_hdmi_rx_i2c_driver);
 err_exit2:
	i2c_del_driver(&sii9234_tpi_i2c_driver);
 err_exit1:
	i2c_del_driver(&sii9234_mhl_tx_i2c_driver);
	pr_err("[ERROR] i2c_add_driver fail\n");
	return ret;
}

static void __exit sii9234_exit(void)
{
	i2c_del_driver(&sii9234_cbus_i2c_driver);
	i2c_del_driver(&sii9234_hdmi_rx_i2c_driver);
	i2c_del_driver(&sii9234_tpi_i2c_driver);
	i2c_del_driver(&sii9234_mhl_tx_i2c_driver);
}

module_init(sii9234_init);
module_exit(sii9234_exit);
