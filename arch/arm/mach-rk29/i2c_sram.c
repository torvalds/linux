#include <mach/rk29_iomap.h>
#include <mach/board.h>
#include <mach/sram.h>
#include <mach/iomux.h>
#include <mach/cru.h>
#include <asm/io.h>
#include <mach/gpio.h>

#if defined(CONFIG_RK29_I2C_INSRAM)

#define I2C_SPEED 200

#if defined(CONFIG_MACH_RK29_TD8801_V2)
/******************need set when you use i2c*************************/
#define I2C_SADDR               (0x34)        /* slave address ,wm8310 addr is 0x34*/
#define SRAM_I2C_CH 1	//CH==0, i2c0,CH==1, i2c1,CH==2, i2c2,CH==3, i2c3
#define SRAM_I2C_ADDRBASE RK29_I2C1_BASE //RK29_I2C0_BASE\RK29_I2C2_BASE\RK29_I2C3_BASE
#define I2C_SLAVE_ADDR_LEN 1  // 2:slav addr is 10bit ,1:slav addr is 7bit
#define I2C_SLAVE_REG_LEN 2   // 2:slav reg addr is 16 bit ,1:is 8 bit
#define SRAM_I2C_DATA_BYTE 2  //i2c transmission data is 1bit(8wei) or 2bit(16wei)
#define GRF_GPIO_IOMUX GRF_GPIO1L_IOMUX
/*ch=0:GRF_GPIO2L_IOMUX,ch=1:GRF_GPIO1L_IOMUX,ch=2:GRF_GPIO5H_IOMUX,ch=3:GRF_GPIO2L_IOMUX*/
#define I2C_GRF_GPIO_IOMUX (~(0x03<<14))&(~(0x03<<12))|(0x01<<14)|(0x01<<12)
/*CH=0:(~(0x03<<30))&(~(0x03<<28))|(0x01<<30)|(0x01<<28),CH=1:(~(0x03<<14))&(~(0x03<<12))|(0x01<<14)|(0x01<<12),
CH=2:(~(0x03<<24))&(~(0x03<<22))|(0x01<<24)|(0x01<<22),CH=3:(~(0x03<<26))&(~(0x03<<24))|(0x02<<26)|(0x02<<24)*/
/***************************************/
#if defined(SRAM_I2C_CH)
#define CRU_CLKGATE_ADDR  CRU_CLKGATE2_CON
#define  CRU_CLKGATE_BIT SRAM_I2C_CH+11
#else
#define CRU_CLKGATE_ADDR  CRU_CLKGATE0_CON
#define  CRU_CLKGATE_BIT 26
#endif
//#define SRAM_I2C_ADDRBASE (RK29_I2C##SRAM_I2C_CH##_BASE	)

#define I2C_SLAVE_TYPE (((I2C_SLAVE_ADDR_LEN-1)<<4)|((I2C_SLAVE_REG_LEN-1)))

#define uint8 unsigned char
#define uint16 unsigned short
#define uint32 unsigned int
uint32 __sramdata data[5];

#define CRU_CLKGATE0_CON	0x54
#define CRU_CLKGATE2_CON	0x64
#define CRU_CLKGATE1_CON	0x60

#define CRU_CLKSEL0_CON		0x14
#define GRF_GPIO5H_IOMUX	0x74
#define GRF_GPIO2L_IOMUX    	0x58
#define GRF_GPIO1L_IOMUX    	0x50

#define I2C_ARBITR_LOSE_STATUS   (1<<7)  	// Arbitration lose STATUS
#define I2C_RECE_INT_MACKP       (1<<1) 	// Master ACK period interrupt status bit
#define I2C_RECE_INT_MACK        (1)     	// Master receives ACK interrupt status bit

#define I2C_MTXR                (0x0000)       	/* master transmit */
#define I2C_MRXR                (0x0004)        /* master receive */

#define I2C_IER                 (0x0014)        /* interrupt enable control */
#define I2C_ISR                 (0x0018)        /* interrupt status, write 0 to clear */
#define I2C_LCMR                (0x001c)        /* stop/start/resume command, write 1 to set */
#define I2C_LSR                 (0x0020)        /* i2c core status */
#define I2C_CONR                (0x0024)        /* i2c config */
#define I2C_OPR                 (0x0028)        /* i2c core config */
#define I2C_MASTER_TRAN_MODE    (1<<3)
#define I2C_MASTER_PORT_ENABLE  (1<<2)
#define I2C_CON_NACK		(1 << 4)
#define I2C_CON_ACK		(0)
#define I2C_LCMR_RESUME         (1<<2)
#define I2C_LCMR_STOP           (1<<1)
#define I2C_LCMR_START          (1<<0)
#define SRAM_I2C_WRITE 		(0x0ul)
#define SRAM_I2C_READ 		(0x1ul)
#define I2C_MASTER_RECE_MODE 	(0)
#define I2C_CORE_ENABLE         (1<<6)
#define I2C_CORE_DISABLE        (0)

