/*
 * opyright (C) 2011 Samsung Electronics
 *
 * Authors: Adam Hampson <ahampson@sta.samsung.com>
 *          Erik Gilling <konkers@android.com>
 *
 * Additional contributions by : Shankar Bandal <shankar.b@samsung.com>
 *                               Dharam Kumar <dharam.kr@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#ifndef _SII9234_DRIVER_H_
#define _SII9234_DRIVER_H_

/*Flag for MHL Factory test*/
#ifndef CONFIG_SS_FACTORY
#define CONFIG_SS_FACTORY	1
#endif

#ifndef __CONFIG_TMDS_OFFON_WORKAROUND__
#define __CONFIG_TMDS_OFFON_WORKAROUND__
#endif

#ifndef __CONFIG_USE_TIMER__
#define __CONFIG_USE_TIMER__
#endif

#ifndef CONFIG_SII9234_RCP
#define CONFIG_SII9234_RCP		1
#include <linux/input.h>
#endif
#include <linux/wakelock.h>

#ifdef CONFIG_SAMSUNG_MHL_9290
#include <linux/30pin_con.h>
#endif

#ifdef CONFIG_SAMSUNG_SMARTDOCK
#define ADC_SMARTDOCK   0x10 /* 40.2K ohm */
#endif

#define T_WAIT_TIMEOUT_RGND_INT		2000
#define T_WAIT_TIMEOUT_DISC_INT		1000
#define T_WAIT_TIMEOUT_RSEN_INT		200

#define T_SRC_VBUS_CBUS_TO_STABLE	200
#define T_SRC_WAKE_PULSE_WIDTH_1	19
#define T_SRC_WAKE_PULSE_WIDTH_2	60
#define T_SRC_WAKE_TO_DISCOVER		500
#define T_SRC_VBUS_CBUS_T0_STABLE	500

#define T_SRC_CBUS_FLOAT		100
#define T_HPD_WIDTH			100
#define T_SRC_RXSENSE_DEGLITCH		110
#define T_SRC_CBUS_DEGLITCH		2

/* MHL TX Addr 0x72 Registers */
#define MHL_TX_IDL_REG                  0x02
#define MHL_TX_IDH_REG                  0x03
#define MHL_TX_REV_REG                  0x04
#define MHL_TX_SRST                     0x05
#define MHL_TX_INTR1_REG                0x71
#define MHL_TX_INTR2_REG                0x72    /* Not Documented */
#define MHL_TX_INTR3_REG                0x73    /* Not Documented */
#define MHL_TX_INTR4_REG                0x74
#define MHL_TX_INTR1_ENABLE_REG         0x75
#define MHL_TX_INTR2_ENABLE_REG         0x76    /* Not Documented */
#define MHL_TX_INTR3_ENABLE_REG         0x77    /* Not Documented */
#define MHL_TX_INTR4_ENABLE_REG         0x78
#define MHL_TX_INT_CTRL_REG             0x79
#define MHL_TX_TMDS_CCTRL               0x80

#define MHL_TX_DISC_CTRL1_REG           0x90
#define MHL_TX_DISC_CTRL2_REG           0x91
#define MHL_TX_DISC_CTRL3_REG           0x92
#define MHL_TX_DISC_CTRL4_REG		0x93

#define MHL_TX_DISC_CTRL5_REG           0x94
#define MHL_TX_DISC_CTRL6_REG           0x95
#define MHL_TX_DISC_CTRL7_REG           0x96
#define MHL_TX_DISC_CTRL8_REG		0x97
#define MHL_TX_STAT1_REG		0x98
#define MHL_TX_STAT2_REG                0x99

#define MHL_TX_MHLTX_CTL1_REG           0xA0
#define MHL_TX_MHLTX_CTL2_REG           0xA1
#define MHL_TX_MHLTX_CTL4_REG           0xA3
#define MHL_TX_MHLTX_CTL6_REG           0xA5
#define MHL_TX_MHLTX_CTL7_REG           0xA6

/* MHL TX SYS STAT Registers */
#define MHL_TX_SYSSTAT_REG              0x09

/* MHL TX SYS STAT Register Bits */
#define RSEN_STATUS                     (1<<2)

