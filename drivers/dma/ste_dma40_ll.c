/*
 * driver/dma/ste_dma40_ll.c
 *
 * Copyright (C) ST-Ericsson 2007-2010
 * License terms: GNU General Public License (GPL) version 2
 * Author: Per Friden <per.friden@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
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

	l3 |= 1 << D40_MEM_LCSP3_DCFG_TIM_POS;
	l3 |= 1 << D40_MEM_LCSP3_DCFG_EIM_POS;
	l3 |= cfg->dst_info.psize << D40_MEM_LCSP3_DCFG_PSIZE_POS;
	l3 |= cfg->dst_info.data_width << D40_MEM_LCSP3_DCFG_ESIZE_POS;
	l3 |= 1 << D40_MEM_LCSP3_DTCP_POS;

	l1 |= 1 << D40_MEM_LCSP1_SCFG_EIM_POS;
	l1 |= cfg->src_info.psize << D40_MEM_LCSP1_SCFG_PSIZE_POS;
	l1 |= cfg->src_info.data_width << D40_MEM_LCSP1_SCFG_ESIZE_POS;
	l1 |= 1 << D40_MEM_LCSP1_STCP_POS;

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

	if (cfg->channel_type & STEDMA40_HIGH_PRIORITY_CHANNEL) {
		src |= 1 << D40_SREG_CFG_PRI_POS;
		dst |= 1 << D40_SREG_CFG_PRI_POS;
	}

	src |= cfg->src_info.endianess << D40_SREG_CFG_LBE_POS;
	dst |= cfg->dst_info.endianess << D40_SREG_CFG_LBE_POS;

	*src_cfg = src;
	*dst_cfg = dst;
}

int d40_phy_fill_lli(struct d40_phy_lli *lli,
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

	/*
	 * Size is 16bit. data_width is 8, 16, 32 or 64 bit
	 * Block large than 64 KiB must be split.
	 */
	if (data_size > (0xffff << data_width))
		return -EINVAL;

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

