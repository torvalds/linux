/*
defines of FPGA chip ICE65L08's register 
*/

#ifndef SPI_UART_H
#define SPI_UART_H

#include <linux/circ_buf.h>
#include <linux/miscdevice.h>

#define SPI_FPGA_INT_PIN RK2818_PIN_PA4
#define SPI_DPRAM_INT_PIN RK2818_PIN_PA2
#define SPI_FPGA_STANDBY_PIN RK2818_PIN_PH7
#define SPI_FPGA_RST_PIN RK2818_PIN_PF4

#define SPI_FPGA_I2C_EVENT	1
#define SPI_FPGA_POLL_WAIT	0
#define SPI_FPGA_TRANS_WORK	1
#define SPI_FPGA_TEST_DEBUG	0
#if SPI_FPGA_TEST_DEBUG
#define SPI_FPGA_TEST_DEBUG_PIN RK2818_PIN_PE0
extern int spi_test_wrong_handle(void);
#endif

#define TRUE 		1
#define FALSE 		0

struct uart_icount {
	__u32	cts;
	__u32	dsr;
	__u32	rng;
	__u32	dcd;
	__u32	rx;
	__u32	tx;
	__u32	frame;
	__u32	overrun;
	__u32	parity;
	__u32	brk;
};

struct spi_uart
{
	struct workqueue_struct 	*spi_uart_workqueue;
	struct work_struct spi_uart_work;	
	struct timer_list 	uart_timer;
	struct tty_struct	*tty;
	struct kref		kref;
	struct mutex		open_lock;
	struct task_struct	*in_spi_uart_irq;	
	struct circ_buf		xmit;
	struct uart_icount	icount;
	spinlock_t		write_lock;
	spinlock_t		irq_lock;
	unsigned int		index;
	unsigned int		opened;
	unsigned int		regs_offset;
	unsigned int		uartclk;
	unsigned int		mctrl;
	unsigned int		read_status_mask;
	unsigned int		ignore_status_mask;
	unsigned char		x_char;
	unsigned char       ier;
	unsigned char       lcr;

};

struct spi_gpio
{
	struct workqueue_struct 	*spi_gpio_workqueue;
	struct work_struct 	spi_gpio_work;
	struct workqueue_struct 	*spi_gpio_irq_workqueue;
	struct work_struct 	spi_gpio_irq_work;
	struct timer_list 	gpio_timer;
	struct list_head	msg_queue;

};
struct spi_i2c_data
{
	struct i2c_adapter     adapter;
	struct i2c_client         *client;
	struct spi_fpga_port   *port;
	unsigned int                 speed;	
	unsigned int                 mode;
	unsigned int			msg_idx;
	unsigned int			msg_num;
};
struct spi_i2c
{
	struct workqueue_struct 	*spi_i2c_workqueue;
	struct work_struct 	spi_i2c_work;
	struct timer_list i2c_timer;
	struct i2c_adapter *adapter;
	struct i2c_client  *client;
	spinlock_t i2c_lock ; 
	unsigned char interrupt;
	unsigned char i2c_data_width[2];
	unsigned int  speed[2];
	#if SPI_FPGA_I2C_EVENT
	wait_queue_head_t wait_w,wait_r;
	#endif
};

struct spi_dpram
{
	struct workqueue_struct 	*spi_dpram_workqueue;
	struct work_struct 	spi_dpram_work;	
	struct workqueue_struct 	*spi_dpram_irq_workqueue;
	struct work_struct 	spi_dpram_irq_work;
	struct timer_list 	dpram_timer;
	unsigned char		*prx;
	unsigned char		*ptx;
	unsigned int 		rec_len;
	unsigned int		send_len;
	unsigned int 		max_rec_len;
	unsigned int 		max_send_len;
	volatile int 		apwrite_en;
	unsigned short int  dpram_addr;
	struct semaphore 	rec_sem;  
	struct semaphore 	send_sem; 
	struct mutex		rec_lock,send_lock;
	spinlock_t			spin_rec_lock,spin_send_lock;
	wait_queue_head_t 	recq, sendq;
	struct miscdevice 	miscdev;

