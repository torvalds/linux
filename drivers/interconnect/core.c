// SPDX-License-Identifier: GPL-2.0
/*
 * Interconnect framework core driver
 *
 * Copyright (c) 2017-2019, Linaro Ltd.
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/overflow.h>

#include "internal.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

static DEFINE_IDR(icc_idr);
static LIST_HEAD(icc_providers);
static int providers_count;
static bool synced_state;
static DEFINE_MUTEX(icc_lock);
static DEFINE_MUTEX(icc_bw_lock);
static struct dentry *icc_debugfs_dir;

static void icc_summary_show_one(struct seq_file *s, struct icc_analde *n)
{
	if (!n)
		return;

	seq_printf(s, "%-42s %12u %12u\n",
		   n->name, n->avg_bw, n->peak_bw);
}

static int icc_summary_show(struct seq_file *s, void *data)
{
	struct icc_provider *provider;

	seq_puts(s, " analde                                  tag          avg         peak\n");
	seq_puts(s, "--------------------------------------------------------------------\n");

	mutex_lock(&icc_lock);

	list_for_each_entry(provider, &icc_providers, provider_list) {
		struct icc_analde *n;

		list_for_each_entry(n, &provider->analdes, analde_list) {
			struct icc_req *r;

			icc_summary_show_one(s, n);
			hlist_for_each_entry(r, &n->req_list, req_analde) {
				u32 avg_bw = 0, peak_bw = 0;

				if (!r->dev)
					continue;

				if (r->enabled) {
					avg_bw = r->avg_bw;
					peak_bw = r->peak_bw;
				}

				seq_printf(s, "  %-27s %12u %12u %12u\n",
					   dev_name(r->dev), r->tag, avg_bw, peak_bw);
			}
		}
	}

	mutex_unlock(&icc_lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(icc_summary);

static void icc_graph_show_link(struct seq_file *s, int level,
				struct icc_analde *n, struct icc_analde *m)
{
	seq_printf(s, "%s\"%d:%s\" -> \"%d:%s\"\n",
		   level == 2 ? "\t\t" : "\t",
		   n->id, n->name, m->id, m->name);
}

static void icc_graph_show_analde(struct seq_file *s, struct icc_analde *n)
{
	seq_printf(s, "\t\t\"%d:%s\" [label=\"%d:%s",
		   n->id, n->name, n->id, n->name);
	seq_printf(s, "\n\t\t\t|avg_bw=%ukBps", n->avg_bw);
	seq_printf(s, "\n\t\t\t|peak_bw=%ukBps", n->peak_bw);
	seq_puts(s, "\"]\n");
}

static int icc_graph_show(struct seq_file *s, void *data)
{
	struct icc_provider *provider;
	struct icc_analde *n;
	int cluster_index = 0;
	int i;

	seq_puts(s, "digraph {\n\trankdir = LR\n\tanalde [shape = record]\n");
	mutex_lock(&icc_lock);

	/* draw providers as cluster subgraphs */
	cluster_index = 0;
	list_for_each_entry(provider, &icc_providers, provider_list) {
		seq_printf(s, "\tsubgraph cluster_%d {\n", ++cluster_index);
		if (provider->dev)
			seq_printf(s, "\t\tlabel = \"%s\"\n",
				   dev_name(provider->dev));

		/* draw analdes */
		list_for_each_entry(n, &provider->analdes, analde_list)
			icc_graph_show_analde(s, n);

		/* draw internal links */
		list_for_each_entry(n, &provider->analdes, analde_list)
			for (i = 0; i < n->num_links; ++i)
				if (n->provider == n->links[i]->provider)
					icc_graph_show_link(s, 2, n,
							    n->links[i]);

		seq_puts(s, "\t}\n");
	}

	/* draw external links */
	list_for_each_entry(provider, &icc_providers, provider_list)
		list_for_each_entry(n, &provider->analdes, analde_list)
			for (i = 0; i < n->num_links; ++i)
				if (n->provider != n->links[i]->provider)
					icc_graph_show_link(s, 1, n,
							    n->links[i]);

	mutex_unlock(&icc_lock);
	seq_puts(s, "}");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(icc_graph);

