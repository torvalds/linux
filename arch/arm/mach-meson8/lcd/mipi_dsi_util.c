#include <linux/types.h>
#include <mach/cpu.h>
#include <plat/cpu.h>
#include <mach/io.h>
#include <plat/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <mach/lcd_reg.h>
#include <mach/mipi_dsi_reg.h>
#include <mach/lcdoutc.h>
#include <linux/amlogic/vout/lcdoutc.h>
#include <linux/amlogic/vout/aml_lcd_extern.h>
#include "lcd_config.h"
#include "mipi_dsi_util.h"

#define DPRINT(...)		printk(__VA_ARGS__)
//#define MIPI_DSI_COMMAND_READ
//===============================================================================
// Define MIPI DSI Default config
//===============================================================================
#define MIPI_DSI_VIRTUAL_CHAN_ID        0                       // Range [0,3]
#define MIPI_DSI_CMD_TRANS_TYPE         DCS_TRANS_LP            // Define DSI command transfer type: high speed or low power
#define MIPI_DSI_DCS_ACK_TYPE           MIPI_DSI_DCS_NO_ACK     // Define if DSI command need ack: req_ack or no_ack
#define MIPI_DSI_VIDEO_MODE_TYPE        BURST_MODE              // Applicable only to video mode. Define data transfer method: non-burst sync pulse; non-burst sync event; or burst.
#define MIPI_DSI_COLOR_18BIT            COLOR_18BIT_CFG_1
#define MIPI_DSI_COLOR_24BIT            COLOR_24BIT
#define MIPI_DSI_TEAR_SWITCH            MIPI_DCS_DISABLE_TEAR
#define CMD_TIMEOUT_CNT                 3000
//===============================================================================

static const char *video_mode_type_table[] = {
    "COLOR_16BIT_CFG_1",
    "COLOR_16BIT_CFG_2",
    "COLOR_16BIT_CFG_3",
    "COLOR_18BIT_CFG_1",
    "COLOR_18BIT_CFG_2(loosely)",
    "COLOR_24BIT",
    "COLOR_20BIT_LOOSE",
    "COLOR_24_BIT_YCBCR",
    "COLOR_16BIT_YCBCR",
    "COLOR_30BIT",
    "COLOR_36BIT",
    "COLOR_12BIT",
    "COLOR_RGB_111",
    "COLOR_RGB_332",
    "COLOR_RGB_444",
    "un-support type",
};

static DSI_Phy_t dsi_phy_config;
static DSI_Vid_t dsi_vid_config;
static DSI_Config_t *dsi_config = NULL;
static unsigned char dsi_init_on_table_dft[] = {
    0x05,0x11,0,
    0xff,50,
    0x05,0x29,0,
    0xff,20,
    0xff,0xff,
};

static inline void print_mipi_cmd_status(int cnt, unsigned status)
{
    if (cnt == 0) {
        DPRINT("cmd error: status=0x%04x, int0=0x%06x, int1=0x%06x\n", status, READ_LCD_REG(MIPI_DSI_DWC_INT_ST0_OS), READ_LCD_REG(MIPI_DSI_DWC_INT_ST1_OS));
    }
}

static void print_info(void)
{
    int i, j, n;
    unsigned temp;

    if (dsi_config == NULL) {
        DPRINT("dsi config is NULL\n");
        return;
    }
    DPRINT("================================================\n");
    DPRINT("MIPI DSI Config\n");
    DPRINT(" Lane Num:              %d\n", dsi_config->lane_num);
    //DPRINT(" Bit Rate min:          %dMHz\n", (dsi_config->bit_rate_min / 1000));
    DPRINT(" Bit Rate max:          %dMHz\n", (dsi_config->bit_rate_max / 1000));
    DPRINT(" Bit Rate:              %d.%03dMHz\n", (dsi_config->bit_rate / 1000000), (dsi_config->bit_rate % 1000000) / 1000);
    DPRINT(" Pclk lanebyte factor:  %d\n", ((dsi_config->factor_numerator * 100 / dsi_config->factor_denominator) + 5) / 10);
    DPRINT(" Operation mode:\n");
    DPRINT("     init:              %s\n", ((dsi_config->operation_mode>>BIT_OPERATION_MODE_INIT) & 1) ? "COMMAND":"VIDEO");
    DPRINT("     display:           %s\n", ((dsi_config->operation_mode>>BIT_OPERATION_MODE_DISP) & 1) ? "COMMAND":"VIDEO");
    DPRINT(" Transfer control:\n");
    DPRINT("     clk auto stop:     %d\n", ((dsi_config->transfer_ctrl>>BIT_TRANS_CTRL_CLK) & 1));
    DPRINT("     transfer switch:   %d\n", ((dsi_config->transfer_ctrl>>BIT_TRANS_CTRL_SWITCH) & 3));
    if(dsi_config->video_mode_type == NON_BURST_SYNC_PULSE) {
        DPRINT(" Video mode type:       NON_BURST_SYNC_PULSE\n");
    }
    else if(dsi_config->video_mode_type == NON_BURST_SYNC_EVENT) {
        DPRINT(" Video mode type:       NON_BURST_SYNC_EVENT\n");
    }
    else if(dsi_config->video_mode_type == BURST_MODE) {
        DPRINT(" Video mode type:       BURST_MODE\n");
    }

    //DPRINT(" Venc format:           %d\n", dsi_config->venc_fmt);
    DPRINT(" Data Format:           %s\n\n", video_mode_type_table[dsi_config->dpi_data_format]);
    //DPRINT(" POLARITY:              HIGH ACTIVE\n");
    //DPRINT(" Enable CRC/ECC/BTA\n");

    temp = dsi_config->bit_rate / 8 / dsi_phy_config.lp_tesc;
    DPRINT("DSI LP escape clock:    %d.%03dMHz\n", (temp / 1000000), (temp % 1000000) / 1000);
    if (dsi_config->dsi_init_on) {
        DPRINT("DSI INIT ON:\n");
        i = 0;
        while (i < DSI_INIT_ON_MAX) {
            if (dsi_config->dsi_init_on[i] == 0xff) {
                n = 2;
                if (dsi_config->dsi_init_on[i+1] == 0xff) {
                    DPRINT("    0x%02x,0x%02x,\n", dsi_config->dsi_init_on[i], dsi_config->dsi_init_on[i+1]);
                    break;
                }
                else {
                    DPRINT("    0x%02x,%d,\n", dsi_config->dsi_init_on[i], dsi_config->dsi_init_on[i+1]);
                }
            }
            else {
                n = 3 + dsi_config->dsi_init_on[i+2];
                DPRINT("    ");
                for (j=0; j<n; j++) {
                    if (j == 2)
                        DPRINT("%d,", dsi_config->dsi_init_on[i+j]);
                    else
                        DPRINT("0x%02x,", dsi_config->dsi_init_on[i+j]);
                }
                DPRINT("\n");
            }
            i += n;
        }
    }
    if (dsi_config->dsi_init_off) {
        DPRINT("DSI INIT OFF:\n");
        i = 0;
        while (i < DSI_INIT_OFF_MAX) {
            if (dsi_config->dsi_init_off[i] == 0xff) {
                n = 2;
                if (dsi_config->dsi_init_off[i+1] == 0xff) {
                    DPRINT("    0x%02x,0x%02x,\n", dsi_config->dsi_init_off[i], dsi_config->dsi_init_off[i+1]);
                    break;
                }
                else {
                    DPRINT("    0x%02x,%d,\n", dsi_config->dsi_init_off[i], dsi_config->dsi_init_off[i+1]);
                }
            }
            else {
                n = 3 + dsi_config->dsi_init_off[i+2];
                DPRINT("    ");
                for (j=0; j<n; j++) {
                    if (j == 2)
                        DPRINT("%d,", dsi_config->dsi_init_off[i+j]);
                    else
                        DPRINT("0x%02x,", dsi_config->dsi_init_off[i+j]);
                }
                DPRINT("\n");
            }
            i += n;
        }
    }
    DPRINT("DSI INIT EXTERN:        %d\n", dsi_config->lcd_extern_init);
    DPRINT("================================================\n");
}

