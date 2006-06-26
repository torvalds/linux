#include <linux/kernel.h>
#include <media/saa7146_vv.h>

static void calculate_output_format_register(struct saa7146_dev* saa, u32 palette, u32* clip_format)
{
	/* clear out the necessary bits */
	*clip_format &= 0x0000ffff;
	/* set these bits new */
	*clip_format |=  (( ((palette&0xf00)>>8) << 30) | ((palette&0x00f) << 24) | (((palette&0x0f0)>>4) << 16));
}

static void calculate_hps_source_and_sync(struct saa7146_dev *dev, int source, int sync, u32* hps_ctrl)
{
	*hps_ctrl &= ~(MASK_30 | MASK_31 | MASK_28);
	*hps_ctrl |= (source << 30) | (sync << 28);
}

static void calculate_hxo_and_hyo(struct saa7146_vv *vv, u32* hps_h_scale, u32* hps_ctrl)
{
	int hyo = 0, hxo = 0;

	hyo = vv->standard->v_offset;
	hxo = vv->standard->h_offset;

	*hps_h_scale	&= ~(MASK_B0 | 0xf00);
	*hps_h_scale	|= (hxo <<  0);

	*hps_ctrl	&= ~(MASK_W0 | MASK_B2);
	*hps_ctrl	|= (hyo << 12);
}

/* helper functions for the calculation of the horizontal- and vertical
   scaling registers, clip-format-register etc ...
   these functions take pointers to the (most-likely read-out
   original-values) and manipulate them according to the requested
   changes.
*/

/* hps_coeff used for CXY and CXUV; scale 1/1 -> scale 1/64 */
static struct {
	u16 hps_coeff;
	u16 weight_sum;
} hps_h_coeff_tab [] = {
	{0x00,   2}, {0x02,   4}, {0x00,   4}, {0x06,   8}, {0x02,   8},
	{0x08,   8}, {0x00,   8}, {0x1E,  16}, {0x0E,   8}, {0x26,   8},
	{0x06,   8}, {0x42,   8}, {0x02,   8}, {0x80,   8}, {0x00,   8},
	{0xFE,  16}, {0xFE,   8}, {0x7E,   8}, {0x7E,   8}, {0x3E,   8},
	{0x3E,   8}, {0x1E,   8}, {0x1E,   8}, {0x0E,   8}, {0x0E,   8},
	{0x06,   8}, {0x06,   8}, {0x02,   8}, {0x02,   8}, {0x00,   8},
	{0x00,   8}, {0xFE,  16}, {0xFE,   8}, {0xFE,   8}, {0xFE,   8},
	{0xFE,   8}, {0xFE,   8}, {0xFE,   8}, {0xFE,   8}, {0xFE,   8},
	{0xFE,   8}, {0xFE,   8}, {0xFE,   8}, {0xFE,   8}, {0xFE,   8},
	{0xFE,   8}, {0xFE,   8}, {0xFE,   8}, {0xFE,   8}, {0x7E,   8},
	{0x7E,   8}, {0x3E,   8}, {0x3E,   8}, {0x1E,   8}, {0x1E,   8},
	{0x0E,   8}, {0x0E,   8}, {0x06,   8}, {0x06,   8}, {0x02,   8},
	{0x02,   8}, {0x00,   8}, {0x00,   8}, {0xFE,  16}
};

/* table of attenuation values for horizontal scaling */
static u8 h_attenuation[] = { 1, 2, 4, 8, 2, 4, 8, 16, 0};

/* calculate horizontal scale registers */
static int calculate_h_scale_registers(struct saa7146_dev *dev,
	int in_x, int out_x, int flip_lr,
	u32* hps_ctrl, u32* hps_v_gain, u32* hps_h_prescale, u32* hps_h_scale)
{
	/* horizontal prescaler */
	u32 dcgx = 0, xpsc = 0, xacm = 0, cxy = 0, cxuv = 0;
	/* horizontal scaler */
	u32 xim = 0, xp = 0, xsci =0;
	/* vertical scale & gain */
	u32 pfuv = 0;

	/* helper variables */
	u32 h_atten = 0, i = 0;

	if ( 0 == out_x ) {
		return -EINVAL;
	}

	/* mask out vanity-bit */
	*hps_ctrl &= ~MASK_29;

	/* calculate prescale-(xspc)-value:	[n   .. 1/2) : 1
						[1/2 .. 1/3) : 2
						[1/3 .. 1/4) : 3
						...		*/
	if (in_x > out_x) {
		xpsc = in_x / out_x;
	}
	else {
		/* zooming */
		xpsc = 1;
	}

	/* if flip_lr-bit is set, number of pixels after
	   horizontal prescaling must be < 384 */
	if ( 0 != flip_lr ) {

		/* set vanity bit */
		*hps_ctrl |= MASK_29;

		while (in_x / xpsc >= 384 )
			xpsc++;
	}
	/* if zooming is wanted, number of pixels after
	   horizontal prescaling must be < 768 */
	else {
		while ( in_x / xpsc >= 768 )
			xpsc++;
	}

	/* maximum prescale is 64 (p.69) */
	if ( xpsc > 64 )
		xpsc = 64;

	/* keep xacm clear*/
	xacm = 0;

	/* set horizontal filter parameters (CXY = CXUV) */
	cxy = hps_h_coeff_tab[( (xpsc - 1) < 63 ? (xpsc - 1) : 63 )].hps_coeff;
	cxuv = cxy;

	/* calculate and set horizontal fine scale (xsci) */

	/* bypass the horizontal scaler ? */
	if ( (in_x == out_x) && ( 1 == xpsc ) )
		xsci = 0x400;
	else
		xsci = ( (1024 * in_x) / (out_x * xpsc) ) + xpsc;

	/* set start phase for horizontal fine scale (xp) to 0 */
	xp = 0;

	/* set xim, if we bypass the horizontal scaler */
	if ( 0x400 == xsci )
		xim = 1;
	else
		xim = 0;

	/* if the prescaler is bypassed, enable horizontal
	   accumulation mode (xacm) and clear dcgx */
	if( 1 == xpsc ) {
		xacm = 1;
		dcgx = 0;
	} else {
		xacm = 0;
		/* get best match in the table of attenuations
		   for horizontal scaling */
		h_atten = hps_h_coeff_tab[( (xpsc - 1) < 63 ? (xpsc - 1) : 63 )].weight_sum;

		for (i = 0; h_attenuation[i] != 0; i++) {
			if (h_attenuation[i] >= h_atten)
				break;
		}

		dcgx = i;
	}

	/* the horizontal scaling increment controls the UV filter
	   to reduce the bandwith to improve the display quality,
	   so set it ... */
	if ( xsci == 0x400)
		pfuv = 0x00;
	else if ( xsci < 0x600)
		pfuv = 0x01;
	else if ( xsci < 0x680)
		pfuv = 0x11;
	else if ( xsci < 0x700)
		pfuv = 0x22;
	else
		pfuv = 0x33;


	*hps_v_gain  &= MASK_W0|MASK_B2;
	*hps_v_gain  |= (pfuv << 24);

	*hps_h_scale	&= ~(MASK_W1 | 0xf000);
	*hps_h_scale	|= (xim << 31) | (xp << 24) | (xsci << 12);

	*hps_h_prescale	|= (dcgx << 27) | ((xpsc-1) << 18) | (xacm << 17) | (cxy << 8) | (cxuv << 0);

	return 0;
}

