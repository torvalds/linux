/* ----------------------------------------------------------------------- *
 *
 This file created by albert RDA Inc
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/string.h>
#include <linux/rtc.h>       /* get the user-level API */
#include <linux/bcd.h>
#include <linux/list.h>
#include <linux/delay.h>


#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/nfs_mount.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/tty.h>
#include <linux/syscalls.h>
#include <asm/termbits.h>
#include <linux/serial.h>

#include <linux/wakelock.h>

#define RDA5890_USE_CRYSTAL // if use share crystal should close this
#define RDA5990_USE_DCDC

#define u32 unsigned int
#define u8 unsigned char
#define u16 unsigned short

#define RDA_I2C_CHANNEL		(0)
#define RDA_WIFI_CORE_ADDR (0x13 << 1)
#define RDA_WIFI_RF_ADDR (0x14 << 1) //correct add is 0x14
#define RDA_BT_CORE_ADDR (0x15 << 1)
#define RDA_BT_RF_ADDR (0x16 << 1)

#define I2C_MASTER_ACK              (1<<0)
#define I2C_MASTER_RD               (1<<4)
#define I2C_MASTER_STO              (1<<8)
#define I2C_MASTER_WR               (1<<12)
#define I2C_MASTER_STA              (1<<16)

#define RDA_WIFI_RF_I2C_DEVNAME "rda_wifi_rf_i2c"
#define RDA_WIFI_CORE_I2C_DEVNAME "rda_wifi_core_i2c"
#define RDA_BT_RF_I2C_DEVNAME "rda_bt_rf_i2c"
#define RDA_BT_CORE_I2C_DEVNAME "rda_bt_core_i2c"

static struct mutex i2c_rw_lock;
//extern int
//i2c_register_board_info(int busnum, struct i2c_board_info const *info,
//                        unsigned n);


static unsigned short wlan_version = 0;
static struct wake_lock rda_5990_wake_lock;
static struct delayed_work   rda_5990_sleep_worker;
static struct i2c_client * rda_wifi_core_client = NULL;
static struct i2c_client * rda_wifi_rf_client = NULL;
static struct i2c_client * rda_bt_core_client = NULL;
static struct i2c_client * rda_bt_rf_client = NULL;

static const struct i2c_device_id wifi_rf_i2c_id[] = {{RDA_WIFI_RF_I2C_DEVNAME, 0}, {}};
MODULE_DEVICE_TABLE(i2c, wifi_rf_i2c_id);

static unsigned short wifi_rf_force[] = { RDA_I2C_CHANNEL, RDA_WIFI_RF_ADDR, I2C_CLIENT_END, I2C_CLIENT_END };

static struct i2c_board_info __initdata i2c_rda_wifi_rf={ I2C_BOARD_INFO(RDA_WIFI_RF_I2C_DEVNAME, (RDA_WIFI_RF_ADDR>>1))};
static struct i2c_driver rda_wifi_rf_driver;



static const struct i2c_device_id wifi_core_i2c_id[] = {{RDA_WIFI_CORE_I2C_DEVNAME, 0}, {}};
MODULE_DEVICE_TABLE(i2c, wifi_core_i2c_id);

static unsigned short wifi_core_force[] = { RDA_I2C_CHANNEL, RDA_WIFI_CORE_ADDR, I2C_CLIENT_END, I2C_CLIENT_END };
static struct i2c_board_info __initdata i2c_rda_wifi_core={ I2C_BOARD_INFO(RDA_WIFI_CORE_I2C_DEVNAME, (RDA_WIFI_CORE_ADDR>>1))};
static struct i2c_driver rda_wifi_core_driver;



static const struct i2c_device_id bt_rf_i2c_id[] = {{RDA_BT_RF_I2C_DEVNAME, 0}, {}};

MODULE_DEVICE_TABLE(i2c, bt_rf_i2c_id);

static unsigned short bt_rf_force[] = { RDA_I2C_CHANNEL, RDA_BT_RF_ADDR, I2C_CLIENT_END, I2C_CLIENT_END };
static struct i2c_board_info __initdata i2c_rda_bt_rf={ I2C_BOARD_INFO(RDA_BT_RF_I2C_DEVNAME, (RDA_BT_RF_ADDR>>1))};
static struct i2c_driver rda_bt_rf_driver;

static const struct i2c_device_id bt_core_i2c_id[] = {{RDA_BT_CORE_I2C_DEVNAME, 0}, {}};
MODULE_DEVICE_TABLE(i2c, bt_core_i2c_id);

static unsigned short bt_core_force[] = { RDA_I2C_CHANNEL, RDA_BT_CORE_ADDR, I2C_CLIENT_END, I2C_CLIENT_END };
static struct i2c_board_info __initdata i2c_rda_bt_core={ I2C_BOARD_INFO(RDA_BT_CORE_I2C_DEVNAME, (RDA_BT_CORE_ADDR>>1))};
static struct i2c_driver rda_bt_core_driver;


static u8 isBigEnded = 0;
static u8 wifi_in_test_mode = 0;

static int i2c_write_data_4_addr_4_data(struct i2c_client* client, const u32 addr, const u32 data)
{
	unsigned char ADDR[4], DATA[4];
	int ret = 0;

	if(!isBigEnded)
	{
		ADDR[0] = addr >> 24;
		ADDR[1] = addr >> 16;
		ADDR[2] = addr >> 8;
		ADDR[3] = addr >> 0;

		DATA[0] = data >> 24;
		DATA[1] = data >> 16;
		DATA[2] = data >> 8;
		DATA[3] = data >> 0;
	}
	else
	{
		memcpy(ADDR, &addr, sizeof(u32));
		memcpy(DATA, &data, sizeof(u32));
	}

	ret = i2c_master_send(client, (char*)ADDR, 4);
	if (ret < 0)
	{
		printk(KERN_INFO "***i2c_write_data_4_addr_4_data send:0x%X err:%d\n", addr,ret);
		return -1;
	}

	ret = i2c_master_send(client, (char*)DATA, 4);
	if (ret < 0)
	{
		printk(KERN_INFO "***i2c_write_data_4_addr_4_data send:0x%X err:%d\n", data,ret);
		return -1;
	}

	return 0;
}


static int i2c_read_4_addr_4_data(struct i2c_client* client, const u32 addr, u32* data)
{
	unsigned char ADDR[4], DATA[4];
	int ret = 0;

	if(!isBigEnded)
	{
		ADDR[0] = addr >> 24;
		ADDR[1] = addr >> 16;
		ADDR[2] = addr >> 8;
		ADDR[3] = addr >> 0;
	}
	else
	{
		memcpy(ADDR, &addr, sizeof(u32));
	}

	ret = i2c_master_send(client, (char*)ADDR, 4);
	if (ret < 0)
	{
		printk(KERN_INFO "***i2c_read_4_addr_4_data send:0x%X err:%d\n", addr,ret);
		return -1;
	}

	ret = i2c_master_recv(client, (char*)DATA, 4);
	if (ret < 0)
	{
		printk(KERN_INFO "***i2c_read_4_addr_4_data send:0x%X err:%d\n", addr,ret);
		return -1;
	}

	if(!isBigEnded)
	{
		data[3] = DATA[0];
		data[2] = DATA[1];
		data[1] = DATA[2];
		data[0] = DATA[3];
	}
	else
		memcpy(data, DATA, sizeof(u32));

	return 0;
}


static int i2c_write_1_addr_2_data(struct i2c_client* client, const u8 addr, const u16 data)
{
	unsigned char  DATA[3];
	int ret = 0;

	if(!isBigEnded)
	{
		DATA[0] = addr;
		DATA[1] = data >> 8;
		DATA[2] = data >> 0;
	}
	else
	{
		DATA[0] = addr;
		DATA[1] = data >> 0;
		DATA[2] = data >> 8;
	}

	ret = i2c_master_send(client, (char*)DATA, 3);
	if (ret < 0)
	{
		printk(KERN_INFO "***i2c_write_1_addr_2_data send:0x%X err:%d bigendia: %d \n", addr,ret, isBigEnded);
		return -1;
	}

	return 0;
}


