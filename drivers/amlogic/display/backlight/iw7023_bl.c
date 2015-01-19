/*
 * iW7023 Driver for LCD Panel Backlight
 *
 * Copyright (C) 2012 AMLOGIC, INC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/irq.h>
#include <linux/i2c/at24.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#include <linux/panel/iw7023.h>
#include <linux/panel/backlight.h>
#include <mach/am_regs.h>
#if defined(CONFIG_AMLOGIC_SPI_HW_MASTER)
#include <mach/spi.h>
#endif
#include <plat/fiq_bridge.h>

#include <mach/gpio.h>
#include "iw7023_bl.h"
#include "iw7023_lpf.h"

static struct iw7023_platform_data *pdata = NULL;
static struct spi_device *spi_dev;

static int iw7023_status = 0;

//extern int fiq_register(int irq, void (*handler)(void));
//extern int fiq_unregister(int irq, void (*handler)(void));


#define IW7023_RD		(0<<12)
#define IW7023_WR		(1<<12)
#define IW7023_ADDR_INC		(0<<13)
#define IW7023_ADDR_REP		(1<<13)
#define IW7023_ADDR(addr)	(((addr)&0x3ff) << 2)


static bool spi_debug = false;
module_param(spi_debug, bool, 0644);

static u16 brightness_2d = BRIGHTNESS_2D;
module_param(brightness_2d, ushort, 0644);

static u16 brightness_3d = BRIGHTNESS_3D;
module_param(brightness_3d, ushort, 0644);

static bool set_bri_zero_en = true;
module_param(set_bri_zero_en, bool, 0644);

static u16 iset_value_2d = ISET_VALUE_2D_SKY42;
module_param(iset_value_2d, ushort, 0644);

static u16 iset_value_3d = ISET_VALUE_3D_SKY42;
module_param(iset_value_3d, ushort, 0644);

static u16 vdac_value_2d = VDAC_VALUE_2D_SKY42;
module_param(vdac_value_2d, ushort, 0644);

static u16 vdac_2d_backup = VDAC_VALUE_2D_SKY42;
module_param(vdac_2d_backup, ushort, 0644);

static u16 vdac_value_3d = VDAC_VALUE_3D_SKY42;
module_param(vdac_value_3d, ushort, 0644);

static u16 vdac_3d_backup = VDAC_VALUE_3D_SKY42;
module_param(vdac_3d_backup, ushort, 0644);

static u16 vsync_delay_set_bri_zero = VSYNC_CNT_SET_BRI_ZERO;
module_param(vsync_delay_set_bri_zero, ushort, 0644);


static u16 td0_2d   = DEFAULT_TD0_2D;
static u16 dg1_2d   = DEFAULT_DG1_2D;
static u16 delta_2d = DEFAULT_DELTAT_2D;

static u16 td0_3d   = DEFAULT_TD0_3D;
static u16 dg1_3d   = DEFAULT_DG1_3D;
static u16 delta_3d = DEFAULT_DELTAT_3D;

static bool ld_enable = false;
module_param(ld_enable, bool, 0644);

static bool w_mode_inc = true;
module_param(w_mode_inc, bool, 0644);

static struct workqueue_struct *workqueue = NULL;
static struct work_struct backup_vdac_work;

enum {
	MODE_INIT = 0,
	MODE_2D,
	MODE_2D_TO_3D,
	MODE_3D,
	MODE_3D_TO_2D,
};

static unsigned int mode = MODE_INIT;

/*
 * 3	SW 39"
 * 4	SW 42"
 * 5	SW 50"
 *
 * 6	CM 39"
 * 7	CM 42"
 * 8	CM 50"
 */
static u8 panel = 4; // default is skyworth 42"
static s32 panel_setup = -1;
static u32 channel_num = 8;

/*
 * bits		field		2D			3D
 *------------------------------------------------------------------------
 * 0x0A0
 * bit[0]	cali_time_step	1			1
 * bit[1]	cali_cfg
 * bit[3:2]	clk_set		3			3
 * bit[4]	vled_short
 * bit[5]	lum_trk_en
 * bit[6]	otf_apt_en	1			1
 * bit[7]	apt_sw_en	1			1
 * bit[9:8]	pwm_mdl_cfg	1			1
 * bit[10]	cg_cfg
 * bit[11]	vdac_rvs_en
 * bit[13:12]	apt_sw_cfg	1			1
 * bit[14]	ldac_hf_en
 * bit[15]	auto_rcv_en
 *
 * 0x0A1
 * bit[7:0]	iset		0x37(55x2=110mA)	0xA5(165x2=330mA)
 * bit[15:8]	siset
 *
 * 0x0A2
 * bit[13]	iset_2mA_en	1			1
 */
static const struct iwatt_reg_map onetime_map[] =
{
	/* address, 2D value, 3D value */
	{0x200, 0x0000, 0x0000},
	{0x0A0, 0xE1CF, 0xE1CF},
	// 39"
	//{0x0A1, 0XB43C, 0XB4B4},
	// 42" panal
	{0x0A1, ISET_VALUE_2D_SKY42, ISET_VALUE_3D_SKY42},
	// 50"
	//{0x0A1, 0XB43C, 0XB4B4},
	{0x0A2, 0x25B5, 0x25B5},
	{0x0A3, 0x4039, 0x4039},
};

#define IW7023_ONE_TIME_MAP_SIZE ARRAY_SIZE(onetime_map)

static const struct iwatt_reg_map onetime_map_sky39[] =
{
	/* address, 2D value, 3D value */
	{0x200, 0x0000, 0x0000},
	{0x0A0, 0xE1CF, 0xE1CF},
	// 39"
	{0x0A1, ISET_VALUE_2D_SKY39, ISET_VALUE_3D_SKY39},
	// 42" panal
	//{0x0A1, ISET_VALUE_2D, ISET_VALUE_3D},
	// 50"
	//{0x0A1, 0XB43C, 0XB4B4},
	{0x0A2, 0x25B5, 0x25B5},
	{0x0A3, 0x4039, 0x4039},
};

#define IW7023_ONE_TIME_MAP_SIZE_SKY39 ARRAY_SIZE(onetime_map_sky39)

static const struct iwatt_reg_map onetime_map_sky50[] =
{
	/* address, 2D value, 3D value */
	{0x200, 0x0000, 0x0000},
	{0x0A0, 0xE1CF, 0xE1CF},
	// 39"
	//{0x0A1, ISET_VALUE_2D_SKY39, ISET_VALUE_3D_SKY39},
	// 42" panal
	//{0x0A1, ISET_VALUE_2D, ISET_VALUE_3D},
	// 50"
	{0x0A1, ISET_VALUE_2D_SKY50, ISET_VALUE_3D_SKY50},
	{0x0A2, 0x25B5, 0x25B5},
	{0x0A3, 0x4039, 0x4039},
};

#define IW7023_ONE_TIME_MAP_SIZE_SKY50 ARRAY_SIZE(onetime_map_sky50)

/*
 * bits		field		2D			3D
 *------------------------------------------------------------------------
 * 0x050
 * bit[4:0]	scan_duty	20%			20%
 * bit[7:6]	vsync_cfg	120Hz			120Hz
 * bit[12:8]	pwm_freq	600Hz			600Hz
 * bit[15:14]	tv_mode		NTSC			NTSC
 *
 * 0x051
 * bit[0]	soft_rst	0			0
 * bit[1]	sw_init_byp	0			0
 * bit[5:4]	pwm_bnk_cfg	1			1
 * bit[8]	dis_open_prot	1			1
 * bit[9]	dis_short_prot	1			1
 *
 * 0x052
 * bit[7:0]	vdac_ld		VDAC_VALUE_2D		VDAC_VALUE_3D
 * bit[8]	ld_vdac_ld	1			1
 */
static const struct iwatt_reg_map usermode_map1[4] =
{
	/* address, 2D value, 3D value */
	{0x050, 0x0400, 0x0000},
	{0x051, 0x0010, 0x0010},
	{0x052, 0x0100|VDAC_VALUE_2D_SKY42, 0x0100|VDAC_VALUE_3D_SKY42},
	{0x053, 0x0000, 0x0000},
};

#define IW7023_USER_MODE_MAP1_SIZE ARRAY_SIZE(usermode_map1)

static const struct iwatt_reg_map usermode_map1_sky39[4] =
{
	/* address, 2D value, 3D value */
	{0x050, 0x0400, 0x0000},
	{0x051, 0x0010, 0x0010},
	{0x052, 0x0100|VDAC_VALUE_2D_SKY39, 0x0100|VDAC_VALUE_3D_SKY39},
	{0x053, 0x0000, 0x0000},
};

#define IW7023_USER_MODE_MAP1_SIZE_SKY39 ARRAY_SIZE(usermode_map1_sky39)

static const struct iwatt_reg_map usermode_map1_sky50[4] =
{
	/* address, 2D value, 3D value */
	{0x050, 0x0400, 0x0000},
	{0x051, 0x0010, 0x0010},
	{0x052, 0x0100|VDAC_VALUE_2D_SKY50, 0x0100|VDAC_VALUE_3D_SKY50},
	{0x053, 0x0000, 0x0000},
};

#define IW7023_USER_MODE_MAP1_SIZE_SKY50 ARRAY_SIZE(usermode_map1_sky50)