/* MHL TX INTR4 Register Bits */
#define RGND_READY_INT                  (1<<6)
#define VBUS_LOW_INT                    (1<<5)
#define CBUS_LKOUT_INT                  (1<<4)
#define MHL_DISC_FAIL_INT               (1<<3)
#define MHL_EST_INT                     (1<<2)

/* MHL TX INTR4_ENABLE 0x78 Register Bits */
#define RGND_READY_MASK                 (1<<6)
#define CBUS_LKOUT_MASK                 (1<<4)
#define MHL_DISC_FAIL_MASK              (1<<3)
#define MHL_EST_MASK                    (1<<2)

/* MHL TX INTR1 Register Bits*/
#define HPD_CHANGE_INT                  (1<<6)
#define RSEN_CHANGE_INT                 (1<<5)

/* MHL TX INTR1_ENABLE 0x75 Register Bits*/
#define HPD_CHANGE_INT_MASK             (1<<6)
#define RSEN_CHANGE_INT_MASK            (1<<5)

/* CBUS_INT_1_ENABLE: CBUS Transaction Interrupt #1 Mask */
#define CBUS_INTR1_ENABLE_REG		0x09
#define CBUS_INTR2_ENABLE_REG		0x1F

/* CBUS Interrupt Status Registers*/
#define CBUS_INT_STATUS_1_REG           0x08
#define CBUS_INT_STATUS_2_REG           0x1E

/* CBUS INTR1 STATUS Register bits */
#define MSC_RESP_ABORT                  (1<<6)
#define MSC_REQ_ABORT                   (1<<5)
#define MSC_REQ_DONE                    (1<<4)
#define MSC_MSG_RECD                    (1<<3)
#define CBUS_DDC_ABORT                  (1<<2)

/* CBUS INTR1 STATUS 0x09 Enable Mask*/
#define MSC_RESP_ABORT_MASK             (1<<6)
#define MSC_REQ_ABORT_MASK              (1<<5)
#define MSC_REQ_DONE_MASK               (1<<4)
#define MSC_MSG_RECD_MASK               (1<<3)
#define CBUS_DDC_ABORT_MASK             (1<<2)

/* CBUS INTR2 STATUS Register bits */
#define WRT_STAT_RECD                   (1<<3)
#define SET_INT_RECD                    (1<<2)
#define WRT_BURST_RECD                  (1<<0)

/* CBUS INTR2 STATUS 0x1F Enable Mask*/
#define WRT_STAT_RECD_MASK              (1<<3)
#define SET_INT_RECD_MASK               (1<<2)
#define WRT_BURST_RECD_MASK             (1<<0)

#define MHL_INT_EDID_CHG                (1<<1)

#define MHL_RCHANGE_INT			0x20
#define MHL_DCHANGE_INT			0x21
#define MHL_INT_DCAP_CHG		(1<<0)
#define MHL_INT_DSCR_CHG		(1<<1)
#define MHL_INT_REQ_WRT			(1<<2)
#define MHL_INT_GRT_WRT			(1<<3)

/* CBUS Control Registers*/
/* Retry count for all MSC commands*/
#define MSC_RETRY_FAIL_LIM_REG          0x1D

#define MSC_REQ_ABORT_REASON_REG        0x0D
#define MSC_RESP_ABORT_REASON_REG       0x0E

/* MSC Requestor/Responder Abort Reason Register bits*/
#define ABORT_BY_PEER                   (1<<7)
#define UNDEF_CMD                       (1<<3)
#define TIMEOUT                         (1<<2)
#define PROTO_ERROR                     (1<<1)
#define MAX_FAIL                        (1<<0)

#define REG_CBUS_INTR_STATUS		0x08
/* Responder aborted DDC command at translation layer */
#define BIT_DDC_ABORT			(1<<2)
/* Responder sent a VS_MSG packet (response data or command.) */
#define BIT_MSC_MSG_RCV			(1<<3)
 /* Responder sent ACK packet (not VS_MSG) */
#define BIT_MSC_XFR_DONE		(1<<4)
 /* Command send aborted on TX side */
#define BIT_MSC_XFR_ABORT		(1<<5)
#define BIT_MSC_ABORT			(1<<6)

/* Set HPD came from Downstream, */
#define SET_HPD_DOWNSTREAM              (1<<6)

/* MHL TX DISC1 Register Bits */
#define DISC_EN                         (1<<0)

