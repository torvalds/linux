#ifndef LYNX_HW750_H__
#define LYNX_HW750_H__


#define DEFAULT_SM750_CHIP_CLOCK 		290
#define DEFAULT_SM750LE_CHIP_CLOCK  	333
#ifndef SM750LE_REVISION_ID
#define SM750LE_REVISION_ID (unsigned char)0xfe
#endif

//#define DEFAULT_MEM_CLOCK	(DEFAULT_SM750_CHIP_CLOCK/1)
//#define DEFAULT_MASTER_CLOCK	(DEFAULT_SM750_CHIP_CLOCK/3)


enum sm750_pnltype{

	sm750_24TFT = 0,/* 24bit tft */

	sm750_dualTFT = 2,/* dual 18 bit tft */

	sm750_doubleTFT = 1,/* 36 bit double pixel tft */
};

/* vga channel is not concerned  */
enum sm750_dataflow{
	sm750_simul_pri,/* primary => all head */

	sm750_simul_sec,/* secondary => all head */

	sm750_dual_normal,/* 	primary => panel head and secondary => crt */

	sm750_dual_swap,/* 	primary => crt head and secondary => panel */
};


enum sm750_channel{
	sm750_primary = 0,
	/* enum value equal to the register filed data */
	sm750_secondary = 1,
};

enum sm750_path{
	sm750_panel = 1,
	sm750_crt = 2,
	sm750_pnc = 3,/* panel and crt */
};

struct init_status{
	ushort powerMode;
	/* below three clocks are in unit of MHZ*/
	ushort chip_clk;
	ushort mem_clk;
	ushort master_clk;
	ushort setAllEngOff;
	ushort resetMemory;
};

struct sm750_state{
	struct init_status initParm;
	enum sm750_pnltype pnltype;
	enum sm750_dataflow dataflow;
	int nocrt;
	int xLCD;
	int yLCD;
};

/* 	sm750_share stands for a presentation of two frame buffer
	that use one sm750 adaptor, it is similiar to the super class of lynx_share
	in C++
*/

struct sm750_share{
	/* it's better to put lynx_share struct to the first place of sm750_share */
	struct lynx_share share;
	struct sm750_state state;
	int hwCursor;
	/* 	0: no hardware cursor
		1: primary crtc hw cursor enabled,
		2: secondary crtc hw cursor enabled
		3: both ctrc hw cursor enabled
	*/
};

int hw_sm750_map(struct lynx_share* share,struct pci_dev* pdev);
int hw_sm750_inithw(struct lynx_share*,struct pci_dev *);
void hw_sm750_initAccel(struct lynx_share *);
int hw_sm750_deWait(void);
int hw_sm750le_deWait(void);

resource_size_t hw_sm750_getVMSize(struct lynx_share *);
int hw_sm750_output_checkMode(struct lynxfb_output*,struct fb_var_screeninfo*);
int hw_sm750_output_setMode(struct lynxfb_output*,struct fb_var_screeninfo*,struct fb_fix_screeninfo*);
int hw_sm750_crtc_checkMode(struct lynxfb_crtc*,struct fb_var_screeninfo*);
int hw_sm750_crtc_setMode(struct lynxfb_crtc*,struct fb_var_screeninfo*,struct fb_fix_screeninfo*);
int hw_sm750_setColReg(struct lynxfb_crtc*,ushort,ushort,ushort,ushort);
int hw_sm750_setBLANK(struct lynxfb_output*,int);
int hw_sm750le_setBLANK(struct lynxfb_output*,int);
void hw_sm750_crtc_clear(struct lynxfb_crtc*);
void hw_sm750_output_clear(struct lynxfb_output*);
int hw_sm750_pan_display(struct lynxfb_crtc *crtc,
        const struct fb_var_screeninfo *var,
        const struct fb_info *info);

#endif