static struct {
	u16 hps_coeff;
	u16 weight_sum;
} hps_v_coeff_tab [] = {
 {0x0100,   2},  {0x0102,   4},  {0x0300,   4},  {0x0106,   8},  {0x0502,   8},
 {0x0708,   8},  {0x0F00,   8},  {0x011E,  16},  {0x110E,  16},  {0x1926,  16},
 {0x3906,  16},  {0x3D42,  16},  {0x7D02,  16},  {0x7F80,  16},  {0xFF00,  16},
 {0x01FE,  32},  {0x01FE,  32},  {0x817E,  32},  {0x817E,  32},  {0xC13E,  32},
 {0xC13E,  32},  {0xE11E,  32},  {0xE11E,  32},  {0xF10E,  32},  {0xF10E,  32},
 {0xF906,  32},  {0xF906,  32},  {0xFD02,  32},  {0xFD02,  32},  {0xFF00,  32},
 {0xFF00,  32},  {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},
 {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},
 {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},
 {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},  {0x817E,  64},
 {0x817E,  64},  {0xC13E,  64},  {0xC13E,  64},  {0xE11E,  64},  {0xE11E,  64},
 {0xF10E,  64},  {0xF10E,  64},  {0xF906,  64},  {0xF906,  64},  {0xFD02,  64},
 {0xFD02,  64},  {0xFF00,  64},  {0xFF00,  64},  {0x01FE, 128}
};

/* table of attenuation values for vertical scaling */
static u16 v_attenuation[] = { 2, 4, 8, 16, 32, 64, 128, 256, 0};

/* calculate vertical scale registers */
static int calculate_v_scale_registers(struct saa7146_dev *dev, enum v4l2_field field,
	int in_y, int out_y, u32* hps_v_scale, u32* hps_v_gain)
{
	int lpi = 0;

	/* vertical scaling */
	u32 yacm = 0, ysci = 0, yacl = 0, ypo = 0, ype = 0;
	/* vertical scale & gain */
	u32 dcgy = 0, cya_cyb = 0;

	/* helper variables */
	u32 v_atten = 0, i = 0;

	/* error, if vertical zooming */
	if ( in_y < out_y ) {
		return -EINVAL;
	}

	/* linear phase interpolation may be used
	   if scaling is between 1 and 1/2 (both fields used)
	   or scaling is between 1/2 and 1/4 (if only one field is used) */

	if (V4L2_FIELD_HAS_BOTH(field)) {
		if( 2*out_y >= in_y) {
			lpi = 1;
		}
	} else if (field == V4L2_FIELD_TOP
		|| field == V4L2_FIELD_ALTERNATE
		|| field == V4L2_FIELD_BOTTOM) {
		if( 4*out_y >= in_y ) {
			lpi = 1;
		}
		out_y *= 2;
	}
	if( 0 != lpi ) {

		yacm = 0;
		yacl = 0;
		cya_cyb = 0x00ff;

		/* calculate scaling increment */
		if ( in_y > out_y )
			ysci = ((1024 * in_y) / (out_y + 1)) - 1024;
		else
			ysci = 0;

		dcgy = 0;

		/* calculate ype and ypo */
		ype = ysci / 16;
		ypo = ype + (ysci / 64);

	} else {
		yacm = 1;

		/* calculate scaling increment */
		ysci = (((10 * 1024 * (in_y - out_y - 1)) / in_y) + 9) / 10;

		/* calculate ype and ypo */
		ypo = ype = ((ysci + 15) / 16);

		/* the sequence length interval (yacl) has to be set according
		   to the prescale value, e.g.	[n   .. 1/2) : 0
						[1/2 .. 1/3) : 1
						[1/3 .. 1/4) : 2
						... */
		if ( ysci < 512) {
			yacl = 0;
		} else {
			yacl = ( ysci / (1024 - ysci) );
		}

		/* get filter coefficients for cya, cyb from table hps_v_coeff_tab */
		cya_cyb = hps_v_coeff_tab[ (yacl < 63 ? yacl : 63 ) ].hps_coeff;

		/* get best match in the table of attenuations for vertical scaling */
		v_atten = hps_v_coeff_tab[ (yacl < 63 ? yacl : 63 ) ].weight_sum;

		for (i = 0; v_attenuation[i] != 0; i++) {
			if (v_attenuation[i] >= v_atten)
				break;
		}

		dcgy = i;
	}

	/* ypo and ype swapped in spec ? */
	*hps_v_scale	|= (yacm << 31) | (ysci << 21) | (yacl << 15) | (ypo << 8 ) | (ype << 1);

	*hps_v_gain	&= ~(MASK_W0|MASK_B2);
	*hps_v_gain	|= (dcgy << 16) | (cya_cyb << 0);

	return 0;
}

