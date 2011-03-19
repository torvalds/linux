/*
 * Copyright (C) ST-Ericsson SA 2007-2010
 * Author: Per Forlin <per.forlin@stericsson.com> for ST-Ericsson
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <plat/ste_dma40.h>

#include "ste_dma40_ll.h"

/* Sets up proper LCSP1 and LCSP3 register for a logical channel */
void d40_log_cfg(struct stedma40_chan_cfg *cfg,
		 u32 *lcsp1, u32 *lcsp3)
{
	u32 l3 = 0; /* dst */
	u32 l1 = 0; /* src */

	/* src is mem? -> increase address pos */
	if (cfg->dir ==  STEDMA40_MEM_TO_PERIPH ||
	    cfg->dir ==  STEDMA40_MEM_TO_MEM)
		l1 |= 1 << D40_MEM_LCSP1_SCFG_INCR_POS;

	/* dst is mem? -> increase address pos */
	if (cfg->dir ==  STEDMA40_PERIPH_TO_MEM ||
	    cfg->dir ==  STEDMA40_MEM_TO_MEM)
		l3 |= 1 << D40_MEM_LCSP3_DCFG_INCR_POS;

	/* src is hw? -> master port 1 */
	if (cfg->dir ==  STEDMA40_PERIPH_TO_MEM ||
	    cfg->dir ==  STEDMA40_PERIPH_TO_PERIPH)
		l1 |= 1 << D40_MEM_LCSP1_SCFG_MST_POS;

	/* dst is hw? -> master port 1 */
	if (cfg->dir ==  STEDMA40_MEM_TO_PERIPH ||
	    cfg->dir ==  STEDMA40_PERIPH_TO_PERIPH)
		l3 |= 1 << D40_MEM_LCSP3_DCFG_MST_POS;

	l3 |= 1 << D40_MEM_LCSP3_DCFG_EIM_POS;
	l3 |= cfg->dst_info.psize << D40_MEM_LCSP3_DCFG_PSIZE_POS;
	l3 |= cfg->dst_info.data_width << D40_MEM_LCSP3_DCFG_ESIZE_POS;

	l1 |= 1 << D40_MEM_LCSP1_SCFG_EIM_POS;
	l1 |= cfg->src_info.psize << D40_MEM_LCSP1_SCFG_PSIZE_POS;
	l1 |= cfg->src_info.data_width << D40_MEM_LCSP1_SCFG_ESIZE_POS;

	*lcsp1 = l1;
	*lcsp3 = l3;

}

/* Sets up SRC and DST CFG register for both logical and physical channels */
void d40_phy_cfg(struct stedma40_chan_cfg *cfg,
		 u32 *src_cfg, u32 *dst_cfg, bool is_log)
{
	u32 src = 0;
	u32 dst = 0;

	if (!is_log) {
		/* Physical channel */
		if ((cfg->dir ==  STEDMA40_PERIPH_TO_MEM) ||
		    (cfg->dir == STEDMA40_PERIPH_TO_PERIPH)) {
			/* Set master port to 1 */
			src |= 1 << D40_SREG_CFG_MST_POS;
			src |= D40_TYPE_TO_EVENT(cfg->src_dev_type);

			if (cfg->src_info.flow_ctrl == STEDMA40_NO_FLOW_CTRL)
				src |= 1 << D40_SREG_CFG_PHY_TM_POS;
			else
				src |= 3 << D40_SREG_CFG_PHY_TM_POS;
		}
		if ((cfg->dir ==  STEDMA40_MEM_TO_PERIPH) ||
		    (cfg->dir == STEDMA40_PERIPH_TO_PERIPH)) {
			/* Set master port to 1 */
			dst |= 1 << D40_SREG_CFG_MST_POS;
			dst |= D40_TYPE_TO_EVENT(cfg->dst_dev_type);

			if (cfg->dst_info.flow_ctrl == STEDMA40_NO_FLOW_CTRL)
				dst |= 1 << D40_SREG_CFG_PHY_TM_POS;
			else
				dst |= 3 << D40_SREG_CFG_PHY_TM_POS;
		}
		/* Interrupt on end of transfer for destination */
		dst |= 1 << D40_SREG_CFG_TIM_POS;

		/* Generate interrupt on error */
		src |= 1 << D40_SREG_CFG_EIM_POS;
		dst |= 1 << D40_SREG_CFG_EIM_POS;

		/* PSIZE */
		if (cfg->src_info.psize != STEDMA40_PSIZE_PHY_1) {
			src |= 1 << D40_SREG_CFG_PHY_PEN_POS;
			src |= cfg->src_info.psize << D40_SREG_CFG_PSIZE_POS;
		}
		if (cfg->dst_info.psize != STEDMA40_PSIZE_PHY_1) {
			dst |= 1 << D40_SREG_CFG_PHY_PEN_POS;
			dst |= cfg->dst_info.psize << D40_SREG_CFG_PSIZE_POS;
		}

		/* Element size */
		src |= cfg->src_info.data_width << D40_SREG_CFG_ESIZE_POS;
		dst |= cfg->dst_info.data_width << D40_SREG_CFG_ESIZE_POS;

	} else {
		/* Logical channel */
		dst |= 1 << D40_SREG_CFG_LOG_GIM_POS;
		src |= 1 << D40_SREG_CFG_LOG_GIM_POS;
	}

	if (cfg->high_priority) {
		src |= 1 << D40_SREG_CFG_PRI_POS;
		dst |= 1 << D40_SREG_CFG_PRI_POS;
	}

	if (cfg->src_info.big_endian)
		src |= 1 << D40_SREG_CFG_LBE_POS;
	if (cfg->dst_info.big_endian)
		dst |= 1 << D40_SREG_CFG_LBE_POS;

	*src_cfg = src;
	*dst_cfg = dst;
}