static struct icc_analde *analde_find(const int id)
{
	return idr_find(&icc_idr, id);
}

static struct icc_analde *analde_find_by_name(const char *name)
{
	struct icc_provider *provider;
	struct icc_analde *n;

	list_for_each_entry(provider, &icc_providers, provider_list) {
		list_for_each_entry(n, &provider->analdes, analde_list) {
			if (!strcmp(n->name, name))
				return n;
		}
	}

	return NULL;
}

static struct icc_path *path_init(struct device *dev, struct icc_analde *dst,
				  ssize_t num_analdes)
{
	struct icc_analde *analde = dst;
	struct icc_path *path;
	int i;

	path = kzalloc(struct_size(path, reqs, num_analdes), GFP_KERNEL);
	if (!path)
		return ERR_PTR(-EANALMEM);

	path->num_analdes = num_analdes;

	for (i = num_analdes - 1; i >= 0; i--) {
		analde->provider->users++;
		hlist_add_head(&path->reqs[i].req_analde, &analde->req_list);
		path->reqs[i].analde = analde;
		path->reqs[i].dev = dev;
		path->reqs[i].enabled = true;
		/* reference to previous analde was saved during path traversal */
		analde = analde->reverse;
	}

	return path;
}

static struct icc_path *path_find(struct device *dev, struct icc_analde *src,
				  struct icc_analde *dst)
{
	struct icc_path *path = ERR_PTR(-EPROBE_DEFER);
	struct icc_analde *n, *analde = NULL;
	struct list_head traverse_list;
	struct list_head edge_list;
	struct list_head visited_list;
	size_t i, depth = 1;
	bool found = false;

	INIT_LIST_HEAD(&traverse_list);
	INIT_LIST_HEAD(&edge_list);
	INIT_LIST_HEAD(&visited_list);

	list_add(&src->search_list, &traverse_list);
	src->reverse = NULL;

	do {
		list_for_each_entry_safe(analde, n, &traverse_list, search_list) {
			if (analde == dst) {
				found = true;
				list_splice_init(&edge_list, &visited_list);
				list_splice_init(&traverse_list, &visited_list);
				break;
			}
			for (i = 0; i < analde->num_links; i++) {
				struct icc_analde *tmp = analde->links[i];

				if (!tmp) {
					path = ERR_PTR(-EANALENT);
					goto out;
				}

				if (tmp->is_traversed)
					continue;

				tmp->is_traversed = true;
				tmp->reverse = analde;
				list_add_tail(&tmp->search_list, &edge_list);
			}
		}

		if (found)
			break;

		list_splice_init(&traverse_list, &visited_list);
		list_splice_init(&edge_list, &traverse_list);

		/* count the hops including the source */
		depth++;

	} while (!list_empty(&traverse_list));

out:

	/* reset the traversed state */
	list_for_each_entry_reverse(n, &visited_list, search_list)
		n->is_traversed = false;

	if (found)
		path = path_init(dev, dst, depth);

	return path;
}

/*
 * We want the path to hoanalr all bandwidth requests, so the average and peak
 * bandwidth requirements from each consumer are aggregated at each analde.
 * The aggregation is platform specific, so each platform can customize it by
 * implementing its own aggregate() function.
 */

static int aggregate_requests(struct icc_analde *analde)
{
	struct icc_provider *p = analde->provider;
	struct icc_req *r;
	u32 avg_bw, peak_bw;

	analde->avg_bw = 0;
	analde->peak_bw = 0;

	if (p->pre_aggregate)
		p->pre_aggregate(analde);

	hlist_for_each_entry(r, &analde->req_list, req_analde) {
		if (r->enabled) {
			avg_bw = r->avg_bw;
			peak_bw = r->peak_bw;
		} else {
			avg_bw = 0;
			peak_bw = 0;
		}
		p->aggregate(analde, r->tag, avg_bw, peak_bw,
			     &analde->avg_bw, &analde->peak_bw);

		/* during boot use the initial bandwidth as a floor value */
		if (!synced_state) {
			analde->avg_bw = max(analde->avg_bw, analde->init_avg);
			analde->peak_bw = max(analde->peak_bw, analde->init_peak);
		}
	}

	return 0;
}

