// SPDX-License-Identifier: GPL-2.0
#include "blacklist.h"

const char __initconst *const blacklist_hashes[] = {
#include "blacklist_hash_list"
	, NULL
};
