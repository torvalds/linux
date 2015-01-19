/*aml_i2c.h*/

#ifndef AML_I2C
#define AML_I2C

#include <linux/i2c.h>
#include <linux/i2c-aml.h>

#define ENABLE_GPIO_TRIGGER

#define I2C_DELAY_MODE 0
#define I2C_INTERRUPT_MODE 1
#define I2C_TIMER_POLLING_MODE 2

#define I2C_STATE_IDLE  0
#define I2C_STATE_WORKING  1
#define I2C_STATE_SUSPEND 2


#define ADAPTER_NAME    "aml_i2c_adap"
#define NAME_LEN 8
#define AML_I2C_MAX_TOKENS		8

#define AML_I2C_CTRL_CLK_DELAY_MASK			0x3ff
#define AML_I2C_SLAVE_ADDR_MASK				0xff

#define AML_I2C_PRINT_DATA(name) do{  \
     if(i2c->i2c_debug) {\
            printk("[%s]:%s", name, p->flags & I2C_M_RD? "read" : "write");\
        	for(i=0;i<p->len;i++)   \
        		printk("%x-",*(p->buf)++);  \
        	printk("\n");\
     }\
}while(0)

/*I2C_CONTROL_REG	0x2140*/
struct aml_i2c_reg_ctrl {
	unsigned int start:1;		/*[0] Set to 1 to start list processing*/
	/*Setting this bit to 0 while the list processor is operating causes the list 
		processor to abort the current I2C operation and generate an I2C STOP
		command on the I2C bus.   Normally this bit is set to 1 and left high 
		until processing is complete.  To re-start the list processor with a 
		new list (after a previous list has been exhausted), simply set this 
		bit to zero then to one.*/
	unsigned int ack_ignore:1;	/*[1] Set to 1 to disable I2C ACK detection.*/
	/*The I2C bus uses an ACK signal after every byte transfer to detect 
		problems during the transfer.  Current Software implementations of the
		I2C bus ignore this ACK.  This bit is for compatibility with the current 
		Amlogic software.   This bit should be set to 0 to allow NACK 
		operations to abort I2C bus transactions.  If a NACK occurs, the ERROR
		bit above will be set. */
	unsigned int status:1;		/*[2] the status of the List processor*/
	#define 	IDLE		0
	#define 	RUNNING	1
	/*	0:	IDLE
		1: 	Running.  The list processor will enter this state on the clock cycle
		after the START bit is set.  The software can poll the status register to 
		determine when processing is complete.
	*/
	unsigned int error:1;		/*[3] */
	/*This read only bit is set if the I2C device generates a NACK during writing. 
		This bit is cleared at on the clock cycle after the START bit is set to 1 
		indicating the start of list processing.  Errors can be ignored by setting 
		the ACK_IGNORE bit below.  Errors will be generated on Writes to 
		devices that return NACK instead of ACK.  A NACK is returned by a 
		device if it is unable to accept any more data (for example because it 
		is processing some other real-time function).  In the event of an 
		ERROR, the I2C module will automatically generate a STOP condition 
		on the bus.*/
	unsigned int cur_token:4;	/*[7:4] the current token being processed*/
	unsigned int rd_data_cnt:4;/*[11:8] number of bytes READ over the I2C bus*/
	/*If this value is zero, then no data has been read.  If this value is 1, then 
		bits [7:0] in TOKEN_RDATA_REG0 contains valid data.  The software can 
		read this register after an I2C transaction to get the number of bytes to
		read from the I2C device*/
	unsigned int clk_delay:10;	/*[21:12] Quarter clock delay*/
	/*This value corresponds to period of the SCL clock divided by 4
		Quarter Clock Delay = * System Clock Frequency
		For example, if the system clock is 133Mhz, and the I2C clock period 
		is 10uS (100khz), then
		Quarter Clock Delay = * 133 Mhz = 332
	*/
	unsigned int manual_en:1;	/*[22] enable manual mode. */
	/*Manual I2C mode is controlled by bits 12,13,14 and 15 above.*/
	unsigned int wrscl:1;		/*[23] Sets the level of the SCL line */
	/*if manual mode is enabled.  If this bit is '0', then the SCL line is
		pulled low.  If this bit is '1' then the SCL line is tri-stated.*/
	unsigned int wrsda:1; 		/*[24] Sets the level of the SDA line */
	/*if manual mode is enabled.  If this bit is '0', 	then the SDA line is 
		pulled low.  If this bit is '1' then the SDA line is tri-stated.*/
	unsigned int rdscl:1; 		/*[25] Read back level of the SCL line*/
	unsigned int rdsda:1; 		/*[26] Read back level of the SDA line*/
#if MESON_CPU_TYPE > MESON_CPU_TYPE_MESON8
	unsigned int unused:1; 	/*[27]*/
	unsigned int clk_delay_ext:2; 	/*[29:28]*/
	unsigned int unused2:2; 	/*[31:30]*/
#else
	unsigned int unused:5; 	/*[31:27]*/
#endif
};