/* simple bubble-sort algorithm with duplicate elimination */
static int sort_and_eliminate(u32* values, int* count)
{
	int low = 0, high = 0, top = 0, temp = 0;
	int cur = 0, next = 0;

	/* sanity checks */
	if( (0 > *count) || (NULL == values) ) {
		return -EINVAL;
	}

	/* bubble sort the first ´count´ items of the array ´values´ */
	for( top = *count; top > 0; top--) {
		for( low = 0, high = 1; high < top; low++, high++) {
			if( values[low] > values[high] ) {
				temp = values[low];
				values[low] = values[high];
				values[high] = temp;
			}
		}
	}

	/* remove duplicate items */
	for( cur = 0, next = 1; next < *count; next++) {
		if( values[cur] != values[next])
			values[++cur] = values[next];
	}

	*count = cur + 1;

	return 0;
}

static void calculate_clipping_registers_rect(struct saa7146_dev *dev, struct saa7146_fh *fh,
	struct saa7146_video_dma *vdma2, u32* clip_format, u32* arbtr_ctrl, enum v4l2_field field)
{
	struct saa7146_vv *vv = dev->vv_data;
	u32 *clipping = vv->d_clipping.cpu_addr;

	int width = fh->ov.win.w.width;
	int height =  fh->ov.win.w.height;
	int clipcount = fh->ov.nclips;

	u32 line_list[32];
	u32 pixel_list[32];
	int numdwords = 0;

	int i = 0, j = 0;
	int cnt_line = 0, cnt_pixel = 0;

	int x[32], y[32], w[32], h[32];

	/* clear out memory */
	memset(&line_list[0],  0x00, sizeof(u32)*32);
	memset(&pixel_list[0], 0x00, sizeof(u32)*32);
	memset(clipping,  0x00, SAA7146_CLIPPING_MEM);

	/* fill the line and pixel-lists */
	for(i = 0; i < clipcount; i++) {
		int l = 0, r = 0, t = 0, b = 0;

		x[i] = fh->ov.clips[i].c.left;
		y[i] = fh->ov.clips[i].c.top;
		w[i] = fh->ov.clips[i].c.width;
		h[i] = fh->ov.clips[i].c.height;

		if( w[i] < 0) {
			x[i] += w[i]; w[i] = -w[i];
		}
		if( h[i] < 0) {
			y[i] += h[i]; h[i] = -h[i];
		}
		if( x[i] < 0) {
			w[i] += x[i]; x[i] = 0;
		}
		if( y[i] < 0) {
			h[i] += y[i]; y[i] = 0;
		}
		if( 0 != vv->vflip ) {
			y[i] = height - y[i] - h[i];
		}

		l = x[i];
		r = x[i]+w[i];
		t = y[i];
		b = y[i]+h[i];

		/* insert left/right coordinates */
		pixel_list[ 2*i   ] = min_t(int, l, width);
		pixel_list[(2*i)+1] = min_t(int, r, width);
		/* insert top/bottom coordinates */
		line_list[ 2*i   ] = min_t(int, t, height);
		line_list[(2*i)+1] = min_t(int, b, height);
	}

	/* sort and eliminate lists */
	cnt_line = cnt_pixel = 2*clipcount;
	sort_and_eliminate( &pixel_list[0], &cnt_pixel );
	sort_and_eliminate( &line_list[0], &cnt_line );

	/* calculate the number of used u32s */
	numdwords = max_t(int, (cnt_line+1), (cnt_pixel+1))*2;
	numdwords = max_t(int, 4, numdwords);
	numdwords = min_t(int, 64, numdwords);

	/* fill up cliptable */
	for(i = 0; i < cnt_pixel; i++) {
		clipping[2*i] |= cpu_to_le32(pixel_list[i] << 16);
	}
	for(i = 0; i < cnt_line; i++) {
		clipping[(2*i)+1] |= cpu_to_le32(line_list[i] << 16);
	}

	/* fill up cliptable with the display infos */
	for(j = 0; j < clipcount; j++) {

		for(i = 0; i < cnt_pixel; i++) {

			if( x[j] < 0)
				x[j] = 0;

			if( pixel_list[i] < (x[j] + w[j])) {

				if ( pixel_list[i] >= x[j] ) {
					clipping[2*i] |= cpu_to_le32(1 << j);
				}
			}
		}
		for(i = 0; i < cnt_line; i++) {

			if( y[j] < 0)
				y[j] = 0;

			if( line_list[i] < (y[j] + h[j]) ) {

				if( line_list[i] >= y[j] ) {
					clipping[(2*i)+1] |= cpu_to_le32(1 << j);
				}
			}
		}
	}

