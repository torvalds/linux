// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2020 Marvell International Ltd. All rights reserved */

#include <linux/kernel.h>
#include <linux/list.h>

#include "prestera.h"
#include "prestera_hw.h"
#include "prestera_acl.h"
#include "prestera_flow.h"
#include "prestera_span.h"

struct prestera_span_entry {
	struct list_head list;
	struct prestera_port *port;
	refcount_t ref_count;
	u8 id;
};

struct prestera_span {
	struct prestera_switch *sw;
	struct list_head entries;
};

static struct prestera_span_entry *
prestera_span_entry_create(struct prestera_port *port, u8 span_id)
{
	struct prestera_span_entry *entry;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	refcount_set(&entry->ref_count, 1);
	entry->port = port;
	entry->id = span_id;
	list_add_tail(&entry->list, &port->sw->span->entries);

	return entry;
}

static void prestera_span_entry_del(struct prestera_span_entry *entry)
{
	list_del(&entry->list);
	kfree(entry);
}

static struct prestera_span_entry *
prestera_span_entry_find_by_id(struct prestera_span *span, u8 span_id)
{
	struct prestera_span_entry *entry;

	list_for_each_entry(entry, &span->entries, list) {
		if (entry->id == span_id)
			return entry;
	}

	return NULL;
}

static struct prestera_span_entry *
prestera_span_entry_find_by_port(struct prestera_span *span,
				 struct prestera_port *port)
{
	struct prestera_span_entry *entry;

	list_for_each_entry(entry, &span->entries, list) {
		if (entry->port == port)
			return entry;
	}

	return NULL;
}

static int prestera_span_get(struct prestera_port *port, u8 *span_id)
{
	u8 new_span_id;
	struct prestera_switch *sw = port->sw;
	struct prestera_span_entry *entry;
	int err;

	entry = prestera_span_entry_find_by_port(sw->span, port);
	if (entry) {
		refcount_inc(&entry->ref_count);
		*span_id = entry->id;
		return 0;
	}

	err = prestera_hw_span_get(port, &new_span_id);
	if (err)
		return err;

	entry = prestera_span_entry_create(port, new_span_id);
	if (IS_ERR(entry)) {
		prestera_hw_span_release(sw, new_span_id);
		return PTR_ERR(entry);
	}

	*span_id = new_span_id;
	return 0;
}

static int prestera_span_put(struct prestera_switch *sw, u8 span_id)
{
	struct prestera_span_entry *entry;
	int err;

	entry = prestera_span_entry_find_by_id(sw->span, span_id);
	if (!entry)
		return false;

	if (!refcount_dec_and_test(&entry->ref_count))
		return 0;

	err = prestera_hw_span_release(sw, span_id);
	if (err)
		return err;

	prestera_span_entry_del(entry);
	return 0;
}

int prestera_span_rule_add(struct prestera_flow_block_binding *binding,
			   struct prestera_port *to_port,
			   bool ingress)
{
	struct prestera_switch *sw = binding->port->sw;
	u8 span_id;
	int err;

	if (binding->span_id != PRESTERA_SPAN_INVALID_ID)
		/* port already in mirroring */
		return -EEXIST;

	err = prestera_span_get(to_port, &span_id);
	if (err)
		return err;

	err = prestera_hw_span_bind(binding->port, span_id, ingress);
	if (err) {
		prestera_span_put(sw, span_id);
		return err;
	}

	binding->span_id = span_id;
	return 0;
}

int prestera_span_rule_del(struct prestera_flow_block_binding *binding,
			   bool ingress)
{
	int err;

	err = prestera_hw_span_unbind(binding->port, ingress);
	if (err)
		return err;

	err = prestera_span_put(binding->port->sw, binding->span_id);
	if (err)
		return err;

	binding->span_id = PRESTERA_SPAN_INVALID_ID;
	return 0;
}

int prestera_span_init(struct prestera_switch *sw)
{
	struct prestera_span *span;

	span = kzalloc(sizeof(*span), GFP_KERNEL);
	if (!span)
		return -ENOMEM;

	INIT_LIST_HEAD(&span->entries);

	sw->span = span;
	span->sw = sw;

	return 0;
}

void prestera_span_fini(struct prestera_switch *sw)
{
	struct prestera_span *span = sw->span;

	WARN_ON(!list_empty(&span->entries));
	kfree(span);
}