	int (*write_dpram)(struct spi_dpram *, unsigned short int addr, unsigned char *buf, unsigned int len);
	int (*read_dpram)(struct spi_dpram *, unsigned short int addr, unsigned char *buf, unsigned int len);
	int (*write_ptr)(struct spi_dpram *, unsigned short int addr, unsigned int size);
	int (*read_ptr)(struct spi_dpram *, unsigned short int addr);
	int (*write_irq)(struct spi_dpram *, unsigned int mailbox);
	int (*read_irq)(struct spi_dpram *);
	int (*write_ack)(struct spi_dpram *, unsigned int mailbox);
	int (*read_ack)(struct spi_dpram *);

};

struct spi_fpga_port {
	const char *name;
	struct spi_device 	*spi;
	spinlock_t		work_lock;
	struct mutex		spi_lock;
	struct workqueue_struct 	*fpga_irq_workqueue;
	struct work_struct 	fpga_irq_work;	
	struct timer_list 	fpga_timer;
#if SPI_FPGA_TRANS_WORK
	struct workqueue_struct 	*fpga_trans_workqueue;
	struct work_struct 	fpga_trans_work;	
	int write_en;
	int read_en;
	wait_queue_head_t 	wait_wq, wait_rq;
	struct list_head	trans_queue;
#endif

#if SPI_FPGA_POLL_WAIT
	wait_queue_head_t spi_wait_q;
#endif

	/*spi2uart*/
#ifdef CONFIG_SPI_FPGA_UART
	struct spi_uart uart;
#endif
	/*spi2gpio*/
#ifdef CONFIG_SPI_FPGA_GPIO
	struct spi_gpio gpio;
#endif
	/*spi2i2c*/
#ifdef CONFIG_SPI_FPGA_I2C
	struct spi_i2c i2c;
#endif
	/*spi2dpram*/
#ifdef CONFIG_SPI_FPGA_DPRAM
	struct spi_dpram dpram;
#endif

};


#define ICE_CC72		0
#define ICE_CC196		1
#define FPGA_TYPE		ICE_CC196
#define SEL_UART		0
#define SEL_GPIO		1
#define SEL_I2C			2
#define SEL_DPRAM		3
#define READ_TOP_INT	4

/* CMD */
#define ICE_SEL_UART 			(SEL_UART<<6)
#define ICE_SEL_GPIO 			(SEL_GPIO<<6)
#define ICE_SEL_I2C 			(SEL_I2C<<6)
#define ICE_SEL_DPRAM 			(SEL_DPRAM<<6)

#define ICE_SEL_WRITE			(~(1<<5))
#define ICE_SEL_READ 			(1<<5)

#define ICE_SEL_UART_CH(ch) 	((ch&0x03)<<3)
#define ICE_SEL_READ_INT_TYPE	(3<<3)

/*read int type*/
#define ICE_INT_TYPE_UART0		(~(1<<0))
#define ICE_INT_TYPE_UART1		(~(1<<1))
#define ICE_INT_TYPE_UART2		(~(1<<2))
#define ICE_INT_TYPE_I2C2		(~(1<<3))
#define ICE_INT_TYPE_I2C3		(~(1<<4))
#define ICE_INT_TYPE_GPIO		(~(1<<5))
#define ICE_INT_TYPE_DPRAM		(~(1<<6))
#define ICE_INT_TYPE_SLEEP		(~(1<<7))

#define ICE_INT_I2C_ACK			(~(1<<0))
#define ICE_INT_I2C_READ		(~(1<<1))
#define ICE_INT_I2C_WRITE		(~(1<<2))

/*spi to uart*/
#define	ICE_RXFIFO_FULL			(1<<8)
#define	ICE_RXFIFO_NOT_FULL		(~(1<<8))
#define	ICE_RXFIFO_EMPTY		(1<<9)
#define	ICE_RXFIFO_NOT_EMPTY	(~(1<<9))
#define	ICE_TXFIFO_FULL			(1<<10)
#define	ICE_TXFIFO_NOT_FULL		(~(1<<10))
#define	ICE_TXFIFO_EMPTY		(1<<11)
#define	ICE_TXFIFO_NOT_EMPTY	(~(1<<11))


