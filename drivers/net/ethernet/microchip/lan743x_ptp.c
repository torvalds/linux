/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2018 Microchip Technology Inc. */

#include <linux/netdevice.h>

#include <linux/ptp_clock_kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/net_tstamp.h>
#include "lan743x_main.h"

#include "lan743x_ptp.h"

#define LAN743X_LED0_ENABLE		20	/* LED0 offset in HW_CFG */
#define LAN743X_LED_ENABLE(pin)		BIT(LAN743X_LED0_ENABLE + (pin))

#define LAN743X_PTP_MAX_FREQ_ADJ_IN_PPB		(31249999)
#define LAN743X_PTP_MAX_FINE_ADJ_IN_SCALED_PPM	(2047999934)

static bool lan743x_ptp_is_enabled(struct lan743x_adapter *adapter);
static void lan743x_ptp_enable(struct lan743x_adapter *adapter);
static void lan743x_ptp_disable(struct lan743x_adapter *adapter);
static void lan743x_ptp_reset(struct lan743x_adapter *adapter);
static void lan743x_ptp_clock_set(struct lan743x_adapter *adapter,
				  u32 seconds, u32 nano_seconds,
				  u32 sub_nano_seconds);

static int lan743x_get_channel(u32 ch_map)
{
	int idx;

	for (idx = 0; idx < 32; idx++) {
		if (ch_map & (0x1 << idx))
			return idx;
	}

	return -EINVAL;
}

int lan743x_gpio_init(struct lan743x_adapter *adapter)
{
	struct lan743x_gpio *gpio = &adapter->gpio;

	spin_lock_init(&gpio->gpio_lock);

	gpio->gpio_cfg0 = 0; /* set all direction to input, data = 0 */
	gpio->gpio_cfg1 = 0x0FFF0000;/* disable all gpio, set to open drain */
	gpio->gpio_cfg2 = 0;/* set all to 1588 low polarity level */
	gpio->gpio_cfg3 = 0;/* disable all 1588 output */
	lan743x_csr_write(adapter, GPIO_CFG0, gpio->gpio_cfg0);
	lan743x_csr_write(adapter, GPIO_CFG1, gpio->gpio_cfg1);
	lan743x_csr_write(adapter, GPIO_CFG2, gpio->gpio_cfg2);
	lan743x_csr_write(adapter, GPIO_CFG3, gpio->gpio_cfg3);

	return 0;
}

static void lan743x_ptp_wait_till_cmd_done(struct lan743x_adapter *adapter,
					   u32 bit_mask)
{
	int timeout = PTP_CMD_CTL_TIMEOUT_CNT;
	u32 data = 0;

	while (timeout &&
	       (data = (lan743x_csr_read(adapter, PTP_CMD_CTL) &
	       bit_mask))) {
		usleep_range(1000, 20000);
		timeout--;
	}
	if (data) {
		netif_err(adapter, drv, adapter->netdev,
			  "timeout waiting for cmd to be done, cmd = 0x%08X\n",
			  bit_mask);
	}
}

static void lan743x_ptp_tx_ts_enqueue_ts(struct lan743x_adapter *adapter,
					 u32 seconds, u32 nano_seconds,
					 u32 header)
{
	struct lan743x_ptp *ptp = &adapter->ptp;

	spin_lock_bh(&ptp->tx_ts_lock);
	if (ptp->tx_ts_queue_size < LAN743X_PTP_NUMBER_OF_TX_TIMESTAMPS) {
		ptp->tx_ts_seconds_queue[ptp->tx_ts_queue_size] = seconds;
		ptp->tx_ts_nseconds_queue[ptp->tx_ts_queue_size] = nano_seconds;
		ptp->tx_ts_header_queue[ptp->tx_ts_queue_size] = header;
		ptp->tx_ts_queue_size++;
	} else {
		netif_err(adapter, drv, adapter->netdev,
			  "tx ts queue overflow\n");
	}
	spin_unlock_bh(&ptp->tx_ts_lock);
}

static void lan743x_ptp_tx_ts_complete(struct lan743x_adapter *adapter)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	struct skb_shared_hwtstamps tstamps;
	u32 header, nseconds, seconds;
	bool ignore_sync = false;
	struct sk_buff *skb;
	int c, i;

	spin_lock_bh(&ptp->tx_ts_lock);
	c = ptp->tx_ts_skb_queue_size;

	if (c > ptp->tx_ts_queue_size)
		c = ptp->tx_ts_queue_size;
	if (c <= 0)
		goto done;

	for (i = 0; i < c; i++) {
		ignore_sync = ((ptp->tx_ts_ignore_sync_queue &
				BIT(i)) != 0);
		skb = ptp->tx_ts_skb_queue[i];
		nseconds = ptp->tx_ts_nseconds_queue[i];
		seconds = ptp->tx_ts_seconds_queue[i];
		header = ptp->tx_ts_header_queue[i];

		memset(&tstamps, 0, sizeof(tstamps));
		tstamps.hwtstamp = ktime_set(seconds, nseconds);
		if (!ignore_sync ||
		    ((header & PTP_TX_MSG_HEADER_MSG_TYPE_) !=
		    PTP_TX_MSG_HEADER_MSG_TYPE_SYNC_))
			skb_tstamp_tx(skb, &tstamps);

		dev_kfree_skb(skb);

		ptp->tx_ts_skb_queue[i] = NULL;
		ptp->tx_ts_seconds_queue[i] = 0;
		ptp->tx_ts_nseconds_queue[i] = 0;
		ptp->tx_ts_header_queue[i] = 0;
	}

	/* shift queue */
	ptp->tx_ts_ignore_sync_queue >>= c;
	for (i = c; i < LAN743X_PTP_NUMBER_OF_TX_TIMESTAMPS; i++) {
		ptp->tx_ts_skb_queue[i - c] = ptp->tx_ts_skb_queue[i];
		ptp->tx_ts_seconds_queue[i - c] = ptp->tx_ts_seconds_queue[i];
		ptp->tx_ts_nseconds_queue[i - c] = ptp->tx_ts_nseconds_queue[i];
		ptp->tx_ts_header_queue[i - c] = ptp->tx_ts_header_queue[i];

		ptp->tx_ts_skb_queue[i] = NULL;
		ptp->tx_ts_seconds_queue[i] = 0;
		ptp->tx_ts_nseconds_queue[i] = 0;
		ptp->tx_ts_header_queue[i] = 0;
	}
	ptp->tx_ts_skb_queue_size -= c;
	ptp->tx_ts_queue_size -= c;
done:
	ptp->pending_tx_timestamps -= c;
	spin_unlock_bh(&ptp->tx_ts_lock);
}

static int lan743x_ptp_reserve_event_ch(struct lan743x_adapter *adapter,
					int event_channel)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	int result = -ENODEV;

	mutex_lock(&ptp->command_lock);
	if (!(test_bit(event_channel, &ptp->used_event_ch))) {
		ptp->used_event_ch |= BIT(event_channel);
		result = event_channel;
	} else {
		netif_warn(adapter, drv, adapter->netdev,
			   "attempted to reserved a used event_channel = %d\n",
			   event_channel);
	}
	mutex_unlock(&ptp->command_lock);
	return result;
}

static void lan743x_ptp_release_event_ch(struct lan743x_adapter *adapter,
					 int event_channel)
{
	struct lan743x_ptp *ptp = &adapter->ptp;

	mutex_lock(&ptp->command_lock);
	if (test_bit(event_channel, &ptp->used_event_ch)) {
		ptp->used_event_ch &= ~BIT(event_channel);
	} else {
		netif_warn(adapter, drv, adapter->netdev,
			   "attempted release on a not used event_channel = %d\n",
			   event_channel);
	}
	mutex_unlock(&ptp->command_lock);
}

static void lan743x_ptp_clock_get(struct lan743x_adapter *adapter,
				  u32 *seconds, u32 *nano_seconds,
				  u32 *sub_nano_seconds);
static void lan743x_ptp_io_clock_get(struct lan743x_adapter *adapter,
				     u32 *sec, u32 *nsec, u32 *sub_nsec);
static void lan743x_ptp_clock_step(struct lan743x_adapter *adapter,
				   s64 time_step_ns);

static void lan743x_led_mux_enable(struct lan743x_adapter *adapter,
				   int pin, bool enable)
{
	struct lan743x_ptp *ptp = &adapter->ptp;

	if (ptp->leds_multiplexed &&
	    ptp->led_enabled[pin]) {
		u32 val = lan743x_csr_read(adapter, HW_CFG);

		if (enable)
			val |= LAN743X_LED_ENABLE(pin);
		else
			val &= ~LAN743X_LED_ENABLE(pin);

		lan743x_csr_write(adapter, HW_CFG, val);
	}
}

