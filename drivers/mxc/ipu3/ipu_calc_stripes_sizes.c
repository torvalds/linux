/*
 * Copyright 2009-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*
 * @file ipu_calc_stripes_sizes.c
 *
 * @brief IPU IC functions
 *
 * @ingroup IPU
 */

#include <linux/ipu-v3.h>
#include <linux/module.h>
#include <linux/math64.h>

#define BPP_32 0
#define BPP_16 3
#define BPP_8 5
#define BPP_24 1
#define BPP_12 4
#define BPP_18 2

static u32 truncate(u32 up, /* 0: down; else: up */
					u64 a, /* must be non-negative */
					u32 b)
{
	u32 d;
	u64 div;
	div = div_u64(a, b);
	d = b * (div >> 32);
	if (up && (a > (((u64)d) << 32)))
		return d+b;
	else
		return d;
}

static unsigned int f_calc(unsigned int pfs, unsigned int bpp, unsigned int *write)
{/* return input_f */
	unsigned int f_calculated = 0;
	switch (pfs) {
	case IPU_PIX_FMT_YVU422P:
	case IPU_PIX_FMT_YUV422P:
	case IPU_PIX_FMT_YUV420P2:
	case IPU_PIX_FMT_YUV420P:
	case IPU_PIX_FMT_YVU420P:
	case IPU_PIX_FMT_YUV444P:
		f_calculated = 16;
		break;

	case IPU_PIX_FMT_RGB565:
	case IPU_PIX_FMT_YUYV:
	case IPU_PIX_FMT_UYVY:
		f_calculated = 8;
		break;

	case IPU_PIX_FMT_NV12:
		f_calculated = 8;
		break;

	default:
		f_calculated = 0;
		break;

	}
	if (!f_calculated) {
		switch (bpp) {
		case BPP_32:
			f_calculated = 2;
			break;

		case BPP_16:
			f_calculated = 4;
			break;

		case BPP_8:
		case BPP_24:
			f_calculated = 8;
			break;

		case BPP_12:
			f_calculated = 16;
			break;

		case BPP_18:
			f_calculated = 32;
			break;

		default:
			f_calculated = 0;
			break;
			}
		}
	return f_calculated;
}


static unsigned int m_calc(unsigned int pfs)
{
	unsigned int m_calculated = 0;
	switch (pfs) {
	case IPU_PIX_FMT_YUV420P2:
	case IPU_PIX_FMT_YUV420P:
	case IPU_PIX_FMT_YVU422P:
	case IPU_PIX_FMT_YUV422P:
	case IPU_PIX_FMT_YVU420P:
	case IPU_PIX_FMT_YUV444P:
		m_calculated = 16;
		break;

	case IPU_PIX_FMT_NV12:
	case IPU_PIX_FMT_YUYV:
	case IPU_PIX_FMT_UYVY:
		m_calculated = 8;
		break;

	default:
		m_calculated = 8;
		break;

	}
	return m_calculated;
}

static int calc_split_resize_coeffs(unsigned int inSize, unsigned int outSize,
				    unsigned int *resizeCoeff,
				    unsigned int *downsizeCoeff)
{
	uint32_t tempSize;
	uint32_t tempDownsize;

	if (inSize > 4096) {
		pr_debug("IC input size(%d) cannot exceed 4096\n",
			inSize);
		return -EINVAL;
	}

	if (outSize > 1024) {
		pr_debug("IC output size(%d) cannot exceed 1024\n",
			outSize);
		return -EINVAL;
	}

	if ((outSize << 3) < inSize) {
		pr_debug("IC cannot downsize more than 8:1\n");
		return -EINVAL;
	}

	/* Compute downsizing coefficient */
	/* Output of downsizing unit cannot be more than 1024 */
	tempDownsize = 0;
	tempSize = inSize;
	while (((tempSize > 1024) || (tempSize >= outSize * 2)) &&
	       (tempDownsize < 2)) {
		tempSize >>= 1;
		tempDownsize++;
	}
	*downsizeCoeff = tempDownsize;

	/* compute resizing coefficient using the following equation:
	   resizeCoeff = M*(SI -1)/(SO - 1)
	   where M = 2^13, SI - input size, SO - output size    */
	*resizeCoeff = (8192L * (tempSize - 1)) / (outSize - 1);
	if (*resizeCoeff >= 16384L) {
		pr_debug("Overflow on IC resize coefficient.\n");
		return -EINVAL;
	}

	pr_debug("resizing from %u -> %u pixels, "
		"downsize=%u, resize=%u.%lu (reg=%u)\n", inSize, outSize,
		*downsizeCoeff, (*resizeCoeff >= 8192L) ? 1 : 0,
		((*resizeCoeff & 0x1FFF) * 10000L) / 8192L, *resizeCoeff);

	return 0;
}

