/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i2c.h>
//#include <asm/semaphore.h>
#include <linux/gpio.h>
#include <mach/gpio.h>
#include <linux/suspend.h>
#include <linux/input.h>
#include <mach/hardware.h>

#include <linux/semaphore.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include <mach/iomux.h>

#include <mach/spi_fpga.h>

extern void rk2818_mux_api_set(char *name, unsigned int mode);
extern int lp8725_ldo_en(uint8_t ldo,uint8_t enble);
extern int lp8725_set_ldo_vol(uint8_t ldon,uint16_t vol);
extern int lp8725_set_lilo_vol(uint8_t lilon,uint16_t vol);
extern int lp8725_lilo_en(uint8_t lilon,uint8_t enble);


struct Ctp_it7250_data {
    char  status;
    char  curr_tate;
	struct input_dev *input_dev;
	struct i2c_client *client;
	struct delayed_work delaywork;
	int irq;  /* Our chip IRQ */
	u32 temp_x;
	u32 temp_y;
};
static struct i2c_client *Ctp_it7250_client;

#define Ctp_it7250_GPIO_INT     RK2818_PIN_PE1

#if 0
#define rk28printk(x...) printk(x)
#else
#define rk28printk(x...) 
#endif

 struct KeyInfo
{
	int value;
	int key;
};

static struct KeyInfo panel_key_info[]={
{0x01, KEY_HOME},
{0x02, KEY_MENU},
{0x03, KEY_BACK},
{0x04, KEY_SEARCH},
};

#define MAX_FINGER_NUM				3
//#define DEVICE_ADDRESS					0x8C
#define COMMAND_BUFFER_INDEX			0x20
#define QUERY_BUFFER_INDEX				0x80
#define COMMAND_RESPONSE_BUFFER_INDEX	0xA0
#define POINT_BUFFER_INDEX				0xE0
#define QUERY_SUCCESS					0x00
#define QUERY_BUSY					0x01
#define QUERY_ERROR					0x02
#define QUERY_POINT					0x80


static u8 gpucPointBuffer[14];
static u32 gpdwSampleX[3],gpdwSampleY[3],gpdwPressure[1];

static int Ctp_it7250_rx_data(struct i2c_client *client, u8 reg,u8* rxData, int length)
{
#if 0
   
  int ret;
    struct i2c_adapter *adap;int i;
    struct i2c_msg msgs[2];
    if(!client)
		return ret;    
    adap = client->adapter;
	
    //发送寄存器地址
    msgs[0].addr = client->addr;
    msgs[0].buf = &reg;
    msgs[0].flags = client->flags;
    msgs[0].len =1;
    msgs[0].scl_rate = 400*1000;
    //接收数据
    
//    rk28printk("i2c addr=0x%x \r\n",msgs[0].addr);
 //   rk28printk("msgs[0].buf = 0x%x rxData=0x%x\n",*(msgs[0].buf),*rxData);
	
	msgs[1].buf = rxData;
    msgs[1].addr = client->addr;
    msgs[1].flags = client->flags | I2C_M_RD;
    msgs[1].len = length;
    msgs[1].scl_rate = 400*1000;

    ret = i2c_transfer(adap, msgs, 2);
    //DBG("**has run at %s %d ret=%d**\n",__FUNCTION__,__LINE__,ret);
//	rk28printk("\r\n msgs[1].buf = 0x%x ret=%d\n",*(msgs[1].buf),ret);


	//for (i=0;i<length;i++)
	//	rk28printk("rxData[%d]=%d \r\n",i,rxData[i]);

#else
//int i;
return  i2c_master_reg8_recv(client, reg, rxData, length, 400 * 1000);
//for (i=0;i<length;i++)
//	rk28printk("rxData[%d]=%d \r\n",i,rxData[i]);
#endif
	
}

static int Ctp_it7250_tx_data(struct i2c_client *client, u8 reg,char *txData, int length)
{
#if 0
    int ret,i;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	
	u8 buf[128];//128
//	rk28printk("reg=0x%x txData=0x%x  \r\n",reg,*txData);
	buf[0]=reg;
	for (i=0;i<length;i++)
		{
		buf[i+1]=*txData++;
		rk28printk("buf[%d]=0x%x   ",i+1,buf[i+1]);
		}
	rk28printk("\r\n");
//	rk28printk("buf[0]=0x%x buf[1]=0x%x",buf[0],buf[1]);
	msg.addr = client->addr;
//rk28printk("i2c addr=0x%x",msg.addr);
	msg.buf =&buf[0];
	msg.len = length+1;//+1 means add the reg length;by roberts
	msg.flags = client->flags;
	msg.scl_rate = 400*1000;
    
	ret = i2c_transfer(adap, &msg, 1);
return ret;
#else
 return i2c_master_reg8_send(client, reg, txData, length, 400 * 1000);
#endif




}