static void lan743x_led_mux_save(struct lan743x_adapter *adapter)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	u32 id_rev = adapter->csr.id_rev & ID_REV_ID_MASK_;

	if (id_rev == ID_REV_ID_LAN7430_) {
		int i;
		u32 val = lan743x_csr_read(adapter, HW_CFG);

		for (i = 0; i < LAN7430_N_LED; i++) {
			bool led_enabled = (val & LAN743X_LED_ENABLE(i)) != 0;

			ptp->led_enabled[i] = led_enabled;
		}
		ptp->leds_multiplexed = true;
	} else {
		ptp->leds_multiplexed = false;
	}
}

static void lan743x_led_mux_restore(struct lan743x_adapter *adapter)
{
	u32 id_rev = adapter->csr.id_rev & ID_REV_ID_MASK_;

	if (id_rev == ID_REV_ID_LAN7430_) {
		int i;

		for (i = 0; i < LAN7430_N_LED; i++)
			lan743x_led_mux_enable(adapter, i, true);
	}
}

static int lan743x_gpio_rsrv_ptp_out(struct lan743x_adapter *adapter,
				     int pin, int event_channel)
{
	struct lan743x_gpio *gpio = &adapter->gpio;
	unsigned long irq_flags = 0;
	int bit_mask = BIT(pin);
	int ret = -EBUSY;

	spin_lock_irqsave(&gpio->gpio_lock, irq_flags);

	if (!(gpio->used_bits & bit_mask)) {
		gpio->used_bits |= bit_mask;
		gpio->output_bits |= bit_mask;
		gpio->ptp_bits |= bit_mask;

		/* assign pin to GPIO function */
		lan743x_led_mux_enable(adapter, pin, false);

		/* set as output, and zero initial value */
		gpio->gpio_cfg0 |= GPIO_CFG0_GPIO_DIR_BIT_(pin);
		gpio->gpio_cfg0 &= ~GPIO_CFG0_GPIO_DATA_BIT_(pin);
		lan743x_csr_write(adapter, GPIO_CFG0, gpio->gpio_cfg0);

		/* enable gpio, and set buffer type to push pull */
		gpio->gpio_cfg1 &= ~GPIO_CFG1_GPIOEN_BIT_(pin);
		gpio->gpio_cfg1 |= GPIO_CFG1_GPIOBUF_BIT_(pin);
		lan743x_csr_write(adapter, GPIO_CFG1, gpio->gpio_cfg1);

		/* set 1588 polarity to high */
		gpio->gpio_cfg2 |= GPIO_CFG2_1588_POL_BIT_(pin);
		lan743x_csr_write(adapter, GPIO_CFG2, gpio->gpio_cfg2);

		if (event_channel == 0) {
			/* use channel A */
			gpio->gpio_cfg3 &= ~GPIO_CFG3_1588_CH_SEL_BIT_(pin);
		} else {
			/* use channel B */
			gpio->gpio_cfg3 |= GPIO_CFG3_1588_CH_SEL_BIT_(pin);
		}
		gpio->gpio_cfg3 |= GPIO_CFG3_1588_OE_BIT_(pin);
		lan743x_csr_write(adapter, GPIO_CFG3, gpio->gpio_cfg3);

		ret = pin;
	}
	spin_unlock_irqrestore(&gpio->gpio_lock, irq_flags);
	return ret;
}

static void lan743x_gpio_release(struct lan743x_adapter *adapter, int pin)
{
	struct lan743x_gpio *gpio = &adapter->gpio;
	unsigned long irq_flags = 0;
	int bit_mask = BIT(pin);

	spin_lock_irqsave(&gpio->gpio_lock, irq_flags);
	if (gpio->used_bits & bit_mask) {
		gpio->used_bits &= ~bit_mask;
		if (gpio->output_bits & bit_mask) {
			gpio->output_bits &= ~bit_mask;

			if (gpio->ptp_bits & bit_mask) {
				gpio->ptp_bits &= ~bit_mask;
				/* disable ptp output */
				gpio->gpio_cfg3 &= ~GPIO_CFG3_1588_OE_BIT_(pin);
				lan743x_csr_write(adapter, GPIO_CFG3,
						  gpio->gpio_cfg3);
			}
			/* release gpio output */

			/* disable gpio */
			gpio->gpio_cfg1 |= GPIO_CFG1_GPIOEN_BIT_(pin);
			gpio->gpio_cfg1 &= ~GPIO_CFG1_GPIOBUF_BIT_(pin);
			lan743x_csr_write(adapter, GPIO_CFG1, gpio->gpio_cfg1);

			/* reset back to input */
			gpio->gpio_cfg0 &= ~GPIO_CFG0_GPIO_DIR_BIT_(pin);
			gpio->gpio_cfg0 &= ~GPIO_CFG0_GPIO_DATA_BIT_(pin);
			lan743x_csr_write(adapter, GPIO_CFG0, gpio->gpio_cfg0);

			/* assign pin to original function */
			lan743x_led_mux_enable(adapter, pin, true);
		}
	}
	spin_unlock_irqrestore(&gpio->gpio_lock, irq_flags);
}

static int lan743x_ptpci_adjfine(struct ptp_clock_info *ptpci, long scaled_ppm)
{
	struct lan743x_ptp *ptp =
		container_of(ptpci, struct lan743x_ptp, ptp_clock_info);
	struct lan743x_adapter *adapter =
		container_of(ptp, struct lan743x_adapter, ptp);
	u32 lan743x_rate_adj = 0;
	u64 u64_delta;

	if ((scaled_ppm < (-LAN743X_PTP_MAX_FINE_ADJ_IN_SCALED_PPM)) ||
	    scaled_ppm > LAN743X_PTP_MAX_FINE_ADJ_IN_SCALED_PPM) {
		return -EINVAL;
	}

	/* diff_by_scaled_ppm returns true if the difference is negative */
	if (diff_by_scaled_ppm(1ULL << 35, scaled_ppm, &u64_delta))
		lan743x_rate_adj = (u32)u64_delta;
	else
		lan743x_rate_adj = (u32)u64_delta | PTP_CLOCK_RATE_ADJ_DIR_;

	lan743x_csr_write(adapter, PTP_CLOCK_RATE_ADJ,
			  lan743x_rate_adj);

	return 0;
}

static int lan743x_ptpci_adjtime(struct ptp_clock_info *ptpci, s64 delta)
{
	struct lan743x_ptp *ptp =
		container_of(ptpci, struct lan743x_ptp, ptp_clock_info);
	struct lan743x_adapter *adapter =
		container_of(ptp, struct lan743x_adapter, ptp);

	lan743x_ptp_clock_step(adapter, delta);

	return 0;
}

static int lan743x_ptpci_gettime64(struct ptp_clock_info *ptpci,
				   struct timespec64 *ts)
{
	struct lan743x_ptp *ptp =
		container_of(ptpci, struct lan743x_ptp, ptp_clock_info);
	struct lan743x_adapter *adapter =
		container_of(ptp, struct lan743x_adapter, ptp);
	u32 nano_seconds = 0;
	u32 seconds = 0;

	if (adapter->is_pci11x1x)
		lan743x_ptp_io_clock_get(adapter, &seconds, &nano_seconds,
					 NULL);
	else
		lan743x_ptp_clock_get(adapter, &seconds, &nano_seconds, NULL);
	ts->tv_sec = seconds;
	ts->tv_nsec = nano_seconds;

	return 0;
}

static int lan743x_ptpci_settime64(struct ptp_clock_info *ptpci,
				   const struct timespec64 *ts)
{
	struct lan743x_ptp *ptp =
		container_of(ptpci, struct lan743x_ptp, ptp_clock_info);
	struct lan743x_adapter *adapter =
		container_of(ptp, struct lan743x_adapter, ptp);
	u32 nano_seconds = 0;
	u32 seconds = 0;

	if (ts->tv_sec > 0xFFFFFFFFLL) {
		netif_warn(adapter, drv, adapter->netdev,
			   "ts->tv_sec out of range, %lld\n",
			   ts->tv_sec);
		return -ERANGE;
	}
	if (ts->tv_nsec < 0) {
		netif_warn(adapter, drv, adapter->netdev,
			   "ts->tv_nsec out of range, %ld\n",
			   ts->tv_nsec);
		return -ERANGE;
	}
	seconds = ts->tv_sec;
	nano_seconds = ts->tv_nsec;
	lan743x_ptp_clock_set(adapter, seconds, nano_seconds, 0);

	return 0;
}