int d40_phy_sg_to_lli(struct scatterlist *sg,
		      int sg_len,
		      dma_addr_t target,
		      struct d40_phy_lli *lli,
		      dma_addr_t lli_phys,
		      u32 reg_cfg,
		      u32 data_width,
		      int psize,
		      bool term_int)
{
	int total_size = 0;
	int i;
	struct scatterlist *current_sg = sg;
	dma_addr_t next_lli_phys;
	dma_addr_t dst;
	int err = 0;

	for_each_sg(sg, current_sg, sg_len, i) {

		total_size += sg_dma_len(current_sg);

		/* If this scatter list entry is the last one, no next link */
		if (sg_len - 1 == i)
			next_lli_phys = 0;
		else
			next_lli_phys = ALIGN(lli_phys + (i + 1) *
					      sizeof(struct d40_phy_lli),
					      D40_LLI_ALIGN);

		if (target)
			dst = target;
		else
			dst = sg_phys(current_sg);

		err = d40_phy_fill_lli(&lli[i],
				       dst,
				       sg_dma_len(current_sg),
				       psize,
				       next_lli_phys,
				       reg_cfg,
				       !next_lli_phys,
				       data_width,
				       target == dst);
		if (err)
			goto err;
	}

	return total_size;
 err:
	return err;
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

void d40_log_fill_lli(struct d40_log_lli *lli,
		      dma_addr_t data, u32 data_size,
		      u32 lli_next_off, u32 reg_cfg,
		      u32 data_width,
		      bool term_int, bool addr_inc)
{
	lli->lcsp13 = reg_cfg;

	/* The number of elements to transfer */
	lli->lcsp02 = ((data_size >> data_width) <<
		       D40_MEM_LCSP0_ECNT_POS) & D40_MEM_LCSP0_ECNT_MASK;
	/* 16 LSBs address of the current element */
	lli->lcsp02 |= data & D40_MEM_LCSP0_SPTR_MASK;
	/* 16 MSBs address of the current element */
	lli->lcsp13 |= data & D40_MEM_LCSP1_SPTR_MASK;

	if (addr_inc)
		lli->lcsp13 |= D40_MEM_LCSP1_SCFG_INCR_MASK;

	lli->lcsp13 |= D40_MEM_LCSP3_DTCP_MASK;
	/* If this scatter list entry is the last one, no next link */
	lli->lcsp13 |= (lli_next_off << D40_MEM_LCSP1_SLOS_POS) &
		D40_MEM_LCSP1_SLOS_MASK;

	if (term_int)
		lli->lcsp13 |= D40_MEM_LCSP1_SCFG_TIM_MASK;
	else
		lli->lcsp13 &= ~D40_MEM_LCSP1_SCFG_TIM_MASK;
}

int d40_log_sg_to_dev(struct d40_lcla_elem *lcla,
		      struct scatterlist *sg,
		      int sg_len,
		      struct d40_log_lli_bidir *lli,
		      struct d40_def_lcsp *lcsp,
		      u32 src_data_width,
		      u32 dst_data_width,
		      enum dma_data_direction direction,
		      bool term_int, dma_addr_t dev_addr, int max_len,
		      int llis_per_log)
{
	int total_size = 0;
	struct scatterlist *current_sg = sg;
	int i;
	u32 next_lli_off_dst = 0;
	u32 next_lli_off_src = 0;

	for_each_sg(sg, current_sg, sg_len, i) {
		total_size += sg_dma_len(current_sg);

		/*
		 * If this scatter list entry is the last one or
		 * max length, terminate link.
		 */
		if (sg_len - 1 == i || ((i+1) % max_len == 0)) {
			next_lli_off_src = 0;
			next_lli_off_dst = 0;
		} else {
			if (next_lli_off_dst == 0 &&
			    next_lli_off_src == 0) {
				/* The first lli will be at next_lli_off */
				next_lli_off_dst = (lcla->dst_id *
						    llis_per_log + 1);
				next_lli_off_src = (lcla->src_id *
						    llis_per_log + 1);
			} else {
				next_lli_off_dst++;
				next_lli_off_src++;
			}
		}

		if (direction == DMA_TO_DEVICE) {
			d40_log_fill_lli(&lli->src[i],
					 sg_phys(current_sg),
					 sg_dma_len(current_sg),
					 next_lli_off_src,
					 lcsp->lcsp1, src_data_width,
					 false,
					 true);
			d40_log_fill_lli(&lli->dst[i],
					 dev_addr,
					 sg_dma_len(current_sg),
					 next_lli_off_dst,
					 lcsp->lcsp3, dst_data_width,
					 /* No next == terminal interrupt */
					 term_int && !next_lli_off_dst,
					 false);
		} else {
			d40_log_fill_lli(&lli->dst[i],
					 sg_phys(current_sg),
					 sg_dma_len(current_sg),
					 next_lli_off_dst,
					 lcsp->lcsp3, dst_data_width,
					 /* No next == terminal interrupt */
					 term_int && !next_lli_off_dst,
					 true);
			d40_log_fill_lli(&lli->src[i],
					 dev_addr,
					 sg_dma_len(current_sg),
					 next_lli_off_src,
					 lcsp->lcsp1, src_data_width,
					 false,
					 false);
		}
	}
	return total_size;
}

int d40_log_sg_to_lli(int lcla_id,
		      struct scatterlist *sg,
		      int sg_len,
		      struct d40_log_lli *lli_sg,
		      u32 lcsp13, /* src or dst*/
		      u32 data_width,
		      bool term_int, int max_len, int llis_per_log)
{
	int total_size = 0;
	struct scatterlist *current_sg = sg;
	int i;
	u32 next_lli_off = 0;

	for_each_sg(sg, current_sg, sg_len, i) {
		total_size += sg_dma_len(current_sg);

		/*
		 * If this scatter list entry is the last one or
		 * max length, terminate link.
		 */
		if (sg_len - 1 == i || ((i+1) % max_len == 0))
			next_lli_off = 0;
		else {
			if (next_lli_off == 0)
				/* The first lli will be at next_lli_off */
				next_lli_off = lcla_id * llis_per_log + 1;
			else
				next_lli_off++;
		}

		d40_log_fill_lli(&lli_sg[i],
				 sg_phys(current_sg),
				 sg_dma_len(current_sg),
				 next_lli_off,
				 lcsp13, data_width,
				 term_int && !next_lli_off,
				 true);
	}
	return total_size;
}

int d40_log_lli_write(struct d40_log_lli_full *lcpa,
		       struct d40_log_lli *lcla_src,
		       struct d40_log_lli *lcla_dst,
		       struct d40_log_lli *lli_dst,
		       struct d40_log_lli *lli_src,
		       int llis_per_log)
{
	u32 slos;
	u32 dlos;
	int i;

	writel(lli_src->lcsp02, &lcpa->lcsp0);
	writel(lli_src->lcsp13, &lcpa->lcsp1);
	writel(lli_dst->lcsp02, &lcpa->lcsp2);
	writel(lli_dst->lcsp13, &lcpa->lcsp3);

	slos = lli_src->lcsp13 & D40_MEM_LCSP1_SLOS_MASK;
	dlos = lli_dst->lcsp13 & D40_MEM_LCSP3_DLOS_MASK;

	for (i = 0; (i < llis_per_log) && slos && dlos; i++) {
		writel(lli_src[i + 1].lcsp02, &lcla_src[i].lcsp02);
		writel(lli_src[i + 1].lcsp13, &lcla_src[i].lcsp13);
		writel(lli_dst[i + 1].lcsp02, &lcla_dst[i].lcsp02);
		writel(lli_dst[i + 1].lcsp13, &lcla_dst[i].lcsp13);

		slos = lli_src[i + 1].lcsp13 & D40_MEM_LCSP1_SLOS_MASK;
		dlos = lli_dst[i + 1].lcsp13 & D40_MEM_LCSP3_DLOS_MASK;
	}

	return i;

}