static int i2c_read_1_addr_2_data(struct i2c_client* client, const u8 addr, u16* data)
{
	unsigned char DATA[2];
	int ret = 0;

	ret = i2c_master_send(client, (char*)&addr, 1);
	if (ret < 0)
	{
		printk(KERN_INFO "***i2c_read_1_addr_2_data send:0x%X err:%d\n", addr,ret);
		return -1;
	}

	ret = i2c_master_recv(client, DATA, 2);
	if (ret < 0)
	{
		printk(KERN_INFO "***i2c_read_1_addr_2_data send:0x%X err:%d\n", addr,ret);
		return -1;
	}

	if(!isBigEnded)
	{
		*data = (DATA[0] << 8) | DATA[1];
	}
	else
	{
		*data = (DATA[1] << 8) | DATA[0];
	}
	return 0;
}


static int rda_wifi_rf_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int result = 0;

	rda_wifi_rf_client = client;
	return result;
}

static int rda_wifi_rf_remove(struct i2c_client *client)
{
	return 0;
}

static int rda_wifi_rf_detect(struct i2c_client *client, int kind, struct i2c_board_info *info)
{  
	strcpy(info->type, RDA_WIFI_RF_I2C_DEVNAME);   
	return 0;   
}

static struct i2c_driver rda_wifi_rf_driver = {
	.probe = rda_wifi_rf_probe,
	.remove = rda_wifi_rf_remove,
	.detect = rda_wifi_rf_detect,
	.driver.name = RDA_WIFI_RF_I2C_DEVNAME,
	.id_table = wifi_rf_i2c_id,
//	.address_data = &rda_wifi_rf_addr_data,
	//.address_list = (const unsigned short*) wifi_rf_force,
};

static int rda_wifi_core_detect(struct i2c_client *client, int kind, struct i2c_board_info *info)
{
	strcpy(info->type, RDA_WIFI_CORE_I2C_DEVNAME);
	return 0;   
}

static int rda_wifi_core_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int result = 0;

	rda_wifi_core_client = client;
	return result;
}

static int rda_wifi_core_remove(struct i2c_client *client)
{
	return 0;
}

int rda_wifi_power_off(void);
static void rda_wifi_shutdown(struct i2c_client * client)
{
	printk("rda_wifi_shutdown \n");
	rda_wifi_power_off();
}

static struct i2c_driver rda_wifi_core_driver = 
{
	.probe = rda_wifi_core_probe,
	.remove = rda_wifi_core_remove,
	.detect = rda_wifi_core_detect,
	.shutdown = rda_wifi_shutdown,
	.driver.name = RDA_WIFI_CORE_I2C_DEVNAME,
	.id_table = wifi_core_i2c_id,
	//.address_data = &rda_wifi_core_addr_data,
	//.address_list = (const unsigned short*)wifi_core_force,
};

const u32 wifi_core_init_data[][2] = 
{

};

u16 wifi_off_data[][2] = 
{
	{ 0x3F, 0x0001 }, //page up
	{ 0x31, 0x0B40 }, //power off wifi
	{ 0x3F, 0x0000 }, //page down
};

u16 wifi_en_data[][2] = 
{
    //item:VerD_wf_on_2012_02_08
    {0x3f, 0x0001},
#ifdef RDA5990_USE_DCDC     /*houzhen update Mar 15 2012 */
    {0x23, 0x8FA1},//20111001 higher AVDD voltage to improve EVM to 0x8f21 download current -1db 0x8fA1>>0x8bA1   
#else
	{0x23, 0x0FA1},
#endif
    {0x31, 0x0B40 }, //power off wifi
//    {0x22, 0xD3C7},//for ver.c 20111109, txswitch
    {0x24, 0x80C8},//freq_osc_in[1:0]00  0x80C8 >> 0x80CB
    {0x27, 0x4925},//for ver.c20111109, txswitch
    //                {0x28, 0x80A1}, //BT_enable 
    {0x31, 0x8140},//enable wifi  
    {0x32, 0x0113},//set_ rdenout_ldooff_wf=0; rden4in_ldoon_wf=1						
    //                {0x39, 0x0004}, 	//uart switch to wf  
    {0x3F, 0x0000}, //page down
};


u16 wifi_dc_cal_data[][2]=
{
	{0x3f, 0x0000},
	{0x30, 0x0248},
	{0x30, 0x0249},
	//{wait 200ms; } here
};

u16 wifi_dig_reset_data[][2]=
{
	{0x3F,  0x0001},
	{0x31,  0x8D40},
	{0x31,  0x8F40},
	{0x31,  0x8b40},
	{0x3F,  0x0000},
};

u16 wifi_rf_init_data_verE[][2] = 
{
	{0x3f, 0x0000},
	//{;;set_rf_swi},ch
	{0x06, 0x0101},
	{0x07, 0x0101},
	{0x08, 0x0101},
	{0x09, 0x0101},
	{0x0A, 0x002C},//aain_0
	{0x0D, 0x0507},
	{0x0E, 0x2300},
	{0x0F, 0x5689},//
	//{;;//set_RF  },
	{0x10, 0x0f78},//20110824
	{0x11, 0x0602},
	{0x13, 0x0652},//adc_tuning_bit[011]
	{0x14, 0x8886},
	{0x15, 0x0990},
	{0x16, 0x049f},
	{0x17, 0x0990},
	{0x18, 0x049F},
	{0x19, 0x3C01},
	{0x1C, 0x0934},
	{0x1D, 0xFF00},//for ver.D20120119for temperature 70 degree
	//{0x1F, 0x01F8},//for ver.c20111109
	//{0x1F, 0x0300},//for burst tx 不锁
	{0x20, 0x06E4},
	{0x21, 0x0ACF},//for ver.c20111109,dr dac reset,dr txflt reset
	{0x22, 0x24DC},
	{0x23, 0x23FF},
	{0x24, 0x00FC},
	{0x26, 0x004F},//004F >> 005f premote pa 
	{0x27, 0x171D},///mdll*7
	{0x28, 0x031D},///mdll*7
	{0x2A, 0x2860},//et0x2849-8.5p  :yd 0x2861-7pf C1,C2=6.8p
	{0x2B, 0x0800},//bbpll,or ver.c20111116
	{0x32, 0x8a08},
	{0x33, 0x1D02},//liuyanan
	//{;;//agc_gain},
	{0x36, 0x02f4}, //00F8;//gain_7
	{0x37, 0x01f4}, //0074;//aain_6
	{0x38, 0x21d4}, //0014;//gain_5
	{0x39, 0x25d4}, //0414;//aain_4
	{0x3A, 0x2584}, //1804;//gain_3
	{0x3B, 0x2dc4}, //1C04;//aain_2
	{0x3C, 0x2d04}, //1C02;//gain_1
	{0x3D, 0x2c02}, //3C01;//gain_0
	{0x33, 0x1502},//liuyanan
	//{;;SET_channe},_to_11
	{0x1B, 0x0001},//set_channel   
	{0x30, 0x024D},
	{0x29, 0xD468},
	{0x29, 0x1468},
	{0x30, 0x0249},
	{0x3f, 0x0000},
};