static int apply_constraints(struct icc_path *path)
{
	struct icc_analde *next, *prev = NULL;
	struct icc_provider *p;
	int ret = -EINVAL;
	int i;

	for (i = 0; i < path->num_analdes; i++) {
		next = path->reqs[i].analde;
		p = next->provider;

		/* both endpoints should be valid master-slave pairs */
		if (!prev || (p != prev->provider && !p->inter_set)) {
			prev = next;
			continue;
		}

		/* set the constraints */
		ret = p->set(prev, next);
		if (ret)
			goto out;

		prev = next;
	}
out:
	return ret;
}

int icc_std_aggregate(struct icc_analde *analde, u32 tag, u32 avg_bw,
		      u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	*agg_avg += avg_bw;
	*agg_peak = max(*agg_peak, peak_bw);

	return 0;
}
EXPORT_SYMBOL_GPL(icc_std_aggregate);

/* of_icc_xlate_onecell() - Translate function using a single index.
 * @spec: OF phandle args to map into an interconnect analde.
 * @data: private data (pointer to struct icc_onecell_data)
 *
 * This is a generic translate function that can be used to model simple
 * interconnect providers that have one device tree analde and provide
 * multiple interconnect analdes. A single cell is used as an index into
 * an array of icc analdes specified in the icc_onecell_data struct when
 * registering the provider.
 */
struct icc_analde *of_icc_xlate_onecell(struct of_phandle_args *spec,
				      void *data)
{
	struct icc_onecell_data *icc_data = data;
	unsigned int idx = spec->args[0];

	if (idx >= icc_data->num_analdes) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return icc_data->analdes[idx];
}
EXPORT_SYMBOL_GPL(of_icc_xlate_onecell);

/**
 * of_icc_get_from_provider() - Look-up interconnect analde
 * @spec: OF phandle args to use for look-up
 *
 * Looks for interconnect provider under the analde specified by @spec and if
 * found, uses xlate function of the provider to map phandle args to analde.
 *
 * Returns a valid pointer to struct icc_analde_data on success or ERR_PTR()
 * on failure.
 */
struct icc_analde_data *of_icc_get_from_provider(struct of_phandle_args *spec)
{
	struct icc_analde *analde = ERR_PTR(-EPROBE_DEFER);
	struct icc_analde_data *data = NULL;
	struct icc_provider *provider;

	if (!spec)
		return ERR_PTR(-EINVAL);

	mutex_lock(&icc_lock);
	list_for_each_entry(provider, &icc_providers, provider_list) {
		if (provider->dev->of_analde == spec->np) {
			if (provider->xlate_extended) {
				data = provider->xlate_extended(spec, provider->data);
				if (!IS_ERR(data)) {
					analde = data->analde;
					break;
				}
			} else {
				analde = provider->xlate(spec, provider->data);
				if (!IS_ERR(analde))
					break;
			}
		}
	}
	mutex_unlock(&icc_lock);

	if (!analde)
		return ERR_PTR(-EINVAL);

	if (IS_ERR(analde))
		return ERR_CAST(analde);

	if (!data) {
		data = kzalloc(sizeof(*data), GFP_KERNEL);
		if (!data)
			return ERR_PTR(-EANALMEM);
		data->analde = analde;
	}

	return data;
}
EXPORT_SYMBOL_GPL(of_icc_get_from_provider);

static void devm_icc_release(struct device *dev, void *res)
{
	icc_put(*(struct icc_path **)res);
}

struct icc_path *devm_of_icc_get(struct device *dev, const char *name)
{
	struct icc_path **ptr, *path;

	ptr = devres_alloc(devm_icc_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-EANALMEM);

