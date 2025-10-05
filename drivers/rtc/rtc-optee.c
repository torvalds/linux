// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Microchip.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/tee_drv.h>

#define RTC_INFO_VERSION			0x1

#define TA_RTC_FEATURE_CORRECTION		BIT(0)
#define TA_RTC_FEATURE_ALARM			BIT(1)
#define TA_RTC_FEATURE_WAKEUP_ALARM		BIT(2)

enum rtc_optee_pta_cmd {
	/*  PTA_CMD_RTC_GET_INFO - Get RTC information
	 *
	 * [out]        memref[0]  RTC buffer memory reference containing a struct pta_rtc_info
	 */
	PTA_CMD_RTC_GET_INF = 0x0,

	/*
	 * PTA_CMD_RTC_GET_TIME - Get time from RTC
	 *
	 * [out]    memref[0]  RTC buffer memory reference containing a struct pta_rtc_time
	 */
	PTA_CMD_RTC_GET_TIME = 0x1,

	/*
	 * PTA_CMD_RTC_SET_TIME - Set time from RTC
	 *
	 * [in]     memref[0]  RTC buffer memory reference containing a struct pta_rtc_time to be
	 *                     used as RTC time
	 */
	PTA_CMD_RTC_SET_TIME = 0x2,

	/*
	 * PTA_CMD_RTC_GET_OFFSET - Get RTC offset
	 *
	 * [out]    value[0].a  RTC offset (signed 32bit value)
	 */
	PTA_CMD_RTC_GET_OFFSET = 0x3,

	/*
	 * PTA_CMD_RTC_SET_OFFSET - Set RTC offset
	 *
	 * [in]     value[0].a  RTC offset to be set (signed 32bit value)
	 */
	PTA_CMD_RTC_SET_OFFSET = 0x4,

	/*
	 * PTA_CMD_RTC_READ_ALARM - Read RTC alarm
	 *
	 * [out]     memref[0]  RTC buffer memory reference containing a struct pta_rtc_alarm
	 */
	PTA_CMD_RTC_READ_ALARM = 0x5,

	/*
	 * PTA_CMD_RTC_SET_ALARM - Set RTC alarm
	 *
	 * [in]     memref[0]  RTC buffer memory reference containing a struct pta_rtc_alarm to be
	 *                     used as RTC alarm
	 */
	PTA_CMD_RTC_SET_ALARM = 0x6,

	/*
	 * PTA_CMD_RTC_ENABLE_ALARM - Enable Alarm
	 *
	 * [in]     value[0].a  RTC IRQ flag (uint32_t), 0 to disable the alarm, 1 to enable
	 */
	PTA_CMD_RTC_ENABLE_ALARM = 0x7,

	/*
	 * PTA_CMD_RTC_WAIT_ALARM - Get alarm event
	 *
	 * [out]     value[0].a  RTC wait alarm return status (uint32_t):
	 *                       - 0: No alarm event
	 *                       - 1: Alarm event occurred
	 *                       - 2: Alarm event canceled
	 */
	PTA_CMD_RTC_WAIT_ALARM = 0x8,

	/*
	 * PTA_CMD_RTC_CANCEL_WAIT - Cancel wait for alarm event
	 */
	PTA_CMD_RTC_CANCEL_WAIT = 0x9,

	/*
	 * PTA_CMD_RTC_SET_WAKE_ALARM_STATUS - Set RTC wake alarm status flag
	 *
	 * [in]     value[0].a RTC IRQ wake alarm flag (uint32_t), 0 to disable the wake up
	 *                     capability, 1 to enable.
	 */
	PTA_CMD_RTC_SET_WAKE_ALARM_STATUS = 0xA,
};

enum rtc_wait_alarm_status {
	WAIT_ALARM_RESET = 0x0,
	WAIT_ALARM_ALARM_OCCURRED = 0x1,
	WAIT_ALARM_CANCELED = 0x2,
};

struct optee_rtc_time {
	u32 tm_sec;
	u32 tm_min;
	u32 tm_hour;
	u32 tm_mday;
	u32 tm_mon;
	u32 tm_year;
	u32 tm_wday;
};

struct optee_rtc_alarm {
	u8 enabled;
	u8 pending;
	struct optee_rtc_time time;
};

struct optee_rtc_info {
	u64 version;
	u64 features;
	struct optee_rtc_time range_min;
	struct optee_rtc_time range_max;
};

