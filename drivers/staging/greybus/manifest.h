/*
 * Greybus manifest parsing
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __MANIFEST_H
#define __MANIFEST_H

struct gb_interface;
bool gb_manifest_parse(struct gb_interface *intf, void *data, size_t size);

#endif /* __MANIFEST_H */