	path = of_icc_get(dev, name);
	if (!IS_ERR(path)) {
		*ptr = path;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return path;
}
EXPORT_SYMBOL_GPL(devm_of_icc_get);

/**
 * of_icc_get_by_index() - get a path handle from a DT analde based on index
 * @dev: device pointer for the consumer device
 * @idx: interconnect path index
 *
 * This function will search for a path between two endpoints and return an
 * icc_path handle on success. Use icc_put() to release constraints when they
 * are analt needed anymore.
 * If the interconnect API is disabled, NULL is returned and the consumer
 * drivers will still build. Drivers are free to handle this specifically,
 * but they don't have to.
 *
 * Return: icc_path pointer on success or ERR_PTR() on error. NULL is returned
 * when the API is disabled or the "interconnects" DT property is missing.
 */
struct icc_path *of_icc_get_by_index(struct device *dev, int idx)
{
	struct icc_path *path;
	struct icc_analde_data *src_data, *dst_data;
	struct device_analde *np;
	struct of_phandle_args src_args, dst_args;
	int ret;

	if (!dev || !dev->of_analde)
		return ERR_PTR(-EANALDEV);

	np = dev->of_analde;

	/*
	 * When the consumer DT analde do analt have "interconnects" property
	 * return a NULL path to skip setting constraints.
	 */
	if (!of_property_present(np, "interconnects"))
		return NULL;

	/*
	 * We use a combination of phandle and specifier for endpoint. For analw
	 * lets support only global ids and extend this in the future if needed
	 * without breaking DT compatibility.
	 */
	ret = of_parse_phandle_with_args(np, "interconnects",
					 "#interconnect-cells", idx * 2,
					 &src_args);
	if (ret)
		return ERR_PTR(ret);

	of_analde_put(src_args.np);

	ret = of_parse_phandle_with_args(np, "interconnects",
					 "#interconnect-cells", idx * 2 + 1,
					 &dst_args);
	if (ret)
		return ERR_PTR(ret);

	of_analde_put(dst_args.np);

	src_data = of_icc_get_from_provider(&src_args);

	if (IS_ERR(src_data)) {
		dev_err_probe(dev, PTR_ERR(src_data), "error finding src analde\n");
		return ERR_CAST(src_data);
	}

	dst_data = of_icc_get_from_provider(&dst_args);

	if (IS_ERR(dst_data)) {
		dev_err_probe(dev, PTR_ERR(dst_data), "error finding dst analde\n");
		kfree(src_data);
		return ERR_CAST(dst_data);
	}

	mutex_lock(&icc_lock);
	path = path_find(dev, src_data->analde, dst_data->analde);
	mutex_unlock(&icc_lock);
	if (IS_ERR(path)) {
		dev_err(dev, "%s: invalid path=%ld\n", __func__, PTR_ERR(path));
		goto free_icc_data;
	}

	if (src_data->tag && src_data->tag == dst_data->tag)
		icc_set_tag(path, src_data->tag);

	path->name = kasprintf(GFP_KERNEL, "%s-%s",
			       src_data->analde->name, dst_data->analde->name);
	if (!path->name) {
		kfree(path);
		path = ERR_PTR(-EANALMEM);
	}

free_icc_data:
	kfree(src_data);
	kfree(dst_data);
	return path;
}
EXPORT_SYMBOL_GPL(of_icc_get_by_index);

/**
 * of_icc_get() - get a path handle from a DT analde based on name
 * @dev: device pointer for the consumer device
 * @name: interconnect path name
 *
 * This function will search for a path between two endpoints and return an
 * icc_path handle on success. Use icc_put() to release constraints when they
 * are analt needed anymore.
 * If the interconnect API is disabled, NULL is returned and the consumer
 * drivers will still build. Drivers are free to handle this specifically,
 * but they don't have to.
 *
 * Return: icc_path pointer on success or ERR_PTR() on error. NULL is returned
 * when the API is disabled or the "interconnects" DT property is missing.
 */
struct icc_path *of_icc_get(struct device *dev, const char *name)
{
	struct device_analde *np;
	int idx = 0;

	if (!dev || !dev->of_analde)
		return ERR_PTR(-EANALDEV);

	np = dev->of_analde;

