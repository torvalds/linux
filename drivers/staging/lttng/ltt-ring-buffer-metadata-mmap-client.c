/*
 * ltt-ring-buffer-metadata-client.c
 *
 * Copyright (C) 2010 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * LTTng lib ring buffer metadta client.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/module.h>
#include "ltt-tracer.h"

#define RING_BUFFER_MODE_TEMPLATE		RING_BUFFER_DISCARD
#define RING_BUFFER_MODE_TEMPLATE_STRING	"metadata-mmap"
#define RING_BUFFER_OUTPUT_TEMPLATE		RING_BUFFER_MMAP
#include "ltt-ring-buffer-metadata-client.h"

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("LTTng Ring Buffer Metadata Client");
