//#include "register.h"
//#include "c_always_on_pointer.h"
#include "hdmi.h"
#include "hdmirx.h"
#include "hdmirx_parameter.h"
//#include "c_stimulus.h"

#define Wr(reg,val) WRITE_MPEG_REG(reg,val)
#define Rd(reg)   READ_MPEG_REG(reg)
#define Wr_reg_bits(reg, val, start, len) \
  Wr(reg, (Rd(reg) & ~(((1L<<(len))-1)<<(start)))|((unsigned int)(val) << (start)))

void hdmirx_wr_top (unsigned long addr, unsigned long data);
void hdmi_rx_ctrl_write (unsigned long addr, unsigned long data);
void debug_phy_write (unsigned long addr, unsigned long data);
unsigned hdmirx_rd_top (unsigned long addr);
unsigned hdmi_rx_ctrl_read (unsigned long addr);
unsigned debug_phy_read (unsigned long addr);

void hdmirx_wr_only_TOP (unsigned long addr, unsigned long data)
{
    hdmirx_wr_top(addr, data);
   /*
    unsigned long dev_offset = 0;       // TOP ADDR_PORT: 0xc800e000; DWC ADDR_PORT: 0xc800e010
    *((volatile unsigned long *) (HDMIRX_ADDR_PORT+dev_offset)) = addr;
    *((volatile unsigned long *) (HDMIRX_DATA_PORT+dev_offset)) = data;
    */
} /* hdmirx_wr_only_TOP */

void hdmirx_wr_only_DWC (unsigned long addr, unsigned long data)
{
    hdmi_rx_ctrl_write(addr, data);
    /*
    unsigned long dev_offset = 0x10;    // TOP ADDR_PORT: 0xc800e000; DWC ADDR_PORT: 0xc800e010
    *((volatile unsigned long *) (HDMIRX_ADDR_PORT+dev_offset)) = addr;
    *((volatile unsigned long *) (HDMIRX_DATA_PORT+dev_offset)) = data;
    */
} /* hdmirx_wr_only_DWC */

void hdmirx_wr_only_PHY (unsigned long addr, unsigned long data)
{
    debug_phy_write(addr, data);
    /*
    hdmirx_wr_only_DWC( HDMIRX_DWC_I2CM_PHYG3_ADDRESS,  addr & 0xff);
    hdmirx_wr_only_DWC( HDMIRX_DWC_I2CM_PHYG3_DATAO,    data & 0xffff);
    hdmirx_wr_only_DWC( HDMIRX_DWC_I2CM_PHYG3_OPERATION,0x1);
    Wr(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 400 ) {}
    hdmirx_poll_DWC(    HDMIRX_DWC_HDMI_ISTS,           1<<28, ~(1<<28), 0);
    hdmirx_wr_only_DWC( HDMIRX_DWC_HDMI_ICLR,           1<<28);
    hdmirx_poll_DWC(    HDMIRX_DWC_HDMI_ISTS,           0<<28, ~(1<<28), 0);
    */
} /* hdmirx_wr_only_PHY */

void hdmirx_wr_only_reg (unsigned char dev_id, unsigned long addr, unsigned long data)
{
    if (dev_id == HDMIRX_DEV_ID_TOP) {
        hdmirx_wr_only_TOP(addr, data);
    } else if (dev_id == HDMIRX_DEV_ID_DWC) {
        hdmirx_wr_only_DWC(addr, data);
    } else if (dev_id == HDMIRX_DEV_ID_PHY) {
        hdmirx_wr_only_PHY(addr, data);
    } else {
        stimulus_print("Error: hdmirx_wr_only_reg access unknown device ID!\n");
        stimulus_finish_fail(10);
    }
} /* hdmirx_wr_only_reg */

unsigned long hdmirx_rd_TOP (unsigned long addr)
{
    unsigned long dev_offset = 0;       // TOP ADDR_PORT: 0xc800e000; DWC ADDR_PORT: 0xc800e010
    unsigned long data;
    /*
    *((volatile unsigned long *) (HDMIRX_ADDR_PORT+dev_offset)) = addr;
    data = *((volatile unsigned long *) (HDMIRX_DATA_PORT+dev_offset)); 
    */
    data = hdmirx_rd_top(addr);
    return (data);
} /* hdmirx_rd_TOP */

unsigned long hdmirx_rd_DWC (unsigned long addr)
{
    unsigned long dev_offset = 0x10;    // TOP ADDR_PORT: 0xc800e000; DWC ADDR_PORT: 0xc800e010
    unsigned long data;
    /*
    *((volatile unsigned long *) (HDMIRX_ADDR_PORT+dev_offset)) = addr;
    data = *((volatile unsigned long *) (HDMIRX_DATA_PORT+dev_offset)); 
    */
    data = hdmi_rx_ctrl_read(addr);
    return (data);
} /* hdmirx_rd_DWC */

unsigned long hdmirx_rd_PHY (unsigned long addr)
{
    unsigned long data;
    /*
    hdmirx_wr_only_DWC( HDMIRX_DWC_I2CM_PHYG3_ADDRESS,  addr & 0xff);
    hdmirx_wr_only_DWC( HDMIRX_DWC_I2CM_PHYG3_OPERATION,0x2);
    Wr(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 400 ) {}
    hdmirx_poll_DWC(    HDMIRX_DWC_HDMI_ISTS,           1<<28, ~(1<<28), 0);
    hdmirx_wr_only_DWC( HDMIRX_DWC_HDMI_ICLR,           1<<28);
    hdmirx_poll_DWC(    HDMIRX_DWC_HDMI_ISTS,           0<<28, ~(1<<28), 0);
    data = hdmirx_rd_DWC(HDMIRX_DWC_I2CM_PHYG3_DATAI);
    */
    data = debug_phy_read(addr);
    return (data);
} /* hdmirx_rd_PHY */

unsigned long hdmirx_rd_reg (unsigned char dev_id, unsigned long addr)
{
    unsigned long data;

    if (dev_id == HDMIRX_DEV_ID_TOP) {
        data = hdmirx_rd_TOP(addr);
    } else if (dev_id == HDMIRX_DEV_ID_DWC) {
        data = hdmirx_rd_DWC(addr);
    } else if (dev_id == HDMIRX_DEV_ID_PHY) {
        data = hdmirx_rd_PHY(addr);
    } else {
        stimulus_print("Error: hdmirx_rd_reg access unknown device ID!\n");
        stimulus_finish_fail(10);
    }
    return (data);
} /* hdmirx_rd_reg */

void hdmirx_rd_check_TOP (unsigned long addr, unsigned long exp_data, unsigned long mask)
{
    unsigned long rd_data;
    rd_data = hdmirx_rd_TOP(addr);
    if ((rd_data | mask) != (exp_data | mask)) 
    {
        printk("Error: HDMIRX-TOP addr=0x%x, rd_data=0x%x, exp_data=0x%x, mask=0x%x\n", 
            addr, rd_data, exp_data, mask);
        /*
        stimulus_print("Error: HDMIRX-TOP addr=0x");
        stimulus_print_num_hex(addr);
        stimulus_print_without_timestamp(" rd_data=0x");
        stimulus_print_num_hex(rd_data);
        stimulus_print_without_timestamp(" exp_data=0x");
        stimulus_print_num_hex(exp_data);
        stimulus_print_without_timestamp(" mask=0x");
        stimulus_print_num_hex(mask);
        stimulus_print_without_timestamp("\n");
        */
        //stimulus_finish_fail(10);
    }
} /* hdmirx_rd_check_TOP */

void hdmirx_rd_check_DWC (unsigned long addr, unsigned long exp_data, unsigned long mask)
{
    unsigned long rd_data;
    rd_data = hdmirx_rd_DWC(addr);
    if ((rd_data | mask) != (exp_data | mask)) 
    {
        printk("Error: HDMIRX-DWC addr=0x%x, rd_data=0x%x, exp_data=0x%x, mask=0x%x\n", 
            addr, rd_data, exp_data, mask);

        /*
        stimulus_print("Error: HDMIRX-DWC addr=0x");
        stimulus_print_num_hex(addr);
        stimulus_print_without_timestamp(" rd_data=0x");
        stimulus_print_num_hex(rd_data);
        stimulus_print_without_timestamp(" exp_data=0x");
        stimulus_print_num_hex(exp_data);
        stimulus_print_without_timestamp(" mask=0x");
        stimulus_print_num_hex(mask);
        stimulus_print_without_timestamp("\n");
        //stimulus_finish_fail(10); */
    }
} /* hdmirx_rd_check_DWC */

void hdmirx_rd_check_PHY (unsigned long addr, unsigned long exp_data, unsigned long mask)
{
    unsigned long rd_data;
    rd_data = hdmirx_rd_PHY(addr);
    if ((rd_data | mask) != (exp_data | mask)) 
    {
        printk("Error: HDMIRX-PHY addr=0x%x, rd_data=0x%x, exp_data=0x%x, mask=0x%x\n", 
            addr, rd_data, exp_data, mask);
        /*
        stimulus_print("Error: HDMIRX-PHY addr=0x");
        stimulus_print_num_hex(addr);
        stimulus_print_without_timestamp(" rd_data=0x");
        stimulus_print_num_hex(rd_data);
        stimulus_print_without_timestamp(" exp_data=0x");
        stimulus_print_num_hex(exp_data);
        stimulus_print_without_timestamp(" mask=0x");
        stimulus_print_num_hex(mask);
        stimulus_print_without_timestamp("\n");
        stimulus_finish_fail(10);*/
    }
} /* hdmirx_rd_check_PHY */

void hdmirx_rd_check_reg (unsigned char dev_id, unsigned long addr, unsigned long exp_data, unsigned long mask)
{
    if (dev_id == HDMIRX_DEV_ID_TOP) {
        hdmirx_rd_check_TOP(addr, exp_data, mask);
    } else if (dev_id == HDMIRX_DEV_ID_DWC) {
        hdmirx_rd_check_DWC(addr, exp_data, mask);
    } else if (dev_id == HDMIRX_DEV_ID_PHY) {
        hdmirx_rd_check_PHY(addr, exp_data, mask);
    } else {
        stimulus_print("Error: hdmirx_rd_check_reg access unknown device ID!\n");
        stimulus_finish_fail(10);
    }
} /* hdmirx_rd_check_reg */

void hdmirx_wr_TOP (unsigned long addr, unsigned long data)
{
    hdmirx_wr_only_TOP(addr, data);
    hdmirx_rd_check_TOP(addr, data, 0);
} /* hdmirx_wr_TOP */

void hdmirx_wr_DWC (unsigned long addr, unsigned long data)
{
    hdmirx_wr_only_DWC(addr, data);
    hdmirx_rd_check_DWC(addr, data, 0);
} /* hdmirx_wr_DWC */

void hdmirx_wr_PHY (unsigned long addr, unsigned long data)
{
    hdmirx_wr_only_PHY(addr, data);
    hdmirx_rd_check_PHY(addr, data, 0);
} /* hdmirx_wr_PHY */

