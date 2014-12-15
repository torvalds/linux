/*******************************************************************
 *
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2013/1/31   18:20
 *
 *******************************************************************/
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/pinctrl/consumer.h>
#include <mach/am_regs.h>
#include <linux/delay.h>
#include <mach/gpio.h>
//#include <mach/gpio_data.h>
#include <linux/amlogic/tvin/tvin.h>

#include <linux/amlogic/camera/aml_cam_info.h>
#include <linux/amlogic/aml_gpio_consumer.h>

//extern int amlogic_gpio_name_map_num(const char *name);
//extern int32_t gpio_out(uint32_t pin,bool high);

static int aml_camera_read_buff(struct i2c_adapter *adapter, 
		unsigned short dev_addr, char *buf, int addr_len, int data_len)
{
	int  i2c_flag = -1;
	struct i2c_msg msgs[] = {
		{
			.addr	= dev_addr,
			.flags	= 0,
			.len	= addr_len,
			.buf	= buf,
		},{
			.addr	= dev_addr,
			.flags	= I2C_M_RD,
			.len	= data_len,
			.buf	= buf,
		}
	};

	i2c_flag = i2c_transfer(adapter, msgs, 2);

	return i2c_flag;
}

static int aml_camera_write_buff(struct i2c_adapter *adapter, 
				unsigned short dev_addr, char *buf, int len)
{
	struct i2c_msg msg[] = {
		{
			.addr	= dev_addr,
			.flags	= 0,    //|I2C_M_TEN,
			.len	= len,
			.buf	= buf,
		}
	};

	if (i2c_transfer(adapter, msg, 1) < 0) {
		return -1;
	} else
		return 0;
}

static int aml_i2c_get_byte(struct i2c_adapter *adapter, 
		unsigned short dev_addr, unsigned short addr)
{
	unsigned char buff[4];
	buff[0] = (unsigned char)((addr >> 8) & 0xff);
	buff[1] = (unsigned char)(addr & 0xff);
       
	if (aml_camera_read_buff(adapter, dev_addr, buff, 2, 1) <0)
		return -1;
	return buff[0];
}

static int aml_i2c_put_byte(struct i2c_adapter *adapter, 
		unsigned short dev_addr, unsigned short addr, unsigned char data)
{
	unsigned char buff[4];
	buff[0] = (unsigned char)((addr >> 8) & 0xff);
	buff[1] = (unsigned char)(addr & 0xff);
	buff[2] = data;
	if (aml_camera_write_buff(adapter, dev_addr, buff, 3) <0)
		return -1;
	return  0;
}


static int aml_i2c_get_byte_add8(struct i2c_adapter *adapter, 
		unsigned short dev_addr, unsigned short addr)
{
	unsigned char buff[4];
	buff[0] = (unsigned char)(addr & 0xff);
       
	if (aml_camera_read_buff(adapter, dev_addr, buff, 1, 1) <0)
		return -1;
	return buff[0];
}

static int aml_i2c_put_byte_add8(struct i2c_adapter *adapter, 
		unsigned short dev_addr, unsigned short addr, unsigned char data)
{
	unsigned char buff[4];
	buff[0] = (unsigned char)(addr & 0xff);
	buff[1] = data;
	if (aml_camera_write_buff(adapter, dev_addr, buff, 2) <0)
		return -1;
	return  0;
}

int aml_i2c_put_word(struct i2c_adapter *adapter, 
		unsigned short dev_addr, unsigned short addr, unsigned short data)
{
	unsigned char buff[4];
	buff[0] = (unsigned char)((addr >> 8) & 0xff);
	buff[1] = (unsigned char)(addr & 0xff);
	buff[2] = (unsigned char)((data >> 8) & 0xff);
	buff[3] = (unsigned char)(data & 0xff);
	if (aml_camera_write_buff(adapter, dev_addr, buff, 4) <0)
		return -1;
	return 0;
}

static int aml_i2c_get_word(struct i2c_adapter *adapter, 
		unsigned short dev_addr, unsigned short addr)
{
	int ret;
	unsigned char buff[4];
	buff[0] = (unsigned char)((addr >> 8) & 0xff);
	buff[1] = (unsigned char)(addr & 0xff);
	if (aml_camera_read_buff(adapter, dev_addr, buff, 2, 2) <0)
		return -1;
	ret =  (buff[0]<< 8)|(buff[1]);
	return ret;
}

static int aml_i2c_get_word_add8(struct i2c_adapter *adapter, 
		unsigned short dev_addr, unsigned short addr)
{
	int ret;
	unsigned char buff[4];
	buff[0] = (unsigned char)((addr >> 8) & 0xff);
	buff[1] = (unsigned char)(addr & 0xff);
	if (aml_camera_read_buff(adapter, dev_addr, buff, 2, 2) <0)
		return -1;
	ret =  buff[0] | (buff[1] << 8);
	return ret;
}


static int aml_i2c_put_word_add8(struct i2c_adapter *adapter, 
		unsigned short dev_addr, unsigned char addr, unsigned short data)
{
	unsigned char buff[4];
	buff[0] = (unsigned char)(addr & 0xff);
	buff[1] = (unsigned char)(data >> 8 & 0xff);
	buff[2] = (unsigned char)(data & 0xff);
	if (aml_camera_write_buff(adapter, dev_addr, buff, 3) <0)
		return -1;
	return  0;
}


extern struct i2c_client *
i2c_new_existing_device(struct i2c_adapter *adap, 
			struct i2c_board_info const *info);

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC0307
int gc0307_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg;  
	reg = aml_i2c_get_byte_add8(adapter, 0x21, 0x00);
	if (reg == 0x99)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC0308
int gc0308_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg;   
	reg = aml_i2c_get_byte_add8(adapter, 0x21, 0x00);
	if (reg == 0x9b)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC0328
int gc0328_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg;   
	reg = aml_i2c_get_byte_add8(adapter, 0x21, 0xf0);
	if (reg == 0x9d)
		ret = 1;
	return ret;
}
#endif 

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC0329
int gc0329_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg;  
	aml_i2c_put_byte_add8(adapter, 0x31, 0xfc, 0x16); //select page 0
	reg = aml_i2c_get_byte_add8(adapter, 0x31, 0x00);
	if (reg == 0xc0)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC2015
