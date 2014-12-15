#ifndef EDP_DRV_H
#define EDP_DRV_H
#include <linux/amlogic/vout/lcdoutc.h>

//********************************************************//
// displayport host (tx) control
//********************************************************//
//Tx core ID
#define VAL_EDP_TX_CORE_ID								0x0a

//Link Configuration Field
#define VAL_EDP_TX_LINK_BW_SET_162						0x06
#define VAL_EDP_TX_LINK_BW_SET_270						0x0a
#define VAL_EDP_TX_LINK_BW_SET_540						0x14

//Lane setting
#define VAL_EDP_TX_PHY_VSWING_0							0x03   // 0.4
#define VAL_EDP_TX_PHY_VSWING_1							0x06   // 0.6
#define VAL_EDP_TX_PHY_VSWING_2							0x09   // 0.8
#define VAL_EDP_TX_PHY_VSWING_3							0x0F   // 1.2
#define VAL_EDP_TX_PHY_PREEMPHASIS_0					0x00   // 0 db
#define VAL_EDP_TX_PHY_PREEMPHASIS_1					0x05   // 3.5 db
#define VAL_EDP_TX_PHY_PREEMPHASIS_2					0x0a   // 6 db
#define VAL_EDP_TX_PHY_PREEMPHASIS_3					0x0f   // 9.5 db

//AUX Channel Interface
#define VAL_EDP_TX_AUX_WAIT_TIME						400	//us

#define VAL_EDP_TX_AUX_CMD_WRITE						(0x8 << 8)
#define VAL_EDP_TX_AUX_CMD_READ							(0x9 << 8)
#define VAL_EDP_TX_AUX_CMD_I2C_WRITE					(0x0 << 8)
#define VAL_EDP_TX_AUX_CMD_I2C_WRITE_MOT				(0x4 << 8)
#define VAL_EDP_TX_AUX_CMD_I2C_READ						(0x1 << 8)
#define VAL_EDP_TX_AUX_CMD_I2C_READ_MOT					(0x5 << 8)
#define VAL_EDP_TX_AUX_CMD_I2C_WRITE_STATUS				(0x2 << 8)

#define VAL_EDP_TX_AUX_REPLY_CODE_ACK					(0x0 << 0)
#define VAL_EDP_TX_AUX_REPLY_CODE_NACK					(0x1 << 0)
#define VAL_EDP_TX_AUX_REPLY_CODE_DEFER					(0x2 << 0)
#define VAL_EDP_TX_AUX_REPLY_CODE_I2C_ACK				(0x0 << 0)
#define VAL_EDP_TX_AUX_REPLY_CODE_I2C_NACK				(0x4 << 0)
#define VAL_EDP_TX_AUX_REPLY_CODE_I2C_DEFER				(0x8 << 0)

#define VAL_EDP_TX_AUX_STATE_TIMEOUT					(1 << 3)
#define VAL_EDP_TX_AUX_STATE_RECEIVED					(1 << 2)
#define VAL_EDP_TX_AUX_STATE_REQUEST_IN_PROGRESS		(1 << 1)
#define VAL_EDP_TX_AUX_STATE_HPD_STATE					(1 << 0)

#define VAL_EDP_TX_AUX_STATUS_REPLY_ERROR				(1 << 3)
#define VAL_EDP_TX_AUX_STATUS_REQUEST_IN_PROGRESS		(1 << 2)
#define VAL_EDP_TX_AUX_STATUS_REPLY_IN_PROGRESS			(1 << 1)
#define VAL_EDP_TX_AUX_STATUS_REPLY_RECEIVED			(1 << 0)

#define VAL_EDP_TX_LANE_STATUS_OK_4						0x17777
#define VAL_EDP_TX_LANE_STATUS_OK_2						0x10077
#define VAL_EDP_TX_LANE_STATUS_OK_1						0x10007
#define VAL_EDP_TX_LANE_STATUS_OK_NONE					0xfffff

