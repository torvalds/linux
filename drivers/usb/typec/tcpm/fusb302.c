// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2016-2017 Google, Inc
 *
 * Fairchild FUSB302 Type-C Chip Driver
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/extcon.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/proc_fs.h>
#include <linux/regulator/consumer.h>
#include <linux/sched/clock.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/usb.h>
#include <linux/usb/typec.h>
#include <linux/usb/tcpm.h>
#include <linux/usb/pd.h>
#include <linux/workqueue.h>

#include "fusb302_reg.h"

/*
 * When the device is SNK, BC_LVL interrupt is used to monitor cc pins
 * for the current capability offered by the SRC. As FUSB302 chip fires
 * the BC_LVL interrupt on PD signalings, cc lvl should be handled after
 * a delay to avoid measuring on PD activities. The delay is slightly
 * longer than PD_T_PD_DEBPUNCE (10-20ms).
 */
#define T_BC_LVL_DEBOUNCE_DELAY_MS 30

enum toggling_mode {
	TOGGLING_MODE_OFF,
	TOGGLING_MODE_DRP,
	TOGGLING_MODE_SNK,
	TOGGLING_MODE_SRC,
};

enum src_current_status {
	SRC_CURRENT_DEFAULT,
	SRC_CURRENT_MEDIUM,
	SRC_CURRENT_HIGH,
};

static const u8 ra_mda_value[] = {
	[SRC_CURRENT_DEFAULT] = 4,	/* 210mV */
	[SRC_CURRENT_MEDIUM] = 9,	/* 420mV */
	[SRC_CURRENT_HIGH] = 18,	/* 798mV */
};

static const u8 rd_mda_value[] = {
	[SRC_CURRENT_DEFAULT] = 38,	/* 1638mV */
	[SRC_CURRENT_MEDIUM] = 38,	/* 1638mV */
	[SRC_CURRENT_HIGH] = 61,	/* 2604mV */
};

#define LOG_BUFFER_ENTRIES	1024
#define LOG_BUFFER_ENTRY_SIZE	128

struct fusb302_chip {
	struct device *dev;
	struct i2c_client *i2c_client;
	struct tcpm_port *tcpm_port;
	struct tcpc_dev tcpc_dev;

	struct regulator *vbus;

	spinlock_t irq_lock;
	struct work_struct irq_work;
	bool irq_suspended;
	bool irq_while_suspended;
	struct gpio_desc *gpio_int_n;
	int gpio_int_n_irq;
	struct extcon_dev *extcon;

	struct workqueue_struct *wq;
	struct delayed_work bc_lvl_handler;

	/* lock for sharing chip states */
	struct mutex lock;

	/* chip status */
	enum toggling_mode toggling_mode;
	enum src_current_status src_current_status;
	bool intr_togdone;
	bool intr_bc_lvl;
	bool intr_comp_chng;

	/* port status */
	bool vconn_on;
	bool vbus_on;
	bool charge_on;
	bool vbus_present;
	enum typec_cc_polarity cc_polarity;
	enum typec_cc_status cc1;
	enum typec_cc_status cc2;
	u32 snk_pdo[PDO_MAX_OBJECTS];

#ifdef CONFIG_DEBUG_FS
	struct dentry *dentry;
	/* lock for log buffer access */
	struct mutex logbuffer_lock;
	int logbuffer_head;
	int logbuffer_tail;
	u8 *logbuffer[LOG_BUFFER_ENTRIES];
#endif
};

/*
 * Logging
 */

#ifdef CONFIG_DEBUG_FS
static bool fusb302_log_full(struct fusb302_chip *chip)
{
	return chip->logbuffer_tail ==
		(chip->logbuffer_head + 1) % LOG_BUFFER_ENTRIES;
}

__printf(2, 0)
static void _fusb302_log(struct fusb302_chip *chip, const char *fmt,
			 va_list args)
{
	char tmpbuffer[LOG_BUFFER_ENTRY_SIZE];
	u64 ts_nsec = local_clock();
	unsigned long rem_nsec;

	if (!chip->logbuffer[chip->logbuffer_head]) {
		chip->logbuffer[chip->logbuffer_head] =
				kzalloc(LOG_BUFFER_ENTRY_SIZE, GFP_KERNEL);
		if (!chip->logbuffer[chip->logbuffer_head])
			return;
	}

	vsnprintf(tmpbuffer, sizeof(tmpbuffer), fmt, args);

	mutex_lock(&chip->logbuffer_lock);

	if (fusb302_log_full(chip)) {
		chip->logbuffer_head = max(chip->logbuffer_head - 1, 0);
		strscpy(tmpbuffer, "overflow", sizeof(tmpbuffer));
	}

	if (chip->logbuffer_head < 0 ||
	    chip->logbuffer_head >= LOG_BUFFER_ENTRIES) {
		dev_warn(chip->dev,
			 "Bad log buffer index %d\n", chip->logbuffer_head);
		goto abort;
	}

	if (!chip->logbuffer[chip->logbuffer_head]) {
		dev_warn(chip->dev,
			 "Log buffer index %d is NULL\n", chip->logbuffer_head);
		goto abort;
	}

	rem_nsec = do_div(ts_nsec, 1000000000);
	scnprintf(chip->logbuffer[chip->logbuffer_head],
		  LOG_BUFFER_ENTRY_SIZE, "[%5lu.%06lu] %s",
		  (unsigned long)ts_nsec, rem_nsec / 1000,
		  tmpbuffer);
	chip->logbuffer_head = (chip->logbuffer_head + 1) % LOG_BUFFER_ENTRIES;

abort:
	mutex_unlock(&chip->logbuffer_lock);
}

__printf(2, 3)
static void fusb302_log(struct fusb302_chip *chip, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	_fusb302_log(chip, fmt, args);
	va_end(args);
}