/**
 * struct optee_rtc - OP-TEE RTC private data
 * @dev:		OP-TEE based RTC device.
 * @ctx:		OP-TEE context handler.
 * @session_id:		RTC TA session identifier.
 * @session2_id:	RTC wait alarm session identifier.
 * @shm:		Memory pool shared with RTC device.
 * @features:		Bitfield of RTC features
 * @alarm_task:		RTC wait alamr task.
 * @rtc:		RTC device.
 */
struct optee_rtc {
	struct device *dev;
	struct tee_context *ctx;
	u32 session_id;
	u32 session2_id;
	struct tee_shm *shm;
	u64 features;
	struct task_struct *alarm_task;
	struct rtc_device *rtc;
};

static int optee_rtc_readtime(struct device *dev, struct rtc_time *tm)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct optee_rtc_time *optee_tm;
	struct tee_param param[4] = {0};
	int ret;

	inv_arg.func = PTA_CMD_RTC_GET_TIME;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	param[0].u.memref.shm = priv->shm;
	param[0].u.memref.size = sizeof(struct optee_rtc_time);

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0)
		return ret ? ret : -EPROTO;

	optee_tm = tee_shm_get_va(priv->shm, 0);
	if (IS_ERR(optee_tm))
		return PTR_ERR(optee_tm);

	if (param[0].u.memref.size != sizeof(*optee_tm))
		return -EPROTO;

	tm->tm_sec = optee_tm->tm_sec;
	tm->tm_min = optee_tm->tm_min;
	tm->tm_hour = optee_tm->tm_hour;
	tm->tm_mday = optee_tm->tm_mday;
	tm->tm_mon = optee_tm->tm_mon;
	tm->tm_year = optee_tm->tm_year - 1900;
	tm->tm_wday = optee_tm->tm_wday;
	tm->tm_yday = rtc_year_days(tm->tm_mday, tm->tm_mon, tm->tm_year);

	return 0;
}

static int optee_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct tee_param param[4] = {0};
	struct optee_rtc_time *optee_tm;
	int ret;

	inv_arg.func = PTA_CMD_RTC_SET_TIME;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[0].u.memref.shm = priv->shm;
	param[0].u.memref.size = sizeof(struct optee_rtc_time);

	optee_tm = tee_shm_get_va(priv->shm, 0);
	if (IS_ERR(optee_tm))
		return PTR_ERR(optee_tm);

	optee_tm->tm_min = tm->tm_min;
	optee_tm->tm_sec = tm->tm_sec;
	optee_tm->tm_hour = tm->tm_hour;
	optee_tm->tm_mday = tm->tm_mday;
	optee_tm->tm_mon = tm->tm_mon;
	optee_tm->tm_year = tm->tm_year + 1900;
	optee_tm->tm_wday = tm->tm_wday;

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0)
		return ret ? ret : -EPROTO;

	return 0;
}

static int optee_rtc_readoffset(struct device *dev, long *offset)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct tee_param param[4] = {0};
	int ret;

	if (!(priv->features & TA_RTC_FEATURE_CORRECTION))
		return -EOPNOTSUPP;

	inv_arg.func = PTA_CMD_RTC_GET_OFFSET;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT;

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0)
		return ret ? ret : -EPROTO;

	*offset = param[0].u.value.a;

	return 0;
}

static int optee_rtc_setoffset(struct device *dev, long offset)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct tee_param param[4] = {0};
	int ret;

	if (!(priv->features & TA_RTC_FEATURE_CORRECTION))
		return -EOPNOTSUPP;

	inv_arg.func = PTA_CMD_RTC_SET_OFFSET;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = offset;

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0)
		return ret ? ret : -EPROTO;

	return 0;
}

static int optee_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct optee_rtc_alarm *optee_alarm;
	struct tee_param param[1] = {0};
	int ret;

	if (!(priv->features & TA_RTC_FEATURE_ALARM))
		return -EOPNOTSUPP;

	inv_arg.func = PTA_CMD_RTC_READ_ALARM;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 1;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	param[0].u.memref.shm = priv->shm;
	param[0].u.memref.size = sizeof(struct optee_rtc_alarm);

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0)
		return ret ? ret : -EPROTO;

	optee_alarm = tee_shm_get_va(priv->shm, 0);
	if (IS_ERR(optee_alarm))
		return PTR_ERR(optee_alarm);

	if (param[0].u.memref.size != sizeof(*optee_alarm))
		return -EPROTO;

	alarm->enabled = optee_alarm->enabled;
	alarm->pending = optee_alarm->pending;
	alarm->time.tm_sec = optee_alarm->time.tm_sec;
	alarm->time.tm_min = optee_alarm->time.tm_min;
	alarm->time.tm_hour = optee_alarm->time.tm_hour;
	alarm->time.tm_mday = optee_alarm->time.tm_mday;
	alarm->time.tm_mon = optee_alarm->time.tm_mon;
	alarm->time.tm_year = optee_alarm->time.tm_year - 1900;
	alarm->time.tm_wday = optee_alarm->time.tm_wday;
	alarm->time.tm_yday = rtc_year_days(alarm->time.tm_mday,
					    alarm->time.tm_mon,
					    alarm->time.tm_year);

	return 0;
}

