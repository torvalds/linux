/*
 * Copyright (C) 2012 ROCKCHIP, Inc.
 * drivers/video/display/transmitter/tc358768.c
 * author: hhb@rock-chips.com
 * create date: 2012-10-26
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/rk_fb.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include <linux/rk_screen.h>
#include <linux/ktime.h>
#include "mipi_dsi.h"

#define CONFIG_TC358768_I2C     1
#define CONFIG_TC358768_I2C_CLK     400*1000


#if 0
#define dsi_debug   printk
#else
#define dsi_debug(fmt...)   do { } while (0)
#endif

#ifdef CONFIG_TC358768_I2C
static struct tc358768_t *tc358768 = NULL;
static struct i2c_client *tc358768_client = NULL;
static struct mipi_dsi_ops tc358768_ops;


u32 i2c_write_32bits(u32 value) 
{
	struct i2c_msg msgs;
	int ret = -1;
	char buf[4];
	buf[0] = value>>24;
	buf[1] = value>>16;
	buf[2] = value>>8;
	buf[3] = value;
	
	msgs.addr = tc358768_client->addr;
	msgs.flags = tc358768_client->flags;
	msgs.len = 4;
	msgs.buf = buf;
	msgs.scl_rate = CONFIG_TC358768_I2C_CLK;
	msgs.udelay = tc358768_client->udelay;

	ret = i2c_transfer(tc358768_client->adapter, &msgs, 1);
	if(ret < 0)
		printk("%s:i2c_transfer fail =%d\n",__func__, ret);
	return ret;
}

u32 i2c_read_32bits(u32 value) 
{
	struct i2c_msg msgs[2];
	int ret = -1;
	char buf[4];
	buf[0] = value>>8;
	buf[1] = value;
	
	msgs[0].addr = tc358768_client->addr;
	msgs[0].flags = tc358768_client->flags;
	msgs[0].len = 2;
	msgs[0].buf = buf;
	msgs[0].scl_rate = CONFIG_TC358768_I2C_CLK;
	msgs[0].udelay = tc358768_client->udelay;

	msgs[1].addr = tc358768_client->addr;
	msgs[1].flags = tc358768_client->flags | I2C_M_RD;
	msgs[1].len = 2;
	msgs[1].buf = buf;
	msgs[1].scl_rate = CONFIG_TC358768_I2C_CLK;
	msgs[1].udelay = tc358768_client->udelay;

	ret = i2c_transfer(tc358768_client->adapter, msgs, 2);
	if(ret < 0)
		printk("%s:i2c_transfer fail =%d\n",__func__, ret);
	else
		ret = (buf[0]<<8) | buf[1];	
	
	return ret;
}


int tc358768_gpio_init(void *data) {
	int ret = 0;
	struct reset_t *reset = &tc358768->reset;
	struct power_t *vdd = &tc358768->vddc;
	if(reset->reset_pin > INVALID_GPIO) {
		ret = gpio_request(reset->reset_pin, "tc358768_reset");
		if (ret != 0) {
			//gpio_free(reset->reset_pin);
			printk("%s: request TC358768_RST_PIN error\n", __func__);
		} else {
#if OLD_RK_IOMUX		
			if(reset->mux_name)
				rk30_mux_api_set(reset->mux_name, reset->mux_mode);
#endif
			gpio_direction_output(reset->reset_pin, !reset->effect_value);
		}
	}
	
	if(vdd->enable_pin > INVALID_GPIO) {
		ret = gpio_request(vdd->enable_pin, "tc358768_vddc");
		if (ret != 0) {
			//gpio_free(vdd->enable_pin);
			printk("%s: request TC358768_vddc_PIN error\n", __func__);
		} else {
#if OLD_RK_IOMUX		
			if(vdd->mux_name)
				rk30_mux_api_set(vdd->mux_name, vdd->mux_mode);	
#endif
			gpio_direction_output(vdd->enable_pin, !vdd->effect_value);
		}
	}
	
	vdd = &tc358768->vddio;
	if(vdd->enable_pin > INVALID_GPIO) {
		ret = gpio_request(vdd->enable_pin, "tc358768_vddio");
		if (ret != 0) {
			//gpio_free(vdd->enable_pin);
			printk("%s: request TC358768_vddio_PIN error\n", __func__);
		} else {
#if OLD_RK_IOMUX		
			if(vdd->mux_name)
				rk30_mux_api_set(vdd->mux_name, vdd->mux_mode);	
#endif
			gpio_direction_output(vdd->enable_pin, !vdd->effect_value);	
		}
	}
	
	vdd = &tc358768->vdd_mipi;
	if(vdd->enable_pin > INVALID_GPIO) {
		ret = gpio_request(vdd->enable_pin, "tc358768_vdd_mipi");
		if (ret != 0) {
			//gpio_free(vdd->enable_pin);
			printk("%s: request TC358768_vdd_mipi_PIN error\n", __func__);
		} else {
#if OLD_RK_IOMUX		
			if(vdd->mux_name)
				rk30_mux_api_set(vdd->mux_name, vdd->mux_mode);	
#endif
			gpio_direction_output(vdd->enable_pin, !vdd->effect_value);	
		}
	}
	return 0;

}

int tc358768_gpio_deinit(void *data) {
	struct reset_t *reset = &tc358768->reset;
	struct power_t *vdd = &tc358768->vddc;
	gpio_direction_input(reset->reset_pin);
	gpio_free(reset->reset_pin);
	
	gpio_direction_input(vdd->enable_pin);
	gpio_free(vdd->enable_pin);
	
	vdd = &tc358768->vddio;
	gpio_direction_input(vdd->enable_pin);
	gpio_free(vdd->enable_pin);
	
	vdd = &tc358768->vdd_mipi;
	gpio_direction_input(vdd->enable_pin);
	gpio_free(vdd->enable_pin);
	return 0;
}

int tc358768_reset(void *data) {
	int ret = 0;
	struct reset_t *reset = &tc358768->reset;
	if(reset->reset_pin <= INVALID_GPIO)
		return -1;
	gpio_set_value(reset->reset_pin, reset->effect_value);
	if(reset->time_before_reset <= 0)
		msleep(1);
	else
		msleep(reset->time_before_reset);
	
	gpio_set_value(reset->reset_pin, !reset->effect_value);
	if(reset->time_after_reset <= 0)
		msleep(5);
	else
		msleep(reset->time_after_reset);
	return ret;	
}

int tc358768_vdd_enable(void *data) {
	int ret = 0;
	struct power_t *vdd = (struct power_t *)data;
	if(vdd->enable_pin > INVALID_GPIO) {
		gpio_set_value(vdd->enable_pin, vdd->effect_value);
	} else {
		//for other control
	}
	return ret;
}

int tc358768_vdd_disable(void *data) {
	int ret = 0;
	struct power_t *vdd = (struct power_t *)data;
	
	if(vdd->enable_pin > INVALID_GPIO) {
		gpio_set_value(vdd->enable_pin, !vdd->effect_value);
	} else {
		//for other control
	}
	return ret;
}


int tc358768_power_up(void) {

	int ret = 0;
	struct tc358768_t *tc = (struct tc358768_t *)tc358768;
	
	tc->vddc.enable(&tc->vddc);
	tc->vdd_mipi.enable(&tc->vdd_mipi);
	tc->vddio.enable(&tc->vddio);
	tc->reset.do_reset(&tc->reset);
	
	return ret;
}

int tc358768_power_down(void) {

	int ret = 0;
	struct tc358768_t *tc = (struct tc358768_t *)tc358768;
	
	tc->vddio.disable(&tc->vddio);
	tc->vdd_mipi.disable(&tc->vdd_mipi);
	tc->vddc.disable(&tc->vddc);
	gpio_set_value(tc358768->reset.reset_pin, 0);
	
	return ret;
}

static int tc358768_probe(struct i2c_client *client,
			 const struct i2c_device_id *did) 
{
    struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
    int ret = 0;

    if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
        dev_warn(&adapter->dev,
        	 "I2C-Adapter doesn't support I2C_FUNC_I2C\n");
        return -EIO;
    }
    
    tc358768 = (struct tc358768_t *)client->dev.platform_data;
    if(!tc358768) {
    	ret = -1;
    	printk("%s:%d tc358768 is null\n", __func__, __LINE__);
    	return ret;
    }	

    tc358768_client = client;
    if(!tc358768_client) {
    	ret = -1;
    	printk("%s:%d tc358768_client is null\n", __func__, __LINE__);
    	return ret;
    }
    
    if(!tc358768->gpio_init)
    	tc358768->gpio_init = tc358768_gpio_init;
    	
    if(!tc358768->gpio_deinit)
    	tc358768->gpio_deinit = tc358768_gpio_deinit;    
    
    if(!tc358768->power_up)
    	tc358768->power_up = tc358768_power_up;  
    if(!tc358768->power_down)
    	tc358768->power_down = tc358768_power_down;  	
    
    if(!tc358768->reset.do_reset)
    	tc358768->reset.do_reset = tc358768_reset;
    
    if(!tc358768->vddc.enable)
    	tc358768->vddc.enable = tc358768_vdd_enable;    
    if(!tc358768->vddc.disable)
    	tc358768->vddc.disable = tc358768_vdd_disable;
    
    if(!tc358768->vddio.enable)
    	tc358768->vddio.enable = tc358768_vdd_enable;    
    if(!tc358768->vddio.disable)
    	tc358768->vddio.disable = tc358768_vdd_disable;
    
    if(!tc358768->vdd_mipi.enable)
    	tc358768->vdd_mipi.enable = tc358768_vdd_enable;    
    if(!tc358768->vdd_mipi.disable)
    	tc358768->vdd_mipi.disable = tc358768_vdd_disable;
    	
    tc358768_gpio_init(NULL);
    
    return ret;
}
static int tc358768_remove(struct i2c_client *client)
{
    tc358768_gpio_deinit(NULL);
    tc358768_client = NULL;
    tc358768 = NULL;
    return 0;
}

static const struct i2c_device_id tc358768_id[] = {
	{"tc358768", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tc358768_id);

static struct i2c_driver tc358768_driver = {
	.probe		= tc358768_probe,
	.remove		= tc358768_remove,
	.id_table	= tc358768_id,
	.driver = {
		.name	= "tc358768",
	},
};
#else

u32 spi_read_32bits(u32 addr)
{
	unsigned int i = 32;
	//a frame starts
	CS_CLR();
	CLK_SET();

	addr <<= 16;
	addr &= 0xfffe0000;
	addr |= 0x00010000;

	udelay(2);
	while(i--) {
		CLK_CLR();
		if(addr & 0x80000000)
			TXD_SET();
        else
			TXD_CLR();
		addr <<= 1;
		udelay(2);
		CLK_SET();
		udelay(2);
	}
	//a frame ends
    CS_SET();


    udelay(2);
    CS_CLR();
    addr = 0xfffe0000;
    i = 16;
	while(i--) {
		CLK_CLR();
		if(addr & 0x80000000)
			TXD_SET();
        else
			TXD_CLR();
		addr <<= 1;
		udelay(2);
		CLK_SET();
		udelay(2);
	}

	TXD_SET();

	addr = 0;
    i = 16;
	while(i--) {
		CLK_CLR();
		udelay(1);
		CLK_SET();
		udelay(1);
		if (gpio_get_value(gLcd_info->rxd_pin) == 1)
			addr |= 1 << i;
		udelay(1);
	}
    CS_SET();

    return addr;
}


//32 bits per frame
u32 spi_write_32bits(u32 value)
{
	int i = 32;

    //a frame starts
	CS_CLR();
	CLK_SET();

	while(i--) {
		CLK_CLR();
		if(value & 0x80000000)
			TXD_SET();
        else
			TXD_CLR();
		value <<= 1;
		CLK_SET();
	}
	//a frame ends
    CS_SET();

    return 0;
}

#endif

u32 tc358768_wr_reg_32bits(u32 data) {
#ifdef CONFIG_TC358768_I2C
	i2c_write_32bits(data);
#else
	spi_write_32bits(data);
#endif
	return 0;
}


u32 tc358768_wr_reg_32bits_delay(u32 delay, u32 data) {
	//wait a minute  according to the source format
    if(delay < 20000)
    	udelay(delay);
    else {
    	mdelay(delay/1000);
    }

#ifdef CONFIG_TC358768_I2C
	i2c_write_32bits(data);
#else
	spi_write_32bits(data);
#endif
	return 0;
}



u32 tc358768_rd_reg_32bits(u32 addr) {
#ifdef CONFIG_TC358768_I2C
	return i2c_read_32bits(addr);
#else
	return spi_read_32bits(addr);
#endif
}



void tc_print(u32 addr) {
	dsi_debug("+++++++++++addr->%04x: %04x\n", addr, tc358768_rd_reg_32bits(addr));
}

#define tc358768_wr_regs_32bits(reg_array)  _tc358768_wr_regs_32bits(reg_array, ARRAY_SIZE(reg_array))
int _tc358768_wr_regs_32bits(unsigned int reg_array[], u32 n) {

	int i = 0;
	dsi_debug("%s:%d\n", __func__, n);
	for(i = 0; i < n; i++) {
		if(reg_array[i] < 0x00020000) {
		    if(reg_array[i] < 20000)
		    	udelay(reg_array[i]);
		    else {
		    	mdelay(reg_array[i]/1000);
		    }
		} else {
			tc358768_wr_reg_32bits(reg_array[i]);
		}
	}
	return 0;
}

int tc358768_command_tx_less8bytes(unsigned char type, unsigned char *regs, u32 n) {
	int i = 0;
	unsigned int command[] = {
			0x06020000,
			0x06040000,
			0x06100000,
			0x06120000,
			0x06140000,
			0x06160000,
	};

	if(n <= 2)
		command[0] |= 0x1000;   //short packet
	else {
		command[0] |= 0x4000;   //long packet
		command[1] |= n;		//word count byte
	}
	command[0] |= type;         //data type

	//dsi_debug("*cmd:\n");
	//dsi_debug("0x%08x\n", command[0]);
	//dsi_debug("0x%08x\n", command[1]);

	for(i = 0; i < (n + 1)/2; i++) {
		command[i+2] |= regs[i*2];
		if((i*2 + 1) < n)
			command[i+2] |= regs[i*2 + 1] << 8;
		dsi_debug("0x%08x\n", command[i+2]);
	}

	_tc358768_wr_regs_32bits(command, (n + 1)/2 + 2);
	tc358768_wr_reg_32bits(0x06000001);   //Packet Transfer
	//wait until packet is out
	i = 100;
	while(tc358768_rd_reg_32bits(0x0600) & 0x01) {
		if(i-- == 0)
			break;
		tc_print(0x0600);
	}
	//udelay(50);
	return 0;
}

int tc358768_command_tx_more8bytes_hs(unsigned char type, unsigned char regs[], u32 n) {

	int i = 0;
	unsigned int dbg_data = 0x00E80000, temp = 0;
	unsigned int command[] = {
			0x05000080,    //HS data 4 lane, EOT is added
			0x0502A300,
			0x00080001,
			0x00500000,    //Data ID setting
			0x00220000,    //Transmission byte count= byte
			0x00E08000,	   //Enable I2C/SPI write to VB
			0x00E20048,    //Total word count = 0x48 (max 0xFFF). This value should be adjusted considering trade off between transmission time and transmission start/stop time delay
			0x00E4007F,    //Vertical blank line = 0x7F
	};


	command[3] |= type;        //data type
	command[4] |= n & 0xffff;           //Transmission byte count

	tc358768_wr_regs_32bits(command);

	for(i = 0; i < (n + 1)/2; i++) {
		temp = dbg_data | regs[i*2];
		if((i*2 + 1) < n)
			temp |= (regs[i*2 + 1] << 8);
		//dsi_debug("0x%08x\n", temp);
		tc358768_wr_reg_32bits(temp);
	}
	if((n % 4 == 1) ||  (n % 4 == 2))     //4 bytes align
		tc358768_wr_reg_32bits(dbg_data);

	tc358768_wr_reg_32bits(0x00E0C000);     //Start command transmisison
	tc358768_wr_reg_32bits(0x00E00000);	 //Stop command transmission. This setting should be done just after above setting to prevent multiple output
	udelay(200);
	//Re-Initialize
	//tc358768_wr_regs_32bits(re_initialize);
	return 0;
}

//low power mode only for tc358768a
int tc358768_command_tx_more8bytes_lp(unsigned char type, unsigned char regs[], u32 n) {

	int i = 0;
	unsigned int dbg_data = 0x00E80000, temp = 0;
	unsigned int command[] = {
			0x00080001,
			0x00500000,    //Data ID setting
			0x00220000,    //Transmission byte count= byte
			0x00E08000,	   //Enable I2C/SPI write to VB
	};

	command[1] |= type;        //data type
	command[2] |= n & 0xffff;           //Transmission byte count

	tc358768_wr_regs_32bits(command);

	for(i = 0; i < (n + 1)/2; i++) {
		temp = dbg_data | regs[i*2];
		if((i*2 + 1) < n)
			temp |= (regs[i*2 + 1] << 8);
		//dsi_debug("0x%08x\n", temp);
		tc358768_wr_reg_32bits(temp);

	}
	if((n % 4 == 1) ||  (n % 4 == 2))     //4 bytes align
		tc358768_wr_reg_32bits(dbg_data);

	tc358768_wr_reg_32bits(0x00E0E000);     //Start command transmisison
	udelay(1000);
	tc358768_wr_reg_32bits(0x00E02000);	 //Keep Mask High to prevent short packets send out
	tc358768_wr_reg_32bits(0x00E00000);	 //Stop command transmission. This setting should be done just after above setting to prevent multiple output
	udelay(10);
	return 0;
}

int _tc358768_send_packet(unsigned char type, unsigned char regs[], u32 n) {

	if(n <= 8) {
		tc358768_command_tx_less8bytes(type, regs, n);
	} else {
		//tc358768_command_tx_more8bytes_hs(type, regs, n);
		tc358768_command_tx_more8bytes_lp(type, regs, n);
	}
	return 0;
}

int tc358768_send_packet(unsigned char type, unsigned char regs[], u32 n) {
	return _tc358768_send_packet(type, regs, n);
}


/*
The DCS is separated into two functional areas: the User Command Set and the Manufacturer Command
Set. Each command is an eight-bit code with 00h to AFh assigned to the User Command Set and all other
codes assigned to the Manufacturer Command Set.
*/
int _mipi_dsi_send_dcs_packet(unsigned char regs[], u32 n) {

	unsigned char type = 0;
	if(n == 1) {
		type = DTYPE_DCS_SWRITE_0P;
	} else if (n == 2) {
		type = DTYPE_DCS_SWRITE_1P;
	} else if (n > 2) {
		type = DTYPE_DCS_LWRITE;
	} 
	_tc358768_send_packet(type, regs, n);
	return 0;
}

