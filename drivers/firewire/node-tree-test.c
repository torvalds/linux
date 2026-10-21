// SPDX-License-Identifier: GPL-2.0-only
//
// node-tree-test.c - An application of Kunit to test node tree.
//
// Copyright (c) 2026 Takashi Sakamoto
//
// This file can not be built independently since it is intentionally included in core-topology.c.

#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <kunit/device.h>

struct private_data {
	struct fw_card *card;
	unsigned int release_count;
};

static int node_tree_test_init(struct kunit *test)
{
	struct private_data *data;

	data = kunit_kzalloc(test, sizeof(*data), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, data);

	data->card = kunit_kzalloc(test, sizeof(struct fw_card), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, data->card);

	data->card->device = kunit_device_register(test, "dummy-device");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, data->card->device);

	test->priv = data;

	return 0;
}

static void node_tree_test_exit(struct kunit *test)
{
	struct private_data *data = test->priv;

	kunit_device_unregister(test, data->card->device);
	kunit_kfree(test, data->card);
	kunit_kfree(test, data);
}

static void release_fw_node(struct fw_card *card, struct fw_node *node, struct fw_node *parent)
{
	struct private_data *data = kunit_get_current_test()->priv;

	fw_node_put(node);
	++data->release_count;
}

static void node_tree_test_two_nodes(struct kunit *test)
{
	//       root
	// ++============++
	// ||    phy 1   ||
	// ||  P0 P1 P2  ||
	// ++===|==|==|==++
	//            |
	//      +-----+
	//      |
	// ++===|==x==x==++
	// ||  P0 P1 P2  ||
	// ||    phy 0   ||
	// ++============++
	//
	static const u32 self_id_sequence[] = {
		0x80000080,
		0x8100005e,
	};
	struct private_data *data = test->priv;
	struct fw_card *card = data->card;

	card->node_id = LOCAL_BUS | 0x01;

	card->local_node = build_tree(card, self_id_sequence, ARRAY_SIZE(self_id_sequence), 123);
	KUNIT_EXPECT_NOT_NULL(test, card->local_node);
	KUNIT_EXPECT_PTR_EQ(test, card->local_node, card->root_node);

	struct fw_node *node = card->root_node;
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x01);
	KUNIT_EXPECT_EQ(test, node->port_count, 3);
	KUNIT_EXPECT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_NULL(test, node->ports[1]);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[2]);

	struct fw_node *parent = node;
	node = parent->ports[2];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x00);
	KUNIT_EXPECT_EQ(test, node->port_count, 1);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[0], parent);

	++card->color;
	for_each_fw_node(card, card->root_node, release_fw_node);
	KUNIT_EXPECT_EQ(test, data->release_count, 2);
}

static void node_tree_test_two_nodes_1394a(struct kunit *test)
{
	//         root
	// ++===============++
	// ||      phy 0    ||
	// ||  P0 P1 P2 P3  ||
	// ++===|==|==|==|==++
	//            |
	//         +--+
	//         |
	// ++===|==|==|==|==|==++
	// ||  P0 P1 P2 P3 P4  ||
	// ||       phy 1      ||
	// ++==================++
	//
	// NOTE: Just for Self-ID Packets Zero and One.
	static const u32 self_id_sequence[] = {
		0x80000065, 0x80814000,
		0x8100005d, 0x81810000,
	};
	struct private_data *data = test->priv;
	struct fw_card *card = data->card;

	card->node_id = LOCAL_BUS | 0x01;

	card->local_node = build_tree(card, self_id_sequence, ARRAY_SIZE(self_id_sequence), 123);
	KUNIT_EXPECT_NOT_NULL(test, card->local_node);
	KUNIT_EXPECT_PTR_EQ(test, card->local_node, card->root_node);

	struct fw_node *node = card->root_node;
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x01);
	KUNIT_EXPECT_EQ(test, node->port_count, 4);
	KUNIT_EXPECT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_NULL(test, node->ports[1]);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[2]);
	KUNIT_EXPECT_NULL(test, node->ports[3]);

	struct fw_node *parent = node;
	node = parent->ports[2];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x00);
	KUNIT_EXPECT_EQ(test, node->port_count, 5);
	KUNIT_EXPECT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[1], parent);
	KUNIT_EXPECT_NULL(test, node->ports[2]);
	KUNIT_EXPECT_NULL(test, node->ports[3]);
	KUNIT_EXPECT_NULL(test, node->ports[4]);

	++card->color;
	for_each_fw_node(card, card->root_node, release_fw_node);
	KUNIT_EXPECT_EQ(test, data->release_count, 2);
}