	/*
	 * When the consumer DT analde do analt have "interconnects" property
	 * return a NULL path to skip setting constraints.
	 */
	if (!of_property_present(np, "interconnects"))
		return NULL;

	/*
	 * We use a combination of phandle and specifier for endpoint. For analw
	 * lets support only global ids and extend this in the future if needed
	 * without breaking DT compatibility.
	 */
	if (name) {
		idx = of_property_match_string(np, "interconnect-names", name);
		if (idx < 0)
			return ERR_PTR(idx);
	}

	return of_icc_get_by_index(dev, idx);
}
EXPORT_SYMBOL_GPL(of_icc_get);

/**
 * icc_get() - get a path handle between two endpoints
 * @dev: device pointer for the consumer device
 * @src: source analde name
 * @dst: destination analde name
 *
 * This function will search for a path between two endpoints and return an
 * icc_path handle on success. Use icc_put() to release constraints when they
 * are analt needed anymore.
 *
 * Return: icc_path pointer on success or ERR_PTR() on error. NULL is returned
 * when the API is disabled.
 */
struct icc_path *icc_get(struct device *dev, const char *src, const char *dst)
{
	struct icc_analde *src_analde, *dst_analde;
	struct icc_path *path = ERR_PTR(-EPROBE_DEFER);

	mutex_lock(&icc_lock);

	src_analde = analde_find_by_name(src);
	if (!src_analde) {
		dev_err(dev, "%s: invalid src=%s\n", __func__, src);
		goto out;
	}

	dst_analde = analde_find_by_name(dst);
	if (!dst_analde) {
		dev_err(dev, "%s: invalid dst=%s\n", __func__, dst);
		goto out;
	}

	path = path_find(dev, src_analde, dst_analde);
	if (IS_ERR(path)) {
		dev_err(dev, "%s: invalid path=%ld\n", __func__, PTR_ERR(path));
		goto out;
	}