#define SRAM_I2C_CLK_ENABLE()	writel((~0x000000085), RK29_CRU_BASE + CRU_CLKGATE1_CON);
#define SRAM_I2C_CLK_DISABLE()	writel(~0, RK29_CRU_BASE + CRU_CLKGATE1_CON);
#define sram_i2c_set_mode() do{ writel(0x0,SRAM_I2C_ADDRBASE + I2C_ISR);writel(0x0, SRAM_I2C_ADDRBASE + I2C_IER);writel((readl(SRAM_I2C_ADDRBASE + I2C_CONR)&(~(0x1ul<<4)))|I2C_MASTER_TRAN_MODE|I2C_MASTER_PORT_ENABLE, SRAM_I2C_ADDRBASE + I2C_CONR);}while(0)

void __sramfunc sram_i2c_start(void);
void __sramfunc sram_i2c_stop(void);
uint8 __sramfunc sram_i2c_wait_event(void);
uint8 __sramfunc sram_i2c_send_data(uint8 buf, uint8 startbit);
uint8 __sramfunc sram_i2c_read_data(uint8 *buf);
uint8 __sramfunc sram_i2c_slaveAdr(uint16 I2CSlaveAddr, uint8 addressBit, uint8 read_or_write);

void __sramfunc sram_printch(char byte);
void __sramfunc print_Hex(unsigned int hex);


void  i2c_interface_ctr_reg_pread()
{
    readl(SRAM_I2C_ADDRBASE);
    readl(RK29_CRU_BASE);
    readl(RK29_GRF_BASE);
	readl(RK29_GPIO0_BASE);
	readl(RK29_GPIO1_BASE);
	readl(RK29_GPIO2_BASE);
	readl(RK29_GPIO3_BASE);
	readl(RK29_GPIO4_BASE);
	readl(RK29_GPIO5_BASE);
	readl(RK29_GPIO6_BASE);
}

void __sramfunc sram_i2c_delay(int delay_time)
{
    int n = 100 * delay_time;
    while(n--)
    {
        __asm__ __volatile__("");
    }
}


/*-------------------------------------------------------------------------------------------------------
Name      : sram_i2c_init
Desc      : initialize the necessary registers
Params    : channel-determine which I2C bus we used
Return    : none
------------------------------------------------------------------------------------------------------*/
void __sramfunc sram_i2c_init()
{

    //enable cru_clkgate1 clock
    data[0] = readl(RK29_CRU_BASE + CRU_CLKGATE1_CON);
    writel(data[0]&(~0x00000085), RK29_CRU_BASE + CRU_CLKGATE1_CON);
    //set the pclk
    data[1] = readl(RK29_CRU_BASE + CRU_CLKSEL0_CON);
    writel(data[1]&(~(0x07 << 5))&(~(0x03 << 10)) | (0x03 << 10), RK29_CRU_BASE + CRU_CLKSEL0_CON);
    data[2] = readl(RK29_CRU_BASE + CRU_CLKGATE_ADDR);
    writel(data[2]&(~(0x01 << CRU_CLKGATE_BIT)), RK29_CRU_BASE + CRU_CLKGATE_ADDR);
    data[3] = readl(RK29_GRF_BASE + GRF_GPIO_IOMUX);
    writel(data[3]&I2C_GRF_GPIO_IOMUX, RK29_GRF_BASE + GRF_GPIO_IOMUX);


    //reset I2c-reg base
    data[4] = readl(SRAM_I2C_ADDRBASE + I2C_OPR);
    writel(data[4] | (0x1ul << 7), SRAM_I2C_ADDRBASE + I2C_OPR);
    sram_i2c_delay(10);
    writel(data[4]&(~(0x1ul << 7)), SRAM_I2C_ADDRBASE + I2C_OPR);
    writel(0x0, SRAM_I2C_ADDRBASE + I2C_LCMR);
    //disable arq
    writel(0x0, SRAM_I2C_ADDRBASE + I2C_IER);
    writel(data[4]&(~0x03f), SRAM_I2C_ADDRBASE + I2C_OPR);
    //enable i2c core
    writel(data[4] | I2C_CORE_ENABLE ,  SRAM_I2C_ADDRBASE  + I2C_OPR);
}


