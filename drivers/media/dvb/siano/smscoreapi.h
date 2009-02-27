/*
 *  Driver for the Siano SMS1xxx USB dongle
 *
 *  author: Anatoly Greenblat
 *
 *  Copyright (c), 2005-2008 Siano Mobile Silicon, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation;
 *
 *  Software distributed under the License is distributed on an "AS IS"
 *  basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *
 *  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __smscoreapi_h__
#define __smscoreapi_h__

#include <linux/version.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/types.h>
#include <asm/page.h>
#include <linux/mutex.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"


#define kmutex_init(_p_) mutex_init(_p_)
#define kmutex_lock(_p_) mutex_lock(_p_)
#define kmutex_trylock(_p_) mutex_trylock(_p_)
#define kmutex_unlock(_p_) mutex_unlock(_p_)

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

#define SMS_ALLOC_ALIGNMENT					128
#define SMS_DMA_ALIGNMENT					16
#define SMS_ALIGN_ADDRESS(addr) \
	((((uintptr_t)(addr)) + (SMS_DMA_ALIGNMENT-1)) & ~(SMS_DMA_ALIGNMENT-1))

#define SMS_DEVICE_FAMILY2					1
#define SMS_ROM_NO_RESPONSE					2
#define SMS_DEVICE_NOT_READY				0x8000000

enum sms_device_type_st {
	SMS_STELLAR = 0,
	SMS_NOVA_A0,
	SMS_NOVA_B0,
	SMS_VEGA,
	SMS_NUM_OF_DEVICE_TYPES
};

struct smscore_device_t;
struct smscore_client_t;
struct smscore_buffer_t;

typedef int (*hotplug_t)(struct smscore_device_t *coredev,
			 struct device *device, int arrival);

typedef int (*setmode_t)(void *context, int mode);
typedef void (*detectmode_t)(void *context, int *mode);
typedef int (*sendrequest_t)(void *context, void *buffer, size_t size);
typedef int (*loadfirmware_t)(void *context, void *buffer, size_t size);
typedef int (*preload_t)(void *context);
typedef int (*postload_t)(void *context);

typedef int (*onresponse_t)(void *context, struct smscore_buffer_t *cb);
typedef void (*onremove_t)(void *context);

struct smscore_buffer_t {
	/* public members, once passed to clients can be changed freely */
	struct list_head entry;
	int				size;
	int				offset;

	/* private members, read-only for clients */
	void			*p;
	dma_addr_t		phys;
	unsigned long	offset_in_common;
};

struct smsdevice_params_t {
	struct device	*device;

	int				buffer_size;
	int				num_buffers;

	char			devpath[32];
	unsigned long	flags;

	setmode_t		setmode_handler;
	detectmode_t	detectmode_handler;
	sendrequest_t	sendrequest_handler;
	preload_t		preload_handler;
	postload_t		postload_handler;

	void			*context;
	enum sms_device_type_st device_type;
};

struct smsclient_params_t {
	int				initial_id;
	int				data_type;
	onresponse_t	onresponse_handler;
	onremove_t		onremove_handler;

	void			*context;
};

/* GPIO definitions for antenna frequency domain control (SMS8021) */
#define SMS_ANTENNA_GPIO_0					1
#define SMS_ANTENNA_GPIO_1					0

#define BW_8_MHZ							0
#define BW_7_MHZ							1
#define BW_6_MHZ							2
#define BW_5_MHZ							3
#define BW_ISDBT_1SEG						4
#define BW_ISDBT_3SEG						5

#define MSG_HDR_FLAG_SPLIT_MSG				4

#define MAX_GPIO_PIN_NUMBER					31

#define HIF_TASK							11
#define SMS_HOST_LIB						150
#define DVBT_BDA_CONTROL_MSG_ID				201

#define SMS_MAX_PAYLOAD_SIZE				240
#define SMS_TUNE_TIMEOUT					500