u16 wifi_rf_init_data[][2] = 
{
	{0x3f, 0x0000},
	//{;;set_rf_swi},ch
	{0x06, 0x0101},
	{0x07, 0x0101},
	{0x08, 0x0101},
	{0x09, 0x0101},
	{0x0A, 0x002C},//aain_0
	{0x0D, 0x0507},
	{0x0E, 0x2300},//2012_02_20  
	{0x0F, 0x5689},//
	//{;;//set_RF  },
	{0x10, 0x0f78},//20110824
	{0x11, 0x0602},
	{0x13, 0x0652},//adc_tuning_bit[011]
	{0x14, 0x8886},
	{0x15, 0x0990},
	{0x16, 0x049f},
	{0x17, 0x0990},
	{0x18, 0x049F},
	{0x19, 0x3C01},//sdm_vbit[3:0]=1111
	{0x1C, 0x0934},
	{0x1D, 0xFF00},//for ver.D20120119for temperature 70 degree 0xCE00 >> 0xFF00
	{0x1F, 0x0300},//div2_band_48g_dr=1;div2_band_48g_reg[8:0]
	{0x20, 0x06E4},
	{0x21, 0x0ACF},//for ver.c20111109,dr dac reset,dr txflt reset
	{0x22, 0x24DC},
	{0x23, 0x23FF},
	{0x24, 0x00FC},
	{0x26, 0x004F},//004F >> 005f premote pa 
	{0x27, 0x171D},///mdll*7
	{0x28, 0x031D},///mdll*7
	{0x2A, 0x2860},//et0x2849-8.5p  :yd 0x2861-7pf
	{0x2B, 0x0800},//bbpll,or ver.c20111116
	{0x32, 0x8a08},
	{0x33, 0x1D02},//liuyanan
	//{;;//agc_gain},
	{0x36, 0x02f4}, //00F8;//gain_7
	{0x37, 0x01f4}, //0074;//aain_6
	{0x38, 0x21d4}, //0014;//gain_5
	{0x39, 0x25d4}, //0414;//aain_4
	{0x3A, 0x2584}, //1804;//gain_3
	{0x3B, 0x2dc4}, //1C04;//aain_2
	{0x3C, 0x2d04}, //1C02;//gain_1
	{0x3D, 0x2c02}, //3C01;//gain_0
	{0x33, 0x1502},//liuyanan
	//{;;SET_channe},_to_11
	{0x1B, 0x0001},//set_channel   
	{0x30, 0x024D},
	{0x29, 0xD468},
	{0x29, 0x1468},
	{0x30, 0x0249},
	{0x3f, 0x0000},
};
u16 wifi_uart_debug_data[][2] = 
{
    {0x3F,0x0001},
    {0x28,0x80A1}, //BT_enable 
    {0x39,0x0004}, //uart switch to wf
    {0x3f,0x0000},
};

u16 wifi_tm_en_data[][2] =
{
    {0x3F,0x0001},
#ifdef RDA5990_USE_DCDC     /*houzhen update Mar 15 2012 */
    {0x23, 0x8FA1},//20111001 higher AVDD voltage to improve EVM to 0x8f21 download current -1db 0x8fA1>>0x8bA1   
#else
	{0x23, 0x0FA1},
#endif
    {0x22,0xD3C7},//for ver.c 20111109, tx
	{0x24, 0x80C8},//freq_osc_in[1:0]00  0x80C8 >> 0x80CB 
    {0x27,0x4925},//for ver.c20111109, txs
    {0x28,0x80A1}, //BT_enable            
    {0x29,0x111F},                        
    {0x31,0x8140},                        
    {0x32,0x0113},//set_ rdenout_ldooff_wf
    {0x39,0x0004},//uart switch to wf
    {0x3f,0x0000},
};

u16 wifi_tm_rf_init_data[][2] = 
{
	{0x3f, 0x0000},
	//set_rf_switch                                                  
	{0x06,0x0101},                                                     
	{0x07,0x0101},                                                     
	{0x08,0x0101},                                                     
	{0x09,0x0101},                                                     
	{0x0A,0x002C},//aain_0   
	{0x0D, 0x0507},                                          
	{0x0E,0x2300},//2012_02_20                                         
	{0x0F,0x5689},//                                                   
	//set_RF                                                            
	{0x10,0x0f78},//20110824                                             
	{0x11,0x0602},                                                     
	{0x13,0x0652},//adc_tuning_bit[011]                               
	{0x14,0x8886},                                                     
	{0x15,0x0990},                                                     
	{0x16,0x049f},                                                     
	{0x17,0x0990},                                                     
	{0x18,0x049F},                                                     
	{0x19,0x3C01},//sdm_vbit[3:0]=1111                                 
	{0x1C,0x0934},                                                     
	{0x1D,0xFF00},//for ver.D20120119for temperature 70 degree         
	{0x1F,0x0300},//div2_band_48g_dr=1;div2_band_48g_reg[8:0]1000000000
	{0x20,0x06E4},                                                     
	{0x21,0x0ACF},//for ver.c20111109,dr dac reset,dr txflt reset      
	{0x22,0x24DC},                                                     
	{0x23,0x23FF},                                                     
	{0x24,0x00FC},                                                     
	{0x26,0x004F},                                                     
	{0x27,0x171D},///mdll*7                                            
	{0x28,0x031D},///mdll*7                                            
	{0x2A,0x2860},                                                     
	{0x2B,0x0800},//bbpll,or ver.c20111116                             
	{0x32,0x8a08},                                                     
	{0x33,0x1D02},//liuyanan                                           
	//agc_gain                                                          
	{0x36,0x02f4}, //00F8;//gain_7                                     
	{0x37,0x01f4}, //0074;//aain_6                                     
	{0x38,0x21d4}, //0014;//gain_5                                     
	{0x39,0x25d4}, //0414;//aain_4                                     
	{0x3A,0x2584}, //1804;//gain_3                                     
	{0x3B,0x2dc4}, //1C04;//aain_2                                     
	{0x3C,0x2d04}, //1C02;//gain_1                                     
	{0x3D,0x2c02}, //3C01;//gain_0                                     
	//DC_CAL                                                            
	{0x30,0x0248},                                                     
	{0x30,0x0249},                                                     
	//wait 200ms;                                                       
	{0x33,0x1502},//liuyanan                                           
	//SET_channel_to_11                                                 
	{0x1B,0x0001},//set_channel     
	{0x3f,0x0000},
};

/*houzhen update Mar 15 2012
  should be called when power up/down bt
  */
static int rda5990_wf_setup_A2_power(int enable)
{
	int ret;
	u16 temp_data=0;
	printk(KERN_INFO "***rda5990_wf_setup_A2_power start! \n");
	ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, 0x3f, 0x0001);
	if(ret)
		goto err;

	if(enable)
	{
		ret=i2c_read_1_addr_2_data(rda_wifi_rf_client,0x22,&temp_data);
		if(ret)
			goto err;
		printk(KERN_INFO "***0xA2 readback value:0x%X \n", temp_data);

		temp_data |=0x0200;   /*en reg4_pa bit*/
#ifdef RDA5890_USE_CRYSTAL	
		temp_data &= ~(1 << 14); //disable xen_out
#endif
		ret=i2c_write_1_addr_2_data(rda_wifi_rf_client,0x22,temp_data);
		if(ret)
			goto err;
		//read wlan version
		ret = i2c_read_1_addr_2_data(rda_wifi_rf_client,0x21,&temp_data);
		if(ret)
			goto err;
		else
			wlan_version = temp_data;
	}
	else
	{
		ret=i2c_read_1_addr_2_data(rda_wifi_rf_client,0x28,&temp_data);
		if(ret)
			goto err;
		if(temp_data&0x8000)        // bt is on 
		{
			goto out;
		}
		else
		{
			ret=i2c_read_1_addr_2_data(rda_wifi_rf_client,0x22,&temp_data);
			if(ret)
				goto err;
			temp_data&=0xfdff;

			ret=i2c_write_1_addr_2_data(rda_wifi_rf_client,0x22,temp_data);
			if(ret)
				goto err;
		}
		wlan_version = 0;
	}

