/*
 *  cx18 firmware functions
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include "cx18-driver.h"
#include "cx18-scb.h"
#include "cx18-irq.h"
#include "cx18-firmware.h"
#include "cx18-cards.h"
#include <linux/firmware.h>

#define CX18_PROC_SOFT_RESET 		0xc70010
#define CX18_DDR_SOFT_RESET          	0xc70014
#define CX18_CLOCK_SELECT1           	0xc71000
#define CX18_CLOCK_SELECT2           	0xc71004
#define CX18_HALF_CLOCK_SELECT1      	0xc71008
#define CX18_HALF_CLOCK_SELECT2      	0xc7100C
#define CX18_CLOCK_POLARITY1         	0xc71010
#define CX18_CLOCK_POLARITY2         	0xc71014
#define CX18_ADD_DELAY_ENABLE1       	0xc71018
#define CX18_ADD_DELAY_ENABLE2       	0xc7101C
#define CX18_CLOCK_ENABLE1           	0xc71020
#define CX18_CLOCK_ENABLE2           	0xc71024

#define CX18_REG_BUS_TIMEOUT_EN      	0xc72024

#define CX18_AUDIO_ENABLE            	0xc72014
#define CX18_REG_BUS_TIMEOUT_EN      	0xc72024

#define CX18_FAST_CLOCK_PLL_INT      	0xc78000
#define CX18_FAST_CLOCK_PLL_FRAC     	0xc78004
#define CX18_FAST_CLOCK_PLL_POST     	0xc78008
#define CX18_FAST_CLOCK_PLL_PRESCALE 	0xc7800C
#define CX18_FAST_CLOCK_PLL_ADJUST_BANDWIDTH 0xc78010

#define CX18_SLOW_CLOCK_PLL_INT      	0xc78014
#define CX18_SLOW_CLOCK_PLL_FRAC     	0xc78018
#define CX18_SLOW_CLOCK_PLL_POST     	0xc7801C
#define CX18_MPEG_CLOCK_PLL_INT		0xc78040
#define CX18_MPEG_CLOCK_PLL_FRAC	0xc78044
#define CX18_MPEG_CLOCK_PLL_POST	0xc78048
#define CX18_PLL_POWER_DOWN          	0xc78088
#define CX18_SW1_INT_STATUS             0xc73104
#define CX18_SW1_INT_ENABLE_PCI         0xc7311C
#define CX18_SW2_INT_SET                0xc73140
#define CX18_SW2_INT_STATUS             0xc73144
#define CX18_ADEC_CONTROL            	0xc78120

#define CX18_DDR_REQUEST_ENABLE      	0xc80000
#define CX18_DDR_CHIP_CONFIG         	0xc80004
#define CX18_DDR_REFRESH            	0xc80008
#define CX18_DDR_TIMING1             	0xc8000C
#define CX18_DDR_TIMING2             	0xc80010
#define CX18_DDR_POWER_REG		0xc8001C

#define CX18_DDR_TUNE_LANE           	0xc80048
#define CX18_DDR_INITIAL_EMRS        	0xc80054
#define CX18_DDR_MB_PER_ROW_7        	0xc8009C
#define CX18_DDR_BASE_63_ADDR        	0xc804FC

#define CX18_WMB_CLIENT02            	0xc90108
#define CX18_WMB_CLIENT05            	0xc90114
#define CX18_WMB_CLIENT06            	0xc90118
#define CX18_WMB_CLIENT07            	0xc9011C
#define CX18_WMB_CLIENT08            	0xc90120
#define CX18_WMB_CLIENT09            	0xc90124
#define CX18_WMB_CLIENT10            	0xc90128
#define CX18_WMB_CLIENT11            	0xc9012C
#define CX18_WMB_CLIENT12            	0xc90130
#define CX18_WMB_CLIENT13            	0xc90134
#define CX18_WMB_CLIENT14            	0xc90138

#define CX18_DSP0_INTERRUPT_MASK     	0xd0004C

/* Encoder/decoder firmware sizes */
#define CX18_FW_CPU_SIZE 		(174716)
#define CX18_FW_APU_SIZE 		(141200)