static void print_dphy_info(void)
{
    unsigned temp;

    temp = ((1000000 * 100) / (dsi_config->bit_rate / 1000)) * 8;
    DPRINT("================================================\n");
    DPRINT("MIPI DSI DPHY timing (unit: ns)\n"
        " UI:                  %d.%02d\n"
        " LP LPX:              %d\n"
        " LP TA_SURE:          %d\n"
        " LP TA_GO:            %d\n"
        " LP TA_GET:           %d\n"
        " HS EXIT:             %d\n"
        " HS TRAIL:            %d\n"
        " HS ZERO:             %d\n"
        " HS PREPARE:          %d\n"
        " CLK TRAIL:           %d\n"
        " CLK POST:            %d\n"
        " CLK ZERO:            %d\n"
        " CLK PREPARE:         %d\n"
        " CLK PRE:             %d\n"
        " INIT:                %d\n"
        " WAKEUP:              %d\n",
        (temp / 8 / 100), ((temp / 8) % 100),
        (temp * dsi_phy_config.lp_lpx / 100), (temp * dsi_phy_config.lp_ta_sure / 100), (temp * dsi_phy_config.lp_ta_go / 100), 
        (temp * dsi_phy_config.lp_ta_get / 100), (temp * dsi_phy_config.hs_exit / 100), (temp * dsi_phy_config.hs_trail / 100), 
        (temp * dsi_phy_config.hs_zero / 100), (temp * dsi_phy_config.hs_prepare / 100), (temp * dsi_phy_config.clk_trail / 100), 
        (temp * dsi_phy_config.clk_post / 100), (temp * dsi_phy_config.clk_zero / 100), (temp * dsi_phy_config.clk_prepare / 100),
        (temp * dsi_phy_config.clk_pre / 100), (temp * dsi_phy_config.init / 100), (temp * dsi_phy_config.wakeup / 100));
    DPRINT("================================================\n");
}

// -----------------------------------------------------------------------------
//                     Function: check_phy_st
// Check the status of the dphy: phylock and stopstateclklane, to decide if the DPHY is ready
// -----------------------------------------------------------------------------
static void check_phy_status(void)
{
    while((( READ_LCD_REG(MIPI_DSI_DWC_PHY_STATUS_OS ) >> BIT_PHY_LOCK) & 0x1) == 0){
        udelay(6);
    }
    while((( READ_LCD_REG(MIPI_DSI_DWC_PHY_STATUS_OS ) >> BIT_PHY_STOPSTATECLKLANE) & 0x1) == 0){
        lcd_print(" Waiting STOP STATE LANE\n");
        udelay(6);
    }
}

// -----------------------------------------------------------------------------
//                     Function: set_mipi_dcs
// Configure relative registers in command mode
// -----------------------------------------------------------------------------
static void set_mipi_dcs(int trans_type,        // 0: high speed, 1: low power
                         int req_ack,           // 1: request ack, 0: do not need ack
                         int tear_en)           // 1: enable tear ack, 0: disable tear ack
{
    WRITE_LCD_REG( MIPI_DSI_DWC_CMD_MODE_CFG_OS, (trans_type << BIT_MAX_RD_PKT_SIZE) | (trans_type << BIT_DCS_LW_TX)    |
                    (trans_type << BIT_DCS_SR_0P_TX)    | (trans_type << BIT_DCS_SW_1P_TX) |
                    (trans_type << BIT_DCS_SW_0P_TX)    | (trans_type << BIT_GEN_LW_TX)    |
                    (trans_type << BIT_GEN_SR_2P_TX)    | (trans_type << BIT_GEN_SR_1P_TX) |
                    (trans_type << BIT_GEN_SR_0P_TX)    | (trans_type << BIT_GEN_SW_2P_TX) |
                    (trans_type << BIT_GEN_SW_1P_TX)    | (trans_type << BIT_GEN_SW_0P_TX) |
                    (req_ack    << BIT_ACK_RQST_EN)     | (tear_en    << BIT_TEAR_FX_EN)  );

    if (tear_en == MIPI_DCS_ENABLE_TEAR) {
        // Enable Tear Interrupt if tear_en is valid
        WRITE_LCD_REG( MIPI_DSI_TOP_INTR_CNTL_STAT, (READ_LCD_REG(MIPI_DSI_TOP_INTR_CNTL_STAT) | (0x1<<BIT_EDPITE_INT_EN)) );
        // Enable Measure Vsync
        WRITE_LCD_REG( MIPI_DSI_TOP_MEAS_CNTL, (READ_LCD_REG(MIPI_DSI_TOP_MEAS_CNTL) | (0x1<<BIT_VSYNC_MEAS_EN | (0x1<<BIT_TE_MEAS_EN))));
    }
}
#if 0
// -----------------------------------------------------------------------------
//                     Function: set_mipi_int
// Configure relative registers for mipi interrupt
// -----------------------------------------------------------------------------
static void set_mipi_int(void)
{
    WRITE_LCD_REG( MIPI_DSI_DWC_INT_MSK0_OS, 0);
    WRITE_LCD_REG( MIPI_DSI_DWC_INT_MSK1_OS, 0);
}
#endif
// ----------------------------------------------------------------------------
// Function: wait_bta_ack
// Poll to check if the BTA ack is finished
// ----------------------------------------------------------------------------
static void wait_bta_ack(void)
{
    unsigned int phy_status;

    // Check if phydirection is RX
    do {
        phy_status = READ_LCD_REG( MIPI_DSI_DWC_PHY_STATUS_OS );
    } while(((phy_status & 0x2) >> BIT_PHY_DIRECTION) == 0x0);

    // Check if phydirection is return to TX
    do {
        phy_status = READ_LCD_REG( MIPI_DSI_DWC_PHY_STATUS_OS );
    } while(((phy_status & 0x2) >> BIT_PHY_DIRECTION) == 0x1);
}

// ----------------------------------------------------------------------------
// Function: wait_cmd_fifo_empty
// Poll to check if the generic command fifo is empty
// ----------------------------------------------------------------------------
static void wait_cmd_fifo_empty(void)
{
    unsigned int cmd_status;
    int i= CMD_TIMEOUT_CNT;

    do {
        udelay(10);
        i--;
        cmd_status = READ_LCD_REG(MIPI_DSI_DWC_CMD_PKT_STATUS_OS);
    } while((((cmd_status >> BIT_GEN_CMD_EMPTY) & 0x1) != 0x1) && (i>0));
    print_mipi_cmd_status(i, cmd_status);
}
#ifdef MIPI_DSI_COMMAND_READ
// ----------------------------------------------------------------------------
// Function: wait_for_generic_read_response
// Wait for generic read response
// ----------------------------------------------------------------------------
static unsigned int wait_for_generic_read_response(void)
{
    unsigned int timeout, phy_status, data_out;

    phy_status = READ_LCD_REG( MIPI_DSI_DWC_PHY_STATUS_OS );
    for(timeout=0; timeout<50; timeout++) {
        if(((phy_status & 0x40)>> BIT_PHY_RXULPSESC0LANE) == 0x0)
            break;
        phy_status = READ_LCD_REG( MIPI_DSI_DWC_PHY_STATUS_OS );
        udelay(1);
    }
    phy_status = READ_LCD_REG( MIPI_DSI_DWC_PHY_STATUS_OS );
    for(timeout=0; timeout<50; timeout++) {
        if(((phy_status & 0x40)>> BIT_PHY_RXULPSESC0LANE) == 0x1)
            break;
        phy_status = READ_LCD_REG( MIPI_DSI_DWC_PHY_STATUS_OS );
        udelay(1);
    }

    data_out = READ_LCD_REG( MIPI_DSI_DWC_GEN_PLD_DATA_OS );
    return data_out;
}
#endif
// ----------------------------------------------------------------------------
// Function: generic_if_wr
// Generic interface write, address has to be MIPI_DSI_DWC_GEN_PLD_DATA_OS and
// MIPI_DSI_DWC_GEN_HDR_OS, MIPI_DSI_DWC_GEN_VCID_OS
// ----------------------------------------------------------------------------
static unsigned int generic_if_wr(unsigned int address, unsigned int data_in)
{
    if(address != MIPI_DSI_DWC_GEN_HDR_OS && address != MIPI_DSI_DWC_GEN_PLD_DATA_OS) {
        lcd_print(" Error Address : 0x%x\n", address);
    }

    lcd_print("address 0x%x = 0x%08x\n", address, data_in);
    WRITE_LCD_REG(address, data_in);

    return 0;
}
#ifdef MIPI_DSI_COMMAND_READ
// ----------------------------------------------------------------------------
//                           Function: generic_if_rd
// Generic interface read, address has to be MIPI_DSI_DWC_GEN_PLD_DATA_OS
// ----------------------------------------------------------------------------
static unsigned int generic_if_rd(unsigned int address)
{
    unsigned int data_out;

    if(address != MIPI_DSI_DWC_GEN_PLD_DATA_OS) {
        lcd_print(" Error Address : %x\n", address);
    }

    data_out = READ_DSI_REG(address);

    return data_out;
}

