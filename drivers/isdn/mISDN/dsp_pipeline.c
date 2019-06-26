// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dsp_pipeline.c: pipelined audio processing
 *
 * Copyright (C) 2007, Nadi Sarrar
 *
 * Nadi Sarrar <nadi@beronet.com>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/mISDNif.h>
#include <linux/mISDNdsp.h>
#include <linux/export.h>
#include "dsp.h"
#include "dsp_hwec.h"

/* uncomment for debugging */
/*#define PIPELINE_DEBUG*/

struct dsp_pipeline_entry {
	struct mISDN_dsp_element *elem;
	void                *p;
	struct list_head     list;
};
struct dsp_element_entry {
	struct mISDN_dsp_element *elem;
	struct device	     dev;
	struct list_head     list;
};

static LIST_HEAD(dsp_elements);

/* sysfs */
static struct class *elements_class;

static ssize_t
attr_show_args(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mISDN_dsp_element *elem = dev_get_drvdata(dev);
	int i;
	char *p = buf;

	*buf = 0;
	for (i = 0; i < elem->num_args; i++)
		p += sprintf(p, "Name:        %s\n%s%s%sDescription: %s\n\n",
			     elem->args[i].name,
			     elem->args[i].def ? "Default:     " : "",
			     elem->args[i].def ? elem->args[i].def : "",
			     elem->args[i].def ? "\n" : "",
			     elem->args[i].desc);

	return p - buf;
}

static struct device_attribute element_attributes[] = {
	__ATTR(args, 0444, attr_show_args, NULL),
};

static void
mISDN_dsp_dev_release(struct device *dev)
{
	struct dsp_element_entry *entry =
		container_of(dev, struct dsp_element_entry, dev);
	list_del(&entry->list);
	kfree(entry);
}

int mISDN_dsp_element_register(struct mISDN_dsp_element *elem)
{
	struct dsp_element_entry *entry;
	int ret, i;

	if (!elem)
		return -EINVAL;

	entry = kzalloc(sizeof(struct dsp_element_entry), GFP_ATOMIC);
	if (!entry)
		return -ENOMEM;

	entry->elem = elem;

	entry->dev.class = elements_class;
	entry->dev.release = mISDN_dsp_dev_release;
	dev_set_drvdata(&entry->dev, elem);
	dev_set_name(&entry->dev, "%s", elem->name);
	ret = device_register(&entry->dev);
	if (ret) {
		printk(KERN_ERR "%s: failed to register %s\n",
		       __func__, elem->name);
		goto err1;
	}
	list_add_tail(&entry->list, &dsp_elements);

	for (i = 0; i < ARRAY_SIZE(element_attributes); ++i) {
		ret = device_create_file(&entry->dev,
					 &element_attributes[i]);
		if (ret) {
			printk(KERN_ERR "%s: failed to create device file\n",
			       __func__);
			goto err2;
		}
	}

#ifdef PIPELINE_DEBUG
	printk(KERN_DEBUG "%s: %s registered\n", __func__, elem->name);
#endif

	return 0;

err2:
	device_unregister(&entry->dev);
	return ret;
err1:
	kfree(entry);
	return ret;
}
EXPORT_SYMBOL(mISDN_dsp_element_register);

void mISDN_dsp_element_unregister(struct mISDN_dsp_element *elem)
{
	struct dsp_element_entry *entry, *n;

	if (!elem)
		return;

	list_for_each_entry_safe(entry, n, &dsp_elements, list)
		if (entry->elem == elem) {
			device_unregister(&entry->dev);
#ifdef PIPELINE_DEBUG
			printk(KERN_DEBUG "%s: %s unregistered\n",
			       __func__, elem->name);
#endif
			return;
		}
	printk(KERN_ERR "%s: element %s not in list.\n", __func__, elem->name);
}
EXPORT_SYMBOL(mISDN_dsp_element_unregister);

int dsp_pipeline_module_init(void)
{
	elements_class = class_create(THIS_MODULE, "dsp_pipeline");
	if (IS_ERR(elements_class))
		return PTR_ERR(elements_class);

#ifdef PIPELINE_DEBUG
	printk(KERN_DEBUG "%s: dsp pipeline module initialized\n", __func__);
#endif

	dsp_hwec_init();

	return 0;
}

void dsp_pipeline_module_exit(void)
{
	struct dsp_element_entry *entry, *n;

	dsp_hwec_exit();

	class_destroy(elements_class);

	list_for_each_entry_safe(entry, n, &dsp_elements, list) {
		list_del(&entry->list);
		printk(KERN_WARNING "%s: element was still registered: %s\n",
		       __func__, entry->elem->name);
		kfree(entry);
	}

#ifdef PIPELINE_DEBUG
	printk(KERN_DEBUG "%s: dsp pipeline module exited\n", __func__);
#endif
}

