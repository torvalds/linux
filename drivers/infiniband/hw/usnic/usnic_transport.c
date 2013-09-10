/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/bitmap.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "usnic_transport.h"
#include "usnic_log.h"

/* ROCE */
static unsigned long *roce_bitmap;
static u16 roce_next_port = 1;
#define ROCE_BITMAP_SZ ((1 << (8 /*CHAR_BIT*/ * sizeof(u16)))/8 /*CHAR BIT*/)
static DEFINE_SPINLOCK(roce_bitmap_lock);

static const char *transport_to_str(enum usnic_transport_type type)
{
	switch (type) {
	case USNIC_TRANSPORT_UNKNOWN:
		return "Unknown";
	case USNIC_TRANSPORT_ROCE_CUSTOM:
		return "roce custom";
	case USNIC_TRANSPORT_MAX:
		return "Max?";
	default:
		return "Not known";
	}
}

/*
 * reserve a port number.  if "0" specified, we will try to pick one
 * starting at roce_next_port.  roce_next_port will take on the values
 * 1..4096
 */
u16 usnic_transport_rsrv_port(enum usnic_transport_type type, u16 port_num)
{
	if (type == USNIC_TRANSPORT_ROCE_CUSTOM) {
		spin_lock(&roce_bitmap_lock);
		if (!port_num) {
			port_num = bitmap_find_next_zero_area(roce_bitmap,
						ROCE_BITMAP_SZ,
						roce_next_port /* start */,
						1 /* nr */,
						0 /* align */);
			roce_next_port = (port_num & 4095) + 1;
		} else if (test_bit(port_num, roce_bitmap)) {
			usnic_err("Failed to allocate port for %s\n",
					transport_to_str(type));
			spin_unlock(&roce_bitmap_lock);
			goto out_fail;
		}
		bitmap_set(roce_bitmap, port_num, 1);
		spin_unlock(&roce_bitmap_lock);
	} else {
		usnic_err("Failed to allocate port - transport %s unsupported\n",
				transport_to_str(type));
		goto out_fail;
	}

	usnic_dbg("Allocating port %hu for %s\n", port_num,
			transport_to_str(type));
	return port_num;

out_fail:
	return 0;
}

void usnic_transport_unrsrv_port(enum usnic_transport_type type, u16 port_num)
{
	if (type == USNIC_TRANSPORT_ROCE_CUSTOM) {
		spin_lock(&roce_bitmap_lock);
		if (!port_num) {
			usnic_err("Unreserved unvalid port num 0 for %s\n",
					transport_to_str(type));
			goto out_roce_custom;
		}

		if (!test_bit(port_num, roce_bitmap)) {
			usnic_err("Unreserving invalid %hu for %s\n",
					port_num,
					transport_to_str(type));
			goto out_roce_custom;
		}
		bitmap_clear(roce_bitmap, port_num, 1);
		usnic_dbg("Freeing port %hu for %s\n", port_num,
				transport_to_str(type));
out_roce_custom:
		spin_unlock(&roce_bitmap_lock);
	} else {
		usnic_err("Freeing invalid port %hu for %d\n", port_num, type);
	}
}

int usnic_transport_init(void)
{
	roce_bitmap = kzalloc(ROCE_BITMAP_SZ, GFP_KERNEL);
	if (!roce_bitmap) {
		usnic_err("Failed to allocate bit map");
		return -ENOMEM;
	}

	/* Do not ever allocate bit 0, hence set it here */
	bitmap_set(roce_bitmap, 0, 1);
	return 0;
}

void usnic_transport_fini(void)
{
	kfree(roce_bitmap);
}
