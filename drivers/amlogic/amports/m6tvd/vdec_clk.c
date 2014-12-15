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
HHI_VDEC_CLK_CNTL..
bits,9~11:
0x106d[11:9] :
0 for fclk_div2,  1GHz
1 for fclk_div3,  2G/3Hz
2 for fclk_div5, 2G/5Hz
3 for fclk_div7, 2G/7HZ

4 for mp1_clk_out
5 for ddr_pll_clk

bit0~6: div N=bit[0-7]+1
bit8: vdec.gate
*/
#define VDEC1_166M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, (0 << 9) | (1 << 8) | (5), 0, 16)
#define VDEC2_166M() WRITE_MPEG_REG(HHI_VDEC2_CLK_CNTL, (0 << 9) | (1 << 8) | (5))

#define VDEC1_200M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, (0 << 9) | (1 << 8) | (4), 0, 16)
#define VDEC2_200M() WRITE_MPEG_REG(HHI_VDEC2_CLK_CNTL, (0 << 9) | (1 << 8) | (4))

#define VDEC1_250M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, (0 << 9) | (1 << 8) | (3), 0, 16)
#define VDEC2_250M() WRITE_MPEG_REG(HHI_VDEC2_CLK_CNTL, (0 << 9) | (1 << 8) | (3))
#define HCODEC_250M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, (0 << 9) | (1 << 8) | (3), 16, 16)

#define VDEC1_333M() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, (0 << 9) | (1 << 8) | (2), 0, 16)
#define VDEC2_333M() WRITE_MPEG_REG(HHI_VDEC2_CLK_CNTL, (0 << 9) | (1 << 8) | (2))

#define VDEC1_CLOCK_ON()   WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, 1, 8, 1); \
                           WRITE_VREG_BITS(DOS_GCLK_EN0, 0x3ff, 0, 10)
#define VDEC2_CLOCK_ON()   WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, 1, 8, 1); \
                           WRITE_VREG(DOS_GCLK_EN1, 0x3ff)
#define HCODEC_CLOCK_ON()  WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, 1, 24, 1); \
                           WRITE_VREG_BITS(DOS_GCLK_EN0, 0x7fff, 12, 15)
#define VDEC1_CLOCK_OFF()  WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL,  0, 8, 1)
#define VDEC2_CLOCK_OFF()  WRITE_MPEG_REG_BITS(HHI_VDEC2_CLK_CNTL, 0, 8, 1)
#define HCODEC_CLOCK_OFF() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, 0, 24, 1)

static int clock_level[VDEC_MAX+1];

void vdec_clock_enable(void)
{
    VDEC1_CLOCK_OFF();
    VDEC1_250M();
    VDEC1_CLOCK_ON();
    clock_level[VDEC_1] = 0;
}

void vdec_clock_hi_enable(void)
{
    VDEC1_CLOCK_OFF();
    VDEC1_250M();
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
    VDEC2_250M();
    VDEC2_CLOCK_ON();
    clock_level[VDEC_2] = 0;
}

void vdec2_clock_hi_enable(void)
{
    VDEC2_CLOCK_OFF();
    VDEC2_250M();
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
    HCODEC_250M();
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

int vdec_clock_level(vdec_type_t core)
{
    if (core >= VDEC_MAX)
        return 0; 

    return clock_level[core];
}

