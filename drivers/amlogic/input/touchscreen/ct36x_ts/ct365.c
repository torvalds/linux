
#include <linux/i2c.h>
#include <linux/delay.h>

#include "tscore.h"
#include "amlchip.h"
#include "ct365.h"

/*
* Private functions
*/
#define CT36X_CHIP_FLASH_SECTOR_NUM	256
#define CT36X_CHIP_FLASH_SECTOR_SIZE	128
#define CT36X_CHIP_FLASH_SOURCE_SIZE	8

#ifdef LATE_UPGRADE
	static unsigned char *binary_data = NULL;
#else
static unsigned char binary_data[] = {
		#include "dr42sd02_78_CT362_V06_E8CF_130930.dat"/*for k101*/
};
#endif
extern struct ct36x_ts_info	ct36x_ts;

static void ct36x_chip_set_idle(struct i2c_client *client, unsigned char *buf)
{
	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	buf[0] = 0x00;
	buf[1] = 0xA5;
	ct36x_ts_reg_write(client, 0x7F, buf, 2);
	mdelay(10);
}

static void ct36x_chip_rst_offset(struct i2c_client *client, unsigned char *buf)
{
	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	buf[0] = 0x00;
	ct36x_ts_reg_write(client, 0x7F, buf, 1);
	mdelay(10);
}

static int ct36x_chip_get_busstatus(struct i2c_client *client, unsigned char *buf)
{
	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	ct36x_ts_reg_read(client, 0x7F, buf, 1);
	mdelay(10);

	return buf[0];
}

static int ct36x_chip_erase_flash(struct i2c_client *client, unsigned char *buf)
{
	int ret = -1;

	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	// Erase 32k flash
	buf[0] = 0x00;
	buf[1] = 0x33;
	buf[2] = 0x00;
	ct36x_ts_reg_write(client, 0x7F, buf, 3);
	mdelay(10);

	// Reset I2C offset address
	ct36x_chip_rst_offset(client, buf);

	// Read I2C bus status
	ret = ct36x_chip_get_busstatus(client, buf);
	if ( ret != 0xAA ) {
		return -1;
	}

	return 0;
}

/*
** Prepare code segment
*/
static int ct36x_chip_set_code(unsigned int flash_addr, unsigned char *buf)
{
	unsigned char cod_chksum;

	//if ( CT36X_TS_CHIP_DEBUG )
	//printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	// Flash address
	// data length
	buf[2] = (char)(flash_addr >> 8);
	buf[3] = (char)(flash_addr & 0xFF);
	buf[4] = 0x08;

	// Fill firmware source data
	//if ( (sec == 1 && cod == 4) || (sec == 1 && cod == 5) ) {
	//if ( flash_addr == (CT36X_CHIP_FLASH_SECTOR_SIZE + 32) || 
	//flash_addr == (CT36X_CHIP_FLASH_SECTOR_SIZE + 40) ) {
	if ( flash_addr == (160) || flash_addr == (168) ) {
		buf[6] = ~binary_data[flash_addr + 0];
		buf[7] = ~binary_data[flash_addr + 1];
		buf[8] = ~binary_data[flash_addr + 2];
		buf[9] = ~binary_data[flash_addr + 3];
		buf[10] = ~binary_data[flash_addr + 4];
		buf[11] = ~binary_data[flash_addr + 5];
		buf[12] = ~binary_data[flash_addr + 6];
		buf[13] = ~binary_data[flash_addr + 7];
	} else {
		buf[6] = binary_data[flash_addr + 0];
		buf[7] = binary_data[flash_addr + 1];
		buf[8] = binary_data[flash_addr + 2];
		buf[9] = binary_data[flash_addr + 3];
		buf[10] = binary_data[flash_addr + 4];
		buf[11] = binary_data[flash_addr + 5];
		buf[12] = binary_data[flash_addr + 6];
		buf[13] = binary_data[flash_addr + 7];
	}
			
	/* Calculate a checksum by Host controller. 
	** Checksum =  ~(FLASH_ADRH+FLASH_ADRL+LENGTH+
	** Binary_Data1+Binary_Data2+Binary_Data3+Binary_Data4+
	** Binary_Data5+Binary_Data6+Binary_Data7+Binary_Data8) + 1
	*/
	cod_chksum = ~(buf[2]+buf[3]+buf[4]+
				buf[6]+buf[7]+buf[8]+buf[9]+
				buf[10]+buf[11]+buf[12]+buf[13]) + 1;
	buf[5] = cod_chksum;

	return cod_chksum;
}