#define MSG_SMS_GPIO_CONFIG_REQ				507
#define MSG_SMS_GPIO_CONFIG_RES				508
#define MSG_SMS_GPIO_SET_LEVEL_REQ			509
#define MSG_SMS_GPIO_SET_LEVEL_RES			510
#define MSG_SMS_GPIO_GET_LEVEL_REQ			511
#define MSG_SMS_GPIO_GET_LEVEL_RES			512
#define MSG_SMS_RF_TUNE_REQ					561
#define MSG_SMS_RF_TUNE_RES					562
#define MSG_SMS_INIT_DEVICE_REQ				578
#define MSG_SMS_INIT_DEVICE_RES				579
#define MSG_SMS_ADD_PID_FILTER_REQ			601
#define MSG_SMS_ADD_PID_FILTER_RES			602
#define MSG_SMS_REMOVE_PID_FILTER_REQ		603
#define MSG_SMS_REMOVE_PID_FILTER_RES		604
#define MSG_SMS_DAB_CHANNEL					607
#define MSG_SMS_GET_PID_FILTER_LIST_REQ		608
#define MSG_SMS_GET_PID_FILTER_LIST_RES		609
#define MSG_SMS_GET_STATISTICS_REQ			615
#define MSG_SMS_GET_STATISTICS_RES			616
#define MSG_SMS_SET_ANTENNA_CONFIG_REQ		651
#define MSG_SMS_SET_ANTENNA_CONFIG_RES		652
#define MSG_SMS_GET_STATISTICS_EX_REQ		653
#define MSG_SMS_GET_STATISTICS_EX_RES		654
#define MSG_SMS_SLEEP_RESUME_COMP_IND		655
#define MSG_SMS_DATA_DOWNLOAD_REQ			660
#define MSG_SMS_DATA_DOWNLOAD_RES			661
#define MSG_SMS_SWDOWNLOAD_TRIGGER_REQ		664
#define MSG_SMS_SWDOWNLOAD_TRIGGER_RES		665
#define MSG_SMS_SWDOWNLOAD_BACKDOOR_REQ		666
#define MSG_SMS_SWDOWNLOAD_BACKDOOR_RES		667
#define MSG_SMS_GET_VERSION_EX_REQ			668
#define MSG_SMS_GET_VERSION_EX_RES			669
#define MSG_SMS_SET_CLOCK_OUTPUT_REQ		670
#define MSG_SMS_I2C_SET_FREQ_REQ			685
#define MSG_SMS_GENERIC_I2C_REQ				687
#define MSG_SMS_GENERIC_I2C_RES				688
#define MSG_SMS_DVBT_BDA_DATA				693
#define MSG_SW_RELOAD_REQ					697
#define MSG_SMS_DATA_MSG					699
#define MSG_SW_RELOAD_START_REQ				702
#define MSG_SW_RELOAD_START_RES				703
#define MSG_SW_RELOAD_EXEC_REQ				704
#define MSG_SW_RELOAD_EXEC_RES				705
#define MSG_SMS_SPI_INT_LINE_SET_REQ		710
#define MSG_SMS_GPIO_CONFIG_EX_REQ			712
#define MSG_SMS_GPIO_CONFIG_EX_RES			713
#define MSG_SMS_ISDBT_TUNE_REQ				776
#define MSG_SMS_ISDBT_TUNE_RES				777

#define SMS_INIT_MSG_EX(ptr, type, src, dst, len) do { \
	(ptr)->msgType = type; (ptr)->msgSrcId = src; (ptr)->msgDstId = dst; \
	(ptr)->msgLength = len; (ptr)->msgFlags = 0; \
} while (0)
#define SMS_INIT_MSG(ptr, type, len) \
	SMS_INIT_MSG_EX(ptr, type, 0, HIF_TASK, len)

enum SMS_DEVICE_MODE {
	DEVICE_MODE_NONE = -1,
	DEVICE_MODE_DVBT = 0,
	DEVICE_MODE_DVBH,
	DEVICE_MODE_DAB_TDMB,
	DEVICE_MODE_DAB_TDMB_DABIP,
	DEVICE_MODE_DVBT_BDA,
	DEVICE_MODE_ISDBT,
	DEVICE_MODE_ISDBT_BDA,
	DEVICE_MODE_CMMB,
	DEVICE_MODE_RAW_TUNER,
	DEVICE_MODE_MAX,
};

struct SmsMsgHdr_ST {
	u16	msgType;
	u8	msgSrcId;
	u8	msgDstId;
	u16	msgLength; /* Length of entire message, including header */
	u16	msgFlags;
};

struct SmsMsgData_ST {
	struct SmsMsgHdr_ST	xMsgHeader;
	u32			msgData[1];
};

struct SmsDataDownload_ST {
	struct SmsMsgHdr_ST	xMsgHeader;
	u32			MemAddr;
	u8			Payload[SMS_MAX_PAYLOAD_SIZE];
};

struct SmsVersionRes_ST {
	struct SmsMsgHdr_ST	xMsgHeader;

	u16		ChipModel; /* e.g. 0x1102 for SMS-1102 "Nova" */
	u8		Step; /* 0 - Step A */
	u8		MetalFix; /* 0 - Metal 0 */