/* Stripe parameters calculator */
/**************************************************************************
Notes:
MSW = the maximal width allowed for a stripe
	i.MX31: 720, i.MX35: 800, i.MX37/51/53: 1024
cirr = the maximal inverse resizing ratio for which overlap in the input
	is requested; typically cirr~2
flags
	bit 0 - equal_stripes
		0  each stripe is allowed to have independent parameters
		for maximal image quality
		1  the stripes are requested to have identical parameters
	(except the base address), for maximal performance
	bit 1 - vertical/horizontal
		0 horizontal
		1 vertical

If performance is the top priority (above image quality)
	Avoid overlap, by setting CIRR = 0
		This will also force effectively identical_stripes = 1
	Choose IF & OF that corresponds to the same IOX/SX for both stripes
	Choose IFW & OFW such that
	IFW/IM, IFW/IF, OFW/OM, OFW/OF are even integers
	The function returns an error status:
	0: no error
	1: invalid input parameters -> aborted without result
		Valid parameters should satisfy the following conditions
		IFW <= OFW, otherwise downsizing is required
					 - which is not supported yet
		4 <= IFW,OFW, so some interpolation may be needed even without overlap
		IM, OM, IF, OF should not vanish
		2*IF <= IFW
		so the frame can be split to two equal stripes, even without overlap
		2*(OF+IF/irr_opt) <= OFW
		so a valid positive INW exists even for equal stripes
		OF <= MSW, otherwise, the left stripe cannot be sufficiently large
		MSW < OFW, so splitting to stripes is required
		OFW <= 2*MSW, so two stripes are sufficient
		(this also implies that 2<=MSW)
	2: OF is not a multiple of OM - not fully-supported yet
	Output is produced but OW is not guaranited to be a multiple of OM
	4: OFW reduced to be a multiple of OM
	8: CIRR > 1: truncated to 1
	Overlap is not supported (and not needed) y for upsizing)
**************************************************************************/
int ipu_calc_stripes_sizes(const unsigned int input_frame_width,
			   /* input frame width;>1 */
			   unsigned int output_frame_width, /* output frame width; >1 */
			   const unsigned int maximal_stripe_width,
			   /* the maximal width allowed for a stripe */
			   const unsigned long long cirr, /* see above */
			   const unsigned int flags, /* see above */
			   u32 input_pixelformat,/* pixel format after of read channel*/
			   u32 output_pixelformat,/* pixel format after of write channel*/
			   struct stripe_param *left,
			   struct stripe_param *right)
{
	const unsigned int irr_frac_bits = 13;
	const unsigned long irr_steps = 1 << irr_frac_bits;
	const u64 dirr = ((u64)1) << (32 - 2);
	/* The maximum relative difference allowed between the irrs */
	const u64 cr = ((u64)4) << 32;
	/* The importance ratio between the two terms in the cost function below */

	unsigned int status;
	unsigned int temp;
	unsigned int onw_min;
	unsigned int inw = 0, onw = 0, inw_best = 0;
	/* number of pixels in the left stripe NOT hidden by the right stripe */
	u64 irr_opt; /* the optimal inverse resizing ratio */
	u64 rr_opt; /* the optimal resizing ratio = 1/irr_opt*/
	u64 dinw; /* the misalignment between the stripes */
	/* (measured in units of input columns) */
	u64 difwl, difwr = 0;
	/* The number of input columns not reflected in the output */
	/* the resizing ratio used for the right stripe is */
	/*   left->irr and right->irr respectively */
	u64 cost, cost_min;
	u64 div; /* result of division */
	bool equal_stripes = (flags & 0x1) != 0;
	bool vertical =      (flags & 0x2) != 0;

	unsigned int input_m, input_f, output_m, output_f; /* parameters for upsizing by stripes */
	unsigned int resize_coeff;
	unsigned int downsize_coeff;

	status = 0;