static const struct iwatt_reg_map usermode_map2[17] =
{
	/* address, 2D value, 3D value */
  	{0x054, 0x0066, 0x0066},	// td0:0.333ms	//4

  	{0x055, 0x00E2, 0x00E2},	// 0.736ms	//5
  	{0x056, 0x01D4, 0x01D4},	// 1.526ms	//6
  	{0x057, 0x02C7, 0x02C7},	// 2.316ms	//7
  	{0x058, 0x03B9, 0x03B9},	// 3.106ms	//8
  	{0x059, 0x04AC, 0x04AC},	// 3.896ms	//9
  	{0x05A, 0x059F, 0x059F},	// 4.686ms	//10
  	{0x05B, 0x0691, 0x0691},	// 5.476ms	//11
  	{0x05C, 0x0784, 0x0784},	// 6.266ms	//12
  	{0x05D, 0x0784, 0x0784},	// 6.266ms	//13
  	{0x05E, 0x0691, 0x0691},	// 5.476ms	//14
  	{0x05F, 0x059F, 0x059F},	// 4.686ms
  	{0x060, 0x04AC, 0x04AC},	// 3.896ms
  	{0x061, 0x03B9, 0x03B9},	// 3.106ms
  	{0x062, 0x02C7, 0x02C7},	// 2.316ms
  	{0x063, 0x01D4, 0x01D4},	// 1.526ms
  	{0x064, 0x00E2, 0x00E2},	// 0.736ms
};

#define IW7023_USER_MODE_MAP2_SIZE ARRAY_SIZE(usermode_map2)

static u16 usermode_value1[IW7023_USER_MODE_MAP1_SIZE];
static u16 usermode_value2[IW7023_USER_MODE_MAP2_SIZE];


//static const IWATT_REGMAP iWattRealTimeRegisterSettingTable_39_6VBLK[] =
static const struct iwatt_reg_map realtime_map[17] =
{
	/* address, 2D value, 3D value */
	{0x000, 0x0000,0x0000},
	/* 2D brightness at 50%, 3D brightness at 20% */
	{0x001, 0X0333, 0x0333},
	{0x002, 0X0333, 0x0333},
	{0x003, 0X0333, 0x0333},
	{0x004, 0X0333, 0x0333},
	{0x005, 0X0333, 0x0333},
	{0x006, 0X0333, 0x0333},
	{0x007, 0X0333, 0x0333},
	{0x008, 0X0333, 0x0333},
	{0x009, 0X0333, 0x0333},
	{0x00A, 0X0333, 0x0333},
	{0x00B, 0X0333, 0x0333},
	{0x00C, 0X0333, 0x0333},
	{0x00D, 0X0333, 0x0333},
	{0x00E, 0X0333, 0x0333},
	{0x00F, 0X0333, 0x0333},
	{0x010, 0X0333, 0x0333},
};

#define IW7023_REAL_TIME_MAP_SIZE ARRAY_SIZE(realtime_map)


#if !defined(CONFIG_AMLOGIC_SPI_HW_MASTER)

/*
 * @name iw7023_read_word
 * @return zero on success, else negative on error.
 */
static int iw7023_read_word(struct spi_device *spi, u16 addr, u16 *val)
{
	u16 command;
	struct spi_message  mesg;
	struct spi_transfer xfer[2];
	int ret;

	memset(xfer, 0, 2*sizeof(struct spi_transfer));

	/* prepare command header */
	command = IW7023_RD | IW7023_ADDR_REP | IW7023_ADDR(addr);
	xfer[0].tx_buf = &command;
	xfer[0].len = 2;

	/* prepare receive data  */
	xfer[1].rx_buf = val;
	xfer[1].len = 2;

	/* spi transfer */
	spi_message_init(&mesg);
	spi_message_add_tail(&xfer[0], &mesg);
	spi_message_add_tail(&xfer[1], &mesg);
	ret = spi_sync(spi, &mesg);

	return ret;
}

#else /* CONFIG_AMLOGIC_SPI_HW_MASTER */

static int iw7023_read_word(struct spi_device *spi, u16 addr, u16 *val)
{
	u16 command;
	u16 buf[2];
	int ret = 0;

	command = IW7023_RD | IW7023_ADDR_REP | IW7023_ADDR(addr);
	buf[0] = command;

	enable_cs(spi);
	udelay(1);
	ret = dirspi_write(spi, (u8 *)buf, 2);
	//udelay(2);
	dirspi_read(spi, (u8 *)&buf[1], 2);
	udelay(1);
	disable_cs(spi);
	*val = buf[1];
	return ret;
}

#endif /* CONFIG_AMLOGIC_SPI_HW_MASTER */


#if !defined(CONFIG_AMLOGIC_SPI_HW_MASTER)

/*
 * @name iw7023_write_word
 * @return zero on success, else negative on error.
 */
static int iw7023_write_word(struct spi_device *spi, u16 addr, u16 data)
{
	u16 command;
	struct spi_message  mesg;
	struct spi_transfer xfer[2];
	int ret;

	memset(&xfer, 0, 2*sizeof(struct spi_transfer));

	/* prepare command header */
	command = IW7023_WR | IW7023_ADDR_REP | IW7023_ADDR(addr);
	xfer[0].tx_buf = &command;
	xfer[0].len = 2;

	/* prepare transfer data */
	xfer[1].tx_buf = &data;
	xfer[1].len = 2;

	/* spi transfer */
	spi_message_init(&mesg);
	spi_message_add_tail(&xfer[0], &mesg);
	spi_message_add_tail(&xfer[1], &mesg);
	ret = spi_sync(spi, &mesg);

	return ret;
}

#else /* CONFIG_AMLOGIC_SPI_HW_MASTER */

static int iw7023_write_word(struct spi_device *spi, u16 addr, u16 data)
{
	u16 command;
	u16 buf[2];
	int ret = 0;

	/* prepare command header */
	command = IW7023_WR | IW7023_ADDR_REP | IW7023_ADDR(addr);
	buf[0] = command;
	buf[1] = data;

	enable_cs(spi);
	udelay(1);
	ret = dirspi_write(spi, (u8 *)buf, 4);
	udelay(ret);
	disable_cs(spi);

	return ret;
}

#endif /* CONFIG_AMLOGIC_SPI_HW_MASTER */


#if !defined(CONFIG_AMLOGIC_SPI_HW_MASTER)

static int iw7023_write_inc(struct spi_device *spi, u16 addr, u16 *data, u32 size)
{
	u16 command;
	struct spi_message  mesg;
	struct spi_transfer xfer[2];
	int ret;

	int i;
	u16 val;

	memset(&xfer, 0, 2*sizeof(struct spi_transfer));

	if (spi_debug) {
		for (i = 0; i < size/2; i++) {
			val = data[i];
			printk("[%2d] -> 0x%04x\n", i, val);
		}

	}

	/* prepare command header */
	command = IW7023_WR | IW7023_ADDR_INC | IW7023_ADDR(addr);
	xfer[0].tx_buf = &command;
	xfer[0].len = 2;

	xfer[1].tx_buf = data;
	xfer[1].len = size;

	/* spi transfer */
	spi_message_init(&mesg);
	spi_message_add_tail(&xfer[0], &mesg);
	spi_message_add_tail(&xfer[1], &mesg);
	ret = spi_sync(spi, &mesg);

	return ret;
}

#else /* CONFIG_AMLOGIC_SPI_HW_MASTER */

static int iw7023_write_inc(struct spi_device *spi, u16 addr, u16 *data, u32 size)
{
	u16 command;
	struct spi_transfer xfer[2];
	int ret;
	u16 buf[3];

	int i;
	u16 val;

	memset(&xfer, 0, 2*sizeof(struct spi_transfer));

	if (spi_debug) {
		for (i = 0; i < size/2; i++) {
			val = data[i];
			printk("[%2d] -> 0x%04x\n", i, val);
		}
	}

	command = IW7023_WR | IW7023_ADDR_INC | IW7023_ADDR(addr);
	buf[0] = command;
	buf[1] = data[0];
	buf[2] = data[1];
	enable_cs(spi);
	udelay(1);
	ret = dirspi_write(spi, (u8 *)buf, 6);
	//udelay(ret);
	ret = dirspi_write(spi, (u8 *)&data[2], size-4);
	//udelay(ret);
	disable_cs(spi);
	return ret;
}

#endif /* CONFIG_AMLOGIC_SPI_HW_MASTER */

static ssize_t iw7023_winc_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t iw7023_winc_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	u16 addr;
	u16 bri[17];
	u16 brightness;
	int i;

	struct spi_device *spi = dev_get_drvdata(dev);

	brightness = simple_strtol(buf, NULL, 16);

	bri[0] = 0x0000;


	for (i = 1; i < ARRAY_SIZE(realtime_map); i++) {

		bri[i] = brightness;
	}

	addr = realtime_map[0].addr;

	printk("addr = 0x%03x, size = %d\n", addr, sizeof(bri));
	iw7023_write_inc(spi, addr, bri, sizeof(bri));

	return count;
}

static DEVICE_ATTR(winc,  S_IWUSR|S_IRUGO, iw7023_winc_show, iw7023_winc_store);


