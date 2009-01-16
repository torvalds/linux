/*
 *  cx18 firmware functions
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@radix.net>
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
#include "cx18-io.h"
#include "cx18-scb.h"
#include "cx18-irq.h"
#include "cx18-firmware.h"
#include "cx18-cards.h"
#include "cx18-av-core.h"
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

#define APU_ROM_SYNC1 0x6D676553 /* "mgeS" */
#define APU_ROM_SYNC2 0x72646548 /* "rdeH" */

struct cx18_apu_rom_seghdr {
	u32 sync1;
	u32 sync2;
	u32 addr;
	u32 size;
};

static int load_cpu_fw_direct(const char *fn, u8 __iomem *mem, struct cx18 *cx)
{
	const struct firmware *fw = NULL;
	int i, j;
	unsigned size;
	u32 __iomem *dst = (u32 __iomem *)mem;
	const u32 *src;

	if (request_firmware(&fw, fn, &cx->dev->dev)) {
		CX18_ERR("Unable to open firmware %s\n", fn);
		CX18_ERR("Did you put the firmware in the hotplug firmware directory?\n");
		return -ENOMEM;
	}

	src = (const u32 *)fw->data;

	for (i = 0; i < fw->size; i += 4096) {
		cx18_setup_page(cx, i);
		for (j = i; j < fw->size && j < i + 4096; j += 4) {
			/* no need for endianness conversion on the ppc */
			cx18_raw_writel(cx, *src, dst);
			if (cx18_raw_readl(cx, dst) != *src) {
				CX18_ERR("Mismatch at offset %x\n", i);
				release_firmware(fw);
				cx18_setup_page(cx, 0);
				return -EIO;
			}
			dst++;
			src++;
		}
	}
	if (!test_bit(CX18_F_I_LOADED_FW, &cx->i_flags))
		CX18_INFO("loaded %s firmware (%zd bytes)\n", fn, fw->size);
	size = fw->size;
	release_firmware(fw);
	cx18_setup_page(cx, SCB_OFFSET);
	return size;
}

static int load_apu_fw_direct(const char *fn, u8 __iomem *dst, struct cx18 *cx,
				u32 *entry_addr)
{
	const struct firmware *fw = NULL;
	int i, j;
	unsigned size;
	const u32 *src;
	struct cx18_apu_rom_seghdr seghdr;
	const u8 *vers;
	u32 offset = 0;
	u32 apu_version = 0;
	int sz;

	if (request_firmware(&fw, fn, &cx->dev->dev)) {
		CX18_ERR("unable to open firmware %s\n", fn);
		CX18_ERR("did you put the firmware in the hotplug firmware directory?\n");
		cx18_setup_page(cx, 0);
		return -ENOMEM;
	}

	*entry_addr = 0;
	src = (const u32 *)fw->data;
	vers = fw->data + sizeof(seghdr);
	sz = fw->size;

	apu_version = (vers[0] << 24) | (vers[4] << 16) | vers[32];
	while (offset + sizeof(seghdr) < fw->size) {
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
		if (*entry_addr == 0)
			*entry_addr = seghdr.addr;
		if (offset + seghdr.size > sz)
			break;
		for (i = 0; i < seghdr.size; i += 4096) {
			cx18_setup_page(cx, seghdr.addr + i);
			for (j = i; j < seghdr.size && j < i + 4096; j += 4) {
				/* no need for endianness conversion on the ppc */
				cx18_raw_writel(cx, src[(offset + j) / 4],
						dst + seghdr.addr + j);
				if (cx18_raw_readl(cx, dst + seghdr.addr + j)
				    != src[(offset + j) / 4]) {
					CX18_ERR("Mismatch at offset %x\n",
						 offset + j);
					release_firmware(fw);
					cx18_setup_page(cx, 0);
					return -EIO;
				}
			}
		}
		offset += seghdr.size;
	}
	if (!test_bit(CX18_F_I_LOADED_FW, &cx->i_flags))
		CX18_INFO("loaded %s firmware V%08x (%zd bytes)\n",
				fn, apu_version, fw->size);
	size = fw->size;
	release_firmware(fw);
	cx18_setup_page(cx, 0);
	return size;
}

