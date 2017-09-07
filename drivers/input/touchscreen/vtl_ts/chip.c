
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/delay.h>


#include "vtl_ts.h"

#define		FLASH_I2C_ADDR	0X7F
#define		CHIP_ID_ADDR	0xf000	
#define 		RW_FLAG		0xff

#define		CHIP_WRITE_FLASH_CMD	0x55
#define		CHIP_FLASH_SOURCE_SIZE	8

#define 	TB1_USE_F402            0

struct chip_cmd {
	unsigned short	addr;
	unsigned char	data;
};


static struct ts_info * ts_object = NULL;
static struct chip_cmd (*chip) = NULL;



enum cmd_index {

	FW_VERSION = 0X00,
	FW_CHECKSUM_CMD,
	FW_CHECKSUM_VAL,
	CHIP_SLEEP,
	CHIP_ID_CMD,

	/***********flash***********/
	FLASH_SECTOR_ERASE_CMD,
	FLASH_SECTOR_NUM,
	FLASH_SECTOR_SIZE,
	FLASH_MASS_ERASE_CMD
};

static struct chip_cmd ct360_cmd[] = {
	{0x0f2a,RW_FLAG},	//fw version

	{0x0fff,0xe1}, 		//fw checksum cmd
	{0x0a0d,RW_FLAG}, 	//fw checksum val

	{0x0f2b,0x00}, 		//chip sleep cmd

	{0xf000,RW_FLAG},	//chip id cmd

	/************flash*************/
	{0x33,RW_FLAG},		//FLASH_SECTOR_ERASE_CMD
	{8,RW_FLAG},		//FLASH_SECTOR_NUM
	{2048,RW_FLAG},		//FLASH_SECTOR_SIZE
	{RW_FLAG,RW_FLAG},	//FLASH_MASS_ERASE_CMD
};

static struct chip_cmd ct36x_cmd[] = {
	{0x3fff,RW_FLAG}, 	//fw version

	{0x8fff,0xe1},		//fw checksum cmd
	{0x8e0e,RW_FLAG},	//fw checksum val

	{0x8fff,0xaf},		//chip sleep cmd

	{0xf000,RW_FLAG},	//chip id cmd

	/************flash*************/
	{0x30,RW_FLAG},		//FLASH_SECTOR_ERASE_CMD
	{256,RW_FLAG},		//FLASH_SECTOR_NUM
	{128,RW_FLAG},		//FLASH_SECTOR_SIZE
	{0x33,RW_FLAG},		//FLASH_MASS_ERASE_CMD
};

#if 0
unsigned int ct36x_cmd[4][2] = {
	{0x3fff,0x00}, //fw version

	{0x0fff,0xe1}, //fw checksum cmd
	{0x0a0d,0x00}, //fw checksum val

	{0x8fff,0xaf},//chip sleep cmd
};

unsigned int (*chip)[2] = ct36x_cmd;
#endif


static int chip_i2c_read(struct i2c_client *client, __u16 addr, __u8 *buf, __u16 len)
{
	struct i2c_msg msgs;
	int ret;

	DEBUG();
	msgs.addr = addr;
	msgs.flags = 0x01;  // 0x00: write 0x01:read 
	msgs.len = len;
	msgs.buf = buf;
	//#if(PLATFORM == ROCKCHIP)
	//msgs.scl_rate = TS_I2C_SPEED;
	//#endif

	ret = i2c_transfer(client->adapter, &msgs, 1);
	if(ret != 1){
		printk("___%s:i2c read error___\n",__func__);
		return -1;
	}
	return 0;
}