static void node_tree_test_three_nodes_case0(struct kunit *test)
{
	//       root
	// ++============++
	// ||    phy 2   ||
	// ||  P0 P1 P2  ||
	// ++===|==|==|==++
	//      |     |
	//      +--+  +-----------------+
	//         |                    |
	// ++===|==|==x==++  ++===|==|==|==++
	// ||  P0 P1 P2  ||  ||  P0 P1 P2  ||
	// ||    phy 0   ||  ||    phy 1   ||
	// ++============++  ++============++
	//
	static const u32 self_id_sequence[] = {
		0x80000060,
		0x81000058,
		0x820000dc,
	};
	struct private_data *data = test->priv;
	struct fw_card *card = data->card;

	card->node_id = LOCAL_BUS | 0x02;

	card->local_node = build_tree(card, self_id_sequence, ARRAY_SIZE(self_id_sequence), 123);
	KUNIT_EXPECT_NOT_NULL(test, card->local_node);
	KUNIT_EXPECT_PTR_EQ(test, card->local_node, card->root_node);

	struct fw_node *node = card->root_node;
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x02);
	KUNIT_EXPECT_EQ(test, node->port_count, 3);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_NULL(test, node->ports[1]);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[2]);

	struct fw_node *parent = node;
	node = parent->ports[0];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x00);
	KUNIT_EXPECT_EQ(test, node->port_count, 2);
	KUNIT_EXPECT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[1], parent);

	node = parent->ports[2];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x01);
	KUNIT_EXPECT_EQ(test, node->port_count, 3);
	KUNIT_EXPECT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_NULL(test, node->ports[1]);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[2], parent);

	++card->color;
	for_each_fw_node(card, card->root_node, release_fw_node);
	KUNIT_EXPECT_EQ(test, data->release_count, 3);
}

static void node_tree_test_three_nodes_case1(struct kunit *test)
{
	//       root
	// ++============++
	// ||    phy 2   ||
	// ||  P0 P1 P2  ||
	// ++===|==|==x==++
	//         |
	//         |  +-----------+
	//         |  |           |
	// ++===|==|==|==++  ++===|==x==x==++
	// ||  P0 P1 P2  ||  ||  P0 P1 P2  ||
	// ||    phy 1   ||  ||    phy 0   ||
	// ++============++  ++============++
	//
	static const u32 self_id_sequence[] = {
		0x80000080,
		0x8100006c,
		0x82000070,
	};
	struct private_data *data = test->priv;
	struct fw_card *card = data->card;

	card->node_id = LOCAL_BUS | 0x02;

	card->local_node = build_tree(card, self_id_sequence, ARRAY_SIZE(self_id_sequence), 123);
	KUNIT_EXPECT_NOT_NULL(test, card->local_node);
	KUNIT_EXPECT_PTR_EQ(test, card->local_node, card->root_node);

	struct fw_node *node = card->root_node;
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x02);
	KUNIT_EXPECT_EQ(test, node->port_count, 2);
	KUNIT_EXPECT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[1]);

	struct fw_node *parent = node;
	node = parent->ports[1];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x01);
	KUNIT_EXPECT_EQ(test, node->port_count, 3);
	KUNIT_EXPECT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[1], parent);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[2]);

	parent = node;
	node = parent->ports[2];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x00);
	KUNIT_EXPECT_EQ(test, node->port_count, 1);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[0], parent);

	++card->color;
	for_each_fw_node(card, card->root_node, release_fw_node);
	KUNIT_EXPECT_EQ(test, data->release_count, 3);
}