void cx18_halt_firmware(struct cx18 *cx)
{
	CX18_DEBUG_INFO("Preparing for firmware halt.\n");
	cx18_write_reg_expect(cx, 0x000F000F, CX18_PROC_SOFT_RESET,
				  0x0000000F, 0x000F000F);
	cx18_write_reg_expect(cx, 0x00020002, CX18_ADEC_CONTROL,
				  0x00000002, 0x00020002);
}

void cx18_init_power(struct cx18 *cx, int lowpwr)
{
	/* power-down Spare and AOM PLLs */
	/* power-up fast, slow and mpeg PLLs */
	cx18_write_reg(cx, 0x00000008, CX18_PLL_POWER_DOWN);

	/* ADEC out of sleep */
	cx18_write_reg_expect(cx, 0x00020000, CX18_ADEC_CONTROL,
				  0x00000000, 0x00020002);

	/*
	 * The PLL parameters are based on the external crystal frequency that
	 * would ideally be:
	 *
	 * NTSC Color subcarrier freq * 8 =
	 * 	4.5 MHz/286 * 455/2 * 8 = 28.63636363... MHz
	 *
	 * The accidents of history and rationale that explain from where this
	 * combination of magic numbers originate can be found in:
	 *
	 * [1] Abrahams, I. C., "Choice of Chrominance Subcarrier Frequency in
	 * the NTSC Standards", Proceedings of the I-R-E, January 1954, pp 79-80
	 *
	 * [2] Abrahams, I. C., "The 'Frequency Interleaving' Principle in the
	 * NTSC Standards", Proceedings of the I-R-E, January 1954, pp 81-83
	 *
	 * As Mike Bradley has rightly pointed out, it's not the exact crystal
	 * frequency that matters, only that all parts of the driver and
	 * firmware are using the same value (close to the ideal value).
	 *
	 * Since I have a strong suspicion that, if the firmware ever assumes a
	 * crystal value at all, it will assume 28.636360 MHz, the crystal
	 * freq used in calculations in this driver will be:
	 *
	 *	xtal_freq = 28.636360 MHz
	 *
	 * an error of less than 0.13 ppm which is way, way better than any off
	 * the shelf crystal will have for accuracy anyway.
	 *
	 * Below I aim to run the PLLs' VCOs near 400 MHz to minimze errors.
	 *
	 * Many thanks to Jeff Campbell and Mike Bradley for their extensive
	 * investigation, experimentation, testing, and suggested solutions of
	 * of audio/video sync problems with SVideo and CVBS captures.
	 */

	/* the fast clock is at 200/245 MHz */
	/* 1 * xtal_freq * 0x0d.f7df9b8 / 2 = 200 MHz: 400 MHz pre post-divide*/
	/* 1 * xtal_freq * 0x11.1c71eb8 / 2 = 245 MHz: 490 MHz pre post-divide*/
	cx18_write_reg(cx, lowpwr ? 0xD : 0x11, CX18_FAST_CLOCK_PLL_INT);
	cx18_write_reg(cx, lowpwr ? 0x1EFBF37 : 0x038E3D7,
						CX18_FAST_CLOCK_PLL_FRAC);

	cx18_write_reg(cx, 2, CX18_FAST_CLOCK_PLL_POST);
	cx18_write_reg(cx, 1, CX18_FAST_CLOCK_PLL_PRESCALE);
	cx18_write_reg(cx, 4, CX18_FAST_CLOCK_PLL_ADJUST_BANDWIDTH);

	/* set slow clock to 125/120 MHz */
	/* xtal_freq * 0x0d.1861a20 / 3 = 125 MHz: 375 MHz before post-divide */
	/* xtal_freq * 0x0c.92493f8 / 3 = 120 MHz: 360 MHz before post-divide */
	cx18_write_reg(cx, lowpwr ? 0xD : 0xC, CX18_SLOW_CLOCK_PLL_INT);
	cx18_write_reg(cx, lowpwr ? 0x30C344 : 0x124927F,
						CX18_SLOW_CLOCK_PLL_FRAC);
	cx18_write_reg(cx, 3, CX18_SLOW_CLOCK_PLL_POST);

	/* mpeg clock pll 54MHz */
	/* xtal_freq * 0xf.15f17f0 / 8 = 54 MHz: 432 MHz before post-divide */
	cx18_write_reg(cx, 0xF, CX18_MPEG_CLOCK_PLL_INT);
	cx18_write_reg(cx, 0x2BE2FE, CX18_MPEG_CLOCK_PLL_FRAC);
	cx18_write_reg(cx, 8, CX18_MPEG_CLOCK_PLL_POST);

	/*
	 * VDCLK  Integer = 0x0f, Post Divider = 0x04
	 * AIMCLK Integer = 0x0e, Post Divider = 0x16
	 */
	cx18_av_write4(cx, CXADEC_PLL_CTRL1, 0x160e040f);

	/* VDCLK Fraction = 0x2be2fe */
	/* xtal * 0xf.15f17f0/4 = 108 MHz: 432 MHz before post divide */
	cx18_av_write4(cx, CXADEC_VID_PLL_FRAC, 0x002be2fe);

	/* AIMCLK Fraction = 0x05227ad */
	/* xtal * 0xe.2913d68/0x16 = 48000 * 384: 406 MHz before post-divide */
	cx18_av_write4(cx, CXADEC_AUX_PLL_FRAC, 0x005227ad);

	/* SA_MCLK_SEL=1, SA_MCLK_DIV=0x16 */
	cx18_av_write(cx, CXADEC_I2S_MCLK, 0x56);

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

	if (lowpwr) {
		cx18_write_reg_expect(cx, 0xFFFF0020, CX18_CLOCK_SELECT1,
					  0x00000020, 0xFFFFFFFF);
		cx18_write_reg_expect(cx, 0xFFFF0004, CX18_CLOCK_SELECT2,
					  0x00000004, 0xFFFFFFFF);
	} else {
		/* This doesn't explicitly set every clock select */
		cx18_write_reg_expect(cx, 0x00060004, CX18_CLOCK_SELECT1,
					  0x00000004, 0x00060006);
		cx18_write_reg_expect(cx, 0x00060006, CX18_CLOCK_SELECT2,
					  0x00000006, 0x00060006);
	}

	cx18_write_reg_expect(cx, 0xFFFF0002, CX18_HALF_CLOCK_SELECT1,
				  0x00000002, 0xFFFFFFFF);
	cx18_write_reg_expect(cx, 0xFFFF0104, CX18_HALF_CLOCK_SELECT2,
				  0x00000104, 0xFFFFFFFF);
	cx18_write_reg_expect(cx, 0xFFFF9026, CX18_CLOCK_ENABLE1,
				  0x00009026, 0xFFFFFFFF);
	cx18_write_reg_expect(cx, 0xFFFF3105, CX18_CLOCK_ENABLE2,
				  0x00003105, 0xFFFFFFFF);
}