void hdmirx_wr_reg (unsigned char dev_id, unsigned long addr, unsigned long data)
{
    if (dev_id == HDMIRX_DEV_ID_TOP) {
        hdmirx_wr_TOP(addr, data);
    } else if (dev_id == HDMIRX_DEV_ID_DWC) {
        hdmirx_wr_DWC(addr, data);
    } else if (dev_id == HDMIRX_DEV_ID_PHY) {
        hdmirx_wr_PHY(addr, data);
    } else {
        stimulus_print("Error: hdmirx_wr_reg access unknown device ID!\n");
        stimulus_finish_fail(10);
    }
} /* hdmirx_wr_reg */

void hdmirx_poll_TOP (unsigned long addr, unsigned long exp_data, unsigned long mask)
{
    unsigned long rd_data;
    rd_data = hdmirx_rd_TOP(addr);
    while ((rd_data | mask) != (exp_data | mask))
    {
        rd_data = hdmirx_rd_TOP(addr);
    }
} /* hdmirx_poll_TOP */

void hdmirx_poll_DWC (unsigned long addr, unsigned long exp_data, unsigned long mask, unsigned long max_try)
{
    unsigned long rd_data;
    unsigned long cnt   = 0;
    unsigned char done  = 0;
    
    rd_data = hdmirx_rd_DWC(addr);
    while (((cnt < max_try) || (max_try == 0)) && (done != 1)) {
        if ((rd_data | mask) == (exp_data | mask)) {
            done = 1;
        } else {
            cnt ++;
            rd_data = hdmirx_rd_DWC(addr);
        }
    }
    if (done == 0) {
        stimulus_print("Error: hdmirx_poll_DWC access time-out!\n");
        stimulus_finish_fail(10);
    }
} /* hdmirx_poll_DWC */

void hdmirx_poll_PHY (unsigned long addr, unsigned long exp_data, unsigned long mask)
{
    unsigned long rd_data;
    rd_data = hdmirx_rd_PHY(addr);
    while ((rd_data | mask) != (exp_data | mask))
    {
        rd_data = hdmirx_rd_PHY(addr);
    }
} /* hdmirx_poll_PHY */

void hdmirx_poll_reg (unsigned char dev_id, unsigned long addr, unsigned long exp_data, unsigned long mask)
{
    if (dev_id == HDMIRX_DEV_ID_TOP) {
        hdmirx_poll_TOP(addr, exp_data, mask);
    } else if (dev_id == HDMIRX_DEV_ID_DWC) {
        hdmirx_poll_DWC(addr, exp_data, mask, 0);
    } else if (dev_id == HDMIRX_DEV_ID_PHY) {
        hdmirx_poll_PHY(addr, exp_data, mask);
    } else {
        stimulus_print("Error: hdmirx_poll_reg access unknown device ID!\n");
        stimulus_finish_fail(10);
    }
} /* hdmirx_poll_reg */