static void iw7023_set_brightness(struct spi_device *spi, u16 bright)
{
	u16 addr;
	u16 bri[17];
	int i;

	bri[0] = 0x0000;

	for (i = 1; i < ARRAY_SIZE(realtime_map); i++) {

		bri[i] = bright;
	}

	if (channel_num == 6) {
		bri[7]  = 0;
		bri[8]  = 0;
		bri[9]  = 0;
		bri[10] = 0;
	}

	if (w_mode_inc) {
		addr = realtime_map[0].addr;
		iw7023_write_inc(spi, addr, bri, sizeof(bri));
	}
	else {
		iw7023_write_word(spi, 0x000, bri[0]);

		for (i = 1; i < ARRAY_SIZE(realtime_map); i++) {
			addr = realtime_map[i].addr;
			iw7023_write_word(spi, addr, bri[i]);
		}
	}
}

static void iw7023_set_brightness_ld(struct spi_device *spi)
{
	u16 addr;
	u16 bri[17];
	int i;

	bri[0] = 0x0000;
	for (i = 1; i < ARRAY_SIZE(realtime_map); i++) {

		bri[i] = get_bri_final(i - 1);
	}

	if (channel_num == 6) {
		bri[7]  = 0;
		bri[8]  = 0;
		bri[9]  = 0;
		bri[10] = 0;
	}

	if (w_mode_inc) {
		addr = realtime_map[0].addr;
		iw7023_write_inc(spi, addr, bri, sizeof(bri));
	}
	else {
		iw7023_write_word(spi, 0x000, bri[0]);

		for (i = 1; i < ARRAY_SIZE(realtime_map); i++) {
			addr = realtime_map[i].addr;
			iw7023_write_word(spi, addr, bri[i]);
		}
	}
}



/*
 * @name iw7023_init_done
 * @return true or false.
 * 	true: init done
 *	false: init not done
 */
static bool iw7023_check_init_done(struct spi_device *spi)
{
	u16 value;
	int ret;
	ret = iw7023_read_word(spi, 0x100, &value);
	return (value & (1 << 4)) ? true : false;
}


/*
 * @td0 us, not ms
 * @dg1 us, not ms
 *
 * example:
 * 0.467ms * 1000us * 1000ns / 407 / 8 = 143 = 0x08F
 */
static void iw7023_set_scan_timing(struct spi_device *spi, u32 td0, u32 dg1, u32 offset)
{
	u32 value;
	u16 addr;
	int i;

	/* calculate td0 and saved to usermode_value array */
	value = td0;
	value = value * 1000 / 407 / 8;
	usermode_value2[0] = (u16)value;

	addr = usermode_map2[0].addr;
	iw7023_write_word(spi, addr, usermode_value2[0]);

	if (channel_num == 6) {
		for (i = 1; i <= 6; i++) {
			value = dg1 + offset * (i - 1);
			value = value * 1000 / 407 / 8;

		//	usermode_value2[7  - i] = (u16)value;
			usermode_value2[i] = (u16)value;
			usermode_value2[17 - i] = (u16)value;

			addr = usermode_map2[i].addr;
			iw7023_write_word(spi, addr, usermode_value2[i]);

			addr = usermode_map2[17 - i].addr;
			iw7023_write_word(spi, addr, usermode_value2[17 -i]);
		}
	}
	else {
		/* calculate dgn(1 - 16) and saved to usermode_value array */
		for (i = 1; i <= 8; i++) {
			value = dg1 + offset * (i - 1);
			value = value * 1000 / 407 / 8;

			usermode_value2[i] = (u16)value;
			usermode_value2[17 - i] = (u16)value;

			addr = usermode_map2[i].addr;
			iw7023_write_word(spi, addr, usermode_value2[i]);

			addr = usermode_map2[17 - i].addr;
			iw7023_write_word(spi, addr, usermode_value2[17 -i]);
		}
	}
}


/*
 * programe "one time" register base 2D mode
 *
 */
static void iw7023_set_onetime_register(struct spi_device *spi)
{
	iw7023_write_word(spi, 0x200, 0x0000);
	/* disable auto-rcv */
	iw7023_write_word(spi, 0x0A0, 0x61CF);
	iw7023_write_word(spi, 0x0A1, iset_value_2d);
	iw7023_write_word(spi, 0x0A2, 0x25B5);
	iw7023_write_word(spi, 0x0A3, 0x4039);
}

static void iw7023_set_usermode1(struct spi_device *spi)
{
	/* programe user mode map 1 */
	iw7023_write_word(spi, 0x050, 0x0400);
	iw7023_write_word(spi, 0x051, 0x0010);
	iw7023_write_word(spi, 0x052, 0x0100 | vdac_value_2d);
	iw7023_write_word(spi, 0x053, 0x0000);
}

static void iw7023_set_usermode2(struct spi_device *spi)
{
	iw7023_set_scan_timing(spi, td0_2d, dg1_2d, delta_2d);
}

/*
 * Program "user mode" registers base 2D mode
 *
 */
static void iw7023_set_usermode_register(struct spi_device *spi)
{
	/* programe user mode map 1 */
	iw7023_set_usermode1(spi);

	/* programe user mode map 2 */
	iw7023_set_usermode2(spi);
}

static void iw7023_release_vdac(struct spi_device *spi)
{
	/* set vdac on the fly */
	iw7023_write_word(spi, 0x052, vdac_value_2d);
}


/*
 * Program "Real Time" brightness registers,frame based "calibratio value"
 */
static void iw7023_set_realtime_register(struct spi_device *spi)
{
	iw7023_set_brightness(spi, brightness_2d);
}

static int iw7023_notify_sys(struct notifier_block *this,
	unsigned long code, void *unused)
{
	//iw7023_turn_off();
	//if (pdata && pdata->power_off)
		//pdata->power_off();

#if defined(CONFIG_IW7023_USE_EEPROM)
	/* read realtime vdac and backup */
	if (mode == MODE_2D)
		iw7023_read_word(spi_dev, 0x106, &vdac_2d_backup);
	if (mode == MODE_3D)
		iw7023_read_word(spi_dev, 0x106, &vdac_3d_backup);

	vdac_2d_backup &= 0x00FF;
	vdac_3d_backup &= 0x00FF;

	/* start work to backup vdac */
	//queue_work(workqueue, &backup_vdac_work);
#endif /* CONFIG_IW7023_USE_EEPROM */

	printk("%s iw7023 turn off\n", __func__);
	return NOTIFY_DONE;
}

static struct notifier_block iw7023_reboot_nb = {
	.notifier_call = iw7023_notify_sys,
};

static bool check_status_enable = true;
module_param(check_status_enable, bool, 0644);

static bool int_src_over_temp = false;
static unsigned int int_src_over_temp_count = 0;

static bool int_src_led_open = false;
static unsigned int int_src_led_open_count = 0;

static bool int_src_led_short = false;
static unsigned int int_src_led_short_count = 0;

static bool int_src_mis_vsync = false;
static unsigned int int_src_mis_vsync_count = 0;

/*
 * 0x0C1
 *
 * bit[0]	ot_int_src
 * bit[1]	led_open_int_src
 * bit[2]	led_short_int_src
 * bit[3]	mis_vsync_int_src
 */
static void iw7023_check_status(struct spi_device *spi)
{
	u16 val;

	if (!check_status_enable)
		return;

	/* read interrupt register */
	iw7023_read_word(spi, 0x0c1, &val);

	/* check OT fault */
	if (val & 1 << 0) {
		int_src_over_temp = true;
		int_src_over_temp_count++;
	}
	else {
		int_src_over_temp = false;
	}

	/* check LED open fault  */
	if (val & 1 << 1) {
		int_src_led_open = true;
		int_src_led_open_count++;
	}
	else {
		int_src_led_open = false;
	}

	/* check LED short fault  */
	if (val & 1 << 1) {
		int_src_led_short = true;
		int_src_led_short_count++;
	}
	else {
		int_src_led_short = false;
	}

	/* check missing vsync fault within 100ms  */
	if (val & 1 << 1) {
		int_src_mis_vsync = true;
		int_src_mis_vsync_count++;
	}
	else {
		int_src_mis_vsync = false;
	}
	/* clear interrupt register */
	if (val & 0x000F)
		iw7023_write_word(spi, 0x0c1, val & 0x000F);
}

static ssize_t iw7023_int_reg_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	u32 len = 0;
	len += sprintf(buf + len, "over_temp %d(%u)\n",
		int_src_over_temp, int_src_over_temp_count);

	len += sprintf(buf + len, "led_open  %d(%u)\n",
		int_src_led_open, int_src_led_open_count);

	len += sprintf(buf + len, "led_short %d(%u)\n",
		int_src_led_short, int_src_led_short_count);

	len += sprintf(buf + len, "mis_vsync %d(%u)\n",
		int_src_mis_vsync, int_src_mis_vsync_count);
	return len;
}

static DEVICE_ATTR(int_reg, S_IWUSR|S_IRUGO, iw7023_int_reg_show, NULL);



static void iw7023_smr(struct spi_device *spi);


static void iw7023_backup_vdac_work(struct work_struct *data)
{
#if defined(CONFIG_IW7023_USE_EEPROM)
	aml_eeprom_write(EEPROM_ADDR_VDAC_2D, (char *)(&vdac_2d_backup), 2);
	mdelay(200);
	aml_eeprom_write(EEPROM_ADDR_VDAC_3D, (char *)(&vdac_3d_backup), 2);
#endif /* CONFIG_IW7023_USE_EEPROM */
}


static unsigned int vsync_delay = 100;
module_param(vsync_delay, uint, 0644);

static bridge_item_t iw7023_vsync_fiq_bridge;

static spinlock_t spi_lock;