out:
	ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, 0x3f, 0x0000);
	if(ret)
		goto err;
	printk(KERN_INFO "***rda5990_wf_setup_A2_power succeed! \n");
	return 0;

err:
	printk(KERN_INFO "***rda5990_wf_setup_A2_power failed! \n");
	return -1;
}


int rda_wifi_rf_init(void)
{
	unsigned int count = 0;
	int ret = 0;
	
	mutex_lock(&i2c_rw_lock);
	if( (wlan_version&0x1f) == 7)
	{
		for(count = 0; count < sizeof(wifi_rf_init_data)/sizeof(wifi_rf_init_data[0]); count ++)
		{
			ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, wifi_rf_init_data[count][0], wifi_rf_init_data[count][1]);
			if(ret)
				goto err;
		}
	}
	else if((wlan_version&0x1f) == 4 || (wlan_version&0x1f)==5)
	{
		for(count = 0; count < sizeof(wifi_rf_init_data_verE)/sizeof(wifi_rf_init_data_verE[0]); count ++)
		{
			ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, wifi_rf_init_data_verE[count][0], wifi_rf_init_data_verE[count][1]);
			if(ret)
				goto err;
		}
	}
	else
	{
		printk("unknown version of this 5990 chip\n");
		goto err;
	}
	mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***rda_wifi_rf_init_succceed \n");
	msleep(5);   //5ms delay
	return 0;
err:
    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***rda_wifi_rf_init failed! \n");
	return -1;
}

int rda_wifi_dc_cal(void)
{
	unsigned int count = 0;
	int ret = 0;

    mutex_lock(&i2c_rw_lock);
	for(count = 0; count < sizeof(wifi_dc_cal_data)/sizeof(wifi_dc_cal_data[0]); count ++)
	{
		ret = i2c_write_1_addr_2_data(rda_wifi_rf_client ,wifi_dc_cal_data[count][0], wifi_dc_cal_data[count][1]);
		if(ret)
			goto err;
	}

    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***rda_wifi_rf_dc_calsuccceed \n");
	msleep(50);   //50ms delay
	return 0;

err:
    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***rda_wifi_rf_dc_calf_failed! \n");
	return -1;
}

int rda_wifi_en(void)
{
	unsigned int count = 0;
	int ret = 0;

    mutex_lock(&i2c_rw_lock); 
	for(count = 0; count < sizeof(wifi_en_data)/sizeof(wifi_en_data[0]); count ++)
	{
		ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, wifi_en_data[count][0], wifi_en_data[count][1]);
		if(ret)
			goto err;
		if(wifi_en_data[count][0] == 0x31)
			msleep(10);   //10ms delay
	}

	ret=rda5990_wf_setup_A2_power(1);	//en pa_reg for wf
	if(ret)
		goto err;

    mutex_unlock(&i2c_rw_lock);
	msleep(8);   //8ms delay

	printk(KERN_INFO "***rda_wifi_en_succceed \n");
	return 0;
err:
     mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***rda_wifi_power_on failed! \n");
	return -1;
}

int rda_wifi_debug_en(void)
{
    unsigned int count = 0;
    int ret = 0;
    
    mutex_lock(&i2c_rw_lock); 
	for(count = 0; count < sizeof(wifi_uart_debug_data)/sizeof(wifi_uart_debug_data[0]); count ++)
	{
      ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, wifi_uart_debug_data[count][0], wifi_uart_debug_data[count][1]);
      if(ret)
          goto err;
	}

err:
    mutex_unlock(&i2c_rw_lock);
    return ret;
}

int rda_tm_wifi_en(void)
{
	unsigned int count = 0;
	int ret = 0;
    unsigned short temp_data;

	for(count = 0; count < sizeof(wifi_tm_en_data)/sizeof(wifi_tm_en_data[0]); count ++)
	{
		ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, wifi_tm_en_data[count][0], wifi_tm_en_data[count][1]);
		if(ret)
			goto err;
	}

	msleep(8);   //8ms delay
    ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, 0x3f, 0x0001); //PAGE UP
    if(ret)
        goto err;
 
    ret = i2c_read_1_addr_2_data(rda_wifi_rf_client,0x21,&temp_data);
    if(ret)
        goto err;
    else
        wlan_version = temp_data;

    ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, 0x3f, 0x0000); //PAGE DOWN
    if(ret)
        goto err;
 

	printk(KERN_INFO "***rda_wifi_en_succceed \n");
	return 0;
err:
	printk(KERN_INFO "***rda_wifi_power_on failed! \n");
	return -1;
}

int rda_tm_wifi_rf_init(void)
{
	unsigned int count = 0;
	int ret = 0;

	for(count = 0; count < sizeof(wifi_tm_rf_init_data)/sizeof(wifi_tm_rf_init_data[0]); count ++)
	{
		ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, wifi_tm_rf_init_data[count][0], wifi_tm_rf_init_data[count][1]);
		if(ret)
			goto err;
	}

	printk(KERN_INFO "***rda_wifi_rf_init_succceed \n");
	msleep(5);   //5ms delay
	return 0;

err:
	printk(KERN_INFO "***rda_wifi_rf_init failed! \n");
	return -1;
}
/*houzhen add 2012 04 09
  add to ensure wf dig powerup
  */

int rda_wifi_dig_reset(void)
{
	unsigned int count = 0;
	int ret = 0;
	msleep(8);   //8ms delay
    mutex_lock(&i2c_rw_lock);

	for(count = 0; count < sizeof(wifi_dig_reset_data)/sizeof(wifi_dig_reset_data[0]); count ++)
	{
		ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, wifi_dig_reset_data[count][0], wifi_dig_reset_data[count][1]);
		if(ret)
			goto err;
	}

    mutex_unlock(&i2c_rw_lock);
	msleep(8);   //8ms delay
	printk(KERN_INFO "***rda_wifi_dig_reset \n");
	return 0;
err:
    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***rda_wifi_dig_reset failed! \n"); 
	return -1;
}

int rda_wlan_version(void)
{
	printk("******version %x \n", wlan_version);
	return wlan_version;
}

int rda_wifi_power_off(void)
{
	unsigned int count = 0;
	int ret = 0;
	u16 temp=0x0000;
	printk(KERN_INFO "rda_wifi_power_off \n");

	if(!rda_wifi_rf_client)
	{
		printk(KERN_INFO "rda_wifi_power_off failed on:i2c client \n");
		return -1;
	}

    mutex_lock(&i2c_rw_lock);
	ret=rda5990_wf_setup_A2_power(0);   //disable pa_reg for wf
	if(ret)
		goto err;

	ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, 0x3f, 0x0001);   //page up
	if(ret)
		goto err;

	ret=i2c_read_1_addr_2_data(rda_wifi_rf_client,0x28,&temp);   //poll bt status
	if(ret)
		goto err;

	if(temp&0x8000)
	{
		ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, 0x3f, 0x0000);   //page down
		if(ret)
			goto err;

		ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, 0x0f, 0x2223);   // set antenna for bt
		if(ret)
			goto err;

	}

	for(count = 0; count < sizeof(wifi_off_data)/sizeof(wifi_off_data[0]); count ++)
	{
		ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, wifi_off_data[count][0], wifi_off_data[count][1]);
		if(ret)
			goto err;
	}
	printk(KERN_INFO "***rda_wifi_power_off success!!! \n");



    mutex_unlock(&i2c_rw_lock);
	return 0;

err:
    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***rda_wifi_power_off failed! \n");
	return -1;

}