static int ct36x_chip_write_firmware(struct i2c_client *client, unsigned char *buf)
{
//	int ret = -1;
	int sec, cod;
	unsigned char cod_chksum;
	unsigned int fin_chksum;
	unsigned int flash_addr;

	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	// Code checksum, final checksum
	cod_chksum = 0x00; fin_chksum = 0x00;

	// Flash write command
	buf[0] = 0x00;
	buf[1] = 0x55;

	// 256 sectors, 128 bytes per sectors
	for ( sec = 0; sec < CT36X_CHIP_FLASH_SECTOR_NUM; sec++ ) {
		flash_addr = sec * CT36X_CHIP_FLASH_SECTOR_SIZE;
		// 16 segments, 8 bytes per segment
		for ( cod = 0; cod < (CT36X_CHIP_FLASH_SECTOR_SIZE/CT36X_CHIP_FLASH_SOURCE_SIZE); cod++ ) {
			// Fill binary data
			cod_chksum = ct36x_chip_set_code(flash_addr, buf);
			fin_chksum += cod_chksum;

			// Write firmware source data
			ct36x_ts_reg_write(client, 0x7F, buf, 14);

			// 
			mdelay(1);

			// Increase flash address 8bytes for each write command
			flash_addr += CT36X_CHIP_FLASH_SOURCE_SIZE;
		}
		//
		mdelay(20);
	}

	return 0;
}

static int ct36x_chip_read_infoblk(struct i2c_client *client)
{
	unsigned char buf[20] = {0};
//	unsigned char i;
	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	buf[0] = 0x00;
	buf[1] = 0x62;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x08;

	ct36x_ts_reg_write(client, 0x7F, buf, 5);
	msleep(1);
	ct36x_ts_reg_read(client, 0x7f, buf, 14);

	if(buf[5] & 0x10)
	{

		return 0;
	}
	return 1;
}

static int ct36x_chip_erase_infoblk(struct i2c_client *client)
{
	unsigned char buf[20]={0};
	int ret = -1;

	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	// info block erase command
	buf[0] = 0x00;
	buf[1] = 0x60;
	buf[2] = 0x00;
	ct36x_ts_reg_write(client, 0x7F, buf, 3);
	mdelay(10);

	// Reset I2C offset address
	ct36x_chip_rst_offset(client, buf);

	// Read I2C bus status
	ret = ct36x_chip_get_busstatus(client, buf);
	if ( ret != 0xAA ) {
		printk("trim data erase error!!! \n");
		return -1;
	}

	return 0;
}

static int ct36x_chip_write_infoblk(struct i2c_client *client)
{
	//int ret = -1;
	unsigned char buf[20]={0};
	int cod;
	unsigned int flash_addr;

	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

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
		
	ct36x_ts_reg_write(client, 0x7F, buf, 14);
	mdelay(10);

	flash_addr += 8;
	}

	return 0;
}

int ct36x_chip_get_binchksum(unsigned char *buf)
{
	int sec, cod;
	unsigned char cod_chksum;
	unsigned int fin_chksum = 0;
	unsigned int flash_addr;

	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	// 256 sectors, 128 bytes per sectors
	for ( sec = 0; sec < CT36X_CHIP_FLASH_SECTOR_NUM; sec++ ) {
		flash_addr = sec * CT36X_CHIP_FLASH_SECTOR_SIZE;
		// 16 segments, 8 bytes per segment
		for ( cod = 0; cod < (CT36X_CHIP_FLASH_SECTOR_SIZE/CT36X_CHIP_FLASH_SOURCE_SIZE); cod++ ) {
			// Fill binary data
			cod_chksum = ct36x_chip_set_code(flash_addr, buf);
			fin_chksum += cod_chksum;

			// Increase flash address 8bytes for each write command
			flash_addr += CT36X_CHIP_FLASH_SOURCE_SIZE;
		}
	}

	return (unsigned short)fin_chksum;
}

int ct36x_chip_get_fwchksum(struct i2c_client *client, unsigned char *buf)
{
	int fwchksum = 0x00;

	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	buf[0] = 0xFF;
	buf[1] = 0x8F;
	buf[2] = 0xFF;
	ct36x_ts_reg_write(client, client->addr, buf, 3);
	mdelay(2);

	buf[0] = 0x00;
	buf[1] = 0xE1;
	ct36x_ts_reg_write(client, client->addr, buf, 2);
	msleep(500);

	buf[0] = 0xFF;
	buf[1] = 0x8E;
	buf[2] = 0x0E;
	ct36x_ts_reg_write(client, client->addr, buf, 3);
	mdelay(2);

	ct36x_chip_rst_offset(client, buf);

	ct36x_ts_reg_read(client, client->addr, buf, 3);
	mdelay(2);

	fwchksum = ((buf[0]<<8) | buf[1]);

	return fwchksum;
}

