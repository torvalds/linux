#ifndef LYNXDRV_H_
#define LYNXDRV_H_



#define FB_ACCEL_SMI 0xab

#define MHZ(x) ((x) * 1000000)

struct lynx_accel {
	/* base virtual address of DPR registers */
	volatile unsigned char __iomem *dprBase;
	/* base virtual address of de data port */
	volatile unsigned char __iomem *dpPortBase;

	/* function fointers */
	void (*de_init)(struct lynx_accel *);

	int (*de_wait)(void);/* see if hardware ready to work */

	int (*de_fillrect)(struct lynx_accel *, u32, u32, u32, u32,
						u32, u32, u32, u32, u32);

	int (*de_copyarea)(struct lynx_accel *, u32, u32, u32, u32,
						u32, u32, u32, u32,
						u32, u32, u32, u32);

	int (*de_imageblit)(struct lynx_accel *, const char *, u32, u32, u32, u32,
							       u32, u32, u32, u32,
							       u32, u32, u32, u32);

};

/* lynx_share stands for a presentation of two frame buffer
   that use one smi adaptor , it is similar to a basic class of C++
*/
struct lynx_share {
	/* common members */
	u16 devid;
	u8 revid;
	struct pci_dev *pdev;
	struct fb_info *fbinfo[2];
	struct lynx_accel accel;
	int accel_off;
	int dual;
		int mtrr_off;
		struct{
			int vram;
		} mtrr;
	/* all smi graphic adaptor got below attributes */
	unsigned long vidmem_start;
	unsigned long vidreg_start;
	__u32 vidmem_size;
	__u32 vidreg_size;
	void __iomem *pvReg;
	unsigned char __iomem *pvMem;
	/* locks*/
	spinlock_t slock;
};

struct lynx_cursor {
	/* cursor width ,height and size */
	int w;
	int h;
	int size;
	/* hardware limitation */
	int maxW;
	int maxH;
	/* base virtual address and offset  of cursor image */
	char __iomem *vstart;
	int offset;
	/* mmio addr of hw cursor */
	volatile char __iomem *mmio;
	/* the lynx_share of this adaptor */
	struct lynx_share *share;
};

struct lynxfb_crtc {
	unsigned char __iomem *vCursor; /* virtual address of cursor */
	unsigned char __iomem *vScreen; /* virtual address of on_screen */
	int oCursor; /* cursor address offset in vidmem */
	int oScreen; /* onscreen address offset in vidmem */
	int channel;/* which channel this crtc stands for*/
	resource_size_t vidmem_size;/* this view's video memory max size */

	/* below attributes belong to info->fix, their value depends on specific adaptor*/
	u16 line_pad;/* padding information:0,1,2,4,8,16,... */
	u16 xpanstep;
	u16 ypanstep;
	u16 ywrapstep;

	void *priv;

	/* cursor information */
	struct lynx_cursor cursor;
};

struct lynxfb_output {
	int dpms;
	int paths;
	/* which paths(s) this output stands for,for sm750:
	   paths=1:means output for panel paths
	   paths=2:means output for crt paths
	   paths=3:means output for both panel and crt paths
	*/

	int *channel;
	/* which channel these outputs linked with,for sm750:
	   *channel=0 means primary channel
	   *channel=1 means secondary channel
	   output->channel ==> &crtc->channel
	*/
	void *priv;

	int (*proc_setBLANK)(struct lynxfb_output*, int);
};

struct lynxfb_par {
	/* either 0 or 1 for dual head adaptor,0 is the older one registered */
	int index;
	unsigned int pseudo_palette[256];
	struct lynxfb_crtc crtc;
	struct lynxfb_output output;
	struct fb_info *info;
	struct lynx_share *share;
};

static inline unsigned long ps_to_hz(unsigned int psvalue)
{
	unsigned long long numerator = 1000*1000*1000*1000ULL;
	/* 10^12 / picosecond period gives frequency in Hz */
	do_div(numerator, psvalue);
	return (unsigned long)numerator;
}


#endif