static void lan743x_ptp_perout_off(struct lan743x_adapter *adapter,
				   unsigned int index)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	u32 general_config = 0;
	struct lan743x_ptp_perout *perout = &ptp->perout[index];

	if (perout->gpio_pin >= 0) {
		lan743x_gpio_release(adapter, perout->gpio_pin);
		perout->gpio_pin = -1;
	}

	if (perout->event_ch >= 0) {
		/* set target to far in the future, effectively disabling it */
		lan743x_csr_write(adapter,
				  PTP_CLOCK_TARGET_SEC_X(perout->event_ch),
				  0xFFFF0000);
		lan743x_csr_write(adapter,
				  PTP_CLOCK_TARGET_NS_X(perout->event_ch),
				  0);

		general_config = lan743x_csr_read(adapter, PTP_GENERAL_CONFIG);
		general_config |= PTP_GENERAL_CONFIG_RELOAD_ADD_X_
				  (perout->event_ch);
		lan743x_csr_write(adapter, PTP_GENERAL_CONFIG, general_config);
		lan743x_ptp_release_event_ch(adapter, perout->event_ch);
		perout->event_ch = -1;
	}
}

static int lan743x_ptp_perout(struct lan743x_adapter *adapter, int on,
			      struct ptp_perout_request *perout_request)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	u32 period_sec = 0, period_nsec = 0;
	u32 start_sec = 0, start_nsec = 0;
	u32 general_config = 0;
	int pulse_width = 0;
	int perout_pin = 0;
	unsigned int index = perout_request->index;
	struct lan743x_ptp_perout *perout = &ptp->perout[index];
	int ret = 0;

	/* Reject requests with unsupported flags */
	if (perout_request->flags & ~PTP_PEROUT_DUTY_CYCLE)
		return -EOPNOTSUPP;

	if (on) {
		perout_pin = ptp_find_pin(ptp->ptp_clock, PTP_PF_PEROUT,
					  perout_request->index);
		if (perout_pin < 0)
			return -EBUSY;
	} else {
		lan743x_ptp_perout_off(adapter, index);
		return 0;
	}

	if (perout->event_ch >= 0 ||
	    perout->gpio_pin >= 0) {
		/* already on, turn off first */
		lan743x_ptp_perout_off(adapter, index);
	}

	perout->event_ch = lan743x_ptp_reserve_event_ch(adapter, index);

	if (perout->event_ch < 0) {
		netif_warn(adapter, drv, adapter->netdev,
			   "Failed to reserve event channel %d for PEROUT\n",
			   index);
		ret = -EBUSY;
		goto failed;
	}

	perout->gpio_pin = lan743x_gpio_rsrv_ptp_out(adapter,
						     perout_pin,
						     perout->event_ch);

	if (perout->gpio_pin < 0) {
		netif_warn(adapter, drv, adapter->netdev,
			   "Failed to reserve gpio %d for PEROUT\n",
			   perout_pin);
		ret = -EBUSY;
		goto failed;
	}

	start_sec = perout_request->start.sec;
	start_sec += perout_request->start.nsec / 1000000000;
	start_nsec = perout_request->start.nsec % 1000000000;

	period_sec = perout_request->period.sec;
	period_sec += perout_request->period.nsec / 1000000000;
	period_nsec = perout_request->period.nsec % 1000000000;

	if (perout_request->flags & PTP_PEROUT_DUTY_CYCLE) {
		struct timespec64 ts_on, ts_period;
		s64 wf_high, period64, half;
		s32 reminder;

		ts_on.tv_sec = perout_request->on.sec;
		ts_on.tv_nsec = perout_request->on.nsec;
		wf_high = timespec64_to_ns(&ts_on);
		ts_period.tv_sec = perout_request->period.sec;
		ts_period.tv_nsec = perout_request->period.nsec;
		period64 = timespec64_to_ns(&ts_period);

		if (period64 < 200) {
			netif_warn(adapter, drv, adapter->netdev,
				   "perout period too small, minimum is 200nS\n");
			ret = -EOPNOTSUPP;
			goto failed;
		}
		if (wf_high >= period64) {
			netif_warn(adapter, drv, adapter->netdev,
				   "pulse width must be smaller than period\n");
			ret = -EINVAL;
			goto failed;
		}

		/* Check if we can do 50% toggle on an even value of period.
		 * If the period number is odd, then check if the requested
		 * pulse width is the same as one of pre-defined width values.
		 * Otherwise, return failure.
		 */
		half = div_s64_rem(period64, 2, &reminder);
		if (!reminder) {
			if (half == wf_high) {
				/* It's 50% match. Use the toggle option */
				pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_TOGGLE_;
				/* In this case, divide period value by 2 */
				ts_period = ns_to_timespec64(div_s64(period64, 2));
				period_sec = ts_period.tv_sec;
				period_nsec = ts_period.tv_nsec;

				goto program;
			}
		}
		/* if we can't do toggle, then the width option needs to be the exact match */
		if (wf_high == 200000000) {
			pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_200MS_;
		} else if (wf_high == 10000000) {
			pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_10MS_;
		} else if (wf_high == 1000000) {
			pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_1MS_;
		} else if (wf_high == 100000) {
			pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_100US_;
		} else if (wf_high == 10000) {
			pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_10US_;
		} else if (wf_high == 100) {
			pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_100NS_;
		} else {
			netif_warn(adapter, drv, adapter->netdev,
				   "duty cycle specified is not supported\n");
			ret = -EOPNOTSUPP;
			goto failed;
		}
	} else {
		if (period_sec == 0) {
			if (period_nsec >= 400000000) {
				pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_200MS_;
			} else if (period_nsec >= 20000000) {
				pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_10MS_;
			} else if (period_nsec >= 2000000) {
				pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_1MS_;
			} else if (period_nsec >= 200000) {
				pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_100US_;
			} else if (period_nsec >= 20000) {
				pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_10US_;
			} else if (period_nsec >= 200) {
				pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_100NS_;
			} else {
				netif_warn(adapter, drv, adapter->netdev,
					   "perout period too small, minimum is 200nS\n");
				ret = -EOPNOTSUPP;
				goto failed;
			}
		} else {
			pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_200MS_;
		}
	}
program:

	/* turn off by setting target far in future */
	lan743x_csr_write(adapter,
			  PTP_CLOCK_TARGET_SEC_X(perout->event_ch),
			  0xFFFF0000);
	lan743x_csr_write(adapter,
			  PTP_CLOCK_TARGET_NS_X(perout->event_ch), 0);

	/* Configure to pulse every period */
	general_config = lan743x_csr_read(adapter, PTP_GENERAL_CONFIG);
	general_config &= ~(PTP_GENERAL_CONFIG_CLOCK_EVENT_X_MASK_
			  (perout->event_ch));
	general_config |= PTP_GENERAL_CONFIG_CLOCK_EVENT_X_SET_
			  (perout->event_ch, pulse_width);
	general_config &= ~PTP_GENERAL_CONFIG_RELOAD_ADD_X_
			  (perout->event_ch);
	lan743x_csr_write(adapter, PTP_GENERAL_CONFIG, general_config);

	/* set the reload to one toggle cycle */
	lan743x_csr_write(adapter,
			  PTP_CLOCK_TARGET_RELOAD_SEC_X(perout->event_ch),
			  period_sec);
	lan743x_csr_write(adapter,
			  PTP_CLOCK_TARGET_RELOAD_NS_X(perout->event_ch),
			  period_nsec);

	/* set the start time */
	lan743x_csr_write(adapter,
			  PTP_CLOCK_TARGET_SEC_X(perout->event_ch),
			  start_sec);
	lan743x_csr_write(adapter,
			  PTP_CLOCK_TARGET_NS_X(perout->event_ch),
			  start_nsec);

	return 0;

failed:
	lan743x_ptp_perout_off(adapter, index);
	return ret;
}

static void lan743x_ptp_io_perout_off(struct lan743x_adapter *adapter,
				      u32 index)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	int perout_pin;
	int event_ch;
	u32 gen_cfg;
	int val;

	event_ch = ptp->ptp_io_perout[index];
	if (event_ch >= 0) {
		/* set target to far in the future, effectively disabling it */
		lan743x_csr_write(adapter,
				  PTP_CLOCK_TARGET_SEC_X(event_ch),
				  0xFFFF0000);
		lan743x_csr_write(adapter,
				  PTP_CLOCK_TARGET_NS_X(event_ch),
				  0);

		gen_cfg = lan743x_csr_read(adapter, HS_PTP_GENERAL_CONFIG);
		gen_cfg &= ~(HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_X_MASK_
				    (event_ch));
		gen_cfg &= ~(HS_PTP_GENERAL_CONFIG_EVENT_POL_X_(event_ch));
		gen_cfg |= HS_PTP_GENERAL_CONFIG_RELOAD_ADD_X_(event_ch);
		lan743x_csr_write(adapter, HS_PTP_GENERAL_CONFIG, gen_cfg);
		if (event_ch)
			lan743x_csr_write(adapter, PTP_INT_STS,
					  PTP_INT_TIMER_INT_B_);
		else
			lan743x_csr_write(adapter, PTP_INT_STS,
					  PTP_INT_TIMER_INT_A_);
		lan743x_ptp_release_event_ch(adapter, event_ch);
		ptp->ptp_io_perout[index] = -1;
	}

	perout_pin = ptp_find_pin(ptp->ptp_clock, PTP_PF_PEROUT, index);

	/* Deselect Event output */
	val = lan743x_csr_read(adapter, PTP_IO_EVENT_OUTPUT_CFG);

	/* Disables the output of Local Time Target compare events */
	val &= ~PTP_IO_EVENT_OUTPUT_CFG_EN_(perout_pin);
	lan743x_csr_write(adapter, PTP_IO_EVENT_OUTPUT_CFG, val);

	/* Configured as an opendrain driver*/
	val = lan743x_csr_read(adapter, PTP_IO_PIN_CFG);
	val &= ~PTP_IO_PIN_CFG_OBUF_TYPE_(perout_pin);
	lan743x_csr_write(adapter, PTP_IO_PIN_CFG, val);
	/* Dummy read to make sure write operation success */
	val = lan743x_csr_read(adapter, PTP_IO_PIN_CFG);
}

