/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Xen Event Channels (internal header)
 *
 * Copyright (C) 2013 Citrix Systems R&D Ltd.
 */
#ifndef __EVENTS_INTERNAL_H__
#define __EVENTS_INTERNAL_H__

struct evtchn_loop_ctrl;

struct evtchn_ops {
	unsigned (*max_channels)(void);
	unsigned (*nr_channels)(void);

	int (*setup)(evtchn_port_t port);
	void (*remove)(evtchn_port_t port, unsigned int cpu);
	void (*bind_to_cpu)(evtchn_port_t evtchn, unsigned int cpu,
			    unsigned int old_cpu);

	void (*clear_pending)(evtchn_port_t port);
	void (*set_pending)(evtchn_port_t port);
	bool (*is_pending)(evtchn_port_t port);
	void (*mask)(evtchn_port_t port);
	void (*unmask)(evtchn_port_t port);

	void (*handle_events)(unsigned cpu, struct evtchn_loop_ctrl *ctrl);
	void (*resume)(void);

	int (*percpu_init)(unsigned int cpu);
	int (*percpu_deinit)(unsigned int cpu);
};

extern const struct evtchn_ops *evtchn_ops;

void handle_irq_for_port(evtchn_port_t port, struct evtchn_loop_ctrl *ctrl);

unsigned int cpu_from_evtchn(evtchn_port_t evtchn);

static inline unsigned xen_evtchn_max_channels(void)
{
	return evtchn_ops->max_channels();
}

/*
 * Do any ABI specific setup for a bound event channel before it can
 * be unmasked and used.
 */
static inline int xen_evtchn_port_setup(evtchn_port_t evtchn)
{
	if (evtchn_ops->setup)
		return evtchn_ops->setup(evtchn);
	return 0;
}

static inline void xen_evtchn_port_remove(evtchn_port_t evtchn,
					  unsigned int cpu)
{
	if (evtchn_ops->remove)
		evtchn_ops->remove(evtchn, cpu);
}

static inline void xen_evtchn_port_bind_to_cpu(evtchn_port_t evtchn,
					       unsigned int cpu,
					       unsigned int old_cpu)
{
	evtchn_ops->bind_to_cpu(evtchn, cpu, old_cpu);
}

static inline void clear_evtchn(evtchn_port_t port)
{
	evtchn_ops->clear_pending(port);
}

static inline void set_evtchn(evtchn_port_t port)
{
	evtchn_ops->set_pending(port);
}

static inline bool test_evtchn(evtchn_port_t port)
{
	return evtchn_ops->is_pending(port);
}

static inline void mask_evtchn(evtchn_port_t port)
{
	return evtchn_ops->mask(port);
}

static inline void unmask_evtchn(evtchn_port_t port)
{
	return evtchn_ops->unmask(port);
}

static inline void xen_evtchn_handle_events(unsigned cpu,
					    struct evtchn_loop_ctrl *ctrl)
{
	return evtchn_ops->handle_events(cpu, ctrl);
}

static inline void xen_evtchn_resume(void)
{
	if (evtchn_ops->resume)
		evtchn_ops->resume();
}

void xen_evtchn_2l_init(void);
int xen_evtchn_fifo_init(void);

#endif /* #ifndef __EVENTS_INTERNAL_H__ */
