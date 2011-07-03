/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2011 Intel Corporation. All rights reserved.
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
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2011 Intel Corporation. All rights reserved.
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
static inline int trans_rx_init(struct iwl_priv *priv)
{
	return priv->trans.ops->rx_init(priv);
}

static inline int trans_rx_stop(struct iwl_priv *priv)
{
	return priv->trans.ops->rx_stop(priv);
}

static inline void trans_rx_free(struct iwl_priv *priv)
{
	priv->trans.ops->rx_free(priv);
}

static inline int trans_tx_init(struct iwl_priv *priv)
{
	return priv->trans.ops->tx_init(priv);
}

static inline int trans_tx_stop(struct iwl_priv *priv)
{
	return priv->trans.ops->tx_stop(priv);
}

static inline void trans_tx_free(struct iwl_priv *priv)
{
	priv->trans.ops->tx_free(priv);
}

static inline int trans_send_cmd(struct iwl_priv *priv,
				struct iwl_host_cmd *cmd)
{
	return priv->trans.ops->send_cmd(priv, cmd);
}

static inline int trans_send_cmd_pdu(struct iwl_priv *priv, u8 id, u32 flags,
					u16 len, const void *data)
{
	return priv->trans.ops->send_cmd_pdu(priv, id, flags, len, data);
}

static inline struct iwl_tx_cmd *trans_get_tx_cmd(struct iwl_priv *priv,
					int txq_id)
{
	return priv->trans.ops->get_tx_cmd(priv, txq_id);
}

static inline int trans_tx(struct iwl_priv *priv, struct sk_buff *skb,
		struct iwl_tx_cmd *tx_cmd, int txq_id, __le16 fc, bool ampdu,
		struct iwl_rxon_context *ctx)
{
	return priv->trans.ops->tx(priv, skb, tx_cmd, txq_id, fc, ampdu, ctx);
}

void iwl_trans_register(struct iwl_trans *trans);
