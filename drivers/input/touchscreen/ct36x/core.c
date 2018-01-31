/* SPDX-License-Identifier: GPL-2.0 */
#include "ct36x_priv.h"

/*
* Private functions
*/
#define CT36X_CHIP_FLASH_SECTOR_NUM	256
#define CT36X_CHIP_FLASH_SECTOR_SIZE	128
#define CT36X_CHIP_FLASH_SOURCE_SIZE	8

static unsigned char ct365_binary_data[] = {
#include "RK_DPT101_CT365_01_V02_099E_140107.dat"
};

static unsigned char ct363_binary_data[] = {
#include  "lx--js77_97_CT365_V01_E7DA_130419.dat" //"wgj97112tsm01_CT363_01_V01_EA50_140224.dat"
};


int ct36x_chip_set_idle(struct ct36x_data *ts)
{
	int ret = 0;
	char buf[2] = {0x00, 0xA5};

	ret = ct36x_update_write(ts, 0x7F, buf, 2);
	mdelay(10);

	return ret;
}

static int ct36x_chip_rst_offset(struct ct36x_data *ts)
{
	int ret = 0;
	char buf = 0x00;

	ret =ct36x_update_write(ts, 0x7F, &buf, 1);
	mdelay(10);

	return ret;
}

static char ct36x_chip_get_bus_status(struct ct36x_data *ts)
{
	int ret = 0;
	char buf;

	ret =ct36x_update_read(ts, 0x7F, &buf, 1);
	mdelay(10);

	return (ret < 0)?-1:buf;
}

static int ct36x_chip_era_flash(struct ct36x_data *ts)
{
	char c;
	int ret = 0;
	char buf[3] = {0x00, 0x33, 0x00};

	ret = ct36x_update_write(ts, 0x7F, buf, 3);
	mdelay(10);

	// Reset I2C offset address
	ret = ct36x_chip_rst_offset(ts);
	if(ret < 0){
		dev_err(ts->dev, "CT36X chip: Failed to reset I2C offset address\n");
		return ret;
	}

	// Read I2C bus status
	c = ct36x_chip_get_bus_status(ts);
	if ( c != 0xAA ) {
		dev_err(ts->dev, "CT36X chip: Failed to get bus status: %d\n", c);
		return -1;
	}

	return 0;
}

/*
** Prepare code segment
*/
static int ct36x_chip_set_code(unsigned short flash_addr, char *buf)
{
	unsigned char cod_chksum;
	unsigned char *binary_data;

	// Flash address
	// data length
	buf[2] = (char)(flash_addr >> 8);
	buf[3] = (char)(flash_addr & 0xFF);
	buf[4] = 0x08;
	
	if(flag_ct36x_model ==365)
		binary_data = ct365_binary_data;
	else if(flag_ct36x_model ==363)
		binary_data = ct363_binary_data;
		
	// Fill firmware source data
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

static int ct36x_chip_wr_firmware(struct ct36x_data *ts)
{
	int ret = 0;
	int sec, cod;
	unsigned char cod_chksum = 0x00;
	unsigned int fin_chksum = 0x00;
	unsigned short flash_addr;
	char buf[14];
	
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
			ret =ct36x_update_write(ts, 0x7F, buf, 14);
			if(ret < 0)
				return ret;
			mdelay(1);

			// Increase flash address 8bytes for each write command
			flash_addr += CT36X_CHIP_FLASH_SOURCE_SIZE;
		}
		//
		mdelay(20);
	}

	return 0;
}

int ct36x_chip_get_binchksum(void)
{
	int sec, cod;
	unsigned char cod_chksum;
	unsigned int fin_chksum = 0;
	unsigned short flash_addr;
	char buf[14];

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

int ct36x_chip_get_fwchksum(struct ct36x_data *ts)
{
	int ret, fwchksum = 0x00;
	char buf[3];

	buf[0] = 0xFF;
	buf[1] = 0x8F;
	buf[2] = 0xFF;
	ret = ct36x_write(ts, buf, 3);
	if(ret < 0)
		return ret;
	mdelay(20);

	buf[0] = 0x00;
	buf[1] = 0xE1;
	ret = ct36x_write(ts, buf, 2);
	if(ret < 0)
		return ret;
	mdelay(500);

	buf[0] = 0xFF;
	buf[1] = 0x8E;
	buf[2] = 0x0E;
	ret = ct36x_write(ts, buf, 3);
	if(ret < 0)
		return ret;
	mdelay(20);

	ret = ct36x_chip_rst_offset(ts);
	if(ret < 0){
		dev_err(ts->dev, "CT36X chip: Failed to reset I2C offset address\n");
		return ret;
	}

	ret = ct36x_read(ts, buf, 3);
	if(ret < 0)
		return ret;

	mdelay(20);
	fwchksum = ((buf[0]<<8) | buf[1]);

	return fwchksum;
}

int ct36x_chip_get_ver(struct ct36x_data *ts)
{
	int ret = 0;
	char buf[3], ver;

	// Read version command
	buf[0] = 0xFF;
	buf[1] = 0x3F;
	buf[2] = 0xFF;
	ret = ct36x_write(ts, buf, 3);
	if(ret < 0)
		return ret;
	mdelay(10);

	buf[0] = 0x00;
	ret = ct36x_write(ts, buf, 1);
	if(ret < 0)
		return ret;
	mdelay(10);

	// do read version
	ret = ct36x_read(ts, &ver, 1);
	if(ret < 0)
		return ret;
	mdelay(10);

	return ver;
}

int ct36x_chip_get_vendor(struct ct36x_data *ts)
{
	return 0;
}

int ct36x_chip_go_sleep(struct ct36x_data *ts)
{
	int ret = 0;
	char buf[3];

	buf[0] = 0xFF;
	buf[1] = 0x8F;
	buf[2] = 0xFF;
	ret = ct36x_write(ts, buf, 3);
	if(ret < 0)
		return ret;
	mdelay(3);

	buf[0] = 0x00;
	buf[1] = 0xAF;
	ret = ct36x_write(ts, buf, 2);
	if(ret < 0)
		return ret;
	mdelay(3);
	
	return 0;
}

int ct36x_chip_go_bootloader(struct ct36x_data *ts)
{
	int ret = 0;
	char  c;

	// Init bootloader
	ret = ct36x_chip_set_idle(ts);
	if(ret < 0){
		dev_err(ts->dev, "CT36X chip: Failed to set idle\n");
		return ret;
	}

	// Reset I2C offset address
	ret = ct36x_chip_rst_offset(ts);
	if(ret < 0){
		dev_err(ts->dev, "CT36X chip: Failed to reset I2C offset address\n");
		return ret;
	}

	// Get I2C bus status
	c = ct36x_chip_get_bus_status(ts);
	if ( c != 0xAA ) {
		dev_err(ts->dev, "CT36X chip: Failed to get bus status: %d\n", c);
		return -1;
	}

	// Erase flash
	ret = ct36x_chip_era_flash(ts);
	if ( ret < 0 ) {
		dev_err(ts->dev, "CT36X chip: Failed to era flash\n");
		return ret;
	}

	// Write source data
	ret = ct36x_chip_wr_firmware(ts);
	if(ret < 0){
		dev_err(ts->dev, "CT36X chip: Failed to write firmware\n");
		return ret;
	}
	return 0;
}
