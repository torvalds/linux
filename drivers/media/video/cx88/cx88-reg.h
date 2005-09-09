/*

    cx88x-hw.h - CX2388x register offsets

    Copyright (C) 1996,97,98 Ralph Metzler (rjkm@thp.uni-koeln.de)
                  2001 Michael Eskin
                  2002 Yurij Sysoev <yurij@naturesoft.net>
                  2003 Gerd Knorr <kraxel@bytesex.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef _CX88_REG_H_
#define _CX88_REG_H_

/* ---------------------------------------------------------------------- */
/* PCI IDs and config space                                               */

#ifndef PCI_VENDOR_ID_CONEXANT
# define PCI_VENDOR_ID_CONEXANT		0x14F1
#endif
#ifndef PCI_DEVICE_ID_CX2300_VID
# define PCI_DEVICE_ID_CX2300_VID	0x8800
#endif

#define CX88X_DEVCTRL 0x40
#define CX88X_EN_TBFX 0x02
#define CX88X_EN_VSFX 0x04

/* ---------------------------------------------------------------------- */
/* PCI controller registers                                               */

/* Command and Status Register */
#define F0_CMD_STAT_MM      0x2f0004
#define F1_CMD_STAT_MM      0x2f0104
#define F2_CMD_STAT_MM      0x2f0204
#define F3_CMD_STAT_MM      0x2f0304
#define F4_CMD_STAT_MM      0x2f0404

/* Device Control #1 */
#define F0_DEV_CNTRL1_MM    0x2f0040
#define F1_DEV_CNTRL1_MM    0x2f0140
#define F2_DEV_CNTRL1_MM    0x2f0240
#define F3_DEV_CNTRL1_MM    0x2f0340
#define F4_DEV_CNTRL1_MM    0x2f0440

/* Device Control #1 */
#define F0_BAR0_MM          0x2f0010
#define F1_BAR0_MM          0x2f0110
#define F2_BAR0_MM          0x2f0210
#define F3_BAR0_MM          0x2f0310
#define F4_BAR0_MM          0x2f0410

/* ---------------------------------------------------------------------- */
/* DMA Controller registers                                               */

#define MO_PDMA_STHRSH      0x200000 // Source threshold
#define MO_PDMA_STADRS      0x200004 // Source target address
#define MO_PDMA_SIADRS      0x200008 // Source internal address
#define MO_PDMA_SCNTRL      0x20000C // Source control
#define MO_PDMA_DTHRSH      0x200010 // Destination threshold
#define MO_PDMA_DTADRS      0x200014 // Destination target address
#define MO_PDMA_DIADRS      0x200018 // Destination internal address
#define MO_PDMA_DCNTRL      0x20001C // Destination control
#define MO_LD_SSID          0x200030 // Load subsystem ID
#define MO_DEV_CNTRL2       0x200034 // Device control
#define MO_PCI_INTMSK       0x200040 // PCI interrupt mask
#define MO_PCI_INTSTAT      0x200044 // PCI interrupt status
#define MO_PCI_INTMSTAT     0x200048 // PCI interrupt masked status
#define MO_VID_INTMSK       0x200050 // Video interrupt mask
#define MO_VID_INTSTAT      0x200054 // Video interrupt status
#define MO_VID_INTMSTAT     0x200058 // Video interrupt masked status
#define MO_VID_INTSSTAT     0x20005C // Video interrupt set status
#define MO_AUD_INTMSK       0x200060 // Audio interrupt mask
#define MO_AUD_INTSTAT      0x200064 // Audio interrupt status
#define MO_AUD_INTMSTAT     0x200068 // Audio interrupt masked status
#define MO_AUD_INTSSTAT     0x20006C // Audio interrupt set status
#define MO_TS_INTMSK        0x200070 // Transport stream interrupt mask
#define MO_TS_INTSTAT       0x200074 // Transport stream interrupt status
#define MO_TS_INTMSTAT      0x200078 // Transport stream interrupt mask status
#define MO_TS_INTSSTAT      0x20007C // Transport stream interrupt set status
#define MO_VIP_INTMSK       0x200080 // VIP interrupt mask
#define MO_VIP_INTSTAT      0x200084 // VIP interrupt status
#define MO_VIP_INTMSTAT     0x200088 // VIP interrupt masked status
#define MO_VIP_INTSSTAT     0x20008C // VIP interrupt set status
#define MO_GPHST_INTMSK     0x200090 // Host interrupt mask
#define MO_GPHST_INTSTAT    0x200094 // Host interrupt status
#define MO_GPHST_INTMSTAT   0x200098 // Host interrupt masked status
#define MO_GPHST_INTSSTAT   0x20009C // Host interrupt set status

// DMA Channels 1-6 belong to SPIPE
#define MO_DMA7_PTR1        0x300018 // {24}RW* DMA Current Ptr : Ch#7
#define MO_DMA8_PTR1        0x30001C // {24}RW* DMA Current Ptr : Ch#8