static int chip_i2c_write(struct i2c_client *client, __u16 addr, __u8 *buf, __u16 len)
{
	struct i2c_msg msgs;
	int ret;

	DEBUG();
	msgs.addr = addr;
	msgs.flags = 0x00;  // 0x00: write 0x01:read 
	msgs.len = len;
	msgs.buf = buf;
	//#if(PLATFORM == ROCKCHIP)
	//msgs.scl_rate = TS_I2C_SPEED;
	//#endif

	ret = i2c_transfer(client->adapter, &msgs, 1);
	if(ret != 1){
		printk("___%s:i2c write error___\n",__func__);
		return -1;
	}
	return 0;
}


static int chip_ram_write_1byte(unsigned short addr,unsigned char data)
{
	struct i2c_client *client = ts_object->driver->client;
	unsigned char buf[3];
	int ret = 0;
	
	DEBUG();
	buf[0] = 0xff;
	buf[1] = addr >> 8;
	buf[2] = addr & 0x00ff;
	//printk("addr = %x,buf[0] = %x,buf[1] = %x,buf[2] = %x,data = %x\n",addr,buf[0],buf[1],buf[2],data);
	ret = chip_i2c_write(client, client->addr, buf,3);
	if(ret)
	{
		return ret;
	}
	udelay(10);
	buf[0] = 0x00;
	buf[1] = data;
	ret = chip_i2c_write(client, client->addr, buf,2);
	udelay(10);
	return ret;
}

static int chip_ram_read(unsigned short addr,unsigned char *rx_buf,unsigned short len)
{
	struct i2c_client *client = ts_object->driver->client;
	unsigned char buf[3];
	int ret = 0;

	DEBUG();
	buf[0] = 0xff;
	buf[1] = addr >> 8;
	buf[2] = addr & 0x00ff;
	//printk("addr = %x,buf[0] = %x,buf[1] = %x,buf[2] = %x\n",addr,buf[0],buf[1],buf[2]);
	ret = chip_i2c_write(client, client->addr, buf,3);
	if(ret)
	{
		return ret;
	}
	udelay(10);
	buf[0] = 0x00;
	ret = chip_i2c_write(client, client->addr, buf,1);
	udelay(10);
	if(ret)
	{
		return ret;
	}
	udelay(10);
	ret = chip_i2c_read(client,client->addr,rx_buf,len);
	
	return ret;
}

int chip_get_fw_version(unsigned char *buf)
{
	int ret = 0;

	DEBUG();
	ret = chip_ram_read(chip[FW_VERSION].addr,buf,1);
	return ret;
}

#if 0
int chip_get_chip_id(unsigned char *buf)
{
	int ret = 0;

	DEBUG();
	ret = chip_ram_read(chip[CHIP_ID_CMD].addr,buf,1);
	
	return ret;
}
#endif

int chip_enter_sleep_mode(void)
{
	int ret = 0;

	DEBUG();
	if(chip == NULL)
	{
			return -1;	
	}
	ret = chip_ram_write_1byte(chip[CHIP_SLEEP].addr,chip[CHIP_SLEEP].data);
	return ret;
}


int chip_function(enum cmd_index cmd_index,unsigned char *rx_buf,unsigned char len)
{
	int ret = 0;

	DEBUG();

	if(chip[cmd_index].data != RW_FLAG)  //write
	{
		ret = chip_ram_write_1byte(chip[cmd_index].addr,chip[cmd_index].data);
	}
	else  				  //read
	{
		ret = chip_ram_read(chip[cmd_index].addr,rx_buf,len);
	}

	return ret;
}

/***************flash********************/
#if 0
static int chip_flash_init(struct i2c_client *client)
{
	unsigned char buf[2];
	int ret = 0;

	DEBUG();
	
	buf[0] = 0x00;
	buf[1] = 0x00;
	ret = chip_i2c_write(client, FLASH_I2C_ADDR, buf,2);
	
	return ret;
}
#endif

static int chip_read_bus_status(struct i2c_client *client,unsigned char *rx_buf)
{
	unsigned char buf[1];
	int ret = 0;

	DEBUG();
	
	buf[0] = 0x00;
	ret = chip_i2c_write(client, FLASH_I2C_ADDR, buf,1);
	if(ret)
	{
		return ret;
	}
	mdelay(1);
	ret = chip_i2c_read(client,FLASH_I2C_ADDR,rx_buf,1);
	
	return ret;
}