int gc2015_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg[2];  
	reg[0] = aml_i2c_get_byte_add8(adapter, 0x30, 0x00);
	reg[1] = aml_i2c_get_byte_add8(adapter, 0x30, 0x01);
	if (reg[0] == 0x20 && reg[1] == 0x05)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_HM2057
int hm2057_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg[2];  
	reg[0] = aml_i2c_get_byte(adapter, 0x24, 0x0001);
	reg[1] = aml_i2c_get_byte(adapter, 0x24, 0x0002);
	if (reg[0] == 0x20 && reg[1] == 0x56)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC2035
int gc2035_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg[2];  
	reg[0] = aml_i2c_get_byte_add8(adapter, 0x3c, 0xf0);
	reg[1] = aml_i2c_get_byte_add8(adapter, 0x3c, 0xf1);
	if (reg[0] == 0x20 && reg[1] == 0x35)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC2155
int gc2155_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg[2];  
	reg[0] = aml_i2c_get_byte_add8(adapter, 0x3c, 0xf0);
	reg[1] = aml_i2c_get_byte_add8(adapter, 0x3c, 0xf1);
	if (reg[0] == 0x21 && reg[1] == 0x55)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GT2005
int gt2005_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg[2];
	reg[0] = aml_i2c_get_byte(adapter, 0x3c, 0x0000);
	reg[1] = aml_i2c_get_byte(adapter, 0x3c, 0x0001);
	if (reg[0] == 0x51 && reg[1] == 0x38)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV2659
int ov2659_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg[2];   
	reg[0] = aml_i2c_get_byte(adapter, 0x30, 0x300a);
	reg[1] = aml_i2c_get_byte(adapter, 0x30, 0x300b);
	if (reg[0] == 0x26 && reg[1] == 0x56)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV3640
int ov3640_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg[2];  
	reg[0] = aml_i2c_get_byte(adapter, 0x3c, 0x300a);
	reg[1] = aml_i2c_get_byte(adapter, 0x3c, 0x300b);
	if (reg[0] == 0x36 && reg[1] == 0x4c)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV3660
int ov3660_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg[2];  
	reg[0] = aml_i2c_get_byte(adapter, 0x3c, 0x300a);
	reg[1] = aml_i2c_get_byte(adapter, 0x3c, 0x300b);
	if (reg[0] == 0x36 && reg[1] == 0x60)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV5640
int ov5640_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg[2];   
	reg[0] = aml_i2c_get_byte(adapter, 0x3c, 0x300a);
	reg[1] = aml_i2c_get_byte(adapter, 0x3c, 0x300b);
	if (reg[0] == 0x56 && reg[1] == 0x40)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV5642
int ov5642_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg[2];  
	reg[0] = aml_i2c_get_byte(adapter, 0x3c, 0x300a);
	reg[1] = aml_i2c_get_byte(adapter, 0x3c, 0x300b);
	if (reg[0] == 0x56 && reg[1] == 0x42)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV7675
int ov7675_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg[2];   
	reg[0] = aml_i2c_get_byte_add8(adapter, 0x21, 0x0a);
	reg[1] = aml_i2c_get_byte_add8(adapter, 0x21, 0x0b);
	if (reg[0] == 0x76 && reg[1] == 0x73)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_SP0A19
int sp0a19_v4l2_probe(struct i2c_adapter *adapter)
{
    int ret = 0;
    unsigned char reg;
    reg = aml_i2c_get_byte_add8(adapter, 0x21, 0x02);
    if (reg == 0xa6)
        ret = 1;
    return ret;
}
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_SP2518
int sp2518_v4l2_probe(struct i2c_adapter *adapter)
{
    int ret = 0;
    unsigned char reg;
    reg = aml_i2c_get_byte_add8(adapter, 0x30, 0x02);
    if (reg == 0x53)
        ret = 1;
    return ret;
}
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_SP0838
int sp0838_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg;    
	reg = aml_i2c_get_byte_add8(adapter, 0x18, 0x02);
	if (reg == 0x27)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_HI253
int hi253_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg;   
	reg = aml_i2c_get_byte_add8(adapter, 0x20, 0x04);
	if (reg == 0x92)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_HM5065
int hm5065_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg[2];   
	reg[0] = aml_i2c_get_byte(adapter, 0x1F, 0x0000);
	reg[1] = aml_i2c_get_byte(adapter, 0x1F, 0x0001);
	if (reg[0] == 0x03 && reg[1] == 0x9e)
		ret = 1;
	return ret;
}
#endif


