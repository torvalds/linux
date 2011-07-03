#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

static u32
nv04_crystal_freq(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 extdev_boot0 = nv_rd32(dev, 0x101000);
	int type;

	type = !!(extdev_boot0 & 0x00000040);
	if ((dev_priv->chipset >= 0x17 && dev_priv->chipset < 0x20) ||
	    dev_priv->chipset >= 0x25)
		type |= (extdev_boot0 & 0x00400000) ? 2 : 0;

	switch (type) {
	case 0: return 13500000;
	case 1: return 14318180;
	case 2: return 27000000;
	case 3: return 25000000;
	default:
		break;
	}

	return 0;
}

int
nv04_timer_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 m, n, d;

	nv_wr32(dev, NV04_PTIMER_INTR_EN_0, 0x00000000);
	nv_wr32(dev, NV04_PTIMER_INTR_0, 0xFFFFFFFF);

	/* aim for 31.25MHz, which gives us nanosecond timestamps */
	d = 1000000000 / 32;

	/* determine base clock for timer source */
	if (dev_priv->chipset < 0x40) {
		n = dev_priv->engine.pm.clock_get(dev, PLL_CORE);
	} else
	if (dev_priv->chipset == 0x40) {
		/*XXX: figure this out */
		n = 0;
	} else {
		n = nv04_crystal_freq(dev);
		m = 1;
		while (n < (d * 2)) {
			n += (n / m);
			m++;
		}

		nv_wr32(dev, 0x009220, m - 1);
	}

	if (!n) {
		NV_WARN(dev, "PTIMER: unknown input clock freq\n");
		if (!nv_rd32(dev, NV04_PTIMER_NUMERATOR) ||
		    !nv_rd32(dev, NV04_PTIMER_DENOMINATOR)) {
			nv_wr32(dev, NV04_PTIMER_NUMERATOR, 1);
			nv_wr32(dev, NV04_PTIMER_DENOMINATOR, 1);
		}
		return 0;
	}

	/* reduce ratio to acceptable values */
	while (((n % 5) == 0) && ((d % 5) == 0)) {
		n /= 5;
		d /= 5;
	}

	while (((n % 2) == 0) && ((d % 2) == 0)) {
		n /= 2;
		d /= 2;
	}

	while (n > 0xffff || d > 0xffff) {
		n >>= 1;
		d >>= 1;
	}

	nv_wr32(dev, NV04_PTIMER_NUMERATOR, n);
	nv_wr32(dev, NV04_PTIMER_DENOMINATOR, d);
	return 0;
}

u64
nv04_timer_read(struct drm_device *dev)
{
	u32 hi, lo;

	do {
		hi = nv_rd32(dev, NV04_PTIMER_TIME_1);
		lo = nv_rd32(dev, NV04_PTIMER_TIME_0);
	} while (hi != nv_rd32(dev, NV04_PTIMER_TIME_1));

	return ((u64)hi << 32 | lo);
}

void
nv04_timer_takedown(struct drm_device *dev)
{
}