/*-------------------------------------------------------------------------------------------------------
Name      : sram_i2c_deinit
Desc      : de-initialize the necessary registers
Params    : noe
Return    : none
------------------------------------------------------------------------------------------------------*/
void __sramfunc sram_i2c_deinit(void)
{
    SRAM_I2C_CLK_ENABLE();
    //restore i2c opr reg
    writel(data[4], SRAM_I2C_ADDRBASE + I2C_OPR);

    //restore iomux reg
    writel(data[3], RK29_GRF_BASE + GRF_GPIO_IOMUX);

    //restore cru gate2
    writel(data[2], RK29_CRU_BASE + CRU_CLKGATE_ADDR);

    //restore scu clock reg
    writel(data[1], RK29_CRU_BASE + CRU_CLKSEL0_CON);

    //restore cru gate1
    writel(data[0], RK29_CRU_BASE + CRU_CLKGATE1_CON);
}


/*-------------------------------------------------------------------------------------------------------
Name      : sram_i2c_start
Desc      : start i2c
Params    : none
Return    : none
------------------------------------------------------------------------------------------------------*/
void __sramfunc sram_i2c_start(void)
{
    writel(I2C_LCMR_START | I2C_LCMR_RESUME, SRAM_I2C_ADDRBASE + I2C_LCMR);
}


/*-------------------------------------------------------------------------------------------------------
Name      : sram_i2c_stop
Desc      : stop i2c
Params    : none
Return    : none
------------------------------------------------------------------------------------------------------*/
void __sramfunc sram_i2c_stop(void)
{
    writel(I2C_LCMR_STOP | I2C_LCMR_RESUME, SRAM_I2C_ADDRBASE + I2C_LCMR);
}


/*-------------------------------------------------------------------------------------------------------
Name      : sram_i2c_wait_event
Desc      : wait the ack
Params    : none
Return    : success: return 0; fail: return 1
------------------------------------------------------------------------------------------------------*/
uint8 __sramfunc sram_i2c_wait_event(void)
{
    unsigned int isr, waiteSendDelay = 3;

    isr = readl(SRAM_I2C_ADDRBASE + I2C_ISR);

    while (waiteSendDelay > 0)
    {
        if ((isr & I2C_ARBITR_LOSE_STATUS) != 0)
        {
            writel(0x0, SRAM_I2C_ADDRBASE + I2C_ISR);
            return 1;
        }
        if ((isr & I2C_RECE_INT_MACK) != 0)
        {
            break;
        }
        sram_i2c_delay(1);
        waiteSendDelay--;
    }
    writel(isr & (~0x1ul) , SRAM_I2C_ADDRBASE + I2C_ISR);
    return 0;
}


/*-------------------------------------------------------------------------------------------------------
Name      : sram_i2c_send_data
Desc      : send a byte data
Params    : buf: the data we need to send;
            startbit: startbit=1, send a start signal
                      startbit=0, do not send a start signal
Return    : success: return 0; fail: return 1
------------------------------------------------------------------------------------------------------*/
uint8 __sramfunc sram_i2c_send_data(uint8 buf, uint8 startbit)
{
    writel(buf, SRAM_I2C_ADDRBASE + I2C_MTXR);
    readl(SRAM_I2C_ADDRBASE + I2C_LCMR);
    if(startbit)
    {
        writel(I2C_LCMR_START | I2C_LCMR_RESUME, SRAM_I2C_ADDRBASE + I2C_LCMR);
        sram_i2c_delay(50);
    }
    else
    {
        writel(I2C_LCMR_RESUME, SRAM_I2C_ADDRBASE + I2C_LCMR);
        sram_i2c_delay(50);
    }

    if(sram_i2c_wait_event() != 0)
        return 1;

    return 0;
}


/*-------------------------------------------------------------------------------------------------------
Name      : sram_i2c_send_data
Desc      : receive a byte data
Params    : buf: save the data we received
Return    : success: return 0; fail: return 1
------------------------------------------------------------------------------------------------------*/
uint8 __sramfunc sram_i2c_read_data(uint8 *buf)
{
    unsigned int ret;
    uint8 waitDelay = 3;

    ret = readl(SRAM_I2C_ADDRBASE + I2C_LCMR);
    writel(ret | I2C_LCMR_RESUME, SRAM_I2C_ADDRBASE + I2C_LCMR);

    while(waitDelay > 0)
    {
        ret = readl(SRAM_I2C_ADDRBASE + I2C_ISR);
        if((ret & I2C_ARBITR_LOSE_STATUS) != 0)
            return 1;

        if((ret & I2C_RECE_INT_MACKP) != 0)
            break;

        waitDelay--;
    }

    sram_i2c_delay(50);
    *buf = (uint8)readl(SRAM_I2C_ADDRBASE + I2C_MRXR);
    ret = readl(SRAM_I2C_ADDRBASE + I2C_ISR);
    writel(ret & (~(0x1 << 1)), SRAM_I2C_ADDRBASE + I2C_ISR);
    return 0;
}


