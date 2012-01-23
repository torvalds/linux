/*
*********************************************************************************************************
*											        eBase
*						                the Abstract of Hardware
*									           the OAL of PIO
*
*						        (c) Copyright 2006-2010, AW China
*											All	Rights Reserved
*
* File    	: 	pin.h
* Date		:	2010-11-15
* By      	: 	holi
* Version 	: 	V1.00
* Description:
*
*********************************************************************************************************
*/
#ifndef	__CSP_PIN_PARA_H__
#define	__CSP_PIN_PARA_H__

//=======================================================================================================
//======================================           Logic Dev    =========================================
//=======================================================================================================



typedef enum LOG_DEVS
{
	PIN_DEV_NULL	=0	,//null device
	PIN_DEV_NAND0		,

	PIN_DEV_TWI0		,
	PIN_DEV_SPI1		,
	PIN_DEV_IR			,
	PIN_DEV_PWM0		,
	PIN_DEV_LCD0		,

	PIN_DEV_TWI1		,
	PIN_DEV_TWI2		,

	PIN_DEV_DRAM		,//no pins

	PIN_DEV_TS0			,//no use
    PIN_DEV_TS1         ,

	PIN_DEV_SPI0		,

	PIN_DEV_SPI2		,

	PIN_DEV_MS0			,
	PIN_DEV_CSI0		,
	PIN_DEV_CSI1		,

	PIN_DEV_EMAC		,

	PIN_DEV_SDC0		,
	PIN_DEV_SDC1		,
	PIN_DEV_SDC2		,
	PIN_DEV_SDC3		,

	PIN_DEV_USB0		,//no device pins
	PIN_DEV_USB1		,//no device pins
	PIN_DEV_USB2		,//no device pins

	PIN_DEV_ATA			,//no pins
	PIN_DEV_LCD1		,//no pins

	PIN_DEV_COM			,

	PIN_DEV_UART0		,
	PIN_DEV_UART1		,
	PIN_DEV_UART2		,
	PIN_DEV_UART3		,
	PIN_DEV_UART4		,
	PIN_DEV_UART5		,
	PIN_DEV_UART6		,
	PIN_DEV_UART7		,

	PIN_DEV_SPDIF		,
	PIN_DEV_PS2_0		,
	PIN_DEV_PS2_1		,
	PIN_DEV_AC97		,

	PIN_DEV_IIS			,


	PIN_DEV_SMC			,
	PIN_DEV_CAN			,

	PIN_DEV_EINT0		,   //GPIOB:11
	PIN_DEV_EINT1		,   //GPIOB:12
	PIN_DEV_EINT2		,   //GPIOB:13
	PIN_DEV_EINT3		,   //GPIOB:14
	PIN_DEV_EINT4		,   //GPIOA:18
	PIN_DEV_EINT5		,   //GPIOA:19
	PIN_DEV_EINT6		,   //GPIOB:10
	PIN_DEV_EINT7		,   //GPIOB:9

	PIN_GPIO			,	//通用GPIO类设备
	PIN_DEV_MAX

}LOG_DEVS_t;



//=======================================================================================================
//======================================           Logic PIN               =========================================
//=======================================================================================================

//typedef enum DRAM_LOG_PINS{
//
//}DRAM_LOG_PINS_t;

//for PIN_DEV_NAND0
typedef enum NAND_LOG_PINS{
	NAND_WE	=	0	,
	NAND_ALE		,
	NAND_CLE		,
	NAND_RD			,
	NAND_WP			,
	NAND_RB0		,
	NAND_RB1		,
	NAND_D0			,
	NAND_D1			,
	NAND_D2			,
	NAND_D3			,
	NAND_D4			,
	NAND_D5			,
	NAND_D6			,
	NAND_D7			,
	NAND_CE0		,
	NAND_CE1		,
	NAND_CE2		,
	NAND_CE3		,
	NAND_CE4		,
	NAND_CE5		,
	NAND_CE6		,
	NAND_CE7		,
	NAND_PIN_MAX	//用来描述该控制器的PIN的最大数目,该字段必须放在最后
}NAND_LOG_PINS_t;

// typedef enum USB_LOG_PINS{
//
// }USB_LOG_PINS_t;
//
// typedef enum ATA_LOG_PINS{
//     ATA_A0,
//     ATA_A1,
//     ATA_A2,
//     ATAT_IRQ,
//     ATA_D0,
//     ATA_D1,
//     ATA_D2,
//     ATA_D3,
//     ATA_D4,
//     ATA_D5,
//     ATA_D6,
//     ATA_D7,
//     ATA_D8,
//     ATA_D9,
//     ATA_D10,
//     ATA_D11,
//     ATA_D12,
//     ATA_D13,
//     ATA_D14,
//     ATA_D15,
//     ATA_OE,
//     ATA_DREQ,
//     ATA_DACK,
//     ATA_CS0,
//     ATA_CS1,
//     ATA_IORDY,
//
// }ATA_LOG_PINS_t;