void hdmirx_edid_setting (unsigned char edid_extension_flag)
{
    const unsigned char rx_edid[] = {
        // Block 0:
        0,                  // BLK_0_0   Reserved
        255,                // BLK_0_1   
        255,                // BLK_0_2   
        255,                // BLK_0_3   
        255,                // BLK_0_4   
        255,                // BLK_0_5   
        255,                // BLK_0_6   
        0,                  // BLK_0_7   
        83,                 // BLK_0_8   ID_MAN_NAME_7_0
        3,                  // BLK_0_9   ID_MAN_NAME_15_8
        65,                 // BLK_0_10  ID_PRO_CODE_7_0
        1,                  // BLK_0_11  ID_PRO_CODE_15_8
        0,                  // BLK_0_12  ID_SER_NUM_7_0
        0,                  // BLK_0_13  ID_SER_NUM_15_8
        0,                  // BLK_0_14  ID_SER_NUM_23_16
        0,                  // BLK_0_15  ID_SER_NUM_31_24
        0,                  // BLK_0_16  ID_MAN_WEEK_7_0
        18,                 // BLK_0_17  ID_MAN_YEAR_7_0
        1,                  // BLK_0_18  ID_VER_7_0
        3,                  // BLK_0_19  ID_REV_7_0
        128,                // BLK_0_20  ID_VID_IN_DEF_7_0
        105,                // BLK_0_21  ID_MAX_HOR_SIZE_7_0
        59,                 // BLK_0_22  ID_MAX_VER_SIZE_7_0
        120,                // BLK_0_23  ID_GAM_7_0
        10,                 // BLK_0_24  ID_FEAT_SUP_7_0
        13,                 // BLK_0_25  ID_RG_LOW_BITS_7_0
        201,                // BLK_0_26  ID_BW_LOW_BIT_7_0
        160,                // BLK_0_27  ID_RX_7_0
        87,                 // BLK_0_28  ID_RY_7_0
        71,                 // BLK_0_29  ID_GX_7_0
        152,                // BLK_0_30  ID_GY_7_0
        39,                 // BLK_0_31  ID_BX_7_0
        18,                 // BLK_0_32  ID_BY_7_0
        72,                 // BLK_0_33  ID_WX_7_0
        76,                 // BLK_0_34  ID_WY_7_0
        32,                 // BLK_0_35  ID_TIM_1_7_0
        0,                  // BLK_0_36  ID_TIM_2_7_0
        0,                  // BLK_0_37  ID_TIM_RES_7_0
        1,                  // BLK_0_38  ID_STD_TIM_7_0
        1,                  // BLK_0_39  ID_STD_TIM_15_8
        1,                  // BLK_0_40  ID_STD_TIM_ID_2_7_0
        1,                  // BLK_0_41  ID_STD_TIM_ID_2_15_8
        1,                  // BLK_0_42  ID_STD_TIM_ID_3_7_0
        1,                  // BLK_0_43  ID_STD_TIM_ID_3_15_8
        1,                  // BLK_0_44  ID_STD_TIM_ID_4_7_0
        1,                  // BLK_0_45  ID_STD_TIM_ID_4_15_8
        1,                  // BLK_0_46  ID_STD_TIM_ID_5_7_0
        1,                  // BLK_0_47  ID_STD_TIM_ID_5_15_8
        1,                  // BLK_0_48  ID_STD_TIM_ID_6_7_0
        1,                  // BLK_0_49  ID_STD_TIM_ID_6_15_8
        1,                  // BLK_0_50  ID_STD_TIM_ID_7_7_0
        1,                  // BLK_0_51  ID_STD_TIM_ID_7_15_8
        1,                  // BLK_0_52  ID_STD_TIM_ID_8_7_0
        1,                  // BLK_0_53  ID_STD_TIM_ID_8_15_8
        2,                  // BLK_0_54  ID_DET_TIM_DES_1_7_0
        58,                 // BLK_0_55  ID_DET_TIM_DES_1_15_8
        128,                // BLK_0_56  ID_DET_TIM_DES_1_23_16
        208,                // BLK_0_57  ID_DET_TIM_DES_1_31_24
        114,                // BLK_0_58  ID_DET_TIM_DES_1_39_32
        56,                 // BLK_0_59  ID_DET_TIM_DES_1_47_40
        45,                 // BLK_0_60  ID_DET_TIM_DES_1_55_48
        64,                 // BLK_0_61  ID_DET_TIM_DES_1_63_5
        16,                 // BLK_0_62  ID_DET_TIM_DES_1_71_64
        44,                 // BLK_0_63  ID_DET_TIM_DES_1_79_72
        69,                 // BLK_0_64  ID_DET_TIM_DES_1_87_80
        128,                // BLK_0_65  ID_DET_TIM_DES_1_95_88
        196,                // BLK_0_66  ID_DET_TIM_DES_1_103_96
        142,                // BLK_0_67  ID_DET_TIM_DES_1_111_104
        33,                 // BLK_0_68  ID_DET_TIM_DES_1_119_112
        0,                  // BLK_0_69  ID_DET_TIM_DES_1_127_120
        0,                  // BLK_0_70  ID_DET_TIM_DES_1_135_128
        30,                 // BLK_0_71  ID_DET_TIM_DES_1_143_136
        140,                // BLK_0_72  ID_DET_TIM_DES_2_7_0
        10,                 // BLK_0_73  ID_DET_TIM_DES_2_15_8
        208,                // BLK_0_74  ID_DET_TIM_DES_2_23_16
        144,                // BLK_0_75  ID_DET_TIM_DES_2_31_24
        32,                 // BLK_0_76  ID_DET_TIM_DES_2_39_32
        64,                 // BLK_0_77  ID_DET_TIM_DES_2_47_40
        49,                 // BLK_0_78  ID_DET_TIM_DES_2_55_48
        32,                 // BLK_0_79  ID_DET_TIM_DES_2_63_56
        12,                 // BLK_0_80  ID_DET_TIM_DES_2_71_64
        64,                 // BLK_0_81  ID_DET_TIM_DES_2_79_72
        85,                 // BLK_0_82  ID_DET_TIM_DES_2_87_80
        0,                  // BLK_0_83  ID_DET_TIM_DES_2_95_88
        196,                // BLK_0_84  ID_DET_TIM_DES_2_103_96
        142,                // BLK_0_85  ID_DET_TIM_DES_2_111_104
        33,                 // BLK_0_86  ID_DET_TIM_DES_2_119_112
        0,                  // BLK_0_87  ID_DET_TIM_DES_2_127_120
        0,                  // BLK_0_88  ID_DET_TIM_DES_2_135_128
        24,                 // BLK_0_89  ID_DET_TIM_DES_2_143_136
        0,                  // BLK_0_90  ID_DET_TIM_DES_3_7_0
        0,                  // BLK_0_91  ID_DET_TIM_DES_3_15_8
        0,                  // BLK_0_92  ID_DET_TIM_DES_3_23_16
        252,                // BLK_0_93  ID_DET_TIM_DES_3_31_24
        0,                  // BLK_0_94  ID_DET_TIM_DES_3_39_32
        84,                 // BLK_0_95  ID_DET_TIM_DES_3_47_40
        88,                 // BLK_0_96  ID_DET_TIM_DES_3_55_48
        67,                 // BLK_0_97  ID_DET_TIM_DES_3_63_56
        45,                 // BLK_0_98  ID_DET_TIM_DES_3_71_64
        72,                 // BLK_0_99  ID_DET_TIM_DES_3_79_72
        68,                 // BLK_0_100 ID_DET_TIM_DES_3_87_80
        77,                 // BLK_0_101 ID_DET_TIM_DES_3_95_88
        73,                 // BLK_0_102 ID_DET_TIM_DES_3_103_96
        10,                 // BLK_0_103 ID_DET_TIM_DES_3_111_104
        32,                 // BLK_0_104 ID_DET_TIM_DES_3_119_112
        32,                 // BLK_0_105 ID_DET_TIM_DES_3_127_120
        32,                 // BLK_0_106 ID_DET_TIM_DES_3_135_128
        32,                 // BLK_0_107 ID_DET_TIM_DES_3_143_136
        0,                  // BLK_0_108 ID_DET_TIM_DES_4_7_0
        0,                  // BLK_0_109 ID_DET_TIM_DES_4_15_8
        0,                  // BLK_0_110 ID_DET_TIM_DES_4_23_16
        253,                // BLK_0_111 ID_DET_TIM_DES_4_31_24
        0,                  // BLK_0_112 ID_DET_TIM_DES_4_39_32
        23,                 // BLK_0_113 ID_DET_TIM_DES_4_47_40
        61,                 // BLK_0_114 ID_DET_TIM_DES_4_55_48
        15,                 // BLK_0_115 ID_DET_TIM_DES_4_63_56
        68,                 // BLK_0_116 ID_DET_TIM_DES_4_71_64
        15,                 // BLK_0_117 ID_DET_TIM_DES_4_79_72
        0,                  // BLK_0_118 ID_DET_TIM_DES_4_87_80
        10,                 // BLK_0_119 ID_DET_TIM_DES_4_95_88
        32,                 // BLK_0_120 ID_DET_TIM_DES_4_103_96
        32,                 // BLK_0_121 ID_DET_TIM_DES_4_111_104
        32,                 // BLK_0_122 ID_DET_TIM_DES_4_119_112
        32,                 // BLK_0_123 ID_DET_TIM_DES_4_127_120
        32,                 // BLK_0_124 ID_DET_TIM_DES_4_135_128
        32,                 // BLK_0_125 ID_DET_TIM_DES_4_143_136
        edid_extension_flag,// BLK_0_126 EXT_FLAG_7_0//1,
        191,                // BLK_0_127 CHECKSUM_7_0
        // Block 1:
        2,                  // BLK_1_0
        3,                  // BLK_1_1
        81,                 // BLK_1_2
        113,                // BLK_1_3
        95,                 // BLK_1_4
        144,                // BLK_1_5
        2,                  // BLK_1_6
        3,                  // BLK_1_7
        4,                  // BLK_1_8
        1,                  // BLK_1_9
        5,                  // BLK_1_10
        6,                  // BLK_1_11
        7,                  // BLK_1_12
        8,                  // BLK_1_13
        9,                  // BLK_1_14
        10,                 // BLK_1_15
        11,                 // BLK_1_16
        12,                 // BLK_1_17
        13,                 // BLK_1_18
        14,                 // BLK_1_19
        15,                 // BLK_1_20
        17,                 // BLK_1_21
        18,                 // BLK_1_22
        19,                 // BLK_1_23
        20,                 // BLK_1_24
        21,                 // BLK_1_25
        22,                 // BLK_1_26
        23,                 // BLK_1_27
        24,                 // BLK_1_28
        25,                 // BLK_1_29
        26,                 // BLK_1_30
        27,                 // BLK_1_31
        28,                 // BLK_1_32
        29,                 // BLK_1_33
        30,                 // BLK_1_34
        31,                 // BLK_1_35
        92,                 // BLK_1_36
        32,                 // BLK_1_37
        33,                 // BLK_1_38
        34,                 // BLK_1_39
        35,                 // BLK_1_40
        36,                 // BLK_1_41
        37,                 // BLK_1_42 
        38,                 // BLK_1_43 
        39,                 // BLK_1_44 
        40,                 // BLK_1_45 
        41,                 // BLK_1_46 
        42,                 // BLK_1_47 
        43,                 // BLK_1_48 
        44,                 // BLK_1_49 
        45,                 // BLK_1_50 
        46,                 // BLK_1_51 
        47,                 // BLK_1_52 
        48,                 // BLK_1_53 
        49,                 // BLK_1_54 
        50,                 // BLK_1_55 
        51,                 // BLK_1_56 
        52,                 // BLK_1_57 
        53,                 // BLK_1_58 
        54,                 // BLK_1_59 
        55,                 // BLK_1_60 
        56,                 // BLK_1_61 
        57,                 // BLK_1_62 
        58,                 // BLK_1_63 
        59,                 // BLK_1_64 
        35,                 // BLK_1_65 
        9,                  // BLK_1_66 
        31,                 // BLK_1_67 
        7,                  // BLK_1_68 
        131,                // BLK_1_69 
        1,                  // BLK_1_70 
        0,                  // BLK_1_71 
        0,                  // BLK_1_72 
        103,                // BLK_1_73 
        3,                  // BLK_1_74 
        12,                 // BLK_1_75 
        0,                  // BLK_1_76 
        16,                 // BLK_1_77 
        0,                  // BLK_1_78 
        8,                  // BLK_1_79 
        30,                 // BLK_1_80 
        0,                  // BLK_1_81 
        0,                  // BLK_1_82 
        0,                  // BLK_1_83 
        0,                  // BLK_1_84 
        0,                  // BLK_1_85 
        0,                  // BLK_1_86 
        0,                  // BLK_1_87 
        0,                  // BLK_1_88 
        0,                  // BLK_1_89 
        0,                  // BLK_1_90 
        0,                  // BLK_1_91 
        0,                  // BLK_1_92 
        0,                  // BLK_1_93 
        0,                  // BLK_1_94 
        0,                  // BLK_1_95 
        0,                  // BLK_1_96 
        0,                  // BLK_1_97 
        0,                  // BLK_1_98 
        0,                  // BLK_1_99 
        0,                  // BLK_1_100
        0,                  // BLK_1_101
        0,                  // BLK_1_102
        0,                  // BLK_1_103
        0,                  // BLK_1_104
        0,                  // BLK_1_105
        0,                  // BLK_1_106
        0,                  // BLK_1_107
        0,                  // BLK_1_108
        0,                  // BLK_1_109
        0,                  // BLK_1_110
        0,                  // BLK_1_111
        0,                  // BLK_1_112
        0,                  // BLK_1_113
        0,                  // BLK_1_114
        0,                  // BLK_1_115
        0,                  // BLK_1_116
        0,                  // BLK_1_117
        0,                  // BLK_1_118
        0,                  // BLK_1_119
        0,                  // BLK_1_120
        0,                  // BLK_1_121
        0,                  // BLK_1_122
        0,                  // BLK_1_123
        0,                  // BLK_1_124
        0,                  // BLK_1_125
        0,                  // BLK_1_126
        146,                // BLK_1_127 CHECKSUM_7_0
        // Block 2:
        22,                 // BLK_2_0  
        23,                 // BLK_2_1  
        24,                 // BLK_2_2  
        25,                 // BLK_2_3  
        26,                 // BLK_2_4  
        27,                 // BLK_2_5  
        28,                 // BLK_2_6  
        29,                 // BLK_2_7  
        30,                 // BLK_2_8  
        31,                 // BLK_2_9  
        32,                 // BLK_2_10 
        33,                 // BLK_2_11 
        34,                 // BLK_2_12 
        35,                 // BLK_2_13 
        36,                 // BLK_2_14 
        37,                 // BLK_2_15 
        38,                 // BLK_2_16 
        39,                 // BLK_2_17 
        40,                 // BLK_2_18 
        41,                 // BLK_2_19 
        42,                 // BLK_2_20 
        43,                 // BLK_2_21 
        44,                 // BLK_2_22 
        45,                 // BLK_2_23 
        46,                 // BLK_2_24 
        47,                 // BLK_2_25 
        48,                 // BLK_2_26 
        49,                 // BLK_2_27 
        50,                 // BLK_2_28 
        51,                 // BLK_2_29 
        52,                 // BLK_2_30 
        53,                 // BLK_2_31 
        54,                 // BLK_2_32 
        55,                 // BLK_2_33 
        56,                 // BLK_2_34 
        57,                 // BLK_2_35 
        58,                 // BLK_2_36 
        59,                 // BLK_2_37 
        60,                 // BLK_2_38 
        61,                 // BLK_2_39 
        62,                 // BLK_2_40 
        63,                 // BLK_2_41 
        64,                 // BLK_2_42 
        65,                 // BLK_2_43 
        66,                 // BLK_2_44 
        67,                 // BLK_2_45 
        68,                 // BLK_2_46 
        69,                 // BLK_2_47 
        70,                 // BLK_2_48 
        71,                 // BLK_2_49 
        72,                 // BLK_2_50 
        73,                 // BLK_2_51 
        74,                 // BLK_2_52 
        75,                 // BLK_2_53 
        76,                 // BLK_2_54 
        77,                 // BLK_2_55 
        78,                 // BLK_2_56 
        79,                 // BLK_2_57 
        80,                 // BLK_2_58 
        81,                 // BLK_2_59 
        82,                 // BLK_2_60 
        83,                 // BLK_2_61 
        84,                 // BLK_2_62 
        85,                 // BLK_2_63 
        86,                 // BLK_2_64 
        87,                 // BLK_2_65 
        88,                 // BLK_2_66 
        89,                 // BLK_2_67 
        90,                 // BLK_2_68 
        91,                 // BLK_2_69 
        92,                 // BLK_2_70 
        93,                 // BLK_2_71 
        94,                 // BLK_2_72 
        95,                 // BLK_2_73 
        96,                 // BLK_2_74 
        97,                 // BLK_2_75 
        98,                 // BLK_2_76 
        99,                 // BLK_2_77 
        100,                // BLK_2_78 
        101,                // BLK_2_79 
        102,                // BLK_2_80 
        103,                // BLK_2_81 
        104,                // BLK_2_82 
        105,                // BLK_2_83 
        106,                // BLK_2_84 
        107,                // BLK_2_85 
        108,                // BLK_2_86 
        109,                // BLK_2_87 
        110,                // BLK_2_88 
        111,                // BLK_2_89 
        112,                // BLK_2_90 
        113,                // BLK_2_91 
        114,                // BLK_2_92 
        115,                // BLK_2_93 
        116,                // BLK_2_94 
        117,                // BLK_2_95 
        118,                // BLK_2_96 
        119,                // BLK_2_97 
        120,                // BLK_2_98 
        121,                // BLK_2_99 
        122,                // BLK_2_100
        123,                // BLK_2_101
        124,                // BLK_2_102
        125,                // BLK_2_103
        126,                // BLK_2_104
        127,                // BLK_2_105
        128,                // BLK_2_106
        129,                // BLK_2_107
        130,                // BLK_2_108
        131,                // BLK_2_109
        132,                // BLK_2_110
        133,                // BLK_2_111
        134,                // BLK_2_112
        135,                // BLK_2_113
        136,                // BLK_2_114
        137,                // BLK_2_115
        138,                // BLK_2_116
        139,                // BLK_2_117
        140,                // BLK_2_118
        141,                // BLK_2_119
        142,                // BLK_2_120
        143,                // BLK_2_121
        144,                // BLK_2_122
        145,                // BLK_2_123
        146,                // BLK_2_124
        147,                // BLK_2_125
        148,                // BLK_2_126
        149};               // BLK_2_127 CHECKSUM_7_0

    int i, ram_addr, byte_num;
    unsigned int value;
    
    //byte_num = sizeof(rx_edid)/sizeof(unsigned char);
    
    byte_num = (edid_extension_flag<=2)? (1+edid_extension_flag)*128 : 3*128;
    for (i = 0; i < byte_num; i++)
    {
        value = rx_edid[i]; 
        ram_addr = HDMIRX_TOP_EDID_OFFSET+i;
        hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, ram_addr, value);
    }
} /* hdmirx_edid_setting */