	/* adjust arbitration control register */
	*arbtr_ctrl &= 0xffff00ff;
	*arbtr_ctrl |= 0x00001c00;

	vdma2->base_even	= vv->d_clipping.dma_handle;
	vdma2->base_odd		= vv->d_clipping.dma_handle;
	vdma2->prot_addr	= vv->d_clipping.dma_handle+((sizeof(u32))*(numdwords));
	vdma2->base_page	= 0x04;
	vdma2->pitch		= 0x00;
	vdma2->num_line_byte	= (0 << 16 | (sizeof(u32))*(numdwords-1) );

	/* set clipping-mode. this depends on the field(s) used */
	*clip_format &= 0xfffffff7;
	if (V4L2_FIELD_HAS_BOTH(field)) {
		*clip_format |= 0x00000008;
	} else {
		*clip_format |= 0x00000000;
	}
}

/* disable clipping */
static void saa7146_disable_clipping(struct saa7146_dev *dev)
{
	u32 clip_format	= saa7146_read(dev, CLIP_FORMAT_CTRL);

	/* mask out relevant bits (=lower word)*/
	clip_format &= MASK_W1;

	/* upload clipping-registers*/
	saa7146_write(dev, CLIP_FORMAT_CTRL,clip_format);
	saa7146_write(dev, MC2, (MASK_05 | MASK_21));

	/* disable video dma2 */
	saa7146_write(dev, MC1, MASK_21);
}

static void saa7146_set_clipping_rect(struct saa7146_fh *fh)
{
	struct saa7146_dev *dev = fh->dev;
	enum v4l2_field field = fh->ov.win.field;
	struct	saa7146_video_dma vdma2;
	u32 clip_format;
	u32 arbtr_ctrl;

	/* check clipcount, disable clipping if clipcount == 0*/
	if( fh->ov.nclips == 0 ) {
		saa7146_disable_clipping(dev);
		return;
	}

	clip_format = saa7146_read(dev, CLIP_FORMAT_CTRL);
	arbtr_ctrl = saa7146_read(dev, PCI_BT_V1);

	calculate_clipping_registers_rect(dev, fh, &vdma2, &clip_format, &arbtr_ctrl, field);

	/* set clipping format */
	clip_format &= 0xffff0008;
	clip_format |= (SAA7146_CLIPPING_RECT << 4);

	/* prepare video dma2 */
	saa7146_write(dev, BASE_EVEN2,		vdma2.base_even);
	saa7146_write(dev, BASE_ODD2,		vdma2.base_odd);
	saa7146_write(dev, PROT_ADDR2,		vdma2.prot_addr);
	saa7146_write(dev, BASE_PAGE2,		vdma2.base_page);
	saa7146_write(dev, PITCH2,		vdma2.pitch);
	saa7146_write(dev, NUM_LINE_BYTE2,	vdma2.num_line_byte);

	/* prepare the rest */
	saa7146_write(dev, CLIP_FORMAT_CTRL,clip_format);
	saa7146_write(dev, PCI_BT_V1, arbtr_ctrl);

	/* upload clip_control-register, clipping-registers, enable video dma2 */
	saa7146_write(dev, MC2, (MASK_05 | MASK_21 | MASK_03 | MASK_19));
	saa7146_write(dev, MC1, (MASK_05 | MASK_21));
}

static void saa7146_set_window(struct saa7146_dev *dev, int width, int height, enum v4l2_field field)
{
	struct saa7146_vv *vv = dev->vv_data;

	int source = vv->current_hps_source;
	int sync = vv->current_hps_sync;

	u32 hps_v_scale = 0, hps_v_gain  = 0, hps_ctrl = 0, hps_h_prescale = 0, hps_h_scale = 0;

	/* set vertical scale */
	hps_v_scale = 0; /* all bits get set by the function-call */
	hps_v_gain  = 0; /* fixme: saa7146_read(dev, HPS_V_GAIN);*/
	calculate_v_scale_registers(dev, field, vv->standard->v_field*2, height, &hps_v_scale, &hps_v_gain);

	/* set horizontal scale */
	hps_ctrl	= 0;
	hps_h_prescale	= 0; /* all bits get set in the function */
	hps_h_scale	= 0;
	calculate_h_scale_registers(dev, vv->standard->h_pixels, width, vv->hflip, &hps_ctrl, &hps_v_gain, &hps_h_prescale, &hps_h_scale);

	/* set hyo and hxo */
	calculate_hxo_and_hyo(vv, &hps_h_scale, &hps_ctrl);
	calculate_hps_source_and_sync(dev, source, sync, &hps_ctrl);

	/* write out new register contents */
	saa7146_write(dev, HPS_V_SCALE,	hps_v_scale);
	saa7146_write(dev, HPS_V_GAIN,	hps_v_gain);
	saa7146_write(dev, HPS_CTRL,	hps_ctrl);
	saa7146_write(dev, HPS_H_PRESCALE,hps_h_prescale);
	saa7146_write(dev, HPS_H_SCALE,	hps_h_scale);

	/* upload shadow-ram registers */
	saa7146_write(dev, MC2, (MASK_05 | MASK_06 | MASK_21 | MASK_22) );
}