static int chip_enter_idle_mode(struct i2c_client *client)
{
	unsigned char buf[2];
	int ret = 0;

	DEBUG();
	
	buf[0] = 0x00;
	buf[1] = 0xa5;
	ret = chip_i2c_write(client, FLASH_I2C_ADDR, buf,2);
	mdelay(5);
	//mdelay(10);
	return ret;
}

int chip_solfware_reset(struct i2c_client *client)
{
	unsigned char buf[2];
	int ret = 0;

	DEBUG();
	
	buf[0] = 0x00;
	buf[1] = 0x5a;
	ret = chip_i2c_write(client, FLASH_I2C_ADDR, buf,2);
	msleep(200);//ct36x
	//msleep(100);
	return ret;
}

static int chip_erase_flash(struct i2c_client *client)
{
	unsigned char buf[4];
	int sec,sec_addr;
	int ret = 0;

	DEBUG();
	if(chip[FLASH_MASS_ERASE_CMD].addr == 0x33)//ct36x mass erase
	{
		ret = chip_read_bus_status(client,buf);
		if(buf[0] != 0xaa)
		{
			printk("___i2c bus busy,bus_status = %d___\n",buf[0]);
			return -1;
		}
		buf[0] = 0x00;
		buf[1] = chip[FLASH_MASS_ERASE_CMD].addr;
		buf[2] = 0x00;
		buf[3] = 0x00;
		ret = chip_i2c_write(client, FLASH_I2C_ADDR, buf,4);
		if(ret)
		{
			printk("vtl chip flash erase fail\n");
			return ret;
		}
		//printk("mass erase\n");
		//mdelay(10);
		msleep(10);
	}
	else 					  //ct360/ct36x sector erase
	{
		for(sec = 0;sec < chip[FLASH_SECTOR_NUM].addr;sec++)
		{
			ret = chip_read_bus_status(client,buf);
			if(buf[0] != 0xaa)
			{
				printk("___i2c bus busy,bus_status = %x,sec = %d___\n",buf[0],sec);
				return -1;
			}
			sec_addr = sec * chip[FLASH_SECTOR_SIZE].addr;
			buf[0] = 0x00;
			buf[1] = chip[FLASH_SECTOR_ERASE_CMD].addr;
			buf[2] = sec_addr >> 8;
			buf[3] = sec_addr & 0x00ff;
			ret = chip_i2c_write(client, FLASH_I2C_ADDR, buf,4);
			if(ret)
			{
				printk("vtl chip flash erase fail\n");
				return ret;
			}
			//msleep(10);//ct36x
			msleep(100);//ct360
		}
		//printk("sector erase\n");
	}
	return 0;
}

extern unsigned char *gtpfw;
static int chip_set_code(unsigned int flash_addr, unsigned char *buf)
{
	unsigned char i;	
	static unsigned char *binary_data = NULL;

	if (binary_data == NULL) {
		binary_data = gtpfw;
	}

	buf[2] = (flash_addr >> 8);
	buf[3] = (flash_addr & 0xFF);
	buf[4] = 0x08;

	DEBUG();
	if ( (flash_addr == 160) || (flash_addr == 168) ) 
	{
		for(i=0;i<8;i++)
		{
			buf[i+6] = ~binary_data[flash_addr + i];
		}
	} 
	else 
	{
		for(i=0;i<8;i++)
		{
			buf[i+6] = binary_data[flash_addr + i];
		}
	}
	buf[5] = ~(buf[2]+buf[3]+buf[4]+buf[6]+buf[7]+buf[8]+buf[9]+buf[10]+buf[11]+buf[12]+buf[13]) + 1;
	return buf[5];
}