int rda_wifi_power_on(void)
{
	int ret;
	char retry = 3;

    printk("------------------rda_wifi_power_on\n");
	if(!rda_wifi_rf_client)
	{
		printk(KERN_INFO "rda_wifi_power_on failed on:i2c client \n");
		return -1;
	}

_retry: 


	if(!wifi_in_test_mode)
	{
		ret = rda_wifi_en();	
		if(ret < 0)
			goto err;

		ret = rda_wifi_rf_init();	
		if(ret < 0)
			goto err;

		ret = rda_wifi_dc_cal();	
		if(ret < 0)
			goto err;

		msleep(20);   //20ms delay
		ret=rda_wifi_dig_reset();   //houzhen add to ensure wf power up safely

		if(ret < 0)
			goto err;
		msleep(20);   //20ms delay
	}
	else
	{
		ret = rda_tm_wifi_en();
		if(ret < 0)
			goto err;

		ret = rda_tm_wifi_rf_init();
		if(ret < 0)
			goto err;
	}
	printk(KERN_INFO "rda_wifi_power_on_succeed!! \n");

	return 0;

err:
	printk(KERN_INFO "rda_wifi_power_on_failed retry:%d \n", retry);
	if(retry -- > 0)
	{   
		rda_wifi_power_off();
		goto _retry;
	}

	return -1;
}

extern int rda_wifi_power_on(void);

int rda_fm_power_on(void)
{
	int ret = 0;
	u16 temp = 0;

	if(!rda_wifi_rf_client)
	{
		printk(KERN_INFO "rda_wifi_rf_client is NULL, rda_fm_power_on failed!\n");
		return -1;
	}

	ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, 0x3f, 0x0001);   // page down
	if(ret < 0){
		printk(KERN_INFO "%s() write address(0x%02x) with value(0x%04x) failed! \n", __func__, 0x3f, 0x0001);
		return -1;
	}
	ret = i2c_read_1_addr_2_data(rda_wifi_rf_client, 0x22, &temp);		//read 0xA2
	if(ret < 0){
		printk(KERN_INFO "%s() read from address(0x%02x) failed! \n", __func__, 0x22);
		return -1;
	}
	temp = temp & (~(1 << 15));		//clear bit[15]
	ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, 0x22, temp);		//write back
	if(ret < 0){
		printk(KERN_INFO "%s() write address(0x%02x) with value(0x%04x) failed! \n", __func__, 0x3f, 0x0001);
		return -1;
	}
	ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, 0x3f, 0x0000);   // page up
	if(ret < 0){
		printk(KERN_INFO "%s() write address(0x%02x) with value(0x%04x) failed! \n", __func__, 0x3f, 0x0001);
		return -1;
	}

	return 0;
}

int rda_fm_power_off(void)
{
	int ret = 0;
	u16 temp = 0;

	if(!rda_wifi_rf_client)
	{
		printk(KERN_INFO "rda_wifi_rf_client is NULL, rda_fm_power_off failed!\n");
		return -1;
	}

	ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, 0x3f, 0x0001);   // page down
	if(ret < 0){
		printk(KERN_INFO "%s() write address(0x%02x) with value(0x%04x) failed! \n", __func__, 0x3f, 0x0001);
		return -1;
	}
	ret = i2c_read_1_addr_2_data(rda_wifi_rf_client, 0x22, &temp);		//read 0xA2
	if(ret < 0){
		printk(KERN_INFO "%s() read from address(0x%02x) failed! \n", __func__, 0x22);
		return -1;
	}
	temp = temp | (1 << 15);		//set bit[15]
	ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, 0x22, temp);		//write back
	if(ret < 0){
		printk(KERN_INFO "%s() write address(0x%02x) with value(0x%04x) failed! \n", __func__, 0x3f, 0x0001);
		return -1;
	}
	ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, 0x3f, 0x0000);   // page up
	if(ret < 0){
		printk(KERN_INFO "%s() write address(0x%02x) with value(0x%04x) failed! \n", __func__, 0x3f, 0x0001);
		return -1;
	}

	return 0;
}

static int rda_bt_rf_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int result = 0;

	rda_bt_rf_client = client;
	return result;
}

static int rda_bt_rf_remove(struct i2c_client *client)
{
	rda_bt_rf_client = NULL;
	return 0;
}

static int rda_bt_rf_detect(struct i2c_client *client, int kind, struct i2c_board_info *info)
{  
	strcpy(info->type, RDA_BT_RF_I2C_DEVNAME);   
	return 0;   
}

static struct i2c_driver rda_bt_rf_driver = {
	.probe = rda_bt_rf_probe,
	.remove = rda_bt_rf_remove,
	.detect = rda_bt_rf_detect,
	.driver.name = RDA_BT_RF_I2C_DEVNAME,
	.id_table = bt_rf_i2c_id,
	//.address_data = &rda_bt_rf_addr_data,
	//.address_list = (const unsigned short*)bt_rf_force,
};


static int rda_bt_core_detect(struct i2c_client *client, int kind, struct i2c_board_info *info)
{
	strcpy(info->type, RDA_BT_CORE_I2C_DEVNAME);
	return 0;   
}

static int rda_bt_core_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int result = 0;

	rda_bt_core_client = client;
	return result;
}

static int rda_bt_core_remove(struct i2c_client *client)
{
	return 0;
}

static struct i2c_driver rda_bt_core_driver = {
	.probe = rda_bt_core_probe,
	.remove = rda_bt_core_remove,
	.detect = rda_bt_core_detect,
	.driver.name = RDA_BT_CORE_I2C_DEVNAME,
	.id_table = bt_core_i2c_id,
	//.address_data = &rda_bt_core_addr_data,
	//.address_list = (const unsigned short*)bt_core_force,
};

u16 rda_5990_bt_off_data[][2] = 
{
	{0x3f, 0x0001 }, //pageup
	{0x28, 0x00A1 }, //power off bt
	{0x3f, 0x0000 }, //pagedown
};

/*houzhen update 2012 03 06*/
u16 rda_5990_bt_en_data[][2] = 
{
    {0x3f, 0x0001 },      	//pageup
#ifdef RDA5990_USE_DCDC    
    {0x23, 0x8FA1},		  // //20111001 higher AVDD voltage to improve EVM
#else
	{0x23, 0x0FA1},
#endif 
	{0x24, 0x80C8},		  // ;//freq_osc_in[1:0]00	
	{0x26, 0x47A5},		  //  reg_vbit_normal_bt[2:0] =111
	{0x27, 0x4925},		  // //for ver.c20111109, txswitch
	{0x29, 0x111F},		  // // rden4in_ldoon_bt=1	
	{0x32, 0x0111},		  // set_ rdenout_ldooff_wf=0;						 
	{0x39, 0x0000},		  //	  //uart switch to bt

	{0x28, 0x80A1},      	// bt en
	{0x3f, 0x0000},      	//pagedown
};


u16 rda_5990_bt_dc_cal[][2] = 
{
	{0x3f, 0x0000 }, 
	{0x30, 0x0129 },
	{0x30, 0x012B },
	{0x3f, 0x0000 }, 
};


u16 rda_5990_bt_set_rf_switch_data[][2] = 
{
	{0x3f, 0x0000 }, 
	{0x0F, 0x2223 },
	{0x3f, 0x0000 }, 
};


u16 RDA5990_bt_enable_clk_data[][2] = 
{
	{0x3f, 0x0000 }, 
	{0x30, 0x0040 },
	{0x2a, 0x285d },
	{0x3f, 0x0000 }, 
};

u16 RDA5990_bt_dig_reset_data[][2] = 
{
	{0x3f, 0x0001 }, //pageup
	{0x28, 0x86A1 },
	{0x28, 0x87A1 },
	{0x28, 0x85A1 },
	{0x3f, 0x0000 }, //pagedown
};