/* calculate the new memory offsets for a desired position */
static void saa7146_set_position(struct saa7146_dev *dev, int w_x, int w_y, int w_height, enum v4l2_field field, u32 pixelformat)
{
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_format *sfmt = format_by_fourcc(dev, pixelformat);

	int b_depth = vv->ov_fmt->depth;
	int b_bpl = vv->ov_fb.fmt.bytesperline;
	/* The unsigned long cast is to remove a 64-bit compile warning since
	   it looks like a 64-bit address is cast to a 32-bit value, even
	   though the base pointer is really a 32-bit physical address that
	   goes into a 32-bit DMA register.
	   FIXME: might not work on some 64-bit platforms, but see the FIXME
	   in struct v4l2_framebuffer (videodev2.h) for that.
	 */
	u32 base = (u32)(unsigned long)vv->ov_fb.base;

	struct	saa7146_video_dma vdma1;

	/* calculate memory offsets for picture, look if we shall top-down-flip */
	vdma1.pitch	= 2*b_bpl;
	if ( 0 == vv->vflip ) {
		vdma1.base_even = base + (w_y * (vdma1.pitch/2)) + (w_x * (b_depth / 8));
		vdma1.base_odd  = vdma1.base_even + (vdma1.pitch / 2);
		vdma1.prot_addr = vdma1.base_even + (w_height * (vdma1.pitch / 2));
	}
	else {
		vdma1.base_even = base + ((w_y+w_height) * (vdma1.pitch/2)) + (w_x * (b_depth / 8));
		vdma1.base_odd  = vdma1.base_even - (vdma1.pitch / 2);
		vdma1.prot_addr = vdma1.base_odd - (w_height * (vdma1.pitch / 2));
	}

	if (V4L2_FIELD_HAS_BOTH(field)) {
	} else if (field == V4L2_FIELD_ALTERNATE) {
		/* fixme */
		vdma1.base_odd = vdma1.prot_addr;
		vdma1.pitch /= 2;
	} else if (field == V4L2_FIELD_TOP) {
		vdma1.base_odd = vdma1.prot_addr;
		vdma1.pitch /= 2;
	} else if (field == V4L2_FIELD_BOTTOM) {
		vdma1.base_odd = vdma1.base_even;
		vdma1.base_even = vdma1.prot_addr;
		vdma1.pitch /= 2;
	}

	if ( 0 != vv->vflip ) {
		vdma1.pitch *= -1;
	}

	vdma1.base_page = sfmt->swap;
	vdma1.num_line_byte = (vv->standard->v_field<<16)+vv->standard->h_pixels;

	saa7146_write_out_dma(dev, 1, &vdma1);
}

static void saa7146_set_output_format(struct saa7146_dev *dev, unsigned long palette)
{
	u32 clip_format = saa7146_read(dev, CLIP_FORMAT_CTRL);

	/* call helper function */
	calculate_output_format_register(dev,palette,&clip_format);

	/* update the hps registers */
	saa7146_write(dev, CLIP_FORMAT_CTRL, clip_format);
	saa7146_write(dev, MC2, (MASK_05 | MASK_21));
}

/* select input-source */
void saa7146_set_hps_source_and_sync(struct saa7146_dev *dev, int source, int sync)
{
	struct saa7146_vv *vv = dev->vv_data;
	u32 hps_ctrl = 0;

	/* read old state */
	hps_ctrl = saa7146_read(dev, HPS_CTRL);

	hps_ctrl &= ~( MASK_31 | MASK_30 | MASK_28 );
	hps_ctrl |= (source << 30) | (sync << 28);

	/* write back & upload register */
	saa7146_write(dev, HPS_CTRL, hps_ctrl);
	saa7146_write(dev, MC2, (MASK_05 | MASK_21));

	vv->current_hps_source = source;
	vv->current_hps_sync = sync;
}
EXPORT_SYMBOL_GPL(saa7146_set_hps_source_and_sync);

int saa7146_enable_overlay(struct saa7146_fh *fh)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;

	saa7146_set_window(dev, fh->ov.win.w.width, fh->ov.win.w.height, fh->ov.win.field);
	saa7146_set_position(dev, fh->ov.win.w.left, fh->ov.win.w.top, fh->ov.win.w.height, fh->ov.win.field, vv->ov_fmt->pixelformat);
	saa7146_set_output_format(dev, vv->ov_fmt->trans);
	saa7146_set_clipping_rect(fh);

	/* enable video dma1 */
	saa7146_write(dev, MC1, (MASK_06 | MASK_22));
	return 0;
}

void saa7146_disable_overlay(struct saa7146_fh *fh)
{
	struct saa7146_dev *dev = fh->dev;

	/* disable clipping + video dma1 */
	saa7146_disable_clipping(dev);
	saa7146_write(dev, MC1, MASK_22);
}

void saa7146_write_out_dma(struct saa7146_dev* dev, int which, struct saa7146_video_dma* vdma)
{
	int where = 0;

	if( which < 1 || which > 3) {
		return;
	}

	/* calculate starting address */
	where  = (which-1)*0x18;

	saa7146_write(dev, where,	vdma->base_odd);
	saa7146_write(dev, where+0x04,	vdma->base_even);
	saa7146_write(dev, where+0x08,	vdma->prot_addr);
	saa7146_write(dev, where+0x0c,	vdma->pitch);
	saa7146_write(dev, where+0x10,	vdma->base_page);
	saa7146_write(dev, where+0x14,	vdma->num_line_byte);

	/* upload */
	saa7146_write(dev, MC2, (MASK_02<<(which-1))|(MASK_18<<(which-1)));
/*
	printk("vdma%d.base_even:     0x%08x\n", which,vdma->base_even);
	printk("vdma%d.base_odd:      0x%08x\n", which,vdma->base_odd);
	printk("vdma%d.prot_addr:     0x%08x\n", which,vdma->prot_addr);
	printk("vdma%d.base_page:     0x%08x\n", which,vdma->base_page);
	printk("vdma%d.pitch:         0x%08x\n", which,vdma->pitch);
	printk("vdma%d.num_line_byte: 0x%08x\n", which,vdma->num_line_byte);
*/
}