static irqreturn_t iw7023_vsync_bridge_isr(int irq, void *dev_id)
{
	spin_lock_irq(&spi_lock);
	if (vsync_delay)
	    udelay(vsync_delay);
	udelay(((td0_2d > td0_3d) ? td0_2d : td0_3d));

	iw7023_smr(spi_dev);
	spin_unlock_irq(&spi_lock);
	return IRQ_HANDLED;
}

/*
 * vsync fiq handler
 */
static void iw7023_vsync_fisr(void)
{
	fiq_bridge_pulse_trigger(&iw7023_vsync_fiq_bridge);
}


static ssize_t iw7023_mode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	u32 len = 0;
	len += sprintf(buf + len, "%d\n", mode);
	return len;
}

static ssize_t iw7023_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	uint val;
	val = simple_strtoul(buf, NULL, 10);

	pr_info("%s current mode = %d\n", __func__, mode);
	pr_info("%s input   mode = %d\n", __func__, val);
	if (val > MODE_3D_TO_2D)
		return count;
	if ((mode == MODE_2D && val == MODE_2D_TO_3D) ||
	    (mode == MODE_3D && val == MODE_3D_TO_2D))
		mode = val;

	return count;
}

static DEVICE_ATTR(mode, S_IWUSR|S_IRUGO, iw7023_mode_show, iw7023_mode_store);

static void iw7023_open_short_prot_en(struct spi_device *spi, bool enable);
static void iw7023_auto_rcv_en(struct spi_device *spi, bool enable);

static void iw7023_init_handle(struct spi_device *spi);
static void iw7023_2d_handle(struct spi_device *spi);
static void iw7023_2d_to_3d_handle(struct spi_device *spi);
static void iw7023_3d_handle(struct spi_device *spi);
static void iw7023_3d_to_2d_handle(struct spi_device *spi);

static u32 vsync_counter_pwm = 4;

static bool smr_switch = true;
module_param(smr_switch, bool, 0644);

static void iw7023_smr(struct spi_device *spi)
{

	if (!smr_switch)
		return;

	switch (mode) {
	case MODE_INIT:
		iw7023_init_handle(spi);
		break;
	case MODE_2D:
		iw7023_2d_handle(spi);
		break;
	case MODE_2D_TO_3D:
		iw7023_2d_to_3d_handle(spi);
		break;
	case MODE_3D:
		iw7023_3d_handle(spi);
		break;
	case MODE_3D_TO_2D:
		iw7023_3d_to_2d_handle(spi);
		break;
	default:
		pr_err("%s invalid mode status %u\n", __func__, mode);
		break;
	}

	/* checking status in every vsync */
	iw7023_check_status(spi);
}


/*
 * @chnl the channels of iw7023.
 * @size the num of channels
 */
void set_bri_for_channels(unsigned short bri[16])
{
	set_luma_hist(bri);
}
EXPORT_SYMBOL(set_bri_for_channels);

/*
 * @iw7023_status the status of iw7023.
 */
int get_iw7023_status(void)
{
	return iw7023_status;
}
EXPORT_SYMBOL(get_iw7023_status);


enum {
	STEP_INIT_NULL = 0,
	STEP_INIT_START,
	STEP_INIT_CHECK_INIT_DONE,
	STEP_INIT_DIS_OPEN_SHORT_PROT,
	STEP_INIT_DIS_AUTO_RCV,
	STEP_INIT_ONE_TIME_REG,
	STEP_INIT_DELAY_VDAC,
	STEP_INIT_USER_MODE_REG,
	STEP_INIT_RELEASE_VDAC,
	STEP_INIT_REAL_TIME_REG,
	STEP_INIT_CHECK_CALI_DONE,
	STEP_INIT_WAIT_EN_PROT,
	STEP_INIT_FINISH,
};

static unsigned int curr_step_init = STEP_INIT_NULL;
module_param(curr_step_init, uint, 0644);
static unsigned int check_init_done_count = 0;
static bool init_done = false;

static unsigned int vdac_delay = 8;

static void iw7023_init_handle(struct spi_device *spi)
{
	switch (curr_step_init) {
	case STEP_INIT_NULL:
		curr_step_init = STEP_INIT_START;
		break;
	case STEP_INIT_START:
		check_init_done_count = 0;
		curr_step_init = STEP_INIT_CHECK_INIT_DONE;
		break;
	case STEP_INIT_CHECK_INIT_DONE:
		if (check_init_done_count < CHECK_INIT_DONE_MAX_COUNT) {
			init_done = iw7023_check_init_done(spi);
			if (init_done) {
				curr_step_init = STEP_INIT_DIS_OPEN_SHORT_PROT;
				pr_info("%s check init done\n", __func__);
				break;
			}
			check_init_done_count++;
		}
		else {
			if (spi_debug)
				pr_err("%s init is not done\n", __func__);
		}
		break;
	case STEP_INIT_DIS_OPEN_SHORT_PROT:
		iw7023_open_short_prot_en(spi, false);
		curr_step_init = STEP_INIT_ONE_TIME_REG;
		break;
	case STEP_INIT_ONE_TIME_REG:
		iw7023_set_onetime_register(spi);
		curr_step_init = STEP_INIT_DELAY_VDAC;
		break;
	case STEP_INIT_DELAY_VDAC:
		if (!vdac_delay--)
			curr_step_init = STEP_INIT_USER_MODE_REG;
		break;
	case STEP_INIT_USER_MODE_REG:
		iw7023_set_usermode_register(spi);
		curr_step_init = STEP_INIT_RELEASE_VDAC;
		break;
	case STEP_INIT_RELEASE_VDAC:
		iw7023_release_vdac(spi);
		curr_step_init = STEP_INIT_REAL_TIME_REG;
		break;
	case STEP_INIT_REAL_TIME_REG:
		iw7023_set_realtime_register(spi);
		curr_step_init = STEP_INIT_CHECK_CALI_DONE;
		break;
	case STEP_INIT_CHECK_CALI_DONE:
		/* check cali done is passed */
		curr_step_init = STEP_INIT_WAIT_EN_PROT;
		break;
	case STEP_INIT_WAIT_EN_PROT:
		iw7023_set_brightness(spi, brightness_2d);
		if (!vsync_counter_pwm--) {
			iw7023_open_short_prot_en(spi, true);
			iw7023_auto_rcv_en(spi, true);
			curr_step_init = STEP_INIT_FINISH;
		}
		break;
	case STEP_INIT_FINISH:
		mode = MODE_2D;
		/* read realtime vdac */
		iw7023_read_word(spi_dev, 0x106, &vdac_2d_backup);
		vdac_2d_backup &= 0x00FF;
		/* start work to backup vdac */
		//queue_work(workqueue, &backup_vdac_work);
		break;
	default:
		pr_err("%s invalid init step %u\n", __func__, curr_step_init);
		break;
	}
}

/*
 * handle for 2d mode
 */
static void iw7023_2d_handle(struct spi_device *spi)
{
	if (ld_enable) {
		/* calculate final brightness */
		lpf_work();
		iw7023_set_brightness_ld(spi);
	}
	else {
		iw7023_set_brightness(spi, brightness_2d);
	}
}


/*
 * handle for 3d mode
 */
static void iw7023_3d_handle(struct spi_device *spi)
{
	/* @todo iw7023_3d_handle */
	if (brightness_3d > BRIGHTNESS_3D_MAX) {
		brightness_3d = BRIGHTNESS_3D_MAX;
	}

	iw7023_set_brightness(spi, brightness_3d);
}

/* 0x0A0
 * bit[15] auto_rcv_en
 */
static void iw7023_auto_rcv_en(struct spi_device *spi, bool enable)
{
	u16 val;

	iw7023_read_word(spi, 0x0A0, &val);

	if (enable)
		val  |= 0x8000;
	else
		val  &= ~0x8000;

	iw7023_write_word(spi, 0x0A0, val);
}


/* 0x051
 * bit[8] dis_open_prot
 * bit[9] dis_open_prot
 */
static void iw7023_open_short_prot_en(struct spi_device *spi, bool enable)
{
	u16 val;

	iw7023_read_word(spi, 0x051, &val);

	if (enable)
		val  &= ~0x0300;
	else
		val  |= 0x0300;

	iw7023_write_word(spi, 0x051, val);
}


static void iw7023_set_iset(struct spi_device *spi, u16 iset)
{
	iw7023_write_word(spi, 0x0A1, iset);
}



/*
 * 0x052
 * bit[8] ld_vdac_ld
 * 0: IC internal vdac for LD mode is adjusted On-the-Fly
 * 1: load 8-set vdac value from vdac_ld for LD mode
 *
 * Note: When this bit is left as a 1, the value of VDAC is frozen
 * at the value that was written to it when it was set to 1.
 * When writing to this bit set to 0, the VDAC becomes read only,
 * and if free to adjust by normal on the fly operations.
 */
static void iw7023_load_vdac_ld(struct spi_device *spi, bool enable)
{
	u16 val;

	iw7023_read_word(spi, 0x052, &val);
	if (enable)
		val |= 0x0100;
	else
		val &= ~0x0100;
	iw7023_write_word(spi, 0x052, val);
}

static void iw7023_set_scan_timing(struct spi_device *spi, u32 td0, u32 dg1, u32 offset);