static int fusb302_debug_show(struct seq_file *s, void *v)
{
	struct fusb302_chip *chip = s->private;
	int tail;

	mutex_lock(&chip->logbuffer_lock);
	tail = chip->logbuffer_tail;
	while (tail != chip->logbuffer_head) {
		seq_printf(s, "%s\n", chip->logbuffer[tail]);
		tail = (tail + 1) % LOG_BUFFER_ENTRIES;
	}
	if (!seq_has_overflowed(s))
		chip->logbuffer_tail = tail;
	mutex_unlock(&chip->logbuffer_lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fusb302_debug);

static void fusb302_debugfs_init(struct fusb302_chip *chip)
{
	char name[NAME_MAX];

	mutex_init(&chip->logbuffer_lock);
	snprintf(name, NAME_MAX, "fusb302-%s", dev_name(chip->dev));
	chip->dentry = debugfs_create_dir(name, usb_debug_root);
	debugfs_create_file("log", S_IFREG | 0444, chip->dentry, chip,
			    &fusb302_debug_fops);
}

static void fusb302_debugfs_exit(struct fusb302_chip *chip)
{
	debugfs_remove(chip->dentry);
}

#else

static void fusb302_log(const struct fusb302_chip *chip,
			const char *fmt, ...) { }
static void fusb302_debugfs_init(const struct fusb302_chip *chip) { }
static void fusb302_debugfs_exit(const struct fusb302_chip *chip) { }

#endif

static int fusb302_i2c_write(struct fusb302_chip *chip,
			     u8 address, u8 data)
{
	int ret = 0;

	ret = i2c_smbus_write_byte_data(chip->i2c_client, address, data);
	if (ret < 0)
		fusb302_log(chip, "cannot write 0x%02x to 0x%02x, ret=%d",
			    data, address, ret);

	return ret;
}

static int fusb302_i2c_block_write(struct fusb302_chip *chip, u8 address,
				   u8 length, const u8 *data)
{
	int ret = 0;

	if (length <= 0)
		return ret;

	ret = i2c_smbus_write_i2c_block_data(chip->i2c_client, address,
					     length, data);
	if (ret < 0)
		fusb302_log(chip, "cannot block write 0x%02x, len=%d, ret=%d",
			    address, length, ret);

	return ret;
}

static int fusb302_i2c_read(struct fusb302_chip *chip,
			    u8 address, u8 *data)
{
	int ret = 0;

	ret = i2c_smbus_read_byte_data(chip->i2c_client, address);
	*data = (u8)ret;
	if (ret < 0)
		fusb302_log(chip, "cannot read %02x, ret=%d", address, ret);

	return ret;
}

static int fusb302_i2c_block_read(struct fusb302_chip *chip, u8 address,
				  u8 length, u8 *data)
{
	int ret = 0;

	if (length <= 0)
		return ret;

	ret = i2c_smbus_read_i2c_block_data(chip->i2c_client, address,
					    length, data);
	if (ret < 0) {
		fusb302_log(chip, "cannot block read 0x%02x, len=%d, ret=%d",
			    address, length, ret);
		goto done;
	}
	if (ret != length) {
		fusb302_log(chip, "only read %d/%d bytes from 0x%02x",
			    ret, length, address);
		ret = -EIO;
	}

done:
	return ret;
}

static int fusb302_i2c_mask_write(struct fusb302_chip *chip, u8 address,
				  u8 mask, u8 value)
{
	int ret = 0;
	u8 data;

	ret = fusb302_i2c_read(chip, address, &data);
	if (ret < 0)
		return ret;
	data &= ~mask;
	data |= value;
	ret = fusb302_i2c_write(chip, address, data);
	if (ret < 0)
		return ret;

	return ret;
}

static int fusb302_i2c_set_bits(struct fusb302_chip *chip, u8 address,
				u8 set_bits)
{
	return fusb302_i2c_mask_write(chip, address, 0x00, set_bits);
}

static int fusb302_i2c_clear_bits(struct fusb302_chip *chip, u8 address,
				  u8 clear_bits)
{
	return fusb302_i2c_mask_write(chip, address, clear_bits, 0x00);
}

static int fusb302_sw_reset(struct fusb302_chip *chip)
{
	int ret = 0;

	ret = fusb302_i2c_write(chip, FUSB_REG_RESET,
				FUSB_REG_RESET_SW_RESET);
	if (ret < 0)
		fusb302_log(chip, "cannot sw reset the chip, ret=%d", ret);
	else
		fusb302_log(chip, "sw reset");

	return ret;
}

static int fusb302_enable_tx_auto_retries(struct fusb302_chip *chip, u8 retry_count)
{
	int ret = 0;

	ret = fusb302_i2c_set_bits(chip, FUSB_REG_CONTROL3, retry_count |
				   FUSB_REG_CONTROL3_AUTO_RETRY);

	return ret;
}

/*
 * initialize interrupt on the chip
 * - unmasked interrupt: VBUS_OK
 */
static int fusb302_init_interrupt(struct fusb302_chip *chip)
{
	int ret = 0;

	ret = fusb302_i2c_write(chip, FUSB_REG_MASK,
				0xFF & ~FUSB_REG_MASK_VBUSOK);
	if (ret < 0)
		return ret;
	ret = fusb302_i2c_write(chip, FUSB_REG_MASKA, 0xFF);
	if (ret < 0)
		return ret;
	ret = fusb302_i2c_write(chip, FUSB_REG_MASKB, 0xFF);
	if (ret < 0)
		return ret;
	ret = fusb302_i2c_clear_bits(chip, FUSB_REG_CONTROL0,
				     FUSB_REG_CONTROL0_INT_MASK);
	if (ret < 0)
		return ret;

	return ret;
}

static int fusb302_set_power_mode(struct fusb302_chip *chip, u8 power_mode)
{
	int ret = 0;

	ret = fusb302_i2c_write(chip, FUSB_REG_POWER, power_mode);

	return ret;
}

static int tcpm_init(struct tcpc_dev *dev)
{
	struct fusb302_chip *chip = container_of(dev, struct fusb302_chip,
						 tcpc_dev);
	int ret = 0;
	u8 data;

	ret = fusb302_sw_reset(chip);
	if (ret < 0)
		return ret;
	ret = fusb302_enable_tx_auto_retries(chip, FUSB_REG_CONTROL3_N_RETRIES_3);
	if (ret < 0)
		return ret;
	ret = fusb302_init_interrupt(chip);
	if (ret < 0)
		return ret;
	ret = fusb302_set_power_mode(chip, FUSB_REG_POWER_PWR_ALL);
	if (ret < 0)
		return ret;
	ret = fusb302_i2c_read(chip, FUSB_REG_STATUS0, &data);
	if (ret < 0)
		return ret;
	chip->vbus_present = !!(data & FUSB_REG_STATUS0_VBUSOK);
	ret = fusb302_i2c_read(chip, FUSB_REG_DEVICE_ID, &data);
	if (ret < 0)
		return ret;
	fusb302_log(chip, "fusb302 device ID: 0x%02x", data);

	return ret;
}

static int tcpm_get_vbus(struct tcpc_dev *dev)
{
	struct fusb302_chip *chip = container_of(dev, struct fusb302_chip,
						 tcpc_dev);
	int ret = 0;

	mutex_lock(&chip->lock);
	ret = chip->vbus_present ? 1 : 0;
	mutex_unlock(&chip->lock);

	return ret;
}

static int tcpm_get_current_limit(struct tcpc_dev *dev)
{
	struct fusb302_chip *chip = container_of(dev, struct fusb302_chip,
						 tcpc_dev);
	int current_limit = 0;
	unsigned long timeout;

	if (!chip->extcon)
		return 0;

	/*
	 * USB2 Charger detection may still be in progress when we get here,
	 * this can take upto 600ms, wait 800ms max.
	 */
	timeout = jiffies + msecs_to_jiffies(800);
	do {
		if (extcon_get_state(chip->extcon, EXTCON_CHG_USB_SDP) == 1)
			current_limit = 500;

		if (extcon_get_state(chip->extcon, EXTCON_CHG_USB_CDP) == 1 ||
		    extcon_get_state(chip->extcon, EXTCON_CHG_USB_ACA) == 1)
			current_limit = 1500;

		if (extcon_get_state(chip->extcon, EXTCON_CHG_USB_DCP) == 1)
			current_limit = 2000;

		msleep(50);
	} while (current_limit == 0 && time_before(jiffies, timeout));

	return current_limit;
}

static int fusb302_set_src_current(struct fusb302_chip *chip,
				   enum src_current_status status)
{
	int ret = 0;

	chip->src_current_status = status;
	switch (status) {
	case SRC_CURRENT_DEFAULT:
		ret = fusb302_i2c_mask_write(chip, FUSB_REG_CONTROL0,
					     FUSB_REG_CONTROL0_HOST_CUR_MASK,
					     FUSB_REG_CONTROL0_HOST_CUR_DEF);
		break;
	case SRC_CURRENT_MEDIUM:
		ret = fusb302_i2c_mask_write(chip, FUSB_REG_CONTROL0,
					     FUSB_REG_CONTROL0_HOST_CUR_MASK,
					     FUSB_REG_CONTROL0_HOST_CUR_MED);
		break;
	case SRC_CURRENT_HIGH:
		ret = fusb302_i2c_mask_write(chip, FUSB_REG_CONTROL0,
					     FUSB_REG_CONTROL0_HOST_CUR_MASK,
					     FUSB_REG_CONTROL0_HOST_CUR_HIGH);
		break;
	default:
		break;
	}

	return ret;
}

static int fusb302_set_toggling(struct fusb302_chip *chip,
				enum toggling_mode mode)
{
	int ret = 0;

	/* first disable toggling */
	ret = fusb302_i2c_clear_bits(chip, FUSB_REG_CONTROL2,
				     FUSB_REG_CONTROL2_TOGGLE);
	if (ret < 0)
		return ret;
	/* mask interrupts for SRC or SNK */
	ret = fusb302_i2c_set_bits(chip, FUSB_REG_MASK,
				   FUSB_REG_MASK_BC_LVL |
				   FUSB_REG_MASK_COMP_CHNG);
	if (ret < 0)
		return ret;
	chip->intr_bc_lvl = false;
	chip->intr_comp_chng = false;
	/* configure toggling mode: none/snk/src/drp */
	switch (mode) {
	case TOGGLING_MODE_OFF:
		ret = fusb302_i2c_mask_write(chip, FUSB_REG_CONTROL2,
					     FUSB_REG_CONTROL2_MODE_MASK,
					     FUSB_REG_CONTROL2_MODE_NONE);
		if (ret < 0)
			return ret;
		break;
	case TOGGLING_MODE_SNK:
		ret = fusb302_i2c_mask_write(chip, FUSB_REG_CONTROL2,
					     FUSB_REG_CONTROL2_MODE_MASK,
					     FUSB_REG_CONTROL2_MODE_UFP);
		if (ret < 0)
			return ret;
		break;
	case TOGGLING_MODE_SRC:
		ret = fusb302_i2c_mask_write(chip, FUSB_REG_CONTROL2,
					     FUSB_REG_CONTROL2_MODE_MASK,
					     FUSB_REG_CONTROL2_MODE_DFP);
		if (ret < 0)
			return ret;
		break;
	case TOGGLING_MODE_DRP:
		ret = fusb302_i2c_mask_write(chip, FUSB_REG_CONTROL2,
					     FUSB_REG_CONTROL2_MODE_MASK,
					     FUSB_REG_CONTROL2_MODE_DRP);
		if (ret < 0)
			return ret;
		break;
	default:
		break;
	}

	if (mode == TOGGLING_MODE_OFF) {
		/* mask TOGDONE interrupt */
		ret = fusb302_i2c_set_bits(chip, FUSB_REG_MASKA,
					   FUSB_REG_MASKA_TOGDONE);
		if (ret < 0)
			return ret;
		chip->intr_togdone = false;
	} else {
		/* Datasheet says vconn MUST be off when toggling */
		WARN(chip->vconn_on, "Vconn is on during toggle start");
		/* unmask TOGDONE interrupt */
		ret = fusb302_i2c_clear_bits(chip, FUSB_REG_MASKA,
					     FUSB_REG_MASKA_TOGDONE);
		if (ret < 0)
			return ret;
		chip->intr_togdone = true;
		/* start toggling */
		ret = fusb302_i2c_set_bits(chip, FUSB_REG_CONTROL2,
					   FUSB_REG_CONTROL2_TOGGLE);
		if (ret < 0)
			return ret;
		/* during toggling, consider cc as Open */
		chip->cc1 = TYPEC_CC_OPEN;
		chip->cc2 = TYPEC_CC_OPEN;
	}
	chip->toggling_mode = mode;

	return ret;
}

static const char * const typec_cc_status_name[] = {
	[TYPEC_CC_OPEN]		= "Open",
	[TYPEC_CC_RA]		= "Ra",
	[TYPEC_CC_RD]		= "Rd",
	[TYPEC_CC_RP_DEF]	= "Rp-def",
	[TYPEC_CC_RP_1_5]	= "Rp-1.5",
	[TYPEC_CC_RP_3_0]	= "Rp-3.0",
};

static const enum src_current_status cc_src_current[] = {
	[TYPEC_CC_OPEN]		= SRC_CURRENT_DEFAULT,
	[TYPEC_CC_RA]		= SRC_CURRENT_DEFAULT,
	[TYPEC_CC_RD]		= SRC_CURRENT_DEFAULT,
	[TYPEC_CC_RP_DEF]	= SRC_CURRENT_DEFAULT,
	[TYPEC_CC_RP_1_5]	= SRC_CURRENT_MEDIUM,
	[TYPEC_CC_RP_3_0]	= SRC_CURRENT_HIGH,
};

static int tcpm_set_cc(struct tcpc_dev *dev, enum typec_cc_status cc)
{
	struct fusb302_chip *chip = container_of(dev, struct fusb302_chip,
						 tcpc_dev);
	u8 switches0_mask = FUSB_REG_SWITCHES0_CC1_PU_EN |
			    FUSB_REG_SWITCHES0_CC2_PU_EN |
			    FUSB_REG_SWITCHES0_CC1_PD_EN |
			    FUSB_REG_SWITCHES0_CC2_PD_EN;
	u8 rd_mda, switches0_data = 0x00;
	int ret = 0;

	mutex_lock(&chip->lock);
	switch (cc) {
	case TYPEC_CC_OPEN:
		break;
	case TYPEC_CC_RD:
		switches0_data |= FUSB_REG_SWITCHES0_CC1_PD_EN |
				  FUSB_REG_SWITCHES0_CC2_PD_EN;
		break;
	case TYPEC_CC_RP_DEF:
	case TYPEC_CC_RP_1_5:
	case TYPEC_CC_RP_3_0:
		switches0_data |= (chip->cc_polarity == TYPEC_POLARITY_CC1) ?
				  FUSB_REG_SWITCHES0_CC1_PU_EN :
				  FUSB_REG_SWITCHES0_CC2_PU_EN;
		break;
	default:
		fusb302_log(chip, "unsupported cc value %s",
			    typec_cc_status_name[cc]);
		ret = -EINVAL;
		goto done;
	}

	fusb302_log(chip, "cc := %s", typec_cc_status_name[cc]);

	ret = fusb302_set_toggling(chip, TOGGLING_MODE_OFF);
	if (ret < 0) {
		fusb302_log(chip, "cannot set toggling mode, ret=%d", ret);
		goto done;
	}

	ret = fusb302_i2c_mask_write(chip, FUSB_REG_SWITCHES0,
				     switches0_mask, switches0_data);
	if (ret < 0) {
		fusb302_log(chip, "cannot set pull-up/-down, ret = %d", ret);
		goto done;
	}
	/* reset the cc status */
	chip->cc1 = TYPEC_CC_OPEN;
	chip->cc2 = TYPEC_CC_OPEN;

	/* adjust current for SRC */
	ret = fusb302_set_src_current(chip, cc_src_current[cc]);
	if (ret < 0) {
		fusb302_log(chip, "cannot set src current %s, ret=%d",
			    typec_cc_status_name[cc], ret);
		goto done;
	}

	/* enable/disable interrupts, BC_LVL for SNK and COMP_CHNG for SRC */
	switch (cc) {
	case TYPEC_CC_RP_DEF:
	case TYPEC_CC_RP_1_5:
	case TYPEC_CC_RP_3_0:
		rd_mda = rd_mda_value[cc_src_current[cc]];
		ret = fusb302_i2c_write(chip, FUSB_REG_MEASURE, rd_mda);
		if (ret < 0) {
			fusb302_log(chip,
				    "cannot set SRC measure value, ret=%d",
				    ret);
			goto done;
		}
		ret = fusb302_i2c_mask_write(chip, FUSB_REG_MASK,
					     FUSB_REG_MASK_BC_LVL |
					     FUSB_REG_MASK_COMP_CHNG,
					     FUSB_REG_MASK_BC_LVL);
		if (ret < 0) {
			fusb302_log(chip, "cannot set SRC interrupt, ret=%d",
				    ret);
			goto done;
		}
		chip->intr_comp_chng = true;
		chip->intr_bc_lvl = false;
		break;
	case TYPEC_CC_RD:
		ret = fusb302_i2c_mask_write(chip, FUSB_REG_MASK,
					     FUSB_REG_MASK_BC_LVL |
					     FUSB_REG_MASK_COMP_CHNG,
					     FUSB_REG_MASK_COMP_CHNG);
		if (ret < 0) {
			fusb302_log(chip, "cannot set SRC interrupt, ret=%d",
				    ret);
			goto done;
		}
		chip->intr_bc_lvl = true;
		chip->intr_comp_chng = false;
		break;
	default:
		break;
	}
done:
	mutex_unlock(&chip->lock);

	return ret;
}

static int tcpm_get_cc(struct tcpc_dev *dev, enum typec_cc_status *cc1,
		       enum typec_cc_status *cc2)
{
	struct fusb302_chip *chip = container_of(dev, struct fusb302_chip,
						 tcpc_dev);

	mutex_lock(&chip->lock);
	*cc1 = chip->cc1;
	*cc2 = chip->cc2;
	fusb302_log(chip, "cc1=%s, cc2=%s", typec_cc_status_name[*cc1],
		    typec_cc_status_name[*cc2]);
	mutex_unlock(&chip->lock);

	return 0;
}

static int tcpm_set_polarity(struct tcpc_dev *dev,
			     enum typec_cc_polarity polarity)
{
	return 0;
}

static int tcpm_set_vconn(struct tcpc_dev *dev, bool on)
{
	struct fusb302_chip *chip = container_of(dev, struct fusb302_chip,
						 tcpc_dev);
	int ret = 0;
	u8 switches0_data = 0x00;
	u8 switches0_mask = FUSB_REG_SWITCHES0_VCONN_CC1 |
			    FUSB_REG_SWITCHES0_VCONN_CC2;

	mutex_lock(&chip->lock);
	if (chip->vconn_on == on) {
		fusb302_log(chip, "vconn is already %s", on ? "On" : "Off");
		goto done;
	}
	if (on) {
		switches0_data = (chip->cc_polarity == TYPEC_POLARITY_CC1) ?
				 FUSB_REG_SWITCHES0_VCONN_CC2 :
				 FUSB_REG_SWITCHES0_VCONN_CC1;
	}
	ret = fusb302_i2c_mask_write(chip, FUSB_REG_SWITCHES0,
				     switches0_mask, switches0_data);
	if (ret < 0)
		goto done;
	chip->vconn_on = on;
	fusb302_log(chip, "vconn := %s", on ? "On" : "Off");
done:
	mutex_unlock(&chip->lock);

	return ret;
}

static int tcpm_set_vbus(struct tcpc_dev *dev, bool on, bool charge)
{
	struct fusb302_chip *chip = container_of(dev, struct fusb302_chip,
						 tcpc_dev);
	int ret = 0;

	mutex_lock(&chip->lock);
	if (chip->vbus_on == on) {
		fusb302_log(chip, "vbus is already %s", on ? "On" : "Off");
	} else {
		if (on)
			ret = regulator_enable(chip->vbus);
		else
			ret = regulator_disable(chip->vbus);
		if (ret < 0) {
			fusb302_log(chip, "cannot %s vbus regulator, ret=%d",
				    on ? "enable" : "disable", ret);
			goto done;
		}
		chip->vbus_on = on;
		fusb302_log(chip, "vbus := %s", on ? "On" : "Off");
	}
	if (chip->charge_on == charge)
		fusb302_log(chip, "charge is already %s",
			    charge ? "On" : "Off");
	else
		chip->charge_on = charge;

done:
	mutex_unlock(&chip->lock);

	return ret;
}

static int fusb302_pd_tx_flush(struct fusb302_chip *chip)
{
	return fusb302_i2c_set_bits(chip, FUSB_REG_CONTROL0,
				    FUSB_REG_CONTROL0_TX_FLUSH);
}

static int fusb302_pd_rx_flush(struct fusb302_chip *chip)
{
	return fusb302_i2c_set_bits(chip, FUSB_REG_CONTROL1,
				    FUSB_REG_CONTROL1_RX_FLUSH);
}

static int fusb302_pd_set_auto_goodcrc(struct fusb302_chip *chip, bool on)
{
	if (on)
		return fusb302_i2c_set_bits(chip, FUSB_REG_SWITCHES1,
					    FUSB_REG_SWITCHES1_AUTO_GCRC);
	return fusb302_i2c_clear_bits(chip, FUSB_REG_SWITCHES1,
					    FUSB_REG_SWITCHES1_AUTO_GCRC);
}

static int fusb302_pd_set_interrupts(struct fusb302_chip *chip, bool on)
{
	int ret = 0;
	u8 mask_interrupts = FUSB_REG_MASK_COLLISION;
	u8 maska_interrupts = FUSB_REG_MASKA_RETRYFAIL |
			      FUSB_REG_MASKA_HARDSENT |
			      FUSB_REG_MASKA_TX_SUCCESS |
			      FUSB_REG_MASKA_HARDRESET;
	u8 maskb_interrupts = FUSB_REG_MASKB_GCRCSENT;

	ret = on ?
		fusb302_i2c_clear_bits(chip, FUSB_REG_MASK, mask_interrupts) :
		fusb302_i2c_set_bits(chip, FUSB_REG_MASK, mask_interrupts);
	if (ret < 0)
		return ret;
	ret = on ?
		fusb302_i2c_clear_bits(chip, FUSB_REG_MASKA, maska_interrupts) :
		fusb302_i2c_set_bits(chip, FUSB_REG_MASKA, maska_interrupts);
	if (ret < 0)
		return ret;
	ret = on ?
		fusb302_i2c_clear_bits(chip, FUSB_REG_MASKB, maskb_interrupts) :
		fusb302_i2c_set_bits(chip, FUSB_REG_MASKB, maskb_interrupts);
	return ret;
}

static int tcpm_set_pd_rx(struct tcpc_dev *dev, bool on)
{
	struct fusb302_chip *chip = container_of(dev, struct fusb302_chip,
						 tcpc_dev);
	int ret = 0;

	mutex_lock(&chip->lock);
	ret = fusb302_pd_rx_flush(chip);
	if (ret < 0) {
		fusb302_log(chip, "cannot flush pd rx buffer, ret=%d", ret);
		goto done;
	}
	ret = fusb302_pd_tx_flush(chip);
	if (ret < 0) {
		fusb302_log(chip, "cannot flush pd tx buffer, ret=%d", ret);
		goto done;
	}
	ret = fusb302_pd_set_auto_goodcrc(chip, on);
	if (ret < 0) {
		fusb302_log(chip, "cannot turn %s auto GCRC, ret=%d",
			    on ? "on" : "off", ret);
		goto done;
	}
	ret = fusb302_pd_set_interrupts(chip, on);
	if (ret < 0) {
		fusb302_log(chip, "cannot turn %s pd interrupts, ret=%d",
			    on ? "on" : "off", ret);
		goto done;
	}
	fusb302_log(chip, "pd := %s", on ? "on" : "off");
done:
	mutex_unlock(&chip->lock);

	return ret;
}

static const char * const typec_role_name[] = {
	[TYPEC_SINK]		= "Sink",
	[TYPEC_SOURCE]		= "Source",
};

static const char * const typec_data_role_name[] = {
	[TYPEC_DEVICE]		= "Device",
	[TYPEC_HOST]		= "Host",
};

static int tcpm_set_roles(struct tcpc_dev *dev, bool attached,
			  enum typec_role pwr, enum typec_data_role data)
{
	struct fusb302_chip *chip = container_of(dev, struct fusb302_chip,
						 tcpc_dev);
	int ret = 0;
	u8 switches1_mask = FUSB_REG_SWITCHES1_POWERROLE |
			    FUSB_REG_SWITCHES1_DATAROLE;
	u8 switches1_data = 0x00;

	mutex_lock(&chip->lock);
	if (pwr == TYPEC_SOURCE)
		switches1_data |= FUSB_REG_SWITCHES1_POWERROLE;
	if (data == TYPEC_HOST)
		switches1_data |= FUSB_REG_SWITCHES1_DATAROLE;
	ret = fusb302_i2c_mask_write(chip, FUSB_REG_SWITCHES1,
				     switches1_mask, switches1_data);
	if (ret < 0) {
		fusb302_log(chip, "unable to set pd header %s, %s, ret=%d",
			    typec_role_name[pwr], typec_data_role_name[data],
			    ret);
		goto done;
	}
	fusb302_log(chip, "pd header := %s, %s", typec_role_name[pwr],
		    typec_data_role_name[data]);
done:
	mutex_unlock(&chip->lock);

	return ret;
}

static int tcpm_start_toggling(struct tcpc_dev *dev,
			       enum typec_port_type port_type,
			       enum typec_cc_status cc)
{
	struct fusb302_chip *chip = container_of(dev, struct fusb302_chip,
						 tcpc_dev);
	enum toggling_mode mode = TOGGLING_MODE_OFF;
	int ret = 0;

	switch (port_type) {
	case TYPEC_PORT_SRC:
		mode = TOGGLING_MODE_SRC;
		break;
	case TYPEC_PORT_SNK:
		mode = TOGGLING_MODE_SNK;
		break;
	case TYPEC_PORT_DRP:
		mode = TOGGLING_MODE_DRP;
		break;
	}

	mutex_lock(&chip->lock);
	ret = fusb302_set_src_current(chip, cc_src_current[cc]);
	if (ret < 0) {
		fusb302_log(chip, "unable to set src current %s, ret=%d",
			    typec_cc_status_name[cc], ret);
		goto done;
	}
	ret = fusb302_set_toggling(chip, mode);
	if (ret < 0) {
		fusb302_log(chip,
			    "unable to start drp toggling, ret=%d", ret);
		goto done;
	}
	fusb302_log(chip, "start drp toggling");
done:
	mutex_unlock(&chip->lock);

	return ret;
}

static int fusb302_pd_send_message(struct fusb302_chip *chip,
				   const struct pd_message *msg)
{
	int ret = 0;
	u8 buf[40];
	u8 pos = 0;
	int len;

	/* SOP tokens */
	buf[pos++] = FUSB302_TKN_SYNC1;
	buf[pos++] = FUSB302_TKN_SYNC1;
	buf[pos++] = FUSB302_TKN_SYNC1;
	buf[pos++] = FUSB302_TKN_SYNC2;

	len = pd_header_cnt_le(msg->header) * 4;
	/* plug 2 for header */
	len += 2;
	if (len > 0x1F) {
		fusb302_log(chip,
			    "PD message too long %d (incl. header)", len);
		return -EINVAL;
	}
	/* packsym tells the FUSB302 chip that the next X bytes are payload */
	buf[pos++] = FUSB302_TKN_PACKSYM | (len & 0x1F);
	memcpy(&buf[pos], &msg->header, sizeof(msg->header));
	pos += sizeof(msg->header);

	len -= 2;
	memcpy(&buf[pos], msg->payload, len);
	pos += len;

	/* CRC */
	buf[pos++] = FUSB302_TKN_JAMCRC;
	/* EOP */
	buf[pos++] = FUSB302_TKN_EOP;
	/* turn tx off after sending message */
	buf[pos++] = FUSB302_TKN_TXOFF;
	/* start transmission */
	buf[pos++] = FUSB302_TKN_TXON;

	ret = fusb302_i2c_block_write(chip, FUSB_REG_FIFOS, pos, buf);
	if (ret < 0)
		return ret;
	fusb302_log(chip, "sending PD message header: %x", msg->header);
	fusb302_log(chip, "sending PD message len: %d", len);

	return ret;
}

static int fusb302_pd_send_hardreset(struct fusb302_chip *chip)
{
	return fusb302_i2c_set_bits(chip, FUSB_REG_CONTROL3,
				    FUSB_REG_CONTROL3_SEND_HARDRESET);
}

static const char * const transmit_type_name[] = {
	[TCPC_TX_SOP]			= "SOP",
	[TCPC_TX_SOP_PRIME]		= "SOP'",
	[TCPC_TX_SOP_PRIME_PRIME]	= "SOP''",
	[TCPC_TX_SOP_DEBUG_PRIME]	= "DEBUG'",
	[TCPC_TX_SOP_DEBUG_PRIME_PRIME]	= "DEBUG''",
	[TCPC_TX_HARD_RESET]		= "HARD_RESET",
	[TCPC_TX_CABLE_RESET]		= "CABLE_RESET",
	[TCPC_TX_BIST_MODE_2]		= "BIST_MODE_2",
};

static int tcpm_pd_transmit(struct tcpc_dev *dev, enum tcpm_transmit_type type,
			    const struct pd_message *msg, unsigned int negotiated_rev)
{
	struct fusb302_chip *chip = container_of(dev, struct fusb302_chip,
						 tcpc_dev);
	int ret = 0;

	mutex_lock(&chip->lock);
	switch (type) {
	case TCPC_TX_SOP:
		/* nRetryCount 3 in P2.0 spec, whereas 2 in PD3.0 spec */
		ret = fusb302_enable_tx_auto_retries(chip, negotiated_rev > PD_REV20 ?
						     FUSB_REG_CONTROL3_N_RETRIES_2 :
						     FUSB_REG_CONTROL3_N_RETRIES_3);
		if (ret < 0)
			fusb302_log(chip, "Cannot update retry count ret=%d", ret);

		ret = fusb302_pd_send_message(chip, msg);
		if (ret < 0)
			fusb302_log(chip,
				    "cannot send PD message, ret=%d", ret);
		break;
	case TCPC_TX_HARD_RESET:
		ret = fusb302_pd_send_hardreset(chip);
		if (ret < 0)
			fusb302_log(chip,
				    "cannot send hardreset, ret=%d", ret);
		break;
	default:
		fusb302_log(chip, "type %s not supported",
			    transmit_type_name[type]);
		ret = -EINVAL;
	}
	mutex_unlock(&chip->lock);

	return ret;
}

static enum typec_cc_status fusb302_bc_lvl_to_cc(u8 bc_lvl)
{
	if (bc_lvl == FUSB_REG_STATUS0_BC_LVL_1230_MAX)
		return TYPEC_CC_RP_3_0;
	if (bc_lvl == FUSB_REG_STATUS0_BC_LVL_600_1230)
		return TYPEC_CC_RP_1_5;
	if (bc_lvl == FUSB_REG_STATUS0_BC_LVL_200_600)
		return TYPEC_CC_RP_DEF;
	return TYPEC_CC_OPEN;
}

static void fusb302_bc_lvl_handler_work(struct work_struct *work)
{
	struct fusb302_chip *chip = container_of(work, struct fusb302_chip,
						 bc_lvl_handler.work);
	int ret = 0;
	u8 status0;
	u8 bc_lvl;
	enum typec_cc_status cc_status;

	mutex_lock(&chip->lock);
	if (!chip->intr_bc_lvl) {
		fusb302_log(chip, "BC_LVL interrupt is turned off, abort");
		goto done;
	}
	ret = fusb302_i2c_read(chip, FUSB_REG_STATUS0, &status0);
	if (ret < 0)
		goto done;
	fusb302_log(chip, "BC_LVL handler, status0=0x%02x", status0);
	if (status0 & FUSB_REG_STATUS0_ACTIVITY) {
		fusb302_log(chip, "CC activities detected, delay handling");
		mod_delayed_work(chip->wq, &chip->bc_lvl_handler,
				 msecs_to_jiffies(T_BC_LVL_DEBOUNCE_DELAY_MS));
		goto done;
	}
	bc_lvl = status0 & FUSB_REG_STATUS0_BC_LVL_MASK;
	cc_status = fusb302_bc_lvl_to_cc(bc_lvl);
	if (chip->cc_polarity == TYPEC_POLARITY_CC1) {
		if (chip->cc1 != cc_status) {
			fusb302_log(chip, "cc1: %s -> %s",
				    typec_cc_status_name[chip->cc1],
				    typec_cc_status_name[cc_status]);
			chip->cc1 = cc_status;
			tcpm_cc_change(chip->tcpm_port);
		}
	} else {
		if (chip->cc2 != cc_status) {
			fusb302_log(chip, "cc2: %s -> %s",
				    typec_cc_status_name[chip->cc2],
				    typec_cc_status_name[cc_status]);
			chip->cc2 = cc_status;
			tcpm_cc_change(chip->tcpm_port);
		}
	}

done:
	mutex_unlock(&chip->lock);
}

static void init_tcpc_dev(struct tcpc_dev *fusb302_tcpc_dev)
{
	fusb302_tcpc_dev->init = tcpm_init;
	fusb302_tcpc_dev->get_vbus = tcpm_get_vbus;
	fusb302_tcpc_dev->get_current_limit = tcpm_get_current_limit;
	fusb302_tcpc_dev->set_cc = tcpm_set_cc;
	fusb302_tcpc_dev->get_cc = tcpm_get_cc;
	fusb302_tcpc_dev->set_polarity = tcpm_set_polarity;
	fusb302_tcpc_dev->set_vconn = tcpm_set_vconn;
	fusb302_tcpc_dev->set_vbus = tcpm_set_vbus;
	fusb302_tcpc_dev->set_pd_rx = tcpm_set_pd_rx;
	fusb302_tcpc_dev->set_roles = tcpm_set_roles;
	fusb302_tcpc_dev->start_toggling = tcpm_start_toggling;
	fusb302_tcpc_dev->pd_transmit = tcpm_pd_transmit;
}

static const char * const cc_polarity_name[] = {
	[TYPEC_POLARITY_CC1]	= "Polarity_CC1",
	[TYPEC_POLARITY_CC2]	= "Polarity_CC2",
};

static int fusb302_set_cc_polarity_and_pull(struct fusb302_chip *chip,
					    enum typec_cc_polarity cc_polarity,
					    bool pull_up, bool pull_down)
{
	int ret = 0;
	u8 switches0_data = 0x00;
	u8 switches1_mask = FUSB_REG_SWITCHES1_TXCC1_EN |
			    FUSB_REG_SWITCHES1_TXCC2_EN;
	u8 switches1_data = 0x00;

	if (pull_down)
		switches0_data |= FUSB_REG_SWITCHES0_CC1_PD_EN |
				  FUSB_REG_SWITCHES0_CC2_PD_EN;

	if (cc_polarity == TYPEC_POLARITY_CC1) {
		switches0_data |= FUSB_REG_SWITCHES0_MEAS_CC1;
		if (chip->vconn_on)
			switches0_data |= FUSB_REG_SWITCHES0_VCONN_CC2;
		if (pull_up)
			switches0_data |= FUSB_REG_SWITCHES0_CC1_PU_EN;
		switches1_data = FUSB_REG_SWITCHES1_TXCC1_EN;
	} else {
		switches0_data |= FUSB_REG_SWITCHES0_MEAS_CC2;
		if (chip->vconn_on)
			switches0_data |= FUSB_REG_SWITCHES0_VCONN_CC1;
		if (pull_up)
			switches0_data |= FUSB_REG_SWITCHES0_CC2_PU_EN;
		switches1_data = FUSB_REG_SWITCHES1_TXCC2_EN;
	}
	ret = fusb302_i2c_write(chip, FUSB_REG_SWITCHES0, switches0_data);
	if (ret < 0)
		return ret;
	ret = fusb302_i2c_mask_write(chip, FUSB_REG_SWITCHES1,
				     switches1_mask, switches1_data);
	if (ret < 0)
		return ret;
	chip->cc_polarity = cc_polarity;

	return ret;
}

static int fusb302_handle_togdone_snk(struct fusb302_chip *chip,
				      u8 togdone_result)
{
	int ret = 0;
	u8 status0;
	u8 bc_lvl;
	enum typec_cc_polarity cc_polarity;
	enum typec_cc_status cc_status_active, cc1, cc2;

	/* set polarity and pull_up, pull_down */
	cc_polarity = (togdone_result == FUSB_REG_STATUS1A_TOGSS_SNK1) ?
		      TYPEC_POLARITY_CC1 : TYPEC_POLARITY_CC2;
	ret = fusb302_set_cc_polarity_and_pull(chip, cc_polarity, false, true);
	if (ret < 0) {
		fusb302_log(chip, "cannot set cc polarity %s, ret=%d",
			    cc_polarity_name[cc_polarity], ret);
		return ret;
	}
	/* fusb302_set_cc_polarity() has set the correct measure block */
	ret = fusb302_i2c_read(chip, FUSB_REG_STATUS0, &status0);
	if (ret < 0)
		return ret;
	bc_lvl = status0 & FUSB_REG_STATUS0_BC_LVL_MASK;
	cc_status_active = fusb302_bc_lvl_to_cc(bc_lvl);
	/* restart toggling if the cc status on the active line is OPEN */
	if (cc_status_active == TYPEC_CC_OPEN) {
		fusb302_log(chip, "restart toggling as CC_OPEN detected");
		ret = fusb302_set_toggling(chip, chip->toggling_mode);
		return ret;
	}
	/* update tcpm with the new cc value */
	cc1 = (cc_polarity == TYPEC_POLARITY_CC1) ?
	      cc_status_active : TYPEC_CC_OPEN;
	cc2 = (cc_polarity == TYPEC_POLARITY_CC2) ?
	      cc_status_active : TYPEC_CC_OPEN;
	if ((chip->cc1 != cc1) || (chip->cc2 != cc2)) {
		chip->cc1 = cc1;
		chip->cc2 = cc2;
		tcpm_cc_change(chip->tcpm_port);
	}
	/* turn off toggling */
	ret = fusb302_set_toggling(chip, TOGGLING_MODE_OFF);
	if (ret < 0) {
		fusb302_log(chip,
			    "cannot set toggling mode off, ret=%d", ret);
		return ret;
	}
	/* unmask bc_lvl interrupt */
	ret = fusb302_i2c_clear_bits(chip, FUSB_REG_MASK, FUSB_REG_MASK_BC_LVL);
	if (ret < 0) {
		fusb302_log(chip,
			    "cannot unmask bc_lcl interrupt, ret=%d", ret);
		return ret;
	}
	chip->intr_bc_lvl = true;
	fusb302_log(chip, "detected cc1=%s, cc2=%s",
		    typec_cc_status_name[cc1],
		    typec_cc_status_name[cc2]);

	return ret;
}

/* On error returns < 0, otherwise a typec_cc_status value */
static int fusb302_get_src_cc_status(struct fusb302_chip *chip,
				     enum typec_cc_polarity cc_polarity,
				     enum typec_cc_status *cc)
{
	u8 ra_mda = ra_mda_value[chip->src_current_status];
	u8 rd_mda = rd_mda_value[chip->src_current_status];
	u8 switches0_data, status0;
	int ret;

	/* Step 1: Set switches so that we measure the right CC pin */
	switches0_data = (cc_polarity == TYPEC_POLARITY_CC1) ?
		FUSB_REG_SWITCHES0_CC1_PU_EN | FUSB_REG_SWITCHES0_MEAS_CC1 :
		FUSB_REG_SWITCHES0_CC2_PU_EN | FUSB_REG_SWITCHES0_MEAS_CC2;
	ret = fusb302_i2c_write(chip, FUSB_REG_SWITCHES0, switches0_data);
	if (ret < 0)
		return ret;

	fusb302_i2c_read(chip, FUSB_REG_SWITCHES0, &status0);
	fusb302_log(chip, "get_src_cc_status switches: 0x%0x", status0);

	/* Step 2: Set compararator volt to differentiate between Open and Rd */
	ret = fusb302_i2c_write(chip, FUSB_REG_MEASURE, rd_mda);
	if (ret < 0)
		return ret;

	usleep_range(50, 100);
	ret = fusb302_i2c_read(chip, FUSB_REG_STATUS0, &status0);
	if (ret < 0)
		return ret;

	fusb302_log(chip, "get_src_cc_status rd_mda status0: 0x%0x", status0);
	if (status0 & FUSB_REG_STATUS0_COMP) {
		*cc = TYPEC_CC_OPEN;
		return 0;
	}

	/* Step 3: Set compararator input to differentiate between Rd and Ra. */
	ret = fusb302_i2c_write(chip, FUSB_REG_MEASURE, ra_mda);
	if (ret < 0)
		return ret;

	usleep_range(50, 100);
	ret = fusb302_i2c_read(chip, FUSB_REG_STATUS0, &status0);
	if (ret < 0)
		return ret;

	fusb302_log(chip, "get_src_cc_status ra_mda status0: 0x%0x", status0);
	if (status0 & FUSB_REG_STATUS0_COMP)
		*cc = TYPEC_CC_RD;
	else
		*cc = TYPEC_CC_RA;

	return 0;
}

static int fusb302_handle_togdone_src(struct fusb302_chip *chip,
				      u8 togdone_result)
{
	/*
	 * - set polarity (measure cc, vconn, tx)
	 * - set pull_up, pull_down
	 * - set cc1, cc2, and update to tcpm_port
	 * - set I_COMP interrupt on
	 */
	int ret = 0;
	u8 rd_mda = rd_mda_value[chip->src_current_status];
	enum toggling_mode toggling_mode = chip->toggling_mode;
	enum typec_cc_polarity cc_polarity;
	enum typec_cc_status cc1, cc2;

	/*
	 * The toggle-engine will stop in a src state if it sees either Ra or
	 * Rd. Determine the status for both CC pins, starting with the one
	 * where toggling stopped, as that is where the switches point now.
	 */
	if (togdone_result == FUSB_REG_STATUS1A_TOGSS_SRC1)
		ret = fusb302_get_src_cc_status(chip, TYPEC_POLARITY_CC1, &cc1);
	else
		ret = fusb302_get_src_cc_status(chip, TYPEC_POLARITY_CC2, &cc2);
	if (ret < 0)
		return ret;
	/* we must turn off toggling before we can measure the other pin */
	ret = fusb302_set_toggling(chip, TOGGLING_MODE_OFF);
	if (ret < 0) {
		fusb302_log(chip, "cannot set toggling mode off, ret=%d", ret);
		return ret;
	}
	/* get the status of the other pin */
	if (togdone_result == FUSB_REG_STATUS1A_TOGSS_SRC1)
		ret = fusb302_get_src_cc_status(chip, TYPEC_POLARITY_CC2, &cc2);
	else
		ret = fusb302_get_src_cc_status(chip, TYPEC_POLARITY_CC1, &cc1);
	if (ret < 0)
		return ret;

	/* determine polarity based on the status of both pins */
	if (cc1 == TYPEC_CC_RD &&
			(cc2 == TYPEC_CC_OPEN || cc2 == TYPEC_CC_RA)) {
		cc_polarity = TYPEC_POLARITY_CC1;
	} else if (cc2 == TYPEC_CC_RD &&
		    (cc1 == TYPEC_CC_OPEN || cc1 == TYPEC_CC_RA)) {
		cc_polarity = TYPEC_POLARITY_CC2;
	} else {
		fusb302_log(chip, "unexpected CC status cc1=%s, cc2=%s, restarting toggling",
			    typec_cc_status_name[cc1],
			    typec_cc_status_name[cc2]);
		return fusb302_set_toggling(chip, toggling_mode);
	}
	/* set polarity and pull_up, pull_down */
	ret = fusb302_set_cc_polarity_and_pull(chip, cc_polarity, true, false);
	if (ret < 0) {
		fusb302_log(chip, "cannot set cc polarity %s, ret=%d",
			    cc_polarity_name[cc_polarity], ret);
		return ret;
	}
	/* update tcpm with the new cc value */
	if ((chip->cc1 != cc1) || (chip->cc2 != cc2)) {
		chip->cc1 = cc1;
		chip->cc2 = cc2;
		tcpm_cc_change(chip->tcpm_port);
	}
	/* set MDAC to Rd threshold, and unmask I_COMP for unplug detection */
	ret = fusb302_i2c_write(chip, FUSB_REG_MEASURE, rd_mda);
	if (ret < 0)
		return ret;
	/* unmask comp_chng interrupt */
	ret = fusb302_i2c_clear_bits(chip, FUSB_REG_MASK,
				     FUSB_REG_MASK_COMP_CHNG);
	if (ret < 0) {
		fusb302_log(chip,
			    "cannot unmask comp_chng interrupt, ret=%d", ret);
		return ret;
	}
	chip->intr_comp_chng = true;
	fusb302_log(chip, "detected cc1=%s, cc2=%s",
		    typec_cc_status_name[cc1],
		    typec_cc_status_name[cc2]);

	return ret;
}

static int fusb302_handle_togdone(struct fusb302_chip *chip)
{
	int ret = 0;
	u8 status1a;
	u8 togdone_result;

	ret = fusb302_i2c_read(chip, FUSB_REG_STATUS1A, &status1a);
	if (ret < 0)
		return ret;
	togdone_result = (status1a >> FUSB_REG_STATUS1A_TOGSS_POS) &
			 FUSB_REG_STATUS1A_TOGSS_MASK;
	switch (togdone_result) {
	case FUSB_REG_STATUS1A_TOGSS_SNK1:
	case FUSB_REG_STATUS1A_TOGSS_SNK2:
		return fusb302_handle_togdone_snk(chip, togdone_result);
	case FUSB_REG_STATUS1A_TOGSS_SRC1:
	case FUSB_REG_STATUS1A_TOGSS_SRC2:
		return fusb302_handle_togdone_src(chip, togdone_result);
	case FUSB_REG_STATUS1A_TOGSS_AA:
		/* doesn't support */
		fusb302_log(chip, "AudioAccessory not supported");
		fusb302_set_toggling(chip, chip->toggling_mode);
		break;
	default:
		fusb302_log(chip, "TOGDONE with an invalid state: %d",
			    togdone_result);
		fusb302_set_toggling(chip, chip->toggling_mode);
		break;
	}
	return ret;
}

static int fusb302_pd_reset(struct fusb302_chip *chip)
{
	return fusb302_i2c_set_bits(chip, FUSB_REG_RESET,
				    FUSB_REG_RESET_PD_RESET);
}

static int fusb302_pd_read_message(struct fusb302_chip *chip,
				   struct pd_message *msg)
{
	int ret = 0;
	u8 token;
	u8 crc[4];
	int len;

	/* first SOP token */
	ret = fusb302_i2c_read(chip, FUSB_REG_FIFOS, &token);
	if (ret < 0)
		return ret;
	ret = fusb302_i2c_block_read(chip, FUSB_REG_FIFOS, 2,
				     (u8 *)&msg->header);
	if (ret < 0)
		return ret;
	len = pd_header_cnt_le(msg->header) * 4;
	/* add 4 to length to include the CRC */
	if (len > PD_MAX_PAYLOAD * 4) {
		fusb302_log(chip, "PD message too long %d", len);
		return -EINVAL;
	}
	if (len > 0) {
		ret = fusb302_i2c_block_read(chip, FUSB_REG_FIFOS, len,
					     (u8 *)msg->payload);
		if (ret < 0)
			return ret;
	}
	/* another 4 bytes to read CRC out */
	ret = fusb302_i2c_block_read(chip, FUSB_REG_FIFOS, 4, crc);
	if (ret < 0)
		return ret;
	fusb302_log(chip, "PD message header: %x", msg->header);
	fusb302_log(chip, "PD message len: %d", len);

	/*
	 * Check if we've read off a GoodCRC message. If so then indicate to
	 * TCPM that the previous transmission has completed. Otherwise we pass
	 * the received message over to TCPM for processing.
	 *
	 * We make this check here instead of basing the reporting decision on
	 * the IRQ event type, as it's possible for the chip to report the
	 * TX_SUCCESS and GCRCSENT events out of order on occasion, so we need
	 * to check the message type to ensure correct reporting to TCPM.
	 */
	if ((!len) && (pd_header_type_le(msg->header) == PD_CTRL_GOOD_CRC))
		tcpm_pd_transmit_complete(chip->tcpm_port, TCPC_TX_SUCCESS);
	else
		tcpm_pd_receive(chip->tcpm_port, msg, TCPC_TX_SOP);

	return ret;
}

static irqreturn_t fusb302_irq_intn(int irq, void *dev_id)
{
	struct fusb302_chip *chip = dev_id;
	unsigned long flags;

	/* Disable our level triggered IRQ until our irq_work has cleared it */
	disable_irq_nosync(chip->gpio_int_n_irq);

	spin_lock_irqsave(&chip->irq_lock, flags);
	if (chip->irq_suspended)
		chip->irq_while_suspended = true;
	else
		schedule_work(&chip->irq_work);
	spin_unlock_irqrestore(&chip->irq_lock, flags);

	return IRQ_HANDLED;
}

static void fusb302_irq_work(struct work_struct *work)
{
	struct fusb302_chip *chip = container_of(work, struct fusb302_chip,
						 irq_work);
	int ret = 0;
	u8 interrupt;
	u8 interrupta;
	u8 interruptb;
	u8 status0;
	bool vbus_present;
	bool comp_result;
	bool intr_togdone;
	bool intr_bc_lvl;
	bool intr_comp_chng;
	struct pd_message pd_msg;

	mutex_lock(&chip->lock);
	/* grab a snapshot of intr flags */
	intr_togdone = chip->intr_togdone;
	intr_bc_lvl = chip->intr_bc_lvl;
	intr_comp_chng = chip->intr_comp_chng;

	ret = fusb302_i2c_read(chip, FUSB_REG_INTERRUPT, &interrupt);
	if (ret < 0)
		goto done;
	ret = fusb302_i2c_read(chip, FUSB_REG_INTERRUPTA, &interrupta);
	if (ret < 0)
		goto done;
	ret = fusb302_i2c_read(chip, FUSB_REG_INTERRUPTB, &interruptb);
	if (ret < 0)
		goto done;
	ret = fusb302_i2c_read(chip, FUSB_REG_STATUS0, &status0);
	if (ret < 0)
		goto done;
	fusb302_log(chip,
		    "IRQ: 0x%02x, a: 0x%02x, b: 0x%02x, status0: 0x%02x",
		    interrupt, interrupta, interruptb, status0);

	if (interrupt & FUSB_REG_INTERRUPT_VBUSOK) {
		vbus_present = !!(status0 & FUSB_REG_STATUS0_VBUSOK);
		fusb302_log(chip, "IRQ: VBUS_OK, vbus=%s",
			    vbus_present ? "On" : "Off");
		if (vbus_present != chip->vbus_present) {
			chip->vbus_present = vbus_present;
			tcpm_vbus_change(chip->tcpm_port);
		}
	}

	if ((interrupta & FUSB_REG_INTERRUPTA_TOGDONE) && intr_togdone) {
		fusb302_log(chip, "IRQ: TOGDONE");
		ret = fusb302_handle_togdone(chip);
		if (ret < 0) {
			fusb302_log(chip,
				    "handle togdone error, ret=%d", ret);
			goto done;
		}
	}

	if ((interrupt & FUSB_REG_INTERRUPT_BC_LVL) && intr_bc_lvl) {
		fusb302_log(chip, "IRQ: BC_LVL, handler pending");
		/*
		 * as BC_LVL interrupt can be affected by PD activity,
		 * apply delay to for the handler to wait for the PD
		 * signaling to finish.
		 */
		mod_delayed_work(chip->wq, &chip->bc_lvl_handler,
				 msecs_to_jiffies(T_BC_LVL_DEBOUNCE_DELAY_MS));
	}

	if ((interrupt & FUSB_REG_INTERRUPT_COMP_CHNG) && intr_comp_chng) {
		comp_result = !!(status0 & FUSB_REG_STATUS0_COMP);
		fusb302_log(chip, "IRQ: COMP_CHNG, comp=%s",
			    comp_result ? "true" : "false");
		if (comp_result) {
			/* cc level > Rd_threshold, detach */
			chip->cc1 = TYPEC_CC_OPEN;
			chip->cc2 = TYPEC_CC_OPEN;
			tcpm_cc_change(chip->tcpm_port);
		}
	}

	if (interrupt & FUSB_REG_INTERRUPT_COLLISION) {
		fusb302_log(chip, "IRQ: PD collision");
		tcpm_pd_transmit_complete(chip->tcpm_port, TCPC_TX_FAILED);
	}

	if (interrupta & FUSB_REG_INTERRUPTA_RETRYFAIL) {
		fusb302_log(chip, "IRQ: PD retry failed");
		tcpm_pd_transmit_complete(chip->tcpm_port, TCPC_TX_FAILED);
	}

	if (interrupta & FUSB_REG_INTERRUPTA_HARDSENT) {
		fusb302_log(chip, "IRQ: PD hardreset sent");
		ret = fusb302_pd_reset(chip);
		if (ret < 0) {
			fusb302_log(chip, "cannot PD reset, ret=%d", ret);
			goto done;
		}
		tcpm_pd_transmit_complete(chip->tcpm_port, TCPC_TX_SUCCESS);
	}

	if (interrupta & FUSB_REG_INTERRUPTA_TX_SUCCESS) {
		fusb302_log(chip, "IRQ: PD tx success");
		ret = fusb302_pd_read_message(chip, &pd_msg);
		if (ret < 0) {
			fusb302_log(chip,
				    "cannot read in PD message, ret=%d", ret);
			goto done;
		}
	}

	if (interrupta & FUSB_REG_INTERRUPTA_HARDRESET) {
		fusb302_log(chip, "IRQ: PD received hardreset");
		ret = fusb302_pd_reset(chip);
		if (ret < 0) {
			fusb302_log(chip, "cannot PD reset, ret=%d", ret);
			goto done;
		}
		tcpm_pd_hard_reset(chip->tcpm_port);
	}

	if (interruptb & FUSB_REG_INTERRUPTB_GCRCSENT) {
		fusb302_log(chip, "IRQ: PD sent good CRC");
		ret = fusb302_pd_read_message(chip, &pd_msg);
		if (ret < 0) {
			fusb302_log(chip,
				    "cannot read in PD message, ret=%d", ret);
			goto done;
		}
	}
done:
	mutex_unlock(&chip->lock);
	enable_irq(chip->gpio_int_n_irq);
}

static int init_gpio(struct fusb302_chip *chip)
{
	struct device *dev = chip->dev;
	int ret = 0;

	chip->gpio_int_n = devm_gpiod_get(dev, "fcs,int_n", GPIOD_IN);
	if (IS_ERR(chip->gpio_int_n)) {
		dev_err(dev, "failed to request gpio_int_n\n");
		return PTR_ERR(chip->gpio_int_n);
	}
	ret = gpiod_to_irq(chip->gpio_int_n);
	if (ret < 0) {
		dev_err(dev,
			"cannot request IRQ for GPIO Int_N, ret=%d", ret);
		return ret;
	}
	chip->gpio_int_n_irq = ret;
	return 0;
}

#define PDO_FIXED_FLAGS \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | PDO_FIXED_USB_COMM)

static const u32 src_pdo[] = {
	PDO_FIXED(5000, 400, PDO_FIXED_FLAGS)
};

static const u32 snk_pdo[] = {
	PDO_FIXED(5000, 400, PDO_FIXED_FLAGS)
};

static const struct property_entry port_props[] = {
	PROPERTY_ENTRY_STRING("data-role", "dual"),
	PROPERTY_ENTRY_STRING("power-role", "dual"),
	PROPERTY_ENTRY_STRING("try-power-role", "sink"),
	PROPERTY_ENTRY_U32_ARRAY("source-pdos", src_pdo),
	PROPERTY_ENTRY_U32_ARRAY("sink-pdos", snk_pdo),
	PROPERTY_ENTRY_U32("op-sink-microwatt", 2500000),
	{ }
};

static struct fwnode_handle *fusb302_fwnode_get(struct device *dev)
{
	struct fwnode_handle *fwnode;

	fwnode = device_get_named_child_node(dev, "connector");
	if (!fwnode)
		fwnode = fwnode_create_software_node(port_props, NULL);

	return fwnode;
}

static int fusb302_probe(struct i2c_client *client)
{
	struct fusb302_chip *chip;
	struct i2c_adapter *adapter = client->adapter;
	struct device *dev = &client->dev;
	const char *name;
	int ret = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_I2C_BLOCK)) {
		dev_err(&client->dev,
			"I2C/SMBus block functionality not supported!\n");
		return -ENODEV;
	}
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->i2c_client = client;
	chip->dev = &client->dev;
	mutex_init(&chip->lock);

	/*
	 * Devicetree platforms should get extcon via phandle (not yet
	 * supported). On ACPI platforms, we get the name from a device prop.
	 * This device prop is for kernel internal use only and is expected
	 * to be set by the platform code which also registers the i2c client
	 * for the fusb302.
	 */
	if (device_property_read_string(dev, "linux,extcon-name", &name) == 0) {
		chip->extcon = extcon_get_extcon_dev(name);
		if (IS_ERR(chip->extcon))
			return PTR_ERR(chip->extcon);
	}

	chip->vbus = devm_regulator_get(chip->dev, "vbus");
	if (IS_ERR(chip->vbus))
		return PTR_ERR(chip->vbus);

	chip->wq = create_singlethread_workqueue(dev_name(chip->dev));
	if (!chip->wq)
		return -ENOMEM;

	spin_lock_init(&chip->irq_lock);
	INIT_WORK(&chip->irq_work, fusb302_irq_work);
	INIT_DELAYED_WORK(&chip->bc_lvl_handler, fusb302_bc_lvl_handler_work);
	init_tcpc_dev(&chip->tcpc_dev);
	fusb302_debugfs_init(chip);

	if (client->irq) {
		chip->gpio_int_n_irq = client->irq;
	} else {
		ret = init_gpio(chip);
		if (ret < 0)
			goto destroy_workqueue;
	}

	chip->tcpc_dev.fwnode = fusb302_fwnode_get(dev);
	if (IS_ERR(chip->tcpc_dev.fwnode)) {
		ret = PTR_ERR(chip->tcpc_dev.fwnode);
		goto destroy_workqueue;
	}

	chip->tcpm_port = tcpm_register_port(&client->dev, &chip->tcpc_dev);
	if (IS_ERR(chip->tcpm_port)) {
		fwnode_handle_put(chip->tcpc_dev.fwnode);
		ret = dev_err_probe(dev, PTR_ERR(chip->tcpm_port),
				    "cannot register tcpm port\n");
		goto destroy_workqueue;
	}

	ret = request_irq(chip->gpio_int_n_irq, fusb302_irq_intn,
			  IRQF_ONESHOT | IRQF_TRIGGER_LOW,
			  "fsc_interrupt_int_n", chip);
	if (ret < 0) {
		dev_err(dev, "cannot request IRQ for GPIO Int_N, ret=%d", ret);
		goto tcpm_unregister_port;
	}
	enable_irq_wake(chip->gpio_int_n_irq);
	i2c_set_clientdata(client, chip);

	return ret;