static int calculate_video_dma_grab_packed(struct saa7146_dev* dev, struct saa7146_buf *buf)
{
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_video_dma vdma1;

	struct saa7146_format *sfmt = format_by_fourcc(dev,buf->fmt->pixelformat);

	int width = buf->fmt->width;
	int height = buf->fmt->height;
	int bytesperline = buf->fmt->bytesperline;
	enum v4l2_field field = buf->fmt->field;

	int depth = sfmt->depth;

	DEB_CAP(("[size=%dx%d,fields=%s]\n",
		width,height,v4l2_field_names[field]));

	if( bytesperline != 0) {
		vdma1.pitch = bytesperline*2;
	} else {
		vdma1.pitch = (width*depth*2)/8;
	}
	vdma1.num_line_byte	= ((vv->standard->v_field<<16) + vv->standard->h_pixels);
	vdma1.base_page		= buf->pt[0].dma | ME1 | sfmt->swap;

	if( 0 != vv->vflip ) {
		vdma1.prot_addr	= buf->pt[0].offset;
		vdma1.base_even	= buf->pt[0].offset+(vdma1.pitch/2)*height;
		vdma1.base_odd	= vdma1.base_even - (vdma1.pitch/2);
	} else {
		vdma1.base_even	= buf->pt[0].offset;
		vdma1.base_odd	= vdma1.base_even + (vdma1.pitch/2);
		vdma1.prot_addr	= buf->pt[0].offset+(vdma1.pitch/2)*height;
	}

	if (V4L2_FIELD_HAS_BOTH(field)) {
	} else if (field == V4L2_FIELD_ALTERNATE) {
		/* fixme */
		if ( vv->last_field == V4L2_FIELD_TOP ) {
			vdma1.base_odd	= vdma1.prot_addr;
			vdma1.pitch /= 2;
		} else if ( vv->last_field == V4L2_FIELD_BOTTOM ) {
			vdma1.base_odd	= vdma1.base_even;
			vdma1.base_even = vdma1.prot_addr;
			vdma1.pitch /= 2;
		}
	} else if (field == V4L2_FIELD_TOP) {
		vdma1.base_odd	= vdma1.prot_addr;
		vdma1.pitch /= 2;
	} else if (field == V4L2_FIELD_BOTTOM) {
		vdma1.base_odd	= vdma1.base_even;
		vdma1.base_even = vdma1.prot_addr;
		vdma1.pitch /= 2;
	}

	if( 0 != vv->vflip ) {
		vdma1.pitch *= -1;
	}

	saa7146_write_out_dma(dev, 1, &vdma1);
	return 0;
}

static int calc_planar_422(struct saa7146_vv *vv, struct saa7146_buf *buf, struct saa7146_video_dma *vdma2, struct saa7146_video_dma *vdma3)
{
	int height = buf->fmt->height;
	int width = buf->fmt->width;

	vdma2->pitch	= width;
	vdma3->pitch	= width;

	/* fixme: look at bytesperline! */

	if( 0 != vv->vflip ) {
		vdma2->prot_addr	= buf->pt[1].offset;
		vdma2->base_even	= ((vdma2->pitch/2)*height)+buf->pt[1].offset;
		vdma2->base_odd		= vdma2->base_even - (vdma2->pitch/2);

		vdma3->prot_addr	= buf->pt[2].offset;
		vdma3->base_even	= ((vdma3->pitch/2)*height)+buf->pt[2].offset;
		vdma3->base_odd		= vdma3->base_even - (vdma3->pitch/2);
	} else {
		vdma3->base_even	= buf->pt[2].offset;
		vdma3->base_odd		= vdma3->base_even + (vdma3->pitch/2);
		vdma3->prot_addr	= (vdma3->pitch/2)*height+buf->pt[2].offset;

		vdma2->base_even	= buf->pt[1].offset;
		vdma2->base_odd		= vdma2->base_even + (vdma2->pitch/2);
		vdma2->prot_addr	= (vdma2->pitch/2)*height+buf->pt[1].offset;
	}

	return 0;
}

static int calc_planar_420(struct saa7146_vv *vv, struct saa7146_buf *buf, struct saa7146_video_dma *vdma2, struct saa7146_video_dma *vdma3)
{
	int height = buf->fmt->height;
	int width = buf->fmt->width;

	vdma2->pitch	= width/2;
	vdma3->pitch	= width/2;

	if( 0 != vv->vflip ) {
		vdma2->prot_addr	= buf->pt[2].offset;
		vdma2->base_even	= ((vdma2->pitch/2)*height)+buf->pt[2].offset;
		vdma2->base_odd		= vdma2->base_even - (vdma2->pitch/2);

		vdma3->prot_addr	= buf->pt[1].offset;
		vdma3->base_even	= ((vdma3->pitch/2)*height)+buf->pt[1].offset;
		vdma3->base_odd		= vdma3->base_even - (vdma3->pitch/2);

	} else {
		vdma3->base_even	= buf->pt[2].offset;
		vdma3->base_odd		= vdma3->base_even + (vdma3->pitch);
		vdma3->prot_addr	= (vdma3->pitch/2)*height+buf->pt[2].offset;

		vdma2->base_even	= buf->pt[1].offset;
		vdma2->base_odd		= vdma2->base_even + (vdma2->pitch);
		vdma2->prot_addr	= (vdma2->pitch/2)*height+buf->pt[1].offset;
	}
	return 0;
}