	if (vertical) {
		input_f = 2;
		input_m = 8;
		output_f = 8;
		output_m = 2;
	} else {
		input_f = f_calc(input_pixelformat, 0, NULL);
		input_m = m_calc(input_pixelformat);
		output_f = input_m;
		output_m = m_calc(output_pixelformat);
	}
	if ((input_frame_width < 4) || (output_frame_width < 4))
		return 1;

	irr_opt = div_u64((((u64)(input_frame_width - 1)) << 32),
			  (output_frame_width - 1));
	rr_opt = div_u64((((u64)(output_frame_width - 1)) << 32),
			 (input_frame_width - 1));

	if ((input_m == 0) || (output_m == 0) || (input_f == 0) || (output_f == 0)
	    || (input_frame_width < (2 * input_f))
	    || ((((u64)output_frame_width) << 32) <
		(2 * ((((u64)output_f) << 32) + (input_f * rr_opt))))
	    || (maximal_stripe_width < output_f)
	    || ((output_frame_width <= maximal_stripe_width)
		&& (equal_stripes == 0))
	    || ((2 * maximal_stripe_width) < output_frame_width))
		return 1;

	if (output_f % output_m)
		status += 2;

	temp = truncate(0, (((u64)output_frame_width) << 32), output_m);
	if (temp < output_frame_width) {
		output_frame_width = temp;
		status += 4;
	}

	pr_debug("---------------->\n"
		   "if  = %d\n"
		   "im  = %d\n"
		   "of = %d\n"
		   "om = %d\n"
		   "irr_opt  = %llu\n"
		   "rr_opt   = %llu\n"
		   "cirr     = %llu\n"
		   "pixel in  = %08x\n"
		   "pixel out = %08x\n"
		   "ifw = %d\n"
		   "ofwidth = %d\n",
		   input_f,
		   input_m,
		   output_f,
		   output_m,
		   irr_opt,
		   rr_opt,
		   cirr,
		   input_pixelformat,
		   output_pixelformat,
		   input_frame_width,
		   output_frame_width
		   );