static ssize_t iw7023_scan_timing_2d_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int len = 0;
	u16 addr;
	u16 val;
	int i;

	len += sprintf(buf + len, "td0_2d:   %d\n", td0_2d);
	len += sprintf(buf + len, "dg1_2d:   %d\n", dg1_2d);
	len += sprintf(buf + len, "delta_2d: %d\n", delta_2d);
	len += sprintf(buf + len, "\n");

	for (i = 0; i < IW7023_USER_MODE_MAP1_SIZE; i++) {
		addr = usermode_map1[i].addr;
		val  = usermode_value1[i];
		len += sprintf(buf + len, "0x%03x: %d(%x)\n", addr, val, val);
	}

	for (i = 0; i < IW7023_USER_MODE_MAP2_SIZE; i++) {
		addr = usermode_map2[i].addr;
		val  = usermode_value2[i];
		len += sprintf(buf + len, "0x%03x: %d(%x)\n", addr, val, val);
	}
	return len;
}

static ssize_t iw7023_scan_timing_2d_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct spi_device *spi = dev_get_drvdata(dev);

	ret = sscanf(buf, "%hu %hu %hu", &td0_2d, &dg1_2d, &delta_2d);
	/* enable writing vdac */
	iw7023_load_vdac_ld(spi, true);
	iw7023_set_scan_timing(spi,td0_2d, dg1_2d, delta_2d);
	return count;
}


static ssize_t iw7023_scan_timing_3d_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int len = 0;
	u16 addr;
	u16 val;
	int i;

	len += sprintf(buf + len, "td0_3d:   %d\n", td0_3d);
	len += sprintf(buf + len, "dg1_3d:   %d\n", dg1_3d);
	len += sprintf(buf + len, "delta_3d: %d\n", delta_3d);
	len += sprintf(buf + len, "\n");

	for (i = 0; i < IW7023_USER_MODE_MAP1_SIZE; i++) {
		addr = usermode_map1[i].addr;
		val  = usermode_value1[i];
		len += sprintf(buf + len, "0x%03x: %d(%x)\n", addr, val, val);
	}

	for (i = 0; i < IW7023_USER_MODE_MAP2_SIZE; i++) {
		addr = usermode_map2[i].addr;
		val  = usermode_value2[i];
		len += sprintf(buf + len, "0x%03x: %d(%x)\n", addr, val, val);
	}
	return len;
}

static ssize_t iw7023_scan_timing_3d_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct spi_device *spi = dev_get_drvdata(dev);

	ret = sscanf(buf, "%hu %hu %hu", &td0_3d, &dg1_3d, &delta_3d);
	/* enable writing vdac */
	iw7023_load_vdac_ld(spi, true);
	iw7023_set_scan_timing(spi,td0_3d, dg1_3d, delta_3d);
	return count;
}

static DEVICE_ATTR(scan_timing_2d,  S_IWUSR|S_IRUGO,
	iw7023_scan_timing_2d_show, iw7023_scan_timing_2d_store);
static DEVICE_ATTR(scan_timing_3d,  S_IWUSR|S_IRUGO,
	iw7023_scan_timing_3d_show, iw7023_scan_timing_3d_store);


enum {
	STEP_2D_TO_3D_START = 0,
	STEP_2D_TO_3D_BACKUP_2D_VDAC,
	STEP_2D_TO_3D_SET_BRI_ZERO,
	STEP_2D_TO_3D_DIS_AUTO_RCV,
	STEP_2D_TO_3D_DIS_OPEN_SHORT_PROT,
	STEP_2D_TO_3D_SET_ISET,
	STEP_2D_TO_3D_UPDATE_PWM_FREQ,
	STEP_2D_TO_3D_SET_BRI_ZERO_CNT,
	STEP_2D_TO_3D_SET_BRI_3D_CNT,
	STEP_2D_TO_3D_WAIT_EN_PROT,
	STEP_2D_TO_3D_FINISHED,
};

static unsigned int curr_step_2d_to_3d = STEP_2D_TO_3D_START;

static u16 vsync_cnt_set_bri_zero	= 0;
static u16 vsync_cnt_set_bri_cnt	= 0;
static u16 vsync_cnt_wait_en_proc	= 0;

enum {
	RAMP_VDAC_NULL = 0,
	RAMP_VDAC_2D_TO_3D,
	RAMP_VDAC_3D_TO_2D,
	RAMP_VDAC_RELEASE,
};

/* ramp vdac */
static u16 ramp_vdac_flag	= RAMP_VDAC_NULL;
static u16 ramp_vdac_step	= 0;
static u16 ramp_vdac_cnt	= 0;
static u8 ramp_curr_vdac	= 0;
static u8 ramp_targ_vdac	= 0;

void ramp_vdac_init(u8 curr_vdac, u8 targ_vdac)
{
	u8 val;
	val = abs(curr_vdac - targ_vdac);
	if (curr_vdac > targ_vdac)
		ramp_vdac_flag = RAMP_VDAC_2D_TO_3D;
	else
		ramp_vdac_flag = RAMP_VDAC_3D_TO_2D;

	ramp_vdac_step = val / VSYNC_CNT_RAMP + 1;
	ramp_vdac_cnt  = val / ramp_vdac_step + 1;
	ramp_curr_vdac = curr_vdac;
	ramp_targ_vdac = targ_vdac;
}


void ramp_vdac_calc(struct spi_device *spi)
{
	switch (ramp_vdac_flag) {
	case RAMP_VDAC_2D_TO_3D:
		ramp_curr_vdac -= ramp_vdac_step;
		if (ramp_curr_vdac > (0xFF - ramp_vdac_step)) {

			ramp_curr_vdac = ramp_targ_vdac;
			ramp_vdac_flag = RAMP_VDAC_RELEASE;
		}
		if (ramp_curr_vdac <= ramp_targ_vdac) {
			ramp_curr_vdac = ramp_targ_vdac;
			ramp_vdac_flag = RAMP_VDAC_RELEASE;
		}

		iw7023_write_word(spi, 0x052, ramp_curr_vdac | 0x0100);
		break;
	case RAMP_VDAC_3D_TO_2D:
		ramp_curr_vdac += ramp_vdac_step;
		if (ramp_curr_vdac < ramp_vdac_step) {
			ramp_curr_vdac = ramp_targ_vdac;
			ramp_vdac_flag = RAMP_VDAC_RELEASE;
		}
		if (ramp_curr_vdac >= ramp_targ_vdac)
		{
			ramp_curr_vdac = ramp_targ_vdac;
			ramp_vdac_flag = RAMP_VDAC_RELEASE;
		}
		iw7023_write_word(spi, 0x052, ramp_curr_vdac | 0x0100);
		break;
	case RAMP_VDAC_RELEASE:
		iw7023_write_word(spi, 0x052, ramp_targ_vdac);
		ramp_vdac_flag = RAMP_VDAC_NULL;
		break;
	default:
		break;
	} /* switch */
}


static void iw7023_2d_to_3d_handle(struct spi_device *spi)
{
	switch (curr_step_2d_to_3d) {
	case STEP_2D_TO_3D_START:
		/* init vsync count */
		vsync_cnt_set_bri_zero	= 0;
		vsync_cnt_set_bri_cnt	= 0;
		vsync_cnt_wait_en_proc	= 0;
		/* changed state */
		curr_step_2d_to_3d = STEP_2D_TO_3D_BACKUP_2D_VDAC;
		break;
	case STEP_2D_TO_3D_BACKUP_2D_VDAC:
		/* read realtime vdac and backup */
		iw7023_read_word(spi, 0x106, &vdac_2d_backup);
		vdac_2d_backup &= 0x00FF;
		ramp_vdac_init(vdac_2d_backup, vdac_3d_backup);
		//queue_work(workqueue, &backup_vdac_work);
		curr_step_2d_to_3d = STEP_2D_TO_3D_SET_BRI_ZERO;
		break;
	case STEP_2D_TO_3D_SET_BRI_ZERO:
		iw7023_set_brightness(spi, 0x0000);
		curr_step_2d_to_3d = STEP_2D_TO_3D_DIS_AUTO_RCV;
		break;
	case STEP_2D_TO_3D_DIS_AUTO_RCV:
		iw7023_auto_rcv_en(spi, false);
		/* changed state */
		curr_step_2d_to_3d = STEP_2D_TO_3D_DIS_OPEN_SHORT_PROT;
		break;
	case STEP_2D_TO_3D_DIS_OPEN_SHORT_PROT:
		iw7023_open_short_prot_en(spi, false);
		/* changed state */
		curr_step_2d_to_3d = STEP_2D_TO_3D_SET_ISET;
		break;
	case STEP_2D_TO_3D_SET_ISET:
		iw7023_set_iset(spi, iset_value_3d);
		/* changed state */
		curr_step_2d_to_3d = STEP_2D_TO_3D_UPDATE_PWM_FREQ;
		break;
	case STEP_2D_TO_3D_UPDATE_PWM_FREQ:
		/* 0x0000:120Hz */
		iw7023_write_word(spi, 0x050, 0x0000);
		curr_step_2d_to_3d = STEP_2D_TO_3D_SET_BRI_ZERO_CNT;
		break;
	case STEP_2D_TO_3D_SET_BRI_ZERO_CNT:
		iw7023_set_brightness(spi, 0x0000);
		ramp_vdac_calc(spi);
		if (++vsync_cnt_set_bri_zero >= vsync_delay_set_bri_zero)
			curr_step_2d_to_3d = STEP_2D_TO_3D_SET_BRI_3D_CNT;
		break;
	case STEP_2D_TO_3D_SET_BRI_3D_CNT:
		iw7023_set_brightness(spi, brightness_3d);
		if (++vsync_cnt_set_bri_cnt >= VSYNC_CNT_SET_BRI_3D)
			curr_step_2d_to_3d = STEP_2D_TO_3D_WAIT_EN_PROT;
		break;
	case STEP_2D_TO_3D_WAIT_EN_PROT:
		if (++vsync_cnt_wait_en_proc >= VSYNC_CNT_WAIT_EN_PROT) {
			iw7023_open_short_prot_en(spi, true);
			iw7023_auto_rcv_en(spi, true);
			curr_step_2d_to_3d = STEP_2D_TO_3D_FINISHED;
		}
		break;
	case STEP_2D_TO_3D_FINISHED:
		/* now set current mode is 3D */
		mode = MODE_3D;
		/* reset to default step */
		curr_step_2d_to_3d = STEP_2D_TO_3D_START;
		/* read realtime vdac */
		iw7023_read_word(spi_dev, 0x106, &vdac_3d_backup);
		vdac_3d_backup &= 0x00FF;
		/* start work to backup vdac */
		//queue_work(workqueue, &backup_vdac_work);
		break;
	}

	return;
}


