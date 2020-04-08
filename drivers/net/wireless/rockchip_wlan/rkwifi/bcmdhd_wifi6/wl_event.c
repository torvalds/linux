/* SPDX-License-Identifier: GPL-2.0 */

#if defined(WL_EXT_IAPSTA) || defined(USE_IW)
#include <bcmendian.h>
#include <wl_android.h>
#include <dhd_config.h>

#define EVENT_ERROR(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_ERROR_LEVEL) { \
			printk(KERN_ERR "[dhd-%s] EVENT-ERROR) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define EVENT_TRACE(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_TRACE_LEVEL) { \
			printk(KERN_ERR "[dhd-%s] EVENT-TRACE) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define EVENT_DBG(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_DBG_LEVEL) { \
			printk(KERN_ERR "[dhd-%s] EVENT-DBG) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
#define BCM_SET_LIST_FIRST_ENTRY(entry, ptr, type, member) \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"") \
(entry) = list_first_entry((ptr), type, member); \
_Pragma("GCC diagnostic pop") \

#define BCM_SET_CONTAINER_OF(entry, ptr, type, member) \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"") \
entry = container_of((ptr), type, member); \
_Pragma("GCC diagnostic pop") \

#else
#define BCM_SET_LIST_FIRST_ENTRY(entry, ptr, type, member) \
(entry) = list_first_entry((ptr), type, member); \

#define BCM_SET_CONTAINER_OF(entry, ptr, type, member) \
entry = container_of((ptr), type, member); \

#endif /* STRICT_GCC_WARNINGS */

#ifdef DHD_MAX_IFS
#define WL_MAX_IFS DHD_MAX_IFS
#else
#define WL_MAX_IFS 16
#endif

/* event queue for cfg80211 main event */
struct wl_event_q {
	struct list_head eq_list;
	u32 etype;
	wl_event_msg_t emsg;
	s8 edata[1];
};

typedef s32(*EXT_EVENT_HANDLER) (struct net_device *dev, void *cb_argu,
	const wl_event_msg_t *e, void *data);

typedef struct event_handler_list {
	struct event_handler_list *next;
	struct net_device *dev;
	uint32 etype;
	EXT_EVENT_HANDLER cb_func;
	void *cb_argu;
	wl_event_prio_t prio;
} event_handler_list_t;

typedef struct event_handler_head {
	event_handler_list_t *evt_head;
} event_handler_head_t;

typedef struct wl_event_params {
	dhd_pub_t *pub;
	struct net_device *dev[WL_MAX_IFS];
	struct event_handler_head evt_head;
	struct list_head eq_list;	/* used for event queue */
	spinlock_t eq_lock;	/* for event queue synchronization */
	struct workqueue_struct *event_workq;   /* workqueue for event */
	struct work_struct event_work;		/* work item for event */
	struct mutex event_sync;
} wl_event_params_t;

static unsigned long
wl_ext_event_lock_eq(struct wl_event_params *event_params)
{
	unsigned long flags;

	spin_lock_irqsave(&event_params->eq_lock, flags);
	return flags;
}

static void
wl_ext_event_unlock_eq(struct wl_event_params *event_params, unsigned long flags)
{
	spin_unlock_irqrestore(&event_params->eq_lock, flags);
}

static void
wl_ext_event_init_eq_lock(struct wl_event_params *event_params)
{
	spin_lock_init(&event_params->eq_lock);
}

static void
wl_ext_event_init_eq(struct wl_event_params *event_params)
{
	wl_ext_event_init_eq_lock(event_params);
	INIT_LIST_HEAD(&event_params->eq_list);
}

static void
wl_ext_event_flush_eq(struct wl_event_params *event_params)
{
	struct wl_event_q *e;
	unsigned long flags;

	flags = wl_ext_event_lock_eq(event_params);
	while (!list_empty_careful(&event_params->eq_list)) {
		BCM_SET_LIST_FIRST_ENTRY(e, &event_params->eq_list, struct wl_event_q, eq_list);
		list_del(&e->eq_list);
		kfree(e);
	}
	wl_ext_event_unlock_eq(event_params, flags);
}

/*
* retrieve first queued event from head
*/

static struct wl_event_q *
wl_ext_event_deq_event(struct wl_event_params *event_params)
{
	struct wl_event_q *e = NULL;
	unsigned long flags;

	flags = wl_ext_event_lock_eq(event_params);
	if (likely(!list_empty(&event_params->eq_list))) {
		BCM_SET_LIST_FIRST_ENTRY(e, &event_params->eq_list, struct wl_event_q, eq_list);
		list_del(&e->eq_list);
	}
	wl_ext_event_unlock_eq(event_params, flags);

	return e;
}

/*
 * push event to tail of the queue
 */

static s32
wl_ext_event_enq_event(struct wl_event_params *event_params, u32 event,
	const wl_event_msg_t *msg, void *data)
{
	struct wl_event_q *e;
	s32 err = 0;
	uint32 evtq_size;
	uint32 data_len;
	unsigned long flags;
	gfp_t aflags;

	data_len = 0;
	if (data)
		data_len = ntoh32(msg->datalen);
	evtq_size = sizeof(struct wl_event_q) + data_len;
	aflags = (in_atomic()) ? GFP_ATOMIC : GFP_KERNEL;
	e = kzalloc(evtq_size, aflags);
	if (unlikely(!e)) {
		EVENT_ERROR("wlan", "event alloc failed\n");
		return -ENOMEM;
	}
	e->etype = event;
	memcpy(&e->emsg, msg, sizeof(wl_event_msg_t));
	if (data)
		memcpy(e->edata, data, data_len);
	flags = wl_ext_event_lock_eq(event_params);
	list_add_tail(&e->eq_list, &event_params->eq_list);
	wl_ext_event_unlock_eq(event_params, flags);

	return err;
}

static void
wl_ext_event_put_event(struct wl_event_q *e)
{
	kfree(e);
}

static void
wl_ext_event_handler(struct work_struct *work_data)
{
	struct wl_event_params *event_params = NULL;
	struct wl_event_q *e;
	struct net_device *dev = NULL;
	struct event_handler_list *evt_node;
	dhd_pub_t *dhd;
	unsigned long flags = 0;

	BCM_SET_CONTAINER_OF(event_params, work_data, struct wl_event_params, event_work);
	DHD_EVENT_WAKE_LOCK(event_params->pub);
	while ((e = wl_ext_event_deq_event(event_params))) {
		if (e->emsg.ifidx >= DHD_MAX_IFS) {
			EVENT_ERROR("wlan", "ifidx=%d not in range\n", e->emsg.ifidx);
			goto fail;
		}
		dev = event_params->dev[e->emsg.ifidx];
		if (!dev) {
			EVENT_DBG("wlan", "ifidx=%d dev not ready\n", e->emsg.ifidx);
			goto fail;
		}
		dhd = dhd_get_pub(dev);
		if (e->etype > WLC_E_LAST) {
			EVENT_TRACE(dev->name, "Unknown Event (%d): ignoring\n", e->etype);
			goto fail;
		}
		DHD_GENERAL_LOCK(dhd, flags);
		if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(dhd)) {
			EVENT_ERROR(dev->name, "BUS is DOWN.\n");
			DHD_GENERAL_UNLOCK(dhd, flags);
			goto fail;
		}
		DHD_GENERAL_UNLOCK(dhd, flags);
		EVENT_DBG(dev->name, "event type (%d)\n", e->etype);
		mutex_lock(&event_params->event_sync);
		evt_node = event_params->evt_head.evt_head;
		for (;evt_node;) {
			if (evt_node->dev == dev &&
					(evt_node->etype == e->etype || evt_node->etype == WLC_E_LAST))
				evt_node->cb_func(dev, evt_node->cb_argu, &e->emsg, e->edata);
			evt_node = evt_node->next;
		}
		mutex_unlock(&event_params->event_sync);
fail:
		wl_ext_event_put_event(e);
	}
	DHD_EVENT_WAKE_UNLOCK(event_params->pub);
}

void
wl_ext_event_send(void *params, const wl_event_msg_t * e, void *data)
{
	struct wl_event_params *event_params = params;
	u32 event_type = ntoh32(e->event_type);

	if (event_params == NULL) {
		EVENT_ERROR("wlan", "Stale event %d(%s) ignored\n",
			event_type, bcmevent_get_name(event_type));
		return;
	}

	if (event_params->event_workq == NULL) {
		EVENT_ERROR("wlan", "Event handler is not created %d(%s)\n",
			event_type, bcmevent_get_name(event_type));
		return;
	}

	if (likely(!wl_ext_event_enq_event(event_params, event_type, e, data))) {
		queue_work(event_params->event_workq, &event_params->event_work);
	}
}

static s32
wl_ext_event_create_handler(struct wl_event_params *event_params)
{
	int ret = 0;
	EVENT_TRACE("wlan", "Enter\n");

	/* Allocate workqueue for event */
	if (!event_params->event_workq) {
		event_params->event_workq = alloc_workqueue("ext_eventd", WQ_MEM_RECLAIM | WQ_HIGHPRI, 0);
	}

	if (!event_params->event_workq) {
		EVENT_ERROR("wlan", "event_workq alloc_workqueue failed\n");
		ret = -ENOMEM;
	} else {
		INIT_WORK(&event_params->event_work, wl_ext_event_handler);
	}
	return ret;
}

static void
wl_ext_event_free(struct wl_event_params *event_params)
{
	struct event_handler_list *node, *cur, **evt_head;

	evt_head = &event_params->evt_head.evt_head;
	node = *evt_head;

	for (;node;) {
		EVENT_TRACE(node->dev->name, "Free etype=%d\n", node->etype);
		cur = node;
		node = cur->next;
		kfree(cur);
	}
	*evt_head = NULL;
}

static void
wl_ext_event_destroy_handler(struct wl_event_params *event_params)
{
	if (event_params && event_params->event_workq) {
		cancel_work_sync(&event_params->event_work);
		destroy_workqueue(event_params->event_workq);
		event_params->event_workq = NULL;
	}
}

int
wl_ext_event_register(struct net_device *dev, dhd_pub_t *dhd, uint32 event,
	void *cb_func, void *data, wl_event_prio_t prio)
{
	struct wl_event_params *event_params = dhd->event_params;
	struct event_handler_list *node, *leaf, *node_prev, **evt_head;
	int ret = 0;

	if (event_params) {
		mutex_lock(&event_params->event_sync);
		evt_head = &event_params->evt_head.evt_head;
		node = *evt_head;
		for (;node;) {
			if (node->dev == dev && node->etype == event && node->cb_func == cb_func) {
				EVENT_TRACE(dev->name, "skip event %d\n", event);
				mutex_unlock(&event_params->event_sync);
				return 0;
			}
			node = node->next;
		}
		leaf = kmalloc(sizeof(event_handler_list_t), GFP_KERNEL);
		if (!leaf) {
			EVENT_ERROR(dev->name, "Memory alloc failure %d for event %d\n",
				(int)sizeof(event_handler_list_t), event);
			mutex_unlock(&event_params->event_sync);
			return -ENOMEM;
		}
		leaf->next = NULL;
		leaf->dev = dev;
		leaf->etype = event;
		leaf->cb_func = cb_func;
		leaf->cb_argu = data;
		leaf->prio = prio;
		if (*evt_head == NULL) {
			*evt_head = leaf;
		} else {
			node = *evt_head;
			node_prev = NULL;
			for (;node;) {
				if (node->prio <= prio) {
					leaf->next = node;
					if (node_prev)
						node_prev->next = leaf;
					else
						*evt_head = leaf;
					break;
				} else if (node->next == NULL) {
					node->next = leaf;
					break;
				}
				node_prev = node;
				node = node->next;
			}
		}
		EVENT_TRACE(dev->name, "event %d registered\n", event);
		mutex_unlock(&event_params->event_sync);
	} else {
		EVENT_ERROR(dev->name, "event_params not ready %d\n", event);
		ret = -ENODEV;
	}

	return ret;
}

void
wl_ext_event_deregister(struct net_device *dev, dhd_pub_t *dhd,
	uint32 event, void *cb_func)
{
	struct wl_event_params *event_params = dhd->event_params;
	struct event_handler_list *node, *prev, **evt_head;
	int tmp = 0;

	if (event_params) {
		mutex_lock(&event_params->event_sync);
		evt_head = &event_params->evt_head.evt_head;
		node = *evt_head;
		prev = node;
		for (;node;) {
			if (node->dev == dev && node->etype == event && node->cb_func == cb_func) {
				if (node == *evt_head) {
					tmp = 1;
					*evt_head = node->next;
				} else {
					tmp = 0;
					prev->next = node->next;
				}
				EVENT_TRACE(dev->name, "event %d deregistered\n", event);
				kfree(node);
				if (tmp == 1) {
					node = *evt_head;
					prev = node;
				} else {
					node = prev->next;
				}
				continue;
			}
			prev = node;
			node = node->next;
		}
		mutex_unlock(&event_params->event_sync);
	} else {
		EVENT_ERROR(dev->name, "event_params not ready %d\n", event);
	}
}

static s32
wl_ext_event_init_priv(struct wl_event_params *event_params)
{
	s32 err = 0;

	mutex_init(&event_params->event_sync);
	wl_ext_event_init_eq(event_params);
	if (wl_ext_event_create_handler(event_params))
		return -ENOMEM;

	return err;
}

static void
wl_ext_event_deinit_priv(struct wl_event_params *event_params)
{
	wl_ext_event_destroy_handler(event_params);
	wl_ext_event_flush_eq(event_params);
	wl_ext_event_free(event_params);
}

int
wl_ext_event_attach_netdev(struct net_device *net, int ifidx, uint8 bssidx)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_event_params *event_params = dhd->event_params;

	EVENT_TRACE(net->name, "ifidx=%d, bssidx=%d\n", ifidx, bssidx);
	if (event_params && ifidx < WL_MAX_IFS) {
		event_params->dev[ifidx] = net;
	}

	return 0;
}

