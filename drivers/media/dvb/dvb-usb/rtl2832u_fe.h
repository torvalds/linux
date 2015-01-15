
#ifndef __RTL2832U_FE_H__
#define __RTL2832U_FE_H__

#include "nim_rtl2832_tua9001.h"
#include "nim_rtl2832_mt2266.h"
#include "nim_rtl2832_fc2580.h"
#include "nim_rtl2832_mxl5007t.h"
#include "nim_rtl2832_fc0012.h"
#include "nim_rtl2832_e4000.h"
#include "nim_rtl2832_mt2063.h"
#include "nim_rtl2832_max3543.h"
#include "nim_rtl2832_tda18272.h"
#include "nim_rtl2832_fc0013.h"
#include "nim_rtl2832_r820t.h"

#include "nim_rtl2836_fc2580.h"
#include "nim_rtl2836_mxl5007t.h"

#include "nim_rtl2840_mt2063.h"
#include "nim_rtl2840_max3543.h"

#include "rtl2832u_io.h"
#include <linux/param.h>
#include "dvb_frontend.h"
#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)) || (defined V4L2_VERSION)
/* all DVB frontend drivers now work directly with the DVBv5
 * structure. This warrants that all drivers will be
 * getting/setting frontend parameters on a consistent way, in
 * order to avoid copying data from/to the DVBv3 structs
 * without need.
 */
#define V4L2_ONLY_DVB_V5
#endif

#define  UPDATE_FUNC_ENABLE_2840      0
#define  UPDATE_FUNC_ENABLE_2836      1
#define  UPDATE_FUNC_ENABLE_2832      1

#define UPDATE_PROCEDURE_PERIOD_2836	       (HZ/5) //200ms = jiffies*1000/HZ
#define UPDATE_PROCEDURE_PERIOD_2832       (HZ/5)  //200ms

typedef enum{
      RTL2832_TUNER_TYPE_UNKNOWN = 0,
	RTL2832_TUNER_TYPE_MT2266,		
	RTL2832_TUNER_TYPE_FC2580,	
	RTL2832_TUNER_TYPE_TUA9001,	
	RTL2832_TUNER_TYPE_MXL5007T,
	RTL2832_TUNER_TYPE_E4000,	
	RTL2832_TUNER_TYPE_FC0012,
	RTL2832_TUNER_TYPE_FC0013,
	RTL2832_TUNER_TYPE_MT2063,
	RTL2832_TUNER_TYPE_MAX3543,
	RTL2832_TUNER_TYPE_TDA18272,	
	RTL2832_TUNER_TYPE_R820T,		
}RTL2832_TUNER_TYPE;

typedef enum{
	RTK_UNKNOWN = 0,
	RTK_VIDEO,
	RTK_AUDIO,
}RTL2832_WORK_TYPE;

//3  state of total device 
struct rtl2832_state {
	struct dvb_frontend			frontend;
#ifndef V4L2_ONLY_DVB_V5
	struct dvb_frontend_parameters	fep;
#endif	
	struct dvb_usb_device*		d;

	struct mutex					i2c_repeater_mutex;

       unsigned long					current_frequency;
#ifdef V4L2_ONLY_DVB_V5
	unsigned long			current_bandwidth_hz;
#else
	enum fe_bandwidth			current_bandwidth;
#endif	
	   
	RTL2832_TUNER_TYPE			tuner_type;
	unsigned char					is_mt2266_nim_module_built;  //3 For close MT handle
	unsigned char					is_mt2063_nim_module_built;  //3 For close MT handle


	//3 DTMB related begin ---
	unsigned int                                demod_support_type;
	unsigned int                                demod_type;
	unsigned int                                demod_ask_type;
	//3 DTMB related end end ---


	//3 DVBC related begin ---
	unsigned char					b_rtl2840_power_onoff_once;
	
	//3 DVBC related end end ---
	

	//3if init() is called, is_initial is true ->check it to see if need to flush work queue 
	unsigned short				is_initial;
	unsigned char                             is_frequency_valid;