int mipi_dsi_send_dcs_packet(unsigned char regs[], u32 n) {
	return _mipi_dsi_send_dcs_packet(regs, n);
}


int _tc358768_rd_lcd_regs(unsigned char type, char comd, int size, unsigned char* buf) {

	unsigned char regs[8];
	u32 count = 0, data30, data32;
	regs[0] = size;
	regs[1] = 0;
	tc358768_command_tx_less8bytes(0x37, regs, 2);
	tc358768_wr_reg_32bits(0x05040010);
	tc358768_wr_reg_32bits(0x05060000);
	regs[0] = comd;
	tc358768_command_tx_less8bytes(type, regs, 1);

	while (!(tc358768_rd_reg_32bits(0x0410) & 0x20)){
		printk("error 0x0410:%04x\n", tc358768_rd_reg_32bits(0x0410));
		msleep(1);
		if(count++ > 10) {
			break;
		}
	}
	
	data30 = tc358768_rd_reg_32bits(0x0430);	  //data id , word count[0:7]
	//printk("0x0430:%04x\n", data30);
	data32 = tc358768_rd_reg_32bits(0x0432);	  //word count[8:15]  ECC
	//printk("0x0432:%04x\n", data32);
	
	while(size > 0) {
		data30 = tc358768_rd_reg_32bits(0x0430);	  
		//printk("0x0430:%04x\n", data30);
		data32 = tc358768_rd_reg_32bits(0x0432);	  
		//printk("0x0432:%04x\n", data32);
	
		if(size-- > 0)
			*buf++ = (u8)data30;
		else
			break;
		if(size-- > 0)
			*buf++ = (u8)(data30 >> 8);
		else
			break;
		if(size-- > 0) {
			*buf++ = (u8)data32;
			if(size-- > 0)
				*buf++ = (u8)(data32 >> 8);
		}
	}	
	
	data30 = tc358768_rd_reg_32bits(0x0430);	  
	//printk("0x0430:%04x\n", data30);
	data32 = tc358768_rd_reg_32bits(0x0432);	  
	//printk("0x0432:%04x\n", data32);
	return 0;
}