//for PIN_DEV_SDC0/PIN_DEV_SDC1/PIN_DEV_SDC2/PIN_DEV_SDC3
typedef enum SDC_LOG_PINS{
	SDC_CLK	=	0	,
	SDC_CMD			,
	SDC_D0			,
	SDC_D1			,
	SDC_D2			,
	SDC_D3			,
	SDC_PIN_MAX
}SDMMC_LOG_PINS_t;

//for PIN_DEV_COM
typedef enum COM_LOG_PINS{
    COM_CLK,
    COM_SIGN,
    COM_MAG,
    COM_PIN_MAX,
}COM_LOG_PINS_t;

//PIN_DEV_MS0
typedef enum MS_LOG_PINS{
	MS_CLK	=	0	,
	MS_BS			,
	MS_D0			,
	MS_D1			,
	MS_D2			,
	MS_D3			,
	MS_PIN_MAX
}MS_LOG_PINSt;

//for PIN_DEV_SPI0/PIN_DEV_SPI1/PIN_DEV_SPI2
typedef enum SPI_LOG_PINS{
	SPI_CS0	=	0	,
	SPI_CS1			,	//部分spi控制器没有引出
	SPI_CS2			,	//目前没有引出
	SPI_CS3			,	//目前没有引出
	SPI_CLK			,
	SPI_MOSI		,
	SPI_MISO		,
	SPI_PIN_MAX
}SPI_LOG_PINS_t;

//for PIN_DEV_UART0/PIN_DEV_UART1/PIN_DEV_UART2
//PIN_DEV_UART3/PIN_DEV_UART4/PIN_DEV_UART5/PIN_DEV_UART6/PIN_DEV_UART7
typedef enum UART_LOG_PINS{
	UART_TX=	0	,
	UART_RX			,
	UART_RTS		,
	UART_CTS		,
    UART_DTR        ,
    UART_DSR        ,
    UART_DCD        ,
    UART_RING       ,
	UART_PIN_MAX
}UART_LOG_PINS_t;

//for PIN_DEV_TWI0/PIN_DEV_TWI1/PIN_DEV_TWI2
typedef enum TWI_LOG_PINS{
	TWI_SCK	=	0	,
	TWI_SDA			,
	TWI_PIN_MAX
}TWI_LOG_PINS_t;

//PIN_DEV_PWM0
typedef enum PWM_LOG_PINS{
	PWM_PIN	=	0	,
	PWM_PIN_MAX
}PWM_LOG_PINS_t;

//FOR PIN_DEV_IR
typedef enum IR_LOG_PINS{
	IR_TX	=	0	,
	IR_RX			,
	IR_PIN_MAX
}IR_LOG_PINS_t;

//PIN_DEV_IIS
typedef enum IIS_LOG_PINS{
	IIS_MCLK	=	0	,
	IIS_BCLK			,
	IIS_LRCK			,
	IIS_DO0				,
	IIS_DO1				,
	IIS_DO2				,
	IIS_DO3				,
	IIS_DI				,
	IIS_PIN_MAX
}IIS_LOG_PINS_t;

//FOR PIN_DEV_AC97
typedef enum AC97_LOG_PINS{
	AC97_MCLK	=	0	,
	AC97_BCLK			,
	AC97_SYNC			,
	AC97_DO				,
	AC97_DI				,
	AC97_PIN_MAX
}AC97_LOG_PINS_t;

//PIN_DEV_TS0/PIN_DEV_TS1
typedef enum TS_LOG_PINS{
	TS_CLK	=	0	,
	TS_ERR			,
	TS_SYNC			,
	TS_DVLD			,
	TS_D0			,
	TS_D1			,
	TS_D2			,
	TS_D3			,
	TS_D4			,
	TS_D5			,
	TS_D6			,
	TS_D7			,
	TS_PIN_MAX
}TS_LOG_PINS_t;

//FOR PIN_DEV_CSI0/PIN_DEV_CSI1
typedef enum CSI_LOG_PINS{
	CSI_PCK	=	0	,
	CSI_CK			,
	CSI_HSYNC		,
	CSI_VSYNC		,
	CSI_D0			,
	CSI_D1			,
	CSI_D2			,
	CSI_D3			,
	CSI_D4			,
	CSI_D5			,
	CSI_D6			,
	CSI_D7			,
	CSI_PIN_MAX
}CSI_LOG_PINS_t;