	unsigned char                             rtl2832_audio_video_mode;


#if  UPDATE_FUNC_ENABLE_2840
	struct delayed_work                  update2840_procedure_work;
#endif

#if  UPDATE_FUNC_ENABLE_2836
	struct delayed_work                  update2836_procedure_work;
#endif

#if  UPDATE_FUNC_ENABLE_2832
       struct delayed_work			update2832_procedure_work;
#endif

	DVBT_NIM_MODULE*			pNim;//Nim of 2832
	DVBT_NIM_MODULE			DvbtNimModuleMemory;

	//3  DTMB related begin ---
       DTMB_NIM_MODULE*                 pNim2836;//Nim of 2836
	DTMB_NIM_MODULE                   DtmbNimModuleMemory;
	//3DTMB related end ---

	//3  DVBC related begin ---
       QAM_NIM_MODULE*			pNim2840;//Nim of 2840
	QAM_NIM_MODULE				QamNimModuleMemory;
	//3DVBC related end ---
	
};




#define MT2266_TUNER_ADDR		0xc0
#define FC2580_TUNER_ADDR		0xac
#define TUA9001_TUNER_ADDR	0xc0

#define MT2266_OFFSET			0x00
#define MT2266_CHECK_VAL		0x85

#define FC2580_OFFSET			0x01
#define FC2580_CHECK_VAL		0x56

#define TUA9001_OFFSET			0x7e
#define TUA9001_CHECK_VAL		0x2328

#define MXL5007T_BASE_ADDRESS	0xc0
#define MXL5007T_CHECK_ADDRESS	0xD9
#define MXL5007T_CHECK_VALUE	0x14

#define FC0012_BASE_ADDRESS		0xc6
#define FC0012_CHECK_ADDRESS	0x00
#define FC0012_CHECK_VALUE		0xa1

#define E4000_BASE_ADDRESS     0xc8
#define E4000_CHECK_ADDRESS   0x02
#define E4000_CHECK_VALUE       0x40


#define MT2063_TUNER_ADDR		0xc0
#define MT2063_CHECK_OFFSET		0x00
#define MT2063_CHECK_VALUE		0x9e
#define MT2063_CHECK_VALUE_2		0x9c


#define MAX3543_TUNER_ADDR		0xc0
#define MAX3543_CHECK_OFFSET		0x00
#define MAX3543_CHECK_VALUE		0x38
#define MAX3543_SHUTDOWN_OFFSET	0x08


#define TDA18272_TUNER_ADDR		0xc0
#define TDA18272_CHECK_OFFSET		0x00
#define TDA18272_CHECK_VALUE1		0xc7
#define TDA18272_CHECK_VALUE2		0x60


#define FC0013_BASE_ADDRESS		0xc6
#define FC0013_CHECK_ADDRESS		0x00
#define FC0013_CHECK_VALUE			0xa3
#define FC0013_STANDBY_ADDRESS	0x06

#define R820T_BASE_ADDRESS		0x34
#define R820T_CHECK_ADDRESS		0x00
#define R820T_CHECK_VALUE			0x69


struct rtl2832_reg_addr{
	RegType			reg_type;
	unsigned short	reg_addr;
	int				bit_low;
	int				bit_high;
};