//operation
#define VAL_EDP_TX_TRAINING_RETRY_COUNT			5
#define VAL_EDP_TX_AUX_MAX_DEFER_COUNT			7
#define VAL_EDP_TX_AUX_MAX_TIMEOUT_COUNT		5


//******************************************************//

//********************************************************//
// displayport sink (rx) control
// DPCD
//********************************************************//
#define VAL_EDP_DPCD_REPLY_RECEIVED					(1 << 0)
//**********************************************//

//**********************************************//
//Training status
//**********************************************//
//DPCD device service IRQ
#define BIT_EDP_DPCD_SERVICE_IRQ_HDCP				2
#define BIT_EDP_DPCD_SERVICE_IRQ_TEST				1
#define BIT_EDP_DPCD_SERVICE_IRQ_REMOTE				0

#define VAL_EDP_TRAINING_PATTERN_OFF				0x00
#define VAL_EDP_TRAINING_PATTERN_1					0x01
#define VAL_EDP_TRAINING_PATTERN_2					0x02
#define VAL_EDP_TRAINING_PATTERN_3					0x03

// Lane status register constants
#define VAL_EDP_LANE_0_STATUS_CLK_REC_DONE			0x01
#define VAL_EDP_LANE_0_STATUS_CHAN_EQ_DONE			0x02
#define VAL_EDP_LANE_0_STATUS_SYM_LOCK_DONE			0x04
#define VAL_EDP_LANE_1_STATUS_CLK_REC_DONE			0x10
#define VAL_EDP_LANE_1_STATUS_CHAN_EQ_DONE			0x20
#define VAL_EDP_LANE_1_STATUS_SYM_LOCK_DONE			0x40
#define VAL_EDP_LANE_2_STATUS_CLK_REC_DONE			VAL_EDP_LANE_0_STATUS_CLK_REC_DONE      
#define VAL_EDP_LANE_2_STATUS_CHAN_EQ_DONE			VAL_EDP_LANE_0_STATUS_CHAN_EQ_DONE 
#define VAL_EDP_LANE_2_STATUS_SYM_LOCK_DONE			VAL_EDP_LANE_0_STATUS_SYM_LOCK_DONE
#define VAL_EDP_LANE_3_STATUS_CLK_REC_DONE			VAL_EDP_LANE_1_STATUS_CLK_REC_DONE 
#define VAL_EDP_LANE_3_STATUS_CHAN_EQ_DONE			VAL_EDP_LANE_1_STATUS_CHAN_EQ_DONE 
#define VAL_EDP_LANE_3_STATUS_SYM_LOCK_DONE			VAL_EDP_LANE_1_STATUS_SYM_LOCK_DONE
#define VAL_EDP_LANE_ALIGNMENT_DONE					1

// Lane status register constants
#define BIT_EDP_LANE_0_STATUS_CLK_REC_DONE			0
#define BIT_EDP_LANE_0_STATUS_CHAN_EQ_DONE			1
#define BIT_EDP_LANE_0_STATUS_SYM_LOCK_DONE			2
#define BIT_EDP_LANE_1_STATUS_CLK_REC_DONE			4
#define BIT_EDP_LANE_1_STATUS_CHAN_EQ_DONE			5
#define BIT_EDP_LANE_1_STATUS_SYM_LOCK_DONE			6
#define BIT_EDP_LANE_2_STATUS_CLK_REC_DONE			BIT_EDP_LANE_0_STATUS_CLK_REC_DONE      
#define BIT_EDP_LANE_2_STATUS_CHAN_EQ_DONE			BIT_EDP_LANE_0_STATUS_CHAN_EQ_DONE 
#define BIT_EDP_LANE_2_STATUS_SYM_LOCK_DONE			BIT_EDP_LANE_0_STATUS_SYM_LOCK_DONE
#define BIT_EDP_LANE_3_STATUS_CLK_REC_DONE			BIT_EDP_LANE_1_STATUS_CLK_REC_DONE 
#define BIT_EDP_LANE_3_STATUS_CHAN_EQ_DONE			BIT_EDP_LANE_1_STATUS_CHAN_EQ_DONE 
#define BIT_EDP_LANE_3_STATUS_SYM_LOCK_DONE			BIT_EDP_LANE_1_STATUS_SYM_LOCK_DONE
#define BIT_EDP_LANE_ALIGNMENT_DONE					0

