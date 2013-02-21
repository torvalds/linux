/*
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * author: hhb@rock-chips.com
 * create date: 2013-01-17
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
#include "mipi_dsi.h"

/* define spi gpio*/
#define TXD_PORT        ssd2828->spi.mosi
#define CLK_PORT        ssd2828->spi.sck
#define CS_PORT         ssd2828->spi.cs
#define RXD_PORT        ssd2828->spi.miso

#define CS_OUT()        gpio_direction_output(CS_PORT, 0)
#define CS_SET()        gpio_set_value(CS_PORT, GPIO_HIGH)
#define CS_CLR()        gpio_set_value(CS_PORT, GPIO_LOW)
#define CLK_OUT()       gpio_direction_output(CLK_PORT, 0)
#define CLK_SET()       gpio_set_value(CLK_PORT, GPIO_HIGH)
#define CLK_CLR()       gpio_set_value(CLK_PORT, GPIO_LOW)
#define TXD_OUT()       gpio_direction_output(TXD_PORT, 0)
#define TXD_SET()       gpio_set_value(TXD_PORT, GPIO_HIGH)
#define TXD_CLR()       gpio_set_value(TXD_PORT, GPIO_LOW)
#define RXD_INPUT()		gpio_direction_input(RXD_PORT)
#define RXD_GET()  		gpio_get_value(RXD_PORT)


struct ssd2828_t *ssd2828 = NULL;


int ssd2828_gpio_init(void *data) {
	int ret = 0;
	struct reset_t *reset = &ssd2828->reset;
	struct power_t *vdd = &ssd2828->vddio;
	struct spi_t *spi = &ssd2828->spi;
	
	if(reset->reset_pin > INVALID_GPIO) {
		ret = gpio_request(reset->reset_pin, "ssd2828_reset");
		if (ret != 0) {
			//gpio_free(reset->reset_pin);
			printk("%s: request ssd2828_RST_PIN error\n", __func__);
		} else {
			if(reset->mux_name)
				rk30_mux_api_set(reset->mux_name, 0);
			gpio_direction_output(reset->reset_pin, reset->effect_value);
		}
	}
	
	if(vdd->enable_pin > INVALID_GPIO) {
		ret = gpio_request(vdd->enable_pin, "ssd2828_vddio");
		if (ret != 0) {
			//gpio_free(vdd->enable_pin);
			printk("%s: request ssd2828_vddio_PIN error\n", __func__);
		} else {
			if(vdd->mux_name)
				rk30_mux_api_set(vdd->mux_name, 0);	
			gpio_direction_output(vdd->enable_pin, !vdd->effect_value);	
		}
	}
	
	vdd = &ssd2828->vdd_mipi;
	if(vdd->enable_pin > INVALID_GPIO) {
		ret = gpio_request(vdd->enable_pin, "ssd2828_vdd_mipi");
		if (ret != 0) {
			//gpio_free(vdd->enable_pin);
			printk("%s: request ssd2828_vdd_mipi_PIN error\n", __func__);
		} else {
			if(vdd->mux_name)
				rk30_mux_api_set(vdd->mux_name, 0);	
			gpio_direction_output(vdd->enable_pin, !vdd->effect_value);	
		}
	}
	
	if(spi->cs > INVALID_GPIO) {
		ret = gpio_request(spi->cs, "ssd2828_spi_cs");
		if (ret != 0) {
			//gpio_free(spi->cs);
			printk("%s: request ssd2828_spi->cs_PIN error\n", __func__);
		} else {
			if(spi->cs_mux_name)
				rk30_mux_api_set(spi->cs_mux_name, 0);	
			gpio_direction_output(spi->cs, GPIO_HIGH);	
		}
	}
	if(spi->sck > INVALID_GPIO) {
		ret = gpio_request(spi->sck, "ssd2828_spi_sck");
		if (ret != 0) {
			//gpio_free(spi->sck);
			printk("%s: request ssd2828_spi->sck_PIN error\n", __func__);
		} else {
			if(spi->sck_mux_name)
				rk30_mux_api_set(spi->sck_mux_name, 0);	
			gpio_direction_output(spi->sck, GPIO_HIGH);	
		}
	}	
	if(spi->mosi > INVALID_GPIO) {
		ret = gpio_request(spi->mosi, "ssd2828_spi_mosi");
		if (ret != 0) {
			//gpio_free(spi->mosi);
			printk("%s: request ssd2828_spi->mosi_PIN error\n", __func__);
		} else {
			if(spi->mosi_mux_name)
				rk30_mux_api_set(spi->mosi_mux_name, 0);	
			gpio_direction_output(spi->mosi, GPIO_HIGH);	
		}
	}	
	if(spi->miso > INVALID_GPIO) {
		ret = gpio_request(spi->miso, "ssd2828_spi_miso");
		if (ret != 0) {
			//gpio_free(spi->miso);
			printk("%s: request ssd2828_spi->miso_PIN error\n", __func__);
		} else {
			if(spi->miso_mux_name)
				rk30_mux_api_set(spi->miso_mux_name, 0);	
			gpio_direction_input(spi->miso);	
		}
	}	
	
	return 0;

}