static int lan743x_ptp_io_perout(struct lan743x_adapter *adapter, int on,
				 struct ptp_perout_request *perout_request)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	u32 period_sec, period_nsec;
	u32 start_sec, start_nsec;
	u32 pulse_sec, pulse_nsec;
	int pulse_width;
	int perout_pin;
	int event_ch;
	u32 gen_cfg;
	u32 index;
	int val;

	index = perout_request->index;
	event_ch = ptp->ptp_io_perout[index];

	if (on) {
		perout_pin = ptp_find_pin(ptp->ptp_clock, PTP_PF_PEROUT, index);
		if (perout_pin < 0)
			return -EBUSY;
	} else {
		lan743x_ptp_io_perout_off(adapter, index);
		return 0;
	}

	if (event_ch >= LAN743X_PTP_N_EVENT_CHAN) {
		/* already on, turn off first */
		lan743x_ptp_io_perout_off(adapter, index);
	}

	event_ch = lan743x_ptp_reserve_event_ch(adapter, index);
	if (event_ch < 0) {
		netif_warn(adapter, drv, adapter->netdev,
			   "Failed to reserve event channel %d for PEROUT\n",
			   index);
		goto failed;
	}
	ptp->ptp_io_perout[index] = event_ch;

	if (perout_request->flags & PTP_PEROUT_DUTY_CYCLE) {
		pulse_sec = perout_request->on.sec;
		pulse_sec += perout_request->on.nsec / 1000000000;
		pulse_nsec = perout_request->on.nsec % 1000000000;
	} else {
		pulse_sec = perout_request->period.sec;
		pulse_sec += perout_request->period.nsec / 1000000000;
		pulse_nsec = perout_request->period.nsec % 1000000000;
	}

	if (pulse_sec == 0) {
		if (pulse_nsec >= 400000000) {
			pulse_width = PTP_GENERAL_CONFIG_CLOCK_EVENT_200MS_;
		} else if (pulse_nsec >= 200000000) {
			pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_100MS_;
		} else if (pulse_nsec >= 100000000) {
			pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_50MS_;
		} else if (pulse_nsec >= 20000000) {
			pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_10MS_;
		} else if (pulse_nsec >= 10000000) {
			pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_5MS_;
		} else if (pulse_nsec >= 2000000) {
			pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_1MS_;
		} else if (pulse_nsec >= 1000000) {
			pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_500US_;
		} else if (pulse_nsec >= 200000) {
			pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_100US_;
		} else if (pulse_nsec >= 100000) {
			pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_50US_;
		} else if (pulse_nsec >= 20000) {
			pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_10US_;
		} else if (pulse_nsec >= 10000) {
			pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_5US_;
		} else if (pulse_nsec >= 2000) {
			pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_1US_;
		} else if (pulse_nsec >= 1000) {
			pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_500NS_;
		} else if (pulse_nsec >= 200) {
			pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_100NS_;
		} else {
			netif_warn(adapter, drv, adapter->netdev,
				   "perout period too small, min is 200nS\n");
			goto failed;
		}
	} else {
		pulse_width = HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_200MS_;
	}

	/* turn off by setting target far in future */
	lan743x_csr_write(adapter,
			  PTP_CLOCK_TARGET_SEC_X(event_ch),
			  0xFFFF0000);
	lan743x_csr_write(adapter,
			  PTP_CLOCK_TARGET_NS_X(event_ch), 0);

	/* Configure to pulse every period */
	gen_cfg = lan743x_csr_read(adapter, HS_PTP_GENERAL_CONFIG);
	gen_cfg &= ~(HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_X_MASK_(event_ch));
	gen_cfg |= HS_PTP_GENERAL_CONFIG_CLOCK_EVENT_X_SET_
			  (event_ch, pulse_width);
	gen_cfg |= HS_PTP_GENERAL_CONFIG_EVENT_POL_X_(event_ch);
	gen_cfg &= ~(HS_PTP_GENERAL_CONFIG_RELOAD_ADD_X_(event_ch));
	lan743x_csr_write(adapter, HS_PTP_GENERAL_CONFIG, gen_cfg);

	/* set the reload to one toggle cycle */
	period_sec = perout_request->period.sec;
	period_sec += perout_request->period.nsec / 1000000000;
	period_nsec = perout_request->period.nsec % 1000000000;
	lan743x_csr_write(adapter,
			  PTP_CLOCK_TARGET_RELOAD_SEC_X(event_ch),
			  period_sec);
	lan743x_csr_write(adapter,
			  PTP_CLOCK_TARGET_RELOAD_NS_X(event_ch),
			  period_nsec);

	start_sec = perout_request->start.sec;
	start_sec += perout_request->start.nsec / 1000000000;
	start_nsec = perout_request->start.nsec % 1000000000;

	/* set the start time */
	lan743x_csr_write(adapter,
			  PTP_CLOCK_TARGET_SEC_X(event_ch),
			  start_sec);
	lan743x_csr_write(adapter,
			  PTP_CLOCK_TARGET_NS_X(event_ch),
			  start_nsec);

	/* Enable LTC Target Read */
	val = lan743x_csr_read(adapter, PTP_CMD_CTL);
	val |= PTP_CMD_CTL_PTP_LTC_TARGET_READ_;
	lan743x_csr_write(adapter, PTP_CMD_CTL, val);

	/* Configure as an push/pull driver */
	val = lan743x_csr_read(adapter, PTP_IO_PIN_CFG);
	val |= PTP_IO_PIN_CFG_OBUF_TYPE_(perout_pin);
	lan743x_csr_write(adapter, PTP_IO_PIN_CFG, val);

	/* Select Event output */
	val = lan743x_csr_read(adapter, PTP_IO_EVENT_OUTPUT_CFG);
	if (event_ch)
		/* Channel B as the output */
		val |= PTP_IO_EVENT_OUTPUT_CFG_SEL_(perout_pin);
	else
		/* Channel A as the output */
		val &= ~PTP_IO_EVENT_OUTPUT_CFG_SEL_(perout_pin);

	/* Enables the output of Local Time Target compare events */
	val |= PTP_IO_EVENT_OUTPUT_CFG_EN_(perout_pin);
	lan743x_csr_write(adapter, PTP_IO_EVENT_OUTPUT_CFG, val);

	return 0;

failed:
	lan743x_ptp_io_perout_off(adapter, index);
	return -ENODEV;
}

static void lan743x_ptp_io_extts_off(struct lan743x_adapter *adapter,
				     u32 index)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	struct lan743x_extts *extts;
	int val;

	extts = &ptp->extts[index];
	/* PTP Interrupt Enable Clear Register */
	if (extts->flags & PTP_FALLING_EDGE)
		val = PTP_INT_EN_FE_EN_CLR_(index);
	else
		val = PTP_INT_EN_RE_EN_CLR_(index);
	lan743x_csr_write(adapter, PTP_INT_EN_CLR, val);

	/* Disables PTP-IO edge lock */
	val = lan743x_csr_read(adapter, PTP_IO_CAP_CONFIG);
	if (extts->flags & PTP_FALLING_EDGE) {
		val &= ~PTP_IO_CAP_CONFIG_LOCK_FE_(index);
		val &= ~PTP_IO_CAP_CONFIG_FE_CAP_EN_(index);
	} else {
		val &= ~PTP_IO_CAP_CONFIG_LOCK_RE_(index);
		val &= ~PTP_IO_CAP_CONFIG_RE_CAP_EN_(index);
	}
	lan743x_csr_write(adapter, PTP_IO_CAP_CONFIG, val);

	/* PTP-IO De-select register */
	val = lan743x_csr_read(adapter, PTP_IO_SEL);
	val &= ~PTP_IO_SEL_MASK_;
	lan743x_csr_write(adapter, PTP_IO_SEL, val);

	/* Clear timestamp */
	memset(&extts->ts, 0, sizeof(struct timespec64));
	extts->flags = 0;
}