void hdmirx_key_setting (unsigned char encrypt_en)
{
    //first set of test Device Private Keys (HDCP Table A-1, Receiver B1)
    const unsigned long hdmirx_hdcp_bksvs[2]    = {0x51, 0x1ef21acd}; // {high 8-bit, low32-bit}
    const unsigned long hdmirx_hdcp_keys[40*2]  = {
        0xbc13e0, 0xc75bf0fd,   // key set  0: {high 24-bit, low 32-bit}
        0xae0d2c, 0x7f76443b,   // key set  1: {high 24-bit, low 32-bit}
        0x24bf21, 0x85a36c60,   // key set  2: {high 24-bit, low 32-bit}
        0xf4bc6c, 0xbcd7a32f,   // key set  3: {high 24-bit, low 32-bit}
        0xa72e69, 0xc5eb6388,   // key set  4: {high 24-bit, low 32-bit}
        0x7fa2d2, 0x7a37d9f8,   // key set  5: {high 24-bit, low 32-bit}
        0x32fd35, 0x29dea3d1,   // key set  6: {high 24-bit, low 32-bit}
        0x485fc2, 0x40cc9bae,   // key set  7: {high 24-bit, low 32-bit}
        0x3b9857, 0x797d5103,   // key set  8: {high 24-bit, low 32-bit}
        0x0dd170, 0xbe615250,   // key set  9: {high 24-bit, low 32-bit}
        0x1a748b, 0xe4866bb1,   // key set 10: {high 24-bit, low 32-bit}
        0xf9606a, 0x7c348cca,   // key set 11: {high 24-bit, low 32-bit}
        0x4bbb03, 0x7899eea1,   // key set 12: {high 24-bit, low 32-bit}
        0x190ecf, 0x9cc095a9,   // key set 13: {high 24-bit, low 32-bit}
        0xa821c4, 0x6897447f,   // key set 14: {high 24-bit, low 32-bit}
        0x1a8a0b, 0xc4298a41,   // key set 15: {high 24-bit, low 32-bit}
        0xaefc08, 0x53e62082,   // key set 16: {high 24-bit, low 32-bit}
        0xf75d4a, 0x0c497ba4,   // key set 17: {high 24-bit, low 32-bit}
        0xad6495, 0xfc8a06d8,   // key set 18: {high 24-bit, low 32-bit}
        0x67c202, 0x0c2b2e02,   // key set 19: {high 24-bit, low 32-bit}
        0x8f116b, 0x18f4ae8d,   // key set 20: {high 24-bit, low 32-bit}
        0xe3053f, 0xa3e9fa69,   // key set 21: {high 24-bit, low 32-bit}
        0x37d800, 0x2881c7d1,   // key set 22: {high 24-bit, low 32-bit}
        0xc3a5fd, 0x1c15669c,   // key set 23: {high 24-bit, low 32-bit}
        0x9e93d4, 0x1e0811f7,   // key set 24: {high 24-bit, low 32-bit}
        0x2c4074, 0x509eec6c,   // key set 25: {high 24-bit, low 32-bit}
        0x8b7fd8, 0x19279b61,   // key set 26: {high 24-bit, low 32-bit}
        0xd7caad, 0xa0a06ce9,   // key set 27: {high 24-bit, low 32-bit}
        0x9297dc, 0xa1f8c1db,   // key set 28: {high 24-bit, low 32-bit}
        0x5d1aaa, 0x99dea489,   // key set 29: {high 24-bit, low 32-bit}
        0x60cb56, 0xddbaa1d9,   // key set 30: {high 24-bit, low 32-bit}
        0x85d4ad, 0x5e5ff2e0,   // key set 31: {high 24-bit, low 32-bit}
        0x128016, 0x1221df6d,   // key set 32: {high 24-bit, low 32-bit}
        0xca31a5, 0xf2406589,   // key set 33: {high 24-bit, low 32-bit}
        0x1d30e8, 0xcb198e6f,   // key set 34: {high 24-bit, low 32-bit}
        0xd1c18b, 0xed07d3fa,   // key set 35: {high 24-bit, low 32-bit}
        0xcec7ec, 0x09245b43,   // key set 36: {high 24-bit, low 32-bit}
        0xb08129, 0xefedd583,   // key set 37: {high 24-bit, low 32-bit}
        0x2134cf, 0x4ce286e5,   // key set 38: {high 24-bit, low 32-bit}
        0xedeef9, 0xd099b78c    // key set 39: {high 24-bit, low 32-bit}
    }; /* hdmirx_hdcp_keys */
    const unsigned long hdmirx_hdcp_key_decrypt_seed    = 0xA55A;
    const unsigned long hdmirx_hdcp_encrypt_keys[40*2]  = {
        0xC0E0BD, 0x0AB26F9F,   // key set  0: {high 24-bit, low 32-bit}
        0x0B90B3, 0xE9B2B75F,   // key set  1: {high 24-bit, low 32-bit}
        0xBD00B5, 0xD15859EE,   // key set  2: {high 24-bit, low 32-bit}
        0xD89597, 0x7578E44C,   // key set  3: {high 24-bit, low 32-bit}
        0x4AFF12, 0xFCC45CA2,   // key set  4: {high 24-bit, low 32-bit}
        0x36555B, 0xD5B12FAF,   // key set  5: {high 24-bit, low 32-bit}
        0x8AE77F, 0x4EDFD419,   // key set  6: {high 24-bit, low 32-bit}
        0x7AA3D0, 0x0FD2C60F,   // key set  7: {high 24-bit, low 32-bit}
        0x79052E, 0xBD613745,   // key set  8: {high 24-bit, low 32-bit}
        0xB67BB5, 0xE12AE0A6,   // key set  9: {high 24-bit, low 32-bit}
        0x78B9DD, 0xF6629AC5,   // key set 10: {high 24-bit, low 32-bit}
        0x61DEEE, 0x2BFE2F2F,   // key set 11: {high 24-bit, low 32-bit}
        0x1A40B2, 0x1F63F998,   // key set 12: {high 24-bit, low 32-bit}
        0x5A9AE6, 0xDE543C62,   // key set 13: {high 24-bit, low 32-bit}
        0x65DF19, 0xA00E5744,   // key set 14: {high 24-bit, low 32-bit}
        0x6C684F, 0x4B65A8BB,   // key set 15: {high 24-bit, low 32-bit}
        0x7DA075, 0xB7F8D6CC,   // key set 16: {high 24-bit, low 32-bit}
        0x1DE01C, 0xEEADFBC8,   // key set 17: {high 24-bit, low 32-bit}
        0x06E607, 0xC4DC61C4,   // key set 18: {high 24-bit, low 32-bit}
        0xA3BB1E, 0xD7510D5E,   // key set 19: {high 24-bit, low 32-bit}
        0x02F495, 0xECEB5843,   // key set 20: {high 24-bit, low 32-bit}
        0x80E13E, 0x57081DCB,   // key set 21: {high 24-bit, low 32-bit}
        0x6FB563, 0x6F2E0EAB,   // key set 22: {high 24-bit, low 32-bit}
        0x72439F, 0x4058074B,   // key set 23: {high 24-bit, low 32-bit}
        0xB98261, 0xF21FBEEF,   // key set 24: {high 24-bit, low 32-bit}
        0xC1EB77, 0x5AECDF3B,   // key set 25: {high 24-bit, low 32-bit}
        0xF780A5, 0x5E975124,   // key set 26: {high 24-bit, low 32-bit}
        0xE1DB09, 0x5E94F736,   // key set 27: {high 24-bit, low 32-bit}
        0x8FFA7B, 0x82786B25,   // key set 28: {high 24-bit, low 32-bit}
        0xE60823, 0x52B35574,   // key set 29: {high 24-bit, low 32-bit}
        0x212A04, 0x82E7C09F,   // key set 30: {high 24-bit, low 32-bit}
        0x38AF79, 0xC2A06F25,   // key set 31: {high 24-bit, low 32-bit}
        0xFB17B5, 0x2A46ACA3,   // key set 32: {high 24-bit, low 32-bit}
        0x2C2DE0, 0x1316DBC3,   // key set 33: {high 24-bit, low 32-bit}
        0x5E5761, 0x758CCA16,   // key set 34: {high 24-bit, low 32-bit}
        0x4D93A9, 0x09C6A332,   // key set 35: {high 24-bit, low 32-bit}
        0xFA6BF7, 0x463357F5,   // key set 36: {high 24-bit, low 32-bit}
        0x60B17C, 0xA1A5D7FA,   // key set 37: {high 24-bit, low 32-bit}
        0x7BB35C, 0x605646D5,   // key set 38: {high 24-bit, low 32-bit}
        0x28AAD1, 0x52893794    // key set 39: {high 24-bit, low 32-bit}
    }; /* hdmirx_hdcp_encrypt_keys */

    int i;
    
    if (encrypt_en) {
        hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDCP_SEED,  hdmirx_hdcp_key_decrypt_seed);
    }

    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDCP_KIDX,      0);
    for (i = 0; i < 40; i ++) {
        hdmirx_poll_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDCP_STS, 1, 0);
        hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDCP_KEY1,  encrypt_en? hdmirx_hdcp_encrypt_keys[i*2]   : hdmirx_hdcp_keys[i*2]);
        hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDCP_KEY0,  encrypt_en? hdmirx_hdcp_encrypt_keys[i*2+1] : hdmirx_hdcp_keys[i*2+1]);
    }
    
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDCP_BKSV1, hdmirx_hdcp_bksvs[0]);
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDCP_BKSV0, hdmirx_hdcp_bksvs[1]);
} /* hdmirx_key_setting */

void start_video_gen_ana (  unsigned char   vic,                // Video format identification code
                            unsigned char   pixel_repeat_hdmi,
                            unsigned char   interlace_mode,     // 0=Progressive; 1=Interlace.
                            unsigned long   front_porch,        // Number of pixels from DE Low to HSYNC high
                            unsigned long   back_porch,         // Number of pixels from HSYNC low to DE high
                            unsigned long   hsync_pixels,       // Number of pixels of HSYNC pulse
                            unsigned long   hsync_polarity,     // TX HSYNC polarity: 0=low active; 1=high active.
                            unsigned long   sof_lines,          // HSYNC count between VSYNC de-assertion and first line of active video
                            unsigned long   eof_lines,          // HSYNC count between last line of active video and start of VSYNC
                            unsigned long   vsync_lines,        // HSYNC count of VSYNC assertion
                            unsigned long   vsync_polarity,     // TX VSYNC polarity: 0=low active; 1=high active.
                            unsigned long   total_pixels,       // Number of total pixels per line
                            unsigned long   total_lines)        // Number of total lines per frame
{
    unsigned long data32;
    
    stimulus_print("[TEST.C] Configure external video data generator\n");
    
    data32  = vic               << 0;
    data32 |= pixel_repeat_hdmi << 8;
    data32 |= interlace_mode    << 12;
    //stimulus_event(31, STIMULUS_HDMI_UTIL_SET_VIC | data32);
    
    data32  = front_porch       << 0;
    data32 |= back_porch        << 12;
    //stimulus_event(31, STIMULUS_HDMI_UTIL_SET_HSYNC_0 | data32);
    
    data32  = hsync_pixels      << 0;
    data32 |= hsync_polarity    << 12;
    //stimulus_event(31, STIMULUS_HDMI_UTIL_SET_HSYNC_1 | data32);
    
    data32  = sof_lines << 0;
    data32 |= eof_lines << 12;
    //stimulus_event(31, STIMULUS_HDMI_UTIL_SET_VSYNC_0 | data32);
    
    data32  = vsync_lines       << 0;
    data32 |= vsync_polarity    << 12;
    //stimulus_event(31, STIMULUS_HDMI_UTIL_SET_VSYNC_1 | data32);

    data32  = total_pixels      << 0;
    data32 |= total_lines       << 12;
    //stimulus_event(31, STIMULUS_HDMI_UTIL_SET_HV_TOTAL | data32);

    //stimulus_event(31, STIMULUS_HDMI_UTIL_VGEN_RESET        | 0);
}

