/*
 *  max17047_config.c
 *  fuel-gauge systems for lithium-ion (Li+) batteries
 *
 *  Copyright (C) 2012 Hardkernel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/power/max17047_battery.h>
#include <linux/slab.h>

const	unsigned short	CustomModel[] = {
	// Write Start Reg 0x80
	0x8AE0,	0xB5A0,	0xB990,	0xBA20,	0xBA90,	0xBAF0,	0xBB60,	0xBC40,	
	0xBD40,	0xBF50,	0xC080,	0xC310,	0xC5D0,	0xC970,	0xCCF0,	0xD0D0,	
	
	// Write Start Reg 0x90
	0x0080,	0x0600,	0x2900,	0x0020,	0x3B00,	0x3C60,	0x1840,	0x10B0,
	0x1800,	0x1600,	0x0E20,	0x0AC0,	0x0B60,	0x07B0,	0x08E0,	0x08E0,

	// Write Start Reg 0xA0
	0x0100,	0x0100,	0x0100,	0x0100,	0x0100,	0x0100,	0x0100,	0x0100,
	0x0100,	0x0100,	0x0100,	0x0100,	0x0100,	0x0100,	0x0100,	0x0100,
};

const	unsigned short	RCOMP0		= 0x0082;
const	unsigned short	TempCo		= 0x041D;
const	unsigned short	ICHGTerm	= 0x0166;
const	unsigned short	V_empty		= 0xACDA;
const	unsigned short	QRTable00	= 0x3F80;
const	unsigned short	QRTable10	= 0x2C80;
const	unsigned short	QRTable20	= 0x1A04;
const	unsigned short	QRTable30	= 0x1905;
const	unsigned short	Capacity	= 0x2F22;
const	unsigned short	TGAIN		= 0xE3E1;
const	unsigned short	TOFF		= 0x290E;

static int max17047_read_reg(struct i2c_client *client, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_word_data(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}

static int max17047_write_reg(struct i2c_client *client, u8 reg, u16 value)
{
	int ret;

	ret = i2c_smbus_write_word_data(client, reg, value);

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}

static int max17047_write_verify(struct i2c_client *client, u8 reg, u16 value)
{
	int ret;

	ret = i2c_smbus_write_word_data(client, reg, value);

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
		
	ret = i2c_smbus_read_word_data(client, reg);
	
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
		
	if(ret == value)	return	0;

	dev_err(&client->dev, "%s : reg(0x%02X) write(0x%04X) read(0x%04X)\n", __func__, reg, value, ret);
	
	return ret;
}

static int max17047_reset_check(struct i2c_client *client)
{
	return	(max17047_read_reg(client, 0x00) & 0x0002);
}

void max17047_set_config(struct i2c_client *client)
{
	int		cnt, VFSOC, FullCap0, RemCap, RepCap, dQ_acc, Status;
	
	if(!max17047_reset_check(client))		return;
	
restart:
	// 1. Delay 500ms
	mdelay(500);
	// 2. Initialize Configuration
	max17047_write_reg(client, 0x2A, 0x506B);	// Write RelaxCFG
	max17047_write_reg(client, 0x1D, 0x2210);	// Write CONFIG(10kohm thermistor from battery pack)
	max17047_write_reg(client, 0x29, 0x87A2);	// Write FilterCFG
	max17047_write_reg(client, 0x28, 0x2607);	// Write LearnCFG
	max17047_write_reg(client, 0x13, 0x5F00);	// Write FullSOCthr=95%
	// LOAD CUSTOM MODEL AND PARAMETERS
	// 4. Unlock Model Access
	max17047_write_reg(client, 0x62, 0x0059);	// Unlock Model Access
	max17047_write_reg(client, 0x63, 0x00C4);
	// 5. Write/Read/Verify the Custom Model
write_custom_model:	
	for(cnt = 0; cnt < sizeof(CustomModel)/sizeof(CustomModel[0]); cnt++)	{
		if(max17047_write_verify(client, 0x80 + cnt, CustomModel[cnt]))	goto write_custom_model;
	}
	
	// 8. Lock Model Access	
lock_model:	
	max17047_write_reg(client, 0x62, 0x0000);
	max17047_write_reg(client, 0x63, 0x0000);
	
	for(cnt = 0; cnt < sizeof(CustomModel)/sizeof(CustomModel[0]); cnt++)	{
		if(max17047_read_reg(client, 0x80 + cnt) != 0x0000)	goto lock_model;
	}

	// 10. Write Custom Parameters
	max17047_write_verify(client, 0x38, RCOMP0);	// Write and Verify RCOMP0 = 0082h
	max17047_write_verify(client, 0x39, TempCo);	// Write and Verify TempCo = 041Dh
	max17047_write_reg(client, 0x1E, ICHGTerm);		// Write ICHGTerm = 0166h
	max17047_write_verify(client, 0x3A, V_empty);	// Write and Verify Vempty = ACDAh
	max17047_write_verify(client, 0x12, QRTable00);	// Write and Verify QRTable00 = 3F80h
	max17047_write_verify(client, 0x22, QRTable10);	// Write and Verify QRTable10 = 2C80h
	max17047_write_verify(client, 0x32, QRTable20);	// Write and Verify QRTable20 = 1A04h
	max17047_write_verify(client, 0x42, QRTable30);	// Write and Verify QRTable30 = 1905h

	// 11. Update Full Capacity Parameters
	// Capacity is value provide by Maxim. (Capacity = 6033 x 2 = 2F22h)
	max17047_write_verify(client, 0x10, Capacity);	// Write and Verify FullCap
	max17047_write_reg(client, 0x18, Capacity);		// Write DesignCap
	max17047_write_verify(client, 0x23, Capacity);	// Write and Verify FullCapNom

	// 13. Delay at least 350ms
	mdelay(350);
	
	// 14. Write VFSOC value to VFSOC0
	VFSOC = max17047_read_reg(client, 0xFF);		// read VFSOC
	max17047_write_reg(client, 0x60, 0x0080);		// Enable Write Access to VFSOC0
	max17047_write_verify(client, 0x48, VFSOC);		// Write and Verify VFSOC0
	max17047_write_reg(client, 0x60, 0x0000);		// Disable Write Access to VFSOC0

	// 15. Advance to Coulomb-Counter Mode
	// To advance to Coulomb-Counter Mode, simply write the Cycle register to value of 96% for MAX17047
	max17047_write_verify(client, 0x17, 0x0060);

	// 16, Load New Capacity Parameters
	// VFSOC was read in step 14
	FullCap0 = max17047_read_reg(client, 0x35);		// Read FullCap0
	RemCap = (VFSOC * FullCap0) / 25600;
	max17047_write_verify(client, 0x0F, RemCap);	// Write and Verify RemCap
	RepCap = RemCap;
	max17047_write_verify(client, 0x05, RepCap);	// Write and Verify RepCap
	// Write dQ_acc to 200% of Capacity and dP_acc to 200%
	dQ_acc = (Capacity / 4);
	max17047_write_verify(client, 0x45, dQ_acc);	// Write and Verify dQ_acc
	max17047_write_verify(client, 0x46, 0x3200);	// Write and Verify dP_acc
	max17047_write_verify(client, 0x10, Capacity);	// Write and Verify FullCap
	max17047_write_reg(client, 0x18, Capacity);		// Write DesignCap
	max17047_write_verify(client, 0x23, Capacity);	// Write and Verify FullCapNom

	// 16.1 Set Gain and Offset Register Values
	max17047_write_reg(client, 0x2C, TGAIN);		// Write TGAIN = E3E1h
	max17047_write_reg(client, 0x2D, TOFF);			// Write TOFF = 290Eh

	// 17. Initialization Complet
	// Clear the POR & Bi bit to indicate that the custom model and parameters were successfully loaded.
	Status = max17047_read_reg(client, 0x00);		// Read Status
	max17047_write_verify(client, 0x00, Status & 0xFFFD);	// Write and Verify Status with POR bit Cleared
	
	// 17.5 Check for MAX17047 Reset
	Status = max17047_read_reg(client, 0x00);		// Read Status
	if(Status & 0x0002)		goto restart;
}