// ----------------------------------------------------------------------------
//                           Function: generic_read_packet_0_para
// Generic Read Packet 0 Parameter with Generic Interface
// Supported DCS Command: DCS_SET_ADDRESS_MODE/DCS_SET_GAMMA_CURVE/
//                        DCS_SET_PIXEL_FORMAT/DCS_SET_TEAR_ON
// ----------------------------------------------------------------------------
static unsigned int generic_read_packet_0_para(unsigned char data_type, unsigned char vc_id, unsigned char dcs_command)
{
    unsigned int read_data;

    // lcd_print(" para is %x, dcs_command is %x\n", para, dcs_command);
    // lcd_print(" vc_id %x, data_type is %x\n", vc_id, data_type);
    generic_if_wr(MIPI_DSI_DWC_GEN_HDR_OS, ((0 << BIT_GEN_WC_MSBYTE)                           |
                                            (((unsigned int)dcs_command) << BIT_GEN_WC_LSBYTE) |
                                            (((unsigned int)vc_id) << BIT_GEN_VC)              |
                                            (((unsigned int)data_type) << BIT_GEN_DT)));

    read_data = wait_for_generic_read_response();

    return read_data;
}
#endif
// ----------------------------------------------------------------------------
//                           Function: generic_write_short_packet
// Generic Write Short Packet with Generic Interface
// Supported Data Type: DT_GEN_SHORT_WR_0, DT_GEN_SHORT_WR_1, DT_GEN_SHORT_WR_2,
// ----------------------------------------------------------------------------
static void dsi_generic_write_short_packet(unsigned char data_type, unsigned char vc_id, unsigned char* payload, unsigned short pld_count, unsigned int req_ack)
{
    unsigned int d_para[2];

    vc_id &= 0x3;
    switch (data_type) {
        case DT_GEN_SHORT_WR_0:
            d_para[0] = 0;
            d_para[1] = 0;
            break;
        case DT_GEN_SHORT_WR_1:
            d_para[0] = ((unsigned int)payload[1]) & 0xff;
            d_para[1] = 0;
            break;
        case DT_GEN_SHORT_WR_2:
        default:
            d_para[0] = ((unsigned int)payload[1]) & 0xff;
            d_para[1] = (pld_count == 0) ? 0 : (((unsigned int)payload[3]) & 0xff);
            break;
    }

    generic_if_wr(MIPI_DSI_DWC_GEN_HDR_OS, ((d_para[1] << BIT_GEN_WC_MSBYTE)      |
                                            (d_para[0] << BIT_GEN_WC_LSBYTE)      |
                                            (((unsigned int)vc_id) << BIT_GEN_VC) |
                                            (((unsigned int)data_type) << BIT_GEN_DT)));
    if( req_ack == MIPI_DSI_DCS_REQ_ACK ) {
        wait_bta_ack();
    }
    else if( req_ack == MIPI_DSI_DCS_NO_ACK ) {
        wait_cmd_fifo_empty();
    }
}

// ----------------------------------------------------------------------------
//                           Function: dcs_write_short_packet
// DCS Write Short Packet with Generic Interface
// Supported Data Type: DT_DCS_SHORT_WR_0, DT_DCS_SHORT_WR_1,
// ----------------------------------------------------------------------------
static void dsi_dcs_write_short_packet(unsigned char data_type, unsigned char vc_id, unsigned char* payload, unsigned short pld_count, unsigned int req_ack)
{
    unsigned int d_command, d_para;

    vc_id &= 0x3;
    d_command = ((unsigned int)payload[1]) & 0xff;
    d_para = (pld_count == 0) ? 0 : (((unsigned int)payload[3]) & 0xff);

    generic_if_wr(MIPI_DSI_DWC_GEN_HDR_OS, ((d_para << BIT_GEN_WC_MSBYTE)         |
                                            (d_command << BIT_GEN_WC_LSBYTE)      |
                                            (((unsigned int)vc_id) << BIT_GEN_VC) |
                                            (((unsigned int)data_type) << BIT_GEN_DT)));
    if( req_ack == MIPI_DSI_DCS_REQ_ACK ) {
        wait_bta_ack();
    }
    else if( req_ack == MIPI_DSI_DCS_NO_ACK ) {
        wait_cmd_fifo_empty();
    }
}

// ----------------------------------------------------------------------------
//                           Function: dsi_write_long_packet
// Write Long Packet with Generic Interface
// Supported Data Type: DT_GEN_LONG_WR, DT_DCS_LONG_WR
// ----------------------------------------------------------------------------
static void dsi_write_long_packet(unsigned char data_type, unsigned char vc_id, unsigned char* payload, unsigned short pld_count, unsigned int req_ack)
{
    unsigned int d_command, payload_data=0, header_data;
    unsigned int cmd_status;
    unsigned int i, d_start_index;
    int j;

    vc_id &= 0x3;
    d_command = ((unsigned int)payload[1]) & 0xff;
    pld_count = (pld_count + 1) & 0xffff;//include command
    d_start_index = 3;//payload[3] start (payload[0]: data_type, payload[1]: command, payload[2]: para_num)

    // Write Payload Register First
    payload_data = d_command;
    for(i=1; i<pld_count; i++) {
        if(i%4 == 0)
            payload_data = 0;
        payload_data |= (((unsigned int)payload[i-1+d_start_index]) << 8*(i%4));

        if((i%4 == 3) || (i == (pld_count-1))) {  // when last byte  //write max 4 byte payload data once
            // Check the pld fifo status before write to it, do not need check every word
            if((i == (pld_count/3)) || (i == (pld_count/2))) {
                j = CMD_TIMEOUT_CNT;
                do {
                    udelay(10);
                    j--;
                    cmd_status = READ_LCD_REG(MIPI_DSI_DWC_CMD_PKT_STATUS_OS);
                } while((((cmd_status >> BIT_GEN_PLD_W_FULL) & 0x1) == 0x1) && (j>0));
                print_mipi_cmd_status(j, cmd_status);
            }
            if(d_command == DCS_WRITE_MEMORY_CONTINUE) { // Use direct memory write to save time when in WRITE_MEMORY_CONTINUE
                WRITE_LCD_REG(MIPI_DSI_DWC_GEN_PLD_DATA_OS, payload_data);
            }
            else {
                generic_if_wr(MIPI_DSI_DWC_GEN_PLD_DATA_OS, payload_data);
            }
        }
    }

    // Check cmd fifo status before write to it
    j = CMD_TIMEOUT_CNT;
    do {
        udelay(10);
        j--;
        cmd_status = READ_LCD_REG(MIPI_DSI_DWC_CMD_PKT_STATUS_OS);
    } while((((cmd_status >> BIT_GEN_CMD_FULL) & 0x1) == 0x1) && (j>0));
    print_mipi_cmd_status(j, cmd_status);
    // Write Header Register
    header_data = ( (((unsigned int)pld_count) << BIT_GEN_WC_LSBYTE) |//include command
                    (((unsigned int)vc_id) << BIT_GEN_VC)                |
                    (((unsigned int)data_type) << BIT_GEN_DT));
    generic_if_wr(MIPI_DSI_DWC_GEN_HDR_OS, header_data);
    if (req_ack == MIPI_DSI_DCS_REQ_ACK) {
        wait_bta_ack();
    }
    else if (req_ack == MIPI_DSI_DCS_NO_ACK) {
        wait_cmd_fifo_empty();
    }
}

