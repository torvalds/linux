/*
 *  FM Driver for Connectivity chip of Texas Instruments.
 *  FM Common module header file
 *
 *  Copyright (C) 2011 Texas Instruments
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _FMDRV_COMMON_H
#define _FMDRV_COMMON_H

#define FM_ST_REG_TIMEOUT   msecs_to_jiffies(6000)	/* 6 sec */
#define FM_PKT_LOGICAL_CHAN_NUMBER  0x08   /* Logical channel 8 */

#define REG_RD       0x1
#define REG_WR      0x0

struct fm_reg_table {
	u8 opcode;
	u8 type;
	u8 *name;
};

#define STEREO_GET               0
#define RSSI_LVL_GET             1
#define IF_COUNT_GET             2
#define FLAG_GET                 3
#define RDS_SYNC_GET             4
#define RDS_DATA_GET             5
#define FREQ_SET                 10
#define AF_FREQ_SET              11
#define MOST_MODE_SET            12
#define MOST_BLEND_SET           13
#define DEMPH_MODE_SET           14
#define SEARCH_LVL_SET           15
#define BAND_SET                 16
#define MUTE_STATUS_SET          17
#define RDS_PAUSE_LVL_SET        18
#define RDS_PAUSE_DUR_SET        19
#define RDS_MEM_SET              20
#define RDS_BLK_B_SET            21
#define RDS_MSK_B_SET            22
#define RDS_PI_MASK_SET          23
#define RDS_PI_SET               24
#define RDS_SYSTEM_SET           25
#define INT_MASK_SET             26
#define SEARCH_DIR_SET           27
#define VOLUME_SET               28
#define AUDIO_ENABLE_SET         29
#define PCM_MODE_SET             30
#define I2S_MODE_CONFIG_SET      31
#define POWER_SET                32
#define INTX_CONFIG_SET          33
#define PULL_EN_SET              34
#define HILO_SET                 35
#define SWITCH2FREF              36
#define FREQ_DRIFT_REPORT        37

#define PCE_GET                  40
#define FIRM_VER_GET             41
#define ASIC_VER_GET             42
#define ASIC_ID_GET              43
#define MAN_ID_GET               44
#define TUNER_MODE_SET           45
#define STOP_SEARCH              46
#define RDS_CNTRL_SET            47

#define WRITE_HARDWARE_REG       100
#define CODE_DOWNLOAD            101
#define RESET                    102

#define FM_POWER_MODE            254
#define FM_INTERRUPT             255

/* Transmitter API */

#define CHANL_SET                55
#define CHANL_BW_SET		56
#define REF_SET                  57
#define POWER_ENB_SET            90
#define POWER_ATT_SET            58
#define POWER_LEV_SET            59
#define AUDIO_DEV_SET            60
#define PILOT_DEV_SET            61
#define RDS_DEV_SET              62
#define TX_BAND_SET              65
#define PUPD_SET                 91
#define AUDIO_IO_SET             63
#define PREMPH_SET               64
#define MONO_SET                 66
#define MUTE                     92
#define MPX_LMT_ENABLE           67
#define PI_SET                   93
#define ECC_SET                  69
#define PTY                      70
#define AF                       71
#define DISPLAY_MODE             74
#define RDS_REP_SET              77
#define RDS_CONFIG_DATA_SET      98
#define RDS_DATA_SET             99
#define RDS_DATA_ENB             94
#define TA_SET                   78
#define TP_SET                   79
#define DI_SET                   80
#define MS_SET                   81
#define PS_SCROLL_SPEED          82
#define TX_AUDIO_LEVEL_TEST      96
#define TX_AUDIO_LEVEL_TEST_THRESHOLD    73
#define TX_AUDIO_INPUT_LEVEL_RANGE_SET   54
#define RX_ANTENNA_SELECT        87
#define I2C_DEV_ADDR_SET         86
#define REF_ERR_CALIB_PARAM_SET          88
#define REF_ERR_CALIB_PERIODICITY_SET    89
#define SOC_INT_TRIGGER                  52
#define SOC_AUDIO_PATH_SET               83
#define SOC_PCMI_OVERRIDE                84
#define SOC_I2S_OVERRIDE         85
#define RSSI_BLOCK_SCAN_FREQ_SET 95
#define RSSI_BLOCK_SCAN_START    97
#define RSSI_BLOCK_SCAN_DATA_GET  5
#define READ_FMANT_TUNE_VALUE            104

