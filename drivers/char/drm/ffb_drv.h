/* $Id: ffb_drv.h,v 1.1 2000/06/01 04:24:39 davem Exp $
 * ffb_drv.h: Creator/Creator3D direct rendering driver.
 *
 * Copyright (C) 2000 David S. Miller (davem@redhat.com)
 */

/* Auxilliary clips. */
typedef struct  {
	volatile unsigned int min;
	volatile unsigned int max;
} ffb_auxclip, *ffb_auxclipPtr;

/* FFB register set. */
typedef struct _ffb_fbc {
	/* Next vertex registers, on the right we list which drawops
	 * use said register and the logical name the register has in
	 * that context.
	 */					/* DESCRIPTION		DRAWOP(NAME)	*/
/*0x00*/unsigned int		pad1[3];	/* Reserved				*/
/*0x0c*/volatile unsigned int	alpha;		/* ALPHA Transparency			*/
/*0x10*/volatile unsigned int	red;		/* RED					*/
/*0x14*/volatile unsigned int	green;		/* GREEN				*/
/*0x18*/volatile unsigned int	blue;		/* BLUE					*/
/*0x1c*/volatile unsigned int	z;		/* DEPTH				*/
/*0x20*/volatile unsigned int	y;		/* Y			triangle(DOYF)	*/
						/*                      aadot(DYF)	*/
						/*                      ddline(DYF)	*/
						/*                      aaline(DYF)	*/
/*0x24*/volatile unsigned int	x;		/* X			triangle(DOXF)	*/
						/*                      aadot(DXF)	*/
						/*                      ddline(DXF)	*/
						/*                      aaline(DXF)	*/
/*0x28*/unsigned int		pad2[2];	/* Reserved				*/
/*0x30*/volatile unsigned int	ryf;		/* Y (alias to DOYF)	ddline(RYF)	*/
						/*			aaline(RYF)	*/
						/*			triangle(RYF)	*/
/*0x34*/volatile unsigned int	rxf;		/* X			ddline(RXF)	*/
						/*			aaline(RXF)	*/
						/*			triangle(RXF)	*/
/*0x38*/unsigned int		pad3[2];	/* Reserved				*/
/*0x40*/volatile unsigned int	dmyf;		/* Y (alias to DOYF)	triangle(DMYF)	*/
/*0x44*/volatile unsigned int	dmxf;		/* X			triangle(DMXF)	*/
/*0x48*/unsigned int		pad4[2];	/* Reserved				*/
/*0x50*/volatile unsigned int	ebyi;		/* Y (alias to RYI)	polygon(EBYI)	*/
/*0x54*/volatile unsigned int	ebxi;		/* X			polygon(EBXI)	*/
/*0x58*/unsigned int		pad5[2];	/* Reserved				*/
/*0x60*/volatile unsigned int	by;		/* Y			brline(RYI)	*/
						/*			fastfill(OP)	*/
						/*			polygon(YI)	*/
						/*			rectangle(YI)	*/
						/*			bcopy(SRCY)	*/
						/*			vscroll(SRCY)	*/
/*0x64*/volatile unsigned int	bx;		/* X			brline(RXI)	*/
						/*			polygon(XI)	*/
						/*			rectangle(XI)	*/
						/*			bcopy(SRCX)	*/
						/*			vscroll(SRCX)	*/
						/*			fastfill(GO)	*/
/*0x68*/volatile unsigned int	dy;		/* destination Y	fastfill(DSTY)	*/
						/*			bcopy(DSRY)	*/
						/*			vscroll(DSRY)	*/
/*0x6c*/volatile unsigned int	dx;		/* destination X	fastfill(DSTX)	*/
						/*			bcopy(DSTX)	*/
						/*			vscroll(DSTX)	*/
/*0x70*/volatile unsigned int	bh;		/* Y (alias to RYI)	brline(DYI)	*/
						/*			dot(DYI)	*/
						/*			polygon(ETYI)	*/
						/* Height		fastfill(H)	*/
						/*			bcopy(H)	*/
						/*			vscroll(H)	*/
						/* Y count		fastfill(NY)	*/
/*0x74*/volatile unsigned int	bw;		/* X			dot(DXI)	*/
						/*			brline(DXI)	*/
						/*			polygon(ETXI)	*/
						/*			fastfill(W)	*/
						/*			bcopy(W)	*/
						/*			vscroll(W)	*/
						/*			fastfill(NX)	*/
/*0x78*/unsigned int		pad6[2];	/* Reserved				*/
/*0x80*/unsigned int		pad7[32];	/* Reserved				*/
	
	/* Setup Unit's vertex state register */
/*100*/	volatile unsigned int	suvtx;
/*104*/	unsigned int		pad8[63];	/* Reserved				*/
	
	/* Frame Buffer Control Registers */
/*200*/	volatile unsigned int	ppc;		/* Pixel Processor Control		*/
/*204*/	volatile unsigned int	wid;		/* Current WID				*/
/*208*/	volatile unsigned int	fg;		/* FG data				*/
/*20c*/	volatile unsigned int	bg;		/* BG data				*/
/*210*/	volatile unsigned int	consty;		/* Constant Y				*/
/*214*/	volatile unsigned int	constz;		/* Constant Z				*/
/*218*/	volatile unsigned int	xclip;		/* X Clip				*/
/*21c*/	volatile unsigned int	dcss;		/* Depth Cue Scale Slope		*/
/*220*/	volatile unsigned int	vclipmin;	/* Viewclip XY Min Bounds		*/
/*224*/	volatile unsigned int	vclipmax;	/* Viewclip XY Max Bounds		*/
/*228*/	volatile unsigned int	vclipzmin;	/* Viewclip Z Min Bounds		*/
/*22c*/	volatile unsigned int	vclipzmax;	/* Viewclip Z Max Bounds		*/
/*230*/	volatile unsigned int	dcsf;		/* Depth Cue Scale Front Bound		*/
/*234*/	volatile unsigned int	dcsb;		/* Depth Cue Scale Back Bound		*/
/*238*/	volatile unsigned int	dczf;		/* Depth Cue Z Front			*/
/*23c*/	volatile unsigned int	dczb;		/* Depth Cue Z Back			*/
/*240*/	unsigned int		pad9;		/* Reserved				*/
/*244*/	volatile unsigned int	blendc;		/* Alpha Blend Control			*/
/*248*/	volatile unsigned int	blendc1;	/* Alpha Blend Color 1			*/
/*24c*/	volatile unsigned int	blendc2;	/* Alpha Blend Color 2			*/
/*250*/	volatile unsigned int	fbramitc;	/* FB RAM Interleave Test Control	*/
/*254*/	volatile unsigned int	fbc;		/* Frame Buffer Control			*/
/*258*/	volatile unsigned int	rop;		/* Raster OPeration			*/
/*25c*/	volatile unsigned int	cmp;		/* Frame Buffer Compare			*/
/*260*/	volatile unsigned int	matchab;	/* Buffer AB Match Mask			*/
/*264*/	volatile unsigned int	matchc;		/* Buffer C(YZ) Match Mask		*/
/*268*/	volatile unsigned int	magnab;		/* Buffer AB Magnitude Mask		*/
/*26c*/	volatile unsigned int	magnc;		/* Buffer C(YZ) Magnitude Mask		*/
/*270*/	volatile unsigned int	fbcfg0;		/* Frame Buffer Config 0		*/
/*274*/	volatile unsigned int	fbcfg1;		/* Frame Buffer Config 1		*/
/*278*/	volatile unsigned int	fbcfg2;		/* Frame Buffer Config 2		*/
/*27c*/	volatile unsigned int	fbcfg3;		/* Frame Buffer Config 3		*/
/*280*/	volatile unsigned int	ppcfg;		/* Pixel Processor Config		*/
/*284*/	volatile unsigned int	pick;		/* Picking Control			*/
/*288*/	volatile unsigned int	fillmode;	/* FillMode				*/
/*28c*/	volatile unsigned int	fbramwac;	/* FB RAM Write Address Control		*/
/*290*/	volatile unsigned int	pmask;		/* RGB PlaneMask			*/
/*294*/	volatile unsigned int	xpmask;		/* X PlaneMask				*/
/*298*/	volatile unsigned int	ypmask;		/* Y PlaneMask				*/
/*29c*/	volatile unsigned int	zpmask;		/* Z PlaneMask				*/
/*2a0*/	ffb_auxclip		auxclip[4]; 	/* Auxilliary Viewport Clip		*/
	
	/* New 3dRAM III support regs */
/*2c0*/	volatile unsigned int	rawblend2;
/*2c4*/	volatile unsigned int	rawpreblend;
/*2c8*/	volatile unsigned int	rawstencil;
/*2cc*/	volatile unsigned int	rawstencilctl;
/*2d0*/	volatile unsigned int	threedram1;
/*2d4*/	volatile unsigned int	threedram2;
/*2d8*/	volatile unsigned int	passin;
/*2dc*/	volatile unsigned int	rawclrdepth;
/*2e0*/	volatile unsigned int	rawpmask;
/*2e4*/	volatile unsigned int	rawcsrc;
/*2e8*/	volatile unsigned int	rawmatch;
/*2ec*/	volatile unsigned int	rawmagn;
/*2f0*/	volatile unsigned int	rawropblend;
/*2f4*/	volatile unsigned int	rawcmp;
/*2f8*/	volatile unsigned int	rawwac;
/*2fc*/	volatile unsigned int	fbramid;
	
/*300*/	volatile unsigned int	drawop;		/* Draw OPeration			*/
/*304*/	unsigned int		pad10[2];	/* Reserved				*/
/*30c*/	volatile unsigned int	lpat;		/* Line Pattern control			*/
/*310*/	unsigned int		pad11;		/* Reserved				*/
/*314*/	volatile unsigned int	fontxy;		/* XY Font coordinate			*/
/*318*/	volatile unsigned int	fontw;		/* Font Width				*/
/*31c*/	volatile unsigned int	fontinc;	/* Font Increment			*/
/*320*/	volatile unsigned int	font;		/* Font bits				*/
/*324*/	unsigned int		pad12[3];	/* Reserved				*/
/*330*/	volatile unsigned int	blend2;
/*334*/	volatile unsigned int	preblend;
/*338*/	volatile unsigned int	stencil;
/*33c*/	volatile unsigned int	stencilctl;

/*340*/	unsigned int		pad13[4];	/* Reserved				*/
/*350*/	volatile unsigned int	dcss1;		/* Depth Cue Scale Slope 1		*/
/*354*/	volatile unsigned int	dcss2;		/* Depth Cue Scale Slope 2		*/
/*358*/	volatile unsigned int	dcss3;		/* Depth Cue Scale Slope 3		*/
/*35c*/	volatile unsigned int	widpmask;
/*360*/	volatile unsigned int	dcs2;
/*364*/	volatile unsigned int	dcs3;
/*368*/	volatile unsigned int	dcs4;
/*36c*/	unsigned int		pad14;		/* Reserved				*/
/*370*/	volatile unsigned int	dcd2;
/*374*/	volatile unsigned int	dcd3;
/*378*/	volatile unsigned int	dcd4;
/*37c*/	unsigned int		pad15;		/* Reserved				*/
/*380*/	volatile unsigned int	pattern[32];	/* area Pattern				*/
/*400*/	unsigned int		pad16[8];	/* Reserved				*/
/*420*/	volatile unsigned int	reset;		/* chip RESET				*/
/*424*/	unsigned int		pad17[247];	/* Reserved				*/
/*800*/	volatile unsigned int	devid;		/* Device ID				*/
/*804*/	unsigned int		pad18[63];	/* Reserved				*/
/*900*/	volatile unsigned int	ucsr;		/* User Control & Status Register	*/
/*904*/	unsigned int		pad19[31];	/* Reserved				*/
/*980*/	volatile unsigned int	mer;		/* Mode Enable Register			*/
/*984*/	unsigned int		pad20[1439];	/* Reserved				*/
} ffb_fbc, *ffb_fbcPtr;

