// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the Interconnect framework.
 *
 * Copyright (c) 2025 Kuan-Wei Chiu <visitorckw@gmail.com>
 *
 * This suite verifies the behavior of the interconnect core, including
 * topology construction, bandwidth aggregation, and path lifecycle.
 */

#include <kunit/platform_device.h>
#include <kunit/test.h>
#include <linux/interconnect-provider.h>
#include <linux/interconnect.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "internal.h"

enum {
	NODE_CPU,
	NODE_GPU,
	NODE_BUS,
	NODE_DDR,
	NODE_MAX
};

struct test_node_data {
	int id;
	const char *name;
	int num_links;
	int links[2];
};

/*
 * Static Topology:
 * CPU -\
 * -> BUS -> DDR
 * GPU -/
 */
static const struct test_node_data test_topology[] = {
	{ NODE_CPU, "cpu", 1, { NODE_BUS } },
	{ NODE_GPU, "gpu", 1, { NODE_BUS } },
	{ NODE_BUS, "bus", 1, { NODE_DDR } },
	{ NODE_DDR, "ddr", 0, { } },
};

struct icc_test_priv {
	struct icc_provider provider;
	struct platform_device *pdev;
	struct icc_node *nodes[NODE_MAX];
};

static struct icc_node *get_node(struct icc_test_priv *priv, int id)
{
	int idx = id - NODE_CPU;

	if (idx < 0 || idx >= ARRAY_SIZE(test_topology))
		return NULL;
	return priv->nodes[idx];
}

static int icc_test_set(struct icc_node *src, struct icc_node *dst)
{
	return 0;
}

static int icc_test_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
			      u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	return icc_std_aggregate(node, tag, avg_bw, peak_bw, agg_avg, agg_peak);
}

static struct icc_node *icc_test_xlate(const struct of_phandle_args *spec, void *data)
{
	return NULL;
}

static int icc_test_get_bw(struct icc_node *node, u32 *avg, u32 *peak)
{
	*avg = 0;
	*peak = 0;

	return 0;
}

static int icc_test_init(struct kunit *test)
{
	struct icc_test_priv *priv;
	struct icc_node *node;
	int i, j, ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);
	test->priv = priv;

	priv->pdev = kunit_platform_device_alloc(test, "icc-test-dev", -1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->pdev);
	KUNIT_ASSERT_EQ(test, kunit_platform_device_add(test, priv->pdev), 0);

	priv->provider.set = icc_test_set;
	priv->provider.aggregate = icc_test_aggregate;
	priv->provider.xlate = icc_test_xlate;
	priv->provider.get_bw = icc_test_get_bw;
	priv->provider.dev = &priv->pdev->dev;
	priv->provider.data = priv;
	INIT_LIST_HEAD(&priv->provider.nodes);

	ret = icc_provider_register(&priv->provider);
	KUNIT_ASSERT_EQ(test, ret, 0);

	for (i = 0; i < ARRAY_SIZE(test_topology); i++) {
		const struct test_node_data *data = &test_topology[i];

		node = icc_node_create(data->id);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, node);

		node->name = data->name;
		icc_node_add(node, &priv->provider);
		priv->nodes[i] = node;
	}

	for (i = 0; i < ARRAY_SIZE(test_topology); i++) {
		const struct test_node_data *data = &test_topology[i];
		struct icc_node *src = get_node(priv, data->id);

		for (j = 0; j < data->num_links; j++) {
			ret = icc_link_create(src, data->links[j]);
			KUNIT_ASSERT_EQ_MSG(test, ret, 0, "Failed to link %s->%d",
					    src->name, data->links[j]);
		}
	}

	icc_sync_state(&priv->pdev->dev);

	return 0;
}

static void icc_test_exit(struct kunit *test)
{
	struct icc_test_priv *priv = test->priv;

	icc_nodes_remove(&priv->provider);
	icc_provider_deregister(&priv->provider);
}

/*
 * Helper to construct a mock path.
 *
 * Because we are bypassing icc_get(), we must manually link the requests
 * to the nodes' req_list so that icc_std_aggregate() can discover them.
 */
static struct icc_path *icc_test_create_path(struct kunit *test,
					     struct icc_node **nodes, int num)
{
	struct icc_path *path;
	int i;

	path = kunit_kzalloc(test, struct_size(path, reqs, num), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, path);

	path->num_nodes = num;
	for (i = 0; i < num; i++) {
		path->reqs[i].node = nodes[i];
		hlist_add_head(&path->reqs[i].req_node, &nodes[i]->req_list);
	}
	path->name = "mock-path";

	return path;
}

static void icc_test_destroy_path(struct kunit *test, struct icc_path *path)
{
	int i;

	for (i = 0; i < path->num_nodes; i++)
		hlist_del(&path->reqs[i].req_node);

	kunit_kfree(test, path);
}

static void icc_test_topology_integrity(struct kunit *test)
{
	struct icc_test_priv *priv = test->priv;
	struct icc_node *cpu = get_node(priv, NODE_CPU);
	struct icc_node *bus = get_node(priv, NODE_BUS);

	KUNIT_EXPECT_EQ(test, cpu->num_links, 1);
	KUNIT_EXPECT_PTR_EQ(test, cpu->links[0], bus);
	KUNIT_EXPECT_PTR_EQ(test, cpu->provider, &priv->provider);
}

