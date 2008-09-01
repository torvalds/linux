#ifndef BF54X_LQ043_H
#define BF54X_LQ043_H

struct bfin_bf54xfb_val {
	unsigned int	defval;
	unsigned int	min;
	unsigned int	max;
};

struct bfin_bf54xfb_mach_info {
	unsigned char	fixed_syncs;	/* do not update sync/border */

	/* LCD types */
	int		type;

	/* Screen size */
	int		width;
	int		height;

	/* Screen info */
	struct bfin_bf54xfb_val xres;
	struct bfin_bf54xfb_val yres;
	struct bfin_bf54xfb_val bpp;

	/* GPIOs */
	unsigned short 		disp;

};

#endif /* BF54X_LQ043_H */