/* MHL TX DISC2 Register Bits */
#define SKIP_GND                        (1<<6)
#define ATT_THRESH_SHIFT                0x04
#define ATT_THRESH_MASK                 (0x03 << ATT_THRESH_SHIFT)
#define USB_D_OEN                       (1<<3)
#define DEGLITCH_TIME_MASK              0x07
#define DEGLITCH_TIME_2MS               0
#define DEGLITCH_TIME_4MS               1
#define DEGLITCH_TIME_8MS               2
#define DEGLITCH_TIME_16MS              3
#define DEGLITCH_TIME_40MS              4
#define DEGLITCH_TIME_50MS              5
#define DEGLITCH_TIME_60MS              6
#define DEGLITCH_TIME_128MS             7

#define DISC_CTRL3_COMM_IMME            (1<<7)
#define DISC_CTRL3_FORCE_MHL            (1<<6)
#define DISC_CTRL3_FORCE_USB            (1<<4)
#define DISC_CTRL3_USB_EN               (1<<3)

/* MHL TX DISC4 0x93 Register Bits*/
#define CBUS_DISC_PUP_SEL_SHIFT         6
#define CBUS_DISC_PUP_SEL_MASK          (3<<CBUS_DISC_PUP_SEL_SHIFT)
#define CBUS_DISC_PUP_SEL_10K           (2<<CBUS_DISC_PUP_SEL_SHIFT)
#define CBUS_DISC_PUP_SEL_OPEN          (0<<CBUS_DISC_PUP_SEL_SHIFT)
#define CBUS_IDLE_PUP_SEL_SHIFT         4
#define CBUS_IDLE_PUP_SEL_MASK          (3<<CBUS_IDLE_PUP_SEL_SHIFT)
#define CBUS_IDLE_PUP_SEL_OPEN          (0<<CBUS_IDLE_PUP_SEL_SHIFT)

/* MHL TX DISC5 0x94 Register Bits */
#define CBUS_MHL_PUP_SEL_MASK		0x03
#define CBUS_MHL_PUP_SEL_5K		0x01
#define CBUS_MHL_PUP_SEL_OPEN           0x00

/* MHL Interrupt Registers */
#define CBUS_MHL_INTR_REG_0		0xA0
#define CBUS_MHL_INTR_REG_1		0xA1
#define CBUS_MHL_INTR_REG_2		0xA2
#define CBUS_MHL_INTR_REG_3		0xA3

/* MHL Status Registers */
#define CBUS_MHL_STATUS_REG_0           0xB0
#define CBUS_MHL_STATUS_REG_1           0xB1
#define CBUS_MHL_STATUS_REG_2           0xB2
#define CBUS_MHL_STATUS_REG_3           0xB3

/* MHL TX DISC6 0x95 Register Bits */
#define USB_D_OVR                       (1<<7)
#define USB_ID_OVR                      (1<<6)
#define DVRFLT_SEL                      (1<<5)
#define BLOCK_RGND_INT                  (1<<4)
#define SKIP_DEG                        (1<<3)
#define CI2CA_POL                       (1<<2)
#define CI2CA_WKUP                      (1<<1)
#define SINGLE_ATT                      (1<<0)

/* MHL TX DISC7 0x96 Register Bits
 *
 * Bits 7 and 6 are labeled as reserved but seem to be related to toggling
 * the CBUS signal when generating the wake pulse sequence.
 */
#define USB_D_ODN                       (1<<5)
#define VBUS_CHECK                      (1<<2)
#define RGND_INTP_MASK                  0x03
#define RGND_INTP_OPEN                  0
#define RGND_INTP_2K                    1
#define RGND_INTP_1K                    2
#define RGND_INTP_SHORT                 3

/* TPI Addr 0x7A Registers */
#define TPI_DPD_REG                     0x3D

#define TPI_PD_TMDS                     (1<<5)
#define TPI_PD_OSC_EN                   (1<<4)
#define TPI_TCLK_PHASE                  (1<<3)
#define TPI_PD_IDCK                     (1<<2)
#define TPI_PD_OSC                      (1<<1)
#define TPI_PD                          (1<<0)

#define CBUS_CONFIG_REG                 0x07
#define CBUS_LINK_CONTROL_2_REG         0x31