// ----------------------------------------------------------------------------
//                           Function: dsi_write_cmd
// Generic Write Command
// Supported Data Type: DT_GEN_SHORT_WR_0, DT_GEN_SHORT_WR_1, DT_GEN_SHORT_WR_2,
//                      DT_DCS_SHORT_WR_0, DT_DCS_SHORT_WR_1,
//                      DT_GEN_LONG_WR, DT_DCS_LONG_WR,
//                      DT_SET_MAX_RPS
// Return:              command number
// ----------------------------------------------------------------------------
int dsi_write_cmd(unsigned char* payload)
{
    int i=0, j=0;
    int num = 0;
    unsigned char vc_id = MIPI_DSI_VIRTUAL_CHAN_ID;
    unsigned int req_ack = MIPI_DSI_DCS_ACK_TYPE;

    //payload struct:
    //data_type, command, para_num, parameters...
    //data_type=0xff, command=0xff, means ending flag
    //data_type=0xff, command<0xff, means delay time(unit ms)
    while(i < DSI_CMD_SIZE_MAX) {
        if(payload[i]==0xff) {
            j = 2;
            if(payload[i+1]==0xff)
                break;
            else
                mdelay(payload[i+1]);
        }
        else {
            j = 3 + payload[i+2]; //payload[i+2] is parameter num
            switch (payload[i]) {//analysis data_type
                case DT_GEN_SHORT_WR_0:
                case DT_GEN_SHORT_WR_1:
                case DT_GEN_SHORT_WR_2:
                    dsi_generic_write_short_packet(payload[i], vc_id, &payload[i], payload[i+2], req_ack);
                    break;
                case DT_DCS_SHORT_WR_0:
                case DT_DCS_SHORT_WR_1:
                    dsi_dcs_write_short_packet(payload[i], vc_id, &payload[i], payload[i+2], req_ack);
                    break;
                case DT_DCS_LONG_WR:
                case DT_GEN_LONG_WR:
                    dsi_write_long_packet(payload[i], vc_id, &payload[i], payload[i+2], req_ack);
                    break;
                case DT_SET_MAX_RPS:
                    DPRINT("to do data_type: 0x%2x\n", payload[i]);
                    break;
                case DT_TURN_ON:
                    WRITE_LCD_REG_BITS(MIPI_DSI_TOP_CNTL, 1, 2, 1);
                    mdelay(20); //wait for vsync trigger
                    WRITE_LCD_REG_BITS(MIPI_DSI_TOP_CNTL, 0, 2, 1);
                    mdelay(20); //wait for vsync trigger
                    break;
                case DT_SHUT_DOWN:
                    WRITE_LCD_REG_BITS(MIPI_DSI_TOP_CNTL, 1, 2, 1);
                    mdelay(20); //wait for vsync trigger
                    break;
                default:
                    DPRINT("un-support data_type: 0x%02x\n", payload[i]);
            }
        }
        i += j;
        num++;
    }

    return num;
}