void cx18_init_memory(struct cx18 *cx)
{
	cx18_msleep_timeout(10, 0);
	cx18_write_reg_expect(cx, 0x00010000, CX18_DDR_SOFT_RESET,
				  0x00000000, 0x00010001);
	cx18_msleep_timeout(10, 0);

	cx18_write_reg(cx, cx->card->ddr.chip_config, CX18_DDR_CHIP_CONFIG);

	cx18_msleep_timeout(10, 0);

	cx18_write_reg(cx, cx->card->ddr.refresh, CX18_DDR_REFRESH);
	cx18_write_reg(cx, cx->card->ddr.timing1, CX18_DDR_TIMING1);
	cx18_write_reg(cx, cx->card->ddr.timing2, CX18_DDR_TIMING2);

	cx18_msleep_timeout(10, 0);

	/* Initialize DQS pad time */
	cx18_write_reg(cx, cx->card->ddr.tune_lane, CX18_DDR_TUNE_LANE);
	cx18_write_reg(cx, cx->card->ddr.initial_emrs, CX18_DDR_INITIAL_EMRS);

	cx18_msleep_timeout(10, 0);

	cx18_write_reg_expect(cx, 0x00020000, CX18_DDR_SOFT_RESET,
				  0x00000000, 0x00020002);
	cx18_msleep_timeout(10, 0);

	/* use power-down mode when idle */
	cx18_write_reg(cx, 0x00000010, CX18_DDR_POWER_REG);

	cx18_write_reg_expect(cx, 0x00010001, CX18_REG_BUS_TIMEOUT_EN,
				  0x00000001, 0x00010001);

	cx18_write_reg(cx, 0x48, CX18_DDR_MB_PER_ROW_7);
	cx18_write_reg(cx, 0xE0000, CX18_DDR_BASE_63_ADDR);

	cx18_write_reg(cx, 0x00000101, CX18_WMB_CLIENT02);  /* AO */
	cx18_write_reg(cx, 0x00000101, CX18_WMB_CLIENT09);  /* AI2 */
	cx18_write_reg(cx, 0x00000101, CX18_WMB_CLIENT05);  /* VIM1 */
	cx18_write_reg(cx, 0x00000101, CX18_WMB_CLIENT06);  /* AI1 */
	cx18_write_reg(cx, 0x00000101, CX18_WMB_CLIENT07);  /* 3D comb */
	cx18_write_reg(cx, 0x00000101, CX18_WMB_CLIENT10);  /* ME */
	cx18_write_reg(cx, 0x00000101, CX18_WMB_CLIENT12);  /* ENC */
	cx18_write_reg(cx, 0x00000101, CX18_WMB_CLIENT13);  /* PK */
	cx18_write_reg(cx, 0x00000101, CX18_WMB_CLIENT11);  /* RC */
	cx18_write_reg(cx, 0x00000101, CX18_WMB_CLIENT14);  /* AVO */
}