static int d40_phy_fill_lli(struct d40_phy_lli *lli,
			    dma_addr_t data,
			    u32 data_size,
			    int psize,
			    dma_addr_t next_lli,
			    u32 reg_cfg,
			    bool term_int,
			    u32 data_width,
			    bool is_device)
{
	int num_elems;

	if (psize == STEDMA40_PSIZE_PHY_1)
		num_elems = 1;
	else
		num_elems = 2 << psize;

	/* Must be aligned */
	if (!IS_ALIGNED(data, 0x1 << data_width))
		return -EINVAL;

	/* Transfer size can't be smaller than (num_elms * elem_size) */
	if (data_size < num_elems * (0x1 << data_width))
		return -EINVAL;

	/* The number of elements. IE now many chunks */
	lli->reg_elt = (data_size >> data_width) << D40_SREG_ELEM_PHY_ECNT_POS;

	/*
	 * Distance to next element sized entry.
	 * Usually the size of the element unless you want gaps.
	 */
	if (!is_device)
		lli->reg_elt |= (0x1 << data_width) <<
			D40_SREG_ELEM_PHY_EIDX_POS;

	/* Where the data is */
	lli->reg_ptr = data;
	lli->reg_cfg = reg_cfg;

	/* If this scatter list entry is the last one, no next link */
	if (next_lli == 0)
		lli->reg_lnk = 0x1 << D40_SREG_LNK_PHY_TCP_POS;
	else
		lli->reg_lnk = next_lli;

	/* Set/clear interrupt generation on this link item.*/
	if (term_int)
		lli->reg_cfg |= 0x1 << D40_SREG_CFG_TIM_POS;
	else
		lli->reg_cfg &= ~(0x1 << D40_SREG_CFG_TIM_POS);

	/* Post link */
	lli->reg_lnk |= 0 << D40_SREG_LNK_PHY_PRE_POS;

	return 0;
}

static int d40_seg_size(int size, int data_width1, int data_width2)
{
	u32 max_w = max(data_width1, data_width2);
	u32 min_w = min(data_width1, data_width2);
	u32 seg_max = ALIGN(STEDMA40_MAX_SEG_SIZE << min_w, 1 << max_w);

	if (seg_max > STEDMA40_MAX_SEG_SIZE)
		seg_max -= (1 << max_w);

	if (size <= seg_max)
		return size;

	if (size <= 2 * seg_max)
		return ALIGN(size / 2, 1 << max_w);

	return seg_max;
}

struct d40_phy_lli *d40_phy_buf_to_lli(struct d40_phy_lli *lli,
				       dma_addr_t addr,
				       u32 size,
				       int psize,
				       dma_addr_t lli_phys,
				       u32 reg_cfg,
				       bool term_int,
				       u32 data_width1,
				       u32 data_width2,
				       bool is_device)
{
	int err;
	dma_addr_t next = lli_phys;
	int size_rest = size;
	int size_seg = 0;

	do {
		size_seg = d40_seg_size(size_rest, data_width1, data_width2);
		size_rest -= size_seg;

		if (term_int && size_rest == 0)
			next = 0;
		else
			next = ALIGN(next + sizeof(struct d40_phy_lli),
				     D40_LLI_ALIGN);

		err = d40_phy_fill_lli(lli,
				       addr,
				       size_seg,
				       psize,
				       next,
				       reg_cfg,
				       !next,
				       data_width1,
				       is_device);

		if (err)
			goto err;

		lli++;
		if (!is_device)
			addr += size_seg;
	} while (size_rest);

	return lli;

 err:
	return NULL;
}