bool  ReadQueryBuffer(struct i2c_client *client, u8* pucData)
{
	return Ctp_it7250_rx_data(client, QUERY_BUFFER_INDEX, pucData, 1);
}

bool ReadCommandResponseBuffer(struct i2c_client *client, u8* pucData, unsigned int unDataLength)
{
	return Ctp_it7250_rx_data(client, COMMAND_RESPONSE_BUFFER_INDEX, pucData, unDataLength);
}

bool ReadPointBuffer(struct i2c_client *client, u8* pucData)
{
	return Ctp_it7250_rx_data(client, POINT_BUFFER_INDEX, pucData, 14);
}

bool WriteCommandBuffer(struct i2c_client *client, u8* pucData, unsigned int unDataLength)
{
	return Ctp_it7250_tx_data(client, COMMAND_BUFFER_INDEX, pucData, unDataLength);
}

static int Ctp_it7250_touch_open(struct input_dev *idev)
{
	
//struct Ctp_it7250_data *Ctp_it7250 = (struct Ctp_it7250_data *)i2c_get_clientdata(client);


//BTN_TOUCH =0 means no touch ;by robert
	input_report_key(idev,BTN_TOUCH, 0);
	input_sync(idev);

	return 0;
}

static void Ctp_it7250_touch_close(struct input_dev *idev)
{
return ;
}

static irqreturn_t Ctp_it7250_touch_irq(int irq, void *dev_id)
{	
	struct Ctp_it7250_data *Ctp_it7250 = dev_id;
	//printk("%s++++ %d \r\n",__FUNCTION__,__LINE__);
	//rk28printk("%s++++ %d \r\n",__FUNCTION__,__LINE__);
	disable_irq_nosync(irq);
	//rk28printk("%s++++ %d irq=%d\r\n",__FUNCTION__,__LINE__,irq);
	schedule_delayed_work(&Ctp_it7250->delaywork,msecs_to_jiffies(10));	
	
	return IRQ_HANDLED; 
		
}



static int ts_input_init(struct i2c_client *client)
{
	int ret = -1,i;
	struct Ctp_it7250_data *Ctp_it7250;
	Ctp_it7250 = i2c_get_clientdata(client);
	/* register input device */
	Ctp_it7250->input_dev = input_allocate_device();
	if (Ctp_it7250->input_dev == NULL) {
		pr_emerg( "%s: failed to allocate input dev\n",
			__FUNCTION__);
		return -ENOMEM;
	}
rk28printk("+++++++     %s+++++++\n", __FUNCTION__);
	Ctp_it7250->input_dev->name = "CTS_Ctp_it7250";
	Ctp_it7250->input_dev->phys = "CTS_Ctp_it7250/input1";
	Ctp_it7250->input_dev->dev.parent = &client->dev;
     //no need to open & close it,it will do it automaticlly;noted by robert
	Ctp_it7250->input_dev->open = Ctp_it7250_touch_open;
	Ctp_it7250->input_dev->close = Ctp_it7250_touch_close;

	__set_bit(EV_ABS, Ctp_it7250->input_dev->evbit);
	__set_bit(ABS_X, Ctp_it7250->input_dev->absbit);
	__set_bit(ABS_Y, Ctp_it7250->input_dev->absbit);

	__set_bit(EV_SYN, Ctp_it7250->input_dev->evbit);
	__set_bit(EV_KEY, Ctp_it7250->input_dev->evbit);
	__set_bit(BTN_TOUCH, Ctp_it7250->input_dev->keybit);

for (i = 0; i < ARRAY_SIZE(panel_key_info); i++)
{rk28printk("ts_input_init  i=%d\r\n",i);
	__set_bit(panel_key_info[i].key,Ctp_it7250->input_dev->keybit);
}
	//__clear_bit(0, input_dev->keybit);

	input_set_abs_params(Ctp_it7250->input_dev, ABS_X, 0, 1000, 0, 0);
	input_set_abs_params(Ctp_it7250->input_dev, ABS_Y, 0, 1000, 0, 0);
//pr_emerg("+++++++ %s\n", __FUNCTION__); 
	ret = input_register_device(Ctp_it7250->input_dev);
	if (ret) {
		pr_emerg(
			"%s: unabled to register input device, ret = %d\n",
			__FUNCTION__, ret);
		return ret;
	}
	rk28printk("+++++++     %s+++++++END\n", __FUNCTION__);
	return 0;

}