#define APU_ROM_SYNC1 0x6D676553 /* "mgeS" */
#define APU_ROM_SYNC2 0x72646548 /* "rdeH" */

struct cx18_apu_rom_seghdr {
	u32 sync1;
	u32 sync2;
	u32 addr;
	u32 size;
};

static int load_cpu_fw_direct(const char *fn, u8 __iomem *mem, struct cx18 *cx, long size)
{
	const struct firmware *fw = NULL;
	int retries = 3;
	int i, j;
	u32 __iomem *dst = (u32 __iomem *)mem;
	const u32 *src;

retry:
	if (!retries || request_firmware(&fw, fn, &cx->dev->dev)) {
		CX18_ERR("Unable to open firmware %s (must be %ld bytes)\n",
				fn, size);
		CX18_ERR("Did you put the firmware in the hotplug firmware directory?\n");
		return -ENOMEM;
	}

	src = (const u32 *)fw->data;

	if (fw->size != size) {
		/* Due to race conditions in firmware loading (esp. with
		   udev <0.95) the wrong file was sometimes loaded. So we check
		   filesizes to see if at least the right-sized file was
		   loaded. If not, then we retry. */
		CX18_INFO("retry: file loaded was not %s (expected size %ld, got %zd)\n",
				fn, size, fw->size);
		release_firmware(fw);
		retries--;
		goto retry;
	}
	for (i = 0; i < fw->size; i += 4096) {
		setup_page(i);
		for (j = i; j < fw->size && j < i + 4096; j += 4) {
			/* no need for endianness conversion on the ppc */
			__raw_writel(*src, dst);
			if (__raw_readl(dst) != *src) {
				CX18_ERR("Mismatch at offset %x\n", i);
				release_firmware(fw);
				return -EIO;
			}
			dst++;
			src++;
		}
	}
	if (!test_bit(CX18_F_I_LOADED_FW, &cx->i_flags))
		CX18_INFO("loaded %s firmware (%zd bytes)\n", fn, fw->size);
	release_firmware(fw);
	return size;
}

static int load_apu_fw_direct(const char *fn, u8 __iomem *dst, struct cx18 *cx, long size)
{
	const struct firmware *fw = NULL;
	int retries = 3;
	int i, j;
	const u32 *src;
	struct cx18_apu_rom_seghdr seghdr;
	const u8 *vers;
	u32 offset = 0;
	u32 apu_version = 0;
	int sz;

retry:
	if (!retries || request_firmware(&fw, fn, &cx->dev->dev)) {
		CX18_ERR("unable to open firmware %s (must be %ld bytes)\n",
				fn, size);
		CX18_ERR("did you put the firmware in the hotplug firmware directory?\n");
		return -ENOMEM;
	}

	src = (const u32 *)fw->data;
	vers = fw->data + sizeof(seghdr);
	sz = fw->size;

	if (fw->size != size) {
		/* Due to race conditions in firmware loading (esp. with
		   udev <0.95) the wrong file was sometimes loaded. So we check
		   filesizes to see if at least the right-sized file was
		   loaded. If not, then we retry. */
		CX18_INFO("retry: file loaded was not %s (expected size %ld, got %zd)\n",
			       fn, size, fw->size);
		release_firmware(fw);
		retries--;
		goto retry;
	}
	apu_version = (vers[0] << 24) | (vers[4] << 16) | vers[32];
	while (offset + sizeof(seghdr) < size) {
		/* TODO: byteswapping */
		memcpy(&seghdr, src + offset / 4, sizeof(seghdr));
		offset += sizeof(seghdr);
		if (seghdr.sync1 != APU_ROM_SYNC1 ||
		    seghdr.sync2 != APU_ROM_SYNC2) {
			offset += seghdr.size;
			continue;
		}
		CX18_DEBUG_INFO("load segment %x-%x\n", seghdr.addr,
				seghdr.addr + seghdr.size - 1);
		if (offset + seghdr.size > sz)
			break;
		for (i = 0; i < seghdr.size; i += 4096) {
			setup_page(offset + i);
			for (j = i; j < seghdr.size && j < i + 4096; j += 4) {
				/* no need for endianness conversion on the ppc */
				__raw_writel(src[(offset + j) / 4], dst + seghdr.addr + j);
				if (__raw_readl(dst + seghdr.addr + j) != src[(offset + j) / 4]) {
					CX18_ERR("Mismatch at offset %x\n", offset + j);
					release_firmware(fw);
					return -EIO;
				}
			}
		}
		offset += seghdr.size;
	}
	if (!test_bit(CX18_F_I_LOADED_FW, &cx->i_flags))
		CX18_INFO("loaded %s firmware V%08x (%zd bytes)\n",
				fn, apu_version, fw->size);
	release_firmware(fw);
	/* Clear bit0 for APU to start from 0 */
	write_reg(read_reg(0xc72030) & ~1, 0xc72030);
	return size;
}

