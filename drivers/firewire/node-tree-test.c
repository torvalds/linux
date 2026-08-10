// SPDX-License-Identifier: GPL-2.0-only
//
// node-tree-test.c - An application of Kunit to test node tree.
//
// Copyright (c) 2026 Takashi Sakamoto
//
// This file can not be built independently since it is intentionally included in core-topology.c.

#include <kunit/test.h>

static struct kunit_case node_tree_test_cases[] = {
	{}
};

static struct kunit_suite node_tree_test_suite = {
	.name = "firewire-node-tree",
	.test_cases = node_tree_test_cases,
};
kunit_test_suite(node_tree_test_suite);