/*-------------------------------------------------------------------------------------------------------
Name      : sram_i2c_slaveAdr
Desc      : send the slaveAddr in 10bit mode or in 7bit mode
Params    : I2CSlaveAddr: slave address
            addressbit: high 4bits determine 7 bits or 10 bits slave address锛沴ow 4bits determine 8 bits or 16 bits regAddress
            high 4bits==7, slave address is 7 bits
            high 4bits==10,slave address is 10 bits
            low 4bits==0, slave address is 8 bits
            low 4bits==1, slave address is 16 bits
            read_or_write: read a data or write a data from salve
Return    : sucess return 0, fail return 0
------------------------------------------------------------------------------------------------------*/
uint8 __sramfunc sram_i2c_slaveAdr(uint16 I2CSlaveAddr, uint8 addressBit, uint8 read_or_write)
{
    uint8 retv = 1;

    if((addressBit & 0xf0) == 0x10)               //10bit slave address
    {
        if(sram_i2c_send_data((I2CSlaveAddr >> 7) & 0x06 | 0xf0 | read_or_write, 1) != 0)
            goto STOP;

        sram_i2c_delay(50);
        if(sram_i2c_send_data((I2CSlaveAddr) & 0xff | read_or_write, 0) != 0)
            goto STOP;
    }
    else                //7bit slave address
    {
        if(sram_i2c_send_data((I2CSlaveAddr << 1) | read_or_write, 1) != 0)
            goto STOP;
    }

    retv = 0;

STOP:
    //sram_i2c_stop();
    return retv;
}


/*-------------------------------------------------------------------------------------------------------
Name      : sram_i2c_wirte
Desc      : conduct wirte operation
Params    : I2CSlaveAddr: slave address
            regAddr: slave register address
            *pdataBuff: data we want to write
            size: number of bytes
            addressbit: high 4bits determine 7 bits or 10 bits slave address锛沴ow 4bits determine 8 bits or 16 bits regAddress
            high 4bits==7, slave address is 7 bits
            high 4bits==10,slave address is 10 bits
            low 4bits==0, slave address is 8 bits
            low 4bits==1, slave address is 16 bits
Return    : success: return 0; fail: return 1
------------------------------------------------------------------------------------------------------*/
uint8 __sramfunc sram_i2c_write(uint16 I2CSlaveAddr, uint16 regAddr, void *pdataBuff, uint16 size, uint8 addressBit)
{
    unsigned int ret;
    uint8 *pdata;
    uint8 bit_if16 = (addressBit & 0x0f) ? 0x2 : 0x1;
    uint8 retv = 1;

    pdata = (uint8 *) pdataBuff;

    sram_i2c_set_mode();

    sram_i2c_delay(50);
    if((retv = sram_i2c_slaveAdr(I2CSlaveAddr, addressBit, SRAM_I2C_WRITE)) != 0)
        goto STOP;
    sram_i2c_delay(50);

    do
    {
	bit_if16--;
        if (sram_i2c_send_data((regAddr >> (bit_if16 ? 8 : 0)) & 0xff, 0) != 0)
            goto STOP;

    }
    while(bit_if16);

    sram_i2c_delay(50);

    do
    {
        if (sram_i2c_send_data(*pdata, 0) != 0)
            goto STOP;
        sram_i2c_delay(50);
        pdata++;
        size--;
    }
    while (size);

    retv = 0;
    ret = readl( SRAM_I2C_ADDRBASE + I2C_CONR);
    writel(ret | I2C_CON_NACK, SRAM_I2C_ADDRBASE + I2C_CONR);

STOP:
    sram_i2c_stop();
    return retv;
}


