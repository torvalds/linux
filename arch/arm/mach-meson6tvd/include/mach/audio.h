/*
 * arch/arm/mach-meson6tvd/include/mach/audio.h
 *
 * Copyright (C) 2014 Amlogic, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MACH_MESON_AUDIO_REGS_H
#define __MACH_MESON_AUDIO_REGS_H

#define SPDIF_EN			31
#define SPDIF_INT_EN			30
#define SPDIF_BURST_PRE_INT_EN		29
#define SPDIF_TIE_0			24
#define SPDIF_SAMPLE_SEL		23
#define SPDIF_REVERSE_EN		22
#define SPDIF_BIT_ORDER 		20
#define SPDIF_CHNL_ORDER		19
#define SPDIF_DATA_TYPE_SEL		18
#define SPDIF_XTDCLK_UPD_ITVL		14	//16:14
#define SPDIF_CLKNUM_54U		0	//13:0

#define SPDIF_CLKNUM_192K		24	//29:24
#define SPDIF_CLKNUM_96K		18	//23:18
#define SPDIF_CLKNUM_48K		12	//17:12
#define SPDIF_CLKNUM_44K		6	// 11:6
#define SPDIF_CLKNUM_32K		0	// 5:0

#define I2SIN_DIR			0	// I2S CLK and LRCLK direction. 0 : input 1 : output.
#define I2SIN_CLK_SEL			1	// I2S clk selection : 0 : from pad input. 1 : from AIU.
#define I2SIN_LRCLK_SEL			2
#define I2SIN_POS_SYNC			3
#define I2SIN_LRCLK_SKEW		4	// 6:4
#define I2SIN_LRCLK_INVT		7
#define I2SIN_SIZE			8	//9:8 : 0 16 bit. 1 : 18 bits 2 : 20 bits 3 : 24bits.
#define I2SIN_CHAN_EN			10	//13:10.
#define I2SIN_EN			15

#define AUDIN_FIFO0_EN			0
#define AUDIN_FIFO0_RST			1
#define AUDIN_FIFO0_LOAD		2	//write 1 to load address to AUDIN_FIFO0.

#define AUDIN_FIFO0_DIN_SEL		3
	// 0	 spdifIN
	// 1	 i2Sin
	// 2	 PCMIN
	// 3	 HDMI in
	// 4	 DEMODULATOR IN
#define AUDIN_FIFO0_ENDIAN		8	//10:8   data endian control.
#define AUDIN_FIFO0_CHAN		11	//14:11   channel number.  in M1 suppose there's only 1 channel and 2 channel.
#define AUDIN_FIFO0_UG			15	// urgent request enable.
#define AUDIN_FIFO0_HOLD0_EN		19
#define AUDIN_FIFO0_HOLD1_EN		20
#define AUDIN_FIFO0_HOLD2_EN		21
#define AUDIN_FIFO0_HOLD0_SEL		22	// 23:22
#define AUDIN_FIFO0_HOLD1_SEL		24	// 25:24
#define AUDIN_FIFO0_HOLD2_SEL		26	// 27:26
#define AUDIN_FIFO0_HOLD_LVL		28	// 27:26

#define AUDIN_FIFO1_EN			0
#define AUDIN_FIFO1_RST			1
#define AUDIN_FIFO1_LOAD		2	//write 1 to load address to AUDIN_FIFO0.

#define AUDIN_FIFO1_DIN_SEL		3
	// 0	 spdifIN
	// 1	 i2Sin
	// 2	 PCMIN
	// 3	 HDMI in
	// 4	 DEMODULATOR IN
#define AUDIN_FIFO1_ENDIAN		8	//10:8   data endian control.
#define AUDIN_FIFO1_CHAN		11	//14:11   channel number.  in M1 suppose there's only 1 channel and 2 channel.
#define AUDIN_FIFO1_UG			15	// urgent request enable.
#define AUDIN_FIFO1_HOLD0_EN		19
#define AUDIN_FIFO1_HOLD1_EN		20
#define AUDIN_FIFO1_HOLD2_EN		21
#define AUDIN_FIFO1_HOLD0_SEL		22	// 23:22
#define AUDIN_FIFO1_HOLD1_SEL		24	// 25:24
#define AUDIN_FIFO1_HOLD2_SEL		26	// 27:26
#define AUDIN_FIFO1_HOLD_LVL		28	// 27:26


#endif // __MACH_MESON_AUDIO_REGS_H