int ssd2828_gpio_deinit(void *data) {
	struct reset_t *reset = &ssd2828->reset;
	struct power_t *vdd = &ssd2828->vddio;
	struct spi_t *spi = &ssd2828->spi;
	
	gpio_direction_input(reset->reset_pin);
	gpio_free(reset->reset_pin);
	
	gpio_direction_input(vdd->enable_pin);
	gpio_free(vdd->enable_pin);
	
	vdd = &ssd2828->vdd_mipi;
	gpio_direction_input(vdd->enable_pin);
	gpio_free(vdd->enable_pin);
	
	gpio_direction_input(spi->cs);
	gpio_free(spi->cs);
	gpio_direction_input(spi->sck);
	gpio_free(spi->sck);
	gpio_direction_input(spi->mosi);
	gpio_free(spi->mosi);
	gpio_free(spi->miso);
	
	return 0;
}

int ssd2828_reset(void *data) {
	int ret = 0;
	struct reset_t *reset = &ssd2828->reset;
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

int ssd2828_vdd_enable(void *data) {
	int ret = 0;
	struct power_t *vdd = (struct power_t *)data;
	if(vdd->enable_pin > INVALID_GPIO) {
		gpio_set_value(vdd->enable_pin, vdd->effect_value);
	} else {
		//for other control
	}
	return ret;
}

int ssd2828_vdd_disable(void *data) {
	int ret = 0;
	struct power_t *vdd = (struct power_t *)data;
	
	if(vdd->enable_pin > INVALID_GPIO) {
		gpio_set_value(vdd->enable_pin, !vdd->effect_value);
	} else {
		//for other control
	}
	return ret;
}


int ssd2828_power_up(void) {

	int ret = 0;
	struct ssd2828_t *ssd = (struct ssd2828_t *)ssd2828;
	
	ssd2828_gpio_init(NULL);
	ssd->vdd_mipi.enable(&ssd->vdd_mipi);
	ssd->vddio.enable(&ssd->vddio);
	ssd->reset.do_reset(&ssd->reset);
	
	return ret;
}

int ssd2828_power_down(void) {

	int ret = 0;
	struct ssd2828_t *ssd = (struct ssd2828_t *)ssd2828;
	
	ssd->vddio.disable(&ssd->vddio);
	ssd->vdd_mipi.disable(&ssd->vdd_mipi);
	ssd2828_gpio_deinit(NULL);
	
	return ret;
}



/* spi write a data frame,type mean command or data 
	3 wire 24 bit SPI interface
*/

static void spi_send_data(unsigned int data)
{
    unsigned int i;

    CS_SET();
    udelay(1);
    CLK_SET();
    TXD_SET();

    CS_CLR();
    udelay(1);

    for (i = 0; i < 24; i++)
    {
        //udelay(1); 
        CLK_CLR();
        udelay(1);
        if (data & 0x00800000) {
            TXD_SET();
        } else {
            TXD_CLR();
        }
        udelay(1);
        CLK_SET();
        udelay(1);
        data <<= 1;
    }

    TXD_SET();
    CS_SET();
}

static void spi_recv_data(unsigned int* data)
{
    unsigned int i = 0, temp = 0x73;   //read data

    CS_SET();
    udelay(1);
    CLK_SET();
    TXD_SET();

    CS_CLR();
    udelay(1);

	for(i = 0; i < 8; i++) // 8 bits Data
    {
		udelay(1); 
		CLK_CLR();
		if (temp & 0x80)
		   TXD_SET();
		else
		   TXD_CLR();
		temp <<= 1;
		udelay(1); 
		CLK_SET();
		udelay(1); 
	}
	udelay(1);
	temp = 0;
	for(i = 0; i < 16; i++) // 16 bits Data
	{
		udelay(1); 
		CLK_CLR();
		udelay(1); 
		CLK_SET();
		udelay(1); 
		temp <<= 1;
		if(RXD_GET() == GPIO_HIGH)
		   temp |= 0x01;
		
	}

    TXD_SET();
    CS_SET();
    *data = temp;
}

#define DEVIE_ID (0x70 << 16)
void send_ctrl_cmd(unsigned int cmd)
{
    unsigned int out = (DEVIE_ID | cmd );
    spi_send_data(out);
}

static void send_data_cmd(unsigned int data)
{
    unsigned int out = (DEVIE_ID | (0x2 << 16) | data );
    spi_send_data(out);
}

unsigned int ssd_read_register(unsigned int reg) {
	unsigned int data = 0;
	send_ctrl_cmd(reg);
	spi_recv_data(&data);
	return data;
}

void ssd_set_register(unsigned int reg_and_value)
{
    send_ctrl_cmd(reg_and_value >> 16);
    send_data_cmd(reg_and_value & 0x0000ffff);
}

int ssd_set_registers(unsigned int reg_array[], int n) {

	int i = 0;
	for(i = 0; i < n; i++) {
		if(reg_array[i] < 0x00b00000) {      //the lowest address is 0xb0 of ssd2828
		    if(reg_array[i] < 20000)
		    	udelay(reg_array[i]);
		    else {
		    	mdelay(reg_array[i]/1000);
		    }
		} else {
			ssd_set_register(reg_array[i]);
		}
	}
	return 0;
}

int ssd_mipi_dsi_send_dcs_packet(unsigned char regs[], int n) {
	//unsigned int data = 0, i = 0;
	ssd_set_register(0x00B70363);
	ssd_set_register(0x00B80000);
	ssd_set_register(0x00Bc0001);
	
	ssd_set_register(0x00Bf0000 | regs[0]);
	msleep(1);

	return 0;
}


int _ssd2828_send_packet(unsigned char type, unsigned char regs[], int n) {

	
	return 0;
}

int ssd2828_send_packet(unsigned char type, unsigned char regs[], int n) {
	return _ssd2828_send_packet(type, regs, n);
}

int ssd_mipi_dsi_read_dcs_packet(unsigned char *data, int n) {
	//DCS READ 
	unsigned int i = 0;
	
	i = ssd_read_register(0xc6);
	printk("read mipi slave error:%04x\n", i);
	ssd_set_register(0x00B70382);
	ssd_set_register(0x00BB0008);
	ssd_set_register(0x00C1000A);
	ssd_set_register(0x00C00001);
	ssd_set_register(0x00Bc0001);
	ssd_set_register(0x00Bf0000 | *data);
	msleep(10);
	i = ssd_read_register(0xc6);
	printk("read mipi slave error:%04x\n", i);
	
	if(i & 1) {
		i = ssd_read_register(0xff);
		printk("read %02x:%04x\n", *data, i);
		i = ssd_read_register(0xff);
		printk("read %02x:%04x\n", *data, i);
		i = ssd_read_register(0xff);
		printk("read %02x:%04x\n", *data, i);
	
	} 
		
	return 0;
}


int ssd2828_get_id(void) {
	
	int id = -1;
	ssd2828_power_up();
	id = ssd_read_register(0xb0);
	
	return id;
}

static struct mipi_dsi_ops ssd2828_ops = {
	.id = 0x2828,
	.name = "ssd2828",
	.get_id = ssd2828_get_id,
	.dsi_set_regs = ssd_set_registers,
	.dsi_send_dcs_packet = ssd_mipi_dsi_send_dcs_packet,
	.dsi_read_dcs_packet = ssd_mipi_dsi_read_dcs_packet,
	.power_up = ssd2828_power_up,
	.power_down = ssd2828_power_down,
	
};

static int ssd2828_probe(struct platform_device *pdev) {

	if(pdev->dev.platform_data)
		ssd2828 = pdev->dev.platform_data;
		
    if(!ssd2828->gpio_init)
    	ssd2828->gpio_init = ssd2828_gpio_init;
    	
    if(!ssd2828->gpio_deinit)
    	ssd2828->gpio_deinit = ssd2828_gpio_deinit;    
    
    if(!ssd2828->power_up)
    	ssd2828->power_up = ssd2828_power_up;  
    if(!ssd2828->power_down)
    	ssd2828->power_down = ssd2828_power_down;  	
    
    if(!ssd2828->reset.do_reset)
    	ssd2828->reset.do_reset = ssd2828_reset;
    
    if(!ssd2828->vddio.enable)
    	ssd2828->vddio.enable = ssd2828_vdd_enable;    
    if(!ssd2828->vddio.disable)
    	ssd2828->vddio.disable = ssd2828_vdd_disable;
    
    if(!ssd2828->vdd_mipi.enable)
    	ssd2828->vdd_mipi.enable = ssd2828_vdd_enable;    
    if(!ssd2828->vdd_mipi.disable)
    	ssd2828->vdd_mipi.disable = ssd2828_vdd_disable;	
	
	return 0;
}


static int ssd2828_remove(struct platform_device *pdev) {

	if(!ssd2828)
		ssd2828 = NULL;

	return 0;
}


static struct platform_driver ssd2828_driver = {
	.probe		= ssd2828_probe,
	.remove		= ssd2828_remove,
	//.suspend		= mipi_dsi_suspend,
	//.resume		= mipi_dsi_resume,
	.driver = {
		.name = "ssd2828",
		.owner	= THIS_MODULE,
	}
};

static int __init ssd2828_init(void)
{
	platform_driver_register(&ssd2828_driver);
	if(!ssd2828)
		return -1;
	register_dsi_ops(&ssd2828_ops);
	if(ssd2828->id > 0)
		ssd2828_ops.id = ssd2828->id;
	return 0;
}

static void __exit ssd2828_exit(void)
{
	platform_driver_unregister(&ssd2828_driver);
	del_dsi_ops(&ssd2828_ops);
}

subsys_initcall_sync(ssd2828_init);
module_exit(ssd2828_exit);