/* HDMI RX Registers */
#define HDMI_RX_TMDS0_CCTRL1_REG        0x10
#define HDMI_RX_TMDS_CLK_EN_REG         0x11
#define HDMI_RX_TMDS_CH_EN_REG          0x12
#define HDMI_RX_PLL_CALREFSEL_REG       0x17
#define HDMI_RX_PLL_VCOCAL_REG          0x1A
#define HDMI_RX_EQ_DATA0_REG            0x22
#define HDMI_RX_EQ_DATA1_REG            0x23
#define HDMI_RX_EQ_DATA2_REG            0x24
#define HDMI_RX_EQ_DATA3_REG            0x25
#define HDMI_RX_EQ_DATA4_REG            0x26
#define HDMI_RX_TMDS_ZONE_CTRL_REG      0x4C
#define HDMI_RX_TMDS_MODE_CTRL_REG      0x4D

/* MHL SideBand Channel(MSC) Registers */
#define CBUS_MSC_COMMAND_START_REG      0x12
#define CBUS_MSC_OFFSET_REG             0x13
#define CBUS_MSC_FIRST_DATA_OUT_REG     0x14
#define CBUS_MSC_SECOND_DATA_OUT_REG    0x15
#define CBUS_MSC_FIRST_DATA_IN_REG      0x16
#define CBUS_MSC_SECOND_DATA_IN_REG     0x17
#define CBUS_MSC_MSG_CMD_IN_REG         0x18
#define CBUS_MSC_MSG_DATA_IN_REG        0x19
#define CBUS_MSC_WRITE_BURST_LEN        0x20
#define	CBUS_MSC_RAP_CONTENT_ON		0x10
#define	CBUS_MSC_RAP_CONTENT_OFF	0x11

/* Register Bits for CBUS_MSC_COMMAND_START_REG */
#define START_BIT_MSC_RESERVED		(1<<0)
#define START_BIT_MSC_MSG		(1<<1)
#define START_BIT_READ_DEVCAP		(1<<2)
#define START_BIT_WRITE_STAT_INT	(1<<3)
#define START_BIT_WRITE_BURST		(1<<4)

/* MHL Device Capability Register offsets */
#define DEVCAP_DEV_STATE		0x00
#define DEVCAP_MHL_VERSION		0x01
#define DEVCAP_DEV_CAT                  0x02
#define DEVCAP_ADOPTER_ID_H		0x03
#define DEVCAP_ADOPTER_ID_L		0x04
#define DEVCAP_VID_LINK_MODE		0x05
#define DEVCAP_AUD_LINK_MODE		0x06
#define DEVCAP_VIDEO_TYPE		0x07
#define DEVCAP_LOG_DEV_MAP		0x08
#define DEVCAP_BANDWIDTH		0x09
#define DEVCAP_DEV_FEATURE_FLAG         0x0A
#define DEVCAP_DEVICE_ID_H              0x0B
#define DEVCAP_DEVICE_ID_L              0x0C
#define DEVCAP_SCRATCHPAD_SIZE		0x0D
#define DEVCAP_INT_STAT_SIZE		0x0E
#define DEVCAP_RESERVED			0x0F

#define BIT0				0x01
#define BIT1				0x02
#define BIT2				0x04
#define BIT3				0x08
#define BIT4				0x10
#define BIT5				0x20
#define BIT6				0x40
#define BIT7				0x80

#define MHL_DEV_CATEGORY_POW_BIT        (1<<4)

/* Device Capability Ready Bit */
#define MHL_STATUS_DCAP_READY           (1<<0)

#define MHL_FEATURE_RCP_SUPPORT         (1<<0)
#define MHL_FEATURE_RAP_SUPPORT         (1<<1)
#define MHL_FEATURE_SP_SUPPORT          (1<<2)

#define MHL_STATUS_CLK_MODE_PACKED_PIXEL	0x02
#define MHL_STATUS_CLK_MODE_NORMAL	0x03

#define MHL_STATUS_PATH_ENABLED		0x08
#define MHL_STATUS_PATH_DISABLED	0x00

#define MHL_STATUS_REG_CONNECTED_RDY	0x30

#define CBUS_PKT_BUF_COUNT		18

#define VBUS_NONE			0
#define VBUS_USB			1