int ct36x_chip_get_ver(struct i2c_client *client, unsigned char *buf)
{
	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	// Read version command
	buf[0] = 0xFF;
	buf[1] = 0x3F;
	buf[2] = 0xFF;

	ct36x_ts_reg_write(client, client->addr, buf, 3);
	mdelay(10);

	buf[0] = 0x00;
	ct36x_ts_reg_write(client, client->addr, buf, 1);
	mdelay(10);

	// do read version
	ct36x_ts_reg_read(client, client->addr, buf, 1);
	mdelay(10);

	return buf[0];
}

int ct36x_chip_get_vendor(struct i2c_client *client, unsigned char *buf)
{
	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	return 0;
}

void ct36x_chip_go_sleep(struct i2c_client *client, unsigned char *buf)
{
	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	buf[0] = 0xFF;
	buf[1] = 0x8F;
	buf[2] = 0xFF;
	ct36x_ts_reg_write(client, client->addr, buf, 3);
	mdelay(3);

	buf[0] = 0x00;
	buf[1] = 0xAF;
	ct36x_ts_reg_write(client, client->addr, buf, 2);
	mdelay(3);

	//mdelay(50);
}

int ct36x_chip_go_bootloader(struct i2c_client *client, unsigned char *buf)
{
	int ret = -1;

	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	// Init bootloader
	ct36x_chip_set_idle(client, buf);

	// Reset I2C offset address
	ct36x_chip_rst_offset(client, buf);

	// Get I2C bus status
	ret = ct36x_chip_get_busstatus(client, buf);
	if ( ret != 0xAA ) {
		printk("I2C bus status: 0x%x.\n", ret);
		return -1;
	}


	// Erase flash
	ret = ct36x_chip_erase_flash(client, buf);
	if ( ret ) {
		printk("Erase flash failed.\n");
		return -1;
	}

	// Write source data
	ct36x_chip_write_firmware(client, buf);

	return 0;
}

void ct36x_chip_set_adapter_on(struct i2c_client *client, unsigned char *buf)
{
	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	buf[0] = 0xFF;
	buf[1] = 0x0F;
	buf[2] = 0xFF;
	ct36x_ts_reg_write(client, client->addr, buf, 3);
	mdelay(3);

	buf[0] = 0x00;
	buf[1] = 0xE3;
	ct36x_ts_reg_write(client, client->addr, buf, 2);
	mdelay(3);
}

void ct36x_chip_set_adapter_off(struct i2c_client *client, unsigned char *buf)
{
	if ( CT36X_TS_CHIP_DEBUG )
	printk(">>>>> %s() called <<<<< \n", __FUNCTION__);

	buf[0] = 0xFF;
	buf[1] = 0x0F;
	buf[2] = 0xFF;
	ct36x_ts_reg_write(client, client->addr, buf, 3);
	mdelay(3);

	buf[0] = 0x00;
	buf[1] = 0xE2;
	ct36x_ts_reg_write(client, client->addr, buf, 2);
	mdelay(3);
}