// DMA Channels 9-20 belong to SPIPE
#define MO_DMA21_PTR1       0x300080 // {24}R0* DMA Current Ptr : Ch#21
#define MO_DMA22_PTR1       0x300084 // {24}R0* DMA Current Ptr : Ch#22
#define MO_DMA23_PTR1       0x300088 // {24}R0* DMA Current Ptr : Ch#23
#define MO_DMA24_PTR1       0x30008C // {24}R0* DMA Current Ptr : Ch#24
#define MO_DMA25_PTR1       0x300090 // {24}R0* DMA Current Ptr : Ch#25
#define MO_DMA26_PTR1       0x300094 // {24}R0* DMA Current Ptr : Ch#26
#define MO_DMA27_PTR1       0x300098 // {24}R0* DMA Current Ptr : Ch#27
#define MO_DMA28_PTR1       0x30009C // {24}R0* DMA Current Ptr : Ch#28
#define MO_DMA29_PTR1       0x3000A0 // {24}R0* DMA Current Ptr : Ch#29
#define MO_DMA30_PTR1       0x3000A4 // {24}R0* DMA Current Ptr : Ch#30
#define MO_DMA31_PTR1       0x3000A8 // {24}R0* DMA Current Ptr : Ch#31
#define MO_DMA32_PTR1       0x3000AC // {24}R0* DMA Current Ptr : Ch#32

#define MO_DMA21_PTR2       0x3000C0 // {24}RW* DMA Tab Ptr : Ch#21
#define MO_DMA22_PTR2       0x3000C4 // {24}RW* DMA Tab Ptr : Ch#22
#define MO_DMA23_PTR2       0x3000C8 // {24}RW* DMA Tab Ptr : Ch#23
#define MO_DMA24_PTR2       0x3000CC // {24}RW* DMA Tab Ptr : Ch#24
#define MO_DMA25_PTR2       0x3000D0 // {24}RW* DMA Tab Ptr : Ch#25
#define MO_DMA26_PTR2       0x3000D4 // {24}RW* DMA Tab Ptr : Ch#26
#define MO_DMA27_PTR2       0x3000D8 // {24}RW* DMA Tab Ptr : Ch#27
#define MO_DMA28_PTR2       0x3000DC // {24}RW* DMA Tab Ptr : Ch#28
#define MO_DMA29_PTR2       0x3000E0 // {24}RW* DMA Tab Ptr : Ch#29
#define MO_DMA30_PTR2       0x3000E4 // {24}RW* DMA Tab Ptr : Ch#30
#define MO_DMA31_PTR2       0x3000E8 // {24}RW* DMA Tab Ptr : Ch#31
#define MO_DMA32_PTR2       0x3000EC // {24}RW* DMA Tab Ptr : Ch#32

#define MO_DMA21_CNT1       0x300100 // {11}RW* DMA Buffer Size : Ch#21
#define MO_DMA22_CNT1       0x300104 // {11}RW* DMA Buffer Size : Ch#22
#define MO_DMA23_CNT1       0x300108 // {11}RW* DMA Buffer Size : Ch#23
#define MO_DMA24_CNT1       0x30010C // {11}RW* DMA Buffer Size : Ch#24
#define MO_DMA25_CNT1       0x300110 // {11}RW* DMA Buffer Size : Ch#25
#define MO_DMA26_CNT1       0x300114 // {11}RW* DMA Buffer Size : Ch#26
#define MO_DMA27_CNT1       0x300118 // {11}RW* DMA Buffer Size : Ch#27
#define MO_DMA28_CNT1       0x30011C // {11}RW* DMA Buffer Size : Ch#28
#define MO_DMA29_CNT1       0x300120 // {11}RW* DMA Buffer Size : Ch#29
#define MO_DMA30_CNT1       0x300124 // {11}RW* DMA Buffer Size : Ch#30
#define MO_DMA31_CNT1       0x300128 // {11}RW* DMA Buffer Size : Ch#31
#define MO_DMA32_CNT1       0x30012C // {11}RW* DMA Buffer Size : Ch#32

#define MO_DMA21_CNT2       0x300140 // {11}RW* DMA Table Size : Ch#21
#define MO_DMA22_CNT2       0x300144 // {11}RW* DMA Table Size : Ch#22
#define MO_DMA23_CNT2       0x300148 // {11}RW* DMA Table Size : Ch#23
#define MO_DMA24_CNT2       0x30014C // {11}RW* DMA Table Size : Ch#24
#define MO_DMA25_CNT2       0x300150 // {11}RW* DMA Table Size : Ch#25
#define MO_DMA26_CNT2       0x300154 // {11}RW* DMA Table Size : Ch#26
#define MO_DMA27_CNT2       0x300158 // {11}RW* DMA Table Size : Ch#27
#define MO_DMA28_CNT2       0x30015C // {11}RW* DMA Table Size : Ch#28
#define MO_DMA29_CNT2       0x300160 // {11}RW* DMA Table Size : Ch#29
#define MO_DMA30_CNT2       0x300164 // {11}RW* DMA Table Size : Ch#30
#define MO_DMA31_CNT2       0x300168 // {11}RW* DMA Table Size : Ch#31
#define MO_DMA32_CNT2       0x30016C // {11}RW* DMA Table Size : Ch#32


/* ---------------------------------------------------------------------- */
/* Video registers                                                        */

#define MO_VIDY_DMA         0x310000 // {64}RWp Video Y
#define MO_VIDU_DMA         0x310008 // {64}RWp Video U
#define MO_VIDV_DMA         0x310010 // {64}RWp Video V
#define MO_VBI_DMA          0x310018 // {64}RWp VBI (Vertical blanking interval)