#ifdef __MHL_NEW_CBUS_MSC_CMD__
#	define SS_MHL_DONGLE_DEV_ID		0x1134
#	define SS_MHL_DOCK_DEV_ID		0x1234
#endif

/* wolverin*/
#define	INTR_CBUS1_DESIRED_MASK		(BIT2 | BIT3 | BIT4 | BIT5 | BIT6)
#define	INTR_CBUS2_DESIRED_MASK		(BIT2 | BIT3) /* (BIT0| BIT2 | BIT3) */

enum hpd_state {
	LOW = 0,
	HIGH
};

enum page_num {
	PAGE0 = 0,
	PAGE1,
	PAGE2,
	PAGE3
};

enum rgnd_state {
	RGND_UNKNOWN = 0,
	RGND_OPEN,
	RGND_1K,
	RGND_2K,
	RGND_SHORT
};

enum mhl_state {
	NO_MHL_STATUS = 0x00,
	STATE_DISCONNECTED,
	STATE_DISCOVERY_FAILED,
	STATE_CBUS_LOCKOUT,
	STATE_ESTABLISHED,
	MHL_READY_RGND_DETECT,  /* after d3mode wait rgnd int */
	MHL_RX_CONNECTED, /* after detection rgnd 1K */
	MHL_USB_CONNECTED, /* mhl is not 1K */
	MHL_DISCOVERY_ON,
	MHL_DISCOVERY_SUCCESS, /* after detection RSEN */
};

enum msc_subcommand {
	/* MSC_MSG Sub-Command codes */
	MSG_RCP =       0x10,
	MSG_RCPK =      0x11,
	MSG_RCPE =      0x12,
	MSG_RAP =       0x20,
	MSG_RAPK =      0x21,
};

enum cbus_command {
	CBUS_IDLE =     0x00,
	CBUS_ACK =		0x33,
	CBUS_NACK =		0x35,
	CBUS_WRITE_STAT  =      0x60 | 0x80,
	CBUS_SET_INT     =      0x60,
	CBUS_READ_DEVCAP =      0x61,
	CBUS_GET_STATE =	0x62,
	CBUS_GET_VENDOR_ID =	0x63,
	CBUS_SET_HPD =		0x64,
	CBUS_CLR_HPD =		0x65,
	CBUS_SET_CAP_ID =	0x66,
	CBUS_GET_CAP_ID =	0x67,
	CBUS_MSC_MSG =		0x68,
	CBUS_GET_SC1_ERR_CODE =	0x69,
	CBUS_GET_DDC_ERR_CODE =	0x6A,
	CBUS_GET_MSC_ERR_CODE =	0x6B,
	CBUS_WRITE_BURST =      0x6C,
	CBUS_GET_SC3_ERR_CODE =	0x6D,
};
#if 0
enum mhl_status_enum_type {
	NO_MHL_STATUS = 0x00,
	MHL_INIT_DONE,
	MHL_WAITING_RGND_DETECT,
	MHL_CABLE_CONNECT,
	MHL_DISCOVERY_START,
	MHL_DISCOVERY_END,
	MHL_DISCOVERY_SUCCESS,
	MHL_DISCOVERY_FAIL,
	MHL_RSEN_GLITCH,
	MHL_RSEN_LOW,
};

#endif
struct mhl_tx_status_type {
	u8 intr4_mask_value;
	u8 intr1_mask_value;
	u8 intr_cbus1_mask_value;
	u8 intr_cbus2_mask_value;
/*	enum mhl_status_enum_type mhl_status;*/
	u8 linkmode;
	u8 connected_ready;
	bool cbus_connected;
	bool sink_hpd;
	bool rgnd_1k;
	u8 rsen_check_available;
};

static inline bool mhl_state_is_error(enum mhl_state state)
{
	return state == STATE_DISCOVERY_FAILED ||
		state == STATE_CBUS_LOCKOUT;
}

struct sii9234_data;
#define CBUS_DATA_LENGTH 2
/* Structure for holding MSC command data */
struct cbus_packet {
	enum cbus_command command;
	u8 offset;
	u8 data[CBUS_DATA_LENGTH];
	u8 status;
};

