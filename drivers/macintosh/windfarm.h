/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Windfarm PowerMac thermal control.
 *
 * (c) Copyright 2005 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 */

#ifndef __WINDFARM_H__
#define __WINDFARM_H__

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/device.h>

/* Display a 16.16 fixed point value */
#define FIX32TOPRINT(f)	(((s32)(f)) >> 16),(((((s32)(f)) & 0xffff) * 1000) >> 16)

/*
 * Control objects
 */

struct wf_control;

struct wf_control_ops {
	int			(*set_value)(struct wf_control *ct, s32 val);
	int			(*get_value)(struct wf_control *ct, s32 *val);
	s32			(*get_min)(struct wf_control *ct);
	s32			(*get_max)(struct wf_control *ct);
	void			(*release)(struct wf_control *ct);
	struct module		*owner;
};

struct wf_control {
	struct list_head		link;
	const struct wf_control_ops	*ops;
	const char			*name;
	int				type;
	struct kref			ref;
	struct device_attribute		attr;
	void				*priv;
};

#define WF_CONTROL_TYPE_GENERIC		0
#define WF_CONTROL_RPM_FAN		1
#define WF_CONTROL_PWM_FAN		2


/* Note about lifetime rules: wf_register_control() will initialize
 * the kref and wf_unregister_control will decrement it, thus the
 * object creating/disposing a given control shouldn't assume it
 * still exists after wf_unregister_control has been called.
 */
extern int wf_register_control(struct wf_control *ct);
extern void wf_unregister_control(struct wf_control *ct);
extern int wf_get_control(struct wf_control *ct);
extern void wf_put_control(struct wf_control *ct);

static inline int wf_control_set_max(struct wf_control *ct)
{
	s32 vmax = ct->ops->get_max(ct);
	return ct->ops->set_value(ct, vmax);
}

static inline int wf_control_set_min(struct wf_control *ct)
{
	s32 vmin = ct->ops->get_min(ct);
	return ct->ops->set_value(ct, vmin);
}

static inline int wf_control_set(struct wf_control *ct, s32 val)
{
	return ct->ops->set_value(ct, val);
}

static inline int wf_control_get(struct wf_control *ct, s32 *val)
{
	return ct->ops->get_value(ct, val);
}

static inline s32 wf_control_get_min(struct wf_control *ct)
{
	return ct->ops->get_min(ct);
}

static inline s32 wf_control_get_max(struct wf_control *ct)
{
	return ct->ops->get_max(ct);
}

/*
 * Sensor objects
 */

struct wf_sensor;

struct wf_sensor_ops {
	int			(*get_value)(struct wf_sensor *sr, s32 *val);
	void			(*release)(struct wf_sensor *sr);
	struct module		*owner;
};

struct wf_sensor {
	struct list_head		link;
	const struct wf_sensor_ops	*ops;
	const char			*name;
	struct kref			ref;
	struct device_attribute		attr;
	void				*priv;
};

/* Same lifetime rules as controls */
extern int wf_register_sensor(struct wf_sensor *sr);
extern void wf_unregister_sensor(struct wf_sensor *sr);
extern int wf_get_sensor(struct wf_sensor *sr);
extern void wf_put_sensor(struct wf_sensor *sr);

static inline int wf_sensor_get(struct wf_sensor *sr, s32 *val)
{
	return sr->ops->get_value(sr, val);
}

/* For use by clients. Note that we are a bit racy here since
 * notifier_block doesn't have a module owner field. I may fix
 * it one day ...
 *
 * LOCKING NOTE !
 *
 * All "events" except WF_EVENT_TICK are called with an internal mutex
 * held which will deadlock if you call basically any core routine.
 * So don't ! Just take note of the event and do your actual operations
 * from the ticker.
 *
 */
extern int wf_register_client(struct notifier_block *nb);
extern int wf_unregister_client(struct notifier_block *nb);

/* Overtemp conditions. Those are refcounted */
extern void wf_set_overtemp(void);
extern void wf_clear_overtemp(void);

#define WF_EVENT_NEW_CONTROL	0 /* param is wf_control * */
#define WF_EVENT_NEW_SENSOR	1 /* param is wf_sensor * */
#define WF_EVENT_OVERTEMP	2 /* no param */
#define WF_EVENT_NORMALTEMP	3 /* overtemp condition cleared */
#define WF_EVENT_TICK		4 /* 1 second tick */

/* Note: If that driver gets more broad use, we could replace the
 * simplistic overtemp bits with "environmental conditions". That
 * could then be used to also notify of things like fan failure,
 * case open, battery conditions, ...
 */

#endif /* __WINDFARM_H__ */