struct aml_i2c_reg_slave_addr {
	unsigned int slave_addr:8;	/*[7:0] SLAVE ADDRESS.*/
	/*This is a 7-bit value for a 7-bit I2C device, or (0xF0 | {A9,A8}) for a 
		10 bit I2C device.  By convention, the slave address is typically 
		stored in by first left shifting it so that it's MSB is D7 (The I2C bus
		assumes the 7-bit address is left shifted one).  Additionally, since 
		the SLAVE address is always an 7-bit value, D0 is always 0. 

		NOTE:  The I2C always transfers 8-bits even for address.  The I2C
		hardware will use D0 to dictate the direction of the bus.  Therefore, 
		D0 should always be '0' when this register is set.
	*/
	unsigned int sda_filter:3;	/*[10:8] SDA FILTER*/
	/*A filter was added in the SCL input path to allow for filtering of slow 
		rise times.  0 = no filtering, 7 = max filtering*/
	unsigned int scl_filter:3;	/*[13:11] SCL FILTER*/
	/*A filter was added in the SCL input path to allow for filtering of slow 
		rise times.  0 = no filtering, 7 = max filtering*/
#if MESON_CPU_TYPE > MESON_CPU_TYPE_MESON8
	unsigned int unused:2;	/*[15:14]*/
	unsigned int clk_low_delay:12;	/*[27:16]*/
	unsigned int clk_low_delay_en:1;	/*[28]*/
	unsigned int unused2:3;	/*[31:29]*/
#else
	unsigned int unused:18;	/*[31:14]*/
#endif
};

/*Write data associated with the DATA token should be placed into the 
	I2C_TOKEN_WDATA_REG0 or I2C_TOKEN_WDATA_REG1 registers.   
	Read data associated with the DATA or DATA-LAST token can be read from
	the I2C_TOKEN_RDATA_REG0 or I2C_TOKEN_RDATA_REG1 registers*/
	
enum aml_i2c_token {
	TOKEN_END,
	TOKEN_START,
	TOKEN_SLAVE_ADDR_WRITE,
	TOKEN_SLAVE_ADDR_READ,
	TOKEN_DATA,
	TOKEN_DATA_LAST,
	TOKEN_STOP
};

struct aml_i2c_reg_master {
	volatile unsigned int i2c_ctrl;
	volatile unsigned int i2c_slave_addr;
	volatile unsigned int i2c_token_list_0;
	volatile unsigned int i2c_token_list_1;
	volatile unsigned int i2c_token_wdata_0;
	volatile unsigned int i2c_token_wdata_1;
	volatile unsigned int i2c_token_rdata_0;
	volatile unsigned int i2c_token_rdata_1;	
};


