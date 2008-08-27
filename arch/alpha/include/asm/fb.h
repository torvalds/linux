#ifndef _ASM_FB_H_
#define _ASM_FB_H_
#include <linux/device.h>

/* Caching is off in the I/O space quadrant by design.  */
#define fb_pgprotect(...) do {} while (0)

static inline int fb_is_primary_device(struct fb_info *info)
{
	return 0;
}

#endif /* _ASM_FB_H_ */