/* SKB helpers */
struct fm_skb_cb {
	__u8 fm_op;
	struct completion *completion;
};

#define fm_cb(skb) ((struct fm_skb_cb *)(skb->cb))

/* FM Channel-8 command message format */
struct fm_cmd_msg_hdr {
	__u8 hdr;		/* Logical Channel-8 */
	__u8 len;		/* Number of bytes follows */
	__u8 op;		/* FM Opcode */
	__u8 rd_wr;		/* Read/Write command */
	__u8 dlen;		/* Length of payload */
} __attribute__ ((packed));

#define FM_CMD_MSG_HDR_SIZE    5	/* sizeof(struct fm_cmd_msg_hdr) */

/* FM Channel-8 event messgage format */
struct fm_event_msg_hdr {
	__u8 header;		/* Logical Channel-8 */
	__u8 len;		/* Number of bytes follows */
	__u8 status;		/* Event status */
	__u8 num_fm_hci_cmds;	/* Number of pkts the host allowed to send */
	__u8 op;		/* FM Opcode */
	__u8 rd_wr;		/* Read/Write command */
	__u8 dlen;		/* Length of payload */
} __attribute__ ((packed));

#define FM_EVT_MSG_HDR_SIZE     7	/* sizeof(struct fm_event_msg_hdr) */

/* TI's magic number in firmware file */
#define FM_FW_FILE_HEADER_MAGIC	     0x42535442

#define FM_ENABLE   1
#define FM_DISABLE  0

/* FLAG_GET register bits */
#define FM_FR_EVENT		(1 << 0)
#define FM_BL_EVENT		(1 << 1)
#define FM_RDS_EVENT		(1 << 2)
#define FM_BBLK_EVENT		(1 << 3)
#define FM_LSYNC_EVENT		(1 << 4)
#define FM_LEV_EVENT		(1 << 5)
#define FM_IFFR_EVENT		(1 << 6)
#define FM_PI_EVENT		(1 << 7)
#define FM_PD_EVENT		(1 << 8)
#define FM_STIC_EVENT		(1 << 9)
#define FM_MAL_EVENT		(1 << 10)
#define FM_POW_ENB_EVENT	(1 << 11)

/*
 * Firmware files of FM. ASIC ID and ASIC version will be appened to this,
 * later.
 */
#define FM_FMC_FW_FILE_START      ("fmc_ch8")
#define FM_RX_FW_FILE_START       ("fm_rx_ch8")
#define FM_TX_FW_FILE_START       ("fm_tx_ch8")

#define FM_UNDEFINED_FREQ		   0xFFFFFFFF

/* Band types */
#define FM_BAND_EUROPE_US	0
#define FM_BAND_JAPAN		1

/* Seek directions */
#define FM_SEARCH_DIRECTION_DOWN	0
#define FM_SEARCH_DIRECTION_UP		1

/* Tunner modes */
#define FM_TUNER_STOP_SEARCH_MODE	0
#define FM_TUNER_PRESET_MODE		1
#define FM_TUNER_AUTONOMOUS_SEARCH_MODE	2
#define FM_TUNER_AF_JUMP_MODE		3

/* Min and Max volume */
#define FM_RX_VOLUME_MIN	0
#define FM_RX_VOLUME_MAX	70

/* Volume gain step */
#define FM_RX_VOLUME_GAIN_STEP	0x370

/* Mute modes */
#define	FM_MUTE_ON		0
#define FM_MUTE_OFF		1
#define	FM_MUTE_ATTENUATE	2

#define FM_RX_UNMUTE_MODE		0x00
#define FM_RX_RF_DEP_MODE		0x01
#define FM_RX_AC_MUTE_MODE		0x02
#define FM_RX_HARD_MUTE_LEFT_MODE	0x04
#define FM_RX_HARD_MUTE_RIGHT_MODE	0x08
#define FM_RX_SOFT_MUTE_FORCE_MODE	0x10

/* RF dependent mute mode */
#define FM_RX_RF_DEPENDENT_MUTE_ON	1
#define FM_RX_RF_DEPENDENT_MUTE_OFF	0

/* RSSI threshold min and max */
#define FM_RX_RSSI_THRESHOLD_MIN	-128
#define FM_RX_RSSI_THRESHOLD_MAX	127

