#include <drm/drmP.h>
#include <drm/drm_mode.h>
#include "nouveau_drm.h"
#include "nouveau_reg.h"
#include "nouveau_crtc.h"
#include "hw.h"

static void
nv04_cursor_show(struct nouveau_crtc *nv_crtc, bool update)
{
	nv_show_cursor(nv_crtc->base.dev, nv_crtc->index, true);
}

static void
nv04_cursor_hide(struct nouveau_crtc *nv_crtc, bool update)
{
	nv_show_cursor(nv_crtc->base.dev, nv_crtc->index, false);
}

static void
nv04_cursor_set_pos(struct nouveau_crtc *nv_crtc, int x, int y)
{
	nv_crtc->cursor_saved_x = x; nv_crtc->cursor_saved_y = y;
	NVWriteRAMDAC(nv_crtc->base.dev, nv_crtc->index,
		      NV_PRAMDAC_CU_START_POS,
		      XLATE(y, 0, NV_PRAMDAC_CU_START_POS_Y) |
		      XLATE(x, 0, NV_PRAMDAC_CU_START_POS_X));
}

static void
crtc_wr_cio_state(struct drm_crtc *crtc, struct nv04_crtc_reg *crtcstate, int index)
{
	NVWriteVgaCrtc(crtc->dev, nouveau_crtc(crtc)->index, index,
		       crtcstate->CRTC[index]);
}

static void
nv04_cursor_set_offset(struct nouveau_crtc *nv_crtc, uint32_t offset)
{
	struct drm_device *dev = nv_crtc->base.dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nv04_crtc_reg *regp = &nv04_display(dev)->mode_reg.crtc_reg[nv_crtc->index];
	struct drm_crtc *crtc = &nv_crtc->base;

	regp->CRTC[NV_CIO_CRE_HCUR_ADDR0_INDEX] =
		MASK(NV_CIO_CRE_HCUR_ASI) |
		XLATE(offset, 17, NV_CIO_CRE_HCUR_ADDR0_ADR);
	regp->CRTC[NV_CIO_CRE_HCUR_ADDR1_INDEX] =
		XLATE(offset, 11, NV_CIO_CRE_HCUR_ADDR1_ADR);
	if (crtc->mode.flags & DRM_MODE_FLAG_DBLSCAN)
		regp->CRTC[NV_CIO_CRE_HCUR_ADDR1_INDEX] |=
			MASK(NV_CIO_CRE_HCUR_ADDR1_CUR_DBL);
	regp->CRTC[NV_CIO_CRE_HCUR_ADDR2_INDEX] = offset >> 24;

	crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_HCUR_ADDR0_INDEX);
	crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_HCUR_ADDR1_INDEX);
	crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_HCUR_ADDR2_INDEX);
	if (drm->device.info.family == NV_DEVICE_INFO_V0_CURIE)
		nv_fix_nv40_hw_cursor(dev, nv_crtc->index);
}

int
nv04_cursor_init(struct nouveau_crtc *crtc)
{
	crtc->cursor.set_offset = nv04_cursor_set_offset;
	crtc->cursor.set_pos = nv04_cursor_set_pos;
	crtc->cursor.hide = nv04_cursor_hide;
	crtc->cursor.show = nv04_cursor_show;
	return 0;
}