void cx18_halt_firmware(struct cx18 *cx)
{
	CX18_DEBUG_INFO("Preparing for firmware halt.\n");
	write_reg(0x000F000F, CX18_PROC_SOFT_RESET); /* stop the fw */
	write_reg(0x00020002, CX18_ADEC_CONTROL);
}

void cx18_init_power(struct cx18 *cx, int lowpwr)
{
	/* power-down Spare and AOM PLLs */
	/* power-up fast, slow and mpeg PLLs */
	write_reg(0x00000008, CX18_PLL_POWER_DOWN);

	/* ADEC out of sleep */
	write_reg(0x00020000, CX18_ADEC_CONTROL);

	/* The fast clock is at 200/245 MHz */
	write_reg(lowpwr ? 0xD : 0x11, CX18_FAST_CLOCK_PLL_INT);
	write_reg(lowpwr ? 0x1EFBF37 : 0x038E3D7, CX18_FAST_CLOCK_PLL_FRAC);

	write_reg(2, CX18_FAST_CLOCK_PLL_POST);
	write_reg(1, CX18_FAST_CLOCK_PLL_PRESCALE);
	write_reg(4, CX18_FAST_CLOCK_PLL_ADJUST_BANDWIDTH);

	/* set slow clock to 125/120 MHz */
	write_reg(lowpwr ? 0x11 : 0x10, CX18_SLOW_CLOCK_PLL_INT);
	write_reg(lowpwr ? 0xEBAF05 : 0x18618A8, CX18_SLOW_CLOCK_PLL_FRAC);
	write_reg(4, CX18_SLOW_CLOCK_PLL_POST);

	/* mpeg clock pll 54MHz */
	write_reg(0xF, CX18_MPEG_CLOCK_PLL_INT);
	write_reg(0x2BCFEF, CX18_MPEG_CLOCK_PLL_FRAC);
	write_reg(8, CX18_MPEG_CLOCK_PLL_POST);

	/* Defaults */
	/* APU = SC or SC/2 = 125/62.5 */
	/* EPU = SC = 125 */
	/* DDR = FC = 180 */
	/* ENC = SC = 125 */
	/* AI1 = SC = 125 */
	/* VIM2 = disabled */
	/* PCI = FC/2 = 90 */
	/* AI2 = disabled */
	/* DEMUX = disabled */
	/* AO = SC/2 = 62.5 */
	/* SER = 54MHz */
	/* VFC = disabled */
	/* USB = disabled */

	write_reg(lowpwr ? 0xFFFF0020 : 0x00060004, CX18_CLOCK_SELECT1);
	write_reg(lowpwr ? 0xFFFF0004 : 0x00060006, CX18_CLOCK_SELECT2);

	write_reg(0xFFFF0002, CX18_HALF_CLOCK_SELECT1);
	write_reg(0xFFFF0104, CX18_HALF_CLOCK_SELECT2);

	write_reg(0xFFFF9026, CX18_CLOCK_ENABLE1);
	write_reg(0xFFFF3105, CX18_CLOCK_ENABLE2);
}