static int chip_get_bin_checksum(void)
{
	unsigned char buf[14];
	int sec,cod;
	int flash_addr;
	unsigned short bin_checksum = 0;

	DEBUG();
	flash_addr = 0x00;
	cod = chip[FLASH_SECTOR_NUM].addr * (chip[FLASH_SECTOR_SIZE].addr/CHIP_FLASH_SOURCE_SIZE);
	for(sec=0;sec<cod;sec++)
	{
		bin_checksum += chip_set_code(flash_addr,buf);
		flash_addr += CHIP_FLASH_SOURCE_SIZE;
		//printk("sec = %d\n",sec);
	}
	return bin_checksum;
}

int chip_get_fwchksum(struct i2c_client *client,int *fwchksum)
{
	unsigned char buf[2];
	int ret = 0;
	
	DEBUG();
	if(chip == NULL){
		return -1;
	}
	ret = chip_ram_write_1byte(chip[FW_CHECKSUM_CMD].addr,chip[FW_CHECKSUM_CMD].data);
	if(ret)
	{
		return -1;
	}
	msleep(700);
	ret = chip_ram_read(chip[FW_CHECKSUM_VAL].addr,buf,2);
	*fwchksum = (buf[0]<<8)|buf[1];
	//chip_solfware_reset(client);
	vtl_ts_hw_reset();
	return 0;
}

static int chip_write_flash(struct i2c_client *client)
{
	unsigned char buf[14];
#if 0
	unsigned char bus_status[1];
#endif
	int sec,cod,sec_8byte_num;
	int flash_addr;
	int ret = 0;

	DEBUG();

	buf[0] = 0x00;
	buf[1] = CHIP_WRITE_FLASH_CMD;
	sec_8byte_num = chip[FLASH_SECTOR_SIZE].addr/CHIP_FLASH_SOURCE_SIZE;
	cod = chip[FLASH_SECTOR_NUM].addr * sec_8byte_num;
	flash_addr = 0x00;
	for(sec=0;sec<cod;)
	{
		chip_set_code(flash_addr,buf);
		flash_addr += CHIP_FLASH_SOURCE_SIZE;
#if 0
		ret = chip_read_bus_status(client,bus_status);
		if(bus_status[0] != 0xaa)
		{
			printk("i2c bus busy,sec = %d,bus_status = %x\n",sec,bus_status[0]);
			return -1;
		}
#endif		
		ret = chip_i2c_write(client,FLASH_I2C_ADDR, buf,14);
		if(ret)
		{
			return ret;
		}
		sec++;
		if(!(sec%sec_8byte_num))
		{
			msleep(10);
			//mdelay(10);
		}
		mdelay(1);//ct360
	}
	return 0;
}

int chip_get_checksum(struct i2c_client *client,int *bin_checksum,int *fw_checksum)
{
	DEBUG();
	
	if(chip == NULL){
		return -1;
	}
	*bin_checksum = chip_get_bin_checksum();
	chip_get_fwchksum(client,fw_checksum);
	//printk("bin_checksum = 0x%x,fw_checksum = 0x%x\n",*bin_checksum,*fw_checksum);
	return 0;
}

int update(struct i2c_client *client)
{
	unsigned char buf[20];
	int ret = 0;
	DEBUG();
	
	if(chip == NULL)
	{
		return -1;
	}
	
	printk("___chip update start___\n");
	ret = chip_enter_idle_mode(client);
	if(ret)
	{
		return -1;
	}
	ret = chip_read_bus_status(client,buf);
	if(buf[0] != 0xaa)
	{
		printk("___i2c bus busy,bus_status = %x___\n",buf[0]);
		return -1;
	}
	ret = chip_erase_flash(client);
	if(ret)
	{
		printk("___erase flash fail___\n");
		return -1;
	}
	ret = chip_write_flash(client);
	if(ret)
	{
		printk("___write flash fail___\n");
		return -1;
	}
	vtl_ts_hw_reset();
	printk("___chip update end___\n");
	
	return 0;
}