tcpm_unregister_port:
	tcpm_unregister_port(chip->tcpm_port);
	fwnode_handle_put(chip->tcpc_dev.fwnode);
destroy_workqueue:
	fusb302_debugfs_exit(chip);
	destroy_workqueue(chip->wq);

	return ret;
}

static void fusb302_remove(struct i2c_client *client)
{
	struct fusb302_chip *chip = i2c_get_clientdata(client);

	disable_irq_wake(chip->gpio_int_n_irq);
	free_irq(chip->gpio_int_n_irq, chip);
	cancel_work_sync(&chip->irq_work);
	cancel_delayed_work_sync(&chip->bc_lvl_handler);
	tcpm_unregister_port(chip->tcpm_port);
	fwnode_handle_put(chip->tcpc_dev.fwnode);
	destroy_workqueue(chip->wq);
	fusb302_debugfs_exit(chip);
}

static int fusb302_pm_suspend(struct device *dev)
{
	struct fusb302_chip *chip = dev->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&chip->irq_lock, flags);
	chip->irq_suspended = true;
	spin_unlock_irqrestore(&chip->irq_lock, flags);

	/* Make sure any pending irq_work is finished before the bus suspends */
	flush_work(&chip->irq_work);
	return 0;
}

static int fusb302_pm_resume(struct device *dev)
{
	struct fusb302_chip *chip = dev->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&chip->irq_lock, flags);
	if (chip->irq_while_suspended) {
		schedule_work(&chip->irq_work);
		chip->irq_while_suspended = false;
	}
	chip->irq_suspended = false;
	spin_unlock_irqrestore(&chip->irq_lock, flags);

	return 0;
}

static const struct of_device_id fusb302_dt_match[] __maybe_unused = {
	{.compatible = "fcs,fusb302"},
	{},
};
MODULE_DEVICE_TABLE(of, fusb302_dt_match);

static const struct i2c_device_id fusb302_i2c_device_id[] = {
	{"typec_fusb302", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, fusb302_i2c_device_id);

static const struct dev_pm_ops fusb302_pm_ops = {
	.suspend = fusb302_pm_suspend,
	.resume = fusb302_pm_resume,
};

static struct i2c_driver fusb302_driver = {
	.driver = {
		   .name = "typec_fusb302",
		   .pm = &fusb302_pm_ops,
		   .of_match_table = of_match_ptr(fusb302_dt_match),
		   },
	.probe = fusb302_probe,
	.remove = fusb302_remove,
	.id_table = fusb302_i2c_device_id,
};
module_i2c_driver(fusb302_driver);

MODULE_AUTHOR("Yueyao Zhu <yueyao.zhu@gmail.com>");
MODULE_DESCRIPTION("Fairchild FUSB302 Type-C Chip Driver");
MODULE_LICENSE("GPL");
