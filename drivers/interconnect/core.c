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
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/overflow.h>

static DEFINE_IDR(icc_idr);
static LIST_HEAD(icc_providers);
static DEFINE_MUTEX(icc_lock);
static struct dentry *icc_debugfs_dir;

/**
 * struct icc_req - constraints that are attached to each node
 * @req_node: entry in list of requests for the particular @node
 * @node: the interconnect node to which this constraint applies
 * @dev: reference to the device that sets the constraints
 * @tag: path tag (optional)
 * @avg_bw: an integer describing the average bandwidth in kBps
 * @peak_bw: an integer describing the peak bandwidth in kBps
 */
struct icc_req {
	struct hlist_node req_node;
	struct icc_node *node;
	struct device *dev;
	u32 tag;
	u32 avg_bw;
	u32 peak_bw;
};

/**
 * struct icc_path - interconnect path structure
 * @num_nodes: number of hops (nodes)
 * @reqs: array of the requests applicable to this path of nodes
 */
struct icc_path {
	size_t num_nodes;
	struct icc_req reqs[];
};

static void icc_summary_show_one(struct seq_file *s, struct icc_node *n)
{
	if (!n)
		return;

	seq_printf(s, "%-30s %12u %12u\n",
		   n->name, n->avg_bw, n->peak_bw);
}