#define MO_DEVICE_STATUS    0x310100
#define MO_INPUT_FORMAT     0x310104
#define MO_AGC_BURST        0x31010c
#define MO_CONTR_BRIGHT     0x310110
#define MO_UV_SATURATION    0x310114
#define MO_HUE              0x310118
#define MO_HTOTAL           0x310120
#define MO_HDELAY_EVEN      0x310124
#define MO_HDELAY_ODD       0x310128
#define MO_VDELAY_ODD       0x31012c
#define MO_VDELAY_EVEN      0x310130
#define MO_HACTIVE_EVEN     0x31013c
#define MO_HACTIVE_ODD      0x310140
#define MO_VACTIVE_EVEN     0x310144
#define MO_VACTIVE_ODD      0x310148
#define MO_HSCALE_EVEN      0x31014c
#define MO_HSCALE_ODD       0x310150
#define MO_VSCALE_EVEN      0x310154
#define MO_FILTER_EVEN      0x31015c
#define MO_VSCALE_ODD       0x310158
#define MO_FILTER_ODD       0x310160
#define MO_OUTPUT_FORMAT    0x310164

#define MO_PLL_REG          0x310168 // PLL register
#define MO_PLL_ADJ_CTRL     0x31016c // PLL adjust control register
#define MO_SCONV_REG        0x310170 // sample rate conversion register
#define MO_SCONV_FIFO       0x310174 // sample rate conversion fifo
#define MO_SUB_STEP         0x310178 // subcarrier step size
#define MO_SUB_STEP_DR      0x31017c // subcarrier step size for DR line

#define MO_CAPTURE_CTRL     0x310180 // capture control
#define MO_COLOR_CTRL       0x310184
#define MO_VBI_PACKET       0x310188 // vbi packet size / delay
#define MO_FIELD_COUNT      0x310190 // field counter
#define MO_VIP_CONFIG       0x310194
#define MO_VBOS_CONTROL	    0x3101a8

#define MO_AGC_BACK_VBI     0x310200
#define MO_AGC_SYNC_TIP1    0x310208

#define MO_VIDY_GPCNT       0x31C020 // {16}RO Video Y general purpose counter
#define MO_VIDU_GPCNT       0x31C024 // {16}RO Video U general purpose counter
#define MO_VIDV_GPCNT       0x31C028 // {16}RO Video V general purpose counter
#define MO_VBI_GPCNT        0x31C02C // {16}RO VBI general purpose counter
#define MO_VIDY_GPCNTRL     0x31C030 // {2}WO Video Y general purpose control
#define MO_VIDU_GPCNTRL     0x31C034 // {2}WO Video U general purpose control
#define MO_VIDV_GPCNTRL     0x31C038 // {2}WO Video V general purpose control
#define MO_VBI_GPCNTRL      0x31C03C // {2}WO VBI general purpose counter
#define MO_VID_DMACNTRL     0x31C040 // {8}RW Video DMA control
#define MO_VID_XFR_STAT     0x31C044 // {1}RO Video transfer status


/* ---------------------------------------------------------------------- */
/* audio registers                                                        */

#define MO_AUDD_DMA         0x320000 // {64}RWp Audio downstream
#define MO_AUDU_DMA         0x320008 // {64}RWp Audio upstream
#define MO_AUDR_DMA         0x320010 // {64}RWp Audio RDS (downstream)
#define MO_AUDD_GPCNT       0x32C020 // {16}RO Audio down general purpose counter
#define MO_AUDU_GPCNT       0x32C024 // {16}RO Audio up general purpose counter
#define MO_AUDR_GPCNT       0x32C028 // {16}RO Audio RDS general purpose counter
#define MO_AUDD_GPCNTRL     0x32C030 // {2}WO Audio down general purpose control
#define MO_AUDU_GPCNTRL     0x32C034 // {2}WO Audio up general purpose control
#define MO_AUDR_GPCNTRL     0x32C038 // {2}WO Audio RDS general purpose control
#define MO_AUD_DMACNTRL     0x32C040 // {6}RW Audio DMA control
#define MO_AUD_XFR_STAT     0x32C044 // {1}RO Audio transfer status
#define MO_AUDD_LNGTH       0x32C048 // {12}RW Audio down line length
#define MO_AUDR_LNGTH       0x32C04C // {12}RW Audio RDS line length