enum {
	STEP_3D_TO_2D_START = 0,
	STEP_3D_TO_2D_BACKUP_3D_VDAC,
	STEP_3D_TO_2D_SET_BRI_ZERO,
	STEP_3D_TO_2D_DIS_AUTO_RCV,
	STEP_3D_TO_2D_DIS_OPEN_SHORT_PROT,
	STEP_3D_TO_2D_SET_ISET,
	STEP_3D_TO_2D_UPDATE_PWM_FREQ,
	STEP_3D_TO_2D_SET_BRI_ZERO_CNT,
	STEP_3D_TO_2D_SET_BRI_2D_CNT,
	STEP_3D_TO_2D_WAIT_EN_PROT,
	STEP_3D_TO_2D_FINISHED,
};

static unsigned int curr_step_3d_to_2d = STEP_3D_TO_2D_START;

static void iw7023_3d_to_2d_handle(struct spi_device *spi)
{
	switch (curr_step_3d_to_2d) {
	case STEP_3D_TO_2D_START:
		vsync_cnt_set_bri_zero	= 0;
		vsync_cnt_set_bri_cnt	= 0;
		vsync_cnt_wait_en_proc	= 0;
		/* changed state */
		curr_step_3d_to_2d = STEP_3D_TO_2D_BACKUP_3D_VDAC;
		break;
	case STEP_3D_TO_2D_BACKUP_3D_VDAC:
		/* read realtime vdac and backup */
		iw7023_read_word(spi, 0x106, &vdac_3d_backup);
		vdac_3d_backup &= 0x00FF;
		ramp_vdac_init(vdac_3d_backup, vdac_2d_backup);
		//queue_work(workqueue, &backup_vdac_work);
		/* changed state */
		curr_step_3d_to_2d = STEP_3D_TO_2D_SET_BRI_ZERO;
		break;
	case STEP_3D_TO_2D_SET_BRI_ZERO:
		iw7023_set_brightness(spi, 0x0000);
		curr_step_3d_to_2d = STEP_3D_TO_2D_DIS_AUTO_RCV;
		break;
	case STEP_3D_TO_2D_DIS_AUTO_RCV:
		iw7023_auto_rcv_en(spi, false);
		/* changed state */
		curr_step_3d_to_2d = STEP_3D_TO_2D_DIS_OPEN_SHORT_PROT;
		break;
	case STEP_3D_TO_2D_DIS_OPEN_SHORT_PROT:
		iw7023_open_short_prot_en(spi, false);
		/* changed state */
		curr_step_3d_to_2d = STEP_3D_TO_2D_SET_ISET;
		break;
	case STEP_3D_TO_2D_SET_ISET:
		iw7023_set_iset(spi, iset_value_2d);
		/* changed state */
		curr_step_3d_to_2d = STEP_3D_TO_2D_UPDATE_PWM_FREQ;
		break;
	case STEP_3D_TO_2D_UPDATE_PWM_FREQ:
		/* 0x0400:600Hz */
		iw7023_write_word(spi, 0x050, 0x0400);
		curr_step_3d_to_2d = STEP_2D_TO_3D_SET_BRI_ZERO_CNT;
		break;
	case STEP_3D_TO_2D_SET_BRI_ZERO_CNT:
		/* set brightness to zero */
		iw7023_set_brightness(spi, 0x0000);
		ramp_vdac_calc(spi);
		if (++vsync_cnt_set_bri_zero >= vsync_delay_set_bri_zero)
			curr_step_3d_to_2d = STEP_3D_TO_2D_SET_BRI_2D_CNT;
		break;
	case STEP_3D_TO_2D_SET_BRI_2D_CNT:
		iw7023_set_brightness(spi, brightness_2d);
		if (++vsync_cnt_set_bri_cnt >= VSYNC_CNT_SET_BRI_2D)
			curr_step_3d_to_2d = STEP_3D_TO_2D_WAIT_EN_PROT;
		break;
	case STEP_3D_TO_2D_WAIT_EN_PROT:
		if (++vsync_cnt_wait_en_proc >= VSYNC_CNT_WAIT_EN_PROT){
			iw7023_open_short_prot_en(spi, true);
			iw7023_auto_rcv_en(spi, true);
			/* changed state */
			curr_step_3d_to_2d = STEP_3D_TO_2D_FINISHED;
		}
		break;
	case STEP_3D_TO_2D_FINISHED:
		/* now set current mode is 3D */
		mode = MODE_2D;
		/* reset to default step */
		curr_step_3d_to_2d = STEP_3D_TO_2D_START;
		/* read realtime vdac */
		iw7023_read_word(spi_dev, 0x106, &vdac_2d_backup);
		vdac_2d_backup &= 0x00FF;
		/* start work to backup vdac */
		//queue_work(workqueue, &backup_vdac_work);
		break;
	}

	return;
}

static ssize_t iw7023_test_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	u16 addr;
	u16 val;
	struct spi_device *spi = dev_get_drvdata(dev);
	int i;
	int len = 0;

	len += sprintf(buf + len, "one time registers\n");
	for (i = 0; i < ARRAY_SIZE(onetime_map); i++) {
		addr = onetime_map[i].addr;
		iw7023_read_word(spi, addr, &val);
		len += sprintf(buf + len, "0x%03x - > 0x%04x\n", addr, val);
	}
	len += sprintf(buf + len, "\n\n");

	len += sprintf(buf + len, "user mode registers\n");
	for (i = 0; i < ARRAY_SIZE(usermode_map1); i++) {
		addr = usermode_map1[i].addr;
		iw7023_read_word(spi, addr, &val);
		len += sprintf(buf + len, "0x%03x - > 0x%04x\n", addr, val);
	}
	len += sprintf(buf + len, "\n");
	for (i = 0; i < ARRAY_SIZE(usermode_map2); i++) {
		addr = usermode_map2[i].addr;
		iw7023_read_word(spi, addr, &val);
		len += sprintf(buf + len, "0x%03x - > 0x%04x\n", addr, val);
	}
	len += sprintf(buf + len, "\n\n");

	len += sprintf(buf + len, "real time registers\n");
	for (i = 0; i < ARRAY_SIZE(realtime_map); i++) {
		addr = realtime_map[i].addr;
		iw7023_read_word(spi, addr, &val);
		len += sprintf(buf + len, "0x%03x - > 0x%04x\n", addr, val);
	}

	return len;
}

static ssize_t iw7023_test_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	u8 buff[2];
	u16 command;
	u8 data[2];
	int ret;
	u16 addr;
	struct spi_device *spi = dev_get_drvdata(dev);

	addr = simple_strtol(buf, NULL, 16);
	command = IW7023_RD | IW7023_ADDR_REP | IW7023_ADDR(addr);
	buff[0] = (u8)(command & 0x00FF);
	buff[1] = (u8)(command >> 8);

	ret = spi_write_then_read(spi, buff, 2, data, 2);

	printk("0x%03x -> 0x%02x%02x\n", addr, data[1], data[0]);

	return count;
}



static ssize_t iw7023_light_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct spi_device *spi = dev_get_drvdata(dev);

	iw7023_set_onetime_register(spi);
	iw7023_set_usermode_register(spi);
	iw7023_set_realtime_register(spi);

	return count;
}

static ssize_t iw7023_rreg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	u16 val;
	u16 addr = simple_strtol(buf, NULL, 16);
	struct spi_device *spi = dev_get_drvdata(dev);
	int ret;

	ret = iw7023_read_word(spi, addr, &val);
	printk("0x%03x -> 0x%x\n", addr, val);
	return count;
}


static ssize_t iw7023_wreg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	u32 addr;
	u32 val;
	struct spi_device *spi = dev_get_drvdata(dev);
	int ret;

	ret = sscanf(buf, "%x %x", &addr, &val);
	iw7023_write_word(spi, addr, val);
	printk("0x%03x <- 0x%x\n", addr, val);
	return count;
}

