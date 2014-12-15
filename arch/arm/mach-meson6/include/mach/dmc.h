#define CANVAS_ADDR_LMASK       0x1fffffff
#define CANVAS_WIDTH_LMASK      0x7
#define CANVAS_WIDTH_LWID       3
#define CANVAS_WIDTH_LBIT       29

#define CANVAS_WIDTH_HMASK      0x1ff
#define CANVAS_WIDTH_HBIT       0
#define CANVAS_HEIGHT_MASK      0x1fff
#define CANVAS_HEIGHT_BIT       9
#define CANVAS_YWRAP            (1<<23)
#define CANVAS_XWRAP            (1<<22)
#define CANVAS_ADDR_NOWRAP      0x00
#define CANVAS_ADDR_WRAPX       0x01
#define CANVAS_ADDR_WRAPY       0x02
#define CANVAS_BLKMODE_MASK     3
#define CANVAS_BLKMODE_BIT      24
#define CANVAS_BLKMODE_LINEAR   0x00
#define CANVAS_BLKMODE_32X32    0x01
#define CANVAS_BLKMODE_64X32    0x02

#define CANVAS_LUT_INDEX_BIT    0
#define CANVAS_LUT_INDEX_MASK   0x7
#define CANVAS_LUT_WR_EN        (0x2 << 8)
#define CANVAS_LUT_RD_EN        (0x1 << 8)

#define MMC_PHY_CTRL              0x1380

/****************logo relative part *************************************************/
#define ASSIST_MBOX1_CLR_REG VDEC_ASSIST_MBOX1_CLR_REG
#define ASSIST_MBOX1_MASK VDEC_ASSIST_MBOX1_MASK
#define RESET_PSCALE        (1<<4)
#define RESET_IQIDCT        (1<<2)
#define RESET_MC            (1<<3)
#define MEM_BUFCTRL_MANUAL		(1<<1)
#define MEM_BUFCTRL_INIT		(1<<0)
#define MEM_LEVEL_CNT_BIT       18
#define MEM_FIFO_CNT_BIT        16
#define MEM_FILL_ON_LEVEL		(1<<10)
#define MEM_CTRL_EMPTY_EN		(1<<2)
#define MEM_CTRL_FILL_EN		(1<<1)
#define MEM_CTRL_INIT			(1<<0)


#if 0
#ifdef DDR_DMC
#else

#define DDR_DMC
#define MMC_DDR_CTRL        0x1000
#define MMC_REQ_CTRL        0x1004
#define MMC_ARB_CTRL        0x1008
#define MMC_ARB_CTRL1       0x100c

#define MMC_QOS0_CTRL  0x1010
//bit 31     qos enable.
//bit 26     1 : danamic change the bandwidth percentage. 0 : fixed bandwidth.  all 64.
//bit 25       grant mode. 1 grant clock cycles. 0 grant data cycles.
//bit 24       leakybucket counter goes to 0. When no req or no other request.
//bit 21:16    bankwidth requirement. unit 1/64.
//bit 15:0.    after stop the re_enable threadhold.

#define MMC_QOS0_MAX   0x1014
#define MMC_QOS0_MIN   0x1018
#define MMC_QOS0_LIMIT 0x101c
#define MMC_QOS0_STOP  0x1020
#define MMC_QOS1_CTRL  0x1024
#define MMC_QOS1_MAX   0x1028
#define MMC_QOS1_MIN   0x102c
#define MMC_QOS1_STOP  0x1030
#define MMC_QOS1_LIMIT 0x1034
#define MMC_QOS2_CTRL  0x1038
#define MMC_QOS2_MAX   0x103c
#define MMC_QOS2_MIN   0x1040
#define MMC_QOS2_STOP  0x1044
#define MMC_QOS2_LIMIT 0x1048
#define MMC_QOS3_CTRL  0x104c
#define MMC_QOS3_MAX   0x1050
#define MMC_QOS3_MIN   0x1054
#define MMC_QOS3_STOP  0x1058
#define MMC_QOS3_LIMIT 0x105c
#define MMC_QOS4_CTRL  0x1060
#define MMC_QOS4_MAX   0x1064
#define MMC_QOS4_MIN   0x1068
#define MMC_QOS4_STOP  0x106c
#define MMC_QOS4_LIMIT 0x1070
#define MMC_QOS5_CTRL  0x1074
#define MMC_QOS5_MAX   0x1078
#define MMC_QOS5_MIN   0x107c
#define MMC_QOS5_STOP  0x1080
#define MMC_QOS5_LIMIT 0x1084
#define MMC_QOS6_CTRL  0x1088
#define MMC_QOS6_MAX   0x108c
#define MMC_QOS6_MIN   0x1090
#define MMC_QOS6_STOP  0x1094
#define MMC_QOS6_LIMIT 0x1098
#define MMC_QOS7_CTRL  0x109c
#define MMC_QOS7_MAX   0x10a0
#define MMC_QOS7_MIN   0x10a4
#define MMC_QOS7_STOP  0x10a8
#define MMC_QOS7_LIMIT 0x10ac