	if (equal_stripes) {
		if ((irr_opt > cirr) /* overlap in the input is not requested */
		    && ((input_frame_width % (input_m << 1)) == 0)
		    && ((input_frame_width % (input_f << 1)) == 0)
		    && ((output_frame_width % (output_m << 1)) == 0)
		    && ((output_frame_width % (output_f << 1)) == 0)) {
			/* without overlap */
			left->input_width = right->input_width = right->input_column =
				input_frame_width >> 1;
			left->output_width = right->output_width = right->output_column =
				output_frame_width >> 1;
			left->input_column = 0;
			left->output_column = 0;
			div = div_u64(((((u64)irr_steps) << 32) *
				       (right->input_width - 1)), (right->output_width - 1));
			left->irr = right->irr = truncate(0, div, 1);
		} else { /* with overlap */
			onw = truncate(0, (((u64)output_frame_width - 1) << 32) >> 1,
				       output_f);
			inw = truncate(0, onw * irr_opt, input_f);
			/* this is the maximal inw which allows the same resizing ratio */
			/* in both stripes */
			onw = truncate(1, (inw * rr_opt), output_f);
			div = div_u64((((u64)(irr_steps * inw)) <<
				       32), onw);
			left->irr = right->irr = truncate(0, div, 1);
			left->output_width = right->output_width =
				output_frame_width - onw;
			/* These are valid assignments for output_width, */
			/* assuming output_f is a multiple of output_m */
			div = (((u64)(left->output_width-1) * (left->irr)) << 32);
			div = (((u64)1) << 32) + div_u64(div, irr_steps);

			left->input_width = right->input_width = truncate(1, div, input_m);

			div = div_u64((((u64)((right->output_width - 1) * right->irr)) <<
				       32), irr_steps);
			difwr = (((u64)(input_frame_width - 1 - inw)) << 32) - div;
			div = div_u64((difwr + (((u64)input_f) << 32)), 2);
			left->input_column = truncate(0, div, input_f);


			/* This splits the truncated input columns evenly */
			/*    between the left and right margins */
			right->input_column = left->input_column + inw;
			left->output_column = 0;
			right->output_column = onw;
		}
		if (left->input_width > left->output_width) {
			if (calc_split_resize_coeffs(left->input_width,
						     left->output_width,
						     &resize_coeff,
						     &downsize_coeff) < 0)
				return -EINVAL;

			if (downsize_coeff > 0) {
				left->irr = right->irr =
					(downsize_coeff << 14) | resize_coeff;
			}
		}
		pr_debug("inw %d, onw %d, ilw %d, ilc %d, olw %d,"
			 " irw %d, irc %d, orw %d, orc %d, "
			 "difwr  %llu, lirr %u\n",
			 inw, onw, left->input_width,
			 left->input_column, left->output_width,
			 right->input_width, right->input_column,
			 right->output_width,
			 right->output_column, difwr, left->irr);
		} else { /* independent stripes */
		onw_min = output_frame_width - maximal_stripe_width;
		/* onw is a multiple of output_f, in the range */
		/* [max(output_f,output_frame_width-maximal_stripe_width),*/
		/*min(output_frame_width-2,maximal_stripe_width)] */
		/* definitely beyond the cost of any valid setting */
		cost_min = (((u64)input_frame_width) << 32) + cr;
		onw = truncate(0, ((u64)maximal_stripe_width), output_f);
		if (output_frame_width - onw == 1)
			onw -= output_f; /*  => onw and output_frame_width-1-onw are positive */
		inw = truncate(0, onw * irr_opt, input_f);
		/* this is the maximal inw which allows the same resizing ratio */
		/* in both stripes */
		onw = truncate(1, inw * rr_opt, output_f);
		do {
			div = div_u64((((u64)(irr_steps * inw)) << 32), onw);
			left->irr = truncate(0, div, 1);
			div = div_u64((((u64)(onw * left->irr)) << 32),
				      irr_steps);
			dinw = (((u64)inw) << 32) - div;

			div = div_u64((((u64)((output_frame_width - 1 - onw) * left->irr)) <<
				       32), irr_steps);

			difwl = (((u64)(input_frame_width - 1 - inw)) << 32) - div;

			cost = difwl + (((u64)(cr * dinw)) >> 32);

			if (cost < cost_min) {
				inw_best = inw;
				cost_min = cost;
			}

			inw -= input_f;
			onw = truncate(1, inw * rr_opt, output_f);
			/* This is the minimal onw which allows the same resizing ratio */
			/*     in both stripes */
		} while (onw >= onw_min);

		inw = inw_best;
		onw = truncate(1, inw * rr_opt, output_f);
		div = div_u64((((u64)(irr_steps * inw)) << 32), onw);
		left->irr = truncate(0, div, 1);

		left->output_width = onw;
		right->output_width = output_frame_width - onw;
		/* These are valid assignments for output_width, */
		/* assuming output_f is a multiple of output_m */
		left->input_width = truncate(1, ((u64)(inw + 1)) << 32, input_m);
		right->input_width = truncate(1, ((u64)(input_frame_width - inw)) <<
					      32, input_m);

		div = div_u64((((u64)(irr_steps * (input_frame_width - 1 - inw))) <<
			       32), (right->output_width - 1));
		right->irr = truncate(0, div, 1);
		temp = truncate(0, ((u64)left->irr) * ((((u64)1) << 32) + dirr), 1);
		if (temp < right->irr)
			right->irr = temp;
		div = div_u64(((u64)((right->output_width - 1) * right->irr) <<
			       32), irr_steps);
		difwr = (u64)(input_frame_width - 1 - inw) - div;


		div = div_u64((difwr + (((u64)input_f) << 32)), 2);
		left->input_column = truncate(0, div, input_f);

		/* This splits the truncated input columns evenly */
		/*    between the left and right margins */
		right->input_column = left->input_column + inw;
		left->output_column = 0;
		right->output_column = onw;
		if (left->input_width > left->output_width) {
			if (calc_split_resize_coeffs(left->input_width,
						     left->output_width,
						     &resize_coeff,
						     &downsize_coeff) < 0)
				return -EINVAL;
			left->irr = (downsize_coeff << 14) | resize_coeff;
		}
		if (right->input_width > right->output_width) {
			if (calc_split_resize_coeffs(right->input_width,
						     right->output_width,
						     &resize_coeff,
						     &downsize_coeff) < 0)
				return -EINVAL;
			right->irr = (downsize_coeff << 14) | resize_coeff;
		}
	}
	return status;
}
EXPORT_SYMBOL(ipu_calc_stripes_sizes);