/*spi to gpio*/
#define	ICE_SEL_GPIO0				(0X00<<3)	//INT/GPIO0
#define	ICE_SEL_GPIO1				(0X02<<2)	//GPIO1
#define	ICE_SEL_GPIO2				(0X03<<2)
#define	ICE_SEL_GPIO3				(0X04<<2)
#define	ICE_SEL_GPIO4				(0X05<<2)
#define	ICE_SEL_GPIO5				(0X06<<2)

#define ICE_SEL_GPIO0_TYPE			(0X00)
#define ICE_SEL_GPIO0_DIR			(0X01)
#define ICE_SEL_GPIO0_DATA			(0X02)
#define ICE_SEL_GPIO0_INT_EN		(0X03)
#define ICE_SEL_GPIO0_INT_TRI		(0X04)		//0:falling edge  1:rising edge
#define ICE_SEL_GPIO0_INT_STATE		(0X05)
#define ICE_SEL_GPIO0_INT_TYPE		(0X06)		//0:edge 1:level,if 1 then support falling and rising trigger
#define ICE_SEL_GPIO0_INT_WAKE		(0X07)		

#define	ICE_SEL_GPIO_DIR			(0X01)
#define	ICE_SEL_GPIO_DATA			(0X02)

#define ICE_STATUS_SLEEP			1
#define ICE_STATUS_WAKE				0

/*spi to i2c*/

typedef enum I2C_ch
{
	I2C_CH0,
	I2C_CH1,
	I2C_CH2,
	I2C_CH3	
}eI2C_ch_t;
typedef enum eI2CReadMode
{
	I2C_NORMAL,
	I2C_NO_REG,
	I2C_NO_STOP
}eI2ReadMode_t;

#define ICE_SEL_I2C_START            (0<<0)
#define ICE_SEL_I2C_STOP              (1<<0)
#define ICE_SEL_I2C_RESTART        (2<<0)
#define ICE_SEL_I2C_TRANS           (3<<0)
#define ICE_SEL_I2C_SMASK     	(~(3<<0))
#define ICE_SEL_I2C_CH2               (0<<2)
#define ICE_SEL_I2C_CH3               (1<<2)
#define ICE_SEL_I2C_DEFMODE      (0<<3)
#define ICE_SEL_I2C_FIFO              (1<<3)
#define ICE_SEL_I2C_SPEED            (2<<3)
#define ICE_SEL_I2C_INT                 (3<<3)
#define ICE_SEL_I2C_MMASK     	(~(3<<3))

#define ICE_I2C_SLAVE_WRITE           (0<<0)
#define ICE_I2C_SLAVE_READ             (1<<0)



#define ICE_SEL_I2C_W8BIT           (0<<2)
#define ICE_SEL_I2C_W16BIT		(1<<2)
#define ICE_SEL_I2C_DWIDTH    	(2<<2)

#define ICE_I2C_AD_ACK              (~(1<<0))
#define ICE_I2C_WRITE_ACK        (~(1<<1))
#define ICE_I2C_READ_ACK	     (~(1<<2))

#define ICE_SEL_I2C_CH2_8BIT          (0<<2)
#define ICE_SEL_I2C_CH2_16BIT        (1<<2)
#define ICE_SEL_I2C_CH2_MIX           (2<<2)

#define ICE_SEL_I2C_CH3_8BIT         (4<<2)
#define ICE_SEL_I2C_CH3_16BIT       (5<<2)
#define ICE_SEL_I2C_CH3_MIX          (6<<2)
#define ICE_SEL_I2C_RD_A               (7<<2)
#define ICE_SEL_I2C_MASK               (7<<2)
#define ICE_SEL_I2C_ACK3               (1<<1)
#define ICE_SEL_I2C_ACK2               (0<<1)

#define INT_I2C_WRITE_ACK		(2)
#define INT_I2C_WRITE_NACK        (3)
#define INT_I2C_READ_ACK	       (4)	
#define INT_I2C_READ_NACK          (5)
#define INT_I2C_WRITE_MASK        (~(1<<1))
#define INT_I2C_READ_MASK        (~(1<<2))