#define AUD_INIT                 0x320100
#define AUD_INIT_LD              0x320104
#define AUD_SOFT_RESET           0x320108
#define AUD_I2SINPUTCNTL         0x320120
#define AUD_BAUDRATE             0x320124
#define AUD_I2SOUTPUTCNTL        0x320128
#define AAGC_HYST                0x320134
#define AAGC_GAIN                0x320138
#define AAGC_DEF                 0x32013c
#define AUD_IIR1_0_SEL           0x320150
#define AUD_IIR1_0_SHIFT         0x320154
#define AUD_IIR1_1_SEL           0x320158
#define AUD_IIR1_1_SHIFT         0x32015c
#define AUD_IIR1_2_SEL           0x320160
#define AUD_IIR1_2_SHIFT         0x320164
#define AUD_IIR1_3_SEL           0x320168
#define AUD_IIR1_3_SHIFT         0x32016c
#define AUD_IIR1_4_SEL           0x320170
#define AUD_IIR1_4_SHIFT         0x32017c
#define AUD_IIR1_5_SEL           0x320180
#define AUD_IIR1_5_SHIFT         0x320184
#define AUD_IIR2_0_SEL           0x320190
#define AUD_IIR2_0_SHIFT         0x320194
#define AUD_IIR2_1_SEL           0x320198
#define AUD_IIR2_1_SHIFT         0x32019c
#define AUD_IIR2_2_SEL           0x3201a0
#define AUD_IIR2_2_SHIFT         0x3201a4
#define AUD_IIR2_3_SEL           0x3201a8
#define AUD_IIR2_3_SHIFT         0x3201ac
#define AUD_IIR3_0_SEL           0x3201c0
#define AUD_IIR3_0_SHIFT         0x3201c4
#define AUD_IIR3_1_SEL           0x3201c8
#define AUD_IIR3_1_SHIFT         0x3201cc
#define AUD_IIR3_2_SEL           0x3201d0
#define AUD_IIR3_2_SHIFT         0x3201d4
#define AUD_IIR4_0_SEL           0x3201e0
#define AUD_IIR4_0_SHIFT         0x3201e4
#define AUD_IIR4_1_SEL           0x3201e8
#define AUD_IIR4_1_SHIFT         0x3201ec
#define AUD_IIR4_2_SEL           0x3201f0
#define AUD_IIR4_2_SHIFT         0x3201f4
#define AUD_IIR4_0_CA0           0x320200
#define AUD_IIR4_0_CA1           0x320204
#define AUD_IIR4_0_CA2           0x320208
#define AUD_IIR4_0_CB0           0x32020c
#define AUD_IIR4_0_CB1           0x320210
#define AUD_IIR4_1_CA0           0x320214
#define AUD_IIR4_1_CA1           0x320218
#define AUD_IIR4_1_CA2           0x32021c
#define AUD_IIR4_1_CB0           0x320220
#define AUD_IIR4_1_CB1           0x320224
#define AUD_IIR4_2_CA0           0x320228
#define AUD_IIR4_2_CA1           0x32022c
#define AUD_IIR4_2_CA2           0x320230
#define AUD_IIR4_2_CB0           0x320234
#define AUD_IIR4_2_CB1           0x320238
#define AUD_HP_MD_IIR4_1         0x320250
#define AUD_HP_PROG_IIR4_1       0x320254
#define AUD_FM_MODE_ENABLE       0x320258
#define AUD_POLY0_DDS_CONSTANT   0x320270
#define AUD_DN0_FREQ             0x320274
#define AUD_DN1_FREQ             0x320278
#define AUD_DN1_FREQ_SHIFT       0x32027c
#define AUD_DN1_AFC              0x320280
#define AUD_DN1_SRC_SEL          0x320284
#define AUD_DN1_SHFT             0x320288
#define AUD_DN2_FREQ             0x32028c
#define AUD_DN2_FREQ_SHIFT       0x320290
#define AUD_DN2_AFC              0x320294
#define AUD_DN2_SRC_SEL          0x320298
#define AUD_DN2_SHFT             0x32029c
#define AUD_CRDC0_SRC_SEL        0x320300
#define AUD_CRDC0_SHIFT          0x320304
#define AUD_CORDIC_SHIFT_0       0x320308
#define AUD_CRDC1_SRC_SEL        0x32030c
#define AUD_CRDC1_SHIFT          0x320310
#define AUD_CORDIC_SHIFT_1       0x320314
#define AUD_DCOC_0_SRC           0x320320
#define AUD_DCOC0_SHIFT          0x320324
#define AUD_DCOC_0_SHIFT_IN0     0x320328
#define AUD_DCOC_0_SHIFT_IN1     0x32032c
#define AUD_DCOC_1_SRC           0x320330
#define AUD_DCOC1_SHIFT          0x320334
#define AUD_DCOC_1_SHIFT_IN0     0x320338
#define AUD_DCOC_1_SHIFT_IN1     0x32033c
#define AUD_DCOC_2_SRC           0x320340
#define AUD_DCOC2_SHIFT          0x320344
#define AUD_DCOC_2_SHIFT_IN0     0x320348
#define AUD_DCOC_2_SHIFT_IN1     0x32034c
#define AUD_DCOC_PASS_IN         0x320350
#define AUD_PDET_SRC             0x320370
#define AUD_PDET_SHIFT           0x320374
#define AUD_PILOT_BQD_1_K0       0x320380
#define AUD_PILOT_BQD_1_K1       0x320384
#define AUD_PILOT_BQD_1_K2       0x320388
#define AUD_PILOT_BQD_1_K3       0x32038c
#define AUD_PILOT_BQD_1_K4       0x320390
#define AUD_PILOT_BQD_2_K0       0x320394
#define AUD_PILOT_BQD_2_K1       0x320398
#define AUD_PILOT_BQD_2_K2       0x32039c
#define AUD_PILOT_BQD_2_K3       0x3203a0
#define AUD_PILOT_BQD_2_K4       0x3203a4
#define AUD_THR_FR               0x3203c0
#define AUD_X_PROG               0x3203c4
#define AUD_Y_PROG               0x3203c8
#define AUD_HARMONIC_MULT        0x3203cc
#define AUD_C1_UP_THR            0x3203d0
#define AUD_C1_LO_THR            0x3203d4
#define AUD_C2_UP_THR            0x3203d8
#define AUD_C2_LO_THR            0x3203dc
#define AUD_PLL_EN               0x320400
#define AUD_PLL_SRC              0x320404
#define AUD_PLL_SHIFT            0x320408
#define AUD_PLL_IF_SEL           0x32040c
#define AUD_PLL_IF_SHIFT         0x320410
#define AUD_BIQUAD_PLL_K0        0x320414
#define AUD_BIQUAD_PLL_K1        0x320418
#define AUD_BIQUAD_PLL_K2        0x32041c
#define AUD_BIQUAD_PLL_K3        0x320420
#define AUD_BIQUAD_PLL_K4        0x320424
#define AUD_DEEMPH0_SRC_SEL      0x320440
#define AUD_DEEMPH0_SHIFT        0x320444
#define AUD_DEEMPH0_G0           0x320448
#define AUD_DEEMPH0_A0           0x32044c
#define AUD_DEEMPH0_B0           0x320450
#define AUD_DEEMPH0_A1           0x320454
#define AUD_DEEMPH0_B1           0x320458
#define AUD_DEEMPH1_SRC_SEL      0x32045c
#define AUD_DEEMPH1_SHIFT        0x320460
#define AUD_DEEMPH1_G0           0x320464
#define AUD_DEEMPH1_A0           0x320468
#define AUD_DEEMPH1_B0           0x32046c
#define AUD_DEEMPH1_A1           0x320470
#define AUD_DEEMPH1_B1           0x320474
#define AUD_OUT0_SEL             0x320490
#define AUD_OUT0_SHIFT           0x320494
#define AUD_OUT1_SEL             0x320498
#define AUD_OUT1_SHIFT           0x32049c
#define AUD_RDSI_SEL             0x3204a0
#define AUD_RDSI_SHIFT           0x3204a4
#define AUD_RDSQ_SEL             0x3204a8
#define AUD_RDSQ_SHIFT           0x3204ac
#define AUD_DBX_IN_GAIN          0x320500
#define AUD_DBX_WBE_GAIN         0x320504
#define AUD_DBX_SE_GAIN          0x320508
#define AUD_DBX_RMS_WBE          0x32050c
#define AUD_DBX_RMS_SE           0x320510
#define AUD_DBX_SE_BYPASS        0x320514
#define AUD_FAWDETCTL            0x320530
#define AUD_FAWDETWINCTL         0x320534
#define AUD_DEEMPHGAIN_R         0x320538
#define AUD_DEEMPHNUMER1_R       0x32053c
#define AUD_DEEMPHNUMER2_R       0x320540
#define AUD_DEEMPHDENOM1_R       0x320544
#define AUD_DEEMPHDENOM2_R       0x320548
#define AUD_ERRLOGPERIOD_R       0x32054c
#define AUD_ERRINTRPTTHSHLD1_R   0x320550
#define AUD_ERRINTRPTTHSHLD2_R   0x320554
#define AUD_ERRINTRPTTHSHLD3_R   0x320558
#define AUD_NICAM_STATUS1        0x32055c
#define AUD_NICAM_STATUS2        0x320560
#define AUD_ERRLOG1              0x320564
#define AUD_ERRLOG2              0x320568
#define AUD_ERRLOG3              0x32056c
#define AUD_DAC_BYPASS_L         0x320580
#define AUD_DAC_BYPASS_R         0x320584
#define AUD_DAC_BYPASS_CTL       0x320588
#define AUD_CTL                  0x32058c
#define AUD_STATUS               0x320590
#define AUD_VOL_CTL              0x320594
#define AUD_BAL_CTL              0x320598
#define AUD_START_TIMER          0x3205b0
#define AUD_MODE_CHG_TIMER       0x3205b4
#define AUD_POLYPH80SCALEFAC     0x3205b8
#define AUD_DMD_RA_DDS           0x3205bc
#define AUD_I2S_RA_DDS           0x3205c0
#define AUD_RATE_THRES_DMD       0x3205d0
#define AUD_RATE_THRES_I2S       0x3205d4
#define AUD_RATE_ADJ1            0x3205d8
#define AUD_RATE_ADJ2            0x3205dc
#define AUD_RATE_ADJ3            0x3205e0
#define AUD_RATE_ADJ4            0x3205e4
#define AUD_RATE_ADJ5            0x3205e8
#define AUD_APB_IN_RATE_ADJ      0x3205ec
#define AUD_I2SCNTL              0x3205ec
#define AUD_PHASE_FIX_CTL        0x3205f0
#define AUD_PLL_PRESCALE         0x320600
#define AUD_PLL_DDS              0x320604
#define AUD_PLL_INT              0x320608
#define AUD_PLL_FRAC             0x32060c
#define AUD_PLL_JTAG             0x320620
#define AUD_PLL_SPMP             0x320624
#define AUD_AFE_12DB_EN          0x320628