static void set_dsi_phy_config(DSI_Phy_t *dphy, unsigned dsi_ui)
{
    unsigned t_lane_byte, t_ui;

    t_ui = (1000000 * 100) / (dsi_ui / 1000); //0.01ns*100
    t_lane_byte = t_ui * 8;

    dphy->lp_tesc = ((DPHY_TIME_LP_TESC(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->lp_lpx = ((DPHY_TIME_LP_LPX(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->lp_ta_sure = ((DPHY_TIME_LP_TA_SURE(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->lp_ta_go = ((DPHY_TIME_LP_TA_GO(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->lp_ta_get = ((DPHY_TIME_LP_TA_GETX(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->hs_exit = ((DPHY_TIME_HS_EXIT(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->hs_trail = ((DPHY_TIME_HS_TRAIL(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->hs_prepare = ((DPHY_TIME_HS_PREPARE(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->hs_zero = ((DPHY_TIME_HS_ZERO(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->clk_trail = ((DPHY_TIME_CLK_TRAIL(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->clk_post = ((DPHY_TIME_CLK_POST(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->clk_prepare = ((DPHY_TIME_CLK_PREPARE(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->clk_zero = ((DPHY_TIME_CLK_ZERO(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->clk_pre = ((DPHY_TIME_CLK_PRE(t_ui) + t_lane_byte - 1) / t_lane_byte) & 0xff;
    dphy->init = (DPHY_TIME_INIT(t_ui) + t_lane_byte - 1) / t_lane_byte;
    dphy->wakeup = (DPHY_TIME_WAKEUP(t_ui) + t_lane_byte - 1) / t_lane_byte;

    lcd_print("lp_tesc = 0x%02x\n"
            "lp_lpx = 0x%02x\n"
            "lp_ta_sure = 0x%02x\n"
            "lp_ta_go = 0x%02x\n"
            "lp_ta_get = 0x%02x\n"
            "hs_exit = 0x%02x\n"
            "hs_trail = 0x%02x\n"
            "hs_zero = 0x%02x\n"
            "hs_prepare = 0x%02x\n"
            "clk_trail = 0x%02x\n"
            "clk_post = 0x%02x\n"
            "clk_zero = 0x%02x\n"
            "clk_prepare = 0x%02x\n"
            "clk_pre = 0x%02x\n"
            "init = 0x%02x\n"
            "wakeup = 0x%02x\n",
            dphy->lp_tesc, dphy->lp_lpx, dphy->lp_ta_sure, dphy->lp_ta_go, dphy->lp_ta_get,
            dphy->hs_exit, dphy->hs_trail, dphy->hs_zero, dphy->hs_prepare,
            dphy->clk_trail, dphy->clk_post, dphy->clk_zero, dphy->clk_prepare, dphy->clk_pre,
            dphy->init, dphy->wakeup);
}

static void dsi_phy_init(DSI_Phy_t *dphy, unsigned char lane_num)
{
    // enable phy clock.
    WRITE_DSI_REG(MIPI_DSI_PHY_CTRL,  0x1);          //enable DSI top clock.
    WRITE_DSI_REG(MIPI_DSI_PHY_CTRL,  0x1 |          //enable the DSI PLL clock .
                    (1 << 7 )  |   //enable pll clock which connected to  DDR clock path
                    (1 << 8 )  |   //enable the clock divider counter
                    (0 << 9 )  |   //enable the divider clock out
                    (0 << 10 ) |   //clock divider. 1: freq/4, 0: freq/2
                    (0 << 11 ) |   //1: select the mipi DDRCLKHS from clock divider, 0: from PLL clock
                    (0 << 12));    //enable the byte clock generateion.
    WRITE_DSI_REG_BITS(MIPI_DSI_PHY_CTRL,  1, 9, 1); //enable the divider clock out
    WRITE_DSI_REG_BITS(MIPI_DSI_PHY_CTRL,  1, 12, 1); //enable the byte clock generateion.
    WRITE_DSI_REG_BITS(MIPI_DSI_PHY_CTRL,  1, 31, 1);
    WRITE_DSI_REG_BITS(MIPI_DSI_PHY_CTRL,  0, 31, 1);

    WRITE_DSI_REG(MIPI_DSI_CLK_TIM,  (dphy->clk_trail | (dphy->clk_post << 8) | (dphy->clk_zero << 16) | (dphy->clk_prepare << 24)));//0x05210f08);//0x03211c08
    WRITE_DSI_REG(MIPI_DSI_CLK_TIM1, dphy->clk_pre);//??
    WRITE_DSI_REG(MIPI_DSI_HS_TIM, (dphy->hs_exit | (dphy->hs_trail << 8) | (dphy->hs_zero << 16) | (dphy->hs_prepare << 24)));//0x050f090d
    WRITE_DSI_REG(MIPI_DSI_LP_TIM, (dphy->lp_lpx | (dphy->lp_ta_sure << 8) | (dphy->lp_ta_go << 16) | (dphy->lp_ta_get << 24)));//0x4a370e0e
    WRITE_DSI_REG(MIPI_DSI_ANA_UP_TIM, 0x0100); //?? //some number to reduce sim time.
    WRITE_DSI_REG(MIPI_DSI_INIT_TIM, dphy->init); //0xe20   //30d4 -> d4 to reduce sim time.
    WRITE_DSI_REG(MIPI_DSI_WAKEUP_TIM, dphy->wakeup); //0x8d40  //1E848-> 48 to reduct sim time.
    WRITE_DSI_REG(MIPI_DSI_LPOK_TIM,  0x7C);   //wait for the LP analog ready.
    WRITE_DSI_REG(MIPI_DSI_ULPS_CHECK,  0x927C);   //1/3 of the tWAKEUP.
    WRITE_DSI_REG(MIPI_DSI_LP_WCHDOG,  0x1000);   // phy TURN watch dog.
    WRITE_DSI_REG(MIPI_DSI_TURN_WCHDOG,  0x1000);   // phy ESC command watch dog.

    // Powerup the analog circuit.
    switch (lane_num) {
        case 1:
            WRITE_DSI_REG(MIPI_DSI_CHAN_CTRL, 0x0e);
            break;
        case 2:
            WRITE_DSI_REG(MIPI_DSI_CHAN_CTRL, 0x0c);
            break;
        case 3:
            WRITE_DSI_REG(MIPI_DSI_CHAN_CTRL, 0x08);
            break;
        case 4:
        default:
            WRITE_DSI_REG(MIPI_DSI_CHAN_CTRL, 0);
            break;
    }
}

static void mipi_dsi_phy_config(Lcd_Config_t *pConf)
{
    lcd_print("%s\n", __func__);
    //Digital
    // Power up DSI
    WRITE_LCD_REG(MIPI_DSI_DWC_PWR_UP_OS, 1);

    // Setup Parameters of DPHY
    WRITE_LCD_REG(MIPI_DSI_DWC_PHY_TST_CTRL1_OS, 0x00010044);                            // testcode
    WRITE_LCD_REG(MIPI_DSI_DWC_PHY_TST_CTRL0_OS, 0x2);
    WRITE_LCD_REG(MIPI_DSI_DWC_PHY_TST_CTRL0_OS, 0x0);
    WRITE_LCD_REG(MIPI_DSI_DWC_PHY_TST_CTRL1_OS, 0x00000074);                            // testwrite
    WRITE_LCD_REG(MIPI_DSI_DWC_PHY_TST_CTRL0_OS, 0x2);
    WRITE_LCD_REG(MIPI_DSI_DWC_PHY_TST_CTRL0_OS, 0x0);

    // Power up D-PHY
    WRITE_LCD_REG(MIPI_DSI_DWC_PHY_RSTZ_OS, 0xf);

    //Analog
    dsi_phy_init(&dsi_phy_config, pConf->lcd_control.mipi_config->lane_num);

    // Check the phylock/stopstateclklane to decide if the DPHY is ready
    check_phy_status();

    // Trigger a sync active for esc_clk
    WRITE_DSI_REG(MIPI_DSI_PHY_CTRL, READ_DSI_REG(MIPI_DSI_PHY_CTRL) | (1 << 1));
}

static void dsi_video_config(Lcd_Config_t *pConf)
{
    DSI_Config_t *cfg= pConf->lcd_control.mipi_config;

    dsi_vid_config.hline =(pConf->lcd_basic.h_period * cfg->factor_denominator + cfg->factor_numerator - 1) / cfg->factor_numerator;  // Rounded. Applicable for Period(pixclk)/Period(bytelaneclk)=9/16
    dsi_vid_config.hsa =(pConf->lcd_timing.hsync_width * cfg->factor_denominator + cfg->factor_numerator - 1) / cfg->factor_numerator;
    dsi_vid_config.hbp =((pConf->lcd_timing.hsync_bp-pConf->lcd_timing.hsync_width) * cfg->factor_denominator + cfg->factor_numerator - 1) / cfg->factor_numerator;

    dsi_vid_config.vsa = pConf->lcd_timing.vsync_width;
    dsi_vid_config.vbp = pConf->lcd_timing.vsync_bp - pConf->lcd_timing.vsync_width;
    dsi_vid_config.vfp = pConf->lcd_basic.v_period - pConf->lcd_timing.vsync_bp - pConf->lcd_basic.v_active;
    dsi_vid_config.vact = pConf->lcd_basic.v_active;

    lcd_print(" ============= VIDEO TIMING SETTING =============\n");
    lcd_print(" HLINE        = %d\n", dsi_vid_config.hline);
    lcd_print(" HSA          = %d\n", dsi_vid_config.hsa);
    lcd_print(" HBP          = %d\n", dsi_vid_config.hbp);
    lcd_print(" VSA          = %d\n", dsi_vid_config.vsa);
    lcd_print(" VBP          = %d\n", dsi_vid_config.vbp);
    lcd_print(" VFP          = %d\n", dsi_vid_config.vfp);
    lcd_print(" VACT         = %d\n", dsi_vid_config.vact);
    lcd_print(" ================================================\n");
}

#define DSI_PACKET_HEADER_CRC      6 //4(header)+2(CRC)
static void dsi_non_burst_chunk_config(Lcd_Config_t *pConf)
{
    int pixel_per_chunk=0, num_of_chunk=0, vid_null_size=0;
    int byte_per_chunk=0, total_bytes_per_chunk=0, chunk_overhead=0;
    int bit_rate_pclk_factor;
    int lane_num;
    int i, done;

    i = 1;
    done = 0;
    lane_num = (int)(pConf->lcd_control.mipi_config->lane_num);
    bit_rate_pclk_factor = pConf->lcd_control.mipi_config->bit_rate / pConf->lcd_timing.lcd_clk;
    while ((i<=(pConf->lcd_basic.h_active/8)) && (done == 0)) {
        pixel_per_chunk = i * 8;
        if (pConf->lcd_control.mipi_config->dpi_data_format == COLOR_18BIT_CFG_1)
            byte_per_chunk = pixel_per_chunk * 9/4; //18bit (4*18/8=9byte)
        else
            byte_per_chunk = pixel_per_chunk * 3; //24bit or 18bit-loosely
        total_bytes_per_chunk = (lane_num * pixel_per_chunk * bit_rate_pclk_factor) / 8;
        num_of_chunk = pConf->lcd_basic.h_active / pixel_per_chunk;
        chunk_overhead = total_bytes_per_chunk - (byte_per_chunk + DSI_PACKET_HEADER_CRC); // byte_per_chunk+6=valid_payload
        if (chunk_overhead >= DSI_PACKET_HEADER_CRC) { // if room for null_vid's head(4)+crc(2)
            vid_null_size = chunk_overhead - DSI_PACKET_HEADER_CRC; // chunk_overhead-null_vid's head(4)+crc(2) = null_vid's payload
            done = 1;
        }
        else if (chunk_overhead >= 0) {
            vid_null_size = 0;
            done = 1;
        }
        else
            vid_null_size = 0;
        i++;
    }
    if (done == 0) {
        DPRINT(" No room packet header & CRC, chunk_overhead is %d\n", chunk_overhead);
    }

    dsi_vid_config.pixel_per_chunk = pixel_per_chunk;
    dsi_vid_config.num_of_chunk = num_of_chunk;
    dsi_vid_config.vid_null_size = vid_null_size;
    lcd_print(" ============== NON_BURST SETTINGS =============\n");
    lcd_print(" pixel_per_chunk       = %d\n", pixel_per_chunk);
    lcd_print(" num_of_chunk          = %d\n", num_of_chunk);
    lcd_print(" total_bytes_per_chunk = %d\n", total_bytes_per_chunk);
    lcd_print(" byte_per_chunk        = %d\n", byte_per_chunk);
    lcd_print(" chunk_overhead        = %d\n", chunk_overhead);
    lcd_print(" vid_null_size         = %d\n", vid_null_size);
    lcd_print(" ===============================================\n");
}

// ----------------------------------------------------------------------------
//                           Function: set_mipi_dsi_host
// Parameters:
//              vcid,                    // virtual id
//              chroma_subsample,        // chroma_subsample for YUV422 or YUV420 only
//              operation_mode,          // video mode/command mode
//              p,                       //lcd config
// ----------------------------------------------------------------------------
static void set_mipi_dsi_host(unsigned int vcid, unsigned int chroma_subsample, unsigned int operation_mode, Lcd_Config_t *p)
{
    unsigned int dpi_data_format, venc_data_width;
    unsigned int lane_num, vid_mode_type;
    tv_enc_lcd_type_t  output_type;

    venc_data_width = p->lcd_control.mipi_config->venc_data_width;
    dpi_data_format = p->lcd_control.mipi_config->dpi_data_format;
    lane_num        = (unsigned int)(p->lcd_control.mipi_config->lane_num);
    vid_mode_type   = (unsigned int)(p->lcd_control.mipi_config->video_mode_type);
    output_type     = p->lcd_control.mipi_config->venc_fmt;

    // -----------------------------------------------------
    // Standard Configuration for Video Mode Operation
    // -----------------------------------------------------
    // 1,    Configure Lane number and phy stop wait time
    if ((output_type != TV_ENC_LCD240x160_dsi) && (output_type != TV_ENC_LCD1920x1200p) &&
        (output_type != TV_ENC_LCD2560x1600) && (output_type != TV_ENC_LCD768x1024p)) {
        WRITE_LCD_REG( MIPI_DSI_DWC_PHY_IF_CFG_OS, (0x28 << BIT_PHY_STOP_WAIT_TIME) | ((lane_num-1) << BIT_N_LANES));
    } else {
        WRITE_LCD_REG( MIPI_DSI_DWC_PHY_IF_CFG_OS, (1 << BIT_PHY_STOP_WAIT_TIME) | ((lane_num-1) << BIT_N_LANES));
    }

    // 2.1,  Configure Virtual channel settings
    WRITE_LCD_REG( MIPI_DSI_DWC_DPI_VCID_OS, vcid );
    // 2.2,  Configure Color format
    WRITE_LCD_REG( MIPI_DSI_DWC_DPI_COLOR_CODING_OS, (((dpi_data_format == COLOR_18BIT_CFG_2) ? 1 : 0) << BIT_LOOSELY18_EN) | (dpi_data_format << BIT_DPI_COLOR_CODING) );
    // 2.2.1 Configure Set color format for DPI register
    WRITE_LCD_REG( MIPI_DSI_TOP_CNTL, ((READ_LCD_REG(MIPI_DSI_TOP_CNTL) & ~(0xf<<BIT_DPI_COLOR_MODE) & ~(0x7<<BIT_IN_COLOR_MODE) & ~(0x3<<BIT_CHROMA_SUBSAMPLE)) |
                                (dpi_data_format    << BIT_DPI_COLOR_MODE)  |
                                (venc_data_width    << BIT_IN_COLOR_MODE)   |
                                (chroma_subsample   << BIT_CHROMA_SUBSAMPLE)) );
    // 2.3   Configure Signal polarity
    WRITE_LCD_REG( MIPI_DSI_DWC_DPI_CFG_POL_OS, (0x0 << BIT_COLORM_ACTIVE_LOW) |
                        (0x0 << BIT_SHUTD_ACTIVE_LOW)  |
                        (0 << BIT_HSYNC_ACTIVE_LOW)  |//(((p->lcd_timing.pol_ctrl >> POL_CTRL_HS) & 1) << BIT_HSYNC_ACTIVE_LOW)  |
                        (0 << BIT_VSYNC_ACTIVE_LOW)  |//(((p->lcd_timing.pol_ctrl >> POL_CTRL_VS) & 1) << BIT_VSYNC_ACTIVE_LOW)  |
                        (0x0 << BIT_DATAEN_ACTIVE_LOW));

    if (operation_mode == OPERATION_VIDEO_MODE) {
        // 3.1   Configure Low power and video mode type settings
        WRITE_LCD_REG( MIPI_DSI_DWC_VID_MODE_CFG_OS, (1 << BIT_LP_HFP_EN)  |                  // enalbe lp
                        (1 << BIT_LP_HBP_EN)  |                  // enalbe lp
                        (1 << BIT_LP_VCAT_EN) |                  // enalbe lp
                        (1 << BIT_LP_VFP_EN)  |                  // enalbe lp
                        (1 << BIT_LP_VBP_EN)  |                  // enalbe lp
                        (1 << BIT_LP_VSA_EN)  |                  // enalbe lp
                        (1 << BIT_FRAME_BTA_ACK_EN) |            // enable BTA after one frame, TODO, need check
                        //(1 << BIT_LP_CMD_EN) |                   // enable the command transmission only in lp mode
                        (vid_mode_type << BIT_VID_MODE_TYPE) );  // burst/non burst
        WRITE_LCD_REG( MIPI_DSI_DWC_DPI_LP_CMD_TIM_OS, (4 << 16) | (4 << 0));  //[23:16]outvact, [7:0]invact

        // 3.2   Configure video packet size settings
        if( vid_mode_type == BURST_MODE ) {                                        // burst mode
            WRITE_LCD_REG( MIPI_DSI_DWC_VID_PKT_SIZE_OS, p->lcd_basic.h_active);                   // should be one line in pixels, such as 480/240...
        }
        else {  // non-burst mode
            WRITE_LCD_REG( MIPI_DSI_DWC_VID_PKT_SIZE_OS, dsi_vid_config.pixel_per_chunk);        // in unit of pixels, (pclk period/byte clk period)*num_of_lane should be integer
        }

        // 3.3   Configure number of chunks and null packet size for one line
        if( vid_mode_type == BURST_MODE ) {                                // burst mode
            WRITE_LCD_REG( MIPI_DSI_DWC_VID_NUM_CHUNKS_OS, 0);
            WRITE_LCD_REG( MIPI_DSI_DWC_VID_NULL_SIZE_OS, 0);
        }
        else {                                                             // non burst mode
            WRITE_LCD_REG( MIPI_DSI_DWC_VID_NUM_CHUNKS_OS, dsi_vid_config.num_of_chunk);  // HACT/VID_PKT_SIZE
            WRITE_LCD_REG( MIPI_DSI_DWC_VID_NULL_SIZE_OS, dsi_vid_config.vid_null_size);  // video null size
        }

        // 4     Configure the video relative parameters according to the output type
        //         include horizontal timing and vertical line
        WRITE_LCD_REG( MIPI_DSI_DWC_VID_HLINE_TIME_OS,    dsi_vid_config.hline);
        WRITE_LCD_REG( MIPI_DSI_DWC_VID_HSA_TIME_OS,      dsi_vid_config.hsa);
        WRITE_LCD_REG( MIPI_DSI_DWC_VID_HBP_TIME_OS,      dsi_vid_config.hbp);
        WRITE_LCD_REG( MIPI_DSI_DWC_VID_VSA_LINES_OS,     dsi_vid_config.vsa);
        WRITE_LCD_REG( MIPI_DSI_DWC_VID_VBP_LINES_OS,     dsi_vid_config.vbp);
        WRITE_LCD_REG( MIPI_DSI_DWC_VID_VFP_LINES_OS,     dsi_vid_config.vfp);
        WRITE_LCD_REG( MIPI_DSI_DWC_VID_VACTIVE_LINES_OS, dsi_vid_config.vact);
    }  /* operation_mode == OPERATION_VIDEO_MODE */

    // -----------------------------------------------------
    // Finish Configuration
    // -----------------------------------------------------

    // Inner clock divider settings
    WRITE_LCD_REG( MIPI_DSI_DWC_CLKMGR_CFG_OS, (0x1 << BIT_TO_CLK_DIV) | (dsi_phy_config.lp_tesc << BIT_TX_ESC_CLK_DIV) );
    // Packet header settings
    WRITE_LCD_REG( MIPI_DSI_DWC_PCKHDL_CFG_OS, (1 << BIT_CRC_RX_EN) |
                        (1 << BIT_ECC_RX_EN) |
                        (0 << BIT_BTA_EN) |
                        (0 << BIT_EOTP_RX_EN) |
                        (0 << BIT_EOTP_TX_EN) );
    // operation mode setting: video/command mode
    WRITE_LCD_REG( MIPI_DSI_DWC_MODE_CFG_OS, operation_mode );

    // Phy Timer
    if ((output_type != TV_ENC_LCD240x160_dsi) &&
                    (output_type != TV_ENC_LCD1920x1200p) &&
                    (output_type != TV_ENC_LCD2560x1600) &&
                    (output_type != TV_ENC_LCD768x1024p)) {
        WRITE_LCD_REG( MIPI_DSI_DWC_PHY_TMR_CFG_OS, 0x03320000);
    } else {
        WRITE_LCD_REG( MIPI_DSI_DWC_PHY_TMR_CFG_OS, 0x090f0000);
    }

    // Configure DPHY Parameters
    if ((output_type != TV_ENC_LCD240x160_dsi) &&
                    (output_type != TV_ENC_LCD1920x1200p) &&
                    (output_type != TV_ENC_LCD2560x1600) &&
                    (output_type != TV_ENC_LCD768x1024p)) {
        WRITE_LCD_REG( MIPI_DSI_DWC_PHY_TMR_LPCLK_CFG_OS, 0x870025);
    } else {
        WRITE_LCD_REG( MIPI_DSI_DWC_PHY_TMR_LPCLK_CFG_OS, 0x260017);
    }
}

static void startup_transfer_cmd(void)
{
    // Startup transfer
    WRITE_LCD_REG( MIPI_DSI_DWC_LPCLK_CTRL_OS, (0x1 << BIT_AUTOCLKLANE_CTRL) | (0x1 << BIT_TXREQUESTCLKHS));
}
static void startup_transfer_video(void)
{
    WRITE_LCD_REG( MIPI_DSI_DWC_LPCLK_CTRL_OS, (0x1 << BIT_TXREQUESTCLKHS));
}

static void mipi_dsi_host_config(Lcd_Config_t *pConf)
{

    unsigned int       operation_mode_init;
    operation_mode_init  = ((pConf->lcd_control.mipi_config->operation_mode >> BIT_OPERATION_MODE_INIT) & 1);

    if (lcd_print_flag > 0) {
        print_info();
        print_dphy_info();
    }

    lcd_print("Set mipi_dsi_host\n");
    set_mipi_dcs(MIPI_DSI_CMD_TRANS_TYPE,              // 0: high speed, 1: low power
                 MIPI_DSI_DCS_ACK_TYPE,                // if need bta ack check
                 MIPI_DSI_TEAR_SWITCH);                // enable tear ack

    set_mipi_dsi_host(MIPI_DSI_VIRTUAL_CHAN_ID,        // Virtual channel id
                      0,                               // Chroma sub sample, only for YUV 422 or 420, even or odd
                      operation_mode_init,             // DSI operation mode, video or command
                      pConf);
}

void mipi_dsi_link_on(Lcd_Config_t *pConf)
{
    unsigned int      operation_mode_disp, operation_mode_init;
    struct aml_lcd_extern_driver_t *lcd_extern_driver;
    unsigned int init_flag = 0;

    DPRINT("%s\n", __FUNCTION__);
    operation_mode_disp = ((pConf->lcd_control.mipi_config->operation_mode >> BIT_OPERATION_MODE_DISP) & 1);
    operation_mode_init = ((pConf->lcd_control.mipi_config->operation_mode >> BIT_OPERATION_MODE_INIT) & 1);

    if (pConf->lcd_control.mipi_config->lcd_extern_init > 0) {
        lcd_extern_driver = aml_lcd_extern_get_driver();
        if (lcd_extern_driver == NULL) {
            DPRINT("no lcd_extern driver\n");
        }
        else {
            if (lcd_extern_driver->init_on_cmd_8) {
                init_flag += dsi_write_cmd(lcd_extern_driver->init_on_cmd_8);
                DPRINT("[extern]%s dsi init on\n", lcd_extern_driver->name);
            }
        }
    }

    if (pConf->lcd_control.mipi_config->dsi_init_on) {
        init_flag += dsi_write_cmd(pConf->lcd_control.mipi_config->dsi_init_on);
        lcd_print("dsi init on\n");
    }

    if (init_flag == 0) {
        DPRINT("[warning]: not init for mipi-dsi, use default command\n");
        dsi_write_cmd(dsi_init_on_table_dft);
    }

    if (operation_mode_disp != operation_mode_init) {
        set_mipi_dsi_host(MIPI_DSI_VIRTUAL_CHAN_ID,        // Virtual channel id
                          0,                               // Chroma sub sample, only for YUV 422 or 420, even or odd
                          operation_mode_disp,             // DSI operation mode, video or command
                          pConf);
    }
}

void mipi_dsi_link_off(Lcd_Config_t *pConf)
{
    struct aml_lcd_extern_driver_t *lcd_extern_driver;

    if (pConf->lcd_control.mipi_config->dsi_init_off) {
        dsi_write_cmd(pConf->lcd_control.mipi_config->dsi_init_off);
        lcd_print("dsi init off\n");
    }

    if (pConf->lcd_control.mipi_config->lcd_extern_init > 0) {
        lcd_extern_driver = aml_lcd_extern_get_driver();
        if (lcd_extern_driver == NULL) {
            DPRINT("no lcd_extern driver\n");
        }
        else {
            if (lcd_extern_driver->init_off_cmd_8) {
                dsi_write_cmd(lcd_extern_driver->init_off_cmd_8);
                DPRINT("[extern]%s dsi init off\n", lcd_extern_driver->name);
            }
        }
    }
}

void set_mipi_dsi_control_config(Lcd_Config_t *pConf)
{
    unsigned int bit_rate, lcd_bits;
    unsigned int operation_mode;
    DSI_Config_t *cfg= pConf->lcd_control.mipi_config;
    int n;

    operation_mode = ((cfg->operation_mode >> BIT_OPERATION_MODE_DISP) & 1);
    cfg->video_mode_type = MIPI_DSI_VIDEO_MODE_TYPE;
    if(pConf->lcd_basic.lcd_bits == 6){
        cfg->venc_data_width = MIPI_DSI_VENC_COLOR_18B;
        cfg->dpi_data_format = MIPI_DSI_COLOR_18BIT;
        if (cfg->dpi_data_format == COLOR_18BIT_CFG_2)
            lcd_bits = 8;
        else
            lcd_bits = 6;
    }else{
        cfg->venc_data_width = MIPI_DSI_VENC_COLOR_24B;
        cfg->dpi_data_format  = MIPI_DSI_COLOR_24BIT;
        lcd_bits = 8;
    }
    if (cfg->bit_rate_max == 0) { //auto calculate
        if ((operation_mode == OPERATION_VIDEO_MODE) && (cfg->video_mode_type != BURST_MODE))
            bit_rate = ((pConf->lcd_timing.lcd_clk / 1000) * 4 * lcd_bits) / cfg->lane_num;
        else
            bit_rate = ((pConf->lcd_timing.lcd_clk / 1000) * 3 * lcd_bits) / cfg->lane_num;
        cfg->bit_rate_min = 0;
        n = 0;
        while ((cfg->bit_rate_min < (PLL_VCO_MIN / od_table[OD_SEL_MAX-1])) && (n < 50)) {
            cfg->bit_rate_max = bit_rate + 20000 + (n * (pConf->lcd_timing.lcd_clk / 1000));
            cfg->bit_rate_min = cfg->bit_rate_max - (pConf->lcd_timing.lcd_clk / 1000);
            n++;
        }
        if (cfg->bit_rate_max > MIPI_PHY_MAX_CLK_IN) {
            cfg->bit_rate_max = MIPI_PHY_MAX_CLK_IN;
            cfg->bit_rate_min = cfg->bit_rate_max - (pConf->lcd_timing.lcd_clk / 1000);
        }
        DPRINT("mipi dsi bit_rate max=%dMHz\n", (cfg->bit_rate_max / 1000));
    }
    else { //user define
        cfg->bit_rate_min = cfg->bit_rate_max - (pConf->lcd_timing.lcd_clk / 1000);
        if (cfg->bit_rate_max < (PLL_VCO_MIN / od_table[OD_SEL_MAX-1])) {
            DPRINT("[error]: mipi-dsi can't support %dMHz bit_rate (min bit_rate=%dMHz)\n", (cfg->bit_rate_max / 1000), ((PLL_VCO_MIN / od_table[OD_SEL_MAX-1]) / 1000));
        }
        if (cfg->bit_rate_max > MIPI_PHY_MAX_CLK_IN) {
            DPRINT("[warning]: mipi-dsi bit_rate_max %dMHz is out of spec (%dMHz)\n", (cfg->bit_rate_max / 1000), (MIPI_PHY_MAX_CLK_IN / 1000));
        }
    }

    switch ((cfg->transfer_ctrl >> BIT_TRANS_CTRL_SWITCH) & 3) {//Venc resolution format
        case 1: //standard
            cfg->venc_fmt=TV_ENC_LCD768x1024p;
            break;
        case 2: //slow
            cfg->venc_fmt=TV_ENC_LCD1280x720;
            break;
        case 0: //auto
        default:
            if((pConf->lcd_basic.h_active !=240)&&(pConf->lcd_basic.h_active !=768)&&(pConf->lcd_basic.h_active !=1920)&&(pConf->lcd_basic.h_active !=2560))
                cfg->venc_fmt=TV_ENC_LCD1280x720;
            else
                cfg->venc_fmt=TV_ENC_LCD768x1024p;
            break;
    }
}

void set_mipi_dsi_control_config_post(Lcd_Config_t *pConf)
{
    unsigned int pclk, lanebyteclk;
    unsigned int operation_mode;
    DSI_Config_t *cfg= pConf->lcd_control.mipi_config;

    pclk = pConf->lcd_timing.lcd_clk;

    if (cfg->factor_numerator == 0) {
        lanebyteclk = cfg->bit_rate / 8;
        lcd_print("pixel_clk = %d.%03dMHz, bit_rate = %d.%03dMHz, lanebyteclk = %d.%03dMHz\n", (pclk / 1000000), ((pclk / 1000) % 1000), 
                 (cfg->bit_rate / 1000000), ((cfg->bit_rate / 1000) % 1000), (lanebyteclk / 1000000), ((lanebyteclk / 1000) % 1000));

        cfg->factor_denominator = lanebyteclk/1000;
        cfg->factor_numerator = pclk/1000;
        //cfg->factor_denominator = 10;
    }
    lcd_print("d=%d, n=%d, factor=%d.%02d\n", cfg->factor_denominator, cfg->factor_numerator, (cfg->factor_denominator/cfg->factor_numerator), 
             ((cfg->factor_denominator % cfg->factor_numerator) * 100 / cfg->factor_numerator));

    operation_mode = ((cfg->operation_mode >> BIT_OPERATION_MODE_DISP) & 1);
    if (operation_mode == OPERATION_VIDEO_MODE) {
        dsi_video_config(pConf);
        if (cfg->video_mode_type != BURST_MODE)
            dsi_non_burst_chunk_config(pConf);
    }

    set_dsi_phy_config(&dsi_phy_config, cfg->bit_rate);
}

void set_mipi_dsi_control(Lcd_Config_t *pConf)
{
    mipi_dsi_host_config(pConf);

    mipi_dsi_phy_config(pConf);

    if(((pConf->lcd_control.mipi_config->transfer_ctrl >> BIT_TRANS_CTRL_CLK) & 1) == 0)
        startup_transfer_video();
    else
        startup_transfer_cmd();

    mipi_dsi_link_on(pConf);
}

void mipi_dsi_off(void)
{
    lcd_print("poweroff dsi digital\n");
    // Power down DSI
    WRITE_LCD_REG(MIPI_DSI_DWC_PWR_UP_OS, 0);

    // Power down D-PHY, do not have to close dphy
    // WRITE_LCD_REG(MIPI_DSI_DWC_PHY_RSTZ_OS, (READ_LCD_REG( MIPI_DSI_DWC_PHY_RSTZ_OS ) & 0xc));
    // WRITE_LCD_REG(MIPI_DSI_DWC_PHY_RSTZ_OS, 0xc);

    WRITE_DSI_REG(MIPI_DSI_CHAN_CTRL, 0x1f);
    lcd_print("MIPI_DSI_PHY_CTRL=0x%x\n", READ_DSI_REG(MIPI_DSI_PHY_CTRL)); //read
    WRITE_DSI_REG_BITS(MIPI_DSI_PHY_CTRL, 0, 7, 1);
}

//***********************************************//
static const char * dsi_usage_str =
{"Usage:\n"
"    echo read <addr> <reg_count> > dsi ; read dsi phy reg value\n"
"    echo write <addr> <value> > dsi ; write dsi phy reg with value\n"
"    echo info > dsi ; print dsi config information\n"
"    echo dphy > dsi ; print dsi phy timing information\n"
};

static ssize_t dsi_debug_help(struct class *class, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n", dsi_usage_str);
}

static ssize_t dsi_debug(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    unsigned int ret;
    unsigned t[3];
    unsigned num = 0;
    int i;

    switch (buf[0]) {
        case 'r': //read
            num = 1;
            t[0] = 0;
            ret = sscanf(buf, "read %x %u", &t[0], &num);
            DPRINT("read dsi phy reg:\n");
            for (i=0; i<num; i++) {
                DPRINT("  0x%04x = 0x%08x\n", (t[0]+i), READ_DSI_REG((t[0]+i)));
            }
            break;
        case 'w': //write
            t[0] = 0;
            t[1] = 0;
            ret = sscanf(buf, "write %x %x", &t[0], &t[1]);
            WRITE_DSI_REG(t[0], t[1]);
            DPRINT("write dsi phy reg 0x%04x = 0x%08x, readback 0x%08x\n", t[0], t[1], READ_DSI_REG(t[0]));
            break;
        case 'i':
            print_info();
            break;
        case 'd':
            print_dphy_info();
            break;
        default:
            DPRINT("wrong format of dsi debug command.\n");
            break;
    }

    if (ret != 1 || ret !=2)
        return -EINVAL;
    
    return count;
    //return 0;
}

static struct class_attribute dsi_debug_class_attrs[] = {
    __ATTR(dsi, S_IRUGO | S_IWUSR, dsi_debug_help, dsi_debug),
};

static int creat_dsi_attr(Lcd_Config_t *pConf)
{
    int i;

    //create class attr
    for(i=0;i<ARRAY_SIZE(dsi_debug_class_attrs);i++) {
        if (class_create_file(pConf->lcd_misc_ctrl.debug_class, &dsi_debug_class_attrs[i])) {
            printk("create dsi debug attribute %s fail\n",dsi_debug_class_attrs[i].attr.name);
        }
    }

    return 0;
}
static int remove_dsi_attr(Lcd_Config_t *pConf)
{
    int i;

    if (pConf->lcd_misc_ctrl.debug_class == NULL)
        return -1;

    for(i=0;i<ARRAY_SIZE(dsi_debug_class_attrs);i++) {
        class_remove_file(pConf->lcd_misc_ctrl.debug_class, &dsi_debug_class_attrs[i]);
    }
    class_destroy(pConf->lcd_misc_ctrl.debug_class);

    return 0;
}
//*********************************************************//

void dsi_probe(Lcd_Config_t *pConf)
{
    dsi_config = pConf->lcd_control.mipi_config;

    //pConf->lcd_control.mipi_config->bit_rate_min *= 1000;
    pConf->lcd_control.mipi_config->bit_rate_max *= 1000;

    creat_dsi_attr(pConf);
}

void dsi_remove(Lcd_Config_t *pConf)
{
    remove_dsi_attr(pConf);
}