int cx18_firmware_init(struct cx18 *cx)
{
	u32 fw_entry_addr;
	int sz, retries;
	u32 api_args[MAX_MB_ARGUMENTS];

	/* Allow chip to control CLKRUN */
	cx18_write_reg(cx, 0x5, CX18_DSP0_INTERRUPT_MASK);

	/* Stop the firmware */
	cx18_write_reg_expect(cx, 0x000F000F, CX18_PROC_SOFT_RESET,
				  0x0000000F, 0x000F000F);

	cx18_msleep_timeout(1, 0);

	/* If the CPU is still running */
	if ((cx18_read_reg(cx, CX18_PROC_SOFT_RESET) & 8) == 0) {
		CX18_ERR("%s: couldn't stop CPU to load firmware\n", __func__);
		return -EIO;
	}

	cx18_sw1_irq_enable(cx, IRQ_CPU_TO_EPU | IRQ_APU_TO_EPU);
	cx18_sw2_irq_enable(cx, IRQ_CPU_TO_EPU_ACK | IRQ_APU_TO_EPU_ACK);

	sz = load_cpu_fw_direct("v4l-cx23418-cpu.fw", cx->enc_mem, cx);
	if (sz <= 0)
		return sz;

	/* The SCB & IPC area *must* be correct before starting the firmwares */
	cx18_init_scb(cx);

	fw_entry_addr = 0;
	sz = load_apu_fw_direct("v4l-cx23418-apu.fw", cx->enc_mem, cx,
				&fw_entry_addr);
	if (sz <= 0)
		return sz;

	/* Start the CPU. The CPU will take care of the APU for us. */
	cx18_write_reg_expect(cx, 0x00080000, CX18_PROC_SOFT_RESET,
				  0x00000000, 0x00080008);

	/* Wait up to 500 ms for the APU to come out of reset */
	for (retries = 0;
	     retries < 50 && (cx18_read_reg(cx, CX18_PROC_SOFT_RESET) & 1) == 1;
	     retries++)
		cx18_msleep_timeout(10, 0);

	cx18_msleep_timeout(200, 0);

	if (retries == 50 &&
	    (cx18_read_reg(cx, CX18_PROC_SOFT_RESET) & 1) == 1) {
		CX18_ERR("Could not start the CPU\n");
		return -EIO;
	}

	/*
	 * The CPU had once before set up to receive an interrupt for it's
	 * outgoing IRQ_CPU_TO_EPU_ACK to us.  If it ever does this, we get an
	 * interrupt when it sends us an ack, but by the time we process it,
	 * that flag in the SW2 status register has been cleared by the CPU
	 * firmware.  We'll prevent that not so useful condition from happening
	 * by clearing the CPU's interrupt enables for Ack IRQ's we want to
	 * process.
	 */
	cx18_sw2_irq_disable_cpu(cx, IRQ_CPU_TO_EPU_ACK | IRQ_APU_TO_EPU_ACK);

	/* Try a benign command to see if the CPU is alive and well */
	sz = cx18_vapi_result(cx, api_args, CX18_CPU_DEBUG_PEEK32, 1, 0);
	if (sz < 0)
		return sz;

	/* initialize GPIO */
	cx18_write_reg_expect(cx, 0x14001400, 0xc78110, 0x00001400, 0x14001400);
	return 0;
}