// Audio QAM Register Addresses
#define AUD_PDF_DDS_CNST_BYTE2   0x320d01
#define AUD_PDF_DDS_CNST_BYTE1   0x320d02
#define AUD_PDF_DDS_CNST_BYTE0   0x320d03
#define AUD_PHACC_FREQ_8MSB      0x320d2a
#define AUD_PHACC_FREQ_8LSB      0x320d2b
#define AUD_QAM_MODE             0x320d04


/* ---------------------------------------------------------------------- */
/* transport stream registers                                             */

#define MO_TS_DMA           0x330000 // {64}RWp Transport stream downstream
#define MO_TS_GPCNT         0x33C020 // {16}RO TS general purpose counter
#define MO_TS_GPCNTRL       0x33C030 // {2}WO TS general purpose control
#define MO_TS_DMACNTRL      0x33C040 // {6}RW TS DMA control
#define MO_TS_XFR_STAT      0x33C044 // {1}RO TS transfer status
#define MO_TS_LNGTH         0x33C048 // {12}RW TS line length

#define TS_HW_SOP_CNTRL     0x33C04C
#define TS_GEN_CNTRL        0x33C050
#define TS_BD_PKT_STAT      0x33C054
#define TS_SOP_STAT         0x33C058
#define TS_FIFO_OVFL_STAT   0x33C05C
#define TS_VALERR_CNTRL     0x33C060


