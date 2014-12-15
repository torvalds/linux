/*
 * AMLOGIC Audio/Video streaming port driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 :*
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>

#include <mach/am_regs.h>

#include "../vdec_reg.h"

#include "../amports_config.h"

#include "../vdec.h"
#include "../vdec_clk.h"

/*
HHI_VDEC_CLK_CNTL
0x1078[11:9] (fclk = 2550MHz)
    0: fclk_div4
    1: fclk_div3
    2: fclk_div5
    3: fclk_div7
    4: mpll_clk_out1
    5: mpll_clk_out2
0x1078[6:0]
    devider
0x1078[8]
    enable
*/

//182.14M <-- (2550/7)/2
#define VDEC1_182M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL,  (3 << 9) | (1), 0, 16)
#define VDEC2_182M() WRITE_MPEG_REG(HHI_VDEC2_CLK_CNTL, (3 << 9) | (1))

//212.50M <-- (2550/3)/4
#define VDEC1_212M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL,  (1 << 9) | (3), 0, 16)
#define VDEC2_212M() WRITE_MPEG_REG(HHI_VDEC2_CLK_CNTL, (1 << 9) | (3))

//255.00M <-- (2550/5)/2
#define VDEC1_255M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL,  (2 << 9) | (1), 0, 16)
#define VDEC2_255M() WRITE_MPEG_REG(HHI_VDEC2_CLK_CNTL, (2 << 9) | (1))
#define HCODEC_255M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, (2 << 9) | (1), 16, 16)
#define HEVC_255M()  WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, (2 << 9) | (1), 16, 16)

//283.33M <-- (2550/3)/3
#define VDEC1_283M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL,  (1 << 9) | (2), 0, 16)
#define VDEC2_283M() WRITE_MPEG_REG(HHI_VDEC2_CLK_CNTL, (1 << 9) | (2));

//318.75M <-- (2550/4)/2
#define VDEC1_319M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL,  (0 << 9) | (1), 0, 16)
#define VDEC2_319M() WRITE_MPEG_REG(HHI_VDEC2_CLK_CNTL, (0 << 9) | (1))

//364.29M <-- (2550/7)/1 -- over limit, do not use
#define VDEC1_364M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL,  (3 << 9) | (0), 0, 16)
#define VDEC2_364M() WRITE_MPEG_REG(HHI_VDEC2_CLK_CNTL, (3 << 9) | (0))

//425.00M <-- (2550/3)/2
#define VDEC1_425M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL,  (1 << 9) | (2), 0, 16)

//510.00M <-- (2550/5)/1
#define VDEC1_510M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL,  (2 << 9) | (0), 0, 16)
#define HEVC_510M()  WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, (2 << 9) | (0), 16, 16)

//637.50M <-- (2550/4)/1
#define VDEC1_638M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL,  (0 << 9) | (0), 0, 16)
#define HEVC_638M()  WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, (0 << 9) | (0), 16, 16)

#define VDEC1_CLOCK_ON()  do { if (IS_MESON_M8_CPU) { \
                                    WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, 1, 8, 1); \
                                    WRITE_VREG_BITS(DOS_GCLK_EN0, 0x3ff,0,10); \
                                } else { \
                                    WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, 1, 8, 1); \
                                    WRITE_MPEG_REG_BITS(HHI_VDEC3_CLK_CNTL, 0, 15, 1); \
                                    WRITE_MPEG_REG_BITS(HHI_VDEC3_CLK_CNTL, 0, 8, 1); \
                                    WRITE_VREG_BITS(DOS_GCLK_EN0, 0x3ff,0,10); \
                                } \
                            } while (0)

#define VDEC2_CLOCK_ON()   WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, 1, 8, 1); \
                           WRITE_VREG(DOS_GCLK_EN1, 0x3ff)

#define HCODEC_CLOCK_ON()  WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, 1, 24, 1); \
                           WRITE_VREG_BITS(DOS_GCLK_EN0, 0x7fff, 12, 15)