static void node_tree_test_four_nodes_case0(struct kunit *test)
{
	//       root
	// ++============++
	// ||    phy 3   ||
	// ||  P0 P1 P2  ||
	// ++===|==|==|==++
	//         |
	//         |  +-----------+  +--------------+
	//         |  |           |  |              |
	// ++===|==|==|==++  ++===|==|==x==++  ++===|==x==x==++
	// ||  P0 P1 P2  ||  ||  P0 P1 P2  ||  ||  P0 P1 P2  ||
	// ||    phy 2   ||  ||    phy 1   ||  ||    phy 0   ||
	// ++============++  ++============++  ++============++
	//
	static const u32 self_id_sequence[] = {
		0x80000080,
		0x810000b0,
		0x8200006c,
		0x83000074,
	};
	struct private_data *data = test->priv;
	struct fw_card *card = data->card;

	card->node_id = LOCAL_BUS | 0x03;

	card->local_node = build_tree(card, self_id_sequence, ARRAY_SIZE(self_id_sequence), 123);
	KUNIT_EXPECT_NOT_NULL(test, card->local_node);
	KUNIT_EXPECT_PTR_EQ(test, card->local_node, card->root_node);

	struct fw_node *node = card->root_node;
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x03);
	KUNIT_EXPECT_EQ(test, node->port_count, 3);
	KUNIT_EXPECT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[1]);
	KUNIT_EXPECT_NULL(test, node->ports[2]);

	struct fw_node *parent = node;
	node = parent->ports[1];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x02);
	KUNIT_EXPECT_EQ(test, node->port_count, 3);
	KUNIT_EXPECT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[1], parent);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[2]);

	parent = node;
	node = parent->ports[2];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x01);
	KUNIT_EXPECT_EQ(test, node->port_count, 2);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[0], parent);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[1]);

	parent = node;
	node = parent->ports[1];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x00);
	KUNIT_EXPECT_EQ(test, node->port_count, 1);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[0], parent);

	++card->color;
	for_each_fw_node(card, card->root_node, release_fw_node);
	KUNIT_EXPECT_EQ(test, data->release_count, 4);
}

static void node_tree_test_four_nodes_case1(struct kunit *test)
{
	//       root
	// ++============++
	// ||    phy 3   ||
	// ||  P0 P1 P2  ||
	// ++===|==|==x==++
	//      |
	//      |  +--------------------------------+
	//      |  |  +-----------+                 |
	// ++===|==|==|==++  ++===|==x==x==++  ++===|==|==|==++
	// ||  P0 P1 P2  ||  ||  P0 P1 P2  ||  ||  P0 P1 P2  ||
	// ||    phy 2   ||  ||    phy 1   ||  ||    phy 0   ||
	// ++============++  ++============++  ++============++
	//
	static const u32 self_id_sequence[] = {
		0x80000094,
		0x81000080,
		0x820000bc,
		0x830000d0,
	};
	struct private_data *data = test->priv;
	struct fw_card *card = data->card;

	card->node_id = LOCAL_BUS | 0x03;

	card->local_node = build_tree(card, self_id_sequence, ARRAY_SIZE(self_id_sequence), 123);
	KUNIT_EXPECT_NOT_NULL(test, card->local_node);
	KUNIT_EXPECT_PTR_EQ(test, card->local_node, card->root_node);

	struct fw_node *node = card->root_node;
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x03);
	KUNIT_EXPECT_EQ(test, node->port_count, 2);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_NULL(test, node->ports[1]);

	struct fw_node *parent = node;
	node = parent->ports[0];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x02);
	KUNIT_EXPECT_EQ(test, node->port_count, 3);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[0], parent);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[1]);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[2]);

	parent = node;
	node = parent->ports[2];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x01);
	KUNIT_EXPECT_EQ(test, node->port_count, 1);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[0], parent);

	node = parent->ports[1];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x00);
	KUNIT_EXPECT_EQ(test, node->port_count, 3);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[0], parent);
	KUNIT_EXPECT_NULL(test, node->ports[1]);
	KUNIT_EXPECT_NULL(test, node->ports[2]);

	++card->color;
	for_each_fw_node(card, card->root_node, release_fw_node);
	KUNIT_EXPECT_EQ(test, data->release_count, 4);
}