/* ---------------------------------------------------------------------- */
/* VIP registers                                                          */

#define MO_VIPD_DMA         0x340000 // {64}RWp VIP downstream
#define MO_VIPU_DMA         0x340008 // {64}RWp VIP upstream
#define MO_VIPD_GPCNT       0x34C020 // {16}RO VIP down general purpose counter
#define MO_VIPU_GPCNT       0x34C024 // {16}RO VIP up general purpose counter
#define MO_VIPD_GPCNTRL     0x34C030 // {2}WO VIP down general purpose control
#define MO_VIPU_GPCNTRL     0x34C034 // {2}WO VIP up general purpose control
#define MO_VIP_DMACNTRL     0x34C040 // {6}RW VIP DMA control
#define MO_VIP_XFR_STAT     0x34C044 // {1}RO VIP transfer status
#define MO_VIP_CFG          0x340048 // VIP configuration
#define MO_VIPU_CNTRL       0x34004C // VIP upstream control #1
#define MO_VIPD_CNTRL       0x340050 // VIP downstream control #2
#define MO_VIPD_LNGTH       0x340054 // VIP downstream line length
#define MO_VIP_BRSTLN       0x340058 // VIP burst length
#define MO_VIP_INTCNTRL     0x34C05C // VIP Interrupt Control
#define MO_VIP_XFTERM       0x340060 // VIP transfer terminate


/* ---------------------------------------------------------------------- */
/* misc registers                                                         */

#define MO_M2M_DMA          0x350000 // {64}RWp Mem2Mem DMA Bfr
#define MO_GP0_IO           0x350010 // {32}RW* GPIOoutput enablesdata I/O
#define MO_GP1_IO           0x350014 // {32}RW* GPIOoutput enablesdata I/O
#define MO_GP2_IO           0x350018 // {32}RW* GPIOoutput enablesdata I/O
#define MO_GP3_IO           0x35001C // {32}RW* GPIO Mode/Ctrloutput enables
#define MO_GPIO             0x350020 // {32}RW* GPIO I2C Ctrldata I/O
#define MO_GPOE             0x350024 // {32}RW  GPIO I2C Ctrloutput enables
#define MO_GP_ISM           0x350028 // {16}WO  GPIO Intr Sens/Pol

#define MO_PLL_B            0x35C008 // {32}RW* PLL Control for ASB bus clks
#define MO_M2M_CNT          0x35C024 // {32}RW  Mem2Mem DMA Cnt
#define MO_M2M_XSUM         0x35C028 // {32}RO  M2M XOR-Checksum
#define MO_CRC              0x35C02C // {16}RW  CRC16 init/result
#define MO_CRC_D            0x35C030 // {32}WO  CRC16 new data in
#define MO_TM_CNT_LDW       0x35C034 // {32}RO  Timer : Counter low dword
#define MO_TM_CNT_UW        0x35C038 // {16}RO  Timer : Counter high word
#define MO_TM_LMT_LDW       0x35C03C // {32}RW  Timer : Limit low dword
#define MO_TM_LMT_UW        0x35C040 // {32}RW  Timer : Limit high word
#define MO_PINMUX_IO        0x35C044 // {8}RW  Pin Mux Control
#define MO_TSTSEL_IO        0x35C048 // {2}RW  Pin Mux Control
#define MO_AFECFG_IO        0x35C04C // AFE configuration reg
#define MO_DDS_IO           0x35C050 // DDS Increment reg
#define MO_DDSCFG_IO        0x35C054 // DDS Configuration reg
#define MO_SAMPLE_IO        0x35C058 // IRIn sample reg
#define MO_SRST_IO          0x35C05C // Output system reset reg

#define MO_INT1_MSK         0x35C060 // DMA RISC interrupt mask
#define MO_INT1_STAT        0x35C064 // DMA RISC interrupt status
#define MO_INT1_MSTAT       0x35C068 // DMA RISC interrupt masked status


/* ---------------------------------------------------------------------- */
/* i2c bus registers                                                      */

#define MO_I2C              0x368000 // I2C data/control
#define MO_I2C_DIV          (0xf<<4)
#define MO_I2C_SYNC         (1<<3)
#define MO_I2C_W3B          (1<<2)
#define MO_I2C_SCL          (1<<1)
#define MO_I2C_SDA          (1<<0)


/* ---------------------------------------------------------------------- */
/* general purpose host registers                                         */
/* FIXME: tyops?  s/0x35/0x38/ ??                                         */

#define MO_GPHSTD_DMA       0x350000 // {64}RWp Host downstream
#define MO_GPHSTU_DMA       0x350008 // {64}RWp Host upstream
#define MO_GPHSTU_CNTRL     0x380048 // Host upstream control #1
#define MO_GPHSTD_CNTRL     0x38004C // Host downstream control #2
#define MO_GPHSTD_LNGTH     0x380050 // Host downstream line length
#define MO_GPHST_WSC        0x380054 // Host wait state control
#define MO_GPHST_XFR        0x380058 // Host transfer control
#define MO_GPHST_WDTH       0x38005C // Host interface width
#define MO_GPHST_HDSHK      0x380060 // Host peripheral handshake
#define MO_GPHST_MUX16      0x380064 // Host muxed 16-bit transfer parameters
#define MO_GPHST_MODE       0x380068 // Host mode select