/*houzhen update 2012 03 06*/
u16 rda_5990_bt_rf_data[][2] = 
{
	{0x3f, 0x0000}, //pagedown
	{0x01, 0x1FFF},
	{0x06, 0x07F7},
	{0x08, 0x29E7},
	{0x09, 0x0520},
	{0x0B, 0x03DF},
	{0x0C, 0x85E8},
	{0x0F, 0x0DBC},
	{0x12, 0x07F7},
	{0x13, 0x0327},
	{0x14, 0x0CCC},
	{0x15, 0x0526},
	{0x16, 0x8918},
	{0x18, 0x8800},
	{0x19, 0x10C8},
	{0x1A, 0x9078},
	{0x1B, 0x80E2},
	{0x1C, 0x361F},
	{0x1D, 0x4363},
	{0x1E, 0x303F},
	{0x23, 0x2222},
	{0x24, 0x359D},
	{0x27, 0x0011},
	{0x28, 0x124F},
	{0x39, 0xA5FC},
	{0x3f, 0x0001}, //page 1
	{0x00, 0x043F},
	{0x01, 0x467F},
	{0x02, 0x28FF},
	{0x03, 0x67FF},
	{0x04, 0x57FF},
	{0x05, 0x7BFF},
	{0x06, 0x3FFF},
	{0x07, 0x7FFF},
	{0x18, 0xF3F5},
	{0x19, 0xF3F5},
	{0x1A, 0xE7F3},
	{0x1B, 0xF1FF},
	{0x1C, 0xFFFF},
	{0x1D, 0xFFFF},
	{0x1E, 0xFFFF},
	{0x1F, 0xFFFF},
	//	{0x22, 0xD3C7},	
	//	{0x23, 0x8fa1},
	//	{0x24, 0x80c8},
	//	{0x26, 0x47A5},
	//	{0x27, 0x4925},
	//	{0x28, 0x85a1},
	//	{0x29, 0x111f},
	//	{0x32, 0x0111},
	//	{0x39, 0x0000},
	{0x3f, 0x0000}, //pagedown
};

/*houzhen update Mar 15 2012
  should be called when power up/down bt
  */
static int rda5990_bt_setup_A2_power(int enable)
{
	int ret;
	u16 temp_data=0;

	ret = i2c_write_1_addr_2_data(rda_bt_rf_client, 0x3f, 0x0001);
	if(ret)
		goto err;

	if(enable)
	{
		ret=i2c_read_1_addr_2_data(rda_bt_rf_client,0x22,&temp_data);
		if(ret)
			goto err;
		printk(KERN_INFO "***0xA2 readback value:0x%X \n", temp_data);

		temp_data |=0x0200;   /*en reg4_pa bit*/

		ret=i2c_write_1_addr_2_data(rda_bt_rf_client,0x22,temp_data);
		if(ret)
			goto err;   		
	}
	else
	{
		ret=i2c_read_1_addr_2_data(rda_bt_rf_client,0x31,&temp_data);
		if(ret)
			goto err;

		if(temp_data&0x8000)        // wf is on 
		{
			goto out;
		}
		else
		{
			ret=i2c_read_1_addr_2_data(rda_bt_rf_client,0x22,&temp_data);
			if(ret)
				goto err;
			temp_data&=0xfdff;

			ret=i2c_write_1_addr_2_data(rda_bt_rf_client,0x22,temp_data);
			if(ret)
				goto err;
		}

	}

out:
	ret = i2c_write_1_addr_2_data(rda_bt_rf_client, 0x3f, 0x0000);
	if(ret)
		goto err;
	return 0;

err:
	printk(KERN_INFO "***rda5990_bt_setup_A2_power failed! \n");
	return -1;
}

int rda_bt_power_off(void);

int rda_bt_power_on(void)
{
	unsigned int count = 0;
	int ret = 0;

	printk(KERN_INFO "rda_bt_power_on \n");

	if(!rda_bt_rf_client || !rda_bt_rf_client)
	{
		printk(KERN_INFO "rda_bt_power_on failed on:i2c client \n");
		return -1;
	}

    mutex_lock(&i2c_rw_lock);

	for(count = 0; count < sizeof(rda_5990_bt_en_data)/sizeof(rda_5990_bt_en_data[0]); count ++)
	{
		ret = i2c_write_1_addr_2_data(rda_bt_rf_client, rda_5990_bt_en_data[count][0], rda_5990_bt_en_data[count][1]);
		if(ret)
			goto err;
	}

	ret=rda5990_bt_setup_A2_power(1);	
	if(ret)
	{   
		printk(KERN_INFO "***rda5990_bt_setup_A2_power fail!!! \n");
		goto err;
	}

	printk(KERN_INFO "***rda_bt_power_on success!!! \n");

    mutex_unlock(&i2c_rw_lock);
	/*houzhen update 2012 03 06*/
	msleep(10);     //delay 10 ms after power on

	return 0;

err:
    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***rda_bt_power_on failed! \n");
	return -1;

}

int rda_bt_power_off(void)
{
	unsigned int count = 0;
	int ret = 0;
	printk(KERN_INFO "rda_bt_power_off \n");

	if(!rda_bt_rf_client)
	{
		printk(KERN_INFO "rda_bt_power_off failed on:i2c client \n");
		return -1;
	}

    mutex_lock(&i2c_rw_lock);
	for(count = 0; count < sizeof(rda_5990_bt_off_data)/sizeof(rda_5990_bt_off_data[0]); count ++)
	{
		ret = i2c_write_1_addr_2_data(rda_bt_rf_client ,rda_5990_bt_off_data[count][0], rda_5990_bt_off_data[count][1]);
		if(ret)
			goto err;
	}
	msleep(10);   //10ms
	printk(KERN_INFO "***rda_bt_power_off success!!! \n");

	ret=rda5990_bt_setup_A2_power(0);//disable ldo_pa reg 
	if(ret)
		goto err;



    mutex_unlock(&i2c_rw_lock);    
	return 0;

err:
    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***rda_bt_power_off failed! \n");
	return -1;

}


int RDA5990_bt_rf_init(void)
{
	unsigned int count = 0;
	int ret = 0;
	printk(KERN_INFO "RDA5990_bt_rf_init \n");

	if(!rda_bt_rf_client)
	{
		printk(KERN_INFO "RDA5990_bt_rf_init on:i2c client \n");
		return -1;
	}

    mutex_lock(&i2c_rw_lock);
	for(count = 0; count < sizeof(rda_5990_bt_rf_data)/sizeof(rda_5990_bt_rf_data[0]); count ++)
	{
		ret = i2c_write_1_addr_2_data(rda_bt_rf_client, rda_5990_bt_rf_data[count][0], rda_5990_bt_rf_data[count][1]);
		if(ret)
			goto err;
	}

    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***RDA5990_bt_rf_init success!!! \n");
	msleep(5);   //5ms
	return 0;

err:
    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***RDA5990_bt_rf_init failed! \n");
	return -1;

}

/*houzhen add 2012 04 09
  add to ensure bt dig powerup
  */

int RDA5990_bt_dig_reset(void)
{
	unsigned int count = 0;
	int ret = 0;

	printk(KERN_INFO "RDA5990_bt_dig_reset \n");
	if(!rda_bt_rf_client)
	{
		printk(KERN_INFO "RDA5990_bt_dig_reset on:i2c client \n");
		return -1;
	}

    mutex_lock(&i2c_rw_lock);
	for(count = 0; count < sizeof(RDA5990_bt_dig_reset_data)/sizeof(RDA5990_bt_dig_reset_data[0]); count ++)
	{
		ret = i2c_write_1_addr_2_data(rda_bt_rf_client, RDA5990_bt_dig_reset_data[count][0], RDA5990_bt_dig_reset_data[count][1]);
		if(ret)
			goto err;
	}

    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***RDA5990_bt_rf_init success!!! \n");
	msleep(5);   //5ms
	return 0;

err:
    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***RDA5990_bt_rf_init failed! \n");
	return -1;

}


