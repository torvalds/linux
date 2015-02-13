#ifndef __USBDEV_RKUOC_H
#define _USBDEV_RKUOC_H

typedef union uoc_field {
	u32 array[3];
	struct {
		u32 offset;
		u32 bitmap;
		u32 mask;
	} b;
} uoc_field_t;

static inline void grf_uoc_set(void *base, u32 offset, u8 bitmap, u8 mask,
			       u32 value)
{
	/* printk("bc_debug:set addr %p val = 0x%08x\n",
	 *	  ((u32*)(base + offset)),(((((1 << mask) - 1) & value)
	 *	  | (((1 << mask) - 1) << 16))<< bitmap));*/
	*((u32 *) (base + offset)) =
	    (((((1 << mask) - 1) & value) | (((1 << mask) -
					      1) << 16)) << bitmap);
}

static inline u32 grf_uoc_get(void *base, u32 offset, u32 bitmap, u32 mask)
{
	u32 ret;
	/* printk("bc_debug:get addr %p bit %d val = 0x%08x\n",
	 *	  (u32*)(base + offset), bitmap,
	 *	  *((u32*)(base + offset))); */
	ret = (*((u32 *) (base + offset)) >> bitmap) & ((1 << mask) - 1);
	return ret;
}

static inline void regmap_grf_uoc_set(struct regmap *grf, u32 offset,
				      u32 bitmap, u32 mask, u32 val)
{
	unsigned int reg_val;

	reg_val = (((((1 << mask) - 1) & val) |
		   (((1 << mask) - 1) << 16)) << bitmap);
	regmap_write(grf, offset, reg_val);
}

static inline u32 regmap_grf_uoc_get(struct regmap *grf, u32 offset,
				     u32 bitmap, u32 mask)
{
	unsigned int ret;

	regmap_read(grf, offset, &ret);
	ret = (ret >> bitmap) & ((1 << mask) - 1);
	return ret;
}

static inline bool uoc_field_valid(uoc_field_t *f)
{
	if ((f->b.bitmap < 32) && (f->b.mask < 32))
		return true;
	else {
		printk("%s field invalid\n", __func__);
		return false;
	}
}

#endif