int mipi_dsi_read_dcs_packet(unsigned char *data, u32 n) {
	//DCS READ 
	_tc358768_rd_lcd_regs(0x06, *data, n, data);
	return 0;
}

int tc358768_get_id(void) {
	
	int id = -1;
	
	tc358768_power_up();
	id = tc358768_rd_reg_32bits(0);
	return id;
}

static struct mipi_dsi_ops tc358768_ops = {
	.id = 0x4401,
	.name = "tc358768a",
	.get_id = tc358768_get_id,
	.dsi_set_regs = _tc358768_wr_regs_32bits,
	.dsi_send_dcs_packet = mipi_dsi_send_dcs_packet,
	.dsi_read_dcs_packet = mipi_dsi_read_dcs_packet,
	.power_up = tc358768_power_up,
	.power_down = tc358768_power_down,
	
};

static int __init tc358768_module_init(void)
{
#ifdef CONFIG_TC358768_I2C    
    i2c_add_driver(&tc358768_driver);
    
	if(!tc358768 || !tc358768_client)
		return -1;
#endif		
		
	register_dsi_ops(&tc358768_ops);
	if(tc358768->id > 0)
		tc358768_ops.id = tc358768->id;
    return 0;
}

static void __exit tc358768_module_exit(void)
{
	del_dsi_ops(&tc358768_ops);
#ifdef CONFIG_TC358768_I2C
	i2c_del_driver(&tc358768_driver);
#endif
}

subsys_initcall_sync(tc358768_module_init);
//module_exit(tc358768_module_init);
module_exit(tc358768_module_exit);