int RDA5990_bt_dc_cal(void)
{
	unsigned int count = 0;
	int ret = 0;
	printk(KERN_INFO "rda_bt_dc_cal \n");

	if(!rda_bt_rf_client)
	{
		printk(KERN_INFO "rda_bt_rf_client \n");
		return -1;
	}
    mutex_lock(&i2c_rw_lock);

	for(count = 0; count < sizeof(rda_5990_bt_dc_cal)/sizeof(rda_5990_bt_dc_cal[0]); count ++)
	{
		ret = i2c_write_1_addr_2_data(rda_bt_rf_client, rda_5990_bt_dc_cal[count][0], rda_5990_bt_dc_cal[count][1]);
		if(ret)
			goto err;
	}

    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***rda_bt_dc_cal success!!! \n");
	msleep(200);   //200ms

	return 0;

err:
    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***rda_bt_dc_cal  failed! \n");
	return -1;

}



/*houzhen update Mar 15 2012 
  bypass RDA5990_bt_set_rf_switch when wf is already on
  */

int RDA5990_bt_set_rf_switch(void)
{
	unsigned int count = 0;
	int ret = 0;
	u16 temp_data=0;
	printk(KERN_INFO "RDA5990_bt_set_rf_switch \n");

	if(!rda_bt_rf_client || !rda_wifi_rf_client)
	{
		printk(KERN_INFO "RDA5990_bt_set_rf_switch failed on:i2c client \n");
		return -1;
	}

    mutex_lock(&i2c_rw_lock);

	ret = i2c_write_1_addr_2_data(rda_bt_rf_client, 0x3f, 0x0001);
	if(ret)
		goto err;	

	ret=i2c_read_1_addr_2_data(rda_bt_rf_client,0x31,&temp_data);  

	if(ret)
		goto err;	

	if(temp_data&0x8000)   // if wf is already on
	{
		printk(KERN_INFO "wf already en, bypass RDA5990_bt_set_rf_switch function \n");
		ret = i2c_write_1_addr_2_data(rda_bt_rf_client, 0x3f, 0x0000);
		if(ret)
			goto err;	
        mutex_unlock(&i2c_rw_lock);	
		return 0;
	}

	for(count = 0; count < sizeof(rda_5990_bt_set_rf_switch_data)/sizeof(rda_5990_bt_set_rf_switch_data[0]); count ++)
	{
		ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, rda_5990_bt_set_rf_switch_data[count][0], rda_5990_bt_set_rf_switch_data[count][1]);
		if(ret)
			goto err;	    
	}

    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***RDA5990_bt_set_rf_switch success!!! \n");
	msleep(50);   //50ms
	return 0;

err:
    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***RDA5990_bt_set_rf_switch  failed! \n");
	return -1;

}


/*houzhen update Mar 15 2012 
  bypass RDA5990_bt_enable_clk when wf is already on
  */

int RDA5990_bt_enable_clk(void)
{
	unsigned int count = 0;
	int ret = 0;
	u16 temp_data=0;
	printk(KERN_INFO "RDA5990_bt_enable_clk \n");

	if(!rda_bt_rf_client)
	{
		printk(KERN_INFO "RDA5990_bt_enable_clk failed on:i2c client \n");
		return -1;
	}

    mutex_lock(&i2c_rw_lock);
	ret = i2c_write_1_addr_2_data(rda_bt_rf_client, 0x3f, 0x0001);
	if(ret)
		goto err;	

	ret=i2c_read_1_addr_2_data(rda_bt_rf_client,0x31,&temp_data);  

	if(ret)
		goto err;	

	if(temp_data&0x8000)   // if wf is already on
	{
		printk(KERN_INFO "wf already en, bypass RDA5990_bt_enable_clk function \n");
		ret = i2c_write_1_addr_2_data(rda_bt_rf_client, 0x3f, 0x0000);
		if(ret)
			goto err;	
        mutex_unlock(&i2c_rw_lock);
		return 0;
	}


	for(count = 0; count < sizeof(RDA5990_bt_enable_clk_data)/sizeof(RDA5990_bt_enable_clk_data[0]); count ++)
	{
		ret = i2c_write_1_addr_2_data(rda_wifi_rf_client, RDA5990_bt_enable_clk_data[count][0], RDA5990_bt_enable_clk_data[count][1]);
		if(ret)
			goto err;	
	}

    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***RDA5990_bt_enable_clk success!!! \n");
	msleep(50);   //50ms
	return 0;

err:
    mutex_unlock(&i2c_rw_lock);
	printk(KERN_INFO "***RDA5990_bt_enable_clk  failed! \n");
	return -1;

}

//extern void mt_combo_bgf_enable_irq(void);
//extern void mt_combo_bgf_disable_irq(void);

#define RDA_BT_IOCTL_MAGIC 'u'
#define RDA_BT_POWER_ON_IOCTL _IO(RDA_BT_IOCTL_MAGIC ,0x01)
#define RD_BT_RF_INIT_IOCTL   _IO(RDA_BT_IOCTL_MAGIC ,0x02)
#define RD_BT_DC_CAL_IOCTL    _IO(RDA_BT_IOCTL_MAGIC ,0x03)
#define RD_BT_SET_RF_SWITCH_IOCTL _IO(RDA_BT_IOCTL_MAGIC ,0x04)
#define RDA_BT_POWER_OFF_IOCTL _IO(RDA_BT_IOCTL_MAGIC ,0x05)
#define RDA_BT_EN_CLK _IO(RDA_BT_IOCTL_MAGIC ,0x06)
#define RD_BT_DC_DIG_RESET_IOCTL    _IO(RDA_BT_IOCTL_MAGIC ,0x07)

#define RDA_WIFI_POWER_ON_IOCTL    _IO(RDA_BT_IOCTL_MAGIC ,0x10)
#define RDA_WIFI_POWER_OFF_IOCTL    _IO(RDA_BT_IOCTL_MAGIC ,0x11)
#define RDA_WIFI_POWER_SET_TEST_MODE_IOCTL    _IO(RDA_BT_IOCTL_MAGIC ,0x12)
#define RDA_WIFI_POWER_CANCEL_TEST_MODE_IOCTL    _IO(RDA_BT_IOCTL_MAGIC ,0x13)
#define RDA_WIFI_DEBUG_MODE_IOCTL    _IO(RDA_BT_IOCTL_MAGIC ,0x14)

extern int rk29sdk_wifi_set_carddetect(int val);
static int rda_5990_pw_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;

	switch(cmd)
	{
		case RDA_WIFI_POWER_ON_IOCTL:
			rda_wifi_power_on();
			rk29sdk_wifi_set_carddetect(1);
			break;

		case RDA_WIFI_POWER_OFF_IOCTL:
		    rk29sdk_wifi_set_carddetect(0);
			rda_wifi_power_off();
			break;

		case RDA_WIFI_POWER_SET_TEST_MODE_IOCTL:
			wifi_in_test_mode = 1;
			printk("****set rda wifi in test mode");
			break;

		case RDA_WIFI_POWER_CANCEL_TEST_MODE_IOCTL:
			wifi_in_test_mode = 0;
			printk("****set rda wifi in normal mode");
			break;

        case RDA_WIFI_DEBUG_MODE_IOCTL:
            ret = rda_wifi_debug_en();
            break;
            
		case RDA_BT_POWER_ON_IOCTL:
			{
				ret = rda_bt_power_on();
				//mt_combo_bgf_enable_irq();
			}
			break;

			/* should call thif function after bt_power_on*/
		case RDA_BT_EN_CLK:
			ret = RDA5990_bt_enable_clk();
			break;

		case RD_BT_RF_INIT_IOCTL:
			ret = RDA5990_bt_rf_init();
			break;

		case RD_BT_DC_CAL_IOCTL:	
			ret = RDA5990_bt_dc_cal();
			break;

		case RD_BT_DC_DIG_RESET_IOCTL:	
			ret = RDA5990_bt_dig_reset();
			break;

		case RD_BT_SET_RF_SWITCH_IOCTL:
			ret = RDA5990_bt_set_rf_switch();			
			break;

		case RDA_BT_POWER_OFF_IOCTL:
			{
				//mt_combo_bgf_disable_irq();
				ret = rda_bt_power_off();               
			}
			break;

		default:
			break;
	}

	printk(KERN_INFO "rda_bt_pw_ioctl cmd=0x%02x \n", cmd);

	return ret;
}	