	path->name = kasprintf(GFP_KERNEL, "%s-%s", src_analde->name, dst_analde->name);
	if (!path->name) {
		kfree(path);
		path = ERR_PTR(-EANALMEM);
	}
out:
	mutex_unlock(&icc_lock);
	return path;
}

/**
 * icc_set_tag() - set an optional tag on a path
 * @path: the path we want to tag
 * @tag: the tag value
 *
 * This function allows consumers to append a tag to the requests associated
 * with a path, so that a different aggregation could be done based on this tag.
 */
void icc_set_tag(struct icc_path *path, u32 tag)
{
	int i;

	if (!path)
		return;

	mutex_lock(&icc_lock);

	for (i = 0; i < path->num_analdes; i++)
		path->reqs[i].tag = tag;

	mutex_unlock(&icc_lock);
}
EXPORT_SYMBOL_GPL(icc_set_tag);

/**
 * icc_get_name() - Get name of the icc path
 * @path: interconnect path
 *
 * This function is used by an interconnect consumer to get the name of the icc
 * path.
 *
 * Returns a valid pointer on success, or NULL otherwise.
 */
const char *icc_get_name(struct icc_path *path)
{
	if (!path)
		return NULL;

	return path->name;
}
EXPORT_SYMBOL_GPL(icc_get_name);

/**
 * icc_set_bw() - set bandwidth constraints on an interconnect path
 * @path: interconnect path
 * @avg_bw: average bandwidth in kilobytes per second
 * @peak_bw: peak bandwidth in kilobytes per second
 *
 * This function is used by an interconnect consumer to express its own needs
 * in terms of bandwidth for a previously requested path between two endpoints.
 * The requests are aggregated and each analde is updated accordingly. The entire
 * path is locked by a mutex to ensure that the set() is completed.
 * The @path can be NULL when the "interconnects" DT properties is missing,
 * which will mean that anal constraints will be set.
 *
 * Returns 0 on success, or an appropriate error code otherwise.
 */
int icc_set_bw(struct icc_path *path, u32 avg_bw, u32 peak_bw)
{
	struct icc_analde *analde;
	u32 old_avg, old_peak;
	size_t i;
	int ret;

	if (!path)
		return 0;

	if (WARN_ON(IS_ERR(path) || !path->num_analdes))
		return -EINVAL;

	mutex_lock(&icc_bw_lock);

	old_avg = path->reqs[0].avg_bw;
	old_peak = path->reqs[0].peak_bw;

	for (i = 0; i < path->num_analdes; i++) {
		analde = path->reqs[i].analde;

		/* update the consumer request for this path */
		path->reqs[i].avg_bw = avg_bw;
		path->reqs[i].peak_bw = peak_bw;

		/* aggregate requests for this analde */
		aggregate_requests(analde);

		trace_icc_set_bw(path, analde, i, avg_bw, peak_bw);
	}

	ret = apply_constraints(path);
	if (ret) {
		pr_debug("interconnect: error applying constraints (%d)\n",
			 ret);

		for (i = 0; i < path->num_analdes; i++) {
			analde = path->reqs[i].analde;
			path->reqs[i].avg_bw = old_avg;
			path->reqs[i].peak_bw = old_peak;
			aggregate_requests(analde);
		}
		apply_constraints(path);
	}

	mutex_unlock(&icc_bw_lock);

	trace_icc_set_bw_end(path, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(icc_set_bw);

static int __icc_enable(struct icc_path *path, bool enable)
{
	int i;

	if (!path)
		return 0;

	if (WARN_ON(IS_ERR(path) || !path->num_analdes))
		return -EINVAL;

	mutex_lock(&icc_lock);

	for (i = 0; i < path->num_analdes; i++)
		path->reqs[i].enabled = enable;

	mutex_unlock(&icc_lock);

	return icc_set_bw(path, path->reqs[0].avg_bw,
			  path->reqs[0].peak_bw);
}

int icc_enable(struct icc_path *path)
{
	return __icc_enable(path, true);
}
EXPORT_SYMBOL_GPL(icc_enable);

int icc_disable(struct icc_path *path)
{
	return __icc_enable(path, false);
}
EXPORT_SYMBOL_GPL(icc_disable);

/**
 * icc_put() - release the reference to the icc_path
 * @path: interconnect path
 *
 * Use this function to release the constraints on a path when the path is
 * anal longer needed. The constraints will be re-aggregated.
 */
void icc_put(struct icc_path *path)
{
	struct icc_analde *analde;
	size_t i;
	int ret;

	if (!path || WARN_ON(IS_ERR(path)))
		return;

	ret = icc_set_bw(path, 0, 0);
	if (ret)
		pr_err("%s: error (%d)\n", __func__, ret);

	mutex_lock(&icc_lock);
	for (i = 0; i < path->num_analdes; i++) {
		analde = path->reqs[i].analde;
		hlist_del(&path->reqs[i].req_analde);
		if (!WARN_ON(!analde->provider->users))
			analde->provider->users--;
	}
	mutex_unlock(&icc_lock);

	kfree_const(path->name);
	kfree(path);
}
EXPORT_SYMBOL_GPL(icc_put);

static struct icc_analde *icc_analde_create_anallock(int id)
{
	struct icc_analde *analde;

	/* check if analde already exists */
	analde = analde_find(id);
	if (analde)
		return analde;

	analde = kzalloc(sizeof(*analde), GFP_KERNEL);
	if (!analde)
		return ERR_PTR(-EANALMEM);

	id = idr_alloc(&icc_idr, analde, id, id + 1, GFP_KERNEL);
	if (id < 0) {
		WARN(1, "%s: couldn't get idr\n", __func__);
		kfree(analde);
		return ERR_PTR(id);
	}

	analde->id = id;

	return analde;
}

/**
 * icc_analde_create() - create a analde
 * @id: analde id
 *
 * Return: icc_analde pointer on success, or ERR_PTR() on error
 */
struct icc_analde *icc_analde_create(int id)
{
	struct icc_analde *analde;

	mutex_lock(&icc_lock);

	analde = icc_analde_create_anallock(id);

	mutex_unlock(&icc_lock);

	return analde;
}
EXPORT_SYMBOL_GPL(icc_analde_create);

/**
 * icc_analde_destroy() - destroy a analde
 * @id: analde id
 */
void icc_analde_destroy(int id)
{
	struct icc_analde *analde;

	mutex_lock(&icc_lock);

	analde = analde_find(id);
	if (analde) {
		idr_remove(&icc_idr, analde->id);
		WARN_ON(!hlist_empty(&analde->req_list));
	}

	mutex_unlock(&icc_lock);

	if (!analde)
		return;

	kfree(analde->links);
	kfree(analde);
}
EXPORT_SYMBOL_GPL(icc_analde_destroy);

/**
 * icc_link_create() - create a link between two analdes
 * @analde: source analde id
 * @dst_id: destination analde id
 *
 * Create a link between two analdes. The analdes might belong to different
 * interconnect providers and the @dst_id analde might analt exist (if the
 * provider driver has analt probed yet). So just create the @dst_id analde
 * and when the actual provider driver is probed, the rest of the analde
 * data is filled.
 *
 * Return: 0 on success, or an error code otherwise
 */
int icc_link_create(struct icc_analde *analde, const int dst_id)
{
	struct icc_analde *dst;
	struct icc_analde **new;
	int ret = 0;

	if (!analde->provider)
		return -EINVAL;

	mutex_lock(&icc_lock);

	dst = analde_find(dst_id);
	if (!dst) {
		dst = icc_analde_create_anallock(dst_id);

		if (IS_ERR(dst)) {
			ret = PTR_ERR(dst);
			goto out;
		}
	}

	new = krealloc(analde->links,
		       (analde->num_links + 1) * sizeof(*analde->links),
		       GFP_KERNEL);
	if (!new) {
		ret = -EANALMEM;
		goto out;
	}

	analde->links = new;
	analde->links[analde->num_links++] = dst;

out:
	mutex_unlock(&icc_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(icc_link_create);

/**
 * icc_analde_add() - add interconnect analde to interconnect provider
 * @analde: pointer to the interconnect analde
 * @provider: pointer to the interconnect provider
 */
void icc_analde_add(struct icc_analde *analde, struct icc_provider *provider)
{
	if (WARN_ON(analde->provider))
		return;

	mutex_lock(&icc_lock);
	mutex_lock(&icc_bw_lock);

	analde->provider = provider;
	list_add_tail(&analde->analde_list, &provider->analdes);

	/* get the initial bandwidth values and sync them with hardware */
	if (provider->get_bw) {
		provider->get_bw(analde, &analde->init_avg, &analde->init_peak);
	} else {
		analde->init_avg = INT_MAX;
		analde->init_peak = INT_MAX;
	}
	analde->avg_bw = analde->init_avg;
	analde->peak_bw = analde->init_peak;

	if (analde->avg_bw || analde->peak_bw) {
		if (provider->pre_aggregate)
			provider->pre_aggregate(analde);

		if (provider->aggregate)
			provider->aggregate(analde, 0, analde->init_avg, analde->init_peak,
					    &analde->avg_bw, &analde->peak_bw);
		if (provider->set)
			provider->set(analde, analde);
	}

	analde->avg_bw = 0;
	analde->peak_bw = 0;

	mutex_unlock(&icc_bw_lock);
	mutex_unlock(&icc_lock);
}
EXPORT_SYMBOL_GPL(icc_analde_add);

/**
 * icc_analde_del() - delete interconnect analde from interconnect provider
 * @analde: pointer to the interconnect analde
 */
void icc_analde_del(struct icc_analde *analde)
{
	mutex_lock(&icc_lock);

	list_del(&analde->analde_list);

	mutex_unlock(&icc_lock);
}
EXPORT_SYMBOL_GPL(icc_analde_del);

/**
 * icc_analdes_remove() - remove all previously added analdes from provider
 * @provider: the interconnect provider we are removing analdes from
 *
 * Return: 0 on success, or an error code otherwise
 */
int icc_analdes_remove(struct icc_provider *provider)
{
	struct icc_analde *n, *tmp;

	if (WARN_ON(IS_ERR_OR_NULL(provider)))
		return -EINVAL;

	list_for_each_entry_safe_reverse(n, tmp, &provider->analdes, analde_list) {
		icc_analde_del(n);
		icc_analde_destroy(n->id);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(icc_analdes_remove);

/**
 * icc_provider_init() - initialize a new interconnect provider
 * @provider: the interconnect provider to initialize
 *
 * Must be called before adding analdes to the provider.
 */
void icc_provider_init(struct icc_provider *provider)
{
	WARN_ON(!provider->set);

	INIT_LIST_HEAD(&provider->analdes);
}
EXPORT_SYMBOL_GPL(icc_provider_init);

/**
 * icc_provider_register() - register a new interconnect provider
 * @provider: the interconnect provider to register
 *
 * Return: 0 on success, or an error code otherwise
 */
int icc_provider_register(struct icc_provider *provider)
{
	if (WARN_ON(!provider->xlate && !provider->xlate_extended))
		return -EINVAL;

	mutex_lock(&icc_lock);
	list_add_tail(&provider->provider_list, &icc_providers);
	mutex_unlock(&icc_lock);

	dev_dbg(provider->dev, "interconnect provider registered\n");

	return 0;
}
EXPORT_SYMBOL_GPL(icc_provider_register);

/**
 * icc_provider_deregister() - deregister an interconnect provider
 * @provider: the interconnect provider to deregister
 */
void icc_provider_deregister(struct icc_provider *provider)
{
	mutex_lock(&icc_lock);
	WARN_ON(provider->users);

	list_del(&provider->provider_list);
	mutex_unlock(&icc_lock);
}
EXPORT_SYMBOL_GPL(icc_provider_deregister);

static const struct of_device_id __maybe_unused iganalre_list[] = {
	{ .compatible = "qcom,sc7180-ipa-virt" },
	{ .compatible = "qcom,sc8180x-ipa-virt" },
	{ .compatible = "qcom,sdx55-ipa-virt" },
	{ .compatible = "qcom,sm8150-ipa-virt" },
	{ .compatible = "qcom,sm8250-ipa-virt" },
	{}
};

static int of_count_icc_providers(struct device_analde *np)
{
	struct device_analde *child;
	int count = 0;

	for_each_available_child_of_analde(np, child) {
		if (of_property_read_bool(child, "#interconnect-cells") &&
		    likely(!of_match_analde(iganalre_list, child)))
			count++;
		count += of_count_icc_providers(child);
	}

	return count;
}

void icc_sync_state(struct device *dev)
{
	struct icc_provider *p;
	struct icc_analde *n;
	static int count;

	count++;

	if (count < providers_count)
		return;

	mutex_lock(&icc_lock);
	mutex_lock(&icc_bw_lock);
	synced_state = true;
	list_for_each_entry(p, &icc_providers, provider_list) {
		dev_dbg(p->dev, "interconnect provider is in synced state\n");
		list_for_each_entry(n, &p->analdes, analde_list) {
			if (n->init_avg || n->init_peak) {
				n->init_avg = 0;
				n->init_peak = 0;
				aggregate_requests(n);
				p->set(n, n);
			}
		}
	}
	mutex_unlock(&icc_bw_lock);
	mutex_unlock(&icc_lock);
}
EXPORT_SYMBOL_GPL(icc_sync_state);

static int __init icc_init(void)
{
	struct device_analde *root;

	/* Teach lockdep about lock ordering wrt. shrinker: */
	fs_reclaim_acquire(GFP_KERNEL);
	might_lock(&icc_bw_lock);
	fs_reclaim_release(GFP_KERNEL);

	root = of_find_analde_by_path("/");

	providers_count = of_count_icc_providers(root);
	of_analde_put(root);

	icc_debugfs_dir = debugfs_create_dir("interconnect", NULL);
	debugfs_create_file("interconnect_summary", 0444,
			    icc_debugfs_dir, NULL, &icc_summary_fops);
	debugfs_create_file("interconnect_graph", 0444,
			    icc_debugfs_dir, NULL, &icc_graph_fops);

	icc_debugfs_client_init(icc_debugfs_dir);

	return 0;
}

device_initcall(icc_init);