#define ICE_SET_10K_I2C_SPEED         (0x01)
#define ICE_SET_100K_I2C_SPEED        (0x02)     
#define ICE_SET_200K_I2C_SPEED        (0x04)
#define ICE_SET_300K_I2C_SPEED        (0x08)
#define ICE_SET_400K_I2C_SPEED        (0x10)


/*spi to dpram*/
#define ICE_SEL_DPRAM_NOMAL		(~(1<<5))
#define	ICE_SEL_DPRAM_SEM		(1<<5)
#define	ICE_SEL_DPRAM_READ		(~(1<<4))
#define	ICE_SEL_DPRAM_WRITE		(1<<4)
#define ICE_SEL_DPRAM_BL1		(0)
#define ICE_SEL_DPRAM_BL32		(1)
#define ICE_SEL_DPRAM_BL64		(2)
#define ICE_SEL_DPRAM_BL128		(3)
#define ICE_SEL_DPRAM_FULL		(4)

#define ICE_SEL_SEM_WRITE		(0x7F)
#define ICE_SEL_SEM_READ		(0xBF)
#define ICE_SEL_SEM_WRRD		(0x3F)

typedef 	void (*pSpiFunc)(void);	//定义函数指针, 用于调用绝对地址
typedef 	void (*pSpiFuncIntr)(int,void *);
typedef struct
{
	pSpiFuncIntr gpio_vector;
	void *gpio_devid;
}SPI_GPIO_PDATA;


typedef enum eSpiGpioTypeSel
{
	SPI_GPIO0_IS_GPIO = 0,
	SPI_GPIO0_IS_INT,
}eSpiGpioTypeSel_t;



typedef enum eSpiGpioPinInt
{
	SPI_GPIO_INT_DISABLE = 0,
	SPI_GPIO_INT_ENABLE,
}eSpiGpioPinInt_t;


typedef enum eSpiGpioIntTri 
{
	SPI_GPIO_EDGE_FALLING = 0,
	SPI_GPIO_EDGE_RISING,
}eSpiGpioIntTri_t;


typedef enum eSpiGpioIntType 
{
	SPI_GPIO_EDGE = 0,
	SPI_GPIO_LEVEL,
}eSpiGpioIntType_t;

typedef enum eSpiGpioPinDirection
{
	SPI_GPIO_IN = 0,
	SPI_GPIO_OUT,
	SPI_GPIO_DIR_ERR,
}eSpiGpioPinDirection_t;


typedef enum eSpiGpioPinLevel
{
	SPI_GPIO_LOW = 0,
	SPI_GPIO_HIGH,
	SPI_GPIO_LEVEL_ERR,
}eSpiGpioPinLevel_t;

#if (FPGA_TYPE == ICE_CC72)
typedef enum eSpiGpioPinNum
{
	SPI_GPIO_P0_00 = 0,	//GPIO0[0]
	SPI_GPIO_P0_01,
	SPI_GPIO_P0_02,
	SPI_GPIO_P0_03,
	SPI_GPIO_P0_04,
	SPI_GPIO_P0_05,	
	
	SPI_GPIO_P2_00,		
	SPI_GPIO_P2_01,
	SPI_GPIO_P2_02,
	SPI_GPIO_P2_03,
	SPI_GPIO_P2_04,
	SPI_GPIO_P2_05,
	SPI_GPIO_P2_06,
	SPI_GPIO_P2_07,
	SPI_GPIO_P2_08, 
	SPI_GPIO_P2_09 = 15,	//GPIO0[15],the last interrupt/gpio pin	
	
	SPI_GPIO_P3_00 = 16,	//GPIO1[0]
	SPI_GPIO_P3_01,
	SPI_GPIO_P3_02,
	SPI_GPIO_P3_03,
	SPI_GPIO_P3_04,
	SPI_GPIO_P3_05,
	SPI_GPIO_P3_06,
	SPI_GPIO_P3_07,
	SPI_GPIO_P3_08,
	SPI_GPIO_P3_09,
	SPI_GPIO_P0_06 = 26,   	
	SPI_GPIO_I2C3_SCL,
	SPI_GPIO_I2C3_SDA,
	SPI_GPIO_I2C4_SCL,
	SPI_GPIO_I2C4_SDA,
	
}eSpiGpioPinNum_t;