void hdmirx_test_function ( unsigned char   acr_mode,                   // Select which ACR scheme: 0=Analog PLL based ACR; 1=Digital ACR.
                            unsigned long   manual_acr_cts,
                            unsigned long   manual_acr_n,
                            unsigned char   rx_8_channel,               // Audio channels: 0=2-channel; 1=4 x 2-channel.
                            unsigned char   edid_extension_flag,        // Number of 128-bytes blocks that following the basic block
                            unsigned char   edid_auto_cec_enable,       // 1=Automatic switch CEC ID depend on RX_PORT_SEL
                            unsigned long   edid_cec_id_addr,           // EDID address offsets for storing 2-byte of Physical Address
                            unsigned long   edid_cec_id_data,           // Physical Address: e.g. 0x1023 is 1.0.2.3
                            unsigned char   edid_auto_checksum_enable,  // Checksum byte selection: 0=Use data stored in MEM; 1=Use checksum calculated by HW.
                            unsigned char   edid_clk_divide_m1,         // EDID I2C clock = sysclk / (1+edid_clk_divide_m1).
                            unsigned char   hdcp_on,
                            unsigned char   hdcp_key_decrypt_en,
                            unsigned char   vic,                        // Video format identification code
                            unsigned char   pixel_repeat_hdmi,
                            unsigned char   interlace_mode,             // 0=Progressive; 1=Interlace.
                            unsigned long   front_porch,                // Number of pixels from DE Low to HSYNC high
                            unsigned long   back_porch,                 // Number of pixels from HSYNC low to DE high
                            unsigned long   hsync_pixels,               // Number of pixels of HSYNC pulse
                            unsigned long   hsync_polarity,             // TX HSYNC polarity: 0=low active; 1=high active.
                            unsigned long   sof_lines,                  // HSYNC count between VSYNC de-assertion and first line of active video
                            unsigned long   eof_lines,                  // HSYNC count between last line of active video and start of VSYNC
                            unsigned long   vsync_lines,                // HSYNC count of VSYNC assertion
                            unsigned char   vsync_polarity,             // TX VSYNC polarity: 0=low active; 1=high active.
                            unsigned long   total_pixels,               // Number of total pixels per line
                            unsigned long   total_lines,                // Number of total lines per frame
                            unsigned char   rx_input_color_format,      // Pixel format: 0=RGB444; 1=YCbCr422; 2=YCbCr444.
                            unsigned char   rx_input_color_depth,       // Pixel bit width: 0=24-bit; 1=30-bit; 2=36-bit; 3=48-bit.
                            unsigned char   rx_hscale_half,             // 1=RX output video horizontally scaled by half, to reduce clock speed.
                            unsigned long   *curr_pdec_ien_maskn,
                            unsigned long   *curr_aud_clk_ien_maskn,
                            unsigned long   *curr_aud_fifo_ien_maskn,
                            unsigned long   *curr_md_ien_maskn,
                            unsigned long   *curr_hdmi_ien_maskn,
                            unsigned long   pdec_ien_maskn,
                            unsigned long   aud_clk_ien_maskn,
                            unsigned long   aud_fifo_ien_maskn,
                            unsigned long   md_ien_maskn,
                            unsigned long   hdmi_ien_maskn,
                            unsigned char   rx_port_sel,                // Select HDMI RX input port: 0=PortA; 1=PortB; 2=PortC, 3=PortD; others=invalid.
                            unsigned char   hdmi_arctx_en,              // Audio Return Channel (ARC) transmission block control:0=Disable; 1=Enable.
                            unsigned char   hdmi_arctx_mode,            // ARC transmission mode: 0=Single-ended mode; 1=Common mode.
                            unsigned char   *hdmi_pll_lock)
{
    unsigned long   data32;

    stimulus_print("[TEST.C] Configure HDMIRX\n");

    Wr_reg_bits(HHI_GCLK_MPEG0, 1, 21, 1);  // Turn on clk_hdmirx_pclk, also = sysclk

    // Enable APB3 fail on error
    *((volatile unsigned long *) HDMIRX_CTRL_PORT)          |= (1 << 15);   // APB3 to HDMIRX-TOP err_en
    *((volatile unsigned long *) (HDMIRX_CTRL_PORT+0x10))   |= (1 << 15);   // APB3 to HDMIRX-DWC err_en

    //--------------------------------------------------------------------------
    // Enable HDMIRX interrupts:
    //--------------------------------------------------------------------------
    // [12]     meter_stable_chg_hdmi
    // [11]     vid_colour_depth_chg
    // [10]     vid_fmt_chg
    // [9:6]    hdmirx_5v_fall
    // [5:2]    hdmirx_5v_rise
    // [1]      edid_addr_intr
    // [0]      core_intr_rise: sub-interrupts will be configured later
    hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_INTR_MASKN, 0x00001fff);
    
    //--------------------------------------------------------------------------
    // Step 1-13: RX_INITIAL_CONFIG
    //--------------------------------------------------------------------------

    // 1. DWC reset default to be active, until reg HDMIRX_TOP_SW_RESET[0] is set to 0.
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_SW_RESET, 0x1, 0x0);
    
    // 2. turn on clocks: md, cfg...

    data32  = 0;
    data32 |= 0 << 25;  // [26:25] HDMIRX mode detection clock mux select: osc_clk
    data32 |= 1 << 24;  // [24]    HDMIRX mode detection clock enable
    data32 |= 0 << 16;  // [22:16] HDMIRX mode detection clock divider
    data32 |= 3 << 9;   // [10: 9] HDMIRX config clock mux select: fclk_div5=400MHz
    data32 |= 1 << 8;   // [    8] HDMIRX config clock enable
    data32 |= 3 << 0;   // [ 6: 0] HDMIRX config clock divider: 400/4=100MHz
    Wr(HHI_HDMIRX_CLK_CNTL,     data32);

    data32  = 0;
    data32 |= 2             << 25;  // [26:25] HDMIRX ACR ref clock mux select: fclk_div5
    data32 |= acr_mode      << 24;  // [24]    HDMIRX ACR ref clock enable
    data32 |= 0             << 16;  // [22:16] HDMIRX ACR ref clock divider
    data32 |= 2             << 9;   // [10: 9] HDMIRX audmeas clock mux select: fclk_div5
    data32 |= 1             << 8;   // [    8] HDMIRX audmeas clock enable
    data32 |= 1             << 0;   // [ 6: 0] HDMIRX audmeas clock divider: 400/2 = 200MHz
    Wr(HHI_HDMIRX_AUD_CLK_CNTL, data32);

    data32  = 0;
    data32 |= 1 << 17;  // [17]     audfifo_rd_en
    data32 |= 1 << 16;  // [16]     pktfifo_rd_en
    data32 |= 1 << 2;   // [2]      hdmirx_cecclk_en
    data32 |= 0 << 1;   // [1]      bus_clk_inv
    data32 |= 0 << 0;   // [0]      hdmi_clk_inv
    hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_CLK_CNTL, data32);    // DEFAULT: {32'h0}

    // 3. wait for TX PHY clock up
    
    // 4. wait for rx sense
    
    // 5. Release IP reset
    hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_SW_RESET, 0x0);

    // 6. Enable functional modules
    data32  = 0;
    data32 |= 1 << 5;   // [5]      cec_enable
    data32 |= 1 << 4;   // [4]      aud_enable
    data32 |= 1 << 3;   // [3]      bus_enable
    data32 |= 1 << 2;   // [2]      hdmi_enable
    data32 |= 1 << 1;   // [1]      modet_enable
    data32 |= 1 << 0;   // [0]      cfg_enable
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_DMI_DISABLE_IF, data32);    // DEFAULT: {31'd0, 1'b0}
    //Wr(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 5 ) {}  // delay 5uS
            mdelay(1);

    // 7. Reset functional modules
    hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_DMI_SW_RST,     0x0000007F);
    //Wr(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 10 ) {} // delay 10uS
            mdelay(1);
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_DMI_SW_RST,   0, 0);
    
    // 8. If defined, force manual N & CTS to speed up simulation

    data32  = 0;
    data32 |= 0         << 9;   // [9]      force_afif_status:1=Use cntl_audfifo_status_cfg as fifo status; 0=Use detected afif_status.
    data32 |= 1         << 8;   // [8]      afif_status_auto:1=Enable audio FIFO status auto-exit EMPTY/FULL, if FIFO level is back to LipSync; 0=Once enters EMPTY/FULL, never exits.
    data32 |= 1         << 6;   // [ 7: 6]  Audio FIFO nominal level :0=s_total/4;1=s_total/8;2=s_total/16;3=s_total/32.
    data32 |= 3         << 4;   // [ 5: 4]  Audio FIFO critical level:0=s_total/4;1=s_total/8;2=s_total/16;3=s_total/32.
    data32 |= 0         << 3;   // [3]      afif_status_clr:1=Clear audio FIFO status to IDLE.
    data32 |= acr_mode  << 2;   // [2]      dig_acr_en
    data32 |= 0         << 1;   // [1]      audmeas_clk_sel: 0=select aud_pll_clk; 1=select aud_acr_clk.
    data32 |= acr_mode  << 0;   // [0]      aud_clk_sel: 0=select aud_pll_clk; 1=select aud_acr_clk.
    hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_ACR_CNTL_STAT, data32);

    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUDPLL_GEN_CTS, manual_acr_cts);
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUDPLL_GEN_N,   manual_acr_n);

    // Force N&CTS to start with, will switch to received values later on, for simulation speed up.
    data32  = 0;
    data32 |= 1 << 4;   // [4]      cts_n_ref: 0=used decoded; 1=use manual N&CTS.
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_CLK_CTRL,   data32);

    data32  = 0;
    data32 |= 0 << 28;  // [28]     pll_lock_filter_byp
    data32 |= 0 << 24;  // [27:24]  pll_lock_toggle_div
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_PLL_CTRL,   data32);    // DEFAULT: {1'b0, 3'd0, 4'd6, 4'd3, 4'd8, 1'b0, 1'b0, 1'b1, 1'b0, 12'd0}
    
    // 9. Set EDID data at RX

    hdmirx_edid_setting(edid_extension_flag);

    data32  = 0;
    data32 |= 0                         << 13;  // [   13]  checksum_init_mode
    data32 |= edid_auto_checksum_enable << 12;  // [   12]  auto_checksum_enable
    data32 |= edid_auto_cec_enable      << 11;  // [   11]  auto_cec_enable
    data32 |= 0                         << 10;  // [   10]  scl_stretch_trigger_config
    data32 |= 0                         << 9;   // [    9]  force_scl_stretch_trigger
    data32 |= 1                         << 8;   // [    8]  scl_stretch_enable
    data32 |= edid_clk_divide_m1 << 0;   // [ 7: 0]  clk_divide_m1
    hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_EDID_GEN_CNTL,  data32);
    
    if (edid_cec_id_addr != 0x00990098) {
        hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_EDID_ADDR_CEC,  edid_cec_id_addr);
    }

    if (rx_port_sel == 0) {
        hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_EDID_DATA_CEC_PORT01,  ((edid_cec_id_data&0xff)<<8) | (edid_cec_id_data>>8));
    } else if (rx_port_sel == 1) {
        hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_EDID_DATA_CEC_PORT01,  (((edid_cec_id_data&0xff)<<8) | (edid_cec_id_data>>8))<<16);
    } else if (rx_port_sel == 2) {
        hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_EDID_DATA_CEC_PORT23,  ((edid_cec_id_data&0xff)<<8) | (edid_cec_id_data>>8));
    } else { // rx_port_sel == 3
        hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_EDID_DATA_CEC_PORT23,  (((edid_cec_id_data&0xff)<<8) | (edid_cec_id_data>>8))<<16);
    }
    
    // 10. HDCP
    if (hdcp_on) {
        data32  = 0;
        data32 |= 0                     << 14;  // [14]     hdcp_vid_de: Force DE=1.
        data32 |= 0                     << 10;  // [11:10]  hdcp_sel_avmute: 0=normal mode.
        data32 |= 0                     << 8;   // [9:8]    hdcp_ctl: 0=automatic.
        data32 |= 0                     << 6;   // [7:6]    hdcp_ri_rate: 0=Ri exchange once every 128 frames.
        data32 |= hdcp_key_decrypt_en   << 1;   // [1]      key_decrypt_enable
        data32 |= hdcp_on               << 0;   // [0]      hdcp_enable
        hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDCP_CTRL,  data32);
    
        data32  = 0;
        data32 |= 1                     << 16;  // [17:16]  i2c_spike_suppr
        data32 |= 1                     << 13;  // [13]     hdmi_reserved. 0=No HDMI capabilities.
        data32 |= 1                     << 12;  // [12]     fast_i2c
        data32 |= 1                     << 9;   // [9]      one_dot_one
        data32 |= 1                     << 8;   // [8]      fast_reauth
        data32 |= 0x3a                  << 1;   // [7:1]    hdcp_ddc_addr
        hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDCP_SETTINGS,  data32);    // DEFAAULT: {13'd0, 2'd1, 1'b1, 3'd0, 1'b1, 2'd0, 1'b1, 1'b1, 7'd58, 1'b0}

        hdmirx_key_setting(hdcp_key_decrypt_en);
    } /* if (hdcp_on) */

    // 11. RX configuration

    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_CKM_EVLTM, 0x0016fff0);
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_CKM_F,     0xf98a0190);

    data32  = 0;
    data32 |= 80    << 18;  // [26:18]  afif_th_start
    data32 |= 8     << 9;   // [17:9]   afif_th_max
    data32 |= 8     << 0;   // [8:0]    afif_th_min
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_FIFO_TH,    data32);

    data32  = 0;
    data32 |= 0     << 24;  // [25:24]  mr_vs_pol_adj_mode
    data32 |= 0     << 18;  // [18]     spike_filter_en
    data32 |= 0     << 13;  // [17:13]  dvi_mode_hyst
    data32 |= 0     << 8;   // [12:8]   hdmi_mode_hyst
    data32 |= 0     << 6;   // [7:6]    hdmi_mode: 0=automatic
    data32 |= 2     << 4;   // [5:4]    gb_det
    data32 |= 0     << 2;   // [3:2]    eess_oess
    data32 |= 1     << 0;   // [1:0]    sel_ctl01
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_MODE_RECOVER,  data32); // DEFAULT: {6'd0, 2'd0, 5'd0, 1'b0, 5'd8, 5'd8, 2'd0, 2'd0, 2'd0, 2'd0}

    data32  = 0;
    data32 |= 1     << 31;  // [31]     pfifo_store_filter_en
    data32 |= 1     << 26;  // [26]     pfifo_store_mpegs_if
    data32 |= 1     << 25;  // [25]     pfifo_store_aud_if
    data32 |= 1     << 24;  // [24]     pfifo_store_spd_if
    data32 |= 1     << 23;  // [23]     pfifo_store_avi_if
    data32 |= 1     << 22;  // [22]     pfifo_store_vs_if
    data32 |= 1     << 21;  // [21]     pfifo_store_gmtp
    data32 |= 1     << 20;  // [20]     pfifo_store_isrc2
    data32 |= 1     << 19;  // [19]     pfifo_store_isrc1
    data32 |= 1     << 18;  // [18]     pfifo_store_acp
    data32 |= 0     << 17;  // [17]     pfifo_store_gcp
    data32 |= 0     << 16;  // [16]     pfifo_store_acr
    data32 |= 0     << 14;  // [14]     gcpforce_clravmute
    data32 |= 0     << 13;  // [13]     gcpforce_setavmute
    data32 |= 0     << 12;  // [12]     gcp_avmute_allsps
    data32 |= 0     << 8;   // [8]      pd_fifo_fill_info_clr
    data32 |= 0     << 6;   // [6]      pd_fifo_skip
    data32 |= 0     << 5;   // [5]      pd_fifo_clr
    data32 |= 1     << 4;   // [4]      pd_fifo_we
    data32 |= 1     << 0;   // [0]      pdec_bch_en
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_PDEC_CTRL,  data32); // DEFAULT: {23'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 3'd0, 1'b0}

    data32  = 0;
    data32 |= 1     << 6;   // [6]      auto_vmute
    data32 |= 0xf   << 2;   // [5:2]    auto_spflat_mute
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_PDEC_ASP_CTRL,  data32); // DEFAULT: {25'd0, 1'b1, 4'd0, 2'd0}

    data32  = 0;
    data32 |= 1     << 16;  // [16]     afif_subpackets: 0=store all sp; 1=store only the ones' spX=1.
    data32 |= 0     << 0;   // [0]      afif_init
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_FIFO_CTRL,  data32); // DEFAULT: {13'd0, 2'd0, 1'b1, 15'd0, 1'b0}

    data32  = 0;
    data32 |= 0     << 10;  // [10]     ws_disable
    data32 |= 0     << 9;   // [9]      sck_disable
    data32 |= 0     << 5;   // [8:5]    i2s_disable
    data32 |= 0     << 1;   // [4:1]    spdif_disable
    data32 |= 1     << 0;   // [0]      i2s_32_16 
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_SAO_CTRL,   data32); // DEFAULT: {21'd0, 1'b1, 1'b1, 4'd15, 4'd15, 1'b1}

    // Manual de-repeat to speed up simulation
    data32  = 0;
    data32 |= pixel_repeat_hdmi << 1;   // [4:1]    man_vid_derepeat
    data32 |= 0                 << 0;   // [0]      auto_derepeat
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_RESMPL_CTRL,   data32); // DEFAULT: {27'd0, 4'd0, 1'b1}

    // At the 1st frame, HDMIRX hasn't received AVI packet, to speed up receiving YUV422 video, force oavi_video_format=1, release forcing on receiving AVI packet.
    if (rx_input_color_format == 1) {
        /*stimulus_event(31, STIMULUS_HDMI_UTIL_VID_FORMAT    |
                           (1                       << 4)   |   // 0=Release force; 1=Force vid_fmt
                           (rx_input_color_format   << 0));     // Video format: 0=RGB444; 1=YCbCr422; 2=YCbCr444.
                           */
    }

    // At the 1st frame, HDMIRX hasn't received AVI packet, HDMIRX default video format to RGB, so manual/force something to speed up simulation:
    data32  = 0;
    data32 |= (((rx_input_color_depth==3) ||
                (rx_hscale_half==1))? 1:0)  << 29;  // [29]     cntl_vid_clk_half: To make timing easier, this bit can be set to 1: if input is dn-sample by 1, or input is 3*16-bit.
    data32 |= 0                             << 28;  // [28]     cntl_vs_timing: 0=Detect VS rising; 1=Detect HS falling.
    data32 |= 0                             << 27;  // [27]     cntl_hs_timing: 0=Detect HS rising; 1=Detect HS falling.
    // For receiving YUV444 video, we manually map component data to speed up simulation, manual-mapping will be cancelled once AVI is received.
    if (rx_input_color_format == 2) {
        data32 |= 2                         << 24;  // [26:24]  vid_data_map. 2={vid1, vid0, vid2}->{vid2, vid1, vid0}
    } else {
        data32 |= 0                         << 24;  // [26:24]  vid_data_map. 0={vid2, vid1, vid0}->{vid2, vid1, vid0}
    }
    data32 |= rx_hscale_half                << 23;  // [23]     hscale_half: 1=Horizontally scale down by half
    data32 |= 0                             << 22;  // [22]     force_vid_rate: 1=Force video output sample rate
    data32 |= 0                             << 19;  // [21:19]  force_vid_rate_chroma_cfg : 0=Bypass, not rate change. Applicable only if force_vid_rate=1
    data32 |= 0                             << 16;  // [18:16]  force_vid_rate_luma_cfg   : 0=Bypass, not rate change. Applicable only if force_vid_rate=1
    data32 |= 0x7fff                        << 0;   // [14: 0]  hsizem1
    hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_VID_CNTL,   data32);

    // To speed up simulation:
    // Force VS polarity until for the first 2 frames, because it takes one whole frame for HDMIRX to detect the correct VS polarity;
    // HS polarity can be detected just after one line, so it can be set to auto-detect from the start.
    data32  = 0;
    data32 |= vsync_polarity    << 3;   // [4:3]    vs_pol_adj_mode:0=invert input VS; 1=no invert; 2=auto convert to high active; 3=no invert.
    data32 |= 2                 << 1;   // [2:1]    hs_pol_adj_mode:0=invert input VS; 1=no invert; 2=auto convert to high active; 3=no invert.
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_SYNC_CTRL,     data32); // DEFAULT: {27'd0, 2'd0, 2'd0, 1'b0}

    data32  = 0;
    data32 |= 3     << 21;  // [22:21]  aport_shdw_ctrl
    data32 |= 2     << 19;  // [20:19]  auto_aclk_mute
    data32 |= 1     << 10;  // [16:10]  aud_mute_speed
    data32 |= 1     << 7;   // [7]      aud_avmute_en
    data32 |= 1     << 5;   // [6:5]    aud_mute_sel
    data32 |= 1     << 3;   // [4:3]    aud_mute_mode
    data32 |= 0     << 1;   // [2:1]    aud_ttone_fs_sel
    data32 |= 0     << 0;   // [0]      testtone_en 
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_MUTE_CTRL,  data32); // DEFAULT: {9'd0, 2'd0, 2'd0, 2'd0, 7'd48, 2'd0, 1'b1, 2'd3, 2'd3, 2'd0, 1'b0}

    data32  = 0;
    data32 |= 0     << 4;   // [11:4]   audio_fmt_chg_thres
    data32 |= 0     << 1;   // [2:1]    audio_fmt
    data32 |= 0     << 0;   // [0]      audio_fmt_sel
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_PAO_CTRL,   data32); // DEFAULT: {20'd0, 8'd176, 1'b0, 2'd0, 1'b0}

    data32  = 0;
    data32 |= (rx_8_channel? 0x7 :0x0)  << 8;   // [10:8]   ch_map[7:5]
    data32 |= 1                         << 7;   // [7]      ch_map_manual
    data32 |= (rx_8_channel? 0x1f:0x3)  << 2;   // [6:2]    ch_map[4:0]
    data32 |= 1                         << 0;   // [1:0]    aud_layout_ctrl:0/1=auto layout; 2=layout 0; 3=layout 1.
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_CHEXTR_CTRL,    data32); // DEFAULT: {24'd0, 1'b0, 5'd0, 2'd0}

    data32  = 0;
    data32 |= 0     << 8;   // [8]      fc_lfe_exchg: 1=swap channel 3 and 4
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_PDEC_AIF_CTRL,  data32); // DEFAULT: {23'd0, 1'b0, 8'd0}

    data32  = 0;
    data32 |= 0     << 20;  // [20]     rg_block_off:1=Enable HS/VS/CTRL filtering during active video
    data32 |= 1     << 19;  // [19]     block_off:1=Enable HS/VS/CTRL passing during active video
    data32 |= 5     << 16;  // [18:16]  valid_mode
    data32 |= 0     << 12;  // [13:12]  ctrl_filt_sens
    data32 |= 3     << 10;  // [11:10]  vs_filt_sens
    data32 |= 0     << 8;   // [9:8]    hs_filt_sens
    data32 |= 2     << 6;   // [7:6]    de_measure_mode
    data32 |= 0     << 5;   // [5]      de_regen
    data32 |= 3     << 3;   // [4:3]    de_filter_sens 
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_ERROR_PROTECT, data32); // DEFAULT: {11'd0, 1'b0, 1'b0, 3'd0, 2'd0, 2'd0, 2'd0, 2'd0, 2'd0, 1'b0, 2'd0, 3'd0}

    data32  = 0;
    data32 |= 0     << 8;   // [10:8]   hact_pix_ith
    data32 |= 0     << 5;   // [5]      hact_pix_src
    data32 |= 1     << 4;   // [4]      htot_pix_src
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_MD_HCTRL1,  data32); // DEFAULT: {21'd0, 3'd1, 2'd0, 1'b0, 1'b1, 4'd0}

    data32  = 0;
    data32 |= 1     << 12;  // [14:12]  hs_clk_ith
    data32 |= 7     << 8;   // [10:8]   htot32_clk_ith
    data32 |= 1     << 5;   // [5]      vs_act_time
    data32 |= 3     << 3;   // [4:3]    hs_act_time
    data32 |= 0     << 0;   // [1:0]    h_start_pos
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_MD_HCTRL2,  data32); // DEFAULT: {17'd0, 3'd2, 1'b0, 3'd1, 2'd0, 1'b0, 2'd0, 1'b0, 2'd2}

    data32  = 0;
    data32 |= 1                 << 4;   // [4]      v_offs_lin_mode
    data32 |= 1                 << 1;   // [1]      v_edge
    data32 |= interlace_mode    << 0;   // [0]      v_mode
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_MD_VCTRL,   data32); // DEFAULT: {27'd0, 1'b0, 2'd0, 1'b1, 1'b0}

    data32  = 0;
    data32 |= 1 << 10;  // [11:10]  vofs_lin_ith
    data32 |= 3 << 8;   // [9:8]    vact_lin_ith 
    data32 |= 0 << 6;   // [7:6]    vtot_lin_ith
    data32 |= 7 << 3;   // [5:3]    vs_clk_ith
    data32 |= 2 << 0;   // [2:0]    vtot_clk_ith
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_MD_VTH,     data32); // DEFAULT: {20'd0, 2'd2, 2'd0, 2'd0, 3'd2, 3'd2}

    data32  = 0;
    data32 |= 1 << 2;   // [2]      fafielddet_en
    data32 |= 0 << 0;   // [1:0]    field_pol_mode
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_MD_IL_POL,  data32); // DEFAULT: {29'd0, 1'b0, 2'd0}

    data32  = 0;
    data32 |= 0 << 2;   // [4:2]    deltacts_irqtrig
    data32 |= 0 << 0;   // [1:0]    cts_n_meas_mode
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_PDEC_ACRM_CTRL, data32); // DEFAULT: {27'd0, 3'd0, 2'd1}

    // 12. RX PHY GEN3 configuration

    // Turn on interrupts that to do with PHY communication
    hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_ICLR,         0xffffffff);
    *curr_hdmi_ien_maskn    = hdmi_ien_maskn;
    hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_IEN_SET,      hdmi_ien_maskn);
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_ISTS,        0, 0);

    // PDDQ = 1'b1; PHY_RESET = 1'b1;
    data32  = 0;
    data32 |= 1             << 6;   // [6]      physvsretmodez
    data32 |= 1             << 4;   // [5:4]    cfgclkfreq
    data32 |= rx_port_sel   << 2;   // [3:2]    portselect
    data32 |= 1             << 1;   // [1]      phypddq
    data32 |= 1             << 0;   // [0]      phyreset
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_SNPS_PHYG3_CTRL,    data32); // DEFAULT: {27'd0, 3'd0, 2'd1}
    //Wr(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 1 ) {} // delay 1uS
        mdelay(1);

    // PDDQ = 1'b1; PHY_RESET = 1'b0;
    data32  = 0;
    data32 |= 1             << 6;   // [6]      physvsretmodez
    data32 |= 1             << 4;   // [5:4]    cfgclkfreq
    data32 |= rx_port_sel   << 2;   // [3:2]    portselect
    data32 |= 1             << 1;   // [1]      phypddq
    data32 |= 0             << 0;   // [0]      phyreset
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_SNPS_PHYG3_CTRL,    data32); // DEFAULT: {27'd0, 3'd0, 2'd1}

    // Configuring I2C to work in fastmode
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_I2CM_PHYG3_MODE,    0x1);

    // Write PHY register 0x02 -> { 6'b001000, 1'b1, timebase_ovr[8:0]};
    //                                              - timebase_ovr[8:0] = Fcfg_clk(MHz) x 4;
    data32  = 0;
    data32 |= 8         << 10;  // [15:10]  lock_thres
    data32 |= 1         << 9;   // [9]      timebase_ovr_en
    data32 |= (25 * 4)  << 0;   // [8:0]    timebase_ovr = F_cfgclk(MHz) * 4
    hdmirx_wr_reg(HDMIRX_DEV_ID_PHY, 0x02, data32);

    //------------------------------------------------------------------------------------------
    // Write PHY register 0x03 -> {9'b000000100, color_depth[1:0], 5'b0000};
    //                                              - color_depth = 00 ->  8bits;
    //                                              - color_depth = 01 -> 10bits;
    //                                              - color_depth = 10 -> 12bits;
    //                                              - color_depth = 11 -> 16bits.
    //------------------------------------------------------------------------------------------
    data32  = 0;
    data32 |= 0                     << 15;  // [15]     mpll_short_power_up
    data32 |= 0                     << 13;  // [14:13]  mpll_mult
    data32 |= 0                     << 12;  // [12]     dis_off_lp
    data32 |= 0                     << 11;  // [11]     fast_switching
    data32 |= 0                     << 10;  // [10]     bypass_afe
    data32 |= 1                     << 9;   // [9]      fsm_enhancement
    data32 |= 0                     << 8;   // [8]      low_freq_eq
    data32 |= 0                     << 7;   // [7]      bypass_aligner
    data32 |= rx_input_color_depth  << 5;   // [6:5]    color_depth: 0=8-bit; 1=10-bit; 2=12-bit; 3=16-bit.
    data32 |= 0                     << 3;   // [4:3]    sel_tmdsclk: 0=Use chan0 clk; 1=Use chan1 clk; 2=Use chan2 clk; 3=Rsvd.
    data32 |= 0                     << 2;   // [2]      port_select_ovr_en
    data32 |= 0                     << 0;   // [1:0]    port_select_ovr
    hdmirx_wr_only_reg(HDMIRX_DEV_ID_PHY, 0x03, data32);

    // PDDQ = 1'b0; PHY_RESET = 1'b0;
    data32  = 0;
    data32 |= 1             << 6;   // [6]      physvsretmodez
    data32 |= 1             << 4;   // [5:4]    cfgclkfreq
    data32 |= rx_port_sel   << 2;   // [3:2]    portselect
    data32 |= 0             << 1;   // [1]      phypddq
    data32 |= 0             << 0;   // [0]      phyreset
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_SNPS_PHYG3_CTRL,    data32); // DEFAULT: {27'd0, 3'd0, 2'd1}

    data32  = 0;
    data32 |= 0                     << 15;  // [15]     mpll_short_power_up
    data32 |= 0                     << 13;  // [14:13]  mpll_mult
    data32 |= 0                     << 12;  // [12]     dis_off_lp
    data32 |= 0                     << 11;  // [11]     fast_switching
    data32 |= 0                     << 10;  // [10]     bypass_afe
    data32 |= 1                     << 9;   // [9]      fsm_enhancement
    data32 |= 0                     << 8;   // [8]      low_freq_eq
    data32 |= 0                     << 7;   // [7]      bypass_aligner
    data32 |= rx_input_color_depth  << 5;   // [6:5]    color_depth: 0=8-bit; 1=10-bit; 2=12-bit; 3=16-bit.
    data32 |= 0                     << 3;   // [4:3]    sel_tmdsclk: 0=Use chan0 clk; 1=Use chan1 clk; 2=Use chan2 clk; 3=Rsvd.
    data32 |= 0                     << 2;   // [2]      port_select_ovr_en
    data32 |= 0                     << 0;   // [1:0]    port_select_ovr
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_PHY, 0x03, data32, 0);

    // 13.  HDMI RX Ready! - Assert HPD
    stimulus_print("[TEST.C] HDMI RX Ready! - Assert HPD\n");

    hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_PORT_SEL,   (1<<rx_port_sel));

    data32  = 0;
    data32 |= 1                 << 5;   // [    5]  invert_hpd
    data32 |= 1                 << 4;   // [    4]  force_hpd: default=1
    data32 |= (1<<rx_port_sel)  << 0;   // [ 3: 0]  hpd_config
    hdmirx_wr_only_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_HPD_PWR5V,  data32);

    // Configure external video data generator and analyzer
    /*
    start_video_gen_ana(vic,                // Video format identification code
                        pixel_repeat_hdmi,
                        interlace_mode,     // 0=Progressive; 1=Interlace.
                        front_porch,        // Number of pixels from DE Low to HSYNC high
                        back_porch,         // Number of pixels from HSYNC low to DE high
                        hsync_pixels,       // Number of pixels of HSYNC pulse
                        hsync_polarity,     // TX HSYNC polarity: 0=low active; 1=high active.
                        sof_lines,          // HSYNC count between VSYNC de-assertion and first line of active video
                        eof_lines,          // HSYNC count between last line of active video and start of VSYNC
                        vsync_lines,        // HSYNC count of VSYNC assertion
                        vsync_polarity,     // TX VSYNC polarity: 0=low active; 1=high active.
                        total_pixels,       // Number of total pixels per line
                        total_lines);       // Number of total lines per frame
    */
    // 14.  RX_FINAL_CONFIG
    
    // RX PHY PLL configuration
    //get config for CMU
    /*stimulus_event(31, STIMULUS_HDMI_UTIL_CALC_PLL_CONFIG   |
                       (0   << 4)                           |   // mdclk freq: 0=24MHz; 1=25MHz; 2=27MHz.
                       (1   << 0));                             // tmds_clk_freq: 0=25MHz; 1=27MHz; 2=54MHz; 3=74.25MHz; 4=148.5MHz; 5=27*5/4MHz.
                       */
