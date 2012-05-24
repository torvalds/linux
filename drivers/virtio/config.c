/* Configuration space parsing helpers for virtio.
 *
 * The configuration is [type][len][... len bytes ...] fields.
 *
 * Copyright 2007 Rusty Russell, IBM Corporation.
 * GPL v2 or later.
 */
#include <linux/err.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/bug.h>