static int optee_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct optee_rtc_alarm *optee_alarm;
	struct tee_param param[1] = {0};
	int ret;

	if (!(priv->features & TA_RTC_FEATURE_ALARM))
		return -EOPNOTSUPP;

	inv_arg.func = PTA_CMD_RTC_SET_ALARM;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 1;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[0].u.memref.shm = priv->shm;
	param[0].u.memref.size = sizeof(struct optee_rtc_alarm);

	optee_alarm = tee_shm_get_va(priv->shm, 0);
	if (IS_ERR(optee_alarm))
		return PTR_ERR(optee_alarm);

	optee_alarm->enabled = alarm->enabled;
	optee_alarm->pending = alarm->pending;
	optee_alarm->time.tm_sec = alarm->time.tm_sec;
	optee_alarm->time.tm_min = alarm->time.tm_min;
	optee_alarm->time.tm_hour = alarm->time.tm_hour;
	optee_alarm->time.tm_mday = alarm->time.tm_mday;
	optee_alarm->time.tm_mon = alarm->time.tm_mon;
	optee_alarm->time.tm_year = alarm->time.tm_year + 1900;
	optee_alarm->time.tm_wday = alarm->time.tm_wday;

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0)
		return ret ? ret : -EPROTO;

	return 0;
}

static int optee_rtc_enable_alarm(struct device *dev, unsigned int enabled)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct tee_param param[1] = {0};
	int ret;

	if (!(priv->features & TA_RTC_FEATURE_ALARM))
		return -EOPNOTSUPP;

	inv_arg.func = PTA_CMD_RTC_ENABLE_ALARM;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 1;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = (bool)enabled;

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0)
		return ret ? ret : -EPROTO;

	return 0;
}

static const struct rtc_class_ops optee_rtc_ops = {
	.read_time		= optee_rtc_readtime,
	.set_time		= optee_rtc_settime,
	.set_offset		= optee_rtc_setoffset,
	.read_offset		= optee_rtc_readoffset,
	.read_alarm		= optee_rtc_read_alarm,
	.set_alarm		= optee_rtc_set_alarm,
	.alarm_irq_enable	= optee_rtc_enable_alarm,
};

static int optee_rtc_wait_alarm(struct device *dev, int *return_status)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct tee_param param[1] = {0};
	int ret;

	if (!(priv->features & TA_RTC_FEATURE_ALARM))
		return -EOPNOTSUPP;

	inv_arg.func = PTA_CMD_RTC_WAIT_ALARM;
	inv_arg.session = priv->session2_id;
	inv_arg.num_params = 1;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT;

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0)
		return ret ? ret : -EPROTO;

	*return_status = param[0].u.value.a;

	return 0;
}

static int optee_rtc_cancel_wait_alarm(struct device *dev)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct tee_param param[1] = {0};
	int ret;

	if (!(priv->features & TA_RTC_FEATURE_ALARM))
		return -EOPNOTSUPP;

	inv_arg.func = PTA_CMD_RTC_CANCEL_WAIT;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 0;

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0)
		return ret ? ret : -EPROTO;

	return 0;
}

static int optee_rtc_set_alarm_wake_status(struct device *dev, bool status)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct tee_param param[1] = {0};
	int ret;

	if (!(priv->features & TA_RTC_FEATURE_ALARM))
		return -EOPNOTSUPP;

	inv_arg.func = PTA_CMD_RTC_SET_WAKE_ALARM_STATUS;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 1;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = status;

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);

	if (ret < 0 || inv_arg.ret != 0)
		return ret ? ret : -EPROTO;

	return 0;
}