static int icc_summary_show(struct seq_file *s, void *data)
{
	struct icc_provider *provider;

	seq_puts(s, " node                                   avg         peak\n");
	seq_puts(s, "--------------------------------------------------------\n");

	mutex_lock(&icc_lock);

	list_for_each_entry(provider, &icc_providers, provider_list) {
		struct icc_node *n;

		list_for_each_entry(n, &provider->nodes, node_list) {
			struct icc_req *r;

			icc_summary_show_one(s, n);
			hlist_for_each_entry(r, &n->req_list, req_node) {
				if (!r->dev)
					continue;

				seq_printf(s, "    %-26s %12u %12u\n",
					   dev_name(r->dev), r->avg_bw,
					   r->peak_bw);
			}
		}
	}

	mutex_unlock(&icc_lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(icc_summary);

static struct icc_node *node_find(const int id)
{
	return idr_find(&icc_idr, id);
}

static struct icc_path *path_init(struct device *dev, struct icc_node *dst,
				  ssize_t num_nodes)
{
	struct icc_node *node = dst;
	struct icc_path *path;
	int i;

	path = kzalloc(struct_size(path, reqs, num_nodes), GFP_KERNEL);
	if (!path)
		return ERR_PTR(-ENOMEM);

	path->num_nodes = num_nodes;

	for (i = num_nodes - 1; i >= 0; i--) {
		node->provider->users++;
		hlist_add_head(&path->reqs[i].req_node, &node->req_list);
		path->reqs[i].node = node;
		path->reqs[i].dev = dev;
		/* reference to previous node was saved during path traversal */
		node = node->reverse;
	}

	return path;
}

static struct icc_path *path_find(struct device *dev, struct icc_node *src,
				  struct icc_node *dst)
{
	struct icc_path *path = ERR_PTR(-EPROBE_DEFER);
	struct icc_node *n, *node = NULL;
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
		list_for_each_entry_safe(node, n, &traverse_list, search_list) {
			if (node == dst) {
				found = true;
				list_splice_init(&edge_list, &visited_list);
				list_splice_init(&traverse_list, &visited_list);
				break;
			}
			for (i = 0; i < node->num_links; i++) {
				struct icc_node *tmp = node->links[i];

				if (!tmp) {
					path = ERR_PTR(-ENOENT);
					goto out;
				}

				if (tmp->is_traversed)
					continue;

				tmp->is_traversed = true;
				tmp->reverse = node;
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
 * We want the path to honor all bandwidth requests, so the average and peak
 * bandwidth requirements from each consumer are aggregated at each node.
 * The aggregation is platform specific, so each platform can customize it by
 * implementing its own aggregate() function.
 */

static int aggregate_requests(struct icc_node *node)
{
	struct icc_provider *p = node->provider;
	struct icc_req *r;

	node->avg_bw = 0;
	node->peak_bw = 0;

	if (p->pre_aggregate)
		p->pre_aggregate(node);

	hlist_for_each_entry(r, &node->req_list, req_node)
		p->aggregate(node, r->tag, r->avg_bw, r->peak_bw,
			     &node->avg_bw, &node->peak_bw);

	return 0;
}

static int apply_constraints(struct icc_path *path)
{
	struct icc_node *next, *prev = NULL;
	int ret = -EINVAL;
	int i;

	for (i = 0; i < path->num_nodes; i++) {
		next = path->reqs[i].node;

		/*
		 * Both endpoints should be valid master-slave pairs of the
		 * same interconnect provider that will be configured.
		 */
		if (!prev || next->provider != prev->provider) {
			prev = next;
			continue;
		}

		/* set the constraints */
		ret = next->provider->set(prev, next);
		if (ret)
			goto out;

		prev = next;
	}
out:
	return ret;
}

/* of_icc_xlate_onecell() - Translate function using a single index.
 * @spec: OF phandle args to map into an interconnect node.
 * @data: private data (pointer to struct icc_onecell_data)
 *
 * This is a generic translate function that can be used to model simple
 * interconnect providers that have one device tree node and provide
 * multiple interconnect nodes. A single cell is used as an index into
 * an array of icc nodes specified in the icc_onecell_data struct when
 * registering the provider.
 */
struct icc_node *of_icc_xlate_onecell(struct of_phandle_args *spec,
				      void *data)
{
	struct icc_onecell_data *icc_data = data;
	unsigned int idx = spec->args[0];

	if (idx >= icc_data->num_nodes) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return icc_data->nodes[idx];
}
EXPORT_SYMBOL_GPL(of_icc_xlate_onecell);

/**
 * of_icc_get_from_provider() - Look-up interconnect node
 * @spec: OF phandle args to use for look-up
 *
 * Looks for interconnect provider under the node specified by @spec and if
 * found, uses xlate function of the provider to map phandle args to node.
 *
 * Returns a valid pointer to struct icc_node on success or ERR_PTR()
 * on failure.
 */
static struct icc_node *of_icc_get_from_provider(struct of_phandle_args *spec)
{
	struct icc_node *node = ERR_PTR(-EPROBE_DEFER);
	struct icc_provider *provider;

	if (!spec || spec->args_count != 1)
		return ERR_PTR(-EINVAL);

	mutex_lock(&icc_lock);
	list_for_each_entry(provider, &icc_providers, provider_list) {
		if (provider->dev->of_node == spec->np)
			node = provider->xlate(spec, provider->data);
		if (!IS_ERR(node))
			break;
	}
	mutex_unlock(&icc_lock);

	return node;
}

/**
 * of_icc_get() - get a path handle from a DT node based on name
 * @dev: device pointer for the consumer device
 * @name: interconnect path name
 *
 * This function will search for a path between two endpoints and return an
 * icc_path handle on success. Use icc_put() to release constraints when they
 * are not needed anymore.
 * If the interconnect API is disabled, NULL is returned and the consumer
 * drivers will still build. Drivers are free to handle this specifically,
 * but they don't have to.
 *
 * Return: icc_path pointer on success or ERR_PTR() on error. NULL is returned
 * when the API is disabled or the "interconnects" DT property is missing.
 */
struct icc_path *of_icc_get(struct device *dev, const char *name)
{
	struct icc_path *path = ERR_PTR(-EPROBE_DEFER);
	struct icc_node *src_node, *dst_node;
	struct device_node *np = NULL;
	struct of_phandle_args src_args, dst_args;
	int idx = 0;
	int ret;

	if (!dev || !dev->of_node)
		return ERR_PTR(-ENODEV);

	np = dev->of_node;

	/*
	 * When the consumer DT node do not have "interconnects" property
	 * return a NULL path to skip setting constraints.
	 */
	if (!of_find_property(np, "interconnects", NULL))
		return NULL;

	/*
	 * We use a combination of phandle and specifier for endpoint. For now
	 * lets support only global ids and extend this in the future if needed
	 * without breaking DT compatibility.
	 */
	if (name) {
		idx = of_property_match_string(np, "interconnect-names", name);
		if (idx < 0)
			return ERR_PTR(idx);
	}

	ret = of_parse_phandle_with_args(np, "interconnects",
					 "#interconnect-cells", idx * 2,
					 &src_args);
	if (ret)
		return ERR_PTR(ret);

	of_node_put(src_args.np);

	ret = of_parse_phandle_with_args(np, "interconnects",
					 "#interconnect-cells", idx * 2 + 1,
					 &dst_args);
	if (ret)
		return ERR_PTR(ret);

	of_node_put(dst_args.np);

	src_node = of_icc_get_from_provider(&src_args);

	if (IS_ERR(src_node)) {
		if (PTR_ERR(src_node) != -EPROBE_DEFER)
			dev_err(dev, "error finding src node: %ld\n",
				PTR_ERR(src_node));
		return ERR_CAST(src_node);
	}

	dst_node = of_icc_get_from_provider(&dst_args);

	if (IS_ERR(dst_node)) {
		if (PTR_ERR(dst_node) != -EPROBE_DEFER)
			dev_err(dev, "error finding dst node: %ld\n",
				PTR_ERR(dst_node));
		return ERR_CAST(dst_node);
	}

	mutex_lock(&icc_lock);
	path = path_find(dev, src_node, dst_node);
	if (IS_ERR(path))
		dev_err(dev, "%s: invalid path=%ld\n", __func__, PTR_ERR(path));
	mutex_unlock(&icc_lock);

	return path;
}
EXPORT_SYMBOL_GPL(of_icc_get);

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

	for (i = 0; i < path->num_nodes; i++)
		path->reqs[i].tag = tag;
}
EXPORT_SYMBOL_GPL(icc_set_tag);

/**
 * icc_set_bw() - set bandwidth constraints on an interconnect path
 * @path: reference to the path returned by icc_get()
 * @avg_bw: average bandwidth in kilobytes per second
 * @peak_bw: peak bandwidth in kilobytes per second
 *
 * This function is used by an interconnect consumer to express its own needs
 * in terms of bandwidth for a previously requested path between two endpoints.
 * The requests are aggregated and each node is updated accordingly. The entire
 * path is locked by a mutex to ensure that the set() is completed.
 * The @path can be NULL when the "interconnects" DT properties is missing,
 * which will mean that no constraints will be set.
 *
 * Returns 0 on success, or an appropriate error code otherwise.
 */
int icc_set_bw(struct icc_path *path, u32 avg_bw, u32 peak_bw)
{
	struct icc_node *node;
	u32 old_avg, old_peak;
	size_t i;
	int ret;

	if (!path || !path->num_nodes)
		return 0;

	mutex_lock(&icc_lock);

	old_avg = path->reqs[0].avg_bw;
	old_peak = path->reqs[0].peak_bw;

	for (i = 0; i < path->num_nodes; i++) {
		node = path->reqs[i].node;

		/* update the consumer request for this path */
		path->reqs[i].avg_bw = avg_bw;
		path->reqs[i].peak_bw = peak_bw;

		/* aggregate requests for this node */
		aggregate_requests(node);
	}

	ret = apply_constraints(path);
	if (ret) {
		pr_debug("interconnect: error applying constraints (%d)\n",
			 ret);

		for (i = 0; i < path->num_nodes; i++) {
			node = path->reqs[i].node;
			path->reqs[i].avg_bw = old_avg;
			path->reqs[i].peak_bw = old_peak;
			aggregate_requests(node);
		}
		apply_constraints(path);
	}

	mutex_unlock(&icc_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(icc_set_bw);

/**
 * icc_get() - return a handle for path between two endpoints
 * @dev: the device requesting the path
 * @src_id: source device port id
 * @dst_id: destination device port id
 *
 * This function will search for a path between two endpoints and return an
 * icc_path handle on success. Use icc_put() to release
 * constraints when they are not needed anymore.
 * If the interconnect API is disabled, NULL is returned and the consumer
 * drivers will still build. Drivers are free to handle this specifically,
 * but they don't have to.
 *
 * Return: icc_path pointer on success, ERR_PTR() on error or NULL if the
 * interconnect API is disabled.
 */
struct icc_path *icc_get(struct device *dev, const int src_id, const int dst_id)
{
	struct icc_node *src, *dst;
	struct icc_path *path = ERR_PTR(-EPROBE_DEFER);

	mutex_lock(&icc_lock);

	src = node_find(src_id);
	if (!src)
		goto out;

	dst = node_find(dst_id);
	if (!dst)
		goto out;

	path = path_find(dev, src, dst);
	if (IS_ERR(path))
		dev_err(dev, "%s: invalid path=%ld\n", __func__, PTR_ERR(path));

out:
	mutex_unlock(&icc_lock);
	return path;
}
EXPORT_SYMBOL_GPL(icc_get);

/**
 * icc_put() - release the reference to the icc_path
 * @path: interconnect path
 *
 * Use this function to release the constraints on a path when the path is
 * no longer needed. The constraints will be re-aggregated.
 */
void icc_put(struct icc_path *path)
{
	struct icc_node *node;
	size_t i;
	int ret;

	if (!path || WARN_ON(IS_ERR(path)))
		return;

	ret = icc_set_bw(path, 0, 0);
	if (ret)
		pr_err("%s: error (%d)\n", __func__, ret);

	mutex_lock(&icc_lock);
	for (i = 0; i < path->num_nodes; i++) {
		node = path->reqs[i].node;
		hlist_del(&path->reqs[i].req_node);
		if (!WARN_ON(!node->provider->users))
			node->provider->users--;
	}
	mutex_unlock(&icc_lock);

	kfree(path);
}
EXPORT_SYMBOL_GPL(icc_put);

static struct icc_node *icc_node_create_nolock(int id)
{
	struct icc_node *node;

	/* check if node already exists */
	node = node_find(id);
	if (node)
		return node;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return ERR_PTR(-ENOMEM);

	id = idr_alloc(&icc_idr, node, id, id + 1, GFP_KERNEL);
	if (id < 0) {
		WARN(1, "%s: couldn't get idr\n", __func__);
		kfree(node);
		return ERR_PTR(id);
	}

	node->id = id;

	return node;
}

/**
 * icc_node_create() - create a node
 * @id: node id
 *
 * Return: icc_node pointer on success, or ERR_PTR() on error
 */
struct icc_node *icc_node_create(int id)
{
	struct icc_node *node;

	mutex_lock(&icc_lock);

	node = icc_node_create_nolock(id);

	mutex_unlock(&icc_lock);

	return node;
}
EXPORT_SYMBOL_GPL(icc_node_create);

/**
 * icc_node_destroy() - destroy a node
 * @id: node id
 */
void icc_node_destroy(int id)
{
	struct icc_node *node;

	mutex_lock(&icc_lock);

	node = node_find(id);
	if (node) {
		idr_remove(&icc_idr, node->id);
		WARN_ON(!hlist_empty(&node->req_list));
	}

	mutex_unlock(&icc_lock);

	kfree(node);
}
EXPORT_SYMBOL_GPL(icc_node_destroy);

/**
 * icc_link_create() - create a link between two nodes
 * @node: source node id
 * @dst_id: destination node id
 *
 * Create a link between two nodes. The nodes might belong to different
 * interconnect providers and the @dst_id node might not exist (if the
 * provider driver has not probed yet). So just create the @dst_id node
 * and when the actual provider driver is probed, the rest of the node
 * data is filled.
 *
 * Return: 0 on success, or an error code otherwise
 */
int icc_link_create(struct icc_node *node, const int dst_id)
{
	struct icc_node *dst;
	struct icc_node **new;
	int ret = 0;

	if (!node->provider)
		return -EINVAL;

	mutex_lock(&icc_lock);

	dst = node_find(dst_id);
	if (!dst) {
		dst = icc_node_create_nolock(dst_id);

		if (IS_ERR(dst)) {
			ret = PTR_ERR(dst);
			goto out;
		}
	}

	new = krealloc(node->links,
		       (node->num_links + 1) * sizeof(*node->links),
		       GFP_KERNEL);
	if (!new) {
		ret = -ENOMEM;
		goto out;
	}

	node->links = new;
	node->links[node->num_links++] = dst;

out:
	mutex_unlock(&icc_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(icc_link_create);

/**
 * icc_link_destroy() - destroy a link between two nodes
 * @src: pointer to source node
 * @dst: pointer to destination node
 *
 * Return: 0 on success, or an error code otherwise
 */
int icc_link_destroy(struct icc_node *src, struct icc_node *dst)
{
	struct icc_node **new;
	size_t slot;
	int ret = 0;

	if (IS_ERR_OR_NULL(src))
		return -EINVAL;

	if (IS_ERR_OR_NULL(dst))
		return -EINVAL;

	mutex_lock(&icc_lock);

	for (slot = 0; slot < src->num_links; slot++)
		if (src->links[slot] == dst)
			break;

	if (WARN_ON(slot == src->num_links)) {
		ret = -ENXIO;
		goto out;
	}

	src->links[slot] = src->links[--src->num_links];

	new = krealloc(src->links, src->num_links * sizeof(*src->links),
		       GFP_KERNEL);
	if (new)
		src->links = new;

out:
	mutex_unlock(&icc_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(icc_link_destroy);

/**
 * icc_node_add() - add interconnect node to interconnect provider
 * @node: pointer to the interconnect node
 * @provider: pointer to the interconnect provider
 */
void icc_node_add(struct icc_node *node, struct icc_provider *provider)
{
	mutex_lock(&icc_lock);

	node->provider = provider;
	list_add_tail(&node->node_list, &provider->nodes);

	mutex_unlock(&icc_lock);
}
EXPORT_SYMBOL_GPL(icc_node_add);

/**
 * icc_node_del() - delete interconnect node from interconnect provider
 * @node: pointer to the interconnect node
 */
void icc_node_del(struct icc_node *node)
{
	mutex_lock(&icc_lock);

	list_del(&node->node_list);

	mutex_unlock(&icc_lock);
}
EXPORT_SYMBOL_GPL(icc_node_del);

/**
 * icc_provider_add() - add a new interconnect provider
 * @provider: the interconnect provider that will be added into topology
 *
 * Return: 0 on success, or an error code otherwise
 */
int icc_provider_add(struct icc_provider *provider)
{
	if (WARN_ON(!provider->set))
		return -EINVAL;
	if (WARN_ON(!provider->xlate))
		return -EINVAL;

	mutex_lock(&icc_lock);

	INIT_LIST_HEAD(&provider->nodes);
	list_add_tail(&provider->provider_list, &icc_providers);

	mutex_unlock(&icc_lock);

	dev_dbg(provider->dev, "interconnect provider added to topology\n");

	return 0;
}
EXPORT_SYMBOL_GPL(icc_provider_add);

/**
 * icc_provider_del() - delete previously added interconnect provider
 * @provider: the interconnect provider that will be removed from topology
 *
 * Return: 0 on success, or an error code otherwise
 */
int icc_provider_del(struct icc_provider *provider)
{
	mutex_lock(&icc_lock);
	if (provider->users) {
		pr_warn("interconnect provider still has %d users\n",
			provider->users);
		mutex_unlock(&icc_lock);
		return -EBUSY;
	}

	if (!list_empty(&provider->nodes)) {
		pr_warn("interconnect provider still has nodes\n");
		mutex_unlock(&icc_lock);
		return -EBUSY;
	}

	list_del(&provider->provider_list);
	mutex_unlock(&icc_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(icc_provider_del);

static int __init icc_init(void)
{
	icc_debugfs_dir = debugfs_create_dir("interconnect", NULL);
	debugfs_create_file("interconnect_summary", 0444,
			    icc_debugfs_dir, NULL, &icc_summary_fops);
	return 0;
}

static void __exit icc_exit(void)
{
	debugfs_remove_recursive(icc_debugfs_dir);
}
module_init(icc_init);
module_exit(icc_exit);

MODULE_AUTHOR("Georgi Djakov <georgi.djakov@linaro.org>");
MODULE_DESCRIPTION("Interconnect Driver Core");
MODULE_LICENSE("GPL v2");
