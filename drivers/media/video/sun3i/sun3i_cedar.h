#ifndef __CEDAR_DEV_H__
#define __CEDAR_DEV_H__

enum IOCTL_CMD {
	IOCTL_UNKOWN = 0x100,
	IOCTL_GET_ENV_INFO,
	IOCTL_WAIT_VE,
	IOCTL_RESET_VE,
	IOCTL_ENABLE_VE,
	IOCTL_DISABLE_VE,
	IOCTL_SET_VE_FREQ,

	IOCTL_CONFIG_AVS2 = 0x200,
	IOCTL_GETVALUE_AVS2 ,
	IOCTL_PAUSE_AVS2 ,
	IOCTL_START_AVS2 ,
	IOCTL_RESET_AVS2 ,
	IOCTL_ADJUST_AVS2,
};

typedef struct CEDARV_ENV_INFOMATION{
	unsigned int phymem_start;
	int  phymem_total_size;
	unsigned int  address_macc;
}cedarv_env_info_t;



/*--------------------------------------------------------------------------------*/
#define REGS_pBASE			(0x01C00000)	 	      // register base addr

#define SRAM_REGS_pBASE     (REGS_pBASE + 0x00000)    // SRAM Controller
#define CCMU_REGS_pBASE     (REGS_pBASE + 0x20000)    // clock manager unit
#define MACC_REGS_pBASE     (REGS_pBASE + 0x0E000)    // media accelerate VE
#define SS_REGS_pBASE       (REGS_pBASE + 0x15000)    // Security System
#define TIMER_REGS_pBASE    (REGS_pBASE + 0x20c00)    // Timer

#define SRAM_REGS_BASE      SRAM_REGS_pBASE           // SRAM Controller
#define CCMU_REGS_BASE      CCMU_REGS_pBASE           // Clock Control manager unit
#define MACC_REGS_BASE      MACC_REGS_pBASE           // Media ACCelerate
#define SS_REGS_BASE        SS_REGS_pBASE             // Security System
#define TIMER_REGS_BASE     TIMER_REGS_pBASE          // Timer

#define MPEG_REGS_BASE      (MACC_REGS_BASE + 0x100)  // MPEG engine
#define H264_REGS_BASE      (MACC_REGS_BASE + 0x200)  // H264 engine
#define VC1_REGS_BASE       (MACC_REGS_BASE + 0x300)  // VC-1 engine

#define SRAM_REGS_SIZE      (4096)  // 4K
#define CCMU_REGS_SIZE      (1024)  // 1K
#define MACC_REGS_SIZE      (4096)  // 4K
#define SS_REGS_SIZE        (4096)  // 4K
#define TIMER_REGS_SIZE     (1024)  // 1K
/*--------------------------------------------------------------------------------*/

#define SRAM_REG_o_CFG	    (0x00)
#define SRAM_REG_ADDR_CFG   (SRAM_REGS_BASE + SRAM_REG_o_CFG) // SRAM MAP Cfg Reg 0
/*--------------------------------------------------------------------------------*/

#define AVS_CNT_CTL_REG     (TIMER_REGS_BASE + 0x80) // AVS Counter Control Reg
#define AVS0_CNTVAL_REG     (TIMER_REGS_BASE + 0x84) // AVS0 Counter Value Reg
#define AVS1_CNTVAL_REG     (TIMER_REGS_BASE + 0x88) // AVS1 Counter Value Reg
#define AVS_CNT_DIVISOR_REG (TIMER_REGS_BASE + 0x8c) // AVS Counter Divisor Reg



#endif