static int lan743x_ptp_io_event_cap_en(struct lan743x_adapter *adapter,
				       u32 flags, u32 channel)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	int val;

	if ((flags & PTP_EXTTS_EDGES) ==  PTP_EXTTS_EDGES)
		return -EOPNOTSUPP;

	mutex_lock(&ptp->command_lock);
	/* PTP-IO Event Capture Enable */
	val = lan743x_csr_read(adapter, PTP_IO_CAP_CONFIG);
	if (flags & PTP_FALLING_EDGE) {
		val &= ~PTP_IO_CAP_CONFIG_LOCK_RE_(channel);
		val &= ~PTP_IO_CAP_CONFIG_RE_CAP_EN_(channel);
		val |= PTP_IO_CAP_CONFIG_LOCK_FE_(channel);
		val |= PTP_IO_CAP_CONFIG_FE_CAP_EN_(channel);
	} else {
		/* Rising eventing as Default */
		val &= ~PTP_IO_CAP_CONFIG_LOCK_FE_(channel);
		val &= ~PTP_IO_CAP_CONFIG_FE_CAP_EN_(channel);
		val |= PTP_IO_CAP_CONFIG_LOCK_RE_(channel);
		val |= PTP_IO_CAP_CONFIG_RE_CAP_EN_(channel);
	}
	lan743x_csr_write(adapter, PTP_IO_CAP_CONFIG, val);

	/* PTP-IO Select */
	val = lan743x_csr_read(adapter, PTP_IO_SEL);
	val &= ~PTP_IO_SEL_MASK_;
	val |= channel << PTP_IO_SEL_SHIFT_;
	lan743x_csr_write(adapter, PTP_IO_SEL, val);

	/* PTP Interrupt Enable Register */
	if (flags & PTP_FALLING_EDGE)
		val = PTP_INT_EN_FE_EN_SET_(channel);
	else
		val = PTP_INT_EN_RE_EN_SET_(channel);
	lan743x_csr_write(adapter, PTP_INT_EN_SET, val);

	mutex_unlock(&ptp->command_lock);

	return 0;
}

static int lan743x_ptp_io_extts(struct lan743x_adapter *adapter, int on,
				struct ptp_extts_request *extts_request)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	u32 flags = extts_request->flags;
	u32 index = extts_request->index;
	struct lan743x_extts *extts;
	int extts_pin;
	int ret = 0;

	extts = &ptp->extts[index];

	if (extts_request->flags & ~(PTP_ENABLE_FEATURE |
				     PTP_RISING_EDGE |
				     PTP_FALLING_EDGE |
				     PTP_STRICT_FLAGS))
		return -EOPNOTSUPP;

	if (on) {
		extts_pin = ptp_find_pin(ptp->ptp_clock, PTP_PF_EXTTS, index);
		if (extts_pin < 0)
			return -EBUSY;

		ret = lan743x_ptp_io_event_cap_en(adapter, flags, index);
		if (!ret)
			extts->flags = flags;
	} else {
		lan743x_ptp_io_extts_off(adapter, index);
	}

	return ret;
}

static int lan743x_ptpci_enable(struct ptp_clock_info *ptpci,
				struct ptp_clock_request *request, int on)
{
	struct lan743x_ptp *ptp =
		container_of(ptpci, struct lan743x_ptp, ptp_clock_info);
	struct lan743x_adapter *adapter =
		container_of(ptp, struct lan743x_adapter, ptp);

	if (request) {
		switch (request->type) {
		case PTP_CLK_REQ_EXTTS:
			if (request->extts.index < ptpci->n_ext_ts)
				return lan743x_ptp_io_extts(adapter, on,
							 &request->extts);
			return -EINVAL;
		case PTP_CLK_REQ_PEROUT:
			if (request->perout.index < ptpci->n_per_out) {
				if (adapter->is_pci11x1x)
					return lan743x_ptp_io_perout(adapter, on,
							     &request->perout);
				else
					return lan743x_ptp_perout(adapter, on,
							  &request->perout);
			}
			return -EINVAL;
		case PTP_CLK_REQ_PPS:
			return -EINVAL;
		default:
			netif_err(adapter, drv, adapter->netdev,
				  "request->type == %d, Unknown\n",
				  request->type);
			break;
		}
	} else {
		netif_err(adapter, drv, adapter->netdev, "request == NULL\n");
	}
	return 0;
}

static int lan743x_ptpci_verify_pin_config(struct ptp_clock_info *ptp,
					   unsigned int pin,
					   enum ptp_pin_function func,
					   unsigned int chan)
{
	struct lan743x_ptp *lan_ptp =
		container_of(ptp, struct lan743x_ptp, ptp_clock_info);
	struct lan743x_adapter *adapter =
		container_of(lan_ptp, struct lan743x_adapter, ptp);
	int result = 0;

	/* Confirm the requested function is supported. Parameter
	 * validation is done by the caller.
	 */
	switch (func) {
	case PTP_PF_NONE:
	case PTP_PF_PEROUT:
		break;
	case PTP_PF_EXTTS:
		if (!adapter->is_pci11x1x)
			result = -1;
		break;
	case PTP_PF_PHYSYNC:
	default:
		result = -1;
		break;
	}
	return result;
}

static void lan743x_ptp_io_event_clock_get(struct lan743x_adapter *adapter,
					   bool fe, u8 channel,
					   struct timespec64 *ts)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	struct lan743x_extts *extts;
	u32 sec, nsec;

	mutex_lock(&ptp->command_lock);
	if (fe) {
		sec = lan743x_csr_read(adapter, PTP_IO_FE_LTC_SEC_CAP_X);
		nsec = lan743x_csr_read(adapter, PTP_IO_FE_LTC_NS_CAP_X);
	} else {
		sec = lan743x_csr_read(adapter, PTP_IO_RE_LTC_SEC_CAP_X);
		nsec = lan743x_csr_read(adapter, PTP_IO_RE_LTC_NS_CAP_X);
	}

	mutex_unlock(&ptp->command_lock);

	/* Update Local timestamp */
	extts = &ptp->extts[channel];
	extts->ts.tv_sec = sec;
	extts->ts.tv_nsec = nsec;
	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}

static long lan743x_ptpci_do_aux_work(struct ptp_clock_info *ptpci)
{
	struct lan743x_ptp *ptp =
		container_of(ptpci, struct lan743x_ptp, ptp_clock_info);
	struct lan743x_adapter *adapter =
		container_of(ptp, struct lan743x_adapter, ptp);
	u32 cap_info, cause, header, nsec, seconds;
	bool new_timestamp_available = false;
	struct ptp_clock_event ptp_event;
	struct timespec64 ts;
	int ptp_int_sts;
	int count = 0;
	int channel;
	s64 ns;

	ptp_int_sts = lan743x_csr_read(adapter, PTP_INT_STS);
	while ((count < 100) && ptp_int_sts) {
		count++;

		if (ptp_int_sts & PTP_INT_BIT_TX_TS_) {
			cap_info = lan743x_csr_read(adapter, PTP_CAP_INFO);

			if (PTP_CAP_INFO_TX_TS_CNT_GET_(cap_info) > 0) {
				seconds = lan743x_csr_read(adapter,
							   PTP_TX_EGRESS_SEC);
				nsec = lan743x_csr_read(adapter,
							PTP_TX_EGRESS_NS);
				cause = (nsec &
					 PTP_TX_EGRESS_NS_CAPTURE_CAUSE_MASK_);
				header = lan743x_csr_read(adapter,
							  PTP_TX_MSG_HEADER);

				if (cause ==
				    PTP_TX_EGRESS_NS_CAPTURE_CAUSE_SW_) {
					nsec &= PTP_TX_EGRESS_NS_TS_NS_MASK_;
					lan743x_ptp_tx_ts_enqueue_ts(adapter,
								     seconds,
								     nsec,
								     header);
					new_timestamp_available = true;
				} else if (cause ==
					   PTP_TX_EGRESS_NS_CAPTURE_CAUSE_AUTO_) {
					netif_err(adapter, drv, adapter->netdev,
						  "Auto capture cause not supported\n");
				} else {
					netif_warn(adapter, drv, adapter->netdev,
						   "unknown tx timestamp capture cause\n");
				}
			} else {
				netif_warn(adapter, drv, adapter->netdev,
					   "TX TS INT but no TX TS CNT\n");
			}
			lan743x_csr_write(adapter, PTP_INT_STS,
					  PTP_INT_BIT_TX_TS_);
		}

		if (ptp_int_sts & PTP_INT_IO_FE_MASK_) {
			do {
				channel = lan743x_get_channel((ptp_int_sts &
							PTP_INT_IO_FE_MASK_) >>
							PTP_INT_IO_FE_SHIFT_);
				if (channel >= 0 &&
				    channel < PCI11X1X_PTP_IO_MAX_CHANNELS) {
					lan743x_ptp_io_event_clock_get(adapter,
								       true,
								       channel,
								       &ts);
					/* PTP Falling Event post */
					ns = timespec64_to_ns(&ts);
					ptp_event.timestamp = ns;
					ptp_event.index = channel;
					ptp_event.type = PTP_CLOCK_EXTTS;
					ptp_clock_event(ptp->ptp_clock,
							&ptp_event);
					lan743x_csr_write(adapter, PTP_INT_STS,
							  PTP_INT_IO_FE_SET_
							  (channel));
					ptp_int_sts &= ~(1 <<
							 (PTP_INT_IO_FE_SHIFT_ +
							  channel));
				} else {
					/* Clear falling event interrupts */
					lan743x_csr_write(adapter, PTP_INT_STS,
							  PTP_INT_IO_FE_MASK_);
					ptp_int_sts &= ~PTP_INT_IO_FE_MASK_;
				}
			} while (ptp_int_sts & PTP_INT_IO_FE_MASK_);
		}

		if (ptp_int_sts & PTP_INT_IO_RE_MASK_) {
			do {
				channel = lan743x_get_channel((ptp_int_sts &
						       PTP_INT_IO_RE_MASK_) >>
						       PTP_INT_IO_RE_SHIFT_);
				if (channel >= 0 &&
				    channel < PCI11X1X_PTP_IO_MAX_CHANNELS) {
					lan743x_ptp_io_event_clock_get(adapter,
								       false,
								       channel,
								       &ts);
					/* PTP Rising Event post */
					ns = timespec64_to_ns(&ts);
					ptp_event.timestamp = ns;
					ptp_event.index = channel;
					ptp_event.type = PTP_CLOCK_EXTTS;
					ptp_clock_event(ptp->ptp_clock,
							&ptp_event);
					lan743x_csr_write(adapter, PTP_INT_STS,
							  PTP_INT_IO_RE_SET_
							  (channel));
					ptp_int_sts &= ~(1 <<
							 (PTP_INT_IO_RE_SHIFT_ +
							  channel));
				} else {
					/* Clear Rising event interrupt */
					lan743x_csr_write(adapter, PTP_INT_STS,
							  PTP_INT_IO_RE_MASK_);
					ptp_int_sts &= ~PTP_INT_IO_RE_MASK_;
				}
			} while (ptp_int_sts & PTP_INT_IO_RE_MASK_);
		}

		ptp_int_sts = lan743x_csr_read(adapter, PTP_INT_STS);
	}

	if (new_timestamp_available)
		lan743x_ptp_tx_ts_complete(adapter);

	lan743x_csr_write(adapter, INT_EN_SET, INT_BIT_1588_);

	return -1;
}