#define HEVC_CLOCK_ON()    WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, 0, 24, 1); \
                           WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, 0, 31, 1); \
                           WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, 1, 24, 1); \
                           WRITE_VREG(DOS_GCLK_EN3, 0xffffffff)
#define VDEC1_SAFE_CLOCK() WRITE_MPEG_REG_BITS(HHI_VDEC3_CLK_CNTL, READ_MPEG_REG(HHI_VDEC_CLK_CNTL) & 0x7f, 0, 7); \
                           WRITE_MPEG_REG_BITS(HHI_VDEC3_CLK_CNTL, 1, 8, 1); \
                           WRITE_MPEG_REG_BITS(HHI_VDEC3_CLK_CNTL, 1, 15, 1);
#define VDEC1_CLOCK_OFF()  WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL,  0, 8, 1)
#define VDEC2_CLOCK_OFF()  WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, 0, 8, 1)
#define HCODEC_CLOCK_OFF() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, 0, 24, 1)
#define HEVC_SAFE_CLOCK()  WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, (READ_MPEG_REG(HHI_VDEC2_CLK_CNTL) >> 16) & 0x7f, 16, 7); \
                           WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, 1, 24, 1); \
                           WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, 1, 31, 1)
#define HEVC_CLOCK_OFF()   WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, 0, 24, 1)

static int clock_level[VDEC_MAX+1];

void vdec_clock_enable(void)
{
    VDEC1_CLOCK_OFF();
    VDEC1_255M();
    VDEC1_CLOCK_ON();
    clock_level[VDEC_1] = 0;
}

void vdec_clock_hi_enable(void)
{
    VDEC1_CLOCK_OFF();
    if (IS_MESON_M8_CPU) {
        VDEC1_364M();
    } else {
        VDEC1_638M();
    }
    VDEC1_CLOCK_ON();
    clock_level[VDEC_1] = 1;
}

void vdec_clock_on(void)
{
    VDEC1_CLOCK_ON();
}

void vdec_clock_off(void)
{
    VDEC1_CLOCK_OFF();
}

void vdec2_clock_enable(void)
{
    VDEC2_CLOCK_OFF();
    VDEC2_255M();
    VDEC2_CLOCK_ON();
    clock_level[VDEC_2] = 0;
}

void vdec2_clock_hi_enable(void)
{
    VDEC2_CLOCK_OFF();
    VDEC2_364M();
    VDEC2_CLOCK_ON();
    clock_level[VDEC_2] = 1;
}

void vdec2_clock_on(void)
{
    VDEC2_CLOCK_ON();
}

void vdec2_clock_off(void)
{
    VDEC2_CLOCK_OFF();
}

void hcodec_clock_enable(void)
{
    HCODEC_CLOCK_OFF();
    HCODEC_255M();
    HCODEC_CLOCK_ON();
}

void hcodec_clock_on(void)
{
    HCODEC_CLOCK_ON();
}

void hcodec_clock_off(void)
{
    HCODEC_CLOCK_OFF();
}

void hevc_clock_enable(void)
{
    HEVC_CLOCK_OFF();
//    HEVC_255M();
    HEVC_638M();
    HEVC_CLOCK_ON();
}

void hevc_clock_hi_enable(void)
{
    HEVC_CLOCK_OFF();
    HEVC_638M();
    HEVC_CLOCK_ON();
    clock_level[VDEC_HEVC] = 1;
}

void hevc_clock_on(void)
{
    HEVC_CLOCK_ON();
}

void hevc_clock_off(void)
{
    HEVC_CLOCK_OFF();
}

void vdec_clock_prepare_switch(void)
{
    VDEC1_SAFE_CLOCK();
}

void hevc_clock_prepare_switch(void)
{
    HEVC_SAFE_CLOCK();
}

int vdec_clock_level(vdec_type_t core)
{
    if (core >= VDEC_MAX)
        return 0; 

    return clock_level[core];
}

