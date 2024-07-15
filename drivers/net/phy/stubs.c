// SPDX-License-Identifier: GPL-2.0+
/*
 * Stubs for PHY library functionality called by the core network stack.
 * These are necessary because CONFIG_PHYLIB can be a module, and built-in
 * code cannot directly call symbols exported by modules.
 */
#include <linux/phylib_stubs.h>

const struct phylib_stubs *phylib_stubs;
EXPORT_SYMBOL_GPL(phylib_stubs);