int d40_phy_sg_to_lli(struct scatterlist *sg,
		      int sg_len,
		      dma_addr_t target,
		      struct d40_phy_lli *lli_sg,
		      dma_addr_t lli_phys,
		      u32 reg_cfg,
		      u32 data_width1,
		      u32 data_width2,
		      int psize)
{
	int total_size = 0;
	int i;
	struct scatterlist *current_sg = sg;
	dma_addr_t dst;
	struct d40_phy_lli *lli = lli_sg;
	dma_addr_t l_phys = lli_phys;

	for_each_sg(sg, current_sg, sg_len, i) {

		total_size += sg_dma_len(current_sg);

		if (target)
			dst = target;
		else
			dst = sg_phys(current_sg);

		l_phys = ALIGN(lli_phys + (lli - lli_sg) *
			       sizeof(struct d40_phy_lli), D40_LLI_ALIGN);

		lli = d40_phy_buf_to_lli(lli,
					 dst,
					 sg_dma_len(current_sg),
					 psize,
					 l_phys,
					 reg_cfg,
					 sg_len - 1 == i,
					 data_width1,
					 data_width2,
					 target == dst);
		if (lli == NULL)
			return -EINVAL;
	}

	return total_size;
}


void d40_phy_lli_write(void __iomem *virtbase,
		       u32 phy_chan_num,
		       struct d40_phy_lli *lli_dst,
		       struct d40_phy_lli *lli_src)
{

	writel(lli_src->reg_cfg, virtbase + D40_DREG_PCBASE +
	       phy_chan_num * D40_DREG_PCDELTA + D40_CHAN_REG_SSCFG);
	writel(lli_src->reg_elt, virtbase + D40_DREG_PCBASE +
	       phy_chan_num * D40_DREG_PCDELTA + D40_CHAN_REG_SSELT);
	writel(lli_src->reg_ptr, virtbase + D40_DREG_PCBASE +
	       phy_chan_num * D40_DREG_PCDELTA + D40_CHAN_REG_SSPTR);
	writel(lli_src->reg_lnk, virtbase + D40_DREG_PCBASE +
	       phy_chan_num * D40_DREG_PCDELTA + D40_CHAN_REG_SSLNK);

	writel(lli_dst->reg_cfg, virtbase + D40_DREG_PCBASE +
	       phy_chan_num * D40_DREG_PCDELTA + D40_CHAN_REG_SDCFG);
	writel(lli_dst->reg_elt, virtbase + D40_DREG_PCBASE +
	       phy_chan_num * D40_DREG_PCDELTA + D40_CHAN_REG_SDELT);
	writel(lli_dst->reg_ptr, virtbase + D40_DREG_PCBASE +
	       phy_chan_num * D40_DREG_PCDELTA + D40_CHAN_REG_SDPTR);
	writel(lli_dst->reg_lnk, virtbase + D40_DREG_PCBASE +
	       phy_chan_num * D40_DREG_PCDELTA + D40_CHAN_REG_SDLNK);

}

/* DMA logical lli operations */

static void d40_log_lli_link(struct d40_log_lli *lli_dst,
			     struct d40_log_lli *lli_src,
			     int next)
{
	u32 slos = 0;
	u32 dlos = 0;

	if (next != -EINVAL) {
		slos = next * 2;
		dlos = next * 2 + 1;
	} else {
		lli_dst->lcsp13 |= D40_MEM_LCSP1_SCFG_TIM_MASK;
		lli_dst->lcsp13 |= D40_MEM_LCSP3_DTCP_MASK;
	}

	lli_src->lcsp13 = (lli_src->lcsp13 & ~D40_MEM_LCSP1_SLOS_MASK) |
		(slos << D40_MEM_LCSP1_SLOS_POS);

	lli_dst->lcsp13 = (lli_dst->lcsp13 & ~D40_MEM_LCSP1_SLOS_MASK) |
		(dlos << D40_MEM_LCSP1_SLOS_POS);
}

void d40_log_lli_lcpa_write(struct d40_log_lli_full *lcpa,
			   struct d40_log_lli *lli_dst,
			   struct d40_log_lli *lli_src,
			   int next)
{
	d40_log_lli_link(lli_dst, lli_src, next);

	writel(lli_src->lcsp02, &lcpa[0].lcsp0);
	writel(lli_src->lcsp13, &lcpa[0].lcsp1);
	writel(lli_dst->lcsp02, &lcpa[0].lcsp2);
	writel(lli_dst->lcsp13, &lcpa[0].lcsp3);
}