static ssize_t iw7023_chns_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	u16 bri;
	u32 len = 0;
	int i;

	for (i = 0; i < 8; i++)	{
		bri = get_luma_hist(i);
		len += sprintf(buf + len, "0x%03x \n", bri);
	}
	for (i = 8; i < 16; i++)	{
		bri = get_luma_hist(i);
		len += sprintf(buf + len, "0x%03x \n", bri);
	}
	return len;
}

static ssize_t iw7023_chns_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	u16 bri[16];
	sscanf(buf, "%hx %hx %hx %hx %hx %hx %hx %hx %hx %hx %hx %hx %hx %hx %hx %hx",
		&bri[0],  &bri[1],  &bri[2],  &bri[3],
		&bri[4],  &bri[5],  &bri[6],  &bri[7],
		&bri[8],  &bri[9],  &bri[10], &bri[11],
		&bri[12], &bri[13], &bri[14], &bri[15]);

	set_luma_hist(bri);

	return count;

}

static ssize_t iw7023_maplookup_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	print_map_lookup();
	return count;
}

static ssize_t iw7023_lumahist_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	print_luma_hist();
	return count;
}

static ssize_t iw7023_britarget_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	print_bri_target();
	return count;
}

static ssize_t iw7023_bricurrent_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	print_bri_current();
	return count;
}

static ssize_t iw7023_brifinal_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	print_bri_final();
	return count;
}


static DEVICE_ATTR(test,  S_IWUSR|S_IRUGO, iw7023_test_show, iw7023_test_store);
static DEVICE_ATTR(light, S_IWUSR|S_IRUGO, NULL, iw7023_light_store);
static DEVICE_ATTR(rreg,  S_IWUSR|S_IRUGO, NULL, iw7023_rreg_store);
static DEVICE_ATTR(wreg,  S_IWUSR|S_IRUGO, NULL, iw7023_wreg_store);

static DEVICE_ATTR(chns, S_IWUSR|S_IRUGO, iw7023_chns_show, iw7023_chns_store);

/* attribute files of low pass filter */
static DEVICE_ATTR(maplookup,  S_IWUSR|S_IRUGO, NULL, iw7023_maplookup_store);
static DEVICE_ATTR(lumahist,   S_IWUSR|S_IRUGO, NULL, iw7023_lumahist_store);
static DEVICE_ATTR(britarget,  S_IWUSR|S_IRUGO, NULL, iw7023_britarget_store);
static DEVICE_ATTR(bricurrent, S_IWUSR|S_IRUGO, NULL, iw7023_bricurrent_store);
static DEVICE_ATTR(brifinal,   S_IWUSR|S_IRUGO, NULL, iw7023_brifinal_store);

/* low pass filter default value */
static unsigned int speed = 4;
static unsigned int bri_min = 0x000; /*  333  20% */
static unsigned int bri_max = 0xFFF; /* 4095 100% */
static unsigned int dimming_rate = 100; /* 100% */

static ssize_t iw7023_speed_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int len = 0;
	len += sprintf(buf + len, "low speed filter speed: %d\n", speed);
	return len;
}

static ssize_t iw7023_speed_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	speed = simple_strtol(buf, NULL, 10);
	set_lpf_speed(speed);
	return count;
}


static ssize_t iw7023_limit_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int len = 0;
	len += sprintf(buf + len, "min = 0x%04x\n", bri_min);
	len += sprintf(buf + len, "max = 0x%04x\n", bri_max);
	return len;
}

static ssize_t iw7023_limit_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%x %x", &bri_min, &bri_max);
	set_user_limit(bri_min, bri_max);
	return count;
}


static ssize_t iw7023_dimrate_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int len = 0;
	len += sprintf(buf + len, "dimming rate = %d\n", dimming_rate);
	return len;
}

static ssize_t iw7023_dimrate_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	dimming_rate = simple_strtol(buf, NULL, 10);
	if (dimming_rate < 0)
		dimming_rate = 0;
	if (dimming_rate > 100)
		dimming_rate = 100;

	set_user_dimrate(dimming_rate);
	return count;
}

static DEVICE_ATTR(speed, S_IWUSR|S_IRUGO, iw7023_speed_show, iw7023_speed_store);
static DEVICE_ATTR(limit, S_IWUSR|S_IRUGO, iw7023_limit_show, iw7023_limit_store);
static DEVICE_ATTR(dimrate, S_IWUSR|S_IRUGO, iw7023_dimrate_show, iw7023_dimrate_store);


static int iw7023_init_for_panel(void)
{
	int ret = -1;
	/* read panel parameter from eeprom */
	//aml_eeprom_read(EEPROM_ADDR_PANEL, &panel, 1);
	printk("%s read panel = %d\n", __func__, panel);

	/* set by kernel parameter, replaced with it*/
	if (panel_setup != -1) {
		panel = panel_setup;
		printk("%s set panel = %d\n", __func__, panel);
	}

#if defined(CONFIG_IW7023_USE_EEPROM)
	/* read backup vdac from eeprom */
	aml_eeprom_read(EEPROM_ADDR_VDAC_2D, (char *)(&vdac_value_2d), 2);
	aml_eeprom_read(EEPROM_ADDR_VDAC_3D, (char *)(&vdac_value_3d), 2);
	vdac_value_2d &= 0x00FF;
	vdac_value_3d &= 0x00FF;
#endif

	switch (panel)
	{
	/* skyworth 39" */
	case 3:
		/* init channel num */
		channel_num = 6;
		/* init iset & vdac */
		iset_value_2d = ISET_VALUE_2D_SKY39;
		iset_value_3d = ISET_VALUE_3D_SKY39;

		if (vdac_value_2d < VDAC_MIN_2D_SKY39 ||
		    vdac_value_2d > VDAC_MAX_2D_SKY39)
			vdac_value_2d = VDAC_VALUE_2D_SKY39;

		if (vdac_value_3d < VDAC_MIN_3D_SKY39 ||
		    vdac_value_3d > VDAC_MAX_3D_SKY39)
			vdac_value_3d = VDAC_VALUE_3D_SKY39;

		/* init scan timming parameters */
		td0_2d   = DEFAULT_TD0_2D_SKY39;
		dg1_2d   = DEFAULT_DG1_2D_SKY39;
		delta_2d = DEFAULT_DELTAT_2D_SKY39;

		td0_3d	 = DEFAULT_TD0_3D_SKY39;
		dg1_3d	 = DEFAULT_DG1_3D_SKY39;
		delta_3d = DEFAULT_DELTAT_3D_SKY39;
		ret = 0;
		break;
	/* skyworth 42" */
	case 4:
		/* init channel num */
		channel_num = 8;
		/* init iset & vdac */
		iset_value_2d = ISET_VALUE_2D_SKY42;
		iset_value_3d = ISET_VALUE_3D_SKY42;

		if (vdac_value_2d < VDAC_MIN_2D_SKY42 ||
		    vdac_value_2d > VDAC_MAX_2D_SKY42)
			vdac_value_2d = VDAC_VALUE_2D_SKY42;

		if (vdac_value_3d < VDAC_MIN_3D_SKY42 ||
		    vdac_value_3d > VDAC_MAX_3D_SKY42)
			vdac_value_3d = VDAC_VALUE_3D_SKY42;
		ret = 0;
		break;
	/* skyworth 50" */
	case 5:
		/* init channel num */
		channel_num = 8;
		/* init iset & vdac */
		iset_value_2d = ISET_VALUE_2D_SKY50;
		iset_value_3d = ISET_VALUE_3D_SKY50;

		if (vdac_value_2d < VDAC_MIN_2D_SKY50 ||
		    vdac_value_2d > VDAC_MAX_2D_SKY50)
			vdac_value_2d = VDAC_VALUE_2D_SKY50;

		if (vdac_value_3d < VDAC_MIN_3D_SKY50 ||
		    vdac_value_3d > VDAC_MAX_3D_SKY50)
			vdac_value_3d = VDAC_VALUE_3D_SKY50;

		/* init scan timming parameters */
		td0_2d	 = DEFAULT_TD0_2D_SKY50;
		dg1_2d	 = DEFAULT_DG1_2D_SKY50;
		delta_2d = DEFAULT_DELTAT_2D_SKY50;

		td0_3d	 = DEFAULT_TD0_3D_SKY50;
		dg1_3d	 = DEFAULT_DG1_3D_SKY50;
		delta_3d = DEFAULT_DELTAT_3D_SKY50;
		ret = 0;
		break;
	/* chimei 39" */
	case 6:
		break;
	/* chimei 42" */
	case 7:
		break;
	/* chimei 50" */
	case 8:
		break;
	default:
		break;
	}

	vdac_2d_backup = vdac_value_2d;
	vdac_3d_backup = vdac_value_3d;

	return ret;
}