//FOR PIN_DEV_LCD0/PIN_DEV_LCD1
typedef enum LCD_LOG_PINS{
	LCD_CLK	=	0	,
	LCD_DE			,
	LCD_HSYNC		,
	LCD_VSYNC		,
	LCD_D0			,
	LCD_D1			,
	LCD_D2			,
	LCD_D3			,
	LCD_D4			,
	LCD_D5			,
	LCD_D6			,
	LCD_D7			,
	LCD_D8			,
	LCD_D9			,
	LCD_D10			,
	LCD_D11			,
	LCD_D12			,
	LCD_D13			,
	LCD_D14			,
	LCD_D15			,
	LCD_D16			,
	LCD_D17			,
	LCD_D18			,
	LCD_D19			,
	LCD_D20			,
	LCD_D21			,
	LCD_D22			,
	LCD_D23			,
	LCD_PIN_MAX

}LCD_LOG_PINS_t;

//FOR PIN_DEV_EMAC
typedef enum EMAC_LOG_PINS{
	EMAC_RX_D3	=	0	,
	EMAC_RX_D2			,
	EMAC_RX_D1			,
	EMAC_RX_D0			,

	EMAC_TX_D3			,
	EMAC_TX_D2			,
	EMAC_TX_D1			,
	EMAC_TX_D0			,

	EMAC_RXCK			,
	EMAC_RX_ERR			,
    EMAC_RX_DV          ,
	EMAC_MDC			,
	EMAC_MDIO			,
	EMAC_TX_EN			,
	EMAC_TXCK			,
	EMAC_CRS			,
	EMAC_COL			,
	EMAC_TX_ERR			,
	EMAC_PIN_MAX
}EMAC_LOG_PINS_t;

//for PIN_DEV_SPDIF
typedef enum SPDIF_LOG_PINS{
	SPDIF_MCLK	=	0	,
	SPDIF_DI			,
	SPDIF_DO			,
	SPDIF_PIN_MAX
}SPDIF_LOG_PINS_t;

//for PIN_DEV_PS2_0/PIN_DEV_PS2_1
typedef enum PS2_LOG_PINS{
	PS2_SCK	=	0	,
	PS2_SDA			,
	PS2_PIN_MAX
}PS2_LOG_PINS_t;

//for PIN_DEV_SMC
typedef enum SMC_LOG_PINS{
		SMC_VPPEN,
		SMC_VPPPP,
		SMC_DET,
		SMC_VCCEN,
		SMC_RST,
		SMC_SLK,
		SMC_SDA,
		SMC_PIN_MAX
}SMC_LOG_PINS_t;

//for PIN_DEV_CAN
typedef enum CAN_LOG_PINS{
	CAN_RX,
    CAN_TX,
    CAN_PIN_MAX,
}CAN_LOG_PINS_t;

/*--FIXME--中断属性的PIN还没有处理
     PIN_DEV_EINT0			,   //GPIOB:11
     PIN_DEV_EINT1			,   //GPIOB:12
     PIN_DEV_EINT2			,   //GPIOB:13
     PIN_DEV_EINT3			,   //GPIOB:14
     PIN_DEV_EINT4			,   //GPIOA:18
     PIN_DEV_EINT5			,   //GPIOA:19
     PIN_DEV_EINT6			,   //GPIOB:10
     PIN_DEV_EINT7			,   //GPIOB:9
*/


//=======================================================================================================
//======================================                        =========================================
//=======================================================================================================

#define	PIN_LOG_DEV_MAX		(PIN_DEV_MAX + 0x04)				//目前支持的最大逻辑设备数
#define	PIN_MAX_NR			512				//目前支持的最大PIN数目

#define	PIN_CFG_NAME_LEN	16


//扩展部分，用来标识是否被使用等信息
typedef	struct pin_log_2_phy_ext{
	u8 is_used;
	u8 user_dev_id;				//该pin的设备id
}pin_log_2_phy_ext_t;


typedef	struct pin_log_2_phy_item{
	u8 log_dev_id;

	u8 log_dev_pin_nr;				//该log dev的pin的数目
	u8 log_pin_id;
	u8 phy_pin_group;
	u8 phy_pin_offset;

	u8 func;
	u8 pull;
	u8 driver;
	u8 data;

	u8 log_pin_name[PIN_CFG_NAME_LEN];
	u32 log_pin_name_addsum;	//名字的累加和

	u8 reserved[2];

	pin_log_2_phy_ext_t ext;

}pin_log_2_phy_item_t;




//=======================================================================================================
//======================================                        =========================================
//=======================================================================================================