#define READ_COUNT  5
void ct36x_upgrade_touch(void)
{
	int binchksum = 0, fwchksum = 0, updcnt = 2;
	u32 offset = 0,count = 0;
	int file_size;
	u8 tmp[READ_COUNT];
	int i_ret, i;

	file_size = touch_open_fw(ts_com->fw_file);
	printk("%s: file_size = %d\n", __func__, file_size);
	if(file_size < 0) {
			printk("%s: no fw file\n", ts_com->owner);
			return;
	}

	if (binary_data == NULL)
		binary_data = (unsigned char*)vmalloc(sizeof(*binary_data)*(file_size/5));
	if (binary_data == NULL) {
		printk("Insufficient memory in upgrade!\n");
		return;
	}

	while (offset < file_size) {
	memset(tmp, 0, READ_COUNT);
    touch_read_fw(offset, min_t(int,file_size-offset,READ_COUNT), &tmp[0]);
    i_ret = sscanf(&tmp[0],"0x%x,",(int *)(binary_data + count));
    if (i_ret == 1) {
			count++;
			offset += READ_COUNT;
		}
	else
    offset++;
	}
	printk("touch dump fw data:");
	for (i=0; i<20; i++ ) {
		printk("%x ", binary_data[i]);
	}
	printk("\ntouch dump fw data:");
	for (i=count-20; i<count; i++) {
		printk("%x ", binary_data[i]);
	}
	printk("\n");
	touch_close_fw();
	/* Hardware reset */
	#ifdef CONFIG_OF
	ct36x_platform_hw_reset(&ct36x_ts);
	#else
	ct36x_platform_hw_reset(plat_data);
	#endif
	binchksum = ct36x_chip_get_binchksum(ct36x_ts.data.buf);
	printk("Bin checksum: 0x%x\n", binchksum);
	fwchksum = ct36x_chip_get_fwchksum(ct36x_ts.client, ct36x_ts.data.buf);
	printk("Fw checksum: 0x%x\n", fwchksum);
	while ( binchksum != fwchksum && updcnt--) {
		/* Update Firmware */
		ct36x_chip_go_bootloader(ct36x_ts.client, ct36x_ts.data.buf);

		/* Hardware reset */
		#ifdef CONFIG_OF
		ct36x_platform_hw_reset(&ct36x_ts);
		#else
		ct36x_platform_hw_reset(plat_data);
		#endif

		// Get firmware Checksum
		fwchksum = ct36x_chip_get_fwchksum(ct36x_ts.client, ct36x_ts.data.buf);
		printk("Fw checksum: 0x%x\n", fwchksum);
	}
	printk("Fw update %s. 0x%x, 0x%x\n", binchksum != fwchksum ? "Failed" : "Success", binchksum, fwchksum);

	/* Hardware reset */
	#ifdef CONFIG_OF
	ct36x_platform_hw_reset(&ct36x_ts);
	#else
	ct36x_platform_hw_reset(plat_data);
	#endif
	if (binary_data != NULL) {
		vfree(binary_data);
		binary_data = NULL;
	}
}
#ifdef LATE_UPGRADE
int ct36x_late_upgrade(void *p)
{
	int file_size;
//	static int count;
	while(1) {
		file_size = touch_open_fw(ts_com->fw_file);
		if(file_size < 0) {
			//printk("%s: %d\n", __func__, count++);
			msleep(10);
		}
		else break;
	}
	touch_close_fw();
	ct36x_upgrade_touch();
	printk("%s: load firmware\n", ts_com->owner);
#if defined(CONFIG_TOUCHSCREEN_CT36X_CHIP_CT365)
	ct36x_check_trim(ct36x_ts.client);
#endif
	enable_irq(ct36x_ts.irq);
	//do_exit(0);
	return 0;
}
#endif
void ct36x_read_version(char* ver)
{
	unsigned char buf[3], ver_info;

	ver_info = ct36x_chip_get_ver(ct36x_ts.client, buf);
	if (ver != NULL)
		sprintf(ver,"VTL TouchScreen Version:%d\n", ver_info);
	else
		printk("VTL TouchScreen Version:%d\n", ver_info);
}

int ct36x_get_chip_id(struct i2c_client *client,unsigned char *rx_buf)
{
	unsigned char buf[3];
	int ret = 0;

	ct36x_chip_set_idle(client, buf);

	// Reset I2C offset address
	ct36x_chip_rst_offset(client, buf);

	// Get I2C bus status
	ret = ct36x_chip_get_busstatus(client, buf);
	if ( ret != 0xAA ) {
		printk("I2C bus status: 0x%x.\n", ret);
		return -1;
	}


	buf[0] = 0xff;
	buf[1] = 0xf0;
	buf[2] = 0x00;
	ct36x_ts_reg_write(client,0x01, buf,3);

	buf[0] = 0x00;
	ct36x_ts_reg_write(client,0x01, buf,1);

	ct36x_ts_reg_read(client,0x01,rx_buf,1);
	#ifdef CONFIG_OF
	ct36x_platform_hw_reset(&ct36x_ts);
	#else
	ct36x_platform_hw_reset(plat_data);
	#endif

	//printk("___chip ID = %d___\n",*rx_buf);
	return 0;

}

int ct36x_check_trim(struct i2c_client *client)
{
	int retry = 5;
	unsigned char chip_id = 0;
	ct36x_get_chip_id(client,&chip_id);
	if(chip_id == 0x01)//ct36x
	{
		while(ct36x_chip_read_infoblk(client) && (retry--))
		{
			ct36x_chip_erase_infoblk(client);
			ct36x_chip_write_infoblk(client);
		}
	#ifdef CONFIG_OF
	ct36x_platform_hw_reset(&ct36x_ts);
	#else
	ct36x_platform_hw_reset(plat_data);
	#endif
	}

	return 0;
}