static void node_tree_test_four_nodes_case2(struct kunit *test)
{
	//       root
	// ++============++
	// ||    phy 3   ||
	// ||  P0 P1 P2  ||
	// ++===|==|==|==++
	//      |     |
	//      |     +-----------------------------+
	//      |  +--------------+                 |
	// ++===|==|==x==++  ++===|==|==|==++  ++===|==x==x==++
	// ||  P0 P1 P2  ||  ||  P0 P1 P2  ||  ||  P0 P1 P2  ||
	// ||    phy 1   ||  ||    phy 0   ||  ||    phy 2   ||
	// ++============++  ++============++  ++============++
	//
	static const u32 self_id_sequence[] = {
		0x80000094,
		0x810000b0,
		0x82000080,
		0x830000dc,
	};
	struct private_data *data = test->priv;
	struct fw_card *card = data->card;

	card->node_id = LOCAL_BUS | 0x03;

	card->local_node = build_tree(card, self_id_sequence, ARRAY_SIZE(self_id_sequence), 123);
	KUNIT_EXPECT_NOT_NULL(test, card->local_node);
	KUNIT_EXPECT_PTR_EQ(test, card->local_node, card->root_node);

	struct fw_node *node = card->root_node;
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x03);
	KUNIT_EXPECT_EQ(test, node->port_count, 3);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_NULL(test, node->ports[1]);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[2]);

	struct fw_node *parent = node;
	node = parent->ports[2];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x02);
	KUNIT_EXPECT_EQ(test, node->port_count, 1);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[0], parent);

	node = parent->ports[0];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x01);
	KUNIT_EXPECT_EQ(test, node->port_count, 2);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[0], parent);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[1]);

	parent = node;
	node = parent->ports[1];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x00);
	KUNIT_EXPECT_EQ(test, node->port_count, 3);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[0], parent);
	KUNIT_EXPECT_NULL(test, node->ports[1]);
	KUNIT_EXPECT_NULL(test, node->ports[2]);

	++card->color;
	for_each_fw_node(card, card->root_node, release_fw_node);
	KUNIT_EXPECT_EQ(test, data->release_count, 4);
}

static void node_tree_test_four_nodes_case3(struct kunit *test)
{
	//       root
	// ++============++
	// ||    phy 3   ||
	// ||  P0 P1 P2  ||
	// ++===|==|==|==++
	//      |  |  +--------------------------------+
	//      |  +--------------------+              |
	//      |                       |              |
	// ++===|==|==x==++  ++===|==|==|==++  ++===|==|==x==++
	// ||  P0 P1 P2  ||  ||  P0 P1 P2  ||  ||  P0 P1 P2  ||
	// ||    phy 0   ||  ||    phy 1   ||  ||    phy 2   ||
	// ++============++  ++============++  ++============++
	//
	static const u32 self_id_sequence[] = {
		0x80000090,
		0x81000058,
		0x82000060,
		0x830000fc,
	};
	struct private_data *data = test->priv;
	struct fw_card *card = data->card;

	card->node_id = LOCAL_BUS | 0x03;

	card->local_node = build_tree(card, self_id_sequence, ARRAY_SIZE(self_id_sequence), 123);
	KUNIT_EXPECT_NOT_NULL(test, card->local_node);
	KUNIT_EXPECT_PTR_EQ(test, card->local_node, card->root_node);

	struct fw_node *node = card->root_node;
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x03);
	KUNIT_EXPECT_EQ(test, node->port_count, 3);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[1]);
	KUNIT_EXPECT_NOT_NULL(test, node->ports[2]);

	struct fw_node *parent = node;
	node = parent->ports[2];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x02);
	KUNIT_EXPECT_EQ(test, node->port_count, 2);
	KUNIT_EXPECT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[1], parent);

	node = parent->ports[1];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x01);
	KUNIT_EXPECT_EQ(test, node->port_count, 3);
	KUNIT_EXPECT_NULL(test, node->ports[0]);
	KUNIT_EXPECT_NULL(test, node->ports[1]);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[2], parent);

	node = parent->ports[0];
	KUNIT_EXPECT_EQ(test, node->node_id, LOCAL_BUS | 0x00);
	KUNIT_EXPECT_EQ(test, node->port_count, 2);
	KUNIT_EXPECT_PTR_EQ(test, node->ports[0], parent);
	KUNIT_EXPECT_NULL(test, node->ports[1]);

	++card->color;
	for_each_fw_node(card, card->root_node, release_fw_node);
	KUNIT_EXPECT_EQ(test, data->release_count, 4);
}