/* Stereo/Mono mode */
#define FM_STEREO_MODE		0
#define FM_MONO_MODE		1
#define FM_STEREO_SOFT_BLEND	1

/* FM RX De-emphasis filter modes */
#define FM_RX_EMPHASIS_FILTER_50_USEC	0
#define FM_RX_EMPHASIS_FILTER_75_USEC	1

/* FM RDS modes */
#define FM_RDS_DISABLE	0
#define FM_RDS_ENABLE	1

#define FM_NO_PI_CODE	0

/* FM and RX RDS block enable/disable  */
#define FM_RX_PWR_SET_FM_ON_RDS_OFF		0x1
#define FM_RX_PWR_SET_FM_AND_RDS_BLK_ON		0x3
#define FM_RX_PWR_SET_FM_AND_RDS_BLK_OFF	0x0

/* RX RDS */
#define FM_RX_RDS_FLUSH_FIFO		0x1
#define FM_RX_RDS_FIFO_THRESHOLD	64	/* tuples */
#define FM_RDS_BLK_SIZE		3	/* 3 bytes */

/* RDS block types */
#define FM_RDS_BLOCK_A		0
#define FM_RDS_BLOCK_B		1
#define FM_RDS_BLOCK_C		2
#define FM_RDS_BLOCK_Ctag	3
#define FM_RDS_BLOCK_D		4
#define FM_RDS_BLOCK_E		5

#define FM_RDS_BLK_IDX_A		0
#define FM_RDS_BLK_IDX_B		1
#define FM_RDS_BLK_IDX_C		2
#define FM_RDS_BLK_IDX_D		3
#define FM_RDS_BLK_IDX_UNKNOWN	0xF0

#define FM_RDS_STATUS_ERR_MASK	0x18

/*
 * Represents an RDS group type & version.
 * There are 15 groups, each group has 2 versions: A and B.
 */
#define FM_RDS_GROUP_TYPE_MASK_0A	    ((unsigned long)1<<0)
#define FM_RDS_GROUP_TYPE_MASK_0B	    ((unsigned long)1<<1)
#define FM_RDS_GROUP_TYPE_MASK_1A	    ((unsigned long)1<<2)
#define FM_RDS_GROUP_TYPE_MASK_1B	    ((unsigned long)1<<3)
#define FM_RDS_GROUP_TYPE_MASK_2A	    ((unsigned long)1<<4)
#define FM_RDS_GROUP_TYPE_MASK_2B	    ((unsigned long)1<<5)
#define FM_RDS_GROUP_TYPE_MASK_3A	    ((unsigned long)1<<6)
#define FM_RDS_GROUP_TYPE_MASK_3B           ((unsigned long)1<<7)
#define FM_RDS_GROUP_TYPE_MASK_4A	    ((unsigned long)1<<8)
#define FM_RDS_GROUP_TYPE_MASK_4B	    ((unsigned long)1<<9)
#define FM_RDS_GROUP_TYPE_MASK_5A	    ((unsigned long)1<<10)
#define FM_RDS_GROUP_TYPE_MASK_5B	    ((unsigned long)1<<11)
#define FM_RDS_GROUP_TYPE_MASK_6A	    ((unsigned long)1<<12)
#define FM_RDS_GROUP_TYPE_MASK_6B	    ((unsigned long)1<<13)
#define FM_RDS_GROUP_TYPE_MASK_7A	    ((unsigned long)1<<14)
#define FM_RDS_GROUP_TYPE_MASK_7B	    ((unsigned long)1<<15)
#define FM_RDS_GROUP_TYPE_MASK_8A           ((unsigned long)1<<16)
#define FM_RDS_GROUP_TYPE_MASK_8B	    ((unsigned long)1<<17)
#define FM_RDS_GROUP_TYPE_MASK_9A	    ((unsigned long)1<<18)
#define FM_RDS_GROUP_TYPE_MASK_9B	    ((unsigned long)1<<19)
#define FM_RDS_GROUP_TYPE_MASK_10A	    ((unsigned long)1<<20)
#define FM_RDS_GROUP_TYPE_MASK_10B	    ((unsigned long)1<<21)
#define FM_RDS_GROUP_TYPE_MASK_11A	    ((unsigned long)1<<22)
#define FM_RDS_GROUP_TYPE_MASK_11B	    ((unsigned long)1<<23)
#define FM_RDS_GROUP_TYPE_MASK_12A	    ((unsigned long)1<<24)
#define FM_RDS_GROUP_TYPE_MASK_12B	    ((unsigned long)1<<25)
#define FM_RDS_GROUP_TYPE_MASK_13A	    ((unsigned long)1<<26)
#define FM_RDS_GROUP_TYPE_MASK_13B	    ((unsigned long)1<<27)
#define FM_RDS_GROUP_TYPE_MASK_14A	    ((unsigned long)1<<28)
#define FM_RDS_GROUP_TYPE_MASK_14B	    ((unsigned long)1<<29)
#define FM_RDS_GROUP_TYPE_MASK_15A	    ((unsigned long)1<<30)
#define FM_RDS_GROUP_TYPE_MASK_15B	    ((unsigned long)1<<31)