int dsp_pipeline_init(struct dsp_pipeline *pipeline)
{
	if (!pipeline)
		return -EINVAL;

	INIT_LIST_HEAD(&pipeline->list);

#ifdef PIPELINE_DEBUG
	printk(KERN_DEBUG "%s: dsp pipeline ready\n", __func__);
#endif

	return 0;
}

static inline void _dsp_pipeline_destroy(struct dsp_pipeline *pipeline)
{
	struct dsp_pipeline_entry *entry, *n;

	list_for_each_entry_safe(entry, n, &pipeline->list, list) {
		list_del(&entry->list);
		if (entry->elem == dsp_hwec)
			dsp_hwec_disable(container_of(pipeline, struct dsp,
						      pipeline));
		else
			entry->elem->free(entry->p);
		kfree(entry);
	}
}

void dsp_pipeline_destroy(struct dsp_pipeline *pipeline)
{

	if (!pipeline)
		return;

	_dsp_pipeline_destroy(pipeline);

#ifdef PIPELINE_DEBUG
	printk(KERN_DEBUG "%s: dsp pipeline destroyed\n", __func__);
#endif
}

int dsp_pipeline_build(struct dsp_pipeline *pipeline, const char *cfg)
{
	int incomplete = 0, found = 0;
	char *dup, *tok, *name, *args;
	struct dsp_element_entry *entry, *n;
	struct dsp_pipeline_entry *pipeline_entry;
	struct mISDN_dsp_element *elem;

	if (!pipeline)
		return -EINVAL;

	if (!list_empty(&pipeline->list))
		_dsp_pipeline_destroy(pipeline);

	dup = kstrdup(cfg, GFP_ATOMIC);
	if (!dup)
		return 0;
	while ((tok = strsep(&dup, "|"))) {
		if (!strlen(tok))
			continue;
		name = strsep(&tok, "(");
		args = strsep(&tok, ")");
		if (args && !*args)
			args = NULL;

		list_for_each_entry_safe(entry, n, &dsp_elements, list)
			if (!strcmp(entry->elem->name, name)) {
				elem = entry->elem;

				pipeline_entry = kmalloc(sizeof(struct
								dsp_pipeline_entry), GFP_ATOMIC);
				if (!pipeline_entry) {
					printk(KERN_ERR "%s: failed to add "
					       "entry to pipeline: %s (out of "
					       "memory)\n", __func__, elem->name);
					incomplete = 1;
					goto _out;
				}
				pipeline_entry->elem = elem;

				if (elem == dsp_hwec) {
					/* This is a hack to make the hwec
					   available as a pipeline module */
					dsp_hwec_enable(container_of(pipeline,
								     struct dsp, pipeline), args);
					list_add_tail(&pipeline_entry->list,
						      &pipeline->list);
				} else {
					pipeline_entry->p = elem->new(args);
					if (pipeline_entry->p) {
						list_add_tail(&pipeline_entry->
							      list, &pipeline->list);
#ifdef PIPELINE_DEBUG
						printk(KERN_DEBUG "%s: created "
						       "instance of %s%s%s\n",
						       __func__, name, args ?
						       " with args " : "", args ?
						       args : "");
#endif
					} else {
						printk(KERN_ERR "%s: failed "
						       "to add entry to pipeline: "
						       "%s (new() returned NULL)\n",
						       __func__, elem->name);
						kfree(pipeline_entry);
						incomplete = 1;
					}
				}
				found = 1;
				break;
			}

		if (found)
			found = 0;
		else {
			printk(KERN_ERR "%s: element not found, skipping: "
			       "%s\n", __func__, name);
			incomplete = 1;
		}
	}

_out:
	if (!list_empty(&pipeline->list))
		pipeline->inuse = 1;
	else
		pipeline->inuse = 0;

#ifdef PIPELINE_DEBUG
	printk(KERN_DEBUG "%s: dsp pipeline built%s: %s\n",
	       __func__, incomplete ? " incomplete" : "", cfg);
#endif
	kfree(dup);
	return 0;
}

void dsp_pipeline_process_tx(struct dsp_pipeline *pipeline, u8 *data, int len)
{
	struct dsp_pipeline_entry *entry;

	if (!pipeline)
		return;

	list_for_each_entry(entry, &pipeline->list, list)
		if (entry->elem->process_tx)
			entry->elem->process_tx(entry->p, data, len);
}

void dsp_pipeline_process_rx(struct dsp_pipeline *pipeline, u8 *data, int len,
			     unsigned int txlen)
{
	struct dsp_pipeline_entry *entry;

	if (!pipeline)
		return;

	list_for_each_entry_reverse(entry, &pipeline->list, list)
		if (entry->elem->process_rx)
			entry->elem->process_rx(entry->p, data, len, txlen);
}