struct ffb_hw_context {
	int is_2d_only;

	unsigned int ppc;
	unsigned int wid;
	unsigned int fg;
	unsigned int bg;
	unsigned int consty;
	unsigned int constz;
	unsigned int xclip;
	unsigned int dcss;
	unsigned int vclipmin;
	unsigned int vclipmax;
	unsigned int vclipzmin;
	unsigned int vclipzmax;
	unsigned int dcsf;
	unsigned int dcsb;
	unsigned int dczf;
	unsigned int dczb;
	unsigned int blendc;
	unsigned int blendc1;
	unsigned int blendc2;
	unsigned int fbc;
	unsigned int rop;
	unsigned int cmp;
	unsigned int matchab;
	unsigned int matchc;
	unsigned int magnab;
	unsigned int magnc;
	unsigned int pmask;
	unsigned int xpmask;
	unsigned int ypmask;
	unsigned int zpmask;
	unsigned int auxclip0min;
	unsigned int auxclip0max;
	unsigned int auxclip1min;
	unsigned int auxclip1max;
	unsigned int auxclip2min;
	unsigned int auxclip2max;
	unsigned int auxclip3min;
	unsigned int auxclip3max;
	unsigned int drawop;
	unsigned int lpat;
	unsigned int fontxy;
	unsigned int fontw;
	unsigned int fontinc;
	unsigned int area_pattern[32];
	unsigned int ucsr;
	unsigned int stencil;
	unsigned int stencilctl;
	unsigned int dcss1;
	unsigned int dcss2;
	unsigned int dcss3;
	unsigned int dcs2;
	unsigned int dcs3;
	unsigned int dcs4;
	unsigned int dcd2;
	unsigned int dcd3;
	unsigned int dcd4;
	unsigned int mer;
};