#if 0
static void CTS_configure_pin(struct i2c_client *client)
{
	//add  reset pin;but we not used it ;robert
	//had already do it it spi_gpio.c
	//end add	
	//RESET THE CTP;note by robert
	spi_gpio_set_pinlevel(SPI_GPIO_P2_15, SPI_GPIO_HIGH);
	mdelay(5);
	spi_gpio_set_pinlevel(SPI_GPIO_P2_15, SPI_GPIO_LOW);
	mdelay(5);
	spi_gpio_set_pinlevel(SPI_GPIO_P2_15, SPI_GPIO_HIGH);
	mdelay(5);
}
#endif


static int Ctp_it7250_init_irq(struct i2c_client *client)
{
	struct Ctp_it7250_data *Ctp_it7250;
	int ret;
	
	Ctp_it7250 = i2c_get_clientdata(client);
	#if 1
	if ( !gpio_is_valid(client->irq)) {
		rk28printk("+++++++++++gpio_is_invalid\n");
		return -EINVAL;
	}
	ret = gpio_request(client->irq, "Ctp_it7250_int");
	if (ret) {
		rk28printk( "failed to request Ctp_it7250_init_irq GPIO%d\n",gpio_to_irq(client->irq));
		return ret;
	}
#if 1
ret = gpio_direction_input(client->irq);
	if (ret) {
		rk28printk("failed to set CTS_configure_pin gpio input\n");
		//goto err_free_gpio;
	}
	gpio_pull_updown(client->irq,GPIOPullUp);
#endif
	rk28printk("%s gpio_to_irq(%d) is %d\n",__FUNCTION__,client->irq,gpio_to_irq(client->irq));
		
	Ctp_it7250->irq = gpio_to_irq(client->irq);
	#endif
	ret = request_irq(Ctp_it7250->irq, Ctp_it7250_touch_irq, IRQF_TRIGGER_LOW, client->dev.driver->name, Ctp_it7250);
	rk28printk("%s request irq is %d,irq1 is %d,ret is  0x%x\n",__FUNCTION__,client->irq,Ctp_it7250->irq,ret);
	if (ret ) {
		rk28printk(KERN_ERR "Ctp_it7250_init_irq: request irq failed,ret is %d\n",ret);
        return ret;
	}
	return true;	
}

// ================================================================================
// Function Name --- GetFirmwareInformation
// Description --- Get firmware information
// Input --- NULL
//Output --- return true if the command execute successfully, otherwuse return false.
// ================================================================================
bool GetFirmwareInformation(struct i2c_client *client)
{
	u8 ucWriteLength, ucReadLength;
	u8 pucData[128];
	u8 ucQuery;
int i;
	ucWriteLength = 2;
	ucReadLength = 0x09;
	pucData[0] = 0x01;
	pucData[1] = 0x00;

	// Query
	do
	{
		if(!ReadQueryBuffer(client, &ucQuery))
		{
			ucQuery = QUERY_BUSY;
		}
	}while(ucQuery & QUERY_BUSY);

	// Write Command
	if(!WriteCommandBuffer(client, pucData, ucWriteLength))
	{
		return false;
	}

	// Query
	do
	{
		if(!ReadQueryBuffer(client, &ucQuery))
		{
			ucQuery = QUERY_BUSY;
		}
	}while(ucQuery & QUERY_BUSY);
	pucData[5]= 0 ;
	 pucData[6]= 0 ;
	 pucData[7]= 0 ;
	 pucData[8]= 0;
	// Read Command Response
	if(!ReadCommandResponseBuffer(client, pucData, ucReadLength))
	{
		return false;
	}


	
for (i =0;i<ucReadLength;i++)
	rk28printk("GetFirmwareInformation pucData[%d]=%d \r\n",i,pucData[i]);
	if(pucData[5]== 0 
	&& pucData[6] == 0 
	&& pucData[7] == 0 
	&& pucData[8] == 0) 
	{
		// There is no flash code
		rk28printk("There is no flash code \r\n");
		return false;
	}

	return true;
}

// ================================================================================
// Function Name --- SetInterruptNotification
// Description --- Set It7260 interrupt mode
// Input --- 
//	ucStatus- the interrupt status
//	ucType- the interrupt type
//Output --- return true if the command execute successfully, otherwuse return false.
// ================================================================================
bool SetInterruptNotification(struct i2c_client *client, u8 ucStatus, u8 ucType)
{
	u8 ucWriteLength, ucReadLength;
	u8 pucData[128];
	u8 ucQuery;
int i;
	ucWriteLength = 4;// 2
	ucReadLength = 2;
	pucData[0] = 0x02;
	pucData[1] = 0x04;
	pucData[2] = ucStatus;
	pucData[3] = ucType;

	// Query
	do
	{
		if(!ReadQueryBuffer(client, &ucQuery))
		{
			ucQuery = QUERY_BUSY;
		}
	}while(ucQuery & QUERY_BUSY);

	// Write Command
	if(!WriteCommandBuffer(client, pucData, ucWriteLength))
	{
		return false;
	}

	// Query
	do
	{
		if(!ReadQueryBuffer(client, &ucQuery))
		{
			ucQuery = QUERY_BUSY;
		}
	}while(ucQuery & QUERY_BUSY);

	// Read Command Response
	if(!ReadCommandResponseBuffer(client, pucData, ucReadLength))
	{
		return false;
	}

for (i =0;i<ucReadLength;i++)
	rk28printk("SetInterruptNotification pucData[%d]=0x%x \r\n",i,pucData[i]);
	if(*(u16*) pucData != 0) 
	{
		return false;
	}
	return true;
}