static void icc_test_set_bw(struct kunit *test)
{
	struct icc_test_priv *priv = test->priv;
	struct icc_path *path;
	struct icc_node *path_nodes[3];
	int ret;

	/* Path: CPU -> BUS -> DDR */
	path_nodes[0] = get_node(priv, NODE_CPU);
	path_nodes[1] = get_node(priv, NODE_BUS);
	path_nodes[2] = get_node(priv, NODE_DDR);

	path = icc_test_create_path(test, path_nodes, 3);

	ret = icc_enable(path);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = icc_set_bw(path, 1000, 2000);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, path_nodes[0]->avg_bw, 1000);
	KUNIT_EXPECT_EQ(test, path_nodes[0]->peak_bw, 2000);
	KUNIT_EXPECT_EQ(test, path_nodes[1]->avg_bw, 1000);
	KUNIT_EXPECT_EQ(test, path_nodes[1]->peak_bw, 2000);

	icc_set_tag(path, 0xABC);
	KUNIT_EXPECT_EQ(test, path->reqs[0].tag, 0xABC);

	icc_disable(path);
	KUNIT_EXPECT_EQ(test, path_nodes[0]->avg_bw, 0);

	icc_test_destroy_path(test, path);
}

static void icc_test_aggregation(struct kunit *test)
{
	struct icc_test_priv *priv = test->priv;
	struct icc_path *path_cpu, *path_gpu;
	struct icc_node *nodes_cpu[3], *nodes_gpu[2];
	struct icc_node *bus = get_node(priv, NODE_BUS);
	int ret;

	nodes_cpu[0] = get_node(priv, NODE_CPU);
	nodes_cpu[1] = bus;
	nodes_cpu[2] = get_node(priv, NODE_DDR);
	path_cpu = icc_test_create_path(test, nodes_cpu, 3);

	nodes_gpu[0] = get_node(priv, NODE_GPU);
	nodes_gpu[1] = bus;
	path_gpu = icc_test_create_path(test, nodes_gpu, 2);

	icc_enable(path_cpu);
	icc_enable(path_gpu);

	ret = icc_set_bw(path_cpu, 1000, 1000);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, bus->avg_bw, 1000);

	ret = icc_set_bw(path_gpu, 2000, 2000);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Bus aggregates: CPU(1000) + GPU(2000) */
	KUNIT_EXPECT_EQ(test, bus->avg_bw, 3000);
	/* Peak aggregates: max(CPU, GPU) */
	KUNIT_EXPECT_EQ(test, bus->peak_bw, 2000);

	icc_test_destroy_path(test, path_cpu);
	icc_test_destroy_path(test, path_gpu);
}

static void icc_test_bulk_ops(struct kunit *test)
{
	struct icc_test_priv *priv = test->priv;
	struct icc_node *nodes_cpu[3], *nodes_gpu[2];
	struct icc_bulk_data bulk[2];
	int ret;

	nodes_cpu[0] = get_node(priv, NODE_CPU);
	nodes_cpu[1] = get_node(priv, NODE_BUS);
	nodes_cpu[2] = get_node(priv, NODE_DDR);

	nodes_gpu[0] = get_node(priv, NODE_GPU);
	nodes_gpu[1] = get_node(priv, NODE_BUS);

	bulk[0].path = icc_test_create_path(test, nodes_cpu, 3);
	bulk[0].avg_bw = 500;
	bulk[0].peak_bw = 500;

	bulk[1].path = icc_test_create_path(test, nodes_gpu, 2);
	bulk[1].avg_bw = 600;
	bulk[1].peak_bw = 600;

	ret = icc_bulk_set_bw(2, bulk);
	KUNIT_EXPECT_EQ(test, ret, 0);
	/* Paths disabled, bandwidth should be 0 */
	KUNIT_EXPECT_EQ(test, get_node(priv, NODE_BUS)->avg_bw, 0);

	ret = icc_bulk_enable(2, bulk);
	KUNIT_EXPECT_EQ(test, ret, 0);
	/* Paths enabled, aggregation applies */
	KUNIT_EXPECT_EQ(test, get_node(priv, NODE_BUS)->avg_bw, 1100);

	icc_bulk_disable(2, bulk);
	KUNIT_EXPECT_EQ(test, get_node(priv, NODE_BUS)->avg_bw, 0);

	icc_test_destroy_path(test, bulk[0].path);
	icc_test_destroy_path(test, bulk[1].path);
}

static struct kunit_case icc_test_cases[] = {
	KUNIT_CASE(icc_test_topology_integrity),
	KUNIT_CASE(icc_test_set_bw),
	KUNIT_CASE(icc_test_aggregation),
	KUNIT_CASE(icc_test_bulk_ops),
	{}
};

static struct kunit_suite icc_test_suite = {
	.name = "interconnect",
	.init = icc_test_init,
	.exit = icc_test_exit,
	.test_cases = icc_test_cases,
};

kunit_test_suite(icc_test_suite);

MODULE_AUTHOR("Kuan-Wei Chiu <visitorckw@gmail.com>");
MODULE_DESCRIPTION("KUnit tests for the Interconnect framework");
MODULE_LICENSE("GPL");
