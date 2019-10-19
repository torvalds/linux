/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/io.h>
#include <linux/console.h>

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vram_mm_helper.h>

/* ---------------------------------------------------------------------- */

#define VBE_DISPI_IOPORT_INDEX           0x01CE
#define VBE_DISPI_IOPORT_DATA            0x01CF

#define VBE_DISPI_INDEX_ID               0x0
#define VBE_DISPI_INDEX_XRES             0x1
#define VBE_DISPI_INDEX_YRES             0x2
#define VBE_DISPI_INDEX_BPP              0x3
#define VBE_DISPI_INDEX_ENABLE           0x4
#define VBE_DISPI_INDEX_BANK             0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH       0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT      0x7
#define VBE_DISPI_INDEX_X_OFFSET         0x8
#define VBE_DISPI_INDEX_Y_OFFSET         0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa

#define VBE_DISPI_ID0                    0xB0C0
#define VBE_DISPI_ID1                    0xB0C1
#define VBE_DISPI_ID2                    0xB0C2
#define VBE_DISPI_ID3                    0xB0C3
#define VBE_DISPI_ID4                    0xB0C4
#define VBE_DISPI_ID5                    0xB0C5

#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_GETCAPS                0x02
#define VBE_DISPI_8BIT_DAC               0x20
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80

/* ---------------------------------------------------------------------- */

enum bochs_types {
	BOCHS_QEMU_STDVGA,
	BOCHS_UNKNOWN,
};

struct bochs_device {
	/* hw */
	void __iomem   *mmio;
	int            ioports;
	void __iomem   *fb_map;
	unsigned long  fb_base;
	unsigned long  fb_size;
	unsigned long  qext_size;

	/* mode */
	u16 xres;
	u16 yres;
	u16 yres_virtual;
	u32 stride;
	u32 bpp;
	struct edid *edid;

	/* drm */
	struct drm_device *dev;
	struct drm_simple_display_pipe pipe;
	struct drm_connector connector;
};

/* ---------------------------------------------------------------------- */

/* bochs_hw.c */
int bochs_hw_init(struct drm_device *dev);
void bochs_hw_fini(struct drm_device *dev);

void bochs_hw_setmode(struct bochs_device *bochs,
		      struct drm_display_mode *mode);
void bochs_hw_setformat(struct bochs_device *bochs,
			const struct drm_format_info *format);
void bochs_hw_setbase(struct bochs_device *bochs,
		      int x, int y, int stride, u64 addr);
int bochs_hw_load_edid(struct bochs_device *bochs);

/* bochs_mm.c */
int bochs_mm_init(struct bochs_device *bochs);
void bochs_mm_fini(struct bochs_device *bochs);

/* bochs_kms.c */
int bochs_kms_init(struct bochs_device *bochs);
void bochs_kms_fini(struct bochs_device *bochs);

/* bochs_fbdev.c */
extern const struct drm_mode_config_funcs bochs_mode_funcs;