// ================================================================================
// Function Name --- IdentifyCapSensor
// Description --- Identify Capacitive Sensor information
// Input --- NULL
//Output --- return true if the command execute successfully, otherwuse return false.
// ================================================================================
bool IdentifyCapSensor(struct i2c_client *client)
{
	u8 ucWriteLength, ucReadLength;
	u8 pucData[128];
	u8 ucQuery=0;int i;

	ucWriteLength = 1;
	ucReadLength = 0x0A;
	pucData[0] = 0x00;
	rk28printk("%s\r\n",__FUNCTION__);
	// Query
	do
	{//printk("first wait 111\r\n");
		if(!ReadQueryBuffer(client, &ucQuery))
		{
		
			rk28printk("first wait \r\n");
			//means we use resister touchscreen
			goto error_out;
			ucQuery = QUERY_BUSY;
		}
		rk28printk("%s ucQuery!!!!=0x%x \r\n",__FUNCTION__,ucQuery);
		if (0xff == ucQuery)
			goto error_out;
		mdelay(500);
	}while(ucQuery & QUERY_BUSY);


	// Write Command
	//rk28printk("%s11\r\n",__FUNCTION__);
	pucData[0] = 0x00;ucWriteLength = 1;
	if(!WriteCommandBuffer(client, pucData, ucWriteLength))
	{
		rk28printk("WriteCommandBuffer false \r\n");
	//	return false;
	}
//rk28printk("show1 %s\r\n",__FUNCTION__);
	// Query
	do
	{
		if(!ReadQueryBuffer(client, &ucQuery))
		{
			rk28printk("second wait \r\n");
			ucQuery = QUERY_BUSY;
		}rk28printk("%s ucQuery1=0x%x \r\n",__FUNCTION__,ucQuery);
	}while(ucQuery & QUERY_BUSY);
//rk28printk("show2 %s\r\n",__FUNCTION__);
	// Read Command Response
	for (i=0;i<ucReadLength;i++)
		{pucData[i]=0x0;
		rk28printk("pucData[%d]=%d \r\n",i,pucData[i]);
		}
	if(!ReadCommandResponseBuffer(client, pucData, ucReadLength))
	{
		rk28printk("ReadCommandResponseBuffer false \r\n");
	//	return false;
	}
//rk28printk("show %s\r\n",__FUNCTION__);
for (i=0;i<ucReadLength;i++)
		rk28printk("pucData[%d]=%d \r\n",i,pucData[i]);
	rk28printk("pucData=%c %c %c %c %c %c %c \r\n",pucData[1],pucData[2],pucData[3],pucData[4],pucData[5],pucData[6],pucData[7]);

	return true;


error_out:
	return false;
}

// ================================================================================
// Function Name --- Get2DResolutions
// Description --- Get the resolution of X and Y axes
// Input --- 
//	pwXResolution - the X resolution
//	pwYResolution - the Y resolution
//	pucStep - the step
//Output --- return true if the command execute successfully, otherwuse return false.
// ================================================================================
bool Get2DResolutions(struct i2c_client *client, u32 *pwXResolution, u32*pwYResolution, u8 *pucStep)
{
	u8 ucWriteLength, ucReadLength;
	u8 pucData[128];
	u8 ucQuery;
int i;

	ucWriteLength = 3;
	ucReadLength = 0x07;
	pucData[0] = 0x01;
	pucData[1] = 0x02;
	pucData[2] = 0x00;

	// Query
	do
	{
		if(!ReadQueryBuffer(client, &ucQuery))
		{
			ucQuery = QUERY_BUSY;
		}
	}while(ucQuery & QUERY_BUSY);

	// Write Command
	rk28printk("%s WriteCommandBuffer\r\n",__FUNCTION__);
	if(!WriteCommandBuffer(client, pucData, ucWriteLength))
	{
		return false;
	}

	// Query
	do
	{
		if(!ReadQueryBuffer(client, &ucQuery))
		{
			ucQuery = QUERY_BUSY;
		}
	}while(ucQuery & QUERY_BUSY);
rk28printk("%s ReadCommandResponseBuffer\r\n",__FUNCTION__);
	// Read Command Response
	if(!ReadCommandResponseBuffer(client, pucData, ucReadLength))
	{
		return false;
	}
rk28printk("%s ReadCommandResponseBuffer EDN\r\n",__FUNCTION__);

 for (i=0;i<ucReadLength;i++)
	rk28printk("pucData[%d] = 0x%x \r\n",i,pucData[i]);
 
	if(pwXResolution != NULL) 
	{
		*pwXResolution = (pucData[2] | (pucData[3] << 8));
	}
	if(pwYResolution!= NULL) 
	{
		* pwYResolution = (pucData[4] | (pucData[5] << 8));
	}
	if(pucStep!= NULL) 
	{
		* pucStep = pucData[6];
	}
	rk28printk("%s x res=%d y res=%d !\r\n",__FUNCTION__,*pwXResolution,* pwYResolution);
	return true;
}