//        //margin of +/-0.78% for clock drift
//        clockrate_max[15:0] = (expected_clockrate[15:0]+expected_clockrate[15:7]);
//        clockrate_min[15:0] = (expected_clockrate[15:0]-expected_clockrate[15:7]);

    data32  = 0;
    data32 |= 1     << 20;  // [21:20]  lock_hyst
    data32 |= 0     << 16;  // [18:16]  clk_hyst
    data32 |= 2490  << 4;   // [15:4]   eval_time
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_CKM_EVLTM, data32);    // DEFAULT: {10'd0, 2'd1, 1'b0, 3'd0, 12'd4095, 3'd0, 1'b0}

    data32  = 0;
    data32 |= 3533  << 16;  // [31:16]  maxfreq
    data32 |= 3479  << 0;   // [15:0]   minfreq
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_CKM_F, data32);    // DEFAULT: {16'd63882, 16'd9009}

    // RX PHY PLL lock wait
    stimulus_print("[TEST.C] WAITING FOR TMDSVALID-------------------\n");
    //while (! (*hdmi_pll_lock)) {
    while(1){
        if( hdmirx_rd_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_PLL_LCK_STS) & 0x1)
            break;
        mdelay(1);
        //Wr(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 10 ) {} // delay 10uS
    }
    hdmirx_poll_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_CKM_RESULT, 1<<16, ~(1<<16));

    // 15. Waiting for AUDIO PLL to lock before performing RX synchronous resets!
    //hdmirx_poll_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_PLL_CTRL, 1<<31, ~(1<<31));

    // 16. RX Reset

    data32  = 0;
    data32 |= 0 << 5;   // [5]      cec_enable
    data32 |= 0 << 4;   // [4]      aud_enable
    data32 |= 0 << 3;   // [3]      bus_enable
    data32 |= 0 << 2;   // [2]      hdmi_enable
    data32 |= 0 << 1;   // [1]      modet_enable
    data32 |= 1 << 0;   // [0]      cfg_enable
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_DMI_DISABLE_IF, data32);    // DEFAULT: {31'd0, 1'b0}

    //Wr(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 1 ) {} // delay 1uS
        mdelay(1);

    //--------------------------------------------------------------------------
    // Enable HDMIRX-DWC interrupts:
    //--------------------------------------------------------------------------
    
    hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_PDEC_ICLR,         0xffffffff);
    hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_CLK_ICLR,      0xffffffff);
    hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_FIFO_ICLR,     0xffffffff);
    hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_MD_ICLR,           0xffffffff);
    //hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_ICLR,         0xffffffff);

    *curr_pdec_ien_maskn     = pdec_ien_maskn;
    *curr_aud_clk_ien_maskn  = aud_clk_ien_maskn;
    *curr_aud_fifo_ien_maskn = aud_fifo_ien_maskn;
    *curr_md_ien_maskn       = md_ien_maskn;

    hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_PDEC_IEN_SET,      pdec_ien_maskn);
    hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_CLK_IEN_SET,   aud_clk_ien_maskn);
    hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_FIFO_IEN_SET,  aud_fifo_ien_maskn);
    hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_MD_IEN_SET,        md_ien_maskn);
    //hdmirx_wr_only_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_IEN_SET,      hdmi_ien_maskn);

    hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_PDEC_ISTS,        0, 0);
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_CLK_ISTS,     0, 0);
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_AUD_FIFO_ISTS,    0, 0);
    hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_MD_ISTS,          0, 0);
    //hdmirx_rd_check_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_HDMI_ISTS,        0, 0);
    
    //--------------------------------------------------------------------------
    // Bring up RX
    //--------------------------------------------------------------------------
    data32  = 0;
    data32 |= 1 << 5;   // [5]      cec_enable
    data32 |= 1 << 4;   // [4]      aud_enable
    data32 |= 1 << 3;   // [3]      bus_enable
    data32 |= 1 << 2;   // [2]      hdmi_enable
    data32 |= 1 << 1;   // [1]      modet_enable
    data32 |= 1 << 0;   // [0]      cfg_enable
    hdmirx_wr_reg(HDMIRX_DEV_ID_DWC, HDMIRX_DWC_DMI_DISABLE_IF, data32);    // DEFAULT: {31'd0, 1'b0}

    //Wr(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 10 ) {} // delay 10uS
        mdelay(1);

    // To speed up simulation, reset the video generator after HDMIRX-PHY is locked,
    // so that HDMIRX-DWC doesn't have wait for a whole frame before seeing the 1st Vsync.
    //stimulus_event(31, STIMULUS_HDMI_UTIL_VGEN_RESET        | 1);
    //stimulus_event(31, STIMULUS_HDMI_UTIL_VGEN_RESET        | 0);

    // Enable HDMI_ARCTX if needed
    if (hdmi_arctx_en) {
        data32  = 0;
        data32 |= hdmi_arctx_mode   << 1;   // [1]      arctx_mode
        data32 |= 0                 << 0;   // [0]      arctx_en
        hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_ARCTX_CNTL, data32);
        
        data32  = 0;
        data32 |= hdmi_arctx_mode   << 1;   // [1]      arctx_mode
        data32 |= hdmi_arctx_en     << 0;   // [0]      arctx_en
        hdmirx_wr_reg(HDMIRX_DEV_ID_TOP, HDMIRX_TOP_ARCTX_CNTL, data32);
    }