static int optee_rtc_handle_alarm_event(void *data)
{
	struct optee_rtc *priv = (struct optee_rtc *)data;
	int wait_alarm_return_status = 0;
	int ret;

	while (!kthread_should_stop()) {
		ret = optee_rtc_wait_alarm(priv->dev, &wait_alarm_return_status);
		if (ret) {
			dev_err(priv->dev, "Failed to wait for alarm: %d\n", ret);
			return ret;
		}
		switch (wait_alarm_return_status) {
		case WAIT_ALARM_ALARM_OCCURRED:
			dev_dbg(priv->dev, "Alarm occurred\n");
			rtc_update_irq(priv->rtc, 1, RTC_IRQF | RTC_AF);
			break;
		case WAIT_ALARM_CANCELED:
			dev_dbg(priv->dev, "Alarm canceled\n");
			break;
		default:
			dev_warn(priv->dev, "Unknown return status: %d\n",
				 wait_alarm_return_status);
			break;
		}
	}

	return 0;
}

static int optee_rtc_read_info(struct device *dev, struct rtc_device *rtc,
			       u64 *features)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct tee_param param[4] = {0};
	struct optee_rtc_info *info;
	struct optee_rtc_time *tm;
	int ret;

	inv_arg.func = PTA_CMD_RTC_GET_INF;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	param[0].u.memref.shm = priv->shm;
	param[0].u.memref.size = sizeof(*info);

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if (ret < 0 || inv_arg.ret != 0)
		return ret ? ret : -EPROTO;

	info = tee_shm_get_va(priv->shm, 0);
	if (IS_ERR(info))
		return PTR_ERR(info);

	if (param[0].u.memref.size != sizeof(*info))
		return -EPROTO;

	if (info->version != RTC_INFO_VERSION)
		return -EPROTO;

	*features = info->features;

	tm = &info->range_min;
	rtc->range_min = mktime64(tm->tm_year, tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min,
				  tm->tm_sec);
	tm = &info->range_max;
	rtc->range_max = mktime64(tm->tm_year, tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min,
				  tm->tm_sec);

	return 0;
}

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	if (ver->impl_id == TEE_IMPL_ID_OPTEE)
		return 1;
	else
		return 0;
}