static void lan743x_ptp_clock_get(struct lan743x_adapter *adapter,
				  u32 *seconds, u32 *nano_seconds,
				  u32 *sub_nano_seconds)
{
	struct lan743x_ptp *ptp = &adapter->ptp;

	mutex_lock(&ptp->command_lock);

	lan743x_csr_write(adapter, PTP_CMD_CTL, PTP_CMD_CTL_PTP_CLOCK_READ_);
	lan743x_ptp_wait_till_cmd_done(adapter, PTP_CMD_CTL_PTP_CLOCK_READ_);

	if (seconds)
		(*seconds) = lan743x_csr_read(adapter, PTP_CLOCK_SEC);

	if (nano_seconds)
		(*nano_seconds) = lan743x_csr_read(adapter, PTP_CLOCK_NS);

	if (sub_nano_seconds)
		(*sub_nano_seconds) =
		lan743x_csr_read(adapter, PTP_CLOCK_SUBNS);

	mutex_unlock(&ptp->command_lock);
}

static void lan743x_ptp_io_clock_get(struct lan743x_adapter *adapter,
				     u32 *sec, u32 *nsec, u32 *sub_nsec)
{
	struct lan743x_ptp *ptp = &adapter->ptp;

	mutex_lock(&ptp->command_lock);
	lan743x_csr_write(adapter, PTP_CMD_CTL, PTP_CMD_CTL_PTP_CLOCK_READ_);
	lan743x_ptp_wait_till_cmd_done(adapter, PTP_CMD_CTL_PTP_CLOCK_READ_);

	if (sec)
		(*sec) = lan743x_csr_read(adapter, PTP_LTC_RD_SEC_LO);

	if (nsec)
		(*nsec) = lan743x_csr_read(adapter, PTP_LTC_RD_NS);

	if (sub_nsec)
		(*sub_nsec) =
		lan743x_csr_read(adapter, PTP_LTC_RD_SUBNS);

	mutex_unlock(&ptp->command_lock);
}

static void lan743x_ptp_clock_step(struct lan743x_adapter *adapter,
				   s64 time_step_ns)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	u32 nano_seconds_step = 0;
	u64 abs_time_step_ns = 0;
	u32 unsigned_seconds = 0;
	u32 nano_seconds = 0;
	u32 remainder = 0;
	s32 seconds = 0;

	if (time_step_ns >  15000000000LL) {
		/* convert to clock set */
		if (adapter->is_pci11x1x)
			lan743x_ptp_io_clock_get(adapter, &unsigned_seconds,
						 &nano_seconds, NULL);
		else
			lan743x_ptp_clock_get(adapter, &unsigned_seconds,
					      &nano_seconds, NULL);
		unsigned_seconds += div_u64_rem(time_step_ns, 1000000000LL,
						&remainder);
		nano_seconds += remainder;
		if (nano_seconds >= 1000000000) {
			unsigned_seconds++;
			nano_seconds -= 1000000000;
		}
		lan743x_ptp_clock_set(adapter, unsigned_seconds,
				      nano_seconds, 0);
		return;
	} else if (time_step_ns < -15000000000LL) {
		/* convert to clock set */
		time_step_ns = -time_step_ns;

		if (adapter->is_pci11x1x) {
			lan743x_ptp_io_clock_get(adapter, &unsigned_seconds,
						 &nano_seconds, NULL);
		} else {
			lan743x_ptp_clock_get(adapter, &unsigned_seconds,
					      &nano_seconds, NULL);
		}
		unsigned_seconds -= div_u64_rem(time_step_ns, 1000000000LL,
						&remainder);
		nano_seconds_step = remainder;
		if (nano_seconds < nano_seconds_step) {
			unsigned_seconds--;
			nano_seconds += 1000000000;
		}
		nano_seconds -= nano_seconds_step;
		lan743x_ptp_clock_set(adapter, unsigned_seconds,
				      nano_seconds, 0);
		return;
	}

	/* do clock step */
	if (time_step_ns >= 0) {
		abs_time_step_ns = (u64)(time_step_ns);
		seconds = (s32)div_u64_rem(abs_time_step_ns, 1000000000,
					   &remainder);
		nano_seconds = (u32)remainder;
	} else {
		abs_time_step_ns = (u64)(-time_step_ns);
		seconds = -((s32)div_u64_rem(abs_time_step_ns, 1000000000,
					     &remainder));
		nano_seconds = (u32)remainder;
		if (nano_seconds > 0) {
			/* subtracting nano seconds is not allowed
			 * convert to subtracting from seconds,
			 * and adding to nanoseconds
			 */
			seconds--;
			nano_seconds = (1000000000 - nano_seconds);
		}
	}

	if (nano_seconds > 0) {
		/* add 8 ns to cover the likely normal increment */
		nano_seconds += 8;
	}

	if (nano_seconds >= 1000000000) {
		/* carry into seconds */
		seconds++;
		nano_seconds -= 1000000000;
	}

	while (seconds) {
		mutex_lock(&ptp->command_lock);
		if (seconds > 0) {
			u32 adjustment_value = (u32)seconds;

			if (adjustment_value > 0xF)
				adjustment_value = 0xF;
			lan743x_csr_write(adapter, PTP_CLOCK_STEP_ADJ,
					  PTP_CLOCK_STEP_ADJ_DIR_ |
					  adjustment_value);
			seconds -= ((s32)adjustment_value);
		} else {
			u32 adjustment_value = (u32)(-seconds);

			if (adjustment_value > 0xF)
				adjustment_value = 0xF;
			lan743x_csr_write(adapter, PTP_CLOCK_STEP_ADJ,
					  adjustment_value);
			seconds += ((s32)adjustment_value);
		}
		lan743x_csr_write(adapter, PTP_CMD_CTL,
				  PTP_CMD_CTL_PTP_CLOCK_STEP_SEC_);
		lan743x_ptp_wait_till_cmd_done(adapter,
					       PTP_CMD_CTL_PTP_CLOCK_STEP_SEC_);
		mutex_unlock(&ptp->command_lock);
	}
	if (nano_seconds) {
		mutex_lock(&ptp->command_lock);
		lan743x_csr_write(adapter, PTP_CLOCK_STEP_ADJ,
				  PTP_CLOCK_STEP_ADJ_DIR_ |
				  (nano_seconds &
				  PTP_CLOCK_STEP_ADJ_VALUE_MASK_));
		lan743x_csr_write(adapter, PTP_CMD_CTL,
				  PTP_CMD_CTL_PTP_CLK_STP_NSEC_);
		lan743x_ptp_wait_till_cmd_done(adapter,
					       PTP_CMD_CTL_PTP_CLK_STP_NSEC_);
		mutex_unlock(&ptp->command_lock);
	}
}