int chip_update(struct i2c_client *client)
{
	int bin_checksum = 0xff;
	int fw_checksum = 0;
	int cnt = 0;
	
	DEBUG();
	if(chip == NULL)
	{
		return -1;
	}

	chip_get_checksum(client,&bin_checksum,&fw_checksum);
	printk("bin_checksum = 0x%x,fw_checksum = 0x%x\n",bin_checksum,fw_checksum);
	cnt = 2;
	while((bin_checksum != fw_checksum) && (cnt--))
	{
		if(update(client) < 0)
		{
			vtl_ts_hw_reset();
			continue;
		}
		chip_get_fwchksum(client,&fw_checksum);
		printk("bin_checksum = %x,fw_checksum = %x,cnt = %d\n",bin_checksum,fw_checksum,cnt);
	}
	
	if(bin_checksum != fw_checksum)
	{
		return -1;
	}
	return 0;
}

/*
int chip_update(struct i2c_client *client)
{
	unsigned char buf[20];
	int bin_checksum,fw_checksum,cnt;
	int ret = 0;

	DEBUG();
	
	if(chip == NULL)
	{
		return -1;
	}
	bin_checksum = chip_get_bin_checksum();
	chip_get_fwchksum(client,&fw_checksum);
	printk("bin_checksum = %x,fw_checksum = %x\n",bin_checksum,fw_checksum);
	cnt = 2;
	while((bin_checksum != fw_checksum) && (cnt--))
	//while(cnt--)
	{
		printk("___chip update start___\n");
		ret = chip_enter_idle_mode(client);
		if(ret)
		{
			//return ret;
			continue;
		}
		ret = chip_read_bus_status(client,buf);
		if(buf[0] != 0xaa)
		{
			printk("___i2c bus busy,bus_status = %x___\n",buf[0]);
			//return ret;
			continue;
		}
		ret = chip_erase_flash(client);
		if(ret)
		{
			printk("___erase flash fail___\n");
			//return ret;
			continue;
		}
		ret = chip_write_flash(client);
		if(ret)
		{
			printk("___write flash fail___\n");
			//return ret;
			continue;
		}
		vtl_ts_hw_reset();
		//chip_solfware_reset(client);
		ret = chip_get_fwchksum(client,&fw_checksum);
		if(ret)
		{
			printk("___get fwchksum fail___\n");
			//return ret;
			continue;
		}
		printk("___chip update end___\n");
		printk("bin_checksum = %x,fw_checksum = %x\n",bin_checksum,fw_checksum);
	}
	//vtl_ts_hw_reset();
	if(bin_checksum != fw_checksum)
	{
		return -1;
	}
	return 0;
}
*/

int chip_get_chip_id(struct i2c_client *client,unsigned char *rx_buf)
{
	unsigned char buf[3];
	int ret = 0;

	DEBUG();
	ret = chip_enter_idle_mode(client);
	if(ret)
	{
		return ret;
	}
	ret = chip_read_bus_status(client,buf);
	if(buf[0]!= 0xaa)
	{
		printk("___i2c bus status = %x,ret = %d___\n",buf[0],ret);
		return -1;
	}
	mdelay(1);

	buf[0] = 0xff;
	buf[1] = CHIP_ID_ADDR>>8;
	buf[2] = CHIP_ID_ADDR & 0x00ff;
	ret = chip_i2c_write(client,0x01, buf,3);
	if(ret)
	{
		return ret;
	}
	mdelay(1);
	buf[0] = 0x00;
	ret = chip_i2c_write(client,0x01, buf,1);
	if(ret)
	{
		return ret;
	}
	mdelay(1);
	ret = chip_i2c_read(client,0x01,rx_buf,1);
	//chip_solfware_reset(client);
	vtl_ts_hw_reset();

	//printk("___chip ID = %d___\n",*rx_buf);
	return ret;
	
}


