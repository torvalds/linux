/*
 * Greybus module manifest parsing
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __MANIFEST_H
#define __MANIFEST_H

struct gb_interface_block;
bool gb_manifest_parse(struct gb_interface_block *gb_ib, void *data, size_t size);

#endif /* __MANIFEST_H */