// *******************************************************************************************
// Function Name: CaptouchMode
// Description: 
//   This function is mainly used to initialize cap-touch controller to active state.
// Parameters: 
//   dwMode -- the power state to be entered
// Return value: 
//   return zero if success, otherwise return non zero value
// *******************************************************************************************
int CaptouchMode(struct i2c_client *client, u8 dwMode)
{
	u8 ucQueryResponse;
	u8 pucCommandBuffer[128];

	do
	{
		ReadQueryBuffer(client, &ucQueryResponse);
	}
	while(ucQueryResponse & QUERY_BUSY);

	pucCommandBuffer[0] = 0x04;
	pucCommandBuffer[1] = 0x00;
	switch(dwMode)
	{
		case 0x00:
			pucCommandBuffer[2] = 0x00;
			break;
		case 0x01:
			pucCommandBuffer[2] = 0x01;
			break;
		case 0x02:
			pucCommandBuffer[2] = 0x02;
			break;
		default:
			return -1;
	}

	if(!WriteCommandBuffer(client, pucCommandBuffer,3))
	{
		return -1;
	}

	return 0;
}

// *******************************************************************************************
// Function Name: CaptouchReset
// Description: 
//   This function is mainly used to reset cap-touch controller by sending reset command.
// Parameters: NULL
// Return value: 
//   return TRUE if success, otherwise return FALSE
// *******************************************************************************************
int CaptouchReset(struct i2c_client *client)
{
	u8 ucQueryResponse;
	u8 pucCommandBuffer[128];

	do
	{
		ReadQueryBuffer(client, &ucQueryResponse);
	}
	while(ucQueryResponse & QUERY_BUSY);

	pucCommandBuffer[0] = 0x6F;
	if(!WriteCommandBuffer(client, pucCommandBuffer,1))
	{
		return -1;
	}
	mdelay(200);
	

	do
	{
		ReadQueryBuffer(client, &ucQueryResponse);
	}
	while(ucQueryResponse & QUERY_BUSY);


	if(!ReadCommandResponseBuffer(client, pucCommandBuffer,2))
	{
		return -1;
	}

	if(pucCommandBuffer[0] == 0 && pucCommandBuffer[1] == 0)
	{
		return 0;
	}

	return -1;
}



// *******************************************************************************************
// Function Name: CaptouchHWInitial
// Description: 
//   This function is mainly used to initialize cap-touch controller to active state.
// Parameters: NULL
// Return value: 
//   return zero if success, otherwise return non zero value
// *******************************************************************************************
int CaptouchHWInitial(struct i2c_client *client)
{
	u32 wXResolution=0,wYResolution=0;
	u8 ucStep=0;
	if (!IdentifyCapSensor(client))
	{
		rk28printk("%s IdentifyCapSensor error \r\n",__FUNCTION__);
		printk("%s IdentifyCapSensor error \r\n",__FUNCTION__);
		return false;
		//goto resetagin;
}
	#if 1
if (!GetFirmwareInformation (client))
	{
	rk28printk("%s GetFirmwareInformation error \r\n",__FUNCTION__);
	printk("%s GetFirmwareInformation error \r\n",__FUNCTION__);
	//	goto resetagin;
}

	if (!Get2DResolutions(client, &wXResolution, &wYResolution, &ucStep))
	{
	rk28printk("%s Get2DResolutions error \r\n",__FUNCTION__);
	printk("%s Get2DResolutions error \r\n",__FUNCTION__);
	//	goto resetagin;
}

//no need to set interrupt mode because firmware has done that;note by robert
	#if 0
	if (!SetInterruptNotification(client, 0x01, 0x00))
	{
	rk28printk("%s SetInterruptNotification error \r\n",__FUNCTION__);
	goto resetagin;
}
	#endif
	//note end
#endif
	return true;

#if 0
resetagin:
	if (!CaptouchReset(client))
		rk28printk("CaptouchReset success \r\n");
	mdelay(100);
	#endif
//	if (!CaptouchMode(client, 0x00))
	//	rk28printk("CaptouchMode success \r\n");
}