#elif (FPGA_TYPE == ICE_CC196)

typedef enum eSpiGpioPinNum
{
	//GPIO0/INT
	SPI_GPIO_P6_00 = 0,	//HS_DET input	
	SPI_GPIO_P6_01,
	SPI_GPIO_P6_02,
	SPI_GPIO_P6_03,
	SPI_GPIO_P6_04,		//CM3605_POUT_L_INT input
	SPI_GPIO_P6_05,		
	SPI_GPIO_P6_06,		//CHG_OK input
	SPI_GPIO_P6_07,		//HP_HOOK input
	SPI_GPIO_P6_08,
	SPI_GPIO_P6_09,
	SPI_GPIO_P6_10,		//DEFSEL input	
	SPI_GPIO_P6_11,		//FLASH_WP_INT input
	SPI_GPIO_P6_12,		//LOW_BATT_INT input
	SPI_GPIO_P6_13,		//DC_DET input
	SPI_GPIO_P3_08,		
	SPI_GPIO_P3_09 = 15,

	//GPIO1
	SPI_GPIO_P1_00 = 16,	//LCD_ON output
	SPI_GPIO_P1_01,		//LCD_PWR_CTRL output
	SPI_GPIO_P1_02,		//SD_POW_ON output
	SPI_GPIO_P1_03,		//WL_RST_N/WIFI_EN output
	SPI_GPIO_P1_04,		//HARDO,input
	SPI_GPIO_P1_05,		//SENSOR_PWDN output
	SPI_GPIO_P1_06,		//BT_PWR_EN output
	SPI_GPIO_P1_07,		//BT_RST output
	SPI_GPIO_P1_08,		//BT_WAKE_B output
	SPI_GPIO_P1_09,		//LCD_DISP_ON output
	SPI_GPIO_P1_10,		//WM_PWR_EN output
	SPI_GPIO_P1_11,		//HARD1,input
	SPI_GPIO_P1_12,		//VIB_MOTO output
	SPI_GPIO_P1_13,		//KEYLED_EN output
	SPI_GPIO_P1_14,		//CAM_RST output
	SPI_GPIO_P1_15 = 31,	//WL_WAKE_B output

	//GPIO2
	SPI_GPIO_P2_00 = 32,	//Y+YD input
	SPI_GPIO_P2_01,		//Y-YU input
	SPI_GPIO_P2_02,		//AP_TD_UNDIFED input
	SPI_GPIO_P2_03,		//AP_PW_EN_TD output
	SPI_GPIO_P2_04,		//AP_RESET_TD output
	SPI_GPIO_P2_05,		//AP_SHUTDOWN_TD_PMU output
	SPI_GPIO_P2_06,		//AP_RESET_CMMB output
	SPI_GPIO_P2_07,		//AP_CHECK_TD_STATUS input
	SPI_GPIO_P2_08,		//CHARGE_CURRENT_SEL output
	SPI_GPIO_P2_09,		//AP_PWD_CMMB output
 	SPI_GPIO_P2_10,		//X-XL input
	SPI_GPIO_P2_11,		//X+XR input
	SPI_GPIO_P2_12,		//LCD_RESET output
	SPI_GPIO_P2_13,		//USB_PWR_EN output
	SPI_GPIO_P2_14,		//WL_HOST_WAKE_B output
	SPI_GPIO_P2_15 = 47,	//TOUCH_SCREEN_RST output

	//GPIO3
	SPI_GPIO_P0_00 = 48,	//
	SPI_GPIO_P0_01,
	SPI_GPIO_P0_02,
	SPI_GPIO_P0_03,
	SPI_GPIO_P0_04,
	SPI_GPIO_P0_05,
	SPI_GPIO_P0_06,
	SPI_GPIO_P0_07,
	SPI_GPIO_P0_08,
	SPI_GPIO_P0_09,		//FPGA小板该引脚未引出 C5
	SPI_GPIO_P0_10,
	SPI_GPIO_P0_11,
	SPI_GPIO_P0_12,
	SPI_GPIO_P0_13,
	SPI_GPIO_P0_14,
	SPI_GPIO_P0_15 = 63,

	//GPIO4
	SPI_GPIO_P4_00 = 64,	
	SPI_GPIO_P4_01,	
	SPI_GPIO_P4_02,
	SPI_GPIO_P4_03,
	SPI_GPIO_P4_04,
	SPI_GPIO_P4_05,
	SPI_GPIO_P4_06,		//CHARGER_INT_END input
	SPI_GPIO_P4_07,		//CM3605_PWD output
	SPI_GPIO_P3_00,		
	SPI_GPIO_P3_01,
	SPI_GPIO_P3_02,
	SPI_GPIO_P3_03,
	SPI_GPIO_P3_04,
	SPI_GPIO_P3_05,
	SPI_GPIO_P3_06,
	SPI_GPIO_P3_07 = 79,	

	//GPIO5
	SPI_GPIO_P4_08 = 80,	//CM3605_PS_SHUTDOWN
	SPI_GPIO_P0_TXD2,		//temp

}eSpiGpioPinNum_t;

