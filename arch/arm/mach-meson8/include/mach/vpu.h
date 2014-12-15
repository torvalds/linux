#ifndef __VPU_H__
#define __VPU_H__

//#define CONFIG_VPU_DYNAMIC_ADJ

#define VPU_MOD_START	100
typedef enum {
    VPU_VIU_OSD1 = VPU_MOD_START,
    VPU_VIU_OSD2,
	VPU_VIU_VD1,
	VPU_VIU_VD2,
	VPU_VIU_CHROMA,
	VPU_VIU_OFIFO,
	VPU_VIU_SCALE,
	VPU_VIU_OSD_SCALE,
	VPU_VIU_VDIN0,
	VPU_VIU_VDIN1,
	VPU_PIC_ROT1,
	VPU_PIC_ROT2,
	VPU_PIC_ROT3,
	VPU_DI_PRE,
	VPU_DI_POST,
	VPU_VIU2_OSD1,
    VPU_VIU2_OSD2,
	VPU_VIU2_VD1,
	VPU_VIU2_CHROMA,
	VPU_VIU2_OFIFO,
	VPU_VIU2_SCALE,
	VPU_VIU2_OSD_SCALE,
	VPU_VENCP,
	VPU_VENCL,
	VPU_VENCI,
	VPU_ISP,
	VPU_MAX,
} vpu_mod_t;

//VPU memory power down
#define VPU_MEM_POWER_ON		0
#define VPU_MEM_POWER_DOWN		1

extern unsigned int get_vpu_clk(void);
extern unsigned int get_vpu_clk_vmod(unsigned int vmod);
extern int request_vpu_clk_vmod(unsigned int vclk, unsigned int vmod);
extern int release_vpu_clk_vmod(unsigned int vmod);

extern void switch_vpu_mem_pd_vmod(unsigned int vmod, int flag);
extern int get_vpu_mem_pd_vmod(unsigned int vmod);
#endif