// *******************************************************************************************
// Function Name: CaptouchGetSampleValue
// Description: 
//   This function is mainly used to get sample values which shall contain x, y position and pressure. 
// Parameters:
//   pdwSampleX -- the pointer of returned X coordinates
//   pdwSampleY -- the pointer of returned Y coordinates
//   pdwPressure -- the pointer of returned pressures
// Return value: 
//   The return value is the number of sample points. Return 0 if failing to get sample. The maximum //   return value is 10 in normal case. If the CTP controller does not support pressure measurement, //   the return value is the sample values OR with PRESSURE_INVALID.
// *******************************************************************************************
int CaptouchGetSampleValue(u32* pdwSampleX, u32* pdwSampleY, u32* pdwPressure)
{
	int nRet;
	int i;

	if(gpucPointBuffer[1] & 0x01)
	{
		return MAX_FINGER_NUM + 1;
	}

	nRet = 0;
	for(i = 0; i < MAX_FINGER_NUM; i++)
	{
		if(gpucPointBuffer[0] & (1 << i))
		{
			nRet++;
			#if 0
			pdwSampleX[i] = ((u32)(gpucPointBuffer[i * 4 + 3] & 0x0F) << 8) + 
(u32)gpucPointBuffer[i * 4 + 2];;
			pdwSampleY[i] = ((u32)(gpucPointBuffer[i * 4 + 3] & 0xF0) << 4) + 
(u32)gpucPointBuffer[i * 4 + 4];
			pdwPressure[i] = (u32)(gpucPointBuffer[i * 4 + 5] & 0x0F);
			#else 
			pdwSampleX[i] = ((u32)(gpucPointBuffer[i * 4 + 3] & 0x0F) << 8) | 
gpucPointBuffer[i * 4 + 2];
			pdwSampleY[i] = ((u32)(gpucPointBuffer[i * 4 + 3] & 0xF0) << 4) | 
gpucPointBuffer[i * 4 + 4];
			pdwPressure[i] = (u32)(gpucPointBuffer[i * 4 + 5] & 0x0F);

		//	rk28printk("%s x[%d]=%d  y[%d]=%d \r\n",__FUNCTION__,i,pdwSampleX[i],i,pdwSampleY[i]);
			#endif
		}
		else
		{
			pdwSampleX[i] = 0;
			pdwSampleY[i] = 0;
			pdwPressure[i] = 0;
		}
	}
	//add by robert for test
	#if 0
	for(i = 0; i < MAX_FINGER_NUM; i++)
	{
		rk28printk("%s x[%d]=%d  y[%d]=%d \r\n",__FUNCTION__,0,pdwSampleX[0],0,pdwSampleY[0]);
	}
	#endif
	return nRet;
}

// *******************************************************************************************
// Function Name: CaptouchGetGesture
// Description: 
//   This function is mainly used to initialize cap-touch controller to active state.
// Parameters: NULL
// Return value: 
//   return gesture ID
// *******************************************************************************************
int CaptouchGetGesture(void)
{
	return (int)gpucPointBuffer[1];
}