void d40_log_lli_lcla_write(struct d40_log_lli *lcla,
			   struct d40_log_lli *lli_dst,
			   struct d40_log_lli *lli_src,
			   int next)
{
	d40_log_lli_link(lli_dst, lli_src, next);

	writel(lli_src->lcsp02, &lcla[0].lcsp02);
	writel(lli_src->lcsp13, &lcla[0].lcsp13);
	writel(lli_dst->lcsp02, &lcla[1].lcsp02);
	writel(lli_dst->lcsp13, &lcla[1].lcsp13);
}

static void d40_log_fill_lli(struct d40_log_lli *lli,
			     dma_addr_t data, u32 data_size,
			     u32 reg_cfg,
			     u32 data_width,
			     bool addr_inc)
{
	lli->lcsp13 = reg_cfg;

	/* The number of elements to transfer */
	lli->lcsp02 = ((data_size >> data_width) <<
		       D40_MEM_LCSP0_ECNT_POS) & D40_MEM_LCSP0_ECNT_MASK;

	BUG_ON((data_size >> data_width) > STEDMA40_MAX_SEG_SIZE);

	/* 16 LSBs address of the current element */
	lli->lcsp02 |= data & D40_MEM_LCSP0_SPTR_MASK;
	/* 16 MSBs address of the current element */
	lli->lcsp13 |= data & D40_MEM_LCSP1_SPTR_MASK;

	if (addr_inc)
		lli->lcsp13 |= D40_MEM_LCSP1_SCFG_INCR_MASK;

}

int d40_log_sg_to_dev(struct scatterlist *sg,
		      int sg_len,
		      struct d40_log_lli_bidir *lli,
		      struct d40_def_lcsp *lcsp,
		      u32 src_data_width,
		      u32 dst_data_width,
		      enum dma_data_direction direction,
		      dma_addr_t dev_addr)
{
	int total_size = 0;
	struct scatterlist *current_sg = sg;
	int i;
	struct d40_log_lli *lli_src = lli->src;
	struct d40_log_lli *lli_dst = lli->dst;

	for_each_sg(sg, current_sg, sg_len, i) {
		total_size += sg_dma_len(current_sg);

		if (direction == DMA_TO_DEVICE) {
			lli_src =
				d40_log_buf_to_lli(lli_src,
						   sg_phys(current_sg),
						   sg_dma_len(current_sg),
						   lcsp->lcsp1, src_data_width,
						   dst_data_width,
						   true);
			lli_dst =
				d40_log_buf_to_lli(lli_dst,
						   dev_addr,
						   sg_dma_len(current_sg),
						   lcsp->lcsp3, dst_data_width,
						   src_data_width,
						   false);
		} else {
			lli_dst =
				d40_log_buf_to_lli(lli_dst,
						   sg_phys(current_sg),
						   sg_dma_len(current_sg),
						   lcsp->lcsp3, dst_data_width,
						   src_data_width,
						   true);
			lli_src =
				d40_log_buf_to_lli(lli_src,
						   dev_addr,
						   sg_dma_len(current_sg),
						   lcsp->lcsp1, src_data_width,
						   dst_data_width,
						   false);
		}
	}
	return total_size;
}

struct d40_log_lli *d40_log_buf_to_lli(struct d40_log_lli *lli_sg,
				       dma_addr_t addr,
				       int size,
				       u32 lcsp13, /* src or dst*/
				       u32 data_width1,
				       u32 data_width2,
				       bool addr_inc)
{
	struct d40_log_lli *lli = lli_sg;
	int size_rest = size;
	int size_seg = 0;

	do {
		size_seg = d40_seg_size(size_rest, data_width1, data_width2);
		size_rest -= size_seg;

		d40_log_fill_lli(lli,
				 addr,
				 size_seg,
				 lcsp13, data_width1,
				 addr_inc);
		if (addr_inc)
			addr += size_seg;
		lli++;
	} while (size_rest);

	return lli;
}

int d40_log_sg_to_lli(struct scatterlist *sg,
		      int sg_len,
		      struct d40_log_lli *lli_sg,
		      u32 lcsp13, /* src or dst*/
		      u32 data_width1, u32 data_width2)
{
	int total_size = 0;
	struct scatterlist *current_sg = sg;
	int i;
	struct d40_log_lli *lli = lli_sg;

	for_each_sg(sg, current_sg, sg_len, i) {
		total_size += sg_dma_len(current_sg);
		lli = d40_log_buf_to_lli(lli,
					 sg_phys(current_sg),
					 sg_dma_len(current_sg),
					 lcsp13,
					 data_width1, data_width2, true);
	}
	return total_size;
}