struct device_cap {
	u8 mhl_ver;
	u8 dev_type;
	u16 adopter_id;
	u8 vid_link_mode;
	u8 aud_link_mode;
	u8 video_type;
	u8 log_dev_map;
	u8 bandwidth;
	u16 device_id;
	u8 scratchpad_size;
	u8 int_stat_size;
#ifdef __MHL_NEW_CBUS_MSC_CMD__
	u8 reserved_data; /*Only SAMSUNG use this offset
			  as a method to distinguish TA and USB*/
#endif

	bool rcp_support;
	bool rap_support;
	bool sp_support;
};

struct sii9234_data {
	struct sii9234_platform_data	*pdata;
	wait_queue_head_t		wq;
#ifdef CONFIG_SAMSUNG_MHL_9290
	struct notifier_block           acc_con_nb;
	struct work_struct		tmds_reset_work;
#endif
	bool				claimed;
	u8				cbus_connected; /* wolverin */
	enum mhl_state			state;
	enum rgnd_state			rgnd;
	bool				rsen;
	atomic_t			is_irq_enabled;

	struct mutex			lock;
	struct mutex			cbus_lock;
	struct mutex			mhl_status_lock; /* wolverin */
	struct cbus_packet		cbus_pkt;
	struct cbus_packet		cbus_pkt_buf[CBUS_PKT_BUF_COUNT];
	struct device_cap		devcap;
	struct mhl_tx_status_type mhl_status_value;
#ifdef CONFIG_SII9234_RCP
	u8 error_key;
	struct input_dev		*input_dev;
#endif
#ifdef __MHL_NEW_CBUS_MSC_CMD__
	struct completion		msc_complete;
	struct work_struct		msc_work;
	int				vbus_owner;
	int				dcap_ready_status;
#endif

	struct work_struct		mhl_restart_work;
	struct work_struct		mhl_end_work;
	struct work_struct		rgnd_work;
	struct work_struct		mhl_cbus_write_stat_work;
	struct work_struct		mhl_d3_work;
#ifdef __CONFIG_TMDS_OFFON_WORKAROUND__
	struct work_struct		tmds_offon_work;
#endif
#ifdef __CONFIG_USE_TIMER__
	struct timer_list		cbus_command_timer;
#endif
#ifdef CONFIG_MACH_MIDAS
	struct wake_lock		mhl_wake_lock;
#endif
	struct work_struct mhl_tx_init_work; /* wolverin */
	struct workqueue_struct *mhl_tx_init_wq;
	struct work_struct mhl_400ms_rsen_work;
	struct workqueue_struct *mhl_400ms_rsen_wq;

#ifdef CONFIG_EXTCON
	/* Extcon */
	struct extcon_specific_cable_nb extcon_dev;
	struct notifier_block extcon_nb;
	struct work_struct extcon_wq;
	bool extcon_attached;
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
	bool suspend_state;
#endif
#ifdef __CONFIG_TMDS_OFFON_WORKAROUND__
	bool tmds_state;
#endif
};

#ifdef __MHL_NEW_CBUS_MSC_CMD__
struct msc_packet {
	enum cbus_command	command;
	u8	offset;
	u8	data_1;
	u8	data_2;
	struct list_head p_msc_packet_list;
};

static int sii9234_msc_req_locked(struct sii9234_data *sii9234,
					struct msc_packet *msc_pkt);
static int sii9234_enqueue_msc_work(struct sii9234_data *sii9234, u8 command,
		u8 offset, u8 data_1, u8 data_2);
#endif
static struct device *sii9244_mhldev;
extern void mhl_hpd_handler(bool state);
static int sii9234_detection_callback(void);
static void sii9234_cancel_callback(void);
static u8 sii9234_tmds_control(struct sii9234_data *sii9234, bool enable);
static bool cbus_command_request(struct sii9234_data *sii9234,
				 enum cbus_command command,
				 u8 offset, u8 data);
static void cbus_command_response(struct sii9234_data *sii9234);
static irqreturn_t sii9234_irq_thread(int irq, void *data);

#ifdef CONFIG_SAMSUNG_MHL_9290
static int sii9234_30pin_init_for_9290(struct sii9234_data *sii9234);
void sii9234_tmds_reset(void);
void sii9234_tmds_reset_work(struct work_struct *work);
#endif

#ifdef CONFIG_SAMSUNG_USE_11PIN_CONNECTOR
#	if !defined(CONFIG_MACH_P4NOTE)
extern int max77693_muic_get_status1_adc1k_value(void);
#endif
#endif

#endif