#define MMC_QOSMON_CTRL     0x10b0
#define MMC_QOSMON_TIM      0x10b4
#define MMC_QOSMON_MST      0x10b8
#define MMC_MON_CLKCNT      0x10bc
#define MMC_ALL_REQCNT      0x10c0
#define MMC_ALL_GANTCNT     0x10c4
#define MMC_ONE_REQCNT      0x10c8
#define MMC_ONE_CYCLE_CNT   0x10cc
#define MMC_ONE_DATA_CNT    0x10d0



#define DC_CAV_CTRL               0x1300

#define DC_CAV_LVL3_GRANT         0x1304
#define DC_CAV_LVL3_GH            0x1308
// this is a 32 bit grant regsiter.
// each bit grant a thread ID for LVL3 use.

#define DC_CAV_LVL3_FLIP          0x130c
#define DC_CAV_LVL3_FH            0x1310
// this is a 32 bit FLIP regsiter.
// each bit to define  a thread ID for LVL3 use.

#define DC_CAV_LVL3_CTRL0         0x1314
#define DC_CAV_LVL3_CTRL1         0x1318
#define DC_CAV_LVL3_CTRL2         0x131c
#define DC_CAV_LVL3_CTRL3         0x1320
#define DC_CAV_LUT_DATAL          0x1324
#define CANVAS_ADDR_LMASK       0x1fffffff
#define CANVAS_WIDTH_LMASK      0x7
#define CANVAS_WIDTH_LWID       3
#define CANVAS_WIDTH_LBIT       29
#define DC_CAV_LUT_DATAH          0x1328
#define CANVAS_WIDTH_HMASK      0x1ff
#define CANVAS_WIDTH_HBIT       0
#define CANVAS_HEIGHT_MASK      0x1fff
#define CANVAS_HEIGHT_BIT       9
#define CANVAS_YWRAP            (1<<23)
#define CANVAS_XWRAP            (1<<22)
#define CANVAS_ADDR_NOWRAP      0x00
#define CANVAS_ADDR_WRAPX       0x01
#define CANVAS_ADDR_WRAPY       0x02
#define CANVAS_BLKMODE_MASK     3
#define CANVAS_BLKMODE_BIT      24
#define CANVAS_BLKMODE_LINEAR   0x00
#define CANVAS_BLKMODE_32X32    0x01
#define CANVAS_BLKMODE_64X32    0x02
#define DC_CAV_LUT_ADDR           0x132c
#define CANVAS_LUT_INDEX_BIT    0
#define CANVAS_LUT_INDEX_MASK   0x7
#define CANVAS_LUT_WR_EN        (0x2 << 8)
#define CANVAS_LUT_RD_EN        (0x1 << 8)
#define DC_CAV_LVL3_MODE          0x1330
#define MMC_PROT_ADDR             0x1334
#define MMC_PROT_SELH             0x1338
#define MMC_PROT_SELL             0x133c
#define MMC_PROT_CTL_STS          0x1340
#define MMC_INT_STS               0x1344
#define MMC_PHY_CTRL              0x1380
#define MMC_APB3_CTRL             0x1384

#define MMC_REQ0_CTRL             0x1388
// bit 31,            request in enable.
// 30:24:             cmd fifo counter when request generate to dmc arbitor if there's no lbrst.
// 23:16:             waiting time when request generate to dmc arbitor if there's o lbrst.
// 15:8:              how many write rsp can hold in the whole dmc pipe lines.
// 7:0:               how many read data can hold in the whole dmc pipe lines.

#define MMC_REQ1_CTRL             0x138c
#define MMC_REQ2_CTRL             0x1390
#define MMC_REQ3_CTRL             0x1394
#define MMC_REQ4_CTRL             0x1398
#define MMC_REQ5_CTRL             0x139c
#define MMC_REQ6_CTRL             0x13a0
#define MMC_REQ7_CTRL             0x13a4
/****************logo relative part *************************************************/
#define ASSIST_MBOX1_CLR_REG VDEC_ASSIST_MBOX1_CLR_REG
#define ASSIST_MBOX1_MASK VDEC_ASSIST_MBOX1_MASK
#define RESET_PSCALE        (1<<4)
#define RESET_IQIDCT        (1<<2)
#define RESET_MC            (1<<3)
#define MEM_BUFCTRL_MANUAL		(1<<1)
#define MEM_BUFCTRL_INIT		(1<<0)
#define MEM_LEVEL_CNT_BIT       18
#define MEM_FIFO_CNT_BIT        16
#define MEM_FILL_ON_LEVEL		(1<<10)
#define MEM_CTRL_EMPTY_EN		(1<<2)
#define MEM_CTRL_FILL_EN		(1<<1)
#define MEM_CTRL_INIT			(1<<0)
#endif
#endif