/*-------------------------------------------------------------------------------------------------------
Name      : sram_i2c_read
Desc      : conduct read operation
Params    : I2CSlaveAddr: slave address
            regAddr: slave register address
            *pdataBuff: save the data master received
            size: number of bytes
            addressbit: high 4bits determine 7 bits or 10 bits slave address锛沴ow 4bits determine 8 bits or 16 bits regAddress
            high 4bits==7, slave address is 7 bits
            high 4bits==10,slave address is 10 bits
            low 4bits==0, slave address is 8 bits
            low 4bits==1, slave address is 16 bits
            mode: mode=0, NORMALMODE
                  mode=1, DIRECTMODE
Return    : success: return 0; fail: return 1
------------------------------------------------------------------------------------------------------*/
uint8 __sramfunc sram_i2c_read(uint16 I2CSlaveAddr, uint16 regAddr, void *pdataBuff, uint16 size, uint8 addressBit)
{
    uint8 *pdata;
    unsigned int ret;
    uint8 bit_if16 = (addressBit & 0x0f) ? 0x2 : 0x1;
    uint8 retv = 1;

    pdata = (uint8 *)pdataBuff;
    sram_i2c_set_mode();
    //sram_i2c_delay(50);

    if((retv = sram_i2c_slaveAdr(I2CSlaveAddr, addressBit, SRAM_I2C_WRITE)) != 0)
        goto STOP;
    //sram_i2c_delay(50);

    do
    {
	bit_if16--;
        if (sram_i2c_send_data((regAddr >> (bit_if16 ? 8 : 0)) & 0xff, 0) != 0)
            goto STOP;
    }
    while(bit_if16);

    sram_i2c_delay(50);

    if((retv = sram_i2c_slaveAdr(I2CSlaveAddr, addressBit, SRAM_I2C_READ)) != 0)
        goto STOP;

    writel((ret&(~(0x1 << 3))) | I2C_MASTER_RECE_MODE | I2C_MASTER_PORT_ENABLE, SRAM_I2C_ADDRBASE + I2C_CONR);
    do
    {
        ret = readl(SRAM_I2C_ADDRBASE + I2C_CONR);
        if(size == 1)
        {
            if((sram_i2c_read_data(pdata)) != 0)
                goto STOP;
            writel(ret | I2C_CON_NACK, SRAM_I2C_ADDRBASE + I2C_CONR);
        }
        else
        {
            if((sram_i2c_read_data(pdata)) != 0)
                goto STOP;
            writel(ret & (~(0x1ul << 4)) | I2C_CON_ACK, SRAM_I2C_ADDRBASE + I2C_CONR);
        }
        //sram_i2c_delay(50);
        pdata++;
        size--;
    }
    while (size);

    retv = 0;

STOP:
    sram_i2c_stop();
    return retv;
}
unsigned int __sramfunc rk29_suspend_voltage_set(unsigned int vol)
{
    uint8 slaveaddr;
    uint16 slavereg;
    unsigned int ret, mask, addr;
    uint8 data[2];

    sram_i2c_init();  //init i2c device
    slaveaddr = I2C_SADDR;            //slave device addr
    slavereg = 0x4003;            // reg addr

    data[0] = 0x00;       //clear i2c when read
    data[1] = 0x00;
    ret = sram_i2c_read(slaveaddr, slavereg, data, SRAM_I2C_DATA_BYTE, I2C_SLAVE_TYPE);
   // print_Hex(data[0]); //read data saved in data
   // print_Hex(data[1]); //read data saved in data
    //sram_printch('\n');

    data[0] |= (0x1<<6);        //write data
    sram_i2c_write(slaveaddr, slavereg, data, SRAM_I2C_DATA_BYTE, I2C_SLAVE_TYPE);//wm831x enter sleep mode
    sram_i2c_delay(50);

    sram_i2c_deinit();  //deinit i2c device

}

void __sramfunc rk29_suspend_voltage_resume(unsigned int vol)
{
    uint8 slaveaddr;
    uint16 slavereg;
    unsigned int ret, mask, addr;
    uint8 data[2];

    sram_i2c_init();  //init i2c device
    slaveaddr = I2C_SADDR;            //slave device addr
    slavereg = 0x4003;            // reg addr

    data[0] = 0x00;       //clear i2c when read
    data[1] = 0x00;
    ret = sram_i2c_read(slaveaddr, slavereg, data, SRAM_I2C_DATA_BYTE, I2C_SLAVE_TYPE);
   // print_Hex(data[0]); //read data saved in data
   // print_Hex(data[1]); //read data saved in data
   // sram_printch('\n');

    data[0] &= ~(0x1<<6);        //write data
    sram_i2c_write(slaveaddr, slavereg, data, SRAM_I2C_DATA_BYTE, I2C_SLAVE_TYPE);//wm831x exit sleep mode
    sram_i2c_delay(50);

    sram_i2c_deinit();  //deinit i2c device

}

#endif
#endif