/* RX Alternate Frequency info */
#define FM_RDS_MIN_AF		          1
#define FM_RDS_MAX_AF		        204
#define FM_RDS_MAX_AF_JAPAN	        140
#define FM_RDS_1_AF_FOLLOWS	        225
#define FM_RDS_25_AF_FOLLOWS	        249

/* RDS system type (RDS/RBDS) */
#define FM_RDS_SYSTEM_RDS		0
#define FM_RDS_SYSTEM_RBDS		1

/* AF on/off */
#define FM_RX_RDS_AF_SWITCH_MODE_ON	1
#define FM_RX_RDS_AF_SWITCH_MODE_OFF	0

/* Retry count when interrupt process goes wrong */
#define FM_IRQ_TIMEOUT_RETRY_MAX	5	/* 5 times */

/* Audio IO set values */
#define FM_RX_AUDIO_ENABLE_I2S	0x01
#define FM_RX_AUDIO_ENABLE_ANALOG	0x02
#define FM_RX_AUDIO_ENABLE_I2S_AND_ANALOG	0x03
#define FM_RX_AUDIO_ENABLE_DISABLE	0x00

/* HI/LO set values */
#define FM_RX_IFFREQ_TO_HI_SIDE		0x0
#define FM_RX_IFFREQ_TO_LO_SIDE		0x1
#define FM_RX_IFFREQ_HILO_AUTOMATIC	0x2

/*
 * Default RX mode configuration. Chip will be configured
 * with this default values after loading RX firmware.
 */
#define FM_DEFAULT_RX_VOLUME		10
#define FM_DEFAULT_RSSI_THRESHOLD	3

/* Range for TX power level in units for dB/uV */
#define FM_PWR_LVL_LOW			91
#define FM_PWR_LVL_HIGH			122

/* Chip specific default TX power level value */
#define FM_PWR_LVL_DEF			4

/* FM TX Pre-emphasis filter values */
#define FM_TX_PREEMPH_OFF		1
#define FM_TX_PREEMPH_50US		0
#define FM_TX_PREEMPH_75US		2

/* FM TX antenna impedance values */
#define FM_TX_ANT_IMP_50		0
#define FM_TX_ANT_IMP_200		1
#define FM_TX_ANT_IMP_500		2

/* Functions exported by FM common sub-module */
int fmc_prepare(struct fmdev *);
int fmc_release(struct fmdev *);

void fmc_update_region_info(struct fmdev *, u8);
int fmc_send_cmd(struct fmdev *, u8, u16,
				void *, unsigned int, void *, int *);
int fmc_is_rds_data_available(struct fmdev *, struct file *,
				struct poll_table_struct *);
int fmc_transfer_rds_from_internal_buff(struct fmdev *, struct file *,
					u8 __user *, size_t);

int fmc_set_freq(struct fmdev *, u32);
int fmc_set_mode(struct fmdev *, u8);
int fmc_set_region(struct fmdev *, u8);
int fmc_set_mute_mode(struct fmdev *, u8);
int fmc_set_stereo_mono(struct fmdev *, u16);
int fmc_set_rds_mode(struct fmdev *, u8);

int fmc_get_freq(struct fmdev *, u32 *);
int fmc_get_region(struct fmdev *, u8 *);
int fmc_get_mode(struct fmdev *, u8 *);

/*
 * channel spacing
 */
#define FM_CHANNEL_SPACING_50KHZ 1
#define FM_CHANNEL_SPACING_100KHZ 2
#define FM_CHANNEL_SPACING_200KHZ 4
#define FM_FREQ_MUL 50

#endif