// Link training constants
#define VAL_EDP_MAX_TRAINING_ATTEMPTS				5
#define VAL_EDP_CLOCK_REC_TIMEOUT					1//ms //100 //us
#define VAL_EDP_CHAN_EQ_TIMEOUT						4//ms //400 //us
#define VAL_EDP_MAX_DEFER_COUNT						7
#define VAL_EDP_MAX_TIMEOUT_COUNT					5
#define VAL_EDP_MAX_DELAY_CYCLES					10 // 10us delay

// Link training state constants
#define VAL_EDP_TS_CLOCK_REC						0x01
#define VAL_EDP_TS_CHANNEL_EQ						0x02
#define VAL_EDP_TS_ADJUST_SPD						0x04
#define VAL_EDP_TS_ADJUST_LANES						0x08
#define VAL_EDP_TS_UPDATE_STATUS					0x10

// Embedded DisplayPort constants
#define VAL_EDP_NOT_SUPPORTED						0x707
//**********************************************//

#define VAL_EDP_DPCD_TEST_RESPONSE_ACK				(1 << 0)
#define VAL_EDP_DPCD_TEST_RESPONSE_NACK				(1 << 1)
#define VAL_EDP_DPCD_TEST_RESPONSE_EDID_CHKSUM_WR	(1 << 2)

//********************************************************//
// displayport operation stauts
//
// 0x00xx: EDP total operaion
// 0x11xx: AUX operation
// 0xAAxx: EDP link policy maker control
// 0xEExx: EDP configuration verify
// 0xFFxx: EDP training operation
//
// note: all success or correct status are x0000
//********************************************************//
#define VAL_EDP_CONFIG_VALID					0x0000
#define VAL_EDP_CONFIG_INVALID_LINK_RATE		0xEEAA
#define VAL_EDP_CONFIG_INVALID_LANE_COUNT		0xEEBB
#define VAL_EDP_CONFIG_HPD_DEASSERTED			0xEECC

#define VAL_EDP_TRAINING_CR_FAILED				0xFF11
#define VAL_EDP_TRAINING_CHAN_EQ_FAILED			0xFF22
#define VAL_EDP_TRAINING_INVALID_CONFIG			0xFF88

#define VAL_EDP_LPM_STATUS_TRAINING_SUCCESS		0x0000
#define VAL_EDP_LPM_STATUS_LINK_VALID			0x0000
#define VAL_EDP_LPM_STATUS_CHANGED				0xAA10
#define VAL_EDP_LPM_STATUS_RETRAIN				0xAA11
#define VAL_EDP_LPM_STATUS_LINK_RATE_ADJUST		0xAA12
#define VAL_EDP_LPM_STATUS_NOT_CONNECTED		0xAA13
#define VAL_EDP_LPM_STATUS_TX_NOT_CONFIGURED	0xAA21
#define VAL_EDP_LPM_STATUS_RX_IDLE				0xAA31
#define VAL_EDP_LPM_STATUS_RX_ACTIVE			0xAA32

#define VAL_EDP_TX_AUX_INVALID_PARAMETER		0x1144
#define VAL_EDP_TX_AUX_OPERATION_TIMEOUT		0x1133
#define VAL_EDP_TX_AUX_OPERATION_ERROR			0x1122
#define VAL_EDP_TX_AUX_OPERATION_FAILED			0x1111
#define VAL_EDP_TX_AUX_OPERATION_SUCCESS		0x0000

#define VAL_EDP_TX_OPERATION_FAILED				0x00FF
#define VAL_EDP_TX_OPERATION_SUCCESS			0x0000
//********************************************************//

#define VAL_EDP_TX_INVALID_VALUE    0xFF
static const unsigned char edp_link_rate_table[] = {
    VAL_EDP_TX_LINK_BW_SET_162,
    VAL_EDP_TX_LINK_BW_SET_270,
    VAL_EDP_TX_LINK_BW_SET_540,
    VAL_EDP_TX_INVALID_VALUE,
};