#endif


typedef enum eSpiGpioPinIntIsr
{
	SPI_GPIO_IS_INT = 0,
	SPI_GPIO_NO_INT,
}eSpiGpioPinIntIsr_t;

extern struct spi_fpga_port *pFpgaPort;
#if SPI_FPGA_TRANS_WORK
extern int spi_write_work(struct spi_device *spi, u8 *buf, size_t len);
#endif
extern unsigned int spi_in(struct spi_fpga_port *port, int reg, int type);
extern void spi_out(struct spi_fpga_port *port, int reg, int value, int type);

#if defined(CONFIG_SPI_FPGA_UART)
extern void spi_uart_handle_irq(struct spi_device *spi);
extern int spi_uart_register(struct spi_fpga_port *port);
extern int spi_uart_unregister(struct spi_fpga_port *port);
#endif
#if defined(CONFIG_SPI_FPGA_GPIO)
extern int spi_gpio_int_sel(eSpiGpioPinNum_t PinNum,eSpiGpioTypeSel_t type);
extern int spi_gpio_set_pindirection(eSpiGpioPinNum_t PinNum,eSpiGpioPinDirection_t direction);
extern int spi_gpio_set_pinlevel(eSpiGpioPinNum_t PinNum, eSpiGpioPinLevel_t PinLevel);
extern eSpiGpioPinLevel_t spi_gpio_get_pinlevel(eSpiGpioPinNum_t PinNum);
extern int spi_gpio_enable_int(eSpiGpioPinNum_t PinNum);
extern int spi_gpio_disable_int(eSpiGpioPinNum_t PinNum);
extern int spi_gpio_set_int_trigger(eSpiGpioPinNum_t PinNum,eSpiGpioIntTri_t IntTri);
extern int spi_gpio_read_iir(void);
extern int spi_request_gpio_irq(eSpiGpioPinNum_t PinNum, pSpiFunc Routine, eSpiGpioIntTri_t IntType,void *dev_id);
extern int spi_free_gpio_irq(eSpiGpioPinNum_t PinNum);
extern int spi_gpio_handle_irq(struct spi_device *spi);
extern int spi_gpio_init(void);
extern void spi_gpio_irq_setup(void);
extern int spi_gpio_register(struct spi_fpga_port *port);
extern int spi_gpio_unregister(struct spi_fpga_port *port);
#endif
#if defined(CONFIG_SPI_FPGA_I2C)
extern int spi_i2c_handle_irq(struct spi_fpga_port *port,unsigned char channel);
extern int spi_i2c_register(struct spi_fpga_port *port,int num);
extern int spi_i2c_unregister(struct spi_fpga_port *port);
#endif
#if defined(CONFIG_SPI_FPGA_DPRAM)
extern int spi_dpram_handle_ack(struct spi_device *spi);
extern int spi_dpram_register(struct spi_fpga_port *port);
extern int spi_dpram_unregister(struct spi_fpga_port *port);
#endif

#if defined(CONFIG_SPI_FPGA_FW)
extern int __init fpga_dl_fw(void);
#endif

#endif
