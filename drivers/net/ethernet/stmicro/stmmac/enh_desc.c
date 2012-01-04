/*******************************************************************************
  This contains the functions to handle the enhanced descriptors.

  Copyright (C) 2007-2009  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include "common.h"
#include "descs_com.h"

static int enh_desc_get_tx_status(void *data, struct stmmac_extra_stats *x,
				  struct dma_desc *p, void __iomem *ioaddr)
{
	int ret = 0;
	struct net_device_stats *stats = (struct net_device_stats *)data;

	if (unlikely(p->des01.etx.error_summary)) {
		CHIP_DBG(KERN_ERR "GMAC TX error... 0x%08x\n", p->des01.etx);
		if (unlikely(p->des01.etx.jabber_timeout)) {
			CHIP_DBG(KERN_ERR "\tjabber_timeout error\n");
			x->tx_jabber++;
		}

		if (unlikely(p->des01.etx.frame_flushed)) {
			CHIP_DBG(KERN_ERR "\tframe_flushed error\n");
			x->tx_frame_flushed++;
			dwmac_dma_flush_tx_fifo(ioaddr);
		}

		if (unlikely(p->des01.etx.loss_carrier)) {
			CHIP_DBG(KERN_ERR "\tloss_carrier error\n");
			x->tx_losscarrier++;
			stats->tx_carrier_errors++;
		}
		if (unlikely(p->des01.etx.no_carrier)) {
			CHIP_DBG(KERN_ERR "\tno_carrier error\n");
			x->tx_carrier++;
			stats->tx_carrier_errors++;
		}
		if (unlikely(p->des01.etx.late_collision)) {
			CHIP_DBG(KERN_ERR "\tlate_collision error\n");
			stats->collisions += p->des01.etx.collision_count;
		}
		if (unlikely(p->des01.etx.excessive_collisions)) {
			CHIP_DBG(KERN_ERR "\texcessive_collisions\n");
			stats->collisions += p->des01.etx.collision_count;
		}
		if (unlikely(p->des01.etx.excessive_deferral)) {
			CHIP_DBG(KERN_INFO "\texcessive tx_deferral\n");
			x->tx_deferred++;
		}

		if (unlikely(p->des01.etx.underflow_error)) {
			CHIP_DBG(KERN_ERR "\tunderflow error\n");
			dwmac_dma_flush_tx_fifo(ioaddr);
			x->tx_underflow++;
		}

		if (unlikely(p->des01.etx.ip_header_error)) {
			CHIP_DBG(KERN_ERR "\tTX IP header csum error\n");
			x->tx_ip_header_error++;
		}

		if (unlikely(p->des01.etx.payload_error)) {
			CHIP_DBG(KERN_ERR "\tAddr/Payload csum error\n");
			x->tx_payload_error++;
			dwmac_dma_flush_tx_fifo(ioaddr);
		}

		ret = -1;
	}

	if (unlikely(p->des01.etx.deferred)) {
		CHIP_DBG(KERN_INFO "GMAC TX status: tx deferred\n");
		x->tx_deferred++;
	}
#ifdef STMMAC_VLAN_TAG_USED
	if (p->des01.etx.vlan_frame) {
		CHIP_DBG(KERN_INFO "GMAC TX status: VLAN frame\n");
		x->tx_vlan++;
	}
#endif

	return ret;
}

static int enh_desc_get_tx_len(struct dma_desc *p)
{
	return p->des01.etx.buffer1_size;
}

static int enh_desc_coe_rdes0(int ipc_err, int type, int payload_err)
{
	int ret = good_frame;
	u32 status = (type << 2 | ipc_err << 1 | payload_err) & 0x7;

	/* bits 5 7 0 | Frame status
	 * ----------------------------------------------------------
	 *      0 0 0 | IEEE 802.3 Type frame (length < 1536 octects)
	 *      1 0 0 | IPv4/6 No CSUM errorS.
	 *      1 0 1 | IPv4/6 CSUM PAYLOAD error
	 *      1 1 0 | IPv4/6 CSUM IP HR error
	 *      1 1 1 | IPv4/6 IP PAYLOAD AND HEADER errorS
	 *      0 0 1 | IPv4/6 unsupported IP PAYLOAD
	 *      0 1 1 | COE bypassed.. no IPv4/6 frame
	 *      0 1 0 | Reserved.
	 */
	if (status == 0x0) {
		CHIP_DBG(KERN_INFO "RX Des0 status: IEEE 802.3 Type frame.\n");
		ret = llc_snap;
	} else if (status == 0x4) {
		CHIP_DBG(KERN_INFO "RX Des0 status: IPv4/6 No CSUM errorS.\n");
		ret = good_frame;
	} else if (status == 0x5) {
		CHIP_DBG(KERN_ERR "RX Des0 status: IPv4/6 Payload Error.\n");
		ret = csum_none;
	} else if (status == 0x6) {
		CHIP_DBG(KERN_ERR "RX Des0 status: IPv4/6 Header Error.\n");
		ret = csum_none;
	} else if (status == 0x7) {
		CHIP_DBG(KERN_ERR
		    "RX Des0 status: IPv4/6 Header and Payload Error.\n");
		ret = csum_none;
	} else if (status == 0x1) {
		CHIP_DBG(KERN_ERR
		    "RX Des0 status: IPv4/6 unsupported IP PAYLOAD.\n");
		ret = discard_frame;
	} else if (status == 0x3) {
		CHIP_DBG(KERN_ERR "RX Des0 status: No IPv4, IPv6 frame.\n");
		ret = discard_frame;
	}
	return ret;
}