static int __devinit iw7023_probe(struct spi_device *spi)
{
	int ret;

	printk ("%s probe start\n", __func__);

	pdata = spi->dev.platform_data;
	if (!pdata) {
		pr_err("%s no platform data\n", __func__);
		return -ENODEV;
	}

	/* init panel parameters */
	if (iw7023_init_for_panel() < 0) {
		pr_err("%s panel id is not correct.\n", __func__);
		return -ENODEV;
	}

	spin_lock_init(&spi_lock);
	dev_set_drvdata(&spi->dev, spi);

	/* register reboot nb */
	ret = register_reboot_notifier(&iw7023_reboot_nb);
	if (ret != 0) {
		pr_err("%s failed to register reboot notifier (%d)\n", __func__, ret);
		return ret;
	}

	spi_dev = spi;

	ret = device_create_file(&spi->dev, &dev_attr_winc);

	ret = device_create_file(&spi->dev, &dev_attr_mode);
	/* create sysfs files */
	ret = device_create_file(&spi->dev, &dev_attr_test);
	ret = device_create_file(&spi->dev, &dev_attr_light);
	ret = device_create_file(&spi->dev, &dev_attr_rreg);
	ret = device_create_file(&spi->dev, &dev_attr_wreg);
	ret = device_create_file(&spi->dev, &dev_attr_chns);

	/* attribute files of low pass filter */
	ret = device_create_file(&spi->dev, &dev_attr_maplookup);
	ret = device_create_file(&spi->dev, &dev_attr_lumahist);
	ret = device_create_file(&spi->dev, &dev_attr_britarget);
	ret = device_create_file(&spi->dev, &dev_attr_bricurrent);
	ret = device_create_file(&spi->dev, &dev_attr_brifinal);

	ret = device_create_file(&spi->dev, &dev_attr_speed);
	ret = device_create_file(&spi->dev, &dev_attr_limit);
	ret = device_create_file(&spi->dev, &dev_attr_dimrate);
	/* scan timing file */
	ret = device_create_file(&spi->dev, &dev_attr_scan_timing_2d);
	ret = device_create_file(&spi->dev, &dev_attr_scan_timing_3d);

	/* interrupt register file */
	ret = device_create_file(&spi->dev, &dev_attr_int_reg);

	/* setup spi device */
	spi->bits_per_word = 16;
	spi->mode = SPI_MODE_0;
	ret = spi_setup(spi);

	#if 0
	/* init spi master */
	aml_spi_init(spi);
	#endif

	/* GPIOX-32 */
	//CLEAR_CBUS_REG_MASK(PREG_FGPIO_EN_N, 1 << 0);
	/* turn on iw7023 */
	//iw7023_turn_on();

	/* power controled by lcd driver */
	#if 0
	if (pdata && pdata->power_on)
		pdata->power_on();
	#endif

	/* init local dimming low pass filter */
	set_lpf_speed(speed);
	set_user_limit(bri_min, bri_max);
	set_user_dimrate(dimming_rate);
	lpf_init();


	/* init workqueue */
	workqueue = create_singlethread_workqueue("iw7023");
	INIT_WORK(&backup_vdac_work, iw7023_backup_vdac_work);

	/* register vsync handler */
	iw7023_vsync_fiq_bridge.handle = iw7023_vsync_bridge_isr;
	iw7023_vsync_fiq_bridge.key = (u32)iw7023_vsync_bridge_isr;
	iw7023_vsync_fiq_bridge.name = "iw7023_vsync_fiq_bridge";

	register_fiq_bridge_handle(&iw7023_vsync_fiq_bridge);
	request_fiq(INT_VIU_VSYNC, &iw7023_vsync_fisr);

	iw7023_status = 1;
	return 0;
}

static int __devexit iw7023_remove(struct spi_device *spi)
{
	/* power controled by lcd driver */
	#if 0
	if (pdata && pdata->power_off)
		pdata->power_off();
	else
		printk("%s, pdata->power_off null!\n", __func__);
	#endif


	/* unregister vsync handler */
	free_fiq(INT_VIU_VSYNC, &iw7023_vsync_fisr);
	unregister_fiq_bridge_handle(&iw7023_vsync_fiq_bridge);
	destroy_workqueue(workqueue);
	unregister_reboot_notifier(&iw7023_reboot_nb);

	device_remove_file(&spi->dev, &dev_attr_winc);

	device_remove_file(&spi->dev, &dev_attr_mode);
	/* remvoe sysfs files */
	device_remove_file(&spi->dev, &dev_attr_test);
	device_remove_file(&spi->dev, &dev_attr_light);
	device_remove_file(&spi->dev, &dev_attr_rreg);
	device_remove_file(&spi->dev, &dev_attr_wreg);
	device_remove_file(&spi->dev, &dev_attr_chns);

	/* attribute files of low pass filter */
	device_remove_file(&spi->dev, &dev_attr_maplookup);
	device_remove_file(&spi->dev, &dev_attr_lumahist);
	device_remove_file(&spi->dev, &dev_attr_britarget);
	device_remove_file(&spi->dev, &dev_attr_bricurrent);
	device_remove_file(&spi->dev, &dev_attr_brifinal);

	device_remove_file(&spi->dev, &dev_attr_speed);
	device_remove_file(&spi->dev, &dev_attr_limit);
	device_remove_file(&spi->dev, &dev_attr_dimrate);
	/* scan timing file */
	device_remove_file(&spi->dev, &dev_attr_scan_timing_2d);
	device_remove_file(&spi->dev, &dev_attr_scan_timing_3d);

	/* interrupt register file */
	device_remove_file(&spi->dev, &dev_attr_int_reg);

	return 0;
}

static int iw7023_suspend(struct spi_device *spi, pm_message_t mesg)
{
	/* @todo iw7023_suspend */
	return 0;
}

static int iw7023_resume(struct spi_device *spi)
{
	/* @todo iw7023_resume */
	return 0;
}

static struct spi_driver iw7023_driver = {
	.driver = {
		.name	= "iw7023",
		.owner	= THIS_MODULE,
	},
	.probe		= iw7023_probe,
	.remove		= __devexit_p(iw7023_remove),
	.suspend	= iw7023_suspend,
	.resume		= iw7023_resume,
};

static int __init iw7023_init(void)
{
	printk("%s start\n", __func__);
	return spi_register_driver(&iw7023_driver);
}

static void __exit iw7023_exit(void)
{
	spi_unregister_driver(&iw7023_driver);
}

module_init(iw7023_init);
module_exit(iw7023_exit);

/*
 * ld2d_param=<bri>,<iset>,<vdac>
 *
 */
static int __init iw7023_ld2d_param_setup (char *str)
{
	char *token;

	token = strsep(&str, ",");
	if (token != NULL) {
		brightness_2d = simple_strtoul(token, NULL, 0);
		printk("ld2d_param: bri_2d    = 0x%04x\n", brightness_2d);
	}

	token = strsep(&str, ",");
	if (token != NULL) {
		iset_value_2d = simple_strtoul(token, NULL, 0);
		printk("ld2d_param: iset_2d   = 0x%04x\n", iset_value_2d);
	}

	token = strsep(&str, ",");
	if (token != NULL) {
		vdac_value_2d = simple_strtoul(token, NULL, 0);
		printk("ld2d_param: vdac_2d   = 0x%04x\n", vdac_value_2d);
	}

	return 1;
}

__setup ("ld2d_param=", iw7023_ld2d_param_setup);

static int __init iw7023_ld3d_param_setup (char *str)
{
	char *token;

	token = strsep(&str, ",");
	if (token != NULL) {
		brightness_3d = simple_strtoul(token, NULL, 0);
		printk("ld3d_param: bri_3d    = 0x%04x\n", brightness_3d);
	}

	token = strsep(&str, ",");
	if (token != NULL) {
		iset_value_3d = simple_strtoul(token, NULL, 0);
		printk("ld3d_param: iset_3d   = 0x%04x\n", iset_value_3d);
	}

	token = strsep(&str, ",");
	if (token != NULL) {
		vdac_value_3d = simple_strtoul(token, NULL, 0);
		printk("ld3d_param: vdac_3d   = 0x%04x\n", vdac_value_3d);
	}

	return 1;
}

__setup ("ld3d_param=", iw7023_ld3d_param_setup);


static int __init iw7023_ld2d_scant_setup (char *str)
{
        /* bri, iset, vdac */
	int ints[4];

	str = get_options(str, ARRAY_SIZE(ints), ints);

	switch (ints[0]) {
	case 3:
		delta_2d = ints[3];
		printk("ld2d_scant: delta_2d = %d\n", delta_2d);
	case 2:
		dg1_2d = ints[2];
		printk("ld2d_scant: dg1_2d   = %d\n", dg1_2d);
	case 1:
		td0_2d = ints[1];
		printk("ld2d_scant: td0_2d   = %d\n", td0_2d);
	case 0:
		break;
	default:
		printk("ld2d_scant: invalid number of arguments\n");
		return 0;
	}
	return 1;
}

__setup ("ld2d_scant=", iw7023_ld2d_scant_setup);


static int __init iw7023_ld3d_scant_setup (char *str)
{
        /* bri, iset, vdac */
	int ints[4];

	str = get_options(str, ARRAY_SIZE(ints), ints);

	switch (ints[0]) {
	case 3:
		delta_3d = ints[3];
		printk("ld3d_scant: delta_3d = %d\n", delta_3d);
	case 2:
		dg1_3d = ints[2];
		printk("ld3d_scant: dg1_3d   = %d\n", dg1_3d);
	case 1:
		td0_3d = ints[1];
		printk("ld3d_scant: td0_3d   = %d\n", td0_3d);
	case 0:
		break;
	default:
		printk("ld3d_scant: invalid number of arguments\n");
		return 0;
	}
	return 1;
}

__setup ("ld3d_scant=", iw7023_ld3d_scant_setup);


static int __init iw7023_panel_setup (char *str)
{
	panel_setup = simple_strtol(str, NULL, 10);
	return 1;
}

__setup ("panel=", iw7023_panel_setup);


MODULE_DESCRIPTION("iW7023 LED Driver for LCD Panel Backlight");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bobby Yang <bo.yang@amlogic.com>");