int
wl_ext_event_dettach_netdev(struct net_device *net, int ifidx)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_event_params *event_params = dhd->event_params;

	EVENT_TRACE(net->name, "ifidx=%d\n", ifidx);
	if (event_params && ifidx < WL_MAX_IFS) {
		event_params->dev[ifidx] = NULL;
	}

	return 0;
}

s32
wl_ext_event_attach(struct net_device *dev, dhd_pub_t *dhdp)
{
	struct wl_event_params *event_params = NULL;
	s32 err = 0;

	event_params = kmalloc(sizeof(wl_event_params_t), GFP_KERNEL);
	if (!event_params) {
		EVENT_ERROR(dev->name, "Failed to allocate memory (%zu)\n",
			sizeof(wl_event_params_t));
		return -ENOMEM;
	}
	dhdp->event_params = event_params;
	memset(event_params, 0, sizeof(wl_event_params_t));
	event_params->pub = dhdp;

	err = wl_ext_event_init_priv(event_params);
	if (err) {
		EVENT_ERROR(dev->name, "Failed to wl_ext_event_init_priv (%d)\n", err);
		goto ext_attach_out;
	}

	return err;
ext_attach_out:
	wl_ext_event_dettach(dhdp);
	return err;
}

void
wl_ext_event_dettach(dhd_pub_t *dhdp)
{
	struct wl_event_params *event_params = dhdp->event_params;

	if (event_params) {
		wl_ext_event_deinit_priv(event_params);
		kfree(event_params);
		dhdp->event_params = NULL;
	}
}
#endif
