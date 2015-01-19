
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include "sii9233_drv.h"
#include <config.h>
#include <hal_cp9223.h>


/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// for timer
#define SII9233A_TIMER_INTERVAL	1 // 1 jiffies for 10ms
static struct timer_list sii9233a_timer;
static uint16_t TimerArray[CONF__TIMER_COUNT];
static uint16_t wTickCnt = 0;

void TIMER_Set(uint8_t index, uint16_t value)
{
	if (index < CONF__TIMER_COUNT)
    {
        TimerArray[index] = value/10;
    }

	return ;
}

uint8_t TIMER_Expired(uint8_t index)
{
	if (index < CONF__TIMER_COUNT)
    {
        return (TimerArray[index] == 0);
    }

	return 0;
}

uint16_t TIMER_GetTickCounter( void )
{
	return wTickCnt;
}

static void sii9233a_timer_handler(unsigned long arg)
{
	int i = 0;

	wTickCnt++;

	for( i = 0; i < CONF__TIMER_COUNT; i++ )
	{
		if (TimerArray[ i ] > 0)
        {
            TimerArray[ i ]--;
        }
    }

    mod_timer(&sii9233a_timer, jiffies + SII9233A_TIMER_INTERVAL);
}

void TIMER_Init(void)
{
	int i = 0;

	for (i = 0; i < CONF__TIMER_COUNT; i++)
    {
        TimerArray[ i ] = 0;
    }

	init_timer(&sii9233a_timer);
	sii9233a_timer.data = (ulong)0;
	sii9233a_timer.function = sii9233a_timer_handler;
	sii9233a_timer.expires = jiffies + SII9233A_TIMER_INTERVAL;
	add_timer(&sii9233a_timer);

	return ;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// for i2c

uint8_t I2C_ReadByte(uint8_t deviceID, uint8_t offset)
{
	int ret = 0;
	char buf[2] = {0,0};

	buf[0] = offset;

	ret = aml_sii9233a_i2c_read(deviceID, buf, 1, 1);
	
	return (uint8_t)buf[0];
}

void I2C_WriteByte(uint8_t deviceID, uint8_t offset, uint8_t value)
{
	int ret = 0;
	char buf[2] = {0,0};

	buf[0] = offset;
	buf[1] = value;

	ret = aml_sii9233a_i2c_write(deviceID, buf, 2);

	return ;
}

uint8_t I2C_ReadBlock(uint8_t deviceID, uint8_t offset, uint8_t *buffer, uint16_t length)
{
	int ret = 0;

	buffer[0] = offset;
	ret = aml_sii9233a_i2c_read(deviceID, buffer, 1, length);
	
	return (uint8_t)ret;
}

uint8_t I2C_WriteBlock(uint8_t deviceID, uint8_t offset, uint8_t *buffer, uint16_t length)
{
	int ret = 0;
	char *tmp = NULL;

	tmp = (char*)kmalloc(length+1, GFP_KERNEL);

	if( tmp == NULL )
		return 1;

	tmp[0] = offset;
	memcpy(&tmp[1], buffer, length);

	ret = aml_sii9233a_i2c_write(deviceID, tmp, length+1);

	return (uint8_t)ret;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// fake functions for 9233a private driver
uint8_t GPIO_GetPins(uint8_t pinMask)
{
	return 0;
}

void GPIO_SetPins(uint8_t pinMask)
{
	return ;
}

void GPIO_ClearPins(uint8_t pinMask)
{
	return ;
}