void lan743x_ptp_isr(void *context)
{
	struct lan743x_adapter *adapter = (struct lan743x_adapter *)context;
	struct lan743x_ptp *ptp = NULL;
	int enable_flag = 1;
	u32 ptp_int_sts = 0;

	ptp = &adapter->ptp;

	lan743x_csr_write(adapter, INT_EN_CLR, INT_BIT_1588_);

	ptp_int_sts = lan743x_csr_read(adapter, PTP_INT_STS);
	ptp_int_sts &= lan743x_csr_read(adapter, PTP_INT_EN_SET);

	if (ptp_int_sts & PTP_INT_BIT_TX_TS_) {
		ptp_schedule_worker(ptp->ptp_clock, 0);
		enable_flag = 0;/* tasklet will re-enable later */
	}
	if (ptp_int_sts & PTP_INT_BIT_TX_SWTS_ERR_) {
		netif_err(adapter, drv, adapter->netdev,
			  "PTP TX Software Timestamp Error\n");
		/* clear int status bit */
		lan743x_csr_write(adapter, PTP_INT_STS,
				  PTP_INT_BIT_TX_SWTS_ERR_);
	}
	if (ptp_int_sts & PTP_INT_BIT_TIMER_B_) {
		/* clear int status bit */
		lan743x_csr_write(adapter, PTP_INT_STS,
				  PTP_INT_BIT_TIMER_B_);
	}
	if (ptp_int_sts & PTP_INT_BIT_TIMER_A_) {
		/* clear int status bit */
		lan743x_csr_write(adapter, PTP_INT_STS,
				  PTP_INT_BIT_TIMER_A_);
	}

	if (enable_flag) {
		/* re-enable isr */
		lan743x_csr_write(adapter, INT_EN_SET, INT_BIT_1588_);
	}
}

static void lan743x_ptp_tx_ts_enqueue_skb(struct lan743x_adapter *adapter,
					  struct sk_buff *skb, bool ignore_sync)
{
	struct lan743x_ptp *ptp = &adapter->ptp;

	spin_lock_bh(&ptp->tx_ts_lock);
	if (ptp->tx_ts_skb_queue_size < LAN743X_PTP_NUMBER_OF_TX_TIMESTAMPS) {
		ptp->tx_ts_skb_queue[ptp->tx_ts_skb_queue_size] = skb;
		if (ignore_sync)
			ptp->tx_ts_ignore_sync_queue |=
				BIT(ptp->tx_ts_skb_queue_size);
		ptp->tx_ts_skb_queue_size++;
	} else {
		/* this should never happen, so long as the tx channel
		 * calls and honors the result from
		 * lan743x_ptp_request_tx_timestamp
		 */
		netif_err(adapter, drv, adapter->netdev,
			  "tx ts skb queue overflow\n");
		dev_kfree_skb(skb);
	}
	spin_unlock_bh(&ptp->tx_ts_lock);
}

static void lan743x_ptp_sync_to_system_clock(struct lan743x_adapter *adapter)
{
	struct timespec64 ts;

	ktime_get_clocktai_ts64(&ts);

	lan743x_ptp_clock_set(adapter, ts.tv_sec, ts.tv_nsec, 0);
}

void lan743x_ptp_update_latency(struct lan743x_adapter *adapter,
				u32 link_speed)
{
	switch (link_speed) {
	case 10:
		lan743x_csr_write(adapter, PTP_LATENCY,
				  PTP_LATENCY_TX_SET_(0) |
				  PTP_LATENCY_RX_SET_(0));
		break;
	case 100:
		lan743x_csr_write(adapter, PTP_LATENCY,
				  PTP_LATENCY_TX_SET_(181) |
				  PTP_LATENCY_RX_SET_(594));
		break;
	case 1000:
		lan743x_csr_write(adapter, PTP_LATENCY,
				  PTP_LATENCY_TX_SET_(30) |
				  PTP_LATENCY_RX_SET_(525));
		break;
	}
}

int lan743x_ptp_init(struct lan743x_adapter *adapter)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	int i;

	mutex_init(&ptp->command_lock);
	spin_lock_init(&ptp->tx_ts_lock);
	ptp->used_event_ch = 0;

	for (i = 0; i < LAN743X_PTP_N_EVENT_CHAN; i++) {
		ptp->perout[i].event_ch = -1;
		ptp->perout[i].gpio_pin = -1;
	}

	lan743x_led_mux_save(adapter);

	return 0;
}

int lan743x_ptp_open(struct lan743x_adapter *adapter)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	int ret = -ENODEV;
	u32 temp;
	int i;
	int n_pins;

	lan743x_ptp_reset(adapter);
	lan743x_ptp_sync_to_system_clock(adapter);
	temp = lan743x_csr_read(adapter, PTP_TX_MOD2);
	temp |= PTP_TX_MOD2_TX_PTP_CLR_UDPV4_CHKSUM_;
	lan743x_csr_write(adapter, PTP_TX_MOD2, temp);

	/* Default Timestamping */
	lan743x_rx_set_tstamp_mode(adapter, HWTSTAMP_FILTER_NONE);

	lan743x_ptp_enable(adapter);
	lan743x_csr_write(adapter, INT_EN_SET, INT_BIT_1588_);
	lan743x_csr_write(adapter, PTP_INT_EN_SET,
			  PTP_INT_BIT_TX_SWTS_ERR_ | PTP_INT_BIT_TX_TS_);
	ptp->flags |= PTP_FLAG_ISR_ENABLED;

	if (!IS_ENABLED(CONFIG_PTP_1588_CLOCK))
		return 0;

	switch (adapter->csr.id_rev & ID_REV_ID_MASK_) {
	case ID_REV_ID_LAN7430_:
		n_pins = LAN7430_N_GPIO;
		break;
	case ID_REV_ID_LAN7431_:
	case ID_REV_ID_A011_:
	case ID_REV_ID_A041_:
		n_pins = LAN7431_N_GPIO;
		break;
	default:
		netif_warn(adapter, drv, adapter->netdev,
			   "Unknown LAN743x (%08x). Assuming no GPIO\n",
			   adapter->csr.id_rev);
		n_pins = 0;
		break;
	}

	if (n_pins > LAN743X_PTP_N_GPIO)
		n_pins = LAN743X_PTP_N_GPIO;

	for (i = 0; i < n_pins; i++) {
		struct ptp_pin_desc *ptp_pin = &ptp->pin_config[i];

		snprintf(ptp_pin->name,
			 sizeof(ptp_pin->name), "lan743x_ptp_pin_%02d", i);
		ptp_pin->index = i;
		ptp_pin->func = PTP_PF_NONE;
	}

	ptp->ptp_clock_info.owner = THIS_MODULE;
	snprintf(ptp->ptp_clock_info.name, 16, "%pm",
		 adapter->netdev->dev_addr);
	ptp->ptp_clock_info.max_adj = LAN743X_PTP_MAX_FREQ_ADJ_IN_PPB;
	ptp->ptp_clock_info.n_alarm = 0;
	ptp->ptp_clock_info.n_ext_ts = LAN743X_PTP_N_EXTTS;
	ptp->ptp_clock_info.n_per_out = LAN743X_PTP_N_EVENT_CHAN;
	ptp->ptp_clock_info.n_pins = n_pins;
	ptp->ptp_clock_info.pps = LAN743X_PTP_N_PPS;
	ptp->ptp_clock_info.pin_config = ptp->pin_config;
	ptp->ptp_clock_info.adjfine = lan743x_ptpci_adjfine;
	ptp->ptp_clock_info.adjtime = lan743x_ptpci_adjtime;
	ptp->ptp_clock_info.gettime64 = lan743x_ptpci_gettime64;
	ptp->ptp_clock_info.getcrosststamp = NULL;
	ptp->ptp_clock_info.settime64 = lan743x_ptpci_settime64;
	ptp->ptp_clock_info.enable = lan743x_ptpci_enable;
	ptp->ptp_clock_info.do_aux_work = lan743x_ptpci_do_aux_work;
	ptp->ptp_clock_info.verify = lan743x_ptpci_verify_pin_config;

	ptp->ptp_clock = ptp_clock_register(&ptp->ptp_clock_info,
					    &adapter->pdev->dev);

	if (IS_ERR(ptp->ptp_clock)) {
		netif_err(adapter, ifup, adapter->netdev,
			  "ptp_clock_register failed\n");
		goto done;
	}
	ptp->flags |= PTP_FLAG_PTP_CLOCK_REGISTERED;
	netif_info(adapter, ifup, adapter->netdev,
		   "successfully registered ptp clock\n");

	return 0;