static const unsigned char edp_lane_count_table[] = {1, 2, 4, VAL_EDP_TX_INVALID_VALUE};
#define LINK_RATE_TO_CAPACITY(x)    (x * 8 / 10) //8/10 coding
static const unsigned edp_link_capacity_table[] = { //Mbps
    LINK_RATE_TO_CAPACITY(1620), //1.62G
    LINK_RATE_TO_CAPACITY(2700), //2.7G
    LINK_RATE_TO_CAPACITY(5400), //5.4G
};

static const unsigned char edp_vswing_table[] = {
    VAL_EDP_TX_PHY_VSWING_0,
    VAL_EDP_TX_PHY_VSWING_1,
    VAL_EDP_TX_PHY_VSWING_2,
    VAL_EDP_TX_PHY_VSWING_3,
    VAL_EDP_TX_INVALID_VALUE,
};
static const unsigned char edp_preemphasis_table[] = {
    VAL_EDP_TX_PHY_PREEMPHASIS_0,
    VAL_EDP_TX_PHY_PREEMPHASIS_1,
    VAL_EDP_TX_PHY_PREEMPHASIS_2,
    VAL_EDP_TX_PHY_PREEMPHASIS_3,
    VAL_EDP_TX_INVALID_VALUE,
};

typedef enum {
	EDP_HPD_STATE_DISCONNECTED = 0,
	EDP_HPD_STATE_CONNECTED,
	EDP_HPD_STATE_INTERRUPT,
} EDP_HPD_state_t;

typedef struct {
	unsigned char dpcd_rev;
	unsigned char max_lane_count;
	unsigned char max_link_rate;
	unsigned char downstream_port_caps[10];
	unsigned char downstream_port_types[10];
	unsigned char rev_string;
	unsigned char link_rate_string;
	char *port_type_string;
	unsigned num_rcv_ports;
	unsigned num_downstream_ports;
	unsigned char format_conversion_support;
	unsigned char oui_support;
	unsigned char ansi_8B10_support;
	unsigned char enhanced_framing_support;
	unsigned char downspread_support;
	unsigned char require_aux_handshake;
	unsigned char rx0_has_edid;
	unsigned char rx0_use_prev;
	unsigned char rx0_buffer_size;
} TRDP_DPCDData_t;

typedef struct {
	unsigned short h_active;
	unsigned short v_active;
	unsigned short h_period;
	unsigned short v_period;
	unsigned clk;
	unsigned short hsync_pol;
	unsigned short hsync_width;
	unsigned short hsync_bp;
	unsigned short vsync_pol;
	unsigned short vsync_width;
	unsigned short vsync_bp;
	
	unsigned short ppc;		//pixels per clock cycle
	unsigned short cformat;	//color format(0=RGB, 1=4:2:2, 2=Y only)
	unsigned short bpc;		//bits per color
	unsigned int sync_clock_mode;
} EDP_MSA_t; //Main Stream Attribute

typedef struct {
	unsigned char max_lane_count;
	unsigned char max_link_rate;
	unsigned char enhanced_framing_en;
	unsigned char lane_count;
	unsigned char link_rate;
	unsigned char vswing;
	unsigned char preemphasis;
	unsigned char ss_level;
	unsigned char link_update;
	unsigned char training_settings;
	unsigned char main_stream_enable;
	unsigned char use_dpcd_caps;
	unsigned char link_rate_adjust_en;
	unsigned char link_adaptive;
	unsigned int bit_rate; //Mbps
} EDP_Link_Config_t;

extern void edp_phy_config_update(unsigned char vswing_tx, unsigned char preemp_tx);

extern int dplpm_link_policy_maker(EDP_Link_Config_t *mlconfig, EDP_MSA_t *vm);
extern int dplpm_link_off(void);
extern void dplpm_off(void);
extern void edp_probe(Lcd_Config_t *pConf);
extern void edp_remove(Lcd_Config_t *pConf);

#endif
