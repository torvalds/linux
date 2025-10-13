// SPDX-License-Identifier: GPL-2.0
/*
 * Linux driver for digital TV devices equipped with B2C2 FlexcopII(b)/III
 * flexcop-sram.c - functions for controlling the SRAM
 * see flexcop.c for copyright information
 */
#include "flexcop.h"

static void flexcop_sram_set_chip(struct flexcop_device *fc,
		flexcop_sram_type_t type)
{
	flexcop_set_ibi_value(wan_ctrl_reg_71c, sram_chip, type);
}

int flexcop_sram_init(struct flexcop_device *fc)
{
	switch (fc->rev) {
	case FLEXCOP_II:
	case FLEXCOP_IIB:
		flexcop_sram_set_chip(fc, FC_SRAM_1_32KB);
		break;
	case FLEXCOP_III:
		flexcop_sram_set_chip(fc, FC_SRAM_1_48KB);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int flexcop_sram_set_dest(struct flexcop_device *fc, flexcop_sram_dest_t dest,
		 flexcop_sram_dest_target_t target)
{
	flexcop_ibi_value v;
	v = fc->read_ibi_reg(fc, sram_dest_reg_714);

	if (fc->rev != FLEXCOP_III && target == FC_SRAM_DEST_TARGET_FC3_CA) {
		err("SRAM destination target to available on FlexCopII(b)\n");
		return -EINVAL;
	}
	deb_sram("sram dest: %x target: %x\n", dest, target);

	if (dest & FC_SRAM_DEST_NET)
		v.sram_dest_reg_714.NET_Dest = target;
	if (dest & FC_SRAM_DEST_CAI)
		v.sram_dest_reg_714.CAI_Dest = target;
	if (dest & FC_SRAM_DEST_CAO)
		v.sram_dest_reg_714.CAO_Dest = target;
	if (dest & FC_SRAM_DEST_MEDIA)
		v.sram_dest_reg_714.MEDIA_Dest = target;

	fc->write_ibi_reg(fc,sram_dest_reg_714,v);
	udelay(1000); /* TODO delay really necessary */

	return 0;
}
EXPORT_SYMBOL(flexcop_sram_set_dest);

void flexcop_wan_set_speed(struct flexcop_device *fc, flexcop_wan_speed_t s)
{
	flexcop_set_ibi_value(wan_ctrl_reg_71c,wan_speed_sig,s);
}
EXPORT_SYMBOL(flexcop_wan_set_speed);

void flexcop_sram_ctrl(struct flexcop_device *fc, int usb_wan, int sramdma, int maximumfill)
{
	flexcop_ibi_value v = fc->read_ibi_reg(fc,sram_dest_reg_714);
	v.sram_dest_reg_714.ctrl_usb_wan = usb_wan;
	v.sram_dest_reg_714.ctrl_sramdma = sramdma;
	v.sram_dest_reg_714.ctrl_maximumfill = maximumfill;
	fc->write_ibi_reg(fc,sram_dest_reg_714,v);
}
EXPORT_SYMBOL(flexcop_sram_ctrl);

#if 0
static void flexcop_sram_write(struct adapter *adapter, u32 bank, u32 addr, u8 *buf, u32 len)
{
	int i, retries;
	u32 command;

	for (i = 0; i < len; i++) {
		command = bank | addr | 0x04000000 | (*buf << 0x10);

		retries = 2;

		while (((read_reg_dw(adapter, 0x700) & 0x80000000) != 0) && (retries > 0)) {
			mdelay(1);
			retries--;
		}

		if (retries == 0)
			printk("%s: SRAM timeout\n", __func__);

		write_reg_dw(adapter, 0x700, command);

		buf++;
		addr++;
	}
}

static void flex_sram_read(struct adapter *adapter, u32 bank, u32 addr, u8 *buf, u32 len)
{
	int i, retries;
	u32 command, value;

	for (i = 0; i < len; i++) {
		command = bank | addr | 0x04008000;

		retries = 10000;

		while (((read_reg_dw(adapter, 0x700) & 0x80000000) != 0) && (retries > 0)) {
			mdelay(1);
			retries--;
		}

		if (retries == 0)
			printk("%s: SRAM timeout\n", __func__);

		write_reg_dw(adapter, 0x700, command);

		retries = 10000;

		while (((read_reg_dw(adapter, 0x700) & 0x80000000) != 0) && (retries > 0)) {
			mdelay(1);
			retries--;
		}

		if (retries == 0)
			printk("%s: SRAM timeout\n", __func__);

		value = read_reg_dw(adapter, 0x700) >> 0x10;

		*buf = (value & 0xff);

		addr++;
		buf++;
	}
}

static void sram_write_chunk(struct adapter *adapter, u32 addr, u8 *buf, u16 len)
{
	u32 bank;

	bank = 0;

	if (adapter->dw_sram_type == 0x20000) {
		bank = (addr & 0x18000) << 0x0d;
	}

	if (adapter->dw_sram_type == 0x00000) {
		if ((addr >> 0x0f) == 0)
			bank = 0x20000000;
		else
			bank = 0x10000000;
	}
	flex_sram_write(adapter, bank, addr & 0x7fff, buf, len);
}

static void sram_read_chunk(struct adapter *adapter, u32 addr, u8 *buf, u16 len)
{
	u32 bank;
	bank = 0;

	if (adapter->dw_sram_type == 0x20000) {
		bank = (addr & 0x18000) << 0x0d;
	}

	if (adapter->dw_sram_type == 0x00000) {
		if ((addr >> 0x0f) == 0)
			bank = 0x20000000;
		else
			bank = 0x10000000;
	}
	flex_sram_read(adapter, bank, addr & 0x7fff, buf, len);
}

static void sram_read(struct adapter *adapter, u32 addr, u8 *buf, u32 len)
{
	u32 length;
	while (len != 0) {
		length = len;
		/* check if the address range belongs to the same
		 * 32K memory chip. If not, the data is read
		 * from one chip at a time */
		if ((addr >> 0x0f) != ((addr + len - 1) >> 0x0f)) {
			length = (((addr >> 0x0f) + 1) << 0x0f) - addr;
		}

		sram_read_chunk(adapter, addr, buf, length);
		addr = addr + length;
		buf = buf + length;
		len = len - length;
	}
}

static void sram_write(struct adapter *adapter, u32 addr, u8 *buf, u32 len)
{
	u32 length;
	while (len != 0) {
		length = len;

		/* check if the address range belongs to the same
		 * 32K memory chip. If not, the data is
		 * written to one chip at a time */
		if ((addr >> 0x0f) != ((addr + len - 1) >> 0x0f)) {
			length = (((addr >> 0x0f) + 1) << 0x0f) - addr;
		}

		sram_write_chunk(adapter, addr, buf, length);
		addr = addr + length;
		buf = buf + length;
		len = len - length;
	}
}

static void sram_set_size(struct adapter *adapter, u32 mask)
{
	write_reg_dw(adapter, 0x71c,
			(mask | (~0x30000 & read_reg_dw(adapter, 0x71c))));
}

static void sram_init(struct adapter *adapter)
{
	u32 tmp;
	tmp = read_reg_dw(adapter, 0x71c);
	write_reg_dw(adapter, 0x71c, 1);

	if (read_reg_dw(adapter, 0x71c) != 0) {
		write_reg_dw(adapter, 0x71c, tmp);
		adapter->dw_sram_type = tmp & 0x30000;
		ddprintk("%s: dw_sram_type = %x\n", __func__, adapter->dw_sram_type);
	} else {
		adapter->dw_sram_type = 0x10000;
		ddprintk("%s: dw_sram_type = %x\n", __func__, adapter->dw_sram_type);
	}
}

static int sram_test_location(struct adapter *adapter, u32 mask, u32 addr)
{
	u8 tmp1, tmp2;
	dprintk("%s: mask = %x, addr = %x\n", __func__, mask, addr);

	sram_set_size(adapter, mask);
	sram_init(adapter);

	tmp2 = 0xa5;
	tmp1 = 0x4f;

	sram_write(adapter, addr, &tmp2, 1);
	sram_write(adapter, addr + 4, &tmp1, 1);

	tmp2 = 0;
	mdelay(20);

	sram_read(adapter, addr, &tmp2, 1);
	sram_read(adapter, addr, &tmp2, 1);

	dprintk("%s: wrote 0xa5, read 0x%2x\n", __func__, tmp2);

	if (tmp2 != 0xa5)
		return 0;

	tmp2 = 0x5a;
	tmp1 = 0xf4;

	sram_write(adapter, addr, &tmp2, 1);
	sram_write(adapter, addr + 4, &tmp1, 1);

	tmp2 = 0;
	mdelay(20);

	sram_read(adapter, addr, &tmp2, 1);
	sram_read(adapter, addr, &tmp2, 1);

	dprintk("%s: wrote 0x5a, read 0x%2x\n", __func__, tmp2);

	if (tmp2 != 0x5a)
		return 0;
	return 1;
}

static u32 sram_length(struct adapter *adapter)
{
	if (adapter->dw_sram_type == 0x10000)
		return 32768; /* 32K */
	if (adapter->dw_sram_type == 0x00000)
		return 65536; /* 64K */
	if (adapter->dw_sram_type == 0x20000)
		return 131072; /* 128K */
	return 32768; /* 32K */
}

/* FlexcopII can work with 32K, 64K or 128K of external SRAM memory.
   - for 128K there are 4x32K chips at bank 0,1,2,3.
   - for  64K there are 2x32K chips at bank 1,2.
   - for  32K there is one 32K chip at bank 0.

   FlexCop works only with one bank at a time. The bank is selected
   by bits 28-29 of the 0x700 register.

   bank 0 covers addresses 0x00000-0x07fff
   bank 1 covers addresses 0x08000-0x0ffff
   bank 2 covers addresses 0x10000-0x17fff
   bank 3 covers addresses 0x18000-0x1ffff */

static int flexcop_sram_detect(struct flexcop_device *fc)
{
	flexcop_ibi_value r208, r71c_0, vr71c_1;
	r208 = fc->read_ibi_reg(fc, ctrl_208);
	fc->write_ibi_reg(fc, ctrl_208, ibi_zero);

	r71c_0 = fc->read_ibi_reg(fc, wan_ctrl_reg_71c);
	write_reg_dw(adapter, 0x71c, 1);
	tmp3 = read_reg_dw(adapter, 0x71c);
	dprintk("%s: tmp3 = %x\n", __func__, tmp3);
	write_reg_dw(adapter, 0x71c, tmp2);

	// check for internal SRAM ???
	tmp3--;
	if (tmp3 != 0) {
		sram_set_size(adapter, 0x10000);
		sram_init(adapter);
		write_reg_dw(adapter, 0x208, tmp);
		dprintk("%s: sram size = 32K\n", __func__);
		return 32;
	}

	if (sram_test_location(adapter, 0x20000, 0x18000) != 0) {
		sram_set_size(adapter, 0x20000);
		sram_init(adapter);
		write_reg_dw(adapter, 0x208, tmp);
		dprintk("%s: sram size = 128K\n", __func__);
		return 128;
	}

	if (sram_test_location(adapter, 0x00000, 0x10000) != 0) {
		sram_set_size(adapter, 0x00000);
		sram_init(adapter);
		write_reg_dw(adapter, 0x208, tmp);
		dprintk("%s: sram size = 64K\n", __func__);
		return 64;
	}

	if (sram_test_location(adapter, 0x10000, 0x00000) != 0) {
		sram_set_size(adapter, 0x10000);
		sram_init(adapter);
		write_reg_dw(adapter, 0x208, tmp);
		dprintk("%s: sram size = 32K\n", __func__);
		return 32;
	}

	sram_set_size(adapter, 0x10000);
	sram_init(adapter);
	write_reg_dw(adapter, 0x208, tmp);
	dprintk("%s: SRAM detection failed. Set to 32K\n", __func__);
	return 0;
}

static void sll_detect_sram_size(struct adapter *adapter)
{
	sram_detect_for_flex2(adapter);
}

#endif
