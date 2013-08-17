/*******************************************************************************
  Header File to describe Normal/enhanced descriptor functions used for RING
  and CHAINED modes.

  Copyright(C) 2011  STMicroelectronics Ltd

  It defines all the functions used to handle the normal/enhanced
  descriptors in case of the DMA is configured to work in chained or
  in ring mode.

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

#if defined(CONFIG_STMMAC_RING)
static inline void ehn_desc_rx_set_on_ring_chain(struct dma_desc *p, int end)
{
	p->des01.erx.buffer2_size = BUF_SIZE_8KiB - 1;
	if (end)
		p->des01.erx.end_ring = 1;
}

static inline void ehn_desc_tx_set_on_ring_chain(struct dma_desc *p, int end)
{
	if (end)
		p->des01.etx.end_ring = 1;
}

static inline void enh_desc_end_tx_desc(struct dma_desc *p, int ter)
{
	p->des01.etx.end_ring = ter;
}

static inline void enh_set_tx_desc_len(struct dma_desc *p, int len)
{
	if (unlikely(len > BUF_SIZE_4KiB)) {
		p->des01.etx.buffer1_size = BUF_SIZE_4KiB;
		p->des01.etx.buffer2_size = len - BUF_SIZE_4KiB;
	} else
		p->des01.etx.buffer1_size = len;
}

static inline void ndesc_rx_set_on_ring_chain(struct dma_desc *p, int end)
{
	p->des01.rx.buffer2_size = BUF_SIZE_2KiB - 1;
	if (end)
		p->des01.rx.end_ring = 1;
}

static inline void ndesc_tx_set_on_ring_chain(struct dma_desc *p, int end)
{
	if (end)
		p->des01.tx.end_ring = 1;
}

static inline void ndesc_end_tx_desc(struct dma_desc *p, int ter)
{
	p->des01.tx.end_ring = ter;
}

static inline void norm_set_tx_desc_len(struct dma_desc *p, int len)
{
	if (unlikely(len > BUF_SIZE_2KiB)) {
		p->des01.etx.buffer1_size = BUF_SIZE_2KiB - 1;
		p->des01.etx.buffer2_size = len - p->des01.etx.buffer1_size;
	} else
		p->des01.tx.buffer1_size = len;
}

#else

static inline void ehn_desc_rx_set_on_ring_chain(struct dma_desc *p, int end)
{
	p->des01.erx.second_address_chained = 1;
}

static inline void ehn_desc_tx_set_on_ring_chain(struct dma_desc *p, int end)
{
	p->des01.etx.second_address_chained = 1;
}

static inline void enh_desc_end_tx_desc(struct dma_desc *p, int ter)
{
	p->des01.etx.second_address_chained = 1;
}

static inline void enh_set_tx_desc_len(struct dma_desc *p, int len)
{
	p->des01.etx.buffer1_size = len;
}

static inline void ndesc_rx_set_on_ring_chain(struct dma_desc *p, int end)
{
	p->des01.rx.second_address_chained = 1;
}

static inline void ndesc_tx_set_on_ring_chain(struct dma_desc *p, int ring_size)
{
	p->des01.tx.second_address_chained = 1;
}

static inline void ndesc_end_tx_desc(struct dma_desc *p, int ter)
{
	p->des01.tx.second_address_chained = 1;
}

static inline void norm_set_tx_desc_len(struct dma_desc *p, int len)
{
	p->des01.tx.buffer1_size = len;
}
#endif