#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_HI2056
int hi2056_v4l2_probe(struct i2c_adapter *adapter)
{
	int ret = 0;
	unsigned char reg[2];
	reg[0] = aml_i2c_get_byte(adapter, 0x24, 0x0001);
	reg[1] = aml_i2c_get_byte(adapter, 0x24, 0x0002);
        printk("reg[0]=%x, reg[1]=%x\n", reg[0], reg[1]);
	if (reg[0] == 0x20 && reg[1] == 0x56)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV5647
int ov5647_v4l2_probe(struct i2c_adapter *adapter)
{
 	int ret = 0;
	unsigned char reg[2];  
	reg[0] = aml_i2c_get_byte(adapter, 0x36, 0x300a);
	reg[1] = aml_i2c_get_byte(adapter, 0x36, 0x300b);
	printk("reg[0]:%x,reg[1]:%x\n",reg[0],reg[1]);
	if (reg[0] == 0x56 && reg[1] == 0x47)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_AR0543
int ar0543_v4l2_probe(struct i2c_adapter *adapter)
{
 	int ret = 0, reg_val;
	reg_val = aml_i2c_get_word(adapter, 0x36, 0x3000);
	printk("reg:0x%x\n",reg_val);
	if (reg_val == 0x4800)
		ret = 1;
	return ret;
}
#endif


#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_AR0833
int ar0833_v4l2_probe(struct i2c_adapter *adapter)
{
 	int ret = 0, reg_val;
	reg_val = aml_i2c_get_word(adapter, 0x36, 0x3000);
	printk("reg:0x%x\n",reg_val);
	if (reg_val == 0x4B03)
		ret = 1;
	return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_SP1628
int sp1628_v4l2_probe(struct i2c_adapter *adapter)
{
    int ret = 0;
	unsigned char reg[2];   
	reg[0] = aml_i2c_get_byte_add8(adapter, 0x3c, 0x02);
	reg[1] = aml_i2c_get_byte_add8(adapter, 0x3c, 0xa0);
	if (reg[0] == 0x16 && reg[1] == 0x28)
		ret = 1;
    return ret;
}
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_BF3720
int bf3720_v4l2_probe(struct i2c_adapter *adapter)
{
    int ret = 0;
	unsigned char reg[2];   
	reg[0] = aml_i2c_get_byte_add8(adapter, 0x6e, 0xfc);
	reg[1] = aml_i2c_get_byte_add8(adapter, 0x6e, 0xfd);
	if (reg[0] == 0x37 && reg[1] == 0x20)
		ret = 1;
    return ret;
}
#endif

typedef struct {
	unsigned char addr;
	char* name;
	unsigned char pwdn;
	resolution_size_t max_cap_size;
	aml_cam_probe_fun_t probe_func;
}aml_cam_dev_info_t;

static aml_cam_dev_info_t cam_devs[] = {
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC0307
	{
		.addr = 0x21,
		.name = "gc0307",
		.pwdn = 1,
		.max_cap_size = SIZE_640X480,
		.probe_func = gc0307_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC0308
	{
		.addr = 0x21,
		.name = "gc0308",
		.pwdn = 1,
		.max_cap_size = SIZE_640X480,
		.probe_func = gc0308_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC0328
	{
		.addr = 0x21,
		.name = "gc0328",
		.pwdn = 1,
		.max_cap_size = SIZE_640X480,
		.probe_func = gc0328_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC0329
	{
		.addr = 0x31,
		.name = "gc0329",
		.pwdn = 1,
		.max_cap_size = SIZE_640X480,
		.probe_func = gc0329_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC2015
	{
		.addr = 0x30,
		.name = "gc2015",
		.pwdn = 1,
		.max_cap_size = SIZE_1600X1200,
		.probe_func = gc2015_v4l2_probe,
	},
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_HM2057
	{
		.addr = 0x24,
		.name = "hm2057",
		.pwdn = 1,
		.max_cap_size = SIZE_1600X1200,
		.probe_func = hm2057_v4l2_probe,
	},
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC2035
	{
		.addr = 0x3c,
		.name = "gc2035",
		.pwdn = 1,
		.max_cap_size = SIZE_1600X1200,
		.probe_func = gc2035_v4l2_probe,
	},
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GC2155
	{
		.addr = 0x3c,
		.name = "gc2155",
		.pwdn = 1,
		.max_cap_size = SIZE_1600X1200,
		.probe_func = gc2155_v4l2_probe,
	},
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_GT2005
	{
		.addr = 0x3c,
		.name = "gt2005",
		.pwdn = 0,
		.max_cap_size = SIZE_1600X1200,
		.probe_func = gt2005_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV2659
	{
		.addr = 0x30,
		.name = "ov2659",
		.pwdn = 1,
		.max_cap_size = SIZE_1600X1200,
		.probe_func = ov2659_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV3640
	{
		.addr = 0x3c,
		.name = "ov3640",
		.pwdn = 1,
		.max_cap_size = SIZE_2048X1536;
		.probe_func = ov3640_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV3660
	{
		.addr = 0x3c,
		.name = "ov3660",
		.pwdn = 1,
		.max_cap_size = SIZE_2048X1536,
		.probe_func = ov3660_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV5640
	{
		.addr = 0x3c,
		.name = "ov5640",
		.pwdn = 1,
		.max_cap_size = SIZE_2592X1944,
		.probe_func = ov5640_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV5642
	{
		.addr = 0x3c,
		.name = "ov5642",
		.pwdn = 1,
		.max_cap_size = SIZE_2592X1944,
		.probe_func = ov5642_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV5647
    {
		.addr = 0x36, // really value should be 0x6c
		.name = "ov5647",
		.pwdn = 1,
		.max_cap_size = SIZE_2592X1944,
		.probe_func = ov5647_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_OV7675
	{
		.addr = 0x21,
		.name = "ov7675",
		.pwdn = 1,
		.max_cap_size = SIZE_640X480,
		.probe_func = ov7675_v4l2_probe,
	},
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_SP0A19
	{
		.addr = 0x21,
		.name = "sp0a19",
		.pwdn =1,
        .max_cap_size = SIZE_640X480,
		.probe_func = sp0a19_v4l2_probe,
	},
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_SP0838
	{
		.addr = 0x18,
		.name = "sp0838",
		.pwdn = 1,
		.max_cap_size = SIZE_640X480,
		.probe_func = sp0838_v4l2_probe,
	},
#endif
		
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_SP2518
	{
		.addr = 0x30,
		.name = "sp2518",
		.pwdn = 1,
		.max_cap_size = SIZE_1600X1200,
		.probe_func = sp2518_v4l2_probe,
	},
#endif

#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_HI253
	{
		.addr = 0x20,
		.name = "hi253",
		.pwdn = 1,
		.max_cap_size = SIZE_1600X1200,
		.probe_func = hi253_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_HM5065
	{
		.addr = 0x1f,
		.name = "hm5065",
		.pwdn = 0,
		.max_cap_size = SIZE_2592X1944,
		.probe_func = hm5065_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_HI2056
	{
		.addr = 0x24,
		.name = "mipi-hi2056",
		.pwdn = 1,
		.max_cap_size = SIZE_1600X1200,
		.probe_func = hi2056_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_AR0543
	{
		.addr = 0x36,
		.name = "ar0543",
		.pwdn = 0,
		.max_cap_size = SIZE_2592X1944,
		.probe_func = ar0543_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_AR0833
	{
		.addr = 0x36,
		.name = "ar0833",
		.pwdn = 0,
		.max_cap_size = SIZE_2592X1944,
		.probe_func = ar0833_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_SP1628
	{
		.addr = 0x3c,
		.name = "sp1628",
		.pwdn = 1,
		.max_cap_size = SIZE_1280X960,
		.probe_func = sp1628_v4l2_probe,
	},
#endif
#ifdef CONFIG_VIDEO_AMLOGIC_CAPTURE_BF3720
	{
		.addr = 0x6e,
		.name = "bf3720",
		.pwdn = 1,
		.max_cap_size = SIZE_1600X1200,
		.probe_func = bf3720_v4l2_probe,
	},
#endif

};

static aml_cam_dev_info_t* get_cam_info_by_name(const char* name)
{
	int i;
	if (!name)
		return NULL;
	printk("cam_devs num is %d\n", ARRAY_SIZE(cam_devs));
	for (i = 0; i < ARRAY_SIZE(cam_devs); i++) {
		if (!strcmp(name, cam_devs[i].name)) {
			printk("camera dev %s found\n", cam_devs[i].name);
			printk("camera i2c addr: 0x%x\n", cam_devs[i].addr);
			return &cam_devs[i];
		}
	}
	return NULL;
}

struct res_item {
	resolution_size_t size;
	char* name;
};

struct res_item res_item_array[] = {
	{SIZE_320X240, "320X240"},
	{SIZE_640X480, "640X480"},
	{SIZE_720X405, "720X405"},
	{SIZE_800X600, "800X600"},
	{SIZE_960X540, "960X540"},
	{SIZE_1024X576, "1024X576"},
	{SIZE_960X720, "960X720"},
	{SIZE_1024X768, "1024X768"},
	{SIZE_1280X720, "1280X720"},
	{SIZE_1152X864, "1152X864"},
	{SIZE_1366X768, "1366X768"},
	{SIZE_1280X960, "1280X960"},
	{SIZE_1400X1050, "1400X1050"},
	{SIZE_1600X900, "1600X900"},
	{SIZE_1600X1200, "1600X1200"},
	{SIZE_1920X1080, "1920X1080"},
	{SIZE_1792X1344, "1792X1344"},
	{SIZE_2048X1152, "2048X1152"},
	{SIZE_2048X1536, "2048X1536"},
	{SIZE_2304X1728, "2304X1728"},
	{SIZE_2560X1440, "2560X1440"},
	{SIZE_2592X1944, "2592X1944"},
	{SIZE_3072X1728, "3072X1728"},
	{SIZE_2816X2112, "2816X2112"},
	{SIZE_3072X2304, "3072X2304"},
	{SIZE_3200X2400, "3200X2400"},
	{SIZE_3264X2448, "3264X2448"},
	{SIZE_3840X2160, "3840X2160"},
	{SIZE_3456X2592, "3456X2592"},
	{SIZE_3600X2700, "3600X2700"},
	{SIZE_4096X2304, "4096X2304"},
	{SIZE_3672X2754, "3672X2754"},
	{SIZE_3840X2880, "3840X2880"},
	{SIZE_4000X3000, "4000X3000"},
	{SIZE_4608X2592, "4608X2592"},
	{SIZE_4096X3072, "4096X3072"},
	{SIZE_4800X3200, "4800X3200"},
	{SIZE_5120X2880, "5120X2880"},
	{SIZE_5120X3840, "5120X3840"},
	{SIZE_6400X4800, "6400X480"},
	
};


static resolution_size_t get_res_size(const char* res_str)
{
	resolution_size_t ret = SIZE_NULL;
	struct res_item* item;
	int i;
	if (!res_str)
		return SIZE_NULL;
	for (i = 0; i < ARRAY_SIZE(res_item_array); i++) {
		item = &res_item_array[i];
		if (!strcmp(item->name, res_str)) {
			ret = item->size;
			return ret;
		}
	}
	
	return ret;
}

#ifdef CONFIG_ARCH_MESON8B
static inline void cam_spread_spectrum(int spread_spectrum)
{
	printk("spread_spectrum = %d\n", spread_spectrum);
	if (spread_spectrum == 1)
		aml_set_reg32_bits(P_HHI_DPLL_TOP_0, 0x1c1, 0, 9);
	else if (spread_spectrum == 2)
		aml_set_reg32_bits(P_HHI_DPLL_TOP_0, 0x1a1, 0, 9);
	else if (spread_spectrum == 3)
		aml_set_reg32_bits(P_HHI_DPLL_TOP_0, 0x181, 0, 9);
	else if (spread_spectrum == 4)
		aml_set_reg32_bits(P_HHI_DPLL_TOP_0, 0x141, 0, 9);
	else if (spread_spectrum == 5)
		aml_set_reg32_bits(P_HHI_DPLL_TOP_0, 0x121, 0, 9);
}

static inline void cam_enable_clk(int clk, int spread_spectrum)
{
	if (spread_spectrum) {
		cam_spread_spectrum(spread_spectrum);
		aml_set_reg32_bits(P_HHI_MPLL_CNTL7, 0x15d063, 0, 25);
		if (clk == 12000)
			aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 0x6809, 0, 16);
		else
			aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 0x6804, 0, 16);
	} else {
		if (clk == 12000)
			aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 3, 16, 2);
		else
			aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 1, 16, 2);
	}
}

static inline void cam_disable_clk(int spread_spectrum)
{
	if (spread_spectrum) {
		aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 0, 0, 16); //close clock
	} else {
		aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 0, 16, 2); //close clock
	}
}
#elif defined CONFIG_ARCH_MESON8
static inline void cam_enable_clk(int clk, int spread_spectrum)
{
	if (clk == 12000) {
		aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 0, 12, 4);
		aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 1, 0, 7);
	} else if (clk == 18000) {
		aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 0xd, 12, 4);
		aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 0x13, 0, 7);
	} else { //default
		aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 0, 12, 4);
		aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 0, 0, 7);
	}
	aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 1, 11, 1);
}

static inline void cam_disable_clk(int spread_spectrum)
{
	aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 0, 11, 5); //close clock
	aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 0, 0, 7);
}
#elif defined CONFIG_ARCH_MESON6
static inline void cam_enable_clk(int clk, int spread_spectrum)
{
	aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 1, 8, 5); 
}

static inline void cam_disable_clk(int spread_spectrum)
{
	aml_set_reg32_bits(P_HHI_GEN_CLK_CNTL, 0, 8, 5);  //close clock
}
#else
static inline void cam_enable_clk(int clk, int spread_spectrum)
{
}

static inline void cam_disable_clk(int spread_spectrum)
{	
}
#endif

static struct platform_device* cam_pdev = NULL;

void aml_cam_init(aml_cam_info_t* cam_dev)
{
	 struct pinctrl* pin_ctrl;
	//pinmux_set;
	if (cam_dev->bt_path == BT_PATH_GPIO)
		pin_ctrl = pinctrl_get_select((struct device*)(&cam_pdev->dev), "gpio");
	else if (cam_dev->bt_path == BT_PATH_CSI2)
		pin_ctrl = pinctrl_get_select((struct device*)(&cam_pdev->dev), "csi");
	else
		pin_ctrl = pinctrl_get_select((struct device*)(&cam_pdev->dev), "gpio");

	//select XTAL as camera clock
	cam_enable_clk(cam_dev->mclk, cam_dev->spread_spectrum);
	
	msleep(20);
	// set camera power enable
	amlogic_gpio_request(cam_dev->pwdn_pin,"camera");
	amlogic_gpio_direction_output(cam_dev->pwdn_pin,cam_dev->pwdn_act,"camera");
	msleep(20);
	
	amlogic_gpio_request(cam_dev->rst_pin,"camera");
	amlogic_gpio_direction_output(cam_dev->rst_pin,0,"camera");
	msleep(20);
	
	amlogic_gpio_direction_output(cam_dev->rst_pin,1,"camera");
	msleep(20);
	
	// set camera power enable
	amlogic_gpio_direction_output(cam_dev->pwdn_pin,!(cam_dev->pwdn_act),"camera");
	msleep(20);
	
	printk("aml_cams: %s init OK\n",  cam_dev->name);

}

void aml_cam_uninit(aml_cam_info_t* cam_dev)
{
	struct pinctrl *p;
	printk( "aml_cams: %s uninit.\n", cam_dev->name);
	// set camera power disable
	amlogic_gpio_direction_output(cam_dev->pwdn_pin,
					cam_dev->pwdn_act,"camera");
	msleep(5);
	
	cam_disable_clk(cam_dev->spread_spectrum);
	
	p = pinctrl_get(&cam_pdev->dev);
	if (IS_ERR(p))
		return;
	devm_pinctrl_put(p);
}

void aml_cam_flash(aml_cam_info_t* cam_dev, int is_on)
{
	if (cam_dev->flash_support) {
		printk( "aml_cams: %s flash %s.\n", 
				cam_dev->name, is_on ? "on" : "off");
		amlogic_gpio_direction_output(cam_dev->flash_ctrl_pin, 
			cam_dev->flash_ctrl_level ? is_on : !is_on, "camera");
	}
}

void aml_cam_torch(aml_cam_info_t* cam_dev, int is_on)
{
	if (cam_dev->torch_support) {
		printk( "aml_cams: %s torch %s.\n", 
				cam_dev->name, is_on ? "on" : "off");
		amlogic_gpio_direction_output(cam_dev->torch_ctrl_pin, 
			cam_dev->torch_ctrl_level ? is_on : !is_on, "camera");
	} 
}

static struct list_head cam_head = LIST_HEAD_INIT(cam_head);

#define DEBUG_DUMP_CAM_INFO

static int fill_csi_dev(struct device_node* p_node, aml_cam_info_t* cam_dev)
{
	const char* str;
	int ret = 0;
	//aml_cam_dev_info_t* cam_info = NULL;
	//struct i2c_adapter *adapter;

	ret = of_property_read_string(p_node, "clk_channel", &str);
	if (ret) {
		printk("failed to read clock channel, \"a or b\"\n");
		cam_dev->clk_channel = CLK_CHANNEL_A;
	} else {
		printk("clock channel:clk %s\n", str);
		if (strncmp("a", str, 1) == 0){
                        cam_dev->clk_channel = CLK_CHANNEL_A;
                }else{
                        cam_dev->clk_channel = CLK_CHANNEL_B;
                }
	}

        return ret;

}
static int fill_cam_dev(struct device_node* p_node, aml_cam_info_t* cam_dev)
{
	const char* str;
	int ret = 0;
	aml_cam_dev_info_t* cam_info = NULL;
	struct i2c_adapter *adapter;
	unsigned mclk = 0;
	unsigned vcm_mode = 0;
	
	if (!p_node || !cam_dev)
		return -1;
		
	ret = of_property_read_string(p_node, "cam_name", &cam_dev->name);
	if (ret) {
		printk("get camera name failed!\n");
		goto err_out;
	}
	
	ret = of_property_read_string(p_node, "gpio_pwdn", &str);
	if (ret) {
		printk("%s: faild to get gpio_pwdn!\n", cam_dev->name);
		goto err_out;
	}
	ret = amlogic_gpio_name_map_num(str);
	if (ret < 0) {
		printk("%s: faild to map gpio_pwdn !\n", cam_dev->name);
		goto err_out;
	}
	cam_dev->pwdn_pin = ret;
	
	ret = of_property_read_string(p_node, "gpio_rst", &str);
	if (ret) {
		printk("%s: faild to get gpio_rst!\n", cam_dev->name);
		goto err_out;
	}
	ret = amlogic_gpio_name_map_num(str);
	if (ret < 0) {
		printk("%s: faild to map gpio_rst !\n", cam_dev->name);
		goto err_out;
	}
	cam_dev->rst_pin = ret;
	
	ret = of_property_read_string(p_node, "i2c_bus", &str);
	if (ret) {
		printk("%s: faild to get i2c_bus str!\n", cam_dev->name);
		cam_dev->i2c_bus_num = AML_I2C_MASTER_A;
	} else {
		if (!strncmp(str, "i2c_bus_a", 9))
			cam_dev->i2c_bus_num = AML_I2C_MASTER_A;
		else if (!strncmp(str, "i2c_bus_b", 9))
			cam_dev->i2c_bus_num = AML_I2C_MASTER_B;
		else if (!strncmp(str, "i2c_bus_c", 9))
			cam_dev->i2c_bus_num = AML_I2C_MASTER_C;
		else if (!strncmp(str, "i2c_bus_d", 9))
			cam_dev->i2c_bus_num = AML_I2C_MASTER_D;
		else if (!strncmp(str, "i2c_bus_ao", 9))
			cam_dev->i2c_bus_num = AML_I2C_MASTER_AO;
		else
			cam_dev->i2c_bus_num = AML_I2C_MASTER_A; 
	}
	
	cam_info = get_cam_info_by_name(cam_dev->name);
	if (cam_info == NULL) {
		printk("camera %s is not support\n", cam_dev->name);
		ret = -1;
		goto err_out;
	} 
	
	of_property_read_u32(p_node, "spread_spectrum", &cam_dev->spread_spectrum);
	
	cam_dev->pwdn_act = cam_info->pwdn;
	cam_dev->i2c_addr = cam_info->addr;
	printk("camer addr: 0x%x\n", cam_dev->i2c_addr);
	printk("camer i2c bus: %d\n", cam_dev->i2c_bus_num);
	
	/* test if the camera is exist */
	adapter = i2c_get_adapter(cam_dev->i2c_bus_num);
	if (adapter && cam_info->probe_func) {
		aml_cam_init(cam_dev);
		if (cam_info->probe_func(adapter)!= 1) {
			printk("camera %s not on board\n", cam_dev->name);
			ret = -1;
			aml_cam_uninit(cam_dev);
			goto err_out;
		}
		aml_cam_uninit(cam_dev);
	} else {
		printk("can not do probe function\n");
		ret = -1;
		goto err_out;
	}
	
	of_property_read_u32(p_node, "front_back", &cam_dev->front_back);
	of_property_read_u32(p_node, "mirror_flip", &cam_dev->m_flip);
	of_property_read_u32(p_node, "vertical_flip", &cam_dev->v_flip);
	
	ret = of_property_read_string(p_node, "max_cap_size", &str);
	if (ret) {
		printk("failed to read max_cap_size\n");
	} else {
		printk("max_cap_size :%s\n",str);
		cam_dev->max_cap_size = get_res_size(str);
	}
	if (cam_dev->max_cap_size == SIZE_NULL)
		cam_dev->max_cap_size = cam_info->max_cap_size;
	
	ret = of_property_read_string(p_node, "bt_path", &str);
	if (ret) {
		printk("failed to read bt_path\n");
		cam_dev->bt_path = BT_PATH_GPIO;
	} else {
		printk("bt_path :%s\n", (char*)cam_dev->bt_path);
		if (strncmp("csi", str, 3) == 0) 
			cam_dev->bt_path = BT_PATH_CSI2;
		else
			cam_dev->bt_path = BT_PATH_GPIO;
	}

	ret = of_property_read_u32(p_node, "mclk", &mclk);
	if (ret) {
		cam_dev->mclk = 24000;
	} else {
		cam_dev->mclk = mclk;
	}
	
	ret = of_property_read_u32(p_node, "vcm_mode", &vcm_mode);
	if (ret) {
		cam_dev->vcm_mode = 0;
	} else {
		cam_dev->vcm_mode = vcm_mode;
	}
	printk("vcm mode is %d\n", cam_dev->vcm_mode);
	
	ret = of_property_read_u32(p_node, "flash_support", &cam_dev->flash_support);
	if (cam_dev->flash_support){
                of_property_read_u32(p_node, "flash_ctrl_level", &cam_dev->flash_ctrl_level);
                ret = of_property_read_string(p_node, "flash_ctrl_pin", &str);
		if (ret) {
			printk("%s: faild to get flash_ctrl_pin!\n", cam_dev->name);
			cam_dev->flash_support = 0;
		} else {
			ret = amlogic_gpio_name_map_num(str);
			if (ret < 0) {
				printk("%s: faild to map flash_ctrl_pin !\n", cam_dev->name);
				cam_dev->flash_support = 0;
				cam_dev->flash_ctrl_level = 0;
			}
			cam_dev->flash_ctrl_pin = ret;  
			amlogic_gpio_request(cam_dev->flash_ctrl_pin,"camera");
		}
        }
        
        ret = of_property_read_u32(p_node, "torch_support", &cam_dev->torch_support);
	if (cam_dev->torch_support){
                of_property_read_u32(p_node, "torch_ctrl_level", &cam_dev->torch_ctrl_level);
                ret = of_property_read_string(p_node, "torch_ctrl_pin", &str);
		if (ret) {
			printk("%s: faild to get torch_ctrl_pin!\n", cam_dev->name);
			cam_dev->torch_support = 0;
		} else {
			ret = amlogic_gpio_name_map_num(str);
			if (ret < 0) {
				printk("%s: faild to map flash_ctrl_pin !\n", cam_dev->name);
				cam_dev->torch_support = 0;
				cam_dev->torch_ctrl_level = 0;
			}
			cam_dev->torch_ctrl_pin = ret;  
			amlogic_gpio_request(cam_dev->torch_ctrl_pin,"camera");
		}
        }

	ret = of_property_read_string(p_node, "interface", &str);
	if (ret) {
		printk("failed to read camera interface \"mipi or dvp\"\n");
		cam_dev->interface = CAM_DVP;
	} else {
		printk("camera interface:%s\n", str);
		if (strncmp("dvp", str, 1) == 0){
                        cam_dev->interface = CAM_DVP;
                }else{
                        cam_dev->interface = CAM_MIPI;
                }
	}
        if( CAM_MIPI == cam_dev->interface ){
		ret = fill_csi_dev( p_node, cam_dev);
                if ( ret < 0 )
                        goto err_out;
        }
        
        ret = of_property_read_string(p_node, "bayer_fmt", &str);
	if (ret) {
		printk("failed to read camera bayer fmt \n");
		cam_dev->bayer_fmt = TVIN_GBRG;
	} else {
		printk("color format:%s\n", str);
		if (strncmp("BGGR", str, 4) == 0){
                        cam_dev->bayer_fmt = TVIN_BGGR;
                } else if (strncmp("RGGB", str, 4) == 0){
                        cam_dev->bayer_fmt = TVIN_RGGB;
                } else if (strncmp("GBRG", str, 4) == 0){
                        cam_dev->bayer_fmt = TVIN_GBRG;
                } else if (strncmp("GRBG", str, 4) == 0){
                        cam_dev->bayer_fmt = TVIN_GRBG;
                } else {
                	cam_dev->bayer_fmt = TVIN_GBRG;
                }
	}

	ret = of_property_read_string(p_node, "config_path", &cam_dev->config);
	// cam_dev->config = "/system/etc/myconfig";
	//ret = 0;
	if(ret){
		printk("failed to read config_file path\n");
	}else{
		printk("config path :%s\n",cam_dev->config);
	}
	
#ifdef DEBUG_DUMP_CAM_INFO
	printk("=======cam %s info=======\n"
		"i2c_bus_num: %d\n"
		"pwdn_act: %d\n"
		"front_back: %d\n"
		"m_flip: %d\n"
		"v_flip: %d\n"
		"i2c_addr: 0x%x\n"
		"config path:%s\n"
		"bt_path:%d\n",
		cam_dev->name,
		cam_dev->i2c_bus_num, cam_dev->pwdn_act, cam_dev->front_back,
		cam_dev->m_flip, cam_dev->v_flip, cam_dev->i2c_addr,cam_dev->config,
		cam_dev->bt_path);
#endif /* DEBUG_DUMP_CAM_INFO */
	
	ret = 0;
	
err_out:
	return ret;	
}

static int  do_read_work(char argn ,char **argv)
{
	unsigned int dev_addr, reg_addr, data_len = 1, result;     
	unsigned int i2c_bus;    
	struct i2c_adapter *adapter;   

	if (argn < 4){
		printk("args num error");
		return -1;
	}
	
	
	if (!strncmp(argv[1], "i2c_bus_ao", 9))
		i2c_bus = AML_I2C_MASTER_AO;
	else if (!strncmp(argv[1], "i2c_bus_a", 9))
		i2c_bus = AML_I2C_MASTER_A;
	else if (!strncmp(argv[1], "i2c_bus_b", 9))
		i2c_bus = AML_I2C_MASTER_B;
	else if (!strncmp(argv[1], "i2c_bus_c", 9))
		i2c_bus = AML_I2C_MASTER_C;
	else if (!strncmp(argv[1], "i2c_bus_d", 9))
		i2c_bus = AML_I2C_MASTER_D;
	else {
		printk("bus name error!\n");
		return -1;
	}
	
        adapter = i2c_get_adapter(i2c_bus);
        
        if (adapter == NULL) {
        	printk("no adapter!\n");
		return -1;
	}
        
	dev_addr = simple_strtol(argv[2],NULL,16);
	reg_addr = simple_strtol(argv[3],NULL,16);
	if (argn == 5) {
		printk("argv[4] is %s\n", argv[4]);
		data_len = simple_strtol(argv[4],NULL,16);
	}
	
	if (reg_addr > 256) {
		if (data_len != 2) {
			result = aml_i2c_get_byte(adapter, dev_addr, reg_addr);
			printk("register [0x%04x]=0x%02x\n", reg_addr, result);
		} else {
			result = aml_i2c_get_word(adapter, dev_addr, reg_addr);
			printk("register [0x%04x]=0x%04x\n", reg_addr, result);
		}
	} else {
		if (data_len != 2) {
			result = aml_i2c_get_byte_add8(adapter, dev_addr, reg_addr);
			printk("register [0x%02x]=0x%02x\n", reg_addr, result);
		} else {
			result = aml_i2c_get_word_add8(adapter, dev_addr, reg_addr);
			printk("register [0x%02x]=0x%04x\n", reg_addr, result);
		}
	}
		
	return 0;
}

static int do_write_work(char argn ,char **argv)
{
	unsigned int dev_addr, reg_addr, reg_val, data_len = 1, ret = 0;     
	unsigned int i2c_bus;    
	struct i2c_adapter *adapter;   


	if (argn < 5){
		printk("args num error");
		return -1;
	}
	
	if (!strncmp(argv[1], "i2c_bus_a", 9))
		i2c_bus = AML_I2C_MASTER_A;
	else if (!strncmp(argv[1], "i2c_bus_b", 9))
		i2c_bus = AML_I2C_MASTER_B;
	else if (!strncmp(argv[1], "i2c_bus_c", 9))
		i2c_bus = AML_I2C_MASTER_C;
	else if (!strncmp(argv[1], "i2c_bus_d", 9))
		i2c_bus = AML_I2C_MASTER_D;
	else if (!strncmp(argv[1], "i2c_bus_ao", 9))
		i2c_bus = AML_I2C_MASTER_AO;
	else {
		printk("bus name error!\n");
		return -1;
	}
	
        adapter = i2c_get_adapter(i2c_bus);
        
        if (adapter == NULL) {
        	printk("no adapter!\n");
		return -1;
	}
        
	dev_addr = simple_strtol(argv[2],NULL,16);
	reg_addr = simple_strtol(argv[3],NULL,16);
	reg_val = simple_strtol(argv[4],NULL,16);
	if (argn == 6)
		data_len = simple_strtol(argv[5],NULL,16);
	if (reg_addr > 256) {
		if (data_len != 2) {
			if(aml_i2c_put_byte(adapter, dev_addr, reg_addr, reg_val) < 0) {
				printk("write error\n");
				ret = -1;
			} else {
				printk("write ok\n");
				ret = 0;
			}
		} else {
			if(aml_i2c_put_word(adapter, dev_addr, reg_addr, reg_val) < 0) {
				printk("write error\n");
				ret = -1;
			} else {
				printk("write ok\n");
				ret = 0;
			}
		}
	} else {
		if (data_len != 2) {
			if (aml_i2c_put_byte_add8(adapter, dev_addr, reg_addr, reg_val) < 0){
				printk("write error\n");
				ret = -1;
			} else {
				printk("write ok\n");
				ret = 0;
			}
		} else {
			if (aml_i2c_put_word_add8(adapter, dev_addr, reg_addr, reg_val) < 0){
				printk("write error\n");
				ret = -1;
			} else {
				printk("write ok\n");
				ret = 0;
			}
		}
	}
		
	return ret;
}

static struct class* cam_clsp;


static ssize_t show_help(struct class* class, struct class_attribute* attr,
	char* buf)
{
	ssize_t size = 0;
	printk( "echo [read | write] i2c_bus_type device_address register_address [value] [data_len] > i2c_debug\n"
		"i2c_bus_type are: i2c_bus_ao, i2c_bus_a, i2c_bus_b, i2c_bus_c, i2c_bus_d\n"
		"e.g.: echo read i2c_bus_ao 0x3c 0x18 1\n"
		"      echo write i2c_bus_ao 0x3c 0x18 0x24 1\n");
	return size;
}

static ssize_t store_i2c_debug(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
	int argn;
	char * buf_work,*p,*para;
	char cmd;
	char * argv[6];
	
	buf_work = kstrdup(buf, GFP_KERNEL);
	p = buf_work;
	
	for(argn = 0; argn < 6; argn++){
		para = strsep(&p," ");
		if(para == NULL)
			break;
		argv[argn] = para;
		printk("argv[%d] = %s\n",argn,para);
	}
	
	if(argn < 4 || argn > 6)
		goto end;
		
	cmd = argv[0][0];
	switch (cmd){
	case 'r':
	case 'R':
		do_read_work(argn,argv);
		break;
	case 'w':
	case 'W':
		do_write_work(argn,argv);
		break;
	}
	return count;
end:
	printk("error command!\n");
	kfree(buf_work);
	return -EINVAL;	
}

static LIST_HEAD(info_head);

static ssize_t cam_info_show(struct class* class, 
			struct class_attribute* attr, char* buf)
{
	struct list_head* p;
	aml_cam_info_t* cam_info = NULL;
	int count = 0;
	if (!list_empty(&info_head)) {
		count += sprintf(&buf[count], "name\t\tversion\t\t\t\tface_dir\t"
					"i2c_addr\n");
		list_for_each(p, &info_head) {
			cam_info = list_entry(p, aml_cam_info_t, info_entry);
			if (cam_info) {
				count += sprintf(&buf[count], "%s\t\t%s\t\t%s"
					"\t\t0x%x\n", 
					cam_info->name, cam_info->version, 
					cam_info->front_back?"front":"back",
					cam_info->i2c_addr);
			}
		}
	}
	return count;
}

static struct class_attribute aml_cam_attrs[]={
	__ATTR(i2c_debug,  S_IRUGO | S_IWUSR, show_help, store_i2c_debug),
	__ATTR_RO(cam_info),
	__ATTR(help,  S_IRUGO | S_IWUSR, show_help, NULL),
	__ATTR_NULL,
};

int aml_cam_info_reg(aml_cam_info_t* cam_info)
{
	int ret = -1;
	if (cam_info) {
		//printk("reg camera %s\n", cam_info->name);
		list_add(&cam_info->info_entry, &info_head);
		ret = 0;
	}
	return ret;
}

int aml_cam_info_unreg(aml_cam_info_t* cam_info)
{
	int ret = -1;
	struct list_head* p, *n;
	aml_cam_info_t* tmp_info = NULL;
	if (cam_info) {
		list_for_each_safe(p, n, &info_head) {
			tmp_info = list_entry(p, aml_cam_info_t, info_entry);
			if (tmp_info == cam_info) {
				list_del(p);
				return 0;
			}
		}
	}
	return ret;
}

static int aml_cams_probe(struct platform_device *pdev)
{
	//printk("##############aml_cams_probe start############\n");
	
	struct device_node* cams_node = pdev->dev.of_node;
	struct device_node* child;
	struct i2c_board_info board_info;
	struct i2c_adapter *adapter;
	int i;
	aml_cam_info_t temp_cam;
	cam_pdev = pdev;
	for_each_child_of_node(cams_node, child) {
		/*
		temp_cam = kzalloc(sizeof(aml_cam_info_t), GFP_KERNEL); 
		if (!temp_cam) {
			printk("alloc mem error\n");
			return -ENOMEM;
		}
		*/
		memset(&temp_cam, 0, sizeof(aml_cam_info_t));
		
		if (fill_cam_dev(child, &temp_cam)) {
			continue;
		}
		
		/* register exist camera */
		memset(&board_info, 0, sizeof(board_info));
		strncpy(board_info.type, temp_cam.name, I2C_NAME_SIZE);
		adapter = i2c_get_adapter(temp_cam.i2c_bus_num);
		board_info.addr = temp_cam.i2c_addr;
		board_info.platform_data = &temp_cam;
		printk("new i2c device\n");
		i2c_new_existing_device(adapter, &board_info);	
	}
	//printk("aml probe finish\n");
	cam_clsp = class_create(THIS_MODULE, "aml_camera");
	for(i = 0; aml_cam_attrs[i].attr.name; i++){
		if(class_create_file(cam_clsp, &aml_cam_attrs[i]) < 0)
			return -1;
	}
	return 0;
}

static int aml_cams_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id cams_prober_dt_match[]={
	{	
		.compatible = "amlogic,cams_prober",
	},
	{},
};

static  struct platform_driver aml_cams_prober_driver = {
	.probe		= aml_cams_probe,
	.remove		= aml_cams_remove,
	.driver		= {
		.name	= "aml_cams_prober",
		.owner	= THIS_MODULE,
		.of_match_table = cams_prober_dt_match,
	},
};

static int __init aml_cams_prober_init(void)
{
	if (platform_driver_register(&aml_cams_prober_driver)){
		printk(KERN_ERR"aml_cams_probre_driver register failed\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit aml_cams_prober_exit(void)
{
	platform_driver_unregister(&aml_cams_prober_driver);
}

module_init(aml_cams_prober_init);
module_exit(aml_cams_prober_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Amlogic Cameras prober driver");