static void  Ctp_it7250_delaywork_func(struct work_struct  *work)
{
	
	u8 ucQueryResponse;
	u32 dwTouchEvent;
	struct delayed_work *delaywork = container_of(work, struct delayed_work, work);
	struct Ctp_it7250_data *Ctp_it7250 = container_of(delaywork, struct Ctp_it7250_data, delaywork);	
	struct i2c_client *client = Ctp_it7250->client;
	
	int PE1status = 0;

//	rk28printk("%s++++ %d \r\n",__FUNCTION__,__LINE__);


	//PE1status =  gpio_get_value(Ctp_it7250_GPIO_INT);
	PE1status =  gpio_get_value(client->irq);
	//  PE1status 为低，表示低电平中断有效 
	if (!PE1status)
		{
	//	rk28printk("%s PE1status low!!  \r\n",__FUNCTION__);
	if(!ReadQueryBuffer(client, &ucQueryResponse))
	{rk28printk("%s++++ %d  ucQueryResponse=0x%x \r\n",__FUNCTION__,__LINE__,ucQueryResponse);
		return false;
	}
//rk28printk("%s++++ %d  ucQueryResponse=0x%x \r\n",__FUNCTION__,__LINE__,ucQueryResponse);
	// Touch Event
	if(ucQueryResponse & QUERY_POINT)
	{//rk28printk("%s++++ %d  \r\n",__FUNCTION__,__LINE__);
		if(!ReadPointBuffer(client, gpucPointBuffer))
		{rk28printk("%s++++ %d  \r\n",__FUNCTION__,__LINE__);
			return false;
		}
//rk28printk("%s++++ %d  ucQueryResponse=0x%x gpucPointBuffer=0x%x\r\n",__FUNCTION__,__LINE__,ucQueryResponse,gpucPointBuffer[0] & 0xF0);
		switch(gpucPointBuffer[0] & 0xF0)
		{
			case 0x00:
				dwTouchEvent = CaptouchGetSampleValue(gpdwSampleX, gpdwSampleY, gpdwPressure);
				if(dwTouchEvent == 0)
				{
					//SynchroSystemEvent(SYSTEM_TOUCH_EVENT_FINGER_RELEASE);
					rk28printk("TOUCH Release  !!\r\n");
					input_report_key(Ctp_it7250->input_dev,BTN_TOUCH, 0);
					input_sync(Ctp_it7250->input_dev);
				}
				else if(dwTouchEvent <=MAX_FINGER_NUM) //CTP_MAX_FINGER_NUMBER)
				{
					//SynchroSystemEvent(SYSTEM_TOUCH_EVENT_FINGER_ASSERT);
			if ((abs(Ctp_it7250->temp_x -gpdwSampleX[0])>10)  ||( abs(Ctp_it7250->temp_y-gpdwSampleY[0])>10))
					{
					input_report_abs(Ctp_it7250->input_dev, ABS_X, gpdwSampleX[0]);// & 0xfff
					input_report_abs(Ctp_it7250->input_dev, ABS_Y, gpdwSampleY[0]); //& 0xfff
					input_report_key(Ctp_it7250->input_dev,BTN_TOUCH, 1);
					input_sync(Ctp_it7250->input_dev);
					rk28printk("x=%d  y=%d \r\n",gpdwSampleX[0],gpdwSampleY[0]);
					Ctp_it7250->temp_x=gpdwSampleX[0];
					Ctp_it7250->temp_y=gpdwSampleY[0];
					}
				else
					rk28printk("the same \r\n");
				}
				else
				{
					rk28printk("%s SYSTEM_TOUCH_EVENT_PALM_DETECT \r\n",__FUNCTION__);
					//SynchroSystemEvent(SYSTEM_TOUCH_EVENT_PALM_DETECT);
				}
				break;
			case 0x80:
				dwTouchEvent = CaptouchGetGesture();
				//SynchroSystemEvent(SYSTEM_GESTURE_EVENT);
				rk28printk("gesture:0x%x \r\n",dwTouchEvent);
				break;
			//for keypad below
			case 0x40:
				rk28printk("ID of button is 0x%x status is 0x%x\r\n",gpucPointBuffer[1],gpucPointBuffer[2]);
				switch (gpucPointBuffer[1]) 
					{case 0x01:
						rk28printk("home \r\n");
						if (gpucPointBuffer[2])
							input_report_key(Ctp_it7250->input_dev, panel_key_info[(gpucPointBuffer[1]-1)].key,1);
						else
							input_report_key(Ctp_it7250->input_dev, panel_key_info[(gpucPointBuffer[1]-1)].key,0);
						break;
					case 0x02:
						rk28printk("menu \r\n");
						if (gpucPointBuffer[2])
							input_report_key(Ctp_it7250->input_dev, panel_key_info[(gpucPointBuffer[1]-1)].key,1);
						else
							input_report_key(Ctp_it7250->input_dev, panel_key_info[(gpucPointBuffer[1]-1)].key,0);
						break;
					case 0x03:
						rk28printk("back \r\n");
						if (gpucPointBuffer[2])
							{
							rk28printk("back down key=%d\r\n",panel_key_info[(gpucPointBuffer[1]-1)].key);
							input_report_key(Ctp_it7250->input_dev, panel_key_info[(gpucPointBuffer[1]-1)].key,1);
							}
						else
							{
							rk28printk("back up key=%d\r\n",panel_key_info[(gpucPointBuffer[1]-1)].key);
							input_report_key(Ctp_it7250->input_dev, panel_key_info[(gpucPointBuffer[1]-1)].key,0);
							}
						break;
					case 0x04:
						rk28printk("find \r\n");
						if (gpucPointBuffer[2])
							{
							rk28printk("find down key=%d\r\n",panel_key_info[(gpucPointBuffer[1]-1)].key);
							input_report_key(Ctp_it7250->input_dev, panel_key_info[(gpucPointBuffer[1]-1)].key,1);
							}
						else
							{
							rk28printk("find up key=%d\r\n",panel_key_info[(gpucPointBuffer[1]-1)].key);
							input_report_key(Ctp_it7250->input_dev, panel_key_info[(gpucPointBuffer[1]-1)].key,0);
							}
						break;		
					default:
							rk28printk("shit default \r\n");
						break;
					}
				break;
			default:
				rk28printk("default \r\n");
				break;
		}
	}
	else if (ucQueryResponse & QUERY_ERROR)
		{
		if (!CaptouchReset(client))
		rk28printk("!! CaptouchReset success \r\n");
		mdelay(100);
	//if (!CaptouchMode(client, 0x00))
		//rk28printk("!! CaptouchMode success \r\n");
		}
	}
//	rk28printk("enable ctp_irq=%d \r\n",Ctp_it7250->irq);
	
	enable_irq(Ctp_it7250->irq);
	//不是查询模式，不需要轮询
//schedule_delayed_work(&Ctp_it7250->delaywork,msecs_to_jiffies(1000));

}

 static int  Ctp_it7250_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct Ctp_it7250_data *Ctp_it7250;

	Ctp_it7250_client = client;
	rk28printk("+++++++     %s+++++++\n", __FUNCTION__);
	Ctp_it7250 = kzalloc(sizeof(struct Ctp_it7250_data), GFP_KERNEL);
	if (!Ctp_it7250) 
		{
		rk28printk("[Ctp_it7250_probe]:alloc data failed.\n");
		return  -ENOMEM;
		}