void cx18_init_memory(struct cx18 *cx)
{
	cx18_msleep_timeout(10, 0);
	write_reg(0x10000, CX18_DDR_SOFT_RESET);
	cx18_msleep_timeout(10, 0);

	write_reg(cx->card->ddr.chip_config, CX18_DDR_CHIP_CONFIG);

	cx18_msleep_timeout(10, 0);

	write_reg(cx->card->ddr.refresh, CX18_DDR_REFRESH);
	write_reg(cx->card->ddr.timing1, CX18_DDR_TIMING1);
	write_reg(cx->card->ddr.timing2, CX18_DDR_TIMING2);

	cx18_msleep_timeout(10, 0);

	/* Initialize DQS pad time */
	write_reg(cx->card->ddr.tune_lane, CX18_DDR_TUNE_LANE);
	write_reg(cx->card->ddr.initial_emrs, CX18_DDR_INITIAL_EMRS);

	cx18_msleep_timeout(10, 0);

	write_reg(0x20000, CX18_DDR_SOFT_RESET);
	cx18_msleep_timeout(10, 0);

	/* use power-down mode when idle */
	write_reg(0x00000010, CX18_DDR_POWER_REG);

	write_reg(0x10001, CX18_REG_BUS_TIMEOUT_EN);

	write_reg(0x48, CX18_DDR_MB_PER_ROW_7);
	write_reg(0xE0000, CX18_DDR_BASE_63_ADDR);

	write_reg(0x00000101, CX18_WMB_CLIENT02);  /* AO */
	write_reg(0x00000101, CX18_WMB_CLIENT09);  /* AI2 */
	write_reg(0x00000101, CX18_WMB_CLIENT05);  /* VIM1 */
	write_reg(0x00000101, CX18_WMB_CLIENT06);  /* AI1 */
	write_reg(0x00000101, CX18_WMB_CLIENT07);  /* 3D comb */
	write_reg(0x00000101, CX18_WMB_CLIENT10);  /* ME */
	write_reg(0x00000101, CX18_WMB_CLIENT12);  /* ENC */
	write_reg(0x00000101, CX18_WMB_CLIENT13);  /* PK */
	write_reg(0x00000101, CX18_WMB_CLIENT11);  /* RC */
	write_reg(0x00000101, CX18_WMB_CLIENT14);  /* AVO */
}

int cx18_firmware_init(struct cx18 *cx)
{
	/* Allow chip to control CLKRUN */
	write_reg(0x5, CX18_DSP0_INTERRUPT_MASK);

	write_reg(0x000F000F, CX18_PROC_SOFT_RESET); /* stop the fw */

	cx18_msleep_timeout(1, 0);

	sw1_irq_enable(IRQ_CPU_TO_EPU | IRQ_APU_TO_EPU);
	sw2_irq_enable(IRQ_CPU_TO_EPU_ACK | IRQ_APU_TO_EPU_ACK);

	/* Only if the processor is not running */
	if (read_reg(CX18_PROC_SOFT_RESET) & 8) {
		int sz = load_apu_fw_direct("v4l-cx23418-apu.fw",
			       cx->enc_mem, cx, CX18_FW_APU_SIZE);

		sz = sz <= 0 ? sz : load_cpu_fw_direct("v4l-cx23418-cpu.fw",
					cx->enc_mem, cx, CX18_FW_CPU_SIZE);

		if (sz > 0) {
			int retries = 0;

			/* start the CPU */
			write_reg(0x00080000, CX18_PROC_SOFT_RESET);
			while (retries++ < 50) { /* Loop for max 500mS */
				if ((read_reg(CX18_PROC_SOFT_RESET) & 1) == 0)
					break;
				cx18_msleep_timeout(10, 0);
			}
			cx18_msleep_timeout(200, 0);
			if (retries == 51) {
				CX18_ERR("Could not start the CPU\n");
				return -EIO;
			}
		}
		if (sz <= 0)
			return -EIO;
	}
	/* initialize GPIO */
	write_reg(0x14001400, 0xC78110);
	return 0;
}