static int optee_rtc_probe(struct device *dev)
{
	struct tee_client_device *rtc_device = to_tee_client_device(dev);
	struct tee_ioctl_open_session_arg sess2_arg = {0};
	struct tee_ioctl_open_session_arg sess_arg = {0};
	struct optee_rtc *priv;
	struct rtc_device *rtc;
	struct tee_shm *shm;
	int ret, err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	priv->rtc = rtc;

	/* Open context with TEE driver */
	priv->ctx = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR(priv->ctx))
		return -ENODEV;

	/* Open first session with rtc Pseudo Trusted App */
	export_uuid(sess_arg.uuid, &rtc_device->id.uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_REE_KERNEL;

	ret = tee_client_open_session(priv->ctx, &sess_arg, NULL);
	if (ret < 0 || sess_arg.ret != 0) {
		dev_err(dev, "tee_client_open_session failed, err: %x\n", sess_arg.ret);
		err = -EINVAL;
		goto out_ctx;
	}
	priv->session_id = sess_arg.session;

	/*
	 * Shared memory is used for passing an instance of either struct optee_rtc_info,
	 * struct optee_rtc_time or struct optee_rtc_alarm to OP-TEE service.
	 * The former is by definition large enough to cover both parameter cases.
	 */
	shm = tee_shm_alloc_kernel_buf(priv->ctx, sizeof(struct optee_rtc_info));
	if (IS_ERR(shm)) {
		dev_err(priv->dev, "tee_shm_alloc_kernel_buf failed\n");
		err = PTR_ERR(shm);
		goto out_sess;
	}

	priv->shm = shm;
	priv->dev = dev;
	dev_set_drvdata(dev, priv);

	rtc->ops = &optee_rtc_ops;

	err = optee_rtc_read_info(dev, rtc, &priv->features);
	if (err) {
		dev_err(dev, "Failed to get RTC features from OP-TEE\n");
		goto out_shm;
	}

	/* Handle feature's related setup before registering to rtc framework */
	if (priv->features & TA_RTC_FEATURE_ALARM) {
		priv->alarm_task = kthread_create(optee_rtc_handle_alarm_event,
						  priv, "rtc_alarm_evt");
		if (IS_ERR(priv->alarm_task)) {
			dev_err(dev, "Failed to create alarm thread\n");
			err = PTR_ERR(priv->alarm_task);
			goto out_shm;
		}

		/*
		 * In case of supported alarm feature on optee side, we create a kthread
		 * that will, in a new optee session, call a PTA interface "rtc_wait_alarm".
		 * This call return in case of alarm and in case of canceled alarm.
		 * The new optee session is therefore only needed in this case as we cannot
		 * use the same session for parallel calls to optee PTA.
		 * Hence one session is reserved to wait for alarms and the other to make
		 * standard calls to RTC PTA.
		 */

		/* Open second session with rtc Trusted App */
		export_uuid(sess2_arg.uuid, &rtc_device->id.uuid);
		sess2_arg.clnt_login = TEE_IOCTL_LOGIN_REE_KERNEL;

		ret = tee_client_open_session(priv->ctx, &sess2_arg, NULL);
		if (ret < 0 || sess2_arg.ret != 0) {
			dev_err(dev, "tee_client_open_session failed, err: %x\n", sess2_arg.ret);
			err = -EINVAL;
			goto out_thrd;
		}
		priv->session2_id = sess2_arg.session;

		if (priv->features & TA_RTC_FEATURE_WAKEUP_ALARM)
			device_init_wakeup(dev, true);
	}

	err = devm_rtc_register_device(rtc);
	if (err)
		goto out_wk;

	/*
	 * We must clear those bits after registering because registering a rtc_device
	 * will set them if it sees that .set_offset and .set_alarm are provided.
	 */
	if (!(priv->features & TA_RTC_FEATURE_CORRECTION))
		clear_bit(RTC_FEATURE_CORRECTION, rtc->features);
	if (!(priv->features & TA_RTC_FEATURE_ALARM))
		clear_bit(RTC_FEATURE_ALARM, rtc->features);

	/* Start the thread after the rtc is setup */
	if (priv->alarm_task) {
		wake_up_process(priv->alarm_task);
		dev_dbg(dev, "Wait alarm thread successfully started\n");
	}

	return 0;
out_wk:
	if (priv->features & TA_RTC_FEATURE_ALARM) {
		device_init_wakeup(dev, false);
		tee_client_close_session(priv->ctx, priv->session2_id);
	}
out_thrd:
	if (priv->features & TA_RTC_FEATURE_ALARM)
		kthread_stop(priv->alarm_task);
out_shm:
	tee_shm_free(priv->shm);
out_sess:
	tee_client_close_session(priv->ctx, priv->session_id);
out_ctx:
	tee_client_close_context(priv->ctx);

	return err;
}

static int optee_rtc_remove(struct device *dev)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);

	if (priv->features & TA_RTC_FEATURE_ALARM) {
		optee_rtc_cancel_wait_alarm(dev);
		kthread_stop(priv->alarm_task);
		device_init_wakeup(dev, false);
		tee_client_close_session(priv->ctx, priv->session2_id);
	}

	tee_shm_free(priv->shm);
	tee_client_close_session(priv->ctx, priv->session_id);
	tee_client_close_context(priv->ctx);

	return 0;
}

static int optee_rtc_suspend(struct device *dev)
{
	int res = optee_rtc_set_alarm_wake_status(dev, device_may_wakeup(dev));

	if (res) {
		dev_err(dev, "Unable to transmit wakeup information to optee rtc\n");
		return res;
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(optee_rtc_pm_ops, optee_rtc_suspend, NULL);

static const struct tee_client_device_id optee_rtc_id_table[] = {
	{UUID_INIT(0xf389f8c8, 0x845f, 0x496c,
		   0x8b, 0xbe, 0xd6, 0x4b, 0xd2, 0x4c, 0x92, 0xfd)},
	{}
};

MODULE_DEVICE_TABLE(tee, optee_rtc_id_table);

static struct tee_client_driver optee_rtc_driver = {
	.id_table	= optee_rtc_id_table,
	.driver		= {
		.name		= "optee_rtc",
		.bus		= &tee_bus_type,
		.probe		= optee_rtc_probe,
		.remove		= optee_rtc_remove,
		.pm		= pm_sleep_ptr(&optee_rtc_pm_ops),
	},
};

static int __init optee_rtc_mod_init(void)
{
	return driver_register(&optee_rtc_driver.driver);
}

static void __exit optee_rtc_mod_exit(void)
{
	driver_unregister(&optee_rtc_driver.driver);
}

module_init(optee_rtc_mod_init);
module_exit(optee_rtc_mod_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Clément Léger <clement.leger@bootlin.com>");
MODULE_DESCRIPTION("OP-TEE based RTC driver");
