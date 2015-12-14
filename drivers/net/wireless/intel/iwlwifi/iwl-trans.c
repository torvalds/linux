/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Mobile Communications GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Mobile Communications GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include <linux/kernel.h>
#include "iwl-trans.h"

struct iwl_trans *iwl_trans_alloc(unsigned int priv_size,
				  struct device *dev,
				  const struct iwl_cfg *cfg,
				  const struct iwl_trans_ops *ops,
				  size_t dev_cmd_headroom)
{
	struct iwl_trans *trans;
#ifdef CONFIG_LOCKDEP
	static struct lock_class_key __key;
#endif

	trans = kzalloc(sizeof(*trans) + priv_size, GFP_KERNEL);
	if (!trans)
		return NULL;

#ifdef CONFIG_LOCKDEP
	lockdep_init_map(&trans->sync_cmd_lockdep_map, "sync_cmd_lockdep_map",
			 &__key, 0);
#endif

	trans->dev = dev;
	trans->cfg = cfg;
	trans->ops = ops;
	trans->dev_cmd_headroom = dev_cmd_headroom;
	trans->num_rx_queues = 1;

	snprintf(trans->dev_cmd_pool_name, sizeof(trans->dev_cmd_pool_name),
		 "iwl_cmd_pool:%s", dev_name(trans->dev));
	trans->dev_cmd_pool =
		kmem_cache_create(trans->dev_cmd_pool_name,
				  sizeof(struct iwl_device_cmd)
				  + trans->dev_cmd_headroom,
				  sizeof(void *),
				  SLAB_HWCACHE_ALIGN,
				  NULL);
	if (!trans->dev_cmd_pool)
		goto free;

	return trans;
 free:
	kfree(trans);
	return NULL;
}

void iwl_trans_free(struct iwl_trans *trans)
{
	kmem_cache_destroy(trans->dev_cmd_pool);
	kfree(trans);
}