//        register_read(`RX_HDMI_ERD_STS,supportreg,"VERBOSE_MODE");
//        check_vector("Acc valid indication",supportreg,32'd0,32,error_tmp,"NOPRINT"); errorsum(error,error_tmp,error);
//        wait_for(5000,"VERBOSE_MODE");
//        register_read(`RX_HDMI_ERD_STS,supportreg,"VERBOSE_MODE");
//        check_vector("Acc valid indication",supportreg,32'd7,32,error_tmp,"NOPRINT"); errorsum(error,error_tmp,error);
//
//        wait_for(5000,"VERBOSE_MODE");
//
//        display_msg( "END OF HDMI RX CONFIGURATION", "DEBUG_MODE");
//
//      register_read(  `RX_PDEC_STS  ,  supportreg, "VERBOSE_MODE");
//      register_read(  `RX_MD_IL_SKEW  ,  supportreg, "NO_PRINT");
//      {phase, skew} = supportreg[3:0];
//      if (phase) begin
//        display_msg("ERROR - PHASE should be 0! Phase 1 detected","VERBOSE_MODE");
//        errorsum(error,1,error);
//      end
//
//      display_msg("############################### VS vs HS Skew Results ##################################", "VERBOSE_MODE");
//      if ((skew != 0) && (skew != 3) && (skew != 4) && (skew != 7)) begin
//        $sformat(supportstring,"   ERROR - Current frame skew %d/8 of a line width (phase %d)                   ", (skew+1), phase);
//        errorsum(error,1,error);
//      end
//      else $sformat(supportstring,"    Current frame skew %d/8 of a line width (phase %d)                           ", (skew+1), phase);
//      display_msg(supportstring, "VERBOSE_MODE");
//      display_msg("########################################################################################", "VERBOSE_MODE");
//
//          register_read(  `RX_MD_VSC      , { supportreg[15:0], vs_clk_temp  }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_HT0      , { htot32_clk      , hs_clk       }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_HT1      , { htot_pix        , hofs_pix     }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_HACT_PX  , { supportreg[15:0], hact_pix     }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_VSC      , { supportreg[15:0], vs_clk_temp  }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_VTC      , { vtot_clk                       }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_VOL      , { supportreg[15:0], vofs_lin     }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_VAL      , { supportreg[15:0], vact_lin     }  , "VERBOSE_MODE");
//    register_read(  `RX_MD_VTL      , { supportreg[15:0], vtot_lin     }  , "VERBOSE_MODE");
//      register_read(  `RX_AUD_FIFO_STS    , supportreg  , "VERBOSE_MODE");

} /* hdmirx_test_function */