static int calculate_video_dma_grab_planar(struct saa7146_dev* dev, struct saa7146_buf *buf)
{
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_video_dma vdma1;
	struct saa7146_video_dma vdma2;
	struct saa7146_video_dma vdma3;

	struct saa7146_format *sfmt = format_by_fourcc(dev,buf->fmt->pixelformat);

	int width = buf->fmt->width;
	int height = buf->fmt->height;
	enum v4l2_field field = buf->fmt->field;

	BUG_ON(0 == buf->pt[0].dma);
	BUG_ON(0 == buf->pt[1].dma);
	BUG_ON(0 == buf->pt[2].dma);

	DEB_CAP(("[size=%dx%d,fields=%s]\n",
		width,height,v4l2_field_names[field]));

	/* fixme: look at bytesperline! */

	/* fixme: what happens for user space buffers here?. The offsets are
	   most likely wrong, this version here only works for page-aligned
	   buffers, modifications to the pagetable-functions are necessary...*/

	vdma1.pitch		= width*2;
	vdma1.num_line_byte	= ((vv->standard->v_field<<16) + vv->standard->h_pixels);
	vdma1.base_page		= buf->pt[0].dma | ME1;

	if( 0 != vv->vflip ) {
		vdma1.prot_addr	= buf->pt[0].offset;
		vdma1.base_even	= ((vdma1.pitch/2)*height)+buf->pt[0].offset;
		vdma1.base_odd	= vdma1.base_even - (vdma1.pitch/2);
	} else {
		vdma1.base_even	= buf->pt[0].offset;
		vdma1.base_odd	= vdma1.base_even + (vdma1.pitch/2);
		vdma1.prot_addr	= (vdma1.pitch/2)*height+buf->pt[0].offset;
	}

	vdma2.num_line_byte	= 0; /* unused */
	vdma2.base_page		= buf->pt[1].dma | ME1;

	vdma3.num_line_byte	= 0; /* unused */
	vdma3.base_page		= buf->pt[2].dma | ME1;

	switch( sfmt->depth ) {
		case 12: {
			calc_planar_420(vv,buf,&vdma2,&vdma3);
			break;
		}
		case 16: {
			calc_planar_422(vv,buf,&vdma2,&vdma3);
			break;
		}
		default: {
			return -1;
		}
	}

	if (V4L2_FIELD_HAS_BOTH(field)) {
	} else if (field == V4L2_FIELD_ALTERNATE) {
		/* fixme */
		vdma1.base_odd	= vdma1.prot_addr;
		vdma1.pitch /= 2;
		vdma2.base_odd	= vdma2.prot_addr;
		vdma2.pitch /= 2;
		vdma3.base_odd	= vdma3.prot_addr;
		vdma3.pitch /= 2;
	} else if (field == V4L2_FIELD_TOP) {
		vdma1.base_odd	= vdma1.prot_addr;
		vdma1.pitch /= 2;
		vdma2.base_odd	= vdma2.prot_addr;
		vdma2.pitch /= 2;
		vdma3.base_odd	= vdma3.prot_addr;
		vdma3.pitch /= 2;
	} else if (field == V4L2_FIELD_BOTTOM) {
		vdma1.base_odd	= vdma1.base_even;
		vdma1.base_even = vdma1.prot_addr;
		vdma1.pitch /= 2;
		vdma2.base_odd	= vdma2.base_even;
		vdma2.base_even = vdma2.prot_addr;
		vdma2.pitch /= 2;
		vdma3.base_odd	= vdma3.base_even;
		vdma3.base_even = vdma3.prot_addr;
		vdma3.pitch /= 2;
	}

	if( 0 != vv->vflip ) {
		vdma1.pitch *= -1;
		vdma2.pitch *= -1;
		vdma3.pitch *= -1;
	}

	saa7146_write_out_dma(dev, 1, &vdma1);
	if( (sfmt->flags & FORMAT_BYTE_SWAP) != 0 ) {
		saa7146_write_out_dma(dev, 3, &vdma2);
		saa7146_write_out_dma(dev, 2, &vdma3);
	} else {
		saa7146_write_out_dma(dev, 2, &vdma2);
		saa7146_write_out_dma(dev, 3, &vdma3);
	}
	return 0;
}