static int chip_read_infoblk(struct i2c_client *client)
{
	unsigned char buf[20] = {0};

	DEBUG();

	buf[0] = 0x00;
	buf[1] = 0x62;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x08;
	
	chip_i2c_write(client,0x7F, buf,5);
	mdelay(1);
	chip_i2c_read(client,0x7f, buf,14);

	if(buf[5] & 0x10)
	{
		
		return 0;
	}
	return 1;
}

static int chip_erase_infoblk(struct i2c_client *client)
{
	unsigned char buf[20]={0};
	int ret = -1;

	DEBUG();
	
	// info block erase command
	buf[0] = 0x00;
	buf[1] = 0x60;
	buf[2] = 0x00;
	chip_i2c_write(client, 0x7F, buf, 3);
	mdelay(10);

	ret = chip_read_bus_status(client,buf);
	if(buf[0]!= 0xaa)
	{
		printk("___i2c bus status = %x,ret = %d___\n",buf[0],ret);
		return -1;
	}
	return 0;
}

static int chip_write_infoblk(struct i2c_client *client)
{
	//int ret = -1;
	unsigned char buf[20]={0};
	int cod;
	unsigned int flash_addr;

	DEBUG();
	
	flash_addr = 0x00;

	// write info block 0
	buf[0] = 0x00;
	buf[1] = 0x61;

	for ( cod = 0; cod < 16; cod++ ) {
	// Flash address
	// data length
	buf[2] = (char)(flash_addr >> 8);
	buf[3] = (char)(flash_addr & 0xFF);
	buf[4] = 0x08;
	if ( flash_addr == 0x0000 )
	buf[6] = 0x17;
	else
	buf[6] = 0x00;
	
	buf[7] = 0x00;
	buf[8] = 0x00;
	buf[9] = 0x00;
	buf[10] = 0x00;
	buf[11] = 0x00;
	buf[12] = 0x00;
	buf[13] = 0x00;

	buf[5] = (~(buf[2]+buf[3]+buf[4]+buf[6]+buf[7]+buf[8]+buf[9]+buf[10]+buf[11]+buf[12]+buf[13]))+1;
		
	chip_i2c_write(client, 0x7F, buf, 14);
	mdelay(10);

	flash_addr += 8;
	}

	return 0;
}

static int chip_trim_info_init(struct i2c_client *client)
{
	int retry =5;

	while(chip_read_infoblk(client) && (retry--))
	{
		chip_erase_infoblk(client);
		chip_write_infoblk(client);
	}
	vtl_ts_hw_reset();
	return 0;
}

int chip_init(void)
{
	struct i2c_client *client;
	unsigned char chip_id = 0xff;
	unsigned char retry;
	int ret = 0;

	DEBUG();

	ts_object = vtl_ts_get_object();
	
	if(ts_object == NULL)
	{
		return -1;
	}
	client = ts_object->driver->client;
	
	chip = NULL;
	for(retry = 0;retry<3;retry++)
	{
		ret = chip_get_chip_id(client,&chip_id);
		printk("___chip ID = %d___cnt = %d\n",chip_id,retry);
		switch(chip_id)
		{
			case 1:	{			//chip: CT362, CT363, CT365, CT368, CT369
					chip = ct36x_cmd;
					chip_trim_info_init(client);
				}break;

			case 2:	{			//chip: CT360
					chip = ct360_cmd;
				}break;

			case 6:	{			//chip: CT362M, CT363M, CT365M, CT368M, CT369M
					chip = ct36x_cmd;
				}break;

			default : {
			
					chip = NULL;
				}
		}
		if(chip != NULL)
		{
			break;
		}
	}

	if(chip == NULL)
	{
		return -1;
	}

	#if(CHIP_UPDATE_ENABLE)
	if(chip_update(client)<0)
	{
		printk("___chip updata faile___\n");
		return -1;
	}
	#endif
	
	return 0;
}