static int enh_desc_get_rx_status(void *data, struct stmmac_extra_stats *x,
				  struct dma_desc *p)
{
	int ret = good_frame;
	struct net_device_stats *stats = (struct net_device_stats *)data;

	if (unlikely(p->des01.erx.error_summary)) {
		CHIP_DBG(KERN_ERR "GMAC RX Error Summary 0x%08x\n",
				  p->des01.erx);
		if (unlikely(p->des01.erx.descriptor_error)) {
			CHIP_DBG(KERN_ERR "\tdescriptor error\n");
			x->rx_desc++;
			stats->rx_length_errors++;
		}
		if (unlikely(p->des01.erx.overflow_error)) {
			CHIP_DBG(KERN_ERR "\toverflow error\n");
			x->rx_gmac_overflow++;
		}

		if (unlikely(p->des01.erx.ipc_csum_error))
			CHIP_DBG(KERN_ERR "\tIPC Csum Error/Giant frame\n");

		if (unlikely(p->des01.erx.late_collision)) {
			CHIP_DBG(KERN_ERR "\tlate_collision error\n");
			stats->collisions++;
			stats->collisions++;
		}
		if (unlikely(p->des01.erx.receive_watchdog)) {
			CHIP_DBG(KERN_ERR "\treceive_watchdog error\n");
			x->rx_watchdog++;
		}
		if (unlikely(p->des01.erx.error_gmii)) {
			CHIP_DBG(KERN_ERR "\tReceive Error\n");
			x->rx_mii++;
		}
		if (unlikely(p->des01.erx.crc_error)) {
			CHIP_DBG(KERN_ERR "\tCRC error\n");
			x->rx_crc++;
			stats->rx_crc_errors++;
		}
		ret = discard_frame;
	}

	/* After a payload csum error, the ES bit is set.
	 * It doesn't match with the information reported into the databook.
	 * At any rate, we need to understand if the CSUM hw computation is ok
	 * and report this info to the upper layers. */
	ret = enh_desc_coe_rdes0(p->des01.erx.ipc_csum_error,
		p->des01.erx.frame_type, p->des01.erx.payload_csum_error);