typedef enum{
	RTD2831_RMAP_INDEX_USB_CTRL_BIT5 =0,
	RTD2831_RMAP_INDEX_USB_STAT,		
	RTD2831_RMAP_INDEX_USB_EPA_CTL,
	RTD2831_RMAP_INDEX_USB_SYSCTL,
	RTD2831_RMAP_INDEX_USB_EPA_CFG,
	RTD2831_RMAP_INDEX_USB_EPA_MAXPKT,
	RTD2831_RMAP_INDEX_USB_EPA_FIFO_CFG,	

	RTD2831_RMAP_INDEX_SYS_DEMOD_CTL,
	RTD2831_RMAP_INDEX_SYS_GPIO_OUTPUT_VAL,		
	RTD2831_RMAP_INDEX_SYS_GPIO_OUTPUT_EN_BIT3,		
	RTD2831_RMAP_INDEX_SYS_GPIO_DIR_BIT3,			
	RTD2831_RMAP_INDEX_SYS_GPIO_CFG0_BIT67,
	RTD2831_RMAP_INDEX_SYS_DEMOD_CTL1,
	RTD2831_RMAP_INDEX_SYS_GPIO_OUTPUT_EN_BIT1,		
	RTD2831_RMAP_INDEX_SYS_GPIO_DIR_BIT1,	
	RTD2831_RMAP_INDEX_SYS_GPIO_OUTPUT_EN_BIT6,		
	RTD2831_RMAP_INDEX_SYS_GPIO_DIR_BIT6,
	RTD2831_RMAP_INDEX_SYS_GPIO_OUTPUT_EN_BIT5,
	RTD2831_RMAP_INDEX_SYS_GPIO_DIR_BIT5,
#if 0	
    RTD2831_RMAP_INDEX_SYS_GPD,
    RTD2831_RMAP_INDEX_SYS_GPOE,
    RTD2831_RMAP_INDEX_SYS_GPO,
    RTD2831_RMAP_INDEX_SYS_SYS_0,    
#endif 

} rtl2832_reg_map_index;



#define USB_SYSCTL				0x0000 	
#define USB_CTRL				0x0010
#define USB_STAT				0x0014	
#define USB_EPA_CTL				0x0148  	
#define USB_EPA_CFG				0x0144
#define USB_EPA_MAXPKT			0x0158  
#define USB_EPA_FIFO_CFG		0x0160 

#define DEMOD_CTL				0x0000	
#define GPIO_OUTPUT_VAL		0x0001
#define GPIO_OUTPUT_EN			0x0003
#define GPIO_DIR					0x0004
#define GPIO_CFG0				0x0007
#define GPIO_CFG1				0x0008	
#define DEMOD_CTL1				0x000b






#define BIT0		0x00000001
#define BIT1		0x00000002
#define BIT2		0x00000004
#define BIT3		0x00000008
#define BIT4		0x00000010
#define BIT5		0x00000020
#define BIT6		0x00000040
#define BIT7		0x00000080
#define BIT8		0x00000100
#define BIT9		0x00000200
#define BIT10	0x00000400
#define BIT11	0x00000800
#define BIT12	0x00001000
#define BIT13	0x00002000
#define BIT14	0x00004000
#define BIT15	0x00008000
#define BIT16	0x00010000
#define BIT17	0x00020000
#define BIT18	0x00040000
#define BIT19	0x00080000
#define BIT20	0x00100000
#define BIT21	0x00200000
#define BIT22	0x00400000
#define BIT23	0x00800000
#define BIT24	0x01000000
#define BIT25	0x02000000
#define BIT26	0x04000000
#define BIT27	0x08000000
#define BIT28	0x10000000
#define BIT29	0x20000000
#define BIT30	0x40000000
#define BIT31	0x80000000


/* DTMB related 

typedef enum {
	PAGE_0 = 0,
	PAGE_1 = 1,
	PAGE_2 = 2,
	PAGE_3 = 3,
	PAGE_4 = 4,	
	PAGE_5 = 5,	
	PAGE_6 = 6,	
	PAGE_7 = 7,	
	PAGE_8 = 8,	
	PAGE_9 = 9,	
};*/


#define	SUPPORT_DVBT_MODE	0x01
#define	SUPPORT_DTMB_MODE	0x02
#define	SUPPORT_DVBC_MODE	0x04

#define INPUT_ADC_LEVEL 	-8
typedef enum {
	RTL2832 = 0,
	RTL2836,
	RTL2840	
}DEMOD_TYPE;



static int
build_nim_module(
		struct rtl2832_state*  p_state);


int  rtl2832_hw_reset(struct rtl2832_state *p_state);




int rtl2832_read_signal_quality(
	struct dvb_frontend*	fe,
	u32*	quality);
int 
rtl2832_read_signal_strength(
	struct dvb_frontend*	fe,
	u16*	strength);
#endif // __RTD2830_PRIV_H__