static void program_capture_engine(struct saa7146_dev *dev, int planar)
{
	struct saa7146_vv *vv = dev->vv_data;
	int count = 0;

	unsigned long e_wait = vv->current_hps_sync == SAA7146_HPS_SYNC_PORT_A ? CMD_E_FID_A : CMD_E_FID_B;
	unsigned long o_wait = vv->current_hps_sync == SAA7146_HPS_SYNC_PORT_A ? CMD_O_FID_A : CMD_O_FID_B;

	/* wait for o_fid_a/b / e_fid_a/b toggle only if rps register 0 is not set*/
	WRITE_RPS0(CMD_PAUSE | CMD_OAN | CMD_SIG0 | o_wait);
	WRITE_RPS0(CMD_PAUSE | CMD_OAN | CMD_SIG0 | e_wait);

	/* set rps register 0 */
	WRITE_RPS0(CMD_WR_REG | (1 << 8) | (MC2/4));
	WRITE_RPS0(MASK_27 | MASK_11);

	/* turn on video-dma1 */
	WRITE_RPS0(CMD_WR_REG_MASK | (MC1/4));
	WRITE_RPS0(MASK_06 | MASK_22);			/* => mask */
	WRITE_RPS0(MASK_06 | MASK_22);			/* => values */
	if( 0 != planar ) {
		/* turn on video-dma2 */
		WRITE_RPS0(CMD_WR_REG_MASK | (MC1/4));
		WRITE_RPS0(MASK_05 | MASK_21);			/* => mask */
		WRITE_RPS0(MASK_05 | MASK_21);			/* => values */

		/* turn on video-dma3 */
		WRITE_RPS0(CMD_WR_REG_MASK | (MC1/4));
		WRITE_RPS0(MASK_04 | MASK_20);			/* => mask */
		WRITE_RPS0(MASK_04 | MASK_20);			/* => values */
	}

	/* wait for o_fid_a/b / e_fid_a/b toggle */
	if ( vv->last_field == V4L2_FIELD_INTERLACED ) {
		WRITE_RPS0(CMD_PAUSE | o_wait);
		WRITE_RPS0(CMD_PAUSE | e_wait);
	} else if ( vv->last_field == V4L2_FIELD_TOP ) {
		WRITE_RPS0(CMD_PAUSE | (vv->current_hps_sync == SAA7146_HPS_SYNC_PORT_A ? MASK_10 : MASK_09));
		WRITE_RPS0(CMD_PAUSE | o_wait);
	} else if ( vv->last_field == V4L2_FIELD_BOTTOM ) {
		WRITE_RPS0(CMD_PAUSE | (vv->current_hps_sync == SAA7146_HPS_SYNC_PORT_A ? MASK_10 : MASK_09));
		WRITE_RPS0(CMD_PAUSE | e_wait);
	}

	/* turn off video-dma1 */
	WRITE_RPS0(CMD_WR_REG_MASK | (MC1/4));
	WRITE_RPS0(MASK_22 | MASK_06);			/* => mask */
	WRITE_RPS0(MASK_22);				/* => values */
	if( 0 != planar ) {
		/* turn off video-dma2 */
		WRITE_RPS0(CMD_WR_REG_MASK | (MC1/4));
		WRITE_RPS0(MASK_05 | MASK_21);			/* => mask */
		WRITE_RPS0(MASK_21);				/* => values */

		/* turn off video-dma3 */
		WRITE_RPS0(CMD_WR_REG_MASK | (MC1/4));
		WRITE_RPS0(MASK_04 | MASK_20);			/* => mask */
		WRITE_RPS0(MASK_20);				/* => values */
	}

	/* generate interrupt */
	WRITE_RPS0(CMD_INTERRUPT);

	/* stop */
	WRITE_RPS0(CMD_STOP);
}

void saa7146_set_capture(struct saa7146_dev *dev, struct saa7146_buf *buf, struct saa7146_buf *next)
{
	struct saa7146_format *sfmt = format_by_fourcc(dev,buf->fmt->pixelformat);
	struct saa7146_vv *vv = dev->vv_data;
	u32 vdma1_prot_addr;

	DEB_CAP(("buf:%p, next:%p\n",buf,next));

	vdma1_prot_addr = saa7146_read(dev, PROT_ADDR1);
	if( 0 == vdma1_prot_addr ) {
		/* clear out beginning of streaming bit (rps register 0)*/
		DEB_CAP(("forcing sync to new frame\n"));
		saa7146_write(dev, MC2, MASK_27 );
	}

	saa7146_set_window(dev, buf->fmt->width, buf->fmt->height, buf->fmt->field);
	saa7146_set_output_format(dev, sfmt->trans);
	saa7146_disable_clipping(dev);

	if ( vv->last_field == V4L2_FIELD_INTERLACED ) {
	} else if ( vv->last_field == V4L2_FIELD_TOP ) {
		vv->last_field = V4L2_FIELD_BOTTOM;
	} else if ( vv->last_field == V4L2_FIELD_BOTTOM ) {
		vv->last_field = V4L2_FIELD_TOP;
	}

	if( 0 != IS_PLANAR(sfmt->trans)) {
		calculate_video_dma_grab_planar(dev, buf);
		program_capture_engine(dev,1);
	} else {
		calculate_video_dma_grab_packed(dev, buf);
		program_capture_engine(dev,0);
	}

/*
	printk("vdma%d.base_even:     0x%08x\n", 1,saa7146_read(dev,BASE_EVEN1));
	printk("vdma%d.base_odd:      0x%08x\n", 1,saa7146_read(dev,BASE_ODD1));
	printk("vdma%d.prot_addr:     0x%08x\n", 1,saa7146_read(dev,PROT_ADDR1));
	printk("vdma%d.base_page:     0x%08x\n", 1,saa7146_read(dev,BASE_PAGE1));
	printk("vdma%d.pitch:         0x%08x\n", 1,saa7146_read(dev,PITCH1));
	printk("vdma%d.num_line_byte: 0x%08x\n", 1,saa7146_read(dev,NUM_LINE_BYTE1));
	printk("vdma%d => vptr      : 0x%08x\n", 1,saa7146_read(dev,PCI_VDP1));
*/

	/* write the address of the rps-program */
	saa7146_write(dev, RPS_ADDR0, dev->d_rps0.dma_handle);

	/* turn on rps */
	saa7146_write(dev, MC1, (MASK_12 | MASK_28));
}