//	INIT_WORK(&Ctp_it7250->irq_work, Ctp_it7250_irq_worker);
	INIT_DELAYED_WORK(&Ctp_it7250->delaywork, Ctp_it7250_delaywork_func);

	Ctp_it7250->client = client;
	i2c_set_clientdata(client, Ctp_it7250);
	
	if (!CaptouchHWInitial(client))
		goto  err_free_mem;
	Ctp_it7250_init_irq(client);	
	ts_input_init(client);
//	CTS_configure_pin(client);

#if 0
	lp8725_lilo_en(2,0);
	mdelay(100);

	lp8725_lilo_en(2,1);
	mdelay(100);
	lp8725_set_lilo_vol(2,300);
	mdelay(5);
#endif

//不是查询模式，不需要轮询
//schedule_delayed_work(&Ctp_it7250->delaywork,msecs_to_jiffies(50));


	rk28printk("+++++++     %s+++++++\n", __FUNCTION__);
	return 0;
	err_free_mem:
 	kfree(Ctp_it7250);
	 return false;

}



static int Ctp_it7250_remove(struct i2c_client *client)
{
	
	return 0;
}



#ifdef	CONFIG_PM
static int Ctp_it7250_suspend(struct i2c_client *client, pm_message_t state)
{//pr_emerg("\n irq1=%d \n",irq1);
	struct Ctp_it7250_data *Ctp_it7250 = (struct Ctp_it7250_data *)i2c_get_clientdata(client);

	rk28printk("%s\n",__func__);

	CaptouchMode(client, 2);
	disable_irq(Ctp_it7250->irq);

	return 0;
}

static int Ctp_it7250_resume(struct i2c_client *client)
{
	struct Ctp_it7250_data *Ctp_it7250 = (struct Ctp_it7250_data *)i2c_get_clientdata(client);

	u8 ucQuery;
	rk28printk("%s\n",__func__);

	if(gpio_direction_output(client->irq,GPIO_LOW))
		printk("%s:set pin output error\n",__func__);
	msleep(20);
	ReadQueryBuffer(client, &ucQuery);
	if (gpio_direction_input(client->irq)) 
		printk("%s:failed to set CTS_configure_pin gpio input\n",__func__);
	gpio_pull_updown(client->irq,GPIOPullUp);
	msleep(20);
	enable_irq(Ctp_it7250->irq);
	
	return 0;
}
#else
#define	Ctp_it7250_suspend		NULL
#define	Ctp_it7250_resume		NULL
#endif

static const struct i2c_device_id Ctp_it7250_id[] = {
		{"Ctp_it7250", 0},
		{ }
};

static struct i2c_driver Ctp_it7250_driver = {
	.driver = {
		.name 	= "Ctp_it7250",
	},
	.id_table 	= Ctp_it7250_id,
	.probe	= Ctp_it7250_probe,
	 .remove     =  Ctp_it7250_remove,
	 .suspend  = Ctp_it7250_suspend,
	 .resume = Ctp_it7250_resume,
};

static int __init Ctp_it7250_init(void)
{
	int ret;	
	rk28printk("+++++++     %s++\n", __FUNCTION__);
	ret=i2c_add_driver(&Ctp_it7250_driver);
	rk28printk("**Ctp_it7250_init_init return %d**\n",ret);
	return ret;
}

static void __exit Ctp_it7250_exit(void)
{
	/* We move these codes here because we want to detect the
	 * pen down event even when touch driver is not opened.
	 */
	i2c_del_driver(&Ctp_it7250_driver);
}

 late_initcall(Ctp_it7250_init);
module_exit(Ctp_it7250_exit);

MODULE_AUTHOR("Robert_mu<robert.mu@rahotech.com>");


