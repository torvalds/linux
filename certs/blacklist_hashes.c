#include "blacklist.h"

const char __initdata *const blacklist_hashes[] = {
#include CONFIG_SYSTEM_BLACKLIST_HASH_LIST
	, NULL
};