	u8		FirmwareId; /* 0xFF � ROM, otherwise the
				     * value indicated by
				     * SMSHOSTLIB_DEVICE_MODES_E */
	u8		SupportedProtocols; /* Bitwise OR combination of
					     * supported protocols */

	u8		VersionMajor;
	u8		VersionMinor;
	u8		VersionPatch;
	u8		VersionFieldPatch;

	u8		RomVersionMajor;
	u8		RomVersionMinor;
	u8		RomVersionPatch;
	u8		RomVersionFieldPatch;

	u8		TextLabel[34];
};

struct SmsFirmware_ST {
	u32			CheckSum;
	u32			Length;
	u32			StartAddress;
	u8			Payload[1];
};

struct SMSHOSTLIB_STATISTICS_ST {
	u32 Reserved; /* Reserved */

	/* Common parameters */
	u32 IsRfLocked; /* 0 - not locked, 1 - locked */
	u32 IsDemodLocked; /* 0 - not locked, 1 - locked */
	u32 IsExternalLNAOn; /* 0 - external LNA off, 1 - external LNA on */

	/* Reception quality */
	s32  SNR; /* dB */
	u32 BER; /* Post Viterbi BER [1E-5] */
	u32 FIB_CRC;	/* CRC errors percentage, valid only for DAB */
	u32 TS_PER; /* Transport stream PER, 0xFFFFFFFF indicate N/A,
		     * valid only for DVB-T/H */
	u32 MFER; /* DVB-H frame error rate in percentage,
		   * 0xFFFFFFFF indicate N/A, valid only for DVB-H */
	s32  RSSI; /* dBm */
	s32  InBandPwr; /* In band power in dBM */
	s32  CarrierOffset; /* Carrier Offset in bin/1024 */

	/* Transmission parameters, valid only for DVB-T/H */
	u32 Frequency; /* Frequency in Hz */
	u32 Bandwidth; /* Bandwidth in MHz */
	u32 TransmissionMode; /* Transmission Mode, for DAB modes 1-4,
			       * for DVB-T/H FFT mode carriers in Kilos */
	u32 ModemState; /* from SMS_DvbModemState_ET */
	u32 GuardInterval; /* Guard Interval, 1 divided by value */
	u32 CodeRate; /* Code Rate from SMS_DvbModemState_ET */
	u32 LPCodeRate; /* Low Priority Code Rate from SMS_DvbModemState_ET */
	u32 Hierarchy; /* Hierarchy from SMS_Hierarchy_ET */
	u32 Constellation; /* Constellation from SMS_Constellation_ET */

	/* Burst parameters, valid only for DVB-H */
	u32 BurstSize; /* Current burst size in bytes */
	u32 BurstDuration; /* Current burst duration in mSec */
	u32 BurstCycleTime; /* Current burst cycle time in mSec */
	u32 CalculatedBurstCycleTime; /* Current burst cycle time in mSec,
				       * as calculated by demodulator */
	u32 NumOfRows; /* Number of rows in MPE table */
	u32 NumOfPaddCols; /* Number of padding columns in MPE table */
	u32 NumOfPunctCols; /* Number of puncturing columns in MPE table */
	/* Burst parameters */
	u32 ErrorTSPackets; /* Number of erroneous transport-stream packets */
	u32 TotalTSPackets; /* Total number of transport-stream packets */
	u32 NumOfValidMpeTlbs; /* Number of MPE tables which do not include
				* errors after MPE RS decoding */
	u32 NumOfInvalidMpeTlbs; /* Number of MPE tables which include errors
				  * after MPE RS decoding */
	u32 NumOfCorrectedMpeTlbs; /* Number of MPE tables which were corrected
				    * by MPE RS decoding */

	/* Common params */
	u32 BERErrorCount; /* Number of errornous SYNC bits. */
	u32 BERBitCount; /* Total number of SYNC bits. */

	/* Interface information */
	u32 SmsToHostTxErrors; /* Total number of transmission errors. */

	/* DAB/T-DMB */
	u32 PreBER; /* DAB/T-DMB only: Pre Viterbi BER [1E-5] */

	/* DVB-H TPS parameters */
	u32 CellId; /* TPS Cell ID in bits 15..0, bits 31..16 zero;
		     * if set to 0xFFFFFFFF cell_id not yet recovered */

};

struct SmsMsgStatisticsInfo_ST {
	u32 RequestResult;

	struct SMSHOSTLIB_STATISTICS_ST Stat;

	/* Split the calc of the SNR in DAB */
	u32 Signal; /* dB */
	u32 Noise; /* dB */

};