#define FFB_MAX_CTXS	32

enum ffb_chip_type {
	ffb1_prototype = 0,	/* Early pre-FCS FFB */
	ffb1_standard,		/* First FCS FFB, 100Mhz UPA, 66MHz gclk */
	ffb1_speedsort,		/* Second FCS FFB, 100Mhz UPA, 75MHz gclk */
	ffb2_prototype,		/* Early pre-FCS vertical FFB2 */
	ffb2_vertical,		/* First FCS FFB2/vertical, 100Mhz UPA, 100MHZ gclk,
				   75(SingleBuffer)/83(DoubleBuffer) MHz fclk */
	ffb2_vertical_plus,	/* Second FCS FFB2/vertical, same timings */
	ffb2_horizontal,	/* First FCS FFB2/horizontal, same timings as FFB2/vert */
	ffb2_horizontal_plus,	/* Second FCS FFB2/horizontal, same timings */
	afb_m3,			/* FCS Elite3D, 3 float chips */
	afb_m6			/* FCS Elite3D, 6 float chips */
};

typedef struct ffb_dev_priv {
	/* Misc software state. */
	int			prom_node;
	enum ffb_chip_type	ffb_type;
	u64			card_phys_base;
	struct miscdevice 	miscdev;

	/* Controller registers. */
	ffb_fbcPtr		regs;

	/* Context table. */
	struct ffb_hw_context	*hw_state[FFB_MAX_CTXS];
} ffb_dev_priv_t;

extern unsigned long ffb_get_unmapped_area(struct file *filp,
					   unsigned long hint,
					   unsigned long len,
					   unsigned long pgoff,
					   unsigned long flags);
extern void ffb_set_context_ioctls(void);
extern drm_ioctl_desc_t DRM(ioctls)[];

extern int ffb_driver_context_switch(drm_device_t *dev, int old, int new);
