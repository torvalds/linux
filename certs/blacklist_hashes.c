 // SPDX-License-Identifier: GPL-2.0

// The `blacklist_hashes` array stores hashes of blacklisted certificates.
// These hashes are used to prevent the usage of certificates that are deemed untrusted or compromised.

#include "blacklist.h"

// The `blacklist_hashes` array is populated with hashes from the `blacklist_hash_list` file.
// Each entry in the array represents a hash of a blacklisted certificate.
const char __initconst *const blacklist_hashes[] = {
#include "blacklist_hash_list"
};
