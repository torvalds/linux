/*
 *  Driver for the Siano SMS10xx USB dongle
 *
 *  Copyright (c) 2008 <TODO: INSERT ALL COPYRIGHT OWNERS and MAINT. NAMES HERE>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
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
#include <asm/scatterlist.h>
#include <asm/page.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"

#include <linux/mutex.h>

typedef struct mutex kmutex_t;

#define kmutex_init(_p_) mutex_init(_p_)
#define kmutex_lock(_p_) mutex_lock(_p_)
#define kmutex_trylock(_p_) mutex_trylock(_p_)
#define kmutex_unlock(_p_) mutex_unlock(_p_)


#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#define SMS_ALLOC_ALIGNMENT					128
#define SMS_DMA_ALIGNMENT					16
#define SMS_ALIGN_ADDRESS(addr) ((((u32)(addr)) + (SMS_DMA_ALIGNMENT-1)) & ~(SMS_DMA_ALIGNMENT-1))

#define SMS_DEVICE_FAMILY2					1
#define SMS_ROM_NO_RESPONSE					2
#define SMS_DEVICE_NOT_READY				0x8000000

typedef struct _smscore_device smscore_device_t;
typedef struct _smscore_client smscore_client_t;
typedef struct _smscore_buffer smscore_buffer_t;

typedef int (*hotplug_t)(smscore_device_t *coredev, struct device *device, int arrival);

typedef int (*setmode_t)(void *context, int mode);
typedef void (*detectmode_t)(void *context, int *mode);
typedef int (*sendrequest_t)(void *context, void *buffer, size_t size);
typedef int (*loadfirmware_t)(void *context, void *buffer, size_t size);
typedef int (*preload_t)(void *context);
typedef int (*postload_t)(void *context);

typedef int (*onresponse_t)(void *context, smscore_buffer_t *cb);
typedef void (*onremove_t)(void *context);

typedef struct _smscore_buffer
{
	// public members, once passed to clients can be changed freely
	struct list_head entry;
	int				size;
	int				offset;

	// private members, read-only for clients
	void			*p;
	dma_addr_t		phys;
	unsigned long	offset_in_common;
} *psmscore_buffer_t;

typedef struct _smsdevice_params
{
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
} smsdevice_params_t;

typedef struct _smsclient_params
{
	int				initial_id;
	int				data_type;
	onresponse_t	onresponse_handler;
	onremove_t		onremove_handler;

	void			*context;
} smsclient_params_t;

// GPIO definitions for antenna frequency domain control (SMS8021)
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
#define MSG_SMS_ISDBT_TUNE_REQ				776
#define MSG_SMS_ISDBT_TUNE_RES				777

#define SMS_INIT_MSG_EX(ptr, type, src, dst, len) do { \
	(ptr)->msgType = type; (ptr)->msgSrcId = src; (ptr)->msgDstId = dst; \
	(ptr)->msgLength = len; (ptr)->msgFlags = 0; \
} while (0)
#define SMS_INIT_MSG(ptr, type, len) SMS_INIT_MSG_EX(ptr, type, 0, HIF_TASK, len)

typedef enum
{
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
} SMS_DEVICE_MODE;

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef int INT32;

typedef struct SmsMsgHdr_S
{
	UINT16	msgType;
	UINT8	msgSrcId;
	UINT8	msgDstId;
	UINT16	msgLength;	// Length is of the entire message, including header
	UINT16	msgFlags;
} SmsMsgHdr_ST;

typedef struct SmsMsgData_S
{
	SmsMsgHdr_ST	xMsgHeader;
	UINT32			msgData[1];
} SmsMsgData_ST;

typedef struct SmsDataDownload_S
{
	SmsMsgHdr_ST	xMsgHeader;
	UINT32			MemAddr;
	UINT8			Payload[SMS_MAX_PAYLOAD_SIZE];
} SmsDataDownload_ST;

typedef struct SmsVersionRes_S
{
	SmsMsgHdr_ST	xMsgHeader;

	UINT16			ChipModel;                    // e.g. 0x1102 for SMS-1102 "Nova"
	UINT8			Step;                         // 0 - Step A
	UINT8			MetalFix;                     // 0 - Metal 0

	UINT8			FirmwareId;                   // 0xFF � ROM, otherwise the value indicated by SMSHOSTLIB_DEVICE_MODES_E
	UINT8			SupportedProtocols;           // Bitwise OR combination of supported protocols

	UINT8			VersionMajor;
	UINT8			VersionMinor;
	UINT8			VersionPatch;
	UINT8			VersionFieldPatch;

	UINT8			RomVersionMajor;
	UINT8			RomVersionMinor;
	UINT8			RomVersionPatch;
	UINT8			RomVersionFieldPatch;

	UINT8			TextLabel[34];
} SmsVersionRes_ST;

typedef struct SmsFirmware_S
{
	UINT32			CheckSum;
	UINT32			Length;
	UINT32			StartAddress;
	UINT8			Payload[1];
} SmsFirmware_ST;

typedef struct SMSHOSTLIB_STATISTICS_S
{
	UINT32 Reserved;				//!< Reserved

	/// Common parameters
	UINT32 IsRfLocked;				//!< 0 - not locked, 1 - locked
	UINT32 IsDemodLocked;			//!< 0 - not locked, 1 - locked
	UINT32 IsExternalLNAOn;			//!< 0 - external LNA off, 1 - external LNA on

	/// Reception quality
	INT32  SNR;						//!< dB
	UINT32 BER;						//!< Post Viterbi BER [1E-5]
	UINT32 FIB_CRC;					//!< CRC errors percentage, valid only for DAB
	UINT32 TS_PER;					//!< Transport stream PER, 0xFFFFFFFF indicate N/A, valid only for DVB-T/H
	UINT32 MFER;					//!< DVB-H frame error rate in percentage, 0xFFFFFFFF indicate N/A, valid only for DVB-H
	INT32  RSSI;					//!< dBm
	INT32  InBandPwr;				//!< In band power in dBM
	INT32  CarrierOffset;			//!< Carrier Offset in bin/1024

	/// Transmission parameters
	UINT32 Frequency;				//!< Frequency in Hz
	UINT32 Bandwidth;				//!< Bandwidth in MHz, valid only for DVB-T/H
	UINT32 TransmissionMode;		//!< Transmission Mode, for DAB modes 1-4, for DVB-T/H FFT mode carriers in Kilos
	UINT32 ModemState;				//!< from SMS_DvbModemState_ET , valid only for DVB-T/H
	UINT32 GuardInterval;			//!< Guard Interval, 1 divided by value , valid only for DVB-T/H
	UINT32 CodeRate;				//!< Code Rate from SMS_DvbModemState_ET, valid only for DVB-T/H
	UINT32 LPCodeRate;				//!< Low Priority Code Rate from SMS_DvbModemState_ET, valid only for DVB-T/H
	UINT32 Hierarchy;				//!< Hierarchy from SMS_Hierarchy_ET, valid only for DVB-T/H
	UINT32 Constellation;			//!< Constellation from SMS_Constellation_ET, valid only for DVB-T/H

	/// Burst parameters, valid only for DVB-H
	UINT32 BurstSize;				//!< Current burst size in bytes, valid only for DVB-H
	UINT32 BurstDuration;			//!< Current burst duration in mSec, valid only for DVB-H
	UINT32 BurstCycleTime;			//!< Current burst cycle time in mSec, valid only for DVB-H
	UINT32 CalculatedBurstCycleTime;//!< Current burst cycle time in mSec, as calculated by demodulator, valid only for DVB-H
	UINT32 NumOfRows;				//!< Number of rows in MPE table, valid only for DVB-H
	UINT32 NumOfPaddCols;			//!< Number of padding columns in MPE table, valid only for DVB-H
	UINT32 NumOfPunctCols;			//!< Number of puncturing columns in MPE table, valid only for DVB-H
	UINT32 ErrorTSPackets;			//!< Number of erroneous transport-stream packets
	UINT32 TotalTSPackets;			//!< Total number of transport-stream packets
	UINT32 NumOfValidMpeTlbs;		//!< Number of MPE tables which do not include errors after MPE RS decoding
	UINT32 NumOfInvalidMpeTlbs;		//!< Number of MPE tables which include errors after MPE RS decoding
	UINT32 NumOfCorrectedMpeTlbs;	//!< Number of MPE tables which were corrected by MPE RS decoding
	/// Common params
	UINT32 BERErrorCount;			//!< Number of errornous SYNC bits.
	UINT32 BERBitCount;				//!< Total number of SYNC bits.

	/// Interface information
	UINT32 SmsToHostTxErrors;		//!< Total number of transmission errors.

	/// DAB/T-DMB
	UINT32 PreBER; 					//!< DAB/T-DMB only: Pre Viterbi BER [1E-5]

	/// DVB-H TPS parameters
	UINT32 CellId;					//!< TPS Cell ID in bits 15..0, bits 31..16 zero; if set to 0xFFFFFFFF cell_id not yet recovered

} SMSHOSTLIB_STATISTICS_ST;

typedef struct
{
	UINT32 RequestResult;

	SMSHOSTLIB_STATISTICS_ST Stat;

	// Split the calc of the SNR in DAB
	UINT32 Signal;				//!< dB
	UINT32 Noise;				//!< dB

} SmsMsgStatisticsInfo_ST;

typedef struct SMSHOSTLIB_ISDBT_LAYER_STAT_S
{
	// Per-layer information
	UINT32 CodeRate;			//!< Code Rate from SMSHOSTLIB_CODE_RATE_ET, 255 means layer does not exist
	UINT32 Constellation;		//!< Constellation from SMSHOSTLIB_CONSTELLATION_ET, 255 means layer does not exist
	UINT32 BER;					//!< Post Viterbi BER [1E-5], 0xFFFFFFFF indicate N/A
	UINT32 BERErrorCount;		//!< Post Viterbi Error Bits Count
	UINT32 BERBitCount;			//!< Post Viterbi Total Bits Count
	UINT32 PreBER; 				//!< Pre Viterbi BER [1E-5], 0xFFFFFFFF indicate N/A
	UINT32 TS_PER;				//!< Transport stream PER [%], 0xFFFFFFFF indicate N/A
	UINT32 ErrorTSPackets;		//!< Number of erroneous transport-stream packets
	UINT32 TotalTSPackets;		//!< Total number of transport-stream packets
	UINT32 TILdepthI;			//!< Time interleaver depth I parameter, 255 means layer does not exist
	UINT32 NumberOfSegments;	//!< Number of segments in layer A, 255 means layer does not exist
	UINT32 TMCCErrors;			//!< TMCC errors
} SMSHOSTLIB_ISDBT_LAYER_STAT_ST;

typedef struct SMSHOSTLIB_STATISTICS_ISDBT_S
{
	UINT32 StatisticsType;			//!< Enumerator identifying the type of the structure.  Values are the same as SMSHOSTLIB_DEVICE_MODES_E
									//!< This fiels MUST always first in any statistics structure

	UINT32 FullSize;				//!< Total size of the structure returned by the modem.  If the size requested by
									//!< the host is smaller than FullSize, the struct will be truncated

	// Common parameters
	UINT32 IsRfLocked;				//!< 0 - not locked, 1 - locked
	UINT32 IsDemodLocked;			//!< 0 - not locked, 1 - locked
	UINT32 IsExternalLNAOn;			//!< 0 - external LNA off, 1 - external LNA on

	// Reception quality
	INT32  SNR;						//!< dB
	INT32  RSSI;					//!< dBm
	INT32  InBandPwr;				//!< In band power in dBM
	INT32  CarrierOffset;			//!< Carrier Offset in Hz

	// Transmission parameters
	UINT32 Frequency;				//!< Frequency in Hz
	UINT32 Bandwidth;				//!< Bandwidth in MHz
	UINT32 TransmissionMode;		//!< ISDB-T transmission mode
	UINT32 ModemState;				//!< 0 - Acquisition, 1 - Locked
	UINT32 GuardInterval;			//!< Guard Interval, 1 divided by value
	UINT32 SystemType;				//!< ISDB-T system type (ISDB-T / ISDB-Tsb)
	UINT32 PartialReception;		//!< TRUE - partial reception, FALSE otherwise
	UINT32 NumOfLayers;				//!< Number of ISDB-T layers in the network

	// Per-layer information
	// Layers A, B and C
	SMSHOSTLIB_ISDBT_LAYER_STAT_ST	LayerInfo[3];	//!< Per-layer statistics, see SMSHOSTLIB_ISDBT_LAYER_STAT_ST

	// Interface information
	UINT32 SmsToHostTxErrors;		//!< Total number of transmission errors.

} SMSHOSTLIB_STATISTICS_ISDBT_ST;

typedef struct SMSHOSTLIB_STATISTICS_DVB_S
{
	UINT32 StatisticsType;			//!< Enumerator identifying the type of the structure.  Values are the same as SMSHOSTLIB_DEVICE_MODES_E
								//!< This fiels MUST always first in any statistics structure

	UINT32 FullSize;				//!< Total size of the structure returned by the modem.  If the size requested by
									//!< the host is smaller than FullSize, the struct will be truncated
	// Common parameters
	UINT32 IsRfLocked;				//!< 0 - not locked, 1 - locked
	UINT32 IsDemodLocked;			//!< 0 - not locked, 1 - locked
	UINT32 IsExternalLNAOn;			//!< 0 - external LNA off, 1 - external LNA on

	// Reception quality
	INT32  SNR;						//!< dB
	UINT32 BER;						//!< Post Viterbi BER [1E-5]
	UINT32 BERErrorCount;			//!< Number of errornous SYNC bits.
	UINT32 BERBitCount;				//!< Total number of SYNC bits.
	UINT32 TS_PER;					//!< Transport stream PER, 0xFFFFFFFF indicate N/A
	UINT32 MFER;					//!< DVB-H frame error rate in percentage, 0xFFFFFFFF indicate N/A, valid only for DVB-H
	INT32  RSSI;					//!< dBm
	INT32  InBandPwr;				//!< In band power in dBM
	INT32  CarrierOffset;			//!< Carrier Offset in bin/1024

	// Transmission parameters
	UINT32 Frequency;				//!< Frequency in Hz
	UINT32 Bandwidth;				//!< Bandwidth in MHz
	UINT32 ModemState;				//!< from SMSHOSTLIB_DVB_MODEM_STATE_ET
	UINT32 TransmissionMode;		//!< FFT mode carriers in Kilos
	UINT32 GuardInterval;			//!< Guard Interval, 1 divided by value
	UINT32 CodeRate;				//!< Code Rate from SMSHOSTLIB_CODE_RATE_ET
	UINT32 LPCodeRate;				//!< Low Priority Code Rate from SMSHOSTLIB_CODE_RATE_ET
	UINT32 Hierarchy;				//!< Hierarchy from SMSHOSTLIB_HIERARCHY_ET
	UINT32 Constellation;			//!< Constellation from SMSHOSTLIB_CONSTELLATION_ET

	// Burst parameters, valid only for DVB-H
	UINT32 BurstSize;				//!< Current burst size in bytes, valid only for DVB-H
	UINT32 BurstDuration;			//!< Current burst duration in mSec, valid only for DVB-H
	UINT32 BurstCycleTime;			//!< Current burst cycle time in mSec, valid only for DVB-H
	UINT32 CalculatedBurstCycleTime;//!< Current burst cycle time in mSec, as calculated by demodulator, valid only for DVB-H
	UINT32 NumOfRows;				//!< Number of rows in MPE table, valid only for DVB-H
	UINT32 NumOfPaddCols;			//!< Number of padding columns in MPE table, valid only for DVB-H
	UINT32 NumOfPunctCols;			//!< Number of puncturing columns in MPE table, valid only for DVB-H
	UINT32 ErrorTSPackets;			//!< Number of erroneous transport-stream packets
	UINT32 TotalTSPackets;			//!< Total number of transport-stream packets
	UINT32 NumOfValidMpeTlbs;		//!< Number of MPE tables which do not include errors after MPE RS decoding, valid only for DVB-H
	UINT32 NumOfInvalidMpeTlbs;		//!< Number of MPE tables which include errors after MPE RS decoding, valid only for DVB-H
	UINT32 NumOfCorrectedMpeTlbs;	//!< Number of MPE tables which were corrected by MPE RS decoding, valid only for DVB-H
	UINT32 NumMPEReceived;			//!< DVB-H, Num MPE section received

	// DVB-H TPS parameters
	UINT32 CellId;					//!< TPS Cell ID in bits 15..0, bits 31..16 zero; if set to 0xFFFFFFFF cell_id not yet recovered
	UINT32 DvbhSrvIndHP;			//!< DVB-H service indication info, bit 1 - Time Slicing indicator, bit 0 - MPE-FEC indicator
	UINT32 DvbhSrvIndLP;			//!< DVB-H service indication info, bit 1 - Time Slicing indicator, bit 0 - MPE-FEC indicator

	// Interface information
	UINT32 SmsToHostTxErrors;		//!< Total number of transmission errors.

} SMSHOSTLIB_STATISTICS_DVB_ST;

typedef struct SMSHOSTLIB_GPIO_CONFIG_S
{
	UINT8	Direction;				//!< GPIO direction: Input - 0, Output - 1
	UINT8	PullUpDown;				//!< PullUp/PullDown: None - 0, PullDown - 1, PullUp - 2, Keeper - 3
	UINT8	InputCharacteristics;	//!< Input Characteristics: Normal - 0, Schmitt trigger - 1
	UINT8	OutputSlewRate;			//!< Output Slew Rate:	Fast slew rate - 0, Slow slew rate - 1
	UINT8	OutputDriving;			//!< Output driving capability: 4mA - 0, 8mA - 1, 12mA - 2, 16mA - 3
} SMSHOSTLIB_GPIO_CONFIG_ST;

typedef struct SMSHOSTLIB_I2C_REQ_S
{
	UINT32	DeviceAddress;			// I2c device address
	UINT32	WriteCount;				// number of bytes to write
	UINT32	ReadCount;				// number of bytes to read
	UINT8	Data[1];
} SMSHOSTLIB_I2C_REQ_ST;

typedef struct SMSHOSTLIB_I2C_RES_S
{
	UINT32	Status;					// non-zero value in case of failure
	UINT32	ReadCount;				// number of bytes read
	UINT8	Data[1];
} SMSHOSTLIB_I2C_RES_ST;

typedef struct _smsdvb_client
{
	struct list_head entry;

	smscore_device_t	*coredev;
	smscore_client_t	*smsclient;

	struct dvb_adapter	adapter;
	struct dvb_demux	demux;
	struct dmxdev		dmxdev;
	struct dvb_frontend	frontend;

	fe_status_t			fe_status;
	int					fe_ber, fe_snr, fe_signal_strength;

	struct completion	tune_done, stat_done;

	// todo: save freq/band instead whole struct
	struct dvb_frontend_parameters fe_params;

} smsdvb_client_t;

extern void smscore_registry_setmode(char *devpath, int mode);
extern int smscore_registry_getmode(char *devpath);

extern int smscore_register_hotplug(hotplug_t hotplug);
extern void smscore_unregister_hotplug(hotplug_t hotplug);

extern int smscore_register_device(smsdevice_params_t *params, smscore_device_t **coredev);
extern void smscore_unregister_device(smscore_device_t *coredev);

extern int smscore_start_device(smscore_device_t *coredev);
extern int smscore_load_firmware(smscore_device_t *coredev, char* filename, loadfirmware_t loadfirmware_handler);

extern int smscore_set_device_mode(smscore_device_t *coredev, int mode);
extern int smscore_get_device_mode(smscore_device_t *coredev);

extern int smscore_register_client(smscore_device_t *coredev, smsclient_params_t* params, smscore_client_t **client);
extern void smscore_unregister_client(smscore_client_t *client);

extern int smsclient_sendrequest(smscore_client_t *client, void *buffer, size_t size);
extern void smscore_onresponse(smscore_device_t *coredev, smscore_buffer_t *cb);

extern int smscore_get_common_buffer_size(smscore_device_t *coredev);
extern int smscore_map_common_buffer(smscore_device_t *coredev, struct vm_area_struct * vma);

extern smscore_buffer_t *smscore_getbuffer(smscore_device_t *coredev);
extern void smscore_putbuffer(smscore_device_t *coredev, smscore_buffer_t *cb);

/* smsdvb.c */
int smsdvb_register(void);
void smsdvb_unregister(void);

/* smsusb.c */
int smsusb_register(void);
void smsusb_unregister(void);

#endif // __smscoreapi_h__