static void node_tree_test_invalid_extended_self_id_sequence(struct kunit *test)
{
	// Use the same node tree as node_tree_test_four_nodes_case1, except for the invalid
	// content of self ID packet for the phy 3.
	static const u32 self_id_sequence[] = {
		0x80000094,
		0x81000080,
		0x820000bc,
		0x830000d1, // Invalid.
	};
	struct private_data *data = test->priv;
	struct fw_card *card = data->card;

	card->node_id = LOCAL_BUS | 0x03;

	card->local_node = build_tree(card, self_id_sequence, ARRAY_SIZE(self_id_sequence), 123);
	KUNIT_EXPECT_NULL(test, card->local_node);
}

static void node_tree_test_invalid_phy_id(struct kunit *test)
{
	// Use the same node tree as node_tree_test_four_nodes_case1, except for the invalid
	// phy ID for phy 3.
	static const u32 self_id_sequence[] = {
		0x80000094,
		0x81000080,
		0x820000bc,
		0x8f0000d0, // Invalid.
	};
	struct private_data *data = test->priv;
	struct fw_card *card = data->card;

	card->node_id = LOCAL_BUS | 0x03;

	card->local_node = build_tree(card, self_id_sequence, ARRAY_SIZE(self_id_sequence), 123);
	KUNIT_EXPECT_NULL(test, card->local_node);
}

static void node_tree_test_invalid_child_port_count(struct kunit *test)
{
	// Use the same node tree as node_tree_test_four_nodes_case1, except for the invalid
	// count of child ports for phy 3.
	static const u32 self_id_sequence[] = {
		0x80000094,
		0x81000080,
		0x820000bc,
		0x830000fc, // Invalid.
	};
	struct private_data *data = test->priv;
	struct fw_card *card = data->card;

	card->node_id = LOCAL_BUS | 0x03;

	card->local_node = build_tree(card, self_id_sequence, ARRAY_SIZE(self_id_sequence), 123);
	KUNIT_EXPECT_NULL(test, card->local_node);
}

static void node_tree_test_invalid_parent_port_count(struct kunit *test)
{
	// Use the same node tree as node_tree_test_four_nodes_case1, except for the invalid
	// count of parent ports for phy 3.
	static const u32 self_id_sequence[] = {
		0x80000094,
		0x81000080,
		0x820000bc,
		0x830000e8, // Invalid.
	};
	struct private_data *data = test->priv;
	struct fw_card *card = data->card;

	card->node_id = LOCAL_BUS | 0x03;

	card->local_node = build_tree(card, self_id_sequence, ARRAY_SIZE(self_id_sequence), 123);
	KUNIT_EXPECT_NULL(test, card->local_node);
}

static struct kunit_case node_tree_test_cases[] = {
	KUNIT_CASE(node_tree_test_two_nodes),
	KUNIT_CASE(node_tree_test_two_nodes_1394a),
	KUNIT_CASE(node_tree_test_three_nodes_case0),
	KUNIT_CASE(node_tree_test_three_nodes_case1),
	KUNIT_CASE(node_tree_test_four_nodes_case0),
	KUNIT_CASE(node_tree_test_four_nodes_case1),
	KUNIT_CASE(node_tree_test_four_nodes_case2),
	KUNIT_CASE(node_tree_test_four_nodes_case3),
	KUNIT_CASE(node_tree_test_invalid_extended_self_id_sequence),
	KUNIT_CASE(node_tree_test_invalid_phy_id),
	KUNIT_CASE(node_tree_test_invalid_child_port_count),
	KUNIT_CASE(node_tree_test_invalid_parent_port_count),
	{}
};

static struct kunit_suite node_tree_test_suite = {
	.name = "firewire-node-tree",
	.init = node_tree_test_init,
	.exit = node_tree_test_exit,
	.test_cases = node_tree_test_cases,
};
kunit_test_suite(node_tree_test_suite);
