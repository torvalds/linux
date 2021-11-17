/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PSERIES_OF_HELPERS_H
#define _PSERIES_OF_HELPERS_H

#include <linux/of.h>

struct device_node *pseries_of_derive_parent(const char *path);

#endif /* _PSERIES_OF_HELPERS_H */