#define MO_GPHSTD_GPCNT     0x35C020 // Host down general purpose counter
#define MO_GPHSTU_GPCNT     0x35C024 // Host up general purpose counter
#define MO_GPHSTD_GPCNTRL   0x38C030 // Host down general purpose control
#define MO_GPHSTU_GPCNTRL   0x38C034 // Host up general purpose control
#define MO_GPHST_DMACNTRL   0x38C040 // Host DMA control
#define MO_GPHST_XFR_STAT   0x38C044 // Host transfer status
#define MO_GPHST_SOFT_RST   0x38C06C // Host software reset


/* ---------------------------------------------------------------------- */
/* RISC instructions                                                      */

#define RISC_SYNC		 0x80000000
#define RISC_SYNC_ODD		 0x80000000
#define RISC_SYNC_EVEN		 0x80000200
#define RISC_RESYNC		 0x80008000
#define RISC_RESYNC_ODD		 0x80008000
#define RISC_RESYNC_EVEN	 0x80008200
#define RISC_WRITE		 0x10000000
#define RISC_WRITEC		 0x50000000
#define RISC_READ		 0x90000000
#define RISC_READC		 0xA0000000
#define RISC_JUMP		 0x70000000
#define RISC_SKIP		 0x20000000
#define RISC_WRITERM		 0xB0000000
#define RISC_WRITECM		 0xC0000000
#define RISC_WRITECR		 0xD0000000
#define RISC_IMM		 0x00000001

#define RISC_SOL		 0x08000000
#define RISC_EOL		 0x04000000

#define RISC_IRQ2		 0x02000000
#define RISC_IRQ1		 0x01000000

#define RISC_CNT_NONE		 0x00000000
#define RISC_CNT_INC		 0x00010000
#define RISC_CNT_RSVR		 0x00020000
#define RISC_CNT_RESET		 0x00030000
#define RISC_JMP_SRP         	 0x01


/* ---------------------------------------------------------------------- */
/* various constants                                                      */

#define SEL_BTSC     0x01
#define SEL_EIAJ     0x02
#define SEL_A2       0x04
#define SEL_SAP      0x08
#define SEL_NICAM    0x10
#define SEL_FMRADIO  0x20

// AUD_CTL
#define EN_BTSC_FORCE_MONO      0
#define EN_BTSC_FORCE_STEREO    1
#define EN_BTSC_FORCE_SAP       2
#define EN_BTSC_AUTO_STEREO     3
#define EN_BTSC_AUTO_SAP        4

#define EN_A2_FORCE_MONO1       8
#define EN_A2_FORCE_MONO2       9
#define EN_A2_FORCE_STEREO      10
#define EN_A2_AUTO_MONO2        11
#define EN_A2_AUTO_STEREO       12

#define EN_EIAJ_FORCE_MONO1     16
#define EN_EIAJ_FORCE_MONO2     17
#define EN_EIAJ_FORCE_STEREO    18
#define EN_EIAJ_AUTO_MONO2      19
#define EN_EIAJ_AUTO_STEREO     20

#define EN_NICAM_FORCE_MONO1    32
#define EN_NICAM_FORCE_MONO2    33
#define EN_NICAM_FORCE_STEREO   34
#define EN_NICAM_AUTO_MONO2     35
#define EN_NICAM_AUTO_STEREO    36

#define EN_FMRADIO_FORCE_MONO   24
#define EN_FMRADIO_FORCE_STEREO 25
#define EN_FMRADIO_AUTO_STEREO  26

#define EN_NICAM_AUTO_FALLBACK  0x00000040
#define EN_FMRADIO_EN_RDS       0x00000200
#define EN_NICAM_TRY_AGAIN_BIT  0x00000400
#define EN_DAC_ENABLE           0x00001000
#define EN_I2SOUT_ENABLE        0x00002000
#define EN_I2SIN_STR2DAC        0x00004000
#define EN_I2SIN_ENABLE         0x00008000

#define EN_DMTRX_SUMDIFF        (0 << 7)
#define EN_DMTRX_SUMR           (1 << 7)
#define EN_DMTRX_LR             (2 << 7)
#define EN_DMTRX_MONO           (3 << 7)
#define EN_DMTRX_BYPASS         (1 << 11)

// Video
#define VID_CAPTURE_CONTROL		0x310180

#define CX23880_CAP_CTL_CAPTURE_VBI_ODD  (1<<3)
#define CX23880_CAP_CTL_CAPTURE_VBI_EVEN (1<<2)
#define CX23880_CAP_CTL_CAPTURE_ODD      (1<<1)
#define CX23880_CAP_CTL_CAPTURE_EVEN     (1<<0)

#define VideoInputMux0		 0x0
#define VideoInputMux1		 0x1
#define VideoInputMux2		 0x2
#define VideoInputMux3		 0x3
#define VideoInputTuner		 0x0
#define VideoInputComposite	 0x1
#define VideoInputSVideo	 0x2
#define VideoInputOther		 0x3

#define Xtal0		 0x1
#define Xtal1		 0x2
#define XtalAuto	 0x3

#define VideoFormatAuto		 0x0
#define VideoFormatNTSC		 0x1
#define VideoFormatNTSCJapan	 0x2
#define VideoFormatNTSC443	 0x3
#define VideoFormatPAL		 0x4
#define VideoFormatPALB		 0x4
#define VideoFormatPALD		 0x4
#define VideoFormatPALG		 0x4
#define VideoFormatPALH		 0x4
#define VideoFormatPALI		 0x4
#define VideoFormatPALBDGHI	 0x4
#define VideoFormatPALM		 0x5
#define VideoFormatPALN		 0x6
#define VideoFormatPALNC	 0x7
#define VideoFormatPAL60	 0x8
#define VideoFormatSECAM	 0x9