#define	PORT_PINS_MAX			32
#define	FUNC_SELECT_ITEM_MAX	9			//
#define	PIN_NR_MAX				(PORT_PINS_MAX*FUNC_SELECT_ITEM_MAX)		//pin的最大数目
#define	DEV_NR_MAX				128			//系统内设备的最大数目
#define	DEVID_2_LOGPIN_MAX		64			//一个设备允许的最大log管脚数目


//=========================================================================
//==由于一类外设可能存在多个控制器，但这些控制器又未必是完全对称，
//=========================================================================



typedef enum{
	PIN_PULL_DEFAULT 	= 	0xFF,
	PIN_PULL_DISABLE 	=	0x00,
	PIN_PULL_UP			=	0x01,
	PIN_PULL_DOWN		=	0x02,
	PIN_PULL_RESERVED	=	0x03
}pin_pull_t;



typedef	enum{
	PIN_MULTI_DRIVING_DEFAULT	=	0xFF,
	PIN_MULTI_DRIVING_0			=	0x00,
	PIN_MULTI_DRIVING_1			=	0x01,
	PIN_MULTI_DRIVING_2			=	0x02,
	PIN_MULTI_DRIVING_3			=	0x03
}pin_multi_driving_t;

typedef enum{
    PIN_DATA_LOW    ,
    PIN_DATA_HIGH   ,
    PIN_DATA_DEFAULT = 0XFF
}pin_data_t;

#define	PRIV_0_IS_USED	0



#define	PIN_TOTAL_CFG_VERSION	0xEB162000	//表示的PIN的配置

typedef struct pin_total_cfg{
	u32 version;								//PIN_TOTAL_CFG_VERSION
    u32 group_nr_max;							//group的最大数目
	struct pin_log_2_phy_item p_items[PIN_MAX_NR];	//这些项必须遵行如下要求:
													//1,同一个逻辑设备，其所有pin必须连续存放
													//2,同一个逻辑设备，按照逻辑PIN号，从小到大，排列，否则得用动态查找，效率低下
	u32 real_item_nr;							//实际逻辑PIN的数目
    u32 realDevNum;                             //real logic device number
	u32 log_dev_start_index[PIN_LOG_DEV_MAX];	//某个逻辑设备的PIN在p_items[]中的index，方便快速定位
	u32 logDevId[PIN_LOG_DEV_MAX];			//array of log_dev_xxx, should be type of LOG_DEVS_t

	u8  log_dev_name[PIN_LOG_DEV_MAX][PIN_CFG_NAME_LEN];
	u32  log_dev_name_addsum[PIN_LOG_DEV_MAX];	//名字的累加和，为了加速

}pin_total_cfg_t;


//=======================================================================================================
//======================================         handler         =========================================
//=======================================================================================================
#define	PIN_TYPE_DEFAULT		0x00
#define	PIN_TYPE_DEV			0x01
#define	PIN_TYPE_GPIO			0x02

struct gpio_pin_info{
	u16 phy_pin_group;
	u16 phy_pin_offset;
};

struct dev_pin_info{
	u32 log_2_phy_list_index;	//在pin_log_2_phy_item中的index


	u8 log_pin_id;
	u8 phy_pin_group;
	u8 phy_pin_offset;
	u8 reserved;
};

#define	PIN_HANDLER_MAGIC 0x0ADE

//pin的req句柄
typedef struct pins_handle
{
	u16 magic;				//PIN_HANDLER_MAGIC
	u8 type;				//such as PIN_TYPE_DEFAULT
	u8 pin_nr;				//pin的数目
	u8 log_dev_id;			//从pin_log_2_phy_item{}中copy过来的信息
	union{
		struct gpio_pin_info gpio;
		struct dev_pin_info *p_dev;
	}pins;

}pins_handle_t;



#define	PIN_PHY_GROUP_A			0x00
#define	PIN_PHY_GROUP_B			0x01
#define	PIN_PHY_GROUP_C			0x02
#define	PIN_PHY_GROUP_D			0x03
#define	PIN_PHY_GROUP_E			0x04
#define	PIN_PHY_GROUP_F			0x05
#define	PIN_PHY_GROUP_G			0x06
#define	PIN_PHY_GROUP_H			0x07
#define	PIN_PHY_GROUP_I			0x08
#define	PIN_PHY_GROUP_J			0x09
#define	PIN_NOT_USED	        0xff	//没有使用的PIN，如SPI的CS，IP中有4个，但实际可能只用2个，没有的两个则用NOT_USED表示


#define	PIN_XXX_NA		0xFF
#define PIN_FUNC_NA 	PIN_XXX_NA
#define PIN_PULL_NA 	PIN_XXX_NA
#define PIN_DRIV_NA 	PIN_XXX_NA
#define PIN_FUNC_NA 	PIN_XXX_NA

#endif	//__CSP_PIN_PARA_H__