done:
	lan743x_ptp_close(adapter);
	return ret;
}

void lan743x_ptp_close(struct lan743x_adapter *adapter)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	int index;

	if (IS_ENABLED(CONFIG_PTP_1588_CLOCK) &&
	    (ptp->flags & PTP_FLAG_PTP_CLOCK_REGISTERED)) {
		ptp_clock_unregister(ptp->ptp_clock);
		ptp->ptp_clock = NULL;
		ptp->flags &= ~PTP_FLAG_PTP_CLOCK_REGISTERED;
		netif_info(adapter, drv, adapter->netdev,
			   "ptp clock unregister\n");
	}

	if (ptp->flags & PTP_FLAG_ISR_ENABLED) {
		lan743x_csr_write(adapter, PTP_INT_EN_CLR,
				  PTP_INT_BIT_TX_SWTS_ERR_ |
				  PTP_INT_BIT_TX_TS_);
		lan743x_csr_write(adapter, INT_EN_CLR, INT_BIT_1588_);
		ptp->flags &= ~PTP_FLAG_ISR_ENABLED;
	}

	/* clean up pending timestamp requests */
	lan743x_ptp_tx_ts_complete(adapter);
	spin_lock_bh(&ptp->tx_ts_lock);
	for (index = 0;
		index < LAN743X_PTP_NUMBER_OF_TX_TIMESTAMPS;
		index++) {
		struct sk_buff *skb = ptp->tx_ts_skb_queue[index];

		dev_kfree_skb(skb);
		ptp->tx_ts_skb_queue[index] = NULL;
		ptp->tx_ts_seconds_queue[index] = 0;
		ptp->tx_ts_nseconds_queue[index] = 0;
	}
	ptp->tx_ts_skb_queue_size = 0;
	ptp->tx_ts_queue_size = 0;
	ptp->pending_tx_timestamps = 0;
	spin_unlock_bh(&ptp->tx_ts_lock);

	lan743x_led_mux_restore(adapter);

	lan743x_ptp_disable(adapter);
}

static void lan743x_ptp_set_sync_ts_insert(struct lan743x_adapter *adapter,
					   bool ts_insert_enable)
{
	u32 ptp_tx_mod = lan743x_csr_read(adapter, PTP_TX_MOD);

	if (ts_insert_enable)
		ptp_tx_mod |= PTP_TX_MOD_TX_PTP_SYNC_TS_INSERT_;
	else
		ptp_tx_mod &= ~PTP_TX_MOD_TX_PTP_SYNC_TS_INSERT_;

	lan743x_csr_write(adapter, PTP_TX_MOD, ptp_tx_mod);
}

static bool lan743x_ptp_is_enabled(struct lan743x_adapter *adapter)
{
	if (lan743x_csr_read(adapter, PTP_CMD_CTL) & PTP_CMD_CTL_PTP_ENABLE_)
		return true;
	return false;
}

static void lan743x_ptp_enable(struct lan743x_adapter *adapter)
{
	struct lan743x_ptp *ptp = &adapter->ptp;

	mutex_lock(&ptp->command_lock);

	if (lan743x_ptp_is_enabled(adapter)) {
		netif_warn(adapter, drv, adapter->netdev,
			   "PTP already enabled\n");
		goto done;
	}
	lan743x_csr_write(adapter, PTP_CMD_CTL, PTP_CMD_CTL_PTP_ENABLE_);
done:
	mutex_unlock(&ptp->command_lock);
}

static void lan743x_ptp_disable(struct lan743x_adapter *adapter)
{
	struct lan743x_ptp *ptp = &adapter->ptp;

	/* Disable Timestamping */
	lan743x_rx_set_tstamp_mode(adapter, HWTSTAMP_FILTER_NONE);

	mutex_lock(&ptp->command_lock);
	if (!lan743x_ptp_is_enabled(adapter)) {
		netif_warn(adapter, drv, adapter->netdev,
			   "PTP already disabled\n");
		goto done;
	}
	lan743x_csr_write(adapter, PTP_CMD_CTL, PTP_CMD_CTL_PTP_DISABLE_);
	lan743x_ptp_wait_till_cmd_done(adapter, PTP_CMD_CTL_PTP_ENABLE_);
done:
	mutex_unlock(&ptp->command_lock);
}

static void lan743x_ptp_reset(struct lan743x_adapter *adapter)
{
	struct lan743x_ptp *ptp = &adapter->ptp;

	mutex_lock(&ptp->command_lock);

	if (lan743x_ptp_is_enabled(adapter)) {
		netif_err(adapter, drv, adapter->netdev,
			  "Attempting reset while enabled\n");
		goto done;
	}

	lan743x_csr_write(adapter, PTP_CMD_CTL, PTP_CMD_CTL_PTP_RESET_);
	lan743x_ptp_wait_till_cmd_done(adapter, PTP_CMD_CTL_PTP_RESET_);
done:
	mutex_unlock(&ptp->command_lock);
}

static void lan743x_ptp_clock_set(struct lan743x_adapter *adapter,
				  u32 seconds, u32 nano_seconds,
				  u32 sub_nano_seconds)
{
	struct lan743x_ptp *ptp = &adapter->ptp;

	mutex_lock(&ptp->command_lock);

	lan743x_csr_write(adapter, PTP_CLOCK_SEC, seconds);
	lan743x_csr_write(adapter, PTP_CLOCK_NS, nano_seconds);
	lan743x_csr_write(adapter, PTP_CLOCK_SUBNS, sub_nano_seconds);

	lan743x_csr_write(adapter, PTP_CMD_CTL, PTP_CMD_CTL_PTP_CLOCK_LOAD_);
	lan743x_ptp_wait_till_cmd_done(adapter, PTP_CMD_CTL_PTP_CLOCK_LOAD_);
	mutex_unlock(&ptp->command_lock);
}

bool lan743x_ptp_request_tx_timestamp(struct lan743x_adapter *adapter)
{
	struct lan743x_ptp *ptp = &adapter->ptp;
	bool result = false;

	spin_lock(&ptp->tx_ts_lock);
	if (ptp->pending_tx_timestamps < LAN743X_PTP_NUMBER_OF_TX_TIMESTAMPS) {
		/* request granted */
		ptp->pending_tx_timestamps++;
		result = true;
	}
	spin_unlock(&ptp->tx_ts_lock);
	return result;
}

void lan743x_ptp_unrequest_tx_timestamp(struct lan743x_adapter *adapter)
{
	struct lan743x_ptp *ptp = &adapter->ptp;

	spin_lock_bh(&ptp->tx_ts_lock);
	if (ptp->pending_tx_timestamps > 0)
		ptp->pending_tx_timestamps--;
	else
		netif_err(adapter, drv, adapter->netdev,
			  "unrequest failed, pending_tx_timestamps==0\n");
	spin_unlock_bh(&ptp->tx_ts_lock);
}

void lan743x_ptp_tx_timestamp_skb(struct lan743x_adapter *adapter,
				  struct sk_buff *skb, bool ignore_sync)
{
	lan743x_ptp_tx_ts_enqueue_skb(adapter, skb, ignore_sync);

	lan743x_ptp_tx_ts_complete(adapter);
}

int lan743x_ptp_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	struct hwtstamp_config config;
	int ret = 0;
	int index;

	if (!ifr) {
		netif_err(adapter, drv, adapter->netdev,
			  "SIOCSHWTSTAMP, ifr == NULL\n");
		return -EINVAL;
	}

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		for (index = 0; index < adapter->used_tx_channels;
		     index++)
			lan743x_tx_set_timestamping_mode(&adapter->tx[index],
							 false, false);
		lan743x_ptp_set_sync_ts_insert(adapter, false);
		break;
	case HWTSTAMP_TX_ON:
		for (index = 0; index < adapter->used_tx_channels;
			index++)
			lan743x_tx_set_timestamping_mode(&adapter->tx[index],
							 true, false);
		lan743x_ptp_set_sync_ts_insert(adapter, false);
		break;
	case HWTSTAMP_TX_ONESTEP_SYNC:
		for (index = 0; index < adapter->used_tx_channels;
			index++)
			lan743x_tx_set_timestamping_mode(&adapter->tx[index],
							 true, true);

		lan743x_ptp_set_sync_ts_insert(adapter, true);
		break;
	case HWTSTAMP_TX_ONESTEP_P2P:
		ret = -ERANGE;
		break;
	default:
		netif_warn(adapter, drv, adapter->netdev,
			   "  tx_type = %d, UNKNOWN\n", config.tx_type);
		ret = -EINVAL;
		break;
	}

	ret = lan743x_rx_set_tstamp_mode(adapter, config.rx_filter);

	if (!ret)
		return copy_to_user(ifr->ifr_data, &config,
			sizeof(config)) ? -EFAULT : 0;
	return ret;
}