#define VideoFormatAuto27MHz		 0x10
#define VideoFormatNTSC27MHz		 0x11
#define VideoFormatNTSCJapan27MHz	 0x12
#define VideoFormatNTSC44327MHz		 0x13
#define VideoFormatPAL27MHz		 0x14
#define VideoFormatPALB27MHz		 0x14
#define VideoFormatPALD27MHz		 0x14
#define VideoFormatPALG27MHz		 0x14
#define VideoFormatPALH27MHz		 0x14
#define VideoFormatPALI27MHz		 0x14
#define VideoFormatPALBDGHI27MHz	 0x14
#define VideoFormatPALM27MHz		 0x15
#define VideoFormatPALN27MHz		 0x16
#define VideoFormatPALNC27MHz		 0x17
#define VideoFormatPAL6027MHz		 0x18
#define VideoFormatSECAM27MHz		 0x19

#define NominalUSECAM	 0x87
#define NominalVSECAM	 0x85
#define NominalUNTSC	 0xFE
#define NominalVNTSC	 0xB4

#define NominalContrast  0xD8

#define HFilterAutoFormat	 0x0
#define HFilterCIF		 0x1
#define HFilterQCIF		 0x2
#define HFilterICON		 0x3

#define VFilter2TapInterpolate  0
#define VFilter3TapInterpolate  1
#define VFilter4TapInterpolate  2
#define VFilter5TapInterpolate  3
#define VFilter2TapNoInterpolate  4
#define VFilter3TapNoInterpolate  5
#define VFilter4TapNoInterpolate  6
#define VFilter5TapNoInterpolate  7

#define ColorFormatRGB32	 0x0000
#define ColorFormatRGB24	 0x0011
#define ColorFormatRGB16	 0x0022
#define ColorFormatRGB15	 0x0033
#define ColorFormatYUY2		 0x0044
#define ColorFormatBTYUV	 0x0055
#define ColorFormatY8		 0x0066
#define ColorFormatRGB8		 0x0077
#define ColorFormatPL422	 0x0088
#define ColorFormatPL411	 0x0099
#define ColorFormatYUV12	 0x00AA
#define ColorFormatYUV9		 0x00BB
#define ColorFormatRAW		 0x00EE
#define ColorFormatBSWAP         0x0300
#define ColorFormatWSWAP         0x0c00
#define ColorFormatEvenMask      0x050f
#define ColorFormatOddMask       0x0af0
#define ColorFormatGamma         0x1000

#define Interlaced		 0x1
#define NonInterlaced	 	 0x0

#define FieldEven		 0x1
#define FieldOdd		 0x0

#define TGReadWriteMode	 	 0x0
#define TGEnableMode	 	 0x1

#define DV_CbAlign		 0x0
#define DV_Y0Align		 0x1
#define DV_CrAlign		 0x2
#define DV_Y1Align		 0x3

#define DVF_Analog		 0x0
#define DVF_CCIR656		 0x1
#define DVF_ByteStream		 0x2
#define DVF_ExtVSYNC		 0x4
#define DVF_ExtField		 0x5

#define CHANNEL_VID_Y		 0x1
#define CHANNEL_VID_U		 0x2
#define CHANNEL_VID_V		 0x3
#define CHANNEL_VID_VBI		 0x4
#define CHANNEL_AUD_DN		 0x5
#define CHANNEL_AUD_UP		 0x6
#define CHANNEL_AUD_RDS_DN	 0x7
#define CHANNEL_MPEG_DN		 0x8
#define CHANNEL_VIP_DN		 0x9
#define CHANNEL_VIP_UP		 0xA
#define CHANNEL_HOST_DN		 0xB
#define CHANNEL_HOST_UP		 0xC
#define CHANNEL_FIRST		 0x1
#define CHANNEL_LAST		 0xC

#define GP_COUNT_CONTROL_NONE		 0x0
#define GP_COUNT_CONTROL_INC		 0x1
#define GP_COUNT_CONTROL_RESERVED	 0x2
#define GP_COUNT_CONTROL_RESET		 0x3

#define PLL_PRESCALE_BY_2  2
#define PLL_PRESCALE_BY_3  3
#define PLL_PRESCALE_BY_4  4
#define PLL_PRESCALE_BY_5  5

#define HLNotchFilter4xFsc	 0
#define HLNotchFilterSquare	 1
#define HLNotchFilter135NTSC	 2
#define HLNotchFilter135PAL	 3

#define NTSC_8x_SUB_CARRIER  28.63636E6
#define PAL_8x_SUB_CARRIER  35.46895E6

// Default analog settings
#define DEFAULT_HUE_NTSC			0x00
#define DEFAULT_BRIGHTNESS_NTSC			0x00
#define DEFAULT_CONTRAST_NTSC			0x39
#define DEFAULT_SAT_U_NTSC			0x7F
#define DEFAULT_SAT_V_NTSC			0x5A

typedef enum
{
	SOURCE_TUNER = 0,
	SOURCE_COMPOSITE,
	SOURCE_SVIDEO,
	SOURCE_OTHER1,
	SOURCE_OTHER2,
	SOURCE_COMPVIASVIDEO,
	SOURCE_CCIR656
} VIDEOSOURCETYPE;

#endif /* _CX88_REG_H_ */