void mmc_rescan_slot(int id)
{
    rda_wifi_power_on();
    rk29sdk_wifi_set_carddetect(1);
}
EXPORT_SYMBOL(mmc_rescan_slot);

void mmc_remove(int id)
{
    rk29sdk_wifi_set_carddetect(0);
			rda_wifi_power_off();
}
EXPORT_SYMBOL(mmc_remove);

static int rda_5990_major;
static struct class *rda_5990_class = NULL;
static const struct file_operations rda_5990_operations = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = rda_5990_pw_ioctl,
	.release = NULL
};

void rda_5990_sleep_worker_task(struct work_struct *work)
{
	printk("---rda_5990_sleep_worker_task end");
	wake_unlock(&rda_5990_wake_lock);
}

void rda_5990_set_wake_lock(void)
{
	wake_lock(&rda_5990_wake_lock);
	cancel_delayed_work(&rda_5990_sleep_worker);
	schedule_delayed_work(&rda_5990_sleep_worker, 6*HZ);
}

int rda_5990_power_ctrl_init(void)
{
	int ret = 0;
	printk("rda_5990_power_ctrl_init begin\n");

	/*i2c_register_board_info(0, &i2c_rda_wifi_core, 1);
	if (i2c_add_driver(&rda_wifi_core_driver))
	{
		printk("rda_wifi_core_driver failed!\n");
		ret = -ENODEV;
		return ret;
	}
	*/

	//i2c_register_board_info(0, &i2c_rda_wifi_rf, 1);
	if (i2c_add_driver(&rda_wifi_rf_driver))
	{
		printk("rda_wifi_rf_driver failed!\n");
		ret = -ENODEV;
		return ret;
	}
	
	/*
	i2c_register_board_info(0, &i2c_rda_bt_core, 1);
	if (i2c_add_driver(&rda_bt_core_driver))
	{
		printk("rda_bt_core_driver failed!\n");
		ret = -ENODEV;
		return ret;
	}
	*/

	//i2c_register_board_info(0, &i2c_rda_bt_rf, 1);
	if (i2c_add_driver(&rda_bt_rf_driver))
	{
		printk("rda_bt_rf_driver failed!\n");
		ret = -ENODEV;
		return ret;
	}

	rda_5990_major = register_chrdev(0, "rda5990_power_ctrl", &rda_5990_operations);
	if(rda_5990_major < 0)
	{
		printk(KERN_INFO "register rdabt_power_ctrl failed!!! \n");
		return rda_5990_major;
	}

	rda_5990_class = class_create(THIS_MODULE, "rda_combo");
	if(IS_ERR(rda_5990_class))
	{
		unregister_chrdev(rda_5990_major, "rdabt_power_ctrl");
		return PTR_ERR(rda_5990_class);
	}

	device_create(rda_5990_class, NULL, MKDEV(rda_5990_major, 0), NULL, "rdacombo");

	{
		unsigned char*  temp = NULL;
		unsigned short testData = 0xffee;
		temp = (unsigned char *)&testData;
		if(*temp == 0xee)
			isBigEnded = 0;
		else
			isBigEnded = 1;
	}

	INIT_DELAYED_WORK(&rda_5990_sleep_worker, rda_5990_sleep_worker_task);
	wake_lock_init(&rda_5990_wake_lock, WAKE_LOCK_SUSPEND, "RDA_sleep_worker_wake_lock");

    mutex_init(&i2c_rw_lock);    
	printk("rda_5990_power_ctrl_init end\n");
	return 0;
}

void rda_5990_power_ctrl_exit(void)
{
	i2c_del_driver(&rda_wifi_core_driver);
	i2c_del_driver(&rda_wifi_rf_driver);
	i2c_del_driver(&rda_bt_core_driver);
	i2c_del_driver(&rda_bt_rf_driver);

	unregister_chrdev(rda_5990_major, "rdabt_power_ctrl");
	if(rda_5990_class)
		class_destroy(rda_5990_class);       

	cancel_delayed_work_sync(&rda_5990_sleep_worker);
	wake_lock_destroy(&rda_5990_wake_lock);
}

unsigned char rda_5990_wifi_in_test_mode(void)
{
	return wifi_in_test_mode;
}


static __inline__ void cfmakeraw(struct termios *s)
{
	s->c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
	s->c_oflag &= ~OPOST;
	s->c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
	s->c_cflag &= ~(CSIZE|PARENB);
	s->c_cflag |= CS8;
}

static __inline__ int cfsetospeed(struct termios *s, speed_t  speed)
{
	s->c_cflag = (s->c_cflag & ~CBAUD) | (speed & CBAUD);
	return 0;
}

static __inline__ int cfsetispeed(struct termios *s, speed_t  speed)
{
	s->c_cflag = (s->c_cflag & ~CBAUD) | (speed & CBAUD);
	return 0;
}

static int rda_wifi_init_uart(char *dev)
{
	int errno;
	struct termios ti;
	struct serial_struct ss;
	int fd;

	fd = sys_open(dev, O_RDWR | O_NOCTTY, 0);
	if (fd < 0) {
		printk("Can't open serial port");
		return -1;
	}

	sys_ioctl(fd, TCFLSH, TCIOFLUSH);

	/* Clear the cust flag */
	if((errno = sys_ioctl(fd, TIOCGSERIAL, &ss))<0){
		printk("BAUD: error to get the serial_struct info:%s\n", errno);
		goto err;
	}

	if (ss.flags & ASYNC_SPD_CUST) {
		printk("clear ASYNC_SPD_CUST\r\n");
		ss.flags &= ~ASYNC_SPD_CUST;
	}
	if((errno = sys_ioctl(fd, TIOCSSERIAL, &ss))<0){
		printk("BAUD: error to set serial_struct:%s\n", errno);
		goto err;
	}

	if ((errno = sys_ioctl(fd, TCGETS, (long)&ti))  < 0) {
		printk("unable to get UART port setting");
		printk("Can't get port settings");
		goto err;
	}

	cfmakeraw(&ti);

	ti.c_cflag |= CLOCAL;
	ti.c_cflag &= ~CRTSCTS;
	ti.c_lflag = 0;
	ti.c_cc[VTIME]    = 5; /* 0.5 sec */
	ti.c_cc[VMIN]     = 0;

	/* Set initial baudrate */
	cfsetospeed(&ti, B115200);
	cfsetispeed(&ti, B115200);

	if ((errno = sys_ioctl(fd, TCSETS, (long)&ti)) < 0) {
		printk("unable to set UART port setting");
		printk("Can't set port settings");
		goto err;
	}

	errno = sys_ioctl(fd, TCFLSH, TCIOFLUSH);
	if(errno < 0)
		goto err;

	return fd;

err:
	if(fd > 0)
		sys_close(fd);

	return -1;
}  

EXPORT_SYMBOL(rda_wlan_version);

EXPORT_SYMBOL(rda_wifi_init_uart);
EXPORT_SYMBOL(rda_5990_wifi_in_test_mode);

EXPORT_SYMBOL(rda_5990_set_wake_lock);
EXPORT_SYMBOL(rda_wifi_power_off);
EXPORT_SYMBOL(rda_wifi_power_on);
EXPORT_SYMBOL(rda_fm_power_on);
EXPORT_SYMBOL(rda_fm_power_off);

module_init(rda_5990_power_ctrl_init);
module_exit(rda_5990_power_ctrl_exit);

MODULE_LICENSE("GPL");