	if (unlikely(p->des01.erx.dribbling)) {
		CHIP_DBG(KERN_ERR "GMAC RX: dribbling error\n");
		ret = discard_frame;
	}
	if (unlikely(p->des01.erx.sa_filter_fail)) {
		CHIP_DBG(KERN_ERR "GMAC RX : Source Address filter fail\n");
		x->sa_rx_filter_fail++;
		ret = discard_frame;
	}
	if (unlikely(p->des01.erx.da_filter_fail)) {
		CHIP_DBG(KERN_ERR "GMAC RX : Dest Address filter fail\n");
		x->da_rx_filter_fail++;
		ret = discard_frame;
	}
	if (unlikely(p->des01.erx.length_error)) {
		CHIP_DBG(KERN_ERR "GMAC RX: length_error error\n");
		x->rx_length++;
		ret = discard_frame;
	}
#ifdef STMMAC_VLAN_TAG_USED
	if (p->des01.erx.vlan_tag) {
		CHIP_DBG(KERN_INFO "GMAC RX: VLAN frame tagged\n");
		x->rx_vlan++;
	}
#endif
	return ret;
}

static void enh_desc_init_rx_desc(struct dma_desc *p, unsigned int ring_size,
				  int disable_rx_ic)
{
	int i;
	for (i = 0; i < ring_size; i++) {
		p->des01.erx.own = 1;
		p->des01.erx.buffer1_size = BUF_SIZE_8KiB - 1;

		ehn_desc_rx_set_on_ring_chain(p, (i == ring_size - 1));

		if (disable_rx_ic)
			p->des01.erx.disable_ic = 1;
		p++;
	}
}

static void enh_desc_init_tx_desc(struct dma_desc *p, unsigned int ring_size)
{
	int i;

	for (i = 0; i < ring_size; i++) {
		p->des01.etx.own = 0;
		ehn_desc_tx_set_on_ring_chain(p, (i == ring_size - 1));
		p++;
	}
}

static int enh_desc_get_tx_owner(struct dma_desc *p)
{
	return p->des01.etx.own;
}

static int enh_desc_get_rx_owner(struct dma_desc *p)
{
	return p->des01.erx.own;
}

static void enh_desc_set_tx_owner(struct dma_desc *p)
{
	p->des01.etx.own = 1;
}

static void enh_desc_set_rx_owner(struct dma_desc *p)
{
	p->des01.erx.own = 1;
}

static int enh_desc_get_tx_ls(struct dma_desc *p)
{
	return p->des01.etx.last_segment;
}

static void enh_desc_release_tx_desc(struct dma_desc *p)
{
	int ter = p->des01.etx.end_ring;

	memset(p, 0, offsetof(struct dma_desc, des2));
	enh_desc_end_tx_desc(p, ter);
}

static void enh_desc_prepare_tx_desc(struct dma_desc *p, int is_fs, int len,
				     int csum_flag)
{
	p->des01.etx.first_segment = is_fs;

	enh_set_tx_desc_len(p, len);

	if (likely(csum_flag))
		p->des01.etx.checksum_insertion = cic_full;
}

static void enh_desc_clear_tx_ic(struct dma_desc *p)
{
	p->des01.etx.interrupt = 0;
}

static void enh_desc_close_tx_desc(struct dma_desc *p)
{
	p->des01.etx.last_segment = 1;
	p->des01.etx.interrupt = 1;
}

static int enh_desc_get_rx_frame_len(struct dma_desc *p)
{
	return p->des01.erx.frame_length;
}

const struct stmmac_desc_ops enh_desc_ops = {
	.tx_status = enh_desc_get_tx_status,
	.rx_status = enh_desc_get_rx_status,
	.get_tx_len = enh_desc_get_tx_len,
	.init_rx_desc = enh_desc_init_rx_desc,
	.init_tx_desc = enh_desc_init_tx_desc,
	.get_tx_owner = enh_desc_get_tx_owner,
	.get_rx_owner = enh_desc_get_rx_owner,
	.release_tx_desc = enh_desc_release_tx_desc,
	.prepare_tx_desc = enh_desc_prepare_tx_desc,
	.clear_tx_ic = enh_desc_clear_tx_ic,
	.close_tx_desc = enh_desc_close_tx_desc,
	.get_tx_ls = enh_desc_get_tx_ls,
	.set_tx_owner = enh_desc_set_tx_owner,
	.set_rx_owner = enh_desc_set_rx_owner,
	.get_rx_frame_len = enh_desc_get_rx_frame_len,
};