struct smscore_gpio_config {
#define SMS_GPIO_DIRECTION_INPUT  0
#define SMS_GPIO_DIRECTION_OUTPUT 1
	u8 direction;

#define SMS_GPIO_PULLUPDOWN_NONE     0
#define SMS_GPIO_PULLUPDOWN_PULLDOWN 1
#define SMS_GPIO_PULLUPDOWN_PULLUP   2
#define SMS_GPIO_PULLUPDOWN_KEEPER   3
	u8 pullupdown;

#define SMS_GPIO_INPUTCHARACTERISTICS_NORMAL  0
#define SMS_GPIO_INPUTCHARACTERISTICS_SCHMITT 1
	u8 inputcharacteristics;

#define SMS_GPIO_OUTPUTSLEWRATE_FAST 0
#define SMS_GPIO_OUTPUTSLEWRATE_SLOW 1
	u8 outputslewrate;

#define SMS_GPIO_OUTPUTDRIVING_4mA  0
#define SMS_GPIO_OUTPUTDRIVING_8mA  1
#define SMS_GPIO_OUTPUTDRIVING_12mA 2
#define SMS_GPIO_OUTPUTDRIVING_16mA 3
	u8 outputdriving;
};

extern void smscore_registry_setmode(char *devpath, int mode);
extern int smscore_registry_getmode(char *devpath);

extern int smscore_register_hotplug(hotplug_t hotplug);
extern void smscore_unregister_hotplug(hotplug_t hotplug);

extern int smscore_register_device(struct smsdevice_params_t *params,
				   struct smscore_device_t **coredev);
extern void smscore_unregister_device(struct smscore_device_t *coredev);

extern int smscore_start_device(struct smscore_device_t *coredev);
extern int smscore_load_firmware(struct smscore_device_t *coredev,
				 char *filename,
				 loadfirmware_t loadfirmware_handler);

extern int smscore_set_device_mode(struct smscore_device_t *coredev, int mode);
extern int smscore_get_device_mode(struct smscore_device_t *coredev);

extern int smscore_register_client(struct smscore_device_t *coredev,
				    struct smsclient_params_t *params,
				    struct smscore_client_t **client);
extern void smscore_unregister_client(struct smscore_client_t *client);

extern int smsclient_sendrequest(struct smscore_client_t *client,
				 void *buffer, size_t size);
extern void smscore_onresponse(struct smscore_device_t *coredev,
			       struct smscore_buffer_t *cb);

extern int smscore_get_common_buffer_size(struct smscore_device_t *coredev);
extern int smscore_map_common_buffer(struct smscore_device_t *coredev,
				      struct vm_area_struct *vma);
extern int smscore_get_fw_filename(struct smscore_device_t *coredev,
				   int mode, char *filename);
extern int smscore_send_fw_file(struct smscore_device_t *coredev,
				u8 *ufwbuf, int size);

extern
struct smscore_buffer_t *smscore_getbuffer(struct smscore_device_t *coredev);
extern void smscore_putbuffer(struct smscore_device_t *coredev,
			      struct smscore_buffer_t *cb);

int smscore_configure_gpio(struct smscore_device_t *coredev, u32 pin,
			   struct smscore_gpio_config *pinconfig);
int smscore_set_gpio(struct smscore_device_t *coredev, u32 pin, int level);

void smscore_set_board_id(struct smscore_device_t *core, int id);
int smscore_get_board_id(struct smscore_device_t *core);

int smscore_led_state(struct smscore_device_t *core, int led);


/* ------------------------------------------------------------------------ */

#define DBG_INFO 1
#define DBG_ADV  2

#define sms_printk(kern, fmt, arg...) \
	printk(kern "%s: " fmt "\n", __func__, ##arg)

#define dprintk(kern, lvl, fmt, arg...) do {\
	if (sms_dbg & lvl) \
		sms_printk(kern, fmt, ##arg); } while (0)

#define sms_log(fmt, arg...) sms_printk(KERN_INFO, fmt, ##arg)
#define sms_err(fmt, arg...) \
	sms_printk(KERN_ERR, "line: %d: " fmt, __LINE__, ##arg)
#define sms_warn(fmt, arg...)  sms_printk(KERN_WARNING, fmt, ##arg)
#define sms_info(fmt, arg...) \
	dprintk(KERN_INFO, DBG_INFO, fmt, ##arg)
#define sms_debug(fmt, arg...) \
	dprintk(KERN_DEBUG, DBG_ADV, fmt, ##arg)


#endif /* __smscoreapi_h__ */