struct aml_i2c_reg_slave_ctrl {
	unsigned int samp_rate:7;	/*[6:0] sampling rate*/
	/*Defined as MPEG system clock / (value + 1).  The SDA and SCL inputs into 
		the slave module are sampled as a way of filtering the inputs.   A 
		rising or falling edge is determined by when 3 successive samples are 
		either high or low respectively*/	
	unsigned int enable:1;		/*[7] A '1' enables the I2C slave state machine*/
	unsigned int hold_time:8;	/*[15:8]*/
	/*Data hold time after the falling edge of SCL.  
		Hold time = (MPEG system clock period) * (value + 1).
	*/
	unsigned int slave_addr:8;	/*[23-16]*/
	/*Bits [7:1] are used to identify the device.  
		Bit [0] is ignored since this corresponds to the R/W bit.*/
	unsigned int ack_always:1;	/*[24]*/
	/*Typically the ACK of a slave I2C device is dependent upon the 
		availability of data (if reading) and room to store data (when we are 
		being written).  Our I2C module has a status register that can be read
		continuously.  This bit can be set if the I2C master wants to 
		continually read the status register. */
	unsigned int irq_en:1;		/*[25]*/
	/*If this bit is set, then an interrupt will be sent to the ARC whenever 4 
		bytes have been read or 4 bytes have been written to the I2C slave 
		module.*/
	unsigned int busy:1;		/*[26] */
	/*Read only status bit.  '1' indicates that the I2C slave module is sending
		or receiving data.*/
	unsigned int rx_rdy:1;		/*[27] */
	/*This bit is set to '1' by the ARC to indicate to the slave machine that 
		the I2C slave module is ready to receive data.  This bit is cleared by 
		the I2C module when it has received 4 bytes from the I2C master.  
		This bit is also available in the status register that can be read by 
		the I2C master.   The I2C master can read the status register to see 
		when the I2C slave module is ready to receive data.*/
	unsigned int tx_rdy:1;		/*[28] */
	/*This bit is set to '1' by the ARC to indicate to the slave machine that 
		the I2C slave module is ready to send data.  This bit is cleared by 
		the I2C module when it has sent 4 bytes to the I2C master.  This bit
		is also available in the status register that can be read by the I2C 
		master.   The I2C master can read the status register to see when the
		I2C slave module has data to send.*/
	unsigned int reg_ptr:3;		/*[31:29] */
	/*There are 5 internal registers inside the I2C slave module.  The I2C 
		Master sets this value using the byte that follows the address byte 
		in the I2C data stream.  Register 4 (numbered 0,1,бн4) is the 
		status register.*/
};

struct aml_i2c_reg_slave{
	unsigned int i2c_slave_ctrl;
	unsigned int i2c_slave_tx_data;
	unsigned int i2c_slave_rx_data;
};

struct aml_i2c {
	unsigned int 		i2c_debug;
	unsigned int		cur_slave_addr;
	unsigned int 		wait_count;
	unsigned int 		wait_ack_interval;
	unsigned int 		wait_read_interval;
	unsigned int 		wait_xfer_interval;
	unsigned int 		master_no;/*master a:0 master b:1*/
	#define 			MASTER_A		0
	#define			MASTER_B		1
	unsigned char		token_tag[AML_I2C_MAX_TOKENS];
	unsigned int 		msg_flags;

    int mode;
    int state;
    struct completion   aml_i2c_completion;
    struct hrtimer      aml_i2c_hrtimer;
    int irq;
    struct i2c_msg *msgs;
    int msgs_num;
    unsigned char *xfer_buf;
    int xfer_num;
    int remain_len;
#ifdef ENABLE_GPIO_TRIGGER
    int trig_gpio;
#endif

	struct i2c_adapter  	adap;
	struct aml_i2c_ops* ops;

	struct aml_i2c_reg_master __iomem* master_regs;

	pinmux_set_t	master_pinmux;
	
	unsigned int		master_i2c_speed;
	struct mutex     *lock;
      struct class      cls;
	unsigned int 		cur_token;

	/*reserved original member, used in bsp*/
	unsigned int		use_pio;/*0: hardware i2c, 1: pio i2c*/
	pinmux_set_t	master_a_pinmux;
	pinmux_set_t	master_b_pinmux;
	struct device *dev;
	struct pinctrl *p;
	const char *master_state_name;
};

struct aml_i2c_ops {
     void (*xfer_prepare)(struct aml_i2c *i2c, unsigned int speed);
	 int (*read)(struct aml_i2c *i2c, unsigned char *buf, unsigned int len);
	 int (*write)(struct aml_i2c *i2c, unsigned char *buf, unsigned int len);
	 int (*do_address)(struct aml_i2c *i2c, unsigned int addr);
	 void (*stop)(struct aml_i2c *i2c);
};

#endif