#if 0
void aocec_poll_reg_busy (unsigned char reg_busy)
{
    if (reg_busy) {
        stimulus_print("[TEST.C] Polling AO_CEC reg_busy=1\n");
    } else {
        stimulus_print("[TEST.C] Polling AO_CEC reg_busy=0\n");
    }
    while (((*P_AO_CEC_RW_REG) & (1 << 23)) != (reg_busy << 23)) {
        Wr(ISA_TIMERE, 0); while( Rd(ISA_TIMERE) < 31 ) {}
    }
} /* aocec_poll_reg_busy */

void aocec_wr_only_reg (unsigned long addr, unsigned long data)
{
    unsigned long data32;
    
    aocec_poll_reg_busy(0);
    
    data32  = 0;
    data32 |= 1     << 16;  // [16]     cec_reg_wr
    data32 |= data  << 8;   // [15:8]   cec_reg_wrdata
    data32 |= addr  << 0;   // [7:0]    cec_reg_addr
    (*P_AO_CEC_RW_REG) = data32;
} /* aocec_wr_only_reg */

unsigned long aocec_rd_reg (unsigned long addr)
{
    unsigned long data32;
    
    aocec_poll_reg_busy(0);
    
    data32  = 0;
    data32 |= 0     << 16;  // [16]     cec_reg_wr
    data32 |= 0     << 8;   // [15:8]   cec_reg_wrdata
    data32 |= addr  << 0;   // [7:0]    cec_reg_addr
    (*P_AO_CEC_RW_REG) = data32;

    aocec_poll_reg_busy(1);
    aocec_poll_reg_busy(0);
    
    data32 = ((*P_AO_CEC_RW_REG) >> 24) & 0xff;
    
    return (data32);
} /* aocec_rd_reg */

void aocec_rd_check_reg (unsigned long addr, unsigned long exp_data, unsigned long mask)
{
    unsigned long rd_data;
    rd_data = aocec_rd_reg(addr);
    if ((rd_data | mask) != (exp_data | mask)) 
    {
        stimulus_print("Error: AO-CEC addr=0x");
        stimulus_print_num_hex(addr);
        stimulus_print_without_timestamp(" rd_data=0x");
        stimulus_print_num_hex(rd_data);
        stimulus_print_without_timestamp(" exp_data=0x");
        stimulus_print_num_hex(exp_data);
        stimulus_print_without_timestamp(" mask=0x");
        stimulus_print_num_hex(mask);
        stimulus_print_without_timestamp("\n");
        //stimulus_finish_fail(10);
    }
} /* aocec_rd_check_reg */

void aocec_wr_reg (unsigned long addr, unsigned long data)
{
    aocec_wr_only_reg(addr, data);
    aocec_rd_check_reg(addr, data, 0);
} /* aocec_wr_reg */

#endif

